/*
 * Alchemy Semi Au1000 IrDA driver
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>
#include <asm/mach-au1x00/au1000.h>

/* registers */
#define IR_RING_PTR_STATUS	0x00
#define IR_RING_BASE_ADDR_H	0x04
#define IR_RING_BASE_ADDR_L	0x08
#define IR_RING_SIZE		0x0C
#define IR_RING_PROMPT		0x10
#define IR_RING_ADDR_CMPR	0x14
#define IR_INT_CLEAR		0x18
#define IR_CONFIG_1		0x20
#define IR_SIR_FLAGS		0x24
#define IR_STATUS		0x28
#define IR_READ_PHY_CONFIG	0x2C
#define IR_WRITE_PHY_CONFIG	0x30
#define IR_MAX_PKT_LEN		0x34
#define IR_RX_BYTE_CNT		0x38
#define IR_CONFIG_2		0x3C
#define IR_ENABLE		0x40

/* Config1 */
#define IR_RX_INVERT_LED	(1 << 0)
#define IR_TX_INVERT_LED	(1 << 1)
#define IR_ST			(1 << 2)
#define IR_SF			(1 << 3)
#define IR_SIR			(1 << 4)
#define IR_MIR			(1 << 5)
#define IR_FIR			(1 << 6)
#define IR_16CRC		(1 << 7)
#define IR_TD			(1 << 8)
#define IR_RX_ALL		(1 << 9)
#define IR_DMA_ENABLE		(1 << 10)
#define IR_RX_ENABLE		(1 << 11)
#define IR_TX_ENABLE		(1 << 12)
#define IR_LOOPBACK		(1 << 14)
#define IR_SIR_MODE		(IR_SIR | IR_DMA_ENABLE | \
				 IR_RX_ALL | IR_RX_ENABLE | IR_SF | \
				 IR_16CRC)

/* ir_status */
#define IR_RX_STATUS		(1 << 9)
#define IR_TX_STATUS		(1 << 10)
#define IR_PHYEN		(1 << 15)

/* ir_write_phy_config */
#define IR_BR(x)		(((x) & 0x3f) << 10)	/* baud rate */
#define IR_PW(x)		(((x) & 0x1f) << 5)	/* pulse width */
#define IR_P(x)			((x) & 0x1f)		/* preamble bits */

/* Config2 */
#define IR_MODE_INV		(1 << 0)
#define IR_ONE_PIN		(1 << 1)
#define IR_PHYCLK_40MHZ		(0 << 2)
#define IR_PHYCLK_48MHZ		(1 << 2)
#define IR_PHYCLK_56MHZ		(2 << 2)
#define IR_PHYCLK_64MHZ		(3 << 2)
#define IR_DP			(1 << 4)
#define IR_DA			(1 << 5)
#define IR_FLT_HIGH		(0 << 6)
#define IR_FLT_MEDHI		(1 << 6)
#define IR_FLT_MEDLO		(2 << 6)
#define IR_FLT_LO		(3 << 6)
#define IR_IEN			(1 << 8)

/* ir_enable */
#define IR_HC			(1 << 3)	/* divide SBUS clock by 2 */
#define IR_CE			(1 << 2)	/* clock enable */
#define IR_C			(1 << 1)	/* coherency bit */
#define IR_BE			(1 << 0)	/* set in big endian mode */

#define NUM_IR_DESC	64
#define RING_SIZE_4	0x0
#define RING_SIZE_16	0x3
#define RING_SIZE_64	0xF
#define MAX_NUM_IR_DESC	64
#define MAX_BUF_SIZE	2048

/* Ring descriptor flags */
#define AU_OWN		(1 << 7) /* tx,rx */
#define IR_DIS_CRC	(1 << 6) /* tx */
#define IR_BAD_CRC	(1 << 5) /* tx */
#define IR_NEED_PULSE	(1 << 4) /* tx */
#define IR_FORCE_UNDER	(1 << 3) /* tx */
#define IR_DISABLE_TX	(1 << 2) /* tx */
#define IR_HW_UNDER	(1 << 0) /* tx */
#define IR_TX_ERROR	(IR_DIS_CRC | IR_BAD_CRC | IR_HW_UNDER)

