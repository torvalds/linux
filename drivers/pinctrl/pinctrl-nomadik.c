/*
 * Generic GPIO driver for logic cells found in the Nomadik SoC
 *
 * Copyright (C) 2008,2009 STMicroelectronics
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *   Rewritten based on work by Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2011 Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/pinctrl/pinctrl.h>

#include <asm/mach/irq.h>

#include <plat/pincfg.h>
#include <plat/gpio-nomadik.h>

#include "pinctrl-nomadik.h"

/*
 * The GPIO module in the Nomadik family of Systems-on-Chip is an
 * AMBA device, managing 32 pins and alternate functions.  The logic block
 * is currently used in the Nomadik and ux500.
 *
 * Symbols in this file are called "nmk_gpio" for "nomadik gpio"
 */

#define NMK_GPIO_PER_CHIP	32

struct nmk_gpio_chip {
	struct gpio_chip chip;
	struct irq_domain *domain;
	void __iomem *addr;
	struct clk *clk;
	unsigned int bank;
	unsigned int parent_irq;
	int secondary_parent_irq;
	u32 (*get_secondary_status)(unsigned int bank);
	void (*set_ioforce)(bool enable);
	spinlock_t lock;
	bool sleepmode;
	/* Keep track of configured edges */
	u32 edge_rising;
	u32 edge_falling;
	u32 real_wake;
	u32 rwimsc;
	u32 fwimsc;
	u32 rimsc;
	u32 fimsc;
	u32 pull_up;
	u32 lowemi;
};

struct nmk_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	const struct nmk_pinctrl_soc_data *soc;
};

static struct nmk_gpio_chip *
nmk_gpio_chips[DIV_ROUND_UP(ARCH_NR_GPIOS, NMK_GPIO_PER_CHIP)];

static DEFINE_SPINLOCK(nmk_gpio_slpm_lock);

#define NUM_BANKS ARRAY_SIZE(nmk_gpio_chips)

static void __nmk_gpio_set_mode(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, int gpio_mode)
{
	u32 bit = 1 << offset;
	u32 afunc, bfunc;

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & ~bit;
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & ~bit;
	if (gpio_mode & NMK_GPIO_ALT_A)
		afunc |= bit;
	if (gpio_mode & NMK_GPIO_ALT_B)
		bfunc |= bit;
	writel(afunc, nmk_chip->addr + NMK_GPIO_AFSLA);
	writel(bfunc, nmk_chip->addr + NMK_GPIO_AFSLB);
}

static void __nmk_gpio_set_slpm(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, enum nmk_gpio_slpm mode)
{
	u32 bit = 1 << offset;
	u32 slpm;

	slpm = readl(nmk_chip->addr + NMK_GPIO_SLPC);
	if (mode == NMK_GPIO_SLPM_NOCHANGE)
		slpm |= bit;
	else
		slpm &= ~bit;
	writel(slpm, nmk_chip->addr + NMK_GPIO_SLPC);
}

static void __nmk_gpio_set_pull(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, enum nmk_gpio_pull pull)
{
	u32 bit = 1 << offset;
	u32 pdis;

	pdis = readl(nmk_chip->addr + NMK_GPIO_PDIS);
	if (pull == NMK_GPIO_PULL_NONE) {
		pdis |= bit;
		nmk_chip->pull_up &= ~bit;
	} else {
		pdis &= ~bit;
	}

	writel(pdis, nmk_chip->addr + NMK_GPIO_PDIS);

	if (pull == NMK_GPIO_PULL_UP) {
		nmk_chip->pull_up |= bit;
		writel(bit, nmk_chip->addr + NMK_GPIO_DATS);
	} else if (pull == NMK_GPIO_PULL_DOWN) {
		nmk_chip->pull_up &= ~bit;
		writel(bit, nmk_chip->addr + NMK_GPIO_DATC);
	}
}

static void __nmk_gpio_set_lowemi(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, bool lowemi)
{
	u32 bit = BIT(offset);
	bool enabled = nmk_chip->lowemi & bit;

	if (lowemi == enabled)
		return;

	if (lowemi)
		nmk_chip->lowemi |= bit;
	else
		nmk_chip->lowemi &= ~bit;

	writel_relaxed(nmk_chip->lowemi,
		       nmk_chip->addr + NMK_GPIO_LOWEMI);
}

static void __nmk_gpio_make_input(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset)
{
	writel(1 << offset, nmk_chip->addr + NMK_GPIO_DIRC);
}

static void __nmk_gpio_set_output(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, int val)
{
	if (val)
		writel(1 << offset, nmk_chip->addr + NMK_GPIO_DATS);
	else
		writel(1 << offset, nmk_chip->addr + NMK_GPIO_DATC);
}

static void __nmk_gpio_make_output(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, int val)
{
	writel(1 << offset, nmk_chip->addr + NMK_GPIO_DIRS);
	__nmk_gpio_set_output(nmk_chip, offset, val);
}

