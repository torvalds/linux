/*

  he.c

  ForeRunnerHE ATM Adapter driver for ATM on Linux
  Copyright (C) 1999-2001  Naval Research Laboratory

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*

  he.c

  ForeRunnerHE ATM Adapter driver for ATM on Linux
  Copyright (C) 1999-2001  Naval Research Laboratory

  Permission to use, copy, modify and distribute this software and its
  documentation is hereby granted, provided that both the copyright
  notice and this permission notice appear in all copies of the software,
  derivative works or modified versions, and any portions thereof, and
  that both notices appear in supporting documentation.

  NRL ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION AND
  DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
  RESULTING FROM THE USE OF THIS SOFTWARE.

  This driver was written using the "Programmer's Reference Manual for
  ForeRunnerHE(tm)", MANU0361-01 - Rev. A, 08/21/98.

  AUTHORS:
	chas williams <chas@cmf.nrl.navy.mil>
	eric kinzie <ekinzie@cmf.nrl.navy.mil>

  NOTES:
	4096 supported 'connections'
	group 0 is used for all traffic
	interrupt queue 0 is used for all interrupts
	aal0 support (based on work from ulrich.u.muller@nokia.com)

 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include <linux/atmdev.h>
#include <linux/atm.h>
#include <linux/sonet.h>

#undef USE_SCATTERGATHER
#undef USE_CHECKSUM_HW			/* still confused about this */
/* #undef HE_DEBUG */

#include "he.h"
#include "suni.h"
#include <linux/atm_he.h>

#define hprintk(fmt,args...)	printk(KERN_ERR DEV_LABEL "%d: " fmt, he_dev->number , ##args)

#ifdef HE_DEBUG
#define HPRINTK(fmt,args...)	printk(KERN_DEBUG DEV_LABEL "%d: " fmt, he_dev->number , ##args)
#else /* !HE_DEBUG */
#define HPRINTK(fmt,args...)	do { } while (0)
#endif /* HE_DEBUG */

/* declarations */

static int he_open(struct atm_vcc *vcc);
static void he_close(struct atm_vcc *vcc);
static int he_send(struct atm_vcc *vcc, struct sk_buff *skb);
static int he_ioctl(struct atm_dev *dev, unsigned int cmd, void __user *arg);
static irqreturn_t he_irq_handler(int irq, void *dev_id);
static void he_tasklet(unsigned long data);
static int he_proc_read(struct atm_dev *dev,loff_t *pos,char *page);
static int he_start(struct atm_dev *dev);
static void he_stop(struct he_dev *dev);
static void he_phy_put(struct atm_dev *, unsigned char, unsigned long);
static unsigned char he_phy_get(struct atm_dev *, unsigned long);

static u8 read_prom_byte(struct he_dev *he_dev, int addr);

/* globals */

static struct he_dev *he_devs;
static bool disable64;
static short nvpibits = -1;
static short nvcibits = -1;
static short rx_skb_reserve = 16;
static bool irq_coalesce = 1;
static bool sdh = 0;

/* Read from EEPROM = 0000 0011b */
static unsigned int readtab[] = {
	CS_HIGH | CLK_HIGH,
	CS_LOW | CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW,
	CLK_HIGH,               /* 0 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH,     /* 1 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH      /* 1 */
};     
 
/* Clock to read from/write to the EEPROM */
static unsigned int clocktab[] = {
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW
};     

static struct atmdev_ops he_ops =
{
	.open =		he_open,
	.close =	he_close,	
	.ioctl =	he_ioctl,	
	.send =		he_send,
	.phy_put =	he_phy_put,
	.phy_get =	he_phy_get,
	.proc_read =	he_proc_read,
	.owner =	THIS_MODULE
};

#define he_writel(dev, val, reg)	do { writel(val, (dev)->membase + (reg)); wmb(); } while (0)
#define he_readl(dev, reg)		readl((dev)->membase + (reg))

/* section 2.12 connection memory access */

static __inline__ void
he_writel_internal(struct he_dev *he_dev, unsigned val, unsigned addr,
								unsigned flags)
{
	he_writel(he_dev, val, CON_DAT);
	(void) he_readl(he_dev, CON_DAT);		/* flush posted writes */
	he_writel(he_dev, flags | CON_CTL_WRITE | CON_CTL_ADDR(addr), CON_CTL);
	while (he_readl(he_dev, CON_CTL) & CON_CTL_BUSY);
}

#define he_writel_rcm(dev, val, reg) 				\
			he_writel_internal(dev, val, reg, CON_CTL_RCM)

#define he_writel_tcm(dev, val, reg) 				\
			he_writel_internal(dev, val, reg, CON_CTL_TCM)

#define he_writel_mbox(dev, val, reg) 				\
			he_writel_internal(dev, val, reg, CON_CTL_MBOX)

static unsigned
he_readl_internal(struct he_dev *he_dev, unsigned addr, unsigned flags)
{
	he_writel(he_dev, flags | CON_CTL_READ | CON_CTL_ADDR(addr), CON_CTL);
	while (he_readl(he_dev, CON_CTL) & CON_CTL_BUSY);
	return he_readl(he_dev, CON_DAT);
}

#define he_readl_rcm(dev, reg) \
			he_readl_internal(dev, reg, CON_CTL_RCM)

#define he_readl_tcm(dev, reg) \
			he_readl_internal(dev, reg, CON_CTL_TCM)

#define he_readl_mbox(dev, reg) \
			he_readl_internal(dev, reg, CON_CTL_MBOX)


/* figure 2.2 connection id */

#define he_mkcid(dev, vpi, vci)		(((vpi << (dev)->vcibits) | vci) & 0x1fff)

/* 2.5.1 per connection transmit state registers */

#define he_writel_tsr0(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 0)
#define he_readl_tsr0(dev, cid) \
		he_readl_tcm(dev, CONFIG_TSRA | (cid << 3) | 0)

#define he_writel_tsr1(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 1)

#define he_writel_tsr2(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 2)

#define he_writel_tsr3(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 3)

#define he_writel_tsr4(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 4)

	/* from page 2-20
	 *
	 * NOTE While the transmit connection is active, bits 23 through 0
	 *      of this register must not be written by the host.  Byte
	 *      enables should be used during normal operation when writing
	 *      the most significant byte.
	 */

#define he_writel_tsr4_upper(dev, val, cid) \
		he_writel_internal(dev, val, CONFIG_TSRA | (cid << 3) | 4, \
							CON_CTL_TCM \
							| CON_BYTE_DISABLE_2 \
							| CON_BYTE_DISABLE_1 \
							| CON_BYTE_DISABLE_0)

#define he_readl_tsr4(dev, cid) \
		he_readl_tcm(dev, CONFIG_TSRA | (cid << 3) | 4)

#define he_writel_tsr5(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 5)

#define he_writel_tsr6(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 6)

#define he_writel_tsr7(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRA | (cid << 3) | 7)


#define he_writel_tsr8(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRB | (cid << 2) | 0)

#define he_writel_tsr9(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRB | (cid << 2) | 1)

#define he_writel_tsr10(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRB | (cid << 2) | 2)

#define he_writel_tsr11(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRB | (cid << 2) | 3)


#define he_writel_tsr12(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRC | (cid << 1) | 0)

#define he_writel_tsr13(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRC | (cid << 1) | 1)


#define he_writel_tsr14(dev, val, cid) \
		he_writel_tcm(dev, val, CONFIG_TSRD | cid)

#define he_writel_tsr14_upper(dev, val, cid) \
		he_writel_internal(dev, val, CONFIG_TSRD | cid, \
							CON_CTL_TCM \
							| CON_BYTE_DISABLE_2 \
							| CON_BYTE_DISABLE_1 \
							| CON_BYTE_DISABLE_0)

/* 2.7.1 per connection receive state registers */

#define he_writel_rsr0(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 0)
#define he_readl_rsr0(dev, cid) \
		he_readl_rcm(dev, 0x00000 | (cid << 3) | 0)

#define he_writel_rsr1(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 1)

#define he_writel_rsr2(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 2)

#define he_writel_rsr3(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 3)

#define he_writel_rsr4(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 4)

#define he_writel_rsr5(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 5)

#define he_writel_rsr6(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 6)

#define he_writel_rsr7(dev, val, cid) \
		he_writel_rcm(dev, val, 0x00000 | (cid << 3) | 7)

static __inline__ struct atm_vcc*
__find_vcc(struct he_dev *he_dev, unsigned cid)
{
	struct hlist_head *head;
	struct atm_vcc *vcc;
	struct sock *s;
	short vpi;
	int vci;

	vpi = cid >> he_dev->vcibits;
	vci = cid & ((1 << he_dev->vcibits) - 1);
	head = &vcc_hash[vci & (VCC_HTABLE_SIZE -1)];

	sk_for_each(s, head) {
		vcc = atm_sk(s);
		if (vcc->dev == he_dev->atm_dev &&
		    vcc->vci == vci && vcc->vpi == vpi &&
		    vcc->qos.rxtp.traffic_class != ATM_NONE) {
				return vcc;
		}
	}
	return NULL;
}

static int he_init_one(struct pci_dev *pci_dev,
		       const struct pci_device_id *pci_ent)
{
	struct atm_dev *atm_dev = NULL;
	struct he_dev *he_dev = NULL;
	int err = 0;

	printk(KERN_INFO "ATM he driver\n");

	if (pci_enable_device(pci_dev))
		return -EIO;
	if (dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(32)) != 0) {
		printk(KERN_WARNING "he: no suitable dma available\n");
		err = -EIO;
		goto init_one_failure;
	}

	atm_dev = atm_dev_register(DEV_LABEL, &pci_dev->dev, &he_ops, -1, NULL);
	if (!atm_dev) {
		err = -ENODEV;
		goto init_one_failure;
	}
	pci_set_drvdata(pci_dev, atm_dev);

	he_dev = kzalloc(sizeof(struct he_dev),
							GFP_KERNEL);
	if (!he_dev) {
		err = -ENOMEM;
		goto init_one_failure;
	}
	he_dev->pci_dev = pci_dev;
	he_dev->atm_dev = atm_dev;
	he_dev->atm_dev->dev_data = he_dev;
	atm_dev->dev_data = he_dev;
	he_dev->number = atm_dev->number;
	tasklet_init(&he_dev->tasklet, he_tasklet, (unsigned long) he_dev);
	spin_lock_init(&he_dev->global_lock);

	if (he_start(atm_dev)) {
		he_stop(he_dev);
		err = -ENODEV;
		goto init_one_failure;
	}
	he_dev->next = NULL;
	if (he_devs)
		he_dev->next = he_devs;
	he_devs = he_dev;
	return 0;

init_one_failure:
	if (atm_dev)
		atm_dev_deregister(atm_dev);
	kfree(he_dev);
	pci_disable_device(pci_dev);
	return err;
}

static void he_remove_one(struct pci_dev *pci_dev)
{
	struct atm_dev *atm_dev;
	struct he_dev *he_dev;

	atm_dev = pci_get_drvdata(pci_dev);
	he_dev = HE_DEV(atm_dev);

	/* need to remove from he_devs */

	he_stop(he_dev);
	atm_dev_deregister(atm_dev);
	kfree(he_dev);

	pci_disable_device(pci_dev);
}


static unsigned
rate_to_atmf(unsigned rate)		/* cps to atm forum format */
{
#define NONZERO (1 << 14)

	unsigned exp = 0;

	if (rate == 0)
		return 0;

	rate <<= 9;
	while (rate > 0x3ff) {
		++exp;
		rate >>= 1;
	}

	return (NONZERO | (exp << 9) | (rate & 0x1ff));
}

static void he_init_rx_lbfp0(struct he_dev *he_dev)
{
	unsigned i, lbm_offset, lbufd_index, lbuf_addr, lbuf_count;
	unsigned lbufs_per_row = he_dev->cells_per_row / he_dev->cells_per_lbuf;
	unsigned lbuf_bufsize = he_dev->cells_per_lbuf * ATM_CELL_PAYLOAD;
	unsigned row_offset = he_dev->r0_startrow * he_dev->bytes_per_row;
	
	lbufd_index = 0;
	lbm_offset = he_readl(he_dev, RCMLBM_BA);

	he_writel(he_dev, lbufd_index, RLBF0_H);

	for (i = 0, lbuf_count = 0; i < he_dev->r0_numbuffs; ++i) {
		lbufd_index += 2;
		lbuf_addr = (row_offset + (lbuf_count * lbuf_bufsize)) / 32;

		he_writel_rcm(he_dev, lbuf_addr, lbm_offset);
		he_writel_rcm(he_dev, lbufd_index, lbm_offset + 1);

		if (++lbuf_count == lbufs_per_row) {
			lbuf_count = 0;
			row_offset += he_dev->bytes_per_row;
		}
		lbm_offset += 4;
	}
		
	he_writel(he_dev, lbufd_index - 2, RLBF0_T);
	he_writel(he_dev, he_dev->r0_numbuffs, RLBF0_C);
}