#define IR_PHY_ERROR	(1 << 6) /* rx */
#define IR_CRC_ERROR	(1 << 5) /* rx */
#define IR_MAX_LEN	(1 << 4) /* rx */
#define IR_FIFO_OVER	(1 << 3) /* rx */
#define IR_SIR_ERROR	(1 << 2) /* rx */
#define IR_RX_ERROR	(IR_PHY_ERROR | IR_CRC_ERROR | \
			 IR_MAX_LEN | IR_FIFO_OVER | IR_SIR_ERROR)

struct db_dest {
	struct db_dest *pnext;
	volatile u32 *vaddr;
	dma_addr_t dma_addr;
};

struct ring_dest {
	u8 count_0;	/* 7:0  */
	u8 count_1;	/* 12:8 */
	u8 reserved;
	u8 flags;
	u8 addr_0;	/* 7:0   */
	u8 addr_1;	/* 15:8  */
	u8 addr_2;	/* 23:16 */
	u8 addr_3;	/* 31:24 */
};

/* Private data for each instance */
struct au1k_private {
	void __iomem *iobase;
	int irq_rx, irq_tx;

	struct db_dest *pDBfree;
	struct db_dest db[2 * NUM_IR_DESC];
	volatile struct ring_dest *rx_ring[NUM_IR_DESC];
	volatile struct ring_dest *tx_ring[NUM_IR_DESC];
	struct db_dest *rx_db_inuse[NUM_IR_DESC];
	struct db_dest *tx_db_inuse[NUM_IR_DESC];
	u32 rx_head;
	u32 tx_head;
	u32 tx_tail;
	u32 tx_full;

	iobuff_t rx_buff;

	struct net_device *netdev;
	struct timeval stamp;
	struct timeval now;
	struct qos_info qos;
	struct irlap_cb *irlap;

	u8 open;
	u32 speed;
	u32 newspeed;

	struct timer_list timer;

	struct resource *ioarea;
	struct au1k_irda_platform_data *platdata;
};

static int qos_mtt_bits = 0x07;  /* 1 ms or more */

#define RUN_AT(x) (jiffies + (x))

static void au1k_irda_plat_set_phy_mode(struct au1k_private *p, int mode)
{
	if (p->platdata && p->platdata->set_phy_mode)
		p->platdata->set_phy_mode(mode);
}

static inline unsigned long irda_read(struct au1k_private *p,
				      unsigned long ofs)
{
	/*
	* IrDA peripheral bug. You have to read the register
	* twice to get the right value.
	*/
	(void)__raw_readl(p->iobase + ofs);
	return __raw_readl(p->iobase + ofs);
}

static inline void irda_write(struct au1k_private *p, unsigned long ofs,
			      unsigned long val)
{
	__raw_writel(val, p->iobase + ofs);
	wmb();
}

/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for
 * both, receive and transmit operations.
 */
static struct db_dest *GetFreeDB(struct au1k_private *aup)
{
	struct db_dest *db;
	db = aup->pDBfree;

	if (db)
		aup->pDBfree = db->pnext;
	return db;
}

/*
  DMA memory allocation, derived from pci_alloc_consistent.
  However, the Au1000 data cache is coherent (when programmed
  so), therefore we return KSEG0 address, not KSEG1.
*/
static void *dma_alloc(size_t size, dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC | GFP_DMA;

	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
		ret = (void *)KSEG0ADDR(ret);
	}
	return ret;
}

static void dma_free(void *vaddr, size_t size)
{
	vaddr = (void *)KSEG0ADDR(vaddr);
	free_pages((unsigned long) vaddr, get_order(size));
}


static void setup_hw_rings(struct au1k_private *aup, u32 rx_base, u32 tx_base)
{
	int i;
	for (i = 0; i < NUM_IR_DESC; i++) {
		aup->rx_ring[i] = (volatile struct ring_dest *)
			(rx_base + sizeof(struct ring_dest) * i);
	}
	for (i = 0; i < NUM_IR_DESC; i++) {
		aup->tx_ring[i] = (volatile struct ring_dest *)
			(tx_base + sizeof(struct ring_dest) * i);
	}
}

static int au1k_irda_init_iobuf(iobuff_t *io, int size)
{
	io->head = kmalloc(size, GFP_KERNEL);
	if (io->head != NULL) {
		io->truesize	= size;
		io->in_frame	= FALSE;
		io->state	= OUTSIDE_FRAME;
		io->data	= io->head;
	}
	return io->head ? 0 : -ENOMEM;
}

