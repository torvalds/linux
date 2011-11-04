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
#include <mach/hardware.h>

/* padmux devices to enable */
static struct pmx_dev *pmx_devs[] = {
	/* spear3xx specific devices */
	&spear3xx_pmx_i2c,
	&spear3xx_pmx_ssp_cs,
	&spear3xx_pmx_ssp,
	&spear3xx_pmx_mii,
	&spear3xx_pmx_uart0,

	/* spear300 specific devices */
	&spear300_pmx_fsmc_2_chips,
	&spear300_pmx_clcd,
	&spear300_pmx_telecom_sdhci_4bit,
	&spear300_pmx_gpio1,
};

static struct amba_device *amba_devs[] __initdata = {
	/* spear3xx specific devices */
	&spear3xx_gpio_device,
	&spear3xx_uart_device,

	/* spear300 specific devices */
	&spear300_gpio1_device,
};

static struct platform_device *plat_devs[] __initdata = {
	/* spear3xx specific devices */

	/* spear300 specific devices */
};

static void __init spear300_evb_init(void)
{
	unsigned int i;

	/* call spear300 machine init function */
	spear300_init(&spear300_photo_frame_mode, pmx_devs,
			ARRAY_SIZE(pmx_devs));

	/* Add Platform Devices */
	platform_add_devices(plat_devs, ARRAY_SIZE(plat_devs));

	/* Add Amba Devices */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);
}

MACHINE_START(SPEAR300, "ST-SPEAR300-EVB")
	.atag_offset	=	0x100,
	.map_io		=	spear3xx_map_io,
	.init_irq	=	spear3xx_init_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear300_evb_init,
MACHINE_END
