/*
 * Support for configuration of IO Delay module found on Texas Instruments SoCs
 * such as DRA7
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - https://www.ti.com/
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>

#include "../core.h"
#include "../devicetree.h"

#define DRIVER_NAME	"ti-iodelay"

/**
 * struct ti_iodelay_reg_data - Describes the registers for the iodelay instance
 * @signature_mask: CONFIG_REG mask for the signature bits (see TRM)
 * @signature_value: CONFIG_REG signature value to be written (see TRM)
 * @lock_mask: CONFIG_REG mask for the lock bits (see TRM)
 * @lock_val: CONFIG_REG lock value for the lock bits (see TRM)
 * @unlock_val:CONFIG_REG unlock value for the lock bits (see TRM)
 * @binary_data_coarse_mask: CONFIG_REG coarse mask (see TRM)
 * @binary_data_fine_mask: CONFIG_REG fine mask (see TRM)
 * @reg_refclk_offset: Refclk register offset
 * @refclk_period_mask: Refclk mask
 * @reg_coarse_offset: Coarse register configuration offset
 * @coarse_delay_count_mask: Coarse delay count mask
 * @coarse_ref_count_mask: Coarse ref count mask
 * @reg_fine_offset: Fine register configuration offset
 * @fine_delay_count_mask: Fine delay count mask
 * @fine_ref_count_mask: Fine ref count mask
 * @reg_global_lock_offset: Global iodelay module lock register offset
 * @global_lock_mask: Lock mask
 * @global_unlock_val: Unlock value
 * @global_lock_val: Lock value
 * @reg_start_offset: Offset to iodelay registers after the CONFIG_REG_0 to 8
 * @reg_nr_per_pin: Number of iodelay registers for each pin
 * @regmap_config: Regmap configuration for the IODelay region
 */
struct ti_iodelay_reg_data {
	u32 signature_mask;
	u32 signature_value;
	u32 lock_mask;
	u32 lock_val;
	u32 unlock_val;
	u32 binary_data_coarse_mask;
	u32 binary_data_fine_mask;

	u32 reg_refclk_offset;
	u32 refclk_period_mask;

	u32 reg_coarse_offset;
	u32 coarse_delay_count_mask;
	u32 coarse_ref_count_mask;

	u32 reg_fine_offset;
	u32 fine_delay_count_mask;
	u32 fine_ref_count_mask;

	u32 reg_global_lock_offset;
	u32 global_lock_mask;
	u32 global_unlock_val;
	u32 global_lock_val;

	u32 reg_start_offset;
	u32 reg_nr_per_pin;

	struct regmap_config *regmap_config;
};

/**
 * struct ti_iodelay_reg_values - Computed io_reg configuration values (see TRM)
 * @coarse_ref_count: Coarse reference count
 * @coarse_delay_count: Coarse delay count
 * @fine_ref_count: Fine reference count
 * @fine_delay_count: Fine Delay count
 * @ref_clk_period: Reference Clock period
 * @cdpe: Coarse delay parameter
 * @fdpe: Fine delay parameter
 */
struct ti_iodelay_reg_values {
	u16 coarse_ref_count;
	u16 coarse_delay_count;

	u16 fine_ref_count;
	u16 fine_delay_count;

	u16 ref_clk_period;

	u32 cdpe;
	u32 fdpe;
};

/**
 * struct ti_iodelay_cfg - Description of each configuration parameters
 * @offset: Configuration register offset
 * @a_delay: Agnostic Delay (in ps)
 * @g_delay: Gnostic Delay (in ps)
 */
struct ti_iodelay_cfg {
	u16 offset;
	u16 a_delay;
	u16 g_delay;
};

/**
 * struct ti_iodelay_pingroup - Structure that describes one group
 * @cfg: configuration array for the pin (from dt)
 * @ncfg: number of configuration values allocated
 * @config: pinconf "Config" - currently a dummy value
 */
struct ti_iodelay_pingroup {
	struct ti_iodelay_cfg *cfg;
	int ncfg;
	unsigned long config;
};

/**
 * struct ti_iodelay_device - Represents information for a iodelay instance
 * @dev: Device pointer
 * @phys_base: Physical address base of the iodelay device
 * @reg_base: Virtual address base of the iodelay device
 * @regmap: Regmap for this iodelay instance
 * @pctl: Pinctrl device
 * @desc: pinctrl descriptor for pctl
 * @pa: pinctrl pin wise description
 * @reg_data: Register definition data for the IODelay instance
 * @reg_init_conf_values: Initial configuration values.
 */
