/*****************************************************************************
 *                                                                           *
 * File: sge.c                                                               *
 * $Revision: 1.13 $                                                         *
 * $Date: 2005/03/23 07:41:27 $                                              *
 * Description:                                                              *
 *  DMA engine.                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#include "common.h"

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_arp.h>

#include "cpl5_cmd.h"
#include "sge.h"
#include "regs.h"
#include "espi.h"

#include <linux/tcp.h>

#define SGE_CMDQ_N		2
#define SGE_FREELQ_N		2
#define SGE_CMDQ0_E_N		512
#define SGE_CMDQ1_E_N		128
#define SGE_FREEL_SIZE		4096
#define SGE_JUMBO_FREEL_SIZE	512
#define SGE_FREEL_REFILL_THRESH	16
#define SGE_RESPQ_E_N		1024
#define SGE_INTR_BUCKETSIZE	100
#define SGE_INTR_LATBUCKETS	5
#define SGE_INTR_MAXBUCKETS	11
#define SGE_INTRTIMER0		1
#define SGE_INTRTIMER1		50
#define SGE_INTRTIMER_NRES	10000
#define SGE_RX_COPY_THRESHOLD	256
#define SGE_RX_SM_BUF_SIZE	1536

#define SGE_RESPQ_REPLENISH_THRES ((3 * SGE_RESPQ_E_N) / 4)

#define SGE_RX_OFFSET 2
#ifndef NET_IP_ALIGN
# define NET_IP_ALIGN SGE_RX_OFFSET
#endif

/*
 * Memory Mapped HW Command, Freelist and Response Queue Descriptors
 */
#if defined(__BIG_ENDIAN_BITFIELD)
struct cmdQ_e {
	u32 AddrLow;
	u32 GenerationBit	: 1;
	u32 BufferLength	: 31;
	u32 RespQueueSelector	: 4;
	u32 ResponseTokens	: 12;
	u32 CmdId		: 8;
	u32 Reserved		: 3;
	u32 TokenValid		: 1;
	u32 Eop			: 1;
	u32 Sop			: 1;
	u32 DataValid		: 1;
	u32 GenerationBit2	: 1;
	u32 AddrHigh;
};

struct freelQ_e {
	u32 AddrLow;
	u32 GenerationBit	: 1;
	u32 BufferLength	: 31;
	u32 Reserved		: 31;
	u32 GenerationBit2	: 1;
	u32 AddrHigh;
};

struct respQ_e {
	u32 Qsleeping		: 4;
	u32 Cmdq1CreditReturn	: 5;
	u32 Cmdq1DmaComplete	: 5;
	u32 Cmdq0CreditReturn	: 5;
	u32 Cmdq0DmaComplete	: 5;
	u32 FreelistQid		: 2;
	u32 CreditValid		: 1;
	u32 DataValid		: 1;
	u32 Offload		: 1;
	u32 Eop			: 1;
	u32 Sop			: 1;
	u32 GenerationBit	: 1;
	u32 BufferLength;
};

#elif defined(__LITTLE_ENDIAN_BITFIELD)
struct cmdQ_e {
	u32 BufferLength	: 31;
	u32 GenerationBit	: 1;
	u32 AddrLow;
	u32 AddrHigh;
	u32 GenerationBit2	: 1;
	u32 DataValid		: 1;
	u32 Sop			: 1;
	u32 Eop			: 1;
	u32 TokenValid		: 1;
	u32 Reserved		: 3;
	u32 CmdId		: 8;
	u32 ResponseTokens	: 12;
	u32 RespQueueSelector	: 4;
};

struct freelQ_e {
	u32 BufferLength	: 31;
	u32 GenerationBit	: 1;
	u32 AddrLow;
	u32 AddrHigh;
	u32 GenerationBit2	: 1;
	u32 Reserved		: 31;
};

struct respQ_e {
	u32 BufferLength;
	u32 GenerationBit	: 1;
	u32 Sop			: 1;
	u32 Eop			: 1;
	u32 Offload		: 1;
	u32 DataValid		: 1;
	u32 CreditValid		: 1;
	u32 FreelistQid		: 2;
	u32 Cmdq0DmaComplete	: 5;
	u32 Cmdq0CreditReturn	: 5;
	u32 Cmdq1DmaComplete	: 5;
	u32 Cmdq1CreditReturn	: 5;
	u32 Qsleeping		: 4;
} ;
#endif

/*
 * SW Context Command and Freelist Queue Descriptors
 */
struct cmdQ_ce {
	struct sk_buff *skb;
	DECLARE_PCI_UNMAP_ADDR(dma_addr);
	DECLARE_PCI_UNMAP_LEN(dma_len);
	unsigned int single;
};

struct freelQ_ce {
	struct sk_buff *skb;
	DECLARE_PCI_UNMAP_ADDR(dma_addr);
	DECLARE_PCI_UNMAP_LEN(dma_len);
};

/*
 * SW Command, Freelist and Response Queue
 */
struct cmdQ {
	atomic_t	asleep;		/* HW DMA Fetch status */
	atomic_t	credits;	/* # available descriptors for TX */
	atomic_t	pio_pidx;	/* Variable updated on Doorbell */
	u16		entries_n;	/* # descriptors for TX	*/
	u16		pidx;		/* producer index (SW) */
	u16		cidx;		/* consumer index (HW) */
	u8		genbit;		/* current generation (=valid) bit */
	struct cmdQ_e  *entries;	/* HW command descriptor Q */
	struct cmdQ_ce *centries;	/* SW command context descriptor Q */
	spinlock_t	Qlock;		/* Lock to protect cmdQ enqueuing */
	dma_addr_t	dma_addr;	/* DMA addr HW command descriptor Q */
};

struct freelQ {
	unsigned int	credits;	/* # of available RX buffers */
	unsigned int	entries_n;	/* free list capacity */
	u16		pidx;		/* producer index (SW) */
	u16		cidx;		/* consumer index (HW) */
	u16		rx_buffer_size; /* Buffer size on this free list */
	u16		dma_offset;     /* DMA offset to align IP headers */
	u8		genbit;		/* current generation (=valid) bit */
	struct freelQ_e	*entries;	/* HW freelist descriptor Q */
	struct freelQ_ce *centries;	/* SW freelist conext descriptor Q */
	dma_addr_t	dma_addr;	/* DMA addr HW freelist descriptor Q */
};

