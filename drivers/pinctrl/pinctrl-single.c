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
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/list.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"

#define DRIVER_NAME			"pinctrl-single"
#define PCS_MUX_PINS_NAME		"pinctrl-single,pins"
#define PCS_MUX_BITS_NAME		"pinctrl-single,bits"
#define PCS_REG_NAME_LEN		((sizeof(unsigned long) * 2) + 1)
#define PCS_OFF_DISABLED		~0U
#define PCS_MAX_GPIO_VALUES		2

/**
 * struct pcs_pingroup - pingroups for a function
 * @np:		pingroup device node pointer
 * @name:	pingroup name
 * @gpins:	array of the pins in the group
 * @ngpins:	number of pins in the group
 * @node:	list node
 */
struct pcs_pingroup {
	struct device_node *np;
	const char *name;
	int *gpins;
	int ngpins;
	struct list_head node;
};

/**
 * struct pcs_func_vals - mux function register offset and value pair
 * @reg:	register virtual address
 * @val:	register value
 */
struct pcs_func_vals {
	void __iomem *reg;
	unsigned val;
	unsigned mask;
};

/**
 * struct pcs_function - pinctrl function
 * @name:	pinctrl function name
 * @vals:	register and vals array
 * @nvals:	number of entries in vals array
 * @pgnames:	array of pingroup names the function uses
 * @npgnames:	number of pingroup names the function uses
 * @node:	list node
 */
struct pcs_function {
	const char *name;
	struct pcs_func_vals *vals;
	unsigned nvals;
	const char **pgnames;
	int npgnames;
	struct list_head node;
};

/**
 * struct pcs_gpio_range - pinctrl gpio range
 * @range:	subrange of the GPIO number space
 * @gpio_func:	gpio function value in the pinmux register
 */
struct pcs_gpio_range {
	struct pinctrl_gpio_range range;
	int gpio_func;
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
 * struct pcs_name - register name for a pin
 * @name:	name of the pinctrl register
 *
 * REVISIT: We may want to make names optional in the pinctrl
 * framework as some drivers may not care about pin names to
 * avoid kernel bloat. The pin names can be deciphered by user
 * space tools using debugfs based on the register address and
 * SoC packaging information.
 */
struct pcs_name {
	char name[PCS_REG_NAME_LEN];
};

/**
 * struct pcs_device - pinctrl device instance
 * @res:	resources
 * @base:	virtual address of the controller
 * @size:	size of the ioremapped area
 * @dev:	device entry
 * @pctl:	pin controller device
 * @mutex:	mutex protecting the lists
 * @width:	bits per mux register
 * @fmask:	function register mask
 * @fshift:	function register shift
 * @foff:	value to turn mux off
 * @fmax:	max number of functions in fmask
 * @names:	array of register names for pins
 * @pins:	physical pins on the SoC
 * @pgtree:	pingroup index radix tree
 * @ftree:	function index radix tree
 * @pingroups:	list of pingroups
 * @functions:	list of functions
 * @ngroups:	number of pingroups
 * @nfuncs:	number of functions
 * @desc:	pin controller descriptor
 * @read:	register read function to use
 * @write:	register write function to use
 */
struct pcs_device {
	struct resource *res;
	void __iomem *base;
	unsigned size;
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct mutex mutex;
	unsigned width;
	unsigned fmask;
	unsigned fshift;
	unsigned foff;
	unsigned fmax;
	bool bits_per_mux;
	struct pcs_name *names;
	struct pcs_data pins;
	struct radix_tree_root pgtree;
	struct radix_tree_root ftree;
	struct list_head pingroups;
	struct list_head functions;
	unsigned ngroups;
	unsigned nfuncs;
	struct pinctrl_desc desc;
	unsigned (*read)(void __iomem *reg);
	void (*write)(unsigned val, void __iomem *reg);
};

/*
 * REVISIT: Reads and writes could eventually use regmap or something
 * generic. But at least on omaps, some mux registers are performance
 * critical as they may need to be remuxed every time before and after
 * idle. Adding tests for register access width for every read and
 * write like regmap is doing is not desired, and caching the registers
 * does not help in this case.
 */

static unsigned __maybe_unused pcs_readb(void __iomem *reg)
{
	return readb(reg);
}

static unsigned __maybe_unused pcs_readw(void __iomem *reg)
{
	return readw(reg);
}

static unsigned __maybe_unused pcs_readl(void __iomem *reg)
{
	return readl(reg);
}

static void __maybe_unused pcs_writeb(unsigned val, void __iomem *reg)
{
	writeb(val, reg);
}

static void __maybe_unused pcs_writew(unsigned val, void __iomem *reg)
{
	writew(val, reg);
}

static void __maybe_unused pcs_writel(unsigned val, void __iomem *reg)
{
	writel(val, reg);
}

static int pcs_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	return pcs->ngroups;
}

