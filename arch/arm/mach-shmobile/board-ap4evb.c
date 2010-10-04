/*
 * AP4EVB board support
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
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
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mfd/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/sh_intc.h>
#include <linux/sh_clk.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/input/sh_keysc.h>
#include <linux/usb/r8a66597.h>

#include <sound/sh_fsi.h>

#include <video/sh_mobile_hdmi.h>
#include <video/sh_mobile_lcdc.h>
#include <video/sh_mipi_dsi.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/sh7372.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

/*
 * Address	Interface		BusWidth	note
 * ------------------------------------------------------------------
 * 0x0000_0000	NOR Flash ROM (MCP)	16bit		SW7 : bit1 = ON
 * 0x0800_0000	user area		-
 * 0x1000_0000	NOR Flash ROM (MCP)	16bit		SW7 : bit1 = OFF
 * 0x1400_0000	Ether (LAN9220)		16bit
 * 0x1600_0000	user area		-		cannot use with NAND
 * 0x1800_0000	user area		-
 * 0x1A00_0000	-
 * 0x4000_0000	LPDDR2-SDRAM (POP)	32bit
 */

/*
 * NOR Flash ROM
 *
 *  SW1  |     SW2    | SW7  | NOR Flash ROM
 *  bit1 | bit1  bit2 | bit1 | Memory allocation
 * ------+------------+------+------------------
 *  OFF  | ON     OFF | ON   |    Area 0
 *  OFF  | ON     OFF | OFF  |    Area 4
 */

/*
 * NAND Flash ROM
 *
 *  SW1  |     SW2    | SW7  | NAND Flash ROM
 *  bit1 | bit1  bit2 | bit2 | Memory allocation
 * ------+------------+------+------------------
 *  OFF  | ON     OFF | ON   |    FCE 0
 *  OFF  | ON     OFF | OFF  |    FCE 1
 */

/*
 * SMSC 9220
 *
 *  SW1		SMSC 9220
 * -----------------------
 *  ON		access disable
 *  OFF		access enable
 */

/*
 * LCD / IRQ / KEYSC / IrDA
 *
 * IRQ = IRQ26 (TS), IRQ27 (VIO), IRQ28 (QHD-TouchScreen)
 * LCD = 2nd LCDC (WVGA)
 *
 * 		|		SW43			|
 * SW3		|	ON		|	OFF	|
 * -------------+-----------------------+---------------+
 * ON		| KEY / IrDA		| LCD		|
 * OFF		| KEY / IrDA / IRQ	| IRQ		|
 *
 *
 * QHD / WVGA display
 *
 * You can choice display type on menuconfig.
 * Then, check above dip-switch.
 */

/*
 * USB
 *
 * J7 : 1-2  MAX3355E VBUS
 *      2-3  DC 5.0V
 *
 * S39: bit2: off
 */

/*
 * FSI/FSMI
 *
 * SW41	:  ON : SH-Mobile AP4 Audio Mode
 *	: OFF : Bluetooth Audio Mode
 */

/*
 * MMC0/SDHI1 (CN7)
 *
 * J22 : select card voltage
 *       1-2 pin : 1.8v
 *       2-3 pin : 3.3v
 *
 *        SW1  |             SW33
 *             | bit1 | bit2 | bit3 | bit4
 * ------------+------+------+------+-------
 * MMC0   OFF  |  OFF |  ON  |  ON  |  X
 * SDHI1  OFF  |  ON  |   X  |  OFF | ON
 *
 * voltage lebel
 * CN7 : 1.8v
 * CN12: 3.3v
 */

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
	},
	{
		.name		= "kernel_ro",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 8 * 1024 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 8 * 1024 * 1024,
	},
	{
		.name		= "data",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0]	= {
		.start	= 0x00000000,
		.end	= 0x08000000 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.resource	= nor_flash_resources,
};

