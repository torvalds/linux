/*
 * arch/arm/mach-spear3xx/spear310_evb.c
 *
 * SPEAr310 evaluation board source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <mach/generic.h>
#include <mach/spear.h>

static struct amba_device *amba_devs[] __initdata = {
	/* spear3xx specific devices */
	&gpio_device,
	&uart_device,

	/* spear310 specific devices */
};

static struct platform_device *plat_devs[] __initdata = {
	/* spear3xx specific devices */

	/* spear310 specific devices */
};

static void __init spear310_evb_init(void)
{
	unsigned int i;

	/* call spear310 machine init function */
	spear310_init();

	/* Add Platform Devices */
	platform_add_devices(plat_devs, ARRAY_SIZE(plat_devs));

	/* Add Amba Devices */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);
}

MACHINE_START(SPEAR310, "ST-SPEAR310-EVB")
	.boot_params	=	0x00000100,
	.map_io		=	spear3xx_map_io,
	.init_irq	=	spear3xx_init_irq,
	.timer		=	&spear_sys_timer,
	.init_machine	=	spear310_evb_init,
MACHINE_END
