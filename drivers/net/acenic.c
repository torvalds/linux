/*
 * acenic.c: Linux driver for the Alteon AceNIC Gigabit Ethernet card
 *           and other Tigon based cards.
 *
 * Copyright 1998-2002 by Jes Sorensen, <jes@trained-monkey.org>.
 *
 * Thanks to Alteon and 3Com for providing hardware and documentation
 * enabling me to write this driver.
 *
 * A mailing list for discussing the use of this driver has been
 * setup, please subscribe to the lists if you have any questions
 * about the driver. Send mail to linux-acenic-help@sunsite.auc.dk to
 * see how to subscribe.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Additional credits:
 *   Pete Wyckoff <wyckoff@ca.sandia.gov>: Initial Linux/Alpha and trace
 *       dump support. The trace dump support has not been
 *       integrated yet however.
 *   Troy Benjegerdes: Big Endian (PPC) patches.
 *   Nate Stahl: Better out of memory handling and stats support.
 *   Aman Singla: Nasty race between interrupt handler and tx code dealing
 *                with 'testing the tx_ret_csm and setting tx_full'
 *   David S. Miller <davem@redhat.com>: conversion to new PCI dma mapping
 *                                       infrastructure and Sparc support
 *   Pierrick Pinasseau (CERN): For lending me an Ultra 5 to test the
 *                              driver under Linux/Sparc64
 *   Matt Domsch <Matt_Domsch@dell.com>: Detect Alteon 1000baseT cards
 *                                       ETHTOOL_GDRVINFO support
 *   Chip Salzenberg <chip@valinux.com>: Fix race condition between tx
 *                                       handler and close() cleanup.
 *   Ken Aaker <kdaaker@rchland.vnet.ibm.com>: Correct check for whether
 *                                       memory mapped IO is enabled to
 *                                       make the driver work on RS/6000.
 *   Takayoshi Kouchi <kouchi@hpc.bs1.fc.nec.co.jp>: Identifying problem
 *                                       where the driver would disable
 *                                       bus master mode if it had to disable
 *                                       write and invalidate.
 *   Stephen Hack <stephen_hack@hp.com>: Fixed ace_set_mac_addr for little
 *                                       endian systems.
 *   Val Henson <vhenson@esscom.com>:    Reset Jumbo skb producer and
 *                                       rx producer index when
 *                                       flushing the Jumbo ring.
 *   Hans Grobler <grobh@sun.ac.za>:     Memory leak fixes in the
 *                                       driver init path.
 *   Grant Grundler <grundler@cup.hp.com>: PCI write posting fixes.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sockios.h>

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#include <linux/if_vlan.h>
#endif

#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif

#include <net/sock.h>
#include <net/ip.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>


#define DRV_NAME "acenic"

#undef INDEX_DEBUG

#ifdef CONFIG_ACENIC_OMIT_TIGON_I
#define ACE_IS_TIGON_I(ap)	0
#define ACE_TX_RING_ENTRIES(ap)	MAX_TX_RING_ENTRIES
#else
#define ACE_IS_TIGON_I(ap)	(ap->version == 1)
#define ACE_TX_RING_ENTRIES(ap)	ap->tx_ring_entries
#endif

#ifndef PCI_VENDOR_ID_ALTEON
#define PCI_VENDOR_ID_ALTEON		0x12ae
#endif
#ifndef PCI_DEVICE_ID_ALTEON_ACENIC_FIBRE
#define PCI_DEVICE_ID_ALTEON_ACENIC_FIBRE  0x0001
#define PCI_DEVICE_ID_ALTEON_ACENIC_COPPER 0x0002
#endif
#ifndef PCI_DEVICE_ID_3COM_3C985
#define PCI_DEVICE_ID_3COM_3C985	0x0001
#endif
#ifndef PCI_VENDOR_ID_NETGEAR
#define PCI_VENDOR_ID_NETGEAR		0x1385
#define PCI_DEVICE_ID_NETGEAR_GA620	0x620a
#endif
#ifndef PCI_DEVICE_ID_NETGEAR_GA620T
#define PCI_DEVICE_ID_NETGEAR_GA620T	0x630a
#endif


/*
 * Farallon used the DEC vendor ID by mistake and they seem not
 * to care - stinky!
 */
#ifndef PCI_DEVICE_ID_FARALLON_PN9000SX
#define PCI_DEVICE_ID_FARALLON_PN9000SX	0x1a
#endif
#ifndef PCI_DEVICE_ID_FARALLON_PN9100T
#define PCI_DEVICE_ID_FARALLON_PN9100T  0xfa
#endif
#ifndef PCI_VENDOR_ID_SGI
#define PCI_VENDOR_ID_SGI		0x10a9
#endif
#ifndef PCI_DEVICE_ID_SGI_ACENIC
#define PCI_DEVICE_ID_SGI_ACENIC	0x0009
#endif

static struct pci_device_id acenic_pci_tbl[] = {
	{ PCI_VENDOR_ID_ALTEON, PCI_DEVICE_ID_ALTEON_ACENIC_FIBRE,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_ALTEON, PCI_DEVICE_ID_ALTEON_ACENIC_COPPER,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_3COM, PCI_DEVICE_ID_3COM_3C985,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_NETGEAR, PCI_DEVICE_ID_NETGEAR_GA620,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_NETGEAR, PCI_DEVICE_ID_NETGEAR_GA620T,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	/*
	 * Farallon used the DEC vendor ID on their cards incorrectly,
	 * then later Alteon's ID.
	 */
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_FARALLON_PN9000SX,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_ALTEON, PCI_DEVICE_ID_FARALLON_PN9100T,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_ACENIC,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, },
	{ }
};
MODULE_DEVICE_TABLE(pci, acenic_pci_tbl);

#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(net, pdev)	do{} while(0)
#endif

#define ace_sync_irq(irq)	synchronize_irq(irq)

#ifndef offset_in_page
#define offset_in_page(ptr)	((unsigned long)(ptr) & ~PAGE_MASK)
#endif

#define ACE_MAX_MOD_PARMS	8
#define BOARD_IDX_STATIC	0
#define BOARD_IDX_OVERFLOW	-1

#if (defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)) && \
	defined(NETIF_F_HW_VLAN_RX)
#define ACENIC_DO_VLAN		1
#define ACE_RCB_VLAN_FLAG	RCB_FLG_VLAN_ASSIST
#else
#define ACENIC_DO_VLAN		0
#define ACE_RCB_VLAN_FLAG	0
#endif

#include "acenic.h"

/*
 * These must be defined before the firmware is included.
 */
#define MAX_TEXT_LEN	96*1024
#define MAX_RODATA_LEN	8*1024
#define MAX_DATA_LEN	2*1024

#include "acenic_firmware.h"

#ifndef tigon2FwReleaseLocal
#define tigon2FwReleaseLocal 0
#endif

/*
 * This driver currently supports Tigon I and Tigon II based cards
 * including the Alteon AceNIC, the 3Com 3C985[B] and NetGear
 * GA620. The driver should also work on the SGI, DEC and Farallon
 * versions of the card, however I have not been able to test that
 * myself.
 *
 * This card is really neat, it supports receive hardware checksumming
 * and jumbo frames (up to 9000 bytes) and does a lot of work in the
 * firmware. Also the programming interface is quite neat, except for
 * the parts dealing with the i2c eeprom on the card ;-)
 *
 * Using jumbo frames:
 *
 * To enable jumbo frames, simply specify an mtu between 1500 and 9000
 * bytes to ifconfig. Jumbo frames can be enabled or disabled at any time
 * by running `ifconfig eth<X> mtu <MTU>' with <X> being the Ethernet
 * interface number and <MTU> being the MTU value.
 *
 * Module parameters:
 *
 * When compiled as a loadable module, the driver allows for a number
 * of module parameters to be specified. The driver supports the
 * following module parameters:
 *
 *  trace=<val> - Firmware trace level. This requires special traced
 *                firmware to replace the firmware supplied with
 *                the driver - for debugging purposes only.
 *
 *  link=<val>  - Link state. Normally you want to use the default link
 *                parameters set by the driver. This can be used to
 *                override these in case your switch doesn't negotiate
 *                the link properly. Valid values are:
 *         0x0001 - Force half duplex link.
 *         0x0002 - Do not negotiate line speed with the other end.
 *         0x0010 - 10Mbit/sec link.
 *         0x0020 - 100Mbit/sec link.
 *         0x0040 - 1000Mbit/sec link.
 *         0x0100 - Do not negotiate flow control.
 *         0x0200 - Enable RX flow control Y
 *         0x0400 - Enable TX flow control Y (Tigon II NICs only).
 *                Default value is 0x0270, ie. enable link+flow
 *                control negotiation. Negotiating the highest
 *                possible link speed with RX flow control enabled.
 *
 *                When disabling link speed negotiation, only one link
 *                speed is allowed to be specified!
 *
 *  tx_coal_tick=<val> - number of coalescing clock ticks (us) allowed
 *                to wait for more packets to arive before
 *                interrupting the host, from the time the first
 *                packet arrives.
 *
 *  rx_coal_tick=<val> - number of coalescing clock ticks (us) allowed
 *                to wait for more packets to arive in the transmit ring,
 *                before interrupting the host, after transmitting the
 *                first packet in the ring.
 *
 *  max_tx_desc=<val> - maximum number of transmit descriptors
 *                (packets) transmitted before interrupting the host.
 *
 *  max_rx_desc=<val> - maximum number of receive descriptors
 *                (packets) received before interrupting the host.
 *
 *  tx_ratio=<val> - 7 bit value (0 - 63) specifying the split in 64th
 *                increments of the NIC's on board memory to be used for
 *                transmit and receive buffers. For the 1MB NIC app. 800KB
 *                is available, on the 1/2MB NIC app. 300KB is available.
 *                68KB will always be available as a minimum for both
 *                directions. The default value is a 50/50 split.
 *  dis_pci_mem_inval=<val> - disable PCI memory write and invalidate
 *                operations, default (1) is to always disable this as
 *                that is what Alteon does on NT. I have not been able
 *                to measure any real performance differences with
 *                this on my systems. Set <val>=0 if you want to
 *                enable these operations.
 *
 * If you use more than one NIC, specify the parameters for the
 * individual NICs with a comma, ie. trace=0,0x00001fff,0 you want to
 * run tracing on NIC #2 but not on NIC #1 and #3.
 *
 * TODO:
 *
 * - Proper multicast support.
 * - NIC dump support.
 * - More tuning parameters.
 *
 * The mini ring is not used under Linux and I am not sure it makes sense
 * to actually use it.
 *
 * New interrupt handler strategy:
 *
 * The old interrupt handler worked using the traditional method of
 * replacing an skbuff with a new one when a packet arrives. However
 * the rx rings do not need to contain a static number of buffer
 * descriptors, thus it makes sense to move the memory allocation out
 * of the main interrupt handler and do it in a bottom half handler
 * and only allocate new buffers when the number of buffers in the
 * ring is below a certain threshold. In order to avoid starving the
 * NIC under heavy load it is however necessary to force allocation
 * when hitting a minimum threshold. The strategy for alloction is as
 * follows:
 *
 *     RX_LOW_BUF_THRES    - allocate buffers in the bottom half
 *     RX_PANIC_LOW_THRES  - we are very low on buffers, allocate
 *                           the buffers in the interrupt handler
 *     RX_RING_THRES       - maximum number of buffers in the rx ring
 *     RX_MINI_THRES       - maximum number of buffers in the mini ring
 *     RX_JUMBO_THRES      - maximum number of buffers in the jumbo ring
 *
 * One advantagous side effect of this allocation approach is that the
 * entire rx processing can be done without holding any spin lock
 * since the rx rings and registers are totally independent of the tx
 * ring and its registers.  This of course includes the kmalloc's of
 * new skb's. Thus start_xmit can run in parallel with rx processing
 * and the memory allocation on SMP systems.
 *
 * Note that running the skb reallocation in a bottom half opens up
 * another can of races which needs to be handled properly. In
 * particular it can happen that the interrupt handler tries to run
 * the reallocation while the bottom half is either running on another
 * CPU or was interrupted on the same CPU. To get around this the
 * driver uses bitops to prevent the reallocation routines from being
 * reentered.
 *
 * TX handling can also be done without holding any spin lock, wheee
 * this is fun! since tx_ret_csm is only written to by the interrupt
 * handler. The case to be aware of is when shutting down the device
 * and cleaning up where it is necessary to make sure that
 * start_xmit() is not running while this is happening. Well DaveM
 * informs me that this case is already protected against ... bye bye
 * Mr. Spin Lock, it was nice to know you.
 *
 * TX interrupts are now partly disabled so the NIC will only generate
 * TX interrupts for the number of coal ticks, not for the number of
 * TX packets in the queue. This should reduce the number of TX only,
 * ie. when no RX processing is done, interrupts seen.
 */

/*
 * Threshold values for RX buffer allocation - the low water marks for
 * when to start refilling the rings are set to 75% of the ring
 * sizes. It seems to make sense to refill the rings entirely from the
 * intrrupt handler once it gets below the panic threshold, that way
 * we don't risk that the refilling is moved to another CPU when the
 * one running the interrupt handler just got the slab code hot in its
 * cache.
 */
#define RX_RING_SIZE		72
#define RX_MINI_SIZE		64
#define RX_JUMBO_SIZE		48

