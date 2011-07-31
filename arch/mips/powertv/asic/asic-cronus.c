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

#include <linux/init.h>
#include <asm/mach-powertv/asic.h>

#define CRONUS_ADDR(x)	(CRONUS_IO_BASE + (x))

const struct register_map cronus_register_map __initdata = {
	.eic_slow0_strt_add = {.phys = CRONUS_ADDR(0x000000)},
	.eic_cfg_bits = {.phys = CRONUS_ADDR(0x000038)},
	.eic_ready_status = {.phys = CRONUS_ADDR(0x00004C)},

	.chipver3 = {.phys = CRONUS_ADDR(0x2A0800)},
	.chipver2 = {.phys = CRONUS_ADDR(0x2A0804)},
	.chipver1 = {.phys = CRONUS_ADDR(0x2A0808)},
	.chipver0 = {.phys = CRONUS_ADDR(0x2A080C)},

	/* The registers of IRBlaster */
	.uart1_intstat = {.phys = CRONUS_ADDR(0x2A1800)},
	.uart1_inten = {.phys = CRONUS_ADDR(0x2A1804)},
	.uart1_config1 = {.phys = CRONUS_ADDR(0x2A1808)},
	.uart1_config2 = {.phys = CRONUS_ADDR(0x2A180C)},
	.uart1_divisorhi = {.phys = CRONUS_ADDR(0x2A1810)},
	.uart1_divisorlo = {.phys = CRONUS_ADDR(0x2A1814)},
	.uart1_data = {.phys = CRONUS_ADDR(0x2A1818)},
	.uart1_status = {.phys = CRONUS_ADDR(0x2A181C)},

	.int_stat_3 = {.phys = CRONUS_ADDR(0x2A2800)},
	.int_stat_2 = {.phys = CRONUS_ADDR(0x2A2804)},
	.int_stat_1 = {.phys = CRONUS_ADDR(0x2A2808)},
	.int_stat_0 = {.phys = CRONUS_ADDR(0x2A280C)},
	.int_config = {.phys = CRONUS_ADDR(0x2A2810)},
	.int_int_scan = {.phys = CRONUS_ADDR(0x2A2818)},
	.ien_int_3 = {.phys = CRONUS_ADDR(0x2A2830)},
	.ien_int_2 = {.phys = CRONUS_ADDR(0x2A2834)},
	.ien_int_1 = {.phys = CRONUS_ADDR(0x2A2838)},
	.ien_int_0 = {.phys = CRONUS_ADDR(0x2A283C)},
	.int_level_3_3 = {.phys = CRONUS_ADDR(0x2A2880)},
	.int_level_3_2 = {.phys = CRONUS_ADDR(0x2A2884)},
	.int_level_3_1 = {.phys = CRONUS_ADDR(0x2A2888)},
	.int_level_3_0 = {.phys = CRONUS_ADDR(0x2A288C)},
	.int_level_2_3 = {.phys = CRONUS_ADDR(0x2A2890)},
	.int_level_2_2 = {.phys = CRONUS_ADDR(0x2A2894)},
	.int_level_2_1 = {.phys = CRONUS_ADDR(0x2A2898)},
	.int_level_2_0 = {.phys = CRONUS_ADDR(0x2A289C)},
	.int_level_1_3 = {.phys = CRONUS_ADDR(0x2A28A0)},
	.int_level_1_2 = {.phys = CRONUS_ADDR(0x2A28A4)},
	.int_level_1_1 = {.phys = CRONUS_ADDR(0x2A28A8)},
	.int_level_1_0 = {.phys = CRONUS_ADDR(0x2A28AC)},
	.int_level_0_3 = {.phys = CRONUS_ADDR(0x2A28B0)},
	.int_level_0_2 = {.phys = CRONUS_ADDR(0x2A28B4)},
	.int_level_0_1 = {.phys = CRONUS_ADDR(0x2A28B8)},
	.int_level_0_0 = {.phys = CRONUS_ADDR(0x2A28BC)},
	.int_docsis_en = {.phys = CRONUS_ADDR(0x2A28F4)},

	.mips_pll_setup = {.phys = CRONUS_ADDR(0x1C0000)},
	.fs432x4b4_usb_ctl = {.phys = CRONUS_ADDR(0x1C0028)},
	.test_bus = {.phys = CRONUS_ADDR(0x1C00CC)},
	.crt_spare = {.phys = CRONUS_ADDR(0x1c00d4)},
	.usb2_ohci_int_mask = {.phys = CRONUS_ADDR(0x20000C)},
	.usb2_strap = {.phys = CRONUS_ADDR(0x200014)},
	.ehci_hcapbase = {.phys = CRONUS_ADDR(0x21FE00)},
	.ohci_hc_revision = {.phys = CRONUS_ADDR(0x21fc00)},
	.bcm1_bs_lmi_steer = {.phys = CRONUS_ADDR(0x2E0008)},
	.usb2_control = {.phys = CRONUS_ADDR(0x2E004C)},
	.usb2_stbus_obc = {.phys = CRONUS_ADDR(0x21FF00)},
	.usb2_stbus_mess_size = {.phys = CRONUS_ADDR(0x21FF04)},
	.usb2_stbus_chunk_size = {.phys = CRONUS_ADDR(0x21FF08)},

	.pcie_regs = {.phys = CRONUS_ADDR(0x220000)},
	.tim_ch = {.phys = CRONUS_ADDR(0x2A2C10)},
	.tim_cl = {.phys = CRONUS_ADDR(0x2A2C14)},
	.gpio_dout = {.phys = CRONUS_ADDR(0x2A2C20)},
	.gpio_din = {.phys = CRONUS_ADDR(0x2A2C24)},
	.gpio_dir = {.phys = CRONUS_ADDR(0x2A2C2C)},
	.watchdog = {.phys = CRONUS_ADDR(0x2A2C30)},
	.front_panel = {.phys = CRONUS_ADDR(0x2A3800)},
};
