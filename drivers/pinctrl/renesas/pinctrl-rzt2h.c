// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/T2H Pin Control and GPIO driver core
 *
 * Based on drivers/pinctrl/renesas/pinctrl-rzg2l.c
 *
 * Copyright (C) 2025 Renesas Electronics Corporation.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/renesas,r9a09g077-pinctrl.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define DRV_NAME	"pinctrl-rzt2h"

#define P(m)		(0x001 * (m))
#define PM(m)		(0x200 + 2 * (m))
#define PMC(m)		(0x400 + (m))
#define PFC(m)		(0x600 + 8 * (m))
#define PIN(m)		(0x800 + (m))
#define RSELP(m)	(0xc00 + (m))

#define PM_MASK			GENMASK(1, 0)
#define PM_PIN_MASK(pin)	(PM_MASK << ((pin) * 2))
#define PM_INPUT		BIT(0)
#define PM_OUTPUT		BIT(1)

#define PFC_MASK		GENMASK_ULL(5, 0)
#define PFC_PIN_MASK(pin)	(PFC_MASK << ((pin) * 8))

/*
 * Use 16 lower bits [15:0] for pin identifier
 * Use 8 higher bits [23:16] for pin mux function
 */
#define MUX_PIN_ID_MASK		GENMASK(15, 0)
#define MUX_FUNC_MASK		GENMASK(23, 16)

#define RZT2H_PIN_ID_TO_PORT(id)	((id) / RZT2H_PINS_PER_PORT)
#define RZT2H_PIN_ID_TO_PIN(id)		((id) % RZT2H_PINS_PER_PORT)

#define RZT2H_MAX_SAFETY_PORTS		12

struct rzt2h_pinctrl_data {
	unsigned int n_port_pins;
	const u8 *port_pin_configs;
	unsigned int n_ports;
};

struct rzt2h_pinctrl {
	struct pinctrl_dev		*pctl;
	struct pinctrl_desc		desc;
	struct pinctrl_pin_desc		*pins;
	const struct rzt2h_pinctrl_data	*data;
	void __iomem			*base0, *base1;
	struct device			*dev;
	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	gpio_range;
	spinlock_t			lock; /* lock read/write registers */
	struct mutex			mutex; /* serialize adding groups and functions */
	bool				safety_port_enabled;
};

#define RZT2H_GET_BASE(pctrl, port) \
	((port) > RZT2H_MAX_SAFETY_PORTS ? (pctrl)->base0 : (pctrl)->base1)

#define RZT2H_PINCTRL_REG_ACCESS(size, type)						\
static inline void rzt2h_pinctrl_write##size(struct rzt2h_pinctrl *pctrl, u8 port,	\
					     type val, unsigned int offset)		\
{											\
	write##size(val, RZT2H_GET_BASE(pctrl, port) + offset);				\
}											\
static inline type rzt2h_pinctrl_read##size(struct rzt2h_pinctrl *pctrl, u8 port,	\
					    unsigned int offset)			\
{											\
	return read##size(RZT2H_GET_BASE(pctrl, port) + offset);			\
}

RZT2H_PINCTRL_REG_ACCESS(b, u8)
RZT2H_PINCTRL_REG_ACCESS(w, u16)
RZT2H_PINCTRL_REG_ACCESS(q, u64)

static int rzt2h_validate_pin(struct rzt2h_pinctrl *pctrl, unsigned int offset)
{
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 pin = RZT2H_PIN_ID_TO_PIN(offset);
	u8 pincfg;

	if (offset >= pctrl->data->n_port_pins || port >= pctrl->data->n_ports)
		return -EINVAL;

	if (!pctrl->safety_port_enabled && port <= RZT2H_MAX_SAFETY_PORTS)
		return -EINVAL;

	pincfg = pctrl->data->port_pin_configs[port];
	return (pincfg & BIT(pin)) ? 0 : -EINVAL;
}

static void rzt2h_pinctrl_set_pfc_mode(struct rzt2h_pinctrl *pctrl,
				       u8 port, u8 pin, u8 func)
{
	u64 reg64;
	u16 reg16;

	guard(spinlock_irqsave)(&pctrl->lock);

	/* Set pin to 'Non-use (Hi-Z input protection)'  */
	reg16 = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg16 &= ~PM_PIN_MASK(pin);
	rzt2h_pinctrl_writew(pctrl, port, reg16, PM(port));

	/* Temporarily switch to GPIO mode with PMC register */
	reg16 = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	rzt2h_pinctrl_writeb(pctrl, port, reg16 & ~BIT(pin), PMC(port));

	/* Select Pin function mode with PFC register */
	reg64 = rzt2h_pinctrl_readq(pctrl, port, PFC(port));
	reg64 &= ~PFC_PIN_MASK(pin);
	rzt2h_pinctrl_writeq(pctrl, port, reg64 | ((u64)func << (pin * 8)), PFC(port));

	/* Switch to Peripheral pin function with PMC register */
	reg16 = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	rzt2h_pinctrl_writeb(pctrl, port, reg16 | BIT(pin), PMC(port));
};