static const char *pcs_get_group_name(struct pinctrl_dev *pctldev,
					unsigned gselector)
{
	struct pcs_device *pcs;
	struct pcs_pingroup *group;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	group = radix_tree_lookup(&pcs->pgtree, gselector);
	if (!group) {
		dev_err(pcs->dev, "%s could not find pingroup%i\n",
			__func__, gselector);
		return NULL;
	}

	return group->name;
}

static int pcs_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned gselector,
					const unsigned **pins,
					unsigned *npins)
{
	struct pcs_device *pcs;
	struct pcs_pingroup *group;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	group = radix_tree_lookup(&pcs->pgtree, gselector);
	if (!group) {
		dev_err(pcs->dev, "%s could not find pingroup%i\n",
			__func__, gselector);
		return -EINVAL;
	}

	*pins = group->gpins;
	*npins = group->ngpins;

	return 0;
}

static void pcs_pin_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned pin)
{
	struct pcs_device *pcs;
	unsigned val, mux_bytes;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	mux_bytes = pcs->width / BITS_PER_BYTE;
	val = pcs->read(pcs->base + pin * mux_bytes);

	seq_printf(s, "%08x %s " , val, DRIVER_NAME);
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

static struct pinctrl_ops pcs_pinctrl_ops = {
	.get_groups_count = pcs_get_groups_count,
	.get_group_name = pcs_get_group_name,
	.get_group_pins = pcs_get_group_pins,
	.pin_dbg_show = pcs_pin_dbg_show,
	.dt_node_to_map = pcs_dt_node_to_map,
	.dt_free_map = pcs_dt_free_map,
};

static int pcs_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	return pcs->nfuncs;
}

static const char *pcs_get_function_name(struct pinctrl_dev *pctldev,
						unsigned fselector)
{
	struct pcs_device *pcs;
	struct pcs_function *func;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return NULL;
	}

	return func->name;
}

static int pcs_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned fselector,
					const char * const **groups,
					unsigned * const ngroups)
{
	struct pcs_device *pcs;
	struct pcs_function *func;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return -EINVAL;
	}
	*groups = func->pgnames;
	*ngroups = func->npgnames;

	return 0;
}

static int pcs_enable(struct pinctrl_dev *pctldev, unsigned fselector,
	unsigned group)
{
	struct pcs_device *pcs;
	struct pcs_function *func;
	int i;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func)
		return -EINVAL;

	dev_dbg(pcs->dev, "enabling %s function%i\n",
		func->name, fselector);

	for (i = 0; i < func->nvals; i++) {
		struct pcs_func_vals *vals;
		unsigned val, mask;

		vals = &func->vals[i];
		val = pcs->read(vals->reg);
		if (!vals->mask)
			mask = pcs->fmask;
		else
			mask = pcs->fmask & vals->mask;

		val &= ~mask;
		val |= (vals->val & mask);
		pcs->write(val, vals->reg);
	}

	return 0;
}

