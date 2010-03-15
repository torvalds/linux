/* linux/arch/arm/mach-s3c6400/include/mach/pwm-clock.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64xx - pwm clock and timer support
 */

/**
 * pwm_cfg_src_is_tclk() - return whether the given mux config is a tclk
 * @tcfg: The timer TCFG1 register bits shifted down to 0.
 *
 * Return true if the given configuration from TCFG1 is a TCLK instead
 * any of the TDIV clocks.
 */
static inline int pwm_cfg_src_is_tclk(unsigned long tcfg)
{
	return tcfg >= S3C64XX_TCFG1_MUX_TCLK;
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
	return 1 << tcfg1;
}

/**
 * pwm_tdiv_has_div1() - does the tdiv setting have a /1
 *
 * Return true if we have a /1 in the tdiv setting.
 */
static inline unsigned int pwm_tdiv_has_div1(void)
{
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
	return ilog2(div);
}

#define S3C_TCFG1_MUX_TCLK S3C64XX_TCFG1_MUX_TCLK
