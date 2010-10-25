/* linux/arch/arm/plat-s3c/include/plat/gpio-cfg-helper.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C Platform - GPIO pin configuration helper definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* This is meant for core cpu support, machine or other driver files
 * should not be including this header.
 */

#ifndef __PLAT_GPIO_CFG_HELPERS_H
#define __PLAT_GPIO_CFG_HELPERS_H __FILE__

/* As a note, all gpio configuration functions are entered exclusively, either
 * with the relevant lock held or the system prevented from doing anything else
 * by disabling interrupts.
*/

static inline int s3c_gpio_do_setcfg(struct s3c_gpio_chip *chip,
				     unsigned int off, unsigned int config)
{
	return (chip->config->set_config)(chip, off, config);
}

static inline unsigned s3c_gpio_do_getcfg(struct s3c_gpio_chip *chip,
					  unsigned int off)
{
	return (chip->config->get_config)(chip, off);
}

static inline int s3c_gpio_do_setpull(struct s3c_gpio_chip *chip,
				      unsigned int off, s3c_gpio_pull_t pull)
{
	return (chip->config->set_pull)(chip, off, pull);
}

static inline s3c_gpio_pull_t s3c_gpio_do_getpull(struct s3c_gpio_chip *chip,
						  unsigned int off)
{
	return chip->config->get_pull(chip, off);
}

/**
 * s3c_gpio_setcfg_s3c24xx - S3C24XX style GPIO configuration.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register
 * has two bits of configuration per gpio, which have the following
 * functions:
 *	00 = input
 *	01 = output
 *	1x = special function
*/
extern int s3c_gpio_setcfg_s3c24xx(struct s3c_gpio_chip *chip,
				   unsigned int off, unsigned int cfg);

/**
 * s3c_gpio_getcfg_s3c24xx - S3C24XX style GPIO configuration read.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of s3c_gpio_setcfg_s3c24xx(). Will return a value whicg
 * could be directly passed back to s3c_gpio_setcfg_s3c24xx(), from the
 * S3C_GPIO_SPECIAL() macro.
 */
unsigned int s3c_gpio_getcfg_s3c24xx(struct s3c_gpio_chip *chip,
				     unsigned int off);

/**
 * s3c_gpio_setcfg_s3c24xx_a - S3C24XX style GPIO configuration (Bank A)
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register
 * has one bit of configuration for the gpio, where setting the bit
 * means the pin is in special function mode and unset means output.
*/
extern int s3c_gpio_setcfg_s3c24xx_a(struct s3c_gpio_chip *chip,
				     unsigned int off, unsigned int cfg);


/**
 * s3c_gpio_getcfg_s3c24xx_a - S3C24XX style GPIO configuration read (Bank A)
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of s3c_gpio_setcfg_s3c24xx_a() turning an GPIO into a usable
 * GPIO configuration value.
 *
 * @sa s3c_gpio_getcfg_s3c24xx
 * @sa s3c_gpio_getcfg_s3c64xx_4bit
 */
extern unsigned s3c_gpio_getcfg_s3c24xx_a(struct s3c_gpio_chip *chip,
					  unsigned int off);

/**
 * s3c_gpio_setcfg_s3c64xx_4bit - S3C64XX 4bit single register GPIO config.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @cfg: The configuration value to set.
 *
 * This helper deal with the GPIO cases where the control register has 4 bits
 * of control per GPIO, generally in the form of:
 *	0000 = Input
 *	0001 = Output
 *	others = Special functions (dependant on bank)
 *
 * Note, since the code to deal with the case where there are two control
 * registers instead of one, we do not have a separate set of functions for
 * each case.
*/
extern int s3c_gpio_setcfg_s3c64xx_4bit(struct s3c_gpio_chip *chip,
					unsigned int off, unsigned int cfg);


/**
 * s3c_gpio_getcfg_s3c64xx_4bit - S3C64XX 4bit single register GPIO config read.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 *
 * The reverse of s3c_gpio_setcfg_s3c64xx_4bit(), turning a gpio configuration
 * register setting into a value the software can use, such as could be passed
 * to s3c_gpio_setcfg_s3c64xx_4bit().
 *
 * @sa s3c_gpio_getcfg_s3c24xx
 */