static void pcs_disable(struct pinctrl_dev *pctldev, unsigned fselector,
					unsigned group)
{
	struct pcs_device *pcs;
	struct pcs_function *func;
	int i;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return;
	}

	/*
	 * Ignore disable if function-off is not specified. Some hardware
	 * does not have clearly defined disable function. For pin specific
	 * off modes, you can use alternate named states as described in
	 * pinctrl-bindings.txt.
	 */
	if (pcs->foff == PCS_OFF_DISABLED) {
		dev_dbg(pcs->dev, "ignoring disable for %s function%i\n",
			func->name, fselector);
		return;
	}

	dev_dbg(pcs->dev, "disabling function%i %s\n",
		fselector, func->name);

	for (i = 0; i < func->nvals; i++) {
		struct pcs_func_vals *vals;
		unsigned val;

		vals = &func->vals[i];
		val = pcs->read(vals->reg);
		val &= ~pcs->fmask;
		val |= pcs->foff << pcs->fshift;
		pcs->write(val, vals->reg);
	}
}

static int pcs_request_gpio(struct pinctrl_dev *pctldev,
			    struct pinctrl_gpio_range *range, unsigned pin)
{
	struct pcs_device *pcs = pinctrl_dev_get_drvdata(pctldev);
	struct pcs_gpio_range *gpio = NULL;
	int end, mux_bytes;
	unsigned data;

	gpio = container_of(range, struct pcs_gpio_range, range);
	end = range->pin_base + range->npins - 1;
	if (pin < range->pin_base || pin > end) {
		dev_err(pctldev->dev,
			"pin %d isn't in the range of %d to %d\n",
			pin, range->pin_base, end);
		return -EINVAL;
	}
	mux_bytes = pcs->width / BITS_PER_BYTE;
	data = pcs->read(pcs->base + pin * mux_bytes) & ~pcs->fmask;
	data |= gpio->gpio_func;
	pcs->write(data, pcs->base + pin * mux_bytes);
	return 0;
}

static struct pinmux_ops pcs_pinmux_ops = {
	.get_functions_count = pcs_get_functions_count,
	.get_function_name = pcs_get_function_name,
	.get_function_groups = pcs_get_function_groups,
	.enable = pcs_enable,
	.disable = pcs_disable,
	.gpio_request_enable = pcs_request_gpio,
};

static int pcs_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long config)
{
	return -ENOTSUPP;
}

static void pcs_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned offset)
{
}

static void pcs_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned selector)
{
}

static struct pinconf_ops pcs_pinconf_ops = {
	.pin_config_get = pcs_pinconf_get,
	.pin_config_set = pcs_pinconf_set,
	.pin_config_group_get = pcs_pinconf_group_get,
	.pin_config_group_set = pcs_pinconf_group_set,
	.pin_config_dbg_show = pcs_pinconf_dbg_show,
	.pin_config_group_dbg_show = pcs_pinconf_group_dbg_show,
};

/**
 * pcs_add_pin() - add a pin to the static per controller pin array
 * @pcs: pcs driver instance
 * @offset: register offset from base
 */
