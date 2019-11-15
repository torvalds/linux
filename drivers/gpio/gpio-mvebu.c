/*
 * GPIO driver for Marvell SoCs
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Andrew Lunn <andrew@lunn.ch>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This driver is a fairly straightforward GPIO driver for the
 * complete family of Marvell EBU SoC platforms (Orion, Dove,
 * Kirkwood, Discovery, Armada 370/XP). The only complexity of this
 * driver is the different register layout that exists between the
 * non-SMP platforms (Orion, Dove, Kirkwood, Armada 370) and the SMP
 * platforms (MV78200 from the Discovery family and the Armada
 * XP). Therefore, this driver handles three variants of the GPIO
 * block:
 * - the basic variant, called "orion-gpio", with the simplest
 *   register set. Used on Orion, Dove, Kirkwoord, Armada 370 and
 *   non-SMP Discovery systems
 * - the mv78200 variant for MV78200 Discovery systems. This variant
 *   turns the edge mask and level mask registers into CPU0 edge
 *   mask/level mask registers, and adds CPU1 edge mask/level mask
 *   registers.
 * - the armadaxp variant for Armada XP systems. This variant keeps
 *   the normal cause/edge mask/level mask registers when the global
 *   interrupts are used, but adds per-CPU cause/edge mask/level mask
 *   registers n a separate memory area for the per-CPU GPIO
 *   interrupts.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/*
 * GPIO unit register offsets.
 */
#define GPIO_OUT_OFF			0x0000
#define GPIO_IO_CONF_OFF		0x0004
#define GPIO_BLINK_EN_OFF		0x0008
#define GPIO_IN_POL_OFF			0x000c
#define GPIO_DATA_IN_OFF		0x0010
#define GPIO_EDGE_CAUSE_OFF		0x0014
#define GPIO_EDGE_MASK_OFF		0x0018
#define GPIO_LEVEL_MASK_OFF		0x001c
#define GPIO_BLINK_CNT_SELECT_OFF	0x0020

/*
 * PWM register offsets.
 */
#define PWM_BLINK_ON_DURATION_OFF	0x0
#define PWM_BLINK_OFF_DURATION_OFF	0x4


/* The MV78200 has per-CPU registers for edge mask and level mask */
#define GPIO_EDGE_MASK_MV78200_OFF(cpu)	  ((cpu) ? 0x30 : 0x18)
#define GPIO_LEVEL_MASK_MV78200_OFF(cpu)  ((cpu) ? 0x34 : 0x1C)

/*
 * The Armada XP has per-CPU registers for interrupt cause, interrupt
 * mask and interrupt level mask. Those are relative to the
 * percpu_membase.
 */
#define GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu) ((cpu) * 0x4)
#define GPIO_EDGE_MASK_ARMADAXP_OFF(cpu)  (0x10 + (cpu) * 0x4)
#define GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu) (0x20 + (cpu) * 0x4)

#define MVEBU_GPIO_SOC_VARIANT_ORION	0x1
#define MVEBU_GPIO_SOC_VARIANT_MV78200	0x2
#define MVEBU_GPIO_SOC_VARIANT_ARMADAXP 0x3
#define MVEBU_GPIO_SOC_VARIANT_A8K	0x4

#define MVEBU_MAX_GPIO_PER_BANK		32

struct mvebu_pwm {
	void __iomem		*membase;
	unsigned long		 clk_rate;
	struct gpio_desc	*gpiod;
	struct pwm_chip		 chip;
	spinlock_t		 lock;
	struct mvebu_gpio_chip	*mvchip;

	/* Used to preserve GPIO/PWM registers across suspend/resume */
	u32			 blink_select;
	u32			 blink_on_duration;
	u32			 blink_off_duration;
};

struct mvebu_gpio_chip {
	struct gpio_chip   chip;
	struct regmap     *regs;
	u32		   offset;
	struct regmap     *percpu_regs;
	int		   irqbase;
	struct irq_domain *domain;
	int		   soc_variant;

	/* Used for PWM support */
	struct clk	  *clk;
	struct mvebu_pwm  *mvpwm;

	/* Used to preserve GPIO registers across suspend/resume */
	u32		   out_reg;
	u32		   io_conf_reg;
	u32		   blink_en_reg;
	u32		   in_pol_reg;
	u32		   edge_mask_regs[4];
	u32		   level_mask_regs[4];
};

/*
 * Functions returning addresses of individual registers for a given
 * GPIO controller.
 */

static void mvebu_gpioreg_edge_cause(struct mvebu_gpio_chip *mvchip,
			 struct regmap **map, unsigned int *offset)
{
	int cpu;

	switch (mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		*map = mvchip->regs;
		*offset = GPIO_EDGE_CAUSE_OFF + mvchip->offset;
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		*map = mvchip->percpu_regs;
		*offset = GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu);
		break;
	default:
		BUG();
	}
}

