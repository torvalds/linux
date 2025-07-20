/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __I9XX_WM_REGS_H__
#define __I9XX_WM_REGS_H__

#include "intel_display_reg_defs.h"

#define DSPARB(dev_priv)			_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70030)
#define   DSPARB_CSTART_MASK	(0x7f << 7)
#define   DSPARB_CSTART_SHIFT	7
#define   DSPARB_BSTART_MASK	(0x7f)
#define   DSPARB_BSTART_SHIFT	0
#define   DSPARB_BEND_SHIFT	9 /* on 855 */
#define   DSPARB_AEND_SHIFT	0
#define   DSPARB_SPRITEA_SHIFT_VLV	0
#define   DSPARB_SPRITEA_MASK_VLV	(0xff << 0)
#define   DSPARB_SPRITEB_SHIFT_VLV	8
#define   DSPARB_SPRITEB_MASK_VLV	(0xff << 8)
#define   DSPARB_SPRITEC_SHIFT_VLV	16
#define   DSPARB_SPRITEC_MASK_VLV	(0xff << 16)
#define   DSPARB_SPRITED_SHIFT_VLV	24
#define   DSPARB_SPRITED_MASK_VLV	(0xff << 24)
#define DSPARB2				_MMIO(VLV_DISPLAY_BASE + 0x70060) /* vlv/chv */
#define   DSPARB_SPRITEA_HI_SHIFT_VLV	0
#define   DSPARB_SPRITEA_HI_MASK_VLV	(0x1 << 0)
#define   DSPARB_SPRITEB_HI_SHIFT_VLV	4
#define   DSPARB_SPRITEB_HI_MASK_VLV	(0x1 << 4)
#define   DSPARB_SPRITEC_HI_SHIFT_VLV	8
#define   DSPARB_SPRITEC_HI_MASK_VLV	(0x1 << 8)
#define   DSPARB_SPRITED_HI_SHIFT_VLV	12
#define   DSPARB_SPRITED_HI_MASK_VLV	(0x1 << 12)
#define   DSPARB_SPRITEE_HI_SHIFT_VLV	16
#define   DSPARB_SPRITEE_HI_MASK_VLV	(0x1 << 16)
#define   DSPARB_SPRITEF_HI_SHIFT_VLV	20
#define   DSPARB_SPRITEF_HI_MASK_VLV	(0x1 << 20)
#define DSPARB3				_MMIO(VLV_DISPLAY_BASE + 0x7006c) /* chv */
#define   DSPARB_SPRITEE_SHIFT_VLV	0
#define   DSPARB_SPRITEE_MASK_VLV	(0xff << 0)
#define   DSPARB_SPRITEF_SHIFT_VLV	8
#define   DSPARB_SPRITEF_MASK_VLV	(0xff << 8)

/* pnv/gen4/g4x/vlv/chv */
#define DSPFW1(dev_priv)		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70034)
#define   DSPFW_SR_SHIFT		23
#define   DSPFW_SR_MASK			(0x1ff << 23)
#define   DSPFW_CURSORB_SHIFT		16
#define   DSPFW_CURSORB_MASK		(0x3f << 16)
#define   DSPFW_PLANEB_SHIFT		8
#define   DSPFW_PLANEB_MASK		(0x7f << 8)
#define   DSPFW_PLANEB_MASK_VLV		(0xff << 8) /* vlv/chv */
#define   DSPFW_PLANEA_SHIFT		0
#define   DSPFW_PLANEA_MASK		(0x7f << 0)
#define   DSPFW_PLANEA_MASK_VLV		(0xff << 0) /* vlv/chv */
#define DSPFW2(dev_priv)		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70038)
#define   DSPFW_FBC_SR_EN		(1 << 31)	  /* g4x */
#define   DSPFW_FBC_SR_SHIFT		28
#define   DSPFW_FBC_SR_MASK		(0x7 << 28) /* g4x */
#define   DSPFW_FBC_HPLL_SR_SHIFT	24
#define   DSPFW_FBC_HPLL_SR_MASK	(0xf << 24) /* g4x */
#define   DSPFW_SPRITEB_SHIFT		(16)
#define   DSPFW_SPRITEB_MASK		(0x7f << 16) /* g4x */
#define   DSPFW_SPRITEB_MASK_VLV	(0xff << 16) /* vlv/chv */
#define   DSPFW_CURSORA_SHIFT		8
#define   DSPFW_CURSORA_MASK		(0x3f << 8)
#define   DSPFW_PLANEC_OLD_SHIFT	0
#define   DSPFW_PLANEC_OLD_MASK		(0x7f << 0) /* pre-gen4 sprite C */
#define   DSPFW_SPRITEA_SHIFT		0
#define   DSPFW_SPRITEA_MASK		(0x7f << 0) /* g4x */
#define   DSPFW_SPRITEA_MASK_VLV	(0xff << 0) /* vlv/chv */
#define DSPFW3(dev_priv)		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x7003c)
#define   DSPFW_HPLL_SR_EN		(1 << 31)
#define   PINEVIEW_SELF_REFRESH_EN	(1 << 30)
#define   DSPFW_CURSOR_SR_SHIFT		24
#define   DSPFW_CURSOR_SR_MASK		(0x3f << 24)
#define   DSPFW_HPLL_CURSOR_SHIFT	16
#define   DSPFW_HPLL_CURSOR_MASK	(0x3f << 16)
#define   DSPFW_HPLL_SR_SHIFT		0
#define   DSPFW_HPLL_SR_MASK		(0x1ff << 0)