static int rzt2h_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func;
	struct group_desc *group;
	const unsigned int *pins;
	unsigned int i;
	u8 *psel_val;
	int ret;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;

	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	psel_val = func->data;
	pins = group->grp.pins;

	for (i = 0; i < group->grp.npins; i++) {
		dev_dbg(pctrl->dev, "port:%u pin:%u PSEL:%u\n",
			RZT2H_PIN_ID_TO_PORT(pins[i]), RZT2H_PIN_ID_TO_PIN(pins[i]),
			psel_val[i]);
		ret = rzt2h_validate_pin(pctrl, pins[i]);
		if (ret)
			return ret;

		rzt2h_pinctrl_set_pfc_mode(pctrl, RZT2H_PIN_ID_TO_PORT(pins[i]),
					   RZT2H_PIN_ID_TO_PIN(pins[i]), psel_val[i]);
	}

	return 0;
};

static int rzt2h_map_add_config(struct pinctrl_map *map,
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

static int rzt2h_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct device_node *parent,
				   struct pinctrl_map **map,
				   unsigned int *num_maps,
				   unsigned int *index)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned long *configs = NULL;
	unsigned int num_pinmux = 0;
	unsigned int idx = *index;
	unsigned int num_pins, i;
	unsigned int num_configs;
	struct property *pinmux;
	struct property *prop;
	int ret, gsel, fsel;
	const char **pin_fn;
	unsigned int *pins;
	const char *name;
	const char *pin;
	u8 *psel_val;

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

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (num_pins && !num_configs) {
		dev_err(pctrl->dev, "DT node must contain a config\n");
		ret = -ENODEV;
		goto done;
	}

	if (num_pinmux) {
		nmaps += 1;
		if (num_configs)
			nmaps += 1;
	}

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
			ret = rzt2h_map_add_config(&maps[idx], pin,
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
		pins[i] = FIELD_GET(MUX_PIN_ID_MASK, value);
		psel_val[i] = FIELD_GET(MUX_FUNC_MASK, value);
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

	if (num_configs) {
		ret = rzt2h_map_add_config(&maps[idx], name,
					   PIN_MAP_TYPE_CONFIGS_GROUP,
					   configs, num_configs);
		if (ret < 0)
			goto done;

		idx++;
	}

	scoped_guard(mutex, &pctrl->mutex) {
		/* Register a single pin group listing all the pins we read from DT */
		gsel = pinctrl_generic_add_group(pctldev, name, pins, num_pinmux, NULL);
		if (gsel < 0) {
			ret = gsel;
			goto done;
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
	}

	maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
	maps[idx].data.mux.group = name;
	maps[idx].data.mux.function = name;
	idx++;

	dev_dbg(pctrl->dev, "Parsed %pOF with %d pins\n", np, num_pinmux);
	ret = 0;
	goto done;

remove_group:
	pinctrl_generic_remove_group(pctldev, gsel);
done:
	*index = idx;
	kfree(configs);
	return ret;
}

static void rzt2h_dt_free_map(struct pinctrl_dev *pctldev,
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

static int rzt2h_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map,
				unsigned int *num_maps)
{
	struct rzt2h_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int index;
	int ret;

	*map = NULL;
	*num_maps = 0;
	index = 0;

	for_each_child_of_node_scoped(np, child) {
		ret = rzt2h_dt_subnode_to_map(pctldev, child, np, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps == 0) {
		ret = rzt2h_dt_subnode_to_map(pctldev, np, NULL, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(pctrl->dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;

done:
	rzt2h_dt_free_map(pctldev, *map, *num_maps);
	return ret;
}

static const struct pinctrl_ops rzt2h_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = rzt2h_dt_node_to_map,
	.dt_free_map = rzt2h_dt_free_map,
};

static const struct pinmux_ops rzt2h_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzt2h_pinctrl_set_mux,
	.strict = true,
};

static int rzt2h_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	int ret;
	u8 reg;

	ret = rzt2h_validate_pin(pctrl, offset);
	if (ret)
		return ret;

	ret = pinctrl_gpio_request(chip, offset);
	if (ret)
		return ret;

	guard(spinlock_irqsave)(&pctrl->lock);

	/* Select GPIO mode in PMC Register */
	reg = rzt2h_pinctrl_readb(pctrl, port, PMC(port));
	reg &= ~BIT(bit);
	rzt2h_pinctrl_writeb(pctrl, port, reg, PMC(port));

	return 0;
}

static void rzt2h_gpio_set_direction(struct rzt2h_pinctrl *pctrl, u32 port,
				     u8 bit, bool output)
{
	u16 reg;

	guard(spinlock_irqsave)(&pctrl->lock);

	reg = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg &= ~PM_PIN_MASK(bit);

	reg |= (output ? PM_OUTPUT : PM_INPUT) << (bit * 2);
	rzt2h_pinctrl_writew(pctrl, port, reg, PM(port));
}

static int rzt2h_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	u16 reg;
	int ret;

	ret = rzt2h_validate_pin(pctrl, offset);
	if (ret)
		return ret;

	if (rzt2h_pinctrl_readb(pctrl, port, PMC(port)) & BIT(bit))
		return -EINVAL;

	reg = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg = (reg >> (bit * 2)) & PM_MASK;
	if (reg & PM_OUTPUT)
		return GPIO_LINE_DIRECTION_OUT;
	if (reg & PM_INPUT)
		return GPIO_LINE_DIRECTION_IN;

	return -EINVAL;
}

static int rzt2h_gpio_set(struct gpio_chip *chip, unsigned int offset,
			  int value)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	u8 reg;

	guard(spinlock_irqsave)(&pctrl->lock);

	reg = rzt2h_pinctrl_readb(pctrl, port, P(port));
	if (value)
		rzt2h_pinctrl_writeb(pctrl, port, reg | BIT(bit), P(port));
	else
		rzt2h_pinctrl_writeb(pctrl, port, reg & ~BIT(bit), P(port));

	return 0;
}