#define RX_PANIC_STD_THRES	16
#define RX_PANIC_STD_REFILL	(3*RX_PANIC_STD_THRES)/2
#define RX_LOW_STD_THRES	(3*RX_RING_SIZE)/4
#define RX_PANIC_MINI_THRES	12
#define RX_PANIC_MINI_REFILL	(3*RX_PANIC_MINI_THRES)/2
#define RX_LOW_MINI_THRES	(3*RX_MINI_SIZE)/4
#define RX_PANIC_JUMBO_THRES	6
#define RX_PANIC_JUMBO_REFILL	(3*RX_PANIC_JUMBO_THRES)/2
#define RX_LOW_JUMBO_THRES	(3*RX_JUMBO_SIZE)/4


/*
 * Size of the mini ring entries, basically these just should be big
 * enough to take TCP ACKs
 */
#define ACE_MINI_SIZE		100

#define ACE_MINI_BUFSIZE	ACE_MINI_SIZE
#define ACE_STD_BUFSIZE		(ACE_STD_MTU + ETH_HLEN + 4)
#define ACE_JUMBO_BUFSIZE	(ACE_JUMBO_MTU + ETH_HLEN + 4)

/*
 * There seems to be a magic difference in the effect between 995 and 996
 * but little difference between 900 and 995 ... no idea why.
 *
 * There is now a default set of tuning parameters which is set, depending
 * on whether or not the user enables Jumbo frames. It's assumed that if
 * Jumbo frames are enabled, the user wants optimal tuning for that case.
 */
#define DEF_TX_COAL		400 /* 996 */
#define DEF_TX_MAX_DESC		60  /* was 40 */
#define DEF_RX_COAL		120 /* 1000 */
#define DEF_RX_MAX_DESC		25
#define DEF_TX_RATIO		21 /* 24 */

#define DEF_JUMBO_TX_COAL	20
#define DEF_JUMBO_TX_MAX_DESC	60
#define DEF_JUMBO_RX_COAL	30
#define DEF_JUMBO_RX_MAX_DESC	6
#define DEF_JUMBO_TX_RATIO	21

#if tigon2FwReleaseLocal < 20001118
/*
 * Standard firmware and early modifications duplicate
 * IRQ load without this flag (coal timer is never reset).
 * Note that with this flag tx_coal should be less than
 * time to xmit full tx ring.
 * 400usec is not so bad for tx ring size of 128.
 */
#define TX_COAL_INTS_ONLY	1	/* worth it */
#else
/*
 * With modified firmware, this is not necessary, but still useful.
 */
#define TX_COAL_INTS_ONLY	1
#endif

#define DEF_TRACE		0
#define DEF_STAT		(2 * TICKS_PER_SEC)


static int link[ACE_MAX_MOD_PARMS];
static int trace[ACE_MAX_MOD_PARMS];
static int tx_coal_tick[ACE_MAX_MOD_PARMS];
static int rx_coal_tick[ACE_MAX_MOD_PARMS];
static int max_tx_desc[ACE_MAX_MOD_PARMS];
static int max_rx_desc[ACE_MAX_MOD_PARMS];
static int tx_ratio[ACE_MAX_MOD_PARMS];
static int dis_pci_mem_inval[ACE_MAX_MOD_PARMS] = {1, 1, 1, 1, 1, 1, 1, 1};

MODULE_AUTHOR("Jes Sorensen <jes@trained-monkey.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AceNIC/3C985/GA620 Gigabit Ethernet driver");

module_param_array(link, int, NULL, 0);
module_param_array(trace, int, NULL, 0);
module_param_array(tx_coal_tick, int, NULL, 0);
module_param_array(max_tx_desc, int, NULL, 0);
module_param_array(rx_coal_tick, int, NULL, 0);
module_param_array(max_rx_desc, int, NULL, 0);
module_param_array(tx_ratio, int, NULL, 0);
MODULE_PARM_DESC(link, "AceNIC/3C985/NetGear link state");
MODULE_PARM_DESC(trace, "AceNIC/3C985/NetGear firmware trace level");
MODULE_PARM_DESC(tx_coal_tick, "AceNIC/3C985/GA620 max clock ticks to wait from first tx descriptor arrives");
MODULE_PARM_DESC(max_tx_desc, "AceNIC/3C985/GA620 max number of transmit descriptors to wait");
MODULE_PARM_DESC(rx_coal_tick, "AceNIC/3C985/GA620 max clock ticks to wait from first rx descriptor arrives");
MODULE_PARM_DESC(max_rx_desc, "AceNIC/3C985/GA620 max number of receive descriptors to wait");
MODULE_PARM_DESC(tx_ratio, "AceNIC/3C985/GA620 ratio of NIC memory used for TX/RX descriptors (range 0-63)");


static char version[] __devinitdata =
  "acenic.c: v0.92 08/05/2002  Jes Sorensen, linux-acenic@SunSITE.dk\n"
  "                            http://home.cern.ch/~jes/gige/acenic.html\n";

static int ace_get_settings(struct net_device *, struct ethtool_cmd *);
static int ace_set_settings(struct net_device *, struct ethtool_cmd *);
static void ace_get_drvinfo(struct net_device *, struct ethtool_drvinfo *);

static const struct ethtool_ops ace_ethtool_ops = {
	.get_settings = ace_get_settings,
	.set_settings = ace_set_settings,
	.get_drvinfo = ace_get_drvinfo,
};

static void ace_watchdog(struct net_device *dev);

static int __devinit acenic_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	struct net_device *dev;
	struct ace_private *ap;
	static int boards_found;

	dev = alloc_etherdev(sizeof(struct ace_private));
	if (dev == NULL) {
		printk(KERN_ERR "acenic: Unable to allocate "
		       "net_device structure!\n");
		return -ENOMEM;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ap = dev->priv;
	ap->pdev = pdev;
	ap->name = pci_name(pdev);

	dev->features |= NETIF_F_SG | NETIF_F_IP_CSUM;
#if ACENIC_DO_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register = ace_vlan_rx_register;
	dev->vlan_rx_kill_vid = ace_vlan_rx_kill_vid;
#endif
	if (1) {
		dev->tx_timeout = &ace_watchdog;
		dev->watchdog_timeo = 5*HZ;
	}

	dev->open = &ace_open;
	dev->stop = &ace_close;
	dev->hard_start_xmit = &ace_start_xmit;
	dev->get_stats = &ace_get_stats;
	dev->set_multicast_list = &ace_set_multicast_list;
	SET_ETHTOOL_OPS(dev, &ace_ethtool_ops);
	dev->set_mac_address = &ace_set_mac_addr;
	dev->change_mtu = &ace_change_mtu;

	/* we only display this string ONCE */
	if (!boards_found)
		printk(version);

	if (pci_enable_device(pdev))
		goto fail_free_netdev;

	/*
	 * Enable master mode before we start playing with the
	 * pci_command word since pci_set_master() will modify
	 * it.
	 */
	pci_set_master(pdev);

	pci_read_config_word(pdev, PCI_COMMAND, &ap->pci_command);

	/* OpenFirmware on Mac's does not set this - DOH.. */
	if (!(ap->pci_command & PCI_COMMAND_MEMORY)) {
		printk(KERN_INFO "%s: Enabling PCI Memory Mapped "
		       "access - was not enabled by BIOS/Firmware\n",
		       ap->name);
		ap->pci_command = ap->pci_command | PCI_COMMAND_MEMORY;
		pci_write_config_word(ap->pdev, PCI_COMMAND,
				      ap->pci_command);
		wmb();
	}

	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &ap->pci_latency);
	if (ap->pci_latency <= 0x40) {
		ap->pci_latency = 0x40;
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, ap->pci_latency);
	}

	/*
	 * Remap the regs into kernel space - this is abuse of
	 * dev->base_addr since it was means for I/O port
	 * addresses but who gives a damn.
	 */
	dev->base_addr = pci_resource_start(pdev, 0);
	ap->regs = ioremap(dev->base_addr, 0x4000);
	if (!ap->regs) {
		printk(KERN_ERR "%s:  Unable to map I/O register, "
		       "AceNIC %i will be disabled.\n",
		       ap->name, boards_found);
		goto fail_free_netdev;
	}

	switch(pdev->vendor) {
	case PCI_VENDOR_ID_ALTEON:
		if (pdev->device == PCI_DEVICE_ID_FARALLON_PN9100T) {
			printk(KERN_INFO "%s: Farallon PN9100-T ",
			       ap->name);
		} else {
			printk(KERN_INFO "%s: Alteon AceNIC ",
			       ap->name);
		}
		break;
	case PCI_VENDOR_ID_3COM:
		printk(KERN_INFO "%s: 3Com 3C985 ", ap->name);
		break;
	case PCI_VENDOR_ID_NETGEAR:
		printk(KERN_INFO "%s: NetGear GA620 ", ap->name);
		break;
	case PCI_VENDOR_ID_DEC:
		if (pdev->device == PCI_DEVICE_ID_FARALLON_PN9000SX) {
			printk(KERN_INFO "%s: Farallon PN9000-SX ",
			       ap->name);
			break;
		}
	case PCI_VENDOR_ID_SGI:
		printk(KERN_INFO "%s: SGI AceNIC ", ap->name);
		break;
	default:
		printk(KERN_INFO "%s: Unknown AceNIC ", ap->name);
		break;
	}

	printk("Gigabit Ethernet at 0x%08lx, ", dev->base_addr);
	printk("irq %d\n", pdev->irq);

#ifdef CONFIG_ACENIC_OMIT_TIGON_I
	if ((readl(&ap->regs->HostCtrl) >> 28) == 4) {
		printk(KERN_ERR "%s: Driver compiled without Tigon I"
		       " support - NIC disabled\n", dev->name);
		goto fail_uninit;
	}
#endif

	if (ace_allocate_descriptors(dev))
		goto fail_free_netdev;

#ifdef MODULE
	if (boards_found >= ACE_MAX_MOD_PARMS)
		ap->board_idx = BOARD_IDX_OVERFLOW;
	else
		ap->board_idx = boards_found;
#else
	ap->board_idx = BOARD_IDX_STATIC;
#endif

	if (ace_init(dev))
		goto fail_free_netdev;

	if (register_netdev(dev)) {
		printk(KERN_ERR "acenic: device registration failed\n");
		goto fail_uninit;
	}
	ap->name = dev->name;

	if (ap->pci_using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pdev, dev);

	boards_found++;
	return 0;

 fail_uninit:
	ace_init_cleanup(dev);
 fail_free_netdev:
	free_netdev(dev);
	return -ENODEV;
}

static void __devexit acenic_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	short i;

	unregister_netdev(dev);

	writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
	if (ap->version >= 2)
		writel(readl(&regs->CpuBCtrl) | CPU_HALT, &regs->CpuBCtrl);

	/*
	 * This clears any pending interrupts
	 */
	writel(1, &regs->Mb0Lo);
	readl(&regs->CpuCtrl);	/* flush */

	/*
	 * Make sure no other CPUs are processing interrupts
	 * on the card before the buffers are being released.
	 * Otherwise one might experience some `interesting'
	 * effects.
	 *
	 * Then release the RX buffers - jumbo buffers were
	 * already released in ace_close().
	 */
	ace_sync_irq(dev->irq);

	for (i = 0; i < RX_STD_RING_ENTRIES; i++) {
		struct sk_buff *skb = ap->skb->rx_std_skbuff[i].skb;

		if (skb) {
			struct ring_info *ringp;
			dma_addr_t mapping;

			ringp = &ap->skb->rx_std_skbuff[i];
			mapping = pci_unmap_addr(ringp, mapping);
			pci_unmap_page(ap->pdev, mapping,
				       ACE_STD_BUFSIZE,
				       PCI_DMA_FROMDEVICE);

			ap->rx_std_ring[i].size = 0;
			ap->skb->rx_std_skbuff[i].skb = NULL;
			dev_kfree_skb(skb);
		}
	}

	if (ap->version >= 2) {
		for (i = 0; i < RX_MINI_RING_ENTRIES; i++) {
			struct sk_buff *skb = ap->skb->rx_mini_skbuff[i].skb;

			if (skb) {
				struct ring_info *ringp;
				dma_addr_t mapping;

				ringp = &ap->skb->rx_mini_skbuff[i];
				mapping = pci_unmap_addr(ringp,mapping);
				pci_unmap_page(ap->pdev, mapping,
					       ACE_MINI_BUFSIZE,
					       PCI_DMA_FROMDEVICE);

				ap->rx_mini_ring[i].size = 0;
				ap->skb->rx_mini_skbuff[i].skb = NULL;
				dev_kfree_skb(skb);
			}
		}
	}

	for (i = 0; i < RX_JUMBO_RING_ENTRIES; i++) {
		struct sk_buff *skb = ap->skb->rx_jumbo_skbuff[i].skb;
		if (skb) {
			struct ring_info *ringp;
			dma_addr_t mapping;

			ringp = &ap->skb->rx_jumbo_skbuff[i];
			mapping = pci_unmap_addr(ringp, mapping);
			pci_unmap_page(ap->pdev, mapping,
				       ACE_JUMBO_BUFSIZE,
				       PCI_DMA_FROMDEVICE);

			ap->rx_jumbo_ring[i].size = 0;
			ap->skb->rx_jumbo_skbuff[i].skb = NULL;
			dev_kfree_skb(skb);
		}
	}

	ace_init_cleanup(dev);
	free_netdev(dev);
}

static struct pci_driver acenic_pci_driver = {
	.name		= "acenic",
	.id_table	= acenic_pci_tbl,
	.probe		= acenic_probe_one,
	.remove		= __devexit_p(acenic_remove_one),
};

static int __init acenic_init(void)
{
	return pci_register_driver(&acenic_pci_driver);
}

static void __exit acenic_exit(void)
{
	pci_unregister_driver(&acenic_pci_driver);
}

module_init(acenic_init);
module_exit(acenic_exit);

