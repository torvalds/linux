/*
 * Copyright (C) 2010 Linaro Limited
 *
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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/fsl_devices.h>

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

#define	MX51_USB_PLL_DIV_24_MHZ	0x01

static struct pad_desc mx51efikamx_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,
};

/* Serial ports */
#if defined(CONFIG_SERIAL_IMX) || defined(CONFIG_SERIAL_IMX_MODULE)
static const struct imxuart_platform_data uart_pdata = {
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
	usbother_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_24_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);
	return 0;
}

static struct mxc_usbh_platform_data dr_utmi_config = {
	.init   = initialize_otg_port,
	.portsc = MXC_EHCI_UTMI_16BIT,
	.flags  = MXC_EHCI_INTERNAL_PHY,
};

static void __init mxc_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efikamx_pads,
					ARRAY_SIZE(mx51efikamx_pads));
	mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	mxc_init_imx_uart();
}

static void __init mx51_efikamx_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 24576000);
}

static struct sys_timer mxc_timer = {
	.init	= mx51_efikamx_timer_init,
};

MACHINE_START(MX51_EFIKAMX, "Genesi EfikaMX nettop")
	/* Maintainer: Amit Kucheria <amit.kucheria@linaro.org> */
	.boot_params = MX51_PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_irq = mx51_init_irq,
	.init_machine =  mxc_board_init,
	.timer = &mxc_timer,
MACHINE_END
