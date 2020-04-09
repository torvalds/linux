// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ingenic SoCs pinctrl driver
 *
 * Copyright (c) 2017 Paul Cercueil <paul@crapouillou.net>
 * Copyright (c) 2019 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 * Copyright (c) 2017, 2019 Paul Boddie <paul@boddie.org.uk>
 */

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"

#define GPIO_PIN	0x00
#define GPIO_MSK	0x20

#define JZ4740_GPIO_DATA	0x10
#define JZ4740_GPIO_PULL_DIS	0x30
#define JZ4740_GPIO_FUNC	0x40
#define JZ4740_GPIO_SELECT	0x50
#define JZ4740_GPIO_DIR		0x60
#define JZ4740_GPIO_TRIG	0x70
#define JZ4740_GPIO_FLAG	0x80

#define JZ4760_GPIO_INT		0x10
#define JZ4760_GPIO_PAT1	0x30
#define JZ4760_GPIO_PAT0	0x40
#define JZ4760_GPIO_FLAG	0x50
#define JZ4760_GPIO_PEN		0x70

#define X1830_GPIO_PEL			0x110
#define X1830_GPIO_PEH			0x120

#define REG_SET(x) ((x) + 0x4)
#define REG_CLEAR(x) ((x) + 0x8)

#define REG_PZ_BASE(x) ((x) * 7)
#define REG_PZ_GID2LD(x) ((x) * 7 + 0xf0)

#define GPIO_PULL_DIS	0
#define GPIO_PULL_UP	1
#define GPIO_PULL_DOWN	2

#define PINS_PER_GPIO_CHIP 32

enum jz_version {
	ID_JZ4740,
	ID_JZ4725B,
	ID_JZ4760,
	ID_JZ4770,
	ID_JZ4780,
	ID_X1000,
	ID_X1500,
	ID_X1830,
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
	struct irq_chip irq_chip;
	unsigned int irq, reg_base;
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
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x52, 0x53, 0x54,
};
static int jz4740_lcd_16bit_pins[] = {
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x55,
};
static int jz4740_lcd_18bit_pins[] = { 0x50, 0x51, };
static int jz4740_lcd_18bit_tft_pins[] = { 0x56, 0x57, 0x31, 0x32, };
static int jz4740_nand_cs1_pins[] = { 0x39, };
static int jz4740_nand_cs2_pins[] = { 0x3a, };
static int jz4740_nand_cs3_pins[] = { 0x3b, };
static int jz4740_nand_cs4_pins[] = { 0x3c, };
static int jz4740_pwm_pwm0_pins[] = { 0x77, };
static int jz4740_pwm_pwm1_pins[] = { 0x78, };
static int jz4740_pwm_pwm2_pins[] = { 0x79, };
static int jz4740_pwm_pwm3_pins[] = { 0x7a, };
static int jz4740_pwm_pwm4_pins[] = { 0x7b, };
static int jz4740_pwm_pwm5_pins[] = { 0x7c, };
static int jz4740_pwm_pwm6_pins[] = { 0x7e, };
static int jz4740_pwm_pwm7_pins[] = { 0x7f, };

static int jz4740_mmc_1bit_funcs[] = { 0, 0, 0, };
static int jz4740_mmc_4bit_funcs[] = { 0, 0, 0, };
static int jz4740_uart0_data_funcs[] = { 1, 1, };
static int jz4740_uart0_hwflow_funcs[] = { 1, 1, };
static int jz4740_uart1_data_funcs[] = { 2, 2, };
static int jz4740_lcd_8bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4740_lcd_16bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4740_lcd_18bit_funcs[] = { 0, 0, };
static int jz4740_lcd_18bit_tft_funcs[] = { 0, 0, 0, 0, };
static int jz4740_nand_cs1_funcs[] = { 0, };
static int jz4740_nand_cs2_funcs[] = { 0, };
static int jz4740_nand_cs3_funcs[] = { 0, };
static int jz4740_nand_cs4_funcs[] = { 0, };
static int jz4740_pwm_pwm0_funcs[] = { 0, };
static int jz4740_pwm_pwm1_funcs[] = { 0, };
static int jz4740_pwm_pwm2_funcs[] = { 0, };
static int jz4740_pwm_pwm3_funcs[] = { 0, };
static int jz4740_pwm_pwm4_funcs[] = { 0, };
static int jz4740_pwm_pwm5_funcs[] = { 0, };
static int jz4740_pwm_pwm6_funcs[] = { 0, };
static int jz4740_pwm_pwm7_funcs[] = { 0, };

