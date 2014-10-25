/*
 * Lager board support - Reference DT implementation
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Simon Horman
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

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/platform_data/rcar-du.h>

#include <asm/mach/arch.h>

#include "clock.h"
#include "common.h"
#include "irqs.h"
#include "r8a7790.h"
#include "rcar-gen2.h"

/* DU */
static struct rcar_du_encoder_data lager_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_VGA,
		.output = RCAR_DU_OUTPUT_DPAD0,
	}, {
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS1,
		.connector.lvds.panel = {
			.width_mm = 210,
			.height_mm = 158,
			.mode = {
				.pixelclock = 65000000,
				.hactive = 1024,
				.hfront_porch = 20,
				.hback_porch = 160,
				.hsync_len = 136,
				.vactive = 768,
				.vfront_porch = 3,
				.vback_porch = 29,
				.vsync_len = 6,
			},
		},
	},
};

static struct rcar_du_platform_data lager_du_pdata = {
	.encoders = lager_du_encoders,
	.num_encoders = ARRAY_SIZE(lager_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x70000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_MEM_NAMED(0xfeb94000, 0x1c, "lvds.1"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
	DEFINE_RES_IRQ(gic_spi(269)),
};

static void __init lager_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7790",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &lager_du_pdata,
		.size_data = sizeof(lager_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/*
 * This is a really crude hack to provide clkdev support to platform
 * devices until they get moved to DT.
 */
static const struct clk_name clk_names[] __initconst = {
	{ "du0", "du.0", "rcar-du-r8a7790" },
	{ "du1", "du.1", "rcar-du-r8a7790" },
	{ "du2", "du.2", "rcar-du-r8a7790" },
	{ "lvds0", "lvds.0", "rcar-du-r8a7790" },
	{ "lvds1", "lvds.1", "rcar-du-r8a7790" },
};

static void __init lager_add_standard_devices(void)
{
	shmobile_clk_workaround(clk_names, ARRAY_SIZE(clk_names), false);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	lager_add_du_device();
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager",
	"renesas,lager-reference",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= shmobile_init_delay,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= lager_add_standard_devices,
	.init_late	= shmobile_init_late,
	.reserve	= rcar_gen2_reserve,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
