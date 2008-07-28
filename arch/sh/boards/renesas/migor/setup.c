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
#include <asm/clock.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/sh_keysc.h>
#include <asm/sh_mobile_lcdc.h>
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
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
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

static struct sh_mobile_lcdc_info sh_mobile_lcdc_info = {
#ifdef CONFIG_SH_MIGOR_RTA_WVGA
	.clock_source = LCDC_CLK_BUS,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.interface_type = RGB16,
		.clock_divider = 2,
		.lcd_cfg = {
			.name = "LB070WV1",
			.xres = 800,
			.yres = 480,
			.left_margin = 64,
			.right_margin = 16,
			.hsync_len = 120,
			.upper_margin = 1,
			.lower_margin = 17,
			.vsync_len = 2,
			.sync = 0,
		},
	}
#endif
#ifdef CONFIG_SH_MIGOR_QVGA
	.clock_source = LCDC_CLK_PERIPHERAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.interface_type = SYS16A,
		.clock_divider = 10,
		.lcd_cfg = {
			.name = "PH240320T",
			.xres = 320,
			.yres = 240,
			.left_margin = 0,
			.right_margin = 16,
			.hsync_len = 8,
			.upper_margin = 1,
			.lower_margin = 17,
			.vsync_len = 2,
			.sync = FB_SYNC_HOR_HIGH_ACT,
		},
		.board_cfg = {
			.setup_sys = migor_lcd_qvga_setup,
		},
		.sys_bus_cfg = {
			.ldmt2r = 0x06000a09,
			.ldmt3r = 0x180e3418,
		},
	}
#endif
};

static struct resource migor_lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000, /* P4-only space */
		.end	= 0xfe941fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device migor_lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(migor_lcdc_resources),
	.resource	= migor_lcdc_resources,
	.dev	= {
		.platform_data	= &sh_mobile_lcdc_info,
	},
};

static struct platform_device *migor_devices[] __initdata = {
	&smc91x_eth_device,
	&sh_keysc_device,
	&migor_lcdc_device,
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
	clk_always_enable("mstp214"); /* KEYSC */
	clk_always_enable("mstp200"); /* LCDC */

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

	/* NAND Flash */
	ctrl_outw(ctrl_inw(PORT_PXCR) & 0x0fff, PORT_PXCR);
	ctrl_outl((ctrl_inl(BSC_CS6ABCR) & ~0x00000600) | 0x00000200,
		  BSC_CS6ABCR);

	/* Touch Panel - Enable IRQ6 */
	ctrl_outw(ctrl_inw(PORT_PZCR) & ~0xc, PORT_PZCR);
	ctrl_outw((ctrl_inw(PORT_PSELA) | 0x8000), PORT_PSELA);
	ctrl_outw((ctrl_inw(PORT_HIZCRC) & ~0x4000), PORT_HIZCRC);

#ifdef CONFIG_SH_MIGOR_RTA_WVGA
	/* LCDC - WVGA - Enable RGB Interface signals */
	ctrl_outw(ctrl_inw(PORT_PACR) & ~0x0003, PORT_PACR);
	ctrl_outw(0x0000, PORT_PHCR);
	ctrl_outw(0x0000, PORT_PLCR);
	ctrl_outw(0x0000, PORT_PMCR);
	ctrl_outw(ctrl_inw(PORT_PRCR) & ~0x000f, PORT_PRCR);
	ctrl_outw((ctrl_inw(PORT_PSELD) & ~0x000d) | 0x0400, PORT_PSELD);
	ctrl_outw(ctrl_inw(PORT_MSELCRB) & ~0x0100, PORT_MSELCRB);
	ctrl_outw(ctrl_inw(PORT_HIZCRA) & ~0x01e0, PORT_HIZCRA);
#endif
#ifdef CONFIG_SH_MIGOR_QVGA
	/* LCDC - QVGA - Enable SYS Interface signals */
	ctrl_outw(ctrl_inw(PORT_PACR) & ~0x0003, PORT_PACR);
	ctrl_outw((ctrl_inw(PORT_PHCR) & ~0xcfff) | 0x0010, PORT_PHCR);
	ctrl_outw(0x0000, PORT_PLCR);
	ctrl_outw(0x0000, PORT_PMCR);
	ctrl_outw(ctrl_inw(PORT_PRCR) & ~0x030f, PORT_PRCR);
	ctrl_outw((ctrl_inw(PORT_PSELD) & ~0x0001) | 0x0420, PORT_PSELD);
	ctrl_outw(ctrl_inw(PORT_MSELCRB) | 0x0100, PORT_MSELCRB);
	ctrl_outw(ctrl_inw(PORT_HIZCRA) & ~0x01e0, PORT_HIZCRA);
#endif
}

static struct sh_machine_vector mv_migor __initmv = {
	.mv_name		= "Migo-R",
	.mv_setup		= migor_setup,
};
