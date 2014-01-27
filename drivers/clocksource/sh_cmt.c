/*
 * SuperH Timer Support - CMT
 *
 *  Copyright (C) 2008 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/sh_timer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

struct sh_cmt_device;

struct sh_cmt_channel {
	struct sh_cmt_device *cmt;

	void __iomem *base;

	unsigned long flags;
	unsigned long match_value;
	unsigned long next_match_value;
	unsigned long max_match_value;
	unsigned long rate;
	raw_spinlock_t lock;
	struct clock_event_device ced;
	struct clocksource cs;
	unsigned long total_cycles;
	bool cs_enabled;
};

struct sh_cmt_device {
	struct platform_device *pdev;

	void __iomem *mapbase_ch;
	void __iomem *mapbase;
	struct clk *clk;

	struct sh_cmt_channel channel;

	unsigned long width; /* 16 or 32 bit version of hardware block */
	unsigned long overflow_bit;
	unsigned long clear_bits;

	/* callbacks for CMSTR and CMCSR access */
	unsigned long (*read_control)(void __iomem *base, unsigned long offs);
	void (*write_control)(void __iomem *base, unsigned long offs,
			      unsigned long value);

	/* callbacks for CMCNT and CMCOR access */
	unsigned long (*read_count)(void __iomem *base, unsigned long offs);
	void (*write_count)(void __iomem *base, unsigned long offs,
			    unsigned long value);
};

/* Examples of supported CMT timer register layouts and I/O access widths:
 *
 * "16-bit counter and 16-bit control" as found on sh7263:
 * CMSTR 0xfffec000 16-bit
 * CMCSR 0xfffec002 16-bit
 * CMCNT 0xfffec004 16-bit
 * CMCOR 0xfffec006 16-bit
 *
 * "32-bit counter and 16-bit control" as found on sh7372, sh73a0, r8a7740:
 * CMSTR 0xffca0000 16-bit
 * CMCSR 0xffca0060 16-bit
 * CMCNT 0xffca0064 32-bit
 * CMCOR 0xffca0068 32-bit
 *
 * "32-bit counter and 32-bit control" as found on r8a73a4 and r8a7790:
 * CMSTR 0xffca0500 32-bit
 * CMCSR 0xffca0510 32-bit
 * CMCNT 0xffca0514 32-bit
 * CMCOR 0xffca0518 32-bit
 */

static unsigned long sh_cmt_read16(void __iomem *base, unsigned long offs)
{
	return ioread16(base + (offs << 1));
}

static unsigned long sh_cmt_read32(void __iomem *base, unsigned long offs)
{
	return ioread32(base + (offs << 2));
}

static void sh_cmt_write16(void __iomem *base, unsigned long offs,
			   unsigned long value)
{
	iowrite16(value, base + (offs << 1));
}

static void sh_cmt_write32(void __iomem *base, unsigned long offs,
			   unsigned long value)
{
	iowrite32(value, base + (offs << 2));
}

#define CMCSR 0 /* channel register */
#define CMCNT 1 /* channel register */
#define CMCOR 2 /* channel register */

static inline unsigned long sh_cmt_read_cmstr(struct sh_cmt_channel *ch)
{
	return ch->cmt->read_control(ch->cmt->mapbase, 0);
}

static inline unsigned long sh_cmt_read_cmcsr(struct sh_cmt_channel *ch)
{
	return ch->cmt->read_control(ch->base, CMCSR);
}

static inline unsigned long sh_cmt_read_cmcnt(struct sh_cmt_channel *ch)
{
	return ch->cmt->read_count(ch->base, CMCNT);
}

static inline void sh_cmt_write_cmstr(struct sh_cmt_channel *ch,
				      unsigned long value)
{
	ch->cmt->write_control(ch->cmt->mapbase, 0, value);
}

static inline void sh_cmt_write_cmcsr(struct sh_cmt_channel *ch,
				      unsigned long value)
{
	ch->cmt->write_control(ch->base, CMCSR, value);
}

static inline void sh_cmt_write_cmcnt(struct sh_cmt_channel *ch,
				      unsigned long value)
{
	ch->cmt->write_count(ch->base, CMCNT, value);
}

static inline void sh_cmt_write_cmcor(struct sh_cmt_channel *ch,
				      unsigned long value)
{
	ch->cmt->write_count(ch->base, CMCOR, value);
}

