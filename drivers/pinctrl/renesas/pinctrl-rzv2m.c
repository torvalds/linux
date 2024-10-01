// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2M Pin Control and GPIO driver core
 *
 * Based on:
 *   Renesas RZ/G2L Pin Control and GPIO driver core
 *
 * Copyright (C) 2022 Renesas Electronics Corporation.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/rzv2m-pinctrl.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define DRV_NAME	"pinctrl-rzv2m"

/*
 * Use 16 lower bits [15:0] for pin identifier
 * Use 16 higher bits [31:16] for pin mux function
 */
#define MUX_PIN_ID_MASK		GENMASK(15, 0)
#define MUX_FUNC_MASK		GENMASK(31, 16)
#define MUX_FUNC(pinconf)	FIELD_GET(MUX_FUNC_MASK, (pinconf))

/* PIN capabilities */
#define PIN_CFG_GRP_1_8V_2		1
#define PIN_CFG_GRP_1_8V_3		2
#define PIN_CFG_GRP_SWIO_1		3
#define PIN_CFG_GRP_SWIO_2		4
#define PIN_CFG_GRP_3_3V		5
#define PIN_CFG_GRP_MASK		GENMASK(2, 0)
#define PIN_CFG_BIAS			BIT(3)
#define PIN_CFG_DRV			BIT(4)
#define PIN_CFG_SLEW			BIT(5)

#define RZV2M_MPXED_PIN_FUNCS		(PIN_CFG_BIAS | \
					 PIN_CFG_DRV | \
					 PIN_CFG_SLEW)

/*
 * n indicates number of pins in the port, a is the register index
 * and f is pin configuration capabilities supported.
 */
#define RZV2M_GPIO_PORT_PACK(n, a, f)	(((n) << 24) | ((a) << 16) | (f))
#define RZV2M_GPIO_PORT_GET_PINCNT(x)	FIELD_GET(GENMASK(31, 24), (x))
#define RZV2M_GPIO_PORT_GET_INDEX(x)	FIELD_GET(GENMASK(23, 16), (x))
#define RZV2M_GPIO_PORT_GET_CFGS(x)	FIELD_GET(GENMASK(15, 0), (x))

#define RZV2M_DEDICATED_PORT_IDX	22

/*
 * BIT(31) indicates dedicated pin, b is the register bits (b * 16)
 * and f is the pin configuration capabilities supported.
 */
#define RZV2M_SINGLE_PIN		BIT(31)
#define RZV2M_SINGLE_PIN_PACK(b, f)	(RZV2M_SINGLE_PIN | \
					 ((RZV2M_DEDICATED_PORT_IDX) << 24) | \
					 ((b) << 16) | (f))
#define RZV2M_SINGLE_PIN_GET_PORT(x)	FIELD_GET(GENMASK(30, 24), (x))
#define RZV2M_SINGLE_PIN_GET_BIT(x)	FIELD_GET(GENMASK(23, 16), (x))
#define RZV2M_SINGLE_PIN_GET_CFGS(x)	FIELD_GET(GENMASK(15, 0), (x))

#define RZV2M_PIN_ID_TO_PORT(id)	((id) / RZV2M_PINS_PER_PORT)
#define RZV2M_PIN_ID_TO_PIN(id)		((id) % RZV2M_PINS_PER_PORT)

#define DO(n)		(0x00 + (n) * 0x40)
#define OE(n)		(0x04 + (n) * 0x40)
#define IE(n)		(0x08 + (n) * 0x40)
#define PFSEL(n)	(0x10 + (n) * 0x40)
#define DI(n)		(0x20 + (n) * 0x40)
#define PUPD(n)		(0x24 + (n) * 0x40)
#define DRV(n)		((n) < RZV2M_DEDICATED_PORT_IDX ? (0x28 + (n) * 0x40) \
							: 0x590)
#define SR(n)		((n) < RZV2M_DEDICATED_PORT_IDX ? (0x2c + (n) * 0x40) \
							: 0x594)
#define DI_MSK(n)	(0x30 + (n) * 0x40)
#define EN_MSK(n)	(0x34 + (n) * 0x40)

#define PFC_MASK	0x07
#define PUPD_MASK	0x03
#define DRV_MASK	0x03

struct rzv2m_dedicated_configs {
	const char *name;
	u32 config;
};

struct rzv2m_pinctrl_data {
	const char * const *port_pins;
	const u32 *port_pin_configs;
	const struct rzv2m_dedicated_configs *dedicated_pins;
	unsigned int n_port_pins;
	unsigned int n_dedicated_pins;
};

