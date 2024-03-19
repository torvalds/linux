/*
 * Generic device tree based pinctrl driver for one register per pin
 * type pinmux controllers
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <linux/platform_data/pinctrl-single.h>

#include "core.h"
#include "devicetree.h"
#include "pinconf.h"
#include "pinmux.h"

#define DRIVER_NAME			"pinctrl-single"
#define PCS_OFF_DISABLED		~0U

/**
 * struct pcs_func_vals - mux function register offset and value pair
 * @reg:	register virtual address
 * @val:	register value
 * @mask:	mask
 */
struct pcs_func_vals {
	void __iomem *reg;
	unsigned val;
	unsigned mask;
};

/**
 * struct pcs_conf_vals - pinconf parameter, pinconf register offset
 * and value, enable, disable, mask
 * @param:	config parameter
 * @val:	user input bits in the pinconf register
 * @enable:	enable bits in the pinconf register
 * @disable:	disable bits in the pinconf register
 * @mask:	mask bits in the register value
 */
struct pcs_conf_vals {
	enum pin_config_param param;
	unsigned val;
	unsigned enable;
	unsigned disable;
	unsigned mask;
};

/**
 * struct pcs_conf_type - pinconf property name, pinconf param pair
 * @name:	property name in DTS file
 * @param:	config parameter
 */
struct pcs_conf_type {
	const char *name;
	enum pin_config_param param;
};

/**
 * struct pcs_function - pinctrl function
 * @name:	pinctrl function name
 * @vals:	register and vals array
 * @nvals:	number of entries in vals array
 * @conf:	array of pin configurations
 * @nconfs:	number of pin configurations available
 * @node:	list node
 */
struct pcs_function {
	const char *name;
	struct pcs_func_vals *vals;
	unsigned nvals;
	struct pcs_conf_vals *conf;
	int nconfs;
	struct list_head node;
};

/**
 * struct pcs_gpiofunc_range - pin ranges with same mux value of gpio function
 * @offset:	offset base of pins
 * @npins:	number pins with the same mux value of gpio function
 * @gpiofunc:	mux value of gpio function
 * @node:	list node
 */
struct pcs_gpiofunc_range {
	unsigned offset;
	unsigned npins;
	unsigned gpiofunc;
	struct list_head node;
};

/**
 * struct pcs_data - wrapper for data needed by pinctrl framework
 * @pa:		pindesc array
 * @cur:	index to current element
 *
 * REVISIT: We should be able to drop this eventually by adding
 * support for registering pins individually in the pinctrl
 * framework for those drivers that don't need a static array.
 */
struct pcs_data {
	struct pinctrl_pin_desc *pa;
	int cur;
};

/**
 * struct pcs_soc_data - SoC specific settings
 * @flags:	initial SoC specific PCS_FEAT_xxx values
 * @irq:	optional interrupt for the controller
 * @irq_enable_mask:	optional SoC specific interrupt enable mask
 * @irq_status_mask:	optional SoC specific interrupt status mask
 * @rearm:	optional SoC specific wake-up rearm function
 */
struct pcs_soc_data {
	unsigned flags;
	int irq;
	unsigned irq_enable_mask;
	unsigned irq_status_mask;
	void (*rearm)(void);
};

/**
 * struct pcs_device - pinctrl device instance
 * @res:	resources
 * @base:	virtual address of the controller
 * @saved_vals: saved values for the controller
 * @size:	size of the ioremapped area
 * @dev:	device entry
 * @np:		device tree node
 * @pctl:	pin controller device
 * @flags:	mask of PCS_FEAT_xxx values
 * @missing_nr_pinctrl_cells: for legacy binding, may go away
 * @socdata:	soc specific data
 * @lock:	spinlock for register access
 * @mutex:	mutex protecting the lists
 * @width:	bits per mux register
 * @fmask:	function register mask
 * @fshift:	function register shift
 * @foff:	value to turn mux off
 * @fmax:	max number of functions in fmask
 * @bits_per_mux: number of bits per mux
 * @bits_per_pin: number of bits per pin
 * @pins:	physical pins on the SoC
 * @gpiofuncs:	list of gpio functions
 * @irqs:	list of interrupt registers
 * @chip:	chip container for this instance
 * @domain:	IRQ domain for this instance
 * @desc:	pin controller descriptor
 * @read:	register read function to use
 * @write:	register write function to use
 */
struct pcs_device {
	struct resource *res;
	void __iomem *base;
	void *saved_vals;
	unsigned size;
	struct device *dev;
	struct device_node *np;
	struct pinctrl_dev *pctl;
	unsigned flags;
#define PCS_CONTEXT_LOSS_OFF	(1 << 3)
#define PCS_QUIRK_SHARED_IRQ	(1 << 2)
#define PCS_FEAT_IRQ		(1 << 1)
#define PCS_FEAT_PINCONF	(1 << 0)
	struct property *missing_nr_pinctrl_cells;
	struct pcs_soc_data socdata;
	raw_spinlock_t lock;
	struct mutex mutex;
	unsigned width;
	unsigned fmask;
	unsigned fshift;
	unsigned foff;
	unsigned fmax;
	bool bits_per_mux;
	unsigned bits_per_pin;
	struct pcs_data pins;
	struct list_head gpiofuncs;
	struct list_head irqs;
	struct irq_chip chip;
	struct irq_domain *domain;
	struct pinctrl_desc desc;
	unsigned (*read)(void __iomem *reg);
	void (*write)(unsigned val, void __iomem *reg);
};

#define PCS_QUIRK_HAS_SHARED_IRQ	(pcs->flags & PCS_QUIRK_SHARED_IRQ)
#define PCS_HAS_IRQ		(pcs->flags & PCS_FEAT_IRQ)
#define PCS_HAS_PINCONF		(pcs->flags & PCS_FEAT_PINCONF)

static int pcs_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config);
static int pcs_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *configs, unsigned num_configs);

static enum pin_config_param pcs_bias[] = {
	PIN_CONFIG_BIAS_PULL_DOWN,
	PIN_CONFIG_BIAS_PULL_UP,
};

/*
 * This lock class tells lockdep that irqchip core that this single
 * pinctrl can be in a different category than its parents, so it won't
 * report false recursion.
 */
static struct lock_class_key pcs_lock_class;

/* Class for the IRQ request mutex */
static struct lock_class_key pcs_request_class;

/*
 * REVISIT: Reads and writes could eventually use regmap or something
 * generic. But at least on omaps, some mux registers are performance
 * critical as they may need to be remuxed every time before and after
 * idle. Adding tests for register access width for every read and
 * write like regmap is doing is not desired, and caching the registers
 * does not help in this case.
 */

static unsigned int pcs_readb(void __iomem *reg)
{
	return readb(reg);
}

static unsigned int pcs_readw(void __iomem *reg)
{
	return readw(reg);
}

static unsigned int pcs_readl(void __iomem *reg)
{
	return readl(reg);
}

static void pcs_writeb(unsigned int val, void __iomem *reg)
{
	writeb(val, reg);
}

static void pcs_writew(unsigned int val, void __iomem *reg)
{
	writew(val, reg);
}

static void pcs_writel(unsigned int val, void __iomem *reg)
{
	writel(val, reg);
}

