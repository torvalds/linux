/*
 * Copyright 2011, Netlogic Microsystems.
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/resource.h>
#include <linux/spi/flash.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/flash.h>
#include <asm/netlogic/xlr/bridge.h>
#include <asm/netlogic/xlr/gpio.h>
#include <asm/netlogic/xlr/xlr.h>

/*
 * Default NOR partition layout
 */
static struct mtd_partition xlr_nor_parts[] = {
	{
		.name = "User FS",
		.offset = 0x800000,
		.size	= MTDPART_SIZ_FULL,
	}
};

/*
 * Default NAND partition layout
 */
static struct mtd_partition xlr_nand_parts[] = {
	{
		.name	= "Root Filesystem",
		.offset = 64 * 64 * 2048,
		.size	= 432 * 64 * 2048,
	},
	{
		.name	= "Home Filesystem",
		.offset = MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

/* Use PHYSMAP flash for NOR */
struct physmap_flash_data xlr_nor_data = {
	.width		= 2,
	.parts		= xlr_nor_parts,
	.nr_parts	= ARRAY_SIZE(xlr_nor_parts),
};

static struct resource xlr_nor_res[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device xlr_nor_dev = {
	.name	= "physmap-flash",
	.dev	= {
		.platform_data	= &xlr_nor_data,
	},
	.num_resources	= ARRAY_SIZE(xlr_nor_res),
	.resource	= xlr_nor_res,
};

/*
 * Use "gen_nand" driver for NAND flash
 *
 * There seems to be no way to store a private pointer containing
 * platform specific info in gen_nand drivier. We will use a global
 * struct for now, since we currently have only one NAND chip per board.
 */
struct xlr_nand_flash_priv {
	int cs;
	uint64_t flash_mmio;
};

static struct xlr_nand_flash_priv nand_priv;

static void xlr_nand_ctrl(struct mtd_info *mtd, int cmd,
		unsigned int ctrl)
{
	if (ctrl & NAND_CLE)
		nlm_write_reg(nand_priv.flash_mmio,
			FLASH_NAND_CLE(nand_priv.cs), cmd);
	else if (ctrl & NAND_ALE)
		nlm_write_reg(nand_priv.flash_mmio,
			FLASH_NAND_ALE(nand_priv.cs), cmd);
}

struct platform_nand_data xlr_nand_data = {
	.chip = {
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(xlr_nand_parts),
		.chip_delay	= 50,
		.partitions	= xlr_nand_parts,
	},
	.ctrl = {
		.cmd_ctrl	= xlr_nand_ctrl,
	},
};

static struct resource xlr_nand_res[] = {
	{
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device xlr_nand_dev = {
	.name		= "gen_nand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(xlr_nand_res),
	.resource	= xlr_nand_res,
	.dev		= {
		.platform_data	= &xlr_nand_data,
	}
};

/*
 * XLR/XLS supports upto 8 devices on its FLASH interface. The value in
 * FLASH_BAR (on the MEM/IO bridge) gives the base for mapping all the
 * flash devices.
 * Under this, each flash device has an offset and size given by the
 * CSBASE_ADDR and CSBASE_MASK registers for the device.
 *
 * The CSBASE_ registers are expected to be setup by the bootloader.
 */
static void setup_flash_resource(uint64_t flash_mmio,
	uint64_t flash_map_base, int cs, struct resource *res)
{
	u32 base, mask;

	base = nlm_read_reg(flash_mmio, FLASH_CSBASE_ADDR(cs));
	mask = nlm_read_reg(flash_mmio, FLASH_CSADDR_MASK(cs));

	res->start = flash_map_base + ((unsigned long)base << 16);
	res->end = res->start + (mask + 1) * 64 * 1024;
}

static int __init xlr_flash_init(void)
{
	uint64_t gpio_mmio, flash_mmio, flash_map_base;
	u32 gpio_resetcfg, flash_bar;
	int cs, boot_nand, boot_nor;

	/* Flash address bits 39:24 is in bridge flash BAR */
	flash_bar = nlm_read_reg(nlm_io_base, BRIDGE_FLASH_BAR);
	flash_map_base = (flash_bar & 0xffff0000) << 8;

	gpio_mmio = nlm_mmio_base(NETLOGIC_IO_GPIO_OFFSET);
	flash_mmio = nlm_mmio_base(NETLOGIC_IO_FLASH_OFFSET);

	/* Get the chip reset config */
	gpio_resetcfg = nlm_read_reg(gpio_mmio, GPIO_PWRON_RESET_CFG_REG);

	/* Check for boot flash type */
	boot_nor = boot_nand = 0;
	if (nlm_chip_is_xls()) {
		/* On XLS, check boot from NAND bit (GPIO reset reg bit 16) */
		if (gpio_resetcfg & (1 << 16))
			boot_nand = 1;

		/* check boot from PCMCIA, (GPIO reset reg bit 15 */
		if ((gpio_resetcfg & (1 << 15)) == 0)
			boot_nor = 1;	/* not set, booted from NOR */
	} else { /* XLR */
		/* check boot from PCMCIA (bit 16 in GPIO reset on XLR) */
		if ((gpio_resetcfg & (1 << 16)) == 0)
			boot_nor = 1;	/* not set, booted from NOR */
	}

	/* boot flash at chip select 0 */
	cs = 0;

	if (boot_nand) {
		nand_priv.cs = cs;
		nand_priv.flash_mmio = flash_mmio;
		setup_flash_resource(flash_mmio, flash_map_base, cs,
			 xlr_nand_res);

		/* Initialize NAND flash at CS 0 */
		nlm_write_reg(flash_mmio, FLASH_CSDEV_PARM(cs),
				FLASH_NAND_CSDEV_PARAM);
		nlm_write_reg(flash_mmio, FLASH_CSTIME_PARMA(cs),
				FLASH_NAND_CSTIME_PARAMA);
		nlm_write_reg(flash_mmio, FLASH_CSTIME_PARMB(cs),
				FLASH_NAND_CSTIME_PARAMB);

		pr_info("ChipSelect %d: NAND Flash %pR\n", cs, xlr_nand_res);
		return platform_device_register(&xlr_nand_dev);
	}

	if (boot_nor) {
		setup_flash_resource(flash_mmio, flash_map_base, cs,
			xlr_nor_res);
		pr_info("ChipSelect %d: NOR Flash %pR\n", cs, xlr_nor_res);
		return platform_device_register(&xlr_nor_dev);
	}
	return 0;
}

arch_initcall(xlr_flash_init);