#define INGENIC_PIN_GROUP(name, id)			\
	{						\
		name,					\
		id##_pins,				\
		ARRAY_SIZE(id##_pins),			\
		id##_funcs,				\
	}

static const struct group_desc jz4740_groups[] = {
	INGENIC_PIN_GROUP("mmc-1bit", jz4740_mmc_1bit),
	INGENIC_PIN_GROUP("mmc-4bit", jz4740_mmc_4bit),
	INGENIC_PIN_GROUP("uart0-data", jz4740_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4740_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data", jz4740_uart1_data),
	INGENIC_PIN_GROUP("lcd-8bit", jz4740_lcd_8bit),
	INGENIC_PIN_GROUP("lcd-16bit", jz4740_lcd_16bit),
	INGENIC_PIN_GROUP("lcd-18bit", jz4740_lcd_18bit),
	INGENIC_PIN_GROUP("lcd-18bit-tft", jz4740_lcd_18bit_tft),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("nand-cs1", jz4740_nand_cs1),
	INGENIC_PIN_GROUP("nand-cs2", jz4740_nand_cs2),
	INGENIC_PIN_GROUP("nand-cs3", jz4740_nand_cs3),
	INGENIC_PIN_GROUP("nand-cs4", jz4740_nand_cs4),
	INGENIC_PIN_GROUP("pwm0", jz4740_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", jz4740_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", jz4740_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", jz4740_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", jz4740_pwm_pwm4),
	INGENIC_PIN_GROUP("pwm5", jz4740_pwm_pwm5),
	INGENIC_PIN_GROUP("pwm6", jz4740_pwm_pwm6),
	INGENIC_PIN_GROUP("pwm7", jz4740_pwm_pwm7),
};

static const char *jz4740_mmc_groups[] = { "mmc-1bit", "mmc-4bit", };
static const char *jz4740_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4740_uart1_groups[] = { "uart1-data", };
static const char *jz4740_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-18bit-tft", "lcd-no-pins",
};
static const char *jz4740_nand_groups[] = {
	"nand-cs1", "nand-cs2", "nand-cs3", "nand-cs4",
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
static int jz4725b_lcd_8bit_pins[] = {
	0x72, 0x73, 0x74,
	0x60, 0x61, 0x62, 0x63,
	0x64, 0x65, 0x66, 0x67,
};
static int jz4725b_lcd_16bit_pins[] = {
	0x68, 0x69, 0x6a, 0x6b,
	0x6c, 0x6d, 0x6e, 0x6f,
};
static int jz4725b_lcd_18bit_pins[] = { 0x70, 0x71, };
static int jz4725b_lcd_24bit_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4725b_lcd_special_pins[] = { 0x76, 0x77, 0x78, 0x79, };
static int jz4725b_lcd_generic_pins[] = { 0x75, };

static int jz4725b_mmc0_1bit_funcs[] = { 1, 1, 1, };
static int jz4725b_mmc0_4bit_funcs[] = { 1, 0, 1, };
static int jz4725b_mmc1_1bit_funcs[] = { 0, 0, 0, };
static int jz4725b_mmc1_4bit_funcs[] = { 0, 0, 0, };
static int jz4725b_uart_data_funcs[] = { 1, 1, };
static int jz4725b_nand_cs1_funcs[] = { 0, };
static int jz4725b_nand_cs2_funcs[] = { 0, };
static int jz4725b_nand_cs3_funcs[] = { 0, };
static int jz4725b_nand_cs4_funcs[] = { 0, };
static int jz4725b_nand_cle_ale_funcs[] = { 0, 0, };
static int jz4725b_nand_fre_fwe_funcs[] = { 0, 0, };
static int jz4725b_pwm_pwm0_funcs[] = { 0, };
static int jz4725b_pwm_pwm1_funcs[] = { 0, };
static int jz4725b_pwm_pwm2_funcs[] = { 0, };
static int jz4725b_pwm_pwm3_funcs[] = { 0, };
static int jz4725b_pwm_pwm4_funcs[] = { 0, };
static int jz4725b_pwm_pwm5_funcs[] = { 0, };
static int jz4725b_lcd_8bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4725b_lcd_16bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4725b_lcd_18bit_funcs[] = { 0, 0, };
static int jz4725b_lcd_24bit_funcs[] = { 1, 1, 1, 1, };
static int jz4725b_lcd_special_funcs[] = { 0, 0, 0, 0, };
static int jz4725b_lcd_generic_funcs[] = { 0, };

static const struct group_desc jz4725b_groups[] = {
	INGENIC_PIN_GROUP("mmc0-1bit", jz4725b_mmc0_1bit),
	INGENIC_PIN_GROUP("mmc0-4bit", jz4725b_mmc0_4bit),
	INGENIC_PIN_GROUP("mmc1-1bit", jz4725b_mmc1_1bit),
	INGENIC_PIN_GROUP("mmc1-4bit", jz4725b_mmc1_4bit),
	INGENIC_PIN_GROUP("uart-data", jz4725b_uart_data),
	INGENIC_PIN_GROUP("nand-cs1", jz4725b_nand_cs1),
	INGENIC_PIN_GROUP("nand-cs2", jz4725b_nand_cs2),
	INGENIC_PIN_GROUP("nand-cs3", jz4725b_nand_cs3),
	INGENIC_PIN_GROUP("nand-cs4", jz4725b_nand_cs4),
	INGENIC_PIN_GROUP("nand-cle-ale", jz4725b_nand_cle_ale),
	INGENIC_PIN_GROUP("nand-fre-fwe", jz4725b_nand_fre_fwe),
	INGENIC_PIN_GROUP("pwm0", jz4725b_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", jz4725b_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", jz4725b_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", jz4725b_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", jz4725b_pwm_pwm4),
	INGENIC_PIN_GROUP("pwm5", jz4725b_pwm_pwm5),
	INGENIC_PIN_GROUP("lcd-8bit", jz4725b_lcd_8bit),
	INGENIC_PIN_GROUP("lcd-16bit", jz4725b_lcd_16bit),
	INGENIC_PIN_GROUP("lcd-18bit", jz4725b_lcd_18bit),
	INGENIC_PIN_GROUP("lcd-24bit", jz4725b_lcd_24bit),
	INGENIC_PIN_GROUP("lcd-special", jz4725b_lcd_special),
	INGENIC_PIN_GROUP("lcd-generic", jz4725b_lcd_generic),
};

static const char *jz4725b_mmc0_groups[] = { "mmc0-1bit", "mmc0-4bit", };
static const char *jz4725b_mmc1_groups[] = { "mmc1-1bit", "mmc1-4bit", };
static const char *jz4725b_uart_groups[] = { "uart-data", };
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
static const char *jz4725b_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-18bit", "lcd-24bit",
	"lcd-special", "lcd-generic",
};

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

static const u32 jz4760_pull_ups[6] = {
	0xffffffff, 0xfffcf3ff, 0xffffffff, 0xffffcfff, 0xfffffb7c, 0xfffff00f,
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
static int jz4760_lcd_24bit_pins[] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b,
};
static int jz4760_pwm_pwm0_pins[] = { 0x80, };
static int jz4760_pwm_pwm1_pins[] = { 0x81, };
static int jz4760_pwm_pwm2_pins[] = { 0x82, };
static int jz4760_pwm_pwm3_pins[] = { 0x83, };
static int jz4760_pwm_pwm4_pins[] = { 0x84, };
static int jz4760_pwm_pwm5_pins[] = { 0x85, };
static int jz4760_pwm_pwm6_pins[] = { 0x6a, };
static int jz4760_pwm_pwm7_pins[] = { 0x6b, };

static int jz4760_uart0_data_funcs[] = { 0, 0, };
static int jz4760_uart0_hwflow_funcs[] = { 0, 0, };
static int jz4760_uart1_data_funcs[] = { 0, 0, };
static int jz4760_uart1_hwflow_funcs[] = { 0, 0, };
static int jz4760_uart2_data_funcs[] = { 0, 0, };
static int jz4760_uart2_hwflow_funcs[] = { 0, 0, };
static int jz4760_uart3_data_funcs[] = { 0, 1, };
static int jz4760_uart3_hwflow_funcs[] = { 0, 0, };
static int jz4760_mmc0_1bit_a_funcs[] = { 1, 1, 0, };
static int jz4760_mmc0_4bit_a_funcs[] = { 1, 1, 1, };
static int jz4760_mmc0_1bit_e_funcs[] = { 0, 0, 0, };
static int jz4760_mmc0_4bit_e_funcs[] = { 0, 0, 0, };
static int jz4760_mmc0_8bit_e_funcs[] = { 0, 0, 0, 0, };
static int jz4760_mmc1_1bit_d_funcs[] = { 0, 0, 0, };
static int jz4760_mmc1_4bit_d_funcs[] = { 0, 0, 0, };
static int jz4760_mmc1_1bit_e_funcs[] = { 1, 1, 1, };
static int jz4760_mmc1_4bit_e_funcs[] = { 1, 1, 1, };
static int jz4760_mmc1_8bit_e_funcs[] = { 1, 1, 1, 1, };
static int jz4760_mmc2_1bit_b_funcs[] = { 0, 0, 0, };
static int jz4760_mmc2_4bit_b_funcs[] = { 0, 0, 0, };
static int jz4760_mmc2_1bit_e_funcs[] = { 2, 2, 2, };
static int jz4760_mmc2_4bit_e_funcs[] = { 2, 2, 2, };
static int jz4760_mmc2_8bit_e_funcs[] = { 2, 2, 2, 2, };
static int jz4760_nemc_8bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4760_nemc_16bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4760_nemc_cle_ale_funcs[] = { 0, 0, };
static int jz4760_nemc_addr_funcs[] = { 0, 0, 0, 0, };
static int jz4760_nemc_rd_we_funcs[] = { 0, 0, };
static int jz4760_nemc_frd_fwe_funcs[] = { 0, 0, };
static int jz4760_nemc_wait_funcs[] = { 0, };
static int jz4760_nemc_cs1_funcs[] = { 0, };
static int jz4760_nemc_cs2_funcs[] = { 0, };
static int jz4760_nemc_cs3_funcs[] = { 0, };
static int jz4760_nemc_cs4_funcs[] = { 0, };
static int jz4760_nemc_cs5_funcs[] = { 0, };
static int jz4760_nemc_cs6_funcs[] = { 0, };
static int jz4760_i2c0_funcs[] = { 0, 0, };
static int jz4760_i2c1_funcs[] = { 0, 0, };
static int jz4760_cim_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4760_lcd_24bit_funcs[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0,
};
static int jz4760_pwm_pwm0_funcs[] = { 0, };
static int jz4760_pwm_pwm1_funcs[] = { 0, };
static int jz4760_pwm_pwm2_funcs[] = { 0, };
static int jz4760_pwm_pwm3_funcs[] = { 0, };
static int jz4760_pwm_pwm4_funcs[] = { 0, };
static int jz4760_pwm_pwm5_funcs[] = { 0, };
static int jz4760_pwm_pwm6_funcs[] = { 0, };
static int jz4760_pwm_pwm7_funcs[] = { 0, };

static const struct group_desc jz4760_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4760_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4760_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data", jz4760_uart1_data),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4760_uart1_hwflow),
	INGENIC_PIN_GROUP("uart2-data", jz4760_uart2_data),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4760_uart2_hwflow),
	INGENIC_PIN_GROUP("uart3-data", jz4760_uart3_data),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4760_uart3_hwflow),
	INGENIC_PIN_GROUP("mmc0-1bit-a", jz4760_mmc0_1bit_a),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4760_mmc0_4bit_a),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4760_mmc0_1bit_e),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4760_mmc0_4bit_e),
	INGENIC_PIN_GROUP("mmc0-8bit-e", jz4760_mmc0_8bit_e),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4760_mmc1_1bit_d),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4760_mmc1_4bit_d),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4760_mmc1_1bit_e),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4760_mmc1_4bit_e),
	INGENIC_PIN_GROUP("mmc1-8bit-e", jz4760_mmc1_8bit_e),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4760_mmc2_1bit_b),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4760_mmc2_4bit_b),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4760_mmc2_1bit_e),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4760_mmc2_4bit_e),
	INGENIC_PIN_GROUP("mmc2-8bit-e", jz4760_mmc2_8bit_e),
	INGENIC_PIN_GROUP("nemc-8bit-data", jz4760_nemc_8bit_data),
	INGENIC_PIN_GROUP("nemc-16bit-data", jz4760_nemc_16bit_data),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4760_nemc_cle_ale),
	INGENIC_PIN_GROUP("nemc-addr", jz4760_nemc_addr),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4760_nemc_rd_we),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4760_nemc_frd_fwe),
	INGENIC_PIN_GROUP("nemc-wait", jz4760_nemc_wait),
	INGENIC_PIN_GROUP("nemc-cs1", jz4760_nemc_cs1),
	INGENIC_PIN_GROUP("nemc-cs2", jz4760_nemc_cs2),
	INGENIC_PIN_GROUP("nemc-cs3", jz4760_nemc_cs3),
	INGENIC_PIN_GROUP("nemc-cs4", jz4760_nemc_cs4),
	INGENIC_PIN_GROUP("nemc-cs5", jz4760_nemc_cs5),
	INGENIC_PIN_GROUP("nemc-cs6", jz4760_nemc_cs6),
	INGENIC_PIN_GROUP("i2c0-data", jz4760_i2c0),
	INGENIC_PIN_GROUP("i2c1-data", jz4760_i2c1),
	INGENIC_PIN_GROUP("cim-data", jz4760_cim),
	INGENIC_PIN_GROUP("lcd-24bit", jz4760_lcd_24bit),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0", jz4760_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", jz4760_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", jz4760_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", jz4760_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", jz4760_pwm_pwm4),
	INGENIC_PIN_GROUP("pwm5", jz4760_pwm_pwm5),
	INGENIC_PIN_GROUP("pwm6", jz4760_pwm_pwm6),
	INGENIC_PIN_GROUP("pwm7", jz4760_pwm_pwm7),
};

