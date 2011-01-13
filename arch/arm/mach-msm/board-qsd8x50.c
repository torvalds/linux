/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/usb/msm_hsusb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/irqs.h>
#include <mach/sirc.h>
#include <mach/gpio.h>

#include "devices.h"

extern struct sys_timer msm_timer;

static const resource_size_t qsd8x50_surf_smc91x_base __initdata = 0x70000300;
static const unsigned        qsd8x50_surf_smc91x_gpio __initdata = 156;

/* Leave smc91x resources empty here, as we'll fill them in
 * at run-time: they vary from board to board, and the true
 * configuration won't be known until boot.
 */
static struct resource smc91x_resources[] __initdata = {
	[0] = {
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device __initdata = {
	.name           = "smc91x",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(smc91x_resources),
	.resource       = smc91x_resources,
};

static int __init msm_init_smc91x(void)
{
	if (machine_is_qsd8x50_surf()) {
		smc91x_resources[0].start = qsd8x50_surf_smc91x_base;
		smc91x_resources[0].end   = qsd8x50_surf_smc91x_base + 0xff;
		smc91x_resources[1].start =
			gpio_to_irq(qsd8x50_surf_smc91x_gpio);
		smc91x_resources[1].end   =
			gpio_to_irq(qsd8x50_surf_smc91x_gpio);
		platform_device_register(&smc91x_device);
	}

	return 0;
}
module_init(msm_init_smc91x);

static int hsusb_phy_init_seq[] = {
	0x08, 0x31,	/* Increase HS Driver Amplitude */
	0x20, 0x32,	/* Enable and set Pre-Emphasis Depth to 10% */
	-1
};

static struct msm_otg_platform_data msm_otg_pdata = {
	.phy_init_seq		= hsusb_phy_init_seq,
	.mode                   = USB_PERIPHERAL,
	.otg_control		= OTG_PHY_CONTROL,
};

static struct platform_device *devices[] __initdata = {
	&msm_device_uart3,
	&msm_device_smd,
	&msm_device_otg,
	&msm_device_hsusb,
	&msm_device_hsusb_host,
};

static void __init qsd8x50_map_io(void)
{
	msm_map_qsd8x50_io();
	msm_clock_init(msm_clocks_8x50, msm_num_clocks_8x50);
}

static void __init qsd8x50_init_irq(void)
{
	msm_init_irq();
	msm_init_sirc();
}

static void __init qsd8x50_init(void)
{
	msm_device_otg.dev.platform_data = &msm_otg_pdata;
	msm_device_hsusb.dev.parent = &msm_device_otg.dev;
	msm_device_hsusb_host.dev.parent = &msm_device_otg.dev;
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(QSD8X50_SURF, "QCT QSD8X50 SURF")
#ifdef CONFIG_MSM_DEBUG_UART
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(QSD8X50A_ST1_5, "QCT QSD8X50A ST1.5")
#ifdef CONFIG_MSM_DEBUG_UART
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END