static void he_init_rx_lbfp1(struct he_dev *he_dev)
{
	unsigned i, lbm_offset, lbufd_index, lbuf_addr, lbuf_count;
	unsigned lbufs_per_row = he_dev->cells_per_row / he_dev->cells_per_lbuf;
	unsigned lbuf_bufsize = he_dev->cells_per_lbuf * ATM_CELL_PAYLOAD;
	unsigned row_offset = he_dev->r1_startrow * he_dev->bytes_per_row;
	
	lbufd_index = 1;
	lbm_offset = he_readl(he_dev, RCMLBM_BA) + (2 * lbufd_index);

	he_writel(he_dev, lbufd_index, RLBF1_H);

	for (i = 0, lbuf_count = 0; i < he_dev->r1_numbuffs; ++i) {
		lbufd_index += 2;
		lbuf_addr = (row_offset + (lbuf_count * lbuf_bufsize)) / 32;

		he_writel_rcm(he_dev, lbuf_addr, lbm_offset);
		he_writel_rcm(he_dev, lbufd_index, lbm_offset + 1);

		if (++lbuf_count == lbufs_per_row) {
			lbuf_count = 0;
			row_offset += he_dev->bytes_per_row;
		}
		lbm_offset += 4;
	}
		
	he_writel(he_dev, lbufd_index - 2, RLBF1_T);
	he_writel(he_dev, he_dev->r1_numbuffs, RLBF1_C);
}

static void he_init_tx_lbfp(struct he_dev *he_dev)
{
	unsigned i, lbm_offset, lbufd_index, lbuf_addr, lbuf_count;
	unsigned lbufs_per_row = he_dev->cells_per_row / he_dev->cells_per_lbuf;
	unsigned lbuf_bufsize = he_dev->cells_per_lbuf * ATM_CELL_PAYLOAD;
	unsigned row_offset = he_dev->tx_startrow * he_dev->bytes_per_row;
	
	lbufd_index = he_dev->r0_numbuffs + he_dev->r1_numbuffs;
	lbm_offset = he_readl(he_dev, RCMLBM_BA) + (2 * lbufd_index);

	he_writel(he_dev, lbufd_index, TLBF_H);

	for (i = 0, lbuf_count = 0; i < he_dev->tx_numbuffs; ++i) {
		lbufd_index += 1;
		lbuf_addr = (row_offset + (lbuf_count * lbuf_bufsize)) / 32;

		he_writel_rcm(he_dev, lbuf_addr, lbm_offset);
		he_writel_rcm(he_dev, lbufd_index, lbm_offset + 1);

		if (++lbuf_count == lbufs_per_row) {
			lbuf_count = 0;
			row_offset += he_dev->bytes_per_row;
		}
		lbm_offset += 2;
	}
		
	he_writel(he_dev, lbufd_index - 1, TLBF_T);
}

static int he_init_tpdrq(struct he_dev *he_dev)
{
	he_dev->tpdrq_base = dma_zalloc_coherent(&he_dev->pci_dev->dev,
						 CONFIG_TPDRQ_SIZE * sizeof(struct he_tpdrq),
						 &he_dev->tpdrq_phys, GFP_KERNEL);
	if (he_dev->tpdrq_base == NULL) {
		hprintk("failed to alloc tpdrq\n");
		return -ENOMEM;
	}

	he_dev->tpdrq_tail = he_dev->tpdrq_base;
	he_dev->tpdrq_head = he_dev->tpdrq_base;

	he_writel(he_dev, he_dev->tpdrq_phys, TPDRQ_B_H);
	he_writel(he_dev, 0, TPDRQ_T);	
	he_writel(he_dev, CONFIG_TPDRQ_SIZE - 1, TPDRQ_S);

	return 0;
}

static void he_init_cs_block(struct he_dev *he_dev)
{
	unsigned clock, rate, delta;
	int reg;

	/* 5.1.7 cs block initialization */

	for (reg = 0; reg < 0x20; ++reg)
		he_writel_mbox(he_dev, 0x0, CS_STTIM0 + reg);

	/* rate grid timer reload values */

	clock = he_is622(he_dev) ? 66667000 : 50000000;
	rate = he_dev->atm_dev->link_rate;
	delta = rate / 16 / 2;

	for (reg = 0; reg < 0x10; ++reg) {
		/* 2.4 internal transmit function
		 *
	 	 * we initialize the first row in the rate grid.
		 * values are period (in clock cycles) of timer
		 */
		unsigned period = clock / rate;

		he_writel_mbox(he_dev, period, CS_TGRLD0 + reg);
		rate -= delta;
	}

	if (he_is622(he_dev)) {
		/* table 5.2 (4 cells per lbuf) */
		he_writel_mbox(he_dev, 0x000800fa, CS_ERTHR0);
		he_writel_mbox(he_dev, 0x000c33cb, CS_ERTHR1);
		he_writel_mbox(he_dev, 0x0010101b, CS_ERTHR2);
		he_writel_mbox(he_dev, 0x00181dac, CS_ERTHR3);
		he_writel_mbox(he_dev, 0x00280600, CS_ERTHR4);

		/* table 5.3, 5.4, 5.5, 5.6, 5.7 */
		he_writel_mbox(he_dev, 0x023de8b3, CS_ERCTL0);
		he_writel_mbox(he_dev, 0x1801, CS_ERCTL1);
		he_writel_mbox(he_dev, 0x68b3, CS_ERCTL2);
		he_writel_mbox(he_dev, 0x1280, CS_ERSTAT0);
		he_writel_mbox(he_dev, 0x68b3, CS_ERSTAT1);
		he_writel_mbox(he_dev, 0x14585, CS_RTFWR);

		he_writel_mbox(he_dev, 0x4680, CS_RTATR);

		/* table 5.8 */
		he_writel_mbox(he_dev, 0x00159ece, CS_TFBSET);
		he_writel_mbox(he_dev, 0x68b3, CS_WCRMAX);
		he_writel_mbox(he_dev, 0x5eb3, CS_WCRMIN);
		he_writel_mbox(he_dev, 0xe8b3, CS_WCRINC);
		he_writel_mbox(he_dev, 0xdeb3, CS_WCRDEC);
		he_writel_mbox(he_dev, 0x68b3, CS_WCRCEIL);

		/* table 5.9 */
		he_writel_mbox(he_dev, 0x5, CS_OTPPER);
		he_writel_mbox(he_dev, 0x14, CS_OTWPER);
	} else {
		/* table 5.1 (4 cells per lbuf) */
		he_writel_mbox(he_dev, 0x000400ea, CS_ERTHR0);
		he_writel_mbox(he_dev, 0x00063388, CS_ERTHR1);
		he_writel_mbox(he_dev, 0x00081018, CS_ERTHR2);
		he_writel_mbox(he_dev, 0x000c1dac, CS_ERTHR3);
		he_writel_mbox(he_dev, 0x0014051a, CS_ERTHR4);

		/* table 5.3, 5.4, 5.5, 5.6, 5.7 */
		he_writel_mbox(he_dev, 0x0235e4b1, CS_ERCTL0);
		he_writel_mbox(he_dev, 0x4701, CS_ERCTL1);
		he_writel_mbox(he_dev, 0x64b1, CS_ERCTL2);
		he_writel_mbox(he_dev, 0x1280, CS_ERSTAT0);
		he_writel_mbox(he_dev, 0x64b1, CS_ERSTAT1);
		he_writel_mbox(he_dev, 0xf424, CS_RTFWR);

		he_writel_mbox(he_dev, 0x4680, CS_RTATR);

		/* table 5.8 */
		he_writel_mbox(he_dev, 0x000563b7, CS_TFBSET);
		he_writel_mbox(he_dev, 0x64b1, CS_WCRMAX);
		he_writel_mbox(he_dev, 0x5ab1, CS_WCRMIN);
		he_writel_mbox(he_dev, 0xe4b1, CS_WCRINC);
		he_writel_mbox(he_dev, 0xdab1, CS_WCRDEC);
		he_writel_mbox(he_dev, 0x64b1, CS_WCRCEIL);

		/* table 5.9 */
		he_writel_mbox(he_dev, 0x6, CS_OTPPER);
		he_writel_mbox(he_dev, 0x1e, CS_OTWPER);
	}

	he_writel_mbox(he_dev, 0x8, CS_OTTLIM);

	for (reg = 0; reg < 0x8; ++reg)
		he_writel_mbox(he_dev, 0x0, CS_HGRRT0 + reg);

}

static int he_init_cs_block_rcm(struct he_dev *he_dev)
{
	unsigned (*rategrid)[16][16];
	unsigned rate, delta;
	int i, j, reg;

	unsigned rate_atmf, exp, man;
	unsigned long long rate_cps;
	int mult, buf, buf_limit = 4;

	rategrid = kmalloc( sizeof(unsigned) * 16 * 16, GFP_KERNEL);
	if (!rategrid)
		return -ENOMEM;

	/* initialize rate grid group table */

	for (reg = 0x0; reg < 0xff; ++reg)
		he_writel_rcm(he_dev, 0x0, CONFIG_RCMABR + reg);

	/* initialize rate controller groups */

	for (reg = 0x100; reg < 0x1ff; ++reg)
		he_writel_rcm(he_dev, 0x0, CONFIG_RCMABR + reg);
	
	/* initialize tNrm lookup table */

	/* the manual makes reference to a routine in a sample driver
	   for proper configuration; fortunately, we only need this
	   in order to support abr connection */
	
	/* initialize rate to group table */

	rate = he_dev->atm_dev->link_rate;
	delta = rate / 32;

	/*
	 * 2.4 transmit internal functions
	 * 
	 * we construct a copy of the rate grid used by the scheduler
	 * in order to construct the rate to group table below
	 */

	for (j = 0; j < 16; j++) {
		(*rategrid)[0][j] = rate;
		rate -= delta;
	}

	for (i = 1; i < 16; i++)
		for (j = 0; j < 16; j++)
			if (i > 14)
				(*rategrid)[i][j] = (*rategrid)[i - 1][j] / 4;
			else
				(*rategrid)[i][j] = (*rategrid)[i - 1][j] / 2;

	/*
	 * 2.4 transmit internal function
	 *
	 * this table maps the upper 5 bits of exponent and mantissa
	 * of the atm forum representation of the rate into an index
	 * on rate grid  
	 */

	rate_atmf = 0;
	while (rate_atmf < 0x400) {
		man = (rate_atmf & 0x1f) << 4;
		exp = rate_atmf >> 5;

		/* 
			instead of '/ 512', use '>> 9' to prevent a call
			to divdu3 on x86 platforms
		*/
		rate_cps = (unsigned long long) (1 << exp) * (man + 512) >> 9;

		if (rate_cps < 10)
			rate_cps = 10;	/* 2.2.1 minimum payload rate is 10 cps */

		for (i = 255; i > 0; i--)
			if ((*rategrid)[i/16][i%16] >= rate_cps)
				break;	 /* pick nearest rate instead? */

		/*
		 * each table entry is 16 bits: (rate grid index (8 bits)
		 * and a buffer limit (8 bits)
		 * there are two table entries in each 32-bit register
		 */

#ifdef notdef
		buf = rate_cps * he_dev->tx_numbuffs /
				(he_dev->atm_dev->link_rate * 2);
#else
		/* this is pretty, but avoids _divdu3 and is mostly correct */
		mult = he_dev->atm_dev->link_rate / ATM_OC3_PCR;
		if (rate_cps > (272 * mult))
			buf = 4;
		else if (rate_cps > (204 * mult))
			buf = 3;
		else if (rate_cps > (136 * mult))
			buf = 2;
		else if (rate_cps > (68 * mult))
			buf = 1;
		else
			buf = 0;
#endif
		if (buf > buf_limit)
			buf = buf_limit;
		reg = (reg << 16) | ((i << 8) | buf);

#define RTGTBL_OFFSET 0x400
	  
		if (rate_atmf & 0x1)
			he_writel_rcm(he_dev, reg,
				CONFIG_RCMABR + RTGTBL_OFFSET + (rate_atmf >> 1));

		++rate_atmf;
	}

	kfree(rategrid);
	return 0;
}

