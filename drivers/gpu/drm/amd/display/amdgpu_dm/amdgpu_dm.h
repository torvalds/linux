/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __AMDGPU_DM_H__
#define __AMDGPU_DM_H__

#include <drm/drmP.h>
#include <drm/drm_atomic.h>

/*
 * This file contains the definition for amdgpu_display_manager
 * and its API for amdgpu driver's use.
 * This component provides all the display related functionality
 * and this is the only component that calls DAL API.
 * The API contained here intended for amdgpu driver use.
 * The API that is called directly from KMS framework is located
 * in amdgpu_dm_kms.h file
 */

#define AMDGPU_DM_MAX_DISPLAY_INDEX 31
/*
#include "include/amdgpu_dal_power_if.h"
#include "amdgpu_dm_irq.h"
*/

#include "irq_types.h"
#include "signal_types.h"

/* Forward declarations */
struct amdgpu_device;
struct drm_device;
struct amdgpu_dm_irq_handler_data;
struct dc;

struct amdgpu_dm_prev_state {
	struct drm_framebuffer *fb;
	int32_t x;
	int32_t y;
	struct drm_display_mode mode;
};

struct common_irq_params {
	struct amdgpu_device *adev;
	enum dc_irq_source irq_src;
};

struct irq_list_head {
	struct list_head head;
	/* In case this interrupt needs post-processing, 'work' will be queued*/
	struct work_struct work;
};

#if defined(CONFIG_DRM_AMD_DC_FBC)
struct dm_comressor_info {
	void *cpu_addr;
	struct amdgpu_bo *bo_ptr;
	uint64_t gpu_addr;
};
#endif


struct amdgpu_display_manager {
	struct dal *dal;
	struct dc *dc;
	struct cgs_device *cgs_device;

	struct amdgpu_device *adev;	/*AMD base driver*/
	struct drm_device *ddev;	/*DRM base driver*/
	u16 display_indexes_num;

	struct amdgpu_dm_prev_state prev_state;

	/*
	 * 'irq_source_handler_table' holds a list of handlers
	 * per (DAL) IRQ source.
	 *
	 * Each IRQ source may need to be handled at different contexts.
	 * By 'context' we mean, for example:
	 * - The ISR context, which is the direct interrupt handler.
	 * - The 'deferred' context - this is the post-processing of the
	 *	interrupt, but at a lower priority.
	 *
	 * Note that handlers are called in the same order as they were
	 * registered (FIFO).
	 */
	struct irq_list_head irq_handler_list_low_tab[DAL_IRQ_SOURCES_NUMBER];
	struct list_head irq_handler_list_high_tab[DAL_IRQ_SOURCES_NUMBER];

	struct common_irq_params
	pflip_params[DC_IRQ_SOURCE_PFLIP_LAST - DC_IRQ_SOURCE_PFLIP_FIRST + 1];

	struct common_irq_params
	vblank_params[DC_IRQ_SOURCE_VBLANK6 - DC_IRQ_SOURCE_VBLANK1 + 1];

	/* this spin lock synchronizes access to 'irq_handler_list_table' */
	spinlock_t irq_handler_list_table_lock;

	struct backlight_device *backlight_dev;

	const struct dc_link *backlight_link;

	struct work_struct mst_hotplug_work;

	struct mod_freesync *freesync_module;

	/**
	 * Caches device atomic state for suspend/resume
	 */
	struct drm_atomic_state *cached_state;
#if defined(CONFIG_DRM_AMD_DC_FBC)
	struct dm_comressor_info compressor;
#endif
};

struct amdgpu_dm_connector {

	struct drm_connector base;
	uint32_t connector_id;

	/* we need to mind the EDID between detect
	   and get modes due to analog/digital/tvencoder */
	struct edid *edid;

	/* shared with amdgpu */
	struct amdgpu_hpd hpd;

	/* number of modes generated from EDID at 'dc_sink' */
	int num_modes;

