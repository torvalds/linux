// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP 1588 clock for Freescale QorIQ 1588 timer
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <linux/fsl/ptp_qoriq.h>

/*
 * Register access functions
 */

/* Caller must hold ptp_qoriq->lock. */
static u64 tmr_cnt_read(struct ptp_qoriq *ptp_qoriq)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u64 ns;
	u32 lo, hi;

	lo = ptp_qoriq->read(&regs->ctrl_regs->tmr_cnt_l);
	hi = ptp_qoriq->read(&regs->ctrl_regs->tmr_cnt_h);
	ns = ((u64) hi) << 32;
	ns |= lo;
	return ns;
}

/* Caller must hold ptp_qoriq->lock. */
static void tmr_cnt_write(struct ptp_qoriq *ptp_qoriq, u64 ns)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 hi = ns >> 32;
	u32 lo = ns & 0xffffffff;

	ptp_qoriq->write(&regs->ctrl_regs->tmr_cnt_l, lo);
	ptp_qoriq->write(&regs->ctrl_regs->tmr_cnt_h, hi);
}

static u64 tmr_offset_read(struct ptp_qoriq *ptp_qoriq)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 lo, hi;
	u64 ns;

	lo = ptp_qoriq->read(&regs->ctrl_regs->tmroff_l);
	hi = ptp_qoriq->read(&regs->ctrl_regs->tmroff_h);
	ns = ((u64) hi) << 32;
	ns |= lo;
	return ns;
}

static void tmr_offset_write(struct ptp_qoriq *ptp_qoriq, u64 delta_ns)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 lo = delta_ns & 0xffffffff;
	u32 hi = delta_ns >> 32;

	ptp_qoriq->write(&regs->ctrl_regs->tmroff_l, lo);
	ptp_qoriq->write(&regs->ctrl_regs->tmroff_h, hi);
}

/* Caller must hold ptp_qoriq->lock. */
static void set_alarm(struct ptp_qoriq *ptp_qoriq)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u64 ns;
	u32 lo, hi;

	ns = tmr_cnt_read(ptp_qoriq) + tmr_offset_read(ptp_qoriq)
				     + 1500000000ULL;

	ns = div_u64(ns, 1000000000UL) * 1000000000ULL;
	ns -= ptp_qoriq->tclk_period;
	hi = ns >> 32;
	lo = ns & 0xffffffff;
	ptp_qoriq->write(&regs->alarm_regs->tmr_alarm1_l, lo);
	ptp_qoriq->write(&regs->alarm_regs->tmr_alarm1_h, hi);
}

/* Caller must hold ptp_qoriq->lock. */
static void set_fipers(struct ptp_qoriq *ptp_qoriq)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;

	set_alarm(ptp_qoriq);
	ptp_qoriq->write(&regs->fiper_regs->tmr_fiper1, ptp_qoriq->tmr_fiper1);
	ptp_qoriq->write(&regs->fiper_regs->tmr_fiper2, ptp_qoriq->tmr_fiper2);

	if (ptp_qoriq->fiper3_support)
		ptp_qoriq->write(&regs->fiper_regs->tmr_fiper3,
				 ptp_qoriq->tmr_fiper3);
}

int extts_clean_up(struct ptp_qoriq *ptp_qoriq, int index, bool update_event)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	struct ptp_clock_event event;
	void __iomem *reg_etts_l;
	void __iomem *reg_etts_h;
	u32 valid, lo, hi;

	switch (index) {
	case 0:
		valid = ETS1_VLD;
		reg_etts_l = &regs->etts_regs->tmr_etts1_l;
		reg_etts_h = &regs->etts_regs->tmr_etts1_h;
		break;
	case 1:
		valid = ETS2_VLD;
		reg_etts_l = &regs->etts_regs->tmr_etts2_l;
		reg_etts_h = &regs->etts_regs->tmr_etts2_h;
		break;
	default:
		return -EINVAL;
	}

	event.type = PTP_CLOCK_EXTTS;
	event.index = index;

	if (ptp_qoriq->extts_fifo_support)
		if (!(ptp_qoriq->read(&regs->ctrl_regs->tmr_stat) & valid))
			return 0;

	do {
		lo = ptp_qoriq->read(reg_etts_l);
		hi = ptp_qoriq->read(reg_etts_h);

		if (update_event) {
			event.timestamp = ((u64) hi) << 32;
			event.timestamp |= lo;
			ptp_clock_event(ptp_qoriq->clock, &event);
		}

		if (!ptp_qoriq->extts_fifo_support)
			break;
	} while (ptp_qoriq->read(&regs->ctrl_regs->tmr_stat) & valid);

	return 0;
}
EXPORT_SYMBOL_GPL(extts_clean_up);