/*
 * Set the IrDA communications speed.
 */
static int au1k_irda_set_speed(struct net_device *dev, int speed)
{
	struct au1k_private *aup = netdev_priv(dev);
	volatile struct ring_dest *ptxd;
	unsigned long control;
	int ret = 0, timeout = 10, i;

	if (speed == aup->speed)
		return ret;

	/* disable PHY first */
	au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_OFF);
	irda_write(aup, IR_STATUS, irda_read(aup, IR_STATUS) & ~IR_PHYEN);

	/* disable RX/TX */
	irda_write(aup, IR_CONFIG_1,
	    irda_read(aup, IR_CONFIG_1) & ~(IR_RX_ENABLE | IR_TX_ENABLE));
	msleep(20);
	while (irda_read(aup, IR_STATUS) & (IR_RX_STATUS | IR_TX_STATUS)) {
		msleep(20);
		if (!timeout--) {
			printk(KERN_ERR "%s: rx/tx disable timeout\n",
					dev->name);
			break;
		}
	}

	/* disable DMA */
	irda_write(aup, IR_CONFIG_1,
		   irda_read(aup, IR_CONFIG_1) & ~IR_DMA_ENABLE);
	msleep(20);

	/* After we disable tx/rx. the index pointers go back to zero. */
	aup->tx_head = aup->tx_tail = aup->rx_head = 0;
	for (i = 0; i < NUM_IR_DESC; i++) {
		ptxd = aup->tx_ring[i];
		ptxd->flags = 0;
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
	}

	for (i = 0; i < NUM_IR_DESC; i++) {
		ptxd = aup->rx_ring[i];
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
		ptxd->flags = AU_OWN;
	}

	if (speed == 4000000)
		au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_FIR);
	else
		au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_SIR);

	switch (speed) {
	case 9600:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_BR(11) | IR_PW(12));
		irda_write(aup, IR_CONFIG_1, IR_SIR_MODE);
		break;
	case 19200:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_BR(5) | IR_PW(12));
		irda_write(aup, IR_CONFIG_1, IR_SIR_MODE);
		break;
	case 38400:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_BR(2) | IR_PW(12));
		irda_write(aup, IR_CONFIG_1, IR_SIR_MODE);
		break;
	case 57600:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_BR(1) | IR_PW(12));
		irda_write(aup, IR_CONFIG_1, IR_SIR_MODE);
		break;
	case 115200:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_PW(12));
		irda_write(aup, IR_CONFIG_1, IR_SIR_MODE);
		break;
	case 4000000:
		irda_write(aup, IR_WRITE_PHY_CONFIG, IR_P(15));
		irda_write(aup, IR_CONFIG_1, IR_FIR | IR_DMA_ENABLE |
				IR_RX_ENABLE);
		break;
	default:
		printk(KERN_ERR "%s unsupported speed %x\n", dev->name, speed);
		ret = -EINVAL;
		break;
	}

	aup->speed = speed;
	irda_write(aup, IR_STATUS, irda_read(aup, IR_STATUS) | IR_PHYEN);

	control = irda_read(aup, IR_STATUS);
	irda_write(aup, IR_RING_PROMPT, 0);

	if (control & (1 << 14)) {
		printk(KERN_ERR "%s: configuration error\n", dev->name);
	} else {
		if (control & (1 << 11))
			printk(KERN_DEBUG "%s Valid SIR config\n", dev->name);
		if (control & (1 << 12))
			printk(KERN_DEBUG "%s Valid MIR config\n", dev->name);
		if (control & (1 << 13))
			printk(KERN_DEBUG "%s Valid FIR config\n", dev->name);
		if (control & (1 << 10))
			printk(KERN_DEBUG "%s TX enabled\n", dev->name);
		if (control & (1 << 9))
			printk(KERN_DEBUG "%s RX enabled\n", dev->name);
	}

	return ret;
}

