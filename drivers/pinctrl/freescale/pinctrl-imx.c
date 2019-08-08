// SPDX-License-Identifier: GPL-2.0+
//
// Core driver for the imx pin controller
//
// Copyright (C) 2012 Freescale Semiconductor, Inc.
// Copyright (C) 2012 Linaro Ltd.
//
// Author: Dong Aisheng <dong.aisheng@linaro.org>

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "pinctrl-imx.h"

/* The bits in CONFIG cell defined in binding doc*/
#define IMX_NO_PAD_CTL	0x80000000	/* no pin config need */
#define IMX_PAD_SION 0x40000000		/* set SION */

static inline const struct group_desc *imx_pinctrl_find_group_by_name(
				struct pinctrl_dev *pctldev,
				const char *name)
{
	const struct group_desc *grp = NULL;
	int i;

	for (i = 0; i < pctldev->num_groups; i++) {
		grp = pinctrl_generic_get_group(pctldev, i);
		if (grp && !strcmp(grp->name, name))
			break;
	}

	return grp;
}

static void imx_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int imx_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct group_desc *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	struct imx_pin *pin;
	int map_num = 1;
	int i, j;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = imx_pinctrl_find_group_by_name(pctldev, np->name);
	if (!grp) {
		dev_err(ipctl->dev, "unable to find group for node %pOFn\n", np);
		return -EINVAL;
	}

	if (info->flags & IMX_USE_SCU) {
		map_num += grp->num_pins;
	} else {
		for (i = 0; i < grp->num_pins; i++) {
			pin = &((struct imx_pin *)(grp->data))[i];
			if (!(pin->conf.mmio.config & IMX_NO_PAD_CTL))
				map_num++;
		}
	}

	new_map = kmalloc_array(map_num, sizeof(struct pinctrl_map),
				GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		kfree(new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = j = 0; i < grp->num_pins; i++) {
		pin = &((struct imx_pin *)(grp->data))[i];

		/*
		 * We only create config maps for SCU pads or MMIO pads that
		 * are not using the default config(a.k.a IMX_NO_PAD_CTL)
		 */
		if (!(info->flags & IMX_USE_SCU) &&
		    (pin->conf.mmio.config & IMX_NO_PAD_CTL))
			continue;

		new_map[j].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[j].data.configs.group_or_pin =
					pin_get_name(pctldev, pin->pin);

		if (info->flags & IMX_USE_SCU) {
			/*
			 * For SCU case, we set mux and conf together
			 * in one IPC call
			 */
			new_map[j].data.configs.configs =
					(unsigned long *)&pin->conf.scu;
			new_map[j].data.configs.num_configs = 2;
		} else {
			new_map[j].data.configs.configs =
					&pin->conf.mmio.config;
			new_map[j].data.configs.num_configs = 1;
		}

		j++;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void imx_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops imx_pctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = imx_pin_dbg_show,
	.dt_node_to_map = imx_dt_node_to_map,
	.dt_free_map = imx_dt_free_map,
};

static int imx_pmx_set_one_pin_mmio(struct imx_pinctrl *ipctl,
				    struct imx_pin *pin)
{
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct imx_pin_mmio *pin_mmio = &pin->conf.mmio;
	const struct imx_pin_reg *pin_reg;
	unsigned int pin_id;

	pin_id = pin->pin;
	pin_reg = &ipctl->pin_regs[pin_id];

	if (pin_reg->mux_reg == -1) {
		dev_dbg(ipctl->dev, "Pin(%s) does not support mux function\n",
			info->pins[pin_id].name);
		return 0;
	}

	if (info->flags & SHARE_MUX_CONF_REG) {
		u32 reg;

		reg = readl(ipctl->base + pin_reg->mux_reg);
		reg &= ~info->mux_mask;
		reg |= (pin_mmio->mux_mode << info->mux_shift);
		writel(reg, ipctl->base + pin_reg->mux_reg);
		dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%x\n",
			pin_reg->mux_reg, reg);
	} else {
		writel(pin_mmio->mux_mode, ipctl->base + pin_reg->mux_reg);
		dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%x\n",
			pin_reg->mux_reg, pin_mmio->mux_mode);
	}

	/*
	 * If the select input value begins with 0xff, it's a quirky
	 * select input and the value should be interpreted as below.
	 *     31     23      15      7        0
	 *     | 0xff | shift | width | select |
	 * It's used to work around the problem that the select
	 * input for some pin is not implemented in the select
	 * input register but in some general purpose register.
	 * We encode the select input value, width and shift of
	 * the bit field into input_val cell of pin function ID
	 * in device tree, and then decode them here for setting
	 * up the select input bits in general purpose register.
	 */
	if (pin_mmio->input_val >> 24 == 0xff) {
		u32 val = pin_mmio->input_val;
		u8 select = val & 0xff;
		u8 width = (val >> 8) & 0xff;
		u8 shift = (val >> 16) & 0xff;
		u32 mask = ((1 << width) - 1) << shift;
		/*
		 * The input_reg[i] here is actually some IOMUXC general
		 * purpose register, not regular select input register.
		 */
		val = readl(ipctl->base + pin_mmio->input_reg);
		val &= ~mask;
		val |= select << shift;
		writel(val, ipctl->base + pin_mmio->input_reg);
	} else if (pin_mmio->input_reg) {
		/*
		 * Regular select input register can never be at offset
		 * 0, and we only print register value for regular case.
		 */
		if (ipctl->input_sel_base)
			writel(pin_mmio->input_val, ipctl->input_sel_base +
					pin_mmio->input_reg);
		else
			writel(pin_mmio->input_val, ipctl->base +
					pin_mmio->input_reg);
		dev_dbg(ipctl->dev,
			"==>select_input: offset 0x%x val 0x%x\n",
			pin_mmio->input_reg, pin_mmio->input_val);
	}

	return 0;
}

