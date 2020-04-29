// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Authors: Ludovic Barre <ludovic.barre@st.com> for STMicroelectronics.
 *          Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>

#define IPCC_XCR		0x000
#define XCR_RXOIE		BIT(0)
#define XCR_TXOIE		BIT(16)

#define IPCC_XMR		0x004
#define IPCC_XSCR		0x008
#define IPCC_XTOYSR		0x00c

#define IPCC_PROC_OFFST		0x010

#define IPCC_HWCFGR		0x3f0
#define IPCFGR_CHAN_MASK	GENMASK(7, 0)

#define IPCC_VER		0x3f4
#define VER_MINREV_MASK		GENMASK(3, 0)
#define VER_MAJREV_MASK		GENMASK(7, 4)

#define RX_BIT_MASK		GENMASK(15, 0)
#define RX_BIT_CHAN(chan)	BIT(chan)
#define TX_BIT_SHIFT		16
#define TX_BIT_MASK		GENMASK(31, 16)
#define TX_BIT_CHAN(chan)	BIT(TX_BIT_SHIFT + (chan))

#define STM32_MAX_PROCS		2

enum {
	IPCC_IRQ_RX,
	IPCC_IRQ_TX,
	IPCC_IRQ_NUM,
};

struct stm32_ipcc {
	struct mbox_controller controller;
	void __iomem *reg_base;
	void __iomem *reg_proc;
	struct clk *clk;
	spinlock_t lock; /* protect access to IPCC registers */
	int irqs[IPCC_IRQ_NUM];
	u32 proc_id;
	u32 n_chans;
	u32 xcr;
	u32 xmr;
};

static inline void stm32_ipcc_set_bits(spinlock_t *lock, void __iomem *reg,
				       u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	writel_relaxed(readl_relaxed(reg) | mask, reg);
	spin_unlock_irqrestore(lock, flags);
}

static inline void stm32_ipcc_clr_bits(spinlock_t *lock, void __iomem *reg,
				       u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	writel_relaxed(readl_relaxed(reg) & ~mask, reg);
	spin_unlock_irqrestore(lock, flags);
}

static irqreturn_t stm32_ipcc_rx_irq(int irq, void *data)
{
	struct stm32_ipcc *ipcc = data;
	struct device *dev = ipcc->controller.dev;
	u32 status, mr, tosr, chan;
	irqreturn_t ret = IRQ_NONE;
	int proc_offset;

	/* read 'channel occupied' status from other proc */
	proc_offset = ipcc->proc_id ? -IPCC_PROC_OFFST : IPCC_PROC_OFFST;
	tosr = readl_relaxed(ipcc->reg_proc + proc_offset + IPCC_XTOYSR);
	mr = readl_relaxed(ipcc->reg_proc + IPCC_XMR);

	/* search for unmasked 'channel occupied' */
	status = tosr & FIELD_GET(RX_BIT_MASK, ~mr);

	for (chan = 0; chan < ipcc->n_chans; chan++) {
		if (!(status & (1 << chan)))
			continue;

		dev_dbg(dev, "%s: chan:%d rx\n", __func__, chan);

		mbox_chan_received_data(&ipcc->controller.chans[chan], NULL);

		stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XSCR,
				    RX_BIT_CHAN(chan));

		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t stm32_ipcc_tx_irq(int irq, void *data)
{
	struct stm32_ipcc *ipcc = data;
	struct device *dev = ipcc->controller.dev;
	u32 status, mr, tosr, chan;
	irqreturn_t ret = IRQ_NONE;

	tosr = readl_relaxed(ipcc->reg_proc + IPCC_XTOYSR);
	mr = readl_relaxed(ipcc->reg_proc + IPCC_XMR);

	/* search for unmasked 'channel free' */
	status = ~tosr & FIELD_GET(TX_BIT_MASK, ~mr);

	for (chan = 0; chan < ipcc->n_chans ; chan++) {
		if (!(status & (1 << chan)))
			continue;

		dev_dbg(dev, "%s: chan:%d tx\n", __func__, chan);

		/* mask 'tx channel free' interrupt */
		stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XMR,
				    TX_BIT_CHAN(chan));

		mbox_chan_txdone(&ipcc->controller.chans[chan], 0);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static int stm32_ipcc_send_data(struct mbox_chan *link, void *data)
{
	unsigned int chan = (unsigned int)link->con_priv;
	struct stm32_ipcc *ipcc = container_of(link->mbox, struct stm32_ipcc,
					       controller);

	dev_dbg(ipcc->controller.dev, "%s: chan:%d\n", __func__, chan);

	/* set channel n occupied */
	stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XSCR,
			    TX_BIT_CHAN(chan));

	/* unmask 'tx channel free' interrupt */
	stm32_ipcc_clr_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XMR,
			    TX_BIT_CHAN(chan));

	return 0;
}

