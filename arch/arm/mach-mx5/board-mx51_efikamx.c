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

#define EFIKAMX_PCBID0		(2*32 + 16)
#define EFIKAMX_PCBID1		(2*32 + 17)
#define EFIKAMX_PCBID2		(2*32 + 11)

/* the pci ids pin have pull up. they're driven low according to board id */
#define MX51_PAD_PCBID0	IOMUX_PAD(0x518, 0x130, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID1	IOMUX_PAD(0x51C, 0x134, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID2	IOMUX_PAD(0x504, 0x128, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)

static iomux_v3_cfg_t mx51efikamx_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,
	/* board id */
	MX51_PAD_PCBID0,
	MX51_PAD_PCBID1,
	MX51_PAD_PCBID2,
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

/*   PCBID2  PCBID1 PCBID0  STATE
	1       1      1    ER1:rev1.1
	1       1      0    ER2:rev1.2
	1       0      1    ER3:rev1.3
	1       0      0    ER4:rev1.4
*/
static void __init mx51_efikamx_board_id(void)
{
	int id;

	/* things are taking time to settle */
	msleep(150);

	gpio_request(EFIKAMX_PCBID0, "pcbid0");
	gpio_direction_input(EFIKAMX_PCBID0);
	gpio_request(EFIKAMX_PCBID1, "pcbid1");
	gpio_direction_input(EFIKAMX_PCBID1);
	gpio_request(EFIKAMX_PCBID2, "pcbid2");
	gpio_direction_input(EFIKAMX_PCBID2);

	id = gpio_get_value(EFIKAMX_PCBID0);
	id |= gpio_get_value(EFIKAMX_PCBID1) << 1;
	id |= gpio_get_value(EFIKAMX_PCBID2) << 2;

	switch (id) {
	case 7:
		system_rev = 0x11;
		break;
	case 6:
		system_rev = 0x12;
		break;
	case 5:
		system_rev = 0x13;
		break;
	case 4:
		system_rev = 0x14;
		break;
	default:
		system_rev = 0x10;
		break;
	}

	if ((system_rev == 0x10)
		|| (system_rev == 0x12)
		|| (system_rev == 0x14)) {
		printk(KERN_WARNING
			"EfikaMX: Unsupported board revision 1.%u!\n",
			system_rev & 0xf);
	}
}

static void __init mxc_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efikamx_pads,
					ARRAY_SIZE(mx51efikamx_pads));
	mx51_efikamx_board_id();
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
