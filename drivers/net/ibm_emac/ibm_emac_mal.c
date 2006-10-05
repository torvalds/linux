/*
 * drivers/net/ibm_emac/ibm_emac_mal.c
 *
 * Memory Access Layer (MAL) support
 * 
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Benjamin Herrenschmidt <benh@kernel.crashing.org>,
 *      David Gibson <hermes@gibson.dropbear.id.au>,
 *
 *      Armin Kuster <akuster@mvista.com>
 *      Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

#include <asm/ocp.h>

#include "ibm_emac_core.h"
#include "ibm_emac_mal.h"
#include "ibm_emac_debug.h"

int __init mal_register_commac(struct ibm_ocp_mal *mal,
			       struct mal_commac *commac)
{
	unsigned long flags;
	local_irq_save(flags);

	MAL_DBG("%d: reg(%08x, %08x)" NL, mal->def->index,
		commac->tx_chan_mask, commac->rx_chan_mask);

	/* Don't let multiple commacs claim the same channel(s) */
	if ((mal->tx_chan_mask & commac->tx_chan_mask) ||
	    (mal->rx_chan_mask & commac->rx_chan_mask)) {
		local_irq_restore(flags);
		printk(KERN_WARNING "mal%d: COMMAC channels conflict!\n",
		       mal->def->index);
		return -EBUSY;
	}

	mal->tx_chan_mask |= commac->tx_chan_mask;
	mal->rx_chan_mask |= commac->rx_chan_mask;
	list_add(&commac->list, &mal->list);

	local_irq_restore(flags);
	return 0;
}

void __exit mal_unregister_commac(struct ibm_ocp_mal *mal,
				  struct mal_commac *commac)
{
	unsigned long flags;
	local_irq_save(flags);

	MAL_DBG("%d: unreg(%08x, %08x)" NL, mal->def->index,
		commac->tx_chan_mask, commac->rx_chan_mask);

	mal->tx_chan_mask &= ~commac->tx_chan_mask;
	mal->rx_chan_mask &= ~commac->rx_chan_mask;
	list_del_init(&commac->list);

	local_irq_restore(flags);
}

int mal_set_rcbs(struct ibm_ocp_mal *mal, int channel, unsigned long size)
{
	struct ocp_func_mal_data *maldata = mal->def->additions;
	BUG_ON(channel < 0 || channel >= maldata->num_rx_chans ||
	       size > MAL_MAX_RX_SIZE);

	MAL_DBG("%d: set_rbcs(%d, %lu)" NL, mal->def->index, channel, size);

	if (size & 0xf) {
		printk(KERN_WARNING
		       "mal%d: incorrect RX size %lu for the channel %d\n",
		       mal->def->index, size, channel);
		return -EINVAL;
	}

	set_mal_dcrn(mal, MAL_RCBS(channel), size >> 4);
	return 0;
}

int mal_tx_bd_offset(struct ibm_ocp_mal *mal, int channel)
{
	struct ocp_func_mal_data *maldata = mal->def->additions;
	BUG_ON(channel < 0 || channel >= maldata->num_tx_chans);
	return channel * NUM_TX_BUFF;
}

int mal_rx_bd_offset(struct ibm_ocp_mal *mal, int channel)
{
	struct ocp_func_mal_data *maldata = mal->def->additions;
	BUG_ON(channel < 0 || channel >= maldata->num_rx_chans);
	return maldata->num_tx_chans * NUM_TX_BUFF + channel * NUM_RX_BUFF;
}

void mal_enable_tx_channel(struct ibm_ocp_mal *mal, int channel)
{
	local_bh_disable();
	MAL_DBG("%d: enable_tx(%d)" NL, mal->def->index, channel);
	set_mal_dcrn(mal, MAL_TXCASR,
		     get_mal_dcrn(mal, MAL_TXCASR) | MAL_CHAN_MASK(channel));
	local_bh_enable();
}

void mal_disable_tx_channel(struct ibm_ocp_mal *mal, int channel)
{
	set_mal_dcrn(mal, MAL_TXCARR, MAL_CHAN_MASK(channel));
	MAL_DBG("%d: disable_tx(%d)" NL, mal->def->index, channel);
}

