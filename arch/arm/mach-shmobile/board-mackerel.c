/*
 * mackerel board support
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * based on ap4evb
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
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mfd/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/smsc911x.h>
#include <linux/sh_intc.h>
#include <linux/tca6416_keypad.h>
#include <linux/usb/r8a66597.h>

#include <video/sh_mobile_hdmi.h>
#include <video/sh_mobile_lcdc.h>
#include <media/sh_mobile_ceu.h>
#include <media/soc_camera.h>
#include <media/soc_camera_platform.h>
#include <sound/sh_fsi.h>

#include <mach/common.h>
#include <mach/sh7372.h>

#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>

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
 * CPU mode
 *
 * SW4                                     | Boot Area| Master   | Remarks
 *  1  | 2   | 3   | 4   | 5   | 6   | 8   |          | Processor|
 * ----+-----+-----+-----+-----+-----+-----+----------+----------+--------------
 * ON  | ON  | OFF | ON  | ON  | OFF | OFF | External | System   | External ROM
 * ON  | ON  | ON  | ON  | ON  | OFF | OFF | External | System   | ROM Debug
 * ON  | ON  | X   | ON  | OFF | OFF | OFF | Built-in | System   | ROM Debug
 * X   | OFF | X   | X   | X   | X   | OFF | Built-in | System   | MaskROM
 * OFF | X   | X   | X   | X   | X   | OFF | Built-in | System   | MaskROM
 * X   | X   | X   | OFF | X   | X   | OFF | Built-in | System   | MaskROM
 * OFF | ON  | OFF | X   | X   | OFF | ON  | External | System   | Standalone
 * ON  | OFF | OFF | X   | X   | OFF | ON  | External | Realtime | Standalone
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
 * SMSC 9220
 *
 *  SW1		SMSC 9220
 * -----------------------
 *  ON		access disable
 *  OFF		access enable
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
 * External interrupt pin settings
 *
 * IRQX  | pin setting        | device             | level
 * ------+--------------------+--------------------+-------
 * IRQ0  | ICR1A.IRQ0SA=0010  | SDHI2 card detect  | Low
 * IRQ6  | ICR1A.IRQ6SA=0011  | Ether(LAN9220)     | High
 * IRQ7  | ICR1A.IRQ7SA=0010  | LCD Tuch Panel     | Low
 * IRQ8  | ICR2A.IRQ8SA=0010  | MMC/SD card detect | Low
 * IRQ9  | ICR2A.IRQ9SA=0010  | KEY(TCA6408)       | Low
 * IRQ21 | ICR4A.IRQ21SA=0011 | Sensor(ADXL345)    | High
 * IRQ22 | ICR4A.IRQ22SA=0011 | Sensor(AK8975)     | High
 */

/*
 * USB
 *
 * USB0 : CN22 : Function
 * USB1 : CN31 : Function/Host *1
 *
 * J30 (for CN31) *1
 * ----------+---------------+-------------
 * 1-2 short | VBUS 5V       | Host
 * open      | external VBUS | Function
 *
 * *1
 * CN31 is used as Host in Linux.
 */

/*
 * SDHI0 (CN12)
 *
 * SW56 : OFF
 *
 */

/* MMC /SDHI1 (CN7)
 *
 * I/O voltage : 1.8v
 *
 * Power voltage : 1.8v or 3.3v
 *  J22 : select power voltage *1
 *	1-2 pin : 1.8v
 *	2-3 pin : 3.3v
 *
 * *1
 * Please change J22 depends the card to be used.
 * MMC's OCR field set to support either voltage for the card inserted.
 *
 *	SW1	|	SW33
 *		| bit1 | bit2 | bit3 | bit4
 * -------------+------+------+------+-------
 * MMC0	  OFF	|  OFF |  ON  |  ON  |  X
 * MMC1	  ON	|  OFF |  ON  |  X   | ON
 * SDHI1  OFF	|  ON  |   X  |  OFF | ON
 *
 */

