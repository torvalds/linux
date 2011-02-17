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

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "devices.h"
#include "efika.h"

#define	MX51_USB_PLL_DIV_24_MHZ	0x01

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
	v |= MX51_USB_PLL_DIV_24_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);

	mdelay(10);

	return mx51_initialize_usb_hw(0, MXC_EHCI_INTERNAL_PHY);
}

static struct mxc_usbh_platform_data dr_utmi_config = {
	.init   = initialize_otg_port,
	.portsc = MXC_EHCI_UTMI_16BIT,
};

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
	mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_sdhci_esdhc_imx(0, NULL);

	spi_register_board_info(mx51_efika_spi_board_info,
		ARRAY_SIZE(mx51_efika_spi_board_info));
	imx51_add_ecspi(0, &mx51_efika_spi_pdata);
}

