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
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>
#include <linux/io.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/sh_intc.h>
#include <linux/sh_clk.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/input/sh_keysc.h>
#include <linux/usb/r8a66597.h>
#include <linux/pm_clock.h>
#include <linux/dma-mapping.h>

#include <media/sh_mobile_ceu.h>
#include <media/sh_mobile_csi2.h>
#include <media/soc_camera.h>

#include <sound/sh_fsi.h>
#include <sound/simple_card.h>

#include <video/sh_mobile_hdmi.h>
#include <video/sh_mobile_lcdc.h>
#include <video/sh_mipi_dsi.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/sh7372.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>

#include "sh-gpio.h"

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
 *
 * it needs amixer settings for playing
 *
 * amixer set "Headphone Enable" on
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

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply fixed1v8_power_consumers[] =
{
	/* J22 default position: 1.8V */
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
};

static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
		.mask_flags	= MTD_WRITEABLE,
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
		.start	= 0x20000000, /* CS0 shadow instead of regular CS0 */
		.end	= 0x28000000 - 1, /* needed by USB MASK ROM boot */
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

/*
 * The card detect pin of the top SD/MMC slot (CN7) is active low and is
 * connected to GPIO A22 of SH7372 (GPIO 41).
 */
static int slot_cn7_get_cd(struct platform_device *pdev)
{
	return !gpio_get_value(41);
}
/* MERAM */
static struct sh_mobile_meram_info meram_info = {
	.addr_mode      = SH_MOBILE_MERAM_MODE1,
};

static struct resource meram_resources[] = {
	[0] = {
		.name	= "regs",
		.start	= 0xe8000000,
		.end	= 0xe807ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "meram",
		.start	= 0xe8080000,
		.end	= 0xe81fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device meram_device = {
	.name           = "sh_mobile_meram",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(meram_resources),
	.resource       = meram_resources,
	.dev            = {
		.platform_data = &meram_info,
	},
};

/* SH_MMCIF */
static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "MMCIF",
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
	.get_cd		= slot_cn7_get_cd,
	.slave_id_tx	= SHDMA_SLAVE_MMCIF_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMCIF_RX,
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
	.tmio_caps	= MMC_CAP_SDIO_IRQ,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0xe6850000,
		.end    = 0xe68500ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0e00) /* SDHI0_SDHI0I0 */,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= evt2irq(0x0e20) /* SDHI0_SDHI0I1 */,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= evt2irq(0x0e40) /* SDHI0_SDHI0I2 */,
		.flags	= IORESOURCE_IRQ,
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
	.tmio_caps	= MMC_CAP_NEEDS_POLL | MMC_CAP_SDIO_IRQ,
	.get_cd		= slot_cn7_get_cd,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start  = 0xe6860000,
		.end    = 0xe68600ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0e80), /* SDHI1_SDHI1I0 */
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= evt2irq(0x0ea0), /* SDHI1_SDHI1I1 */
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= evt2irq(0x0ec0), /* SDHI1_SDHI1I2 */
		.flags	= IORESOURCE_IRQ,
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
	__raw_writew(__raw_readw(IOMEM(0xE68B0008)) | 0x600, IOMEM(0xE68B0008));
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
static int sh_mipi_set_dot_clock(struct platform_device *pdev,
				 void __iomem *base,
				 int enable)
{
	struct clk *pck = clk_get(&pdev->dev, "dsip_clk");

	if (IS_ERR(pck))
		return PTR_ERR(pck);

	if (enable) {
		/*
		 * DSIPCLK	= 24MHz
		 * D-PHY	= DSIPCLK * ((0x6*2)+1) = 312MHz (see .phyctrl)
		 * HsByteCLK	= D-PHY/8 = 39MHz
		 *
		 *  X * Y * FPS =
		 * (544+72+600+16) * (961+8+8+2) * 30 = 36.1MHz
		 */
		clk_set_rate(pck, clk_round_rate(pck, 24000000));
		clk_enable(pck);
	} else {
		clk_disable(pck);
	}

	clk_put(pck);

	return 0;
}

