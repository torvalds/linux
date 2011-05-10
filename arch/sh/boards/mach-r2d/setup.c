/*
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Copyright (C) 2002 - 2006 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2004 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/ata_platform.h>
#include <linux/sm501.h>
#include <linux/sm501-regs.h>
#include <linux/pm.h>
#include <linux/fb.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <asm/machvec.h>
#include <mach/r2d.h>
#include <asm/io.h>
#include <asm/io_trapped.h>
#include <asm/spi.h>

static struct resource cf_ide_resources[] = {
	[0] = {
		.start	= PA_AREA5_IO + 0x1000,
		.end	= PA_AREA5_IO + 0x1000 + 0x10 - 0x2,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PA_AREA5_IO + 0x80c,
		.end	= PA_AREA5_IO + 0x80c,
		.flags	= IORESOURCE_MEM,
	},
#ifndef CONFIG_RTS7751R2D_1 /* For R2D-1 polling is preferred */
	[2] = {
		.start	= IRQ_CF_IDE,
		.flags	= IORESOURCE_IRQ,
	},
#endif
};

static struct pata_platform_info pata_info = {
	.ioport_shift	= 1,
};

static struct platform_device cf_ide_device  = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(cf_ide_resources),
	.resource	= cf_ide_resources,
	.dev	= {
		.platform_data	= &pata_info,
	},
};

static struct spi_board_info spi_bus[] = {
	{
		.modalias	= "rtc-r9701",
		.max_speed_hz	= 1000000,
		.mode		= SPI_MODE_3,
	},
};

static void r2d_chip_select(struct sh_spi_info *spi, int cs, int state)
{
	BUG_ON(cs != 0);  /* Single Epson RTC-9701JE attached on CS0 */
	__raw_writew(state == BITBANG_CS_ACTIVE, PA_RTCCE);
}

static struct sh_spi_info spi_info = {
	.num_chipselect = 1,
	.chip_select = r2d_chip_select,
};

