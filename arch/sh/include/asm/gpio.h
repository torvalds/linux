/*
 *  include/asm-sh/gpio.h
 *
 * Generic GPIO API and pinmux table support for SuperH.
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_GPIO_H
#define __ASM_SH_GPIO_H

#include <linux/kernel.h>
#include <linux/errno.h>

#if defined(CONFIG_CPU_SH3)
#include <cpu/gpio.h>
#endif

typedef unsigned short pinmux_enum_t;
typedef unsigned char pinmux_flag_t;

#define PINMUX_TYPE_NONE            0
#define PINMUX_TYPE_FUNCTION        1
#define PINMUX_TYPE_GPIO            2
#define PINMUX_TYPE_OUTPUT          3
#define PINMUX_TYPE_INPUT           4
#define PINMUX_TYPE_INPUT_PULLUP    5
#define PINMUX_TYPE_INPUT_PULLDOWN  6

#define PINMUX_FLAG_TYPE            (0x7)
#define PINMUX_FLAG_WANT_PULLUP     (1 << 3)
#define PINMUX_FLAG_WANT_PULLDOWN   (1 << 4)

struct pinmux_gpio {
	pinmux_enum_t enum_id;
	pinmux_flag_t flags;
};

#define PINMUX_GPIO(gpio, data_or_mark) [gpio] = { data_or_mark }
#define PINMUX_DATA(data_or_mark, ids...) data_or_mark, ids, 0

struct pinmux_cfg_reg {
	unsigned long reg, reg_width, field_width;
	unsigned long *cnt;
	pinmux_enum_t *enum_ids;
};

#define PINMUX_CFG_REG(name, r, r_width, f_width) \
	.reg = r, .reg_width = r_width, .field_width = f_width,		\
	.cnt = (unsigned long [r_width / f_width]) {}, \
	.enum_ids = (pinmux_enum_t [(r_width / f_width) * (1 << f_width)]) \

struct pinmux_data_reg {
	unsigned long reg, reg_width;
	pinmux_enum_t *enum_ids;
};

#define PINMUX_DATA_REG(name, r, r_width) \
	.reg = r, .reg_width = r_width,	\
	.enum_ids = (pinmux_enum_t [r_width]) \

struct pinmux_range {
	pinmux_enum_t begin;
	pinmux_enum_t end;
	pinmux_enum_t force;
};

struct pinmux_info {
	char *name;
	pinmux_enum_t reserved_id;
	struct pinmux_range data;
	struct pinmux_range input;
	struct pinmux_range input_pd;
	struct pinmux_range input_pu;
	struct pinmux_range output;
	struct pinmux_range mark;
	struct pinmux_range function;

	unsigned first_gpio, last_gpio;

	struct pinmux_gpio *gpios;
	struct pinmux_cfg_reg *cfg_regs;
	struct pinmux_data_reg *data_regs;

	pinmux_enum_t *gpio_data;
	unsigned int gpio_data_size;

	unsigned long *gpio_in_use;
};

int register_pinmux(struct pinmux_info *pip);

int __gpio_request(unsigned gpio);
static inline int gpio_request(unsigned gpio, const char *label)
{
	return __gpio_request(gpio);
}
void gpio_free(unsigned gpio);
int gpio_direction_input(unsigned gpio);
int gpio_direction_output(unsigned gpio, int value);
int gpio_get_value(unsigned gpio);
void gpio_set_value(unsigned gpio, int value);

/* IRQ modes are unspported */
static inline int gpio_to_irq(unsigned gpio)
{
	WARN_ON(1);
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned irq)
{
	WARN_ON(1);
	return -EINVAL;
}

#include <asm-generic/gpio.h>

#endif /* __ASM_SH_GPIO_H */
