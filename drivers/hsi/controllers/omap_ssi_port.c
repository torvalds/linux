// SPDX-License-Identifier: GPL-2.0-only
/* OMAP SSI port driver.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2014 Sebastian Reichel <sre@kernel.org>
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/debugfs.h>

#include "omap_ssi_regs.h"
#include "omap_ssi.h"

static inline int hsi_dummy_msg(struct hsi_msg *msg __maybe_unused)
{
	return 0;
}

static inline int hsi_dummy_cl(struct hsi_client *cl __maybe_unused)
{
	return 0;
}

static inline unsigned int ssi_wakein(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	return gpiod_get_value(omap_port->wake_gpio);
}

#ifdef CONFIG_DEBUG_FS
static void ssi_debug_remove_port(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	debugfs_remove_recursive(omap_port->dir);
}

static int ssi_port_regs_show(struct seq_file *m, void *p __maybe_unused)
{
	struct hsi_port *port = m->private;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*base = omap_ssi->sys;
	unsigned int ch;

	pm_runtime_get_sync(omap_port->pdev);
	if (omap_port->wake_irq > 0)
		seq_printf(m, "CAWAKE\t\t: %d\n", ssi_wakein(port));
	seq_printf(m, "WAKE\t\t: 0x%08x\n",
				readl(base + SSI_WAKE_REG(port->num)));
	seq_printf(m, "MPU_ENABLE_IRQ%d\t: 0x%08x\n", 0,
			readl(base + SSI_MPU_ENABLE_REG(port->num, 0)));
	seq_printf(m, "MPU_STATUS_IRQ%d\t: 0x%08x\n", 0,
			readl(base + SSI_MPU_STATUS_REG(port->num, 0)));
	/* SST */
	base = omap_port->sst_base;
	seq_puts(m, "\nSST\n===\n");
	seq_printf(m, "ID SST\t\t: 0x%08x\n",
				readl(base + SSI_SST_ID_REG));
	seq_printf(m, "MODE\t\t: 0x%08x\n",
				readl(base + SSI_SST_MODE_REG));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				readl(base + SSI_SST_FRAMESIZE_REG));
	seq_printf(m, "DIVISOR\t\t: 0x%08x\n",
				readl(base + SSI_SST_DIVISOR_REG));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				readl(base + SSI_SST_CHANNELS_REG));
	seq_printf(m, "ARBMODE\t\t: 0x%08x\n",
				readl(base + SSI_SST_ARBMODE_REG));
	seq_printf(m, "TXSTATE\t\t: 0x%08x\n",
				readl(base + SSI_SST_TXSTATE_REG));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				readl(base + SSI_SST_BUFSTATE_REG));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				readl(base + SSI_SST_BREAK_REG));
	for (ch = 0; ch < omap_port->channels; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				readl(base + SSI_SST_BUFFER_CH_REG(ch)));
	}
	/* SSR */
	base = omap_port->ssr_base;
	seq_puts(m, "\nSSR\n===\n");
	seq_printf(m, "ID SSR\t\t: 0x%08x\n",
				readl(base + SSI_SSR_ID_REG));
	seq_printf(m, "MODE\t\t: 0x%08x\n",
				readl(base + SSI_SSR_MODE_REG));
	seq_printf(m, "FRAMESIZE\t: 0x%08x\n",
				readl(base + SSI_SSR_FRAMESIZE_REG));
	seq_printf(m, "CHANNELS\t: 0x%08x\n",
				readl(base + SSI_SSR_CHANNELS_REG));
	seq_printf(m, "TIMEOUT\t\t: 0x%08x\n",
				readl(base + SSI_SSR_TIMEOUT_REG));
	seq_printf(m, "RXSTATE\t\t: 0x%08x\n",
				readl(base + SSI_SSR_RXSTATE_REG));
	seq_printf(m, "BUFSTATE\t: 0x%08x\n",
				readl(base + SSI_SSR_BUFSTATE_REG));
	seq_printf(m, "BREAK\t\t: 0x%08x\n",
				readl(base + SSI_SSR_BREAK_REG));
	seq_printf(m, "ERROR\t\t: 0x%08x\n",
				readl(base + SSI_SSR_ERROR_REG));
	seq_printf(m, "ERRORACK\t: 0x%08x\n",
				readl(base + SSI_SSR_ERRORACK_REG));
	for (ch = 0; ch < omap_port->channels; ch++) {
		seq_printf(m, "BUFFER_CH%d\t: 0x%08x\n", ch,
				readl(base + SSI_SSR_BUFFER_CH_REG(ch)));
	}
	pm_runtime_put_autosuspend(omap_port->pdev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ssi_port_regs);

static int ssi_div_get(void *data, u64 *val)
{
	struct hsi_port *port = data;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	pm_runtime_get_sync(omap_port->pdev);
	*val = readl(omap_port->sst_base + SSI_SST_DIVISOR_REG);
	pm_runtime_put_autosuspend(omap_port->pdev);

	return 0;
}

static int ssi_div_set(void *data, u64 val)
{
	struct hsi_port *port = data;
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	if (val > 127)
		return -EINVAL;

	pm_runtime_get_sync(omap_port->pdev);
	writel(val, omap_port->sst_base + SSI_SST_DIVISOR_REG);
	omap_port->sst.divisor = val;
	pm_runtime_put_autosuspend(omap_port->pdev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ssi_sst_div_fops, ssi_div_get, ssi_div_set, "%llu\n");

static void ssi_debug_add_port(struct omap_ssi_port *omap_port,
				     struct dentry *dir)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);

	dir = debugfs_create_dir(dev_name(omap_port->dev), dir);
	omap_port->dir = dir;
	debugfs_create_file("regs", S_IRUGO, dir, port, &ssi_port_regs_fops);
	dir = debugfs_create_dir("sst", dir);
	debugfs_create_file_unsafe("divisor", 0644, dir, port,
				   &ssi_sst_div_fops);
}
#endif

