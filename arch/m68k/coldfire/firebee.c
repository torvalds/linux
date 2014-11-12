/***************************************************************************/

/*
 *	firebee.c -- extra startup code support for the FireBee boards
 *
 *	Copyright (C) 2011, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/***************************************************************************/

/*
 *	8MB of NOR flash fitted to the FireBee board.
 */
#define	FLASH_PHYS_ADDR		0xe0000000	/* Physical address of flash */
#define	FLASH_PHYS_SIZE		0x00800000	/* Size of flash */

#define	PART_BOOT_START		0x00000000	/* Start at bottom of flash */
#define	PART_BOOT_SIZE		0x00040000	/* 256k in size */
#define	PART_IMAGE_START	0x00040000	/* Start after boot loader */
#define	PART_IMAGE_SIZE		0x006c0000	/* Most of flash */
#define	PART_FPGA_START		0x00700000	/* Start at offset 7MB */
#define	PART_FPGA_SIZE		0x00100000	/* 1MB in size */

static struct mtd_partition firebee_flash_parts[] = {
	{
		.name	= "dBUG",
		.offset	= PART_BOOT_START,
		.size	= PART_BOOT_SIZE,
	},
	{
		.name	= "FPGA",
		.offset	= PART_FPGA_START,
		.size	= PART_FPGA_SIZE,
	},
	{
		.name	= "image",
		.offset	= PART_IMAGE_START,
		.size	= PART_IMAGE_SIZE,
	},
};

static struct physmap_flash_data firebee_flash_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(firebee_flash_parts),
	.parts		= firebee_flash_parts,
};

static struct resource firebee_flash_resource = {
	.start		= FLASH_PHYS_ADDR,
	.end		= FLASH_PHYS_ADDR + FLASH_PHYS_SIZE,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device firebee_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data = &firebee_flash_data,
	},
	.num_resources	= 1,
	.resource	= &firebee_flash_resource,
};

/***************************************************************************/

static int __init init_firebee(void)
{
	platform_device_register(&firebee_flash);
	return 0;
}

arch_initcall(init_firebee);

/***************************************************************************/
