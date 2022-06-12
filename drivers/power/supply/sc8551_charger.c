// SPDX-License-Identifier: GPL-2.0
/*
 * Chrager driver for Sc8551
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>

/* Register 00h */
#define SC8551_REG_00				0x00
#define SC8551_BAT_OVP_DIS_MASK			0x80
#define SC8551_BAT_OVP_DIS_SHIFT		7
#define SC8551_BAT_OVP_ENABLE			0
#define SC8551_BAT_OVP_DISABLE			1

#define SC8551_BAT_OVP_MASK			0x3F
#define SC8551_BAT_OVP_SHIFT			0
#define SC8551_BAT_OVP_BASE			3500
#define SC8551_BAT_OVP_LSB			25

/* Register 02h */
#define SC8551_REG_02				0x02
#define SC8551_BAT_OCP_DIS_MASK			0x80
#define SC8551_BAT_OCP_DIS_SHIFT		7
#define SC8551_BAT_OCP_ENABLE			0
#define SC8551_BAT_OCP_DISABLE			1

#define SC8551_BAT_OCP_MASK			0x7F
#define SC8551_BAT_OCP_SHIFT			0
#define SC8551_BAT_OCP_BASE			2000
#define SC8551_BAT_OCP_LSB			100

/* Register 05h */
#define SC8551_REG_05				0x05
#define SC8551_AC_OVP_STAT_MASK			0x80
#define SC8551_AC_OVP_STAT_SHIFT		7

#define SC8551_AC_OVP_FLAG_MASK			0x40
#define SC8551_AC_OVP_FLAG_SHIFT		6

#define SC8551_AC_OVP_MASK_MASK			0x20
#define SC8551_AC_OVP_MASK_SHIFT		5

#define SC8551_VDROP_THRESHOLD_SET_MASK		0x10
#define SC8551_VDROP_THRESHOLD_SET_SHIFT	4
#define SC8551_VDROP_THRESHOLD_300MV		0
#define SC8551_VDROP_THRESHOLD_400MV		1

#define SC8551_VDROP_DEGLITCH_SET_MASK		0x08
#define SC8551_VDROP_DEGLITCH_SET_SHIFT		3
#define SC8551_VDROP_DEGLITCH_8US		0
#define SC8551_VDROP_DEGLITCH_5MS		1

#define SC8551_AC_OVP_MASK			0x07
#define SC8551_AC_OVP_SHIFT			0
#define SC8551_AC_OVP_BASE			11
#define SC8551_AC_OVP_LSB			1
#define SC8551_AC_OVP_6P5V			65

/* Register 06h */
#define SC8551_REG_06				0x06
#define SC8551_VBUS_PD_EN_MASK			0x80
#define SC8551_VBUS_PD_EN_SHIFT			7
#define SC8551_VBUS_PD_ENABLE			1
#define SC8551_VBUS_PD_DISABLE			0

#define SC8551_BUS_OVP_MASK			0x7F
#define SC8551_BUS_OVP_SHIFT			0
#define SC8551_BUS_OVP_BASE			6000
#define SC8551_BUS_OVP_LSB			50

/* Register 08h */
#define SC8551_REG_08				0x08
#define SC8551_BUS_OCP_DIS_MASK			0x80
#define SC8551_BUS_OCP_DIS_SHIFT		7
#define SC8551_BUS_OCP_ENABLE			0
#define SC8551_BUS_OCP_DISABLE			1

#define SC8551_IBUS_UCP_RISE_FLAG_MASK		0x40
#define SC8551_IBUS_UCP_RISE_FLAG_SHIFT		6

#define SC8551_IBUS_UCP_RISE_MASK_MASK		0x20
#define SC8551_IBUS_UCP_RISE_MASK_SHIFT		5
#define SC8551_IBUS_UCP_RISE_MASK_ENABLE	1
#define SC8551_IBUS_UCP_RISE_MASK_DISABLE	0

#define SC8551_IBUS_UCP_FALL_FLAG_MASK		0x10
#define SC8551_IBUS_UCP_FALL_FLAG_SHIFT		4

#define SC8551_BUS_OCP_MASK			0x0F
#define SC8551_BUS_OCP_SHIFT			0
#define SC8551_BUS_OCP_BASE			1000
#define SC8551_BUS_OCP_LSB			250

/* Register 0Ah */
#define SC8551_REG_0A				0x0A
#define SC8551_TSHUT_FLAG_MASK			0x80
#define SC8551_TSHUT_FLAG_SHIFT			7

#define SC8551_TSHUT_STAT_MASK			0x40
#define SC8551_TSHUT_STAT_SHIFT			6

#define SC8551_VBUS_ERRORLO_STAT_MASK		0x20
#define SC8551_VBUS_ERRORLO_STAT_SHIFT		5

#define SC8551_VBUS_ERRORHI_STAT_MASK		0x10
#define SC8551_VBUS_ERRORHI_STAT_SHIFT		4

#define SC8551_SS_TIMEOUT_FLAG_MASK		0x08
#define SC8551_SS_TIMEOUT_FLAG_SHIFT		3

#define SC8551_CONV_SWITCHING_STAT_MASK		0x04
#define SC8551_CONV_SWITCHING_STAT_SHIFT	2

#define SC8551_CONV_OCP_FLAG_MASK		0x02
#define SC8551_CONV_OCP_FLAG_SHIFT		1

#define SC8551_PIN_DIAG_FALL_FLAG_MASK		0x01
#define SC8551_PIN_DIAG_FALL_FLAG_SHIFT		0

/* Register 0Bh */
#define SC8551_REG_0B				0x0B
#define SC8551_REG_RST_MASK			0x80
#define SC8551_REG_RST_SHIFT			7
#define SC8551_REG_RST_ENABLE			1
#define SC8551_REG_RST_DISABLE			0

#define SC8551_FSW_SET_MASK			0x70
#define SC8551_FSW_SET_SHIFT			4
#define SC8551_FSW_SET_300KHZ			0
#define SC8551_FSW_SET_350KHZ			1
#define SC8551_FSW_SET_400KHZ			2
#define SC8551_FSW_SET_450KHZ			3
#define SC8551_FSW_SET_500KHZ			4
#define SC8551_FSW_SET_550KHZ			5
#define SC8551_FSW_SET_600KHZ			6
#define SC8551_FSW_SET_750KHZ			7

#define SC8551_WD_TIMEOUT_FLAG_MASK		0x08
#define SC8551_WD_TIMEOUT_SHIFT			3

#define SC8551_WATCHDOG_DIS_MASK		0x04
#define SC8551_WATCHDOG_DIS_SHIFT		2
#define SC8551_WATCHDOG_ENABLE			0
#define SC8551_WATCHDOG_DISABLE			1

#define SC8551_WATCHDOG_MASK			0x03
#define SC8551_WATCHDOG_SHIFT			0
#define SC8551_WATCHDOG_0P5S			0
#define SC8551_WATCHDOG_1S			1
#define SC8551_WATCHDOG_5S			2
#define SC8551_WATCHDOG_30S			3

/* Register 0Ch */
#define SC8551_REG_0C				0x0C
#define SC8551_CHG_EN_MASK			0x80
#define SC8551_CHG_EN_SHIFT			7
#define SC8551_CHG_ENABLE			1
#define SC8551_CHG_DISABLE			0

#define SC8551_MS_MASK				0x60
#define SC8551_MS_SHIFT				5
#define SC8551_MS_STANDALONE			0
#define SC8551_MS_SLAVE				1
#define SC8551_MS_MASTER			2
#define SC8551_ROLE_STDALONE			0
#define SC8551_ROLE_SLAVE			1
#define SC8551_ROLE_MASTER			2

#define SC8551_FREQ_SHIFT_MASK			0x18
#define SC8551_FREQ_SHIFT_SHIFT			3
#define SC8551_FREQ_SHIFT_NORMINAL		0
#define SC8551_FREQ_SHIFT_POSITIVE10		1
#define SC8551_FREQ_SHIFT_NEGATIVE10		2
#define SC8551_FREQ_SHIFT_SPREAD_SPECTRUM	3

#define SC8551_TSBUS_DIS_MASK			0x04
#define SC8551_TSBUS_DIS_SHIFT			2
#define SC8551_TSBUS_ENABLE			0
#define SC8551_TSBUS_DISABLE			1

#define SC8551_TSBAT_DIS_MASK			0x02
#define SC8551_TSBAT_DIS_SHIFT			1
#define SC8551_TSBAT_ENABLE			0
#define SC8551_TSBAT_DISABLE			1

/* Register 0Dh */
#define SC8551_REG_0D				0x0D
#define SC8551_BAT_OVP_ALM_STAT_MASK		0x80
#define SC8551_BAT_OVP_ALM_STAT_SHIFT		7