/*
 * Interrupt service routine
 */

irqreturn_t ptp_qoriq_isr(int irq, void *priv)
{
	struct ptp_qoriq *ptp_qoriq = priv;
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	struct ptp_clock_event event;
	u32 ack = 0, mask, val, irqs;

	spin_lock(&ptp_qoriq->lock);

	val = ptp_qoriq->read(&regs->ctrl_regs->tmr_tevent);
	mask = ptp_qoriq->read(&regs->ctrl_regs->tmr_temask);

	spin_unlock(&ptp_qoriq->lock);

	irqs = val & mask;

	if (irqs & ETS1) {
		ack |= ETS1;
		extts_clean_up(ptp_qoriq, 0, true);
	}

	if (irqs & ETS2) {
		ack |= ETS2;
		extts_clean_up(ptp_qoriq, 1, true);
	}

	if (irqs & PP1) {
		ack |= PP1;
		event.type = PTP_CLOCK_PPS;
		ptp_clock_event(ptp_qoriq->clock, &event);
	}

	if (ack) {
		ptp_qoriq->write(&regs->ctrl_regs->tmr_tevent, ack);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_isr);

/*
 * PTP clock operations
 */

int ptp_qoriq_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	u64 adj, diff;
	u32 tmr_add;
	int neg_adj = 0;
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}
	tmr_add = ptp_qoriq->tmr_add;
	adj = tmr_add;

	/*
	 * Calculate diff and round() to the nearest integer
	 *
	 * diff = adj * (ppb / 1000000000)
	 *      = adj * scaled_ppm / 65536000000
	 */
	diff = mul_u64_u64_div_u64(adj, scaled_ppm, 32768000000);
	diff = DIV64_U64_ROUND_UP(diff, 2);

	tmr_add = neg_adj ? tmr_add - diff : tmr_add + diff;
	ptp_qoriq->write(&regs->ctrl_regs->tmr_add, tmr_add);

	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_adjfine);

int ptp_qoriq_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);
	s64 now, curr_delta;
	unsigned long flags;

	spin_lock_irqsave(&ptp_qoriq->lock, flags);

	/* On LS1021A, eTSEC2 and eTSEC3 do not take into account the TMR_OFF
	 * adjustment
	 */
	if (ptp_qoriq->etsec) {
		now = tmr_cnt_read(ptp_qoriq);
		now += delta;
		tmr_cnt_write(ptp_qoriq, now);
	} else {
		curr_delta = tmr_offset_read(ptp_qoriq);
		curr_delta += delta;
		tmr_offset_write(ptp_qoriq, curr_delta);
	}
	set_fipers(ptp_qoriq);

	spin_unlock_irqrestore(&ptp_qoriq->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_adjtime);

int ptp_qoriq_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);

	spin_lock_irqsave(&ptp_qoriq->lock, flags);

	ns = tmr_cnt_read(ptp_qoriq) + tmr_offset_read(ptp_qoriq);

	spin_unlock_irqrestore(&ptp_qoriq->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_gettime);

int ptp_qoriq_settime(struct ptp_clock_info *ptp,
		      const struct timespec64 *ts)
{
	u64 ns;
	unsigned long flags;
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);

	ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&ptp_qoriq->lock, flags);

	tmr_offset_write(ptp_qoriq, 0);
	tmr_cnt_write(ptp_qoriq, ns);
	set_fipers(ptp_qoriq);

	spin_unlock_irqrestore(&ptp_qoriq->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_settime);

int ptp_qoriq_enable(struct ptp_clock_info *ptp,
		     struct ptp_clock_request *rq, int on)
{
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	unsigned long flags;
	u32 bit, mask = 0;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		switch (rq->extts.index) {
		case 0:
			bit = ETS1EN;
			break;
		case 1:
			bit = ETS2EN;
			break;
		default:
			return -EINVAL;
		}

		if (on)
			extts_clean_up(ptp_qoriq, rq->extts.index, false);

		break;
	case PTP_CLK_REQ_PPS:
		bit = PP1EN;
		break;
	default:
		return -EOPNOTSUPP;
	}

	spin_lock_irqsave(&ptp_qoriq->lock, flags);

	mask = ptp_qoriq->read(&regs->ctrl_regs->tmr_temask);
	if (on) {
		mask |= bit;
		ptp_qoriq->write(&regs->ctrl_regs->tmr_tevent, bit);
	} else {
		mask &= ~bit;
	}

	ptp_qoriq->write(&regs->ctrl_regs->tmr_temask, mask);

	spin_unlock_irqrestore(&ptp_qoriq->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_enable);

