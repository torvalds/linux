// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 */

#include <asm/div64.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/aspeed.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/string.h>

/*
 * These two headers aren't meant to be used by GPIO drivers. We need
 * them in order to access gpio_chip_hwgpio() which we need to implement
 * the aspeed specific API which allows the coprocessor to request
 * access to some GPIOs and to arbitrate between coprocessor and ARM.
 */
#include <linux/gpio/consumer.h>
#include "gpiolib.h"

struct aspeed_bank_props {
	unsigned int bank;
	u32 input;
	u32 output;
};

struct aspeed_gpio_config {
	unsigned int nr_gpios;
	const struct aspeed_bank_props *props;
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
	struct irq_chip irqc;
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
	const char	names[4][3];
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
		.names = { "A", "B", "C", "D" },
	},
	{
		.val_regs = 0x0020,
		.rdata_reg = 0x00c4,
		.irq_regs = 0x0028,
		.debounce_regs = 0x0048,
		.tolerance_regs = 0x003c,
		.cmdsrc_regs = 0x0068,
		.names = { "E", "F", "G", "H" },
	},
	{
		.val_regs = 0x0070,
		.rdata_reg = 0x00c8,
		.irq_regs = 0x0098,
		.debounce_regs = 0x00b0,
		.tolerance_regs = 0x00ac,
		.cmdsrc_regs = 0x0090,
		.names = { "I", "J", "K", "L" },
	},
	{
		.val_regs = 0x0078,
		.rdata_reg = 0x00cc,
		.irq_regs = 0x00e8,
		.debounce_regs = 0x0100,
		.tolerance_regs = 0x00fc,
		.cmdsrc_regs = 0x00e0,
		.names = { "M", "N", "O", "P" },
	},
	{
		.val_regs = 0x0080,
		.rdata_reg = 0x00d0,
		.irq_regs = 0x0118,
		.debounce_regs = 0x0130,
		.tolerance_regs = 0x012c,
		.cmdsrc_regs = 0x0110,
		.names = { "Q", "R", "S", "T" },
	},
	{
		.val_regs = 0x0088,
		.rdata_reg = 0x00d4,
		.irq_regs = 0x0148,
		.debounce_regs = 0x0160,
		.tolerance_regs = 0x015c,
		.cmdsrc_regs = 0x0140,
		.names = { "U", "V", "W", "X" },
	},
	{
		.val_regs = 0x01E0,
		.rdata_reg = 0x00d8,
		.irq_regs = 0x0178,
		.debounce_regs = 0x0190,
		.tolerance_regs = 0x018c,
		.cmdsrc_regs = 0x0170,
		.names = { "Y", "Z", "AA", "AB" },
	},
	{
		.val_regs = 0x01e8,
		.rdata_reg = 0x00dc,
		.irq_regs = 0x01a8,
		.debounce_regs = 0x01c0,
		.tolerance_regs = 0x01bc,
		.cmdsrc_regs = 0x01a0,
		.names = { "AC", "", "", "" },
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
static inline void __iomem *bank_reg(struct aspeed_gpio *gpio,
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

#define GPIO_BANK(x)	((x) >> 5)
#define GPIO_OFFSET(x)	((x) & 0x1f)
#define GPIO_BIT(x)	BIT(GPIO_OFFSET(x))

#define _GPIO_SET_DEBOUNCE(t, o, i) ((!!((t) & BIT(i))) << GPIO_OFFSET(o))
#define GPIO_SET_DEBOUNCE1(t, o) _GPIO_SET_DEBOUNCE(t, o, 1)
#define GPIO_SET_DEBOUNCE2(t, o) _GPIO_SET_DEBOUNCE(t, o, 0)

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
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	unsigned int group = GPIO_OFFSET(offset) / 8;

	return bank->names[group][0] != '\0' &&
		(!props || ((props->input | props->output) & GPIO_BIT(offset)));
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

static void aspeed_gpio_change_cmd_source(struct aspeed_gpio *gpio,
					  const struct aspeed_gpio_bank *bank,
					  int bindex, int cmdsrc)
{
	void __iomem *c0 = bank_reg(gpio, bank, reg_cmdsrc0);
	void __iomem *c1 = bank_reg(gpio, bank, reg_cmdsrc1);
	u32 bit, reg;

	/*
	 * Each register controls 4 banks, so take the bottom 2
	 * bits of the bank index, and use them to select the
	 * right control bit (0, 8, 16 or 24).
	 */
	bit = BIT((bindex & 3) << 3);

	/* Source 1 first to avoid illegal 11 combination */
	reg = ioread32(c1);
	if (cmdsrc & 2)
		reg |= bit;
	else
		reg &= ~bit;
	iowrite32(reg, c1);

	/* Then Source 0 */
	reg = ioread32(c0);
	if (cmdsrc & 1)
		reg |= bit;
	else
		reg &= ~bit;
	iowrite32(reg, c0);
}

static bool aspeed_gpio_copro_request(struct aspeed_gpio *gpio,
				      unsigned int offset)
{
	const struct aspeed_gpio_bank *bank = to_bank(offset);

	if (!copro_ops || !gpio->cf_copro_bankmap)
		return false;
	if (!gpio->cf_copro_bankmap[offset >> 3])
		return false;
	if (!copro_ops->request_access)
		return false;

	/* Pause the coprocessor */
	copro_ops->request_access(copro_data);

	/* Change command source back to ARM */
	aspeed_gpio_change_cmd_source(gpio, bank, offset >> 3, GPIO_CMDSRC_ARM);

	/* Update cache */
	gpio->dcache[GPIO_BANK(offset)] = ioread32(bank_reg(gpio, bank, reg_rdata));

	return true;
}

static void aspeed_gpio_copro_release(struct aspeed_gpio *gpio,
				      unsigned int offset)
{
	const struct aspeed_gpio_bank *bank = to_bank(offset);

	if (!copro_ops || !gpio->cf_copro_bankmap)
		return;
	if (!gpio->cf_copro_bankmap[offset >> 3])
		return;
	if (!copro_ops->release_access)
		return;

	/* Change command source back to ColdFire */
	aspeed_gpio_change_cmd_source(gpio, bank, offset >> 3,
				      GPIO_CMDSRC_COLDFIRE);

	/* Restart the coprocessor */
	copro_ops->release_access(copro_data);
}

static int aspeed_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);

	return !!(ioread32(bank_reg(gpio, bank, reg_val)) & GPIO_BIT(offset));
}