static void ssi_process_errqueue(struct work_struct *work)
{
	struct omap_ssi_port *omap_port;
	struct list_head *head, *tmp;
	struct hsi_msg *msg;

	omap_port = container_of(work, struct omap_ssi_port, errqueue_work.work);

	list_for_each_safe(head, tmp, &omap_port->errqueue) {
		msg = list_entry(head, struct hsi_msg, link);
		msg->complete(msg);
		list_del(head);
	}
}

static int ssi_claim_lch(struct hsi_msg *msg)
{

	struct hsi_port *port = hsi_get_port(msg->cl);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int lch;

	for (lch = 0; lch < SSI_MAX_GDD_LCH; lch++)
		if (!omap_ssi->gdd_trn[lch].msg) {
			omap_ssi->gdd_trn[lch].msg = msg;
			omap_ssi->gdd_trn[lch].sg = msg->sgt.sgl;
			return lch;
		}

	return -EBUSY;
}

static int ssi_start_dma(struct hsi_msg *msg, int lch)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *gdd = omap_ssi->gdd;
	int err;
	u16 csdp;
	u16 ccr;
	u32 s_addr;
	u32 d_addr;
	u32 tmp;

	/* Hold clocks during the transfer */
	pm_runtime_get(omap_port->pdev);

	if (!pm_runtime_active(omap_port->pdev)) {
		dev_warn(&port->device, "ssi_start_dma called without runtime PM!\n");
		pm_runtime_put_autosuspend(omap_port->pdev);
		return -EREMOTEIO;
	}

	if (msg->ttype == HSI_MSG_READ) {
		err = dma_map_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents,
							DMA_FROM_DEVICE);
		if (!err) {
			dev_dbg(&ssi->device, "DMA map SG failed !\n");
			pm_runtime_put_autosuspend(omap_port->pdev);
			return -EIO;
		}
		csdp = SSI_DST_BURST_4x32_BIT | SSI_DST_MEMORY_PORT |
			SSI_SRC_SINGLE_ACCESS0 | SSI_SRC_PERIPHERAL_PORT |
			SSI_DATA_TYPE_S32;
		ccr = msg->channel + 0x10 + (port->num * 8); /* Sync */
		ccr |= SSI_DST_AMODE_POSTINC | SSI_SRC_AMODE_CONST |
			SSI_CCR_ENABLE;
		s_addr = omap_port->ssr_dma +
					SSI_SSR_BUFFER_CH_REG(msg->channel);
		d_addr = sg_dma_address(msg->sgt.sgl);
	} else {
		err = dma_map_sg(&ssi->device, msg->sgt.sgl, msg->sgt.nents,
							DMA_TO_DEVICE);
		if (!err) {
			dev_dbg(&ssi->device, "DMA map SG failed !\n");
			pm_runtime_put_autosuspend(omap_port->pdev);
			return -EIO;
		}
		csdp = SSI_SRC_BURST_4x32_BIT | SSI_SRC_MEMORY_PORT |
			SSI_DST_SINGLE_ACCESS0 | SSI_DST_PERIPHERAL_PORT |
			SSI_DATA_TYPE_S32;
		ccr = (msg->channel + 1 + (port->num * 8)) & 0xf; /* Sync */
		ccr |= SSI_SRC_AMODE_POSTINC | SSI_DST_AMODE_CONST |
			SSI_CCR_ENABLE;
		s_addr = sg_dma_address(msg->sgt.sgl);
		d_addr = omap_port->sst_dma +
					SSI_SST_BUFFER_CH_REG(msg->channel);
	}
	dev_dbg(&ssi->device, "lch %d cdsp %08x ccr %04x s_addr %08x d_addr %08x\n",
		lch, csdp, ccr, s_addr, d_addr);

	writew_relaxed(csdp, gdd + SSI_GDD_CSDP_REG(lch));
	writew_relaxed(SSI_BLOCK_IE | SSI_TOUT_IE, gdd + SSI_GDD_CICR_REG(lch));
	writel_relaxed(d_addr, gdd + SSI_GDD_CDSA_REG(lch));
	writel_relaxed(s_addr, gdd + SSI_GDD_CSSA_REG(lch));
	writew_relaxed(SSI_BYTES_TO_FRAMES(msg->sgt.sgl->length),
						gdd + SSI_GDD_CEN_REG(lch));

	spin_lock_bh(&omap_ssi->lock);
	tmp = readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp |= SSI_GDD_LCH(lch);
	writel_relaxed(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	spin_unlock_bh(&omap_ssi->lock);
	writew(ccr, gdd + SSI_GDD_CCR_REG(lch));
	msg->status = HSI_STATUS_PROCEEDING;

	return 0;
}

static int ssi_start_pio(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	u32 val;

	pm_runtime_get(omap_port->pdev);

	if (!pm_runtime_active(omap_port->pdev)) {
		dev_warn(&port->device, "ssi_start_pio called without runtime PM!\n");
		pm_runtime_put_autosuspend(omap_port->pdev);
		return -EREMOTEIO;
	}

	if (msg->ttype == HSI_MSG_WRITE) {
		val = SSI_DATAACCEPT(msg->channel);
		/* Hold clocks for pio writes */
		pm_runtime_get(omap_port->pdev);
	} else {
		val = SSI_DATAAVAILABLE(msg->channel) | SSI_ERROROCCURED;
	}
	dev_dbg(&port->device, "Single %s transfer\n",
						msg->ttype ? "write" : "read");
	val |= readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	pm_runtime_put_autosuspend(omap_port->pdev);
	msg->actual_len = 0;
	msg->status = HSI_STATUS_PROCEEDING;

	return 0;
}

static int ssi_start_transfer(struct list_head *queue)
{
	struct hsi_msg *msg;
	int lch = -1;

	if (list_empty(queue))
		return 0;
	msg = list_first_entry(queue, struct hsi_msg, link);
	if (msg->status != HSI_STATUS_QUEUED)
		return 0;
	if ((msg->sgt.nents) && (msg->sgt.sgl->length > sizeof(u32)))
		lch = ssi_claim_lch(msg);
	if (lch >= 0)
		return ssi_start_dma(msg, lch);
	else
		return ssi_start_pio(msg);
}

