/*
 * Copyright (C) 2015-2020 Advanced Micro Devices, Inc. All rights reserved.
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

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include "link_service_types.h"

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

#define AMDGPU_DM_MAX_CRTC 6

#define AMDGPU_DM_MAX_NUM_EDP 2

#define AMDGPU_DMUB_NOTIFICATION_MAX 5

/*
#include "include/amdgpu_dal_power_if.h"
#include "amdgpu_dm_irq.h"
*/

#include "irq_types.h"
#include "signal_types.h"
#include "amdgpu_dm_crc.h"
#include "mod_info_packet.h"
struct aux_payload;
struct set_config_cmd_payload;
enum aux_return_code_type;
enum set_config_status;

/* Forward declarations */
struct amdgpu_device;
struct amdgpu_crtc;
struct drm_device;
struct dc;
struct amdgpu_bo;
struct dmub_srv;
struct dc_plane_state;
struct dmub_notification;

struct common_irq_params {
	struct amdgpu_device *adev;
	enum dc_irq_source irq_src;
	atomic64_t previous_timestamp;
};

/**
 * struct dm_compressor_info - Buffer info used by frame buffer compression
 * @cpu_addr: MMIO cpu addr
 * @bo_ptr: Pointer to the buffer object
 * @gpu_addr: MMIO gpu addr
 */
struct dm_compressor_info {
	void *cpu_addr;
	struct amdgpu_bo *bo_ptr;
	uint64_t gpu_addr;
};

typedef void (*dmub_notify_interrupt_callback_t)(struct amdgpu_device *adev, struct dmub_notification *notify);

/**
 * struct dmub_hpd_work - Handle time consuming work in low priority outbox IRQ
 *
 * @handle_hpd_work: Work to be executed in a separate thread to handle hpd_low_irq
 * @dmub_notify:  notification for callback function
 * @adev: amdgpu_device pointer
 */
struct dmub_hpd_work {
	struct work_struct handle_hpd_work;
	struct dmub_notification *dmub_notify;
	struct amdgpu_device *adev;
};

/**
 * struct vblank_control_work - Work data for vblank control
 * @work: Kernel work data for the work event
 * @dm: amdgpu display manager device
 * @acrtc: amdgpu CRTC instance for which the event has occurred
 * @stream: DC stream for which the event has occurred
 * @enable: true if enabling vblank
 */
struct vblank_control_work {
	struct work_struct work;
	struct amdgpu_display_manager *dm;
	struct amdgpu_crtc *acrtc;
	struct dc_stream_state *stream;
	bool enable;
};

/**
 * struct amdgpu_dm_backlight_caps - Information about backlight
 *
 * Describe the backlight support for ACPI or eDP AUX.
 */
struct amdgpu_dm_backlight_caps {
	/**
	 * @ext_caps: Keep the data struct with all the information about the
	 * display support for HDR.
	 */
	union dpcd_sink_ext_caps *ext_caps;
	/**
	 * @aux_min_input_signal: Min brightness value supported by the display
	 */
	u32 aux_min_input_signal;
	/**
	 * @aux_max_input_signal: Max brightness value supported by the display
	 * in nits.
	 */
	u32 aux_max_input_signal;
	/**
	 * @min_input_signal: minimum possible input in range 0-255.
	 */
	int min_input_signal;
	/**
	 * @max_input_signal: maximum possible input in range 0-255.
	 */
	int max_input_signal;
	/**
	 * @caps_valid: true if these values are from the ACPI interface.
	 */
	bool caps_valid;
	/**
	 * @aux_support: Describes if the display supports AUX backlight.
	 */
	bool aux_support;
};

/**
 * struct dal_allocation - Tracks mapped FB memory for SMU communication
 * @list: list of dal allocations
 * @bo: GPU buffer object
 * @cpu_ptr: CPU virtual address of the GPU buffer object
 * @gpu_addr: GPU virtual address of the GPU buffer object
 */
struct dal_allocation {
	struct list_head list;
	struct amdgpu_bo *bo;
	void *cpu_ptr;
	u64 gpu_addr;
};

/**
 * struct hpd_rx_irq_offload_work_queue - Work queue to handle hpd_rx_irq
 * offload work
 */
struct hpd_rx_irq_offload_work_queue {
	/**
	 * @wq: workqueue structure to queue offload work.
	 */
	struct workqueue_struct *wq;
	/**
	 * @offload_lock: To protect fields of offload work queue.
	 */
	spinlock_t offload_lock;
	/**
	 * @is_handling_link_loss: Used to prevent inserting link loss event when
	 * we're handling link loss
	 */
	bool is_handling_link_loss;
	/**
	 * @aconnector: The aconnector that this work queue is attached to
	 */
	struct amdgpu_dm_connector *aconnector;
};

