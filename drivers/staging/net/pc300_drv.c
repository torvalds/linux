#define	USE_PCI_CLOCK
static const char rcsid[] =
"Revision: 3.4.5 Date: 2002/03/07 ";

/*
 * pc300.c	Cyclades-PC300(tm) Driver.
 *
 * Author:	Ivan Passos <ivan@cyclades.com>
 * Maintainer:	PC300 Maintainer <pc300@cyclades.com>
 *
 * Copyright:	(c) 1999-2003 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Using tabstop = 4.
 * 
 * $Log: pc300_drv.c,v $
 * Revision 3.23  2002/03/20 13:58:40  henrique
 * Fixed ortographic mistakes
 *
 * Revision 3.22  2002/03/13 16:56:56  henrique
 * Take out the debug messages
 *
 * Revision 3.21  2002/03/07 14:17:09  henrique
 * License data fixed
 *
 * Revision 3.20  2002/01/17 17:58:52  ivan
 * Support for PC300-TE/M (PMC).
 *
 * Revision 3.19  2002/01/03 17:08:47  daniela
 * Enables DMA reception when the SCA-II disables it improperly.
 *
 * Revision 3.18  2001/12/03 18:47:50  daniela
 * Esthetic changes.
 *
 * Revision 3.17  2001/10/19 16:50:13  henrique
 * Patch to kernel 2.4.12 and new generic hdlc.
 *
 * Revision 3.16  2001/10/16 15:12:31  regina
 * clear statistics
 *
 * Revision 3.11 to 3.15  2001/10/11 20:26:04  daniela
 * More DMA fixes for noisy lines.
 * Return the size of bad frames in dma_get_rx_frame_size, so that the Rx buffer
 * descriptors can be cleaned by dma_buf_read (called in cpc_net_rx).
 * Renamed dma_start routine to rx_dma_start. Improved Rx statistics.
 * Fixed BOF interrupt treatment. Created dma_start routine.
 * Changed min and max to cpc_min and cpc_max.
 *
 * Revision 3.10  2001/08/06 12:01:51  regina
 * Fixed problem in DSR_DE bit.
 *
 * Revision 3.9  2001/07/18 19:27:26  daniela
 * Added some history comments.
 *
 * Revision 3.8  2001/07/12 13:11:19  regina
 * bug fix - DCD-OFF in pc300 tty driver
 *
 * Revision 3.3 to 3.7  2001/07/06 15:00:20  daniela
 * Removing kernel 2.4.3 and previous support.
 * DMA transmission bug fix.
 * MTU check in cpc_net_rx fixed.
 * Boot messages reviewed.
 * New configuration parameters (line code, CRC calculation and clock).
 *
 * Revision 3.2 2001/06/22 13:13:02  regina
 * MLPPP implementation. Changed the header of message trace to include
 * the device name. New format : "hdlcX[R/T]: ".
 * Default configuration changed.
 *
 * Revision 3.1 2001/06/15 regina
 * in cpc_queue_xmit, netif_stop_queue is called if don't have free descriptor
 * upping major version number
 *
 * Revision 1.1.1.1  2001/06/13 20:25:04  daniela
 * PC300 initial CVS version (3.4.0-pre1)
 *
 * Revision 3.0.1.2 2001/06/08 daniela
 * Did some changes in the DMA programming implementation to avoid the 
 * occurrence of a SCA-II bug when CDA is accessed during a DMA transfer.
 *
 * Revision 3.0.1.1 2001/05/02 daniela
 * Added kernel 2.4.3 support.
 * 
 * Revision 3.0.1.0 2001/03/13 daniela, henrique
 * Added Frame Relay Support.
 * Driver now uses HDLC generic driver to provide protocol support.
 * 
 * Revision 3.0.0.8 2001/03/02 daniela
 * Fixed ram size detection. 
 * Changed SIOCGPC300CONF ioctl, to give hw information to pc300util.
 * 
 * Revision 3.0.0.7 2001/02/23 daniela
 * netif_stop_queue called before the SCA-II transmition commands in 
 * cpc_queue_xmit, and with interrupts disabled to avoid race conditions with 
 * transmition interrupts.
 * Fixed falc_check_status for Unframed E1.
 * 
 * Revision 3.0.0.6 2000/12/13 daniela
 * Implemented pc300util support: trace, statistics, status and loopback
 * tests for the PC300 TE boards.
 * 
 * Revision 3.0.0.5 2000/12/12 ivan
 * Added support for Unframed E1.
 * Implemented monitor mode.
 * Fixed DCD sensitivity on the second channel.
 * Driver now complies with new PCI kernel architecture.
 *
 * Revision 3.0.0.4 2000/09/28 ivan
 * Implemented DCD sensitivity.
 * Moved hardware-specific open to the end of cpc_open, to avoid race
 * conditions with early reception interrupts.
 * Included code for [request|release]_mem_region().
 * Changed location of pc300.h .
 * Minor code revision (contrib. of Jeff Garzik).
 *
 * Revision 3.0.0.3 2000/07/03 ivan
 * Previous bugfix for the framing errors with external clock made X21
 * boards stop working. This version fixes it.
 *
 * Revision 3.0.0.2 2000/06/23 ivan
 * Revisited cpc_queue_xmit to prevent race conditions on Tx DMA buffer
 * handling when Tx timeouts occur.
 * Revisited Rx statistics.
 * Fixed a bug in the SCA-II programming that would cause framing errors
 * when external clock was configured.
 *
 * Revision 3.0.0.1 2000/05/26 ivan
 * Added logic in the SCA interrupt handler so that no board can monopolize
 * the driver.
 * Request PLX I/O region, although driver doesn't use it, to avoid
 * problems with other drivers accessing it.
 *
 * Revision 3.0.0.0 2000/05/15 ivan
 * Did some changes in the DMA programming implementation to avoid the
 * occurrence of a SCA-II bug in the second channel.
 * Implemented workaround for PLX9050 bug that would cause a system lockup
 * in certain systems, depending on the MMIO addresses allocated to the
 * board.
 * Fixed the FALC chip programming to avoid synchronization problems in the
 * second channel (TE only).
 * Implemented a cleaner and faster Tx DMA descriptor cleanup procedure in
 * cpc_queue_xmit().
 * Changed the built-in driver implementation so that the driver can use the
 * general 'hdlcN' naming convention instead of proprietary device names.
 * Driver load messages are now device-centric, instead of board-centric.
 * Dynamic allocation of net_device structures.
 * Code is now compliant with the new module interface (module_[init|exit]).
 * Make use of the PCI helper functions to access PCI resources.
 *
 * Revision 2.0.0.0 2000/04/15 ivan
 * Added support for the PC300/TE boards (T1/FT1/E1/FE1).
 *
 * Revision 1.1.0.0 2000/02/28 ivan
 * Major changes in the driver architecture.
 * Softnet compliancy implemented.
 * Driver now reports physical instead of virtual memory addresses.
 * Added cpc_change_mtu function.
 *
 * Revision 1.0.0.0 1999/12/16 ivan
 * First official release.
 * Support for 1- and 2-channel boards (which use distinct PCI Device ID's).
 * Support for monolythic installation (i.e., drv built into the kernel).
 * X.25 additional checking when lapb_[dis]connect_request returns an error.
 * SCA programming now covers X.21 as well.
 *
 * Revision 0.3.1.0 1999/11/18 ivan
 * Made X.25 support configuration-dependent (as it depends on external 
 * modules to work).
 * Changed X.25-specific function names to comply with adopted convention.
 * Fixed typos in X.25 functions that would cause compile errors (Daniela).
 * Fixed bug in ch_config that would disable interrupts on a previously 
 * enabled channel if the other channel on the same board was enabled later.
 *
 * Revision 0.3.0.0 1999/11/16 daniela
 * X.25 support.
 *
 * Revision 0.2.3.0 1999/11/15 ivan
 * Function cpc_ch_status now provides more detailed information.
 * Added support for X.21 clock configuration.
 * Changed TNR1 setting in order to prevent Tx FIFO overaccesses by the SCA.
 * Now using PCI clock instead of internal oscillator clock for the SCA.
 *
 * Revision 0.2.2.0 1999/11/10 ivan
 * Changed the *_dma_buf_check functions so that they would print only 
 * the useful info instead of the whole buffer descriptor bank.
 * Fixed bug in cpc_queue_xmit that would eventually crash the system 
 * in case of a packet drop.
 * Implemented TX underrun handling.
 * Improved SCA fine tuning to boost up its performance.
 *
 * Revision 0.2.1.0 1999/11/03 ivan
 * Added functions *dma_buf_pt_init to allow independent initialization 
 * of the next-descr. and DMA buffer pointers on the DMA descriptors.
 * Kernel buffer release and tbusy clearing is now done in the interrupt 
 * handler.
 * Fixed bug in cpc_open that would cause an interface reopen to fail.
 * Added a protocol-specific code section in cpc_net_rx.
 * Removed printk level defs (they might be added back after the beta phase).
 *
 * Revision 0.2.0.0 1999/10/28 ivan
 * Revisited the code so that new protocols can be easily added / supported. 
 *
 * Revision 0.1.0.1 1999/10/20 ivan
 * Mostly "esthetic" changes.
 *
 * Revision 0.1.0.0 1999/10/11 ivan
 * Initial version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/if.h>
#include <linux/slab.h>
#include <net/arp.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "pc300.h"

#define	CPC_LOCK(card,flags)		\
		do {						\
		spin_lock_irqsave(&card->card_lock, flags);	\
		} while (0)

#define CPC_UNLOCK(card,flags)			\
		do {							\
		spin_unlock_irqrestore(&card->card_lock, flags);	\
		} while (0)

#undef	PC300_DEBUG_PCI
#undef	PC300_DEBUG_INTR
#undef	PC300_DEBUG_TX
#undef	PC300_DEBUG_RX
#undef	PC300_DEBUG_OTHER

static DEFINE_PCI_DEVICE_TABLE(cpc_pci_dev_id) = {
	/* PC300/RSV or PC300/X21, 2 chan */
	{0x120e, 0x300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x300},
	/* PC300/RSV or PC300/X21, 1 chan */
	{0x120e, 0x301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x301},
	/* PC300/TE, 2 chan */
	{0x120e, 0x310, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x310},
	/* PC300/TE, 1 chan */
	{0x120e, 0x311, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x311},
	/* PC300/TE-M, 2 chan */
	{0x120e, 0x320, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x320},
	/* PC300/TE-M, 1 chan */
	{0x120e, 0x321, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0x321},
	/* End of table */
	{0,},
};
MODULE_DEVICE_TABLE(pci, cpc_pci_dev_id);

#ifndef cpc_min
#define	cpc_min(a,b)	(((a)<(b))?(a):(b))
#endif
#ifndef cpc_max
#define	cpc_max(a,b)	(((a)>(b))?(a):(b))
#endif

/* prototypes */
static void tx_dma_buf_pt_init(pc300_t *, int);
static void tx_dma_buf_init(pc300_t *, int);
static void rx_dma_buf_pt_init(pc300_t *, int);
static void rx_dma_buf_init(pc300_t *, int);
static void tx_dma_buf_check(pc300_t *, int);
static void rx_dma_buf_check(pc300_t *, int);
static irqreturn_t cpc_intr(int, void *);
static int clock_rate_calc(u32, u32, int *);
static u32 detect_ram(pc300_t *);
static void plx_init(pc300_t *);
static void cpc_trace(struct net_device *, struct sk_buff *, char);
static int cpc_attach(struct net_device *, unsigned short, unsigned short);
static int cpc_close(struct net_device *dev);

#ifdef CONFIG_PC300_MLPPP
void cpc_tty_init(pc300dev_t * dev);
void cpc_tty_unregister_service(pc300dev_t * pc300dev);
void cpc_tty_receive(pc300dev_t * pc300dev);
void cpc_tty_trigger_poll(pc300dev_t * pc300dev);
#endif