static void __aspeed_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr;
	u32 reg;

	addr = bank_reg(gpio, bank, reg_val);
	reg = gpio->dcache[GPIO_BANK(offset)];

	if (val)
		reg |= GPIO_BIT(offset);
	else
		reg &= ~GPIO_BIT(offset);
	gpio->dcache[GPIO_BANK(offset)] = reg;

	iowrite32(reg, addr);
	/* Flush write */
	ioread32(addr);
}

static void aspeed_gpio_set(struct gpio_chip *gc, unsigned int offset,
			    int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;
	bool copro;

	raw_spin_lock_irqsave(&gpio->lock, flags);
	copro = aspeed_gpio_copro_request(gpio, offset);

	__aspeed_gpio_set(gc, offset, val);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);
}

static int aspeed_gpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr = bank_reg(gpio, bank, reg_dir);
	unsigned long flags;
	bool copro;
	u32 reg;

	if (!have_input(gpio, offset))
		return -ENOTSUPP;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(addr);
	reg &= ~GPIO_BIT(offset);

	copro = aspeed_gpio_copro_request(gpio, offset);
	iowrite32(reg, addr);
	if (copro)
		aspeed_gpio_copro_release(gpio, offset);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_gpio_dir_out(struct gpio_chip *gc,
			       unsigned int offset, int val)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	void __iomem *addr = bank_reg(gpio, bank, reg_dir);
	unsigned long flags;
	bool copro;
	u32 reg;

	if (!have_output(gpio, offset))
		return -ENOTSUPP;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	reg = ioread32(addr);
	reg |= GPIO_BIT(offset);

	copro = aspeed_gpio_copro_request(gpio, offset);
	__aspeed_gpio_set(gc, offset, val);
	iowrite32(reg, addr);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	unsigned long flags;
	u32 val;

	if (!have_input(gpio, offset))
		return GPIO_LINE_DIRECTION_OUT;

	if (!have_output(gpio, offset))
		return GPIO_LINE_DIRECTION_IN;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	val = ioread32(bank_reg(gpio, bank, reg_dir)) & GPIO_BIT(offset);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return val ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static inline int irqd_to_aspeed_gpio_data(struct irq_data *d,
					   struct aspeed_gpio **gpio,
					   const struct aspeed_gpio_bank **bank,
					   u32 *bit, int *offset)
{
	struct aspeed_gpio *internal;

	*offset = irqd_to_hwirq(d);

	internal = irq_data_get_irq_chip_data(d);

	/* This might be a bit of a questionable place to check */
	if (!have_irq(internal, *offset))
		return -ENOTSUPP;

	*gpio = internal;
	*bank = to_bank(*offset);
	*bit = GPIO_BIT(*offset);

	return 0;
}