static void __nmk_gpio_set_mode_safe(struct nmk_gpio_chip *nmk_chip,
				     unsigned offset, int gpio_mode,
				     bool glitch)
{
	u32 rwimsc = nmk_chip->rwimsc;
	u32 fwimsc = nmk_chip->fwimsc;

	if (glitch && nmk_chip->set_ioforce) {
		u32 bit = BIT(offset);

		/* Prevent spurious wakeups */
		writel(rwimsc & ~bit, nmk_chip->addr + NMK_GPIO_RWIMSC);
		writel(fwimsc & ~bit, nmk_chip->addr + NMK_GPIO_FWIMSC);

		nmk_chip->set_ioforce(true);
	}

	__nmk_gpio_set_mode(nmk_chip, offset, gpio_mode);

	if (glitch && nmk_chip->set_ioforce) {
		nmk_chip->set_ioforce(false);

		writel(rwimsc, nmk_chip->addr + NMK_GPIO_RWIMSC);
		writel(fwimsc, nmk_chip->addr + NMK_GPIO_FWIMSC);
	}
}

static void
nmk_gpio_disable_lazy_irq(struct nmk_gpio_chip *nmk_chip, unsigned offset)
{
	u32 falling = nmk_chip->fimsc & BIT(offset);
	u32 rising = nmk_chip->rimsc & BIT(offset);
	int gpio = nmk_chip->chip.base + offset;
	int irq = NOMADIK_GPIO_TO_IRQ(gpio);
	struct irq_data *d = irq_get_irq_data(irq);

	if (!rising && !falling)
		return;

	if (!d || !irqd_irq_disabled(d))
		return;

	if (rising) {
		nmk_chip->rimsc &= ~BIT(offset);
		writel_relaxed(nmk_chip->rimsc,
			       nmk_chip->addr + NMK_GPIO_RIMSC);
	}

	if (falling) {
		nmk_chip->fimsc &= ~BIT(offset);
		writel_relaxed(nmk_chip->fimsc,
			       nmk_chip->addr + NMK_GPIO_FIMSC);
	}

	dev_dbg(nmk_chip->chip.dev, "%d: clearing interrupt mask\n", gpio);
}

static void __nmk_config_pin(struct nmk_gpio_chip *nmk_chip, unsigned offset,
			     pin_cfg_t cfg, bool sleep, unsigned int *slpmregs)
{
	static const char *afnames[] = {
		[NMK_GPIO_ALT_GPIO]	= "GPIO",
		[NMK_GPIO_ALT_A]	= "A",
		[NMK_GPIO_ALT_B]	= "B",
		[NMK_GPIO_ALT_C]	= "C"
	};
	static const char *pullnames[] = {
		[NMK_GPIO_PULL_NONE]	= "none",
		[NMK_GPIO_PULL_UP]	= "up",
		[NMK_GPIO_PULL_DOWN]	= "down",
		[3] /* illegal */	= "??"
	};
	static const char *slpmnames[] = {
		[NMK_GPIO_SLPM_INPUT]		= "input/wakeup",
		[NMK_GPIO_SLPM_NOCHANGE]	= "no-change/no-wakeup",
	};

	int pin = PIN_NUM(cfg);
	int pull = PIN_PULL(cfg);
	int af = PIN_ALT(cfg);
	int slpm = PIN_SLPM(cfg);
	int output = PIN_DIR(cfg);
	int val = PIN_VAL(cfg);
	bool glitch = af == NMK_GPIO_ALT_C;

	dev_dbg(nmk_chip->chip.dev, "pin %d [%#lx]: af %s, pull %s, slpm %s (%s%s)\n",
		pin, cfg, afnames[af], pullnames[pull], slpmnames[slpm],
		output ? "output " : "input",
		output ? (val ? "high" : "low") : "");

	if (sleep) {
		int slpm_pull = PIN_SLPM_PULL(cfg);
		int slpm_output = PIN_SLPM_DIR(cfg);
		int slpm_val = PIN_SLPM_VAL(cfg);

		af = NMK_GPIO_ALT_GPIO;

		/*
		 * The SLPM_* values are normal values + 1 to allow zero to
		 * mean "same as normal".
		 */
		if (slpm_pull)
			pull = slpm_pull - 1;
		if (slpm_output)
			output = slpm_output - 1;
		if (slpm_val)
			val = slpm_val - 1;

		dev_dbg(nmk_chip->chip.dev, "pin %d: sleep pull %s, dir %s, val %s\n",
			pin,
			slpm_pull ? pullnames[pull] : "same",
			slpm_output ? (output ? "output" : "input") : "same",
			slpm_val ? (val ? "high" : "low") : "same");
	}

	if (output)
		__nmk_gpio_make_output(nmk_chip, offset, val);
	else {
		__nmk_gpio_make_input(nmk_chip, offset);
		__nmk_gpio_set_pull(nmk_chip, offset, pull);
	}

	__nmk_gpio_set_lowemi(nmk_chip, offset, PIN_LOWEMI(cfg));

	/*
	 * If the pin is switching to altfunc, and there was an interrupt
	 * installed on it which has been lazy disabled, actually mask the
	 * interrupt to prevent spurious interrupts that would occur while the
	 * pin is under control of the peripheral.  Only SKE does this.
	 */
	if (af != NMK_GPIO_ALT_GPIO)
		nmk_gpio_disable_lazy_irq(nmk_chip, offset);

	/*
	 * If we've backed up the SLPM registers (glitch workaround), modify
	 * the backups since they will be restored.
	 */
	if (slpmregs) {
		if (slpm == NMK_GPIO_SLPM_NOCHANGE)
			slpmregs[nmk_chip->bank] |= BIT(offset);
		else
			slpmregs[nmk_chip->bank] &= ~BIT(offset);
	} else
		__nmk_gpio_set_slpm(nmk_chip, offset, slpm);

	__nmk_gpio_set_mode_safe(nmk_chip, offset, af, glitch);
}