static int he_init_group(struct he_dev *he_dev, int group)
{
	struct he_buff *heb, *next;
	dma_addr_t mapping;
	int i;

	he_writel(he_dev, 0x0, G0_RBPS_S + (group * 32));
	he_writel(he_dev, 0x0, G0_RBPS_T + (group * 32));
	he_writel(he_dev, 0x0, G0_RBPS_QI + (group * 32));
	he_writel(he_dev, RBP_THRESH(0x1) | RBP_QSIZE(0x0),
		  G0_RBPS_BS + (group * 32));

	/* bitmap table */
	he_dev->rbpl_table = kmalloc(BITS_TO_LONGS(RBPL_TABLE_SIZE)
				     * sizeof(unsigned long), GFP_KERNEL);
	if (!he_dev->rbpl_table) {
		hprintk("unable to allocate rbpl bitmap table\n");
		return -ENOMEM;
	}
	bitmap_zero(he_dev->rbpl_table, RBPL_TABLE_SIZE);

	/* rbpl_virt 64-bit pointers */
	he_dev->rbpl_virt = kmalloc(RBPL_TABLE_SIZE
				    * sizeof(struct he_buff *), GFP_KERNEL);
	if (!he_dev->rbpl_virt) {
		hprintk("unable to allocate rbpl virt table\n");
		goto out_free_rbpl_table;
	}

	/* large buffer pool */
	he_dev->rbpl_pool = dma_pool_create("rbpl", &he_dev->pci_dev->dev,
					    CONFIG_RBPL_BUFSIZE, 64, 0);
	if (he_dev->rbpl_pool == NULL) {
		hprintk("unable to create rbpl pool\n");
		goto out_free_rbpl_virt;
	}

	he_dev->rbpl_base = dma_zalloc_coherent(&he_dev->pci_dev->dev,
						CONFIG_RBPL_SIZE * sizeof(struct he_rbp),
						&he_dev->rbpl_phys, GFP_KERNEL);
	if (he_dev->rbpl_base == NULL) {
		hprintk("failed to alloc rbpl_base\n");
		goto out_destroy_rbpl_pool;
	}

	INIT_LIST_HEAD(&he_dev->rbpl_outstanding);

	for (i = 0; i < CONFIG_RBPL_SIZE; ++i) {

		heb = dma_pool_alloc(he_dev->rbpl_pool, GFP_KERNEL, &mapping);
		if (!heb)
			goto out_free_rbpl;
		heb->mapping = mapping;
		list_add(&heb->entry, &he_dev->rbpl_outstanding);

		set_bit(i, he_dev->rbpl_table);
		he_dev->rbpl_virt[i] = heb;
		he_dev->rbpl_hint = i + 1;
		he_dev->rbpl_base[i].idx =  i << RBP_IDX_OFFSET;
		he_dev->rbpl_base[i].phys = mapping + offsetof(struct he_buff, data);
	}
	he_dev->rbpl_tail = &he_dev->rbpl_base[CONFIG_RBPL_SIZE - 1];

	he_writel(he_dev, he_dev->rbpl_phys, G0_RBPL_S + (group * 32));
	he_writel(he_dev, RBPL_MASK(he_dev->rbpl_tail),
						G0_RBPL_T + (group * 32));
	he_writel(he_dev, (CONFIG_RBPL_BUFSIZE - sizeof(struct he_buff))/4,
						G0_RBPL_BS + (group * 32));
	he_writel(he_dev,
			RBP_THRESH(CONFIG_RBPL_THRESH) |
			RBP_QSIZE(CONFIG_RBPL_SIZE - 1) |
			RBP_INT_ENB,
						G0_RBPL_QI + (group * 32));

	/* rx buffer ready queue */

	he_dev->rbrq_base = dma_zalloc_coherent(&he_dev->pci_dev->dev,
						CONFIG_RBRQ_SIZE * sizeof(struct he_rbrq),
						&he_dev->rbrq_phys, GFP_KERNEL);
	if (he_dev->rbrq_base == NULL) {
		hprintk("failed to allocate rbrq\n");
		goto out_free_rbpl;
	}

	he_dev->rbrq_head = he_dev->rbrq_base;
	he_writel(he_dev, he_dev->rbrq_phys, G0_RBRQ_ST + (group * 16));
	he_writel(he_dev, 0, G0_RBRQ_H + (group * 16));
	he_writel(he_dev,
		RBRQ_THRESH(CONFIG_RBRQ_THRESH) | RBRQ_SIZE(CONFIG_RBRQ_SIZE - 1),
						G0_RBRQ_Q + (group * 16));
	if (irq_coalesce) {
		hprintk("coalescing interrupts\n");
		he_writel(he_dev, RBRQ_TIME(768) | RBRQ_COUNT(7),
						G0_RBRQ_I + (group * 16));
	} else
		he_writel(he_dev, RBRQ_TIME(0) | RBRQ_COUNT(1),
						G0_RBRQ_I + (group * 16));

	/* tx buffer ready queue */

	he_dev->tbrq_base = dma_zalloc_coherent(&he_dev->pci_dev->dev,
						CONFIG_TBRQ_SIZE * sizeof(struct he_tbrq),
						&he_dev->tbrq_phys, GFP_KERNEL);
	if (he_dev->tbrq_base == NULL) {
		hprintk("failed to allocate tbrq\n");
		goto out_free_rbpq_base;
	}

	he_dev->tbrq_head = he_dev->tbrq_base;

	he_writel(he_dev, he_dev->tbrq_phys, G0_TBRQ_B_T + (group * 16));
	he_writel(he_dev, 0, G0_TBRQ_H + (group * 16));
	he_writel(he_dev, CONFIG_TBRQ_SIZE - 1, G0_TBRQ_S + (group * 16));
	he_writel(he_dev, CONFIG_TBRQ_THRESH, G0_TBRQ_THRESH + (group * 16));

	return 0;

out_free_rbpq_base:
	dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_RBRQ_SIZE *
			  sizeof(struct he_rbrq), he_dev->rbrq_base,
			  he_dev->rbrq_phys);
out_free_rbpl:
	list_for_each_entry_safe(heb, next, &he_dev->rbpl_outstanding, entry)
		dma_pool_free(he_dev->rbpl_pool, heb, heb->mapping);

	dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_RBPL_SIZE *
			  sizeof(struct he_rbp), he_dev->rbpl_base,
			  he_dev->rbpl_phys);
out_destroy_rbpl_pool:
	dma_pool_destroy(he_dev->rbpl_pool);
out_free_rbpl_virt:
	kfree(he_dev->rbpl_virt);
out_free_rbpl_table:
	kfree(he_dev->rbpl_table);

	return -ENOMEM;
}

static int he_init_irq(struct he_dev *he_dev)
{
	int i;

	/* 2.9.3.5  tail offset for each interrupt queue is located after the
		    end of the interrupt queue */

	he_dev->irq_base = dma_zalloc_coherent(&he_dev->pci_dev->dev,
					       (CONFIG_IRQ_SIZE + 1)
					       * sizeof(struct he_irq),
					       &he_dev->irq_phys,
					       GFP_KERNEL);
	if (he_dev->irq_base == NULL) {
		hprintk("failed to allocate irq\n");
		return -ENOMEM;
	}
	he_dev->irq_tailoffset = (unsigned *)
					&he_dev->irq_base[CONFIG_IRQ_SIZE];
	*he_dev->irq_tailoffset = 0;
	he_dev->irq_head = he_dev->irq_base;
	he_dev->irq_tail = he_dev->irq_base;

	for (i = 0; i < CONFIG_IRQ_SIZE; ++i)
		he_dev->irq_base[i].isw = ITYPE_INVALID;

	he_writel(he_dev, he_dev->irq_phys, IRQ0_BASE);
	he_writel(he_dev,
		IRQ_SIZE(CONFIG_IRQ_SIZE) | IRQ_THRESH(CONFIG_IRQ_THRESH),
								IRQ0_HEAD);
	he_writel(he_dev, IRQ_INT_A | IRQ_TYPE_LINE, IRQ0_CNTL);
	he_writel(he_dev, 0x0, IRQ0_DATA);

	he_writel(he_dev, 0x0, IRQ1_BASE);
	he_writel(he_dev, 0x0, IRQ1_HEAD);
	he_writel(he_dev, 0x0, IRQ1_CNTL);
	he_writel(he_dev, 0x0, IRQ1_DATA);

	he_writel(he_dev, 0x0, IRQ2_BASE);
	he_writel(he_dev, 0x0, IRQ2_HEAD);
	he_writel(he_dev, 0x0, IRQ2_CNTL);
	he_writel(he_dev, 0x0, IRQ2_DATA);

	he_writel(he_dev, 0x0, IRQ3_BASE);
	he_writel(he_dev, 0x0, IRQ3_HEAD);
	he_writel(he_dev, 0x0, IRQ3_CNTL);
	he_writel(he_dev, 0x0, IRQ3_DATA);

	/* 2.9.3.2 interrupt queue mapping registers */

	he_writel(he_dev, 0x0, GRP_10_MAP);
	he_writel(he_dev, 0x0, GRP_32_MAP);
	he_writel(he_dev, 0x0, GRP_54_MAP);
	he_writel(he_dev, 0x0, GRP_76_MAP);

	if (request_irq(he_dev->pci_dev->irq,
			he_irq_handler, IRQF_SHARED, DEV_LABEL, he_dev)) {
		hprintk("irq %d already in use\n", he_dev->pci_dev->irq);
		return -EINVAL;
	}   

	he_dev->irq = he_dev->pci_dev->irq;

	return 0;
}

static int he_start(struct atm_dev *dev)
{
	struct he_dev *he_dev;
	struct pci_dev *pci_dev;
	unsigned long membase;

	u16 command;
	u32 gen_cntl_0, host_cntl, lb_swap;
	u8 cache_size, timer;
	
	unsigned err;
	unsigned int status, reg;
	int i, group;

	he_dev = HE_DEV(dev);
	pci_dev = he_dev->pci_dev;

	membase = pci_resource_start(pci_dev, 0);
	HPRINTK("membase = 0x%lx  irq = %d.\n", membase, pci_dev->irq);

	/*
	 * pci bus controller initialization 
	 */

	/* 4.3 pci bus controller-specific initialization */
	if (pci_read_config_dword(pci_dev, GEN_CNTL_0, &gen_cntl_0) != 0) {
		hprintk("can't read GEN_CNTL_0\n");
		return -EINVAL;
	}
	gen_cntl_0 |= (MRL_ENB | MRM_ENB | IGNORE_TIMEOUT);
	if (pci_write_config_dword(pci_dev, GEN_CNTL_0, gen_cntl_0) != 0) {
		hprintk("can't write GEN_CNTL_0.\n");
		return -EINVAL;
	}

	if (pci_read_config_word(pci_dev, PCI_COMMAND, &command) != 0) {
		hprintk("can't read PCI_COMMAND.\n");
		return -EINVAL;
	}

	command |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE);
	if (pci_write_config_word(pci_dev, PCI_COMMAND, command) != 0) {
		hprintk("can't enable memory.\n");
		return -EINVAL;
	}

	if (pci_read_config_byte(pci_dev, PCI_CACHE_LINE_SIZE, &cache_size)) {
		hprintk("can't read cache line size?\n");
		return -EINVAL;
	}

	if (cache_size < 16) {
		cache_size = 16;
		if (pci_write_config_byte(pci_dev, PCI_CACHE_LINE_SIZE, cache_size))
			hprintk("can't set cache line size to %d\n", cache_size);
	}

	if (pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &timer)) {
		hprintk("can't read latency timer?\n");
		return -EINVAL;
	}

	/* from table 3.9
	 *
	 * LAT_TIMER = 1 + AVG_LAT + BURST_SIZE/BUS_SIZE
	 * 
	 * AVG_LAT: The average first data read/write latency [maximum 16 clock cycles]
	 * BURST_SIZE: 1536 bytes (read) for 622, 768 bytes (read) for 155 [192 clock cycles]
	 *
	 */ 
#define LAT_TIMER 209
	if (timer < LAT_TIMER) {
		HPRINTK("latency timer was %d, setting to %d\n", timer, LAT_TIMER);
		timer = LAT_TIMER;
		if (pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, timer))
			hprintk("can't set latency timer to %d\n", timer);
	}

	if (!(he_dev->membase = ioremap(membase, HE_REGMAP_SIZE))) {
		hprintk("can't set up page mapping\n");
		return -EINVAL;
	}

	/* 4.4 card reset */
	he_writel(he_dev, 0x0, RESET_CNTL);
	he_writel(he_dev, 0xff, RESET_CNTL);

	msleep(16);	/* 16 ms */
	status = he_readl(he_dev, RESET_CNTL);
	if ((status & BOARD_RST_STATUS) == 0) {
		hprintk("reset failed\n");
		return -EINVAL;
	}

	/* 4.5 set bus width */
	host_cntl = he_readl(he_dev, HOST_CNTL);
	if (host_cntl & PCI_BUS_SIZE64)
		gen_cntl_0 |= ENBL_64;
	else
		gen_cntl_0 &= ~ENBL_64;

	if (disable64 == 1) {
		hprintk("disabling 64-bit pci bus transfers\n");
		gen_cntl_0 &= ~ENBL_64;
	}

	if (gen_cntl_0 & ENBL_64)
		hprintk("64-bit transfers enabled\n");

	pci_write_config_dword(pci_dev, GEN_CNTL_0, gen_cntl_0);

	/* 4.7 read prom contents */
	for (i = 0; i < PROD_ID_LEN; ++i)
		he_dev->prod_id[i] = read_prom_byte(he_dev, PROD_ID + i);

	he_dev->media = read_prom_byte(he_dev, MEDIA);

	for (i = 0; i < 6; ++i)
		dev->esi[i] = read_prom_byte(he_dev, MAC_ADDR + i);

	hprintk("%s%s, %pM\n", he_dev->prod_id,
		he_dev->media & 0x40 ? "SM" : "MM", dev->esi);
	he_dev->atm_dev->link_rate = he_is622(he_dev) ?
						ATM_OC12_PCR : ATM_OC3_PCR;

	/* 4.6 set host endianess */
	lb_swap = he_readl(he_dev, LB_SWAP);
	if (he_is622(he_dev))
		lb_swap &= ~XFER_SIZE;		/* 4 cells */
	else
		lb_swap |= XFER_SIZE;		/* 8 cells */
#ifdef __BIG_ENDIAN
	lb_swap |= DESC_WR_SWAP | INTR_SWAP | BIG_ENDIAN_HOST;
#else
	lb_swap &= ~(DESC_WR_SWAP | INTR_SWAP | BIG_ENDIAN_HOST |
			DATA_WR_SWAP | DATA_RD_SWAP | DESC_RD_SWAP);
