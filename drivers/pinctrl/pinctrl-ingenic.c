// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ingenic SoCs pinctrl driver
 *
 * Copyright (c) 2017 Paul Cercueil <paul@crapouillou.net>
 * Copyright (c) 2017, 2019 Paul Boddie <paul@boddie.org.uk>
 * Copyright (c) 2019, 2020 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"

#define GPIO_PIN					0x00
#define GPIO_MSK					0x20

#define JZ4730_GPIO_DATA			0x00
#define JZ4730_GPIO_GPDIR			0x04
#define JZ4730_GPIO_GPPUR			0x0c
#define JZ4730_GPIO_GPALR			0x10
#define JZ4730_GPIO_GPAUR			0x14
#define JZ4730_GPIO_GPIDLR			0x18
#define JZ4730_GPIO_GPIDUR			0x1c
#define JZ4730_GPIO_GPIER			0x20
#define JZ4730_GPIO_GPIMR			0x24
#define JZ4730_GPIO_GPFR			0x28

#define JZ4740_GPIO_DATA			0x10
#define JZ4740_GPIO_PULL_DIS		0x30
#define JZ4740_GPIO_FUNC			0x40
#define JZ4740_GPIO_SELECT			0x50
#define JZ4740_GPIO_DIR				0x60
#define JZ4740_GPIO_TRIG			0x70
#define JZ4740_GPIO_FLAG			0x80

#define JZ4770_GPIO_INT				0x10
#define JZ4770_GPIO_PAT1			0x30
#define JZ4770_GPIO_PAT0			0x40
#define JZ4770_GPIO_FLAG			0x50
#define JZ4770_GPIO_PEN				0x70

#define X1830_GPIO_PEL				0x110
#define X1830_GPIO_PEH				0x120
#define X1830_GPIO_SR				0x150
#define X1830_GPIO_SMT				0x160

#define X2000_GPIO_EDG				0x70
#define X2000_GPIO_PEPU				0x80
#define X2000_GPIO_PEPD				0x90
#define X2000_GPIO_SR				0xd0
#define X2000_GPIO_SMT				0xe0

#define REG_SET(x)					((x) + 0x4)
#define REG_CLEAR(x)				((x) + 0x8)

#define REG_PZ_BASE(x)				((x) * 7)
#define REG_PZ_GID2LD(x)			((x) * 7 + 0xf0)

#define GPIO_PULL_DIS				0
#define GPIO_PULL_UP				1
#define GPIO_PULL_DOWN				2

#define PINS_PER_GPIO_CHIP			32
#define JZ4730_PINS_PER_PAIRED_REG	16

#define INGENIC_PIN_GROUP_FUNCS(name, id, funcs)		\
	{						\
		name,					\
		id##_pins,				\
		ARRAY_SIZE(id##_pins),			\
		funcs,					\
	}

#define INGENIC_PIN_GROUP(name, id, func)		\
	INGENIC_PIN_GROUP_FUNCS(name, id, (void *)(func))

enum jz_version {
	ID_JZ4730,
	ID_JZ4740,
	ID_JZ4725B,
	ID_JZ4750,
	ID_JZ4755,
	ID_JZ4760,
	ID_JZ4770,
	ID_JZ4775,
	ID_JZ4780,
	ID_X1000,
	ID_X1500,
	ID_X1830,
	ID_X2000,
	ID_X2100,
};

struct ingenic_chip_info {
	unsigned int num_chips;
	unsigned int reg_offset;
	enum jz_version version;

	const struct group_desc *groups;
	unsigned int num_groups;

	const struct function_desc *functions;
	unsigned int num_functions;

	const u32 *pull_ups, *pull_downs;

	const struct regmap_access_table *access_table;
};

struct ingenic_pinctrl {
	struct device *dev;
	struct regmap *map;
	struct pinctrl_dev *pctl;
	struct pinctrl_pin_desc *pdesc;

	const struct ingenic_chip_info *info;
};

struct ingenic_gpio_chip {
	struct ingenic_pinctrl *jzpc;
	struct gpio_chip gc;
	unsigned int irq, reg_base;
};

static const unsigned long enabled_socs =
	IS_ENABLED(CONFIG_MACH_JZ4730) << ID_JZ4730 |
	IS_ENABLED(CONFIG_MACH_JZ4740) << ID_JZ4740 |
	IS_ENABLED(CONFIG_MACH_JZ4725B) << ID_JZ4725B |
	IS_ENABLED(CONFIG_MACH_JZ4750) << ID_JZ4750 |
	IS_ENABLED(CONFIG_MACH_JZ4755) << ID_JZ4755 |
	IS_ENABLED(CONFIG_MACH_JZ4760) << ID_JZ4760 |
	IS_ENABLED(CONFIG_MACH_JZ4770) << ID_JZ4770 |
	IS_ENABLED(CONFIG_MACH_JZ4775) << ID_JZ4775 |
	IS_ENABLED(CONFIG_MACH_JZ4780) << ID_JZ4780 |
	IS_ENABLED(CONFIG_MACH_X1000) << ID_X1000 |
	IS_ENABLED(CONFIG_MACH_X1500) << ID_X1500 |
	IS_ENABLED(CONFIG_MACH_X1830) << ID_X1830 |
	IS_ENABLED(CONFIG_MACH_X2000) << ID_X2000 |
	IS_ENABLED(CONFIG_MACH_X2100) << ID_X2100;

static bool
is_soc_or_above(const struct ingenic_pinctrl *jzpc, enum jz_version version)
{
	return (enabled_socs >> version) &&
		(!(enabled_socs & GENMASK(version - 1, 0))
		 || jzpc->info->version >= version);
}

static const u32 jz4730_pull_ups[4] = {
	0x3fa3320f, 0xf200ffff, 0xffffffff, 0xffffffff,
};

static const u32 jz4730_pull_downs[4] = {
	0x00000df0, 0x0dff0000, 0x00000000, 0x00000000,
};

static int jz4730_mmc_1bit_pins[] = { 0x27, 0x26, 0x22, };
static int jz4730_mmc_4bit_pins[] = { 0x23, 0x24, 0x25, };
static int jz4730_uart0_data_pins[] = { 0x7e, 0x7f, };
static int jz4730_uart1_data_pins[] = { 0x18, 0x19, };
static int jz4730_uart2_data_pins[] = { 0x6f, 0x7d, };
static int jz4730_uart3_data_pins[] = { 0x10, 0x15, };
static int jz4730_uart3_hwflow_pins[] = { 0x11, 0x17, };
static int jz4730_lcd_8bit_pins[] = {
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x3a, 0x39, 0x38,
};
static int jz4730_lcd_16bit_pins[] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
};
static int jz4730_lcd_special_pins[] = { 0x3d, 0x3c, 0x3e, 0x3f, };
static int jz4730_lcd_generic_pins[] = { 0x3b, };
static int jz4730_nand_cs1_pins[] = { 0x53, };
static int jz4730_nand_cs2_pins[] = { 0x54, };
static int jz4730_nand_cs3_pins[] = { 0x55, };
static int jz4730_nand_cs4_pins[] = { 0x56, };
static int jz4730_nand_cs5_pins[] = { 0x57, };
static int jz4730_pwm_pwm0_pins[] = { 0x5e, };
static int jz4730_pwm_pwm1_pins[] = { 0x5f, };

static u8 jz4730_lcd_8bit_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, };

static const struct group_desc jz4730_groups[] = {
	INGENIC_PIN_GROUP("mmc-1bit", jz4730_mmc_1bit, 1),
	INGENIC_PIN_GROUP("mmc-4bit", jz4730_mmc_4bit, 1),
	INGENIC_PIN_GROUP("uart0-data", jz4730_uart0_data, 1),
	INGENIC_PIN_GROUP("uart1-data", jz4730_uart1_data, 1),
	INGENIC_PIN_GROUP("uart2-data", jz4730_uart2_data, 1),
	INGENIC_PIN_GROUP("uart3-data", jz4730_uart3_data, 1),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4730_uart3_hwflow, 1),
	INGENIC_PIN_GROUP_FUNCS("lcd-8bit", jz4730_lcd_8bit, jz4730_lcd_8bit_funcs),
	INGENIC_PIN_GROUP("lcd-16bit", jz4730_lcd_16bit, 1),
	INGENIC_PIN_GROUP("lcd-special", jz4730_lcd_special, 1),
	INGENIC_PIN_GROUP("lcd-generic", jz4730_lcd_generic, 1),
	INGENIC_PIN_GROUP("nand-cs1", jz4730_nand_cs1, 1),
	INGENIC_PIN_GROUP("nand-cs2", jz4730_nand_cs2, 1),
	INGENIC_PIN_GROUP("nand-cs3", jz4730_nand_cs3, 1),
	INGENIC_PIN_GROUP("nand-cs4", jz4730_nand_cs4, 1),
	INGENIC_PIN_GROUP("nand-cs5", jz4730_nand_cs5, 1),
	INGENIC_PIN_GROUP("pwm0", jz4730_pwm_pwm0, 1),
	INGENIC_PIN_GROUP("pwm1", jz4730_pwm_pwm1, 1),
};

static const char *jz4730_mmc_groups[] = { "mmc-1bit", "mmc-4bit", };
static const char *jz4730_uart0_groups[] = { "uart0-data", };
static const char *jz4730_uart1_groups[] = { "uart1-data", };
static const char *jz4730_uart2_groups[] = { "uart2-data", };
static const char *jz4730_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
static const char *jz4730_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-special", "lcd-generic",
};
static const char *jz4730_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4", "nand-cs5",
};
static const char *jz4730_pwm0_groups[] = { "pwm0", };
static const char *jz4730_pwm1_groups[] = { "pwm1", };

static const struct function_desc jz4730_functions[] = {
	{ "mmc", jz4730_mmc_groups, ARRAY_SIZE(jz4730_mmc_groups), },
	{ "uart0", jz4730_uart0_groups, ARRAY_SIZE(jz4730_uart0_groups), },
	{ "uart1", jz4730_uart1_groups, ARRAY_SIZE(jz4730_uart1_groups), },
	{ "uart2", jz4730_uart2_groups, ARRAY_SIZE(jz4730_uart2_groups), },
	{ "uart3", jz4730_uart3_groups, ARRAY_SIZE(jz4730_uart3_groups), },
	{ "lcd", jz4730_lcd_groups, ARRAY_SIZE(jz4730_lcd_groups), },
	{ "nand", jz4730_nand_groups, ARRAY_SIZE(jz4730_nand_groups), },
	{ "pwm0", jz4730_pwm0_groups, ARRAY_SIZE(jz4730_pwm0_groups), },
	{ "pwm1", jz4730_pwm1_groups, ARRAY_SIZE(jz4730_pwm1_groups), },
};

static const struct ingenic_chip_info jz4730_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x30,
	.version = ID_JZ4730,
	.groups = jz4730_groups,
	.num_groups = ARRAY_SIZE(jz4730_groups),
	.functions = jz4730_functions,
	.num_functions = ARRAY_SIZE(jz4730_functions),
	.pull_ups = jz4730_pull_ups,
	.pull_downs = jz4730_pull_downs,
};

static const u32 jz4740_pull_ups[4] = {
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
};

static const u32 jz4740_pull_downs[4] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static int jz4740_mmc_1bit_pins[] = { 0x69, 0x68, 0x6a, };
static int jz4740_mmc_4bit_pins[] = { 0x6b, 0x6c, 0x6d, };
static int jz4740_uart0_data_pins[] = { 0x7a, 0x79, };
static int jz4740_uart0_hwflow_pins[] = { 0x7e, 0x7f, };
static int jz4740_uart1_data_pins[] = { 0x7e, 0x7f, };
static int jz4740_lcd_8bit_pins[] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x52, 0x53, 0x54,
};
static int jz4740_lcd_16bit_pins[] = {
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
};
static int jz4740_lcd_18bit_pins[] = { 0x50, 0x51, };
static int jz4740_lcd_special_pins[] = { 0x31, 0x32, 0x56, 0x57, };
static int jz4740_lcd_generic_pins[] = { 0x55, };
static int jz4740_nand_cs1_pins[] = { 0x39, };
static int jz4740_nand_cs2_pins[] = { 0x3a, };
static int jz4740_nand_cs3_pins[] = { 0x3b, };
static int jz4740_nand_cs4_pins[] = { 0x3c, };
static int jz4740_nand_fre_fwe_pins[] = { 0x5c, 0x5d, };
static int jz4740_pwm_pwm0_pins[] = { 0x77, };
static int jz4740_pwm_pwm1_pins[] = { 0x78, };
static int jz4740_pwm_pwm2_pins[] = { 0x79, };
static int jz4740_pwm_pwm3_pins[] = { 0x7a, };
static int jz4740_pwm_pwm4_pins[] = { 0x7b, };
static int jz4740_pwm_pwm5_pins[] = { 0x7c, };
static int jz4740_pwm_pwm6_pins[] = { 0x7e, };
static int jz4740_pwm_pwm7_pins[] = { 0x7f, };

static const struct group_desc jz4740_groups[] = {
	INGENIC_PIN_GROUP("mmc-1bit", jz4740_mmc_1bit, 0),
	INGENIC_PIN_GROUP("mmc-4bit", jz4740_mmc_4bit, 0),
	INGENIC_PIN_GROUP("uart0-data", jz4740_uart0_data, 1),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4740_uart0_hwflow, 1),
	INGENIC_PIN_GROUP("uart1-data", jz4740_uart1_data, 2),
	INGENIC_PIN_GROUP("lcd-8bit", jz4740_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4740_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4740_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-special", jz4740_lcd_special, 0),
	INGENIC_PIN_GROUP("lcd-generic", jz4740_lcd_generic, 0),
	INGENIC_PIN_GROUP("nand-cs1", jz4740_nand_cs1, 0),
	INGENIC_PIN_GROUP("nand-cs2", jz4740_nand_cs2, 0),
	INGENIC_PIN_GROUP("nand-cs3", jz4740_nand_cs3, 0),
	INGENIC_PIN_GROUP("nand-cs4", jz4740_nand_cs4, 0),
	INGENIC_PIN_GROUP("nand-fre-fwe", jz4740_nand_fre_fwe, 0),
	INGENIC_PIN_GROUP("pwm0", jz4740_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4740_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4740_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4740_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4740_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4740_pwm_pwm5, 0),
	INGENIC_PIN_GROUP("pwm6", jz4740_pwm_pwm6, 0),
	INGENIC_PIN_GROUP("pwm7", jz4740_pwm_pwm7, 0),
};

static const char *jz4740_mmc_groups[] = { "mmc-1bit", "mmc-4bit", };
static const char *jz4740_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4740_uart1_groups[] = { "uart1-data", };
static const char *jz4740_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-special", "lcd-generic",
};
static const char *jz4740_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4", "nand-fre-fwe",
};
static const char *jz4740_pwm0_groups[] = { "pwm0", };
static const char *jz4740_pwm1_groups[] = { "pwm1", };
static const char *jz4740_pwm2_groups[] = { "pwm2", };
static const char *jz4740_pwm3_groups[] = { "pwm3", };
static const char *jz4740_pwm4_groups[] = { "pwm4", };
static const char *jz4740_pwm5_groups[] = { "pwm5", };
static const char *jz4740_pwm6_groups[] = { "pwm6", };
static const char *jz4740_pwm7_groups[] = { "pwm7", };

static const struct function_desc jz4740_functions[] = {
	{ "mmc", jz4740_mmc_groups, ARRAY_SIZE(jz4740_mmc_groups), },
	{ "uart0", jz4740_uart0_groups, ARRAY_SIZE(jz4740_uart0_groups), },
	{ "uart1", jz4740_uart1_groups, ARRAY_SIZE(jz4740_uart1_groups), },
	{ "lcd", jz4740_lcd_groups, ARRAY_SIZE(jz4740_lcd_groups), },
	{ "nand", jz4740_nand_groups, ARRAY_SIZE(jz4740_nand_groups), },
	{ "pwm0", jz4740_pwm0_groups, ARRAY_SIZE(jz4740_pwm0_groups), },
	{ "pwm1", jz4740_pwm1_groups, ARRAY_SIZE(jz4740_pwm1_groups), },
	{ "pwm2", jz4740_pwm2_groups, ARRAY_SIZE(jz4740_pwm2_groups), },
	{ "pwm3", jz4740_pwm3_groups, ARRAY_SIZE(jz4740_pwm3_groups), },
	{ "pwm4", jz4740_pwm4_groups, ARRAY_SIZE(jz4740_pwm4_groups), },
	{ "pwm5", jz4740_pwm5_groups, ARRAY_SIZE(jz4740_pwm5_groups), },
	{ "pwm6", jz4740_pwm6_groups, ARRAY_SIZE(jz4740_pwm6_groups), },
	{ "pwm7", jz4740_pwm7_groups, ARRAY_SIZE(jz4740_pwm7_groups), },
};

static const struct ingenic_chip_info jz4740_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x100,
	.version = ID_JZ4740,
	.groups = jz4740_groups,
	.num_groups = ARRAY_SIZE(jz4740_groups),
	.functions = jz4740_functions,
	.num_functions = ARRAY_SIZE(jz4740_functions),
	.pull_ups = jz4740_pull_ups,
	.pull_downs = jz4740_pull_downs,
};

static int jz4725b_mmc0_1bit_pins[] = { 0x48, 0x49, 0x5c, };
static int jz4725b_mmc0_4bit_pins[] = { 0x5d, 0x5b, 0x56, };
static int jz4725b_mmc1_1bit_pins[] = { 0x7a, 0x7b, 0x7c, };
static int jz4725b_mmc1_4bit_pins[] = { 0x7d, 0x7e, 0x7f, };
static int jz4725b_uart_data_pins[] = { 0x4c, 0x4d, };
static int jz4725b_lcd_8bit_pins[] = {
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x72, 0x73, 0x74,
};
static int jz4725b_lcd_16bit_pins[] = {
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
};
static int jz4725b_lcd_18bit_pins[] = { 0x70, 0x71, };
static int jz4725b_lcd_24bit_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4725b_lcd_special_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4725b_lcd_generic_pins[] = { 0x75, };
static int jz4725b_nand_cs1_pins[] = { 0x55, };
static int jz4725b_nand_cs2_pins[] = { 0x56, };
static int jz4725b_nand_cs3_pins[] = { 0x57, };
static int jz4725b_nand_cs4_pins[] = { 0x58, };
static int jz4725b_nand_cle_ale_pins[] = { 0x48, 0x49 };
static int jz4725b_nand_fre_fwe_pins[] = { 0x5c, 0x5d };
static int jz4725b_pwm_pwm0_pins[] = { 0x4a, };
static int jz4725b_pwm_pwm1_pins[] = { 0x4b, };
static int jz4725b_pwm_pwm2_pins[] = { 0x4c, };
static int jz4725b_pwm_pwm3_pins[] = { 0x4d, };
static int jz4725b_pwm_pwm4_pins[] = { 0x4e, };
static int jz4725b_pwm_pwm5_pins[] = { 0x4f, };

static u8 jz4725b_mmc0_4bit_funcs[] = { 1, 0, 1, };

static const struct group_desc jz4725b_groups[] = {
	INGENIC_PIN_GROUP("mmc0-1bit", jz4725b_mmc0_1bit, 1),
	INGENIC_PIN_GROUP_FUNCS("mmc0-4bit", jz4725b_mmc0_4bit,
				jz4725b_mmc0_4bit_funcs),
	INGENIC_PIN_GROUP("mmc1-1bit", jz4725b_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", jz4725b_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("uart-data", jz4725b_uart_data, 1),
	INGENIC_PIN_GROUP("lcd-8bit", jz4725b_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4725b_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4725b_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4725b_lcd_24bit, 1),
	INGENIC_PIN_GROUP("lcd-special", jz4725b_lcd_special, 0),
	INGENIC_PIN_GROUP("lcd-generic", jz4725b_lcd_generic, 0),
	INGENIC_PIN_GROUP("nand-cs1", jz4725b_nand_cs1, 0),
	INGENIC_PIN_GROUP("nand-cs2", jz4725b_nand_cs2, 0),
	INGENIC_PIN_GROUP("nand-cs3", jz4725b_nand_cs3, 0),
	INGENIC_PIN_GROUP("nand-cs4", jz4725b_nand_cs4, 0),
	INGENIC_PIN_GROUP("nand-cle-ale", jz4725b_nand_cle_ale, 0),
	INGENIC_PIN_GROUP("nand-fre-fwe", jz4725b_nand_fre_fwe, 0),
	INGENIC_PIN_GROUP("pwm0", jz4725b_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4725b_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4725b_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4725b_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4725b_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4725b_pwm_pwm5, 0),
};

static const char *jz4725b_mmc0_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *jz4725b_mmc1_groups[] = { "mmc1-1bit", "mmc1-4bit", };
static const char *jz4725b_uart_groups[] = { "uart-data", };
static const char *jz4725b_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4725b_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4",
	"nand-cle-ale", "nand-fre-fwe",
};
static const char *jz4725b_pwm0_groups[] = { "pwm0", };
static const char *jz4725b_pwm1_groups[] = { "pwm1", };
static const char *jz4725b_pwm2_groups[] = { "pwm2", };
static const char *jz4725b_pwm3_groups[] = { "pwm3", };
static const char *jz4725b_pwm4_groups[] = { "pwm4", };
static const char *jz4725b_pwm5_groups[] = { "pwm5", };

static const struct function_desc jz4725b_functions[] = {
	{ "mmc0", jz4725b_mmc0_groups, ARRAY_SIZE(jz4725b_mmc0_groups), },
	{ "mmc1", jz4725b_mmc1_groups, ARRAY_SIZE(jz4725b_mmc1_groups), },
	{ "uart", jz4725b_uart_groups, ARRAY_SIZE(jz4725b_uart_groups), },
	{ "nand", jz4725b_nand_groups, ARRAY_SIZE(jz4725b_nand_groups), },
	{ "pwm0", jz4725b_pwm0_groups, ARRAY_SIZE(jz4725b_pwm0_groups), },
	{ "pwm1", jz4725b_pwm1_groups, ARRAY_SIZE(jz4725b_pwm1_groups), },
	{ "pwm2", jz4725b_pwm2_groups, ARRAY_SIZE(jz4725b_pwm2_groups), },
	{ "pwm3", jz4725b_pwm3_groups, ARRAY_SIZE(jz4725b_pwm3_groups), },
	{ "pwm4", jz4725b_pwm4_groups, ARRAY_SIZE(jz4725b_pwm4_groups), },
	{ "pwm5", jz4725b_pwm5_groups, ARRAY_SIZE(jz4725b_pwm5_groups), },
	{ "lcd", jz4725b_lcd_groups, ARRAY_SIZE(jz4725b_lcd_groups), },
};

static const struct ingenic_chip_info jz4725b_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x100,
	.version = ID_JZ4725B,
	.groups = jz4725b_groups,
	.num_groups = ARRAY_SIZE(jz4725b_groups),
	.functions = jz4725b_functions,
	.num_functions = ARRAY_SIZE(jz4725b_functions),
	.pull_ups = jz4740_pull_ups,
	.pull_downs = jz4740_pull_downs,
};

static const u32 jz4750_pull_ups[6] = {
	0xffffffff, 0xffffffff, 0x3fffffff, 0x7fffffff, 0x1fff3fff, 0x00ffffff,
};

static const u32 jz4750_pull_downs[6] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static int jz4750_uart0_data_pins[] = { 0xa4, 0xa5, };
static int jz4750_uart0_hwflow_pins[] = { 0xa6, 0xa7, };
static int jz4750_uart1_data_pins[] = { 0x90, 0x91, };
static int jz4750_uart1_hwflow_pins[] = { 0x92, 0x93, };
static int jz4750_uart2_data_pins[] = { 0x9b, 0x9a, };
static int jz4750_uart3_data_pins[] = { 0xb0, 0xb1, };
static int jz4750_uart3_hwflow_pins[] = { 0xb2, 0xb3, };
static int jz4750_mmc0_1bit_pins[] = { 0xa8, 0xa9, 0xa0, };
static int jz4750_mmc0_4bit_pins[] = { 0xa1, 0xa2, 0xa3, };
static int jz4750_mmc0_8bit_pins[] = { 0xa4, 0xa5, 0xa6, 0xa7, };
static int jz4750_mmc1_1bit_pins[] = { 0xae, 0xaf, 0xaa, };
static int jz4750_mmc1_4bit_pins[] = { 0xab, 0xac, 0xad, };
static int jz4750_i2c_pins[] = { 0x8c, 0x8d, };
static int jz4750_cim_pins[] = {
	0x89, 0x8b, 0x8a, 0x88,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
};
static int jz4750_lcd_8bit_pins[] = {
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x72, 0x73, 0x74,
};
static int jz4750_lcd_16bit_pins[] = {
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
};
static int jz4750_lcd_18bit_pins[] = { 0x70, 0x71, };
static int jz4750_lcd_24bit_pins[] = { 0x76, 0x77, 0x78, 0x79, 0xb2, 0xb3, };
static int jz4750_lcd_special_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4750_lcd_generic_pins[] = { 0x75, };
static int jz4750_nand_cs1_pins[] = { 0x55, };
static int jz4750_nand_cs2_pins[] = { 0x56, };
static int jz4750_nand_cs3_pins[] = { 0x57, };
static int jz4750_nand_cs4_pins[] = { 0x58, };
static int jz4750_nand_fre_fwe_pins[] = { 0x5c, 0x5d, };
static int jz4750_pwm_pwm0_pins[] = { 0x94, };
static int jz4750_pwm_pwm1_pins[] = { 0x95, };
static int jz4750_pwm_pwm2_pins[] = { 0x96, };
static int jz4750_pwm_pwm3_pins[] = { 0x97, };
static int jz4750_pwm_pwm4_pins[] = { 0x98, };
static int jz4750_pwm_pwm5_pins[] = { 0x99, };

static const struct group_desc jz4750_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4750_uart0_data, 1),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4750_uart0_hwflow, 1),
	INGENIC_PIN_GROUP("uart1-data", jz4750_uart1_data, 0),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4750_uart1_hwflow, 0),
	INGENIC_PIN_GROUP("uart2-data", jz4750_uart2_data, 1),
	INGENIC_PIN_GROUP("uart3-data", jz4750_uart3_data, 0),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4750_uart3_hwflow, 0),
	INGENIC_PIN_GROUP("mmc0-1bit", jz4750_mmc0_1bit, 0),
	INGENIC_PIN_GROUP("mmc0-4bit", jz4750_mmc0_4bit, 0),
	INGENIC_PIN_GROUP("mmc0-8bit", jz4750_mmc0_8bit, 0),
	INGENIC_PIN_GROUP("mmc1-1bit", jz4750_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", jz4750_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("i2c-data", jz4750_i2c, 0),
	INGENIC_PIN_GROUP("cim-data", jz4750_cim, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4750_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4750_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4750_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4750_lcd_24bit, 1),
	INGENIC_PIN_GROUP("lcd-special", jz4750_lcd_special, 0),
	INGENIC_PIN_GROUP("lcd-generic", jz4750_lcd_generic, 0),
	INGENIC_PIN_GROUP("nand-cs1", jz4750_nand_cs1, 0),
	INGENIC_PIN_GROUP("nand-cs2", jz4750_nand_cs2, 0),
	INGENIC_PIN_GROUP("nand-cs3", jz4750_nand_cs3, 0),
	INGENIC_PIN_GROUP("nand-cs4", jz4750_nand_cs4, 0),
	INGENIC_PIN_GROUP("nand-fre-fwe", jz4750_nand_fre_fwe, 0),
	INGENIC_PIN_GROUP("pwm0", jz4750_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4750_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4750_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4750_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4750_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4750_pwm_pwm5, 0),
};

static const char *jz4750_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4750_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4750_uart2_groups[] = { "uart2-data", };
static const char *jz4750_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
static const char *jz4750_mmc0_groups[] = {
	"mmc0-1bit", "mmc0-4bit", "mmc0-8bit",
};
static const char *jz4750_mmc1_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *jz4750_i2c_groups[] = { "i2c-data", };
static const char *jz4750_cim_groups[] = { "cim-data", };
static const char *jz4750_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4750_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4", "nand-fre-fwe",
};
static const char *jz4750_pwm0_groups[] = { "pwm0", };
static const char *jz4750_pwm1_groups[] = { "pwm1", };
static const char *jz4750_pwm2_groups[] = { "pwm2", };
static const char *jz4750_pwm3_groups[] = { "pwm3", };
static const char *jz4750_pwm4_groups[] = { "pwm4", };
static const char *jz4750_pwm5_groups[] = { "pwm5", };

static const struct function_desc jz4750_functions[] = {
	{ "uart0", jz4750_uart0_groups, ARRAY_SIZE(jz4750_uart0_groups), },
	{ "uart1", jz4750_uart1_groups, ARRAY_SIZE(jz4750_uart1_groups), },
	{ "uart2", jz4750_uart2_groups, ARRAY_SIZE(jz4750_uart2_groups), },
	{ "uart3", jz4750_uart3_groups, ARRAY_SIZE(jz4750_uart3_groups), },
	{ "mmc0", jz4750_mmc0_groups, ARRAY_SIZE(jz4750_mmc0_groups), },
	{ "mmc1", jz4750_mmc1_groups, ARRAY_SIZE(jz4750_mmc1_groups), },
	{ "i2c", jz4750_i2c_groups, ARRAY_SIZE(jz4750_i2c_groups), },
	{ "cim", jz4750_cim_groups, ARRAY_SIZE(jz4750_cim_groups), },
	{ "lcd", jz4750_lcd_groups, ARRAY_SIZE(jz4750_lcd_groups), },
	{ "nand", jz4750_nand_groups, ARRAY_SIZE(jz4750_nand_groups), },
	{ "pwm0", jz4750_pwm0_groups, ARRAY_SIZE(jz4750_pwm0_groups), },
	{ "pwm1", jz4750_pwm1_groups, ARRAY_SIZE(jz4750_pwm1_groups), },
	{ "pwm2", jz4750_pwm2_groups, ARRAY_SIZE(jz4750_pwm2_groups), },
	{ "pwm3", jz4750_pwm3_groups, ARRAY_SIZE(jz4750_pwm3_groups), },
	{ "pwm4", jz4750_pwm4_groups, ARRAY_SIZE(jz4750_pwm4_groups), },
	{ "pwm5", jz4750_pwm5_groups, ARRAY_SIZE(jz4750_pwm5_groups), },
};

static const struct ingenic_chip_info jz4750_chip_info = {
	.num_chips = 6,
	.reg_offset = 0x100,
	.version = ID_JZ4750,
	.groups = jz4750_groups,
	.num_groups = ARRAY_SIZE(jz4750_groups),
	.functions = jz4750_functions,
	.num_functions = ARRAY_SIZE(jz4750_functions),
	.pull_ups = jz4750_pull_ups,
	.pull_downs = jz4750_pull_downs,
};

static const u32 jz4755_pull_ups[6] = {
	0xffffffff, 0xffffffff, 0x0fffffff, 0xffffffff, 0x33dc3fff, 0x0000fc00,
};

