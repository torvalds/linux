/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Tangier pinctrl functions
 *
 * Copyright (C) 2016, 2023 Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#ifndef PINCTRL_TANGIER_H
#define PINCTRL_TANGIER_H

#include <linux/spinlock_types.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"

struct device;
struct platform_device;

#define TNG_FAMILY_NR			64
#define TNG_FAMILY_LEN			0x400

/**
 * struct tng_family - Tangier pin family description
 * @barno: MMIO BAR number where registers for this family reside
 * @pin_base: Starting pin of pins in this family
 * @npins: Number of pins in this family
 * @protected: True if family is protected by access
 * @regs: Family specific common registers
 */
struct tng_family {
	unsigned int barno;
	unsigned int pin_base;
	size_t npins;
	bool protected;
	void __iomem *regs;
};

#define TNG_FAMILY(b, s, e)				\
	{						\
		.barno = (b),				\
		.pin_base = (s),			\
		.npins = (e) - (s) + 1,			\
	}

#define TNG_FAMILY_PROTECTED(b, s, e)			\
	{						\
		.barno = (b),				\
		.pin_base = (s),			\
		.npins = (e) - (s) + 1,			\
		.protected = true,			\
	}

/**
 * struct tng_pinctrl - Tangier pinctrl private structure
 * @dev: Pointer to the device structure
 * @lock: Lock to serialize register access
 * @pctldesc: Pin controller description
 * @pctldev: Pointer to the pin controller device
 * @families: Array of families this pinctrl handles
 * @nfamilies: Number of families in the array
 * @functions: Array of functions
 * @nfunctions: Number of functions in the array
 * @groups: Array of pin groups
 * @ngroups: Number of groups in the array
 * @pins: Array of pins this pinctrl controls
 * @npins: Number of pins in the array
 */
struct tng_pinctrl {
	struct device *dev;
	raw_spinlock_t lock;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;

	/* Pin controller configuration */
	const struct tng_family *families;
	size_t nfamilies;
	const struct intel_function *functions;
	size_t nfunctions;
	const struct intel_pingroup *groups;
	size_t ngroups;
	const struct pinctrl_pin_desc *pins;
	size_t npins;
};

int devm_tng_pinctrl_probe(struct platform_device *pdev);

#endif /* PINCTRL_TANGIER_H */