/* SMSC 9220 */
static struct resource smc911x_resources[] = {
	{
		.start	= 0x14000000,
		.end	= 0x16000000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x02c0) /* IRQ6A */,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_16BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type       = SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device smc911x_device = {
	.name           = "smsc911x",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(smc911x_resources),
	.resource       = smc911x_resources,
	.dev            = {
		.platform_data = &smsc911x_info,
	},
};

/* SH_MMCIF */
static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "SH_MMCIF",
		.start	= 0xE6BD0000,
		.end	= 0xE6BD00FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* MMC ERR */
		.start	= evt2irq(0x1ac0),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* MMC NOR */
		.start	= evt2irq(0x1ae0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0,
	.ocr		= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NEEDS_POLL,
};

static struct platform_device sh_mmcif_device = {
	.name		= "sh_mmcif",
	.id		= 0,
	.dev		= {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh_mmcif_plat,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0xe6850000,
		.end    = 0xe68501ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x0e00) /* SDHI0 */,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi0_resources),
	.resource       = sdhi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

/* SDHI1 */
static struct sh_mobile_sdhi_info sdhi1_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.tmio_ocr_mask	= MMC_VDD_165_195,
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start  = 0xe6860000,
		.end    = 0xe68601ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x0e80),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi1_resources),
	.resource       = sdhi1_resources,
	.id             = 1,
	.dev	= {
		.platform_data	= &sdhi1_info,
	},
};

/* USB1 */
static void usb1_host_port_power(int port, int power)
{
	if (!power) /* only power-on supported for now */
		return;

	/* set VBOUT/PWEN and EXTLP1 in DVSTCTR */
	__raw_writew(__raw_readw(0xE68B0008) | 0x600, 0xE68B0008);
}

static struct r8a66597_platdata usb1_host_data = {
	.on_chip	= 1,
	.port_power	= usb1_host_port_power,
};

static struct resource usb1_host_resources[] = {
	[0] = {
		.name	= "USBHS",
		.start	= 0xE68B0000,
		.end	= 0xE68B00E6 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x1ce0) /* USB1_USB1I0 */,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb1_host_device = {
	.name	= "r8a66597_hcd",
	.id	= 1,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb1_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb1_host_resources),
	.resource	= usb1_host_resources,
};

const static struct fb_videomode ap4evb_lcdc_modes[] = {
	{
#ifdef CONFIG_AP4EVB_QHD
		.name		= "R63302(QHD)",
		.xres		= 544,
		.yres		= 961,
		.left_margin	= 72,
		.right_margin	= 600,
		.hsync_len	= 16,
		.upper_margin	= 8,
		.lower_margin	= 8,
		.vsync_len	= 2,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
#else
		.name		= "WVGA Panel",
		.xres		= 800,
		.yres		= 480,
		.left_margin	= 220,
		.right_margin	= 110,
		.hsync_len	= 70,
		.upper_margin	= 20,
		.lower_margin	= 5,
		.vsync_len	= 5,
		.sync		= 0,
#endif
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.lcd_cfg = ap4evb_lcdc_modes,
		.num_cfg = ARRAY_SIZE(ap4evb_lcdc_modes),
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000, /* P4-only space */
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev	= {
		.platform_data	= &lcdc_info,
		.coherent_dma_mask = ~0,
	},
};

/*
 * QHD display
 */
#ifdef CONFIG_AP4EVB_QHD

/* KEYSC (Needs SW43 set to ON) */
static struct sh_keysc_info keysc_info = {
	.mode		= SH_KEYSC_MODE_1,
	.scan_timing	= 3,
	.delay		= 2500,
	.keycodes = {
		KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
		KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
		KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
		KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start  = 0xe61b0000,
		.end    = 0xe61b0063,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x0be0), /* KEYSC_KEY */
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* "keysc0" clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
};

/* MIPI-DSI */
static struct resource mipidsi0_resources[] = {
	[0] = {
		.start  = 0xffc60000,
		.end    = 0xffc68fff,
		.flags  = IORESOURCE_MEM,
	},
};

static struct sh_mipi_dsi_info mipidsi0_info = {
	.data_format	= MIPI_RGB888,
	.lcd_chan	= &lcdc_info.ch[0],
};

static struct platform_device mipidsi0_device = {
	.name           = "sh-mipi-dsi",
	.num_resources  = ARRAY_SIZE(mipidsi0_resources),
	.resource       = mipidsi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &mipidsi0_info,
	},
};

