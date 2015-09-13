/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __FSL_DCU_DRM_DRV_H__
#define __FSL_DCU_DRM_DRV_H__

#include "fsl_dcu_drm_crtc.h"
#include "fsl_dcu_drm_output.h"
#include "fsl_dcu_drm_plane.h"

#define DCU_DCU_MODE			0x0010
#define DCU_MODE_BLEND_ITER(x)		((x) << 20)
#define DCU_MODE_RASTER_EN		BIT(14)
#define DCU_MODE_DCU_MODE(x)		(x)
#define DCU_MODE_DCU_MODE_MASK		0x03
#define DCU_MODE_OFF			0
#define DCU_MODE_NORMAL			1
#define DCU_MODE_TEST			2
#define DCU_MODE_COLORBAR		3

#define DCU_BGND			0x0014
#define DCU_BGND_R(x)			((x) << 16)
#define DCU_BGND_G(x)			((x) << 8)
#define DCU_BGND_B(x)			(x)

#define DCU_DISP_SIZE			0x0018
#define DCU_DISP_SIZE_DELTA_Y(x)	((x) << 16)
/*Regisiter value 1/16 of horizontal resolution*/
#define DCU_DISP_SIZE_DELTA_X(x)	((x) >> 4)

#define DCU_HSYN_PARA			0x001c
#define DCU_HSYN_PARA_BP(x)		((x) << 22)
#define DCU_HSYN_PARA_PW(x)		((x) << 11)
#define DCU_HSYN_PARA_FP(x)		(x)

#define DCU_VSYN_PARA			0x0020
#define DCU_VSYN_PARA_BP(x)		((x) << 22)
#define DCU_VSYN_PARA_PW(x)		((x) << 11)
#define DCU_VSYN_PARA_FP(x)		(x)

#define DCU_SYN_POL			0x0024
#define DCU_SYN_POL_INV_PXCK_FALL	(0 << 6)
#define DCU_SYN_POL_NEG_REMAIN		(0 << 5)
#define DCU_SYN_POL_INV_VS_LOW		BIT(1)
#define DCU_SYN_POL_INV_HS_LOW		BIT(0)

#define DCU_THRESHOLD			0x0028
#define DCU_THRESHOLD_LS_BF_VS(x)	((x) << 16)
#define DCU_THRESHOLD_OUT_BUF_HIGH(x)	((x) << 8)
#define DCU_THRESHOLD_OUT_BUF_LOW(x)	(x)
#define BF_VS_VAL			0x03
#define BUF_MAX_VAL			0x78
#define BUF_MIN_VAL			0x0a

#define DCU_INT_STATUS			0x002C
#define DCU_INT_STATUS_VSYNC		BIT(0)
#define DCU_INT_STATUS_UNDRUN		BIT(1)
#define DCU_INT_STATUS_LSBFVS		BIT(2)
#define DCU_INT_STATUS_VBLANK		BIT(3)
#define DCU_INT_STATUS_CRCREADY		BIT(4)
#define DCU_INT_STATUS_CRCOVERFLOW	BIT(5)
#define DCU_INT_STATUS_P1FIFOLO		BIT(6)
#define DCU_INT_STATUS_P1FIFOHI		BIT(7)
#define DCU_INT_STATUS_P2FIFOLO		BIT(8)
#define DCU_INT_STATUS_P2FIFOHI		BIT(9)
#define DCU_INT_STATUS_PROGEND		BIT(10)
#define DCU_INT_STATUS_IPMERROR		BIT(11)
#define DCU_INT_STATUS_LYRTRANS		BIT(12)
#define DCU_INT_STATUS_DMATRANS		BIT(14)
#define DCU_INT_STATUS_P3FIFOLO		BIT(16)
#define DCU_INT_STATUS_P3FIFOHI		BIT(17)
#define DCU_INT_STATUS_P4FIFOLO		BIT(18)
#define DCU_INT_STATUS_P4FIFOHI		BIT(19)
#define DCU_INT_STATUS_P1EMPTY		BIT(26)
#define DCU_INT_STATUS_P2EMPTY		BIT(27)
#define DCU_INT_STATUS_P3EMPTY		BIT(28)
#define DCU_INT_STATUS_P4EMPTY		BIT(29)