#define SC8551_BAT_OCP_ALM_STAT_MASK		0x40
#define SC8551_BAT_OCP_ALM_STAT_SHIFT		6

#define SC8551_BUS_OVP_ALM_STAT_MASK		0x20
#define SC8551_BUS_OVP_ALM_STAT_SHIFT		5

#define SC8551_BUS_OCP_ALM_STAT_MASK		0x10
#define SC8551_BUS_OCP_ALM_STAT_SHIFT		4

#define SC8551_BAT_UCP_ALM_STAT_MASK		0x08
#define SC8551_BAT_UCP_ALM_STAT_SHIFT		3

#define SC8551_ADAPTER_INSERT_STAT_MASK		0x04
#define SC8551_ADAPTER_INSERT_STAT_SHIFT	2

#define SC8551_VBAT_INSERT_STAT_MASK		0x02
#define SC8551_VBAT_INSERT_STAT_SHIFT		1

#define SC8551_ADC_DONE_STAT_MASK		0x01
#define SC8551_ADC_DONE_STAT_SHIFT		0
#define SC8551_ADC_DONE_STAT_COMPLETE		1
#define SC8551_ADC_DONE_STAT_NOTCOMPLETE	0

/* Register 0Eh */
#define SC8551_REG_0E				0x0E
#define SC8551_BAT_OVP_ALM_FLAG_MASK		0x80
#define SC8551_BAT_OVP_ALM_FLAG_SHIFT		7

#define SC8551_BAT_OCP_ALM_FLAG_MASK		0x40
#define SC8551_BAT_OCP_ALM_FLAG_SHIFT		6

#define SC8551_BUS_OVP_ALM_FLAG_MASK		0x20
#define SC8551_BUS_OVP_ALM_FLAG_SHIFT		5

#define SC8551_BUS_OCP_ALM_FLAG_MASK		0x10
#define SC8551_BUS_OCP_ALM_FLAG_SHIFT		4

#define SC8551_BAT_UCP_ALM_FLAG_MASK		0x08
#define SC8551_BAT_UCP_ALM_FLAG_SHIFT		3

#define SC8551_ADAPTER_INSERT_FLAG_MASK		0x04
#define SC8551_ADAPTER_INSERT_FLAG_SHIFT	2

#define SC8551_VBAT_INSERT_FLAG_MASK		0x02
#define SC8551_VBAT_INSERT_FLAG_SHIFT		1

#define SC8551_ADC_DONE_FLAG_MASK		0x01
#define SC8551_ADC_DONE_FLAG_SHIFT		0
#define SC8551_ADC_DONE_FLAG_COMPLETE		1
#define SC8551_ADC_DONE_FLAG_NOTCOMPLETE	0

/* Register 0Fh */
#define SC8551_REG_0F				0x0F
#define SC8551_BAT_OVP_ALM_MASK_MASK		0x80
#define SC8551_BAT_OVP_ALM_MASK_SHIFT		7
#define SC8551_BAT_OVP_ALM_MASK_ENABLE		1
#define SC8551_BAT_OVP_ALM_MASK_DISABLE		0

#define SC8551_BAT_OCP_ALM_MASK_MASK		0x40
#define SC8551_BAT_OCP_ALM_MASK_SHIFT		6
#define SC8551_BAT_OCP_ALM_MASK_ENABLE		1
#define SC8551_BAT_OCP_ALM_MASK_DISABLE		0

#define SC8551_BUS_OVP_ALM_MASK_MASK		0x20
#define SC8551_BUS_OVP_ALM_MASK_SHIFT		5
#define SC8551_BUS_OVP_ALM_MASK_ENABLE		1
#define SC8551_BUS_OVP_ALM_MASK_DISABLE		0

#define SC8551_BUS_OCP_ALM_MASK_MASK		0x10
#define SC8551_BUS_OCP_ALM_MASK_SHIFT		4
#define SC8551_BUS_OCP_ALM_MASK_ENABLE		1
#define SC8551_BUS_OCP_ALM_MASK_DISABLE		0

#define SC8551_BAT_UCP_ALM_MASK_MASK		0x08
#define SC8551_BAT_UCP_ALM_MASK_SHIFT		3
#define SC8551_BAT_UCP_ALM_MASK_ENABLE		1
#define SC8551_BAT_UCP_ALM_MASK_DISABLE		0

#define SC8551_ADAPTER_INSERT_MASK_MASK		0x04
#define SC8551_ADAPTER_INSERT_MASK_SHIFT	2
#define SC8551_ADAPTER_INSERT_MASK_ENABLE	1
#define SC8551_ADAPTER_INSERT_MASK_DISABLE	0

#define SC8551_VBAT_INSERT_MASK_MASK		0x02
#define SC8551_VBAT_INSERT_MASK_SHIFT		1
#define SC8551_VBAT_INSERT_MASK_ENABLE		1
#define SC8551_VBAT_INSERT_MASK_DISABLE		0

#define SC8551_ADC_DONE_MASK_MASK		0x01
#define SC8551_ADC_DONE_MASK_SHIFT		0
#define SC8551_ADC_DONE_MASK_ENABLE		1
#define SC8551_ADC_DONE_MASK_DISABLE		0

/* Register 10h */
#define SC8551_REG_10				0x10
#define SC8551_BAT_OVP_FLT_STAT_MASK		0x80
#define SC8551_BAT_OVP_FLT_STAT_SHIFT		7

#define SC8551_BAT_OCP_FLT_STAT_MASK		0x40
#define SC8551_BAT_OCP_FLT_STAT_SHIFT		6

#define SC8551_BUS_OVP_FLT_STAT_MASK		0x20
#define SC8551_BUS_OVP_FLT_STAT_SHIFT		5

#define SC8551_BUS_OCP_FLT_STAT_MASK		0x10
#define SC8551_BUS_OCP_FLT_STAT_SHIFT		4

#define SC8551_TSBUS_TSBAT_ALM_STAT_MASK	0x08
#define SC8551_TSBUS_TSBAT_ALM_STAT_SHIFT	3

#define SC8551_TSBAT_FLT_STAT_MASK		0x04
#define SC8551_TSBAT_FLT_STAT_SHIFT		2

#define SC8551_TSBUS_FLT_STAT_MASK		0x02
#define SC8551_TSBUS_FLT_STAT_SHIFT		1

#define SC8551_TDIE_ALM_STAT_MASK		0x01
#define SC8551_TDIE_ALM_STAT_SHIFT		0

/* Register 11h */
#define SC8551_REG_11				0x11
#define SC8551_BAT_OVP_FLT_FLAG_MASK		0x80
#define SC8551_BAT_OVP_FLT_FLAG_SHIFT		7

#define SC8551_BAT_OCP_FLT_FLAG_MASK		0x40
#define SC8551_BAT_OCP_FLT_FLAG_SHIFT		6

#define SC8551_BUS_OVP_FLT_FLAG_MASK		0x20
#define SC8551_BUS_OVP_FLT_FLAG_SHIFT		5

#define SC8551_BUS_OCP_FLT_FLAG_MASK		0x10
#define SC8551_BUS_OCP_FLT_FLAG_SHIFT		4

#define SC8551_TSBUS_TSBAT_ALM_FLAG_MASK	0x08
#define SC8551_TSBUS_TSBAT_ALM_FLAG_SHIFT	3

#define SC8551_TSBAT_FLT_FLAG_MASK		0x04
#define SC8551_TSBAT_FLT_FLAG_SHIFT		2

#define SC8551_TSBUS_FLT_FLAG_MASK		0x02
#define SC8551_TSBUS_FLT_FLAG_SHIFT		1

#define SC8551_TDIE_ALM_FLAG_MASK		0x01
#define SC8551_TDIE_ALM_FLAG_SHIFT		0

/* Register 12h */
#define SC8551_REG_12				0x12
#define SC8551_BAT_OVP_FLT_MASK_MASK		0x80
#define SC8551_BAT_OVP_FLT_MASK_SHIFT		7
#define SC8551_BAT_OVP_FLT_MASK_ENABLE		1
#define SC8551_BAT_OVP_FLT_MASK_DISABLE		0

#define SC8551_BAT_OCP_FLT_MASK_MASK		0x40
#define SC8551_BAT_OCP_FLT_MASK_SHIFT		6
#define SC8551_BAT_OCP_FLT_MASK_ENABLE		1
#define SC8551_BAT_OCP_FLT_MASK_DISABLE		0

#define SC8551_BUS_OVP_FLT_MASK_MASK		0x20
#define SC8551_BUS_OVP_FLT_MASK_SHIFT		5
#define SC8551_BUS_OVP_FLT_MASK_ENABLE		1
#define SC8551_BUS_OVP_FLT_MASK_DISABLE		0