/* This function will disappear when we switch to (runtime) PM */
static int __init ap4evb_init_display_clk(void)
{
	struct clk *lcdc_clk;
	struct clk *dsitx_clk;
	int ret;

	lcdc_clk = clk_get(&lcdc_device.dev, "sh_mobile_lcdc_fb.0");
	if (IS_ERR(lcdc_clk))
		return PTR_ERR(lcdc_clk);

	dsitx_clk = clk_get(&mipidsi0_device.dev, "sh-mipi-dsi.0");
	if (IS_ERR(dsitx_clk)) {
		ret = PTR_ERR(dsitx_clk);
		goto eclkdsitxget;
	}

	ret = clk_enable(lcdc_clk);
	if (ret < 0)
		goto eclklcdcon;

	ret = clk_enable(dsitx_clk);
	if (ret < 0)
		goto eclkdsitxon;

	return 0;

eclkdsitxon:
	clk_disable(lcdc_clk);
eclklcdcon:
	clk_put(dsitx_clk);
eclkdsitxget:
	clk_put(lcdc_clk);

	return ret;
}
device_initcall(ap4evb_init_display_clk);

static struct platform_device *qhd_devices[] __initdata = {
	&mipidsi0_device,
	&keysc_device,
};
#endif /* CONFIG_AP4EVB_QHD */

/* FSI */
#define IRQ_FSI		evt2irq(0x1840)
#define FSIACKCR	0xE6150018
static void fsiackcr_init(struct clk *clk)
{
	u32 status = __raw_readl(clk->enable_reg);

	/* use external clock */
	status &= ~0x000000ff;
	status |= 0x00000080;
	__raw_writel(status, clk->enable_reg);
}

static struct clk_ops fsiackcr_clk_ops = {
	.init = fsiackcr_init,
};

static struct clk fsiackcr_clk = {
	.ops		= &fsiackcr_clk_ops,
	.enable_reg	= (void __iomem *)FSIACKCR,
	.rate		= 0, /* unknown */
};

static struct sh_fsi_platform_info fsi_info = {
	.porta_flags = SH_FSI_BRS_INV |
		       SH_FSI_OUT_SLAVE_MODE |
		       SH_FSI_IN_SLAVE_MODE |
		       SH_FSI_OFMT(PCM) |
		       SH_FSI_IFMT(PCM),
};

static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xFE3C0000,
		.end	= 0xFE3C0400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_FSI,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device fsi_device = {
	.name		= "sh_fsi2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(fsi_resources),
	.resource	= fsi_resources,
	.dev	= {
		.platform_data	= &fsi_info,
	},
};

/*
 * If left and right margins are not multiples of 8,
 * LDHAJR will be adjusted accordingly by the LCDC
 * driver. Until we start using EDID, these values
 * might have to be adjusted for different monitors.
 */
const static struct fb_videomode ap4evb_hdmi_modes[] = {
	{
		.name = "HDMI 720p",
		.xres = 1280,
		.yres = 720,

		.left_margin = 200,
		.right_margin = 88,
		.hsync_len = 48,

		.upper_margin = 20,
		.lower_margin = 5,
		.vsync_len = 5,

		.pixclock = 13468,
		.sync = FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
	}, {
		.name = "1280x1024",
		.xres = 1280,
		.yres = 1024,

		.left_margin = 144,
		.right_margin = 48,
		.hsync_len = 64,

		.upper_margin = 35,
		.lower_margin = 5,
		.vsync_len = 3,

		.pixclock = 9800,
		.sync = FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
	}, {
		.name = "HDMI 480p",
		.xres = 720,
		.yres = 480,

		.left_margin = 36,
		.right_margin = 18,
		.hsync_len = 68,

		.upper_margin = 36,
		.lower_margin = 3,
		.vsync_len = 6,
		.pixclock = 37037,
		.sync = FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
	},
};

static struct sh_mobile_lcdc_info sh_mobile_lcdc1_info = {
	.clock_source = LCDC_CLK_EXTERNAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.interface_type = RGB24,
		.clock_divider = 1,
		.flags = LCDC_FLAGS_DWPOL,
		.lcd_cfg = ap4evb_hdmi_modes,
		.num_cfg = ARRAY_SIZE(ap4evb_hdmi_modes),
	}
};