static unsigned int pcs_pin_reg_offset_get(struct pcs_device *pcs,
					   unsigned int pin)
{
	unsigned int mux_bytes = pcs->width / BITS_PER_BYTE;

	if (pcs->bits_per_mux) {
		unsigned int pin_offset_bytes;

		pin_offset_bytes = (pcs->bits_per_pin * pin) / BITS_PER_BYTE;
		return (pin_offset_bytes / mux_bytes) * mux_bytes;
	}

	return pin * mux_bytes;
}

static unsigned int pcs_pin_shift_reg_get(struct pcs_device *pcs,
					  unsigned int pin)
{
	return (pin % (pcs->width / pcs->bits_per_pin)) * pcs->bits_per_pin;
}

static void pcs_pin_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned pin)
{
	struct pcs_device *pcs;
	unsigned int val;
	unsigned long offset;
	size_t pa;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	offset = pcs_pin_reg_offset_get(pcs, pin);
	val = pcs->read(pcs->base + offset);

	if (pcs->bits_per_mux)
		val &= pcs->fmask << pcs_pin_shift_reg_get(pcs, pin);

	pa = pcs->res->start + offset;

	seq_printf(s, "%zx %08x %s ", pa, val, DRIVER_NAME);
}

static void pcs_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	devm_kfree(pcs->dev, map);
}

static int pcs_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np_config,
				struct pinctrl_map **map, unsigned *num_maps);

static const struct pinctrl_ops pcs_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = pcs_pin_dbg_show,
	.dt_node_to_map = pcs_dt_node_to_map,
	.dt_free_map = pcs_dt_free_map,
};

static int pcs_get_function(struct pinctrl_dev *pctldev, unsigned pin,
			    struct pcs_function **func)
{
	struct pcs_device *pcs = pinctrl_dev_get_drvdata(pctldev);
	struct pin_desc *pdesc = pin_desc_get(pctldev, pin);
	const struct pinctrl_setting_mux *setting;
	struct function_desc *function;
	unsigned fselector;

	/* If pin is not described in DTS & enabled, mux_setting is NULL. */
	setting = pdesc->mux_setting;
	if (!setting)
		return -ENOTSUPP;
	fselector = setting->func;
	function = pinmux_generic_get_function(pctldev, fselector);
	*func = function->data;
	if (!(*func)) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return -ENOTSUPP;
	}
	return 0;
}

static int pcs_set_mux(struct pinctrl_dev *pctldev, unsigned fselector,
	unsigned group)
{
	struct pcs_device *pcs;
	struct function_desc *function;
	struct pcs_function *func;
	int i;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	/* If function mask is null, needn't enable it. */
	if (!pcs->fmask)
		return 0;
	function = pinmux_generic_get_function(pctldev, fselector);
	if (!function)
		return -EINVAL;
	func = function->data;
	if (!func)
		return -EINVAL;

	dev_dbg(pcs->dev, "enabling %s function%i\n",
		func->name, fselector);

	for (i = 0; i < func->nvals; i++) {
		struct pcs_func_vals *vals;
		unsigned long flags;
		unsigned val, mask;

		vals = &func->vals[i];
		raw_spin_lock_irqsave(&pcs->lock, flags);
		val = pcs->read(vals->reg);

		if (pcs->bits_per_mux)
			mask = vals->mask;
		else
			mask = pcs->fmask;

		val &= ~mask;
		val |= (vals->val & mask);
		pcs->write(val, vals->reg);
		raw_spin_unlock_irqrestore(&pcs->lock, flags);
	}

	return 0;
}

static int pcs_request_gpio(struct pinctrl_dev *pctldev,
			    struct pinctrl_gpio_range *range, unsigned pin)
{
	struct pcs_device *pcs = pinctrl_dev_get_drvdata(pctldev);
	struct pcs_gpiofunc_range *frange = NULL;
	struct list_head *pos, *tmp;
	unsigned data;

	/* If function mask is null, return directly. */
	if (!pcs->fmask)
		return -ENOTSUPP;

	list_for_each_safe(pos, tmp, &pcs->gpiofuncs) {
		u32 offset;

		frange = list_entry(pos, struct pcs_gpiofunc_range, node);
		if (pin >= frange->offset + frange->npins
			|| pin < frange->offset)
			continue;

		offset = pcs_pin_reg_offset_get(pcs, pin);

		if (pcs->bits_per_mux) {
			int pin_shift = pcs_pin_shift_reg_get(pcs, pin);

			data = pcs->read(pcs->base + offset);
			data &= ~(pcs->fmask << pin_shift);
			data |= frange->gpiofunc << pin_shift;
			pcs->write(data, pcs->base + offset);
		} else {
			data = pcs->read(pcs->base + offset);
			data &= ~pcs->fmask;
			data |= frange->gpiofunc;
			pcs->write(data, pcs->base + offset);
		}
		break;
	}
	return 0;
}

static const struct pinmux_ops pcs_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = pcs_set_mux,
	.gpio_request_enable = pcs_request_gpio,
};

/* Clear BIAS value */
static void pcs_pinconf_clear_bias(struct pinctrl_dev *pctldev, unsigned pin)
{
	unsigned long config;
	int i;
	for (i = 0; i < ARRAY_SIZE(pcs_bias); i++) {
		config = pinconf_to_config_packed(pcs_bias[i], 0);
		pcs_pinconf_set(pctldev, pin, &config, 1);
	}
}

/*
 * Check whether PIN_CONFIG_BIAS_DISABLE is valid.
 * It's depend on that PULL_DOWN & PULL_UP configs are all invalid.
 */
static bool pcs_pinconf_bias_disable(struct pinctrl_dev *pctldev, unsigned pin)
{
	unsigned long config;
	int i;

	for (i = 0; i < ARRAY_SIZE(pcs_bias); i++) {
		config = pinconf_to_config_packed(pcs_bias[i], 0);
		if (!pcs_pinconf_get(pctldev, pin, &config))
			goto out;
	}
	return true;
out:
	return false;
}

static int pcs_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *config)
{
	struct pcs_device *pcs = pinctrl_dev_get_drvdata(pctldev);
	struct pcs_function *func;
	enum pin_config_param param;
	unsigned offset = 0, data = 0, i, j, ret;

	ret = pcs_get_function(pctldev, pin, &func);
	if (ret)
		return ret;

	for (i = 0; i < func->nconfs; i++) {
		param = pinconf_to_config_param(*config);
		if (param == PIN_CONFIG_BIAS_DISABLE) {
			if (pcs_pinconf_bias_disable(pctldev, pin)) {
				*config = 0;
				return 0;
			} else {
				return -ENOTSUPP;
			}
		} else if (param != func->conf[i].param) {
			continue;
		}

		offset = pin * (pcs->width / BITS_PER_BYTE);
		data = pcs->read(pcs->base + offset) & func->conf[i].mask;
		switch (func->conf[i].param) {
		/* 4 parameters */
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if ((data != func->conf[i].enable) ||
			    (data == func->conf[i].disable))
				return -ENOTSUPP;
			*config = 0;
			break;
		/* 2 parameters */
		case PIN_CONFIG_INPUT_SCHMITT:
			for (j = 0; j < func->nconfs; j++) {
				switch (func->conf[j].param) {
				case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
					if (data != func->conf[j].enable)
						return -ENOTSUPP;
					break;
				default:
					break;
				}
			}
			*config = data;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
		case PIN_CONFIG_SLEW_RATE:
		case PIN_CONFIG_MODE_LOW_POWER:
		case PIN_CONFIG_INPUT_ENABLE:
		default:
			*config = data;
			break;
		}
		return 0;
	}
	return -ENOTSUPP;
}

