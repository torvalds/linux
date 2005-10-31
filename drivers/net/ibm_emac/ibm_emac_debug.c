/*
 * drivers/net/ibm_emac/ibm_emac_debug.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, debug print routines.
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/sysrq.h>
#include <asm/io.h>

#include "ibm_emac_core.h"

static void emac_desc_dump(int idx, struct ocp_enet_private *p)
{
	int i;
	printk("** EMAC%d TX BDs **\n"
	       " tx_cnt = %d tx_slot = %d ack_slot = %d\n",
	       idx, p->tx_cnt, p->tx_slot, p->ack_slot);
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

	printk("** EMAC%d RX BDs **\n"
	       " rx_slot = %d rx_stopped = %d rx_skb_size = %d rx_sync_size = %d\n"
	       " rx_sg_skb = 0x%p\n",
	       idx, p->rx_slot, p->commac.rx_stopped, p->rx_skb_size,
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

static void emac_mac_dump(int idx, struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;

	printk("** EMAC%d registers **\n"
	       "MR0 = 0x%08x MR1 = 0x%08x TMR0 = 0x%08x TMR1 = 0x%08x\n"
	       "RMR = 0x%08x ISR = 0x%08x ISER = 0x%08x\n"
	       "IAR = %04x%08x VTPID = 0x%04x VTCI = 0x%04x\n"
	       "IAHT: 0x%04x 0x%04x 0x%04x 0x%04x "
	       "GAHT: 0x%04x 0x%04x 0x%04x 0x%04x\n"
	       "LSA = %04x%08x IPGVR = 0x%04x\n"
	       "STACR = 0x%08x TRTR = 0x%08x RWMR = 0x%08x\n"
	       "OCTX = 0x%08x OCRX = 0x%08x IPCR = 0x%08x\n",
	       idx, in_be32(&p->mr0), in_be32(&p->mr1),
	       in_be32(&p->tmr0), in_be32(&p->tmr1),
	       in_be32(&p->rmr), in_be32(&p->isr), in_be32(&p->iser),
	       in_be32(&p->iahr), in_be32(&p->ialr), in_be32(&p->vtpid),
	       in_be32(&p->vtci),
	       in_be32(&p->iaht1), in_be32(&p->iaht2), in_be32(&p->iaht3),
	       in_be32(&p->iaht4),
	       in_be32(&p->gaht1), in_be32(&p->gaht2), in_be32(&p->gaht3),
	       in_be32(&p->gaht4),
	       in_be32(&p->lsah), in_be32(&p->lsal), in_be32(&p->ipgvr),
	       in_be32(&p->stacr), in_be32(&p->trtr), in_be32(&p->rwmr),
	       in_be32(&p->octx), in_be32(&p->ocrx), in_be32(&p->ipcr)
	    );

	emac_desc_dump(idx, dev);
}

static void emac_mal_dump(struct ibm_ocp_mal *mal)
{
	struct ocp_func_mal_data *maldata = mal->def->additions;
	int i;

	printk("** MAL%d Registers **\n"
	       "CFG = 0x%08x ESR = 0x%08x IER = 0x%08x\n"
	       "TX|CASR = 0x%08x CARR = 0x%08x EOBISR = 0x%08x DEIR = 0x%08x\n"
	       "RX|CASR = 0x%08x CARR = 0x%08x EOBISR = 0x%08x DEIR = 0x%08x\n",
	       mal->def->index,
	       get_mal_dcrn(mal, MAL_CFG), get_mal_dcrn(mal, MAL_ESR),
	       get_mal_dcrn(mal, MAL_IER),
	       get_mal_dcrn(mal, MAL_TXCASR), get_mal_dcrn(mal, MAL_TXCARR),
	       get_mal_dcrn(mal, MAL_TXEOBISR), get_mal_dcrn(mal, MAL_TXDEIR),
	       get_mal_dcrn(mal, MAL_RXCASR), get_mal_dcrn(mal, MAL_RXCARR),
	       get_mal_dcrn(mal, MAL_RXEOBISR), get_mal_dcrn(mal, MAL_RXDEIR)
	    );

	printk("TX|");
	for (i = 0; i < maldata->num_tx_chans; ++i) {
		if (i && !(i % 4))
			printk("\n   ");
		printk("CTP%d = 0x%08x ", i, get_mal_dcrn(mal, MAL_TXCTPR(i)));
	}
	printk("\nRX|");
	for (i = 0; i < maldata->num_rx_chans; ++i) {
		if (i && !(i % 4))
			printk("\n   ");
		printk("CTP%d = 0x%08x ", i, get_mal_dcrn(mal, MAL_RXCTPR(i)));
	}
	printk("\n   ");
	for (i = 0; i < maldata->num_rx_chans; ++i) {
		u32 r = get_mal_dcrn(mal, MAL_RCBS(i));
		if (i && !(i % 3))
			printk("\n   ");
		printk("RCBS%d = 0x%08x (%d) ", i, r, r * 16);
	}
	printk("\n");
}

static struct ocp_enet_private *__emacs[4];
static struct ibm_ocp_mal *__mals[1];

void emac_dbg_register(int idx, struct ocp_enet_private *dev)
{
	unsigned long flags;

	if (idx >= sizeof(__emacs) / sizeof(__emacs[0])) {
		printk(KERN_WARNING
		       "invalid index %d when registering EMAC for debugging\n",
		       idx);
		return;
	}

	local_irq_save(flags);
	__emacs[idx] = dev;
	local_irq_restore(flags);
}

void mal_dbg_register(int idx, struct ibm_ocp_mal *mal)
{
	unsigned long flags;

	if (idx >= sizeof(__mals) / sizeof(__mals[0])) {
		printk(KERN_WARNING
		       "invalid index %d when registering MAL for debugging\n",
		       idx);
		return;
	}

	local_irq_save(flags);
	__mals[idx] = mal;
	local_irq_restore(flags);
}

void emac_dbg_dump_all(void)
{
	unsigned int i;
	unsigned long flags;

	local_irq_save(flags);

	for (i = 0; i < sizeof(__mals) / sizeof(__mals[0]); ++i)
		if (__mals[i])
			emac_mal_dump(__mals[i]);

	for (i = 0; i < sizeof(__emacs) / sizeof(__emacs[0]); ++i)
		if (__emacs[i])
			emac_mac_dump(i, __emacs[i]);

	local_irq_restore(flags);
}

#if defined(CONFIG_MAGIC_SYSRQ)
static void emac_sysrq_handler(int key, struct pt_regs *pt_regs,
			       struct tty_struct *tty)
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