static const struct ptp_clock_info ptp_qoriq_caps = {
	.owner		= THIS_MODULE,
	.name		= "qoriq ptp clock",
	.max_adj	= 512000,
	.n_alarm	= 0,
	.n_ext_ts	= N_EXT_TS,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 1,
	.adjfine	= ptp_qoriq_adjfine,
	.adjtime	= ptp_qoriq_adjtime,
	.gettime64	= ptp_qoriq_gettime,
	.settime64	= ptp_qoriq_settime,
	.enable		= ptp_qoriq_enable,
};

/**
 * ptp_qoriq_nominal_freq - calculate nominal frequency according to
 *			    reference clock frequency
 *
 * @clk_src: reference clock frequency
 *
 * The nominal frequency is the desired clock frequency.
 * It should be less than the reference clock frequency.
 * It should be a factor of 1000MHz.
 *
 * Return the nominal frequency
 */
static u32 ptp_qoriq_nominal_freq(u32 clk_src)
{
	u32 remainder = 0;

	clk_src /= 1000000;
	remainder = clk_src % 100;
	if (remainder) {
		clk_src -= remainder;
		clk_src += 100;
	}

	do {
		clk_src -= 100;

	} while (1000 % clk_src);

	return clk_src * 1000000;
}

/**
 * ptp_qoriq_auto_config - calculate a set of default configurations
 *
 * @ptp_qoriq: pointer to ptp_qoriq
 * @node: pointer to device_node
 *
 * If below dts properties are not provided, this function will be
 * called to calculate a set of default configurations for them.
 *   "fsl,tclk-period"
 *   "fsl,tmr-prsc"
 *   "fsl,tmr-add"
 *   "fsl,tmr-fiper1"
 *   "fsl,tmr-fiper2"
 *   "fsl,tmr-fiper3" (required only for DPAA2 and ENETC hardware)
 *   "fsl,max-adj"
 *
 * Return 0 if success
 */
static int ptp_qoriq_auto_config(struct ptp_qoriq *ptp_qoriq,
				 struct device_node *node)
{
	struct clk *clk;
	u64 freq_comp;
	u64 max_adj;
	u32 nominal_freq;
	u32 remainder = 0;
	u32 clk_src = 0;

	ptp_qoriq->cksel = DEFAULT_CKSEL;

	clk = of_clk_get(node, 0);
	if (!IS_ERR(clk)) {
		clk_src = clk_get_rate(clk);
		clk_put(clk);
	}

	if (clk_src <= 100000000UL) {
		pr_err("error reference clock value, or lower than 100MHz\n");
		return -EINVAL;
	}

	nominal_freq = ptp_qoriq_nominal_freq(clk_src);
	if (!nominal_freq)
		return -EINVAL;

	ptp_qoriq->tclk_period = 1000000000UL / nominal_freq;
	ptp_qoriq->tmr_prsc = DEFAULT_TMR_PRSC;

	/* Calculate initial frequency compensation value for TMR_ADD register.
	 * freq_comp = ceil(2^32 / freq_ratio)
	 * freq_ratio = reference_clock_freq / nominal_freq
	 */
	freq_comp = ((u64)1 << 32) * nominal_freq;
	freq_comp = div_u64_rem(freq_comp, clk_src, &remainder);
	if (remainder)
		freq_comp++;

	ptp_qoriq->tmr_add = freq_comp;
	ptp_qoriq->tmr_fiper1 = DEFAULT_FIPER1_PERIOD - ptp_qoriq->tclk_period;
	ptp_qoriq->tmr_fiper2 = DEFAULT_FIPER2_PERIOD - ptp_qoriq->tclk_period;
	ptp_qoriq->tmr_fiper3 = DEFAULT_FIPER3_PERIOD - ptp_qoriq->tclk_period;

	/* max_adj = 1000000000 * (freq_ratio - 1.0) - 1
	 * freq_ratio = reference_clock_freq / nominal_freq
	 */
	max_adj = 1000000000ULL * (clk_src - nominal_freq);
	max_adj = div_u64(max_adj, nominal_freq) - 1;
	ptp_qoriq->caps.max_adj = max_adj;

	return 0;
}

