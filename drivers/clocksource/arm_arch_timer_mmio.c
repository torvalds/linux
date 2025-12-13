// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ARM Generic Memory Mapped Timer support
 *
 *  Split from drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 */

#define pr_fmt(fmt) 	"arch_timer_mmio: " fmt

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <clocksource/arm_arch_timer.h>

#define CNTTIDR		0x08
#define CNTTIDR_VIRT(n)	(BIT(1) << ((n) * 4))

#define CNTACR(n)	(0x40 + ((n) * 4))
#define CNTACR_RPCT	BIT(0)
#define CNTACR_RVCT	BIT(1)
#define CNTACR_RFRQ	BIT(2)
#define CNTACR_RVOFF	BIT(3)
#define CNTACR_RWVT	BIT(4)
#define CNTACR_RWPT	BIT(5)

#define CNTPCT_LO	0x00
#define CNTVCT_LO	0x08
#define CNTFRQ		0x10
#define CNTP_CVAL_LO	0x20
#define CNTP_CTL	0x2c
#define CNTV_CVAL_LO	0x30
#define CNTV_CTL	0x3c

enum arch_timer_access {
	PHYS_ACCESS,
	VIRT_ACCESS,
};

struct arch_timer {
	struct clock_event_device	evt;
	struct clocksource		cs;
	struct arch_timer_mem		*gt_block;
	void __iomem			*base;
	enum arch_timer_access		access;
	u32				rate;
};

#define evt_to_arch_timer(e) container_of(e, struct arch_timer, evt)
#define cs_to_arch_timer(c) container_of(c, struct arch_timer, cs)

static void arch_timer_mmio_write(struct arch_timer *timer,
				  enum arch_timer_reg reg, u64 val)
{
	switch (timer->access) {
	case PHYS_ACCESS:
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed((u32)val, timer->base + CNTP_CTL);
			return;
		case ARCH_TIMER_REG_CVAL:
			/*
			 * Not guaranteed to be atomic, so the timer
			 * must be disabled at this point.
			 */
			writeq_relaxed(val, timer->base + CNTP_CVAL_LO);
			return;
		}
		break;
	case VIRT_ACCESS:
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed((u32)val, timer->base + CNTV_CTL);
			return;
		case ARCH_TIMER_REG_CVAL:
			/* Same restriction as above */
			writeq_relaxed(val, timer->base + CNTV_CVAL_LO);
			return;
		}
		break;
	}

	/* Should never be here */
	WARN_ON_ONCE(1);
}

static u32 arch_timer_mmio_read(struct arch_timer *timer, enum arch_timer_reg reg)
{
	switch (timer->access) {
	case PHYS_ACCESS:
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return readl_relaxed(timer->base + CNTP_CTL);
		default:
			break;
		}
		break;
	case VIRT_ACCESS:
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return readl_relaxed(timer->base + CNTV_CTL);
		default:
			break;
		}
		break;
	}

	/* Should never be here */
	WARN_ON_ONCE(1);
	return 0;
}

static noinstr u64 arch_counter_mmio_get_cnt(struct arch_timer *t)
{
	int offset_lo = t->access == VIRT_ACCESS ? CNTVCT_LO : CNTPCT_LO;
	u32 cnt_lo, cnt_hi, tmp_hi;

	do {
		cnt_hi = __le32_to_cpu((__le32 __force)__raw_readl(t->base + offset_lo + 4));
		cnt_lo = __le32_to_cpu((__le32 __force)__raw_readl(t->base + offset_lo));
		tmp_hi = __le32_to_cpu((__le32 __force)__raw_readl(t->base + offset_lo + 4));
	} while (cnt_hi != tmp_hi);

	return ((u64) cnt_hi << 32) | cnt_lo;
}

static u64 arch_mmio_counter_read(struct clocksource *cs)
{
	struct arch_timer *at = cs_to_arch_timer(cs);

	return arch_counter_mmio_get_cnt(at);
}

static int arch_timer_mmio_shutdown(struct clock_event_device *clk)
{
	struct arch_timer *at = evt_to_arch_timer(clk);
	unsigned long ctrl;

	ctrl = arch_timer_mmio_read(at, ARCH_TIMER_REG_CTRL);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_mmio_write(at, ARCH_TIMER_REG_CTRL, ctrl);

	return 0;
}

static int arch_timer_mmio_set_next_event(unsigned long evt,
					  struct clock_event_device *clk)
{
	struct arch_timer *timer = evt_to_arch_timer(clk);
	unsigned long ctrl;
	u64 cnt;

	ctrl = arch_timer_mmio_read(timer, ARCH_TIMER_REG_CTRL);

