/*
 * linux/arch/arm/mach-at91rm9200/clock.c
 *
 * Copyright (C) 2005 David Brownell
 * Copyright (C) 2005 Ivan Kokshaysky
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/mach-types.h>

#include <asm/hardware.h>

#include "generic.h"


/*
 * There's a lot more which can be done with clocks, including cpufreq
 * integration, slow clock mode support (for system suspend), letting
 * PLLB be used at other rates (on boards that don't need USB), etc.
 */

struct clk {
	const char	*name;		/* unique clock name */
	const char	*function;	/* function of the clock */
	struct device	*dev;		/* device associated with function */
	unsigned long	rate_hz;
	struct clk	*parent;
	u32		pmc_mask;
	void		(*mode)(struct clk *, int);
	unsigned	id:2;		/* PCK0..3, or 32k/main/a/b */
	unsigned	primary:1;
	unsigned	pll:1;
	unsigned	programmable:1;
	u16		users;
};

static spinlock_t	clk_lock;
static u32		at91_pllb_usb_init;

/*
 * Four primary clock sources:  two crystal oscillators (32K, main), and
 * two PLLs.  PLLA usually runs the master clock; and PLLB must run at
 * 48 MHz (unless no USB function clocks are needed).  The main clock and
 * both PLLs are turned off to run in "slow clock mode" (system suspend).
 */
static struct clk clk32k = {
	.name		= "clk32k",
	.rate_hz	= AT91_SLOW_CLOCK,
	.users		= 1,		/* always on */
	.id		= 0,
	.primary	= 1,
};
static struct clk main_clk = {
	.name		= "main",
	.pmc_mask	= AT91_PMC_MOSCS,	/* in PMC_SR */
	.id		= 1,
	.primary	= 1,
};
static struct clk plla = {
	.name		= "plla",
	.parent		= &main_clk,
	.pmc_mask	= AT91_PMC_LOCKA,	/* in PMC_SR */
	.id		= 2,
	.primary	= 1,
	.pll		= 1,
};

static void pllb_mode(struct clk *clk, int is_on)
{
	u32	value;

	if (is_on) {
		is_on = AT91_PMC_LOCKB;
		value = at91_pllb_usb_init;
	} else
		value = 0;

	at91_sys_write(AT91_CKGR_PLLBR, value);

	do {
		cpu_relax();
	} while ((at91_sys_read(AT91_PMC_SR) & AT91_PMC_LOCKB) != is_on);
}

static struct clk pllb = {
	.name		= "pllb",
	.parent		= &main_clk,
	.pmc_mask	= AT91_PMC_LOCKB,	/* in PMC_SR */
	.mode		= pllb_mode,
	.id		= 3,
	.primary	= 1,
	.pll		= 1,
};

static void pmc_sys_mode(struct clk *clk, int is_on)
{
	if (is_on)
		at91_sys_write(AT91_PMC_SCER, clk->pmc_mask);
	else
		at91_sys_write(AT91_PMC_SCDR, clk->pmc_mask);
}

/* USB function clocks (PLLB must be 48 MHz) */
static struct clk udpck = {
	.name		= "udpck",
	.parent		= &pllb,
	.pmc_mask	= AT91_PMC_UDP,
	.mode		= pmc_sys_mode,
};
static struct clk uhpck = {
	.name		= "uhpck",
	.parent		= &pllb,
	.pmc_mask	= AT91_PMC_UHP,
	.mode		= pmc_sys_mode,
};

#ifdef CONFIG_AT91_PROGRAMMABLE_CLOCKS
/*
 * The four programmable clocks can be parented by any primary clock.
 * You must configure pin multiplexing to bring these signals out.
 */
static struct clk pck0 = {
	.name		= "pck0",
	.pmc_mask	= AT91_PMC_PCK0,
	.mode		= pmc_sys_mode,
	.programmable	= 1,
	.id		= 0,
};
static struct clk pck1 = {
	.name		= "pck1",
	.pmc_mask	= AT91_PMC_PCK1,
	.mode		= pmc_sys_mode,
	.programmable	= 1,
	.id		= 1,
};
static struct clk pck2 = {
	.name		= "pck2",
	.pmc_mask	= AT91_PMC_PCK2,
	.mode		= pmc_sys_mode,
	.programmable	= 1,
	.id		= 2,
};
static struct clk pck3 = {
	.name		= "pck3",
	.pmc_mask	= AT91_PMC_PCK3,
	.mode		= pmc_sys_mode,
	.programmable	= 1,
	.id		= 3,
};
#endif	/* CONFIG_AT91_PROGRAMMABLE_CLOCKS */