static u32
mvebu_gpio_read_edge_cause(struct mvebu_gpio_chip *mvchip)
{
	struct regmap *map;
	unsigned int offset;
	u32 val;

	mvebu_gpioreg_edge_cause(mvchip, &map, &offset);
	regmap_read(map, offset, &val);

	return val;
}

static void
mvebu_gpio_write_edge_cause(struct mvebu_gpio_chip *mvchip, u32 val)
{
	struct regmap *map;
	unsigned int offset;

	mvebu_gpioreg_edge_cause(mvchip, &map, &offset);
	regmap_write(map, offset, val);
}

static inline void
mvebu_gpioreg_edge_mask(struct mvebu_gpio_chip *mvchip,
			struct regmap **map, unsigned int *offset)
{
	int cpu;

	switch (mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		*map = mvchip->regs;
		*offset = GPIO_EDGE_MASK_OFF + mvchip->offset;
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		cpu = smp_processor_id();
		*map = mvchip->regs;
		*offset = GPIO_EDGE_MASK_MV78200_OFF(cpu);
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		*map = mvchip->percpu_regs;
		*offset = GPIO_EDGE_MASK_ARMADAXP_OFF(cpu);
		break;
	default:
		BUG();
	}
}

static u32
mvebu_gpio_read_edge_mask(struct mvebu_gpio_chip *mvchip)
{
	struct regmap *map;
	unsigned int offset;
	u32 val;

	mvebu_gpioreg_edge_mask(mvchip, &map, &offset);
	regmap_read(map, offset, &val);

	return val;
}

static void
mvebu_gpio_write_edge_mask(struct mvebu_gpio_chip *mvchip, u32 val)
{
	struct regmap *map;
	unsigned int offset;

	mvebu_gpioreg_edge_mask(mvchip, &map, &offset);
	regmap_write(map, offset, val);
}

static void
mvebu_gpioreg_level_mask(struct mvebu_gpio_chip *mvchip,
			 struct regmap **map, unsigned int *offset)
{
	int cpu;

	switch (mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		*map = mvchip->regs;
		*offset = GPIO_LEVEL_MASK_OFF + mvchip->offset;
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		cpu = smp_processor_id();
		*map = mvchip->regs;
		*offset = GPIO_LEVEL_MASK_MV78200_OFF(cpu);
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		cpu = smp_processor_id();
		*map = mvchip->percpu_regs;
		*offset = GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu);
		break;
	default:
		BUG();
	}
}

static u32
mvebu_gpio_read_level_mask(struct mvebu_gpio_chip *mvchip)
{
	struct regmap *map;
	unsigned int offset;
	u32 val;

	mvebu_gpioreg_level_mask(mvchip, &map, &offset);
	regmap_read(map, offset, &val);

	return val;
}

static void
mvebu_gpio_write_level_mask(struct mvebu_gpio_chip *mvchip, u32 val)
{
	struct regmap *map;
	unsigned int offset;

	mvebu_gpioreg_level_mask(mvchip, &map, &offset);
	regmap_write(map, offset, val);
}

/*
 * Functions returning addresses of individual registers for a given
 * PWM controller.
 */
static void __iomem *mvebu_pwmreg_blink_on_duration(struct mvebu_pwm *mvpwm)
{
	return mvpwm->membase + PWM_BLINK_ON_DURATION_OFF;
}

static void __iomem *mvebu_pwmreg_blink_off_duration(struct mvebu_pwm *mvpwm)
{
	return mvpwm->membase + PWM_BLINK_OFF_DURATION_OFF;
}

/*
 * Functions implementing the gpio_chip methods
 */
static void mvebu_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);

	regmap_update_bits(mvchip->regs, GPIO_OUT_OFF + mvchip->offset,
			   BIT(pin), value ? BIT(pin) : 0);
}

static int mvebu_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);
	u32 u;

	regmap_read(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset, &u);

	if (u & BIT(pin)) {
		u32 data_in, in_pol;

		regmap_read(mvchip->regs, GPIO_DATA_IN_OFF + mvchip->offset,
			    &data_in);
		regmap_read(mvchip->regs, GPIO_IN_POL_OFF + mvchip->offset,
			    &in_pol);
		u = data_in ^ in_pol;
	} else {
		regmap_read(mvchip->regs, GPIO_OUT_OFF + mvchip->offset, &u);
	}

	return (u >> pin) & 1;
}

static void mvebu_gpio_blink(struct gpio_chip *chip, unsigned int pin,
			     int value)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);

	regmap_update_bits(mvchip->regs, GPIO_BLINK_EN_OFF + mvchip->offset,
			   BIT(pin), value ? BIT(pin) : 0);
}

static int mvebu_gpio_direction_input(struct gpio_chip *chip, unsigned int pin)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);
	int ret;

	/*
	 * Check with the pinctrl driver whether this pin is usable as
	 * an input GPIO
	 */
	ret = pinctrl_gpio_direction_input(chip->base + pin);
	if (ret)
		return ret;

	regmap_update_bits(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset,
			   BIT(pin), BIT(pin));

	return 0;
}

