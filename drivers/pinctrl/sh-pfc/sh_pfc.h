/* SPDX-License-Identifier: GPL-2.0
 *
 * SuperH Pin Function Controller Support
 *
 * Copyright (c) 2008 Magnus Damm
 */

#ifndef __SH_PFC_H
#define __SH_PFC_H

#include <linux/bug.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/spinlock.h>
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
#define SH_PFC_PIN_CFG_IO_VOLTAGE	(1 << 4)
#define SH_PFC_PIN_CFG_DRIVE_STRENGTH	(1 << 5)
#define SH_PFC_PIN_CFG_NO_GPIO		(1 << 31)

struct sh_pfc_pin {
	u16 pin;
	u16 enum_id;
	const char *name;
	unsigned int configs;
};

#define SH_PFC_PIN_GROUP_ALIAS(alias, n)		\
	{						\
		.name = #alias,				\
		.pins = n##_pins,			\
		.mux = n##_mux,				\
		.nr_pins = ARRAY_SIZE(n##_pins) +	\
		BUILD_BUG_ON_ZERO(sizeof(n##_pins) != sizeof(n##_mux)), \
	}
#define SH_PFC_PIN_GROUP(n)	SH_PFC_PIN_GROUP_ALIAS(n, n)

struct sh_pfc_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int *mux;
	unsigned int nr_pins;
};

/*
 * Using union vin_data{,12,16} saves memory occupied by the VIN data pins.
 * VIN_DATA_PIN_GROUP() is a macro used to describe the VIN pin groups
 * in this case. It accepts an optional 'version' argument used when the
 * same group can appear on a different set of pins.
 */
#define VIN_DATA_PIN_GROUP(n, s, ...)					\
	{								\
		.name = #n#s#__VA_ARGS__,				\
		.pins = n##__VA_ARGS__##_pins.data##s,			\
		.mux = n##__VA_ARGS__##_mux.data##s,			\
		.nr_pins = ARRAY_SIZE(n##__VA_ARGS__##_pins.data##s),	\
	}

union vin_data12 {
	unsigned int data12[12];
	unsigned int data10[10];
	unsigned int data8[8];
};

union vin_data16 {
	unsigned int data16[16];
	unsigned int data12[12];
	unsigned int data10[10];
	unsigned int data8[8];
};

union vin_data {
	unsigned int data24[24];
	unsigned int data20[20];
	unsigned int data16[16];
	unsigned int data12[12];
	unsigned int data10[10];
	unsigned int data8[8];
	unsigned int data4[4];
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
	u32 reg;
	u8 reg_width, field_width;
	const u16 *enum_ids;
	const u8 *var_field_width;
};

#define GROUP(...)	__VA_ARGS__

/*
 * Describe a config register consisting of several fields of the same width
 *   - name: Register name (unused, for documentation purposes only)
 *   - r: Physical register address
 *   - r_width: Width of the register (in bits)
 *   - f_width: Width of the fixed-width register fields (in bits)
 *   - ids: For each register field (from left to right, i.e. MSB to LSB),
 *          2^f_width enum IDs must be specified, one for each possible
 *          combination of the register field bit values, all wrapped using
 *          the GROUP() macro.
 */
#define PINMUX_CFG_REG(name, r, r_width, f_width, ids)			\
	.reg = r, .reg_width = r_width,					\
	.field_width = f_width + BUILD_BUG_ON_ZERO(r_width % f_width),	\
	.enum_ids = (const u16 [(r_width / f_width) * (1 << f_width)])	\
		{ ids }

/*
 * Describe a config register consisting of several fields of different widths
 *   - name: Register name (unused, for documentation purposes only)
 *   - r: Physical register address
 *   - r_width: Width of the register (in bits)
 *   - f_widths: List of widths of the register fields (in bits), from left
 *               to right (i.e. MSB to LSB), wrapped using the GROUP() macro.
 *   - ids: For each register field (from left to right, i.e. MSB to LSB),
 *          2^f_widths[i] enum IDs must be specified, one for each possible
 *          combination of the register field bit values, all wrapped using
 *          the GROUP() macro.
 */
#define PINMUX_CFG_REG_VAR(name, r, r_width, f_widths, ids)		\
	.reg = r, .reg_width = r_width,					\
	.var_field_width = (const u8 []) { f_widths, 0 },		\
	.enum_ids = (const u16 []) { ids }

struct pinmux_drive_reg_field {
	u16 pin;
	u8 offset;
	u8 size;
};

struct pinmux_drive_reg {
	u32 reg;
	const struct pinmux_drive_reg_field fields[8];
};

#define PINMUX_DRIVE_REG(name, r) \
	.reg = r, \
	.fields =

struct pinmux_bias_reg {
	u32 puen;		/* Pull-enable or pull-up control register */
	u32 pud;		/* Pull-up/down control register (optional) */
	const u16 pins[32];
};

#define PINMUX_BIAS_REG(name1, r1, name2, r2) \
	.puen = r1,	\
	.pud = r2,	\
	.pins =

struct pinmux_ioctrl_reg {
	u32 reg;
};

struct pinmux_data_reg {
	u32 reg;
	u8 reg_width;
	const u16 *enum_ids;
};

/*
 * Describe a data register
 *   - name: Register name (unused, for documentation purposes only)
 *   - r: Physical register address
 *   - r_width: Width of the register (in bits)
 *   - ids: For each register bit (from left to right, i.e. MSB to LSB), one
 *          enum ID must be specified, all wrapped using the GROUP() macro.
 */
#define PINMUX_DATA_REG(name, r, r_width, ids)				\
	.reg = r, .reg_width = r_width,					\
	.enum_ids = (const u16 [r_width]) { ids }

struct pinmux_irq {
	const short *gpios;
};

/*
 * Describe the mapping from GPIOs to a single IRQ
 *   - ids...: List of GPIOs that are mapped to the same IRQ
 */
#define PINMUX_IRQ(ids...)			   \
	{ .gpios = (const short []) { ids, -1 } }

struct pinmux_range {
	u16 begin;
	u16 end;
	u16 force;
};

struct sh_pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc_pin_range;

struct sh_pfc {
	struct device *dev;
	const struct sh_pfc_soc_info *info;
	spinlock_t lock;

	unsigned int num_windows;
	struct sh_pfc_window *windows;
	unsigned int num_irqs;
	unsigned int *irqs;

	struct sh_pfc_pin_range *ranges;
	unsigned int nr_ranges;

	unsigned int nr_gpio_pins;

	struct sh_pfc_chip *gpio;
	u32 *saved_regs;
};

struct sh_pfc_soc_operations {
	int (*init)(struct sh_pfc *pfc);
	unsigned int (*get_bias)(struct sh_pfc *pfc, unsigned int pin);
	void (*set_bias)(struct sh_pfc *pfc, unsigned int pin,
			 unsigned int bias);
	int (*pin_to_pocctrl)(struct sh_pfc *pfc, unsigned int pin, u32 *pocctrl);
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

#ifdef CONFIG_PINCTRL_SH_FUNC_GPIO
	const struct pinmux_func *func_gpios;
	unsigned int nr_func_gpios;
#endif

	const struct pinmux_cfg_reg *cfg_regs;
	const struct pinmux_drive_reg *drive_regs;
	const struct pinmux_bias_reg *bias_regs;
	const struct pinmux_ioctrl_reg *ioctrl_regs;
	const struct pinmux_data_reg *data_regs;

	const u16 *pinmux_data;
	unsigned int pinmux_data_size;

	const struct pinmux_irq *gpio_irq;
	unsigned int gpio_irq_size;

	u32 unlock_reg;
};

extern const struct sh_pfc_soc_info emev2_pinmux_info;
extern const struct sh_pfc_soc_info r8a73a4_pinmux_info;
extern const struct sh_pfc_soc_info r8a7740_pinmux_info;
extern const struct sh_pfc_soc_info r8a7743_pinmux_info;
extern const struct sh_pfc_soc_info r8a7744_pinmux_info;
extern const struct sh_pfc_soc_info r8a7745_pinmux_info;
extern const struct sh_pfc_soc_info r8a77470_pinmux_info;
extern const struct sh_pfc_soc_info r8a774a1_pinmux_info;
extern const struct sh_pfc_soc_info r8a774c0_pinmux_info;
extern const struct sh_pfc_soc_info r8a7778_pinmux_info;
extern const struct sh_pfc_soc_info r8a7779_pinmux_info;
extern const struct sh_pfc_soc_info r8a7790_pinmux_info;
extern const struct sh_pfc_soc_info r8a7791_pinmux_info;
extern const struct sh_pfc_soc_info r8a7792_pinmux_info;
extern const struct sh_pfc_soc_info r8a7793_pinmux_info;
extern const struct sh_pfc_soc_info r8a7794_pinmux_info;
extern const struct sh_pfc_soc_info r8a7795_pinmux_info;
extern const struct sh_pfc_soc_info r8a7795es1_pinmux_info;
extern const struct sh_pfc_soc_info r8a7796_pinmux_info;
extern const struct sh_pfc_soc_info r8a77965_pinmux_info;
extern const struct sh_pfc_soc_info r8a77970_pinmux_info;
extern const struct sh_pfc_soc_info r8a77980_pinmux_info;
extern const struct sh_pfc_soc_info r8a77990_pinmux_info;
extern const struct sh_pfc_soc_info r8a77995_pinmux_info;
extern const struct sh_pfc_soc_info sh7203_pinmux_info;
extern const struct sh_pfc_soc_info sh7264_pinmux_info;
extern const struct sh_pfc_soc_info sh7269_pinmux_info;
extern const struct sh_pfc_soc_info sh73a0_pinmux_info;
extern const struct sh_pfc_soc_info sh7720_pinmux_info;
extern const struct sh_pfc_soc_info sh7722_pinmux_info;
extern const struct sh_pfc_soc_info sh7723_pinmux_info;
extern const struct sh_pfc_soc_info sh7724_pinmux_info;
extern const struct sh_pfc_soc_info sh7734_pinmux_info;
extern const struct sh_pfc_soc_info sh7757_pinmux_info;
extern const struct sh_pfc_soc_info sh7785_pinmux_info;
extern const struct sh_pfc_soc_info sh7786_pinmux_info;
extern const struct sh_pfc_soc_info shx3_pinmux_info;

/* -----------------------------------------------------------------------------
 * Helper macros to create pin and port lists
 */

/*
 * sh_pfc_soc_info pinmux_data array macros
 */

/*
 * Describe generic pinmux data
 *   - data_or_mark: *_DATA or *_MARK enum ID
 *   - ids...: List of enum IDs to associate with data_or_mark
 */
#define PINMUX_DATA(data_or_mark, ids...)	data_or_mark, ids, 0

/*
 * Describe a pinmux configuration without GPIO function that needs
 * configuration in a Peripheral Function Select Register (IPSR)
 *   - ipsr: IPSR field (unused, for documentation purposes only)
 *   - fn: Function name, referring to a field in the IPSR
 */
#define PINMUX_IPSR_NOGP(ipsr, fn)					\
	PINMUX_DATA(fn##_MARK, FN_##fn)

/*
 * Describe a pinmux configuration with GPIO function that needs configuration
 * in both a Peripheral Function Select Register (IPSR) and in a
 * GPIO/Peripheral Function Select Register (GPSR)
 *   - ipsr: IPSR field
 *   - fn: Function name, also referring to the IPSR field
 */
#define PINMUX_IPSR_GPSR(ipsr, fn)					\
	PINMUX_DATA(fn##_MARK, FN_##fn, FN_##ipsr)

/*
 * Describe a pinmux configuration without GPIO function that needs
 * configuration in a Peripheral Function Select Register (IPSR), and where the
 * pinmux function has a representation in a Module Select Register (MOD_SEL).
 *   - ipsr: IPSR field (unused, for documentation purposes only)
 *   - fn: Function name, also referring to the IPSR field
 *   - msel: Module selector
 */
#define PINMUX_IPSR_NOGM(ipsr, fn, msel)				\
	PINMUX_DATA(fn##_MARK, FN_##fn, FN_##msel)

/*
 * Describe a pinmux configuration with GPIO function where the pinmux function
 * has no representation in a Peripheral Function Select Register (IPSR), but
 * instead solely depends on a group selection.
 *   - gpsr: GPSR field
 *   - fn: Function name, also referring to the GPSR field
 *   - gsel: Group selector
 */
#define PINMUX_IPSR_NOFN(gpsr, fn, gsel)				\
	PINMUX_DATA(fn##_MARK, FN_##gpsr, FN_##gsel)

/*
 * Describe a pinmux configuration with GPIO function that needs configuration
 * in both a Peripheral Function Select Register (IPSR) and a GPIO/Peripheral
 * Function Select Register (GPSR), and where the pinmux function has a
 * representation in a Module Select Register (MOD_SEL).
 *   - ipsr: IPSR field
 *   - fn: Function name, also referring to the IPSR field
 *   - msel: Module selector
 */
#define PINMUX_IPSR_MSEL(ipsr, fn, msel)				\
	PINMUX_DATA(fn##_MARK, FN_##msel, FN_##fn, FN_##ipsr)

/*
 * Describe a pinmux configuration similar to PINMUX_IPSR_MSEL, but with
 * an additional select register that controls physical multiplexing
 * with another pin.
 *   - ipsr: IPSR field
 *   - fn: Function name, also referring to the IPSR field
 *   - psel: Physical multiplexing selector
 *   - msel: Module selector
 */
#define PINMUX_IPSR_PHYS_MSEL(ipsr, fn, psel, msel) \
	PINMUX_DATA(fn##_MARK, FN_##psel, FN_##msel, FN_##fn, FN_##ipsr)

/*
 * Describe a pinmux configuration in which a pin is physically multiplexed
 * with other pins.
 *   - ipsr: IPSR field
 *   - fn: Function name, also referring to the IPSR field
 *   - psel: Physical multiplexing selector
 */
#define PINMUX_IPSR_PHYS(ipsr, fn, psel) \
	PINMUX_DATA(fn##_MARK, FN_##psel)

/*
 * Describe a pinmux configuration for a single-function pin with GPIO
 * capability.
 *   - fn: Function name
 */
#define PINMUX_SINGLE(fn)						\
	PINMUX_DATA(fn##_MARK, FN_##fn)

/*
 * GP port style (32 ports banks)
 */

#define PORT_GP_CFG_1(bank, pin, fn, sfx, cfg)				\
	fn(bank, pin, GP_##bank##_##pin, sfx, cfg)
#define PORT_GP_1(bank, pin, fn, sfx)	PORT_GP_CFG_1(bank, pin, fn, sfx, 0)

#define PORT_GP_CFG_4(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_1(bank, 0,  fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 1,  fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 2,  fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 3,  fn, sfx, cfg)
#define PORT_GP_4(bank, fn, sfx)	PORT_GP_CFG_4(bank, fn, sfx, 0)

#define PORT_GP_CFG_6(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_4(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 4,  fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 5,  fn, sfx, cfg)
#define PORT_GP_6(bank, fn, sfx)	PORT_GP_CFG_6(bank, fn, sfx, 0)

#define PORT_GP_CFG_8(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_6(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 6,  fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 7,  fn, sfx, cfg)
#define PORT_GP_8(bank, fn, sfx)	PORT_GP_CFG_8(bank, fn, sfx, 0)

#define PORT_GP_CFG_9(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_8(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 8,  fn, sfx, cfg)
#define PORT_GP_9(bank, fn, sfx)	PORT_GP_CFG_9(bank, fn, sfx, 0)

#define PORT_GP_CFG_10(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_9(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 9,  fn, sfx, cfg)
#define PORT_GP_10(bank, fn, sfx)	PORT_GP_CFG_10(bank, fn, sfx, 0)

#define PORT_GP_CFG_11(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_10(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 10, fn, sfx, cfg)
#define PORT_GP_11(bank, fn, sfx)	PORT_GP_CFG_11(bank, fn, sfx, 0)

#define PORT_GP_CFG_12(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_11(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 11, fn, sfx, cfg)
#define PORT_GP_12(bank, fn, sfx)	PORT_GP_CFG_12(bank, fn, sfx, 0)

#define PORT_GP_CFG_14(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_12(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 12, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 13, fn, sfx, cfg)
#define PORT_GP_14(bank, fn, sfx)	PORT_GP_CFG_14(bank, fn, sfx, 0)

#define PORT_GP_CFG_15(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_14(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 14, fn, sfx, cfg)
#define PORT_GP_15(bank, fn, sfx)	PORT_GP_CFG_15(bank, fn, sfx, 0)

#define PORT_GP_CFG_16(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_15(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 15, fn, sfx, cfg)
#define PORT_GP_16(bank, fn, sfx)	PORT_GP_CFG_16(bank, fn, sfx, 0)

#define PORT_GP_CFG_17(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_16(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 16, fn, sfx, cfg)
#define PORT_GP_17(bank, fn, sfx)	PORT_GP_CFG_17(bank, fn, sfx, 0)

#define PORT_GP_CFG_18(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_17(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 17, fn, sfx, cfg)
#define PORT_GP_18(bank, fn, sfx)	PORT_GP_CFG_18(bank, fn, sfx, 0)

#define PORT_GP_CFG_20(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_18(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 18, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 19, fn, sfx, cfg)
#define PORT_GP_20(bank, fn, sfx)	PORT_GP_CFG_20(bank, fn, sfx, 0)

#define PORT_GP_CFG_21(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_20(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 20, fn, sfx, cfg)
#define PORT_GP_21(bank, fn, sfx)	PORT_GP_CFG_21(bank, fn, sfx, 0)

#define PORT_GP_CFG_22(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_21(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 21, fn, sfx, cfg)
#define PORT_GP_22(bank, fn, sfx)	PORT_GP_CFG_22(bank, fn, sfx, 0)

#define PORT_GP_CFG_23(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_22(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 22, fn, sfx, cfg)
#define PORT_GP_23(bank, fn, sfx)	PORT_GP_CFG_23(bank, fn, sfx, 0)

#define PORT_GP_CFG_24(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_23(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 23, fn, sfx, cfg)
#define PORT_GP_24(bank, fn, sfx)	PORT_GP_CFG_24(bank, fn, sfx, 0)

#define PORT_GP_CFG_25(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_24(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 24, fn, sfx, cfg)
#define PORT_GP_25(bank, fn, sfx)	PORT_GP_CFG_25(bank, fn, sfx, 0)

#define PORT_GP_CFG_26(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_25(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 25, fn, sfx, cfg)
#define PORT_GP_26(bank, fn, sfx)	PORT_GP_CFG_26(bank, fn, sfx, 0)

#define PORT_GP_CFG_28(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_26(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 26, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 27, fn, sfx, cfg)
#define PORT_GP_28(bank, fn, sfx)	PORT_GP_CFG_28(bank, fn, sfx, 0)

#define PORT_GP_CFG_29(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_28(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 28, fn, sfx, cfg)
#define PORT_GP_29(bank, fn, sfx)	PORT_GP_CFG_29(bank, fn, sfx, 0)

#define PORT_GP_CFG_30(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_29(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 29, fn, sfx, cfg)
#define PORT_GP_30(bank, fn, sfx)	PORT_GP_CFG_30(bank, fn, sfx, 0)

#define PORT_GP_CFG_32(bank, fn, sfx, cfg)				\
	PORT_GP_CFG_30(bank, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 30, fn, sfx, cfg),				\
	PORT_GP_CFG_1(bank, 31, fn, sfx, cfg)
#define PORT_GP_32(bank, fn, sfx)	PORT_GP_CFG_32(bank, fn, sfx, 0)

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
#define _GP_ALL(bank, pin, name, sfx, cfg)	name##_##sfx
#define GP_ALL(str)			CPU_ALL_PORT(_GP_ALL, str)

/* PINMUX_GPIO_GP_ALL - Expand to a list of sh_pfc_pin entries */
#define _GP_GPIO(bank, _pin, _name, sfx, cfg)				\
	{								\
		.pin = (bank * 32) + _pin,				\
		.name = __stringify(_name),				\
		.enum_id = _name##_DATA,				\
		.configs = cfg,						\
	}
#define PINMUX_GPIO_GP_ALL()		CPU_ALL_PORT(_GP_GPIO, unused)

/* PINMUX_DATA_GP_ALL -  Expand to a list of name_DATA, name_FN marks */
#define _GP_DATA(bank, pin, name, sfx, cfg)	PINMUX_DATA(name##_DATA, name##_FN)
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
		.name = __stringify(GPIO_##_pin),			\
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

/* SH_PFC_PIN_NAMED - Expand to a sh_pfc_pin entry with the given name */
#define SH_PFC_PIN_NAMED(row, col, _name)				\
	{								\
		.pin = PIN_NUMBER(row, col),				\
		.name = __stringify(PIN_##_name),			\
		.configs = SH_PFC_PIN_CFG_NO_GPIO,			\
	}

/* SH_PFC_PIN_NAMED_CFG - Expand to a sh_pfc_pin entry with the given name */
#define SH_PFC_PIN_NAMED_CFG(row, col, _name, cfgs)			\
	{								\
		.pin = PIN_NUMBER(row, col),				\
		.name = __stringify(PIN_##_name),			\
		.configs = SH_PFC_PIN_CFG_NO_GPIO | cfgs,		\
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
 * PORTnCR helper macro for SH-Mobile/R-Mobile
 */
#define PORTCR(nr, reg)							\
	{								\
		PINMUX_CFG_REG_VAR("PORT" nr "CR", reg, 8,		\
				   GROUP(2, 2, 1, 3),			\
				   GROUP(				\
			/* PULMD[1:0], handled by .set_bias() */	\
			0, 0, 0, 0,					\
			/* IE and OE */					\
			0, PORT##nr##_OUT, PORT##nr##_IN, 0,		\
			/* SEC, not supported */			\
			0, 0,						\
			/* PTMD[2:0] */					\
			PORT##nr##_FN0, PORT##nr##_FN1,			\
			PORT##nr##_FN2, PORT##nr##_FN3,			\
			PORT##nr##_FN4, PORT##nr##_FN5,			\
			PORT##nr##_FN6, PORT##nr##_FN7			\
		))							\
	}

/*
 * GPIO number helper macro for R-Car
 */
#define RCAR_GP_PIN(bank, pin)		(((bank) * 32) + (pin))

#endif /* __SH_PFC_H */
