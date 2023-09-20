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

/* include the CLKIDs that have been made part of the DT binding */
#include <dt-bindings/clock/amlogic,a1-peripherals-clkc.h>

/*
 * CLKID index values for internal clocks
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/a1-peripherals-clkc.h.
 * Only the clocks ids we don't want to expose, such as the internal muxes and
 * dividers of composite clocks, will remain defined here.
 */
#define CLKID_XTAL_IN		0
#define CLKID_DSPA_SEL		61
#define CLKID_DSPB_SEL		62
#define CLKID_SARADC_SEL	74
#define CLKID_SYS_A_SEL		89
#define CLKID_SYS_A_DIV		90
#define CLKID_SYS_A		91
#define CLKID_SYS_B_SEL		92
#define CLKID_SYS_B_DIV		93
#define CLKID_SYS_B		94
#define CLKID_DSPA_A_DIV	96
#define CLKID_DSPA_A		97
#define CLKID_DSPA_B_DIV	99
#define CLKID_DSPA_B		100
#define CLKID_DSPB_A_DIV	102
#define CLKID_DSPB_A		103
#define CLKID_DSPB_B_DIV	105
#define CLKID_DSPB_B		106
#define CLKID_RTC_32K_IN	107
#define CLKID_RTC_32K_DIV	108
#define CLKID_RTC_32K_XTAL	109
#define CLKID_RTC_32K_SEL	110
#define CLKID_CECB_32K_IN	111
#define CLKID_CECB_32K_DIV	112
#define CLKID_CECA_32K_IN	115
#define CLKID_CECA_32K_DIV	116
#define CLKID_DIV2_PRE		119
#define CLKID_24M_DIV2		120
#define CLKID_GEN_DIV		122
#define CLKID_SARADC_DIV	123
#define CLKID_PWM_A_DIV		125
#define CLKID_PWM_B_DIV		127
#define CLKID_PWM_C_DIV		129
#define CLKID_PWM_D_DIV		131
#define CLKID_PWM_E_DIV		133
#define CLKID_PWM_F_DIV		135
#define CLKID_SPICC_SEL		136
#define CLKID_SPICC_DIV		137
#define CLKID_SPICC_SEL2	138
#define CLKID_TS_DIV		139
#define CLKID_SPIFC_SEL		140
#define CLKID_SPIFC_DIV		141
#define CLKID_SPIFC_SEL2	142
#define CLKID_USB_BUS_SEL	143
#define CLKID_USB_BUS_DIV	144
#define CLKID_SD_EMMC_SEL	145
#define CLKID_SD_EMMC_DIV	146
#define CLKID_PSRAM_SEL		148
#define CLKID_PSRAM_DIV		149
#define CLKID_PSRAM_SEL2	150
#define CLKID_DMC_SEL		151
#define CLKID_DMC_DIV		152
#define CLKID_DMC_SEL2		153
#define NR_CLKS			154

#endif /* __A1_PERIPHERALS_H */