/************************/
/***   DMA Routines   ***/
/************************/
static void tx_dma_buf_pt_init(pc300_t * card, int ch)
{
	int i;
	int ch_factor = ch * N_DMA_TX_BUF;
	volatile pcsca_bd_t __iomem *ptdescr = (card->hw.rambase
			               + DMA_TX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

	for (i = 0; i < N_DMA_TX_BUF; i++, ptdescr++) {
		cpc_writel(&ptdescr->next, (u32)(DMA_TX_BD_BASE +
			(ch_factor + ((i + 1) & (N_DMA_TX_BUF - 1))) * sizeof(pcsca_bd_t)));
		cpc_writel(&ptdescr->ptbuf,
			   (u32)(DMA_TX_BASE + (ch_factor + i) * BD_DEF_LEN));
	}
}

static void tx_dma_buf_init(pc300_t * card, int ch)
{
	int i;
	int ch_factor = ch * N_DMA_TX_BUF;
	volatile pcsca_bd_t __iomem *ptdescr = (card->hw.rambase
			       + DMA_TX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

	for (i = 0; i < N_DMA_TX_BUF; i++, ptdescr++) {
		memset_io(ptdescr, 0, sizeof(pcsca_bd_t));
		cpc_writew(&ptdescr->len, 0);
		cpc_writeb(&ptdescr->status, DST_OSB);
	}
	tx_dma_buf_pt_init(card, ch);
}

static void rx_dma_buf_pt_init(pc300_t * card, int ch)
{
	int i;
	int ch_factor = ch * N_DMA_RX_BUF;
	volatile pcsca_bd_t __iomem *ptdescr = (card->hw.rambase
				       + DMA_RX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

	for (i = 0; i < N_DMA_RX_BUF; i++, ptdescr++) {
		cpc_writel(&ptdescr->next, (u32)(DMA_RX_BD_BASE +
			(ch_factor + ((i + 1) & (N_DMA_RX_BUF - 1))) * sizeof(pcsca_bd_t)));
		cpc_writel(&ptdescr->ptbuf,
			   (u32)(DMA_RX_BASE + (ch_factor + i) * BD_DEF_LEN));
	}
}

static void rx_dma_buf_init(pc300_t * card, int ch)
{
	int i;
	int ch_factor = ch * N_DMA_RX_BUF;
	volatile pcsca_bd_t __iomem *ptdescr = (card->hw.rambase
				       + DMA_RX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

	for (i = 0; i < N_DMA_RX_BUF; i++, ptdescr++) {
		memset_io(ptdescr, 0, sizeof(pcsca_bd_t));
		cpc_writew(&ptdescr->len, 0);
		cpc_writeb(&ptdescr->status, 0);
	}
	rx_dma_buf_pt_init(card, ch);
}

static void tx_dma_buf_check(pc300_t * card, int ch)
{
	volatile pcsca_bd_t __iomem *ptdescr;
	int i;
	u16 first_bd = card->chan[ch].tx_first_bd;
	u16 next_bd = card->chan[ch].tx_next_bd;

	printk("#CH%d: f_bd = %d(0x%08zx), n_bd = %d(0x%08zx)\n", ch,
	       first_bd, TX_BD_ADDR(ch, first_bd),
	       next_bd, TX_BD_ADDR(ch, next_bd));
	for (i = first_bd,
	     ptdescr = (card->hw.rambase + TX_BD_ADDR(ch, first_bd));
	     i != ((next_bd + 1) & (N_DMA_TX_BUF - 1));
	     i = (i + 1) & (N_DMA_TX_BUF - 1), 
		 ptdescr = (card->hw.rambase + TX_BD_ADDR(ch, i))) {
		printk("\n CH%d TX%d: next=0x%x, ptbuf=0x%x, ST=0x%x, len=%d",
		       ch, i, cpc_readl(&ptdescr->next),
		       cpc_readl(&ptdescr->ptbuf),
		       cpc_readb(&ptdescr->status), cpc_readw(&ptdescr->len));
	}
	printk("\n");
}

#ifdef	PC300_DEBUG_OTHER
/* Show all TX buffer descriptors */
static void tx1_dma_buf_check(pc300_t * card, int ch)
{
	volatile pcsca_bd_t __iomem *ptdescr;
	int i;
	u16 first_bd = card->chan[ch].tx_first_bd;
	u16 next_bd = card->chan[ch].tx_next_bd;
	u32 scabase = card->hw.scabase;

	printk ("\nnfree_tx_bd = %d\n", card->chan[ch].nfree_tx_bd);
	printk("#CH%d: f_bd = %d(0x%08x), n_bd = %d(0x%08x)\n", ch,
	       first_bd, TX_BD_ADDR(ch, first_bd),
	       next_bd, TX_BD_ADDR(ch, next_bd));
	printk("TX_CDA=0x%08x, TX_EDA=0x%08x\n",
	       cpc_readl(scabase + DTX_REG(CDAL, ch)),
	       cpc_readl(scabase + DTX_REG(EDAL, ch)));
	for (i = 0; i < N_DMA_TX_BUF; i++) {
		ptdescr = (card->hw.rambase + TX_BD_ADDR(ch, i));
		printk("\n CH%d TX%d: next=0x%x, ptbuf=0x%x, ST=0x%x, len=%d",
		       ch, i, cpc_readl(&ptdescr->next),
		       cpc_readl(&ptdescr->ptbuf),
		       cpc_readb(&ptdescr->status), cpc_readw(&ptdescr->len));
	}
	printk("\n");
}
#endif

static void rx_dma_buf_check(pc300_t * card, int ch)
{
	volatile pcsca_bd_t __iomem *ptdescr;
	int i;
	u16 first_bd = card->chan[ch].rx_first_bd;
	u16 last_bd = card->chan[ch].rx_last_bd;
	int ch_factor;

	ch_factor = ch * N_DMA_RX_BUF;
	printk("#CH%d: f_bd = %d, l_bd = %d\n", ch, first_bd, last_bd);
	for (i = 0, ptdescr = (card->hw.rambase +
					      DMA_RX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));
	     i < N_DMA_RX_BUF; i++, ptdescr++) {
		if (cpc_readb(&ptdescr->status) & DST_OSB)
			printk ("\n CH%d RX%d: next=0x%x, ptbuf=0x%x, ST=0x%x, len=%d",
				 ch, i, cpc_readl(&ptdescr->next),
				 cpc_readl(&ptdescr->ptbuf),
				 cpc_readb(&ptdescr->status),
				 cpc_readw(&ptdescr->len));
	}
	printk("\n");
}

static int dma_get_rx_frame_size(pc300_t * card, int ch)
{
	volatile pcsca_bd_t __iomem *ptdescr;
	u16 first_bd = card->chan[ch].rx_first_bd;
	int rcvd = 0;
	volatile u8 status;

	ptdescr = (card->hw.rambase + RX_BD_ADDR(ch, first_bd));
	while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
		rcvd += cpc_readw(&ptdescr->len);
		first_bd = (first_bd + 1) & (N_DMA_RX_BUF - 1);
		if ((status & DST_EOM) || (first_bd == card->chan[ch].rx_last_bd)) {
			/* Return the size of a good frame or incomplete bad frame 
			* (dma_buf_read will clean the buffer descriptors in this case). */
			return rcvd;
		}
		ptdescr = (card->hw.rambase + cpc_readl(&ptdescr->next));
	}
	return -1;
}

/*
 * dma_buf_write: writes a frame to the Tx DMA buffers
 * NOTE: this function writes one frame at a time.
 */
static int dma_buf_write(pc300_t *card, int ch, u8 *ptdata, int len)
{
	int i, nchar;
	volatile pcsca_bd_t __iomem *ptdescr;
	int tosend = len;
	u8 nbuf = ((len - 1) / BD_DEF_LEN) + 1;

	if (nbuf >= card->chan[ch].nfree_tx_bd) {
		return -ENOMEM;
	}

	for (i = 0; i < nbuf; i++) {
		ptdescr = (card->hw.rambase +
					  TX_BD_ADDR(ch, card->chan[ch].tx_next_bd));
		nchar = cpc_min(BD_DEF_LEN, tosend);
		if (cpc_readb(&ptdescr->status) & DST_OSB) {
			memcpy_toio((card->hw.rambase + cpc_readl(&ptdescr->ptbuf)),
				    &ptdata[len - tosend], nchar);
			cpc_writew(&ptdescr->len, nchar);
			card->chan[ch].nfree_tx_bd--;
			if ((i + 1) == nbuf) {
				/* This must be the last BD to be used */
				cpc_writeb(&ptdescr->status, DST_EOM);
			} else {
				cpc_writeb(&ptdescr->status, 0);
			}
		} else {
			return -ENOMEM;
		}
		tosend -= nchar;
		card->chan[ch].tx_next_bd =
			(card->chan[ch].tx_next_bd + 1) & (N_DMA_TX_BUF - 1);
	}
	/* If it gets to here, it means we have sent the whole frame */
	return 0;
}

/*
 * dma_buf_read: reads a frame from the Rx DMA buffers
 * NOTE: this function reads one frame at a time.
 */
static int dma_buf_read(pc300_t * card, int ch, struct sk_buff *skb)
{
	int nchar;
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	volatile pcsca_bd_t __iomem *ptdescr;
	int rcvd = 0;
	volatile u8 status;

	ptdescr = (card->hw.rambase +
				  RX_BD_ADDR(ch, chan->rx_first_bd));
	while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
		nchar = cpc_readw(&ptdescr->len);
		if ((status & (DST_OVR | DST_CRC | DST_RBIT | DST_SHRT | DST_ABT)) ||
		    (nchar > BD_DEF_LEN)) {

			if (nchar > BD_DEF_LEN)
				status |= DST_RBIT;
			rcvd = -status;
			/* Discard remaining descriptors used by the bad frame */
			while (chan->rx_first_bd != chan->rx_last_bd) {
				cpc_writeb(&ptdescr->status, 0);
				chan->rx_first_bd = (chan->rx_first_bd+1) & (N_DMA_RX_BUF-1);
				if (status & DST_EOM)
					break;
				ptdescr = (card->hw.rambase +
							  cpc_readl(&ptdescr->next));
				status = cpc_readb(&ptdescr->status);
			}
			break;
		}
		if (nchar != 0) {
			if (skb) {
				memcpy_fromio(skb_put(skb, nchar),
				 (card->hw.rambase+cpc_readl(&ptdescr->ptbuf)),nchar);
			}
			rcvd += nchar;
		}
		cpc_writeb(&ptdescr->status, 0);
		cpc_writeb(&ptdescr->len, 0);
		chan->rx_first_bd = (chan->rx_first_bd + 1) & (N_DMA_RX_BUF - 1);

		if (status & DST_EOM)
			break;

		ptdescr = (card->hw.rambase + cpc_readl(&ptdescr->next));
	}

	if (rcvd != 0) {
		/* Update pointer */
		chan->rx_last_bd = (chan->rx_first_bd - 1) & (N_DMA_RX_BUF - 1);
		/* Update EDA */
		cpc_writel(card->hw.scabase + DRX_REG(EDAL, ch),
			   RX_BD_ADDR(ch, chan->rx_last_bd));
	}
	return rcvd;
}

static void tx_dma_stop(pc300_t * card, int ch)
{
	void __iomem *scabase = card->hw.scabase;
	u8 drr_ena_bit = 1 << (5 + 2 * ch);
	u8 drr_rst_bit = 1 << (1 + 2 * ch);

	/* Disable DMA */
	cpc_writeb(scabase + DRR, drr_ena_bit);
	cpc_writeb(scabase + DRR, drr_rst_bit & ~drr_ena_bit);
}

static void rx_dma_stop(pc300_t * card, int ch)
{
	void __iomem *scabase = card->hw.scabase;
	u8 drr_ena_bit = 1 << (4 + 2 * ch);
	u8 drr_rst_bit = 1 << (2 * ch);

	/* Disable DMA */
	cpc_writeb(scabase + DRR, drr_ena_bit);
	cpc_writeb(scabase + DRR, drr_rst_bit & ~drr_ena_bit);
}

static void rx_dma_start(pc300_t * card, int ch)
{
	void __iomem *scabase = card->hw.scabase;
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	
	/* Start DMA */
	cpc_writel(scabase + DRX_REG(CDAL, ch),
		   RX_BD_ADDR(ch, chan->rx_first_bd));
	if (cpc_readl(scabase + DRX_REG(CDAL,ch)) !=
				  RX_BD_ADDR(ch, chan->rx_first_bd)) {
		cpc_writel(scabase + DRX_REG(CDAL, ch),
				   RX_BD_ADDR(ch, chan->rx_first_bd));
	}
	cpc_writel(scabase + DRX_REG(EDAL, ch),
		   RX_BD_ADDR(ch, chan->rx_last_bd));
	cpc_writew(scabase + DRX_REG(BFLL, ch), BD_DEF_LEN);
	cpc_writeb(scabase + DSR_RX(ch), DSR_DE);
	if (!(cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
	cpc_writeb(scabase + DSR_RX(ch), DSR_DE);
	}
}

/*************************/
/***   FALC Routines   ***/
/*************************/
static void falc_issue_cmd(pc300_t *card, int ch, u8 cmd)
{
	void __iomem *falcbase = card->hw.falcbase;
	unsigned long i = 0;

	while (cpc_readb(falcbase + F_REG(SIS, ch)) & SIS_CEC) {
		if (i++ >= PC300_FALC_MAXLOOP) {
			printk("%s: FALC command locked(cmd=0x%x).\n",
			       card->chan[ch].d.name, cmd);
			break;
		}
	}
	cpc_writeb(falcbase + F_REG(CMDR, ch), cmd);
}

static void falc_intr_enable(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	/* Interrupt pins are open-drain */
	cpc_writeb(falcbase + F_REG(IPC, ch),
		   cpc_readb(falcbase + F_REG(IPC, ch)) & ~IPC_IC0);
	/* Conters updated each second */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_ECM);
	/* Enable SEC and ES interrupts  */
	cpc_writeb(falcbase + F_REG(IMR3, ch),
		   cpc_readb(falcbase + F_REG(IMR3, ch)) & ~(IMR3_SEC | IMR3_ES));
	if (conf->fr_mode == PC300_FR_UNFRAMED) {
		cpc_writeb(falcbase + F_REG(IMR4, ch),
			   cpc_readb(falcbase + F_REG(IMR4, ch)) & ~(IMR4_LOS));
	} else {
		cpc_writeb(falcbase + F_REG(IMR4, ch),
			   cpc_readb(falcbase + F_REG(IMR4, ch)) &
			   ~(IMR4_LFA | IMR4_AIS | IMR4_LOS | IMR4_SLIP));
	}
	if (conf->media == IF_IFACE_T1) {
		cpc_writeb(falcbase + F_REG(IMR3, ch),
			   cpc_readb(falcbase + F_REG(IMR3, ch)) & ~IMR3_LLBSC);
	} else {
		cpc_writeb(falcbase + F_REG(IPC, ch),
			   cpc_readb(falcbase + F_REG(IPC, ch)) | IPC_SCI);
		if (conf->fr_mode == PC300_FR_UNFRAMED) {
			cpc_writeb(falcbase + F_REG(IMR2, ch),
				   cpc_readb(falcbase + F_REG(IMR2, ch)) & ~(IMR2_LOS));
		} else {
			cpc_writeb(falcbase + F_REG(IMR2, ch),
				   cpc_readb(falcbase + F_REG(IMR2, ch)) &
				   ~(IMR2_FAR | IMR2_LFA | IMR2_AIS | IMR2_LOS));
			if (pfalc->multiframe_mode) {
				cpc_writeb(falcbase + F_REG(IMR2, ch),
					   cpc_readb(falcbase + F_REG(IMR2, ch)) & 
					   ~(IMR2_T400MS | IMR2_MFAR));
			} else {
				cpc_writeb(falcbase + F_REG(IMR2, ch),
					   cpc_readb(falcbase + F_REG(IMR2, ch)) | 
					   IMR2_T400MS | IMR2_MFAR);
			}
		}
	}
}

static void falc_open_timeslot(pc300_t * card, int ch, int timeslot)
{
	void __iomem *falcbase = card->hw.falcbase;
	u8 tshf = card->chan[ch].falc.offset;

	cpc_writeb(falcbase + F_REG((ICB1 + (timeslot - tshf) / 8), ch),
		   cpc_readb(falcbase + F_REG((ICB1 + (timeslot - tshf) / 8), ch)) & 
		   	~(0x80 >> ((timeslot - tshf) & 0x07)));
	cpc_writeb(falcbase + F_REG((TTR1 + timeslot / 8), ch),
		   cpc_readb(falcbase + F_REG((TTR1 + timeslot / 8), ch)) | 
   			(0x80 >> (timeslot & 0x07)));
	cpc_writeb(falcbase + F_REG((RTR1 + timeslot / 8), ch),
		   cpc_readb(falcbase + F_REG((RTR1 + timeslot / 8), ch)) | 
			(0x80 >> (timeslot & 0x07)));
}

static void falc_close_timeslot(pc300_t * card, int ch, int timeslot)
{
	void __iomem *falcbase = card->hw.falcbase;
	u8 tshf = card->chan[ch].falc.offset;

	cpc_writeb(falcbase + F_REG((ICB1 + (timeslot - tshf) / 8), ch),
		   cpc_readb(falcbase + F_REG((ICB1 + (timeslot - tshf) / 8), ch)) | 
		   (0x80 >> ((timeslot - tshf) & 0x07)));
	cpc_writeb(falcbase + F_REG((TTR1 + timeslot / 8), ch),
		   cpc_readb(falcbase + F_REG((TTR1 + timeslot / 8), ch)) & 
		   ~(0x80 >> (timeslot & 0x07)));
	cpc_writeb(falcbase + F_REG((RTR1 + timeslot / 8), ch),
		   cpc_readb(falcbase + F_REG((RTR1 + timeslot / 8), ch)) & 
		   ~(0x80 >> (timeslot & 0x07)));
}

static void falc_close_all_timeslots(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	void __iomem *falcbase = card->hw.falcbase;

	cpc_writeb(falcbase + F_REG(ICB1, ch), 0xff);
	cpc_writeb(falcbase + F_REG(TTR1, ch), 0);
	cpc_writeb(falcbase + F_REG(RTR1, ch), 0);
	cpc_writeb(falcbase + F_REG(ICB2, ch), 0xff);
	cpc_writeb(falcbase + F_REG(TTR2, ch), 0);
	cpc_writeb(falcbase + F_REG(RTR2, ch), 0);
	cpc_writeb(falcbase + F_REG(ICB3, ch), 0xff);
	cpc_writeb(falcbase + F_REG(TTR3, ch), 0);
	cpc_writeb(falcbase + F_REG(RTR3, ch), 0);
	if (conf->media == IF_IFACE_E1) {
		cpc_writeb(falcbase + F_REG(ICB4, ch), 0xff);
		cpc_writeb(falcbase + F_REG(TTR4, ch), 0);
		cpc_writeb(falcbase + F_REG(RTR4, ch), 0);
	}
}

static void falc_open_all_timeslots(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	void __iomem *falcbase = card->hw.falcbase;

	cpc_writeb(falcbase + F_REG(ICB1, ch), 0);
	if (conf->fr_mode == PC300_FR_UNFRAMED) {
		cpc_writeb(falcbase + F_REG(TTR1, ch), 0xff);
		cpc_writeb(falcbase + F_REG(RTR1, ch), 0xff);
	} else {
		/* Timeslot 0 is never enabled */
		cpc_writeb(falcbase + F_REG(TTR1, ch), 0x7f);
		cpc_writeb(falcbase + F_REG(RTR1, ch), 0x7f);
	}
	cpc_writeb(falcbase + F_REG(ICB2, ch), 0);
	cpc_writeb(falcbase + F_REG(TTR2, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RTR2, ch), 0xff);
	cpc_writeb(falcbase + F_REG(ICB3, ch), 0);
	cpc_writeb(falcbase + F_REG(TTR3, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RTR3, ch), 0xff);
	if (conf->media == IF_IFACE_E1) {
		cpc_writeb(falcbase + F_REG(ICB4, ch), 0);
		cpc_writeb(falcbase + F_REG(TTR4, ch), 0xff);
		cpc_writeb(falcbase + F_REG(RTR4, ch), 0xff);
	} else {
		cpc_writeb(falcbase + F_REG(ICB4, ch), 0xff);
		cpc_writeb(falcbase + F_REG(TTR4, ch), 0x80);
		cpc_writeb(falcbase + F_REG(RTR4, ch), 0x80);
	}
}

static void falc_init_timeslot(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	int tslot;

	for (tslot = 0; tslot < pfalc->num_channels; tslot++) {
		if (conf->tslot_bitmap & (1 << tslot)) {
			// Channel enabled
			falc_open_timeslot(card, ch, tslot + 1);
		} else {
			// Channel disabled
			falc_close_timeslot(card, ch, tslot + 1);
		}
	}
}

static void falc_enable_comm(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;

	if (pfalc->full_bandwidth) {
		falc_open_all_timeslots(card, ch);
	} else {
		falc_init_timeslot(card, ch);
	}
	// CTS/DCD ON
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) &
		   ~((CPLD_REG1_FALC_DCD | CPLD_REG1_FALC_CTS) << (2 * ch)));
}

static void falc_disable_comm(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;

	if (pfalc->loop_active != 2) {
		falc_close_all_timeslots(card, ch);
	}
	// CTS/DCD OFF
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) |
		   ((CPLD_REG1_FALC_DCD | CPLD_REG1_FALC_CTS) << (2 * ch)));
}

