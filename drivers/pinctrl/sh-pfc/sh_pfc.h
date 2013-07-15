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

enum {
	PINMUX_TYPE_NONE,
	PINMUX_TYPE_FUNCTION,
	PINMUX_TYPE_GPIO,
	PINMUX_TYPE_OUTPUT,
	PINMUX_TYPE_INPUT,
};

#define SH_PFC_PIN_CFG_INPUT		(1 << 0)
#define SH_PFC_PIN_CFG_OUTPUT		(1 << 1)
#define SH_PFC_PIN_CFG_PULL_UP		(1 << 2)
#define SH_PFC_PIN_CFG_PULL_DOWN	(1 << 3)

struct sh_pfc_pin {
	u16 pin;
	u16 enum_id;
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
	u16 enum_id;
	const char *name;
};

struct pinmux_cfg_reg {
	unsigned long reg, reg_width, field_width;
	const u16 *enum_ids;
	const unsigned long *var_field_width;
};

#define PINMUX_CFG_REG(name, r, r_width, f_width) \
	.reg = r, .reg_width = r_width, .field_width = f_width,		\
	.enum_ids = (u16 [(r_width / f_width) * (1 << f_width)])

#define PINMUX_CFG_REG_VAR(name, r, r_width, var_fw0, var_fwn...) \
	.reg = r, .reg_width = r_width,	\
	.var_field_width = (unsigned long [r_width]) { var_fw0, var_fwn, 0 }, \
	.enum_ids = (u16 [])

struct pinmux_data_reg {
	unsigned long reg, reg_width;
	const u16 *enum_ids;
};

#define PINMUX_DATA_REG(name, r, r_width) \
	.reg = r, .reg_width = r_width,	\
	.enum_ids = (u16 [r_width]) \

struct pinmux_irq {
	int irq;
	unsigned short *gpios;
};

#define PINMUX_IRQ(irq_nr, ids...)			   \
	{ .irq = irq_nr, .gpios = (unsigned short []) { ids, 0 } }	\

struct pinmux_range {
	u16 begin;
	u16 end;
	u16 force;
};

struct sh_pfc;

struct sh_pfc_soc_operations {
	int (*init)(struct sh_pfc *pfc);
	void (*exit)(struct sh_pfc *pfc);
	unsigned int (*get_bias)(struct sh_pfc *pfc, unsigned int pin);
	void (*set_bias)(struct sh_pfc *pfc, unsigned int pin,
			 unsigned int bias);
};

struct sh_pfc_soc_info {
	const char *name;
	const struct sh_pfc_soc_operations *ops;

	struct pinmux_range input;
	struct pinmux_range output;
	struct pinmux_range function;

	const struct sh_pfc_pin *pins;
	unsigned int nr_pins;
	const struct sh_pfc_pin_group *groups;
	unsigned int nr_groups;
	const struct sh_pfc_function *functions;
	unsigned int nr_functions;

	const struct pinmux_func *func_gpios;
	unsigned int nr_func_gpios;

	const struct pinmux_cfg_reg *cfg_regs;
	const struct pinmux_data_reg *data_regs;

	const u16 *gpio_data;
	unsigned int gpio_data_size;

	const struct pinmux_irq *gpio_irq;
	unsigned int gpio_irq_size;

	unsigned long unlock_reg;
};

/* -----------------------------------------------------------------------------
 * Helper macros to create pin and port lists
 */

/*
 * sh_pfc_soc_info gpio_data array macros
 */

#define PINMUX_DATA(data_or_mark, ids...)	data_or_mark, ids, 0

