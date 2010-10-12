/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 *
 * Based on arch/arm/mach-pxa/include/mach/mfp.h:
 *   Copyright (C) 2007 Marvell International Ltd.
 *   eric miao <eric.miao@marvell.com>
 */

#ifndef __PLAT_PINCFG_H
#define __PLAT_PINCFG_H

/*
 * pin configurations are represented by 32-bit integers:
 *
 *	bit  0.. 8 - Pin Number (512 Pins Maximum)
 *	bit  9..10 - Alternate Function Selection
 *	bit 11..12 - Pull up/down state
 *	bit     13 - Sleep mode behaviour
 *
 * to facilitate the definition, the following macros are provided
 *
 * PIN_CFG_DEFAULT - default config (0):
 *		     pull up/down = disabled
 *		     sleep mode = input
 *
 * PIN_CFG	   - default config with alternate function
 * PIN_CFG_PULL	   - default config with alternate function and pull up/down
 */

typedef unsigned long pin_cfg_t;

#define PIN_NUM_MASK		0x1ff
#define PIN_NUM(x)		((x) & PIN_NUM_MASK)

#define PIN_ALT_SHIFT		9
#define PIN_ALT_MASK		(0x3 << PIN_ALT_SHIFT)
#define PIN_ALT(x)		(((x) & PIN_ALT_MASK) >> PIN_ALT_SHIFT)
#define PIN_GPIO		(NMK_GPIO_ALT_GPIO << PIN_ALT_SHIFT)
#define PIN_ALT_A		(NMK_GPIO_ALT_A << PIN_ALT_SHIFT)
#define PIN_ALT_B		(NMK_GPIO_ALT_B << PIN_ALT_SHIFT)
#define PIN_ALT_C		(NMK_GPIO_ALT_C << PIN_ALT_SHIFT)

#define PIN_PULL_SHIFT		11
#define PIN_PULL_MASK		(0x3 << PIN_PULL_SHIFT)
#define PIN_PULL(x)		(((x) & PIN_PULL_MASK) >> PIN_PULL_SHIFT)
#define PIN_PULL_NONE		(NMK_GPIO_PULL_NONE << PIN_PULL_SHIFT)
#define PIN_PULL_UP		(NMK_GPIO_PULL_UP << PIN_PULL_SHIFT)
#define PIN_PULL_DOWN		(NMK_GPIO_PULL_DOWN << PIN_PULL_SHIFT)

#define PIN_SLPM_SHIFT		13
#define PIN_SLPM_MASK		(0x1 << PIN_SLPM_SHIFT)
#define PIN_SLPM(x)		(((x) & PIN_SLPM_MASK) >> PIN_SLPM_SHIFT)
#define PIN_SLPM_INPUT		(NMK_GPIO_SLPM_INPUT << PIN_SLPM_SHIFT)
#define PIN_SLPM_NOCHANGE	(NMK_GPIO_SLPM_NOCHANGE << PIN_SLPM_SHIFT)

#define PIN_CFG_DEFAULT		(PIN_PULL_NONE | PIN_SLPM_INPUT)

#define PIN_CFG(num, alt)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt))

#define PIN_CFG_PULL(num, alt, pull)	\
	((PIN_CFG_DEFAULT & ~PIN_PULL_MASK) |\
	 (PIN_NUM(num) | PIN_##alt | PIN_PULL_##pull))

extern int nmk_config_pin(pin_cfg_t cfg);
extern int nmk_config_pins(pin_cfg_t *cfgs, int num);

#endif
