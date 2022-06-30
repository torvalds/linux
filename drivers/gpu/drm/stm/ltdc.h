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
	u32 layer_ofs;		/* layer offset for applicable regs */
	const u32 *layer_regs;	/* layer register offset */
	u32 bus_width;		/* bus width (32 or 64 bits) */
	const u32 *pix_fmt_hw;	/* supported hw pixel formats */
	const u32 *pix_fmt_drm;	/* supported drm pixel formats */
	int pix_fmt_nb;		/* number of pixel format */
	bool pix_fmt_flex;	/* pixel format flexibility supported */
	bool non_alpha_only_l1; /* non-native no-alpha formats on layer 1 */
	int pad_max_freq_hz;	/* max frequency supported by pad */
	int nb_irq;		/* number of hardware interrupts */
	bool ycbcr_input;	/* ycbcr input converter supported */
	bool ycbcr_output;	/* ycbcr output converter supported */
	bool plane_reg_shadow;	/* plane shadow registers ability */
	bool crc;		/* cyclic redundancy check supported */
	bool dynamic_zorder;	/* dynamic z-order */
	bool plane_rotation;	/* plane rotation */
	bool fifo_threshold;	/* fifo underrun threshold supported */
};

#define LTDC_MAX_LAYER	4

struct fps_info {
	unsigned int counter;
	ktime_t last_timestamp;
};

struct ltdc_device {
	void __iomem *regs;
	struct regmap *regmap;
	struct clk *pixel_clk;	/* lcd pixel clock */
	struct mutex err_lock;	/* protecting error_status */
	struct ltdc_caps caps;
	u32 irq_status;
	u32 fifo_err;		/* fifo underrun error counter */
	u32 fifo_warn;		/* fifo underrun warning counter */
	u32 fifo_threshold;	/* fifo underrun threshold */
	u32 transfer_err;	/* transfer error counter */
	struct fps_info plane_fpsi[LTDC_MAX_LAYER];
	struct drm_atomic_state *suspend_state;
	int crc_skip_count;
	bool crc_active;
};

int ltdc_load(struct drm_device *ddev);
void ltdc_unload(struct drm_device *ddev);
void ltdc_suspend(struct drm_device *ddev);
int ltdc_resume(struct drm_device *ddev);

#endif
