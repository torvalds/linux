/*
 * Copyright (c) 2009-2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef __INTEL_DRV_H__
#define __INTEL_DRV_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <linux/gpio.h>

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

/* these are outputs from the chip - integrated only
 * external chips are via DVO or SDVO output */
#define INTEL_OUTPUT_UNUSED 0
#define INTEL_OUTPUT_ANALOG 1
#define INTEL_OUTPUT_DVO 2
#define INTEL_OUTPUT_SDVO 3
#define INTEL_OUTPUT_LVDS 4
#define INTEL_OUTPUT_TVOUT 5
#define INTEL_OUTPUT_HDMI 6
#define INTEL_OUTPUT_MIPI 7
#define INTEL_OUTPUT_MIPI2 8

#define INTEL_DVO_CHIP_NONE 0
#define INTEL_DVO_CHIP_LVDS 1
#define INTEL_DVO_CHIP_TMDS 2
#define INTEL_DVO_CHIP_TVOUT 4

#define INTEL_MODE_PIXEL_MULTIPLIER_SHIFT (0x0)
#define INTEL_MODE_PIXEL_MULTIPLIER_MASK (0xf << INTEL_MODE_PIXEL_MULTIPLIER_SHIFT)

static inline void
psb_intel_mode_set_pixel_multiplier(struct drm_display_mode *mode,
				int multiplier)
{
	mode->clock *= multiplier;
	mode->private_flags |= multiplier;
}

static inline int
psb_intel_mode_get_pixel_multiplier(const struct drm_display_mode *mode)
{
	return (mode->private_flags & INTEL_MODE_PIXEL_MULTIPLIER_MASK)
	       >> INTEL_MODE_PIXEL_MULTIPLIER_SHIFT;
}


/*
 * Hold information useally put on the device driver privates here,
 * since it needs to be shared across multiple of devices drivers privates.
 */
struct psb_intel_mode_device {

	/*
	 * Abstracted memory manager operations
	 */
	 size_t(*bo_offset) (struct drm_device *dev, void *bo);

	/*
	 * LVDS info
	 */
	int backlight_duty_cycle;	/* restore backlight to this value */
	bool panel_wants_dither;
	struct drm_display_mode *panel_fixed_mode;
	struct drm_display_mode *panel_fixed_mode2;
	struct drm_display_mode *vbt_mode;	/* if any */

	uint32_t saveBLC_PWM_CTL;
};

struct psb_intel_i2c_chan {
	/* for getting at dev. private (mmio etc.) */
	struct drm_device *drm_dev;
	u32 reg;		/* GPIO reg */
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
	u8 slave_addr;
};

struct psb_intel_encoder {
	struct drm_encoder base;
	int type;
	bool needs_tv_clock;
	void (*hot_plug)(struct psb_intel_encoder *);
	int crtc_mask;
	int clone_mask;
	void *dev_priv; /* For sdvo_priv, lvds_priv, etc... */

	/* FIXME: Either make SDVO and LVDS store it's i2c here or give CDV it's
	   own set of output privates */
	struct psb_intel_i2c_chan *i2c_bus;
	struct psb_intel_i2c_chan *ddc_bus;
};

struct psb_intel_connector {
	struct drm_connector base;
	struct psb_intel_encoder *encoder;
};

struct psb_intel_crtc_state {
	uint32_t saveDSPCNTR;
	uint32_t savePIPECONF;
	uint32_t savePIPESRC;
	uint32_t saveDPLL;
	uint32_t saveFP0;
	uint32_t saveFP1;
	uint32_t saveHTOTAL;
	uint32_t saveHBLANK;
	uint32_t saveHSYNC;
	uint32_t saveVTOTAL;
	uint32_t saveVBLANK;
	uint32_t saveVSYNC;
	uint32_t saveDSPSTRIDE;
	uint32_t saveDSPSIZE;
	uint32_t saveDSPPOS;
	uint32_t saveDSPBASE;
	uint32_t savePalette[256];
};

struct psb_intel_crtc {
	struct drm_crtc base;
	int pipe;
	int plane;
	uint32_t cursor_addr;
	struct gtt_range *cursor_gt;
	u8 lut_r[256], lut_g[256], lut_b[256];
	u8 lut_adj[256];
	struct psb_intel_framebuffer *fbdev_fb;
	/* a mode_set for fbdev users on this crtc */
	struct drm_mode_set mode_set;

	/* GEM object that holds our cursor */
	struct drm_gem_object *cursor_obj;

	struct drm_display_mode saved_mode;
	struct drm_display_mode saved_adjusted_mode;

	struct psb_intel_mode_device *mode_dev;

