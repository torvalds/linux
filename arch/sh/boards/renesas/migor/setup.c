/*
 * Renesas System Solutions Asia Pte. Ltd - Migo-R
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/i2c.h>
#include <linux/smc91x.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/sh_keysc.h>
#include <asm/migor.h>

/* Address     IRQ  Size  Bus  Description
 * 0x00000000       64MB  16   NOR Flash (SP29PL256N)
 * 0x0c000000       64MB  64   SDRAM (2xK4M563233G)
 * 0x10000000  IRQ0       16   Ethernet (SMC91C111)
 * 0x14000000  IRQ4       16   USB 2.0 Host Controller (M66596)
 * 0x18000000       8GB    8   NAND Flash (K9K8G08U0A)
 */

static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT,
	.irq_flags = IRQF_TRIGGER_HIGH,
};

static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "SMC91C111" ,
		.start  = 0x10000300,
		.end    = 0x1000030f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 32, /* IRQ0 */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_eth_device = {
	.name           = "smc91x",
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

static struct sh_keysc_info sh_keysc_info = {
	.mode = SH_KEYSC_MODE_2, /* KEYOUT0->4, KEYIN1->5 */
	.scan_timing = 3,
	.delay = 5,
	.keycodes = {
		0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ENTER,
		0, KEY_F, KEY_C, KEY_D,	KEY_H, KEY_1,
		0, KEY_2, KEY_3, KEY_4,	KEY_5, KEY_6,
		0, KEY_7, KEY_8, KEY_9, KEY_S, KEY_0,
		0, KEY_P, KEY_STOP, KEY_REWIND, KEY_PLAY, KEY_FASTFORWARD,
	},
};

static struct resource sh_keysc_resources[] = {
	[0] = {
		.start  = 0x044b0000,
		.end    = 0x044b000f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 79,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sh_keysc_device = {
	.name           = "sh_keysc",
	.num_resources  = ARRAY_SIZE(sh_keysc_resources),
	.resource       = sh_keysc_resources,
	.dev	= {
		.platform_data	= &sh_keysc_info,
	},
};

static struct mtd_partition migor_nor_flash_partitions[] =
{
	{
		.name = "uboot",
		.offset = 0,
		.size = (1 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	},
	{
		.name = "rootfs",
		.offset = MTDPART_OFS_APPEND,
		.size = (15 * 1024 * 1024),
	},
	{
		.name = "other",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data migor_nor_flash_data = {
	.width		= 2,
	.parts		= migor_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(migor_nor_flash_partitions),
};

static struct resource migor_nor_flash_resources[] = {
	[0] = {
		.name		= "NOR Flash",
		.start		= 0x00000000,
		.end		= 0x03ffffff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct platform_device migor_nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= migor_nor_flash_resources,
	.num_resources	= ARRAY_SIZE(migor_nor_flash_resources),
	.dev		= {
		.platform_data = &migor_nor_flash_data,
	},
};

static struct mtd_partition migor_nand_flash_partitions[] = {
	{
		.name		= "nanddata1",
		.offset		= 0x0,
		.size		= 512 * 1024 * 1024,
	},
	{
		.name		= "nanddata2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024 * 1024,
	},
};

static void migor_nand_flash_cmd_ctl(struct mtd_info *mtd, int cmd,
				     unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, chip->IO_ADDR_W + 0x00400000);
	else if (ctrl & NAND_ALE)
		writeb(cmd, chip->IO_ADDR_W + 0x00800000);
	else
		writeb(cmd, chip->IO_ADDR_W);
}

static int migor_nand_flash_ready(struct mtd_info *mtd)
{
	return ctrl_inb(PORT_PADR) & 0x02; /* PTA1 */
}

struct platform_nand_data migor_nand_flash_data = {
	.chip = {
		.nr_chips = 1,
		.partitions = migor_nand_flash_partitions,
		.nr_partitions = ARRAY_SIZE(migor_nand_flash_partitions),
		.chip_delay = 20,
		.part_probe_types = (const char *[]) { "cmdlinepart", NULL },
	},
	.ctrl = {
		.dev_ready = migor_nand_flash_ready,
		.cmd_ctrl = migor_nand_flash_cmd_ctl,
	},
};

static struct resource migor_nand_flash_resources[] = {
	[0] = {
		.name		= "NAND Flash",
		.start		= 0x18000000,
		.end		= 0x18ffffff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device migor_nand_flash_device = {
	.name		= "gen_nand",
	.resource	= migor_nand_flash_resources,
	.num_resources	= ARRAY_SIZE(migor_nand_flash_resources),
	.dev		= {
		.platform_data = &migor_nand_flash_data,
	}
};

static struct platform_device *migor_devices[] __initdata = {
	&smc91x_eth_device,
	&sh_keysc_device,
	&migor_nor_flash_device,
	&migor_nand_flash_device,
};

static struct i2c_board_info __initdata migor_i2c_devices[] = {
	{
		I2C_BOARD_INFO("rs5c372b", 0x32),
	},
	{
		I2C_BOARD_INFO("migor_ts", 0x51),
		.irq = 38, /* IRQ6 */
	},
};

static int __init migor_devices_setup(void)
{
	i2c_register_board_info(0, migor_i2c_devices,
				ARRAY_SIZE(migor_i2c_devices));
 
	return platform_add_devices(migor_devices, ARRAY_SIZE(migor_devices));
}
__initcall(migor_devices_setup);

static void __init migor_setup(char **cmdline_p)
{
	/* SMC91C111 - Enable IRQ0 */
	ctrl_outw(ctrl_inw(PORT_PJCR) & ~0x0003, PORT_PJCR);

	/* KEYSC */
	ctrl_outw(ctrl_inw(PORT_PYCR) & ~0x0fff, PORT_PYCR);
	ctrl_outw(ctrl_inw(PORT_PZCR) & ~0x0ff0, PORT_PZCR);
	ctrl_outw(ctrl_inw(PORT_PSELA) & ~0x4100, PORT_PSELA);
	ctrl_outw(ctrl_inw(PORT_HIZCRA) & ~0x4000, PORT_HIZCRA);
	ctrl_outw(ctrl_inw(PORT_HIZCRC) & ~0xc000, PORT_HIZCRC);
	ctrl_outl(ctrl_inl(MSTPCR2) & ~0x00004000, MSTPCR2);

	/* NAND Flash */
	ctrl_outw(ctrl_inw(PORT_PXCR) & 0x0fff, PORT_PXCR);
	ctrl_outl((ctrl_inl(BSC_CS6ABCR) & ~0x00000600) | 0x00000200,
		  BSC_CS6ABCR);

	/* I2C */
	ctrl_outl(ctrl_inl(MSTPCR1) & ~0x00000200, MSTPCR1);

	/* Touch Panel - Enable IRQ6 */
	ctrl_outw(ctrl_inw(PORT_PZCR) & ~0xc, PORT_PZCR);
	ctrl_outw((ctrl_inw(PORT_PSELA) | 0x8000), PORT_PSELA);
	ctrl_outw((ctrl_inw(PORT_HIZCRC) & ~0x4000), PORT_HIZCRC);
}

static struct sh_machine_vector mv_migor __initmv = {
	.mv_name		= "Migo-R",
	.mv_setup		= migor_setup,
};
