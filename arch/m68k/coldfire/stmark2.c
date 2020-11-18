/*
 * stmark2.c -- Support for Sysam AMCORE open board
 *
 * (C) Copyright 2017, Angelo Dureghello <angelo@sysam.it>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-fsl-dspi.h>
#include <linux/spi/flash.h>
#include <linux/dma-mapping.h>
#include <asm/mcfsim.h>

/*
 * Partitioning of parallel NOR flash (39VF3201B)
 */
static struct mtd_partition stmark2_partitions[] = {
	{
		.name = "U-Boot (1024K)",
		.size = 0x100000,
		.offset = 0x0
	}, {
		.name = "Kernel+initramfs (7168K)",
		.size = 0x700000,
		.offset = MTDPART_OFS_APPEND
	}, {
		.name = "Flash Free Space (8192K)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND
	}
};

static struct flash_platform_data stmark2_spi_flash_data = {
	.name = "is25lp128",
	.parts = stmark2_partitions,
	.nr_parts = ARRAY_SIZE(stmark2_partitions),
	.type = "is25lp128",
};

static struct spi_board_info stmark2_board_info[] __initdata = {
	{
		.modalias = "m25p80",
		.max_speed_hz = 5000000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = &stmark2_spi_flash_data,
		.mode = SPI_MODE_3,
	}
};

/* SPI controller data, SPI (0) */
static struct fsl_dspi_platform_data dspi_spi0_info = {
	.cs_num = 4,
	.bus_num = 0,
	.sck_cs_delay = 100,
	.cs_sck_delay = 100,
};

static struct resource dspi_spi0_resource[] = {
	[0] = {
		.start = MCFDSPI_BASE0,
		.end   = MCFDSPI_BASE0 + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = 12,
		.end   = 13,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = MCF_IRQ_DSPI0,
		.end   = MCF_IRQ_DSPI0,
		.flags = IORESOURCE_IRQ,
	},
};

static u64 stmark2_dspi_mask = DMA_BIT_MASK(32);

/* SPI controller, id = bus number */
static struct platform_device dspi_spi0_device = {
	.name = "fsl-dspi",
	.id = 0,
	.num_resources = ARRAY_SIZE(dspi_spi0_resource),
	.resource = dspi_spi0_resource,
	.dev = {
		.platform_data = &dspi_spi0_info,
		.dma_mask = &stmark2_dspi_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device *stmark2_devices[] __initdata = {
	&dspi_spi0_device,
};

/*
 * Note: proper pin-mux setup is mandatory for proper SPI functionality.
 */
static int __init init_stmark2(void)
{
	/* DSPI0, all pins as DSPI, and using CS1 */
	__raw_writeb(0x80, MCFGPIO_PAR_DSPIOWL);
	__raw_writeb(0xfc, MCFGPIO_PAR_DSPIOWH);

	/* Board gpio setup */
	__raw_writeb(0x00, MCFGPIO_PAR_BE);
	__raw_writeb(0x00, MCFGPIO_PAR_FBCTL);
	__raw_writeb(0x00, MCFGPIO_PAR_CS);
	__raw_writeb(0x00, MCFGPIO_PAR_CANI2C);

	platform_add_devices(stmark2_devices, ARRAY_SIZE(stmark2_devices));

	spi_register_board_info(stmark2_board_info,
				ARRAY_SIZE(stmark2_board_info));

	return 0;
}

late_initcall(init_stmark2);
