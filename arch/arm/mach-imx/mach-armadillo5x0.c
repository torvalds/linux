/*
 * armadillo5x0.c
 *
 * Copyright 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 * updates in http://alberdroid.blogspot.com/
 *
 * Based on Atmark Techno, Inc. armadillo 500 BSP 2008
 * Based on mx31ads.c and pcm037.c Great Work!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>

#include "common.h"
#include "devices-imx31.h"
#include "crmregs-imx3.h"
#include "hardware.h"
#include "iomux-mx3.h"
#include "ulpi.h"

static int armadillo5x0_pins[] = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	/* UART2 */
	MX31_PIN_CTS2__CTS2,
	MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2,
	MX31_PIN_RXD2__RXD2,
	/* LAN9118_IRQ */
	IOMUX_MODE(MX31_PIN_GPIO1_0, IOMUX_CONFIG_GPIO),
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	/* Framebuffer */
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_DRDY0__DRDY0,
	IOMUX_MODE(MX31_PIN_LCS1, IOMUX_CONFIG_GPIO), /*ADV7125_PSAVE*/
	/* I2C2 */
	MX31_PIN_CSPI2_MOSI__SCL,
	MX31_PIN_CSPI2_MISO__SDA,
	/* OTG */
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK,
	MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT,
	MX31_PIN_USBOTG_STP__USBOTG_STP,
	/* USB host 2 */
	IOMUX_MODE(MX31_PIN_USBH2_CLK, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DIR, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_NXT, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_STP, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA0, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA1, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_STXD3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SRXD3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SCK3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SFS3, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_STXD6, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_SRXD6, IOMUX_CONFIG_FUNC),
};

/* USB */

#define OTG_RESET IOMUX_TO_GPIO(MX31_PIN_STXD4)
#define USBH2_RESET IOMUX_TO_GPIO(MX31_PIN_SCK6)
#define USBH2_CS IOMUX_TO_GPIO(MX31_PIN_GPIO1_3)

#define USB_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
			PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

static int usbotg_init(struct platform_device *pdev)
{
	int err;

	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP, USB_PAD_CFG);

	/* Chip already enabled by hardware */
	/* OTG phy reset*/
	err = gpio_request(OTG_RESET, "USB-OTG-RESET");
	if (err) {
		pr_err("Failed to request the usb otg reset gpio\n");
		return err;
	}

	err = gpio_direction_output(OTG_RESET, 1/*HIGH*/);
	if (err) {
		pr_err("Failed to reset the usb otg phy\n");
		goto otg_free_reset;
	}

	gpio_set_value(OTG_RESET, 0/*LOW*/);
	mdelay(5);
	gpio_set_value(OTG_RESET, 1/*HIGH*/);
	mdelay(10);

	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED |
			MXC_EHCI_INTERFACE_DIFF_UNI);

otg_free_reset:
	gpio_free(OTG_RESET);
	return err;
}

static int usbh2_init(struct platform_device *pdev)
{
	int err;

	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SRXD6, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_STXD6, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SFS3, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SCK3, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SRXD3, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_STXD3, USB_PAD_CFG);

	mxc_iomux_set_gpr(MUX_PGP_UH2, true);


	/* Enable the chip */
	err = gpio_request(USBH2_CS, "USB-H2-CS");
	if (err) {
		pr_err("Failed to request the usb host 2 CS gpio\n");
		return err;
	}

	err = gpio_direction_output(USBH2_CS, 0/*Enabled*/);
	if (err) {
		pr_err("Failed to drive the usb host 2 CS gpio\n");
		goto h2_free_cs;
	}

	/* H2 phy reset*/
	err = gpio_request(USBH2_RESET, "USB-H2-RESET");
	if (err) {
		pr_err("Failed to request the usb host 2 reset gpio\n");
		goto h2_free_cs;
	}

	err = gpio_direction_output(USBH2_RESET, 1/*HIGH*/);
	if (err) {
		pr_err("Failed to reset the usb host 2 phy\n");
		goto h2_free_reset;
	}

	gpio_set_value(USBH2_RESET, 0/*LOW*/);
	mdelay(5);
	gpio_set_value(USBH2_RESET, 1/*HIGH*/);
	mdelay(10);

	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED |
			MXC_EHCI_INTERFACE_DIFF_UNI);

h2_free_reset:
	gpio_free(USBH2_RESET);
