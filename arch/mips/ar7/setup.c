/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/time.h>

#include <asm/reboot.h>
#include <asm/mach-ar7/ar7.h>
#include <asm/mach-ar7/prom.h>
#include <asm/mach-ar7/gpio.h>

static void ar7_machine_restart(char *command)
{
	u32 *softres_reg = ioremap(AR7_REGS_RESET + AR7_RESET_SOFTWARE, 1);

	writel(1, softres_reg);
}

static void ar7_machine_halt(void)
{
	while (1)
		;
}

static void ar7_machine_power_off(void)
{
	u32 *power_reg = (u32 *)ioremap(AR7_REGS_POWER, 1);
	u32 power_state = readl(power_reg) | (3 << 30);

	writel(power_state, power_reg);
	ar7_machine_halt();
}

const char *get_system_type(void)
{
	u16 chip_id = ar7_chip_id();
	u16 titan_variant_id = titan_chip_id();

	switch (chip_id) {
	case AR7_CHIP_7100:
		return "TI AR7 (TNETD7100)";
	case AR7_CHIP_7200:
		return "TI AR7 (TNETD7200)";
	case AR7_CHIP_7300:
		return "TI AR7 (TNETD7300)";
	case AR7_CHIP_TITAN:
		switch (titan_variant_id) {
		case TITAN_CHIP_1050:
			return "TI AR7 (TNETV1050)";
		case TITAN_CHIP_1055:
			return "TI AR7 (TNETV1055)";
		case TITAN_CHIP_1056:
			return "TI AR7 (TNETV1056)";
		case TITAN_CHIP_1060:
			return "TI AR7 (TNETV1060)";
		}
	default:
		return "TI AR7 (unknown)";
	}
}

static int __init ar7_init_console(void)
{
	return 0;
}
console_initcall(ar7_init_console);

/*
 * Initializes basic routines and structures pointers, memory size (as
 * given by the bios and saves the command line.
 */
void __init plat_mem_setup(void)
{
	unsigned long io_base;

	_machine_restart = ar7_machine_restart;
	_machine_halt = ar7_machine_halt;
	pm_power_off = ar7_machine_power_off;

	io_base = (unsigned long)ioremap(AR7_REGS_BASE, 0x10000);
	if (!io_base)
		panic("Can't remap IO base!");
	set_io_port_base(io_base);

	prom_meminit();

	printk(KERN_INFO "%s, ID: 0x%04x, Revision: 0x%02x\n",
			get_system_type(), ar7_chip_id(), ar7_chip_rev());
}
