/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef __INTEL_DRV_H__
#define __INTEL_DRV_H__

#include <linux/async.h>
#include <linux/i2c.h>
#include <linux/sched/clock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_dp_dual_mode_helper.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_vblank.h>
#include <drm/i915_drm.h>
#include <drm/i915_mei_hdcp_interface.h>
#include <media/cec-notifier.h>

#include "i915_drv.h"

struct drm_printer;

/*
 * Display related stuff
 */

/* these are outputs from the chip - integrated only
   external chips are via DVO or SDVO output */
enum intel_output_type {
	INTEL_OUTPUT_UNUSED = 0,
	INTEL_OUTPUT_ANALOG = 1,
	INTEL_OUTPUT_DVO = 2,
	INTEL_OUTPUT_SDVO = 3,
	INTEL_OUTPUT_LVDS = 4,
	INTEL_OUTPUT_TVOUT = 5,
	INTEL_OUTPUT_HDMI = 6,
	INTEL_OUTPUT_DP = 7,
	INTEL_OUTPUT_EDP = 8,
	INTEL_OUTPUT_DSI = 9,
	INTEL_OUTPUT_DDI = 10,
	INTEL_OUTPUT_DP_MST = 11,
};

struct intel_framebuffer {
	struct drm_framebuffer base;
	struct intel_rotation_info rot_info;

	/* for each plane in the normal GTT view */
	struct {
		unsigned int x, y;
	} normal[2];
	/* for each plane in the rotated GTT view */
	struct {
		unsigned int x, y;
		unsigned int pitch; /* pixels */
	} rotated[2];
};

struct intel_fbdev {
	struct drm_fb_helper helper;
	struct intel_framebuffer *fb;
	struct i915_vma *vma;
	unsigned long vma_flags;
	async_cookie_t cookie;
	int preferred_bpp;

	/* Whether or not fbdev hpd processing is temporarily suspended */
	bool hpd_suspended : 1;
	/* Set when a hotplug was received while HPD processing was
	 * suspended
	 */
	bool hpd_waiting : 1;

	/* Protects hpd_suspended */
	struct mutex hpd_lock;
};

struct intel_encoder {
	struct drm_encoder base;

	enum intel_output_type type;
	enum port port;
	unsigned int cloneable;
	bool (*hotplug)(struct intel_encoder *encoder,
			struct intel_connector *connector);
	enum intel_output_type (*compute_output_type)(struct intel_encoder *,
						      struct intel_crtc_state *,
						      struct drm_connector_state *);
	int (*compute_config)(struct intel_encoder *,
			      struct intel_crtc_state *,
			      struct drm_connector_state *);
	void (*pre_pll_enable)(struct intel_encoder *,
			       const struct intel_crtc_state *,
			       const struct drm_connector_state *);
	void (*pre_enable)(struct intel_encoder *,
			   const struct intel_crtc_state *,
			   const struct drm_connector_state *);
	void (*enable)(struct intel_encoder *,
		       const struct intel_crtc_state *,
		       const struct drm_connector_state *);
	void (*disable)(struct intel_encoder *,
			const struct intel_crtc_state *,
			const struct drm_connector_state *);
	void (*post_disable)(struct intel_encoder *,
			     const struct intel_crtc_state *,
			     const struct drm_connector_state *);
	void (*post_pll_disable)(struct intel_encoder *,
				 const struct intel_crtc_state *,
				 const struct drm_connector_state *);
	void (*update_pipe)(struct intel_encoder *,
			    const struct intel_crtc_state *,
			    const struct drm_connector_state *);
	/* Read out the current hw state of this connector, returning true if
	 * the encoder is active. If the encoder is enabled it also set the pipe
	 * it is connected to in the pipe parameter. */
	bool (*get_hw_state)(struct intel_encoder *, enum pipe *pipe);
	/* Reconstructs the equivalent mode flags for the current hardware
	 * state. This must be called _after_ display->get_pipe_config has
	 * pre-filled the pipe config. Note that intel_encoder->base.crtc must
	 * be set correctly before calling this function. */
	void (*get_config)(struct intel_encoder *,
			   struct intel_crtc_state *pipe_config);
	/*
	 * Acquires the power domains needed for an active encoder during
	 * hardware state readout.
	 */
	void (*get_power_domains)(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state);
	/*
	 * Called during system suspend after all pending requests for the
	 * encoder are flushed (for example for DP AUX transactions) and
	 * device interrupts are disabled.
	 */
	void (*suspend)(struct intel_encoder *);
	int crtc_mask;
	enum hpd_pin hpd_pin;
	enum intel_display_power_domain power_domain;
	/* for communication with audio component; protected by av_mutex */
	const struct drm_connector *audio_connector;
};

struct intel_panel {
	struct drm_display_mode *fixed_mode;
	struct drm_display_mode *downclock_mode;

	/* backlight */
	struct {
		bool present;
		u32 level;
		u32 min;
		u32 max;
		bool enabled;
		bool combination_mode;	/* gen 2/4 only */
		bool active_low_pwm;
		bool alternate_pwm_increment;	/* lpt+ */

		/* PWM chip */
		bool util_pin_active_low;	/* bxt+ */
		u8 controller;		/* bxt+ only */
		struct pwm_device *pwm;

		struct backlight_device *device;

		/* Connector and platform specific backlight functions */
		int (*setup)(struct intel_connector *connector, enum pipe pipe);
		u32 (*get)(struct intel_connector *connector);
		void (*set)(const struct drm_connector_state *conn_state, u32 level);
		void (*disable)(const struct drm_connector_state *conn_state);
		void (*enable)(const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state);
		u32 (*hz_to_pwm)(struct intel_connector *connector, u32 hz);
		void (*power)(struct intel_connector *, bool enable);
	} backlight;
};

struct intel_digital_port;

enum check_link_response {
	HDCP_LINK_PROTECTED	= 0,
	HDCP_TOPOLOGY_CHANGE,
	HDCP_LINK_INTEGRITY_FAILURE,
	HDCP_REAUTH_REQUEST
};