h2_free_cs:
	gpio_free(USBH2_CS);
	return err;
}

static struct mxc_usbh_platform_data usbotg_pdata __initdata = {
	.init	= usbotg_init,
	.portsc	= MXC_EHCI_MODE_ULPI | MXC_EHCI_UTMI_8BIT,
};

static struct mxc_usbh_platform_data usbh2_pdata __initdata = {
	.init	= usbh2_init,
	.portsc	= MXC_EHCI_MODE_ULPI | MXC_EHCI_UTMI_8BIT,
};

/* RTC over I2C*/
#define ARMADILLO5X0_RTC_GPIO	IOMUX_TO_GPIO(MX31_PIN_SRXD4)

static struct i2c_board_info armadillo5x0_i2c_rtc = {
	I2C_BOARD_INFO("s35390a", 0x30),
};

/* GPIO BUTTONS */
static struct gpio_keys_button armadillo5x0_buttons[] = {
	{
		.code		= KEY_ENTER, /*28*/
		.gpio		= IOMUX_TO_GPIO(MX31_PIN_SCLK0),
		.active_low	= 1,
		.desc		= "menu",
		.wakeup		= 1,
	}, {
		.code		= KEY_BACK, /*158*/
		.gpio		= IOMUX_TO_GPIO(MX31_PIN_SRST0),
		.active_low	= 1,
		.desc		= "back",
		.wakeup		= 1,
	}
};

static const struct gpio_keys_platform_data
		armadillo5x0_button_data __initconst = {
	.buttons	= armadillo5x0_buttons,
	.nbuttons	= ARRAY_SIZE(armadillo5x0_buttons),
};

/*
 * NAND Flash
 */
static const struct mxc_nand_platform_data
armadillo5x0_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
};

/*
 * MTD NOR Flash
 */
static struct mtd_partition armadillo5x0_nor_flash_partitions[] = {
	{
		.name		= "nor.bootloader",
		.offset		= 0x00000000,
		.size		= 4*32*1024,
	}, {
		.name		= "nor.kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 16*128*1024,
	}, {
		.name		= "nor.userland",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 110*128*1024,
	}, {
		.name		= "nor.config",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 1*128*1024,
	},
};

static const struct physmap_flash_data
		armadillo5x0_nor_flash_pdata __initconst = {
	.width		= 2,
	.parts		= armadillo5x0_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(armadillo5x0_nor_flash_partitions),
};

static const struct resource armadillo5x0_nor_flash_resource __initconst = {
	.flags		= IORESOURCE_MEM,
	.start		= MX31_CS0_BASE_ADDR,
	.end		= MX31_CS0_BASE_ADDR + SZ_64M - 1,
};

/*
 * FB support
 */
static const struct fb_videomode fb_modedb[] = {
	{	/* 640x480 @ 60 Hz */
		.name		= "CRT-VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 35,
		.right_margin	= 115,
		.upper_margin	= 43,
		.lower_margin	= 1,
		.hsync_len	= 10,
		.vsync_len	= 1,
		.sync		= FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {/* 800x600 @ 56 Hz */
		.name		= "CRT-SVGA",
		.refresh	= 56,
		.xres		= 800,
		.yres		= 600,
		.pixclock	= 30000,
		.left_margin	= 30,
		.right_margin	= 108,
		.upper_margin	= 13,
		.lower_margin	= 10,
		.hsync_len	= 10,
		.vsync_len	= 1,
		.sync		= FB_SYNC_OE_ACT_HIGH | FB_SYNC_HOR_HIGH_ACT |
				  FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name		= "CRT-VGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

/*
 * SDHC 1
 * MMC support
 */
static int armadillo5x0_sdhc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B));
}

static int armadillo5x0_sdhc1_init(struct device *dev,
				   irq_handler_t detect_irq, void *data)
{
	int ret;
	int gpio_det, gpio_wp;

	gpio_det = IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK);
	gpio_wp = IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B);

	ret = gpio_request(gpio_det, "sdhc-card-detect");
	if (ret)
		return ret;

	gpio_direction_input(gpio_det);

	ret = gpio_request(gpio_wp, "sdhc-write-protect");
	if (ret)
		goto err_gpio_free;

	gpio_direction_input(gpio_wp);

	/* When supported the trigger type have to be BOTH */
	ret = request_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK)),
			  detect_irq,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "sdhc-detect", data);

	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
	gpio_free(gpio_wp);

