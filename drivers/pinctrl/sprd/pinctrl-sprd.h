/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver header file for pin controller driver
 * Copyright (C) 2017 Spreadtrum  - http://www.spreadtrum.com
 */

#ifndef __PINCTRL_SPRD_H__
#define __PINCTRL_SPRD_H__

struct platform_device;

#define NUM_OFFSET	(20)
#define TYPE_OFFSET	(16)
#define BIT_OFFSET	(8)
#define WIDTH_OFFSET	(4)

#define SPRD_PIN_INFO(num, type, offset, width, reg)	\
		(((num) & 0xFFF) << NUM_OFFSET |	\
		 ((type) & 0xF) << TYPE_OFFSET |	\
		 ((offset) & 0xFF) << BIT_OFFSET |	\
		 ((width) & 0xF) << WIDTH_OFFSET |	\
		 ((reg) & 0xF))

#define SPRD_PINCTRL_PIN(pin)	SPRD_PINCTRL_PIN_DATA(pin, #pin)

#define SPRD_PINCTRL_PIN_DATA(a, b)				\
	{							\
		.name = b,					\
		.num = (((a) >> NUM_OFFSET) & 0xfff),		\
		.type = (((a) >> TYPE_OFFSET) & 0xf),		\
		.bit_offset = (((a) >> BIT_OFFSET) & 0xff),	\
		.bit_width = ((a) >> WIDTH_OFFSET & 0xf),	\
		.reg = ((a) & 0xf)				\
	}

enum pin_type {
	GLOBAL_CTRL_PIN,
	COMMON_PIN,
	MISC_PIN,
};

struct sprd_pins_info {
	const char *name;
	unsigned int num;
	enum pin_type type;

	/* for global control pins configuration */
	unsigned long bit_offset;
	unsigned long bit_width;
	unsigned int reg;
};

int sprd_pinctrl_core_probe(struct platform_device *pdev,
			    struct sprd_pins_info *sprd_soc_pin_info,
			    int pins_cnt);
void sprd_pinctrl_remove(struct platform_device *pdev);
void sprd_pinctrl_shutdown(struct platform_device *pdev);

#endif /* __PINCTRL_SPRD_H__ */