static struct resource lcdc1_resources[] = {
	[0] = {
		.name	= "LCDC1",
		.start	= 0xfe944000,
		.end	= 0xfe947fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x17a0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc1_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc1_resources),
	.resource	= lcdc1_resources,
	.id             = 1,
	.dev	= {
		.platform_data	= &sh_mobile_lcdc1_info,
		.coherent_dma_mask = ~0,
	},
};

static struct sh_mobile_hdmi_info hdmi_info = {
	.lcd_chan = &sh_mobile_lcdc1_info.ch[0],
	.lcd_dev = &lcdc1_device.dev,
};

static struct resource hdmi_resources[] = {
	[0] = {
		.name	= "HDMI",
		.start	= 0xe6be0000,
		.end	= 0xe6be00ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* There's also an HDMI interrupt on INTCS @ 0x18e0 */
		.start	= evt2irq(0x17e0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device hdmi_device = {
	.name		= "sh-mobile-hdmi",
	.num_resources	= ARRAY_SIZE(hdmi_resources),
	.resource	= hdmi_resources,
	.id             = -1,
	.dev	= {
		.platform_data	= &hdmi_info,
	},
};

static struct gpio_led ap4evb_leds[] = {
	{
		.name			= "led4",
		.gpio			= GPIO_PORT185,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led2",
		.gpio			= GPIO_PORT186,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led3",
		.gpio			= GPIO_PORT187,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led1",
		.gpio			= GPIO_PORT188,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}
};

static struct gpio_led_platform_data ap4evb_leds_pdata = {
	.num_leds = ARRAY_SIZE(ap4evb_leds),
	.leds = ap4evb_leds,
};

static struct platform_device leds_device = {
	.name = "leds-gpio",
	.id = 0,
	.dev = {
		.platform_data  = &ap4evb_leds_pdata,
	},
};

static struct platform_device *ap4evb_devices[] __initdata = {
	&leds_device,
	&nor_flash_device,
	&smc911x_device,
	&sdhi0_device,
	&sdhi1_device,
	&usb1_host_device,
	&fsi_device,
	&sh_mmcif_device,
	&lcdc1_device,
	&lcdc_device,
	&hdmi_device,
};

static int __init hdmi_init_pm_clock(void)
{
	struct clk *hdmi_ick = clk_get(&hdmi_device.dev, "ick");
	int ret;
	long rate;

	if (IS_ERR(hdmi_ick)) {
		ret = PTR_ERR(hdmi_ick);
		pr_err("Cannot get HDMI ICK: %d\n", ret);
		goto out;
	}

	ret = clk_set_parent(&pllc2_clk, &dv_clki_div2_clk);
	if (ret < 0) {
		pr_err("Cannot set PLLC2 parent: %d, %d users\n", ret, pllc2_clk.usecount);
		goto out;
	}

	pr_debug("PLLC2 initial frequency %lu\n", clk_get_rate(&pllc2_clk));

	rate = clk_round_rate(&pllc2_clk, 594000000);
	if (rate < 0) {
		pr_err("Cannot get suitable rate: %ld\n", rate);
		ret = rate;
		goto out;
	}

	ret = clk_set_rate(&pllc2_clk, rate);
	if (ret < 0) {
		pr_err("Cannot set rate %ld: %d\n", rate, ret);
		goto out;
	}

	pr_debug("PLLC2 set frequency %lu\n", rate);

	ret = clk_set_parent(hdmi_ick, &pllc2_clk);
	if (ret < 0) {
		pr_err("Cannot set HDMI parent: %d\n", ret);
		goto out;
	}

out:
	if (!IS_ERR(hdmi_ick))
		clk_put(hdmi_ick);
	return ret;
}

device_initcall(hdmi_init_pm_clock);

/*
 * FIXME !!
 *
 * gpio_no_direction
 * are quick_hack.
 *
 * current gpio frame work doesn't have
 * the method to control only pull up/down/free.
 * this function should be replaced by correct gpio function
 */
static void __init gpio_no_direction(u32 addr)
{
	__raw_writeb(0x00, addr);
}

/* TouchScreen */
#ifdef CONFIG_AP4EVB_QHD
# define GPIO_TSC_IRQ	GPIO_FN_IRQ28_123
# define GPIO_TSC_PORT	GPIO_PORT123
#else /* WVGA */
# define GPIO_TSC_IRQ	GPIO_FN_IRQ7_40
# define GPIO_TSC_PORT	GPIO_PORT40
#endif

#define IRQ28	evt2irq(0x3380) /* IRQ28A */
#define IRQ7	evt2irq(0x02e0) /* IRQ7A */
static int ts_get_pendown_state(void)
{
	int val;

	gpio_free(GPIO_TSC_IRQ);

	gpio_request(GPIO_TSC_PORT, NULL);

	gpio_direction_input(GPIO_TSC_PORT);

	val = gpio_get_value(GPIO_TSC_PORT);

	gpio_request(GPIO_TSC_IRQ, NULL);

	return !val;
}

static int ts_init(void)
{
	gpio_request(GPIO_TSC_IRQ, NULL);

	return 0;
}

static struct tsc2007_platform_data tsc2007_info = {
	.model			= 2007,
	.x_plate_ohms		= 180,
	.get_pendown_state	= ts_get_pendown_state,
	.init_platform_hw	= ts_init,
};

static struct i2c_board_info tsc_device = {
	I2C_BOARD_INFO("tsc2007", 0x48),
	.type		= "tsc2007",
	.platform_data	= &tsc2007_info,
	/*.irq is selected on ap4evb_init */
};

/* I2C */
static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("ak4643", 0x13),
	},
};

static struct i2c_board_info i2c1_devices[] = {
	{
		I2C_BOARD_INFO("r2025sd", 0x32),
	},
};

static struct map_desc ap4evb_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init ap4evb_map_io(void)
{
	iotable_init(ap4evb_io_desc, ARRAY_SIZE(ap4evb_io_desc));