struct ti_iodelay_device {
	struct device *dev;
	unsigned long phys_base;
	void __iomem *reg_base;
	struct regmap *regmap;

	struct pinctrl_dev *pctl;
	struct pinctrl_desc desc;
	struct pinctrl_pin_desc *pa;

	const struct ti_iodelay_reg_data *reg_data;
	struct ti_iodelay_reg_values reg_init_conf_values;
};

/**
 * ti_iodelay_extract() - extract bits for a field
 * @val: Register value
 * @mask: Mask
 *
 * Return: extracted value which is appropriately shifted
 */
static inline u32 ti_iodelay_extract(u32 val, u32 mask)
{
	return (val & mask) >> __ffs(mask);
}

/**
 * ti_iodelay_compute_dpe() - Compute equation for delay parameter
 * @period: Period to use
 * @ref: Reference Count
 * @delay: Delay count
 * @delay_m: Delay multiplier
 *
 * Return: Computed delay parameter
 */
static inline u32 ti_iodelay_compute_dpe(u16 period, u16 ref, u16 delay,
					 u16 delay_m)
{
	u64 m, d;

	/* Handle overflow conditions */
	m = 10 * (u64)period * (u64)ref;
	d = 2 * (u64)delay * (u64)delay_m;

	/* Truncate result back to 32 bits */
	return div64_u64(m, d);
}

/**
 * ti_iodelay_pinconf_set() - Configure the pin configuration
 * @iod: iodelay device
 * @cfg: Configuration
 *
 * Update the configuration register as per TRM and lockup once done.
 * *IMPORTANT NOTE* SoC TRM does recommend doing iodelay programmation only
 * while in Isolation. But, then, isolation also implies that every pin
 * on the SoC (including DDR) will be isolated out. The only benefit being
 * a glitchless configuration, However, the intent of this driver is purely
 * to support a "glitchy" configuration where applicable.
 *
 * Return: 0 in case of success, else appropriate error value
 */
static int ti_iodelay_pinconf_set(struct ti_iodelay_device *iod,
				  struct ti_iodelay_cfg *cfg)
{
	const struct ti_iodelay_reg_data *reg = iod->reg_data;
	struct ti_iodelay_reg_values *ival = &iod->reg_init_conf_values;
	struct device *dev = iod->dev;
	u32 g_delay_coarse, g_delay_fine;
	u32 a_delay_coarse, a_delay_fine;
	u32 c_elements, f_elements;
	u32 total_delay;
	u32 reg_mask, reg_val, tmp_val;
	int r;

	/* NOTE: Truncation is expected in all division below */
	g_delay_coarse = cfg->g_delay / 920;
	g_delay_fine = ((cfg->g_delay % 920) * 10) / 60;

	a_delay_coarse = cfg->a_delay / ival->cdpe;
	a_delay_fine = ((cfg->a_delay % ival->cdpe) * 10) / ival->fdpe;

	c_elements = g_delay_coarse + a_delay_coarse;
	f_elements = (g_delay_fine + a_delay_fine) / 10;

	if (f_elements > 22) {
		total_delay = c_elements * ival->cdpe + f_elements * ival->fdpe;
		c_elements = total_delay / ival->cdpe;
		f_elements = (total_delay % ival->cdpe) / ival->fdpe;
	}

	reg_mask = reg->signature_mask;
	reg_val = reg->signature_value << __ffs(reg->signature_mask);

	reg_mask |= reg->binary_data_coarse_mask;
	tmp_val = c_elements << __ffs(reg->binary_data_coarse_mask);
	if (tmp_val & ~reg->binary_data_coarse_mask) {
		dev_err(dev, "Masking overflow of coarse elements %08x\n",
			tmp_val);
		tmp_val &= reg->binary_data_coarse_mask;
	}
	reg_val |= tmp_val;

	reg_mask |= reg->binary_data_fine_mask;
	tmp_val = f_elements << __ffs(reg->binary_data_fine_mask);
	if (tmp_val & ~reg->binary_data_fine_mask) {
		dev_err(dev, "Masking overflow of fine elements %08x\n",
			tmp_val);
		tmp_val &= reg->binary_data_fine_mask;
	}
	reg_val |= tmp_val;