/*
 * The master clock is divided from the CPU clock (by 1-4).  It's used for
 * memory, interfaces to on-chip peripherals, the AIC, and sometimes more
 * (e.g baud rate generation).  It's sourced from one of the primary clocks.
 */
static struct clk mck = {
	.name		= "mck",
	.pmc_mask	= AT91_PMC_MCKRDY,	/* in PMC_SR */
};

static void pmc_periph_mode(struct clk *clk, int is_on)
{
	if (is_on)
		at91_sys_write(AT91_PMC_PCER, clk->pmc_mask);
	else
		at91_sys_write(AT91_PMC_PCDR, clk->pmc_mask);
}

static struct clk udc_clk = {
	.name		= "udc_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_UDP,
	.mode		= pmc_periph_mode,
};
static struct clk ohci_clk = {
	.name		= "ohci_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_UHP,
	.mode		= pmc_periph_mode,
};
static struct clk ether_clk = {
	.name		= "ether_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_EMAC,
	.mode		= pmc_periph_mode,
};
static struct clk mmc_clk = {
	.name		= "mci_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_MCI,
	.mode		= pmc_periph_mode,
};
static struct clk twi_clk = {
	.name		= "twi_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_TWI,
	.mode		= pmc_periph_mode,
};
static struct clk usart0_clk = {
	.name		= "usart0_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_US0,
	.mode		= pmc_periph_mode,
};
static struct clk usart1_clk = {
	.name		= "usart1_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_US1,
	.mode		= pmc_periph_mode,
};
static struct clk usart2_clk = {
	.name		= "usart2_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_US2,
	.mode		= pmc_periph_mode,
};
static struct clk usart3_clk = {
	.name		= "usart3_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_US3,
	.mode		= pmc_periph_mode,
};
static struct clk spi_clk = {
	.name		= "spi0_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_SPI,
	.mode		= pmc_periph_mode,
};
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_PIOA,
	.mode		= pmc_periph_mode,
};
static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_PIOB,
	.mode		= pmc_periph_mode,
};
static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_PIOC,
	.mode		= pmc_periph_mode,
};
static struct clk pioD_clk = {
	.name		= "pioD_clk",
	.parent		= &mck,
	.pmc_mask	= 1 << AT91_ID_PIOD,
	.mode		= pmc_periph_mode,
};

static struct clk *const clock_list[] = {
	/* four primary clocks -- MUST BE FIRST! */
	&clk32k,
	&main_clk,
	&plla,
	&pllb,

	/* PLLB children (USB) */
	&udpck,
	&uhpck,

#ifdef CONFIG_AT91_PROGRAMMABLE_CLOCKS
	/* programmable clocks */
	&pck0,
	&pck1,
	&pck2,
	&pck3,
#endif	/* CONFIG_AT91_PROGRAMMABLE_CLOCKS */

	/* MCK and peripherals */
	&mck,
	&usart0_clk,
	&usart1_clk,
	&usart2_clk,
	&usart3_clk,
	&mmc_clk,
	&udc_clk,
	&twi_clk,
	&spi_clk,
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&pioD_clk,
	// ssc0..ssc2
	// tc0..tc5
	// irq0..irq6
	&ohci_clk,
	&ether_clk,
};


/*
 * Associate a particular clock with a function (eg, "uart") and device.
 * The drivers can then request the same 'function' with several different
 * devices and not care about which clock name to use.
 */
void __init at91_clock_associate(const char *id, struct device *dev, const char *func)
{
	struct clk *clk = clk_get(NULL, id);

	if (!dev || !clk || !IS_ERR(clk_get(dev, func)))
		return;

	clk->function = func;
	clk->dev = dev;
}

/* clocks are all static for now; no refcounting necessary */
struct clk *clk_get(struct device *dev, const char *id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clock_list); i++) {
		struct clk *clk = clock_list[i];