/*
 * SDHI2 (CN23)
 *
 * microSD card sloct
 *
 */

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

/* SMSC */
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

/* LCDC */
static struct fb_videomode mackerel_lcdc_modes[] = {
	{
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
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.clock_source = LCDC_CLK_BUS,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.lcd_cfg = mackerel_lcdc_modes,
		.num_cfg = ARRAY_SIZE(mackerel_lcdc_modes),
		.interface_type		= RGB24,
		.clock_divider		= 2,
		.flags			= 0,
		.lcd_size_cfg.width	= 152,
		.lcd_size_cfg.height	= 91,
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
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

/* HDMI */
static struct sh_mobile_lcdc_info hdmi_lcdc_info = {
	.clock_source = LCDC_CLK_EXTERNAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.interface_type = RGB24,
		.clock_divider = 1,
		.flags = LCDC_FLAGS_DWPOL,
	}
};

static struct resource hdmi_lcdc_resources[] = {
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

static struct platform_device hdmi_lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(hdmi_lcdc_resources),
	.resource	= hdmi_lcdc_resources,
	.id		= 1,
	.dev	= {
		.platform_data	= &hdmi_lcdc_info,
		.coherent_dma_mask = ~0,
	},
};

static struct sh_mobile_hdmi_info hdmi_info = {
	.lcd_chan	= &hdmi_lcdc_info.ch[0],
	.lcd_dev	= &hdmi_lcdc_device.dev,
	.flags		= HDMI_SND_SRC_SPDIF,
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

	ret = clk_set_parent(&sh7372_pllc2_clk, &sh7372_dv_clki_div2_clk);
	if (ret < 0) {
		pr_err("Cannot set PLLC2 parent: %d, %d users\n",
		       ret, sh7372_pllc2_clk.usecount);
		goto out;
	}

	pr_debug("PLLC2 initial frequency %lu\n",
		 clk_get_rate(&sh7372_pllc2_clk));

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

	ret = clk_enable(&sh7372_pllc2_clk);
	if (ret < 0) {
		pr_err("Cannot enable pllc2 clock\n");
		goto out;
	}

	pr_debug("PLLC2 set frequency %lu\n", rate);