static const u32 jz4755_pull_downs[6] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static int jz4755_uart0_data_pins[] = { 0x7c, 0x7d, };
static int jz4755_uart0_hwflow_pins[] = { 0x7e, 0x7f, };
static int jz4755_uart1_data_pins[] = { 0x97, 0x99, };
static int jz4755_uart2_data_pins[] = { 0x9f, };
static int jz4755_ssi_dt_b_pins[] = { 0x3b, };
static int jz4755_ssi_dt_f_pins[] = { 0xa1, };
static int jz4755_ssi_dr_b_pins[] = { 0x3c, };
static int jz4755_ssi_dr_f_pins[] = { 0xa2, };
static int jz4755_ssi_clk_b_pins[] = { 0x3a, };
static int jz4755_ssi_clk_f_pins[] = { 0xa0, };
static int jz4755_ssi_gpc_b_pins[] = { 0x3e, };
static int jz4755_ssi_gpc_f_pins[] = { 0xa4, };
static int jz4755_ssi_ce0_b_pins[] = { 0x3d, };
static int jz4755_ssi_ce0_f_pins[] = { 0xa3, };
static int jz4755_ssi_ce1_b_pins[] = { 0x3f, };
static int jz4755_ssi_ce1_f_pins[] = { 0xa5, };
static int jz4755_mmc0_1bit_pins[] = { 0x2f, 0x50, 0x5c, };
static int jz4755_mmc0_4bit_pins[] = { 0x5d, 0x5b, 0x51, };
static int jz4755_mmc1_1bit_pins[] = { 0x3a, 0x3d, 0x3c, };
static int jz4755_mmc1_4bit_pins[] = { 0x3b, 0x3e, 0x3f, };
static int jz4755_i2c_pins[] = { 0x8c, 0x8d, };
static int jz4755_cim_pins[] = {
	0x89, 0x8b, 0x8a, 0x88,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
};
static int jz4755_lcd_8bit_pins[] = {
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x72, 0x73, 0x74,
};
static int jz4755_lcd_16bit_pins[] = {
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
};
static int jz4755_lcd_18bit_pins[] = { 0x70, 0x71, };
static int jz4755_lcd_24bit_pins[] = { 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, };
static int jz4755_lcd_special_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4755_lcd_generic_pins[] = { 0x75, };
static int jz4755_nand_cs1_pins[] = { 0x55, };
static int jz4755_nand_cs2_pins[] = { 0x56, };
static int jz4755_nand_cs3_pins[] = { 0x57, };
static int jz4755_nand_cs4_pins[] = { 0x58, };
static int jz4755_nand_fre_fwe_pins[] = { 0x5c, 0x5d, };
static int jz4755_pwm_pwm0_pins[] = { 0x94, };
static int jz4755_pwm_pwm1_pins[] = { 0xab, };
static int jz4755_pwm_pwm2_pins[] = { 0x96, };
static int jz4755_pwm_pwm3_pins[] = { 0x97, };
static int jz4755_pwm_pwm4_pins[] = { 0x98, };
static int jz4755_pwm_pwm5_pins[] = { 0x99, };

static u8 jz4755_mmc0_1bit_funcs[] = { 2, 2, 1, };
static u8 jz4755_mmc0_4bit_funcs[] = { 1, 0, 1, };
static u8 jz4755_lcd_24bit_funcs[] = { 1, 1, 1, 1, 0, 0, };

static const struct group_desc jz4755_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4755_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4755_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", jz4755_uart1_data, 0),
	INGENIC_PIN_GROUP("uart2-data", jz4755_uart2_data, 1),
	INGENIC_PIN_GROUP("ssi-dt-b", jz4755_ssi_dt_b, 0),
	INGENIC_PIN_GROUP("ssi-dt-f", jz4755_ssi_dt_f, 0),
	INGENIC_PIN_GROUP("ssi-dr-b", jz4755_ssi_dr_b, 0),
	INGENIC_PIN_GROUP("ssi-dr-f", jz4755_ssi_dr_f, 0),
	INGENIC_PIN_GROUP("ssi-clk-b", jz4755_ssi_clk_b, 0),
	INGENIC_PIN_GROUP("ssi-clk-f", jz4755_ssi_clk_f, 0),
	INGENIC_PIN_GROUP("ssi-gpc-b", jz4755_ssi_gpc_b, 0),
	INGENIC_PIN_GROUP("ssi-gpc-f", jz4755_ssi_gpc_f, 0),
	INGENIC_PIN_GROUP("ssi-ce0-b", jz4755_ssi_ce0_b, 0),
	INGENIC_PIN_GROUP("ssi-ce0-f", jz4755_ssi_ce0_f, 0),
	INGENIC_PIN_GROUP("ssi-ce1-b", jz4755_ssi_ce1_b, 0),
	INGENIC_PIN_GROUP("ssi-ce1-f", jz4755_ssi_ce1_f, 0),
	INGENIC_PIN_GROUP_FUNCS("mmc0-1bit", jz4755_mmc0_1bit,
				jz4755_mmc0_1bit_funcs),
	INGENIC_PIN_GROUP_FUNCS("mmc0-4bit", jz4755_mmc0_4bit,
				jz4755_mmc0_4bit_funcs),
	INGENIC_PIN_GROUP("mmc1-1bit", jz4755_mmc1_1bit, 1),
	INGENIC_PIN_GROUP("mmc1-4bit", jz4755_mmc1_4bit, 1),
	INGENIC_PIN_GROUP("i2c-data", jz4755_i2c, 0),
	INGENIC_PIN_GROUP("cim-data", jz4755_cim, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4755_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4755_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4755_lcd_18bit, 0),
	INGENIC_PIN_GROUP_FUNCS("lcd-24bit", jz4755_lcd_24bit,
				jz4755_lcd_24bit_funcs),
	INGENIC_PIN_GROUP("lcd-special", jz4755_lcd_special, 0),
	INGENIC_PIN_GROUP("lcd-generic", jz4755_lcd_generic, 0),
	INGENIC_PIN_GROUP("nand-cs1", jz4755_nand_cs1, 0),
	INGENIC_PIN_GROUP("nand-cs2", jz4755_nand_cs2, 0),
	INGENIC_PIN_GROUP("nand-cs3", jz4755_nand_cs3, 0),
	INGENIC_PIN_GROUP("nand-cs4", jz4755_nand_cs4, 0),
	INGENIC_PIN_GROUP("nand-fre-fwe", jz4755_nand_fre_fwe, 0),
	INGENIC_PIN_GROUP("pwm0", jz4755_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4755_pwm_pwm1, 1),
	INGENIC_PIN_GROUP("pwm2", jz4755_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4755_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4755_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4755_pwm_pwm5, 0),
};

static const char *jz4755_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4755_uart1_groups[] = { "uart1-data", };
static const char *jz4755_uart2_groups[] = { "uart2-data", };
static const char *jz4755_ssi_groups[] = {
	"ssi-dt-b", "ssi-dt-f",
	"ssi-dr-b", "ssi-dr-f",
	"ssi-clk-b", "ssi-clk-f",
	"ssi-gpc-b", "ssi-gpc-f",
	"ssi-ce0-b", "ssi-ce0-f",
	"ssi-ce1-b", "ssi-ce1-f",
};
static const char *jz4755_mmc0_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *jz4755_mmc1_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *jz4755_i2c_groups[] = { "i2c-data", };
static const char *jz4755_cim_groups[] = { "cim-data", };
static const char *jz4755_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4755_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4", "nand-fre-fwe",
};
static const char *jz4755_pwm0_groups[] = { "pwm0", };
static const char *jz4755_pwm1_groups[] = { "pwm1", };
static const char *jz4755_pwm2_groups[] = { "pwm2", };
static const char *jz4755_pwm3_groups[] = { "pwm3", };
static const char *jz4755_pwm4_groups[] = { "pwm4", };
static const char *jz4755_pwm5_groups[] = { "pwm5", };

static const struct function_desc jz4755_functions[] = {
	{ "uart0", jz4755_uart0_groups, ARRAY_SIZE(jz4755_uart0_groups), },
	{ "uart1", jz4755_uart1_groups, ARRAY_SIZE(jz4755_uart1_groups), },
	{ "uart2", jz4755_uart2_groups, ARRAY_SIZE(jz4755_uart2_groups), },
	{ "ssi", jz4755_ssi_groups, ARRAY_SIZE(jz4755_ssi_groups), },
	{ "mmc0", jz4755_mmc0_groups, ARRAY_SIZE(jz4755_mmc0_groups), },
	{ "mmc1", jz4755_mmc1_groups, ARRAY_SIZE(jz4755_mmc1_groups), },
	{ "i2c", jz4755_i2c_groups, ARRAY_SIZE(jz4755_i2c_groups), },
	{ "cim", jz4755_cim_groups, ARRAY_SIZE(jz4755_cim_groups), },
	{ "lcd", jz4755_lcd_groups, ARRAY_SIZE(jz4755_lcd_groups), },
	{ "nand", jz4755_nand_groups, ARRAY_SIZE(jz4755_nand_groups), },
	{ "pwm0", jz4755_pwm0_groups, ARRAY_SIZE(jz4755_pwm0_groups), },
	{ "pwm1", jz4755_pwm1_groups, ARRAY_SIZE(jz4755_pwm1_groups), },
	{ "pwm2", jz4755_pwm2_groups, ARRAY_SIZE(jz4755_pwm2_groups), },
	{ "pwm3", jz4755_pwm3_groups, ARRAY_SIZE(jz4755_pwm3_groups), },
	{ "pwm4", jz4755_pwm4_groups, ARRAY_SIZE(jz4755_pwm4_groups), },
	{ "pwm5", jz4755_pwm5_groups, ARRAY_SIZE(jz4755_pwm5_groups), },
};

static const struct ingenic_chip_info jz4755_chip_info = {
	.num_chips = 6,
	.reg_offset = 0x100,
	.version = ID_JZ4755,
	.groups = jz4755_groups,
	.num_groups = ARRAY_SIZE(jz4755_groups),
	.functions = jz4755_functions,
	.num_functions = ARRAY_SIZE(jz4755_functions),
	.pull_ups = jz4755_pull_ups,
	.pull_downs = jz4755_pull_downs,
};

static const u32 jz4760_pull_ups[6] = {
	0xffffffff, 0xfffcf3ff, 0xffffffff, 0xffffcfff, 0xfffffb7c, 0x0000000f,
};

static const u32 jz4760_pull_downs[6] = {
	0x00000000, 0x00030c00, 0x00000000, 0x00003000, 0x00000483, 0x00000ff0,
};

static int jz4760_uart0_data_pins[] = { 0xa0, 0xa3, };
static int jz4760_uart0_hwflow_pins[] = { 0xa1, 0xa2, };
static int jz4760_uart1_data_pins[] = { 0x7a, 0x7c, };
static int jz4760_uart1_hwflow_pins[] = { 0x7b, 0x7d, };
static int jz4760_uart2_data_pins[] = { 0x5c, 0x5e, };
static int jz4760_uart2_hwflow_pins[] = { 0x5d, 0x5f, };
static int jz4760_uart3_data_pins[] = { 0x6c, 0x85, };
static int jz4760_uart3_hwflow_pins[] = { 0x88, 0x89, };
static int jz4760_ssi0_dt_a_pins[] = { 0x15, };
static int jz4760_ssi0_dt_b_pins[] = { 0x35, };
static int jz4760_ssi0_dt_d_pins[] = { 0x75, };
static int jz4760_ssi0_dt_e_pins[] = { 0x91, };
static int jz4760_ssi0_dr_a_pins[] = { 0x14, };
static int jz4760_ssi0_dr_b_pins[] = { 0x34, };
static int jz4760_ssi0_dr_d_pins[] = { 0x74, };
static int jz4760_ssi0_dr_e_pins[] = { 0x8e, };
static int jz4760_ssi0_clk_a_pins[] = { 0x12, };
static int jz4760_ssi0_clk_b_pins[] = { 0x3c, };
static int jz4760_ssi0_clk_d_pins[] = { 0x78, };
static int jz4760_ssi0_clk_e_pins[] = { 0x8f, };
static int jz4760_ssi0_gpc_b_pins[] = { 0x3e, };
static int jz4760_ssi0_gpc_d_pins[] = { 0x76, };
static int jz4760_ssi0_gpc_e_pins[] = { 0x93, };
static int jz4760_ssi0_ce0_a_pins[] = { 0x13, };
static int jz4760_ssi0_ce0_b_pins[] = { 0x3d, };
static int jz4760_ssi0_ce0_d_pins[] = { 0x79, };
static int jz4760_ssi0_ce0_e_pins[] = { 0x90, };
static int jz4760_ssi0_ce1_b_pins[] = { 0x3f, };
static int jz4760_ssi0_ce1_d_pins[] = { 0x77, };
static int jz4760_ssi0_ce1_e_pins[] = { 0x92, };
static int jz4760_ssi1_dt_b_9_pins[] = { 0x29, };
static int jz4760_ssi1_dt_b_21_pins[] = { 0x35, };
static int jz4760_ssi1_dt_d_12_pins[] = { 0x6c, };
static int jz4760_ssi1_dt_d_21_pins[] = { 0x75, };
static int jz4760_ssi1_dt_e_pins[] = { 0x91, };
static int jz4760_ssi1_dt_f_pins[] = { 0xa3, };
static int jz4760_ssi1_dr_b_6_pins[] = { 0x26, };
static int jz4760_ssi1_dr_b_20_pins[] = { 0x34, };
static int jz4760_ssi1_dr_d_13_pins[] = { 0x6d, };
static int jz4760_ssi1_dr_d_20_pins[] = { 0x74, };
static int jz4760_ssi1_dr_e_pins[] = { 0x8e, };
static int jz4760_ssi1_dr_f_pins[] = { 0xa0, };
static int jz4760_ssi1_clk_b_7_pins[] = { 0x27, };
static int jz4760_ssi1_clk_b_28_pins[] = { 0x3c, };
static int jz4760_ssi1_clk_d_pins[] = { 0x78, };
static int jz4760_ssi1_clk_e_7_pins[] = { 0x87, };
static int jz4760_ssi1_clk_e_15_pins[] = { 0x8f, };
static int jz4760_ssi1_clk_f_pins[] = { 0xa2, };
static int jz4760_ssi1_gpc_b_pins[] = { 0x3e, };
static int jz4760_ssi1_gpc_d_pins[] = { 0x76, };
static int jz4760_ssi1_gpc_e_pins[] = { 0x93, };
static int jz4760_ssi1_ce0_b_8_pins[] = { 0x28, };
static int jz4760_ssi1_ce0_b_29_pins[] = { 0x3d, };
static int jz4760_ssi1_ce0_d_pins[] = { 0x79, };
static int jz4760_ssi1_ce0_e_6_pins[] = { 0x86, };
static int jz4760_ssi1_ce0_e_16_pins[] = { 0x90, };
static int jz4760_ssi1_ce0_f_pins[] = { 0xa1, };
static int jz4760_ssi1_ce1_b_pins[] = { 0x3f, };
static int jz4760_ssi1_ce1_d_pins[] = { 0x77, };
static int jz4760_ssi1_ce1_e_pins[] = { 0x92, };
static int jz4760_mmc0_1bit_a_pins[] = { 0x12, 0x13, 0x14, };
static int jz4760_mmc0_4bit_a_pins[] = { 0x15, 0x16, 0x17, };
static int jz4760_mmc0_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4760_mmc0_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4760_mmc0_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4760_mmc1_1bit_d_pins[] = { 0x78, 0x79, 0x74, };
static int jz4760_mmc1_4bit_d_pins[] = { 0x75, 0x76, 0x77, };
static int jz4760_mmc1_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4760_mmc1_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4760_mmc1_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4760_mmc2_1bit_b_pins[] = { 0x3c, 0x3d, 0x34, };
static int jz4760_mmc2_4bit_b_pins[] = { 0x35, 0x3e, 0x3f, };
static int jz4760_mmc2_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4760_mmc2_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4760_mmc2_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4760_nemc_8bit_data_pins[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
static int jz4760_nemc_16bit_data_pins[] = {
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static int jz4760_nemc_cle_ale_pins[] = { 0x20, 0x21, };
static int jz4760_nemc_addr_pins[] = { 0x22, 0x23, 0x24, 0x25, };
static int jz4760_nemc_rd_we_pins[] = { 0x10, 0x11, };
static int jz4760_nemc_frd_fwe_pins[] = { 0x12, 0x13, };
static int jz4760_nemc_wait_pins[] = { 0x1b, };
static int jz4760_nemc_cs1_pins[] = { 0x15, };
static int jz4760_nemc_cs2_pins[] = { 0x16, };
static int jz4760_nemc_cs3_pins[] = { 0x17, };
static int jz4760_nemc_cs4_pins[] = { 0x18, };
static int jz4760_nemc_cs5_pins[] = { 0x19, };
static int jz4760_nemc_cs6_pins[] = { 0x1a, };
static int jz4760_i2c0_pins[] = { 0x7e, 0x7f, };
static int jz4760_i2c1_pins[] = { 0x9e, 0x9f, };
static int jz4760_cim_pins[] = {
	0x26, 0x27, 0x28, 0x29,
	0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
};
static int jz4760_lcd_8bit_pins[] = {
	0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4c,
	0x4d, 0x52, 0x53,
};
static int jz4760_lcd_16bit_pins[] = {
	0x4e, 0x4f, 0x50, 0x51, 0x56, 0x57, 0x58, 0x59,
};
static int jz4760_lcd_18bit_pins[] = {
	0x5a, 0x5b,
};
static int jz4760_lcd_24bit_pins[] = {
	0x40, 0x41, 0x4a, 0x4b, 0x54, 0x55,
};
static int jz4760_lcd_special_pins[] = { 0x54, 0x4a, 0x41, 0x40, };
static int jz4760_lcd_generic_pins[] = { 0x49, };
static int jz4760_pwm_pwm0_pins[] = { 0x80, };
static int jz4760_pwm_pwm1_pins[] = { 0x81, };
static int jz4760_pwm_pwm2_pins[] = { 0x82, };
static int jz4760_pwm_pwm3_pins[] = { 0x83, };
static int jz4760_pwm_pwm4_pins[] = { 0x84, };
static int jz4760_pwm_pwm5_pins[] = { 0x85, };
static int jz4760_pwm_pwm6_pins[] = { 0x6a, };
static int jz4760_pwm_pwm7_pins[] = { 0x6b, };
static int jz4760_otg_pins[] = { 0x8a, };

static u8 jz4760_uart3_data_funcs[] = { 0, 1, };
static u8 jz4760_mmc0_1bit_a_funcs[] = { 1, 1, 0, };

static const struct group_desc jz4760_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4760_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4760_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", jz4760_uart1_data, 0),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4760_uart1_hwflow, 0),
	INGENIC_PIN_GROUP("uart2-data", jz4760_uart2_data, 0),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4760_uart2_hwflow, 0),
	INGENIC_PIN_GROUP_FUNCS("uart3-data", jz4760_uart3_data,
				jz4760_uart3_data_funcs),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4760_uart3_hwflow, 0),
	INGENIC_PIN_GROUP("ssi0-dt-a", jz4760_ssi0_dt_a, 2),
	INGENIC_PIN_GROUP("ssi0-dt-b", jz4760_ssi0_dt_b, 1),
	INGENIC_PIN_GROUP("ssi0-dt-d", jz4760_ssi0_dt_d, 1),
	INGENIC_PIN_GROUP("ssi0-dt-e", jz4760_ssi0_dt_e, 0),
	INGENIC_PIN_GROUP("ssi0-dr-a", jz4760_ssi0_dr_a, 1),
	INGENIC_PIN_GROUP("ssi0-dr-b", jz4760_ssi0_dr_b, 1),
	INGENIC_PIN_GROUP("ssi0-dr-d", jz4760_ssi0_dr_d, 1),
	INGENIC_PIN_GROUP("ssi0-dr-e", jz4760_ssi0_dr_e, 0),
	INGENIC_PIN_GROUP("ssi0-clk-a", jz4760_ssi0_clk_a, 2),
	INGENIC_PIN_GROUP("ssi0-clk-b", jz4760_ssi0_clk_b, 1),
	INGENIC_PIN_GROUP("ssi0-clk-d", jz4760_ssi0_clk_d, 1),
	INGENIC_PIN_GROUP("ssi0-clk-e", jz4760_ssi0_clk_e, 0),
	INGENIC_PIN_GROUP("ssi0-gpc-b", jz4760_ssi0_gpc_b, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-d", jz4760_ssi0_gpc_d, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-e", jz4760_ssi0_gpc_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce0-a", jz4760_ssi0_ce0_a, 2),
	INGENIC_PIN_GROUP("ssi0-ce0-b", jz4760_ssi0_ce0_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-d", jz4760_ssi0_ce0_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-e", jz4760_ssi0_ce0_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce1-b", jz4760_ssi0_ce1_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-d", jz4760_ssi0_ce1_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-e", jz4760_ssi0_ce1_e, 0),
	INGENIC_PIN_GROUP("ssi1-dt-b-9", jz4760_ssi1_dt_b_9, 2),
	INGENIC_PIN_GROUP("ssi1-dt-b-21", jz4760_ssi1_dt_b_21, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d-12", jz4760_ssi1_dt_d_12, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d-21", jz4760_ssi1_dt_d_21, 2),
	INGENIC_PIN_GROUP("ssi1-dt-e", jz4760_ssi1_dt_e, 1),
	INGENIC_PIN_GROUP("ssi1-dt-f", jz4760_ssi1_dt_f, 2),
	INGENIC_PIN_GROUP("ssi1-dr-b-6", jz4760_ssi1_dr_b_6, 2),
	INGENIC_PIN_GROUP("ssi1-dr-b-20", jz4760_ssi1_dr_b_20, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d-13", jz4760_ssi1_dr_d_13, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d-20", jz4760_ssi1_dr_d_20, 2),
	INGENIC_PIN_GROUP("ssi1-dr-e", jz4760_ssi1_dr_e, 1),
	INGENIC_PIN_GROUP("ssi1-dr-f", jz4760_ssi1_dr_f, 2),
	INGENIC_PIN_GROUP("ssi1-clk-b-7", jz4760_ssi1_clk_b_7, 2),
	INGENIC_PIN_GROUP("ssi1-clk-b-28", jz4760_ssi1_clk_b_28, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", jz4760_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e-7", jz4760_ssi1_clk_e_7, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e-15", jz4760_ssi1_clk_e_15, 1),
	INGENIC_PIN_GROUP("ssi1-clk-f", jz4760_ssi1_clk_f, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-b", jz4760_ssi1_gpc_b, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-d", jz4760_ssi1_gpc_d, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-e", jz4760_ssi1_gpc_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce0-b-8", jz4760_ssi1_ce0_b_8, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-b-29", jz4760_ssi1_ce0_b_29, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-d", jz4760_ssi1_ce0_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-e-6", jz4760_ssi1_ce0_e_6, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-e-16", jz4760_ssi1_ce0_e_16, 1),
	INGENIC_PIN_GROUP("ssi1-ce0-f", jz4760_ssi1_ce0_f, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-b", jz4760_ssi1_ce1_b, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-d", jz4760_ssi1_ce1_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-e", jz4760_ssi1_ce1_e, 1),
	INGENIC_PIN_GROUP_FUNCS("mmc0-1bit-a", jz4760_mmc0_1bit_a,
				jz4760_mmc0_1bit_a_funcs),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4760_mmc0_4bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4760_mmc0_1bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4760_mmc0_4bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-8bit-e", jz4760_mmc0_8bit_e, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4760_mmc1_1bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4760_mmc1_4bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4760_mmc1_1bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4760_mmc1_4bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-8bit-e", jz4760_mmc1_8bit_e, 1),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4760_mmc2_1bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4760_mmc2_4bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4760_mmc2_1bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4760_mmc2_4bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-8bit-e", jz4760_mmc2_8bit_e, 2),
	INGENIC_PIN_GROUP("nemc-8bit-data", jz4760_nemc_8bit_data, 0),
	INGENIC_PIN_GROUP("nemc-16bit-data", jz4760_nemc_16bit_data, 0),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4760_nemc_cle_ale, 0),
	INGENIC_PIN_GROUP("nemc-addr", jz4760_nemc_addr, 0),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4760_nemc_rd_we, 0),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4760_nemc_frd_fwe, 0),
	INGENIC_PIN_GROUP("nemc-wait", jz4760_nemc_wait, 0),
	INGENIC_PIN_GROUP("nemc-cs1", jz4760_nemc_cs1, 0),
	INGENIC_PIN_GROUP("nemc-cs2", jz4760_nemc_cs2, 0),
	INGENIC_PIN_GROUP("nemc-cs3", jz4760_nemc_cs3, 0),
	INGENIC_PIN_GROUP("nemc-cs4", jz4760_nemc_cs4, 0),
	INGENIC_PIN_GROUP("nemc-cs5", jz4760_nemc_cs5, 0),
	INGENIC_PIN_GROUP("nemc-cs6", jz4760_nemc_cs6, 0),
	INGENIC_PIN_GROUP("i2c0-data", jz4760_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data", jz4760_i2c1, 0),
	INGENIC_PIN_GROUP("cim-data", jz4760_cim, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4760_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4760_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4760_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4760_lcd_24bit, 0),
	INGENIC_PIN_GROUP("lcd-special", jz4760_lcd_special, 1),
	INGENIC_PIN_GROUP("lcd-generic", jz4760_lcd_generic, 0),
	INGENIC_PIN_GROUP("pwm0", jz4760_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4760_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4760_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4760_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4760_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4760_pwm_pwm5, 0),
	INGENIC_PIN_GROUP("pwm6", jz4760_pwm_pwm6, 0),
	INGENIC_PIN_GROUP("pwm7", jz4760_pwm_pwm7, 0),
	INGENIC_PIN_GROUP("otg-vbus", jz4760_otg, 0),
};

static const char *jz4760_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4760_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4760_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4760_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
static const char *jz4760_ssi0_groups[] = {
	"ssi0-dt-a", "ssi0-dt-b", "ssi0-dt-d", "ssi0-dt-e",
	"ssi0-dr-a", "ssi0-dr-b", "ssi0-dr-d", "ssi0-dr-e",
	"ssi0-clk-a", "ssi0-clk-b", "ssi0-clk-d", "ssi0-clk-e",
	"ssi0-gpc-b", "ssi0-gpc-d", "ssi0-gpc-e",
	"ssi0-ce0-a", "ssi0-ce0-b", "ssi0-ce0-d", "ssi0-ce0-e",
	"ssi0-ce1-b", "ssi0-ce1-d", "ssi0-ce1-e",
};
static const char *jz4760_ssi1_groups[] = {
	"ssi1-dt-b-9", "ssi1-dt-b-21", "ssi1-dt-d-12", "ssi1-dt-d-21", "ssi1-dt-e", "ssi1-dt-f",
	"ssi1-dr-b-6", "ssi1-dr-b-20", "ssi1-dr-d-13", "ssi1-dr-d-20", "ssi1-dr-e", "ssi1-dr-f",
	"ssi1-clk-b-7", "ssi1-clk-b-28", "ssi1-clk-d", "ssi1-clk-e-7", "ssi1-clk-e-15", "ssi1-clk-f",
	"ssi1-gpc-b", "ssi1-gpc-d", "ssi1-gpc-e",
	"ssi1-ce0-b-8", "ssi1-ce0-b-29", "ssi1-ce0-d", "ssi1-ce0-e-6", "ssi1-ce0-e-16", "ssi1-ce0-f",
	"ssi1-ce1-b", "ssi1-ce1-d", "ssi1-ce1-e",
};
static const char *jz4760_mmc0_groups[] = {
	"mmc0-1bit-a", "mmc0-4bit-a",
	"mmc0-1bit-e", "mmc0-4bit-e", "mmc0-8bit-e",
};
static const char *jz4760_mmc1_groups[] = {
	"mmc1-1bit-d", "mmc1-4bit-d",
	"mmc1-1bit-e", "mmc1-4bit-e", "mmc1-8bit-e",
};
static const char *jz4760_mmc2_groups[] = {
	"mmc2-1bit-b", "mmc2-4bit-b",
	"mmc2-1bit-e", "mmc2-4bit-e", "mmc2-8bit-e",
};
static const char *jz4760_nemc_groups[] = {
	"nemc-8bit-data", "nemc-16bit-data", "nemc-cle-ale",
	"nemc-addr", "nemc-rd-we", "nemc-frd-fwe", "nemc-wait",
};
static const char *jz4760_cs1_groups[] = { "nemc-cs1", };
static const char *jz4760_cs2_groups[] = { "nemc-cs2", };
static const char *jz4760_cs3_groups[] = { "nemc-cs3", };
static const char *jz4760_cs4_groups[] = { "nemc-cs4", };
static const char *jz4760_cs5_groups[] = { "nemc-cs5", };
static const char *jz4760_cs6_groups[] = { "nemc-cs6", };
static const char *jz4760_i2c0_groups[] = { "i2c0-data", };
static const char *jz4760_i2c1_groups[] = { "i2c1-data", };
static const char *jz4760_cim_groups[] = { "cim-data", };
static const char *jz4760_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4760_pwm0_groups[] = { "pwm0", };
static const char *jz4760_pwm1_groups[] = { "pwm1", };
static const char *jz4760_pwm2_groups[] = { "pwm2", };
static const char *jz4760_pwm3_groups[] = { "pwm3", };
static const char *jz4760_pwm4_groups[] = { "pwm4", };
static const char *jz4760_pwm5_groups[] = { "pwm5", };
static const char *jz4760_pwm6_groups[] = { "pwm6", };
static const char *jz4760_pwm7_groups[] = { "pwm7", };
static const char *jz4760_otg_groups[] = { "otg-vbus", };

static const struct function_desc jz4760_functions[] = {
	{ "uart0", jz4760_uart0_groups, ARRAY_SIZE(jz4760_uart0_groups), },
	{ "uart1", jz4760_uart1_groups, ARRAY_SIZE(jz4760_uart1_groups), },
	{ "uart2", jz4760_uart2_groups, ARRAY_SIZE(jz4760_uart2_groups), },
	{ "uart3", jz4760_uart3_groups, ARRAY_SIZE(jz4760_uart3_groups), },
	{ "ssi0", jz4760_ssi0_groups, ARRAY_SIZE(jz4760_ssi0_groups), },
	{ "ssi1", jz4760_ssi1_groups, ARRAY_SIZE(jz4760_ssi1_groups), },
	{ "mmc0", jz4760_mmc0_groups, ARRAY_SIZE(jz4760_mmc0_groups), },
	{ "mmc1", jz4760_mmc1_groups, ARRAY_SIZE(jz4760_mmc1_groups), },
	{ "mmc2", jz4760_mmc2_groups, ARRAY_SIZE(jz4760_mmc2_groups), },
	{ "nemc", jz4760_nemc_groups, ARRAY_SIZE(jz4760_nemc_groups), },
	{ "nemc-cs1", jz4760_cs1_groups, ARRAY_SIZE(jz4760_cs1_groups), },
	{ "nemc-cs2", jz4760_cs2_groups, ARRAY_SIZE(jz4760_cs2_groups), },
	{ "nemc-cs3", jz4760_cs3_groups, ARRAY_SIZE(jz4760_cs3_groups), },
	{ "nemc-cs4", jz4760_cs4_groups, ARRAY_SIZE(jz4760_cs4_groups), },
	{ "nemc-cs5", jz4760_cs5_groups, ARRAY_SIZE(jz4760_cs5_groups), },
	{ "nemc-cs6", jz4760_cs6_groups, ARRAY_SIZE(jz4760_cs6_groups), },
	{ "i2c0", jz4760_i2c0_groups, ARRAY_SIZE(jz4760_i2c0_groups), },
	{ "i2c1", jz4760_i2c1_groups, ARRAY_SIZE(jz4760_i2c1_groups), },
	{ "cim", jz4760_cim_groups, ARRAY_SIZE(jz4760_cim_groups), },
	{ "lcd", jz4760_lcd_groups, ARRAY_SIZE(jz4760_lcd_groups), },
	{ "pwm0", jz4760_pwm0_groups, ARRAY_SIZE(jz4760_pwm0_groups), },
	{ "pwm1", jz4760_pwm1_groups, ARRAY_SIZE(jz4760_pwm1_groups), },
	{ "pwm2", jz4760_pwm2_groups, ARRAY_SIZE(jz4760_pwm2_groups), },
	{ "pwm3", jz4760_pwm3_groups, ARRAY_SIZE(jz4760_pwm3_groups), },
	{ "pwm4", jz4760_pwm4_groups, ARRAY_SIZE(jz4760_pwm4_groups), },
	{ "pwm5", jz4760_pwm5_groups, ARRAY_SIZE(jz4760_pwm5_groups), },
	{ "pwm6", jz4760_pwm6_groups, ARRAY_SIZE(jz4760_pwm6_groups), },
	{ "pwm7", jz4760_pwm7_groups, ARRAY_SIZE(jz4760_pwm7_groups), },
	{ "otg", jz4760_otg_groups, ARRAY_SIZE(jz4760_otg_groups), },
};

