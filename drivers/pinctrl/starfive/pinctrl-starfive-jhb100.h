/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#ifndef __PINCTRL_STARFIVE_JHB100_H__
#define __PINCTRL_STARFIVE_JHB100_H__

#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

/* power-source value */
enum jhb100_pinvref {
	JHB100_PINVREF_3_3V,
	JHB100_PINVREF_2_5V,
	JHB100_PINVREF_1_8V,
	JHB100_PINVREF_NUM
};

#define JHB100_MAX_BANKS			2

struct jhb100_pin_layout_desc {
	unsigned int pin_start;
	unsigned int pin_cnt;
	const char *name;
	s8 gpio_func_sel;
};

struct jhb100_pinctrl {
	struct device *dev;
	struct gpio_chip gc;
	unsigned int num_banks;
	struct pinctrl_gpio_range gpios;
	raw_spinlock_t lock;
	const char *iodomain_name;
	void __iomem *base;
	struct pinctrl_dev *pctl;
	/* register read/write mutex */
	struct mutex mutex;
	const struct jhb100_pinctrl_domain_info *info;
	int irq;
	struct irq_domain *irq_domain;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int ngpios;
	s8 *gpio_func_sel_arr;
};

struct pinvref_desc {
	unsigned int range;
	u32 pin_grp[32];
	u32 num_pins;
};

struct pinvref_reg {
	unsigned int reg;
	const struct pinvref_desc *pv_desc;
	u32 num_pv;
};

struct gpio_irq_reg {
	unsigned int reg;
	unsigned int width_per_pin;
};

struct starfive_pinctrl_regs {
	struct pinvref_reg vref;
	struct gpio_irq_reg func_sel;
	unsigned int config;
	unsigned int output;
	unsigned int output_en;
	unsigned int gpio_status;
	unsigned int irq_en;
	unsigned int irq_status;
	unsigned int irq_clr;
	unsigned int irq_trigger;
	unsigned int irq_level;
	unsigned int irq_both_edge;
	unsigned int irq_edge;
};

struct reg_layout_field {
	unsigned char shift;
	unsigned char width;
};

enum jhb100_pad_cfg_field {
	PAD_CFG_DEBOUNCE_WIDTH,
	PAD_CFG_DRIVE_STRENGTH_2BIT,
	PAD_CFG_DRIVE_STRENGTH_3BIT,
	PAD_CFG_INPUT_ENABLE,
	PAD_CFG_VSEL,
	PAD_CFG_MODE_SELECT,
	PAD_CFG_OPEN_DRAIN_PULL_UP_SEL,
	PAD_CFG_PULL_DOWN,
	PAD_CFG_PULL_UP,
	PAD_CFG_SCHMITT_TRIGGER_SELECT,
	PAD_CFG_SLEW_RATE,
	PAD_CFG_COUNT
};

#define RL_DESC_SUPPORTED(crl_desc, field_enum) ({ \
	typeof(crl_desc) _desc = (crl_desc); \
	(_desc && (_desc)->fields[(field_enum)].width > 0); \
})

#define RL_DESC_SHIFT(crl_desc, field_enum) ({ \
	typeof(crl_desc) __desc = (crl_desc); \
	(__desc)->fields[(field_enum)].shift; \
})

#define RL_DESC_GENMASK(crl_desc, field_enum) ({ \
	typeof(crl_desc) __desc = (crl_desc); \
	RL_DESC_SUPPORTED(__desc, field_enum) ? \
	GENMASK( \
		(__desc)->fields[(field_enum)].shift + (__desc)->fields[(field_enum)].width - 1, \
		(__desc)->fields[(field_enum)].shift \
	) : 0; \
})

struct config_reg_layout_desc {
	unsigned int pin_start;
	unsigned int pin_cnt;

	struct reg_layout_field fields[PAD_CFG_COUNT];
};

#define JHB100_FUNC_MAPS_MAX_PIN(n)	((n) + 1)

struct jhb100_pinctrl_func_maps {
	char *func;
	unsigned char val;
	u32 max_pin;
};

struct jhb100_pinctrl_domain_info {
	const char *name;
	const struct pinctrl_pin_desc *pins;
	const struct jhb100_pin_layout_desc *pl_desc;
	const struct jhb100_pinctrl_func_maps *fmaps;
	u32 num_maps;
	const struct config_reg_layout_desc *crl_desc;
	const struct starfive_pinctrl_regs *regs;
};

int jhb100_pinctrl_probe(struct platform_device *pdev);

#endif /* __PINCTRL_STARFIVE_JHB100_H__ */