int ptp_qoriq_init(struct ptp_qoriq *ptp_qoriq, void __iomem *base,
		   const struct ptp_clock_info *caps)
{
	struct device_node *node = ptp_qoriq->dev->of_node;
	struct ptp_qoriq_registers *regs;
	struct timespec64 now;
	unsigned long flags;
	u32 tmr_ctrl;

	if (!node)
		return -ENODEV;

	ptp_qoriq->base = base;
	ptp_qoriq->caps = *caps;

	if (of_property_read_u32(node, "fsl,cksel", &ptp_qoriq->cksel))
		ptp_qoriq->cksel = DEFAULT_CKSEL;

	if (of_property_read_bool(node, "fsl,extts-fifo"))
		ptp_qoriq->extts_fifo_support = true;
	else
		ptp_qoriq->extts_fifo_support = false;

	if (of_device_is_compatible(node, "fsl,dpaa2-ptp") ||
	    of_device_is_compatible(node, "fsl,enetc-ptp"))
		ptp_qoriq->fiper3_support = true;

	if (of_property_read_u32(node,
				 "fsl,tclk-period", &ptp_qoriq->tclk_period) ||
	    of_property_read_u32(node,
				 "fsl,tmr-prsc", &ptp_qoriq->tmr_prsc) ||
	    of_property_read_u32(node,
				 "fsl,tmr-add", &ptp_qoriq->tmr_add) ||
	    of_property_read_u32(node,
				 "fsl,tmr-fiper1", &ptp_qoriq->tmr_fiper1) ||
	    of_property_read_u32(node,
				 "fsl,tmr-fiper2", &ptp_qoriq->tmr_fiper2) ||
	    of_property_read_u32(node,
				 "fsl,max-adj", &ptp_qoriq->caps.max_adj) ||
	    (ptp_qoriq->fiper3_support &&
	     of_property_read_u32(node, "fsl,tmr-fiper3",
				  &ptp_qoriq->tmr_fiper3))) {
		pr_warn("device tree node missing required elements, try automatic configuration\n");

		if (ptp_qoriq_auto_config(ptp_qoriq, node))
			return -ENODEV;
	}

	if (of_property_read_bool(node, "little-endian")) {
		ptp_qoriq->read = qoriq_read_le;
		ptp_qoriq->write = qoriq_write_le;
	} else {
		ptp_qoriq->read = qoriq_read_be;
		ptp_qoriq->write = qoriq_write_be;
	}

	/* The eTSEC uses differnt memory map with DPAA/ENETC */
	if (of_device_is_compatible(node, "fsl,etsec-ptp")) {
		ptp_qoriq->etsec = true;
		ptp_qoriq->regs.ctrl_regs = base + ETSEC_CTRL_REGS_OFFSET;
		ptp_qoriq->regs.alarm_regs = base + ETSEC_ALARM_REGS_OFFSET;
		ptp_qoriq->regs.fiper_regs = base + ETSEC_FIPER_REGS_OFFSET;
		ptp_qoriq->regs.etts_regs = base + ETSEC_ETTS_REGS_OFFSET;
	} else {
		ptp_qoriq->regs.ctrl_regs = base + CTRL_REGS_OFFSET;
		ptp_qoriq->regs.alarm_regs = base + ALARM_REGS_OFFSET;
		ptp_qoriq->regs.fiper_regs = base + FIPER_REGS_OFFSET;
		ptp_qoriq->regs.etts_regs = base + ETTS_REGS_OFFSET;
	}

	spin_lock_init(&ptp_qoriq->lock);

	ktime_get_real_ts64(&now);
	ptp_qoriq_settime(&ptp_qoriq->caps, &now);

	tmr_ctrl =
	  (ptp_qoriq->tclk_period & TCLK_PERIOD_MASK) << TCLK_PERIOD_SHIFT |
	  (ptp_qoriq->cksel & CKSEL_MASK) << CKSEL_SHIFT;

	spin_lock_irqsave(&ptp_qoriq->lock, flags);

	regs = &ptp_qoriq->regs;
	ptp_qoriq->write(&regs->ctrl_regs->tmr_ctrl, tmr_ctrl);
	ptp_qoriq->write(&regs->ctrl_regs->tmr_add, ptp_qoriq->tmr_add);
	ptp_qoriq->write(&regs->ctrl_regs->tmr_prsc, ptp_qoriq->tmr_prsc);
	ptp_qoriq->write(&regs->fiper_regs->tmr_fiper1, ptp_qoriq->tmr_fiper1);
	ptp_qoriq->write(&regs->fiper_regs->tmr_fiper2, ptp_qoriq->tmr_fiper2);

	if (ptp_qoriq->fiper3_support)
		ptp_qoriq->write(&regs->fiper_regs->tmr_fiper3,
				 ptp_qoriq->tmr_fiper3);

	set_alarm(ptp_qoriq);
	ptp_qoriq->write(&regs->ctrl_regs->tmr_ctrl,
			 tmr_ctrl|FIPERST|RTPE|TE|FRD);

	spin_unlock_irqrestore(&ptp_qoriq->lock, flags);

	ptp_qoriq->clock = ptp_clock_register(&ptp_qoriq->caps, ptp_qoriq->dev);
	if (IS_ERR(ptp_qoriq->clock))
		return PTR_ERR(ptp_qoriq->clock);

	ptp_qoriq->phc_index = ptp_clock_index(ptp_qoriq->clock);
	ptp_qoriq_create_debugfs(ptp_qoriq);
	return 0;
}
EXPORT_SYMBOL_GPL(ptp_qoriq_init);

