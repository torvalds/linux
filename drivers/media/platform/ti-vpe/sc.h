/*
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef TI_SC_H
#define TI_SC_H

/* Scaler regs */
#define CFG_SC0				0x0
#define CFG_INTERLACE_O			(1 << 0)
#define CFG_LINEAR			(1 << 1)
#define CFG_SC_BYPASS			(1 << 2)
#define CFG_INVT_FID			(1 << 3)
#define CFG_USE_RAV			(1 << 4)
#define CFG_ENABLE_EV			(1 << 5)
#define CFG_AUTO_HS			(1 << 6)
#define CFG_DCM_2X			(1 << 7)
#define CFG_DCM_4X			(1 << 8)
#define CFG_HP_BYPASS			(1 << 9)
#define CFG_INTERLACE_I			(1 << 10)
#define CFG_ENABLE_SIN2_VER_INTP	(1 << 11)
#define CFG_Y_PK_EN			(1 << 14)
#define CFG_TRIM			(1 << 15)
#define CFG_SELFGEN_FID			(1 << 16)

#define CFG_SC1				0x4
#define CFG_ROW_ACC_INC_MASK		0x07ffffff
#define CFG_ROW_ACC_INC_SHIFT		0

#define CFG_SC2				0x08
#define CFG_ROW_ACC_OFFSET_MASK		0x0fffffff
#define CFG_ROW_ACC_OFFSET_SHIFT	0

#define CFG_SC3				0x0c
#define CFG_ROW_ACC_OFFSET_B_MASK	0x0fffffff
#define CFG_ROW_ACC_OFFSET_B_SHIFT	0

#define CFG_SC4				0x10
#define CFG_TAR_H_MASK			0x07ff
#define CFG_TAR_H_SHIFT			0
#define CFG_TAR_W_MASK			0x07ff
#define CFG_TAR_W_SHIFT			12
#define CFG_LIN_ACC_INC_U_MASK		0x07
#define CFG_LIN_ACC_INC_U_SHIFT		24
#define CFG_NLIN_ACC_INIT_U_MASK	0x07
#define CFG_NLIN_ACC_INIT_U_SHIFT	28

#define CFG_SC5				0x14
#define CFG_SRC_H_MASK			0x07ff
#define CFG_SRC_H_SHIFT			0
#define CFG_SRC_W_MASK			0x07ff
#define CFG_SRC_W_SHIFT			12
#define CFG_NLIN_ACC_INC_U_MASK		0x07
#define CFG_NLIN_ACC_INC_U_SHIFT	24

#define CFG_SC6				0x18
#define CFG_ROW_ACC_INIT_RAV_MASK	0x03ff
#define CFG_ROW_ACC_INIT_RAV_SHIFT	0
#define CFG_ROW_ACC_INIT_RAV_B_MASK	0x03ff
#define CFG_ROW_ACC_INIT_RAV_B_SHIFT	10

#define CFG_SC8				0x20
#define CFG_NLIN_LEFT_MASK		0x07ff
#define CFG_NLIN_LEFT_SHIFT		0
#define CFG_NLIN_RIGHT_MASK		0x07ff
#define CFG_NLIN_RIGHT_SHIFT		12

#define CFG_SC9				0x24
#define CFG_LIN_ACC_INC			CFG_SC9

#define CFG_SC10			0x28
#define CFG_NLIN_ACC_INIT		CFG_SC10

#define CFG_SC11			0x2c
#define CFG_NLIN_ACC_INC		CFG_SC11

#define CFG_SC12			0x30
#define CFG_COL_ACC_OFFSET_MASK		0x01ffffff
#define CFG_COL_ACC_OFFSET_SHIFT	0

#define CFG_SC13			0x34
#define CFG_SC_FACTOR_RAV_MASK		0xff
#define CFG_SC_FACTOR_RAV_SHIFT		0
#define CFG_CHROMA_INTP_THR_MASK	0x03ff
#define CFG_CHROMA_INTP_THR_SHIFT	12
#define CFG_DELTA_CHROMA_THR_MASK	0x0f
#define CFG_DELTA_CHROMA_THR_SHIFT	24

#define CFG_SC17			0x44
#define CFG_EV_THR_MASK			0x03ff
#define CFG_EV_THR_SHIFT		12
#define CFG_DELTA_LUMA_THR_MASK		0x0f
#define CFG_DELTA_LUMA_THR_SHIFT	24
#define CFG_DELTA_EV_THR_MASK		0x0f
#define CFG_DELTA_EV_THR_SHIFT		28

#define CFG_SC18			0x48
#define CFG_HS_FACTOR_MASK		0x03ff
#define CFG_HS_FACTOR_SHIFT		0
#define CFG_CONF_DEFAULT_MASK		0x01ff
#define CFG_CONF_DEFAULT_SHIFT		16