static int ssi_async_break(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	int err = 0;
	u32 tmp;

	pm_runtime_get_sync(omap_port->pdev);
	if (msg->ttype == HSI_MSG_WRITE) {
		if (omap_port->sst.mode != SSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		writel(1, omap_port->sst_base + SSI_SST_BREAK_REG);
		msg->status = HSI_STATUS_COMPLETED;
		msg->complete(msg);
	} else {
		if (omap_port->ssr.mode != SSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		spin_lock_bh(&omap_port->lock);
		tmp = readl(omap_ssi->sys +
					SSI_MPU_ENABLE_REG(port->num, 0));
		writel(tmp | SSI_BREAKDETECTED,
			omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
		msg->status = HSI_STATUS_PROCEEDING;
		list_add_tail(&msg->link, &omap_port->brkqueue);
		spin_unlock_bh(&omap_port->lock);
	}
out:
	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);

	return err;
}

static int ssi_async(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct list_head *queue;
	int err = 0;

	BUG_ON(!msg);

	if (msg->sgt.nents > 1)
		return -ENOSYS; /* TODO: Add sg support */

	if (msg->break_frame)
		return ssi_async_break(msg);

	if (msg->ttype) {
		BUG_ON(msg->channel >= omap_port->sst.channels);
		queue = &omap_port->txqueue[msg->channel];
	} else {
		BUG_ON(msg->channel >= omap_port->ssr.channels);
		queue = &omap_port->rxqueue[msg->channel];
	}
	msg->status = HSI_STATUS_QUEUED;

	pm_runtime_get_sync(omap_port->pdev);
	spin_lock_bh(&omap_port->lock);
	list_add_tail(&msg->link, queue);
	err = ssi_start_transfer(queue);
	if (err < 0) {
		list_del(&msg->link);
		msg->status = HSI_STATUS_ERROR;
	}
	spin_unlock_bh(&omap_port->lock);
	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);
	dev_dbg(&port->device, "msg status %d ttype %d ch %d\n",
				msg->status, msg->ttype, msg->channel);

	return err;
}

static u32 ssi_calculate_div(struct hsi_controller *ssi)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	u32 tx_fckrate = (u32) omap_ssi->fck_rate;

	/* / 2 : SSI TX clock is always half of the SSI functional clock */
	tx_fckrate >>= 1;
	/* Round down when tx_fckrate % omap_ssi->max_speed == 0 */
	tx_fckrate--;
	dev_dbg(&ssi->device, "TX div %d for fck_rate %lu Khz speed %d Kb/s\n",
		tx_fckrate / omap_ssi->max_speed, omap_ssi->fck_rate,
		omap_ssi->max_speed);

	return tx_fckrate / omap_ssi->max_speed;
}

static void ssi_flush_queue(struct list_head *queue, struct hsi_client *cl)
{
	struct list_head *node, *tmp;
	struct hsi_msg *msg;

	list_for_each_safe(node, tmp, queue) {
		msg = list_entry(node, struct hsi_msg, link);
		if ((cl) && (cl != msg->cl))
			continue;
		list_del(node);
		pr_debug("flush queue: ch %d, msg %p len %d type %d ctxt %p\n",
			msg->channel, msg, msg->sgt.sgl->length,
					msg->ttype, msg->context);
		if (msg->destructor)
			msg->destructor(msg);
		else
			hsi_free_msg(msg);
	}
}

static int ssi_setup(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sst = omap_port->sst_base;
	void __iomem *ssr = omap_port->ssr_base;
	u32 div;
	u32 val;
	int err = 0;

	pm_runtime_get_sync(omap_port->pdev);
	spin_lock_bh(&omap_port->lock);
	if (cl->tx_cfg.speed)
		omap_ssi->max_speed = cl->tx_cfg.speed;
	div = ssi_calculate_div(ssi);
	if (div > SSI_MAX_DIVISOR) {
		dev_err(&cl->device, "Invalid TX speed %d Mb/s (div %d)\n",
						cl->tx_cfg.speed, div);
		err = -EINVAL;
		goto out;
	}
	/* Set TX/RX module to sleep to stop TX/RX during cfg update */
	writel_relaxed(SSI_MODE_SLEEP, sst + SSI_SST_MODE_REG);
	writel_relaxed(SSI_MODE_SLEEP, ssr + SSI_SSR_MODE_REG);
	/* Flush posted write */
	val = readl(ssr + SSI_SSR_MODE_REG);
	/* TX */
	writel_relaxed(31, sst + SSI_SST_FRAMESIZE_REG);
	writel_relaxed(div, sst + SSI_SST_DIVISOR_REG);
	writel_relaxed(cl->tx_cfg.num_hw_channels, sst + SSI_SST_CHANNELS_REG);
	writel_relaxed(cl->tx_cfg.arb_mode, sst + SSI_SST_ARBMODE_REG);
	writel_relaxed(cl->tx_cfg.mode, sst + SSI_SST_MODE_REG);
	/* RX */
	writel_relaxed(31, ssr + SSI_SSR_FRAMESIZE_REG);
	writel_relaxed(cl->rx_cfg.num_hw_channels, ssr + SSI_SSR_CHANNELS_REG);
	writel_relaxed(0, ssr + SSI_SSR_TIMEOUT_REG);
	/* Cleanup the break queue if we leave FRAME mode */
	if ((omap_port->ssr.mode == SSI_MODE_FRAME) &&
		(cl->rx_cfg.mode != SSI_MODE_FRAME))
		ssi_flush_queue(&omap_port->brkqueue, cl);
	writel_relaxed(cl->rx_cfg.mode, ssr + SSI_SSR_MODE_REG);
	omap_port->channels = max(cl->rx_cfg.num_hw_channels,
				  cl->tx_cfg.num_hw_channels);
	/* Shadow registering for OFF mode */
	/* SST */
	omap_port->sst.divisor = div;
	omap_port->sst.frame_size = 31;
	omap_port->sst.channels = cl->tx_cfg.num_hw_channels;
	omap_port->sst.arb_mode = cl->tx_cfg.arb_mode;
	omap_port->sst.mode = cl->tx_cfg.mode;
	/* SSR */
	omap_port->ssr.frame_size = 31;
	omap_port->ssr.timeout = 0;
	omap_port->ssr.channels = cl->rx_cfg.num_hw_channels;
	omap_port->ssr.mode = cl->rx_cfg.mode;
out:
	spin_unlock_bh(&omap_port->lock);
	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);

	return err;
}