/*
 * This structure serves as a translation layer between the generic HDCP code
 * and the bus-specific code. What that means is that HDCP over HDMI differs
 * from HDCP over DP, so to account for these differences, we need to
 * communicate with the receiver through this shim.
 *
 * For completeness, the 2 buses differ in the following ways:
 *	- DP AUX vs. DDC
 *		HDCP registers on the receiver are set via DP AUX for DP, and
 *		they are set via DDC for HDMI.
 *	- Receiver register offsets
 *		The offsets of the registers are different for DP vs. HDMI
 *	- Receiver register masks/offsets
 *		For instance, the ready bit for the KSV fifo is in a different
 *		place on DP vs HDMI
 *	- Receiver register names
 *		Seriously. In the DP spec, the 16-bit register containing
 *		downstream information is called BINFO, on HDMI it's called
 *		BSTATUS. To confuse matters further, DP has a BSTATUS register
 *		with a completely different definition.
 *	- KSV FIFO
 *		On HDMI, the ksv fifo is read all at once, whereas on DP it must
 *		be read 3 keys at a time
 *	- Aksv output
 *		Since Aksv is hidden in hardware, there's different procedures
 *		to send it over DP AUX vs DDC
 */
struct intel_hdcp_shim {
	/* Outputs the transmitter's An and Aksv values to the receiver. */
	int (*write_an_aksv)(struct intel_digital_port *intel_dig_port, u8 *an);

	/* Reads the receiver's key selection vector */
	int (*read_bksv)(struct intel_digital_port *intel_dig_port, u8 *bksv);

	/*
	 * Reads BINFO from DP receivers and BSTATUS from HDMI receivers. The
	 * definitions are the same in the respective specs, but the names are
	 * different. Call it BSTATUS since that's the name the HDMI spec
	 * uses and it was there first.
	 */
	int (*read_bstatus)(struct intel_digital_port *intel_dig_port,
			    u8 *bstatus);

	/* Determines whether a repeater is present downstream */
	int (*repeater_present)(struct intel_digital_port *intel_dig_port,
				bool *repeater_present);

	/* Reads the receiver's Ri' value */
	int (*read_ri_prime)(struct intel_digital_port *intel_dig_port, u8 *ri);

	/* Determines if the receiver's KSV FIFO is ready for consumption */
	int (*read_ksv_ready)(struct intel_digital_port *intel_dig_port,
			      bool *ksv_ready);

	/* Reads the ksv fifo for num_downstream devices */
	int (*read_ksv_fifo)(struct intel_digital_port *intel_dig_port,
			     int num_downstream, u8 *ksv_fifo);

	/* Reads a 32-bit part of V' from the receiver */
	int (*read_v_prime_part)(struct intel_digital_port *intel_dig_port,
				 int i, u32 *part);

	/* Enables HDCP signalling on the port */
	int (*toggle_signalling)(struct intel_digital_port *intel_dig_port,
				 bool enable);

	/* Ensures the link is still protected */
	bool (*check_link)(struct intel_digital_port *intel_dig_port);

	/* Detects panel's hdcp capability. This is optional for HDMI. */
	int (*hdcp_capable)(struct intel_digital_port *intel_dig_port,
			    bool *hdcp_capable);

	/* HDCP adaptation(DP/HDMI) required on the port */
	enum hdcp_wired_protocol protocol;

	/* Detects whether sink is HDCP2.2 capable */
	int (*hdcp_2_2_capable)(struct intel_digital_port *intel_dig_port,
				bool *capable);

	/* Write HDCP2.2 messages */
	int (*write_2_2_msg)(struct intel_digital_port *intel_dig_port,
			     void *buf, size_t size);

	/* Read HDCP2.2 messages */
	int (*read_2_2_msg)(struct intel_digital_port *intel_dig_port,
			    u8 msg_id, void *buf, size_t size);

	/*
	 * Implementation of DP HDCP2.2 Errata for the communication of stream
	 * type to Receivers. In DP HDCP2.2 Stream type is one of the input to
	 * the HDCP2.2 Cipher for En/De-Cryption. Not applicable for HDMI.
	 */
	int (*config_stream_type)(struct intel_digital_port *intel_dig_port,
				  bool is_repeater, u8 type);

	/* HDCP2.2 Link Integrity Check */
	int (*check_2_2_link)(struct intel_digital_port *intel_dig_port);
};

struct intel_hdcp {
	const struct intel_hdcp_shim *shim;
	/* Mutex for hdcp state of the connector */
	struct mutex mutex;
	u64 value;
	struct delayed_work check_work;
	struct work_struct prop_work;

	/* HDCP1.4 Encryption status */
	bool hdcp_encrypted;

	/* HDCP2.2 related definitions */
	/* Flag indicates whether this connector supports HDCP2.2 or not. */
	bool hdcp2_supported;

	/* HDCP2.2 Encryption status */
	bool hdcp2_encrypted;

	/*
	 * Content Stream Type defined by content owner. TYPE0(0x0) content can
	 * flow in the link protected by HDCP2.2 or HDCP1.4, where as TYPE1(0x1)
	 * content can flow only through a link protected by HDCP2.2.
	 */
	u8 content_type;
	struct hdcp_port_data port_data;

	bool is_paired;
	bool is_repeater;

	/*
	 * Count of ReceiverID_List received. Initialized to 0 at AKE_INIT.
	 * Incremented after processing the RepeaterAuth_Send_ReceiverID_List.
	 * When it rolls over re-auth has to be triggered.
	 */
	u32 seq_num_v;

	/*
	 * Count of RepeaterAuth_Stream_Manage msg propagated.
	 * Initialized to 0 on AKE_INIT. Incremented after every successful
	 * transmission of RepeaterAuth_Stream_Manage message. When it rolls
	 * over re-Auth has to be triggered.
	 */
	u32 seq_num_m;

	/*
	 * Work queue to signal the CP_IRQ. Used for the waiters to read the
	 * available information from HDCP DP sink.
	 */
	wait_queue_head_t cp_irq_queue;
	atomic_t cp_irq_count;
	int cp_irq_count_cached;
};

struct intel_connector {
	struct drm_connector base;
	/*
	 * The fixed encoder this connector is connected to.
	 */
	struct intel_encoder *encoder;

	/* ACPI device id for ACPI and driver cooperation */
	u32 acpi_device_id;

	/* Reads out the current hw, returning true if the connector is enabled
	 * and active (i.e. dpms ON state). */
	bool (*get_hw_state)(struct intel_connector *);

	/* Panel info for eDP and LVDS */
	struct intel_panel panel;

	/* Cached EDID for eDP and LVDS. May hold ERR_PTR for invalid EDID. */
	struct edid *edid;
	struct edid *detect_edid;

	/* since POLL and HPD connectors may use the same HPD line keep the native
	   state of connector->polled in case hotplug storm detection changes it */
	u8 polled;

	void *port; /* store this opaque as its illegal to dereference it */

	struct intel_dp *mst_port;

	/* Work struct to schedule a uevent on link train failure */
	struct work_struct modeset_retry_work;

	struct intel_hdcp hdcp;
};