static void aspeed_gpio_irq_ack(struct irq_data *d)
{
	const struct aspeed_gpio_bank *bank;
	struct aspeed_gpio *gpio;
	unsigned long flags;
	void __iomem *status_addr;
	int rc, offset;
	bool copro;
	u32 bit;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &bank, &bit, &offset);
	if (rc)
		return;

	status_addr = bank_reg(gpio, bank, reg_irq_status);

	raw_spin_lock_irqsave(&gpio->lock, flags);
	copro = aspeed_gpio_copro_request(gpio, offset);

	iowrite32(bit, status_addr);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);
}

static void aspeed_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	const struct aspeed_gpio_bank *bank;
	struct aspeed_gpio *gpio;
	unsigned long flags;
	u32 reg, bit;
	void __iomem *addr;
	int rc, offset;
	bool copro;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &bank, &bit, &offset);
	if (rc)
		return;

	addr = bank_reg(gpio, bank, reg_irq_enable);

	raw_spin_lock_irqsave(&gpio->lock, flags);
	copro = aspeed_gpio_copro_request(gpio, offset);

	reg = ioread32(addr);
	if (set)
		reg |= bit;
	else
		reg &= ~bit;
	iowrite32(reg, addr);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);
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
	u32 bit, reg;
	const struct aspeed_gpio_bank *bank;
	irq_flow_handler_t handler;
	struct aspeed_gpio *gpio;
	unsigned long flags;
	void __iomem *addr;
	int rc, offset;
	bool copro;

	rc = irqd_to_aspeed_gpio_data(d, &gpio, &bank, &bit, &offset);
	if (rc)
		return -EINVAL;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		type2 |= bit;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		type0 |= bit;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
		handler = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type0 |= bit;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		type1 |= bit;
		handler = handle_level_irq;
		break;
	default:
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&gpio->lock, flags);
	copro = aspeed_gpio_copro_request(gpio, offset);

	addr = bank_reg(gpio, bank, reg_irq_type0);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type0;
	iowrite32(reg, addr);

	addr = bank_reg(gpio, bank, reg_irq_type1);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type1;
	iowrite32(reg, addr);

	addr = bank_reg(gpio, bank, reg_irq_type2);
	reg = ioread32(addr);
	reg = (reg & ~bit) | type2;
	iowrite32(reg, addr);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	irq_set_handler_locked(d, handler);

	return 0;
}

static void aspeed_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct aspeed_gpio *data = gpiochip_get_data(gc);
	unsigned int i, p, banks;
	unsigned long reg;
	struct aspeed_gpio *gpio = gpiochip_get_data(gc);

	chained_irq_enter(ic, desc);

	banks = DIV_ROUND_UP(gpio->chip.ngpio, 32);
	for (i = 0; i < banks; i++) {
		const struct aspeed_gpio_bank *bank = &aspeed_gpio_banks[i];

		reg = ioread32(bank_reg(data, bank, reg_irq_status));

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
	unsigned long flags;
	void __iomem *treg;
	bool copro;
	u32 val;

	treg = bank_reg(gpio, to_bank(offset), reg_tolerance);

	raw_spin_lock_irqsave(&gpio->lock, flags);
	copro = aspeed_gpio_copro_request(gpio, offset);

	val = readl(treg);

	if (enable)
		val |= GPIO_BIT(offset);
	else
		val &= ~GPIO_BIT(offset);

	writel(val, treg);

	if (copro)
		aspeed_gpio_copro_release(gpio, offset);
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int aspeed_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	if (!have_gpio(gpiochip_get_data(chip), offset))
		return -ENODEV;

	return pinctrl_gpio_request(chip->base + offset);
}