	/* setup early devices and console here as well */
	sh7372_add_early_devices();
	shmobile_setup_console();
}

#define GPIO_PORT9CR	0xE6051009
#define GPIO_PORT10CR	0xE605100A
static void __init ap4evb_init(void)
{
	u32 srcr4;
	struct clk *clk;

	sh7372_pinmux_init();

	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIFA0_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA0_RXD, NULL);

	/* enable SMSC911X */
	gpio_request(GPIO_FN_CS5A,	NULL);
	gpio_request(GPIO_FN_IRQ6_39,	NULL);

	/* enable Debug switch (S6) */
	gpio_request(GPIO_PORT32, NULL);
	gpio_request(GPIO_PORT33, NULL);
	gpio_request(GPIO_PORT34, NULL);
	gpio_request(GPIO_PORT35, NULL);
	gpio_direction_input(GPIO_PORT32);
	gpio_direction_input(GPIO_PORT33);
	gpio_direction_input(GPIO_PORT34);
	gpio_direction_input(GPIO_PORT35);
	gpio_export(GPIO_PORT32, 0);
	gpio_export(GPIO_PORT33, 0);
	gpio_export(GPIO_PORT34, 0);
	gpio_export(GPIO_PORT35, 0);

	/* SDHI0 */
	gpio_request(GPIO_FN_SDHICD0, NULL);
	gpio_request(GPIO_FN_SDHIWP0, NULL);
	gpio_request(GPIO_FN_SDHICMD0, NULL);
	gpio_request(GPIO_FN_SDHICLK0, NULL);
	gpio_request(GPIO_FN_SDHID0_3, NULL);
	gpio_request(GPIO_FN_SDHID0_2, NULL);
	gpio_request(GPIO_FN_SDHID0_1, NULL);
	gpio_request(GPIO_FN_SDHID0_0, NULL);

	/* SDHI1 */
	gpio_request(GPIO_FN_SDHICMD1, NULL);
	gpio_request(GPIO_FN_SDHICLK1, NULL);
	gpio_request(GPIO_FN_SDHID1_3, NULL);
	gpio_request(GPIO_FN_SDHID1_2, NULL);
	gpio_request(GPIO_FN_SDHID1_1, NULL);
	gpio_request(GPIO_FN_SDHID1_0, NULL);

