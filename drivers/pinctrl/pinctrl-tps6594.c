// SPDX-License-Identifier: GPL-2.0
/*
 * Pinmux and GPIO driver for tps6594 PMIC
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/module.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <linux/mfd/tps6594.h>

#define TPS6594_PINCTRL_GPIO_FUNCTION 0
#define TPS6594_PINCTRL_SCL_I2C2_CS_SPI_FUNCTION 1
#define TPS6594_PINCTRL_TRIG_WDOG_FUNCTION 1
#define TPS6594_PINCTRL_CLK32KOUT_FUNCTION 1
#define TPS6594_PINCTRL_SCLK_SPMI_FUNCTION 1
#define TPS6594_PINCTRL_SDATA_SPMI_FUNCTION 1
#define TPS6594_PINCTRL_NERR_MCU_FUNCTION 1
#define TPS6594_PINCTRL_PDOG_FUNCTION 1
#define TPS6594_PINCTRL_SYNCCLKIN_FUNCTION 1
#define TPS6594_PINCTRL_NRSTOUT_SOC_FUNCTION 2
#define TPS6594_PINCTRL_SYNCCLKOUT_FUNCTION 2
#define TPS6594_PINCTRL_SDA_I2C2_SDO_SPI_FUNCTION 2
#define TPS6594_PINCTRL_NERR_SOC_FUNCTION 2
#define TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION 3
#define TPS6594_PINCTRL_NSLEEP1_FUNCTION 4
#define TPS6594_PINCTRL_NSLEEP2_FUNCTION 5
#define TPS6594_PINCTRL_WKUP1_FUNCTION 6
#define TPS6594_PINCTRL_WKUP2_FUNCTION 7

/* Special muxval for recalcitrant pins */
#define TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION_GPIO8 2
#define TPS6594_PINCTRL_SYNCCLKOUT_FUNCTION_GPIO8 3
#define TPS6594_PINCTRL_CLK32KOUT_FUNCTION_GPIO9 3

/* TPS65224 pin muxval */
#define TPS65224_PINCTRL_SDA_I2C2_SDO_SPI_FUNCTION 1
#define TPS65224_PINCTRL_SCL_I2C2_CS_SPI_FUNCTION 1
#define TPS65224_PINCTRL_VMON1_FUNCTION 1
#define TPS65224_PINCTRL_VMON2_FUNCTION 1
#define TPS65224_PINCTRL_WKUP_FUNCTION 1
#define TPS65224_PINCTRL_NSLEEP2_FUNCTION 2
#define TPS65224_PINCTRL_NSLEEP1_FUNCTION 2
#define TPS65224_PINCTRL_SYNCCLKIN_FUNCTION 2
#define TPS65224_PINCTRL_NERR_MCU_FUNCTION 2
#define TPS65224_PINCTRL_NINT_FUNCTION 3
#define TPS65224_PINCTRL_TRIG_WDOG_FUNCTION 3
#define TPS65224_PINCTRL_PB_FUNCTION 3
#define TPS65224_PINCTRL_ADC_IN_FUNCTION 3

/* TPS65224 Special muxval for recalcitrant pins */
#define TPS65224_PINCTRL_NSLEEP2_FUNCTION_GPIO5 1
#define TPS65224_PINCTRL_WKUP_FUNCTION_GPIO5 4
#define TPS65224_PINCTRL_SYNCCLKIN_FUNCTION_GPIO5 3

#define TPS6594_OFFSET_GPIO_SEL 5

#define TPS65224_NGPIO_PER_REG 6
#define TPS6594_NGPIO_PER_REG  8

#define FUNCTION(dev_name, fname, v)							\
{											\
	.pinfunction = PINCTRL_PINFUNCTION(#fname,					\
					dev_name##_##fname##_func_group_names,		\
					ARRAY_SIZE(dev_name##_##fname##_func_group_names)),\
	.muxval = v,									\
}

static const struct pinctrl_pin_desc tps6594_pins[] = {
	PINCTRL_PIN(0, "GPIO0"),   PINCTRL_PIN(1, "GPIO1"),
	PINCTRL_PIN(2, "GPIO2"),   PINCTRL_PIN(3, "GPIO3"),
	PINCTRL_PIN(4, "GPIO4"),   PINCTRL_PIN(5, "GPIO5"),
	PINCTRL_PIN(6, "GPIO6"),   PINCTRL_PIN(7, "GPIO7"),
	PINCTRL_PIN(8, "GPIO8"),   PINCTRL_PIN(9, "GPIO9"),
	PINCTRL_PIN(10, "GPIO10"),
};

static const char *const tps6594_gpio_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
	"GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10",
};

static const char *const tps6594_nsleep1_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
	"GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10",
};

