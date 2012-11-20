/*
 * KZM-A9-GT board support - Reference Device Tree Implementation
 *
 * Copyright (C) 2012	Horms Solutions Ltd.
 *
 * Based on board-kzm9g.c
 * Copyright (C) 2012	Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 0),
};

static const struct pinctrl_map kzm_pinctrl_map[] = {
	PIN_MAP_MUX_GROUP_DEFAULT("e6826000.i2c", "e6050000.pfc",
				  "i2c3_1", "i2c3"),
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("e6bd0000.mmcif", "e6050000.pfc",
				  "mmc0_data8_0", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("e6bd0000.mmcif", "e6050000.pfc",
				  "mmc0_ctrl_0", "mmc0"),
	PIN_MAP_CONFIGS_PIN_DEFAULT("e6bd0000.mmcif", "e6050000.pfc",
				    "PORT279", pin_pullup_conf),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("e6bd0000.mmcif", "e6050000.pfc",
				      "mmc0_data8_0", pin_pullup_conf),
	/* SCIFA4 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "e6050000.pfc",
				  "scifa4_data", "scifa4"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "e6050000.pfc",
				  "scifa4_ctrl", "scifa4"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("ee100000.sdhi", "e6050000.pfc",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("ee100000.sdhi", "e6050000.pfc",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("ee100000.sdhi", "e6050000.pfc",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("ee100000.sdhi", "e6050000.pfc",
				  "sdhi0_wp", "sdhi0"),
	/* SDHI2 */
	PIN_MAP_MUX_GROUP_DEFAULT("ee140000.sdhi", "e6050000.pfc",
				  "sdhi2_data4", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("ee140000.sdhi", "e6050000.pfc",
				  "sdhi2_ctrl", "sdhi2"),
};

static void __init kzm_init(void)
{
	sh73a0_add_standard_devices_dt();
	pinctrl_register_mappings(kzm_pinctrl_map, ARRAY_SIZE(kzm_pinctrl_map));

	/* enable SD */
	gpio_request_one(15, GPIOF_OUT_INIT_HIGH, NULL); /* power */

	gpio_request_one(14, GPIOF_OUT_INIT_HIGH, NULL); /* power */

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x40460000, 0x82000fff);
#endif
}

static const char *kzm9g_boards_compat_dt[] __initdata = {
	"renesas,kzm9g-reference",
	NULL,
};

DT_MACHINE_START(KZM9G_DT, "kzm9g-reference")
	.smp		= smp_ops(sh73a0_smp_ops),
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_init_delay,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_machine	= kzm_init,
	.init_time	= shmobile_timer_init,
	.dt_compat	= kzm9g_boards_compat_dt,
MACHINE_END