	/* MMCIF */
	gpio_request(GPIO_FN_MMCD0_0, NULL);
	gpio_request(GPIO_FN_MMCD0_1, NULL);
	gpio_request(GPIO_FN_MMCD0_2, NULL);
	gpio_request(GPIO_FN_MMCD0_3, NULL);
	gpio_request(GPIO_FN_MMCD0_4, NULL);
	gpio_request(GPIO_FN_MMCD0_5, NULL);
	gpio_request(GPIO_FN_MMCD0_6, NULL);
	gpio_request(GPIO_FN_MMCD0_7, NULL);
	gpio_request(GPIO_FN_MMCCMD0, NULL);
	gpio_request(GPIO_FN_MMCCLK0, NULL);

	/* USB enable */
	gpio_request(GPIO_FN_VBUS0_1,    NULL);
	gpio_request(GPIO_FN_IDIN_1_18,  NULL);
	gpio_request(GPIO_FN_PWEN_1_115, NULL);
	gpio_request(GPIO_FN_OVCN_1_114, NULL);
	gpio_request(GPIO_FN_EXTLP_1,    NULL);
	gpio_request(GPIO_FN_OVCN2_1,    NULL);

	/* setup USB phy */
	__raw_writew(0x8a0a, 0xE6058130);	/* USBCR2 */

	/* enable FSI2 */
	gpio_request(GPIO_FN_FSIAIBT,	NULL);
	gpio_request(GPIO_FN_FSIAILR,	NULL);
	gpio_request(GPIO_FN_FSIAISLD,	NULL);
	gpio_request(GPIO_FN_FSIAOSLD,	NULL);
	gpio_request(GPIO_PORT161,	NULL);
	gpio_direction_output(GPIO_PORT161, 0); /* slave */

	gpio_request(GPIO_PORT9, NULL);
	gpio_request(GPIO_PORT10, NULL);
	gpio_no_direction(GPIO_PORT9CR);  /* FSIAOBT needs no direction */
	gpio_no_direction(GPIO_PORT10CR); /* FSIAOLR needs no direction */

	/* set SPU2 clock to 119.6 MHz */
	clk = clk_get(NULL, "spu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 119600000));
		clk_put(clk);
	}

	/* change parent of FSI A */
	clk = clk_get(NULL, "fsia_clk");
	if (!IS_ERR(clk)) {
		clk_register(&fsiackcr_clk);
		clk_set_parent(clk, &fsiackcr_clk);
		clk_put(clk);
	}

	/*
	 * set irq priority, to avoid sound chopping
	 * when NFS rootfs is used
	 *  FSI(3) > SMSC911X(2)
	 */
	intc_set_priority(IRQ_FSI, 3);

	i2c_register_board_info(0, i2c0_devices,
				ARRAY_SIZE(i2c0_devices));

	i2c_register_board_info(1, i2c1_devices,
				ARRAY_SIZE(i2c1_devices));

#ifdef CONFIG_AP4EVB_QHD

	/*
	 * For QHD Panel (MIPI-DSI, CONFIG_AP4EVB_QHD=y) and
	 * IRQ28 for Touch Panel, set dip switches S3, S43 as OFF, ON.
	 */

	/* enable KEYSC */
	gpio_request(GPIO_FN_KEYOUT0, NULL);
	gpio_request(GPIO_FN_KEYOUT1, NULL);
	gpio_request(GPIO_FN_KEYOUT2, NULL);
	gpio_request(GPIO_FN_KEYOUT3, NULL);
	gpio_request(GPIO_FN_KEYOUT4, NULL);
	gpio_request(GPIO_FN_KEYIN0_136, NULL);
	gpio_request(GPIO_FN_KEYIN1_135, NULL);
	gpio_request(GPIO_FN_KEYIN2_134, NULL);
	gpio_request(GPIO_FN_KEYIN3_133, NULL);
	gpio_request(GPIO_FN_KEYIN4,     NULL);

	/* enable TouchScreen */
	set_irq_type(IRQ28, IRQ_TYPE_LEVEL_LOW);

	tsc_device.irq = IRQ28;
	i2c_register_board_info(1, &tsc_device, 1);

