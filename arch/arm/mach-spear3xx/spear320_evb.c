/*
 * arch/arm/mach-spear3xx/spear320_evb.c
 *
 * SPEAr320 evaluation board source file
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
#include <mach/hardware.h>

/* padmux devices to enable */
static struct pmx_dev *pmx_devs[] = {
	/* spear3xx specific devices */
	&spear3xx_pmx_i2c,
	&spear3xx_pmx_ssp,
	&spear3xx_pmx_mii,
	&spear3xx_pmx_uart0,

	/* spear320 specific devices */
	&spear320_pmx_fsmc,
	&spear320_pmx_sdhci,
	&spear320_pmx_i2s,
	&spear320_pmx_uart1,
	&spear320_pmx_uart2,
	&spear320_pmx_can,
	&spear320_pmx_pwm0,
	&spear320_pmx_pwm1,
	&spear320_pmx_pwm2,
	&spear320_pmx_mii1,
};

static struct amba_device *amba_devs[] __initdata = {
	/* spear3xx specific devices */
	&spear3xx_gpio_device,
	&spear3xx_uart_device,

	/* spear320 specific devices */
};

static struct platform_device *plat_devs[] __initdata = {
	/* spear3xx specific devices */

	/* spear320 specific devices */
};

static void __init spear320_evb_init(void)
{
	unsigned int i;

	/* call spear320 machine init function */
	spear320_init(&spear320_auto_net_mii_mode, pmx_devs,
			ARRAY_SIZE(pmx_devs));

	/* Add Platform Devices */
	platform_add_devices(plat_devs, ARRAY_SIZE(plat_devs));

	/* Add Amba Devices */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);
}

MACHINE_START(SPEAR320, "ST-SPEAR320-EVB")
	.boot_params	=	0x00000100,
	.map_io		=	spear3xx_map_io,
	.init_irq	=	spear3xx_init_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear320_evb_init,
MACHINE_END