static int mvebu_gpio_direction_output(struct gpio_chip *chip, unsigned int pin,
				       int value)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);
	int ret;

	/*
	 * Check with the pinctrl driver whether this pin is usable as
	 * an output GPIO
	 */
	ret = pinctrl_gpio_direction_output(chip->base + pin);
	if (ret)
		return ret;

	mvebu_gpio_blink(chip, pin, 0);
	mvebu_gpio_set(chip, pin, value);

	regmap_update_bits(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset,
			   BIT(pin), 0);

	return 0;
}

static int mvebu_gpio_get_direction(struct gpio_chip *chip, unsigned int pin)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);
	u32 u;

	regmap_read(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset, &u);

	return !!(u & BIT(pin));
}

static int mvebu_gpio_to_irq(struct gpio_chip *chip, unsigned int pin)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);

	return irq_create_mapping(mvchip->domain, pin);
}

/*
 * Functions implementing the irq_chip methods
 */
static void mvebu_gpio_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	u32 mask = d->mask;

	irq_gc_lock(gc);
	mvebu_gpio_write_edge_cause(mvchip, ~mask);
	irq_gc_unlock(gc);
}

static void mvebu_gpio_edge_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	ct->mask_cache_priv &= ~mask;
	mvebu_gpio_write_edge_mask(mvchip, ct->mask_cache_priv);
	irq_gc_unlock(gc);
}

static void mvebu_gpio_edge_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	ct->mask_cache_priv |= mask;
	mvebu_gpio_write_edge_mask(mvchip, ct->mask_cache_priv);
	irq_gc_unlock(gc);
}

static void mvebu_gpio_level_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	ct->mask_cache_priv &= ~mask;
	mvebu_gpio_write_level_mask(mvchip, ct->mask_cache_priv);
	irq_gc_unlock(gc);
}

static void mvebu_gpio_level_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	u32 mask = d->mask;

	irq_gc_lock(gc);
	ct->mask_cache_priv |= mask;
	mvebu_gpio_write_level_mask(mvchip, ct->mask_cache_priv);
	irq_gc_unlock(gc);
}

/*****************************************************************************
 * MVEBU GPIO IRQ
 *
 * GPIO_IN_POL register controls whether GPIO_DATA_IN will hold the same
 * value of the line or the opposite value.
 *
 * Level IRQ handlers: DATA_IN is used directly as cause register.
 *		       Interrupt are masked by LEVEL_MASK registers.
 * Edge IRQ handlers:  Change in DATA_IN are latched in EDGE_CAUSE.
 *		       Interrupt are masked by EDGE_MASK registers.
 * Both-edge handlers: Similar to regular Edge handlers, but also swaps
 *		       the polarity to catch the next line transaction.
 *		       This is a race condition that might not perfectly
 *		       work on some use cases.
 *
 * Every eight GPIO lines are grouped (OR'ed) before going up to main
 * cause register.
 *
 *		      EDGE  cause    mask
 *	  data-in   /--------| |-----| |----\
 *     -----| |-----			     ---- to main cause reg
 *	     X	    \----------------| |----/
 *	  polarity    LEVEL	     mask
 *
 ****************************************************************************/

static int mvebu_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct mvebu_gpio_chip *mvchip = gc->private;
	int pin;
	u32 u;

	pin = d->hwirq;

	regmap_read(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset, &u);
	if ((u & BIT(pin)) == 0)
		return -EINVAL;

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
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		regmap_update_bits(mvchip->regs,
				   GPIO_IN_POL_OFF + mvchip->offset,
				   BIT(pin), 0);
		break;
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		regmap_update_bits(mvchip->regs,
				   GPIO_IN_POL_OFF + mvchip->offset,
				   BIT(pin), BIT(pin));
		break;
	case IRQ_TYPE_EDGE_BOTH: {
		u32 data_in, in_pol, val;

		regmap_read(mvchip->regs,
			    GPIO_IN_POL_OFF + mvchip->offset, &in_pol);
		regmap_read(mvchip->regs,
			    GPIO_DATA_IN_OFF + mvchip->offset, &data_in);

		/*
		 * set initial polarity based on current input level
		 */
		if ((data_in ^ in_pol) & BIT(pin))
			val = BIT(pin); /* falling */
		else
			val = 0; /* raising */

		regmap_update_bits(mvchip->regs,
				   GPIO_IN_POL_OFF + mvchip->offset,
				   BIT(pin), val);
		break;
	}
	}
	return 0;
}

