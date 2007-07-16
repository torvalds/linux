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
#include <linux/trdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>

/* A unified ethernet device probe.  This is the easiest way to have every
   ethernet adaptor have the name "eth[0123...]".
   */

extern struct net_device *ne2_probe(int unit);
extern struct net_device *hp100_probe(int unit);
extern struct net_device *ultra_probe(int unit);
extern struct net_device *ultra32_probe(int unit);
extern struct net_device *wd_probe(int unit);
extern struct net_device *el2_probe(int unit);
extern struct net_device *ne_probe(int unit);
extern struct net_device *hp_probe(int unit);
extern struct net_device *hp_plus_probe(int unit);
extern struct net_device *express_probe(int unit);
extern struct net_device *eepro_probe(int unit);
extern struct net_device *at1700_probe(int unit);
extern struct net_device *fmv18x_probe(int unit);
extern struct net_device *eth16i_probe(int unit);
extern struct net_device *i82596_probe(int unit);
extern struct net_device *ewrk3_probe(int unit);
extern struct net_device *el1_probe(int unit);
extern struct net_device *wavelan_probe(int unit);
extern struct net_device *arlan_probe(int unit);
extern struct net_device *el16_probe(int unit);
extern struct net_device *elmc_probe(int unit);
extern struct net_device *elplus_probe(int unit);
extern struct net_device *ac3200_probe(int unit);
extern struct net_device *es_probe(int unit);
extern struct net_device *lne390_probe(int unit);
extern struct net_device *e2100_probe(int unit);
extern struct net_device *ni5010_probe(int unit);
extern struct net_device *ni52_probe(int unit);
extern struct net_device *ni65_probe(int unit);
extern struct net_device *sonic_probe(int unit);
extern struct net_device *SK_init(int unit);
extern struct net_device *seeq8005_probe(int unit);
extern struct net_device *smc_init(int unit);
extern struct net_device *atarilance_probe(int unit);
extern struct net_device *sun3lance_probe(int unit);
extern struct net_device *sun3_82586_probe(int unit);
extern struct net_device *apne_probe(int unit);
extern struct net_device *cs89x0_probe(int unit);
extern struct net_device *hplance_probe(int unit);
extern struct net_device *bagetlance_probe(int unit);
extern struct net_device *mvme147lance_probe(int unit);
extern struct net_device *tc515_probe(int unit);
extern struct net_device *lance_probe(int unit);
extern struct net_device *mac8390_probe(int unit);
extern struct net_device *mac89x0_probe(int unit);
extern struct net_device *mc32_probe(int unit);
extern struct net_device *cops_probe(int unit);
extern struct net_device *ltpc_probe(void);

/* Detachable devices ("pocket adaptors") */
extern struct net_device *de620_probe(int unit);

/* Fibre Channel adapters */
extern int iph5526_probe(struct net_device *dev);

/* SBNI adapters */
extern int sbni_probe(int unit);

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

/*
 * This is a bit of an artificial separation as there are PCI drivers
 * that also probe for EISA cards (in the PCI group) and there are ISA
 * drivers that probe for EISA cards (in the ISA group).  These are the
 * legacy EISA only driver probes, and also the legacy PCI probes
 */