static void falc_init_t1(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u8 dja = (ch ? (LIM2_DJA2 | LIM2_DJA1) : 0);

	/* Switch to T1 mode (PCM 24) */
	cpc_writeb(falcbase + F_REG(FMR1, ch), FMR1_PMOD);

	/* Wait 20 us for setup */
	udelay(20);

	/* Transmit Buffer Size (1 frame) */
	cpc_writeb(falcbase + F_REG(SIC1, ch), SIC1_XBS0);

	/* Clock mode */
	if (conf->phys_settings.clock_type == CLOCK_INT) { /* Master mode */
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_MAS);
	} else { /* Slave mode */
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_MAS);
		cpc_writeb(falcbase + F_REG(LOOP, ch),
			   cpc_readb(falcbase + F_REG(LOOP, ch)) & ~LOOP_RTM);
	}

	cpc_writeb(falcbase + F_REG(IPC, ch), IPC_SCI);
	cpc_writeb(falcbase + F_REG(FMR0, ch),
		   cpc_readb(falcbase + F_REG(FMR0, ch)) &
		   ~(FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1));

	switch (conf->lcode) {
		case PC300_LC_AMI:
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) |
				   FMR0_XC1 | FMR0_RC1);
			/* Clear Channel register to ON for all channels */
			cpc_writeb(falcbase + F_REG(CCB1, ch), 0xff);
			cpc_writeb(falcbase + F_REG(CCB2, ch), 0xff);
			cpc_writeb(falcbase + F_REG(CCB3, ch), 0xff);
			break;

		case PC300_LC_B8ZS:
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) |
				   FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1);
			break;

		case PC300_LC_NRZ:
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) | 0x00);
			break;
	}

	cpc_writeb(falcbase + F_REG(LIM0, ch),
		   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_ELOS);
	cpc_writeb(falcbase + F_REG(LIM0, ch),
		   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~(LIM0_SCL1 | LIM0_SCL0));
	/* Set interface mode to 2 MBPS */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_IMOD);

	switch (conf->fr_mode) {
		case PC300_FR_ESF:
			pfalc->multiframe_mode = 0;
			cpc_writeb(falcbase + F_REG(FMR4, ch),
				   cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_FM1);
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) | 
				   FMR1_CRC | FMR1_EDL);
			cpc_writeb(falcbase + F_REG(XDL1, ch), 0);
			cpc_writeb(falcbase + F_REG(XDL2, ch), 0);
			cpc_writeb(falcbase + F_REG(XDL3, ch), 0);
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) & ~FMR0_SRAF);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2,ch)) | FMR2_MCSP | FMR2_SSP);
			break;

		case PC300_FR_D4:
			pfalc->multiframe_mode = 1;
			cpc_writeb(falcbase + F_REG(FMR4, ch),
				   cpc_readb(falcbase + F_REG(FMR4, ch)) &
				   ~(FMR4_FM1 | FMR4_FM0));
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) | FMR0_SRAF);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_SSP);
			break;
	}

	/* Enable Automatic Resynchronization */
	cpc_writeb(falcbase + F_REG(FMR4, ch),
		   cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_AUTO);

	/* Transmit Automatic Remote Alarm */
	cpc_writeb(falcbase + F_REG(FMR2, ch),
		   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

	/* Channel translation mode 1 : one to one */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_CTM);

	/* No signaling */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_SIGM);
	cpc_writeb(falcbase + F_REG(FMR5, ch),
		   cpc_readb(falcbase + F_REG(FMR5, ch)) &
		   ~(FMR5_EIBR | FMR5_SRS));
	cpc_writeb(falcbase + F_REG(CCR1, ch), 0);

	cpc_writeb(falcbase + F_REG(LIM1, ch),
		   cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RIL0 | LIM1_RIL1);

	switch (conf->lbo) {
			/* Provides proper Line Build Out */
		case PC300_LBO_0_DB:
			cpc_writeb(falcbase + F_REG(LIM2, ch), (LIM2_LOS1 | dja));
			cpc_writeb(falcbase + F_REG(XPM0, ch), 0x5a);
			cpc_writeb(falcbase + F_REG(XPM1, ch), 0x8f);
			cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
			break;
		case PC300_LBO_7_5_DB:
			cpc_writeb(falcbase + F_REG(LIM2, ch), (0x40 | LIM2_LOS1 | dja));
			cpc_writeb(falcbase + F_REG(XPM0, ch), 0x11);
			cpc_writeb(falcbase + F_REG(XPM1, ch), 0x02);
			cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
			break;
		case PC300_LBO_15_DB:
			cpc_writeb(falcbase + F_REG(LIM2, ch), (0x80 | LIM2_LOS1 | dja));
			cpc_writeb(falcbase + F_REG(XPM0, ch), 0x8e);
			cpc_writeb(falcbase + F_REG(XPM1, ch), 0x01);
			cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
			break;
		case PC300_LBO_22_5_DB:
			cpc_writeb(falcbase + F_REG(LIM2, ch), (0xc0 | LIM2_LOS1 | dja));
			cpc_writeb(falcbase + F_REG(XPM0, ch), 0x09);
			cpc_writeb(falcbase + F_REG(XPM1, ch), 0x01);
			cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
			break;
	}

	/* Transmit Clock-Slot Offset */
	cpc_writeb(falcbase + F_REG(XC0, ch),
		   cpc_readb(falcbase + F_REG(XC0, ch)) | 0x01);
	/* Transmit Time-slot Offset */
	cpc_writeb(falcbase + F_REG(XC1, ch), 0x3e);
	/* Receive  Clock-Slot offset */
	cpc_writeb(falcbase + F_REG(RC0, ch), 0x05);
	/* Receive  Time-slot offset */
	cpc_writeb(falcbase + F_REG(RC1, ch), 0x00);

	/* LOS Detection after 176 consecutive 0s */
	cpc_writeb(falcbase + F_REG(PCDR, ch), 0x0a);
	/* LOS Recovery after 22 ones in the time window of PCD */
	cpc_writeb(falcbase + F_REG(PCRR, ch), 0x15);

	cpc_writeb(falcbase + F_REG(IDLE, ch), 0x7f);

	if (conf->fr_mode == PC300_FR_ESF_JAPAN) {
		cpc_writeb(falcbase + F_REG(RC1, ch),
			   cpc_readb(falcbase + F_REG(RC1, ch)) | 0x80);
	}

	falc_close_all_timeslots(card, ch);
}

static void falc_init_e1(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u8 dja = (ch ? (LIM2_DJA2 | LIM2_DJA1) : 0);

	/* Switch to E1 mode (PCM 30) */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_PMOD);

	/* Clock mode */
	if (conf->phys_settings.clock_type == CLOCK_INT) { /* Master mode */
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_MAS);
	} else { /* Slave mode */
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_MAS);
	}
	cpc_writeb(falcbase + F_REG(LOOP, ch),
		   cpc_readb(falcbase + F_REG(LOOP, ch)) & ~LOOP_SFM);

	cpc_writeb(falcbase + F_REG(IPC, ch), IPC_SCI);
	cpc_writeb(falcbase + F_REG(FMR0, ch),
		   cpc_readb(falcbase + F_REG(FMR0, ch)) &
		   ~(FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1));

	switch (conf->lcode) {
		case PC300_LC_AMI:
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) |
				   FMR0_XC1 | FMR0_RC1);
			break;

		case PC300_LC_HDB3:
			cpc_writeb(falcbase + F_REG(FMR0, ch),
				   cpc_readb(falcbase + F_REG(FMR0, ch)) |
				   FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1);
			break;

		case PC300_LC_NRZ:
			break;
	}

	cpc_writeb(falcbase + F_REG(LIM0, ch),
		   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~(LIM0_SCL1 | LIM0_SCL0));
	/* Set interface mode to 2 MBPS */
	cpc_writeb(falcbase + F_REG(FMR1, ch),
		   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_IMOD);

	cpc_writeb(falcbase + F_REG(XPM0, ch), 0x18);
	cpc_writeb(falcbase + F_REG(XPM1, ch), 0x03);
	cpc_writeb(falcbase + F_REG(XPM2, ch), 0x00);

	switch (conf->fr_mode) {
		case PC300_FR_MF_CRC4:
			pfalc->multiframe_mode = 1;
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_XFS);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_RFS1);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_RFS0);
			cpc_writeb(falcbase + F_REG(FMR3, ch),
				   cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_EXTIW);

			/* MultiFrame Resynchronization */
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_MFCS);

			/* Automatic Loss of Multiframe > 914 CRC errors */
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_ALMF);

			/* S1 and SI1/SI2 spare Bits set to 1 */
			cpc_writeb(falcbase + F_REG(XSP, ch),
				   cpc_readb(falcbase + F_REG(XSP, ch)) & ~XSP_AXS);
			cpc_writeb(falcbase + F_REG(XSP, ch),
				   cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_EBP);
			cpc_writeb(falcbase + F_REG(XSP, ch),
				   cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_XS13 | XSP_XS15);

			/* Automatic Force Resynchronization */
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_AFR);

			/* Transmit Automatic Remote Alarm */
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

			/* Transmit Spare Bits for National Use (Y, Sn, Sa) */
			cpc_writeb(falcbase + F_REG(XSW, ch),
				   cpc_readb(falcbase + F_REG(XSW, ch)) |
				   XSW_XY0 | XSW_XY1 | XSW_XY2 | XSW_XY3 | XSW_XY4);
			break;

		case PC300_FR_MF_NON_CRC4:
		case PC300_FR_D4:
			pfalc->multiframe_mode = 0;
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_XFS);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) & 
				   ~(FMR2_RFS1 | FMR2_RFS0));
			cpc_writeb(falcbase + F_REG(XSW, ch),
				   cpc_readb(falcbase + F_REG(XSW, ch)) | XSW_XSIS);
			cpc_writeb(falcbase + F_REG(XSP, ch),
				   cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_XSIF);

			/* Automatic Force Resynchronization */
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_AFR);

			/* Transmit Automatic Remote Alarm */
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

			/* Transmit Spare Bits for National Use (Y, Sn, Sa) */
			cpc_writeb(falcbase + F_REG(XSW, ch),
				   cpc_readb(falcbase + F_REG(XSW, ch)) |
				   XSW_XY0 | XSW_XY1 | XSW_XY2 | XSW_XY3 | XSW_XY4);
			break;

		case PC300_FR_UNFRAMED:
			pfalc->multiframe_mode = 0;
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_XFS);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) & 
				   ~(FMR2_RFS1 | FMR2_RFS0));
			cpc_writeb(falcbase + F_REG(XSP, ch),
				   cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_TT0);
			cpc_writeb(falcbase + F_REG(XSW, ch),
				   cpc_readb(falcbase + F_REG(XSW, ch)) & 
				   ~(XSW_XTM|XSW_XY0|XSW_XY1|XSW_XY2|XSW_XY3|XSW_XY4));
			cpc_writeb(falcbase + F_REG(TSWM, ch), 0xff);
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) |
				   (FMR2_RTM | FMR2_DAIS));
			cpc_writeb(falcbase + F_REG(FMR2, ch),
				   cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_AXRA);
			cpc_writeb(falcbase + F_REG(FMR1, ch),
				   cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_AFR);
			pfalc->sync = 1;
			cpc_writeb(falcbase + card->hw.cpld_reg2,
				   cpc_readb(falcbase + card->hw.cpld_reg2) |
				   (CPLD_REG2_FALC_LED2 << (2 * ch)));
			break;
	}

	/* No signaling */
	cpc_writeb(falcbase + F_REG(XSP, ch),
		   cpc_readb(falcbase + F_REG(XSP, ch)) & ~XSP_CASEN);
	cpc_writeb(falcbase + F_REG(CCR1, ch), 0);

	cpc_writeb(falcbase + F_REG(LIM1, ch),
		   cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RIL0 | LIM1_RIL1);
	cpc_writeb(falcbase + F_REG(LIM2, ch), (LIM2_LOS1 | dja));

	/* Transmit Clock-Slot Offset */
	cpc_writeb(falcbase + F_REG(XC0, ch),
		   cpc_readb(falcbase + F_REG(XC0, ch)) | 0x01);
	/* Transmit Time-slot Offset */
	cpc_writeb(falcbase + F_REG(XC1, ch), 0x3e);
	/* Receive  Clock-Slot offset */
	cpc_writeb(falcbase + F_REG(RC0, ch), 0x05);
	/* Receive  Time-slot offset */
	cpc_writeb(falcbase + F_REG(RC1, ch), 0x00);

	/* LOS Detection after 176 consecutive 0s */
	cpc_writeb(falcbase + F_REG(PCDR, ch), 0x0a);
	/* LOS Recovery after 22 ones in the time window of PCD */
	cpc_writeb(falcbase + F_REG(PCRR, ch), 0x15);

	cpc_writeb(falcbase + F_REG(IDLE, ch), 0x7f);

	falc_close_all_timeslots(card, ch);
}

static void falc_init_hdlc(pc300_t * card, int ch)
{
	void __iomem *falcbase = card->hw.falcbase;
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;

	/* Enable transparent data transfer */
	if (conf->fr_mode == PC300_FR_UNFRAMED) {
		cpc_writeb(falcbase + F_REG(MODE, ch), 0);
	} else {
		cpc_writeb(falcbase + F_REG(MODE, ch),
			   cpc_readb(falcbase + F_REG(MODE, ch)) |
			   (MODE_HRAC | MODE_MDS2));
		cpc_writeb(falcbase + F_REG(RAH2, ch), 0xff);
		cpc_writeb(falcbase + F_REG(RAH1, ch), 0xff);
		cpc_writeb(falcbase + F_REG(RAL2, ch), 0xff);
		cpc_writeb(falcbase + F_REG(RAL1, ch), 0xff);
	}

	/* Tx/Rx reset  */
	falc_issue_cmd(card, ch, CMDR_RRES | CMDR_XRES | CMDR_SRES);

	/* Enable interrupt sources */
	falc_intr_enable(card, ch);
}

static void te_config(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u8 dummy;
	unsigned long flags;

	memset(pfalc, 0, sizeof(falc_t));
	switch (conf->media) {
		case IF_IFACE_T1:
			pfalc->num_channels = NUM_OF_T1_CHANNELS;
			pfalc->offset = 1;
			break;
		case IF_IFACE_E1:
			pfalc->num_channels = NUM_OF_E1_CHANNELS;
			pfalc->offset = 0;
			break;
	}
	if (conf->tslot_bitmap == 0xffffffffUL)
		pfalc->full_bandwidth = 1;
	else
		pfalc->full_bandwidth = 0;

	CPC_LOCK(card, flags);
	/* Reset the FALC chip */
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) |
		   (CPLD_REG1_FALC_RESET << (2 * ch)));
	udelay(10000);
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) &
		   ~(CPLD_REG1_FALC_RESET << (2 * ch)));

	if (conf->media == IF_IFACE_T1) {
		falc_init_t1(card, ch);
	} else {
		falc_init_e1(card, ch);
	}
	falc_init_hdlc(card, ch);
	if (conf->rx_sens == PC300_RX_SENS_SH) {
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_EQON);
	} else {
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_EQON);
	}
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) |
		   ((CPLD_REG2_FALC_TX_CLK | CPLD_REG2_FALC_RX_CLK) << (2 * ch)));

	/* Clear all interrupt registers */
	dummy = cpc_readb(falcbase + F_REG(FISR0, ch)) +
		cpc_readb(falcbase + F_REG(FISR1, ch)) +
		cpc_readb(falcbase + F_REG(FISR2, ch)) +
		cpc_readb(falcbase + F_REG(FISR3, ch));
	CPC_UNLOCK(card, flags);
}

