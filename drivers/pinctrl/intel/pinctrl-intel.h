/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Core pinctrl/GPIO driver for Intel GPIO controllers
 *
 * Copyright (C) 2015, Intel Corporation
 * Authors: Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef PINCTRL_INTEL_H
#define PINCTRL_INTEL_H

#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/spinlock_types.h>

struct platform_device;
struct device;

/**
 * struct intel_pingroup - Description about group of pins
 * @grp: Generic data of the pin group (name and pins)
 * @mode: Native mode in which the group is muxed out @pins. Used if @modes is %NULL.
 * @modes: If not %NULL this will hold mode for each pin in @pins
 */
struct intel_pingroup {
	struct pingroup grp;
	unsigned short mode;
	const unsigned int *modes;
};

/**
 * struct intel_function - Description about a function
 * @func: Generic data of the pin function (name and groups of pins)
 */
struct intel_function {
	struct pinfunction func;
};

#define INTEL_PINCTRL_MAX_GPP_SIZE	32

/**
 * struct intel_padgroup - Hardware pad group information
 * @reg_num: GPI_IS register number
 * @base: Starting pin of this group
 * @size: Size of this group (maximum is %INTEL_PINCTRL_MAX_GPP_SIZE).
 * @gpio_base: Starting GPIO base of this group
 * @padown_num: PAD_OWN register number (assigned by the core driver)
 *
 * If pad groups of a community are not the same size, use this structure
 * to specify them.
 */
struct intel_padgroup {
	unsigned int reg_num;
	unsigned int base;
	unsigned int size;
	int gpio_base;
	unsigned int padown_num;
};

/**
 * enum - Special treatment for GPIO base in pad group
 *
 * @INTEL_GPIO_BASE_ZERO:	force GPIO base to be 0
 * @INTEL_GPIO_BASE_NOMAP:	no GPIO mapping should be created
 * @INTEL_GPIO_BASE_MATCH:	matches with starting pin number
 */
enum {
	INTEL_GPIO_BASE_ZERO	= -2,
	INTEL_GPIO_BASE_NOMAP	= -1,
	INTEL_GPIO_BASE_MATCH	= 0,
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
 * @is_offset: Register offset of GPI_IS from @regs.
 * @ie_offset: Register offset of GPI_IE from @regs.
 * @features: Additional features supported by the hardware
 * @pin_base: Starting pin of pins in this community
 * @npins: Number of pins in this community
 * @gpp_size: Maximum number of pads in each group, such as PADCFGLOCK,
 *            HOSTSW_OWN, GPI_IS, GPI_IE. Used when @gpps is %NULL.
 * @gpp_num_padown_regs: Number of pad registers each pad group consumes at
 *			 minimum. Used when @gpps is %NULL.
 * @gpps: Pad groups if the controller has variable size pad groups
 * @ngpps: Number of pad groups in this community
 * @pad_map: Optional non-linear mapping of the pads
 * @nirqs: Optional total number of IRQs this community can generate
 * @acpi_space_id: Optional address space ID for ACPI OpRegion handler
 * @regs: Community specific common registers (reserved for core driver)
 * @pad_regs: Community specific pad registers (reserved for core driver)
 *
 * In older Intel GPIO host controllers, this driver supports, each pad group
 * is of equal size (except the last one). In that case the driver can just
 * fill in @gpp_size and @gpp_num_padown_regs fields and let the core driver
 * to handle the rest.
 *
 * In newer Intel GPIO host controllers each pad group is of variable size,
 * so the client driver can pass custom @gpps and @ngpps instead.
 */
struct intel_community {
	unsigned int barno;
	unsigned int padown_offset;
	unsigned int padcfglock_offset;
	unsigned int hostown_offset;
	unsigned int is_offset;
	unsigned int ie_offset;
	unsigned int features;
	unsigned int pin_base;
	size_t npins;
	unsigned int gpp_size;
	unsigned int gpp_num_padown_regs;
	const struct intel_padgroup *gpps;
	size_t ngpps;
	const unsigned int *pad_map;
	unsigned short nirqs;
	unsigned short acpi_space_id;