static void aspeed_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip->base + offset);
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
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	const u32 mask = GPIO_BIT(offset);
	void __iomem *addr;
	u32 val;

	/* Note: Debounce timer isn't under control of the command
	 * source registers, so no need to sync with the coprocessor
	 */
	addr = bank_reg(gpio, bank, reg_debounce_sel1);
	val = ioread32(addr);
	iowrite32((val & ~mask) | GPIO_SET_DEBOUNCE1(timer, offset), addr);

	addr = bank_reg(gpio, bank, reg_debounce_sel2);
	val = ioread32(addr);
	iowrite32((val & ~mask) | GPIO_SET_DEBOUNCE2(timer, offset), addr);
}

static int enable_debounce(struct gpio_chip *chip, unsigned int offset,
				    unsigned long usecs)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	u32 requested_cycles;
	unsigned long flags;
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

	raw_spin_lock_irqsave(&gpio->lock, flags);

	if (timer_allocation_registered(gpio, offset)) {
		rc = unregister_allocated_timer(gpio, offset);
		if (rc < 0)
			goto out;
	}

	/* Try to find a timer already configured for the debounce period */
	for (i = 1; i < ARRAY_SIZE(debounce_timers); i++) {
		u32 cycles;

		cycles = ioread32(gpio->base + debounce_timers[i]);
		if (requested_cycles == cycles)
			break;
	}

	if (i == ARRAY_SIZE(debounce_timers)) {
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
			goto out;
		}

		i = j;

		iowrite32(requested_cycles, gpio->base + debounce_timers[i]);
	}

	if (WARN(i == 0, "Cannot register index of disabled timer\n")) {
		rc = -EINVAL;
		goto out;
	}

	register_allocated_timer(gpio, offset, i);
	configure_timer(gpio, offset, i);

out:
	raw_spin_unlock_irqrestore(&gpio->lock, flags);

	return rc;
}

static int disable_debounce(struct gpio_chip *chip, unsigned int offset)
{
	struct aspeed_gpio *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	int rc;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	rc = unregister_allocated_timer(gpio, offset);
	if (!rc)
		configure_timer(gpio, offset, 0);

	raw_spin_unlock_irqrestore(&gpio->lock, flags);

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
		return pinctrl_gpio_set_config(chip->base + offset, config);
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
	unsigned long flags;

	if (!gpio->cf_copro_bankmap)
		gpio->cf_copro_bankmap = kzalloc(gpio->chip.ngpio >> 3, GFP_KERNEL);
	if (!gpio->cf_copro_bankmap)
		return -ENOMEM;
	if (offset < 0 || offset > gpio->chip.ngpio)
		return -EINVAL;
	bindex = offset >> 3;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	/* Sanity check, this shouldn't happen */
	if (gpio->cf_copro_bankmap[bindex] == 0xff) {
		rc = -EIO;
		goto bail;
	}
	gpio->cf_copro_bankmap[bindex]++;

	/* Switch command source */
	if (gpio->cf_copro_bankmap[bindex] == 1)
		aspeed_gpio_change_cmd_source(gpio, bank, bindex,
					      GPIO_CMDSRC_COLDFIRE);

	if (vreg_offset)
		*vreg_offset = bank->val_regs;
	if (dreg_offset)
		*dreg_offset = bank->rdata_reg;
	if (bit)
		*bit = GPIO_OFFSET(offset);
 bail:
	raw_spin_unlock_irqrestore(&gpio->lock, flags);
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
	const struct aspeed_gpio_bank *bank = to_bank(offset);
	unsigned long flags;

	if (!gpio->cf_copro_bankmap)
		return -ENXIO;

	if (offset < 0 || offset > gpio->chip.ngpio)
		return -EINVAL;
	bindex = offset >> 3;

	raw_spin_lock_irqsave(&gpio->lock, flags);

	/* Sanity check, this shouldn't happen */
	if (gpio->cf_copro_bankmap[bindex] == 0) {
		rc = -EIO;
		goto bail;
	}
	gpio->cf_copro_bankmap[bindex]--;

	/* Switch command source */
	if (gpio->cf_copro_bankmap[bindex] == 0)
		aspeed_gpio_change_cmd_source(gpio, bank, bindex,
					      GPIO_CMDSRC_ARM);
 bail:
	raw_spin_unlock_irqrestore(&gpio->lock, flags);
	return rc;
}
EXPORT_SYMBOL_GPL(aspeed_gpio_copro_release_gpio);

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
	{ .nr_gpios = 220, .props = ast2400_bank_props, };

