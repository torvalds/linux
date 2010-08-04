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
#include <linux/fec.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/fsl_devices.h>

#include <mach/eukrea-baseboards.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/mx25.h>
#include <mach/mxc_nand.h>
#include <mach/imxfb.h>
#include <mach/mxc_ehci.h>
#include <mach/ulpi.h>
#include <mach/iomux-mx25.h>

#include "devices-imx25.h"
#include "devices.h"

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct pad_desc eukrea_cpuimx25_pads[] = {
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

static struct fec_platform_data mx25_fec_pdata = {
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

static struct mxc_usbh_platform_data otg_pdata = {
	.portsc	= MXC_EHCI_MODE_UTMI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};

static struct mxc_usbh_platform_data usbh2_pdata = {
	.portsc	= MXC_EHCI_MODE_SERIAL,
	.flags	= MXC_EHCI_INTERFACE_SINGLE_UNI | MXC_EHCI_INTERNAL_PHY |
		  MXC_EHCI_IPPUE_DOWN,
};

static struct fsl_usb2_platform_data otg_device_pdata = {
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
	if (mxc_iomux_v3_setup_multiple_pads(eukrea_cpuimx25_pads,
			ARRAY_SIZE(eukrea_cpuimx25_pads)))
		printk(KERN_ERR "error setting cpuimx25 pads !\n");

	imx25_add_imx_uart0(&uart_pdata);
	imx25_add_mxc_nand(&eukrea_cpuimx25_nand_board_info);
	mxc_register_device(&mx25_rtc_device, NULL);
	mxc_register_device(&mx25_fec_device, &mx25_fec_pdata);

	i2c_register_board_info(0, eukrea_cpuimx25_i2c_devices,
				ARRAY_SIZE(eukrea_cpuimx25_i2c_devices));
	imx25_add_imx_i2c0(&eukrea_cpuimx25_i2c0_data);

#if defined(CONFIG_USB_ULPI)
	if (otg_mode_host) {
		otg_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				USB_OTG_DRV_VBUS | USB_OTG_DRV_VBUS_EXT);

		mxc_register_device(&mxc_otg, &otg_pdata);
	}
	mxc_register_device(&mxc_usbh2, &usbh2_pdata);
#endif
	if (!otg_mode_host)
		mxc_register_device(&otg_udc_device, &otg_device_pdata);

#ifdef CONFIG_MACH_EUKREA_MBIMXSD_BASEBOARD
	eukrea_mbimxsd_baseboard_init();
#endif
}

static void __init eukrea_cpuimx25_timer_init(void)
{
	mx25_clocks_init();
}

static struct sys_timer eukrea_cpuimx25_timer = {
	.init   = eukrea_cpuimx25_timer_init,
};

MACHINE_START(EUKREA_CPUIMX25, "Eukrea CPUIMX25")
	/* Maintainer: Eukrea Electromatique */
	.phys_io	= MX25_AIPS1_BASE_ADDR,
	.io_pg_offst	= ((MX25_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX25_PHYS_OFFSET + 0x100,
	.map_io         = mx25_map_io,
	.init_irq       = mx25_init_irq,
	.init_machine   = eukrea_cpuimx25_init,
	.timer          = &eukrea_cpuimx25_timer,
MACHINE_END
