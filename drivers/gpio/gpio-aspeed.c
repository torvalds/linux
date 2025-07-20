// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 */

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/gpio/aspeed.h>
#include <linux/gpio/driver.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/div64.h>

/*
 * These two headers aren't meant to be used by GPIO drivers. We need
 * them in order to access gpio_chip_hwgpio() which we need to implement
 * the aspeed specific API which allows the coprocessor to request
 * access to some GPIOs and to arbitrate between coprocessor and ARM.
 */
#include <linux/gpio/consumer.h>
#include "gpiolib.h"

/* Non-constant mask variant of FIELD_GET() and FIELD_PREP() */
#define field_get(_mask, _reg)	(((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define field_prep(_mask, _val)	(((_val) << (ffs(_mask) - 1)) & (_mask))

#define GPIO_G7_IRQ_STS_BASE 0x100
#define GPIO_G7_IRQ_STS_OFFSET(x) (GPIO_G7_IRQ_STS_BASE + (x) * 0x4)
#define GPIO_G7_CTRL_REG_BASE 0x180
#define GPIO_G7_CTRL_REG_OFFSET(x) (GPIO_G7_CTRL_REG_BASE + (x) * 0x4)
#define GPIO_G7_CTRL_OUT_DATA BIT(0)
#define GPIO_G7_CTRL_DIR BIT(1)
#define GPIO_G7_CTRL_IRQ_EN BIT(2)
#define GPIO_G7_CTRL_IRQ_TYPE0 BIT(3)
#define GPIO_G7_CTRL_IRQ_TYPE1 BIT(4)
#define GPIO_G7_CTRL_IRQ_TYPE2 BIT(5)
#define GPIO_G7_CTRL_RST_TOLERANCE BIT(6)
#define GPIO_G7_CTRL_DEBOUNCE_SEL1 BIT(7)
#define GPIO_G7_CTRL_DEBOUNCE_SEL2 BIT(8)
#define GPIO_G7_CTRL_INPUT_MASK BIT(9)
#define GPIO_G7_CTRL_IRQ_STS BIT(12)
#define GPIO_G7_CTRL_IN_DATA BIT(13)

struct aspeed_bank_props {
	unsigned int bank;
	u32 input;
	u32 output;
};

struct aspeed_gpio_config {
	unsigned int nr_gpios;
	const struct aspeed_bank_props *props;
	const struct aspeed_gpio_llops *llops;
	const int *debounce_timers_array;
	int debounce_timers_num;
	bool require_dcache;
};

/*
 * @offset_timer: Maps an offset to an @timer_users index, or zero if disabled
 * @timer_users: Tracks the number of users for each timer
 *
 * The @timer_users has four elements but the first element is unused. This is
 * to simplify accounting and indexing, as a zero value in @offset_timer
 * represents disabled debouncing for the GPIO. Any other value for an element
 * of @offset_timer is used as an index into @timer_users. This behaviour of
 * the zero value aligns with the behaviour of zero built from the timer
 * configuration registers (i.e. debouncing is disabled).
 */
struct aspeed_gpio {
	struct gpio_chip chip;
	struct device *dev;
	raw_spinlock_t lock;
	void __iomem *base;
	int irq;
	const struct aspeed_gpio_config *config;

	u8 *offset_timer;
	unsigned int timer_users[4];
	struct clk *clk;

	u32 *dcache;
	u8 *cf_copro_bankmap;
};

struct aspeed_gpio_bank {
	uint16_t	val_regs;	/* +0: Rd: read input value, Wr: set write latch
					 * +4: Rd/Wr: Direction (0=in, 1=out)
					 */
	uint16_t	rdata_reg;	/*     Rd: read write latch, Wr: <none>  */
	uint16_t	irq_regs;
	uint16_t	debounce_regs;
	uint16_t	tolerance_regs;
	uint16_t	cmdsrc_regs;
};

/*
 * Note: The "value" register returns the input value sampled on the
 *       line even when the GPIO is configured as an output. Since
 *       that input goes through synchronizers, writing, then reading
 *       back may not return the written value right away.
 *
 *       The "rdata" register returns the content of the write latch
 *       and thus can be used to read back what was last written
 *       reliably.
 */

static const int debounce_timers[4] = { 0x00, 0x50, 0x54, 0x58 };
static const int g7_debounce_timers[4] = { 0x00, 0x00, 0x04, 0x08 };

/*
 * The debounce timers array is used to configure the debounce timer settings.Here’s how it works:
 * Array Value: Indicates the offset for configuring the debounce timer.
 * Array Index: Corresponds to the debounce setting register.
 * The debounce timers array follows this pattern for configuring the debounce setting registers:
 * Array Index 0: No debounce timer is set;
 *		  Array Value is irrelevant (don’t care).
 * Array Index 1: Debounce setting #2 is set to 1, and debounce setting #1 is set to 0.
 *		  Array Value: offset for configuring debounce timer 0 (g4: 0x50, g7: 0x00)
 * Array Index 2: Debounce setting #2 is set to 0, and debounce setting #1 is set to 1.
 *		  Array Value: offset for configuring debounce timer 1 (g4: 0x54, g7: 0x04)
 * Array Index 3: Debounce setting #2 is set to 1, and debounce setting #1 is set to 1.
 *		  Array Value: offset for configuring debounce timer 2 (g4: 0x58, g7: 0x8)
 */

static const struct aspeed_gpio_copro_ops *copro_ops;
static void *copro_data;