struct respQ {
	u16		credits;	/* # of available respQ descriptors */
	u16		credits_pend;	/* # of not yet returned descriptors */
	u16		entries_n;	/* # of response Q descriptors */
	u16		pidx;		/* producer index (HW) */
	u16		cidx;		/* consumer index (SW) */
	u8		genbit;		/* current generation(=valid) bit */
	struct respQ_e *entries;        /* HW response descriptor Q */
	dma_addr_t	dma_addr;	/* DMA addr HW response descriptor Q */
};

/*
 * Main SGE data structure
 *
 * Interrupts are handled by a single CPU and it is likely that on a MP system
 * the application is migrated to another CPU. In that scenario, we try to
 * seperate the RX(in irq context) and TX state in order to decrease memory
 * contention.
 */
struct sge {
	struct adapter *adapter; 	/* adapter backpointer */
	struct freelQ 	freelQ[SGE_FREELQ_N]; /* freelist Q(s) */
	struct respQ 	respQ;		/* response Q instatiation */
	unsigned int	rx_pkt_pad;     /* RX padding for L2 packets */
	unsigned int	jumbo_fl;       /* jumbo freelist Q index */
	u32		intrtimer[SGE_INTR_MAXBUCKETS];	/* ! */
	u32		currIndex;	/* current index into intrtimer[] */
	u32		intrtimer_nres;	/* no resource interrupt timer value */
	u32		sge_control;	/* shadow content of sge control reg */
	struct sge_intr_counts intr_cnt;
	struct timer_list ptimer;
	struct sk_buff  *pskb;
	u32		ptimeout;
	struct cmdQ cmdQ[SGE_CMDQ_N] ____cacheline_aligned; /* command Q(s)*/
};

static unsigned int t1_sge_tx(struct sk_buff *skb, struct adapter *adapter,
			unsigned int qid);

/*
 * PIO to indicate that memory mapped Q contains valid descriptor(s).
 */
static inline void doorbell_pio(struct sge *sge, u32 val)
{
	wmb();
	t1_write_reg_4(sge->adapter, A_SG_DOORBELL, val);
}

/*
 * Disables the DMA engine.
 */
void t1_sge_stop(struct sge *sge)
{
	t1_write_reg_4(sge->adapter, A_SG_CONTROL, 0);
	t1_read_reg_4(sge->adapter, A_SG_CONTROL);     /* flush write */
	if (is_T2(sge->adapter))
		del_timer_sync(&sge->ptimer);
}

static u8 ch_mac_addr[ETH_ALEN] = {0x0, 0x7, 0x43, 0x0, 0x0, 0x0};
static void t1_espi_workaround(void *data)
{
	struct adapter *adapter = (struct adapter *)data;
	struct sge *sge = adapter->sge;

	if (netif_running(adapter->port[0].dev) &&
		atomic_read(&sge->cmdQ[0].asleep)) {

		u32 seop = t1_espi_get_mon(adapter, 0x930, 0);

		if ((seop & 0xfff0fff) == 0xfff && sge->pskb) {
			struct sk_buff *skb = sge->pskb;
			if (!skb->cb[0]) {
				memcpy(skb->data+sizeof(struct cpl_tx_pkt), ch_mac_addr, ETH_ALEN);
				memcpy(skb->data+skb->len-10, ch_mac_addr, ETH_ALEN);

				skb->cb[0] = 0xff;
			}
			t1_sge_tx(skb, adapter,0);
		}
	}
	mod_timer(&adapter->sge->ptimer, jiffies + sge->ptimeout);
}

/*
 * Enables the DMA engine.
 */
void t1_sge_start(struct sge *sge)
{
	t1_write_reg_4(sge->adapter, A_SG_CONTROL, sge->sge_control);
	t1_read_reg_4(sge->adapter, A_SG_CONTROL);     /* flush write */
	if (is_T2(sge->adapter)) {
		init_timer(&sge->ptimer);
		sge->ptimer.function = (void *)&t1_espi_workaround;
		sge->ptimer.data = (unsigned long)sge->adapter;
		sge->ptimer.expires = jiffies + sge->ptimeout;
		add_timer(&sge->ptimer);
	}
}

/*
 * Creates a t1_sge structure and returns suggested resource parameters.
 */
struct sge * __devinit t1_sge_create(struct adapter *adapter,
				     struct sge_params *p)
{
	struct sge *sge = kmalloc(sizeof(*sge), GFP_KERNEL);

	if (!sge)
		return NULL;
	memset(sge, 0, sizeof(*sge));

	if (is_T2(adapter))
		sge->ptimeout = 1;	/* finest allowed */

	sge->adapter = adapter;
	sge->rx_pkt_pad = t1_is_T1B(adapter) ? 0 : SGE_RX_OFFSET;
	sge->jumbo_fl = t1_is_T1B(adapter) ? 1 : 0;

	p->cmdQ_size[0] = SGE_CMDQ0_E_N;
	p->cmdQ_size[1] = SGE_CMDQ1_E_N;
	p->freelQ_size[!sge->jumbo_fl] = SGE_FREEL_SIZE;
	p->freelQ_size[sge->jumbo_fl] = SGE_JUMBO_FREEL_SIZE;
	p->rx_coalesce_usecs = SGE_INTRTIMER1;
	p->last_rx_coalesce_raw = SGE_INTRTIMER1 *
	  (board_info(sge->adapter)->clock_core / 1000000);
	p->default_rx_coalesce_usecs = SGE_INTRTIMER1;
	p->coalesce_enable = 0;	/* Turn off adaptive algorithm by default */
	p->sample_interval_usecs = 0;
	return sge;
}

/*
 * Frees all RX buffers on the freelist Q. The caller must make sure that
 * the SGE is turned off before calling this function.
 */
static void free_freelQ_buffers(struct pci_dev *pdev, struct freelQ *Q)
{
	unsigned int cidx = Q->cidx, credits = Q->credits;

	while (credits--) {
		struct freelQ_ce *ce = &Q->centries[cidx];

		pci_unmap_single(pdev, pci_unmap_addr(ce, dma_addr),
				 pci_unmap_len(ce, dma_len),
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb(ce->skb);
		ce->skb = NULL;
		if (++cidx == Q->entries_n)
			cidx = 0;
	}
}

/*
 * Free RX free list and response queue resources.
 */
static void free_rx_resources(struct sge *sge)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int size, i;

	if (sge->respQ.entries) {
		size = sizeof(struct respQ_e) * sge->respQ.entries_n;
		pci_free_consistent(pdev, size, sge->respQ.entries,
				    sge->respQ.dma_addr);
	}

	for (i = 0; i < SGE_FREELQ_N; i++) {
		struct freelQ *Q = &sge->freelQ[i];

		if (Q->centries) {
			free_freelQ_buffers(pdev, Q);
			kfree(Q->centries);
		}
		if (Q->entries) {
			size = sizeof(struct freelQ_e) * Q->entries_n;
			pci_free_consistent(pdev, size, Q->entries,
					    Q->dma_addr);
		}
	}
}

