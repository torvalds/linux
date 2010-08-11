/*
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
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/fsl_devices.h>
#include <linux/fec.h>

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

#define BABBAGE_USB_HUB_RESET	(0*32 + 7)	/* GPIO_1_7 */
#define BABBAGE_USBH1_STP	(0*32 + 27)	/* GPIO_1_27 */
#define BABBAGE_PHY_RESET	(1*32 + 5)	/* GPIO_2_5 */
#define BABBAGE_FEC_PHY_RESET	(1*32 + 14)	/* GPIO_2_14 */

/* USB_CTRL_1 */
#define MX51_USB_CTRL_1_OFFSET			0x10
#define MX51_USB_CTRL_UH1_EXT_CLK_EN		(1 << 25)

#define	MX51_USB_PLLDIV_12_MHZ		0x00
#define	MX51_USB_PLL_DIV_19_2_MHZ	0x01
#define	MX51_USB_PLL_DIV_24_MHZ	0x02

static struct platform_device *devices[] __initdata = {
	&mxc_fec_device,
};

static struct pad_desc mx51babbage_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* UART2 */
	MX51_PAD_UART2_RXD__UART2_RXD,
	MX51_PAD_UART2_TXD__UART2_TXD,

	/* UART3 */
	MX51_PAD_EIM_D25__UART3_RXD,
	MX51_PAD_EIM_D26__UART3_TXD,
	MX51_PAD_EIM_D27__UART3_RTS,
	MX51_PAD_EIM_D24__UART3_CTS,

	/* I2C1 */
	MX51_PAD_EIM_D16__I2C1_SDA,
	MX51_PAD_EIM_D19__I2C1_SCL,

	/* I2C2 */
	MX51_PAD_KEY_COL4__I2C2_SCL,
	MX51_PAD_KEY_COL5__I2C2_SDA,

	/* HSI2C */
	MX51_PAD_I2C1_CLK__HSI2C_CLK,
	MX51_PAD_I2C1_DAT__HSI2C_DAT,

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

	/* USB HUB reset line*/
	MX51_PAD_GPIO_1_7__GPIO_1_7,

	/* FEC */
	MX51_PAD_EIM_EB2__FEC_MDIO,
	MX51_PAD_EIM_EB3__FEC_RDAT1,
	MX51_PAD_EIM_CS2__FEC_RDAT2,
	MX51_PAD_EIM_CS3__FEC_RDAT3,
	MX51_PAD_EIM_CS4__FEC_RX_ER,
	MX51_PAD_EIM_CS5__FEC_CRS,
	MX51_PAD_NANDF_RB2__FEC_COL,
	MX51_PAD_NANDF_RB3__FEC_RXCLK,
	MX51_PAD_NANDF_RB6__FEC_RDAT0,
	MX51_PAD_NANDF_RB7__FEC_TDAT0,
	MX51_PAD_NANDF_CS2__FEC_TX_ER,
	MX51_PAD_NANDF_CS3__FEC_MDC,
	MX51_PAD_NANDF_CS4__FEC_TDAT1,
	MX51_PAD_NANDF_CS5__FEC_TDAT2,
	MX51_PAD_NANDF_CS6__FEC_TDAT3,
	MX51_PAD_NANDF_CS7__FEC_TX_EN,
	MX51_PAD_NANDF_RDY_INT__FEC_TX_CLK,

	/* FEC PHY reset line */
	MX51_PAD_EIM_A20__GPIO_2_14,
};

/* Serial ports */
#if defined(CONFIG_SERIAL_IMX) || defined(CONFIG_SERIAL_IMX_MODULE)
static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static inline void mxc_init_imx_uart(void)
{
	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_imx_uart(1, &uart_pdata);
	imx51_add_imx_uart(2, &uart_pdata);
}
#else /* !SERIAL_IMX */
static inline void mxc_init_imx_uart(void)
{
}
#endif /* SERIAL_IMX */

static const struct imxi2c_platform_data babbage_i2c_data __initconst = {
	.bitrate = 100000,
};

static struct imxi2c_platform_data babbage_hsi2c_data = {
	.bitrate = 400000,
};