	ret = clk_set_parent(hdmi_ick, &sh7372_pllc2_clk);
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

/* USB1 (Host) */
static void usb1_host_port_power(int port, int power)
{
	if (!power) /* only power-on is supported for now */
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

/* LED */
static struct gpio_led mackerel_leds[] = {
	{
		.name		= "led0",
		.gpio		= GPIO_PORT0,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led1",
		.gpio		= GPIO_PORT1,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led2",
		.gpio		= GPIO_PORT2,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led3",
		.gpio		= GPIO_PORT159,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}
};

static struct gpio_led_platform_data mackerel_leds_pdata = {
	.leds = mackerel_leds,
	.num_leds = ARRAY_SIZE(mackerel_leds),
};

static struct platform_device leds_device = {
	.name = "leds-gpio",
	.id = 0,
	.dev = {
		.platform_data  = &mackerel_leds_pdata,
	},
};

/* FSI */
#define IRQ_FSI evt2irq(0x1840)
static int __fsi_set_round_rate(struct clk *clk, long rate, int enable)
{
	int ret;

	if (rate <= 0)
		return 0;

	if (!enable) {
		clk_disable(clk);
		return 0;
	}

	ret = clk_set_rate(clk, clk_round_rate(clk, rate));
	if (ret < 0)
		return ret;

	return clk_enable(clk);
}

static int fsi_set_rate(struct device *dev, int is_porta, int rate, int enable)
{
	struct clk *fsib_clk;
	struct clk *fdiv_clk = &sh7372_fsidivb_clk;
	long fsib_rate = 0;
	long fdiv_rate = 0;
	int ackmd_bpfmd;
	int ret;

	/* FSIA is slave mode. nothing to do here */
	if (is_porta)
		return 0;

	/* clock start */
	switch (rate) {
	case 44100:
		fsib_rate	= rate * 256;
		ackmd_bpfmd	= SH_FSI_ACKMD_256 | SH_FSI_BPFMD_64;
		break;
	case 48000:
		fsib_rate	= 85428000; /* around 48kHz x 256 x 7 */
		fdiv_rate	= rate * 256;
		ackmd_bpfmd	= SH_FSI_ACKMD_256 | SH_FSI_BPFMD_64;
		break;
	default:
		pr_err("unsupported rate in FSI2 port B\n");
		return -EINVAL;
	}

	/* FSI B setting */
	fsib_clk = clk_get(dev, "ickb");
	if (IS_ERR(fsib_clk))
		return -EIO;

	/* fsib */
	ret = __fsi_set_round_rate(fsib_clk, fsib_rate, enable);
	if (ret < 0)
		goto fsi_set_rate_end;

	/* FSI DIV */
	ret = __fsi_set_round_rate(fdiv_clk, fdiv_rate, enable);
	if (ret < 0) {
		/* disable FSI B */
		if (enable)
			__fsi_set_round_rate(fsib_clk, fsib_rate, 0);
		goto fsi_set_rate_end;
	}

	ret = ackmd_bpfmd;

fsi_set_rate_end:
	clk_put(fsib_clk);
	return ret;
}

static struct sh_fsi_platform_info fsi_info = {
	.porta_flags =	SH_FSI_BRS_INV		|
			SH_FSI_OUT_SLAVE_MODE	|
			SH_FSI_IN_SLAVE_MODE	|
			SH_FSI_OFMT(PCM)	|
			SH_FSI_IFMT(PCM),

	.portb_flags =	SH_FSI_BRS_INV	|
			SH_FSI_BRM_INV	|
			SH_FSI_LRS_INV	|
			SH_FSI_OFMT(SPDIF),

	.set_rate = fsi_set_rate,
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

static struct platform_device fsi_ak4643_device = {
	.name		= "sh_fsi2_a_ak4643",
};

/*
 * The card detect pin of the top SD/MMC slot (CN7) is active low and is
 * connected to GPIO A22 of SH7372 (GPIO_PORT41).
 */
static int slot_cn7_get_cd(struct platform_device *pdev)
{
	if (gpio_is_valid(GPIO_PORT41))
		return !gpio_get_value(GPIO_PORT41);
	else
		return -ENXIO;
}

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start	= 0xe6850000,
		.end	= 0xe68501ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0e00) /* SDHI0 */,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name		= "sh_mobile_sdhi",
	.num_resources	= ARRAY_SIZE(sdhi0_resources),
	.resource	= sdhi0_resources,
	.id		= 0,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

#if !defined(CONFIG_MMC_SH_MMCIF)
/* SDHI1 */
static struct sh_mobile_sdhi_info sdhi1_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.tmio_ocr_mask	= MMC_VDD_165_195,
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED |
			  MMC_CAP_NEEDS_POLL,
	.get_cd		= slot_cn7_get_cd,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start	= 0xe6860000,
		.end	= 0xe68601ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0e80),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name		= "sh_mobile_sdhi",
	.num_resources	= ARRAY_SIZE(sdhi1_resources),
	.resource	= sdhi1_resources,
	.id		= 1,
	.dev	= {
		.platform_data	= &sdhi1_info,
	},
};
#endif

/* SDHI2 */
static struct sh_mobile_sdhi_info sdhi2_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI2_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI2_RX,
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED |
			  MMC_CAP_NEEDS_POLL,
};