struct intel_digital_connector_state {
	struct drm_connector_state base;

	enum hdmi_force_audio force_audio;
	int broadcast_rgb;
};

#define to_intel_digital_connector_state(x) container_of(x, struct intel_digital_connector_state, base)

struct dpll {
	/* given values */
	int n;
	int m1, m2;
	int p1, p2;
	/* derived values */
	int	dot;
	int	vco;
	int	m;
	int	p;
};

struct intel_atomic_state {
	struct drm_atomic_state base;

	intel_wakeref_t wakeref;

	struct {
		/*
		 * Logical state of cdclk (used for all scaling, watermark,
		 * etc. calculations and checks). This is computed as if all
		 * enabled crtcs were active.
		 */
		struct intel_cdclk_state logical;

		/*
		 * Actual state of cdclk, can be different from the logical
		 * state only when all crtc's are DPMS off.
		 */
		struct intel_cdclk_state actual;

		int force_min_cdclk;
		bool force_min_cdclk_changed;
		/* pipe to which cd2x update is synchronized */
		enum pipe pipe;
	} cdclk;

	bool dpll_set, modeset;

	/*
	 * Does this transaction change the pipes that are active?  This mask
	 * tracks which CRTC's have changed their active state at the end of
	 * the transaction (not counting the temporary disable during modesets).
	 * This mask should only be non-zero when intel_state->modeset is true,
	 * but the converse is not necessarily true; simply changing a mode may
	 * not flip the final active status of any CRTC's
	 */
	unsigned int active_pipe_changes;

	unsigned int active_crtcs;
	/* minimum acceptable cdclk for each pipe */
	int min_cdclk[I915_MAX_PIPES];
	/* minimum acceptable voltage level for each pipe */
	u8 min_voltage_level[I915_MAX_PIPES];

	struct intel_shared_dpll_state shared_dpll[I915_NUM_PLLS];

	/*
	 * Current watermarks can't be trusted during hardware readout, so
	 * don't bother calculating intermediate watermarks.
	 */
	bool skip_intermediate_wm;

	bool rps_interactive;

	/* Gen9+ only */
	struct skl_ddb_values wm_results;

	struct i915_sw_fence commit_ready;

	struct llist_node freed;
};

struct intel_plane_state {
	struct drm_plane_state base;
	struct i915_ggtt_view view;
	struct i915_vma *vma;
	unsigned long flags;
#define PLANE_HAS_FENCE BIT(0)

	struct {
		u32 offset;
		/*
		 * Plane stride in:
		 * bytes for 0/180 degree rotation
		 * pixels for 90/270 degree rotation
		 */
		u32 stride;
		int x, y;
	} color_plane[2];

	/* plane control register */
	u32 ctl;

	/* plane color control register */
	u32 color_ctl;

	/*
	 * scaler_id
	 *    = -1 : not using a scaler
	 *    >=  0 : using a scalers
	 *
	 * plane requiring a scaler:
	 *   - During check_plane, its bit is set in
	 *     crtc_state->scaler_state.scaler_users by calling helper function
	 *     update_scaler_plane.
	 *   - scaler_id indicates the scaler it got assigned.
	 *
	 * plane doesn't require a scaler:
	 *   - this can happen when scaling is no more required or plane simply
	 *     got disabled.
	 *   - During check_plane, corresponding bit is reset in
	 *     crtc_state->scaler_state.scaler_users by calling helper function
	 *     update_scaler_plane.
	 */
	int scaler_id;

	/*
	 * linked_plane:
	 *
	 * ICL planar formats require 2 planes that are updated as pairs.
	 * This member is used to make sure the other plane is also updated
	 * when required, and for update_slave() to find the correct
	 * plane_state to pass as argument.
	 */
	struct intel_plane *linked_plane;

	/*
	 * slave:
	 * If set don't update use the linked plane's state for updating
	 * this plane during atomic commit with the update_slave() callback.
	 *
	 * It's also used by the watermark code to ignore wm calculations on
	 * this plane. They're calculated by the linked plane's wm code.
	 */
	u32 slave;

	struct drm_intel_sprite_colorkey ckey;
};

struct intel_initial_plane_config {
	struct intel_framebuffer *fb;
	unsigned int tiling;
	int size;
	u32 base;
	u8 rotation;
};

struct intel_scaler {
	int in_use;
	u32 mode;
};

struct intel_crtc_scaler_state {
#define SKL_NUM_SCALERS 2
	struct intel_scaler scalers[SKL_NUM_SCALERS];

	/*
	 * scaler_users: keeps track of users requesting scalers on this crtc.
	 *
	 *     If a bit is set, a user is using a scaler.
	 *     Here user can be a plane or crtc as defined below:
	 *       bits 0-30 - plane (bit position is index from drm_plane_index)
	 *       bit 31    - crtc
	 *
	 * Instead of creating a new index to cover planes and crtc, using
	 * existing drm_plane_index for planes which is well less than 31
	 * planes and bit 31 for crtc. This should be fine to cover all
	 * our platforms.
	 *
	 * intel_atomic_setup_scalers will setup available scalers to users
	 * requesting scalers. It will gracefully fail if request exceeds
	 * avilability.
	 */
#define SKL_CRTC_INDEX 31
	unsigned scaler_users;

	/* scaler used by crtc for panel fitting purpose */
	int scaler_id;
};

/* drm_mode->private_flags */
#define I915_MODE_FLAG_INHERITED (1<<0)
/* Flag to get scanline using frame time stamps */
#define I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP (1<<1)
/* Flag to use the scanline counter instead of the pixel counter */
#define I915_MODE_FLAG_USE_SCANLINE_COUNTER (1<<2)

struct intel_pipe_wm {
	struct intel_wm_level wm[5];
	u32 linetime;
	bool fbc_wm_enabled;
	bool pipe_enabled;
	bool sprites_enabled;
	bool sprites_scaled;
};

struct skl_plane_wm {
	struct skl_wm_level wm[8];
	struct skl_wm_level uv_wm[8];
	struct skl_wm_level trans_wm;
	bool is_planar;
};

struct skl_pipe_wm {
	struct skl_plane_wm planes[I915_MAX_PLANES];
	u32 linetime;
};

enum vlv_wm_level {
	VLV_WM_LEVEL_PM2,
	VLV_WM_LEVEL_PM5,
	VLV_WM_LEVEL_DDR_DVFS,
	NUM_VLV_WM_LEVELS,
};

struct vlv_wm_state {
	struct g4x_pipe_wm wm[NUM_VLV_WM_LEVELS];
	struct g4x_sr_wm sr[NUM_VLV_WM_LEVELS];
	u8 num_levels;
	bool cxsr;
};