static int imx_pmx_set(struct pinctrl_dev *pctldev, unsigned selector,
		       unsigned group)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct function_desc *func;
	struct group_desc *grp;
	struct imx_pin *pin;
	unsigned int npins;
	int i, err;

	/*
	 * Configure the mux mode for each pin in the group for a specific
	 * function.
	 */
	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	npins = grp->num_pins;

	dev_dbg(ipctl->dev, "enable function %s group %s\n",
		func->name, grp->name);

	for (i = 0; i < npins; i++) {
		/*
		 * For IMX_USE_SCU case, we postpone the mux setting
		 * until config is set as we can set them together
		 * in one IPC call
		 */
		pin = &((struct imx_pin *)(grp->data))[i];
		if (!(info->flags & IMX_USE_SCU)) {
			err = imx_pmx_set_one_pin_mmio(ipctl, pin);
			if (err)
				return err;
		}
	}

	return 0;
}

struct pinmux_ops imx_pmx_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = imx_pmx_set,
};

/* decode generic config into raw register values */
static u32 imx_pinconf_decode_generic_config(struct imx_pinctrl *ipctl,
					      unsigned long *configs,
					      unsigned int num_configs)
{
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_cfg_params_decode *decode;
	enum pin_config_param param;
	u32 raw_config = 0;
	u32 param_val;
	int i, j;

	WARN_ON(num_configs > info->num_decodes);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		param_val = pinconf_to_config_argument(configs[i]);
		decode = info->decodes;
		for (j = 0; j < info->num_decodes; j++) {
			if (param == decode->param) {
				if (decode->invert)
					param_val = !param_val;
				raw_config |= (param_val << decode->shift)
					      & decode->mask;
				break;
			}
			decode++;
		}
	}

	if (info->fixup)
		info->fixup(configs, num_configs, &raw_config);

	return raw_config;
}

static u32 imx_pinconf_parse_generic_config(struct device_node *np,
					    struct imx_pinctrl *ipctl)
{
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct pinctrl_dev *pctl = ipctl->pctl;
	unsigned int num_configs;
	unsigned long *configs;
	int ret;

	if (!info->generic_pinconf)
		return 0;

	ret = pinconf_generic_parse_dt_config(np, pctl, &configs,
					      &num_configs);
	if (ret)
		return 0;

	return imx_pinconf_decode_generic_config(ipctl, configs, num_configs);
}

