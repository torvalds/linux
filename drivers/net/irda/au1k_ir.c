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
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/bitops.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/au1000.h>
#if defined(CONFIG_MIPS_PB1000) || defined(CONFIG_MIPS_PB1100)
#include <asm/pb1000.h>
#elif defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
#include <asm/db1x00.h>
#else 
#error au1k_ir: unsupported board
#endif

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/wrapper.h>
#include <net/irda/irda_device.h>
#include "au1000_ircc.h"

static int au1k_irda_net_init(struct net_device *);
static int au1k_irda_start(struct net_device *);
static int au1k_irda_stop(struct net_device *dev);
static int au1k_irda_hard_xmit(struct sk_buff *, struct net_device *);
static int au1k_irda_rx(struct net_device *);
static void au1k_irda_interrupt(int, void *);
static void au1k_tx_timeout(struct net_device *);
static int au1k_irda_ioctl(struct net_device *, struct ifreq *, int);
static int au1k_irda_set_speed(struct net_device *dev, int speed);

static void *dma_alloc(size_t, dma_addr_t *);
static void dma_free(void *, size_t);

static int qos_mtt_bits = 0x07;  /* 1 ms or more */
static struct net_device *ir_devs[NUM_IR_IFF];
static char version[] __devinitdata =
    "au1k_ircc:1.2 ppopov@mvista.com\n";

#define RUN_AT(x) (jiffies + (x))

#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
static BCSR * const bcsr = (BCSR *)0xAE000000;
#endif

static DEFINE_SPINLOCK(ir_lock);

/*
 * IrDA peripheral bug. You have to read the register
 * twice to get the right value.
 */
u32 read_ir_reg(u32 addr) 
{ 
	readl(addr);
	return readl(addr);
}


/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for 
 * both, receive and transmit operations.
 */
static db_dest_t *GetFreeDB(struct au1k_private *aup)
{
	db_dest_t *pDB;
	pDB = aup->pDBfree;

	if (pDB) {
		aup->pDBfree = pDB->pnext;
	}
	return pDB;
}

static void ReleaseDB(struct au1k_private *aup, db_dest_t *pDB)
{
	db_dest_t *pDBfree = aup->pDBfree;
	if (pDBfree)
		pDBfree->pnext = pDB;
	aup->pDBfree = pDB;
}


/*
  DMA memory allocation, derived from pci_alloc_consistent.
  However, the Au1000 data cache is coherent (when programmed
  so), therefore we return KSEG0 address, not KSEG1.
*/
static void *dma_alloc(size_t size, dma_addr_t * dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC | GFP_DMA;

	ret = (void *) __get_free_pages(gfp, get_order(size));

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


static void 
setup_hw_rings(struct au1k_private *aup, u32 rx_base, u32 tx_base)
{
	int i;
	for (i=0; i<NUM_IR_DESC; i++) {
		aup->rx_ring[i] = (volatile ring_dest_t *) 
			(rx_base + sizeof(ring_dest_t)*i);
	}
	for (i=0; i<NUM_IR_DESC; i++) {
		aup->tx_ring[i] = (volatile ring_dest_t *) 
			(tx_base + sizeof(ring_dest_t)*i);
	}
}

static int au1k_irda_init(void)
{
	static unsigned version_printed = 0;
	struct au1k_private *aup;
	struct net_device *dev;
	int err;

	if (version_printed++ == 0) printk(version);

	dev = alloc_irdadev(sizeof(struct au1k_private));
	if (!dev)
		return -ENOMEM;

	dev->irq = AU1000_IRDA_RX_INT; /* TX has its own interrupt */
	err = au1k_irda_net_init(dev);
	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	ir_devs[0] = dev;
	printk(KERN_INFO "IrDA: Registered device %s\n", dev->name);
	return 0;

out1:
	aup = netdev_priv(dev);
	dma_free((void *)aup->db[0].vaddr,
		MAX_BUF_SIZE * 2*NUM_IR_DESC);
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC*(sizeof(ring_dest_t)));
	kfree(aup->rx_buff.head);
out:
	free_netdev(dev);
	return err;
}

