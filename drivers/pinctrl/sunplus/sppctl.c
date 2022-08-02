// SPDX-License-Identifier: GPL-2.0
/*
 * SP7021 Pin Controller Driver.
 * Copyright (C) Sunplus Tech / Tibbo Tech.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/sppctl-sp7021.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#include "sppctl.h"

struct sppctl_gpio_chip {
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN, I_INV, O_INV, OD */
	void __iomem *first_base;	/* GPIO_FIRST                            */

	struct gpio_chip chip;
	spinlock_t lock;		/* lock for accessing OE register        */
};

static inline u32 sppctl_first_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST + off);
}

static inline void sppctl_first_writel(struct sppctl_gpio_chip *spp_gchip, u32 val, u32 off)
{
	writel(val, spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST + off);
}

static inline u32 sppctl_gpio_master_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_MASTER + off);
}

static inline void sppctl_gpio_master_writel(struct sppctl_gpio_chip *spp_gchip, u32 val,
					     u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_MASTER + off);
}

static inline u32 sppctl_gpio_oe_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OE + off);
}

static inline void sppctl_gpio_oe_writel(struct sppctl_gpio_chip *spp_gchip, u32 val, u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OE + off);
}

static inline void sppctl_gpio_out_writel(struct sppctl_gpio_chip *spp_gchip, u32 val, u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OUT + off);
}

static inline u32 sppctl_gpio_in_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_IN + off);
}

static inline u32 sppctl_gpio_iinv_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_IINV + off);
}

static inline void sppctl_gpio_iinv_writel(struct sppctl_gpio_chip *spp_gchip, u32 val,
					   u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_IINV + off);
}

static inline u32 sppctl_gpio_oinv_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OINV + off);
}

static inline void sppctl_gpio_oinv_writel(struct sppctl_gpio_chip *spp_gchip, u32 val,
					   u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OINV + off);
}

static inline u32 sppctl_gpio_od_readl(struct sppctl_gpio_chip *spp_gchip, u32 off)
{
	return readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OD + off);
}

static inline void sppctl_gpio_od_writel(struct sppctl_gpio_chip *spp_gchip, u32 val, u32 off)
{
	writel(val, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OD + off);
}

static inline u32 sppctl_get_reg_and_bit_offset(unsigned int offset, u32 *reg_off)
{
	u32 bit_off;

	/* Each register has 32 bits. */
	*reg_off = (offset / 32) * 4;
	bit_off = offset % 32;

	return bit_off;
}

static inline u32 sppctl_get_moon_reg_and_bit_offset(unsigned int offset, u32 *reg_off)
{
	u32 bit_off;

	/*
	 * Each MOON register has 32 bits. Upper 16-bit word are mask-fields.
	 * The lower 16-bit word are the control-fields. The corresponding
	 * bits in mask-field should be set then you can write something to
	 * control-field.
	 */
	*reg_off = (offset / 16) * 4;
	bit_off = offset % 16;

	return bit_off;
}

static inline u32 sppctl_prep_moon_reg_and_offset(unsigned int offset, u32 *reg_off, int val)
{
	u32 bit_off;

	bit_off = sppctl_get_moon_reg_and_bit_offset(offset, reg_off);
	if (val)
		return SPPCTL_SET_MOON_REG_BIT(bit_off);
	else
		return SPPCTL_CLR_MOON_REG_BIT(bit_off);
}

/**
 * sppctl_func_set() - Set pin of fully-pinmux function.
 *
 * Mask-fields and control-fields of fully-pinmux function of SP7021 are
 * arranged as shown below:
 *
 *  func# | register |  mask-field  | control-field
 * -------+----------+--------------+---------------
 *    0   | base[0]  |  (22 : 16)   |   ( 6 : 0)
 *    1   | base[0]  |  (30 : 24)   |   (14 : 8)
 *    2   | base[1]  |  (22 : 16)   |   ( 6 : 0)
 *    3   | baeg[1]  |  (30 : 24)   |   (14 : 8)
 *    :   |    :     |      :       |       :
 *
 * where mask-fields are used to protect control-fields from write-in
 * accidentally. Set the corresponding bits in the mask-field before
 * you write a value into a control-field.
 *
 * Control-fields are used to set where the function pin is going to
 * be routed to.
 *
 * Note that mask-fields and control-fields of even number of 'func'
 * are located at bits (22:16) and (6:0), while odd number of 'func's
 * are located at bits (30:24) and (14:8).
 */
