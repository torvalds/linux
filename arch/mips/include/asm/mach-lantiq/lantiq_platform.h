/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#ifndef _LANTIQ_PLATFORM_H__
#define _LANTIQ_PLATFORM_H__

#include <linux/mtd/partitions.h>
#include <linux/socket.h>

/* struct used to pass info to the pci core */
enum {
	PCI_CLOCK_INT = 0,
	PCI_CLOCK_EXT
};

#define PCI_EXIN0	0x0001
#define PCI_EXIN1	0x0002
#define PCI_EXIN2	0x0004
#define PCI_EXIN3	0x0008
#define PCI_EXIN4	0x0010
#define PCI_EXIN5	0x0020
#define PCI_EXIN_MAX	6

#define PCI_GNT1	0x0040
#define PCI_GNT2	0x0080
#define PCI_GNT3	0x0100
#define PCI_GNT4	0x0200

#define PCI_REQ1	0x0400
#define PCI_REQ2	0x0800
#define PCI_REQ3	0x1000
#define PCI_REQ4	0x2000
#define PCI_REQ_SHIFT	10
#define PCI_REQ_MASK	0xf

struct ltq_pci_data {
	int clock;
	int gpio;
	int irq[16];
};

/* struct used to pass info to network drivers */
struct ltq_eth_data {
	struct sockaddr mac;
	int mii_mode;
};

#endif