static int au1k_irda_init_iobuf(iobuff_t *io, int size)
{
	io->head = kmalloc(size, GFP_KERNEL);
	if (io->head != NULL) {
		io->truesize = size;
		io->in_frame = FALSE;
		io->state    = OUTSIDE_FRAME;
		io->data     = io->head;
	}
	return io->head ? 0 : -ENOMEM;
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
	int i, retval = 0, err;
	db_dest_t *pDB, *pDBfree;
	dma_addr_t temp;

	err = au1k_irda_init_iobuf(&aup->rx_buff, 14384);
	if (err)
		goto out1;

	dev->netdev_ops = &au1k_irda_netdev_ops;

	irda_init_max_qos_capabilies(&aup->qos);

	/* The only value we must override it the baudrate */
	aup->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000 |(IR_4000000 << 8);
	
	aup->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&aup->qos);

	retval = -ENOMEM;

	/* Tx ring follows rx ring + 512 bytes */
	/* we need a 1k aligned buffer */
	aup->rx_ring[0] = (ring_dest_t *)
		dma_alloc(2*MAX_NUM_IR_DESC*(sizeof(ring_dest_t)), &temp);
	if (!aup->rx_ring[0])
		goto out2;

	/* allocate the data buffers */
	aup->db[0].vaddr = 
		(void *)dma_alloc(MAX_BUF_SIZE * 2*NUM_IR_DESC, &temp);
	if (!aup->db[0].vaddr)
		goto out3;

	setup_hw_rings(aup, (u32)aup->rx_ring[0], (u32)aup->rx_ring[0] + 512);

	pDBfree = NULL;
	pDB = aup->db;
	for (i=0; i<(2*NUM_IR_DESC); i++) {
		pDB->pnext = pDBfree;
		pDBfree = pDB;
		pDB->vaddr = 
			(u32 *)((unsigned)aup->db[0].vaddr + MAX_BUF_SIZE*i);
		pDB->dma_addr = (dma_addr_t)virt_to_bus(pDB->vaddr);
		pDB++;
	}
	aup->pDBfree = pDBfree;

	/* attach a data buffer to each descriptor */
	for (i=0; i<NUM_IR_DESC; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) goto out;
		aup->rx_ring[i]->addr_0 = (u8)(pDB->dma_addr & 0xff);
		aup->rx_ring[i]->addr_1 = (u8)((pDB->dma_addr>>8) & 0xff);
		aup->rx_ring[i]->addr_2 = (u8)((pDB->dma_addr>>16) & 0xff);
		aup->rx_ring[i]->addr_3 = (u8)((pDB->dma_addr>>24) & 0xff);
		aup->rx_db_inuse[i] = pDB;
	}
	for (i=0; i<NUM_IR_DESC; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) goto out;
		aup->tx_ring[i]->addr_0 = (u8)(pDB->dma_addr & 0xff);
		aup->tx_ring[i]->addr_1 = (u8)((pDB->dma_addr>>8) & 0xff);
		aup->tx_ring[i]->addr_2 = (u8)((pDB->dma_addr>>16) & 0xff);
		aup->tx_ring[i]->addr_3 = (u8)((pDB->dma_addr>>24) & 0xff);
		aup->tx_ring[i]->count_0 = 0;
		aup->tx_ring[i]->count_1 = 0;
		aup->tx_ring[i]->flags = 0;
		aup->tx_db_inuse[i] = pDB;
	}

#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
	/* power on */
	bcsr->resets &= ~BCSR_RESETS_IRDA_MODE_MASK;
	bcsr->resets |= BCSR_RESETS_IRDA_MODE_FULL;
	au_sync();
#endif

	return 0;

out3:
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC*(sizeof(ring_dest_t)));
out2:
	kfree(aup->rx_buff.head);
out1:
	printk(KERN_ERR "au1k_init_module failed.  Returns %d\n", retval);
	return retval;
}


static int au1k_init(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	int i;
	u32 control;
	u32 ring_address;

	/* bring the device out of reset */
	control = 0xe; /* coherent, clock enable, one half system clock */
			  
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	control |= 1;
#endif
	aup->tx_head = 0;
	aup->tx_tail = 0;
	aup->rx_head = 0;

	for (i=0; i<NUM_IR_DESC; i++) {
		aup->rx_ring[i]->flags = AU_OWN;
	}

	writel(control, IR_INTERFACE_CONFIG);
	au_sync_delay(10);

	writel(read_ir_reg(IR_ENABLE) & ~0x8000, IR_ENABLE); /* disable PHY */
	au_sync_delay(1);

	writel(MAX_BUF_SIZE, IR_MAX_PKT_LEN);

	ring_address = (u32)virt_to_phys((void *)aup->rx_ring[0]);
	writel(ring_address >> 26, IR_RING_BASE_ADDR_H);
	writel((ring_address >> 10) & 0xffff, IR_RING_BASE_ADDR_L);

	writel(RING_SIZE_64<<8 | RING_SIZE_64<<12, IR_RING_SIZE);

	writel(1<<2 | IR_ONE_PIN, IR_CONFIG_2); /* 48MHz */
	writel(0, IR_RING_ADDR_CMPR);

	au1k_irda_set_speed(dev, 9600);
	return 0;
}