static void sppctl_func_set(struct sppctl_pdata *pctl, u8 func, u8 val)
{
	u32 reg, offset;

	/*
	 * Note that upper 16-bit word are mask-fields and lower 16-bit
	 * word are the control-fields. Set corresponding bits in mask-
	 * field before write to a control-field.
	 */
	reg = SPPCTL_FULLY_PINMUX_MASK_MASK | val;

	/*
	 * MUXF_L2SW_CLK_OUT is the first fully-pinmux pin
	 * and its register offset is 0.
	 */
	func -= MUXF_L2SW_CLK_OUT;

	/*
	 * Check if 'func' is an odd number or not. Mask and control-
	 * fields of odd number 'func' is located at upper portion of
	 * a register. Extra shift is needed.
	 */
	if (func & BIT(0))
		reg <<= SPPCTL_FULLY_PINMUX_UPPER_SHIFT;

	/* Convert func# to register offset w.r.t. base register. */
	offset = func * 2;
	offset &= GENMASK(31, 2);

	writel(reg, pctl->moon2_base + offset);
}

/**
 * sppctl_gmx_set() - Set pin of group-pinmux.
 *
 * Mask-fields and control-fields of group-pinmux function of SP7021 are
 * arranged as shown below:
 *
 *  register |  mask-fields | control-fields
 * ----------+--------------+----------------
 *  base[0]  |  (31 : 16)   |   (15 : 0)
 *  base[1]  |  (31 : 24)   |   (15 : 0)
 *  base[2]  |  (31 : 24)   |   (15 : 0)
 *     :     |      :       |       :
 *
 * where mask-fields are used to protect control-fields from write-in
 * accidentally. Set the corresponding bits in the mask-field before
 * you write a value into a control-field.
 *
 * Control-fields are used to set where the function pin is going to
 * be routed to. A control-field consists of one or more bits.
 */
static void sppctl_gmx_set(struct sppctl_pdata *pctl, u8 reg_off, u8 bit_off, u8 bit_sz,
			   u8 val)
{
	u32 mask, reg;

	/*
	 * Note that upper 16-bit word are mask-fields and lower 16-bit
	 * word are the control-fields. Set corresponding bits in mask-
	 * field before write to a control-field.
	 */
	mask = GENMASK(bit_sz - 1, 0) << SPPCTL_MOON_REG_MASK_SHIFT;
	reg = (mask | val) << bit_off;

	writel(reg, pctl->moon1_base + reg_off * 4);
}

/**
 * sppctl_first_get() - get bit of FIRST register.
 *
 * There are 4 FIRST registers. Each has 32 control-bits.
 * Totally, there are 4 * 32 = 128 control-bits.
 * Control-bits are arranged as shown below:
 *
 *  registers | control-bits
 * -----------+--------------
 *  first[0]  |  (31 :  0)
 *  first[1]  |  (63 : 32)
 *  first[2]  |  (95 : 64)
 *  first[3]  | (127 : 96)
 *
 * Each control-bit sets type of a GPIO pin.
 *   0: a fully-pinmux pin
 *   1: a GPIO or IOP pin
 */
static int sppctl_first_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	bit_off = sppctl_get_reg_and_bit_offset(offset, &reg_off);
	reg = sppctl_first_readl(spp_gchip, reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

/**
 * sppctl_master_get() - get bit of MASTER register.
 *
 * There are 8 MASTER registers. Each has 16 mask-bits and 16 control-bits.
 * Upper 16-bit of MASTER registers are mask-bits while lower 16-bit are
 * control-bits. Totally, there are 128 mask-bits and 128 control-bits.
 * They are arranged as shown below:
 *
 *  register  |  mask-bits  | control-bits
 * -----------+-------------+--------------
 *  master[0] |  (15 :   0) |  (15 :   0)
 *  master[1] |  (31 :  16) |  (31 :  16)
 *  master[2] |  (47 :  32) |  (47 :  32)
 *     :      |      :      |      :
 *  master[7] | (127 : 112) | (127 : 112)
 *
 * where mask-bits are used to protect control-bits from write-in
 * accidentally. Set the corresponding mask-bit before you write
 * a value into a control-bit.
 *
 * Each control-bit sets type of a GPIO pin when FIRST bit is 1.
 *   0: a IOP pin
 *   1: a GPIO pin
 */
static int sppctl_master_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	bit_off = sppctl_get_moon_reg_and_bit_offset(offset, &reg_off);
	reg = sppctl_gpio_master_readl(spp_gchip, reg_off);
	return (reg & BIT(bit_off)) ? 1 : 0;
}

