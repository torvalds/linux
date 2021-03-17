/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  Copyright (C) 2004 IDT Inc.
 *  Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 */
#ifndef __ASM_RC32434_RB_H
#define __ASM_RC32434_RB_H

#include <linux/genhd.h>

#define REGBASE		0x18000000
#define IDT434_REG_BASE ((volatile void *) KSEG1ADDR(REGBASE))
#define UART0BASE	0x58000
#define RST		(1 << 15)
#define DEV0BASE	0x010000
#define DEV0MASK	0x010004
#define DEV0C		0x010008
#define DEV0T		0x01000C
#define DEV1BASE	0x010010
#define DEV1MASK	0x010014
#define DEV1C		0x010018
#define DEV1TC		0x01001C
#define DEV2BASE	0x010020
#define DEV2MASK	0x010024
#define DEV2C		0x010028
#define DEV2TC		0x01002C
#define DEV3BASE	0x010030
#define DEV3MASK	0x010034
#define DEV3C		0x010038
#define DEV3TC		0x01003C
#define BTCS		0x010040
#define BTCOMPARE	0x010044
#define GPIOBASE	0x050000
/* Offsets relative to GPIOBASE */
#define GPIOFUNC	0x00
#define GPIOCFG		0x04
#define GPIOD		0x08
#define GPIOILEVEL	0x0C
#define GPIOISTAT	0x10
#define GPIONMIEN	0x14
#define IMASK6		0x38
#define LO_WPX		(1 << 0)
#define LO_ALE		(1 << 1)
#define LO_CLE		(1 << 2)
#define LO_CEX		(1 << 3)
#define LO_FOFF		(1 << 5)
#define LO_SPICS	(1 << 6)
#define LO_ULED		(1 << 7)

#define BIT_TO_MASK(x)	(1 << x)

struct dev_reg {
	u32	base;
	u32	mask;
	u32	ctl;
	u32	timing;
};

struct korina_device {
	char *name;
	unsigned char mac[6];
	struct net_device *dev;
};

struct mpmc_device {
	unsigned char	state;
	spinlock_t	lock;
	void __iomem	*base;
};

extern void set_latch_u5(unsigned char or_mask, unsigned char nand_mask);
extern unsigned char get_latch_u5(void);

#endif	/* __ASM_RC32434_RB_H */
