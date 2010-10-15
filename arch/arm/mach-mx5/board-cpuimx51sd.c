/*
 *
 * Copyright (C) 2010 Eric Bénard <eric@eukrea.com>
 *
 * based on board-mx51_babbage.c which is
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009-2010 Amit Kucheria <amit.kucheria@canonical.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/fsl_devices.h>
#include <linux/i2c-gpio.h>
#include <linux/spi/spi.h>
#include <linux/can/platform/mcp251x.h>

#include <mach/eukrea-baseboards.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>
#include <mach/mxc_ehci.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "devices.h"

#define USBH1_RST		(1*32 + 28)
#define ETH_RST			(1*32 + 31)
#define TSC2007_IRQGPIO		(2*32 + 12)
#define CAN_IRQGPIO		(0*32 + 1)
#define CAN_RST			(3*32 + 15)
#define CAN_NCS			(3*32 + 24)
#define CAN_RXOBF		(0*32 + 4)
#define CAN_RX1BF		(0*32 + 6)
#define CAN_TXORTS		(0*32 + 7)
#define CAN_TX1RTS		(0*32 + 8)
#define CAN_TX2RTS		(0*32 + 9)
#define I2C_SCL			(3*32 + 16)
#define I2C_SDA			(3*32 + 17)

/* USB_CTRL_1 */
#define MX51_USB_CTRL_1_OFFSET		0x10
#define MX51_USB_CTRL_UH1_EXT_CLK_EN	(1 << 25)

#define	MX51_USB_PLLDIV_12_MHZ		0x00
#define	MX51_USB_PLL_DIV_19_2_MHZ	0x01
#define	MX51_USB_PLL_DIV_24_MHZ		0x02

#define CPUIMX51SD_GPIO_3_12 IOMUX_PAD(0x57C, 0x194, 3, 0x0, 0, \
				MX51_PAD_CTRL_1 | PAD_CTL_PUS_22K_UP)

static struct pad_desc eukrea_cpuimx51sd_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* USB HOST1 */
	MX51_PAD_USBH1_CLK__USBH1_CLK,
	MX51_PAD_USBH1_DIR__USBH1_DIR,
	MX51_PAD_USBH1_NXT__USBH1_NXT,
	MX51_PAD_USBH1_DATA0__USBH1_DATA0,
	MX51_PAD_USBH1_DATA1__USBH1_DATA1,
	MX51_PAD_USBH1_DATA2__USBH1_DATA2,
	MX51_PAD_USBH1_DATA3__USBH1_DATA3,
	MX51_PAD_USBH1_DATA4__USBH1_DATA4,
	MX51_PAD_USBH1_DATA5__USBH1_DATA5,
	MX51_PAD_USBH1_DATA6__USBH1_DATA6,
	MX51_PAD_USBH1_DATA7__USBH1_DATA7,
	MX51_PAD_USBH1_STP__USBH1_STP,
	MX51_PAD_EIM_CS3__GPIO_2_28,		/* PHY nRESET */

	/* FEC */
	MX51_PAD_EIM_DTACK__GPIO_2_31,		/* PHY nRESET */

	/* HSI2C */
	MX51_PAD_I2C1_CLK__GPIO_4_16,
	MX51_PAD_I2C1_DAT__GPIO_4_17,

	/* CAN */
	MX51_PAD_CSPI1_MOSI__ECSPI1_MOSI,
	MX51_PAD_CSPI1_MISO__ECSPI1_MISO,
	MX51_PAD_CSPI1_SCLK__ECSPI1_SCLK,
	MX51_PAD_CSPI1_SS0__GPIO_4_24,		/* nCS */
	MX51_PAD_CSI2_PIXCLK__GPIO_4_15,	/* nReset */
	MX51_PAD_GPIO_1_1__GPIO_1_1,		/* IRQ */
	MX51_PAD_GPIO_1_4__GPIO_1_4,		/* Control signals */
	MX51_PAD_GPIO_1_6__GPIO_1_6,
	MX51_PAD_GPIO_1_7__GPIO_1_7,
	MX51_PAD_GPIO_1_8__GPIO_1_8,
	MX51_PAD_GPIO_1_9__GPIO_1_9,

	/* Touchscreen */
	CPUIMX51SD_GPIO_3_12,			/* IRQ */
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static int ts_get_pendown_state(void)
{
	return gpio_get_value(TSC2007_IRQGPIO) ? 0 : 1;
}

static struct tsc2007_platform_data tsc2007_info = {
	.model			= 2007,
	.x_plate_ohms		= 180,
	.get_pendown_state	= ts_get_pendown_state,
};

static struct i2c_board_info eukrea_cpuimx51sd_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	}, {
		I2C_BOARD_INFO("tsc2007", 0x49),
		.type		= "tsc2007",
		.platform_data	= &tsc2007_info,
		.irq		= gpio_to_irq(TSC2007_IRQGPIO),
	},
};

static const struct mxc_nand_platform_data
		eukrea_cpuimx51sd_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
	.flash_bbt	= 1,
};

/* This function is board specific as the bit mask for the plldiv will also
be different for other Freescale SoCs, thus a common bitmask is not
possible and cannot get place in /plat-mxc/ehci.c.*/
static int initialize_otg_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;

	usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);
	usbother_base = usb_base + MX5_USBOTHER_REGS_OFFSET;

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_19_2_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);
	return 0;
}

