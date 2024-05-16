/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 */
#ifndef __PINCTRL_STM32_H
#define __PINCTRL_STM32_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>

#define STM32_PIN_NO(x) ((x) << 8)
#define STM32_GET_PIN_NO(x) ((x) >> 8)
#define STM32_GET_PIN_FUNC(x) ((x) & 0xff)

#define STM32_PIN_GPIO		0
#define STM32_PIN_AF(x)		((x) + 1)
#define STM32_PIN_ANALOG	(STM32_PIN_AF(15) + 1)
#define STM32_CONFIG_NUM	(STM32_PIN_ANALOG + 1)

/*  package information */
#define STM32MP_PKG_AA		BIT(0)
#define STM32MP_PKG_AB		BIT(1)
#define STM32MP_PKG_AC		BIT(2)
#define STM32MP_PKG_AD		BIT(3)
#define STM32MP_PKG_AI		BIT(8)
#define STM32MP_PKG_AK		BIT(10)
#define STM32MP_PKG_AL		BIT(11)

struct stm32_desc_function {
	const char *name;
	const unsigned char num;
};

struct stm32_desc_pin {
	struct pinctrl_pin_desc pin;
	const struct stm32_desc_function functions[STM32_CONFIG_NUM];
	const unsigned int pkg;
};

#define STM32_PIN(_pin, ...)					\
	{							\
		.pin = _pin,					\
		.functions = {	\
			__VA_ARGS__},			\
	}

#define STM32_PIN_PKG(_pin, _pkg, ...)					\
	{							\
		.pin = _pin,					\
		.pkg  = _pkg,				\
		.functions = {	\
			__VA_ARGS__},			\
	}
#define STM32_FUNCTION(_num, _name)		\
	[_num] = {						\
		.num = _num,					\
		.name = _name,					\
	}

struct stm32_pinctrl_match_data {
	const struct stm32_desc_pin *pins;
	const unsigned int npins;
	bool secure_control;
};

struct stm32_gpio_bank;

int stm32_pctl_probe(struct platform_device *pdev);
void stm32_pmx_get_mode(struct stm32_gpio_bank *bank,
			int pin, u32 *mode, u32 *alt);
int stm32_pinctrl_suspend(struct device *dev);
int stm32_pinctrl_resume(struct device *dev);

#endif /* __PINCTRL_STM32_H */

