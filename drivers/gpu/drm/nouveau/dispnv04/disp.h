#ifndef __NV04_DISPLAY_H__
#define __NV04_DISPLAY_H__
#include <subdev/bios.h>
#include <subdev/bios/pll.h>

#include "nouveau_display.h"

enum nv04_fp_display_regs {
	FP_DISPLAY_END,
	FP_TOTAL,
	FP_CRTC,
	FP_SYNC_START,
	FP_SYNC_END,
	FP_VALID_START,
	FP_VALID_END
};

struct nv04_crtc_reg {
	unsigned char MiscOutReg;
	uint8_t CRTC[0xa0];
	uint8_t CR58[0x10];
	uint8_t Sequencer[5];
	uint8_t Graphics[9];
	uint8_t Attribute[21];
	unsigned char DAC[768];

	/* PCRTC regs */
	uint32_t fb_start;
	uint32_t crtc_cfg;
	uint32_t cursor_cfg;
	uint32_t gpio_ext;
	uint32_t crtc_830;
	uint32_t crtc_834;
	uint32_t crtc_850;
	uint32_t crtc_eng_ctrl;

	/* PRAMDAC regs */
	uint32_t nv10_cursync;
	struct nvkm_pll_vals pllvals;
	uint32_t ramdac_gen_ctrl;
	uint32_t ramdac_630;
	uint32_t ramdac_634;
	uint32_t tv_setup;
	uint32_t tv_vtotal;
	uint32_t tv_vskew;
	uint32_t tv_vsync_delay;
	uint32_t tv_htotal;
	uint32_t tv_hskew;
	uint32_t tv_hsync_delay;
	uint32_t tv_hsync_delay2;
	uint32_t fp_horiz_regs[7];
	uint32_t fp_vert_regs[7];
	uint32_t dither;
	uint32_t fp_control;
	uint32_t dither_regs[6];
	uint32_t fp_debug_0;
	uint32_t fp_debug_1;
	uint32_t fp_debug_2;
	uint32_t fp_margin_color;
	uint32_t ramdac_8c0;
	uint32_t ramdac_a20;
	uint32_t ramdac_a24;
	uint32_t ramdac_a34;
	uint32_t ctv_regs[38];
};

struct nv04_output_reg {
	uint32_t output;
	int head;
};

struct nv04_mode_state {
	struct nv04_crtc_reg crtc_reg[2];
	uint32_t pllsel;
	uint32_t sel_clk;
};

struct nv04_display {
	struct nv04_mode_state mode_reg;
	struct nv04_mode_state saved_reg;
	uint32_t saved_vga_font[4][16384];
	uint32_t dac_users[4];
	struct nouveau_bo *image[2];
};

static inline struct nv04_display *
nv04_display(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

/* nv04_display.c */
int nv04_display_create(struct drm_device *);
void nv04_display_destroy(struct drm_device *);
int nv04_display_init(struct drm_device *);
void nv04_display_fini(struct drm_device *);

/* nv04_crtc.c */
int nv04_crtc_create(struct drm_device *, int index);

/* nv04_dac.c */
int nv04_dac_create(struct drm_connector *, struct dcb_output *);
uint32_t nv17_dac_sample_load(struct drm_encoder *encoder);
int nv04_dac_output_offset(struct drm_encoder *encoder);
void nv04_dac_update_dacclk(struct drm_encoder *encoder, bool enable);
bool nv04_dac_in_use(struct drm_encoder *encoder);

/* nv04_dfp.c */
int nv04_dfp_create(struct drm_connector *, struct dcb_output *);
int nv04_dfp_get_bound_head(struct drm_device *dev, struct dcb_output *dcbent);
void nv04_dfp_bind_head(struct drm_device *dev, struct dcb_output *dcbent,
			       int head, bool dl);
void nv04_dfp_disable(struct drm_device *dev, int head);
void nv04_dfp_update_fp_control(struct drm_encoder *encoder, int mode);

/* nv04_tv.c */
int nv04_tv_identify(struct drm_device *dev, int i2c_index);
int nv04_tv_create(struct drm_connector *, struct dcb_output *);

/* nv17_tv.c */
int nv17_tv_create(struct drm_connector *, struct dcb_output *);

/* overlay.c */
void nouveau_overlay_init(struct drm_device *dev);

static inline bool
nv_two_heads(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	const int impl = dev->pdev->device & 0x0ff0;

	if (drm->device.info.family >= NV_DEVICE_INFO_V0_CELSIUS && impl != 0x0100 &&
	    impl != 0x0150 && impl != 0x01a0 && impl != 0x0200)
		return true;

	return false;
}

static inline bool
nv_gf4_disp_arch(struct drm_device *dev)
{
	return nv_two_heads(dev) && (dev->pdev->device & 0x0ff0) != 0x0110;
}

static inline bool
nv_two_reg_pll(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	const int impl = dev->pdev->device & 0x0ff0;

	if (impl == 0x0310 || impl == 0x0340 || drm->device.info.family >= NV_DEVICE_INFO_V0_CURIE)
		return true;
	return false;
}

static inline bool
nv_match_device(struct drm_device *dev, unsigned device,
		unsigned sub_vendor, unsigned sub_device)
{
	return dev->pdev->device == device &&
		dev->pdev->subsystem_vendor == sub_vendor &&
		dev->pdev->subsystem_device == sub_device;
}

#include <subdev/bios.h>
#include <subdev/bios/init.h>

static inline void
nouveau_bios_run_init_table(struct drm_device *dev, u16 table,
			    struct dcb_output *outp, int crtc)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_bios *bios = nvxx_bios(&drm->device);
	struct nvbios_init init = {
		.subdev = &bios->subdev,
		.bios = bios,
		.offset = table,
		.outp = outp,
		.crtc = crtc,
		.execute = 1,
	};

	nvbios_exec(&init);
}

#endif
