/*
 *  Copyright (C) 2007-2009 Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/zorro.h>

#include <asm/amigahw.h>


#ifdef CONFIG_ZORRO

static const struct resource zorro_resources[] __initconst = {
	/* Zorro II regions (on Zorro II/III) */
	{
		.name	= "Zorro II exp",
		.start	= 0x00e80000,
		.end	= 0x00efffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "Zorro II mem",
		.start	= 0x00200000,
		.end	= 0x009fffff,
		.flags	= IORESOURCE_MEM,
	},
	/* Zorro III regions (on Zorro III only) */
	{
		.name	= "Zorro III exp",
		.start	= 0xff000000,
		.end	= 0xffffffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "Zorro III cfg",
		.start	= 0x40000000,
		.end	= 0x7fffffff,
		.flags	= IORESOURCE_MEM,
	}
};


static int __init amiga_init_bus(void)
{
	if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
		return -ENODEV;

	platform_device_register_simple("amiga-zorro", -1, zorro_resources,
					AMIGAHW_PRESENT(ZORRO3) ? 4 : 2);
	return 0;
}

subsys_initcall(amiga_init_bus);

#endif /* CONFIG_ZORRO */


static int __init amiga_init_devices(void)
{
	if (!MACH_IS_AMIGA)
		return -ENODEV;

	/* video hardware */
	if (AMIGAHW_PRESENT(AMI_VIDEO))
		platform_device_register_simple("amiga-video", -1, NULL, 0);


	/* sound hardware */
	if (AMIGAHW_PRESENT(AMI_AUDIO))
		platform_device_register_simple("amiga-audio", -1, NULL, 0);


	/* storage interfaces */
	if (AMIGAHW_PRESENT(AMI_FLOPPY))
		platform_device_register_simple("amiga-floppy", -1, NULL, 0);

	return 0;
}

device_initcall(amiga_init_devices);