/*
 * Allocates basic RX resources, consisting of memory mapped freelist Qs and a
 * response Q.
 */
static int alloc_rx_resources(struct sge *sge, struct sge_params *p)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int size, i;

	for (i = 0; i < SGE_FREELQ_N; i++) {
		struct freelQ *Q = &sge->freelQ[i];

		Q->genbit = 1;
		Q->entries_n = p->freelQ_size[i];
		Q->dma_offset = SGE_RX_OFFSET - sge->rx_pkt_pad;
		size = sizeof(struct freelQ_e) * Q->entries_n;
		Q->entries = (struct freelQ_e *)
			      pci_alloc_consistent(pdev, size, &Q->dma_addr);
		if (!Q->entries)
			goto err_no_mem;
		memset(Q->entries, 0, size);
		Q->centries = kcalloc(Q->entries_n, sizeof(struct freelQ_ce),
				      GFP_KERNEL);
		if (!Q->centries)
			goto err_no_mem;
	}

	/*
	 * Calculate the buffer sizes for the two free lists.  FL0 accommodates
	 * regular sized Ethernet frames, FL1 is sized not to exceed 16K,
	 * including all the sk_buff overhead.
	 *
	 * Note: For T2 FL0 and FL1 are reversed.
	 */
	sge->freelQ[!sge->jumbo_fl].rx_buffer_size = SGE_RX_SM_BUF_SIZE +
		sizeof(struct cpl_rx_data) +
		sge->freelQ[!sge->jumbo_fl].dma_offset;
	sge->freelQ[sge->jumbo_fl].rx_buffer_size = (16 * 1024) -
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	sge->respQ.genbit = 1;
	sge->respQ.entries_n = SGE_RESPQ_E_N;
	sge->respQ.credits = SGE_RESPQ_E_N;
	size = sizeof(struct respQ_e) * sge->respQ.entries_n;
	sge->respQ.entries = (struct respQ_e *)
		pci_alloc_consistent(pdev, size, &sge->respQ.dma_addr);
	if (!sge->respQ.entries)
		goto err_no_mem;
	memset(sge->respQ.entries, 0, size);
	return 0;

err_no_mem:
	free_rx_resources(sge);
	return -ENOMEM;
}

/*
 * Frees 'credits_pend' TX buffers and returns the credits to Q->credits.
 *
 * The adaptive algorithm receives the total size of the buffers freed
 * accumulated in @*totpayload. No initialization of this argument here.
 *
 */
static void free_cmdQ_buffers(struct sge *sge, struct cmdQ *Q,
			      unsigned int credits_pend, unsigned int *totpayload)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	struct sk_buff *skb;
	struct cmdQ_ce *ce, *cq = Q->centries;
	unsigned int entries_n = Q->entries_n, cidx = Q->cidx,
		     i = credits_pend;


	ce = &cq[cidx];
	while (i--) {
		if (ce->single)
			pci_unmap_single(pdev, pci_unmap_addr(ce, dma_addr),
					 pci_unmap_len(ce, dma_len),
					 PCI_DMA_TODEVICE);
		else
			pci_unmap_page(pdev, pci_unmap_addr(ce, dma_addr),
				       pci_unmap_len(ce, dma_len),
				       PCI_DMA_TODEVICE);
		if (totpayload)
			*totpayload += pci_unmap_len(ce, dma_len);

		skb = ce->skb;
		if (skb)
			dev_kfree_skb_irq(skb);

		ce++;
		if (++cidx == entries_n) {
			cidx = 0;
			ce = cq;
		}
	}

	Q->cidx = cidx;
	atomic_add(credits_pend, &Q->credits);
}

/*
 * Free TX resources.
 *
 * Assumes that SGE is stopped and all interrupts are disabled.
 */
static void free_tx_resources(struct sge *sge)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int size, i;

	for (i = 0; i < SGE_CMDQ_N; i++) {
		struct cmdQ *Q = &sge->cmdQ[i];

		if (Q->centries) {
			unsigned int pending = Q->entries_n -
					       atomic_read(&Q->credits);

			if (pending)
				free_cmdQ_buffers(sge, Q, pending, NULL);
			kfree(Q->centries);
		}
		if (Q->entries) {
			size = sizeof(struct cmdQ_e) * Q->entries_n;
			pci_free_consistent(pdev, size, Q->entries,
					    Q->dma_addr);
		}
	}
}

/*
 * Allocates basic TX resources, consisting of memory mapped command Qs.
 */
static int alloc_tx_resources(struct sge *sge, struct sge_params *p)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int size, i;

	for (i = 0; i < SGE_CMDQ_N; i++) {
		struct cmdQ *Q = &sge->cmdQ[i];

		Q->genbit = 1;
		Q->entries_n = p->cmdQ_size[i];
		atomic_set(&Q->credits, Q->entries_n);
		atomic_set(&Q->asleep, 1);
		spin_lock_init(&Q->Qlock);
		size = sizeof(struct cmdQ_e) * Q->entries_n;
		Q->entries = (struct cmdQ_e *)
			      pci_alloc_consistent(pdev, size, &Q->dma_addr);
		if (!Q->entries)
			goto err_no_mem;
		memset(Q->entries, 0, size);
		Q->centries = kcalloc(Q->entries_n, sizeof(struct cmdQ_ce),
				      GFP_KERNEL);
		if (!Q->centries)
			goto err_no_mem;
	}

	return 0;

err_no_mem:
	free_tx_resources(sge);
	return -ENOMEM;
}

static inline void setup_ring_params(struct adapter *adapter, u64 addr,
				     u32 size, int base_reg_lo,
				     int base_reg_hi, int size_reg)
{
	t1_write_reg_4(adapter, base_reg_lo, (u32)addr);
	t1_write_reg_4(adapter, base_reg_hi, addr >> 32);
	t1_write_reg_4(adapter, size_reg, size);
}

/*
 * Enable/disable VLAN acceleration.
 */
void t1_set_vlan_accel(struct adapter *adapter, int on_off)
{
	struct sge *sge = adapter->sge;

	sge->sge_control &= ~F_VLAN_XTRACT;
	if (on_off)
		sge->sge_control |= F_VLAN_XTRACT;
	if (adapter->open_device_map) {
		t1_write_reg_4(adapter, A_SG_CONTROL, sge->sge_control);
		t1_read_reg_4(adapter, A_SG_CONTROL);	/* flush */
	}
}