static int au1k_irda_start(struct net_device *dev)
{
	int retval;
	char hwname[32];
	struct au1k_private *aup = netdev_priv(dev);

	if ((retval = au1k_init(dev))) {
		printk(KERN_ERR "%s: error in au1k_init\n", dev->name);
		return retval;
	}

	if ((retval = request_irq(AU1000_IRDA_TX_INT, &au1k_irda_interrupt, 
					0, dev->name, dev))) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", 
				dev->name, dev->irq);
		return retval;
	}
	if ((retval = request_irq(AU1000_IRDA_RX_INT, &au1k_irda_interrupt, 
					0, dev->name, dev))) {
		free_irq(AU1000_IRDA_TX_INT, dev);
		printk(KERN_ERR "%s: unable to get IRQ %d\n", 
				dev->name, dev->irq);
		return retval;
	}

	/* Give self a hardware name */
	sprintf(hwname, "Au1000 SIR/FIR");
	aup->irlap = irlap_open(dev, &aup->qos, hwname);
	netif_start_queue(dev);

	writel(read_ir_reg(IR_CONFIG_2) | 1<<8, IR_CONFIG_2); /* int enable */

	aup->timer.expires = RUN_AT((3*HZ)); 
	aup->timer.data = (unsigned long)dev;
	return 0;
}

static int au1k_irda_stop(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);

	/* disable interrupts */
	writel(read_ir_reg(IR_CONFIG_2) & ~(1<<8), IR_CONFIG_2);
	writel(0, IR_CONFIG_1); 
	writel(0, IR_INTERFACE_CONFIG); /* disable clock */
	au_sync();

	if (aup->irlap) {
		irlap_close(aup->irlap);
		aup->irlap = NULL;
	}

	netif_stop_queue(dev);
	del_timer(&aup->timer);

	/* disable the interrupt */
	free_irq(AU1000_IRDA_TX_INT, dev);
	free_irq(AU1000_IRDA_RX_INT, dev);
	return 0;
}

static void __exit au1k_irda_exit(void)
{
	struct net_device *dev = ir_devs[0];
	struct au1k_private *aup = netdev_priv(dev);

	unregister_netdev(dev);

	dma_free((void *)aup->db[0].vaddr,
		MAX_BUF_SIZE * 2*NUM_IR_DESC);
	dma_free((void *)aup->rx_ring[0],
		2 * MAX_NUM_IR_DESC*(sizeof(ring_dest_t)));
	kfree(aup->rx_buff.head);
	free_netdev(dev);
}


static inline void 
update_tx_stats(struct net_device *dev, u32 status, u32 pkt_len)
{
	struct au1k_private *aup = netdev_priv(dev);
	struct net_device_stats *ps = &aup->stats;

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
	volatile ring_dest_t *ptxd;

	ptxd = aup->tx_ring[aup->tx_tail];
	while (!(ptxd->flags & AU_OWN) && (aup->tx_tail != aup->tx_head)) {
		update_tx_stats(dev, ptxd->flags, 
				ptxd->count_1<<8 | ptxd->count_0);
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
		au_sync();

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
		}
		else {
			writel(read_ir_reg(IR_CONFIG_1) & ~IR_TX_ENABLE, 
					IR_CONFIG_1); 
			au_sync();
			writel(read_ir_reg(IR_CONFIG_1) | IR_RX_ENABLE, 
					IR_CONFIG_1); 
			writel(0, IR_RING_PROMPT);
			au_sync();
		}
	}
}


/*
 * Au1000 transmit routine.
 */
static int au1k_irda_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	int speed = irda_get_next_speed(skb);
	volatile ring_dest_t *ptxd;
	u32 len;

	u32 flags;
	db_dest_t *pDB;

	if (speed != aup->speed && speed != -1) {
		aup->newspeed = speed;
	}

	if ((skb->len == 0) && (aup->newspeed)) {
		if (aup->tx_tail == aup->tx_head) {
			au1k_irda_set_speed(dev, speed);
			aup->newspeed = 0;
		}
		dev_kfree_skb(skb);
		return 0;
	}

	ptxd = aup->tx_ring[aup->tx_head];
	flags = ptxd->flags;

	if (flags & AU_OWN) {
		printk(KERN_DEBUG "%s: tx_full\n", dev->name);
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return NETDEV_TX_BUSY;
	}
	else if (((aup->tx_head + 1) & (NUM_IR_DESC - 1)) == aup->tx_tail) {
		printk(KERN_DEBUG "%s: tx_full\n", dev->name);
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return NETDEV_TX_BUSY;
	}

	pDB = aup->tx_db_inuse[aup->tx_head];

