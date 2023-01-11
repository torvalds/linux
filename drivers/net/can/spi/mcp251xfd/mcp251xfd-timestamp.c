// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2021, 2023 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/clocksource.h>
#include <linux/workqueue.h>

#include "mcp251xfd.h"

static u64 mcp251xfd_timestamp_raw_read(const struct cyclecounter *cc)
{
	const struct mcp251xfd_priv *priv;
	u32 ts_raw = 0;
	int err;

	priv = container_of(cc, struct mcp251xfd_priv, cc);
	err = mcp251xfd_get_timestamp_raw(priv, &ts_raw);
	if (err)
		netdev_err(priv->ndev,
			   "Error %d while reading timestamp. HW timestamps may be inaccurate.",
			   err);

	return ts_raw;
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

void mcp251xfd_timestamp_init(struct mcp251xfd_priv *priv)
{
	struct cyclecounter *cc = &priv->cc;

	cc->read = mcp251xfd_timestamp_raw_read;
	cc->mask = CYCLECOUNTER_MASK(32);
	cc->shift = 1;
	cc->mult = clocksource_hz2mult(priv->can.clock.freq, cc->shift);

	INIT_DELAYED_WORK(&priv->timestamp, mcp251xfd_timestamp_work);
}

void mcp251xfd_timestamp_start(struct mcp251xfd_priv *priv)
{
	timecounter_init(&priv->tc, &priv->cc, ktime_get_real_ns());
	schedule_delayed_work(&priv->timestamp,
			      MCP251XFD_TIMESTAMP_WORK_DELAY_SEC * HZ);
}

void mcp251xfd_timestamp_stop(struct mcp251xfd_priv *priv)
{
	cancel_delayed_work_sync(&priv->timestamp);
}
