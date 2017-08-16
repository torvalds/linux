/*
 *  H8S TPU Driver
 *
 *  Copyright 2015 Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TCR	0x0
#define TSR	0x5
#define TCNT	0x6

#define TCFV	0x10

struct tpu_priv {
	struct clocksource cs;
	void __iomem *mapbase1;
	void __iomem *mapbase2;
	raw_spinlock_t lock;
	unsigned int cs_enabled;
};

static inline unsigned long read_tcnt32(struct tpu_priv *p)
{
	unsigned long tcnt;

	tcnt = ioread16be(p->mapbase1 + TCNT) << 16;
	tcnt |= ioread16be(p->mapbase2 + TCNT);
	return tcnt;
}

static int tpu_get_counter(struct tpu_priv *p, unsigned long long *val)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = ioread8(p->mapbase1 + TSR) & TCFV;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = read_tcnt32(p);
		v2 = read_tcnt32(p);
		v3 = read_tcnt32(p);
		o1 = ioread8(p->mapbase1 + TSR) & TCFV;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	*val = v2;
	return o1;
}

static inline struct tpu_priv *cs_to_priv(struct clocksource *cs)
{
	return container_of(cs, struct tpu_priv, cs);
}

static u64 tpu_clocksource_read(struct clocksource *cs)
{
	struct tpu_priv *p = cs_to_priv(cs);
	unsigned long flags;
	unsigned long long value;

	raw_spin_lock_irqsave(&p->lock, flags);
	if (tpu_get_counter(p, &value))
		value += 0x100000000;
	raw_spin_unlock_irqrestore(&p->lock, flags);

	return value;
}

static int tpu_clocksource_enable(struct clocksource *cs)
{
	struct tpu_priv *p = cs_to_priv(cs);

	WARN_ON(p->cs_enabled);

	iowrite16be(0, p->mapbase1 + TCNT);
	iowrite16be(0, p->mapbase2 + TCNT);
	iowrite8(0x0f, p->mapbase1 + TCR);
	iowrite8(0x03, p->mapbase2 + TCR);

	p->cs_enabled = true;
	return 0;
}

static void tpu_clocksource_disable(struct clocksource *cs)
{
	struct tpu_priv *p = cs_to_priv(cs);

	WARN_ON(!p->cs_enabled);

	iowrite8(0, p->mapbase1 + TCR);
	iowrite8(0, p->mapbase2 + TCR);
	p->cs_enabled = false;
}

static struct tpu_priv tpu_priv = {
	.cs = {
		.name = "H8S_TPU",
		.rating = 200,
		.read = tpu_clocksource_read,
		.enable = tpu_clocksource_enable,
		.disable = tpu_clocksource_disable,
		.mask = CLOCKSOURCE_MASK(sizeof(unsigned long) * 8),
		.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	},
};

#define CH_L 0
#define CH_H 1

static int __init h8300_tpu_init(struct device_node *node)
{
	void __iomem *base[2];
	struct clk *clk;
	int ret = -ENXIO;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("failed to get clock for clocksource\n");
		return PTR_ERR(clk);
	}

	base[CH_L] = of_iomap(node, CH_L);
	if (!base[CH_L]) {
		pr_err("failed to map registers for clocksource\n");
		goto free_clk;
	}
	base[CH_H] = of_iomap(node, CH_H);
	if (!base[CH_H]) {
		pr_err("failed to map registers for clocksource\n");
		goto unmap_L;
	}

	tpu_priv.mapbase1 = base[CH_L];
	tpu_priv.mapbase2 = base[CH_H];

	return clocksource_register_hz(&tpu_priv.cs, clk_get_rate(clk) / 64);

unmap_L:
	iounmap(base[CH_H]);
free_clk:
	clk_put(clk);
	return ret;
}

TIMER_OF_DECLARE(h8300_tpu, "renesas,tpu", h8300_tpu_init);