#if 0
	if (read_ir_reg(IR_RX_BYTE_CNT) != 0) {
		printk("tx warning: rx byte cnt %x\n", 
				read_ir_reg(IR_RX_BYTE_CNT));
	}
#endif
	
	if (aup->speed == 4000000) {
		/* FIR */
		skb_copy_from_linear_data(skb, pDB->vaddr, skb->len);
		ptxd->count_0 = skb->len & 0xff;
		ptxd->count_1 = (skb->len >> 8) & 0xff;

	}
	else {
		/* SIR */
		len = async_wrap_skb(skb, (u8 *)pDB->vaddr, MAX_BUF_SIZE);
		ptxd->count_0 = len & 0xff;
		ptxd->count_1 = (len >> 8) & 0xff;
		ptxd->flags |= IR_DIS_CRC;
		au_writel(au_readl(0xae00000c) & ~(1<<13), 0xae00000c);
	}
	ptxd->flags |= AU_OWN;
	au_sync();

	writel(read_ir_reg(IR_CONFIG_1) | IR_TX_ENABLE, IR_CONFIG_1); 
	writel(0, IR_RING_PROMPT);
	au_sync();

	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_IR_DESC - 1);
	dev->trans_start = jiffies;
	return 0;
}


static inline void 
update_rx_stats(struct net_device *dev, u32 status, u32 count)
{
	struct au1k_private *aup = netdev_priv(dev);
	struct net_device_stats *ps = &aup->stats;

	ps->rx_packets++;

	if (status & IR_RX_ERROR) {
		ps->rx_errors++;
		if (status & (IR_PHY_ERROR|IR_FIFO_OVER))
			ps->rx_missed_errors++;
		if (status & IR_MAX_LEN)
			ps->rx_length_errors++;
		if (status & IR_CRC_ERROR)
			ps->rx_crc_errors++;
	}
	else 
		ps->rx_bytes += count;
}

/*
 * Au1000 receive routine.
 */
static int au1k_irda_rx(struct net_device *dev)
{
	struct au1k_private *aup = netdev_priv(dev);
	struct sk_buff *skb;
	volatile ring_dest_t *prxd;
	u32 flags, count;
	db_dest_t *pDB;

	prxd = aup->rx_ring[aup->rx_head];
	flags = prxd->flags;

	while (!(flags & AU_OWN))  {
		pDB = aup->rx_db_inuse[aup->rx_head];
		count = prxd->count_1<<8 | prxd->count_0;
		if (!(flags & IR_RX_ERROR))  {
			/* good frame */
			update_rx_stats(dev, flags, count);
			skb=alloc_skb(count+1,GFP_ATOMIC);
			if (skb == NULL) {
				aup->netdev->stats.rx_dropped++;
				continue;
			}
			skb_reserve(skb, 1);
			if (aup->speed == 4000000)
				skb_put(skb, count);
			else
				skb_put(skb, count-2);
			skb_copy_to_linear_data(skb, pDB->vaddr, count - 2);
			skb->dev = dev;
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IRDA);
			netif_rx(skb);
			prxd->count_0 = 0;
			prxd->count_1 = 0;
		}
		prxd->flags |= AU_OWN;
		aup->rx_head = (aup->rx_head + 1) & (NUM_IR_DESC - 1);
		writel(0, IR_RING_PROMPT);
		au_sync();

		/* next descriptor */
		prxd = aup->rx_ring[aup->rx_head];
		flags = prxd->flags;

	}
	return 0;
}