static void falc_check_status(pc300_t * card, int ch, unsigned char frs0)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	/* Verify LOS */
	if (frs0 & FRS0_LOS) {
		if (!pfalc->red_alarm) {
			pfalc->red_alarm = 1;
			pfalc->los++;
			if (!pfalc->blue_alarm) {
				// EVENT_FALC_ABNORMAL
				if (conf->media == IF_IFACE_T1) {
					/* Disable this interrupt as it may otherwise interfere 
					 * with other working boards. */
					cpc_writeb(falcbase + F_REG(IMR0, ch), 
						   cpc_readb(falcbase + F_REG(IMR0, ch))
						   | IMR0_PDEN);
				}
				falc_disable_comm(card, ch);
				// EVENT_FALC_ABNORMAL
			}
		}
	} else {
		if (pfalc->red_alarm) {
			pfalc->red_alarm = 0;
			pfalc->losr++;
		}
	}

	if (conf->fr_mode != PC300_FR_UNFRAMED) {
		/* Verify AIS alarm */
		if (frs0 & FRS0_AIS) {
			if (!pfalc->blue_alarm) {
				pfalc->blue_alarm = 1;
				pfalc->ais++;
				// EVENT_AIS
				if (conf->media == IF_IFACE_T1) {
					/* Disable this interrupt as it may otherwise interfere with                       other working boards. */
					cpc_writeb(falcbase + F_REG(IMR0, ch),
						   cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
				}
				falc_disable_comm(card, ch);
				// EVENT_AIS
			}
		} else {
			pfalc->blue_alarm = 0;
		}

		/* Verify LFA */
		if (frs0 & FRS0_LFA) {
			if (!pfalc->loss_fa) {
				pfalc->loss_fa = 1;
				pfalc->lfa++;
				if (!pfalc->blue_alarm && !pfalc->red_alarm) {
					// EVENT_FALC_ABNORMAL
					if (conf->media == IF_IFACE_T1) {
						/* Disable this interrupt as it may otherwise 
						 * interfere with other working boards. */
						cpc_writeb(falcbase + F_REG(IMR0, ch),
							   cpc_readb(falcbase + F_REG(IMR0, ch))
							   | IMR0_PDEN);
					}
					falc_disable_comm(card, ch);
					// EVENT_FALC_ABNORMAL
				}
			}
		} else {
			if (pfalc->loss_fa) {
				pfalc->loss_fa = 0;
				pfalc->farec++;
			}
		}

		/* Verify LMFA */
		if (pfalc->multiframe_mode && (frs0 & FRS0_LMFA)) {
			/* D4 or CRC4 frame mode */
			if (!pfalc->loss_mfa) {
				pfalc->loss_mfa = 1;
				pfalc->lmfa++;
				if (!pfalc->blue_alarm && !pfalc->red_alarm &&
				    !pfalc->loss_fa) {
					// EVENT_FALC_ABNORMAL
					if (conf->media == IF_IFACE_T1) {
						/* Disable this interrupt as it may otherwise 
						 * interfere with other working boards. */
						cpc_writeb(falcbase + F_REG(IMR0, ch),
							   cpc_readb(falcbase + F_REG(IMR0, ch))
							   | IMR0_PDEN);
					}
					falc_disable_comm(card, ch);
					// EVENT_FALC_ABNORMAL
				}
			}
		} else {
			pfalc->loss_mfa = 0;
		}

		/* Verify Remote Alarm */
		if (frs0 & FRS0_RRA) {
			if (!pfalc->yellow_alarm) {
				pfalc->yellow_alarm = 1;
				pfalc->rai++;
				if (pfalc->sync) {
					// EVENT_RAI
					falc_disable_comm(card, ch);
					// EVENT_RAI
				}
			}
		} else {
			pfalc->yellow_alarm = 0;
		}
	} /* if !PC300_UNFRAMED */

	if (pfalc->red_alarm || pfalc->loss_fa ||
	    pfalc->loss_mfa || pfalc->blue_alarm) {
		if (pfalc->sync) {
			pfalc->sync = 0;
			chan->d.line_off++;
			cpc_writeb(falcbase + card->hw.cpld_reg2,
				   cpc_readb(falcbase + card->hw.cpld_reg2) &
				   ~(CPLD_REG2_FALC_LED2 << (2 * ch)));
		}
	} else {
		if (!pfalc->sync) {
			pfalc->sync = 1;
			chan->d.line_on++;
			cpc_writeb(falcbase + card->hw.cpld_reg2,
				   cpc_readb(falcbase + card->hw.cpld_reg2) |
				   (CPLD_REG2_FALC_LED2 << (2 * ch)));
		}
	}

	if (pfalc->sync && !pfalc->yellow_alarm) {
		if (!pfalc->active) {
			// EVENT_FALC_NORMAL
			if (pfalc->loop_active) {
				return;
			}
			if (conf->media == IF_IFACE_T1) {
				cpc_writeb(falcbase + F_REG(IMR0, ch),
					   cpc_readb(falcbase + F_REG(IMR0, ch)) & ~IMR0_PDEN);
			}
			falc_enable_comm(card, ch);
			// EVENT_FALC_NORMAL
			pfalc->active = 1;
		}
	} else {
		if (pfalc->active) {
			pfalc->active = 0;
		}
	}
}

static void falc_update_stats(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u16 counter;

	counter = cpc_readb(falcbase + F_REG(FECL, ch));
	counter |= cpc_readb(falcbase + F_REG(FECH, ch)) << 8;
	pfalc->fec += counter;

	counter = cpc_readb(falcbase + F_REG(CVCL, ch));
	counter |= cpc_readb(falcbase + F_REG(CVCH, ch)) << 8;
	pfalc->cvc += counter;

	counter = cpc_readb(falcbase + F_REG(CECL, ch));
	counter |= cpc_readb(falcbase + F_REG(CECH, ch)) << 8;
	pfalc->cec += counter;

	counter = cpc_readb(falcbase + F_REG(EBCL, ch));
	counter |= cpc_readb(falcbase + F_REG(EBCH, ch)) << 8;
	pfalc->ebc += counter;

	if (cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) {
		mdelay(10);
		counter = cpc_readb(falcbase + F_REG(BECL, ch));
		counter |= cpc_readb(falcbase + F_REG(BECH, ch)) << 8;
		pfalc->bec += counter;

		if (((conf->media == IF_IFACE_T1) &&
		     (cpc_readb(falcbase + F_REG(FRS1, ch)) & FRS1_LLBAD) &&
		     (!(cpc_readb(falcbase + F_REG(FRS1, ch)) & FRS1_PDEN))) ||
		    ((conf->media == IF_IFACE_E1) &&
		     (cpc_readb(falcbase + F_REG(RSP, ch)) & RSP_LLBAD))) {
			pfalc->prbs = 2;
		} else {
			pfalc->prbs = 1;
		}
	}
}

/*----------------------------------------------------------------------------
 * falc_remote_loop
 *----------------------------------------------------------------------------
 * Description:	In the remote loopback mode the clock and data recovered
 *		from the line inputs RL1/2 or RDIP/RDIN are routed back
 *		to the line outputs XL1/2 or XDOP/XDON via the analog
 *		transmitter. As in normal mode they are processed by
 *		the synchronizer and then sent to the system interface.
 *----------------------------------------------------------------------------
 */
static void falc_remote_loop(pc300_t * card, int ch, int loop_on)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (loop_on) {
		// EVENT_FALC_ABNORMAL
		if (conf->media == IF_IFACE_T1) {
			/* Disable this interrupt as it may otherwise interfere with 
			 * other working boards. */
			cpc_writeb(falcbase + F_REG(IMR0, ch),
				   cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
		}
		falc_disable_comm(card, ch);
		// EVENT_FALC_ABNORMAL
		cpc_writeb(falcbase + F_REG(LIM1, ch),
			   cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RL);
		pfalc->loop_active = 1;
	} else {
		cpc_writeb(falcbase + F_REG(LIM1, ch),
			   cpc_readb(falcbase + F_REG(LIM1, ch)) & ~LIM1_RL);
		pfalc->sync = 0;
		cpc_writeb(falcbase + card->hw.cpld_reg2,
			   cpc_readb(falcbase + card->hw.cpld_reg2) &
			   ~(CPLD_REG2_FALC_LED2 << (2 * ch)));
		pfalc->active = 0;
		falc_issue_cmd(card, ch, CMDR_XRES);
		pfalc->loop_active = 0;
	}
}

/*----------------------------------------------------------------------------
 * falc_local_loop
 *----------------------------------------------------------------------------
 * Description: The local loopback mode disconnects the receive lines 
 *		RL1/RL2 resp. RDIP/RDIN from the receiver. Instead of the
 *		signals coming from the line the data provided by system
 *		interface are routed through the analog receiver back to
 *		the system interface. The unipolar bit stream will be
 *		undisturbed transmitted on the line. Receiver and transmitter
 *		coding must be identical.
 *----------------------------------------------------------------------------
 */
static void falc_local_loop(pc300_t * card, int ch, int loop_on)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (loop_on) {
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_LL);
		pfalc->loop_active = 1;
	} else {
		cpc_writeb(falcbase + F_REG(LIM0, ch),
			   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_LL);
		pfalc->loop_active = 0;
	}
}

/*----------------------------------------------------------------------------
 * falc_payload_loop
 *----------------------------------------------------------------------------
 * Description: This routine allows to enable/disable payload loopback.
 *		When the payload loop is activated, the received 192 bits
 *		of payload data will be looped back to the transmit
 *		direction. The framing bits, CRC6 and DL bits are not 
 *		looped. They are originated by the FALC-LH transmitter.
 *----------------------------------------------------------------------------
 */
static void falc_payload_loop(pc300_t * card, int ch, int loop_on)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (loop_on) {
		// EVENT_FALC_ABNORMAL
		if (conf->media == IF_IFACE_T1) {
			/* Disable this interrupt as it may otherwise interfere with 
			 * other working boards. */
			cpc_writeb(falcbase + F_REG(IMR0, ch),
				   cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
		}
		falc_disable_comm(card, ch);
		// EVENT_FALC_ABNORMAL
		cpc_writeb(falcbase + F_REG(FMR2, ch),
			   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_PLB);
		if (conf->media == IF_IFACE_T1) {
			cpc_writeb(falcbase + F_REG(FMR4, ch),
				   cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_TM);
		} else {
			cpc_writeb(falcbase + F_REG(FMR5, ch),
				   cpc_readb(falcbase + F_REG(FMR5, ch)) | XSP_TT0);
		}
		falc_open_all_timeslots(card, ch);
		pfalc->loop_active = 2;
	} else {
		cpc_writeb(falcbase + F_REG(FMR2, ch),
			   cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_PLB);
		if (conf->media == IF_IFACE_T1) {
			cpc_writeb(falcbase + F_REG(FMR4, ch),
				   cpc_readb(falcbase + F_REG(FMR4, ch)) & ~FMR4_TM);
		} else {
			cpc_writeb(falcbase + F_REG(FMR5, ch),
				   cpc_readb(falcbase + F_REG(FMR5, ch)) & ~XSP_TT0);
		}
		pfalc->sync = 0;
		cpc_writeb(falcbase + card->hw.cpld_reg2,
			   cpc_readb(falcbase + card->hw.cpld_reg2) &
			   ~(CPLD_REG2_FALC_LED2 << (2 * ch)));
		pfalc->active = 0;
		falc_issue_cmd(card, ch, CMDR_XRES);
		pfalc->loop_active = 0;
	}
}

/*----------------------------------------------------------------------------
 * turn_off_xlu
 *----------------------------------------------------------------------------
 * Description:	Turns XLU bit off in the proper register
 *----------------------------------------------------------------------------
 */
static void turn_off_xlu(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	void __iomem *falcbase = card->hw.falcbase;

	if (conf->media == IF_IFACE_T1) {
		cpc_writeb(falcbase + F_REG(FMR5, ch),
			   cpc_readb(falcbase + F_REG(FMR5, ch)) & ~FMR5_XLU);
	} else {
		cpc_writeb(falcbase + F_REG(FMR3, ch),
			   cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_XLU);
	}
}

/*----------------------------------------------------------------------------
 * turn_off_xld
 *----------------------------------------------------------------------------
 * Description: Turns XLD bit off in the proper register
 *----------------------------------------------------------------------------
 */
static void turn_off_xld(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	void __iomem *falcbase = card->hw.falcbase;

	if (conf->media == IF_IFACE_T1) {
		cpc_writeb(falcbase + F_REG(FMR5, ch),
			   cpc_readb(falcbase + F_REG(FMR5, ch)) & ~FMR5_XLD);
	} else {
		cpc_writeb(falcbase + F_REG(FMR3, ch),
			   cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_XLD);
	}
}

/*----------------------------------------------------------------------------
 * falc_generate_loop_up_code
 *----------------------------------------------------------------------------
 * Description:	This routine writes the proper FALC chip register in order
 *		to generate a LOOP activation code over a T1/E1 line.
 *----------------------------------------------------------------------------
 */
static void falc_generate_loop_up_code(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (conf->media == IF_IFACE_T1) {
		cpc_writeb(falcbase + F_REG(FMR5, ch),
			   cpc_readb(falcbase + F_REG(FMR5, ch)) | FMR5_XLU);
	} else {
		cpc_writeb(falcbase + F_REG(FMR3, ch),
			   cpc_readb(falcbase + F_REG(FMR3, ch)) | FMR3_XLU);
	}
	// EVENT_FALC_ABNORMAL
	if (conf->media == IF_IFACE_T1) {
		/* Disable this interrupt as it may otherwise interfere with 
		 * other working boards. */
		cpc_writeb(falcbase + F_REG(IMR0, ch),
			   cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
	}
	falc_disable_comm(card, ch);
	// EVENT_FALC_ABNORMAL
	pfalc->loop_gen = 1;
}

/*----------------------------------------------------------------------------
 * falc_generate_loop_down_code
 *----------------------------------------------------------------------------
 * Description:	This routine writes the proper FALC chip register in order
 *		to generate a LOOP deactivation code over a T1/E1 line.
 *----------------------------------------------------------------------------
 */
static void falc_generate_loop_down_code(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (conf->media == IF_IFACE_T1) {
		cpc_writeb(falcbase + F_REG(FMR5, ch),
			   cpc_readb(falcbase + F_REG(FMR5, ch)) | FMR5_XLD);
	} else {
		cpc_writeb(falcbase + F_REG(FMR3, ch),
			   cpc_readb(falcbase + F_REG(FMR3, ch)) | FMR3_XLD);
	}
	pfalc->sync = 0;
	cpc_writeb(falcbase + card->hw.cpld_reg2,
		   cpc_readb(falcbase + card->hw.cpld_reg2) &
		   ~(CPLD_REG2_FALC_LED2 << (2 * ch)));
	pfalc->active = 0;
//?    falc_issue_cmd(card, ch, CMDR_XRES);
	pfalc->loop_gen = 0;
}

/*----------------------------------------------------------------------------
 * falc_pattern_test
 *----------------------------------------------------------------------------
 * Description:	This routine generates a pattern code and checks
 *		it on the reception side.
 *----------------------------------------------------------------------------
 */
static void falc_pattern_test(pc300_t * card, int ch, unsigned int activate)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (activate) {
		pfalc->prbs = 1;
		pfalc->bec = 0;
		if (conf->media == IF_IFACE_T1) {
			/* Disable local loop activation/deactivation detect */
			cpc_writeb(falcbase + F_REG(IMR3, ch),
				   cpc_readb(falcbase + F_REG(IMR3, ch)) | IMR3_LLBSC);
		} else {
			/* Disable local loop activation/deactivation detect */
			cpc_writeb(falcbase + F_REG(IMR1, ch),
				   cpc_readb(falcbase + F_REG(IMR1, ch)) | IMR1_LLBSC);
		}
		/* Activates generation and monitoring of PRBS 
		 * (Pseudo Random Bit Sequence) */
		cpc_writeb(falcbase + F_REG(LCR1, ch),
			   cpc_readb(falcbase + F_REG(LCR1, ch)) | LCR1_EPRM | LCR1_XPRBS);
	} else {
		pfalc->prbs = 0;
		/* Deactivates generation and monitoring of PRBS 
		 * (Pseudo Random Bit Sequence) */
		cpc_writeb(falcbase + F_REG(LCR1, ch),
			   cpc_readb(falcbase+F_REG(LCR1,ch)) & ~(LCR1_EPRM | LCR1_XPRBS));
		if (conf->media == IF_IFACE_T1) {
			/* Enable local loop activation/deactivation detect */
			cpc_writeb(falcbase + F_REG(IMR3, ch),
				   cpc_readb(falcbase + F_REG(IMR3, ch)) & ~IMR3_LLBSC);
		} else {
			/* Enable local loop activation/deactivation detect */
			cpc_writeb(falcbase + F_REG(IMR1, ch),
				   cpc_readb(falcbase + F_REG(IMR1, ch)) & ~IMR1_LLBSC);
		}
	}
}

/*----------------------------------------------------------------------------
 * falc_pattern_test_error
 *----------------------------------------------------------------------------
 * Description:	This routine returns the bit error counter value
 *----------------------------------------------------------------------------
 */
static u16 falc_pattern_test_error(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;

	return pfalc->bec;
}

/**********************************/
/***   Net Interface Routines   ***/
/**********************************/

static void
cpc_trace(struct net_device *dev, struct sk_buff *skb_main, char rx_tx)
{
	struct sk_buff *skb;

	if ((skb = dev_alloc_skb(10 + skb_main->len)) == NULL) {
		printk("%s: out of memory\n", dev->name);
		return;
	}
	skb_put(skb, 10 + skb_main->len);

	skb->dev = dev;
	skb->protocol = htons(ETH_P_CUST);
	skb_reset_mac_header(skb);
	skb->pkt_type = PACKET_HOST;
	skb->len = 10 + skb_main->len;

	skb_copy_to_linear_data(skb, dev->name, 5);
	skb->data[5] = '[';
	skb->data[6] = rx_tx;
	skb->data[7] = ']';
	skb->data[8] = ':';
	skb->data[9] = ' ';
	skb_copy_from_linear_data(skb_main, &skb->data[10], skb_main->len);

	netif_rx(skb);
}

static void cpc_tx_timeout(struct net_device *dev)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	int ch = chan->channel;
	unsigned long flags;
	u8 ilar;

	dev->stats.tx_errors++;
	dev->stats.tx_aborted_errors++;
	CPC_LOCK(card, flags);
	if ((ilar = cpc_readb(card->hw.scabase + ILAR)) != 0) {
		printk("%s: ILAR=0x%x\n", dev->name, ilar);
		cpc_writeb(card->hw.scabase + ILAR, ilar);
		cpc_writeb(card->hw.scabase + DMER, 0x80);
	}
	if (card->hw.type == PC300_TE) {
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) &
			   ~(CPLD_REG2_FALC_LED1 << (2 * ch)));
	}
	dev->trans_start = jiffies; /* prevent tx timeout */
	CPC_UNLOCK(card, flags);
	netif_wake_queue(dev);
}

static int cpc_queue_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	int ch = chan->channel;
	unsigned long flags;
#ifdef PC300_DEBUG_TX
	int i;
