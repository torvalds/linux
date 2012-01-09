/* linux/arch/arm/plat-samsung/include/plat/pwm-clock.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * SAMSUNG - pwm clock and timer support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_PWM_CLOCK_H
#define __ASM_PLAT_PWM_CLOCK_H __FILE__

/**
 * pwm_cfg_src_is_tclk() - return whether the given mux config is a tclk
 * @tcfg: The timer TCFG1 register bits shifted down to 0.
 *
 * Return true if the given configuration from TCFG1 is a TCLK instead
 * any of the TDIV clocks.
 */
static inline int pwm_cfg_src_is_tclk(unsigned long tcfg)
{
	if (soc_is_s3c24xx())
		return tcfg == S3C2410_TCFG1_MUX_TCLK;
	else if (soc_is_s3c64xx() || soc_is_s5pc100())
		return tcfg >= S3C64XX_TCFG1_MUX_TCLK;
	else if (soc_is_s5p6440() || soc_is_s5p6450())
		return 0;
	else
		return tcfg == S3C64XX_TCFG1_MUX_TCLK;
}

/**
 * tcfg_to_divisor() - convert tcfg1 setting to a divisor
 * @tcfg1: The tcfg1 setting, shifted down.
 *
 * Get the divisor value for the given tcfg1 setting. We assume the
 * caller has already checked to see if this is not a TCLK source.
 */
static inline unsigned long tcfg_to_divisor(unsigned long tcfg1)
{
	if (soc_is_s3c24xx())
		return 1 << (tcfg1 + 1);
	else
		return 1 << tcfg1;
}

/**
 * pwm_tdiv_has_div1() - does the tdiv setting have a /1
 *
 * Return true if we have a /1 in the tdiv setting.
 */
static inline unsigned int pwm_tdiv_has_div1(void)
{
	if (soc_is_s3c24xx())
		return 0;
	else
		return 1;
}

/**
 * pwm_tdiv_div_bits() - calculate TCFG1 divisor value.
 * @div: The divisor to calculate the bit information for.
 *
 * Turn a divisor into the necessary bit field for TCFG1.
 */
static inline unsigned long pwm_tdiv_div_bits(unsigned int div)
{
	if (soc_is_s3c24xx())
		return ilog2(div) - 1;
	else
		return ilog2(div);
}
#endif /* __ASM_PLAT_PWM_CLOCK_H */
