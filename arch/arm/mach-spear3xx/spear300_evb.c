/*
 * arch/arm/mach-spear3xx/spear300_evb.c
 *
 * SPEAr300 evaluation board source file
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

/* padmux devices to enable */
static struct pmx_dev *pmx_devs[] = {
	/* spear3xx specific devices */
	&pmx_i2c,
	&pmx_ssp_cs,
	&pmx_ssp,
	&pmx_mii,
	&pmx_uart0,

	/* spear300 specific devices */
	&pmx_fsmc_2_chips,
	&pmx_clcd,
	&pmx_telecom_sdio_4bit,
	&pmx_gpio1,
};

static struct amba_device *amba_devs[] __initdata = {
	/* spear3xx specific devices */
	&gpio_device,
	&uart_device,

	/* spear300 specific devices */
	&gpio1_device,
};

static struct platform_device *plat_devs[] __initdata = {
	/* spear3xx specific devices */

	/* spear300 specific devices */
};

static void __init spear300_evb_init(void)
{
	unsigned int i;

	/* call spear300 machine init function */
	spear300_init();

	/* padmux initialization */
	pmx_driver.mode = &photo_frame_mode;
	pmx_driver.devs = pmx_devs;
	pmx_driver.devs_count = ARRAY_SIZE(pmx_devs);
	spear300_pmx_init();

	/* Add Platform Devices */
	platform_add_devices(plat_devs, ARRAY_SIZE(plat_devs));

	/* Add Amba Devices */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);
}

MACHINE_START(SPEAR300, "ST-SPEAR300-EVB")
	.boot_params	=	0x00000100,
	.map_io		=	spear3xx_map_io,
	.init_irq	=	spear3xx_init_irq,
	.timer		=	&spear_sys_timer,
	.init_machine	=	spear300_evb_init,
MACHINE_END
