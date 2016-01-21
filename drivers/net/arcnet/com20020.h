/*
 * Linux ARCnet driver - COM20020 chipset support - function declarations
 *
 * Written 1997 by David Woodhouse.
 * Written 1994-1999 by Avery Pennarun.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#ifndef __COM20020_H
#define __COM20020_H
#include <linux/leds.h>

int com20020_check(struct net_device *dev);
int com20020_found(struct net_device *dev, int shared);
extern const struct net_device_ops com20020_netdev_ops;

/* The number of low I/O ports used by the card. */
#define ARCNET_TOTAL_SIZE 8

#define PLX_PCI_MAX_CARDS 2

struct ledoffsets {
	int green;
	int red;
};

struct com20020_pci_channel_map {
	u32 bar;
	u32 offset;
	u32 size;               /* 0x00 - auto, e.g. length of entire bar */
};

struct com20020_pci_card_info {
	const char *name;
	int devcount;

	struct com20020_pci_channel_map chan_map_tbl[PLX_PCI_MAX_CARDS];
	struct com20020_pci_channel_map misc_map;

	struct ledoffsets leds[PLX_PCI_MAX_CARDS];
	int rotary;

	unsigned int flags;
};

struct com20020_priv {
	struct com20020_pci_card_info *ci;
	struct list_head list_dev;
	resource_size_t misc;
};

struct com20020_dev {
	struct list_head list;
	struct net_device *dev;

	struct led_classdev tx_led;
	struct led_classdev recon_led;

	struct com20020_priv *pci_priv;
	int index;
};

#define COM20020_REG_W_INTMASK	0	/* writable */
#define COM20020_REG_R_STATUS	0	/* readable */
#define COM20020_REG_W_COMMAND	1	/* standard arcnet commands */
#define COM20020_REG_R_DIAGSTAT	1	/* diagnostic status */
#define COM20020_REG_W_ADDR_HI	2	/* control for IO-mapped memory */
#define COM20020_REG_W_ADDR_LO	3
#define COM20020_REG_RW_MEMDATA	4	/* data port for IO-mapped memory */
#define COM20020_REG_W_SUBADR	5	/* the extended port _XREG refers to */
#define COM20020_REG_W_CONFIG	6	/* configuration */
#define COM20020_REG_W_XREG	7	/* extra
					 * (indexed by _CONFIG or _SUBADDR)
					 */

/* in the ADDR_HI register */
#define RDDATAflag	0x80	/* next access is a read (not a write) */

/* in the DIAGSTAT register */
#define NEWNXTIDflag	0x02	/* ID to which token is passed has changed */

/* in the CONFIG register */
#define RESETcfg	0x80	/* put card in reset state */
#define TXENcfg		0x20	/* enable TX */
#define XTOcfg(x)	((x) << 3)	/* extended timeout */

/* in SETUP register */
#define PROMISCset	0x10	/* enable RCV_ALL */
#define P1MODE		0x80    /* enable P1-MODE for Backplane */
#define SLOWARB		0x01    /* enable Slow Arbitration for >=5Mbps */

/* COM2002x */
#define SUB_TENTATIVE	0	/* tentative node ID */
#define SUB_NODE	1	/* node ID */
#define SUB_SETUP1	2	/* various options */
#define SUB_TEST	3	/* test/diag register */

/* COM20022 only */
#define SUB_SETUP2	4	/* sundry options */
#define SUB_BUSCTL	5	/* bus control options */
#define SUB_DMACOUNT	6	/* DMA count options */

static inline void com20020_set_subaddress(struct arcnet_local *lp,
					   int ioaddr, int val)
{
	if (val < 4) {
		lp->config = (lp->config & ~0x03) | val;
		arcnet_outb(lp->config, ioaddr, COM20020_REG_W_CONFIG);
	} else {
		arcnet_outb(val, ioaddr, COM20020_REG_W_SUBADR);
	}
}

#endif /* __COM20020_H */