#endif /* __BIG_ENDIAN */
	he_writel(he_dev, lb_swap, LB_SWAP);

	/* 4.8 sdram controller initialization */
	he_writel(he_dev, he_is622(he_dev) ? LB_64_ENB : 0x0, SDRAM_CTL);

	/* 4.9 initialize rnum value */
	lb_swap |= SWAP_RNUM_MAX(0xf);
	he_writel(he_dev, lb_swap, LB_SWAP);

	/* 4.10 initialize the interrupt queues */
	if ((err = he_init_irq(he_dev)) != 0)
		return err;

	/* 4.11 enable pci bus controller state machines */
	host_cntl |= (OUTFF_ENB | CMDFF_ENB |
				QUICK_RD_RETRY | QUICK_WR_RETRY | PERR_INT_ENB);
	he_writel(he_dev, host_cntl, HOST_CNTL);

	gen_cntl_0 |= INT_PROC_ENBL|INIT_ENB;
	pci_write_config_dword(pci_dev, GEN_CNTL_0, gen_cntl_0);

	/*
	 * atm network controller initialization
	 */

	/* 5.1.1 generic configuration state */

	/*
	 *		local (cell) buffer memory map
	 *                    
	 *             HE155                          HE622
	 *                                                      
	 *        0 ____________1023 bytes  0 _______________________2047 bytes
	 *         |            |            |                   |   |
	 *         |  utility   |            |        rx0        |   |
	 *        5|____________|         255|___________________| u |
	 *        6|            |         256|                   | t |
	 *         |            |            |                   | i |
	 *         |    rx0     |     row    |        tx         | l |
	 *         |            |            |                   | i |
	 *         |            |         767|___________________| t |
	 *      517|____________|         768|                   | y |
	 * row  518|            |            |        rx1        |   |
	 *         |            |        1023|___________________|___|
	 *         |            |
	 *         |    tx      |
	 *         |            |
	 *         |            |
	 *     1535|____________|
	 *     1536|            |
	 *         |    rx1     |
	 *     2047|____________|
	 *
	 */

	/* total 4096 connections */
	he_dev->vcibits = CONFIG_DEFAULT_VCIBITS;
	he_dev->vpibits = CONFIG_DEFAULT_VPIBITS;

	if (nvpibits != -1 && nvcibits != -1 && nvpibits+nvcibits != HE_MAXCIDBITS) {
		hprintk("nvpibits + nvcibits != %d\n", HE_MAXCIDBITS);
		return -ENODEV;
	}

	if (nvpibits != -1) {
		he_dev->vpibits = nvpibits;
		he_dev->vcibits = HE_MAXCIDBITS - nvpibits;
	}

	if (nvcibits != -1) {
		he_dev->vcibits = nvcibits;
		he_dev->vpibits = HE_MAXCIDBITS - nvcibits;
	}


	if (he_is622(he_dev)) {
		he_dev->cells_per_row = 40;
		he_dev->bytes_per_row = 2048;
		he_dev->r0_numrows = 256;
		he_dev->tx_numrows = 512;
		he_dev->r1_numrows = 256;
		he_dev->r0_startrow = 0;
		he_dev->tx_startrow = 256;
		he_dev->r1_startrow = 768;
	} else {
		he_dev->cells_per_row = 20;
		he_dev->bytes_per_row = 1024;
		he_dev->r0_numrows = 512;
		he_dev->tx_numrows = 1018;
		he_dev->r1_numrows = 512;
		he_dev->r0_startrow = 6;
		he_dev->tx_startrow = 518;
		he_dev->r1_startrow = 1536;
	}

	he_dev->cells_per_lbuf = 4;
	he_dev->buffer_limit = 4;
	he_dev->r0_numbuffs = he_dev->r0_numrows *
				he_dev->cells_per_row / he_dev->cells_per_lbuf;
	if (he_dev->r0_numbuffs > 2560)
		he_dev->r0_numbuffs = 2560;

	he_dev->r1_numbuffs = he_dev->r1_numrows *
				he_dev->cells_per_row / he_dev->cells_per_lbuf;
	if (he_dev->r1_numbuffs > 2560)
		he_dev->r1_numbuffs = 2560;

	he_dev->tx_numbuffs = he_dev->tx_numrows *
				he_dev->cells_per_row / he_dev->cells_per_lbuf;
	if (he_dev->tx_numbuffs > 5120)
		he_dev->tx_numbuffs = 5120;

	/* 5.1.2 configure hardware dependent registers */

	he_writel(he_dev, 
		SLICE_X(0x2) | ARB_RNUM_MAX(0xf) | TH_PRTY(0x3) |
		RH_PRTY(0x3) | TL_PRTY(0x2) | RL_PRTY(0x1) |
		(he_is622(he_dev) ? BUS_MULTI(0x28) : BUS_MULTI(0x46)) |
		(he_is622(he_dev) ? NET_PREF(0x50) : NET_PREF(0x8c)),
								LBARB);

	he_writel(he_dev, BANK_ON |
		(he_is622(he_dev) ? (REF_RATE(0x384) | WIDE_DATA) : REF_RATE(0x150)),
								SDRAMCON);

	he_writel(he_dev,
		(he_is622(he_dev) ? RM_BANK_WAIT(1) : RM_BANK_WAIT(0)) |
						RM_RW_WAIT(1), RCMCONFIG);
	he_writel(he_dev,
		(he_is622(he_dev) ? TM_BANK_WAIT(2) : TM_BANK_WAIT(1)) |
						TM_RW_WAIT(1), TCMCONFIG);

	he_writel(he_dev, he_dev->cells_per_lbuf * ATM_CELL_PAYLOAD, LB_CONFIG);

	he_writel(he_dev, 
		(he_is622(he_dev) ? UT_RD_DELAY(8) : UT_RD_DELAY(0)) |
		(he_is622(he_dev) ? RC_UT_MODE(0) : RC_UT_MODE(1)) |
		RX_VALVP(he_dev->vpibits) |
		RX_VALVC(he_dev->vcibits),			 RC_CONFIG);

	he_writel(he_dev, DRF_THRESH(0x20) |
		(he_is622(he_dev) ? TX_UT_MODE(0) : TX_UT_MODE(1)) |
		TX_VCI_MASK(he_dev->vcibits) |
		LBFREE_CNT(he_dev->tx_numbuffs), 		TX_CONFIG);

	he_writel(he_dev, 0x0, TXAAL5_PROTO);

	he_writel(he_dev, PHY_INT_ENB |
		(he_is622(he_dev) ? PTMR_PRE(67 - 1) : PTMR_PRE(50 - 1)),
								RH_CONFIG);

	/* 5.1.3 initialize connection memory */

	for (i = 0; i < TCM_MEM_SIZE; ++i)
		he_writel_tcm(he_dev, 0, i);

	for (i = 0; i < RCM_MEM_SIZE; ++i)
		he_writel_rcm(he_dev, 0, i);

	/*
	 *	transmit connection memory map
	 *
	 *                  tx memory
	 *          0x0 ___________________
	 *             |                   |
	 *             |                   |
	 *             |       TSRa        |
	 *             |                   |
	 *             |                   |
	 *       0x8000|___________________|
	 *             |                   |
	 *             |       TSRb        |
	 *       0xc000|___________________|
	 *             |                   |
	 *             |       TSRc        |
	 *       0xe000|___________________|
	 *             |       TSRd        |
	 *       0xf000|___________________|
	 *             |       tmABR       |
	 *      0x10000|___________________|
	 *             |                   |
	 *             |       tmTPD       |
	 *             |___________________|
	 *             |                   |
	 *                      ....
	 *      0x1ffff|___________________|
	 *
	 *
	 */

	he_writel(he_dev, CONFIG_TSRB, TSRB_BA);
	he_writel(he_dev, CONFIG_TSRC, TSRC_BA);
	he_writel(he_dev, CONFIG_TSRD, TSRD_BA);
	he_writel(he_dev, CONFIG_TMABR, TMABR_BA);
	he_writel(he_dev, CONFIG_TPDBA, TPD_BA);


	/*
	 *	receive connection memory map
	 *
	 *          0x0 ___________________
	 *             |                   |
	 *             |                   |
	 *             |       RSRa        |
	 *             |                   |
	 *             |                   |
	 *       0x8000|___________________|
	 *             |                   |
	 *             |             rx0/1 |
	 *             |       LBM         |   link lists of local
	 *             |             tx    |   buffer memory 
	 *             |                   |
	 *       0xd000|___________________|
	 *             |                   |
	 *             |      rmABR        |
	 *       0xe000|___________________|
	 *             |                   |
	 *             |       RSRb        |
	 *             |___________________|
	 *             |                   |
	 *                      ....
	 *       0xffff|___________________|
	 */

	he_writel(he_dev, 0x08000, RCMLBM_BA);
	he_writel(he_dev, 0x0e000, RCMRSRB_BA);
	he_writel(he_dev, 0x0d800, RCMABR_BA);

	/* 5.1.4 initialize local buffer free pools linked lists */

	he_init_rx_lbfp0(he_dev);
	he_init_rx_lbfp1(he_dev);

	he_writel(he_dev, 0x0, RLBC_H);
	he_writel(he_dev, 0x0, RLBC_T);
	he_writel(he_dev, 0x0, RLBC_H2);

	he_writel(he_dev, 512, RXTHRSH);	/* 10% of r0+r1 buffers */
	he_writel(he_dev, 256, LITHRSH); 	/* 5% of r0+r1 buffers */

	he_init_tx_lbfp(he_dev);

	he_writel(he_dev, he_is622(he_dev) ? 0x104780 : 0x800, UBUFF_BA);

	/* 5.1.5 initialize intermediate receive queues */

	if (he_is622(he_dev)) {
		he_writel(he_dev, 0x000f, G0_INMQ_S);
		he_writel(he_dev, 0x200f, G0_INMQ_L);

		he_writel(he_dev, 0x001f, G1_INMQ_S);
		he_writel(he_dev, 0x201f, G1_INMQ_L);

		he_writel(he_dev, 0x002f, G2_INMQ_S);
		he_writel(he_dev, 0x202f, G2_INMQ_L);

		he_writel(he_dev, 0x003f, G3_INMQ_S);
		he_writel(he_dev, 0x203f, G3_INMQ_L);

		he_writel(he_dev, 0x004f, G4_INMQ_S);
		he_writel(he_dev, 0x204f, G4_INMQ_L);

		he_writel(he_dev, 0x005f, G5_INMQ_S);
		he_writel(he_dev, 0x205f, G5_INMQ_L);

		he_writel(he_dev, 0x006f, G6_INMQ_S);
		he_writel(he_dev, 0x206f, G6_INMQ_L);

		he_writel(he_dev, 0x007f, G7_INMQ_S);
		he_writel(he_dev, 0x207f, G7_INMQ_L);
	} else {
		he_writel(he_dev, 0x0000, G0_INMQ_S);
		he_writel(he_dev, 0x0008, G0_INMQ_L);

		he_writel(he_dev, 0x0001, G1_INMQ_S);
		he_writel(he_dev, 0x0009, G1_INMQ_L);

		he_writel(he_dev, 0x0002, G2_INMQ_S);
		he_writel(he_dev, 0x000a, G2_INMQ_L);

		he_writel(he_dev, 0x0003, G3_INMQ_S);
		he_writel(he_dev, 0x000b, G3_INMQ_L);

		he_writel(he_dev, 0x0004, G4_INMQ_S);
		he_writel(he_dev, 0x000c, G4_INMQ_L);

		he_writel(he_dev, 0x0005, G5_INMQ_S);
		he_writel(he_dev, 0x000d, G5_INMQ_L);

		he_writel(he_dev, 0x0006, G6_INMQ_S);
		he_writel(he_dev, 0x000e, G6_INMQ_L);

		he_writel(he_dev, 0x0007, G7_INMQ_S);
		he_writel(he_dev, 0x000f, G7_INMQ_L);
	}

	/* 5.1.6 application tunable parameters */

	he_writel(he_dev, 0x0, MCC);
	he_writel(he_dev, 0x0, OEC);
	he_writel(he_dev, 0x0, DCC);
	he_writel(he_dev, 0x0, CEC);
	
	/* 5.1.7 cs block initialization */

	he_init_cs_block(he_dev);

	/* 5.1.8 cs block connection memory initialization */
	
	if (he_init_cs_block_rcm(he_dev) < 0)
		return -ENOMEM;

	/* 5.1.10 initialize host structures */

	he_init_tpdrq(he_dev);

	he_dev->tpd_pool = dma_pool_create("tpd", &he_dev->pci_dev->dev,
					   sizeof(struct he_tpd), TPD_ALIGNMENT, 0);
	if (he_dev->tpd_pool == NULL) {
		hprintk("unable to create tpd dma_pool\n");
		return -ENOMEM;         
	}

	INIT_LIST_HEAD(&he_dev->outstanding_tpds);

	if (he_init_group(he_dev, 0) != 0)
		return -ENOMEM;

	for (group = 1; group < HE_NUM_GROUPS; ++group) {
		he_writel(he_dev, 0x0, G0_RBPS_S + (group * 32));
		he_writel(he_dev, 0x0, G0_RBPS_T + (group * 32));
		he_writel(he_dev, 0x0, G0_RBPS_QI + (group * 32));
		he_writel(he_dev, RBP_THRESH(0x1) | RBP_QSIZE(0x0),
						G0_RBPS_BS + (group * 32));

		he_writel(he_dev, 0x0, G0_RBPL_S + (group * 32));
		he_writel(he_dev, 0x0, G0_RBPL_T + (group * 32));
		he_writel(he_dev, RBP_THRESH(0x1) | RBP_QSIZE(0x0),
						G0_RBPL_QI + (group * 32));
		he_writel(he_dev, 0x0, G0_RBPL_BS + (group * 32));

		he_writel(he_dev, 0x0, G0_RBRQ_ST + (group * 16));
		he_writel(he_dev, 0x0, G0_RBRQ_H + (group * 16));
		he_writel(he_dev, RBRQ_THRESH(0x1) | RBRQ_SIZE(0x0),
						G0_RBRQ_Q + (group * 16));
		he_writel(he_dev, 0x0, G0_RBRQ_I + (group * 16));

		he_writel(he_dev, 0x0, G0_TBRQ_B_T + (group * 16));
		he_writel(he_dev, 0x0, G0_TBRQ_H + (group * 16));
		he_writel(he_dev, TBRQ_THRESH(0x1),
						G0_TBRQ_THRESH + (group * 16));
		he_writel(he_dev, 0x0, G0_TBRQ_S + (group * 16));
	}

	/* host status page */

	he_dev->hsp = dma_zalloc_coherent(&he_dev->pci_dev->dev,
					  sizeof(struct he_hsp),
					  &he_dev->hsp_phys, GFP_KERNEL);
	if (he_dev->hsp == NULL) {
		hprintk("failed to allocate host status page\n");
		return -ENOMEM;
	}
	he_writel(he_dev, he_dev->hsp_phys, HSP_BA);

	/* initialize framer */