static const char *const tps6594_nsleep2_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
	"GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10",
};

static const char *const tps6594_wkup1_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
	"GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10",
};

static const char *const tps6594_wkup2_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
	"GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10",
};

static const char *const tps6594_scl_i2c2_cs_spi_func_group_names[] = {
	"GPIO0",
	"GPIO1",
};

static const char *const tps6594_nrstout_soc_func_group_names[] = {
	"GPIO0",
	"GPIO10",
};

static const char *const tps6594_trig_wdog_func_group_names[] = {
	"GPIO1",
	"GPIO10",
};

static const char *const tps6594_sda_i2c2_sdo_spi_func_group_names[] = {
	"GPIO1",
};

static const char *const tps6594_clk32kout_func_group_names[] = {
	"GPIO2",
	"GPIO3",
	"GPIO7",
};

static const char *const tps6594_nerr_soc_func_group_names[] = {
	"GPIO2",
};

static const char *const tps6594_sclk_spmi_func_group_names[] = {
	"GPIO4",
};

static const char *const tps6594_sdata_spmi_func_group_names[] = {
	"GPIO5",
};

static const char *const tps6594_nerr_mcu_func_group_names[] = {
	"GPIO6",
};

static const char *const tps6594_syncclkout_func_group_names[] = {
	"GPIO7",
	"GPIO9",
};

static const char *const tps6594_disable_wdog_func_group_names[] = {
	"GPIO7",
	"GPIO8",
};

static const char *const tps6594_pdog_func_group_names[] = {
	"GPIO8",
};

static const char *const tps6594_syncclkin_func_group_names[] = {
	"GPIO9",
};

static const struct pinctrl_pin_desc tps65224_pins[] = {
	PINCTRL_PIN(0, "GPIO0"),   PINCTRL_PIN(1, "GPIO1"),
	PINCTRL_PIN(2, "GPIO2"),   PINCTRL_PIN(3, "GPIO3"),
	PINCTRL_PIN(4, "GPIO4"),   PINCTRL_PIN(5, "GPIO5"),
};

static const char *const tps65224_gpio_func_group_names[] = {
	"GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5",
};

static const char *const tps65224_sda_i2c2_sdo_spi_func_group_names[] = {
	"GPIO0",
};

static const char *const tps65224_nsleep2_func_group_names[] = {
	"GPIO0", "GPIO5",
};

static const char *const tps65224_nint_func_group_names[] = {
	"GPIO0",
};

static const char *const tps65224_scl_i2c2_cs_spi_func_group_names[] = {
	"GPIO1",
};

static const char *const tps65224_nsleep1_func_group_names[] = {
	"GPIO1", "GPIO2", "GPIO3",
};

static const char *const tps65224_trig_wdog_func_group_names[] = {
	"GPIO1",
};

static const char *const tps65224_vmon1_func_group_names[] = {
	"GPIO2",
};

static const char *const tps65224_pb_func_group_names[] = {
	"GPIO2",
};

static const char *const tps65224_vmon2_func_group_names[] = {
	"GPIO3",
};

static const char *const tps65224_adc_in_func_group_names[] = {
	"GPIO3", "GPIO4",
};

static const char *const tps65224_wkup_func_group_names[] = {
	"GPIO4", "GPIO5",
};

static const char *const tps65224_syncclkin_func_group_names[] = {
	"GPIO4", "GPIO5",
};

static const char *const tps65224_nerr_mcu_func_group_names[] = {
	"GPIO5",
};

struct tps6594_pinctrl_function {
	struct pinfunction pinfunction;
	u8 muxval;
};

struct muxval_remap {
	unsigned int group;
	u8 muxval;
	u8 remap;
};

static struct muxval_remap tps65224_muxval_remap[] = {
	{5, TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION, TPS65224_PINCTRL_WKUP_FUNCTION_GPIO5},
	{5, TPS65224_PINCTRL_SYNCCLKIN_FUNCTION, TPS65224_PINCTRL_SYNCCLKIN_FUNCTION_GPIO5},
	{5, TPS65224_PINCTRL_NSLEEP2_FUNCTION, TPS65224_PINCTRL_NSLEEP2_FUNCTION_GPIO5},
};