/**
 * struct hpd_rx_irq_offload_work - hpd_rx_irq offload work structure
 */
struct hpd_rx_irq_offload_work {
	/**
	 * @work: offload work
	 */
	struct work_struct work;
	/**
	 * @data: reference irq data which is used while handling offload work
	 */
	union hpd_irq_data data;
	/**
	 * @offload_wq: offload work queue that this work is queued to
	 */
	struct hpd_rx_irq_offload_work_queue *offload_wq;
};

/**
 * struct amdgpu_display_manager - Central amdgpu display manager device
 *
 * @dc: Display Core control structure
 * @adev: AMDGPU base driver structure
 * @ddev: DRM base driver structure
 * @display_indexes_num: Max number of display streams supported
 * @irq_handler_list_table_lock: Synchronizes access to IRQ tables
 * @backlight_dev: Backlight control device
 * @backlight_link: Link on which to control backlight
 * @backlight_caps: Capabilities of the backlight device
 * @freesync_module: Module handling freesync calculations
 * @hdcp_workqueue: AMDGPU content protection queue
 * @fw_dmcu: Reference to DMCU firmware
 * @dmcu_fw_version: Version of the DMCU firmware
 * @soc_bounding_box: SOC bounding box values provided by gpu_info FW
 * @cached_state: Caches device atomic state for suspend/resume
 * @cached_dc_state: Cached state of content streams
 * @compressor: Frame buffer compression buffer. See &struct dm_compressor_info
 * @force_timing_sync: set via debugfs. When set, indicates that all connected
 *		       displays will be forced to synchronize.
 * @dmcub_trace_event_en: enable dmcub trace events
 * @dmub_outbox_params: DMUB Outbox parameters
 * @num_of_edps: number of backlight eDPs
 * @disable_hpd_irq: disables all HPD and HPD RX interrupt handling in the
 *		     driver when true
 * @dmub_aux_transfer_done: struct completion used to indicate when DMUB
 * 			    transfers are done
 * @delayed_hpd_wq: work queue used to delay DMUB HPD work
 */
struct amdgpu_display_manager {

	struct dc *dc;

	/**
	 * @dmub_srv:
	 *
	 * DMUB service, used for controlling the DMUB on hardware
	 * that supports it. The pointer to the dmub_srv will be
	 * NULL on hardware that does not support it.
	 */
	struct dmub_srv *dmub_srv;

	/**
	 * @dmub_notify:
	 *
	 * Notification from DMUB.
	 */

	struct dmub_notification *dmub_notify;

	/**
	 * @dmub_callback:
	 *
	 * Callback functions to handle notification from DMUB.
	 */

	dmub_notify_interrupt_callback_t dmub_callback[AMDGPU_DMUB_NOTIFICATION_MAX];

	/**
	 * @dmub_thread_offload:
	 *
	 * Flag to indicate if callback is offload.
	 */

	bool dmub_thread_offload[AMDGPU_DMUB_NOTIFICATION_MAX];

	/**
	 * @dmub_fb_info:
	 *
	 * Framebuffer regions for the DMUB.
	 */
	struct dmub_srv_fb_info *dmub_fb_info;

	/**
	 * @dmub_fw:
	 *
	 * DMUB firmware, required on hardware that has DMUB support.
	 */
	const struct firmware *dmub_fw;

	/**
	 * @dmub_bo:
	 *
	 * Buffer object for the DMUB.
	 */
	struct amdgpu_bo *dmub_bo;

	/**
	 * @dmub_bo_gpu_addr:
	 *
	 * GPU virtual address for the DMUB buffer object.
	 */
	u64 dmub_bo_gpu_addr;

	/**
	 * @dmub_bo_cpu_addr:
	 *
	 * CPU address for the DMUB buffer object.
	 */
	void *dmub_bo_cpu_addr;

	/**
	 * @dmcub_fw_version:
	 *
	 * DMCUB firmware version.
	 */
	uint32_t dmcub_fw_version;

	/**
	 * @cgs_device:
	 *
	 * The Common Graphics Services device. It provides an interface for
	 * accessing registers.
	 */
	struct cgs_device *cgs_device;

	struct amdgpu_device *adev;
	struct drm_device *ddev;
	u16 display_indexes_num;

