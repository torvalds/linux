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
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/sh_flctl.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_data/gpio_backlight.h>
#include <linux/pm_clock.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/sh_intc.h>
#include <linux/tca6416_keypad.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/dma-mapping.h>
#include <video/sh_mobile_hdmi.h>
#include <video/sh_mobile_lcdc.h>
#include <media/sh_mobile_ceu.h>
#include <media/soc_camera.h>
#include <media/soc_camera_platform.h>
#include <sound/sh_fsi.h>
#include <sound/simple_card.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/sh7372.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

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
 * IRQ7  | ICR1A.IRQ7SA=0010  | LCD Touch Panel    | Low
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
 * CAUTION
 *
 * renesas_usbhs driver can use external interrupt mode
 * (which come from USB-PHY) or autonomy mode (it use own interrupt)
 * for detecting connection/disconnection when Function.
 * USB will be power OFF while it has been disconnecting
 * if external interrupt mode, and it is always power ON if autonomy mode,
 *
 * mackerel can not use external interrupt (IRQ7-PORT167) mode on "USB0",
 * because Touchscreen is using IRQ7-PORT40.
 * It is impossible to use IRQ7 demux on this board.
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
 * MMC0   OFF	|  OFF |   X  |  ON  |  X       (Use MMCIF)
 * SDHI1  OFF	|  ON  |   X  |  OFF |  X       (Use MFD_SH_MOBILE_SDHI)
 *
 */

/*
 * SDHI2 (CN23)
 *
 * microSD card sloct
 *
 */

/*
 * FSI - AK4642
 *
 * it needs amixer settings for playing
 *
 * amixer set "Headphone Enable" on
 */

/* Fixed 3.3V and 1.8V regulators to be used by multiple devices */
static struct regulator_consumer_supply fixed1v8_power_consumers[] =
{
	/*
	 * J22 on mackerel switches mmcif.0 and sdhi.1 between 1.8V and 3.3V
	 * Since we cannot support both voltages, we support the default 1.8V
	 */
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
};

static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.2"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.2"),
};

/* Dummy supplies, where voltage doesn't matter */
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

/* MERAM */
static struct sh_mobile_meram_info mackerel_meram_info = {
	.addr_mode	= SH_MOBILE_MERAM_MODE1,
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
	.name		= "sh_mobile_meram",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(meram_resources),
	.resource	= meram_resources,
	.dev		= {
		.platform_data = &mackerel_meram_info,
	},
};

/* LCDC and backlight */
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