static struct muxval_remap tps6594_muxval_remap[] = {
	{8, TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION, TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION_GPIO8},
	{8, TPS6594_PINCTRL_SYNCCLKOUT_FUNCTION, TPS6594_PINCTRL_SYNCCLKOUT_FUNCTION_GPIO8},
	{9, TPS6594_PINCTRL_CLK32KOUT_FUNCTION, TPS6594_PINCTRL_CLK32KOUT_FUNCTION_GPIO9},
};

static const struct tps6594_pinctrl_function pinctrl_functions[] = {
	FUNCTION(tps6594, gpio, TPS6594_PINCTRL_GPIO_FUNCTION),
	FUNCTION(tps6594, nsleep1, TPS6594_PINCTRL_NSLEEP1_FUNCTION),
	FUNCTION(tps6594, nsleep2, TPS6594_PINCTRL_NSLEEP2_FUNCTION),
	FUNCTION(tps6594, wkup1, TPS6594_PINCTRL_WKUP1_FUNCTION),
	FUNCTION(tps6594, wkup2, TPS6594_PINCTRL_WKUP2_FUNCTION),
	FUNCTION(tps6594, scl_i2c2_cs_spi, TPS6594_PINCTRL_SCL_I2C2_CS_SPI_FUNCTION),
	FUNCTION(tps6594, nrstout_soc, TPS6594_PINCTRL_NRSTOUT_SOC_FUNCTION),
	FUNCTION(tps6594, trig_wdog, TPS6594_PINCTRL_TRIG_WDOG_FUNCTION),
	FUNCTION(tps6594, sda_i2c2_sdo_spi, TPS6594_PINCTRL_SDA_I2C2_SDO_SPI_FUNCTION),
	FUNCTION(tps6594, clk32kout, TPS6594_PINCTRL_CLK32KOUT_FUNCTION),
	FUNCTION(tps6594, nerr_soc, TPS6594_PINCTRL_NERR_SOC_FUNCTION),
	FUNCTION(tps6594, sclk_spmi, TPS6594_PINCTRL_SCLK_SPMI_FUNCTION),
	FUNCTION(tps6594, sdata_spmi, TPS6594_PINCTRL_SDATA_SPMI_FUNCTION),
	FUNCTION(tps6594, nerr_mcu, TPS6594_PINCTRL_NERR_MCU_FUNCTION),
	FUNCTION(tps6594, syncclkout, TPS6594_PINCTRL_SYNCCLKOUT_FUNCTION),
	FUNCTION(tps6594, disable_wdog, TPS6594_PINCTRL_DISABLE_WDOG_FUNCTION),
	FUNCTION(tps6594, pdog, TPS6594_PINCTRL_PDOG_FUNCTION),
	FUNCTION(tps6594, syncclkin, TPS6594_PINCTRL_SYNCCLKIN_FUNCTION),
};

static const struct tps6594_pinctrl_function tps65224_pinctrl_functions[] = {
	FUNCTION(tps65224, gpio, TPS6594_PINCTRL_GPIO_FUNCTION),
	FUNCTION(tps65224, sda_i2c2_sdo_spi, TPS65224_PINCTRL_SDA_I2C2_SDO_SPI_FUNCTION),
	FUNCTION(tps65224, nsleep2, TPS65224_PINCTRL_NSLEEP2_FUNCTION),
	FUNCTION(tps65224, nint, TPS65224_PINCTRL_NINT_FUNCTION),
	FUNCTION(tps65224, scl_i2c2_cs_spi, TPS65224_PINCTRL_SCL_I2C2_CS_SPI_FUNCTION),
	FUNCTION(tps65224, nsleep1, TPS65224_PINCTRL_NSLEEP1_FUNCTION),
	FUNCTION(tps65224, trig_wdog, TPS65224_PINCTRL_TRIG_WDOG_FUNCTION),
	FUNCTION(tps65224, vmon1, TPS65224_PINCTRL_VMON1_FUNCTION),
	FUNCTION(tps65224, pb, TPS65224_PINCTRL_PB_FUNCTION),
	FUNCTION(tps65224, vmon2, TPS65224_PINCTRL_VMON2_FUNCTION),
	FUNCTION(tps65224, adc_in, TPS65224_PINCTRL_ADC_IN_FUNCTION),
	FUNCTION(tps65224, wkup, TPS65224_PINCTRL_WKUP_FUNCTION),
	FUNCTION(tps65224, syncclkin, TPS65224_PINCTRL_SYNCCLKIN_FUNCTION),
	FUNCTION(tps65224, nerr_mcu, TPS65224_PINCTRL_NERR_MCU_FUNCTION),
};