static const char *jz4760_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4760_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4760_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4760_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
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
static const char *jz4760_lcd_groups[] = { "lcd-24bit", "lcd-no-pins", };
static const char *jz4760_pwm0_groups[] = { "pwm0", };
static const char *jz4760_pwm1_groups[] = { "pwm1", };
static const char *jz4760_pwm2_groups[] = { "pwm2", };
static const char *jz4760_pwm3_groups[] = { "pwm3", };
static const char *jz4760_pwm4_groups[] = { "pwm4", };
static const char *jz4760_pwm5_groups[] = { "pwm5", };
static const char *jz4760_pwm6_groups[] = { "pwm6", };
static const char *jz4760_pwm7_groups[] = { "pwm7", };

static const struct function_desc jz4760_functions[] = {
	{ "uart0", jz4760_uart0_groups, ARRAY_SIZE(jz4760_uart0_groups), },
	{ "uart1", jz4760_uart1_groups, ARRAY_SIZE(jz4760_uart1_groups), },
	{ "uart2", jz4760_uart2_groups, ARRAY_SIZE(jz4760_uart2_groups), },
	{ "uart3", jz4760_uart3_groups, ARRAY_SIZE(jz4760_uart3_groups), },
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
	0x3fffffff, 0xfff0030c, 0xffffffff, 0xffff4fff, 0xfffffb7c, 0xffa7f00f,
};

static const u32 jz4770_pull_downs[6] = {
	0x00000000, 0x000f0c03, 0x00000000, 0x0000b000, 0x00000483, 0x00580ff0,
};

static int jz4770_uart0_data_pins[] = { 0xa0, 0xa3, };
static int jz4770_uart0_hwflow_pins[] = { 0xa1, 0xa2, };
static int jz4770_uart1_data_pins[] = { 0x7a, 0x7c, };
static int jz4770_uart1_hwflow_pins[] = { 0x7b, 0x7d, };
static int jz4770_uart2_data_pins[] = { 0x5c, 0x5e, };
static int jz4770_uart2_hwflow_pins[] = { 0x5d, 0x5f, };
static int jz4770_uart3_data_pins[] = { 0x6c, 0x85, };
static int jz4770_uart3_hwflow_pins[] = { 0x88, 0x89, };
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
static int jz4770_lcd_24bit_pins[] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b,
};
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
static int jz4770_mac_mii_pins[] = { 0xa7, 0xaf, };
static int jz4770_otg_pins[] = { 0x8a, };

static int jz4770_uart0_data_funcs[] = { 0, 0, };
static int jz4770_uart0_hwflow_funcs[] = { 0, 0, };
static int jz4770_uart1_data_funcs[] = { 0, 0, };
static int jz4770_uart1_hwflow_funcs[] = { 0, 0, };
static int jz4770_uart2_data_funcs[] = { 0, 0, };
static int jz4770_uart2_hwflow_funcs[] = { 0, 0, };
static int jz4770_uart3_data_funcs[] = { 0, 1, };
static int jz4770_uart3_hwflow_funcs[] = { 0, 0, };
static int jz4770_mmc0_1bit_a_funcs[] = { 1, 1, 0, };
static int jz4770_mmc0_4bit_a_funcs[] = { 1, 1, 1, };
static int jz4770_mmc0_1bit_e_funcs[] = { 0, 0, 0, };
static int jz4770_mmc0_4bit_e_funcs[] = { 0, 0, 0, };
static int jz4770_mmc0_8bit_e_funcs[] = { 0, 0, 0, 0, };
static int jz4770_mmc1_1bit_d_funcs[] = { 0, 0, 0, };
static int jz4770_mmc1_4bit_d_funcs[] = { 0, 0, 0, };
static int jz4770_mmc1_1bit_e_funcs[] = { 1, 1, 1, };
static int jz4770_mmc1_4bit_e_funcs[] = { 1, 1, 1, };
static int jz4770_mmc1_8bit_e_funcs[] = { 1, 1, 1, 1, };
static int jz4770_mmc2_1bit_b_funcs[] = { 0, 0, 0, };
static int jz4770_mmc2_4bit_b_funcs[] = { 0, 0, 0, };
static int jz4770_mmc2_1bit_e_funcs[] = { 2, 2, 2, };
static int jz4770_mmc2_4bit_e_funcs[] = { 2, 2, 2, };
static int jz4770_mmc2_8bit_e_funcs[] = { 2, 2, 2, 2, };
static int jz4770_nemc_8bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4770_nemc_16bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4770_nemc_cle_ale_funcs[] = { 0, 0, };
static int jz4770_nemc_addr_funcs[] = { 0, 0, 0, 0, };
static int jz4770_nemc_rd_we_funcs[] = { 0, 0, };
static int jz4770_nemc_frd_fwe_funcs[] = { 0, 0, };
static int jz4770_nemc_wait_funcs[] = { 0, };
static int jz4770_nemc_cs1_funcs[] = { 0, };
static int jz4770_nemc_cs2_funcs[] = { 0, };
static int jz4770_nemc_cs3_funcs[] = { 0, };
static int jz4770_nemc_cs4_funcs[] = { 0, };
static int jz4770_nemc_cs5_funcs[] = { 0, };
static int jz4770_nemc_cs6_funcs[] = { 0, };
static int jz4770_i2c0_funcs[] = { 0, 0, };
static int jz4770_i2c1_funcs[] = { 0, 0, };
static int jz4770_i2c2_funcs[] = { 2, 2, };
static int jz4770_cim_8bit_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4770_cim_12bit_funcs[] = { 0, 0, 0, 0, };
static int jz4770_lcd_24bit_funcs[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0,
};
static int jz4770_pwm_pwm0_funcs[] = { 0, };
static int jz4770_pwm_pwm1_funcs[] = { 0, };
static int jz4770_pwm_pwm2_funcs[] = { 0, };
static int jz4770_pwm_pwm3_funcs[] = { 0, };
static int jz4770_pwm_pwm4_funcs[] = { 0, };
static int jz4770_pwm_pwm5_funcs[] = { 0, };
static int jz4770_pwm_pwm6_funcs[] = { 0, };
static int jz4770_pwm_pwm7_funcs[] = { 0, };
static int jz4770_mac_rmii_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int jz4770_mac_mii_funcs[] = { 0, 0, };
static int jz4770_otg_funcs[] = { 0, };

static const struct group_desc jz4770_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4770_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4770_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data", jz4770_uart1_data),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4770_uart1_hwflow),
	INGENIC_PIN_GROUP("uart2-data", jz4770_uart2_data),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4770_uart2_hwflow),
	INGENIC_PIN_GROUP("uart3-data", jz4770_uart3_data),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4770_uart3_hwflow),
	INGENIC_PIN_GROUP("mmc0-1bit-a", jz4770_mmc0_1bit_a),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4770_mmc0_4bit_a),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4770_mmc0_1bit_e),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4770_mmc0_4bit_e),
	INGENIC_PIN_GROUP("mmc0-8bit-e", jz4770_mmc0_8bit_e),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4770_mmc1_1bit_d),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4770_mmc1_4bit_d),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4770_mmc1_1bit_e),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4770_mmc1_4bit_e),
	INGENIC_PIN_GROUP("mmc1-8bit-e", jz4770_mmc1_8bit_e),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4770_mmc2_1bit_b),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4770_mmc2_4bit_b),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4770_mmc2_1bit_e),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4770_mmc2_4bit_e),
	INGENIC_PIN_GROUP("mmc2-8bit-e", jz4770_mmc2_8bit_e),
	INGENIC_PIN_GROUP("nemc-8bit-data", jz4770_nemc_8bit_data),
	INGENIC_PIN_GROUP("nemc-16bit-data", jz4770_nemc_16bit_data),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4770_nemc_cle_ale),
	INGENIC_PIN_GROUP("nemc-addr", jz4770_nemc_addr),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4770_nemc_rd_we),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4770_nemc_frd_fwe),
	INGENIC_PIN_GROUP("nemc-wait", jz4770_nemc_wait),
	INGENIC_PIN_GROUP("nemc-cs1", jz4770_nemc_cs1),
	INGENIC_PIN_GROUP("nemc-cs2", jz4770_nemc_cs2),
	INGENIC_PIN_GROUP("nemc-cs3", jz4770_nemc_cs3),
	INGENIC_PIN_GROUP("nemc-cs4", jz4770_nemc_cs4),
	INGENIC_PIN_GROUP("nemc-cs5", jz4770_nemc_cs5),
	INGENIC_PIN_GROUP("nemc-cs6", jz4770_nemc_cs6),
	INGENIC_PIN_GROUP("i2c0-data", jz4770_i2c0),
	INGENIC_PIN_GROUP("i2c1-data", jz4770_i2c1),
	INGENIC_PIN_GROUP("i2c2-data", jz4770_i2c2),
	INGENIC_PIN_GROUP("cim-data-8bit", jz4770_cim_8bit),
	INGENIC_PIN_GROUP("cim-data-12bit", jz4770_cim_12bit),
	INGENIC_PIN_GROUP("lcd-24bit", jz4770_lcd_24bit),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0", jz4770_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", jz4770_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", jz4770_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", jz4770_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", jz4770_pwm_pwm4),
	INGENIC_PIN_GROUP("pwm5", jz4770_pwm_pwm5),
	INGENIC_PIN_GROUP("pwm6", jz4770_pwm_pwm6),
	INGENIC_PIN_GROUP("pwm7", jz4770_pwm_pwm7),
	INGENIC_PIN_GROUP("mac-rmii", jz4770_mac_rmii),
	INGENIC_PIN_GROUP("mac-mii", jz4770_mac_mii),
	INGENIC_PIN_GROUP("otg-vbus", jz4770_otg),
};

