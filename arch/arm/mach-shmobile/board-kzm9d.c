/*
 * kzm9d board support
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Magnus Damm
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

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

MACHINE_START(KZM9D, "kzm9d")
	.init_early	= emev2_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= emev2_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= emev2_add_standard_devices,
	.timer		= &shmobile_timer,
MACHINE_END