static int stm32_ipcc_startup(struct mbox_chan *link)
{
	unsigned int chan = (unsigned int)link->con_priv;
	struct stm32_ipcc *ipcc = container_of(link->mbox, struct stm32_ipcc,
					       controller);
	int ret;

	ret = clk_prepare_enable(ipcc->clk);
	if (ret) {
		dev_err(ipcc->controller.dev, "can not enable the clock\n");
		return ret;
	}

	/* unmask 'rx channel occupied' interrupt */
	stm32_ipcc_clr_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XMR,
			    RX_BIT_CHAN(chan));

	return 0;
}

static void stm32_ipcc_shutdown(struct mbox_chan *link)
{
	unsigned int chan = (unsigned int)link->con_priv;
	struct stm32_ipcc *ipcc = container_of(link->mbox, struct stm32_ipcc,
					       controller);

	/* mask rx/tx interrupt */
	stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XMR,
			    RX_BIT_CHAN(chan) | TX_BIT_CHAN(chan));

	clk_disable_unprepare(ipcc->clk);
}

static const struct mbox_chan_ops stm32_ipcc_ops = {
	.send_data	= stm32_ipcc_send_data,
	.startup	= stm32_ipcc_startup,
	.shutdown	= stm32_ipcc_shutdown,
};

static int stm32_ipcc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct stm32_ipcc *ipcc;
	struct resource *res;
	unsigned int i;
	int ret;
	u32 ip_ver;
	static const char * const irq_name[] = {"rx", "tx"};
	irq_handler_t irq_thread[] = {stm32_ipcc_rx_irq, stm32_ipcc_tx_irq};

	if (!np) {
		dev_err(dev, "No DT found\n");
		return -ENODEV;
	}

	ipcc = devm_kzalloc(dev, sizeof(*ipcc), GFP_KERNEL);
	if (!ipcc)
		return -ENOMEM;

	spin_lock_init(&ipcc->lock);

	/* proc_id */
	if (of_property_read_u32(np, "st,proc-id", &ipcc->proc_id)) {
		dev_err(dev, "Missing st,proc-id\n");
		return -ENODEV;
	}

	if (ipcc->proc_id >= STM32_MAX_PROCS) {
		dev_err(dev, "Invalid proc_id (%d)\n", ipcc->proc_id);
		return -EINVAL;
	}

	/* regs */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ipcc->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ipcc->reg_base))
		return PTR_ERR(ipcc->reg_base);

	ipcc->reg_proc = ipcc->reg_base + ipcc->proc_id * IPCC_PROC_OFFST;

	/* clock */
	ipcc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ipcc->clk))
		return PTR_ERR(ipcc->clk);

	ret = clk_prepare_enable(ipcc->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	/* irq */
	for (i = 0; i < IPCC_IRQ_NUM; i++) {
		ipcc->irqs[i] = platform_get_irq_byname(pdev, irq_name[i]);
		if (ipcc->irqs[i] < 0) {
			if (ipcc->irqs[i] != -EPROBE_DEFER)
				dev_err(dev, "no IRQ specified %s\n",
					irq_name[i]);
			ret = ipcc->irqs[i];
			goto err_clk;
		}

		ret = devm_request_threaded_irq(dev, ipcc->irqs[i], NULL,
						irq_thread[i], IRQF_ONESHOT,
						dev_name(dev), ipcc);
		if (ret) {
			dev_err(dev, "failed to request irq %d (%d)\n", i, ret);
			goto err_clk;
		}
	}

	/* mask and enable rx/tx irq */
	stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XMR,
			    RX_BIT_MASK | TX_BIT_MASK);
	stm32_ipcc_set_bits(&ipcc->lock, ipcc->reg_proc + IPCC_XCR,
			    XCR_RXOIE | XCR_TXOIE);

	/* wakeup */
	if (of_property_read_bool(np, "wakeup-source")) {
		device_set_wakeup_capable(dev, true);

		ret = dev_pm_set_wake_irq(dev, ipcc->irqs[IPCC_IRQ_RX]);
		if (ret) {
			dev_err(dev, "Failed to set wake up irq\n");
			goto err_init_wkp;
		}
	}

	/* mailbox controller */
	ipcc->n_chans = readl_relaxed(ipcc->reg_base + IPCC_HWCFGR);
	ipcc->n_chans &= IPCFGR_CHAN_MASK;

	ipcc->controller.dev = dev;
	ipcc->controller.txdone_irq = true;
	ipcc->controller.ops = &stm32_ipcc_ops;
	ipcc->controller.num_chans = ipcc->n_chans;
	ipcc->controller.chans = devm_kcalloc(dev, ipcc->controller.num_chans,
					      sizeof(*ipcc->controller.chans),
					      GFP_KERNEL);
	if (!ipcc->controller.chans) {
		ret = -ENOMEM;
		goto err_irq_wkp;
	}

	for (i = 0; i < ipcc->controller.num_chans; i++)
		ipcc->controller.chans[i].con_priv = (void *)i;

	ret = devm_mbox_controller_register(dev, &ipcc->controller);
	if (ret)
		goto err_irq_wkp;

	platform_set_drvdata(pdev, ipcc);

	ip_ver = readl_relaxed(ipcc->reg_base + IPCC_VER);

	dev_info(dev, "ipcc rev:%ld.%ld enabled, %d chans, proc %d\n",
		 FIELD_GET(VER_MAJREV_MASK, ip_ver),
		 FIELD_GET(VER_MINREV_MASK, ip_ver),
		 ipcc->controller.num_chans, ipcc->proc_id);

	clk_disable_unprepare(ipcc->clk);
	return 0;