static void ace_free_descriptors(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	int size;

	if (ap->rx_std_ring != NULL) {
		size = (sizeof(struct rx_desc) *
			(RX_STD_RING_ENTRIES +
			 RX_JUMBO_RING_ENTRIES +
			 RX_MINI_RING_ENTRIES +
			 RX_RETURN_RING_ENTRIES));
		pci_free_consistent(ap->pdev, size, ap->rx_std_ring,
				    ap->rx_ring_base_dma);
		ap->rx_std_ring = NULL;
		ap->rx_jumbo_ring = NULL;
		ap->rx_mini_ring = NULL;
		ap->rx_return_ring = NULL;
	}
	if (ap->evt_ring != NULL) {
		size = (sizeof(struct event) * EVT_RING_ENTRIES);
		pci_free_consistent(ap->pdev, size, ap->evt_ring,
				    ap->evt_ring_dma);
		ap->evt_ring = NULL;
	}
	if (ap->tx_ring != NULL && !ACE_IS_TIGON_I(ap)) {
		size = (sizeof(struct tx_desc) * MAX_TX_RING_ENTRIES);
		pci_free_consistent(ap->pdev, size, ap->tx_ring,
				    ap->tx_ring_dma);
	}
	ap->tx_ring = NULL;

	if (ap->evt_prd != NULL) {
		pci_free_consistent(ap->pdev, sizeof(u32),
				    (void *)ap->evt_prd, ap->evt_prd_dma);
		ap->evt_prd = NULL;
	}
	if (ap->rx_ret_prd != NULL) {
		pci_free_consistent(ap->pdev, sizeof(u32),
				    (void *)ap->rx_ret_prd,
				    ap->rx_ret_prd_dma);
		ap->rx_ret_prd = NULL;
	}
	if (ap->tx_csm != NULL) {
		pci_free_consistent(ap->pdev, sizeof(u32),
				    (void *)ap->tx_csm, ap->tx_csm_dma);
		ap->tx_csm = NULL;
	}
}


static int ace_allocate_descriptors(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	int size;

	size = (sizeof(struct rx_desc) *
		(RX_STD_RING_ENTRIES +
		 RX_JUMBO_RING_ENTRIES +
		 RX_MINI_RING_ENTRIES +
		 RX_RETURN_RING_ENTRIES));

	ap->rx_std_ring = pci_alloc_consistent(ap->pdev, size,
					       &ap->rx_ring_base_dma);
	if (ap->rx_std_ring == NULL)
		goto fail;

	ap->rx_jumbo_ring = ap->rx_std_ring + RX_STD_RING_ENTRIES;
	ap->rx_mini_ring = ap->rx_jumbo_ring + RX_JUMBO_RING_ENTRIES;
	ap->rx_return_ring = ap->rx_mini_ring + RX_MINI_RING_ENTRIES;

	size = (sizeof(struct event) * EVT_RING_ENTRIES);

	ap->evt_ring = pci_alloc_consistent(ap->pdev, size, &ap->evt_ring_dma);

	if (ap->evt_ring == NULL)
		goto fail;

	/*
	 * Only allocate a host TX ring for the Tigon II, the Tigon I
	 * has to use PCI registers for this ;-(
	 */
	if (!ACE_IS_TIGON_I(ap)) {
		size = (sizeof(struct tx_desc) * MAX_TX_RING_ENTRIES);

		ap->tx_ring = pci_alloc_consistent(ap->pdev, size,
						   &ap->tx_ring_dma);

		if (ap->tx_ring == NULL)
			goto fail;
	}

	ap->evt_prd = pci_alloc_consistent(ap->pdev, sizeof(u32),
					   &ap->evt_prd_dma);
	if (ap->evt_prd == NULL)
		goto fail;

	ap->rx_ret_prd = pci_alloc_consistent(ap->pdev, sizeof(u32),
					      &ap->rx_ret_prd_dma);
	if (ap->rx_ret_prd == NULL)
		goto fail;

	ap->tx_csm = pci_alloc_consistent(ap->pdev, sizeof(u32),
					  &ap->tx_csm_dma);
	if (ap->tx_csm == NULL)
		goto fail;

	return 0;

fail:
	/* Clean up. */
	ace_init_cleanup(dev);
	return 1;
}


/*
 * Generic cleanup handling data allocated during init. Used when the
 * module is unloaded or if an error occurs during initialization
 */
static void ace_init_cleanup(struct net_device *dev)
{
	struct ace_private *ap;

	ap = netdev_priv(dev);

	ace_free_descriptors(dev);

	if (ap->info)
		pci_free_consistent(ap->pdev, sizeof(struct ace_info),
				    ap->info, ap->info_dma);
	kfree(ap->skb);
	kfree(ap->trace_buf);

	if (dev->irq)
		free_irq(dev->irq, dev);

	iounmap(ap->regs);
}


/*
 * Commands are considered to be slow.
 */
static inline void ace_issue_cmd(struct ace_regs __iomem *regs, struct cmd *cmd)
{
	u32 idx;

	idx = readl(&regs->CmdPrd);

	writel(*(u32 *)(cmd), &regs->CmdRng[idx]);
	idx = (idx + 1) % CMD_RING_ENTRIES;

	writel(idx, &regs->CmdPrd);
}


static int __devinit ace_init(struct net_device *dev)
{
	struct ace_private *ap;
	struct ace_regs __iomem *regs;
	struct ace_info *info = NULL;
	struct pci_dev *pdev;
	unsigned long myjif;
	u64 tmp_ptr;
	u32 tig_ver, mac1, mac2, tmp, pci_state;
	int board_idx, ecode = 0;
	short i;
	unsigned char cache_size;

	ap = netdev_priv(dev);
	regs = ap->regs;

	board_idx = ap->board_idx;

	/*
	 * aman@sgi.com - its useful to do a NIC reset here to
	 * address the `Firmware not running' problem subsequent
	 * to any crashes involving the NIC
	 */
	writel(HW_RESET | (HW_RESET << 24), &regs->HostCtrl);
	readl(&regs->HostCtrl);		/* PCI write posting */
	udelay(5);

	/*
	 * Don't access any other registers before this point!
	 */
#ifdef __BIG_ENDIAN
	/*
	 * This will most likely need BYTE_SWAP once we switch
	 * to using __raw_writel()
	 */
	writel((WORD_SWAP | CLR_INT | ((WORD_SWAP | CLR_INT) << 24)),
	       &regs->HostCtrl);
#else
	writel((CLR_INT | WORD_SWAP | ((CLR_INT | WORD_SWAP) << 24)),
	       &regs->HostCtrl);
#endif
	readl(&regs->HostCtrl);		/* PCI write posting */

	/*
	 * Stop the NIC CPU and clear pending interrupts
	 */
	writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
	readl(&regs->CpuCtrl);		/* PCI write posting */
	writel(0, &regs->Mb0Lo);

	tig_ver = readl(&regs->HostCtrl) >> 28;

	switch(tig_ver){
#ifndef CONFIG_ACENIC_OMIT_TIGON_I
	case 4:
	case 5:
		printk(KERN_INFO "  Tigon I  (Rev. %i), Firmware: %i.%i.%i, ",
		       tig_ver, tigonFwReleaseMajor, tigonFwReleaseMinor,
		       tigonFwReleaseFix);
		writel(0, &regs->LocalCtrl);
		ap->version = 1;
		ap->tx_ring_entries = TIGON_I_TX_RING_ENTRIES;
		break;
#endif
	case 6:
		printk(KERN_INFO "  Tigon II (Rev. %i), Firmware: %i.%i.%i, ",
		       tig_ver, tigon2FwReleaseMajor, tigon2FwReleaseMinor,
		       tigon2FwReleaseFix);
		writel(readl(&regs->CpuBCtrl) | CPU_HALT, &regs->CpuBCtrl);
		readl(&regs->CpuBCtrl);		/* PCI write posting */
		/*
		 * The SRAM bank size does _not_ indicate the amount
		 * of memory on the card, it controls the _bank_ size!
		 * Ie. a 1MB AceNIC will have two banks of 512KB.
		 */
		writel(SRAM_BANK_512K, &regs->LocalCtrl);
		writel(SYNC_SRAM_TIMING, &regs->MiscCfg);
		ap->version = 2;
		ap->tx_ring_entries = MAX_TX_RING_ENTRIES;
		break;
	default:
		printk(KERN_WARNING "  Unsupported Tigon version detected "
		       "(%i)\n", tig_ver);
		ecode = -ENODEV;
		goto init_error;
	}

	/*
	 * ModeStat _must_ be set after the SRAM settings as this change
	 * seems to corrupt the ModeStat and possible other registers.
	 * The SRAM settings survive resets and setting it to the same
	 * value a second time works as well. This is what caused the
	 * `Firmware not running' problem on the Tigon II.
	 */
#ifdef __BIG_ENDIAN
	writel(ACE_BYTE_SWAP_DMA | ACE_WARN | ACE_FATAL | ACE_BYTE_SWAP_BD |
	       ACE_WORD_SWAP_BD | ACE_NO_JUMBO_FRAG, &regs->ModeStat);
#else
	writel(ACE_BYTE_SWAP_DMA | ACE_WARN | ACE_FATAL |
	       ACE_WORD_SWAP_BD | ACE_NO_JUMBO_FRAG, &regs->ModeStat);
#endif
	readl(&regs->ModeStat);		/* PCI write posting */

	mac1 = 0;
	for(i = 0; i < 4; i++) {
		int tmp;

		mac1 = mac1 << 8;
		tmp = read_eeprom_byte(dev, 0x8c+i);
		if (tmp < 0) {
			ecode = -EIO;
			goto init_error;
		} else
			mac1 |= (tmp & 0xff);
	}
	mac2 = 0;
	for(i = 4; i < 8; i++) {
		int tmp;

		mac2 = mac2 << 8;
		tmp = read_eeprom_byte(dev, 0x8c+i);
		if (tmp < 0) {
			ecode = -EIO;
			goto init_error;
		} else
			mac2 |= (tmp & 0xff);
	}

	writel(mac1, &regs->MacAddrHi);
	writel(mac2, &regs->MacAddrLo);

	printk("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	       (mac1 >> 8) & 0xff, mac1 & 0xff, (mac2 >> 24) &0xff,
	       (mac2 >> 16) & 0xff, (mac2 >> 8) & 0xff, mac2 & 0xff);

	dev->dev_addr[0] = (mac1 >> 8) & 0xff;
	dev->dev_addr[1] = mac1 & 0xff;
	dev->dev_addr[2] = (mac2 >> 24) & 0xff;
	dev->dev_addr[3] = (mac2 >> 16) & 0xff;
	dev->dev_addr[4] = (mac2 >> 8) & 0xff;
	dev->dev_addr[5] = mac2 & 0xff;

	/*
	 * Looks like this is necessary to deal with on all architectures,
	 * even this %$#%$# N440BX Intel based thing doesn't get it right.
	 * Ie. having two NICs in the machine, one will have the cache
	 * line set at boot time, the other will not.
	 */
	pdev = ap->pdev;
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cache_size);
	cache_size <<= 2;
	if (cache_size != SMP_CACHE_BYTES) {
		printk(KERN_INFO "  PCI cache line size set incorrectly "
		       "(%i bytes) by BIOS/FW, ", cache_size);
		if (cache_size > SMP_CACHE_BYTES)
			printk("expecting %i\n", SMP_CACHE_BYTES);
		else {
			printk("correcting to %i\n", SMP_CACHE_BYTES);
			pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
					      SMP_CACHE_BYTES >> 2);
		}
	}

	pci_state = readl(&regs->PciState);
	printk(KERN_INFO "  PCI bus width: %i bits, speed: %iMHz, "
	       "latency: %i clks\n",
	       	(pci_state & PCI_32BIT) ? 32 : 64,
		(pci_state & PCI_66MHZ) ? 66 : 33,
		ap->pci_latency);

	/*
	 * Set the max DMA transfer size. Seems that for most systems
	 * the performance is better when no MAX parameter is
	 * set. However for systems enabling PCI write and invalidate,
	 * DMA writes must be set to the L1 cache line size to get
	 * optimal performance.
	 *
	 * The default is now to turn the PCI write and invalidate off
	 * - that is what Alteon does for NT.
	 */
	tmp = READ_CMD_MEM | WRITE_CMD_MEM;
	if (ap->version >= 2) {
		tmp |= (MEM_READ_MULTIPLE | (pci_state & PCI_66MHZ));
		/*
		 * Tuning parameters only supported for 8 cards
		 */
		if (board_idx == BOARD_IDX_OVERFLOW ||
		    dis_pci_mem_inval[board_idx]) {
			if (ap->pci_command & PCI_COMMAND_INVALIDATE) {
				ap->pci_command &= ~PCI_COMMAND_INVALIDATE;
				pci_write_config_word(pdev, PCI_COMMAND,
						      ap->pci_command);
				printk(KERN_INFO "  Disabling PCI memory "
				       "write and invalidate\n");
			}
		} else if (ap->pci_command & PCI_COMMAND_INVALIDATE) {
			printk(KERN_INFO "  PCI memory write & invalidate "
			       "enabled by BIOS, enabling counter measures\n");

			switch(SMP_CACHE_BYTES) {
			case 16:
				tmp |= DMA_WRITE_MAX_16;
				break;
			case 32:
				tmp |= DMA_WRITE_MAX_32;
				break;
			case 64:
				tmp |= DMA_WRITE_MAX_64;
				break;
			case 128:
				tmp |= DMA_WRITE_MAX_128;
				break;
			default:
				printk(KERN_INFO "  Cache line size %i not "
				       "supported, PCI write and invalidate "
				       "disabled\n", SMP_CACHE_BYTES);
				ap->pci_command &= ~PCI_COMMAND_INVALIDATE;
				pci_write_config_word(pdev, PCI_COMMAND,
						      ap->pci_command);
			}
		}
	}

