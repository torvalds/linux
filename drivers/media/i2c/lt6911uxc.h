/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Dingxian Wen <shawn.wen@rock-chips.com>
 */

#ifndef _LT6911UXC_H_
#define _LT6911UXC_H_

#define LT6911UXC_FW_VERSION	0x2005
#define LT6911UXC_CHIPID	0x0417

#define I2C_ENABLE		0x1
#define I2C_DISABLE		0x0

#define AD_LMTX_WRITE_CLK	0x1b
#define RECEIVED_INT		1

// -------------- regs ---------------
#define I2C_EN_REG		0x80EE

#define CHIPID_H		0x8101
#define CHIPID_L		0x8100
#define FW_VER_A		0x86a7
#define FW_VER_B		0x86a8
#define FW_VER_C		0x86a9
#define FW_VER_D		0x86aa

#define HTOTAL_H		0x867c
#define HTOTAL_L		0x867d
#define HACT_H			0x8680
#define HACT_L			0x8681
#define VTOTAL_H		0x867a
#define VTOTAL_L		0x867b
#define VACT_H			0x867e
#define VACT_L			0x867f

#define HFP_H			0x8678
#define HFP_L			0x8679
#define HS_H			0x8672
#define HS_L			0x8673
#define HBP_H			0x8676
#define HBP_L			0x8677
#define VBP			0x8674
#define VFP			0x8675
#define VS			0x8671

#define HDMI_VERSION		0xb0a2
#define TMDS_CLK_H		0x8750
#define TMDS_CLK_M		0x8751
#define TMDS_CLK_L		0x8752

#define MIPI_LANES		0x86a2

#define FM1_DET_CLK_SRC_SEL	0x8540
#define FREQ_METER_H		0x8548
#define FREQ_METER_M		0x8549
#define FREQ_METER_L		0x854a

#define INT_COMPARE_REG		0x86a6
#define INT_STATUS_86A3		0x86a3
#define INT_STATUS_86A5		0x86a5
#define AUDIO_IN_STATUS		0xb081
#define AUDIO_SAMPLE_RATAE_H	0xb0aa
#define AUDIO_SAMPLE_RATAE_L	0xb0ab

#endif