#define SC8551_BUS_OCP_FLT_MASK_MASK		0x10
#define SC8551_BUS_OCP_FLT_MASK_SHIFT		4
#define SC8551_BUS_OCP_FLT_MASK_ENABLE		1
#define SC8551_BUS_OCP_FLT_MASK_DISABLE		0

#define SC8551_TSBUS_TSBAT_ALM_MASK_MASK	0x08
#define SC8551_TSBUS_TSBAT_ALM_MASK_SHIFT	3
#define SC8551_TSBUS_TSBAT_ALM_MASK_ENABLE	1
#define SC8551_TSBUS_TSBAT_ALM_MASK_DISABLE	0

#define SC8551_TSBAT_FLT_MASK_MASK		0x04
#define SC8551_TSBAT_FLT_MASK_SHIFT		2
#define SC8551_TSBAT_FLT_MASK_ENABLE		1
#define SC8551_TSBAT_FLT_MASK_DISABLE		0

#define SC8551_TSBUS_FLT_MASK_MASK		0x02
#define SC8551_TSBUS_FLT_MASK_SHIFT		1
#define SC8551_TSBUS_FLT_MASK_ENABLE		1
#define SC8551_TSBUS_FLT_MASK_DISABLE		0

#define SC8551_TDIE_ALM_MASK_MASK		0x01
#define SC8551_TDIE_ALM_MASK_SHIFT		0
#define SC8551_TDIE_ALM_MASK_ENABLE		1
#define SC8551_TDIE_ALM_MASK_DISABLE		0

/* Register 13h */
#define SC8551_REG_13				0x13
#define SC8551_DEV_ID_MASK			0xFF
#define SC8551_DEV_ID_SHIFT			0

/* Register 14h */
#define SC8551_REG_14				0x14
#define SC8551_ADC_EN_MASK			0x80
#define SC8551_ADC_EN_SHIFT			7
#define SC8551_ADC_ENABLE			1
#define SC8551_ADC_DISABLE			0

#define SC8551_ADC_RATE_MASK			0x40
#define SC8551_ADC_RATE_SHIFT			6
#define SC8551_ADC_RATE_CONTINOUS		0
#define SC8551_ADC_RATE_ONESHOT			1

#define SC8551_IBUS_ADC_DIS_MASK		0x01
#define SC8551_IBUS_ADC_DIS_SHIFT		0
#define SC8551_IBUS_ADC_ENABLE			0
#define SC8551_IBUS_ADC_DISABLE			1

/* Register 15h */
#define SC8551_REG_15				0x15
#define SC8551_VBUS_ADC_DIS_MASK		0x80
#define SC8551_VBUS_ADC_DIS_SHIFT		7
#define SC8551_VBUS_ADC_ENABLE			0
#define SC8551_VBUS_ADC_DISABLE			1

#define SC8551_VAC_ADC_DIS_MASK			0x40
#define SC8551_VAC_ADC_DIS_SHIFT		6
#define SC8551_VAC_ADC_ENABLE			0
#define SC8551_VAC_ADC_DISABLE			1

#define SC8551_VOUT_ADC_DIS_MASK		0x20
#define SC8551_VOUT_ADC_DIS_SHIFT		5
#define SC8551_VOUT_ADC_ENABLE			0
#define SC8551_VOUT_ADC_DISABLE			1

#define SC8551_VBAT_ADC_DIS_MASK		0x10
#define SC8551_VBAT_ADC_DIS_SHIFT		4
#define SC8551_VBAT_ADC_ENABLE			0
#define SC8551_VBAT_ADC_DISABLE			1

#define SC8551_IBAT_ADC_DIS_MASK		0x08
#define SC8551_IBAT_ADC_DIS_SHIFT		3
#define SC8551_IBAT_ADC_ENABLE			0
#define SC8551_IBAT_ADC_DISABLE			1

#define SC8551_TSBUS_ADC_DIS_MASK		0x04
#define SC8551_TSBUS_ADC_DIS_SHIFT		2
#define SC8551_TSBUS_ADC_ENABLE			0
#define SC8551_TSBUS_ADC_DISABLE		1

#define SC8551_TSBAT_ADC_DIS_MASK		0x02
#define SC8551_TSBAT_ADC_DIS_SHIFT		1
#define SC8551_TSBAT_ADC_ENABLE			0
#define SC8551_TSBAT_ADC_DISABLE		1

#define SC8551_TDIE_ADC_DIS_MASK		0x01
#define SC8551_TDIE_ADC_DIS_SHIFT		0
#define SC8551_TDIE_ADC_ENABLE			0
#define SC8551_TDIE_ADC_DISABLE			1

/* Register 16h */
#define SC8551_REG_16				0x16
#define SC8551_IBUS_POL_H_MASK			0x0F

/* Register 17h */
#define SC8551_REG_17				0x17
#define SC8551_IBUS_POL_L_MASK			0xFF

/* Register 18h */
#define SC8551_REG_18				0x18
#define SC8551_VBUS_POL_H_MASK			0x0F

/* Register 19h */
#define SC8551_REG_19				0x19
#define SC8551_VBUS_POL_L_MASK			0xFF

/* Register 1Ah */
#define SC8551_REG_1A				0x1A
#define SC8551_VAC_POL_H_MASK			0x0F

/* Register 1Bh */
#define SC8551_REG_1B				0x1B
#define SC8551_VAC_POL_L_MASK			0xFF

/* Register 1Ch */
#define SC8551_REG_1C				0x1C
#define SC8551_VOUT_POL_H_MASK			0x0F

/* Register 1Dh */
#define SC8551_REG_1D				0x1D
#define SC8551_VOUT_POL_L_MASK			0x0F

/* Register 1Eh */
#define SC8551_REG_1E				0x1E
#define SC8551_VBAT_POL_H_MASK			0x0F

/* Register 1Fh */
#define SC8551_REG_1F				0x1F
#define SC8551_VBAT_POL_L_MASK			0xFF

/* Register 20h */
#define SC8551_REG_20				0x20
#define SC8551_IBAT_POL_H_MASK			0x0F

/* Register 21h */
#define SC8551_REG_21				0x21
#define SC8551_IBAT_POL_L_MASK			0xFF

/* Register 26h */
#define SC8551_REG_26				0x26
#define SC8551_TDIE_POL_H_MASK			0x01

/* Register 2Bh */
#define SC8551_REG_2B				0x2B
#define SC8551_SS_TIMEOUT_SET_MASK		0xE0
#define SC8551_SS_TIMEOUT_SET_SHIFT		5
#define SC8551_SS_TIMEOUT_DISABLE		0
#define SC8551_SS_TIMEOUT_12P5MS		1
#define SC8551_SS_TIMEOUT_25MS			2
#define SC8551_SS_TIMEOUT_50MS			3
#define SC8551_SS_TIMEOUT_100MS			4
#define SC8551_SS_TIMEOUT_400MS			5
#define SC8551_SS_TIMEOUT_1500MS		6
#define SC8551_SS_TIMEOUT_100000MS		7

#define SC8551_EN_REGULATION_MASK		0x10
#define SC8551_EN_REGULATION_SHIFT		4
#define SC8551_EN_REGULATION_ENABLE		1
#define SC8551_EN_REGULATION_DISABLE		0

#define SC8551_VOUT_OVP_DIS_MASK		0x08
#define SC8551_VOUT_OVP_DIS_SHIFT		3
#define SC8551_VOUT_OVP_ENABLE			1
#define SC8551_VOUT_OVP_DISABLE			0

#define SC8551_IBUS_UCP_RISE_TH_MASK		0x04
#define SC8551_IBUS_UCP_RISE_TH_SHIFT		2
#define SC8551_IBUS_UCP_RISE_150MA		0
#define SC8551_IBUS_UCP_RISE_250MA		1

#define SC8551_SET_IBAT_SNS_RES_MASK		0x02
#define SC8551_SET_IBAT_SNS_RES_SHIFT		1
#define SC8551_SET_IBAT_SNS_RES_2MHM		0
#define SC8551_SET_IBAT_SNS_RES_5MHM		1

#define SC8551_VAC_PD_EN_MASK			0x01
#define SC8551_VAC_PD_EN_SHIFT			0
#define SC8551_VAC_PD_ENABLE			1
#define SC8551_VAC_PD_DISABLE			0

/* Register 2Ch */
#define SC8551_REG_2C				0x2C
#define SC8551_IBAT_REG_MASK			0xC0
#define SC8551_IBAT_REG_SHIFT			6
#define SC8551_IBAT_REG_200MA			0
#define SC8551_IBAT_REG_300MA			1
#define SC8551_IBAT_REG_400MA			2
#define SC8551_IBAT_REG_500MA			3
#define SC8551_VBAT_REG_MASK			0x30
#define SC8551_VBAT_REG_SHIFT			4
#define SC8551_VBAT_REG_50MV			0
#define SC8551_VBAT_REG_100MV			1
#define SC8551_VBAT_REG_150MV			2
#define SC8551_VBAT_REG_200MV			3

