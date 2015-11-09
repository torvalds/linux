/*
 *  H8/300 16bit Timer driver
 *
 *  Copyright 2015 Yoshinori Sato <ysato@users.sourcefoge.jp>
 */

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TSTR	0
#define TISRA	4
#define TISRC	6

#define TCR	0
#define TCNT	2

struct timer16_priv {
	struct clocksource cs;
	unsigned long total_cycles;
	unsigned long mapbase;
	unsigned long mapcommon;
	unsigned short cs_enabled;
	unsigned char enb;
	unsigned char imfa;
	unsigned char imiea;
	unsigned char ovf;
	raw_spinlock_t lock;
	struct clk *clk;
};

static unsigned long timer16_get_counter(struct timer16_priv *p)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = ctrl_inb(p->mapcommon + TISRC) & p->ovf;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = ctrl_inw(p->mapbase + TCNT);
		v2 = ctrl_inw(p->mapbase + TCNT);
		v3 = ctrl_inw(p->mapbase + TCNT);
		o1 = ctrl_inb(p->mapcommon + TISRC) & p->ovf;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	v2 |= 0x10000;
	return v2;
}


static irqreturn_t timer16_interrupt(int irq, void *dev_id)
{
	struct timer16_priv *p = (struct timer16_priv *)dev_id;

	ctrl_outb(ctrl_inb(p->mapcommon + TISRA) & ~p->imfa,
		  p->mapcommon + TISRA);
	p->total_cycles += 0x10000;

	return IRQ_HANDLED;
}

static inline struct timer16_priv *cs_to_priv(struct clocksource *cs)
{
	return container_of(cs, struct timer16_priv, cs);
}

static cycle_t timer16_clocksource_read(struct clocksource *cs)
{
	struct timer16_priv *p = cs_to_priv(cs);
	unsigned long flags, raw;
	unsigned long value;

	raw_spin_lock_irqsave(&p->lock, flags);
	value = p->total_cycles;
	raw = timer16_get_counter(p);
	raw_spin_unlock_irqrestore(&p->lock, flags);

	return value + raw;
}

static int timer16_enable(struct clocksource *cs)
{
	struct timer16_priv *p = cs_to_priv(cs);

	WARN_ON(p->cs_enabled);

	p->total_cycles = 0;
	ctrl_outw(0x0000, p->mapbase + TCNT);
	ctrl_outb(0x83, p->mapbase + TCR);
	ctrl_outb(ctrl_inb(p->mapcommon + TSTR) | p->enb,
		  p->mapcommon + TSTR);

	p->cs_enabled = true;
	return 0;
}

static void timer16_disable(struct clocksource *cs)
{
	struct timer16_priv *p = cs_to_priv(cs);

	WARN_ON(!p->cs_enabled);

	ctrl_outb(ctrl_inb(p->mapcommon + TSTR) & ~p->enb,
		  p->mapcommon + TSTR);

	p->cs_enabled = false;
}

static struct timer16_priv timer16_priv = {
	.cs = {
		.name = "h8300_16timer",
		.rating = 200,
		.read = timer16_clocksource_read,
		.enable = timer16_enable,
		.disable = timer16_disable,
		.mask = CLOCKSOURCE_MASK(sizeof(unsigned long) * 8),
		.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	},
};

#define REG_CH   0
#define REG_COMM 1

static void __init h8300_16timer_init(struct device_node *node)
{
	void __iomem *base[2];
	int ret, irq;
	unsigned int ch;
	struct clk *clk;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("failed to get clock for clocksource\n");
		return;
	}

	base[REG_CH] = of_iomap(node, 0);
	if (!base[REG_CH]) {
		pr_err("failed to map registers for clocksource\n");
		goto free_clk;
	}

	base[REG_COMM] = of_iomap(node, 1);
	if (!base[REG_COMM]) {
		pr_err("failed to map registers for clocksource\n");
		goto unmap_ch;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("failed to get irq for clockevent\n");
		goto unmap_comm;
	}

	of_property_read_u32(node, "renesas,channel", &ch);

	timer16_priv.mapbase = (unsigned long)base[REG_CH];
	timer16_priv.mapcommon = (unsigned long)base[REG_COMM];
	timer16_priv.enb = 1 << ch;
	timer16_priv.imfa = 1 << ch;
	timer16_priv.imiea = 1 << (4 + ch);

	ret = request_irq(irq, timer16_interrupt,
			  IRQF_TIMER, timer16_priv.cs.name, &timer16_priv);
	if (ret < 0) {
		pr_err("failed to request irq %d of clocksource\n", irq);
		goto unmap_comm;
	}

	clocksource_register_hz(&timer16_priv.cs,
				clk_get_rate(timer16_priv.clk) / 8);
	return;

unmap_comm:
	iounmap(base[REG_COMM]);
unmap_ch:
	iounmap(base[REG_CH]);
free_clk:
	clk_put(clk);
}

CLOCKSOURCE_OF_DECLARE(h8300_16bit, "renesas,16bit-timer", h8300_16timer_init);
