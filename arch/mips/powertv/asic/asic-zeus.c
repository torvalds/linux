/*
 * Locations of devices in the Zeus ASIC
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

const struct register_map zeus_register_map = {
	.eic_slow0_strt_add = 0x000000,
	.eic_cfg_bits = 0x000038,
	.eic_ready_status = 0x00004c,

	.chipver3 = 0x280800,
	.chipver2 = 0x280804,
	.chipver1 = 0x280808,
	.chipver0 = 0x28080c,

	/* The registers of IRBlaster */
	.uart1_intstat = 0x281800,
	.uart1_inten = 0x281804,
	.uart1_config1 = 0x281808,
	.uart1_config2 = 0x28180C,
	.uart1_divisorhi = 0x281810,
	.uart1_divisorlo = 0x281814,
	.uart1_data = 0x281818,
	.uart1_status = 0x28181C,

	.int_stat_3 = 0x282800,
	.int_stat_2 = 0x282804,
	.int_stat_1 = 0x282808,
	.int_stat_0 = 0x28280c,
	.int_config = 0x282810,
	.int_int_scan = 0x282818,
	.ien_int_3 = 0x282830,
	.ien_int_2 = 0x282834,
	.ien_int_1 = 0x282838,
	.ien_int_0 = 0x28283c,
	.int_level_3_3 = 0x282880,
	.int_level_3_2 = 0x282884,
	.int_level_3_1 = 0x282888,
	.int_level_3_0 = 0x28288c,
	.int_level_2_3 = 0x282890,
	.int_level_2_2 = 0x282894,
	.int_level_2_1 = 0x282898,
	.int_level_2_0 = 0x28289c,
	.int_level_1_3 = 0x2828a0,
	.int_level_1_2 = 0x2828a4,
	.int_level_1_1 = 0x2828a8,
	.int_level_1_0 = 0x2828ac,
	.int_level_0_3 = 0x2828b0,
	.int_level_0_2 = 0x2828b4,
	.int_level_0_1 = 0x2828b8,
	.int_level_0_0 = 0x2828bc,
	.int_docsis_en = 0x2828F4,

	.mips_pll_setup = 0x1a0000,
	.usb_fs = 0x1a0018,
	.test_bus = 0x1a0238,
	.crt_spare = 0x1a0090,
	.usb2_ohci_int_mask = 0x1e000c,
	.usb2_strap = 0x1e0014,
	.ehci_hcapbase = 0x1FFE00,
	.ohci_hc_revision = 0x1FFC00,
	.bcm1_bs_lmi_steer = 0x2C0008,
	.usb2_control = 0x2c01a0,
	.usb2_stbus_obc = 0x1FFF00,
	.usb2_stbus_mess_size = 0x1FFF04,
	.usb2_stbus_chunk_size = 0x1FFF08,

	.pcie_regs = 0x200000,
	.tim_ch = 0x282C10,
	.tim_cl = 0x282C14,
	.gpio_dout = 0x282c20,
	.gpio_din = 0x282c24,
	.gpio_dir = 0x282c2C,
	.watchdog = 0x282c30,
	.front_panel = 0x283800,
};