static const char *jz4770_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *jz4770_uart1_groups[] = { "uart1-data", "uart1-hwflow", };
static const char *jz4770_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4770_uart3_groups[] = { "uart3-data", "uart3-hwflow", };
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
static const char *jz4770_lcd_groups[] = { "lcd-24bit", "lcd-no-pins", };
static const char *jz4770_pwm0_groups[] = { "pwm0", };
static const char *jz4770_pwm1_groups[] = { "pwm1", };
static const char *jz4770_pwm2_groups[] = { "pwm2", };
static const char *jz4770_pwm3_groups[] = { "pwm3", };
static const char *jz4770_pwm4_groups[] = { "pwm4", };
static const char *jz4770_pwm5_groups[] = { "pwm5", };
static const char *jz4770_pwm6_groups[] = { "pwm6", };
static const char *jz4770_pwm7_groups[] = { "pwm7", };
static const char *jz4770_mac_groups[] = { "mac-rmii", "mac-mii", };
static const char *jz4770_otg_groups[] = { "otg-vbus", };

static const struct function_desc jz4770_functions[] = {
	{ "uart0", jz4770_uart0_groups, ARRAY_SIZE(jz4770_uart0_groups), },
	{ "uart1", jz4770_uart1_groups, ARRAY_SIZE(jz4770_uart1_groups), },
	{ "uart2", jz4770_uart2_groups, ARRAY_SIZE(jz4770_uart2_groups), },
	{ "uart3", jz4770_uart3_groups, ARRAY_SIZE(jz4770_uart3_groups), },
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
	{ "otg", jz4770_otg_groups, ARRAY_SIZE(jz4770_otg_groups), },
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

static int jz4780_uart2_data_pins[] = { 0x66, 0x67, };
static int jz4780_uart2_hwflow_pins[] = { 0x65, 0x64, };
static int jz4780_uart4_data_pins[] = { 0x54, 0x4a, };
static int jz4780_mmc0_8bit_a_pins[] = { 0x04, 0x05, 0x06, 0x07, 0x18, };
static int jz4780_i2c3_pins[] = { 0x6a, 0x6b, };
static int jz4780_i2c4_e_pins[] = { 0x8c, 0x8d, };
static int jz4780_i2c4_f_pins[] = { 0xb9, 0xb8, };
static int jz4780_hdmi_ddc_pins[] = { 0xb9, 0xb8, };

static int jz4780_uart2_data_funcs[] = { 1, 1, };
static int jz4780_uart2_hwflow_funcs[] = { 1, 1, };
static int jz4780_uart4_data_funcs[] = { 2, 2, };
static int jz4780_mmc0_8bit_a_funcs[] = { 1, 1, 1, 1, 1, };
static int jz4780_i2c3_funcs[] = { 1, 1, };
static int jz4780_i2c4_e_funcs[] = { 1, 1, };
static int jz4780_i2c4_f_funcs[] = { 1, 1, };
static int jz4780_hdmi_ddc_funcs[] = { 0, 0, };

static const struct group_desc jz4780_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", jz4770_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", jz4770_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data", jz4770_uart1_data),
	INGENIC_PIN_GROUP("uart1-hwflow", jz4770_uart1_hwflow),
	INGENIC_PIN_GROUP("uart2-data", jz4780_uart2_data),
	INGENIC_PIN_GROUP("uart2-hwflow", jz4780_uart2_hwflow),
	INGENIC_PIN_GROUP("uart3-data", jz4770_uart3_data),
	INGENIC_PIN_GROUP("uart3-hwflow", jz4770_uart3_hwflow),
	INGENIC_PIN_GROUP("uart4-data", jz4780_uart4_data),
	INGENIC_PIN_GROUP("mmc0-1bit-a", jz4770_mmc0_1bit_a),
	INGENIC_PIN_GROUP("mmc0-4bit-a", jz4770_mmc0_4bit_a),
	INGENIC_PIN_GROUP("mmc0-8bit-a", jz4780_mmc0_8bit_a),
	INGENIC_PIN_GROUP("mmc0-1bit-e", jz4770_mmc0_1bit_e),
	INGENIC_PIN_GROUP("mmc0-4bit-e", jz4770_mmc0_4bit_e),
	INGENIC_PIN_GROUP("mmc1-1bit-d", jz4770_mmc1_1bit_d),
	INGENIC_PIN_GROUP("mmc1-4bit-d", jz4770_mmc1_4bit_d),
	INGENIC_PIN_GROUP("mmc1-1bit-e", jz4770_mmc1_1bit_e),
	INGENIC_PIN_GROUP("mmc1-4bit-e", jz4770_mmc1_4bit_e),
	INGENIC_PIN_GROUP("mmc2-1bit-b", jz4770_mmc2_1bit_b),
	INGENIC_PIN_GROUP("mmc2-4bit-b", jz4770_mmc2_4bit_b),
	INGENIC_PIN_GROUP("mmc2-1bit-e", jz4770_mmc2_1bit_e),
	INGENIC_PIN_GROUP("mmc2-4bit-e", jz4770_mmc2_4bit_e),
	INGENIC_PIN_GROUP("nemc-data", jz4770_nemc_8bit_data),
	INGENIC_PIN_GROUP("nemc-cle-ale", jz4770_nemc_cle_ale),
	INGENIC_PIN_GROUP("nemc-addr", jz4770_nemc_addr),
	INGENIC_PIN_GROUP("nemc-rd-we", jz4770_nemc_rd_we),
	INGENIC_PIN_GROUP("nemc-frd-fwe", jz4770_nemc_frd_fwe),
	INGENIC_PIN_GROUP("nemc-wait", jz4770_nemc_wait),
	INGENIC_PIN_GROUP("nemc-cs1", jz4770_nemc_cs1),
	INGENIC_PIN_GROUP("nemc-cs2", jz4770_nemc_cs2),
	INGENIC_PIN_GROUP("nemc-cs3", jz4770_nemc_cs3),
	INGENIC_PIN_GROUP("nemc-cs4", jz4770_nemc_cs4),
	INGENIC_PIN_GROUP("nemc-cs5", jz4770_nemc_cs5),
	INGENIC_PIN_GROUP("nemc-cs6", jz4770_nemc_cs6),
	INGENIC_PIN_GROUP("i2c0-data", jz4770_i2c0),
	INGENIC_PIN_GROUP("i2c1-data", jz4770_i2c1),
	INGENIC_PIN_GROUP("i2c2-data", jz4770_i2c2),
	INGENIC_PIN_GROUP("i2c3-data", jz4780_i2c3),
	INGENIC_PIN_GROUP("i2c4-data-e", jz4780_i2c4_e),
	INGENIC_PIN_GROUP("i2c4-data-f", jz4780_i2c4_f),
	INGENIC_PIN_GROUP("hdmi-ddc", jz4780_hdmi_ddc),
	INGENIC_PIN_GROUP("cim-data", jz4770_cim_8bit),
	INGENIC_PIN_GROUP("lcd-24bit", jz4770_lcd_24bit),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0", jz4770_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", jz4770_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", jz4770_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", jz4770_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", jz4770_pwm_pwm4),
	INGENIC_PIN_GROUP("pwm5", jz4770_pwm_pwm5),
	INGENIC_PIN_GROUP("pwm6", jz4770_pwm_pwm6),
	INGENIC_PIN_GROUP("pwm7", jz4770_pwm_pwm7),
};

static const char *jz4780_uart2_groups[] = { "uart2-data", "uart2-hwflow", };
static const char *jz4780_uart4_groups[] = { "uart4-data", };
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
static const char *jz4780_cim_groups[] = { "cim-data", };
static const char *jz4780_hdmi_ddc_groups[] = { "hdmi-ddc", };