#define SC8551_VBAT_REG_ACTIVE_STAT_MASK	0x08
#define SC8551_IBAT_REG_ACTIVE_STAT_MASK	0x04
#define SC8551_VDROP_OVP_ACTIVE_STAT_MASK	0x02
#define SC8551_VOUT_OVP_ACTIVE_STAT_MASK	0x01

#define SC8551_REG_2D				0x2D
#define SC8551_VBAT_REG_ACTIVE_FLAG_MASK	0x80
#define SC8551_IBAT_REG_ACTIVE_FLAG_MASK	0x40
#define SC8551_VDROP_OVP_FLAG_MASK		0x20
#define SC8551_VOUT_OVP_FLAG_MASK		0x10
#define SC8551_VBAT_REG_ACTIVE_MASK_MASK	0x08
#define SC8551_IBAT_REG_ACTIVE_MASK_MASK	0x04
#define SC8551_VDROP_OVP_MASK_MASK		0x02
#define SC8551_VOUT_OVP_MASK_MASK		0x01

#define SC8551_REG_2E				0x2E
#define SC8551_IBUS_LOW_DG_MASK			0x08
#define SC8551_IBUS_LOW_DG_SHIFT		3
#define SC8551_IBUS_LOW_DG_10US			0
#define SC8551_IBUS_LOW_DG_5MS			1

#define SC8551_REG_2F				0x2F
#define SC8551_PMID2OUT_UVP_FLAG_MASK		0x08
#define SC8551_PMID2OUT_OVP_FLAG_MASK		0x04
#define SC8551_PMID2OUT_UVP_STAT_MASK		0x02
#define SC8551_PMID2OUT_OVP_STAT_MASK		0x01

#define SC8551_REG_30				0x30
#define SC8551_IBUS_REG_EN_MASK			0x80
#define SC8551_IBUS_REG_EN_SHIFT		7
#define SC8551_IBUS_REG_ENABLE			1
#define SC8551_IBUS_REG_DISABLE			0
#define SC8551_IBUS_REG_ACTIVE_STAT_MASK	0x40
#define SC8551_IBUS_REG_ACTIVE_FLAG_MASK	0x20
#define SC8551_IBUS_REG_ACTIVE_MASK_MASK	0x10
#define SC8551_IBUS_REG_ACTIVE_MASK_SHIFT	4
#define SC8551_IBUS_REG_ACTIVE_NOT_MASK		0
#define SC8551_IBUS_REG_ACTIVE_MASK		1
#define SC8551_IBUS_REG_MASK			0x0F
#define SC8551_IBUS_REG_SHIFT			0
#define SC8551_IBUS_REG_BASE			1000
#define SC8551_IBUS_REG_LSB			250

#define SC8551_REG_31				0x31
#define SC8551_CHARGE_MODE_MASK			0x01
#define SC8551_CHARGE_MODE_SHIFT		0
#define SC8551_CHARGE_MODE_2_1			0
#define SC8551_CHARGE_MODE_1_1			1

#define SC8551_REG_35				0x35
#define SC8551_VBUS_IN_RANGE_MASK		0x01
#define SC8551_VBUS_IN_RANGE_SHIFT		6
#define SC8551_VBUS_IN_RANGE_ENABLE		0
#define SC8551_VBUS_IN_RANGE_DISABLE		1


#define SC8551_REG_36				0x36
#define SC8551_OVPGATE_MASK			0x01
#define SC8551_OVPGATE_SHIFT			3
#define SC8551_OVPGATE_ENABLE			0
#define SC8551_OVPGATE_DISABLE			1

#define VBUS_INSERT				BIT(2)
#define VBAT_INSERT				BIT(1)
#define ADC_DONE				BIT(0)

#define BAT_OVP_FAULT				BIT(7)
#define BAT_OCP_FAULT				BIT(6)
#define BUS_OVP_FAULT				BIT(5)
#define BUS_OCP_FAULT				BIT(4)

/*below used for comm with other module*/
#define BAT_OVP_FAULT_SHIFT			0
#define BAT_OCP_FAULT_SHIFT			1
#define BUS_OVP_FAULT_SHIFT			2
#define BUS_OCP_FAULT_SHIFT			3
#define BAT_THERM_FAULT_SHIFT			4
#define BUS_THERM_FAULT_SHIFT			5
#define DIE_THERM_FAULT_SHIFT			6

#define BAT_OVP_FAULT_MASK			(1 << BAT_OVP_FAULT_SHIFT)
#define BAT_OCP_FAULT_MASK			(1 << BAT_OCP_FAULT_SHIFT)
#define BUS_OVP_FAULT_MASK			(1 << BUS_OVP_FAULT_SHIFT)
#define BUS_OCP_FAULT_MASK			(1 << BUS_OCP_FAULT_SHIFT)
#define BAT_THERM_FAULT_MASK			(1 << BAT_THERM_FAULT_SHIFT)
#define BUS_THERM_FAULT_MASK			(1 << BUS_THERM_FAULT_SHIFT)
#define DIE_THERM_FAULT_MASK			(1 << DIE_THERM_FAULT_SHIFT)

#define BAT_OVP_ALARM_SHIFT			0
#define BAT_OCP_ALARM_SHIFT			1
#define BUS_OVP_ALARM_SHIFT			2
#define BUS_OCP_ALARM_SHIFT			3
#define BAT_THERM_ALARM_SHIFT			4
#define BUS_THERM_ALARM_SHIFT			5
#define DIE_THERM_ALARM_SHIFT			6
#define BAT_UCP_ALARM_SHIFT			7
#define BAT_OVP_ALARM_MASK			(1 << BAT_OVP_ALARM_SHIFT)
#define BAT_OCP_ALARM_MASK			(1 << BAT_OCP_ALARM_SHIFT)
#define BUS_OVP_ALARM_MASK			(1 << BUS_OVP_ALARM_SHIFT)
#define BUS_OCP_ALARM_MASK			(1 << BUS_OCP_ALARM_SHIFT)
#define BAT_THERM_ALARM_MASK			(1 << BAT_THERM_ALARM_SHIFT)
#define BUS_THERM_ALARM_MASK			(1 << BUS_THERM_ALARM_SHIFT)
#define DIE_THERM_ALARM_MASK			(1 << DIE_THERM_ALARM_SHIFT)
#define BAT_UCP_ALARM_MASK			(1 << BAT_UCP_ALARM_SHIFT)

#define VBAT_REG_STATUS_SHIFT			0
#define IBAT_REG_STATUS_SHIFT			1

#define VBAT_REG_STATUS_MASK			(1 << VBAT_REG_STATUS_SHIFT)
#define IBAT_REG_STATUS_MASK			(1 << VBAT_REG_STATUS_SHIFT)

#define SC8551_DEBUG_BUF_LEN			30

enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VAC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TBUS,
	ADC_TBAT,
	ADC_TDIE,
	ADC_MAX_NUM,
};

struct sc8551_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;

	int bat_ovp_th;
	int bat_ovp_alm_th;
	int bat_ocp_th;

	bool bus_ocp_disable;

	int bus_ovp_th;
	int bus_ocp_th;

	int ac_ovp_th;

	bool bat_therm_disable;
	bool bus_therm_disable;
	bool die_therm_disable;

	int sense_r_mohm;
};

struct sc8551 {
	struct device *dev;
	struct i2c_client *client;

	int part_no;
	int revision;

	int mode;

	struct mutex data_lock;
	struct mutex i2c_rw_lock;

	int irq;

	bool batt_present;
	bool vbus_present;

	bool usb_present;
	bool charge_enabled;

	int vbus_error;

	/* ADC reading */
	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vac_volt;

	int ibat_curr;
	int ibus_curr;

	int die_temp;

	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;

	bool vbat_reg;
	bool ibat_reg;

	int prev_alarm;
	int prev_fault;

	struct sc8551_cfg *cfg;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *fc2_psy;
};