static int ssi_flush(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	void __iomem *sst = omap_port->sst_base;
	void __iomem *ssr = omap_port->ssr_base;
	unsigned int i;
	u32 err;

	pm_runtime_get_sync(omap_port->pdev);
	spin_lock_bh(&omap_port->lock);

	/* stop all ssi communication */
	pinctrl_pm_select_idle_state(omap_port->pdev);
	udelay(1); /* wait for racing frames */

	/* Stop all DMA transfers */
	for (i = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if (!msg || (port != hsi_get_port(msg->cl)))
			continue;
		writew_relaxed(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
		if (msg->ttype == HSI_MSG_READ)
			pm_runtime_put_autosuspend(omap_port->pdev);
		omap_ssi->gdd_trn[i].msg = NULL;
	}
	/* Flush all SST buffers */
	writel_relaxed(0, sst + SSI_SST_BUFSTATE_REG);
	writel_relaxed(0, sst + SSI_SST_TXSTATE_REG);
	/* Flush all SSR buffers */
	writel_relaxed(0, ssr + SSI_SSR_RXSTATE_REG);
	writel_relaxed(0, ssr + SSI_SSR_BUFSTATE_REG);
	/* Flush all errors */
	err = readl(ssr + SSI_SSR_ERROR_REG);
	writel_relaxed(err, ssr + SSI_SSR_ERRORACK_REG);
	/* Flush break */
	writel_relaxed(0, ssr + SSI_SSR_BREAK_REG);
	/* Clear interrupts */
	writel_relaxed(0, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel_relaxed(0xffffff00,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	writel_relaxed(0, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	writel(0xff, omap_ssi->sys + SSI_GDD_MPU_IRQ_STATUS_REG);
	/* Dequeue all pending requests */
	for (i = 0; i < omap_port->channels; i++) {
		/* Release write clocks */
		if (!list_empty(&omap_port->txqueue[i]))
			pm_runtime_put_autosuspend(omap_port->pdev);
		ssi_flush_queue(&omap_port->txqueue[i], NULL);
		ssi_flush_queue(&omap_port->rxqueue[i], NULL);
	}
	ssi_flush_queue(&omap_port->brkqueue, NULL);

	/* Resume SSI communication */
	pinctrl_pm_select_default_state(omap_port->pdev);

	spin_unlock_bh(&omap_port->lock);
	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);

	return 0;
}

static void start_tx_work(struct work_struct *work)
{
	struct omap_ssi_port *omap_port =
				container_of(work, struct omap_ssi_port, work);
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	pm_runtime_get_sync(omap_port->pdev); /* Grab clocks */
	writel(SSI_WAKE(0), omap_ssi->sys + SSI_SET_WAKE_REG(port->num));
}

static int ssi_start_tx(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);

	dev_dbg(&port->device, "Wake out high %d\n", omap_port->wk_refcount);

	spin_lock_bh(&omap_port->wk_lock);
	if (omap_port->wk_refcount++) {
		spin_unlock_bh(&omap_port->wk_lock);
		return 0;
	}
	spin_unlock_bh(&omap_port->wk_lock);

	schedule_work(&omap_port->work);

	return 0;
}

static int ssi_stop_tx(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(&port->device, "Wake out low %d\n", omap_port->wk_refcount);

	spin_lock_bh(&omap_port->wk_lock);
	BUG_ON(!omap_port->wk_refcount);
	if (--omap_port->wk_refcount) {
		spin_unlock_bh(&omap_port->wk_lock);
		return 0;
	}
	writel(SSI_WAKE(0), omap_ssi->sys + SSI_CLEAR_WAKE_REG(port->num));
	spin_unlock_bh(&omap_port->wk_lock);

	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev); /* Release clocks */


	return 0;
}

static void ssi_transfer(struct omap_ssi_port *omap_port,
							struct list_head *queue)
{
	struct hsi_msg *msg;
	int err = -1;

	pm_runtime_get(omap_port->pdev);
	spin_lock_bh(&omap_port->lock);
	while (err < 0) {
		err = ssi_start_transfer(queue);
		if (err < 0) {
			msg = list_first_entry(queue, struct hsi_msg, link);
			msg->status = HSI_STATUS_ERROR;
			msg->actual_len = 0;
			list_del(&msg->link);
			spin_unlock_bh(&omap_port->lock);
			msg->complete(msg);
			spin_lock_bh(&omap_port->lock);
		}
	}
	spin_unlock_bh(&omap_port->lock);
	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);
}