static const struct aspeed_gpio_bank aspeed_gpio_banks[] = {
	{
		.val_regs = 0x0000,
		.rdata_reg = 0x00c0,
		.irq_regs = 0x0008,
		.debounce_regs = 0x0040,
		.tolerance_regs = 0x001c,
		.cmdsrc_regs = 0x0060,
	},
	{
		.val_regs = 0x0020,
		.rdata_reg = 0x00c4,
		.irq_regs = 0x0028,
		.debounce_regs = 0x0048,
		.tolerance_regs = 0x003c,
		.cmdsrc_regs = 0x0068,
	},
	{
		.val_regs = 0x0070,
		.rdata_reg = 0x00c8,
		.irq_regs = 0x0098,
		.debounce_regs = 0x00b0,
		.tolerance_regs = 0x00ac,
		.cmdsrc_regs = 0x0090,
	},
	{
		.val_regs = 0x0078,
		.rdata_reg = 0x00cc,
		.irq_regs = 0x00e8,
		.debounce_regs = 0x0100,
		.tolerance_regs = 0x00fc,
		.cmdsrc_regs = 0x00e0,
	},
	{
		.val_regs = 0x0080,
		.rdata_reg = 0x00d0,
		.irq_regs = 0x0118,
		.debounce_regs = 0x0130,
		.tolerance_regs = 0x012c,
		.cmdsrc_regs = 0x0110,
	},
	{
		.val_regs = 0x0088,
		.rdata_reg = 0x00d4,
		.irq_regs = 0x0148,
		.debounce_regs = 0x0160,
		.tolerance_regs = 0x015c,
		.cmdsrc_regs = 0x0140,
	},
	{
		.val_regs = 0x01E0,
		.rdata_reg = 0x00d8,
		.irq_regs = 0x0178,
		.debounce_regs = 0x0190,
		.tolerance_regs = 0x018c,
		.cmdsrc_regs = 0x0170,
	},
	{
		.val_regs = 0x01e8,
		.rdata_reg = 0x00dc,
		.irq_regs = 0x01a8,
		.debounce_regs = 0x01c0,
		.tolerance_regs = 0x01bc,
		.cmdsrc_regs = 0x01a0,
	},
};

enum aspeed_gpio_reg {
	reg_val,
	reg_rdata,
	reg_dir,
	reg_irq_enable,
	reg_irq_type0,
	reg_irq_type1,
	reg_irq_type2,
	reg_irq_status,
	reg_debounce_sel1,
	reg_debounce_sel2,
	reg_tolerance,
	reg_cmdsrc0,
	reg_cmdsrc1,
};

struct aspeed_gpio_llops {
	void (*reg_bit_set)(struct aspeed_gpio *gpio, unsigned int offset,
			    const enum aspeed_gpio_reg reg, bool val);
	bool (*reg_bit_get)(struct aspeed_gpio *gpio, unsigned int offset,
			    const enum aspeed_gpio_reg reg);
	int (*reg_bank_get)(struct aspeed_gpio *gpio, unsigned int offset,
			    const enum aspeed_gpio_reg reg);
	void (*privilege_ctrl)(struct aspeed_gpio *gpio, unsigned int offset, int owner);
	void (*privilege_init)(struct aspeed_gpio *gpio);
	bool (*copro_request)(struct aspeed_gpio *gpio, unsigned int offset);
	void (*copro_release)(struct aspeed_gpio *gpio, unsigned int offset);
};

#define GPIO_VAL_VALUE	0x00
#define GPIO_VAL_DIR	0x04

#define GPIO_IRQ_ENABLE	0x00
#define GPIO_IRQ_TYPE0	0x04
#define GPIO_IRQ_TYPE1	0x08
#define GPIO_IRQ_TYPE2	0x0c
#define GPIO_IRQ_STATUS	0x10

#define GPIO_DEBOUNCE_SEL1 0x00
#define GPIO_DEBOUNCE_SEL2 0x04

#define GPIO_CMDSRC_0	0x00
#define GPIO_CMDSRC_1	0x04
#define  GPIO_CMDSRC_ARM		0
#define  GPIO_CMDSRC_LPC		1
#define  GPIO_CMDSRC_COLDFIRE		2
#define  GPIO_CMDSRC_RESERVED		3

/* This will be resolved at compile time */
static void __iomem *aspeed_gpio_g4_bank_reg(struct aspeed_gpio *gpio,
					     const struct aspeed_gpio_bank *bank,
					     const enum aspeed_gpio_reg reg)
{
	switch (reg) {
	case reg_val:
		return gpio->base + bank->val_regs + GPIO_VAL_VALUE;
	case reg_rdata:
		return gpio->base + bank->rdata_reg;
	case reg_dir:
		return gpio->base + bank->val_regs + GPIO_VAL_DIR;
	case reg_irq_enable:
		return gpio->base + bank->irq_regs + GPIO_IRQ_ENABLE;
	case reg_irq_type0:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE0;
	case reg_irq_type1:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE1;
	case reg_irq_type2:
		return gpio->base + bank->irq_regs + GPIO_IRQ_TYPE2;
	case reg_irq_status:
		return gpio->base + bank->irq_regs + GPIO_IRQ_STATUS;
	case reg_debounce_sel1:
		return gpio->base + bank->debounce_regs + GPIO_DEBOUNCE_SEL1;
	case reg_debounce_sel2:
		return gpio->base + bank->debounce_regs + GPIO_DEBOUNCE_SEL2;
	case reg_tolerance:
		return gpio->base + bank->tolerance_regs;
	case reg_cmdsrc0:
		return gpio->base + bank->cmdsrc_regs + GPIO_CMDSRC_0;
	case reg_cmdsrc1:
		return gpio->base + bank->cmdsrc_regs + GPIO_CMDSRC_1;
	}
	BUG();
}

