/* include/linux/leds-rt8547.h
 * Include file of driver to Richtek RT8547 LED Flash IC
 *
 * Copyright (C) 2014 Richtek Technology Corporation
 * Author: CY_Huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_LEDS_RT8547_H
#define __LINUX_LEDS_RT8547_H

#define RT8547_DRV_VER "1.0.2_G"

enum {
	RT8547_FLED_REG0 = 0x01,
	RT8547_FLED_REG1,
	RT8547_FLED_REG2,
	RT8547_FLED_REG3,
	RT8547_FLED_REGMAX,
};

enum {
	RT8547_LVP_3V,
	RT8547_LVP_3P1V,
	RT8547_LVP_3P2V,
	RT8547_LVP_3P3V,
	RT8547_LVP_3P4V,
	RT8547_LVP_3P5V,
	RT8547_LVP_3P6V,
	RT8547_LVP_3P7V,
	RT8547_LVP_3P8V,
	RT8547_LVP_MAX = RT8547_LVP_3P8V,
};

enum {
	RT8547_TOL_100mA,
	RT8547_TOL_150mA,
	RT8547_TOL_200mA,
	RT8547_TOL_250mA,
	RT8547_TOL_300mA,
	RT8547_TOL_350mA,
	RT8547_TOL_400mA,
	RT8547_TOL_MAX = RT8547_TOL_400mA,
};

#define RT8547_STO_MAX 36

#define RT8547_LVP_MASK 0x0F
#define RT8547_TOCLEVEL_MASK 0xE0
#define RT8547_TOCLEVEL_SHFT 5
#define RT8547_SCLEVEL_MASK 0x1F
#define RT8547_SWRST_MASK 0x20
#define RT8547_MODESEL_MASK 0x10
#define RT8547_TCLEVEL_MASK 0x0F
#define RT8547_STO_MASK 0x3F

struct rt8547_platform_data {
	int flen_gpio;
	int flen_active;
	int ctl_gpio;
	int ctl_active;
	int flset_gpio;
	int flset_active;
	unsigned char def_lvp:4;
	unsigned char def_tol:3;
};

/* one wire protocol parameter */
#define RT8547_ONEWIRE_ADDR 0x99
#define RT8547_LONG_DELAY 9
#define RT8547_SHORT_DELAY 4
#define RT8547_START_DELAY 10
#define RT8547_STOP_DELAY 1500

#ifdef CONFIG_LEDS_RT8547_DBG
#define RT_DBG(fmt, args...) pr_info("%s: " fmt, __func__, ##args)
#else
#define RT_DBG(fmt, args...)
#endif /* #ifdef CONFIG_LEDS_RT8547_DBG */

#endif /* #ifndef __LINUX_LEDS_RT8547_H */