	/*
	 * NOTE: we leave the iodelay values unlocked - this is to work around
	 * situations such as those found with mmc mode change.
	 * However, this leaves open any unwarranted changes to padconf register
	 * impacting iodelay configuration. Use with care!
	 */
	reg_mask |= reg->lock_mask;
	reg_val |= reg->unlock_val << __ffs(reg->lock_mask);
	r = regmap_update_bits(iod->regmap, cfg->offset, reg_mask, reg_val);

	dev_dbg(dev, "Set reg 0x%x Delay(a: %d g: %d), Elements(C=%d F=%d)0x%x\n",
		cfg->offset, cfg->a_delay, cfg->g_delay, c_elements,
		f_elements, reg_val);

	return r;
}

/**
 * ti_iodelay_pinconf_init_dev() - Initialize IODelay device
 * @iod: iodelay device
 *
 * Unlocks the iodelay region, computes the common parameters
 *
 * Return: 0 in case of success, else appropriate error value
 */
static int ti_iodelay_pinconf_init_dev(struct ti_iodelay_device *iod)
{
	const struct ti_iodelay_reg_data *reg = iod->reg_data;
	struct device *dev = iod->dev;
	struct ti_iodelay_reg_values *ival = &iod->reg_init_conf_values;
	u32 val;
	int r;

	/* unlock the iodelay region */
	r = regmap_update_bits(iod->regmap, reg->reg_global_lock_offset,
			       reg->global_lock_mask, reg->global_unlock_val);
	if (r)
		return r;

	/* Read up Recalibration sequence done by bootloader */
	r = regmap_read(iod->regmap, reg->reg_refclk_offset, &val);
	if (r)
		return r;
	ival->ref_clk_period = ti_iodelay_extract(val, reg->refclk_period_mask);
	dev_dbg(dev, "refclk_period=0x%04x\n", ival->ref_clk_period);

	r = regmap_read(iod->regmap, reg->reg_coarse_offset, &val);
	if (r)
		return r;
	ival->coarse_ref_count =
	    ti_iodelay_extract(val, reg->coarse_ref_count_mask);
	ival->coarse_delay_count =
	    ti_iodelay_extract(val, reg->coarse_delay_count_mask);
	if (!ival->coarse_delay_count) {
		dev_err(dev, "Invalid Coarse delay count (0) (reg=0x%08x)\n",
			val);
		return -EINVAL;
	}
	ival->cdpe = ti_iodelay_compute_dpe(ival->ref_clk_period,
					    ival->coarse_ref_count,
					    ival->coarse_delay_count, 88);
	if (!ival->cdpe) {
		dev_err(dev, "Invalid cdpe computed params = %d %d %d\n",
			ival->ref_clk_period, ival->coarse_ref_count,
			ival->coarse_delay_count);
		return -EINVAL;
	}
	dev_dbg(iod->dev, "coarse: ref=0x%04x delay=0x%04x cdpe=0x%08x\n",
		ival->coarse_ref_count, ival->coarse_delay_count, ival->cdpe);

	r = regmap_read(iod->regmap, reg->reg_fine_offset, &val);
	if (r)
		return r;
	ival->fine_ref_count =
	    ti_iodelay_extract(val, reg->fine_ref_count_mask);
	ival->fine_delay_count =
	    ti_iodelay_extract(val, reg->fine_delay_count_mask);
	if (!ival->fine_delay_count) {
		dev_err(dev, "Invalid Fine delay count (0) (reg=0x%08x)\n",
			val);
		return -EINVAL;
	}
	ival->fdpe = ti_iodelay_compute_dpe(ival->ref_clk_period,
					    ival->fine_ref_count,
					    ival->fine_delay_count, 264);
	if (!ival->fdpe) {
		dev_err(dev, "Invalid fdpe(0) computed params = %d %d %d\n",
			ival->ref_clk_period, ival->fine_ref_count,
			ival->fine_delay_count);
		return -EINVAL;
	}
	dev_dbg(iod->dev, "fine: ref=0x%04x delay=0x%04x fdpe=0x%08x\n",
		ival->fine_ref_count, ival->fine_delay_count, ival->fdpe);

	return 0;
}

/**
 * ti_iodelay_pinconf_deinit_dev() - deinit the iodelay device
 * @iod:	IODelay device
 *
 * Deinitialize the IODelay device (basically just lock the region back up.
 */
