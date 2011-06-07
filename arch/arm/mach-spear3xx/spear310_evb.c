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
#include <mach/hardware.h>

/* padmux devices to enable */
static struct pmx_dev *pmx_devs[] = {
	/* spear3xx specific devices */
	&spear3xx_pmx_i2c,
	&spear3xx_pmx_ssp,
	&spear3xx_pmx_gpio_pin0,
	&spear3xx_pmx_gpio_pin1,
	&spear3xx_pmx_gpio_pin2,
	&spear3xx_pmx_gpio_pin3,
	&spear3xx_pmx_gpio_pin4,
	&spear3xx_pmx_gpio_pin5,
	&spear3xx_pmx_uart0,

	/* spear310 specific devices */
	&spear310_pmx_emi_cs_0_1_4_5,
	&spear310_pmx_emi_cs_2_3,
	&spear310_pmx_uart1,
	&spear310_pmx_uart2,
	&spear310_pmx_uart3_4_5,
	&spear310_pmx_fsmc,
	&spear310_pmx_rs485_0_1,
	&spear310_pmx_tdm0,
};

static struct amba_device *amba_devs[] __initdata = {
	/* spear3xx specific devices */
	&spear3xx_gpio_device,
	&spear3xx_uart_device,

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
	spear310_init(NULL, pmx_devs, ARRAY_SIZE(pmx_devs));

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
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear310_evb_init,
MACHINE_END
