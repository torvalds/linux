/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRM_TEGRA_DSI_H
#define DRM_TEGRA_DSI_H

#define DSI_INCR_SYNCPT			0x00
#define DSI_INCR_SYNCPT_CONTROL		0x01
#define DSI_INCR_SYNCPT_ERROR		0x02
#define DSI_CTXSW			0x08
#define DSI_RD_DATA			0x09
#define DSI_WR_DATA			0x0a
#define DSI_POWER_CONTROL		0x0b
#define DSI_POWER_CONTROL_ENABLE	(1 << 0)
#define DSI_INT_ENABLE			0x0c
#define DSI_INT_STATUS			0x0d
#define DSI_INT_MASK			0x0e
#define DSI_HOST_CONTROL		0x0f
#define DSI_HOST_CONTROL_RAW		(1 << 6)
#define DSI_HOST_CONTROL_HS		(1 << 5)
#define DSI_HOST_CONTROL_BTA		(1 << 2)
#define DSI_HOST_CONTROL_CS		(1 << 1)
#define DSI_HOST_CONTROL_ECC		(1 << 0)
#define DSI_CONTROL			0x10
#define DSI_CONTROL_HS_CLK_CTRL		(1 << 20)
#define DSI_CONTROL_CHANNEL(c)		(((c) & 0x3) << 16)
#define DSI_CONTROL_FORMAT(f)		(((f) & 0x3) << 12)
#define DSI_CONTROL_TX_TRIG(x)		(((x) & 0x3) <<  8)
#define DSI_CONTROL_LANES(n)		(((n) & 0x3) <<  4)
#define DSI_CONTROL_DCS_ENABLE		(1 << 3)
#define DSI_CONTROL_SOURCE(s)		(((s) & 0x1) <<  2)
#define DSI_CONTROL_VIDEO_ENABLE	(1 << 1)
#define DSI_CONTROL_HOST_ENABLE		(1 << 0)
#define DSI_SOL_DELAY			0x11
#define DSI_MAX_THRESHOLD		0x12
#define DSI_TRIGGER			0x13
#define DSI_TX_CRC			0x14
#define DSI_STATUS			0x15
#define DSI_STATUS_IDLE			(1 << 10)
#define DSI_INIT_SEQ_CONTROL		0x1a
#define DSI_INIT_SEQ_DATA_0		0x1b
#define DSI_INIT_SEQ_DATA_1		0x1c
#define DSI_INIT_SEQ_DATA_2		0x1d
#define DSI_INIT_SEQ_DATA_3		0x1e
#define DSI_INIT_SEQ_DATA_4		0x1f
#define DSI_INIT_SEQ_DATA_5		0x20
#define DSI_INIT_SEQ_DATA_6		0x21
#define DSI_INIT_SEQ_DATA_7		0x22
#define DSI_PKT_SEQ_0_LO		0x23
#define DSI_PKT_SEQ_0_HI		0x24
#define DSI_PKT_SEQ_1_LO		0x25
#define DSI_PKT_SEQ_1_HI		0x26
#define DSI_PKT_SEQ_2_LO		0x27
#define DSI_PKT_SEQ_2_HI		0x28
#define DSI_PKT_SEQ_3_LO		0x29
#define DSI_PKT_SEQ_3_HI		0x2a
#define DSI_PKT_SEQ_4_LO		0x2b
#define DSI_PKT_SEQ_4_HI		0x2c
#define DSI_PKT_SEQ_5_LO		0x2d
#define DSI_PKT_SEQ_5_HI		0x2e
#define DSI_DCS_CMDS			0x33
#define DSI_PKT_LEN_0_1			0x34
#define DSI_PKT_LEN_2_3			0x35
#define DSI_PKT_LEN_4_5			0x36
#define DSI_PKT_LEN_6_7			0x37
#define DSI_PHY_TIMING_0		0x3c
#define DSI_PHY_TIMING_1		0x3d
#define DSI_PHY_TIMING_2		0x3e
#define DSI_BTA_TIMING			0x3f

#define DSI_TIMING_FIELD(value, period, hwinc) \
	((DIV_ROUND_CLOSEST(value, period) - (hwinc)) & 0xff)

#define DSI_TIMEOUT_0			0x44
#define DSI_TIMEOUT_LRX(x)		(((x) & 0xffff) << 16)
#define DSI_TIMEOUT_HTX(x)		(((x) & 0xffff) <<  0)
#define DSI_TIMEOUT_1			0x45
#define DSI_TIMEOUT_PR(x)		(((x) & 0xffff) << 16)
#define DSI_TIMEOUT_TA(x)		(((x) & 0xffff) <<  0)
#define DSI_TO_TALLY			0x46
#define DSI_TALLY_TA(x)			(((x) & 0xff) << 16)
#define DSI_TALLY_LRX(x)		(((x) & 0xff) <<  8)
#define DSI_TALLY_HTX(x)		(((x) & 0xff) <<  0)
#define DSI_PAD_CONTROL_0		0x4b
#define DSI_PAD_CONTROL_VS1_PDIO(x)	(((x) & 0xf) <<  0)
#define DSI_PAD_CONTROL_VS1_PDIO_CLK	(1 <<  8)
#define DSI_PAD_CONTROL_VS1_PULLDN(x)	(((x) & 0xf) << 16)
#define DSI_PAD_CONTROL_VS1_PULLDN_CLK	(1 << 24)
#define DSI_PAD_CONTROL_CD		0x4c
#define DSI_PAD_CD_STATUS		0x4d
#define DSI_VIDEO_MODE_CONTROL		0x4e
#define DSI_PAD_CONTROL_1		0x4f
#define DSI_PAD_CONTROL_2		0x50
#define DSI_PAD_OUT_CLK(x)		(((x) & 0x7) <<  0)
#define DSI_PAD_LP_DN(x)		(((x) & 0x7) <<  4)
#define DSI_PAD_LP_UP(x)		(((x) & 0x7) <<  8)
#define DSI_PAD_SLEW_DN(x)		(((x) & 0x7) << 12)
#define DSI_PAD_SLEW_UP(x)		(((x) & 0x7) << 16)
#define DSI_PAD_CONTROL_3		0x51
#define DSI_PAD_CONTROL_4		0x52
#define DSI_GANGED_MODE_CONTROL		0x53
#define DSI_GANGED_MODE_START		0x54
#define DSI_GANGED_MODE_SIZE		0x55
#define DSI_RAW_DATA_BYTE_COUNT		0x56
#define DSI_ULTRA_LOW_POWER_CONTROL	0x57
#define DSI_INIT_SEQ_DATA_8		0x58
#define DSI_INIT_SEQ_DATA_9		0x59
#define DSI_INIT_SEQ_DATA_10		0x5a
#define DSI_INIT_SEQ_DATA_11		0x5b
#define DSI_INIT_SEQ_DATA_12		0x5c
#define DSI_INIT_SEQ_DATA_13		0x5d
#define DSI_INIT_SEQ_DATA_14		0x5e
#define DSI_INIT_SEQ_DATA_15		0x5f

/*
 * pixel format as used in the DSI_CONTROL_FORMAT field
 */
enum tegra_dsi_format {
	TEGRA_DSI_FORMAT_16P,
	TEGRA_DSI_FORMAT_18NP,
	TEGRA_DSI_FORMAT_18P,
	TEGRA_DSI_FORMAT_24P,
};

#endif