static const struct function_desc jz4780_functions[] = {
	{ "uart0", jz4770_uart0_groups, ARRAY_SIZE(jz4770_uart0_groups), },
	{ "uart1", jz4770_uart1_groups, ARRAY_SIZE(jz4770_uart1_groups), },
	{ "uart2", jz4780_uart2_groups, ARRAY_SIZE(jz4780_uart2_groups), },
	{ "uart3", jz4770_uart3_groups, ARRAY_SIZE(jz4770_uart3_groups), },
	{ "uart4", jz4780_uart4_groups, ARRAY_SIZE(jz4780_uart4_groups), },
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
	.pull_ups = jz4770_pull_ups,
	.pull_downs = jz4770_pull_downs,
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
static int x1000_sfc_pins[] = { 0x1d, 0x1c, 0x1e, 0x1f, 0x1a, 0x1b, };
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

static int x1000_uart0_data_funcs[] = { 0, 0, };
static int x1000_uart0_hwflow_funcs[] = { 0, 0, };
static int x1000_uart1_data_a_funcs[] = { 2, 2, };
static int x1000_uart1_data_d_funcs[] = { 1, 1, };
static int x1000_uart1_hwflow_funcs[] = { 1, 1, };
static int x1000_uart2_data_a_funcs[] = { 2, 2, };
static int x1000_uart2_data_d_funcs[] = { 0, 0, };
static int x1000_sfc_funcs[] = { 1, 1, 1, 1, 1, 1, };
static int x1000_ssi_dt_a_22_funcs[] = { 2, };
static int x1000_ssi_dt_a_29_funcs[] = { 2, };
static int x1000_ssi_dt_d_funcs[] = { 0, };
static int x1000_ssi_dr_a_23_funcs[] = { 2, };
static int x1000_ssi_dr_a_28_funcs[] = { 2, };
static int x1000_ssi_dr_d_funcs[] = { 0, };
static int x1000_ssi_clk_a_24_funcs[] = { 2, };
static int x1000_ssi_clk_a_26_funcs[] = { 2, };
static int x1000_ssi_clk_d_funcs[] = { 0, };
static int x1000_ssi_gpc_a_20_funcs[] = { 2, };
static int x1000_ssi_gpc_a_31_funcs[] = { 2, };
static int x1000_ssi_ce0_a_25_funcs[] = { 2, };
static int x1000_ssi_ce0_a_27_funcs[] = { 2, };
static int x1000_ssi_ce0_d_funcs[] = { 0, };
static int x1000_ssi_ce1_a_21_funcs[] = { 2, };
static int x1000_ssi_ce1_a_30_funcs[] = { 2, };
static int x1000_mmc0_1bit_funcs[] = { 1, 1, 1, };
static int x1000_mmc0_4bit_funcs[] = { 1, 1, 1, };
static int x1000_mmc0_8bit_funcs[] = { 1, 1, 1, 1, 1, };
static int x1000_mmc1_1bit_funcs[] = { 0, 0, 0, };
static int x1000_mmc1_4bit_funcs[] = { 0, 0, 0, };
static int x1000_emc_8bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int x1000_emc_16bit_data_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int x1000_emc_addr_funcs[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static int x1000_emc_rd_we_funcs[] = { 0, 0, };
static int x1000_emc_wait_funcs[] = { 0, };
static int x1000_emc_cs1_funcs[] = { 0, };
static int x1000_emc_cs2_funcs[] = { 0, };
static int x1000_i2c0_funcs[] = { 0, 0, };
static int x1000_i2c1_a_funcs[] = { 2, 2, };
static int x1000_i2c1_c_funcs[] = { 0, 0, };
static int x1000_i2c2_funcs[] = { 1, 1, };
static int x1000_cim_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, };
static int x1000_lcd_8bit_funcs[] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static int x1000_lcd_16bit_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, };
static int x1000_pwm_pwm0_funcs[] = { 0, };
static int x1000_pwm_pwm1_funcs[] = { 1, };
static int x1000_pwm_pwm2_funcs[] = { 1, };
static int x1000_pwm_pwm3_funcs[] = { 2, };
static int x1000_pwm_pwm4_funcs[] = { 0, };
static int x1000_mac_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, };

static const struct group_desc x1000_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1000_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", x1000_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data-a", x1000_uart1_data_a),
	INGENIC_PIN_GROUP("uart1-data-d", x1000_uart1_data_d),
	INGENIC_PIN_GROUP("uart1-hwflow", x1000_uart1_hwflow),
	INGENIC_PIN_GROUP("uart2-data-a", x1000_uart2_data_a),
	INGENIC_PIN_GROUP("uart2-data-d", x1000_uart2_data_d),
	INGENIC_PIN_GROUP("sfc", x1000_sfc),
	INGENIC_PIN_GROUP("ssi-dt-a-22", x1000_ssi_dt_a_22),
	INGENIC_PIN_GROUP("ssi-dt-a-29", x1000_ssi_dt_a_29),
	INGENIC_PIN_GROUP("ssi-dt-d", x1000_ssi_dt_d),
	INGENIC_PIN_GROUP("ssi-dr-a-23", x1000_ssi_dr_a_23),
	INGENIC_PIN_GROUP("ssi-dr-a-28", x1000_ssi_dr_a_28),
	INGENIC_PIN_GROUP("ssi-dr-d", x1000_ssi_dr_d),
	INGENIC_PIN_GROUP("ssi-clk-a-24", x1000_ssi_clk_a_24),
	INGENIC_PIN_GROUP("ssi-clk-a-26", x1000_ssi_clk_a_26),
	INGENIC_PIN_GROUP("ssi-clk-d", x1000_ssi_clk_d),
	INGENIC_PIN_GROUP("ssi-gpc-a-20", x1000_ssi_gpc_a_20),
	INGENIC_PIN_GROUP("ssi-gpc-a-31", x1000_ssi_gpc_a_31),
	INGENIC_PIN_GROUP("ssi-ce0-a-25", x1000_ssi_ce0_a_25),
	INGENIC_PIN_GROUP("ssi-ce0-a-27", x1000_ssi_ce0_a_27),
	INGENIC_PIN_GROUP("ssi-ce0-d", x1000_ssi_ce0_d),
	INGENIC_PIN_GROUP("ssi-ce1-a-21", x1000_ssi_ce1_a_21),
	INGENIC_PIN_GROUP("ssi-ce1-a-30", x1000_ssi_ce1_a_30),
	INGENIC_PIN_GROUP("mmc0-1bit", x1000_mmc0_1bit),
	INGENIC_PIN_GROUP("mmc0-4bit", x1000_mmc0_4bit),
	INGENIC_PIN_GROUP("mmc0-8bit", x1000_mmc0_8bit),
	INGENIC_PIN_GROUP("mmc1-1bit", x1000_mmc1_1bit),
	INGENIC_PIN_GROUP("mmc1-4bit", x1000_mmc1_4bit),
	INGENIC_PIN_GROUP("emc-8bit-data", x1000_emc_8bit_data),
	INGENIC_PIN_GROUP("emc-16bit-data", x1000_emc_16bit_data),
	INGENIC_PIN_GROUP("emc-addr", x1000_emc_addr),
	INGENIC_PIN_GROUP("emc-rd-we", x1000_emc_rd_we),
	INGENIC_PIN_GROUP("emc-wait", x1000_emc_wait),
	INGENIC_PIN_GROUP("emc-cs1", x1000_emc_cs1),
	INGENIC_PIN_GROUP("emc-cs2", x1000_emc_cs2),
	INGENIC_PIN_GROUP("i2c0-data", x1000_i2c0),
	INGENIC_PIN_GROUP("i2c1-data-a", x1000_i2c1_a),
	INGENIC_PIN_GROUP("i2c1-data-c", x1000_i2c1_c),
	INGENIC_PIN_GROUP("i2c2-data", x1000_i2c2),
	INGENIC_PIN_GROUP("cim-data", x1000_cim),
	INGENIC_PIN_GROUP("lcd-8bit", x1000_lcd_8bit),
	INGENIC_PIN_GROUP("lcd-16bit", x1000_lcd_16bit),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0", x1000_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", x1000_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", x1000_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", x1000_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", x1000_pwm_pwm4),
	INGENIC_PIN_GROUP("mac", x1000_mac),
};

