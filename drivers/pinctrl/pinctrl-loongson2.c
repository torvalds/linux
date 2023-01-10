// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: zhanghongchen <zhanghongchen@loongson.cn>
 *         Yinbo Zhu <zhuyinbo@loongson.cn>
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/seq_file.h>

#include "core.h"
#include "pinctrl-utils.h"

#define PMX_GROUP(name, offset, bitv)					\
	{								\
		.grp = PINCTRL_PINGROUP((#name), (name ## _pins),	\
				ARRAY_SIZE((name ## _pins))),		\
		.reg = offset,						\
		.bit = bitv,						\
	}

#define SPECIFIC_GROUP(group)						\
	static const char * const group##_groups[] = {			\
		#group							\
	}

#define FUNCTION(fn)							\
	{								\
		.name = #fn,						\
		.groups = fn ## _groups,				\
		.num_groups = ARRAY_SIZE(fn ## _groups),		\
	}

struct loongson2_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pcdev;
	struct pinctrl_desc desc;
	struct device_node *of_node;
	spinlock_t lock;
	void __iomem *reg_base;
};

struct loongson2_pmx_group {
	struct pingroup grp;
	unsigned int reg;
	unsigned int bit;
};

struct loongson2_pmx_func {
	const char *name;
	const char * const *groups;
	unsigned int num_groups;
};

#define LOONGSON2_PIN(x) PINCTRL_PIN(x, "gpio"#x)
static const struct pinctrl_pin_desc loongson2_pctrl_pins[] = {
	LOONGSON2_PIN(0),  LOONGSON2_PIN(1),  LOONGSON2_PIN(2),  LOONGSON2_PIN(3),
	LOONGSON2_PIN(4),  LOONGSON2_PIN(5),  LOONGSON2_PIN(6),  LOONGSON2_PIN(7),
	LOONGSON2_PIN(8),  LOONGSON2_PIN(9),  LOONGSON2_PIN(10), LOONGSON2_PIN(11),
	LOONGSON2_PIN(12), LOONGSON2_PIN(13), LOONGSON2_PIN(14),
	LOONGSON2_PIN(16), LOONGSON2_PIN(17), LOONGSON2_PIN(18), LOONGSON2_PIN(19),
	LOONGSON2_PIN(20), LOONGSON2_PIN(21), LOONGSON2_PIN(22), LOONGSON2_PIN(23),
	LOONGSON2_PIN(24), LOONGSON2_PIN(25), LOONGSON2_PIN(26), LOONGSON2_PIN(27),
	LOONGSON2_PIN(28), LOONGSON2_PIN(29), LOONGSON2_PIN(30),
	LOONGSON2_PIN(32), LOONGSON2_PIN(33), LOONGSON2_PIN(34), LOONGSON2_PIN(35),
	LOONGSON2_PIN(36), LOONGSON2_PIN(37), LOONGSON2_PIN(38), LOONGSON2_PIN(39),
	LOONGSON2_PIN(40), LOONGSON2_PIN(41),
	LOONGSON2_PIN(44), LOONGSON2_PIN(45), LOONGSON2_PIN(46), LOONGSON2_PIN(47),
	LOONGSON2_PIN(48), LOONGSON2_PIN(49), LOONGSON2_PIN(50), LOONGSON2_PIN(51),
	LOONGSON2_PIN(52), LOONGSON2_PIN(53), LOONGSON2_PIN(54), LOONGSON2_PIN(55),
	LOONGSON2_PIN(56), LOONGSON2_PIN(57), LOONGSON2_PIN(58), LOONGSON2_PIN(59),
	LOONGSON2_PIN(60), LOONGSON2_PIN(61), LOONGSON2_PIN(62), LOONGSON2_PIN(63),
};

static const unsigned int gpio_pins[] = {0, 1, 2, 3, 4, 5, 6, 7,
					 8, 9, 10, 11, 12, 13, 14,
					 16, 17, 18, 19, 20, 21, 22, 23,
					 24, 25, 26, 27, 28, 29, 30,
					 32, 33, 34, 35, 36, 37, 38, 39,
					 40,         43, 44, 45, 46, 47,
					 48, 49, 50, 51, 52, 53, 46, 55,
					 56, 57, 58, 59, 60, 61, 62, 63};
static const unsigned int sdio_pins[] = {36, 37, 38, 39, 40, 41};
static const unsigned int can1_pins[] = {34, 35};
static const unsigned int can0_pins[] = {32, 33};
static const unsigned int pwm3_pins[] = {23};
static const unsigned int pwm2_pins[] = {22};
static const unsigned int pwm1_pins[] = {21};
static const unsigned int pwm0_pins[] = {20};
static const unsigned int i2c1_pins[] = {18, 19};
static const unsigned int i2c0_pins[] = {16, 17};
static const unsigned int nand_pins[] = {44, 45, 46, 47, 48, 49, 50, 51,
					 52, 53, 54, 55, 56, 57, 58, 59, 60,
					 61, 62, 63};
static const unsigned int sata_led_pins[] = {14};
static const unsigned int i2s_pins[]    = {24, 25, 26, 27, 28};
static const unsigned int hda_pins[]    = {24, 25, 26, 27, 28, 29, 30};

static struct loongson2_pmx_group loongson2_pmx_groups[] = {
	PMX_GROUP(gpio, 0x0, 64),
	PMX_GROUP(sdio, 0x0, 20),
	PMX_GROUP(can1, 0x0, 17),
	PMX_GROUP(can0, 0x0, 16),
	PMX_GROUP(pwm3, 0x0, 15),
	PMX_GROUP(pwm2, 0x0, 14),
	PMX_GROUP(pwm1, 0x0, 13),
	PMX_GROUP(pwm0, 0x0, 12),
	PMX_GROUP(i2c1, 0x0, 11),
	PMX_GROUP(i2c0, 0x0, 10),
	PMX_GROUP(nand, 0x0, 9),
	PMX_GROUP(sata_led, 0x0, 8),
	PMX_GROUP(i2s, 0x0, 6),
	PMX_GROUP(hda, 0x0, 4),
};

SPECIFIC_GROUP(sdio);
SPECIFIC_GROUP(can1);
SPECIFIC_GROUP(can0);
SPECIFIC_GROUP(pwm3);
SPECIFIC_GROUP(pwm2);
SPECIFIC_GROUP(pwm1);
SPECIFIC_GROUP(pwm0);
SPECIFIC_GROUP(i2c1);
SPECIFIC_GROUP(i2c0);
SPECIFIC_GROUP(nand);
SPECIFIC_GROUP(sata_led);
SPECIFIC_GROUP(i2s);
SPECIFIC_GROUP(hda);

static const char * const gpio_groups[] = {
	"sdio",
	"can1", "can0",
	"pwm3", "pwm2", "pwm1", "pwm0",
	"i2c1", "i2c0",
	"nand",
	"sata_led",
	"i2s",
	"hda",
};

static const struct loongson2_pmx_func loongson2_pmx_functions[] = {
	FUNCTION(gpio),
	FUNCTION(sdio),
	FUNCTION(can1),
	FUNCTION(can0),
	FUNCTION(pwm3),
	FUNCTION(pwm2),
	FUNCTION(pwm1),
	FUNCTION(pwm0),
	FUNCTION(i2c1),
	FUNCTION(i2c0),
	FUNCTION(nand),
	FUNCTION(sata_led),
	FUNCTION(i2s),
	FUNCTION(hda),
};

static int loongson2_get_groups_count(struct pinctrl_dev *pcdev)
{
	return ARRAY_SIZE(loongson2_pmx_groups);
}

static const char *loongson2_get_group_name(struct pinctrl_dev *pcdev,
					unsigned int selector)
{
	return loongson2_pmx_groups[selector].grp.name;
}

static int loongson2_get_group_pins(struct pinctrl_dev *pcdev, unsigned int selector,
			const unsigned int **pins, unsigned int *num_pins)
{
	*pins = loongson2_pmx_groups[selector].grp.pins;
	*num_pins = loongson2_pmx_groups[selector].grp.npins;

	return 0;
}

static void loongson2_pin_dbg_show(struct pinctrl_dev *pcdev, struct seq_file *s,
			       unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pcdev->dev));
}

static const struct pinctrl_ops loongson2_pctrl_ops = {
	.get_groups_count	= loongson2_get_groups_count,
	.get_group_name		= loongson2_get_group_name,
	.get_group_pins		= loongson2_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinctrl_utils_free_map,
	.pin_dbg_show		= loongson2_pin_dbg_show,
};

static int loongson2_pmx_set_mux(struct pinctrl_dev *pcdev, unsigned int func_num,
			      unsigned int group_num)
{
	struct loongson2_pinctrl *pctrl = pinctrl_dev_get_drvdata(pcdev);
	void __iomem *reg = pctrl->reg_base +
				loongson2_pmx_groups[group_num].reg;
	unsigned int mux_bit = loongson2_pmx_groups[group_num].bit;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);
	val = readl(reg);
	if (func_num == 0)
		val &= ~BIT(mux_bit);
	else
		val |= BIT(mux_bit);
	writel(val, reg);
	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int loongson2_pmx_get_funcs_count(struct pinctrl_dev *pcdev)
{
	return ARRAY_SIZE(loongson2_pmx_functions);
}

static const char *loongson2_pmx_get_func_name(struct pinctrl_dev *pcdev,
				    unsigned int selector)
{
	return loongson2_pmx_functions[selector].name;
}

static int loongson2_pmx_get_groups(struct pinctrl_dev *pcdev,
			 unsigned int selector,
			 const char * const **groups,
			 unsigned int * const num_groups)
{
	*groups = loongson2_pmx_functions[selector].groups;
	*num_groups = loongson2_pmx_functions[selector].num_groups;

	return 0;
}

static const struct pinmux_ops loongson2_pmx_ops = {
	.set_mux = loongson2_pmx_set_mux,
	.get_functions_count = loongson2_pmx_get_funcs_count,
	.get_function_name = loongson2_pmx_get_func_name,
	.get_function_groups = loongson2_pmx_get_groups,
};

static int loongson2_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct loongson2_pinctrl *pctrl;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->reg_base))
		return PTR_ERR(pctrl->reg_base);

	spin_lock_init(&pctrl->lock);

	pctrl->dev = dev;
	pctrl->desc.name	= "pinctrl-loongson2";
	pctrl->desc.owner	= THIS_MODULE;
	pctrl->desc.pctlops	= &loongson2_pctrl_ops;
	pctrl->desc.pmxops	= &loongson2_pmx_ops;
	pctrl->desc.pins	= loongson2_pctrl_pins;
	pctrl->desc.npins	= ARRAY_SIZE(loongson2_pctrl_pins);

	pctrl->pcdev = devm_pinctrl_register(pctrl->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pcdev))
		return dev_err_probe(pctrl->dev, PTR_ERR(pctrl->pcdev),
				     "can't register pinctrl device");

	return 0;
}

static const struct of_device_id loongson2_pinctrl_dt_match[] = {
	{
		.compatible = "loongson,ls2k-pinctrl",
	},
	{ }
};

static struct platform_driver loongson2_pinctrl_driver = {
	.probe		= loongson2_pinctrl_probe,
	.driver = {
		.name	= "loongson2-pinctrl",
		.of_match_table = loongson2_pinctrl_dt_match,
	},
};

static int __init loongson2_pinctrl_init(void)
{
	return platform_driver_register(&loongson2_pinctrl_driver);
}
arch_initcall(loongson2_pinctrl_init);

static void __exit loongson2_pinctrl_exit(void)
{
	platform_driver_unregister(&loongson2_pinctrl_driver);
}
module_exit(loongson2_pinctrl_exit);

MODULE_DESCRIPTION("Loongson2 Pinctrl driver");
MODULE_LICENSE("GPL");
