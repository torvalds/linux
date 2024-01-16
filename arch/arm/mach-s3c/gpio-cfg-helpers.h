/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung Platform - GPIO pin configuration helper definitions
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

static inline int samsung_gpio_do_setcfg(struct samsung_gpio_chip *chip,
					 unsigned int off, unsigned int config)
{
	return (chip->config->set_config)(chip, off, config);
}

static inline unsigned samsung_gpio_do_getcfg(struct samsung_gpio_chip *chip,
					      unsigned int off)
{
	return (chip->config->get_config)(chip, off);
}

static inline int samsung_gpio_do_setpull(struct samsung_gpio_chip *chip,
					  unsigned int off, samsung_gpio_pull_t pull)
{
	return (chip->config->set_pull)(chip, off, pull);
}

static inline samsung_gpio_pull_t samsung_gpio_do_getpull(struct samsung_gpio_chip *chip,
							  unsigned int off)
{
	return chip->config->get_pull(chip, off);
}

/* Pull-{up,down} resistor controls.
 *
 * S3C2410,S3C2440 = Pull-UP,
 * S3C2412,S3C2413 = Pull-Down
 * S3C6400,S3C6410 = Pull-Both [None,Down,Up,Undef]
 * S3C2443 = Pull-Both [not same as S3C6400]
 */

/**
 * s3c24xx_gpio_setpull_1up() - Pull configuration for choice of up or none.
 * @chip: The gpio chip that is being configured.
 * @off: The offset for the GPIO being configured.
 * @param: pull: The pull mode being requested.
 *
 * This is a helper function for the case where we have GPIOs with one
 * bit configuring the presence of a pull-up resistor.
 */
extern int s3c24xx_gpio_setpull_1up(struct samsung_gpio_chip *chip,
				    unsigned int off, samsung_gpio_pull_t pull);

/**
 * s3c24xx_gpio_setpull_1down() - Pull configuration for choice of down or none
 * @chip: The gpio chip that is being configured
 * @off: The offset for the GPIO being configured
 * @param: pull: The pull mode being requested
 *
 * This is a helper function for the case where we have GPIOs with one
 * bit configuring the presence of a pull-down resistor.
 */
extern int s3c24xx_gpio_setpull_1down(struct samsung_gpio_chip *chip,
				      unsigned int off, samsung_gpio_pull_t pull);

/**
 * samsung_gpio_setpull_upown() - Pull configuration for choice of up,
 * down or none
 *
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
extern int samsung_gpio_setpull_updown(struct samsung_gpio_chip *chip,
				       unsigned int off, samsung_gpio_pull_t pull);

/**
 * samsung_gpio_getpull_updown() - Get configuration for choice of up,
 * down or none
 *
 * @chip: The gpio chip that the GPIO pin belongs to
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-{up,down} resistor
 * for the given GPIO in the same case as samsung_gpio_setpull_upown.
*/
extern samsung_gpio_pull_t samsung_gpio_getpull_updown(struct samsung_gpio_chip *chip,
						       unsigned int off);

/**
 * s3c24xx_gpio_getpull_1up() - Get configuration for choice of up or none
 * @chip: The gpio chip that the GPIO pin belongs to
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-up resistor for the
 * given GPIO in the same case as s3c24xx_gpio_setpull_1up.
*/
extern samsung_gpio_pull_t s3c24xx_gpio_getpull_1up(struct samsung_gpio_chip *chip,
						    unsigned int off);

/**
 * s3c24xx_gpio_getpull_1down() - Get configuration for choice of down or none
 * @chip: The gpio chip that the GPIO pin belongs to
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-down resistor for the
 * given GPIO in the same case as s3c24xx_gpio_setpull_1down.
*/
extern samsung_gpio_pull_t s3c24xx_gpio_getpull_1down(struct samsung_gpio_chip *chip,
						      unsigned int off);

/**
 * s3c2443_gpio_setpull() - Pull configuration for s3c2443.
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
extern int s3c2443_gpio_setpull(struct samsung_gpio_chip *chip,
				unsigned int off, samsung_gpio_pull_t pull);

/**
 * s3c2443_gpio_getpull() - Get configuration for s3c2443 pull resistors
 * @chip: The gpio chip that the GPIO pin belongs to.
 * @off: The offset to the pin to get the configuration of.
 *
 * This helper function reads the state of the pull-{up,down} resistor for the
 * given GPIO in the same case as samsung_gpio_setpull_upown.
*/
extern samsung_gpio_pull_t s3c2443_gpio_getpull(struct samsung_gpio_chip *chip,
						unsigned int off);

#endif /* __PLAT_GPIO_CFG_HELPERS_H */