static const char *x1000_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x1000_uart1_groups[] = {
	"uart1-data-a", "uart1-data-d", "uart1-hwflow",
};
static const char *x1000_uart2_groups[] = { "uart2-data-a", "uart2-data-d", };
static const char *x1000_sfc_groups[] = { "sfc", };
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
static const char *x1000_cim_groups[] = { "cim-data", };
static const char *x1000_lcd_groups[] = {
	"lcd-8bit", "lcd-16bit", "lcd-no-pins",
};
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
	{ "cim", x1000_cim_groups, ARRAY_SIZE(x1000_cim_groups), },
	{ "lcd", x1000_lcd_groups, ARRAY_SIZE(x1000_lcd_groups), },
	{ "pwm0", x1000_pwm0_groups, ARRAY_SIZE(x1000_pwm0_groups), },
	{ "pwm1", x1000_pwm1_groups, ARRAY_SIZE(x1000_pwm1_groups), },
	{ "pwm2", x1000_pwm2_groups, ARRAY_SIZE(x1000_pwm2_groups), },
	{ "pwm3", x1000_pwm3_groups, ARRAY_SIZE(x1000_pwm3_groups), },
	{ "pwm4", x1000_pwm4_groups, ARRAY_SIZE(x1000_pwm4_groups), },
	{ "mac", x1000_mac_groups, ARRAY_SIZE(x1000_mac_groups), },
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
static int x1500_cim_pins[] = {
	0x08, 0x09, 0x0a, 0x0b,
	0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d, 0x0c,
};
static int x1500_pwm_pwm0_pins[] = { 0x59, };
static int x1500_pwm_pwm1_pins[] = { 0x5a, };
static int x1500_pwm_pwm2_pins[] = { 0x5b, };
static int x1500_pwm_pwm3_pins[] = { 0x26, };
static int x1500_pwm_pwm4_pins[] = { 0x58, };

static int x1500_uart0_data_funcs[] = { 0, 0, };
static int x1500_uart0_hwflow_funcs[] = { 0, 0, };
static int x1500_uart1_data_a_funcs[] = { 2, 2, };
static int x1500_uart1_data_d_funcs[] = { 1, 1, };
static int x1500_uart1_hwflow_funcs[] = { 1, 1, };
static int x1500_uart2_data_a_funcs[] = { 2, 2, };
static int x1500_uart2_data_d_funcs[] = { 0, 0, };
static int x1500_mmc_1bit_funcs[] = { 1, 1, 1, };
static int x1500_mmc_4bit_funcs[] = { 1, 1, 1, };
static int x1500_i2c0_funcs[] = { 0, 0, };
static int x1500_i2c1_a_funcs[] = { 2, 2, };
static int x1500_i2c1_c_funcs[] = { 0, 0, };
static int x1500_i2c2_funcs[] = { 1, 1, };
static int x1500_cim_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, };
static int x1500_pwm_pwm0_funcs[] = { 0, };
static int x1500_pwm_pwm1_funcs[] = { 1, };
static int x1500_pwm_pwm2_funcs[] = { 1, };
static int x1500_pwm_pwm3_funcs[] = { 2, };
static int x1500_pwm_pwm4_funcs[] = { 0, };

static const struct group_desc x1500_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1500_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", x1500_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data-a", x1500_uart1_data_a),
	INGENIC_PIN_GROUP("uart1-data-d", x1500_uart1_data_d),
	INGENIC_PIN_GROUP("uart1-hwflow", x1500_uart1_hwflow),
	INGENIC_PIN_GROUP("uart2-data-a", x1500_uart2_data_a),
	INGENIC_PIN_GROUP("uart2-data-d", x1500_uart2_data_d),
	INGENIC_PIN_GROUP("sfc", x1000_sfc),
	INGENIC_PIN_GROUP("mmc-1bit", x1500_mmc_1bit),
	INGENIC_PIN_GROUP("mmc-4bit", x1500_mmc_4bit),
	INGENIC_PIN_GROUP("i2c0-data", x1500_i2c0),
	INGENIC_PIN_GROUP("i2c1-data-a", x1500_i2c1_a),
	INGENIC_PIN_GROUP("i2c1-data-c", x1500_i2c1_c),
	INGENIC_PIN_GROUP("i2c2-data", x1500_i2c2),
	INGENIC_PIN_GROUP("cim-data", x1500_cim),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0", x1500_pwm_pwm0),
	INGENIC_PIN_GROUP("pwm1", x1500_pwm_pwm1),
	INGENIC_PIN_GROUP("pwm2", x1500_pwm_pwm2),
	INGENIC_PIN_GROUP("pwm3", x1500_pwm_pwm3),
	INGENIC_PIN_GROUP("pwm4", x1500_pwm_pwm4),
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
static const char *x1500_cim_groups[] = { "cim-data", };
static const char *x1500_lcd_groups[] = { "lcd-no-pins", };
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
	{ "cim", x1500_cim_groups, ARRAY_SIZE(x1500_cim_groups), },
	{ "lcd", x1500_lcd_groups, ARRAY_SIZE(x1500_lcd_groups), },
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
static int x1830_sfc_pins[] = { 0x17, 0x18, 0x1a, 0x19, 0x1b, 0x1c, };
static int x1830_ssi0_dt_pins[] = { 0x4c, };
static int x1830_ssi0_dr_pins[] = { 0x4b, };
static int x1830_ssi0_clk_pins[] = { 0x4f, };
static int x1830_ssi0_gpc_pins[] = { 0x4d, };
static int x1830_ssi0_ce0_pins[] = { 0x50, };
static int x1830_ssi0_ce1_pins[] = { 0x4e, };
static int x1830_ssi1_dt_c_pins[] = { 0x53, };
static int x1830_ssi1_dr_c_pins[] = { 0x54, };
static int x1830_ssi1_clk_c_pins[] = { 0x57, };
static int x1830_ssi1_gpc_c_pins[] = { 0x55, };
static int x1830_ssi1_ce0_c_pins[] = { 0x58, };
static int x1830_ssi1_ce1_c_pins[] = { 0x56, };
static int x1830_ssi1_dt_d_pins[] = { 0x62, };
static int x1830_ssi1_dr_d_pins[] = { 0x63, };
static int x1830_ssi1_clk_d_pins[] = { 0x66, };
static int x1830_ssi1_gpc_d_pins[] = { 0x64, };
static int x1830_ssi1_ce0_d_pins[] = { 0x67, };
static int x1830_ssi1_ce1_d_pins[] = { 0x65, };
static int x1830_mmc0_1bit_pins[] = { 0x24, 0x25, 0x20, };
static int x1830_mmc0_4bit_pins[] = { 0x21, 0x22, 0x23, };
static int x1830_mmc1_1bit_pins[] = { 0x42, 0x43, 0x44, };
static int x1830_mmc1_4bit_pins[] = { 0x45, 0x46, 0x47, };
static int x1830_i2c0_pins[] = { 0x0c, 0x0d, };
static int x1830_i2c1_pins[] = { 0x39, 0x3a, };
static int x1830_i2c2_pins[] = { 0x5b, 0x5c, };
static int x1830_lcd_rgb_18bit_pins[] = {
	0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b,
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

static int x1830_uart0_data_funcs[] = { 0, 0, };
static int x1830_uart0_hwflow_funcs[] = { 0, 0, };
static int x1830_uart1_data_funcs[] = { 0, 0, };
static int x1830_sfc_funcs[] = { 1, 1, 1, 1, 1, 1, };
static int x1830_ssi0_dt_funcs[] = { 0, };
static int x1830_ssi0_dr_funcs[] = { 0, };
static int x1830_ssi0_clk_funcs[] = { 0, };
static int x1830_ssi0_gpc_funcs[] = { 0, };
static int x1830_ssi0_ce0_funcs[] = { 0, };
static int x1830_ssi0_ce1_funcs[] = { 0, };
static int x1830_ssi1_dt_c_funcs[] = { 1, };
static int x1830_ssi1_dr_c_funcs[] = { 1, };
static int x1830_ssi1_clk_c_funcs[] = { 1, };
static int x1830_ssi1_gpc_c_funcs[] = { 1, };
static int x1830_ssi1_ce0_c_funcs[] = { 1, };
static int x1830_ssi1_ce1_c_funcs[] = { 1, };
static int x1830_ssi1_dt_d_funcs[] = { 2, };
static int x1830_ssi1_dr_d_funcs[] = { 2, };
static int x1830_ssi1_clk_d_funcs[] = { 2, };
static int x1830_ssi1_gpc_d_funcs[] = { 2, };
static int x1830_ssi1_ce0_d_funcs[] = { 2, };
static int x1830_ssi1_ce1_d_funcs[] = { 2, };
static int x1830_mmc0_1bit_funcs[] = { 0, 0, 0, };
static int x1830_mmc0_4bit_funcs[] = { 0, 0, 0, };
static int x1830_mmc1_1bit_funcs[] = { 0, 0, 0, };
static int x1830_mmc1_4bit_funcs[] = { 0, 0, 0, };
static int x1830_i2c0_funcs[] = { 1, 1, };
static int x1830_i2c1_funcs[] = { 0, 0, };
static int x1830_i2c2_funcs[] = { 1, 1, };
static int x1830_lcd_rgb_18bit_funcs[] = {
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0,
	0, 0, 0, 0,
};
static int x1830_lcd_slcd_8bit_funcs[] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static int x1830_lcd_slcd_16bit_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, };
static int x1830_pwm_pwm0_b_funcs[] = { 0, };
static int x1830_pwm_pwm0_c_funcs[] = { 1, };
static int x1830_pwm_pwm1_b_funcs[] = { 0, };
static int x1830_pwm_pwm1_c_funcs[] = { 1, };
static int x1830_pwm_pwm2_c_8_funcs[] = { 0, };
static int x1830_pwm_pwm2_c_13_funcs[] = { 1, };
static int x1830_pwm_pwm3_c_9_funcs[] = { 0, };
static int x1830_pwm_pwm3_c_14_funcs[] = { 1, };
static int x1830_pwm_pwm4_c_15_funcs[] = { 1, };
static int x1830_pwm_pwm4_c_25_funcs[] = { 0, };
static int x1830_pwm_pwm5_c_16_funcs[] = { 1, };
static int x1830_pwm_pwm5_c_26_funcs[] = { 0, };
static int x1830_pwm_pwm6_c_17_funcs[] = { 1, };
static int x1830_pwm_pwm6_c_27_funcs[] = { 0, };
static int x1830_pwm_pwm7_c_18_funcs[] = { 1, };
static int x1830_pwm_pwm7_c_28_funcs[] = { 0, };
static int x1830_mac_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

