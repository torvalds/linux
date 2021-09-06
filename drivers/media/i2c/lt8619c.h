/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 */

#ifndef _LT8619C_H
#define _LT8619C_H

/* --------------- configuration -------------------- */
#define CLK_SRC			XTAL_CLK
#define REF_RESISTANCE		EXT_RESISTANCE
#define CP_CONVERT_MODE		HDPC
#define YUV_COLORDEPTH		OUTPUT_16BIT_LOW
#define BT_TX_SYNC_POL		BT_TX_SYNC_POSITIVE

/* -------------------------------------------------- */
#define LT8619C_CHIPID		0x1604B0

#define EDID_NUM_BLOCKS_MAX	2
#define EDID_BLOCK_SIZE		128
#define POLL_INTERVAL_MS	1000
#define lt8619c_PIXEL_RATE	400000000

#define BANK_REG		0xff
#define BANK_60			0x60
#define BANK_80			0x80
#define CHIPID_REG_H		0x00
#define CHIPID_REG_M		0x01
#define CHIPID_REG_L		0x02
#define LT8619C_MAX_REGISTER	0xff

#define WAIT_MAX_TIMES		10

#define BT656_OUTPUT		0x04
#define BT1120_OUTPUT		0x03
#define BT1120_8BIT_OUTPUT	0x05

#define BT_TX_SYNC_POSITIVE	0x30
#define BT_TX_SYNC_NEGATIVE	0x00

#define PROGRESSIVE_INDICATOR	0x00
#define INTERLACE_INDICATOR	0x08

/* 0x08: Use xtal clk; 0x18: Use internal clk */
#define XTAL_CLK		0x08
#define INT_CLK			0x18

 /* internal resistance */
#define INT_RESISTANCE		0x88
/* external resistance(Pin 16 - REXT, 2K resistance) */
#define EXT_RESISTANCE		0x80

#define CLK_SDRMODE		0
 /* CLK divided by 2 */
#define CLK_DDRMODE		1

#define SDTV			0x00
#define SDPC			0x10
#define HDTV			0x20
#define HDPC			0x30

/*
 * enable
 * D0 ~ D7  Y ; D8 ~ D15  C
 * D8 ~ D15 Y ; D16 ~ D23 C
 */
#define YC_SWAP_EN		0x08
/*
 * disable
 * D0 ~ D7  C ; D8 ~ D15  Y
 * D8 ~ D15 C ; D16 ~ D23 Y
 */
#define YC_SWAP_DIS		0x00

/*
 * BT1120 24bit / BT656 12bit
 * when YC_SWAP_EN:
 * BT656 12bit D0 ~ D11
 * BT1120 24bit : D0 ~ D11 Y ; D12 ~ D23 C
 * when YC_SWAP_DIS:
 * BT656 12bit D12 ~ D23
 * BT1120 24bit : D0 ~ D11 C ; D12 ~ D23 Y
 */
#define OUTPUT_24BIT		0x00

/*
 * BT1120 20bit / BT656 10bit
 * when YC_SWAP_EN:
 * BT656 10bit D4 ~ D13
 * BT1120 20bit : D4 ~ D13 Y ; D14 ~ D23 C
 * when YC_SWAP_DIS:
 * BT656 10bit D14 ~ D23
 * BT1120 20bit : D4 ~ D13 C ; D14 ~ D23 Y
 */
#define OUTPUT_20BIT_HIGH	0x04
/*
 * when YC_SWAP_EN:
 * BT656 10bit D0 ~ D9
 * BT1120 20bit : D0 ~ D9 Y ; D10 ~ D19 C
 * when YC_SWAP_DIS:
 * BT656 10bit D10 ~ D19
 * BT1120 20bit : D0 ~ D9 C ; D10 ~ D19 Y
 */
#define OUTPUT_20BIT_LOW	0x05

/*
 * BT1120 16bit / BT656 8bit
 * when YC_SWAP_EN:
 * BT656 8bit D8 ~ D15
 * BT1120 16bit : D8 ~ D15 Y ; D16 ~ D23 C
 * when YC_SWAP_DIS:
 * BT656 8bit D16 ~ D23
 * BT1120 16bit : D8 ~ D15 C ; D16 ~ D23 Y
 */
#define OUTPUT_16BIT_HIGH	0x06
/*
 * when YC_SWAP_EN:
 * BT656 8bit D0 ~ D7
 * BT1120 16bit : D0 ~ D7 Y ; D8 ~ D15 C
 * when YC_SWAP_DIS:
 * BT656 8bit D8 ~ D15
 * BT1120 16bit : D0 ~ D7 C ; D8 ~ D15 Y
 */
#define OUTPUT_16BIT_LOW	0x07

/* ---------------- regs ----------------- */
 /* reg: 0x60_60 */
#define SYNC_POL_MASK		GENMASK(5, 4)
#define IP_SEL_MASK		GENMASK(3, 3)
#define OUTPUT_MODE_MASK	GENMASK(2, 0)

 /* reg: 0x80_05 */
#define RGD_HS_POL_ADJ_MASK	GENMASK(5, 5)
#define RGD_VS_POL_ADJ_MASK	GENMASK(4, 4)

 /* reg: 0x80_17 */
#define RGOD_VID_HSPOL		BIT(7)
#define RGOD_VID_VSPOL		BIT(6)

#endif
