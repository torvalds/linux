// SPDX-License-Identifier: GPL-2.0-only
/* OMAP SSI driver.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2014 Sebastian Reichel <sre@kernel.org>
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 */

#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/of_platform.h>
#include <linux/hsi/hsi.h>
#include <linux/idr.h>

#include "omap_ssi_regs.h"
#include "omap_ssi.h"

/* For automatically allocated device IDs */
static DEFINE_IDA(platform_omap_ssi_ida);

#ifdef CONFIG_DEBUG_FS
static int ssi_regs_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_controller *ssi = m->private;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sys = omap_ssi->sys;

	pm_runtime_get_sync(ssi->device.parent);
	seq_printf(m, "REVISION\t: 0x%08x\n",  readl(sys + SSI_REVISION_REG));
	seq_printf(m, "SYSCONFIG\t: 0x%08x\n", readl(sys + SSI_SYSCONFIG_REG));
	seq_printf(m, "SYSSTATUS\t: 0x%08x\n", readl(sys + SSI_SYSSTATUS_REG));
	pm_runtime_put(ssi->device.parent);

	return 0;
}

static int ssi_gdd_regs_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_controller *ssi = m->private;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *gdd = omap_ssi->gdd;
	void __iomem *sys = omap_ssi->sys;
	int lch;

	pm_runtime_get_sync(ssi->device.parent);

	seq_printf(m, "GDD_MPU_STATUS\t: 0x%08x\n",
		readl(sys + SSI_GDD_MPU_IRQ_STATUS_REG));
	seq_printf(m, "GDD_MPU_ENABLE\t: 0x%08x\n\n",
		readl(sys + SSI_GDD_MPU_IRQ_ENABLE_REG));
	seq_printf(m, "HW_ID\t\t: 0x%08x\n",
				readl(gdd + SSI_GDD_HW_ID_REG));
	seq_printf(m, "PPORT_ID\t: 0x%08x\n",
				readl(gdd + SSI_GDD_PPORT_ID_REG));
	seq_printf(m, "MPORT_ID\t: 0x%08x\n",
				readl(gdd + SSI_GDD_MPORT_ID_REG));
	seq_printf(m, "TEST\t\t: 0x%08x\n",
				readl(gdd + SSI_GDD_TEST_REG));
	seq_printf(m, "GCR\t\t: 0x%08x\n",
				readl(gdd + SSI_GDD_GCR_REG));

	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++) {
		seq_printf(m, "\nGDD LCH %d\n=========\n", lch);
		seq_printf(m, "CSDP\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CSDP_REG(lch)));
		seq_printf(m, "CCR\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CCR_REG(lch)));
		seq_printf(m, "CICR\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CICR_REG(lch)));
		seq_printf(m, "CSR\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CSR_REG(lch)));
		seq_printf(m, "CSSA\t\t: 0x%08x\n",
				readl(gdd + SSI_GDD_CSSA_REG(lch)));
		seq_printf(m, "CDSA\t\t: 0x%08x\n",
				readl(gdd + SSI_GDD_CDSA_REG(lch)));
		seq_printf(m, "CEN\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CEN_REG(lch)));
		seq_printf(m, "CSAC\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CSAC_REG(lch)));
		seq_printf(m, "CDAC\t\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CDAC_REG(lch)));
		seq_printf(m, "CLNK_CTRL\t: 0x%04x\n",
				readw(gdd + SSI_GDD_CLNK_CTRL_REG(lch)));
	}

	pm_runtime_put(ssi->device.parent);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ssi_regs);
DEFINE_SHOW_ATTRIBUTE(ssi_gdd_regs);

static int ssi_debug_add_ctrl(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct dentry *dir;

	/* SSI controller */
	omap_ssi->dir = debugfs_create_dir(dev_name(&ssi->device), NULL);
	if (!omap_ssi->dir)
		return -ENOMEM;

	debugfs_create_file("regs", S_IRUGO, omap_ssi->dir, ssi,
								&ssi_regs_fops);
	/* SSI GDD (DMA) */
	dir = debugfs_create_dir("gdd", omap_ssi->dir);
	if (!dir)
		goto rback;
	debugfs_create_file("regs", S_IRUGO, dir, ssi, &ssi_gdd_regs_fops);

	return 0;
rback:
	debugfs_remove_recursive(omap_ssi->dir);

	return -ENOMEM;
}

static void ssi_debug_remove_ctrl(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	debugfs_remove_recursive(omap_ssi->dir);
}
#endif /* CONFIG_DEBUG_FS */

/*
 * FIXME: Horrible HACK needed until we remove the useless wakeline test
 * in the CMT. To be removed !!!!
 */
void ssi_waketest(struct hsi_client *cl, unsigned int enable)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	omap_port->wktest = !!enable;
	if (omap_port->wktest) {
		pm_runtime_get_sync(ssi->device.parent);
		writel_relaxed(SSI_WAKE(0),
				omap_ssi->sys + SSI_SET_WAKE_REG(port->num));
	} else {
		writel_relaxed(SSI_WAKE(0),
				omap_ssi->sys +	SSI_CLEAR_WAKE_REG(port->num));
		pm_runtime_put(ssi->device.parent);
	}
}
EXPORT_SYMBOL_GPL(ssi_waketest);

static void ssi_gdd_complete(struct hsi_controller *ssi, unsigned int lch)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg = omap_ssi->gdd_trn[lch].msg;
	struct hsi_port *port = to_hsi_port(msg->cl->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	unsigned int dir;
	u32 csr;
	u32 val;

	spin_lock(&omap_ssi->lock);

	val = readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	val &= ~SSI_GDD_LCH(lch);
	writel_relaxed(val, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);

	if (msg->ttype == HSI_MSG_READ) {
		dir = DMA_FROM_DEVICE;
		val = SSI_DATAAVAILABLE(msg->channel);
		pm_runtime_put(omap_port->pdev);
	} else {
		dir = DMA_TO_DEVICE;
		val = SSI_DATAACCEPT(msg->channel);
		/* Keep clocks reference for write pio event */
	}
	dma_unmap_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents, dir);
	csr = readw(omap_ssi->gdd + SSI_GDD_CSR_REG(lch));
	omap_ssi->gdd_trn[lch].msg = NULL; /* release GDD lch */
	dev_dbg(&port->device, "DMA completed ch %d ttype %d\n",
				msg->channel, msg->ttype);
	spin_unlock(&omap_ssi->lock);
	if (csr & SSI_CSR_TOUR) { /* Timeout error */
		msg->status = HSI_STATUS_ERROR;
		msg->actual_len = 0;
		spin_lock(&omap_port->lock);
		list_del(&msg->link); /* Dequeue msg */
		spin_unlock(&omap_port->lock);

		list_add_tail(&msg->link, &omap_port->errqueue);
		schedule_delayed_work(&omap_port->errqueue_work, 0);
		return;
	}
	spin_lock(&omap_port->lock);
	val |= readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel_relaxed(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	spin_unlock(&omap_port->lock);

	msg->status = HSI_STATUS_COMPLETED;
	msg->actual_len = sg_dma_len(msg->sgt.sgl);
}

static void ssi_gdd_tasklet(unsigned long dev)
{
	struct hsi_controller *ssi = (struct hsi_controller *)dev;
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sys = omap_ssi->sys;
	unsigned int lch;
	u32 status_reg;

	pm_runtime_get(ssi->device.parent);

	if (!pm_runtime_active(ssi->device.parent)) {
		dev_warn(ssi->device.parent, "ssi_gdd_tasklet called without runtime PM!\n");
		pm_runtime_put(ssi->device.parent);
		return;
	}

	status_reg = readl(sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++) {
		if (status_reg & SSI_GDD_LCH(lch))
			ssi_gdd_complete(ssi, lch);
	}
	writel_relaxed(status_reg, sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	status_reg = readl(sys + SSI_GDD_MPU_IRQ_STATUS_REG);

	pm_runtime_put(ssi->device.parent);

	if (status_reg)
		tasklet_hi_schedule(&omap_ssi->gdd_tasklet);
	else
		enable_irq(omap_ssi->gdd_irq);

}

static irqreturn_t ssi_gdd_isr(int irq, void *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	tasklet_hi_schedule(&omap_ssi->gdd_tasklet);
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

static unsigned long ssi_get_clk_rate(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	unsigned long rate = clk_get_rate(omap_ssi->fck);
	return rate;
}

static int ssi_clk_event(struct notifier_block *nb, unsigned long event,
								void *data)
{
	struct omap_ssi_controller *omap_ssi = container_of(nb,
					struct omap_ssi_controller, fck_nb);
	struct hsi_controller *ssi = to_hsi_controller(omap_ssi->dev);
	struct clk_notifier_data *clk_data = data;
	struct omap_ssi_port *omap_port;
	int i;

	switch (event) {
	case PRE_RATE_CHANGE:
		dev_dbg(&ssi->device, "pre rate change\n");

		for (i = 0; i < ssi->num_ports; i++) {
			omap_port = omap_ssi->port[i];

			if (!omap_port)
				continue;

			/* Workaround for SWBREAK + CAwake down race in CMT */
			disable_irq(omap_port->wake_irq);

			/* stop all ssi communication */
			pinctrl_pm_select_idle_state(omap_port->pdev);
			udelay(1); /* wait for racing frames */
		}

		break;
	case ABORT_RATE_CHANGE:
		dev_dbg(&ssi->device, "abort rate change\n");
		/* Fall through */
	case POST_RATE_CHANGE:
		dev_dbg(&ssi->device, "post rate change (%lu -> %lu)\n",
			clk_data->old_rate, clk_data->new_rate);
		omap_ssi->fck_rate = DIV_ROUND_CLOSEST(clk_data->new_rate, 1000); /* kHz */

		for (i = 0; i < ssi->num_ports; i++) {
			omap_port = omap_ssi->port[i];

			if (!omap_port)
				continue;

			omap_ssi_port_update_fclk(ssi, omap_port);

			/* resume ssi communication */
			pinctrl_pm_select_default_state(omap_port->pdev);
			enable_irq(omap_port->wake_irq);
		}

		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int ssi_get_iomem(struct platform_device *pd,
		const char *name, void __iomem **pbase, dma_addr_t *phy)
{
	struct resource *mem;
	void __iomem *base;
	struct hsi_controller *ssi = platform_get_drvdata(pd);

	mem = platform_get_resource_byname(pd, IORESOURCE_MEM, name);
	base = devm_ioremap_resource(&ssi->device, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	*pbase = base;

	if (phy)
		*phy = mem->start;

	return 0;
}

static int ssi_add_controller(struct hsi_controller *ssi,
						struct platform_device *pd)
{
	struct omap_ssi_controller *omap_ssi;
	int err;

	omap_ssi = devm_kzalloc(&ssi->device, sizeof(*omap_ssi), GFP_KERNEL);
	if (!omap_ssi)
		return -ENOMEM;

	err = ida_simple_get(&platform_omap_ssi_ida, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto out_err;
	ssi->id = err;

	ssi->owner = THIS_MODULE;
	ssi->device.parent = &pd->dev;
	dev_set_name(&ssi->device, "ssi%d", ssi->id);
	hsi_controller_set_drvdata(ssi, omap_ssi);
	omap_ssi->dev = &ssi->device;
	err = ssi_get_iomem(pd, "sys", &omap_ssi->sys, NULL);
	if (err < 0)
		goto out_err;
	err = ssi_get_iomem(pd, "gdd", &omap_ssi->gdd, NULL);
	if (err < 0)
		goto out_err;
	err = platform_get_irq_byname(pd, "gdd_mpu");
	if (err < 0) {
		dev_err(&pd->dev, "GDD IRQ resource missing\n");
		goto out_err;
	}
	omap_ssi->gdd_irq = err;
	tasklet_init(&omap_ssi->gdd_tasklet, ssi_gdd_tasklet,
							(unsigned long)ssi);
	err = devm_request_irq(&ssi->device, omap_ssi->gdd_irq, ssi_gdd_isr,
						0, "gdd_mpu", ssi);
	if (err < 0) {
		dev_err(&ssi->device, "Request GDD IRQ %d failed (%d)",
							omap_ssi->gdd_irq, err);
		goto out_err;
	}

	omap_ssi->port = devm_kcalloc(&ssi->device, ssi->num_ports,
				      sizeof(*omap_ssi->port), GFP_KERNEL);
	if (!omap_ssi->port) {
		err = -ENOMEM;
		goto out_err;
	}

	omap_ssi->fck = devm_clk_get(&ssi->device, "ssi_ssr_fck");
	if (IS_ERR(omap_ssi->fck)) {
		dev_err(&pd->dev, "Could not acquire clock \"ssi_ssr_fck\": %li\n",
			PTR_ERR(omap_ssi->fck));
		err = -ENODEV;
		goto out_err;
	}

	omap_ssi->fck_nb.notifier_call = ssi_clk_event;
	omap_ssi->fck_nb.priority = INT_MAX;
	clk_notifier_register(omap_ssi->fck, &omap_ssi->fck_nb);

	/* TODO: find register, which can be used to detect context loss */
	omap_ssi->get_loss = NULL;

	omap_ssi->max_speed = UINT_MAX;
	spin_lock_init(&omap_ssi->lock);
	err = hsi_register_controller(ssi);

	if (err < 0)
		goto out_err;

	return 0;

out_err:
	ida_simple_remove(&platform_omap_ssi_ida, ssi->id);
	return err;
}

static int ssi_hw_init(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int err;

	err = pm_runtime_get_sync(ssi->device.parent);
	if (err < 0) {
		dev_err(&ssi->device, "runtime PM failed %d\n", err);
		return err;
	}
	/* Resetting GDD */
	writel_relaxed(SSI_SWRESET, omap_ssi->gdd + SSI_GDD_GRST_REG);
	/* Get FCK rate in kHz */
	omap_ssi->fck_rate = DIV_ROUND_CLOSEST(ssi_get_clk_rate(ssi), 1000);
	dev_dbg(&ssi->device, "SSI fck rate %lu kHz\n", omap_ssi->fck_rate);

	writel_relaxed(SSI_CLK_AUTOGATING_ON, omap_ssi->sys + SSI_GDD_GCR_REG);
	omap_ssi->gdd_gcr = SSI_CLK_AUTOGATING_ON;
	pm_runtime_put_sync(ssi->device.parent);

	return 0;
}

static void ssi_remove_controller(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int id = ssi->id;
	tasklet_kill(&omap_ssi->gdd_tasklet);
	hsi_unregister_controller(ssi);
	clk_notifier_unregister(omap_ssi->fck, &omap_ssi->fck_nb);
	ida_simple_remove(&platform_omap_ssi_ida, id);
}

static inline int ssi_of_get_available_ports_count(const struct device_node *np)
{
	struct device_node *child;
	int num = 0;

	for_each_available_child_of_node(np, child)
		if (of_device_is_compatible(child, "ti,omap3-ssi-port"))
			num++;

	return num;
}

static int ssi_remove_ports(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev->of_node)
		return 0;

	of_node_clear_flag(dev->of_node, OF_POPULATED);
	of_device_unregister(pdev);

	return 0;
}

static int ssi_probe(struct platform_device *pd)
{
	struct platform_device *childpdev;
	struct device_node *np = pd->dev.of_node;
	struct device_node *child;
	struct hsi_controller *ssi;
	int err;
	int num_ports;

	if (!np) {
		dev_err(&pd->dev, "missing device tree data\n");
		return -EINVAL;
	}

	num_ports = ssi_of_get_available_ports_count(np);

	ssi = hsi_alloc_controller(num_ports, GFP_KERNEL);
	if (!ssi) {
		dev_err(&pd->dev, "No memory for controller\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pd, ssi);

	err = ssi_add_controller(ssi, pd);
	if (err < 0)
		goto out1;

	pm_runtime_enable(&pd->dev);

	err = ssi_hw_init(ssi);
	if (err < 0)
		goto out2;
#ifdef CONFIG_DEBUG_FS
	err = ssi_debug_add_ctrl(ssi);
	if (err < 0)
		goto out2;
#endif

	for_each_available_child_of_node(np, child) {
		if (!of_device_is_compatible(child, "ti,omap3-ssi-port"))
			continue;

		childpdev = of_platform_device_create(child, NULL, &pd->dev);
		if (!childpdev) {
			err = -ENODEV;
			dev_err(&pd->dev, "failed to create ssi controller port\n");
			goto out3;
		}
	}

	dev_info(&pd->dev, "ssi controller %d initialized (%d ports)!\n",
		ssi->id, num_ports);
	return err;
out3:
	device_for_each_child(&pd->dev, NULL, ssi_remove_ports);
out2:
	ssi_remove_controller(ssi);
out1:
	platform_set_drvdata(pd, NULL);
	pm_runtime_disable(&pd->dev);

	return err;
}

static int ssi_remove(struct platform_device *pd)
{
	struct hsi_controller *ssi = platform_get_drvdata(pd);

	/* cleanup of of_platform_populate() call */
	device_for_each_child(&pd->dev, NULL, ssi_remove_ports);

#ifdef CONFIG_DEBUG_FS
	ssi_debug_remove_ctrl(ssi);
#endif
	ssi_remove_controller(ssi);
	platform_set_drvdata(pd, NULL);

	pm_runtime_disable(&pd->dev);

	return 0;
}

#ifdef CONFIG_PM
static int omap_ssi_runtime_suspend(struct device *dev)
{
	struct hsi_controller *ssi = dev_get_drvdata(dev);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(dev, "runtime suspend!\n");

	if (omap_ssi->get_loss)
		omap_ssi->loss_count =
				omap_ssi->get_loss(ssi->device.parent);

	return 0;
}

static int omap_ssi_runtime_resume(struct device *dev)
{
	struct hsi_controller *ssi = dev_get_drvdata(dev);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(dev, "runtime resume!\n");

	if ((omap_ssi->get_loss) && (omap_ssi->loss_count ==
				omap_ssi->get_loss(ssi->device.parent)))
		return 0;

	writel_relaxed(omap_ssi->gdd_gcr, omap_ssi->gdd + SSI_GDD_GCR_REG);

	return 0;
}

static const struct dev_pm_ops omap_ssi_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_ssi_runtime_suspend, omap_ssi_runtime_resume,
		NULL)
};

#define DEV_PM_OPS     (&omap_ssi_pm_ops)
#else
#define DEV_PM_OPS     NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id omap_ssi_of_match[] = {
	{ .compatible = "ti,omap3-ssi", },
	{},
};
MODULE_DEVICE_TABLE(of, omap_ssi_of_match);
#else
#define omap_ssi_of_match NULL
#endif

static struct platform_driver ssi_pdriver = {
	.probe = ssi_probe,
	.remove	= ssi_remove,
	.driver	= {
		.name	= "omap_ssi",
		.pm     = DEV_PM_OPS,
		.of_match_table = omap_ssi_of_match,
	},
};

static int __init ssi_init(void) {
	int ret;

	ret = platform_driver_register(&ssi_pdriver);
	if (ret)
		return ret;

	return platform_driver_register(&ssi_port_pdriver);
}
module_init(ssi_init);

static void __exit ssi_exit(void) {
	platform_driver_unregister(&ssi_port_pdriver);
	platform_driver_unregister(&ssi_pdriver);
}
module_exit(ssi_exit);

MODULE_ALIAS("platform:omap_ssi");
MODULE_AUTHOR("Carlos Chinea <carlos.chinea@nokia.com>");
MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_DESCRIPTION("Synchronous Serial Interface Driver");
MODULE_LICENSE("GPL v2");