static void sppctl_first_master_set(struct gpio_chip *chip, unsigned int offset,
				    enum mux_first_reg first, enum mux_master_reg master)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;
	enum mux_first_reg val;

	/* FIRST register */
	if (first != mux_f_keep) {
		bit_off = sppctl_get_reg_and_bit_offset(offset, &reg_off);
		reg = sppctl_first_readl(spp_gchip, reg_off);
		val = (reg & BIT(bit_off)) ? mux_f_gpio : mux_f_mux;

		if (first != val)
			switch (first) {
			case mux_f_gpio:
				reg |= BIT(bit_off);
				sppctl_first_writel(spp_gchip, reg, reg_off);
				break;

			case mux_f_mux:
				reg &= ~BIT(bit_off);
				sppctl_first_writel(spp_gchip, reg, reg_off);
				break;

			case mux_f_keep:
				break;
			}
	}

	/* MASTER register */
	if (master != mux_m_keep) {
		reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, (master == mux_m_gpio));
		sppctl_gpio_master_writel(spp_gchip, reg, reg_off);
	}
}

static void sppctl_gpio_input_inv_set(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, 1);
	sppctl_gpio_iinv_writel(spp_gchip, reg, reg_off);
}

static void sppctl_gpio_output_inv_set(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, 1);
	sppctl_gpio_oinv_writel(spp_gchip, reg, reg_off);
}

static int sppctl_gpio_output_od_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	bit_off = sppctl_get_moon_reg_and_bit_offset(offset, &reg_off);
	reg = sppctl_gpio_od_readl(spp_gchip, reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static void sppctl_gpio_output_od_set(struct gpio_chip *chip, unsigned int offset,
				      unsigned int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, val);
	sppctl_gpio_od_writel(spp_gchip, reg, reg_off);
}

static int sppctl_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	bit_off = sppctl_get_moon_reg_and_bit_offset(offset, &reg_off);
	reg = sppctl_gpio_oe_readl(spp_gchip, reg_off);

	return (reg & BIT(bit_off)) ? 0 : 1;
}

static int sppctl_gpio_inv_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;
	unsigned long flags;

	bit_off = sppctl_get_moon_reg_and_bit_offset(offset, &reg_off);

	spin_lock_irqsave(&spp_gchip->lock, flags);

	if (sppctl_gpio_get_direction(chip, offset))
		reg = sppctl_gpio_iinv_readl(spp_gchip, reg_off);
	else
		reg = sppctl_gpio_oinv_readl(spp_gchip, reg_off);

	spin_unlock_irqrestore(&spp_gchip->lock, flags);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static int sppctl_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	unsigned long flags;
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, 0);

	spin_lock_irqsave(&spp_gchip->lock, flags);

	sppctl_gpio_oe_writel(spp_gchip, reg, reg_off);

	spin_unlock_irqrestore(&spp_gchip->lock, flags);
	return 0;
}

static int sppctl_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	unsigned long flags;
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, 1);

	spin_lock_irqsave(&spp_gchip->lock, flags);

	sppctl_gpio_oe_writel(spp_gchip, reg, reg_off);

	if (val < 0) {
		spin_unlock_irqrestore(&spp_gchip->lock, flags);
		return 0;
	}

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, val);
	sppctl_gpio_out_writel(spp_gchip, reg, reg_off);

	spin_unlock_irqrestore(&spp_gchip->lock, flags);
	return 0;
}

static int sppctl_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	bit_off = sppctl_get_reg_and_bit_offset(offset, &reg_off);
	reg = sppctl_gpio_in_readl(spp_gchip, reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static void sppctl_gpio_set(struct gpio_chip *chip, unsigned int offset, int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, reg;

	reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, val);
	sppctl_gpio_out_writel(spp_gchip, reg, reg_off);
}