#endif

	if (!netif_carrier_ok(dev)) {
		/* DCD must be OFF: drop packet */
		dev_kfree_skb(skb);
		dev->stats.tx_errors++;
		dev->stats.tx_carrier_errors++;
		return 0;
	} else if (cpc_readb(card->hw.scabase + M_REG(ST3, ch)) & ST3_DCD) {
		printk("%s: DCD is OFF. Going administrative down.\n", dev->name);
		dev->stats.tx_errors++;
		dev->stats.tx_carrier_errors++;
		dev_kfree_skb(skb);
		netif_carrier_off(dev);
		CPC_LOCK(card, flags);
		cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_BUF_CLR);
		if (card->hw.type == PC300_TE) {
			cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
				   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) & 
				   			~(CPLD_REG2_FALC_LED1 << (2 * ch)));
		}
		CPC_UNLOCK(card, flags);
		netif_wake_queue(dev);
		return 0;
	}

	/* Write buffer to DMA buffers */
	if (dma_buf_write(card, ch, (u8 *)skb->data, skb->len) != 0) {
//		printk("%s: write error. Dropping TX packet.\n", dev->name);
		netif_stop_queue(dev);
		dev_kfree_skb(skb);
		dev->stats.tx_errors++;
		dev->stats.tx_dropped++;
		return 0;
	}
#ifdef PC300_DEBUG_TX
	printk("%s T:", dev->name);
	for (i = 0; i < skb->len; i++)
		printk(" %02x", *(skb->data + i));
	printk("\n");
#endif

	if (d->trace_on) {
		cpc_trace(dev, skb, 'T');
	}

	/* Start transmission */
	CPC_LOCK(card, flags);
	/* verify if it has more than one free descriptor */
	if (card->chan[ch].nfree_tx_bd <= 1) {
		/* don't have so stop the queue */
		netif_stop_queue(dev);
	}
	cpc_writel(card->hw.scabase + DTX_REG(EDAL, ch),
		   TX_BD_ADDR(ch, chan->tx_next_bd));
	cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_ENA);
	cpc_writeb(card->hw.scabase + DSR_TX(ch), DSR_DE);
	if (card->hw.type == PC300_TE) {
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) |
			   (CPLD_REG2_FALC_LED1 << (2 * ch)));
	}
	CPC_UNLOCK(card, flags);
	dev_kfree_skb(skb);

	return 0;
}

static void cpc_net_rx(struct net_device *dev)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	int ch = chan->channel;
#ifdef PC300_DEBUG_RX
	int i;
#endif
	int rxb;
	struct sk_buff *skb;

	while (1) {
		if ((rxb = dma_get_rx_frame_size(card, ch)) == -1)
			return;

		if (!netif_carrier_ok(dev)) {
			/* DCD must be OFF: drop packet */
		    printk("%s : DCD is OFF - drop %d rx bytes\n", dev->name, rxb); 
			skb = NULL;
		} else {
			if (rxb > (dev->mtu + 40)) { /* add headers */
				printk("%s : MTU exceeded %d\n", dev->name, rxb); 
				skb = NULL;
			} else {
				skb = dev_alloc_skb(rxb);
				if (skb == NULL) {
					printk("%s: Memory squeeze!!\n", dev->name);
					return;
				}
				skb->dev = dev;
			}
		}

		if (((rxb = dma_buf_read(card, ch, skb)) <= 0) || (skb == NULL)) {
#ifdef PC300_DEBUG_RX
			printk("%s: rxb = %x\n", dev->name, rxb);
#endif
			if ((skb == NULL) && (rxb > 0)) {
				/* rxb > dev->mtu */
				dev->stats.rx_errors++;
				dev->stats.rx_length_errors++;
				continue;
			}

			if (rxb < 0) {	/* Invalid frame */
				rxb = -rxb;
				if (rxb & DST_OVR) {
					dev->stats.rx_errors++;
					dev->stats.rx_fifo_errors++;
				}
				if (rxb & DST_CRC) {
					dev->stats.rx_errors++;
					dev->stats.rx_crc_errors++;
				}
				if (rxb & (DST_RBIT | DST_SHRT | DST_ABT)) {
					dev->stats.rx_errors++;
					dev->stats.rx_frame_errors++;
				}
			}
			if (skb) {
				dev_kfree_skb_irq(skb);
			}
			continue;
		}

		dev->stats.rx_bytes += rxb;

#ifdef PC300_DEBUG_RX
		printk("%s R:", dev->name);
		for (i = 0; i < skb->len; i++)
			printk(" %02x", *(skb->data + i));
		printk("\n");
#endif
		if (d->trace_on) {
			cpc_trace(dev, skb, 'R');
		}
		dev->stats.rx_packets++;
		skb->protocol = hdlc_type_trans(skb, dev);
		netif_rx(skb);
	}
}

/************************************/
/***   PC300 Interrupt Routines   ***/
/************************************/
static void sca_tx_intr(pc300dev_t *dev)
{
	pc300ch_t *chan = (pc300ch_t *)dev->chan; 
	pc300_t *card = (pc300_t *)chan->card; 
	int ch = chan->channel; 
	volatile pcsca_bd_t __iomem * ptdescr; 

    /* Clean up descriptors from previous transmission */
	ptdescr = (card->hw.rambase +
						TX_BD_ADDR(ch,chan->tx_first_bd));
	while ((cpc_readl(card->hw.scabase + DTX_REG(CDAL,ch)) !=
		TX_BD_ADDR(ch,chan->tx_first_bd)) &&
	       (cpc_readb(&ptdescr->status) & DST_OSB)) {
		dev->dev->stats.tx_packets++;
		dev->dev->stats.tx_bytes += cpc_readw(&ptdescr->len);
		cpc_writeb(&ptdescr->status, DST_OSB);
		cpc_writew(&ptdescr->len, 0);
		chan->nfree_tx_bd++;
		chan->tx_first_bd = (chan->tx_first_bd + 1) & (N_DMA_TX_BUF - 1);
		ptdescr = (card->hw.rambase + TX_BD_ADDR(ch,chan->tx_first_bd));
    }

#ifdef CONFIG_PC300_MLPPP
	if (chan->conf.proto == PC300_PROTO_MLPPP) {
			cpc_tty_trigger_poll(dev);
	} else {
#endif
	/* Tell the upper layer we are ready to transmit more packets */
		netif_wake_queue(dev->dev);
#ifdef CONFIG_PC300_MLPPP
	}
#endif
}

static void sca_intr(pc300_t * card)
{
	void __iomem *scabase = card->hw.scabase;
	volatile u32 status;
	int ch;
	int intr_count = 0;
	unsigned char dsr_rx;

	while ((status = cpc_readl(scabase + ISR0)) != 0) {
		for (ch = 0; ch < card->hw.nchan; ch++) {
			pc300ch_t *chan = &card->chan[ch];
			pc300dev_t *d = &chan->d;
			struct net_device *dev = d->dev;

			spin_lock(&card->card_lock);

	    /**** Reception ****/
			if (status & IR0_DRX((IR0_DMIA | IR0_DMIB), ch)) {
				u8 drx_stat = cpc_readb(scabase + DSR_RX(ch));

				/* Clear RX interrupts */
				cpc_writeb(scabase + DSR_RX(ch), drx_stat | DSR_DWE);

#ifdef PC300_DEBUG_INTR
				printk ("sca_intr: RX intr chan[%d] (st=0x%08lx, dsr=0x%02x)\n",
					 ch, status, drx_stat);
#endif
				if (status & IR0_DRX(IR0_DMIA, ch)) {
					if (drx_stat & DSR_BOF) {
#ifdef CONFIG_PC300_MLPPP
						if (chan->conf.proto == PC300_PROTO_MLPPP) {
							/* verify if driver is TTY */
							if ((cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
								rx_dma_stop(card, ch);
							}
							cpc_tty_receive(d);
							rx_dma_start(card, ch);
						} else 
#endif
						{
							if ((cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
								rx_dma_stop(card, ch);
							}
							cpc_net_rx(dev);
							/* Discard invalid frames */
							dev->stats.rx_errors++;
							dev->stats.rx_over_errors++;
							chan->rx_first_bd = 0;
							chan->rx_last_bd = N_DMA_RX_BUF - 1;
							rx_dma_start(card, ch);
						}
					}
				}
				if (status & IR0_DRX(IR0_DMIB, ch)) {
					if (drx_stat & DSR_EOM) {
						if (card->hw.type == PC300_TE) {
							cpc_writeb(card->hw.falcbase +
								   card->hw.cpld_reg2,
								   cpc_readb (card->hw.falcbase +
								    	card->hw.cpld_reg2) |
								   (CPLD_REG2_FALC_LED1 << (2 * ch)));
						}
#ifdef CONFIG_PC300_MLPPP
						if (chan->conf.proto == PC300_PROTO_MLPPP) {
							/* verify if driver is TTY */
							cpc_tty_receive(d);
						} else {
							cpc_net_rx(dev);
						}
#else
						cpc_net_rx(dev);
#endif
						if (card->hw.type == PC300_TE) {
							cpc_writeb(card->hw.falcbase +
								   card->hw.cpld_reg2,
								   cpc_readb (card->hw.falcbase +
								    		card->hw.cpld_reg2) &
								   ~ (CPLD_REG2_FALC_LED1 << (2 * ch)));
						}
					}
				}
				if (!(dsr_rx = cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
#ifdef PC300_DEBUG_INTR
		printk("%s: RX intr chan[%d] (st=0x%08lx, dsr=0x%02x, dsr2=0x%02x)\n",
			dev->name, ch, status, drx_stat, dsr_rx);
#endif
					cpc_writeb(scabase + DSR_RX(ch), (dsr_rx | DSR_DE) & 0xfe);
				}
			}

	    /**** Transmission ****/
			if (status & IR0_DTX((IR0_EFT | IR0_DMIA | IR0_DMIB), ch)) {
				u8 dtx_stat = cpc_readb(scabase + DSR_TX(ch));

				/* Clear TX interrupts */
				cpc_writeb(scabase + DSR_TX(ch), dtx_stat | DSR_DWE);

#ifdef PC300_DEBUG_INTR
				printk ("sca_intr: TX intr chan[%d] (st=0x%08lx, dsr=0x%02x)\n",
					 ch, status, dtx_stat);
#endif
				if (status & IR0_DTX(IR0_EFT, ch)) {
					if (dtx_stat & DSR_UDRF) {
						if (cpc_readb (scabase + M_REG(TBN, ch)) != 0) {
							cpc_writeb(scabase + M_REG(CMD,ch), CMD_TX_BUF_CLR);
						}
						if (card->hw.type == PC300_TE) {
							cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
								   cpc_readb (card->hw.falcbase + 
										   card->hw.cpld_reg2) &
								   ~ (CPLD_REG2_FALC_LED1 << (2 * ch)));
						}
						dev->stats.tx_errors++;
						dev->stats.tx_fifo_errors++;
						sca_tx_intr(d);
					}
				}
				if (status & IR0_DTX(IR0_DMIA, ch)) {
					if (dtx_stat & DSR_BOF) {
					}
				}
				if (status & IR0_DTX(IR0_DMIB, ch)) {
					if (dtx_stat & DSR_EOM) {
						if (card->hw.type == PC300_TE) {
							cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
								   cpc_readb (card->hw.falcbase +
								    			card->hw.cpld_reg2) &
								   ~ (CPLD_REG2_FALC_LED1 << (2 * ch)));
						}
						sca_tx_intr(d);
					}
				}
			}

	    /**** MSCI ****/
			if (status & IR0_M(IR0_RXINTA, ch)) {
				u8 st1 = cpc_readb(scabase + M_REG(ST1, ch));

				/* Clear MSCI interrupts */
				cpc_writeb(scabase + M_REG(ST1, ch), st1);

#ifdef PC300_DEBUG_INTR
				printk("sca_intr: MSCI intr chan[%d] (st=0x%08lx, st1=0x%02x)\n",
					 ch, status, st1);
#endif
				if (st1 & ST1_CDCD) {	/* DCD changed */
					if (cpc_readb(scabase + M_REG(ST3, ch)) & ST3_DCD) {
						printk ("%s: DCD is OFF. Going administrative down.\n",
							 dev->name);
#ifdef CONFIG_PC300_MLPPP
						if (chan->conf.proto != PC300_PROTO_MLPPP) {
							netif_carrier_off(dev);
						}
#else
						netif_carrier_off(dev);

#endif
						card->chan[ch].d.line_off++;
					} else {	/* DCD = 1 */
						printk ("%s: DCD is ON. Going administrative up.\n",
							 dev->name);
#ifdef CONFIG_PC300_MLPPP
						if (chan->conf.proto != PC300_PROTO_MLPPP)
							/* verify if driver is not TTY */
#endif
							netif_carrier_on(dev);
						card->chan[ch].d.line_on++;
					}
				}
			}
			spin_unlock(&card->card_lock);
		}
		if (++intr_count == 10)
			/* Too much work at this board. Force exit */
			break;
	}
}

static void falc_t1_loop_detection(pc300_t *card, int ch, u8 frs1)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_XPRBS) == 0) &&
	    !pfalc->loop_gen) {
		if (frs1 & FRS1_LLBDD) {
			// A Line Loop Back Deactivation signal detected
			if (pfalc->loop_active) {
				falc_remote_loop(card, ch, 0);
			}
		} else {
			if ((frs1 & FRS1_LLBAD) &&
			    ((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) == 0)) {
				// A Line Loop Back Activation signal detected  
				if (!pfalc->loop_active) {
					falc_remote_loop(card, ch, 1);
				}
			}
		}
	}
}

static void falc_e1_loop_detection(pc300_t *card, int ch, u8 rsp)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;

	if (((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_XPRBS) == 0) &&
	    !pfalc->loop_gen) {
		if (rsp & RSP_LLBDD) {
			// A Line Loop Back Deactivation signal detected
			if (pfalc->loop_active) {
				falc_remote_loop(card, ch, 0);
			}
		} else {
			if ((rsp & RSP_LLBAD) &&
			    ((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) == 0)) {
				// A Line Loop Back Activation signal detected  
				if (!pfalc->loop_active) {
					falc_remote_loop(card, ch, 1);
				}
			}
		}
	}
}

static void falc_t1_intr(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u8 isr0, isr3, gis;
	u8 dummy;

	while ((gis = cpc_readb(falcbase + F_REG(GIS, ch))) != 0) {
		if (gis & GIS_ISR0) {
			isr0 = cpc_readb(falcbase + F_REG(FISR0, ch));
			if (isr0 & FISR0_PDEN) {
				/* Read the bit to clear the situation */
				if (cpc_readb(falcbase + F_REG(FRS1, ch)) &
				    FRS1_PDEN) {
					pfalc->pden++;
				}
			}
		}

		if (gis & GIS_ISR1) {
			dummy = cpc_readb(falcbase + F_REG(FISR1, ch));
		}

		if (gis & GIS_ISR2) {
			dummy = cpc_readb(falcbase + F_REG(FISR2, ch));
		}

		if (gis & GIS_ISR3) {
			isr3 = cpc_readb(falcbase + F_REG(FISR3, ch));
			if (isr3 & FISR3_SEC) {
				pfalc->sec++;
				falc_update_stats(card, ch);
				falc_check_status(card, ch,
						  cpc_readb(falcbase + F_REG(FRS0, ch)));
			}
			if (isr3 & FISR3_ES) {
				pfalc->es++;
			}
			if (isr3 & FISR3_LLBSC) {
				falc_t1_loop_detection(card, ch,
						       cpc_readb(falcbase + F_REG(FRS1, ch)));
			}
		}
	}
}

static void falc_e1_intr(pc300_t * card, int ch)
{
	pc300ch_t *chan = (pc300ch_t *) & card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	void __iomem *falcbase = card->hw.falcbase;
	u8 isr1, isr2, isr3, gis, rsp;
	u8 dummy;

	while ((gis = cpc_readb(falcbase + F_REG(GIS, ch))) != 0) {
		rsp = cpc_readb(falcbase + F_REG(RSP, ch));

		if (gis & GIS_ISR0) {
			dummy = cpc_readb(falcbase + F_REG(FISR0, ch));
		}
		if (gis & GIS_ISR1) {
			isr1 = cpc_readb(falcbase + F_REG(FISR1, ch));
			if (isr1 & FISR1_XMB) {
				if ((pfalc->xmb_cause & 2) &&
				    pfalc->multiframe_mode) {
					if (cpc_readb (falcbase + F_REG(FRS0, ch)) & 
									(FRS0_LOS | FRS0_AIS | FRS0_LFA)) {
						cpc_writeb(falcbase + F_REG(XSP, ch),
							   cpc_readb(falcbase + F_REG(XSP, ch))
							   & ~XSP_AXS);
					} else {
						cpc_writeb(falcbase + F_REG(XSP, ch),
							   cpc_readb(falcbase + F_REG(XSP, ch))
							   | XSP_AXS);
					}
				}
				pfalc->xmb_cause = 0;
				cpc_writeb(falcbase + F_REG(IMR1, ch),
					   cpc_readb(falcbase + F_REG(IMR1, ch)) | IMR1_XMB);
			}
			if (isr1 & FISR1_LLBSC) {
				falc_e1_loop_detection(card, ch, rsp);
			}
		}
		if (gis & GIS_ISR2) {
			isr2 = cpc_readb(falcbase + F_REG(FISR2, ch));
			if (isr2 & FISR2_T400MS) {
				cpc_writeb(falcbase + F_REG(XSW, ch),
					   cpc_readb(falcbase + F_REG(XSW, ch)) | XSW_XRA);
			}
			if (isr2 & FISR2_MFAR) {
				cpc_writeb(falcbase + F_REG(XSW, ch),
					   cpc_readb(falcbase + F_REG(XSW, ch)) & ~XSW_XRA);
			}
			if (isr2 & (FISR2_FAR | FISR2_LFA | FISR2_AIS | FISR2_LOS)) {
				pfalc->xmb_cause |= 2;
				cpc_writeb(falcbase + F_REG(IMR1, ch),
					   cpc_readb(falcbase + F_REG(IMR1, ch)) & ~IMR1_XMB);
			}
		}
		if (gis & GIS_ISR3) {
			isr3 = cpc_readb(falcbase + F_REG(FISR3, ch));
			if (isr3 & FISR3_SEC) {
				pfalc->sec++;
				falc_update_stats(card, ch);
				falc_check_status(card, ch,
						  cpc_readb(falcbase + F_REG(FRS0, ch)));
			}
			if (isr3 & FISR3_ES) {
				pfalc->es++;
			}
		}
	}
}