static const struct group_desc x1830_groups[] = {
	INGENIC_PIN_GROUP("uart0-data", x1830_uart0_data),
	INGENIC_PIN_GROUP("uart0-hwflow", x1830_uart0_hwflow),
	INGENIC_PIN_GROUP("uart1-data", x1830_uart1_data),
	INGENIC_PIN_GROUP("sfc", x1830_sfc),
	INGENIC_PIN_GROUP("ssi0-dt", x1830_ssi0_dt),
	INGENIC_PIN_GROUP("ssi0-dr", x1830_ssi0_dr),
	INGENIC_PIN_GROUP("ssi0-clk", x1830_ssi0_clk),
	INGENIC_PIN_GROUP("ssi0-gpc", x1830_ssi0_gpc),
	INGENIC_PIN_GROUP("ssi0-ce0", x1830_ssi0_ce0),
	INGENIC_PIN_GROUP("ssi0-ce1", x1830_ssi0_ce1),
	INGENIC_PIN_GROUP("ssi1-dt-c", x1830_ssi1_dt_c),
	INGENIC_PIN_GROUP("ssi1-dr-c", x1830_ssi1_dr_c),
	INGENIC_PIN_GROUP("ssi1-clk-c", x1830_ssi1_clk_c),
	INGENIC_PIN_GROUP("ssi1-gpc-c", x1830_ssi1_gpc_c),
	INGENIC_PIN_GROUP("ssi1-ce0-c", x1830_ssi1_ce0_c),
	INGENIC_PIN_GROUP("ssi1-ce1-c", x1830_ssi1_ce1_c),
	INGENIC_PIN_GROUP("ssi1-dt-d", x1830_ssi1_dt_d),
	INGENIC_PIN_GROUP("ssi1-dr-d", x1830_ssi1_dr_d),
	INGENIC_PIN_GROUP("ssi1-clk-d", x1830_ssi1_clk_d),
	INGENIC_PIN_GROUP("ssi1-gpc-d", x1830_ssi1_gpc_d),
	INGENIC_PIN_GROUP("ssi1-ce0-d", x1830_ssi1_ce0_d),
	INGENIC_PIN_GROUP("ssi1-ce1-d", x1830_ssi1_ce1_d),
	INGENIC_PIN_GROUP("mmc0-1bit", x1830_mmc0_1bit),
	INGENIC_PIN_GROUP("mmc0-4bit", x1830_mmc0_4bit),
	INGENIC_PIN_GROUP("mmc1-1bit", x1830_mmc1_1bit),
	INGENIC_PIN_GROUP("mmc1-4bit", x1830_mmc1_4bit),
	INGENIC_PIN_GROUP("i2c0-data", x1830_i2c0),
	INGENIC_PIN_GROUP("i2c1-data", x1830_i2c1),
	INGENIC_PIN_GROUP("i2c2-data", x1830_i2c2),
	INGENIC_PIN_GROUP("lcd-rgb-18bit", x1830_lcd_rgb_18bit),
	INGENIC_PIN_GROUP("lcd-slcd-8bit", x1830_lcd_slcd_8bit),
	INGENIC_PIN_GROUP("lcd-slcd-16bit", x1830_lcd_slcd_16bit),
	{ "lcd-no-pins", },
	INGENIC_PIN_GROUP("pwm0-b", x1830_pwm_pwm0_b),
	INGENIC_PIN_GROUP("pwm0-c", x1830_pwm_pwm0_c),
	INGENIC_PIN_GROUP("pwm1-b", x1830_pwm_pwm1_b),
	INGENIC_PIN_GROUP("pwm1-c", x1830_pwm_pwm1_c),
	INGENIC_PIN_GROUP("pwm2-c-8", x1830_pwm_pwm2_c_8),
	INGENIC_PIN_GROUP("pwm2-c-13", x1830_pwm_pwm2_c_13),
	INGENIC_PIN_GROUP("pwm3-c-9", x1830_pwm_pwm3_c_9),
	INGENIC_PIN_GROUP("pwm3-c-14", x1830_pwm_pwm3_c_14),
	INGENIC_PIN_GROUP("pwm4-c-15", x1830_pwm_pwm4_c_15),
	INGENIC_PIN_GROUP("pwm4-c-25", x1830_pwm_pwm4_c_25),
	INGENIC_PIN_GROUP("pwm5-c-16", x1830_pwm_pwm5_c_16),
	INGENIC_PIN_GROUP("pwm5-c-26", x1830_pwm_pwm5_c_26),
	INGENIC_PIN_GROUP("pwm6-c-17", x1830_pwm_pwm6_c_17),
	INGENIC_PIN_GROUP("pwm6-c-27", x1830_pwm_pwm6_c_27),
	INGENIC_PIN_GROUP("pwm7-c-18", x1830_pwm_pwm7_c_18),
	INGENIC_PIN_GROUP("pwm7-c-28", x1830_pwm_pwm7_c_28),
	INGENIC_PIN_GROUP("mac", x1830_mac),
};

static const char *x1830_uart0_groups[] = { "uart0-data", "uart0-hwflow", };
static const char *x1830_uart1_groups[] = { "uart1-data", };
static const char *x1830_sfc_groups[] = { "sfc", };
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
static const char *x1830_lcd_groups[] = {
	"lcd-rgb-18bit", "lcd-slcd-8bit", "lcd-slcd-16bit", "lcd-no-pins",
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

static inline bool ingenic_gpio_get_value(struct ingenic_gpio_chip *jzgc,
					  u8 offset)
{
	unsigned int val = ingenic_gpio_read_reg(jzgc, GPIO_PIN);

	return !!(val & BIT(offset));
}

static void ingenic_gpio_set_value(struct ingenic_gpio_chip *jzgc,
				   u8 offset, int value)
{
	if (jzgc->jzpc->info->version >= ID_JZ4760)
		ingenic_gpio_set_bit(jzgc, JZ4760_GPIO_PAT0, offset, !!value);
	else
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_DATA, offset, !!value);
}

static void irq_set_type(struct ingenic_gpio_chip *jzgc,
		u8 offset, unsigned int type)
{
	u8 reg1, reg2;
	bool val1, val2;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		val1 = val2 = true;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val1 = false;
		val2 = true;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val1 = true;
		val2 = false;
		break;
	case IRQ_TYPE_LEVEL_LOW:
	default:
		val1 = val2 = false;
		break;
	}

	if (jzgc->jzpc->info->version >= ID_JZ4760) {
		reg1 = JZ4760_GPIO_PAT1;
		reg2 = JZ4760_GPIO_PAT0;
	} else {
		reg1 = JZ4740_GPIO_TRIG;
		reg2 = JZ4740_GPIO_DIR;
	}

	if (jzgc->jzpc->info->version >= ID_X1000) {
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

	ingenic_gpio_set_bit(jzgc, GPIO_MSK, irqd->hwirq, true);
}

static void ingenic_gpio_irq_unmask(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);

	ingenic_gpio_set_bit(jzgc, GPIO_MSK, irqd->hwirq, false);
}

static void ingenic_gpio_irq_enable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	int irq = irqd->hwirq;

	if (jzgc->jzpc->info->version >= ID_JZ4760)
		ingenic_gpio_set_bit(jzgc, JZ4760_GPIO_INT, irq, true);
	else
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_SELECT, irq, true);

	ingenic_gpio_irq_unmask(irqd);
}

static void ingenic_gpio_irq_disable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	int irq = irqd->hwirq;

	ingenic_gpio_irq_mask(irqd);

	if (jzgc->jzpc->info->version >= ID_JZ4760)
		ingenic_gpio_set_bit(jzgc, JZ4760_GPIO_INT, irq, false);
	else
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_SELECT, irq, false);
}

static void ingenic_gpio_irq_ack(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);
	int irq = irqd->hwirq;
	bool high;

	if (irqd_get_trigger_type(irqd) == IRQ_TYPE_EDGE_BOTH) {
		/*
		 * Switch to an interrupt for the opposite edge to the one that
		 * triggered the interrupt being ACKed.
		 */
		high = ingenic_gpio_get_value(jzgc, irq);
		if (high)
			irq_set_type(jzgc, irq, IRQ_TYPE_EDGE_FALLING);
		else
			irq_set_type(jzgc, irq, IRQ_TYPE_EDGE_RISING);
	}

	if (jzgc->jzpc->info->version >= ID_JZ4760)
		ingenic_gpio_set_bit(jzgc, JZ4760_GPIO_FLAG, irq, false);
	else
		ingenic_gpio_set_bit(jzgc, JZ4740_GPIO_DATA, irq, true);
}

