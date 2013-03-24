/*
 * arch/arm/plat-orion/gpio.c
 *
 * Marvell Orion SoC GPIO handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <plat/orion-gpio.h>

/*
 * GPIO unit register offsets.
 */
#define GPIO_OUT_OFF		0x0000
#define GPIO_IO_CONF_OFF	0x0004
#define GPIO_BLINK_EN_OFF	0x0008
#define GPIO_IN_POL_OFF		0x000c
#define GPIO_DATA_IN_OFF	0x0010
#define GPIO_EDGE_CAUSE_OFF	0x0014
#define GPIO_EDGE_MASK_OFF	0x0018
#define GPIO_LEVEL_MASK_OFF	0x001c

struct orion_gpio_chip {
	struct gpio_chip	chip;
	spinlock_t		lock;
	void __iomem		*base;
	unsigned long		valid_input;
	unsigned long		valid_output;
	int			mask_offset;
	int			secondary_irq_base;
	struct irq_domain       *domain;
};

static void __iomem *GPIO_OUT(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_OUT_OFF;
}

static void __iomem *GPIO_IO_CONF(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_IO_CONF_OFF;
}

static void __iomem *GPIO_BLINK_EN(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_BLINK_EN_OFF;
}

static void __iomem *GPIO_IN_POL(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_IN_POL_OFF;
}

static void __iomem *GPIO_DATA_IN(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_DATA_IN_OFF;
}

static void __iomem *GPIO_EDGE_CAUSE(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_EDGE_CAUSE_OFF;
}

static void __iomem *GPIO_EDGE_MASK(struct orion_gpio_chip *ochip)
{
	return ochip->base + ochip->mask_offset + GPIO_EDGE_MASK_OFF;
}

static void __iomem *GPIO_LEVEL_MASK(struct orion_gpio_chip *ochip)
{
	return ochip->base + ochip->mask_offset + GPIO_LEVEL_MASK_OFF;
}


static struct orion_gpio_chip orion_gpio_chips[2];
static int orion_gpio_chip_count;

static inline void
__set_direction(struct orion_gpio_chip *ochip, unsigned pin, int input)
{
	u32 u;

	u = readl(GPIO_IO_CONF(ochip));
	if (input)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_IO_CONF(ochip));
}

static void __set_level(struct orion_gpio_chip *ochip, unsigned pin, int high)
{
	u32 u;

	u = readl(GPIO_OUT(ochip));
	if (high)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_OUT(ochip));
}

static inline void
__set_blinking(struct orion_gpio_chip *ochip, unsigned pin, int blink)
{
	u32 u;

	u = readl(GPIO_BLINK_EN(ochip));
	if (blink)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_BLINK_EN(ochip));
}

static inline int
orion_gpio_is_valid(struct orion_gpio_chip *ochip, unsigned pin, int mode)
{
	if (pin >= ochip->chip.ngpio)
		goto err_out;

	if ((mode & GPIO_INPUT_OK) && !test_bit(pin, &ochip->valid_input))
		goto err_out;

	if ((mode & GPIO_OUTPUT_OK) && !test_bit(pin, &ochip->valid_output))
		goto err_out;

	return 1;

err_out:
	pr_debug("%s: invalid GPIO %d\n", __func__, pin);
	return false;
}

/*
 * GENERIC_GPIO primitives.
 */
static int orion_gpio_request(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);

	if (orion_gpio_is_valid(ochip, pin, GPIO_INPUT_OK) ||
	    orion_gpio_is_valid(ochip, pin, GPIO_OUTPUT_OK))
		return 0;

	return -EINVAL;
}

static int orion_gpio_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	if (!orion_gpio_is_valid(ochip, pin, GPIO_INPUT_OK))
		return -EINVAL;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_direction(ochip, pin, 1);
	spin_unlock_irqrestore(&ochip->lock, flags);

	return 0;
}

static int orion_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	int val;

	if (readl(GPIO_IO_CONF(ochip)) & (1 << pin)) {
		val = readl(GPIO_DATA_IN(ochip)) ^ readl(GPIO_IN_POL(ochip));
	} else {
		val = readl(GPIO_OUT(ochip));
	}

	return (val >> pin) & 1;
}

static int
orion_gpio_direction_output(struct gpio_chip *chip, unsigned pin, int value)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	if (!orion_gpio_is_valid(ochip, pin, GPIO_OUTPUT_OK))
		return -EINVAL;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_blinking(ochip, pin, 0);
	__set_level(ochip, pin, value);
	__set_direction(ochip, pin, 0);
	spin_unlock_irqrestore(&ochip->lock, flags);

	return 0;
}