static struct resource sdhi2_resources[] = {
	[0] = {
		.name	= "SDHI2",
		.start	= 0xe6870000,
		.end	= 0xe68701ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x1200),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi2_device = {
	.name	= "sh_mobile_sdhi",
	.num_resources	= ARRAY_SIZE(sdhi2_resources),
	.resource	= sdhi2_resources,
	.id		= 2,
	.dev	= {
		.platform_data	= &sdhi2_info,
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


static int mackerel_camera_add(struct soc_camera_link *icl, struct device *dev);
static void mackerel_camera_del(struct soc_camera_link *icl);

static int camera_set_capture(struct soc_camera_platform_info *info,
			      int enable)
{
	return 0; /* camera sensor always enabled */
}

static struct soc_camera_platform_info camera_info = {
	.format_name = "UYVY",
	.format_depth = 16,
	.format = {
		.code = V4L2_MBUS_FMT_UYVY8_2X8,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.field = V4L2_FIELD_NONE,
		.width = 640,
		.height = 480,
	},
	.bus_param = SOCAM_PCLK_SAMPLE_RISING | SOCAM_HSYNC_ACTIVE_HIGH |
	SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_MASTER | SOCAM_DATAWIDTH_8 |
	SOCAM_DATA_ACTIVE_HIGH,
	.set_capture = camera_set_capture,
};

static struct soc_camera_link camera_link = {
	.bus_id		= 0,
	.add_device	= mackerel_camera_add,
	.del_device	= mackerel_camera_del,
	.module_name	= "soc_camera_platform",
	.priv		= &camera_info,
};

static void dummy_release(struct device *dev)
{
}

static struct platform_device camera_device = {
	.name		= "soc_camera_platform",
	.dev		= {
		.platform_data	= &camera_info,
		.release	= dummy_release,
	},
};

static int mackerel_camera_add(struct soc_camera_link *icl,
			       struct device *dev)
{
	if (icl != &camera_link)
		return -ENODEV;

	camera_info.dev = dev;

	return platform_device_register(&camera_device);
}

static void mackerel_camera_del(struct soc_camera_link *icl)
{
	if (icl != &camera_link)
		return;

	platform_device_unregister(&camera_device);
	memset(&camera_device.dev.kobj, 0,
	       sizeof(camera_device.dev.kobj));
}

static struct sh_mobile_ceu_info sh_mobile_ceu_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu_resources[] = {
	[0] = {
		.name	= "CEU",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = intcs_evt2irq(0x880),
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
	.dev		= {
		.platform_data	= &sh_mobile_ceu_info,
	},
};

static struct platform_device mackerel_camera = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &camera_link,
	},
};

static struct platform_device *mackerel_devices[] __initdata = {
	&nor_flash_device,
	&smc911x_device,
	&lcdc_device,
	&usb1_host_device,
	&leds_device,
	&fsi_device,
	&fsi_ak4643_device,
	&sdhi0_device,
#if !defined(CONFIG_MMC_SH_MMCIF)
	&sdhi1_device,
#endif
	&sdhi2_device,
	&sh_mmcif_device,
	&ceu_device,
	&mackerel_camera,
	&hdmi_lcdc_device,
	&hdmi_device,
};

/* Keypad Initialization */
#define KEYPAD_BUTTON(ev_type, ev_code, act_low) \
{								\
	.type		= ev_type,				\
	.code		= ev_code,				\
	.active_low	= act_low,				\
}

#define KEYPAD_BUTTON_LOW(event_code) KEYPAD_BUTTON(EV_KEY, event_code, 1)

static struct tca6416_button mackerel_gpio_keys[] = {
	KEYPAD_BUTTON_LOW(KEY_HOME),
	KEYPAD_BUTTON_LOW(KEY_MENU),
	KEYPAD_BUTTON_LOW(KEY_BACK),
	KEYPAD_BUTTON_LOW(KEY_POWER),
};

static struct tca6416_keys_platform_data mackerel_tca6416_keys_info = {
	.buttons	= mackerel_gpio_keys,
	.nbuttons	= ARRAY_SIZE(mackerel_gpio_keys),
	.rep		= 1,
	.use_polling	= 0,
	.pinmask	= 0x000F,
};

/* I2C */
#define IRQ9 evt2irq(0x0320)

static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("ak4643", 0x13),
	},
	/* Keypad */
	{
		I2C_BOARD_INFO("tca6408-keys", 0x20),
		.platform_data = &mackerel_tca6416_keys_info,
		.irq = IRQ9,
	},
};

