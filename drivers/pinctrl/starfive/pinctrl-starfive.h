/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#ifndef __DRIVERS_PINCTRL_STARFIVE_H
#define __DRIVERS_PINCTRL_STARFIVE_H

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

#define MAX_GPIO				64

#define STARFIVE_PINS_SIZE			4

#define STARFIVE_USE_SCU			BIT(0)

#define SYS_REG_SUSPENDED_NUM			174
#define AON_REG_SUSPENDED_NUM			37

struct platform_device;

extern const struct pinmux_ops starfive_pmx_ops;

struct starfive_pin_config {
	unsigned long io_config;
	u32 pinmux_func;
	u32 gpio_num;
	u32 gpio_dout;
	u32 gpio_doen;
	u32 gpio_din_num;
	s32 *gpio_din_reg;
	s32 syscon;
	s32 padmux_func;
};

struct starfive_pin {
	unsigned int pin;
	struct starfive_pin_config pin_config;
};

struct starfive_pin_reg {
	s32 io_conf_reg;
	s32 gpo_dout_reg;
	s32 gpo_doen_reg;
	s32 func_sel_reg;
	s32 func_sel_shift;
	s32 func_sel_mask;
	s32 syscon_reg;
	s32 pad_sel_reg;
	s32 pad_sel_shift;
	s32 pad_sel_mask;
};

struct starfive_iopad_sel_func_inf {
	unsigned int padctl_gpio_base;
	unsigned int padctl_gpio0;
};

struct starfive_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl_dev;
	void __iomem *padctl_base;
	void __iomem *gpio_base;
	unsigned int padctl_gpio_base;
	unsigned int padctl_gpio0;
	const struct starfive_pinctrl_soc_info *info;
	struct starfive_pin_reg *pin_regs;
	unsigned int group_index;

	struct mutex mutex;
	raw_spinlock_t lock;

	struct gpio_chip gc;
	struct pinctrl_gpio_range gpios;
	unsigned long enabled;
	unsigned int trigger[MAX_GPIO];

	u32 sys_reg_suspended[SYS_REG_SUSPENDED_NUM];
	u32 aon_reg_suspended[AON_REG_SUSPENDED_NUM];
};

struct starfive_pinctrl_soc_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	unsigned int flags;

	/*gpio dout/doen/din register*/
	unsigned int dout_reg_base;
	unsigned int dout_reg_offset;
	unsigned int doen_reg_base;
	unsigned int doen_reg_offset;
	unsigned int din_reg_base;
	unsigned int din_reg_offset;

	/* sel-function */
	int (*starfive_iopad_sel_func)(struct platform_device *pdev,
					struct starfive_pinctrl *ipctl,
					unsigned int func_id);
	/* generic pinconf */
	int (*starfive_pinconf_get)(struct pinctrl_dev *pctldev, unsigned int pin_id,
			       unsigned long *config);
	int (*starfive_pinconf_set)(struct pinctrl_dev *pctldev,
				unsigned int pin_id, unsigned long *configs,
				unsigned int num_configs);

	/* generic pinmux */
	int (*starfive_pmx_set_one_pin_mux)(struct starfive_pinctrl *ipctl,
				struct starfive_pin *pin);
	/* gpio chip */
	int (*starfive_gpio_register)(struct platform_device *pdev,
				struct starfive_pinctrl *ipctl);
	void (*starfive_pinctrl_parse_pin)(struct starfive_pinctrl *ipctl,
					unsigned int *pins_id, struct starfive_pin *pin_data,
					const __be32 *list_p,
					struct device_node *np);
};

#define	STARFIVE_PINCTRL_PIN(pin) PINCTRL_PIN(pin, #pin)

int starfive_pinctrl_probe(struct platform_device *pdev,
			const struct starfive_pinctrl_soc_info *info);

#endif /* __DRIVERS_PINCTRL_STARFIVE_H */
