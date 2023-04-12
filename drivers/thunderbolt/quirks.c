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

static void quirk_usb3_maximum_bandwidth(struct tb_switch *sw)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (!tb_port_is_usb3_down(port))
			continue;
		port->max_bw = 16376;
		tb_port_dbg(port, "USB3 maximum bandwidth limited to %u Mb/s\n",
			    port->max_bw);
	}
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
	 * Limit the maximum USB3 bandwidth for the following Intel USB4
	 * host routers due to a hardware issue.
	 */
	{ 0x8087, PCI_DEVICE_ID_INTEL_ADL_NHI0, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_ADL_NHI1, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_RPL_NHI0, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_RPL_NHI1, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_MTL_M_NHI0, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_MTL_P_NHI0, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
	{ 0x8087, PCI_DEVICE_ID_INTEL_MTL_P_NHI1, 0x0000, 0x0000,
		  quirk_usb3_maximum_bandwidth },
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