static int imx_pinconf_get_mmio(struct pinctrl_dev *pctldev, unsigned pin_id,
				unsigned long *config)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg = &ipctl->pin_regs[pin_id];

	if (pin_reg->conf_reg == -1) {
		dev_err(ipctl->dev, "Pin(%s) does not support config function\n",
			info->pins[pin_id].name);
		return -EINVAL;
	}

	*config = readl(ipctl->base + pin_reg->conf_reg);

	if (info->flags & SHARE_MUX_CONF_REG)
		*config &= ~info->mux_mask;

	return 0;
}

static int imx_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned pin_id, unsigned long *config)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	if (info->flags & IMX_USE_SCU)
		return imx_pinconf_get_scu(pctldev, pin_id, config);
	else
		return imx_pinconf_get_mmio(pctldev, pin_id, config);
}

static int imx_pinconf_set_mmio(struct pinctrl_dev *pctldev,
				unsigned pin_id, unsigned long *configs,
				unsigned num_configs)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg = &ipctl->pin_regs[pin_id];
	int i;

	if (pin_reg->conf_reg == -1) {
		dev_err(ipctl->dev, "Pin(%s) does not support config function\n",
			info->pins[pin_id].name);
		return -EINVAL;
	}

	dev_dbg(ipctl->dev, "pinconf set pin %s\n",
		info->pins[pin_id].name);

	for (i = 0; i < num_configs; i++) {
		if (info->flags & SHARE_MUX_CONF_REG) {
			u32 reg;
			reg = readl(ipctl->base + pin_reg->conf_reg);
			reg &= info->mux_mask;
			reg |= configs[i];
			writel(reg, ipctl->base + pin_reg->conf_reg);
			dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%x\n",
				pin_reg->conf_reg, reg);
		} else {
			writel(configs[i], ipctl->base + pin_reg->conf_reg);
			dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%lx\n",
				pin_reg->conf_reg, configs[i]);
		}
	} /* for each config */

	return 0;
}

static int imx_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned pin_id, unsigned long *configs,
			   unsigned num_configs)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	if (info->flags & IMX_USE_SCU)
		return imx_pinconf_set_scu(pctldev, pin_id,
					   configs, num_configs);
	else
		return imx_pinconf_set_mmio(pctldev, pin_id,
					    configs, num_configs);
}

static void imx_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned pin_id)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg;
	unsigned long config;
	int ret;

	if (info->flags & IMX_USE_SCU) {
		ret = imx_pinconf_get_scu(pctldev, pin_id, &config);
		if (ret) {
			dev_err(ipctl->dev, "failed to get %s pinconf\n",
				pin_get_name(pctldev, pin_id));
			seq_puts(s, "N/A");
			return;
		}
	} else {
		pin_reg = &ipctl->pin_regs[pin_id];
		if (pin_reg->conf_reg == -1) {
			seq_puts(s, "N/A");
			return;
		}

		config = readl(ipctl->base + pin_reg->conf_reg);
	}

	seq_printf(s, "0x%lx", config);
}

static void imx_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
	struct group_desc *grp;
	unsigned long config;
	const char *name;
	int i, ret;

	if (group >= pctldev->num_groups)
		return;

	seq_puts(s, "\n");
	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return;

	for (i = 0; i < grp->num_pins; i++) {
		struct imx_pin *pin = &((struct imx_pin *)(grp->data))[i];

		name = pin_get_name(pctldev, pin->pin);
		ret = imx_pinconf_get(pctldev, pin->pin, &config);
		if (ret)
			return;
		seq_printf(s, "  %s: 0x%lx\n", name, config);
	}
}

static const struct pinconf_ops imx_pinconf_ops = {
	.pin_config_get = imx_pinconf_get,
	.pin_config_set = imx_pinconf_set,
	.pin_config_dbg_show = imx_pinconf_dbg_show,
	.pin_config_group_dbg_show = imx_pinconf_group_dbg_show,
};

/*
 * Each pin represented in fsl,pins consists of a number of u32 PIN_FUNC_ID
 * and 1 u32 CONFIG, the total size is PIN_FUNC_ID + CONFIG for each pin.
 * For generic_pinconf case, there's no extra u32 CONFIG.
 *
 * PIN_FUNC_ID format:
 * Default:
 *     <mux_reg conf_reg input_reg mux_mode input_val>
 * SHARE_MUX_CONF_REG:
 *     <mux_conf_reg input_reg mux_mode input_val>
 * IMX_USE_SCU:
 *	<pin_id mux_mode>
 */
