// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/clocksource.h>
#include <linux/workqueue.h>

#include "mcp251xfd.h"

static u64 mcp251xfd_timestamp_read(const struct cyclecounter *cc)
{
	const struct mcp251xfd_priv *priv;
	u32 timestamp = 0;
	int err;

	priv = container_of(cc, struct mcp251xfd_priv, cc);
	err = mcp251xfd_get_timestamp(priv, &timestamp);
	if (err)
		netdev_err(priv->ndev,
			   "Error %d while reading timestamp. HW timestamps may be inaccurate.",
			   err);

	return timestamp;
}

static void mcp251xfd_timestamp_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mcp251xfd_priv *priv;

	priv = container_of(delayed_work, struct mcp251xfd_priv, timestamp);
	timecounter_read(&priv->tc);

	schedule_delayed_work(&priv->timestamp,
			      MCP251XFD_TIMESTAMP_WORK_DELAY_SEC * HZ);
}

void mcp251xfd_skb_set_timestamp(const struct mcp251xfd_priv *priv,
				 struct sk_buff *skb, u32 timestamp)
{
	struct skb_shared_hwtstamps *hwtstamps = skb_hwtstamps(skb);
	u64 ns;

	ns = timecounter_cyc2time(&priv->tc, timestamp);
	hwtstamps->hwtstamp = ns_to_ktime(ns);
}

void mcp251xfd_timestamp_init(struct mcp251xfd_priv *priv)
{
	struct cyclecounter *cc = &priv->cc;

	cc->read = mcp251xfd_timestamp_read;
	cc->mask = CYCLECOUNTER_MASK(32);
	cc->shift = 1;
	cc->mult = clocksource_hz2mult(priv->can.clock.freq, cc->shift);

	timecounter_init(&priv->tc, &priv->cc, ktime_get_real_ns());

	INIT_DELAYED_WORK(&priv->timestamp, mcp251xfd_timestamp_work);
	schedule_delayed_work(&priv->timestamp,
			      MCP251XFD_TIMESTAMP_WORK_DELAY_SEC * HZ);
}

void mcp251xfd_timestamp_stop(struct mcp251xfd_priv *priv)
{
	cancel_delayed_work_sync(&priv->timestamp);
}