static unsigned long sh_cmt_get_counter(struct sh_cmt_channel *ch,
					int *has_wrapped)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = sh_cmt_read_cmcsr(ch) & ch->cmt->overflow_bit;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = sh_cmt_read_cmcnt(ch);
		v2 = sh_cmt_read_cmcnt(ch);
		v3 = sh_cmt_read_cmcnt(ch);
		o1 = sh_cmt_read_cmcsr(ch) & ch->cmt->overflow_bit;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	*has_wrapped = o1;
	return v2;
}

static DEFINE_RAW_SPINLOCK(sh_cmt_lock);

static void sh_cmt_start_stop_ch(struct sh_cmt_channel *ch, int start)
{
	struct sh_timer_config *cfg = ch->cmt->pdev->dev.platform_data;
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	raw_spin_lock_irqsave(&sh_cmt_lock, flags);
	value = sh_cmt_read_cmstr(ch);

	if (start)
		value |= 1 << cfg->timer_bit;
	else
		value &= ~(1 << cfg->timer_bit);

	sh_cmt_write_cmstr(ch, value);
	raw_spin_unlock_irqrestore(&sh_cmt_lock, flags);
}

static int sh_cmt_enable(struct sh_cmt_channel *ch, unsigned long *rate)
{
	int k, ret;

	pm_runtime_get_sync(&ch->cmt->pdev->dev);
	dev_pm_syscore_device(&ch->cmt->pdev->dev, true);

	/* enable clock */
	ret = clk_enable(ch->cmt->clk);
	if (ret) {
		dev_err(&ch->cmt->pdev->dev, "cannot enable clock\n");
		goto err0;
	}

	/* make sure channel is disabled */
	sh_cmt_start_stop_ch(ch, 0);

	/* configure channel, periodic mode and maximum timeout */
	if (ch->cmt->width == 16) {
		*rate = clk_get_rate(ch->cmt->clk) / 512;
		sh_cmt_write_cmcsr(ch, 0x43);
	} else {
		*rate = clk_get_rate(ch->cmt->clk) / 8;
		sh_cmt_write_cmcsr(ch, 0x01a4);
	}

	sh_cmt_write_cmcor(ch, 0xffffffff);
	sh_cmt_write_cmcnt(ch, 0);

	/*
	 * According to the sh73a0 user's manual, as CMCNT can be operated
	 * only by the RCLK (Pseudo 32 KHz), there's one restriction on
	 * modifying CMCNT register; two RCLK cycles are necessary before
	 * this register is either read or any modification of the value
	 * it holds is reflected in the LSI's actual operation.
	 *
	 * While at it, we're supposed to clear out the CMCNT as of this
	 * moment, so make sure it's processed properly here.  This will
	 * take RCLKx2 at maximum.
	 */
	for (k = 0; k < 100; k++) {
		if (!sh_cmt_read_cmcnt(ch))
			break;
		udelay(1);
	}

	if (sh_cmt_read_cmcnt(ch)) {
		dev_err(&ch->cmt->pdev->dev, "cannot clear CMCNT\n");
		ret = -ETIMEDOUT;
		goto err1;
	}

	/* enable channel */
	sh_cmt_start_stop_ch(ch, 1);
	return 0;
 err1:
	/* stop clock */
	clk_disable(ch->cmt->clk);

 err0:
	return ret;
}

static void sh_cmt_disable(struct sh_cmt_channel *ch)
{
	/* disable channel */
	sh_cmt_start_stop_ch(ch, 0);

	/* disable interrupts in CMT block */
	sh_cmt_write_cmcsr(ch, 0);

	/* stop clock */
	clk_disable(ch->cmt->clk);

	dev_pm_syscore_device(&ch->cmt->pdev->dev, false);
	pm_runtime_put(&ch->cmt->pdev->dev);
}

/* private flags */
#define FLAG_CLOCKEVENT (1 << 0)
#define FLAG_CLOCKSOURCE (1 << 1)
#define FLAG_REPROGRAM (1 << 2)
#define FLAG_SKIPEVENT (1 << 3)
#define FLAG_IRQCONTEXT (1 << 4)

