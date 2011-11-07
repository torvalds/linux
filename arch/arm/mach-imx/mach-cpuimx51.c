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
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include <mach/eukrea-baseboards.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"

#define CPUIMX51_USBH1_STP	IMX_GPIO_NR(1, 27)
#define CPUIMX51_QUARTA_GPIO	IMX_GPIO_NR(3, 28)
#define CPUIMX51_QUARTB_GPIO	IMX_GPIO_NR(3, 25)
#define CPUIMX51_QUARTC_GPIO	IMX_GPIO_NR(3, 26)
#define CPUIMX51_QUARTD_GPIO	IMX_GPIO_NR(3, 27)
#define CPUIMX51_QUART_XTAL	14745600
#define CPUIMX51_QUART_REGSHIFT	17

/* USB_CTRL_1 */
#define MX51_USB_CTRL_1_OFFSET		0x10
#define MX51_USB_CTRL_UH1_EXT_CLK_EN	(1 << 25)

#define	MX51_USB_PLLDIV_12_MHZ		0x00
#define	MX51_USB_PLL_DIV_19_2_MHZ	0x01
#define	MX51_USB_PLL_DIV_24_MHZ		0x02

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.mapbase = (unsigned long)(MX51_CS1_BASE_ADDR + 0x400000),
		.irq = IMX_GPIO_TO_IRQ(CPUIMX51_QUARTA_GPIO),
		.irqflags = IRQF_TRIGGER_HIGH,
		.uartclk = CPUIMX51_QUART_XTAL,
		.regshift = CPUIMX51_QUART_REGSHIFT,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX51_CS1_BASE_ADDR + 0x800000),
		.irq = IMX_GPIO_TO_IRQ(CPUIMX51_QUARTB_GPIO),
		.irqflags = IRQF_TRIGGER_HIGH,
		.uartclk = CPUIMX51_QUART_XTAL,
		.regshift = CPUIMX51_QUART_REGSHIFT,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX51_CS1_BASE_ADDR + 0x1000000),
		.irq = IMX_GPIO_TO_IRQ(CPUIMX51_QUARTC_GPIO),
		.irqflags = IRQF_TRIGGER_HIGH,
		.uartclk = CPUIMX51_QUART_XTAL,
		.regshift = CPUIMX51_QUART_REGSHIFT,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX51_CS1_BASE_ADDR + 0x2000000),
		.irq = IMX_GPIO_TO_IRQ(CPUIMX51_QUARTD_GPIO),
		.irqflags = IRQF_TRIGGER_HIGH,
		.uartclk = CPUIMX51_QUART_XTAL,
		.regshift = CPUIMX51_QUART_REGSHIFT,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
	}
};

static struct platform_device serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = serial_platform_data,
	},
};

static struct platform_device *devices[] __initdata = {
	&serial_device,
};

static iomux_v3_cfg_t eukrea_cpuimx51_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* I2C2 */
	MX51_PAD_GPIO1_2__I2C2_SCL,
	MX51_PAD_GPIO1_3__I2C2_SDA,
	MX51_PAD_NANDF_D10__GPIO3_30,

	/* QUART IRQ */
	MX51_PAD_NANDF_D15__GPIO3_25,
	MX51_PAD_NANDF_D14__GPIO3_26,
	MX51_PAD_NANDF_D13__GPIO3_27,
	MX51_PAD_NANDF_D12__GPIO3_28,

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
};

static const struct mxc_nand_platform_data
		eukrea_cpuimx51_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
	.flash_bbt	= 1,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const
struct imxi2c_platform_data eukrea_cpuimx51_i2c_data __initconst = {
	.bitrate = 100000,
};

static struct i2c_board_info eukrea_cpuimx51_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

/* This function is board specific as the bit mask for the plldiv will also
be different for other Freescale SoCs, thus a common bitmask is not
possible and cannot get place in /plat-mxc/ehci.c.*/
static int initialize_otg_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;

	usb_base = ioremap(MX51_USB_OTG_BASE_ADDR, SZ_4K);
	if (!usb_base)
		return -ENOMEM;
	usbother_base = usb_base + MX5_USBOTHER_REGS_OFFSET;

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_19_2_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);

	mdelay(10);

	return mx51_initialize_usb_hw(0, MXC_EHCI_INTERNAL_PHY);
}