static void mvebu_gpio_irq_handler(struct irq_desc *desc)
{
	struct mvebu_gpio_chip *mvchip = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 cause, type, data_in, level_mask, edge_cause, edge_mask;
	int i;

	if (mvchip == NULL)
		return;

	chained_irq_enter(chip, desc);

	regmap_read(mvchip->regs, GPIO_DATA_IN_OFF + mvchip->offset, &data_in);
	level_mask = mvebu_gpio_read_level_mask(mvchip);
	edge_cause = mvebu_gpio_read_edge_cause(mvchip);
	edge_mask  = mvebu_gpio_read_edge_mask(mvchip);

	cause = (data_in & level_mask) | (edge_cause & edge_mask);

	for (i = 0; i < mvchip->chip.ngpio; i++) {
		int irq;

		irq = irq_find_mapping(mvchip->domain, i);

		if (!(cause & BIT(i)))
			continue;

		type = irq_get_trigger_type(irq);
		if ((type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			/* Swap polarity (race with GPIO line) */
			u32 polarity;

			regmap_read(mvchip->regs,
				    GPIO_IN_POL_OFF + mvchip->offset,
				    &polarity);
			polarity ^= BIT(i);
			regmap_write(mvchip->regs,
				     GPIO_IN_POL_OFF + mvchip->offset,
				     polarity);
		}

		generic_handle_irq(irq);
	}

	chained_irq_exit(chip, desc);
}

/*
 * Functions implementing the pwm_chip methods
 */
static struct mvebu_pwm *to_mvebu_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct mvebu_pwm, chip);
}

static int mvebu_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mvebu_pwm *mvpwm = to_mvebu_pwm(chip);
	struct mvebu_gpio_chip *mvchip = mvpwm->mvchip;
	struct gpio_desc *desc;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&mvpwm->lock, flags);

	if (mvpwm->gpiod) {
		ret = -EBUSY;
	} else {
		desc = gpiochip_request_own_desc(&mvchip->chip,
						 pwm->hwpwm, "mvebu-pwm",
						 GPIO_ACTIVE_HIGH,
						 GPIOD_OUT_LOW);
		if (IS_ERR(desc)) {
			ret = PTR_ERR(desc);
			goto out;
		}

		mvpwm->gpiod = desc;
	}
out:
	spin_unlock_irqrestore(&mvpwm->lock, flags);
	return ret;
}

static void mvebu_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mvebu_pwm *mvpwm = to_mvebu_pwm(chip);
	unsigned long flags;

	spin_lock_irqsave(&mvpwm->lock, flags);
	gpiochip_free_own_desc(mvpwm->gpiod);
	mvpwm->gpiod = NULL;
	spin_unlock_irqrestore(&mvpwm->lock, flags);
}

static void mvebu_pwm_get_state(struct pwm_chip *chip,
				struct pwm_device *pwm,
				struct pwm_state *state) {

	struct mvebu_pwm *mvpwm = to_mvebu_pwm(chip);
	struct mvebu_gpio_chip *mvchip = mvpwm->mvchip;
	unsigned long long val;
	unsigned long flags;
	u32 u;

	spin_lock_irqsave(&mvpwm->lock, flags);

	val = (unsigned long long)
		readl_relaxed(mvebu_pwmreg_blink_on_duration(mvpwm));
	val *= NSEC_PER_SEC;
	do_div(val, mvpwm->clk_rate);
	if (val > UINT_MAX)
		state->duty_cycle = UINT_MAX;
	else if (val)
		state->duty_cycle = val;
	else
		state->duty_cycle = 1;

	val = (unsigned long long)
		readl_relaxed(mvebu_pwmreg_blink_off_duration(mvpwm));
	val *= NSEC_PER_SEC;
	do_div(val, mvpwm->clk_rate);
	if (val < state->duty_cycle) {
		state->period = 1;
	} else {
		val -= state->duty_cycle;
		if (val > UINT_MAX)
			state->period = UINT_MAX;
		else if (val)
			state->period = val;
		else
			state->period = 1;
	}

	regmap_read(mvchip->regs, GPIO_BLINK_EN_OFF + mvchip->offset, &u);
	if (u)
		state->enabled = true;
	else
		state->enabled = false;

	spin_unlock_irqrestore(&mvpwm->lock, flags);
}

static int mvebu_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct mvebu_pwm *mvpwm = to_mvebu_pwm(chip);
	struct mvebu_gpio_chip *mvchip = mvpwm->mvchip;
	unsigned long long val;
	unsigned long flags;
	unsigned int on, off;

	val = (unsigned long long) mvpwm->clk_rate * state->duty_cycle;
	do_div(val, NSEC_PER_SEC);
	if (val > UINT_MAX)
		return -EINVAL;
	if (val)
		on = val;
	else
		on = 1;

	val = (unsigned long long) mvpwm->clk_rate *
		(state->period - state->duty_cycle);
	do_div(val, NSEC_PER_SEC);
	if (val > UINT_MAX)
		return -EINVAL;
	if (val)
		off = val;
	else
		off = 1;

	spin_lock_irqsave(&mvpwm->lock, flags);

	writel_relaxed(on, mvebu_pwmreg_blink_on_duration(mvpwm));
	writel_relaxed(off, mvebu_pwmreg_blink_off_duration(mvpwm));
	if (state->enabled)
		mvebu_gpio_blink(&mvchip->chip, pwm->hwpwm, 1);
	else
		mvebu_gpio_blink(&mvchip->chip, pwm->hwpwm, 0);

	spin_unlock_irqrestore(&mvpwm->lock, flags);

	return 0;
}