	/* The 'old' sink - before an HPD.
	 * The 'current' sink is in dc_link->sink. */
	struct dc_sink *dc_sink;
	struct dc_link *dc_link;
	struct dc_sink *dc_em_sink;

	/* DM only */
	struct drm_dp_mst_topology_mgr mst_mgr;
	struct amdgpu_dm_dp_aux dm_dp_aux;
	struct drm_dp_mst_port *port;
	struct amdgpu_dm_connector *mst_port;
	struct amdgpu_encoder *mst_encoder;

	/* TODO see if we can merge with ddc_bus or make a dm_connector */
	struct amdgpu_i2c_adapter *i2c;

	/* Monitor range limits */
	int min_vfreq ;
	int max_vfreq ;
	int pixel_clock_mhz;

	/*freesync caps*/
	struct mod_freesync_caps caps;

	struct mutex hpd_lock;

	bool fake_enable;

	bool mst_connected;
};

#define to_amdgpu_dm_connector(x) container_of(x, struct amdgpu_dm_connector, base)

extern const struct amdgpu_ip_block_version dm_ip_block;

struct amdgpu_framebuffer;
struct amdgpu_display_manager;
struct dc_validation_set;
struct dc_plane_state;

struct dm_plane_state {
	struct drm_plane_state base;
	struct dc_plane_state *dc_state;
};

struct dm_crtc_state {
	struct drm_crtc_state base;
	struct dc_stream_state *stream;

	int crc_skip_count;
	bool crc_enabled;
};

#define to_dm_crtc_state(x)    container_of(x, struct dm_crtc_state, base)

struct dm_atomic_state {
	struct drm_atomic_state base;

	struct dc_state *context;
};

#define to_dm_atomic_state(x) container_of(x, struct dm_atomic_state, base)

struct dm_connector_state {
	struct drm_connector_state base;

	enum amdgpu_rmx_type scaling;
	uint8_t underscan_vborder;
	uint8_t underscan_hborder;
	bool underscan_enable;
	struct mod_freesync_user_enable user_enable;
};

#define to_dm_connector_state(x)\
	container_of((x), struct dm_connector_state, base)

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector);
struct drm_connector_state *
amdgpu_dm_connector_atomic_duplicate_state(struct drm_connector *connector);
int amdgpu_dm_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t val);

int amdgpu_dm_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val);

int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev);

void amdgpu_dm_connector_init_helper(struct amdgpu_display_manager *dm,
				     struct amdgpu_dm_connector *aconnector,
				     int connector_type,
				     struct dc_link *link,
				     int link_index);

int amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   struct drm_display_mode *mode);

void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector);

void amdgpu_dm_add_sink_to_freesync_module(struct drm_connector *connector,
					   struct edid *edid);

void
amdgpu_dm_remove_sink_from_freesync_module(struct drm_connector *connector);

/* amdgpu_dm_crc.c */
#ifdef CONFIG_DEBUG_FS
int amdgpu_dm_crtc_set_crc_source(struct drm_crtc *crtc, const char *src_name,
				  size_t *values_cnt);
void amdgpu_dm_crtc_handle_crc_irq(struct drm_crtc *crtc);
#else
#define amdgpu_dm_crtc_set_crc_source NULL
#define amdgpu_dm_crtc_handle_crc_irq(x)
#endif

#define MAX_COLOR_LUT_ENTRIES 4096
/* Legacy gamm LUT users such as X doesn't like large LUT sizes */
#define MAX_COLOR_LEGACY_LUT_ENTRIES 256

void amdgpu_dm_init_color_mod(void);
int amdgpu_dm_set_degamma_lut(struct drm_crtc_state *crtc_state,
			      struct dc_plane_state *dc_plane_state);
void amdgpu_dm_set_ctm(struct dm_crtc_state *crtc);
int amdgpu_dm_set_regamma_lut(struct dm_crtc_state *crtc);

extern const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs;

#endif /* __AMDGPU_DM_H__ */