static struct resource mipidsi0_resources[] = {
	[0] = {
		.start  = 0xffc60000,
		.end    = 0xffc63073,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 0xffc68000,
		.end    = 0xffc680ef,
		.flags  = IORESOURCE_MEM,
	},
};

static struct sh_mipi_dsi_info mipidsi0_info = {
	.data_format	= MIPI_RGB888,
	.channel	= LCDC_CHAN_MAINLCD,
	.lane		= 2,
	.vsynw_offset	= 17,
	.phyctrl	= 0x6 << 8,
	.flags		= SH_MIPI_DSI_SYNC_PULSES_MODE |
			  SH_MIPI_DSI_HSbyteCLK,
	.set_dot_clock	= sh_mipi_set_dot_clock,
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

static struct platform_device *qhd_devices[] __initdata = {
	&mipidsi0_device,
	&keysc_device,
};
#endif /* CONFIG_AP4EVB_QHD */

/* LCDC0 */
static const struct fb_videomode ap4evb_lcdc_modes[] = {
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

static const struct sh_mobile_meram_cfg lcd_meram_cfg = {
	.icb[0] = {
		.meram_size     = 0x40,
	},
	.icb[1] = {
		.meram_size     = 0x40,
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.meram_dev = &meram_info,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.lcd_modes = ap4evb_lcdc_modes,
		.num_modes = ARRAY_SIZE(ap4evb_lcdc_modes),
		.meram_cfg = &lcd_meram_cfg,
#ifdef CONFIG_AP4EVB_QHD
		.tx_dev = &mipidsi0_device,
#endif
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

/* FSI */
#define IRQ_FSI		evt2irq(0x1840)
static struct sh_fsi_platform_info fsi_info = {
	.port_b = {
		.flags		= SH_FSI_CLK_CPG |
				  SH_FSI_FMT_SPDIF,
	},
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

static struct asoc_simple_card_info fsi2_ak4643_info = {
	.name		= "AK4643",
	.card		= "FSI2A-AK4643",
	.codec		= "ak4642-codec.0-0013",
	.platform	= "sh_fsi2",
	.daifmt		= SND_SOC_DAIFMT_LEFT_J,
	.cpu_dai = {
		.name	= "fsia-dai",
		.fmt	= SND_SOC_DAIFMT_CBS_CFS,
	},
	.codec_dai = {
		.name	= "ak4642-hifi",
		.fmt	= SND_SOC_DAIFMT_CBM_CFM,
		.sysclk	= 11289600,
	},
};

static struct platform_device fsi_ak4643_device = {
	.name	= "asoc-simple-card",
	.dev	= {
		.platform_data	= &fsi2_ak4643_info,
	},
};

/* LCDC1 */
static long ap4evb_clk_optimize(unsigned long target, unsigned long *best_freq,
				unsigned long *parent_freq);

static struct sh_mobile_hdmi_info hdmi_info = {
	.flags = HDMI_SND_SRC_SPDIF,
	.clk_optimize_parent = ap4evb_clk_optimize,
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

static long ap4evb_clk_optimize(unsigned long target, unsigned long *best_freq,
				unsigned long *parent_freq)
{
	struct clk *hdmi_ick = clk_get(&hdmi_device.dev, "ick");
	long error;

	if (IS_ERR(hdmi_ick)) {
		int ret = PTR_ERR(hdmi_ick);
		pr_err("Cannot get HDMI ICK: %d\n", ret);
		return ret;
	}

	error = clk_round_parent(hdmi_ick, target, best_freq, parent_freq, 1, 64);

	clk_put(hdmi_ick);

	return error;
}

static const struct sh_mobile_meram_cfg hdmi_meram_cfg = {
	.icb[0] = {
		.meram_size     = 0x100,
	},
	.icb[1] = {
		.meram_size     = 0x100,
	},
};

static struct sh_mobile_lcdc_info sh_mobile_lcdc1_info = {
	.clock_source = LCDC_CLK_EXTERNAL,
	.meram_dev = &meram_info,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.interface_type = RGB24,
		.clock_divider = 1,
		.flags = LCDC_FLAGS_DWPOL,
		.meram_cfg = &hdmi_meram_cfg,
		.tx_dev = &hdmi_device,
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
		.start	= intcs_evt2irq(0x1780),
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

static struct asoc_simple_card_info fsi2_hdmi_info = {
	.name		= "HDMI",
	.card		= "FSI2B-HDMI",
	.codec		= "sh-mobile-hdmi",
	.platform	= "sh_fsi2",
	.cpu_dai = {
		.name	= "fsib-dai",
		.fmt	= SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_IB_NF,
	},
	.codec_dai = {
		.name	= "sh_mobile_hdmi-hifi",
	},
};

static struct platform_device fsi_hdmi_device = {
	.name	= "asoc-simple-card",
	.id	= 1,
	.dev	= {
		.platform_data	= &fsi2_hdmi_info,
	},
};

static struct gpio_led ap4evb_leds[] = {
	{
		.name			= "led4",
		.gpio			= 185,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led2",
		.gpio			= 186,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led3",
		.gpio			= 187,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name			= "led1",
		.gpio			= 188,
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

static struct i2c_board_info imx074_info = {
	I2C_BOARD_INFO("imx074", 0x1a),
};

static struct soc_camera_link imx074_link = {
	.bus_id		= 0,
	.board_info	= &imx074_info,
	.i2c_adapter_id	= 0,
	.module_name	= "imx074",
};

static struct platform_device ap4evb_camera = {
	.name   = "soc-camera-pdrv",
	.id     = 0,
	.dev    = {
		.platform_data = &imx074_link,
	},
};

static struct sh_csi2_client_config csi2_clients[] = {
	{
		.phy		= SH_CSI2_PHY_MAIN,
		.lanes		= 0,		/* default: 2 lanes */
		.channel	= 0,
		.pdev		= &ap4evb_camera,
	},
};

static struct sh_csi2_pdata csi2_info = {
	.type		= SH_CSI2C,
	.clients	= csi2_clients,
	.num_clients	= ARRAY_SIZE(csi2_clients),
	.flags		= SH_CSI2_ECC | SH_CSI2_CRC,
};

static struct resource csi2_resources[] = {
	[0] = {
		.name	= "CSI2",
		.start	= 0xffc90000,
		.end	= 0xffc90fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x17a0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_mobile_ceu_companion csi2 = {
	.id		= 0,
	.num_resources	= ARRAY_SIZE(csi2_resources),
	.resource	= csi2_resources,
	.platform_data	= &csi2_info,
};

static struct sh_mobile_ceu_info sh_mobile_ceu_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
	.max_width = 8188,
	.max_height = 8188,
	.csi2 = &csi2,
};

static struct resource ceu_resources[] = {
	[0] = {
		.name	= "CEU",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x880),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu_device = {
	.name		= "sh_mobile_ceu",
	.id             = 0, /* "ceu0" clock */
	.num_resources	= ARRAY_SIZE(ceu_resources),
	.resource	= ceu_resources,
	.dev	= {
		.platform_data		= &sh_mobile_ceu_info,
		.coherent_dma_mask	= 0xffffffff,
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
	&fsi_ak4643_device,
	&fsi_hdmi_device,
	&sh_mmcif_device,
	&hdmi_device,
	&lcdc_device,
	&lcdc1_device,
	&ceu_device,
	&ap4evb_camera,
	&meram_device,
};

static void __init hdmi_init_pm_clock(void)
{
	struct clk *hdmi_ick = clk_get(&hdmi_device.dev, "ick");
	int ret;
	long rate;

	if (IS_ERR(hdmi_ick)) {
		ret = PTR_ERR(hdmi_ick);
		pr_err("Cannot get HDMI ICK: %d\n", ret);
		goto out;
	}

	ret = clk_set_parent(&sh7372_pllc2_clk, &sh7372_dv_clki_div2_clk);
	if (ret < 0) {
		pr_err("Cannot set PLLC2 parent: %d, %d users\n", ret, sh7372_pllc2_clk.usecount);
		goto out;
	}

	pr_debug("PLLC2 initial frequency %lu\n", clk_get_rate(&sh7372_pllc2_clk));

	rate = clk_round_rate(&sh7372_pllc2_clk, 594000000);
	if (rate < 0) {
		pr_err("Cannot get suitable rate: %ld\n", rate);
		ret = rate;
		goto out;
	}

	ret = clk_set_rate(&sh7372_pllc2_clk, rate);
	if (ret < 0) {
		pr_err("Cannot set rate %ld: %d\n", rate, ret);
		goto out;
	}

	pr_debug("PLLC2 set frequency %lu\n", rate);

	ret = clk_set_parent(hdmi_ick, &sh7372_pllc2_clk);
	if (ret < 0)
		pr_err("Cannot set HDMI parent: %d\n", ret);

out:
	if (!IS_ERR(hdmi_ick))
		clk_put(hdmi_ick);
}

/* TouchScreen */
#ifdef CONFIG_AP4EVB_QHD
# define GPIO_TSC_IRQ	GPIO_FN_IRQ28_123
# define GPIO_TSC_PORT	123
#else /* WVGA */
# define GPIO_TSC_IRQ	GPIO_FN_IRQ7_40
# define GPIO_TSC_PORT	40
#endif

#define IRQ28	evt2irq(0x3380) /* IRQ28A */
#define IRQ7	evt2irq(0x02e0) /* IRQ7A */
static int ts_get_pendown_state(void)
{
	int val;

	gpio_free(GPIO_TSC_IRQ);

	gpio_request_one(GPIO_TSC_PORT, GPIOF_IN, NULL);

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


static const struct pinctrl_map ap4evb_pinctrl_map[] = {
	/* CEU */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-sh7372",
				  "ceu_clk_0", "ceu"),
	/* FSIA (AK4643) */
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-sh7372",
				  "fsia_sclk_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-sh7372",
				  "fsia_data_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-sh7372",
				  "fsia_data_out", "fsia"),
	/* FSIB (HDMI) */
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.1", "pfc-sh7372",
				  "fsib_mclk_in", "fsib"),
	/* HDMI */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-mobile-hdmi", "pfc-sh7372",
				  "hdmi", "hdmi"),
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh7372",
				  "mmc0_data8_0", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh7372",
				  "mmc0_ctrl_0", "mmc0"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_wp", "sdhi0"),
	/* SDHI1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh7372",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh7372",
				  "sdhi1_ctrl", "sdhi1"),
};

#define GPIO_PORT9CR	IOMEM(0xE6051009)
#define GPIO_PORT10CR	IOMEM(0xE605100A)
#define USCCR1		IOMEM(0xE6058144)
static void __init ap4evb_init(void)
{
	struct pm_domain_device domain_devices[] = {
		{ "A4LC", &lcdc1_device, },
		{ "A4LC", &lcdc_device, },
		{ "A4MP", &fsi_device, },
		{ "A3SP", &sh_mmcif_device, },
		{ "A3SP", &sdhi0_device, },
		{ "A3SP", &sdhi1_device, },
		{ "A4R", &ceu_device, },
	};
	u32 srcr4;
	struct clk *clk;

	regulator_register_always_on(0, "fixed-1.8V", fixed1v8_power_consumers,
				     ARRAY_SIZE(fixed1v8_power_consumers), 1800000);
	regulator_register_always_on(1, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	regulator_register_fixed(2, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	/* External clock source */
	clk_set_rate(&sh7372_dv_clki_clk, 27000000);

	pinctrl_register_mappings(ap4evb_pinctrl_map,
				  ARRAY_SIZE(ap4evb_pinctrl_map));
	sh7372_pinmux_init();

	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIFA0_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA0_RXD, NULL);

	/* enable SMSC911X */
	gpio_request(GPIO_FN_CS5A,	NULL);
	gpio_request(GPIO_FN_IRQ6_39,	NULL);

	/* enable Debug switch (S6) */
	gpio_request_one(32, GPIOF_IN | GPIOF_EXPORT, NULL);
	gpio_request_one(33, GPIOF_IN | GPIOF_EXPORT, NULL);
	gpio_request_one(34, GPIOF_IN | GPIOF_EXPORT, NULL);
	gpio_request_one(35, GPIOF_IN | GPIOF_EXPORT, NULL);

	/* USB enable */
	gpio_request(GPIO_FN_VBUS0_1,    NULL);
	gpio_request(GPIO_FN_IDIN_1_18,  NULL);
	gpio_request(GPIO_FN_PWEN_1_115, NULL);
	gpio_request(GPIO_FN_OVCN_1_114, NULL);
	gpio_request(GPIO_FN_EXTLP_1,    NULL);
	gpio_request(GPIO_FN_OVCN2_1,    NULL);

	/* setup USB phy */
	__raw_writew(0x8a0a, IOMEM(0xE6058130));	/* USBCR4 */

	/* FSI2 port A (ak4643) */
	gpio_request_one(161, GPIOF_OUT_INIT_LOW, NULL); /* slave */

	gpio_request(9, NULL);
	gpio_request(10, NULL);
	gpio_direction_none(GPIO_PORT9CR);  /* FSIAOBT needs no direction */
	gpio_direction_none(GPIO_PORT10CR); /* FSIAOLR needs no direction */

	/* card detect pin for MMC slot (CN7) */
	gpio_request_one(41, GPIOF_IN, NULL);

	/* FSI2 port B (HDMI) */
	__raw_writew(__raw_readw(USCCR1) & ~(1 << 6), USCCR1); /* use SPDIF */

	/* set SPU2 clock to 119.6 MHz */
	clk = clk_get(NULL, "spu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 119600000));
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
	irq_set_irq_type(IRQ28, IRQ_TYPE_LEVEL_LOW);

	tsc_device.irq = IRQ28;
	i2c_register_board_info(1, &tsc_device, 1);

	/* LCDC0 */
	lcdc_info.clock_source			= LCDC_CLK_PERIPHERAL;
	lcdc_info.ch[0].interface_type		= RGB24;
	lcdc_info.ch[0].clock_divider		= 1;
	lcdc_info.ch[0].flags			= LCDC_FLAGS_DWPOL;
	lcdc_info.ch[0].panel_cfg.width		= 44;
	lcdc_info.ch[0].panel_cfg.height	= 79;

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

	gpio_request_one(189, GPIOF_OUT_INIT_HIGH, NULL); /* backlight */
	gpio_request_one(151, GPIOF_OUT_INIT_HIGH, NULL); /* LCDDON */

	lcdc_info.clock_source			= LCDC_CLK_BUS;
	lcdc_info.ch[0].interface_type		= RGB18;
	lcdc_info.ch[0].clock_divider		= 3;
	lcdc_info.ch[0].flags			= 0;
	lcdc_info.ch[0].panel_cfg.width		= 152;
	lcdc_info.ch[0].panel_cfg.height	= 91;

	/* enable TouchScreen */
	irq_set_irq_type(IRQ7, IRQ_TYPE_LEVEL_LOW);

	tsc_device.irq = IRQ7;
	i2c_register_board_info(0, &tsc_device, 1);
#endif /* CONFIG_AP4EVB_QHD */

	/* CEU */

	/*
	 * TODO: reserve memory for V4L2 DMA buffers, when a suitable API
	 * becomes available
	 */

	/* MIPI-CSI stuff */
	clk = clk_get(NULL, "vck1_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 13000000));
		clk_enable(clk);
		clk_put(clk);
	}

	sh7372_add_standard_devices();

	/* Reset HDMI, must be held at least one EXTALR (32768Hz) period */
#define SRCR4 IOMEM(0xe61580bc)
	srcr4 = __raw_readl(SRCR4);
	__raw_writel(srcr4 | (1 << 13), SRCR4);
	udelay(50);
	__raw_writel(srcr4 & ~(1 << 13), SRCR4);

	platform_add_devices(ap4evb_devices, ARRAY_SIZE(ap4evb_devices));

	rmobile_add_devices_to_domains(domain_devices,
				       ARRAY_SIZE(domain_devices));

	hdmi_init_pm_clock();
	sh7372_pm_init();
	pm_clk_add(&fsi_device.dev, "spu2");
	pm_clk_add(&lcdc1_device.dev, "hdmi");
}

MACHINE_START(AP4EVB, "ap4evb")
	.map_io		= sh7372_map_io,
	.init_early	= sh7372_add_early_devices,
	.init_irq	= sh7372_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= ap4evb_init,
	.init_late	= sh7372_pm_init_late,
	.init_time	= sh7372_earlytimer_init,
MACHINE_END