static const struct pwm_ops mvebu_pwm_ops = {
	.request = mvebu_pwm_request,
	.free = mvebu_pwm_free,
	.get_state = mvebu_pwm_get_state,
	.apply = mvebu_pwm_apply,
	.owner = THIS_MODULE,
};

static void __maybe_unused mvebu_pwm_suspend(struct mvebu_gpio_chip *mvchip)
{
	struct mvebu_pwm *mvpwm = mvchip->mvpwm;

	regmap_read(mvchip->regs, GPIO_BLINK_CNT_SELECT_OFF + mvchip->offset,
		    &mvpwm->blink_select);
	mvpwm->blink_on_duration =
		readl_relaxed(mvebu_pwmreg_blink_on_duration(mvpwm));
	mvpwm->blink_off_duration =
		readl_relaxed(mvebu_pwmreg_blink_off_duration(mvpwm));
}

static void __maybe_unused mvebu_pwm_resume(struct mvebu_gpio_chip *mvchip)
{
	struct mvebu_pwm *mvpwm = mvchip->mvpwm;

	regmap_write(mvchip->regs, GPIO_BLINK_CNT_SELECT_OFF + mvchip->offset,
		     mvpwm->blink_select);
	writel_relaxed(mvpwm->blink_on_duration,
		       mvebu_pwmreg_blink_on_duration(mvpwm));
	writel_relaxed(mvpwm->blink_off_duration,
		       mvebu_pwmreg_blink_off_duration(mvpwm));
}

static int mvebu_pwm_probe(struct platform_device *pdev,
			   struct mvebu_gpio_chip *mvchip,
			   int id)
{
	struct device *dev = &pdev->dev;
	struct mvebu_pwm *mvpwm;
	struct resource *res;
	u32 set;

	if (!of_device_is_compatible(mvchip->chip.of_node,
				     "marvell,armada-370-gpio"))
		return 0;

	/*
	 * There are only two sets of PWM configuration registers for
	 * all the GPIO lines on those SoCs which this driver reserves
	 * for the first two GPIO chips. So if the resource is missing
	 * we can't treat it as an error.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pwm");
	if (!res)
		return 0;

	if (IS_ERR(mvchip->clk))
		return PTR_ERR(mvchip->clk);

	/*
	 * Use set A for lines of GPIO chip with id 0, B for GPIO chip
	 * with id 1. Don't allow further GPIO chips to be used for PWM.
	 */
	if (id == 0)
		set = 0;
	else if (id == 1)
		set = U32_MAX;
	else
		return -EINVAL;
	regmap_write(mvchip->regs,
		     GPIO_BLINK_CNT_SELECT_OFF + mvchip->offset, set);

	mvpwm = devm_kzalloc(dev, sizeof(struct mvebu_pwm), GFP_KERNEL);
	if (!mvpwm)
		return -ENOMEM;
	mvchip->mvpwm = mvpwm;
	mvpwm->mvchip = mvchip;

	mvpwm->membase = devm_ioremap_resource(dev, res);
	if (IS_ERR(mvpwm->membase))
		return PTR_ERR(mvpwm->membase);

	mvpwm->clk_rate = clk_get_rate(mvchip->clk);
	if (!mvpwm->clk_rate) {
		dev_err(dev, "failed to get clock rate\n");
		return -EINVAL;
	}

	mvpwm->chip.dev = dev;
	mvpwm->chip.ops = &mvebu_pwm_ops;
	mvpwm->chip.npwm = mvchip->chip.ngpio;
	/*
	 * There may already be some PWM allocated, so we can't force
	 * mvpwm->chip.base to a fixed point like mvchip->chip.base.
	 * So, we let pwmchip_add() do the numbering and take the next free
	 * region.
	 */
	mvpwm->chip.base = -1;

	spin_lock_init(&mvpwm->lock);

	return pwmchip_add(&mvpwm->chip);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void mvebu_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct mvebu_gpio_chip *mvchip = gpiochip_get_data(chip);
	u32 out, io_conf, blink, in_pol, data_in, cause, edg_msk, lvl_msk;
	int i;

	regmap_read(mvchip->regs, GPIO_OUT_OFF + mvchip->offset, &out);
	regmap_read(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset, &io_conf);
	regmap_read(mvchip->regs, GPIO_BLINK_EN_OFF + mvchip->offset, &blink);
	regmap_read(mvchip->regs, GPIO_IN_POL_OFF + mvchip->offset, &in_pol);
	regmap_read(mvchip->regs, GPIO_DATA_IN_OFF + mvchip->offset, &data_in);
	cause	= mvebu_gpio_read_edge_cause(mvchip);
	edg_msk	= mvebu_gpio_read_edge_mask(mvchip);
	lvl_msk	= mvebu_gpio_read_level_mask(mvchip);

	for (i = 0; i < chip->ngpio; i++) {
		const char *label;
		u32 msk;
		bool is_out;

		label = gpiochip_is_requested(chip, i);
		if (!label)
			continue;

		msk = BIT(i);
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
			seq_puts(s, " disabled\n");
			continue;
		}
		if (edg_msk & msk)
			seq_puts(s, " edge ");
		if (lvl_msk & msk)
			seq_puts(s, " level");
		seq_printf(s, " (%s)\n", cause & msk ? "pending" : "clear  ");
	}
}
#else
#define mvebu_gpio_dbg_show NULL
#endif