static int initialize_usbh1_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;

	usb_base = ioremap(MX51_USB_OTG_BASE_ADDR, SZ_4K);
	if (!usb_base)
		return -ENOMEM;
	usbother_base = usb_base + MX5_USBOTHER_REGS_OFFSET;

	/* The clock for the USBH1 ULPI port will come externally from the PHY. */
	v = __raw_readl(usbother_base + MX51_USB_CTRL_1_OFFSET);
	__raw_writel(v | MX51_USB_CTRL_UH1_EXT_CLK_EN, usbother_base + MX51_USB_CTRL_1_OFFSET);
	iounmap(usb_base);

	mdelay(10);

	return mx51_initialize_usb_hw(1, MXC_EHCI_POWER_PINS_ENABLED |
			MXC_EHCI_ITC_NO_THRESHOLD);
}

static const struct mxc_usbh_platform_data dr_utmi_config __initconst = {
	.init		= initialize_otg_port,
	.portsc	= MXC_EHCI_UTMI_16BIT,
};

static const struct fsl_usb2_platform_data usb_pdata __initconst = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI_WIDE,
};

static const struct mxc_usbh_platform_data usbh1_config __initconst = {
	.init		= initialize_usbh1_port,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static int otg_mode_host;

static int __init eukrea_cpuimx51_otg_mode(char *options)
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
__setup("otg_mode=", eukrea_cpuimx51_otg_mode);

/*
 * Board specific initialization.
 */
static void __init eukrea_cpuimx51_init(void)
{
	imx51_soc_init();

	mxc_iomux_v3_setup_multiple_pads(eukrea_cpuimx51_pads,
					ARRAY_SIZE(eukrea_cpuimx51_pads));

	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_mxc_nand(&eukrea_cpuimx51_nand_board_info);

	gpio_request(CPUIMX51_QUARTA_GPIO, "quarta_irq");
	gpio_direction_input(CPUIMX51_QUARTA_GPIO);
	gpio_free(CPUIMX51_QUARTA_GPIO);
	gpio_request(CPUIMX51_QUARTB_GPIO, "quartb_irq");
	gpio_direction_input(CPUIMX51_QUARTB_GPIO);
	gpio_free(CPUIMX51_QUARTB_GPIO);
	gpio_request(CPUIMX51_QUARTC_GPIO, "quartc_irq");
	gpio_direction_input(CPUIMX51_QUARTC_GPIO);
	gpio_free(CPUIMX51_QUARTC_GPIO);
	gpio_request(CPUIMX51_QUARTD_GPIO, "quartd_irq");
	gpio_direction_input(CPUIMX51_QUARTD_GPIO);
	gpio_free(CPUIMX51_QUARTD_GPIO);

	imx51_add_fec(NULL);
	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx51_add_imx_i2c(1, &eukrea_cpuimx51_i2c_data);
	i2c_register_board_info(1, eukrea_cpuimx51_i2c_devices,
				ARRAY_SIZE(eukrea_cpuimx51_i2c_devices));

	if (otg_mode_host)
		imx51_add_mxc_ehci_otg(&dr_utmi_config);
	else {
		initialize_otg_port(NULL);
		imx51_add_fsl_usb2_udc(&usb_pdata);
	}
	imx51_add_mxc_ehci_hs(1, &usbh1_config);

#ifdef CONFIG_MACH_EUKREA_MBIMX51_BASEBOARD
	eukrea_mbimx51_baseboard_init();
#endif
}

static void __init eukrea_cpuimx51_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mxc_timer = {
	.init	= eukrea_cpuimx51_timer_init,
};

MACHINE_START(EUKREA_CPUIMX51, "Eukrea CPUIMX51 Module")
	/* Maintainer: Eric Bénard <eric@eukrea.com> */
	.atag_offset = 0x100,
	.map_io = mx51_map_io,
	.init_early = imx51_init_early,
	.init_irq = mx51_init_irq,
	.handle_irq = imx51_handle_irq,
	.timer = &mxc_timer,
	.init_machine = eukrea_cpuimx51_init,
MACHINE_END