static const struct aspeed_bank_props ast2500_bank_props[] = {
	/*     input	  output   */
	{ 5, 0xffffffff, 0x0000ffff }, /* U/V/W/X */
	{ 6, 0x0fffffff, 0x0fffffff }, /* Y/Z/AA/AB, 4-GPIO hole */
	{ 7, 0x000000ff, 0x000000ff }, /* AC */
	{ },
};

static const struct aspeed_gpio_config ast2500_config =
	/* 232 for simplicity, actual number is 228 (4-GPIO hole in GPIOAB) */
	{ .nr_gpios = 232, .props = ast2500_bank_props, };

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
	{ .nr_gpios = 208, .props = ast2600_bank_props, };

static const struct of_device_id aspeed_gpio_of_table[] = {
	{ .compatible = "aspeed,ast2400-gpio", .data = &ast2400_config, },
	{ .compatible = "aspeed,ast2500-gpio", .data = &ast2500_config, },
	{ .compatible = "aspeed,ast2600-gpio", .data = &ast2600_config, },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_gpio_of_table);

static int __init aspeed_gpio_probe(struct platform_device *pdev)
{
	const struct of_device_id *gpio_id;
	struct aspeed_gpio *gpio;
	int rc, i, banks, err;
	u32 ngpio;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	raw_spin_lock_init(&gpio->lock);

	gpio_id = of_match_node(aspeed_gpio_of_table, pdev->dev.of_node);
	if (!gpio_id)
		return -EINVAL;

	gpio->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(gpio->clk)) {
		dev_warn(&pdev->dev,
				"Failed to get clock from devicetree, debouncing disabled\n");
		gpio->clk = NULL;
	}

	gpio->config = gpio_id->data;

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
	gpio->chip.set = aspeed_gpio_set;
	gpio->chip.set_config = aspeed_gpio_set_config;
	gpio->chip.label = dev_name(&pdev->dev);
	gpio->chip.base = -1;

	/* Allocate a cache of the output registers */
	banks = DIV_ROUND_UP(gpio->chip.ngpio, 32);
	gpio->dcache = devm_kcalloc(&pdev->dev,
				    banks, sizeof(u32), GFP_KERNEL);
	if (!gpio->dcache)
		return -ENOMEM;

	/*
	 * Populate it with initial values read from the HW and switch
	 * all command sources to the ARM by default
	 */
	for (i = 0; i < banks; i++) {
		const struct aspeed_gpio_bank *bank = &aspeed_gpio_banks[i];
		void __iomem *addr = bank_reg(gpio, bank, reg_rdata);
		gpio->dcache[i] = ioread32(addr);
		aspeed_gpio_change_cmd_source(gpio, bank, 0, GPIO_CMDSRC_ARM);
		aspeed_gpio_change_cmd_source(gpio, bank, 1, GPIO_CMDSRC_ARM);
		aspeed_gpio_change_cmd_source(gpio, bank, 2, GPIO_CMDSRC_ARM);
		aspeed_gpio_change_cmd_source(gpio, bank, 3, GPIO_CMDSRC_ARM);
	}

	/* Optionally set up an irqchip if there is an IRQ */
	rc = platform_get_irq(pdev, 0);
	if (rc > 0) {
		struct gpio_irq_chip *girq;

		gpio->irq = rc;
		girq = &gpio->chip.irq;
		girq->chip = &gpio->irqc;
		girq->chip->name = dev_name(&pdev->dev);
		girq->chip->irq_ack = aspeed_gpio_irq_ack;
		girq->chip->irq_mask = aspeed_gpio_irq_mask;
		girq->chip->irq_unmask = aspeed_gpio_irq_unmask;
		girq->chip->irq_set_type = aspeed_gpio_set_type;
		girq->parent_handler = aspeed_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = gpio->irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
		girq->init_valid_mask = aspeed_init_irq_valid_mask;
	}

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
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_gpio_of_table,
	},
};

module_platform_driver_probe(aspeed_gpio_driver, aspeed_gpio_probe);

MODULE_DESCRIPTION("Aspeed GPIO Driver");
MODULE_LICENSE("GPL");
