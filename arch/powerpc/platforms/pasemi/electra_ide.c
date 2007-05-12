/*
 * Copyright (C) 2007 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/platform_device.h>

#include <asm/prom.h>
#include <asm/system.h>

/* The electra IDE interface is incredibly simple: Just a device on the localbus
 * with interrupts hooked up to one of the GPIOs. The device tree contains the
 * address window and interrupt mappings already, and the pata_platform driver handles
 * the rest. We just need to hook the two up.
 */

#define MAX_IFS	4	/* really, we have only one */

static struct platform_device *pdevs[MAX_IFS];

static int __devinit electra_ide_init(void)
{
	struct device_node *np;
	struct resource r[3];
	int ret = 0;
	int i;

	np = of_find_compatible_node(NULL, "ide", "electra-ide");
	i = 0;

	while (np && i < MAX_IFS) {
		memset(r, 0, sizeof(r));

		/* pata_platform wants two address ranges: one for the base registers,
		 * another for the control (altstatus). It's located at offset 0x3f6 in
		 * the window, but the device tree only has one large register window
		 * that covers both ranges. So we need to split it up by hand here:
		 */

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto out;
		ret = of_address_to_resource(np, 0, &r[1]);
		if (ret)
			goto out;

		r[1].start += 0x3f6;
		r[0].end = r[1].start-1;

		r[2].start = irq_of_parse_and_map(np, 0);
		r[2].end = irq_of_parse_and_map(np, 0);
		r[2].flags = IORESOURCE_IRQ;

		pr_debug("registering platform device at 0x%lx/0x%lx, irq is %ld\n",
			 r[0].start, r[1].start, r[2].start);
		pdevs[i] = platform_device_register_simple("pata_platform", i, r, 3);
		if (IS_ERR(pdevs[i])) {
			ret = PTR_ERR(pdevs[i]);
			pdevs[i] = NULL;
			goto out;
		}
		np = of_find_compatible_node(np, "ide", "electra-ide");
	}
out:
	return ret;
}
module_init(electra_ide_init);

static void __devexit electra_ide_exit(void)
{
	int i;

	for (i = 0; i < MAX_IFS; i++)
		if (pdevs[i])
			platform_device_unregister(pdevs[i]);
}
module_exit(electra_ide_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi Electra IDE driver");
