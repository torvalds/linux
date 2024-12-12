// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L Pin Control and GPIO driver core
 *
 * Copyright (C) 2021 Renesas Electronics Corporation.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/rzg2l-pinctrl.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define DRV_NAME	"pinctrl-rzg2l"

/*
 * Use 16 lower bits [15:0] for pin identifier
 * Use 16 higher bits [31:16] for pin mux function
 */
#define MUX_PIN_ID_MASK		GENMASK(15, 0)
#define MUX_FUNC_MASK		GENMASK(31, 16)

/* PIN capabilities */
#define PIN_CFG_IOLH_A			BIT(0)
#define PIN_CFG_IOLH_B			BIT(1)
#define PIN_CFG_SR			BIT(2)
#define PIN_CFG_IEN			BIT(3)
#define PIN_CFG_PUPD			BIT(4)
#define PIN_CFG_IO_VMC_SD0		BIT(5)
#define PIN_CFG_IO_VMC_SD1		BIT(6)
#define PIN_CFG_IO_VMC_QSPI		BIT(7)
#define PIN_CFG_IO_VMC_ETH0		BIT(8)
#define PIN_CFG_IO_VMC_ETH1		BIT(9)
#define PIN_CFG_NF			BIT(10)	/* Digital noise filter */
#define PIN_CFG_IOLH_C			BIT(11)
#define PIN_CFG_SOFT_PS			BIT(12)
#define PIN_CFG_OEN			BIT(13)
#define PIN_CFG_NOGPIO_INT		BIT(14)
#define PIN_CFG_NOD			BIT(15)	/* N-ch Open Drain */
#define PIN_CFG_SMT			BIT(16)	/* Schmitt-trigger input control */
#define PIN_CFG_ELC			BIT(17)
#define PIN_CFG_IOLH_RZV2H		BIT(18)

#define RZG2L_SINGLE_PIN		BIT_ULL(63)	/* Dedicated pin */
#define RZG2L_VARIABLE_CFG		BIT_ULL(62)	/* Variable cfg for port pins */

#define RZG2L_MPXED_COMMON_PIN_FUNCS(group) \
					(PIN_CFG_IOLH_##group | \
					 PIN_CFG_PUPD | \
					 PIN_CFG_NF)

#define RZG2L_MPXED_PIN_FUNCS		(RZG2L_MPXED_COMMON_PIN_FUNCS(A) | \
					 PIN_CFG_SR)

#define RZG3S_MPXED_PIN_FUNCS(group)	(RZG2L_MPXED_COMMON_PIN_FUNCS(group) | \
					 PIN_CFG_SOFT_PS)

#define RZV2H_MPXED_PIN_FUNCS		(RZG2L_MPXED_COMMON_PIN_FUNCS(RZV2H) | \
					 PIN_CFG_NOD | \
					 PIN_CFG_SR | \
					 PIN_CFG_SMT)

#define RZG2L_MPXED_ETH_PIN_FUNCS(x)	((x) | PIN_CFG_NF)

#define PIN_CFG_PIN_MAP_MASK		GENMASK_ULL(61, 54)
#define PIN_CFG_PIN_REG_MASK		GENMASK_ULL(53, 46)
#define PIN_CFG_MASK			GENMASK_ULL(31, 0)

/*
 * m indicates the bitmap of supported pins, a is the register index
 * and f is pin configuration capabilities supported.
 */
#define RZG2L_GPIO_PORT_SPARSE_PACK(m, a, f)	(FIELD_PREP_CONST(PIN_CFG_PIN_MAP_MASK, (m)) | \
						 FIELD_PREP_CONST(PIN_CFG_PIN_REG_MASK, (a)) | \
						 FIELD_PREP_CONST(PIN_CFG_MASK, (f)))
#define RZG2L_GPIO_PORT_SPARSE_PACK_VARIABLE(m, a)	\
						(RZG2L_VARIABLE_CFG | \
						 RZG2L_GPIO_PORT_SPARSE_PACK(m, a, 0))

/*
 * n indicates number of pins in the port, a is the register index
 * and f is pin configuration capabilities supported.
 */
#define RZG2L_GPIO_PORT_PACK(n, a, f)	RZG2L_GPIO_PORT_SPARSE_PACK((1ULL << (n)) - 1, (a), (f))
#define RZG2L_GPIO_PORT_PACK_VARIABLE(n, a)	(RZG2L_VARIABLE_CFG | \
						 RZG2L_GPIO_PORT_PACK(n, a, 0))

#define RZG2L_SINGLE_PIN_INDEX_MASK	GENMASK_ULL(62, 56)
#define RZG2L_SINGLE_PIN_BITS_MASK	GENMASK_ULL(55, 53)
/*
 * p is the register index while referencing to SR/IEN/IOLH/FILxx
 * registers, b is the register bits (b * 8) and f is the pin
 * configuration capabilities supported.
 */
#define RZG2L_SINGLE_PIN_PACK(p, b, f)	(RZG2L_SINGLE_PIN | \
					 FIELD_PREP_CONST(RZG2L_SINGLE_PIN_INDEX_MASK, (p)) | \
					 FIELD_PREP_CONST(RZG2L_SINGLE_PIN_BITS_MASK, (b)) | \
					 FIELD_PREP_CONST(PIN_CFG_MASK, (f)))

#define RZG2L_PIN_CFG_TO_PORT_OFFSET(cfg)	((cfg) & RZG2L_SINGLE_PIN ? \
						 FIELD_GET(RZG2L_SINGLE_PIN_INDEX_MASK, (cfg)) : \
						 FIELD_GET(PIN_CFG_PIN_REG_MASK, (cfg)))

#define VARIABLE_PIN_CFG_PIN_MASK		GENMASK_ULL(54, 52)
#define VARIABLE_PIN_CFG_PORT_MASK		GENMASK_ULL(51, 47)
#define RZG2L_VARIABLE_PIN_CFG_PACK(port, pin, cfg) \
	(FIELD_PREP_CONST(VARIABLE_PIN_CFG_PIN_MASK, (pin)) | \
	 FIELD_PREP_CONST(VARIABLE_PIN_CFG_PORT_MASK, (port)) | \
	 FIELD_PREP_CONST(PIN_CFG_MASK, (cfg)))

#define P(off)			(0x0000 + (off))
#define PM(off)			(0x0100 + (off) * 2)
#define PMC(off)		(0x0200 + (off))
#define PFC(off)		(0x0400 + (off) * 4)
#define PIN(off)		(0x0800 + (off))
#define IOLH(off)		(0x1000 + (off) * 8)
#define SR(off)			(0x1400 + (off) * 8)
#define IEN(off)		(0x1800 + (off) * 8)
#define PUPD(off)		(0x1C00 + (off) * 8)
#define ISEL(off)		(0x2C00 + (off) * 8)
#define SD_CH(off, ch)		((off) + (ch) * 4)
#define ETH_POC(off, ch)	((off) + (ch) * 4)
#define QSPI			(0x3008)
#define ETH_MODE		(0x3018)
#define PFC_OEN			(0x3C40) /* known on RZ/V2H(P) only */

#define PVDD_2500		2	/* I/O domain voltage 2.5V */
#define PVDD_1800		1	/* I/O domain voltage <= 1.8V */
#define PVDD_3300		0	/* I/O domain voltage >= 3.3V */

#define PWPR_B0WI		BIT(7)	/* Bit Write Disable */
#define PWPR_PFCWE		BIT(6)	/* PFC Register Write Enable */
#define PWPR_REGWE_A		BIT(6)	/* PFC and PMC Register Write Enable on RZ/V2H(P) */
#define PWPR_REGWE_B		BIT(5)	/* OEN Register Write Enable, known only in RZ/V2H(P) */

#define PM_MASK			0x03
#define PFC_MASK		0x07
#define IEN_MASK		0x01
#define IOLH_MASK		0x03
#define SR_MASK			0x01
#define PUPD_MASK		0x03

#define PM_INPUT		0x1
#define PM_OUTPUT		0x2

#define RZG2L_PIN_ID_TO_PORT(id)	((id) / RZG2L_PINS_PER_PORT)
#define RZG2L_PIN_ID_TO_PIN(id)		((id) % RZG2L_PINS_PER_PORT)

#define RZG2L_TINT_MAX_INTERRUPT	32
#define RZG2L_TINT_IRQ_START_INDEX	9
#define RZG2L_PACK_HWIRQ(t, i)		(((t) << 16) | (i))

/* Custom pinconf parameters */
#define RENESAS_RZV2H_PIN_CONFIG_OUTPUT_IMPEDANCE	(PIN_CONFIG_END + 1)

static const struct pinconf_generic_params renesas_rzv2h_custom_bindings[] = {
	{ "renesas,output-impedance", RENESAS_RZV2H_PIN_CONFIG_OUTPUT_IMPEDANCE, 1 },
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item renesas_rzv2h_conf_items[] = {
	PCONFDUMP(RENESAS_RZV2H_PIN_CONFIG_OUTPUT_IMPEDANCE, "output-impedance", "x", true),
};
#endif

/* Read/write 8 bits register */
#define RZG2L_PCTRL_REG_ACCESS8(_read, _addr, _val)	\
	do {						\
		if (_read)				\
			_val = readb(_addr);		\
		else					\
			writeb(_val, _addr);		\
	} while (0)

/* Read/write 16 bits register */
#define RZG2L_PCTRL_REG_ACCESS16(_read, _addr, _val)	\
	do {						\
		if (_read)				\
			_val = readw(_addr);		\
		else					\
			writew(_val, _addr);		\
	} while (0)

/* Read/write 32 bits register */
#define RZG2L_PCTRL_REG_ACCESS32(_read, _addr, _val)	\
	do {						\
		if (_read)				\
			_val = readl(_addr);		\
		else					\
			writel(_val, _addr);		\
	} while (0)

/**
 * struct rzg2l_register_offsets - specific register offsets
 * @pwpr: PWPR register offset
 * @sd_ch: SD_CH register offset
 * @eth_poc: ETH_POC register offset
 */
struct rzg2l_register_offsets {
	u16 pwpr;
	u16 sd_ch;
	u16 eth_poc;
};

/**
 * enum rzg2l_iolh_index - starting indices in IOLH specific arrays
 * @RZG2L_IOLH_IDX_1V8: starting index for 1V8 power source
 * @RZG2L_IOLH_IDX_2V5: starting index for 2V5 power source
 * @RZG2L_IOLH_IDX_3V3: starting index for 3V3 power source
 * @RZG2L_IOLH_IDX_MAX: maximum index
 */
enum rzg2l_iolh_index {
	RZG2L_IOLH_IDX_1V8 = 0,
	RZG2L_IOLH_IDX_2V5 = 4,
	RZG2L_IOLH_IDX_3V3 = 8,
	RZG2L_IOLH_IDX_MAX = 12,
};

/* Maximum number of driver strength entries per power source. */
#define RZG2L_IOLH_MAX_DS_ENTRIES	(4)

/**
 * struct rzg2l_hwcfg - hardware configuration data structure
 * @regs: hardware specific register offsets
 * @iolh_groupa_ua: IOLH group A uA specific values
 * @iolh_groupb_ua: IOLH group B uA specific values
 * @iolh_groupc_ua: IOLH group C uA specific values
 * @iolh_groupb_oi: IOLH group B output impedance specific values
 * @drive_strength_ua: drive strength in uA is supported (otherwise mA is supported)
 * @func_base: base number for port function (see register PFC)
 * @oen_max_pin: the maximum pin number supporting output enable
 * @oen_max_port: the maximum port number supporting output enable
 */
struct rzg2l_hwcfg {
	const struct rzg2l_register_offsets regs;
	u16 iolh_groupa_ua[RZG2L_IOLH_IDX_MAX];
	u16 iolh_groupb_ua[RZG2L_IOLH_IDX_MAX];
	u16 iolh_groupc_ua[RZG2L_IOLH_IDX_MAX];
	u16 iolh_groupb_oi[4];
	bool drive_strength_ua;
	u8 func_base;
	u8 oen_max_pin;
	u8 oen_max_port;
};

struct rzg2l_dedicated_configs {
	const char *name;
	u64 config;
};

struct rzg2l_pinctrl;

struct rzg2l_pinctrl_data {
	const char * const *port_pins;
	const u64 *port_pin_configs;
	unsigned int n_ports;
	const struct rzg2l_dedicated_configs *dedicated_pins;
	unsigned int n_port_pins;
	unsigned int n_dedicated_pins;
	const struct rzg2l_hwcfg *hwcfg;
	const u64 *variable_pin_cfg;
	unsigned int n_variable_pin_cfg;
	unsigned int num_custom_params;
	const struct pinconf_generic_params *custom_params;
#ifdef CONFIG_DEBUG_FS
	const struct pin_config_item *custom_conf_items;
#endif
	void (*pwpr_pfc_lock_unlock)(struct rzg2l_pinctrl *pctrl, bool lock);
	void (*pmc_writeb)(struct rzg2l_pinctrl *pctrl, u8 val, u16 offset);
	u32 (*oen_read)(struct rzg2l_pinctrl *pctrl, unsigned int _pin);
	int (*oen_write)(struct rzg2l_pinctrl *pctrl, unsigned int _pin, u8 oen);
	int (*hw_to_bias_param)(unsigned int val);
	int (*bias_param_to_hw)(enum pin_config_param param);
};

/**
 * struct rzg2l_pinctrl_pin_settings - pin data
 * @power_source: power source
 * @drive_strength_ua: drive strength (in micro amps)
 */
struct rzg2l_pinctrl_pin_settings {
	u16 power_source;
	u16 drive_strength_ua;
};

/**
 * struct rzg2l_pinctrl_reg_cache - register cache structure (to be used in suspend/resume)
 * @p: P registers cache
 * @pm: PM registers cache
 * @pmc: PMC registers cache
 * @pfc: PFC registers cache
 * @iolh: IOLH registers cache
 * @ien: IEN registers cache
 * @sd_ch: SD_CH registers cache
 * @eth_poc: ET_POC registers cache
 * @eth_mode: ETH_MODE register cache
 * @qspi: QSPI registers cache
 */
struct rzg2l_pinctrl_reg_cache {
	u8	*p;
	u16	*pm;
	u8	*pmc;
	u32	*pfc;
	u32	*iolh[2];
	u32	*ien[2];
	u8	sd_ch[2];
	u8	eth_poc[2];
	u8	eth_mode;
	u8	qspi;
};

struct rzg2l_pinctrl {
	struct pinctrl_dev		*pctl;
	struct pinctrl_desc		desc;
	struct pinctrl_pin_desc		*pins;

	const struct rzg2l_pinctrl_data	*data;
	void __iomem			*base;
	struct device			*dev;

	struct clk			*clk;

	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	gpio_range;
	DECLARE_BITMAP(tint_slot, RZG2L_TINT_MAX_INTERRUPT);
	spinlock_t			bitmap_lock; /* protect tint_slot bitmap */
	unsigned int			hwirq[RZG2L_TINT_MAX_INTERRUPT];

	spinlock_t			lock; /* lock read/write registers */
	struct mutex			mutex; /* serialize adding groups and functions */

	struct rzg2l_pinctrl_pin_settings *settings;
	struct rzg2l_pinctrl_reg_cache	*cache;
	struct rzg2l_pinctrl_reg_cache	*dedicated_cache;
	atomic_t			wakeup_path;
};

static const u16 available_ps[] = { 1800, 2500, 3300 };

static u64 rzg2l_pinctrl_get_variable_pin_cfg(struct rzg2l_pinctrl *pctrl,
					      u64 pincfg,
					      unsigned int port,
					      u8 pin)
{
	unsigned int i;

	for (i = 0; i < pctrl->data->n_variable_pin_cfg; i++) {
		u64 cfg = pctrl->data->variable_pin_cfg[i];

		if (FIELD_GET(VARIABLE_PIN_CFG_PORT_MASK, cfg) == port &&
		    FIELD_GET(VARIABLE_PIN_CFG_PIN_MASK, cfg) == pin)
			return (pincfg & ~RZG2L_VARIABLE_CFG) | FIELD_GET(PIN_CFG_MASK, cfg);
	}

	return 0;
}

static const u64 r9a09g057_variable_pin_cfg[] = {
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 0, RZV2H_MPXED_PIN_FUNCS),
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 1, RZV2H_MPXED_PIN_FUNCS | PIN_CFG_IEN),
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 2, RZV2H_MPXED_PIN_FUNCS | PIN_CFG_IEN),
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 3, RZV2H_MPXED_PIN_FUNCS | PIN_CFG_IEN),
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 4, RZV2H_MPXED_PIN_FUNCS | PIN_CFG_IEN),
	RZG2L_VARIABLE_PIN_CFG_PACK(11, 5, RZV2H_MPXED_PIN_FUNCS | PIN_CFG_IEN),
};