struct rzv2m_pinctrl {
	struct pinctrl_dev		*pctl;
	struct pinctrl_desc		desc;
	struct pinctrl_pin_desc		*pins;

	const struct rzv2m_pinctrl_data	*data;
	void __iomem			*base;
	struct device			*dev;

	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	gpio_range;

	spinlock_t			lock; /* lock read/write registers */
	struct mutex			mutex; /* serialize adding groups and functions */
};

static const unsigned int drv_1_8V_group2_uA[] = { 1800, 3800, 7800, 11000 };
static const unsigned int drv_1_8V_group3_uA[] = { 1600, 3200, 6400, 9600 };
static const unsigned int drv_SWIO_group2_3_3V_uA[] = { 9000, 11000, 13000, 18000 };
static const unsigned int drv_3_3V_group_uA[] = { 2000, 4000, 8000, 12000 };

/* Helper for registers that have a write enable bit in the upper word */
static void rzv2m_writel_we(void __iomem *addr, u8 shift, u8 value)
{
	writel((BIT(16) | value) << shift, addr);
}

static void rzv2m_pinctrl_set_pfc_mode(struct rzv2m_pinctrl *pctrl,
				       u8 port, u8 pin, u8 func)
{
	void __iomem *addr;

	/* Mask input/output */
	rzv2m_writel_we(pctrl->base + DI_MSK(port), pin, 1);
	rzv2m_writel_we(pctrl->base + EN_MSK(port), pin, 1);

	/* Select the function and set the write enable bits */
	addr = pctrl->base + PFSEL(port) + (pin / 4) * 4;
	writel(((PFC_MASK << 16) | func) << ((pin % 4) * 4), addr);

	/* Unmask input/output */
	rzv2m_writel_we(pctrl->base + EN_MSK(port), pin, 0);
	rzv2m_writel_we(pctrl->base + DI_MSK(port), pin, 0);
};

static int rzv2m_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzv2m_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	unsigned int i, *psel_val;
	struct group_desc *group;
	const unsigned int *pins;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;
	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	psel_val = func->data;
	pins = group->grp.pins;

	for (i = 0; i < group->grp.npins; i++) {
		dev_dbg(pctrl->dev, "port:%u pin: %u PSEL:%u\n",
			RZV2M_PIN_ID_TO_PORT(pins[i]), RZV2M_PIN_ID_TO_PIN(pins[i]),
			psel_val[i]);
		rzv2m_pinctrl_set_pfc_mode(pctrl, RZV2M_PIN_ID_TO_PORT(pins[i]),
					   RZV2M_PIN_ID_TO_PIN(pins[i]), psel_val[i]);
	}

	return 0;
};

static int rzv2m_map_add_config(struct pinctrl_map *map,
				const char *group_or_pin,
				enum pinctrl_map_type type,
				unsigned long *configs,
				unsigned int num_configs)
{
	unsigned long *cfgs;

	cfgs = kmemdup_array(configs, num_configs, sizeof(*cfgs), GFP_KERNEL);
	if (!cfgs)
		return -ENOMEM;

	map->type = type;
	map->data.configs.group_or_pin = group_or_pin;
	map->data.configs.configs = cfgs;
	map->data.configs.num_configs = num_configs;

	return 0;
}