#define FSL_PIN_SIZE 24
#define FSL_PIN_SHARE_SIZE 20
#define FSL_SCU_PIN_SIZE 12

static void imx_pinctrl_parse_pin_mmio(struct imx_pinctrl *ipctl,
				       unsigned int *pin_id, struct imx_pin *pin,
				       const __be32 **list_p,
				       struct device_node *np)
{
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct imx_pin_mmio *pin_mmio = &pin->conf.mmio;
	struct imx_pin_reg *pin_reg;
	const __be32 *list = *list_p;
	u32 mux_reg, conf_reg;
	u32 config;

	mux_reg = be32_to_cpu(*list++);

	if (!(info->flags & ZERO_OFFSET_VALID) && !mux_reg)
		mux_reg = -1;

	if (info->flags & SHARE_MUX_CONF_REG) {
		conf_reg = mux_reg;
	} else {
		conf_reg = be32_to_cpu(*list++);
		if (!conf_reg)
			conf_reg = -1;
	}

	*pin_id = (mux_reg != -1) ? mux_reg / 4 : conf_reg / 4;
	pin_reg = &ipctl->pin_regs[*pin_id];
	pin->pin = *pin_id;
	pin_reg->mux_reg = mux_reg;
	pin_reg->conf_reg = conf_reg;
	pin_mmio->input_reg = be32_to_cpu(*list++);
	pin_mmio->mux_mode = be32_to_cpu(*list++);
	pin_mmio->input_val = be32_to_cpu(*list++);

	if (info->generic_pinconf) {
		/* generic pin config decoded */
		pin_mmio->config = imx_pinconf_parse_generic_config(np, ipctl);
	} else {
		/* legacy pin config read from devicetree */
		config = be32_to_cpu(*list++);

		/* SION bit is in mux register */
		if (config & IMX_PAD_SION)
			pin_mmio->mux_mode |= IOMUXC_CONFIG_SION;
		pin_mmio->config = config & ~IMX_PAD_SION;
	}

	*list_p = list;

	dev_dbg(ipctl->dev, "%s: 0x%x 0x%08lx", info->pins[*pin_id].name,
			     pin_mmio->mux_mode, pin_mmio->config);
}

static int imx_pinctrl_parse_groups(struct device_node *np,
				    struct group_desc *grp,
				    struct imx_pinctrl *ipctl,
				    u32 index)
{
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct imx_pin *pin;
	int size, pin_size;
	const __be32 *list;
	int i;

	dev_dbg(ipctl->dev, "group(%d): %pOFn\n", index, np);

	if (info->flags & IMX_USE_SCU)
		pin_size = FSL_SCU_PIN_SIZE;
	else if (info->flags & SHARE_MUX_CONF_REG)
		pin_size = FSL_PIN_SHARE_SIZE;
	else
		pin_size = FSL_PIN_SIZE;

	if (info->generic_pinconf)
		pin_size -= 4;

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is fsl,pins = <PIN_FUNC_ID CONFIG ...>,
	 * do sanity check and calculate pins number
	 *
	 * First try legacy 'fsl,pins' property, then fall back to the
	 * generic 'pinmux'.
	 *
	 * Note: for generic 'pinmux' case, there's no CONFIG part in
	 * the binding format.
	 */
	list = of_get_property(np, "fsl,pins", &size);
	if (!list) {
		list = of_get_property(np, "pinmux", &size);
		if (!list) {
			dev_err(ipctl->dev,
				"no fsl,pins and pins property in node %pOF\n", np);
			return -EINVAL;
		}
	}

	/* we do not check return since it's safe node passed down */
	if (!size || size % pin_size) {
		dev_err(ipctl->dev, "Invalid fsl,pins or pins property in node %pOF\n", np);
		return -EINVAL;
	}

	grp->num_pins = size / pin_size;
	grp->data = devm_kcalloc(ipctl->dev,
				 grp->num_pins, sizeof(struct imx_pin),
				 GFP_KERNEL);
	grp->pins = devm_kcalloc(ipctl->dev,
				 grp->num_pins, sizeof(unsigned int),
				 GFP_KERNEL);
	if (!grp->pins || !grp->data)
		return -ENOMEM;

	for (i = 0; i < grp->num_pins; i++) {
		pin = &((struct imx_pin *)(grp->data))[i];
		if (info->flags & IMX_USE_SCU)
			imx_pinctrl_parse_pin_scu(ipctl, &grp->pins[i],
						  pin, &list);
		else
			imx_pinctrl_parse_pin_mmio(ipctl, &grp->pins[i],
						   pin, &list, np);
	}

	return 0;
}

