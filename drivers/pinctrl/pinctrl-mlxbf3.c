// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause
/* Copyright (C) 2022 NVIDIA CORPORATION & AFFILIATES */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#define MLXBF3_NGPIOS_GPIO0    32
#define MLXBF3_MAX_GPIO_PINS   56

enum {
	MLXBF3_GPIO_HW_MODE,
	MLXBF3_GPIO_SW_MODE,
};

struct mlxbf3_pinctrl {
	void __iomem *fw_ctrl_set0;
	void __iomem *fw_ctrl_clr0;
	void __iomem *fw_ctrl_set1;
	void __iomem *fw_ctrl_clr1;
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_gpio_range gpio_range;
};

#define MLXBF3_GPIO_RANGE(_id, _pinbase, _gpiobase, _npins)	\
	{							\
		.name = "mlxbf3_gpio_range",			\
		.id = _id,					\
		.base = _gpiobase,				\
		.pin_base = _pinbase,				\
		.npins = _npins,				\
	}

static struct pinctrl_gpio_range mlxbf3_pinctrl_gpio_ranges[] = {
	MLXBF3_GPIO_RANGE(0, 0,  480, 32),
	MLXBF3_GPIO_RANGE(1,  32, 456, 24),
};

static const struct pinctrl_pin_desc mlxbf3_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
	PINCTRL_PIN(32, "gpio32"),
	PINCTRL_PIN(33, "gpio33"),
	PINCTRL_PIN(34, "gpio34"),
	PINCTRL_PIN(35, "gpio35"),
	PINCTRL_PIN(36, "gpio36"),
	PINCTRL_PIN(37, "gpio37"),
	PINCTRL_PIN(38, "gpio38"),
	PINCTRL_PIN(39, "gpio39"),
	PINCTRL_PIN(40, "gpio40"),
	PINCTRL_PIN(41, "gpio41"),
	PINCTRL_PIN(42, "gpio42"),
	PINCTRL_PIN(43, "gpio43"),
	PINCTRL_PIN(44, "gpio44"),
	PINCTRL_PIN(45, "gpio45"),
	PINCTRL_PIN(46, "gpio46"),
	PINCTRL_PIN(47, "gpio47"),
	PINCTRL_PIN(48, "gpio48"),
	PINCTRL_PIN(49, "gpio49"),
	PINCTRL_PIN(50, "gpio50"),
	PINCTRL_PIN(51, "gpio51"),
	PINCTRL_PIN(52, "gpio52"),
	PINCTRL_PIN(53, "gpio53"),
	PINCTRL_PIN(54, "gpio54"),
	PINCTRL_PIN(55, "gpio55"),
};

/*
 * All single-pin functions can be mapped to any GPIO, however pinmux applies
 * functions to pin groups and only those groups declared as supporting that
 * function. To make this work we must put each pin in its own dummy group so
 * that the functions can be described as applying to all pins.
 * We use the same name as in the datasheet.
 */
static const char * const mlxbf3_pinctrl_single_group_names[] = {
	"gpio0", "gpio1",  "gpio2",  "gpio3",  "gpio4",  "gpio5",  "gpio6", "gpio7",
	"gpio8",  "gpio9",  "gpio10", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15",
	"gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31",
	"gpio32", "gpio33", "gpio34", "gpio35", "gpio36", "gpio37", "gpio38", "gpio39",
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47",
	"gpio48", "gpio49", "gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55",
};

static int mlxbf3_get_groups_count(struct pinctrl_dev *pctldev)
{
	/* Number single-pin groups */
	return MLXBF3_MAX_GPIO_PINS;
}

static const char *mlxbf3_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned int selector)
{
	return mlxbf3_pinctrl_single_group_names[selector];
}

static int mlxbf3_get_group_pins(struct pinctrl_dev *pctldev,
				 unsigned int selector,
				 const unsigned int **pins,
				 unsigned int *num_pins)
{
	/* return the dummy group for a single pin */
	*pins = &selector;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops mlxbf3_pinctrl_group_ops = {
	.get_groups_count = mlxbf3_get_groups_count,
	.get_group_name = mlxbf3_get_group_name,
	.get_group_pins = mlxbf3_get_group_pins,
};

/*
 * Only 2 functions are supported and they apply to all pins:
 * 1) Default hardware functionality
 * 2) Software controlled GPIO
 */
static const char * const mlxbf3_gpiofunc_group_names[] = { "swctrl" };
static const char * const mlxbf3_hwfunc_group_names[]   = { "hwctrl" };

static struct pinfunction mlxbf3_pmx_funcs[] = {
	PINCTRL_PINFUNCTION("hwfunc", mlxbf3_hwfunc_group_names, 1),
	PINCTRL_PINFUNCTION("gpiofunc", mlxbf3_gpiofunc_group_names, 1),
};

static int mlxbf3_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mlxbf3_pmx_funcs);
}