void ptp_qoriq_free(struct ptp_qoriq *ptp_qoriq)
{
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;

	ptp_qoriq->write(&regs->ctrl_regs->tmr_temask, 0);
	ptp_qoriq->write(&regs->ctrl_regs->tmr_ctrl,   0);

	ptp_qoriq_remove_debugfs(ptp_qoriq);
	ptp_clock_unregister(ptp_qoriq->clock);
	iounmap(ptp_qoriq->base);
	free_irq(ptp_qoriq->irq, ptp_qoriq);
}
EXPORT_SYMBOL_GPL(ptp_qoriq_free);

static int ptp_qoriq_probe(struct platform_device *dev)
{
	struct ptp_qoriq *ptp_qoriq;
	int err = -ENOMEM;
	void __iomem *base;

	ptp_qoriq = kzalloc(sizeof(*ptp_qoriq), GFP_KERNEL);
	if (!ptp_qoriq)
		goto no_memory;

	ptp_qoriq->dev = &dev->dev;

	err = -ENODEV;

	ptp_qoriq->irq = platform_get_irq(dev, 0);
	if (ptp_qoriq->irq < 0) {
		pr_err("irq not in device tree\n");
		goto no_node;
	}
	if (request_irq(ptp_qoriq->irq, ptp_qoriq_isr, IRQF_SHARED,
			DRIVER, ptp_qoriq)) {
		pr_err("request_irq failed\n");
		goto no_node;
	}

	ptp_qoriq->rsrc = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!ptp_qoriq->rsrc) {
		pr_err("no resource\n");
		goto no_resource;
	}
	if (request_resource(&iomem_resource, ptp_qoriq->rsrc)) {
		pr_err("resource busy\n");
		goto no_resource;
	}

	base = ioremap(ptp_qoriq->rsrc->start,
		       resource_size(ptp_qoriq->rsrc));
	if (!base) {
		pr_err("ioremap ptp registers failed\n");
		goto no_ioremap;
	}

	err = ptp_qoriq_init(ptp_qoriq, base, &ptp_qoriq_caps);
	if (err)
		goto no_clock;

	platform_set_drvdata(dev, ptp_qoriq);
	return 0;

no_clock:
	iounmap(base);
no_ioremap:
	release_resource(ptp_qoriq->rsrc);
no_resource:
	free_irq(ptp_qoriq->irq, ptp_qoriq);
no_node:
	kfree(ptp_qoriq);
no_memory:
	return err;
}

static int ptp_qoriq_remove(struct platform_device *dev)
{
	struct ptp_qoriq *ptp_qoriq = platform_get_drvdata(dev);

	ptp_qoriq_free(ptp_qoriq);
	release_resource(ptp_qoriq->rsrc);
	kfree(ptp_qoriq);
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "fsl,etsec-ptp" },
	{ .compatible = "fsl,fman-ptp-timer" },
	{},
};
MODULE_DEVICE_TABLE(of, match_table);

static struct platform_driver ptp_qoriq_driver = {
	.driver = {
		.name		= "ptp_qoriq",
		.of_match_table	= match_table,
	},
	.probe       = ptp_qoriq_probe,
	.remove      = ptp_qoriq_remove,
};

module_platform_driver(ptp_qoriq_driver);

MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
MODULE_DESCRIPTION("PTP clock for Freescale QorIQ 1588 timer");
MODULE_LICENSE("GPL");