#ifdef CONFIG_ATM_HE_USE_SUNI
	if (he_isMM(he_dev))
		suni_init(he_dev->atm_dev);
	if (he_dev->atm_dev->phy && he_dev->atm_dev->phy->start)
		he_dev->atm_dev->phy->start(he_dev->atm_dev);
#endif /* CONFIG_ATM_HE_USE_SUNI */

	if (sdh) {
		/* this really should be in suni.c but for now... */
		int val;

		val = he_phy_get(he_dev->atm_dev, SUNI_TPOP_APM);
		val = (val & ~SUNI_TPOP_APM_S) | (SUNI_TPOP_S_SDH << SUNI_TPOP_APM_S_SHIFT);
		he_phy_put(he_dev->atm_dev, val, SUNI_TPOP_APM);
		he_phy_put(he_dev->atm_dev, SUNI_TACP_IUCHP_CLP, SUNI_TACP_IUCHP);
	}

	/* 5.1.12 enable transmit and receive */

	reg = he_readl_mbox(he_dev, CS_ERCTL0);
	reg |= TX_ENABLE|ER_ENABLE;
	he_writel_mbox(he_dev, reg, CS_ERCTL0);

	reg = he_readl(he_dev, RC_CONFIG);
	reg |= RX_ENABLE;
	he_writel(he_dev, reg, RC_CONFIG);

	for (i = 0; i < HE_NUM_CS_STPER; ++i) {
		he_dev->cs_stper[i].inuse = 0;
		he_dev->cs_stper[i].pcr = -1;
	}
	he_dev->total_bw = 0;


	/* atm linux initialization */

	he_dev->atm_dev->ci_range.vpi_bits = he_dev->vpibits;
	he_dev->atm_dev->ci_range.vci_bits = he_dev->vcibits;

	he_dev->irq_peak = 0;
	he_dev->rbrq_peak = 0;
	he_dev->rbpl_peak = 0;
	he_dev->tbrq_peak = 0;

	HPRINTK("hell bent for leather!\n");

	return 0;
}

static void
he_stop(struct he_dev *he_dev)
{
	struct he_buff *heb, *next;
	struct pci_dev *pci_dev;
	u32 gen_cntl_0, reg;
	u16 command;

	pci_dev = he_dev->pci_dev;

	/* disable interrupts */

	if (he_dev->membase) {
		pci_read_config_dword(pci_dev, GEN_CNTL_0, &gen_cntl_0);
		gen_cntl_0 &= ~(INT_PROC_ENBL | INIT_ENB);
		pci_write_config_dword(pci_dev, GEN_CNTL_0, gen_cntl_0);

		tasklet_disable(&he_dev->tasklet);

		/* disable recv and transmit */

		reg = he_readl_mbox(he_dev, CS_ERCTL0);
		reg &= ~(TX_ENABLE|ER_ENABLE);
		he_writel_mbox(he_dev, reg, CS_ERCTL0);

		reg = he_readl(he_dev, RC_CONFIG);
		reg &= ~(RX_ENABLE);
		he_writel(he_dev, reg, RC_CONFIG);
	}

#ifdef CONFIG_ATM_HE_USE_SUNI
	if (he_dev->atm_dev->phy && he_dev->atm_dev->phy->stop)
		he_dev->atm_dev->phy->stop(he_dev->atm_dev);
#endif /* CONFIG_ATM_HE_USE_SUNI */

	if (he_dev->irq)
		free_irq(he_dev->irq, he_dev);

	if (he_dev->irq_base)
		dma_free_coherent(&he_dev->pci_dev->dev, (CONFIG_IRQ_SIZE + 1)
				  * sizeof(struct he_irq), he_dev->irq_base, he_dev->irq_phys);

	if (he_dev->hsp)
		dma_free_coherent(&he_dev->pci_dev->dev, sizeof(struct he_hsp),
				  he_dev->hsp, he_dev->hsp_phys);

	if (he_dev->rbpl_base) {
		list_for_each_entry_safe(heb, next, &he_dev->rbpl_outstanding, entry)
			dma_pool_free(he_dev->rbpl_pool, heb, heb->mapping);

		dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_RBPL_SIZE
				  * sizeof(struct he_rbp), he_dev->rbpl_base, he_dev->rbpl_phys);
	}

	kfree(he_dev->rbpl_virt);
	kfree(he_dev->rbpl_table);

	if (he_dev->rbpl_pool)
		dma_pool_destroy(he_dev->rbpl_pool);

	if (he_dev->rbrq_base)
		dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_RBRQ_SIZE * sizeof(struct he_rbrq),
				  he_dev->rbrq_base, he_dev->rbrq_phys);

	if (he_dev->tbrq_base)
		dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_TBRQ_SIZE * sizeof(struct he_tbrq),
				  he_dev->tbrq_base, he_dev->tbrq_phys);

	if (he_dev->tpdrq_base)
		dma_free_coherent(&he_dev->pci_dev->dev, CONFIG_TBRQ_SIZE * sizeof(struct he_tbrq),
				  he_dev->tpdrq_base, he_dev->tpdrq_phys);

	if (he_dev->tpd_pool)
		dma_pool_destroy(he_dev->tpd_pool);

	if (he_dev->pci_dev) {
		pci_read_config_word(he_dev->pci_dev, PCI_COMMAND, &command);
		command &= ~(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
		pci_write_config_word(he_dev->pci_dev, PCI_COMMAND, command);
	}
	
	if (he_dev->membase)
		iounmap(he_dev->membase);
}

static struct he_tpd *
__alloc_tpd(struct he_dev *he_dev)
{
	struct he_tpd *tpd;
	dma_addr_t mapping;

	tpd = dma_pool_alloc(he_dev->tpd_pool, GFP_ATOMIC, &mapping);
	if (tpd == NULL)
		return NULL;
			
	tpd->status = TPD_ADDR(mapping);
	tpd->reserved = 0; 
	tpd->iovec[0].addr = 0; tpd->iovec[0].len = 0;
	tpd->iovec[1].addr = 0; tpd->iovec[1].len = 0;
	tpd->iovec[2].addr = 0; tpd->iovec[2].len = 0;

	return tpd;
}

#define AAL5_LEN(buf,len) 						\
			((((unsigned char *)(buf))[(len)-6] << 8) |	\
				(((unsigned char *)(buf))[(len)-5]))

/* 2.10.1.2 receive
 *
 * aal5 packets can optionally return the tcp checksum in the lower
 * 16 bits of the crc (RSR0_TCP_CKSUM)
 */

#define TCP_CKSUM(buf,len) 						\
			((((unsigned char *)(buf))[(len)-2] << 8) |	\
				(((unsigned char *)(buf))[(len-1)]))

static int
he_service_rbrq(struct he_dev *he_dev, int group)
{
	struct he_rbrq *rbrq_tail = (struct he_rbrq *)
				((unsigned long)he_dev->rbrq_base |
					he_dev->hsp->group[group].rbrq_tail);
	unsigned cid, lastcid = -1;
	struct sk_buff *skb;
	struct atm_vcc *vcc = NULL;
	struct he_vcc *he_vcc;
	struct he_buff *heb, *next;
	int i;
	int pdus_assembled = 0;
	int updated = 0;

	read_lock(&vcc_sklist_lock);
	while (he_dev->rbrq_head != rbrq_tail) {
		++updated;

		HPRINTK("%p rbrq%d 0x%x len=%d cid=0x%x %s%s%s%s%s%s\n",
			he_dev->rbrq_head, group,
			RBRQ_ADDR(he_dev->rbrq_head),
			RBRQ_BUFLEN(he_dev->rbrq_head),
			RBRQ_CID(he_dev->rbrq_head),
			RBRQ_CRC_ERR(he_dev->rbrq_head) ? " CRC_ERR" : "",
			RBRQ_LEN_ERR(he_dev->rbrq_head) ? " LEN_ERR" : "",
			RBRQ_END_PDU(he_dev->rbrq_head) ? " END_PDU" : "",
			RBRQ_AAL5_PROT(he_dev->rbrq_head) ? " AAL5_PROT" : "",
			RBRQ_CON_CLOSED(he_dev->rbrq_head) ? " CON_CLOSED" : "",
			RBRQ_HBUF_ERR(he_dev->rbrq_head) ? " HBUF_ERR" : "");

		i = RBRQ_ADDR(he_dev->rbrq_head) >> RBP_IDX_OFFSET;
		heb = he_dev->rbpl_virt[i];

		cid = RBRQ_CID(he_dev->rbrq_head);
		if (cid != lastcid)
			vcc = __find_vcc(he_dev, cid);
		lastcid = cid;

		if (vcc == NULL || (he_vcc = HE_VCC(vcc)) == NULL) {
			hprintk("vcc/he_vcc == NULL  (cid 0x%x)\n", cid);
			if (!RBRQ_HBUF_ERR(he_dev->rbrq_head)) {
				clear_bit(i, he_dev->rbpl_table);
				list_del(&heb->entry);
				dma_pool_free(he_dev->rbpl_pool, heb, heb->mapping);
			}
					
			goto next_rbrq_entry;
		}

		if (RBRQ_HBUF_ERR(he_dev->rbrq_head)) {
			hprintk("HBUF_ERR!  (cid 0x%x)\n", cid);
				atomic_inc(&vcc->stats->rx_drop);
			goto return_host_buffers;
		}

		heb->len = RBRQ_BUFLEN(he_dev->rbrq_head) * 4;
		clear_bit(i, he_dev->rbpl_table);
		list_move_tail(&heb->entry, &he_vcc->buffers);
		he_vcc->pdu_len += heb->len;

		if (RBRQ_CON_CLOSED(he_dev->rbrq_head)) {
			lastcid = -1;
			HPRINTK("wake_up rx_waitq  (cid 0x%x)\n", cid);
			wake_up(&he_vcc->rx_waitq);
			goto return_host_buffers;
		}

		if (!RBRQ_END_PDU(he_dev->rbrq_head))
			goto next_rbrq_entry;

		if (RBRQ_LEN_ERR(he_dev->rbrq_head)
				|| RBRQ_CRC_ERR(he_dev->rbrq_head)) {
			HPRINTK("%s%s (%d.%d)\n",
				RBRQ_CRC_ERR(he_dev->rbrq_head)
							? "CRC_ERR " : "",
				RBRQ_LEN_ERR(he_dev->rbrq_head)
							? "LEN_ERR" : "",
							vcc->vpi, vcc->vci);
			atomic_inc(&vcc->stats->rx_err);
			goto return_host_buffers;
		}

		skb = atm_alloc_charge(vcc, he_vcc->pdu_len + rx_skb_reserve,
							GFP_ATOMIC);
		if (!skb) {
			HPRINTK("charge failed (%d.%d)\n", vcc->vpi, vcc->vci);
			goto return_host_buffers;
		}

		if (rx_skb_reserve > 0)
			skb_reserve(skb, rx_skb_reserve);

		__net_timestamp(skb);

		list_for_each_entry(heb, &he_vcc->buffers, entry)
			memcpy(skb_put(skb, heb->len), &heb->data, heb->len);

		switch (vcc->qos.aal) {
			case ATM_AAL0:
				/* 2.10.1.5 raw cell receive */
				skb->len = ATM_AAL0_SDU;
				skb_set_tail_pointer(skb, skb->len);
				break;
			case ATM_AAL5:
				/* 2.10.1.2 aal5 receive */

				skb->len = AAL5_LEN(skb->data, he_vcc->pdu_len);
				skb_set_tail_pointer(skb, skb->len);
#ifdef USE_CHECKSUM_HW
				if (vcc->vpi == 0 && vcc->vci >= ATM_NOT_RSV_VCI) {
					skb->ip_summed = CHECKSUM_COMPLETE;
					skb->csum = TCP_CKSUM(skb->data,
							he_vcc->pdu_len);
				}
#endif
				break;
		}

#ifdef should_never_happen
		if (skb->len > vcc->qos.rxtp.max_sdu)
			hprintk("pdu_len (%d) > vcc->qos.rxtp.max_sdu (%d)!  cid 0x%x\n", skb->len, vcc->qos.rxtp.max_sdu, cid);
#endif

#ifdef notdef
		ATM_SKB(skb)->vcc = vcc;
#endif
		spin_unlock(&he_dev->global_lock);
		vcc->push(vcc, skb);
		spin_lock(&he_dev->global_lock);

		atomic_inc(&vcc->stats->rx);

return_host_buffers:
		++pdus_assembled;

		list_for_each_entry_safe(heb, next, &he_vcc->buffers, entry)
			dma_pool_free(he_dev->rbpl_pool, heb, heb->mapping);
		INIT_LIST_HEAD(&he_vcc->buffers);
		he_vcc->pdu_len = 0;

next_rbrq_entry:
		he_dev->rbrq_head = (struct he_rbrq *)
				((unsigned long) he_dev->rbrq_base |
					RBRQ_MASK(he_dev->rbrq_head + 1));

	}
	read_unlock(&vcc_sklist_lock);

	if (updated) {
		if (updated > he_dev->rbrq_peak)
			he_dev->rbrq_peak = updated;

		he_writel(he_dev, RBRQ_MASK(he_dev->rbrq_head),
						G0_RBRQ_H + (group * 16));
	}

	return pdus_assembled;
}

