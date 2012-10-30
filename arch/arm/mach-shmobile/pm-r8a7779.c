/*
 * r8a7779 Power management support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <asm/io.h>
#include <mach/common.h>
#include <mach/r8a7779.h>

static void __iomem *r8a7779_sysc_base;

/* SYSC */
#define SYSCSR 0x00
#define SYSCISR 0x04
#define SYSCISCR 0x08
#define SYSCIER 0x0c
#define SYSCIMR 0x10
#define PWRSR0 0x40
#define PWRSR1 0x80
#define PWRSR2 0xc0
#define PWRSR3 0x100
#define PWRSR4 0x140

#define PWRSR_OFFS 0x00
#define PWROFFCR_OFFS 0x04
#define PWRONCR_OFFS 0x0c
#define PWRER_OFFS 0x14

#define SYSCSR_RETRIES 100
#define SYSCSR_DELAY_US 1

#define SYSCISR_RETRIES 1000
#define SYSCISR_DELAY_US 1

#if defined(CONFIG_PM) || defined(CONFIG_SMP)

static DEFINE_SPINLOCK(r8a7779_sysc_lock); /* SMP CPUs + I/O devices */

static int r8a7779_sysc_pwr_on_off(struct r8a7779_pm_ch *r8a7779_ch,
				   int sr_bit, int reg_offs)
{
	int k;

	for (k = 0; k < SYSCSR_RETRIES; k++) {
		if (ioread32(r8a7779_sysc_base + SYSCSR) & (1 << sr_bit))
			break;
		udelay(SYSCSR_DELAY_US);
	}

	if (k == SYSCSR_RETRIES)
		return -EAGAIN;

	iowrite32(1 << r8a7779_ch->chan_bit,
		  r8a7779_sysc_base + r8a7779_ch->chan_offs + reg_offs);

	return 0;
}

static int r8a7779_sysc_pwr_off(struct r8a7779_pm_ch *r8a7779_ch)
{
	return r8a7779_sysc_pwr_on_off(r8a7779_ch, 0, PWROFFCR_OFFS);
}

static int r8a7779_sysc_pwr_on(struct r8a7779_pm_ch *r8a7779_ch)
{
	return r8a7779_sysc_pwr_on_off(r8a7779_ch, 1, PWRONCR_OFFS);
}

static int r8a7779_sysc_update(struct r8a7779_pm_ch *r8a7779_ch,
			       int (*on_off_fn)(struct r8a7779_pm_ch *))
{
	unsigned int isr_mask = 1 << r8a7779_ch->isr_bit;
	unsigned int chan_mask = 1 << r8a7779_ch->chan_bit;
	unsigned int status;
	unsigned long flags;
	int ret = 0;
	int k;

	spin_lock_irqsave(&r8a7779_sysc_lock, flags);

	iowrite32(isr_mask, r8a7779_sysc_base + SYSCISCR);

	do {
		ret = on_off_fn(r8a7779_ch);
		if (ret)
			goto out;

		status = ioread32(r8a7779_sysc_base +
				  r8a7779_ch->chan_offs + PWRER_OFFS);
	} while (status & chan_mask);

	for (k = 0; k < SYSCISR_RETRIES; k++) {
		if (ioread32(r8a7779_sysc_base + SYSCISR) & isr_mask)
			break;
		udelay(SYSCISR_DELAY_US);
	}

	if (k == SYSCISR_RETRIES)
		ret = -EIO;

	iowrite32(isr_mask, r8a7779_sysc_base + SYSCISCR);

 out:
	spin_unlock_irqrestore(&r8a7779_sysc_lock, flags);

	pr_debug("r8a7779 power domain %d: %02x %02x %02x %02x %02x -> %d\n",
		 r8a7779_ch->isr_bit, ioread32(r8a7779_sysc_base + PWRSR0),
		 ioread32(r8a7779_sysc_base + PWRSR1),
		 ioread32(r8a7779_sysc_base + PWRSR2),
		 ioread32(r8a7779_sysc_base + PWRSR3),
		 ioread32(r8a7779_sysc_base + PWRSR4), ret);
	return ret;
}

