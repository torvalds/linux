/*
 * arch/arm/mach-lpc32xx/common.h
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2009-2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LPC32XX_COMMON_H
#define __LPC32XX_COMMON_H

#include <mach/board.h>
#include <linux/platform_device.h>

/*
 * Arch specific platform device structures
 */
extern struct platform_device lpc32xx_watchdog_device;
extern struct platform_device lpc32xx_i2c0_device;
extern struct platform_device lpc32xx_i2c1_device;
extern struct platform_device lpc32xx_i2c2_device;
extern struct platform_device lpc32xx_tsc_device;
extern struct platform_device lpc32xx_adc_device;
extern struct platform_device lpc32xx_rtc_device;
extern struct platform_device lpc32xx_ohci_device;
extern struct platform_device lpc32xx_net_device;

/*
 * Other arch specific structures and functions
 */
extern struct sys_timer lpc32xx_timer;
extern void __init lpc32xx_init_irq(void);
extern void __init lpc32xx_map_io(void);
extern void __init lpc32xx_serial_init(void);
extern void __init lpc32xx_gpio_init(void);
extern void lpc23xx_restart(char, const char *);


/*
 * Structure used for setting up and querying the PLLS
 */
struct clk_pll_setup {
	int analog_on;
	int cco_bypass_b15;
	int direct_output_b14;
	int fdbk_div_ctrl_b13;
	int pll_p;
	int pll_n;
	u32 pll_m;
};

extern int clk_is_sysclk_mainosc(void);
extern u32 clk_check_pll_setup(u32 ifreq, struct clk_pll_setup *pllsetup);
extern u32 clk_get_pllrate_from_reg(u32 inputclk, u32 regval);
extern u32 clk_get_pclk_div(void);

/*
 * Returns the LPC32xx unique 128-bit chip ID
 */
extern void lpc32xx_get_uid(u32 devid[4]);

extern u32 lpc32xx_return_iram_size(void);
/*
 * Pointers used for sizing and copying suspend function data
 */
extern int lpc32xx_sys_suspend(void);
extern int lpc32xx_sys_suspend_sz;

#endif
