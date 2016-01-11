/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 by John Crispin <blogic@openwrt.org>
 */

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/mach-ralink/ralink_regs.h>

#define SYSTICK_FREQ		(50 * 1000)

#define SYSTICK_CONFIG		0x00
#define SYSTICK_COMPARE		0x04
#define SYSTICK_COUNT		0x08

/* route systick irq to mips irq 7 instead of the r4k-timer */
#define CFG_EXT_STK_EN		0x2
/* enable the counter */
#define CFG_CNT_EN		0x1

struct systick_device {
	void __iomem *membase;
	struct clock_event_device dev;
	int irq_requested;
	int freq_scale;
};

static int systick_set_oneshot(struct clock_event_device *evt);
static int systick_shutdown(struct clock_event_device *evt);

static int systick_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	struct systick_device *sdev;
	u32 count;

	sdev = container_of(evt, struct systick_device, dev);
	count = ioread32(sdev->membase + SYSTICK_COUNT);
	count = (count + delta) % SYSTICK_FREQ;
	iowrite32(count, sdev->membase + SYSTICK_COMPARE);

	return 0;
}

static void systick_event_handler(struct clock_event_device *dev)
{
	/* noting to do here */
}

static irqreturn_t systick_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *dev = (struct clock_event_device *) dev_id;

	dev->event_handler(dev);

	return IRQ_HANDLED;
}

static struct systick_device systick = {
	.dev = {
		/*
		 * cevt-r4k uses 300, make sure systick
		 * gets used if available
		 */
		.rating			= 310,
		.features		= CLOCK_EVT_FEAT_ONESHOT,
		.set_next_event		= systick_next_event,
		.set_state_shutdown	= systick_shutdown,
		.set_state_oneshot	= systick_set_oneshot,
		.event_handler		= systick_event_handler,
	},
};

static struct irqaction systick_irqaction = {
	.handler = systick_interrupt,
	.flags = IRQF_PERCPU | IRQF_TIMER,
	.dev_id = &systick.dev,
};

static int systick_shutdown(struct clock_event_device *evt)
{
	struct systick_device *sdev;

	sdev = container_of(evt, struct systick_device, dev);

	if (sdev->irq_requested)
		free_irq(systick.dev.irq, &systick_irqaction);
	sdev->irq_requested = 0;
	iowrite32(0, systick.membase + SYSTICK_CONFIG);

	return 0;
}

static int systick_set_oneshot(struct clock_event_device *evt)
{
	struct systick_device *sdev;

	sdev = container_of(evt, struct systick_device, dev);

	if (!sdev->irq_requested)
		setup_irq(systick.dev.irq, &systick_irqaction);
	sdev->irq_requested = 1;
	iowrite32(CFG_EXT_STK_EN | CFG_CNT_EN,
		  systick.membase + SYSTICK_CONFIG);

	return 0;
}

static void __init ralink_systick_init(struct device_node *np)
{
	systick.membase = of_iomap(np, 0);
	if (!systick.membase)
		return;

	systick_irqaction.name = np->name;
	systick.dev.name = np->name;
	clockevents_calc_mult_shift(&systick.dev, SYSTICK_FREQ, 60);
	systick.dev.max_delta_ns = clockevent_delta2ns(0x7fff, &systick.dev);
	systick.dev.min_delta_ns = clockevent_delta2ns(0x3, &systick.dev);
	systick.dev.irq = irq_of_parse_and_map(np, 0);
	if (!systick.dev.irq) {
		pr_err("%s: request_irq failed", np->name);
		return;
	}

	clocksource_mmio_init(systick.membase + SYSTICK_COUNT, np->name,
			SYSTICK_FREQ, 301, 16, clocksource_mmio_readl_up);

	clockevents_register_device(&systick.dev);

	pr_info("%s: running - mult: %d, shift: %d\n",
			np->name, systick.dev.mult, systick.dev.shift);
}

CLOCKSOURCE_OF_DECLARE(systick, "ralink,cevt-systick", ralink_systick_init);