		if (strcmp(id, clk->name) == 0)
			return clk;
		if (clk->function && (dev == clk->dev) && strcmp(id, clk->function) == 0)
			return clk;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

static void __clk_enable(struct clk *clk)
{
	if (clk->parent)
		__clk_enable(clk->parent);
	if (clk->users++ == 0 && clk->mode)
		clk->mode(clk, 1);
}

int clk_enable(struct clk *clk)
{
	unsigned long	flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_enable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	BUG_ON(clk->users == 0);
	if (--clk->users == 0 && clk->mode)
		clk->mode(clk, 0);
	if (clk->parent)
		__clk_disable(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long	flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long	flags;
	unsigned long	rate;

	spin_lock_irqsave(&clk_lock, flags);
	for (;;) {
		rate = clk->rate_hz;
		if (rate || !clk->parent)
			break;
		clk = clk->parent;
	}
	spin_unlock_irqrestore(&clk_lock, flags);
	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

/*------------------------------------------------------------------------*/

#ifdef CONFIG_AT91_PROGRAMMABLE_CLOCKS

/*
 * For now, only the programmable clocks support reparenting (MCK could
 * do this too, with care) or rate changing (the PLLs could do this too,
 * ditto MCK but that's more for cpufreq).  Drivers may reparent to get
 * a better rate match; we don't.
 */

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long	flags;
	unsigned	prescale;
	unsigned long	actual;

	if (!clk->programmable)
		return -EINVAL;
	spin_lock_irqsave(&clk_lock, flags);

	actual = clk->parent->rate_hz;
	for (prescale = 0; prescale < 7; prescale++) {
		if (actual && actual <= rate)
			break;
		actual >>= 1;
	}

	spin_unlock_irqrestore(&clk_lock, flags);
	return (prescale < 7) ? actual : -ENOENT;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long	flags;
	unsigned	prescale;
	unsigned long	actual;

	if (!clk->programmable)
		return -EINVAL;
	if (clk->users)
		return -EBUSY;
	spin_lock_irqsave(&clk_lock, flags);

	actual = clk->parent->rate_hz;
	for (prescale = 0; prescale < 7; prescale++) {
		if (actual && actual <= rate) {
			u32	pckr;

			pckr = at91_sys_read(AT91_PMC_PCKR(clk->id));
			pckr &= AT91_PMC_CSS_PLLB;	/* clock selection */
			pckr |= prescale << 2;
			at91_sys_write(AT91_PMC_PCKR(clk->id), pckr);
			clk->rate_hz = actual;
			break;
		}
		actual >>= 1;
	}

	spin_unlock_irqrestore(&clk_lock, flags);
	return (prescale < 7) ? actual : -ENOENT;
}
EXPORT_SYMBOL(clk_set_rate);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long	flags;

	if (clk->users)
		return -EBUSY;
	if (!parent->primary || !clk->programmable)
		return -EINVAL;
	spin_lock_irqsave(&clk_lock, flags);

	clk->rate_hz = parent->rate_hz;
	clk->parent = parent;
	at91_sys_write(AT91_PMC_PCKR(clk->id), parent->id);

	spin_unlock_irqrestore(&clk_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_set_parent);

#endif	/* CONFIG_AT91_PROGRAMMABLE_CLOCKS */

/*------------------------------------------------------------------------*/

#ifdef CONFIG_DEBUG_FS

static int at91_clk_show(struct seq_file *s, void *unused)
{
	u32		scsr, pcsr, sr;
	unsigned	i;

	seq_printf(s, "SCSR = %8x\n", scsr = at91_sys_read(AT91_PMC_SCSR));
	seq_printf(s, "PCSR = %8x\n", pcsr = at91_sys_read(AT91_PMC_PCSR));

	seq_printf(s, "MOR  = %8x\n", at91_sys_read(AT91_CKGR_MOR));
	seq_printf(s, "MCFR = %8x\n", at91_sys_read(AT91_CKGR_MCFR));
	seq_printf(s, "PLLA = %8x\n", at91_sys_read(AT91_CKGR_PLLAR));
	seq_printf(s, "PLLB = %8x\n", at91_sys_read(AT91_CKGR_PLLBR));

	seq_printf(s, "MCKR = %8x\n", at91_sys_read(AT91_PMC_MCKR));
	for (i = 0; i < 4; i++)
		seq_printf(s, "PCK%d = %8x\n", i, at91_sys_read(AT91_PMC_PCKR(i)));
	seq_printf(s, "SR   = %8x\n", sr = at91_sys_read(AT91_PMC_SR));

	seq_printf(s, "\n");

	for (i = 0; i < ARRAY_SIZE(clock_list); i++) {
		char		*state;
		struct clk	*clk = clock_list[i];

		if (clk->mode == pmc_sys_mode)
			state = (scsr & clk->pmc_mask) ? "on" : "off";
		else if (clk->mode == pmc_periph_mode)
			state = (pcsr & clk->pmc_mask) ? "on" : "off";
		else if (clk->pmc_mask)
			state = (sr & clk->pmc_mask) ? "on" : "off";
		else if (clk == &clk32k || clk == &main_clk)
			state = "on";
		else
			state = "";

		seq_printf(s, "%-10s users=%2d %-3s %9ld Hz %s\n",
			clk->name, clk->users, state, clk_get_rate(clk),
			clk->parent ? clk->parent->name : "");
	}
	return 0;
}

static int at91_clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, at91_clk_show, NULL);
}

