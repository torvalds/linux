/*
 * SBus helper functions
 *
 * Sun3 don't have a sbus, but many of the used devices are also
 * used on Sparc machines with sbus. To avoid having a lot of
 * duplicate code, we provide necessary glue stuff to make using
 * of the sbus driver code possible.
 *
 * (C) 1999 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/init.h>

int __init sbus_init(void)
{
	return 0;
}

void *sparc_alloc_io (u32 address, void *virtual, int len, char *name,
                      u32 bus_type, int rdonly)
{
	return (void *)address;
}

subsys_initcall(sbus_init);