static irqreturn_t au1k_irda_interrupt(int dummy, void *dev_id)
{
	struct net_device *dev = dev_id;

	writel(0, IR_INT_CLEAR); /* ack irda interrupts */

	au1k_irda_rx(dev);
	au1k_tx_ack(dev);

	return IRQ_HANDLED;
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


/*
 * Set the IrDA communications speed.
 */
static int 
au1k_irda_set_speed(struct net_device *dev, int speed)
{
	unsigned long flags;
	struct au1k_private *aup = netdev_priv(dev);
	u32 control;
	int ret = 0, timeout = 10, i;
	volatile ring_dest_t *ptxd;
#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
	unsigned long irda_resets;
#endif

	if (speed == aup->speed)
		return ret;

	spin_lock_irqsave(&ir_lock, flags);

	/* disable PHY first */
	writel(read_ir_reg(IR_ENABLE) & ~0x8000, IR_ENABLE);

	/* disable RX/TX */
	writel(read_ir_reg(IR_CONFIG_1) & ~(IR_RX_ENABLE|IR_TX_ENABLE), 
			IR_CONFIG_1);
	au_sync_delay(1);
	while (read_ir_reg(IR_ENABLE) & (IR_RX_STATUS | IR_TX_STATUS)) {
		mdelay(1);
		if (!timeout--) {
			printk(KERN_ERR "%s: rx/tx disable timeout\n",
					dev->name);
			break;
		}
	}

	/* disable DMA */
	writel(read_ir_reg(IR_CONFIG_1) & ~IR_DMA_ENABLE, IR_CONFIG_1);
	au_sync_delay(1);

	/* 
	 *  After we disable tx/rx. the index pointers
 	 * go back to zero.
	 */
	aup->tx_head = aup->tx_tail = aup->rx_head = 0;
	for (i=0; i<NUM_IR_DESC; i++) {
		ptxd = aup->tx_ring[i];
		ptxd->flags = 0;
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
	}

	for (i=0; i<NUM_IR_DESC; i++) {
		ptxd = aup->rx_ring[i];
		ptxd->count_0 = 0;
		ptxd->count_1 = 0;
		ptxd->flags = AU_OWN;
	}

	if (speed == 4000000) {
#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
		bcsr->resets |= BCSR_RESETS_FIR_SEL;
#else /* Pb1000 and Pb1100 */
		writel(1<<13, CPLD_AUX1);
#endif
	}
	else {
#if defined(CONFIG_MIPS_DB1000) || defined(CONFIG_MIPS_DB1100)
		bcsr->resets &= ~BCSR_RESETS_FIR_SEL;
#else /* Pb1000 and Pb1100 */
		writel(readl(CPLD_AUX1) & ~(1<<13), CPLD_AUX1);
#endif
	}

	switch (speed) {
	case 9600:	
		writel(11<<10 | 12<<5, IR_WRITE_PHY_CONFIG); 
		writel(IR_SIR_MODE, IR_CONFIG_1); 
		break;
	case 19200:	
		writel(5<<10 | 12<<5, IR_WRITE_PHY_CONFIG); 
		writel(IR_SIR_MODE, IR_CONFIG_1); 
		break;
	case 38400:
		writel(2<<10 | 12<<5, IR_WRITE_PHY_CONFIG); 
		writel(IR_SIR_MODE, IR_CONFIG_1); 
		break;
	case 57600:	
		writel(1<<10 | 12<<5, IR_WRITE_PHY_CONFIG); 
		writel(IR_SIR_MODE, IR_CONFIG_1); 
		break;
	case 115200: 
		writel(12<<5, IR_WRITE_PHY_CONFIG); 
		writel(IR_SIR_MODE, IR_CONFIG_1); 
		break;
	case 4000000:
		writel(0xF, IR_WRITE_PHY_CONFIG);
		writel(IR_FIR|IR_DMA_ENABLE|IR_RX_ENABLE, IR_CONFIG_1); 
		break;
	default:
		printk(KERN_ERR "%s unsupported speed %x\n", dev->name, speed);
		ret = -EINVAL;
		break;
	}

	aup->speed = speed;
	writel(read_ir_reg(IR_ENABLE) | 0x8000, IR_ENABLE);
	au_sync();

	control = read_ir_reg(IR_ENABLE);
	writel(0, IR_RING_PROMPT);
	au_sync();

	if (control & (1<<14)) {
		printk(KERN_ERR "%s: configuration error\n", dev->name);
	}
	else {
		if (control & (1<<11))
			printk(KERN_DEBUG "%s Valid SIR config\n", dev->name);
		if (control & (1<<12))
			printk(KERN_DEBUG "%s Valid MIR config\n", dev->name);
		if (control & (1<<13))
			printk(KERN_DEBUG "%s Valid FIR config\n", dev->name);
		if (control & (1<<10))
			printk(KERN_DEBUG "%s TX enabled\n", dev->name);
		if (control & (1<<9))
			printk(KERN_DEBUG "%s RX enabled\n", dev->name);
	}

	spin_unlock_irqrestore(&ir_lock, flags);
	return ret;
}

static int 
au1k_irda_ioctl(struct net_device *dev, struct ifreq *ifreq, int cmd)
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

MODULE_AUTHOR("Pete Popov <ppopov@mvista.com>");
MODULE_DESCRIPTION("Au1000 IrDA Device Driver");

module_init(au1k_irda_init);
module_exit(au1k_irda_exit);