static const struct ingenic_chip_info jz4760_chip_info = {
	.num_chips = 6,
	.reg_offset = 0x100,
	.version = ID_JZ4760,
	.groups = jz4760_groups,
	.num_groups = ARRAY_SIZE(jz4760_groups),
	.functions = jz4760_functions,
	.num_functions = ARRAY_SIZE(jz4760_functions),
	.pull_ups = jz4760_pull_ups,
	.pull_downs = jz4760_pull_downs,
};

static const u32 jz4770_pull_ups[6] = {
	0x3fffffff, 0xfff0f3fc, 0xffffffff, 0xffff4fff, 0xfffffb7c, 0x0024f00f,
};

static const u32 jz4770_pull_downs[6] = {
	0x00000000, 0x000f0c03, 0x00000000, 0x0000b000, 0x00000483, 0x005b0ff0,
};

static int jz4770_uart0_data_pins[] = { 0xa0, 0xa3, };
static int jz4770_uart0_hwflow_pins[] = { 0xa1, 0xa2, };
static int jz4770_uart1_data_pins[] = { 0x7a, 0x7c, };
static int jz4770_uart1_hwflow_pins[] = { 0x7b, 0x7d, };
static int jz4770_uart2_data_pins[] = { 0x5c, 0x5e, };
static int jz4770_uart2_hwflow_pins[] = { 0x5d, 0x5f, };
static int jz4770_uart3_data_pins[] = { 0x6c, 0x85, };
static int jz4770_uart3_hwflow_pins[] = { 0x88, 0x89, };
static int jz4770_ssi0_dt_a_pins[] = { 0x15, };
static int jz4770_ssi0_dt_b_pins[] = { 0x35, };
static int jz4770_ssi0_dt_d_pins[] = { 0x75, };
static int jz4770_ssi0_dt_e_pins[] = { 0x91, };
static int jz4770_ssi0_dr_a_pins[] = { 0x14, };
static int jz4770_ssi0_dr_b_pins[] = { 0x34, };
static int jz4770_ssi0_dr_d_pins[] = { 0x74, };
static int jz4770_ssi0_dr_e_pins[] = { 0x8e, };
static int jz4770_ssi0_clk_a_pins[] = { 0x12, };
static int jz4770_ssi0_clk_b_pins[] = { 0x3c, };
static int jz4770_ssi0_clk_d_pins[] = { 0x78, };
static int jz4770_ssi0_clk_e_pins[] = { 0x8f, };
static int jz4770_ssi0_gpc_b_pins[] = { 0x3e, };
static int jz4770_ssi0_gpc_d_pins[] = { 0x76, };
static int jz4770_ssi0_gpc_e_pins[] = { 0x93, };
static int jz4770_ssi0_ce0_a_pins[] = { 0x13, };
static int jz4770_ssi0_ce0_b_pins[] = { 0x3d, };
static int jz4770_ssi0_ce0_d_pins[] = { 0x79, };
static int jz4770_ssi0_ce0_e_pins[] = { 0x90, };
static int jz4770_ssi0_ce1_b_pins[] = { 0x3f, };
static int jz4770_ssi0_ce1_d_pins[] = { 0x77, };
static int jz4770_ssi0_ce1_e_pins[] = { 0x92, };
static int jz4770_ssi1_dt_b_pins[] = { 0x35, };
static int jz4770_ssi1_dt_d_pins[] = { 0x75, };
static int jz4770_ssi1_dt_e_pins[] = { 0x91, };
static int jz4770_ssi1_dr_b_pins[] = { 0x34, };
static int jz4770_ssi1_dr_d_pins[] = { 0x74, };
static int jz4770_ssi1_dr_e_pins[] = { 0x8e, };
static int jz4770_ssi1_clk_b_pins[] = { 0x3c, };
static int jz4770_ssi1_clk_d_pins[] = { 0x78, };
static int jz4770_ssi1_clk_e_pins[] = { 0x8f, };
static int jz4770_ssi1_gpc_b_pins[] = { 0x3e, };
static int jz4770_ssi1_gpc_d_pins[] = { 0x76, };
static int jz4770_ssi1_gpc_e_pins[] = { 0x93, };
static int jz4770_ssi1_ce0_b_pins[] = { 0x3d, };
static int jz4770_ssi1_ce0_d_pins[] = { 0x79, };
static int jz4770_ssi1_ce0_e_pins[] = { 0x90, };
static int jz4770_ssi1_ce1_b_pins[] = { 0x3f, };
static int jz4770_ssi1_ce1_d_pins[] = { 0x77, };
static int jz4770_ssi1_ce1_e_pins[] = { 0x92, };
static int jz4770_mmc0_1bit_a_pins[] = { 0x12, 0x13, 0x14, };
static int jz4770_mmc0_4bit_a_pins[] = { 0x15, 0x16, 0x17, };
static int jz4770_mmc0_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4770_mmc0_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4770_mmc0_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4770_mmc1_1bit_d_pins[] = { 0x78, 0x79, 0x74, };
static int jz4770_mmc1_4bit_d_pins[] = { 0x75, 0x76, 0x77, };
static int jz4770_mmc1_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4770_mmc1_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4770_mmc1_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4770_mmc2_1bit_b_pins[] = { 0x3c, 0x3d, 0x34, };
static int jz4770_mmc2_4bit_b_pins[] = { 0x35, 0x3e, 0x3f, };
static int jz4770_mmc2_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4770_mmc2_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4770_mmc2_8bit_e_pins[] = { 0x98, 0x99, 0x9a, 0x9b, };
static int jz4770_nemc_8bit_data_pins[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
static int jz4770_nemc_16bit_data_pins[] = {
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static int jz4770_nemc_cle_ale_pins[] = { 0x20, 0x21, };
static int jz4770_nemc_addr_pins[] = { 0x22, 0x23, 0x24, 0x25, };
static int jz4770_nemc_rd_we_pins[] = { 0x10, 0x11, };
static int jz4770_nemc_frd_fwe_pins[] = { 0x12, 0x13, };
static int jz4770_nemc_wait_pins[] = { 0x1b, };
static int jz4770_nemc_cs1_pins[] = { 0x15, };
static int jz4770_nemc_cs2_pins[] = { 0x16, };
static int jz4770_nemc_cs3_pins[] = { 0x17, };
static int jz4770_nemc_cs4_pins[] = { 0x18, };
static int jz4770_nemc_cs5_pins[] = { 0x19, };
static int jz4770_nemc_cs6_pins[] = { 0x1a, };
static int jz4770_i2c0_pins[] = { 0x7e, 0x7f, };
static int jz4770_i2c1_pins[] = { 0x9e, 0x9f, };
static int jz4770_i2c2_pins[] = { 0xb0, 0xb1, };
static int jz4770_cim_8bit_pins[] = {
	0x26, 0x27, 0x28, 0x29,
	0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
};
static int jz4770_cim_12bit_pins[] = {
	0x32, 0x33, 0xb0, 0xb1,
};
static int jz4770_lcd_8bit_pins[] = {
	0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x4c, 0x4d,
	0x48, 0x52, 0x53,
};
static int jz4770_lcd_16bit_pins[] = {
	0x4e, 0x4f, 0x50, 0x51, 0x56, 0x57, 0x58, 0x59,
};
static int jz4770_lcd_18bit_pins[] = {
	0x5a, 0x5b,
};
static int jz4770_lcd_24bit_pins[] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b,
};
static int jz4770_lcd_special_pins[] = { 0x54, 0x4a, 0x41, 0x40, };
static int jz4770_lcd_generic_pins[] = { 0x49, };
static int jz4770_pwm_pwm0_pins[] = { 0x80, };
static int jz4770_pwm_pwm1_pins[] = { 0x81, };
static int jz4770_pwm_pwm2_pins[] = { 0x82, };
static int jz4770_pwm_pwm3_pins[] = { 0x83, };
static int jz4770_pwm_pwm4_pins[] = { 0x84, };
static int jz4770_pwm_pwm5_pins[] = { 0x85, };
static int jz4770_pwm_pwm6_pins[] = { 0x6a, };
static int jz4770_pwm_pwm7_pins[] = { 0x6b, };
static int jz4770_mac_rmii_pins[] = {
	0xa9, 0xab, 0xaa, 0xac, 0xa5, 0xa4, 0xad, 0xae, 0xa6, 0xa8,
};
static int jz4770_mac_mii_pins[] = {
	0x7b, 0x7a, 0x7d, 0x7c, 0xa7, 0x24, 0xaf,
};

static const struct group_desc jz4770_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4770_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4770_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", jz4770_uart1_data, 0),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4770_uart1_hwflow, 0),
	INGENIC_PIN_GROUP("uart2-data", jz4770_uart2_data, 0),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4770_uart2_hwflow, 0),
	INGENIC_PIN_GROUP_FUNCS("uart3-data", jz4770_uart3_data,
				jz4760_uart3_data_funcs),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4770_uart3_hwflow, 0),
	INGENIC_PIN_GROUP("ssi0-dt-a", jz4770_ssi0_dt_a, 2),
	INGENIC_PIN_GROUP("ssi0-dt-b", jz4770_ssi0_dt_b, 1),
	INGENIC_PIN_GROUP("ssi0-dt-d", jz4770_ssi0_dt_d, 1),
	INGENIC_PIN_GROUP("ssi0-dt-e", jz4770_ssi0_dt_e, 0),
	INGENIC_PIN_GROUP("ssi0-dr-a", jz4770_ssi0_dr_a, 1),
	INGENIC_PIN_GROUP("ssi0-dr-b", jz4770_ssi0_dr_b, 1),
	INGENIC_PIN_GROUP("ssi0-dr-d", jz4770_ssi0_dr_d, 1),
	INGENIC_PIN_GROUP("ssi0-dr-e", jz4770_ssi0_dr_e, 0),
	INGENIC_PIN_GROUP("ssi0-clk-a", jz4770_ssi0_clk_a, 2),
	INGENIC_PIN_GROUP("ssi0-clk-b", jz4770_ssi0_clk_b, 1),
	INGENIC_PIN_GROUP("ssi0-clk-d", jz4770_ssi0_clk_d, 1),
	INGENIC_PIN_GROUP("ssi0-clk-e", jz4770_ssi0_clk_e, 0),
	INGENIC_PIN_GROUP("ssi0-gpc-b", jz4770_ssi0_gpc_b, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-d", jz4770_ssi0_gpc_d, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-e", jz4770_ssi0_gpc_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce0-a", jz4770_ssi0_ce0_a, 2),
	INGENIC_PIN_GROUP("ssi0-ce0-b", jz4770_ssi0_ce0_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-d", jz4770_ssi0_ce0_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-e", jz4770_ssi0_ce0_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce1-b", jz4770_ssi0_ce1_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-d", jz4770_ssi0_ce1_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-e", jz4770_ssi0_ce1_e, 0),
	INGENIC_PIN_GROUP("ssi1-dt-b", jz4770_ssi1_dt_b, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d", jz4770_ssi1_dt_d, 2),
	INGENIC_PIN_GROUP("ssi1-dt-e", jz4770_ssi1_dt_e, 1),
	INGENIC_PIN_GROUP("ssi1-dr-b", jz4770_ssi1_dr_b, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d", jz4770_ssi1_dr_d, 2),
	INGENIC_PIN_GROUP("ssi1-dr-e", jz4770_ssi1_dr_e, 1),
	INGENIC_PIN_GROUP("ssi1-clk-b", jz4770_ssi1_clk_b, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", jz4770_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e", jz4770_ssi1_clk_e, 1),
	INGENIC_PIN_GROUP("ssi1-gpc-b", jz4770_ssi1_gpc_b, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-d", jz4770_ssi1_gpc_d, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-e", jz4770_ssi1_gpc_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce0-b", jz4770_ssi1_ce0_b, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-d", jz4770_ssi1_ce0_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-e", jz4770_ssi1_ce0_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce1-b", jz4770_ssi1_ce1_b, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-d", jz4770_ssi1_ce1_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-e", jz4770_ssi1_ce1_e, 1),
	INGENIC_PIN_GROUP_FUNCS("mmc0-1bit-a", jz4770_mmc0_1bit_a,
				jz4760_mmc0_1bit_a_funcs),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4770_mmc0_4bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4770_mmc0_1bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4770_mmc0_4bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-8bit-e", jz4770_mmc0_8bit_e, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4770_mmc1_1bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4770_mmc1_4bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4770_mmc1_1bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4770_mmc1_4bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-8bit-e", jz4770_mmc1_8bit_e, 1),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4770_mmc2_1bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4770_mmc2_4bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4770_mmc2_1bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4770_mmc2_4bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-8bit-e", jz4770_mmc2_8bit_e, 2),
	INGENIC_PIN_GROUP("nemc-8bit-data", jz4770_nemc_8bit_data, 0),
	INGENIC_PIN_GROUP("nemc-16bit-data", jz4770_nemc_16bit_data, 0),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4770_nemc_cle_ale, 0),
	INGENIC_PIN_GROUP("nemc-addr", jz4770_nemc_addr, 0),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4770_nemc_rd_we, 0),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4770_nemc_frd_fwe, 0),
	INGENIC_PIN_GROUP("nemc-wait", jz4770_nemc_wait, 0),
	INGENIC_PIN_GROUP("nemc-cs1", jz4770_nemc_cs1, 0),
	INGENIC_PIN_GROUP("nemc-cs2", jz4770_nemc_cs2, 0),
	INGENIC_PIN_GROUP("nemc-cs3", jz4770_nemc_cs3, 0),
	INGENIC_PIN_GROUP("nemc-cs4", jz4770_nemc_cs4, 0),
	INGENIC_PIN_GROUP("nemc-cs5", jz4770_nemc_cs5, 0),
	INGENIC_PIN_GROUP("nemc-cs6", jz4770_nemc_cs6, 0),
	INGENIC_PIN_GROUP("i2c0-data", jz4770_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data", jz4770_i2c1, 0),
	INGENIC_PIN_GROUP("i2c2-data", jz4770_i2c2, 2),
	INGENIC_PIN_GROUP("cim-data-8bit", jz4770_cim_8bit, 0),
	INGENIC_PIN_GROUP("cim-data-12bit", jz4770_cim_12bit, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4770_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4770_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4770_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4770_lcd_24bit, 0),
	INGENIC_PIN_GROUP("lcd-special", jz4770_lcd_special, 1),
	INGENIC_PIN_GROUP("lcd-generic", jz4770_lcd_generic, 0),
	INGENIC_PIN_GROUP("pwm0", jz4770_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4770_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4770_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4770_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4770_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4770_pwm_pwm5, 0),
	INGENIC_PIN_GROUP("pwm6", jz4770_pwm_pwm6, 0),
	INGENIC_PIN_GROUP("pwm7", jz4770_pwm_pwm7, 0),
	INGENIC_PIN_GROUP("mac-rmii", jz4770_mac_rmii, 0),
	INGENIC_PIN_GROUP("mac-mii", jz4770_mac_mii, 0),
	INGENIC_PIN_GROUP("otg-vbus", jz4760_otg, 0),
};

static const char *jz4770_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4770_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4770_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4770_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
static const char *jz4770_ssi0_groups[] = {
	"ssi0-dt-a", "ssi0-dt-b", "ssi0-dt-d", "ssi0-dt-e",
	"ssi0-dr-a", "ssi0-dr-b", "ssi0-dr-d", "ssi0-dr-e",
	"ssi0-clk-a", "ssi0-clk-b", "ssi0-clk-d", "ssi0-clk-e",
	"ssi0-gpc-b", "ssi0-gpc-d", "ssi0-gpc-e",
	"ssi0-ce0-a", "ssi0-ce0-b", "ssi0-ce0-d", "ssi0-ce0-e",
	"ssi0-ce1-b", "ssi0-ce1-d", "ssi0-ce1-e",
};
static const char *jz4770_ssi1_groups[] = {
	"ssi1-dt-b", "ssi1-dt-d", "ssi1-dt-e",
	"ssi1-dr-b", "ssi1-dr-d", "ssi1-dr-e",
	"ssi1-clk-b", "ssi1-clk-d", "ssi1-clk-e",
	"ssi1-gpc-b", "ssi1-gpc-d", "ssi1-gpc-e",
	"ssi1-ce0-b", "ssi1-ce0-d", "ssi1-ce0-e",
	"ssi1-ce1-b", "ssi1-ce1-d", "ssi1-ce1-e",
};
static const char *jz4770_mmc0_groups[] = {
	"mmc0-1bit-a", "mmc0-4bit-a",
	"mmc0-1bit-e", "mmc0-4bit-e", "mmc0-8bit-e",
};
static const char *jz4770_mmc1_groups[] = {
	"mmc1-1bit-d", "mmc1-4bit-d",
	"mmc1-1bit-e", "mmc1-4bit-e", "mmc1-8bit-e",
};
static const char *jz4770_mmc2_groups[] = {
	"mmc2-1bit-b", "mmc2-4bit-b",
	"mmc2-1bit-e", "mmc2-4bit-e", "mmc2-8bit-e",
};
static const char *jz4770_nemc_groups[] = {
	"nemc-8bit-data", "nemc-16bit-data", "nemc-cle-ale",
	"nemc-addr", "nemc-rd-we", "nemc-frd-fwe", "nemc-wait",
};
static const char *jz4770_cs1_groups[] = { "nemc-cs1", };
static const char *jz4770_cs2_groups[] = { "nemc-cs2", };
static const char *jz4770_cs3_groups[] = { "nemc-cs3", };
static const char *jz4770_cs4_groups[] = { "nemc-cs4", };
static const char *jz4770_cs5_groups[] = { "nemc-cs5", };
static const char *jz4770_cs6_groups[] = { "nemc-cs6", };
static const char *jz4770_i2c0_groups[] = { "i2c0-data", };
static const char *jz4770_i2c1_groups[] = { "i2c1-data", };
static const char *jz4770_i2c2_groups[] = { "i2c2-data", };
static const char *jz4770_cim_groups[] = { "cim-data-8bit", "cim-data-12bit", };
static const char *jz4770_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4770_pwm0_groups[] = { "pwm0", };
static const char *jz4770_pwm1_groups[] = { "pwm1", };
static const char *jz4770_pwm2_groups[] = { "pwm2", };
static const char *jz4770_pwm3_groups[] = { "pwm3", };
static const char *jz4770_pwm4_groups[] = { "pwm4", };
static const char *jz4770_pwm5_groups[] = { "pwm5", };
static const char *jz4770_pwm6_groups[] = { "pwm6", };
static const char *jz4770_pwm7_groups[] = { "pwm7", };
static const char *jz4770_mac_groups[] = { "mac-rmii", "mac-mii", };

static const struct function_desc jz4770_functions[] = {
	{ "uart0", jz4770_uart0_groups, ARRAY_SIZE(jz4770_uart0_groups), },
	{ "uart1", jz4770_uart1_groups, ARRAY_SIZE(jz4770_uart1_groups), },
	{ "uart2", jz4770_uart2_groups, ARRAY_SIZE(jz4770_uart2_groups), },
	{ "uart3", jz4770_uart3_groups, ARRAY_SIZE(jz4770_uart3_groups), },
	{ "ssi0", jz4770_ssi0_groups, ARRAY_SIZE(jz4770_ssi0_groups), },
	{ "ssi1", jz4770_ssi1_groups, ARRAY_SIZE(jz4770_ssi1_groups), },
	{ "mmc0", jz4770_mmc0_groups, ARRAY_SIZE(jz4770_mmc0_groups), },
	{ "mmc1", jz4770_mmc1_groups, ARRAY_SIZE(jz4770_mmc1_groups), },
	{ "mmc2", jz4770_mmc2_groups, ARRAY_SIZE(jz4770_mmc2_groups), },
	{ "nemc", jz4770_nemc_groups, ARRAY_SIZE(jz4770_nemc_groups), },
	{ "nemc-cs1", jz4770_cs1_groups, ARRAY_SIZE(jz4770_cs1_groups), },
	{ "nemc-cs2", jz4770_cs2_groups, ARRAY_SIZE(jz4770_cs2_groups), },
	{ "nemc-cs3", jz4770_cs3_groups, ARRAY_SIZE(jz4770_cs3_groups), },
	{ "nemc-cs4", jz4770_cs4_groups, ARRAY_SIZE(jz4770_cs4_groups), },
	{ "nemc-cs5", jz4770_cs5_groups, ARRAY_SIZE(jz4770_cs5_groups), },
	{ "nemc-cs6", jz4770_cs6_groups, ARRAY_SIZE(jz4770_cs6_groups), },
	{ "i2c0", jz4770_i2c0_groups, ARRAY_SIZE(jz4770_i2c0_groups), },
	{ "i2c1", jz4770_i2c1_groups, ARRAY_SIZE(jz4770_i2c1_groups), },
	{ "i2c2", jz4770_i2c2_groups, ARRAY_SIZE(jz4770_i2c2_groups), },
	{ "cim", jz4770_cim_groups, ARRAY_SIZE(jz4770_cim_groups), },
	{ "lcd", jz4770_lcd_groups, ARRAY_SIZE(jz4770_lcd_groups), },
	{ "pwm0", jz4770_pwm0_groups, ARRAY_SIZE(jz4770_pwm0_groups), },
	{ "pwm1", jz4770_pwm1_groups, ARRAY_SIZE(jz4770_pwm1_groups), },
	{ "pwm2", jz4770_pwm2_groups, ARRAY_SIZE(jz4770_pwm2_groups), },
	{ "pwm3", jz4770_pwm3_groups, ARRAY_SIZE(jz4770_pwm3_groups), },
	{ "pwm4", jz4770_pwm4_groups, ARRAY_SIZE(jz4770_pwm4_groups), },
	{ "pwm5", jz4770_pwm5_groups, ARRAY_SIZE(jz4770_pwm5_groups), },
	{ "pwm6", jz4770_pwm6_groups, ARRAY_SIZE(jz4770_pwm6_groups), },
	{ "pwm7", jz4770_pwm7_groups, ARRAY_SIZE(jz4770_pwm7_groups), },
	{ "mac", jz4770_mac_groups, ARRAY_SIZE(jz4770_mac_groups), },
	{ "otg", jz4760_otg_groups, ARRAY_SIZE(jz4760_otg_groups), },
};

static const struct ingenic_chip_info jz4770_chip_info = {
	.num_chips = 6,
	.reg_offset = 0x100,
	.version = ID_JZ4770,
	.groups = jz4770_groups,
	.num_groups = ARRAY_SIZE(jz4770_groups),
	.functions = jz4770_functions,
	.num_functions = ARRAY_SIZE(jz4770_functions),
	.pull_ups = jz4770_pull_ups,
	.pull_downs = jz4770_pull_downs,
};

static const u32 jz4775_pull_ups[7] = {
	0x28ff00ff, 0xf030f3fc, 0x0fffffff, 0xfffe4000, 0xf0f0000c, 0x0000f00f, 0x0000f3c0,
};

static const u32 jz4775_pull_downs[7] = {
	0x00000000, 0x00030c03, 0x00000000, 0x00008000, 0x00000403, 0x00000ff0, 0x00030c00,
};

static int jz4775_uart0_data_pins[] = { 0xa0, 0xa3, };
static int jz4775_uart0_hwflow_pins[] = { 0xa1, 0xa2, };
static int jz4775_uart1_data_pins[] = { 0x7a, 0x7c, };
static int jz4775_uart1_hwflow_pins[] = { 0x7b, 0x7d, };
static int jz4775_uart2_data_c_pins[] = { 0x54, 0x4a, };
static int jz4775_uart2_data_f_pins[] = { 0xa5, 0xa4, };
static int jz4775_uart3_data_pins[] = { 0x1e, 0x1f, };
static int jz4775_ssi_dt_a_pins[] = { 0x13, };
static int jz4775_ssi_dt_d_pins[] = { 0x75, };
static int jz4775_ssi_dr_a_pins[] = { 0x14, };
static int jz4775_ssi_dr_d_pins[] = { 0x74, };
static int jz4775_ssi_clk_a_pins[] = { 0x12, };
static int jz4775_ssi_clk_d_pins[] = { 0x78, };
static int jz4775_ssi_gpc_pins[] = { 0x76, };
static int jz4775_ssi_ce0_a_pins[] = { 0x17, };
static int jz4775_ssi_ce0_d_pins[] = { 0x79, };
static int jz4775_ssi_ce1_pins[] = { 0x77, };
static int jz4775_mmc0_1bit_a_pins[] = { 0x12, 0x13, 0x14, };
static int jz4775_mmc0_4bit_a_pins[] = { 0x15, 0x16, 0x17, };
static int jz4775_mmc0_8bit_a_pins[] = { 0x04, 0x05, 0x06, 0x07, };
static int jz4775_mmc0_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4775_mmc0_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4775_mmc1_1bit_d_pins[] = { 0x78, 0x79, 0x74, };
static int jz4775_mmc1_4bit_d_pins[] = { 0x75, 0x76, 0x77, };
static int jz4775_mmc1_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4775_mmc1_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4775_mmc2_1bit_b_pins[] = { 0x3c, 0x3d, 0x34, };
static int jz4775_mmc2_4bit_b_pins[] = { 0x35, 0x3e, 0x3f, };
static int jz4775_mmc2_1bit_e_pins[] = { 0x9c, 0x9d, 0x94, };
static int jz4775_mmc2_4bit_e_pins[] = { 0x95, 0x96, 0x97, };
static int jz4775_nemc_8bit_data_pins[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
static int jz4775_nemc_16bit_data_pins[] = {
	0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1,
};
static int jz4775_nemc_cle_ale_pins[] = { 0x20, 0x21, };
static int jz4775_nemc_addr_pins[] = { 0x22, 0x23, 0x24, 0x25, };
static int jz4775_nemc_rd_we_pins[] = { 0x10, 0x11, };
static int jz4775_nemc_frd_fwe_pins[] = { 0x12, 0x13, };
static int jz4775_nemc_wait_pins[] = { 0x1b, };
static int jz4775_nemc_cs1_pins[] = { 0x15, };
static int jz4775_nemc_cs2_pins[] = { 0x16, };
static int jz4775_nemc_cs3_pins[] = { 0x17, };
static int jz4775_i2c0_pins[] = { 0x7e, 0x7f, };
static int jz4775_i2c1_pins[] = { 0x9e, 0x9f, };
static int jz4775_i2c2_pins[] = { 0x80, 0x83, };
static int jz4775_i2s_data_tx_pins[] = { 0xa3, };
static int jz4775_i2s_data_rx_pins[] = { 0xa2, };
static int jz4775_i2s_clk_txrx_pins[] = { 0xa0, 0xa1, };
static int jz4775_i2s_sysclk_pins[] = { 0x83, };
static int jz4775_dmic_pins[] = { 0xaa, 0xab, };
static int jz4775_cim_pins[] = {
	0x26, 0x27, 0x28, 0x29,
	0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
};
static int jz4775_lcd_8bit_pins[] = {
	0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x4c, 0x4d,
	0x48, 0x52, 0x53,
};
static int jz4775_lcd_16bit_pins[] = {
	0x4e, 0x4f, 0x50, 0x51, 0x56, 0x57, 0x58, 0x59,
};
static int jz4775_lcd_18bit_pins[] = {
	0x5a, 0x5b,
};
static int jz4775_lcd_24bit_pins[] = {
	0x40, 0x41, 0x4a, 0x4b, 0x54, 0x55,
};
static int jz4775_lcd_special_pins[] = { 0x54, 0x4a, 0x41, 0x40, };
static int jz4775_lcd_generic_pins[] = { 0x49, };
static int jz4775_pwm_pwm0_pins[] = { 0x80, };
static int jz4775_pwm_pwm1_pins[] = { 0x81, };
static int jz4775_pwm_pwm2_pins[] = { 0x82, };
static int jz4775_pwm_pwm3_pins[] = { 0x83, };
static int jz4775_mac_rmii_pins[] = {
	0xa9, 0xab, 0xaa, 0xac, 0xa5, 0xa4, 0xad, 0xae, 0xa6, 0xa8,
};
static int jz4775_mac_mii_pins[] = {
	0x7b, 0x7a, 0x7d, 0x7c, 0xa7, 0x24, 0xaf,
};
static int jz4775_mac_rgmii_pins[] = {
	0xa9, 0x7b, 0x7a, 0xab, 0xaa, 0xac, 0x7d, 0x7c, 0xa5, 0xa4,
	0xad, 0xae, 0xa7, 0xa6,
};
static int jz4775_mac_gmii_pins[] = {
	0x31, 0x30, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a,
	0xa8, 0x28, 0x24, 0xaf,
};
static int jz4775_otg_pins[] = { 0x8a, };

static u8 jz4775_uart3_data_funcs[] = { 0, 1, };
static u8 jz4775_mac_mii_funcs[] = { 1, 1, 1, 1, 0, 1, 0, };
static u8 jz4775_mac_rgmii_funcs[] = {
	0, 1, 1, 0, 0, 0, 1, 1, 0, 0,
	0, 0, 0, 0,
};
static u8 jz4775_mac_gmii_funcs[] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 1, 1, 0,
};