static void update_rx_stats(struct net_device *dev, u32 status, u32 count)
{
	struct net_device_stats *ps = &dev->stats;

	ps->rx_packets++;

	if (status & IR_RX_ERROR) {
		ps->rx_errors++;
		if (status & (IR_PHY_ERROR | IR_FIFO_OVER))
			ps->rx_missed_errors++;
		if (status & IR_MAX_LEN)
			ps->rx_length_errors++;
		if (status & IR_CRC_ERROR)
			ps->rx_crc_errors++;
	} else
		ps->rx_bytes += count;
}

static void update_tx_stats(struct net_device *dev, u32 status, u32 pkt_len)
{
	struct net_device_stats *ps = &dev->stats;

	ps->tx_packets++;
	ps->tx_bytes += pkt_len;

	if (status & IR_TX_ERROR) {
		ps->tx_errors++;
		ps->tx_aborted_errors++;
	}
}

static void au1k_tx_ack(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	volatile struct ring_dest *ptxd;

	ptxd = aup->tx_ring[aup->tx_tail];
	while (!(ptxd->flags & AU_OWN) && (aup->tx_tail != aup->tx_head)) {
		update_tx_stats(dev, ptxd->flags,
				(ptxd->count_1 << 8) | ptxd->count_0);
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
		wmb();
		aup->tx_tail = (aup->tx_tail + 1) & (NUM_IR_DESC - 1);
		ptxd = aup->tx_ring[aup->tx_tail];

		if (aup->tx_full) {
			aup->tx_full = 0;
			netif_wake_queue(dev);
		}
	}

	if (aup->tx_tail == aup->tx_head) {
		if (aup->newspeed) {
			au1k_irda_set_speed(dev, aup->newspeed);
			aup->newspeed = 0;
		} else {
			irda_write(aup, IR_CONFIG_1,
			    irda_read(aup, IR_CONFIG_1) & ~IR_TX_ENABLE);
			irda_write(aup, IR_CONFIG_1,
			    irda_read(aup, IR_CONFIG_1) | IR_RX_ENABLE);
			irda_write(aup, IR_RING_PROMPT, 0);
		}
	}
}

static int au1k_irda_rx(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	volatile struct ring_dest *prxd;
	struct sk_buff *skb;
	struct db_dest *pDB;
	u32 flags, count;

	prxd = aup->rx_ring[aup->rx_head];
	flags = prxd->flags;

	while (!(flags & AU_OWN))  {
		pDB = aup->rx_db_inuse[aup->rx_head];
		count = (prxd->count_1 << 8) | prxd->count_0;
		if (!(flags & IR_RX_ERROR)) {
			/* good frame */
			update_rx_stats(dev, flags, count);
			skb = alloc_skb(count + 1, GFP_ATOMIC);
			if (skb == NULL) {
				dev->stats.rx_dropped++;
				continue;
			}
			skb_reserve(skb, 1);
			if (aup->speed == 4000000)
				skb_put(skb, count);
			else
				skb_put(skb, count - 2);
			skb_copy_to_linear_data(skb, (void *)pDB->vaddr,
						count - 2);
			skb->dev = dev;
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
			prxd->count_0 = 0;
			prxd->count_1 = 0;
		}
		prxd->flags |= AU_OWN;
		aup->rx_head = (aup->rx_head + 1) & (NUM_IR_DESC - 1);
		irda_write(aup, IR_RING_PROMPT, 0);

		/* next descriptor */
		prxd = aup->rx_ring[aup->rx_head];
		flags = prxd->flags;

	}
	return 0;
}

static irqreturn_t au1k_irda_interrupt(int dummy, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct au1k_private *aup = netdev_priv(dev);

	irda_write(aup, IR_INT_CLEAR, 0); /* ack irda interrupts */

	au1k_irda_rx(dev);
	au1k_tx_ack(dev);

	return IRQ_HANDLED;
}

