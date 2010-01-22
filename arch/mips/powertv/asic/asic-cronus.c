/*
 * Locations of devices in the Cronus ASIC
 *
 * Copyright (C) 2005-2009 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author:       Ken Eppinett
 *               David Schleef <ds@schleef.org>
 *
 * Description:  Defines the platform resources for the SA settop.
 */

#include <asm/mach-powertv/asic.h>

const struct register_map cronus_register_map = {
	.eic_slow0_strt_add = 0x000000,
	.eic_cfg_bits = 0x000038,
	.eic_ready_status = 0x00004C,

	.chipver3 = 0x2A0800,
	.chipver2 = 0x2A0804,
	.chipver1 = 0x2A0808,
	.chipver0 = 0x2A080C,

	/* The registers of IRBlaster */
	.uart1_intstat = 0x2A1800,
	.uart1_inten = 0x2A1804,
	.uart1_config1 = 0x2A1808,
	.uart1_config2 = 0x2A180C,
	.uart1_divisorhi = 0x2A1810,
	.uart1_divisorlo = 0x2A1814,
	.uart1_data = 0x2A1818,
	.uart1_status = 0x2A181C,

	.int_stat_3 = 0x2A2800,
	.int_stat_2 = 0x2A2804,
	.int_stat_1 = 0x2A2808,
	.int_stat_0 = 0x2A280C,
	.int_config = 0x2A2810,
	.int_int_scan = 0x2A2818,
	.ien_int_3 = 0x2A2830,
	.ien_int_2 = 0x2A2834,
	.ien_int_1 = 0x2A2838,
	.ien_int_0 = 0x2A283C,
	.int_level_3_3 = 0x2A2880,
	.int_level_3_2 = 0x2A2884,
	.int_level_3_1 = 0x2A2888,
	.int_level_3_0 = 0x2A288C,
	.int_level_2_3 = 0x2A2890,
	.int_level_2_2 = 0x2A2894,
	.int_level_2_1 = 0x2A2898,
	.int_level_2_0 = 0x2A289C,
	.int_level_1_3 = 0x2A28A0,
	.int_level_1_2 = 0x2A28A4,
	.int_level_1_1 = 0x2A28A8,
	.int_level_1_0 = 0x2A28AC,
	.int_level_0_3 = 0x2A28B0,
	.int_level_0_2 = 0x2A28B4,
	.int_level_0_1 = 0x2A28B8,
	.int_level_0_0 = 0x2A28BC,
	.int_docsis_en = 0x2A28F4,

	.mips_pll_setup = 0x1C0000,
	.usb_fs = 0x1C0018,
	.test_bus = 0x1C00CC,
	.crt_spare = 0x1c00d4,
	.usb2_ohci_int_mask = 0x20000C,
	.usb2_strap = 0x200014,
	.ehci_hcapbase = 0x21FE00,
	.ohci_hc_revision = 0x1E0000,
	.bcm1_bs_lmi_steer = 0x2E0008,
	.usb2_control = 0x2E004C,
	.usb2_stbus_obc = 0x21FF00,
	.usb2_stbus_mess_size = 0x21FF04,
	.usb2_stbus_chunk_size = 0x21FF08,

	.pcie_regs = 0x220000,
	.tim_ch = 0x2A2C10,
	.tim_cl = 0x2A2C14,
	.gpio_dout = 0x2A2C20,
	.gpio_din = 0x2A2C24,
	.gpio_dir = 0x2A2C2C,
	.watchdog = 0x2A2C30,
	.front_panel = 0x2A3800,
};