#ifdef CONFIG_RISCV
static const u64 r9a07g043f_variable_pin_cfg[] = {
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 0, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NF |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 1, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NF |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 2, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NF |
					   PIN_CFG_IEN  | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 3, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 4, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 5, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 6, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(20, 7, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(23, 1, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(23, 2, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(23, 3, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(23, 4, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(23, 5, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 0, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 1, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 2, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 3, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 4, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NOGPIO_INT),
	RZG2L_VARIABLE_PIN_CFG_PACK(24, 5, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
					   PIN_CFG_NF |
					   PIN_CFG_NOGPIO_INT),
};
#endif

static void rzg2l_pmc_writeb(struct rzg2l_pinctrl *pctrl, u8 val, u16 offset)
{
	writeb(val, pctrl->base + offset);
}

static void rzv2h_pmc_writeb(struct rzg2l_pinctrl *pctrl, u8 val, u16 offset)
{
	const struct rzg2l_register_offsets *regs = &pctrl->data->hwcfg->regs;
	u8 pwpr;

	pwpr = readb(pctrl->base + regs->pwpr);
	writeb(pwpr | PWPR_REGWE_A, pctrl->base + regs->pwpr);
	writeb(val, pctrl->base + offset);
	writeb(pwpr & ~PWPR_REGWE_A, pctrl->base + regs->pwpr);
}

static void rzg2l_pinctrl_set_pfc_mode(struct rzg2l_pinctrl *pctrl,
				       u8 pin, u8 off, u8 func)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Set pin to 'Non-use (Hi-Z input protection)'  */
	reg = readw(pctrl->base + PM(off));
	reg &= ~(PM_MASK << (pin * 2));
	writew(reg, pctrl->base + PM(off));

	pctrl->data->pwpr_pfc_lock_unlock(pctrl, false);

	/* Temporarily switch to GPIO mode with PMC register */
	reg = readb(pctrl->base + PMC(off));
	writeb(reg & ~BIT(pin), pctrl->base + PMC(off));

	/* Select Pin function mode with PFC register */
	reg = readl(pctrl->base + PFC(off));
	reg &= ~(PFC_MASK << (pin * 4));
	writel(reg | (func << (pin * 4)), pctrl->base + PFC(off));

	/* Switch to Peripheral pin function with PMC register */
	reg = readb(pctrl->base + PMC(off));
	writeb(reg | BIT(pin), pctrl->base + PMC(off));

	pctrl->data->pwpr_pfc_lock_unlock(pctrl, true);

	spin_unlock_irqrestore(&pctrl->lock, flags);
};

static int rzg2l_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	struct function_desc *func;
	unsigned int i, *psel_val;
	struct group_desc *group;
	const unsigned int *pins;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;
	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	psel_val = func->data;
	pins = group->grp.pins;

	for (i = 0; i < group->grp.npins; i++) {
		u64 *pin_data = pctrl->desc.pins[pins[i]].drv_data;
		u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
		u32 pin = RZG2L_PIN_ID_TO_PIN(pins[i]);

		dev_dbg(pctrl->dev, "port:%u pin: %u off:%x PSEL:%u\n",
			RZG2L_PIN_ID_TO_PORT(pins[i]), pin, off, psel_val[i] - hwcfg->func_base);

		rzg2l_pinctrl_set_pfc_mode(pctrl, pin, off, psel_val[i] - hwcfg->func_base);
	}

	return 0;
};

static int rzg2l_map_add_config(struct pinctrl_map *map,
				const char *group_or_pin,
				enum pinctrl_map_type type,
				unsigned long *configs,
				unsigned int num_configs)
{
	unsigned long *cfgs;

	cfgs = kmemdup_array(configs, num_configs, sizeof(*cfgs), GFP_KERNEL);
	if (!cfgs)
		return -ENOMEM;

	map->type = type;
	map->data.configs.group_or_pin = group_or_pin;
	map->data.configs.configs = cfgs;
	map->data.configs.num_configs = num_configs;

	return 0;
}

static int rzg2l_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct device_node *parent,
				   struct pinctrl_map **map,
				   unsigned int *num_maps,
				   unsigned int *index)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned long *configs = NULL;
	unsigned int *pins, *psel_val;
	unsigned int num_pinmux = 0;
	unsigned int idx = *index;
	unsigned int num_pins, i;
	unsigned int num_configs;
	struct property *pinmux;
	struct property *prop;
	int ret, gsel, fsel;
	const char **pin_fn;
	const char *name;
	const char *pin;

	pinmux = of_find_property(np, "pinmux", NULL);
	if (pinmux)
		num_pinmux = pinmux->length / sizeof(u32);

	ret = of_property_count_strings(np, "pins");
	if (ret == -EINVAL) {
		num_pins = 0;
	} else if (ret < 0) {
		dev_err(pctrl->dev, "Invalid pins list in DT\n");
		return ret;
	} else {
		num_pins = ret;
	}

	if (!num_pinmux && !num_pins)
		return 0;

	if (num_pinmux && num_pins) {
		dev_err(pctrl->dev,
			"DT node must contain either a pinmux or pins and not both\n");
		return -EINVAL;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (num_pins && !num_configs) {
		dev_err(pctrl->dev, "DT node must contain a config\n");
		ret = -ENODEV;
		goto done;
	}

	if (num_pinmux) {
		nmaps += 1;
		if (num_configs)
			nmaps += 1;
	}

	if (num_pins)
		nmaps += num_pins;

	maps = krealloc_array(maps, nmaps, sizeof(*maps), GFP_KERNEL);
	if (!maps) {
		ret = -ENOMEM;
		goto done;
	}

	*map = maps;
	*num_maps = nmaps;
	if (num_pins) {
		of_property_for_each_string(np, "pins", prop, pin) {
			ret = rzg2l_map_add_config(&maps[idx], pin,
						   PIN_MAP_TYPE_CONFIGS_PIN,
						   configs, num_configs);
			if (ret < 0)
				goto done;

			idx++;
		}
		ret = 0;
		goto done;
	}

	pins = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*pins), GFP_KERNEL);
	psel_val = devm_kcalloc(pctrl->dev, num_pinmux, sizeof(*psel_val),
				GFP_KERNEL);
	pin_fn = devm_kzalloc(pctrl->dev, sizeof(*pin_fn), GFP_KERNEL);
	if (!pins || !psel_val || !pin_fn) {
		ret = -ENOMEM;
		goto done;
	}

	/* Collect pin locations and mux settings from DT properties */
	for (i = 0; i < num_pinmux; ++i) {
		u32 value;

		ret = of_property_read_u32_index(np, "pinmux", i, &value);
		if (ret)
			goto done;
		pins[i] = FIELD_GET(MUX_PIN_ID_MASK, value);
		psel_val[i] = FIELD_GET(MUX_FUNC_MASK, value);
	}

	if (parent) {
		name = devm_kasprintf(pctrl->dev, GFP_KERNEL, "%pOFn.%pOFn",
				      parent, np);
		if (!name) {
			ret = -ENOMEM;
			goto done;
		}
	} else {
		name = np->name;
	}

	if (num_configs) {
		ret = rzg2l_map_add_config(&maps[idx], name,
					   PIN_MAP_TYPE_CONFIGS_GROUP,
					   configs, num_configs);
		if (ret < 0)
			goto done;

		idx++;
	}

	mutex_lock(&pctrl->mutex);

	/* Register a single pin group listing all the pins we read from DT */
	gsel = pinctrl_generic_add_group(pctldev, name, pins, num_pinmux, NULL);
	if (gsel < 0) {
		ret = gsel;
		goto unlock;
	}

	/*
	 * Register a single group function where the 'data' is an array PSEL
	 * register values read from DT.
	 */
	pin_fn[0] = name;
	fsel = pinmux_generic_add_function(pctldev, name, pin_fn, 1, psel_val);
	if (fsel < 0) {
		ret = fsel;
		goto remove_group;
	}

	mutex_unlock(&pctrl->mutex);

	maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
	maps[idx].data.mux.group = name;
	maps[idx].data.mux.function = name;
	idx++;

	dev_dbg(pctrl->dev, "Parsed %pOF with %d pins\n", np, num_pinmux);
	ret = 0;
	goto done;

remove_group:
	pinctrl_generic_remove_group(pctldev, gsel);
unlock:
	mutex_unlock(&pctrl->mutex);
done:
	*index = idx;
	kfree(configs);
	return ret;
}

static void rzg2l_dt_free_map(struct pinctrl_dev *pctldev,
			      struct pinctrl_map *map,
			      unsigned int num_maps)
{
	unsigned int i;

	if (!map)
		return;

	for (i = 0; i < num_maps; ++i) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP ||
		    map[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(map[i].data.configs.configs);
	}
	kfree(map);
}

static int rzg2l_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map,
				unsigned int *num_maps)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int index;
	int ret;

	*map = NULL;
	*num_maps = 0;
	index = 0;

	for_each_child_of_node_scoped(np, child) {
		ret = rzg2l_dt_subnode_to_map(pctldev, child, np, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps == 0) {
		ret = rzg2l_dt_subnode_to_map(pctldev, np, NULL, map,
					      num_maps, &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(pctrl->dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;

done:
	rzg2l_dt_free_map(pctldev, *map, *num_maps);

	return ret;
}

static int rzg2l_validate_gpio_pin(struct rzg2l_pinctrl *pctrl,
				   u64 cfg, u32 port, u8 bit)
{
	u8 pinmap = FIELD_GET(PIN_CFG_PIN_MAP_MASK, cfg);
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(cfg);
	u64 data;

	if (!(pinmap & BIT(bit)) || port >= pctrl->data->n_port_pins)
		return -EINVAL;

	data = pctrl->data->port_pin_configs[port];
	if (off != RZG2L_PIN_CFG_TO_PORT_OFFSET(data))
		return -EINVAL;

	return 0;
}

static u32 rzg2l_read_pin_config(struct rzg2l_pinctrl *pctrl, u32 offset,
				 u8 bit, u32 mask)
{
	void __iomem *addr = pctrl->base + offset;

	/* handle _L/_H for 32-bit register read/write */
	if (bit >= 4) {
		bit -= 4;
		addr += 4;
	}

	return (readl(addr) >> (bit * 8)) & mask;
}

static void rzg2l_rmw_pin_config(struct rzg2l_pinctrl *pctrl, u32 offset,
				 u8 bit, u32 mask, u32 val)
{
	void __iomem *addr = pctrl->base + offset;
	unsigned long flags;
	u32 reg;

	/* handle _L/_H for 32-bit register read/write */
	if (bit >= 4) {
		bit -= 4;
		addr += 4;
	}

	spin_lock_irqsave(&pctrl->lock, flags);
	reg = readl(addr) & ~(mask << (bit * 8));
	writel(reg | (val << (bit * 8)), addr);
	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_caps_to_pwr_reg(const struct rzg2l_register_offsets *regs, u32 caps)
{
	if (caps & PIN_CFG_IO_VMC_SD0)
		return SD_CH(regs->sd_ch, 0);
	if (caps & PIN_CFG_IO_VMC_SD1)
		return SD_CH(regs->sd_ch, 1);
	if (caps & PIN_CFG_IO_VMC_ETH0)
		return ETH_POC(regs->eth_poc, 0);
	if (caps & PIN_CFG_IO_VMC_ETH1)
		return ETH_POC(regs->eth_poc, 1);
	if (caps & PIN_CFG_IO_VMC_QSPI)
		return QSPI;

	return -EINVAL;
}

static int rzg2l_get_power_source(struct rzg2l_pinctrl *pctrl, u32 pin, u32 caps)
{
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct rzg2l_register_offsets *regs = &hwcfg->regs;
	int pwr_reg;
	u8 val;

	if (caps & PIN_CFG_SOFT_PS)
		return pctrl->settings[pin].power_source;

	pwr_reg = rzg2l_caps_to_pwr_reg(regs, caps);
	if (pwr_reg < 0)
		return pwr_reg;

	val = readb(pctrl->base + pwr_reg);
	switch (val) {
	case PVDD_1800:
		return 1800;
	case PVDD_2500:
		return 2500;
	case PVDD_3300:
		return 3300;
	default:
		/* Should not happen. */
		return -EINVAL;
	}
}

static int rzg2l_set_power_source(struct rzg2l_pinctrl *pctrl, u32 pin, u32 caps, u32 ps)
{
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct rzg2l_register_offsets *regs = &hwcfg->regs;
	int pwr_reg;
	u8 val;

	if (caps & PIN_CFG_SOFT_PS) {
		pctrl->settings[pin].power_source = ps;
		return 0;
	}

	switch (ps) {
	case 1800:
		val = PVDD_1800;
		break;
	case 2500:
		if (!(caps & (PIN_CFG_IO_VMC_ETH0 | PIN_CFG_IO_VMC_ETH1)))
			return -EINVAL;
		val = PVDD_2500;
		break;
	case 3300:
		val = PVDD_3300;
		break;
	default:
		return -EINVAL;
	}

	pwr_reg = rzg2l_caps_to_pwr_reg(regs, caps);
	if (pwr_reg < 0)
		return pwr_reg;

	writeb(val, pctrl->base + pwr_reg);
	pctrl->settings[pin].power_source = ps;

	return 0;
}

static bool rzg2l_ps_is_supported(u16 ps)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(available_ps); i++) {
		if (available_ps[i] == ps)
			return true;
	}

	return false;
}

static enum rzg2l_iolh_index rzg2l_ps_to_iolh_idx(u16 ps)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(available_ps); i++) {
		if (available_ps[i] == ps)
			break;
	}

	/*
	 * We multiply with RZG2L_IOLH_MAX_DS_ENTRIES as we have
	 * RZG2L_IOLH_MAX_DS_ENTRIES DS values per power source
	 */
	return i * RZG2L_IOLH_MAX_DS_ENTRIES;
}