void mal_enable_rx_channel(struct ibm_ocp_mal *mal, int channel)
{
	local_bh_disable();
	MAL_DBG("%d: enable_rx(%d)" NL, mal->def->index, channel);
	set_mal_dcrn(mal, MAL_RXCASR,
		     get_mal_dcrn(mal, MAL_RXCASR) | MAL_CHAN_MASK(channel));
	local_bh_enable();
}

void mal_disable_rx_channel(struct ibm_ocp_mal *mal, int channel)
{
	set_mal_dcrn(mal, MAL_RXCARR, MAL_CHAN_MASK(channel));
	MAL_DBG("%d: disable_rx(%d)" NL, mal->def->index, channel);
}

void mal_poll_add(struct ibm_ocp_mal *mal, struct mal_commac *commac)
{
	local_bh_disable();
	MAL_DBG("%d: poll_add(%p)" NL, mal->def->index, commac);
	list_add_tail(&commac->poll_list, &mal->poll_list);
	local_bh_enable();
}

void mal_poll_del(struct ibm_ocp_mal *mal, struct mal_commac *commac)
{
	local_bh_disable();
	MAL_DBG("%d: poll_del(%p)" NL, mal->def->index, commac);
	list_del(&commac->poll_list);
	local_bh_enable();
}

/* synchronized by mal_poll() */
static inline void mal_enable_eob_irq(struct ibm_ocp_mal *mal)
{
	MAL_DBG2("%d: enable_irq" NL, mal->def->index);
	set_mal_dcrn(mal, MAL_CFG, get_mal_dcrn(mal, MAL_CFG) | MAL_CFG_EOPIE);
}

/* synchronized by __LINK_STATE_RX_SCHED bit in ndev->state */
static inline void mal_disable_eob_irq(struct ibm_ocp_mal *mal)
{
	set_mal_dcrn(mal, MAL_CFG, get_mal_dcrn(mal, MAL_CFG) & ~MAL_CFG_EOPIE);
	MAL_DBG2("%d: disable_irq" NL, mal->def->index);
}

static irqreturn_t mal_serr(int irq, void *dev_instance)
{
	struct ibm_ocp_mal *mal = dev_instance;
	u32 esr = get_mal_dcrn(mal, MAL_ESR);

	/* Clear the error status register */
	set_mal_dcrn(mal, MAL_ESR, esr);

	MAL_DBG("%d: SERR %08x" NL, mal->def->index, esr);

	if (esr & MAL_ESR_EVB) {
		if (esr & MAL_ESR_DE) {
			/* We ignore Descriptor error,
			 * TXDE or RXDE interrupt will be generated anyway.
			 */
			return IRQ_HANDLED;
		}

		if (esr & MAL_ESR_PEIN) {
			/* PLB error, it's probably buggy hardware or
			 * incorrect physical address in BD (i.e. bug)
			 */
			if (net_ratelimit())
				printk(KERN_ERR
				       "mal%d: system error, PLB (ESR = 0x%08x)\n",
				       mal->def->index, esr);
			return IRQ_HANDLED;
		}

		/* OPB error, it's probably buggy hardware or incorrect EBC setup */
		if (net_ratelimit())
			printk(KERN_ERR
			       "mal%d: system error, OPB (ESR = 0x%08x)\n",
			       mal->def->index, esr);
	}
	return IRQ_HANDLED;
}

static inline void mal_schedule_poll(struct ibm_ocp_mal *mal)
{
	if (likely(netif_rx_schedule_prep(&mal->poll_dev))) {
		MAL_DBG2("%d: schedule_poll" NL, mal->def->index);
		mal_disable_eob_irq(mal);
		__netif_rx_schedule(&mal->poll_dev);
	} else
		MAL_DBG2("%d: already in poll" NL, mal->def->index);
}

static irqreturn_t mal_txeob(int irq, void *dev_instance)
{
	struct ibm_ocp_mal *mal = dev_instance;
	u32 r = get_mal_dcrn(mal, MAL_TXEOBISR);
	MAL_DBG2("%d: txeob %08x" NL, mal->def->index, r);
	mal_schedule_poll(mal);
	set_mal_dcrn(mal, MAL_TXEOBISR, r);
	return IRQ_HANDLED;
}

static irqreturn_t mal_rxeob(int irq, void *dev_instance)
{
	struct ibm_ocp_mal *mal = dev_instance;
	u32 r = get_mal_dcrn(mal, MAL_RXEOBISR);
	MAL_DBG2("%d: rxeob %08x" NL, mal->def->index, r);
	mal_schedule_poll(mal);
	set_mal_dcrn(mal, MAL_RXEOBISR, r);
	return IRQ_HANDLED;
}

