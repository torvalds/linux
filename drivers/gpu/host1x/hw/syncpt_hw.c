/*
 * Tegra host1x Syncpoints
 *
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

#include <linux/io.h>

#include "../dev.h"
#include "../syncpt.h"

/*
 * Write the current syncpoint value back to hw.
 */
static void syncpt_restore(struct host1x_syncpt *sp)
{
	u32 min = host1x_syncpt_read_min(sp);
	struct host1x *host = sp->host;

	host1x_sync_writel(host, min, HOST1X_SYNC_SYNCPT(sp->id));
}

/*
 * Write the current waitbase value back to hw.
 */
static void syncpt_restore_wait_base(struct host1x_syncpt *sp)
{
	struct host1x *host = sp->host;

	host1x_sync_writel(host, sp->base_val,
			   HOST1X_SYNC_SYNCPT_BASE(sp->id));
}

/*
 * Read waitbase value from hw.
 */
static void syncpt_read_wait_base(struct host1x_syncpt *sp)
{
	struct host1x *host = sp->host;

	sp->base_val =
		host1x_sync_readl(host, HOST1X_SYNC_SYNCPT_BASE(sp->id));
}

/*
 * Updates the last value read from hardware.
 */
static u32 syncpt_load(struct host1x_syncpt *sp)
{
	struct host1x *host = sp->host;
	u32 old, live;

	/* Loop in case there's a race writing to min_val */
	do {
		old = host1x_syncpt_read_min(sp);
		live = host1x_sync_readl(host, HOST1X_SYNC_SYNCPT(sp->id));
	} while ((u32)atomic_cmpxchg(&sp->min_val, old, live) != old);

	if (!host1x_syncpt_check_max(sp, live))
		dev_err(host->dev, "%s failed: id=%u, min=%d, max=%d\n",
			__func__, sp->id, host1x_syncpt_read_min(sp),
			host1x_syncpt_read_max(sp));

	return live;
}

/*
 * Write a cpu syncpoint increment to the hardware, without touching
 * the cache.
 */
static int syncpt_cpu_incr(struct host1x_syncpt *sp)
{
	struct host1x *host = sp->host;
	u32 reg_offset = sp->id / 32;

	if (!host1x_syncpt_client_managed(sp) &&
	    host1x_syncpt_idle(sp))
		return -EINVAL;

	host1x_sync_writel(host, BIT(sp->id % 32),
			   HOST1X_SYNC_SYNCPT_CPU_INCR(reg_offset));
	wmb();

	return 0;
}

/**
 * syncpt_assign_to_channel() - Assign syncpoint to channel
 * @sp: syncpoint
 * @ch: channel
 *
 * On chips with the syncpoint protection feature (Tegra186+), assign @sp to
 * @ch, preventing other channels from incrementing the syncpoints. If @ch is
 * NULL, unassigns the syncpoint.
 *
 * On older chips, do nothing.
 */
static void syncpt_assign_to_channel(struct host1x_syncpt *sp,
				  struct host1x_channel *ch)
{
#if HOST1X_HW >= 6
	struct host1x *host = sp->host;

	if (!host->hv_regs)
		return;

	host1x_sync_writel(host,
			   HOST1X_SYNC_SYNCPT_CH_APP_CH(ch ? ch->id : 0xff),
			   HOST1X_SYNC_SYNCPT_CH_APP(sp->id));
#endif
}

/**
 * syncpt_enable_protection() - Enable syncpoint protection
 * @host: host1x instance
 *
 * On chips with the syncpoint protection feature (Tegra186+), enable this
 * feature. On older chips, do nothing.
 */
static void syncpt_enable_protection(struct host1x *host)
{
#if HOST1X_HW >= 6
	if (!host->hv_regs)
		return;

	host1x_hypervisor_writel(host, HOST1X_HV_SYNCPT_PROT_EN_CH_EN,
				 HOST1X_HV_SYNCPT_PROT_EN);
#endif
}

static const struct host1x_syncpt_ops host1x_syncpt_ops = {
	.restore = syncpt_restore,
	.restore_wait_base = syncpt_restore_wait_base,
	.load_wait_base = syncpt_read_wait_base,
	.load = syncpt_load,
	.cpu_incr = syncpt_cpu_incr,
	.assign_to_channel = syncpt_assign_to_channel,
	.enable_protection = syncpt_enable_protection,
};