static struct resource spi_sh_sci_resources[] = {
	{
		.start	= 0xffe00000,
		.end	= 0xffe0001f,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spi_sh_sci_device  = {
	.name		= "spi_sh_sci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(spi_sh_sci_resources),
	.resource	= spi_sh_sci_resources,
	.dev	= {
		.platform_data	= &spi_info,
	},
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_OUTPORT,
		.end	= PA_OUTPORT,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct resource sm501_resources[] = {
	[0]	= {
		.start	= 0x10000000,
		.end	= 0x13e00000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 0x13e00000,
		.end	= 0x13ffffff,
		.flags	= IORESOURCE_MEM,
	},
	[2]	= {
		.start	= IRQ_VOYAGER,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct fb_videomode sm501_default_mode = {
	.pixclock	= 35714,
	.xres		= 640,
	.yres		= 480,
	.left_margin	= 105,
	.right_margin	= 50,
	.upper_margin	= 35,
	.lower_margin	= 0,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct sm501_platdata_fbsub sm501_pdata_fbsub_pnl = {
	.def_bpp	= 16,
	.def_mode	= &sm501_default_mode,
	.flags		= SM501FB_FLAG_USE_INIT_MODE |
			  SM501FB_FLAG_USE_HWCURSOR |
			  SM501FB_FLAG_USE_HWACCEL |
			  SM501FB_FLAG_DISABLE_AT_EXIT,
};

static struct sm501_platdata_fbsub sm501_pdata_fbsub_crt = {
	.flags		= (SM501FB_FLAG_USE_INIT_MODE |
			   SM501FB_FLAG_USE_HWCURSOR |
			   SM501FB_FLAG_USE_HWACCEL |
			   SM501FB_FLAG_DISABLE_AT_EXIT),

};

static struct sm501_platdata_fb sm501_fb_pdata = {
	.fb_route	= SM501_FB_OWN,
	.fb_crt		= &sm501_pdata_fbsub_crt,
	.fb_pnl		= &sm501_pdata_fbsub_pnl,
	.flags		= SM501_FBPD_SWAP_FB_ENDIAN,
};

static struct sm501_initdata sm501_initdata = {
	.devices	= SM501_USE_USB_HOST | SM501_USE_UART0,
};

static struct sm501_platdata sm501_platform_data = {
	.init		= &sm501_initdata,
	.fb		= &sm501_fb_pdata,
};

static struct platform_device sm501_device = {
	.name		= "sm501",
	.id		= -1,
	.dev		= {
		.platform_data	= &sm501_platform_data,
	},
	.num_resources	= ARRAY_SIZE(sm501_resources),
	.resource	= sm501_resources,
};

static struct mtd_partition r2d_partitions[] = {
	{
		.name		= "U-Boot",
		.offset		= 0x00000000,
		.size		= 0x00040000,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "Environment",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= 0x00040000,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "Kernel",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= 0x001c0000,
	}, {
		.name		= "Flash_FS",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data flash_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(r2d_partitions),
	.parts		= r2d_partitions,
};

static struct resource flash_resource = {
	.start		= 0x00000000,
	.end		= 0x02000000,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= &flash_resource,
	.num_resources	= 1,
	.dev		= {
		.platform_data = &flash_data,
	},
};

static struct platform_device *rts7751r2d_devices[] __initdata = {
	&sm501_device,
	&heartbeat_device,
	&spi_sh_sci_device,
};

/*
 * The CF is connected with a 16-bit bus where 8-bit operations are
 * unsupported. The linux ata driver is however using 8-bit operations, so
 * insert a trapped io filter to convert 8-bit operations into 16-bit.
 */
static struct trapped_io cf_trapped_io = {
	.resource		= cf_ide_resources,
	.num_resources		= 2,
	.minimum_bus_width	= 16,
};

static int __init rts7751r2d_devices_setup(void)
{
	if (register_trapped_io(&cf_trapped_io) == 0)
		platform_device_register(&cf_ide_device);

	if (mach_is_r2d_plus())
		platform_device_register(&flash_device);

	spi_register_board_info(spi_bus, ARRAY_SIZE(spi_bus));

	return platform_add_devices(rts7751r2d_devices,
				    ARRAY_SIZE(rts7751r2d_devices));
}
device_initcall(rts7751r2d_devices_setup);

static void rts7751r2d_power_off(void)
{
	__raw_writew(0x0001, PA_POWOFF);
}

/*
 * Initialize the board
 */
static void __init rts7751r2d_setup(char **cmdline_p)
{
	void __iomem *sm501_reg;
	u16 ver = __raw_readw(PA_VERREG);

	printk(KERN_INFO "Renesas Technology Sales RTS7751R2D support.\n");

	printk(KERN_INFO "FPGA version:%d (revision:%d)\n",
					(ver >> 4) & 0xf, ver & 0xf);

	__raw_writew(0x0000, PA_OUTPORT);
	pm_power_off = rts7751r2d_power_off;

	/* sm501 dram configuration:
	 * ColSizeX = 11 - External Memory Column Size: 256 words.
	 * APX = 1 - External Memory Active to Pre-Charge Delay: 7 clocks.
	 * RstX = 1 - External Memory Reset: Normal.
	 * Rfsh = 1 - Local Memory Refresh to Command Delay: 12 clocks.
	 * BwC =  1 - Local Memory Block Write Cycle Time: 2 clocks.
	 * BwP =  1 - Local Memory Block Write to Pre-Charge Delay: 1 clock.
	 * AP = 1 - Internal Memory Active to Pre-Charge Delay: 7 clocks.
	 * Rst = 1 - Internal Memory Reset: Normal.
	 * RA = 1 - Internal Memory Remain in Active State: Do not remain.
	 */

	sm501_reg = (void __iomem *)0xb3e00000 + SM501_DRAM_CONTROL;
	writel(readl(sm501_reg) | 0x00f107c0, sm501_reg);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_rts7751r2d __initmv = {
	.mv_name		= "RTS7751R2D",
	.mv_setup		= rts7751r2d_setup,
	.mv_init_irq		= init_rts7751r2d_IRQ,
	.mv_irq_demux		= rts7751r2d_irq_demux,
};
