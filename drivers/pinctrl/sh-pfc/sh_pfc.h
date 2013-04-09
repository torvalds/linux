/*
 * SuperH Pin Function Controller Support
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __SH_PFC_H
#define __SH_PFC_H

#include <linux/bug.h>
#include <linux/stringify.h>

typedef unsigned short pinmux_enum_t;

#define SH_PFC_MARK_INVALID	((pinmux_enum_t)-1)

enum {
	PINMUX_TYPE_NONE,

	PINMUX_TYPE_FUNCTION,
	PINMUX_TYPE_GPIO,
	PINMUX_TYPE_OUTPUT,
	PINMUX_TYPE_INPUT,
	PINMUX_TYPE_INPUT_PULLUP,
	PINMUX_TYPE_INPUT_PULLDOWN,

	PINMUX_FLAG_TYPE,	/* must be last */
};

#define SH_PFC_PIN_CFG_INPUT		(1 << 0)
#define SH_PFC_PIN_CFG_OUTPUT		(1 << 1)
#define SH_PFC_PIN_CFG_PULL_UP		(1 << 2)
#define SH_PFC_PIN_CFG_PULL_DOWN	(1 << 3)

struct sh_pfc_pin {
	const pinmux_enum_t enum_id;
	const char *name;
	unsigned int configs;
};

#define SH_PFC_PIN_GROUP(n)				\
	{						\
		.name = #n,				\
		.pins = n##_pins,			\
		.mux = n##_mux,				\
		.nr_pins = ARRAY_SIZE(n##_pins),	\
	}

struct sh_pfc_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int *mux;
	unsigned int nr_pins;
};

#define SH_PFC_FUNCTION(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.nr_groups = ARRAY_SIZE(n##_groups),	\
	}

struct sh_pfc_function {
	const char *name;
	const char * const *groups;
	unsigned int nr_groups;
};

struct pinmux_func {
	const pinmux_enum_t enum_id;
	const char *name;
};

#define PINMUX_GPIO(gpio, data_or_mark)			\
	[gpio] = {					\
		.name = __stringify(gpio),		\
		.enum_id = data_or_mark,		\
	}
#define PINMUX_GPIO_FN(gpio, base, data_or_mark)	\
	[gpio - (base)] = {				\
		.name = __stringify(gpio),		\
		.enum_id = data_or_mark,		\
	}

#define PINMUX_DATA(data_or_mark, ids...) data_or_mark, ids, 0

struct pinmux_cfg_reg {
	unsigned long reg, reg_width, field_width;
	const pinmux_enum_t *enum_ids;
	const unsigned long *var_field_width;
};

#define PINMUX_CFG_REG(name, r, r_width, f_width) \
	.reg = r, .reg_width = r_width, .field_width = f_width,		\
	.enum_ids = (pinmux_enum_t [(r_width / f_width) * (1 << f_width)])

#define PINMUX_CFG_REG_VAR(name, r, r_width, var_fw0, var_fwn...) \
	.reg = r, .reg_width = r_width,	\
	.var_field_width = (unsigned long [r_width]) { var_fw0, var_fwn, 0 }, \
	.enum_ids = (pinmux_enum_t [])

struct pinmux_data_reg {
	unsigned long reg, reg_width;
	const pinmux_enum_t *enum_ids;
};

#define PINMUX_DATA_REG(name, r, r_width) \
	.reg = r, .reg_width = r_width,	\
	.enum_ids = (pinmux_enum_t [r_width]) \

struct pinmux_irq {
	int irq;
	unsigned short *gpios;
};

#define PINMUX_IRQ(irq_nr, ids...)			   \
	{ .irq = irq_nr, .gpios = (unsigned short []) { ids, 0 } }	\

struct pinmux_range {
	pinmux_enum_t begin;
	pinmux_enum_t end;
	pinmux_enum_t force;
};

struct sh_pfc;

struct sh_pfc_soc_operations {
	unsigned int (*get_bias)(struct sh_pfc *pfc, unsigned int pin);
	void (*set_bias)(struct sh_pfc *pfc, unsigned int pin,
			 unsigned int bias);
};

struct sh_pfc_soc_info {
	const char *name;
	const struct sh_pfc_soc_operations *ops;

	struct pinmux_range input;
	struct pinmux_range input_pd;
	struct pinmux_range input_pu;
	struct pinmux_range output;
	struct pinmux_range function;

	const struct sh_pfc_pin *pins;
	unsigned int nr_pins;
	const struct pinmux_range *ranges;
	unsigned int nr_ranges;
	const struct sh_pfc_pin_group *groups;
	unsigned int nr_groups;
	const struct sh_pfc_function *functions;
	unsigned int nr_functions;

	const struct pinmux_func *func_gpios;
	unsigned int nr_func_gpios;

	const struct pinmux_cfg_reg *cfg_regs;
	const struct pinmux_data_reg *data_regs;