#ifdef __sparc__
	/*
	 * On this platform, we know what the best dma settings
	 * are.  We use 64-byte maximum bursts, because if we
	 * burst larger than the cache line size (or even cross
	 * a 64byte boundary in a single burst) the UltraSparc
	 * PCI controller will disconnect at 64-byte multiples.
	 *
	 * Read-multiple will be properly enabled above, and when
	 * set will give the PCI controller proper hints about
	 * prefetching.
	 */
	tmp &= ~DMA_READ_WRITE_MASK;
	tmp |= DMA_READ_MAX_64;
	tmp |= DMA_WRITE_MAX_64;
#endif
#ifdef __alpha__
	tmp &= ~DMA_READ_WRITE_MASK;
	tmp |= DMA_READ_MAX_128;
	/*
	 * All the docs say MUST NOT. Well, I did.
	 * Nothing terrible happens, if we load wrong size.
	 * Bit w&i still works better!
	 */
	tmp |= DMA_WRITE_MAX_128;
#endif
	writel(tmp, &regs->PciState);

#if 0
	/*
	 * The Host PCI bus controller driver has to set FBB.
	 * If all devices on that PCI bus support FBB, then the controller
	 * can enable FBB support in the Host PCI Bus controller (or on
	 * the PCI-PCI bridge if that applies).
	 * -ggg
	 */
	/*
	 * I have received reports from people having problems when this
	 * bit is enabled.
	 */
	if (!(ap->pci_command & PCI_COMMAND_FAST_BACK)) {
		printk(KERN_INFO "  Enabling PCI Fast Back to Back\n");
		ap->pci_command |= PCI_COMMAND_FAST_BACK;
		pci_write_config_word(pdev, PCI_COMMAND, ap->pci_command);
	}
#endif

	/*
	 * Configure DMA attributes.
	 */
	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		ap->pci_using_dac = 1;
	} else if (!pci_set_dma_mask(pdev, DMA_32BIT_MASK)) {
		ap->pci_using_dac = 0;
	} else {
		ecode = -ENODEV;
		goto init_error;
	}

	/*
	 * Initialize the generic info block and the command+event rings
	 * and the control blocks for the transmit and receive rings
	 * as they need to be setup once and for all.
	 */
	if (!(info = pci_alloc_consistent(ap->pdev, sizeof(struct ace_info),
					  &ap->info_dma))) {
		ecode = -EAGAIN;
		goto init_error;
	}
	ap->info = info;

	/*
	 * Get the memory for the skb rings.
	 */
	if (!(ap->skb = kmalloc(sizeof(struct ace_skb), GFP_KERNEL))) {
		ecode = -EAGAIN;
		goto init_error;
	}

	ecode = request_irq(pdev->irq, ace_interrupt, IRQF_SHARED,
			    DRV_NAME, dev);
	if (ecode) {
		printk(KERN_WARNING "%s: Requested IRQ %d is busy\n",
		       DRV_NAME, pdev->irq);
		goto init_error;
	} else
		dev->irq = pdev->irq;

#ifdef INDEX_DEBUG
	spin_lock_init(&ap->debug_lock);
	ap->last_tx = ACE_TX_RING_ENTRIES(ap) - 1;
	ap->last_std_rx = 0;
	ap->last_mini_rx = 0;
#endif

	memset(ap->info, 0, sizeof(struct ace_info));
	memset(ap->skb, 0, sizeof(struct ace_skb));

	ace_load_firmware(dev);
	ap->fw_running = 0;

	tmp_ptr = ap->info_dma;
	writel(tmp_ptr >> 32, &regs->InfoPtrHi);
	writel(tmp_ptr & 0xffffffff, &regs->InfoPtrLo);

	memset(ap->evt_ring, 0, EVT_RING_ENTRIES * sizeof(struct event));

	set_aceaddr(&info->evt_ctrl.rngptr, ap->evt_ring_dma);
	info->evt_ctrl.flags = 0;

	*(ap->evt_prd) = 0;
	wmb();
	set_aceaddr(&info->evt_prd_ptr, ap->evt_prd_dma);
	writel(0, &regs->EvtCsm);

	set_aceaddr(&info->cmd_ctrl.rngptr, 0x100);
	info->cmd_ctrl.flags = 0;
	info->cmd_ctrl.max_len = 0;

	for (i = 0; i < CMD_RING_ENTRIES; i++)
		writel(0, &regs->CmdRng[i]);

	writel(0, &regs->CmdPrd);
	writel(0, &regs->CmdCsm);

	tmp_ptr = ap->info_dma;
	tmp_ptr += (unsigned long) &(((struct ace_info *)0)->s.stats);
	set_aceaddr(&info->stats2_ptr, (dma_addr_t) tmp_ptr);

	set_aceaddr(&info->rx_std_ctrl.rngptr, ap->rx_ring_base_dma);
	info->rx_std_ctrl.max_len = ACE_STD_BUFSIZE;
	info->rx_std_ctrl.flags =
	  RCB_FLG_TCP_UDP_SUM | RCB_FLG_NO_PSEUDO_HDR | ACE_RCB_VLAN_FLAG;

	memset(ap->rx_std_ring, 0,
	       RX_STD_RING_ENTRIES * sizeof(struct rx_desc));

	for (i = 0; i < RX_STD_RING_ENTRIES; i++)
		ap->rx_std_ring[i].flags = BD_FLG_TCP_UDP_SUM;

	ap->rx_std_skbprd = 0;
	atomic_set(&ap->cur_rx_bufs, 0);

	set_aceaddr(&info->rx_jumbo_ctrl.rngptr,
		    (ap->rx_ring_base_dma +
		     (sizeof(struct rx_desc) * RX_STD_RING_ENTRIES)));
	info->rx_jumbo_ctrl.max_len = 0;
	info->rx_jumbo_ctrl.flags =
	  RCB_FLG_TCP_UDP_SUM | RCB_FLG_NO_PSEUDO_HDR | ACE_RCB_VLAN_FLAG;

	memset(ap->rx_jumbo_ring, 0,
	       RX_JUMBO_RING_ENTRIES * sizeof(struct rx_desc));

	for (i = 0; i < RX_JUMBO_RING_ENTRIES; i++)
		ap->rx_jumbo_ring[i].flags = BD_FLG_TCP_UDP_SUM | BD_FLG_JUMBO;

	ap->rx_jumbo_skbprd = 0;
	atomic_set(&ap->cur_jumbo_bufs, 0);

	memset(ap->rx_mini_ring, 0,
	       RX_MINI_RING_ENTRIES * sizeof(struct rx_desc));

	if (ap->version >= 2) {
		set_aceaddr(&info->rx_mini_ctrl.rngptr,
			    (ap->rx_ring_base_dma +
			     (sizeof(struct rx_desc) *
			      (RX_STD_RING_ENTRIES +
			       RX_JUMBO_RING_ENTRIES))));
		info->rx_mini_ctrl.max_len = ACE_MINI_SIZE;
		info->rx_mini_ctrl.flags =
		  RCB_FLG_TCP_UDP_SUM|RCB_FLG_NO_PSEUDO_HDR|ACE_RCB_VLAN_FLAG;

		for (i = 0; i < RX_MINI_RING_ENTRIES; i++)
			ap->rx_mini_ring[i].flags =
				BD_FLG_TCP_UDP_SUM | BD_FLG_MINI;
	} else {
		set_aceaddr(&info->rx_mini_ctrl.rngptr, 0);
		info->rx_mini_ctrl.flags = RCB_FLG_RNG_DISABLE;
		info->rx_mini_ctrl.max_len = 0;
	}

	ap->rx_mini_skbprd = 0;
	atomic_set(&ap->cur_mini_bufs, 0);

	set_aceaddr(&info->rx_return_ctrl.rngptr,
		    (ap->rx_ring_base_dma +
		     (sizeof(struct rx_desc) *
		      (RX_STD_RING_ENTRIES +
		       RX_JUMBO_RING_ENTRIES +
		       RX_MINI_RING_ENTRIES))));
	info->rx_return_ctrl.flags = 0;
	info->rx_return_ctrl.max_len = RX_RETURN_RING_ENTRIES;

	memset(ap->rx_return_ring, 0,
	       RX_RETURN_RING_ENTRIES * sizeof(struct rx_desc));

	set_aceaddr(&info->rx_ret_prd_ptr, ap->rx_ret_prd_dma);
	*(ap->rx_ret_prd) = 0;

	writel(TX_RING_BASE, &regs->WinBase);

	if (ACE_IS_TIGON_I(ap)) {
		ap->tx_ring = (struct tx_desc *) regs->Window;
		for (i = 0; i < (TIGON_I_TX_RING_ENTRIES
				 * sizeof(struct tx_desc)) / sizeof(u32); i++)
			writel(0, (void __iomem *)ap->tx_ring  + i * 4);

		set_aceaddr(&info->tx_ctrl.rngptr, TX_RING_BASE);
	} else {
		memset(ap->tx_ring, 0,
		       MAX_TX_RING_ENTRIES * sizeof(struct tx_desc));

		set_aceaddr(&info->tx_ctrl.rngptr, ap->tx_ring_dma);
	}

	info->tx_ctrl.max_len = ACE_TX_RING_ENTRIES(ap);
	tmp = RCB_FLG_TCP_UDP_SUM | RCB_FLG_NO_PSEUDO_HDR | ACE_RCB_VLAN_FLAG;

	/*
	 * The Tigon I does not like having the TX ring in host memory ;-(
	 */
	if (!ACE_IS_TIGON_I(ap))
		tmp |= RCB_FLG_TX_HOST_RING;
#if TX_COAL_INTS_ONLY
	tmp |= RCB_FLG_COAL_INT_ONLY;
#endif
	info->tx_ctrl.flags = tmp;

	set_aceaddr(&info->tx_csm_ptr, ap->tx_csm_dma);

	/*
	 * Potential item for tuning parameter
	 */
#if 0 /* NO */
	writel(DMA_THRESH_16W, &regs->DmaReadCfg);
	writel(DMA_THRESH_16W, &regs->DmaWriteCfg);
#else
	writel(DMA_THRESH_8W, &regs->DmaReadCfg);
	writel(DMA_THRESH_8W, &regs->DmaWriteCfg);
#endif

	writel(0, &regs->MaskInt);
	writel(1, &regs->IfIdx);
#if 0
	/*
	 * McKinley boxes do not like us fiddling with AssistState
	 * this early
	 */
	writel(1, &regs->AssistState);