static void orion_gpio_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_level(ochip, pin, value);
	spin_unlock_irqrestore(&ochip->lock, flags);
}

static int orion_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);

	return irq_create_mapping(ochip->domain,
				  ochip->secondary_irq_base + pin);
}

/*
 * Orion-specific GPIO API extensions.
 */
static struct orion_gpio_chip *orion_gpio_chip_find(int pin)
{
	int i;

	for (i = 0; i < orion_gpio_chip_count; i++) {
		struct orion_gpio_chip *ochip = orion_gpio_chips + i;
		struct gpio_chip *chip = &ochip->chip;

		if (pin >= chip->base && pin < chip->base + chip->ngpio)
			return ochip;
	}

	return NULL;
}

void __init orion_gpio_set_unused(unsigned pin)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);

	if (ochip == NULL)
		return;

	pin -= ochip->chip.base;

	/* Configure as output, drive low. */
	__set_level(ochip, pin, 0);
	__set_direction(ochip, pin, 0);
}

void __init orion_gpio_set_valid(unsigned pin, int mode)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);

	if (ochip == NULL)
		return;

	pin -= ochip->chip.base;

	if (mode == 1)
		mode = GPIO_INPUT_OK | GPIO_OUTPUT_OK;

	if (mode & GPIO_INPUT_OK)
		__set_bit(pin, &ochip->valid_input);
	else
		__clear_bit(pin, &ochip->valid_input);

	if (mode & GPIO_OUTPUT_OK)
		__set_bit(pin, &ochip->valid_output);
	else
		__clear_bit(pin, &ochip->valid_output);
}

void orion_gpio_set_blink(unsigned pin, int blink)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);
	unsigned long flags;

	if (ochip == NULL)
		return;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_level(ochip, pin & 31, 0);
	__set_blinking(ochip, pin & 31, blink);
	spin_unlock_irqrestore(&ochip->lock, flags);
}
EXPORT_SYMBOL(orion_gpio_set_blink);

#define ORION_BLINK_HALF_PERIOD 100 /* ms */

int orion_gpio_led_blink_set(unsigned gpio, int state,
	unsigned long *delay_on, unsigned long *delay_off)
{

	if (delay_on && delay_off && !*delay_on && !*delay_off)
		*delay_on = *delay_off = ORION_BLINK_HALF_PERIOD;

	switch (state) {
	case GPIO_LED_NO_BLINK_LOW:
	case GPIO_LED_NO_BLINK_HIGH:
		orion_gpio_set_blink(gpio, 0);
		gpio_set_value(gpio, state);
		break;
	case GPIO_LED_BLINK:
		orion_gpio_set_blink(gpio, 1);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(orion_gpio_led_blink_set);


/*****************************************************************************
 * Orion GPIO IRQ
 *
 * GPIO_IN_POL register controls whether GPIO_DATA_IN will hold the same
 * value of the line or the opposite value.
 *
 * Level IRQ handlers: DATA_IN is used directly as cause register.
 *                     Interrupt are masked by LEVEL_MASK registers.
 * Edge IRQ handlers:  Change in DATA_IN are latched in EDGE_CAUSE.
 *                     Interrupt are masked by EDGE_MASK registers.
 * Both-edge handlers: Similar to regular Edge handlers, but also swaps
 *                     the polarity to catch the next line transaction.
 *                     This is a race condition that might not perfectly
 *                     work on some use cases.
 *
 * Every eight GPIO lines are grouped (OR'ed) before going up to main
 * cause register.
 *
 *                    EDGE  cause    mask
 *        data-in   /--------| |-----| |----\
 *     -----| |-----                         ---- to main cause reg
 *           X      \----------------| |----/
 *        polarity    LEVEL          mask
 *
 ****************************************************************************/

static int gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct orion_gpio_chip *ochip = gc->private;
	int pin;
	u32 u;

	pin = d->hwirq - ochip->secondary_irq_base;

	u = readl(GPIO_IO_CONF(ochip)) & (1 << pin);
	if (!u) {
		return -EINVAL;
	}

	type &= IRQ_TYPE_SENSE_MASK;
	if (type == IRQ_TYPE_NONE)
		return -EINVAL;

	/* Check if we need to change chip and handler */
	if (!(ct->type & type))
		if (irq_setup_alt_chip(d, type))
			return -EINVAL;

	/*
	 * Configure interrupt polarity.
	 */
	if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH) {
		u = readl(GPIO_IN_POL(ochip));
		u &= ~(1 << pin);
		writel(u, GPIO_IN_POL(ochip));
	} else if (type == IRQ_TYPE_EDGE_FALLING || type == IRQ_TYPE_LEVEL_LOW) {
		u = readl(GPIO_IN_POL(ochip));
		u |= 1 << pin;
		writel(u, GPIO_IN_POL(ochip));
	} else if (type == IRQ_TYPE_EDGE_BOTH) {
		u32 v;

		v = readl(GPIO_IN_POL(ochip)) ^ readl(GPIO_DATA_IN(ochip));

		/*
		 * set initial polarity based on current input level
		 */
		u = readl(GPIO_IN_POL(ochip));
		if (v & (1 << pin))
			u |= 1 << pin;		/* falling */
		else
			u &= ~(1 << pin);	/* rising */
		writel(u, GPIO_IN_POL(ochip));
	}
	return 0;
}