static int rzt2h_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);
	u16 reg;

	reg = rzt2h_pinctrl_readw(pctrl, port, PM(port));
	reg = (reg >> (bit * 2)) & PM_MASK;
	if (reg & PM_INPUT)
		return !!(rzt2h_pinctrl_readb(pctrl, port, PIN(port)) & BIT(bit));
	if (reg & PM_OUTPUT)
		return !!(rzt2h_pinctrl_readb(pctrl, port, P(port)) & BIT(bit));

	return -EINVAL;
}

static int rzt2h_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);

	rzt2h_gpio_set_direction(pctrl, port, bit, false);

	return 0;
}

static int rzt2h_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzt2h_pinctrl *pctrl = gpiochip_get_data(chip);
	u8 port = RZT2H_PIN_ID_TO_PORT(offset);
	u8 bit = RZT2H_PIN_ID_TO_PIN(offset);

	rzt2h_gpio_set(chip, offset, value);
	rzt2h_gpio_set_direction(pctrl, port, bit, true);

	return 0;
}

static void rzt2h_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip, offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzt2h_gpio_direction_input(chip, offset);
}

static const char * const rzt2h_gpio_names[] = {
	"P00_0", "P00_1", "P00_2", "P00_3", "P00_4", "P00_5", "P00_6", "P00_7",
	"P01_0", "P01_1", "P01_2", "P01_3", "P01_4", "P01_5", "P01_6", "P01_7",
	"P02_0", "P02_1", "P02_2", "P02_3", "P02_4", "P02_5", "P02_6", "P02_7",
	"P03_0", "P03_1", "P03_2", "P03_3", "P03_4", "P03_5", "P03_6", "P03_7",
	"P04_0", "P04_1", "P04_2", "P04_3", "P04_4", "P04_5", "P04_6", "P04_7",
	"P05_0", "P05_1", "P05_2", "P05_3", "P05_4", "P05_5", "P05_6", "P05_7",
	"P06_0", "P06_1", "P06_2", "P06_3", "P06_4", "P06_5", "P06_6", "P06_7",
	"P07_0", "P07_1", "P07_2", "P07_3", "P07_4", "P07_5", "P07_6", "P07_7",
	"P08_0", "P08_1", "P08_2", "P08_3", "P08_4", "P08_5", "P08_6", "P08_7",
	"P09_0", "P09_1", "P09_2", "P09_3", "P09_4", "P09_5", "P09_6", "P09_7",
	"P10_0", "P10_1", "P10_2", "P10_3", "P10_4", "P10_5", "P10_6", "P10_7",
	"P11_0", "P11_1", "P11_2", "P11_3", "P11_4", "P11_5", "P11_6", "P11_7",
	"P12_0", "P12_1", "P12_2", "P12_3", "P12_4", "P12_5", "P12_6", "P12_7",
	"P13_0", "P13_1", "P13_2", "P13_3", "P13_4", "P13_5", "P13_6", "P13_7",
	"P14_0", "P14_1", "P14_2", "P14_3", "P14_4", "P14_5", "P14_6", "P14_7",
	"P15_0", "P15_1", "P15_2", "P15_3", "P15_4", "P15_5", "P15_6", "P15_7",
	"P16_0", "P16_1", "P16_2", "P16_3", "P16_4", "P16_5", "P16_6", "P16_7",
	"P17_0", "P17_1", "P17_2", "P17_3", "P17_4", "P17_5", "P17_6", "P17_7",
	"P18_0", "P18_1", "P18_2", "P18_3", "P18_4", "P18_5", "P18_6", "P18_7",
	"P19_0", "P19_1", "P19_2", "P19_3", "P19_4", "P19_5", "P19_6", "P19_7",
	"P20_0", "P20_1", "P20_2", "P20_3", "P20_4", "P20_5", "P20_6", "P20_7",
	"P21_0", "P21_1", "P21_2", "P21_3", "P21_4", "P21_5", "P21_6", "P21_7",
	"P22_0", "P22_1", "P22_2", "P22_3", "P22_4", "P22_5", "P22_6", "P22_7",
	"P23_0", "P23_1", "P23_2", "P23_3", "P23_4", "P23_5", "P23_6", "P23_7",
	"P24_0", "P24_1", "P24_2", "P24_3", "P24_4", "P24_5", "P24_6", "P24_7",
	"P25_0", "P25_1", "P25_2", "P25_3", "P25_4", "P25_5", "P25_6", "P25_7",
	"P26_0", "P26_1", "P26_2", "P26_3", "P26_4", "P26_5", "P26_6", "P26_7",
	"P27_0", "P27_1", "P27_2", "P27_3", "P27_4", "P27_5", "P27_6", "P27_7",
	"P28_0", "P28_1", "P28_2", "P28_3", "P28_4", "P28_5", "P28_6", "P28_7",
	"P29_0", "P29_1", "P29_2", "P29_3", "P29_4", "P29_5", "P29_6", "P29_7",
	"P30_0", "P30_1", "P30_2", "P30_3", "P30_4", "P30_5", "P30_6", "P30_7",
	"P31_0", "P31_1", "P31_2", "P31_3", "P31_4", "P31_5", "P31_6", "P31_7",
	"P32_0", "P32_1", "P32_2", "P32_3", "P32_4", "P32_5", "P32_6", "P32_7",
	"P33_0", "P33_1", "P33_2", "P33_3", "P33_4", "P33_5", "P33_6", "P33_7",
	"P34_0", "P34_1", "P34_2", "P34_3", "P34_4", "P34_5", "P34_6", "P34_7",
	"P35_0", "P35_1", "P35_2", "P35_3", "P35_4", "P35_5", "P35_6", "P35_7",
};