static void
he_service_tbrq(struct he_dev *he_dev, int group)
{
	struct he_tbrq *tbrq_tail = (struct he_tbrq *)
				((unsigned long)he_dev->tbrq_base |
					he_dev->hsp->group[group].tbrq_tail);
	struct he_tpd *tpd;
	int slot, updated = 0;
	struct he_tpd *__tpd;

	/* 2.1.6 transmit buffer return queue */

	while (he_dev->tbrq_head != tbrq_tail) {
		++updated;

		HPRINTK("tbrq%d 0x%x%s%s\n",
			group,
			TBRQ_TPD(he_dev->tbrq_head), 
			TBRQ_EOS(he_dev->tbrq_head) ? " EOS" : "",
			TBRQ_MULTIPLE(he_dev->tbrq_head) ? " MULTIPLE" : "");
		tpd = NULL;
		list_for_each_entry(__tpd, &he_dev->outstanding_tpds, entry) {
			if (TPD_ADDR(__tpd->status) == TBRQ_TPD(he_dev->tbrq_head)) {
				tpd = __tpd;
				list_del(&__tpd->entry);
				break;
			}
		}

		if (tpd == NULL) {
			hprintk("unable to locate tpd for dma buffer %x\n",
						TBRQ_TPD(he_dev->tbrq_head));
			goto next_tbrq_entry;
		}

		if (TBRQ_EOS(he_dev->tbrq_head)) {
			HPRINTK("wake_up(tx_waitq) cid 0x%x\n",
				he_mkcid(he_dev, tpd->vcc->vpi, tpd->vcc->vci));
			if (tpd->vcc)
				wake_up(&HE_VCC(tpd->vcc)->tx_waitq);

			goto next_tbrq_entry;
		}

		for (slot = 0; slot < TPD_MAXIOV; ++slot) {
			if (tpd->iovec[slot].addr)
				dma_unmap_single(&he_dev->pci_dev->dev,
					tpd->iovec[slot].addr,
					tpd->iovec[slot].len & TPD_LEN_MASK,
							DMA_TO_DEVICE);
			if (tpd->iovec[slot].len & TPD_LST)
				break;
				
		}

		if (tpd->skb) {	/* && !TBRQ_MULTIPLE(he_dev->tbrq_head) */
			if (tpd->vcc && tpd->vcc->pop)
				tpd->vcc->pop(tpd->vcc, tpd->skb);
			else
				dev_kfree_skb_any(tpd->skb);
		}

next_tbrq_entry:
		if (tpd)
			dma_pool_free(he_dev->tpd_pool, tpd, TPD_ADDR(tpd->status));
		he_dev->tbrq_head = (struct he_tbrq *)
				((unsigned long) he_dev->tbrq_base |
					TBRQ_MASK(he_dev->tbrq_head + 1));
	}

	if (updated) {
		if (updated > he_dev->tbrq_peak)
			he_dev->tbrq_peak = updated;

		he_writel(he_dev, TBRQ_MASK(he_dev->tbrq_head),
						G0_TBRQ_H + (group * 16));
	}
}

static void
he_service_rbpl(struct he_dev *he_dev, int group)
{
	struct he_rbp *new_tail;
	struct he_rbp *rbpl_head;
	struct he_buff *heb;
	dma_addr_t mapping;
	int i;
	int moved = 0;

	rbpl_head = (struct he_rbp *) ((unsigned long)he_dev->rbpl_base |
					RBPL_MASK(he_readl(he_dev, G0_RBPL_S)));

	for (;;) {
		new_tail = (struct he_rbp *) ((unsigned long)he_dev->rbpl_base |
						RBPL_MASK(he_dev->rbpl_tail+1));

		/* table 3.42 -- rbpl_tail should never be set to rbpl_head */
		if (new_tail == rbpl_head)
			break;

		i = find_next_zero_bit(he_dev->rbpl_table, RBPL_TABLE_SIZE, he_dev->rbpl_hint);
		if (i > (RBPL_TABLE_SIZE - 1)) {
			i = find_first_zero_bit(he_dev->rbpl_table, RBPL_TABLE_SIZE);
			if (i > (RBPL_TABLE_SIZE - 1))
				break;
		}
		he_dev->rbpl_hint = i + 1;

		heb = dma_pool_alloc(he_dev->rbpl_pool, GFP_ATOMIC, &mapping);
		if (!heb)
			break;
		heb->mapping = mapping;
		list_add(&heb->entry, &he_dev->rbpl_outstanding);
		he_dev->rbpl_virt[i] = heb;
		set_bit(i, he_dev->rbpl_table);
		new_tail->idx = i << RBP_IDX_OFFSET;
		new_tail->phys = mapping + offsetof(struct he_buff, data);

		he_dev->rbpl_tail = new_tail;
		++moved;
	} 

	if (moved)
		he_writel(he_dev, RBPL_MASK(he_dev->rbpl_tail), G0_RBPL_T);
}

static void
he_tasklet(unsigned long data)
{
	unsigned long flags;
	struct he_dev *he_dev = (struct he_dev *) data;
	int group, type;
	int updated = 0;

	HPRINTK("tasklet (0x%lx)\n", data);
	spin_lock_irqsave(&he_dev->global_lock, flags);

	while (he_dev->irq_head != he_dev->irq_tail) {
		++updated;

		type = ITYPE_TYPE(he_dev->irq_head->isw);
		group = ITYPE_GROUP(he_dev->irq_head->isw);

		switch (type) {
			case ITYPE_RBRQ_THRESH:
				HPRINTK("rbrq%d threshold\n", group);
				/* fall through */
			case ITYPE_RBRQ_TIMER:
				if (he_service_rbrq(he_dev, group))
					he_service_rbpl(he_dev, group);
				break;
			case ITYPE_TBRQ_THRESH:
				HPRINTK("tbrq%d threshold\n", group);
				/* fall through */
			case ITYPE_TPD_COMPLETE:
				he_service_tbrq(he_dev, group);
				break;
			case ITYPE_RBPL_THRESH:
				he_service_rbpl(he_dev, group);
				break;
			case ITYPE_RBPS_THRESH:
				/* shouldn't happen unless small buffers enabled */
				break;
			case ITYPE_PHY:
				HPRINTK("phy interrupt\n");
#ifdef CONFIG_ATM_HE_USE_SUNI
				spin_unlock_irqrestore(&he_dev->global_lock, flags);
				if (he_dev->atm_dev->phy && he_dev->atm_dev->phy->interrupt)
					he_dev->atm_dev->phy->interrupt(he_dev->atm_dev);
				spin_lock_irqsave(&he_dev->global_lock, flags);
#endif
				break;
			case ITYPE_OTHER:
				switch (type|group) {
					case ITYPE_PARITY:
						hprintk("parity error\n");
						break;
					case ITYPE_ABORT:
						hprintk("abort 0x%x\n", he_readl(he_dev, ABORT_ADDR));
						break;
				}
				break;
			case ITYPE_TYPE(ITYPE_INVALID):
				/* see 8.1.1 -- check all queues */

				HPRINTK("isw not updated 0x%x\n", he_dev->irq_head->isw);

				he_service_rbrq(he_dev, 0);
				he_service_rbpl(he_dev, 0);
				he_service_tbrq(he_dev, 0);
				break;
			default:
				hprintk("bad isw 0x%x?\n", he_dev->irq_head->isw);
		}

		he_dev->irq_head->isw = ITYPE_INVALID;

		he_dev->irq_head = (struct he_irq *) NEXT_ENTRY(he_dev->irq_base, he_dev->irq_head, IRQ_MASK);
	}

	if (updated) {
		if (updated > he_dev->irq_peak)
			he_dev->irq_peak = updated;

		he_writel(he_dev,
			IRQ_SIZE(CONFIG_IRQ_SIZE) |
			IRQ_THRESH(CONFIG_IRQ_THRESH) |
			IRQ_TAIL(he_dev->irq_tail), IRQ0_HEAD);
		(void) he_readl(he_dev, INT_FIFO); /* 8.1.2 controller errata; flush posted writes */
	}
	spin_unlock_irqrestore(&he_dev->global_lock, flags);
}

static irqreturn_t
he_irq_handler(int irq, void *dev_id)
{
	unsigned long flags;
	struct he_dev *he_dev = (struct he_dev * )dev_id;
	int handled = 0;

	if (he_dev == NULL)
		return IRQ_NONE;

	spin_lock_irqsave(&he_dev->global_lock, flags);

	he_dev->irq_tail = (struct he_irq *) (((unsigned long)he_dev->irq_base) |
						(*he_dev->irq_tailoffset << 2));

	if (he_dev->irq_tail == he_dev->irq_head) {
		HPRINTK("tailoffset not updated?\n");
		he_dev->irq_tail = (struct he_irq *) ((unsigned long)he_dev->irq_base |
			((he_readl(he_dev, IRQ0_BASE) & IRQ_MASK) << 2));
		(void) he_readl(he_dev, INT_FIFO);	/* 8.1.2 controller errata */
	}

#ifdef DEBUG
	if (he_dev->irq_head == he_dev->irq_tail /* && !IRQ_PENDING */)
		hprintk("spurious (or shared) interrupt?\n");
#endif

	if (he_dev->irq_head != he_dev->irq_tail) {
		handled = 1;
		tasklet_schedule(&he_dev->tasklet);
		he_writel(he_dev, INT_CLEAR_A, INT_FIFO);	/* clear interrupt */
		(void) he_readl(he_dev, INT_FIFO);		/* flush posted writes */
	}
	spin_unlock_irqrestore(&he_dev->global_lock, flags);
	return IRQ_RETVAL(handled);

}

static __inline__ void
__enqueue_tpd(struct he_dev *he_dev, struct he_tpd *tpd, unsigned cid)
{
	struct he_tpdrq *new_tail;

	HPRINTK("tpdrq %p cid 0x%x -> tpdrq_tail %p\n",
					tpd, cid, he_dev->tpdrq_tail);

	/* new_tail = he_dev->tpdrq_tail; */
	new_tail = (struct he_tpdrq *) ((unsigned long) he_dev->tpdrq_base |
					TPDRQ_MASK(he_dev->tpdrq_tail+1));

	/*
	 * check to see if we are about to set the tail == head
	 * if true, update the head pointer from the adapter
	 * to see if this is really the case (reading the queue
	 * head for every enqueue would be unnecessarily slow)
	 */

	if (new_tail == he_dev->tpdrq_head) {
		he_dev->tpdrq_head = (struct he_tpdrq *)
			(((unsigned long)he_dev->tpdrq_base) |
				TPDRQ_MASK(he_readl(he_dev, TPDRQ_B_H)));

		if (new_tail == he_dev->tpdrq_head) {
			int slot;

			hprintk("tpdrq full (cid 0x%x)\n", cid);
			/*
			 * FIXME
			 * push tpd onto a transmit backlog queue
			 * after service_tbrq, service the backlog
			 * for now, we just drop the pdu
			 */
			for (slot = 0; slot < TPD_MAXIOV; ++slot) {
				if (tpd->iovec[slot].addr)
					dma_unmap_single(&he_dev->pci_dev->dev,
						tpd->iovec[slot].addr,
						tpd->iovec[slot].len & TPD_LEN_MASK,
								DMA_TO_DEVICE);
			}
			if (tpd->skb) {
				if (tpd->vcc->pop)
					tpd->vcc->pop(tpd->vcc, tpd->skb);
				else
					dev_kfree_skb_any(tpd->skb);
				atomic_inc(&tpd->vcc->stats->tx_err);
			}
			dma_pool_free(he_dev->tpd_pool, tpd, TPD_ADDR(tpd->status));
			return;
		}
	}

	/* 2.1.5 transmit packet descriptor ready queue */
	list_add_tail(&tpd->entry, &he_dev->outstanding_tpds);
	he_dev->tpdrq_tail->tpd = TPD_ADDR(tpd->status);
	he_dev->tpdrq_tail->cid = cid;
	wmb();

	he_dev->tpdrq_tail = new_tail;

	he_writel(he_dev, TPDRQ_MASK(he_dev->tpdrq_tail), TPDRQ_T);
	(void) he_readl(he_dev, TPDRQ_T);		/* flush posted writes */
}

