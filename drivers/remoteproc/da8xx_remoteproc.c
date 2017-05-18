/*
 * Remote processor machine-specific module for DA8XX
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>

#include <mach/clock.h>   /* for davinci_clk_reset_assert/deassert() */

#include "remoteproc_internal.h"

static char *da8xx_fw_name;
module_param(da8xx_fw_name, charp, S_IRUGO);
MODULE_PARM_DESC(da8xx_fw_name,
		 "Name of DSP firmware file in /lib/firmware (if not specified defaults to 'rproc-dsp-fw')");

/*
 * OMAP-L138 Technical References:
 * http://www.ti.com/product/omap-l138
 */
#define SYSCFG_CHIPSIG0 BIT(0)
#define SYSCFG_CHIPSIG1 BIT(1)
#define SYSCFG_CHIPSIG2 BIT(2)
#define SYSCFG_CHIPSIG3 BIT(3)
#define SYSCFG_CHIPSIG4 BIT(4)

/**
 * struct da8xx_rproc - da8xx remote processor instance state
 * @rproc: rproc handle
 * @dsp_clk: placeholder for platform's DSP clk
 * @ack_fxn: chip-specific ack function for ack'ing irq
 * @irq_data: ack_fxn function parameter
 * @chipsig: virt ptr to DSP interrupt registers (CHIPSIG & CHIPSIG_CLR)
 * @bootreg: virt ptr to DSP boot address register (HOST1CFG)
 * @irq: irq # used by this instance
 */
struct da8xx_rproc {
	struct rproc *rproc;
	struct clk *dsp_clk;
	void (*ack_fxn)(struct irq_data *data);
	struct irq_data *irq_data;
	void __iomem *chipsig;
	void __iomem *bootreg;
	int irq;
};

/**
 * handle_event() - inbound virtqueue message workqueue function
 *
 * This function is registered as a kernel thread and is scheduled by the
 * kernel handler.
 */
static irqreturn_t handle_event(int irq, void *p)
{
	struct rproc *rproc = (struct rproc *)p;

	/* Process incoming buffers on all our vrings */
	rproc_vq_interrupt(rproc, 0);
	rproc_vq_interrupt(rproc, 1);

	return IRQ_HANDLED;
}

/**
 * da8xx_rproc_callback() - inbound virtqueue message handler
 *
 * This handler is invoked directly by the kernel whenever the remote
 * core (DSP) has modified the state of a virtqueue.  There is no
 * "payload" message indicating the virtqueue index as is the case with
 * mailbox-based implementations on OMAP4.  As such, this handler "polls"
 * each known virtqueue index for every invocation.
 */
static irqreturn_t da8xx_rproc_callback(int irq, void *p)
{
	struct rproc *rproc = (struct rproc *)p;
	struct da8xx_rproc *drproc = (struct da8xx_rproc *)rproc->priv;
	u32 chipsig;

	chipsig = readl(drproc->chipsig);
	if (chipsig & SYSCFG_CHIPSIG0) {
		/* Clear interrupt level source */
		writel(SYSCFG_CHIPSIG0, drproc->chipsig + 4);

		/*
		 * ACK intr to AINTC.
		 *
		 * It has already been ack'ed by the kernel before calling
		 * this function, but since the ARM<->DSP interrupts in the
		 * CHIPSIG register are "level" instead of "pulse" variety,
		 * we need to ack it after taking down the level else we'll
		 * be called again immediately after returning.
		 */
		drproc->ack_fxn(drproc->irq_data);

		return IRQ_WAKE_THREAD;
	}

	return IRQ_HANDLED;
}

static int da8xx_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct da8xx_rproc *drproc = (struct da8xx_rproc *)rproc->priv;
	struct clk *dsp_clk = drproc->dsp_clk;

	/* hw requires the start (boot) address be on 1KB boundary */
	if (rproc->bootaddr & 0x3ff) {
		dev_err(dev, "invalid boot address: must be aligned to 1KB\n");

		return -EINVAL;
	}

	writel(rproc->bootaddr, drproc->bootreg);

	clk_enable(dsp_clk);
	davinci_clk_reset_deassert(dsp_clk);

	return 0;
}