static int rzv2m_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct device_node *parent,
				   struct pinctrl_map **map,
				   unsigned int *num_maps,
				   unsigned int *index)
{
	struct rzv2m_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned long *configs = NULL;
	unsigned int *pins, *psel_val;
	unsigned int num_pinmux = 0;
	unsigned int idx = *index;
	unsigned int num_pins, i;
	unsigned int num_configs;
	struct property *pinmux;
	struct property *prop;
	int ret, gsel, fsel;
	const char **pin_fn;
	const char *name;
	const char *pin;

	pinmux = of_find_property(np, "pinmux", NULL);
	if (pinmux)
		num_pinmux = pinmux->length / sizeof(u32);

	ret = of_property_count_strings(np, "pins");
	if (ret == -EINVAL) {
		num_pins = 0;
	} else if (ret < 0) {
		dev_err(pctrl->dev, "Invalid pins list in DT\n");
		return ret;
	} else {
		num_pins = ret;
	}

	if (!num_pinmux && !num_pins)
		return 0;

	if (num_pinmux && num_pins) {
		dev_err(pctrl->dev,
			"DT node must contain either a pinmux or pins and not both\n");
		return -EINVAL;
	}

	ret = pinconf_generic_parse_dt_config(np, NULL, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (num_pins && !num_configs) {
		dev_err(pctrl->dev, "DT node must contain a config\n");
		ret = -ENODEV;
		goto done;
	}

	if (num_pinmux)
		nmaps += 1;

	if (num_pins)
		nmaps += num_pins;

	maps = krealloc_array(maps, nmaps, sizeof(*maps), GFP_KERNEL);
	if (!maps) {
		ret = -ENOMEM;
		goto done;
	}

	*map = maps;
	*num_maps = nmaps;
	if (num_pins) {
		of_property_for_each_string(np, "pins", prop, pin) {
			ret = rzv2m_map_add_config(&maps[idx], pin,
						   PIN_MAP_TYPE_CONFIGS_PIN,
						   configs, num_configs);
			if (ret < 0)
				goto done;

			idx++;
		}
		ret = 0;
		goto done;
	}

	pins = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*pins), GFP_KERNEL);
	psel_val = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*psel_val),
				GFP_KERNEL);
	pin_fn = devm_kzalloc(pctrl->dev, sizeof(*pin_fn), GFP_KERNEL);
	if (!pins || !psel_val || !pin_fn) {
		ret = -ENOMEM;
		goto done;
	}

	/* Collect pin locations and mux settings from DT properties */
	for (i = 0; i < num_pinmux; ++i) {
		u32 value;

		ret = of_property_read_u32_index(np, "pinmux", i, &value);
		if (ret)
			goto done;
		pins[i] = value & MUX_PIN_ID_MASK;
		psel_val[i] = MUX_FUNC(value);
	}

	if (parent) {
		name = devm_kasprintf(pctrl->dev, GFP_KERNEL, "%pOFn.%pOFn",
				      parent, np);
		if (!name) {
			ret = -ENOMEM;
			goto done;
		}
	} else {
		name = np->name;
	}

	mutex_lock(&pctrl->mutex);

	/* Register a single pin group listing all the pins we read from DT */
	gsel = pinctrl_generic_add_group(pctldev, name, pins, num_pinmux, NULL);
	if (gsel < 0) {
		ret = gsel;
		goto unlock;
	}

	/*
	 * Register a single group function where the 'data' is an array PSEL
	 * register values read from DT.
	 */
	pin_fn[0] = name;
	fsel = pinmux_generic_add_function(pctldev, name, pin_fn, 1, psel_val);
	if (fsel < 0) {
		ret = fsel;
		goto remove_group;
	}

	mutex_unlock(&pctrl->mutex);

	maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
	maps[idx].data.mux.group = name;
	maps[idx].data.mux.function = name;
	idx++;

	dev_dbg(pctrl->dev, "Parsed %pOF with %d pins\n", np, num_pinmux);
	ret = 0;
	goto done;

remove_group:
	pinctrl_generic_remove_group(pctldev, gsel);
unlock:
	mutex_unlock(&pctrl->mutex);
done:
	*index = idx;
	kfree(configs);
	return ret;
}

static void rzv2m_dt_free_map(struct pinctrl_dev *pctldev,
			      struct pinctrl_map *map,
			      unsigned int num_maps)
{
	unsigned int i;

	if (!map)
		return;

	for (i = 0; i < num_maps; ++i) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP ||
		    map[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(map[i].data.configs.configs);
	}
	kfree(map);
}

