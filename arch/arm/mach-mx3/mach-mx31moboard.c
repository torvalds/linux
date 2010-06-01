/*
 *  Copyright (C) 2008 Valentin Longchamp, EPFL Mobots group
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/memory.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/mc13783.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/board-mx31moboard.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/ipu.h>
#include <mach/i2c.h>
#include <mach/mmc.h>
#include <mach/mxc_ehci.h>
#include <mach/mx3_camera.h>
#include <mach/spi.h>
#include <mach/ulpi.h>

#include "devices.h"

static unsigned int moboard_pins[] = {
	/* UART0 */
	MX31_PIN_TXD1__TXD1, MX31_PIN_RXD1__RXD1,
	MX31_PIN_CTS1__GPIO2_7,
	/* UART4 */
	MX31_PIN_PC_RST__CTS5, MX31_PIN_PC_VS2__RTS5,
	MX31_PIN_PC_BVD2__TXD5, MX31_PIN_PC_BVD1__RXD5,
	/* I2C0 */
	MX31_PIN_I2C_DAT__I2C1_SDA, MX31_PIN_I2C_CLK__I2C1_SCL,
	/* I2C1 */
	MX31_PIN_DCD_DTE1__I2C2_SDA, MX31_PIN_RI_DTE1__I2C2_SCL,
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3, MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1, MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK, MX31_PIN_SD1_CMD__SD1_CMD,
	MX31_PIN_ATA_CS0__GPIO3_26, MX31_PIN_ATA_CS1__GPIO3_27,
	/* USB reset */
	MX31_PIN_GPIO1_0__GPIO1_0,
	/* USB OTG */
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK, MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT, MX31_PIN_USBOTG_STP__USBOTG_STP,
	MX31_PIN_USB_OC__GPIO1_30,
	/* USB H2 */
	MX31_PIN_USBH2_DATA0__USBH2_DATA0,
	MX31_PIN_USBH2_DATA1__USBH2_DATA1,
	MX31_PIN_STXD3__USBH2_DATA2, MX31_PIN_SRXD3__USBH2_DATA3,
	MX31_PIN_SCK3__USBH2_DATA4, MX31_PIN_SFS3__USBH2_DATA5,
	MX31_PIN_STXD6__USBH2_DATA6, MX31_PIN_SRXD6__USBH2_DATA7,
	MX31_PIN_USBH2_CLK__USBH2_CLK, MX31_PIN_USBH2_DIR__USBH2_DIR,
	MX31_PIN_USBH2_NXT__USBH2_NXT, MX31_PIN_USBH2_STP__USBH2_STP,
	MX31_PIN_SCK6__GPIO1_25,
	/* LEDs */
	MX31_PIN_SVEN0__GPIO2_0, MX31_PIN_STX0__GPIO2_1,
	MX31_PIN_SRX0__GPIO2_2, MX31_PIN_SIMPD0__GPIO2_3,
	/* SPI1 */
	MX31_PIN_CSPI2_MOSI__MOSI, MX31_PIN_CSPI2_MISO__MISO,
	MX31_PIN_CSPI2_SCLK__SCLK, MX31_PIN_CSPI2_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI2_SS0__SS0, MX31_PIN_CSPI2_SS2__SS2,
	/* Atlas IRQ */
	MX31_PIN_GPIO1_3__GPIO1_3,
	/* SPI2 */
	MX31_PIN_CSPI3_MOSI__MOSI, MX31_PIN_CSPI3_MISO__MISO,
	MX31_PIN_CSPI3_SCLK__SCLK, MX31_PIN_CSPI3_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI2_SS1__CSPI3_SS1,
};

static struct physmap_flash_data mx31moboard_flash_data = {
	.width  	= 2,
};

static struct resource mx31moboard_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mx31moboard_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &mx31moboard_flash_data,
	},
	.resource = &mx31moboard_flash_resource,
	.num_resources = 1,
};

static int moboard_uart0_init(struct platform_device *pdev)
{
	gpio_request(IOMUX_TO_GPIO(MX31_PIN_CTS1), "uart0-cts-hack");
	gpio_direction_output(IOMUX_TO_GPIO(MX31_PIN_CTS1), 0);
	return 0;
}

