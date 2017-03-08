/*
 * Core pinctrl/GPIO driver for Intel GPIO controllers
 *
 * Copyright (C) 2015, Intel Corporation
 * Authors: Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PINCTRL_INTEL_H
#define PINCTRL_INTEL_H

struct pinctrl_pin_desc;
struct platform_device;
struct device;

/**
 * struct intel_pingroup - Description about group of pins
 * @name: Name of the groups
 * @pins: All pins in this group
 * @npins: Number of pins in this groups
 * @mode: Native mode in which the group is muxed out @pins
 */
struct intel_pingroup {
	const char *name;
	const unsigned *pins;
	size_t npins;
	unsigned short mode;
};

/**
 * struct intel_function - Description about a function
 * @name: Name of the function
 * @groups: An array of groups for this function
 * @ngroups: Number of groups in @groups
 */
struct intel_function {
	const char *name;
	const char * const *groups;
	size_t ngroups;
};

/**
 * struct intel_community - Intel pin community description
 * @barno: MMIO BAR number where registers for this community reside
 * @padown_offset: Register offset of PAD_OWN register from @regs. If %0
 *                 then there is no support for owner.
 * @padcfglock_offset: Register offset of PADCFGLOCK from @regs. If %0 then
 *                     locking is not supported.
 * @hostown_offset: Register offset of HOSTSW_OWN from @regs. If %0 then it
 *                  is assumed that the host owns the pin (rather than
 *                  ACPI).
 * @ie_offset: Register offset of GPI_IE from @regs.
 * @pin_base: Starting pin of pins in this community
 * @gpp_size: Maximum number of pads in each group, such as PADCFGLOCK,
 *            HOSTSW_OWN,  GPI_IS, GPI_IE, etc.
 * @npins: Number of pins in this community
 * @features: Additional features supported by the hardware
 * @regs: Community specific common registers (reserved for core driver)
 * @pad_regs: Community specific pad registers (reserved for core driver)
 * @ngpps: Number of groups (hw groups) in this community (reserved for
 *         core driver)
 */
struct intel_community {
	unsigned barno;
	unsigned padown_offset;
	unsigned padcfglock_offset;
	unsigned hostown_offset;
	unsigned ie_offset;
	unsigned pin_base;
	unsigned gpp_size;
	size_t npins;
	unsigned features;
	void __iomem *regs;
	void __iomem *pad_regs;
	size_t ngpps;
};

/* Additional features supported by the hardware */
#define PINCTRL_FEATURE_DEBOUNCE	BIT(0)
#define PINCTRL_FEATURE_1K_PD		BIT(1)

#define PIN_GROUP(n, p, m)			\
	{					\
		.name = (n),			\
		.pins = (p),			\
		.npins = ARRAY_SIZE((p)),	\
		.mode = (m),			\
	}

#define FUNCTION(n, g)				\
	{					\
		.name = (n),			\
		.groups = (g),			\
		.ngroups = ARRAY_SIZE((g)),	\
	}

/**
 * struct intel_pinctrl_soc_data - Intel pin controller per-SoC configuration
 * @uid: ACPI _UID for the probe driver use if needed
 * @pins: Array if pins this pinctrl controls
 * @npins: Number of pins in the array
 * @groups: Array of pin groups
 * @ngroups: Number of groups in the array
 * @functions: Array of functions
 * @nfunctions: Number of functions in the array
 * @communities: Array of communities this pinctrl handles
 * @ncommunities: Number of communities in the array
 *
 * The @communities is used as a template by the core driver. It will make
 * copy of all communities and fill in rest of the information.
 */
struct intel_pinctrl_soc_data {
	const char *uid;
	const struct pinctrl_pin_desc *pins;
	size_t npins;
	const struct intel_pingroup *groups;
	size_t ngroups;
	const struct intel_function *functions;
	size_t nfunctions;
	const struct intel_community *communities;
	size_t ncommunities;
};

int intel_pinctrl_probe(struct platform_device *pdev,
			const struct intel_pinctrl_soc_data *soc_data);
#ifdef CONFIG_PM_SLEEP
int intel_pinctrl_suspend(struct device *dev);
int intel_pinctrl_resume(struct device *dev);
#endif

#endif /* PINCTRL_INTEL_H */
