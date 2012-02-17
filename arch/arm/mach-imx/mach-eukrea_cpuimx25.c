/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
 * Copyright 2010 Eric Bénard - Eukréa Electromatique, <eric@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include <mach/eukrea-baseboards.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/mx25.h>
#include <mach/iomux-mx25.h>

#include "devices-imx25.h"

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static iomux_v3_cfg_t eukrea_cpuimx25_pads[] = {
	/* FEC - RMII */
	MX25_PAD_FEC_MDC__FEC_MDC,
	MX25_PAD_FEC_MDIO__FEC_MDIO,
	MX25_PAD_FEC_TDATA0__FEC_TDATA0,
	MX25_PAD_FEC_TDATA1__FEC_TDATA1,
	MX25_PAD_FEC_TX_EN__FEC_TX_EN,
	MX25_PAD_FEC_RDATA0__FEC_RDATA0,
	MX25_PAD_FEC_RDATA1__FEC_RDATA1,
	MX25_PAD_FEC_RX_DV__FEC_RX_DV,
	MX25_PAD_FEC_TX_CLK__FEC_TX_CLK,
	/* I2C1 */
	MX25_PAD_I2C1_CLK__I2C1_CLK,
	MX25_PAD_I2C1_DAT__I2C1_DAT,
};

static const struct fec_platform_data mx25_fec_pdata __initconst = {
	.phy	= PHY_INTERFACE_MODE_RMII,
};

static const struct mxc_nand_platform_data
eukrea_cpuimx25_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
	.flash_bbt	= 1,
};

static const struct imxi2c_platform_data
eukrea_cpuimx25_i2c0_data __initconst = {
	.bitrate = 100000,
};

static struct i2c_board_info eukrea_cpuimx25_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

static int eukrea_cpuimx25_otg_init(struct platform_device *pdev)
{
	return mx25_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static const struct mxc_usbh_platform_data otg_pdata __initconst = {
	.init	= eukrea_cpuimx25_otg_init,
	.portsc	= MXC_EHCI_MODE_UTMI,
};

static int eukrea_cpuimx25_usbh2_init(struct platform_device *pdev)
{
	return mx25_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_SINGLE_UNI |
			MXC_EHCI_INTERNAL_PHY | MXC_EHCI_IPPUE_DOWN);
}

static const struct mxc_usbh_platform_data usbh2_pdata __initconst = {
	.init	= eukrea_cpuimx25_usbh2_init,
	.portsc	= MXC_EHCI_MODE_SERIAL,
};

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_UTMI,
};

static int otg_mode_host;

static int __init eukrea_cpuimx25_otg_mode(char *options)
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
__setup("otg_mode=", eukrea_cpuimx25_otg_mode);

static void __init eukrea_cpuimx25_init(void)
{
	imx25_soc_init();

	if (mxc_iomux_v3_setup_multiple_pads(eukrea_cpuimx25_pads,
			ARRAY_SIZE(eukrea_cpuimx25_pads)))
		printk(KERN_ERR "error setting cpuimx25 pads !\n");

	imx25_add_imx_uart0(&uart_pdata);
	imx25_add_mxc_nand(&eukrea_cpuimx25_nand_board_info);
	imx25_add_imxdi_rtc(NULL);
	imx25_add_fec(&mx25_fec_pdata);

	i2c_register_board_info(0, eukrea_cpuimx25_i2c_devices,
				ARRAY_SIZE(eukrea_cpuimx25_i2c_devices));
	imx25_add_imx_i2c0(&eukrea_cpuimx25_i2c0_data);

	if (otg_mode_host)
		imx25_add_mxc_ehci_otg(&otg_pdata);
	else
		imx25_add_fsl_usb2_udc(&otg_device_pdata);

	imx25_add_mxc_ehci_hs(&usbh2_pdata);

#ifdef CONFIG_MACH_EUKREA_MBIMXSD25_BASEBOARD
	eukrea_mbimxsd25_baseboard_init();
#endif
}

static void __init eukrea_cpuimx25_timer_init(void)
{
	mx25_clocks_init();
}

static struct sys_timer eukrea_cpuimx25_timer = {
	.init   = eukrea_cpuimx25_timer_init,
};

MACHINE_START(EUKREA_CPUIMX25SD, "Eukrea CPUIMX25")
	/* Maintainer: Eukrea Electromatique */
	.atag_offset = 0x100,
	.map_io = mx25_map_io,
	.init_early = imx25_init_early,
	.init_irq = mx25_init_irq,
	.handle_irq = imx25_handle_irq,
	.timer = &eukrea_cpuimx25_timer,
	.init_machine = eukrea_cpuimx25_init,
	.restart	= mxc_restart,
MACHINE_END