static irqreturn_t mal_txde(int irq, void *dev_instance)
{
	struct ibm_ocp_mal *mal = dev_instance;
	u32 deir = get_mal_dcrn(mal, MAL_TXDEIR);
	set_mal_dcrn(mal, MAL_TXDEIR, deir);

	MAL_DBG("%d: txde %08x" NL, mal->def->index, deir);

	if (net_ratelimit())
		printk(KERN_ERR
		       "mal%d: TX descriptor error (TXDEIR = 0x%08x)\n",
		       mal->def->index, deir);

	return IRQ_HANDLED;
}

static irqreturn_t mal_rxde(int irq, void *dev_instance)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	u32 deir = get_mal_dcrn(mal, MAL_RXDEIR);

	MAL_DBG("%d: rxde %08x" NL, mal->def->index, deir);

	list_for_each(l, &mal->list) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);
		if (deir & mc->rx_chan_mask) {
			mc->rx_stopped = 1;
			mc->ops->rxde(mc->dev);
		}
	}

	mal_schedule_poll(mal);
	set_mal_dcrn(mal, MAL_RXDEIR, deir);

	return IRQ_HANDLED;
}

static int mal_poll(struct net_device *ndev, int *budget)
{
	struct ibm_ocp_mal *mal = ndev->priv;
	struct list_head *l;
	int rx_work_limit = min(ndev->quota, *budget), received = 0, done;

	MAL_DBG2("%d: poll(%d) %d ->" NL, mal->def->index, *budget,
		 rx_work_limit);
      again:
	/* Process TX skbs */
	list_for_each(l, &mal->poll_list) {
		struct mal_commac *mc =
		    list_entry(l, struct mal_commac, poll_list);
		mc->ops->poll_tx(mc->dev);
	}

	/* Process RX skbs.
	 * We _might_ need something more smart here to enforce polling fairness.
	 */
	list_for_each(l, &mal->poll_list) {
		struct mal_commac *mc =
		    list_entry(l, struct mal_commac, poll_list);
		int n = mc->ops->poll_rx(mc->dev, rx_work_limit);
		if (n) {
			received += n;
			rx_work_limit -= n;
			if (rx_work_limit <= 0) {
				done = 0;
				goto more_work;	// XXX What if this is the last one ?
			}
		}
	}

	/* We need to disable IRQs to protect from RXDE IRQ here */
	local_irq_disable();
	__netif_rx_complete(ndev);
	mal_enable_eob_irq(mal);
	local_irq_enable();

	done = 1;

	/* Check for "rotting" packet(s) */
	list_for_each(l, &mal->poll_list) {
		struct mal_commac *mc =
		    list_entry(l, struct mal_commac, poll_list);
		if (unlikely(mc->ops->peek_rx(mc->dev) || mc->rx_stopped)) {
			MAL_DBG2("%d: rotting packet" NL, mal->def->index);
			if (netif_rx_reschedule(ndev, received))
				mal_disable_eob_irq(mal);
			else
				MAL_DBG2("%d: already in poll list" NL,
					 mal->def->index);

			if (rx_work_limit > 0)
				goto again;
			else
				goto more_work;
		}
		mc->ops->poll_tx(mc->dev);
	}

      more_work:
	ndev->quota -= received;
	*budget -= received;

	MAL_DBG2("%d: poll() %d <- %d" NL, mal->def->index, *budget,
		 done ? 0 : 1);
	return done ? 0 : 1;
}

static void mal_reset(struct ibm_ocp_mal *mal)
{
	int n = 10;
	MAL_DBG("%d: reset" NL, mal->def->index);

	set_mal_dcrn(mal, MAL_CFG, MAL_CFG_SR);

	/* Wait for reset to complete (1 system clock) */
	while ((get_mal_dcrn(mal, MAL_CFG) & MAL_CFG_SR) && n)
		--n;

	if (unlikely(!n))
		printk(KERN_ERR "mal%d: reset timeout\n", mal->def->index);
}

int mal_get_regs_len(struct ibm_ocp_mal *mal)
{
	return sizeof(struct emac_ethtool_regs_subhdr) +
	    sizeof(struct ibm_mal_regs);
}