static int rzv2m_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map,
				unsigned int *num_maps)
{
	struct rzv2m_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int index;
	int ret;

	*map = NULL;
	*num_maps = 0;
	index = 0;

	for_each_child_of_node_scoped(np, child) {
		ret = rzv2m_dt_subnode_to_map(pctldev, child, np, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps == 0) {
		ret = rzv2m_dt_subnode_to_map(pctldev, np, NULL, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(pctrl->dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;

done:
	rzv2m_dt_free_map(pctldev, *map, *num_maps);

	return ret;
}

static int rzv2m_validate_gpio_pin(struct rzv2m_pinctrl *pctrl,
				   u32 cfg, u32 port, u8 bit)
{
	u8 pincount = RZV2M_GPIO_PORT_GET_PINCNT(cfg);
	u32 port_index = RZV2M_GPIO_PORT_GET_INDEX(cfg);
	u32 data;

	if (bit >= pincount || port >= pctrl->data->n_port_pins)
		return -EINVAL;

	data = pctrl->data->port_pin_configs[port];
	if (port_index != RZV2M_GPIO_PORT_GET_INDEX(data))
		return -EINVAL;

	return 0;
}

static void rzv2m_rmw_pin_config(struct rzv2m_pinctrl *pctrl, u32 offset,
				 u8 shift, u32 mask, u32 val)
{
	void __iomem *addr = pctrl->base + offset;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&pctrl->lock, flags);
	reg = readl(addr) & ~(mask << shift);
	writel(reg | (val << shift), addr);
	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzv2m_pinctrl_pinconf_get(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *config)
{
	struct rzv2m_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	const struct pinctrl_pin_desc *pin = &pctrl->desc.pins[_pin];
	unsigned int *pin_data = pin->drv_data;
	unsigned int arg = 0;
	u32 port;
	u32 cfg;
	u8 bit;
	u32 val;

	if (!pin_data)
		return -EINVAL;

	if (*pin_data & RZV2M_SINGLE_PIN) {
		port = RZV2M_SINGLE_PIN_GET_PORT(*pin_data);
		cfg = RZV2M_SINGLE_PIN_GET_CFGS(*pin_data);
		bit = RZV2M_SINGLE_PIN_GET_BIT(*pin_data);
	} else {
		cfg = RZV2M_GPIO_PORT_GET_CFGS(*pin_data);
		port = RZV2M_PIN_ID_TO_PORT(_pin);
		bit = RZV2M_PIN_ID_TO_PIN(_pin);

		if (rzv2m_validate_gpio_pin(pctrl, *pin_data, RZV2M_PIN_ID_TO_PORT(_pin), bit))
			return -EINVAL;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN: {
		enum pin_config_param bias;

		if (!(cfg & PIN_CFG_BIAS))
			return -EINVAL;

		/* PUPD uses 2-bits per pin */
		bit *= 2;

		switch ((readl(pctrl->base + PUPD(port)) >> bit) & PUPD_MASK) {
		case 0:
			bias = PIN_CONFIG_BIAS_PULL_DOWN;
			break;
		case 2:
			bias = PIN_CONFIG_BIAS_PULL_UP;
			break;
		default:
			bias = PIN_CONFIG_BIAS_DISABLE;
		}

		if (bias != param)
			return -EINVAL;
		break;
	}

	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		if (!(cfg & PIN_CFG_DRV))
			return -EINVAL;

		/* DRV uses 2-bits per pin */
		bit *= 2;

		val = (readl(pctrl->base + DRV(port)) >> bit) & DRV_MASK;

		switch (cfg & PIN_CFG_GRP_MASK) {
		case PIN_CFG_GRP_1_8V_2:
			arg = drv_1_8V_group2_uA[val];
			break;
		case PIN_CFG_GRP_1_8V_3:
			arg = drv_1_8V_group3_uA[val];
			break;
		case PIN_CFG_GRP_SWIO_2:
			arg = drv_SWIO_group2_3_3V_uA[val];
			break;
		case PIN_CFG_GRP_SWIO_1:
		case PIN_CFG_GRP_3_3V:
			arg = drv_3_3V_group_uA[val];
			break;
		default:
			return -EINVAL;
		}

		break;

	case PIN_CONFIG_SLEW_RATE:
		if (!(cfg & PIN_CFG_SLEW))
			return -EINVAL;

		arg = readl(pctrl->base + SR(port)) & BIT(bit);
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
};

static int rzv2m_pinctrl_pinconf_set(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *_configs,
				     unsigned int num_configs)
{
	struct rzv2m_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct pinctrl_pin_desc *pin = &pctrl->desc.pins[_pin];
	unsigned int *pin_data = pin->drv_data;
	enum pin_config_param param;
	u32 port;
	unsigned int i;
	u32 cfg;
	u8 bit;
	u32 val;

	if (!pin_data)
		return -EINVAL;

	if (*pin_data & RZV2M_SINGLE_PIN) {
		port = RZV2M_SINGLE_PIN_GET_PORT(*pin_data);
		cfg = RZV2M_SINGLE_PIN_GET_CFGS(*pin_data);
		bit = RZV2M_SINGLE_PIN_GET_BIT(*pin_data);
	} else {
		cfg = RZV2M_GPIO_PORT_GET_CFGS(*pin_data);
		port = RZV2M_PIN_ID_TO_PORT(_pin);
		bit = RZV2M_PIN_ID_TO_PIN(_pin);

		if (rzv2m_validate_gpio_pin(pctrl, *pin_data, RZV2M_PIN_ID_TO_PORT(_pin), bit))
			return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(_configs[i]);
		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!(cfg & PIN_CFG_BIAS))
				return -EINVAL;

			/* PUPD uses 2-bits per pin */
			bit *= 2;

			switch (param) {
			case PIN_CONFIG_BIAS_PULL_DOWN:
				val = 0;
				break;
			case PIN_CONFIG_BIAS_PULL_UP:
				val = 2;
				break;
			default:
				val = 1;
			}

			rzv2m_rmw_pin_config(pctrl, PUPD(port), bit, PUPD_MASK, val);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH_UA: {
			unsigned int arg = pinconf_to_config_argument(_configs[i]);
			const unsigned int *drv_strengths;
			unsigned int index;

			if (!(cfg & PIN_CFG_DRV))
				return -EINVAL;

			switch (cfg & PIN_CFG_GRP_MASK) {
			case PIN_CFG_GRP_1_8V_2:
				drv_strengths = drv_1_8V_group2_uA;
				break;
			case PIN_CFG_GRP_1_8V_3:
				drv_strengths = drv_1_8V_group3_uA;
				break;
			case PIN_CFG_GRP_SWIO_2:
				drv_strengths = drv_SWIO_group2_3_3V_uA;
				break;
			case PIN_CFG_GRP_SWIO_1:
			case PIN_CFG_GRP_3_3V:
				drv_strengths = drv_3_3V_group_uA;
				break;
			default:
				return -EINVAL;
			}

			for (index = 0; index < 4; index++) {
				if (arg == drv_strengths[index])
					break;
			}
			if (index >= 4)
				return -EINVAL;

			/* DRV uses 2-bits per pin */
			bit *= 2;

			rzv2m_rmw_pin_config(pctrl, DRV(port), bit, DRV_MASK, index);
			break;
		}

		case PIN_CONFIG_SLEW_RATE: {
			unsigned int arg = pinconf_to_config_argument(_configs[i]);

			if (!(cfg & PIN_CFG_SLEW))
				return -EINVAL;

			rzv2m_writel_we(pctrl->base + SR(port), bit, !arg);
			break;
		}

		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int rzv2m_pinctrl_pinconf_group_set(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   unsigned long *configs,
					   unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzv2m_pinctrl_pinconf_set(pctldev, pins[i], configs,
						num_configs);
		if (ret)
			return ret;
	}

	return 0;
};

static int rzv2m_pinctrl_pinconf_group_get(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, prev_config = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzv2m_pinctrl_pinconf_get(pctldev, pins[i], config);
		if (ret)
			return ret;

		/* Check config matches previous pins */
		if (i && prev_config != *config)
			return -EOPNOTSUPP;

		prev_config = *config;
	}

	return 0;
};

static const struct pinctrl_ops rzv2m_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = rzv2m_dt_node_to_map,
	.dt_free_map = rzv2m_dt_free_map,
};

static const struct pinmux_ops rzv2m_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzv2m_pinctrl_set_mux,
	.strict = true,
};

static const struct pinconf_ops rzv2m_pinctrl_confops = {
	.is_generic = true,
	.pin_config_get = rzv2m_pinctrl_pinconf_get,
	.pin_config_set = rzv2m_pinctrl_pinconf_set,
	.pin_config_group_set = rzv2m_pinctrl_pinconf_group_set,
	.pin_config_group_get = rzv2m_pinctrl_pinconf_group_get,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int rzv2m_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);
	int ret;

	ret = pinctrl_gpio_request(chip, offset);
	if (ret)
		return ret;

	rzv2m_pinctrl_set_pfc_mode(pctrl, port, bit, 0);

	return 0;
}

static void rzv2m_gpio_set_direction(struct rzv2m_pinctrl *pctrl, u32 port,
				     u8 bit, bool output)
{
	rzv2m_writel_we(pctrl->base + OE(port), bit, output);
	rzv2m_writel_we(pctrl->base + IE(port), bit, !output);
}

static int rzv2m_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);

	if (!(readl(pctrl->base + IE(port)) & BIT(bit)))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int rzv2m_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);

	rzv2m_gpio_set_direction(pctrl, port, bit, false);

	return 0;
}