static int
he_open(struct atm_vcc *vcc)
{
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(vcc->dev);
	struct he_vcc *he_vcc;
	int err = 0;
	unsigned cid, rsr0, rsr1, rsr4, tsr0, tsr0_aal, tsr4, period, reg, clock;
	short vpi = vcc->vpi;
	int vci = vcc->vci;

	if (vci == ATM_VCI_UNSPEC || vpi == ATM_VPI_UNSPEC)
		return 0;

	HPRINTK("open vcc %p %d.%d\n", vcc, vpi, vci);

	set_bit(ATM_VF_ADDR, &vcc->flags);

	cid = he_mkcid(he_dev, vpi, vci);

	he_vcc = kmalloc(sizeof(struct he_vcc), GFP_ATOMIC);
	if (he_vcc == NULL) {
		hprintk("unable to allocate he_vcc during open\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&he_vcc->buffers);
	he_vcc->pdu_len = 0;
	he_vcc->rc_index = -1;

	init_waitqueue_head(&he_vcc->rx_waitq);
	init_waitqueue_head(&he_vcc->tx_waitq);

	vcc->dev_data = he_vcc;

	if (vcc->qos.txtp.traffic_class != ATM_NONE) {
		int pcr_goal;

		pcr_goal = atm_pcr_goal(&vcc->qos.txtp);
		if (pcr_goal == 0)
			pcr_goal = he_dev->atm_dev->link_rate;
		if (pcr_goal < 0)	/* means round down, technically */
			pcr_goal = -pcr_goal;

		HPRINTK("open tx cid 0x%x pcr_goal %d\n", cid, pcr_goal);

		switch (vcc->qos.aal) {
			case ATM_AAL5:
				tsr0_aal = TSR0_AAL5;
				tsr4 = TSR4_AAL5;
				break;
			case ATM_AAL0:
				tsr0_aal = TSR0_AAL0_SDU;
				tsr4 = TSR4_AAL0_SDU;
				break;
			default:
				err = -EINVAL;
				goto open_failed;
		}

		spin_lock_irqsave(&he_dev->global_lock, flags);
		tsr0 = he_readl_tsr0(he_dev, cid);
		spin_unlock_irqrestore(&he_dev->global_lock, flags);

		if (TSR0_CONN_STATE(tsr0) != 0) {
			hprintk("cid 0x%x not idle (tsr0 = 0x%x)\n", cid, tsr0);
			err = -EBUSY;
			goto open_failed;
		}

		switch (vcc->qos.txtp.traffic_class) {
			case ATM_UBR:
				/* 2.3.3.1 open connection ubr */

				tsr0 = TSR0_UBR | TSR0_GROUP(0) | tsr0_aal |
					TSR0_USE_WMIN | TSR0_UPDATE_GER;
				break;

			case ATM_CBR:
				/* 2.3.3.2 open connection cbr */

				/* 8.2.3 cbr scheduler wrap problem -- limit to 90% total link rate */
				if ((he_dev->total_bw + pcr_goal)
					> (he_dev->atm_dev->link_rate * 9 / 10))
				{
					err = -EBUSY;
					goto open_failed;
				}

				spin_lock_irqsave(&he_dev->global_lock, flags);			/* also protects he_dev->cs_stper[] */

				/* find an unused cs_stper register */
				for (reg = 0; reg < HE_NUM_CS_STPER; ++reg)
					if (he_dev->cs_stper[reg].inuse == 0 || 
					    he_dev->cs_stper[reg].pcr == pcr_goal)
							break;

				if (reg == HE_NUM_CS_STPER) {
					err = -EBUSY;
					spin_unlock_irqrestore(&he_dev->global_lock, flags);
					goto open_failed;
				}

				he_dev->total_bw += pcr_goal;

				he_vcc->rc_index = reg;
				++he_dev->cs_stper[reg].inuse;
				he_dev->cs_stper[reg].pcr = pcr_goal;

				clock = he_is622(he_dev) ? 66667000 : 50000000;
				period = clock / pcr_goal;
				
				HPRINTK("rc_index = %d period = %d\n",
								reg, period);

				he_writel_mbox(he_dev, rate_to_atmf(period/2),
							CS_STPER0 + reg);
				spin_unlock_irqrestore(&he_dev->global_lock, flags);

				tsr0 = TSR0_CBR | TSR0_GROUP(0) | tsr0_aal |
							TSR0_RC_INDEX(reg);

				break;
			default:
				err = -EINVAL;
				goto open_failed;
		}

		spin_lock_irqsave(&he_dev->global_lock, flags);

		he_writel_tsr0(he_dev, tsr0, cid);
		he_writel_tsr4(he_dev, tsr4 | 1, cid);
		he_writel_tsr1(he_dev, TSR1_MCR(rate_to_atmf(0)) |
					TSR1_PCR(rate_to_atmf(pcr_goal)), cid);
		he_writel_tsr2(he_dev, TSR2_ACR(rate_to_atmf(pcr_goal)), cid);
		he_writel_tsr9(he_dev, TSR9_OPEN_CONN, cid);

		he_writel_tsr3(he_dev, 0x0, cid);
		he_writel_tsr5(he_dev, 0x0, cid);
		he_writel_tsr6(he_dev, 0x0, cid);
		he_writel_tsr7(he_dev, 0x0, cid);
		he_writel_tsr8(he_dev, 0x0, cid);
		he_writel_tsr10(he_dev, 0x0, cid);
		he_writel_tsr11(he_dev, 0x0, cid);
		he_writel_tsr12(he_dev, 0x0, cid);
		he_writel_tsr13(he_dev, 0x0, cid);
		he_writel_tsr14(he_dev, 0x0, cid);
		(void) he_readl_tsr0(he_dev, cid);		/* flush posted writes */
		spin_unlock_irqrestore(&he_dev->global_lock, flags);
	}

	if (vcc->qos.rxtp.traffic_class != ATM_NONE) {
		unsigned aal;

		HPRINTK("open rx cid 0x%x (rx_waitq %p)\n", cid,
		 				&HE_VCC(vcc)->rx_waitq);

		switch (vcc->qos.aal) {
			case ATM_AAL5:
				aal = RSR0_AAL5;
				break;
			case ATM_AAL0:
				aal = RSR0_RAWCELL;
				break;
			default:
				err = -EINVAL;
				goto open_failed;
		}

		spin_lock_irqsave(&he_dev->global_lock, flags);

		rsr0 = he_readl_rsr0(he_dev, cid);
		if (rsr0 & RSR0_OPEN_CONN) {
			spin_unlock_irqrestore(&he_dev->global_lock, flags);

			hprintk("cid 0x%x not idle (rsr0 = 0x%x)\n", cid, rsr0);
			err = -EBUSY;
			goto open_failed;
		}

		rsr1 = RSR1_GROUP(0) | RSR1_RBPL_ONLY;
		rsr4 = RSR4_GROUP(0) | RSR4_RBPL_ONLY;
		rsr0 = vcc->qos.rxtp.traffic_class == ATM_UBR ? 
				(RSR0_EPD_ENABLE|RSR0_PPD_ENABLE) : 0;

#ifdef USE_CHECKSUM_HW
		if (vpi == 0 && vci >= ATM_NOT_RSV_VCI)
			rsr0 |= RSR0_TCP_CKSUM;
#endif

		he_writel_rsr4(he_dev, rsr4, cid);
		he_writel_rsr1(he_dev, rsr1, cid);
		/* 5.1.11 last parameter initialized should be
			  the open/closed indication in rsr0 */
		he_writel_rsr0(he_dev,
			rsr0 | RSR0_START_PDU | RSR0_OPEN_CONN | aal, cid);
		(void) he_readl_rsr0(he_dev, cid);		/* flush posted writes */

		spin_unlock_irqrestore(&he_dev->global_lock, flags);
	}

open_failed:

	if (err) {
		kfree(he_vcc);
		clear_bit(ATM_VF_ADDR, &vcc->flags);
	}
	else
		set_bit(ATM_VF_READY, &vcc->flags);

	return err;
}

static void
he_close(struct atm_vcc *vcc)
{
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);
	struct he_dev *he_dev = HE_DEV(vcc->dev);
	struct he_tpd *tpd;
	unsigned cid;
	struct he_vcc *he_vcc = HE_VCC(vcc);
#define MAX_RETRY 30
	int retry = 0, sleep = 1, tx_inuse;

	HPRINTK("close vcc %p %d.%d\n", vcc, vcc->vpi, vcc->vci);

	clear_bit(ATM_VF_READY, &vcc->flags);
	cid = he_mkcid(he_dev, vcc->vpi, vcc->vci);

	if (vcc->qos.rxtp.traffic_class != ATM_NONE) {
		int timeout;

		HPRINTK("close rx cid 0x%x\n", cid);

		/* 2.7.2.2 close receive operation */

		/* wait for previous close (if any) to finish */

		spin_lock_irqsave(&he_dev->global_lock, flags);
		while (he_readl(he_dev, RCC_STAT) & RCC_BUSY) {
			HPRINTK("close cid 0x%x RCC_BUSY\n", cid);
			udelay(250);
		}

		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&he_vcc->rx_waitq, &wait);

		he_writel_rsr0(he_dev, RSR0_CLOSE_CONN, cid);
		(void) he_readl_rsr0(he_dev, cid);		/* flush posted writes */
		he_writel_mbox(he_dev, cid, RXCON_CLOSE);
		spin_unlock_irqrestore(&he_dev->global_lock, flags);

		timeout = schedule_timeout(30*HZ);

		remove_wait_queue(&he_vcc->rx_waitq, &wait);
		set_current_state(TASK_RUNNING);

		if (timeout == 0)
			hprintk("close rx timeout cid 0x%x\n", cid);

		HPRINTK("close rx cid 0x%x complete\n", cid);

	}

	if (vcc->qos.txtp.traffic_class != ATM_NONE) {
		volatile unsigned tsr4, tsr0;
		int timeout;

		HPRINTK("close tx cid 0x%x\n", cid);
		
		/* 2.1.2
		 *
		 * ... the host must first stop queueing packets to the TPDRQ
		 * on the connection to be closed, then wait for all outstanding
		 * packets to be transmitted and their buffers returned to the
		 * TBRQ. When the last packet on the connection arrives in the
		 * TBRQ, the host issues the close command to the adapter.
		 */

		while (((tx_inuse = atomic_read(&sk_atm(vcc)->sk_wmem_alloc)) > 1) &&
		       (retry < MAX_RETRY)) {
			msleep(sleep);
			if (sleep < 250)
				sleep = sleep * 2;

			++retry;
		}

		if (tx_inuse > 1)
			hprintk("close tx cid 0x%x tx_inuse = %d\n", cid, tx_inuse);

		/* 2.3.1.1 generic close operations with flush */

		spin_lock_irqsave(&he_dev->global_lock, flags);
		he_writel_tsr4_upper(he_dev, TSR4_FLUSH_CONN, cid);
					/* also clears TSR4_SESSION_ENDED */

		switch (vcc->qos.txtp.traffic_class) {
			case ATM_UBR:
				he_writel_tsr1(he_dev, 
					TSR1_MCR(rate_to_atmf(200000))
					| TSR1_PCR(0), cid);
				break;
			case ATM_CBR:
				he_writel_tsr14_upper(he_dev, TSR14_DELETE, cid);
				break;
		}
		(void) he_readl_tsr4(he_dev, cid);		/* flush posted writes */

		tpd = __alloc_tpd(he_dev);
		if (tpd == NULL) {
			hprintk("close tx he_alloc_tpd failed cid 0x%x\n", cid);
			goto close_tx_incomplete;
		}
		tpd->status |= TPD_EOS | TPD_INT;
		tpd->skb = NULL;
		tpd->vcc = vcc;
		wmb();

		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&he_vcc->tx_waitq, &wait);
		__enqueue_tpd(he_dev, tpd, cid);
		spin_unlock_irqrestore(&he_dev->global_lock, flags);

		timeout = schedule_timeout(30*HZ);

		remove_wait_queue(&he_vcc->tx_waitq, &wait);
		set_current_state(TASK_RUNNING);

		spin_lock_irqsave(&he_dev->global_lock, flags);

		if (timeout == 0) {
			hprintk("close tx timeout cid 0x%x\n", cid);
			goto close_tx_incomplete;
		}

		while (!((tsr4 = he_readl_tsr4(he_dev, cid)) & TSR4_SESSION_ENDED)) {
			HPRINTK("close tx cid 0x%x !TSR4_SESSION_ENDED (tsr4 = 0x%x)\n", cid, tsr4);
			udelay(250);
		}

		while (TSR0_CONN_STATE(tsr0 = he_readl_tsr0(he_dev, cid)) != 0) {
			HPRINTK("close tx cid 0x%x TSR0_CONN_STATE != 0 (tsr0 = 0x%x)\n", cid, tsr0);
			udelay(250);
		}

close_tx_incomplete:

		if (vcc->qos.txtp.traffic_class == ATM_CBR) {
			int reg = he_vcc->rc_index;

			HPRINTK("cs_stper reg = %d\n", reg);

			if (he_dev->cs_stper[reg].inuse == 0)
				hprintk("cs_stper[%d].inuse = 0!\n", reg);
			else
				--he_dev->cs_stper[reg].inuse;

			he_dev->total_bw -= he_dev->cs_stper[reg].pcr;
		}
		spin_unlock_irqrestore(&he_dev->global_lock, flags);

		HPRINTK("close tx cid 0x%x complete\n", cid);
	}

	kfree(he_vcc);

	clear_bit(ATM_VF_ADDR, &vcc->flags);
}

