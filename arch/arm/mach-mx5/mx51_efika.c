/*
 * based on code from the following
 * Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Pegatron Corporation. All Rights Reserved.
 * Copyright 2009-2010 Genesi USA, Inc. All Rights Reserved.
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
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/fsl_devices.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>
#include <mach/i2c.h>
#include <mach/mxc_ehci.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <mach/ulpi.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "devices.h"
#include "efika.h"

#define MX51_USB_CTRL_1_OFFSET          0x10
#define MX51_USB_CTRL_UH1_EXT_CLK_EN    (1 << 25)
#define	MX51_USB_PLL_DIV_19_2_MHZ	0x01

#define EFIKAMX_USB_HUB_RESET	IMX_GPIO_NR(1, 5)
#define EFIKAMX_USBH1_STP	IMX_GPIO_NR(1, 27)

#define EFIKAMX_SPI_CS0		IMX_GPIO_NR(4, 24)
#define EFIKAMX_SPI_CS1		IMX_GPIO_NR(4, 25)

static iomux_v3_cfg_t mx51efika_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* SD 1 */
	MX51_PAD_SD1_CMD__SD1_CMD,
	MX51_PAD_SD1_CLK__SD1_CLK,
	MX51_PAD_SD1_DATA0__SD1_DATA0,
	MX51_PAD_SD1_DATA1__SD1_DATA1,
	MX51_PAD_SD1_DATA2__SD1_DATA2,
	MX51_PAD_SD1_DATA3__SD1_DATA3,

	/* SD 2 */
	MX51_PAD_SD2_CMD__SD2_CMD,
	MX51_PAD_SD2_CLK__SD2_CLK,
	MX51_PAD_SD2_DATA0__SD2_DATA0,
	MX51_PAD_SD2_DATA1__SD2_DATA1,
	MX51_PAD_SD2_DATA2__SD2_DATA2,
	MX51_PAD_SD2_DATA3__SD2_DATA3,

	/* SD/MMC WP/CD */
	MX51_PAD_GPIO1_0__SD1_CD,
	MX51_PAD_GPIO1_1__SD1_WP,
	MX51_PAD_GPIO1_7__SD2_WP,
	MX51_PAD_GPIO1_8__SD2_CD,

	/* spi */
	MX51_PAD_CSPI1_MOSI__ECSPI1_MOSI,
	MX51_PAD_CSPI1_MISO__ECSPI1_MISO,
	MX51_PAD_CSPI1_SS0__GPIO4_24,
	MX51_PAD_CSPI1_SS1__GPIO4_25,
	MX51_PAD_CSPI1_RDY__ECSPI1_RDY,
	MX51_PAD_CSPI1_SCLK__ECSPI1_SCLK,

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

	/* USB HUB RESET */
	MX51_PAD_GPIO1_5__GPIO1_5,

	/* WLAN */
	MX51_PAD_EIM_A22__GPIO2_16,
	MX51_PAD_EIM_A16__GPIO2_10,

	/* USB PHY RESET */
	MX51_PAD_EIM_D27__GPIO2_9,
};

/* Serial ports */
static const struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/* This function is board specific as the bit mask for the plldiv will also
 * be different for other Freescale SoCs, thus a common bitmask is not
 * possible and cannot get place in /plat-mxc/ehci.c.
 */
static int initialize_otg_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;
	usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);
	if (!usb_base)
		return -ENOMEM;
	usbother_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_19_2_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);

	mdelay(10);

	return mx51_initialize_usb_hw(pdev->id, MXC_EHCI_INTERNAL_PHY);
}

static struct mxc_usbh_platform_data dr_utmi_config = {
	.init   = initialize_otg_port,
	.portsc = MXC_EHCI_UTMI_16BIT,
};

