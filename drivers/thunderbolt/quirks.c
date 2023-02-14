// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - quirks
 *
 * Copyright (c) 2020 Mario Limonciello <mario.limonciello@dell.com>
 */

#include "tb.h"

static void quirk_force_power_link(struct tb_switch *sw)
{
	sw->quirks |= QUIRK_FORCE_POWER_LINK_CONTROLLER;
}

static void quirk_dp_credit_allocation(struct tb_switch *sw)
{
	if (sw->credit_allocation && sw->min_dp_main_credits == 56) {
		sw->min_dp_main_credits = 18;
		tb_sw_dbg(sw, "quirked DP main: %u\n", sw->min_dp_main_credits);
	}
}

static void quirk_clx_disable(struct tb_switch *sw)
{
	sw->quirks |= QUIRK_NO_CLX;
	tb_sw_dbg(sw, "disabling CL states\n");
}

struct tb_quirk {
	u16 hw_vendor_id;
	u16 hw_device_id;
	u16 vendor;
	u16 device;
	void (*hook)(struct tb_switch *sw);
};

static const struct tb_quirk tb_quirks[] = {
	/* Dell WD19TB supports self-authentication on unplug */
	{ 0x0000, 0x0000, 0x00d4, 0xb070, quirk_force_power_link },
	{ 0x0000, 0x0000, 0x00d4, 0xb071, quirk_force_power_link },
	/*
	 * Intel Goshen Ridge NVM 27 and before report wrong number of
	 * DP buffers.
	 */
	{ 0x8087, 0x0b26, 0x0000, 0x0000, quirk_dp_credit_allocation },
	/*
	 * CLx is not supported on AMD USB4 Yellow Carp and Pink Sardine platforms.
	 */
	{ 0x0438, 0x0208, 0x0000, 0x0000, quirk_clx_disable },
	{ 0x0438, 0x0209, 0x0000, 0x0000, quirk_clx_disable },
	{ 0x0438, 0x020a, 0x0000, 0x0000, quirk_clx_disable },
	{ 0x0438, 0x020b, 0x0000, 0x0000, quirk_clx_disable },
};

/**
 * tb_check_quirks() - Check for quirks to apply
 * @sw: Thunderbolt switch
 *
 * Apply any quirks for the Thunderbolt controller.
 */
void tb_check_quirks(struct tb_switch *sw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tb_quirks); i++) {
		const struct tb_quirk *q = &tb_quirks[i];

		if (q->hw_vendor_id && q->hw_vendor_id != sw->config.vendor_id)
			continue;
		if (q->hw_device_id && q->hw_device_id != sw->config.device_id)
			continue;
		if (q->vendor && q->vendor != sw->vendor)
			continue;
		if (q->device && q->device != sw->device)
			continue;

		q->hook(sw);
	}
}
