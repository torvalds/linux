/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __VLV_DSI_PLL_REGS_H__
#define __VLV_DSI_PLL_REGS_H__

#include "vlv_dsi_regs.h"

#define MIPIO_TXESC_CLK_DIV1			_MMIO(0x160004)
#define  GLK_TX_ESC_CLK_DIV1_MASK			0x3FF
#define MIPIO_TXESC_CLK_DIV2			_MMIO(0x160008)
#define  GLK_TX_ESC_CLK_DIV2_MASK			0x3FF

#define BXT_MAX_VAR_OUTPUT_KHZ			39500

#define BXT_MIPI_CLOCK_CTL			_MMIO(0x46090)
#define  BXT_MIPI1_DIV_SHIFT			26
#define  BXT_MIPI2_DIV_SHIFT			10
#define  BXT_MIPI_DIV_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_DIV_SHIFT, \
					BXT_MIPI2_DIV_SHIFT)

/* TX control divider to select actual TX clock output from (8x/var) */
#define  BXT_MIPI1_TX_ESCLK_SHIFT		26
#define  BXT_MIPI2_TX_ESCLK_SHIFT		10
#define  BXT_MIPI_TX_ESCLK_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_TX_ESCLK_SHIFT, \
					BXT_MIPI2_TX_ESCLK_SHIFT)
#define  BXT_MIPI1_TX_ESCLK_FIXDIV_MASK		(0x3F << 26)
#define  BXT_MIPI2_TX_ESCLK_FIXDIV_MASK		(0x3F << 10)
#define  BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_TX_ESCLK_FIXDIV_MASK, \
					BXT_MIPI2_TX_ESCLK_FIXDIV_MASK)
#define  BXT_MIPI_TX_ESCLK_DIVIDER(port, val)	\
		(((val) & 0x3F) << BXT_MIPI_TX_ESCLK_SHIFT(port))
/* RX upper control divider to select actual RX clock output from 8x */
#define  BXT_MIPI1_RX_ESCLK_UPPER_SHIFT		21
#define  BXT_MIPI2_RX_ESCLK_UPPER_SHIFT		5
#define  BXT_MIPI_RX_ESCLK_UPPER_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_UPPER_SHIFT, \
					BXT_MIPI2_RX_ESCLK_UPPER_SHIFT)
#define  BXT_MIPI1_RX_ESCLK_UPPER_FIXDIV_MASK		(3 << 21)
#define  BXT_MIPI2_RX_ESCLK_UPPER_FIXDIV_MASK		(3 << 5)
#define  BXT_MIPI_RX_ESCLK_UPPER_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_UPPER_FIXDIV_MASK, \
					BXT_MIPI2_RX_ESCLK_UPPER_FIXDIV_MASK)
#define  BXT_MIPI_RX_ESCLK_UPPER_DIVIDER(port, val)	\
		(((val) & 3) << BXT_MIPI_RX_ESCLK_UPPER_SHIFT(port))
/* 8/3X divider to select the actual 8/3X clock output from 8x */
#define  BXT_MIPI1_8X_BY3_SHIFT                19
#define  BXT_MIPI2_8X_BY3_SHIFT                3
#define  BXT_MIPI_8X_BY3_SHIFT(port)          \
			_MIPI_PORT(port, BXT_MIPI1_8X_BY3_SHIFT, \
					BXT_MIPI2_8X_BY3_SHIFT)
#define  BXT_MIPI1_8X_BY3_DIVIDER_MASK         (3 << 19)
#define  BXT_MIPI2_8X_BY3_DIVIDER_MASK         (3 << 3)
#define  BXT_MIPI_8X_BY3_DIVIDER_MASK(port)    \
			_MIPI_PORT(port, BXT_MIPI1_8X_BY3_DIVIDER_MASK, \
						BXT_MIPI2_8X_BY3_DIVIDER_MASK)
#define  BXT_MIPI_8X_BY3_DIVIDER(port, val)    \
			(((val) & 3) << BXT_MIPI_8X_BY3_SHIFT(port))
/* RX lower control divider to select actual RX clock output from 8x */
#define  BXT_MIPI1_RX_ESCLK_LOWER_SHIFT		16
#define  BXT_MIPI2_RX_ESCLK_LOWER_SHIFT		0
#define  BXT_MIPI_RX_ESCLK_LOWER_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_LOWER_SHIFT, \
					BXT_MIPI2_RX_ESCLK_LOWER_SHIFT)
#define  BXT_MIPI1_RX_ESCLK_LOWER_FIXDIV_MASK		(3 << 16)
#define  BXT_MIPI2_RX_ESCLK_LOWER_FIXDIV_MASK		(3 << 0)
#define  BXT_MIPI_RX_ESCLK_LOWER_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_LOWER_FIXDIV_MASK, \
					BXT_MIPI2_RX_ESCLK_LOWER_FIXDIV_MASK)
#define  BXT_MIPI_RX_ESCLK_LOWER_DIVIDER(port, val)	\
		(((val) & 3) << BXT_MIPI_RX_ESCLK_LOWER_SHIFT(port))

#define RX_DIVIDER_BIT_1_2                     0x3
#define RX_DIVIDER_BIT_3_4                     0xC

#define BXT_DSI_PLL_CTL			_MMIO(0x161000)
#define  BXT_DSI_PLL_PVD_RATIO_SHIFT	16
#define  BXT_DSI_PLL_PVD_RATIO_MASK	(3 << BXT_DSI_PLL_PVD_RATIO_SHIFT)
#define  BXT_DSI_PLL_PVD_RATIO_1	(1 << BXT_DSI_PLL_PVD_RATIO_SHIFT)
#define  BXT_DSIC_16X_BY1		(0 << 10)
#define  BXT_DSIC_16X_BY2		(1 << 10)
#define  BXT_DSIC_16X_BY3		(2 << 10)
#define  BXT_DSIC_16X_BY4		(3 << 10)
#define  BXT_DSIC_16X_MASK		(3 << 10)
#define  BXT_DSIA_16X_BY1		(0 << 8)
#define  BXT_DSIA_16X_BY2		(1 << 8)
#define  BXT_DSIA_16X_BY3		(2 << 8)
#define  BXT_DSIA_16X_BY4		(3 << 8)
#define  BXT_DSIA_16X_MASK		(3 << 8)
#define  BXT_DSI_FREQ_SEL_SHIFT		8
#define  BXT_DSI_FREQ_SEL_MASK		(0xF << BXT_DSI_FREQ_SEL_SHIFT)

#define BXT_DSI_PLL_RATIO_MAX		0x7D
#define BXT_DSI_PLL_RATIO_MIN		0x22
#define GLK_DSI_PLL_RATIO_MAX		0x6F
#define GLK_DSI_PLL_RATIO_MIN		0x22
#define BXT_DSI_PLL_RATIO_MASK		0xFF
#define BXT_REF_CLOCK_KHZ		19200

#define BXT_DSI_PLL_ENABLE		_MMIO(0x46080)
#define  BXT_DSI_PLL_DO_ENABLE		(1 << 31)
#define  BXT_DSI_PLL_LOCKED		(1 << 30)

#endif /* __VLV_DSI_PLL_REGS_H__ */