#endif

	writel(DEF_STAT, &regs->TuneStatTicks);
	writel(DEF_TRACE, &regs->TuneTrace);

	ace_set_rxtx_parms(dev, 0);

	if (board_idx == BOARD_IDX_OVERFLOW) {
		printk(KERN_WARNING "%s: more than %i NICs detected, "
		       "ignoring module parameters!\n",
		       ap->name, ACE_MAX_MOD_PARMS);
	} else if (board_idx >= 0) {
		if (tx_coal_tick[board_idx])
			writel(tx_coal_tick[board_idx],
			       &regs->TuneTxCoalTicks);
		if (max_tx_desc[board_idx])
			writel(max_tx_desc[board_idx], &regs->TuneMaxTxDesc);

		if (rx_coal_tick[board_idx])
			writel(rx_coal_tick[board_idx],
			       &regs->TuneRxCoalTicks);
		if (max_rx_desc[board_idx])
			writel(max_rx_desc[board_idx], &regs->TuneMaxRxDesc);

		if (trace[board_idx])
			writel(trace[board_idx], &regs->TuneTrace);

		if ((tx_ratio[board_idx] > 0) && (tx_ratio[board_idx] < 64))
			writel(tx_ratio[board_idx], &regs->TxBufRat);
	}

	/*
	 * Default link parameters
	 */
	tmp = LNK_ENABLE | LNK_FULL_DUPLEX | LNK_1000MB | LNK_100MB |
		LNK_10MB | LNK_RX_FLOW_CTL_Y | LNK_NEG_FCTL | LNK_NEGOTIATE;
	if(ap->version >= 2)
		tmp |= LNK_TX_FLOW_CTL_Y;

	/*
	 * Override link default parameters
	 */
	if ((board_idx >= 0) && link[board_idx]) {
		int option = link[board_idx];

		tmp = LNK_ENABLE;

		if (option & 0x01) {
			printk(KERN_INFO "%s: Setting half duplex link\n",
			       ap->name);
			tmp &= ~LNK_FULL_DUPLEX;
		}
		if (option & 0x02)
			tmp &= ~LNK_NEGOTIATE;
		if (option & 0x10)
			tmp |= LNK_10MB;
		if (option & 0x20)
			tmp |= LNK_100MB;
		if (option & 0x40)
			tmp |= LNK_1000MB;
		if ((option & 0x70) == 0) {
			printk(KERN_WARNING "%s: No media speed specified, "
			       "forcing auto negotiation\n", ap->name);
			tmp |= LNK_NEGOTIATE | LNK_1000MB |
				LNK_100MB | LNK_10MB;
		}
		if ((option & 0x100) == 0)
			tmp |= LNK_NEG_FCTL;
		else
			printk(KERN_INFO "%s: Disabling flow control "
			       "negotiation\n", ap->name);
		if (option & 0x200)
			tmp |= LNK_RX_FLOW_CTL_Y;
		if ((option & 0x400) && (ap->version >= 2)) {
			printk(KERN_INFO "%s: Enabling TX flow control\n",
			       ap->name);
			tmp |= LNK_TX_FLOW_CTL_Y;
		}
	}

	ap->link = tmp;
	writel(tmp, &regs->TuneLink);
	if (ap->version >= 2)
		writel(tmp, &regs->TuneFastLink);

	if (ACE_IS_TIGON_I(ap))
		writel(tigonFwStartAddr, &regs->Pc);
	if (ap->version == 2)
		writel(tigon2FwStartAddr, &regs->Pc);

	writel(0, &regs->Mb0Lo);

	/*
	 * Set tx_csm before we start receiving interrupts, otherwise
	 * the interrupt handler might think it is supposed to process
	 * tx ints before we are up and running, which may cause a null
	 * pointer access in the int handler.
	 */
	ap->cur_rx = 0;
	ap->tx_prd = *(ap->tx_csm) = ap->tx_ret_csm = 0;

	wmb();
	ace_set_txprd(regs, ap, 0);
	writel(0, &regs->RxRetCsm);

	/*
	 * Zero the stats before starting the interface
	 */
	memset(&ap->stats, 0, sizeof(ap->stats));

       /*
	* Enable DMA engine now.
	* If we do this sooner, Mckinley box pukes.
	* I assume it's because Tigon II DMA engine wants to check
	* *something* even before the CPU is started.
	*/
       writel(1, &regs->AssistState);  /* enable DMA */

	/*
	 * Start the NIC CPU
	 */
	writel(readl(&regs->CpuCtrl) & ~(CPU_HALT|CPU_TRACE), &regs->CpuCtrl);
	readl(&regs->CpuCtrl);

	/*
	 * Wait for the firmware to spin up - max 3 seconds.
	 */
	myjif = jiffies + 3 * HZ;
	while (time_before(jiffies, myjif) && !ap->fw_running)
		cpu_relax();

	if (!ap->fw_running) {
		printk(KERN_ERR "%s: Firmware NOT running!\n", ap->name);

		ace_dump_trace(ap);
		writel(readl(&regs->CpuCtrl) | CPU_HALT, &regs->CpuCtrl);
		readl(&regs->CpuCtrl);

		/* aman@sgi.com - account for badly behaving firmware/NIC:
		 * - have observed that the NIC may continue to generate
		 *   interrupts for some reason; attempt to stop it - halt
		 *   second CPU for Tigon II cards, and also clear Mb0
		 * - if we're a module, we'll fail to load if this was
		 *   the only GbE card in the system => if the kernel does
		 *   see an interrupt from the NIC, code to handle it is
		 *   gone and OOps! - so free_irq also
		 */
		if (ap->version >= 2)
			writel(readl(&regs->CpuBCtrl) | CPU_HALT,
			       &regs->CpuBCtrl);
		writel(0, &regs->Mb0Lo);
		readl(&regs->Mb0Lo);

		ecode = -EBUSY;
		goto init_error;
	}

	/*
	 * We load the ring here as there seem to be no way to tell the
	 * firmware to wipe the ring without re-initializing it.
	 */
	if (!test_and_set_bit(0, &ap->std_refill_busy))
		ace_load_std_rx_ring(ap, RX_RING_SIZE);
	else
		printk(KERN_ERR "%s: Someone is busy refilling the RX ring\n",
		       ap->name);
	if (ap->version >= 2) {
		if (!test_and_set_bit(0, &ap->mini_refill_busy))
			ace_load_mini_rx_ring(ap, RX_MINI_SIZE);
		else
			printk(KERN_ERR "%s: Someone is busy refilling "
			       "the RX mini ring\n", ap->name);
	}
	return 0;

 init_error:
	ace_init_cleanup(dev);
	return ecode;
}


static void ace_set_rxtx_parms(struct net_device *dev, int jumbo)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	int board_idx = ap->board_idx;

	if (board_idx >= 0) {
		if (!jumbo) {
			if (!tx_coal_tick[board_idx])
				writel(DEF_TX_COAL, &regs->TuneTxCoalTicks);
			if (!max_tx_desc[board_idx])
				writel(DEF_TX_MAX_DESC, &regs->TuneMaxTxDesc);
			if (!rx_coal_tick[board_idx])
				writel(DEF_RX_COAL, &regs->TuneRxCoalTicks);
			if (!max_rx_desc[board_idx])
				writel(DEF_RX_MAX_DESC, &regs->TuneMaxRxDesc);
			if (!tx_ratio[board_idx])
				writel(DEF_TX_RATIO, &regs->TxBufRat);
		} else {
			if (!tx_coal_tick[board_idx])
				writel(DEF_JUMBO_TX_COAL,
				       &regs->TuneTxCoalTicks);
			if (!max_tx_desc[board_idx])
				writel(DEF_JUMBO_TX_MAX_DESC,
				       &regs->TuneMaxTxDesc);
			if (!rx_coal_tick[board_idx])
				writel(DEF_JUMBO_RX_COAL,
				       &regs->TuneRxCoalTicks);
			if (!max_rx_desc[board_idx])
				writel(DEF_JUMBO_RX_MAX_DESC,
				       &regs->TuneMaxRxDesc);
			if (!tx_ratio[board_idx])
				writel(DEF_JUMBO_TX_RATIO, &regs->TxBufRat);
		}
	}
}


static void ace_watchdog(struct net_device *data)
{
	struct net_device *dev = data;
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;

	/*
	 * We haven't received a stats update event for more than 2.5
	 * seconds and there is data in the transmit queue, thus we
	 * asume the card is stuck.
	 */
	if (*ap->tx_csm != ap->tx_ret_csm) {
		printk(KERN_WARNING "%s: Transmitter is stuck, %08x\n",
		       dev->name, (unsigned int)readl(&regs->HostCtrl));
		/* This can happen due to ieee flow control. */
	} else {
		printk(KERN_DEBUG "%s: BUG... transmitter died. Kicking it.\n",
		       dev->name);
#if 0
		netif_wake_queue(dev);
#endif
	}
}


static void ace_tasklet(unsigned long dev)
{
	struct ace_private *ap = netdev_priv((struct net_device *)dev);
	int cur_size;

	cur_size = atomic_read(&ap->cur_rx_bufs);
	if ((cur_size < RX_LOW_STD_THRES) &&
	    !test_and_set_bit(0, &ap->std_refill_busy)) {
#ifdef DEBUG
		printk("refilling buffers (current %i)\n", cur_size);
#endif
		ace_load_std_rx_ring(ap, RX_RING_SIZE - cur_size);
	}

	if (ap->version >= 2) {
		cur_size = atomic_read(&ap->cur_mini_bufs);
		if ((cur_size < RX_LOW_MINI_THRES) &&
		    !test_and_set_bit(0, &ap->mini_refill_busy)) {
#ifdef DEBUG
			printk("refilling mini buffers (current %i)\n",
			       cur_size);
#endif
			ace_load_mini_rx_ring(ap, RX_MINI_SIZE - cur_size);
		}
	}

	cur_size = atomic_read(&ap->cur_jumbo_bufs);
	if (ap->jumbo && (cur_size < RX_LOW_JUMBO_THRES) &&
	    !test_and_set_bit(0, &ap->jumbo_refill_busy)) {
#ifdef DEBUG
		printk("refilling jumbo buffers (current %i)\n", cur_size);
#endif
		ace_load_jumbo_rx_ring(ap, RX_JUMBO_SIZE - cur_size);
	}
	ap->tasklet_pending = 0;
}


/*
 * Copy the contents of the NIC's trace buffer to kernel memory.
 */
static void ace_dump_trace(struct ace_private *ap)
{
#if 0
	if (!ap->trace_buf)
		if (!(ap->trace_buf = kmalloc(ACE_TRACE_SIZE, GFP_KERNEL)))
		    return;
#endif
}


/*
 * Load the standard rx ring.
 *
 * Loading rings is safe without holding the spin lock since this is
 * done only before the device is enabled, thus no interrupts are
 * generated and by the interrupt handler/tasklet handler.
 */
static void ace_load_std_rx_ring(struct ace_private *ap, int nr_bufs)
{
	struct ace_regs __iomem *regs = ap->regs;
	short i, idx;


	prefetchw(&ap->cur_rx_bufs);

	idx = ap->rx_std_skbprd;

	for (i = 0; i < nr_bufs; i++) {
		struct sk_buff *skb;
		struct rx_desc *rd;
		dma_addr_t mapping;

		skb = alloc_skb(ACE_STD_BUFSIZE + NET_IP_ALIGN, GFP_ATOMIC);
		if (!skb)
			break;

		skb_reserve(skb, NET_IP_ALIGN);
		mapping = pci_map_page(ap->pdev, virt_to_page(skb->data),
				       offset_in_page(skb->data),
				       ACE_STD_BUFSIZE,
				       PCI_DMA_FROMDEVICE);
		ap->skb->rx_std_skbuff[idx].skb = skb;
		pci_unmap_addr_set(&ap->skb->rx_std_skbuff[idx],
				   mapping, mapping);

		rd = &ap->rx_std_ring[idx];
		set_aceaddr(&rd->addr, mapping);
		rd->size = ACE_STD_BUFSIZE;
		rd->idx = idx;
		idx = (idx + 1) % RX_STD_RING_ENTRIES;
	}

	if (!i)
		goto error_out;

	atomic_add(i, &ap->cur_rx_bufs);
	ap->rx_std_skbprd = idx;

	if (ACE_IS_TIGON_I(ap)) {
		struct cmd cmd;
		cmd.evt = C_SET_RX_PRD_IDX;
		cmd.code = 0;
		cmd.idx = ap->rx_std_skbprd;
		ace_issue_cmd(regs, &cmd);
	} else {
		writel(idx, &regs->RxStdPrd);
		wmb();
	}

 out:
	clear_bit(0, &ap->std_refill_busy);
	return;

 error_out:
	printk(KERN_INFO "Out of memory when allocating "
	       "standard receive buffers\n");
	goto out;
}


static void ace_load_mini_rx_ring(struct ace_private *ap, int nr_bufs)
{
	struct ace_regs __iomem *regs = ap->regs;
	short i, idx;

	prefetchw(&ap->cur_mini_bufs);

	idx = ap->rx_mini_skbprd;
	for (i = 0; i < nr_bufs; i++) {
		struct sk_buff *skb;
		struct rx_desc *rd;
		dma_addr_t mapping;

		skb = alloc_skb(ACE_MINI_BUFSIZE + NET_IP_ALIGN, GFP_ATOMIC);
		if (!skb)
			break;

		skb_reserve(skb, NET_IP_ALIGN);
		mapping = pci_map_page(ap->pdev, virt_to_page(skb->data),
				       offset_in_page(skb->data),
				       ACE_MINI_BUFSIZE,
				       PCI_DMA_FROMDEVICE);
		ap->skb->rx_mini_skbuff[idx].skb = skb;
		pci_unmap_addr_set(&ap->skb->rx_mini_skbuff[idx],
				   mapping, mapping);

		rd = &ap->rx_mini_ring[idx];
		set_aceaddr(&rd->addr, mapping);
		rd->size = ACE_MINI_BUFSIZE;
		rd->idx = idx;
		idx = (idx + 1) % RX_MINI_RING_ENTRIES;
	}

	if (!i)
		goto error_out;

	atomic_add(i, &ap->cur_mini_bufs);

	ap->rx_mini_skbprd = idx;

	writel(idx, &regs->RxMiniPrd);
	wmb();

 out:
	clear_bit(0, &ap->mini_refill_busy);
	return;
 error_out:
	printk(KERN_INFO "Out of memory when allocating "
	       "mini receive buffers\n");
	goto out;
}


/*
 * Load the jumbo rx ring, this may happen at any time if the MTU
 * is changed to a value > 1500.
 */
static void ace_load_jumbo_rx_ring(struct ace_private *ap, int nr_bufs)
{
	struct ace_regs __iomem *regs = ap->regs;
	short i, idx;

	idx = ap->rx_jumbo_skbprd;

	for (i = 0; i < nr_bufs; i++) {
		struct sk_buff *skb;
		struct rx_desc *rd;
		dma_addr_t mapping;

		skb = alloc_skb(ACE_JUMBO_BUFSIZE + NET_IP_ALIGN, GFP_ATOMIC);
		if (!skb)
			break;

		skb_reserve(skb, NET_IP_ALIGN);
		mapping = pci_map_page(ap->pdev, virt_to_page(skb->data),
				       offset_in_page(skb->data),
				       ACE_JUMBO_BUFSIZE,
				       PCI_DMA_FROMDEVICE);
		ap->skb->rx_jumbo_skbuff[idx].skb = skb;
		pci_unmap_addr_set(&ap->skb->rx_jumbo_skbuff[idx],
				   mapping, mapping);

		rd = &ap->rx_jumbo_ring[idx];
		set_aceaddr(&rd->addr, mapping);
		rd->size = ACE_JUMBO_BUFSIZE;
		rd->idx = idx;
		idx = (idx + 1) % RX_JUMBO_RING_ENTRIES;
	}

	if (!i)
		goto error_out;

	atomic_add(i, &ap->cur_jumbo_bufs);
	ap->rx_jumbo_skbprd = idx;

	if (ACE_IS_TIGON_I(ap)) {
		struct cmd cmd;
		cmd.evt = C_SET_RX_JUMBO_PRD_IDX;
		cmd.code = 0;
		cmd.idx = ap->rx_jumbo_skbprd;
		ace_issue_cmd(regs, &cmd);
	} else {
		writel(idx, &regs->RxJumboPrd);
		wmb();
	}

 out:
	clear_bit(0, &ap->jumbo_refill_busy);
	return;
 error_out:
	if (net_ratelimit())
		printk(KERN_INFO "Out of memory when allocating "
		       "jumbo receive buffers\n");
	goto out;
}