static const struct of_device_id mvebu_gpio_of_match[] = {
	{
		.compatible = "marvell,orion-gpio",
		.data	    = (void *) MVEBU_GPIO_SOC_VARIANT_ORION,
	},
	{
		.compatible = "marvell,mv78200-gpio",
		.data	    = (void *) MVEBU_GPIO_SOC_VARIANT_MV78200,
	},
	{
		.compatible = "marvell,armadaxp-gpio",
		.data	    = (void *) MVEBU_GPIO_SOC_VARIANT_ARMADAXP,
	},
	{
		.compatible = "marvell,armada-370-gpio",
		.data	    = (void *) MVEBU_GPIO_SOC_VARIANT_ORION,
	},
	{
		.compatible = "marvell,armada-8k-gpio",
		.data       = (void *) MVEBU_GPIO_SOC_VARIANT_A8K,
	},
	{
		/* sentinel */
	},
};

static int mvebu_gpio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mvebu_gpio_chip *mvchip = platform_get_drvdata(pdev);
	int i;

	regmap_read(mvchip->regs, GPIO_OUT_OFF + mvchip->offset,
		    &mvchip->out_reg);
	regmap_read(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset,
		    &mvchip->io_conf_reg);
	regmap_read(mvchip->regs, GPIO_BLINK_EN_OFF + mvchip->offset,
		    &mvchip->blink_en_reg);
	regmap_read(mvchip->regs, GPIO_IN_POL_OFF + mvchip->offset,
		    &mvchip->in_pol_reg);

	switch (mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		regmap_read(mvchip->regs, GPIO_EDGE_MASK_OFF + mvchip->offset,
			    &mvchip->edge_mask_regs[0]);
		regmap_read(mvchip->regs, GPIO_LEVEL_MASK_OFF + mvchip->offset,
			    &mvchip->level_mask_regs[0]);
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		for (i = 0; i < 2; i++) {
			regmap_read(mvchip->regs,
				    GPIO_EDGE_MASK_MV78200_OFF(i),
				    &mvchip->edge_mask_regs[i]);
			regmap_read(mvchip->regs,
				    GPIO_LEVEL_MASK_MV78200_OFF(i),
				    &mvchip->level_mask_regs[i]);
		}
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		for (i = 0; i < 4; i++) {
			regmap_read(mvchip->regs,
				    GPIO_EDGE_MASK_ARMADAXP_OFF(i),
				    &mvchip->edge_mask_regs[i]);
			regmap_read(mvchip->regs,
				    GPIO_LEVEL_MASK_ARMADAXP_OFF(i),
				    &mvchip->level_mask_regs[i]);
		}
		break;
	default:
		BUG();
	}

	if (IS_ENABLED(CONFIG_PWM))
		mvebu_pwm_suspend(mvchip);

	return 0;
}

static int mvebu_gpio_resume(struct platform_device *pdev)
{
	struct mvebu_gpio_chip *mvchip = platform_get_drvdata(pdev);
	int i;

	regmap_write(mvchip->regs, GPIO_OUT_OFF + mvchip->offset,
		     mvchip->out_reg);
	regmap_write(mvchip->regs, GPIO_IO_CONF_OFF + mvchip->offset,
		     mvchip->io_conf_reg);
	regmap_write(mvchip->regs, GPIO_BLINK_EN_OFF + mvchip->offset,
		     mvchip->blink_en_reg);
	regmap_write(mvchip->regs, GPIO_IN_POL_OFF + mvchip->offset,
		     mvchip->in_pol_reg);

	switch (mvchip->soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		regmap_write(mvchip->regs, GPIO_EDGE_MASK_OFF + mvchip->offset,
			     mvchip->edge_mask_regs[0]);
		regmap_write(mvchip->regs, GPIO_LEVEL_MASK_OFF + mvchip->offset,
			     mvchip->level_mask_regs[0]);
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		for (i = 0; i < 2; i++) {
			regmap_write(mvchip->regs,
				     GPIO_EDGE_MASK_MV78200_OFF(i),
				     mvchip->edge_mask_regs[i]);
			regmap_write(mvchip->regs,
				     GPIO_LEVEL_MASK_MV78200_OFF(i),
				     mvchip->level_mask_regs[i]);
		}
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		for (i = 0; i < 4; i++) {
			regmap_write(mvchip->regs,
				     GPIO_EDGE_MASK_ARMADAXP_OFF(i),
				     mvchip->edge_mask_regs[i]);
			regmap_write(mvchip->regs,
				     GPIO_LEVEL_MASK_ARMADAXP_OFF(i),
				     mvchip->level_mask_regs[i]);
		}
		break;
	default:
		BUG();
	}

	if (IS_ENABLED(CONFIG_PWM))
		mvebu_pwm_resume(mvchip);

	return 0;
}

