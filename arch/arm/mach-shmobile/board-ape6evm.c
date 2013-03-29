/*
 * APE6EVM board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/common.h>
#include <mach/r8a73a4.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static void __init ape6evm_add_standard_devices(void)
{
	r8a73a4_clock_init();
	r8a73a4_add_standard_devices();
}

static const char *ape6evm_boards_compat_dt[] __initdata = {
	"renesas,ape6evm",
	NULL,
};

DT_MACHINE_START(APE6EVM_DT, "ape6evm")
	.init_irq	= irqchip_init,
	.init_time	= shmobile_timer_init,
	.init_machine	= ape6evm_add_standard_devices,
	.dt_compat	= ape6evm_boards_compat_dt,
MACHINE_END
