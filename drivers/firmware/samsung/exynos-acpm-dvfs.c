// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2025 Linaro Ltd.
 */

#include <linux/bitfield.h>
#include <linux/firmware/samsung/exynos-acpm-protocol.h>
#include <linux/ktime.h>
#include <linux/types.h>
#include <linux/units.h>

#include "exynos-acpm.h"
#include "exynos-acpm-dvfs.h"

#define ACPM_DVFS_ID			GENMASK(11, 0)
#define ACPM_DVFS_REQ_TYPE		GENMASK(15, 0)

#define ACPM_DVFS_FREQ_REQ		0
#define ACPM_DVFS_FREQ_GET		1

static void acpm_dvfs_set_xfer(struct acpm_xfer *xfer, u32 *cmd, size_t cmdlen,
			       unsigned int acpm_chan_id, bool response)
{
	xfer->acpm_chan_id = acpm_chan_id;
	xfer->txd = cmd;
	xfer->txlen = cmdlen;

	if (response) {
		xfer->rxd = cmd;
		xfer->rxlen = cmdlen;
	}
}

static void acpm_dvfs_init_set_rate_cmd(u32 cmd[4], unsigned int clk_id,
					unsigned long rate)
{
	cmd[0] = FIELD_PREP(ACPM_DVFS_ID, clk_id);
	cmd[1] = rate / HZ_PER_KHZ;
	cmd[2] = FIELD_PREP(ACPM_DVFS_REQ_TYPE, ACPM_DVFS_FREQ_REQ);
	cmd[3] = ktime_to_ms(ktime_get());
}

int acpm_dvfs_set_rate(const struct acpm_handle *handle,
		       unsigned int acpm_chan_id, unsigned int clk_id,
		       unsigned long rate)
{
	struct acpm_xfer xfer = {0};
	u32 cmd[4];

	acpm_dvfs_init_set_rate_cmd(cmd, clk_id, rate);
	acpm_dvfs_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id, false);

	return acpm_do_xfer(handle, &xfer);
}

static void acpm_dvfs_init_get_rate_cmd(u32 cmd[4], unsigned int clk_id)
{
	cmd[0] = FIELD_PREP(ACPM_DVFS_ID, clk_id);
	cmd[2] = FIELD_PREP(ACPM_DVFS_REQ_TYPE, ACPM_DVFS_FREQ_GET);
	cmd[3] = ktime_to_ms(ktime_get());
}

unsigned long acpm_dvfs_get_rate(const struct acpm_handle *handle,
				 unsigned int acpm_chan_id, unsigned int clk_id)
{
	struct acpm_xfer xfer;
	unsigned int cmd[4] = {0};
	int ret;

	acpm_dvfs_init_get_rate_cmd(cmd, clk_id);
	acpm_dvfs_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id, true);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return 0;

	return xfer.rxd[1] * HZ_PER_KHZ;
}