#define IRQ21 evt2irq(0x32a0)

static struct i2c_board_info i2c1_devices[] = {
	/* Accelerometer */
	{
		I2C_BOARD_INFO("adxl34x", 0x53),
		.irq = IRQ21,
	},
};

static struct map_desc mackerel_io_desc[] __initdata = {
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

static void __init mackerel_map_io(void)
{
	iotable_init(mackerel_io_desc, ARRAY_SIZE(mackerel_io_desc));

	/* setup early devices and console here as well */
	sh7372_add_early_devices();
	shmobile_setup_console();
}

#define GPIO_PORT9CR	0xE6051009
#define GPIO_PORT10CR	0xE605100A
#define SRCR4		0xe61580bc
#define USCCR1		0xE6058144
static void __init mackerel_init(void)
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

	/* LCDC */
	gpio_request(GPIO_FN_LCDD23,   NULL);
	gpio_request(GPIO_FN_LCDD22,   NULL);
	gpio_request(GPIO_FN_LCDD21,   NULL);
	gpio_request(GPIO_FN_LCDD20,   NULL);
	gpio_request(GPIO_FN_LCDD19,   NULL);
	gpio_request(GPIO_FN_LCDD18,   NULL);
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

	gpio_request(GPIO_PORT31, NULL); /* backlight */
	gpio_direction_output(GPIO_PORT31, 1);

	gpio_request(GPIO_PORT151, NULL); /* LCDDON */
	gpio_direction_output(GPIO_PORT151, 1);

	/* USB enable */
	gpio_request(GPIO_FN_VBUS0_1,    NULL);
	gpio_request(GPIO_FN_IDIN_1_18,  NULL);
	gpio_request(GPIO_FN_PWEN_1_115, NULL);
	gpio_request(GPIO_FN_OVCN_1_114, NULL);
	gpio_request(GPIO_FN_EXTLP_1,    NULL);
	gpio_request(GPIO_FN_OVCN2_1,    NULL);

	/* setup USB phy */
	__raw_writew(0x8a0a, 0xE6058130);	/* USBCR4 */

	/* enable FSI2 port A (ak4643) */
	gpio_request(GPIO_FN_FSIAIBT,	NULL);
	gpio_request(GPIO_FN_FSIAILR,	NULL);
	gpio_request(GPIO_FN_FSIAISLD,	NULL);
	gpio_request(GPIO_FN_FSIAOSLD,	NULL);
	gpio_request(GPIO_PORT161,	NULL);
	gpio_direction_output(GPIO_PORT161, 0); /* slave */

	gpio_request(GPIO_PORT9,  NULL);
	gpio_request(GPIO_PORT10, NULL);
	gpio_no_direction(GPIO_PORT9CR);  /* FSIAOBT needs no direction */
	gpio_no_direction(GPIO_PORT10CR); /* FSIAOLR needs no direction */

	intc_set_priority(IRQ_FSI, 3); /* irq priority FSI(3) > SMSC911X(2) */

	/* setup FSI2 port B (HDMI) */
	gpio_request(GPIO_FN_FSIBCK, NULL);
	__raw_writew(__raw_readw(USCCR1) & ~(1 << 6), USCCR1); /* use SPDIF */

	/* set SPU2 clock to 119.6 MHz */
	clk = clk_get(NULL, "spu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 119600000));
		clk_put(clk);
	}

	/* enable Keypad */
	gpio_request(GPIO_FN_IRQ9_42,	NULL);
	set_irq_type(IRQ9, IRQ_TYPE_LEVEL_HIGH);

	/* enable Accelerometer */
	gpio_request(GPIO_FN_IRQ21,	NULL);
	set_irq_type(IRQ21, IRQ_TYPE_LEVEL_HIGH);