static int ingenic_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ingenic_gpio_chip *jzgc = gpiochip_get_data(gc);

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

	if (type == IRQ_TYPE_EDGE_BOTH) {
		/*
		 * The hardware does not support interrupts on both edges. The
		 * best we can do is to set up a single-edge interrupt and then
		 * switch to the opposing edge when ACKing the interrupt.
		 */
		bool high = ingenic_gpio_get_value(jzgc, irqd->hwirq);

		type = high ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING;
	}

	irq_set_type(jzgc, irqd->hwirq, type);
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

	if (jzgc->jzpc->info->version >= ID_JZ4760)
		flag = ingenic_gpio_read_reg(jzgc, JZ4760_GPIO_FLAG);
	else
		flag = ingenic_gpio_read_reg(jzgc, JZ4740_GPIO_FLAG);

	for_each_set_bit(i, &flag, 32)
		generic_handle_irq(irq_linear_revmap(gc->irq.domain, i));
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
		unsigned int pin, u8 reg, bool set)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;

	regmap_write(jzpc->map, offt * jzpc->info->reg_offset +
			(set ? REG_SET(reg) : REG_CLEAR(reg)), BIT(idx));
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

static inline bool ingenic_get_pin_config(struct ingenic_pinctrl *jzpc,
		unsigned int pin, u8 reg)
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

	if (jzpc->info->version >= ID_JZ4760) {
		if (ingenic_get_pin_config(jzpc, pin, JZ4760_GPIO_PAT1))
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

static int ingenic_pinmux_set_pin_fn(struct ingenic_pinctrl *jzpc,
		int pin, int func)
{
	unsigned int idx = pin % PINS_PER_GPIO_CHIP;
	unsigned int offt = pin / PINS_PER_GPIO_CHIP;

	dev_dbg(jzpc->dev, "set pin P%c%u to function %u\n",
			'A' + offt, idx, func);

	if (jzpc->info->version >= ID_X1000) {
		ingenic_shadow_config_pin(jzpc, pin, JZ4760_GPIO_INT, false);
		ingenic_shadow_config_pin(jzpc, pin, GPIO_MSK, false);
		ingenic_shadow_config_pin(jzpc, pin, JZ4760_GPIO_PAT1, func & 0x2);
		ingenic_shadow_config_pin(jzpc, pin, JZ4760_GPIO_PAT0, func & 0x1);
		ingenic_shadow_config_pin_load(jzpc, pin);
	} else if (jzpc->info->version >= ID_JZ4760) {
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_INT, false);
		ingenic_config_pin(jzpc, pin, GPIO_MSK, false);
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_PAT1, func & 0x2);
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_PAT0, func & 0x1);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_FUNC, true);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_TRIG, func & 0x2);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_SELECT, func > 0);
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

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, grp->name);

	for (i = 0; i < grp->num_pins; i++) {
		int *pin_modes = grp->data;

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

	if (jzpc->info->version >= ID_X1000) {
		ingenic_shadow_config_pin(jzpc, pin, JZ4760_GPIO_INT, false);
		ingenic_shadow_config_pin(jzpc, pin, GPIO_MSK, true);
		ingenic_shadow_config_pin(jzpc, pin, JZ4760_GPIO_PAT1, input);
		ingenic_shadow_config_pin_load(jzpc, pin);
	} else if (jzpc->info->version >= ID_JZ4760) {
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_INT, false);
		ingenic_config_pin(jzpc, pin, GPIO_MSK, true);
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_PAT1, input);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_SELECT, false);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_DIR, !input);
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_FUNC, false);
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
	bool pull;

	if (jzpc->info->version >= ID_JZ4760)
		pull = !ingenic_get_pin_config(jzpc, pin, JZ4760_GPIO_PEN);
	else
		pull = !ingenic_get_pin_config(jzpc, pin, JZ4740_GPIO_PULL_DIS);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (!pull || !(jzpc->info->pull_ups[offt] & BIT(idx)))
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!pull || !(jzpc->info->pull_downs[offt] & BIT(idx)))
			return -EINVAL;
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, 1);
	return 0;
}

static void ingenic_set_bias(struct ingenic_pinctrl *jzpc,
		unsigned int pin, unsigned int bias)
{
	if (jzpc->info->version >= ID_X1830) {
		unsigned int idx = pin % PINS_PER_GPIO_CHIP;
		unsigned int half = PINS_PER_GPIO_CHIP / 2;
		unsigned int idxh = pin % half * 2;
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

	} else if (jzpc->info->version >= ID_JZ4760) {
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_PEN, !bias);
	} else {
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_PULL_DIS, !bias);
	}
}

static void ingenic_set_output_level(struct ingenic_pinctrl *jzpc,
				     unsigned int pin, bool high)
{
	if (jzpc->info->version >= ID_JZ4760)
		ingenic_config_pin(jzpc, pin, JZ4760_GPIO_PAT0, high);
	else
		ingenic_config_pin(jzpc, pin, JZ4740_GPIO_DATA, high);
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
		case PIN_CONFIG_OUTPUT:
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

		case PIN_CONFIG_OUTPUT:
			ret = pinctrl_gpio_direction_output(pin);
			if (ret)
				return ret;

			ingenic_set_output_level(jzpc, pin, arg);
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

static const struct of_device_id ingenic_gpio_of_match[] __initconst = {
	{ .compatible = "ingenic,jz4740-gpio", },
	{ .compatible = "ingenic,jz4760-gpio", },
	{ .compatible = "ingenic,jz4770-gpio", },
	{ .compatible = "ingenic,jz4780-gpio", },
	{ .compatible = "ingenic,x1000-gpio", },
	{ .compatible = "ingenic,x1830-gpio", },
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

	jzgc->irq_chip.name = jzgc->gc.label;
	jzgc->irq_chip.irq_enable = ingenic_gpio_irq_enable;
	jzgc->irq_chip.irq_disable = ingenic_gpio_irq_disable;
	jzgc->irq_chip.irq_unmask = ingenic_gpio_irq_unmask;
	jzgc->irq_chip.irq_mask = ingenic_gpio_irq_mask;
	jzgc->irq_chip.irq_ack = ingenic_gpio_irq_ack;
	jzgc->irq_chip.irq_set_type = ingenic_gpio_irq_set_type;
	jzgc->irq_chip.irq_set_wake = ingenic_gpio_irq_set_wake;
	jzgc->irq_chip.flags = IRQCHIP_MASK_ON_SUSPEND;

	girq = &jzgc->gc.irq;
	girq->chip = &jzgc->irq_chip;
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
	unsigned int i;
	int err;

	jzpc = devm_kzalloc(dev, sizeof(*jzpc), GFP_KERNEL);
	if (!jzpc)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	jzpc->map = devm_regmap_init_mmio(dev, base,
			&ingenic_pinctrl_regmap_config);
	if (IS_ERR(jzpc->map)) {
		dev_err(dev, "Failed to create regmap\n");
		return PTR_ERR(jzpc->map);
	}

	jzpc->dev = dev;
	jzpc->info = chip_info = of_device_get_match_data(dev);

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
		if (of_match_node(ingenic_gpio_of_match, node)) {
			err = ingenic_gpio_probe(jzpc, node);
			if (err)
				return err;
		}
	}

	return 0;
}

static const struct of_device_id ingenic_pinctrl_of_match[] = {
	{ .compatible = "ingenic,jz4740-pinctrl", .data = &jz4740_chip_info },
	{ .compatible = "ingenic,jz4725b-pinctrl", .data = &jz4725b_chip_info },
	{ .compatible = "ingenic,jz4760-pinctrl", .data = &jz4760_chip_info },
	{ .compatible = "ingenic,jz4760b-pinctrl", .data = &jz4760_chip_info },
	{ .compatible = "ingenic,jz4770-pinctrl", .data = &jz4770_chip_info },
	{ .compatible = "ingenic,jz4780-pinctrl", .data = &jz4780_chip_info },
	{ .compatible = "ingenic,x1000-pinctrl", .data = &x1000_chip_info },
	{ .compatible = "ingenic,x1000e-pinctrl", .data = &x1000_chip_info },
	{ .compatible = "ingenic,x1500-pinctrl", .data = &x1500_chip_info },
	{ .compatible = "ingenic,x1830-pinctrl", .data = &x1830_chip_info },
	{},
};

static struct platform_driver ingenic_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-ingenic",
		.of_match_table = ingenic_pinctrl_of_match,
	},
};

static int __init ingenic_pinctrl_drv_register(void)
{
	return platform_driver_probe(&ingenic_pinctrl_driver,
				     ingenic_pinctrl_probe);
}
subsys_initcall(ingenic_pinctrl_drv_register);