static struct imxuart_platform_data uart0_pdata = {
	.init = moboard_uart0_init,
};

static struct imxuart_platform_data uart4_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct imxi2c_platform_data moboard_i2c0_pdata = {
	.bitrate = 400000,
};

static struct imxi2c_platform_data moboard_i2c1_pdata = {
	.bitrate = 100000,
};

static int moboard_spi1_cs[] = {
	MXC_SPI_CS(0),
	MXC_SPI_CS(2),
};

static struct spi_imx_master moboard_spi1_master = {
	.chipselect	= moboard_spi1_cs,
	.num_chipselect	= ARRAY_SIZE(moboard_spi1_cs),
};

static struct regulator_consumer_supply sdhc_consumers[] = {
	{
		.dev	= &mxcsdhc_device0.dev,
		.supply	= "sdhc0_vcc",
	},
	{
		.dev	= &mxcsdhc_device1.dev,
		.supply	= "sdhc1_vcc",
	},
};

static struct regulator_init_data sdhc_vreg_data = {
	.constraints = {
		.min_uV = 2700000,
		.max_uV = 3000000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
			REGULATOR_MODE_FAST,
		.always_on = 0,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(sdhc_consumers),
	.consumer_supplies = sdhc_consumers,
};

static struct regulator_consumer_supply cam_consumers[] = {
	{
		.dev	= &mx3_camera.dev,
		.supply	= "cam_vcc",
	},
};

static struct regulator_init_data cam_vreg_data = {
	.constraints = {
		.min_uV = 2700000,
		.max_uV = 3000000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
			REGULATOR_MODE_FAST,
		.always_on = 0,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(cam_consumers),
	.consumer_supplies = cam_consumers,
};

static struct mc13783_regulator_init_data moboard_regulators[] = {
	{
		.id = MC13783_REGU_VMMC1,
		.init_data = &sdhc_vreg_data,
	},
	{
		.id = MC13783_REGU_VCAM,
		.init_data = &cam_vreg_data,
	},
};

static struct mc13783_led_platform_data moboard_led[] = {
	{
		.id = MC13783_LED_R1,
		.name = "coreboard-led-4:red",
		.max_current = 2,
	},
	{
		.id = MC13783_LED_G1,
		.name = "coreboard-led-4:green",
		.max_current = 2,
	},
	{
		.id = MC13783_LED_B1,
		.name = "coreboard-led-4:blue",
		.max_current = 2,
	},
	{
		.id = MC13783_LED_R2,
		.name = "coreboard-led-5:red",
		.max_current = 3,
	},
	{
		.id = MC13783_LED_G2,
		.name = "coreboard-led-5:green",
		.max_current = 3,
	},
	{
		.id = MC13783_LED_B2,
		.name = "coreboard-led-5:blue",
		.max_current = 3,
	},
};

static struct mc13783_leds_platform_data moboard_leds = {
	.num_leds = ARRAY_SIZE(moboard_led),
	.led = moboard_led,
	.flags = MC13783_LED_SLEWLIMTC,
	.abmode = MC13783_LED_AB_DISABLED,
	.tc1_period = MC13783_LED_PERIOD_10MS,
	.tc2_period = MC13783_LED_PERIOD_10MS,
};

static struct mc13783_platform_data moboard_pmic = {
	.regulators = moboard_regulators,
	.num_regulators = ARRAY_SIZE(moboard_regulators),
	.leds = &moboard_leds,
	.flags = MC13783_USE_REGULATOR | MC13783_USE_RTC |
		MC13783_USE_ADC | MC13783_USE_LED,
};

static struct spi_board_info moboard_spi_board_info[] __initdata = {
	{
		.modalias = "mc13783",
		.irq = IOMUX_TO_IRQ(MX31_PIN_GPIO1_3),
		.max_speed_hz = 300000,
		.bus_num = 1,
		.chip_select = 0,
		.platform_data = &moboard_pmic,
		.mode = SPI_CS_HIGH,
	},
};

static int moboard_spi2_cs[] = {
	MXC_SPI_CS(1),
};

static struct spi_imx_master moboard_spi2_master = {
	.chipselect	= moboard_spi2_cs,
	.num_chipselect	= ARRAY_SIZE(moboard_spi2_cs),
};

#define SDHC1_CD IOMUX_TO_GPIO(MX31_PIN_ATA_CS0)
#define SDHC1_WP IOMUX_TO_GPIO(MX31_PIN_ATA_CS1)

static int moboard_sdhc1_get_ro(struct device *dev)
{
	return !gpio_get_value(SDHC1_WP);
}

static int moboard_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = gpio_request(SDHC1_CD, "sdhc-detect");
	if (ret)
		return ret;