struct vlv_fifo_state {
	u16 plane[I915_MAX_PLANES];
};

enum g4x_wm_level {
	G4X_WM_LEVEL_NORMAL,
	G4X_WM_LEVEL_SR,
	G4X_WM_LEVEL_HPLL,
	NUM_G4X_WM_LEVELS,
};

struct g4x_wm_state {
	struct g4x_pipe_wm wm;
	struct g4x_sr_wm sr;
	struct g4x_sr_wm hpll;
	bool cxsr;
	bool hpll_en;
	bool fbc_en;
};

struct intel_crtc_wm_state {
	union {
		struct {
			/*
			 * Intermediate watermarks; these can be
			 * programmed immediately since they satisfy
			 * both the current configuration we're
			 * switching away from and the new
			 * configuration we're switching to.
			 */
			struct intel_pipe_wm intermediate;

			/*
			 * Optimal watermarks, programmed post-vblank
			 * when this state is committed.
			 */
			struct intel_pipe_wm optimal;
		} ilk;

		struct {
			/* gen9+ only needs 1-step wm programming */
			struct skl_pipe_wm optimal;
			struct skl_ddb_entry ddb;
			struct skl_ddb_entry plane_ddb_y[I915_MAX_PLANES];
			struct skl_ddb_entry plane_ddb_uv[I915_MAX_PLANES];
		} skl;

		struct {
			/* "raw" watermarks (not inverted) */
			struct g4x_pipe_wm raw[NUM_VLV_WM_LEVELS];
			/* intermediate watermarks (inverted) */
			struct vlv_wm_state intermediate;
			/* optimal watermarks (inverted) */
			struct vlv_wm_state optimal;
			/* display FIFO split */
			struct vlv_fifo_state fifo_state;
		} vlv;

		struct {
			/* "raw" watermarks */
			struct g4x_pipe_wm raw[NUM_G4X_WM_LEVELS];
			/* intermediate watermarks */
			struct g4x_wm_state intermediate;
			/* optimal watermarks */
			struct g4x_wm_state optimal;
		} g4x;
	};

	/*
	 * Platforms with two-step watermark programming will need to
	 * update watermark programming post-vblank to switch from the
	 * safe intermediate watermarks to the optimal final
	 * watermarks.
	 */
	bool need_postvbl_update;
};

enum intel_output_format {
	INTEL_OUTPUT_FORMAT_INVALID,
	INTEL_OUTPUT_FORMAT_RGB,
	INTEL_OUTPUT_FORMAT_YCBCR420,
	INTEL_OUTPUT_FORMAT_YCBCR444,
};

struct intel_crtc_state {
	struct drm_crtc_state base;

	/**
	 * quirks - bitfield with hw state readout quirks
	 *
	 * For various reasons the hw state readout code might not be able to
	 * completely faithfully read out the current state. These cases are
	 * tracked with quirk flags so that fastboot and state checker can act
	 * accordingly.
	 */
#define PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS	(1<<0) /* unreliable sync mode.flags */
	unsigned long quirks;

	unsigned fb_bits; /* framebuffers to flip */
	bool update_pipe; /* can a fast modeset be performed? */
	bool disable_cxsr;
	bool update_wm_pre, update_wm_post; /* watermarks are updated */
	bool fb_changed; /* fb on any of the planes is changed */
	bool fifo_changed; /* FIFO split is changed */

	/* Pipe source size (ie. panel fitter input size)
	 * All planes will be positioned inside this space,
	 * and get clipped at the edges. */
	int pipe_src_w, pipe_src_h;

	/*
	 * Pipe pixel rate, adjusted for
	 * panel fitter/pipe scaler downscaling.
	 */
	unsigned int pixel_rate;

	/* Whether to set up the PCH/FDI. Note that we never allow sharing
	 * between pch encoders and cpu encoders. */
	bool has_pch_encoder;

	/* Are we sending infoframes on the attached port */
	bool has_infoframe;

	/* CPU Transcoder for the pipe. Currently this can only differ from the
	 * pipe on Haswell and later (where we have a special eDP transcoder)
	 * and Broxton (where we have special DSI transcoders). */
	enum transcoder cpu_transcoder;

	/*
	 * Use reduced/limited/broadcast rbg range, compressing from the full
	 * range fed into the crtcs.
	 */
	bool limited_color_range;

	/* Bitmask of encoder types (enum intel_output_type)
	 * driven by the pipe.
	 */
	unsigned int output_types;

	/* Whether we should send NULL infoframes. Required for audio. */
	bool has_hdmi_sink;

	/* Audio enabled on this pipe. Only valid if either has_hdmi_sink or
	 * has_dp_encoder is set. */
	bool has_audio;

	/*
	 * Enable dithering, used when the selected pipe bpp doesn't match the
	 * plane bpp.
	 */
	bool dither;

	/*
	 * Dither gets enabled for 18bpp which causes CRC mismatch errors for
	 * compliance video pattern tests.
	 * Disable dither only if it is a compliance test request for
	 * 18bpp.
	 */
	bool dither_force_disable;

	/* Controls for the clock computation, to override various stages. */
	bool clock_set;

	/* SDVO TV has a bunch of special case. To make multifunction encoders
	 * work correctly, we need to track this at runtime.*/
	bool sdvo_tv_clock;

	/*
	 * crtc bandwidth limit, don't increase pipe bpp or clock if not really
	 * required. This is set in the 2nd loop of calling encoder's
	 * ->compute_config if the first pick doesn't work out.
	 */
	bool bw_constrained;

	/* Settings for the intel dpll used on pretty much everything but
	 * haswell. */
	struct dpll dpll;

	/* Selected dpll when shared or NULL. */
	struct intel_shared_dpll *shared_dpll;

	/* Actual register state of the dpll, for shared dpll cross-checking. */
	struct intel_dpll_hw_state dpll_hw_state;

	/* DSI PLL registers */
	struct {
		u32 ctrl, div;
	} dsi_pll;

	int pipe_bpp;
	struct intel_link_m_n dp_m_n;

	/* m2_n2 for eDP downclock */
	struct intel_link_m_n dp_m2_n2;
	bool has_drrs;

	bool has_psr;
	bool has_psr2;

	/*
	 * Frequence the dpll for the port should run at. Differs from the
	 * adjusted dotclock e.g. for DP or 12bpc hdmi mode. This is also
	 * already multiplied by pixel_multiplier.
	 */
	int port_clock;