static void rzv2m_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);

	rzv2m_writel_we(pctrl->base + DO(port), bit, !!value);
}

static int rzv2m_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);

	rzv2m_gpio_set(chip, offset, value);
	rzv2m_gpio_set_direction(pctrl, port, bit, true);

	return 0;
}

static int rzv2m_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzv2m_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZV2M_PIN_ID_TO_PORT(offset);
	u8 bit = RZV2M_PIN_ID_TO_PIN(offset);
	int direction = rzv2m_gpio_get_direction(chip, offset);

	if (direction == GPIO_LINE_DIRECTION_IN)
		return !!(readl(pctrl->base + DI(port)) & BIT(bit));
	else
		return !!(readl(pctrl->base + DO(port)) & BIT(bit));
}

static void rzv2m_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip, offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzv2m_gpio_direction_input(chip, offset);
}

static const char * const rzv2m_gpio_names[] = {
	"P0_0", "P0_1", "P0_2", "P0_3", "P0_4", "P0_5", "P0_6", "P0_7",
	"P0_8", "P0_9", "P0_10", "P0_11", "P0_12", "P0_13", "P0_14", "P0_15",
	"P1_0", "P1_1", "P1_2", "P1_3", "P1_4", "P1_5", "P1_6", "P1_7",
	"P1_8", "P1_9", "P1_10", "P1_11", "P1_12", "P1_13", "P1_14", "P1_15",
	"P2_0", "P2_1", "P2_2", "P2_3", "P2_4", "P2_5", "P2_6", "P2_7",
	"P2_8", "P2_9", "P2_10", "P2_11", "P2_12", "P2_13", "P2_14", "P2_15",
	"P3_0", "P3_1", "P3_2", "P3_3", "P3_4", "P3_5", "P3_6", "P3_7",
	"P3_8", "P3_9", "P3_10", "P3_11", "P3_12", "P3_13", "P3_14", "P3_15",
	"P4_0", "P4_1", "P4_2", "P4_3", "P4_4", "P4_5", "P4_6", "P4_7",
	"P4_8", "P4_9", "P4_10", "P4_11", "P4_12", "P4_13", "P4_14", "P4_15",
	"P5_0", "P5_1", "P5_2", "P5_3", "P5_4", "P5_5", "P5_6", "P5_7",
	"P5_8", "P5_9", "P5_10", "P5_11", "P5_12", "P5_13", "P5_14", "P5_15",
	"P6_0", "P6_1", "P6_2", "P6_3", "P6_4", "P6_5", "P6_6", "P6_7",
	"P6_8", "P6_9", "P6_10", "P6_11", "P6_12", "P6_13", "P6_14", "P6_15",
	"P7_0", "P7_1", "P7_2", "P7_3", "P7_4", "P7_5", "P7_6", "P7_7",
	"P7_8", "P7_9", "P7_10", "P7_11", "P7_12", "P7_13", "P7_14", "P7_15",
	"P8_0", "P8_1", "P8_2", "P8_3", "P8_4", "P8_5", "P8_6", "P8_7",
	"P8_8", "P8_9", "P8_10", "P8_11", "P8_12", "P8_13", "P8_14", "P8_15",
	"P9_0", "P9_1", "P9_2", "P9_3", "P9_4", "P9_5", "P9_6", "P9_7",
	"P9_8", "P9_9", "P9_10", "P9_11", "P9_12", "P9_13", "P9_14", "P9_15",
	"P10_0", "P10_1", "P10_2", "P10_3", "P10_4", "P10_5", "P10_6", "P10_7",
	"P10_8", "P10_9", "P10_10", "P10_11", "P10_12", "P10_13", "P10_14", "P10_15",
	"P11_0", "P11_1", "P11_2", "P11_3", "P11_4", "P11_5", "P11_6", "P11_7",
	"P11_8", "P11_9", "P11_10", "P11_11", "P11_12", "P11_13", "P11_14", "P11_15",
	"P12_0", "P12_1", "P12_2", "P12_3", "P12_4", "P12_5", "P12_6", "P12_7",
	"P12_8", "P12_9", "P12_10", "P12_11", "P12_12", "P12_13", "P12_14", "P12_15",
	"P13_0", "P13_1", "P13_2", "P13_3", "P13_4", "P13_5", "P13_6", "P13_7",
	"P13_8", "P13_9", "P13_10", "P13_11", "P13_12", "P13_13", "P13_14", "P13_15",
	"P14_0", "P14_1", "P14_2", "P14_3", "P14_4", "P14_5", "P14_6", "P14_7",
	"P14_8", "P14_9", "P14_10", "P14_11", "P14_12", "P14_13", "P14_14", "P14_15",
	"P15_0", "P15_1", "P15_2", "P15_3", "P15_4", "P15_5", "P15_6", "P15_7",
	"P15_8", "P15_9", "P15_10", "P15_11", "P15_12", "P15_13", "P15_14", "P15_15",
	"P16_0", "P16_1", "P16_2", "P16_3", "P16_4", "P16_5", "P16_6", "P16_7",
	"P16_8", "P16_9", "P16_10", "P16_11", "P16_12", "P16_13", "P16_14", "P16_15",
	"P17_0", "P17_1", "P17_2", "P17_3", "P17_4", "P17_5", "P17_6", "P17_7",
	"P17_8", "P17_9", "P17_10", "P17_11", "P17_12", "P17_13", "P17_14", "P17_15",
	"P18_0", "P18_1", "P18_2", "P18_3", "P18_4", "P18_5", "P18_6", "P18_7",
	"P18_8", "P18_9", "P18_10", "P18_11", "P18_12", "P18_13", "P18_14", "P18_15",
	"P19_0", "P19_1", "P19_2", "P19_3", "P19_4", "P19_5", "P19_6", "P19_7",
	"P19_8", "P19_9", "P19_10", "P19_11", "P19_12", "P19_13", "P19_14", "P19_15",
	"P20_0", "P20_1", "P20_2", "P20_3", "P20_4", "P20_5", "P20_6", "P20_7",
	"P20_8", "P20_9", "P20_10", "P20_11", "P20_12", "P20_13", "P20_14", "P20_15",
	"P21_0", "P21_1", "P21_2", "P21_3", "P21_4", "P21_5", "P21_6", "P21_7",
	"P21_8", "P21_9", "P21_10", "P21_11", "P21_12", "P21_13", "P21_14", "P21_15",
};

