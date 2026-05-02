/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "heif_image_handle.h"
#include "api_structs.h"
#include "box.h"
#if WITH_UNCOMPRESSED_CODEC
#include "codecs/uncompressed/unc_boxes.h"
#include "codecs/uncompressed/unc_codec.h"
#endif
#include <climits>
#include <cstring>
#include <string>
#include <memory>
#include <vector>


void heif_image_handle_release(const heif_image_handle* handle)
{
  delete handle;
}


int heif_image_handle_is_primary_image(const heif_image_handle* handle)
{
  return handle->image->is_primary();
}


heif_item_id heif_image_handle_get_item_id(const heif_image_handle* handle)
{
  return handle->image->get_id();
}


int heif_image_handle_get_width(const heif_image_handle* handle)
{
  if (handle && handle->image) {
    uint32_t w = handle->image->get_width();
    if (w > INT_MAX) {
      return 0;
    }
    else {
      return static_cast<int>(w);
    }
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_height(const heif_image_handle* handle)
{
  if (handle && handle->image) {
    uint32_t h = handle->image->get_height();
    if (h > INT_MAX) {
      return 0;
    }
    else {
      return static_cast<int>(h);
    }
  }
  else {
    return 0;
  }
}


int heif_image_handle_has_alpha_channel(const heif_image_handle* handle)
{
  // TODO: for now, also scan the grid tiles for alpha information (issue #708), but depending about
  // how the discussion about this structure goes forward, we might remove this again.

  return handle->context->has_alpha(handle->image->get_id()); // handle case in issue #708
  //return handle->image->get_alpha_channel() != nullptr;       // old alpha check that fails on alpha in grid tiles
}


int heif_image_handle_is_premultiplied_alpha(const heif_image_handle* handle)
{
  // TODO: what about images that have the alpha in the grid tiles (issue #708) ?
  return handle->image->is_premultiplied_alpha();
}


int heif_image_handle_get_luma_bits_per_pixel(const heif_image_handle* handle)
{
  return handle->image->get_luma_bits_per_pixel();
}


int heif_image_handle_get_chroma_bits_per_pixel(const heif_image_handle* handle)
{
  return handle->image->get_chroma_bits_per_pixel();
}


heif_error heif_image_handle_get_preferred_decoding_colorspace(const heif_image_handle* image_handle,
                                                               heif_colorspace* out_colorspace,
                                                               heif_chroma* out_chroma)
{
  Error err = image_handle->image->get_coded_image_colorspace(out_colorspace, out_chroma);
  if (err) {
    return err.error_struct(image_handle->image.get());
  }

  return heif_error_success;
}


int heif_image_handle_get_ispe_width(const heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_width();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_ispe_height(const heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_height();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_pixel_aspect_ratio(const heif_image_handle* handle, uint32_t* aspect_h, uint32_t* aspect_v)
{
  auto pasp = handle->image->get_property<Box_pasp>();
  if (pasp) {
    *aspect_h = pasp->hSpacing;
    *aspect_v = pasp->vSpacing;
    return 1;
  }
  else {
    *aspect_h = 1;
    *aspect_v = 1;
    return 0;
  }
}


heif_context* heif_image_handle_get_context(const heif_image_handle* handle)
{
  auto ctx = new heif_context();
  ctx->context = handle->context;
  return ctx;
}


const char* heif_image_handle_get_gimi_content_id(const heif_image_handle* handle)
{
  if (!handle->image->has_gimi_sample_content_id()) {
    return nullptr;
  }

  std::string id = handle->image->get_gimi_sample_content_id();
  char* idstring = new char[id.size() + 1];
  strcpy(idstring, id.c_str());
  return idstring;
}


void heif_image_handle_set_gimi_content_id(heif_image_handle* handle, const char* content_id)
{
  auto gimi_box = std::make_shared<Box_gimi_content_id>();
  gimi_box->set_content_id(content_id);
  handle->context->add_property(handle->image->get_id(), gimi_box, false);
  handle->image->set_gimi_sample_content_id(content_id);
}


#if WITH_UNCOMPRESSED_CODEC
static std::shared_ptr<Box_cmpd> get_effective_cmpd(const heif_image_handle* handle)
{
  // Try explicit cmpd property first.
  auto cmpd = handle->image->get_property<Box_cmpd>();
  if (cmpd) {
    return cmpd;
  }

  // For profile-based uncC (version 1), the cmpd is synthesized inside the uncC box.
  auto uncC = handle->image->get_property<Box_uncC>();
  if (uncC) {
    fill_uncC_and_cmpd_from_profile(uncC, cmpd);
  }
  return cmpd;
}

// Returns the uncC box, with the components vector populated even for
// profile-based (version-1) uncC. fill_uncC_and_cmpd_from_profile() expands
// the profile fourcc into the uncC components.
static std::shared_ptr<Box_uncC> get_effective_uncC(const heif_image_handle* handle)
{
  auto uncC = handle->image->get_property<Box_uncC>();
  if (!uncC) {
    return nullptr;
  }
  std::shared_ptr<Box_cmpd> cmpd;
  fill_uncC_and_cmpd_from_profile(uncC, cmpd);
  return uncC;
}
#endif


uint32_t heif_image_handle_get_number_of_cmpd_components(const heif_image_handle* handle)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return 0;
  }
  auto cmpd = get_effective_cmpd(handle);
  if (!cmpd) {
    return 0;
  }
  return static_cast<uint32_t>(cmpd->get_components().size());
#else
  return 0;
#endif
}


uint16_t heif_image_handle_get_cmpd_component_type(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return 0;
  }
  auto cmpd = get_effective_cmpd(handle);
  if (!cmpd) {
    return 0;
  }
  const auto& components = cmpd->get_components();
  if (component_idx >= components.size()) {
    return 0;
  }
  return components[component_idx].component_type;
#else
  return 0;
#endif
}


const char* heif_image_handle_get_cmpd_component_type_uri(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return nullptr;
  }
  auto cmpd = get_effective_cmpd(handle);
  if (!cmpd) {
    return nullptr;
  }
  const auto& components = cmpd->get_components();
  if (component_idx >= components.size()) {
    return nullptr;
  }
  const auto& uri = components[component_idx].component_type_uri;
  if (uri.empty()) {
    return nullptr;
  }
  char* uristring = new char[uri.size() + 1];
  strcpy(uristring, uri.c_str());
  return uristring;
#else
  return nullptr;
#endif
}