	/* Used by SDVO (and if we ever fix it, HDMI). */
	unsigned pixel_multiplier;

	u8 lane_count;

	/*
	 * Used by platforms having DP/HDMI PHY with programmable lane
	 * latency optimization.
	 */
	u8 lane_lat_optim_mask;

	/* minimum acceptable voltage level */
	u8 min_voltage_level;

	/* Panel fitter controls for gen2-gen4 + VLV */
	struct {
		u32 control;
		u32 pgm_ratios;
		u32 lvds_border_bits;
	} gmch_pfit;

	/* Panel fitter placement and size for Ironlake+ */
	struct {
		u32 pos;
		u32 size;
		bool enabled;
		bool force_thru;
	} pch_pfit;

	/* FDI configuration, only valid if has_pch_encoder is set. */
	int fdi_lanes;
	struct intel_link_m_n fdi_m_n;

	bool ips_enabled;

	bool crc_enabled;

	bool enable_fbc;

	bool double_wide;

	int pbn;

	struct intel_crtc_scaler_state scaler_state;

	/* w/a for waiting 2 vblanks during crtc enable */
	enum pipe hsw_workaround_pipe;

	/* IVB sprite scaling w/a (WaCxSRDisabledForSpriteScaling:ivb) */
	bool disable_lp_wm;

	struct intel_crtc_wm_state wm;

	u32 data_rate[I915_MAX_PLANES];

	/* Gamma mode programmed on the pipe */
	u32 gamma_mode;

	union {
		/* CSC mode programmed on the pipe */
		u32 csc_mode;

		/* CHV CGM mode */
		u32 cgm_mode;
	};

	/* bitmask of visible planes (enum plane_id) */
	u8 active_planes;
	u8 nv12_planes;
	u8 c8_planes;

	/* bitmask of planes that will be updated during the commit */
	u8 update_planes;

	struct {
		u32 enable;
		u32 gcp;
		union hdmi_infoframe avi;
		union hdmi_infoframe spd;
		union hdmi_infoframe hdmi;
		union hdmi_infoframe drm;
	} infoframes;

	/* HDMI scrambling status */
	bool hdmi_scrambling;

	/* HDMI High TMDS char rate ratio */
	bool hdmi_high_tmds_clock_ratio;

	/* Output format RGB/YCBCR etc */
	enum intel_output_format output_format;

	/* Output down scaling is done in LSPCON device */
	bool lspcon_downsampling;

	/* enable pipe gamma? */
	bool gamma_enable;

	/* enable pipe csc? */
	bool csc_enable;

	/* Display Stream compression state */
	struct {
		bool compression_enable;
		bool dsc_split;
		u16 compressed_bpp;
		u8 slice_count;
	} dsc_params;
	struct drm_dsc_config dp_dsc_cfg;

	/* Forward Error correction State */
	bool fec_enable;
};

struct intel_crtc {
	struct drm_crtc base;
	enum pipe pipe;
	/*
	 * Whether the crtc and the connected output pipeline is active. Implies
	 * that crtc->enabled is set, i.e. the current mode configuration has
	 * some outputs connected to this crtc.
	 */
	bool active;
	u8 plane_ids_mask;
	unsigned long long enabled_power_domains;
	struct intel_overlay *overlay;

	struct intel_crtc_state *config;

	/* Access to these should be protected by dev_priv->irq_lock. */
	bool cpu_fifo_underrun_disabled;
	bool pch_fifo_underrun_disabled;

	/* per-pipe watermark state */
	struct {
		/* watermarks currently being used  */
		union {
			struct intel_pipe_wm ilk;
			struct vlv_wm_state vlv;
			struct g4x_wm_state g4x;
		} active;
	} wm;

	int scanline_offset;

	struct {
		unsigned start_vbl_count;
		ktime_t start_vbl_time;
		int min_vbl, max_vbl;
		int scanline_start;
	} debug;

	/* scalers available on this crtc */
	int num_scalers;
};

struct intel_plane {
	struct drm_plane base;
	enum i9xx_plane_id i9xx_plane;
	enum plane_id id;
	enum pipe pipe;
	bool has_fbc;
	bool has_ccs;
	u32 frontbuffer_bit;

	struct {
		u32 base, cntl, size;
	} cursor;

	/*
	 * NOTE: Do not place new plane state fields here (e.g., when adding
	 * new plane properties).  New runtime state should now be placed in
	 * the intel_plane_state structure and accessed via plane_state.
	 */

	unsigned int (*max_stride)(struct intel_plane *plane,
				   u32 pixel_format, u64 modifier,
				   unsigned int rotation);
	void (*update_plane)(struct intel_plane *plane,
			     const struct intel_crtc_state *crtc_state,
			     const struct intel_plane_state *plane_state);
	void (*update_slave)(struct intel_plane *plane,
			     const struct intel_crtc_state *crtc_state,
			     const struct intel_plane_state *plane_state);
	void (*disable_plane)(struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state);
	bool (*get_hw_state)(struct intel_plane *plane, enum pipe *pipe);
	int (*check_plane)(struct intel_crtc_state *crtc_state,
			   struct intel_plane_state *plane_state);
};

struct intel_watermark_params {
	u16 fifo_size;
	u16 max_wm;
	u8 default_wm;
	u8 guard_size;
	u8 cacheline_size;
};

struct cxsr_latency {
	bool is_desktop : 1;
	bool is_ddr3 : 1;
	u16 fsb_freq;
	u16 mem_freq;
	u16 display_sr;
	u16 display_hpll_disable;
	u16 cursor_sr;
	u16 cursor_hpll_disable;
};

#define to_intel_atomic_state(x) container_of(x, struct intel_atomic_state, base)
#define to_intel_crtc(x) container_of(x, struct intel_crtc, base)
#define to_intel_crtc_state(x) container_of(x, struct intel_crtc_state, base)
#define to_intel_connector(x) container_of(x, struct intel_connector, base)
#define to_intel_encoder(x) container_of(x, struct intel_encoder, base)
#define to_intel_framebuffer(x) container_of(x, struct intel_framebuffer, base)
#define to_intel_plane(x) container_of(x, struct intel_plane, base)
#define to_intel_plane_state(x) container_of(x, struct intel_plane_state, base)
#define intel_fb_obj(x) ((x) ? to_intel_bo((x)->obj[0]) : NULL)

struct intel_hdmi {
	i915_reg_t hdmi_reg;
	int ddc_bus;
	struct {
		enum drm_dp_dual_mode_type type;
		int max_tmds_clock;
	} dp_dual_mode;
	bool has_hdmi_sink;
	bool has_audio;
	struct intel_connector *attached_connector;
	struct cec_notifier *cec_notifier;
};