static void falc_intr(pc300_t * card)
{
	int ch;

	for (ch = 0; ch < card->hw.nchan; ch++) {
		pc300ch_t *chan = &card->chan[ch];
		pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;

		if (conf->media == IF_IFACE_T1) {
			falc_t1_intr(card, ch);
		} else {
			falc_e1_intr(card, ch);
		}
	}
}

static irqreturn_t cpc_intr(int irq, void *dev_id)
{
	pc300_t *card = dev_id;
	volatile u8 plx_status;

	if (!card) {
#ifdef PC300_DEBUG_INTR
		printk("cpc_intr: spurious intr %d\n", irq);
#endif
		return IRQ_NONE;		/* spurious intr */
	}

	if (!card->hw.rambase) {
#ifdef PC300_DEBUG_INTR
		printk("cpc_intr: spurious intr2 %d\n", irq);
#endif
		return IRQ_NONE;		/* spurious intr */
	}

	switch (card->hw.type) {
		case PC300_RSV:
		case PC300_X21:
			sca_intr(card);
			break;

		case PC300_TE:
			while ( (plx_status = (cpc_readb(card->hw.plxbase + card->hw.intctl_reg) &
				 (PLX_9050_LINT1_STATUS | PLX_9050_LINT2_STATUS))) != 0) {
				if (plx_status & PLX_9050_LINT1_STATUS) {	/* SCA Interrupt */
					sca_intr(card);
				}
				if (plx_status & PLX_9050_LINT2_STATUS) {	/* FALC Interrupt */
					falc_intr(card);
				}
			}
			break;
	}
	return IRQ_HANDLED;
}

static void cpc_sca_status(pc300_t * card, int ch)
{
	u8 ilar;
	void __iomem *scabase = card->hw.scabase;
	unsigned long flags;

	tx_dma_buf_check(card, ch);
	rx_dma_buf_check(card, ch);
	ilar = cpc_readb(scabase + ILAR);
	printk ("ILAR=0x%02x, WCRL=0x%02x, PCR=0x%02x, BTCR=0x%02x, BOLR=0x%02x\n",
		 ilar, cpc_readb(scabase + WCRL), cpc_readb(scabase + PCR),
		 cpc_readb(scabase + BTCR), cpc_readb(scabase + BOLR));
	printk("TX_CDA=0x%08x, TX_EDA=0x%08x\n",
	       cpc_readl(scabase + DTX_REG(CDAL, ch)),
	       cpc_readl(scabase + DTX_REG(EDAL, ch)));
	printk("RX_CDA=0x%08x, RX_EDA=0x%08x, BFL=0x%04x\n",
	       cpc_readl(scabase + DRX_REG(CDAL, ch)),
	       cpc_readl(scabase + DRX_REG(EDAL, ch)),
	       cpc_readw(scabase + DRX_REG(BFLL, ch)));
	printk("DMER=0x%02x, DSR_TX=0x%02x, DSR_RX=0x%02x\n",
	       cpc_readb(scabase + DMER), cpc_readb(scabase + DSR_TX(ch)),
	       cpc_readb(scabase + DSR_RX(ch)));
	printk("DMR_TX=0x%02x, DMR_RX=0x%02x, DIR_TX=0x%02x, DIR_RX=0x%02x\n",
	       cpc_readb(scabase + DMR_TX(ch)), cpc_readb(scabase + DMR_RX(ch)),
	       cpc_readb(scabase + DIR_TX(ch)),
	       cpc_readb(scabase + DIR_RX(ch)));
	printk("DCR_TX=0x%02x, DCR_RX=0x%02x, FCT_TX=0x%02x, FCT_RX=0x%02x\n",
	       cpc_readb(scabase + DCR_TX(ch)), cpc_readb(scabase + DCR_RX(ch)),
	       cpc_readb(scabase + FCT_TX(ch)),
	       cpc_readb(scabase + FCT_RX(ch)));
	printk("MD0=0x%02x, MD1=0x%02x, MD2=0x%02x, MD3=0x%02x, IDL=0x%02x\n",
	       cpc_readb(scabase + M_REG(MD0, ch)),
	       cpc_readb(scabase + M_REG(MD1, ch)),
	       cpc_readb(scabase + M_REG(MD2, ch)),
	       cpc_readb(scabase + M_REG(MD3, ch)),
	       cpc_readb(scabase + M_REG(IDL, ch)));
	printk("CMD=0x%02x, SA0=0x%02x, SA1=0x%02x, TFN=0x%02x, CTL=0x%02x\n",
	       cpc_readb(scabase + M_REG(CMD, ch)),
	       cpc_readb(scabase + M_REG(SA0, ch)),
	       cpc_readb(scabase + M_REG(SA1, ch)),
	       cpc_readb(scabase + M_REG(TFN, ch)),
	       cpc_readb(scabase + M_REG(CTL, ch)));
	printk("ST0=0x%02x, ST1=0x%02x, ST2=0x%02x, ST3=0x%02x, ST4=0x%02x\n",
	       cpc_readb(scabase + M_REG(ST0, ch)),
	       cpc_readb(scabase + M_REG(ST1, ch)),
	       cpc_readb(scabase + M_REG(ST2, ch)),
	       cpc_readb(scabase + M_REG(ST3, ch)),
	       cpc_readb(scabase + M_REG(ST4, ch)));
	printk ("CST0=0x%02x, CST1=0x%02x, CST2=0x%02x, CST3=0x%02x, FST=0x%02x\n",
		 cpc_readb(scabase + M_REG(CST0, ch)),
		 cpc_readb(scabase + M_REG(CST1, ch)),
		 cpc_readb(scabase + M_REG(CST2, ch)),
		 cpc_readb(scabase + M_REG(CST3, ch)),
		 cpc_readb(scabase + M_REG(FST, ch)));
	printk("TRC0=0x%02x, TRC1=0x%02x, RRC=0x%02x, TBN=0x%02x, RBN=0x%02x\n",
	       cpc_readb(scabase + M_REG(TRC0, ch)),
	       cpc_readb(scabase + M_REG(TRC1, ch)),
	       cpc_readb(scabase + M_REG(RRC, ch)),
	       cpc_readb(scabase + M_REG(TBN, ch)),
	       cpc_readb(scabase + M_REG(RBN, ch)));
	printk("TFS=0x%02x, TNR0=0x%02x, TNR1=0x%02x, RNR=0x%02x\n",
	       cpc_readb(scabase + M_REG(TFS, ch)),
	       cpc_readb(scabase + M_REG(TNR0, ch)),
	       cpc_readb(scabase + M_REG(TNR1, ch)),
	       cpc_readb(scabase + M_REG(RNR, ch)));
	printk("TCR=0x%02x, RCR=0x%02x, TNR1=0x%02x, RNR=0x%02x\n",
	       cpc_readb(scabase + M_REG(TCR, ch)),
	       cpc_readb(scabase + M_REG(RCR, ch)),
	       cpc_readb(scabase + M_REG(TNR1, ch)),
	       cpc_readb(scabase + M_REG(RNR, ch)));
	printk("TXS=0x%02x, RXS=0x%02x, EXS=0x%02x, TMCT=0x%02x, TMCR=0x%02x\n",
	       cpc_readb(scabase + M_REG(TXS, ch)),
	       cpc_readb(scabase + M_REG(RXS, ch)),
	       cpc_readb(scabase + M_REG(EXS, ch)),
	       cpc_readb(scabase + M_REG(TMCT, ch)),
	       cpc_readb(scabase + M_REG(TMCR, ch)));
	printk("IE0=0x%02x, IE1=0x%02x, IE2=0x%02x, IE4=0x%02x, FIE=0x%02x\n",
	       cpc_readb(scabase + M_REG(IE0, ch)),
	       cpc_readb(scabase + M_REG(IE1, ch)),
	       cpc_readb(scabase + M_REG(IE2, ch)),
	       cpc_readb(scabase + M_REG(IE4, ch)),
	       cpc_readb(scabase + M_REG(FIE, ch)));
	printk("IER0=0x%08x\n", cpc_readl(scabase + IER0));

	if (ilar != 0) {
		CPC_LOCK(card, flags);
		cpc_writeb(scabase + ILAR, ilar);
		cpc_writeb(scabase + DMER, 0x80);
		CPC_UNLOCK(card, flags);
	}
}

static void cpc_falc_status(pc300_t * card, int ch)
{
	pc300ch_t *chan = &card->chan[ch];
	falc_t *pfalc = (falc_t *) & chan->falc;
	unsigned long flags;

	CPC_LOCK(card, flags);
	printk("CH%d:   %s %s  %d channels\n",
	       ch, (pfalc->sync ? "SYNC" : ""), (pfalc->active ? "ACTIVE" : ""),
	       pfalc->num_channels);

	printk("        pden=%d,  los=%d,  losr=%d,  lfa=%d,  farec=%d\n",
	       pfalc->pden, pfalc->los, pfalc->losr, pfalc->lfa, pfalc->farec);
	printk("        lmfa=%d,  ais=%d,  sec=%d,  es=%d,  rai=%d\n",
	       pfalc->lmfa, pfalc->ais, pfalc->sec, pfalc->es, pfalc->rai);
	printk("        bec=%d,  fec=%d,  cvc=%d,  cec=%d,  ebc=%d\n",
	       pfalc->bec, pfalc->fec, pfalc->cvc, pfalc->cec, pfalc->ebc);

	printk("\n");
	printk("        STATUS: %s  %s  %s  %s  %s  %s\n",
	       (pfalc->red_alarm ? "RED" : ""),
	       (pfalc->blue_alarm ? "BLU" : ""),
	       (pfalc->yellow_alarm ? "YEL" : ""),
	       (pfalc->loss_fa ? "LFA" : ""),
	       (pfalc->loss_mfa ? "LMF" : ""), (pfalc->prbs ? "PRB" : ""));
	CPC_UNLOCK(card, flags);
}

static int cpc_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 128) || (new_mtu > PC300_DEF_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static int cpc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	pc300conf_t conf_aux;
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	int ch = chan->channel;
	void __user *arg = ifr->ifr_data;
	struct if_settings *settings = &ifr->ifr_settings;
	void __iomem *scabase = card->hw.scabase;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
		case SIOCGPC300CONF:
#ifdef CONFIG_PC300_MLPPP
			if (conf->proto != PC300_PROTO_MLPPP) {
				conf->proto = /* FIXME hdlc->proto.id */ 0;
			}
#else
			conf->proto = /* FIXME hdlc->proto.id */ 0;
#endif
			memcpy(&conf_aux.conf, conf, sizeof(pc300chconf_t));
			memcpy(&conf_aux.hw, &card->hw, sizeof(pc300hw_t));
			if (!arg || 
				copy_to_user(arg, &conf_aux, sizeof(pc300conf_t))) 
				return -EINVAL;
			return 0;
		case SIOCSPC300CONF:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			if (!arg || 
				copy_from_user(&conf_aux.conf, arg, sizeof(pc300chconf_t)))
				return -EINVAL;
			if (card->hw.cpld_id < 0x02 &&
			    conf_aux.conf.fr_mode == PC300_FR_UNFRAMED) {
				/* CPLD_ID < 0x02 doesn't support Unframed E1 */
				return -EINVAL;
			}
#ifdef CONFIG_PC300_MLPPP
			if (conf_aux.conf.proto == PC300_PROTO_MLPPP) {
				if (conf->proto != PC300_PROTO_MLPPP) {
					memcpy(conf, &conf_aux.conf, sizeof(pc300chconf_t));
					cpc_tty_init(d);	/* init TTY driver */
				}
			} else {
				if (conf_aux.conf.proto == 0xffff) {
					if (conf->proto == PC300_PROTO_MLPPP){ 
						/* ifdown interface */
						cpc_close(dev);
					}
				} else {
					memcpy(conf, &conf_aux.conf, sizeof(pc300chconf_t));
					/* FIXME hdlc->proto.id = conf->proto; */
				}
			}
#else
			memcpy(conf, &conf_aux.conf, sizeof(pc300chconf_t));
			/* FIXME hdlc->proto.id = conf->proto; */
#endif
			return 0;
		case SIOCGPC300STATUS:
			cpc_sca_status(card, ch);
			return 0;
		case SIOCGPC300FALCSTATUS:
			cpc_falc_status(card, ch);
			return 0;

		case SIOCGPC300UTILSTATS:
			{
				if (!arg) {	/* clear statistics */
					memset(&dev->stats, 0, sizeof(dev->stats));
					if (card->hw.type == PC300_TE) {
						memset(&chan->falc, 0, sizeof(falc_t));
					}
				} else {
					pc300stats_t pc300stats;

					memset(&pc300stats, 0, sizeof(pc300stats_t));
					pc300stats.hw_type = card->hw.type;
					pc300stats.line_on = card->chan[ch].d.line_on;
					pc300stats.line_off = card->chan[ch].d.line_off;
					memcpy(&pc300stats.gen_stats, &dev->stats,
					       sizeof(dev->stats));
					if (card->hw.type == PC300_TE)
						memcpy(&pc300stats.te_stats,&chan->falc,sizeof(falc_t));
				    	if (copy_to_user(arg, &pc300stats, sizeof(pc300stats_t)))
						return -EFAULT;
				}
				return 0;
			}

		case SIOCGPC300UTILSTATUS:
			{
				struct pc300status pc300status;

				pc300status.hw_type = card->hw.type;
				if (card->hw.type == PC300_TE) {
					pc300status.te_status.sync = chan->falc.sync;
					pc300status.te_status.red_alarm = chan->falc.red_alarm;
					pc300status.te_status.blue_alarm = chan->falc.blue_alarm;
					pc300status.te_status.loss_fa = chan->falc.loss_fa;
					pc300status.te_status.yellow_alarm =chan->falc.yellow_alarm;
					pc300status.te_status.loss_mfa = chan->falc.loss_mfa;
					pc300status.te_status.prbs = chan->falc.prbs;
				} else {
					pc300status.gen_status.dcd =
						!(cpc_readb (scabase + M_REG(ST3, ch)) & ST3_DCD);
					pc300status.gen_status.cts =
						!(cpc_readb (scabase + M_REG(ST3, ch)) & ST3_CTS);
					pc300status.gen_status.rts =
						!(cpc_readb (scabase + M_REG(CTL, ch)) & CTL_RTS);
					pc300status.gen_status.dtr =
						!(cpc_readb (scabase + M_REG(CTL, ch)) & CTL_DTR);
					/* There is no DSR in HD64572 */
				}
				if (!arg ||
				    copy_to_user(arg, &pc300status, sizeof(pc300status_t)))
					return -EINVAL;
				return 0;
			}

		case SIOCSPC300TRACE:
			/* Sets/resets a trace_flag for the respective device */
			if (!arg || copy_from_user(&d->trace_on, arg,sizeof(unsigned char)))
					return -EINVAL;
			return 0;

		case SIOCSPC300LOOPBACK:
			{
				struct pc300loopback pc300loop;

				/* TE boards only */
				if (card->hw.type != PC300_TE)
					return -EINVAL;

				if (!arg || 
					copy_from_user(&pc300loop, arg, sizeof(pc300loopback_t)))
						return -EINVAL;
				switch (pc300loop.loop_type) {
					case PC300LOCLOOP:	/* Turn the local loop on/off */
						falc_local_loop(card, ch, pc300loop.loop_on);
						return 0;

					case PC300REMLOOP:	/* Turn the remote loop on/off */
						falc_remote_loop(card, ch, pc300loop.loop_on);
						return 0;

					case PC300PAYLOADLOOP:	/* Turn the payload loop on/off */
						falc_payload_loop(card, ch, pc300loop.loop_on);
						return 0;

					case PC300GENLOOPUP:	/* Generate loop UP */
						if (pc300loop.loop_on) {
							falc_generate_loop_up_code (card, ch);
						} else {
							turn_off_xlu(card, ch);
						}
						return 0;

					case PC300GENLOOPDOWN:	/* Generate loop DOWN */
						if (pc300loop.loop_on) {
							falc_generate_loop_down_code (card, ch);
						} else {
							turn_off_xld(card, ch);
						}
						return 0;

					default:
						return -EINVAL;
				}
			}

		case SIOCSPC300PATTERNTEST:
			/* Turn the pattern test on/off and show the errors counter */
			{
				struct pc300patterntst pc300patrntst;

				/* TE boards only */
				if (card->hw.type != PC300_TE)
					return -EINVAL;

				if (card->hw.cpld_id < 0x02) {
					/* CPLD_ID < 0x02 doesn't support pattern test */
					return -EINVAL;
				}

				if (!arg || 
					copy_from_user(&pc300patrntst,arg,sizeof(pc300patterntst_t)))
						return -EINVAL;
				if (pc300patrntst.patrntst_on == 2) {
					if (chan->falc.prbs == 0) {
						falc_pattern_test(card, ch, 1);
					}
					pc300patrntst.num_errors =
						falc_pattern_test_error(card, ch);
					if (copy_to_user(arg, &pc300patrntst,
							 sizeof(pc300patterntst_t)))
							return -EINVAL;
				} else {
					falc_pattern_test(card, ch, pc300patrntst.patrntst_on);
				}
				return 0;
			}

		case SIOCWANDEV:
			switch (ifr->ifr_settings.type) {
				case IF_GET_IFACE:
				{
					const size_t size = sizeof(sync_serial_settings);
					ifr->ifr_settings.type = conf->media;
					if (ifr->ifr_settings.size < size) {
						/* data size wanted */
						ifr->ifr_settings.size = size;
						return -ENOBUFS;
					}
	
					if (copy_to_user(settings->ifs_ifsu.sync,
							 &conf->phys_settings, size)) {
						return -EFAULT;
					}
					return 0;
				}

				case IF_IFACE_V35:
				case IF_IFACE_V24:
				case IF_IFACE_X21:
				{
					const size_t size = sizeof(sync_serial_settings);

					if (!capable(CAP_NET_ADMIN)) {
						return -EPERM;
					}
					/* incorrect data len? */
					if (ifr->ifr_settings.size != size) {
						return -ENOBUFS;
					}

					if (copy_from_user(&conf->phys_settings, 
							   settings->ifs_ifsu.sync, size)) {
						return -EFAULT;
					}

					if (conf->phys_settings.loopback) {
						cpc_writeb(card->hw.scabase + M_REG(MD2, ch),
							cpc_readb(card->hw.scabase + M_REG(MD2, ch)) | 
							MD2_LOOP_MIR);
					}
					conf->media = ifr->ifr_settings.type;
					return 0;
				}

				case IF_IFACE_T1:
				case IF_IFACE_E1:
				{
					const size_t te_size = sizeof(te1_settings);
					const size_t size = sizeof(sync_serial_settings);

					if (!capable(CAP_NET_ADMIN)) {
						return -EPERM;
					}

					/* incorrect data len? */
					if (ifr->ifr_settings.size != te_size) {
						return -ENOBUFS;
					}

					if (copy_from_user(&conf->phys_settings, 
							   settings->ifs_ifsu.te1, size)) {
						return -EFAULT;
					}/* Ignoring HDLC slot_map for a while */
					
					if (conf->phys_settings.loopback) {
						cpc_writeb(card->hw.scabase + M_REG(MD2, ch),
							cpc_readb(card->hw.scabase + M_REG(MD2, ch)) | 
							MD2_LOOP_MIR);
					}
					conf->media = ifr->ifr_settings.type;
					return 0;
				}
				default:
					return hdlc_ioctl(dev, ifr, cmd);
			}

		default:
			return hdlc_ioctl(dev, ifr, cmd);
	}
}

