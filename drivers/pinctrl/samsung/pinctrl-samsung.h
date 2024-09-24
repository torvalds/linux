/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * pin-controller/pin-mux/pin-config/gpio-driver for Samsung's SoC's.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 */

#ifndef __PINCTRL_SAMSUNG_H
#define __PINCTRL_SAMSUNG_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>

#include <linux/gpio/driver.h>

/**
 * enum pincfg_type - possible pin configuration types supported.
 * @PINCFG_TYPE_FUNC: Function configuration.
 * @PINCFG_TYPE_DAT: Pin value configuration.
 * @PINCFG_TYPE_PUD: Pull up/down configuration.
 * @PINCFG_TYPE_DRV: Drive strength configuration.
 * @PINCFG_TYPE_CON_PDN: Pin function in power down mode.
 * @PINCFG_TYPE_PUD_PDN: Pull up/down configuration in power down mode.
 */
enum pincfg_type {
	PINCFG_TYPE_FUNC,
	PINCFG_TYPE_DAT,
	PINCFG_TYPE_PUD,
	PINCFG_TYPE_DRV,
	PINCFG_TYPE_CON_PDN,
	PINCFG_TYPE_PUD_PDN,

	PINCFG_TYPE_NUM
};

/*
 * pin configuration (pull up/down and drive strength) type and its value are
 * packed together into a 16-bits. The upper 8-bits represent the configuration
 * type and the lower 8-bits hold the value of the configuration type.
 */
#define PINCFG_TYPE_MASK		0xFF
#define PINCFG_VALUE_SHIFT		8
#define PINCFG_VALUE_MASK		(0xFF << PINCFG_VALUE_SHIFT)
#define PINCFG_PACK(type, value)	(((value) << PINCFG_VALUE_SHIFT) | type)
#define PINCFG_UNPACK_TYPE(cfg)		((cfg) & PINCFG_TYPE_MASK)
#define PINCFG_UNPACK_VALUE(cfg)	(((cfg) & PINCFG_VALUE_MASK) >> \
						PINCFG_VALUE_SHIFT)
/*
 * Values for the pin CON register, choosing pin function.
 * The basic set (input and output) are same between: S3C24xx, S3C64xx, S5PV210,
 * Exynos ARMv7, Exynos ARMv8, Tesla FSD.
 */
#define PIN_CON_FUNC_INPUT		0x0
#define PIN_CON_FUNC_OUTPUT		0x1

/* Values for the pin PUD register */
#define EXYNOS_PIN_PUD_PULL_DISABLE	0x0
#define EXYNOS_PIN_PID_PULL_DOWN	0x1
#define EXYNOS_PIN_PID_PULL_UP		0x3

/*
 * enum pud_index - Possible index values to access the pud_val array.
 * @PUD_PULL_DISABLE: Index for the value of pud disable
 * @PUD_PULL_DOWN: Index for the value of pull down enable
 * @PUD_PULL_UP: Index for the value of pull up enable
 * @PUD_MAX: Maximum value of the index
 */
enum pud_index {
	PUD_PULL_DISABLE,
	PUD_PULL_DOWN,
	PUD_PULL_UP,
	PUD_MAX,
};

/**
 * enum eint_type - possible external interrupt types.
 * @EINT_TYPE_NONE: bank does not support external interrupts
 * @EINT_TYPE_GPIO: bank supportes external gpio interrupts
 * @EINT_TYPE_WKUP: bank supportes external wakeup interrupts
 * @EINT_TYPE_WKUP_MUX: bank supports multiplexed external wakeup interrupts
 *
 * Samsung GPIO controller groups all the available pins into banks. The pins
 * in a pin bank can support external gpio interrupts or external wakeup
 * interrupts or no interrupts at all. From a software perspective, the only
 * difference between external gpio and external wakeup interrupts is that
 * the wakeup interrupts can additionally wakeup the system if it is in
 * suspended state.
 */
enum eint_type {
	EINT_TYPE_NONE,
	EINT_TYPE_GPIO,
	EINT_TYPE_WKUP,
	EINT_TYPE_WKUP_MUX,
};