static u32 aspeed_gpio_g7_reg_mask(const enum aspeed_gpio_reg reg)
{
	switch (reg) {
	case reg_val:
		return GPIO_G7_CTRL_OUT_DATA;
	case reg_dir:
		return GPIO_G7_CTRL_DIR;
	case reg_irq_enable:
		return GPIO_G7_CTRL_IRQ_EN;
	case reg_irq_type0:
		return GPIO_G7_CTRL_IRQ_TYPE0;
	case reg_irq_type1:
		return GPIO_G7_CTRL_IRQ_TYPE1;
	case reg_irq_type2:
		return GPIO_G7_CTRL_IRQ_TYPE2;
	case reg_tolerance:
		return GPIO_G7_CTRL_RST_TOLERANCE;
	case reg_debounce_sel1:
		return GPIO_G7_CTRL_DEBOUNCE_SEL1;
	case reg_debounce_sel2:
		return GPIO_G7_CTRL_DEBOUNCE_SEL2;
	case reg_rdata:
		return GPIO_G7_CTRL_OUT_DATA;
	case reg_irq_status:
		return GPIO_G7_CTRL_IRQ_STS;
	case reg_cmdsrc0:
	case reg_cmdsrc1:
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

#define GPIO_BANK(x)	((x) >> 5)
#define GPIO_OFFSET(x)	((x) & 0x1f)
#define GPIO_BIT(x)	BIT(GPIO_OFFSET(x))

static const struct aspeed_gpio_bank *to_bank(unsigned int offset)
{
	unsigned int bank = GPIO_BANK(offset);

	WARN_ON(bank >= ARRAY_SIZE(aspeed_gpio_banks));
	return &aspeed_gpio_banks[bank];
}

static inline bool is_bank_props_sentinel(const struct aspeed_bank_props *props)
{
	return !(props->input || props->output);
}

static inline const struct aspeed_bank_props *find_bank_props(
		struct aspeed_gpio *gpio, unsigned int offset)
{
	const struct aspeed_bank_props *props = gpio->config->props;

	while (!is_bank_props_sentinel(props)) {
		if (props->bank == GPIO_BANK(offset))
			return props;
		props++;
	}

	return NULL;
}

static inline bool have_gpio(struct aspeed_gpio *gpio, unsigned int offset)
{
	const struct aspeed_bank_props *props = find_bank_props(gpio, offset);

	if (offset >= gpio->chip.ngpio)
		return false;

	return (!props || ((props->input | props->output) & GPIO_BIT(offset)));
}

static inline bool have_input(struct aspeed_gpio *gpio, unsigned int offset)
{
	const struct aspeed_bank_props *props = find_bank_props(gpio, offset);

	return !props || (props->input & GPIO_BIT(offset));
}

#define have_irq(g, o) have_input((g), (o))
#define have_debounce(g, o) have_input((g), (o))

static inline bool have_output(struct aspeed_gpio *gpio, unsigned int offset)
{
	const struct aspeed_bank_props *props = find_bank_props(gpio, offset);

	return !props || (props->output & GPIO_BIT(offset));
}

static void aspeed_gpio_change_cmd_source(struct aspeed_gpio *gpio, unsigned int offset, int cmdsrc)
{
	if (gpio->config->llops->privilege_ctrl)
		gpio->config->llops->privilege_ctrl(gpio, offset, cmdsrc);
}

static bool aspeed_gpio_copro_request(struct aspeed_gpio *gpio,
				      unsigned int offset)
{
	if (gpio->config->llops->copro_request)
		return gpio->config->llops->copro_request(gpio, offset);

	return false;
}

static void aspeed_gpio_copro_release(struct aspeed_gpio *gpio,
				      unsigned int offset)
{
	if (gpio->config->llops->copro_release)
		gpio->config->llops->copro_release(gpio, offset);
}

static bool aspeed_gpio_support_copro(struct aspeed_gpio *gpio)
{
	return gpio->config->llops->copro_request && gpio->config->llops->copro_release &&
	       gpio->config->llops->privilege_ctrl && gpio->config->llops->privilege_init;
}

static int aspeed_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);

	return gpio->config->llops->reg_bit_get(gpio, offset, reg_val);
}

static void __aspeed_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);

	gpio->config->llops->reg_bit_set(gpio, offset, reg_val, val);
	/* Flush write */
	gpio->config->llops->reg_bit_get(gpio, offset, reg_val);
}

static int aspeed_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	bool copro = false;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);

	__aspeed_gpio_set(gc, offset, val);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	return 0;
}

static int aspeed_gpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	bool copro = false;

	if (!have_input(gpio, offset))
		return -ENOTSUPP;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);
	gpio->config->llops->reg_bit_set(gpio, offset, reg_dir, 0);
	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	return 0;
}

static int aspeed_gpio_dir_out(struct gpio_chip *gc,
			       unsigned int offset, int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	bool copro = false;

	if (!have_output(gpio, offset))
		return -ENOTSUPP;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);
	__aspeed_gpio_set(gc, offset, val);
	gpio->config->llops->reg_bit_set(gpio, offset, reg_dir, 1);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	return 0;
}