// --- handle-side per-component getters (unci images, indexed 0..N-1 in uncC order)
//
// These do NOT use the opaque component-ID system that the decoded heif_image
// exposes via heif_image_get_used_component_ids(). The decoder assigns those
// IDs at decode time (starting from 1, with extra IDs allocated for cpat
// reference components), so they cannot be predicted from the file alone.

uint32_t heif_image_handle_get_number_of_components(const heif_image_handle* handle)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return 0;
  }
  auto uncC = get_effective_uncC(handle);
  if (!uncC) {
    return 0;
  }
  return static_cast<uint32_t>(uncC->get_components().size());
#else
  return 0;
#endif
}


uint16_t heif_image_handle_get_component_type(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return 0;
  }
  auto uncC = get_effective_uncC(handle);
  auto cmpd = get_effective_cmpd(handle);
  if (!uncC || !cmpd) {
    return 0;
  }
  const auto& uc = uncC->get_components();
  if (component_idx >= uc.size()) {
    return 0;
  }
  const auto& cd = cmpd->get_components();
  uint32_t cmpd_idx = uc[component_idx].component_index;
  if (cmpd_idx >= cd.size()) {
    return 0;
  }
  return cd[cmpd_idx].component_type;
#else
  return 0;
#endif
}


int heif_image_handle_get_component_bits_per_pixel(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return -1;
  }
  auto uncC = get_effective_uncC(handle);
  if (!uncC) {
    return -1;
  }
  const auto& uc = uncC->get_components();
  if (component_idx >= uc.size()) {
    return -1;
  }
  return static_cast<int>(uc[component_idx].component_bit_depth);
#else
  return -1;
#endif
}


heif_component_datatype heif_image_handle_get_component_datatype(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return heif_component_datatype_undefined;
  }
  auto uncC = get_effective_uncC(handle);
  if (!uncC) {
    return heif_component_datatype_undefined;
  }
  const auto& uc = uncC->get_components();
  if (component_idx >= uc.size()) {
    return heif_component_datatype_undefined;
  }
  return unc_component_format_to_datatype(uc[component_idx].component_format);
#else
  return heif_component_datatype_undefined;
#endif
}


int heif_image_handle_has_gimi_component_content_ids(const heif_image_handle* handle)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return 0;
  }
  auto box = handle->image->get_property<Box_gimi_component_content_ids>();
  if (!box) {
    return 0;
  }
  return static_cast<int>(box->get_content_ids().size());
#else
  return 0;
#endif
}


const char* heif_image_handle_get_gimi_component_content_id(const heif_image_handle* handle, uint32_t component_idx)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle) {
    return nullptr;
  }
  auto box = handle->image->get_property<Box_gimi_component_content_ids>();
  if (!box) {
    return nullptr;
  }
  const auto& ids = box->get_content_ids();
  if (component_idx >= ids.size()) {
    return nullptr;
  }
  char* idstring = new char[ids[component_idx].size() + 1];
  strcpy(idstring, ids[component_idx].c_str());
  return idstring;
#else
  return nullptr;
#endif
}


void heif_image_handle_set_gimi_component_content_id(heif_image_handle* handle,
                                                     uint32_t component_idx,
                                                     const char* content_id)
{
#if WITH_UNCOMPRESSED_CODEC
  if (!handle || !content_id) {
    return;
  }

  auto box = handle->image->get_property<Box_gimi_component_content_ids>();
  if (!box) {
    // Create a new box and add it as property.
    auto new_box = std::make_shared<Box_gimi_component_content_ids>();
    std::vector<std::string> ids(component_idx + 1);
    ids[component_idx] = content_id;
    new_box->set_content_ids(ids);
    handle->context->add_property(handle->image->get_id(), new_box, false);
  }
  else {
    // Mutate the existing box in-place.
    auto ids = box->get_content_ids();
    if (component_idx >= ids.size()) {
      ids.resize(component_idx + 1);
    }
    ids[component_idx] = content_id;
    box->set_content_ids(ids);
  }
#endif
}
