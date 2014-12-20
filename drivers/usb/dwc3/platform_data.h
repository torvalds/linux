/**
 * platform_data.h - USB DWC3 Platform Data Support
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * Author: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>

struct dwc3_platform_data {
	enum usb_device_speed maximum_speed;
	enum usb_dr_mode dr_mode;
	bool tx_fifo_resize;

	unsigned is_utmi_l1_suspend:1;
	u8 hird_threshold;

	u8 lpm_nyet_threshold;

	unsigned disable_scramble_quirk:1;
	unsigned has_lpm_erratum:1;
	unsigned u2exit_lfps_quirk:1;
	unsigned u2ss_inp3_quirk:1;
	unsigned req_p1p2p3_quirk:1;
	unsigned del_p1p2p3_quirk:1;
	unsigned del_phy_power_chg_quirk:1;
	unsigned lfps_filter_quirk:1;
	unsigned rx_detect_poll_quirk:1;
	unsigned dis_u3_susphy_quirk:1;
	unsigned dis_u2_susphy_quirk:1;

	unsigned tx_de_emphasis_quirk:1;
	unsigned tx_de_emphasis:2;
};
