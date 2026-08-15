/*
 * stmark2.c -- Support for Kernelspace AMCORE open board
 *
 * (C) Copyright 2026, Angelo Dureghello <angelo@kernel-space.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/ioport.h>
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
	DEFINE_RES_MEM(MCFDSPI_BASE0, 0x100),
	DEFINE_RES_IRQ(MCF_IRQ_DSPI0),
	DEFINE_RES_DMA(12),
	DEFINE_RES_DMA(13),
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

static struct resource dac0_resource = DEFINE_RES_MEM(MCFDAC_BASE0, 0x100);

static struct platform_device dac0_device = {
	.name = "mcfdac",
	.id = 0,
	.num_resources = 1,
	.resource = &dac0_resource,
};

static struct resource dac1_resource = DEFINE_RES_MEM(MCFDAC_BASE1, 0x100);

static struct platform_device dac1_device = {
	.name = "mcfdac",
	.id = 1,
	.num_resources = 1,
	.resource = &dac1_resource,
};

static struct platform_device *stmark2_devices[] __initdata = {
	&dspi_spi0_device,
	&dac0_device,
	&dac1_device,
};

/*
 * Note: proper pin-mux setup is mandatory for proper SPI functionality.
 */
static int __init init_stmark2(void)
{
	u16 val;

	/* DSPI0, all pins as DSPI, and using CS1 */
	mcf_write8(0x80, MCFGPIO_PAR_DSPIOWL);
	mcf_write8(0xfc, MCFGPIO_PAR_DSPIOWH);

	/* Board gpio setup */
	mcf_write8(0x00, MCFGPIO_PAR_BE);
	mcf_write8(0x00, MCFGPIO_PAR_FBCTL);
	mcf_write8(0x00, MCFGPIO_PAR_CS);

	/* CAN pads */
	mcf_write8(0x50, MCFGPIO_PAR_CANI2C);

	val = mcf_read16(MCF_CCM_MISCCR2);
	val &= ~(MCF_CCM_MISCCR2_ADC3_EN | MCF_CCM_MISCCR2_ADC7_EN);
	val |= MCF_CCM_MISCCR2_DAC0_SEL | MCF_CCM_MISCCR2_DAC1_SEL;
	mcf_write16(val, MCF_CCM_MISCCR2);

	platform_add_devices(stmark2_devices, ARRAY_SIZE(stmark2_devices));

	spi_register_board_info(stmark2_board_info,
				ARRAY_SIZE(stmark2_board_info));

	return 0;
}

device_initcall(init_stmark2);