static void ti_iodelay_pinconf_deinit_dev(struct ti_iodelay_device *iod)
{
	const struct ti_iodelay_reg_data *reg = iod->reg_data;

	/* lock the iodelay region back again */
	regmap_update_bits(iod->regmap, reg->reg_global_lock_offset,
			   reg->global_lock_mask, reg->global_lock_val);
}

/**
 * ti_iodelay_get_pingroup() - Find the group mapped by a group selector
 * @iod: iodelay device
 * @selector: Group Selector
 *
 * Return: Corresponding group representing group selector
 */
static struct ti_iodelay_pingroup *
ti_iodelay_get_pingroup(struct ti_iodelay_device *iod, unsigned int selector)
{
	struct group_desc *g;

	g = pinctrl_generic_get_group(iod->pctl, selector);
	if (!g) {
		dev_err(iod->dev, "%s could not find pingroup %i\n", __func__,
			selector);

		return NULL;
	}

	return g->data;
}

/**
 * ti_iodelay_offset_to_pin() - get a pin index based on the register offset
 * @iod: iodelay driver instance
 * @offset: register offset from the base
 */
static int ti_iodelay_offset_to_pin(struct ti_iodelay_device *iod,
				    unsigned int offset)
{
	const struct ti_iodelay_reg_data *r = iod->reg_data;
	unsigned int index;

	if (offset > r->regmap_config->max_register) {
		dev_err(iod->dev, "mux offset out of range: 0x%x (0x%x)\n",
			offset, r->regmap_config->max_register);
		return -EINVAL;
	}

	index = (offset - r->reg_start_offset) / r->regmap_config->reg_stride;
	index /= r->reg_nr_per_pin;

	return index;
}

/**
 * ti_iodelay_node_iterator() - Iterate iodelay node
 * @pctldev: Pin controller driver
 * @np: Device node
 * @pinctrl_spec: Parsed arguments from device tree
 * @pins: Array of pins in the pin group
 * @pin_index: Pin index in the pin array
 * @data: Pin controller driver specific data
 *
 */
static int ti_iodelay_node_iterator(struct pinctrl_dev *pctldev,
				    struct device_node *np,
				    const struct of_phandle_args *pinctrl_spec,
				    int *pins, int pin_index, void *data)
{
	struct ti_iodelay_device *iod;
	struct ti_iodelay_cfg *cfg = data;
	const struct ti_iodelay_reg_data *r;
	struct pinctrl_pin_desc *pd;
	int pin;

	iod = pinctrl_dev_get_drvdata(pctldev);
	if (!iod)
		return -EINVAL;

	r = iod->reg_data;

	if (pinctrl_spec->args_count < r->reg_nr_per_pin) {
		dev_err(iod->dev, "invalid args_count for spec: %i\n",
			pinctrl_spec->args_count);

		return -EINVAL;
	}

	/* Index plus two value cells */
	cfg[pin_index].offset = pinctrl_spec->args[0];
	cfg[pin_index].a_delay = pinctrl_spec->args[1] & 0xffff;
	cfg[pin_index].g_delay = pinctrl_spec->args[2] & 0xffff;

	pin = ti_iodelay_offset_to_pin(iod, cfg[pin_index].offset);
	if (pin < 0) {
		dev_err(iod->dev, "could not add functions for %pOFn %ux\n",
			np, cfg[pin_index].offset);
		return -ENODEV;
	}
	pins[pin_index] = pin;

	pd = &iod->pa[pin];
	pd->drv_data = &cfg[pin_index];

	dev_dbg(iod->dev, "%pOFn offset=%x a_delay = %d g_delay = %d\n",
		np, cfg[pin_index].offset, cfg[pin_index].a_delay,
		cfg[pin_index].g_delay);

	return 0;
}

/**
 * ti_iodelay_dt_node_to_map() - Map a device tree node to appropriate group
 * @pctldev: pinctrl device representing IODelay device
 * @np: Node Pointer (device tree)
 * @map: Pinctrl Map returned back to pinctrl framework
 * @num_maps: Number of maps (1)
 *
 * Maps the device tree description into a group of configuration parameters
 * for iodelay block entry.
 *
 * Return: 0 in case of success, else appropriate error value
 */
