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

#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>
#include "i915_drv.h"
#include "drm_crtc.h"

#include "drm_crtc_helper.h"
/*
 * Display related stuff
 */

/* store information about an Ixxx DVO */
/* The i830->i865 use multiple DVOs with multiple i2cs */
/* the i915, i945 have a single sDVO i2c bus - which is different */
#define MAX_OUTPUTS 6
/* maximum connectors per crtcs in the mode set */
#define INTELFB_CONN_LIMIT 4

#define INTEL_I2C_BUS_DVO 1
#define INTEL_I2C_BUS_SDVO 2

/* these are outputs from the chip - integrated only
   external chips are via DVO or SDVO output */
#define INTEL_OUTPUT_UNUSED 0
#define INTEL_OUTPUT_ANALOG 1
#define INTEL_OUTPUT_DVO 2
#define INTEL_OUTPUT_SDVO 3
#define INTEL_OUTPUT_LVDS 4
#define INTEL_OUTPUT_TVOUT 5
#define INTEL_OUTPUT_HDMI 6
#define INTEL_OUTPUT_DISPLAYPORT 7
#define INTEL_OUTPUT_EDP 8

/* Intel Pipe Clone Bit */
#define INTEL_HDMIB_CLONE_BIT 1
#define INTEL_HDMIC_CLONE_BIT 2
#define INTEL_HDMID_CLONE_BIT 3
#define INTEL_HDMIE_CLONE_BIT 4
#define INTEL_HDMIF_CLONE_BIT 5
#define INTEL_SDVO_NON_TV_CLONE_BIT 6
#define INTEL_SDVO_TV_CLONE_BIT 7
#define INTEL_SDVO_LVDS_CLONE_BIT 8
#define INTEL_ANALOG_CLONE_BIT 9
#define INTEL_TV_CLONE_BIT 10
#define INTEL_DP_B_CLONE_BIT 11
#define INTEL_DP_C_CLONE_BIT 12
#define INTEL_DP_D_CLONE_BIT 13
#define INTEL_LVDS_CLONE_BIT 14
#define INTEL_DVO_TMDS_CLONE_BIT 15
#define INTEL_DVO_LVDS_CLONE_BIT 16
#define INTEL_EDP_CLONE_BIT 17

#define INTEL_DVO_CHIP_NONE 0
#define INTEL_DVO_CHIP_LVDS 1
#define INTEL_DVO_CHIP_TMDS 2
#define INTEL_DVO_CHIP_TVOUT 4

struct intel_i2c_chan {
	struct drm_device *drm_dev; /* for getting at dev. private (mmio etc.) */
	u32 reg; /* GPIO reg */
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
};

struct intel_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};


struct intel_output {
	struct drm_connector base;

	struct drm_encoder enc;
	int type;
	struct i2c_adapter *i2c_bus;
	struct i2c_adapter *ddc_bus;
	bool load_detect_temp;
	bool needs_tv_clock;
	void *dev_priv;
	void (*hot_plug)(struct intel_output *);
	int crtc_mask;
	int clone_mask;
};

struct intel_crtc {
	struct drm_crtc base;
	enum pipe pipe;
	enum plane plane;
	struct drm_gem_object *cursor_bo;
	uint32_t cursor_addr;
	u8 lut_r[256], lut_g[256], lut_b[256];
	int dpms_mode;
	bool busy; /* is scanout buffer being updated frequently? */
	struct timer_list idle_timer;
	bool lowfreq_avail;
};

#define to_intel_crtc(x) container_of(x, struct intel_crtc, base)
#define to_intel_output(x) container_of(x, struct intel_output, base)
#define enc_to_intel_output(x) container_of(x, struct intel_output, enc)
#define to_intel_framebuffer(x) container_of(x, struct intel_framebuffer, base)

struct i2c_adapter *intel_i2c_create(struct drm_device *dev, const u32 reg,
				     const char *name);
void intel_i2c_destroy(struct i2c_adapter *adapter);
int intel_ddc_get_modes(struct intel_output *intel_output);
extern bool intel_ddc_probe(struct intel_output *intel_output);
void intel_i2c_quirk_set(struct drm_device *dev, bool enable);
void intel_i2c_reset_gmbus(struct drm_device *dev);

extern void intel_crt_init(struct drm_device *dev);
extern void intel_hdmi_init(struct drm_device *dev, int sdvox_reg);
extern bool intel_sdvo_init(struct drm_device *dev, int output_device);
extern void intel_dvo_init(struct drm_device *dev);
extern void intel_tv_init(struct drm_device *dev);
extern void intel_mark_busy(struct drm_device *dev, struct drm_gem_object *obj);
extern void intel_lvds_init(struct drm_device *dev);
extern void intel_dp_init(struct drm_device *dev, int dp_reg);
void
intel_dp_set_m_n(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode);
extern void intel_edp_link_config (struct intel_output *, int *, int *);


extern void intel_crtc_load_lut(struct drm_crtc *crtc);
extern void intel_encoder_prepare (struct drm_encoder *encoder);
extern void intel_encoder_commit (struct drm_encoder *encoder);

extern struct drm_encoder *intel_best_encoder(struct drm_connector *connector);

extern struct drm_display_mode *intel_crtc_mode_get(struct drm_device *dev,
						    struct drm_crtc *crtc);
int intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern void intel_wait_for_vblank(struct drm_device *dev);
extern struct drm_crtc *intel_get_crtc_from_pipe(struct drm_device *dev, int pipe);
extern struct drm_crtc *intel_get_load_detect_pipe(struct intel_output *intel_output,
						   struct drm_display_mode *mode,
						   int *dpms_mode);
extern void intel_release_load_detect_pipe(struct intel_output *intel_output,
					   int dpms_mode);

extern struct drm_connector* intel_sdvo_find(struct drm_device *dev, int sdvoB);
extern int intel_sdvo_supports_hotplug(struct drm_connector *connector);
extern void intel_sdvo_set_hotplug(struct drm_connector *connector, int enable);
extern int intelfb_probe(struct drm_device *dev);
extern int intelfb_remove(struct drm_device *dev, struct drm_framebuffer *fb);
extern int intelfb_resize(struct drm_device *dev, struct drm_crtc *crtc);
extern void intelfb_restore(void);
extern void intel_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
				    u16 blue, int regno);
extern void intel_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
				    u16 *blue, int regno);

extern int intel_framebuffer_create(struct drm_device *dev,
				    struct drm_mode_fb_cmd *mode_cmd,
				    struct drm_framebuffer **fb,
				    struct drm_gem_object *obj);

#endif /* __INTEL_DRV_H__ */
