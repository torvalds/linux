/*
 * Routines common to most mpc86xx-based boards.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_platform.h>
#include <asm/synch.h>

#include "mpc86xx.h"

static const struct of_device_id mpc86xx_common_ids[] __initconst = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .name = "localbus", },
	{ .compatible = "gianfar", },
	{ .compatible = "fsl,mpc8641-pcie", },
	{},
};

int __init mpc86xx_common_publish_devices(void)
{
	return of_platform_bus_probe(NULL, mpc86xx_common_ids, NULL);
}

long __init mpc86xx_time_init(void)
{
	unsigned int temp;

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	temp = mfspr(SPRN_HID0);
	temp |= HID0_TBEN;
	mtspr(SPRN_HID0, temp);
	isync();

	return 0;
}