	/* Reserved for the core driver */
	void __iomem *regs;
	void __iomem *pad_regs;
};

/* Additional features supported by the hardware */
#define PINCTRL_FEATURE_DEBOUNCE	BIT(0)
#define PINCTRL_FEATURE_1K_PD		BIT(1)
#define PINCTRL_FEATURE_GPIO_HW_INFO	BIT(2)
#define PINCTRL_FEATURE_PWM		BIT(3)
#define PINCTRL_FEATURE_BLINK		BIT(4)
#define PINCTRL_FEATURE_EXP		BIT(5)

#define __INTEL_COMMUNITY(b, s, e, g, n, gs, gn, soc)		\
	{							\
		.barno = (b),					\
		.padown_offset = soc ## _PAD_OWN,		\
		.padcfglock_offset = soc ## _PADCFGLOCK,	\
		.hostown_offset = soc ## _HOSTSW_OWN,		\
		.is_offset = soc ## _GPI_IS,			\
		.ie_offset = soc ## _GPI_IE,			\
		.gpp_size = (gs),				\
		.gpp_num_padown_regs = (gn),			\
		.pin_base = (s),				\
		.npins = ((e) - (s) + 1),			\
		.gpps = (g),					\
		.ngpps = (n),					\
	}

#define INTEL_COMMUNITY_GPPS(b, s, e, g, soc)			\
	__INTEL_COMMUNITY(b, s, e, g, ARRAY_SIZE(g), 0, 0, soc)

#define INTEL_COMMUNITY_SIZE(b, s, e, gs, gn, soc)		\
	__INTEL_COMMUNITY(b, s, e, NULL, 0, gs, gn, soc)

/**
 * PIN_GROUP - Declare a pin group
 * @n: Name of the group
 * @p: An array of pins this group consists
 * @m: Mode which the pins are put when this group is active. Can be either
 *     a single integer or an array of integers in which case mode is per
 *     pin.
 */
#define PIN_GROUP(n, p, m)								\
	{										\
		.grp = PINCTRL_PINGROUP((n), (p), ARRAY_SIZE((p))),			\
		.mode = __builtin_choose_expr(__builtin_constant_p((m)), (m), 0),	\
		.modes = __builtin_choose_expr(__builtin_constant_p((m)), NULL, (m)),	\
	}

#define FUNCTION(n, g)							\
	{								\
		.func = PINCTRL_PINFUNCTION((n), (g), ARRAY_SIZE(g)),	\
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

const struct intel_pinctrl_soc_data *intel_pinctrl_get_soc_data(struct platform_device *pdev);

struct intel_pad_context;
struct intel_community_context;

/**
 * struct intel_pinctrl_context - context to be saved during suspend-resume
 * @pads: Opaque context per pad (driver dependent)
 * @communities: Opaque context per community (driver dependent)
 */
struct intel_pinctrl_context {
	struct intel_pad_context *pads;
	struct intel_community_context *communities;
};

/**
 * struct intel_pinctrl - Intel pinctrl private structure
 * @dev: Pointer to the device structure
 * @lock: Lock to serialize register access
 * @pctldesc: Pin controller description
 * @pctldev: Pointer to the pin controller device
 * @chip: GPIO chip in this pin controller
 * @soc: SoC/PCH specific pin configuration data
 * @communities: All communities in this pin controller
 * @ncommunities: Number of communities in this pin controller
 * @context: Configuration saved over system sleep
 * @irq: pinctrl/GPIO chip irq number
 */
struct intel_pinctrl {
	struct device *dev;
	raw_spinlock_t lock;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	struct gpio_chip chip;
	const struct intel_pinctrl_soc_data *soc;
	struct intel_community *communities;
	size_t ncommunities;
	struct intel_pinctrl_context context;
	int irq;
};

int intel_pinctrl_probe_by_hid(struct platform_device *pdev);
int intel_pinctrl_probe_by_uid(struct platform_device *pdev);

#ifdef CONFIG_PM_SLEEP
int intel_pinctrl_suspend_noirq(struct device *dev);
int intel_pinctrl_resume_noirq(struct device *dev);
#endif

#define INTEL_PINCTRL_PM_OPS(_name)					\
const struct dev_pm_ops _name = {					\
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(intel_pinctrl_suspend_noirq,	\
				      intel_pinctrl_resume_noirq)	\
}

#endif /* PINCTRL_INTEL_H */