static int aspeed_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	u32 val;

	if (!have_input(gpio, offset))
		return GPIO_LINE_DIRECTION_OUT;

	if (!have_output(gpio, offset))
		return GPIO_LINE_DIRECTION_IN;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	val = gpio->config->llops->reg_bit_get(gpio, offset, reg_dir);

	return val ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static inline int irqd_to_aspeed_gpio_data(struct irq_data *d,
					   struct aspeed_gpio **gpio,
					   int *offset)
{
	struct aspeed_gpio *internal;

	*offset = irqd_to_hwirq(d);

	internal = irq_data_get_irq_chip_data(d);

	/* This might be a bit of a questionable place to check */
	if (!have_irq(internal, *offset))
		return -ENOTSUPP;

	*gpio = internal;

	return 0;
}

static void aspeed_gpio_irq_ack(struct irq_data *d)
{
	struct aspeed_gpio *gpio;
	int rc, offset;
	bool copro = false;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &offset);
	if (rc)
		return;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);

	gpio->config->llops->reg_bit_set(gpio, offset, reg_irq_status, 1);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
}

static void aspeed_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	struct aspeed_gpio *gpio;
	int rc, offset;
	bool copro = false;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &offset);
	if (rc)
		return;

	/* Unmasking the IRQ */
	if (set)
		gpiochip_enable_irq(&gpio->chip, irqd_to_hwirq(d));

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);

	gpio->config->llops->reg_bit_set(gpio, offset, reg_irq_enable, set);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	/* Masking the IRQ */
	if (!set)
		gpiochip_disable_irq(&gpio->chip, irqd_to_hwirq(d));
}

static void aspeed_gpio_irq_mask(struct irq_data *d)
{
	aspeed_gpio_irq_set_mask(d, false);
}

static void aspeed_gpio_irq_unmask(struct irq_data *d)
{
	aspeed_gpio_irq_set_mask(d, true);
}

static int aspeed_gpio_set_type(struct irq_data *d, unsigned int type)
{
	u32 type0 = 0;
	u32 type1 = 0;
	u32 type2 = 0;
	irq_flow_handler_t handler;
	struct aspeed_gpio *gpio;
	int rc, offset;
	bool copro = false;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &offset);
	if (rc)
		return -EINVAL;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		type2 = 1;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		type0 = 1;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type0 = 1;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		type1 = 1;
		handler = handle_level_irq;
		break;
	default:
		return -EINVAL;
	}

	scoped_guard(raw_spinlock_irqsave, &gpio->lock) {
		copro = aspeed_gpio_copro_request(gpio, offset);

		gpio->config->llops->reg_bit_set(gpio, offset, reg_irq_type0,
						 type0);
		gpio->config->llops->reg_bit_set(gpio, offset, reg_irq_type1,
						 type1);
		gpio->config->llops->reg_bit_set(gpio, offset, reg_irq_type2,
						 type2);

		if (copro)
			aspeed_gpio_copro_release(gpio, offset);
	}

	irq_set_handler_locked(d, handler);

	return 0;
}

static void aspeed_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	unsigned int i, p, banks;
	unsigned long reg;
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);

	chained_irq_enter(ic, desc);

	banks = DIV_ROUND_UP(gpio->chip.ngpio, 32);
	for (i = 0; i < banks; i++) {
		reg = gpio->config->llops->reg_bank_get(gpio, i * 32, reg_irq_status);

		for_each_set_bit(p, &reg, 32)
			generic_handle_domain_irq(gc->irq.domain, i * 32 + p);
	}

	chained_irq_exit(ic, desc);
}

static void aspeed_init_irq_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_bank_props *props = gpio->config->props;

	while (!is_bank_props_sentinel(props)) {
		unsigned int offset;
		const unsigned long int input = props->input;

		/* Pretty crummy approach, but similar to GPIO core */
		for_each_clear_bit(offset, &input, 32) {
			unsigned int i = props->bank * 32 + offset;

			if (i >= gpio->chip.ngpio)
				break;

			clear_bit(i, valid_mask);
		}

		props++;
	}
}

static int aspeed_gpio_reset_tolerance(struct gpio_chip *chip,
					unsigned int offset, bool enable)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	bool copro = false;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	copro = aspeed_gpio_copro_request(gpio, offset);

	gpio->config->llops->reg_bit_set(gpio, offset, reg_tolerance, enable);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	return 0;
}

static int aspeed_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	if (!have_gpio(gpiochip_get_data(chip), offset))
		return -ENODEV;

	return pinctrl_gpio_request(chip, offset);
}

static void aspeed_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip, offset);
}

static int usecs_to_cycles(struct aspeed_gpio *gpio, unsigned long usecs,
		u32 *cycles)
{
	u64 rate;
	u64 n;
	u32 r;

	rate = clk_get_rate(gpio->clk);
	if (!rate)
		return -ENOTSUPP;

	n = rate * usecs;
	r = do_div(n, 1000000);

	if (n >= U32_MAX)
		return -ERANGE;

	/* At least as long as the requested time */
	*cycles = n + (!!r);

	return 0;
}

/* Call under gpio->lock */
static int register_allocated_timer(struct aspeed_gpio *gpio,
		unsigned int offset, unsigned int timer)
{
	if (WARN(gpio->offset_timer[offset] != 0,
				"Offset %d already allocated timer %d\n",
				offset, gpio->offset_timer[offset]))
		return -EINVAL;

	if (WARN(gpio->timer_users[timer] == UINT_MAX,
				"Timer user count would overflow\n"))
		return -EPERM;

	gpio->offset_timer[offset] = timer;
	gpio->timer_users[timer]++;

	return 0;
}