static int ti_iodelay_dt_node_to_map(struct pinctrl_dev *pctldev,
				     struct device_node *np,
				     struct pinctrl_map **map,
				     unsigned int *num_maps)
{
	struct ti_iodelay_device *iod;
	struct ti_iodelay_cfg *cfg;
	struct ti_iodelay_pingroup *g;
	const char *name = "pinctrl-pin-array";
	int rows, *pins, error = -EINVAL, i;

	iod = pinctrl_dev_get_drvdata(pctldev);
	if (!iod)
		return -EINVAL;

	rows = pinctrl_count_index_with_args(np, name);
	if (rows < 0)
		return rows;

	*map = devm_kzalloc(iod->dev, sizeof(**map), GFP_KERNEL);
	if (!*map)
		return -ENOMEM;
	*num_maps = 0;

	g = devm_kzalloc(iod->dev, sizeof(*g), GFP_KERNEL);
	if (!g) {
		error = -ENOMEM;
		goto free_map;
	}

	pins = devm_kcalloc(iod->dev, rows, sizeof(*pins), GFP_KERNEL);
	if (!pins) {
		error = -ENOMEM;
		goto free_group;
	}

	cfg = devm_kcalloc(iod->dev, rows, sizeof(*cfg), GFP_KERNEL);
	if (!cfg) {
		error = -ENOMEM;
		goto free_pins;
	}

	for (i = 0; i < rows; i++) {
		struct of_phandle_args pinctrl_spec;

		error = pinctrl_parse_index_with_args(np, name, i,
						      &pinctrl_spec);
		if (error)
			goto free_data;

		error = ti_iodelay_node_iterator(pctldev, np, &pinctrl_spec,
						 pins, i, cfg);
		if (error)
			goto free_data;
	}

	g->cfg = cfg;
	g->ncfg = i;
	g->config = PIN_CONFIG_END;

	error = pinctrl_generic_add_group(iod->pctl, np->name, pins, i, g);
	if (error < 0)
		goto free_data;

	(*map)->type = PIN_MAP_TYPE_CONFIGS_GROUP;
	(*map)->data.configs.group_or_pin = np->name;
	(*map)->data.configs.configs = &g->config;
	(*map)->data.configs.num_configs = 1;
	*num_maps = 1;

	return 0;

free_data:
	devm_kfree(iod->dev, cfg);
free_pins:
	devm_kfree(iod->dev, pins);
free_group:
	devm_kfree(iod->dev, g);
free_map:
	devm_kfree(iod->dev, *map);

	return error;
}

/**
 * ti_iodelay_pinconf_group_get() - Get the group configuration
 * @pctldev: pinctrl device representing IODelay device
 * @selector: Group selector
 * @config: Configuration returned
 *
 * Return: The configuration if the group is valid, else returns -EINVAL
 */
static int ti_iodelay_pinconf_group_get(struct pinctrl_dev *pctldev,
					unsigned int selector,
					unsigned long *config)
{
	struct ti_iodelay_device *iod;
	struct ti_iodelay_pingroup *group;

	iod = pinctrl_dev_get_drvdata(pctldev);
	group = ti_iodelay_get_pingroup(iod, selector);

	if (!group)
		return -EINVAL;

	*config = group->config;
	return 0;
}

/**
 * ti_iodelay_pinconf_group_set() - Configure the groups of pins
 * @pctldev: pinctrl device representing IODelay device
 * @selector: Group selector
 * @configs: Configurations
 * @num_configs: Number of configurations
 *
 * Return: 0 if all went fine, else appropriate error value.
 */