static int sppctl_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				  unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, reg;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		reg = sppctl_prep_moon_reg_and_offset(offset, &reg_off, 1);
		sppctl_gpio_od_writel(spp_gchip, reg, reg_off);
		break;

	case PIN_CONFIG_INPUT_ENABLE:
		break;

	case PIN_CONFIG_OUTPUT:
		return sppctl_gpio_direction_output(chip, offset, 0);

	case PIN_CONFIG_PERSIST_STATE:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void sppctl_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	const char *label;
	int i;

	for (i = 0; i < chip->ngpio; i++) {
		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "";

		seq_printf(s, " gpio-%03d (%-16.16s | %-16.16s)", i + chip->base,
			   chip->names[i], label);
		seq_printf(s, " %c", sppctl_gpio_get_direction(chip, i) ? 'I' : 'O');
		seq_printf(s, ":%d", sppctl_gpio_get(chip, i));
		seq_printf(s, " %s", sppctl_first_get(chip, i) ? "gpi" : "mux");
		seq_printf(s, " %s", sppctl_master_get(chip, i) ? "gpi" : "iop");
		seq_printf(s, " %s", sppctl_gpio_inv_get(chip, i) ? "inv" : "   ");
		seq_printf(s, " %s", sppctl_gpio_output_od_get(chip, i) ? "oDr" : "");
		seq_puts(s, "\n");
	}
}
#endif

static int sppctl_gpio_new(struct platform_device *pdev, struct sppctl_pdata *pctl)
{
	struct sppctl_gpio_chip *spp_gchip;
	struct gpio_chip *gchip;
	int err;

	spp_gchip = devm_kzalloc(&pdev->dev, sizeof(*spp_gchip), GFP_KERNEL);
	if (!spp_gchip)
		return -ENOMEM;
	pctl->spp_gchip = spp_gchip;

	spp_gchip->gpioxt_base  = pctl->gpioxt_base;
	spp_gchip->first_base   = pctl->first_base;
	spin_lock_init(&spp_gchip->lock);

	gchip                   = &spp_gchip->chip;
	gchip->label            = SPPCTL_MODULE_NAME;
	gchip->parent           = &pdev->dev;
	gchip->owner            = THIS_MODULE;
	gchip->request          = gpiochip_generic_request;
	gchip->free             = gpiochip_generic_free;
	gchip->get_direction    = sppctl_gpio_get_direction;
	gchip->direction_input  = sppctl_gpio_direction_input;
	gchip->direction_output = sppctl_gpio_direction_output;
	gchip->get              = sppctl_gpio_get;
	gchip->set              = sppctl_gpio_set;
	gchip->set_config       = sppctl_gpio_set_config;
#ifdef CONFIG_DEBUG_FS
	gchip->dbg_show         = sppctl_gpio_dbg_show;
#endif
	gchip->base             = -1;
	gchip->ngpio            = sppctl_gpio_list_sz;
	gchip->names            = sppctl_gpio_list_s;
	gchip->of_gpio_n_cells  = 2;

	pctl->pctl_grange.npins = gchip->ngpio;
	pctl->pctl_grange.name  = gchip->label;
	pctl->pctl_grange.gc    = gchip;

	err = devm_gpiochip_add_data(&pdev->dev, gchip, spp_gchip);
	if (err)
		return dev_err_probe(&pdev->dev, err, "Failed to add gpiochip!\n");

	return 0;
}

static int sppctl_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *config)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	unsigned int arg;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!sppctl_gpio_output_od_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		arg = 0;
		break;

	case PIN_CONFIG_OUTPUT:
		if (!sppctl_first_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		if (!sppctl_master_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		if (sppctl_gpio_get_direction(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		arg = sppctl_gpio_get(&pctl->spp_gchip->chip, pin);
		break;

	default:
		return -EOPNOTSUPP;
	}
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int sppctl_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *configs, unsigned int num_configs)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int i;

	/* Special handling for IOP pins */
	if (configs[0] == SPPCTL_IOP_CONFIGS) {
		sppctl_first_master_set(&pctl->spp_gchip->chip, pin, mux_f_gpio, mux_m_iop);
		return 0;
	}

	for (i = 0; i < num_configs; i++) {
		if (configs[i] & SPPCTL_PCTL_L_OUT)
			sppctl_gpio_direction_output(&pctl->spp_gchip->chip, pin, 0);
		if (configs[i] & SPPCTL_PCTL_L_OU1)
			sppctl_gpio_direction_output(&pctl->spp_gchip->chip, pin, 1);
		if (configs[i] & SPPCTL_PCTL_L_INV)
			sppctl_gpio_input_inv_set(&pctl->spp_gchip->chip, pin);
		if (configs[i] & SPPCTL_PCTL_L_ONV)
			sppctl_gpio_output_inv_set(&pctl->spp_gchip->chip, pin);
		if (configs[i] & SPPCTL_PCTL_L_ODR)
			sppctl_gpio_output_od_set(&pctl->spp_gchip->chip, pin, 1);
	}

	return 0;
}

static const struct pinconf_ops sppctl_pconf_ops = {
	.is_generic     = true,
	.pin_config_get = sppctl_pin_config_get,
	.pin_config_set = sppctl_pin_config_set,
};

static int sppctl_get_functions_count(struct pinctrl_dev *pctldev)
{
	return sppctl_list_funcs_sz;
}

static const char *sppctl_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned int selector)
{
	return sppctl_list_funcs[selector].name;
}

