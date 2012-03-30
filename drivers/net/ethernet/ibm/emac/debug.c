/*
 * drivers/net/ethernet/ibm/emac/debug.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, debug print routines.
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * Based on the arch/ppc version of the driver:
 *
 * Copyright (c) 2004, 2005 Zultys Technologies
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/sysrq.h>
#include <asm/io.h>

#include "core.h"

static DEFINE_SPINLOCK(emac_dbg_lock);

static void emac_desc_dump(struct emac_instance *p)
{
	int i;
	printk("** EMAC %s TX BDs **\n"
	       " tx_cnt = %d tx_slot = %d ack_slot = %d\n",
	       p->ofdev->dev.of_node->full_name,
	       p->tx_cnt, p->tx_slot, p->ack_slot);
	for (i = 0; i < NUM_TX_BUFF / 2; ++i)
		printk
		    ("bd[%2d] 0x%08x %c 0x%04x %4u - bd[%2d] 0x%08x %c 0x%04x %4u\n",
		     i, p->tx_desc[i].data_ptr, p->tx_skb[i] ? 'V' : ' ',
		     p->tx_desc[i].ctrl, p->tx_desc[i].data_len,
		     NUM_TX_BUFF / 2 + i,
		     p->tx_desc[NUM_TX_BUFF / 2 + i].data_ptr,
		     p->tx_skb[NUM_TX_BUFF / 2 + i] ? 'V' : ' ',
		     p->tx_desc[NUM_TX_BUFF / 2 + i].ctrl,
		     p->tx_desc[NUM_TX_BUFF / 2 + i].data_len);

	printk("** EMAC %s RX BDs **\n"
	       " rx_slot = %d flags = 0x%lx rx_skb_size = %d rx_sync_size = %d\n"
	       " rx_sg_skb = 0x%p\n",
	       p->ofdev->dev.of_node->full_name,
	       p->rx_slot, p->commac.flags, p->rx_skb_size,
	       p->rx_sync_size, p->rx_sg_skb);
	for (i = 0; i < NUM_RX_BUFF / 2; ++i)
		printk
		    ("bd[%2d] 0x%08x %c 0x%04x %4u - bd[%2d] 0x%08x %c 0x%04x %4u\n",
		     i, p->rx_desc[i].data_ptr, p->rx_skb[i] ? 'V' : ' ',
		     p->rx_desc[i].ctrl, p->rx_desc[i].data_len,
		     NUM_RX_BUFF / 2 + i,
		     p->rx_desc[NUM_RX_BUFF / 2 + i].data_ptr,
		     p->rx_skb[NUM_RX_BUFF / 2 + i] ? 'V' : ' ',
		     p->rx_desc[NUM_RX_BUFF / 2 + i].ctrl,
		     p->rx_desc[NUM_RX_BUFF / 2 + i].data_len);
}

static void emac_mac_dump(struct emac_instance *dev)
{
	struct emac_regs __iomem *p = dev->emacp;
	const int xaht_regs = EMAC_XAHT_REGS(dev);
	u32 *gaht_base = emac_gaht_base(dev);
	u32 *iaht_base = emac_iaht_base(dev);
	int emac4sync = emac_has_feature(dev, EMAC_FTR_EMAC4SYNC);
	int n;

	printk("** EMAC %s registers **\n"
	       "MR0 = 0x%08x MR1 = 0x%08x TMR0 = 0x%08x TMR1 = 0x%08x\n"
	       "RMR = 0x%08x ISR = 0x%08x ISER = 0x%08x\n"
	       "IAR = %04x%08x VTPID = 0x%04x VTCI = 0x%04x\n",
	       dev->ofdev->dev.of_node->full_name,
	       in_be32(&p->mr0), in_be32(&p->mr1),
	       in_be32(&p->tmr0), in_be32(&p->tmr1),
	       in_be32(&p->rmr), in_be32(&p->isr), in_be32(&p->iser),
	       in_be32(&p->iahr), in_be32(&p->ialr), in_be32(&p->vtpid),
	       in_be32(&p->vtci)
	       );

	if (emac4sync)
		printk("MAR = %04x%08x MMAR = %04x%08x\n",
		       in_be32(&p->u0.emac4sync.mahr),
		       in_be32(&p->u0.emac4sync.malr),
		       in_be32(&p->u0.emac4sync.mmahr),
		       in_be32(&p->u0.emac4sync.mmalr)
		       );

	for (n = 0; n < xaht_regs; n++)
		printk("IAHT%02d = 0x%08x\n", n + 1, in_be32(iaht_base + n));

	for (n = 0; n < xaht_regs; n++)
		printk("GAHT%02d = 0x%08x\n", n + 1, in_be32(gaht_base + n));

	printk("LSA = %04x%08x IPGVR = 0x%04x\n"
	       "STACR = 0x%08x TRTR = 0x%08x RWMR = 0x%08x\n"
	       "OCTX = 0x%08x OCRX = 0x%08x\n",
	       in_be32(&p->lsah), in_be32(&p->lsal), in_be32(&p->ipgvr),
	       in_be32(&p->stacr), in_be32(&p->trtr), in_be32(&p->rwmr),
	       in_be32(&p->octx), in_be32(&p->ocrx)
	       );

	if (!emac4sync) {
		printk("IPCR = 0x%08x\n",
		       in_be32(&p->u1.emac4.ipcr)
		       );
	} else {
		printk("REVID = 0x%08x TPC = 0x%08x\n",
		       in_be32(&p->u1.emac4sync.revid),
		       in_be32(&p->u1.emac4sync.tpc)
		       );
	}

	emac_desc_dump(dev);
}

static void emac_mal_dump(struct mal_instance *mal)
{
	int i;

	printk("** MAL %s Registers **\n"
	       "CFG = 0x%08x ESR = 0x%08x IER = 0x%08x\n"
	       "TX|CASR = 0x%08x CARR = 0x%08x EOBISR = 0x%08x DEIR = 0x%08x\n"
	       "RX|CASR = 0x%08x CARR = 0x%08x EOBISR = 0x%08x DEIR = 0x%08x\n",
	       mal->ofdev->dev.of_node->full_name,
	       get_mal_dcrn(mal, MAL_CFG), get_mal_dcrn(mal, MAL_ESR),
	       get_mal_dcrn(mal, MAL_IER),
	       get_mal_dcrn(mal, MAL_TXCASR), get_mal_dcrn(mal, MAL_TXCARR),
	       get_mal_dcrn(mal, MAL_TXEOBISR), get_mal_dcrn(mal, MAL_TXDEIR),
	       get_mal_dcrn(mal, MAL_RXCASR), get_mal_dcrn(mal, MAL_RXCARR),
	       get_mal_dcrn(mal, MAL_RXEOBISR), get_mal_dcrn(mal, MAL_RXDEIR)
	    );

	printk("TX|");
	for (i = 0; i < mal->num_tx_chans; ++i) {
		if (i && !(i % 4))
			printk("\n   ");
		printk("CTP%d = 0x%08x ", i, get_mal_dcrn(mal, MAL_TXCTPR(i)));
	}
	printk("\nRX|");
	for (i = 0; i < mal->num_rx_chans; ++i) {
		if (i && !(i % 4))
			printk("\n   ");
		printk("CTP%d = 0x%08x ", i, get_mal_dcrn(mal, MAL_RXCTPR(i)));
	}
	printk("\n   ");
	for (i = 0; i < mal->num_rx_chans; ++i) {
		u32 r = get_mal_dcrn(mal, MAL_RCBS(i));
		if (i && !(i % 3))
			printk("\n   ");
		printk("RCBS%d = 0x%08x (%d) ", i, r, r * 16);
	}
	printk("\n");
}

static struct emac_instance *__emacs[4];
static struct mal_instance *__mals[1];

void emac_dbg_register(struct emac_instance *dev)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&emac_dbg_lock, flags);
	for (i = 0; i < ARRAY_SIZE(__emacs); i++)
		if (__emacs[i] == NULL) {
			__emacs[i] = dev;
			break;
		}
	spin_unlock_irqrestore(&emac_dbg_lock, flags);
}

void emac_dbg_unregister(struct emac_instance *dev)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&emac_dbg_lock, flags);
	for (i = 0; i < ARRAY_SIZE(__emacs); i++)
		if (__emacs[i] == dev) {
			__emacs[i] = NULL;
			break;
		}
	spin_unlock_irqrestore(&emac_dbg_lock, flags);
}

void mal_dbg_register(struct mal_instance *mal)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&emac_dbg_lock, flags);
	for (i = 0; i < ARRAY_SIZE(__mals); i++)
		if (__mals[i] == NULL) {
			__mals[i] = mal;
			break;
		}
	spin_unlock_irqrestore(&emac_dbg_lock, flags);
}

void mal_dbg_unregister(struct mal_instance *mal)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&emac_dbg_lock, flags);
	for (i = 0; i < ARRAY_SIZE(__mals); i++)
		if (__mals[i] == mal) {
			__mals[i] = NULL;
			break;
		}
	spin_unlock_irqrestore(&emac_dbg_lock, flags);
}

void emac_dbg_dump_all(void)
{
	unsigned int i;
	unsigned long flags;

	spin_lock_irqsave(&emac_dbg_lock, flags);

	for (i = 0; i < ARRAY_SIZE(__mals); ++i)
		if (__mals[i])
			emac_mal_dump(__mals[i]);

	for (i = 0; i < ARRAY_SIZE(__emacs); ++i)
		if (__emacs[i])
			emac_mac_dump(__emacs[i]);

	spin_unlock_irqrestore(&emac_dbg_lock, flags);
}

#if defined(CONFIG_MAGIC_SYSRQ)
static void emac_sysrq_handler(int key)
{
	emac_dbg_dump_all();
}

static struct sysrq_key_op emac_sysrq_op = {
	.handler = emac_sysrq_handler,
	.help_msg = "emaC",
	.action_msg = "Show EMAC(s) status",
};

int __init emac_init_debug(void)
{
	return register_sysrq_key('c', &emac_sysrq_op);
}

void __exit emac_fini_debug(void)
{
	unregister_sysrq_key('c', &emac_sysrq_op);
}

#else
int __init emac_init_debug(void)
{
	return 0;
}
void __exit emac_fini_debug(void)
{
}
#endif				/* CONFIG_MAGIC_SYSRQ */