static int __sc8551_read_byte(struct sc8551 *sc, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(sc->client, reg);
	if (ret < 0) {
		dev_err(sc->dev, "i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sc8551_write_byte(struct sc8551 *sc, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(sc->client, reg, val);
	if (ret < 0) {
		dev_err(sc->dev, "i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int sc8551_read_byte(struct sc8551 *sc, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_read_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8551_write_byte(struct sc8551 *sc, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_write_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8551_update_bits(struct sc8551 *sc,
			      u8 reg,
			      u8 mask,
			      u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_read_byte(sc, reg, &tmp);
	if (ret) {
		dev_err(sc->dev, "Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8551_write_byte(sc, reg, tmp);
	if (ret)
		dev_err(sc->dev, "Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

static int sc8551_enable_charge(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_CHG_ENABLE;
	else
		val = SC8551_CHG_DISABLE;

	val <<= SC8551_CHG_EN_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_0C,
				 SC8551_CHG_EN_MASK,
				 val);

	return ret;
}

static int sc8551_check_charge_enabled(struct sc8551 *sc, bool *enabled)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0C, &val);

	if (!ret)
		*enabled = !!(val & SC8551_CHG_EN_MASK);
	return ret;
}

static int sc8551_enable_wdt(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_WATCHDOG_ENABLE;
	else
		val = SC8551_WATCHDOG_DISABLE;

	val <<= SC8551_WATCHDOG_DIS_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_0B,
				 SC8551_WATCHDOG_DIS_MASK,
				 val);
	return ret;
}

static int sc8551_set_wdt(struct sc8551 *sc, int ms)
{
	int ret;
	u8 val;

	if (ms == 500)
		val = SC8551_WATCHDOG_0P5S;
	else if (ms == 1000)
		val = SC8551_WATCHDOG_1S;
	else if (ms == 5000)
		val = SC8551_WATCHDOG_5S;
	else if (ms == 30000)
		val = SC8551_WATCHDOG_30S;
	else
		val = SC8551_WATCHDOG_30S;

	val <<= SC8551_WATCHDOG_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_0B,
				 SC8551_WATCHDOG_MASK,
				 val);
	return ret;
}

static int sc8551_enable_batovp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BAT_OVP_ENABLE;
	else
		val = SC8551_BAT_OVP_DISABLE;

	val <<= SC8551_BAT_OVP_DIS_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_00,
				 SC8551_BAT_OVP_DIS_MASK,
				 val);
	return ret;
}

static int sc8551_set_batovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BAT_OVP_BASE)
		threshold = SC8551_BAT_OVP_BASE;

	val = (threshold - SC8551_BAT_OVP_BASE) / SC8551_BAT_OVP_LSB;

	val <<= SC8551_BAT_OVP_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_00,
				 SC8551_BAT_OVP_MASK,
				 val);
	return ret;
}

static int sc8551_enable_batocp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BAT_OCP_ENABLE;
	else
		val = SC8551_BAT_OCP_DISABLE;

	val <<= SC8551_BAT_OCP_DIS_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_02,
				 SC8551_BAT_OCP_DIS_MASK,
				 val);
	return ret;
}

static int sc8551_set_batocp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BAT_OCP_BASE)
		threshold = SC8551_BAT_OCP_BASE;

	val = (threshold - SC8551_BAT_OCP_BASE) / SC8551_BAT_OCP_LSB;

	val <<= SC8551_BAT_OCP_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_02,
				 SC8551_BAT_OCP_MASK,
				 val);
	return ret;
}

static int sc8551_set_busovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BUS_OVP_BASE)
		threshold = SC8551_BUS_OVP_BASE;

	val = (threshold - SC8551_BUS_OVP_BASE) / SC8551_BUS_OVP_LSB;

	val <<= SC8551_BUS_OVP_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_06,
				 SC8551_BUS_OVP_MASK,
				 val);
	return ret;
}

static int sc8551_enable_busocp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BUS_OCP_ENABLE;
	else
		val = SC8551_BUS_OCP_DISABLE;

	val <<= SC8551_BUS_OCP_DIS_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_08,
				 SC8551_BUS_OCP_DIS_MASK,
				 val);
	return ret;
}

static int sc8551_set_busocp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BUS_OCP_BASE)
		threshold = SC8551_BUS_OCP_BASE;

	val = (threshold - SC8551_BUS_OCP_BASE) / SC8551_BUS_OCP_LSB;

	val <<= SC8551_BUS_OCP_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_08,
				 SC8551_BUS_OCP_MASK,
				 val);
	return ret;
}

static int sc8551_set_acovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_AC_OVP_BASE)
		threshold = SC8551_AC_OVP_BASE;

	if (threshold == SC8551_AC_OVP_6P5V)
		val = 0x07;
	else
		val = (threshold - SC8551_AC_OVP_BASE) / SC8551_AC_OVP_LSB;

	val <<= SC8551_AC_OVP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_05,
				 SC8551_AC_OVP_MASK, val);

	return ret;

}

static int sc8551_set_vdrop_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold == 300)
		val = SC8551_VDROP_THRESHOLD_300MV;
	else
		val = SC8551_VDROP_THRESHOLD_400MV;

	val <<= SC8551_VDROP_THRESHOLD_SET_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_05,
				 SC8551_VDROP_THRESHOLD_SET_MASK,
				 val);

	return ret;
}

static int sc8551_set_vdrop_deglitch(struct sc8551 *sc, int us)
{
	int ret;
	u8 val;

	if (us == 8)
		val = SC8551_VDROP_DEGLITCH_8US;
	else
		val = SC8551_VDROP_DEGLITCH_5MS;

	val <<= SC8551_VDROP_DEGLITCH_SET_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_05,
				 SC8551_VDROP_DEGLITCH_SET_MASK,
				 val);
	return ret;
}

static int sc8551_enable_bat_therm(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_TSBAT_ENABLE;
	else
		val = SC8551_TSBAT_DISABLE;

	val <<= SC8551_TSBAT_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0C,
				 SC8551_TSBAT_DIS_MASK, val);
	return ret;
}

static int sc8551_enable_bus_therm(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_TSBUS_ENABLE;
	else
		val = SC8551_TSBUS_DISABLE;

	val <<= SC8551_TSBUS_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0C,
				 SC8551_TSBUS_DIS_MASK, val);
	return ret;
}

static int sc8551_enable_adc(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_ADC_ENABLE;
	else
		val = SC8551_ADC_DISABLE;

	val <<= SC8551_ADC_EN_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_14,
				 SC8551_ADC_EN_MASK, val);
	return ret;
}

static int sc8551_set_adc_scanrate(struct sc8551 *sc, bool oneshot)
{
	int ret;
	u8 val;

	if (oneshot)
		val = SC8551_ADC_RATE_ONESHOT;
	else
		val = SC8551_ADC_RATE_CONTINOUS;

	val <<= SC8551_ADC_RATE_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_14,
				 SC8551_ADC_RATE_MASK, val);
	return ret;
}

static int sc8551_get_adc_data(struct sc8551 *sc, int channel,  int *result)
{
	u8 val_l = 0, val_h = 0;
	u16 val = 0;
	int ret = 0;

	if (channel >= ADC_MAX_NUM)
		return 0;

	ret = sc8551_read_byte(sc, SC8551_REG_16 + (channel << 1), &val_h);
	if (ret < 0)
		return ret;

	ret = sc8551_read_byte(sc, SC8551_REG_16 + (channel << 1) + 1, &val_l);
	if (ret < 0)
		return ret;

	val = (val_h << 8) | val_l;

	if (channel == ADC_IBUS)
		val = val * 15625 / 10000;
	else if (channel == ADC_VBUS)
		val = val * 375 / 100;
	else if (channel == ADC_VAC)
		val = val * 5;
	else if (channel == ADC_VOUT)
		val = val * 125 / 100;
	else if (channel == ADC_VBAT)
		val = val * 125 / 100;
	else if (channel == ADC_IBAT)
		val = val * 3125 / 1000;
	else if (channel == ADC_TDIE)
		val = val * 5 / 10;

	*result = val;

	return ret;
}

static int sc8551_set_adc_scan(struct sc8551 *sc, int channel, bool enable)
{
	int ret;
	u8 reg;
	u8 mask;
	u8 shift;
	u8 val;

	if (channel > ADC_MAX_NUM)
		return -EINVAL;

	if (channel == ADC_IBUS) {
		reg = SC8551_REG_14;
		shift = SC8551_IBUS_ADC_DIS_SHIFT;
		mask = SC8551_IBUS_ADC_DIS_MASK;
	} else {
		reg = SC8551_REG_15;
		shift = 8 - channel;
		mask = 1 << shift;
	}

	if (enable)
		val = 0 << shift;
	else
		val = 1 << shift;

	ret = sc8551_update_bits(sc, reg, mask, val);

	return ret;
}

static int sc8551_set_alarm_int_mask(struct sc8551 *sc, u8 mask)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0F, &val);
	if (ret)
		return ret;

	val |= mask;

	ret = sc8551_write_byte(sc, SC8551_REG_0F, val);

	return ret;
}

static int sc8551_set_sense_resistor(struct sc8551 *sc, int r_mohm)
{
	int ret;
	u8 val;

	if (r_mohm == 2)
		val = SC8551_SET_IBAT_SNS_RES_2MHM;
	else if (r_mohm == 5)
		val = SC8551_SET_IBAT_SNS_RES_5MHM;
	else
		return -EINVAL;

	val <<= SC8551_SET_IBAT_SNS_RES_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_2B,
				 SC8551_SET_IBAT_SNS_RES_MASK,
				 val);
	return ret;
}

