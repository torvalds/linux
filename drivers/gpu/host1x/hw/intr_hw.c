// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include "../intr.h"
#include "../dev.h"

/*
 * Sync point threshold interrupt service function
 * Handles sync point threshold triggers, in interrupt context
 */
static void host1x_intr_syncpt_handle(struct host1x_syncpt *syncpt)
{
	unsigned int id = syncpt->id;
	struct host1x *host = syncpt->host;

	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(id / 32));
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(id / 32));

	schedule_work(&syncpt->intr.work);
}

static irqreturn_t syncpt_thresh_isr(int irq, void *dev_id)
{
	struct host1x *host = dev_id;
	unsigned long reg;
	unsigned int i, id;

	for (i = 0; i < DIV_ROUND_UP(host->info->nb_pts, 32); i++) {
		reg = host1x_sync_readl(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));
		for_each_set_bit(id, &reg, 32) {
			struct host1x_syncpt *syncpt =
				host->syncpt + (i * 32 + id);
			host1x_intr_syncpt_handle(syncpt);
		}
	}

	return IRQ_HANDLED;
}

static void _host1x_intr_disable_all_syncpt_intrs(struct host1x *host)
{
	unsigned int i;

	for (i = 0; i < DIV_ROUND_UP(host->info->nb_pts, 32); ++i) {
		host1x_sync_writel(host, 0xffffffffu,
			HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(i));
		host1x_sync_writel(host, 0xffffffffu,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));
	}
}

static void intr_hw_init(struct host1x *host, u32 cpm)
{
#if HOST1X_HW < 6
	/* disable the ip_busy_timeout. this prevents write drops */
	host1x_sync_writel(host, 0, HOST1X_SYNC_IP_BUSY_TIMEOUT);

	/*
	 * increase the auto-ack timout to the maximum value. 2d will hang
	 * otherwise on Tegra2.
	 */
	host1x_sync_writel(host, 0xff, HOST1X_SYNC_CTXSW_TIMEOUT_CFG);

	/* update host clocks per usec */
	host1x_sync_writel(host, cpm, HOST1X_SYNC_USEC_CLK);
#endif
#if HOST1X_HW >= 8
	u32 id;

	/*
	 * Program threshold interrupt destination among 8 lines per VM,
	 * per syncpoint. For now, just direct all to the first interrupt
	 * line.
	 */
	for (id = 0; id < host->info->nb_pts; id++)
		host1x_sync_writel(host, 0, HOST1X_SYNC_SYNCPT_INTR_DEST(id));
#endif
}

static int
_host1x_intr_init_host_sync(struct host1x *host, u32 cpm,
			    void (*syncpt_thresh_work)(struct work_struct *))
{
	unsigned int i;
	int err;

	host1x_hw_intr_disable_all_syncpt_intrs(host);

	for (i = 0; i < host->info->nb_pts; i++)
		INIT_WORK(&host->syncpt[i].intr.work, syncpt_thresh_work);

	err = devm_request_irq(host->dev, host->intr_syncpt_irq,
			       syncpt_thresh_isr, IRQF_SHARED,
			       "host1x_syncpt", host);
	if (err < 0) {
		WARN_ON(1);
		return err;
	}

	intr_hw_init(host, cpm);

	return 0;
}

static void _host1x_intr_set_syncpt_threshold(struct host1x *host,
					      unsigned int id,
					      u32 thresh)
{
	host1x_sync_writel(host, thresh, HOST1X_SYNC_SYNCPT_INT_THRESH(id));
}

static void _host1x_intr_enable_syncpt_intr(struct host1x *host,
					    unsigned int id)
{
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0(id / 32));
}

static void _host1x_intr_disable_syncpt_intr(struct host1x *host,
					     unsigned int id)
{
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(id / 32));
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(id / 32));
}

static int _host1x_free_syncpt_irq(struct host1x *host)
{
	unsigned int i;

	devm_free_irq(host->dev, host->intr_syncpt_irq, host);

	for (i = 0; i < host->info->nb_pts; i++)
		cancel_work_sync(&host->syncpt[i].intr.work);

	return 0;
}

static const struct host1x_intr_ops host1x_intr_ops = {
	.init_host_sync = _host1x_intr_init_host_sync,
	.set_syncpt_threshold = _host1x_intr_set_syncpt_threshold,
	.enable_syncpt_intr = _host1x_intr_enable_syncpt_intr,
	.disable_syncpt_intr = _host1x_intr_disable_syncpt_intr,
	.disable_all_syncpt_intrs = _host1x_intr_disable_all_syncpt_intrs,
	.free_syncpt_irq = _host1x_free_syncpt_irq,
};