err_gpio_free:
	gpio_free(gpio_det);

	return ret;

}

static void armadillo5x0_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK)), data);
	gpio_free(IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK));
	gpio_free(IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B));
}

static const struct imxmmc_platform_data sdhc_pdata __initconst = {
	.get_ro = armadillo5x0_sdhc1_get_ro,
	.init = armadillo5x0_sdhc1_init,
	.exit = armadillo5x0_sdhc1_exit,
};

/*
 * SMSC 9118
 * Network support
 */
static struct resource armadillo5x0_smc911x_resources[] = {
	{
		.start	= MX31_CS3_BASE_ADDR,
		.end	= MX31_CS3_BASE_ADDR + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		/* irq number is run-time assigned */
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_16BIT,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type       = SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device armadillo5x0_smc911x_device = {
	.name           = "smsc911x",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(armadillo5x0_smc911x_resources),
	.resource       = armadillo5x0_smc911x_resources,
	.dev            = {
		.platform_data = &smsc911x_info,
	},
};

/* UART device data */
static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *devices[] __initdata = {
	&armadillo5x0_smc911x_device,
};

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
};

/*
 * Perform board specific initializations
 */
static void __init armadillo5x0_init(void)
{
	imx31_soc_init();

	mxc_iomux_setup_multiple_pins(armadillo5x0_pins,
			ARRAY_SIZE(armadillo5x0_pins), "armadillo5x0");

	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	armadillo5x0_smc911x_resources[1].start =
			gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_0));
	armadillo5x0_smc911x_resources[1].end =
			gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_0));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	imx_add_gpio_keys(&armadillo5x0_button_data);
	imx31_add_imx_i2c1(NULL);

	/* Register UART */
	imx31_add_imx_uart0(&uart_pdata);
	imx31_add_imx_uart1(&uart_pdata);

	/* SMSC9118 IRQ pin */
	gpio_direction_input(MX31_PIN_GPIO1_0);

	/* Register SDHC */
	imx31_add_mxc_mmc(0, &sdhc_pdata);

	/* Register FB */
	imx31_add_ipu_core();
	imx31_add_mx3_sdc_fb(&mx3fb_pdata);

	/* Register NOR Flash */
	platform_device_register_resndata(NULL, "physmap-flash", -1,
			&armadillo5x0_nor_flash_resource, 1,
			&armadillo5x0_nor_flash_pdata,
			sizeof(armadillo5x0_nor_flash_pdata));

	/* Register NAND Flash */
	imx31_add_mxc_nand(&armadillo5x0_nand_board_info);

	/* set NAND page size to 2k if not configured via boot mode pins */
	__raw_writel(__raw_readl(mx3_ccm_base + MXC_CCM_RCSR) |
					(1 << 30), mx3_ccm_base + MXC_CCM_RCSR);

	/* RTC */
	/* Get RTC IRQ and register the chip */
	if (gpio_request(ARMADILLO5X0_RTC_GPIO, "rtc") == 0) {
		if (gpio_direction_input(ARMADILLO5X0_RTC_GPIO) == 0)
			armadillo5x0_i2c_rtc.irq = gpio_to_irq(ARMADILLO5X0_RTC_GPIO);
		else
			gpio_free(ARMADILLO5X0_RTC_GPIO);
	}
	if (armadillo5x0_i2c_rtc.irq == 0)
		pr_warning("armadillo5x0_init: failed to get RTC IRQ\n");
	i2c_register_board_info(1, &armadillo5x0_i2c_rtc, 1);

	/* USB */

	usbotg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT);
	if (usbotg_pdata.otg)
		imx31_add_mxc_ehci_otg(&usbotg_pdata);
	usbh2_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT);
	if (usbh2_pdata.otg)
		imx31_add_mxc_ehci_hs(2, &usbh2_pdata);
}

static void __init armadillo5x0_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer armadillo5x0_timer = {
	.init	= armadillo5x0_timer_init,
};

MACHINE_START(ARMADILLO5X0, "Armadillo-500")
	/* Maintainer: Alberto Panizzo  */
	.atag_offset = 0x100,
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.handle_irq = imx31_handle_irq,
	.timer = &armadillo5x0_timer,
	.init_machine = armadillo5x0_init,
	.restart	= mxc_restart,
MACHINE_END