static const char *mlxbf3_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	return mlxbf3_pmx_funcs[selector].name;
}

static int mlxbf3_pmx_get_groups(struct pinctrl_dev *pctldev,
				 unsigned int selector,
				 const char * const **groups,
				 unsigned int * const num_groups)
{
	*groups = mlxbf3_pmx_funcs[selector].groups;
	*num_groups = MLXBF3_MAX_GPIO_PINS;

	return 0;
}

static int mlxbf3_pmx_set(struct pinctrl_dev *pctldev,
			      unsigned int selector,
			      unsigned int group)
{
	struct mlxbf3_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	if (selector == MLXBF3_GPIO_HW_MODE) {
		if (group < MLXBF3_NGPIOS_GPIO0)
			writel(BIT(group), priv->fw_ctrl_clr0);
		else
			writel(BIT(group % MLXBF3_NGPIOS_GPIO0), priv->fw_ctrl_clr1);
	}

	if (selector == MLXBF3_GPIO_SW_MODE) {
		if (group < MLXBF3_NGPIOS_GPIO0)
			writel(BIT(group), priv->fw_ctrl_set0);
		else
			writel(BIT(group % MLXBF3_NGPIOS_GPIO0), priv->fw_ctrl_set1);
	}

	return 0;
}

static int mlxbf3_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int offset)
{
	struct mlxbf3_pinctrl *priv = pinctrl_dev_get_drvdata(pctldev);

	if (offset < MLXBF3_NGPIOS_GPIO0)
		writel(BIT(offset), priv->fw_ctrl_set0);
	else
		writel(BIT(offset % MLXBF3_NGPIOS_GPIO0), priv->fw_ctrl_set1);

	return 0;
}

static const struct pinmux_ops mlxbf3_pmx_ops = {
	.get_functions_count = mlxbf3_pmx_get_funcs_count,
	.get_function_name = mlxbf3_pmx_get_func_name,
	.get_function_groups = mlxbf3_pmx_get_groups,
	.set_mux = mlxbf3_pmx_set,
	.gpio_request_enable = mlxbf3_gpio_request_enable,
};

static const struct pinctrl_desc mlxbf3_pin_desc = {
	.name = "pinctrl-mlxbf3",
	.pins = mlxbf3_pins,
	.npins = ARRAY_SIZE(mlxbf3_pins),
	.pctlops = &mlxbf3_pinctrl_group_ops,
	.pmxops = &mlxbf3_pmx_ops,
	.owner = THIS_MODULE,
};

static_assert(ARRAY_SIZE(mlxbf3_pinctrl_single_group_names) == MLXBF3_MAX_GPIO_PINS);

static int mlxbf3_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mlxbf3_pinctrl *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	priv->fw_ctrl_set0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->fw_ctrl_set0))
		return PTR_ERR(priv->fw_ctrl_set0);

	priv->fw_ctrl_clr0 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->fw_ctrl_clr0))
		return PTR_ERR(priv->fw_ctrl_clr0);

	priv->fw_ctrl_set1 = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(priv->fw_ctrl_set1))
		return PTR_ERR(priv->fw_ctrl_set1);

	priv->fw_ctrl_clr1 = devm_platform_ioremap_resource(pdev, 3);
	if (IS_ERR(priv->fw_ctrl_clr1))
		return PTR_ERR(priv->fw_ctrl_clr1);

	ret = devm_pinctrl_register_and_init(dev,
					     &mlxbf3_pin_desc,
					     priv,
					     &priv->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pinctrl\n");

	ret = pinctrl_enable(priv->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pinctrl\n");

	pinctrl_add_gpio_ranges(priv->pctl, mlxbf3_pinctrl_gpio_ranges, 2);

	return 0;
}

static const struct acpi_device_id mlxbf3_pinctrl_acpi_ids[] = {
	{ "MLNXBF34", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, mlxbf3_pinctrl_acpi_ids);

static struct platform_driver mlxbf3_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-mlxbf3",
		.acpi_match_table = mlxbf3_pinctrl_acpi_ids,
	},
	.probe = mlxbf3_pinctrl_probe,
};
module_platform_driver(mlxbf3_pinctrl_driver);

MODULE_DESCRIPTION("NVIDIA pinctrl driver");
MODULE_AUTHOR("Asmaa Mnebhi <asmaa@nvidia.com>");
MODULE_LICENSE("Dual BSD/GPL");