void *mal_dump_regs(struct ibm_ocp_mal *mal, void *buf)
{
	struct emac_ethtool_regs_subhdr *hdr = buf;
	struct ibm_mal_regs *regs = (struct ibm_mal_regs *)(hdr + 1);
	struct ocp_func_mal_data *maldata = mal->def->additions;
	int i;

	hdr->version = MAL_VERSION;
	hdr->index = mal->def->index;

	regs->tx_count = maldata->num_tx_chans;
	regs->rx_count = maldata->num_rx_chans;

	regs->cfg = get_mal_dcrn(mal, MAL_CFG);
	regs->esr = get_mal_dcrn(mal, MAL_ESR);
	regs->ier = get_mal_dcrn(mal, MAL_IER);
	regs->tx_casr = get_mal_dcrn(mal, MAL_TXCASR);
	regs->tx_carr = get_mal_dcrn(mal, MAL_TXCARR);
	regs->tx_eobisr = get_mal_dcrn(mal, MAL_TXEOBISR);
	regs->tx_deir = get_mal_dcrn(mal, MAL_TXDEIR);
	regs->rx_casr = get_mal_dcrn(mal, MAL_RXCASR);
	regs->rx_carr = get_mal_dcrn(mal, MAL_RXCARR);
	regs->rx_eobisr = get_mal_dcrn(mal, MAL_RXEOBISR);
	regs->rx_deir = get_mal_dcrn(mal, MAL_RXDEIR);

	for (i = 0; i < regs->tx_count; ++i)
		regs->tx_ctpr[i] = get_mal_dcrn(mal, MAL_TXCTPR(i));

	for (i = 0; i < regs->rx_count; ++i) {
		regs->rx_ctpr[i] = get_mal_dcrn(mal, MAL_RXCTPR(i));
		regs->rcbs[i] = get_mal_dcrn(mal, MAL_RCBS(i));
	}
	return regs + 1;
}

static int __init mal_probe(struct ocp_device *ocpdev)
{
	struct ibm_ocp_mal *mal;
	struct ocp_func_mal_data *maldata;
	int err = 0, i, bd_size;

	MAL_DBG("%d: probe" NL, ocpdev->def->index);

	maldata = ocpdev->def->additions;
	if (maldata == NULL) {
		printk(KERN_ERR "mal%d: missing additional data!\n",
		       ocpdev->def->index);
		return -ENODEV;
	}

	mal = kzalloc(sizeof(struct ibm_ocp_mal), GFP_KERNEL);
	if (!mal) {
		printk(KERN_ERR
		       "mal%d: out of memory allocating MAL structure!\n",
		       ocpdev->def->index);
		return -ENOMEM;
	}
	mal->dcrbase = maldata->dcr_base;
	mal->def = ocpdev->def;

	INIT_LIST_HEAD(&mal->poll_list);
	set_bit(__LINK_STATE_START, &mal->poll_dev.state);
	mal->poll_dev.weight = CONFIG_IBM_EMAC_POLL_WEIGHT;
	mal->poll_dev.poll = mal_poll;
	mal->poll_dev.priv = mal;
	atomic_set(&mal->poll_dev.refcnt, 1);

	INIT_LIST_HEAD(&mal->list);

	/* Load power-on reset defaults */
	mal_reset(mal);

	/* Set the MAL configuration register */
	set_mal_dcrn(mal, MAL_CFG, MAL_CFG_DEFAULT | MAL_CFG_PLBB |
		     MAL_CFG_OPBBL | MAL_CFG_LEA);

	mal_enable_eob_irq(mal);

	/* Allocate space for BD rings */
	BUG_ON(maldata->num_tx_chans <= 0 || maldata->num_tx_chans > 32);
	BUG_ON(maldata->num_rx_chans <= 0 || maldata->num_rx_chans > 32);
	bd_size = sizeof(struct mal_descriptor) *
	    (NUM_TX_BUFF * maldata->num_tx_chans +
	     NUM_RX_BUFF * maldata->num_rx_chans);
	mal->bd_virt =
	    dma_alloc_coherent(&ocpdev->dev, bd_size, &mal->bd_dma, GFP_KERNEL);

	if (!mal->bd_virt) {
		printk(KERN_ERR
		       "mal%d: out of memory allocating RX/TX descriptors!\n",
		       mal->def->index);
		err = -ENOMEM;
		goto fail;
	}
	memset(mal->bd_virt, 0, bd_size);

	for (i = 0; i < maldata->num_tx_chans; ++i)
		set_mal_dcrn(mal, MAL_TXCTPR(i), mal->bd_dma +
			     sizeof(struct mal_descriptor) *
			     mal_tx_bd_offset(mal, i));

	for (i = 0; i < maldata->num_rx_chans; ++i)
		set_mal_dcrn(mal, MAL_RXCTPR(i), mal->bd_dma +
			     sizeof(struct mal_descriptor) *
			     mal_rx_bd_offset(mal, i));

	err = request_irq(maldata->serr_irq, mal_serr, 0, "MAL SERR", mal);
	if (err)
		goto fail2;
	err = request_irq(maldata->txde_irq, mal_txde, 0, "MAL TX DE", mal);
	if (err)
		goto fail3;
	err = request_irq(maldata->txeob_irq, mal_txeob, 0, "MAL TX EOB", mal);
	if (err)
		goto fail4;
	err = request_irq(maldata->rxde_irq, mal_rxde, 0, "MAL RX DE", mal);
	if (err)
		goto fail5;
	err = request_irq(maldata->rxeob_irq, mal_rxeob, 0, "MAL RX EOB", mal);
	if (err)
		goto fail6;

	/* Enable all MAL SERR interrupt sources */
	set_mal_dcrn(mal, MAL_IER, MAL_IER_EVENTS);

	/* Advertise this instance to the rest of the world */
	ocp_set_drvdata(ocpdev, mal);

	mal_dbg_register(mal->def->index, mal);

	printk(KERN_INFO "mal%d: initialized, %d TX channels, %d RX channels\n",
	       mal->def->index, maldata->num_tx_chans, maldata->num_rx_chans);
	return 0;

      fail6:
	free_irq(maldata->rxde_irq, mal);
      fail5:
	free_irq(maldata->txeob_irq, mal);
      fail4:
	free_irq(maldata->txde_irq, mal);
      fail3:
	free_irq(maldata->serr_irq, mal);
      fail2:
	dma_free_coherent(&ocpdev->dev, bd_size, mal->bd_virt, mal->bd_dma);
      fail:
	kfree(mal);
	return err;
}

