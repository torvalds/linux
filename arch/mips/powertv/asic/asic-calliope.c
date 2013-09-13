/*
 * Locations of devices in the Calliope ASIC.
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
 * Author:	 Ken Eppinett
 *		 David Schleef <ds@schleef.org>
 *
 * Description:	 Defines the platform resources for the SA settop.
 */

#include <linux/init.h>
#include <asm/mach-powertv/asic.h>

#define CALLIOPE_ADDR(x)	(CALLIOPE_IO_BASE + (x))

const struct register_map calliope_register_map __initconst = {
	.eic_slow0_strt_add = {.phys = CALLIOPE_ADDR(0x800000)},
	.eic_cfg_bits = {.phys = CALLIOPE_ADDR(0x800038)},
	.eic_ready_status = {.phys = CALLIOPE_ADDR(0x80004c)},

	.chipver3 = {.phys = CALLIOPE_ADDR(0xA00800)},
	.chipver2 = {.phys = CALLIOPE_ADDR(0xA00804)},
	.chipver1 = {.phys = CALLIOPE_ADDR(0xA00808)},
	.chipver0 = {.phys = CALLIOPE_ADDR(0xA0080c)},

	/* The registers of IRBlaster */
	.uart1_intstat = {.phys = CALLIOPE_ADDR(0xA01800)},
	.uart1_inten = {.phys = CALLIOPE_ADDR(0xA01804)},
	.uart1_config1 = {.phys = CALLIOPE_ADDR(0xA01808)},
	.uart1_config2 = {.phys = CALLIOPE_ADDR(0xA0180C)},
	.uart1_divisorhi = {.phys = CALLIOPE_ADDR(0xA01810)},
	.uart1_divisorlo = {.phys = CALLIOPE_ADDR(0xA01814)},
	.uart1_data = {.phys = CALLIOPE_ADDR(0xA01818)},
	.uart1_status = {.phys = CALLIOPE_ADDR(0xA0181C)},

	.int_stat_3 = {.phys = CALLIOPE_ADDR(0xA02800)},
	.int_stat_2 = {.phys = CALLIOPE_ADDR(0xA02804)},
	.int_stat_1 = {.phys = CALLIOPE_ADDR(0xA02808)},
	.int_stat_0 = {.phys = CALLIOPE_ADDR(0xA0280c)},
	.int_config = {.phys = CALLIOPE_ADDR(0xA02810)},
	.int_int_scan = {.phys = CALLIOPE_ADDR(0xA02818)},
	.ien_int_3 = {.phys = CALLIOPE_ADDR(0xA02830)},
	.ien_int_2 = {.phys = CALLIOPE_ADDR(0xA02834)},
	.ien_int_1 = {.phys = CALLIOPE_ADDR(0xA02838)},
	.ien_int_0 = {.phys = CALLIOPE_ADDR(0xA0283c)},
	.int_level_3_3 = {.phys = CALLIOPE_ADDR(0xA02880)},
	.int_level_3_2 = {.phys = CALLIOPE_ADDR(0xA02884)},
	.int_level_3_1 = {.phys = CALLIOPE_ADDR(0xA02888)},
	.int_level_3_0 = {.phys = CALLIOPE_ADDR(0xA0288c)},
	.int_level_2_3 = {.phys = CALLIOPE_ADDR(0xA02890)},
	.int_level_2_2 = {.phys = CALLIOPE_ADDR(0xA02894)},
	.int_level_2_1 = {.phys = CALLIOPE_ADDR(0xA02898)},
	.int_level_2_0 = {.phys = CALLIOPE_ADDR(0xA0289c)},
	.int_level_1_3 = {.phys = CALLIOPE_ADDR(0xA028a0)},
	.int_level_1_2 = {.phys = CALLIOPE_ADDR(0xA028a4)},
	.int_level_1_1 = {.phys = CALLIOPE_ADDR(0xA028a8)},
	.int_level_1_0 = {.phys = CALLIOPE_ADDR(0xA028ac)},
	.int_level_0_3 = {.phys = CALLIOPE_ADDR(0xA028b0)},
	.int_level_0_2 = {.phys = CALLIOPE_ADDR(0xA028b4)},
	.int_level_0_1 = {.phys = CALLIOPE_ADDR(0xA028b8)},
	.int_level_0_0 = {.phys = CALLIOPE_ADDR(0xA028bc)},
	.int_docsis_en = {.phys = CALLIOPE_ADDR(0xA028F4)},

	.mips_pll_setup = {.phys = CALLIOPE_ADDR(0x980000)},
	.fs432x4b4_usb_ctl = {.phys = CALLIOPE_ADDR(0x980030)},
	.test_bus = {.phys = CALLIOPE_ADDR(0x9800CC)},
	.crt_spare = {.phys = CALLIOPE_ADDR(0x9800d4)},
	.usb2_ohci_int_mask = {.phys = CALLIOPE_ADDR(0x9A000c)},
	.usb2_strap = {.phys = CALLIOPE_ADDR(0x9A0014)},
	.ehci_hcapbase = {.phys = CALLIOPE_ADDR(0x9BFE00)},
	.ohci_hc_revision = {.phys = CALLIOPE_ADDR(0x9BFC00)},
	.bcm1_bs_lmi_steer = {.phys = CALLIOPE_ADDR(0x9E0004)},
	.usb2_control = {.phys = CALLIOPE_ADDR(0x9E0054)},
	.usb2_stbus_obc = {.phys = CALLIOPE_ADDR(0x9BFF00)},
	.usb2_stbus_mess_size = {.phys = CALLIOPE_ADDR(0x9BFF04)},
	.usb2_stbus_chunk_size = {.phys = CALLIOPE_ADDR(0x9BFF08)},

	.pcie_regs = {.phys = 0x000000},	/* -doesn't exist- */
	.tim_ch = {.phys = CALLIOPE_ADDR(0xA02C10)},
	.tim_cl = {.phys = CALLIOPE_ADDR(0xA02C14)},
	.gpio_dout = {.phys = CALLIOPE_ADDR(0xA02c20)},
	.gpio_din = {.phys = CALLIOPE_ADDR(0xA02c24)},
	.gpio_dir = {.phys = CALLIOPE_ADDR(0xA02c2C)},
	.watchdog = {.phys = CALLIOPE_ADDR(0xA02c30)},
	.front_panel = {.phys = 0x000000},	/* -not used- */
};
