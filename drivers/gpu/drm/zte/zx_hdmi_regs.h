/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 */

#ifndef __ZX_HDMI_REGS_H__
#define __ZX_HDMI_REGS_H__

#define FUNC_SEL			0x000b
#define FUNC_HDMI_EN			BIT(0)
#define CLKPWD				0x000d
#define CLKPWD_PDIDCK			BIT(2)
#define P2T_CTRL			0x0066
#define P2T_DC_PKT_EN			BIT(7)
#define L1_INTR_STAT			0x007e
#define L1_INTR_STAT_INTR1		BIT(0)
#define INTR1_STAT			0x008f
#define INTR1_MASK			0x0095
#define INTR1_MONITOR_DETECT		(BIT(5) | BIT(6))
#define ZX_DDC_ADDR			0x00ed
#define ZX_DDC_SEGM			0x00ee
#define ZX_DDC_OFFSET			0x00ef
#define ZX_DDC_DIN_CNT1			0x00f0
#define ZX_DDC_DIN_CNT2			0x00f1
#define ZX_DDC_CMD			0x00f3
#define DDC_CMD_MASK			0xf
#define DDC_CMD_CLEAR_FIFO		0x9
#define DDC_CMD_SEQUENTIAL_READ		0x2
#define ZX_DDC_DATA			0x00f4
#define ZX_DDC_DOUT_CNT			0x00f5
#define DDC_DOUT_CNT_MASK		0x1f
#define TEST_TXCTRL			0x00f7
#define TEST_TXCTRL_HDMI_MODE		BIT(1)
#define HDMICTL4			0x0235
#define TPI_HPD_RSEN			0x063b
#define TPI_HPD_CONNECTION		(BIT(1) | BIT(2))
#define TPI_INFO_FSEL			0x06bf
#define FSEL_AVI			0
#define FSEL_GBD			1
#define FSEL_AUDIO			2
#define FSEL_SPD			3
#define FSEL_MPEG			4
#define FSEL_VSIF			5
#define TPI_INFO_B0			0x06c0
#define TPI_INFO_EN			0x06df
#define TPI_INFO_TRANS_EN		BIT(7)
#define TPI_INFO_TRANS_RPT		BIT(6)
#define TPI_DDC_MASTER_EN		0x06f8
#define HW_DDC_MASTER			BIT(7)
#define N_SVAL1				0xa03
#define N_SVAL2				0xa04
#define N_SVAL3				0xa05
#define AUD_EN				0xa13
#define AUD_IN_EN			BIT(0)
#define AUD_MODE			0xa14
#define SPDIF_EN			BIT(1)
#define TPI_AUD_CONFIG			0xa62
#define SPDIF_SAMPLE_SIZE_SHIFT		6
#define SPDIF_SAMPLE_SIZE_MASK		(0x3 << SPDIF_SAMPLE_SIZE_SHIFT)
#define SPDIF_SAMPLE_SIZE_16BIT		(0x1 << SPDIF_SAMPLE_SIZE_SHIFT)
#define SPDIF_SAMPLE_SIZE_20BIT		(0x2 << SPDIF_SAMPLE_SIZE_SHIFT)
#define SPDIF_SAMPLE_SIZE_24BIT		(0x3 << SPDIF_SAMPLE_SIZE_SHIFT)
#define TPI_AUD_MUTE			BIT(4)

#endif /* __ZX_HDMI_REGS_H__ */