/* maximum length of a pin in pin descriptor (example: "gpa0-0") */
#define PIN_NAME_LENGTH	10

#define PIN_GROUP(n, p, f)				\
	{						\
		.name		= n,			\
		.pins		= p,			\
		.num_pins	= ARRAY_SIZE(p),	\
		.func		= f			\
	}

#define PMX_FUNC(n, g)					\
	{						\
		.name		= n,			\
		.groups		= g,			\
		.num_groups	= ARRAY_SIZE(g),	\
	}

struct samsung_pinctrl_drv_data;

/**
 * struct samsung_pin_bank_type: pin bank type description
 * @fld_width: widths of configuration bitfields (0 if unavailable)
 * @reg_offset: offsets of configuration registers (don't care of width is 0)
 */
struct samsung_pin_bank_type {
	u8 fld_width[PINCFG_TYPE_NUM];
	u8 reg_offset[PINCFG_TYPE_NUM];
};

/**
 * struct samsung_pin_bank_data: represent a controller pin-bank (init data).
 * @type: type of the bank (register offsets and bitfield widths)
 * @pctl_offset: starting offset of the pin-bank registers.
 * @pctl_res_idx: index of base address for pin-bank registers.
 * @nr_pins: number of pins included in this bank.
 * @eint_func: function to set in CON register to configure pin as EINT.
 * @eint_type: type of the external interrupt supported by the bank.
 * @eint_mask: bit mask of pins which support EINT function.
 * @eint_offset: SoC-specific EINT register or interrupt offset of bank.
 * @eint_con_offset: ExynosAuto SoC-specific EINT control register offset of bank.
 * @eint_mask_offset: ExynosAuto SoC-specific EINT mask register offset of bank.
 * @eint_pend_offset: ExynosAuto SoC-specific EINT pend register offset of bank.
 * @name: name to be prefixed for each pin in this pin bank.
 */
struct samsung_pin_bank_data {
	const struct samsung_pin_bank_type *type;
	u32		pctl_offset;
	u8		pctl_res_idx;
	u8		nr_pins;
	u8		eint_func;
	enum eint_type	eint_type;
	u32		eint_mask;
	u32		eint_offset;
	u32		eint_con_offset;
	u32		eint_mask_offset;
	u32		eint_pend_offset;
	const char	*name;
};

/**
 * struct samsung_pin_bank: represent a controller pin-bank.
 * @type: type of the bank (register offsets and bitfield widths)
 * @pctl_base: base address of the pin-bank registers
 * @pctl_offset: starting offset of the pin-bank registers.
 * @nr_pins: number of pins included in this bank.
 * @eint_base: base address of the pin-bank EINT registers.
 * @eint_func: function to set in CON register to configure pin as EINT.
 * @eint_type: type of the external interrupt supported by the bank.
 * @eint_mask: bit mask of pins which support EINT function.
 * @eint_offset: SoC-specific EINT register or interrupt offset of bank.
 * @eint_con_offset: ExynosAuto SoC-specific EINT register or interrupt offset of bank.
 * @eint_mask_offset: ExynosAuto SoC-specific EINT mask register offset of bank.
 * @eint_pend_offset: ExynosAuto SoC-specific EINT pend register offset of bank.
 * @name: name to be prefixed for each pin in this pin bank.
 * @id: id of the bank, propagated to the pin range.
 * @pin_base: starting pin number of the bank.
 * @soc_priv: per-bank private data for SoC-specific code.
 * @of_node: OF node of the bank.
 * @drvdata: link to controller driver data
 * @irq_domain: IRQ domain of the bank.
 * @gpio_chip: GPIO chip of the bank.
 * @grange: linux gpio pin range supported by this bank.
 * @irq_chip: link to irq chip for external gpio and wakeup interrupts.
 * @slock: spinlock protecting bank registers
 * @pm_save: saved register values during suspend
 */
struct samsung_pin_bank {
	const struct samsung_pin_bank_type *type;
	void __iomem	*pctl_base;
	u32		pctl_offset;
	u8		nr_pins;
	void __iomem	*eint_base;
	u8		eint_func;
	enum eint_type	eint_type;
	u32		eint_mask;
	u32		eint_offset;
	u32		eint_con_offset;
	u32		eint_mask_offset;
	u32		eint_pend_offset;
	const char	*name;
	u32		id;