static void sh_cmt_clock_event_program_verify(struct sh_cmt_channel *ch,
					      int absolute)
{
	unsigned long new_match;
	unsigned long value = ch->next_match_value;
	unsigned long delay = 0;
	unsigned long now = 0;
	int has_wrapped;

	now = sh_cmt_get_counter(ch, &has_wrapped);
	ch->flags |= FLAG_REPROGRAM; /* force reprogram */

	if (has_wrapped) {
		/* we're competing with the interrupt handler.
		 *  -> let the interrupt handler reprogram the timer.
		 *  -> interrupt number two handles the event.
		 */
		ch->flags |= FLAG_SKIPEVENT;
		return;
	}

	if (absolute)
		now = 0;

	do {
		/* reprogram the timer hardware,
		 * but don't save the new match value yet.
		 */
		new_match = now + value + delay;
		if (new_match > ch->max_match_value)
			new_match = ch->max_match_value;

		sh_cmt_write_cmcor(ch, new_match);

		now = sh_cmt_get_counter(ch, &has_wrapped);
		if (has_wrapped && (new_match > ch->match_value)) {
			/* we are changing to a greater match value,
			 * so this wrap must be caused by the counter
			 * matching the old value.
			 * -> first interrupt reprograms the timer.
			 * -> interrupt number two handles the event.
			 */
			ch->flags |= FLAG_SKIPEVENT;
			break;
		}

		if (has_wrapped) {
			/* we are changing to a smaller match value,
			 * so the wrap must be caused by the counter
			 * matching the new value.
			 * -> save programmed match value.
			 * -> let isr handle the event.
			 */
			ch->match_value = new_match;
			break;
		}

		/* be safe: verify hardware settings */
		if (now < new_match) {
			/* timer value is below match value, all good.
			 * this makes sure we won't miss any match events.
			 * -> save programmed match value.
			 * -> let isr handle the event.
			 */
			ch->match_value = new_match;
			break;
		}

		/* the counter has reached a value greater
		 * than our new match value. and since the
		 * has_wrapped flag isn't set we must have
		 * programmed a too close event.
		 * -> increase delay and retry.
		 */
		if (delay)
			delay <<= 1;
		else
			delay = 1;

		if (!delay)
			dev_warn(&ch->cmt->pdev->dev, "too long delay\n");

	} while (delay);
}

static void __sh_cmt_set_next(struct sh_cmt_channel *ch, unsigned long delta)
{
	if (delta > ch->max_match_value)
		dev_warn(&ch->cmt->pdev->dev, "delta out of range\n");

	ch->next_match_value = delta;
	sh_cmt_clock_event_program_verify(ch, 0);
}

static void sh_cmt_set_next(struct sh_cmt_channel *ch, unsigned long delta)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ch->lock, flags);
	__sh_cmt_set_next(ch, delta);
	raw_spin_unlock_irqrestore(&ch->lock, flags);
}

static irqreturn_t sh_cmt_interrupt(int irq, void *dev_id)
{
	struct sh_cmt_channel *ch = dev_id;

	/* clear flags */
	sh_cmt_write_cmcsr(ch, sh_cmt_read_cmcsr(ch) & ch->cmt->clear_bits);

	/* update clock source counter to begin with if enabled
	 * the wrap flag should be cleared by the timer specific
	 * isr before we end up here.
	 */
	if (ch->flags & FLAG_CLOCKSOURCE)
		ch->total_cycles += ch->match_value + 1;

	if (!(ch->flags & FLAG_REPROGRAM))
		ch->next_match_value = ch->max_match_value;

	ch->flags |= FLAG_IRQCONTEXT;

	if (ch->flags & FLAG_CLOCKEVENT) {
		if (!(ch->flags & FLAG_SKIPEVENT)) {
			if (ch->ced.mode == CLOCK_EVT_MODE_ONESHOT) {
				ch->next_match_value = ch->max_match_value;
				ch->flags |= FLAG_REPROGRAM;
			}

			ch->ced.event_handler(&ch->ced);
		}
	}

	ch->flags &= ~FLAG_SKIPEVENT;

	if (ch->flags & FLAG_REPROGRAM) {
		ch->flags &= ~FLAG_REPROGRAM;
		sh_cmt_clock_event_program_verify(ch, 1);

		if (ch->flags & FLAG_CLOCKEVENT)
			if ((ch->ced.mode == CLOCK_EVT_MODE_SHUTDOWN)
			    || (ch->match_value == ch->next_match_value))
				ch->flags &= ~FLAG_REPROGRAM;
	}

	ch->flags &= ~FLAG_IRQCONTEXT;

	return IRQ_HANDLED;
}