/* vlv/chv */
#define DSPFW4		_MMIO(VLV_DISPLAY_BASE + 0x70070)
#define   DSPFW_SPRITEB_WM1_SHIFT	16
#define   DSPFW_SPRITEB_WM1_MASK	(0xff << 16)
#define   DSPFW_CURSORA_WM1_SHIFT	8
#define   DSPFW_CURSORA_WM1_MASK	(0x3f << 8)
#define   DSPFW_SPRITEA_WM1_SHIFT	0
#define   DSPFW_SPRITEA_WM1_MASK	(0xff << 0)
#define DSPFW5		_MMIO(VLV_DISPLAY_BASE + 0x70074)
#define   DSPFW_PLANEB_WM1_SHIFT	24
#define   DSPFW_PLANEB_WM1_MASK		(0xff << 24)
#define   DSPFW_PLANEA_WM1_SHIFT	16
#define   DSPFW_PLANEA_WM1_MASK		(0xff << 16)
#define   DSPFW_CURSORB_WM1_SHIFT	8
#define   DSPFW_CURSORB_WM1_MASK	(0x3f << 8)
#define   DSPFW_CURSOR_SR_WM1_SHIFT	0
#define   DSPFW_CURSOR_SR_WM1_MASK	(0x3f << 0)
#define DSPFW6		_MMIO(VLV_DISPLAY_BASE + 0x70078)
#define   DSPFW_SR_WM1_SHIFT		0
#define   DSPFW_SR_WM1_MASK		(0x1ff << 0)
#define DSPFW7		_MMIO(VLV_DISPLAY_BASE + 0x7007c)
#define DSPFW7_CHV	_MMIO(VLV_DISPLAY_BASE + 0x700b4) /* wtf #1? */
#define   DSPFW_SPRITED_WM1_SHIFT	24
#define   DSPFW_SPRITED_WM1_MASK	(0xff << 24)
#define   DSPFW_SPRITED_SHIFT		16
#define   DSPFW_SPRITED_MASK_VLV	(0xff << 16)
#define   DSPFW_SPRITEC_WM1_SHIFT	8
#define   DSPFW_SPRITEC_WM1_MASK	(0xff << 8)
#define   DSPFW_SPRITEC_SHIFT		0
#define   DSPFW_SPRITEC_MASK_VLV	(0xff << 0)
#define DSPFW8_CHV	_MMIO(VLV_DISPLAY_BASE + 0x700b8)
#define   DSPFW_SPRITEF_WM1_SHIFT	24
#define   DSPFW_SPRITEF_WM1_MASK	(0xff << 24)
#define   DSPFW_SPRITEF_SHIFT		16
#define   DSPFW_SPRITEF_MASK_VLV	(0xff << 16)
#define   DSPFW_SPRITEE_WM1_SHIFT	8
#define   DSPFW_SPRITEE_WM1_MASK	(0xff << 8)
#define   DSPFW_SPRITEE_SHIFT		0
#define   DSPFW_SPRITEE_MASK_VLV	(0xff << 0)
#define DSPFW9_CHV	_MMIO(VLV_DISPLAY_BASE + 0x7007c) /* wtf #2? */
#define   DSPFW_PLANEC_WM1_SHIFT	24
#define   DSPFW_PLANEC_WM1_MASK		(0xff << 24)
#define   DSPFW_PLANEC_SHIFT		16
#define   DSPFW_PLANEC_MASK_VLV		(0xff << 16)
#define   DSPFW_CURSORC_WM1_SHIFT	8
#define   DSPFW_CURSORC_WM1_MASK	(0x3f << 16)
#define   DSPFW_CURSORC_SHIFT		0
#define   DSPFW_CURSORC_MASK		(0x3f << 0)