	u32		pin_base;
	void		*soc_priv;
	struct fwnode_handle *fwnode;
	struct samsung_pinctrl_drv_data *drvdata;
	struct irq_domain *irq_domain;
	struct gpio_chip gpio_chip;
	struct pinctrl_gpio_range grange;
	struct exynos_irq_chip *irq_chip;
	raw_spinlock_t slock;

	u32 pm_save[PINCFG_TYPE_NUM + 1]; /* +1 to handle double CON registers*/
};

/**
 * struct samsung_retention_data: runtime pin-bank retention control data.
 * @regs: array of PMU registers to control pad retention.
 * @nr_regs: number of registers in @regs array.
 * @value: value to store to registers to turn off retention.
 * @refcnt: atomic counter if retention control affects more than one bank.
 * @priv: retention control code private data
 * @enable: platform specific callback to enter retention mode.
 * @disable: platform specific callback to exit retention mode.
 **/
struct samsung_retention_ctrl {
	const u32	*regs;
	int		nr_regs;
	u32		value;
	atomic_t	*refcnt;
	void		*priv;
	void		(*enable)(struct samsung_pinctrl_drv_data *);
	void		(*disable)(struct samsung_pinctrl_drv_data *);
};

/**
 * struct samsung_retention_data: represent a pin-bank retention control data.
 * @regs: array of PMU registers to control pad retention.
 * @nr_regs: number of registers in @regs array.
 * @value: value to store to registers to turn off retention.
 * @refcnt: atomic counter if retention control affects more than one bank.
 * @init: platform specific callback to initialize retention control.
 **/
struct samsung_retention_data {
	const u32	*regs;
	int		nr_regs;
	u32		value;
	atomic_t	*refcnt;
	struct samsung_retention_ctrl *(*init)(struct samsung_pinctrl_drv_data *,
					const struct samsung_retention_data *);
};

/**
 * struct samsung_pin_ctrl: represent a pin controller.
 * @pin_banks: list of pin banks included in this controller.
 * @nr_banks: number of pin banks.
 * @nr_ext_resources: number of the extra base address for pin banks.
 * @retention_data: configuration data for retention control.
 * @eint_gpio_init: platform specific callback to setup the external gpio
 *	interrupts for the controller.
 * @eint_wkup_init: platform specific callback to setup the external wakeup
 *	interrupts for the controller.
 * @suspend: platform specific suspend callback, executed during pin controller
 *	device suspend, see samsung_pinctrl_suspend()
 * @resume: platform specific resume callback, executed during pin controller
 *	device suspend, see samsung_pinctrl_resume()
 *
 * External wakeup interrupts must define at least eint_wkup_init,
 * retention_data and suspend in order for proper suspend/resume to work.
 */
struct samsung_pin_ctrl {
	const struct samsung_pin_bank_data *pin_banks;
	unsigned int	nr_banks;
	unsigned int	nr_ext_resources;
	const struct samsung_retention_data *retention_data;

	int		(*eint_gpio_init)(struct samsung_pinctrl_drv_data *);
	int		(*eint_wkup_init)(struct samsung_pinctrl_drv_data *);
	void		(*pud_value_init)(struct samsung_pinctrl_drv_data *drvdata);
	void		(*suspend)(struct samsung_pinctrl_drv_data *);
	void		(*resume)(struct samsung_pinctrl_drv_data *);
};

/**
 * struct samsung_pinctrl_drv_data: wrapper for holding driver data together.
 * @node: global list node
 * @virt_base: register base address of the controller; this will be equal
 *             to each bank samsung_pin_bank->pctl_base and used on legacy
 *             platforms (like S3C24XX or S3C64XX) which has to access the base
 *             through samsung_pinctrl_drv_data, not samsung_pin_bank).
 * @dev: device instance representing the controller.
 * @irq: interrpt number used by the controller to notify gpio interrupts.
 * @pclk: optional bus clock if required for accessing registers
 * @ctrl: pin controller instance managed by the driver.
 * @pctl: pin controller descriptor registered with the pinctrl subsystem.
 * @pctl_dev: cookie representing pinctrl device instance.
 * @pin_groups: list of pin groups available to the driver.
 * @nr_groups: number of such pin groups.
 * @pmx_functions: list of pin functions available to the driver.
 * @nr_function: number of such pin functions.
 * @nr_pins: number of pins supported by the controller.
 * @retention_ctrl: retention control runtime data.
 * @suspend: platform specific suspend callback, executed during pin controller
 *	device suspend, see samsung_pinctrl_suspend()
 * @resume: platform specific resume callback, executed during pin controller
 *	device suspend, see samsung_pinctrl_resume()
 */