extern unsigned s3c_gpio_getcfg_s3c64xx_4bit(struct s3c_gpio_chip *chip,
					     unsigned int off);

/* Pull-{up,down} resistor controls.
 *
 * S3C2410,S3C2440,S3C24A0 = Pull-UP,
 * S3C2412,S3C2413 = Pull-Down
 * S3C6400,S3C6410 = Pull-Both [None,Down,Up,Undef]
 * S3C2443 = Pull-Both [not same as S3C6400]
 */

/**
 * s3c_gpio_setpull_1up() - Pull configuration for choice of up or none.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @param: pull: The pull mode being requested.
 *
 * This is a helper function for the case where we have GPIOs with one
 * bit configuring the presence of a pull-up resistor.
 */
extern int s3c_gpio_setpull_1up(struct s3c_gpio_chip *chip,
				unsigned int off, s3c_gpio_pull_t pull);

/**
 * s3c_gpio_setpull_1down() - Pull configuration for choice of down or none
 * @chip: The gpio chip that is being configured
 * @off: The offset for the GPIO being configured
 * @param: pull: The pull mode being requested
 *
 * This is a helper function for the case where we have GPIOs with one
 * bit configuring the presence of a pull-down resistor.
 */
extern int s3c_gpio_setpull_1down(struct s3c_gpio_chip *chip,
				  unsigned int off, s3c_gpio_pull_t pull);

/**
 * s3c_gpio_setpull_upown() - Pull configuration for choice of up, down or none
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @param: pull: The pull mode being requested.
 *
 * This is a helper function for the case where we have GPIOs with two
 * bits configuring the presence of a pull resistor, in the following
 * order:
 *	00 = No pull resistor connected
 *	01 = Pull-up resistor connected
 *	10 = Pull-down resistor connected
 */
extern int s3c_gpio_setpull_updown(struct s3c_gpio_chip *chip,
				   unsigned int off, s3c_gpio_pull_t pull);


/**
 * s3c_gpio_getpull_updown() - Get configuration for choice of up, down or none
 * @chip: The gpio chip that the GPIO pin belongs to
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-{up,down} resistor for the
 * given GPIO in the same case as s3c_gpio_setpull_upown.
*/
extern s3c_gpio_pull_t s3c_gpio_getpull_updown(struct s3c_gpio_chip *chip,
					       unsigned int off);

/**
 * s3c_gpio_getpull_1up() - Get configuration for choice of up or none
 * @chip: The gpio chip that the GPIO pin belongs to
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-up resistor for the
 * given GPIO in the same case as s3c_gpio_setpull_1up.
*/
extern s3c_gpio_pull_t s3c_gpio_getpull_1up(struct s3c_gpio_chip *chip,
					    unsigned int off);

/**
 * s3c_gpio_setpull_s3c2443() - Pull configuration for s3c2443.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @param: pull: The pull mode being requested.
 *
 * This is a helper function for the case where we have GPIOs with two
 * bits configuring the presence of a pull resistor, in the following
 * order:
 *	00 = Pull-up resistor connected
 *	10 = Pull-down resistor connected
 *	x1 = No pull up resistor
 */
extern int s3c_gpio_setpull_s3c2443(struct s3c_gpio_chip *chip,
				    unsigned int off, s3c_gpio_pull_t pull);

/**
 * s3c_gpio_getpull_s3c2443() - Get configuration for s3c2443 pull resistors
 * @chip: The gpio chip that the GPIO pin belongs to.
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-{up,down} resistor for the
 * given GPIO in the same case as s3c_gpio_setpull_upown.
*/
extern s3c_gpio_pull_t s3c_gpio_getpull_s3c24xx(struct s3c_gpio_chip *chip,
						unsigned int off);

#endif /* __PLAT_GPIO_CFG_HELPERS_H */