static int sh_cmt_start(struct sh_cmt_channel *ch, unsigned long flag)
{
	int ret = 0;
	unsigned long flags;

	raw_spin_lock_irqsave(&ch->lock, flags);

	if (!(ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		ret = sh_cmt_enable(ch, &ch->rate);

	if (ret)
		goto out;
	ch->flags |= flag;

	/* setup timeout if no clockevent */
	if ((flag == FLAG_CLOCKSOURCE) && (!(ch->flags & FLAG_CLOCKEVENT)))
		__sh_cmt_set_next(ch, ch->max_match_value);
 out:
	raw_spin_unlock_irqrestore(&ch->lock, flags);

	return ret;
}

static void sh_cmt_stop(struct sh_cmt_channel *ch, unsigned long flag)
{
	unsigned long flags;
	unsigned long f;

	raw_spin_lock_irqsave(&ch->lock, flags);

	f = ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE);
	ch->flags &= ~flag;

	if (f && !(ch->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		sh_cmt_disable(ch);

	/* adjust the timeout to maximum if only clocksource left */
	if ((flag == FLAG_CLOCKEVENT) && (ch->flags & FLAG_CLOCKSOURCE))
		__sh_cmt_set_next(ch, ch->max_match_value);

	raw_spin_unlock_irqrestore(&ch->lock, flags);
}

static struct sh_cmt_channel *cs_to_sh_cmt(struct clocksource *cs)
{
	return container_of(cs, struct sh_cmt_channel, cs);
}

static cycle_t sh_cmt_clocksource_read(struct clocksource *cs)
{
	struct sh_cmt_channel *ch = cs_to_sh_cmt(cs);
	unsigned long flags, raw;
	unsigned long value;
	int has_wrapped;

	raw_spin_lock_irqsave(&ch->lock, flags);
	value = ch->total_cycles;
	raw = sh_cmt_get_counter(ch, &has_wrapped);

	if (unlikely(has_wrapped))
		raw += ch->match_value + 1;
	raw_spin_unlock_irqrestore(&ch->lock, flags);

	return value + raw;
}

static int sh_cmt_clocksource_enable(struct clocksource *cs)
{
	int ret;
	struct sh_cmt_channel *ch = cs_to_sh_cmt(cs);

	WARN_ON(ch->cs_enabled);

	ch->total_cycles = 0;

	ret = sh_cmt_start(ch, FLAG_CLOCKSOURCE);
	if (!ret) {
		__clocksource_updatefreq_hz(cs, ch->rate);
		ch->cs_enabled = true;
	}
	return ret;
}

static void sh_cmt_clocksource_disable(struct clocksource *cs)
{
	struct sh_cmt_channel *ch = cs_to_sh_cmt(cs);

	WARN_ON(!ch->cs_enabled);

	sh_cmt_stop(ch, FLAG_CLOCKSOURCE);
	ch->cs_enabled = false;
}

static void sh_cmt_clocksource_suspend(struct clocksource *cs)
{
	struct sh_cmt_channel *ch = cs_to_sh_cmt(cs);

	sh_cmt_stop(ch, FLAG_CLOCKSOURCE);
	pm_genpd_syscore_poweroff(&ch->cmt->pdev->dev);
}

static void sh_cmt_clocksource_resume(struct clocksource *cs)
{
	struct sh_cmt_channel *ch = cs_to_sh_cmt(cs);

	pm_genpd_syscore_poweron(&ch->cmt->pdev->dev);
	sh_cmt_start(ch, FLAG_CLOCKSOURCE);
}

static int sh_cmt_register_clocksource(struct sh_cmt_channel *ch,
				       const char *name, unsigned long rating)
{
	struct clocksource *cs = &ch->cs;

	cs->name = name;
	cs->rating = rating;
	cs->read = sh_cmt_clocksource_read;
	cs->enable = sh_cmt_clocksource_enable;
	cs->disable = sh_cmt_clocksource_disable;
	cs->suspend = sh_cmt_clocksource_suspend;
	cs->resume = sh_cmt_clocksource_resume;
	cs->mask = CLOCKSOURCE_MASK(sizeof(unsigned long) * 8);
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;

	dev_info(&ch->cmt->pdev->dev, "used as clock source\n");

	/* Register with dummy 1 Hz value, gets updated in ->enable() */
	clocksource_register_hz(cs, 1);
	return 0;
}

static struct sh_cmt_channel *ced_to_sh_cmt(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_cmt_channel, ced);
}

static void sh_cmt_clock_event_start(struct sh_cmt_channel *ch, int periodic)
{
	struct clock_event_device *ced = &ch->ced;

	sh_cmt_start(ch, FLAG_CLOCKEVENT);

	/* TODO: calculate good shift from rate and counter bit width */

	ced->shift = 32;
	ced->mult = div_sc(ch->rate, NSEC_PER_SEC, ced->shift);
	ced->max_delta_ns = clockevent_delta2ns(ch->max_match_value, ced);
	ced->min_delta_ns = clockevent_delta2ns(0x1f, ced);

	if (periodic)
		sh_cmt_set_next(ch, ((ch->rate + HZ/2) / HZ) - 1);
	else
		sh_cmt_set_next(ch, ch->max_match_value);
}

static void sh_cmt_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_cmt_channel *ch = ced_to_sh_cmt(ced);

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		sh_cmt_stop(ch, FLAG_CLOCKEVENT);
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		dev_info(&ch->cmt->pdev->dev,
			 "used for periodic clock events\n");
		sh_cmt_clock_event_start(ch, 1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		dev_info(&ch->cmt->pdev->dev,
			 "used for oneshot clock events\n");
		sh_cmt_clock_event_start(ch, 0);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		sh_cmt_stop(ch, FLAG_CLOCKEVENT);
		break;
	default:
		break;
	}
}

static int sh_cmt_clock_event_next(unsigned long delta,
				   struct clock_event_device *ced)
{
	struct sh_cmt_channel *ch = ced_to_sh_cmt(ced);

	BUG_ON(ced->mode != CLOCK_EVT_MODE_ONESHOT);
	if (likely(ch->flags & FLAG_IRQCONTEXT))
		ch->next_match_value = delta - 1;
	else
		sh_cmt_set_next(ch, delta - 1);

	return 0;
}

static void sh_cmt_clock_event_suspend(struct clock_event_device *ced)
{
	struct sh_cmt_channel *ch = ced_to_sh_cmt(ced);

	pm_genpd_syscore_poweroff(&ch->cmt->pdev->dev);
	clk_unprepare(ch->cmt->clk);
}

static void sh_cmt_clock_event_resume(struct clock_event_device *ced)
{
	struct sh_cmt_channel *ch = ced_to_sh_cmt(ced);

	clk_prepare(ch->cmt->clk);
	pm_genpd_syscore_poweron(&ch->cmt->pdev->dev);
}

static void sh_cmt_register_clockevent(struct sh_cmt_channel *ch,
				       const char *name, unsigned long rating)
{
	struct clock_event_device *ced = &ch->ced;

	memset(ced, 0, sizeof(*ced));

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->features |= CLOCK_EVT_FEAT_ONESHOT;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_next_event = sh_cmt_clock_event_next;
	ced->set_mode = sh_cmt_clock_event_mode;
	ced->suspend = sh_cmt_clock_event_suspend;
	ced->resume = sh_cmt_clock_event_resume;

	dev_info(&ch->cmt->pdev->dev, "used for clock events\n");
	clockevents_register_device(ced);
}

static int sh_cmt_register(struct sh_cmt_channel *ch, const char *name,
			   unsigned long clockevent_rating,
			   unsigned long clocksource_rating)
{
	if (clockevent_rating)
		sh_cmt_register_clockevent(ch, name, clockevent_rating);

	if (clocksource_rating)
		sh_cmt_register_clocksource(ch, name, clocksource_rating);

	return 0;
}

static int sh_cmt_setup_channel(struct sh_cmt_channel *ch,
				struct sh_cmt_device *cmt)
{
	struct sh_timer_config *cfg = cmt->pdev->dev.platform_data;
	int irq;
	int ret;

	memset(ch, 0, sizeof(*ch));
	ch->cmt = cmt;
	ch->base = cmt->mapbase_ch;

	irq = platform_get_irq(cmt->pdev, 0);
	if (irq < 0) {
		dev_err(&cmt->pdev->dev, "failed to get irq\n");
		return irq;
	}

	if (cmt->width == (sizeof(ch->max_match_value) * 8))
		ch->max_match_value = ~0;
	else
		ch->max_match_value = (1 << cmt->width) - 1;

	ch->match_value = ch->max_match_value;
	raw_spin_lock_init(&ch->lock);

	ret = sh_cmt_register(ch, dev_name(&cmt->pdev->dev),
			      cfg->clockevent_rating,
			      cfg->clocksource_rating);
	if (ret) {
		dev_err(&cmt->pdev->dev, "registration failed\n");
		return ret;
	}
	ch->cs_enabled = false;

	ret = request_irq(irq, sh_cmt_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL | IRQF_NOBALANCING,
			  dev_name(&cmt->pdev->dev), ch);
	if (ret) {
		dev_err(&cmt->pdev->dev, "failed to request irq %d\n", irq);
		return ret;
	}

	return 0;
}

static int sh_cmt_setup(struct sh_cmt_device *cmt, struct platform_device *pdev)
{
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	struct resource *res, *res2;
	int ret;
	ret = -ENXIO;

	memset(cmt, 0, sizeof(*cmt));
	cmt->pdev = pdev;

	if (!cfg) {
		dev_err(&cmt->pdev->dev, "missing platform data\n");
		goto err0;
	}

	res = platform_get_resource(cmt->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&cmt->pdev->dev, "failed to get I/O memory\n");
		goto err0;
	}

	/* optional resource for the shared timer start/stop register */
	res2 = platform_get_resource(cmt->pdev, IORESOURCE_MEM, 1);

	/* map memory, let mapbase_ch point to our channel */
	cmt->mapbase_ch = ioremap_nocache(res->start, resource_size(res));
	if (cmt->mapbase_ch == NULL) {
		dev_err(&cmt->pdev->dev, "failed to remap I/O memory\n");
		goto err0;
	}

	/* map second resource for CMSTR */
	cmt->mapbase = ioremap_nocache(res2 ? res2->start :
				       res->start - cfg->channel_offset,
				       res2 ? resource_size(res2) : 2);
	if (cmt->mapbase == NULL) {
		dev_err(&cmt->pdev->dev, "failed to remap I/O second memory\n");
		goto err1;
	}

	/* get hold of clock */
	cmt->clk = clk_get(&cmt->pdev->dev, "cmt_fck");
	if (IS_ERR(cmt->clk)) {
		dev_err(&cmt->pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(cmt->clk);
		goto err2;
	}

	ret = clk_prepare(cmt->clk);
	if (ret < 0)
		goto err3;

	if (res2 && (resource_size(res2) == 4)) {
		/* assume both CMSTR and CMCSR to be 32-bit */
		cmt->read_control = sh_cmt_read32;
		cmt->write_control = sh_cmt_write32;
	} else {
		cmt->read_control = sh_cmt_read16;
		cmt->write_control = sh_cmt_write16;
	}

	if (resource_size(res) == 6) {
		cmt->width = 16;
		cmt->read_count = sh_cmt_read16;
		cmt->write_count = sh_cmt_write16;
		cmt->overflow_bit = 0x80;
		cmt->clear_bits = ~0x80;
	} else {
		cmt->width = 32;
		cmt->read_count = sh_cmt_read32;
		cmt->write_count = sh_cmt_write32;
		cmt->overflow_bit = 0x8000;
		cmt->clear_bits = ~0xc000;
	}

	ret = sh_cmt_setup_channel(&cmt->channel, cmt);
	if (ret < 0)
		goto err4;

	platform_set_drvdata(pdev, cmt);

	return 0;
err4:
	clk_unprepare(cmt->clk);
err3:
	clk_put(cmt->clk);
err2:
	iounmap(cmt->mapbase);
err1:
	iounmap(cmt->mapbase_ch);
err0:
	return ret;
}

static int sh_cmt_probe(struct platform_device *pdev)
{
	struct sh_cmt_device *cmt = platform_get_drvdata(pdev);
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	int ret;

	if (!is_early_platform_device(pdev)) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}

	if (cmt) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		goto out;
	}

	cmt = kmalloc(sizeof(*cmt), GFP_KERNEL);
	if (cmt == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	ret = sh_cmt_setup(cmt, pdev);
	if (ret) {
		kfree(cmt);
		pm_runtime_idle(&pdev->dev);
		return ret;
	}
	if (is_early_platform_device(pdev))
		return 0;

 out:
	if (cfg->clockevent_rating || cfg->clocksource_rating)
		pm_runtime_irq_safe(&pdev->dev);
	else
		pm_runtime_idle(&pdev->dev);

	return 0;
}

static int sh_cmt_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent and clocksource */
}

static struct platform_driver sh_cmt_device_driver = {
	.probe		= sh_cmt_probe,
	.remove		= sh_cmt_remove,
	.driver		= {
		.name	= "sh_cmt",
	}
};

static int __init sh_cmt_init(void)
{
	return platform_driver_register(&sh_cmt_device_driver);
}

static void __exit sh_cmt_exit(void)
{
	platform_driver_unregister(&sh_cmt_device_driver);
}

early_platform_init("earlytimer", &sh_cmt_device_driver);
subsys_initcall(sh_cmt_init);
module_exit(sh_cmt_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH CMT Timer Driver");
MODULE_LICENSE("GPL v2");