static void __exit mal_remove(struct ocp_device *ocpdev)
{
	struct ibm_ocp_mal *mal = ocp_get_drvdata(ocpdev);
	struct ocp_func_mal_data *maldata = mal->def->additions;

	MAL_DBG("%d: remove" NL, mal->def->index);

	/* Syncronize with scheduled polling, 
	   stolen from net/core/dev.c:dev_close() 
	 */
	clear_bit(__LINK_STATE_START, &mal->poll_dev.state);
	netif_poll_disable(&mal->poll_dev);

	if (!list_empty(&mal->list)) {
		/* This is *very* bad */
		printk(KERN_EMERG
		       "mal%d: commac list is not empty on remove!\n",
		       mal->def->index);
	}

	ocp_set_drvdata(ocpdev, NULL);

	free_irq(maldata->serr_irq, mal);
	free_irq(maldata->txde_irq, mal);
	free_irq(maldata->txeob_irq, mal);
	free_irq(maldata->rxde_irq, mal);
	free_irq(maldata->rxeob_irq, mal);

	mal_reset(mal);

	mal_dbg_register(mal->def->index, NULL);

	dma_free_coherent(&ocpdev->dev,
			  sizeof(struct mal_descriptor) *
			  (NUM_TX_BUFF * maldata->num_tx_chans +
			   NUM_RX_BUFF * maldata->num_rx_chans), mal->bd_virt,
			  mal->bd_dma);

	kfree(mal);
}

/* Structure for a device driver */
static struct ocp_device_id mal_ids[] = {
	{ .vendor = OCP_VENDOR_IBM, .function = OCP_FUNC_MAL },
	{ .vendor = OCP_VENDOR_INVALID}
};

static struct ocp_driver mal_driver = {
	.name = "mal",
	.id_table = mal_ids,

	.probe = mal_probe,
	.remove = mal_remove,
};

int __init mal_init(void)
{
	MAL_DBG(": init" NL);
	return ocp_register_driver(&mal_driver);
}

void __exit mal_exit(void)
{
	MAL_DBG(": exit" NL);
	ocp_unregister_driver(&mal_driver);
}