/*
 * Safe sequence used to switch IOs between GPIO and Alternate-C mode:
 *  - Save SLPM registers
 *  - Set SLPM=0 for the IOs you want to switch and others to 1
 *  - Configure the GPIO registers for the IOs that are being switched
 *  - Set IOFORCE=1
 *  - Modify the AFLSA/B registers for the IOs that are being switched
 *  - Set IOFORCE=0
 *  - Restore SLPM registers
 *  - Any spurious wake up event during switch sequence to be ignored and
 *    cleared
 */
static void nmk_gpio_glitch_slpm_init(unsigned int *slpm)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];
		unsigned int temp = slpm[i];

		if (!chip)
			break;

		clk_enable(chip->clk);

		slpm[i] = readl(chip->addr + NMK_GPIO_SLPC);
		writel(temp, chip->addr + NMK_GPIO_SLPC);
	}
}

static void nmk_gpio_glitch_slpm_restore(unsigned int *slpm)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			break;

		writel(slpm[i], chip->addr + NMK_GPIO_SLPC);

		clk_disable(chip->clk);
	}
}

static int __nmk_config_pins(pin_cfg_t *cfgs, int num, bool sleep)
{
	static unsigned int slpm[NUM_BANKS];
	unsigned long flags;
	bool glitch = false;
	int ret = 0;
	int i;

	for (i = 0; i < num; i++) {
		if (PIN_ALT(cfgs[i]) == NMK_GPIO_ALT_C) {
			glitch = true;
			break;
		}
	}

	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);

	if (glitch) {
		memset(slpm, 0xff, sizeof(slpm));

		for (i = 0; i < num; i++) {
			int pin = PIN_NUM(cfgs[i]);
			int offset = pin % NMK_GPIO_PER_CHIP;

			if (PIN_ALT(cfgs[i]) == NMK_GPIO_ALT_C)
				slpm[pin / NMK_GPIO_PER_CHIP] &= ~BIT(offset);
		}

		nmk_gpio_glitch_slpm_init(slpm);
	}

	for (i = 0; i < num; i++) {
		struct nmk_gpio_chip *nmk_chip;
		int pin = PIN_NUM(cfgs[i]);

		nmk_chip = nmk_gpio_chips[pin / NMK_GPIO_PER_CHIP];
		if (!nmk_chip) {
			ret = -EINVAL;
			break;
		}

		clk_enable(nmk_chip->clk);
		spin_lock(&nmk_chip->lock);
		__nmk_config_pin(nmk_chip, pin % NMK_GPIO_PER_CHIP,
				 cfgs[i], sleep, glitch ? slpm : NULL);
		spin_unlock(&nmk_chip->lock);
		clk_disable(nmk_chip->clk);
	}

	if (glitch)
		nmk_gpio_glitch_slpm_restore(slpm);

	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);

	return ret;
}

/**
 * nmk_config_pin - configure a pin's mux attributes
 * @cfg: pin confguration
 *
 * Configures a pin's mode (alternate function or GPIO), its pull up status,
 * and its sleep mode based on the specified configuration.  The @cfg is
 * usually one of the SoC specific macros defined in mach/<soc>-pins.h.  These
 * are constructed using, and can be further enhanced with, the macros in
 * plat/pincfg.h.
 *
 * If a pin's mode is set to GPIO, it is configured as an input to avoid
 * side-effects.  The gpio can be manipulated later using standard GPIO API
 * calls.
 */
int nmk_config_pin(pin_cfg_t cfg, bool sleep)
{
	return __nmk_config_pins(&cfg, 1, sleep);
}
EXPORT_SYMBOL(nmk_config_pin);

/**
 * nmk_config_pins - configure several pins at once
 * @cfgs: array of pin configurations
 * @num: number of elments in the array
 *
 * Configures several pins using nmk_config_pin().  Refer to that function for
 * further information.
 */
int nmk_config_pins(pin_cfg_t *cfgs, int num)
{
	return __nmk_config_pins(cfgs, num, false);
}
EXPORT_SYMBOL(nmk_config_pins);

int nmk_config_pins_sleep(pin_cfg_t *cfgs, int num)
{
	return __nmk_config_pins(cfgs, num, true);
}
EXPORT_SYMBOL(nmk_config_pins_sleep);