	/* LCDC0 */
	lcdc_info.clock_source			= LCDC_CLK_PERIPHERAL;
	lcdc_info.ch[0].interface_type		= RGB24;
	lcdc_info.ch[0].clock_divider		= 1;
	lcdc_info.ch[0].flags			= LCDC_FLAGS_DWPOL;
	lcdc_info.ch[0].lcd_size_cfg.width	= 44;
	lcdc_info.ch[0].lcd_size_cfg.height	= 79;

	platform_add_devices(qhd_devices, ARRAY_SIZE(qhd_devices));

#else
	/*
	 * For WVGA Panel (18-bit RGB, CONFIG_AP4EVB_WVGA=y) and
	 * IRQ7 for Touch Panel, set dip switches S3, S43 to ON, OFF.
	 */

	gpio_request(GPIO_FN_LCDD17,   NULL);
	gpio_request(GPIO_FN_LCDD16,   NULL);
	gpio_request(GPIO_FN_LCDD15,   NULL);
	gpio_request(GPIO_FN_LCDD14,   NULL);
	gpio_request(GPIO_FN_LCDD13,   NULL);
	gpio_request(GPIO_FN_LCDD12,   NULL);
	gpio_request(GPIO_FN_LCDD11,   NULL);
	gpio_request(GPIO_FN_LCDD10,   NULL);
	gpio_request(GPIO_FN_LCDD9,    NULL);
	gpio_request(GPIO_FN_LCDD8,    NULL);
	gpio_request(GPIO_FN_LCDD7,    NULL);
	gpio_request(GPIO_FN_LCDD6,    NULL);
	gpio_request(GPIO_FN_LCDD5,    NULL);
	gpio_request(GPIO_FN_LCDD4,    NULL);
	gpio_request(GPIO_FN_LCDD3,    NULL);
	gpio_request(GPIO_FN_LCDD2,    NULL);
	gpio_request(GPIO_FN_LCDD1,    NULL);
	gpio_request(GPIO_FN_LCDD0,    NULL);
	gpio_request(GPIO_FN_LCDDISP,  NULL);
	gpio_request(GPIO_FN_LCDDCK,   NULL);

	gpio_request(GPIO_PORT189, NULL); /* backlight */
	gpio_direction_output(GPIO_PORT189, 1);

	gpio_request(GPIO_PORT151, NULL); /* LCDDON */
	gpio_direction_output(GPIO_PORT151, 1);

	lcdc_info.clock_source			= LCDC_CLK_BUS;
	lcdc_info.ch[0].interface_type		= RGB18;
	lcdc_info.ch[0].clock_divider		= 2;
	lcdc_info.ch[0].flags			= 0;
	lcdc_info.ch[0].lcd_size_cfg.width	= 152;
	lcdc_info.ch[0].lcd_size_cfg.height	= 91;

	/* enable TouchScreen */
	set_irq_type(IRQ7, IRQ_TYPE_LEVEL_LOW);

	tsc_device.irq = IRQ7;
	i2c_register_board_info(0, &tsc_device, 1);
#endif /* CONFIG_AP4EVB_QHD */

	sh7372_add_standard_devices();

	/* HDMI */
	gpio_request(GPIO_FN_HDMI_HPD, NULL);
	gpio_request(GPIO_FN_HDMI_CEC, NULL);

	/* Reset HDMI, must be held at least one EXTALR (32768Hz) period */
#define SRCR4 0xe61580bc
	srcr4 = __raw_readl(SRCR4);
	__raw_writel(srcr4 | (1 << 13), SRCR4);
	udelay(50);
	__raw_writel(srcr4 & ~(1 << 13), SRCR4);

	platform_add_devices(ap4evb_devices, ARRAY_SIZE(ap4evb_devices));
}

static void __init ap4evb_timer_init(void)
{
	sh7372_clock_init();
	shmobile_timer.init();

	/* External clock source */
	clk_set_rate(&dv_clki_clk, 27000000);
}

static struct sys_timer ap4evb_timer = {
	.init		= ap4evb_timer_init,
};

MACHINE_START(AP4EVB, "ap4evb")
	.phys_io	= 0xe6000000,
	.io_pg_offst	= ((0xe6000000) >> 18) & 0xfffc,
	.map_io		= ap4evb_map_io,
	.init_irq	= sh7372_init_irq,
	.init_machine	= ap4evb_init,
	.timer		= &ap4evb_timer,
MACHINE_END