/*
 * Sets the interrupt latency timer when the adaptive Rx coalescing
 * is turned off. Do nothing when it is turned on again.
 *
 * This routine relies on the fact that the caller has already set
 * the adaptive policy in adapter->sge_params before calling it.
*/
int t1_sge_set_coalesce_params(struct sge *sge, struct sge_params *p)
{
	if (!p->coalesce_enable) {
		u32 newTimer = p->rx_coalesce_usecs *
			(board_info(sge->adapter)->clock_core / 1000000);

		t1_write_reg_4(sge->adapter, A_SG_INTRTIMER, newTimer);
	}
	return 0;
}

/*
 * Programs the various SGE registers. However, the engine is not yet enabled,
 * but sge->sge_control is setup and ready to go.
 */
static void configure_sge(struct sge *sge, struct sge_params *p)
{
	struct adapter *ap = sge->adapter;
	int i;

	t1_write_reg_4(ap, A_SG_CONTROL, 0);
	setup_ring_params(ap, sge->cmdQ[0].dma_addr, sge->cmdQ[0].entries_n,
			  A_SG_CMD0BASELWR, A_SG_CMD0BASEUPR, A_SG_CMD0SIZE);
	setup_ring_params(ap, sge->cmdQ[1].dma_addr, sge->cmdQ[1].entries_n,
			  A_SG_CMD1BASELWR, A_SG_CMD1BASEUPR, A_SG_CMD1SIZE);
	setup_ring_params(ap, sge->freelQ[0].dma_addr,
			  sge->freelQ[0].entries_n, A_SG_FL0BASELWR,
			  A_SG_FL0BASEUPR, A_SG_FL0SIZE);
	setup_ring_params(ap, sge->freelQ[1].dma_addr,
			  sge->freelQ[1].entries_n, A_SG_FL1BASELWR,
			  A_SG_FL1BASEUPR, A_SG_FL1SIZE);

	/* The threshold comparison uses <. */
	t1_write_reg_4(ap, A_SG_FLTHRESHOLD, SGE_RX_SM_BUF_SIZE + 1);

	setup_ring_params(ap, sge->respQ.dma_addr, sge->respQ.entries_n,
			A_SG_RSPBASELWR, A_SG_RSPBASEUPR, A_SG_RSPSIZE);
	t1_write_reg_4(ap, A_SG_RSPQUEUECREDIT, (u32)sge->respQ.entries_n);

	sge->sge_control = F_CMDQ0_ENABLE | F_CMDQ1_ENABLE | F_FL0_ENABLE |
		F_FL1_ENABLE | F_CPL_ENABLE | F_RESPONSE_QUEUE_ENABLE |
		V_CMDQ_PRIORITY(2) | F_DISABLE_CMDQ1_GTS | F_ISCSI_COALESCE |
		V_RX_PKT_OFFSET(sge->rx_pkt_pad);

#if defined(__BIG_ENDIAN_BITFIELD)
	sge->sge_control |= F_ENABLE_BIG_ENDIAN;
#endif

	/*
	 * Initialize the SGE Interrupt Timer arrray:
	 * intrtimer[0]	      = (SGE_INTRTIMER0) usec
	 * intrtimer[0<i<5]   = (SGE_INTRTIMER0 + i*2) usec
	 * intrtimer[4<i<10]  = ((i - 3) * 6) usec
	 * intrtimer[10]      = (SGE_INTRTIMER1) usec
	 *
	 */
	sge->intrtimer[0] = board_info(sge->adapter)->clock_core / 1000000;
	for (i = 1; i < SGE_INTR_LATBUCKETS; ++i) {
		sge->intrtimer[i] = SGE_INTRTIMER0 + (2 * i);
		sge->intrtimer[i] *= sge->intrtimer[0];
	}
	for (i = SGE_INTR_LATBUCKETS; i < SGE_INTR_MAXBUCKETS - 1; ++i) {
		sge->intrtimer[i] = (i - 3) * 6;
		sge->intrtimer[i] *= sge->intrtimer[0];
	}
	sge->intrtimer[SGE_INTR_MAXBUCKETS - 1] =
	  sge->intrtimer[0] * SGE_INTRTIMER1;
	/* Initialize resource timer */
	sge->intrtimer_nres = sge->intrtimer[0] * SGE_INTRTIMER_NRES;
	/* Finally finish initialization of intrtimer[0] */
	sge->intrtimer[0] *= SGE_INTRTIMER0;
	/* Initialize for a throughput oriented workload */
	sge->currIndex = SGE_INTR_MAXBUCKETS - 1;

	if (p->coalesce_enable)
		t1_write_reg_4(ap, A_SG_INTRTIMER,
			       sge->intrtimer[sge->currIndex]);
	else
		t1_sge_set_coalesce_params(sge, p);
}

/*
 * Return the payload capacity of the jumbo free-list buffers.
 */
static inline unsigned int jumbo_payload_capacity(const struct sge *sge)
{
	return sge->freelQ[sge->jumbo_fl].rx_buffer_size -
		sizeof(struct cpl_rx_data) - SGE_RX_OFFSET + sge->rx_pkt_pad;
}

/*
 * Allocates both RX and TX resources and configures the SGE. However,
 * the hardware is not enabled yet.
 */
int t1_sge_configure(struct sge *sge, struct sge_params *p)
{
	if (alloc_rx_resources(sge, p))
		return -ENOMEM;
	if (alloc_tx_resources(sge, p)) {
		free_rx_resources(sge);
		return -ENOMEM;
	}
	configure_sge(sge, p);

	/*
	 * Now that we have sized the free lists calculate the payload
	 * capacity of the large buffers.  Other parts of the driver use
	 * this to set the max offload coalescing size so that RX packets
	 * do not overflow our large buffers.
	 */
	p->large_buf_capacity = jumbo_payload_capacity(sge);
	return 0;
}

/*
 * Frees all SGE related resources and the sge structure itself
 */
void t1_sge_destroy(struct sge *sge)
{
	if (sge->pskb)
		dev_kfree_skb(sge->pskb);
	free_tx_resources(sge);
	free_rx_resources(sge);
	kfree(sge);
}

/*
 * Allocates new RX buffers on the freelist Q (and tracks them on the freelist
 * context Q) until the Q is full or alloc_skb fails.
 *
 * It is possible that the generation bits already match, indicating that the
 * buffer is already valid and nothing needs to be done. This happens when we
 * copied a received buffer into a new sk_buff during the interrupt processing.
 *
 * If the SGE doesn't automatically align packets properly (!sge->rx_pkt_pad),
 * we specify a RX_OFFSET in order to make sure that the IP header is 4B
 * aligned.
 */