struct tps6594_pinctrl {
	struct tps6594 *tps;
	struct gpio_regmap *gpio_regmap;
	struct pinctrl_dev *pctl_dev;
	const struct tps6594_pinctrl_function *funcs;
	const struct pinctrl_pin_desc *pins;
	int func_cnt;
	int num_pins;
	u8 mux_sel_mask;
	unsigned int remap_cnt;
	struct muxval_remap *remap;
};

static struct tps6594_pinctrl tps65224_template_pinctrl = {
	.funcs = tps65224_pinctrl_functions,
	.func_cnt = ARRAY_SIZE(tps65224_pinctrl_functions),
	.pins = tps65224_pins,
	.num_pins = ARRAY_SIZE(tps65224_pins),
	.mux_sel_mask = TPS65224_MASK_GPIO_SEL,
	.remap = tps65224_muxval_remap,
	.remap_cnt = ARRAY_SIZE(tps65224_muxval_remap),
};

static struct tps6594_pinctrl tps6594_template_pinctrl = {
	.funcs = pinctrl_functions,
	.func_cnt = ARRAY_SIZE(pinctrl_functions),
	.pins = tps6594_pins,
	.num_pins = ARRAY_SIZE(tps6594_pins),
	.mux_sel_mask = TPS6594_MASK_GPIO_SEL,
	.remap = tps6594_muxval_remap,
	.remap_cnt = ARRAY_SIZE(tps6594_muxval_remap),
};

static int tps6594_gpio_regmap_xlate(struct gpio_regmap *gpio,
				     unsigned int base, unsigned int offset,
				     unsigned int *reg, unsigned int *mask)
{
	unsigned int line = offset % 8;
	unsigned int stride = offset / 8;

	switch (base) {
	case TPS6594_REG_GPIOX_CONF(0):
		*reg = TPS6594_REG_GPIOX_CONF(offset);
		*mask = TPS6594_BIT_GPIO_DIR;
		return 0;
	case TPS6594_REG_GPIO_IN_1:
	case TPS6594_REG_GPIO_OUT_1:
		*reg = base + stride;
		*mask = BIT(line);
		return 0;
	default:
		return -EINVAL;
	}
}

static int tps6594_pmx_func_cnt(struct pinctrl_dev *pctldev)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->func_cnt;
}

static const char *tps6594_pmx_func_name(struct pinctrl_dev *pctldev,
					 unsigned int selector)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->funcs[selector].pinfunction.name;
}

static int tps6594_pmx_func_groups(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   const char *const **groups,
				   unsigned int *num_groups)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pinctrl->funcs[selector].pinfunction.groups;
	*num_groups = pinctrl->funcs[selector].pinfunction.ngroups;

	return 0;
}

static int tps6594_pmx_set(struct tps6594_pinctrl *pinctrl, unsigned int pin,
			   u8 muxval)
{
	u8 mux_sel_val = muxval << TPS6594_OFFSET_GPIO_SEL;
	u8 mux_sel_mask = pinctrl->mux_sel_mask;

	if (pinctrl->tps->chip_id == TPS65224 && pin == 5) {
		/* GPIO6 has a different mask in TPS65224*/
		mux_sel_mask = TPS65224_MASK_GPIO_SEL_GPIO6;
	}

	return regmap_update_bits(pinctrl->tps->regmap,
				  TPS6594_REG_GPIOX_CONF(pin),
				  mux_sel_mask, mux_sel_val);
}

static int tps6594_pmx_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int function, unsigned int group)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	u8 muxval = pinctrl->funcs[function].muxval;
	unsigned int remap_cnt = pinctrl->remap_cnt;
	struct muxval_remap *remap = pinctrl->remap;

	for (unsigned int i = 0; i < remap_cnt; i++) {
		if (group == remap[i].group && muxval == remap[i].muxval) {
			muxval = remap[i].remap;
			break;
		}
	}

	return tps6594_pmx_set(pinctrl, group, muxval);
}

static int tps6594_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					  struct pinctrl_gpio_range *range,
					  unsigned int offset, bool input)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	u8 muxval = pinctrl->funcs[TPS6594_PINCTRL_GPIO_FUNCTION].muxval;

	return tps6594_pmx_set(pinctrl, offset, muxval);
}

