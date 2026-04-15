/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on vs_dc_hw.h, which is:
 *   Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DC_TOP_H_
#define _VS_DC_TOP_H_

#include <linux/bits.h>

#define VSDC_TOP_RST				0x0000

#define VSDC_TOP_IRQ_ACK			0x0010
#define VSDC_TOP_IRQ_VSYNC(n)			BIT(n)

#define VSDC_TOP_IRQ_EN				0x0014

#define VSDC_TOP_CHIP_MODEL			0x0020

#define VSDC_TOP_CHIP_REV			0x0024

#define VSDC_TOP_CHIP_CUSTOMER_ID		0x0030

#endif /* _VS_DC_TOP_H_ */