#define PINMUX_IPSR_NOGP(ispr, fn)					\
	PINMUX_DATA(fn##_MARK, FN_##fn)
#define PINMUX_IPSR_DATA(ipsr, fn)					\
	PINMUX_DATA(fn##_MARK, FN_##fn, FN_##ipsr)
#define PINMUX_IPSR_NOGM(ispr, fn, ms)					\
	PINMUX_DATA(fn##_MARK, FN_##fn, FN_##ms)
#define PINMUX_IPSR_MSEL(ipsr, fn, ms)					\
	PINMUX_DATA(fn##_MARK, FN_##fn, FN_##ipsr, FN_##ms)
#define PINMUX_IPSR_MODSEL_DATA(ipsr, fn, ms)				\
	PINMUX_DATA(fn##_MARK, FN_##ms, FN_##ipsr, FN_##fn)

/*
 * GP port style (32 ports banks)
 */

#define PORT_GP_1(bank, pin, fn, sfx) fn(bank, pin, GP_##bank##_##pin, sfx)

#define PORT_GP_32(bank, fn, sfx)					\
	PORT_GP_1(bank, 0,  fn, sfx), PORT_GP_1(bank, 1,  fn, sfx),	\
	PORT_GP_1(bank, 2,  fn, sfx), PORT_GP_1(bank, 3,  fn, sfx),	\
	PORT_GP_1(bank, 4,  fn, sfx), PORT_GP_1(bank, 5,  fn, sfx),	\
	PORT_GP_1(bank, 6,  fn, sfx), PORT_GP_1(bank, 7,  fn, sfx),	\
	PORT_GP_1(bank, 8,  fn, sfx), PORT_GP_1(bank, 9,  fn, sfx),	\
	PORT_GP_1(bank, 10, fn, sfx), PORT_GP_1(bank, 11, fn, sfx),	\
	PORT_GP_1(bank, 12, fn, sfx), PORT_GP_1(bank, 13, fn, sfx),	\
	PORT_GP_1(bank, 14, fn, sfx), PORT_GP_1(bank, 15, fn, sfx),	\
	PORT_GP_1(bank, 16, fn, sfx), PORT_GP_1(bank, 17, fn, sfx),	\
	PORT_GP_1(bank, 18, fn, sfx), PORT_GP_1(bank, 19, fn, sfx),	\
	PORT_GP_1(bank, 20, fn, sfx), PORT_GP_1(bank, 21, fn, sfx),	\
	PORT_GP_1(bank, 22, fn, sfx), PORT_GP_1(bank, 23, fn, sfx),	\
	PORT_GP_1(bank, 24, fn, sfx), PORT_GP_1(bank, 25, fn, sfx),	\
	PORT_GP_1(bank, 26, fn, sfx), PORT_GP_1(bank, 27, fn, sfx),	\
	PORT_GP_1(bank, 28, fn, sfx), PORT_GP_1(bank, 29, fn, sfx),	\
	PORT_GP_1(bank, 30, fn, sfx), PORT_GP_1(bank, 31, fn, sfx)

#define PORT_GP_32_REV(bank, fn, sfx)					\
	PORT_GP_1(bank, 31, fn, sfx), PORT_GP_1(bank, 30, fn, sfx),	\
	PORT_GP_1(bank, 29, fn, sfx), PORT_GP_1(bank, 28, fn, sfx),	\
	PORT_GP_1(bank, 27, fn, sfx), PORT_GP_1(bank, 26, fn, sfx),	\
	PORT_GP_1(bank, 25, fn, sfx), PORT_GP_1(bank, 24, fn, sfx),	\
	PORT_GP_1(bank, 23, fn, sfx), PORT_GP_1(bank, 22, fn, sfx),	\
	PORT_GP_1(bank, 21, fn, sfx), PORT_GP_1(bank, 20, fn, sfx),	\
	PORT_GP_1(bank, 19, fn, sfx), PORT_GP_1(bank, 18, fn, sfx),	\
	PORT_GP_1(bank, 17, fn, sfx), PORT_GP_1(bank, 16, fn, sfx),	\
	PORT_GP_1(bank, 15, fn, sfx), PORT_GP_1(bank, 14, fn, sfx),	\
	PORT_GP_1(bank, 13, fn, sfx), PORT_GP_1(bank, 12, fn, sfx),	\
	PORT_GP_1(bank, 11, fn, sfx), PORT_GP_1(bank, 10, fn, sfx),	\
	PORT_GP_1(bank, 9,  fn, sfx), PORT_GP_1(bank, 8,  fn, sfx),	\
	PORT_GP_1(bank, 7,  fn, sfx), PORT_GP_1(bank, 6,  fn, sfx),	\
	PORT_GP_1(bank, 5,  fn, sfx), PORT_GP_1(bank, 4,  fn, sfx),	\
	PORT_GP_1(bank, 3,  fn, sfx), PORT_GP_1(bank, 2,  fn, sfx),	\
	PORT_GP_1(bank, 1,  fn, sfx), PORT_GP_1(bank, 0,  fn, sfx)

/* GP_ALL(suffix) - Expand to a list of GP_#_#_suffix */
#define _GP_ALL(bank, pin, name, sfx)	name##_##sfx
#define GP_ALL(str)			CPU_ALL_PORT(_GP_ALL, str)

/* PINMUX_GPIO_GP_ALL - Expand to a list of sh_pfc_pin entries */
#define _GP_GPIO(bank, _pin, _name, sfx)				\
	[(bank * 32) + _pin] = {					\
		.pin = (bank * 32) + _pin,				\
		.name = __stringify(_name),				\
		.enum_id = _name##_DATA,				\
	}
#define PINMUX_GPIO_GP_ALL()		CPU_ALL_PORT(_GP_GPIO, unused)

/* PINMUX_DATA_GP_ALL -  Expand to a list of name_DATA, name_FN marks */
#define _GP_DATA(bank, pin, name, sfx)	PINMUX_DATA(name##_DATA, name##_FN)
#define PINMUX_DATA_GP_ALL()		CPU_ALL_PORT(_GP_DATA, unused)

/*
 * PORT style (linear pin space)
 */

#define PORT_1(pn, fn, pfx, sfx) fn(pn, pfx, sfx)

#define PORT_10(pn, fn, pfx, sfx)					  \
	PORT_1(pn,   fn, pfx##0, sfx), PORT_1(pn+1, fn, pfx##1, sfx),	  \
	PORT_1(pn+2, fn, pfx##2, sfx), PORT_1(pn+3, fn, pfx##3, sfx),	  \
	PORT_1(pn+4, fn, pfx##4, sfx), PORT_1(pn+5, fn, pfx##5, sfx),	  \
	PORT_1(pn+6, fn, pfx##6, sfx), PORT_1(pn+7, fn, pfx##7, sfx),	  \
	PORT_1(pn+8, fn, pfx##8, sfx), PORT_1(pn+9, fn, pfx##9, sfx)

#define PORT_90(pn, fn, pfx, sfx)					  \
	PORT_10(pn+10, fn, pfx##1, sfx), PORT_10(pn+20, fn, pfx##2, sfx), \
	PORT_10(pn+30, fn, pfx##3, sfx), PORT_10(pn+40, fn, pfx##4, sfx), \
	PORT_10(pn+50, fn, pfx##5, sfx), PORT_10(pn+60, fn, pfx##6, sfx), \
	PORT_10(pn+70, fn, pfx##7, sfx), PORT_10(pn+80, fn, pfx##8, sfx), \
	PORT_10(pn+90, fn, pfx##9, sfx)

/* PORT_ALL(suffix) - Expand to a list of PORT_#_suffix */
#define _PORT_ALL(pn, pfx, sfx)		pfx##_##sfx
#define PORT_ALL(str)			CPU_ALL_PORT(_PORT_ALL, PORT, str)

/* PINMUX_GPIO - Expand to a sh_pfc_pin entry */
#define PINMUX_GPIO(_pin)						\
	[GPIO_##_pin] = {						\
		.pin = (u16)-1,						\
		.name = __stringify(name),				\
		.enum_id = _pin##_DATA,					\
	}

/* SH_PFC_PIN_CFG - Expand to a sh_pfc_pin entry (named PORT#) with config */
#define SH_PFC_PIN_CFG(_pin, cfgs)					\
	{								\
		.pin = _pin,						\
		.name = __stringify(PORT##_pin),			\
		.enum_id = PORT##_pin##_DATA,				\
		.configs = cfgs,					\
	}

/* PINMUX_DATA_ALL - Expand to a list of PORT_name_DATA, PORT_name_FN0,
 *		     PORT_name_OUT, PORT_name_IN marks
 */
#define _PORT_DATA(pn, pfx, sfx)					\
	PINMUX_DATA(PORT##pfx##_DATA, PORT##pfx##_FN0,			\
		    PORT##pfx##_OUT, PORT##pfx##_IN)
#define PINMUX_DATA_ALL()		CPU_ALL_PORT(_PORT_DATA, , unused)

/* GPIO_FN(name) - Expand to a sh_pfc_pin entry for a function GPIO */
#define PINMUX_GPIO_FN(gpio, base, data_or_mark)			\
	[gpio - (base)] = {						\
		.name = __stringify(gpio),				\
		.enum_id = data_or_mark,				\
	}
#define GPIO_FN(str)							\
	PINMUX_GPIO_FN(GPIO_FN_##str, PINMUX_FN_BASE, str##_MARK)

/*
 * PORTnCR macro
 */
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