static u16 rzg2l_iolh_val_to_ua(const struct rzg2l_hwcfg *hwcfg, u32 caps, u8 val)
{
	if (caps & PIN_CFG_IOLH_A)
		return hwcfg->iolh_groupa_ua[val];

	if (caps & PIN_CFG_IOLH_B)
		return hwcfg->iolh_groupb_ua[val];

	if (caps & PIN_CFG_IOLH_C)
		return hwcfg->iolh_groupc_ua[val];

	/* Should not happen. */
	return 0;
}

static int rzg2l_iolh_ua_to_val(const struct rzg2l_hwcfg *hwcfg, u32 caps,
				enum rzg2l_iolh_index ps_index, u16 ua)
{
	const u16 *array = NULL;
	unsigned int i;

	if (caps & PIN_CFG_IOLH_A)
		array = &hwcfg->iolh_groupa_ua[ps_index];

	if (caps & PIN_CFG_IOLH_B)
		array = &hwcfg->iolh_groupb_ua[ps_index];

	if (caps & PIN_CFG_IOLH_C)
		array = &hwcfg->iolh_groupc_ua[ps_index];

	if (!array)
		return -EINVAL;

	for (i = 0; i < RZG2L_IOLH_MAX_DS_ENTRIES; i++) {
		if (array[i] == ua)
			return i;
	}

	return -EINVAL;
}

static bool rzg2l_ds_is_supported(struct rzg2l_pinctrl *pctrl, u32 caps,
				  enum rzg2l_iolh_index iolh_idx,
				  u16 ds)
{
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const u16 *array = NULL;
	unsigned int i;

	if (caps & PIN_CFG_IOLH_A)
		array = hwcfg->iolh_groupa_ua;

	if (caps & PIN_CFG_IOLH_B)
		array = hwcfg->iolh_groupb_ua;

	if (caps & PIN_CFG_IOLH_C)
		array = hwcfg->iolh_groupc_ua;

	/* Should not happen. */
	if (!array)
		return false;

	if (!array[iolh_idx])
		return false;

	for (i = 0; i < RZG2L_IOLH_MAX_DS_ENTRIES; i++) {
		if (array[iolh_idx + i] == ds)
			return true;
	}

	return false;
}

static int rzg2l_pin_to_oen_bit(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	u64 *pin_data = pctrl->desc.pins[_pin].drv_data;
	u64 caps = FIELD_GET(PIN_CFG_MASK, *pin_data);
	u8 pin = RZG2L_PIN_ID_TO_PIN(_pin);

	if (pin > pctrl->data->hwcfg->oen_max_pin)
		return -EINVAL;

	/*
	 * We can determine which Ethernet interface we're dealing with from
	 * the caps.
	 */
	if (caps & PIN_CFG_IO_VMC_ETH0)
		return 0;
	if (caps & PIN_CFG_IO_VMC_ETH1)
		return 1;

	return -EINVAL;
}

static u32 rzg2l_read_oen(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	int bit;

	bit = rzg2l_pin_to_oen_bit(pctrl, _pin);
	if (bit < 0)
		return 0;

	return !(readb(pctrl->base + ETH_MODE) & BIT(bit));
}

static int rzg2l_write_oen(struct rzg2l_pinctrl *pctrl, unsigned int _pin, u8 oen)
{
	unsigned long flags;
	int bit;
	u8 val;

	bit = rzg2l_pin_to_oen_bit(pctrl, _pin);
	if (bit < 0)
		return bit;

	spin_lock_irqsave(&pctrl->lock, flags);
	val = readb(pctrl->base + ETH_MODE);
	if (oen)
		val &= ~BIT(bit);
	else
		val |= BIT(bit);
	writeb(val, pctrl->base + ETH_MODE);
	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int rzg3s_pin_to_oen_bit(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	u64 *pin_data = pctrl->desc.pins[_pin].drv_data;
	u8 port, pin, bit;

	if (*pin_data & RZG2L_SINGLE_PIN)
		return -EINVAL;

	port = RZG2L_PIN_ID_TO_PORT(_pin);
	pin = RZG2L_PIN_ID_TO_PIN(_pin);
	if (pin > pctrl->data->hwcfg->oen_max_pin)
		return -EINVAL;

	bit = pin * 2;
	if (port == pctrl->data->hwcfg->oen_max_port)
		bit += 1;

	return bit;
}

static u32 rzg3s_oen_read(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	int bit;

	bit = rzg3s_pin_to_oen_bit(pctrl, _pin);
	if (bit < 0)
		return bit;

	return !(readb(pctrl->base + ETH_MODE) & BIT(bit));
}

static int rzg3s_oen_write(struct rzg2l_pinctrl *pctrl, unsigned int _pin, u8 oen)
{
	unsigned long flags;
	int bit;
	u8 val;

	bit = rzg3s_pin_to_oen_bit(pctrl, _pin);
	if (bit < 0)
		return bit;

	spin_lock_irqsave(&pctrl->lock, flags);
	val = readb(pctrl->base + ETH_MODE);
	if (oen)
		val &= ~BIT(bit);
	else
		val |= BIT(bit);
	writeb(val, pctrl->base + ETH_MODE);
	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int rzg2l_hw_to_bias_param(unsigned int bias)
{
	switch (bias) {
	case 0:
		return PIN_CONFIG_BIAS_DISABLE;
	case 1:
		return PIN_CONFIG_BIAS_PULL_UP;
	case 2:
		return PIN_CONFIG_BIAS_PULL_DOWN;
	default:
		break;
	}

	return -EINVAL;
}

static int rzg2l_bias_param_to_hw(enum pin_config_param param)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return 0;
	case PIN_CONFIG_BIAS_PULL_UP:
		return 1;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return 2;
	default:
		break;
	}

	return -EINVAL;
}

static int rzv2h_hw_to_bias_param(unsigned int bias)
{
	switch (bias) {
	case 0:
	case 1:
		return PIN_CONFIG_BIAS_DISABLE;
	case 2:
		return PIN_CONFIG_BIAS_PULL_DOWN;
	case 3:
		return PIN_CONFIG_BIAS_PULL_UP;
	default:
		break;
	}

	return -EINVAL;
}

static int rzv2h_bias_param_to_hw(enum pin_config_param param)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return 0;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return 2;
	case PIN_CONFIG_BIAS_PULL_UP:
		return 3;
	default:
		break;
	}

	return -EINVAL;
}

static u8 rzv2h_pin_to_oen_bit(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	static const char * const pin_names[] = { "ET0_TXC_TXCLK", "ET1_TXC_TXCLK",
						  "XSPI0_RESET0N", "XSPI0_CS0N",
						  "XSPI0_CKN", "XSPI0_CKP" };
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[_pin];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pin_names); i++) {
		if (!strcmp(pin_desc->name, pin_names[i]))
			return i;
	}

	/* Should not happen. */
	return 0;
}

static u32 rzv2h_oen_read(struct rzg2l_pinctrl *pctrl, unsigned int _pin)
{
	u8 bit;

	bit = rzv2h_pin_to_oen_bit(pctrl, _pin);

	return !(readb(pctrl->base + PFC_OEN) & BIT(bit));
}

static int rzv2h_oen_write(struct rzg2l_pinctrl *pctrl, unsigned int _pin, u8 oen)
{
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct rzg2l_register_offsets *regs = &hwcfg->regs;
	unsigned long flags;
	u8 val, bit;
	u8 pwpr;

	bit = rzv2h_pin_to_oen_bit(pctrl, _pin);
	spin_lock_irqsave(&pctrl->lock, flags);
	val = readb(pctrl->base + PFC_OEN);
	if (oen)
		val &= ~BIT(bit);
	else
		val |= BIT(bit);

	pwpr = readb(pctrl->base + regs->pwpr);
	writeb(pwpr | PWPR_REGWE_B, pctrl->base + regs->pwpr);
	writeb(val, pctrl->base + PFC_OEN);
	writeb(pwpr & ~PWPR_REGWE_B, pctrl->base + regs->pwpr);
	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int rzg2l_pinctrl_pinconf_get(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *config)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct pinctrl_pin_desc *pin = &pctrl->desc.pins[_pin];
	u32 param = pinconf_to_config_param(*config);
	u64 *pin_data = pin->drv_data;
	unsigned int arg = 0;
	u32 off;
	u32 cfg;
	int ret;
	u8 bit;

	if (!pin_data)
		return -EINVAL;

	off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	cfg = FIELD_GET(PIN_CFG_MASK, *pin_data);
	if (*pin_data & RZG2L_SINGLE_PIN) {
		bit = FIELD_GET(RZG2L_SINGLE_PIN_BITS_MASK, *pin_data);
	} else {
		bit = RZG2L_PIN_ID_TO_PIN(_pin);

		if (rzg2l_validate_gpio_pin(pctrl, *pin_data, RZG2L_PIN_ID_TO_PORT(_pin), bit))
			return -EINVAL;
	}

	switch (param) {
	case PIN_CONFIG_INPUT_ENABLE:
		if (!(cfg & PIN_CFG_IEN))
			return -EINVAL;
		arg = rzg2l_read_pin_config(pctrl, IEN(off), bit, IEN_MASK);
		if (!arg)
			return -EINVAL;
		break;

	case PIN_CONFIG_OUTPUT_ENABLE:
		if (!(cfg & PIN_CFG_OEN))
			return -EINVAL;
		if (!pctrl->data->oen_read)
			return -EOPNOTSUPP;
		arg = pctrl->data->oen_read(pctrl, _pin);
		if (!arg)
			return -EINVAL;
		break;

	case PIN_CONFIG_POWER_SOURCE:
		ret = rzg2l_get_power_source(pctrl, _pin, cfg);
		if (ret < 0)
			return ret;
		arg = ret;
		break;

	case PIN_CONFIG_SLEW_RATE:
		if (!(cfg & PIN_CFG_SR))
			return -EINVAL;

		arg = rzg2l_read_pin_config(pctrl, SR(off), bit, SR_MASK);
		break;

	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!(cfg & PIN_CFG_PUPD))
			return -EINVAL;

		arg = rzg2l_read_pin_config(pctrl, PUPD(off), bit, PUPD_MASK);
		ret = pctrl->data->hw_to_bias_param(arg);
		if (ret < 0)
			return ret;

		if (ret != param)
			return -EINVAL;
		/* for PIN_CONFIG_BIAS_PULL_UP/DOWN when enabled we just return 1 */
		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_STRENGTH: {
		unsigned int index;

		if (!(cfg & PIN_CFG_IOLH_A) || hwcfg->drive_strength_ua)
			return -EINVAL;

		index = rzg2l_read_pin_config(pctrl, IOLH(off), bit, IOLH_MASK);
		/*
		 * Drive strenght mA is supported only by group A and only
		 * for 3V3 port source.
		 */
		arg = hwcfg->iolh_groupa_ua[index + RZG2L_IOLH_IDX_3V3] / 1000;
		break;
	}

	case PIN_CONFIG_DRIVE_STRENGTH_UA: {
		enum rzg2l_iolh_index iolh_idx;
		u8 val;

		if (!(cfg & (PIN_CFG_IOLH_A | PIN_CFG_IOLH_B | PIN_CFG_IOLH_C)) ||
		    !hwcfg->drive_strength_ua)
			return -EINVAL;

		ret = rzg2l_get_power_source(pctrl, _pin, cfg);
		if (ret < 0)
			return ret;
		iolh_idx = rzg2l_ps_to_iolh_idx(ret);
		val = rzg2l_read_pin_config(pctrl, IOLH(off), bit, IOLH_MASK);
		arg = rzg2l_iolh_val_to_ua(hwcfg, cfg, iolh_idx + val);
		break;
	}

	case PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS: {
		unsigned int index;

		if (!(cfg & PIN_CFG_IOLH_B) || !hwcfg->iolh_groupb_oi[0])
			return -EINVAL;

		index = rzg2l_read_pin_config(pctrl, IOLH(off), bit, IOLH_MASK);
		arg = hwcfg->iolh_groupb_oi[index];
		break;
	}

	case RENESAS_RZV2H_PIN_CONFIG_OUTPUT_IMPEDANCE:
		if (!(cfg & PIN_CFG_IOLH_RZV2H))
			return -EINVAL;

		arg = rzg2l_read_pin_config(pctrl, IOLH(off), bit, IOLH_MASK);
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
};