static int au1k_init(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	u32 enable, ring_address;
	int i;

	enable = IR_HC | IR_CE | IR_C;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	enable |= IR_BE;
#endif
	aup->tx_head = 0;
	aup->tx_tail = 0;
	aup->rx_head = 0;

	for (i = 0; i < NUM_IR_DESC; i++)
		aup->rx_ring[i]->flags = AU_OWN;

	irda_write(aup, IR_ENABLE, enable);
	msleep(20);

	/* disable PHY */
	au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_OFF);
	irda_write(aup, IR_STATUS, irda_read(aup, IR_STATUS) & ~IR_PHYEN);
	msleep(20);

	irda_write(aup, IR_MAX_PKT_LEN, MAX_BUF_SIZE);

	ring_address = (u32)virt_to_phys((void *)aup->rx_ring[0]);
	irda_write(aup, IR_RING_BASE_ADDR_H, ring_address >> 26);
	irda_write(aup, IR_RING_BASE_ADDR_L, (ring_address >> 10) & 0xffff);

	irda_write(aup, IR_RING_SIZE,
				(RING_SIZE_64 << 8) | (RING_SIZE_64 << 12));

	irda_write(aup, IR_CONFIG_2, IR_PHYCLK_48MHZ | IR_ONE_PIN);
	irda_write(aup, IR_RING_ADDR_CMPR, 0);

	au1k_irda_set_speed(dev, 9600);
	return 0;
}

static int au1k_irda_start(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	char hwname[32];
	int retval;

	retval = au1k_init(dev);
	if (retval) {
		printk(KERN_ERR "%s: error in au1k_init\n", dev->name);
		return retval;
	}

	retval = request_irq(aup->irq_tx, &au1k_irda_interrupt, 0,
			     dev->name, dev);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n",
				dev->name, dev->irq);
		return retval;
	}
	retval = request_irq(aup->irq_rx, &au1k_irda_interrupt, 0,
			     dev->name, dev);
	if (retval) {
		free_irq(aup->irq_tx, dev);
		printk(KERN_ERR "%s: unable to get IRQ %d\n",
				dev->name, dev->irq);
		return retval;
	}

	/* Give self a hardware name */
	sprintf(hwname, "Au1000 SIR/FIR");
	aup->irlap = irlap_open(dev, &aup->qos, hwname);
	netif_start_queue(dev);

	/* int enable */
	irda_write(aup, IR_CONFIG_2, irda_read(aup, IR_CONFIG_2) | IR_IEN);

	/* power up */
	au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_SIR);

	aup->timer.expires = RUN_AT((3 * HZ));
	aup->timer.data = (unsigned long)dev;
	return 0;
}

static int au1k_irda_stop(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);

	au1k_irda_plat_set_phy_mode(aup, AU1000_IRDA_PHY_MODE_OFF);

	/* disable interrupts */
	irda_write(aup, IR_CONFIG_2, irda_read(aup, IR_CONFIG_2) & ~IR_IEN);
	irda_write(aup, IR_CONFIG_1, 0);
	irda_write(aup, IR_ENABLE, 0); /* disable clock */

	if (aup->irlap) {
		irlap_close(aup->irlap);
		aup->irlap = NULL;
	}

	netif_stop_queue(dev);
	del_timer(&aup->timer);

	/* disable the interrupt */
	free_irq(aup->irq_tx, dev);
	free_irq(aup->irq_rx, dev);

	return 0;
}

/*
 * Au1000 transmit routine.
 */
static int au1k_irda_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	int speed = irda_get_next_speed(skb);
	volatile struct ring_dest *ptxd;
	struct db_dest *pDB;
	u32 len, flags;

	if (speed != aup->speed && speed != -1)
		aup->newspeed = speed;

	if ((skb->len == 0) && (aup->newspeed)) {
		if (aup->tx_tail == aup->tx_head) {
			au1k_irda_set_speed(dev, speed);
			aup->newspeed = 0;
		}
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	ptxd = aup->tx_ring[aup->tx_head];
	flags = ptxd->flags;

	if (flags & AU_OWN) {
		printk(KERN_DEBUG "%s: tx_full\n", dev->name);
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return 1;
	} else if (((aup->tx_head + 1) & (NUM_IR_DESC - 1)) == aup->tx_tail) {
		printk(KERN_DEBUG "%s: tx_full\n", dev->name);
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return 1;
	}

	pDB = aup->tx_db_inuse[aup->tx_head];

#if 0
	if (irda_read(aup, IR_RX_BYTE_CNT) != 0) {
		printk(KERN_DEBUG "tx warning: rx byte cnt %x\n",
				irda_read(aup, IR_RX_BYTE_CNT));
	}
#endif

	if (aup->speed == 4000000) {
		/* FIR */
		skb_copy_from_linear_data(skb, (void *)pDB->vaddr, skb->len);
		ptxd->count_0 = skb->len & 0xff;
		ptxd->count_1 = (skb->len >> 8) & 0xff;
	} else {
		/* SIR */
		len = async_wrap_skb(skb, (u8 *)pDB->vaddr, MAX_BUF_SIZE);
		ptxd->count_0 = len & 0xff;
		ptxd->count_1 = (len >> 8) & 0xff;
		ptxd->flags |= IR_DIS_CRC;
	}
	ptxd->flags |= AU_OWN;
	wmb();

	irda_write(aup, IR_CONFIG_1,
		   irda_read(aup, IR_CONFIG_1) | IR_TX_ENABLE);
	irda_write(aup, IR_RING_PROMPT, 0);

	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_IR_DESC - 1);
	return NETDEV_TX_OK;
}