static int pcs_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *configs,
				unsigned num_configs)
{
	struct pcs_device *pcs = pinctrl_dev_get_drvdata(pctldev);
	struct pcs_function *func;
	unsigned offset = 0, shift = 0, i, data, ret;
	u32 arg;
	int j;
	enum pin_config_param param;

	ret = pcs_get_function(pctldev, pin, &func);
	if (ret)
		return ret;

	for (j = 0; j < num_configs; j++) {
		param = pinconf_to_config_param(configs[j]);

		/* BIAS_DISABLE has no entry in the func->conf table */
		if (param == PIN_CONFIG_BIAS_DISABLE) {
			/* This just disables all bias entries */
			pcs_pinconf_clear_bias(pctldev, pin);
			continue;
		}

		for (i = 0; i < func->nconfs; i++) {
			if (param != func->conf[i].param)
				continue;

			offset = pin * (pcs->width / BITS_PER_BYTE);
			data = pcs->read(pcs->base + offset);
			arg = pinconf_to_config_argument(configs[j]);
			switch (param) {
			/* 2 parameters */
			case PIN_CONFIG_INPUT_SCHMITT:
			case PIN_CONFIG_DRIVE_STRENGTH:
			case PIN_CONFIG_SLEW_RATE:
			case PIN_CONFIG_MODE_LOW_POWER:
			case PIN_CONFIG_INPUT_ENABLE:
				shift = ffs(func->conf[i].mask) - 1;
				data &= ~func->conf[i].mask;
				data |= (arg << shift) & func->conf[i].mask;
				break;
			/* 4 parameters */
			case PIN_CONFIG_BIAS_PULL_DOWN:
			case PIN_CONFIG_BIAS_PULL_UP:
				if (arg)
					pcs_pinconf_clear_bias(pctldev, pin);
				fallthrough;
			case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
				data &= ~func->conf[i].mask;
				if (arg)
					data |= func->conf[i].enable;
				else
					data |= func->conf[i].disable;
				break;
			default:
				return -ENOTSUPP;
			}
			pcs->write(data, pcs->base + offset);

			break;
		}
		if (i >= func->nconfs)
			return -ENOTSUPP;
	} /* for each config */

	return 0;
}

static int pcs_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *config)
{
	const unsigned *pins;
	unsigned npins, old = 0;
	int i, ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;
	for (i = 0; i < npins; i++) {
		if (pcs_pinconf_get(pctldev, pins[i], config))
			return -ENOTSUPP;
		/* configs do not match between two pins */
		if (i && (old != *config))
			return -ENOTSUPP;
		old = *config;
	}
	return 0;
}

static int pcs_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *configs,
				unsigned num_configs)
{
	const unsigned *pins;
	unsigned npins;
	int i, ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;
	for (i = 0; i < npins; i++) {
		if (pcs_pinconf_set(pctldev, pins[i], configs, num_configs))
			return -ENOTSUPP;
	}
	return 0;
}

static void pcs_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned pin)
{
}

static void pcs_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned selector)
{
}

static void pcs_pinconf_config_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned long config)
{
	pinconf_generic_dump_config(pctldev, s, config);
}

static const struct pinconf_ops pcs_pinconf_ops = {
	.pin_config_get = pcs_pinconf_get,
	.pin_config_set = pcs_pinconf_set,
	.pin_config_group_get = pcs_pinconf_group_get,
	.pin_config_group_set = pcs_pinconf_group_set,
	.pin_config_dbg_show = pcs_pinconf_dbg_show,
	.pin_config_group_dbg_show = pcs_pinconf_group_dbg_show,
	.pin_config_config_dbg_show = pcs_pinconf_config_dbg_show,
	.is_generic = true,
};

/**
 * pcs_add_pin() - add a pin to the static per controller pin array
 * @pcs: pcs driver instance
 * @offset: register offset from base
 */
static int pcs_add_pin(struct pcs_device *pcs, unsigned int offset)
{
	struct pcs_soc_data *pcs_soc = &pcs->socdata;
	struct pinctrl_pin_desc *pin;
	int i;

	i = pcs->pins.cur;
	if (i >= pcs->desc.npins) {
		dev_err(pcs->dev, "too many pins, max %i\n",
			pcs->desc.npins);
		return -ENOMEM;
	}

	if (pcs_soc->irq_enable_mask) {
		unsigned val;

		val = pcs->read(pcs->base + offset);
		if (val & pcs_soc->irq_enable_mask) {
			dev_dbg(pcs->dev, "irq enabled at boot for pin at %lx (%x), clearing\n",
				(unsigned long)pcs->res->start + offset, val);
			val &= ~pcs_soc->irq_enable_mask;
			pcs->write(val, pcs->base + offset);
		}
	}

	pin = &pcs->pins.pa[i];
	pin->number = i;
	pcs->pins.cur++;

	return i;
}

/**
 * pcs_allocate_pin_table() - adds all the pins for the pinctrl driver
 * @pcs: pcs driver instance
 *
 * In case of errors, resources are freed in pcs_free_resources.
 *
 * If your hardware needs holes in the address space, then just set
 * up multiple driver instances.
 */
static int pcs_allocate_pin_table(struct pcs_device *pcs)
{
	int mux_bytes, nr_pins, i;

	mux_bytes = pcs->width / BITS_PER_BYTE;

	if (pcs->bits_per_mux && pcs->fmask) {
		pcs->bits_per_pin = fls(pcs->fmask);
		nr_pins = (pcs->size * BITS_PER_BYTE) / pcs->bits_per_pin;
	} else {
		nr_pins = pcs->size / mux_bytes;
	}

	dev_dbg(pcs->dev, "allocating %i pins\n", nr_pins);
	pcs->pins.pa = devm_kcalloc(pcs->dev,
				nr_pins, sizeof(*pcs->pins.pa),
				GFP_KERNEL);
	if (!pcs->pins.pa)
		return -ENOMEM;

	pcs->desc.pins = pcs->pins.pa;
	pcs->desc.npins = nr_pins;

	for (i = 0; i < pcs->desc.npins; i++) {
		unsigned offset;
		int res;

		offset = pcs_pin_reg_offset_get(pcs, i);
		res = pcs_add_pin(pcs, offset);
		if (res < 0) {
			dev_err(pcs->dev, "error adding pins: %i\n", res);
			return res;
		}
	}

	return 0;
}

/**
 * pcs_add_function() - adds a new function to the function list
 * @pcs: pcs driver instance
 * @fcn: new function allocated
 * @name: name of the function
 * @vals: array of mux register value pairs used by the function
 * @nvals: number of mux register value pairs
 * @pgnames: array of pingroup names for the function
 * @npgnames: number of pingroup names
 *
 * Caller must take care of locking.
 */
static int pcs_add_function(struct pcs_device *pcs,
			    struct pcs_function **fcn,
			    const char *name,
			    struct pcs_func_vals *vals,
			    unsigned int nvals,
			    const char **pgnames,
			    unsigned int npgnames)
{
	struct pcs_function *function;
	int selector;

	function = devm_kzalloc(pcs->dev, sizeof(*function), GFP_KERNEL);
	if (!function)
		return -ENOMEM;

	function->vals = vals;
	function->nvals = nvals;
	function->name = name;

	selector = pinmux_generic_add_function(pcs->pctl, name,
					       pgnames, npgnames,
					       function);
	if (selector < 0) {
		devm_kfree(pcs->dev, function);
		*fcn = NULL;
	} else {
		*fcn = function;
	}

	return selector;
}