static void refill_free_list(struct sge *sge, struct freelQ *Q)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	struct freelQ_ce *ce = &Q->centries[Q->pidx];
	struct freelQ_e *e = &Q->entries[Q->pidx];
	unsigned int dma_len = Q->rx_buffer_size - Q->dma_offset;


	while (Q->credits < Q->entries_n) {
		if (e->GenerationBit != Q->genbit) {
			struct sk_buff *skb;
			dma_addr_t mapping;

			skb = alloc_skb(Q->rx_buffer_size, GFP_ATOMIC);
			if (!skb)
				break;
			if (Q->dma_offset)
				skb_reserve(skb, Q->dma_offset);
			mapping = pci_map_single(pdev, skb->data, dma_len,
						 PCI_DMA_FROMDEVICE);
			ce->skb = skb;
			pci_unmap_addr_set(ce, dma_addr, mapping);
			pci_unmap_len_set(ce, dma_len, dma_len);
			e->AddrLow = (u32)mapping;
			e->AddrHigh = (u64)mapping >> 32;
			e->BufferLength = dma_len;
			e->GenerationBit = e->GenerationBit2 = Q->genbit;
		}

		e++;
		ce++;
		if (++Q->pidx == Q->entries_n) {
			Q->pidx = 0;
			Q->genbit ^= 1;
			ce = Q->centries;
			e = Q->entries;
		}
		Q->credits++;
	}

}

/*
 * Calls refill_free_list for both freelist Qs. If we cannot
 * fill at least 1/4 of both Qs, we go into 'few interrupt mode' in order
 * to give the system time to free up resources.
 */
static void freelQs_empty(struct sge *sge)
{
	u32 irq_reg = t1_read_reg_4(sge->adapter, A_SG_INT_ENABLE);
	u32 irqholdoff_reg;

	refill_free_list(sge, &sge->freelQ[0]);
	refill_free_list(sge, &sge->freelQ[1]);

	if (sge->freelQ[0].credits > (sge->freelQ[0].entries_n >> 2) &&
	    sge->freelQ[1].credits > (sge->freelQ[1].entries_n >> 2)) {
		irq_reg |= F_FL_EXHAUSTED;
		irqholdoff_reg = sge->intrtimer[sge->currIndex];
	} else {
		/* Clear the F_FL_EXHAUSTED interrupts for now */
		irq_reg &= ~F_FL_EXHAUSTED;
		irqholdoff_reg = sge->intrtimer_nres;
	}
	t1_write_reg_4(sge->adapter, A_SG_INTRTIMER, irqholdoff_reg);
	t1_write_reg_4(sge->adapter, A_SG_INT_ENABLE, irq_reg);

	/* We reenable the Qs to force a freelist GTS interrupt later */
	doorbell_pio(sge, F_FL0_ENABLE | F_FL1_ENABLE);
}

#define SGE_PL_INTR_MASK (F_PL_INTR_SGE_ERR | F_PL_INTR_SGE_DATA)
#define SGE_INT_FATAL (F_RESPQ_OVERFLOW | F_PACKET_TOO_BIG | F_PACKET_MISMATCH)
#define SGE_INT_ENABLE (F_RESPQ_EXHAUSTED | F_RESPQ_OVERFLOW | \
			F_FL_EXHAUSTED | F_PACKET_TOO_BIG | F_PACKET_MISMATCH)

/*
 * Disable SGE Interrupts
 */
void t1_sge_intr_disable(struct sge *sge)
{
	u32 val = t1_read_reg_4(sge->adapter, A_PL_ENABLE);

	t1_write_reg_4(sge->adapter, A_PL_ENABLE, val & ~SGE_PL_INTR_MASK);
	t1_write_reg_4(sge->adapter, A_SG_INT_ENABLE, 0);
}

/*
 * Enable SGE interrupts.
 */
void t1_sge_intr_enable(struct sge *sge)
{
	u32 en = SGE_INT_ENABLE;
	u32 val = t1_read_reg_4(sge->adapter, A_PL_ENABLE);

	if (sge->adapter->flags & TSO_CAPABLE)
		en &= ~F_PACKET_TOO_BIG;
	t1_write_reg_4(sge->adapter, A_SG_INT_ENABLE, en);
	t1_write_reg_4(sge->adapter, A_PL_ENABLE, val | SGE_PL_INTR_MASK);
}

/*
 * Clear SGE interrupts.
 */
void t1_sge_intr_clear(struct sge *sge)
{
	t1_write_reg_4(sge->adapter, A_PL_CAUSE, SGE_PL_INTR_MASK);
	t1_write_reg_4(sge->adapter, A_SG_INT_CAUSE, 0xffffffff);
}

/*
 * SGE 'Error' interrupt handler
 */
int t1_sge_intr_error_handler(struct sge *sge)
{
	struct adapter *adapter = sge->adapter;
	u32 cause = t1_read_reg_4(adapter, A_SG_INT_CAUSE);

	if (adapter->flags & TSO_CAPABLE)
		cause &= ~F_PACKET_TOO_BIG;
	if (cause & F_RESPQ_EXHAUSTED)
		sge->intr_cnt.respQ_empty++;
	if (cause & F_RESPQ_OVERFLOW) {
		sge->intr_cnt.respQ_overflow++;
		CH_ALERT("%s: SGE response queue overflow\n",
			 adapter->name);
	}
	if (cause & F_FL_EXHAUSTED) {
		sge->intr_cnt.freelistQ_empty++;
		freelQs_empty(sge);
	}
	if (cause & F_PACKET_TOO_BIG) {
		sge->intr_cnt.pkt_too_big++;
		CH_ALERT("%s: SGE max packet size exceeded\n",
			 adapter->name);
	}
	if (cause & F_PACKET_MISMATCH) {
		sge->intr_cnt.pkt_mismatch++;
		CH_ALERT("%s: SGE packet mismatch\n", adapter->name);
	}
	if (cause & SGE_INT_FATAL)
		t1_fatal_err(adapter);

	t1_write_reg_4(adapter, A_SG_INT_CAUSE, cause);
	return 0;
}