static const struct group_desc jz4775_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4775_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4775_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", jz4775_uart1_data, 0),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4775_uart1_hwflow, 0),
	INGENIC_PIN_GROUP("uart2-data-c", jz4775_uart2_data_c, 2),
	INGENIC_PIN_GROUP("uart2-data-f", jz4775_uart2_data_f, 1),
	INGENIC_PIN_GROUP_FUNCS("uart3-data", jz4775_uart3_data,
				jz4775_uart3_data_funcs),
	INGENIC_PIN_GROUP("ssi-dt-a", jz4775_ssi_dt_a, 2),
	INGENIC_PIN_GROUP("ssi-dt-d", jz4775_ssi_dt_d, 1),
	INGENIC_PIN_GROUP("ssi-dr-a", jz4775_ssi_dr_a, 2),
	INGENIC_PIN_GROUP("ssi-dr-d", jz4775_ssi_dr_d, 1),
	INGENIC_PIN_GROUP("ssi-clk-a", jz4775_ssi_clk_a, 2),
	INGENIC_PIN_GROUP("ssi-clk-d", jz4775_ssi_clk_d, 1),
	INGENIC_PIN_GROUP("ssi-gpc", jz4775_ssi_gpc, 1),
	INGENIC_PIN_GROUP("ssi-ce0-a", jz4775_ssi_ce0_a, 2),
	INGENIC_PIN_GROUP("ssi-ce0-d", jz4775_ssi_ce0_d, 1),
	INGENIC_PIN_GROUP("ssi-ce1", jz4775_ssi_ce1, 1),
	INGENIC_PIN_GROUP("mmc0-1bit-a", jz4775_mmc0_1bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4775_mmc0_4bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-8bit-a", jz4775_mmc0_8bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4775_mmc0_1bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4775_mmc0_4bit_e, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4775_mmc1_1bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4775_mmc1_4bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4775_mmc1_1bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4775_mmc1_4bit_e, 1),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4775_mmc2_1bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4775_mmc2_4bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4775_mmc2_1bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4775_mmc2_4bit_e, 2),
	INGENIC_PIN_GROUP("nemc-8bit-data", jz4775_nemc_8bit_data, 0),
	INGENIC_PIN_GROUP("nemc-16bit-data", jz4775_nemc_16bit_data, 1),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4775_nemc_cle_ale, 0),
	INGENIC_PIN_GROUP("nemc-addr", jz4775_nemc_addr, 0),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4775_nemc_rd_we, 0),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4775_nemc_frd_fwe, 0),
	INGENIC_PIN_GROUP("nemc-wait", jz4775_nemc_wait, 0),
	INGENIC_PIN_GROUP("nemc-cs1", jz4775_nemc_cs1, 0),
	INGENIC_PIN_GROUP("nemc-cs2", jz4775_nemc_cs2, 0),
	INGENIC_PIN_GROUP("nemc-cs3", jz4775_nemc_cs3, 0),
	INGENIC_PIN_GROUP("i2c0-data", jz4775_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data", jz4775_i2c1, 0),
	INGENIC_PIN_GROUP("i2c2-data", jz4775_i2c2, 1),
	INGENIC_PIN_GROUP("i2s-data-tx", jz4775_i2s_data_tx, 1),
	INGENIC_PIN_GROUP("i2s-data-rx", jz4775_i2s_data_rx, 1),
	INGENIC_PIN_GROUP("i2s-clk-txrx", jz4775_i2s_clk_txrx, 1),
	INGENIC_PIN_GROUP("i2s-sysclk", jz4775_i2s_sysclk, 2),
	INGENIC_PIN_GROUP("dmic", jz4775_dmic, 1),
	INGENIC_PIN_GROUP("cim-data", jz4775_cim, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4775_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4775_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4775_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4775_lcd_24bit, 0),
	INGENIC_PIN_GROUP("lcd-generic", jz4775_lcd_generic, 0),
	INGENIC_PIN_GROUP("lcd-special", jz4775_lcd_special, 1),
	INGENIC_PIN_GROUP("pwm0", jz4775_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4775_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4775_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4775_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("mac-rmii", jz4775_mac_rmii, 0),
	INGENIC_PIN_GROUP_FUNCS("mac-mii", jz4775_mac_mii,
				jz4775_mac_mii_funcs),
	INGENIC_PIN_GROUP_FUNCS("mac-rgmii", jz4775_mac_rgmii,
				jz4775_mac_rgmii_funcs),
	INGENIC_PIN_GROUP_FUNCS("mac-gmii", jz4775_mac_gmii,
				jz4775_mac_gmii_funcs),
	INGENIC_PIN_GROUP("otg-vbus", jz4775_otg, 0),
};

static const char *jz4775_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4775_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4775_uart2_groups[] = { "uart2-data-c", "uart2-data-f", };
static const char *jz4775_uart3_groups[] = { "uart3-data", };
static const char *jz4775_ssi_groups[] = {
	"ssi-dt-a", "ssi-dt-d",
	"ssi-dr-a", "ssi-dr-d",
	"ssi-clk-a", "ssi-clk-d",
	"ssi-gpc",
	"ssi-ce0-a", "ssi-ce0-d",
	"ssi-ce1",
};
static const char *jz4775_mmc0_groups[] = {
	"mmc0-1bit-a", "mmc0-4bit-a", "mmc0-8bit-a",
	"mmc0-1bit-e", "mmc0-4bit-e",
};
static const char *jz4775_mmc1_groups[] = {
	"mmc1-1bit-d", "mmc1-4bit-d",
	"mmc1-1bit-e", "mmc1-4bit-e",
};
static const char *jz4775_mmc2_groups[] = {
	"mmc2-1bit-b", "mmc2-4bit-b",
	"mmc2-1bit-e", "mmc2-4bit-e",
};
static const char *jz4775_nemc_groups[] = {
	"nemc-8bit-data", "nemc-16bit-data", "nemc-cle-ale",
	"nemc-addr", "nemc-rd-we", "nemc-frd-fwe", "nemc-wait",
};
static const char *jz4775_cs1_groups[] = { "nemc-cs1", };
static const char *jz4775_cs2_groups[] = { "nemc-cs2", };
static const char *jz4775_cs3_groups[] = { "nemc-cs3", };
static const char *jz4775_i2c0_groups[] = { "i2c0-data", };
static const char *jz4775_i2c1_groups[] = { "i2c1-data", };
static const char *jz4775_i2c2_groups[] = { "i2c2-data", };
static const char *jz4775_i2s_groups[] = {
	"i2s-data-tx", "i2s-data-rx", "i2s-clk-txrx", "i2s-sysclk",
};
static const char *jz4775_dmic_groups[] = { "dmic", };
static const char *jz4775_cim_groups[] = { "cim-data", };
static const char *jz4775_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};
static const char *jz4775_pwm0_groups[] = { "pwm0", };
static const char *jz4775_pwm1_groups[] = { "pwm1", };
static const char *jz4775_pwm2_groups[] = { "pwm2", };
static const char *jz4775_pwm3_groups[] = { "pwm3", };
static const char *jz4775_mac_groups[] = {
	"mac-rmii", "mac-mii", "mac-rgmii", "mac-gmii",
};
static const char *jz4775_otg_groups[] = { "otg-vbus", };

static const struct function_desc jz4775_functions[] = {
	{ "uart0", jz4775_uart0_groups, ARRAY_SIZE(jz4775_uart0_groups), },
	{ "uart1", jz4775_uart1_groups, ARRAY_SIZE(jz4775_uart1_groups), },
	{ "uart2", jz4775_uart2_groups, ARRAY_SIZE(jz4775_uart2_groups), },
	{ "uart3", jz4775_uart3_groups, ARRAY_SIZE(jz4775_uart3_groups), },
	{ "ssi", jz4775_ssi_groups, ARRAY_SIZE(jz4775_ssi_groups), },
	{ "mmc0", jz4775_mmc0_groups, ARRAY_SIZE(jz4775_mmc0_groups), },
	{ "mmc1", jz4775_mmc1_groups, ARRAY_SIZE(jz4775_mmc1_groups), },
	{ "mmc2", jz4775_mmc2_groups, ARRAY_SIZE(jz4775_mmc2_groups), },
	{ "nemc", jz4775_nemc_groups, ARRAY_SIZE(jz4775_nemc_groups), },
	{ "nemc-cs1", jz4775_cs1_groups, ARRAY_SIZE(jz4775_cs1_groups), },
	{ "nemc-cs2", jz4775_cs2_groups, ARRAY_SIZE(jz4775_cs2_groups), },
	{ "nemc-cs3", jz4775_cs3_groups, ARRAY_SIZE(jz4775_cs3_groups), },
	{ "i2c0", jz4775_i2c0_groups, ARRAY_SIZE(jz4775_i2c0_groups), },
	{ "i2c1", jz4775_i2c1_groups, ARRAY_SIZE(jz4775_i2c1_groups), },
	{ "i2c2", jz4775_i2c2_groups, ARRAY_SIZE(jz4775_i2c2_groups), },
	{ "i2s", jz4775_i2s_groups, ARRAY_SIZE(jz4775_i2s_groups), },
	{ "dmic", jz4775_dmic_groups, ARRAY_SIZE(jz4775_dmic_groups), },
	{ "cim", jz4775_cim_groups, ARRAY_SIZE(jz4775_cim_groups), },
	{ "lcd", jz4775_lcd_groups, ARRAY_SIZE(jz4775_lcd_groups), },
	{ "pwm0", jz4775_pwm0_groups, ARRAY_SIZE(jz4775_pwm0_groups), },
	{ "pwm1", jz4775_pwm1_groups, ARRAY_SIZE(jz4775_pwm1_groups), },
	{ "pwm2", jz4775_pwm2_groups, ARRAY_SIZE(jz4775_pwm2_groups), },
	{ "pwm3", jz4775_pwm3_groups, ARRAY_SIZE(jz4775_pwm3_groups), },
	{ "mac", jz4775_mac_groups, ARRAY_SIZE(jz4775_mac_groups), },
	{ "otg", jz4775_otg_groups, ARRAY_SIZE(jz4775_otg_groups), },
};

static const struct ingenic_chip_info jz4775_chip_info = {
	.num_chips = 7,
	.reg_offset = 0x100,
	.version = ID_JZ4775,
	.groups = jz4775_groups,
	.num_groups = ARRAY_SIZE(jz4775_groups),
	.functions = jz4775_functions,
	.num_functions = ARRAY_SIZE(jz4775_functions),
	.pull_ups = jz4775_pull_ups,
	.pull_downs = jz4775_pull_downs,
};

static const u32 jz4780_pull_ups[6] = {
	0x3fffffff, 0xfff0f3fc, 0x0fffffff, 0xffff4fff, 0xfffffb7c, 0x7fa7f00f,
};

static const u32 jz4780_pull_downs[6] = {
	0x00000000, 0x000f0c03, 0x00000000, 0x0000b000, 0x00000483, 0x00580ff0,
};

static int jz4780_uart2_data_pins[] = { 0x66, 0x67, };
static int jz4780_uart2_hwflow_pins[] = { 0x65, 0x64, };
static int jz4780_uart4_data_pins[] = { 0x54, 0x4a, };
static int jz4780_ssi0_dt_a_19_pins[] = { 0x13, };
static int jz4780_ssi0_dt_a_21_pins[] = { 0x15, };
static int jz4780_ssi0_dt_a_28_pins[] = { 0x1c, };
static int jz4780_ssi0_dt_b_pins[] = { 0x3d, };
static int jz4780_ssi0_dt_d_pins[] = { 0x79, };
static int jz4780_ssi0_dr_a_20_pins[] = { 0x14, };
static int jz4780_ssi0_dr_a_27_pins[] = { 0x1b, };
static int jz4780_ssi0_dr_b_pins[] = { 0x34, };
static int jz4780_ssi0_dr_d_pins[] = { 0x74, };
static int jz4780_ssi0_clk_a_pins[] = { 0x12, };
static int jz4780_ssi0_clk_b_5_pins[] = { 0x25, };
static int jz4780_ssi0_clk_b_28_pins[] = { 0x3c, };
static int jz4780_ssi0_clk_d_pins[] = { 0x78, };
static int jz4780_ssi0_gpc_b_pins[] = { 0x3e, };
static int jz4780_ssi0_gpc_d_pins[] = { 0x76, };
static int jz4780_ssi0_ce0_a_23_pins[] = { 0x17, };
static int jz4780_ssi0_ce0_a_25_pins[] = { 0x19, };
static int jz4780_ssi0_ce0_b_pins[] = { 0x3f, };
static int jz4780_ssi0_ce0_d_pins[] = { 0x77, };
static int jz4780_ssi0_ce1_b_pins[] = { 0x35, };
static int jz4780_ssi0_ce1_d_pins[] = { 0x75, };
static int jz4780_ssi1_dt_b_pins[] = { 0x3d, };
static int jz4780_ssi1_dt_d_pins[] = { 0x79, };
static int jz4780_ssi1_dr_b_pins[] = { 0x34, };
static int jz4780_ssi1_dr_d_pins[] = { 0x74, };
static int jz4780_ssi1_clk_b_pins[] = { 0x3c, };
static int jz4780_ssi1_clk_d_pins[] = { 0x78, };
static int jz4780_ssi1_gpc_b_pins[] = { 0x3e, };
static int jz4780_ssi1_gpc_d_pins[] = { 0x76, };
static int jz4780_ssi1_ce0_b_pins[] = { 0x3f, };
static int jz4780_ssi1_ce0_d_pins[] = { 0x77, };
static int jz4780_ssi1_ce1_b_pins[] = { 0x35, };
static int jz4780_ssi1_ce1_d_pins[] = { 0x75, };
static int jz4780_mmc0_8bit_a_pins[] = { 0x04, 0x05, 0x06, 0x07, 0x18, };
static int jz4780_i2c3_pins[] = { 0x6a, 0x6b, };
static int jz4780_i2c4_e_pins[] = { 0x8c, 0x8d, };
static int jz4780_i2c4_f_pins[] = { 0xb9, 0xb8, };
static int jz4780_i2s_data_tx_pins[] = { 0x87, };
static int jz4780_i2s_data_rx_pins[] = { 0x86, };
static int jz4780_i2s_clk_txrx_pins[] = { 0x6c, 0x6d, };
static int jz4780_i2s_clk_rx_pins[] = { 0x88, 0x89, };
static int jz4780_i2s_sysclk_pins[] = { 0x85, };
static int jz4780_dmic_pins[] = { 0x32, 0x33, };
static int jz4780_hdmi_ddc_pins[] = { 0xb9, 0xb8, };

static u8 jz4780_i2s_clk_txrx_funcs[] = { 1, 0, };