static int sc8551_enable_regulation(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_EN_REGULATION_ENABLE;
	else
		val = SC8551_EN_REGULATION_DISABLE;

	val <<= SC8551_EN_REGULATION_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_2B,
				 SC8551_EN_REGULATION_MASK,
				 val);

	return ret;

}

static int sc8551_set_ss_timeout(struct sc8551 *sc, int timeout)
{
	int ret;
	u8 val;

	switch (timeout) {
	case 0:
		val = SC8551_SS_TIMEOUT_DISABLE;
		break;
	case 12:
		val = SC8551_SS_TIMEOUT_12P5MS;
		break;
	case 25:
		val = SC8551_SS_TIMEOUT_25MS;
		break;
	case 50:
		val = SC8551_SS_TIMEOUT_50MS;
		break;
	case 100:
		val = SC8551_SS_TIMEOUT_100MS;
		break;
	case 400:
		val = SC8551_SS_TIMEOUT_400MS;
		break;
	case 1500:
		val = SC8551_SS_TIMEOUT_1500MS;
		break;
	case 100000:
		val = SC8551_SS_TIMEOUT_100000MS;
		break;
	default:
		val = SC8551_SS_TIMEOUT_DISABLE;
		break;
	}

	val <<= SC8551_SS_TIMEOUT_SET_SHIFT;

	ret = sc8551_update_bits(sc,
				 SC8551_REG_2B,
				 SC8551_SS_TIMEOUT_SET_MASK,
				 val);

	return ret;
}

static int sc8551_set_ibat_reg_th(struct sc8551 *sc, int th_ma)
{
	int ret;
	u8 val;

	if (th_ma == 200)
		val = SC8551_IBAT_REG_200MA;
	else if (th_ma == 300)
		val = SC8551_IBAT_REG_300MA;
	else if (th_ma == 400)
		val = SC8551_IBAT_REG_400MA;
	else if (th_ma == 500)
		val = SC8551_IBAT_REG_500MA;
	else
		val = SC8551_IBAT_REG_500MA;

	val <<= SC8551_IBAT_REG_SHIFT;
	ret = sc8551_update_bits(sc,
				 SC8551_REG_2C,
				 SC8551_IBAT_REG_MASK,
				 val);

	return ret;
}

static int sc8551_set_vbat_reg_th(struct sc8551 *sc, int th_mv)
{
	int ret;
	u8 val;

	if (th_mv == 50)
		val = SC8551_VBAT_REG_50MV;
	else if (th_mv == 100)
		val = SC8551_VBAT_REG_100MV;
	else if (th_mv == 150)
		val = SC8551_VBAT_REG_150MV;
	else
		val = SC8551_VBAT_REG_200MV;

	val <<= SC8551_VBAT_REG_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2C,
				SC8551_VBAT_REG_MASK,
				val);

	return ret;
}

static int sc8551_get_work_mode(struct sc8551 *sc, int *mode)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0C, &val);

	if (ret) {
		dev_err(sc->dev, "Failed to read operation mode register\n");
		return ret;
	}

	val = (val & SC8551_MS_MASK) >> SC8551_MS_SHIFT;
	if (val == SC8551_MS_MASTER)
		*mode = SC8551_ROLE_MASTER;
	else if (val == SC8551_MS_SLAVE)
		*mode = SC8551_ROLE_SLAVE;
	else
		*mode = SC8551_ROLE_STDALONE;

	pr_debug("work mode:%s\n", *mode == SC8551_ROLE_STDALONE ? "Standalone" :
		(*mode == SC8551_ROLE_SLAVE ? "Slave" : "Master"));
	return ret;
}

static int sc8551_check_vbus_error_status(struct sc8551 *sc)
{
	int ret;
	u8 data;

	ret = sc8551_read_byte(sc, SC8551_REG_0A, &data);
	if (!ret)
		sc->vbus_error = data;

	return ret;
}

static void sc8551_check_alarm_status(struct sc8551 *sc)
{
	u8 flag = 0;
	u8 stat = 0;
	int ret;

	mutex_lock(&sc->data_lock);

	ret = sc8551_read_byte(sc, SC8551_REG_08, &flag);
	if (!ret && (flag & SC8551_IBUS_UCP_FALL_FLAG_MASK))
		pr_debug("UCP_FLAG =0x%02X\n",
			 !!(flag & SC8551_IBUS_UCP_FALL_FLAG_MASK));

	ret = sc8551_read_byte(sc, SC8551_REG_2D, &flag);
	if (!ret && (flag & SC8551_VDROP_OVP_FLAG_MASK))
		pr_debug("VDROP_OVP_FLAG =0x%02X\n",
			 !!(flag & SC8551_VDROP_OVP_FLAG_MASK));

	/*read to clear alarm flag*/
	ret = sc8551_read_byte(sc, SC8551_REG_0E, &flag);
	if (!ret && flag)
		pr_debug("INT_FLAG =0x%02X\n", flag);

	ret = sc8551_read_byte(sc, SC8551_REG_0D, &stat);
	if (!ret && stat != sc->prev_alarm) {
		pr_debug("INT_STAT = 0X%02x\n", stat);
		sc->prev_alarm = stat;
		sc->batt_present = !!(stat & VBAT_INSERT);
		sc->vbus_present = !!(stat & VBUS_INSERT);
	}

	ret = sc8551_read_byte(sc, SC8551_REG_08, &stat);
	if (!ret && (stat & 0x50))
		dev_err(sc->dev, "Reg[05]BUS_UCPOVP = 0x%02X\n", stat);

	ret = sc8551_read_byte(sc, SC8551_REG_0A, &stat);
	if (!ret && (stat & 0x02))
		dev_err(sc->dev, "Reg[0A]CONV_OCP = 0x%02X\n", stat);

	mutex_unlock(&sc->data_lock);
}

static void sc8551_check_fault_status(struct sc8551 *sc)
{
	u8 flag = 0;
	u8 stat = 0;
	int ret;

	mutex_lock(&sc->data_lock);

	ret = sc8551_read_byte(sc, SC8551_REG_10, &stat);
	if (!ret && stat)
		dev_err(sc->dev, "FAULT_STAT = 0x%02X\n", stat);

	ret = sc8551_read_byte(sc, SC8551_REG_11, &flag);
	if (!ret && flag)
		dev_err(sc->dev, "FAULT_FLAG = 0x%02X\n", flag);

	if (!ret && flag != sc->prev_fault) {
		sc->prev_fault = flag;
		sc->bat_ovp_fault = !!(flag & BAT_OVP_FAULT);
		sc->bat_ocp_fault = !!(flag & BAT_OCP_FAULT);
		sc->bus_ovp_fault = !!(flag & BUS_OVP_FAULT);
		sc->bus_ocp_fault = !!(flag & BUS_OCP_FAULT);
	}

	mutex_unlock(&sc->data_lock);
}

static int sc8551_detect_device(struct sc8551 *sc)
{
	int ret;
	u8 data;

	ret = sc8551_read_byte(sc, SC8551_REG_13, &data);
	if (ret == 0) {
		sc->part_no = (data & SC8551_DEV_ID_MASK);
		sc->part_no >>= SC8551_DEV_ID_SHIFT;
	}
	return ret;
}

static int sc8551_parse_dt(struct sc8551 *sc, struct device *dev)
{
	int ret;
	struct device_node *np = dev->of_node;

	sc->cfg = devm_kzalloc(dev,
			       sizeof(struct sc8551_cfg),
			       GFP_KERNEL);

	if (!sc->cfg)
		return -ENOMEM;

	sc->cfg->bat_ovp_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ovp-disable");
	sc->cfg->bat_ocp_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ocp-disable");
	sc->cfg->bus_ocp_disable = of_property_read_bool(np,
			"sc,sc8551,bus-ocp-disable");
	sc->cfg->bat_therm_disable = of_property_read_bool(np,
			"sc,sc8551,bat-therm-disable");
	sc->cfg->bus_therm_disable = of_property_read_bool(np,
			"sc,sc8551,bus-therm-disable");

	ret = of_property_read_u32(np, "sc,sc8551,bat-ovp-threshold",
				   &sc->cfg->bat_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bat-ocp-threshold",
				   &sc->cfg->bat_ocp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ovp-threshold",
				   &sc->cfg->bus_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ocp-threshold",
				   &sc->cfg->bus_ocp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ocp-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,ac-ovp-threshold",
				   &sc->cfg->ac_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read ac-ovp-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,sense-resistor-mohm",
				   &sc->cfg->sense_r_mohm);
	if (ret) {
		dev_err(sc->dev, "failed to read sense-resistor-mohm\n");
		return ret;
	}

	return 0;
}