	/*crtc mode setting flags*/
	u32 mode_flags;

	bool active;
	bool crtc_enable;

	/* Saved Crtc HW states */
	struct psb_intel_crtc_state *crtc_state;
};

#define to_psb_intel_crtc(x)	\
		container_of(x, struct psb_intel_crtc, base)
#define to_psb_intel_connector(x) \
		container_of(x, struct psb_intel_connector, base)
#define to_psb_intel_encoder(x)	\
		container_of(x, struct psb_intel_encoder, base)
#define to_psb_intel_framebuffer(x)	\
		container_of(x, struct psb_intel_framebuffer, base)

struct psb_intel_i2c_chan *psb_intel_i2c_create(struct drm_device *dev,
					const u32 reg, const char *name);
void psb_intel_i2c_destroy(struct psb_intel_i2c_chan *chan);
int psb_intel_ddc_get_modes(struct drm_connector *connector,
			    struct i2c_adapter *adapter);
extern bool psb_intel_ddc_probe(struct i2c_adapter *adapter);

extern void psb_intel_crtc_init(struct drm_device *dev, int pipe,
			    struct psb_intel_mode_device *mode_dev);
extern void psb_intel_crt_init(struct drm_device *dev);
extern bool psb_intel_sdvo_init(struct drm_device *dev, int output_device);
extern void psb_intel_dvo_init(struct drm_device *dev);
extern void psb_intel_tv_init(struct drm_device *dev);
extern void psb_intel_lvds_init(struct drm_device *dev,
			    struct psb_intel_mode_device *mode_dev);
extern void psb_intel_lvds_set_brightness(struct drm_device *dev, int level);
extern void oaktrail_lvds_init(struct drm_device *dev,
			   struct psb_intel_mode_device *mode_dev);
extern void oaktrail_wait_for_INTR_PKT_SENT(struct drm_device *dev);
extern void oaktrail_dsi_init(struct drm_device *dev,
			   struct psb_intel_mode_device *mode_dev);
extern void mid_dsi_init(struct drm_device *dev,
		    struct psb_intel_mode_device *mode_dev, int dsi_num);

extern void psb_intel_crtc_load_lut(struct drm_crtc *crtc);
extern void psb_intel_encoder_prepare(struct drm_encoder *encoder);
extern void psb_intel_encoder_commit(struct drm_encoder *encoder);
extern void psb_intel_encoder_destroy(struct drm_encoder *encoder);

static inline struct psb_intel_encoder *psb_intel_attached_encoder(
						struct drm_connector *connector)
{
	return to_psb_intel_connector(connector)->encoder;
}

extern void psb_intel_connector_attach_encoder(
					struct psb_intel_connector *connector,
					struct psb_intel_encoder *encoder);

extern struct drm_encoder *psb_intel_best_encoder(struct drm_connector
					      *connector);

extern struct drm_display_mode *psb_intel_crtc_mode_get(struct drm_device *dev,
						    struct drm_crtc *crtc);
extern void psb_intel_wait_for_vblank(struct drm_device *dev);
extern int psb_intel_get_pipe_from_crtc_id(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern struct drm_crtc *psb_intel_get_crtc_from_pipe(struct drm_device *dev,
						 int pipe);
extern struct drm_connector *psb_intel_sdvo_find(struct drm_device *dev,
					     int sdvoB);
extern int psb_intel_sdvo_supports_hotplug(struct drm_connector *connector);
extern void psb_intel_sdvo_set_hotplug(struct drm_connector *connector,
				   int enable);
extern int intelfb_probe(struct drm_device *dev);
extern int intelfb_remove(struct drm_device *dev,
			  struct drm_framebuffer *fb);
extern struct drm_framebuffer *psb_intel_framebuffer_create(struct drm_device
							*dev, struct
							drm_mode_fb_cmd
							*mode_cmd,
							void *mm_private);
extern bool psb_intel_lvds_mode_fixup(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode);
extern int psb_intel_lvds_mode_valid(struct drm_connector *connector,
				     struct drm_display_mode *mode);
extern int psb_intel_lvds_set_property(struct drm_connector *connector,
					struct drm_property *property,
					uint64_t value);
extern void psb_intel_lvds_destroy(struct drm_connector *connector);
extern const struct drm_encoder_funcs psb_intel_lvds_enc_funcs;

/* intel_gmbus.c */
extern void gma_intel_i2c_reset(struct drm_device *dev);
extern int gma_intel_setup_gmbus(struct drm_device *dev);
extern void gma_intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed);
extern void gma_intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
extern void gma_intel_teardown_gmbus(struct drm_device *dev);

#endif				/* __INTEL_DRV_H__ */