struct intel_dp_mst_encoder;
#define DP_MAX_DOWNSTREAM_PORTS		0x10

/*
 * enum link_m_n_set:
 *	When platform provides two set of M_N registers for dp, we can
 *	program them and switch between them incase of DRRS.
 *	But When only one such register is provided, we have to program the
 *	required divider value on that registers itself based on the DRRS state.
 *
 * M1_N1	: Program dp_m_n on M1_N1 registers
 *			  dp_m2_n2 on M2_N2 registers (If supported)
 *
 * M2_N2	: Program dp_m2_n2 on M1_N1 registers
 *			  M2_N2 registers are not supported
 */

enum link_m_n_set {
	/* Sets the m1_n1 and m2_n2 */
	M1_N1 = 0,
	M2_N2
};

struct intel_dp_compliance_data {
	unsigned long edid;
	u8 video_pattern;
	u16 hdisplay, vdisplay;
	u8 bpc;
};

struct intel_dp_compliance {
	unsigned long test_type;
	struct intel_dp_compliance_data test_data;
	bool test_active;
	int test_link_rate;
	u8 test_lane_count;
};

struct intel_dp {
	i915_reg_t output_reg;
	u32 DP;
	int link_rate;
	u8 lane_count;
	u8 sink_count;
	bool link_mst;
	bool link_trained;
	bool has_audio;
	bool reset_link_params;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 psr_dpcd[EDP_PSR_RECEIVER_CAP_SIZE];
	u8 downstream_ports[DP_MAX_DOWNSTREAM_PORTS];
	u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
	u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE];
	u8 fec_capable;
	/* source rates */
	int num_source_rates;
	const int *source_rates;
	/* sink rates as reported by DP_MAX_LINK_RATE/DP_SUPPORTED_LINK_RATES */
	int num_sink_rates;
	int sink_rates[DP_MAX_SUPPORTED_RATES];
	bool use_rate_select;
	/* intersection of source and sink rates */
	int num_common_rates;
	int common_rates[DP_MAX_SUPPORTED_RATES];
	/* Max lane count for the current link */
	int max_link_lane_count;
	/* Max rate for the current link */
	int max_link_rate;
	/* sink or branch descriptor */
	struct drm_dp_desc desc;
	struct drm_dp_aux aux;
	u8 train_set[4];
	int panel_power_up_delay;
	int panel_power_down_delay;
	int panel_power_cycle_delay;
	int backlight_on_delay;
	int backlight_off_delay;
	struct delayed_work panel_vdd_work;
	bool want_panel_vdd;
	unsigned long last_power_on;
	unsigned long last_backlight_off;
	ktime_t panel_power_off_time;

	struct notifier_block edp_notifier;

	/*
	 * Pipe whose power sequencer is currently locked into
	 * this port. Only relevant on VLV/CHV.
	 */
	enum pipe pps_pipe;
	/*
	 * Pipe currently driving the port. Used for preventing
	 * the use of the PPS for any pipe currentrly driving
	 * external DP as that will mess things up on VLV.
	 */
	enum pipe active_pipe;
	/*
	 * Set if the sequencer may be reset due to a power transition,
	 * requiring a reinitialization. Only relevant on BXT.
	 */
	bool pps_reset;
	struct edp_power_seq pps_delays;

	bool can_mst; /* this port supports mst */
	bool is_mst;
	int active_mst_links;
	/* connector directly attached - won't be use for modeset in mst world */
	struct intel_connector *attached_connector;

	/* mst connector list */
	struct intel_dp_mst_encoder *mst_encoders[I915_MAX_PIPES];
	struct drm_dp_mst_topology_mgr mst_mgr;

	u32 (*get_aux_clock_divider)(struct intel_dp *dp, int index);
	/*
	 * This function returns the value we have to program the AUX_CTL
	 * register with to kick off an AUX transaction.
	 */
	u32 (*get_aux_send_ctl)(struct intel_dp *dp, int send_bytes,
				u32 aux_clock_divider);

	i915_reg_t (*aux_ch_ctl_reg)(struct intel_dp *dp);
	i915_reg_t (*aux_ch_data_reg)(struct intel_dp *dp, int index);

	/* This is called before a link training is starterd */
	void (*prepare_link_retrain)(struct intel_dp *intel_dp);

	/* Displayport compliance testing */
	struct intel_dp_compliance compliance;

	/* Display stream compression testing */
	bool force_dsc_en;
};

enum lspcon_vendor {
	LSPCON_VENDOR_MCA,
	LSPCON_VENDOR_PARADE
};

struct intel_lspcon {
	bool active;
	enum drm_lspcon_mode mode;
	enum lspcon_vendor vendor;
};

struct intel_digital_port {
	struct intel_encoder base;
	u32 saved_port_bits;
	struct intel_dp dp;
	struct intel_hdmi hdmi;
	struct intel_lspcon lspcon;
	enum irqreturn (*hpd_pulse)(struct intel_digital_port *, bool);
	bool release_cl2_override;
	u8 max_lanes;
	/* Used for DP and ICL+ TypeC/DP and TypeC/HDMI ports. */
	enum aux_ch aux_ch;
	enum intel_display_power_domain ddi_io_power_domain;
	bool tc_legacy_port:1;
	enum tc_port_mode tc_mode;

	void (*write_infoframe)(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				unsigned int type,
				const void *frame, ssize_t len);
	void (*read_infoframe)(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type,
			       void *frame, ssize_t len);
	void (*set_infoframes)(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state);
	u32 (*infoframes_enabled)(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config);
};

struct intel_dp_mst_encoder {
	struct intel_encoder base;
	enum pipe pipe;
	struct intel_digital_port *primary;
	struct intel_connector *connector;
};

static inline enum dpio_channel
vlv_dport_to_channel(struct intel_digital_port *dport)
{
	switch (dport->base.port) {
	case PORT_B:
	case PORT_D:
		return DPIO_CH0;
	case PORT_C:
		return DPIO_CH1;
	default:
		BUG();
	}
}

static inline enum dpio_phy
vlv_dport_to_phy(struct intel_digital_port *dport)
{
	switch (dport->base.port) {
	case PORT_B:
	case PORT_C:
		return DPIO_PHY0;
	case PORT_D:
		return DPIO_PHY1;
	default:
		BUG();
	}
}

static inline enum dpio_channel
vlv_pipe_to_channel(enum pipe pipe)
{
	switch (pipe) {
	case PIPE_A:
	case PIPE_C:
		return DPIO_CH0;
	case PIPE_B:
		return DPIO_CH1;
	default:
		BUG();
	}
}