#define DCU_INT_MASK			0x0030
#define DCU_INT_MASK_VSYNC		BIT(0)
#define DCU_INT_MASK_UNDRUN		BIT(1)
#define DCU_INT_MASK_LSBFVS		BIT(2)
#define DCU_INT_MASK_VBLANK		BIT(3)
#define DCU_INT_MASK_CRCREADY		BIT(4)
#define DCU_INT_MASK_CRCOVERFLOW	BIT(5)
#define DCU_INT_MASK_P1FIFOLO		BIT(6)
#define DCU_INT_MASK_P1FIFOHI		BIT(7)
#define DCU_INT_MASK_P2FIFOLO		BIT(8)
#define DCU_INT_MASK_P2FIFOHI		BIT(9)
#define DCU_INT_MASK_PROGEND		BIT(10)
#define DCU_INT_MASK_IPMERROR		BIT(11)
#define DCU_INT_MASK_LYRTRANS		BIT(12)
#define DCU_INT_MASK_DMATRANS		BIT(14)
#define DCU_INT_MASK_P3FIFOLO		BIT(16)
#define DCU_INT_MASK_P3FIFOHI		BIT(17)
#define DCU_INT_MASK_P4FIFOLO		BIT(18)
#define DCU_INT_MASK_P4FIFOHI		BIT(19)
#define DCU_INT_MASK_P1EMPTY		BIT(26)
#define DCU_INT_MASK_P2EMPTY		BIT(27)
#define DCU_INT_MASK_P3EMPTY		BIT(28)
#define DCU_INT_MASK_P4EMPTY		BIT(29)

#define DCU_DIV_RATIO			0x0054

#define DCU_UPDATE_MODE			0x00cc
#define DCU_UPDATE_MODE_MODE		BIT(31)
#define DCU_UPDATE_MODE_READREG		BIT(30)

#define DCU_DCFB_MAX			0x300

#define DCU_CTRLDESCLN(layer, reg)	(0x200 + (reg - 1) * 4 + (layer) * 0x40)

#define DCU_LAYER_HEIGHT(x)		((x) << 16)
#define DCU_LAYER_WIDTH(x)		(x)

#define DCU_LAYER_POSY(x)		((x) << 16)
#define DCU_LAYER_POSX(x)		(x)

#define DCU_LAYER_EN			BIT(31)
#define DCU_LAYER_TILE_EN		BIT(30)
#define DCU_LAYER_DATA_SEL_CLUT		BIT(29)
#define DCU_LAYER_SAFETY_EN		BIT(28)
#define DCU_LAYER_TRANS(x)		((x) << 20)
#define DCU_LAYER_BPP(x)		((x) << 16)
#define DCU_LAYER_RLE_EN		BIT(15)
#define DCU_LAYER_LUOFFS(x)		((x) << 4)
#define DCU_LAYER_BB_ON			BIT(2)
#define DCU_LAYER_AB(x)			(x)

#define DCU_LAYER_CKMAX_R(x)		((x) << 16)
#define DCU_LAYER_CKMAX_G(x)		((x) << 8)
#define DCU_LAYER_CKMAX_B(x)		(x)

#define DCU_LAYER_CKMIN_R(x)		((x) << 16)
#define DCU_LAYER_CKMIN_G(x)		((x) << 8)
#define DCU_LAYER_CKMIN_B(x)		(x)

#define DCU_LAYER_TILE_VER(x)		((x) << 16)
#define DCU_LAYER_TILE_HOR(x)		(x)

#define DCU_LAYER_FG_FCOLOR(x)		(x)

#define DCU_LAYER_BG_BCOLOR(x)		(x)

#define DCU_LAYER_POST_SKIP(x)		((x) << 16)
#define DCU_LAYER_PRE_SKIP(x)		(x)

#define FSL_DCU_RGB565			4
#define FSL_DCU_RGB888			5
#define FSL_DCU_ARGB8888		6
#define FSL_DCU_ARGB1555		11
#define FSL_DCU_ARGB4444		12
#define FSL_DCU_YUV422			14

#define VF610_LAYER_REG_NUM		9
#define LS1021A_LAYER_REG_NUM		10

struct clk;
struct device;
struct drm_device;

struct fsl_dcu_soc_data {
	const char *name;
	/*total layer number*/
	unsigned int total_layer;
	/*max layer number DCU supported*/
	unsigned int max_layer;
};

struct fsl_dcu_drm_device {
	struct device *dev;
	struct device_node *np;
	struct regmap *regmap;
	int irq;
	struct clk *clk;
	/*protects hardware register*/
	spinlock_t irq_lock;
	struct drm_device *drm;
	struct drm_fbdev_cma *fbdev;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct fsl_dcu_drm_connector connector;
	const struct fsl_dcu_soc_data *soc;
};

void fsl_dcu_fbdev_init(struct drm_device *dev);
int fsl_dcu_drm_modeset_init(struct fsl_dcu_drm_device *fsl_dev);

#endif /* __FSL_DCU_DRM_DRV_H__ */