/* Call under gpio->lock */
static int unregister_allocated_timer(struct aspeed_gpio *gpio,
		unsigned int offset)
{
	if (WARN(gpio->offset_timer[offset] == 0,
				"No timer allocated to offset %d\n", offset))
		return -EINVAL;

	if (WARN(gpio->timer_users[gpio->offset_timer[offset]] == 0,
				"No users recorded for timer %d\n",
				gpio->offset_timer[offset]))
		return -EINVAL;

	gpio->timer_users[gpio->offset_timer[offset]]--;
	gpio->offset_timer[offset] = 0;

	return 0;
}

/* Call under gpio->lock */
static inline bool timer_allocation_registered(struct aspeed_gpio *gpio,
		unsigned int offset)
{
	return gpio->offset_timer[offset] > 0;
}

/* Call under gpio->lock */
static void configure_timer(struct aspeed_gpio *gpio, unsigned int offset,
		unsigned int timer)
{
	/* Note: Debounce timer isn't under control of the command
	 * source registers, so no need to sync with the coprocessor
	 */
	gpio->config->llops->reg_bit_set(gpio, offset, reg_debounce_sel1, !!(timer & BIT(1)));
	gpio->config->llops->reg_bit_set(gpio, offset, reg_debounce_sel2, !!(timer & BIT(0)));
}

static int enable_debounce(struct gpio_chip *chip, unsigned int offset,
				    unsigned long usecs)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	u32 requested_cycles;
	int rc;
	int i;

	if (!gpio->clk)
		return -EINVAL;

	rc = usecs_to_cycles(gpio, usecs, &requested_cycles);
	if (rc < 0) {
		dev_warn(chip->parent, "Failed to convert %luus to cycles at %luHz: %d\n",
				usecs, clk_get_rate(gpio->clk), rc);
		return rc;
	}

	guard(raw_spinlock_irqsave)(&gpio->lock);

	if (timer_allocation_registered(gpio, offset)) {
		rc = unregister_allocated_timer(gpio, offset);
		if (rc < 0)
			return rc;
	}

	/* Try to find a timer already configured for the debounce period */
	for (i = 1; i < gpio->config->debounce_timers_num; i++) {
		u32 cycles;

		cycles = ioread32(gpio->base + gpio->config->debounce_timers_array[i]);
		if (requested_cycles == cycles)
			break;
	}

	if (i == gpio->config->debounce_timers_num) {
		int j;

		/*
		 * As there are no timers configured for the requested debounce
		 * period, find an unused timer instead
		 */
		for (j = 1; j < ARRAY_SIZE(gpio->timer_users); j++) {
			if (gpio->timer_users[j] == 0)
				break;
		}

		if (j == ARRAY_SIZE(gpio->timer_users)) {
			dev_warn(chip->parent,
				 "Debounce timers exhausted, cannot debounce for period %luus\n",
				 usecs);

			rc = -EPERM;

			/*
			 * We already adjusted the accounting to remove @offset
			 * as a user of its previous timer, so also configure
			 * the hardware so @offset has timers disabled for
			 * consistency.
			 */
			configure_timer(gpio, offset, 0);
			return rc;
		}

		i = j;

		iowrite32(requested_cycles, gpio->base + gpio->config->debounce_timers_array[i]);
	}

	if (WARN(i == 0, "Cannot register index of disabled timer\n"))
		return -EINVAL;

	register_allocated_timer(gpio, offset, i);
	configure_timer(gpio, offset, i);

	return rc;
}

static int disable_debounce(struct gpio_chip *chip, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	int rc;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	rc = unregister_allocated_timer(gpio, offset);
	if (!rc)
		configure_timer(gpio, offset, 0);

	return rc;
}

static int set_debounce(struct gpio_chip *chip, unsigned int offset,
				    unsigned long usecs)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);

	if (!have_debounce(gpio, offset))
		return -ENOTSUPP;

	if (usecs)
		return enable_debounce(chip, offset, usecs);

	return disable_debounce(chip, offset);
}

static int aspeed_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				  unsigned long config)
{
	unsigned long param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	if (param == PIN_CONFIG_INPUT_DEBOUNCE)
		return set_debounce(chip, offset, arg);
	else if (param == PIN_CONFIG_BIAS_DISABLE ||
			param == PIN_CONFIG_BIAS_PULL_DOWN ||
			param == PIN_CONFIG_DRIVE_STRENGTH)
		return pinctrl_gpio_set_config(chip, offset, config);
	else if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN ||
			param == PIN_CONFIG_DRIVE_OPEN_SOURCE)
		/* Return -ENOTSUPP to trigger emulation, as per datasheet */
		return -ENOTSUPP;
	else if (param == PIN_CONFIG_PERSIST_STATE)
		return aspeed_gpio_reset_tolerance(chip, offset, arg);

	return -ENOTSUPP;
}

/**
 * aspeed_gpio_copro_set_ops - Sets the callbacks used for handshaking with
 *                             the coprocessor for shared GPIO banks
 * @ops: The callbacks
 * @data: Pointer passed back to the callbacks
 */
int aspeed_gpio_copro_set_ops(const struct aspeed_gpio_copro_ops *ops, void *data)
{
	copro_data = data;
	copro_ops = ops;

	return 0;
}
EXPORT_SYMBOL_GPL(aspeed_gpio_copro_set_ops);

/**
 * aspeed_gpio_copro_grab_gpio - Mark a GPIO used by the coprocessor. The entire
 *                               bank gets marked and any access from the ARM will
 *                               result in handshaking via callbacks.
 * @desc: The GPIO to be marked
 * @vreg_offset: If non-NULL, returns the value register offset in the GPIO space
 * @dreg_offset: If non-NULL, returns the data latch register offset in the GPIO space
 * @bit: If non-NULL, returns the bit number of the GPIO in the registers
 */