static int sppctl_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				      const char * const **groups, unsigned int *num_groups)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct sppctl_func *f = &sppctl_list_funcs[selector];
	int i;

	*num_groups = 0;
	switch (f->type) {
	case pinmux_type_fpmx:
		*num_groups = sppctl_pmux_list_sz;
		*groups = sppctl_pmux_list_s;
		break;

	case pinmux_type_grp:
		if (!f->grps)
			break;

		*num_groups = f->gnum;
		for (i = 0; i < pctl->unq_grps_sz; i++)
			if (pctl->g2fp_maps[i].f_idx == selector)
				break;
		*groups = &pctl->unq_grps[i];
		break;

	default:
		dev_err(pctldev->dev, "Unknown pinmux (selector: %d, type: %d)\n",
			selector, f->type);
		break;
	}

	return 0;
}

/**
 * sppctl_fully_pinmux_conv - Convert GPIO# to fully-pinmux control-field setting
 *
 * Each fully-pinmux function can be mapped to any of GPIO 8 ~ 71 by
 * settings its control-field. Refer to following table:
 *
 * control-field |  GPIO
 * --------------+--------
 *        0      |  No map
 *        1      |    8
 *        2      |    9
 *        3      |   10
 *        :      |    :
 *       65      |   71
 */
static inline int sppctl_fully_pinmux_conv(unsigned int offset)
{
	return (offset < 8) ? 0 : offset - 7;
}

static int sppctl_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
			  unsigned int group_selector)
{
	const struct sppctl_func *f = &sppctl_list_funcs[func_selector];
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct grp2fp_map g2fpm = pctl->g2fp_maps[group_selector];
	int i;

	switch (f->type) {
	case pinmux_type_fpmx:
		sppctl_first_master_set(&pctl->spp_gchip->chip, group_selector,
					mux_f_mux, mux_m_keep);
		sppctl_func_set(pctl, func_selector, sppctl_fully_pinmux_conv(group_selector));
		break;

	case pinmux_type_grp:
		for (i = 0; i < f->grps[g2fpm.g_idx].pnum; i++)
			sppctl_first_master_set(&pctl->spp_gchip->chip,
						f->grps[g2fpm.g_idx].pins[i],
						mux_f_mux, mux_m_keep);
		sppctl_gmx_set(pctl, f->roff, f->boff, f->blen, f->grps[g2fpm.g_idx].gval);
		break;

	default:
		dev_err(pctldev->dev, "Unknown pinmux type (func_selector: %d, type: %d)\n",
			func_selector, f->type);
		break;
	}

	return 0;
}

static int sppctl_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range, unsigned int offset)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int g_f, g_m;

	g_f = sppctl_first_get(&pctl->spp_gchip->chip, offset);
	g_m = sppctl_master_get(&pctl->spp_gchip->chip, offset);
	if (g_f == mux_f_gpio && g_m == mux_m_gpio)
		return 0;

	sppctl_first_master_set(&pctl->spp_gchip->chip, offset, mux_f_gpio, mux_m_gpio);
	return 0;
}

static const struct pinmux_ops sppctl_pinmux_ops = {
	.get_functions_count = sppctl_get_functions_count,
	.get_function_name   = sppctl_get_function_name,
	.get_function_groups = sppctl_get_function_groups,
	.set_mux             = sppctl_set_mux,
	.gpio_request_enable = sppctl_gpio_request_enable,
	.strict              = true,
};

static int sppctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->unq_grps_sz;
}