static int initialize_usbh1_port(struct platform_device *pdev)
{
	iomux_v3_cfg_t usbh1stp = MX51_PAD_USBH1_STP__USBH1_STP;
	iomux_v3_cfg_t usbh1gpio = MX51_PAD_USBH1_STP__GPIO1_27;
	u32 v;
	void __iomem *usb_base;
	void __iomem *socregs_base;

	mxc_iomux_v3_setup_pad(usbh1gpio);
	gpio_request(EFIKAMX_USBH1_STP, "usbh1_stp");
	gpio_direction_output(EFIKAMX_USBH1_STP, 0);
	msleep(1);
	gpio_set_value(EFIKAMX_USBH1_STP, 1);
	msleep(1);

	usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);
	socregs_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* The clock for the USBH1 ULPI port will come externally */
	/* from the PHY. */
	v = __raw_readl(socregs_base + MX51_USB_CTRL_1_OFFSET);
	__raw_writel(v | MX51_USB_CTRL_UH1_EXT_CLK_EN,
			socregs_base + MX51_USB_CTRL_1_OFFSET);

	iounmap(usb_base);

	gpio_free(EFIKAMX_USBH1_STP);
	mxc_iomux_v3_setup_pad(usbh1stp);

	mdelay(10);

	return mx51_initialize_usb_hw(0, MXC_EHCI_ITC_NO_THRESHOLD);
}

static struct mxc_usbh_platform_data usbh1_config = {
	.init   = initialize_usbh1_port,
	.portsc = MXC_EHCI_MODE_ULPI,
};

static void mx51_efika_hubreset(void)
{
	gpio_request(EFIKAMX_USB_HUB_RESET, "usb_hub_rst");
	gpio_direction_output(EFIKAMX_USB_HUB_RESET, 1);
	msleep(1);
	gpio_set_value(EFIKAMX_USB_HUB_RESET, 0);
	msleep(1);
	gpio_set_value(EFIKAMX_USB_HUB_RESET, 1);
}

static void __init mx51_efika_usb(void)
{
	mx51_efika_hubreset();

	/* pulling it low, means no USB at all... */
	gpio_request(EFIKA_USB_PHY_RESET, "usb_phy_reset");
	gpio_direction_output(EFIKA_USB_PHY_RESET, 0);
	msleep(1);
	gpio_set_value(EFIKA_USB_PHY_RESET, 1);

	usbh1_config.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				ULPI_OTG_DRVVBUS | ULPI_OTG_DRVVBUS_EXT |
				ULPI_OTG_EXTVBUSIND);

	mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	mxc_register_device(&mxc_usbh1_device, &usbh1_config);
}

static struct mtd_partition mx51_efika_spi_nor_partitions[] = {
	{
	 .name = "u-boot",
	 .offset = 0,
	 .size = SZ_256K,
	},
	{
	  .name = "config",
	  .offset = MTDPART_OFS_APPEND,
	  .size = SZ_64K,
	},
};

static struct flash_platform_data mx51_efika_spi_flash_data = {
	.name		= "spi_flash",
	.parts		= mx51_efika_spi_nor_partitions,
	.nr_parts	= ARRAY_SIZE(mx51_efika_spi_nor_partitions),
	.type		= "sst25vf032b",
};

static struct spi_board_info mx51_efika_spi_board_info[] __initdata = {
	{
		.modalias = "m25p80",
		.max_speed_hz = 25000000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = &mx51_efika_spi_flash_data,
		.irq = -1,
	},
};

static int mx51_efika_spi_cs[] = {
	EFIKAMX_SPI_CS0,
	EFIKAMX_SPI_CS1,
};

static const struct spi_imx_master mx51_efika_spi_pdata __initconst = {
	.chipselect     = mx51_efika_spi_cs,
	.num_chipselect = ARRAY_SIZE(mx51_efika_spi_cs),
};

void __init efika_board_common_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efika_pads,
					ARRAY_SIZE(mx51efika_pads));
	imx51_add_imx_uart(0, &uart_pdata);
	mx51_efika_usb();
	imx51_add_sdhci_esdhc_imx(0, NULL);

	spi_register_board_info(mx51_efika_spi_board_info,
		ARRAY_SIZE(mx51_efika_spi_board_info));
	imx51_add_ecspi(0, &mx51_efika_spi_pdata);
}

