/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2010 Yong Shen. <Yong.Shen@linaro.org>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/fec.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx53.h>

#define SMD_FEC_PHY_RST		IMX_GPIO_NR(7, 6)
#define EVK_ECSPI1_CS0		IMX_GPIO_NR(2, 30)
#define EVK_ECSPI1_CS1		IMX_GPIO_NR(3, 19)

#include "crm_regs.h"
#include "devices-imx53.h"

static iomux_v3_cfg_t mx53_evk_pads[] = {
	MX53_PAD_CSI0_D10__UART1_TXD,
	MX53_PAD_CSI0_D11__UART1_RXD,
	MX53_PAD_ATA_DIOW__UART1_TXD,
	MX53_PAD_ATA_DMACK__UART1_RXD,

	MX53_PAD_ATA_BUFFER_EN__UART2_RXD,
	MX53_PAD_ATA_DMARQ__UART2_TXD,
	MX53_PAD_ATA_DIOR__UART2_RTS,
	MX53_PAD_ATA_INTRQ__UART2_CTS,

	MX53_PAD_ATA_CS_0__UART3_TXD,
	MX53_PAD_ATA_CS_1__UART3_RXD,
	MX53_PAD_ATA_DA_1__UART3_CTS,
	MX53_PAD_ATA_DA_2__UART3_RTS,

	MX53_PAD_EIM_D16__CSPI1_SCLK,
	MX53_PAD_EIM_D17__CSPI1_MISO,
	MX53_PAD_EIM_D18__CSPI1_MOSI,

	/* ecspi chip select lines */
	MX53_PAD_EIM_EB2__GPIO_2_30,
	MX53_PAD_EIM_D19__GPIO_3_19,
};

static const struct imxuart_platform_data mx53_evk_uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static inline void mx53_evk_init_uart(void)
{
	imx53_add_imx_uart(0, &mx53_evk_uart_pdata);
	imx53_add_imx_uart(1, &mx53_evk_uart_pdata);
	imx53_add_imx_uart(2, &mx53_evk_uart_pdata);
}

static const struct imxi2c_platform_data mx53_evk_i2c_data __initconst = {
	.bitrate = 100000,
};

static inline void mx53_evk_fec_reset(void)
{
	int ret;

	/* reset FEC PHY */
	ret = gpio_request(SMD_FEC_PHY_RST, "fec-phy-reset");
	if (ret) {
		printk(KERN_ERR"failed to get GPIO_FEC_PHY_RESET: %d\n", ret);
		return;
	}
	gpio_direction_output(SMD_FEC_PHY_RST, 0);
	gpio_set_value(SMD_FEC_PHY_RST, 0);
	msleep(1);
	gpio_set_value(SMD_FEC_PHY_RST, 1);
}

static struct fec_platform_data mx53_evk_fec_pdata = {
	.phy = PHY_INTERFACE_MODE_RMII,
};

static struct spi_board_info mx53_evk_spi_board_info[] __initdata = {
	{
		.modalias = "mtd_dataflash",
		.max_speed_hz = 25000000,
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_MODE_0,
		.platform_data = NULL,
	},
};

static int mx53_evk_spi_cs[] = {
	EVK_ECSPI1_CS0,
	EVK_ECSPI1_CS1,
};

static const struct spi_imx_master mx53_evk_spi_data __initconst = {
	.chipselect     = mx53_evk_spi_cs,
	.num_chipselect = ARRAY_SIZE(mx53_evk_spi_cs),
};

static void __init mx53_evk_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx53_evk_pads,
					ARRAY_SIZE(mx53_evk_pads));
	mx53_evk_init_uart();
	mx53_evk_fec_reset();
	imx53_add_fec(&mx53_evk_fec_pdata);

	imx53_add_imx_i2c(0, &mx53_evk_i2c_data);
	imx53_add_imx_i2c(1, &mx53_evk_i2c_data);

	imx53_add_sdhci_esdhc_imx(0, NULL);
	imx53_add_sdhci_esdhc_imx(1, NULL);

	spi_register_board_info(mx53_evk_spi_board_info,
		ARRAY_SIZE(mx53_evk_spi_board_info));
	imx53_add_ecspi(0, &mx53_evk_spi_data);
}

static void __init mx53_evk_timer_init(void)
{
	mx53_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mx53_evk_timer = {
	.init	= mx53_evk_timer_init,
};

MACHINE_START(MX53_EVK, "Freescale MX53 EVK Board")
	.map_io = mx53_map_io,
	.init_irq = mx53_init_irq,
	.init_machine = mx53_evk_board_init,
	.timer = &mx53_evk_timer,
MACHINE_END
