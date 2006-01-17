/*
 * IXP2000 MSF network device driver
 * Copyright (C) 2004, 2005 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <asm/arch/uengine.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include "ixp2400_rx.ucode"
#include "ixp2400_tx.ucode"
#include "ixpdev_priv.h"
#include "ixpdev.h"

#define DRV_MODULE_VERSION	"0.2"

static int nds_count;
static struct net_device **nds;
static int nds_open;
static void (*set_port_admin_status)(int port, int up);

static struct ixpdev_rx_desc * const rx_desc =
	(struct ixpdev_rx_desc *)(IXP2000_SRAM0_VIRT_BASE + RX_BUF_DESC_BASE);
static struct ixpdev_tx_desc * const tx_desc =
	(struct ixpdev_tx_desc *)(IXP2000_SRAM0_VIRT_BASE + TX_BUF_DESC_BASE);
static int tx_pointer;


static int ixpdev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ixpdev_priv *ip = netdev_priv(dev);
	struct ixpdev_tx_desc *desc;
	int entry;

	if (unlikely(skb->len > PAGE_SIZE)) {
		/* @@@ Count drops.  */
		dev_kfree_skb(skb);
		return 0;
	}

	entry = tx_pointer;
	tx_pointer = (tx_pointer + 1) % TX_BUF_COUNT;

	desc = tx_desc + entry;
	desc->pkt_length = skb->len;
	desc->channel = ip->channel;

	skb_copy_and_csum_dev(skb, phys_to_virt(desc->buf_addr));
	dev_kfree_skb(skb);

	ixp2000_reg_write(RING_TX_PENDING,
		TX_BUF_DESC_BASE + (entry * sizeof(struct ixpdev_tx_desc)));

	dev->trans_start = jiffies;

	local_irq_disable();
	ip->tx_queue_entries++;
	if (ip->tx_queue_entries == TX_BUF_COUNT_PER_CHAN)
		netif_stop_queue(dev);
	local_irq_enable();

	return 0;
}


static int ixpdev_rx(struct net_device *dev, int *budget)
{
	while (*budget > 0) {
		struct ixpdev_rx_desc *desc;
		struct sk_buff *skb;
		void *buf;
		u32 _desc;

		_desc = ixp2000_reg_read(RING_RX_DONE);
		if (_desc == 0)
			return 0;

		desc = rx_desc +
			((_desc - RX_BUF_DESC_BASE) / sizeof(struct ixpdev_rx_desc));
		buf = phys_to_virt(desc->buf_addr);

		if (desc->pkt_length < 4 || desc->pkt_length > PAGE_SIZE) {
			printk(KERN_ERR "ixp2000: rx err, length %d\n",
					desc->pkt_length);
			goto err;
		}

		if (desc->channel < 0 || desc->channel >= nds_count) {
			printk(KERN_ERR "ixp2000: rx err, channel %d\n",
					desc->channel);
			goto err;
		}

		/* @@@ Make FCS stripping configurable.  */
		desc->pkt_length -= 4;

		if (unlikely(!netif_running(nds[desc->channel])))
			goto err;

		skb = dev_alloc_skb(desc->pkt_length + 2);
		if (likely(skb != NULL)) {
			skb->dev = nds[desc->channel];
			skb_reserve(skb, 2);
			eth_copy_and_sum(skb, buf, desc->pkt_length, 0);
			skb_put(skb, desc->pkt_length);
			skb->protocol = eth_type_trans(skb, skb->dev);

			skb->dev->last_rx = jiffies;

			netif_receive_skb(skb);
		}

err:
		ixp2000_reg_write(RING_RX_PENDING, _desc);
		dev->quota--;
		(*budget)--;
	}

	return 1;
}

/* dev always points to nds[0].  */
static int ixpdev_poll(struct net_device *dev, int *budget)
{
	/* @@@ Have to stop polling when nds[0] is administratively
	 * downed while we are polling.  */
	do {
		ixp2000_reg_write(IXP2000_IRQ_THD_RAW_STATUS_A_0, 0x00ff);

		if (ixpdev_rx(dev, budget))
			return 1;
	} while (ixp2000_reg_read(IXP2000_IRQ_THD_RAW_STATUS_A_0) & 0x00ff);

	netif_rx_complete(dev);
	ixp2000_reg_write(IXP2000_IRQ_THD_ENABLE_SET_A_0, 0x00ff);

	return 0;
}