static void ssi_cleanup_queues(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	unsigned int i;
	u32 rxbufstate = 0;
	u32 txbufstate = 0;
	u32 status = SSI_ERROROCCURED;
	u32 tmp;

	ssi_flush_queue(&omap_port->brkqueue, cl);
	if (list_empty(&omap_port->brkqueue))
		status |= SSI_BREAKDETECTED;

	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->txqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->txqueue[i], struct hsi_msg,
									link);
		if ((msg->cl == cl) && (msg->status == HSI_STATUS_PROCEEDING)) {
			txbufstate |= (1 << i);
			status |= SSI_DATAACCEPT(i);
			/* Release the clocks writes, also GDD ones */
			pm_runtime_mark_last_busy(omap_port->pdev);
			pm_runtime_put_autosuspend(omap_port->pdev);
		}
		ssi_flush_queue(&omap_port->txqueue[i], cl);
	}
	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->rxqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->rxqueue[i], struct hsi_msg,
									link);
		if ((msg->cl == cl) && (msg->status == HSI_STATUS_PROCEEDING)) {
			rxbufstate |= (1 << i);
			status |= SSI_DATAAVAILABLE(i);
		}
		ssi_flush_queue(&omap_port->rxqueue[i], cl);
		/* Check if we keep the error detection interrupt armed */
		if (!list_empty(&omap_port->rxqueue[i]))
			status &= ~SSI_ERROROCCURED;
	}
	/* Cleanup write buffers */
	tmp = readl(omap_port->sst_base + SSI_SST_BUFSTATE_REG);
	tmp &= ~txbufstate;
	writel_relaxed(tmp, omap_port->sst_base + SSI_SST_BUFSTATE_REG);
	/* Cleanup read buffers */
	tmp = readl(omap_port->ssr_base + SSI_SSR_BUFSTATE_REG);
	tmp &= ~rxbufstate;
	writel_relaxed(tmp, omap_port->ssr_base + SSI_SSR_BUFSTATE_REG);
	/* Disarm and ack pending interrupts */
	tmp = readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	tmp &= ~status;
	writel_relaxed(tmp, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel_relaxed(status, omap_ssi->sys +
		SSI_MPU_STATUS_REG(port->num, 0));
}

