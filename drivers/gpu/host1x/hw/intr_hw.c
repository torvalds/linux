/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

	host1x_sync_writel(host, BIT_MASK(id),
		HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(BIT_WORD(id)));
	host1x_sync_writel(host, BIT_MASK(id),
		HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(BIT_WORD(id)));

	queue_work(host->intr_wq, &syncpt->intr.work);
}

static irqreturn_t syncpt_thresh_isr(int irq, void *dev_id)
{
	struct host1x *host = dev_id;
	unsigned long reg;
	int i, id;

	for (i = 0; i <= BIT_WORD(host->info->nb_pts); i++) {
		reg = host1x_sync_readl(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));
		for_each_set_bit(id, &reg, BITS_PER_LONG) {
			struct host1x_syncpt *syncpt =
				host->syncpt + (i * BITS_PER_LONG + id);
			host1x_intr_syncpt_handle(syncpt);
		}
	}

	return IRQ_HANDLED;
}

static void _host1x_intr_disable_all_syncpt_intrs(struct host1x *host)
{
	u32 i;

	for (i = 0; i <= BIT_WORD(host->info->nb_pts); ++i) {
		host1x_sync_writel(host, 0xffffffffu,
			HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(i));
		host1x_sync_writel(host, 0xffffffffu,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));
	}
}

static int _host1x_intr_init_host_sync(struct host1x *host, u32 cpm,
	void (*syncpt_thresh_work)(struct work_struct *))
{
	int i, err;

	host1x_hw_intr_disable_all_syncpt_intrs(host);

	for (i = 0; i < host->info->nb_pts; i++)
		INIT_WORK(&host->syncpt[i].intr.work, syncpt_thresh_work);

	err = devm_request_irq(host->dev, host->intr_syncpt_irq,
			       syncpt_thresh_isr, IRQF_SHARED,
			       "host1x_syncpt", host);
	if (IS_ERR_VALUE(err)) {
		WARN_ON(1);
		return err;
	}

	/* disable the ip_busy_timeout. this prevents write drops */
	host1x_sync_writel(host, 0, HOST1X_SYNC_IP_BUSY_TIMEOUT);

	/*
	 * increase the auto-ack timout to the maximum value. 2d will hang
	 * otherwise on Tegra2.
	 */
	host1x_sync_writel(host, 0xff, HOST1X_SYNC_CTXSW_TIMEOUT_CFG);

	/* update host clocks per usec */
	host1x_sync_writel(host, cpm, HOST1X_SYNC_USEC_CLK);

	return 0;
}

static void _host1x_intr_set_syncpt_threshold(struct host1x *host,
	u32 id, u32 thresh)
{
	host1x_sync_writel(host, thresh, HOST1X_SYNC_SYNCPT_INT_THRESH(id));
}

static void _host1x_intr_enable_syncpt_intr(struct host1x *host, u32 id)
{
	host1x_sync_writel(host, BIT_MASK(id),
		HOST1X_SYNC_SYNCPT_THRESH_INT_ENABLE_CPU0(BIT_WORD(id)));
}

static void _host1x_intr_disable_syncpt_intr(struct host1x *host, u32 id)
{
	host1x_sync_writel(host, BIT_MASK(id),
		HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(BIT_WORD(id)));
	host1x_sync_writel(host, BIT_MASK(id),
		HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(BIT_WORD(id)));
}

static int _host1x_free_syncpt_irq(struct host1x *host)
{
	devm_free_irq(host->dev, host->intr_syncpt_irq, host);
	flush_workqueue(host->intr_wq);
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
