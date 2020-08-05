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

struct tb_quirk {
	u16 vendor;
	u16 device;
	void (*hook)(struct tb_switch *sw);
};

static const struct tb_quirk tb_quirks[] = {
	/* Dell WD19TB supports self-authentication on unplug */
	{ 0x00d4, 0xb070, quirk_force_power_link },
};

/**
 * tb_check_quirks() - Check for quirks to apply
 * @sw: Thunderbolt switch
 *
 *  Apply any quirks for the Thunderbolt controller
 */
void tb_check_quirks(struct tb_switch *sw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tb_quirks); i++) {
		const struct tb_quirk *q = &tb_quirks[i];

		if (sw->device == q->device && sw->vendor == q->vendor)
			q->hook(sw);
	}
}