/*
 * All events are considered to be slow (RX/TX ints do not generate
 * events) and are handled here, outside the main interrupt handler,
 * to reduce the size of the handler.
 */
static u32 ace_handle_event(struct net_device *dev, u32 evtcsm, u32 evtprd)
{
	struct ace_private *ap;

	ap = netdev_priv(dev);

	while (evtcsm != evtprd) {
		switch (ap->evt_ring[evtcsm].evt) {
		case E_FW_RUNNING:
			printk(KERN_INFO "%s: Firmware up and running\n",
			       ap->name);
			ap->fw_running = 1;
			wmb();
			break;
		case E_STATS_UPDATED:
			break;
		case E_LNK_STATE:
		{
			u16 code = ap->evt_ring[evtcsm].code;
			switch (code) {
			case E_C_LINK_UP:
			{
				u32 state = readl(&ap->regs->GigLnkState);
				printk(KERN_WARNING "%s: Optical link UP "
				       "(%s Duplex, Flow Control: %s%s)\n",
				       ap->name,
				       state & LNK_FULL_DUPLEX ? "Full":"Half",
				       state & LNK_TX_FLOW_CTL_Y ? "TX " : "",
				       state & LNK_RX_FLOW_CTL_Y ? "RX" : "");
				break;
			}
			case E_C_LINK_DOWN:
				printk(KERN_WARNING "%s: Optical link DOWN\n",
				       ap->name);
				break;
			case E_C_LINK_10_100:
				printk(KERN_WARNING "%s: 10/100BaseT link "
				       "UP\n", ap->name);
				break;
			default:
				printk(KERN_ERR "%s: Unknown optical link "
				       "state %02x\n", ap->name, code);
			}
			break;
		}
		case E_ERROR:
			switch(ap->evt_ring[evtcsm].code) {
			case E_C_ERR_INVAL_CMD:
				printk(KERN_ERR "%s: invalid command error\n",
				       ap->name);
				break;
			case E_C_ERR_UNIMP_CMD:
				printk(KERN_ERR "%s: unimplemented command "
				       "error\n", ap->name);
				break;
			case E_C_ERR_BAD_CFG:
				printk(KERN_ERR "%s: bad config error\n",
				       ap->name);
				break;
			default:
				printk(KERN_ERR "%s: unknown error %02x\n",
				       ap->name, ap->evt_ring[evtcsm].code);
			}
			break;
		case E_RESET_JUMBO_RNG:
		{
			int i;
			for (i = 0; i < RX_JUMBO_RING_ENTRIES; i++) {
				if (ap->skb->rx_jumbo_skbuff[i].skb) {
					ap->rx_jumbo_ring[i].size = 0;
					set_aceaddr(&ap->rx_jumbo_ring[i].addr, 0);
					dev_kfree_skb(ap->skb->rx_jumbo_skbuff[i].skb);
					ap->skb->rx_jumbo_skbuff[i].skb = NULL;
				}
			}

 			if (ACE_IS_TIGON_I(ap)) {
 				struct cmd cmd;
 				cmd.evt = C_SET_RX_JUMBO_PRD_IDX;
 				cmd.code = 0;
 				cmd.idx = 0;
 				ace_issue_cmd(ap->regs, &cmd);
 			} else {
 				writel(0, &((ap->regs)->RxJumboPrd));
 				wmb();
 			}

			ap->jumbo = 0;
			ap->rx_jumbo_skbprd = 0;
			printk(KERN_INFO "%s: Jumbo ring flushed\n",
			       ap->name);
			clear_bit(0, &ap->jumbo_refill_busy);
			break;
		}
		default:
			printk(KERN_ERR "%s: Unhandled event 0x%02x\n",
			       ap->name, ap->evt_ring[evtcsm].evt);
		}
		evtcsm = (evtcsm + 1) % EVT_RING_ENTRIES;
	}

	return evtcsm;
}


static void ace_rx_int(struct net_device *dev, u32 rxretprd, u32 rxretcsm)
{
	struct ace_private *ap = netdev_priv(dev);
	u32 idx;
	int mini_count = 0, std_count = 0;

	idx = rxretcsm;

	prefetchw(&ap->cur_rx_bufs);
	prefetchw(&ap->cur_mini_bufs);

	while (idx != rxretprd) {
		struct ring_info *rip;
		struct sk_buff *skb;
		struct rx_desc *rxdesc, *retdesc;
		u32 skbidx;
		int bd_flags, desc_type, mapsize;
		u16 csum;


		/* make sure the rx descriptor isn't read before rxretprd */
		if (idx == rxretcsm)
			rmb();

		retdesc = &ap->rx_return_ring[idx];
		skbidx = retdesc->idx;
		bd_flags = retdesc->flags;
		desc_type = bd_flags & (BD_FLG_JUMBO | BD_FLG_MINI);

		switch(desc_type) {
			/*
			 * Normal frames do not have any flags set
			 *
			 * Mini and normal frames arrive frequently,
			 * so use a local counter to avoid doing
			 * atomic operations for each packet arriving.
			 */
		case 0:
			rip = &ap->skb->rx_std_skbuff[skbidx];
			mapsize = ACE_STD_BUFSIZE;
			rxdesc = &ap->rx_std_ring[skbidx];
			std_count++;
			break;
		case BD_FLG_JUMBO:
			rip = &ap->skb->rx_jumbo_skbuff[skbidx];
			mapsize = ACE_JUMBO_BUFSIZE;
			rxdesc = &ap->rx_jumbo_ring[skbidx];
			atomic_dec(&ap->cur_jumbo_bufs);
			break;
		case BD_FLG_MINI:
			rip = &ap->skb->rx_mini_skbuff[skbidx];
			mapsize = ACE_MINI_BUFSIZE;
			rxdesc = &ap->rx_mini_ring[skbidx];
			mini_count++;
			break;
		default:
			printk(KERN_INFO "%s: unknown frame type (0x%02x) "
			       "returned by NIC\n", dev->name,
			       retdesc->flags);
			goto error;
		}

		skb = rip->skb;
		rip->skb = NULL;
		pci_unmap_page(ap->pdev,
			       pci_unmap_addr(rip, mapping),
			       mapsize,
			       PCI_DMA_FROMDEVICE);
		skb_put(skb, retdesc->size);

		/*
		 * Fly baby, fly!
		 */
		csum = retdesc->tcp_udp_csum;

		skb->protocol = eth_type_trans(skb, dev);

		/*
		 * Instead of forcing the poor tigon mips cpu to calculate
		 * pseudo hdr checksum, we do this ourselves.
		 */
		if (bd_flags & BD_FLG_TCP_UDP_SUM) {
			skb->csum = htons(csum);
			skb->ip_summed = CHECKSUM_COMPLETE;
		} else {
			skb->ip_summed = CHECKSUM_NONE;
		}

		/* send it up */
#if ACENIC_DO_VLAN
		if (ap->vlgrp && (bd_flags & BD_FLG_VLAN_TAG)) {
			vlan_hwaccel_rx(skb, ap->vlgrp, retdesc->vlan);
		} else
#endif
			netif_rx(skb);

		dev->last_rx = jiffies;
		ap->stats.rx_packets++;
		ap->stats.rx_bytes += retdesc->size;

		idx = (idx + 1) % RX_RETURN_RING_ENTRIES;
	}

	atomic_sub(std_count, &ap->cur_rx_bufs);
	if (!ACE_IS_TIGON_I(ap))
		atomic_sub(mini_count, &ap->cur_mini_bufs);

 out:
	/*
	 * According to the documentation RxRetCsm is obsolete with
	 * the 12.3.x Firmware - my Tigon I NICs seem to disagree!
	 */
	if (ACE_IS_TIGON_I(ap)) {
		writel(idx, &ap->regs->RxRetCsm);
	}
	ap->cur_rx = idx;

	return;
 error:
	idx = rxretprd;
	goto out;
}


static inline void ace_tx_int(struct net_device *dev,
			      u32 txcsm, u32 idx)
{
	struct ace_private *ap = netdev_priv(dev);

	do {
		struct sk_buff *skb;
		dma_addr_t mapping;
		struct tx_ring_info *info;

		info = ap->skb->tx_skbuff + idx;
		skb = info->skb;
		mapping = pci_unmap_addr(info, mapping);

		if (mapping) {
			pci_unmap_page(ap->pdev, mapping,
				       pci_unmap_len(info, maplen),
				       PCI_DMA_TODEVICE);
			pci_unmap_addr_set(info, mapping, 0);
		}

		if (skb) {
			ap->stats.tx_packets++;
			ap->stats.tx_bytes += skb->len;
			dev_kfree_skb_irq(skb);
			info->skb = NULL;
		}

		idx = (idx + 1) % ACE_TX_RING_ENTRIES(ap);
	} while (idx != txcsm);

	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);

	wmb();
	ap->tx_ret_csm = txcsm;

	/* So... tx_ret_csm is advanced _after_ check for device wakeup.
	 *
	 * We could try to make it before. In this case we would get
	 * the following race condition: hard_start_xmit on other cpu
	 * enters after we advanced tx_ret_csm and fills space,
	 * which we have just freed, so that we make illegal device wakeup.
	 * There is no good way to workaround this (at entry
	 * to ace_start_xmit detects this condition and prevents
	 * ring corruption, but it is not a good workaround.)
	 *
	 * When tx_ret_csm is advanced after, we wake up device _only_
	 * if we really have some space in ring (though the core doing
	 * hard_start_xmit can see full ring for some period and has to
	 * synchronize.) Superb.
	 * BUT! We get another subtle race condition. hard_start_xmit
	 * may think that ring is full between wakeup and advancing
	 * tx_ret_csm and will stop device instantly! It is not so bad.
	 * We are guaranteed that there is something in ring, so that
	 * the next irq will resume transmission. To speedup this we could
	 * mark descriptor, which closes ring with BD_FLG_COAL_NOW
	 * (see ace_start_xmit).
	 *
	 * Well, this dilemma exists in all lock-free devices.
	 * We, following scheme used in drivers by Donald Becker,
	 * select the least dangerous.
	 *							--ANK
	 */
}


static irqreturn_t ace_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	u32 idx;
	u32 txcsm, rxretcsm, rxretprd;
	u32 evtcsm, evtprd;

	/*
	 * In case of PCI shared interrupts or spurious interrupts,
	 * we want to make sure it is actually our interrupt before
	 * spending any time in here.
	 */
	if (!(readl(&regs->HostCtrl) & IN_INT))
		return IRQ_NONE;

	/*
	 * ACK intr now. Otherwise we will lose updates to rx_ret_prd,
	 * which happened _after_ rxretprd = *ap->rx_ret_prd; but before
	 * writel(0, &regs->Mb0Lo).
	 *
	 * "IRQ avoidance" recommended in docs applies to IRQs served
	 * threads and it is wrong even for that case.
	 */
	writel(0, &regs->Mb0Lo);
	readl(&regs->Mb0Lo);

	/*
	 * There is no conflict between transmit handling in
	 * start_xmit and receive processing, thus there is no reason
	 * to take a spin lock for RX handling. Wait until we start
	 * working on the other stuff - hey we don't need a spin lock
	 * anymore.
	 */
	rxretprd = *ap->rx_ret_prd;
	rxretcsm = ap->cur_rx;

	if (rxretprd != rxretcsm)
		ace_rx_int(dev, rxretprd, rxretcsm);

	txcsm = *ap->tx_csm;
	idx = ap->tx_ret_csm;

	if (txcsm != idx) {
		/*
		 * If each skb takes only one descriptor this check degenerates
		 * to identity, because new space has just been opened.
		 * But if skbs are fragmented we must check that this index
		 * update releases enough of space, otherwise we just
		 * wait for device to make more work.
		 */
		if (!tx_ring_full(ap, txcsm, ap->tx_prd))
			ace_tx_int(dev, txcsm, idx);
	}

	evtcsm = readl(&regs->EvtCsm);
	evtprd = *ap->evt_prd;

	if (evtcsm != evtprd) {
		evtcsm = ace_handle_event(dev, evtcsm, evtprd);
		writel(evtcsm, &regs->EvtCsm);
	}

	/*
	 * This has to go last in the interrupt handler and run with
	 * the spin lock released ... what lock?
	 */
	if (netif_running(dev)) {
		int cur_size;
		int run_tasklet = 0;

		cur_size = atomic_read(&ap->cur_rx_bufs);
		if (cur_size < RX_LOW_STD_THRES) {
			if ((cur_size < RX_PANIC_STD_THRES) &&
			    !test_and_set_bit(0, &ap->std_refill_busy)) {
#ifdef DEBUG
				printk("low on std buffers %i\n", cur_size);
#endif
				ace_load_std_rx_ring(ap,
						     RX_RING_SIZE - cur_size);
			} else
				run_tasklet = 1;
		}

		if (!ACE_IS_TIGON_I(ap)) {
			cur_size = atomic_read(&ap->cur_mini_bufs);
			if (cur_size < RX_LOW_MINI_THRES) {
				if ((cur_size < RX_PANIC_MINI_THRES) &&
				    !test_and_set_bit(0,
						      &ap->mini_refill_busy)) {
#ifdef DEBUG
					printk("low on mini buffers %i\n",
					       cur_size);
#endif
					ace_load_mini_rx_ring(ap, RX_MINI_SIZE - cur_size);
				} else
					run_tasklet = 1;
			}
		}

		if (ap->jumbo) {
			cur_size = atomic_read(&ap->cur_jumbo_bufs);
			if (cur_size < RX_LOW_JUMBO_THRES) {
				if ((cur_size < RX_PANIC_JUMBO_THRES) &&
				    !test_and_set_bit(0,
						      &ap->jumbo_refill_busy)){
#ifdef DEBUG
					printk("low on jumbo buffers %i\n",
					       cur_size);
#endif
					ace_load_jumbo_rx_ring(ap, RX_JUMBO_SIZE - cur_size);
				} else
					run_tasklet = 1;
			}
		}
		if (run_tasklet && !ap->tasklet_pending) {
			ap->tasklet_pending = 1;
			tasklet_schedule(&ap->ace_tasklet);
		}
	}

	return IRQ_HANDLED;
}