static void gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct orion_gpio_chip *ochip = irq_get_handler_data(irq);
	u32 cause, type;
	int i;

	if (ochip == NULL)
		return;

	cause = readl(GPIO_DATA_IN(ochip)) & readl(GPIO_LEVEL_MASK(ochip));
	cause |= readl(GPIO_EDGE_CAUSE(ochip)) & readl(GPIO_EDGE_MASK(ochip));

	for (i = 0; i < ochip->chip.ngpio; i++) {
		int irq;

		irq = ochip->secondary_irq_base + i;

		if (!(cause & (1 << i)))
			continue;

		type = irqd_get_trigger_type(irq_get_irq_data(irq));
		if ((type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			/* Swap polarity (race with GPIO line) */
			u32 polarity;

			polarity = readl(GPIO_IN_POL(ochip));
			polarity ^= 1 << i;
			writel(polarity, GPIO_IN_POL(ochip));
		}
		generic_handle_irq(irq);
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void orion_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	u32 out, io_conf, blink, in_pol, data_in, cause, edg_msk, lvl_msk;
	int i;

	out	= readl_relaxed(GPIO_OUT(ochip));
	io_conf	= readl_relaxed(GPIO_IO_CONF(ochip));
	blink	= readl_relaxed(GPIO_BLINK_EN(ochip));
	in_pol	= readl_relaxed(GPIO_IN_POL(ochip));
	data_in	= readl_relaxed(GPIO_DATA_IN(ochip));
	cause	= readl_relaxed(GPIO_EDGE_CAUSE(ochip));
	edg_msk	= readl_relaxed(GPIO_EDGE_MASK(ochip));
	lvl_msk	= readl_relaxed(GPIO_LEVEL_MASK(ochip));

	for (i = 0; i < chip->ngpio; i++) {
		const char *label;
		u32 msk;
		bool is_out;

		label = gpiochip_is_requested(chip, i);
		if (!label)
			continue;

		msk = 1 << i;
		is_out = !(io_conf & msk);

		seq_printf(s, " gpio-%-3d (%-20.20s)", chip->base + i, label);

		if (is_out) {
			seq_printf(s, " out %s %s\n",
				   out & msk ? "hi" : "lo",
				   blink & msk ? "(blink )" : "");
			continue;
		}

		seq_printf(s, " in  %s (act %s) - IRQ",
			   (data_in ^ in_pol) & msk  ? "hi" : "lo",
			   in_pol & msk ? "lo" : "hi");
		if (!((edg_msk | lvl_msk) & msk)) {
			seq_printf(s, " disabled\n");
			continue;
		}
		if (edg_msk & msk)
			seq_printf(s, " edge ");
		if (lvl_msk & msk)
			seq_printf(s, " level");
		seq_printf(s, " (%s)\n", cause & msk ? "pending" : "clear  ");
	}
}
#else
#define orion_gpio_dbg_show NULL
#endif

void __init orion_gpio_init(struct device_node *np,
			    int gpio_base, int ngpio,
			    void __iomem *base, int mask_offset,
			    int secondary_irq_base,
			    int irqs[4])
{
	struct orion_gpio_chip *ochip;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	char gc_label[16];
	int i;

	if (orion_gpio_chip_count == ARRAY_SIZE(orion_gpio_chips))
		return;

	snprintf(gc_label, sizeof(gc_label), "orion_gpio%d",
		orion_gpio_chip_count);

	ochip = orion_gpio_chips + orion_gpio_chip_count;
	ochip->chip.label = kstrdup(gc_label, GFP_KERNEL);
	ochip->chip.request = orion_gpio_request;
	ochip->chip.direction_input = orion_gpio_direction_input;
	ochip->chip.get = orion_gpio_get;
	ochip->chip.direction_output = orion_gpio_direction_output;
	ochip->chip.set = orion_gpio_set;
	ochip->chip.to_irq = orion_gpio_to_irq;
	ochip->chip.base = gpio_base;
	ochip->chip.ngpio = ngpio;
	ochip->chip.can_sleep = 0;
#ifdef CONFIG_OF
	ochip->chip.of_node = np;
#endif
	ochip->chip.dbg_show = orion_gpio_dbg_show;

	spin_lock_init(&ochip->lock);
	ochip->base = (void __iomem *)base;
	ochip->valid_input = 0;
	ochip->valid_output = 0;
	ochip->mask_offset = mask_offset;
	ochip->secondary_irq_base = secondary_irq_base;

	gpiochip_add(&ochip->chip);

	/*
	 * Mask and clear GPIO interrupts.
	 */
	writel(0, GPIO_EDGE_CAUSE(ochip));
	writel(0, GPIO_EDGE_MASK(ochip));
	writel(0, GPIO_LEVEL_MASK(ochip));

	/* Setup the interrupt handlers. Each chip can have up to 4
	 * interrupt handlers, with each handler dealing with 8 GPIO
	 * pins. */

	for (i = 0; i < 4; i++) {
		if (irqs[i]) {
			irq_set_handler_data(irqs[i], ochip);
			irq_set_chained_handler(irqs[i], gpio_irq_handler);
		}
	}

	gc = irq_alloc_generic_chip("orion_gpio_irq", 2,
				    secondary_irq_base,
				    ochip->base, handle_level_irq);
	gc->private = ochip;
	ct = gc->chip_types;
	ct->regs.mask = ochip->mask_offset + GPIO_LEVEL_MASK_OFF;
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_set_type = gpio_irq_set_type;
	ct->chip.name = ochip->chip.label;

	ct++;
	ct->regs.mask = ochip->mask_offset + GPIO_EDGE_MASK_OFF;
	ct->regs.ack = GPIO_EDGE_CAUSE_OFF;
	ct->type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	ct->chip.irq_ack = irq_gc_ack_clr_bit;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_set_type = gpio_irq_set_type;
	ct->handler = handle_edge_irq;
	ct->chip.name = ochip->chip.label;

	irq_setup_generic_chip(gc, IRQ_MSK(ngpio), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);

	/* Setup irq domain on top of the generic chip. */
	ochip->domain = irq_domain_add_legacy(np,
					      ochip->chip.ngpio,
					      ochip->secondary_irq_base,
					      ochip->secondary_irq_base,
					      &irq_domain_simple_ops,
					      ochip);
	if (!ochip->domain)
		panic("%s: couldn't allocate irq domain (DT).\n",
		      ochip->chip.label);

	orion_gpio_chip_count++;
}

#ifdef CONFIG_OF
static void __init orion_gpio_of_init_one(struct device_node *np,
					  int irq_gpio_base)
{
	int ngpio, gpio_base, mask_offset;
	void __iomem *base;
	int ret, i;
	int irqs[4];
	int secondary_irq_base;

	ret = of_property_read_u32(np, "ngpio", &ngpio);
	if (ret)
		goto out;
	ret = of_property_read_u32(np, "mask-offset", &mask_offset);
	if (ret == -EINVAL)
		mask_offset = 0;
	else
		goto out;
	base = of_iomap(np, 0);
	if (!base)
		goto out;

	secondary_irq_base = irq_gpio_base + (32 * orion_gpio_chip_count);
	gpio_base = 32 * orion_gpio_chip_count;

	/* Get the interrupt numbers. Each chip can have up to 4
	 * interrupt handlers, with each handler dealing with 8 GPIO
	 * pins. */

	for (i = 0; i < 4; i++)
		irqs[i] = irq_of_parse_and_map(np, i);

	orion_gpio_init(np, gpio_base, ngpio, base, mask_offset,
			secondary_irq_base, irqs);
	return;
out:
	pr_err("%s: %s: missing mandatory property\n", __func__, np->name);
}

void __init orion_gpio_of_init(int irq_gpio_base)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "marvell,orion-gpio")
		orion_gpio_of_init_one(np, irq_gpio_base);
}
#endif