struct samsung_pinctrl_drv_data {
	struct list_head		node;
	void __iomem			*virt_base;
	struct device			*dev;
	int				irq;
	struct clk			*pclk;

	struct pinctrl_desc		pctl;
	struct pinctrl_dev		*pctl_dev;

	const struct samsung_pin_group	*pin_groups;
	unsigned int			nr_groups;
	const struct samsung_pmx_func	*pmx_functions;
	unsigned int			nr_functions;

	struct samsung_pin_bank		*pin_banks;
	unsigned int			nr_banks;
	unsigned int			nr_pins;
	unsigned int			pud_val[PUD_MAX];

	struct samsung_retention_ctrl	*retention_ctrl;

	void (*suspend)(struct samsung_pinctrl_drv_data *);
	void (*resume)(struct samsung_pinctrl_drv_data *);
};

/**
 * struct samsung_pinctrl_of_match_data: OF match device specific configuration data.
 * @ctrl: array of pin controller data.
 * @num_ctrl: size of array @ctrl.
 */
struct samsung_pinctrl_of_match_data {
	const struct samsung_pin_ctrl	*ctrl;
	unsigned int			num_ctrl;
};

/**
 * struct samsung_pin_group: represent group of pins of a pinmux function.
 * @name: name of the pin group, used to lookup the group.
 * @pins: the pins included in this group.
 * @num_pins: number of pins included in this group.
 * @func: the function number to be programmed when selected.
 */
struct samsung_pin_group {
	const char		*name;
	const unsigned int	*pins;
	u8			num_pins;
	u8			func;
};

/**
 * struct samsung_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @num_groups: number of groups included in @groups.
 */
struct samsung_pmx_func {
	const char		*name;
	const char		**groups;
	u8			num_groups;
	u32			val;
};

/* list of all exported SoC specific data */
extern const struct samsung_pinctrl_of_match_data exynos3250_of_data;
extern const struct samsung_pinctrl_of_match_data exynos4210_of_data;
extern const struct samsung_pinctrl_of_match_data exynos4x12_of_data;
extern const struct samsung_pinctrl_of_match_data exynos5250_of_data;
extern const struct samsung_pinctrl_of_match_data exynos5260_of_data;
extern const struct samsung_pinctrl_of_match_data exynos5410_of_data;
extern const struct samsung_pinctrl_of_match_data exynos5420_of_data;
extern const struct samsung_pinctrl_of_match_data exynos5433_of_data;
extern const struct samsung_pinctrl_of_match_data exynos7_of_data;
extern const struct samsung_pinctrl_of_match_data exynos7885_of_data;
extern const struct samsung_pinctrl_of_match_data exynos850_of_data;
extern const struct samsung_pinctrl_of_match_data exynosautov9_of_data;
extern const struct samsung_pinctrl_of_match_data exynosautov920_of_data;
extern const struct samsung_pinctrl_of_match_data fsd_of_data;
extern const struct samsung_pinctrl_of_match_data gs101_of_data;
extern const struct samsung_pinctrl_of_match_data s3c64xx_of_data;
extern const struct samsung_pinctrl_of_match_data s3c2412_of_data;
extern const struct samsung_pinctrl_of_match_data s3c2416_of_data;
extern const struct samsung_pinctrl_of_match_data s3c2440_of_data;
extern const struct samsung_pinctrl_of_match_data s3c2450_of_data;
extern const struct samsung_pinctrl_of_match_data s5pv210_of_data;

#endif /* __PINCTRL_SAMSUNG_H */