#define CFG_SC19			0x4c
#define CFG_HPF_COEFF0_MASK		0xff
#define CFG_HPF_COEFF0_SHIFT		0
#define CFG_HPF_COEFF1_MASK		0xff
#define CFG_HPF_COEFF1_SHIFT		8
#define CFG_HPF_COEFF2_MASK		0xff
#define CFG_HPF_COEFF2_SHIFT		16
#define CFG_HPF_COEFF3_MASK		0xff
#define CFG_HPF_COEFF3_SHIFT		23

#define CFG_SC20			0x50
#define CFG_HPF_COEFF4_MASK		0xff
#define CFG_HPF_COEFF4_SHIFT		0
#define CFG_HPF_COEFF5_MASK		0xff
#define CFG_HPF_COEFF5_SHIFT		8
#define CFG_HPF_NORM_SHIFT_MASK		0x07
#define CFG_HPF_NORM_SHIFT_SHIFT	16
#define CFG_NL_LIMIT_MASK		0x1ff
#define CFG_NL_LIMIT_SHIFT		20

#define CFG_SC21			0x54
#define CFG_NL_LO_THR_MASK		0x01ff
#define CFG_NL_LO_THR_SHIFT		0
#define CFG_NL_LO_SLOPE_MASK		0xff
#define CFG_NL_LO_SLOPE_SHIFT		16

#define CFG_SC22			0x58
#define CFG_NL_HI_THR_MASK		0x01ff
#define CFG_NL_HI_THR_SHIFT		0
#define CFG_NL_HI_SLOPE_SH_MASK		0x07
#define CFG_NL_HI_SLOPE_SH_SHIFT	16

#define CFG_SC23			0x5c
#define CFG_GRADIENT_THR_MASK		0x07ff
#define CFG_GRADIENT_THR_SHIFT		0
#define CFG_GRADIENT_THR_RANGE_MASK	0x0f
#define CFG_GRADIENT_THR_RANGE_SHIFT	12
#define CFG_MIN_GY_THR_MASK		0xff
#define CFG_MIN_GY_THR_SHIFT		16
#define CFG_MIN_GY_THR_RANGE_MASK	0x0f
#define CFG_MIN_GY_THR_RANGE_SHIFT	28

#define CFG_SC24			0x60
#define CFG_ORG_H_MASK			0x07ff
#define CFG_ORG_H_SHIFT			0
#define CFG_ORG_W_MASK			0x07ff
#define CFG_ORG_W_SHIFT			16

#define CFG_SC25			0x64
#define CFG_OFF_H_MASK			0x07ff
#define CFG_OFF_H_SHIFT			0
#define CFG_OFF_W_MASK			0x07ff
#define CFG_OFF_W_SHIFT			16

/* number of phases supported by the polyphase scalers */
#define SC_NUM_PHASES			32

/* number of taps used by horizontal polyphase scaler */
#define SC_H_NUM_TAPS			7

/* number of taps used by vertical polyphase scaler */
#define SC_V_NUM_TAPS			5

/* number of taps expected by the scaler in it's coefficient memory */
#define SC_NUM_TAPS_MEM_ALIGN		8

/* Maximum frame width the scaler can handle (in pixels) */
#define SC_MAX_PIXEL_WIDTH		2047

/* Maximum frame height the scaler can handle (in lines) */
#define SC_MAX_PIXEL_HEIGHT		2047

/*
 * coefficient memory size in bytes:
 * num phases x num sets(luma and chroma) x num taps(aligned) x coeff size
 */
#define SC_COEF_SRAM_SIZE	(SC_NUM_PHASES * 2 * SC_NUM_TAPS_MEM_ALIGN * 2)

struct sc_data {
	void __iomem		*base;
	struct resource		*res;

	dma_addr_t		loaded_coeff_h; /* loaded h coeffs in SC */
	dma_addr_t		loaded_coeff_v; /* loaded v coeffs in SC */

	bool			load_coeff_h;	/* have new h SC coeffs */
	bool			load_coeff_v;	/* have new v SC coeffs */

	struct platform_device *pdev;
};

void sc_dump_regs(struct sc_data *sc);
void sc_set_hs_coeffs(struct sc_data *sc, void *addr, unsigned int src_w,
		unsigned int dst_w);
void sc_set_vs_coeffs(struct sc_data *sc, void *addr, unsigned int src_h,
		unsigned int dst_h);
void sc_config_scaler(struct sc_data *sc, u32 *sc_reg0, u32 *sc_reg8,
		u32 *sc_reg17, unsigned int src_w, unsigned int src_h,
		unsigned int dst_w, unsigned int dst_h);
struct sc_data *sc_create(struct platform_device *pdev, const char *res_name);

#endif