static int rzg2l_pinctrl_pinconf_set(struct pinctrl_dev *pctldev,
				     unsigned int _pin,
				     unsigned long *_configs,
				     unsigned int num_configs)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct pinctrl_pin_desc *pin = &pctrl->desc.pins[_pin];
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	struct rzg2l_pinctrl_pin_settings settings = pctrl->settings[_pin];
	u64 *pin_data = pin->drv_data;
	unsigned int i, arg, index;
	u32 off, param;
	u32 cfg;
	int ret;
	u8 bit;

	if (!pin_data)
		return -EINVAL;

	off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	cfg = FIELD_GET(PIN_CFG_MASK, *pin_data);
	if (*pin_data & RZG2L_SINGLE_PIN) {
		bit = FIELD_GET(RZG2L_SINGLE_PIN_BITS_MASK, *pin_data);
	} else {
		bit = RZG2L_PIN_ID_TO_PIN(_pin);

		if (rzg2l_validate_gpio_pin(pctrl, *pin_data, RZG2L_PIN_ID_TO_PORT(_pin), bit))
			return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(_configs[i]);
		arg = pinconf_to_config_argument(_configs[i]);
		switch (param) {
		case PIN_CONFIG_INPUT_ENABLE:

			if (!(cfg & PIN_CFG_IEN))
				return -EINVAL;

			rzg2l_rmw_pin_config(pctrl, IEN(off), bit, IEN_MASK, !!arg);
			break;

		case PIN_CONFIG_OUTPUT_ENABLE:
			if (!(cfg & PIN_CFG_OEN))
				return -EINVAL;
			if (!pctrl->data->oen_write)
				return -EOPNOTSUPP;
			ret = pctrl->data->oen_write(pctrl, _pin, !!arg);
			if (ret)
				return ret;
			break;

		case PIN_CONFIG_POWER_SOURCE:
			settings.power_source = arg;
			break;

		case PIN_CONFIG_SLEW_RATE:
			if (!(cfg & PIN_CFG_SR) || arg > 1)
				return -EINVAL;

			rzg2l_rmw_pin_config(pctrl, SR(off), bit, SR_MASK, arg);
			break;

		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!(cfg & PIN_CFG_PUPD))
				return -EINVAL;

			ret = pctrl->data->bias_param_to_hw(param);
			if (ret < 0)
				return ret;

			rzg2l_rmw_pin_config(pctrl, PUPD(off), bit, PUPD_MASK, ret);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			if (!(cfg & PIN_CFG_IOLH_A) || hwcfg->drive_strength_ua)
				return -EINVAL;

			for (index = RZG2L_IOLH_IDX_3V3;
			     index < RZG2L_IOLH_IDX_3V3 + RZG2L_IOLH_MAX_DS_ENTRIES; index++) {
				if (arg == (hwcfg->iolh_groupa_ua[index] / 1000))
					break;
			}
			if (index == (RZG2L_IOLH_IDX_3V3 + RZG2L_IOLH_MAX_DS_ENTRIES))
				return -EINVAL;

			rzg2l_rmw_pin_config(pctrl, IOLH(off), bit, IOLH_MASK, index);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			if (!(cfg & (PIN_CFG_IOLH_A | PIN_CFG_IOLH_B | PIN_CFG_IOLH_C)) ||
			    !hwcfg->drive_strength_ua)
				return -EINVAL;

			settings.drive_strength_ua = arg;
			break;

		case PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS:
			if (!(cfg & PIN_CFG_IOLH_B) || !hwcfg->iolh_groupb_oi[0])
				return -EINVAL;

			for (index = 0; index < ARRAY_SIZE(hwcfg->iolh_groupb_oi); index++) {
				if (arg == hwcfg->iolh_groupb_oi[index])
					break;
			}
			if (index == ARRAY_SIZE(hwcfg->iolh_groupb_oi))
				return -EINVAL;

			rzg2l_rmw_pin_config(pctrl, IOLH(off), bit, IOLH_MASK, index);
			break;

		case RENESAS_RZV2H_PIN_CONFIG_OUTPUT_IMPEDANCE:
			if (!(cfg & PIN_CFG_IOLH_RZV2H))
				return -EINVAL;

			if (arg > 3)
				return -EINVAL;
			rzg2l_rmw_pin_config(pctrl, IOLH(off), bit, IOLH_MASK, arg);
			break;

		default:
			return -EOPNOTSUPP;
		}
	}

	/* Apply power source. */
	if (settings.power_source != pctrl->settings[_pin].power_source) {
		ret = rzg2l_ps_is_supported(settings.power_source);
		if (!ret)
			return -EINVAL;

		/* Apply power source. */
		ret = rzg2l_set_power_source(pctrl, _pin, cfg, settings.power_source);
		if (ret)
			return ret;
	}

	/* Apply drive strength. */
	if (settings.drive_strength_ua != pctrl->settings[_pin].drive_strength_ua) {
		enum rzg2l_iolh_index iolh_idx;
		int val;

		iolh_idx = rzg2l_ps_to_iolh_idx(settings.power_source);
		ret = rzg2l_ds_is_supported(pctrl, cfg, iolh_idx,
					    settings.drive_strength_ua);
		if (!ret)
			return -EINVAL;

		/* Get register value for this PS/DS tuple. */
		val = rzg2l_iolh_ua_to_val(hwcfg, cfg, iolh_idx, settings.drive_strength_ua);
		if (val < 0)
			return val;

		/* Apply drive strength. */
		rzg2l_rmw_pin_config(pctrl, IOLH(off), bit, IOLH_MASK, val);
		pctrl->settings[_pin].drive_strength_ua = settings.drive_strength_ua;
	}

	return 0;
}

static int rzg2l_pinctrl_pinconf_group_set(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   unsigned long *configs,
					   unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzg2l_pinctrl_pinconf_set(pctldev, pins[i], configs,
						num_configs);
		if (ret)
			return ret;
	}

	return 0;
};

static int rzg2l_pinctrl_pinconf_group_get(struct pinctrl_dev *pctldev,
					   unsigned int group,
					   unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, prev_config = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rzg2l_pinctrl_pinconf_get(pctldev, pins[i], config);
		if (ret)
			return ret;

		/* Check config matching between to pin  */
		if (i && prev_config != *config)
			return -EOPNOTSUPP;

		prev_config = *config;
	}

	return 0;
};

static const struct pinctrl_ops rzg2l_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = rzg2l_dt_node_to_map,
	.dt_free_map = rzg2l_dt_free_map,
};

static const struct pinmux_ops rzg2l_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzg2l_pinctrl_set_mux,
	.strict = true,
};

static const struct pinconf_ops rzg2l_pinctrl_confops = {
	.is_generic = true,
	.pin_config_get = rzg2l_pinctrl_pinconf_get,
	.pin_config_set = rzg2l_pinctrl_pinconf_set,
	.pin_config_group_set = rzg2l_pinctrl_pinconf_group_set,
	.pin_config_group_get = rzg2l_pinctrl_pinconf_group_get,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int rzg2l_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;
	int ret;

	ret = rzg2l_validate_gpio_pin(pctrl, *pin_data, port, bit);
	if (ret)
		return ret;

	ret = pinctrl_gpio_request(chip, offset);
	if (ret)
		return ret;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Select GPIO mode in PMC Register */
	reg8 = readb(pctrl->base + PMC(off));
	reg8 &= ~BIT(bit);
	pctrl->data->pmc_writeb(pctrl, reg8, PMC(off));

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void rzg2l_gpio_set_direction(struct rzg2l_pinctrl *pctrl, u32 offset,
				     bool output)
{
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u16 reg16;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg16 = readw(pctrl->base + PM(off));
	reg16 &= ~(PM_MASK << (bit * 2));

	reg16 |= (output ? PM_OUTPUT : PM_INPUT) << (bit * 2);
	writew(reg16, pctrl->base + PM(off));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	if (!(readb(pctrl->base + PMC(off)) & BIT(bit))) {
		u16 reg16;

		reg16 = readw(pctrl->base + PM(off));
		reg16 = (reg16 >> (bit * 2)) & PM_MASK;
		if (reg16 == PM_OUTPUT)
			return GPIO_LINE_DIRECTION_OUT;
	}

	return GPIO_LINE_DIRECTION_IN;
}

static int rzg2l_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);

	rzg2l_gpio_set_direction(pctrl, offset, false);

	return 0;
}

static void rzg2l_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg8 = readb(pctrl->base + P(off));

	if (value)
		writeb(reg8 | BIT(bit), pctrl->base + P(off));
	else
		writeb(reg8 & ~BIT(bit), pctrl->base + P(off));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);

	rzg2l_gpio_set(chip, offset, value);
	rzg2l_gpio_set_direction(pctrl, offset, true);

	return 0;
}

static int rzg2l_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u16 reg16;

	reg16 = readw(pctrl->base + PM(off));
	reg16 = (reg16 >> (bit * 2)) & PM_MASK;

	if (reg16 == PM_INPUT)
		return !!(readb(pctrl->base + PIN(off)) & BIT(bit));
	else if (reg16 == PM_OUTPUT)
		return !!(readb(pctrl->base + P(off)) & BIT(bit));
	else
		return -EINVAL;
}

static void rzg2l_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	unsigned int virq;

	pinctrl_gpio_free(chip, offset);

	virq = irq_find_mapping(chip->irq.domain, offset);
	if (virq)
		irq_dispose_mapping(virq);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzg2l_gpio_direction_input(chip, offset);
}

static const char * const rzg2l_gpio_names[] = {
	"P0_0", "P0_1", "P0_2", "P0_3", "P0_4", "P0_5", "P0_6", "P0_7",
	"P1_0", "P1_1", "P1_2", "P1_3", "P1_4", "P1_5", "P1_6", "P1_7",
	"P2_0", "P2_1", "P2_2", "P2_3", "P2_4", "P2_5", "P2_6", "P2_7",
	"P3_0", "P3_1", "P3_2", "P3_3", "P3_4", "P3_5", "P3_6", "P3_7",
	"P4_0", "P4_1", "P4_2", "P4_3", "P4_4", "P4_5", "P4_6", "P4_7",
	"P5_0", "P5_1", "P5_2", "P5_3", "P5_4", "P5_5", "P5_6", "P5_7",
	"P6_0", "P6_1", "P6_2", "P6_3", "P6_4", "P6_5", "P6_6", "P6_7",
	"P7_0", "P7_1", "P7_2", "P7_3", "P7_4", "P7_5", "P7_6", "P7_7",
	"P8_0", "P8_1", "P8_2", "P8_3", "P8_4", "P8_5", "P8_6", "P8_7",
	"P9_0", "P9_1", "P9_2", "P9_3", "P9_4", "P9_5", "P9_6", "P9_7",
	"P10_0", "P10_1", "P10_2", "P10_3", "P10_4", "P10_5", "P10_6", "P10_7",
	"P11_0", "P11_1", "P11_2", "P11_3", "P11_4", "P11_5", "P11_6", "P11_7",
	"P12_0", "P12_1", "P12_2", "P12_3", "P12_4", "P12_5", "P12_6", "P12_7",
	"P13_0", "P13_1", "P13_2", "P13_3", "P13_4", "P13_5", "P13_6", "P13_7",
	"P14_0", "P14_1", "P14_2", "P14_3", "P14_4", "P14_5", "P14_6", "P14_7",
	"P15_0", "P15_1", "P15_2", "P15_3", "P15_4", "P15_5", "P15_6", "P15_7",
	"P16_0", "P16_1", "P16_2", "P16_3", "P16_4", "P16_5", "P16_6", "P16_7",
	"P17_0", "P17_1", "P17_2", "P17_3", "P17_4", "P17_5", "P17_6", "P17_7",
	"P18_0", "P18_1", "P18_2", "P18_3", "P18_4", "P18_5", "P18_6", "P18_7",
	"P19_0", "P19_1", "P19_2", "P19_3", "P19_4", "P19_5", "P19_6", "P19_7",
	"P20_0", "P20_1", "P20_2", "P20_3", "P20_4", "P20_5", "P20_6", "P20_7",
	"P21_0", "P21_1", "P21_2", "P21_3", "P21_4", "P21_5", "P21_6", "P21_7",
	"P22_0", "P22_1", "P22_2", "P22_3", "P22_4", "P22_5", "P22_6", "P22_7",
	"P23_0", "P23_1", "P23_2", "P23_3", "P23_4", "P23_5", "P23_6", "P23_7",
	"P24_0", "P24_1", "P24_2", "P24_3", "P24_4", "P24_5", "P24_6", "P24_7",
	"P25_0", "P25_1", "P25_2", "P25_3", "P25_4", "P25_5", "P25_6", "P25_7",
	"P26_0", "P26_1", "P26_2", "P26_3", "P26_4", "P26_5", "P26_6", "P26_7",
	"P27_0", "P27_1", "P27_2", "P27_3", "P27_4", "P27_5", "P27_6", "P27_7",
	"P28_0", "P28_1", "P28_2", "P28_3", "P28_4", "P28_5", "P28_6", "P28_7",
	"P29_0", "P29_1", "P29_2", "P29_3", "P29_4", "P29_5", "P29_6", "P29_7",
	"P30_0", "P30_1", "P30_2", "P30_3", "P30_4", "P30_5", "P30_6", "P30_7",
	"P31_0", "P31_1", "P31_2", "P31_3", "P31_4", "P31_5", "P31_6", "P31_7",
	"P32_0", "P32_1", "P32_2", "P32_3", "P32_4", "P32_5", "P32_6", "P32_7",
	"P33_0", "P33_1", "P33_2", "P33_3", "P33_4", "P33_5", "P33_6", "P33_7",
	"P34_0", "P34_1", "P34_2", "P34_3", "P34_4", "P34_5", "P34_6", "P34_7",
	"P35_0", "P35_1", "P35_2", "P35_3", "P35_4", "P35_5", "P35_6", "P35_7",
	"P36_0", "P36_1", "P36_2", "P36_3", "P36_4", "P36_5", "P36_6", "P36_7",
	"P37_0", "P37_1", "P37_2", "P37_3", "P37_4", "P37_5", "P37_6", "P37_7",
	"P38_0", "P38_1", "P38_2", "P38_3", "P38_4", "P38_5", "P38_6", "P38_7",
	"P39_0", "P39_1", "P39_2", "P39_3", "P39_4", "P39_5", "P39_6", "P39_7",
	"P40_0", "P40_1", "P40_2", "P40_3", "P40_4", "P40_5", "P40_6", "P40_7",
	"P41_0", "P41_1", "P41_2", "P41_3", "P41_4", "P41_5", "P41_6", "P41_7",
	"P42_0", "P42_1", "P42_2", "P42_3", "P42_4", "P42_5", "P42_6", "P42_7",
	"P43_0", "P43_1", "P43_2", "P43_3", "P43_4", "P43_5", "P43_6", "P43_7",
	"P44_0", "P44_1", "P44_2", "P44_3", "P44_4", "P44_5", "P44_6", "P44_7",
	"P45_0", "P45_1", "P45_2", "P45_3", "P45_4", "P45_5", "P45_6", "P45_7",
	"P46_0", "P46_1", "P46_2", "P46_3", "P46_4", "P46_5", "P46_6", "P46_7",
	"P47_0", "P47_1", "P47_2", "P47_3", "P47_4", "P47_5", "P47_6", "P47_7",
	"P48_0", "P48_1", "P48_2", "P48_3", "P48_4", "P48_5", "P48_6", "P48_7",
};

