/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Mickael Reulier <mickael.reulier@st.com>
 */

#ifndef _LTDC_H_
#define _LTDC_H_

struct ltdc_caps {
	u32 hw_version;		/* hardware version */
	u32 nb_layers;		/* number of supported layers */
	u32 reg_ofs;		/* register offset for applicable regs */
	u32 bus_width;		/* bus width (32 or 64 bits) */
	const u32 *pix_fmt_hw;	/* supported pixel formats */
	bool non_alpha_only_l1; /* non-native no-alpha formats on layer 1 */
	int pad_max_freq_hz;	/* max frequency supported by pad */
};

#define LTDC_MAX_LAYER	4

struct fps_info {
	unsigned int counter;
	ktime_t last_timestamp;
};

struct ltdc_device {
	void __iomem *regs;
	struct clk *pixel_clk;	/* lcd pixel clock */
	struct mutex err_lock;	/* protecting error_status */
	struct ltdc_caps caps;
	u32 error_status;
	u32 irq_status;
	struct fps_info plane_fpsi[LTDC_MAX_LAYER];
	struct drm_atomic_state *suspend_state;
};

bool ltdc_crtc_scanoutpos(struct drm_device *dev, unsigned int pipe,
			  bool in_vblank_irq, int *vpos, int *hpos,
			  ktime_t *stime, ktime_t *etime,
			  const struct drm_display_mode *mode);

int ltdc_load(struct drm_device *ddev);
void ltdc_unload(struct drm_device *ddev);
void ltdc_suspend(struct drm_device *ddev);
int ltdc_resume(struct drm_device *ddev);

#endif