static int imx_pinctrl_parse_functions(struct device_node *np,
				       struct imx_pinctrl *ipctl,
				       u32 index)
{
	struct pinctrl_dev *pctl = ipctl->pctl;
	struct device_node *child;
	struct function_desc *func;
	struct group_desc *grp;
	u32 i = 0;

	dev_dbg(pctl->dev, "parse function(%d): %pOFn\n", index, np);

	func = pinmux_generic_get_function(pctl, index);
	if (!func)
		return -EINVAL;

	/* Initialise function */
	func->name = np->name;
	func->num_group_names = of_get_child_count(np);
	if (func->num_group_names == 0) {
		dev_err(ipctl->dev, "no groups defined in %pOF\n", np);
		return -EINVAL;
	}
	func->group_names = devm_kcalloc(ipctl->dev, func->num_group_names,
					 sizeof(char *), GFP_KERNEL);
	if (!func->group_names)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->group_names[i] = child->name;

		grp = devm_kzalloc(ipctl->dev, sizeof(struct group_desc),
				   GFP_KERNEL);
		if (!grp) {
			of_node_put(child);
			return -ENOMEM;
		}

		mutex_lock(&ipctl->mutex);
		radix_tree_insert(&pctl->pin_group_tree,
				  ipctl->group_index++, grp);
		mutex_unlock(&ipctl->mutex);

		imx_pinctrl_parse_groups(child, grp, ipctl, i++);
	}

	return 0;
}

/*
 * Check if the DT contains pins in the direct child nodes. This indicates the
 * newer DT format to store pins. This function returns true if the first found
 * fsl,pins property is in a child of np. Otherwise false is returned.
 */
static bool imx_pinctrl_dt_is_flat_functions(struct device_node *np)
{
	struct device_node *function_np;
	struct device_node *pinctrl_np;

	for_each_child_of_node(np, function_np) {
		if (of_property_read_bool(function_np, "fsl,pins")) {
			of_node_put(function_np);
			return true;
		}

		for_each_child_of_node(function_np, pinctrl_np) {
			if (of_property_read_bool(pinctrl_np, "fsl,pins")) {
				of_node_put(pinctrl_np);
				of_node_put(function_np);
				return false;
			}
		}
	}

	return true;
}