static int ti_iodelay_pinconf_group_set(struct pinctrl_dev *pctldev,
					unsigned int selector,
					unsigned long *configs,
					unsigned int num_configs)
{
	struct ti_iodelay_device *iod;
	struct device *dev;
	struct ti_iodelay_pingroup *group;
	int i;

	iod = pinctrl_dev_get_drvdata(pctldev);
	dev = iod->dev;
	group = ti_iodelay_get_pingroup(iod, selector);

	if (num_configs != 1) {
		dev_err(dev, "Unsupported number of configurations %d\n",
			num_configs);
		return -EINVAL;
	}

	if (*configs != PIN_CONFIG_END) {
		dev_err(dev, "Unsupported configuration\n");
		return -EINVAL;
	}

	for (i = 0; i < group->ncfg; i++) {
		if (ti_iodelay_pinconf_set(iod, &group->cfg[i]))
			return -ENOTSUPP;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
/**
 * ti_iodelay_pin_to_offset() - get pin register offset based on the pin index
 * @iod: iodelay driver instance
 * @selector: Pin index
 */
static unsigned int ti_iodelay_pin_to_offset(struct ti_iodelay_device *iod,
					     unsigned int selector)
{
	const struct ti_iodelay_reg_data *r = iod->reg_data;
	unsigned int offset;

	offset = selector * r->regmap_config->reg_stride;
	offset *= r->reg_nr_per_pin;
	offset += r->reg_start_offset;

	return offset;
}

static void ti_iodelay_pin_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s,
				    unsigned int pin)
{
	struct ti_iodelay_device *iod;
	struct pinctrl_pin_desc *pd;
	struct ti_iodelay_cfg *cfg;
	const struct ti_iodelay_reg_data *r;
	unsigned long offset;
	u32 in, oen, out;

	iod = pinctrl_dev_get_drvdata(pctldev);
	r = iod->reg_data;

	offset = ti_iodelay_pin_to_offset(iod, pin);
	pd = &iod->pa[pin];
	cfg = pd->drv_data;

	regmap_read(iod->regmap, offset, &in);
	regmap_read(iod->regmap, offset + r->regmap_config->reg_stride, &oen);
	regmap_read(iod->regmap, offset + r->regmap_config->reg_stride * 2,
		    &out);

	seq_printf(s, "%lx a: %i g: %i (%08x %08x %08x) %s ",
		   iod->phys_base + offset,
		   cfg ? cfg->a_delay : -1,
		   cfg ? cfg->g_delay : -1,
		   in, oen, out, DRIVER_NAME);
}

/**
 * ti_iodelay_pinconf_group_dbg_show() - show the group information
 * @pctldev: Show the group information
 * @s: Sequence file
 * @selector: Group selector
 *
 * Provide the configuration information of the selected group
 */
static void ti_iodelay_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					      struct seq_file *s,
					      unsigned int selector)
{
	struct ti_iodelay_device *iod;
	struct ti_iodelay_pingroup *group;
	int i;

	iod = pinctrl_dev_get_drvdata(pctldev);
	group = ti_iodelay_get_pingroup(iod, selector);
	if (!group)
		return;

	for (i = 0; i < group->ncfg; i++) {
		struct ti_iodelay_cfg *cfg;
		u32 reg = 0;

		cfg = &group->cfg[i];
		regmap_read(iod->regmap, cfg->offset, &reg);
		seq_printf(s, "\n\t0x%08x = 0x%08x (%3d, %3d)",
			cfg->offset, reg, cfg->a_delay, cfg->g_delay);
	}
}
#endif

static const struct pinctrl_ops ti_iodelay_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show = ti_iodelay_pin_dbg_show,
#endif
	.dt_node_to_map = ti_iodelay_dt_node_to_map,
};

static const struct pinconf_ops ti_iodelay_pinctrl_pinconf_ops = {
	.pin_config_group_get = ti_iodelay_pinconf_group_get,
	.pin_config_group_set = ti_iodelay_pinconf_group_set,
#ifdef CONFIG_DEBUG_FS
	.pin_config_group_dbg_show = ti_iodelay_pinconf_group_dbg_show,
#endif
};

/**
 * ti_iodelay_alloc_pins() - Allocate structures needed for pins for iodelay
 * @dev: Device pointer
 * @iod: iodelay device
 * @base_phy: Base Physical Address
 *
 * Return: 0 if all went fine, else appropriate error value.
 */
static int ti_iodelay_alloc_pins(struct device *dev,
				 struct ti_iodelay_device *iod, u32 base_phy)
{
	const struct ti_iodelay_reg_data *r = iod->reg_data;
	struct pinctrl_pin_desc *pin;
	u32 phy_reg;
	int nr_pins, i;

	nr_pins = ti_iodelay_offset_to_pin(iod, r->regmap_config->max_register);
	dev_dbg(dev, "Allocating %i pins\n", nr_pins);

	iod->pa = devm_kcalloc(dev, nr_pins, sizeof(*iod->pa), GFP_KERNEL);
	if (!iod->pa)
		return -ENOMEM;

	iod->desc.pins = iod->pa;
	iod->desc.npins = nr_pins;

	phy_reg = r->reg_start_offset + base_phy;

	for (i = 0; i < nr_pins; i++, phy_reg += 4) {
		pin = &iod->pa[i];
		pin->number = i;
	}

	return 0;
}

static struct regmap_config dra7_iodelay_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xd1c,
};