static inline struct intel_crtc *
intel_get_crtc_for_pipe(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	return dev_priv->pipe_to_crtc_mapping[pipe];
}

static inline struct intel_crtc *
intel_get_crtc_for_plane(struct drm_i915_private *dev_priv, enum i9xx_plane_id plane)
{
	return dev_priv->plane_to_crtc_mapping[plane];
}

struct intel_load_detect_pipe {
	struct drm_atomic_state *restore_state;
};

static inline struct intel_encoder *
intel_attached_encoder(struct drm_connector *connector)
{
	return to_intel_connector(connector)->encoder;
}

static inline bool intel_encoder_is_dig_port(struct intel_encoder *encoder)
{
	switch (encoder->type) {
	case INTEL_OUTPUT_DDI:
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
	case INTEL_OUTPUT_HDMI:
		return true;
	default:
		return false;
	}
}

static inline struct intel_digital_port *
enc_to_dig_port(struct drm_encoder *encoder)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

	if (intel_encoder_is_dig_port(intel_encoder))
		return container_of(encoder, struct intel_digital_port,
				    base.base);
	else
		return NULL;
}

static inline struct intel_digital_port *
conn_to_dig_port(struct intel_connector *connector)
{
	return enc_to_dig_port(&intel_attached_encoder(&connector->base)->base);
}

static inline struct intel_dp_mst_encoder *
enc_to_mst(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_dp_mst_encoder, base.base);
}

static inline struct intel_dp *enc_to_intel_dp(struct drm_encoder *encoder)
{
	return &enc_to_dig_port(encoder)->dp;
}

static inline bool intel_encoder_is_dp(struct intel_encoder *encoder)
{
	switch (encoder->type) {
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
		return true;
	case INTEL_OUTPUT_DDI:
		/* Skip pure HDMI/DVI DDI encoders */
		return i915_mmio_reg_valid(enc_to_intel_dp(&encoder->base)->output_reg);
	default:
		return false;
	}
}

static inline struct intel_lspcon *
enc_to_intel_lspcon(struct drm_encoder *encoder)
{
	return &enc_to_dig_port(encoder)->lspcon;
}

static inline struct intel_digital_port *
dp_to_dig_port(struct intel_dp *intel_dp)
{
	return container_of(intel_dp, struct intel_digital_port, dp);
}

static inline struct intel_lspcon *
dp_to_lspcon(struct intel_dp *intel_dp)
{
	return &dp_to_dig_port(intel_dp)->lspcon;
}

static inline struct drm_i915_private *
dp_to_i915(struct intel_dp *intel_dp)
{
	return to_i915(dp_to_dig_port(intel_dp)->base.base.dev);
}

static inline struct intel_digital_port *
hdmi_to_dig_port(struct intel_hdmi *intel_hdmi)
{
	return container_of(intel_hdmi, struct intel_digital_port, hdmi);
}

static inline struct intel_plane_state *
intel_atomic_get_plane_state(struct intel_atomic_state *state,
				 struct intel_plane *plane)
{
	struct drm_plane_state *ret =
		drm_atomic_get_plane_state(&state->base, &plane->base);

	if (IS_ERR(ret))
		return ERR_CAST(ret);

	return to_intel_plane_state(ret);
}

static inline struct intel_plane_state *
intel_atomic_get_old_plane_state(struct intel_atomic_state *state,
				 struct intel_plane *plane)
{
	return to_intel_plane_state(drm_atomic_get_old_plane_state(&state->base,
								   &plane->base));
}

static inline struct intel_plane_state *
intel_atomic_get_new_plane_state(struct intel_atomic_state *state,
				 struct intel_plane *plane)
{
	return to_intel_plane_state(drm_atomic_get_new_plane_state(&state->base,
								   &plane->base));
}

static inline struct intel_crtc_state *
intel_atomic_get_old_crtc_state(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	return to_intel_crtc_state(drm_atomic_get_old_crtc_state(&state->base,
								 &crtc->base));
}

static inline struct intel_crtc_state *
intel_atomic_get_new_crtc_state(struct intel_atomic_state *state,
				struct intel_crtc *crtc)
{
	return to_intel_crtc_state(drm_atomic_get_new_crtc_state(&state->base,
								 &crtc->base));
}

/* intel_display.c */
void intel_plane_destroy(struct drm_plane *plane);
void i830_enable_pipe(struct drm_i915_private *dev_priv, enum pipe pipe);
void i830_disable_pipe(struct drm_i915_private *dev_priv, enum pipe pipe);
enum pipe intel_crtc_pch_transcoder(struct intel_crtc *crtc);
int vlv_get_hpll_vco(struct drm_i915_private *dev_priv);
int vlv_get_cck_clock(struct drm_i915_private *dev_priv,
		      const char *name, u32 reg, int ref_freq);
int vlv_get_cck_clock_hpll(struct drm_i915_private *dev_priv,
			   const char *name, u32 reg);
void lpt_disable_pch_transcoder(struct drm_i915_private *dev_priv);
void lpt_disable_iclkip(struct drm_i915_private *dev_priv);
void intel_init_display_hooks(struct drm_i915_private *dev_priv);
unsigned int intel_fb_xy_to_linear(int x, int y,
				   const struct intel_plane_state *state,
				   int plane);
unsigned int intel_fb_align_height(const struct drm_framebuffer *fb,
				   int color_plane, unsigned int height);
void intel_add_fb_offsets(int *x, int *y,
			  const struct intel_plane_state *state, int plane);
unsigned int intel_rotation_info_size(const struct intel_rotation_info *rot_info);
unsigned int intel_remapped_info_size(const struct intel_remapped_info *rem_info);
bool intel_has_pending_fb_unpin(struct drm_i915_private *dev_priv);
int intel_display_suspend(struct drm_device *dev);
void intel_pps_unlock_regs_wa(struct drm_i915_private *dev_priv);
void intel_encoder_destroy(struct drm_encoder *encoder);
struct drm_display_mode *
intel_encoder_current_mode(struct intel_encoder *encoder);
bool intel_port_is_combophy(struct drm_i915_private *dev_priv, enum port port);
bool intel_port_is_tc(struct drm_i915_private *dev_priv, enum port port);
enum tc_port intel_port_to_tc(struct drm_i915_private *dev_priv,
			      enum port port);
int intel_get_pipe_from_crtc_id_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);
enum transcoder intel_pipe_to_cpu_transcoder(struct drm_i915_private *dev_priv,
					     enum pipe pipe);