static int sc8551_init_protection(struct sc8551 *sc)
{
	int ret;

	ret = sc8551_enable_batovp(sc, !sc->cfg->bat_ovp_disable);
	pr_debug("%s bat ovp %s\n",
		sc->cfg->bat_ovp_disable ? "disable" : "enable",
		!ret ? "successfully" : "failed");

	ret = sc8551_enable_batocp(sc, !sc->cfg->bat_ocp_disable);
	pr_debug("%s bat ocp %s\n",
		sc->cfg->bat_ocp_disable ? "disable" : "enable",
		!ret ? "successfully" : "failed");

	ret = sc8551_enable_busocp(sc, !sc->cfg->bus_ocp_disable);
	pr_debug("%s bus ocp %s\n",
		sc->cfg->bus_ocp_disable ? "disable" : "enable",
		!ret ? "successfully" : "failed");

	ret = sc8551_enable_bat_therm(sc, !sc->cfg->bat_therm_disable);
	pr_debug("%s bat therm %s\n",
		sc->cfg->bat_therm_disable ? "disable" : "enable",
		!ret ? "successfully" : "failed");

	ret = sc8551_enable_bus_therm(sc, !sc->cfg->bus_therm_disable);
	pr_debug("%s bus therm %s\n",
		sc->cfg->bus_therm_disable ? "disable" : "enable",
		!ret ? "successfully" : "failed");

	ret = sc8551_set_batovp_th(sc, sc->cfg->bat_ovp_th);
	pr_debug("set bat ovp th %d %s\n", sc->cfg->bat_ovp_th,
		!ret ? "successfully" : "failed");

	ret = sc8551_set_batocp_th(sc, sc->cfg->bat_ocp_th);
	pr_debug("set bat ocp threshold %d %s\n", sc->cfg->bat_ocp_th,
		!ret ? "successfully" : "failed");

	ret = sc8551_set_busovp_th(sc, sc->cfg->bus_ovp_th);
	pr_debug("set bus ovp threshold %d %s\n", sc->cfg->bus_ovp_th,
		!ret ? "successfully" : "failed");

	ret = sc8551_set_busocp_th(sc, sc->cfg->bus_ocp_th);
	pr_debug("set bus ocp threshold %d %s\n", sc->cfg->bus_ocp_th,
		!ret ? "successfully" : "failed");

	ret = sc8551_set_acovp_th(sc, sc->cfg->ac_ovp_th);
	pr_debug("set ac ovp threshold %d %s\n", sc->cfg->ac_ovp_th,
		!ret ? "successfully" : "failed");

	return 0;
}

static int sc8551_init_adc(struct sc8551 *sc)
{
	int ret;

	ret = sc8551_set_adc_scanrate(sc, false);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_IBUS, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_VBUS, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_VOUT, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_VBAT, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_IBAT, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_TBUS, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_TBAT, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_TDIE, true);
	if (ret)
		return ret;
	ret = sc8551_set_adc_scan(sc, ADC_VAC, true);
	if (ret)
		return ret;

	ret = sc8551_enable_adc(sc, true);
	if (ret)
		return ret;

	return 0;
}

static int sc8551_init_int_src(struct sc8551 *sc)
{
	int ret;

	/*TODO:be careful ts bus and ts bat alarm bit mask is in
	 *	fault mask register.
	 */
	ret = sc8551_set_alarm_int_mask(sc, ADC_DONE);
	if (ret)
		dev_err(sc->dev, "failed to set alarm mask:%d\n", ret);

	return ret;
}

static int sc8551_init_regulation(struct sc8551 *sc)
{
	int ret;

	ret = sc8551_set_ibat_reg_th(sc, 300);
	if (ret)
		return ret;
	ret = sc8551_set_vbat_reg_th(sc, 100);
	if (ret)
		return ret;

	ret = sc8551_set_vdrop_deglitch(sc, 5000);
	if (ret)
		return ret;
	ret = sc8551_set_vdrop_th(sc, 400);
	if (ret)
		return ret;

	ret = sc8551_enable_regulation(sc, false);
	if (ret)
		return ret;

	ret = sc8551_write_byte(sc, SC8551_REG_2E, 0x08);
	if (ret)
		return ret;

	return 0;
}

static int sc8551_init_device(struct sc8551 *sc)
{
	int ret;

	/* Reset registers to their default values */
	ret = sc8551_update_bits(sc,
				 SC8551_REG_0B,
				 SC8551_REG_RST_MASK,
				 SC8551_REG_RST_ENABLE << SC8551_REG_RST_SHIFT);
	if (ret)
		return ret;
	ret = sc8551_enable_wdt(sc, false);
	if (ret)
		return ret;
	ret = sc8551_set_wdt(sc, 30000);
	if (ret)
		return ret;
	ret = sc8551_set_ss_timeout(sc, 100000);
	if (ret)
		return ret;
	ret = sc8551_set_sense_resistor(sc, sc->cfg->sense_r_mohm);
	if (ret)
		return ret;
	ret = sc8551_init_protection(sc);
	if (ret)
		return ret;
	ret = sc8551_init_adc(sc);
	if (ret)
		return ret;
	ret = sc8551_init_int_src(sc);
	if (ret)
		return ret;
	ret = sc8551_init_regulation(sc);
	if (ret)
		return ret;

	return 0;
}

static int sc8551_set_present(struct sc8551 *sc, bool present)
{
	sc->usb_present = present;

	if (present)
		sc8551_init_device(sc);
	return 0;
}

static ssize_t registers_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct sc8551 *sc = dev_get_drvdata(dev);
	u8 tmpbuf[SC8551_DEBUG_BUF_LEN];
	int idx = 0;
	int result;
	u8 addr, val;
	int len, ret;

	ret = sc8551_get_adc_data(sc, ADC_VBAT, &result);
	if (!ret)
		sc->vbat_volt = result;

	ret = sc8551_get_adc_data(sc, ADC_VAC, &result);
	if (!ret)
		sc->vac_volt = result;

	ret = sc8551_get_adc_data(sc, ADC_VBUS, &result);
	if (!ret)
		sc->vbus_volt = result;

	ret = sc8551_get_adc_data(sc, ADC_VOUT, &result);
	if (!ret)
		sc->vout_volt = result;

	ret = sc8551_get_adc_data(sc, ADC_IBUS, &result);
	if (!ret)
		sc->ibus_curr = result;

	ret = sc8551_get_adc_data(sc, ADC_TDIE, &result);
	if (!ret)
		sc->die_temp = result;
	ret = sc8551_get_adc_data(sc, ADC_IBAT, &result);
	if (!ret)
		sc->ibat_curr = result;

	dev_err(sc->dev, "vbus_vol %d vbat_vol(vout) %d vout %d, vac: %d\n",
		sc->vbus_volt, sc->vbat_volt, sc->vout_volt, sc->vac_volt);
	dev_err(sc->dev, "ibus_curr %d ibat_curr %d\n", sc->ibus_curr, sc->ibat_curr);
	dev_err(sc->dev, "die_temp %d\n", sc->die_temp);

	for (addr = SC8551_REG_00; addr <= SC8551_REG_36; addr++) {
		ret = sc8551_read_byte(sc, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, SC8551_DEBUG_BUF_LEN,
				       "Reg[%.2X] = 0x%.2x\n",
				       addr,
				       val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct sc8551 *sc = dev_get_drvdata(dev);
	unsigned int reg;
	unsigned int val;
	int ret;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if ((ret == 2) && (reg >= SC8551_REG_00) && (reg <= SC8551_REG_36))
		sc8551_write_byte(sc,
				  (unsigned char)reg,
				  (unsigned char)val);

	return count;
}
static DEVICE_ATTR_RW(registers);

static void sc8551_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static enum power_supply_property sc8551_charger_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CP_DIE_TEMPERATURE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CP_BAT_OVP_FAULT,
	POWER_SUPPLY_PROP_CP_BAT_OCP_FAULT,
	POWER_SUPPLY_PROP_CP_BUS_OVP_FAULT,
	POWER_SUPPLY_PROP_CP_BUS_OCP_FAULT,
	POWER_SUPPLY_PROP_CP_VBUS_HERROR_STATUS,
	POWER_SUPPLY_PROP_CP_VBUS_LERROR_STATUS,
};