static const char *sppctl_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->unq_grps[selector];
}

static int sppctl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				 const unsigned int **pins, unsigned int *num_pins)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct grp2fp_map g2fpm = pctl->g2fp_maps[selector];
	const struct sppctl_func *f;

	f = &sppctl_list_funcs[g2fpm.f_idx];
	*num_pins = 0;

	/* Except group-pinmux, each group has 1 pin. */
	if (f->type != pinmux_type_grp) {
		*num_pins = 1;
		*pins = &sppctl_pins_gpio[selector];
		return 0;
	}

	/* Group-pinmux may have more than one pin. */
	if (!f->grps)
		return 0;

	if (f->gnum < 1)
		return 0;

	*num_pins = f->grps[g2fpm.g_idx].pnum;
	*pins = f->grps[g2fpm.g_idx].pins;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void sppctl_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned int offset)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	const char *pin_type;
	u8 first, master;

	first = sppctl_first_get(&pctl->spp_gchip->chip, offset);
	master = sppctl_master_get(&pctl->spp_gchip->chip, offset);
	if (first)
		if (master)
			pin_type = "GPIO";
		else
			pin_type = " IOP";
	else
		pin_type = " MUX";
	seq_printf(s, " %s", pin_type);
}
#endif

static int sppctl_dt_node_to_map(struct pinctrl_dev *pctldev, struct device_node *np_config,
				 struct pinctrl_map **map, unsigned int *num_maps)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int nmG = of_property_count_strings(np_config, "groups");
	const struct sppctl_func *f = NULL;
	u8 pin_num, pin_type, pin_func;
	struct device_node *parent;
	unsigned long *configs;
	struct property *prop;
	const char *s_f, *s_g;

	const __be32 *list;
	u32 dt_pin, dt_fun;
	int i, size = 0;

	list = of_get_property(np_config, "sunplus,pins", &size);

	if (nmG <= 0)
		nmG = 0;

	parent = of_get_parent(np_config);
	*num_maps = size / sizeof(*list);

	/*
	 * Process property:
	 *     sunplus,pins = < u32 u32 u32 ... >;
	 *
	 * Each 32-bit integer defines a individual pin in which:
	 *
	 *   Bit 32~24: defines GPIO pin number. Its range is 0 ~ 98.
	 *   Bit 23~16: defines types: (1) fully-pinmux pins
	 *                             (2) IO processor pins
	 *                             (3) digital GPIO pins
	 *   Bit 15~8:  defines pins of peripherals (which are defined in
	 *              'include/dt-binging/pinctrl/sppctl.h').
	 *   Bit 7~0:   defines types or initial-state of digital GPIO pins.
	 */
	for (i = 0; i < (*num_maps); i++) {
		dt_pin = be32_to_cpu(list[i]);
		pin_num = FIELD_GET(GENMASK(31, 24), dt_pin);

		if (pin_num >= sppctl_pins_all_sz) {
			dev_err(pctldev->dev, "Invalid pin property at index %d (0x%08x)\n",
				i, dt_pin);
			return -EINVAL;
		}
	}

	*map = kcalloc(*num_maps + nmG, sizeof(**map), GFP_KERNEL);
	for (i = 0; i < (*num_maps); i++) {
		dt_pin = be32_to_cpu(list[i]);
		pin_num = FIELD_GET(GENMASK(31, 24), dt_pin);
		pin_type = FIELD_GET(GENMASK(23, 16), dt_pin);
		pin_func = FIELD_GET(GENMASK(15, 8), dt_pin);
		(*map)[i].name = parent->name;

		if (pin_type == SPPCTL_PCTL_G_GPIO) {
			/* A digital GPIO pin */
			(*map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*map)[i].data.configs.num_configs = 1;
			(*map)[i].data.configs.group_or_pin = pin_get_name(pctldev, pin_num);
			configs = kmalloc(sizeof(*configs), GFP_KERNEL);
			*configs = FIELD_GET(GENMASK(7, 0), dt_pin);
			(*map)[i].data.configs.configs = configs;

			dev_dbg(pctldev->dev, "%s: GPIO (%s)\n",
				(*map)[i].data.configs.group_or_pin,
				(*configs & (SPPCTL_PCTL_L_OUT | SPPCTL_PCTL_L_OU1)) ?
				"OUT" : "IN");
		} else if (pin_type == SPPCTL_PCTL_G_IOPP) {
			/* A IO Processor (IOP) pin */
			(*map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*map)[i].data.configs.num_configs = 1;
			(*map)[i].data.configs.group_or_pin = pin_get_name(pctldev, pin_num);
			configs = kmalloc(sizeof(*configs), GFP_KERNEL);
			*configs = SPPCTL_IOP_CONFIGS;
			(*map)[i].data.configs.configs = configs;

			dev_dbg(pctldev->dev, "%s: IOP\n",
				(*map)[i].data.configs.group_or_pin);
		} else {
			/* A fully-pinmux pin */
			(*map)[i].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[i].data.mux.function = sppctl_list_funcs[pin_func].name;
			(*map)[i].data.mux.group = pin_get_name(pctldev, pin_num);

			dev_dbg(pctldev->dev, "%s: %s\n", (*map)[i].data.mux.group,
				(*map)[i].data.mux.function);
		}
	}

	/*
	 * Process properties:
	 *     function = "xxx";
	 *     groups = "yyy";
	 */
	if (nmG > 0 && of_property_read_string(np_config, "function", &s_f) == 0) {
		of_property_for_each_string(np_config, "groups", prop, s_g) {
			(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[*num_maps].data.mux.function = s_f;
			(*map)[*num_maps].data.mux.group = s_g;
			(*num_maps)++;

			dev_dbg(pctldev->dev, "%s: %s\n", s_f, s_g);
		}
	}

	/*
	 * Process property:
	 *     sunplus,zerofunc = < u32 u32 u32 ...>
	 */
	list = of_get_property(np_config, "sunplus,zerofunc", &size);
	if (list) {
		for (i = 0; i < (size / sizeof(*list)); i++) {
			dt_fun = be32_to_cpu(list[i]);
			if (dt_fun >= sppctl_list_funcs_sz) {
				dev_err(pctldev->dev, "Zero-func %d out of range!\n",
					dt_fun);
				continue;
			}

			f = &sppctl_list_funcs[dt_fun];
			switch (f->type) {
			case pinmux_type_fpmx:
				sppctl_func_set(pctl, dt_fun, 0);
				dev_dbg(pctldev->dev, "%s: No map\n", f->name);
				break;

			case pinmux_type_grp:
				sppctl_gmx_set(pctl, f->roff, f->boff, f->blen, 0);
				dev_dbg(pctldev->dev, "%s: No map\n", f->name);
				break;

			default:
				dev_err(pctldev->dev, "Wrong zero-group: %d (%s)\n",
					dt_fun, f->name);
				break;
			}
		}
	}

	of_node_put(parent);
	dev_dbg(pctldev->dev, "%d pins mapped\n", *num_maps);
	return 0;
}