/*
 * The Tx ring has been full longer than the watchdog timeout
 * value. The transmitter must be hung?
 */
static void au1k_tx_timeout(struct net_device *dev)
{
	u32 speed;
	struct au1k_private *aup = netdev_priv(dev);

	printk(KERN_ERR "%s: tx timeout\n", dev->name);
	speed = aup->speed;
	aup->speed = 0;
	au1k_irda_set_speed(dev, speed);
	aup->tx_full = 0;
	netif_wake_queue(dev);
}

static int au1k_irda_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
{
	struct if_irda_req *rq = (struct if_irda_req *)ifreq;
	struct au1k_private *aup = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd) {
	case SIOCSBANDWIDTH:
		if (capable(CAP_NET_ADMIN)) {
			/*
			 * We are unable to set the speed if the
			 * device is not running.
			 */
			if (aup->open)
				ret = au1k_irda_set_speed(dev,
						rq->ifr_baudrate);
			else {
				printk(KERN_ERR "%s ioctl: !netif_running\n",
						dev->name);
				ret = 0;
			}
		}
		break;

	case SIOCSMEDIABUSY:
		ret = -EPERM;
		if (capable(CAP_NET_ADMIN)) {
			irda_device_set_media_busy(dev, TRUE);
			ret = 0;
		}
		break;

	case SIOCGRECEIVING:
		rq->ifr_receiving = 0;
		break;
	default:
		break;
	}
	return ret;
}

static const struct net_device_ops au1k_irda_netdev_ops = {
	.ndo_open		= au1k_irda_start,
	.ndo_stop		= au1k_irda_stop,
	.ndo_start_xmit		= au1k_irda_hard_xmit,
	.ndo_tx_timeout		= au1k_tx_timeout,
	.ndo_do_ioctl		= au1k_irda_ioctl,
};

static int au1k_irda_net_init(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	struct db_dest *pDB, *pDBfree;
	int i, err, retval = 0;
	dma_addr_t temp;

	err = au1k_irda_init_iobuf(&aup->rx_buff, 14384);
	if (err)
		goto out1;

	dev->netdev_ops = &au1k_irda_netdev_ops;

	irda_init_max_qos_capabilies(&aup->qos);

	/* The only value we must override it the baudrate */
	aup->qos.baud_rate.bits = IR_9600 | IR_19200 | IR_38400 |
		IR_57600 | IR_115200 | IR_576000 | (IR_4000000 << 8);

	aup->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&aup->qos);

	retval = -ENOMEM;

	/* Tx ring follows rx ring + 512 bytes */
	/* we need a 1k aligned buffer */
	aup->rx_ring[0] = (struct ring_dest *)
		dma_alloc(2 * MAX_NUM_IR_DESC * (sizeof(struct ring_dest)),
			  &temp);
	if (!aup->rx_ring[0])
		goto out2;

	/* allocate the data buffers */
	aup->db[0].vaddr =
		dma_alloc(MAX_BUF_SIZE * 2 * NUM_IR_DESC, &temp);
	if (!aup->db[0].vaddr)
		goto out3;

	setup_hw_rings(aup, (u32)aup->rx_ring[0], (u32)aup->rx_ring[0] + 512);

	pDBfree = NULL;
	pDB = aup->db;
	for (i = 0; i < (2 * NUM_IR_DESC); i++) {
		pDB->pnext = pDBfree;
		pDBfree = pDB;
		pDB->vaddr =
		       (u32 *)((unsigned)aup->db[0].vaddr + (MAX_BUF_SIZE * i));
		pDB->dma_addr = (dma_addr_t)virt_to_bus(pDB->vaddr);
		pDB++;
	}
	aup->pDBfree = pDBfree;

	/* attach a data buffer to each descriptor */
	for (i = 0; i < NUM_IR_DESC; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB)
			goto out3;
		aup->rx_ring[i]->addr_0 = (u8)(pDB->dma_addr & 0xff);
		aup->rx_ring[i]->addr_1 = (u8)((pDB->dma_addr >>  8) & 0xff);
		aup->rx_ring[i]->addr_2 = (u8)((pDB->dma_addr >> 16) & 0xff);
		aup->rx_ring[i]->addr_3 = (u8)((pDB->dma_addr >> 24) & 0xff);
		aup->rx_db_inuse[i] = pDB;
	}
	for (i = 0; i < NUM_IR_DESC; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB)
			goto out3;
		aup->tx_ring[i]->addr_0 = (u8)(pDB->dma_addr & 0xff);
		aup->tx_ring[i]->addr_1 = (u8)((pDB->dma_addr >>  8) & 0xff);
		aup->tx_ring[i]->addr_2 = (u8)((pDB->dma_addr >> 16) & 0xff);
		aup->tx_ring[i]->addr_3 = (u8)((pDB->dma_addr >> 24) & 0xff);
		aup->tx_ring[i]->count_0 = 0;
		aup->tx_ring[i]->count_1 = 0;
		aup->tx_ring[i]->flags = 0;
		aup->tx_db_inuse[i] = pDB;
	}

	return 0;