/**
 * nmk_gpio_set_slpm() - configure the sleep mode of a pin
 * @gpio: pin number
 * @mode: NMK_GPIO_SLPM_INPUT or NMK_GPIO_SLPM_NOCHANGE,
 *
 * This register is actually in the pinmux layer, not the GPIO block itself.
 * The GPIO1B_SLPM register defines the GPIO mode when SLEEP/DEEP-SLEEP
 * mode is entered (i.e. when signal IOFORCE is HIGH by the platform code).
 * Each GPIO can be configured to be forced into GPIO mode when IOFORCE is
 * HIGH, overriding the normal setting defined by GPIO_AFSELx registers.
 * When IOFORCE returns LOW (by software, after SLEEP/DEEP-SLEEP exit),
 * the GPIOs return to the normal setting defined by GPIO_AFSELx registers.
 *
 * If @mode is NMK_GPIO_SLPM_INPUT, the corresponding GPIO is switched to GPIO
 * mode when signal IOFORCE is HIGH (i.e. when SLEEP/DEEP-SLEEP mode is
 * entered) regardless of the altfunction selected. Also wake-up detection is
 * ENABLED.
 *
 * If @mode is NMK_GPIO_SLPM_NOCHANGE, the corresponding GPIO remains
 * controlled by NMK_GPIO_DATC, NMK_GPIO_DATS, NMK_GPIO_DIR, NMK_GPIO_PDIS
 * (for altfunction GPIO) or respective on-chip peripherals (for other
 * altfuncs) when IOFORCE is HIGH. Also wake-up detection DISABLED.
 *
 * Note that enable_irq_wake() will automatically enable wakeup detection.
 */
int nmk_gpio_set_slpm(int gpio, enum nmk_gpio_slpm mode)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = nmk_gpio_chips[gpio / NMK_GPIO_PER_CHIP];
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	__nmk_gpio_set_slpm(nmk_chip, gpio % NMK_GPIO_PER_CHIP, mode);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

/**
 * nmk_gpio_set_pull() - enable/disable pull up/down on a gpio
 * @gpio: pin number
 * @pull: one of NMK_GPIO_PULL_DOWN, NMK_GPIO_PULL_UP, and NMK_GPIO_PULL_NONE
 *
 * Enables/disables pull up/down on a specified pin.  This only takes effect if
 * the pin is configured as an input (either explicitly or by the alternate
 * function).
 *
 * NOTE: If enabling the pull up/down, the caller must ensure that the GPIO is
 * configured as an input.  Otherwise, due to the way the controller registers
 * work, this function will change the value output on the pin.
 */
int nmk_gpio_set_pull(int gpio, enum nmk_gpio_pull pull)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = nmk_gpio_chips[gpio / NMK_GPIO_PER_CHIP];
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_chip->lock, flags);
	__nmk_gpio_set_pull(nmk_chip, gpio % NMK_GPIO_PER_CHIP, pull);
	spin_unlock_irqrestore(&nmk_chip->lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

/* Mode functions */
/**
 * nmk_gpio_set_mode() - set the mux mode of a gpio pin
 * @gpio: pin number
 * @gpio_mode: one of NMK_GPIO_ALT_GPIO, NMK_GPIO_ALT_A,
 *	       NMK_GPIO_ALT_B, and NMK_GPIO_ALT_C
 *
 * Sets the mode of the specified pin to one of the alternate functions or
 * plain GPIO.
 */
int nmk_gpio_set_mode(int gpio, int gpio_mode)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = nmk_gpio_chips[gpio / NMK_GPIO_PER_CHIP];
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_chip->lock, flags);
	__nmk_gpio_set_mode(nmk_chip, gpio % NMK_GPIO_PER_CHIP, gpio_mode);
	spin_unlock_irqrestore(&nmk_chip->lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}
EXPORT_SYMBOL(nmk_gpio_set_mode);

int nmk_gpio_get_mode(int gpio)
{
	struct nmk_gpio_chip *nmk_chip;
	u32 afunc, bfunc, bit;

	nmk_chip = nmk_gpio_chips[gpio / NMK_GPIO_PER_CHIP];
	if (!nmk_chip)
		return -EINVAL;

	bit = 1 << (gpio % NMK_GPIO_PER_CHIP);

	clk_enable(nmk_chip->clk);

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & bit;
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & bit;

	clk_disable(nmk_chip->clk);

	return (afunc ? NMK_GPIO_ALT_A : 0) | (bfunc ? NMK_GPIO_ALT_B : 0);
}
EXPORT_SYMBOL(nmk_gpio_get_mode);


/* IRQ functions */
static inline int nmk_gpio_get_bitmask(int gpio)
{
	return 1 << (gpio % NMK_GPIO_PER_CHIP);
}

static void nmk_gpio_irq_ack(struct irq_data *d)
{
	struct nmk_gpio_chip *nmk_chip;

	nmk_chip = irq_data_get_irq_chip_data(d);
	if (!nmk_chip)
		return;

	clk_enable(nmk_chip->clk);
	writel(nmk_gpio_get_bitmask(d->hwirq), nmk_chip->addr + NMK_GPIO_IC);
	clk_disable(nmk_chip->clk);
}

enum nmk_gpio_irq_type {
	NORMAL,
	WAKE,
};