static const struct group_desc jz4780_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4770_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4770_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", jz4770_uart1_data, 0),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4770_uart1_hwflow, 0),
	INGENIC_PIN_GROUP("uart2-data", jz4780_uart2_data, 1),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4780_uart2_hwflow, 1),
	INGENIC_PIN_GROUP_FUNCS("uart3-data", jz4770_uart3_data,
				jz4760_uart3_data_funcs),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4770_uart3_hwflow, 0),
	INGENIC_PIN_GROUP("uart4-data", jz4780_uart4_data, 2),
	INGENIC_PIN_GROUP("ssi0-dt-a-19", jz4780_ssi0_dt_a_19, 2),
	INGENIC_PIN_GROUP("ssi0-dt-a-21", jz4780_ssi0_dt_a_21, 2),
	INGENIC_PIN_GROUP("ssi0-dt-a-28", jz4780_ssi0_dt_a_28, 2),
	INGENIC_PIN_GROUP("ssi0-dt-b", jz4780_ssi0_dt_b, 1),
	INGENIC_PIN_GROUP("ssi0-dt-d", jz4780_ssi0_dt_d, 1),
	INGENIC_PIN_GROUP("ssi0-dt-e", jz4770_ssi0_dt_e, 0),
	INGENIC_PIN_GROUP("ssi0-dr-a-20", jz4780_ssi0_dr_a_20, 2),
	INGENIC_PIN_GROUP("ssi0-dr-a-27", jz4780_ssi0_dr_a_27, 2),
	INGENIC_PIN_GROUP("ssi0-dr-b", jz4780_ssi0_dr_b, 1),
	INGENIC_PIN_GROUP("ssi0-dr-d", jz4780_ssi0_dr_d, 1),
	INGENIC_PIN_GROUP("ssi0-dr-e", jz4770_ssi0_dr_e, 0),
	INGENIC_PIN_GROUP("ssi0-clk-a", jz4780_ssi0_clk_a, 2),
	INGENIC_PIN_GROUP("ssi0-clk-b-5", jz4780_ssi0_clk_b_5, 1),
	INGENIC_PIN_GROUP("ssi0-clk-b-28", jz4780_ssi0_clk_b_28, 1),
	INGENIC_PIN_GROUP("ssi0-clk-d", jz4780_ssi0_clk_d, 1),
	INGENIC_PIN_GROUP("ssi0-clk-e", jz4770_ssi0_clk_e, 0),
	INGENIC_PIN_GROUP("ssi0-gpc-b", jz4780_ssi0_gpc_b, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-d", jz4780_ssi0_gpc_d, 1),
	INGENIC_PIN_GROUP("ssi0-gpc-e", jz4770_ssi0_gpc_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce0-a-23", jz4780_ssi0_ce0_a_23, 2),
	INGENIC_PIN_GROUP("ssi0-ce0-a-25", jz4780_ssi0_ce0_a_25, 2),
	INGENIC_PIN_GROUP("ssi0-ce0-b", jz4780_ssi0_ce0_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-d", jz4780_ssi0_ce0_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce0-e", jz4770_ssi0_ce0_e, 0),
	INGENIC_PIN_GROUP("ssi0-ce1-b", jz4780_ssi0_ce1_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-d", jz4780_ssi0_ce1_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce1-e", jz4770_ssi0_ce1_e, 0),
	INGENIC_PIN_GROUP("ssi1-dt-b", jz4780_ssi1_dt_b, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d", jz4780_ssi1_dt_d, 2),
	INGENIC_PIN_GROUP("ssi1-dt-e", jz4770_ssi1_dt_e, 1),
	INGENIC_PIN_GROUP("ssi1-dr-b", jz4780_ssi1_dr_b, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d", jz4780_ssi1_dr_d, 2),
	INGENIC_PIN_GROUP("ssi1-dr-e", jz4770_ssi1_dr_e, 1),
	INGENIC_PIN_GROUP("ssi1-clk-b", jz4780_ssi1_clk_b, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", jz4780_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e", jz4770_ssi1_clk_e, 1),
	INGENIC_PIN_GROUP("ssi1-gpc-b", jz4780_ssi1_gpc_b, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-d", jz4780_ssi1_gpc_d, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-e", jz4770_ssi1_gpc_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce0-b", jz4780_ssi1_ce0_b, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-d", jz4780_ssi1_ce0_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-e", jz4770_ssi1_ce0_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce1-b", jz4780_ssi1_ce1_b, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-d", jz4780_ssi1_ce1_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-e", jz4770_ssi1_ce1_e, 1),
	INGENIC_PIN_GROUP_FUNCS("mmc0-1bit-a", jz4770_mmc0_1bit_a,
				jz4760_mmc0_1bit_a_funcs),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4770_mmc0_4bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-8bit-a", jz4780_mmc0_8bit_a, 1),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4770_mmc0_1bit_e, 0),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4770_mmc0_4bit_e, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4770_mmc1_1bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4770_mmc1_4bit_d, 0),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4770_mmc1_1bit_e, 1),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4770_mmc1_4bit_e, 1),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4770_mmc2_1bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4770_mmc2_4bit_b, 0),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4770_mmc2_1bit_e, 2),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4770_mmc2_4bit_e, 2),
	INGENIC_PIN_GROUP("nemc-data", jz4770_nemc_8bit_data, 0),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4770_nemc_cle_ale, 0),
	INGENIC_PIN_GROUP("nemc-addr", jz4770_nemc_addr, 0),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4770_nemc_rd_we, 0),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4770_nemc_frd_fwe, 0),
	INGENIC_PIN_GROUP("nemc-wait", jz4770_nemc_wait, 0),
	INGENIC_PIN_GROUP("nemc-cs1", jz4770_nemc_cs1, 0),
	INGENIC_PIN_GROUP("nemc-cs2", jz4770_nemc_cs2, 0),
	INGENIC_PIN_GROUP("nemc-cs3", jz4770_nemc_cs3, 0),
	INGENIC_PIN_GROUP("nemc-cs4", jz4770_nemc_cs4, 0),
	INGENIC_PIN_GROUP("nemc-cs5", jz4770_nemc_cs5, 0),
	INGENIC_PIN_GROUP("nemc-cs6", jz4770_nemc_cs6, 0),
	INGENIC_PIN_GROUP("i2c0-data", jz4770_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data", jz4770_i2c1, 0),
	INGENIC_PIN_GROUP("i2c2-data", jz4770_i2c2, 2),
	INGENIC_PIN_GROUP("i2c3-data", jz4780_i2c3, 1),
	INGENIC_PIN_GROUP("i2c4-data-e", jz4780_i2c4_e, 1),
	INGENIC_PIN_GROUP("i2c4-data-f", jz4780_i2c4_f, 1),
	INGENIC_PIN_GROUP("i2s-data-tx", jz4780_i2s_data_tx, 0),
	INGENIC_PIN_GROUP("i2s-data-rx", jz4780_i2s_data_rx, 0),
	INGENIC_PIN_GROUP_FUNCS("i2s-clk-txrx", jz4780_i2s_clk_txrx,
				jz4780_i2s_clk_txrx_funcs),
	INGENIC_PIN_GROUP("i2s-clk-rx", jz4780_i2s_clk_rx, 1),
	INGENIC_PIN_GROUP("i2s-sysclk", jz4780_i2s_sysclk, 2),
	INGENIC_PIN_GROUP("dmic", jz4780_dmic, 1),
	INGENIC_PIN_GROUP("hdmi-ddc", jz4780_hdmi_ddc, 0),
	INGENIC_PIN_GROUP("cim-data", jz4770_cim_8bit, 0),
	INGENIC_PIN_GROUP("cim-data-12bit", jz4770_cim_12bit, 0),
	INGENIC_PIN_GROUP("lcd-8bit", jz4770_lcd_8bit, 0),
	INGENIC_PIN_GROUP("lcd-16bit", jz4770_lcd_16bit, 0),
	INGENIC_PIN_GROUP("lcd-18bit", jz4770_lcd_18bit, 0),
	INGENIC_PIN_GROUP("lcd-24bit", jz4770_lcd_24bit, 0),
	INGENIC_PIN_GROUP("lcd-special", jz4770_lcd_special, 1),
	INGENIC_PIN_GROUP("lcd-generic", jz4770_lcd_generic, 0),
	INGENIC_PIN_GROUP("pwm0", jz4770_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", jz4770_pwm_pwm1, 0),
	INGENIC_PIN_GROUP("pwm2", jz4770_pwm_pwm2, 0),
	INGENIC_PIN_GROUP("pwm3", jz4770_pwm_pwm3, 0),
	INGENIC_PIN_GROUP("pwm4", jz4770_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("pwm5", jz4770_pwm_pwm5, 0),
	INGENIC_PIN_GROUP("pwm6", jz4770_pwm_pwm6, 0),
	INGENIC_PIN_GROUP("pwm7", jz4770_pwm_pwm7, 0),
};

static const char *jz4780_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4780_uart4_groups[] = { "uart4-data", };
static const char *jz4780_ssi0_groups[] = {
	"ssi0-dt-a-19", "ssi0-dt-a-21", "ssi0-dt-a-28", "ssi0-dt-b", "ssi0-dt-d", "ssi0-dt-e",
	"ssi0-dr-a-20", "ssi0-dr-a-27", "ssi0-dr-b", "ssi0-dr-d", "ssi0-dr-e",
	"ssi0-clk-a", "ssi0-clk-b-5", "ssi0-clk-b-28", "ssi0-clk-d", "ssi0-clk-e",
	"ssi0-gpc-b", "ssi0-gpc-d", "ssi0-gpc-e",
	"ssi0-ce0-a-23", "ssi0-ce0-a-25", "ssi0-ce0-b", "ssi0-ce0-d", "ssi0-ce0-e",
	"ssi0-ce1-b", "ssi0-ce1-d", "ssi0-ce1-e",
};
static const char *jz4780_ssi1_groups[] = {
	"ssi1-dt-b", "ssi1-dt-d", "ssi1-dt-e",
	"ssi1-dr-b", "ssi1-dr-d", "ssi1-dr-e",
	"ssi1-clk-b", "ssi1-clk-d", "ssi1-clk-e",
	"ssi1-gpc-b", "ssi1-gpc-d", "ssi1-gpc-e",
	"ssi1-ce0-b", "ssi1-ce0-d", "ssi1-ce0-e",
	"ssi1-ce1-b", "ssi1-ce1-d", "ssi1-ce1-e",
};
static const char *jz4780_mmc0_groups[] = {
	"mmc0-1bit-a", "mmc0-4bit-a", "mmc0-8bit-a",
	"mmc0-1bit-e", "mmc0-4bit-e",
};
static const char *jz4780_mmc1_groups[] = {
	"mmc1-1bit-d", "mmc1-4bit-d", "mmc1-1bit-e", "mmc1-4bit-e",
};
static const char *jz4780_mmc2_groups[] = {
	"mmc2-1bit-b", "mmc2-4bit-b", "mmc2-1bit-e", "mmc2-4bit-e",
};
static const char *jz4780_nemc_groups[] = {
	"nemc-data", "nemc-cle-ale", "nemc-addr",
	"nemc-rd-we", "nemc-frd-fwe", "nemc-wait",
};
static const char *jz4780_i2c3_groups[] = { "i2c3-data", };
static const char *jz4780_i2c4_groups[] = { "i2c4-data-e", "i2c4-data-f", };
static const char *jz4780_i2s_groups[] = {
	"i2s-data-tx", "i2s-data-rx", "i2s-clk-txrx", "i2s-clk-rx", "i2s-sysclk",
};
static const char *jz4780_dmic_groups[] = { "dmic", };
static const char *jz4780_cim_groups[] = { "cim-data", };
static const char *jz4780_hdmi_ddc_groups[] = { "hdmi-ddc", };

static const struct function_desc jz4780_functions[] = {
	{ "uart0", jz4770_uart0_groups, ARRAY_SIZE(jz4770_uart0_groups), },
	{ "uart1", jz4770_uart1_groups, ARRAY_SIZE(jz4770_uart1_groups), },
	{ "uart2", jz4780_uart2_groups, ARRAY_SIZE(jz4780_uart2_groups), },
	{ "uart3", jz4770_uart3_groups, ARRAY_SIZE(jz4770_uart3_groups), },
	{ "uart4", jz4780_uart4_groups, ARRAY_SIZE(jz4780_uart4_groups), },
	{ "ssi0", jz4780_ssi0_groups, ARRAY_SIZE(jz4780_ssi0_groups), },
	{ "ssi1", jz4780_ssi1_groups, ARRAY_SIZE(jz4780_ssi1_groups), },
	{ "mmc0", jz4780_mmc0_groups, ARRAY_SIZE(jz4780_mmc0_groups), },
	{ "mmc1", jz4780_mmc1_groups, ARRAY_SIZE(jz4780_mmc1_groups), },
	{ "mmc2", jz4780_mmc2_groups, ARRAY_SIZE(jz4780_mmc2_groups), },
	{ "nemc", jz4780_nemc_groups, ARRAY_SIZE(jz4780_nemc_groups), },
	{ "nemc-cs1", jz4770_cs1_groups, ARRAY_SIZE(jz4770_cs1_groups), },
	{ "nemc-cs2", jz4770_cs2_groups, ARRAY_SIZE(jz4770_cs2_groups), },
	{ "nemc-cs3", jz4770_cs3_groups, ARRAY_SIZE(jz4770_cs3_groups), },
	{ "nemc-cs4", jz4770_cs4_groups, ARRAY_SIZE(jz4770_cs4_groups), },
	{ "nemc-cs5", jz4770_cs5_groups, ARRAY_SIZE(jz4770_cs5_groups), },
	{ "nemc-cs6", jz4770_cs6_groups, ARRAY_SIZE(jz4770_cs6_groups), },
	{ "i2c0", jz4770_i2c0_groups, ARRAY_SIZE(jz4770_i2c0_groups), },
	{ "i2c1", jz4770_i2c1_groups, ARRAY_SIZE(jz4770_i2c1_groups), },
	{ "i2c2", jz4770_i2c2_groups, ARRAY_SIZE(jz4770_i2c2_groups), },
	{ "i2c3", jz4780_i2c3_groups, ARRAY_SIZE(jz4780_i2c3_groups), },
	{ "i2c4", jz4780_i2c4_groups, ARRAY_SIZE(jz4780_i2c4_groups), },
	{ "i2s", jz4780_i2s_groups, ARRAY_SIZE(jz4780_i2s_groups), },
	{ "dmic", jz4780_dmic_groups, ARRAY_SIZE(jz4780_dmic_groups), },
	{ "cim", jz4780_cim_groups, ARRAY_SIZE(jz4780_cim_groups), },
	{ "lcd", jz4770_lcd_groups, ARRAY_SIZE(jz4770_lcd_groups), },
	{ "pwm0", jz4770_pwm0_groups, ARRAY_SIZE(jz4770_pwm0_groups), },
	{ "pwm1", jz4770_pwm1_groups, ARRAY_SIZE(jz4770_pwm1_groups), },
	{ "pwm2", jz4770_pwm2_groups, ARRAY_SIZE(jz4770_pwm2_groups), },
	{ "pwm3", jz4770_pwm3_groups, ARRAY_SIZE(jz4770_pwm3_groups), },
	{ "pwm4", jz4770_pwm4_groups, ARRAY_SIZE(jz4770_pwm4_groups), },
	{ "pwm5", jz4770_pwm5_groups, ARRAY_SIZE(jz4770_pwm5_groups), },
	{ "pwm6", jz4770_pwm6_groups, ARRAY_SIZE(jz4770_pwm6_groups), },
	{ "pwm7", jz4770_pwm7_groups, ARRAY_SIZE(jz4770_pwm7_groups), },
	{ "hdmi-ddc", jz4780_hdmi_ddc_groups,
		      ARRAY_SIZE(jz4780_hdmi_ddc_groups), },
};

static const struct ingenic_chip_info jz4780_chip_info = {
	.num_chips = 6,
	.reg_offset = 0x100,
	.version = ID_JZ4780,
	.groups = jz4780_groups,
	.num_groups = ARRAY_SIZE(jz4780_groups),
	.functions = jz4780_functions,
	.num_functions = ARRAY_SIZE(jz4780_functions),
	.pull_ups = jz4780_pull_ups,
	.pull_downs = jz4780_pull_downs,
};

static const u32 x1000_pull_ups[4] = {
	0xffffffff, 0xfdffffff, 0x0dffffff, 0x0000003f,
};

static const u32 x1000_pull_downs[4] = {
	0x00000000, 0x02000000, 0x02000000, 0x00000000,
};

static int x1000_uart0_data_pins[] = { 0x4a, 0x4b, };
static int x1000_uart0_hwflow_pins[] = { 0x4c, 0x4d, };
static int x1000_uart1_data_a_pins[] = { 0x04, 0x05, };
static int x1000_uart1_data_d_pins[] = { 0x62, 0x63, };
static int x1000_uart1_hwflow_pins[] = { 0x64, 0x65, };
static int x1000_uart2_data_a_pins[] = { 0x02, 0x03, };
static int x1000_uart2_data_d_pins[] = { 0x65, 0x64, };
static int x1000_sfc_data_pins[] = { 0x1d, 0x1c, 0x1e, 0x1f, };
static int x1000_sfc_clk_pins[] = { 0x1a, };
static int x1000_sfc_ce_pins[] = { 0x1b, };
static int x1000_ssi_dt_a_22_pins[] = { 0x16, };
static int x1000_ssi_dt_a_29_pins[] = { 0x1d, };
static int x1000_ssi_dt_d_pins[] = { 0x62, };
static int x1000_ssi_dr_a_23_pins[] = { 0x17, };
static int x1000_ssi_dr_a_28_pins[] = { 0x1c, };
static int x1000_ssi_dr_d_pins[] = { 0x63, };
static int x1000_ssi_clk_a_24_pins[] = { 0x18, };
static int x1000_ssi_clk_a_26_pins[] = { 0x1a, };
static int x1000_ssi_clk_d_pins[] = { 0x60, };
static int x1000_ssi_gpc_a_20_pins[] = { 0x14, };
static int x1000_ssi_gpc_a_31_pins[] = { 0x1f, };
static int x1000_ssi_ce0_a_25_pins[] = { 0x19, };
static int x1000_ssi_ce0_a_27_pins[] = { 0x1b, };
static int x1000_ssi_ce0_d_pins[] = { 0x61, };
static int x1000_ssi_ce1_a_21_pins[] = { 0x15, };
static int x1000_ssi_ce1_a_30_pins[] = { 0x1e, };
static int x1000_mmc0_1bit_pins[] = { 0x18, 0x19, 0x17, };
static int x1000_mmc0_4bit_pins[] = { 0x16, 0x15, 0x14, };
static int x1000_mmc0_8bit_pins[] = { 0x13, 0x12, 0x11, 0x10, };
static int x1000_mmc1_1bit_pins[] = { 0x40, 0x41, 0x42, };
static int x1000_mmc1_4bit_pins[] = { 0x43, 0x44, 0x45, };
static int x1000_emc_8bit_data_pins[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
static int x1000_emc_16bit_data_pins[] = {
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static int x1000_emc_addr_pins[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};
static int x1000_emc_rd_we_pins[] = { 0x30, 0x31, };
static int x1000_emc_wait_pins[] = { 0x34, };
static int x1000_emc_cs1_pins[] = { 0x32, };
static int x1000_emc_cs2_pins[] = { 0x33, };
static int x1000_i2c0_pins[] = { 0x38, 0x37, };
static int x1000_i2c1_a_pins[] = { 0x01, 0x00, };
static int x1000_i2c1_c_pins[] = { 0x5b, 0x5a, };
static int x1000_i2c2_pins[] = { 0x61, 0x60, };
static int x1000_i2s_data_tx_pins[] = { 0x24, };
static int x1000_i2s_data_rx_pins[] = { 0x23, };
static int x1000_i2s_clk_txrx_pins[] = { 0x21, 0x22, };
static int x1000_i2s_sysclk_pins[] = { 0x20, };
static int x1000_dmic_if0_pins[] = { 0x35, 0x36, };
static int x1000_dmic_if1_pins[] = { 0x25, };
static int x1000_cim_pins[] = {
	0x08, 0x09, 0x0a, 0x0b,
	0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d, 0x0c,
};
static int x1000_lcd_8bit_pins[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x30, 0x31, 0x32, 0x33, 0x34,
};
static int x1000_lcd_16bit_pins[] = {
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static int x1000_pwm_pwm0_pins[] = { 0x59, };
static int x1000_pwm_pwm1_pins[] = { 0x5a, };
static int x1000_pwm_pwm2_pins[] = { 0x5b, };
static int x1000_pwm_pwm3_pins[] = { 0x26, };
static int x1000_pwm_pwm4_pins[] = { 0x58, };
static int x1000_mac_pins[] = {
	0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x26,
};

static const struct group_desc x1000_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1000_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", x1000_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data-a", x1000_uart1_data_a, 2),
	INGENIC_PIN_GROUP("uart1-data-d", x1000_uart1_data_d, 1),
	INGENIC_PIN_GROUP("uart1-hwflow", x1000_uart1_hwflow, 1),
	INGENIC_PIN_GROUP("uart2-data-a", x1000_uart2_data_a, 2),
	INGENIC_PIN_GROUP("uart2-data-d", x1000_uart2_data_d, 0),
	INGENIC_PIN_GROUP("sfc-data", x1000_sfc_data, 1),
	INGENIC_PIN_GROUP("sfc-clk", x1000_sfc_clk, 1),
	INGENIC_PIN_GROUP("sfc-ce", x1000_sfc_ce, 1),
	INGENIC_PIN_GROUP("ssi-dt-a-22", x1000_ssi_dt_a_22, 2),
	INGENIC_PIN_GROUP("ssi-dt-a-29", x1000_ssi_dt_a_29, 2),
	INGENIC_PIN_GROUP("ssi-dt-d", x1000_ssi_dt_d, 0),
	INGENIC_PIN_GROUP("ssi-dr-a-23", x1000_ssi_dr_a_23, 2),
	INGENIC_PIN_GROUP("ssi-dr-a-28", x1000_ssi_dr_a_28, 2),
	INGENIC_PIN_GROUP("ssi-dr-d", x1000_ssi_dr_d, 0),
	INGENIC_PIN_GROUP("ssi-clk-a-24", x1000_ssi_clk_a_24, 2),
	INGENIC_PIN_GROUP("ssi-clk-a-26", x1000_ssi_clk_a_26, 2),
	INGENIC_PIN_GROUP("ssi-clk-d", x1000_ssi_clk_d, 0),
	INGENIC_PIN_GROUP("ssi-gpc-a-20", x1000_ssi_gpc_a_20, 2),
	INGENIC_PIN_GROUP("ssi-gpc-a-31", x1000_ssi_gpc_a_31, 2),
	INGENIC_PIN_GROUP("ssi-ce0-a-25", x1000_ssi_ce0_a_25, 2),
	INGENIC_PIN_GROUP("ssi-ce0-a-27", x1000_ssi_ce0_a_27, 2),
	INGENIC_PIN_GROUP("ssi-ce0-d", x1000_ssi_ce0_d, 0),
	INGENIC_PIN_GROUP("ssi-ce1-a-21", x1000_ssi_ce1_a_21, 2),
	INGENIC_PIN_GROUP("ssi-ce1-a-30", x1000_ssi_ce1_a_30, 2),
	INGENIC_PIN_GROUP("mmc0-1bit", x1000_mmc0_1bit, 1),
	INGENIC_PIN_GROUP("mmc0-4bit", x1000_mmc0_4bit, 1),
	INGENIC_PIN_GROUP("mmc0-8bit", x1000_mmc0_8bit, 1),
	INGENIC_PIN_GROUP("mmc1-1bit", x1000_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", x1000_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("emc-8bit-data", x1000_emc_8bit_data, 0),
	INGENIC_PIN_GROUP("emc-16bit-data", x1000_emc_16bit_data, 0),
	INGENIC_PIN_GROUP("emc-addr", x1000_emc_addr, 0),
	INGENIC_PIN_GROUP("emc-rd-we", x1000_emc_rd_we, 0),
	INGENIC_PIN_GROUP("emc-wait", x1000_emc_wait, 0),
	INGENIC_PIN_GROUP("emc-cs1", x1000_emc_cs1, 0),
	INGENIC_PIN_GROUP("emc-cs2", x1000_emc_cs2, 0),
	INGENIC_PIN_GROUP("i2c0-data", x1000_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data-a", x1000_i2c1_a, 2),
	INGENIC_PIN_GROUP("i2c1-data-c", x1000_i2c1_c, 0),
	INGENIC_PIN_GROUP("i2c2-data", x1000_i2c2, 1),
	INGENIC_PIN_GROUP("i2s-data-tx", x1000_i2s_data_tx, 1),
	INGENIC_PIN_GROUP("i2s-data-rx", x1000_i2s_data_rx, 1),
	INGENIC_PIN_GROUP("i2s-clk-txrx", x1000_i2s_clk_txrx, 1),
	INGENIC_PIN_GROUP("i2s-sysclk", x1000_i2s_sysclk, 1),
	INGENIC_PIN_GROUP("dmic-if0", x1000_dmic_if0, 0),
	INGENIC_PIN_GROUP("dmic-if1", x1000_dmic_if1, 1),
	INGENIC_PIN_GROUP("cim-data", x1000_cim, 2),
	INGENIC_PIN_GROUP("lcd-8bit", x1000_lcd_8bit, 1),
	INGENIC_PIN_GROUP("lcd-16bit", x1000_lcd_16bit, 1),
	INGENIC_PIN_GROUP("pwm0", x1000_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", x1000_pwm_pwm1, 1),
	INGENIC_PIN_GROUP("pwm2", x1000_pwm_pwm2, 1),
	INGENIC_PIN_GROUP("pwm3", x1000_pwm_pwm3, 2),
	INGENIC_PIN_GROUP("pwm4", x1000_pwm_pwm4, 0),
	INGENIC_PIN_GROUP("mac", x1000_mac, 1),
};

static const char *x1000_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x1000_uart1_groups[] = {
	"uart1-data-a", "uart1-data-d", "uart1-hwflow",
};
static const char *x1000_uart2_groups[] = { "uart2-data-a", "uart2-data-d", };
static const char *x1000_sfc_groups[] = { "sfc-data", "sfc-clk", "sfc-ce", };
static const char *x1000_ssi_groups[] = {
	"ssi-dt-a-22", "ssi-dt-a-29", "ssi-dt-d",
	"ssi-dr-a-23", "ssi-dr-a-28", "ssi-dr-d",
	"ssi-clk-a-24", "ssi-clk-a-26", "ssi-clk-d",
	"ssi-gpc-a-20", "ssi-gpc-a-31",
	"ssi-ce0-a-25", "ssi-ce0-a-27", "ssi-ce0-d",
	"ssi-ce1-a-21", "ssi-ce1-a-30",
};
static const char *x1000_mmc0_groups[] = {
	"mmc0-1bit", "mmc0-4bit", "mmc0-8bit",
};
static const char *x1000_mmc1_groups[] = {
	"mmc1-1bit", "mmc1-4bit",
};
static const char *x1000_emc_groups[] = {
	"emc-8bit-data", "emc-16bit-data",
	"emc-addr", "emc-rd-we", "emc-wait",
};
static const char *x1000_cs1_groups[] = { "emc-cs1", };
static const char *x1000_cs2_groups[] = { "emc-cs2", };
static const char *x1000_i2c0_groups[] = { "i2c0-data", };
static const char *x1000_i2c1_groups[] = { "i2c1-data-a", "i2c1-data-c", };
static const char *x1000_i2c2_groups[] = { "i2c2-data", };
static const char *x1000_i2s_groups[] = {
	"i2s-data-tx", "i2s-data-rx", "i2s-clk-txrx", "i2s-sysclk",
};
static const char *x1000_dmic_groups[] = { "dmic-if0", "dmic-if1", };
static const char *x1000_cim_groups[] = { "cim-data", };
static const char *x1000_lcd_groups[] = { "lcd-8bit", "lcd-16bit", };
static const char *x1000_pwm0_groups[] = { "pwm0", };
static const char *x1000_pwm1_groups[] = { "pwm1", };
static const char *x1000_pwm2_groups[] = { "pwm2", };
static const char *x1000_pwm3_groups[] = { "pwm3", };
static const char *x1000_pwm4_groups[] = { "pwm4", };
static const char *x1000_mac_groups[] = { "mac", };

static const struct function_desc x1000_functions[] = {
	{ "uart0", x1000_uart0_groups, ARRAY_SIZE(x1000_uart0_groups), },
	{ "uart1", x1000_uart1_groups, ARRAY_SIZE(x1000_uart1_groups), },
	{ "uart2", x1000_uart2_groups, ARRAY_SIZE(x1000_uart2_groups), },
	{ "sfc", x1000_sfc_groups, ARRAY_SIZE(x1000_sfc_groups), },
	{ "ssi", x1000_ssi_groups, ARRAY_SIZE(x1000_ssi_groups), },
	{ "mmc0", x1000_mmc0_groups, ARRAY_SIZE(x1000_mmc0_groups), },
	{ "mmc1", x1000_mmc1_groups, ARRAY_SIZE(x1000_mmc1_groups), },
	{ "emc", x1000_emc_groups, ARRAY_SIZE(x1000_emc_groups), },
	{ "emc-cs1", x1000_cs1_groups, ARRAY_SIZE(x1000_cs1_groups), },
	{ "emc-cs2", x1000_cs2_groups, ARRAY_SIZE(x1000_cs2_groups), },
	{ "i2c0", x1000_i2c0_groups, ARRAY_SIZE(x1000_i2c0_groups), },
	{ "i2c1", x1000_i2c1_groups, ARRAY_SIZE(x1000_i2c1_groups), },
	{ "i2c2", x1000_i2c2_groups, ARRAY_SIZE(x1000_i2c2_groups), },
	{ "i2s", x1000_i2s_groups, ARRAY_SIZE(x1000_i2s_groups), },
	{ "dmic", x1000_dmic_groups, ARRAY_SIZE(x1000_dmic_groups), },
	{ "cim", x1000_cim_groups, ARRAY_SIZE(x1000_cim_groups), },
	{ "lcd", x1000_lcd_groups, ARRAY_SIZE(x1000_lcd_groups), },
	{ "pwm0", x1000_pwm0_groups, ARRAY_SIZE(x1000_pwm0_groups), },
	{ "pwm1", x1000_pwm1_groups, ARRAY_SIZE(x1000_pwm1_groups), },
	{ "pwm2", x1000_pwm2_groups, ARRAY_SIZE(x1000_pwm2_groups), },
	{ "pwm3", x1000_pwm3_groups, ARRAY_SIZE(x1000_pwm3_groups), },
	{ "pwm4", x1000_pwm4_groups, ARRAY_SIZE(x1000_pwm4_groups), },
	{ "mac", x1000_mac_groups, ARRAY_SIZE(x1000_mac_groups), },
};

static const struct regmap_range x1000_access_ranges[] = {
	regmap_reg_range(0x000, 0x400 - 4),
	regmap_reg_range(0x700, 0x800 - 4),
};

/* shared with X1500 */
static const struct regmap_access_table x1000_access_table = {
	.yes_ranges = x1000_access_ranges,
	.n_yes_ranges = ARRAY_SIZE(x1000_access_ranges),
};

static const struct ingenic_chip_info x1000_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x100,
	.version = ID_X1000,
	.groups = x1000_groups,
	.num_groups = ARRAY_SIZE(x1000_groups),
	.functions = x1000_functions,
	.num_functions = ARRAY_SIZE(x1000_functions),
	.pull_ups = x1000_pull_ups,
	.pull_downs = x1000_pull_downs,
	.access_table = &x1000_access_table,
};

static int x1500_uart0_data_pins[] = { 0x4a, 0x4b, };
static int x1500_uart0_hwflow_pins[] = { 0x4c, 0x4d, };
static int x1500_uart1_data_a_pins[] = { 0x04, 0x05, };
static int x1500_uart1_data_d_pins[] = { 0x62, 0x63, };
static int x1500_uart1_hwflow_pins[] = { 0x64, 0x65, };
static int x1500_uart2_data_a_pins[] = { 0x02, 0x03, };
static int x1500_uart2_data_d_pins[] = { 0x65, 0x64, };
static int x1500_mmc_1bit_pins[] = { 0x18, 0x19, 0x17, };
static int x1500_mmc_4bit_pins[] = { 0x16, 0x15, 0x14, };
static int x1500_i2c0_pins[] = { 0x38, 0x37, };
static int x1500_i2c1_a_pins[] = { 0x01, 0x00, };
static int x1500_i2c1_c_pins[] = { 0x5b, 0x5a, };
static int x1500_i2c2_pins[] = { 0x61, 0x60, };
static int x1500_i2s_data_tx_pins[] = { 0x24, };
static int x1500_i2s_data_rx_pins[] = { 0x23, };
static int x1500_i2s_clk_txrx_pins[] = { 0x21, 0x22, };
static int x1500_i2s_sysclk_pins[] = { 0x20, };
static int x1500_dmic_if0_pins[] = { 0x35, 0x36, };
static int x1500_dmic_if1_pins[] = { 0x25, };
static int x1500_cim_pins[] = {
	0x08, 0x09, 0x0a, 0x0b,
	0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d, 0x0c,
};
static int x1500_pwm_pwm0_pins[] = { 0x59, };
static int x1500_pwm_pwm1_pins[] = { 0x5a, };
static int x1500_pwm_pwm2_pins[] = { 0x5b, };
static int x1500_pwm_pwm3_pins[] = { 0x26, };
static int x1500_pwm_pwm4_pins[] = { 0x58, };

static const struct group_desc x1500_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1500_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", x1500_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data-a", x1500_uart1_data_a, 2),
	INGENIC_PIN_GROUP("uart1-data-d", x1500_uart1_data_d, 1),
	INGENIC_PIN_GROUP("uart1-hwflow", x1500_uart1_hwflow, 1),
	INGENIC_PIN_GROUP("uart2-data-a", x1500_uart2_data_a, 2),
	INGENIC_PIN_GROUP("uart2-data-d", x1500_uart2_data_d, 0),
	INGENIC_PIN_GROUP("sfc-data", x1000_sfc_data, 1),
	INGENIC_PIN_GROUP("sfc-clk", x1000_sfc_clk, 1),
	INGENIC_PIN_GROUP("sfc-ce", x1000_sfc_ce, 1),
	INGENIC_PIN_GROUP("mmc-1bit", x1500_mmc_1bit, 1),
	INGENIC_PIN_GROUP("mmc-4bit", x1500_mmc_4bit, 1),
	INGENIC_PIN_GROUP("i2c0-data", x1500_i2c0, 0),
	INGENIC_PIN_GROUP("i2c1-data-a", x1500_i2c1_a, 2),
	INGENIC_PIN_GROUP("i2c1-data-c", x1500_i2c1_c, 0),
	INGENIC_PIN_GROUP("i2c2-data", x1500_i2c2, 1),
	INGENIC_PIN_GROUP("i2s-data-tx", x1500_i2s_data_tx, 1),
	INGENIC_PIN_GROUP("i2s-data-rx", x1500_i2s_data_rx, 1),
	INGENIC_PIN_GROUP("i2s-clk-txrx", x1500_i2s_clk_txrx, 1),
	INGENIC_PIN_GROUP("i2s-sysclk", x1500_i2s_sysclk, 1),
	INGENIC_PIN_GROUP("dmic-if0", x1500_dmic_if0, 0),
	INGENIC_PIN_GROUP("dmic-if1", x1500_dmic_if1, 1),
	INGENIC_PIN_GROUP("cim-data", x1500_cim, 2),
	INGENIC_PIN_GROUP("pwm0", x1500_pwm_pwm0, 0),
	INGENIC_PIN_GROUP("pwm1", x1500_pwm_pwm1, 1),
	INGENIC_PIN_GROUP("pwm2", x1500_pwm_pwm2, 1),
	INGENIC_PIN_GROUP("pwm3", x1500_pwm_pwm3, 2),
	INGENIC_PIN_GROUP("pwm4", x1500_pwm_pwm4, 0),
};

static const char *x1500_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x1500_uart1_groups[] = {
	"uart1-data-a", "uart1-data-d", "uart1-hwflow",
};
static const char *x1500_uart2_groups[] = { "uart2-data-a", "uart2-data-d", };
static const char *x1500_mmc_groups[] = { "mmc-1bit", "mmc-4bit", };
static const char *x1500_i2c0_groups[] = { "i2c0-data", };
static const char *x1500_i2c1_groups[] = { "i2c1-data-a", "i2c1-data-c", };
static const char *x1500_i2c2_groups[] = { "i2c2-data", };
static const char *x1500_i2s_groups[] = {
	"i2s-data-tx", "i2s-data-rx", "i2s-clk-txrx", "i2s-sysclk",
};
static const char *x1500_dmic_groups[] = { "dmic-if0", "dmic-if1", };
static const char *x1500_cim_groups[] = { "cim-data", };
static const char *x1500_pwm0_groups[] = { "pwm0", };
static const char *x1500_pwm1_groups[] = { "pwm1", };
static const char *x1500_pwm2_groups[] = { "pwm2", };
static const char *x1500_pwm3_groups[] = { "pwm3", };
static const char *x1500_pwm4_groups[] = { "pwm4", };

static const struct function_desc x1500_functions[] = {
	{ "uart0", x1500_uart0_groups, ARRAY_SIZE(x1500_uart0_groups), },
	{ "uart1", x1500_uart1_groups, ARRAY_SIZE(x1500_uart1_groups), },
	{ "uart2", x1500_uart2_groups, ARRAY_SIZE(x1500_uart2_groups), },
	{ "sfc", x1000_sfc_groups, ARRAY_SIZE(x1000_sfc_groups), },
	{ "mmc", x1500_mmc_groups, ARRAY_SIZE(x1500_mmc_groups), },
	{ "i2c0", x1500_i2c0_groups, ARRAY_SIZE(x1500_i2c0_groups), },
	{ "i2c1", x1500_i2c1_groups, ARRAY_SIZE(x1500_i2c1_groups), },
	{ "i2c2", x1500_i2c2_groups, ARRAY_SIZE(x1500_i2c2_groups), },
	{ "i2s", x1500_i2s_groups, ARRAY_SIZE(x1500_i2s_groups), },
	{ "dmic", x1500_dmic_groups, ARRAY_SIZE(x1500_dmic_groups), },
	{ "cim", x1500_cim_groups, ARRAY_SIZE(x1500_cim_groups), },
	{ "pwm0", x1500_pwm0_groups, ARRAY_SIZE(x1500_pwm0_groups), },
	{ "pwm1", x1500_pwm1_groups, ARRAY_SIZE(x1500_pwm1_groups), },
	{ "pwm2", x1500_pwm2_groups, ARRAY_SIZE(x1500_pwm2_groups), },
	{ "pwm3", x1500_pwm3_groups, ARRAY_SIZE(x1500_pwm3_groups), },
	{ "pwm4", x1500_pwm4_groups, ARRAY_SIZE(x1500_pwm4_groups), },
};

static const struct ingenic_chip_info x1500_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x100,
	.version = ID_X1500,
	.groups = x1500_groups,
	.num_groups = ARRAY_SIZE(x1500_groups),
	.functions = x1500_functions,
	.num_functions = ARRAY_SIZE(x1500_functions),
	.pull_ups = x1000_pull_ups,
	.pull_downs = x1000_pull_downs,
	.access_table = &x1000_access_table,
};

static const u32 x1830_pull_ups[4] = {
	0x5fdfffc0, 0xffffefff, 0x1ffffbff, 0x0fcff3fc,
};

static const u32 x1830_pull_downs[4] = {
	0x5fdfffc0, 0xffffefff, 0x1ffffbff, 0x0fcff3fc,
};

static int x1830_uart0_data_pins[] = { 0x33, 0x36, };
static int x1830_uart0_hwflow_pins[] = { 0x34, 0x35, };
static int x1830_uart1_data_pins[] = { 0x38, 0x37, };
static int x1830_sfc_data_pins[] = { 0x17, 0x18, 0x1a, 0x19, };
static int x1830_sfc_clk_pins[] = { 0x1b, };
static int x1830_sfc_ce_pins[] = { 0x1c, };
static int x1830_ssi0_dt_pins[] = { 0x4c, };
static int x1830_ssi0_dr_pins[] = { 0x4b, };
static int x1830_ssi0_clk_pins[] = { 0x4f, };
static int x1830_ssi0_gpc_pins[] = { 0x4d, };
static int x1830_ssi0_ce0_pins[] = { 0x50, };
static int x1830_ssi0_ce1_pins[] = { 0x4e, };
static int x1830_ssi1_dt_c_pins[] = { 0x53, };
static int x1830_ssi1_dt_d_pins[] = { 0x62, };
static int x1830_ssi1_dr_c_pins[] = { 0x54, };
static int x1830_ssi1_dr_d_pins[] = { 0x63, };
static int x1830_ssi1_clk_c_pins[] = { 0x57, };
static int x1830_ssi1_clk_d_pins[] = { 0x66, };
static int x1830_ssi1_gpc_c_pins[] = { 0x55, };
static int x1830_ssi1_gpc_d_pins[] = { 0x64, };
static int x1830_ssi1_ce0_c_pins[] = { 0x58, };
static int x1830_ssi1_ce0_d_pins[] = { 0x67, };
static int x1830_ssi1_ce1_c_pins[] = { 0x56, };
static int x1830_ssi1_ce1_d_pins[] = { 0x65, };
static int x1830_mmc0_1bit_pins[] = { 0x24, 0x25, 0x20, };
static int x1830_mmc0_4bit_pins[] = { 0x21, 0x22, 0x23, };
static int x1830_mmc1_1bit_pins[] = { 0x42, 0x43, 0x44, };
static int x1830_mmc1_4bit_pins[] = { 0x45, 0x46, 0x47, };
static int x1830_i2c0_pins[] = { 0x0c, 0x0d, };
static int x1830_i2c1_pins[] = { 0x39, 0x3a, };
static int x1830_i2c2_pins[] = { 0x5b, 0x5c, };
static int x1830_i2s_data_tx_pins[] = { 0x53, };
static int x1830_i2s_data_rx_pins[] = { 0x54, };
static int x1830_i2s_clk_txrx_pins[] = { 0x58, 0x52, };
static int x1830_i2s_clk_rx_pins[] = { 0x56, 0x55, };
static int x1830_i2s_sysclk_pins[] = { 0x57, };
static int x1830_dmic_if0_pins[] = { 0x48, 0x59, };
static int x1830_dmic_if1_pins[] = { 0x5a, };
static int x1830_lcd_tft_8bit_pins[] = {
	0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x73, 0x72, 0x69,
};
static int x1830_lcd_tft_24bit_pins[] = {
	0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71,
	0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
};
static int x1830_lcd_slcd_8bit_pins[] = {
	0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x6c, 0x6d,
	0x69, 0x72, 0x73, 0x7b, 0x7a,
};
static int x1830_lcd_slcd_16bit_pins[] = {
	0x6e, 0x6f, 0x70, 0x71, 0x76, 0x77, 0x78, 0x79,
};
static int x1830_pwm_pwm0_b_pins[] = { 0x31, };
static int x1830_pwm_pwm0_c_pins[] = { 0x4b, };
static int x1830_pwm_pwm1_b_pins[] = { 0x32, };
static int x1830_pwm_pwm1_c_pins[] = { 0x4c, };
static int x1830_pwm_pwm2_c_8_pins[] = { 0x48, };
static int x1830_pwm_pwm2_c_13_pins[] = { 0x4d, };
static int x1830_pwm_pwm3_c_9_pins[] = { 0x49, };
static int x1830_pwm_pwm3_c_14_pins[] = { 0x4e, };
static int x1830_pwm_pwm4_c_15_pins[] = { 0x4f, };
static int x1830_pwm_pwm4_c_25_pins[] = { 0x59, };
static int x1830_pwm_pwm5_c_16_pins[] = { 0x50, };
static int x1830_pwm_pwm5_c_26_pins[] = { 0x5a, };
static int x1830_pwm_pwm6_c_17_pins[] = { 0x51, };
static int x1830_pwm_pwm6_c_27_pins[] = { 0x5b, };
static int x1830_pwm_pwm7_c_18_pins[] = { 0x52, };
static int x1830_pwm_pwm7_c_28_pins[] = { 0x5c, };
static int x1830_mac_pins[] = {
	0x29, 0x30, 0x2f, 0x28, 0x2e, 0x2d, 0x2a, 0x2b, 0x26, 0x27,
};

static const struct group_desc x1830_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1830_uart0_data, 0),
	INGENIC_PIN_GROUP("uart0-hwflow", x1830_uart0_hwflow, 0),
	INGENIC_PIN_GROUP("uart1-data", x1830_uart1_data, 0),
	INGENIC_PIN_GROUP("sfc-data", x1830_sfc_data, 1),
	INGENIC_PIN_GROUP("sfc-clk", x1830_sfc_clk, 1),
	INGENIC_PIN_GROUP("sfc-ce", x1830_sfc_ce, 1),
	INGENIC_PIN_GROUP("ssi0-dt", x1830_ssi0_dt, 0),
	INGENIC_PIN_GROUP("ssi0-dr", x1830_ssi0_dr, 0),
	INGENIC_PIN_GROUP("ssi0-clk", x1830_ssi0_clk, 0),
	INGENIC_PIN_GROUP("ssi0-gpc", x1830_ssi0_gpc, 0),
	INGENIC_PIN_GROUP("ssi0-ce0", x1830_ssi0_ce0, 0),
	INGENIC_PIN_GROUP("ssi0-ce1", x1830_ssi0_ce1, 0),
	INGENIC_PIN_GROUP("ssi1-dt-c", x1830_ssi1_dt_c, 1),
	INGENIC_PIN_GROUP("ssi1-dr-c", x1830_ssi1_dr_c, 1),
	INGENIC_PIN_GROUP("ssi1-clk-c", x1830_ssi1_clk_c, 1),
	INGENIC_PIN_GROUP("ssi1-gpc-c", x1830_ssi1_gpc_c, 1),
	INGENIC_PIN_GROUP("ssi1-ce0-c", x1830_ssi1_ce0_c, 1),
	INGENIC_PIN_GROUP("ssi1-ce1-c", x1830_ssi1_ce1_c, 1),
	INGENIC_PIN_GROUP("ssi1-dt-d", x1830_ssi1_dt_d, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d", x1830_ssi1_dr_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", x1830_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-gpc-d", x1830_ssi1_gpc_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce0-d", x1830_ssi1_ce0_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce1-d", x1830_ssi1_ce1_d, 2),
	INGENIC_PIN_GROUP("mmc0-1bit", x1830_mmc0_1bit, 0),
	INGENIC_PIN_GROUP("mmc0-4bit", x1830_mmc0_4bit, 0),
	INGENIC_PIN_GROUP("mmc1-1bit", x1830_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", x1830_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("i2c0-data", x1830_i2c0, 1),
	INGENIC_PIN_GROUP("i2c1-data", x1830_i2c1, 0),
	INGENIC_PIN_GROUP("i2c2-data", x1830_i2c2, 1),
	INGENIC_PIN_GROUP("i2s-data-tx", x1830_i2s_data_tx, 0),
	INGENIC_PIN_GROUP("i2s-data-rx", x1830_i2s_data_rx, 0),
	INGENIC_PIN_GROUP("i2s-clk-txrx", x1830_i2s_clk_txrx, 0),
	INGENIC_PIN_GROUP("i2s-clk-rx", x1830_i2s_clk_rx, 0),
	INGENIC_PIN_GROUP("i2s-sysclk", x1830_i2s_sysclk, 0),
	INGENIC_PIN_GROUP("dmic-if0", x1830_dmic_if0, 2),
	INGENIC_PIN_GROUP("dmic-if1", x1830_dmic_if1, 2),
	INGENIC_PIN_GROUP("lcd-tft-8bit", x1830_lcd_tft_8bit, 0),
	INGENIC_PIN_GROUP("lcd-tft-24bit", x1830_lcd_tft_24bit, 0),
	INGENIC_PIN_GROUP("lcd-slcd-8bit", x1830_lcd_slcd_8bit, 1),
	INGENIC_PIN_GROUP("lcd-slcd-16bit", x1830_lcd_slcd_16bit, 1),
	INGENIC_PIN_GROUP("pwm0-b", x1830_pwm_pwm0_b, 0),
	INGENIC_PIN_GROUP("pwm0-c", x1830_pwm_pwm0_c, 1),
	INGENIC_PIN_GROUP("pwm1-b", x1830_pwm_pwm1_b, 0),
	INGENIC_PIN_GROUP("pwm1-c", x1830_pwm_pwm1_c, 1),
	INGENIC_PIN_GROUP("pwm2-c-8", x1830_pwm_pwm2_c_8, 0),
	INGENIC_PIN_GROUP("pwm2-c-13", x1830_pwm_pwm2_c_13, 1),
	INGENIC_PIN_GROUP("pwm3-c-9", x1830_pwm_pwm3_c_9, 0),
	INGENIC_PIN_GROUP("pwm3-c-14", x1830_pwm_pwm3_c_14, 1),
	INGENIC_PIN_GROUP("pwm4-c-15", x1830_pwm_pwm4_c_15, 1),
	INGENIC_PIN_GROUP("pwm4-c-25", x1830_pwm_pwm4_c_25, 0),
	INGENIC_PIN_GROUP("pwm5-c-16", x1830_pwm_pwm5_c_16, 1),
	INGENIC_PIN_GROUP("pwm5-c-26", x1830_pwm_pwm5_c_26, 0),
	INGENIC_PIN_GROUP("pwm6-c-17", x1830_pwm_pwm6_c_17, 1),
	INGENIC_PIN_GROUP("pwm6-c-27", x1830_pwm_pwm6_c_27, 0),
	INGENIC_PIN_GROUP("pwm7-c-18", x1830_pwm_pwm7_c_18, 1),
	INGENIC_PIN_GROUP("pwm7-c-28", x1830_pwm_pwm7_c_28, 0),
	INGENIC_PIN_GROUP("mac", x1830_mac, 0),
};

static const char *x1830_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x1830_uart1_groups[] = { "uart1-data", };
static const char *x1830_sfc_groups[] = { "sfc-data", "sfc-clk", "sfc-ce", };
static const char *x1830_ssi0_groups[] = {
	"ssi0-dt", "ssi0-dr", "ssi0-clk", "ssi0-gpc", "ssi0-ce0", "ssi0-ce1",
};
static const char *x1830_ssi1_groups[] = {
	"ssi1-dt-c", "ssi1-dt-d",
	"ssi1-dr-c", "ssi1-dr-d",
	"ssi1-clk-c", "ssi1-clk-d",
	"ssi1-gpc-c", "ssi1-gpc-d",
	"ssi1-ce0-c", "ssi1-ce0-d",
	"ssi1-ce1-c", "ssi1-ce1-d",
};
static const char *x1830_mmc0_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *x1830_mmc1_groups[] = { "mmc1-1bit", "mmc1-4bit", };
static const char *x1830_i2c0_groups[] = { "i2c0-data", };
static const char *x1830_i2c1_groups[] = { "i2c1-data", };
static const char *x1830_i2c2_groups[] = { "i2c2-data", };
static const char *x1830_i2s_groups[] = {
	"i2s-data-tx", "i2s-data-rx", "i2s-clk-txrx", "i2s-clk-rx", "i2s-sysclk",
};
static const char *x1830_dmic_groups[] = { "dmic-if0", "dmic-if1", };
static const char *x1830_lcd_groups[] = {
	"lcd-tft-8bit", "lcd-tft-24bit", "lcd-slcd-8bit", "lcd-slcd-16bit",
};
static const char *x1830_pwm0_groups[] = { "pwm0-b", "pwm0-c", };
static const char *x1830_pwm1_groups[] = { "pwm1-b", "pwm1-c", };
static const char *x1830_pwm2_groups[] = { "pwm2-c-8", "pwm2-c-13", };
static const char *x1830_pwm3_groups[] = { "pwm3-c-9", "pwm3-c-14", };
static const char *x1830_pwm4_groups[] = { "pwm4-c-15", "pwm4-c-25", };
static const char *x1830_pwm5_groups[] = { "pwm5-c-16", "pwm5-c-26", };
static const char *x1830_pwm6_groups[] = { "pwm6-c-17", "pwm6-c-27", };
static const char *x1830_pwm7_groups[] = { "pwm7-c-18", "pwm7-c-28", };
static const char *x1830_mac_groups[] = { "mac", };

static const struct function_desc x1830_functions[] = {
	{ "uart0", x1830_uart0_groups, ARRAY_SIZE(x1830_uart0_groups), },
	{ "uart1", x1830_uart1_groups, ARRAY_SIZE(x1830_uart1_groups), },
	{ "sfc", x1830_sfc_groups, ARRAY_SIZE(x1830_sfc_groups), },
	{ "ssi0", x1830_ssi0_groups, ARRAY_SIZE(x1830_ssi0_groups), },
	{ "ssi1", x1830_ssi1_groups, ARRAY_SIZE(x1830_ssi1_groups), },
	{ "mmc0", x1830_mmc0_groups, ARRAY_SIZE(x1830_mmc0_groups), },
	{ "mmc1", x1830_mmc1_groups, ARRAY_SIZE(x1830_mmc1_groups), },
	{ "i2c0", x1830_i2c0_groups, ARRAY_SIZE(x1830_i2c0_groups), },
	{ "i2c1", x1830_i2c1_groups, ARRAY_SIZE(x1830_i2c1_groups), },
	{ "i2c2", x1830_i2c2_groups, ARRAY_SIZE(x1830_i2c2_groups), },
	{ "i2s", x1830_i2s_groups, ARRAY_SIZE(x1830_i2s_groups), },
	{ "dmic", x1830_dmic_groups, ARRAY_SIZE(x1830_dmic_groups), },
	{ "lcd", x1830_lcd_groups, ARRAY_SIZE(x1830_lcd_groups), },
	{ "pwm0", x1830_pwm0_groups, ARRAY_SIZE(x1830_pwm0_groups), },
	{ "pwm1", x1830_pwm1_groups, ARRAY_SIZE(x1830_pwm1_groups), },
	{ "pwm2", x1830_pwm2_groups, ARRAY_SIZE(x1830_pwm2_groups), },
	{ "pwm3", x1830_pwm3_groups, ARRAY_SIZE(x1830_pwm3_groups), },
	{ "pwm4", x1830_pwm4_groups, ARRAY_SIZE(x1830_pwm4_groups), },
	{ "pwm5", x1830_pwm5_groups, ARRAY_SIZE(x1830_pwm4_groups), },
	{ "pwm6", x1830_pwm6_groups, ARRAY_SIZE(x1830_pwm4_groups), },
	{ "pwm7", x1830_pwm7_groups, ARRAY_SIZE(x1830_pwm4_groups), },
	{ "mac", x1830_mac_groups, ARRAY_SIZE(x1830_mac_groups), },
};

static const struct regmap_range x1830_access_ranges[] = {
	regmap_reg_range(0x0000, 0x4000 - 4),
	regmap_reg_range(0x7000, 0x8000 - 4),
};

static const struct regmap_access_table x1830_access_table = {
	.yes_ranges = x1830_access_ranges,
	.n_yes_ranges = ARRAY_SIZE(x1830_access_ranges),
};

static const struct ingenic_chip_info x1830_chip_info = {
	.num_chips = 4,
	.reg_offset = 0x1000,
	.version = ID_X1830,
	.groups = x1830_groups,
	.num_groups = ARRAY_SIZE(x1830_groups),
	.functions = x1830_functions,
	.num_functions = ARRAY_SIZE(x1830_functions),
	.pull_ups = x1830_pull_ups,
	.pull_downs = x1830_pull_downs,
	.access_table = &x1830_access_table,
};

static const u32 x2000_pull_ups[5] = {
	0x0003ffff, 0xffffffff, 0x1ff0ffff, 0xc7fe3f3f, 0x8fff003f,
};

static const u32 x2000_pull_downs[5] = {
	0x0003ffff, 0xffffffff, 0x1ff0ffff, 0x00000000, 0x8fff003f,
};

static int x2000_uart0_data_pins[] = { 0x77, 0x78, };
static int x2000_uart0_hwflow_pins[] = { 0x79, 0x7a, };
static int x2000_uart1_data_pins[] = { 0x57, 0x58, };
static int x2000_uart1_hwflow_pins[] = { 0x55, 0x56, };
static int x2000_uart2_data_pins[] = { 0x7e, 0x7f, };
static int x2000_uart3_data_c_pins[] = { 0x59, 0x5a, };
static int x2000_uart3_data_d_pins[] = { 0x62, 0x63, };
static int x2000_uart3_hwflow_c_pins[] = { 0x5b, 0x5c, };
static int x2000_uart3_hwflow_d_pins[] = { 0x60, 0x61, };
static int x2000_uart4_data_a_pins[] = { 0x02, 0x03, };
static int x2000_uart4_data_c_pins[] = { 0x4b, 0x4c, };
static int x2000_uart4_hwflow_a_pins[] = { 0x00, 0x01, };
static int x2000_uart4_hwflow_c_pins[] = { 0x49, 0x4a, };
static int x2000_uart5_data_a_pins[] = { 0x04, 0x05, };
static int x2000_uart5_data_c_pins[] = { 0x45, 0x46, };
static int x2000_uart6_data_a_pins[] = { 0x06, 0x07, };
static int x2000_uart6_data_c_pins[] = { 0x47, 0x48, };
static int x2000_uart7_data_a_pins[] = { 0x08, 0x09, };
static int x2000_uart7_data_c_pins[] = { 0x41, 0x42, };
static int x2000_uart8_data_pins[] = { 0x3c, 0x3d, };
static int x2000_uart9_data_pins[] = { 0x3e, 0x3f, };
static int x2000_sfc_data_if0_d_pins[] = { 0x73, 0x74, 0x75, 0x76, };
static int x2000_sfc_data_if0_e_pins[] = { 0x92, 0x93, 0x94, 0x95, };
static int x2000_sfc_data_if1_pins[] = { 0x77, 0x78, 0x79, 0x7a, };
static int x2000_sfc_clk_d_pins[] = { 0x71, };
static int x2000_sfc_clk_e_pins[] = { 0x90, };
static int x2000_sfc_ce_d_pins[] = { 0x72, };
static int x2000_sfc_ce_e_pins[] = { 0x91, };
static int x2000_ssi0_dt_b_pins[] = { 0x3e, };
static int x2000_ssi0_dt_d_pins[] = { 0x69, };
static int x2000_ssi0_dr_b_pins[] = { 0x3d, };
static int x2000_ssi0_dr_d_pins[] = { 0x6a, };
static int x2000_ssi0_clk_b_pins[] = { 0x3f, };
static int x2000_ssi0_clk_d_pins[] = { 0x68, };
static int x2000_ssi0_ce_b_pins[] = { 0x3c, };
static int x2000_ssi0_ce_d_pins[] = { 0x6d, };
static int x2000_ssi1_dt_c_pins[] = { 0x4b, };
static int x2000_ssi1_dt_d_pins[] = { 0x72, };
static int x2000_ssi1_dt_e_pins[] = { 0x91, };
static int x2000_ssi1_dr_c_pins[] = { 0x4a, };
static int x2000_ssi1_dr_d_pins[] = { 0x73, };
static int x2000_ssi1_dr_e_pins[] = { 0x92, };
static int x2000_ssi1_clk_c_pins[] = { 0x4c, };
static int x2000_ssi1_clk_d_pins[] = { 0x71, };
static int x2000_ssi1_clk_e_pins[] = { 0x90, };
static int x2000_ssi1_ce_c_pins[] = { 0x49, };
static int x2000_ssi1_ce_d_pins[] = { 0x76, };
static int x2000_ssi1_ce_e_pins[] = { 0x95, };
static int x2000_mmc0_1bit_pins[] = { 0x71, 0x72, 0x73, };
static int x2000_mmc0_4bit_pins[] = { 0x74, 0x75, 0x75, };
static int x2000_mmc0_8bit_pins[] = { 0x77, 0x78, 0x79, 0x7a, };
static int x2000_mmc1_1bit_pins[] = { 0x68, 0x69, 0x6a, };
static int x2000_mmc1_4bit_pins[] = { 0x6b, 0x6c, 0x6d, };
static int x2000_mmc2_1bit_pins[] = { 0x80, 0x81, 0x82, };
static int x2000_mmc2_4bit_pins[] = { 0x83, 0x84, 0x85, };
static int x2000_emc_8bit_data_pins[] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
};
static int x2000_emc_16bit_data_pins[] = {
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
static int x2000_emc_addr_pins[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c,
};
static int x2000_emc_rd_we_pins[] = { 0x2d, 0x2e, };
static int x2000_emc_wait_pins[] = { 0x2f, };
static int x2000_emc_cs1_pins[] = { 0x57, };
static int x2000_emc_cs2_pins[] = { 0x58, };
static int x2000_i2c0_pins[] = { 0x4e, 0x4d, };
static int x2000_i2c1_c_pins[] = { 0x58, 0x57, };
static int x2000_i2c1_d_pins[] = { 0x6c, 0x6b, };
static int x2000_i2c2_b_pins[] = { 0x37, 0x36, };
static int x2000_i2c2_d_pins[] = { 0x75, 0x74, };
static int x2000_i2c2_e_pins[] = { 0x94, 0x93, };
static int x2000_i2c3_a_pins[] = { 0x11, 0x10, };
static int x2000_i2c3_d_pins[] = { 0x7f, 0x7e, };
static int x2000_i2c4_c_pins[] = { 0x5a, 0x59, };
static int x2000_i2c4_d_pins[] = { 0x61, 0x60, };
static int x2000_i2c5_c_pins[] = { 0x5c, 0x5b, };
static int x2000_i2c5_d_pins[] = { 0x65, 0x64, };
static int x2000_i2s1_data_tx_pins[] = { 0x47, };
static int x2000_i2s1_data_rx_pins[] = { 0x44, };
static int x2000_i2s1_clk_tx_pins[] = { 0x45, 0x46, };
static int x2000_i2s1_clk_rx_pins[] = { 0x42, 0x43, };
static int x2000_i2s1_sysclk_tx_pins[] = { 0x48, };
static int x2000_i2s1_sysclk_rx_pins[] = { 0x41, };
static int x2000_i2s2_data_rx0_pins[] = { 0x0a, };
static int x2000_i2s2_data_rx1_pins[] = { 0x0b, };
static int x2000_i2s2_data_rx2_pins[] = { 0x0c, };
static int x2000_i2s2_data_rx3_pins[] = { 0x0d, };
static int x2000_i2s2_clk_rx_pins[] = { 0x11, 0x09, };
static int x2000_i2s2_sysclk_rx_pins[] = { 0x07, };
static int x2000_i2s3_data_tx0_pins[] = { 0x03, };
static int x2000_i2s3_data_tx1_pins[] = { 0x04, };
static int x2000_i2s3_data_tx2_pins[] = { 0x05, };
static int x2000_i2s3_data_tx3_pins[] = { 0x06, };
static int x2000_i2s3_clk_tx_pins[] = { 0x10, 0x02, };
static int x2000_i2s3_sysclk_tx_pins[] = { 0x00, };
static int x2000_dmic_if0_pins[] = { 0x54, 0x55, };
static int x2000_dmic_if1_pins[] = { 0x56, };
static int x2000_dmic_if2_pins[] = { 0x57, };
static int x2000_dmic_if3_pins[] = { 0x58, };
static int x2000_cim_8bit_pins[] = {
	0x0e, 0x0c, 0x0d, 0x4f,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
static int x2000_cim_12bit_pins[] = { 0x08, 0x09, 0x0a, 0x0b, };
static int x2000_lcd_tft_8bit_pins[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x38, 0x3a, 0x39, 0x3b,
};
static int x2000_lcd_tft_16bit_pins[] = {
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};
static int x2000_lcd_tft_18bit_pins[] = {
	0x30, 0x31,
};
static int x2000_lcd_tft_24bit_pins[] = {
	0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
};
static int x2000_lcd_slcd_8bit_pins[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x3a, 0x38, 0x3b, 0x30, 0x39,
};
static int x2000_pwm_pwm0_c_pins[] = { 0x40, };
static int x2000_pwm_pwm0_d_pins[] = { 0x7e, };
static int x2000_pwm_pwm1_c_pins[] = { 0x41, };
static int x2000_pwm_pwm1_d_pins[] = { 0x7f, };
static int x2000_pwm_pwm2_c_pins[] = { 0x42, };
static int x2000_pwm_pwm2_e_pins[] = { 0x80, };
static int x2000_pwm_pwm3_c_pins[] = { 0x43, };
static int x2000_pwm_pwm3_e_pins[] = { 0x81, };
static int x2000_pwm_pwm4_c_pins[] = { 0x44, };
static int x2000_pwm_pwm4_e_pins[] = { 0x82, };
static int x2000_pwm_pwm5_c_pins[] = { 0x45, };
static int x2000_pwm_pwm5_e_pins[] = { 0x83, };
static int x2000_pwm_pwm6_c_pins[] = { 0x46, };
static int x2000_pwm_pwm6_e_pins[] = { 0x84, };
static int x2000_pwm_pwm7_c_pins[] = { 0x47, };
static int x2000_pwm_pwm7_e_pins[] = { 0x85, };
static int x2000_pwm_pwm8_pins[] = { 0x48, };
static int x2000_pwm_pwm9_pins[] = { 0x49, };
static int x2000_pwm_pwm10_pins[] = { 0x4a, };
static int x2000_pwm_pwm11_pins[] = { 0x4b, };
static int x2000_pwm_pwm12_pins[] = { 0x4c, };
static int x2000_pwm_pwm13_pins[] = { 0x4d, };
static int x2000_pwm_pwm14_pins[] = { 0x4e, };
static int x2000_pwm_pwm15_pins[] = { 0x4f, };
static int x2000_mac0_rmii_pins[] = {
	0x4b, 0x47, 0x46, 0x4a, 0x43, 0x42, 0x4c, 0x4d, 0x4e, 0x41,
};
static int x2000_mac0_rgmii_pins[] = {
	0x4b, 0x49, 0x48, 0x47, 0x46, 0x4a, 0x45, 0x44, 0x43, 0x42,
	0x4c, 0x4d, 0x4f, 0x4e, 0x41,
};
static int x2000_mac1_rmii_pins[] = {
	0x32, 0x2d, 0x2c, 0x31, 0x29, 0x28, 0x33, 0x34, 0x35, 0x37,
};
static int x2000_mac1_rgmii_pins[] = {
	0x32, 0x2f, 0x2e, 0x2d, 0x2c, 0x31, 0x2b, 0x2a, 0x29, 0x28,
	0x33, 0x34, 0x36, 0x35, 0x37,
};
static int x2000_otg_pins[] = { 0x96, };

static u8 x2000_cim_8bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, };

static const struct group_desc x2000_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x2000_uart0_data, 2),
	INGENIC_PIN_GROUP("uart0-hwflow", x2000_uart0_hwflow, 2),
	INGENIC_PIN_GROUP("uart1-data", x2000_uart1_data, 1),
	INGENIC_PIN_GROUP("uart1-hwflow", x2000_uart1_hwflow, 1),
	INGENIC_PIN_GROUP("uart2-data", x2000_uart2_data, 0),
	INGENIC_PIN_GROUP("uart3-data-c", x2000_uart3_data_c, 0),
	INGENIC_PIN_GROUP("uart3-data-d", x2000_uart3_data_d, 1),
	INGENIC_PIN_GROUP("uart3-hwflow-c", x2000_uart3_hwflow_c, 0),
	INGENIC_PIN_GROUP("uart3-hwflow-d", x2000_uart3_hwflow_d, 1),
	INGENIC_PIN_GROUP("uart4-data-a", x2000_uart4_data_a, 1),
	INGENIC_PIN_GROUP("uart4-data-c", x2000_uart4_data_c, 3),
	INGENIC_PIN_GROUP("uart4-hwflow-a", x2000_uart4_hwflow_a, 1),
	INGENIC_PIN_GROUP("uart4-hwflow-c", x2000_uart4_hwflow_c, 3),
	INGENIC_PIN_GROUP("uart5-data-a", x2000_uart5_data_a, 1),
	INGENIC_PIN_GROUP("uart5-data-c", x2000_uart5_data_c, 3),
	INGENIC_PIN_GROUP("uart6-data-a", x2000_uart6_data_a, 1),
	INGENIC_PIN_GROUP("uart6-data-c", x2000_uart6_data_c, 3),
	INGENIC_PIN_GROUP("uart7-data-a", x2000_uart7_data_a, 1),
	INGENIC_PIN_GROUP("uart7-data-c", x2000_uart7_data_c, 3),
	INGENIC_PIN_GROUP("uart8-data", x2000_uart8_data, 3),
	INGENIC_PIN_GROUP("uart9-data", x2000_uart9_data, 3),
	INGENIC_PIN_GROUP("sfc-data-if0-d", x2000_sfc_data_if0_d, 1),
	INGENIC_PIN_GROUP("sfc-data-if0-e", x2000_sfc_data_if0_e, 0),
	INGENIC_PIN_GROUP("sfc-data-if1", x2000_sfc_data_if1, 1),
	INGENIC_PIN_GROUP("sfc-clk-d", x2000_sfc_clk_d, 1),
	INGENIC_PIN_GROUP("sfc-clk-e", x2000_sfc_clk_e, 0),
	INGENIC_PIN_GROUP("sfc-ce-d", x2000_sfc_ce_d, 1),
	INGENIC_PIN_GROUP("sfc-ce-e", x2000_sfc_ce_e, 0),
	INGENIC_PIN_GROUP("ssi0-dt-b", x2000_ssi0_dt_b, 1),
	INGENIC_PIN_GROUP("ssi0-dt-d", x2000_ssi0_dt_d, 1),
	INGENIC_PIN_GROUP("ssi0-dr-b", x2000_ssi0_dr_b, 1),
	INGENIC_PIN_GROUP("ssi0-dr-d", x2000_ssi0_dr_d, 1),
	INGENIC_PIN_GROUP("ssi0-clk-b", x2000_ssi0_clk_b, 1),
	INGENIC_PIN_GROUP("ssi0-clk-d", x2000_ssi0_clk_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce-b", x2000_ssi0_ce_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce-d", x2000_ssi0_ce_d, 1),
	INGENIC_PIN_GROUP("ssi1-dt-c", x2000_ssi1_dt_c, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d", x2000_ssi1_dt_d, 2),
	INGENIC_PIN_GROUP("ssi1-dt-e", x2000_ssi1_dt_e, 1),
	INGENIC_PIN_GROUP("ssi1-dr-c", x2000_ssi1_dr_c, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d", x2000_ssi1_dr_d, 2),
	INGENIC_PIN_GROUP("ssi1-dr-e", x2000_ssi1_dr_e, 1),
	INGENIC_PIN_GROUP("ssi1-clk-c", x2000_ssi1_clk_c, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", x2000_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e", x2000_ssi1_clk_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce-c", x2000_ssi1_ce_c, 2),
	INGENIC_PIN_GROUP("ssi1-ce-d", x2000_ssi1_ce_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce-e", x2000_ssi1_ce_e, 1),
	INGENIC_PIN_GROUP("mmc0-1bit", x2000_mmc0_1bit, 0),
	INGENIC_PIN_GROUP("mmc0-4bit", x2000_mmc0_4bit, 0),
	INGENIC_PIN_GROUP("mmc0-8bit", x2000_mmc0_8bit, 0),
	INGENIC_PIN_GROUP("mmc1-1bit", x2000_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", x2000_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("mmc2-1bit", x2000_mmc2_1bit, 0),
	INGENIC_PIN_GROUP("mmc2-4bit", x2000_mmc2_4bit, 0),
	INGENIC_PIN_GROUP("emc-8bit-data", x2000_emc_8bit_data, 0),
	INGENIC_PIN_GROUP("emc-16bit-data", x2000_emc_16bit_data, 0),
	INGENIC_PIN_GROUP("emc-addr", x2000_emc_addr, 0),
	INGENIC_PIN_GROUP("emc-rd-we", x2000_emc_rd_we, 0),
	INGENIC_PIN_GROUP("emc-wait", x2000_emc_wait, 0),
	INGENIC_PIN_GROUP("emc-cs1", x2000_emc_cs1, 3),
	INGENIC_PIN_GROUP("emc-cs2", x2000_emc_cs2, 3),
	INGENIC_PIN_GROUP("i2c0-data", x2000_i2c0, 3),
	INGENIC_PIN_GROUP("i2c1-data-c", x2000_i2c1_c, 2),
	INGENIC_PIN_GROUP("i2c1-data-d", x2000_i2c1_d, 1),
	INGENIC_PIN_GROUP("i2c2-data-b", x2000_i2c2_b, 2),
	INGENIC_PIN_GROUP("i2c2-data-d", x2000_i2c2_d, 2),
	INGENIC_PIN_GROUP("i2c2-data-e", x2000_i2c2_e, 1),
	INGENIC_PIN_GROUP("i2c3-data-a", x2000_i2c3_a, 0),
	INGENIC_PIN_GROUP("i2c3-data-d", x2000_i2c3_d, 1),
	INGENIC_PIN_GROUP("i2c4-data-c", x2000_i2c4_c, 1),
	INGENIC_PIN_GROUP("i2c4-data-d", x2000_i2c4_d, 2),
	INGENIC_PIN_GROUP("i2c5-data-c", x2000_i2c5_c, 1),
	INGENIC_PIN_GROUP("i2c5-data-d", x2000_i2c5_d, 1),
	INGENIC_PIN_GROUP("i2s1-data-tx", x2000_i2s1_data_tx, 2),
	INGENIC_PIN_GROUP("i2s1-data-rx", x2000_i2s1_data_rx, 2),
	INGENIC_PIN_GROUP("i2s1-clk-tx", x2000_i2s1_clk_tx, 2),
	INGENIC_PIN_GROUP("i2s1-clk-rx", x2000_i2s1_clk_rx, 2),
	INGENIC_PIN_GROUP("i2s1-sysclk-tx", x2000_i2s1_sysclk_tx, 2),
	INGENIC_PIN_GROUP("i2s1-sysclk-rx", x2000_i2s1_sysclk_rx, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx0", x2000_i2s2_data_rx0, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx1", x2000_i2s2_data_rx1, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx2", x2000_i2s2_data_rx2, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx3", x2000_i2s2_data_rx3, 2),
	INGENIC_PIN_GROUP("i2s2-clk-rx", x2000_i2s2_clk_rx, 2),
	INGENIC_PIN_GROUP("i2s2-sysclk-rx", x2000_i2s2_sysclk_rx, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx0", x2000_i2s3_data_tx0, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx1", x2000_i2s3_data_tx1, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx2", x2000_i2s3_data_tx2, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx3", x2000_i2s3_data_tx3, 2),
	INGENIC_PIN_GROUP("i2s3-clk-tx", x2000_i2s3_clk_tx, 2),
	INGENIC_PIN_GROUP("i2s3-sysclk-tx", x2000_i2s3_sysclk_tx, 2),
	INGENIC_PIN_GROUP("dmic-if0", x2000_dmic_if0, 0),
	INGENIC_PIN_GROUP("dmic-if1", x2000_dmic_if1, 0),
	INGENIC_PIN_GROUP("dmic-if2", x2000_dmic_if2, 0),
	INGENIC_PIN_GROUP("dmic-if3", x2000_dmic_if3, 0),
	INGENIC_PIN_GROUP_FUNCS("cim-data-8bit", x2000_cim_8bit,
				x2000_cim_8bit_funcs),
	INGENIC_PIN_GROUP("cim-data-12bit", x2000_cim_12bit, 0),
	INGENIC_PIN_GROUP("lcd-tft-8bit", x2000_lcd_tft_8bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-16bit", x2000_lcd_tft_16bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-18bit", x2000_lcd_tft_18bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-24bit", x2000_lcd_tft_24bit, 1),
	INGENIC_PIN_GROUP("lcd-slcd-8bit", x2000_lcd_slcd_8bit, 2),
	INGENIC_PIN_GROUP("lcd-slcd-16bit", x2000_lcd_tft_16bit, 2),
	INGENIC_PIN_GROUP("pwm0-c", x2000_pwm_pwm0_c, 0),
	INGENIC_PIN_GROUP("pwm0-d", x2000_pwm_pwm0_d, 2),
	INGENIC_PIN_GROUP("pwm1-c", x2000_pwm_pwm1_c, 0),
	INGENIC_PIN_GROUP("pwm1-d", x2000_pwm_pwm1_d, 2),
	INGENIC_PIN_GROUP("pwm2-c", x2000_pwm_pwm2_c, 0),
	INGENIC_PIN_GROUP("pwm2-e", x2000_pwm_pwm2_e, 1),
	INGENIC_PIN_GROUP("pwm3-c", x2000_pwm_pwm3_c, 0),
	INGENIC_PIN_GROUP("pwm3-e", x2000_pwm_pwm3_e, 1),
	INGENIC_PIN_GROUP("pwm4-c", x2000_pwm_pwm4_c, 0),
	INGENIC_PIN_GROUP("pwm4-e", x2000_pwm_pwm4_e, 1),
	INGENIC_PIN_GROUP("pwm5-c", x2000_pwm_pwm5_c, 0),
	INGENIC_PIN_GROUP("pwm5-e", x2000_pwm_pwm5_e, 1),
	INGENIC_PIN_GROUP("pwm6-c", x2000_pwm_pwm6_c, 0),
	INGENIC_PIN_GROUP("pwm6-e", x2000_pwm_pwm6_e, 1),
	INGENIC_PIN_GROUP("pwm7-c", x2000_pwm_pwm7_c, 0),
	INGENIC_PIN_GROUP("pwm7-e", x2000_pwm_pwm7_e, 1),
	INGENIC_PIN_GROUP("pwm8", x2000_pwm_pwm8, 0),
	INGENIC_PIN_GROUP("pwm9", x2000_pwm_pwm9, 0),
	INGENIC_PIN_GROUP("pwm10", x2000_pwm_pwm10, 0),
	INGENIC_PIN_GROUP("pwm11", x2000_pwm_pwm11, 0),
	INGENIC_PIN_GROUP("pwm12", x2000_pwm_pwm12, 0),
	INGENIC_PIN_GROUP("pwm13", x2000_pwm_pwm13, 0),
	INGENIC_PIN_GROUP("pwm14", x2000_pwm_pwm14, 0),
	INGENIC_PIN_GROUP("pwm15", x2000_pwm_pwm15, 0),
	INGENIC_PIN_GROUP("mac0-rmii", x2000_mac0_rmii, 1),
	INGENIC_PIN_GROUP("mac0-rgmii", x2000_mac0_rgmii, 1),
	INGENIC_PIN_GROUP("mac1-rmii", x2000_mac1_rmii, 3),
	INGENIC_PIN_GROUP("mac1-rgmii", x2000_mac1_rgmii, 3),
	INGENIC_PIN_GROUP("otg-vbus", x2000_otg, 0),
};

static const char *x2000_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x2000_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *x2000_uart2_groups[] = { "uart2-data", };
static const char *x2000_uart3_groups[] = {
	"uart3-data-c", "uart3-data-d", "uart3-hwflow-c", "uart3-hwflow-d",
};
static const char *x2000_uart4_groups[] = {
	"uart4-data-a", "uart4-data-c", "uart4-hwflow-a", "uart4-hwflow-c",
};
static const char *x2000_uart5_groups[] = { "uart5-data-a", "uart5-data-c", };
static const char *x2000_uart6_groups[] = { "uart6-data-a", "uart6-data-c", };
static const char *x2000_uart7_groups[] = { "uart7-data-a", "uart7-data-c", };
static const char *x2000_uart8_groups[] = { "uart8-data", };
static const char *x2000_uart9_groups[] = { "uart9-data", };
static const char *x2000_sfc_groups[] = {
	"sfc-data-if0-d", "sfc-data-if0-e", "sfc-data-if1",
	"sfc-clk-d", "sfc-clk-e", "sfc-ce-d", "sfc-ce-e",
};
static const char *x2000_ssi0_groups[] = {
	"ssi0-dt-b", "ssi0-dt-d",
	"ssi0-dr-b", "ssi0-dr-d",
	"ssi0-clk-b", "ssi0-clk-d",
	"ssi0-ce-b", "ssi0-ce-d",
};
static const char *x2000_ssi1_groups[] = {
	"ssi1-dt-c", "ssi1-dt-d", "ssi1-dt-e",
	"ssi1-dr-c", "ssi1-dr-d", "ssi1-dr-e",
	"ssi1-clk-c", "ssi1-clk-d", "ssi1-clk-e",
	"ssi1-ce-c", "ssi1-ce-d", "ssi1-ce-e",
};
static const char *x2000_mmc0_groups[] = { "mmc0-1bit", "mmc0-4bit", "mmc0-8bit", };
static const char *x2000_mmc1_groups[] = { "mmc1-1bit", "mmc1-4bit", };
static const char *x2000_mmc2_groups[] = { "mmc2-1bit", "mmc2-4bit", };
static const char *x2000_emc_groups[] = {
	"emc-8bit-data", "emc-16bit-data",
	"emc-addr", "emc-rd-we", "emc-wait",
};
static const char *x2000_cs1_groups[] = { "emc-cs1", };
static const char *x2000_cs2_groups[] = { "emc-cs2", };
static const char *x2000_i2c0_groups[] = { "i2c0-data", };
static const char *x2000_i2c1_groups[] = { "i2c1-data-c", "i2c1-data-d", };
static const char *x2000_i2c2_groups[] = { "i2c2-data-b", "i2c2-data-d", };
static const char *x2000_i2c3_groups[] = { "i2c3-data-a", "i2c3-data-d", };
static const char *x2000_i2c4_groups[] = { "i2c4-data-c", "i2c4-data-d", };
static const char *x2000_i2c5_groups[] = { "i2c5-data-c", "i2c5-data-d", };
static const char *x2000_i2s1_groups[] = {
	"i2s1-data-tx", "i2s1-data-rx",
	"i2s1-clk-tx", "i2s1-clk-rx",
	"i2s1-sysclk-tx", "i2s1-sysclk-rx",
};
static const char *x2000_i2s2_groups[] = {
	"i2s2-data-rx0", "i2s2-data-rx1", "i2s2-data-rx2", "i2s2-data-rx3",
	"i2s2-clk-rx", "i2s2-sysclk-rx",
};
static const char *x2000_i2s3_groups[] = {
	"i2s3-data-tx0", "i2s3-data-tx1", "i2s3-data-tx2", "i2s3-data-tx3",
	"i2s3-clk-tx", "i2s3-sysclk-tx",
};
static const char *x2000_dmic_groups[] = {
	"dmic-if0", "dmic-if1", "dmic-if2", "dmic-if3",
};
static const char *x2000_cim_groups[] = { "cim-data-8bit", "cim-data-12bit", };
static const char *x2000_lcd_groups[] = {
	"lcd-tft-8bit", "lcd-tft-16bit", "lcd-tft-18bit", "lcd-tft-24bit",
	"lcd-slcd-8bit", "lcd-slcd-16bit",
};
static const char *x2000_pwm0_groups[] = { "pwm0-c", "pwm0-d", };
static const char *x2000_pwm1_groups[] = { "pwm1-c", "pwm1-d", };
static const char *x2000_pwm2_groups[] = { "pwm2-c", "pwm2-e", };
static const char *x2000_pwm3_groups[] = { "pwm3-c", "pwm3-r", };
static const char *x2000_pwm4_groups[] = { "pwm4-c", "pwm4-e", };
static const char *x2000_pwm5_groups[] = { "pwm5-c", "pwm5-e", };
static const char *x2000_pwm6_groups[] = { "pwm6-c", "pwm6-e", };
static const char *x2000_pwm7_groups[] = { "pwm7-c", "pwm7-e", };
static const char *x2000_pwm8_groups[] = { "pwm8", };
static const char *x2000_pwm9_groups[] = { "pwm9", };
static const char *x2000_pwm10_groups[] = { "pwm10", };
static const char *x2000_pwm11_groups[] = { "pwm11", };
static const char *x2000_pwm12_groups[] = { "pwm12", };
static const char *x2000_pwm13_groups[] = { "pwm13", };
static const char *x2000_pwm14_groups[] = { "pwm14", };
static const char *x2000_pwm15_groups[] = { "pwm15", };
static const char *x2000_mac0_groups[] = { "mac0-rmii", "mac0-rgmii", };
static const char *x2000_mac1_groups[] = { "mac1-rmii", "mac1-rgmii", };
static const char *x2000_otg_groups[] = { "otg-vbus", };

static const struct function_desc x2000_functions[] = {
	{ "uart0", x2000_uart0_groups, ARRAY_SIZE(x2000_uart0_groups), },
	{ "uart1", x2000_uart1_groups, ARRAY_SIZE(x2000_uart1_groups), },
	{ "uart2", x2000_uart2_groups, ARRAY_SIZE(x2000_uart2_groups), },
	{ "uart3", x2000_uart3_groups, ARRAY_SIZE(x2000_uart3_groups), },
	{ "uart4", x2000_uart4_groups, ARRAY_SIZE(x2000_uart4_groups), },
	{ "uart5", x2000_uart5_groups, ARRAY_SIZE(x2000_uart5_groups), },
	{ "uart6", x2000_uart6_groups, ARRAY_SIZE(x2000_uart6_groups), },
	{ "uart7", x2000_uart7_groups, ARRAY_SIZE(x2000_uart7_groups), },
	{ "uart8", x2000_uart8_groups, ARRAY_SIZE(x2000_uart8_groups), },
	{ "uart9", x2000_uart9_groups, ARRAY_SIZE(x2000_uart9_groups), },
	{ "sfc", x2000_sfc_groups, ARRAY_SIZE(x2000_sfc_groups), },
	{ "ssi0", x2000_ssi0_groups, ARRAY_SIZE(x2000_ssi0_groups), },
	{ "ssi1", x2000_ssi1_groups, ARRAY_SIZE(x2000_ssi1_groups), },
	{ "mmc0", x2000_mmc0_groups, ARRAY_SIZE(x2000_mmc0_groups), },
	{ "mmc1", x2000_mmc1_groups, ARRAY_SIZE(x2000_mmc1_groups), },
	{ "mmc2", x2000_mmc2_groups, ARRAY_SIZE(x2000_mmc2_groups), },
	{ "emc", x2000_emc_groups, ARRAY_SIZE(x2000_emc_groups), },
	{ "emc-cs1", x2000_cs1_groups, ARRAY_SIZE(x2000_cs1_groups), },
	{ "emc-cs2", x2000_cs2_groups, ARRAY_SIZE(x2000_cs2_groups), },
	{ "i2c0", x2000_i2c0_groups, ARRAY_SIZE(x2000_i2c0_groups), },
	{ "i2c1", x2000_i2c1_groups, ARRAY_SIZE(x2000_i2c1_groups), },
	{ "i2c2", x2000_i2c2_groups, ARRAY_SIZE(x2000_i2c2_groups), },
	{ "i2c3", x2000_i2c3_groups, ARRAY_SIZE(x2000_i2c3_groups), },
	{ "i2c4", x2000_i2c4_groups, ARRAY_SIZE(x2000_i2c4_groups), },
	{ "i2c5", x2000_i2c5_groups, ARRAY_SIZE(x2000_i2c5_groups), },
	{ "i2s1", x2000_i2s1_groups, ARRAY_SIZE(x2000_i2s1_groups), },
	{ "i2s2", x2000_i2s2_groups, ARRAY_SIZE(x2000_i2s2_groups), },
	{ "i2s3", x2000_i2s3_groups, ARRAY_SIZE(x2000_i2s3_groups), },
	{ "dmic", x2000_dmic_groups, ARRAY_SIZE(x2000_dmic_groups), },
	{ "cim", x2000_cim_groups, ARRAY_SIZE(x2000_cim_groups), },
	{ "lcd", x2000_lcd_groups, ARRAY_SIZE(x2000_lcd_groups), },
	{ "pwm0", x2000_pwm0_groups, ARRAY_SIZE(x2000_pwm0_groups), },
	{ "pwm1", x2000_pwm1_groups, ARRAY_SIZE(x2000_pwm1_groups), },
	{ "pwm2", x2000_pwm2_groups, ARRAY_SIZE(x2000_pwm2_groups), },
	{ "pwm3", x2000_pwm3_groups, ARRAY_SIZE(x2000_pwm3_groups), },
	{ "pwm4", x2000_pwm4_groups, ARRAY_SIZE(x2000_pwm4_groups), },
	{ "pwm5", x2000_pwm5_groups, ARRAY_SIZE(x2000_pwm5_groups), },
	{ "pwm6", x2000_pwm6_groups, ARRAY_SIZE(x2000_pwm6_groups), },
	{ "pwm7", x2000_pwm7_groups, ARRAY_SIZE(x2000_pwm7_groups), },
	{ "pwm8", x2000_pwm8_groups, ARRAY_SIZE(x2000_pwm8_groups), },
	{ "pwm9", x2000_pwm9_groups, ARRAY_SIZE(x2000_pwm9_groups), },
	{ "pwm10", x2000_pwm10_groups, ARRAY_SIZE(x2000_pwm10_groups), },
	{ "pwm11", x2000_pwm11_groups, ARRAY_SIZE(x2000_pwm11_groups), },
	{ "pwm12", x2000_pwm12_groups, ARRAY_SIZE(x2000_pwm12_groups), },
	{ "pwm13", x2000_pwm13_groups, ARRAY_SIZE(x2000_pwm13_groups), },
	{ "pwm14", x2000_pwm14_groups, ARRAY_SIZE(x2000_pwm14_groups), },
	{ "pwm15", x2000_pwm15_groups, ARRAY_SIZE(x2000_pwm15_groups), },
	{ "mac0", x2000_mac0_groups, ARRAY_SIZE(x2000_mac0_groups), },
	{ "mac1", x2000_mac1_groups, ARRAY_SIZE(x2000_mac1_groups), },
	{ "otg", x2000_otg_groups, ARRAY_SIZE(x2000_otg_groups), },
};

static const struct regmap_range x2000_access_ranges[] = {
	regmap_reg_range(0x000, 0x500 - 4),
	regmap_reg_range(0x700, 0x800 - 4),
};

/* shared with X2100 */
static const struct regmap_access_table x2000_access_table = {
	.yes_ranges = x2000_access_ranges,
	.n_yes_ranges = ARRAY_SIZE(x2000_access_ranges),
};

static const struct ingenic_chip_info x2000_chip_info = {
	.num_chips = 5,
	.reg_offset = 0x100,
	.version = ID_X2000,
	.groups = x2000_groups,
	.num_groups = ARRAY_SIZE(x2000_groups),
	.functions = x2000_functions,
	.num_functions = ARRAY_SIZE(x2000_functions),
	.pull_ups = x2000_pull_ups,
	.pull_downs = x2000_pull_downs,
	.access_table = &x2000_access_table,
};

static const u32 x2100_pull_ups[5] = {
	0x0003ffff, 0xffffffff, 0x1ff0ffff, 0xc7fe3f3f, 0x0fbf003f,
};

static const u32 x2100_pull_downs[5] = {
	0x0003ffff, 0xffffffff, 0x1ff0ffff, 0x00000000, 0x0fbf003f,
};

static int x2100_mac_pins[] = {
	0x4b, 0x47, 0x46, 0x4a, 0x43, 0x42, 0x4c, 0x4d, 0x4f, 0x41,
};

static const struct group_desc x2100_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x2000_uart0_data, 2),
	INGENIC_PIN_GROUP("uart0-hwflow", x2000_uart0_hwflow, 2),
	INGENIC_PIN_GROUP("uart1-data", x2000_uart1_data, 1),
	INGENIC_PIN_GROUP("uart1-hwflow", x2000_uart1_hwflow, 1),
	INGENIC_PIN_GROUP("uart2-data", x2000_uart2_data, 0),
	INGENIC_PIN_GROUP("uart3-data-c", x2000_uart3_data_c, 0),
	INGENIC_PIN_GROUP("uart3-data-d", x2000_uart3_data_d, 1),
	INGENIC_PIN_GROUP("uart3-hwflow-c", x2000_uart3_hwflow_c, 0),
	INGENIC_PIN_GROUP("uart3-hwflow-d", x2000_uart3_hwflow_d, 1),
	INGENIC_PIN_GROUP("uart4-data-a", x2000_uart4_data_a, 1),
	INGENIC_PIN_GROUP("uart4-data-c", x2000_uart4_data_c, 3),
	INGENIC_PIN_GROUP("uart4-hwflow-a", x2000_uart4_hwflow_a, 1),
	INGENIC_PIN_GROUP("uart4-hwflow-c", x2000_uart4_hwflow_c, 3),
	INGENIC_PIN_GROUP("uart5-data-a", x2000_uart5_data_a, 1),
	INGENIC_PIN_GROUP("uart5-data-c", x2000_uart5_data_c, 3),
	INGENIC_PIN_GROUP("uart6-data-a", x2000_uart6_data_a, 1),
	INGENIC_PIN_GROUP("uart6-data-c", x2000_uart6_data_c, 3),
	INGENIC_PIN_GROUP("uart7-data-a", x2000_uart7_data_a, 1),
	INGENIC_PIN_GROUP("uart7-data-c", x2000_uart7_data_c, 3),
	INGENIC_PIN_GROUP("uart8-data", x2000_uart8_data, 3),
	INGENIC_PIN_GROUP("uart9-data", x2000_uart9_data, 3),
	INGENIC_PIN_GROUP("sfc-data-if0-d", x2000_sfc_data_if0_d, 1),
	INGENIC_PIN_GROUP("sfc-data-if0-e", x2000_sfc_data_if0_e, 0),
	INGENIC_PIN_GROUP("sfc-data-if1", x2000_sfc_data_if1, 1),
	INGENIC_PIN_GROUP("sfc-clk-d", x2000_sfc_clk_d, 1),
	INGENIC_PIN_GROUP("sfc-clk-e", x2000_sfc_clk_e, 0),
	INGENIC_PIN_GROUP("sfc-ce-d", x2000_sfc_ce_d, 1),
	INGENIC_PIN_GROUP("sfc-ce-e", x2000_sfc_ce_e, 0),
	INGENIC_PIN_GROUP("ssi0-dt-b", x2000_ssi0_dt_b, 1),
	INGENIC_PIN_GROUP("ssi0-dt-d", x2000_ssi0_dt_d, 1),
	INGENIC_PIN_GROUP("ssi0-dr-b", x2000_ssi0_dr_b, 1),
	INGENIC_PIN_GROUP("ssi0-dr-d", x2000_ssi0_dr_d, 1),
	INGENIC_PIN_GROUP("ssi0-clk-b", x2000_ssi0_clk_b, 1),
	INGENIC_PIN_GROUP("ssi0-clk-d", x2000_ssi0_clk_d, 1),
	INGENIC_PIN_GROUP("ssi0-ce-b", x2000_ssi0_ce_b, 1),
	INGENIC_PIN_GROUP("ssi0-ce-d", x2000_ssi0_ce_d, 1),
	INGENIC_PIN_GROUP("ssi1-dt-c", x2000_ssi1_dt_c, 2),
	INGENIC_PIN_GROUP("ssi1-dt-d", x2000_ssi1_dt_d, 2),
	INGENIC_PIN_GROUP("ssi1-dt-e", x2000_ssi1_dt_e, 1),
	INGENIC_PIN_GROUP("ssi1-dr-c", x2000_ssi1_dr_c, 2),
	INGENIC_PIN_GROUP("ssi1-dr-d", x2000_ssi1_dr_d, 2),
	INGENIC_PIN_GROUP("ssi1-dr-e", x2000_ssi1_dr_e, 1),
	INGENIC_PIN_GROUP("ssi1-clk-c", x2000_ssi1_clk_c, 2),
	INGENIC_PIN_GROUP("ssi1-clk-d", x2000_ssi1_clk_d, 2),
	INGENIC_PIN_GROUP("ssi1-clk-e", x2000_ssi1_clk_e, 1),
	INGENIC_PIN_GROUP("ssi1-ce-c", x2000_ssi1_ce_c, 2),
	INGENIC_PIN_GROUP("ssi1-ce-d", x2000_ssi1_ce_d, 2),
	INGENIC_PIN_GROUP("ssi1-ce-e", x2000_ssi1_ce_e, 1),
	INGENIC_PIN_GROUP("mmc0-1bit", x2000_mmc0_1bit, 0),
	INGENIC_PIN_GROUP("mmc0-4bit", x2000_mmc0_4bit, 0),
	INGENIC_PIN_GROUP("mmc0-8bit", x2000_mmc0_8bit, 0),
	INGENIC_PIN_GROUP("mmc1-1bit", x2000_mmc1_1bit, 0),
	INGENIC_PIN_GROUP("mmc1-4bit", x2000_mmc1_4bit, 0),
	INGENIC_PIN_GROUP("mmc2-1bit", x2000_mmc2_1bit, 0),
	INGENIC_PIN_GROUP("mmc2-4bit", x2000_mmc2_4bit, 0),
	INGENIC_PIN_GROUP("emc-8bit-data", x2000_emc_8bit_data, 0),
	INGENIC_PIN_GROUP("emc-16bit-data", x2000_emc_16bit_data, 0),
	INGENIC_PIN_GROUP("emc-addr", x2000_emc_addr, 0),
	INGENIC_PIN_GROUP("emc-rd-we", x2000_emc_rd_we, 0),
	INGENIC_PIN_GROUP("emc-wait", x2000_emc_wait, 0),
	INGENIC_PIN_GROUP("emc-cs1", x2000_emc_cs1, 3),
	INGENIC_PIN_GROUP("emc-cs2", x2000_emc_cs2, 3),
	INGENIC_PIN_GROUP("i2c0-data", x2000_i2c0, 3),
	INGENIC_PIN_GROUP("i2c1-data-c", x2000_i2c1_c, 2),
	INGENIC_PIN_GROUP("i2c1-data-d", x2000_i2c1_d, 1),
	INGENIC_PIN_GROUP("i2c2-data-b", x2000_i2c2_b, 2),
	INGENIC_PIN_GROUP("i2c2-data-d", x2000_i2c2_d, 2),
	INGENIC_PIN_GROUP("i2c2-data-e", x2000_i2c2_e, 1),
	INGENIC_PIN_GROUP("i2c3-data-a", x2000_i2c3_a, 0),
	INGENIC_PIN_GROUP("i2c3-data-d", x2000_i2c3_d, 1),
	INGENIC_PIN_GROUP("i2c4-data-c", x2000_i2c4_c, 1),
	INGENIC_PIN_GROUP("i2c4-data-d", x2000_i2c4_d, 2),
	INGENIC_PIN_GROUP("i2c5-data-c", x2000_i2c5_c, 1),
	INGENIC_PIN_GROUP("i2c5-data-d", x2000_i2c5_d, 1),
	INGENIC_PIN_GROUP("i2s1-data-tx", x2000_i2s1_data_tx, 2),
	INGENIC_PIN_GROUP("i2s1-data-rx", x2000_i2s1_data_rx, 2),
	INGENIC_PIN_GROUP("i2s1-clk-tx", x2000_i2s1_clk_tx, 2),
	INGENIC_PIN_GROUP("i2s1-clk-rx", x2000_i2s1_clk_rx, 2),
	INGENIC_PIN_GROUP("i2s1-sysclk-tx", x2000_i2s1_sysclk_tx, 2),
	INGENIC_PIN_GROUP("i2s1-sysclk-rx", x2000_i2s1_sysclk_rx, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx0", x2000_i2s2_data_rx0, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx1", x2000_i2s2_data_rx1, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx2", x2000_i2s2_data_rx2, 2),
	INGENIC_PIN_GROUP("i2s2-data-rx3", x2000_i2s2_data_rx3, 2),
	INGENIC_PIN_GROUP("i2s2-clk-rx", x2000_i2s2_clk_rx, 2),
	INGENIC_PIN_GROUP("i2s2-sysclk-rx", x2000_i2s2_sysclk_rx, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx0", x2000_i2s3_data_tx0, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx1", x2000_i2s3_data_tx1, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx2", x2000_i2s3_data_tx2, 2),
	INGENIC_PIN_GROUP("i2s3-data-tx3", x2000_i2s3_data_tx3, 2),
	INGENIC_PIN_GROUP("i2s3-clk-tx", x2000_i2s3_clk_tx, 2),
	INGENIC_PIN_GROUP("i2s3-sysclk-tx", x2000_i2s3_sysclk_tx, 2),
	INGENIC_PIN_GROUP("dmic-if0", x2000_dmic_if0, 0),
	INGENIC_PIN_GROUP("dmic-if1", x2000_dmic_if1, 0),
	INGENIC_PIN_GROUP("dmic-if2", x2000_dmic_if2, 0),
	INGENIC_PIN_GROUP("dmic-if3", x2000_dmic_if3, 0),
	INGENIC_PIN_GROUP_FUNCS("cim-data-8bit", x2000_cim_8bit,
				x2000_cim_8bit_funcs),
	INGENIC_PIN_GROUP("cim-data-12bit", x2000_cim_12bit, 0),
	INGENIC_PIN_GROUP("lcd-tft-8bit", x2000_lcd_tft_8bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-16bit", x2000_lcd_tft_16bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-18bit", x2000_lcd_tft_18bit, 1),
	INGENIC_PIN_GROUP("lcd-tft-24bit", x2000_lcd_tft_24bit, 1),
	INGENIC_PIN_GROUP("lcd-slcd-8bit", x2000_lcd_slcd_8bit, 2),
	INGENIC_PIN_GROUP("lcd-slcd-16bit", x2000_lcd_tft_16bit, 2),
	INGENIC_PIN_GROUP("pwm0-c", x2000_pwm_pwm0_c, 0),
	INGENIC_PIN_GROUP("pwm0-d", x2000_pwm_pwm0_d, 2),
	INGENIC_PIN_GROUP("pwm1-c", x2000_pwm_pwm1_c, 0),
	INGENIC_PIN_GROUP("pwm1-d", x2000_pwm_pwm1_d, 2),
	INGENIC_PIN_GROUP("pwm2-c", x2000_pwm_pwm2_c, 0),
	INGENIC_PIN_GROUP("pwm2-e", x2000_pwm_pwm2_e, 1),
	INGENIC_PIN_GROUP("pwm3-c", x2000_pwm_pwm3_c, 0),
	INGENIC_PIN_GROUP("pwm3-e", x2000_pwm_pwm3_e, 1),
	INGENIC_PIN_GROUP("pwm4-c", x2000_pwm_pwm4_c, 0),
	INGENIC_PIN_GROUP("pwm4-e", x2000_pwm_pwm4_e, 1),
	INGENIC_PIN_GROUP("pwm5-c", x2000_pwm_pwm5_c, 0),
	INGENIC_PIN_GROUP("pwm5-e", x2000_pwm_pwm5_e, 1),
	INGENIC_PIN_GROUP("pwm6-c", x2000_pwm_pwm6_c, 0),
	INGENIC_PIN_GROUP("pwm6-e", x2000_pwm_pwm6_e, 1),
	INGENIC_PIN_GROUP("pwm7-c", x2000_pwm_pwm7_c, 0),
	INGENIC_PIN_GROUP("pwm7-e", x2000_pwm_pwm7_e, 1),
	INGENIC_PIN_GROUP("pwm8", x2000_pwm_pwm8, 0),
	INGENIC_PIN_GROUP("pwm9", x2000_pwm_pwm9, 0),
	INGENIC_PIN_GROUP("pwm10", x2000_pwm_pwm10, 0),
	INGENIC_PIN_GROUP("pwm11", x2000_pwm_pwm11, 0),
	INGENIC_PIN_GROUP("pwm12", x2000_pwm_pwm12, 0),
	INGENIC_PIN_GROUP("pwm13", x2000_pwm_pwm13, 0),
	INGENIC_PIN_GROUP("pwm14", x2000_pwm_pwm14, 0),
	INGENIC_PIN_GROUP("pwm15", x2000_pwm_pwm15, 0),
	INGENIC_PIN_GROUP("mac", x2100_mac, 1),
};

static const char *x2100_mac_groups[] = { "mac", };

static const struct function_desc x2100_functions[] = {
	{ "uart0", x2000_uart0_groups, ARRAY_SIZE(x2000_uart0_groups), },
	{ "uart1", x2000_uart1_groups, ARRAY_SIZE(x2000_uart1_groups), },
	{ "uart2", x2000_uart2_groups, ARRAY_SIZE(x2000_uart2_groups), },
	{ "uart3", x2000_uart3_groups, ARRAY_SIZE(x2000_uart3_groups), },
	{ "uart4", x2000_uart4_groups, ARRAY_SIZE(x2000_uart4_groups), },
	{ "uart5", x2000_uart5_groups, ARRAY_SIZE(x2000_uart5_groups), },
	{ "uart6", x2000_uart6_groups, ARRAY_SIZE(x2000_uart6_groups), },
	{ "uart7", x2000_uart7_groups, ARRAY_SIZE(x2000_uart7_groups), },
	{ "uart8", x2000_uart8_groups, ARRAY_SIZE(x2000_uart8_groups), },
	{ "uart9", x2000_uart9_groups, ARRAY_SIZE(x2000_uart9_groups), },
	{ "sfc", x2000_sfc_groups, ARRAY_SIZE(x2000_sfc_groups), },
	{ "ssi0", x2000_ssi0_groups, ARRAY_SIZE(x2000_ssi0_groups), },
	{ "ssi1", x2000_ssi1_groups, ARRAY_SIZE(x2000_ssi1_groups), },
	{ "mmc0", x2000_mmc0_groups, ARRAY_SIZE(x2000_mmc0_groups), },
	{ "mmc1", x2000_mmc1_groups, ARRAY_SIZE(x2000_mmc1_groups), },
	{ "mmc2", x2000_mmc2_groups, ARRAY_SIZE(x2000_mmc2_groups), },
	{ "emc", x2000_emc_groups, ARRAY_SIZE(x2000_emc_groups), },
	{ "emc-cs1", x2000_cs1_groups, ARRAY_SIZE(x2000_cs1_groups), },
	{ "emc-cs2", x2000_cs2_groups, ARRAY_SIZE(x2000_cs2_groups), },
	{ "i2c0", x2000_i2c0_groups, ARRAY_SIZE(x2000_i2c0_groups), },
	{ "i2c1", x2000_i2c1_groups, ARRAY_SIZE(x2000_i2c1_groups), },
	{ "i2c2", x2000_i2c2_groups, ARRAY_SIZE(x2000_i2c2_groups), },
	{ "i2c3", x2000_i2c3_groups, ARRAY_SIZE(x2000_i2c3_groups), },
	{ "i2c4", x2000_i2c4_groups, ARRAY_SIZE(x2000_i2c4_groups), },
	{ "i2c5", x2000_i2c5_groups, ARRAY_SIZE(x2000_i2c5_groups), },
	{ "i2s1", x2000_i2s1_groups, ARRAY_SIZE(x2000_i2s1_groups), },
	{ "i2s2", x2000_i2s2_groups, ARRAY_SIZE(x2000_i2s2_groups), },
	{ "i2s3", x2000_i2s3_groups, ARRAY_SIZE(x2000_i2s3_groups), },
	{ "dmic", x2000_dmic_groups, ARRAY_SIZE(x2000_dmic_groups), },
	{ "cim", x2000_cim_groups, ARRAY_SIZE(x2000_cim_groups), },
	{ "lcd", x2000_lcd_groups, ARRAY_SIZE(x2000_lcd_groups), },
	{ "pwm0", x2000_pwm0_groups, ARRAY_SIZE(x2000_pwm0_groups), },
	{ "pwm1", x2000_pwm1_groups, ARRAY_SIZE(x2000_pwm1_groups), },
	{ "pwm2", x2000_pwm2_groups, ARRAY_SIZE(x2000_pwm2_groups), },
	{ "pwm3", x2000_pwm3_groups, ARRAY_SIZE(x2000_pwm3_groups), },
	{ "pwm4", x2000_pwm4_groups, ARRAY_SIZE(x2000_pwm4_groups), },
	{ "pwm5", x2000_pwm5_groups, ARRAY_SIZE(x2000_pwm5_groups), },
	{ "pwm6", x2000_pwm6_groups, ARRAY_SIZE(x2000_pwm6_groups), },
	{ "pwm7", x2000_pwm7_groups, ARRAY_SIZE(x2000_pwm7_groups), },
	{ "pwm8", x2000_pwm8_groups, ARRAY_SIZE(x2000_pwm8_groups), },
	{ "pwm9", x2000_pwm9_groups, ARRAY_SIZE(x2000_pwm9_groups), },
	{ "pwm10", x2000_pwm10_groups, ARRAY_SIZE(x2000_pwm10_groups), },
	{ "pwm11", x2000_pwm11_groups, ARRAY_SIZE(x2000_pwm11_groups), },
	{ "pwm12", x2000_pwm12_groups, ARRAY_SIZE(x2000_pwm12_groups), },
	{ "pwm13", x2000_pwm13_groups, ARRAY_SIZE(x2000_pwm13_groups), },
	{ "pwm14", x2000_pwm14_groups, ARRAY_SIZE(x2000_pwm14_groups), },
	{ "pwm15", x2000_pwm15_groups, ARRAY_SIZE(x2000_pwm15_groups), },
	{ "mac", x2100_mac_groups, ARRAY_SIZE(x2100_mac_groups), },
};

static const struct ingenic_chip_info x2100_chip_info = {
	.num_chips = 5,
	.reg_offset = 0x100,
	.version = ID_X2100,
	.groups = x2100_groups,
	.num_groups = ARRAY_SIZE(x2100_groups),
	.functions = x2100_functions,
	.num_functions = ARRAY_SIZE(x2100_functions),
	.pull_ups = x2100_pull_ups,
	.pull_downs = x2100_pull_downs,
	.access_table = &x2000_access_table,
};

static u32 ingenic_gpio_read_reg(struct ingenic_gpio_chip *jzgc, u8 reg)
{
	unsigned int val;

	regmap_read(jzgc->jzpc->map, jzgc->reg_base + reg, &val);

	return (u32) val;
}

static void ingenic_gpio_set_bit(struct ingenic_gpio_chip *jzgc,
		u8 reg, u8 offset, bool set)
{
	if (!is_soc_or_above(jzgc->jzpc, ID_JZ4740)) {
		regmap_update_bits(jzgc->jzpc->map, jzgc->reg_base + reg,
				BIT(offset), set ? BIT(offset) : 0);
		return;
	}

	if (set)
		reg = REG_SET(reg);
	else
		reg = REG_CLEAR(reg);

	regmap_write(jzgc->jzpc->map, jzgc->reg_base + reg, BIT(offset));
}

static void ingenic_gpio_shadow_set_bit(struct ingenic_gpio_chip *jzgc,
		u8 reg, u8 offset, bool set)
{
	if (set)
		reg = REG_SET(reg);
	else
		reg = REG_CLEAR(reg);

	regmap_write(jzgc->jzpc->map, REG_PZ_BASE(
			jzgc->jzpc->info->reg_offset) + reg, BIT(offset));
}

static void ingenic_gpio_shadow_set_bit_load(struct ingenic_gpio_chip *jzgc)
{
	regmap_write(jzgc->jzpc->map, REG_PZ_GID2LD(
			jzgc->jzpc->info->reg_offset),
			jzgc->gc.base / PINS_PER_GPIO_CHIP);
}

static void jz4730_gpio_set_bits(struct ingenic_gpio_chip *jzgc,
		u8 reg_upper, u8 reg_lower, u8 offset, u8 value)
{
	/*
	 * JZ4730 function and IRQ registers support two-bits-per-pin
	 * definitions, split into two groups of 16.
	 */
	u8 reg = offset < JZ4730_PINS_PER_PAIRED_REG ? reg_lower : reg_upper;
	unsigned int idx = offset % JZ4730_PINS_PER_PAIRED_REG;
	unsigned int mask = GENMASK(1, 0) << idx * 2;

	regmap_update_bits(jzgc->jzpc->map, jzgc->reg_base + reg, mask, value << (idx * 2));
}

static inline bool ingenic_gpio_get_value(struct ingenic_gpio_chip *jzgc,
					  u8 offset)
{
	unsigned int val = ingenic_gpio_read_reg(jzgc, GPIO_PIN);

	return !!(val & BIT(offset));
}

static void ingenic_gpio_set_value(struct ingenic_gpio_chip *jzgc,
				   u8 offset, int value)
{
	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770))
		ingenic_gpio_set_bit(jzgc, JZ4770_GPIO_PAT0, offset, !!value);
	else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_DATA, offset, !!value);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_DATA, offset, !!value);
}

static void irq_set_type(struct ingenic_gpio_chip *jzgc,
		u8 offset, unsigned int type)
{
	u8 reg1, reg2;
	bool val1, val2, val3;

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		val1 = val2 = false;
		val3 = true;
		break;
	case IRQ_TYPE_EDGE_RISING:
		val1 = val2 = true;
		val3 = false;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val1 = val3 = false;
		val2 = true;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val1 = true;
		val2 = val3 = false;
		break;
	case IRQ_TYPE_LEVEL_LOW:
	default:
		val1 = val2 = val3 = false;
		break;
	}

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770)) {
		reg1 = JZ4770_GPIO_PAT1;
		reg2 = JZ4770_GPIO_PAT0;
	} else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740)) {
		reg1 = JZ4740_GPIO_TRIG;
		reg2 = JZ4740_GPIO_DIR;
	} else {
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPDIR, offset, false);
		jz4730_gpio_set_bits(jzgc, JZ4730_GPIO_GPIDUR,
				JZ4730_GPIO_GPIDLR, offset, (val2 << 1) | val1);
		return;
	}

	if (is_soc_or_above(jzgc->jzpc, ID_X2000)) {
		ingenic_gpio_shadow_set_bit(jzgc, reg2, offset, val1);
		ingenic_gpio_shadow_set_bit(jzgc, reg1, offset, val2);
		ingenic_gpio_shadow_set_bit_load(jzgc);
		ingenic_gpio_set_bit(jzgc, X2000_GPIO_EDG, offset, val3);
	} else if (is_soc_or_above(jzgc->jzpc, ID_X1000)) {
		ingenic_gpio_shadow_set_bit(jzgc, reg2, offset, val1);
		ingenic_gpio_shadow_set_bit(jzgc, reg1, offset, val2);
		ingenic_gpio_shadow_set_bit_load(jzgc);
	} else {
		ingenic_gpio_set_bit(jzgc, reg2, offset, val1);
		ingenic_gpio_set_bit(jzgc, reg1, offset, val2);
	}
}