static int clock_rate_calc(u32 rate, u32 clock, int *br_io)
{
	int br, tc;
	int br_pwr, error;

	*br_io = 0;

	if (rate == 0)
		return 0;

	for (br = 0, br_pwr = 1; br <= 9; br++, br_pwr <<= 1) {
		if ((tc = clock / br_pwr / rate) <= 0xff) {
			*br_io = br;
			break;
		}
	}

	if (tc <= 0xff) {
		error = ((rate - (clock / br_pwr / rate)) / rate) * 1000;
		/* Errors bigger than +/- 1% won't be tolerated */
		if (error < -10 || error > 10)
			return -1;
		else
			return tc;
	} else {
		return -1;
	}
}

static int ch_config(pc300dev_t * d)
{
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300chconf_t *conf = (pc300chconf_t *) & chan->conf;
	pc300_t *card = (pc300_t *) chan->card;
	void __iomem *scabase = card->hw.scabase;
	void __iomem *plxbase = card->hw.plxbase;
	int ch = chan->channel;
	u32 clkrate = chan->conf.phys_settings.clock_rate;
	u32 clktype = chan->conf.phys_settings.clock_type;
	u16 encoding = chan->conf.proto_settings.encoding;
	u16 parity = chan->conf.proto_settings.parity;
	u8 md0, md2;

	/* Reset the channel */
	cpc_writeb(scabase + M_REG(CMD, ch), CMD_CH_RST);

	/* Configure the SCA registers */
	switch (parity) {
		case PARITY_NONE:
			md0 = MD0_BIT_SYNC;
			break;
		case PARITY_CRC16_PR0:
			md0 = MD0_CRC16_0|MD0_CRCC0|MD0_BIT_SYNC;
			break;
		case PARITY_CRC16_PR1:
			md0 = MD0_CRC16_1|MD0_CRCC0|MD0_BIT_SYNC;
			break;
		case PARITY_CRC32_PR1_CCITT:
			md0 = MD0_CRC32|MD0_CRCC0|MD0_BIT_SYNC;
			break;
		case PARITY_CRC16_PR1_CCITT:
		default:
			md0 = MD0_CRC_CCITT|MD0_CRCC0|MD0_BIT_SYNC;
			break;
	}
	switch (encoding) {
		case ENCODING_NRZI:
			md2 = MD2_F_DUPLEX|MD2_ADPLL_X8|MD2_NRZI;
			break;
		case ENCODING_FM_MARK:	/* FM1 */
			md2 = MD2_F_DUPLEX|MD2_ADPLL_X8|MD2_FM|MD2_FM1;
			break;
		case ENCODING_FM_SPACE:	/* FM0 */
			md2 = MD2_F_DUPLEX|MD2_ADPLL_X8|MD2_FM|MD2_FM0;
			break;
		case ENCODING_MANCHESTER: /* It's not working... */
			md2 = MD2_F_DUPLEX|MD2_ADPLL_X8|MD2_FM|MD2_MANCH;
			break;
		case ENCODING_NRZ:
		default:
			md2 = MD2_F_DUPLEX|MD2_ADPLL_X8|MD2_NRZ;
			break;
	}
	cpc_writeb(scabase + M_REG(MD0, ch), md0);
	cpc_writeb(scabase + M_REG(MD1, ch), 0);
	cpc_writeb(scabase + M_REG(MD2, ch), md2);
 	cpc_writeb(scabase + M_REG(IDL, ch), 0x7e);
	cpc_writeb(scabase + M_REG(CTL, ch), CTL_URSKP | CTL_IDLC);

	/* Configure HW media */
	switch (card->hw.type) {
		case PC300_RSV:
			if (conf->media == IF_IFACE_V35) {
				cpc_writel((plxbase + card->hw.gpioc_reg),
					   cpc_readl(plxbase + card->hw.gpioc_reg) | PC300_CHMEDIA_MASK(ch));
			} else {
				cpc_writel((plxbase + card->hw.gpioc_reg),
					   cpc_readl(plxbase + card->hw.gpioc_reg) & ~PC300_CHMEDIA_MASK(ch));
			}
			break;

		case PC300_X21:
			break;

		case PC300_TE:
			te_config(card, ch);
			break;
	}

	switch (card->hw.type) {
		case PC300_RSV:
		case PC300_X21:
			if (clktype == CLOCK_INT || clktype == CLOCK_TXINT) {
				int tmc, br;

				/* Calculate the clkrate parameters */
				tmc = clock_rate_calc(clkrate, card->hw.clock, &br);
				if (tmc < 0)
					return -EIO;
				cpc_writeb(scabase + M_REG(TMCT, ch), tmc);
				cpc_writeb(scabase + M_REG(TXS, ch),
					   (TXS_DTRXC | TXS_IBRG | br));
				if (clktype == CLOCK_INT) {
					cpc_writeb(scabase + M_REG(TMCR, ch), tmc);
					cpc_writeb(scabase + M_REG(RXS, ch), 
						   (RXS_IBRG | br));
				} else {
					cpc_writeb(scabase + M_REG(TMCR, ch), 1);
					cpc_writeb(scabase + M_REG(RXS, ch), 0);
				}
	    			if (card->hw.type == PC300_X21) {
					cpc_writeb(scabase + M_REG(GPO, ch), 1);
					cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1 | EXS_RES1);
				} else {
					cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1);
				}
			} else {
				cpc_writeb(scabase + M_REG(TMCT, ch), 1);
				if (clktype == CLOCK_EXT) {
					cpc_writeb(scabase + M_REG(TXS, ch), 
						   TXS_DTRXC);
				} else {
					cpc_writeb(scabase + M_REG(TXS, ch), 
						   TXS_DTRXC|TXS_RCLK);
				}
	    			cpc_writeb(scabase + M_REG(TMCR, ch), 1);
				cpc_writeb(scabase + M_REG(RXS, ch), 0);
				if (card->hw.type == PC300_X21) {
					cpc_writeb(scabase + M_REG(GPO, ch), 0);
					cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1 | EXS_RES1);
				} else {
					cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1);
				}
			}
			break;

		case PC300_TE:
			/* SCA always receives clock from the FALC chip */
			cpc_writeb(scabase + M_REG(TMCT, ch), 1);
			cpc_writeb(scabase + M_REG(TXS, ch), 0);
			cpc_writeb(scabase + M_REG(TMCR, ch), 1);
			cpc_writeb(scabase + M_REG(RXS, ch), 0);
			cpc_writeb(scabase + M_REG(EXS, ch), 0);
			break;
	}

	/* Enable Interrupts */
	cpc_writel(scabase + IER0,
		   cpc_readl(scabase + IER0) |
		   IR0_M(IR0_RXINTA, ch) |
		   IR0_DRX(IR0_EFT | IR0_DMIA | IR0_DMIB, ch) |
		   IR0_DTX(IR0_EFT | IR0_DMIA | IR0_DMIB, ch));
	cpc_writeb(scabase + M_REG(IE0, ch),
		   cpc_readl(scabase + M_REG(IE0, ch)) | IE0_RXINTA);
	cpc_writeb(scabase + M_REG(IE1, ch),
		   cpc_readl(scabase + M_REG(IE1, ch)) | IE1_CDCD);

	return 0;
}

static int rx_config(pc300dev_t * d)
{
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	void __iomem *scabase = card->hw.scabase;
	int ch = chan->channel;

	cpc_writeb(scabase + DSR_RX(ch), 0);

	/* General RX settings */
	cpc_writeb(scabase + M_REG(RRC, ch), 0);
	cpc_writeb(scabase + M_REG(RNR, ch), 16);

	/* Enable reception */
	cpc_writeb(scabase + M_REG(CMD, ch), CMD_RX_CRC_INIT);
	cpc_writeb(scabase + M_REG(CMD, ch), CMD_RX_ENA);

	/* Initialize DMA stuff */
	chan->rx_first_bd = 0;
	chan->rx_last_bd = N_DMA_RX_BUF - 1;
	rx_dma_buf_init(card, ch);
	cpc_writeb(scabase + DCR_RX(ch), DCR_FCT_CLR);
	cpc_writeb(scabase + DMR_RX(ch), (DMR_TMOD | DMR_NF));
	cpc_writeb(scabase + DIR_RX(ch), (DIR_EOM | DIR_BOF));

	/* Start DMA */
	rx_dma_start(card, ch);

	return 0;
}

static int tx_config(pc300dev_t * d)
{
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	void __iomem *scabase = card->hw.scabase;
	int ch = chan->channel;

	cpc_writeb(scabase + DSR_TX(ch), 0);

	/* General TX settings */
	cpc_writeb(scabase + M_REG(TRC0, ch), 0);
	cpc_writeb(scabase + M_REG(TFS, ch), 32);
	cpc_writeb(scabase + M_REG(TNR0, ch), 20);
	cpc_writeb(scabase + M_REG(TNR1, ch), 48);
	cpc_writeb(scabase + M_REG(TCR, ch), 8);

	/* Enable transmission */
	cpc_writeb(scabase + M_REG(CMD, ch), CMD_TX_CRC_INIT);

	/* Initialize DMA stuff */
	chan->tx_first_bd = 0;
	chan->tx_next_bd = 0;
	tx_dma_buf_init(card, ch);
	cpc_writeb(scabase + DCR_TX(ch), DCR_FCT_CLR);
	cpc_writeb(scabase + DMR_TX(ch), (DMR_TMOD | DMR_NF));
	cpc_writeb(scabase + DIR_TX(ch), (DIR_EOM | DIR_BOF | DIR_UDRF));
	cpc_writel(scabase + DTX_REG(CDAL, ch), TX_BD_ADDR(ch, chan->tx_first_bd));
	cpc_writel(scabase + DTX_REG(EDAL, ch), TX_BD_ADDR(ch, chan->tx_next_bd));

	return 0;
}

static int cpc_attach(struct net_device *dev, unsigned short encoding,
		      unsigned short parity)
{
	pc300dev_t *d = (pc300dev_t *)dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *)d->chan;
	pc300_t *card = (pc300_t *)chan->card;
	pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;

	if (card->hw.type == PC300_TE) {
		if (encoding != ENCODING_NRZ && encoding != ENCODING_NRZI) {
			return -EINVAL;
		}
	} else {
		if (encoding != ENCODING_NRZ && encoding != ENCODING_NRZI &&
		    encoding != ENCODING_FM_MARK && encoding != ENCODING_FM_SPACE) {
			/* Driver doesn't support ENCODING_MANCHESTER yet */
			return -EINVAL;
		}
	}

	if (parity != PARITY_NONE && parity != PARITY_CRC16_PR0 &&
	    parity != PARITY_CRC16_PR1 && parity != PARITY_CRC32_PR1_CCITT &&
	    parity != PARITY_CRC16_PR1_CCITT) {
		return -EINVAL;
	}

	conf->proto_settings.encoding = encoding;
	conf->proto_settings.parity = parity;
	return 0;
}

static int cpc_opench(pc300dev_t * d)
{
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	int ch = chan->channel, rc;
	void __iomem *scabase = card->hw.scabase;

	rc = ch_config(d);
	if (rc)
		return rc;

	rx_config(d);

	tx_config(d);

	/* Assert RTS and DTR */
	cpc_writeb(scabase + M_REG(CTL, ch),
		   cpc_readb(scabase + M_REG(CTL, ch)) & ~(CTL_RTS | CTL_DTR));

	return 0;
}

static void cpc_closech(pc300dev_t * d)
{
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	falc_t *pfalc = (falc_t *) & chan->falc;
	int ch = chan->channel;

	cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_CH_RST);
	rx_dma_stop(card, ch);
	tx_dma_stop(card, ch);

	if (card->hw.type == PC300_TE) {
		memset(pfalc, 0, sizeof(falc_t));
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) &
			   ~((CPLD_REG2_FALC_TX_CLK | CPLD_REG2_FALC_RX_CLK |
			      CPLD_REG2_FALC_LED2) << (2 * ch)));
		/* Reset the FALC chip */
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) |
			   (CPLD_REG1_FALC_RESET << (2 * ch)));
		udelay(10000);
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) &
			   ~(CPLD_REG1_FALC_RESET << (2 * ch)));
	}
}

int cpc_open(struct net_device *dev)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	struct ifreq ifr;
	int result;

#ifdef	PC300_DEBUG_OTHER
	printk("pc300: cpc_open");
#endif

	result = hdlc_open(dev);

	if (result)
		return result;

	sprintf(ifr.ifr_name, "%s", dev->name);
	result = cpc_opench(d);
	if (result)
		goto err_out;

	netif_start_queue(dev);
	return 0;

err_out:
	hdlc_close(dev);
	return result;
}

static int cpc_close(struct net_device *dev)
{
	pc300dev_t *d = (pc300dev_t *) dev_to_hdlc(dev)->priv;
	pc300ch_t *chan = (pc300ch_t *) d->chan;
	pc300_t *card = (pc300_t *) chan->card;
	unsigned long flags;

#ifdef	PC300_DEBUG_OTHER
	printk("pc300: cpc_close");
#endif

	netif_stop_queue(dev);

	CPC_LOCK(card, flags);
	cpc_closech(d);
	CPC_UNLOCK(card, flags);

	hdlc_close(dev);

#ifdef CONFIG_PC300_MLPPP
	if (chan->conf.proto == PC300_PROTO_MLPPP) {
		cpc_tty_unregister_service(d);
		chan->conf.proto = 0xffff;
	}
#endif

	return 0;
}

static u32 detect_ram(pc300_t * card)
{
	u32 i;
	u8 data;
	void __iomem *rambase = card->hw.rambase;

	card->hw.ramsize = PC300_RAMSIZE;
	/* Let's find out how much RAM is present on this board */
	for (i = 0; i < card->hw.ramsize; i++) {
		data = (u8)(i & 0xff);
		cpc_writeb(rambase + i, data);
		if (cpc_readb(rambase + i) != data) {
			break;
		}
	}
	return i;
}