static void __nmk_gpio_irq_modify(struct nmk_gpio_chip *nmk_chip,
				  int gpio, enum nmk_gpio_irq_type which,
				  bool enable)
{
	u32 bitmask = nmk_gpio_get_bitmask(gpio);
	u32 *rimscval;
	u32 *fimscval;
	u32 rimscreg;
	u32 fimscreg;

	if (which == NORMAL) {
		rimscreg = NMK_GPIO_RIMSC;
		fimscreg = NMK_GPIO_FIMSC;
		rimscval = &nmk_chip->rimsc;
		fimscval = &nmk_chip->fimsc;
	} else  {
		rimscreg = NMK_GPIO_RWIMSC;
		fimscreg = NMK_GPIO_FWIMSC;
		rimscval = &nmk_chip->rwimsc;
		fimscval = &nmk_chip->fwimsc;
	}

	/* we must individually set/clear the two edges */
	if (nmk_chip->edge_rising & bitmask) {
		if (enable)
			*rimscval |= bitmask;
		else
			*rimscval &= ~bitmask;
		writel(*rimscval, nmk_chip->addr + rimscreg);
	}
	if (nmk_chip->edge_falling & bitmask) {
		if (enable)
			*fimscval |= bitmask;
		else
			*fimscval &= ~bitmask;
		writel(*fimscval, nmk_chip->addr + fimscreg);
	}
}

static void __nmk_gpio_set_wake(struct nmk_gpio_chip *nmk_chip,
				int gpio, bool on)
{
	/*
	 * Ensure WAKEUP_ENABLE is on.  No need to disable it if wakeup is
	 * disabled, since setting SLPM to 1 increases power consumption, and
	 * wakeup is anyhow controlled by the RIMSC and FIMSC registers.
	 */
	if (nmk_chip->sleepmode && on) {
		__nmk_gpio_set_slpm(nmk_chip, gpio % nmk_chip->chip.base,
				    NMK_GPIO_SLPM_WAKEUP_ENABLE);
	}

	__nmk_gpio_irq_modify(nmk_chip, gpio, WAKE, on);
}

static int nmk_gpio_irq_maskunmask(struct irq_data *d, bool enable)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask;

	nmk_chip = irq_data_get_irq_chip_data(d);
	bitmask = nmk_gpio_get_bitmask(d->hwirq);
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, enable);

	if (!(nmk_chip->real_wake & bitmask))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, enable);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static void nmk_gpio_irq_mask(struct irq_data *d)
{
	nmk_gpio_irq_maskunmask(d, false);
}

static void nmk_gpio_irq_unmask(struct irq_data *d)
{
	nmk_gpio_irq_maskunmask(d, true);
}

static int nmk_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask;

	nmk_chip = irq_data_get_irq_chip_data(d);
	if (!nmk_chip)
		return -EINVAL;
	bitmask = nmk_gpio_get_bitmask(d->hwirq);

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	if (irqd_irq_disabled(d))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, on);

	if (on)
		nmk_chip->real_wake |= bitmask;
	else
		nmk_chip->real_wake &= ~bitmask;

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	bool enabled = !irqd_irq_disabled(d);
	bool wake = irqd_is_wakeup_set(d);
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask;

	nmk_chip = irq_data_get_irq_chip_data(d);
	bitmask = nmk_gpio_get_bitmask(d->hwirq);
	if (!nmk_chip)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_chip->lock, flags);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, false);

	if (enabled || wake)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, false);

	nmk_chip->edge_rising &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_RISING)
		nmk_chip->edge_rising |= bitmask;

	nmk_chip->edge_falling &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		nmk_chip->edge_falling |= bitmask;

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, true);

	if (enabled || wake)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, true);

	spin_unlock_irqrestore(&nmk_chip->lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static unsigned int nmk_gpio_irq_startup(struct irq_data *d)
{
	struct nmk_gpio_chip *nmk_chip = irq_data_get_irq_chip_data(d);

	clk_enable(nmk_chip->clk);
	nmk_gpio_irq_unmask(d);
	return 0;
}

static void nmk_gpio_irq_shutdown(struct irq_data *d)
{
	struct nmk_gpio_chip *nmk_chip = irq_data_get_irq_chip_data(d);

	nmk_gpio_irq_mask(d);
	clk_disable(nmk_chip->clk);
}

static struct irq_chip nmk_gpio_irq_chip = {
	.name		= "Nomadik-GPIO",
	.irq_ack	= nmk_gpio_irq_ack,
	.irq_mask	= nmk_gpio_irq_mask,
	.irq_unmask	= nmk_gpio_irq_unmask,
	.irq_set_type	= nmk_gpio_irq_set_type,
	.irq_set_wake	= nmk_gpio_irq_set_wake,
	.irq_startup	= nmk_gpio_irq_startup,
	.irq_shutdown	= nmk_gpio_irq_shutdown,
};

static void __nmk_gpio_irq_handler(unsigned int irq, struct irq_desc *desc,
				   u32 status)
{
	struct nmk_gpio_chip *nmk_chip;
	struct irq_chip *host_chip = irq_get_chip(irq);
	unsigned int first_irq;

	chained_irq_enter(host_chip, desc);

	nmk_chip = irq_get_handler_data(irq);
	first_irq = nmk_chip->domain->revmap_data.legacy.first_irq;
	while (status) {
		int bit = __ffs(status);

		generic_handle_irq(first_irq + bit);
		status &= ~BIT(bit);
	}

	chained_irq_exit(host_chip, desc);
}

static void nmk_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct nmk_gpio_chip *nmk_chip = irq_get_handler_data(irq);
	u32 status;

	clk_enable(nmk_chip->clk);
	status = readl(nmk_chip->addr + NMK_GPIO_IS);
	clk_disable(nmk_chip->clk);

	__nmk_gpio_irq_handler(irq, desc, status);
}