static int __devinit pcs_add_pin(struct pcs_device *pcs, unsigned offset)
{
	struct pinctrl_pin_desc *pin;
	struct pcs_name *pn;
	int i;

	i = pcs->pins.cur;
	if (i >= pcs->desc.npins) {
		dev_err(pcs->dev, "too many pins, max %i\n",
			pcs->desc.npins);
		return -ENOMEM;
	}

	pin = &pcs->pins.pa[i];
	pn = &pcs->names[i];
	sprintf(pn->name, "%lx",
		(unsigned long)pcs->res->start + offset);
	pin->name = pn->name;
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
static int __devinit pcs_allocate_pin_table(struct pcs_device *pcs)
{
	int mux_bytes, nr_pins, i;

	mux_bytes = pcs->width / BITS_PER_BYTE;
	nr_pins = pcs->size / mux_bytes;

	dev_dbg(pcs->dev, "allocating %i pins\n", nr_pins);
	pcs->pins.pa = devm_kzalloc(pcs->dev,
				sizeof(*pcs->pins.pa) * nr_pins,
				GFP_KERNEL);
	if (!pcs->pins.pa)
		return -ENOMEM;

	pcs->names = devm_kzalloc(pcs->dev,
				sizeof(struct pcs_name) * nr_pins,
				GFP_KERNEL);
	if (!pcs->names)
		return -ENOMEM;

	pcs->desc.pins = pcs->pins.pa;
	pcs->desc.npins = nr_pins;

	for (i = 0; i < pcs->desc.npins; i++) {
		unsigned offset;
		int res;

		offset = i * mux_bytes;
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
 * @np: device node of the mux entry
 * @name: name of the function
 * @vals: array of mux register value pairs used by the function
 * @nvals: number of mux register value pairs
 * @pgnames: array of pingroup names for the function
 * @npgnames: number of pingroup names
 */
static struct pcs_function *pcs_add_function(struct pcs_device *pcs,
					struct device_node *np,
					const char *name,
					struct pcs_func_vals *vals,
					unsigned nvals,
					const char **pgnames,
					unsigned npgnames)
{
	struct pcs_function *function;

	function = devm_kzalloc(pcs->dev, sizeof(*function), GFP_KERNEL);
	if (!function)
		return NULL;

	function->name = name;
	function->vals = vals;
	function->nvals = nvals;
	function->pgnames = pgnames;
	function->npgnames = npgnames;

	mutex_lock(&pcs->mutex);
	list_add_tail(&function->node, &pcs->functions);
	radix_tree_insert(&pcs->ftree, pcs->nfuncs, function);
	pcs->nfuncs++;
	mutex_unlock(&pcs->mutex);

	return function;
}

static void pcs_remove_function(struct pcs_device *pcs,
				struct pcs_function *function)
{
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->nfuncs; i++) {
		struct pcs_function *found;

		found = radix_tree_lookup(&pcs->ftree, i);
		if (found == function)
			radix_tree_delete(&pcs->ftree, i);
	}
	list_del(&function->node);
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_add_pingroup() - add a pingroup to the pingroup list
 * @pcs: pcs driver instance
 * @np: device node of the mux entry
 * @name: name of the pingroup
 * @gpins: array of the pins that belong to the group
 * @ngpins: number of pins in the group
 */
static int pcs_add_pingroup(struct pcs_device *pcs,
					struct device_node *np,
					const char *name,
					int *gpins,
					int ngpins)
{
	struct pcs_pingroup *pingroup;

	pingroup = devm_kzalloc(pcs->dev, sizeof(*pingroup), GFP_KERNEL);
	if (!pingroup)
		return -ENOMEM;

	pingroup->name = name;
	pingroup->np = np;
	pingroup->gpins = gpins;
	pingroup->ngpins = ngpins;

	mutex_lock(&pcs->mutex);
	list_add_tail(&pingroup->node, &pcs->pingroups);
	radix_tree_insert(&pcs->pgtree, pcs->ngroups, pingroup);
	pcs->ngroups++;
	mutex_unlock(&pcs->mutex);

	return 0;
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

	index = offset / (pcs->width / BITS_PER_BYTE);

	return index;
}

/**
 * smux_parse_one_pinctrl_entry() - parses a device tree mux entry
 * @pcs: pinctrl driver instance
 * @np: device node of the mux entry
 * @map: map entry
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
						const char **pgnames)
{
	struct pcs_func_vals *vals;
	const __be32 *mux;
	int size, params, rows, *pins, index = 0, found = 0, res = -ENOMEM;
	struct pcs_function *function;

	if (pcs->bits_per_mux) {
		params = 3;
		mux = of_get_property(np, PCS_MUX_BITS_NAME, &size);
	} else {
		params = 2;
		mux = of_get_property(np, PCS_MUX_PINS_NAME, &size);
	}

	if (!mux) {
		dev_err(pcs->dev, "no valid property for %s\n", np->name);
		return -EINVAL;
	}

	if (size < (sizeof(*mux) * params)) {
		dev_err(pcs->dev, "bad data for %s\n", np->name);
		return -EINVAL;
	}

	size /= sizeof(*mux);	/* Number of elements in array */
	rows = size / params;

	vals = devm_kzalloc(pcs->dev, sizeof(*vals) * rows, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	pins = devm_kzalloc(pcs->dev, sizeof(*pins) * rows, GFP_KERNEL);
	if (!pins)
		goto free_vals;

	while (index < size) {
		unsigned offset, val;
		int pin;

		offset = be32_to_cpup(mux + index++);
		val = be32_to_cpup(mux + index++);
		vals[found].reg = pcs->base + offset;
		vals[found].val = val;
		if (params == 3) {
			val = be32_to_cpup(mux + index++);
			vals[found].mask = val;
		}

		pin = pcs_get_pin_by_offset(pcs, offset);
		if (pin < 0) {
			dev_err(pcs->dev,
				"could not add functions for %s %ux\n",
				np->name, offset);
			break;
		}
		pins[found++] = pin;
	}

	pgnames[0] = np->name;
	function = pcs_add_function(pcs, np, np->name, vals, found, pgnames, 1);
	if (!function)
		goto free_pins;

	res = pcs_add_pingroup(pcs, np, np->name, pins, found);
	if (res < 0)
		goto free_function;

	(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)->data.mux.group = np->name;
	(*map)->data.mux.function = np->name;

	return 0;

free_function:
	pcs_remove_function(pcs, function);

free_pins:
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

	*map = devm_kzalloc(pcs->dev, sizeof(**map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	*num_maps = 0;

	pgnames = devm_kzalloc(pcs->dev, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames) {
		ret = -ENOMEM;
		goto free_map;
	}

	ret = pcs_parse_one_pinctrl_entry(pcs, np_config, map, pgnames);
	if (ret < 0) {
		dev_err(pcs->dev, "no pins entries for %s\n",
			np_config->name);
		goto free_pgnames;
	}
	*num_maps = 1;

	return 0;

free_pgnames:
	devm_kfree(pcs->dev, pgnames);
free_map:
	devm_kfree(pcs->dev, *map);

	return ret;
}

/**
 * pcs_free_funcs() - free memory used by functions
 * @pcs: pcs driver instance
 */
static void pcs_free_funcs(struct pcs_device *pcs)
{
	struct list_head *pos, *tmp;
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->nfuncs; i++) {
		struct pcs_function *func;

		func = radix_tree_lookup(&pcs->ftree, i);
		if (!func)
			continue;
		radix_tree_delete(&pcs->ftree, i);
	}
	list_for_each_safe(pos, tmp, &pcs->functions) {
		struct pcs_function *function;

		function = list_entry(pos, struct pcs_function, node);
		list_del(&function->node);
	}
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_free_pingroups() - free memory used by pingroups
 * @pcs: pcs driver instance
 */
static void pcs_free_pingroups(struct pcs_device *pcs)
{
	struct list_head *pos, *tmp;
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->ngroups; i++) {
		struct pcs_pingroup *pingroup;

		pingroup = radix_tree_lookup(&pcs->pgtree, i);
		if (!pingroup)
			continue;
		radix_tree_delete(&pcs->pgtree, i);
	}
	list_for_each_safe(pos, tmp, &pcs->pingroups) {
		struct pcs_pingroup *pingroup;

		pingroup = list_entry(pos, struct pcs_pingroup, node);
		list_del(&pingroup->node);
	}
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_free_resources() - free memory used by this driver
 * @pcs: pcs driver instance
 */
static void pcs_free_resources(struct pcs_device *pcs)
{
	if (pcs->pctl)
		pinctrl_unregister(pcs->pctl);

	pcs_free_funcs(pcs);
	pcs_free_pingroups(pcs);
}

#define PCS_GET_PROP_U32(name, reg, err)				\
	do {								\
		ret = of_property_read_u32(np, name, reg);		\
		if (ret) {						\
			dev_err(pcs->dev, err);				\
			return ret;					\
		}							\
	} while (0);

static struct of_device_id pcs_of_match[];

static int __devinit pcs_add_gpio_range(struct device_node *node,
					struct pcs_device *pcs)
{
	struct pcs_gpio_range *gpio;
	struct device_node *child;
	struct resource r;
	const char name[] = "pinctrl-single";
	u32 gpiores[PCS_MAX_GPIO_VALUES];
	int ret, i = 0, mux_bytes = 0;

	for_each_child_of_node(node, child) {
		ret = of_address_to_resource(child, 0, &r);
		if (ret < 0)
			continue;
		memset(gpiores, 0, sizeof(u32) * PCS_MAX_GPIO_VALUES);
		ret = of_property_read_u32_array(child, "pinctrl-single,gpio",
						 gpiores, PCS_MAX_GPIO_VALUES);
		if (ret < 0)
			continue;
		gpio = devm_kzalloc(pcs->dev, sizeof(*gpio), GFP_KERNEL);
		if (!gpio) {
			dev_err(pcs->dev, "failed to allocate pcs gpio\n");
			return -ENOMEM;
		}
		gpio->range.name = devm_kzalloc(pcs->dev, sizeof(name),
						GFP_KERNEL);
		if (!gpio->range.name) {
			dev_err(pcs->dev, "failed to allocate range name\n");
			return -ENOMEM;
		}
		memcpy((char *)gpio->range.name, name, sizeof(name));

		gpio->range.id = i++;
		gpio->range.base = gpiores[0];
		gpio->gpio_func = gpiores[1];
		mux_bytes = pcs->width / BITS_PER_BYTE;
		gpio->range.pin_base = (r.start - pcs->res->start) / mux_bytes;
		gpio->range.npins = (r.end - r.start) / mux_bytes + 1;

		pinctrl_add_gpio_range(pcs->pctl, &gpio->range);
	}
	return 0;
}

static int __devinit pcs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct resource *res;
	struct pcs_device *pcs;
	int ret;

	match = of_match_device(pcs_of_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	pcs = devm_kzalloc(&pdev->dev, sizeof(*pcs), GFP_KERNEL);
	if (!pcs) {
		dev_err(&pdev->dev, "could not allocate\n");
		return -ENOMEM;
	}
	pcs->dev = &pdev->dev;
	mutex_init(&pcs->mutex);
	INIT_LIST_HEAD(&pcs->pingroups);
	INIT_LIST_HEAD(&pcs->functions);

	PCS_GET_PROP_U32("pinctrl-single,register-width", &pcs->width,
			 "register width not specified\n");

	PCS_GET_PROP_U32("pinctrl-single,function-mask", &pcs->fmask,
			 "function register mask not specified\n");
	pcs->fshift = ffs(pcs->fmask) - 1;
	pcs->fmax = pcs->fmask >> pcs->fshift;

	ret = of_property_read_u32(np, "pinctrl-single,function-off",
					&pcs->foff);
	if (ret)
		pcs->foff = PCS_OFF_DISABLED;

	pcs->bits_per_mux = of_property_read_bool(np,
						  "pinctrl-single,bit-per-mux");

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

	INIT_RADIX_TREE(&pcs->pgtree, GFP_KERNEL);
	INIT_RADIX_TREE(&pcs->ftree, GFP_KERNEL);
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
	pcs->desc.confops = &pcs_pinconf_ops;
	pcs->desc.owner = THIS_MODULE;

	ret = pcs_allocate_pin_table(pcs);
	if (ret < 0)
		goto free;

	pcs->pctl = pinctrl_register(&pcs->desc, pcs->dev, pcs);
	if (!pcs->pctl) {
		dev_err(pcs->dev, "could not register single pinctrl driver\n");
		ret = -EINVAL;
		goto free;
	}

	ret = pcs_add_gpio_range(np, pcs);
	if (ret < 0)
		goto free;

	dev_info(pcs->dev, "%i pins at pa %p size %u\n",
		 pcs->desc.npins, pcs->base, pcs->size);

	return 0;

free:
	pcs_free_resources(pcs);

	return ret;
}

static int __devexit pcs_remove(struct platform_device *pdev)
{
	struct pcs_device *pcs = platform_get_drvdata(pdev);

	if (!pcs)
		return 0;

	pcs_free_resources(pcs);

	return 0;
}

static struct of_device_id pcs_of_match[] __devinitdata = {
	{ .compatible = DRIVER_NAME, },
	{ },
};
MODULE_DEVICE_TABLE(of, pcs_of_match);

static struct platform_driver pcs_driver = {
	.probe		= pcs_probe,
	.remove		= __devexit_p(pcs_remove),
	.driver = {
		.owner		= THIS_MODULE,
		.name		= DRIVER_NAME,
		.of_match_table	= pcs_of_match,
	},
};

module_platform_driver(pcs_driver);

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("One-register-per-pin type device tree based pinctrl driver");
MODULE_LICENSE("GPL v2");
