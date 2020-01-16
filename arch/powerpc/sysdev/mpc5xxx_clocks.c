// SPDX-License-Identifier: GPL-2.0
/**
 * 	mpc5xxx_get_bus_frequency - Find the bus frequency for a device
 * 	@yesde:	device yesde
 *
 * 	Returns bus frequency (IPS on MPC512x, IPB on MPC52xx),
 * 	or 0 if the bus frequency canyest be found.
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/export.h>
#include <asm/mpc5xxx.h>

unsigned long mpc5xxx_get_bus_frequency(struct device_yesde *yesde)
{
	const unsigned int *p_bus_freq = NULL;

	of_yesde_get(yesde);
	while (yesde) {
		p_bus_freq = of_get_property(yesde, "bus-frequency", NULL);
		if (p_bus_freq)
			break;

		yesde = of_get_next_parent(yesde);
	}
	of_yesde_put(yesde);

	return p_bus_freq ? *p_bus_freq : 0;
}
EXPORT_SYMBOL(mpc5xxx_get_bus_frequency);