static const struct regmap_config mvebu_gpio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static int mvebu_gpio_probe_raw(struct platform_device *pdev,
				struct mvebu_gpio_chip *mvchip)
{
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	mvchip->regs = devm_regmap_init_mmio(&pdev->dev, base,
					     &mvebu_gpio_regmap_config);
	if (IS_ERR(mvchip->regs))
		return PTR_ERR(mvchip->regs);

	/*
	 * For the legacy SoCs, the regmap directly maps to the GPIO
	 * registers, so no offset is needed.
	 */
	mvchip->offset = 0;

	/*
	 * The Armada XP has a second range of registers for the
	 * per-CPU registers
	 */
	if (mvchip->soc_variant == MVEBU_GPIO_SOC_VARIANT_ARMADAXP) {
		base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(base))
			return PTR_ERR(base);

		mvchip->percpu_regs =
			devm_regmap_init_mmio(&pdev->dev, base,
					      &mvebu_gpio_regmap_config);
		if (IS_ERR(mvchip->percpu_regs))
			return PTR_ERR(mvchip->percpu_regs);
	}

	return 0;
}

static int mvebu_gpio_probe_syscon(struct platform_device *pdev,
				   struct mvebu_gpio_chip *mvchip)
{
	mvchip->regs = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(mvchip->regs))
		return PTR_ERR(mvchip->regs);

	if (of_property_read_u32(pdev->dev.of_node, "offset", &mvchip->offset))
		return -EINVAL;

	return 0;
}