static void nmk_gpio_secondary_irq_handler(unsigned int irq,
					   struct irq_desc *desc)
{
	struct nmk_gpio_chip *nmk_chip = irq_get_handler_data(irq);
	u32 status = nmk_chip->get_secondary_status(nmk_chip->bank);

	__nmk_gpio_irq_handler(irq, desc, status);
}

static int nmk_gpio_init_irq(struct nmk_gpio_chip *nmk_chip)
{
	irq_set_chained_handler(nmk_chip->parent_irq, nmk_gpio_irq_handler);
	irq_set_handler_data(nmk_chip->parent_irq, nmk_chip);

	if (nmk_chip->secondary_parent_irq >= 0) {
		irq_set_chained_handler(nmk_chip->secondary_parent_irq,
					nmk_gpio_secondary_irq_handler);
		irq_set_handler_data(nmk_chip->secondary_parent_irq, nmk_chip);
	}

	return 0;
}

/* I/O Functions */
static int nmk_gpio_make_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	clk_enable(nmk_chip->clk);

	writel(1 << offset, nmk_chip->addr + NMK_GPIO_DIRC);

	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_get_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);
	u32 bit = 1 << offset;
	int value;

	clk_enable(nmk_chip->clk);

	value = (readl(nmk_chip->addr + NMK_GPIO_DAT) & bit) != 0;

	clk_disable(nmk_chip->clk);

	return value;
}

static void nmk_gpio_set_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_set_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);
}

static int nmk_gpio_make_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_make_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	return irq_find_mapping(nmk_chip->domain, offset);
}

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

static void nmk_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	int mode;
	unsigned		i;
	unsigned		gpio = chip->base;
	int			is_out;
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);
	const char *modes[] = {
		[NMK_GPIO_ALT_GPIO]	= "gpio",
		[NMK_GPIO_ALT_A]	= "altA",
		[NMK_GPIO_ALT_B]	= "altB",
		[NMK_GPIO_ALT_C]	= "altC",
	};

	clk_enable(nmk_chip->clk);

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		const char *label = gpiochip_is_requested(chip, i);
		bool pull;
		u32 bit = 1 << i;

		is_out = readl(nmk_chip->addr + NMK_GPIO_DIR) & bit;
		pull = !(readl(nmk_chip->addr + NMK_GPIO_PDIS) & bit);
		mode = nmk_gpio_get_mode(gpio);
		seq_printf(s, " gpio-%-3d (%-20.20s) %s %s %s %s",
			gpio, label ?: "(none)",
			is_out ? "out" : "in ",
			chip->get
				? (chip->get(chip, i) ? "hi" : "lo")
				: "?  ",
			(mode < 0) ? "unknown" : modes[mode],
			pull ? "pull" : "none");

		if (label && !is_out) {
			int		irq = gpio_to_irq(gpio);
			struct irq_desc	*desc = irq_to_desc(irq);

			/* This races with request_irq(), set_irq_type(),
			 * and set_irq_wake() ... but those are "rare".
			 */
			if (irq >= 0 && desc->action) {
				char *trigger;
				u32 bitmask = nmk_gpio_get_bitmask(gpio);

				if (nmk_chip->edge_rising & bitmask)
					trigger = "edge-rising";
				else if (nmk_chip->edge_falling & bitmask)
					trigger = "edge-falling";
				else
					trigger = "edge-undefined";

				seq_printf(s, " irq-%d %s%s",
					irq, trigger,
					irqd_is_wakeup_set(&desc->irq_data)
						? " wakeup" : "");
			}
		}

		seq_printf(s, "\n");
	}

	clk_disable(nmk_chip->clk);
}

#else
#define nmk_gpio_dbg_show	NULL
#endif

/* This structure is replicated for each GPIO block allocated at probe time */
static struct gpio_chip nmk_gpio_template = {
	.direction_input	= nmk_gpio_make_input,
	.get			= nmk_gpio_get_input,
	.direction_output	= nmk_gpio_make_output,
	.set			= nmk_gpio_set_output,
	.to_irq			= nmk_gpio_to_irq,
	.dbg_show		= nmk_gpio_dbg_show,
	.can_sleep		= 0,
};

void nmk_gpio_clocks_enable(void)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			continue;

		clk_enable(chip->clk);
	}
}

void nmk_gpio_clocks_disable(void)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			continue;

		clk_disable(chip->clk);
	}
}