int aspeed_gpio_copro_grab_gpio(struct gpio_desc *desc,
				u16 *vreg_offset, u16 *dreg_offset, u8 *bit)
{
	struct gpio_chip *chip = gpiod_to_chip(desc);
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	int rc = 0, bindex, offset = gpio_chip_hwgpio(desc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);

	if (!aspeed_gpio_support_copro(gpio))
		return -EOPNOTSUPP;

	if (!gpio->cf_copro_bankmap)
		gpio->cf_copro_bankmap = kzalloc(gpio->chip.ngpio >> 3, GFP_KERNEL);
	if (!gpio->cf_copro_bankmap)
		return -ENOMEM;
	if (offset < 0 || offset > gpio->chip.ngpio)
		return -EINVAL;
	bindex = offset >> 3;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	/* Sanity check, this shouldn't happen */
	if (gpio->cf_copro_bankmap[bindex] == 0xff)
		return -EIO;

	gpio->cf_copro_bankmap[bindex]++;

	/* Switch command source */
	if (gpio->cf_copro_bankmap[bindex] == 1)
		aspeed_gpio_change_cmd_source(gpio, offset,
					      GPIO_CMDSRC_COLDFIRE);

	if (vreg_offset)
		*vreg_offset = bank->val_regs;
	if (dreg_offset)
		*dreg_offset = bank->rdata_reg;
	if (bit)
		*bit = GPIO_OFFSET(offset);
	return rc;
}
EXPORT_SYMBOL_GPL(aspeed_gpio_copro_grab_gpio);

/**
 * aspeed_gpio_copro_release_gpio - Unmark a GPIO used by the coprocessor.
 * @desc: The GPIO to be marked
 */
int aspeed_gpio_copro_release_gpio(struct gpio_desc *desc)
{
	struct gpio_chip *chip = gpiod_to_chip(desc);
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	int rc = 0, bindex, offset = gpio_chip_hwgpio(desc);

	if (!aspeed_gpio_support_copro(gpio))
		return -EOPNOTSUPP;

	if (!gpio->cf_copro_bankmap)
		return -ENXIO;

	if (offset < 0 || offset > gpio->chip.ngpio)
		return -EINVAL;
	bindex = offset >> 3;

	guard(raw_spinlock_irqsave)(&gpio->lock);

	/* Sanity check, this shouldn't happen */
	if (gpio->cf_copro_bankmap[bindex] == 0)
		return -EIO;

	gpio->cf_copro_bankmap[bindex]--;

	/* Switch command source */
	if (gpio->cf_copro_bankmap[bindex] == 0)
		aspeed_gpio_change_cmd_source(gpio, offset,
					      GPIO_CMDSRC_ARM);

	return rc;
}
EXPORT_SYMBOL_GPL(aspeed_gpio_copro_release_gpio);

static void aspeed_gpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct aspeed_gpio *gpio;
	int rc, offset;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &offset);
	if (rc)
		return;

	seq_puts(p, dev_name(gpio->dev));
}

