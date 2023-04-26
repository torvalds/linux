/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Tangier GPIO functions
 *
 * Copyright (c) 2016, 2021, 2023 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Pandith N <pandith.n@intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#ifndef _GPIO_TANGIER_H_
#define _GPIO_TANGIER_H_

#include <linux/gpio/driver.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

struct device;

struct tng_gpio_context;

/* Elkhart Lake specific wake registers */
#define GWMR_EHL	0x100	/* Wake mask */
#define GWSR_EHL	0x118	/* Wake source */
#define GSIR_EHL	0x130	/* Secure input */

/* Merrifield specific wake registers */
#define GWMR_MRFLD	0x400	/* Wake mask */
#define GWSR_MRFLD	0x418	/* Wake source */
#define GSIR_MRFLD	0xc00	/* Secure input */

/**
 * struct tng_wake_regs - Platform specific wake registers
 * @gwmr: Wake mask
 * @gwsr: Wake source
 * @gsir: Secure input
 */
struct tng_wake_regs {
	u32 gwmr;
	u32 gwsr;
	u32 gsir;
};

/**
 * struct tng_gpio_pinrange - Map pin numbers to gpio numbers
 * @gpio_base: Starting GPIO number of this range
 * @pin_base: Starting pin number of this range
 * @npins: Number of pins in this range
 */
struct tng_gpio_pinrange {
	unsigned int gpio_base;
	unsigned int pin_base;
	unsigned int npins;
};

#define GPIO_PINRANGE(gstart, gend, pstart)		\
(struct tng_gpio_pinrange) {				\
		.gpio_base = (gstart),			\
		.pin_base = (pstart),			\
		.npins = (gend) - (gstart) + 1,		\
	}

/**
 * struct tng_gpio_pin_info - Platform specific pinout information
 * @pin_ranges: Pin to GPIO mapping
 * @nranges: Number of pin ranges
 * @name: Respective pinctrl device name
 */
struct tng_gpio_pin_info {
	const struct tng_gpio_pinrange *pin_ranges;
	unsigned int nranges;
	const char *name;
};

/**
 * struct tng_gpio_info - Platform specific GPIO and IRQ information
 * @base: GPIO base to start numbering with
 * @ngpio: Amount of GPIOs supported by the controller
 * @first: First IRQ to start numbering with
 */
struct tng_gpio_info {
	int base;
	u16 ngpio;
	unsigned int first;
};

/**
 * struct tng_gpio - Platform specific private data
 * @chip: Instance of the struct gpio_chip
 * @reg_base: Base address of MMIO registers
 * @irq: Interrupt for the GPIO device
 * @lock: Synchronization lock to prevent I/O race conditions
 * @dev: The GPIO device
 * @ctx: Context to be saved during suspend-resume
 * @wake_regs: Platform specific wake registers
 * @pin_info: Platform specific pinout information
 * @info: Platform specific GPIO and IRQ information
 */
struct tng_gpio {
	struct gpio_chip chip;
	void __iomem *reg_base;
	int irq;
	raw_spinlock_t lock;
	struct device *dev;
	struct tng_gpio_context *ctx;
	struct tng_wake_regs wake_regs;
	struct tng_gpio_pin_info pin_info;
	struct tng_gpio_info info;
};

int devm_tng_gpio_probe(struct device *dev, struct tng_gpio *gpio);

int tng_gpio_suspend(struct device *dev);
int tng_gpio_resume(struct device *dev);

#endif /* _GPIO_TANGIER_H_ */