	/**
	 * @atomic_obj:
	 *
	 * In combination with &dm_atomic_state it helps manage
	 * global atomic state that doesn't map cleanly into existing
	 * drm resources, like &dc_context.
	 */
	struct drm_private_obj atomic_obj;

	/**
	 * @dc_lock:
	 *
	 * Guards access to DC functions that can issue register write
	 * sequences.
	 */
	struct mutex dc_lock;

	/**
	 * @audio_lock:
	 *
	 * Guards access to audio instance changes.
	 */
	struct mutex audio_lock;

	/**
	 * @audio_component:
	 *
	 * Used to notify ELD changes to sound driver.
	 */
	struct drm_audio_component *audio_component;

	/**
	 * @audio_registered:
	 *
	 * True if the audio component has been registered
	 * successfully, false otherwise.
	 */
	bool audio_registered;

	/**
	 * @irq_handler_list_low_tab:
	 *
	 * Low priority IRQ handler table.
	 *
	 * It is a n*m table consisting of n IRQ sources, and m handlers per IRQ
	 * source. Low priority IRQ handlers are deferred to a workqueue to be
	 * processed. Hence, they can sleep.
	 *
	 * Note that handlers are called in the same order as they were
	 * registered (FIFO).
	 */
	struct list_head irq_handler_list_low_tab[DAL_IRQ_SOURCES_NUMBER];

	/**
	 * @irq_handler_list_high_tab:
	 *
	 * High priority IRQ handler table.
	 *
	 * It is a n*m table, same as &irq_handler_list_low_tab. However,
	 * handlers in this table are not deferred and are called immediately.
	 */
	struct list_head irq_handler_list_high_tab[DAL_IRQ_SOURCES_NUMBER];

	/**
	 * @pflip_params:
	 *
	 * Page flip IRQ parameters, passed to registered handlers when
	 * triggered.
	 */
	struct common_irq_params
	pflip_params[DC_IRQ_SOURCE_PFLIP_LAST - DC_IRQ_SOURCE_PFLIP_FIRST + 1];

	/**
	 * @vblank_params:
	 *
	 * Vertical blanking IRQ parameters, passed to registered handlers when
	 * triggered.
	 */
	struct common_irq_params
	vblank_params[DC_IRQ_SOURCE_VBLANK6 - DC_IRQ_SOURCE_VBLANK1 + 1];

	/**
	 * @vline0_params:
	 *
	 * OTG vertical interrupt0 IRQ parameters, passed to registered
	 * handlers when triggered.
	 */
	struct common_irq_params
	vline0_params[DC_IRQ_SOURCE_DC6_VLINE0 - DC_IRQ_SOURCE_DC1_VLINE0 + 1];

	/**
	 * @vupdate_params:
	 *
	 * Vertical update IRQ parameters, passed to registered handlers when
	 * triggered.
	 */
	struct common_irq_params
	vupdate_params[DC_IRQ_SOURCE_VUPDATE6 - DC_IRQ_SOURCE_VUPDATE1 + 1];

	/**
	 * @dmub_trace_params:
	 *
	 * DMUB trace event IRQ parameters, passed to registered handlers when
	 * triggered.
	 */
	struct common_irq_params
	dmub_trace_params[1];

	struct common_irq_params
	dmub_outbox_params[1];

	spinlock_t irq_handler_list_table_lock;

	struct backlight_device *backlight_dev[AMDGPU_DM_MAX_NUM_EDP];

	const struct dc_link *backlight_link[AMDGPU_DM_MAX_NUM_EDP];

	uint8_t num_of_edps;

	struct amdgpu_dm_backlight_caps backlight_caps[AMDGPU_DM_MAX_NUM_EDP];

	struct mod_freesync *freesync_module;
	struct hdcp_workqueue *hdcp_workqueue;

	/**
	 * @vblank_control_workqueue:
	 *
	 * Deferred work for vblank control events.
	 */
	struct workqueue_struct *vblank_control_workqueue;

	struct drm_atomic_state *cached_state;
	struct dc_state *cached_dc_state;

	struct dm_compressor_info compressor;

	const struct firmware *fw_dmcu;
	uint32_t dmcu_fw_version;
	/**
	 * @soc_bounding_box:
	 *
	 * gpu_info FW provided soc bounding box struct or 0 if not
	 * available in FW
	 */
	const struct gpu_info_soc_bounding_box_v1_0 *soc_bounding_box;