static int initialize_usbh1_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;

	usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);
	usbother_base = usb_base + MX5_USBOTHER_REGS_OFFSET;

	/* The clock for the USBH1 ULPI port will come from the PHY. */
	v = __raw_readl(usbother_base + MX51_USB_CTRL_1_OFFSET);
	__raw_writel(v | MX51_USB_CTRL_UH1_EXT_CLK_EN,
			usbother_base + MX51_USB_CTRL_1_OFFSET);
	iounmap(usb_base);
	return 0;
}

static struct mxc_usbh_platform_data dr_utmi_config = {
	.init		= initialize_otg_port,
	.portsc	= MXC_EHCI_UTMI_16BIT,
	.flags	= MXC_EHCI_INTERNAL_PHY,
};

static struct fsl_usb2_platform_data usb_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI_WIDE,
};

static struct mxc_usbh_platform_data usbh1_config = {
	.init		= initialize_usbh1_port,
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= (MXC_EHCI_POWER_PINS_ENABLED | MXC_EHCI_ITC_NO_THRESHOLD),
};

static int otg_mode_host;

static int __init eukrea_cpuimx51sd_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = 1;
	else if (!strcmp(options, "device"))
		otg_mode_host = 0;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 0;
}
__setup("otg_mode=", eukrea_cpuimx51sd_otg_mode);

static struct i2c_gpio_platform_data pdata = {
	.sda_pin		= I2C_SDA,
	.sda_is_open_drain	= 0,
	.scl_pin		= I2C_SCL,
	.scl_is_open_drain	= 0,
	.udelay			= 2,
};

static struct platform_device hsi2c_gpio_device = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev.platform_data	= &pdata,
};

static struct mcp251x_platform_data mcp251x_info = {
	.oscillator_frequency = 24E6,
};

static struct spi_board_info cpuimx51sd_spi_device[] = {
	{
		.modalias        = "mcp2515",
		.max_speed_hz    = 6500000,
		.bus_num         = 0,
		.mode		= SPI_MODE_0,
		.chip_select     = 0,
		.platform_data   = &mcp251x_info,
		.irq             = gpio_to_irq(0 * 32 + 1)
	},
};

static int cpuimx51sd_spi1_cs[] = {
	CAN_NCS,
};

static const struct spi_imx_master cpuimx51sd_ecspi1_pdata __initconst = {
	.chipselect	= cpuimx51sd_spi1_cs,
	.num_chipselect	= ARRAY_SIZE(cpuimx51sd_spi1_cs),
};

static struct platform_device *platform_devices[] __initdata = {
	&hsi2c_gpio_device,
};

static void __init eukrea_cpuimx51sd_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(eukrea_cpuimx51sd_pads,
					ARRAY_SIZE(eukrea_cpuimx51sd_pads));

	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_mxc_nand(&eukrea_cpuimx51sd_nand_board_info);

	gpio_request(ETH_RST, "eth_rst");
	gpio_set_value(ETH_RST, 1);
	imx51_add_fec(NULL);

	gpio_request(CAN_IRQGPIO, "can_irq");
	gpio_direction_input(CAN_IRQGPIO);
	gpio_free(CAN_IRQGPIO);
	gpio_request(CAN_NCS, "can_ncs");
	gpio_direction_output(CAN_NCS, 1);
	gpio_free(CAN_NCS);
	gpio_request(CAN_RST, "can_rst");
	gpio_direction_output(CAN_RST, 0);
	msleep(20);
	gpio_set_value(CAN_RST, 1);
	imx51_add_ecspi(0, &cpuimx51sd_ecspi1_pdata);
	spi_register_board_info(cpuimx51sd_spi_device,
				ARRAY_SIZE(cpuimx51sd_spi_device));

	gpio_request(TSC2007_IRQGPIO, "tsc2007_irq");
	gpio_direction_input(TSC2007_IRQGPIO);
	gpio_free(TSC2007_IRQGPIO);

	i2c_register_board_info(0, eukrea_cpuimx51sd_i2c_devices,
			ARRAY_SIZE(eukrea_cpuimx51sd_i2c_devices));
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	if (otg_mode_host)
		mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	else {
		initialize_otg_port(NULL);
		mxc_register_device(&mxc_usbdr_udc_device, &usb_pdata);
	}

	gpio_request(USBH1_RST, "usb_rst");
	gpio_direction_output(USBH1_RST, 0);
	msleep(20);
	gpio_set_value(USBH1_RST, 1);
	mxc_register_device(&mxc_usbh1_device, &usbh1_config);

#ifdef CONFIG_MACH_EUKREA_MBIMXSD51_BASEBOARD
	eukrea_mbimxsd51_baseboard_init();
#endif
}

static void __init eukrea_cpuimx51sd_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mxc_timer = {
	.init	= eukrea_cpuimx51sd_timer_init,
};

MACHINE_START(EUKREA_CPUIMX51SD, "Eukrea CPUIMX51SD")
	/* Maintainer: Eric Bénard <eric@eukrea.com> */
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_irq = mx51_init_irq,
	.init_machine = eukrea_cpuimx51sd_init,
	.timer = &mxc_timer,
MACHINE_END