static inline bool
intel_crtc_has_type(const struct intel_crtc_state *crtc_state,
		    enum intel_output_type type)
{
	return crtc_state->output_types & (1 << type);
}
static inline bool
intel_crtc_has_dp_encoder(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_types &
		((1 << INTEL_OUTPUT_DP) |
		 (1 << INTEL_OUTPUT_DP_MST) |
		 (1 << INTEL_OUTPUT_EDP));
}
static inline void
intel_wait_for_vblank(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	drm_wait_one_vblank(&dev_priv->drm, pipe);
}
static inline void
intel_wait_for_vblank_if_active(struct drm_i915_private *dev_priv, int pipe)
{
	const struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, pipe);

	if (crtc->active)
		intel_wait_for_vblank(dev_priv, pipe);
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc);

int ironlake_get_lanes_required(int target_clock, int link_bw, int bpp);
void vlv_wait_port_ready(struct drm_i915_private *dev_priv,
			 struct intel_digital_port *dport,
			 unsigned int expected_mask);
int intel_get_load_detect_pipe(struct drm_connector *connector,
			       const struct drm_display_mode *mode,
			       struct intel_load_detect_pipe *old,
			       struct drm_modeset_acquire_ctx *ctx);
void intel_release_load_detect_pipe(struct drm_connector *connector,
				    struct intel_load_detect_pipe *old,
				    struct drm_modeset_acquire_ctx *ctx);
struct i915_vma *
intel_pin_and_fence_fb_obj(struct drm_framebuffer *fb,
			   const struct i915_ggtt_view *view,
			   bool uses_fence,
			   unsigned long *out_flags);
void intel_unpin_fb_vma(struct i915_vma *vma, unsigned long flags);
struct drm_framebuffer *
intel_framebuffer_create(struct drm_i915_gem_object *obj,
			 struct drm_mode_fb_cmd2 *mode_cmd);
int intel_prepare_plane_fb(struct drm_plane *plane,
			   struct drm_plane_state *new_state);
void intel_cleanup_plane_fb(struct drm_plane *plane,
			    struct drm_plane_state *old_state);

void assert_pch_transcoder_disabled(struct drm_i915_private *dev_priv,
				    enum pipe pipe);

int vlv_force_pll_on(struct drm_i915_private *dev_priv, enum pipe pipe,
		     const struct dpll *dpll);
void vlv_force_pll_off(struct drm_i915_private *dev_priv, enum pipe pipe);
int lpt_get_iclkip(struct drm_i915_private *dev_priv);
bool intel_fuzzy_clock_check(int clock1, int clock2);

/* modesetting asserts */
void assert_panel_unlocked(struct drm_i915_private *dev_priv,
			   enum pipe pipe);
void assert_pll(struct drm_i915_private *dev_priv,
		enum pipe pipe, bool state);
#define assert_pll_enabled(d, p) assert_pll(d, p, true)
#define assert_pll_disabled(d, p) assert_pll(d, p, false)
void assert_dsi_pll(struct drm_i915_private *dev_priv, bool state);
#define assert_dsi_pll_enabled(d) assert_dsi_pll(d, true)
#define assert_dsi_pll_disabled(d) assert_dsi_pll(d, false)
void assert_fdi_rx_pll(struct drm_i915_private *dev_priv,
		       enum pipe pipe, bool state);
#define assert_fdi_rx_pll_enabled(d, p) assert_fdi_rx_pll(d, p, true)
#define assert_fdi_rx_pll_disabled(d, p) assert_fdi_rx_pll(d, p, false)
void assert_pipe(struct drm_i915_private *dev_priv, enum pipe pipe, bool state);
#define assert_pipe_enabled(d, p) assert_pipe(d, p, true)
#define assert_pipe_disabled(d, p) assert_pipe(d, p, false)
void intel_prepare_reset(struct drm_i915_private *dev_priv);
void intel_finish_reset(struct drm_i915_private *dev_priv);
void intel_dp_get_m_n(struct intel_crtc *crtc,
		      struct intel_crtc_state *pipe_config);
void intel_dp_set_m_n(const struct intel_crtc_state *crtc_state,
		      enum link_m_n_set m_n);
void intel_dp_ycbcr_420_enable(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state);
int intel_dotclock_calculate(int link_freq, const struct intel_link_m_n *m_n);
bool bxt_find_best_dpll(struct intel_crtc_state *crtc_state,
			struct dpll *best_clock);
int chv_calc_dpll_params(int refclk, struct dpll *pll_clock);

bool intel_crtc_active(struct intel_crtc *crtc);
bool hsw_crtc_state_ips_capable(const struct intel_crtc_state *crtc_state);
void hsw_enable_ips(const struct intel_crtc_state *crtc_state);
void hsw_disable_ips(const struct intel_crtc_state *crtc_state);
enum intel_display_power_domain intel_port_to_power_domain(enum port port);
enum intel_display_power_domain
intel_aux_power_domain(struct intel_digital_port *dig_port);
void intel_mode_from_pipe_config(struct drm_display_mode *mode,
				 struct intel_crtc_state *pipe_config);
void intel_crtc_arm_fifo_underrun(struct intel_crtc *crtc,
				  struct intel_crtc_state *crtc_state);

u16 skl_scaler_calc_phase(int sub, int scale, bool chroma_center);
int skl_update_scaler_crtc(struct intel_crtc_state *crtc_state);
int skl_max_scale(const struct intel_crtc_state *crtc_state,
		  u32 pixel_format);

static inline u32 intel_plane_ggtt_offset(const struct intel_plane_state *state)
{
	return i915_ggtt_offset(state->vma);
}

u32 glk_plane_color_ctl(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
u32 glk_plane_color_ctl_crtc(const struct intel_crtc_state *crtc_state);
u32 skl_plane_ctl(const struct intel_crtc_state *crtc_state,
		  const struct intel_plane_state *plane_state);
u32 skl_plane_ctl_crtc(const struct intel_crtc_state *crtc_state);
u32 skl_plane_stride(const struct intel_plane_state *plane_state,
		     int plane);
int skl_check_plane_surface(struct intel_plane_state *plane_state);
int i9xx_check_plane_surface(struct intel_plane_state *plane_state);
int skl_format_to_fourcc(int format, bool rgb_order, bool alpha);
unsigned int i9xx_plane_max_stride(struct intel_plane *plane,
				   u32 pixel_format, u64 modifier,
				   unsigned int rotation);
int bdw_get_pipemisc_bpp(struct intel_crtc *crtc);

#endif /* __INTEL_DRV_H__ */