static const struct pinmux_ops tps6594_pmx_ops = {
	.get_functions_count = tps6594_pmx_func_cnt,
	.get_function_name = tps6594_pmx_func_name,
	.get_function_groups = tps6594_pmx_func_groups,
	.set_mux = tps6594_pmx_set_mux,
	.gpio_set_direction = tps6594_pmx_gpio_set_direction,
	.strict = true,
};

static int tps6594_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->num_pins;
}

static int tps6594_group_pins(struct pinctrl_dev *pctldev,
			      unsigned int selector, const unsigned int **pins,
			      unsigned int *num_pins)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pinctrl->pins[selector].number;
	*num_pins = 1;

	return 0;
}

static const char *tps6594_group_name(struct pinctrl_dev *pctldev,
				      unsigned int selector)
{
	struct tps6594_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return pinctrl->pins[selector].name;
}

static const struct pinctrl_ops tps6594_pctrl_ops = {
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinconf_generic_dt_free_map,
	.get_groups_count = tps6594_groups_cnt,
	.get_group_name = tps6594_group_name,
	.get_group_pins = tps6594_group_pins,
};

static int tps6594_pinctrl_probe(struct platform_device *pdev)
{
	struct tps6594 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct tps6594_pinctrl *pinctrl;
	struct pinctrl_desc *pctrl_desc;
	struct gpio_regmap_config config = {};

	pctrl_desc = devm_kzalloc(dev, sizeof(*pctrl_desc), GFP_KERNEL);
	if (!pctrl_desc)
		return -ENOMEM;

	pinctrl = devm_kzalloc(dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	switch (tps->chip_id) {
	case TPS65224:
		pctrl_desc->pins = tps65224_pins;
		pctrl_desc->npins = ARRAY_SIZE(tps65224_pins);

		*pinctrl = tps65224_template_pinctrl;

		config.ngpio = ARRAY_SIZE(tps65224_gpio_func_group_names);
		config.ngpio_per_reg = TPS65224_NGPIO_PER_REG;
		break;
	case TPS6593:
	case TPS6594:
	case LP8764:
		pctrl_desc->pins = tps6594_pins;
		pctrl_desc->npins = ARRAY_SIZE(tps6594_pins);

		*pinctrl = tps6594_template_pinctrl;

		config.ngpio = ARRAY_SIZE(tps6594_gpio_func_group_names);
		config.ngpio_per_reg = TPS6594_NGPIO_PER_REG;
		break;
	default:
		break;
	}

	pinctrl->tps = tps;

	pctrl_desc->name = dev_name(dev);
	pctrl_desc->owner = THIS_MODULE;
	pctrl_desc->pctlops = &tps6594_pctrl_ops;
	pctrl_desc->pmxops = &tps6594_pmx_ops;

	config.parent = tps->dev;
	config.regmap = tps->regmap;
	config.reg_dat_base = TPS6594_REG_GPIO_IN_1;
	config.reg_set_base = TPS6594_REG_GPIO_OUT_1;
	config.reg_dir_out_base = TPS6594_REG_GPIOX_CONF(0);
	config.reg_mask_xlate = tps6594_gpio_regmap_xlate;

	pinctrl->pctl_dev = devm_pinctrl_register(dev, pctrl_desc, pinctrl);
	if (IS_ERR(pinctrl->pctl_dev))
		return dev_err_probe(dev, PTR_ERR(pinctrl->pctl_dev),
				     "Couldn't register pinctrl driver\n");

	pinctrl->gpio_regmap = devm_gpio_regmap_register(dev, &config);
	if (IS_ERR(pinctrl->gpio_regmap))
		return dev_err_probe(dev, PTR_ERR(pinctrl->gpio_regmap),
				     "Couldn't register gpio_regmap driver\n");

	return 0;
}

static const struct platform_device_id tps6594_pinctrl_id_table[] = {
	{ "tps6594-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(platform, tps6594_pinctrl_id_table);

static struct platform_driver tps6594_pinctrl_driver = {
	.probe = tps6594_pinctrl_probe,
	.driver = {
		.name = "tps6594-pinctrl",
	},
	.id_table = tps6594_pinctrl_id_table,
};
module_platform_driver(tps6594_pinctrl_driver);

MODULE_AUTHOR("Esteban Blanc <eblanc@baylibre.com>");
MODULE_AUTHOR("Nirmala Devi Mal Nadar <m.nirmaladevi@ltts.com>");
MODULE_DESCRIPTION("TPS6594 pinctrl and GPIO driver");
MODULE_LICENSE("GPL");