static const u64 r9a07g044_gpio_configs[] = {
	RZG2L_GPIO_PORT_PACK(2, 0x10, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x11, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x12, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x13, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x14, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x15, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x16, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x17, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x18, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x19, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1a, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1b, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1c, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x1d, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1e, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1f, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x20, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x21, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x22, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x23, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x24, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0) | PIN_CFG_OEN),
	RZG2L_GPIO_PORT_PACK(2, 0x25, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x26, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x27, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x28, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x29, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x2a, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x2b, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x2c, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(2, 0x2d, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1) | PIN_CFG_OEN),
	RZG2L_GPIO_PORT_PACK(2, 0x2e, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x2f, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x30, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x31, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x32, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x33, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x34, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(3, 0x35, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(2, 0x36, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x37, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x38, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x39, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x3a, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x3b, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x3c, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x3d, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x3e, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x3f, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x40, RZG2L_MPXED_PIN_FUNCS),
};

static const u64 r9a07g043_gpio_configs[] = {
	RZG2L_GPIO_PORT_PACK(4, 0x10, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x11, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0) | PIN_CFG_OEN),
	RZG2L_GPIO_PORT_PACK(4, 0x12, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(4, 0x13, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(6, 0x14, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH0)),
	RZG2L_GPIO_PORT_PACK(5, 0x15, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x16, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x17, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1) | PIN_CFG_OEN),
	RZG2L_GPIO_PORT_PACK(5, 0x18, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(4, 0x19, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(5, 0x1a, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IO_VMC_ETH1)),
	RZG2L_GPIO_PORT_PACK(4, 0x1b, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x1c, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(5, 0x1d, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(3, 0x1e, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x1f, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(2, 0x20, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(4, 0x21, RZG2L_MPXED_PIN_FUNCS),
	RZG2L_GPIO_PORT_PACK(6, 0x22, RZG2L_MPXED_PIN_FUNCS),
#ifdef CONFIG_RISCV
	/* Below additional port pins (P19 - P28) are exclusively available on RZ/Five SoC only */
	RZG2L_GPIO_PORT_SPARSE_PACK(0x2, 0x06, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
				    PIN_CFG_NF | PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),	/* P19 */
	RZG2L_GPIO_PORT_PACK_VARIABLE(8, 0x07),						/* P20 */
	RZG2L_GPIO_PORT_SPARSE_PACK(0x2, 0x08, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
				    PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),			/* P21 */
	RZG2L_GPIO_PORT_PACK(4, 0x09, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_PUPD |
			     PIN_CFG_IEN | PIN_CFG_NOGPIO_INT),				/* P22 */
	RZG2L_GPIO_PORT_SPARSE_PACK_VARIABLE(0x3e, 0x0a),				/* P23 */
	RZG2L_GPIO_PORT_PACK_VARIABLE(6, 0x0b),						/* P24 */
	RZG2L_GPIO_PORT_SPARSE_PACK(0x2, 0x0c, PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_NF |
				    PIN_CFG_NOGPIO_INT),				/* P25 */
	0x0,										/* P26 */
	0x0,										/* P27 */
	RZG2L_GPIO_PORT_PACK(6, 0x0f, RZG2L_MPXED_PIN_FUNCS | PIN_CFG_NOGPIO_INT),	/* P28 */
#endif
};

static const u64 r9a08g045_gpio_configs[] = {
	RZG2L_GPIO_PORT_PACK(4, 0x20, RZG3S_MPXED_PIN_FUNCS(A)),			/* P0  */
	RZG2L_GPIO_PORT_PACK(5, 0x30, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH0)) |
				      PIN_CFG_OEN | PIN_CFG_IEN,			/* P1 */
	RZG2L_GPIO_PORT_PACK(4, 0x31, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH0)),	/* P2 */
	RZG2L_GPIO_PORT_PACK(4, 0x32, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH0)),	/* P3 */
	RZG2L_GPIO_PORT_PACK(6, 0x33, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH0)),	/* P4 */
	RZG2L_GPIO_PORT_PACK(5, 0x21, RZG3S_MPXED_PIN_FUNCS(A)),			/* P5  */
	RZG2L_GPIO_PORT_PACK(5, 0x22, RZG3S_MPXED_PIN_FUNCS(A)),			/* P6  */
	RZG2L_GPIO_PORT_PACK(5, 0x34, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH1)) |
				      PIN_CFG_OEN | PIN_CFG_IEN,			/* P7 */
	RZG2L_GPIO_PORT_PACK(5, 0x35, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH1)),	/* P8 */
	RZG2L_GPIO_PORT_PACK(4, 0x36, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH1)),	/* P9 */
	RZG2L_GPIO_PORT_PACK(5, 0x37, RZG2L_MPXED_ETH_PIN_FUNCS(PIN_CFG_IOLH_C |
								PIN_CFG_IO_VMC_ETH1)),	/* P10 */
	RZG2L_GPIO_PORT_PACK(4, 0x23, RZG3S_MPXED_PIN_FUNCS(B) | PIN_CFG_IEN),		/* P11  */
	RZG2L_GPIO_PORT_PACK(2, 0x24, RZG3S_MPXED_PIN_FUNCS(B) | PIN_CFG_IEN),		/* P12  */
	RZG2L_GPIO_PORT_PACK(5, 0x25, RZG3S_MPXED_PIN_FUNCS(A)),			/* P13  */
	RZG2L_GPIO_PORT_PACK(3, 0x26, RZG3S_MPXED_PIN_FUNCS(A)),			/* P14  */
	RZG2L_GPIO_PORT_PACK(4, 0x27, RZG3S_MPXED_PIN_FUNCS(A)),			/* P15  */
	RZG2L_GPIO_PORT_PACK(2, 0x28, RZG3S_MPXED_PIN_FUNCS(A)),			/* P16  */
	RZG2L_GPIO_PORT_PACK(4, 0x29, RZG3S_MPXED_PIN_FUNCS(A)),			/* P17  */
	RZG2L_GPIO_PORT_PACK(6, 0x2a, RZG3S_MPXED_PIN_FUNCS(A)),			/* P18 */
};

static const char * const rzv2h_gpio_names[] = {
	"P00", "P01", "P02", "P03", "P04", "P05", "P06", "P07",
	"P10", "P11", "P12", "P13", "P14", "P15", "P16", "P17",
	"P20", "P21", "P22", "P23", "P24", "P25", "P26", "P27",
	"P30", "P31", "P32", "P33", "P34", "P35", "P36", "P37",
	"P40", "P41", "P42", "P43", "P44", "P45", "P46", "P47",
	"P50", "P51", "P52", "P53", "P54", "P55", "P56", "P57",
	"P60", "P61", "P62", "P63", "P64", "P65", "P66", "P67",
	"P70", "P71", "P72", "P73", "P74", "P75", "P76", "P77",
	"P80", "P81", "P82", "P83", "P84", "P85", "P86", "P87",
	"P90", "P91", "P92", "P93", "P94", "P95", "P96", "P97",
	"PA0", "PA1", "PA2", "PA3", "PA4", "PA5", "PA6", "PA7",
	"PB0", "PB1", "PB2", "PB3", "PB4", "PB5", "PB6", "PB7",
};

static const u64 r9a09g057_gpio_configs[] = {
	RZG2L_GPIO_PORT_PACK(8, 0x20, RZV2H_MPXED_PIN_FUNCS),	/* P0 */
	RZG2L_GPIO_PORT_PACK(6, 0x21, RZV2H_MPXED_PIN_FUNCS),	/* P1 */
	RZG2L_GPIO_PORT_PACK(2, 0x22, RZG2L_MPXED_COMMON_PIN_FUNCS(RZV2H) |
				      PIN_CFG_NOD),		/* P2 */
	RZG2L_GPIO_PORT_PACK(8, 0x23, RZV2H_MPXED_PIN_FUNCS),	/* P3 */
	RZG2L_GPIO_PORT_PACK(8, 0x24, RZV2H_MPXED_PIN_FUNCS),	/* P4 */
	RZG2L_GPIO_PORT_PACK(8, 0x25, RZV2H_MPXED_PIN_FUNCS),	/* P5 */
	RZG2L_GPIO_PORT_PACK(8, 0x26, RZV2H_MPXED_PIN_FUNCS |
				      PIN_CFG_ELC),		/* P6 */
	RZG2L_GPIO_PORT_PACK(8, 0x27, RZV2H_MPXED_PIN_FUNCS),	/* P7 */
	RZG2L_GPIO_PORT_PACK(8, 0x28, RZV2H_MPXED_PIN_FUNCS |
				      PIN_CFG_ELC),		/* P8 */
	RZG2L_GPIO_PORT_PACK(8, 0x29, RZV2H_MPXED_PIN_FUNCS),	/* P9 */
	RZG2L_GPIO_PORT_PACK(8, 0x2a, RZV2H_MPXED_PIN_FUNCS),	/* PA */
	RZG2L_GPIO_PORT_PACK_VARIABLE(6, 0x2b),			/* PB */
};