/*
 * The following code is copied from 2.6, where the skb_pull is doing the
 * right thing and only pulls ETH_HLEN.
 *
 *	Determine the packet's protocol ID. The rule here is that we
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 */
static unsigned short sge_eth_type_trans(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb->mac.raw = skb->data;
	skb_pull(skb, ETH_HLEN);
	eth = (struct ethhdr *)skb->mac.raw;

	if (*eth->h_dest&1) {
		if(memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}

	/*
	 *	This ALLMULTI check should be redundant by 1.4
	 *	so don't forget to remove it.
	 *
	 *	Seems, you forgot to remove it. All silly devices
	 *	seems to set IFF_PROMISC.
	 */

	else if (1 /*dev->flags&IFF_PROMISC*/)
	{
		if(memcmp(eth->h_dest,dev->dev_addr, ETH_ALEN))
			skb->pkt_type=PACKET_OTHERHOST;
	}

	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;

	rawp = skb->data;

	/*
	 * This is a magic hack to spot IPX packets. Older Novell breaks
	 * the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 * layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 * won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);

	/*
	 * Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

/*
 * Prepare the received buffer and pass it up the stack. If it is small enough
 * and allocation doesn't fail, we use a new sk_buff and copy the content.
 */
static unsigned int t1_sge_rx(struct sge *sge, struct freelQ *Q,
			      unsigned int len, unsigned int offload)
{
	struct sk_buff *skb;
	struct adapter *adapter = sge->adapter;
	struct freelQ_ce *ce = &Q->centries[Q->cidx];

	if (len <= SGE_RX_COPY_THRESHOLD &&
	    (skb = alloc_skb(len + NET_IP_ALIGN, GFP_ATOMIC))) {
		struct freelQ_e *e;
		char *src = ce->skb->data;

		pci_dma_sync_single_for_cpu(adapter->pdev,
					    pci_unmap_addr(ce, dma_addr),
					    pci_unmap_len(ce, dma_len),
					    PCI_DMA_FROMDEVICE);
		if (!offload) {
			skb_reserve(skb, NET_IP_ALIGN);
			src += sge->rx_pkt_pad;
		}
		memcpy(skb->data, src, len);

		/* Reuse the entry. */
		e = &Q->entries[Q->cidx];
		e->GenerationBit  ^= 1;
		e->GenerationBit2 ^= 1;
	} else {
		pci_unmap_single(adapter->pdev, pci_unmap_addr(ce, dma_addr),
				 pci_unmap_len(ce, dma_len),
				 PCI_DMA_FROMDEVICE);
		skb = ce->skb;
		if (!offload && sge->rx_pkt_pad)
			__skb_pull(skb, sge->rx_pkt_pad);
	}

	skb_put(skb, len);


	if (unlikely(offload)) {
		{
			printk(KERN_ERR
			       "%s: unexpected offloaded packet, cmd %u\n",
			       adapter->name, *skb->data);
			dev_kfree_skb_any(skb);
		}
	} else {
		struct cpl_rx_pkt *p = (struct cpl_rx_pkt *)skb->data;

		skb_pull(skb, sizeof(*p));
		skb->dev = adapter->port[p->iff].dev;
		skb->dev->last_rx = jiffies;
		skb->protocol = sge_eth_type_trans(skb, skb->dev);
		if ((adapter->flags & RX_CSUM_ENABLED) && p->csum == 0xffff &&
		    skb->protocol == htons(ETH_P_IP) &&
		    (skb->data[9] == IPPROTO_TCP ||
		     skb->data[9] == IPPROTO_UDP))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
		if (adapter->vlan_grp && p->vlan_valid)
			vlan_hwaccel_rx(skb, adapter->vlan_grp,
					ntohs(p->vlan));
		else
			netif_rx(skb);
	}

	if (++Q->cidx == Q->entries_n)
		Q->cidx = 0;

	if (unlikely(--Q->credits < Q->entries_n - SGE_FREEL_REFILL_THRESH))
		refill_free_list(sge, Q);
	return 1;
}


/*
 * Adaptive interrupt timer logic to keep the CPU utilization to
 * manageable levels. Basically, as the Average Packet Size (APS)
 * gets higher, the interrupt latency setting gets longer. Every
 * SGE_INTR_BUCKETSIZE (of 100B) causes a bump of 2usec to the
 * base value of SGE_INTRTIMER0. At large values of payload the
 * latency hits the ceiling value of SGE_INTRTIMER1 stored at
 * index SGE_INTR_MAXBUCKETS-1 in sge->intrtimer[].
 *
 * sge->currIndex caches the last index to save unneeded PIOs.
 */
static inline void update_intr_timer(struct sge *sge, unsigned int avg_payload)
{
	unsigned int newIndex;

	newIndex = avg_payload / SGE_INTR_BUCKETSIZE;
	if (newIndex > SGE_INTR_MAXBUCKETS - 1) {
		newIndex = SGE_INTR_MAXBUCKETS - 1;
	}
	/* Save a PIO with this check....maybe */
	if (newIndex != sge->currIndex) {
		t1_write_reg_4(sge->adapter, A_SG_INTRTIMER,
			       sge->intrtimer[newIndex]);
		sge->currIndex = newIndex;
		sge->adapter->params.sge.last_rx_coalesce_raw =
			sge->intrtimer[newIndex];
	}
}

/*
 * Returns true if command queue q_num has enough available descriptors that
 * we can resume Tx operation after temporarily disabling its packet queue.
 */
static inline int enough_free_Tx_descs(struct sge *sge, int q_num)
{
	return atomic_read(&sge->cmdQ[q_num].credits) >
		(sge->cmdQ[q_num].entries_n >> 2);
}

/*
 * Main interrupt handler, optimized assuming that we took a 'DATA'
 * interrupt.
 *
 * 1. Clear the interrupt
 * 2. Loop while we find valid descriptors and process them; accumulate
 *	information that can be processed after the loop
 * 3. Tell the SGE at which index we stopped processing descriptors
 * 4. Bookkeeping; free TX buffers, ring doorbell if there are any
 *	outstanding TX buffers waiting, replenish RX buffers, potentially
 *	reenable upper layers if they were turned off due to lack of TX
 *	resources which are available again.
 * 5. If we took an interrupt, but no valid respQ descriptors was found we
 *	let the slow_intr_handler run and do error handling.
 */
irqreturn_t t1_interrupt(int irq, void *cookie, struct pt_regs *regs)
{
	struct net_device *netdev;
	struct adapter *adapter = cookie;
	struct sge *sge = adapter->sge;
	struct respQ *Q = &sge->respQ;
	unsigned int credits = Q->credits, flags = 0, ret = 0;
	unsigned int tot_rxpayload = 0, tot_txpayload = 0, n_rx = 0, n_tx = 0;
	unsigned int credits_pend[SGE_CMDQ_N] = { 0, 0 };

	struct respQ_e *e = &Q->entries[Q->cidx];
	prefetch(e);

	t1_write_reg_4(adapter, A_PL_CAUSE, F_PL_INTR_SGE_DATA);


	while (e->GenerationBit == Q->genbit) {
		if (--credits < SGE_RESPQ_REPLENISH_THRES) {
			u32 n = Q->entries_n - credits - 1;

			t1_write_reg_4(adapter, A_SG_RSPQUEUECREDIT, n);
			credits += n;
		}
		if (likely(e->DataValid)) {
			if (!e->Sop || !e->Eop)
				BUG();
			t1_sge_rx(sge, &sge->freelQ[e->FreelistQid],
				  e->BufferLength, e->Offload);
			tot_rxpayload += e->BufferLength;
			++n_rx;
		}
		flags |= e->Qsleeping;
		credits_pend[0] += e->Cmdq0CreditReturn;
		credits_pend[1] += e->Cmdq1CreditReturn;

#ifdef CONFIG_SMP
		/*
		 * If enough cmdQ0 buffers have finished DMAing free them so
		 * anyone that may be waiting for their release can continue.
		 * We do this only on MP systems to allow other CPUs to proceed
		 * promptly.  UP systems can wait for the free_cmdQ_buffers()
		 * calls after this loop as the sole CPU is currently busy in
		 * this loop.
		 */
		if (unlikely(credits_pend[0] > SGE_FREEL_REFILL_THRESH)) {
			free_cmdQ_buffers(sge, &sge->cmdQ[0], credits_pend[0],
					  &tot_txpayload);
			n_tx += credits_pend[0];
			credits_pend[0] = 0;
		}
#endif
		ret++;
		e++;
		if (unlikely(++Q->cidx == Q->entries_n)) {
			Q->cidx = 0;
			Q->genbit ^= 1;
			e = Q->entries;
		}
	}

	Q->credits = credits;
	t1_write_reg_4(adapter, A_SG_SLEEPING, Q->cidx);

	if (credits_pend[0])
		free_cmdQ_buffers(sge, &sge->cmdQ[0], credits_pend[0], &tot_txpayload);
	if (credits_pend[1])
		free_cmdQ_buffers(sge, &sge->cmdQ[1], credits_pend[1], &tot_txpayload);

	/* Do any coalescing and interrupt latency timer adjustments */
	if (adapter->params.sge.coalesce_enable) {
		unsigned int	avg_txpayload = 0, avg_rxpayload = 0;

		n_tx += credits_pend[0] + credits_pend[1];

		/*
		 * Choose larger avg. payload size to increase
		 * throughput and reduce [CPU util., intr/s.]
		 *
		 * Throughput behavior favored in mixed-mode.
		 */
		if (n_tx)
			avg_txpayload = tot_txpayload/n_tx;
		if (n_rx)
			avg_rxpayload = tot_rxpayload/n_rx;

		if (n_tx && avg_txpayload > avg_rxpayload){
			update_intr_timer(sge, avg_txpayload);
		} else if (n_rx) {
			update_intr_timer(sge, avg_rxpayload);
		}
	}

	if (flags & F_CMDQ0_ENABLE) {
		struct cmdQ *cmdQ = &sge->cmdQ[0];

		atomic_set(&cmdQ->asleep, 1);
		if (atomic_read(&cmdQ->pio_pidx) != cmdQ->pidx) {
			doorbell_pio(sge, F_CMDQ0_ENABLE);
			atomic_set(&cmdQ->pio_pidx, cmdQ->pidx);
		}
	}
	if (unlikely(flags & (F_FL0_ENABLE | F_FL1_ENABLE)))
		freelQs_empty(sge);

	netdev = adapter->port[0].dev;
	if (unlikely(netif_queue_stopped(netdev) && netif_carrier_ok(netdev) &&
		     enough_free_Tx_descs(sge, 0) &&
		     enough_free_Tx_descs(sge, 1))) {
		netif_wake_queue(netdev);
	}
	if (unlikely(!ret))
		ret = t1_slow_intr_handler(adapter);

	return IRQ_RETVAL(ret != 0);
}

/*
 * Enqueues the sk_buff onto the cmdQ[qid] and has hardware fetch it.
 *
 * The code figures out how many entries the sk_buff will require in the
 * cmdQ and updates the cmdQ data structure with the state once the enqueue
 * has complete. Then, it doesn't access the global structure anymore, but
 * uses the corresponding fields on the stack. In conjuction with a spinlock
 * around that code, we can make the function reentrant without holding the
 * lock when we actually enqueue (which might be expensive, especially on
 * architectures with IO MMUs).
 */
static unsigned int t1_sge_tx(struct sk_buff *skb, struct adapter *adapter,
		       unsigned int qid)
{
	struct sge *sge = adapter->sge;
	struct cmdQ *Q = &sge->cmdQ[qid];
	struct cmdQ_e *e;
	struct cmdQ_ce *ce;
	dma_addr_t mapping;
	unsigned int credits, pidx, genbit;

	unsigned int count = 1 + skb_shinfo(skb)->nr_frags;

	/*
	 * Coming from the timer
	 */
	if ((skb == sge->pskb)) {
		/*
		 * Quit if any cmdQ activities
		 */
		if (!spin_trylock(&Q->Qlock))
			return 0;
		if (atomic_read(&Q->credits) != Q->entries_n) {
			spin_unlock(&Q->Qlock);
			return 0;
		}
	}
	else
		spin_lock(&Q->Qlock);

	genbit = Q->genbit;
	pidx = Q->pidx;
	credits = atomic_read(&Q->credits);

	credits -= count;
	atomic_sub(count, &Q->credits);
	Q->pidx += count;
	if (Q->pidx >= Q->entries_n) {
		Q->pidx -= Q->entries_n;
		Q->genbit ^= 1;
	}

	if (unlikely(credits < (MAX_SKB_FRAGS + 1))) {
		sge->intr_cnt.cmdQ_full[qid]++;
		netif_stop_queue(adapter->port[0].dev);
	}
	spin_unlock(&Q->Qlock);

	mapping = pci_map_single(adapter->pdev, skb->data,
				 skb->len - skb->data_len, PCI_DMA_TODEVICE);
	ce = &Q->centries[pidx];
	ce->skb = NULL;
	pci_unmap_addr_set(ce, dma_addr, mapping);
	pci_unmap_len_set(ce, dma_len, skb->len - skb->data_len);
	ce->single = 1;

	e = &Q->entries[pidx];
	e->Sop =  1;
	e->DataValid = 1;
	e->BufferLength = skb->len - skb->data_len;
	e->AddrHigh = (u64)mapping >> 32;
	e->AddrLow = (u32)mapping;

	if (--count > 0) {
		unsigned int i;

		e->Eop = 0;
		wmb();
		e->GenerationBit = e->GenerationBit2 = genbit;

		for (i = 0; i < count; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			ce++; e++;
			if (++pidx == Q->entries_n) {
				pidx = 0;
				genbit ^= 1;
				ce = Q->centries;
				e = Q->entries;
			}

			mapping = pci_map_page(adapter->pdev, frag->page,
					       frag->page_offset,
					       frag->size,
					       PCI_DMA_TODEVICE);
			ce->skb = NULL;
			pci_unmap_addr_set(ce, dma_addr, mapping);
			pci_unmap_len_set(ce, dma_len, frag->size);
			ce->single = 0;

			e->Sop = 0;
			e->DataValid = 1;
			e->BufferLength = frag->size;
			e->AddrHigh = (u64)mapping >> 32;
			e->AddrLow = (u32)mapping;

			if (i < count - 1) {
				e->Eop = 0;
				wmb();
				e->GenerationBit = e->GenerationBit2 = genbit;
			}
		}
	}

	if (skb != sge->pskb)
		ce->skb = skb;
	e->Eop = 1;
	wmb();
	e->GenerationBit = e->GenerationBit2 = genbit;

	/*
	 * We always ring the doorbell for cmdQ1.  For cmdQ0, we only ring
	 * the doorbell if the Q is asleep. There is a natural race, where
	 * the hardware is going to sleep just after we checked, however,
	 * then the interrupt handler will detect the outstanding TX packet
	 * and ring the doorbell for us.
	 */
	if (qid) {
		doorbell_pio(sge, F_CMDQ1_ENABLE);
	} else if (atomic_read(&Q->asleep)) {
		atomic_set(&Q->asleep, 0);
		doorbell_pio(sge, F_CMDQ0_ENABLE);
		atomic_set(&Q->pio_pidx, Q->pidx);
	}
	return 0;
}

#define MK_ETH_TYPE_MSS(type, mss) (((mss) & 0x3FFF) | ((type) << 14))

/*
 * Adds the CPL header to the sk_buff and passes it to t1_sge_tx.
 */
int t1_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct adapter *adapter = dev->priv;
	struct cpl_tx_pkt *cpl;
	struct ethhdr *eth;
	size_t max_len;

	/*
	 * We are using a non-standard hard_header_len and some kernel
	 * components, such as pktgen, do not handle it right.  Complain
	 * when this happens but try to fix things up.
	 */
	if (unlikely(skb_headroom(skb) < dev->hard_header_len - ETH_HLEN)) {
		struct sk_buff *orig_skb = skb;

		if (net_ratelimit())
			printk(KERN_ERR
			       "%s: Tx packet has inadequate headroom\n",
			       dev->name);
		skb = skb_realloc_headroom(skb, sizeof(struct cpl_tx_pkt_lso));
		dev_kfree_skb_any(orig_skb);
		if (!skb)
			return -ENOMEM;
	}

	if (skb_shinfo(skb)->tso_size) {
		int eth_type;
		struct cpl_tx_pkt_lso *hdr;

		eth_type = skb->nh.raw - skb->data == ETH_HLEN ?
			CPL_ETH_II : CPL_ETH_II_VLAN;

		hdr = (struct cpl_tx_pkt_lso *)skb_push(skb, sizeof(*hdr));
		hdr->opcode = CPL_TX_PKT_LSO;
		hdr->ip_csum_dis = hdr->l4_csum_dis = 0;
		hdr->ip_hdr_words = skb->nh.iph->ihl;
		hdr->tcp_hdr_words = skb->h.th->doff;
		hdr->eth_type_mss = htons(MK_ETH_TYPE_MSS(eth_type,
						skb_shinfo(skb)->tso_size));
		hdr->len = htonl(skb->len - sizeof(*hdr));
		cpl = (struct cpl_tx_pkt *)hdr;
	} else
	{
		/*
		 * An Ethernet packet must have at least space for
		 * the DIX Ethernet header and be no greater than
		 * the device set MTU. Otherwise trash the packet.
		 */
		if (skb->len < ETH_HLEN)
			goto t1_start_xmit_fail2;
		eth = (struct ethhdr *)skb->data;
		if (eth->h_proto == htons(ETH_P_8021Q))
			max_len = dev->mtu + VLAN_ETH_HLEN;
		else
			max_len = dev->mtu + ETH_HLEN;
		if (skb->len > max_len)
			goto t1_start_xmit_fail2;

		if (!(adapter->flags & UDP_CSUM_CAPABLE) &&
		    skb->ip_summed == CHECKSUM_HW &&
		    skb->nh.iph->protocol == IPPROTO_UDP &&
		    skb_checksum_help(skb, 0))
			goto t1_start_xmit_fail3;


		if (!adapter->sge->pskb) {
			if (skb->protocol == htons(ETH_P_ARP) &&
			    skb->nh.arph->ar_op == htons(ARPOP_REQUEST))
				adapter->sge->pskb = skb;
		}
		cpl = (struct cpl_tx_pkt *)skb_push(skb, sizeof(*cpl));
		cpl->opcode = CPL_TX_PKT;
		cpl->ip_csum_dis = 1;    /* SW calculates IP csum */
		cpl->l4_csum_dis = skb->ip_summed == CHECKSUM_HW ? 0 : 1;
		/* the length field isn't used so don't bother setting it */
	}
	cpl->iff = dev->if_port;

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	if (adapter->vlan_grp && vlan_tx_tag_present(skb)) {
		cpl->vlan_valid = 1;
		cpl->vlan = htons(vlan_tx_tag_get(skb));
	} else
#endif
		cpl->vlan_valid = 0;

	dev->trans_start = jiffies;
	return t1_sge_tx(skb, adapter, 0);

t1_start_xmit_fail3:
	printk(KERN_INFO "%s: Unable to complete checksum\n", dev->name);
	goto t1_start_xmit_fail1;

t1_start_xmit_fail2:
	printk(KERN_INFO "%s: Invalid packet length %d, dropping\n",
			dev->name, skb->len);

t1_start_xmit_fail1:
	dev_kfree_skb_any(skb);
	return 0;
}

void t1_sge_set_ptimeout(adapter_t *adapter, u32 val)
{
	struct sge *sge = adapter->sge;

	if (is_T2(adapter))
		sge->ptimeout = max((u32)((HZ * val) / 1000), (u32)1);
}

u32 t1_sge_get_ptimeout(adapter_t *adapter)
{
	struct sge *sge = adapter->sge;

	return (is_T2(adapter) ? ((sge->ptimeout * 1000) / HZ) : 0);
}

