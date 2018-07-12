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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <net/Space.h>

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
#if defined(CONFIG_HP100) && defined(CONFIG_ISA)	/* ISA, EISA */
	{hp100_probe, 0},
#endif
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
#ifdef CONFIG_CS89x0
#ifndef CONFIG_CS89x0_PLATFORM
	{cs89x0_probe, 0},
#endif
#endif
#if defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)	/* Intel */
	{i82596_probe, 0},					/* I82596 */
#endif
#ifdef CONFIG_NI65
	{ni65_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe2 m68k_probes[] __initdata = {
#ifdef CONFIG_ATARILANCE	/* Lance-based Atari ethernet boards */
	{atarilance_probe, 0},
#endif
#ifdef CONFIG_SUN3LANCE         /* sun3 onboard Lance chip */
	{sun3lance_probe, 0},
#endif
#ifdef CONFIG_SUN3_82586        /* sun3 onboard Intel 82586 chip */
	{sun3_82586_probe, 0},
#endif
#ifdef CONFIG_APNE		/* A1200 PCMCIA NE2000 */
	{apne_probe, 0},
#endif
#ifdef CONFIG_MVME147_NET	/* MVME147 internal Ethernet */
	{mvme147lance_probe, 0},
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

	(void)(probe_list2(unit, m68k_probes, base_addr == 0) &&
		probe_list2(unit, isa_probes, base_addr == 0));
}

/*  Statically configured drivers -- order matters here. */
static int __init net_olddevs_init(void)
{
	int num;

#ifdef CONFIG_SBNI
	for (num = 0; num < 8; ++num)
		sbni_probe(num);
#endif
	for (num = 0; num < 8; ++num)
		ethif_probe2(num);

#ifdef CONFIG_COPS
	cops_probe(0);
	cops_probe(1);
	cops_probe(2);
#endif
#ifdef CONFIG_LTPC
	ltpc_probe();
#endif

	return 0;
}

device_initcall(net_olddevs_init);