static const struct {
	struct rzg2l_dedicated_configs common[35];
	struct rzg2l_dedicated_configs rzg2l_pins[7];
} rzg2l_dedicated_pins = {
	.common = {
		{ "NMI", RZG2L_SINGLE_PIN_PACK(0x1, 0, PIN_CFG_NF) },
		{ "TMS/SWDIO", RZG2L_SINGLE_PIN_PACK(0x2, 0,
		 (PIN_CFG_IOLH_A | PIN_CFG_SR | PIN_CFG_IEN)) },
		{ "TDO", RZG2L_SINGLE_PIN_PACK(0x3, 0,
		 (PIN_CFG_IOLH_A | PIN_CFG_SR | PIN_CFG_IEN)) },
		{ "AUDIO_CLK1", RZG2L_SINGLE_PIN_PACK(0x4, 0, PIN_CFG_IEN) },
		{ "AUDIO_CLK2", RZG2L_SINGLE_PIN_PACK(0x4, 1, PIN_CFG_IEN) },
		{ "SD0_CLK", RZG2L_SINGLE_PIN_PACK(0x6, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_CMD", RZG2L_SINGLE_PIN_PACK(0x6, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_RST#", RZG2L_SINGLE_PIN_PACK(0x6, 2,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA0", RZG2L_SINGLE_PIN_PACK(0x7, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA1", RZG2L_SINGLE_PIN_PACK(0x7, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA2", RZG2L_SINGLE_PIN_PACK(0x7, 2,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA3", RZG2L_SINGLE_PIN_PACK(0x7, 3,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA4", RZG2L_SINGLE_PIN_PACK(0x7, 4,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA5", RZG2L_SINGLE_PIN_PACK(0x7, 5,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA6", RZG2L_SINGLE_PIN_PACK(0x7, 6,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD0_DATA7", RZG2L_SINGLE_PIN_PACK(0x7, 7,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD0)) },
		{ "SD1_CLK", RZG2L_SINGLE_PIN_PACK(0x8, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_SD1)) },
		{ "SD1_CMD", RZG2L_SINGLE_PIN_PACK(0x8, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD1)) },
		{ "SD1_DATA0", RZG2L_SINGLE_PIN_PACK(0x9, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD1)) },
		{ "SD1_DATA1", RZG2L_SINGLE_PIN_PACK(0x9, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD1)) },
		{ "SD1_DATA2", RZG2L_SINGLE_PIN_PACK(0x9, 2,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD1)) },
		{ "SD1_DATA3", RZG2L_SINGLE_PIN_PACK(0x9, 3,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IEN | PIN_CFG_IO_VMC_SD1)) },
		{ "QSPI0_SPCLK", RZG2L_SINGLE_PIN_PACK(0xa, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI0_IO0", RZG2L_SINGLE_PIN_PACK(0xa, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI0_IO1", RZG2L_SINGLE_PIN_PACK(0xa, 2,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI0_IO2", RZG2L_SINGLE_PIN_PACK(0xa, 3,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI0_IO3", RZG2L_SINGLE_PIN_PACK(0xa, 4,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI0_SSL", RZG2L_SINGLE_PIN_PACK(0xa, 5,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI_RESET#", RZG2L_SINGLE_PIN_PACK(0xc, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI_WP#", RZG2L_SINGLE_PIN_PACK(0xc, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "WDTOVF_PERROUT#", RZG2L_SINGLE_PIN_PACK(0xd, 0, (PIN_CFG_IOLH_A | PIN_CFG_SR)) },
		{ "RIIC0_SDA", RZG2L_SINGLE_PIN_PACK(0xe, 0, PIN_CFG_IEN) },
		{ "RIIC0_SCL", RZG2L_SINGLE_PIN_PACK(0xe, 1, PIN_CFG_IEN) },
		{ "RIIC1_SDA", RZG2L_SINGLE_PIN_PACK(0xe, 2, PIN_CFG_IEN) },
		{ "RIIC1_SCL", RZG2L_SINGLE_PIN_PACK(0xe, 3, PIN_CFG_IEN) },
	},
	.rzg2l_pins = {
		{ "QSPI_INT#", RZG2L_SINGLE_PIN_PACK(0xc, 2, (PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_SPCLK", RZG2L_SINGLE_PIN_PACK(0xb, 0,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_IO0", RZG2L_SINGLE_PIN_PACK(0xb, 1,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_IO1", RZG2L_SINGLE_PIN_PACK(0xb, 2,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_IO2", RZG2L_SINGLE_PIN_PACK(0xb, 3,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_IO3", RZG2L_SINGLE_PIN_PACK(0xb, 4,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR  | PIN_CFG_IO_VMC_QSPI)) },
		{ "QSPI1_SSL", RZG2L_SINGLE_PIN_PACK(0xb, 5,
		 (PIN_CFG_IOLH_B | PIN_CFG_SR | PIN_CFG_IO_VMC_QSPI)) },
	}
};

static const struct rzg2l_dedicated_configs rzg3s_dedicated_pins[] = {
	{ "NMI", RZG2L_SINGLE_PIN_PACK(0x0, 0, PIN_CFG_NF) },
	{ "TMS/SWDIO", RZG2L_SINGLE_PIN_PACK(0x1, 0, (PIN_CFG_IOLH_A | PIN_CFG_IEN |
						      PIN_CFG_SOFT_PS)) },
	{ "TDO", RZG2L_SINGLE_PIN_PACK(0x1, 1, (PIN_CFG_IOLH_A | PIN_CFG_SOFT_PS)) },
	{ "WDTOVF_PERROUT#", RZG2L_SINGLE_PIN_PACK(0x6, 0, PIN_CFG_IOLH_A | PIN_CFG_SOFT_PS) },
	{ "SD0_CLK", RZG2L_SINGLE_PIN_PACK(0x10, 0, (PIN_CFG_IOLH_B | PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_CMD", RZG2L_SINGLE_PIN_PACK(0x10, 1, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						     PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_RST#", RZG2L_SINGLE_PIN_PACK(0x10, 2, (PIN_CFG_IOLH_B | PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA0", RZG2L_SINGLE_PIN_PACK(0x11, 0, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA1", RZG2L_SINGLE_PIN_PACK(0x11, 1, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA2", RZG2L_SINGLE_PIN_PACK(0x11, 2, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA3", RZG2L_SINGLE_PIN_PACK(0x11, 3, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA4", RZG2L_SINGLE_PIN_PACK(0x11, 4, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA5", RZG2L_SINGLE_PIN_PACK(0x11, 5, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA6", RZG2L_SINGLE_PIN_PACK(0x11, 6, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD0_DATA7", RZG2L_SINGLE_PIN_PACK(0x11, 7, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD0)) },
	{ "SD1_CLK", RZG2L_SINGLE_PIN_PACK(0x12, 0, (PIN_CFG_IOLH_B | PIN_CFG_IO_VMC_SD1)) },
	{ "SD1_CMD", RZG2L_SINGLE_PIN_PACK(0x12, 1, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						     PIN_CFG_IO_VMC_SD1)) },
	{ "SD1_DATA0", RZG2L_SINGLE_PIN_PACK(0x13, 0, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD1)) },
	{ "SD1_DATA1", RZG2L_SINGLE_PIN_PACK(0x13, 1, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD1)) },
	{ "SD1_DATA2", RZG2L_SINGLE_PIN_PACK(0x13, 2, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD1)) },
	{ "SD1_DATA3", RZG2L_SINGLE_PIN_PACK(0x13, 3, (PIN_CFG_IOLH_B | PIN_CFG_IEN |
						       PIN_CFG_IO_VMC_SD1)) },
};

static struct rzg2l_dedicated_configs rzv2h_dedicated_pins[] = {
	{ "NMI", RZG2L_SINGLE_PIN_PACK(0x1, 0, PIN_CFG_NF) },
	{ "TMS_SWDIO", RZG2L_SINGLE_PIN_PACK(0x3, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_IEN)) },
	{ "TDO", RZG2L_SINGLE_PIN_PACK(0x3, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "WDTUDFCA", RZG2L_SINGLE_PIN_PACK(0x5, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD | PIN_CFG_NOD)) },
	{ "WDTUDFCM", RZG2L_SINGLE_PIN_PACK(0x5, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD | PIN_CFG_NOD)) },
	{ "SCIF_RXD", RZG2L_SINGLE_PIN_PACK(0x6, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD)) },
	{ "SCIF_TXD", RZG2L_SINGLE_PIN_PACK(0x6, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD)) },
	{ "XSPI0_CKP", RZG2L_SINGLE_PIN_PACK(0x7, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "XSPI0_CKN", RZG2L_SINGLE_PIN_PACK(0x7, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "XSPI0_CS0N", RZG2L_SINGLE_PIN_PACK(0x7, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						       PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "XSPI0_DS", RZG2L_SINGLE_PIN_PACK(0x7, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD)) },
	{ "XSPI0_RESET0N", RZG2L_SINGLE_PIN_PACK(0x7, 4, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
							  PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "XSPI0_RSTO0N", RZG2L_SINGLE_PIN_PACK(0x7, 5, (PIN_CFG_PUPD)) },
	{ "XSPI0_INT0N", RZG2L_SINGLE_PIN_PACK(0x7, 6, (PIN_CFG_PUPD)) },
	{ "XSPI0_ECS0N", RZG2L_SINGLE_PIN_PACK(0x7, 7, (PIN_CFG_PUPD)) },
	{ "XSPI0_IO0", RZG2L_SINGLE_PIN_PACK(0x8, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO1", RZG2L_SINGLE_PIN_PACK(0x8, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO2", RZG2L_SINGLE_PIN_PACK(0x8, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO3", RZG2L_SINGLE_PIN_PACK(0x8, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO4", RZG2L_SINGLE_PIN_PACK(0x8, 4, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO5", RZG2L_SINGLE_PIN_PACK(0x8, 5, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO6", RZG2L_SINGLE_PIN_PACK(0x8, 6, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "XSPI0_IO7", RZG2L_SINGLE_PIN_PACK(0x8, 7, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "SD0CLK", RZG2L_SINGLE_PIN_PACK(0x9, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "SD0CMD", RZG2L_SINGLE_PIN_PACK(0x9, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						   PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0RSTN", RZG2L_SINGLE_PIN_PACK(0x9, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "SD0DAT0", RZG2L_SINGLE_PIN_PACK(0xa, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT1", RZG2L_SINGLE_PIN_PACK(0xa, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT2", RZG2L_SINGLE_PIN_PACK(0xa, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT3", RZG2L_SINGLE_PIN_PACK(0xa, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT4", RZG2L_SINGLE_PIN_PACK(0xa, 4, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT5", RZG2L_SINGLE_PIN_PACK(0xa, 5, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT6", RZG2L_SINGLE_PIN_PACK(0xa, 6, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD0DAT7", RZG2L_SINGLE_PIN_PACK(0xa, 7, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD1CLK", RZG2L_SINGLE_PIN_PACK(0xb, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "SD1CMD", RZG2L_SINGLE_PIN_PACK(0xb, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						   PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD1DAT0", RZG2L_SINGLE_PIN_PACK(0xc, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD1DAT1", RZG2L_SINGLE_PIN_PACK(0xc, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD1DAT2", RZG2L_SINGLE_PIN_PACK(0xc, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "SD1DAT3", RZG2L_SINGLE_PIN_PACK(0xc, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "PCIE0_RSTOUTB", RZG2L_SINGLE_PIN_PACK(0xe, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "PCIE1_RSTOUTB", RZG2L_SINGLE_PIN_PACK(0xe, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR)) },
	{ "ET0_MDIO", RZG2L_SINGLE_PIN_PACK(0xf, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "ET0_MDC", RZG2L_SINGLE_PIN_PACK(0xf, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						    PIN_CFG_PUPD)) },
	{ "ET0_RXCTL_RXDV", RZG2L_SINGLE_PIN_PACK(0x10, 0, (PIN_CFG_PUPD)) },
	{ "ET0_TXCTL_TXEN", RZG2L_SINGLE_PIN_PACK(0x10, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
							    PIN_CFG_PUPD)) },
	{ "ET0_TXER", RZG2L_SINGLE_PIN_PACK(0x10, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET0_RXER", RZG2L_SINGLE_PIN_PACK(0x10, 3, (PIN_CFG_PUPD)) },
	{ "ET0_RXC_RXCLK", RZG2L_SINGLE_PIN_PACK(0x10, 4, (PIN_CFG_PUPD)) },
	{ "ET0_TXC_TXCLK", RZG2L_SINGLE_PIN_PACK(0x10, 5, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
							   PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "ET0_CRS", RZG2L_SINGLE_PIN_PACK(0x10, 6, (PIN_CFG_PUPD)) },
	{ "ET0_COL", RZG2L_SINGLE_PIN_PACK(0x10, 7, (PIN_CFG_PUPD)) },
	{ "ET0_TXD0", RZG2L_SINGLE_PIN_PACK(0x11, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET0_TXD1", RZG2L_SINGLE_PIN_PACK(0x11, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET0_TXD2", RZG2L_SINGLE_PIN_PACK(0x11, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET0_TXD3", RZG2L_SINGLE_PIN_PACK(0x11, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET0_RXD0", RZG2L_SINGLE_PIN_PACK(0x11, 4, (PIN_CFG_PUPD)) },
	{ "ET0_RXD1", RZG2L_SINGLE_PIN_PACK(0x11, 5, (PIN_CFG_PUPD)) },
	{ "ET0_RXD2", RZG2L_SINGLE_PIN_PACK(0x11, 6, (PIN_CFG_PUPD)) },
	{ "ET0_RXD3", RZG2L_SINGLE_PIN_PACK(0x11, 7, (PIN_CFG_PUPD)) },
	{ "ET1_MDIO", RZG2L_SINGLE_PIN_PACK(0x12, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_IEN | PIN_CFG_PUPD)) },
	{ "ET1_MDC", RZG2L_SINGLE_PIN_PACK(0x12, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						     PIN_CFG_PUPD)) },
	{ "ET1_RXCTL_RXDV", RZG2L_SINGLE_PIN_PACK(0x13, 0, (PIN_CFG_PUPD)) },
	{ "ET1_TXCTL_TXEN", RZG2L_SINGLE_PIN_PACK(0x13, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
							    PIN_CFG_PUPD)) },
	{ "ET1_TXER", RZG2L_SINGLE_PIN_PACK(0x13, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						       PIN_CFG_PUPD)) },
	{ "ET1_RXER", RZG2L_SINGLE_PIN_PACK(0x13, 3, (PIN_CFG_PUPD)) },
	{ "ET1_RXC_RXCLK", RZG2L_SINGLE_PIN_PACK(0x13, 4, (PIN_CFG_PUPD)) },
	{ "ET1_TXC_TXCLK", RZG2L_SINGLE_PIN_PACK(0x13, 5, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
							   PIN_CFG_PUPD | PIN_CFG_OEN)) },
	{ "ET1_CRS", RZG2L_SINGLE_PIN_PACK(0x13, 6, (PIN_CFG_PUPD)) },
	{ "ET1_COL", RZG2L_SINGLE_PIN_PACK(0x13, 7, (PIN_CFG_PUPD)) },
	{ "ET1_TXD0", RZG2L_SINGLE_PIN_PACK(0x14, 0, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET1_TXD1", RZG2L_SINGLE_PIN_PACK(0x14, 1, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET1_TXD2", RZG2L_SINGLE_PIN_PACK(0x14, 2, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET1_TXD3", RZG2L_SINGLE_PIN_PACK(0x14, 3, (PIN_CFG_IOLH_RZV2H | PIN_CFG_SR |
						      PIN_CFG_PUPD)) },
	{ "ET1_RXD0", RZG2L_SINGLE_PIN_PACK(0x14, 4, (PIN_CFG_PUPD)) },
	{ "ET1_RXD1", RZG2L_SINGLE_PIN_PACK(0x14, 5, (PIN_CFG_PUPD)) },
	{ "ET1_RXD2", RZG2L_SINGLE_PIN_PACK(0x14, 6, (PIN_CFG_PUPD)) },
	{ "ET1_RXD3", RZG2L_SINGLE_PIN_PACK(0x14, 7, (PIN_CFG_PUPD)) },
};

static int rzg2l_gpio_get_gpioint(unsigned int virq, struct rzg2l_pinctrl *pctrl)
{
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[virq];
	const struct rzg2l_pinctrl_data *data = pctrl->data;
	u64 *pin_data = pin_desc->drv_data;
	unsigned int gpioint;
	unsigned int i;
	u32 port, bit;

	if (*pin_data & PIN_CFG_NOGPIO_INT)
		return -EINVAL;

	port = virq / 8;
	bit = virq % 8;

	if (port >= data->n_ports ||
	    bit >= hweight8(FIELD_GET(PIN_CFG_PIN_MAP_MASK, data->port_pin_configs[port])))
		return -EINVAL;

	gpioint = bit;
	for (i = 0; i < port; i++)
		gpioint += hweight8(FIELD_GET(PIN_CFG_PIN_MAP_MASK, data->port_pin_configs[i]));

	return gpioint;
}

static void rzg2l_gpio_irq_endisable(struct rzg2l_pinctrl *pctrl,
				     unsigned int hwirq, bool enable)
{
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[hwirq];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(hwirq);
	unsigned long flags;
	void __iomem *addr;

	addr = pctrl->base + ISEL(off);
	if (bit >= 4) {
		bit -= 4;
		addr += 4;
	}

	spin_lock_irqsave(&pctrl->lock, flags);
	if (enable)
		writel(readl(addr) | BIT(bit * 8), addr);
	else
		writel(readl(addr) & ~BIT(bit * 8), addr);
	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void rzg2l_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	irq_chip_disable_parent(d);
	gpiochip_disable_irq(gc, hwirq);
}

static void rzg2l_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	irq_chip_enable_parent(d);
}

static int rzg2l_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	return irq_chip_set_type_parent(d, type);
}

static void rzg2l_gpio_irqc_eoi(struct irq_data *d)
{
	irq_chip_eoi_parent(d);
}

static void rzg2l_gpio_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);

	seq_printf(p, dev_name(gc->parent));
}

static int rzg2l_gpio_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct rzg2l_pinctrl *pctrl = container_of(gc, struct rzg2l_pinctrl, gpio_chip);
	int ret;

	/* It should not happen. */
	if (!data->parent_data)
		return -EOPNOTSUPP;

	ret = irq_chip_set_wake_parent(data, on);
	if (ret)
		return ret;

	if (on)
		atomic_inc(&pctrl->wakeup_path);
	else
		atomic_dec(&pctrl->wakeup_path);

	return 0;
}

static const struct irq_chip rzg2l_gpio_irqchip = {
	.name = "rzg2l-gpio",
	.irq_disable = rzg2l_gpio_irq_disable,
	.irq_enable = rzg2l_gpio_irq_enable,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_set_type = rzg2l_gpio_irq_set_type,
	.irq_eoi = rzg2l_gpio_irqc_eoi,
	.irq_print_chip = rzg2l_gpio_irq_print_chip,
	.irq_set_affinity = irq_chip_set_affinity_parent,
	.irq_set_wake = rzg2l_gpio_irq_set_wake,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int rzg2l_gpio_interrupt_input_mode(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct pinctrl_pin_desc *pin_desc = &pctrl->desc.pins[offset];
	u64 *pin_data = pin_desc->drv_data;
	u32 off = RZG2L_PIN_CFG_TO_PORT_OFFSET(*pin_data);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u8 reg8;
	int ret;

	reg8 = readb(pctrl->base + PMC(off));
	if (reg8 & BIT(bit)) {
		ret = rzg2l_gpio_request(chip, offset);
		if (ret)
			return ret;
	}

	return rzg2l_gpio_direction_input(chip, offset);
}

static int rzg2l_gpio_child_to_parent_hwirq(struct gpio_chip *gc,
					    unsigned int child,
					    unsigned int child_type,
					    unsigned int *parent,
					    unsigned int *parent_type)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned long flags;
	int gpioint, irq;
	int ret;

	gpioint = rzg2l_gpio_get_gpioint(child, pctrl);
	if (gpioint < 0)
		return gpioint;

	ret = rzg2l_gpio_interrupt_input_mode(gc, child);
	if (ret)
		return ret;

	spin_lock_irqsave(&pctrl->bitmap_lock, flags);
	irq = bitmap_find_free_region(pctrl->tint_slot, RZG2L_TINT_MAX_INTERRUPT, get_order(1));
	spin_unlock_irqrestore(&pctrl->bitmap_lock, flags);
	if (irq < 0) {
		ret = -ENOSPC;
		goto err;
	}

	rzg2l_gpio_irq_endisable(pctrl, child, true);
	pctrl->hwirq[irq] = child;
	irq += RZG2L_TINT_IRQ_START_INDEX;

	/* All these interrupts are level high in the CPU */
	*parent_type = IRQ_TYPE_LEVEL_HIGH;
	*parent = RZG2L_PACK_HWIRQ(gpioint, irq);
	return 0;

err:
	rzg2l_gpio_free(gc, child);
	return ret;
}

static int rzg2l_gpio_populate_parent_fwspec(struct gpio_chip *chip,
					     union gpio_irq_fwspec *gfwspec,
					     unsigned int parent_hwirq,
					     unsigned int parent_type)
{
	struct irq_fwspec *fwspec = &gfwspec->fwspec;

	fwspec->fwnode = chip->irq.parent_domain->fwnode;
	fwspec->param_count = 2;
	fwspec->param[0] = parent_hwirq;
	fwspec->param[1] = parent_type;

	return 0;
}

static void rzg2l_gpio_irq_restore(struct rzg2l_pinctrl *pctrl)
{
	struct irq_domain *domain = pctrl->gpio_chip.irq.domain;

	for (unsigned int i = 0; i < RZG2L_TINT_MAX_INTERRUPT; i++) {
		struct irq_data *data;
		unsigned long flags;
		unsigned int virq;
		int ret;

		if (!pctrl->hwirq[i])
			continue;

		virq = irq_find_mapping(domain, pctrl->hwirq[i]);
		if (!virq) {
			dev_crit(pctrl->dev, "Failed to find IRQ mapping for hwirq %u\n",
				 pctrl->hwirq[i]);
			continue;
		}

		data = irq_domain_get_irq_data(domain, virq);
		if (!data) {
			dev_crit(pctrl->dev, "Failed to get IRQ data for virq=%u\n", virq);
			continue;
		}

		/*
		 * This has to be atomically executed to protect against a concurrent
		 * interrupt.
		 */
		spin_lock_irqsave(&pctrl->lock, flags);
		ret = rzg2l_gpio_irq_set_type(data, irqd_get_trigger_type(data));
		if (!ret && !irqd_irq_disabled(data))
			rzg2l_gpio_irq_enable(data);
		spin_unlock_irqrestore(&pctrl->lock, flags);

		if (ret)
			dev_crit(pctrl->dev, "Failed to set IRQ type for virq=%u\n", virq);
	}
}

static void rzg2l_gpio_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				       unsigned int nr_irqs)
{
	struct irq_data *d;

	d = irq_domain_get_irq_data(domain, virq);
	if (d) {
		struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
		struct rzg2l_pinctrl *pctrl = container_of(gc, struct rzg2l_pinctrl, gpio_chip);
		irq_hw_number_t hwirq = irqd_to_hwirq(d);
		unsigned long flags;
		unsigned int i;

		for (i = 0; i < RZG2L_TINT_MAX_INTERRUPT; i++) {
			if (pctrl->hwirq[i] == hwirq) {
				rzg2l_gpio_irq_endisable(pctrl, hwirq, false);
				rzg2l_gpio_free(gc, hwirq);
				spin_lock_irqsave(&pctrl->bitmap_lock, flags);
				bitmap_release_region(pctrl->tint_slot, i, get_order(1));
				spin_unlock_irqrestore(&pctrl->bitmap_lock, flags);
				pctrl->hwirq[i] = 0;
				break;
			}
		}
	}
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static void rzg2l_init_irq_valid_mask(struct gpio_chip *gc,
				      unsigned long *valid_mask,
				      unsigned int ngpios)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(gc);
	struct gpio_chip *chip = &pctrl->gpio_chip;
	unsigned int offset;

	/* Forbid unused lines to be mapped as IRQs */
	for (offset = 0; offset < chip->ngpio; offset++) {
		u32 port, bit;

		port = offset / 8;
		bit = offset % 8;

		if (port >= pctrl->data->n_ports ||
		    bit >= hweight8(FIELD_GET(PIN_CFG_PIN_MAP_MASK,
					      pctrl->data->port_pin_configs[port])))
			clear_bit(offset, valid_mask);
	}
}

static int rzg2l_pinctrl_reg_cache_alloc(struct rzg2l_pinctrl *pctrl)
{
	u32 nports = pctrl->data->n_port_pins / RZG2L_PINS_PER_PORT;
	struct rzg2l_pinctrl_reg_cache *cache, *dedicated_cache;

	cache = devm_kzalloc(pctrl->dev, sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	dedicated_cache = devm_kzalloc(pctrl->dev, sizeof(*dedicated_cache), GFP_KERNEL);
	if (!dedicated_cache)
		return -ENOMEM;

	cache->p = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->p), GFP_KERNEL);
	if (!cache->p)
		return -ENOMEM;

	cache->pm = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->pm), GFP_KERNEL);
	if (!cache->pm)
		return -ENOMEM;

	cache->pmc = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->pmc), GFP_KERNEL);
	if (!cache->pmc)
		return -ENOMEM;

	cache->pfc = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->pfc), GFP_KERNEL);
	if (!cache->pfc)
		return -ENOMEM;

	for (u8 i = 0; i < 2; i++) {
		u32 n_dedicated_pins = pctrl->data->n_dedicated_pins;

		cache->iolh[i] = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->iolh[i]),
					      GFP_KERNEL);
		if (!cache->iolh[i])
			return -ENOMEM;

		cache->ien[i] = devm_kcalloc(pctrl->dev, nports, sizeof(*cache->ien[i]),
					     GFP_KERNEL);
		if (!cache->ien[i])
			return -ENOMEM;

		/* Allocate dedicated cache. */
		dedicated_cache->iolh[i] = devm_kcalloc(pctrl->dev, n_dedicated_pins,
							sizeof(*dedicated_cache->iolh[i]),
							GFP_KERNEL);
		if (!dedicated_cache->iolh[i])
			return -ENOMEM;

		dedicated_cache->ien[i] = devm_kcalloc(pctrl->dev, n_dedicated_pins,
						       sizeof(*dedicated_cache->ien[i]),
						       GFP_KERNEL);
		if (!dedicated_cache->ien[i])
			return -ENOMEM;
	}

	pctrl->cache = cache;
	pctrl->dedicated_cache = dedicated_cache;

	return 0;
}

static int rzg2l_gpio_register(struct rzg2l_pinctrl *pctrl)
{
	struct device_node *np = pctrl->dev->of_node;
	struct gpio_chip *chip = &pctrl->gpio_chip;
	const char *name = dev_name(pctrl->dev);
	struct irq_domain *parent_domain;
	struct of_phandle_args of_args;
	struct device_node *parent_np;
	struct gpio_irq_chip *girq;
	int ret;

	parent_np = of_irq_find_parent(np);
	if (!parent_np)
		return -ENXIO;

	parent_domain = irq_find_host(parent_np);
	of_node_put(parent_np);
	if (!parent_domain)
		return -EPROBE_DEFER;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &of_args);
	if (ret)
		return dev_err_probe(pctrl->dev, ret, "Unable to parse gpio-ranges\n");

	if (of_args.args[0] != 0 || of_args.args[1] != 0 ||
	    of_args.args[2] != pctrl->data->n_port_pins)
		return dev_err_probe(pctrl->dev, -EINVAL,
				     "gpio-ranges does not match selected SOC\n");

	chip->names = pctrl->data->port_pins;
	chip->request = rzg2l_gpio_request;
	chip->free = rzg2l_gpio_free;
	chip->get_direction = rzg2l_gpio_get_direction;
	chip->direction_input = rzg2l_gpio_direction_input;
	chip->direction_output = rzg2l_gpio_direction_output;
	chip->get = rzg2l_gpio_get;
	chip->set = rzg2l_gpio_set;
	chip->label = name;
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->ngpio = of_args.args[2];

	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &rzg2l_gpio_irqchip);
	girq->fwnode = dev_fwnode(pctrl->dev);
	girq->parent_domain = parent_domain;
	girq->child_to_parent_hwirq = rzg2l_gpio_child_to_parent_hwirq;
	girq->populate_parent_alloc_arg = rzg2l_gpio_populate_parent_fwspec;
	girq->child_irq_domain_ops.free = rzg2l_gpio_irq_domain_free;
	girq->init_valid_mask = rzg2l_init_irq_valid_mask;

	pctrl->gpio_range.id = 0;
	pctrl->gpio_range.pin_base = 0;
	pctrl->gpio_range.base = 0;
	pctrl->gpio_range.npins = chip->ngpio;
	pctrl->gpio_range.name = chip->label;
	pctrl->gpio_range.gc = chip;
	ret = devm_gpiochip_add_data(pctrl->dev, chip, pctrl);
	if (ret)
		return dev_err_probe(pctrl->dev, ret, "failed to add GPIO controller\n");

	dev_dbg(pctrl->dev, "Registered gpio controller\n");

	return 0;
}

static int rzg2l_pinctrl_register(struct rzg2l_pinctrl *pctrl)
{
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	struct pinctrl_pin_desc *pins;
	unsigned int i, j;
	u64 *pin_data;
	int ret;

	pctrl->desc.name = DRV_NAME;
	pctrl->desc.npins = pctrl->data->n_port_pins + pctrl->data->n_dedicated_pins;
	pctrl->desc.pctlops = &rzg2l_pinctrl_pctlops;
	pctrl->desc.pmxops = &rzg2l_pinctrl_pmxops;
	pctrl->desc.confops = &rzg2l_pinctrl_confops;
	pctrl->desc.owner = THIS_MODULE;
	if (pctrl->data->num_custom_params) {
		pctrl->desc.num_custom_params = pctrl->data->num_custom_params;
		pctrl->desc.custom_params = pctrl->data->custom_params;
#ifdef CONFIG_DEBUG_FS
		pctrl->desc.custom_conf_items = pctrl->data->custom_conf_items;
#endif
	}

	pins = devm_kcalloc(pctrl->dev, pctrl->desc.npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pin_data = devm_kcalloc(pctrl->dev, pctrl->desc.npins,
				sizeof(*pin_data), GFP_KERNEL);
	if (!pin_data)
		return -ENOMEM;

	pctrl->pins = pins;
	pctrl->desc.pins = pins;

	for (i = 0, j = 0; i < pctrl->data->n_port_pins; i++) {
		pins[i].number = i;
		pins[i].name = pctrl->data->port_pins[i];
		if (i && !(i % RZG2L_PINS_PER_PORT))
			j++;
		pin_data[i] = pctrl->data->port_pin_configs[j];
		if (pin_data[i] & RZG2L_VARIABLE_CFG)
			pin_data[i] = rzg2l_pinctrl_get_variable_pin_cfg(pctrl,
									 pin_data[i],
									 j,
									 i % RZG2L_PINS_PER_PORT);
		pins[i].drv_data = &pin_data[i];
	}

	for (i = 0; i < pctrl->data->n_dedicated_pins; i++) {
		unsigned int index = pctrl->data->n_port_pins + i;

		pins[index].number = index;
		pins[index].name = pctrl->data->dedicated_pins[i].name;
		pin_data[index] = pctrl->data->dedicated_pins[i].config;
		pins[index].drv_data = &pin_data[index];
	}

	pctrl->settings = devm_kcalloc(pctrl->dev, pctrl->desc.npins, sizeof(*pctrl->settings),
				       GFP_KERNEL);
	if (!pctrl->settings)
		return -ENOMEM;

	for (i = 0; hwcfg->drive_strength_ua && i < pctrl->desc.npins; i++) {
		if (pin_data[i] & PIN_CFG_SOFT_PS) {
			pctrl->settings[i].power_source = 3300;
		} else {
			ret = rzg2l_get_power_source(pctrl, i, pin_data[i]);
			if (ret < 0)
				continue;
			pctrl->settings[i].power_source = ret;
		}
	}

	ret = rzg2l_pinctrl_reg_cache_alloc(pctrl);
	if (ret)
		return ret;

	ret = devm_pinctrl_register_and_init(pctrl->dev, &pctrl->desc, pctrl,
					     &pctrl->pctl);
	if (ret)
		return dev_err_probe(pctrl->dev, ret, "pinctrl registration failed\n");

	ret = pinctrl_enable(pctrl->pctl);
	if (ret)
		dev_err_probe(pctrl->dev, ret, "pinctrl enable failed\n");

	ret = rzg2l_gpio_register(pctrl);
	if (ret)
		return dev_err_probe(pctrl->dev, ret, "failed to add GPIO chip\n");

	return 0;
}

static int rzg2l_pinctrl_probe(struct platform_device *pdev)
{
	struct rzg2l_pinctrl *pctrl;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(r9a07g044_gpio_configs) * RZG2L_PINS_PER_PORT >
		     ARRAY_SIZE(rzg2l_gpio_names));

	BUILD_BUG_ON(ARRAY_SIZE(r9a07g043_gpio_configs) * RZG2L_PINS_PER_PORT >
		     ARRAY_SIZE(rzg2l_gpio_names));

	BUILD_BUG_ON(ARRAY_SIZE(r9a08g045_gpio_configs) * RZG2L_PINS_PER_PORT >
		     ARRAY_SIZE(rzg2l_gpio_names));

	BUILD_BUG_ON(ARRAY_SIZE(r9a09g057_gpio_configs) * RZG2L_PINS_PER_PORT >
		     ARRAY_SIZE(rzv2h_gpio_names));

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = &pdev->dev;

	pctrl->data = of_device_get_match_data(&pdev->dev);
	if (!pctrl->data)
		return -EINVAL;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	pctrl->clk = devm_clk_get_enabled(pctrl->dev, NULL);
	if (IS_ERR(pctrl->clk)) {
		return dev_err_probe(pctrl->dev, PTR_ERR(pctrl->clk),
				     "failed to enable GPIO clk\n");
	}

	spin_lock_init(&pctrl->lock);
	spin_lock_init(&pctrl->bitmap_lock);
	mutex_init(&pctrl->mutex);
	atomic_set(&pctrl->wakeup_path, 0);

	platform_set_drvdata(pdev, pctrl);

	ret = rzg2l_pinctrl_register(pctrl);
	if (ret)
		return ret;

	dev_info(pctrl->dev, "%s support registered\n", DRV_NAME);
	return 0;
}

static void rzg2l_pinctrl_pm_setup_regs(struct rzg2l_pinctrl *pctrl, bool suspend)
{
	u32 nports = pctrl->data->n_port_pins / RZG2L_PINS_PER_PORT;
	struct rzg2l_pinctrl_reg_cache *cache = pctrl->cache;

	for (u32 port = 0; port < nports; port++) {
		bool has_iolh, has_ien;
		u32 off, caps;
		u8 pincnt;
		u64 cfg;

		cfg = pctrl->data->port_pin_configs[port];
		off = RZG2L_PIN_CFG_TO_PORT_OFFSET(cfg);
		pincnt = hweight8(FIELD_GET(PIN_CFG_PIN_MAP_MASK, cfg));

		caps = FIELD_GET(PIN_CFG_MASK, cfg);
		has_iolh = !!(caps & (PIN_CFG_IOLH_A | PIN_CFG_IOLH_B | PIN_CFG_IOLH_C));
		has_ien = !!(caps & PIN_CFG_IEN);

		if (suspend)
			RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + PFC(off), cache->pfc[port]);

		/*
		 * Now cache the registers or set them in the order suggested by
		 * HW manual (section "Operation for GPIO Function").
		 */
		RZG2L_PCTRL_REG_ACCESS8(suspend, pctrl->base + PMC(off), cache->pmc[port]);
		if (has_iolh) {
			RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IOLH(off),
						 cache->iolh[0][port]);
			if (pincnt >= 4) {
				RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IOLH(off) + 4,
							 cache->iolh[1][port]);
			}
		}

		RZG2L_PCTRL_REG_ACCESS16(suspend, pctrl->base + PM(off), cache->pm[port]);
		RZG2L_PCTRL_REG_ACCESS8(suspend, pctrl->base + P(off), cache->p[port]);

		if (has_ien) {
			RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IEN(off),
						 cache->ien[0][port]);
			if (pincnt >= 4) {
				RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IEN(off) + 4,
							 cache->ien[1][port]);
			}
		}
	}
}

