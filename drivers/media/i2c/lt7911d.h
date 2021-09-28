/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#ifndef __LT7911D_H
#define __LT7911D_H

#define LT7911D_CHIPID	0x0516
#define CHIPID_REGH	0xA001
#define CHIPID_REGL	0xA000
#define I2C_EN_REG	0x80EE
#define I2C_ENABLE	0x1
#define I2C_DISABLE	0x0

#define AD_HALF_PIX_CLK         0x21
#define SOURCE_DP_RX            0x10
#define RECEIVED_INT		1

#define HTOTAL_H		0xd289
#define HTOTAL_L		0xd28a
#define HACT_H			0xd28b
#define HACT_L			0xd28c
#define HFP_H			0xd29c
#define HFP_L			0xd29d
#define HS_H			0xd294
#define HS_L			0xd295
#define HBP_H			0xd298
#define HBP_L			0xd299

#define VTOTAL_H		0xd29e
#define VTOTAL_L		0xd29f
#define VACT_H			0xd296
#define VACT_L			0xd297
#define VBP			0xd287
#define VFP			0xd288
#define VS			0xd286

#define FM_CLK_SEL              0xa034
#define FREQ_METER_H		0xb8b1
#define FREQ_METER_M		0xb8b2
#define FREQ_METER_L		0xb8b3
#define RG_MK_PRESET_SEL        0xd283

#define STREAM_CTL		0x900a
#define ENABLE_STREAM		0xbf
#define DISABLE_STREAM		0xbe

#endif /* __LT7911D_H */
