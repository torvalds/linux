/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef __AMDGPU_DM_HELPERS_H__
#define __AMDGPU_DM_HELPERS_H__

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
#include <drm/drm_edid.h>

struct amdgpu_dm_connector;
struct dc_link;
struct dc_context;
struct dc_stream_state;
struct dc_edid_caps;
struct dc_dp_mst_stream_allocation_table;
struct drm_dp_aux;
struct drm_dp_mst_atomic_payload;
struct drm_dp_mst_topology_mgr;
struct drm_dp_mst_topology_state;

/* Exported for KUnit testing */
u32 edid_extract_panel_id(struct edid *edid);
void apply_edid_quirks(struct dc_link *link, struct edid *edid,
		       struct dc_edid_caps *edid_caps);
uint8_t get_max_frl_rate(uint8_t max_lanes, uint8_t max_rate_per_lane);
uint8_t get_dsc_max_slices(uint8_t max_slices, int clk_per_slice);
bool dm_is_freesync_pcon_whitelist(const uint32_t branch_dev_id);
extern const uint32_t dm_freesync_pcon_whitelist[];
uint32_t dm_freesync_pcon_whitelist_count(void);
void fill_dc_mst_payload_table_from_drm(struct dc_link *link,
					 bool enable,
					 struct drm_dp_mst_atomic_payload *target_payload,
					 struct dc_dp_mst_stream_allocation_table *table);
void dm_helpers_construct_old_payload(struct drm_dp_mst_topology_mgr *mgr,
				      struct drm_dp_mst_topology_state *mst_state,
				      struct drm_dp_mst_atomic_payload *new_payload,
				      struct drm_dp_mst_atomic_payload *old_payload);
bool dm_helpers_dp_write_dsc_enable(struct dc_context *ctx,
				    const struct dc_stream_state *stream,
				    bool enable);
uint dm_helpers_get_dc_debug_mask(void);
void dm_helpers_set_dc_debug_mask(uint debug_mask);
int dm_helpers_probe_acpi_edid(void *data, u8 *buf, unsigned int block, size_t len);
const struct drm_edid *dm_helpers_read_acpi_edid(struct amdgpu_dm_connector *aconnector);
const struct drm_edid *dm_helpers_read_vbios_hardcoded_edid(struct dc_link *link,
							    struct amdgpu_dm_connector *aconnector);
bool execute_synaptics_rc_command(struct drm_dp_aux *aux,
				  bool is_write_cmd,
				  unsigned char cmd,
				  unsigned int length,
				  unsigned int offset,
				  unsigned char *data);
void apply_synaptics_fifo_reset_wa(struct drm_dp_aux *aux);
uint8_t write_dsc_enable_synaptics_non_virtual_dpcd_mst(struct drm_dp_aux *aux,
							const struct dc_stream_state *stream,
							bool enable);
#endif /* CONFIG_DRM_AMD_DC_KUNIT_TEST */

#endif /* __AMDGPU_DM_HELPERS_H__ */
