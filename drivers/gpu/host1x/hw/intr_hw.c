// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#include <linux/io.h>

#include "../intr.h"
#include "../dev.h"

static void process_32_syncpts(struct host1x *host, unsigned long val, u32 reg_offset)
{
	unsigned int id;

	if (!val)
		return;

	host1x_sync_writel(host, val, HOST1X_SYNC_SYNCPT_THRESH_INT_DISABLE(reg_offset));
	host1x_sync_writel(host, val, HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(reg_offset));

	for_each_set_bit(id, &val, 32)
		host1x_intr_handle_interrupt(host, reg_offset * 32 + id);
}

static irqreturn_t syncpt_thresh_isr(int irq, void *dev_id)
{
	struct host1x_intr_irq_data *irq_data = dev_id;
	struct host1x *host = irq_data->host;
	unsigned long reg;
	unsigned int i;

#if !defined(CONFIG_64BIT)
	for (i = irq_data->offset; i < DIV_ROUND_UP(host->info->nb_pts, 32);
	     i += host->num_syncpt_irqs) {
		reg = host1x_sync_readl(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));

		process_32_syncpts(host, reg, i);
	}
#elif HOST1X_HW == 6 || HOST1X_HW == 7
	/*
	 * Tegra186 and Tegra194 have the first INT_STATUS register not 64-bit aligned,
	 * and only have one interrupt line.
	 */
	reg = host1x_sync_readl(host, HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(0));
	process_32_syncpts(host, reg, 0);

	for (i = 1; i < (host->info->nb_pts / 32) - 1; i += 2) {
		reg = host1x_sync_readq(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));

		process_32_syncpts(host, lower_32_bits(reg), i);
		process_32_syncpts(host, upper_32_bits(reg), i + 1);
	}

	reg = host1x_sync_readl(host, HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i));
	process_32_syncpts(host, reg, i);
#else
	/* All 64-bit capable SoCs have number of syncpoints divisible by 64 */
	for (i = irq_data->offset; i < DIV_ROUND_UP(host->info->nb_pts, 64);
	     i += host->num_syncpt_irqs) {
		reg = host1x_sync_readq(host,
			HOST1X_SYNC_SYNCPT_THRESH_CPU0_INT_STATUS(i * 2));

		process_32_syncpts(host, lower_32_bits(reg), i * 2 + 0);
		process_32_syncpts(host, upper_32_bits(reg), i * 2 + 1);
	}
#endif

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

static int
host1x_intr_init_host_sync(struct host1x *host, u32 cpm)
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
	 * per syncpoint. For each group of 64 syncpoints (corresponding to two
	 * interrupt status registers), direct to one interrupt line, going
	 * around in a round robin fashion.
	 */
	for (id = 0; id < host->info->nb_pts; id++) {
		u32 reg_offset = id / 64;
		u32 irq_index = reg_offset % host->num_syncpt_irqs;

		host1x_sync_writel(host, irq_index, HOST1X_SYNC_SYNCPT_INTR_DEST(id));
	}
#endif

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
	.isr = syncpt_thresh_isr,
};