static void rzg2l_pinctrl_pm_setup_dedicated_regs(struct rzg2l_pinctrl *pctrl, bool suspend)
{
	struct rzg2l_pinctrl_reg_cache *cache = pctrl->dedicated_cache;
	u32 caps;
	u32 i;

	/*
	 * Make sure entries in pctrl->data->n_dedicated_pins[] having the same
	 * port offset are close together.
	 */
	for (i = 0, caps = 0; i < pctrl->data->n_dedicated_pins; i++) {
		bool has_iolh, has_ien;
		u32 off, next_off = 0;
		u64 cfg, next_cfg;
		u8 pincnt;

		cfg = pctrl->data->dedicated_pins[i].config;
		off = RZG2L_PIN_CFG_TO_PORT_OFFSET(cfg);
		if (i + 1 < pctrl->data->n_dedicated_pins) {
			next_cfg = pctrl->data->dedicated_pins[i + 1].config;
			next_off = RZG2L_PIN_CFG_TO_PORT_OFFSET(next_cfg);
		}

		if (off == next_off) {
			/* Gather caps of all port pins. */
			caps |= FIELD_GET(PIN_CFG_MASK, cfg);
			continue;
		}

		/* And apply them in a single shot. */
		has_iolh = !!(caps & (PIN_CFG_IOLH_A | PIN_CFG_IOLH_B | PIN_CFG_IOLH_C));
		has_ien = !!(caps & PIN_CFG_IEN);
		pincnt = hweight8(FIELD_GET(RZG2L_SINGLE_PIN_BITS_MASK, cfg));

		if (has_iolh) {
			RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IOLH(off),
						 cache->iolh[0][i]);
		}
		if (has_ien) {
			RZG2L_PCTRL_REG_ACCESS32(suspend, pctrl->base + IEN(off),
						 cache->ien[0][i]);
		}

		if (pincnt >= 4) {
			if (has_iolh) {
				RZG2L_PCTRL_REG_ACCESS32(suspend,
							 pctrl->base + IOLH(off) + 4,
							 cache->iolh[1][i]);
			}
			if (has_ien) {
				RZG2L_PCTRL_REG_ACCESS32(suspend,
							 pctrl->base + IEN(off) + 4,
							 cache->ien[1][i]);
			}
		}
		caps = 0;
	}
}