/**
 * pcs_get_pin_by_offset() - get a pin index based on the register offset
 * @pcs: pcs driver instance
 * @offset: register offset from the base
 *
 * Note that this is OK as long as the pins are in a static array.
 */
static int pcs_get_pin_by_offset(struct pcs_device *pcs, unsigned offset)
{
	unsigned index;

	if (offset >= pcs->size) {
		dev_err(pcs->dev, "mux offset out of range: 0x%x (0x%x)\n",
			offset, pcs->size);
		return -EINVAL;
	}

	if (pcs->bits_per_mux)
		index = (offset * BITS_PER_BYTE) / pcs->bits_per_pin;
	else
		index = offset / (pcs->width / BITS_PER_BYTE);

	return index;
}

/*
 * check whether data matches enable bits or disable bits
 * Return value: 1 for matching enable bits, 0 for matching disable bits,
 *               and negative value for matching failure.
 */
static int pcs_config_match(unsigned data, unsigned enable, unsigned disable)
{
	int ret = -EINVAL;

	if (data == enable)
		ret = 1;
	else if (data == disable)
		ret = 0;
	return ret;
}

static void add_config(struct pcs_conf_vals **conf, enum pin_config_param param,
		       unsigned value, unsigned enable, unsigned disable,
		       unsigned mask)
{
	(*conf)->param = param;
	(*conf)->val = value;
	(*conf)->enable = enable;
	(*conf)->disable = disable;
	(*conf)->mask = mask;
	(*conf)++;
}

static void add_setting(unsigned long **setting, enum pin_config_param param,
			unsigned arg)
{
	**setting = pinconf_to_config_packed(param, arg);
	(*setting)++;
}

/* add pinconf setting with 2 parameters */
static void pcs_add_conf2(struct pcs_device *pcs, struct device_node *np,
			  const char *name, enum pin_config_param param,
			  struct pcs_conf_vals **conf, unsigned long **settings)
{
	unsigned value[2], shift;
	int ret;

	ret = of_property_read_u32_array(np, name, value, 2);
	if (ret)
		return;
	/* set value & mask */
	value[0] &= value[1];
	shift = ffs(value[1]) - 1;
	/* skip enable & disable */
	add_config(conf, param, value[0], 0, 0, value[1]);
	add_setting(settings, param, value[0] >> shift);
}

/* add pinconf setting with 4 parameters */
static void pcs_add_conf4(struct pcs_device *pcs, struct device_node *np,
			  const char *name, enum pin_config_param param,
			  struct pcs_conf_vals **conf, unsigned long **settings)
{
	unsigned value[4];
	int ret;

	/* value to set, enable, disable, mask */
	ret = of_property_read_u32_array(np, name, value, 4);
	if (ret)
		return;
	if (!value[3]) {
		dev_err(pcs->dev, "mask field of the property can't be 0\n");
		return;
	}
	value[0] &= value[3];
	value[1] &= value[3];
	value[2] &= value[3];
	ret = pcs_config_match(value[0], value[1], value[2]);
	if (ret < 0)
		dev_dbg(pcs->dev, "failed to match enable or disable bits\n");
	add_config(conf, param, value[0], value[1], value[2], value[3]);
	add_setting(settings, param, ret);
}

static int pcs_parse_pinconf(struct pcs_device *pcs, struct device_node *np,
			     struct pcs_function *func,
			     struct pinctrl_map **map)

{
	struct pinctrl_map *m = *map;
	int i = 0, nconfs = 0;
	unsigned long *settings = NULL, *s = NULL;
	struct pcs_conf_vals *conf = NULL;
	static const struct pcs_conf_type prop2[] = {
		{ "pinctrl-single,drive-strength", PIN_CONFIG_DRIVE_STRENGTH, },
		{ "pinctrl-single,slew-rate", PIN_CONFIG_SLEW_RATE, },
		{ "pinctrl-single,input-enable", PIN_CONFIG_INPUT_ENABLE, },
		{ "pinctrl-single,input-schmitt", PIN_CONFIG_INPUT_SCHMITT, },
		{ "pinctrl-single,low-power-mode", PIN_CONFIG_MODE_LOW_POWER, },
	};
	static const struct pcs_conf_type prop4[] = {
		{ "pinctrl-single,bias-pullup", PIN_CONFIG_BIAS_PULL_UP, },
		{ "pinctrl-single,bias-pulldown", PIN_CONFIG_BIAS_PULL_DOWN, },
		{ "pinctrl-single,input-schmitt-enable",
			PIN_CONFIG_INPUT_SCHMITT_ENABLE, },
	};

	/* If pinconf isn't supported, don't parse properties in below. */
	if (!PCS_HAS_PINCONF)
		return -ENOTSUPP;

	/* cacluate how much properties are supported in current node */
	for (i = 0; i < ARRAY_SIZE(prop2); i++) {
		if (of_property_present(np, prop2[i].name))
			nconfs++;
	}
	for (i = 0; i < ARRAY_SIZE(prop4); i++) {
		if (of_property_present(np, prop4[i].name))
			nconfs++;
	}
	if (!nconfs)
		return -ENOTSUPP;

	func->conf = devm_kcalloc(pcs->dev,
				  nconfs, sizeof(struct pcs_conf_vals),
				  GFP_KERNEL);
	if (!func->conf)
		return -ENOMEM;
	func->nconfs = nconfs;
	conf = &(func->conf[0]);
	m++;
	settings = devm_kcalloc(pcs->dev, nconfs, sizeof(unsigned long),
				GFP_KERNEL);
	if (!settings)
		return -ENOMEM;
	s = &settings[0];

	for (i = 0; i < ARRAY_SIZE(prop2); i++)
		pcs_add_conf2(pcs, np, prop2[i].name, prop2[i].param,
			      &conf, &s);
	for (i = 0; i < ARRAY_SIZE(prop4); i++)
		pcs_add_conf4(pcs, np, prop4[i].name, prop4[i].param,
			      &conf, &s);
	m->type = PIN_MAP_TYPE_CONFIGS_GROUP;
	m->data.configs.group_or_pin = np->name;
	m->data.configs.configs = settings;
	m->data.configs.num_configs = nconfs;
	return 0;
}

/**
 * pcs_parse_one_pinctrl_entry() - parses a device tree mux entry
 * @pcs: pinctrl driver instance
 * @np: device node of the mux entry
 * @map: map entry
 * @num_maps: number of map
 * @pgnames: pingroup names
 *
 * Note that this binding currently supports only sets of one register + value.
 *
 * Also note that this driver tries to avoid understanding pin and function
 * names because of the extra bloat they would cause especially in the case of
 * a large number of pins. This driver just sets what is specified for the board
 * in the .dts file. Further user space debugging tools can be developed to
 * decipher the pin and function names using debugfs.
 *
 * If you are concerned about the boot time, set up the static pins in
 * the bootloader, and only set up selected pins as device tree entries.
 */
