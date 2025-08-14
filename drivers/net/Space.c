// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Holds initial configuration information for devices.
 *
 * Version:	@(#)Space.c	1.0.7	08/12/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald J. Becker, <becker@scyld.com>
 *
 * Changelog:
 *		Stephen Hemminger (09/2003)
 *		- get rid of pre-linked dev list, dynamic device allocation
 *		Paul Gortmaker (03/2002)
 *		- struct init cleanup, enable multiple ISA autoprobes.
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 09/1999
 *		- fix sbni: s/device/net_device/
 *		Paul Gortmaker (06/98):
 *		 - sort probes in a sane way, make sure all (safe) probes
 *		   get run once & failed autoprobes don't autoprobe again.
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <net/Space.h>

/*
 * This structure holds boot-time configured netdevice settings. They
 * are then used in the device probing.
 */
struct netdev_boot_setup {
	char name[IFNAMSIZ];
	struct ifmap map;
};
#define NETDEV_BOOT_SETUP_MAX 8


/******************************************************************************
 *
 *		      Device Boot-time Settings Routines
 *
 ******************************************************************************/

/* Boot time configuration table */
static struct netdev_boot_setup dev_boot_setup[NETDEV_BOOT_SETUP_MAX];

/**
 *	netdev_boot_setup_add	- add new setup entry
 *	@name: name of the device
 *	@map: configured settings for the device
 *
 *	Adds new setup entry to the dev_boot_setup list.  The function
 *	returns 0 on error and 1 on success.  This is a generic routine to
 *	all netdevices.
 */
static int netdev_boot_setup_add(char *name, struct ifmap *map)
{
	struct netdev_boot_setup *s;
	int i;

	s = dev_boot_setup;
	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] == '\0' || s[i].name[0] == ' ') {
			strscpy_pad(s[i].name, name);
			memcpy(&s[i].map, map, sizeof(s[i].map));
			break;
		}
	}

	return i >= NETDEV_BOOT_SETUP_MAX ? 0 : 1;
}

/**
 * netdev_boot_setup_check	- check boot time settings
 * @dev: the netdevice
 *
 * Check boot time settings for the device.
 * The found settings are set for the device to be used
 * later in the device probing.
 * Returns 0 if no settings found, 1 if they are.
 */
int netdev_boot_setup_check(struct net_device *dev)
{
	struct netdev_boot_setup *s = dev_boot_setup;
	int i;

	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++) {
		if (s[i].name[0] != '\0' && s[i].name[0] != ' ' &&
		    !strcmp(dev->name, s[i].name)) {
			dev->irq = s[i].map.irq;
			dev->base_addr = s[i].map.base_addr;
			dev->mem_start = s[i].map.mem_start;
			dev->mem_end = s[i].map.mem_end;
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(netdev_boot_setup_check);

/**
 * netdev_boot_base	- get address from boot time settings
 * @prefix: prefix for network device
 * @unit: id for network device
 *
 * Check boot time settings for the base address of device.
 * The found settings are set for the device to be used
 * later in the device probing.
 * Returns 0 if no settings found.
 */
static unsigned long netdev_boot_base(const char *prefix, int unit)
{
	const struct netdev_boot_setup *s = dev_boot_setup;
	char name[IFNAMSIZ];
	int i;

	sprintf(name, "%s%d", prefix, unit);

	/*
	 * If device already registered then return base of 1
	 * to indicate not to probe for this interface
	 */
	if (__dev_get_by_name(&init_net, name))
		return 1;

	for (i = 0; i < NETDEV_BOOT_SETUP_MAX; i++)
		if (!strcmp(name, s[i].name))
			return s[i].map.base_addr;
	return 0;
}

/*
 * Saves at boot time configured settings for any netdevice.
 */
static int __init netdev_boot_setup(char *str)
{
	int ints[5];
	struct ifmap map;

	str = get_options(str, ARRAY_SIZE(ints), ints);
	if (!str || !*str)
		return 0;

	/* Save settings */
	memset(&map, 0, sizeof(map));
	if (ints[0] > 0)
		map.irq = ints[1];
	if (ints[0] > 1)
		map.base_addr = ints[2];
	if (ints[0] > 2)
		map.mem_start = ints[3];
	if (ints[0] > 3)
		map.mem_end = ints[4];

	/* Add new entry to the list */
	return netdev_boot_setup_add(str, &map);
}

__setup("netdev=", netdev_boot_setup);

static int __init ether_boot_setup(char *str)
{
	return netdev_boot_setup(str);
}
__setup("ether=", ether_boot_setup);


/* A unified ethernet device probe.  This is the easiest way to have every
 * ethernet adaptor have the name "eth[0123...]".
 */

struct devprobe2 {
	struct net_device *(*probe)(int unit);
	int status;	/* non-zero if autoprobe has failed */
};

static int __init probe_list2(int unit, struct devprobe2 *p, int autoprobe)
{
	struct net_device *dev;

	for (; p->probe; p++) {
		if (autoprobe && p->status)
			continue;
		dev = p->probe(unit);
		if (!IS_ERR(dev))
			return 0;
		if (autoprobe)
			p->status = PTR_ERR(dev);
	}
	return -ENODEV;
}

/* ISA probes that touch addresses < 0x400 (including those that also
 * look for EISA/PCI cards in addition to ISA cards).
 */
static struct devprobe2 isa_probes[] __initdata = {
#ifdef CONFIG_3C515
	{tc515_probe, 0},
#endif
#ifdef CONFIG_ULTRA
	{ultra_probe, 0},
#endif
#ifdef CONFIG_WD80x3
	{wd_probe, 0},
#endif
#if defined(CONFIG_NE2000) /* ISA (use ne2k-pci for PCI cards) */
	{ne_probe, 0},
#endif
#ifdef CONFIG_LANCE		/* ISA/VLB (use pcnet32 for PCI cards) */
	{lance_probe, 0},
#endif
#ifdef CONFIG_SMC9194
	{smc_init, 0},
#endif
#ifdef CONFIG_CS89x0_ISA
	{cs89x0_probe, 0},
#endif
	{NULL, 0},
};

/* Unified ethernet device probe, segmented per architecture and
 * per bus interface. This drives the legacy devices only for now.
 */

static void __init ethif_probe2(int unit)
{
	unsigned long base_addr = netdev_boot_base("eth", unit);

	if (base_addr == 1)
		return;

	probe_list2(unit, isa_probes, base_addr == 0);
}

/*  Statically configured drivers -- order matters here. */
static int __init net_olddevs_init(void)
{
	int num;

	for (num = 0; num < 8; ++num)
		ethif_probe2(num);

	return 0;
}

device_initcall(net_olddevs_init);