static const struct sh_mobile_meram_cfg lcd_meram_cfg = {
	.icb[0] = {
		.meram_size     = 0x40,
	},
	.icb[1] = {
		.meram_size     = 0x40,
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.meram_dev = &mackerel_meram_info,
	.clock_source = LCDC_CLK_BUS,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.lcd_modes = mackerel_lcdc_modes,
		.num_modes = ARRAY_SIZE(mackerel_lcdc_modes),
		.interface_type		= RGB24,
		.clock_divider		= 3,
		.flags			= 0,
		.panel_cfg = {
			.width		= 152,
			.height		= 91,
		},
		.meram_cfg = &lcd_meram_cfg,
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
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct gpio_backlight_platform_data gpio_backlight_data = {
	.fbdev = &lcdc_device.dev,
	.gpio = 31,
	.def_value = 1,
	.name = "backlight",
};

static struct platform_device gpio_backlight_device = {
	.name = "gpio-backlight",
	.dev = {
		.platform_data = &gpio_backlight_data,
	},
};

/* HDMI */
static struct sh_mobile_hdmi_info hdmi_info = {
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

static const struct sh_mobile_meram_cfg hdmi_meram_cfg = {
	.icb[0] = {
		.meram_size     = 0x100,
	},
	.icb[1] = {
		.meram_size     = 0x100,
	},
};

static struct sh_mobile_lcdc_info hdmi_lcdc_info = {
	.meram_dev = &mackerel_meram_info,
	.clock_source = LCDC_CLK_EXTERNAL,
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
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct asoc_simple_card_info fsi2_hdmi_info = {
	.name		= "HDMI",
	.card		= "FSI2B-HDMI",
	.codec		= "sh-mobile-hdmi",
	.platform	= "sh_fsi2",
	.fmt		= SND_SOC_DAIFMT_CBS_CFS,
	.cpu_dai = {
		.name	= "fsib-dai",
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
		pr_err("Cannot set PLLC2 parent: %d, %d users\n",
		       ret, sh7372_pllc2_clk.usecount);
		goto out;
	}

	pr_debug("PLLC2 initial frequency %lu\n",
		 clk_get_rate(&sh7372_pllc2_clk));

	rate = clk_round_rate(&sh7372_pllc2_clk, 594000000);
	if (rate <= 0) {
		pr_err("Cannot get suitable rate: %ld\n", rate);
		ret = -EINVAL;
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

/* USBHS0 is connected to CN22 which takes a USB Mini-B plug
 *
 * The sh7372 SoC has IRQ7 set aside for USBHS0 hotplug,
 * but on this particular board IRQ7 is already used by
 * the touch screen. This leaves us with software polling.
 */
#define USBHS0_POLL_INTERVAL (HZ * 5)

struct usbhs_private {
	void __iomem *usbphyaddr;
	void __iomem *usbcrcaddr;
	struct renesas_usbhs_platform_info info;
	struct delayed_work work;
	struct platform_device *pdev;
};

#define usbhs_get_priv(pdev)				\
	container_of(renesas_usbhs_get_info(pdev),	\
		     struct usbhs_private, info)

#define usbhs_is_connected(priv)			\
	(!((1 << 7) & __raw_readw(priv->usbcrcaddr)))

static int usbhs_get_vbus(struct platform_device *pdev)
{
	return usbhs_is_connected(usbhs_get_priv(pdev));
}

static int usbhs_phy_reset(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	/* init phy */
	__raw_writew(0x8a0a, priv->usbcrcaddr);

	return 0;
}

static int usbhs0_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

static void usbhs0_work_function(struct work_struct *work)
{
	struct usbhs_private *priv = container_of(work, struct usbhs_private,
						  work.work);

	renesas_usbhs_call_notify_hotplug(priv->pdev);
	schedule_delayed_work(&priv->work, USBHS0_POLL_INTERVAL);
}

static int usbhs0_hardware_init(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	priv->pdev = pdev;
	INIT_DELAYED_WORK(&priv->work, usbhs0_work_function);
	schedule_delayed_work(&priv->work, USBHS0_POLL_INTERVAL);
	return 0;
}

static int usbhs0_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	cancel_delayed_work_sync(&priv->work);

	return 0;
}

static struct usbhs_private usbhs0_private = {
	.usbcrcaddr	= IOMEM(0xe605810c),		/* USBCR2 */
	.info = {
		.platform_callback = {
			.hardware_init	= usbhs0_hardware_init,
			.hardware_exit	= usbhs0_hardware_exit,
			.phy_reset	= usbhs_phy_reset,
			.get_id		= usbhs0_get_id,
			.get_vbus	= usbhs_get_vbus,
		},
		.driver_param = {
			.buswait_bwait	= 4,
			.d0_tx_id	= SHDMA_SLAVE_USB0_TX,
			.d1_rx_id	= SHDMA_SLAVE_USB0_RX,
		},
	},
};

static struct resource usbhs0_resources[] = {
	[0] = {
		.name	= "USBHS0",
		.start	= 0xe6890000,
		.end	= 0xe68900e6 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x1ca0) /* USB0_USB0I0 */,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbhs0_device = {
	.name	= "renesas_usbhs",
	.id	= 0,
	.dev = {
		.platform_data		= &usbhs0_private.info,
	},
	.num_resources	= ARRAY_SIZE(usbhs0_resources),
	.resource	= usbhs0_resources,
};

/* USBHS1 is connected to CN31 which takes a USB Mini-AB plug
 *
 * Use J30 to select between Host and Function. This setting
 * can however not be detected by software. Hotplug of USBHS1
 * is provided via IRQ8.
 *
 * Current USB1 works as "USB Host".
 *  - set J30 "short"
 *
 * If you want to use it as "USB gadget",
 *  - J30 "open"
 *  - modify usbhs1_get_id() USBHS_HOST -> USBHS_GADGET
 *  - add .get_vbus = usbhs_get_vbus in usbhs1_private
 *  - check usbhs0_device(pio)/usbhs1_device(irq) order in mackerel_devices.
 */
#define IRQ8 evt2irq(0x0300)
#define USB_PHY_MODE		(1 << 4)
#define USB_PHY_INT_EN		((1 << 3) | (1 << 2))
#define USB_PHY_ON		(1 << 1)
#define USB_PHY_OFF		(1 << 0)
#define USB_PHY_INT_CLR		(USB_PHY_ON | USB_PHY_OFF)

static irqreturn_t usbhs1_interrupt(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	renesas_usbhs_call_notify_hotplug(pdev);

	/* clear status */
	__raw_writew(__raw_readw(priv->usbphyaddr) | USB_PHY_INT_CLR,
		     priv->usbphyaddr);

	return IRQ_HANDLED;
}

static int usbhs1_hardware_init(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);
	int ret;

	/* clear interrupt status */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_CLR, priv->usbphyaddr);

	ret = request_irq(IRQ8, usbhs1_interrupt, IRQF_TRIGGER_HIGH,
			  dev_name(&pdev->dev), pdev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq err\n");
		return ret;
	}

	/* enable USB phy interrupt */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_EN, priv->usbphyaddr);

	return 0;
}

static int usbhs1_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	/* clear interrupt status */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_CLR, priv->usbphyaddr);

	free_irq(IRQ8, pdev);

	return 0;
}

static int usbhs1_get_id(struct platform_device *pdev)
{
	return USBHS_HOST;
}

static u32 usbhs1_pipe_cfg[] = {
	USB_ENDPOINT_XFER_CONTROL,
	USB_ENDPOINT_XFER_ISOC,
	USB_ENDPOINT_XFER_ISOC,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
};

static struct usbhs_private usbhs1_private = {
	.usbphyaddr	= IOMEM(0xe60581e2),	/* USBPHY1INTAP */
	.usbcrcaddr	= IOMEM(0xe6058130),	/* USBCR4 */
	.info = {
		.platform_callback = {
			.hardware_init	= usbhs1_hardware_init,
			.hardware_exit	= usbhs1_hardware_exit,
			.get_id		= usbhs1_get_id,
			.phy_reset	= usbhs_phy_reset,
		},
		.driver_param = {
			.buswait_bwait	= 4,
			.has_otg	= 1,
			.pipe_type	= usbhs1_pipe_cfg,
			.pipe_size	= ARRAY_SIZE(usbhs1_pipe_cfg),
			.d0_tx_id	= SHDMA_SLAVE_USB1_TX,
			.d1_rx_id	= SHDMA_SLAVE_USB1_RX,
		},
	},
};

static struct resource usbhs1_resources[] = {
	[0] = {
		.name	= "USBHS1",
		.start	= 0xe68b0000,
		.end	= 0xe68b00e6 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x1ce0) /* USB1_USB1I0 */,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbhs1_device = {
	.name	= "renesas_usbhs",
	.id	= 1,
	.dev = {
		.platform_data		= &usbhs1_private.info,
		.dma_mask		= &usbhs1_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(usbhs1_resources),
	.resource	= usbhs1_resources,
};

/* LED */
static struct gpio_led mackerel_leds[] = {
	{
		.name		= "led0",
		.gpio		= 0,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led1",
		.gpio		= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led2",
		.gpio		= 2,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "led3",
		.gpio		= 159,
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
static struct sh_fsi_platform_info fsi_info = {
	.port_a = {
		.tx_id = SHDMA_SLAVE_FSIA_TX,
		.rx_id = SHDMA_SLAVE_FSIA_RX,
	},
	.port_b = {
		.flags = SH_FSI_CLK_CPG	|
			 SH_FSI_FMT_SPDIF,
	}
};

static struct resource fsi_resources[] = {
	[0] = {
		/* we need 0xFE1F0000 to access DMA
		 * instead of 0xFE3C0000 */
		.name	= "FSI",
		.start  = 0xFE1F0000,
		.end    = 0xFE1F0400 - 1,
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
	.daifmt		= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBM_CFM,
	.cpu_dai = {
		.name	= "fsia-dai",
	},
	.codec_dai = {
		.name	= "ak4642-hifi",
		.sysclk	= 11289600,
	},
};

static struct platform_device fsi_ak4643_device = {
	.name	= "asoc-simple-card",
	.dev	= {
		.platform_data	= &fsi2_ak4643_info,
	},
};

/* FLCTL */
static struct mtd_partition nand_partition_info[] = {
	{
		.name	= "system",
		.offset	= 0,
		.size	= 128 * 1024 * 1024,
	},
	{
		.name	= "userdata",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 256 * 1024 * 1024,
	},
	{
		.name	= "cache",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 128 * 1024 * 1024,
	},
};

static struct resource nand_flash_resources[] = {
	[0] = {
		.start	= 0xe6a30000,
		.end	= 0xe6a3009b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0d80), /* flstei: status error irq */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_flctl_platform_data nand_flash_data = {
	.parts		= nand_partition_info,
	.nr_parts	= ARRAY_SIZE(nand_partition_info),
	.flcmncr_val	= CLK_16B_12L_4H | TYPESEL_SET
			| SHBUSSEL | SEL_16BIT | SNAND_E,
	.use_holden	= 1,
};

static struct platform_device nand_flash_device = {
	.name		= "sh_flctl",
	.resource	= nand_flash_resources,
	.num_resources	= ARRAY_SIZE(nand_flash_resources),
	.dev		= {
		.platform_data = &nand_flash_data,
	},
};

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_flags	= TMIO_MMC_USE_GPIO_CD,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
	.cd_gpio	= 172,
};

static struct resource sdhi0_resources[] = {
	{
		.name	= "SDHI0",
		.start	= 0xe6850000,
		.end	= 0xe68500ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= evt2irq(0x0e20) /* SDHI0_SDHI0I1 */,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= evt2irq(0x0e40) /* SDHI0_SDHI0I2 */,
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

#if !IS_ENABLED(CONFIG_MMC_SH_MMCIF)
/* SDHI1 */

/* GPIO 41 can trigger IRQ8, but it is used by USBHS1, we have to poll */
static struct sh_mobile_sdhi_info sdhi1_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_USE_GPIO_CD,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_NEEDS_POLL,
	.cd_gpio	= 41,
};

static struct resource sdhi1_resources[] = {
	{
		.name	= "SDHI1",
		.start	= 0xe6860000,
		.end	= 0xe68600ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= evt2irq(0x0ea0), /* SDHI1_SDHI1I1 */
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= evt2irq(0x0ec0), /* SDHI1_SDHI1I2 */
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

/*
 * The card detect pin of the top SD/MMC slot (CN23) is active low and is
 * connected to GPIO SCIFB_SCK of SH7372 (GPIO 162).
 */
static struct sh_mobile_sdhi_info sdhi2_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI2_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI2_RX,
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_USE_GPIO_CD,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_NEEDS_POLL,
	.cd_gpio	= 162,
};

static struct resource sdhi2_resources[] = {
	{
		.name	= "SDHI2",
		.start	= 0xe6870000,
		.end	= 0xe68700ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= evt2irq(0x1220), /* SDHI2_SDHI2I1 */
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= evt2irq(0x1240), /* SDHI2_SDHI2I2 */
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
#if IS_ENABLED(CONFIG_MMC_SH_MMCIF)
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
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NEEDS_POLL,
	.use_cd_gpio	= true,
	/* card detect pin for SD/MMC slot (CN7) */
	.cd_gpio	= 41,
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
#endif

static int mackerel_camera_add(struct soc_camera_device *icd);
static void mackerel_camera_del(struct soc_camera_device *icd);

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
	.mbus_param = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
	V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
	V4L2_MBUS_DATA_ACTIVE_HIGH,
	.mbus_type = V4L2_MBUS_PARALLEL,
	.set_capture = camera_set_capture,
};

static struct soc_camera_link camera_link = {
	.bus_id		= 0,
	.add_device	= mackerel_camera_add,
	.del_device	= mackerel_camera_del,
	.module_name	= "soc_camera_platform",
	.priv		= &camera_info,
};

static struct platform_device *camera_device;

static void mackerel_camera_release(struct device *dev)
{
	soc_camera_platform_release(&camera_device);
}

static int mackerel_camera_add(struct soc_camera_device *icd)
{
	return soc_camera_platform_add(icd, &camera_device, &camera_link,
				       mackerel_camera_release, 0);
}

static void mackerel_camera_del(struct soc_camera_device *icd)
{
	soc_camera_platform_del(icd, camera_device, &camera_link);
}

static struct sh_mobile_ceu_info sh_mobile_ceu_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
	.max_width = 8188,
	.max_height = 8188,
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
		.platform_data		= &sh_mobile_ceu_info,
		.coherent_dma_mask	= 0xffffffff,
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
	&gpio_backlight_device,
	&usbhs0_device,
	&usbhs1_device,
	&leds_device,
	&fsi_device,
	&fsi_ak4643_device,
	&fsi_hdmi_device,
	&nand_flash_device,
	&sdhi0_device,
#if !IS_ENABLED(CONFIG_MMC_SH_MMCIF)
	&sdhi1_device,
#else
	&sh_mmcif_device,
#endif
	&sdhi2_device,
	&ceu_device,
	&mackerel_camera,
	&hdmi_device,
	&hdmi_lcdc_device,
	&meram_device,
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
#define IRQ7 evt2irq(0x02e0)
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
	/* Touchscreen */
	{
		I2C_BOARD_INFO("st1232-ts", 0x55),
		.irq = IRQ7,
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

static unsigned long pin_pulldown_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 0),
};

static const struct pinctrl_map mackerel_pinctrl_map[] = {
	/* ADXL34X */
	PIN_MAP_MUX_GROUP_DEFAULT("1-0053", "pfc-sh7372",
				  "intc_irq21", "intc"),
	/* CEU */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-sh7372",
				  "ceu_data_0_7", "ceu"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-sh7372",
				  "ceu_clk_0", "ceu"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-sh7372",
				  "ceu_sync", "ceu"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-sh7372",
				  "ceu_field", "ceu"),
	/* FLCTL */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_flctl.0", "pfc-sh7372",
				  "flctl_data", "flctl"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_flctl.0", "pfc-sh7372",
				  "flctl_ce0", "flctl"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_flctl.0", "pfc-sh7372",
				  "flctl_ctrl", "flctl"),
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
	/* LCDC */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-sh7372",
				  "lcd_data24", "lcd"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-sh7372",
				  "lcd_sync", "lcd"),
	/* SCIFA0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-sh7372",
				  "scifa0_data", "scifa0"),
	/* SCIFA2 (GT-720F GPS module) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-sh7372",
				  "scifa2_data", "scifa2"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "sdhi0_wp", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh7372",
				  "intc_irq26_1", "intc"),
	/* SDHI1 */
#if !IS_ENABLED(CONFIG_MMC_SH_MMCIF)
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh7372",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh7372",
				  "sdhi1_ctrl", "sdhi1"),
#else
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh7372",
				  "mmc0_data8_0", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh7372",
				  "mmc0_ctrl_0", "mmc0"),
#endif
	/* SDHI2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-sh7372",
				  "sdhi2_data4", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-sh7372",
				  "sdhi2_ctrl", "sdhi2"),
	/* SMSC911X */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-sh7372",
				  "bsc_cs5a", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-sh7372",
				  "intc_irq6_0", "intc"),
	/* ST1232 */
	PIN_MAP_MUX_GROUP_DEFAULT("0-0055", "pfc-sh7372",
				  "intc_irq7_0", "intc"),
	/* TCA6416 */
	PIN_MAP_MUX_GROUP_DEFAULT("0-0020", "pfc-sh7372",
				  "intc_irq9_0", "intc"),
	/* USBHS0 */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs.0", "pfc-sh7372",
				  "usb0_vbus", "usb0"),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("renesas_usbhs.0", "pfc-sh7372",
				      "usb0_vbus", pin_pulldown_conf),
	/* USBHS1 */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs.1", "pfc-sh7372",
				  "usb1_vbus", "usb1"),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("renesas_usbhs.1", "pfc-sh7372",
				      "usb1_vbus", pin_pulldown_conf),
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs.1", "pfc-sh7372",
				  "usb1_otg_id_0", "usb1"),
};