static void ingenic_gpio_irq_mask(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, GPIO_MSK, irq, true);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPIMR, irq, true);
}

static void ingenic_gpio_irq_unmask(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, GPIO_MSK, irq, false);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPIMR, irq, false);
}

static void ingenic_gpio_irq_enable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	gpiochip_enable_irq(gc, irq);

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770))
		ingenic_gpio_set_bit(jzgc, JZ4770_GPIO_INT, irq, true);
	else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_SELECT, irq, true);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPIER, irq, true);

	ingenic_gpio_irq_unmask(irqd);
}

static void ingenic_gpio_irq_disable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	ingenic_gpio_irq_mask(irqd);

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770))
		ingenic_gpio_set_bit(jzgc, JZ4770_GPIO_INT, irq, false);
	else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_SELECT, irq, false);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPIER, irq, false);

	gpiochip_disable_irq(gc, irq);
}

static void ingenic_gpio_irq_ack(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);
	bool high;

	if ((irqd_get_trigger_type(irqd) == IRQ_TYPE_EDGE_BOTH) &&
	    !is_soc_or_above(jzgc->jzpc, ID_X2000)) {
		/*
		 * Switch to an interrupt for the opposite edge to the one that
		 * triggered the interrupt being ACKed.
		 */
		high = ingenic_gpio_get_value(jzgc, irq);
		if (high)
			irq_set_type(jzgc, irq, IRQ_TYPE_LEVEL_LOW);
		else
			irq_set_type(jzgc, irq, IRQ_TYPE_LEVEL_HIGH);
	}

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770))
		ingenic_gpio_set_bit(jzgc, JZ4770_GPIO_FLAG, irq, false);
	else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_DATA, irq, true);
	else
		ingenic_gpio_set_bit(jzgc, JZ4730_GPIO_GPFR, irq, false);
}