#if ACENIC_DO_VLAN
static void ace_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct ace_private *ap = netdev_priv(dev);
	unsigned long flags;

	local_irq_save(flags);
	ace_mask_irq(dev);

	ap->vlgrp = grp;

	ace_unmask_irq(dev);
	local_irq_restore(flags);
}


static void ace_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct ace_private *ap = netdev_priv(dev);
	unsigned long flags;

	local_irq_save(flags);
	ace_mask_irq(dev);
	vlan_group_set_device(ap->vlgrp, vid, NULL);
	ace_unmask_irq(dev);
	local_irq_restore(flags);
}
#endif /* ACENIC_DO_VLAN */


static int ace_open(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	struct cmd cmd;

	if (!(ap->fw_running)) {
		printk(KERN_WARNING "%s: Firmware not running!\n", dev->name);
		return -EBUSY;
	}

	writel(dev->mtu + ETH_HLEN + 4, &regs->IfMtu);

	cmd.evt = C_CLEAR_STATS;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	cmd.evt = C_HOST_STATE;
	cmd.code = C_C_STACK_UP;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	if (ap->jumbo &&
	    !test_and_set_bit(0, &ap->jumbo_refill_busy))
		ace_load_jumbo_rx_ring(ap, RX_JUMBO_SIZE);

	if (dev->flags & IFF_PROMISC) {
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);

		ap->promisc = 1;
	}else
		ap->promisc = 0;
	ap->mcast_all = 0;

#if 0
	cmd.evt = C_LNK_NEGOTIATION;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);
#endif

	netif_start_queue(dev);

	/*
	 * Setup the bottom half rx ring refill handler
	 */
	tasklet_init(&ap->ace_tasklet, ace_tasklet, (unsigned long)dev);
	return 0;
}


static int ace_close(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	struct cmd cmd;
	unsigned long flags;
	short i;

	/*
	 * Without (or before) releasing irq and stopping hardware, this
	 * is an absolute non-sense, by the way. It will be reset instantly
	 * by the first irq.
	 */
	netif_stop_queue(dev);


	if (ap->promisc) {
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->promisc = 0;
	}

	cmd.evt = C_HOST_STATE;
	cmd.code = C_C_STACK_DOWN;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	tasklet_kill(&ap->ace_tasklet);

	/*
	 * Make sure one CPU is not processing packets while
	 * buffers are being released by another.
	 */

	local_irq_save(flags);
	ace_mask_irq(dev);

	for (i = 0; i < ACE_TX_RING_ENTRIES(ap); i++) {
		struct sk_buff *skb;
		dma_addr_t mapping;
		struct tx_ring_info *info;

		info = ap->skb->tx_skbuff + i;
		skb = info->skb;
		mapping = pci_unmap_addr(info, mapping);

		if (mapping) {
			if (ACE_IS_TIGON_I(ap)) {
				struct tx_desc __iomem *tx
					= (struct tx_desc __iomem *) &ap->tx_ring[i];
				writel(0, &tx->addr.addrhi);
				writel(0, &tx->addr.addrlo);
				writel(0, &tx->flagsize);
			} else
				memset(ap->tx_ring + i, 0,
				       sizeof(struct tx_desc));
			pci_unmap_page(ap->pdev, mapping,
				       pci_unmap_len(info, maplen),
				       PCI_DMA_TODEVICE);
			pci_unmap_addr_set(info, mapping, 0);
		}
		if (skb) {
			dev_kfree_skb(skb);
			info->skb = NULL;
		}
	}

	if (ap->jumbo) {
		cmd.evt = C_RESET_JUMBO_RNG;
		cmd.code = 0;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}

	ace_unmask_irq(dev);
	local_irq_restore(flags);

	return 0;
}


static inline dma_addr_t
ace_map_tx_skb(struct ace_private *ap, struct sk_buff *skb,
	       struct sk_buff *tail, u32 idx)
{
	dma_addr_t mapping;
	struct tx_ring_info *info;

	mapping = pci_map_page(ap->pdev, virt_to_page(skb->data),
			       offset_in_page(skb->data),
			       skb->len, PCI_DMA_TODEVICE);

	info = ap->skb->tx_skbuff + idx;
	info->skb = tail;
	pci_unmap_addr_set(info, mapping, mapping);
	pci_unmap_len_set(info, maplen, skb->len);
	return mapping;
}


static inline void
ace_load_tx_bd(struct ace_private *ap, struct tx_desc *desc, u64 addr,
	       u32 flagsize, u32 vlan_tag)
{
#if !USE_TX_COAL_NOW
	flagsize &= ~BD_FLG_COAL_NOW;
#endif

	if (ACE_IS_TIGON_I(ap)) {
		struct tx_desc __iomem *io = (struct tx_desc __iomem *) desc;
		writel(addr >> 32, &io->addr.addrhi);
		writel(addr & 0xffffffff, &io->addr.addrlo);
		writel(flagsize, &io->flagsize);
#if ACENIC_DO_VLAN
		writel(vlan_tag, &io->vlanres);
#endif
	} else {
		desc->addr.addrhi = addr >> 32;
		desc->addr.addrlo = addr;
		desc->flagsize = flagsize;
#if ACENIC_DO_VLAN
		desc->vlanres = vlan_tag;
#endif
	}
}


static int ace_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	struct tx_desc *desc;
	u32 idx, flagsize;
	unsigned long maxjiff = jiffies + 3*HZ;

restart:
	idx = ap->tx_prd;

	if (tx_ring_full(ap, ap->tx_ret_csm, idx))
		goto overflow;

	if (!skb_shinfo(skb)->nr_frags)	{
		dma_addr_t mapping;
		u32 vlan_tag = 0;

		mapping = ace_map_tx_skb(ap, skb, skb, idx);
		flagsize = (skb->len << 16) | (BD_FLG_END);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			flagsize |= BD_FLG_TCP_UDP_SUM;
#if ACENIC_DO_VLAN
		if (vlan_tx_tag_present(skb)) {
			flagsize |= BD_FLG_VLAN_TAG;
			vlan_tag = vlan_tx_tag_get(skb);
		}
#endif
		desc = ap->tx_ring + idx;
		idx = (idx + 1) % ACE_TX_RING_ENTRIES(ap);

		/* Look at ace_tx_int for explanations. */
		if (tx_ring_full(ap, ap->tx_ret_csm, idx))
			flagsize |= BD_FLG_COAL_NOW;

		ace_load_tx_bd(ap, desc, mapping, flagsize, vlan_tag);
	} else {
		dma_addr_t mapping;
		u32 vlan_tag = 0;
		int i, len = 0;

		mapping = ace_map_tx_skb(ap, skb, NULL, idx);
		flagsize = (skb_headlen(skb) << 16);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			flagsize |= BD_FLG_TCP_UDP_SUM;
#if ACENIC_DO_VLAN
		if (vlan_tx_tag_present(skb)) {
			flagsize |= BD_FLG_VLAN_TAG;
			vlan_tag = vlan_tx_tag_get(skb);
		}
#endif

		ace_load_tx_bd(ap, ap->tx_ring + idx, mapping, flagsize, vlan_tag);

		idx = (idx + 1) % ACE_TX_RING_ENTRIES(ap);

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			struct tx_ring_info *info;

			len += frag->size;
			info = ap->skb->tx_skbuff + idx;
			desc = ap->tx_ring + idx;

			mapping = pci_map_page(ap->pdev, frag->page,
					       frag->page_offset, frag->size,
					       PCI_DMA_TODEVICE);

			flagsize = (frag->size << 16);
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				flagsize |= BD_FLG_TCP_UDP_SUM;
			idx = (idx + 1) % ACE_TX_RING_ENTRIES(ap);

			if (i == skb_shinfo(skb)->nr_frags - 1) {
				flagsize |= BD_FLG_END;
				if (tx_ring_full(ap, ap->tx_ret_csm, idx))
					flagsize |= BD_FLG_COAL_NOW;

				/*
				 * Only the last fragment frees
				 * the skb!
				 */
				info->skb = skb;
			} else {
				info->skb = NULL;
			}
			pci_unmap_addr_set(info, mapping, mapping);
			pci_unmap_len_set(info, maplen, frag->size);
			ace_load_tx_bd(ap, desc, mapping, flagsize, vlan_tag);
		}
	}

 	wmb();
 	ap->tx_prd = idx;
 	ace_set_txprd(regs, ap, idx);

	if (flagsize & BD_FLG_COAL_NOW) {
		netif_stop_queue(dev);

		/*
		 * A TX-descriptor producer (an IRQ) might have gotten
		 * inbetween, making the ring free again. Since xmit is
		 * serialized, this is the only situation we have to
		 * re-test.
		 */
		if (!tx_ring_full(ap, ap->tx_ret_csm, idx))
			netif_wake_queue(dev);
	}

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;

overflow:
	/*
	 * This race condition is unavoidable with lock-free drivers.
	 * We wake up the queue _before_ tx_prd is advanced, so that we can
	 * enter hard_start_xmit too early, while tx ring still looks closed.
	 * This happens ~1-4 times per 100000 packets, so that we can allow
	 * to loop syncing to other CPU. Probably, we need an additional
	 * wmb() in ace_tx_intr as well.
	 *
	 * Note that this race is relieved by reserving one more entry
	 * in tx ring than it is necessary (see original non-SG driver).
	 * However, with SG we need to reserve 2*MAX_SKB_FRAGS+1, which
	 * is already overkill.
	 *
	 * Alternative is to return with 1 not throttling queue. In this
	 * case loop becomes longer, no more useful effects.
	 */
	if (time_before(jiffies, maxjiff)) {
		barrier();
		cpu_relax();
		goto restart;
	}

	/* The ring is stuck full. */
	printk(KERN_WARNING "%s: Transmit ring stuck full\n", dev->name);
	return NETDEV_TX_BUSY;
}


static int ace_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;

	if (new_mtu > ACE_JUMBO_MTU)
		return -EINVAL;

	writel(new_mtu + ETH_HLEN + 4, &regs->IfMtu);
	dev->mtu = new_mtu;

	if (new_mtu > ACE_STD_MTU) {
		if (!(ap->jumbo)) {
			printk(KERN_INFO "%s: Enabling Jumbo frame "
			       "support\n", dev->name);
			ap->jumbo = 1;
			if (!test_and_set_bit(0, &ap->jumbo_refill_busy))
				ace_load_jumbo_rx_ring(ap, RX_JUMBO_SIZE);
			ace_set_rxtx_parms(dev, 1);
		}
	} else {
		while (test_and_set_bit(0, &ap->jumbo_refill_busy));
		ace_sync_irq(dev->irq);
		ace_set_rxtx_parms(dev, 0);
		if (ap->jumbo) {
			struct cmd cmd;

			cmd.evt = C_RESET_JUMBO_RNG;
			cmd.code = 0;
			cmd.idx = 0;
			ace_issue_cmd(regs, &cmd);
		}
	}

	return 0;
}

static int ace_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	u32 link;

	memset(ecmd, 0, sizeof(struct ethtool_cmd));
	ecmd->supported =
		(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
		 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
		 SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full |
		 SUPPORTED_Autoneg | SUPPORTED_FIBRE);

	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_INTERNAL;

	link = readl(&regs->GigLnkState);
	if (link & LNK_1000MB)
		ecmd->speed = SPEED_1000;
	else {
		link = readl(&regs->FastLnkState);
		if (link & LNK_100MB)
			ecmd->speed = SPEED_100;
		else if (link & LNK_10MB)
			ecmd->speed = SPEED_10;
		else
			ecmd->speed = 0;
	}
	if (link & LNK_FULL_DUPLEX)
		ecmd->duplex = DUPLEX_FULL;
	else
		ecmd->duplex = DUPLEX_HALF;

	if (link & LNK_NEGOTIATE)
		ecmd->autoneg = AUTONEG_ENABLE;
	else
		ecmd->autoneg = AUTONEG_DISABLE;

#if 0
	/*
	 * Current struct ethtool_cmd is insufficient
	 */
	ecmd->trace = readl(&regs->TuneTrace);

	ecmd->txcoal = readl(&regs->TuneTxCoalTicks);
	ecmd->rxcoal = readl(&regs->TuneRxCoalTicks);
#endif
	ecmd->maxtxpkt = readl(&regs->TuneMaxTxDesc);
	ecmd->maxrxpkt = readl(&regs->TuneMaxRxDesc);

	return 0;
}

