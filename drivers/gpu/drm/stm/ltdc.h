/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Mickael Reulier <mickael.reulier@st.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _LTDC_H_
#define _LTDC_H_

struct ltdc_caps {
	u32 hw_version;		/* hardware version */
	u32 nb_layers;		/* number of supported layers */
	u32 reg_ofs;		/* register offset for applicable regs */
	u32 bus_width;		/* bus width (32 or 64 bits) */
	const u32 *pix_fmt_hw;	/* supported pixel formats */
};

struct ltdc_device {
	struct drm_fbdev_cma *fbdev;
	void __iomem *regs;
	struct clk *pixel_clk;	/* lcd pixel clock */
	struct drm_panel *panel;
	struct mutex err_lock;	/* protecting error_status */
	struct ltdc_caps caps;
	u32 clut[256];		/* color look up table */
	u32 error_status;
	u32 irq_status;
};

int ltdc_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe);
void ltdc_crtc_disable_vblank(struct drm_device *dev, unsigned int pipe);
int ltdc_load(struct drm_device *ddev);
void ltdc_unload(struct drm_device *ddev);

#endif