static int ingenic_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
		irq_set_handler_locked(irqd, handle_edge_irq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		irq_set_handler_locked(irqd, handle_level_irq);
		break;
	default:
		irq_set_handler_locked(irqd, handle_bad_irq);
	}

	if ((type == IRQ_TYPE_EDGE_BOTH) && !is_soc_or_above(jzgc->jzpc, ID_X2000)) {
		/*
		 * The hardware does not support interrupts on both edges. The
		 * best we can do is to set up a single-edge interrupt and then
		 * switch to the opposing edge when ACKing the interrupt.
		 */
		bool high = ingenic_gpio_get_value(jzgc, irq);

		type = high ? IRQ_TYPE_LEVEL_LOW : IRQ_TYPE_LEVEL_HIGH;
	}

	irq_set_type(jzgc, irq, type);
	return 0;
}

static int ingenic_gpio_irq_set_wake(struct irq_data *irqd, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);

	return irq_set_irq_wake(jzgc->irq, on);
}

static void ingenic_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	struct irq_chip *irq_chip = irq_data_get_irq_chip(&desc->irq_data);
	unsigned long flag, i;

	chained_irq_enter(irq_chip, desc);

	if (is_soc_or_above(jzgc->jzpc, ID_JZ4770))
		flag = ingenic_gpio_read_reg(jzgc, JZ4770_GPIO_FLAG);
	else if (is_soc_or_above(jzgc->jzpc, ID_JZ4740))
		flag = ingenic_gpio_read_reg(jzgc, JZ4740_GPIO_FLAG);
	else
		flag = ingenic_gpio_read_reg(jzgc, JZ4730_GPIO_GPFR);

	for_each_set_bit(i, &flag, 32)
		generic_handle_domain_irq(gc->irq.domain, i);
	chained_irq_exit(irq_chip, desc);
}