int r8a7779_sysc_power_down(struct r8a7779_pm_ch *r8a7779_ch)
{
	return r8a7779_sysc_update(r8a7779_ch, r8a7779_sysc_pwr_off);
}

int r8a7779_sysc_power_up(struct r8a7779_pm_ch *r8a7779_ch)
{
	return r8a7779_sysc_update(r8a7779_ch, r8a7779_sysc_pwr_on);
}

static void __init r8a7779_sysc_init(void)
{
	r8a7779_sysc_base = ioremap_nocache(0xffd85000, PAGE_SIZE);
	if (!r8a7779_sysc_base)
		panic("unable to ioremap r8a7779 SYSC hardware block\n");

	/* enable all interrupt sources, but do not use interrupt handler */
	iowrite32(0x0131000e, r8a7779_sysc_base + SYSCIER);
	iowrite32(0, r8a7779_sysc_base + SYSCIMR);
}

#else /* CONFIG_PM || CONFIG_SMP */

static inline void r8a7779_sysc_init(void) {}

#endif /* CONFIG_PM || CONFIG_SMP */

#ifdef CONFIG_PM

static int pd_power_down(struct generic_pm_domain *genpd)
{
	return r8a7779_sysc_power_down(to_r8a7779_ch(genpd));
}

static int pd_power_up(struct generic_pm_domain *genpd)
{
	return r8a7779_sysc_power_up(to_r8a7779_ch(genpd));
}

static bool pd_is_off(struct generic_pm_domain *genpd)
{
	struct r8a7779_pm_ch *r8a7779_ch = to_r8a7779_ch(genpd);
	unsigned int st;

	st = ioread32(r8a7779_sysc_base + r8a7779_ch->chan_offs + PWRSR_OFFS);
	if (st & (1 << r8a7779_ch->chan_bit))
		return true;

	return false;
}

static bool pd_active_wakeup(struct device *dev)
{
	return true;
}

static void r8a7779_init_pm_domain(struct r8a7779_pm_domain *r8a7779_pd)
{
	struct generic_pm_domain *genpd = &r8a7779_pd->genpd;

	pm_genpd_init(genpd, NULL, false);
	genpd->dev_ops.stop = pm_clk_suspend;
	genpd->dev_ops.start = pm_clk_resume;
	genpd->dev_ops.active_wakeup = pd_active_wakeup;
	genpd->dev_irq_safe = true;
	genpd->power_off = pd_power_down;
	genpd->power_on = pd_power_up;

	if (pd_is_off(&r8a7779_pd->genpd))
		pd_power_up(&r8a7779_pd->genpd);
}

static struct r8a7779_pm_domain r8a7779_pm_domains[] = {
	{
		.genpd.name = "SH4A",
		.ch = {
			.chan_offs = 0x80, /* PWRSR1 .. PWRER1 */
			.isr_bit = 16, /* SH4A */
		},
	},
	{
		.genpd.name = "SGX",
		.ch = {
			.chan_offs = 0xc0, /* PWRSR2 .. PWRER2 */
			.isr_bit = 20, /* SGX */
		},
	},
	{
		.genpd.name = "VDP1",
		.ch = {
			.chan_offs = 0x100, /* PWRSR3 .. PWRER3 */
			.isr_bit = 21, /* VDP */
		},
	},
	{
		.genpd.name = "IMPX3",
		.ch = {
			.chan_offs = 0x140, /* PWRSR4 .. PWRER4 */
			.isr_bit = 24, /* IMP */
		},
	},
};

void __init r8a7779_init_pm_domains(void)
{
	int j;

	for (j = 0; j < ARRAY_SIZE(r8a7779_pm_domains); j++)
		r8a7779_init_pm_domain(&r8a7779_pm_domains[j]);
}

#endif /* CONFIG_PM */

void __init r8a7779_pm_init(void)
{
	static int once;

	if (!once++)
		r8a7779_sysc_init();
}