	/**
	 * @active_vblank_irq_count:
	 *
	 * number of currently active vblank irqs
	 */
	uint32_t active_vblank_irq_count;

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	/**
	 * @secure_display_ctxs:
	 *
	 * Store the ROI information and the work_struct to command dmub and psp for
	 * all crtcs.
	 */
	struct secure_display_context *secure_display_ctxs;
#endif
	/**
	 * @hpd_rx_offload_wq:
	 *
	 * Work queue to offload works of hpd_rx_irq
	 */
	struct hpd_rx_irq_offload_work_queue *hpd_rx_offload_wq;
	/**
	 * @mst_encoders:
	 *
	 * fake encoders used for DP MST.
	 */
	struct amdgpu_encoder mst_encoders[AMDGPU_DM_MAX_CRTC];
	bool force_timing_sync;
	bool disable_hpd_irq;
	bool dmcub_trace_event_en;
	/**
	 * @da_list:
	 *
	 * DAL fb memory allocation list, for communication with SMU.
	 */
	struct list_head da_list;
	struct completion dmub_aux_transfer_done;
	struct workqueue_struct *delayed_hpd_wq;

	/**
	 * @brightness:
	 *
	 * cached backlight values.
	 */
	u32 brightness[AMDGPU_DM_MAX_NUM_EDP];
	/**
	 * @actual_brightness:
	 *
	 * last successfully applied backlight values.
	 */
	u32 actual_brightness[AMDGPU_DM_MAX_NUM_EDP];

	/**
	 * @aux_hpd_discon_quirk:
	 *
	 * quirk for hpd discon while aux is on-going.
	 * occurred on certain intel platform
	 */
	bool aux_hpd_discon_quirk;

	/**
	 * @dpia_aux_lock:
	 *
	 * Guards access to DPIA AUX
	 */
	struct mutex dpia_aux_lock;
};

enum dsc_clock_force_state {
	DSC_CLK_FORCE_DEFAULT = 0,
	DSC_CLK_FORCE_ENABLE,
	DSC_CLK_FORCE_DISABLE,
};

struct dsc_preferred_settings {
	enum dsc_clock_force_state dsc_force_enable;
	uint32_t dsc_num_slices_v;
	uint32_t dsc_num_slices_h;
	uint32_t dsc_bits_per_pixel;
	bool dsc_force_disable_passthrough;
};

enum mst_progress_status {
	MST_STATUS_DEFAULT = 0,
	MST_PROBE = BIT(0),
	MST_REMOTE_EDID = BIT(1),
	MST_ALLOCATE_NEW_PAYLOAD = BIT(2),
	MST_CLEAR_ALLOCATED_PAYLOAD = BIT(3),
};

/**
 * struct amdgpu_hdmi_vsdb_info - Keep track of the VSDB info
 *
 * AMDGPU supports FreeSync over HDMI by using the VSDB section, and this
 * struct is useful to keep track of the display-specific information about
 * FreeSync.
 */
struct amdgpu_hdmi_vsdb_info {
	/**
	 * @amd_vsdb_version: Vendor Specific Data Block Version, should be
	 * used to determine which Vendor Specific InfoFrame (VSIF) to send.
	 */
	unsigned int amd_vsdb_version;

	/**
	 * @freesync_supported: FreeSync Supported.
	 */
	bool freesync_supported;

	/**
	 * @min_refresh_rate_hz: FreeSync Minimum Refresh Rate in Hz.
	 */
	unsigned int min_refresh_rate_hz;

	/**
	 * @max_refresh_rate_hz: FreeSync Maximum Refresh Rate in Hz
	 */
	unsigned int max_refresh_rate_hz;
};

struct amdgpu_dm_connector {

	struct drm_connector base;
	uint32_t connector_id;
	int bl_idx;

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

	/**
	 * @dc_em_sink: Reference to the emulated (virtual) sink.
	 */
	struct dc_sink *dc_em_sink;

	/* DM only */
	struct drm_dp_mst_topology_mgr mst_mgr;
	struct amdgpu_dm_dp_aux dm_dp_aux;
	struct drm_dp_mst_port *mst_output_port;
	struct amdgpu_dm_connector *mst_root;
	struct drm_dp_aux *dsc_aux;
	/* TODO see if we can merge with ddc_bus or make a dm_connector */
	struct amdgpu_i2c_adapter *i2c;

	/* Monitor range limits */
	/**
	 * @min_vfreq: Minimal frequency supported by the display in Hz. This
	 * value is set to zero when there is no FreeSync support.
	 */
	int min_vfreq;