static void ssi_cleanup_gdd(struct hsi_controller *ssi, struct hsi_client *cl)
{
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_msg *msg;
	unsigned int i;
	u32 val = 0;
	u32 tmp;

	for (i = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if ((!msg) || (msg->cl != cl))
			continue;
		writew_relaxed(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
		val |= (1 << i);
		/*
		 * Clock references for write will be handled in
		 * ssi_cleanup_queues
		 */
		if (msg->ttype == HSI_MSG_READ) {
			pm_runtime_mark_last_busy(omap_port->pdev);
			pm_runtime_put_autosuspend(omap_port->pdev);
		}
		omap_ssi->gdd_trn[i].msg = NULL;
	}
	tmp = readl_relaxed(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp &= ~val;
	writel_relaxed(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	writel(val, omap_ssi->sys + SSI_GDD_MPU_IRQ_STATUS_REG);
}

static int ssi_set_port_mode(struct omap_ssi_port *omap_port, u32 mode)
{
	writel(mode, omap_port->sst_base + SSI_SST_MODE_REG);
	writel(mode, omap_port->ssr_base + SSI_SSR_MODE_REG);
	/* OCP barrier */
	mode = readl(omap_port->ssr_base + SSI_SSR_MODE_REG);

	return 0;
}

static int ssi_release(struct hsi_client *cl)
{
	struct hsi_port *port = hsi_get_port(cl);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);

	pm_runtime_get_sync(omap_port->pdev);
	spin_lock_bh(&omap_port->lock);
	/* Stop all the pending DMA requests for that client */
	ssi_cleanup_gdd(ssi, cl);
	/* Now cleanup all the queues */
	ssi_cleanup_queues(cl);
	/* If it is the last client of the port, do extra checks and cleanup */
	if (port->claimed <= 1) {
		/*
		 * Drop the clock reference for the incoming wake line
		 * if it is still kept high by the other side.
		 */
		if (test_and_clear_bit(SSI_WAKE_EN, &omap_port->flags))
			pm_runtime_put_sync(omap_port->pdev);
		pm_runtime_get(omap_port->pdev);
		/* Stop any SSI TX/RX without a client */
		ssi_set_port_mode(omap_port, SSI_MODE_SLEEP);
		omap_port->sst.mode = SSI_MODE_SLEEP;
		omap_port->ssr.mode = SSI_MODE_SLEEP;
		pm_runtime_put(omap_port->pdev);
		WARN_ON(omap_port->wk_refcount != 0);
	}
	spin_unlock_bh(&omap_port->lock);
	pm_runtime_put_sync(omap_port->pdev);

	return 0;
}



static void ssi_error(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	unsigned int i;
	u32 err;
	u32 val;
	u32 tmp;

	/* ACK error */
	err = readl(omap_port->ssr_base + SSI_SSR_ERROR_REG);
	dev_err(&port->device, "SSI error: 0x%02x\n", err);
	if (!err) {
		dev_dbg(&port->device, "spurious SSI error ignored!\n");
		return;
	}
	spin_lock(&omap_ssi->lock);
	/* Cancel all GDD read transfers */
	for (i = 0, val = 0; i < SSI_MAX_GDD_LCH; i++) {
		msg = omap_ssi->gdd_trn[i].msg;
		if ((msg) && (msg->ttype == HSI_MSG_READ)) {
			writew_relaxed(0, omap_ssi->gdd + SSI_GDD_CCR_REG(i));
			val |= (1 << i);
			omap_ssi->gdd_trn[i].msg = NULL;
		}
	}
	tmp = readl(omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	tmp &= ~val;
	writel_relaxed(tmp, omap_ssi->sys + SSI_GDD_MPU_IRQ_ENABLE_REG);
	spin_unlock(&omap_ssi->lock);
	/* Cancel all PIO read transfers */
	spin_lock(&omap_port->lock);
	tmp = readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	tmp &= 0xfeff00ff; /* Disable error & all dataavailable interrupts */
	writel_relaxed(tmp, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	/* ACK error */
	writel_relaxed(err, omap_port->ssr_base + SSI_SSR_ERRORACK_REG);
	writel_relaxed(SSI_ERROROCCURED,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	/* Signal the error all current pending read requests */
	for (i = 0; i < omap_port->channels; i++) {
		if (list_empty(&omap_port->rxqueue[i]))
			continue;
		msg = list_first_entry(&omap_port->rxqueue[i], struct hsi_msg,
									link);
		list_del(&msg->link);
		msg->status = HSI_STATUS_ERROR;
		spin_unlock(&omap_port->lock);
		msg->complete(msg);
		/* Now restart queued reads if any */
		ssi_transfer(omap_port, &omap_port->rxqueue[i]);
		spin_lock(&omap_port->lock);
	}
	spin_unlock(&omap_port->lock);
}

static void ssi_break_complete(struct hsi_port *port)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct hsi_msg *msg;
	struct hsi_msg *tmp;
	u32 val;

	dev_dbg(&port->device, "HWBREAK received\n");

	spin_lock(&omap_port->lock);
	val = readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	val &= ~SSI_BREAKDETECTED;
	writel_relaxed(val, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel_relaxed(0, omap_port->ssr_base + SSI_SSR_BREAK_REG);
	writel(SSI_BREAKDETECTED,
			omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	spin_unlock(&omap_port->lock);

	list_for_each_entry_safe(msg, tmp, &omap_port->brkqueue, link) {
		msg->status = HSI_STATUS_COMPLETED;
		spin_lock(&omap_port->lock);
		list_del(&msg->link);
		spin_unlock(&omap_port->lock);
		msg->complete(msg);
	}

}

static void ssi_pio_complete(struct hsi_port *port, struct list_head *queue)
{
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_msg *msg;
	u32 *buf;
	u32 reg;
	u32 val;

	spin_lock_bh(&omap_port->lock);
	msg = list_first_entry(queue, struct hsi_msg, link);
	if ((!msg->sgt.nents) || (!msg->sgt.sgl->length)) {
		msg->actual_len = 0;
		msg->status = HSI_STATUS_PENDING;
	}
	if (msg->ttype == HSI_MSG_WRITE)
		val = SSI_DATAACCEPT(msg->channel);
	else
		val = SSI_DATAAVAILABLE(msg->channel);
	if (msg->status == HSI_STATUS_PROCEEDING) {
		buf = sg_virt(msg->sgt.sgl) + msg->actual_len;
		if (msg->ttype == HSI_MSG_WRITE)
			writel(*buf, omap_port->sst_base +
					SSI_SST_BUFFER_CH_REG(msg->channel));
		 else
			*buf = readl(omap_port->ssr_base +
					SSI_SSR_BUFFER_CH_REG(msg->channel));
		dev_dbg(&port->device, "ch %d ttype %d 0x%08x\n", msg->channel,
							msg->ttype, *buf);
		msg->actual_len += sizeof(*buf);
		if (msg->actual_len >= msg->sgt.sgl->length)
			msg->status = HSI_STATUS_COMPLETED;
		/*
		 * Wait for the last written frame to be really sent before
		 * we call the complete callback
		 */
		if ((msg->status == HSI_STATUS_PROCEEDING) ||
				((msg->status == HSI_STATUS_COMPLETED) &&
					(msg->ttype == HSI_MSG_WRITE))) {
			writel(val, omap_ssi->sys +
					SSI_MPU_STATUS_REG(port->num, 0));
			spin_unlock_bh(&omap_port->lock);

			return;
		}

	}
	/* Transfer completed at this point */
	reg = readl(omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	if (msg->ttype == HSI_MSG_WRITE) {
		/* Release clocks for write transfer */
		pm_runtime_mark_last_busy(omap_port->pdev);
		pm_runtime_put_autosuspend(omap_port->pdev);
	}
	reg &= ~val;
	writel_relaxed(reg, omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));
	writel_relaxed(val, omap_ssi->sys + SSI_MPU_STATUS_REG(port->num, 0));
	list_del(&msg->link);
	spin_unlock_bh(&omap_port->lock);
	msg->complete(msg);
	ssi_transfer(omap_port, queue);
}

static irqreturn_t ssi_pio_thread(int irq, void *ssi_port)
{
	struct hsi_port *port = (struct hsi_port *)ssi_port;
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem *sys = omap_ssi->sys;
	unsigned int ch;
	u32 status_reg;

	pm_runtime_get_sync(omap_port->pdev);

	do {
		status_reg = readl(sys + SSI_MPU_STATUS_REG(port->num, 0));
		status_reg &= readl(sys + SSI_MPU_ENABLE_REG(port->num, 0));

		for (ch = 0; ch < omap_port->channels; ch++) {
			if (status_reg & SSI_DATAACCEPT(ch))
				ssi_pio_complete(port, &omap_port->txqueue[ch]);
			if (status_reg & SSI_DATAAVAILABLE(ch))
				ssi_pio_complete(port, &omap_port->rxqueue[ch]);
		}
		if (status_reg & SSI_BREAKDETECTED)
			ssi_break_complete(port);
		if (status_reg & SSI_ERROROCCURED)
			ssi_error(port);

		status_reg = readl(sys + SSI_MPU_STATUS_REG(port->num, 0));
		status_reg &= readl(sys + SSI_MPU_ENABLE_REG(port->num, 0));

		/* TODO: sleep if we retry? */
	} while (status_reg);

	pm_runtime_mark_last_busy(omap_port->pdev);
	pm_runtime_put_autosuspend(omap_port->pdev);

	return IRQ_HANDLED;
}

static irqreturn_t ssi_wake_thread(int irq __maybe_unused, void *ssi_port)
{
	struct hsi_port *port = (struct hsi_port *)ssi_port;
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	if (ssi_wakein(port)) {
		/**
		 * We can have a quick High-Low-High transition in the line.
		 * In such a case if we have long interrupt latencies,
		 * we can miss the low event or get twice a high event.
		 * This workaround will avoid breaking the clock reference
		 * count when such a situation ocurrs.
		 */
		if (!test_and_set_bit(SSI_WAKE_EN, &omap_port->flags))
			pm_runtime_get_sync(omap_port->pdev);
		dev_dbg(&ssi->device, "Wake in high\n");
		if (omap_port->wktest) { /* FIXME: HACK ! To be removed */
			writel(SSI_WAKE(0),
				omap_ssi->sys + SSI_SET_WAKE_REG(port->num));
		}
		hsi_event(port, HSI_EVENT_START_RX);
	} else {
		dev_dbg(&ssi->device, "Wake in low\n");
		if (omap_port->wktest) { /* FIXME: HACK ! To be removed */
			writel(SSI_WAKE(0),
				omap_ssi->sys + SSI_CLEAR_WAKE_REG(port->num));
		}
		hsi_event(port, HSI_EVENT_STOP_RX);
		if (test_and_clear_bit(SSI_WAKE_EN, &omap_port->flags)) {
			pm_runtime_mark_last_busy(omap_port->pdev);
			pm_runtime_put_autosuspend(omap_port->pdev);
		}
	}

	return IRQ_HANDLED;
}

static int ssi_port_irq(struct hsi_port *port, struct platform_device *pd)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	int err;

	err = platform_get_irq(pd, 0);
	if (err < 0)
		return err;
	omap_port->irq = err;
	err = devm_request_threaded_irq(&port->device, omap_port->irq, NULL,
				ssi_pio_thread, IRQF_ONESHOT, "SSI PORT", port);
	if (err < 0)
		dev_err(&port->device, "Request IRQ %d failed (%d)\n",
							omap_port->irq, err);
	return err;
}

static int ssi_wake_irq(struct hsi_port *port, struct platform_device *pd)
{
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	int cawake_irq;
	int err;

	if (!omap_port->wake_gpio) {
		omap_port->wake_irq = -1;
		return 0;
	}

	cawake_irq = gpiod_to_irq(omap_port->wake_gpio);
	omap_port->wake_irq = cawake_irq;

	err = devm_request_threaded_irq(&port->device, cawake_irq, NULL,
		ssi_wake_thread,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"SSI cawake", port);
	if (err < 0)
		dev_err(&port->device, "Request Wake in IRQ %d failed %d\n",
						cawake_irq, err);
	err = enable_irq_wake(cawake_irq);
	if (err < 0)
		dev_err(&port->device, "Enable wake on the wakeline in irq %d failed %d\n",
			cawake_irq, err);

	return err;
}

static void ssi_queues_init(struct omap_ssi_port *omap_port)
{
	unsigned int ch;

	for (ch = 0; ch < SSI_MAX_CHANNELS; ch++) {
		INIT_LIST_HEAD(&omap_port->txqueue[ch]);
		INIT_LIST_HEAD(&omap_port->rxqueue[ch]);
	}
	INIT_LIST_HEAD(&omap_port->brkqueue);
}

static int ssi_port_get_iomem(struct platform_device *pd,
		const char *name, void __iomem **pbase, dma_addr_t *phy)
{
	struct hsi_port *port = platform_get_drvdata(pd);
	struct resource *mem;
	struct resource *ioarea;
	void __iomem *base;

	mem = platform_get_resource_byname(pd, IORESOURCE_MEM, name);
	if (!mem) {
		dev_err(&pd->dev, "IO memory region missing (%s)\n", name);
		return -ENXIO;
	}
	ioarea = devm_request_mem_region(&port->device, mem->start,
					resource_size(mem), dev_name(&pd->dev));
	if (!ioarea) {
		dev_err(&pd->dev, "%s IO memory region request failed\n",
								mem->name);
		return -ENXIO;
	}
	base = devm_ioremap(&port->device, mem->start, resource_size(mem));
	if (!base) {
		dev_err(&pd->dev, "%s IO remap failed\n", mem->name);
		return -ENXIO;
	}
	*pbase = base;

	if (phy)
		*phy = mem->start;

	return 0;
}

static int ssi_port_probe(struct platform_device *pd)
{
	struct device_node *np = pd->dev.of_node;
	struct hsi_port *port;
	struct omap_ssi_port *omap_port;
	struct hsi_controller *ssi = dev_get_drvdata(pd->dev.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	struct gpio_desc *cawake_gpio = NULL;
	u32 port_id;
	int err;

	dev_dbg(&pd->dev, "init ssi port...\n");

	if (!ssi->port || !omap_ssi->port) {
		dev_err(&pd->dev, "ssi controller not initialized!\n");
		err = -ENODEV;
		goto error;
	}

	/* get id of first uninitialized port in controller */
	for (port_id = 0; port_id < ssi->num_ports && omap_ssi->port[port_id];
		port_id++)
		;

	if (port_id >= ssi->num_ports) {
		dev_err(&pd->dev, "port id out of range!\n");
		err = -ENODEV;
		goto error;
	}

	port = ssi->port[port_id];

	if (!np) {
		dev_err(&pd->dev, "missing device tree data\n");
		err = -EINVAL;
		goto error;
	}

	cawake_gpio = devm_gpiod_get(&pd->dev, "ti,ssi-cawake", GPIOD_IN);
	if (IS_ERR(cawake_gpio)) {
		err = PTR_ERR(cawake_gpio);
		dev_err(&pd->dev, "couldn't get cawake gpio (err=%d)!\n", err);
		goto error;
	}

	omap_port = devm_kzalloc(&port->device, sizeof(*omap_port), GFP_KERNEL);
	if (!omap_port) {
		err = -ENOMEM;
		goto error;
	}
	omap_port->wake_gpio = cawake_gpio;
	omap_port->pdev = &pd->dev;
	omap_port->port_id = port_id;

	INIT_DEFERRABLE_WORK(&omap_port->errqueue_work, ssi_process_errqueue);
	INIT_WORK(&omap_port->work, start_tx_work);

	/* initialize HSI port */
	port->async	= ssi_async;
	port->setup	= ssi_setup;
	port->flush	= ssi_flush;
	port->start_tx	= ssi_start_tx;
	port->stop_tx	= ssi_stop_tx;
	port->release	= ssi_release;
	hsi_port_set_drvdata(port, omap_port);
	omap_ssi->port[port_id] = omap_port;

	platform_set_drvdata(pd, port);

	err = ssi_port_get_iomem(pd, "tx", &omap_port->sst_base,
		&omap_port->sst_dma);
	if (err < 0)
		goto error;
	err = ssi_port_get_iomem(pd, "rx", &omap_port->ssr_base,
		&omap_port->ssr_dma);
	if (err < 0)
		goto error;

	err = ssi_port_irq(port, pd);
	if (err < 0)
		goto error;
	err = ssi_wake_irq(port, pd);
	if (err < 0)
		goto error;

	ssi_queues_init(omap_port);
	spin_lock_init(&omap_port->lock);
	spin_lock_init(&omap_port->wk_lock);
	omap_port->dev = &port->device;

	pm_runtime_use_autosuspend(omap_port->pdev);
	pm_runtime_set_autosuspend_delay(omap_port->pdev, 250);
	pm_runtime_enable(omap_port->pdev);

#ifdef CONFIG_DEBUG_FS
	ssi_debug_add_port(omap_port, omap_ssi->dir);
#endif

	hsi_add_clients_from_dt(port, np);

	dev_info(&pd->dev, "ssi port %u successfully initialized\n", port_id);

	return 0;

error:
	return err;
}

static void ssi_port_remove(struct platform_device *pd)
{
	struct hsi_port *port = platform_get_drvdata(pd);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

#ifdef CONFIG_DEBUG_FS
	ssi_debug_remove_port(port);
#endif

	cancel_delayed_work_sync(&omap_port->errqueue_work);

	hsi_port_unregister_clients(port);

	port->async	= hsi_dummy_msg;
	port->setup	= hsi_dummy_cl;
	port->flush	= hsi_dummy_cl;
	port->start_tx	= hsi_dummy_cl;
	port->stop_tx	= hsi_dummy_cl;
	port->release	= hsi_dummy_cl;

	omap_ssi->port[omap_port->port_id] = NULL;
	platform_set_drvdata(pd, NULL);

	pm_runtime_dont_use_autosuspend(&pd->dev);
	pm_runtime_disable(&pd->dev);
}

static int ssi_restore_divisor(struct omap_ssi_port *omap_port)
{
	writel_relaxed(omap_port->sst.divisor,
				omap_port->sst_base + SSI_SST_DIVISOR_REG);

	return 0;
}

void omap_ssi_port_update_fclk(struct hsi_controller *ssi,
			       struct omap_ssi_port *omap_port)
{
	/* update divisor */
	u32 div = ssi_calculate_div(ssi);
	omap_port->sst.divisor = div;
	ssi_restore_divisor(omap_port);
}

#ifdef CONFIG_PM
static int ssi_save_port_ctx(struct omap_ssi_port *omap_port)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	omap_port->sys_mpu_enable = readl(omap_ssi->sys +
					SSI_MPU_ENABLE_REG(port->num, 0));

	return 0;
}

static int ssi_restore_port_ctx(struct omap_ssi_port *omap_port)
{
	struct hsi_port *port = to_hsi_port(omap_port->dev);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);
	void __iomem	*base;

	writel_relaxed(omap_port->sys_mpu_enable,
			omap_ssi->sys + SSI_MPU_ENABLE_REG(port->num, 0));

	/* SST context */
	base = omap_port->sst_base;
	writel_relaxed(omap_port->sst.frame_size, base + SSI_SST_FRAMESIZE_REG);
	writel_relaxed(omap_port->sst.channels, base + SSI_SST_CHANNELS_REG);
	writel_relaxed(omap_port->sst.arb_mode, base + SSI_SST_ARBMODE_REG);

	/* SSR context */
	base = omap_port->ssr_base;
	writel_relaxed(omap_port->ssr.frame_size, base + SSI_SSR_FRAMESIZE_REG);
	writel_relaxed(omap_port->ssr.channels, base + SSI_SSR_CHANNELS_REG);
	writel_relaxed(omap_port->ssr.timeout, base + SSI_SSR_TIMEOUT_REG);

	return 0;
}

static int ssi_restore_port_mode(struct omap_ssi_port *omap_port)
{
	u32 mode;

	writel_relaxed(omap_port->sst.mode,
				omap_port->sst_base + SSI_SST_MODE_REG);
	writel_relaxed(omap_port->ssr.mode,
				omap_port->ssr_base + SSI_SSR_MODE_REG);
	/* OCP barrier */
	mode = readl(omap_port->ssr_base + SSI_SSR_MODE_REG);

	return 0;
}

static int omap_ssi_port_runtime_suspend(struct device *dev)
{
	struct hsi_port *port = dev_get_drvdata(dev);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(dev, "port runtime suspend!\n");

	ssi_set_port_mode(omap_port, SSI_MODE_SLEEP);
	if (omap_ssi->get_loss)
		omap_port->loss_count =
				omap_ssi->get_loss(ssi->device.parent);
	ssi_save_port_ctx(omap_port);

	return 0;
}

static int omap_ssi_port_runtime_resume(struct device *dev)
{
	struct hsi_port *port = dev_get_drvdata(dev);
	struct omap_ssi_port *omap_port = hsi_port_drvdata(port);
	struct hsi_controller *ssi = to_hsi_controller(port->device.parent);
	struct omap_ssi_controller *omap_ssi = hsi_controller_drvdata(ssi);

	dev_dbg(dev, "port runtime resume!\n");

	if ((omap_ssi->get_loss) && (omap_port->loss_count ==
				omap_ssi->get_loss(ssi->device.parent)))
		goto mode; /* We always need to restore the mode & TX divisor */

	ssi_restore_port_ctx(omap_port);

mode:
	ssi_restore_divisor(omap_port);
	ssi_restore_port_mode(omap_port);

	return 0;
}

static const struct dev_pm_ops omap_ssi_port_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_ssi_port_runtime_suspend,
		omap_ssi_port_runtime_resume, NULL)
};

#define DEV_PM_OPS     (&omap_ssi_port_pm_ops)
#else
#define DEV_PM_OPS     NULL
#endif


#ifdef CONFIG_OF
static const struct of_device_id omap_ssi_port_of_match[] = {
	{ .compatible = "ti,omap3-ssi-port", },
	{},
};
MODULE_DEVICE_TABLE(of, omap_ssi_port_of_match);
#else
#define omap_ssi_port_of_match NULL
#endif

struct platform_driver ssi_port_pdriver = {
	.probe = ssi_port_probe,
	.remove_new = ssi_port_remove,
	.driver	= {
		.name	= "omap_ssi_port",
		.of_match_table = omap_ssi_port_of_match,
		.pm	= DEV_PM_OPS,
	},
};