#define GPIO_PORT9CR	IOMEM(0xE6051009)
#define GPIO_PORT10CR	IOMEM(0xE605100A)
#define SRCR4		IOMEM(0xe61580bc)
#define USCCR1		IOMEM(0xE6058144)
static void __init mackerel_init(void)
{
	struct pm_domain_device domain_devices[] = {
		{ "A4LC", &lcdc_device, },
		{ "A4LC", &hdmi_lcdc_device, },
		{ "A4LC", &meram_device, },
		{ "A4MP", &fsi_device, },
		{ "A3SP", &usbhs0_device, },
		{ "A3SP", &usbhs1_device, },
		{ "A3SP", &nand_flash_device, },
		{ "A3SP", &sdhi0_device, },
#if !IS_ENABLED(CONFIG_MMC_SH_MMCIF)
		{ "A3SP", &sdhi1_device, },
#else
		{ "A3SP", &sh_mmcif_device, },
#endif
		{ "A3SP", &sdhi2_device, },
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

	pinctrl_register_mappings(mackerel_pinctrl_map,
				  ARRAY_SIZE(mackerel_pinctrl_map));
	sh7372_pinmux_init();

	gpio_request_one(151, GPIOF_OUT_INIT_HIGH, NULL); /* LCDDON */

	/* FSI2 port A (ak4643) */
	gpio_request_one(161, GPIOF_OUT_INIT_LOW, NULL); /* slave */

	gpio_request(9,  NULL);
	gpio_request(10, NULL);
	gpio_direction_none(GPIO_PORT9CR);  /* FSIAOBT needs no direction */
	gpio_direction_none(GPIO_PORT10CR); /* FSIAOLR needs no direction */

	intc_set_priority(IRQ_FSI, 3); /* irq priority FSI(3) > SMSC911X(2) */

	/* FSI2 port B (HDMI) */
	__raw_writew(__raw_readw(USCCR1) & ~(1 << 6), USCCR1); /* use SPDIF */

	/* set SPU2 clock to 119.6 MHz */
	clk = clk_get(NULL, "spu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 119600000));
		clk_put(clk);
	}

	/* Keypad */
	irq_set_irq_type(IRQ9, IRQ_TYPE_LEVEL_HIGH);

	/* Touchscreen */
	irq_set_irq_type(IRQ7, IRQ_TYPE_LEVEL_LOW);

	/* Accelerometer */
	irq_set_irq_type(IRQ21, IRQ_TYPE_LEVEL_HIGH);

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

	rmobile_add_devices_to_domains(domain_devices,
				       ARRAY_SIZE(domain_devices));

	hdmi_init_pm_clock();
	sh7372_pm_init();
	pm_clk_add(&fsi_device.dev, "spu2");
	pm_clk_add(&hdmi_lcdc_device.dev, "hdmi");
}

static const char *mackerel_boards_compat_dt[] __initdata = {
	"renesas,mackerel",
	NULL,
};

DT_MACHINE_START(MACKEREL_DT, "mackerel")
	.map_io		= sh7372_map_io,
	.init_early	= sh7372_add_early_devices,
	.init_irq	= sh7372_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= mackerel_init,
	.init_late	= sh7372_pm_init_late,
	.init_time	= sh7372_earlytimer_init,
	.dt_compat  = mackerel_boards_compat_dt,
MACHINE_END