	/**
	 * @max_vfreq: Maximum frequency supported by the display in Hz. This
	 * value is set to zero when there is no FreeSync support.
	 */
	int max_vfreq ;
	int pixel_clock_mhz;

	/* Audio instance - protected by audio_lock. */
	int audio_inst;

	struct mutex hpd_lock;

	bool fake_enable;
	bool force_yuv420_output;
	struct dsc_preferred_settings dsc_settings;
	union dp_downstream_port_present mst_downstream_port_present;
	/* Cached display modes */
	struct drm_display_mode freesync_vid_base;

	int psr_skip_count;

	/* Record progress status of mst*/
	uint8_t mst_status;

	/* Automated testing */
	bool timing_changed;
	struct dc_crtc_timing *timing_requested;

	/* Adaptive Sync */
	bool pack_sdp_v1_3;
	enum adaptive_sync_type as_type;
	struct amdgpu_hdmi_vsdb_info vsdb_info;
};

static inline void amdgpu_dm_set_mst_status(uint8_t *status,
		uint8_t flags, bool set)
{
	if (set)
		*status |= flags;
	else
		*status &= ~flags;
}

#define to_amdgpu_dm_connector(x) container_of(x, struct amdgpu_dm_connector, base)

extern const struct amdgpu_ip_block_version dm_ip_block;

struct dm_plane_state {
	struct drm_plane_state base;
	struct dc_plane_state *dc_state;
};

struct dm_crtc_state {
	struct drm_crtc_state base;
	struct dc_stream_state *stream;

	bool cm_has_degamma;
	bool cm_is_degamma_srgb;

	bool mpo_requested;

	int update_type;
	int active_planes;

	int crc_skip_count;

	bool freesync_vrr_info_changed;

	bool dsc_force_changed;
	bool vrr_supported;
	struct mod_freesync_config freesync_config;
	struct dc_info_packet vrr_infopacket;

	int abm_level;
};

#define to_dm_crtc_state(x) container_of(x, struct dm_crtc_state, base)

struct dm_atomic_state {
	struct drm_private_state base;

	struct dc_state *context;
};

#define to_dm_atomic_state(x) container_of(x, struct dm_atomic_state, base)

struct dm_connector_state {
	struct drm_connector_state base;

	enum amdgpu_rmx_type scaling;
	uint8_t underscan_vborder;
	uint8_t underscan_hborder;
	bool underscan_enable;
	bool freesync_capable;
	bool update_hdcp;
	uint8_t abm_level;
	int vcpi_slots;
	uint64_t pbn;
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

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
				   struct drm_display_mode *mode);

void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector);

void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
					struct edid *edid);

void amdgpu_dm_trigger_timing_sync(struct drm_device *dev);

#define MAX_COLOR_LUT_ENTRIES 4096
/* Legacy gamm LUT users such as X doesn't like large LUT sizes */
#define MAX_COLOR_LEGACY_LUT_ENTRIES 256

void amdgpu_dm_init_color_mod(void);
int amdgpu_dm_verify_lut_sizes(const struct drm_crtc_state *crtc_state);
int amdgpu_dm_update_crtc_color_mgmt(struct dm_crtc_state *crtc);
int amdgpu_dm_update_plane_color_mgmt(struct dm_crtc_state *crtc,
				      struct dc_plane_state *dc_plane_state);

void amdgpu_dm_update_connector_after_detect(
		struct amdgpu_dm_connector *aconnector);

extern const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs;

int amdgpu_dm_process_dmub_aux_transfer_sync(struct dc_context *ctx, unsigned int link_index,
					struct aux_payload *payload, enum aux_return_code_type *operation_result);

int amdgpu_dm_process_dmub_set_config_sync(struct dc_context *ctx, unsigned int link_index,
					struct set_config_cmd_payload *payload, enum set_config_status *operation_result);

bool check_seamless_boot_capability(struct amdgpu_device *adev);

struct dc_stream_state *
	create_validate_stream_for_sink(struct amdgpu_dm_connector *aconnector,
					const struct drm_display_mode *drm_mode,
					const struct dm_connector_state *dm_state,
					const struct dc_stream_state *old_stream);

int dm_atomic_get_state(struct drm_atomic_state *state,
			struct dm_atomic_state **dm_state);

struct amdgpu_dm_connector *
amdgpu_dm_find_first_crtc_matching_connector(struct drm_atomic_state *state,
					     struct drm_crtc *crtc);

int convert_dc_color_depth_into_bpc(enum dc_color_depth display_color_depth);
#endif /* __AMDGPU_DM_H__ */