static int pcs_parse_one_pinctrl_entry(struct pcs_device *pcs,
						struct device_node *np,
						struct pinctrl_map **map,
						unsigned *num_maps,
						const char **pgnames)
{
	const char *name = "pinctrl-single,pins";
	struct pcs_func_vals *vals;
	int rows, *pins, found = 0, res = -ENOMEM, i, fsel, gsel;
	struct pcs_function *function = NULL;

	rows = pinctrl_count_index_with_args(np, name);
	if (rows <= 0) {
		dev_err(pcs->dev, "Invalid number of rows: %d\n", rows);
		return -EINVAL;
	}

	vals = devm_kcalloc(pcs->dev, rows, sizeof(*vals), GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	pins = devm_kcalloc(pcs->dev, rows, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		goto free_vals;

	for (i = 0; i < rows; i++) {
		struct of_phandle_args pinctrl_spec;
		unsigned int offset;
		int pin;

		res = pinctrl_parse_index_with_args(np, name, i, &pinctrl_spec);
		if (res)
			return res;

		if (pinctrl_spec.args_count < 2 || pinctrl_spec.args_count > 3) {
			dev_err(pcs->dev, "invalid args_count for spec: %i\n",
				pinctrl_spec.args_count);
			break;
		}

		offset = pinctrl_spec.args[0];
		vals[found].reg = pcs->base + offset;

		switch (pinctrl_spec.args_count) {
		case 2:
			vals[found].val = pinctrl_spec.args[1];
			break;
		case 3:
			vals[found].val = (pinctrl_spec.args[1] | pinctrl_spec.args[2]);
			break;
		}

		dev_dbg(pcs->dev, "%pOFn index: 0x%x value: 0x%x\n",
			pinctrl_spec.np, offset, vals[found].val);

		pin = pcs_get_pin_by_offset(pcs, offset);
		if (pin < 0) {
			dev_err(pcs->dev,
				"could not add functions for %pOFn %ux\n",
				np, offset);
			break;
		}
		pins[found++] = pin;
	}

	pgnames[0] = np->name;
	mutex_lock(&pcs->mutex);
	fsel = pcs_add_function(pcs, &function, np->name, vals, found,
				pgnames, 1);
	if (fsel < 0) {
		res = fsel;
		goto free_pins;
	}

	gsel = pinctrl_generic_add_group(pcs->pctl, np->name, pins, found, pcs);
	if (gsel < 0) {
		res = gsel;
		goto free_function;
	}

	(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)->data.mux.group = np->name;
	(*map)->data.mux.function = np->name;

	if (PCS_HAS_PINCONF && function) {
		res = pcs_parse_pinconf(pcs, np, function, map);
		if (res == 0)
			*num_maps = 2;
		else if (res == -ENOTSUPP)
			*num_maps = 1;
		else
			goto free_pingroups;
	} else {
		*num_maps = 1;
	}
	mutex_unlock(&pcs->mutex);

	return 0;

free_pingroups:
	pinctrl_generic_remove_group(pcs->pctl, gsel);
	*num_maps = 1;
free_function:
	pinmux_generic_remove_function(pcs->pctl, fsel);
free_pins:
	mutex_unlock(&pcs->mutex);
	devm_kfree(pcs->dev, pins);

free_vals:
	devm_kfree(pcs->dev, vals);

	return res;
}

static int pcs_parse_bits_in_pinctrl_entry(struct pcs_device *pcs,
						struct device_node *np,
						struct pinctrl_map **map,
						unsigned *num_maps,
						const char **pgnames)
{
	const char *name = "pinctrl-single,bits";
	struct pcs_func_vals *vals;
	int rows, *pins, found = 0, res = -ENOMEM, i, fsel;
	int npins_in_row;
	struct pcs_function *function = NULL;

	rows = pinctrl_count_index_with_args(np, name);
	if (rows <= 0) {
		dev_err(pcs->dev, "Invalid number of rows: %d\n", rows);
		return -EINVAL;
	}

	if (PCS_HAS_PINCONF) {
		dev_err(pcs->dev, "pinconf not supported\n");
		return -ENOTSUPP;
	}

	npins_in_row = pcs->width / pcs->bits_per_pin;

	vals = devm_kzalloc(pcs->dev,
			    array3_size(rows, npins_in_row, sizeof(*vals)),
			    GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	pins = devm_kzalloc(pcs->dev,
			    array3_size(rows, npins_in_row, sizeof(*pins)),
			    GFP_KERNEL);
	if (!pins)
		goto free_vals;

	for (i = 0; i < rows; i++) {
		struct of_phandle_args pinctrl_spec;
		unsigned offset, val;
		unsigned mask, bit_pos, val_pos, mask_pos, submask;
		unsigned pin_num_from_lsb;
		int pin;

		res = pinctrl_parse_index_with_args(np, name, i, &pinctrl_spec);
		if (res)
			return res;

		if (pinctrl_spec.args_count < 3) {
			dev_err(pcs->dev, "invalid args_count for spec: %i\n",
				pinctrl_spec.args_count);
			break;
		}

		/* Index plus two value cells */
		offset = pinctrl_spec.args[0];
		val = pinctrl_spec.args[1];
		mask = pinctrl_spec.args[2];

		dev_dbg(pcs->dev, "%pOFn index: 0x%x value: 0x%x mask: 0x%x\n",
			pinctrl_spec.np, offset, val, mask);

		/* Parse pins in each row from LSB */
		while (mask) {
			bit_pos = __ffs(mask);
			pin_num_from_lsb = bit_pos / pcs->bits_per_pin;
			mask_pos = ((pcs->fmask) << bit_pos);
			val_pos = val & mask_pos;
			submask = mask & mask_pos;

			if ((mask & mask_pos) == 0) {
				dev_err(pcs->dev,
					"Invalid mask for %pOFn at 0x%x\n",
					np, offset);
				break;
			}

			mask &= ~mask_pos;

			if (submask != mask_pos) {
				dev_warn(pcs->dev,
						"Invalid submask 0x%x for %pOFn at 0x%x\n",
						submask, np, offset);
				continue;
			}

			vals[found].mask = submask;
			vals[found].reg = pcs->base + offset;
			vals[found].val = val_pos;

			pin = pcs_get_pin_by_offset(pcs, offset);
			if (pin < 0) {
				dev_err(pcs->dev,
					"could not add functions for %pOFn %ux\n",
					np, offset);
				break;
			}
			pins[found++] = pin + pin_num_from_lsb;
		}
	}

	pgnames[0] = np->name;
	mutex_lock(&pcs->mutex);
	fsel = pcs_add_function(pcs, &function, np->name, vals, found,
				pgnames, 1);
	if (fsel < 0) {
		res = fsel;
		goto free_pins;
	}

	res = pinctrl_generic_add_group(pcs->pctl, np->name, pins, found, pcs);
	if (res < 0)
		goto free_function;

	(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)->data.mux.group = np->name;
	(*map)->data.mux.function = np->name;

	*num_maps = 1;
	mutex_unlock(&pcs->mutex);

	return 0;

free_function:
	pinmux_generic_remove_function(pcs->pctl, fsel);
free_pins:
	mutex_unlock(&pcs->mutex);
	devm_kfree(pcs->dev, pins);

free_vals:
	devm_kfree(pcs->dev, vals);

	return res;
}
/**
 * pcs_dt_node_to_map() - allocates and parses pinctrl maps
 * @pctldev: pinctrl instance
 * @np_config: device tree pinmux entry
 * @map: array of map entries
 * @num_maps: number of maps
 */
static int pcs_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np_config,
				struct pinctrl_map **map, unsigned *num_maps)
{
	struct pcs_device *pcs;
	const char **pgnames;
	int ret;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	/* create 2 maps. One is for pinmux, and the other is for pinconf. */
	*map = devm_kcalloc(pcs->dev, 2, sizeof(**map), GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	*num_maps = 0;

	pgnames = devm_kzalloc(pcs->dev, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames) {
		ret = -ENOMEM;
		goto free_map;
	}

	if (pcs->bits_per_mux) {
		ret = pcs_parse_bits_in_pinctrl_entry(pcs, np_config, map,
				num_maps, pgnames);
		if (ret < 0) {
			dev_err(pcs->dev, "no pins entries for %pOFn\n",
				np_config);
			goto free_pgnames;
		}
	} else {
		ret = pcs_parse_one_pinctrl_entry(pcs, np_config, map,
				num_maps, pgnames);
		if (ret < 0) {
			dev_err(pcs->dev, "no pins entries for %pOFn\n",
				np_config);
			goto free_pgnames;
		}
	}

	return 0;

free_pgnames:
	devm_kfree(pcs->dev, pgnames);
free_map:
	devm_kfree(pcs->dev, *map);

	return ret;
}

/**
 * pcs_irq_free() - free interrupt
 * @pcs: pcs driver instance
 */
static void pcs_irq_free(struct pcs_device *pcs)
{
	struct pcs_soc_data *pcs_soc = &pcs->socdata;

	if (pcs_soc->irq < 0)
		return;

	if (pcs->domain)
		irq_domain_remove(pcs->domain);

	if (PCS_QUIRK_HAS_SHARED_IRQ)
		free_irq(pcs_soc->irq, pcs_soc);
	else
		irq_set_chained_handler(pcs_soc->irq, NULL);
}

/**
 * pcs_free_resources() - free memory used by this driver
 * @pcs: pcs driver instance
 */
static void pcs_free_resources(struct pcs_device *pcs)
{
	pcs_irq_free(pcs);
	pinctrl_unregister(pcs->pctl);

#if IS_BUILTIN(CONFIG_PINCTRL_SINGLE)
	if (pcs->missing_nr_pinctrl_cells)
		of_remove_property(pcs->np, pcs->missing_nr_pinctrl_cells);
#endif
}

static int pcs_add_gpio_func(struct device_node *node, struct pcs_device *pcs)
{
	const char *propname = "pinctrl-single,gpio-range";
	const char *cellname = "#pinctrl-single,gpio-range-cells";
	struct of_phandle_args gpiospec;
	struct pcs_gpiofunc_range *range;
	int ret, i;

	for (i = 0; ; i++) {
		ret = of_parse_phandle_with_args(node, propname, cellname,
						 i, &gpiospec);
		/* Do not treat it as error. Only treat it as end condition. */
		if (ret) {
			ret = 0;
			break;
		}
		range = devm_kzalloc(pcs->dev, sizeof(*range), GFP_KERNEL);
		if (!range) {
			ret = -ENOMEM;
			break;
		}
		range->offset = gpiospec.args[0];
		range->npins = gpiospec.args[1];
		range->gpiofunc = gpiospec.args[2];
		mutex_lock(&pcs->mutex);
		list_add_tail(&range->node, &pcs->gpiofuncs);
		mutex_unlock(&pcs->mutex);
	}
	return ret;
}

/**
 * struct pcs_interrupt
 * @reg:	virtual address of interrupt register
 * @hwirq:	hardware irq number
 * @irq:	virtual irq number
 * @node:	list node
 */
struct pcs_interrupt {
	void __iomem *reg;
	irq_hw_number_t hwirq;
	unsigned int irq;
	struct list_head node;
};

/**
 * pcs_irq_set() - enables or disables an interrupt
 * @pcs_soc: SoC specific settings
 * @irq: interrupt
 * @enable: enable or disable the interrupt
 *
 * Note that this currently assumes one interrupt per pinctrl
 * register that is typically used for wake-up events.
 */
static inline void pcs_irq_set(struct pcs_soc_data *pcs_soc,
			       int irq, const bool enable)
{
	struct pcs_device *pcs;
	struct list_head *pos;
	unsigned mask;

	pcs = container_of(pcs_soc, struct pcs_device, socdata);
	list_for_each(pos, &pcs->irqs) {
		struct pcs_interrupt *pcswi;
		unsigned soc_mask;

		pcswi = list_entry(pos, struct pcs_interrupt, node);
		if (irq != pcswi->irq)
			continue;

		soc_mask = pcs_soc->irq_enable_mask;
		raw_spin_lock(&pcs->lock);
		mask = pcs->read(pcswi->reg);
		if (enable)
			mask |= soc_mask;
		else
			mask &= ~soc_mask;
		pcs->write(mask, pcswi->reg);

		/* flush posted write */
		mask = pcs->read(pcswi->reg);
		raw_spin_unlock(&pcs->lock);
	}

	if (pcs_soc->rearm)
		pcs_soc->rearm();
}

/**
 * pcs_irq_mask() - mask pinctrl interrupt
 * @d: interrupt data
 */
static void pcs_irq_mask(struct irq_data *d)
{
	struct pcs_soc_data *pcs_soc = irq_data_get_irq_chip_data(d);

	pcs_irq_set(pcs_soc, d->irq, false);
}

/**
 * pcs_irq_unmask() - unmask pinctrl interrupt
 * @d: interrupt data
 */
static void pcs_irq_unmask(struct irq_data *d)
{
	struct pcs_soc_data *pcs_soc = irq_data_get_irq_chip_data(d);

	pcs_irq_set(pcs_soc, d->irq, true);
}

/**
 * pcs_irq_set_wake() - toggle the suspend and resume wake up
 * @d: interrupt data
 * @state: wake-up state
 *
 * Note that this should be called only for suspend and resume.
 * For runtime PM, the wake-up events should be enabled by default.
 */
static int pcs_irq_set_wake(struct irq_data *d, unsigned int state)
{
	if (state)
		pcs_irq_unmask(d);
	else
		pcs_irq_mask(d);

	return 0;
}

/**
 * pcs_irq_handle() - common interrupt handler
 * @pcs_soc: SoC specific settings
 *
 * Note that this currently assumes we have one interrupt bit per
 * mux register. This interrupt is typically used for wake-up events.
 * For more complex interrupts different handlers can be specified.
 */
static int pcs_irq_handle(struct pcs_soc_data *pcs_soc)
{
	struct pcs_device *pcs;
	struct list_head *pos;
	int count = 0;

	pcs = container_of(pcs_soc, struct pcs_device, socdata);
	list_for_each(pos, &pcs->irqs) {
		struct pcs_interrupt *pcswi;
		unsigned mask;

		pcswi = list_entry(pos, struct pcs_interrupt, node);
		raw_spin_lock(&pcs->lock);
		mask = pcs->read(pcswi->reg);
		raw_spin_unlock(&pcs->lock);
		if (mask & pcs_soc->irq_status_mask) {
			generic_handle_domain_irq(pcs->domain,
						  pcswi->hwirq);
			count++;
		}
	}

	return count;
}

/**
 * pcs_irq_handler() - handler for the shared interrupt case
 * @irq: interrupt
 * @d: data
 *
 * Use this for cases where multiple instances of
 * pinctrl-single share a single interrupt like on omaps.
 */
static irqreturn_t pcs_irq_handler(int irq, void *d)
{
	struct pcs_soc_data *pcs_soc = d;

	return pcs_irq_handle(pcs_soc) ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * pcs_irq_chain_handler() - handler for the dedicated chained interrupt case
 * @desc: interrupt descriptor
 *
 * Use this if you have a separate interrupt for each
 * pinctrl-single instance.
 */
static void pcs_irq_chain_handler(struct irq_desc *desc)
{
	struct pcs_soc_data *pcs_soc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip;

	chip = irq_desc_get_chip(desc);
	chained_irq_enter(chip, desc);
	pcs_irq_handle(pcs_soc);
	/* REVISIT: export and add handle_bad_irq(irq, desc)? */
	chained_irq_exit(chip, desc);
}

static int pcs_irqdomain_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct pcs_soc_data *pcs_soc = d->host_data;
	struct pcs_device *pcs;
	struct pcs_interrupt *pcswi;

	pcs = container_of(pcs_soc, struct pcs_device, socdata);
	pcswi = devm_kzalloc(pcs->dev, sizeof(*pcswi), GFP_KERNEL);
	if (!pcswi)
		return -ENOMEM;

	pcswi->reg = pcs->base + hwirq;
	pcswi->hwirq = hwirq;
	pcswi->irq = irq;

	mutex_lock(&pcs->mutex);
	list_add_tail(&pcswi->node, &pcs->irqs);
	mutex_unlock(&pcs->mutex);

	irq_set_chip_data(irq, pcs_soc);
	irq_set_chip_and_handler(irq, &pcs->chip,
				 handle_level_irq);
	irq_set_lockdep_class(irq, &pcs_lock_class, &pcs_request_class);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops pcs_irqdomain_ops = {
	.map = pcs_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

/**
 * pcs_irq_init_chained_handler() - set up a chained interrupt handler
 * @pcs: pcs driver instance
 * @np: device node pointer
 */
static int pcs_irq_init_chained_handler(struct pcs_device *pcs,
					struct device_node *np)
{
	struct pcs_soc_data *pcs_soc = &pcs->socdata;
	const char *name = "pinctrl";
	int num_irqs;

	if (!pcs_soc->irq_enable_mask ||
	    !pcs_soc->irq_status_mask) {
		pcs_soc->irq = -1;
		return -EINVAL;
	}

	INIT_LIST_HEAD(&pcs->irqs);
	pcs->chip.name = name;
	pcs->chip.irq_ack = pcs_irq_mask;
	pcs->chip.irq_mask = pcs_irq_mask;
	pcs->chip.irq_unmask = pcs_irq_unmask;
	pcs->chip.irq_set_wake = pcs_irq_set_wake;

	if (PCS_QUIRK_HAS_SHARED_IRQ) {
		int res;

		res = request_irq(pcs_soc->irq, pcs_irq_handler,
				  IRQF_SHARED | IRQF_NO_SUSPEND |
				  IRQF_NO_THREAD,
				  name, pcs_soc);
		if (res) {
			pcs_soc->irq = -1;
			return res;
		}
	} else {
		irq_set_chained_handler_and_data(pcs_soc->irq,
						 pcs_irq_chain_handler,
						 pcs_soc);
	}

	/*
	 * We can use the register offset as the hardirq
	 * number as irq_domain_add_simple maps them lazily.
	 * This way we can easily support more than one
	 * interrupt per function if needed.
	 */
	num_irqs = pcs->size;

	pcs->domain = irq_domain_add_simple(np, num_irqs, 0,
					    &pcs_irqdomain_ops,
					    pcs_soc);
	if (!pcs->domain) {
		irq_set_chained_handler(pcs_soc->irq, NULL);
		return -EINVAL;
	}

	return 0;
}

static int pcs_save_context(struct pcs_device *pcs)
{
	int i, mux_bytes;
	u64 *regsl;
	u32 *regsw;
	u16 *regshw;

	mux_bytes = pcs->width / BITS_PER_BYTE;

	if (!pcs->saved_vals) {
		pcs->saved_vals = devm_kzalloc(pcs->dev, pcs->size, GFP_ATOMIC);
		if (!pcs->saved_vals)
			return -ENOMEM;
	}

	switch (pcs->width) {
	case 64:
		regsl = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			*regsl++ = pcs->read(pcs->base + i);
		break;
	case 32:
		regsw = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			*regsw++ = pcs->read(pcs->base + i);
		break;
	case 16:
		regshw = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			*regshw++ = pcs->read(pcs->base + i);
		break;
	}

	return 0;
}

static void pcs_restore_context(struct pcs_device *pcs)
{
	int i, mux_bytes;
	u64 *regsl;
	u32 *regsw;
	u16 *regshw;

	mux_bytes = pcs->width / BITS_PER_BYTE;

	switch (pcs->width) {
	case 64:
		regsl = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			pcs->write(*regsl++, pcs->base + i);
		break;
	case 32:
		regsw = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			pcs->write(*regsw++, pcs->base + i);
		break;
	case 16:
		regshw = pcs->saved_vals;
		for (i = 0; i < pcs->size; i += mux_bytes)
			pcs->write(*regshw++, pcs->base + i);
		break;
	}
}

static int pinctrl_single_suspend_noirq(struct device *dev)
{
	struct pcs_device *pcs = dev_get_drvdata(dev);

	if (pcs->flags & PCS_CONTEXT_LOSS_OFF) {
		int ret;

		ret = pcs_save_context(pcs);
		if (ret < 0)
			return ret;
	}

	return pinctrl_force_sleep(pcs->pctl);
}

static int pinctrl_single_resume_noirq(struct device *dev)
{
	struct pcs_device *pcs = dev_get_drvdata(dev);

	if (pcs->flags & PCS_CONTEXT_LOSS_OFF)
		pcs_restore_context(pcs);

	return pinctrl_force_default(pcs->pctl);
}

static DEFINE_NOIRQ_DEV_PM_OPS(pinctrl_single_pm_ops,
			       pinctrl_single_suspend_noirq,
			       pinctrl_single_resume_noirq);

/**
 * pcs_quirk_missing_pinctrl_cells - handle legacy binding
 * @pcs: pinctrl driver instance
 * @np: device tree node
 * @cells: number of cells
 *
 * Handle legacy binding with no #pinctrl-cells. This should be
 * always two pinctrl-single,bit-per-mux and one for others.
 * At some point we may want to consider removing this.
 */
static int pcs_quirk_missing_pinctrl_cells(struct pcs_device *pcs,
					   struct device_node *np,
					   int cells)
{
	struct property *p;
	const char *name = "#pinctrl-cells";
	int error;
	u32 val;

	error = of_property_read_u32(np, name, &val);
	if (!error)
		return 0;

	dev_warn(pcs->dev, "please update dts to use %s = <%i>\n",
		 name, cells);

	p = devm_kzalloc(pcs->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->length = sizeof(__be32);
	p->value = devm_kzalloc(pcs->dev, sizeof(__be32), GFP_KERNEL);
	if (!p->value)
		return -ENOMEM;
	*(__be32 *)p->value = cpu_to_be32(cells);

	p->name = devm_kstrdup(pcs->dev, name, GFP_KERNEL);
	if (!p->name)
		return -ENOMEM;

	pcs->missing_nr_pinctrl_cells = p;

#if IS_BUILTIN(CONFIG_PINCTRL_SINGLE)
	error = of_add_property(np, pcs->missing_nr_pinctrl_cells);
#endif

	return error;
}

static int pcs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct pcs_pdata *pdata;
	struct resource *res;
	struct pcs_device *pcs;
	const struct pcs_soc_data *soc;
	int ret;

	soc = of_device_get_match_data(&pdev->dev);
	if (WARN_ON(!soc))
		return -EINVAL;

	pcs = devm_kzalloc(&pdev->dev, sizeof(*pcs), GFP_KERNEL);
	if (!pcs)
		return -ENOMEM;

	pcs->dev = &pdev->dev;
	pcs->np = np;
	raw_spin_lock_init(&pcs->lock);
	mutex_init(&pcs->mutex);
	INIT_LIST_HEAD(&pcs->gpiofuncs);
	pcs->flags = soc->flags;
	memcpy(&pcs->socdata, soc, sizeof(*soc));

	ret = of_property_read_u32(np, "pinctrl-single,register-width",
				   &pcs->width);
	if (ret) {
		dev_err(pcs->dev, "register width not specified\n");

		return ret;
	}

	ret = of_property_read_u32(np, "pinctrl-single,function-mask",
				   &pcs->fmask);
	if (!ret) {
		pcs->fshift = __ffs(pcs->fmask);
		pcs->fmax = pcs->fmask >> pcs->fshift;
	} else {
		/* If mask property doesn't exist, function mux is invalid. */
		pcs->fmask = 0;
		pcs->fshift = 0;
		pcs->fmax = 0;
	}

	ret = of_property_read_u32(np, "pinctrl-single,function-off",
					&pcs->foff);
	if (ret)
		pcs->foff = PCS_OFF_DISABLED;

	pcs->bits_per_mux = of_property_read_bool(np,
						  "pinctrl-single,bit-per-mux");
	ret = pcs_quirk_missing_pinctrl_cells(pcs, np,
					      pcs->bits_per_mux ? 2 : 1);
	if (ret) {
		dev_err(&pdev->dev, "unable to patch #pinctrl-cells\n");

		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(pcs->dev, "could not get resource\n");
		return -ENODEV;
	}

	pcs->res = devm_request_mem_region(pcs->dev, res->start,
			resource_size(res), DRIVER_NAME);
	if (!pcs->res) {
		dev_err(pcs->dev, "could not get mem_region\n");
		return -EBUSY;
	}

	pcs->size = resource_size(pcs->res);
	pcs->base = devm_ioremap(pcs->dev, pcs->res->start, pcs->size);
	if (!pcs->base) {
		dev_err(pcs->dev, "could not ioremap\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, pcs);

	switch (pcs->width) {
	case 8:
		pcs->read = pcs_readb;
		pcs->write = pcs_writeb;
		break;
	case 16:
		pcs->read = pcs_readw;
		pcs->write = pcs_writew;
		break;
	case 32:
		pcs->read = pcs_readl;
		pcs->write = pcs_writel;
		break;
	default:
		break;
	}

	pcs->desc.name = DRIVER_NAME;
	pcs->desc.pctlops = &pcs_pinctrl_ops;
	pcs->desc.pmxops = &pcs_pinmux_ops;
	if (PCS_HAS_PINCONF)
		pcs->desc.confops = &pcs_pinconf_ops;
	pcs->desc.owner = THIS_MODULE;

	ret = pcs_allocate_pin_table(pcs);
	if (ret < 0)
		goto free;

	ret = pinctrl_register_and_init(&pcs->desc, pcs->dev, pcs, &pcs->pctl);
	if (ret) {
		dev_err(pcs->dev, "could not register single pinctrl driver\n");
		goto free;
	}

	ret = pcs_add_gpio_func(np, pcs);
	if (ret < 0)
		goto free;

	pcs->socdata.irq = irq_of_parse_and_map(np, 0);
	if (pcs->socdata.irq)
		pcs->flags |= PCS_FEAT_IRQ;

	/* We still need auxdata for some omaps for PRM interrupts */
	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		if (pdata->rearm)
			pcs->socdata.rearm = pdata->rearm;
		if (pdata->irq) {
			pcs->socdata.irq = pdata->irq;
			pcs->flags |= PCS_FEAT_IRQ;
		}
	}

	if (PCS_HAS_IRQ) {
		ret = pcs_irq_init_chained_handler(pcs, np);
		if (ret < 0)
			dev_warn(pcs->dev, "initialized with no interrupts\n");
	}

	dev_info(pcs->dev, "%i pins, size %u\n", pcs->desc.npins, pcs->size);

	return pinctrl_enable(pcs->pctl);

free:
	pcs_free_resources(pcs);

	return ret;
}

static void pcs_remove(struct platform_device *pdev)
{
	struct pcs_device *pcs = platform_get_drvdata(pdev);

	pcs_free_resources(pcs);
}

static const struct pcs_soc_data pinctrl_single_omap_wkup = {
	.flags = PCS_QUIRK_SHARED_IRQ,
	.irq_enable_mask = (1 << 14),	/* OMAP_WAKEUP_EN */
	.irq_status_mask = (1 << 15),	/* OMAP_WAKEUP_EVENT */
};

static const struct pcs_soc_data pinctrl_single_dra7 = {
	.irq_enable_mask = (1 << 24),	/* WAKEUPENABLE */
	.irq_status_mask = (1 << 25),	/* WAKEUPEVENT */
};

static const struct pcs_soc_data pinctrl_single_am437x = {
	.flags = PCS_QUIRK_SHARED_IRQ | PCS_CONTEXT_LOSS_OFF,
	.irq_enable_mask = (1 << 29),   /* OMAP_WAKEUP_EN */
	.irq_status_mask = (1 << 30),   /* OMAP_WAKEUP_EVENT */
};

static const struct pcs_soc_data pinctrl_single_am654 = {
	.flags = PCS_QUIRK_SHARED_IRQ | PCS_CONTEXT_LOSS_OFF,
	.irq_enable_mask = (1 << 29),   /* WKUP_EN */
	.irq_status_mask = (1 << 30),   /* WKUP_EVT */
};

static const struct pcs_soc_data pinctrl_single_j7200 = {
	.flags = PCS_CONTEXT_LOSS_OFF,
};

static const struct pcs_soc_data pinctrl_single = {
};

static const struct pcs_soc_data pinconf_single = {
	.flags = PCS_FEAT_PINCONF,
};

static const struct of_device_id pcs_of_match[] = {
	{ .compatible = "ti,am437-padconf", .data = &pinctrl_single_am437x },
	{ .compatible = "ti,am654-padconf", .data = &pinctrl_single_am654 },
	{ .compatible = "ti,dra7-padconf", .data = &pinctrl_single_dra7 },
	{ .compatible = "ti,omap3-padconf", .data = &pinctrl_single_omap_wkup },
	{ .compatible = "ti,omap4-padconf", .data = &pinctrl_single_omap_wkup },
	{ .compatible = "ti,omap5-padconf", .data = &pinctrl_single_omap_wkup },
	{ .compatible = "ti,j7200-padconf", .data = &pinctrl_single_j7200 },
	{ .compatible = "pinctrl-single", .data = &pinctrl_single },
	{ .compatible = "pinconf-single", .data = &pinconf_single },
	{ },
};
MODULE_DEVICE_TABLE(of, pcs_of_match);

static struct platform_driver pcs_driver = {
	.probe		= pcs_probe,
	.remove_new	= pcs_remove,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= pcs_of_match,
		.pm = pm_sleep_ptr(&pinctrl_single_pm_ops),
	},
};

module_platform_driver(pcs_driver);

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("One-register-per-pin type device tree based pinctrl driver");
MODULE_LICENSE("GPL v2");
