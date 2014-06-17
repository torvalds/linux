/*
 * Genmai board support
 *
 * Copyright (C) 2013-2014  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 * Copyright (C) 2014  Cogent Embedded, Inc.
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
#include <linux/platform_device.h>
#include <linux/sh_eth.h>
#include <linux/spi/rspi.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r7s72100.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* Ether */
static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x00, /* PD60610 */
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.phy_interface		= PHY_INTERFACE_MODE_MII,
	.no_ether_link		= 1
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xe8203000, 0x800),
	DEFINE_RES_MEM(0xe8204800, 0x200),
	DEFINE_RES_IRQ(gic_iid(359)),
};

static const struct platform_device_info ether_info __initconst = {
	.parent		= &platform_bus,
	.name		= "r7s72100-ether",
	.id		= -1,
	.res		= ether_resources,
	.num_res	= ARRAY_SIZE(ether_resources),
	.data		= &ether_pdata,
	.size_data	= sizeof(ether_pdata),
	.dma_mask	= DMA_BIT_MASK(32),
};

/* RSPI */
#define RSPI_RESOURCE(idx, baseaddr, irq)				\
static const struct resource rspi##idx##_resources[] __initconst = {	\
	DEFINE_RES_MEM(baseaddr, 0x24),					\
	DEFINE_RES_IRQ_NAMED(irq, "error"),				\
	DEFINE_RES_IRQ_NAMED(irq + 1, "rx"),				\
	DEFINE_RES_IRQ_NAMED(irq + 2, "tx"),				\
}

RSPI_RESOURCE(0, 0xe800c800, gic_iid(270));
RSPI_RESOURCE(1, 0xe800d000, gic_iid(273));
RSPI_RESOURCE(2, 0xe800d800, gic_iid(276));
RSPI_RESOURCE(3, 0xe800e000, gic_iid(279));
RSPI_RESOURCE(4, 0xe800e800, gic_iid(282));

static const struct rspi_plat_data rspi_pdata __initconst = {
	.num_chipselect	= 1,
};

#define r7s72100_register_rspi(idx)					   \
	platform_device_register_resndata(&platform_bus, "rspi-rz", idx,   \
					rspi##idx##_resources,		   \
					ARRAY_SIZE(rspi##idx##_resources), \
					&rspi_pdata, sizeof(rspi_pdata))

static const struct spi_board_info spi_info[] __initconst = {
	{
		.modalias               = "wm8978",
		.max_speed_hz           = 5000000,
		.bus_num                = 4,
		.chip_select            = 0,
	},
};

static void __init genmai_add_standard_devices(void)
{
	r7s72100_clock_init();
	r7s72100_add_dt_devices();

	platform_device_register_full(&ether_info);

	r7s72100_register_rspi(0);
	r7s72100_register_rspi(1);
	r7s72100_register_rspi(2);
	r7s72100_register_rspi(3);
	r7s72100_register_rspi(4);
	spi_register_board_info(spi_info, ARRAY_SIZE(spi_info));
}

static const char * const genmai_boards_compat_dt[] __initconst = {
	"renesas,genmai",
	NULL,
};

DT_MACHINE_START(GENMAI_DT, "genmai")
	.init_early	= r7s72100_init_early,
	.init_machine	= genmai_add_standard_devices,
	.dt_compat	= genmai_boards_compat_dt,
MACHINE_END