	/* Timer must be disabled before programming CVAL */
	if (ctrl & ARCH_TIMER_CTRL_ENABLE) {
		ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
		arch_timer_mmio_write(timer, ARCH_TIMER_REG_CTRL, ctrl);
	}

	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	cnt = arch_counter_mmio_get_cnt(timer);

	arch_timer_mmio_write(timer, ARCH_TIMER_REG_CVAL, evt + cnt);
	arch_timer_mmio_write(timer, ARCH_TIMER_REG_CTRL, ctrl);
	return 0;
}

static irqreturn_t arch_timer_mmio_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct arch_timer *at = evt_to_arch_timer(evt);
	unsigned long ctrl;

	ctrl = arch_timer_mmio_read(at, ARCH_TIMER_REG_CTRL);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_mmio_write(at, ARCH_TIMER_REG_CTRL, ctrl);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct arch_timer_mem_frame *find_best_frame(struct platform_device *pdev)
{
	struct arch_timer_mem_frame *frame, *best_frame = NULL;
	struct arch_timer *at = platform_get_drvdata(pdev);
	void __iomem *cntctlbase;
	u32 cnttidr;

	cntctlbase = ioremap(at->gt_block->cntctlbase, at->gt_block->size);
	if (!cntctlbase) {
		dev_err(&pdev->dev, "Can't map CNTCTLBase @ %pa\n",
			&at->gt_block->cntctlbase);
		return NULL;
	}

	cnttidr = readl_relaxed(cntctlbase + CNTTIDR);

	/*
	 * Try to find a virtual capable frame. Otherwise fall back to a
	 * physical capable frame.
	 */
	for (int i = 0; i < ARCH_TIMER_MEM_MAX_FRAMES; i++) {
		u32 cntacr = CNTACR_RFRQ | CNTACR_RWPT | CNTACR_RPCT |
			     CNTACR_RWVT | CNTACR_RVOFF | CNTACR_RVCT;

		frame = &at->gt_block->frame[i];
		if (!frame->valid)
			continue;

		/* Try enabling everything, and see what sticks */
		writel_relaxed(cntacr, cntctlbase + CNTACR(i));
		cntacr = readl_relaxed(cntctlbase + CNTACR(i));

		/* Pick a suitable frame for which we have an IRQ */
		if ((cnttidr & CNTTIDR_VIRT(i)) &&
		    !(~cntacr & (CNTACR_RWVT | CNTACR_RVCT)) &&
		    frame->virt_irq) {
			best_frame = frame;
			at->access = VIRT_ACCESS;
			break;
		}

		if ((~cntacr & (CNTACR_RWPT | CNTACR_RPCT)) ||
		     !frame->phys_irq)
			continue;

		at->access = PHYS_ACCESS;
		best_frame = frame;
	}

	iounmap(cntctlbase);

	return best_frame;
}

static void arch_timer_mmio_setup(struct arch_timer *at, int irq)
{
	at->evt = (struct clock_event_device) {
		.features		   = (CLOCK_EVT_FEAT_ONESHOT |
					      CLOCK_EVT_FEAT_DYNIRQ),
		.name			   = "arch_mem_timer",
		.rating			   = 400,
		.cpumask		   = cpu_possible_mask,
		.irq 			   = irq,
		.set_next_event		   = arch_timer_mmio_set_next_event,
		.set_state_oneshot_stopped = arch_timer_mmio_shutdown,
		.set_state_shutdown	   = arch_timer_mmio_shutdown,
	};

	at->evt.set_state_shutdown(&at->evt);

	clockevents_config_and_register(&at->evt, at->rate, 0xf,
					(unsigned long)CLOCKSOURCE_MASK(56));

	enable_irq(at->evt.irq);

	at->cs = (struct clocksource) {
		.name	= "arch_mmio_counter",
		.rating	= 300,
		.read	= arch_mmio_counter_read,
		.mask	= CLOCKSOURCE_MASK(56),
		.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	};

	clocksource_register_hz(&at->cs, at->rate);
}

static int arch_timer_mmio_frame_register(struct platform_device *pdev,
					  struct arch_timer_mem_frame *frame)
{
	struct arch_timer *at = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	int ret, irq;
	u32 rate;

	if (!devm_request_mem_region(&pdev->dev, frame->cntbase, frame->size,
				     "arch_mem_timer"))
		return -EBUSY;

	at->base = devm_ioremap(&pdev->dev, frame->cntbase, frame->size);
	if (!at->base) {
		dev_err(&pdev->dev, "Can't map frame's registers\n");
		return -ENXIO;
	}