static void ixpdev_tx_complete(void)
{
	int channel;
	u32 wake;

	wake = 0;
	while (1) {
		struct ixpdev_priv *ip;
		u32 desc;
		int entry;

		desc = ixp2000_reg_read(RING_TX_DONE);
		if (desc == 0)
			break;

		/* @@@ Check whether entries come back in order.  */
		entry = (desc - TX_BUF_DESC_BASE) / sizeof(struct ixpdev_tx_desc);
		channel = tx_desc[entry].channel;

		if (channel < 0 || channel >= nds_count) {
			printk(KERN_ERR "ixp2000: txcomp channel index "
					"out of bounds (%d, %.8i, %d)\n",
					channel, (unsigned int)desc, entry);
			continue;
		}

		ip = netdev_priv(nds[channel]);
		if (ip->tx_queue_entries == TX_BUF_COUNT_PER_CHAN)
			wake |= 1 << channel;
		ip->tx_queue_entries--;
	}

	for (channel = 0; wake != 0; channel++) {
		if (wake & (1 << channel)) {
			netif_wake_queue(nds[channel]);
			wake &= ~(1 << channel);
		}
	}
}

static irqreturn_t ixpdev_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 status;

	status = ixp2000_reg_read(IXP2000_IRQ_THD_STATUS_A_0);
	if (status == 0)
		return IRQ_NONE;

	/*
	 * Any of the eight receive units signaled RX?
	 */
	if (status & 0x00ff) {
		ixp2000_reg_wrb(IXP2000_IRQ_THD_ENABLE_CLEAR_A_0, 0x00ff);
		if (likely(__netif_rx_schedule_prep(nds[0]))) {
			__netif_rx_schedule(nds[0]);
		} else {
			printk(KERN_CRIT "ixp2000: irq while polling!!\n");
		}
	}

	/*
	 * Any of the eight transmit units signaled TXdone?
	 */
	if (status & 0xff00) {
		ixp2000_reg_wrb(IXP2000_IRQ_THD_RAW_STATUS_A_0, 0xff00);
		ixpdev_tx_complete();
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ixpdev_poll_controller(struct net_device *dev)
{
	disable_irq(IRQ_IXP2000_THDA0);
	ixpdev_interrupt(IRQ_IXP2000_THDA0, dev, NULL);
	enable_irq(IRQ_IXP2000_THDA0);
}
#endif

static int ixpdev_open(struct net_device *dev)
{
	struct ixpdev_priv *ip = netdev_priv(dev);
	int err;

	if (!nds_open++) {
		err = request_irq(IRQ_IXP2000_THDA0, ixpdev_interrupt,
					SA_SHIRQ, "ixp2000_eth", nds);
		if (err) {
			nds_open--;
			return err;
		}

		ixp2000_reg_write(IXP2000_IRQ_THD_ENABLE_SET_A_0, 0xffff);
	}

	set_port_admin_status(ip->channel, 1);
	netif_start_queue(dev);

	return 0;
}

static int ixpdev_close(struct net_device *dev)
{
	struct ixpdev_priv *ip = netdev_priv(dev);

	netif_stop_queue(dev);
	set_port_admin_status(ip->channel, 0);

	if (!--nds_open) {
		ixp2000_reg_write(IXP2000_IRQ_THD_ENABLE_CLEAR_A_0, 0xffff);
		free_irq(IRQ_IXP2000_THDA0, nds);
	}

	return 0;
}

struct net_device *ixpdev_alloc(int channel, int sizeof_priv)
{
	struct net_device *dev;
	struct ixpdev_priv *ip;

	dev = alloc_etherdev(sizeof_priv);
	if (dev == NULL)
		return NULL;

	dev->hard_start_xmit = ixpdev_xmit;
	dev->poll = ixpdev_poll;
	dev->open = ixpdev_open;
	dev->stop = ixpdev_close;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = ixpdev_poll_controller;
#endif

	dev->features |= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->weight = 64;

	ip = netdev_priv(dev);
	ip->channel = channel;
	ip->tx_queue_entries = 0;

	return dev;
}

int ixpdev_init(int __nds_count, struct net_device **__nds,
		void (*__set_port_admin_status)(int port, int up))
{
	int i;
	int err;

	if (RX_BUF_COUNT > 192 || TX_BUF_COUNT > 192) {
		static void __too_many_rx_or_tx_buffers(void);
		__too_many_rx_or_tx_buffers();
	}

	printk(KERN_INFO "IXP2000 MSF ethernet driver %s\n", DRV_MODULE_VERSION);

	nds_count = __nds_count;
	nds = __nds;
	set_port_admin_status = __set_port_admin_status;

	for (i = 0; i < RX_BUF_COUNT; i++) {
		void *buf;

		buf = (void *)get_zeroed_page(GFP_KERNEL);
		if (buf == NULL) {
			err = -ENOMEM;
			while (--i >= 0)
				free_page((unsigned long)phys_to_virt(rx_desc[i].buf_addr));
			goto err_out;
		}
		rx_desc[i].buf_addr = virt_to_phys(buf);
		rx_desc[i].buf_length = PAGE_SIZE;
	}

	/* @@@ Maybe we shouldn't be preallocating TX buffers.  */
	for (i = 0; i < TX_BUF_COUNT; i++) {
		void *buf;

		buf = (void *)get_zeroed_page(GFP_KERNEL);
		if (buf == NULL) {
			err = -ENOMEM;
			while (--i >= 0)
				free_page((unsigned long)phys_to_virt(tx_desc[i].buf_addr));
			goto err_free_rx;
		}
		tx_desc[i].buf_addr = virt_to_phys(buf);
	}

	/* 256 entries, ring status set means 'empty', base address 0x0000.  */
	ixp2000_reg_write(RING_RX_PENDING_BASE, 0x44000000);
	ixp2000_reg_write(RING_RX_PENDING_HEAD, 0x00000000);
	ixp2000_reg_write(RING_RX_PENDING_TAIL, 0x00000000);

	/* 256 entries, ring status set means 'full', base address 0x0400.  */
	ixp2000_reg_write(RING_RX_DONE_BASE, 0x40000400);
	ixp2000_reg_write(RING_RX_DONE_HEAD, 0x00000000);
	ixp2000_reg_write(RING_RX_DONE_TAIL, 0x00000000);

	for (i = 0; i < RX_BUF_COUNT; i++) {
		ixp2000_reg_write(RING_RX_PENDING,
			RX_BUF_DESC_BASE + (i * sizeof(struct ixpdev_rx_desc)));
	}

	ixp2000_uengine_load(0, &ixp2400_rx);
	ixp2000_uengine_start_contexts(0, 0xff);

	/* 256 entries, ring status set means 'empty', base address 0x0800.  */
	ixp2000_reg_write(RING_TX_PENDING_BASE, 0x44000800);
	ixp2000_reg_write(RING_TX_PENDING_HEAD, 0x00000000);
	ixp2000_reg_write(RING_TX_PENDING_TAIL, 0x00000000);

	/* 256 entries, ring status set means 'full', base address 0x0c00.  */
	ixp2000_reg_write(RING_TX_DONE_BASE, 0x40000c00);
	ixp2000_reg_write(RING_TX_DONE_HEAD, 0x00000000);
	ixp2000_reg_write(RING_TX_DONE_TAIL, 0x00000000);

	ixp2000_uengine_load(1, &ixp2400_tx);
	ixp2000_uengine_start_contexts(1, 0xff);

	for (i = 0; i < nds_count; i++) {
		err = register_netdev(nds[i]);
		if (err) {
			while (--i >= 0)
				unregister_netdev(nds[i]);
			goto err_free_tx;
		}
	}

	for (i = 0; i < nds_count; i++) {
		printk(KERN_INFO "%s: IXP2000 MSF ethernet (port %d), "
			"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x.\n", nds[i]->name, i,
			nds[i]->dev_addr[0], nds[i]->dev_addr[1],
			nds[i]->dev_addr[2], nds[i]->dev_addr[3],
			nds[i]->dev_addr[4], nds[i]->dev_addr[5]);
	}

	return 0;

err_free_tx:
	for (i = 0; i < TX_BUF_COUNT; i++)
		free_page((unsigned long)phys_to_virt(tx_desc[i].buf_addr));

err_free_rx:
	for (i = 0; i < RX_BUF_COUNT; i++)
		free_page((unsigned long)phys_to_virt(rx_desc[i].buf_addr));

err_out:
	return err;
} 

void ixpdev_deinit(void)
{
	int i;

	/* @@@ Flush out pending packets.  */

	for (i = 0; i < nds_count; i++)
		unregister_netdev(nds[i]);

	ixp2000_uengine_stop_contexts(1, 0xff);
	ixp2000_uengine_stop_contexts(0, 0xff);
	ixp2000_uengine_reset(0x3);

	for (i = 0; i < TX_BUF_COUNT; i++)
		free_page((unsigned long)phys_to_virt(tx_desc[i].buf_addr));

	for (i = 0; i < RX_BUF_COUNT; i++)
		free_page((unsigned long)phys_to_virt(rx_desc[i].buf_addr));
}