	gpio_direction_input(SDHC1_CD);

	ret = gpio_request(SDHC1_WP, "sdhc-wp");
	if (ret)
		goto err_gpio_free;
	gpio_direction_input(SDHC1_WP);

	ret = request_irq(gpio_to_irq(SDHC1_CD), detect_irq,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"sdhc1-card-detect", data);
	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
	gpio_free(SDHC1_WP);
err_gpio_free:
	gpio_free(SDHC1_CD);

	return ret;
}

static void moboard_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(SDHC1_CD), data);
	gpio_free(SDHC1_WP);
	gpio_free(SDHC1_CD);
}

static struct imxmmc_platform_data sdhc1_pdata = {
	.get_ro	= moboard_sdhc1_get_ro,
	.init	= moboard_sdhc1_init,
	.exit	= moboard_sdhc1_exit,
};

/*
 * this pin is dedicated for all mx31moboard systems, so we do it here
 */
#define USB_RESET_B	IOMUX_TO_GPIO(MX31_PIN_GPIO1_0)
#define USB_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
		      PAD_CTL_ODE_CMOS)

#define OTG_EN_B IOMUX_TO_GPIO(MX31_PIN_USB_OC)
#define USBH2_EN_B IOMUX_TO_GPIO(MX31_PIN_SCK6)

static void usb_xcvr_reset(void)
{
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP, USB_PAD_CFG | PAD_CTL_100K_PU);

	mxc_iomux_set_gpr(MUX_PGP_UH2, true);
	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, USB_PAD_CFG | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_SRXD6, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_STXD6, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_SFS3, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_SCK3, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_SRXD3, USB_PAD_CFG | PAD_CTL_100K_PD);
	mxc_iomux_set_pad(MX31_PIN_STXD3, USB_PAD_CFG | PAD_CTL_100K_PD);

	gpio_request(OTG_EN_B, "usb-udc-en");
	gpio_direction_output(OTG_EN_B, 0);
	gpio_request(USBH2_EN_B, "usbh2-en");
	gpio_direction_output(USBH2_EN_B, 0);

	gpio_request(USB_RESET_B, "usb-reset");
	gpio_direction_output(USB_RESET_B, 0);
	mdelay(1);
	gpio_set_value(USB_RESET_B, 1);
	mdelay(1);
}

#if defined(CONFIG_USB_ULPI)

static struct mxc_usbh_platform_data usbh2_pdata = {
	.portsc	= MXC_EHCI_MODE_ULPI | MXC_EHCI_UTMI_8BIT,
	.flags	= MXC_EHCI_POWER_PINS_ENABLED,
};

static int __init moboard_usbh2_init(void)
{
	usbh2_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
			USB_OTG_DRV_VBUS | USB_OTG_DRV_VBUS_EXT);

	return mxc_register_device(&mxc_usbh2, &usbh2_pdata);
}
#else
static inline int moboard_usbh2_init(void) { return 0; }
#endif


static struct gpio_led mx31moboard_leds[] = {
	{
		.name 	= "coreboard-led-0:red:running",
		.default_trigger = "heartbeat",
		.gpio 	= IOMUX_TO_GPIO(MX31_PIN_SVEN0),
	}, {
		.name	= "coreboard-led-1:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_STX0),
	}, {
		.name	= "coreboard-led-2:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_SRX0),
	}, {
		.name	= "coreboard-led-3:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_SIMPD0),
	},
};

static struct gpio_led_platform_data mx31moboard_led_pdata = {
	.num_leds 	= ARRAY_SIZE(mx31moboard_leds),
	.leds		= mx31moboard_leds,
};

