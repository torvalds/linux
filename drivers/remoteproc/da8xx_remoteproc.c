// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor machine-specific module for DA8XX
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

static char *da8xx_fw_name;
module_param(da8xx_fw_name, charp, 0444);
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

#define DA8XX_RPROC_LOCAL_ADDRESS_MASK	(SZ_16M - 1)

/**
 * struct da8xx_rproc_mem - internal memory structure
 * @cpu_addr: MPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 * @dev_addr: Device address of the memory region from DSP view
 * @size: Size of the memory region
 */
struct da8xx_rproc_mem {
	void __iomem *cpu_addr;
	phys_addr_t bus_addr;
	u32 dev_addr;
	size_t size;
};

/**
 * struct da8xx_rproc - da8xx remote processor instance state
 * @rproc: rproc handle
 * @mem: internal memory regions data
 * @num_mems: number of internal memory regions
 * @dsp_clk: placeholder for platform's DSP clk
 * @ack_fxn: chip-specific ack function for ack'ing irq
 * @irq_data: ack_fxn function parameter
 * @chipsig: virt ptr to DSP interrupt registers (CHIPSIG & CHIPSIG_CLR)
 * @bootreg: virt ptr to DSP boot address register (HOST1CFG)
 * @irq: irq # used by this instance
 */
struct da8xx_rproc {
	struct rproc *rproc;
	struct da8xx_rproc_mem *mem;
	int num_mems;
	struct clk *dsp_clk;
	struct reset_control *dsp_reset;
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
	struct rproc *rproc = p;

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
	struct rproc *rproc = p;
	struct da8xx_rproc *drproc = rproc->priv;
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
	struct da8xx_rproc *drproc = rproc->priv;
	struct clk *dsp_clk = drproc->dsp_clk;
	struct reset_control *dsp_reset = drproc->dsp_reset;
	int ret;

	/* hw requires the start (boot) address be on 1KB boundary */
	if (rproc->bootaddr & 0x3ff) {
		dev_err(dev, "invalid boot address: must be aligned to 1KB\n");

		return -EINVAL;
	}

	writel(rproc->bootaddr, drproc->bootreg);

	ret = clk_prepare_enable(dsp_clk);
	if (ret) {
		dev_err(dev, "clk_prepare_enable() failed: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(dsp_reset);
	if (ret) {
		dev_err(dev, "reset_control_deassert() failed: %d\n", ret);
		clk_disable_unprepare(dsp_clk);
		return ret;
	}

	return 0;
}

static int da8xx_rproc_stop(struct rproc *rproc)
{
	struct da8xx_rproc *drproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int ret;

	ret = reset_control_assert(drproc->dsp_reset);
	if (ret) {
		dev_err(dev, "reset_control_assert() failed: %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(drproc->dsp_clk);

	return 0;
}

/* kick a virtqueue */
static void da8xx_rproc_kick(struct rproc *rproc, int vqid)
{
	struct da8xx_rproc *drproc = rproc->priv;

	/* Interrupt remote proc */
	writel(SYSCFG_CHIPSIG2, drproc->chipsig);
}

static const struct rproc_ops da8xx_rproc_ops = {
	.start = da8xx_rproc_start,
	.stop = da8xx_rproc_stop,
	.kick = da8xx_rproc_kick,
};

static int da8xx_rproc_get_internal_memories(struct platform_device *pdev,
					     struct da8xx_rproc *drproc)
{
	static const char * const mem_names[] = {"l2sram", "l1pram", "l1dram"};
	int num_mems = ARRAY_SIZE(mem_names);
	struct device *dev = &pdev->dev;
	struct resource *res;
	int i;

	drproc->mem = devm_kcalloc(dev, num_mems, sizeof(*drproc->mem),
				   GFP_KERNEL);
	if (!drproc->mem)
		return -ENOMEM;

	for (i = 0; i < num_mems; i++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   mem_names[i]);
		drproc->mem[i].cpu_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(drproc->mem[i].cpu_addr)) {
			dev_err(dev, "failed to parse and map %s memory\n",
				mem_names[i]);
			return PTR_ERR(drproc->mem[i].cpu_addr);
		}
		drproc->mem[i].bus_addr = res->start;
		drproc->mem[i].dev_addr =
				res->start & DA8XX_RPROC_LOCAL_ADDRESS_MASK;
		drproc->mem[i].size = resource_size(res);

		dev_dbg(dev, "memory %8s: bus addr %pa size 0x%zx va %p da 0x%x\n",
			mem_names[i], &drproc->mem[i].bus_addr,
			drproc->mem[i].size, drproc->mem[i].cpu_addr,
			drproc->mem[i].dev_addr);
	}
	drproc->num_mems = num_mems;

	return 0;
}

static int da8xx_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da8xx_rproc *drproc;
	struct rproc *rproc;
	struct irq_data *irq_data;
	struct clk *dsp_clk;
	struct reset_control *dsp_reset;
	void __iomem *chipsig;
	void __iomem *bootreg;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	irq_data = irq_get_irq_data(irq);
	if (!irq_data)
		return dev_err_probe(dev, -EINVAL, "irq_get_irq_data(%d): NULL\n", irq);

	bootreg = devm_platform_ioremap_resource_byname(pdev, "host1cfg");
	if (IS_ERR(bootreg))
		return PTR_ERR(bootreg);

	chipsig = devm_platform_ioremap_resource_byname(pdev, "chipsig");
	if (IS_ERR(chipsig))
		return PTR_ERR(chipsig);

	dsp_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(dsp_clk))
		return dev_err_probe(dev, PTR_ERR(dsp_clk), "clk_get error\n");

	dsp_reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(dsp_reset))
		return dev_err_probe(dev, PTR_ERR(dsp_reset), "unable to get reset control\n");

	if (dev->of_node) {
		ret = of_reserved_mem_device_init(dev);
		if (ret)
			return dev_err_probe(dev, ret, "device does not have specific CMA pool\n");
	}

	rproc = rproc_alloc(dev, "dsp", &da8xx_rproc_ops, da8xx_fw_name,
		sizeof(*drproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto free_mem;
	}

	/* error recovery is not supported at present */
	rproc->recovery_disabled = true;

	drproc = rproc->priv;
	drproc->rproc = rproc;
	drproc->dsp_clk = dsp_clk;
	drproc->dsp_reset = dsp_reset;
	rproc->has_iommu = false;

	ret = da8xx_rproc_get_internal_memories(pdev, drproc);
	if (ret)
		goto free_rproc;

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
	ret = reset_control_assert(dsp_reset);
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
free_mem:
	if (dev->of_node)
		of_reserved_mem_device_release(dev);
	return ret;
}

static void da8xx_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct da8xx_rproc *drproc = rproc->priv;
	struct device *dev = &pdev->dev;

	/*
	 * The devm subsystem might end up releasing things before
	 * freeing the irq, thus allowing an interrupt to sneak in while
	 * the device is being removed.  This should prevent that.
	 */
	disable_irq(drproc->irq);

	rproc_del(rproc);
	rproc_free(rproc);
	if (dev->of_node)
		of_reserved_mem_device_release(dev);
}

static const struct of_device_id davinci_rproc_of_match[] __maybe_unused = {
	{ .compatible = "ti,da850-dsp", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, davinci_rproc_of_match);

static struct platform_driver da8xx_rproc_driver = {
	.probe = da8xx_rproc_probe,
	.remove = da8xx_rproc_remove,
	.driver = {
		.name = "davinci-rproc",
		.of_match_table = of_match_ptr(davinci_rproc_of_match),
	},
};

module_platform_driver(da8xx_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DA8XX Remote Processor control driver");