static int sc8551_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct sc8551 *sc = power_supply_get_drvdata(psy);
	int result;
	u8 reg_val;
	int ret;

	sc8551_check_alarm_status(sc);
	sc8551_check_fault_status(sc);
	sc8551_check_vbus_error_status(sc);
	switch (psp) {
	case POWER_SUPPLY_PROP_CP_CHARGING_ENABLED:
		sc8551_check_charge_enabled(sc, &sc->charge_enabled);
		val->intval = sc->charge_enabled;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sc->usb_present;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		ret = sc8551_read_byte(sc, SC8551_REG_0D, &reg_val);
		if (!ret)
			sc->vbus_present = !!(reg_val & VBUS_INSERT);
		val->intval = sc->vbus_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sc8551_get_adc_data(sc, ADC_VBAT, &result);
		if (!ret)
			sc->vbat_volt = result;
		val->intval = sc->vbat_volt * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sc8551_get_adc_data(sc, ADC_IBAT, &result);
		if (!ret)
			sc->ibat_curr = result;
		val->intval = sc->ibat_curr * 1000;
		break;
	case POWER_SUPPLY_PROP_CP_VBUS: /* BUS_VOLTAGE */
		ret = sc8551_get_adc_data(sc, ADC_VBUS, &result);
		if (!ret)
			sc->vbus_volt = result;
		val->intval = sc->vbus_volt * 1000;
		break;
	case POWER_SUPPLY_PROP_CP_IBUS: /* BUS_CURRENT */
		ret = sc8551_get_adc_data(sc, ADC_IBUS, &result);
		if (!ret)
			sc->ibus_curr = result;
		val->intval = sc->ibus_curr * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT: /* BUS_VOLTAGE */
		val->intval =  12000 * 1000; /* 20V */
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT: /* BUS_CURRENT */
		val->intval =  4500 * 1000; /* 4.75A */
		break;
	case POWER_SUPPLY_PROP_CP_DIE_TEMPERATURE:/* DIE_TEMPERATURE */
		ret = sc8551_get_adc_data(sc, ADC_TDIE, &result);
		if (!ret)
			sc->die_temp = result;
		val->intval = sc->die_temp;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 4300 * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = 8000 * 1000;
		break;

	case POWER_SUPPLY_PROP_CP_BAT_OVP_FAULT:
		val->intval = sc->bat_ovp_fault;
		break;
	case POWER_SUPPLY_PROP_CP_BAT_OCP_FAULT:
		val->intval = sc->bat_ocp_fault;
		break;
	case POWER_SUPPLY_PROP_CP_BUS_OVP_FAULT:
		val->intval = sc->bus_ovp_fault;
		break;
	case POWER_SUPPLY_PROP_CP_BUS_OCP_FAULT:
		val->intval = sc->bus_ocp_fault;
		break;
	case POWER_SUPPLY_PROP_CP_VBUS_HERROR_STATUS:
		val->intval = (sc->vbus_error >> 0x04) & 0x01;
		break;
	case POWER_SUPPLY_PROP_CP_VBUS_LERROR_STATUS:
		val->intval = (sc->vbus_error >> 0x05) & 0x01;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sc8551_charger_set_property(struct power_supply *psy,
				       enum power_supply_property prop,
				       const union power_supply_propval *val)
{
	struct sc8551 *sc = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CP_CHARGING_ENABLED:
		sc8551_enable_charge(sc, val->intval);
		if (val->intval)
			sc8551_enable_wdt(sc, true);
		else
			sc8551_enable_wdt(sc, false);
		sc8551_check_charge_enabled(sc, &sc->charge_enabled);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		sc8551_set_present(sc, !!val->intval);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sc8551_charger_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

static int sc8551_psy_register(struct sc8551 *sc)
{
	sc->psy_cfg.drv_data = sc;
	sc->psy_cfg.of_node = sc->dev->of_node;

	sc->psy_desc.name = "sc8551-standalone";

	sc->psy_desc.type = POWER_SUPPLY_TYPE_CHARGE_PUMP;
	sc->psy_desc.properties = sc8551_charger_props;
	sc->psy_desc.num_properties = ARRAY_SIZE(sc8551_charger_props);
	sc->psy_desc.get_property = sc8551_charger_get_property;
	sc->psy_desc.set_property = sc8551_charger_set_property;
	sc->psy_desc.property_is_writeable = sc8551_charger_is_writeable;

	sc->fc2_psy = devm_power_supply_register(sc->dev,
						 &sc->psy_desc,
						 &sc->psy_cfg);
	if (IS_ERR(sc->fc2_psy)) {
		dev_err(sc->dev, "failed to register fc2_psy\n");
		return PTR_ERR(sc->fc2_psy);
	}

	return 0;
}

/*
 * interrupt does nothing, just info event change, other module could get info
 * through power supply interface
 */
static irqreturn_t sc8551_charger_interrupt(int irq, void *dev_id)
{
	struct sc8551 *sc = dev_id;
	int ret, value;

	ret = sc8551_get_adc_data(sc, ADC_VOUT, &value);
	if (!ret)
		sc->vbat_volt = value;

	ret = sc8551_get_adc_data(sc, ADC_IBAT, &value);
	if (!ret)
		sc->ibat_curr = value;

	ret = sc8551_get_adc_data(sc, ADC_VBUS, &value);
	if (!ret)
		sc->vbus_volt = value;

	ret = sc8551_get_adc_data(sc, ADC_IBUS, &value);
	if (!ret)
		sc->ibus_curr = value;

	ret = sc8551_get_adc_data(sc, ADC_TDIE, &value);
	if (!ret)
		sc->die_temp = value;
	ret = sc8551_get_adc_data(sc, ADC_IBAT, &value);
	if (!ret)
		sc->ibat_curr = value;

	sc8551_check_alarm_status(sc);
	sc8551_check_fault_status(sc);
	sc8551_check_vbus_error_status(sc);
	power_supply_changed(sc->fc2_psy);

	return IRQ_HANDLED;
}

static int sc8551_init_irq(struct sc8551 *sc)
{
	int ret;

	sc->irq = sc->client->irq;
	if (sc->irq <= 0) {
		dev_err(sc->dev, "irq mapping fail\n");
		return 0;
	}

	ret = devm_request_threaded_irq(sc->dev,
					sc->irq,
					NULL,
					sc8551_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"sc8551 standalone irq",
					sc);
	if (ret < 0)
		dev_err(sc->dev, "request irq for irq=%d failed, ret =%d\n",
			sc->irq, ret);

	enable_irq_wake(sc->irq);
	device_init_wakeup(sc->dev, 1);

	return 0;
}

static void determine_initial_status(struct sc8551 *sc)
{
	if (sc->client->irq)
		sc8551_charger_interrupt(sc->client->irq, sc);
}

static const struct of_device_id sc8551_charger_match[] = {
	{ .compatible = "sc,sc8551-standalone", },
	{},
};

static int sc8551_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct sc8551 *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret;

	sc = devm_kzalloc(&client->dev, sizeof(struct sc8551), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;

	sc->client = client;

	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->data_lock);

	ret = sc8551_detect_device(sc);
	if (ret) {
		dev_err(sc->dev, "No sc8551 device found!\n");
		return -ENODEV;
	}

	i2c_set_clientdata(client, sc);

	match = of_match_node(sc8551_charger_match, node);
	if (match == NULL) {
		dev_err(sc->dev, "device tree match not found!\n");
		return -ENODEV;
	}
	sc8551_get_work_mode(sc, &sc->mode);
	if (sc->mode !=  SC8551_ROLE_STDALONE) {
		dev_err(sc->dev, "device operation mode mismatch with dts configuration\n");
		return -EINVAL;
	}

	ret = sc8551_parse_dt(sc, &client->dev);
	if (ret)
		return -EIO;

	ret = sc8551_init_device(sc);
	if (ret) {
		dev_err(sc->dev, "Failed to init device\n");
		return ret;
	}

	ret = sc8551_psy_register(sc);
	if (ret)
		return ret;

	ret = sc8551_init_irq(sc);
	if (ret)
		goto err_1;

	determine_initial_status(sc);
	sc8551_create_device_node(&(client->dev));

	return 0;

err_1:
	power_supply_unregister(sc->fc2_psy);
	return ret;
}

static int sc8551_charger_remove(struct i2c_client *client)
{
	struct sc8551 *sc = i2c_get_clientdata(client);


	sc8551_enable_adc(sc, false);

	power_supply_unregister(sc->fc2_psy);

	mutex_destroy(&sc->data_lock);
	mutex_destroy(&sc->i2c_rw_lock);

	return 0;
}

static void sc8551_charger_shutdown(struct i2c_client *client)
{
	struct sc8551 *sc = i2c_get_clientdata(client);

	sc8551_enable_adc(sc, false);
}

static const struct i2c_device_id sc8551_charger_id[] = {
	{"sc8551-standalone", SC8551_ROLE_STDALONE},
	{},
};

static struct i2c_driver sc8551_charger_driver = {
	.driver		= {
		.name	= "sc8551-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sc8551_charger_match,
	},
	.id_table	= sc8551_charger_id,

	.probe		= sc8551_charger_probe,
	.remove		= sc8551_charger_remove,
	.shutdown	= sc8551_charger_shutdown,
};

module_i2c_driver(sc8551_charger_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("SC SC8551 Charge Pump Driver");
MODULE_LICENSE("GPL");