static void plx_init(pc300_t * card)
{
	struct RUNTIME_9050 __iomem *plx_ctl = card->hw.plxbase;

	/* Reset PLX */
	cpc_writel(&plx_ctl->init_ctrl,
		   cpc_readl(&plx_ctl->init_ctrl) | 0x40000000);
	udelay(10000L);
	cpc_writel(&plx_ctl->init_ctrl,
		   cpc_readl(&plx_ctl->init_ctrl) & ~0x40000000);

	/* Reload Config. Registers from EEPROM */
	cpc_writel(&plx_ctl->init_ctrl,
		   cpc_readl(&plx_ctl->init_ctrl) | 0x20000000);
	udelay(10000L);
	cpc_writel(&plx_ctl->init_ctrl,
		   cpc_readl(&plx_ctl->init_ctrl) & ~0x20000000);

}

static void show_version(void)
{
	char *rcsvers, *rcsdate, *tmp;

	rcsvers = strchr(rcsid, ' ');
	rcsvers++;
	tmp = strchr(rcsvers, ' ');
	*tmp++ = '\0';
	rcsdate = strchr(tmp, ' ');
	rcsdate++;
	tmp = strrchr(rcsdate, ' ');
	*tmp = '\0';
	pr_info("Cyclades-PC300 driver %s %s\n", rcsvers, rcsdate);
}				/* show_version */

static const struct net_device_ops cpc_netdev_ops = {
	.ndo_open		= cpc_open,
	.ndo_stop		= cpc_close,
	.ndo_tx_timeout		= cpc_tx_timeout,
	.ndo_set_mac_address	= NULL,
	.ndo_change_mtu		= cpc_change_mtu,
	.ndo_do_ioctl		= cpc_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};

static void cpc_init_card(pc300_t * card)
{
	int i, devcount = 0;
	static int board_nbr = 1;

	/* Enable interrupts on the PCI bridge */
	plx_init(card);
	cpc_writew(card->hw.plxbase + card->hw.intctl_reg,
		   cpc_readw(card->hw.plxbase + card->hw.intctl_reg) | 0x0040);

#ifdef USE_PCI_CLOCK
	/* Set board clock to PCI clock */
	cpc_writel(card->hw.plxbase + card->hw.gpioc_reg,
		   cpc_readl(card->hw.plxbase + card->hw.gpioc_reg) | 0x00000004UL);
	card->hw.clock = PC300_PCI_CLOCK;
#else
	/* Set board clock to internal oscillator clock */
	cpc_writel(card->hw.plxbase + card->hw.gpioc_reg,
		   cpc_readl(card->hw.plxbase + card->hw.gpioc_reg) & ~0x00000004UL);
	card->hw.clock = PC300_OSC_CLOCK;
#endif

	/* Detect actual on-board RAM size */
	card->hw.ramsize = detect_ram(card);

	/* Set Global SCA-II registers */
	cpc_writeb(card->hw.scabase + PCR, PCR_PR2);
	cpc_writeb(card->hw.scabase + BTCR, 0x10);
	cpc_writeb(card->hw.scabase + WCRL, 0);
	cpc_writeb(card->hw.scabase + DMER, 0x80);

	if (card->hw.type == PC300_TE) {
		u8 reg1;

		/* Check CPLD version */
		reg1 = cpc_readb(card->hw.falcbase + CPLD_REG1);
		cpc_writeb(card->hw.falcbase + CPLD_REG1, (reg1 + 0x5a));
		if (cpc_readb(card->hw.falcbase + CPLD_REG1) == reg1) {
			/* New CPLD */
			card->hw.cpld_id = cpc_readb(card->hw.falcbase + CPLD_ID_REG);
			card->hw.cpld_reg1 = CPLD_V2_REG1;
			card->hw.cpld_reg2 = CPLD_V2_REG2;
		} else {
			/* old CPLD */
			card->hw.cpld_id = 0;
			card->hw.cpld_reg1 = CPLD_REG1;
			card->hw.cpld_reg2 = CPLD_REG2;
			cpc_writeb(card->hw.falcbase + CPLD_REG1, reg1);
		}

		/* Enable the board's global clock */
		cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
			   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) |
			   CPLD_REG1_GLOBAL_CLK);

	}

	for (i = 0; i < card->hw.nchan; i++) {
		pc300ch_t *chan = &card->chan[i];
		pc300dev_t *d = &chan->d;
		hdlc_device *hdlc;
		struct net_device *dev;

		chan->card = card;
		chan->channel = i;
		chan->conf.phys_settings.clock_rate = 0;
		chan->conf.phys_settings.clock_type = CLOCK_EXT;
		chan->conf.proto_settings.encoding = ENCODING_NRZ;
		chan->conf.proto_settings.parity = PARITY_CRC16_PR1_CCITT;
		switch (card->hw.type) {
			case PC300_TE:
				chan->conf.media = IF_IFACE_T1;
				chan->conf.lcode = PC300_LC_B8ZS;
				chan->conf.fr_mode = PC300_FR_ESF;
				chan->conf.lbo = PC300_LBO_0_DB;
				chan->conf.rx_sens = PC300_RX_SENS_SH;
				chan->conf.tslot_bitmap = 0xffffffffUL;
				break;

			case PC300_X21:
				chan->conf.media = IF_IFACE_X21;
				break;

			case PC300_RSV:
			default:
				chan->conf.media = IF_IFACE_V35;
				break;
		}
		chan->conf.proto = IF_PROTO_PPP;
		chan->tx_first_bd = 0;
		chan->tx_next_bd = 0;
		chan->rx_first_bd = 0;
		chan->rx_last_bd = N_DMA_RX_BUF - 1;
		chan->nfree_tx_bd = N_DMA_TX_BUF;

		d->chan = chan;
		d->trace_on = 0;
		d->line_on = 0;
		d->line_off = 0;

		dev = alloc_hdlcdev(d);
		if (dev == NULL)
			continue;

		hdlc = dev_to_hdlc(dev);
		hdlc->xmit = cpc_queue_xmit;
		hdlc->attach = cpc_attach;
		d->dev = dev;
		dev->mem_start = card->hw.ramphys;
		dev->mem_end = card->hw.ramphys + card->hw.ramsize - 1;
		dev->irq = card->hw.irq;
		dev->tx_queue_len = PC300_TX_QUEUE_LEN;
		dev->mtu = PC300_DEF_MTU;

		dev->netdev_ops = &cpc_netdev_ops;
		dev->watchdog_timeo = PC300_TX_TIMEOUT;

		if (register_hdlc_device(dev) == 0) {
			printk("%s: Cyclades-PC300/", dev->name);
			switch (card->hw.type) {
				case PC300_TE:
					if (card->hw.bus == PC300_PMC) {
						printk("TE-M");
					} else {
						printk("TE  ");
					}
					break;

				case PC300_X21:
					printk("X21 ");
					break;

				case PC300_RSV:
				default:
					printk("RSV ");
					break;
			}
			printk (" #%d, %dKB of RAM at 0x%08x, IRQ%d, channel %d.\n",
				 board_nbr, card->hw.ramsize / 1024,
				 card->hw.ramphys, card->hw.irq, i + 1);
			devcount++;
		} else {
			printk ("Dev%d on card(0x%08x): unable to allocate i/f name.\n",
				 i + 1, card->hw.ramphys);
			free_netdev(dev);
			continue;
		}
	}
	spin_lock_init(&card->card_lock);

	board_nbr++;
}

static int
cpc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err, eeprom_outdated = 0;
	u16 device_id;
	pc300_t *card;

	if ((err = pci_enable_device(pdev)) < 0)
		return err;

	card = kzalloc(sizeof(pc300_t), GFP_KERNEL);
	if (card == NULL) {
		printk("PC300 found at RAM 0x%016llx, "
		       "but could not allocate card structure.\n",
		       (unsigned long long)pci_resource_start(pdev, 3));
		err = -ENOMEM;
		goto err_disable_dev;
	}

	err = -ENODEV;

	/* read PCI configuration area */
	device_id = ent->device;
	card->hw.irq = pdev->irq;
	card->hw.iophys = pci_resource_start(pdev, 1);
	card->hw.iosize = pci_resource_len(pdev, 1);
	card->hw.scaphys = pci_resource_start(pdev, 2);
	card->hw.scasize = pci_resource_len(pdev, 2);
	card->hw.ramphys = pci_resource_start(pdev, 3);
	card->hw.alloc_ramsize = pci_resource_len(pdev, 3);
	card->hw.falcphys = pci_resource_start(pdev, 4);
	card->hw.falcsize = pci_resource_len(pdev, 4);
	card->hw.plxphys = pci_resource_start(pdev, 5);
	card->hw.plxsize = pci_resource_len(pdev, 5);

	switch (device_id) {
		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_TE_1:
		case PCI_DEVICE_ID_PC300_TE_M_1:
			card->hw.nchan = 1;
			break;

		case PCI_DEVICE_ID_PC300_RX_2:
		case PCI_DEVICE_ID_PC300_TE_2:
		case PCI_DEVICE_ID_PC300_TE_M_2:
		default:
			card->hw.nchan = PC300_MAXCHAN;
			break;
	}
#ifdef PC300_DEBUG_PCI
	printk("cpc (bus=0x0%x,pci_id=0x%x,", pdev->bus->number, pdev->devfn);
	printk("rev_id=%d) IRQ%d\n", pdev->revision, card->hw.irq);
	printk("cpc:found  ramaddr=0x%08lx plxaddr=0x%08lx "
	       "ctladdr=0x%08lx falcaddr=0x%08lx\n",
	       card->hw.ramphys, card->hw.plxphys, card->hw.scaphys,
	       card->hw.falcphys);
#endif
	/* Although we don't use this I/O region, we should
	 * request it from the kernel anyway, to avoid problems
	 * with other drivers accessing it. */
	if (!request_region(card->hw.iophys, card->hw.iosize, "PLX Registers")) {
		/* In case we can't allocate it, warn user */
		printk("WARNING: couldn't allocate I/O region for PC300 board "
		       "at 0x%08x!\n", card->hw.ramphys);
	}

	if (card->hw.plxphys) {
		pci_write_config_dword(pdev, PCI_BASE_ADDRESS_0, card->hw.plxphys);
	} else {
		eeprom_outdated = 1;
		card->hw.plxphys = pci_resource_start(pdev, 0);
		card->hw.plxsize = pci_resource_len(pdev, 0);
	}

	if (!request_mem_region(card->hw.plxphys, card->hw.plxsize,
				"PLX Registers")) {
		printk("PC300 found at RAM 0x%08x, "
		       "but could not allocate PLX mem region.\n",
		       card->hw.ramphys);
		goto err_release_io;
	}
	if (!request_mem_region(card->hw.ramphys, card->hw.alloc_ramsize,
				"On-board RAM")) {
		printk("PC300 found at RAM 0x%08x, "
		       "but could not allocate RAM mem region.\n",
		       card->hw.ramphys);
		goto err_release_plx;
	}
	if (!request_mem_region(card->hw.scaphys, card->hw.scasize,
				"SCA-II Registers")) {
		printk("PC300 found at RAM 0x%08x, "
		       "but could not allocate SCA mem region.\n",
		       card->hw.ramphys);
		goto err_release_ram;
	}

	card->hw.plxbase = ioremap(card->hw.plxphys, card->hw.plxsize);
	card->hw.rambase = ioremap(card->hw.ramphys, card->hw.alloc_ramsize);
	card->hw.scabase = ioremap(card->hw.scaphys, card->hw.scasize);
	switch (device_id) {
		case PCI_DEVICE_ID_PC300_TE_1:
		case PCI_DEVICE_ID_PC300_TE_2:
		case PCI_DEVICE_ID_PC300_TE_M_1:
		case PCI_DEVICE_ID_PC300_TE_M_2:
			request_mem_region(card->hw.falcphys, card->hw.falcsize,
					   "FALC Registers");
			card->hw.falcbase = ioremap(card->hw.falcphys, card->hw.falcsize);
			break;

		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_RX_2:
		default:
			card->hw.falcbase = NULL;
			break;
	}

#ifdef PC300_DEBUG_PCI
	printk("cpc: relocate ramaddr=0x%08lx plxaddr=0x%08lx "
	       "ctladdr=0x%08lx falcaddr=0x%08lx\n",
	       card->hw.rambase, card->hw.plxbase, card->hw.scabase,
	       card->hw.falcbase);
#endif

	/* Set PCI drv pointer to the card structure */
	pci_set_drvdata(pdev, card);

	/* Set board type */
	switch (device_id) {
		case PCI_DEVICE_ID_PC300_TE_1:
		case PCI_DEVICE_ID_PC300_TE_2:
		case PCI_DEVICE_ID_PC300_TE_M_1:
		case PCI_DEVICE_ID_PC300_TE_M_2:
			card->hw.type = PC300_TE;

			if ((device_id == PCI_DEVICE_ID_PC300_TE_M_1) ||
			    (device_id == PCI_DEVICE_ID_PC300_TE_M_2)) {
				card->hw.bus = PC300_PMC;
				/* Set PLX register offsets */
				card->hw.gpioc_reg = 0x54;
				card->hw.intctl_reg = 0x4c;
			} else {
				card->hw.bus = PC300_PCI;
				/* Set PLX register offsets */
				card->hw.gpioc_reg = 0x50;
				card->hw.intctl_reg = 0x4c;
			}
			break;

		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_RX_2:
		default:
			card->hw.bus = PC300_PCI;
			/* Set PLX register offsets */
			card->hw.gpioc_reg = 0x50;
			card->hw.intctl_reg = 0x4c;

			if ((cpc_readl(card->hw.plxbase + card->hw.gpioc_reg) & PC300_CTYPE_MASK)) {
				card->hw.type = PC300_X21;
			} else {
				card->hw.type = PC300_RSV;
			}
			break;
	}

	/* Allocate IRQ */
	if (request_irq(card->hw.irq, cpc_intr, IRQF_SHARED, "Cyclades-PC300", card)) {
		printk ("PC300 found at RAM 0x%08x, but could not allocate IRQ%d.\n",
			 card->hw.ramphys, card->hw.irq);
		goto err_io_unmap;
	}

	cpc_init_card(card);

	if (eeprom_outdated)
		printk("WARNING: PC300 with outdated EEPROM.\n");
	return 0;

err_io_unmap:
	iounmap(card->hw.plxbase);
	iounmap(card->hw.scabase);
	iounmap(card->hw.rambase);
	if (card->hw.type == PC300_TE) {
		iounmap(card->hw.falcbase);
		release_mem_region(card->hw.falcphys, card->hw.falcsize);
	}
	release_mem_region(card->hw.scaphys, card->hw.scasize);
err_release_ram:
	release_mem_region(card->hw.ramphys, card->hw.alloc_ramsize);
err_release_plx:
	release_mem_region(card->hw.plxphys, card->hw.plxsize);
err_release_io:
	release_region(card->hw.iophys, card->hw.iosize);
	kfree(card);
err_disable_dev:
	pci_disable_device(pdev);
	return err;
}

static void __devexit cpc_remove_one(struct pci_dev *pdev)
{
	pc300_t *card = pci_get_drvdata(pdev);

	if (card->hw.rambase) {
		int i;

		/* Disable interrupts on the PCI bridge */
		cpc_writew(card->hw.plxbase + card->hw.intctl_reg,
			   cpc_readw(card->hw.plxbase + card->hw.intctl_reg) & ~(0x0040));

		for (i = 0; i < card->hw.nchan; i++) {
			unregister_hdlc_device(card->chan[i].d.dev);
		}
		iounmap(card->hw.plxbase);
		iounmap(card->hw.scabase);
		iounmap(card->hw.rambase);
		release_mem_region(card->hw.plxphys, card->hw.plxsize);
		release_mem_region(card->hw.ramphys, card->hw.alloc_ramsize);
		release_mem_region(card->hw.scaphys, card->hw.scasize);
		release_region(card->hw.iophys, card->hw.iosize);
		if (card->hw.type == PC300_TE) {
			iounmap(card->hw.falcbase);
			release_mem_region(card->hw.falcphys, card->hw.falcsize);
		}
		for (i = 0; i < card->hw.nchan; i++)
			if (card->chan[i].d.dev)
				free_netdev(card->chan[i].d.dev);
		if (card->hw.irq)
			free_irq(card->hw.irq, card);
		kfree(card);
		pci_disable_device(pdev);
	}
}

static struct pci_driver cpc_driver = {
	.name           = "pc300",
	.id_table       = cpc_pci_dev_id,
	.probe          = cpc_init_one,
	.remove         = cpc_remove_one,
};

static int __init cpc_init(void)
{
	show_version();
	return pci_register_driver(&cpc_driver);
}

static void __exit cpc_cleanup_module(void)
{
	pci_unregister_driver(&cpc_driver);
}

module_init(cpc_init);
module_exit(cpc_cleanup_module);

MODULE_DESCRIPTION("Cyclades-PC300 cards driver");
MODULE_AUTHOR(  "Author: Ivan Passos <ivan@cyclades.com>\r\n"
                "Maintainer: PC300 Maintainer <pc300@cyclades.com");
MODULE_LICENSE("GPL");