/* vlv/chv high order bits */
#define DSPHOWM		_MMIO(VLV_DISPLAY_BASE + 0x70064)
#define   DSPFW_SR_HI_SHIFT		24
#define   DSPFW_SR_HI_MASK		(3 << 24) /* 2 bits for chv, 1 for vlv */
#define   DSPFW_SPRITEF_HI_SHIFT	23
#define   DSPFW_SPRITEF_HI_MASK		(1 << 23)
#define   DSPFW_SPRITEE_HI_SHIFT	22
#define   DSPFW_SPRITEE_HI_MASK		(1 << 22)
#define   DSPFW_PLANEC_HI_SHIFT		21
#define   DSPFW_PLANEC_HI_MASK		(1 << 21)
#define   DSPFW_SPRITED_HI_SHIFT	20
#define   DSPFW_SPRITED_HI_MASK		(1 << 20)
#define   DSPFW_SPRITEC_HI_SHIFT	16
#define   DSPFW_SPRITEC_HI_MASK		(1 << 16)
#define   DSPFW_PLANEB_HI_SHIFT		12
#define   DSPFW_PLANEB_HI_MASK		(1 << 12)
#define   DSPFW_SPRITEB_HI_SHIFT	8
#define   DSPFW_SPRITEB_HI_MASK		(1 << 8)
#define   DSPFW_SPRITEA_HI_SHIFT	4
#define   DSPFW_SPRITEA_HI_MASK		(1 << 4)
#define   DSPFW_PLANEA_HI_SHIFT		0
#define   DSPFW_PLANEA_HI_MASK		(1 << 0)
#define DSPHOWM1	_MMIO(VLV_DISPLAY_BASE + 0x70068)
#define   DSPFW_SR_WM1_HI_SHIFT		24
#define   DSPFW_SR_WM1_HI_MASK		(3 << 24) /* 2 bits for chv, 1 for vlv */
#define   DSPFW_SPRITEF_WM1_HI_SHIFT	23
#define   DSPFW_SPRITEF_WM1_HI_MASK	(1 << 23)
#define   DSPFW_SPRITEE_WM1_HI_SHIFT	22
#define   DSPFW_SPRITEE_WM1_HI_MASK	(1 << 22)
#define   DSPFW_PLANEC_WM1_HI_SHIFT	21
#define   DSPFW_PLANEC_WM1_HI_MASK	(1 << 21)
#define   DSPFW_SPRITED_WM1_HI_SHIFT	20
#define   DSPFW_SPRITED_WM1_HI_MASK	(1 << 20)
#define   DSPFW_SPRITEC_WM1_HI_SHIFT	16
#define   DSPFW_SPRITEC_WM1_HI_MASK	(1 << 16)
#define   DSPFW_PLANEB_WM1_HI_SHIFT	12
#define   DSPFW_PLANEB_WM1_HI_MASK	(1 << 12)
#define   DSPFW_SPRITEB_WM1_HI_SHIFT	8
#define   DSPFW_SPRITEB_WM1_HI_MASK	(1 << 8)
#define   DSPFW_SPRITEA_WM1_HI_SHIFT	4
#define   DSPFW_SPRITEA_WM1_HI_MASK	(1 << 4)
#define   DSPFW_PLANEA_WM1_HI_SHIFT	0
#define   DSPFW_PLANEA_WM1_HI_MASK	(1 << 0)

/* drain latency register values*/
#define VLV_DDL(pipe)			_MMIO(VLV_DISPLAY_BASE + 0x70050 + 4 * (pipe))
#define DDL_CURSOR_SHIFT		24
#define DDL_SPRITE_SHIFT(sprite)	(8 + 8 * (sprite))
#define DDL_PLANE_SHIFT			0
#define DDL_PRECISION_HIGH		(1 << 7)
#define DDL_PRECISION_LOW		(0 << 7)
#define DRAIN_LATENCY_MASK		0x7f

/* FIFO watermark sizes etc */
#define G4X_FIFO_LINE_SIZE	64
#define I915_FIFO_LINE_SIZE	64
#define I830_FIFO_LINE_SIZE	32

#define VALLEYVIEW_FIFO_SIZE	255
#define G4X_FIFO_SIZE		127
#define I965_FIFO_SIZE		512
#define I945_FIFO_SIZE		127
#define I915_FIFO_SIZE		95
#define I855GM_FIFO_SIZE	127 /* In cachelines */
#define I830_FIFO_SIZE		95