static void rzg2l_pinctrl_pm_setup_pfc(struct rzg2l_pinctrl *pctrl)
{
	u32 nports = pctrl->data->n_port_pins / RZG2L_PINS_PER_PORT;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);
	pctrl->data->pwpr_pfc_lock_unlock(pctrl, false);

	/* Restore port registers. */
	for (u32 port = 0; port < nports; port++) {
		unsigned long pinmap;
		u8 pmc = 0, max_pin;
		u32 off, pfc = 0;
		u64 cfg;
		u16 pm;
		u8 pin;

		cfg = pctrl->data->port_pin_configs[port];
		off = RZG2L_PIN_CFG_TO_PORT_OFFSET(cfg);
		pinmap = FIELD_GET(PIN_CFG_PIN_MAP_MASK, cfg);
		max_pin = fls(pinmap);

		pm = readw(pctrl->base + PM(off));
		for_each_set_bit(pin, &pinmap, max_pin) {
			struct rzg2l_pinctrl_reg_cache *cache = pctrl->cache;

			/* Nothing to do if PFC was not configured before. */
			if (!(cache->pmc[port] & BIT(pin)))
				continue;

			/* Set pin to 'Non-use (Hi-Z input protection)' */
			pm &= ~(PM_MASK << (pin * 2));
			writew(pm, pctrl->base + PM(off));

			/* Temporarily switch to GPIO mode with PMC register */
			pmc &= ~BIT(pin);
			writeb(pmc, pctrl->base + PMC(off));

			/* Select Pin function mode. */
			pfc &= ~(PFC_MASK << (pin * 4));
			pfc |= (cache->pfc[port] & (PFC_MASK << (pin * 4)));
			writel(pfc, pctrl->base + PFC(off));

			/* Switch to Peripheral pin function. */
			pmc |= BIT(pin);
			writeb(pmc, pctrl->base + PMC(off));
		}
	}

	pctrl->data->pwpr_pfc_lock_unlock(pctrl, true);
	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_pinctrl_suspend_noirq(struct device *dev)
{
	struct rzg2l_pinctrl *pctrl = dev_get_drvdata(dev);
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct rzg2l_register_offsets *regs = &hwcfg->regs;
	struct rzg2l_pinctrl_reg_cache *cache = pctrl->cache;

	rzg2l_pinctrl_pm_setup_regs(pctrl, true);
	rzg2l_pinctrl_pm_setup_dedicated_regs(pctrl, true);

	for (u8 i = 0; i < 2; i++) {
		if (regs->sd_ch)
			cache->sd_ch[i] = readb(pctrl->base + SD_CH(regs->sd_ch, i));
		if (regs->eth_poc)
			cache->eth_poc[i] = readb(pctrl->base + ETH_POC(regs->eth_poc, i));
	}

	cache->qspi = readb(pctrl->base + QSPI);
	cache->eth_mode = readb(pctrl->base + ETH_MODE);

	if (!atomic_read(&pctrl->wakeup_path))
		clk_disable_unprepare(pctrl->clk);
	else
		device_set_wakeup_path(dev);

	return 0;
}

static int rzg2l_pinctrl_resume_noirq(struct device *dev)
{
	struct rzg2l_pinctrl *pctrl = dev_get_drvdata(dev);
	const struct rzg2l_hwcfg *hwcfg = pctrl->data->hwcfg;
	const struct rzg2l_register_offsets *regs = &hwcfg->regs;
	struct rzg2l_pinctrl_reg_cache *cache = pctrl->cache;
	int ret;

	if (!atomic_read(&pctrl->wakeup_path)) {
		ret = clk_prepare_enable(pctrl->clk);
		if (ret)
			return ret;
	}

	writeb(cache->qspi, pctrl->base + QSPI);
	writeb(cache->eth_mode, pctrl->base + ETH_MODE);
	for (u8 i = 0; i < 2; i++) {
		if (regs->sd_ch)
			writeb(cache->sd_ch[i], pctrl->base + SD_CH(regs->sd_ch, i));
		if (regs->eth_poc)
			writeb(cache->eth_poc[i], pctrl->base + ETH_POC(regs->eth_poc, i));
	}

	rzg2l_pinctrl_pm_setup_pfc(pctrl);
	rzg2l_pinctrl_pm_setup_regs(pctrl, false);
	rzg2l_pinctrl_pm_setup_dedicated_regs(pctrl, false);
	rzg2l_gpio_irq_restore(pctrl);

	return 0;
}

static void rzg2l_pwpr_pfc_lock_unlock(struct rzg2l_pinctrl *pctrl, bool lock)
{
	const struct rzg2l_register_offsets *regs = &pctrl->data->hwcfg->regs;

	if (lock) {
		/* Set the PWPR register to be write-protected */
		writel(0x0, pctrl->base + regs->pwpr);		/* B0WI=0, PFCWE=0 */
		writel(PWPR_B0WI, pctrl->base + regs->pwpr);	/* B0WI=1, PFCWE=0 */
	} else {
		/* Set the PWPR register to allow PFC register to write */
		writel(0x0, pctrl->base + regs->pwpr);		/* B0WI=0, PFCWE=0 */
		writel(PWPR_PFCWE, pctrl->base + regs->pwpr);	/* B0WI=0, PFCWE=1 */
	}
}

static void rzv2h_pwpr_pfc_lock_unlock(struct rzg2l_pinctrl *pctrl, bool lock)
{
	const struct rzg2l_register_offsets *regs = &pctrl->data->hwcfg->regs;
	u8 pwpr;

	if (lock) {
		/* Set the PWPR register to be write-protected */
		pwpr = readb(pctrl->base + regs->pwpr);
		writeb(pwpr & ~PWPR_REGWE_A, pctrl->base + regs->pwpr);
	} else {
		/* Set the PWPR register to allow PFC and PMC register to write */
		pwpr = readb(pctrl->base + regs->pwpr);
		writeb(PWPR_REGWE_A | pwpr, pctrl->base + regs->pwpr);
	}
}

static const struct rzg2l_hwcfg rzg2l_hwcfg = {
	.regs = {
		.pwpr = 0x3014,
		.sd_ch = 0x3000,
		.eth_poc = 0x300c,
	},
	.iolh_groupa_ua = {
		/* 3v3 power source */
		[RZG2L_IOLH_IDX_3V3] = 2000, 4000, 8000, 12000,
	},
	.iolh_groupb_oi = { 100, 66, 50, 33, },
	.oen_max_pin = 0,
};

static const struct rzg2l_hwcfg rzg3s_hwcfg = {
	.regs = {
		.pwpr = 0x3000,
		.sd_ch = 0x3004,
		.eth_poc = 0x3010,
	},
	.iolh_groupa_ua = {
		/* 1v8 power source */
		[RZG2L_IOLH_IDX_1V8] = 2200, 4400, 9000, 10000,
		/* 3v3 power source */
		[RZG2L_IOLH_IDX_3V3] = 1900, 4000, 8000, 9000,
	},
	.iolh_groupb_ua = {
		/* 1v8 power source */
		[RZG2L_IOLH_IDX_1V8] = 7000, 8000, 9000, 10000,
		/* 3v3 power source */
		[RZG2L_IOLH_IDX_3V3] = 4000, 6000, 8000, 9000,
	},
	.iolh_groupc_ua = {
		/* 1v8 power source */
		[RZG2L_IOLH_IDX_1V8] = 5200, 6000, 6550, 6800,
		/* 2v5 source */
		[RZG2L_IOLH_IDX_2V5] = 4700, 5300, 5800, 6100,
		/* 3v3 power source */
		[RZG2L_IOLH_IDX_3V3] = 4500, 5200, 5700, 6050,
	},
	.drive_strength_ua = true,
	.func_base = 1,
	.oen_max_pin = 1, /* Pin 1 of P0 and P7 is the maximum OEN pin. */
	.oen_max_port = 7, /* P7_1 is the maximum OEN port. */
};

static const struct rzg2l_hwcfg rzv2h_hwcfg = {
	.regs = {
		.pwpr = 0x3c04,
	},
};

static struct rzg2l_pinctrl_data r9a07g043_data = {
	.port_pins = rzg2l_gpio_names,
	.port_pin_configs = r9a07g043_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a07g043_gpio_configs),
	.dedicated_pins = rzg2l_dedicated_pins.common,
	.n_port_pins = ARRAY_SIZE(r9a07g043_gpio_configs) * RZG2L_PINS_PER_PORT,
	.n_dedicated_pins = ARRAY_SIZE(rzg2l_dedicated_pins.common),
	.hwcfg = &rzg2l_hwcfg,
#ifdef CONFIG_RISCV
	.variable_pin_cfg = r9a07g043f_variable_pin_cfg,
	.n_variable_pin_cfg = ARRAY_SIZE(r9a07g043f_variable_pin_cfg),
#endif
	.pwpr_pfc_lock_unlock = &rzg2l_pwpr_pfc_lock_unlock,
	.pmc_writeb = &rzg2l_pmc_writeb,
	.oen_read = &rzg2l_read_oen,
	.oen_write = &rzg2l_write_oen,
	.hw_to_bias_param = &rzg2l_hw_to_bias_param,
	.bias_param_to_hw = &rzg2l_bias_param_to_hw,
};

static struct rzg2l_pinctrl_data r9a07g044_data = {
	.port_pins = rzg2l_gpio_names,
	.port_pin_configs = r9a07g044_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a07g044_gpio_configs),
	.dedicated_pins = rzg2l_dedicated_pins.common,
	.n_port_pins = ARRAY_SIZE(r9a07g044_gpio_configs) * RZG2L_PINS_PER_PORT,
	.n_dedicated_pins = ARRAY_SIZE(rzg2l_dedicated_pins.common) +
		ARRAY_SIZE(rzg2l_dedicated_pins.rzg2l_pins),
	.hwcfg = &rzg2l_hwcfg,
	.pwpr_pfc_lock_unlock = &rzg2l_pwpr_pfc_lock_unlock,
	.pmc_writeb = &rzg2l_pmc_writeb,
	.oen_read = &rzg2l_read_oen,
	.oen_write = &rzg2l_write_oen,
	.hw_to_bias_param = &rzg2l_hw_to_bias_param,
	.bias_param_to_hw = &rzg2l_bias_param_to_hw,
};

static struct rzg2l_pinctrl_data r9a08g045_data = {
	.port_pins = rzg2l_gpio_names,
	.port_pin_configs = r9a08g045_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a08g045_gpio_configs),
	.dedicated_pins = rzg3s_dedicated_pins,
	.n_port_pins = ARRAY_SIZE(r9a08g045_gpio_configs) * RZG2L_PINS_PER_PORT,
	.n_dedicated_pins = ARRAY_SIZE(rzg3s_dedicated_pins),
	.hwcfg = &rzg3s_hwcfg,
	.pwpr_pfc_lock_unlock = &rzg2l_pwpr_pfc_lock_unlock,
	.pmc_writeb = &rzg2l_pmc_writeb,
	.oen_read = &rzg3s_oen_read,
	.oen_write = &rzg3s_oen_write,
	.hw_to_bias_param = &rzg2l_hw_to_bias_param,
	.bias_param_to_hw = &rzg2l_bias_param_to_hw,
};

static struct rzg2l_pinctrl_data r9a09g057_data = {
	.port_pins = rzv2h_gpio_names,
	.port_pin_configs = r9a09g057_gpio_configs,
	.n_ports = ARRAY_SIZE(r9a09g057_gpio_configs),
	.dedicated_pins = rzv2h_dedicated_pins,
	.n_port_pins = ARRAY_SIZE(r9a09g057_gpio_configs) * RZG2L_PINS_PER_PORT,
	.n_dedicated_pins = ARRAY_SIZE(rzv2h_dedicated_pins),
	.hwcfg = &rzv2h_hwcfg,
	.variable_pin_cfg = r9a09g057_variable_pin_cfg,
	.n_variable_pin_cfg = ARRAY_SIZE(r9a09g057_variable_pin_cfg),
	.num_custom_params = ARRAY_SIZE(renesas_rzv2h_custom_bindings),
	.custom_params = renesas_rzv2h_custom_bindings,
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items = renesas_rzv2h_conf_items,
#endif
	.pwpr_pfc_lock_unlock = &rzv2h_pwpr_pfc_lock_unlock,
	.pmc_writeb = &rzv2h_pmc_writeb,
	.oen_read = &rzv2h_oen_read,
	.oen_write = &rzv2h_oen_write,
	.hw_to_bias_param = &rzv2h_hw_to_bias_param,
	.bias_param_to_hw = &rzv2h_bias_param_to_hw,
};

static const struct of_device_id rzg2l_pinctrl_of_table[] = {
	{
		.compatible = "renesas,r9a07g043-pinctrl",
		.data = &r9a07g043_data,
	},
	{
		.compatible = "renesas,r9a07g044-pinctrl",
		.data = &r9a07g044_data,
	},
	{
		.compatible = "renesas,r9a08g045-pinctrl",
		.data = &r9a08g045_data,
	},
	{
		.compatible = "renesas,r9a09g057-pinctrl",
		.data = &r9a09g057_data,
	},
	{ /* sentinel */ }
};

static const struct dev_pm_ops rzg2l_pinctrl_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(rzg2l_pinctrl_suspend_noirq, rzg2l_pinctrl_resume_noirq)
};

static struct platform_driver rzg2l_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzg2l_pinctrl_of_table),
		.pm = pm_sleep_ptr(&rzg2l_pinctrl_pm_ops),
	},
	.probe = rzg2l_pinctrl_probe,
};

static int __init rzg2l_pinctrl_init(void)
{
	return platform_driver_register(&rzg2l_pinctrl_driver);
}
core_initcall(rzg2l_pinctrl_init);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Pin and gpio controller driver for RZ/G2L family");