static int ace_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	u32 link, speed;

	link = readl(&regs->GigLnkState);
	if (link & LNK_1000MB)
		speed = SPEED_1000;
	else {
		link = readl(&regs->FastLnkState);
		if (link & LNK_100MB)
			speed = SPEED_100;
		else if (link & LNK_10MB)
			speed = SPEED_10;
		else
			speed = SPEED_100;
	}

	link = LNK_ENABLE | LNK_1000MB | LNK_100MB | LNK_10MB |
		LNK_RX_FLOW_CTL_Y | LNK_NEG_FCTL;
	if (!ACE_IS_TIGON_I(ap))
		link |= LNK_TX_FLOW_CTL_Y;
	if (ecmd->autoneg == AUTONEG_ENABLE)
		link |= LNK_NEGOTIATE;
	if (ecmd->speed != speed) {
		link &= ~(LNK_1000MB | LNK_100MB | LNK_10MB);
		switch (speed) {
		case SPEED_1000:
			link |= LNK_1000MB;
			break;
		case SPEED_100:
			link |= LNK_100MB;
			break;
		case SPEED_10:
			link |= LNK_10MB;
			break;
		}
	}

	if (ecmd->duplex == DUPLEX_FULL)
		link |= LNK_FULL_DUPLEX;

	if (link != ap->link) {
		struct cmd cmd;
		printk(KERN_INFO "%s: Renegotiating link state\n",
		       dev->name);

		ap->link = link;
		writel(link, &regs->TuneLink);
		if (!ACE_IS_TIGON_I(ap))
			writel(link, &regs->TuneFastLink);
		wmb();

		cmd.evt = C_LNK_NEGOTIATION;
		cmd.code = 0;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}
	return 0;
}

static void ace_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct ace_private *ap = netdev_priv(dev);

	strlcpy(info->driver, "acenic", sizeof(info->driver));
	snprintf(info->version, sizeof(info->version), "%i.%i.%i",
		tigonFwReleaseMajor, tigonFwReleaseMinor,
		tigonFwReleaseFix);

	if (ap->pdev)
		strlcpy(info->bus_info, pci_name(ap->pdev),
			sizeof(info->bus_info));

}

/*
 * Set the hardware MAC address.
 */
static int ace_set_mac_addr(struct net_device *dev, void *p)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	struct sockaddr *addr=p;
	u8 *da;
	struct cmd cmd;

	if(netif_running(dev))
		return -EBUSY;

	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);

	da = (u8 *)dev->dev_addr;

	writel(da[0] << 8 | da[1], &regs->MacAddrHi);
	writel((da[2] << 24) | (da[3] << 16) | (da[4] << 8) | da[5],
	       &regs->MacAddrLo);

	cmd.evt = C_SET_MAC_ADDR;
	cmd.code = 0;
	cmd.idx = 0;
	ace_issue_cmd(regs, &cmd);

	return 0;
}


static void ace_set_multicast_list(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	struct cmd cmd;

	if ((dev->flags & IFF_ALLMULTI) && !(ap->mcast_all)) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->mcast_all = 1;
	} else if (ap->mcast_all) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->mcast_all = 0;
	}

	if ((dev->flags & IFF_PROMISC) && !(ap->promisc)) {
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->promisc = 1;
	}else if (!(dev->flags & IFF_PROMISC) && (ap->promisc)) {
		cmd.evt = C_SET_PROMISC_MODE;
		cmd.code = C_C_PROMISC_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
		ap->promisc = 0;
	}

	/*
	 * For the time being multicast relies on the upper layers
	 * filtering it properly. The Firmware does not allow one to
	 * set the entire multicast list at a time and keeping track of
	 * it here is going to be messy.
	 */
	if ((dev->mc_count) && !(ap->mcast_all)) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_ENABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}else if (!ap->mcast_all) {
		cmd.evt = C_SET_MULTICAST_MODE;
		cmd.code = C_C_MCAST_DISABLE;
		cmd.idx = 0;
		ace_issue_cmd(regs, &cmd);
	}
}


static struct net_device_stats *ace_get_stats(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_mac_stats __iomem *mac_stats =
		(struct ace_mac_stats __iomem *)ap->regs->Stats;

	ap->stats.rx_missed_errors = readl(&mac_stats->drop_space);
	ap->stats.multicast = readl(&mac_stats->kept_mc);
	ap->stats.collisions = readl(&mac_stats->coll);

	return &ap->stats;
}


static void __devinit ace_copy(struct ace_regs __iomem *regs, void *src,
			    u32 dest, int size)
{
	void __iomem *tdest;
	u32 *wsrc;
	short tsize, i;

	if (size <= 0)
		return;

	while (size > 0) {
		tsize = min_t(u32, ((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
			    min_t(u32, size, ACE_WINDOW_SIZE));
		tdest = (void __iomem *) &regs->Window +
			(dest & (ACE_WINDOW_SIZE - 1));
		writel(dest & ~(ACE_WINDOW_SIZE - 1), &regs->WinBase);
		/*
		 * This requires byte swapping on big endian, however
		 * writel does that for us
		 */
		wsrc = src;
		for (i = 0; i < (tsize / 4); i++) {
			writel(wsrc[i], tdest + i*4);
		}
		dest += tsize;
		src += tsize;
		size -= tsize;
	}

	return;
}


static void __devinit ace_clear(struct ace_regs __iomem *regs, u32 dest, int size)
{
	void __iomem *tdest;
	short tsize = 0, i;

	if (size <= 0)
		return;

	while (size > 0) {
		tsize = min_t(u32, ((~dest & (ACE_WINDOW_SIZE - 1)) + 1),
				min_t(u32, size, ACE_WINDOW_SIZE));
		tdest = (void __iomem *) &regs->Window +
			(dest & (ACE_WINDOW_SIZE - 1));
		writel(dest & ~(ACE_WINDOW_SIZE - 1), &regs->WinBase);

		for (i = 0; i < (tsize / 4); i++) {
			writel(0, tdest + i*4);
		}

		dest += tsize;
		size -= tsize;
	}

	return;
}


/*
 * Download the firmware into the SRAM on the NIC
 *
 * This operation requires the NIC to be halted and is performed with
 * interrupts disabled and with the spinlock hold.
 */
int __devinit ace_load_firmware(struct net_device *dev)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;

	if (!(readl(&regs->CpuCtrl) & CPU_HALTED)) {
		printk(KERN_ERR "%s: trying to download firmware while the "
		       "CPU is running!\n", ap->name);
		return -EFAULT;
	}

	/*
	 * Do not try to clear more than 512KB or we end up seeing
	 * funny things on NICs with only 512KB SRAM
	 */
	ace_clear(regs, 0x2000, 0x80000-0x2000);
	if (ACE_IS_TIGON_I(ap)) {
		ace_copy(regs, tigonFwText, tigonFwTextAddr, tigonFwTextLen);
		ace_copy(regs, tigonFwData, tigonFwDataAddr, tigonFwDataLen);
		ace_copy(regs, tigonFwRodata, tigonFwRodataAddr,
			 tigonFwRodataLen);
		ace_clear(regs, tigonFwBssAddr, tigonFwBssLen);
		ace_clear(regs, tigonFwSbssAddr, tigonFwSbssLen);
	}else if (ap->version == 2) {
		ace_clear(regs, tigon2FwBssAddr, tigon2FwBssLen);
		ace_clear(regs, tigon2FwSbssAddr, tigon2FwSbssLen);
		ace_copy(regs, tigon2FwText, tigon2FwTextAddr,tigon2FwTextLen);
		ace_copy(regs, tigon2FwRodata, tigon2FwRodataAddr,
			 tigon2FwRodataLen);
		ace_copy(regs, tigon2FwData, tigon2FwDataAddr,tigon2FwDataLen);
	}

	return 0;
}


/*
 * The eeprom on the AceNIC is an Atmel i2c EEPROM.
 *
 * Accessing the EEPROM is `interesting' to say the least - don't read
 * this code right after dinner.
 *
 * This is all about black magic and bit-banging the device .... I
 * wonder in what hospital they have put the guy who designed the i2c
 * specs.
 *
 * Oh yes, this is only the beginning!
 *
 * Thanks to Stevarino Webinski for helping tracking down the bugs in the
 * code i2c readout code by beta testing all my hacks.
 */
static void __devinit eeprom_start(struct ace_regs __iomem *regs)
{
	u32 local;

	readl(&regs->LocalCtrl);
	udelay(ACE_SHORT_DELAY);
	local = readl(&regs->LocalCtrl);
	local |= EEPROM_DATA_OUT | EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local &= ~EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local &= ~EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
}


static void __devinit eeprom_prep(struct ace_regs __iomem *regs, u8 magic)
{
	short i;
	u32 local;

	udelay(ACE_SHORT_DELAY);
	local = readl(&regs->LocalCtrl);
	local &= ~EEPROM_DATA_OUT;
	local |= EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();

	for (i = 0; i < 8; i++, magic <<= 1) {
		udelay(ACE_SHORT_DELAY);
		if (magic & 0x80)
			local |= EEPROM_DATA_OUT;
		else
			local &= ~EEPROM_DATA_OUT;
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		mb();

		udelay(ACE_SHORT_DELAY);
		local |= EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		mb();
		udelay(ACE_SHORT_DELAY);
		local &= ~(EEPROM_CLK_OUT | EEPROM_DATA_OUT);
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		mb();
	}
}


static int __devinit eeprom_check_ack(struct ace_regs __iomem *regs)
{
	int state;
	u32 local;

	local = readl(&regs->LocalCtrl);
	local &= ~EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_LONG_DELAY);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	/* sample data in middle of high clk */
	state = (readl(&regs->LocalCtrl) & EEPROM_DATA_IN) != 0;
	udelay(ACE_SHORT_DELAY);
	mb();
	writel(readl(&regs->LocalCtrl) & ~EEPROM_CLK_OUT, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();

	return state;
}


static void __devinit eeprom_stop(struct ace_regs __iomem *regs)
{
	u32 local;

	udelay(ACE_SHORT_DELAY);
	local = readl(&regs->LocalCtrl);
	local |= EEPROM_WRITE_ENABLE;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local &= ~EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local |= EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	local |= EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_LONG_DELAY);
	local &= ~EEPROM_CLK_OUT;
	writel(local, &regs->LocalCtrl);
	mb();
}


/*
 * Read a whole byte from the EEPROM.
 */
static int __devinit read_eeprom_byte(struct net_device *dev,
				   unsigned long offset)
{
	struct ace_private *ap = netdev_priv(dev);
	struct ace_regs __iomem *regs = ap->regs;
	unsigned long flags;
	u32 local;
	int result = 0;
	short i;

	if (!dev) {
		printk(KERN_ERR "No device!\n");
		result = -ENODEV;
		goto out;
	}

	/*
	 * Don't take interrupts on this CPU will bit banging
	 * the %#%#@$ I2C device
	 */
	local_irq_save(flags);

	eeprom_start(regs);

	eeprom_prep(regs, EEPROM_WRITE_SELECT);
	if (eeprom_check_ack(regs)) {
		local_irq_restore(flags);
		printk(KERN_ERR "%s: Unable to sync eeprom\n", ap->name);
		result = -EIO;
		goto eeprom_read_error;
	}

	eeprom_prep(regs, (offset >> 8) & 0xff);
	if (eeprom_check_ack(regs)) {
		local_irq_restore(flags);
		printk(KERN_ERR "%s: Unable to set address byte 0\n",
		       ap->name);
		result = -EIO;
		goto eeprom_read_error;
	}

	eeprom_prep(regs, offset & 0xff);
	if (eeprom_check_ack(regs)) {
		local_irq_restore(flags);
		printk(KERN_ERR "%s: Unable to set address byte 1\n",
		       ap->name);
		result = -EIO;
		goto eeprom_read_error;
	}

	eeprom_start(regs);
	eeprom_prep(regs, EEPROM_READ_SELECT);
	if (eeprom_check_ack(regs)) {
		local_irq_restore(flags);
		printk(KERN_ERR "%s: Unable to set READ_SELECT\n",
		       ap->name);
		result = -EIO;
		goto eeprom_read_error;
	}

	for (i = 0; i < 8; i++) {
		local = readl(&regs->LocalCtrl);
		local &= ~EEPROM_WRITE_ENABLE;
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		udelay(ACE_LONG_DELAY);
		mb();
		local |= EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		mb();
		udelay(ACE_SHORT_DELAY);
		/* sample data mid high clk */
		result = (result << 1) |
			((readl(&regs->LocalCtrl) & EEPROM_DATA_IN) != 0);
		udelay(ACE_SHORT_DELAY);
		mb();
		local = readl(&regs->LocalCtrl);
		local &= ~EEPROM_CLK_OUT;
		writel(local, &regs->LocalCtrl);
		readl(&regs->LocalCtrl);
		udelay(ACE_SHORT_DELAY);
		mb();
		if (i == 7) {
			local |= EEPROM_WRITE_ENABLE;
			writel(local, &regs->LocalCtrl);
			readl(&regs->LocalCtrl);
			mb();
			udelay(ACE_SHORT_DELAY);
		}
	}

	local |= EEPROM_DATA_OUT;
	writel(local, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	writel(readl(&regs->LocalCtrl) | EEPROM_CLK_OUT, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	udelay(ACE_LONG_DELAY);
	writel(readl(&regs->LocalCtrl) & ~EEPROM_CLK_OUT, &regs->LocalCtrl);
	readl(&regs->LocalCtrl);
	mb();
	udelay(ACE_SHORT_DELAY);
	eeprom_stop(regs);

	local_irq_restore(flags);
 out:
	return result;

 eeprom_read_error:
	printk(KERN_ERR "%s: Unable to read eeprom byte 0x%02lx\n",
	       ap->name, offset);
	goto out;
}


/*
 * Local variables:
 * compile-command: "gcc -D__SMP__ -D__KERNEL__ -DMODULE -I../../include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -fno-strength-reduce -DMODVERSIONS -include ../../include/linux/modversions.h   -c -o acenic.o acenic.c"
 * End:
 */