	/* enable SDHI0 */
	gpio_request(GPIO_FN_SDHICD0, NULL);
	gpio_request(GPIO_FN_SDHIWP0, NULL);
	gpio_request(GPIO_FN_SDHICMD0, NULL);
	gpio_request(GPIO_FN_SDHICLK0, NULL);
	gpio_request(GPIO_FN_SDHID0_3, NULL);
	gpio_request(GPIO_FN_SDHID0_2, NULL);
	gpio_request(GPIO_FN_SDHID0_1, NULL);
	gpio_request(GPIO_FN_SDHID0_0, NULL);

#if !defined(CONFIG_MMC_SH_MMCIF)
	/* enable SDHI1 */
	gpio_request(GPIO_FN_SDHICMD1, NULL);
	gpio_request(GPIO_FN_SDHICLK1, NULL);
	gpio_request(GPIO_FN_SDHID1_3, NULL);
	gpio_request(GPIO_FN_SDHID1_2, NULL);
	gpio_request(GPIO_FN_SDHID1_1, NULL);
	gpio_request(GPIO_FN_SDHID1_0, NULL);
#endif
	/* card detect pin for MMC slot (CN7) */
	gpio_request(GPIO_PORT41, NULL);
	gpio_direction_input(GPIO_PORT41);

	/* enable SDHI2 */
	gpio_request(GPIO_FN_SDHICMD2, NULL);
	gpio_request(GPIO_FN_SDHICLK2, NULL);
	gpio_request(GPIO_FN_SDHID2_3, NULL);
	gpio_request(GPIO_FN_SDHID2_2, NULL);
	gpio_request(GPIO_FN_SDHID2_1, NULL);
	gpio_request(GPIO_FN_SDHID2_0, NULL);

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

	/* enable GPS module (GT-720F) */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);

	/* CEU */
	gpio_request(GPIO_FN_VIO_CLK, NULL);
	gpio_request(GPIO_FN_VIO_VD, NULL);
	gpio_request(GPIO_FN_VIO_HD, NULL);
	gpio_request(GPIO_FN_VIO_FIELD, NULL);
	gpio_request(GPIO_FN_VIO_CKO, NULL);
	gpio_request(GPIO_FN_VIO_D7, NULL);
	gpio_request(GPIO_FN_VIO_D6, NULL);
	gpio_request(GPIO_FN_VIO_D5, NULL);
	gpio_request(GPIO_FN_VIO_D4, NULL);
	gpio_request(GPIO_FN_VIO_D3, NULL);
	gpio_request(GPIO_FN_VIO_D2, NULL);
	gpio_request(GPIO_FN_VIO_D1, NULL);
	gpio_request(GPIO_FN_VIO_D0, NULL);

	/* HDMI */
	gpio_request(GPIO_FN_HDMI_HPD, NULL);
	gpio_request(GPIO_FN_HDMI_CEC, NULL);

	/* Reset HDMI, must be held at least one EXTALR (32768Hz) period */
	srcr4 = __raw_readl(SRCR4);
	__raw_writel(srcr4 | (1 << 13), SRCR4);
	udelay(50);
	__raw_writel(srcr4 & ~(1 << 13), SRCR4);

	i2c_register_board_info(0, i2c0_devices,
				ARRAY_SIZE(i2c0_devices));
	i2c_register_board_info(1, i2c1_devices,
				ARRAY_SIZE(i2c1_devices));

	sh7372_add_standard_devices();

	platform_add_devices(mackerel_devices, ARRAY_SIZE(mackerel_devices));
}

static void __init mackerel_timer_init(void)
{
	sh7372_clock_init();
	shmobile_timer.init();

	/* External clock source */
	clk_set_rate(&sh7372_dv_clki_clk, 27000000);
}

static struct sys_timer mackerel_timer = {
	.init		= mackerel_timer_init,
};

MACHINE_START(MACKEREL, "mackerel")
	.map_io		= mackerel_map_io,
	.init_irq	= sh7372_init_irq,
	.init_machine	= mackerel_init,
	.timer		= &mackerel_timer,
MACHINE_END