/*
 * Called from the suspend/resume path to only keep the real wakeup interrupts
 * (those that have had set_irq_wake() called on them) as wakeup interrupts,
 * and not the rest of the interrupts which we needed to have as wakeups for
 * cpuidle.
 *
 * PM ops are not used since this needs to be done at the end, after all the
 * other drivers are done with their suspend callbacks.
 */
void nmk_gpio_wakeups_suspend(void)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			break;

		clk_enable(chip->clk);

		writel(chip->rwimsc & chip->real_wake,
		       chip->addr + NMK_GPIO_RWIMSC);
		writel(chip->fwimsc & chip->real_wake,
		       chip->addr + NMK_GPIO_FWIMSC);

		clk_disable(chip->clk);
	}
}

void nmk_gpio_wakeups_resume(void)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			break;

		clk_enable(chip->clk);

		writel(chip->rwimsc, chip->addr + NMK_GPIO_RWIMSC);
		writel(chip->fwimsc, chip->addr + NMK_GPIO_FWIMSC);

		clk_disable(chip->clk);
	}
}

/*
 * Read the pull up/pull down status.
 * A bit set in 'pull_up' means that pull up
 * is selected if pull is enabled in PDIS register.
 * Note: only pull up/down set via this driver can
 * be detected due to HW limitations.
 */
void nmk_gpio_read_pull(int gpio_bank, u32 *pull_up)
{
	if (gpio_bank < NUM_BANKS) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[gpio_bank];

		if (!chip)
			return;

		*pull_up = chip->pull_up;
	}
}

int nmk_gpio_irq_map(struct irq_domain *d, unsigned int irq,
			  irq_hw_number_t hwirq)
{
	struct nmk_gpio_chip *nmk_chip = d->host_data;

	if (!nmk_chip)
		return -EINVAL;

	irq_set_chip_and_handler(irq, &nmk_gpio_irq_chip, handle_edge_irq);
	set_irq_flags(irq, IRQF_VALID);
	irq_set_chip_data(irq, nmk_chip);
	irq_set_irq_type(irq, IRQ_TYPE_EDGE_FALLING);

	return 0;
}

const struct irq_domain_ops nmk_gpio_irq_simple_ops = {
	.map = nmk_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

static int __devinit nmk_gpio_probe(struct platform_device *dev)
{
	struct nmk_gpio_platform_data *pdata = dev->dev.platform_data;
	struct device_node *np = dev->dev.of_node;
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	struct resource *res;
	struct clk *clk;
	int secondary_irq;
	void __iomem *base;
	int irq;
	int ret;

	if (!pdata && !np) {
		dev_err(&dev->dev, "No platform data or device tree found\n");
		return -ENODEV;
	}

	if (np) {
		pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		if (of_get_property(np, "supports-sleepmode", NULL))
			pdata->supports_sleepmode = true;

		if (of_property_read_u32(np, "gpio-bank", &dev->id)) {
			dev_err(&dev->dev, "gpio-bank property not found\n");
			ret = -EINVAL;
			goto out;
		}

		pdata->first_gpio = dev->id * NMK_GPIO_PER_CHIP;
		pdata->num_gpio   = NMK_GPIO_PER_CHIP;
	}

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto out;
	}

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		ret = irq;
		goto out;
	}

	secondary_irq = platform_get_irq(dev, 1);
	if (secondary_irq >= 0 && !pdata->get_secondary_status) {
		ret = -EINVAL;
		goto out;
	}

	if (request_mem_region(res->start, resource_size(res),
			       dev_name(&dev->dev)) == NULL) {
		ret = -EBUSY;
		goto out;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		ret = -ENOMEM;
		goto out_release;
	}

	clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_unmap;
	}

	nmk_chip = kzalloc(sizeof(*nmk_chip), GFP_KERNEL);
	if (!nmk_chip) {
		ret = -ENOMEM;
		goto out_clk;
	}

	/*
	 * The virt address in nmk_chip->addr is in the nomadik register space,
	 * so we can simply convert the resource address, without remapping
	 */
	nmk_chip->bank = dev->id;
	nmk_chip->clk = clk;
	nmk_chip->addr = base;
	nmk_chip->chip = nmk_gpio_template;
	nmk_chip->parent_irq = irq;
	nmk_chip->secondary_parent_irq = secondary_irq;
	nmk_chip->get_secondary_status = pdata->get_secondary_status;
	nmk_chip->set_ioforce = pdata->set_ioforce;
	nmk_chip->sleepmode = pdata->supports_sleepmode;
	spin_lock_init(&nmk_chip->lock);

	chip = &nmk_chip->chip;
	chip->base = pdata->first_gpio;
	chip->ngpio = pdata->num_gpio;
	chip->label = pdata->name ?: dev_name(&dev->dev);
	chip->dev = &dev->dev;
	chip->owner = THIS_MODULE;

	clk_enable(nmk_chip->clk);
	nmk_chip->lowemi = readl_relaxed(nmk_chip->addr + NMK_GPIO_LOWEMI);
	clk_disable(nmk_chip->clk);

#ifdef CONFIG_OF_GPIO
	chip->of_node = np;