static int gpio_usbh1_active(void)
{
	struct pad_desc usbh1stp_gpio = MX51_PAD_USBH1_STP__GPIO_1_27;
	struct pad_desc phyreset_gpio = MX51_PAD_EIM_D21__GPIO_2_5;
	int ret;

	/* Set USBH1_STP to GPIO and toggle it */
	mxc_iomux_v3_setup_pad(&usbh1stp_gpio);
	ret = gpio_request(BABBAGE_USBH1_STP, "usbh1_stp");

	if (ret) {
		pr_debug("failed to get MX51_PAD_USBH1_STP__GPIO_1_27: %d\n", ret);
		return ret;
	}
	gpio_direction_output(BABBAGE_USBH1_STP, 0);
	gpio_set_value(BABBAGE_USBH1_STP, 1);
	msleep(100);
	gpio_free(BABBAGE_USBH1_STP);

	/* De-assert USB PHY RESETB */
	mxc_iomux_v3_setup_pad(&phyreset_gpio);
	ret = gpio_request(BABBAGE_PHY_RESET, "phy_reset");

	if (ret) {
		pr_debug("failed to get MX51_PAD_EIM_D21__GPIO_2_5: %d\n", ret);
		return ret;
	}
	gpio_direction_output(BABBAGE_PHY_RESET, 1);
	return 0;
}

static inline void babbage_usbhub_reset(void)
{
	int ret;

	/* Bring USB hub out of reset */
	ret = gpio_request(BABBAGE_USB_HUB_RESET, "GPIO1_7");
	if (ret) {
		printk(KERN_ERR"failed to get GPIO_USB_HUB_RESET: %d\n", ret);
		return;
	}
	gpio_direction_output(BABBAGE_USB_HUB_RESET, 0);

	/* USB HUB RESET - De-assert USB HUB RESET_N */
	msleep(1);
	gpio_set_value(BABBAGE_USB_HUB_RESET, 0);
	msleep(1);
	gpio_set_value(BABBAGE_USB_HUB_RESET, 1);
}

static inline void babbage_fec_reset(void)
{
	int ret;

	/* reset FEC PHY */
	ret = gpio_request(BABBAGE_FEC_PHY_RESET, "fec-phy-reset");
	if (ret) {
		printk(KERN_ERR"failed to get GPIO_FEC_PHY_RESET: %d\n", ret);
		return;
	}
	gpio_direction_output(BABBAGE_FEC_PHY_RESET, 0);
	gpio_set_value(BABBAGE_FEC_PHY_RESET, 0);
	msleep(1);
	gpio_set_value(BABBAGE_FEC_PHY_RESET, 1);
}

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

	/* The clock for the USBH1 ULPI port will come externally from the PHY. */
	v = __raw_readl(usbother_base + MX51_USB_CTRL_1_OFFSET);
	__raw_writel(v | MX51_USB_CTRL_UH1_EXT_CLK_EN, usbother_base + MX51_USB_CTRL_1_OFFSET);
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

static int __init babbage_otg_mode(char *options)
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
__setup("otg_mode=", babbage_otg_mode);

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	struct pad_desc usbh1stp = MX51_PAD_USBH1_STP__USBH1_STP;

	mxc_iomux_v3_setup_multiple_pads(mx51babbage_pads,
					ARRAY_SIZE(mx51babbage_pads));
	mxc_init_imx_uart();
	babbage_fec_reset();
	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx51_add_imx_i2c(0, &babbage_i2c_data);
	imx51_add_imx_i2c(1, &babbage_i2c_data);
	mxc_register_device(&mxc_hsi2c_device, &babbage_hsi2c_data);

	if (otg_mode_host)
		mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	else {
		initialize_otg_port(NULL);
		mxc_register_device(&mxc_usbdr_udc_device, &usb_pdata);
	}

	gpio_usbh1_active();
	mxc_register_device(&mxc_usbh1_device, &usbh1_config);
	/* setback USBH1_STP to be function */
	mxc_iomux_v3_setup_pad(&usbh1stp);
	babbage_usbhub_reset();
}

static void __init mx51_babbage_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mxc_timer = {
	.init	= mx51_babbage_timer_init,
};

MACHINE_START(MX51_BABBAGE, "Freescale MX51 Babbage Board")
	/* Maintainer: Amit Kucheria <amit.kucheria@canonical.com> */
	.phys_io = MX51_AIPS1_BASE_ADDR,
	.io_pg_offst = ((MX51_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params = MX51_PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_irq = mx51_init_irq,
	.init_machine = mxc_board_init,
	.timer = &mxc_timer,
MACHINE_END
