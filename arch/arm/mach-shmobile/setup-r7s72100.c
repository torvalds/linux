/*
 * r7s72100 processor support
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

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/sh_timer.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r7s72100.h>
#include <asm/mach/arch.h>

static struct resource mtu2_resources[] __initdata = {
	DEFINE_RES_MEM(0xfcff0000, 0x400),
	DEFINE_RES_IRQ_NAMED(gic_iid(139), "tgi0a"),
};

#define r7s72100_register_mtu2()					\
	platform_device_register_resndata(NULL, "sh-mtu2",		\
					  -1, mtu2_resources,		\
					  ARRAY_SIZE(mtu2_resources),	\
					  NULL, 0)

void __init r7s72100_add_dt_devices(void)
{
	r7s72100_register_mtu2();
}

void __init r7s72100_init_early(void)
{
	shmobile_setup_delay(400, 1, 3); /* Cortex-A9 @ 400MHz */
}

#ifdef CONFIG_USE_OF
static const char *r7s72100_boards_compat_dt[] __initdata = {
	"renesas,r7s72100",
	NULL,
};

DT_MACHINE_START(R7S72100_DT, "Generic R7S72100 (Flattened Device Tree)")
	.init_early	= r7s72100_init_early,
	.dt_compat	= r7s72100_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
