/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Header for Broadcom brcmstb GPIO based drivers
 *
 * Copyright (C) 2024-2025 Ivan T. Ivanov, Andrea della Porta
 * Copyright (C) 2021-3 Raspberry Pi Ltd.
 * Copyright (C) 2012 Chris Boot, Simon Arlott, Stephen Warren
 *
 * Based heavily on the BCM2835 GPIO & pinctrl driver, which was inspired by:
 * pinctrl-nomadik.c, please see original file for copyright information
 * pinctrl-tegra.c, please see original file for copyright information
 */

#ifndef __PINCTRL_BRCMSTB_H__
#define __PINCTRL_BRCMSTB_H__

#include <linux/types.h>
#include <linux/platform_device.h>

#define BRCMSTB_FUNC(f) \
	[func_##f] = #f

#define MUX_BIT_VALID		0x8000
#define PAD_BIT_INVALID		0xffff

#define MUX_BIT(muxreg, muxshift) \
	(MUX_BIT_VALID + ((muxreg) << 5) + ((muxshift) << 2))
#define PAD_BIT(padreg, padshift) \
	(((padreg) << 5) + ((padshift) << 1))

#define GPIO_REGS(n, muxreg, muxshift, padreg, padshift) \
	[n] = { MUX_BIT(muxreg, muxshift), PAD_BIT(padreg, padshift) }

#define EMMC_REGS(n, padreg, padshift) \
	[n] = { 0, PAD_BIT(padreg, padshift) }

#define AON_GPIO_REGS(n, muxreg, muxshift, padreg, padshift) \
	GPIO_REGS(n, muxreg, muxshift, padreg, padshift)

#define AON_SGPIO_REGS(n, muxreg, muxshift) \
	[(n) + 32] = { MUX_BIT(muxreg, muxshift), PAD_BIT_INVALID }

#define GPIO_PIN(n)		PINCTRL_PIN(n, "gpio" #n)
/**
 * AON pins are in the Always-On power domain. SGPIOs are also 'Safe'
 * being 5V tolerant (necessary for the HDMI I2C pins), and can be driven
 * while the power is off.
 */
#define AON_GPIO_PIN(n)		PINCTRL_PIN(n, "aon_gpio" #n)
#define AON_SGPIO_PIN(n)	PINCTRL_PIN((n) + 32, "aon_sgpio" #n)

struct pin_regs {
	u16 mux_bit;
	u16 pad_bit;
};

/**
 * struct brcmstb_pin_funcs - pins provide their primary/alternate
 * functions in this struct
 * @func_mask: mask representing valid bits of the function selector
 *	in the registers
 * @funcs: array of function identifiers
 * @n_funcs: number of identifiers of the @funcs array above
 */
struct brcmstb_pin_funcs {
	const u32 func_mask;
	const u8 *funcs;
	const unsigned int n_funcs;
};

/**
 * struct brcmstb_pdata - specific data for a pinctrl chip implementation
 * @pctl_desc: pin controller descriptor for this implementation
 * @gpio_range: range of GPIOs served by this controller
 * @pin_regs: array of register descriptors for each pin
 * @pin_funcs: array of all possible assignable function for each pin
 * @func_count: total number of functions
 * @func_gpio: which function number is GPIO (usually 0)
 * @func_names: an array listing all function names
 */
struct brcmstb_pdata {
	const struct pinctrl_desc *pctl_desc;
	const struct pinctrl_gpio_range *gpio_range;
	const struct pin_regs *pin_regs;
	const struct brcmstb_pin_funcs *pin_funcs;
	const unsigned int func_count;
	const unsigned int func_gpio;
	const char * const *func_names;
};

int brcmstb_pinctrl_probe(struct platform_device *pdev);

#endif