static int rzt2h_gpio_register(struct rzt2h_pinctrl *pctrl)
{
	struct pinctrl_gpio_range *range = &pctrl->gpio_range;
	struct gpio_chip *chip = &pctrl->gpio_chip;
	struct device *dev = pctrl->dev;
	struct of_phandle_args of_args;
	int ret;

	ret = of_parse_phandle_with_fixed_args(dev->of_node, "gpio-ranges", 3, 0, &of_args);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to parse gpio-ranges\n");

	if (of_args.args[0] != 0 || of_args.args[1] != 0 ||
	    of_args.args[2] != pctrl->data->n_port_pins)
		return dev_err_probe(dev, -EINVAL,
				     "gpio-ranges does not match selected SOC\n");

	chip->base = -1;
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->ngpio = of_args.args[2];
	chip->names = rzt2h_gpio_names;
	chip->request = rzt2h_gpio_request;
	chip->free = rzt2h_gpio_free;
	chip->get_direction = rzt2h_gpio_get_direction;
	chip->direction_input = rzt2h_gpio_direction_input;
	chip->direction_output = rzt2h_gpio_direction_output;
	chip->get = rzt2h_gpio_get;
	chip->set = rzt2h_gpio_set;
	chip->label = dev_name(dev);

	range->id = 0;
	range->pin_base = 0;
	range->base = 0;
	range->npins = chip->ngpio;
	range->name = chip->label;
	range->gc = chip;

	ret = devm_gpiochip_add_data(dev, chip, pctrl);
	if (ret)
		return dev_err_probe(dev, ret, "gpiochip registration failed\n");

	return ret;
}