err_irq_wkp:
	if (of_property_read_bool(np, "wakeup-source"))
		dev_pm_clear_wake_irq(dev);
err_init_wkp:
	device_set_wakeup_capable(dev, false);
err_clk:
	clk_disable_unprepare(ipcc->clk);
	return ret;
}

static int stm32_ipcc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (of_property_read_bool(dev->of_node, "wakeup-source"))
		dev_pm_clear_wake_irq(&pdev->dev);

	device_set_wakeup_capable(dev, false);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stm32_ipcc_suspend(struct device *dev)
{
	struct stm32_ipcc *ipcc = dev_get_drvdata(dev);

	ipcc->xmr = readl_relaxed(ipcc->reg_proc + IPCC_XMR);
	ipcc->xcr = readl_relaxed(ipcc->reg_proc + IPCC_XCR);

	return 0;
}

static int stm32_ipcc_resume(struct device *dev)
{
	struct stm32_ipcc *ipcc = dev_get_drvdata(dev);

	writel_relaxed(ipcc->xmr, ipcc->reg_proc + IPCC_XMR);
	writel_relaxed(ipcc->xcr, ipcc->reg_proc + IPCC_XCR);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(stm32_ipcc_pm_ops,
			 stm32_ipcc_suspend, stm32_ipcc_resume);

static const struct of_device_id stm32_ipcc_of_match[] = {
	{ .compatible = "st,stm32mp1-ipcc" },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_ipcc_of_match);

static struct platform_driver stm32_ipcc_driver = {
	.driver = {
		.name = "stm32-ipcc",
		.pm = &stm32_ipcc_pm_ops,
		.of_match_table = stm32_ipcc_of_match,
	},
	.probe		= stm32_ipcc_probe,
	.remove		= stm32_ipcc_remove,
};

module_platform_driver(stm32_ipcc_driver);

MODULE_AUTHOR("Ludovic Barre <ludovic.barre@st.com>");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_DESCRIPTION("STM32 IPCC driver");
MODULE_LICENSE("GPL v2");