#define VALLEYVIEW_MAX_WM	0xff
#define G4X_MAX_WM		0x3f
#define I915_MAX_WM		0x3f

#define PINEVIEW_DISPLAY_FIFO	512 /* in 64byte unit */
#define PINEVIEW_FIFO_LINE_SIZE	64
#define PINEVIEW_MAX_WM		0x1ff
#define PINEVIEW_DFT_WM		0x3f
#define PINEVIEW_DFT_HPLLOFF_WM	0
#define PINEVIEW_GUARD_WM		10
#define PINEVIEW_CURSOR_FIFO		64
#define PINEVIEW_CURSOR_MAX_WM	0x3f
#define PINEVIEW_CURSOR_DFT_WM	0
#define PINEVIEW_CURSOR_GUARD_WM	5

#define VALLEYVIEW_CURSOR_MAX_WM 64
#define I965_CURSOR_FIFO	64
#define I965_CURSOR_MAX_WM	32
#define I965_CURSOR_DFT_WM	8

/* define the Watermark register on Ironlake */
#define _WM0_PIPEA_ILK		0x45100
#define _WM0_PIPEB_ILK		0x45104
#define _WM0_PIPEC_IVB		0x45200
#define WM0_PIPE_ILK(pipe)	_MMIO_BASE_PIPE3(0, (pipe), _WM0_PIPEA_ILK, \
						 _WM0_PIPEB_ILK, _WM0_PIPEC_IVB)
#define  WM0_PIPE_PRIMARY_MASK	REG_GENMASK(31, 16)
#define  WM0_PIPE_SPRITE_MASK	REG_GENMASK(15, 8)
#define  WM0_PIPE_CURSOR_MASK	REG_GENMASK(7, 0)
#define  WM0_PIPE_PRIMARY(x)	REG_FIELD_PREP(WM0_PIPE_PRIMARY_MASK, (x))
#define  WM0_PIPE_SPRITE(x)	REG_FIELD_PREP(WM0_PIPE_SPRITE_MASK, (x))
#define  WM0_PIPE_CURSOR(x)	REG_FIELD_PREP(WM0_PIPE_CURSOR_MASK, (x))
#define WM1_LP_ILK		_MMIO(0x45108)
#define WM2_LP_ILK		_MMIO(0x4510c)
#define WM3_LP_ILK		_MMIO(0x45110)
#define  WM_LP_ENABLE		REG_BIT(31)
#define  WM_LP_LATENCY_MASK	REG_GENMASK(30, 24)
#define  WM_LP_FBC_MASK_BDW	REG_GENMASK(23, 19)
#define  WM_LP_FBC_MASK_ILK	REG_GENMASK(23, 20)
#define  WM_LP_PRIMARY_MASK	REG_GENMASK(18, 8)
#define  WM_LP_CURSOR_MASK	REG_GENMASK(7, 0)
#define  WM_LP_LATENCY(x)	REG_FIELD_PREP(WM_LP_LATENCY_MASK, (x))
#define  WM_LP_FBC_BDW(x)	REG_FIELD_PREP(WM_LP_FBC_MASK_BDW, (x))
#define  WM_LP_FBC_ILK(x)	REG_FIELD_PREP(WM_LP_FBC_MASK_ILK, (x))
#define  WM_LP_PRIMARY(x)	REG_FIELD_PREP(WM_LP_PRIMARY_MASK, (x))
#define  WM_LP_CURSOR(x)	REG_FIELD_PREP(WM_LP_CURSOR_MASK, (x))
#define WM1S_LP_ILK		_MMIO(0x45120)
#define WM2S_LP_IVB		_MMIO(0x45124)
#define WM3S_LP_IVB		_MMIO(0x45128)
#define  WM_LP_SPRITE_ENABLE	REG_BIT(31) /* ilk/snb WM1S only */
#define  WM_LP_SPRITE_MASK	REG_GENMASK(10, 0)
#define  WM_LP_SPRITE(x)	REG_FIELD_PREP(WM_LP_SPRITE_MASK, (x))

#define WM_MISC				_MMIO(0x45260)
#define  WM_MISC_DATA_PARTITION_5_6	(1 << 0)

#define WM_DBG				_MMIO(0x45280)
#define  WM_DBG_DISALLOW_MULTIPLE_LP	(1 << 0)
#define  WM_DBG_DISALLOW_MAXFIFO	(1 << 1)
#define  WM_DBG_DISALLOW_SPRITE		(1 << 2)

#endif /* __I9XX_WM_REGS_H__ */