out3:
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC * (sizeof(struct ring_dest)));
out2:
	kfree(aup->rx_buff.head);
out1:
	printk(KERN_ERR "au1k_irda_net_init() failed.  Returns %d\n", retval);
	return retval;
}

static int au1k_irda_probe(struct platform_device *pdev)
{
	struct au1k_private *aup;
	struct net_device *dev;
	struct resource *r;
	int err;

	dev = alloc_irdadev(sizeof(struct au1k_private));
	if (!dev)
		return -ENOMEM;

	aup = netdev_priv(dev);

	aup->platdata = pdev->dev.platform_data;

	err = -EINVAL;
	r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r)
		goto out;

	aup->irq_tx = r->start;

	r = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!r)
		goto out;

	aup->irq_rx = r->start;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		goto out;

	err = -EBUSY;
	aup->ioarea = request_mem_region(r->start, resource_size(r),
					 pdev->name);
	if (!aup->ioarea)
		goto out;

	aup->iobase = ioremap_nocache(r->start, resource_size(r));
	if (!aup->iobase)
		goto out2;

	dev->irq = aup->irq_rx;

	err = au1k_irda_net_init(dev);
	if (err)
		goto out3;
	err = register_netdev(dev);
	if (err)
		goto out4;

	platform_set_drvdata(pdev, dev);

	printk(KERN_INFO "IrDA: Registered device %s\n", dev->name);
	return 0;

out4:
	dma_free((void *)aup->db[0].vaddr,
		MAX_BUF_SIZE * 2 * NUM_IR_DESC);
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC * (sizeof(struct ring_dest)));
	kfree(aup->rx_buff.head);
out3:
	iounmap(aup->iobase);
out2:
	release_resource(aup->ioarea);
	kfree(aup->ioarea);
out:
	free_netdev(dev);
	return err;
}

static int au1k_irda_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct au1k_private *aup = netdev_priv(dev);

	unregister_netdev(dev);

	dma_free((void *)aup->db[0].vaddr,
		MAX_BUF_SIZE * 2 * NUM_IR_DESC);
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC * (sizeof(struct ring_dest)));
	kfree(aup->rx_buff.head);

	iounmap(aup->iobase);
	release_resource(aup->ioarea);
	kfree(aup->ioarea);

	free_netdev(dev);

	return 0;
}

static struct platform_driver au1k_irda_driver = {
	.driver	= {
		.name	= "au1000-irda",
		.owner	= THIS_MODULE,
	},
	.probe		= au1k_irda_probe,
	.remove		= au1k_irda_remove,
};

module_platform_driver(au1k_irda_driver);

MODULE_AUTHOR("Pete Popov <ppopov@mvista.com>");
MODULE_DESCRIPTION("Au1000 IrDA Device Driver");