static int
he_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(vcc->dev);
	unsigned cid = he_mkcid(he_dev, vcc->vpi, vcc->vci);
	struct he_tpd *tpd;
#ifdef USE_SCATTERGATHER
	int i, slot = 0;
#endif

#define HE_TPD_BUFSIZE 0xffff

	HPRINTK("send %d.%d\n", vcc->vpi, vcc->vci);

	if ((skb->len > HE_TPD_BUFSIZE) ||
	    ((vcc->qos.aal == ATM_AAL0) && (skb->len != ATM_AAL0_SDU))) {
		hprintk("buffer too large (or small) -- %d bytes\n", skb->len );
		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb_any(skb);
		atomic_inc(&vcc->stats->tx_err);
		return -EINVAL;
	}

#ifndef USE_SCATTERGATHER
	if (skb_shinfo(skb)->nr_frags) {
		hprintk("no scatter/gather support\n");
		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb_any(skb);
		atomic_inc(&vcc->stats->tx_err);
		return -EINVAL;
	}
#endif
	spin_lock_irqsave(&he_dev->global_lock, flags);

	tpd = __alloc_tpd(he_dev);
	if (tpd == NULL) {
		if (vcc->pop)
			vcc->pop(vcc, skb);
		else
			dev_kfree_skb_any(skb);
		atomic_inc(&vcc->stats->tx_err);
		spin_unlock_irqrestore(&he_dev->global_lock, flags);
		return -ENOMEM;
	}

	if (vcc->qos.aal == ATM_AAL5)
		tpd->status |= TPD_CELLTYPE(TPD_USERCELL);
	else {
		char *pti_clp = (void *) (skb->data + 3);
		int clp, pti;

		pti = (*pti_clp & ATM_HDR_PTI_MASK) >> ATM_HDR_PTI_SHIFT; 
		clp = (*pti_clp & ATM_HDR_CLP);
		tpd->status |= TPD_CELLTYPE(pti);
		if (clp)
			tpd->status |= TPD_CLP;

		skb_pull(skb, ATM_AAL0_SDU - ATM_CELL_PAYLOAD);
	}

#ifdef USE_SCATTERGATHER
	tpd->iovec[slot].addr = dma_map_single(&he_dev->pci_dev->dev, skb->data,
				skb_headlen(skb), DMA_TO_DEVICE);
	tpd->iovec[slot].len = skb_headlen(skb);
	++slot;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		if (slot == TPD_MAXIOV) {	/* queue tpd; start new tpd */
			tpd->vcc = vcc;
			tpd->skb = NULL;	/* not the last fragment
						   so dont ->push() yet */
			wmb();

			__enqueue_tpd(he_dev, tpd, cid);
			tpd = __alloc_tpd(he_dev);
			if (tpd == NULL) {
				if (vcc->pop)
					vcc->pop(vcc, skb);
				else
					dev_kfree_skb_any(skb);
				atomic_inc(&vcc->stats->tx_err);
				spin_unlock_irqrestore(&he_dev->global_lock, flags);
				return -ENOMEM;
			}
			tpd->status |= TPD_USERCELL;
			slot = 0;
		}

		tpd->iovec[slot].addr = dma_map_single(&he_dev->pci_dev->dev,
			(void *) page_address(frag->page) + frag->page_offset,
				frag->size, DMA_TO_DEVICE);
		tpd->iovec[slot].len = frag->size;
		++slot;

	}

	tpd->iovec[slot - 1].len |= TPD_LST;
#else
	tpd->address0 = dma_map_single(&he_dev->pci_dev->dev, skb->data, skb->len, DMA_TO_DEVICE);
	tpd->length0 = skb->len | TPD_LST;
#endif
	tpd->status |= TPD_INT;

	tpd->vcc = vcc;
	tpd->skb = skb;
	wmb();
	ATM_SKB(skb)->vcc = vcc;

	__enqueue_tpd(he_dev, tpd, cid);
	spin_unlock_irqrestore(&he_dev->global_lock, flags);

	atomic_inc(&vcc->stats->tx);

	return 0;
}

static int
he_ioctl(struct atm_dev *atm_dev, unsigned int cmd, void __user *arg)
{
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(atm_dev);
	struct he_ioctl_reg reg;
	int err = 0;

	switch (cmd) {
		case HE_GET_REG:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;

			if (copy_from_user(&reg, arg,
					   sizeof(struct he_ioctl_reg)))
				return -EFAULT;

			spin_lock_irqsave(&he_dev->global_lock, flags);
			switch (reg.type) {
				case HE_REGTYPE_PCI:
					if (reg.addr >= HE_REGMAP_SIZE) {
						err = -EINVAL;
						break;
					}

					reg.val = he_readl(he_dev, reg.addr);
					break;
				case HE_REGTYPE_RCM:
					reg.val =
						he_readl_rcm(he_dev, reg.addr);
					break;
				case HE_REGTYPE_TCM:
					reg.val =
						he_readl_tcm(he_dev, reg.addr);
					break;
				case HE_REGTYPE_MBOX:
					reg.val =
						he_readl_mbox(he_dev, reg.addr);
					break;
				default:
					err = -EINVAL;
					break;
			}
			spin_unlock_irqrestore(&he_dev->global_lock, flags);
			if (err == 0)
				if (copy_to_user(arg, &reg,
							sizeof(struct he_ioctl_reg)))
					return -EFAULT;
			break;
		default:
#ifdef CONFIG_ATM_HE_USE_SUNI
			if (atm_dev->phy && atm_dev->phy->ioctl)
				err = atm_dev->phy->ioctl(atm_dev, cmd, arg);
#else /* CONFIG_ATM_HE_USE_SUNI */
			err = -EINVAL;
#endif /* CONFIG_ATM_HE_USE_SUNI */
			break;
	}

	return err;
}

static void
he_phy_put(struct atm_dev *atm_dev, unsigned char val, unsigned long addr)
{
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(atm_dev);

	HPRINTK("phy_put(val 0x%x, addr 0x%lx)\n", val, addr);

	spin_lock_irqsave(&he_dev->global_lock, flags);
	he_writel(he_dev, val, FRAMER + (addr*4));
	(void) he_readl(he_dev, FRAMER + (addr*4));		/* flush posted writes */
	spin_unlock_irqrestore(&he_dev->global_lock, flags);
}
 
	
static unsigned char
he_phy_get(struct atm_dev *atm_dev, unsigned long addr)
{ 
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(atm_dev);
	unsigned reg;

	spin_lock_irqsave(&he_dev->global_lock, flags);
	reg = he_readl(he_dev, FRAMER + (addr*4));
	spin_unlock_irqrestore(&he_dev->global_lock, flags);

	HPRINTK("phy_get(addr 0x%lx) =0x%x\n", addr, reg);
	return reg;
}

static int
he_proc_read(struct atm_dev *dev, loff_t *pos, char *page)
{
	unsigned long flags;
	struct he_dev *he_dev = HE_DEV(dev);
	int left, i;
#ifdef notdef
	struct he_rbrq *rbrq_tail;
	struct he_tpdrq *tpdrq_head;
	int rbpl_head, rbpl_tail;
#endif
	static long mcc = 0, oec = 0, dcc = 0, cec = 0;


	left = *pos;
	if (!left--)
		return sprintf(page, "ATM he driver\n");

	if (!left--)
		return sprintf(page, "%s%s\n\n",
			he_dev->prod_id, he_dev->media & 0x40 ? "SM" : "MM");

	if (!left--)
		return sprintf(page, "Mismatched Cells  VPI/VCI Not Open  Dropped Cells  RCM Dropped Cells\n");

	spin_lock_irqsave(&he_dev->global_lock, flags);
	mcc += he_readl(he_dev, MCC);
	oec += he_readl(he_dev, OEC);
	dcc += he_readl(he_dev, DCC);
	cec += he_readl(he_dev, CEC);
	spin_unlock_irqrestore(&he_dev->global_lock, flags);

	if (!left--)
		return sprintf(page, "%16ld  %16ld  %13ld  %17ld\n\n", 
							mcc, oec, dcc, cec);

	if (!left--)
		return sprintf(page, "irq_size = %d  inuse = ?  peak = %d\n",
				CONFIG_IRQ_SIZE, he_dev->irq_peak);

	if (!left--)
		return sprintf(page, "tpdrq_size = %d  inuse = ?\n",
						CONFIG_TPDRQ_SIZE);

	if (!left--)
		return sprintf(page, "rbrq_size = %d  inuse = ?  peak = %d\n",
				CONFIG_RBRQ_SIZE, he_dev->rbrq_peak);

	if (!left--)
		return sprintf(page, "tbrq_size = %d  peak = %d\n",
					CONFIG_TBRQ_SIZE, he_dev->tbrq_peak);


#ifdef notdef
	rbpl_head = RBPL_MASK(he_readl(he_dev, G0_RBPL_S));
	rbpl_tail = RBPL_MASK(he_readl(he_dev, G0_RBPL_T));

	inuse = rbpl_head - rbpl_tail;
	if (inuse < 0)
		inuse += CONFIG_RBPL_SIZE * sizeof(struct he_rbp);
	inuse /= sizeof(struct he_rbp);

	if (!left--)
		return sprintf(page, "rbpl_size = %d  inuse = %d\n\n",
						CONFIG_RBPL_SIZE, inuse);
#endif

	if (!left--)
		return sprintf(page, "rate controller periods (cbr)\n                 pcr  #vc\n");

	for (i = 0; i < HE_NUM_CS_STPER; ++i)
		if (!left--)
			return sprintf(page, "cs_stper%-2d  %8ld  %3d\n", i,
						he_dev->cs_stper[i].pcr,
						he_dev->cs_stper[i].inuse);

	if (!left--)
		return sprintf(page, "total bw (cbr): %d  (limit %d)\n",
			he_dev->total_bw, he_dev->atm_dev->link_rate * 10 / 9);

	return 0;
}

/* eeprom routines  -- see 4.7 */

static u8 read_prom_byte(struct he_dev *he_dev, int addr)
{
	u32 val = 0, tmp_read = 0;
	int i, j = 0;
	u8 byte_read = 0;

	val = readl(he_dev->membase + HOST_CNTL);
	val &= 0xFFFFE0FF;
       
	/* Turn on write enable */
	val |= 0x800;
	he_writel(he_dev, val, HOST_CNTL);
       
	/* Send READ instruction */
	for (i = 0; i < ARRAY_SIZE(readtab); i++) {
		he_writel(he_dev, val | readtab[i], HOST_CNTL);
		udelay(EEPROM_DELAY);
	}
       
	/* Next, we need to send the byte address to read from */
	for (i = 7; i >= 0; i--) {
		he_writel(he_dev, val | clocktab[j++] | (((addr >> i) & 1) << 9), HOST_CNTL);
		udelay(EEPROM_DELAY);
		he_writel(he_dev, val | clocktab[j++] | (((addr >> i) & 1) << 9), HOST_CNTL);
		udelay(EEPROM_DELAY);
	}
       
	j = 0;

	val &= 0xFFFFF7FF;      /* Turn off write enable */
	he_writel(he_dev, val, HOST_CNTL);
       
	/* Now, we can read data from the EEPROM by clocking it in */
	for (i = 7; i >= 0; i--) {
		he_writel(he_dev, val | clocktab[j++], HOST_CNTL);
		udelay(EEPROM_DELAY);
		tmp_read = he_readl(he_dev, HOST_CNTL);
		byte_read |= (unsigned char)
			   ((tmp_read & ID_DOUT) >> ID_DOFFSET << i);
		he_writel(he_dev, val | clocktab[j++], HOST_CNTL);
		udelay(EEPROM_DELAY);
	}
       
	he_writel(he_dev, val | ID_CS, HOST_CNTL);
	udelay(EEPROM_DELAY);

	return byte_read;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chas williams <chas@cmf.nrl.navy.mil>");
MODULE_DESCRIPTION("ForeRunnerHE ATM Adapter driver");
module_param(disable64, bool, 0);
MODULE_PARM_DESC(disable64, "disable 64-bit pci bus transfers");
module_param(nvpibits, short, 0);
MODULE_PARM_DESC(nvpibits, "numbers of bits for vpi (default 0)");
module_param(nvcibits, short, 0);
MODULE_PARM_DESC(nvcibits, "numbers of bits for vci (default 12)");
module_param(rx_skb_reserve, short, 0);
MODULE_PARM_DESC(rx_skb_reserve, "padding for receive skb (default 16)");
module_param(irq_coalesce, bool, 0);
MODULE_PARM_DESC(irq_coalesce, "use interrupt coalescing (default 1)");
module_param(sdh, bool, 0);
MODULE_PARM_DESC(sdh, "use SDH framing (default 0)");

static struct pci_device_id he_pci_tbl[] = {
	{ PCI_VDEVICE(FORE, PCI_DEVICE_ID_FORE_HE), 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, he_pci_tbl);

static struct pci_driver he_driver = {
	.name =		"he",
	.probe =	he_init_one,
	.remove =	he_remove_one,
	.id_table =	he_pci_tbl,
};

module_pci_driver(he_driver);