static int mvebu_gpio_probe(struct platform_device *pdev)
{
	struct mvebu_gpio_chip *mvchip;
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	unsigned int ngpios;
	bool have_irqs;
	int soc_variant;
	int i, cpu, id;
	int err;

	match = of_match_device(mvebu_gpio_of_match, &pdev->dev);
	if (match)
		soc_variant = (unsigned long) match->data;
	else
		soc_variant = MVEBU_GPIO_SOC_VARIANT_ORION;

	/* Some gpio controllers do not provide irq support */
	have_irqs = of_irq_count(np) != 0;

	mvchip = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_gpio_chip),
			      GFP_KERNEL);
	if (!mvchip)
		return -ENOMEM;

	platform_set_drvdata(pdev, mvchip);

	if (of_property_read_u32(pdev->dev.of_node, "ngpios", &ngpios)) {
		dev_err(&pdev->dev, "Missing ngpios OF property\n");
		return -ENODEV;
	}

	id = of_alias_get_id(pdev->dev.of_node, "gpio");
	if (id < 0) {
		dev_err(&pdev->dev, "Couldn't get OF id\n");
		return id;
	}

	mvchip->clk = devm_clk_get(&pdev->dev, NULL);
	/* Not all SoCs require a clock.*/
	if (!IS_ERR(mvchip->clk))
		clk_prepare_enable(mvchip->clk);

	mvchip->soc_variant = soc_variant;
	mvchip->chip.label = dev_name(&pdev->dev);
	mvchip->chip.parent = &pdev->dev;
	mvchip->chip.request = gpiochip_generic_request;
	mvchip->chip.free = gpiochip_generic_free;
	mvchip->chip.get_direction = mvebu_gpio_get_direction;
	mvchip->chip.direction_input = mvebu_gpio_direction_input;
	mvchip->chip.get = mvebu_gpio_get;
	mvchip->chip.direction_output = mvebu_gpio_direction_output;
	mvchip->chip.set = mvebu_gpio_set;
	if (have_irqs)
		mvchip->chip.to_irq = mvebu_gpio_to_irq;
	mvchip->chip.base = id * MVEBU_MAX_GPIO_PER_BANK;
	mvchip->chip.ngpio = ngpios;
	mvchip->chip.can_sleep = false;
	mvchip->chip.of_node = np;
	mvchip->chip.dbg_show = mvebu_gpio_dbg_show;

	if (soc_variant == MVEBU_GPIO_SOC_VARIANT_A8K)
		err = mvebu_gpio_probe_syscon(pdev, mvchip);
	else
		err = mvebu_gpio_probe_raw(pdev, mvchip);

	if (err)
		return err;

	/*
	 * Mask and clear GPIO interrupts.
	 */
	switch (soc_variant) {
	case MVEBU_GPIO_SOC_VARIANT_ORION:
	case MVEBU_GPIO_SOC_VARIANT_A8K:
		regmap_write(mvchip->regs,
			     GPIO_EDGE_CAUSE_OFF + mvchip->offset, 0);
		regmap_write(mvchip->regs,
			     GPIO_EDGE_MASK_OFF + mvchip->offset, 0);
		regmap_write(mvchip->regs,
			     GPIO_LEVEL_MASK_OFF + mvchip->offset, 0);
		break;
	case MVEBU_GPIO_SOC_VARIANT_MV78200:
		regmap_write(mvchip->regs, GPIO_EDGE_CAUSE_OFF, 0);
		for (cpu = 0; cpu < 2; cpu++) {
			regmap_write(mvchip->regs,
				     GPIO_EDGE_MASK_MV78200_OFF(cpu), 0);
			regmap_write(mvchip->regs,
				     GPIO_LEVEL_MASK_MV78200_OFF(cpu), 0);
		}
		break;
	case MVEBU_GPIO_SOC_VARIANT_ARMADAXP:
		regmap_write(mvchip->regs, GPIO_EDGE_CAUSE_OFF, 0);
		regmap_write(mvchip->regs, GPIO_EDGE_MASK_OFF, 0);
		regmap_write(mvchip->regs, GPIO_LEVEL_MASK_OFF, 0);
		for (cpu = 0; cpu < 4; cpu++) {
			regmap_write(mvchip->percpu_regs,
				     GPIO_EDGE_CAUSE_ARMADAXP_OFF(cpu), 0);
			regmap_write(mvchip->percpu_regs,
				     GPIO_EDGE_MASK_ARMADAXP_OFF(cpu), 0);
			regmap_write(mvchip->percpu_regs,
				     GPIO_LEVEL_MASK_ARMADAXP_OFF(cpu), 0);
		}
		break;
	default:
		BUG();
	}

	devm_gpiochip_add_data(&pdev->dev, &mvchip->chip, mvchip);

	/* Some gpio controllers do not provide irq support */
	if (!have_irqs)
		return 0;

	mvchip->domain =
	    irq_domain_add_linear(np, ngpios, &irq_generic_chip_ops, NULL);
	if (!mvchip->domain) {
		dev_err(&pdev->dev, "couldn't allocate irq domain %s (DT).\n",
			mvchip->chip.label);
		return -ENODEV;
	}

	err = irq_alloc_domain_generic_chips(
	    mvchip->domain, ngpios, 2, np->name, handle_level_irq,
	    IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_LEVEL, 0, 0);
	if (err) {
		dev_err(&pdev->dev, "couldn't allocate irq chips %s (DT).\n",
			mvchip->chip.label);
		goto err_domain;
	}

	/*
	 * NOTE: The common accessors cannot be used because of the percpu
	 * access to the mask registers
	 */
	gc = irq_get_domain_generic_chip(mvchip->domain, 0);
	gc->private = mvchip;
	ct = &gc->chip_types[0];
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->chip.irq_mask = mvebu_gpio_level_irq_mask;
	ct->chip.irq_unmask = mvebu_gpio_level_irq_unmask;
	ct->chip.irq_set_type = mvebu_gpio_irq_set_type;
	ct->chip.name = mvchip->chip.label;

	ct = &gc->chip_types[1];
	ct->type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	ct->chip.irq_ack = mvebu_gpio_irq_ack;
	ct->chip.irq_mask = mvebu_gpio_edge_irq_mask;
	ct->chip.irq_unmask = mvebu_gpio_edge_irq_unmask;
	ct->chip.irq_set_type = mvebu_gpio_irq_set_type;
	ct->handler = handle_edge_irq;
	ct->chip.name = mvchip->chip.label;

	/*
	 * Setup the interrupt handlers. Each chip can have up to 4
	 * interrupt handlers, with each handler dealing with 8 GPIO
	 * pins.
	 */
	for (i = 0; i < 4; i++) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0)
			continue;
		irq_set_chained_handler_and_data(irq, mvebu_gpio_irq_handler,
						 mvchip);
	}

	/* Some MVEBU SoCs have simple PWM support for GPIO lines */
	if (IS_ENABLED(CONFIG_PWM))
		return mvebu_pwm_probe(pdev, mvchip, id);

	return 0;

err_domain:
	irq_domain_remove(mvchip->domain);

	return err;
}

static struct platform_driver mvebu_gpio_driver = {
	.driver		= {
		.name		= "mvebu-gpio",
		.of_match_table = mvebu_gpio_of_match,
	},
	.probe		= mvebu_gpio_probe,
	.suspend        = mvebu_gpio_suspend,
	.resume         = mvebu_gpio_resume,
};
builtin_platform_driver(mvebu_gpio_driver);