static int da8xx_rproc_stop(struct rproc *rproc)
{
	struct da8xx_rproc *drproc = rproc->priv;

	davinci_clk_reset_assert(drproc->dsp_clk);
	clk_disable(drproc->dsp_clk);

	return 0;
}

/* kick a virtqueue */
static void da8xx_rproc_kick(struct rproc *rproc, int vqid)
{
	struct da8xx_rproc *drproc = (struct da8xx_rproc *)rproc->priv;

	/* Interrupt remote proc */
	writel(SYSCFG_CHIPSIG2, drproc->chipsig);
}

static const struct rproc_ops da8xx_rproc_ops = {
	.start = da8xx_rproc_start,
	.stop = da8xx_rproc_stop,
	.kick = da8xx_rproc_kick,
};

static int da8xx_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da8xx_rproc *drproc;
	struct rproc *rproc;
	struct irq_data *irq_data;
	struct resource *bootreg_res;
	struct resource *chipsig_res;
	struct clk *dsp_clk;
	void __iomem *chipsig;
	void __iomem *bootreg;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "platform_get_irq(pdev, 0) error: %d\n", irq);
		return irq;
	}

	irq_data = irq_get_irq_data(irq);
	if (!irq_data) {
		dev_err(dev, "irq_get_irq_data(%d): NULL\n", irq);
		return -EINVAL;
	}

	bootreg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bootreg = devm_ioremap_resource(dev, bootreg_res);
	if (IS_ERR(bootreg))
		return PTR_ERR(bootreg);

	chipsig_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	chipsig = devm_ioremap_resource(dev, chipsig_res);
	if (IS_ERR(chipsig))
		return PTR_ERR(chipsig);

	dsp_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(dsp_clk)) {
		dev_err(dev, "clk_get error: %ld\n", PTR_ERR(dsp_clk));

		return PTR_ERR(dsp_clk);
	}

	rproc = rproc_alloc(dev, "dsp", &da8xx_rproc_ops, da8xx_fw_name,
		sizeof(*drproc));
	if (!rproc)
		return -ENOMEM;

	drproc = rproc->priv;
	drproc->rproc = rproc;
	drproc->dsp_clk = dsp_clk;
	rproc->has_iommu = false;

	platform_set_drvdata(pdev, rproc);

	/* everything the ISR needs is now setup, so hook it up */
	ret = devm_request_threaded_irq(dev, irq, da8xx_rproc_callback,
					handle_event, 0, "da8xx-remoteproc",
					rproc);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq error: %d\n", ret);
		goto free_rproc;
	}

	/*
	 * rproc_add() can end up enabling the DSP's clk with the DSP
	 * *not* in reset, but da8xx_rproc_start() needs the DSP to be
	 * held in reset at the time it is called.
	 */
	ret = davinci_clk_reset_assert(drproc->dsp_clk);
	if (ret)
		goto free_rproc;

	drproc->chipsig = chipsig;
	drproc->bootreg = bootreg;
	drproc->ack_fxn = irq_data->chip->irq_ack;
	drproc->irq_data = irq_data;
	drproc->irq = irq;

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed: %d\n", ret);
		goto free_rproc;
	}

	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int da8xx_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct da8xx_rproc *drproc = (struct da8xx_rproc *)rproc->priv;

	/*
	 * The devm subsystem might end up releasing things before
	 * freeing the irq, thus allowing an interrupt to sneak in while
	 * the device is being removed.  This should prevent that.
	 */
	disable_irq(drproc->irq);

	rproc_del(rproc);
	rproc_free(rproc);

	return 0;
}

static struct platform_driver da8xx_rproc_driver = {
	.probe = da8xx_rproc_probe,
	.remove = da8xx_rproc_remove,
	.driver = {
		.name = "davinci-rproc",
	},
};

module_platform_driver(da8xx_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DA8XX Remote Processor control driver");