static struct devprobe2 eisa_probes[] __initdata = {
#ifdef CONFIG_ULTRA32
	{ultra32_probe, 0},
#endif
#ifdef CONFIG_AC3200
	{ac3200_probe, 0},
#endif
#ifdef CONFIG_ES3210
	{es_probe, 0},
#endif
#ifdef CONFIG_LNE390
	{lne390_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe2 mca_probes[] __initdata = {
#ifdef CONFIG_NE2_MCA
	{ne2_probe, 0},
#endif
#ifdef CONFIG_ELMC		/* 3c523 */
	{elmc_probe, 0},
#endif
#ifdef CONFIG_ELMC_II		/* 3c527 */
	{mc32_probe, 0},
#endif
	{NULL, 0},
};

/*
 * ISA probes that touch addresses < 0x400 (including those that also
 * look for EISA/PCI/MCA cards in addition to ISA cards).
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
#ifdef CONFIG_EL2 		/* 3c503 */
	{el2_probe, 0},
#endif
#ifdef CONFIG_HPLAN
	{hp_probe, 0},
#endif
#ifdef CONFIG_HPLAN_PLUS
	{hp_plus_probe, 0},
#endif
#ifdef CONFIG_E2100		/* Cabletron E21xx series. */
	{e2100_probe, 0},
#endif
#if defined(CONFIG_NE2000) || \
    defined(CONFIG_NE_H8300)  /* ISA (use ne2k-pci for PCI cards) */
	{ne_probe, 0},
#endif
#ifdef CONFIG_LANCE		/* ISA/VLB (use pcnet32 for PCI cards) */
	{lance_probe, 0},
#endif
#ifdef CONFIG_SMC9194
	{smc_init, 0},
#endif
#ifdef CONFIG_SEEQ8005
	{seeq8005_probe, 0},
#endif
#ifdef CONFIG_CS89x0
 	{cs89x0_probe, 0},
#endif
#ifdef CONFIG_AT1700
	{at1700_probe, 0},
#endif
#ifdef CONFIG_ETH16I
	{eth16i_probe, 0},	/* ICL EtherTeam 16i/32 */
#endif
#ifdef CONFIG_EEXPRESS		/* Intel EtherExpress */
	{express_probe, 0},
#endif
#ifdef CONFIG_EEXPRESS_PRO	/* Intel EtherExpress Pro/10 */
	{eepro_probe, 0},
#endif
#ifdef CONFIG_EWRK3             /* DEC EtherWORKS 3 */
    	{ewrk3_probe, 0},
#endif
#if defined(CONFIG_APRICOT) || defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)	/* Intel I82596 */
	{i82596_probe, 0},
#endif
#ifdef CONFIG_EL1		/* 3c501 */
	{el1_probe, 0},
#endif
#ifdef CONFIG_WAVELAN		/* WaveLAN */
	{wavelan_probe, 0},
#endif
#ifdef CONFIG_ARLAN		/* Aironet */
	{arlan_probe, 0},
#endif
#ifdef CONFIG_EL16		/* 3c507 */
	{el16_probe, 0},
#endif
#ifdef CONFIG_ELPLUS		/* 3c505 */
	{elplus_probe, 0},
#endif
#ifdef CONFIG_NI5010
	{ni5010_probe, 0},
#endif
#ifdef CONFIG_NI52
	{ni52_probe, 0},
#endif
#ifdef CONFIG_NI65
	{ni65_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe2 parport_probes[] __initdata = {
#ifdef CONFIG_DE620		/* D-Link DE-620 adapter */
	{de620_probe, 0},
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
#ifdef CONFIG_MAC8390           /* NuBus NS8390-based cards */
	{mac8390_probe, 0},
#endif
#ifdef CONFIG_MAC89x0
 	{mac89x0_probe, 0},
#endif
	{NULL, 0},
};

/*
 * Unified ethernet device probe, segmented per architecture and
 * per bus interface. This drives the legacy devices only for now.
 */

static void __init ethif_probe2(int unit)
{
	unsigned long base_addr = netdev_boot_base("eth", unit);

	if (base_addr == 1)
		return;

	(void)(	probe_list2(unit, m68k_probes, base_addr == 0) &&
		probe_list2(unit, eisa_probes, base_addr == 0) &&
		probe_list2(unit, mca_probes, base_addr == 0) &&
		probe_list2(unit, isa_probes, base_addr == 0) &&
		probe_list2(unit, parport_probes, base_addr == 0));
}

#ifdef CONFIG_TR
/* Token-ring device probe */
extern int ibmtr_probe_card(struct net_device *);
extern struct net_device *smctr_probe(int unit);

static struct devprobe2 tr_probes2[] __initdata = {
#ifdef CONFIG_SMCTR
	{smctr_probe, 0},
#endif
	{NULL, 0},
};

static __init int trif_probe(int unit)
{
	int err = -ENODEV;
#ifdef CONFIG_IBMTR
	struct net_device *dev = alloc_trdev(0);
	if (!dev)
		return -ENOMEM;

	sprintf(dev->name, "tr%d", unit);
	netdev_boot_setup_check(dev);
	err = ibmtr_probe_card(dev);
	if (err)
		free_netdev(dev);
#endif
	return err;
}

static void __init trif_probe2(int unit)
{
	unsigned long base_addr = netdev_boot_base("tr", unit);

	if (base_addr == 1)
		return;
	probe_list2(unit, tr_probes2, base_addr == 0);
}
#endif


/*  Statically configured drivers -- order matters here. */
static int __init net_olddevs_init(void)
{
	int num;

#ifdef CONFIG_SBNI
	for (num = 0; num < 8; ++num)
		sbni_probe(num);
#endif
#ifdef CONFIG_TR
	for (num = 0; num < 8; ++num)
		if (!trif_probe(num))
			trif_probe2(num);
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