static struct ti_iodelay_reg_data dra7_iodelay_data = {
	.signature_mask = 0x0003f000,
	.signature_value = 0x29,
	.lock_mask = 0x00000400,
	.lock_val = 1,
	.unlock_val = 0,
	.binary_data_coarse_mask = 0x000003e0,
	.binary_data_fine_mask = 0x0000001f,

	.reg_refclk_offset = 0x14,
	.refclk_period_mask = 0xffff,

	.reg_coarse_offset = 0x18,
	.coarse_delay_count_mask = 0xffff0000,
	.coarse_ref_count_mask = 0x0000ffff,

	.reg_fine_offset = 0x1C,
	.fine_delay_count_mask = 0xffff0000,
	.fine_ref_count_mask = 0x0000ffff,

	.reg_global_lock_offset = 0x2c,
	.global_lock_mask = 0x0000ffff,
	.global_unlock_val = 0x0000aaaa,
	.global_lock_val = 0x0000aaab,

	.reg_start_offset = 0x30,
	.reg_nr_per_pin = 3,
	.regmap_config = &dra7_iodelay_regmap_config,
};

static const struct of_device_id ti_iodelay_of_match[] = {
	{.compatible = "ti,dra7-iodelay", .data = &dra7_iodelay_data},
	{ /* Hopefully no more.. */ },
};
MODULE_DEVICE_TABLE(of, ti_iodelay_of_match);

/**
 * ti_iodelay_probe() - Standard probe
 * @pdev: platform device
 *
 * Return: 0 if all went fine, else appropriate error value.
 */
static int ti_iodelay_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = of_node_get(dev->of_node);
	struct resource *res;
	struct ti_iodelay_device *iod;
	int ret = 0;

	if (!np) {
		ret = -EINVAL;
		dev_err(dev, "No OF node\n");
		goto exit_out;
	}

	iod = devm_kzalloc(dev, sizeof(*iod), GFP_KERNEL);
	if (!iod) {
		ret = -ENOMEM;
		goto exit_out;
	}
	iod->dev = dev;
	iod->reg_data = device_get_match_data(dev);
	if (!iod->reg_data) {
		ret = -EINVAL;
		dev_err(dev, "No DATA match\n");
		goto exit_out;
	}

	/* So far We can assume there is only 1 bank of registers */
	iod->reg_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(iod->reg_base)) {
		ret = PTR_ERR(iod->reg_base);
		goto exit_out;
	}
	iod->phys_base = res->start;

	iod->regmap = devm_regmap_init_mmio(dev, iod->reg_base,
					    iod->reg_data->regmap_config);
	if (IS_ERR(iod->regmap)) {
		dev_err(dev, "Regmap MMIO init failed.\n");
		ret = PTR_ERR(iod->regmap);
		goto exit_out;
	}

	ret = ti_iodelay_pinconf_init_dev(iod);
	if (ret)
		goto exit_out;

	ret = ti_iodelay_alloc_pins(dev, iod, res->start);
	if (ret)
		goto exit_out;

	iod->desc.pctlops = &ti_iodelay_pinctrl_ops;
	/* no pinmux ops - we are pinconf */
	iod->desc.confops = &ti_iodelay_pinctrl_pinconf_ops;
	iod->desc.name = dev_name(dev);
	iod->desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(dev, &iod->desc, iod, &iod->pctl);
	if (ret) {
		dev_err(dev, "Failed to register pinctrl\n");
		goto exit_out;
	}

	platform_set_drvdata(pdev, iod);

	ret = pinctrl_enable(iod->pctl);
	if (ret)
		goto exit_out;

	return 0;

exit_out:
	of_node_put(np);
	return ret;
}

/**
 * ti_iodelay_remove() - standard remove
 * @pdev: platform device
 */
static void ti_iodelay_remove(struct platform_device *pdev)
{
	struct ti_iodelay_device *iod = platform_get_drvdata(pdev);

	ti_iodelay_pinconf_deinit_dev(iod);

	/* Expect other allocations to be freed by devm */
}

static struct platform_driver ti_iodelay_driver = {
	.probe = ti_iodelay_probe,
	.remove_new = ti_iodelay_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = ti_iodelay_of_match,
	},
};
module_platform_driver(ti_iodelay_driver);

MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("Pinconf driver for TI's IO Delay module");
MODULE_LICENSE("GPL v2");
