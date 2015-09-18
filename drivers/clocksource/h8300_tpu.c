/*
 *  H8/300 TPU Driver
 *
 *  Copyright 2015 Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/irq.h>

#define TCR	0
#define TMDR	1
#define TIOR	2
#define TER	4
#define TSR	5
#define TCNT	6
#define TGRA	8
#define TGRB	10
#define TGRC	12
#define TGRD	14

struct tpu_priv {
	struct platform_device *pdev;
	struct clocksource cs;
	struct clk *clk;
	unsigned long mapbase1;
	unsigned long mapbase2;
	raw_spinlock_t lock;
	unsigned int cs_enabled;
};

static inline unsigned long read_tcnt32(struct tpu_priv *p)
{
	unsigned long tcnt;

	tcnt = ctrl_inw(p->mapbase1 + TCNT) << 16;
	tcnt |= ctrl_inw(p->mapbase2 + TCNT);
	return tcnt;
}

static int tpu_get_counter(struct tpu_priv *p, unsigned long long *val)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = ctrl_inb(p->mapbase1 + TSR) & 0x10;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = read_tcnt32(p);
		v2 = read_tcnt32(p);
		v3 = read_tcnt32(p);
		o1 = ctrl_inb(p->mapbase1 + TSR) & 0x10;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	*val = v2;
	return o1;
}

static inline struct tpu_priv *cs_to_priv(struct clocksource *cs)
{
	return container_of(cs, struct tpu_priv, cs);
}

static cycle_t tpu_clocksource_read(struct clocksource *cs)
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

	ctrl_outw(0, p->mapbase1 + TCNT);
	ctrl_outw(0, p->mapbase2 + TCNT);
	ctrl_outb(0x0f, p->mapbase1 + TCR);
	ctrl_outb(0x03, p->mapbase2 + TCR);

	p->cs_enabled = true;
	return 0;
}

static void tpu_clocksource_disable(struct clocksource *cs)
{
	struct tpu_priv *p = cs_to_priv(cs);

	WARN_ON(!p->cs_enabled);

	ctrl_outb(0, p->mapbase1 + TCR);
	ctrl_outb(0, p->mapbase2 + TCR);
	p->cs_enabled = false;
}

#define CH_L 0
#define CH_H 1

static int __init tpu_setup(struct tpu_priv *p, struct platform_device *pdev)
{
	struct resource *res[2];

	memset(p, 0, sizeof(*p));
	p->pdev = pdev;

	res[CH_L] = platform_get_resource(p->pdev, IORESOURCE_MEM, CH_L);
	res[CH_H] = platform_get_resource(p->pdev, IORESOURCE_MEM, CH_H);
	if (!res[CH_L] || !res[CH_H]) {
		dev_err(&p->pdev->dev, "failed to get I/O memory\n");
		return -ENXIO;
	}

	p->clk = clk_get(&p->pdev->dev, "fck");
	if (IS_ERR(p->clk)) {
		dev_err(&p->pdev->dev, "can't get clk\n");
		return PTR_ERR(p->clk);
	}

	p->mapbase1 = res[CH_L]->start;
	p->mapbase2 = res[CH_H]->start;

	p->cs.name = pdev->name;
	p->cs.rating = 200;
	p->cs.read = tpu_clocksource_read;
	p->cs.enable = tpu_clocksource_enable;
	p->cs.disable = tpu_clocksource_disable;
	p->cs.mask = CLOCKSOURCE_MASK(sizeof(unsigned long) * 8);
	p->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	clocksource_register_hz(&p->cs, clk_get_rate(p->clk) / 64);
	platform_set_drvdata(pdev, p);

	return 0;
}

static int tpu_probe(struct platform_device *pdev)
{
	struct tpu_priv *p = platform_get_drvdata(pdev);

	if (p) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		return 0;
	}

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	return tpu_setup(p, pdev);
}

static int tpu_remove(struct platform_device *pdev)
{
	return -EBUSY;
}

static const struct of_device_id tpu_of_table[] = {
	{ .compatible = "renesas,tpu" },
	{ }
};

static struct platform_driver tpu_driver = {
	.probe		= tpu_probe,
	.remove		= tpu_remove,
	.driver		= {
		.name	= "h8s-tpu",
		.of_match_table = of_match_ptr(tpu_of_table),
	}
};

static int __init tpu_init(void)
{
	return platform_driver_register(&tpu_driver);
}

static void __exit tpu_exit(void)
{
	platform_driver_unregister(&tpu_driver);
}

subsys_initcall(tpu_init);
module_exit(tpu_exit);
MODULE_AUTHOR("Yoshinori Sato");
MODULE_DESCRIPTION("H8S Timer Pulse Unit Driver");
MODULE_LICENSE("GPL v2");