static struct platform_device mx31moboard_leds_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &mx31moboard_led_pdata,
	},
};

static struct ipu_platform_data mx3_ipu_data = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct platform_device *devices[] __initdata = {
	&mx31moboard_flash,
	&mx31moboard_leds_device,
};

static struct mx3_camera_pdata camera_pdata = {
	.dma_dev	= &mx3_ipu.dev,
	.flags		= MX3_CAMERA_DATAWIDTH_8 | MX3_CAMERA_DATAWIDTH_10,
	.mclk_10khz	= 4800,
};

#define CAMERA_BUF_SIZE	(4*1024*1024)

static int __init mx31moboard_cam_alloc_dma(const size_t buf_size)
{
	dma_addr_t dma_handle;
	void *buf;
	int dma;

	if (buf_size < 2 * 1024 * 1024)
		return -EINVAL;

	buf = dma_alloc_coherent(NULL, buf_size, &dma_handle, GFP_KERNEL);
	if (!buf) {
		pr_err("%s: cannot allocate camera buffer-memory\n", __func__);
		return -ENOMEM;
	}

	memset(buf, 0, buf_size);

	dma = dma_declare_coherent_memory(&mx3_camera.dev,
					dma_handle, dma_handle, buf_size,
					DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);

	/* The way we call dma_declare_coherent_memory only a malloc can fail */
	return dma & DMA_MEMORY_MAP ? 0 : -ENOMEM;
}

static int mx31moboard_baseboard;
core_param(mx31moboard_baseboard, mx31moboard_baseboard, int, 0444);

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_iomux_setup_multiple_pins(moboard_pins, ARRAY_SIZE(moboard_pins),
		"moboard");

	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_register_device(&mxc_uart_device0, &uart0_pdata);

	mxc_register_device(&mxc_uart_device4, &uart4_pdata);

	mxc_register_device(&mxc_i2c_device0, &moboard_i2c0_pdata);
	mxc_register_device(&mxc_i2c_device1, &moboard_i2c1_pdata);

	mxc_register_device(&mxc_spi_device1, &moboard_spi1_master);
	mxc_register_device(&mxc_spi_device2, &moboard_spi2_master);

	gpio_request(IOMUX_TO_GPIO(MX31_PIN_GPIO1_3), "pmic-irq");
	gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_GPIO1_3));
	spi_register_board_info(moboard_spi_board_info,
		ARRAY_SIZE(moboard_spi_board_info));

	mxc_register_device(&mxcsdhc_device0, &sdhc1_pdata);

	mxc_register_device(&mx3_ipu, &mx3_ipu_data);
	if (!mx31moboard_cam_alloc_dma(CAMERA_BUF_SIZE))
		mxc_register_device(&mx3_camera, &camera_pdata);

	usb_xcvr_reset();

	moboard_usbh2_init();

	switch (mx31moboard_baseboard) {
	case MX31NOBOARD:
		break;
	case MX31DEVBOARD:
		mx31moboard_devboard_init();
		break;
	case MX31MARXBOT:
		mx31moboard_marxbot_init();
		break;
	case MX31SMARTBOT:
	case MX31EYEBOT:
		mx31moboard_smartbot_init(mx31moboard_baseboard);
		break;
	default:
		printk(KERN_ERR "Illegal mx31moboard_baseboard type %d\n",
			mx31moboard_baseboard);
	}
}

static void __init mx31moboard_timer_init(void)
{
	mx31_clocks_init(26000000);
}

struct sys_timer mx31moboard_timer = {
	.init	= mx31moboard_timer_init,
};

MACHINE_START(MX31MOBOARD, "EPFL Mobots mx31moboard")
	/* Maintainer: Valentin Longchamp, EPFL Mobots group */
	.phys_io	= MX31_AIPS1_BASE_ADDR,
	.io_pg_offst	= (MX31_AIPS1_BASE_ADDR_VIRT >> 18) & 0xfffc,
	.boot_params    = MX3x_PHYS_OFFSET + 0x100,
	.map_io         = mx31_map_io,
	.init_irq       = mx31_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31moboard_timer,
MACHINE_END