static const struct irq_chip aspeed_gpio_irq_chip = {
	.irq_ack = aspeed_gpio_irq_ack,
	.irq_mask = aspeed_gpio_irq_mask,
	.irq_unmask = aspeed_gpio_irq_unmask,
	.irq_set_type = aspeed_gpio_set_type,
	.irq_print_chip = aspeed_gpio_irq_print_chip,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void aspeed_g4_reg_bit_set(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg, bool val)
{
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr = aspeed_gpio_g4_bank_reg(gpio, bank, reg);
	u32 temp;

	if (reg == reg_val)
		temp = gpio->dcache[GPIO_BANK(offset)];
	else
		temp = ioread32(addr);

	if (val)
		temp |= GPIO_BIT(offset);
	else
		temp &= ~GPIO_BIT(offset);

	if (reg == reg_val)
		gpio->dcache[GPIO_BANK(offset)] = temp;
	iowrite32(temp, addr);
}

static bool aspeed_g4_reg_bit_get(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg)
{
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr = aspeed_gpio_g4_bank_reg(gpio, bank, reg);

	return !!(ioread32(addr) & GPIO_BIT(offset));
}

static int aspeed_g4_reg_bank_get(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg)
{
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr = aspeed_gpio_g4_bank_reg(gpio, bank, reg);

	if (reg == reg_rdata || reg == reg_irq_status)
		return ioread32(addr);
	else
		return -EOPNOTSUPP;
}

static void aspeed_g4_privilege_ctrl(struct aspeed_gpio *gpio, unsigned int offset, int cmdsrc)
{
	/*
	 * The command source register is only valid in bits 0, 8, 16, and 24, so we use
	 * (offset & ~(0x7)) to ensure that reg_bits_set always targets a valid bit.
	 */
	/* Source 1 first to avoid illegal 11 combination */
	aspeed_g4_reg_bit_set(gpio, offset & ~(0x7), reg_cmdsrc1, !!(cmdsrc & BIT(1)));
	/* Then Source 0 */
	aspeed_g4_reg_bit_set(gpio, offset & ~(0x7), reg_cmdsrc0, !!(cmdsrc & BIT(0)));
}

static void aspeed_g4_privilege_init(struct aspeed_gpio *gpio)
{
	u32 i;

	/* Switch all command sources to the ARM by default */
	for (i = 0; i < DIV_ROUND_UP(gpio->chip.ngpio, 32); i++) {
		aspeed_g4_privilege_ctrl(gpio, (i << 5) + 0, GPIO_CMDSRC_ARM);
		aspeed_g4_privilege_ctrl(gpio, (i << 5) + 8, GPIO_CMDSRC_ARM);
		aspeed_g4_privilege_ctrl(gpio, (i << 5) + 16, GPIO_CMDSRC_ARM);
		aspeed_g4_privilege_ctrl(gpio, (i << 5) + 24, GPIO_CMDSRC_ARM);
	}
}

static bool aspeed_g4_copro_request(struct aspeed_gpio *gpio, unsigned int offset)
{
	if (!copro_ops || !gpio->cf_copro_bankmap)
		return false;
	if (!gpio->cf_copro_bankmap[offset >> 3])
		return false;
	if (!copro_ops->request_access)
		return false;

	/* Pause the coprocessor */
	copro_ops->request_access(copro_data);

	/* Change command source back to ARM */
	aspeed_g4_privilege_ctrl(gpio, offset, GPIO_CMDSRC_ARM);

	/* Update cache */
	gpio->dcache[GPIO_BANK(offset)] = aspeed_g4_reg_bank_get(gpio, offset, reg_rdata);

	return true;
}

static void aspeed_g4_copro_release(struct aspeed_gpio *gpio, unsigned int offset)
{
	if (!copro_ops || !gpio->cf_copro_bankmap)
		return;
	if (!gpio->cf_copro_bankmap[offset >> 3])
		return;
	if (!copro_ops->release_access)
		return;

	/* Change command source back to ColdFire */
	aspeed_g4_privilege_ctrl(gpio, offset, GPIO_CMDSRC_COLDFIRE);

	/* Restart the coprocessor */
	copro_ops->release_access(copro_data);
}

static const struct aspeed_gpio_llops aspeed_g4_llops = {
	.reg_bit_set = aspeed_g4_reg_bit_set,
	.reg_bit_get = aspeed_g4_reg_bit_get,
	.reg_bank_get = aspeed_g4_reg_bank_get,
	.privilege_ctrl = aspeed_g4_privilege_ctrl,
	.privilege_init = aspeed_g4_privilege_init,
	.copro_request = aspeed_g4_copro_request,
	.copro_release = aspeed_g4_copro_release,
};

static void aspeed_g7_reg_bit_set(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg, bool val)
{
	u32 mask = aspeed_gpio_g7_reg_mask(reg);
	void __iomem *addr = gpio->base + GPIO_G7_CTRL_REG_OFFSET(offset);
	u32 write_val;

	if (mask) {
		write_val = (ioread32(addr) & ~(mask)) | field_prep(mask, val);
		iowrite32(write_val, addr);
	}
}

static bool aspeed_g7_reg_bit_get(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg)
{
	u32 mask = aspeed_gpio_g7_reg_mask(reg);
	void __iomem *addr;

	addr = gpio->base + GPIO_G7_CTRL_REG_OFFSET(offset);
	if (reg == reg_val)
		mask = GPIO_G7_CTRL_IN_DATA;

	if (mask)
		return field_get(mask, ioread32(addr));
	else
		return 0;
}

static int aspeed_g7_reg_bank_get(struct aspeed_gpio *gpio, unsigned int offset,
				  const enum aspeed_gpio_reg reg)
{
	void __iomem *addr;

	if (reg == reg_irq_status) {
		addr = gpio->base + GPIO_G7_IRQ_STS_OFFSET(offset >> 5);
		return ioread32(addr);
	} else {
		return -EOPNOTSUPP;
	}
}

static const struct aspeed_gpio_llops aspeed_g7_llops = {
	.reg_bit_set = aspeed_g7_reg_bit_set,
	.reg_bit_get = aspeed_g7_reg_bit_get,
	.reg_bank_get = aspeed_g7_reg_bank_get,
	.privilege_ctrl = NULL,
	.privilege_init = NULL,
	.copro_request = NULL,
	.copro_release = NULL,
};

/*
 * Any banks not specified in a struct aspeed_bank_props array are assumed to
 * have the properties:
 *
 *     { .input = 0xffffffff, .output = 0xffffffff }
 */

static const struct aspeed_bank_props ast2400_bank_props[] = {
	/*     input	  output   */
	{ 5, 0xffffffff, 0x0000ffff }, /* U/V/W/X */
	{ 6, 0x0000000f, 0x0fffff0f }, /* Y/Z/AA/AB, two 4-GPIO holes */
	{ },
};

static const struct aspeed_gpio_config ast2400_config =
	/* 220 for simplicity, really 216 with two 4-GPIO holes, four at end */
	{
		.nr_gpios = 220,
		.props = ast2400_bank_props,
		.llops = &aspeed_g4_llops,
		.debounce_timers_array = debounce_timers,
		.debounce_timers_num = ARRAY_SIZE(debounce_timers),
		.require_dcache = true,
	};

static const struct aspeed_bank_props ast2500_bank_props[] = {
	/*     input	  output   */
	{ 5, 0xffffffff, 0x0000ffff }, /* U/V/W/X */
	{ 6, 0x0fffffff, 0x0fffffff }, /* Y/Z/AA/AB, 4-GPIO hole */
	{ 7, 0x000000ff, 0x000000ff }, /* AC */
	{ },
};

static const struct aspeed_gpio_config ast2500_config =
	/* 232 for simplicity, actual number is 228 (4-GPIO hole in GPIOAB) */
	{
		.nr_gpios = 232,
		.props = ast2500_bank_props,
		.llops = &aspeed_g4_llops,
		.debounce_timers_array = debounce_timers,
		.debounce_timers_num = ARRAY_SIZE(debounce_timers),
		.require_dcache = true,
	};

static const struct aspeed_bank_props ast2600_bank_props[] = {
	/*     input	  output   */
	{4, 0xffffffff,  0x00ffffff}, /* Q/R/S/T */
	{5, 0xffffffff,  0xffffff00}, /* U/V/W/X */
	{6, 0x0000ffff,  0x0000ffff}, /* Y/Z */
	{ },
};

static const struct aspeed_gpio_config ast2600_config =
	/*
	 * ast2600 has two controllers one with 208 GPIOs and one with 36 GPIOs.
	 * We expect ngpio being set in the device tree and this is a fallback
	 * option.
	 */
	{
		.nr_gpios = 208,
		.props = ast2600_bank_props,
		.llops = &aspeed_g4_llops,
		.debounce_timers_array = debounce_timers,
		.debounce_timers_num = ARRAY_SIZE(debounce_timers),
		.require_dcache = true,
	};

static const struct aspeed_bank_props ast2700_bank_props[] = {
	/*     input	  output   */
	{ 1, 0x0fffffff, 0x0fffffff }, /* E/F/G/H, 4-GPIO hole */
	{ 6, 0x00ffffff, 0x00ff0000 }, /* Y/Z/AA */
	{},
};

static const struct aspeed_gpio_config ast2700_config =
	/*
	 * ast2700 has two controllers one with 212 GPIOs and one with 16 GPIOs.
	 * 216 for simplicity, actual number is 212 (4-GPIO hole in GPIOH)
	 * We expect ngpio being set in the device tree and this is a fallback
	 * option.
	 */
	{
		.nr_gpios = 216,
		.props = ast2700_bank_props,
		.llops = &aspeed_g7_llops,
		.debounce_timers_array = g7_debounce_timers,
		.debounce_timers_num = ARRAY_SIZE(g7_debounce_timers),
		.require_dcache = false,
	};

static const struct of_device_id aspeed_gpio_of_table[] = {
	{ .compatible = "aspeed,ast2400-gpio", .data = &ast2400_config, },
	{ .compatible = "aspeed,ast2500-gpio", .data = &ast2500_config, },
	{ .compatible = "aspeed,ast2600-gpio", .data = &ast2600_config, },
	{ .compatible = "aspeed,ast2700-gpio", .data = &ast2700_config, },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_gpio_of_table);

static int aspeed_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *gpio_id;
	struct gpio_irq_chip *girq;
	struct aspeed_gpio *gpio;
	int rc, irq, i, banks, err;
	u32 ngpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	gpio->dev = &pdev->dev;

	raw_spin_lock_init(&gpio->lock);

	gpio_id = of_match_node(aspeed_gpio_of_table, pdev->dev.of_node);
	if (!gpio_id)
		return -EINVAL;

	gpio->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(gpio->clk)) {
		dev_warn(&pdev->dev,
				"Failed to get clock from devicetree, debouncing disabled\n");
		gpio->clk = NULL;
	}

	gpio->config = gpio_id->data;

	if (!gpio->config->llops->reg_bit_set || !gpio->config->llops->reg_bit_get ||
	    !gpio->config->llops->reg_bank_get)
		return -EINVAL;

	gpio->chip.parent = &pdev->dev;
	err = of_property_read_u32(pdev->dev.of_node, "ngpios", &ngpio);
	gpio->chip.ngpio = (u16) ngpio;
	if (err)
		gpio->chip.ngpio = gpio->config->nr_gpios;
	gpio->chip.direction_input = aspeed_gpio_dir_in;
	gpio->chip.direction_output = aspeed_gpio_dir_out;
	gpio->chip.get_direction = aspeed_gpio_get_direction;
	gpio->chip.request = aspeed_gpio_request;
	gpio->chip.free = aspeed_gpio_free;
	gpio->chip.get = aspeed_gpio_get;
	gpio->chip.set_rv = aspeed_gpio_set;
	gpio->chip.set_config = aspeed_gpio_set_config;
	gpio->chip.label = dev_name(&pdev->dev);
	gpio->chip.base = -1;

	if (gpio->config->require_dcache) {
		/* Allocate a cache of the output registers */
		banks = DIV_ROUND_UP(gpio->chip.ngpio, 32);
		gpio->dcache = devm_kcalloc(&pdev->dev, banks, sizeof(u32), GFP_KERNEL);
		if (!gpio->dcache)
			return -ENOMEM;
		/*
		 * Populate it with initial values read from the HW
		 */
		for (i = 0; i < banks; i++)
			gpio->dcache[i] =
				gpio->config->llops->reg_bank_get(gpio, (i << 5), reg_rdata);
	}

	if (gpio->config->llops->privilege_init)
		gpio->config->llops->privilege_init(gpio);

	/* Set up an irqchip */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	gpio->irq = irq;
	girq = &gpio->chip.irq;
	gpio_irq_chip_set_chip(girq, &aspeed_gpio_irq_chip);

	girq->parent_handler = aspeed_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1, sizeof(*girq->parents), GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = gpio->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->init_valid_mask = aspeed_init_irq_valid_mask;

	gpio->offset_timer =
		devm_kzalloc(&pdev->dev, gpio->chip.ngpio, GFP_KERNEL);
	if (!gpio->offset_timer)
		return -ENOMEM;

	rc = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (rc < 0)
		return rc;

	return 0;
}

static struct platform_driver aspeed_gpio_driver = {
	.probe = aspeed_gpio_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_gpio_of_table,
	},
};

module_platform_driver(aspeed_gpio_driver);

MODULE_DESCRIPTION("Aspeed GPIO Driver");
MODULE_LICENSE("GPL");
