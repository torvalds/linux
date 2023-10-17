/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic A1 Peripherals Clock Controller internals
 *
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#ifndef __A1_PERIPHERALS_H
#define __A1_PERIPHERALS_H

/* peripherals clock controller register offset */
#define SYS_OSCIN_CTRL		0x0
#define RTC_BY_OSCIN_CTRL0	0x4
#define RTC_BY_OSCIN_CTRL1	0x8
#define RTC_CTRL		0xc
#define SYS_CLK_CTRL0		0x10
#define SYS_CLK_EN0		0x1c
#define SYS_CLK_EN1		0x20
#define AXI_CLK_EN		0x24
#define DSPA_CLK_EN		0x28
#define DSPB_CLK_EN		0x2c
#define DSPA_CLK_CTRL0		0x30
#define DSPB_CLK_CTRL0		0x34
#define CLK12_24_CTRL		0x38
#define GEN_CLK_CTRL		0x3c
#define SAR_ADC_CLK_CTRL	0xc0
#define PWM_CLK_AB_CTRL		0xc4
#define PWM_CLK_CD_CTRL		0xc8
#define PWM_CLK_EF_CTRL		0xcc
#define SPICC_CLK_CTRL		0xd0
#define TS_CLK_CTRL		0xd4
#define SPIFC_CLK_CTRL		0xd8
#define USB_BUSCLK_CTRL		0xdc
#define SD_EMMC_CLK_CTRL	0xe0
#define CECA_CLK_CTRL0		0xe4
#define CECA_CLK_CTRL1		0xe8
#define CECB_CLK_CTRL0		0xec
#define CECB_CLK_CTRL1		0xf0
#define PSRAM_CLK_CTRL		0xf4
#define DMC_CLK_CTRL		0xf8

#endif /* __A1_PERIPHERALS_H */