#endif

	ret = gpiochip_add(&nmk_chip->chip);
	if (ret)
		goto out_free;

	BUG_ON(nmk_chip->bank >= ARRAY_SIZE(nmk_gpio_chips));

	nmk_gpio_chips[nmk_chip->bank] = nmk_chip;

	platform_set_drvdata(dev, nmk_chip);

	nmk_chip->domain = irq_domain_add_legacy(np, NMK_GPIO_PER_CHIP,
						NOMADIK_GPIO_TO_IRQ(pdata->first_gpio),
						0, &nmk_gpio_irq_simple_ops, nmk_chip);
	if (!nmk_chip->domain) {
		pr_err("%s: Failed to create irqdomain\n", np->full_name);
		ret = -ENOSYS;
		goto out_free;
	}

	nmk_gpio_init_irq(nmk_chip);

	dev_info(&dev->dev, "at address %p\n", nmk_chip->addr);

	return 0;

out_free:
	kfree(nmk_chip);
out_clk:
	clk_disable(clk);
	clk_put(clk);
out_unmap:
	iounmap(base);
out_release:
	release_mem_region(res->start, resource_size(res));
out:
	dev_err(&dev->dev, "Failure %i for GPIO %i-%i\n", ret,
		  pdata->first_gpio, pdata->first_gpio+31);
	if (np)
		kfree(pdata);

	return ret;
}

static int nmk_get_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->ngroups;
}

static const char *nmk_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->groups[selector].name;
}

static int nmk_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	*pins = npct->soc->groups[selector].pins;
	*num_pins = npct->soc->groups[selector].npins;
	return 0;
}

static void nmk_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " Nomadik GPIO");
}

static struct pinctrl_ops nmk_pinctrl_ops = {
	.get_groups_count = nmk_get_groups_cnt,
	.get_group_name = nmk_get_group_name,
	.get_group_pins = nmk_get_group_pins,
	.pin_dbg_show = nmk_pin_dbg_show,
};

static struct pinctrl_desc nmk_pinctrl_desc = {
	.name = "pinctrl-nomadik",
	.pctlops = &nmk_pinctrl_ops,
	.owner = THIS_MODULE,
};

static int __devinit nmk_pinctrl_probe(struct platform_device *pdev)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);
	struct nmk_pinctrl *npct;
	int i;

	npct = devm_kzalloc(&pdev->dev, sizeof(*npct), GFP_KERNEL);
	if (!npct)
		return -ENOMEM;

	/* Poke in other ASIC variants here */
	if (platid->driver_data == PINCTRL_NMK_DB8500)
		nmk_pinctrl_db8500_init(&npct->soc);

	/*
	 * We need all the GPIO drivers to probe FIRST, or we will not be able
	 * to obtain references to the struct gpio_chip * for them, and we
	 * need this to proceed.
	 */
	for (i = 0; i < npct->soc->gpio_num_ranges; i++) {
		if (!nmk_gpio_chips[i]) {
			dev_warn(&pdev->dev, "GPIO chip %d not registered yet\n", i);
			devm_kfree(&pdev->dev, npct);
			return -EPROBE_DEFER;
		}
		npct->soc->gpio_ranges[i].gc = &nmk_gpio_chips[i]->chip;
	}

	nmk_pinctrl_desc.pins = npct->soc->pins;
	nmk_pinctrl_desc.npins = npct->soc->npins;
	npct->dev = &pdev->dev;
	npct->pctl = pinctrl_register(&nmk_pinctrl_desc, &pdev->dev, npct);
	if (!npct->pctl) {
		dev_err(&pdev->dev, "could not register Nomadik pinctrl driver\n");
		return -EINVAL;
	}

	/* We will handle a range of GPIO pins */
	for (i = 0; i < npct->soc->gpio_num_ranges; i++)
		pinctrl_add_gpio_range(npct->pctl, &npct->soc->gpio_ranges[i]);

	platform_set_drvdata(pdev, npct);
	dev_info(&pdev->dev, "initialized Nomadik pin control driver\n");

	return 0;
}

static const struct of_device_id nmk_gpio_match[] = {
	{ .compatible = "st,nomadik-gpio", },
	{}
};

static struct platform_driver nmk_gpio_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "gpio",
		.of_match_table = nmk_gpio_match,
	},
	.probe = nmk_gpio_probe,
};

static const struct platform_device_id nmk_pinctrl_id[] = {
	{ "pinctrl-stn8815", PINCTRL_NMK_STN8815 },
	{ "pinctrl-db8500", PINCTRL_NMK_DB8500 },
};

static struct platform_driver nmk_pinctrl_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "pinctrl-nomadik",
	},
	.probe = nmk_pinctrl_probe,
	.id_table = nmk_pinctrl_id,
};

static int __init nmk_gpio_init(void)
{
	int ret;

	ret = platform_driver_register(&nmk_gpio_driver);
	if (ret)
		return ret;
	return platform_driver_register(&nmk_pinctrl_driver);
}

core_initcall(nmk_gpio_init);

MODULE_AUTHOR("Prafulla WADASKAR and Alessandro Rubini");
MODULE_DESCRIPTION("Nomadik GPIO Driver");
MODULE_LICENSE("GPL");