static void ingenic_gpio_set(struct gpio_chip *gc,
		unsigned int offset, int value)
{
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);

	ingenic_gpio_set_value(jzgc, offset, value);
}

static int ingenic_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);

	return (int) ingenic_gpio_get_value(jzgc, offset);
}

static int ingenic_gpio_direction_input(struct gpio_chip *gc,
		unsigned int offset)
{
	return pinctrl_gpio_direction_input(gc->base + offset);
}

static int ingenic_gpio_direction_output(struct gpio_chip *gc,
		unsigned int offset, int value)
{
	ingenic_gpio_set(gc, offset, value);
	return pinctrl_gpio_direction_output(gc->base + offset);
}

static inline void ingenic_config_pin(struct ingenic_pinctrl *jzpc,
		unsigned int pin, unsigned int reg, bool set)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;

	if (set) {
		if (is_soc_or_above(jzpc, ID_JZ4740))
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_SET(reg), BIT(idx));
		else
			regmap_set_bits(jzpc->map, offt * jzpc->info->reg_offset +
					reg, BIT(idx));
	} else {
		if (is_soc_or_above(jzpc, ID_JZ4740))
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_CLEAR(reg), BIT(idx));
		else
			regmap_clear_bits(jzpc->map, offt * jzpc->info->reg_offset +
					reg, BIT(idx));
	}
}

static inline void ingenic_shadow_config_pin(struct ingenic_pinctrl *jzpc,
		unsigned int pin, u8 reg, bool set)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;

	regmap_write(jzpc->map, REG_PZ_BASE(jzpc->info->reg_offset) +
			(set ? REG_SET(reg) : REG_CLEAR(reg)), BIT(idx));
}

static inline void ingenic_shadow_config_pin_load(struct ingenic_pinctrl *jzpc,
		unsigned int pin)
{
	regmap_write(jzpc->map, REG_PZ_GID2LD(jzpc->info->reg_offset),
			pin / PINS_PER_GPIO_CHIP);
}

static inline void jz4730_config_pin_function(struct ingenic_pinctrl *jzpc,
		unsigned int pin, u8 reg_upper, u8 reg_lower, u8 value)
{
	/*
	 * JZ4730 function and IRQ registers support two-bits-per-pin
	 * definitions, split into two groups of 16.
	 */
	unsigned int idx = pin % JZ4730_PINS_PER_PAIRED_REG;
	unsigned int mask = GENMASK(1, 0) << idx * 2;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;
	u8 reg = (pin % PINS_PER_GPIO_CHIP) < JZ4730_PINS_PER_PAIRED_REG ? reg_lower : reg_upper;

	regmap_update_bits(jzpc->map, offt * jzpc->info->reg_offset + reg,
			mask, value << (idx * 2));
}

static inline bool ingenic_get_pin_config(struct ingenic_pinctrl *jzpc,
		unsigned int pin, unsigned int reg)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;
	unsigned int val;

	regmap_read(jzpc->map, offt * jzpc->info->reg_offset + reg, &val);

	return val & BIT(idx);
}

static int ingenic_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	struct ingenic_pinctrl *jzpc = jzgc->jzpc;
	unsigned int pin = gc->base + offset;

	if (is_soc_or_above(jzpc, ID_JZ4770)) {
		if (ingenic_get_pin_config(jzpc, pin, JZ4770_GPIO_INT) ||
		    ingenic_get_pin_config(jzpc, pin, JZ4770_GPIO_PAT1))
			return GPIO_LINE_DIRECTION_IN;
		return GPIO_LINE_DIRECTION_OUT;
	} else if (!is_soc_or_above(jzpc, ID_JZ4740)) {
		if (!ingenic_get_pin_config(jzpc, pin, JZ4730_GPIO_GPDIR))
			return GPIO_LINE_DIRECTION_IN;
		return GPIO_LINE_DIRECTION_OUT;
	}

	if (ingenic_get_pin_config(jzpc, pin, JZ4740_GPIO_SELECT))
		return GPIO_LINE_DIRECTION_IN;

	if (ingenic_get_pin_config(jzpc, pin, JZ4740_GPIO_DIR))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static const struct pinctrl_ops ingenic_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int ingenic_gpio_irq_request(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	irq_hw_number_t irq = irqd_to_hwirq(data);
	int ret;

	ret = ingenic_gpio_direction_input(gpio_chip, irq);
	if (ret)
		return ret;

	return gpiochip_reqres_irq(gpio_chip, irq);
}

static void ingenic_gpio_irq_release(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	irq_hw_number_t irq = irqd_to_hwirq(data);

	return gpiochip_relres_irq(gpio_chip, irq);
}

static void ingenic_gpio_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);

	seq_printf(p, "%s", gpio_chip->label);
}

static const struct irq_chip ingenic_gpio_irqchip = {
	.irq_enable		= ingenic_gpio_irq_enable,
	.irq_disable		= ingenic_gpio_irq_disable,
	.irq_unmask		= ingenic_gpio_irq_unmask,
	.irq_mask		= ingenic_gpio_irq_mask,
	.irq_ack		= ingenic_gpio_irq_ack,
	.irq_set_type		= ingenic_gpio_irq_set_type,
	.irq_set_wake		= ingenic_gpio_irq_set_wake,
	.irq_request_resources	= ingenic_gpio_irq_request,
	.irq_release_resources	= ingenic_gpio_irq_release,
	.irq_print_chip		= ingenic_gpio_irq_print_chip,
	.flags			= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
};

static int ingenic_pinmux_set_pin_fn(struct ingenic_pinctrl *jzpc,
		int pin, int func)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;

	dev_dbg(jzpc->dev, "set pin P%c%u to function %u\n",
			'A' + offt, idx, func);

	if (is_soc_or_above(jzpc, ID_X1000)) {
		ingenic_shadow_config_pin(jzpc, pin, JZ4770_GPIO_INT, false);
		ingenic_shadow_config_pin(jzpc, pin, GPIO_MSK, false);
		ingenic_shadow_config_pin(jzpc, pin, JZ4770_GPIO_PAT1, func & 0x2);
		ingenic_shadow_config_pin(jzpc, pin, JZ4770_GPIO_PAT0, func & 0x1);
		ingenic_shadow_config_pin_load(jzpc, pin);
	} else if (is_soc_or_above(jzpc, ID_JZ4770)) {
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_INT, false);
		ingenic_config_pin(jzpc, pin, GPIO_MSK, false);
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_PAT1, func & 0x2);
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_PAT0, func & 0x1);
	} else if (is_soc_or_above(jzpc, ID_JZ4740)) {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_FUNC, true);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_TRIG, func & 0x2);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_SELECT, func & 0x1);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4730_GPIO_GPIER, false);
		jz4730_config_pin_function(jzpc, pin, JZ4730_GPIO_GPAUR, JZ4730_GPIO_GPALR, func);
	}

	return 0;
}

static int ingenic_pinmux_set_mux(struct pinctrl_dev *pctldev,
		unsigned int selector, unsigned int group)
{
	struct ingenic_pinctrl *jzpc = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	unsigned int i;
	uintptr_t mode;
	u8 *pin_modes;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, grp->name);

	mode = (uintptr_t)grp->data;
	if (mode <= 3) {
		for (i = 0; i < grp->num_pins; i++)
			ingenic_pinmux_set_pin_fn(jzpc, grp->pins[i], mode);
	} else {
		pin_modes = grp->data;

		for (i = 0; i < grp->num_pins; i++)
			ingenic_pinmux_set_pin_fn(jzpc, grp->pins[i], pin_modes[i]);
	}

	return 0;
}

static int ingenic_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range,
		unsigned int pin, bool input)
{
	struct ingenic_pinctrl *jzpc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;

	dev_dbg(pctldev->dev, "set pin P%c%u to %sput\n",
			'A' + offt, idx, input ? "in" : "out");

	if (is_soc_or_above(jzpc, ID_X1000)) {
		ingenic_shadow_config_pin(jzpc, pin, JZ4770_GPIO_INT, false);
		ingenic_shadow_config_pin(jzpc, pin, GPIO_MSK, true);
		ingenic_shadow_config_pin(jzpc, pin, JZ4770_GPIO_PAT1, input);
		ingenic_shadow_config_pin_load(jzpc, pin);
	} else if (is_soc_or_above(jzpc, ID_JZ4770)) {
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_INT, false);
		ingenic_config_pin(jzpc, pin, GPIO_MSK, true);
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_PAT1, input);
	} else if (is_soc_or_above(jzpc, ID_JZ4740)) {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_SELECT, false);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_DIR, !input);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_FUNC, false);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4730_GPIO_GPIER, false);
		ingenic_config_pin(jzpc, pin, JZ4730_GPIO_GPDIR, !input);
		jz4730_config_pin_function(jzpc, pin, JZ4730_GPIO_GPAUR, JZ4730_GPIO_GPALR, 0);
	}

	return 0;
}

static const struct pinmux_ops ingenic_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = ingenic_pinmux_set_mux,
	.gpio_set_direction = ingenic_pinmux_gpio_set_direction,
};

static int ingenic_pinconf_get(struct pinctrl_dev *pctldev,
		unsigned int pin, unsigned long *config)
{
	struct ingenic_pinctrl *jzpc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;
	unsigned int arg = 1;
	unsigned int bias, reg;
	bool pull, pullup, pulldown;

	if (is_soc_or_above(jzpc, ID_X2000)) {
		pullup = ingenic_get_pin_config(jzpc, pin, X2000_GPIO_PEPU) &&
				!ingenic_get_pin_config(jzpc, pin, X2000_GPIO_PEPD) &&
				(jzpc->info->pull_ups[offt] & BIT(idx));
		pulldown = ingenic_get_pin_config(jzpc, pin, X2000_GPIO_PEPD) &&
				!ingenic_get_pin_config(jzpc, pin, X2000_GPIO_PEPU) &&
				(jzpc->info->pull_downs[offt] & BIT(idx));

	} else if (is_soc_or_above(jzpc, ID_X1830)) {
		unsigned int half = PINS_PER_GPIO_CHIP / 2;
		unsigned int idxh = (pin % half) * 2;

		if (idx < half)
			regmap_read(jzpc->map, offt * jzpc->info->reg_offset +
					X1830_GPIO_PEL, &bias);
		else
			regmap_read(jzpc->map, offt * jzpc->info->reg_offset +
					X1830_GPIO_PEH, &bias);

		bias = (bias >> idxh) & (GPIO_PULL_UP | GPIO_PULL_DOWN);

		pullup = (bias == GPIO_PULL_UP) && (jzpc->info->pull_ups[offt] & BIT(idx));
		pulldown = (bias == GPIO_PULL_DOWN) && (jzpc->info->pull_downs[offt] & BIT(idx));

	} else {
		if (is_soc_or_above(jzpc, ID_JZ4770))
			pull = !ingenic_get_pin_config(jzpc, pin, JZ4770_GPIO_PEN);
		else if (is_soc_or_above(jzpc, ID_JZ4740))
			pull = !ingenic_get_pin_config(jzpc, pin, JZ4740_GPIO_PULL_DIS);
		else
			pull = ingenic_get_pin_config(jzpc, pin, JZ4730_GPIO_GPPUR);

		pullup = pull && (jzpc->info->pull_ups[offt] & BIT(idx));
		pulldown = pull && (jzpc->info->pull_downs[offt] & BIT(idx));
	}

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pullup || pulldown)
			return -EINVAL;

		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (!pullup)
			return -EINVAL;

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!pulldown)
			return -EINVAL;

		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (is_soc_or_above(jzpc, ID_X2000))
			reg = X2000_GPIO_SMT;
		else if (is_soc_or_above(jzpc, ID_X1830))
			reg = X1830_GPIO_SMT;
		else
			return -EINVAL;

		arg = !!ingenic_get_pin_config(jzpc, pin, reg);
		break;

	case PIN_CONFIG_SLEW_RATE:
		if (is_soc_or_above(jzpc, ID_X2000))
			reg = X2000_GPIO_SR;
		else if (is_soc_or_above(jzpc, ID_X1830))
			reg = X1830_GPIO_SR;
		else
			return -EINVAL;

		arg = !!ingenic_get_pin_config(jzpc, pin, reg);
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static void ingenic_set_bias(struct ingenic_pinctrl *jzpc,
		unsigned int pin, unsigned int bias)
{
	if (is_soc_or_above(jzpc, ID_X2000)) {
		switch (bias) {
		case GPIO_PULL_UP:
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPD, false);
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPU, true);
			break;

		case GPIO_PULL_DOWN:
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPU, false);
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPD, true);
			break;

		case GPIO_PULL_DIS:
		default:
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPU, false);
			ingenic_config_pin(jzpc, pin, X2000_GPIO_PEPD, false);
		}

	} else if (is_soc_or_above(jzpc, ID_X1830)) {
		unsigned int idx = pin % PINS_PER_GPIO_CHIP;
		unsigned int half = PINS_PER_GPIO_CHIP / 2;
		unsigned int idxh = (pin % half) * 2;
		unsigned int offt = pin / PINS_PER_GPIO_CHIP;

		if (idx < half) {
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_CLEAR(X1830_GPIO_PEL), 3 << idxh);
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_SET(X1830_GPIO_PEL), bias << idxh);
		} else {
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_CLEAR(X1830_GPIO_PEH), 3 << idxh);
			regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
					REG_SET(X1830_GPIO_PEH), bias << idxh);
		}

	} else if (is_soc_or_above(jzpc, ID_JZ4770)) {
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_PEN, !bias);
	} else if (is_soc_or_above(jzpc, ID_JZ4740)) {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_PULL_DIS, !bias);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4730_GPIO_GPPUR, bias);
	}
}

static void ingenic_set_schmitt_trigger(struct ingenic_pinctrl *jzpc,
		unsigned int pin, bool enable)
{
	if (is_soc_or_above(jzpc, ID_X2000))
		ingenic_config_pin(jzpc, pin, X2000_GPIO_SMT, enable);
	else
		ingenic_config_pin(jzpc, pin, X1830_GPIO_SMT, enable);
}

static void ingenic_set_output_level(struct ingenic_pinctrl *jzpc,
				     unsigned int pin, bool high)
{
	if (is_soc_or_above(jzpc, ID_JZ4770))
		ingenic_config_pin(jzpc, pin, JZ4770_GPIO_PAT0, high);
	else if (is_soc_or_above(jzpc, ID_JZ4740))
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_DATA, high);
	else
		ingenic_config_pin(jzpc, pin, JZ4730_GPIO_DATA, high);
}

static void ingenic_set_slew_rate(struct ingenic_pinctrl *jzpc,
		unsigned int pin, unsigned int slew)
{
	if (is_soc_or_above(jzpc, ID_X2000))
		ingenic_config_pin(jzpc, pin, X2000_GPIO_SR, slew);
	else
		ingenic_config_pin(jzpc, pin, X1830_GPIO_SR, slew);
}

static int ingenic_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
		unsigned long *configs, unsigned int num_configs)
{
	struct ingenic_pinctrl *jzpc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;
	unsigned int cfg, arg;
	int ret;

	for (cfg = 0; cfg < num_configs; cfg++) {
		switch (pinconf_to_config_param(configs[cfg])) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		case PIN_CONFIG_OUTPUT:
		case PIN_CONFIG_SLEW_RATE:
			continue;
		default:
			return -ENOTSUPP;
		}
	}

	for (cfg = 0; cfg < num_configs; cfg++) {
		arg = pinconf_to_config_argument(configs[cfg]);

		switch (pinconf_to_config_param(configs[cfg])) {
		case PIN_CONFIG_BIAS_DISABLE:
			dev_dbg(jzpc->dev, "disable pull-over for pin P%c%u\n",
					'A' + offt, idx);
			ingenic_set_bias(jzpc, pin, GPIO_PULL_DIS);
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			if (!(jzpc->info->pull_ups[offt] & BIT(idx)))
				return -EINVAL;
			dev_dbg(jzpc->dev, "set pull-up for pin P%c%u\n",
					'A' + offt, idx);
			ingenic_set_bias(jzpc, pin, GPIO_PULL_UP);
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!(jzpc->info->pull_downs[offt] & BIT(idx)))
				return -EINVAL;
			dev_dbg(jzpc->dev, "set pull-down for pin P%c%u\n",
					'A' + offt, idx);
			ingenic_set_bias(jzpc, pin, GPIO_PULL_DOWN);
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (!is_soc_or_above(jzpc, ID_X1830))
				return -EINVAL;

			ingenic_set_schmitt_trigger(jzpc, pin, arg);
			break;

		case PIN_CONFIG_OUTPUT:
			ret = pinctrl_gpio_direction_output(pin);
			if (ret)
				return ret;

			ingenic_set_output_level(jzpc, pin, arg);
			break;

		case PIN_CONFIG_SLEW_RATE:
			if (!is_soc_or_above(jzpc, ID_X1830))
				return -EINVAL;

			ingenic_set_slew_rate(jzpc, pin, arg);
			break;

		default:
			/* unreachable */
			break;
		}
	}

	return 0;
}

static int ingenic_pinconf_group_get(struct pinctrl_dev *pctldev,
		unsigned int group, unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, old = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		if (ingenic_pinconf_get(pctldev, pins[i], config))
			return -ENOTSUPP;

		/* configs do not match between two pins */
		if (i && (old != *config))
			return -ENOTSUPP;

		old = *config;
	}

	return 0;
}

static int ingenic_pinconf_group_set(struct pinctrl_dev *pctldev,
		unsigned int group, unsigned long *configs,
		unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = ingenic_pinconf_set(pctldev,
				pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops ingenic_confops = {
	.is_generic = true,
	.pin_config_get = ingenic_pinconf_get,
	.pin_config_set = ingenic_pinconf_set,
	.pin_config_group_get = ingenic_pinconf_group_get,
	.pin_config_group_set = ingenic_pinconf_group_set,
};

static const struct regmap_config ingenic_pinctrl_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const struct of_device_id ingenic_gpio_of_matches[] __initconst = {
	{ .compatible = "ingenic,jz4730-gpio" },
	{ .compatible = "ingenic,jz4740-gpio" },
	{ .compatible = "ingenic,jz4725b-gpio" },
	{ .compatible = "ingenic,jz4750-gpio" },
	{ .compatible = "ingenic,jz4755-gpio" },
	{ .compatible = "ingenic,jz4760-gpio" },
	{ .compatible = "ingenic,jz4770-gpio" },
	{ .compatible = "ingenic,jz4775-gpio" },
	{ .compatible = "ingenic,jz4780-gpio" },
	{ .compatible = "ingenic,x1000-gpio" },
	{ .compatible = "ingenic,x1830-gpio" },
	{ .compatible = "ingenic,x2000-gpio" },
	{ .compatible = "ingenic,x2100-gpio" },
	{},
};

static int __init ingenic_gpio_probe(struct ingenic_pinctrl *jzpc,
				     struct device_node *node)
{
	struct ingenic_gpio_chip *jzgc;
	struct device *dev = jzpc->dev;
	struct gpio_irq_chip *girq;
	unsigned int bank;
	int err;

	err = of_property_read_u32(node, "reg", &bank);
	if (err) {
		dev_err(dev, "Cannot read \"reg\" property: %i\n", err);
		return err;
	}

	jzgc = devm_kzalloc(dev, sizeof(*jzgc), GFP_KERNEL);
	if (!jzgc)
		return -ENOMEM;

	jzgc->jzpc = jzpc;
	jzgc->reg_base = bank * jzpc->info->reg_offset;

	jzgc->gc.label = devm_kasprintf(dev, GFP_KERNEL, "GPIO%c", 'A' + bank);
	if (!jzgc->gc.label)
		return -ENOMEM;

	/* DO NOT EXPAND THIS: FOR BACKWARD GPIO NUMBERSPACE COMPATIBIBILITY
	 * ONLY: WORK TO TRANSITION CONSUMERS TO USE THE GPIO DESCRIPTOR API IN
	 * <linux/gpio/consumer.h> INSTEAD.
	 */
	jzgc->gc.base = bank * 32;

	jzgc->gc.ngpio = 32;
	jzgc->gc.parent = dev;
	jzgc->gc.of_node = node;
	jzgc->gc.owner = THIS_MODULE;

	jzgc->gc.set = ingenic_gpio_set;
	jzgc->gc.get = ingenic_gpio_get;
	jzgc->gc.direction_input = ingenic_gpio_direction_input;
	jzgc->gc.direction_output = ingenic_gpio_direction_output;
	jzgc->gc.get_direction = ingenic_gpio_get_direction;
	jzgc->gc.request = gpiochip_generic_request;
	jzgc->gc.free = gpiochip_generic_free;

	jzgc->irq = irq_of_parse_and_map(node, 0);
	if (!jzgc->irq)
		return -EINVAL;

	girq = &jzgc->gc.irq;
	gpio_irq_chip_set_chip(girq, &ingenic_gpio_irqchip);
	girq->parent_handler = ingenic_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;

	girq->parents[0] = jzgc->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	err = devm_gpiochip_add_data(dev, &jzgc->gc, jzgc);
	if (err)
		return err;

	return 0;
}

static int __init ingenic_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ingenic_pinctrl *jzpc;
	struct pinctrl_desc *pctl_desc;
	void __iomem *base;
	const struct ingenic_chip_info *chip_info;
	struct device_node *node;
	struct regmap_config regmap_config;
	unsigned int i;
	int err;

	chip_info = of_device_get_match_data(dev);
	if (!chip_info) {
		dev_err(dev, "Unsupported SoC\n");
		return -EINVAL;
	}

	jzpc = devm_kzalloc(dev, sizeof(*jzpc), GFP_KERNEL);
	if (!jzpc)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap_config = ingenic_pinctrl_regmap_config;
	if (chip_info->access_table) {
		regmap_config.rd_table = chip_info->access_table;
		regmap_config.wr_table = chip_info->access_table;
	} else {
		regmap_config.max_register = chip_info->num_chips * chip_info->reg_offset - 4;
	}

	jzpc->map = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(jzpc->map)) {
		dev_err(dev, "Failed to create regmap\n");
		return PTR_ERR(jzpc->map);
	}

	jzpc->dev = dev;
	jzpc->info = chip_info;

	pctl_desc = devm_kzalloc(&pdev->dev, sizeof(*pctl_desc), GFP_KERNEL);
	if (!pctl_desc)
		return -ENOMEM;

	/* fill in pinctrl_desc structure */
	pctl_desc->name = dev_name(dev);
	pctl_desc->owner = THIS_MODULE;
	pctl_desc->pctlops = &ingenic_pctlops;
	pctl_desc->pmxops = &ingenic_pmxops;
	pctl_desc->confops = &ingenic_confops;
	pctl_desc->npins = chip_info->num_chips * PINS_PER_GPIO_CHIP;
	pctl_desc->pins = jzpc->pdesc = devm_kcalloc(&pdev->dev,
			pctl_desc->npins, sizeof(*jzpc->pdesc), GFP_KERNEL);
	if (!jzpc->pdesc)
		return -ENOMEM;

	for (i = 0; i < pctl_desc->npins; i++) {
		jzpc->pdesc[i].number = i;
		jzpc->pdesc[i].name = kasprintf(GFP_KERNEL, "P%c%d",
						'A' + (i / PINS_PER_GPIO_CHIP),
						i % PINS_PER_GPIO_CHIP);
	}

	jzpc->pctl = devm_pinctrl_register(dev, pctl_desc, jzpc);
	if (IS_ERR(jzpc->pctl)) {
		dev_err(dev, "Failed to register pinctrl\n");
		return PTR_ERR(jzpc->pctl);
	}

	for (i = 0; i < chip_info->num_groups; i++) {
		const struct group_desc *group = &chip_info->groups[i];

		err = pinctrl_generic_add_group(jzpc->pctl, group->name,
				group->pins, group->num_pins, group->data);
		if (err < 0) {
			dev_err(dev, "Failed to register group %s\n",
					group->name);
			return err;
		}
	}

	for (i = 0; i < chip_info->num_functions; i++) {
		const struct function_desc *func = &chip_info->functions[i];

		err = pinmux_generic_add_function(jzpc->pctl, func->name,
				func->group_names, func->num_group_names,
				func->data);
		if (err < 0) {
			dev_err(dev, "Failed to register function %s\n",
					func->name);
			return err;
		}
	}

	dev_set_drvdata(dev, jzpc->map);

	for_each_child_of_node(dev->of_node, node) {
		if (of_match_node(ingenic_gpio_of_matches, node)) {
			err = ingenic_gpio_probe(jzpc, node);
			if (err) {
				of_node_put(node);
				return err;
			}
		}
	}

	return 0;
}

#define IF_ENABLED(cfg, ptr)	PTR_IF(IS_ENABLED(cfg), (ptr))

static const struct of_device_id ingenic_pinctrl_of_matches[] = {
	{
		.compatible = "ingenic,jz4730-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4730, &jz4730_chip_info)
	},
	{
		.compatible = "ingenic,jz4740-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4740, &jz4740_chip_info)
	},
	{
		.compatible = "ingenic,jz4725b-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4725B, &jz4725b_chip_info)
	},
	{
		.compatible = "ingenic,jz4750-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4750, &jz4750_chip_info)
	},
	{
		.compatible = "ingenic,jz4755-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4755, &jz4755_chip_info)
	},
	{
		.compatible = "ingenic,jz4760-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4760, &jz4760_chip_info)
	},
	{
		.compatible = "ingenic,jz4760b-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4760, &jz4760_chip_info)
	},
	{
		.compatible = "ingenic,jz4770-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4770, &jz4770_chip_info)
	},
	{
		.compatible = "ingenic,jz4775-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4775, &jz4775_chip_info)
	},
	{
		.compatible = "ingenic,jz4780-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_JZ4780, &jz4780_chip_info)
	},
	{
		.compatible = "ingenic,x1000-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X1000, &x1000_chip_info)
	},
	{
		.compatible = "ingenic,x1000e-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X1000, &x1000_chip_info)
	},
	{
		.compatible = "ingenic,x1500-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X1500, &x1500_chip_info)
	},
	{
		.compatible = "ingenic,x1830-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X1830, &x1830_chip_info)
	},
	{
		.compatible = "ingenic,x2000-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X2000, &x2000_chip_info)
	},
	{
		.compatible = "ingenic,x2000e-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X2000, &x2000_chip_info)
	},
	{
		.compatible = "ingenic,x2100-pinctrl",
		.data = IF_ENABLED(CONFIG_MACH_X2100, &x2100_chip_info)
	},
	{ /* sentinel */ },
};

static struct platform_driver ingenic_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-ingenic",
		.of_match_table = ingenic_pinctrl_of_matches,
	},
};

static int __init ingenic_pinctrl_drv_register(void)
{
	return platform_driver_probe(&ingenic_pinctrl_driver,
				     ingenic_pinctrl_probe);
}
subsys_initcall(ingenic_pinctrl_drv_register);