	const pinmux_enum_t *gpio_data;
	unsigned int gpio_data_size;

	const struct pinmux_irq *gpio_irq;
	unsigned int gpio_irq_size;

	unsigned long unlock_reg;
};

enum { GPIO_CFG_REQ, GPIO_CFG_FREE };

/* helper macro for port */
#define PORT_1(fn, pfx, sfx) fn(pfx, sfx)

#define PORT_10(fn, pfx, sfx) \
	PORT_1(fn, pfx##0, sfx), PORT_1(fn, pfx##1, sfx),	\
	PORT_1(fn, pfx##2, sfx), PORT_1(fn, pfx##3, sfx),	\
	PORT_1(fn, pfx##4, sfx), PORT_1(fn, pfx##5, sfx),	\
	PORT_1(fn, pfx##6, sfx), PORT_1(fn, pfx##7, sfx),	\
	PORT_1(fn, pfx##8, sfx), PORT_1(fn, pfx##9, sfx)

#define PORT_10_REV(fn, pfx, sfx)	\
	PORT_1(fn, pfx##9, sfx), PORT_1(fn, pfx##8, sfx),	\
	PORT_1(fn, pfx##7, sfx), PORT_1(fn, pfx##6, sfx),	\
	PORT_1(fn, pfx##5, sfx), PORT_1(fn, pfx##4, sfx),	\
	PORT_1(fn, pfx##3, sfx), PORT_1(fn, pfx##2, sfx),	\
	PORT_1(fn, pfx##1, sfx), PORT_1(fn, pfx##0, sfx)

#define PORT_32(fn, pfx, sfx)					\
	PORT_10(fn, pfx, sfx), PORT_10(fn, pfx##1, sfx),	\
	PORT_10(fn, pfx##2, sfx), PORT_1(fn, pfx##30, sfx),	\
	PORT_1(fn, pfx##31, sfx)

#define PORT_32_REV(fn, pfx, sfx)					\
	PORT_1(fn, pfx##31, sfx), PORT_1(fn, pfx##30, sfx),		\
	PORT_10_REV(fn, pfx##2, sfx), PORT_10_REV(fn, pfx##1, sfx),	\
	PORT_10_REV(fn, pfx, sfx)

#define PORT_90(fn, pfx, sfx) \
	PORT_10(fn, pfx##1, sfx), PORT_10(fn, pfx##2, sfx),	\
	PORT_10(fn, pfx##3, sfx), PORT_10(fn, pfx##4, sfx),	\
	PORT_10(fn, pfx##5, sfx), PORT_10(fn, pfx##6, sfx),	\
	PORT_10(fn, pfx##7, sfx), PORT_10(fn, pfx##8, sfx),	\
	PORT_10(fn, pfx##9, sfx)

#define _PORT_ALL(pfx, sfx) pfx##_##sfx
#define _GPIO_PORT(pfx, sfx) PINMUX_GPIO(GPIO_PORT##pfx, PORT##pfx##_DATA)
#define PORT_ALL(str)	CPU_ALL_PORT(_PORT_ALL, PORT, str)
#define GPIO_PORT_ALL()	CPU_ALL_PORT(_GPIO_PORT, , unused)
#define GPIO_FN(str) PINMUX_GPIO_FN(GPIO_FN_##str, PINMUX_FN_BASE, str##_MARK)

/* helper macro for pinmux_enum_t */
#define PORT_DATA_I(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_IN)

#define PORT_DATA_I_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD)

#define PORT_DATA_I_PU(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,	\
		    PORT##nr##_IN, PORT##nr##_IN_PU)

#define PORT_DATA_I_PU_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,			\
		    PORT##nr##_IN, PORT##nr##_IN_PD, PORT##nr##_IN_PU)

#define PORT_DATA_O(nr)		\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT)

#define PORT_DATA_IO(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN)

#define PORT_DATA_IO_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD)

#define PORT_DATA_IO_PU(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PU)

#define PORT_DATA_IO_PU_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD, PORT##nr##_IN_PU)

/* helper macro for top 4 bits in PORTnCR */
#define _PCRH(in, in_pd, in_pu, out)	\
	0, (out), (in), 0,		\
	0, 0, 0, 0,			\
	0, 0, (in_pd), 0,		\
	0, 0, (in_pu), 0

#define PORTCR(nr, reg)							\
	{								\
		PINMUX_CFG_REG("PORT" nr "CR", reg, 8, 4) {		\
			_PCRH(PORT##nr##_IN, PORT##nr##_IN_PD,		\
			      PORT##nr##_IN_PU, PORT##nr##_OUT),	\
				PORT##nr##_FN0, PORT##nr##_FN1,		\
				PORT##nr##_FN2, PORT##nr##_FN3,		\
				PORT##nr##_FN4, PORT##nr##_FN5,		\
				PORT##nr##_FN6, PORT##nr##_FN7 }	\
	}

#endif /* __SH_PFC_H */