static int imx_pinctrl_probe_dt(struct platform_device *pdev,
				struct imx_pinctrl *ipctl)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct pinctrl_dev *pctl = ipctl->pctl;
	u32 nfuncs = 0;
	u32 i = 0;
	bool flat_funcs;

	if (!np)
		return -ENODEV;

	flat_funcs = imx_pinctrl_dt_is_flat_functions(np);
	if (flat_funcs) {
		nfuncs = 1;
	} else {
		nfuncs = of_get_child_count(np);
		if (nfuncs == 0) {
			dev_err(&pdev->dev, "no functions defined\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < nfuncs; i++) {
		struct function_desc *function;

		function = devm_kzalloc(&pdev->dev, sizeof(*function),
					GFP_KERNEL);
		if (!function)
			return -ENOMEM;

		mutex_lock(&ipctl->mutex);
		radix_tree_insert(&pctl->pin_function_tree, i, function);
		mutex_unlock(&ipctl->mutex);
	}
	pctl->num_functions = nfuncs;

	ipctl->group_index = 0;
	if (flat_funcs) {
		pctl->num_groups = of_get_child_count(np);
	} else {
		pctl->num_groups = 0;
		for_each_child_of_node(np, child)
			pctl->num_groups += of_get_child_count(child);
	}

	if (flat_funcs) {
		imx_pinctrl_parse_functions(np, ipctl, 0);
	} else {
		i = 0;
		for_each_child_of_node(np, child)
			imx_pinctrl_parse_functions(child, ipctl, i++);
	}

	return 0;
}

/*
 * imx_free_resources() - free memory used by this driver
 * @info: info driver instance
 */
static void imx_free_resources(struct imx_pinctrl *ipctl)
{
	if (ipctl->pctl)
		pinctrl_unregister(ipctl->pctl);
}

int imx_pinctrl_probe(struct platform_device *pdev,
		      const struct imx_pinctrl_soc_info *info)
{
	struct regmap_config config = { .name = "gpr" };
	struct device_node *dev_np = pdev->dev.of_node;
	struct pinctrl_desc *imx_pinctrl_desc;
	struct device_node *np;
	struct imx_pinctrl *ipctl;
	struct regmap *gpr;
	int ret, i;

	if (!info || !info->pins || !info->npins) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	if (info->gpr_compatible) {
		gpr = syscon_regmap_lookup_by_compatible(info->gpr_compatible);
		if (!IS_ERR(gpr))
			regmap_attach_dev(&pdev->dev, gpr, &config);
	}

	/* Create state holders etc for this driver */
	ipctl = devm_kzalloc(&pdev->dev, sizeof(*ipctl), GFP_KERNEL);
	if (!ipctl)
		return -ENOMEM;

	if (!(info->flags & IMX_USE_SCU)) {
		ipctl->pin_regs = devm_kmalloc_array(&pdev->dev, info->npins,
						     sizeof(*ipctl->pin_regs),
						     GFP_KERNEL);
		if (!ipctl->pin_regs)
			return -ENOMEM;

		for (i = 0; i < info->npins; i++) {
			ipctl->pin_regs[i].mux_reg = -1;
			ipctl->pin_regs[i].conf_reg = -1;
		}

		ipctl->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(ipctl->base))
			return PTR_ERR(ipctl->base);

		if (of_property_read_bool(dev_np, "fsl,input-sel")) {
			np = of_parse_phandle(dev_np, "fsl,input-sel", 0);
			if (!np) {
				dev_err(&pdev->dev, "iomuxc fsl,input-sel property not found\n");
				return -EINVAL;
			}

			ipctl->input_sel_base = of_iomap(np, 0);
			of_node_put(np);
			if (!ipctl->input_sel_base) {
				dev_err(&pdev->dev,
					"iomuxc input select base address not found\n");
				return -ENOMEM;
			}
		}
	}

	imx_pinctrl_desc = devm_kzalloc(&pdev->dev, sizeof(*imx_pinctrl_desc),
					GFP_KERNEL);
	if (!imx_pinctrl_desc)
		return -ENOMEM;

	imx_pinctrl_desc->name = dev_name(&pdev->dev);
	imx_pinctrl_desc->pins = info->pins;
	imx_pinctrl_desc->npins = info->npins;
	imx_pinctrl_desc->pctlops = &imx_pctrl_ops;
	imx_pinctrl_desc->pmxops = &imx_pmx_ops;
	imx_pinctrl_desc->confops = &imx_pinconf_ops;
	imx_pinctrl_desc->owner = THIS_MODULE;

	/* for generic pinconf */
	imx_pinctrl_desc->custom_params = info->custom_params;
	imx_pinctrl_desc->num_custom_params = info->num_custom_params;

	/* platform specific callback */
	imx_pmx_ops.gpio_set_direction = info->gpio_set_direction;

	mutex_init(&ipctl->mutex);

	ipctl->info = info;
	ipctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, ipctl);
	ret = devm_pinctrl_register_and_init(&pdev->dev,
					     imx_pinctrl_desc, ipctl,
					     &ipctl->pctl);
	if (ret) {
		dev_err(&pdev->dev, "could not register IMX pinctrl driver\n");
		goto free;
	}

	ret = imx_pinctrl_probe_dt(pdev, ipctl);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		goto free;
	}

	dev_info(&pdev->dev, "initialized IMX pinctrl driver\n");

	return pinctrl_enable(ipctl->pctl);

free:
	imx_free_resources(ipctl);

	return ret;
}

static int __maybe_unused imx_pinctrl_suspend(struct device *dev)
{
	struct imx_pinctrl *ipctl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(ipctl->pctl);
}

static int __maybe_unused imx_pinctrl_resume(struct device *dev)
{
	struct imx_pinctrl *ipctl = dev_get_drvdata(dev);

	return pinctrl_force_default(ipctl->pctl);
}

const struct dev_pm_ops imx_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(imx_pinctrl_suspend,
					imx_pinctrl_resume)
};
