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

static irqreturn_t syncpt_thresh_isr(int irq, void *dev_id)
{
	struct host1x *host = dev_id;
	unsigned long reg;
	unsigned int i, id;

	for (i = 0; i < DIV_ROUND_UP(host->info->nb_pts, 32); i++) {
		reg = host1x_sync_readl(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));

		host1x_sync_writel(host, reg,
			HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(i));
		host1x_sync_writel(host, reg,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));

		for_each_set_bit(id, &reg, 32)
			host1x_intr_handle_interrupt(host, i * 32 + id);
	}

	return IRQ_HANDLED;
}

static void host1x_intr_disable_all_syncpt_intrs(struct host1x *host)
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
host1x_intr_init_host_sync(struct host1x *host, u32 cpm)
{
	int err;

	host1x_hw_intr_disable_all_syncpt_intrs(host);

	err = devm_request_irq(host->dev, host->syncpt_irq,
			       syncpt_thresh_isr, IRQF_SHARED,
			       "host1x_syncpt", host);
	if (err < 0)
		return err;

	intr_hw_init(host, cpm);

	return 0;
}

static void host1x_intr_set_syncpt_threshold(struct host1x *host,
					      unsigned int id,
					      u32 thresh)
{
	host1x_sync_writel(host, thresh, HOST1X_SYNC_SYNCPT_INT_THRESH(id));
}

static void host1x_intr_enable_syncpt_intr(struct host1x *host,
					    unsigned int id)
{
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0(id / 32));
}

static void host1x_intr_disable_syncpt_intr(struct host1x *host,
					     unsigned int id)
{
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(id / 32));
	host1x_sync_writel(host, BIT(id % 32),
		HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(id / 32));
}

static const struct host1x_intr_ops host1x_intr_ops = {
	.init_host_sync = host1x_intr_init_host_sync,
	.set_syncpt_threshold = host1x_intr_set_syncpt_threshold,
	.enable_syncpt_intr = host1x_intr_enable_syncpt_intr,
	.disable_syncpt_intr = host1x_intr_disable_syncpt_intr,
	.disable_all_syncpt_intrs = host1x_intr_disable_all_syncpt_intrs,
};