static const u32 rzv2m_gpio_configs[] = {
	RZV2M_GPIO_PORT_PACK(14, 0, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(16, 1, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(8,  2, PIN_CFG_GRP_1_8V_3 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(16, 3, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(8,  4, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(4,  5, PIN_CFG_GRP_1_8V_3 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(12, 6, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(6,  7, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(8,  8, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(8,  9, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(9,  10, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(9,  11, PIN_CFG_GRP_SWIO_1 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(4,  12, PIN_CFG_GRP_3_3V | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(12, 13, PIN_CFG_GRP_3_3V | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(8,  14, PIN_CFG_GRP_3_3V | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(16, 15, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(14, 16, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(1,  17, PIN_CFG_GRP_SWIO_2 | RZV2M_MPXED_PIN_FUNCS),
	RZV2M_GPIO_PORT_PACK(0,  18, 0),
	RZV2M_GPIO_PORT_PACK(0,  19, 0),
	RZV2M_GPIO_PORT_PACK(3,  20, PIN_CFG_GRP_1_8V_2 | PIN_CFG_DRV),
	RZV2M_GPIO_PORT_PACK(1,  21, PIN_CFG_GRP_SWIO_1 | PIN_CFG_DRV | PIN_CFG_SLEW),
};

static const struct rzv2m_dedicated_configs rzv2m_dedicated_pins[] = {
	{ "NAWPN", RZV2M_SINGLE_PIN_PACK(0,
		(PIN_CFG_GRP_SWIO_2 | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "IM0CLK", RZV2M_SINGLE_PIN_PACK(1,
		(PIN_CFG_GRP_SWIO_1 | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "IM1CLK", RZV2M_SINGLE_PIN_PACK(2,
		(PIN_CFG_GRP_SWIO_1 | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "DETDO", RZV2M_SINGLE_PIN_PACK(5,
		(PIN_CFG_GRP_1_8V_3 | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "DETMS", RZV2M_SINGLE_PIN_PACK(6,
		(PIN_CFG_GRP_1_8V_3 | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "PCRSTOUTB", RZV2M_SINGLE_PIN_PACK(12,
		(PIN_CFG_GRP_3_3V | PIN_CFG_DRV | PIN_CFG_SLEW)) },
	{ "USPWEN", RZV2M_SINGLE_PIN_PACK(14,
		(PIN_CFG_GRP_3_3V | PIN_CFG_DRV | PIN_CFG_SLEW)) },
};

static int rzv2m_gpio_register(struct rzv2m_pinctrl *pctrl)
{
	struct device_node *np = pctrl->dev->of_node;
	struct gpio_chip *chip = &pctrl->gpio_chip;
	const char *name = dev_name(pctrl->dev);
	struct of_phandle_args of_args;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &of_args);
	if (ret) {
		dev_err(pctrl->dev, "Unable to parse gpio-ranges\n");
		return ret;
	}

	if (of_args.args[0] != 0 || of_args.args[1] != 0 ||
	    of_args.args[2] != pctrl->data->n_port_pins) {
		dev_err(pctrl->dev, "gpio-ranges does not match selected SOC\n");
		return -EINVAL;
	}

	chip->names = pctrl->data->port_pins;
	chip->request = rzv2m_gpio_request;
	chip->free = rzv2m_gpio_free;
	chip->get_direction = rzv2m_gpio_get_direction;
	chip->direction_input = rzv2m_gpio_direction_input;
	chip->direction_output = rzv2m_gpio_direction_output;
	chip->get = rzv2m_gpio_get;
	chip->set = rzv2m_gpio_set;
	chip->label = name;
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->ngpio = of_args.args[2];

	pctrl->gpio_range.id = 0;
	pctrl->gpio_range.pin_base = 0;
	pctrl->gpio_range.base = 0;
	pctrl->gpio_range.npins = chip->ngpio;
	pctrl->gpio_range.name = chip->label;
	pctrl->gpio_range.gc = chip;
	ret = devm_gpiochip_add_data(pctrl->dev, chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO controller\n");
		return ret;
	}

	dev_dbg(pctrl->dev, "Registered gpio controller\n");

	return 0;
}

static int rzv2m_pinctrl_register(struct rzv2m_pinctrl *pctrl)
{
	struct pinctrl_pin_desc *pins;
	unsigned int i, j;
	u32 *pin_data;
	int ret;

	pctrl->desc.name = DRV_NAME;
	pctrl->desc.npins = pctrl->data->n_port_pins + pctrl->data->n_dedicated_pins;
	pctrl->desc.pctlops = &rzv2m_pinctrl_pctlops;
	pctrl->desc.pmxops = &rzv2m_pinctrl_pmxops;
	pctrl->desc.confops = &rzv2m_pinctrl_confops;
	pctrl->desc.owner = THIS_MODULE;

	pins = devm_kcalloc(pctrl->dev, pctrl->desc.npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pin_data = devm_kcalloc(pctrl->dev, pctrl->desc.npins,
				sizeof(*pin_data), GFP_KERNEL);
	if (!pin_data)
		return -ENOMEM;

	pctrl->pins = pins;
	pctrl->desc.pins = pins;

	for (i = 0, j = 0; i < pctrl->data->n_port_pins; i++) {
		pins[i].number = i;
		pins[i].name = pctrl->data->port_pins[i];
		if (i && !(i % RZV2M_PINS_PER_PORT))
			j++;
		pin_data[i] = pctrl->data->port_pin_configs[j];
		pins[i].drv_data = &pin_data[i];
	}

	for (i = 0; i < pctrl->data->n_dedicated_pins; i++) {
		unsigned int index = pctrl->data->n_port_pins + i;

		pins[index].number = index;
		pins[index].name = pctrl->data->dedicated_pins[i].name;
		pin_data[index] = pctrl->data->dedicated_pins[i].config;
		pins[index].drv_data = &pin_data[index];
	}

	ret = devm_pinctrl_register_and_init(pctrl->dev, &pctrl->desc, pctrl,
					     &pctrl->pctl);
	if (ret) {
		dev_err(pctrl->dev, "pinctrl registration failed\n");
		return ret;
	}

	ret = pinctrl_enable(pctrl->pctl);
	if (ret) {
		dev_err(pctrl->dev, "pinctrl enable failed\n");
		return ret;
	}

	ret = rzv2m_gpio_register(pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO chip: %i\n", ret);
		return ret;
	}

	return 0;
}

static int rzv2m_pinctrl_probe(struct platform_device *pdev)
{
	struct rzv2m_pinctrl *pctrl;
	struct clk *clk;
	int ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = &pdev->dev;

	pctrl->data = of_device_get_match_data(&pdev->dev);
	if (!pctrl->data)
		return -EINVAL;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	clk = devm_clk_get_enabled(pctrl->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(pctrl->dev, PTR_ERR(clk),
				     "failed to enable GPIO clk\n");

	spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);

	platform_set_drvdata(pdev, pctrl);

	ret = rzv2m_pinctrl_register(pctrl);
	if (ret)
		return ret;

	dev_info(pctrl->dev, "%s support registered\n", DRV_NAME);
	return 0;
}

static struct rzv2m_pinctrl_data r9a09g011_data = {
	.port_pins = rzv2m_gpio_names,
	.port_pin_configs = rzv2m_gpio_configs,
	.dedicated_pins = rzv2m_dedicated_pins,
	.n_port_pins = ARRAY_SIZE(rzv2m_gpio_configs) * RZV2M_PINS_PER_PORT,
	.n_dedicated_pins = ARRAY_SIZE(rzv2m_dedicated_pins),
};

static const struct of_device_id rzv2m_pinctrl_of_table[] = {
	{
		.compatible = "renesas,r9a09g011-pinctrl",
		.data = &r9a09g011_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver rzv2m_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzv2m_pinctrl_of_table),
	},
	.probe = rzv2m_pinctrl_probe,
};

static int __init rzv2m_pinctrl_init(void)
{
	return platform_driver_register(&rzv2m_pinctrl_driver);
}
core_initcall(rzv2m_pinctrl_init);

MODULE_AUTHOR("Phil Edworthy <phil.edworthy@renesas.com>");
MODULE_DESCRIPTION("Pin and gpio controller driver for RZ/V2M");