static struct file_operations at91_clk_operations = {
	.open		= at91_clk_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init at91_clk_debugfs_init(void)
{
	/* /sys/kernel/debug/at91_clk */
	(void) debugfs_create_file("at91_clk", S_IFREG | S_IRUGO, NULL, NULL, &at91_clk_operations);

	return 0;
}
postcore_initcall(at91_clk_debugfs_init);

#endif

/*------------------------------------------------------------------------*/

static u32 __init at91_pll_rate(struct clk *pll, u32 freq, u32 reg)
{
	unsigned mul, div;

	div = reg & 0xff;
	mul = (reg >> 16) & 0x7ff;
	if (div && mul) {
		freq /= div;
		freq *= mul + 1;
	} else
		freq = 0;

	return freq;
}

static u32 __init at91_usb_rate(struct clk *pll, u32 freq, u32 reg)
{
	if (pll == &pllb && (reg & AT91_PMC_USB96M))
		return freq / 2;
	else
		return freq;
}

static unsigned __init at91_pll_calc(unsigned main_freq, unsigned out_freq)
{
	unsigned i, div = 0, mul = 0, diff = 1 << 30;
	unsigned ret = (out_freq > 155000000) ? 0xbe00 : 0x3e00;

	/* PLL output max 240 MHz (or 180 MHz per errata) */
	if (out_freq > 240000000)
		goto fail;

	for (i = 1; i < 256; i++) {
		int diff1;
		unsigned input, mul1;

		/*
		 * PLL input between 1MHz and 32MHz per spec, but lower
		 * frequences seem necessary in some cases so allow 100K.
		 */
		input = main_freq / i;
		if (input < 100000)
			continue;
		if (input > 32000000)
			continue;

		mul1 = out_freq / input;
		if (mul1 > 2048)
			continue;
		if (mul1 < 2)
			goto fail;

		diff1 = out_freq - input * mul1;
		if (diff1 < 0)
			diff1 = -diff1;
		if (diff > diff1) {
			diff = diff1;
			div = i;
			mul = mul1;
			if (diff == 0)
				break;
		}
	}
	if (i == 256 && diff > (out_freq >> 5))
		goto fail;
	return ret | ((mul - 1) << 16) | div;
fail:
	return 0;
}


/*
 * Several unused clocks may be active.  Turn them off.
 */
static void at91_periphclk_reset(void)
{
	unsigned long reg;
	int i;

	reg = at91_sys_read(AT91_PMC_PCSR);

	for (i = 0; i < ARRAY_SIZE(clock_list); i++) {
		struct clk	*clk = clock_list[i];

		if (clk->mode != pmc_periph_mode)
			continue;

		if (clk->users > 0)
			reg &= ~clk->pmc_mask;
	}

	at91_sys_write(AT91_PMC_PCDR, reg);
}

int __init at91_clock_init(unsigned long main_clock)
{
	unsigned tmp, freq, mckr;

	spin_lock_init(&clk_lock);

	/*
	 * When the bootloader initialized the main oscillator correctly,
	 * there's no problem using the cycle counter.  But if it didn't,
	 * or when using oscillator bypass mode, we must be told the speed
	 * of the main clock.
	 */
	if (!main_clock) {
		do {
			tmp = at91_sys_read(AT91_CKGR_MCFR);
		} while (!(tmp & AT91_PMC_MAINRDY));
		main_clock = (tmp & AT91_PMC_MAINF) * (AT91_SLOW_CLOCK / 16);
	}
	main_clk.rate_hz = main_clock;

	/* report if PLLA is more than mildly overclocked */
	plla.rate_hz = at91_pll_rate(&plla, main_clock, at91_sys_read(AT91_CKGR_PLLAR));
	if (plla.rate_hz > 209000000)
		pr_info("Clocks: PLLA overclocked, %ld MHz\n", plla.rate_hz / 1000000);

	/*
	 * USB clock init:  choose 48 MHz PLLB value, turn all clocks off,
	 * disable 48MHz clock during usb peripheral suspend.
	 *
	 * REVISIT:  assumes MCK doesn't derive from PLLB!
	 */
	at91_pllb_usb_init = at91_pll_calc(main_clock, 48000000 * 2) | AT91_PMC_USB96M;
	pllb.rate_hz = at91_pll_rate(&pllb, main_clock, at91_pllb_usb_init);
	at91_sys_write(AT91_PMC_SCDR, AT91_PMC_UHP | AT91_PMC_UDP);
	at91_sys_write(AT91_CKGR_PLLBR, 0);
	at91_sys_write(AT91_PMC_SCER, AT91_PMC_MCKUDP);

	udpck.rate_hz = at91_usb_rate(&pllb, pllb.rate_hz, at91_pllb_usb_init);
	uhpck.rate_hz = at91_usb_rate(&pllb, pllb.rate_hz, at91_pllb_usb_init);

	/*
	 * MCK and CPU derive from one of those primary clocks.
	 * For now, assume this parentage won't change.
	 */
	mckr = at91_sys_read(AT91_PMC_MCKR);
	mck.parent = clock_list[mckr & AT91_PMC_CSS];
	freq = mck.parent->rate_hz;
	freq /= (1 << ((mckr >> 2) & 3));		/* prescale */
	mck.rate_hz = freq / (1 + ((mckr >> 8) & 3));	/* mdiv */

	/* MCK and CPU clock are "always on" */
	clk_enable(&mck);

	printk("Clocks: CPU %u MHz, master %u MHz, main %u.%03u MHz\n",
		freq / 1000000, (unsigned) mck.rate_hz / 1000000,
		(unsigned) main_clock / 1000000,
		((unsigned) main_clock % 1000000) / 1000);

#ifdef CONFIG_AT91_PROGRAMMABLE_CLOCKS
	/* establish PCK0..PCK3 parentage */
	for (tmp = 0; tmp < ARRAY_SIZE(clock_list); tmp++) {
		struct clk	*clk = clock_list[tmp], *parent;
		u32		pckr;

		if (!clk->programmable)
			continue;

		pckr = at91_sys_read(AT91_PMC_PCKR(clk->id));
		parent = clock_list[pckr & AT91_PMC_CSS];
		clk->parent = parent;
		clk->rate_hz = parent->rate_hz / (1 << ((pckr >> 2) & 3));

		if (clk->users == 0) {
			/* not being used, so switch it off */
			at91_sys_write(AT91_PMC_SCDR, clk->pmc_mask);
		}
	}
#else
	/* disable all programmable clocks */
	at91_sys_write(AT91_PMC_SCDR, AT91_PMC_PCK0 | AT91_PMC_PCK1 | AT91_PMC_PCK2 | AT91_PMC_PCK3);
#endif

	/* enable the PIO clocks */
	clk_enable(&pioA_clk);
	clk_enable(&pioB_clk);
	clk_enable(&pioC_clk);
	clk_enable(&pioD_clk);

	/* disable all other unused peripheral clocks */
	at91_periphclk_reset();

	return 0;
}