static const struct pinctrl_ops sppctl_pctl_ops = {
	.get_groups_count = sppctl_get_groups_count,
	.get_group_name   = sppctl_get_group_name,
	.get_group_pins   = sppctl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show     = sppctl_pin_dbg_show,
#endif
	.dt_node_to_map   = sppctl_dt_node_to_map,
	.dt_free_map      = pinctrl_utils_free_map,
};

static int sppctl_group_groups(struct platform_device *pdev)
{
	struct sppctl_pdata *sppctl = platform_get_drvdata(pdev);
	int i, k, j;

	/* Calculate number of total group (GPIO + group-pinmux group). */
	sppctl->unq_grps_sz = sppctl_gpio_list_sz;
	for (i = 0; i < sppctl_list_funcs_sz; i++)
		if (sppctl_list_funcs[i].type == pinmux_type_grp)
			sppctl->unq_grps_sz += sppctl_list_funcs[i].gnum;

	sppctl->unq_grps = devm_kcalloc(&pdev->dev, sppctl->unq_grps_sz + 1,
					sizeof(*sppctl->unq_grps), GFP_KERNEL);
	if (!sppctl->unq_grps)
		return -ENOMEM;

	sppctl->g2fp_maps = devm_kcalloc(&pdev->dev, sppctl->unq_grps_sz + 1,
					 sizeof(*sppctl->g2fp_maps), GFP_KERNEL);
	if (!sppctl->g2fp_maps)
		return -ENOMEM;

	/* Add GPIO pins. */
	for (i = 0; i < sppctl_gpio_list_sz; i++) {
		sppctl->unq_grps[i] = sppctl_gpio_list_s[i];
		sppctl->g2fp_maps[i].f_idx = 0;
		sppctl->g2fp_maps[i].g_idx = i;
	}

	/* Add group-pinmux to end of GPIO pins. */
	j = sppctl_gpio_list_sz;
	for (i = 0; i < sppctl_list_funcs_sz; i++) {
		if (sppctl_list_funcs[i].type != pinmux_type_grp)
			continue;

		for (k = 0; k < sppctl_list_funcs[i].gnum; k++) {
			sppctl->unq_grps[j] = sppctl_list_funcs[i].grps[k].name;
			sppctl->g2fp_maps[j].f_idx = i;
			sppctl->g2fp_maps[j].g_idx = k;
			j++;
		}
	}

	return 0;
}