static int rzt2h_pinctrl_register(struct rzt2h_pinctrl *pctrl)
{
	struct pinctrl_desc *desc = &pctrl->desc;
	struct device *dev = pctrl->dev;
	struct pinctrl_pin_desc *pins;
	unsigned int i, j;
	int ret;

	desc->name = DRV_NAME;
	desc->npins = pctrl->data->n_port_pins;
	desc->pctlops = &rzt2h_pinctrl_pctlops;
	desc->pmxops = &rzt2h_pinctrl_pmxops;
	desc->owner = THIS_MODULE;

	pins = devm_kcalloc(dev, desc->npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pctrl->pins = pins;
	desc->pins = pins;

	for (i = 0, j = 0; i < pctrl->data->n_port_pins; i++) {
		pins[i].number = i;
		pins[i].name = rzt2h_gpio_names[i];
		if (i && !(i % RZT2H_PINS_PER_PORT))
			j++;
	}

	ret = devm_pinctrl_register_and_init(dev, desc, pctrl, &pctrl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "pinctrl registration failed\n");

	ret = pinctrl_enable(pctrl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "pinctrl enable failed\n");

	return rzt2h_gpio_register(pctrl);
}

static int rzt2h_pinctrl_cfg_regions(struct platform_device *pdev,
				     struct rzt2h_pinctrl *pctrl)
{
	struct resource *res;

	pctrl->base0 = devm_platform_ioremap_resource_byname(pdev, "nsr");
	if (IS_ERR(pctrl->base0))
		return PTR_ERR(pctrl->base0);

	/*
	 * Open-coded instead of using devm_platform_ioremap_resource_byname()
	 * because the "srs" region is optional.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "srs");
	if (res) {
		u8 port;

		pctrl->base1 = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pctrl->base1))
			return PTR_ERR(pctrl->base1);

		pctrl->safety_port_enabled = true;

		/* Configure to select safety region 0x812c0xxx */
		for (port = 0; port <= RZT2H_MAX_SAFETY_PORTS; port++)
			writeb(0x0, pctrl->base1 + RSELP(port));
	}

	return 0;
}

static int rzt2h_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzt2h_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	pctrl->data = of_device_get_match_data(dev);

	ret = rzt2h_pinctrl_cfg_regions(pdev, pctrl);
	if (ret)
		return ret;

	spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);
	platform_set_drvdata(pdev, pctrl);

	return rzt2h_pinctrl_register(pctrl);
}

static const u8 r9a09g077_gpio_configs[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
};

static const u8 r9a09g087_gpio_configs[] = {
	0x1f, 0xff, 0xff, 0x1f, 0x00, 0xfe, 0xff, 0x00, 0x7e, 0xf0, 0xff, 0x01,
	0xff, 0xff, 0xff, 0x00, 0xe0, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x01,
	0xe0, 0xff, 0xff, 0x7f, 0x00, 0xfe, 0xff, 0x7f, 0x00, 0xfc, 0x7f,
};

static struct rzt2h_pinctrl_data r9a09g077_data = {
	.n_port_pins = ARRAY_SIZE(r9a09g077_gpio_configs) * RZT2H_PINS_PER_PORT,
	.port_pin_configs = r9a09g077_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a09g077_gpio_configs),
};

static struct rzt2h_pinctrl_data r9a09g087_data = {
	.n_port_pins = ARRAY_SIZE(r9a09g087_gpio_configs) * RZT2H_PINS_PER_PORT,
	.port_pin_configs = r9a09g087_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a09g087_gpio_configs),
};

static const struct of_device_id rzt2h_pinctrl_of_table[] = {
	{
		.compatible = "renesas,r9a09g077-pinctrl",
		.data = &r9a09g077_data,
	},
	{
		.compatible = "renesas,r9a09g087-pinctrl",
		.data = &r9a09g087_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver rzt2h_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzt2h_pinctrl_of_table),
		.suppress_bind_attrs = true,
	},
	.probe = rzt2h_pinctrl_probe,
};

static int __init rzt2h_pinctrl_init(void)
{
	return platform_driver_register(&rzt2h_pinctrl_driver);
}
core_initcall(rzt2h_pinctrl_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thierry Bultel <thierry.bultel.yh@bp.renesas.com>");
MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Pin and gpio controller driver for the RZ/T2H family");