	/*
	 * Allow "clock-frequency" to override the probed rate. If neither
	 * lead to something useful, use the CPU timer frequency as the
	 * fallback. The nice thing about that last point is that we woudn't
	 * made it here if we didn't have a valid frequency.
	 */
	rate = readl_relaxed(at->base + CNTFRQ);

	if (!np || of_property_read_u32(np, "clock-frequency", &at->rate))
		at->rate = rate;

	if (!at->rate)
		at->rate = arch_timer_get_rate();

	irq = at->access == VIRT_ACCESS ? frame->virt_irq : frame->phys_irq;
	ret = devm_request_irq(&pdev->dev, irq, arch_timer_mmio_handler,
			       IRQF_TIMER | IRQF_NO_AUTOEN, "arch_mem_timer",
			       &at->evt);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request mem timer irq\n");
		return ret;
	}

	/* Afer this point, we're not allowed to fail anymore */
	arch_timer_mmio_setup(at, irq);
	return 0;
}

static int of_populate_gt_block(struct platform_device *pdev,
				struct arch_timer *at)
{
	struct resource res;

	if (of_address_to_resource(pdev->dev.of_node, 0, &res))
		return -EINVAL;

	at->gt_block->cntctlbase = res.start;
	at->gt_block->size = resource_size(&res);

	for_each_available_child_of_node_scoped(pdev->dev.of_node, frame_node) {
		struct arch_timer_mem_frame *frame;
		u32 n;

		if (of_property_read_u32(frame_node, "frame-number", &n)) {
			dev_err(&pdev->dev, FW_BUG "Missing frame-number\n");
			return -EINVAL;
		}
		if (n >= ARCH_TIMER_MEM_MAX_FRAMES) {
			dev_err(&pdev->dev,
				FW_BUG "Wrong frame-number, only 0-%u are permitted\n",
			       ARCH_TIMER_MEM_MAX_FRAMES - 1);
			return -EINVAL;
		}

		frame = &at->gt_block->frame[n];

		if (frame->valid) {
			dev_err(&pdev->dev, FW_BUG "Duplicated frame-number\n");
			return -EINVAL;
		}

		if (of_address_to_resource(frame_node, 0, &res))
			return -EINVAL;

		frame->cntbase = res.start;
		frame->size = resource_size(&res);

		frame->phys_irq = irq_of_parse_and_map(frame_node, 0);
		frame->virt_irq = irq_of_parse_and_map(frame_node, 1);

		frame->valid = true;
	}

	return 0;
}

static int arch_timer_mmio_probe(struct platform_device *pdev)
{
	struct arch_timer_mem_frame *frame;
	struct arch_timer *at;
	struct device_node *np;
	int ret;

	np = pdev->dev.of_node;

	at = devm_kmalloc(&pdev->dev, sizeof(*at), GFP_KERNEL | __GFP_ZERO);
	if (!at)
		return -ENOMEM;

	if (np) {
		at->gt_block = devm_kmalloc(&pdev->dev, sizeof(*at->gt_block),
					    GFP_KERNEL | __GFP_ZERO);
		if (!at->gt_block)
			return -ENOMEM;
		ret = of_populate_gt_block(pdev, at);
		if (ret)
			return ret;
	} else {
		at->gt_block = dev_get_platdata(&pdev->dev);
	}

	platform_set_drvdata(pdev, at);

	frame = find_best_frame(pdev);
	if (!frame) {
		dev_err(&pdev->dev,
			"Unable to find a suitable frame in timer @ %pa\n",
			&at->gt_block->cntctlbase);
		return -EINVAL;
	}

	ret = arch_timer_mmio_frame_register(pdev, frame);
	if (!ret)
		dev_info(&pdev->dev,
			 "mmio timer running at %lu.%02luMHz (%s)\n",
			 (unsigned long)at->rate / 1000000,
			 (unsigned long)(at->rate / 10000) % 100,
			 at->access == VIRT_ACCESS ? "virt" : "phys");

	return ret;
}

static const struct of_device_id arch_timer_mmio_of_table[] = {
	{ .compatible = "arm,armv7-timer-mem", },
	{}
};

static struct platform_driver arch_timer_mmio_drv = {
	.driver	= {
		.name = "arch-timer-mmio",
		.of_match_table	= arch_timer_mmio_of_table,
	},
	.probe	= arch_timer_mmio_probe,
};
builtin_platform_driver(arch_timer_mmio_drv);

static struct platform_driver arch_timer_mmio_acpi_drv = {
	.driver	= {
		.name = "gtdt-arm-mmio-timer",
	},
	.probe	= arch_timer_mmio_probe,
};
builtin_platform_driver(arch_timer_mmio_acpi_drv);