static int sppctl_pinctrl_init(struct platform_device *pdev)
{
	struct sppctl_pdata *sppctl = platform_get_drvdata(pdev);
	int err;

	sppctl->pctl_desc.owner   = THIS_MODULE;
	sppctl->pctl_desc.name    = dev_name(&pdev->dev);
	sppctl->pctl_desc.pins    = sppctl_pins_all;
	sppctl->pctl_desc.npins   = sppctl_pins_all_sz;
	sppctl->pctl_desc.pctlops = &sppctl_pctl_ops;
	sppctl->pctl_desc.confops = &sppctl_pconf_ops;
	sppctl->pctl_desc.pmxops  = &sppctl_pinmux_ops;

	err = sppctl_group_groups(pdev);
	if (err)
		return err;

	err = devm_pinctrl_register_and_init(&pdev->dev, &sppctl->pctl_desc,
					     sppctl, &sppctl->pctl_dev);
	if (err)
		return dev_err_probe(&pdev->dev, err, "Failed to register pinctrl!\n");

	pinctrl_enable(sppctl->pctl_dev);
	return 0;
}

static int sppctl_resource_map(struct platform_device *pdev, struct sppctl_pdata *sppctl)
{
	sppctl->moon2_base = devm_platform_ioremap_resource_byname(pdev, "moon2");
	if (IS_ERR(sppctl->moon2_base))
		return PTR_ERR(sppctl->moon2_base);

	sppctl->gpioxt_base = devm_platform_ioremap_resource_byname(pdev, "gpioxt");
	if (IS_ERR(sppctl->gpioxt_base))
		return PTR_ERR(sppctl->gpioxt_base);

	sppctl->first_base = devm_platform_ioremap_resource_byname(pdev, "first");
	if (IS_ERR(sppctl->first_base))
		return PTR_ERR(sppctl->first_base);

	sppctl->moon1_base = devm_platform_ioremap_resource_byname(pdev, "moon1");
	if (IS_ERR(sppctl->moon1_base))
		return PTR_ERR(sppctl->moon1_base);

	return 0;
}

static int sppctl_probe(struct platform_device *pdev)
{
	struct sppctl_pdata *sppctl;
	int ret;

	sppctl = devm_kzalloc(&pdev->dev, sizeof(*sppctl), GFP_KERNEL);
	if (!sppctl)
		return -ENOMEM;
	platform_set_drvdata(pdev, sppctl);

	ret = sppctl_resource_map(pdev, sppctl);
	if (ret)
		return ret;

	ret = sppctl_gpio_new(pdev, sppctl);
	if (ret)
		return ret;

	ret = sppctl_pinctrl_init(pdev);
	if (ret)
		return ret;

	pinctrl_add_gpio_range(sppctl->pctl_dev, &sppctl->pctl_grange);

	return 0;
}

static const struct of_device_id sppctl_match_table[] = {
	{ .compatible = "sunplus,sp7021-pctl" },
	{ /* sentinel */ }
};

static struct platform_driver sppctl_pinctrl_driver = {
	.driver = {
		.name           = SPPCTL_MODULE_NAME,
		.of_match_table = sppctl_match_table,
	},
	.probe  = sppctl_probe,
};
builtin_platform_driver(sppctl_pinctrl_driver)

MODULE_AUTHOR("Dvorkin Dmitry <dvorkin@tibbo.com>");
MODULE_AUTHOR("Wells Lu <wellslutw@gmail.com>");
MODULE_DESCRIPTION("Sunplus SP7021 Pin Control and GPIO driver");
MODULE_LICENSE("GPL v2");
