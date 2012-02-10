/*****************************************************************************
 *                                                                           *
 * File: sge.c                                                               *
 * $Revision: 1.26 $                                                         *
 * $Date: 2005/06/21 18:29:48 $                                              *
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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/ktime.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/prefetch.h>

#include "cpl5_cmd.h"
#include "sge.h"
#include "regs.h"
#include "espi.h"

/* This belongs in if_ether.h */
#define ETH_P_CPL5 0xf

#define SGE_CMDQ_N		2
#define SGE_FREELQ_N		2
#define SGE_CMDQ0_E_N		1024
#define SGE_CMDQ1_E_N		128
#define SGE_FREEL_SIZE		4096
#define SGE_JUMBO_FREEL_SIZE	512
#define SGE_FREEL_REFILL_THRESH	16
#define SGE_RESPQ_E_N		1024
#define SGE_INTRTIMER_NRES	1000
#define SGE_RX_SM_BUF_SIZE	1536
#define SGE_TX_DESC_MAX_PLEN	16384

#define SGE_RESPQ_REPLENISH_THRES (SGE_RESPQ_E_N / 4)

/*
 * Period of the TX buffer reclaim timer.  This timer does not need to run
 * frequently as TX buffers are usually reclaimed by new TX packets.
 */
#define TX_RECLAIM_PERIOD (HZ / 4)

#define M_CMD_LEN       0x7fffffff
#define V_CMD_LEN(v)    (v)
#define G_CMD_LEN(v)    ((v) & M_CMD_LEN)
#define V_CMD_GEN1(v)   ((v) << 31)
#define V_CMD_GEN2(v)   (v)
#define F_CMD_DATAVALID (1 << 1)
#define F_CMD_SOP       (1 << 2)
#define V_CMD_EOP(v)    ((v) << 3)

/*
 * Command queue, receive buffer list, and response queue descriptors.
 */
#if defined(__BIG_ENDIAN_BITFIELD)
struct cmdQ_e {
	u32 addr_lo;
	u32 len_gen;
	u32 flags;
	u32 addr_hi;
};

struct freelQ_e {
	u32 addr_lo;
	u32 len_gen;
	u32 gen2;
	u32 addr_hi;
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
	u32 len_gen;
	u32 addr_lo;
	u32 addr_hi;
	u32 flags;
};

struct freelQ_e {
	u32 len_gen;
	u32 addr_lo;
	u32 addr_hi;
	u32 gen2;
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
	DEFINE_DMA_UNMAP_ADDR(dma_addr);
	DEFINE_DMA_UNMAP_LEN(dma_len);
};

struct freelQ_ce {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(dma_addr);
	DEFINE_DMA_UNMAP_LEN(dma_len);
};

/*
 * SW command, freelist and response rings
 */
struct cmdQ {
	unsigned long   status;         /* HW DMA fetch status */
	unsigned int    in_use;         /* # of in-use command descriptors */
	unsigned int	size;	        /* # of descriptors */
	unsigned int    processed;      /* total # of descs HW has processed */
	unsigned int    cleaned;        /* total # of descs SW has reclaimed */
	unsigned int    stop_thres;     /* SW TX queue suspend threshold */
	u16		pidx;           /* producer index (SW) */
	u16		cidx;           /* consumer index (HW) */
	u8		genbit;         /* current generation (=valid) bit */
	u8              sop;            /* is next entry start of packet? */
	struct cmdQ_e  *entries;        /* HW command descriptor Q */
	struct cmdQ_ce *centries;       /* SW command context descriptor Q */
	dma_addr_t	dma_addr;       /* DMA addr HW command descriptor Q */
	spinlock_t	lock;           /* Lock to protect cmdQ enqueuing */
};

struct freelQ {
	unsigned int	credits;        /* # of available RX buffers */
	unsigned int	size;	        /* free list capacity */
	u16		pidx;           /* producer index (SW) */
	u16		cidx;           /* consumer index (HW) */
	u16		rx_buffer_size; /* Buffer size on this free list */
	u16             dma_offset;     /* DMA offset to align IP headers */
	u16             recycleq_idx;   /* skb recycle q to use */
	u8		genbit;	        /* current generation (=valid) bit */
	struct freelQ_e	*entries;       /* HW freelist descriptor Q */
	struct freelQ_ce *centries;     /* SW freelist context descriptor Q */
	dma_addr_t	dma_addr;       /* DMA addr HW freelist descriptor Q */
};

struct respQ {
	unsigned int	credits;        /* credits to be returned to SGE */
	unsigned int	size;	        /* # of response Q descriptors */
	u16		cidx;	        /* consumer index (SW) */
	u8		genbit;	        /* current generation(=valid) bit */
	struct respQ_e *entries;        /* HW response descriptor Q */
	dma_addr_t	dma_addr;       /* DMA addr HW response descriptor Q */
};

/* Bit flags for cmdQ.status */
enum {
	CMDQ_STAT_RUNNING = 1,          /* fetch engine is running */
	CMDQ_STAT_LAST_PKT_DB = 2       /* last packet rung the doorbell */
};

/* T204 TX SW scheduler */

/* Per T204 TX port */
struct sched_port {
	unsigned int	avail;		/* available bits - quota */
	unsigned int	drain_bits_per_1024ns; /* drain rate */
	unsigned int	speed;		/* drain rate, mbps */
	unsigned int	mtu;		/* mtu size */
	struct sk_buff_head skbq;	/* pending skbs */
};

/* Per T204 device */
struct sched {
	ktime_t         last_updated;   /* last time quotas were computed */
	unsigned int	max_avail;	/* max bits to be sent to any port */
	unsigned int	port;		/* port index (round robin ports) */
	unsigned int	num;		/* num skbs in per port queues */
	struct sched_port p[MAX_NPORTS];
	struct tasklet_struct sched_tsk;/* tasklet used to run scheduler */
};
static void restart_sched(unsigned long);


/*
 * Main SGE data structure
 *
 * Interrupts are handled by a single CPU and it is likely that on a MP system
 * the application is migrated to another CPU. In that scenario, we try to
 * separate the RX(in irq context) and TX state in order to decrease memory
 * contention.
 */
struct sge {
	struct adapter *adapter;	/* adapter backpointer */
	struct net_device *netdev;      /* netdevice backpointer */
	struct freelQ	freelQ[SGE_FREELQ_N]; /* buffer free lists */
	struct respQ	respQ;		/* response Q */
	unsigned long   stopped_tx_queues; /* bitmap of suspended Tx queues */
	unsigned int	rx_pkt_pad;     /* RX padding for L2 packets */
	unsigned int	jumbo_fl;       /* jumbo freelist Q index */
	unsigned int	intrtimer_nres;	/* no-resource interrupt timer */
	unsigned int    fixed_intrtimer;/* non-adaptive interrupt timer */
	struct timer_list tx_reclaim_timer; /* reclaims TX buffers */
	struct timer_list espibug_timer;
	unsigned long	espibug_timeout;
	struct sk_buff	*espibug_skb[MAX_NPORTS];
	u32		sge_control;	/* shadow value of sge control reg */
	struct sge_intr_counts stats;
	struct sge_port_stats __percpu *port_stats[MAX_NPORTS];
	struct sched	*tx_sched;
	struct cmdQ cmdQ[SGE_CMDQ_N] ____cacheline_aligned_in_smp;
};

static const u8 ch_mac_addr[ETH_ALEN] = {
	0x0, 0x7, 0x43, 0x0, 0x0, 0x0
};

/*
 * stop tasklet and free all pending skb's
 */
static void tx_sched_stop(struct sge *sge)
{
	struct sched *s = sge->tx_sched;
	int i;

	tasklet_kill(&s->sched_tsk);

	for (i = 0; i < MAX_NPORTS; i++)
		__skb_queue_purge(&s->p[s->port].skbq);
}

/*
 * t1_sched_update_parms() is called when the MTU or link speed changes. It
 * re-computes scheduler parameters to scope with the change.
 */
unsigned int t1_sched_update_parms(struct sge *sge, unsigned int port,
				   unsigned int mtu, unsigned int speed)
{
	struct sched *s = sge->tx_sched;
	struct sched_port *p = &s->p[port];
	unsigned int max_avail_segs;

	pr_debug("t1_sched_update_params mtu=%d speed=%d\n", mtu, speed);
	if (speed)
		p->speed = speed;
	if (mtu)
		p->mtu = mtu;

	if (speed || mtu) {
		unsigned long long drain = 1024ULL * p->speed * (p->mtu - 40);
		do_div(drain, (p->mtu + 50) * 1000);
		p->drain_bits_per_1024ns = (unsigned int) drain;

		if (p->speed < 1000)
			p->drain_bits_per_1024ns =
				90 * p->drain_bits_per_1024ns / 100;
	}

	if (board_info(sge->adapter)->board == CHBT_BOARD_CHT204) {
		p->drain_bits_per_1024ns -= 16;
		s->max_avail = max(4096U, p->mtu + 16 + 14 + 4);
		max_avail_segs = max(1U, 4096 / (p->mtu - 40));
	} else {
		s->max_avail = 16384;
		max_avail_segs = max(1U, 9000 / (p->mtu - 40));
	}

	pr_debug("t1_sched_update_parms: mtu %u speed %u max_avail %u "
		 "max_avail_segs %u drain_bits_per_1024ns %u\n", p->mtu,
		 p->speed, s->max_avail, max_avail_segs,
		 p->drain_bits_per_1024ns);

	return max_avail_segs * (p->mtu - 40);
}

#if 0

/*
 * t1_sched_max_avail_bytes() tells the scheduler the maximum amount of
 * data that can be pushed per port.
 */
void t1_sched_set_max_avail_bytes(struct sge *sge, unsigned int val)
{
	struct sched *s = sge->tx_sched;
	unsigned int i;

	s->max_avail = val;
	for (i = 0; i < MAX_NPORTS; i++)
		t1_sched_update_parms(sge, i, 0, 0);
}

/*
 * t1_sched_set_drain_bits_per_us() tells the scheduler at which rate a port
 * is draining.
 */
void t1_sched_set_drain_bits_per_us(struct sge *sge, unsigned int port,
					 unsigned int val)
{
	struct sched *s = sge->tx_sched;
	struct sched_port *p = &s->p[port];
	p->drain_bits_per_1024ns = val * 1024 / 1000;
	t1_sched_update_parms(sge, port, 0, 0);
}

#endif  /*  0  */


/*
 * get_clock() implements a ns clock (see ktime_get)
 */
static inline ktime_t get_clock(void)
{
	struct timespec ts;

	ktime_get_ts(&ts);
	return timespec_to_ktime(ts);
}

/*
 * tx_sched_init() allocates resources and does basic initialization.
 */
static int tx_sched_init(struct sge *sge)
{
	struct sched *s;
	int i;

	s = kzalloc(sizeof (struct sched), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	pr_debug("tx_sched_init\n");
	tasklet_init(&s->sched_tsk, restart_sched, (unsigned long) sge);
	sge->tx_sched = s;

	for (i = 0; i < MAX_NPORTS; i++) {
		skb_queue_head_init(&s->p[i].skbq);
		t1_sched_update_parms(sge, i, 1500, 1000);
	}

	return 0;
}

/*
 * sched_update_avail() computes the delta since the last time it was called
 * and updates the per port quota (number of bits that can be sent to the any
 * port).
 */
static inline int sched_update_avail(struct sge *sge)
{
	struct sched *s = sge->tx_sched;
	ktime_t now = get_clock();
	unsigned int i;
	long long delta_time_ns;

	delta_time_ns = ktime_to_ns(ktime_sub(now, s->last_updated));

	pr_debug("sched_update_avail delta=%lld\n", delta_time_ns);
	if (delta_time_ns < 15000)
		return 0;

	for (i = 0; i < MAX_NPORTS; i++) {
		struct sched_port *p = &s->p[i];
		unsigned int delta_avail;

		delta_avail = (p->drain_bits_per_1024ns * delta_time_ns) >> 13;
		p->avail = min(p->avail + delta_avail, s->max_avail);
	}

	s->last_updated = now;

	return 1;
}

/*
 * sched_skb() is called from two different places. In the tx path, any
 * packet generating load on an output port will call sched_skb()
 * (skb != NULL). In addition, sched_skb() is called from the irq/soft irq
 * context (skb == NULL).
 * The scheduler only returns a skb (which will then be sent) if the
 * length of the skb is <= the current quota of the output port.
 */
static struct sk_buff *sched_skb(struct sge *sge, struct sk_buff *skb,
				unsigned int credits)
{
	struct sched *s = sge->tx_sched;
	struct sk_buff_head *skbq;
	unsigned int i, len, update = 1;

	pr_debug("sched_skb %p\n", skb);
	if (!skb) {
		if (!s->num)
			return NULL;
	} else {
		skbq = &s->p[skb->dev->if_port].skbq;
		__skb_queue_tail(skbq, skb);
		s->num++;
		skb = NULL;
	}

	if (credits < MAX_SKB_FRAGS + 1)
		goto out;

again:
	for (i = 0; i < MAX_NPORTS; i++) {
		s->port = (s->port + 1) & (MAX_NPORTS - 1);
		skbq = &s->p[s->port].skbq;

		skb = skb_peek(skbq);

		if (!skb)
			continue;

		len = skb->len;
		if (len <= s->p[s->port].avail) {
			s->p[s->port].avail -= len;
			s->num--;
			__skb_unlink(skb, skbq);
			goto out;
		}
		skb = NULL;
	}

	if (update-- && sched_update_avail(sge))
		goto again;

out:
	/* If there are more pending skbs, we use the hardware to schedule us
	 * again.
	 */
	if (s->num && !skb) {
		struct cmdQ *q = &sge->cmdQ[0];
		clear_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
		if (test_and_set_bit(CMDQ_STAT_RUNNING, &q->status) == 0) {
			set_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
			writel(F_CMDQ0_ENABLE, sge->adapter->regs + A_SG_DOORBELL);
		}
	}
	pr_debug("sched_skb ret %p\n", skb);

	return skb;
}

/*
 * PIO to indicate that memory mapped Q contains valid descriptor(s).
 */
static inline void doorbell_pio(struct adapter *adapter, u32 val)
{
	wmb();
	writel(val, adapter->regs + A_SG_DOORBELL);
}

/*
 * Frees all RX buffers on the freelist Q. The caller must make sure that
 * the SGE is turned off before calling this function.
 */
static void free_freelQ_buffers(struct pci_dev *pdev, struct freelQ *q)
{
	unsigned int cidx = q->cidx;

	while (q->credits--) {
		struct freelQ_ce *ce = &q->centries[cidx];

		pci_unmap_single(pdev, dma_unmap_addr(ce, dma_addr),
				 dma_unmap_len(ce, dma_len),
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb(ce->skb);
		ce->skb = NULL;
		if (++cidx == q->size)
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
		size = sizeof(struct respQ_e) * sge->respQ.size;
		pci_free_consistent(pdev, size, sge->respQ.entries,
				    sge->respQ.dma_addr);
	}

	for (i = 0; i < SGE_FREELQ_N; i++) {
		struct freelQ *q = &sge->freelQ[i];

		if (q->centries) {
			free_freelQ_buffers(pdev, q);
			kfree(q->centries);
		}
		if (q->entries) {
			size = sizeof(struct freelQ_e) * q->size;
			pci_free_consistent(pdev, size, q->entries,
					    q->dma_addr);
		}
	}
}

/*
 * Allocates basic RX resources, consisting of memory mapped freelist Qs and a
 * response queue.
 */
static int alloc_rx_resources(struct sge *sge, struct sge_params *p)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int size, i;

	for (i = 0; i < SGE_FREELQ_N; i++) {
		struct freelQ *q = &sge->freelQ[i];

		q->genbit = 1;
		q->size = p->freelQ_size[i];
		q->dma_offset = sge->rx_pkt_pad ? 0 : NET_IP_ALIGN;
		size = sizeof(struct freelQ_e) * q->size;
		q->entries = pci_alloc_consistent(pdev, size, &q->dma_addr);
		if (!q->entries)
			goto err_no_mem;

		size = sizeof(struct freelQ_ce) * q->size;
		q->centries = kzalloc(size, GFP_KERNEL);
		if (!q->centries)
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

		size = (16 * 1024) -
		    SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	sge->freelQ[sge->jumbo_fl].rx_buffer_size = size;

	/*
	 * Setup which skb recycle Q should be used when recycling buffers from
	 * each free list.
	 */
	sge->freelQ[!sge->jumbo_fl].recycleq_idx = 0;
	sge->freelQ[sge->jumbo_fl].recycleq_idx = 1;

	sge->respQ.genbit = 1;
	sge->respQ.size = SGE_RESPQ_E_N;
	sge->respQ.credits = 0;
	size = sizeof(struct respQ_e) * sge->respQ.size;
	sge->respQ.entries =
		pci_alloc_consistent(pdev, size, &sge->respQ.dma_addr);
	if (!sge->respQ.entries)
		goto err_no_mem;
	return 0;

err_no_mem:
	free_rx_resources(sge);
	return -ENOMEM;
}

/*
 * Reclaims n TX descriptors and frees the buffers associated with them.
 */
static void free_cmdQ_buffers(struct sge *sge, struct cmdQ *q, unsigned int n)
{
	struct cmdQ_ce *ce;
	struct pci_dev *pdev = sge->adapter->pdev;
	unsigned int cidx = q->cidx;

	q->in_use -= n;
	ce = &q->centries[cidx];
	while (n--) {
		if (likely(dma_unmap_len(ce, dma_len))) {
			pci_unmap_single(pdev, dma_unmap_addr(ce, dma_addr),
					 dma_unmap_len(ce, dma_len),
					 PCI_DMA_TODEVICE);
			if (q->sop)
				q->sop = 0;
		}
		if (ce->skb) {
			dev_kfree_skb_any(ce->skb);
			q->sop = 1;
		}
		ce++;
		if (++cidx == q->size) {
			cidx = 0;
			ce = q->centries;
		}
	}
	q->cidx = cidx;
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
		struct cmdQ *q = &sge->cmdQ[i];

		if (q->centries) {
			if (q->in_use)
				free_cmdQ_buffers(sge, q, q->in_use);
			kfree(q->centries);
		}
		if (q->entries) {
			size = sizeof(struct cmdQ_e) * q->size;
			pci_free_consistent(pdev, size, q->entries,
					    q->dma_addr);
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
		struct cmdQ *q = &sge->cmdQ[i];

		q->genbit = 1;
		q->sop = 1;
		q->size = p->cmdQ_size[i];
		q->in_use = 0;
		q->status = 0;
		q->processed = q->cleaned = 0;
		q->stop_thres = 0;
		spin_lock_init(&q->lock);
		size = sizeof(struct cmdQ_e) * q->size;
		q->entries = pci_alloc_consistent(pdev, size, &q->dma_addr);
		if (!q->entries)
			goto err_no_mem;

		size = sizeof(struct cmdQ_ce) * q->size;
		q->centries = kzalloc(size, GFP_KERNEL);
		if (!q->centries)
			goto err_no_mem;
	}

	/*
	 * CommandQ 0 handles Ethernet and TOE packets, while queue 1 is TOE
	 * only.  For queue 0 set the stop threshold so we can handle one more
	 * packet from each port, plus reserve an additional 24 entries for
	 * Ethernet packets only.  Queue 1 never suspends nor do we reserve
	 * space for Ethernet packets.
	 */
	sge->cmdQ[0].stop_thres = sge->adapter->params.nports *
		(MAX_SKB_FRAGS + 1);
	return 0;

err_no_mem:
	free_tx_resources(sge);
	return -ENOMEM;
}

static inline void setup_ring_params(struct adapter *adapter, u64 addr,
				     u32 size, int base_reg_lo,
				     int base_reg_hi, int size_reg)
{
	writel((u32)addr, adapter->regs + base_reg_lo);
	writel(addr >> 32, adapter->regs + base_reg_hi);
	writel(size, adapter->regs + size_reg);
}

/*
 * Enable/disable VLAN acceleration.
 */
void t1_vlan_mode(struct adapter *adapter, netdev_features_t features)
{
	struct sge *sge = adapter->sge;

	if (features & NETIF_F_HW_VLAN_RX)
		sge->sge_control |= F_VLAN_XTRACT;
	else
		sge->sge_control &= ~F_VLAN_XTRACT;
	if (adapter->open_device_map) {
		writel(sge->sge_control, adapter->regs + A_SG_CONTROL);
		readl(adapter->regs + A_SG_CONTROL);   /* flush */
	}
}

/*
 * Programs the various SGE registers. However, the engine is not yet enabled,
 * but sge->sge_control is setup and ready to go.
 */
static void configure_sge(struct sge *sge, struct sge_params *p)
{
	struct adapter *ap = sge->adapter;

	writel(0, ap->regs + A_SG_CONTROL);
	setup_ring_params(ap, sge->cmdQ[0].dma_addr, sge->cmdQ[0].size,
			  A_SG_CMD0BASELWR, A_SG_CMD0BASEUPR, A_SG_CMD0SIZE);
	setup_ring_params(ap, sge->cmdQ[1].dma_addr, sge->cmdQ[1].size,
			  A_SG_CMD1BASELWR, A_SG_CMD1BASEUPR, A_SG_CMD1SIZE);
	setup_ring_params(ap, sge->freelQ[0].dma_addr,
			  sge->freelQ[0].size, A_SG_FL0BASELWR,
			  A_SG_FL0BASEUPR, A_SG_FL0SIZE);
	setup_ring_params(ap, sge->freelQ[1].dma_addr,
			  sge->freelQ[1].size, A_SG_FL1BASELWR,
			  A_SG_FL1BASEUPR, A_SG_FL1SIZE);

	/* The threshold comparison uses <. */
	writel(SGE_RX_SM_BUF_SIZE + 1, ap->regs + A_SG_FLTHRESHOLD);

	setup_ring_params(ap, sge->respQ.dma_addr, sge->respQ.size,
			  A_SG_RSPBASELWR, A_SG_RSPBASEUPR, A_SG_RSPSIZE);
	writel((u32)sge->respQ.size - 1, ap->regs + A_SG_RSPQUEUECREDIT);

	sge->sge_control = F_CMDQ0_ENABLE | F_CMDQ1_ENABLE | F_FL0_ENABLE |
		F_FL1_ENABLE | F_CPL_ENABLE | F_RESPONSE_QUEUE_ENABLE |
		V_CMDQ_PRIORITY(2) | F_DISABLE_CMDQ1_GTS | F_ISCSI_COALESCE |
		V_RX_PKT_OFFSET(sge->rx_pkt_pad);

#if defined(__BIG_ENDIAN_BITFIELD)
	sge->sge_control |= F_ENABLE_BIG_ENDIAN;
#endif

	/* Initialize no-resource timer */
	sge->intrtimer_nres = SGE_INTRTIMER_NRES * core_ticks_per_usec(ap);

	t1_sge_set_coalesce_params(sge, p);
}

/*
 * Return the payload capacity of the jumbo free-list buffers.
 */
static inline unsigned int jumbo_payload_capacity(const struct sge *sge)
{
	return sge->freelQ[sge->jumbo_fl].rx_buffer_size -
		sge->freelQ[sge->jumbo_fl].dma_offset -
		sizeof(struct cpl_rx_data);
}

/*
 * Frees all SGE related resources and the sge structure itself
 */
void t1_sge_destroy(struct sge *sge)
{
	int i;

	for_each_port(sge->adapter, i)
		free_percpu(sge->port_stats[i]);

	kfree(sge->tx_sched);
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
static void refill_free_list(struct sge *sge, struct freelQ *q)
{
	struct pci_dev *pdev = sge->adapter->pdev;
	struct freelQ_ce *ce = &q->centries[q->pidx];
	struct freelQ_e *e = &q->entries[q->pidx];
	unsigned int dma_len = q->rx_buffer_size - q->dma_offset;

	while (q->credits < q->size) {
		struct sk_buff *skb;
		dma_addr_t mapping;

		skb = alloc_skb(q->rx_buffer_size, GFP_ATOMIC);
		if (!skb)
			break;

		skb_reserve(skb, q->dma_offset);
		mapping = pci_map_single(pdev, skb->data, dma_len,
					 PCI_DMA_FROMDEVICE);
		skb_reserve(skb, sge->rx_pkt_pad);

		ce->skb = skb;
		dma_unmap_addr_set(ce, dma_addr, mapping);
		dma_unmap_len_set(ce, dma_len, dma_len);
		e->addr_lo = (u32)mapping;
		e->addr_hi = (u64)mapping >> 32;
		e->len_gen = V_CMD_LEN(dma_len) | V_CMD_GEN1(q->genbit);
		wmb();
		e->gen2 = V_CMD_GEN2(q->genbit);

		e++;
		ce++;
		if (++q->pidx == q->size) {
			q->pidx = 0;
			q->genbit ^= 1;
			ce = q->centries;
			e = q->entries;
		}
		q->credits++;
	}
}

/*
 * Calls refill_free_list for both free lists. If we cannot fill at least 1/4
 * of both rings, we go into 'few interrupt mode' in order to give the system
 * time to free up resources.
 */
static void freelQs_empty(struct sge *sge)
{
	struct adapter *adapter = sge->adapter;
	u32 irq_reg = readl(adapter->regs + A_SG_INT_ENABLE);
	u32 irqholdoff_reg;

	refill_free_list(sge, &sge->freelQ[0]);
	refill_free_list(sge, &sge->freelQ[1]);

	if (sge->freelQ[0].credits > (sge->freelQ[0].size >> 2) &&
	    sge->freelQ[1].credits > (sge->freelQ[1].size >> 2)) {
		irq_reg |= F_FL_EXHAUSTED;
		irqholdoff_reg = sge->fixed_intrtimer;
	} else {
		/* Clear the F_FL_EXHAUSTED interrupts for now */
		irq_reg &= ~F_FL_EXHAUSTED;
		irqholdoff_reg = sge->intrtimer_nres;
	}
	writel(irqholdoff_reg, adapter->regs + A_SG_INTRTIMER);
	writel(irq_reg, adapter->regs + A_SG_INT_ENABLE);

	/* We reenable the Qs to force a freelist GTS interrupt later */
	doorbell_pio(adapter, F_FL0_ENABLE | F_FL1_ENABLE);
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
	u32 val = readl(sge->adapter->regs + A_PL_ENABLE);

	writel(val & ~SGE_PL_INTR_MASK, sge->adapter->regs + A_PL_ENABLE);
	writel(0, sge->adapter->regs + A_SG_INT_ENABLE);
}

/*
 * Enable SGE interrupts.
 */
void t1_sge_intr_enable(struct sge *sge)
{
	u32 en = SGE_INT_ENABLE;
	u32 val = readl(sge->adapter->regs + A_PL_ENABLE);

	if (sge->adapter->port[0].dev->hw_features & NETIF_F_TSO)
		en &= ~F_PACKET_TOO_BIG;
	writel(en, sge->adapter->regs + A_SG_INT_ENABLE);
	writel(val | SGE_PL_INTR_MASK, sge->adapter->regs + A_PL_ENABLE);
}

/*
 * Clear SGE interrupts.
 */
void t1_sge_intr_clear(struct sge *sge)
{
	writel(SGE_PL_INTR_MASK, sge->adapter->regs + A_PL_CAUSE);
	writel(0xffffffff, sge->adapter->regs + A_SG_INT_CAUSE);
}

/*
 * SGE 'Error' interrupt handler
 */
int t1_sge_intr_error_handler(struct sge *sge)
{
	struct adapter *adapter = sge->adapter;
	u32 cause = readl(adapter->regs + A_SG_INT_CAUSE);

	if (adapter->port[0].dev->hw_features & NETIF_F_TSO)
		cause &= ~F_PACKET_TOO_BIG;
	if (cause & F_RESPQ_EXHAUSTED)
		sge->stats.respQ_empty++;
	if (cause & F_RESPQ_OVERFLOW) {
		sge->stats.respQ_overflow++;
		pr_alert("%s: SGE response queue overflow\n",
			 adapter->name);
	}
	if (cause & F_FL_EXHAUSTED) {
		sge->stats.freelistQ_empty++;
		freelQs_empty(sge);
	}
	if (cause & F_PACKET_TOO_BIG) {
		sge->stats.pkt_too_big++;
		pr_alert("%s: SGE max packet size exceeded\n",
			 adapter->name);
	}
	if (cause & F_PACKET_MISMATCH) {
		sge->stats.pkt_mismatch++;
		pr_alert("%s: SGE packet mismatch\n", adapter->name);
	}
	if (cause & SGE_INT_FATAL)
		t1_fatal_err(adapter);

	writel(cause, adapter->regs + A_SG_INT_CAUSE);
	return 0;
}

const struct sge_intr_counts *t1_sge_get_intr_counts(const struct sge *sge)
{
	return &sge->stats;
}

void t1_sge_get_port_stats(const struct sge *sge, int port,
			   struct sge_port_stats *ss)
{
	int cpu;

	memset(ss, 0, sizeof(*ss));
	for_each_possible_cpu(cpu) {
		struct sge_port_stats *st = per_cpu_ptr(sge->port_stats[port], cpu);

		ss->rx_cso_good += st->rx_cso_good;
		ss->tx_cso += st->tx_cso;
		ss->tx_tso += st->tx_tso;
		ss->tx_need_hdrroom += st->tx_need_hdrroom;
		ss->vlan_xtract += st->vlan_xtract;
		ss->vlan_insert += st->vlan_insert;
	}
}

/**
 *	recycle_fl_buf - recycle a free list buffer
 *	@fl: the free list
 *	@idx: index of buffer to recycle
 *
 *	Recycles the specified buffer on the given free list by adding it at
 *	the next available slot on the list.
 */
static void recycle_fl_buf(struct freelQ *fl, int idx)
{
	struct freelQ_e *from = &fl->entries[idx];
	struct freelQ_e *to = &fl->entries[fl->pidx];

	fl->centries[fl->pidx] = fl->centries[idx];
	to->addr_lo = from->addr_lo;
	to->addr_hi = from->addr_hi;
	to->len_gen = G_CMD_LEN(from->len_gen) | V_CMD_GEN1(fl->genbit);
	wmb();
	to->gen2 = V_CMD_GEN2(fl->genbit);
	fl->credits++;

	if (++fl->pidx == fl->size) {
		fl->pidx = 0;
		fl->genbit ^= 1;
	}
}

static int copybreak __read_mostly = 256;
module_param(copybreak, int, 0);
MODULE_PARM_DESC(copybreak, "Receive copy threshold");

/**
 *	get_packet - return the next ingress packet buffer
 *	@pdev: the PCI device that received the packet
 *	@fl: the SGE free list holding the packet
 *	@len: the actual packet length, excluding any SGE padding
 *
 *	Get the next packet from a free list and complete setup of the
 *	sk_buff.  If the packet is small we make a copy and recycle the
 *	original buffer, otherwise we use the original buffer itself.  If a
 *	positive drop threshold is supplied packets are dropped and their
 *	buffers recycled if (a) the number of remaining buffers is under the
 *	threshold and the packet is too big to copy, or (b) the packet should
 *	be copied but there is no memory for the copy.
 */
static inline struct sk_buff *get_packet(struct pci_dev *pdev,
					 struct freelQ *fl, unsigned int len)
{
	struct sk_buff *skb;
	const struct freelQ_ce *ce = &fl->centries[fl->cidx];

	if (len < copybreak) {
		skb = alloc_skb(len + 2, GFP_ATOMIC);
		if (!skb)
			goto use_orig_buf;

		skb_reserve(skb, 2);	/* align IP header */
		skb_put(skb, len);
		pci_dma_sync_single_for_cpu(pdev,
					    dma_unmap_addr(ce, dma_addr),
					    dma_unmap_len(ce, dma_len),
					    PCI_DMA_FROMDEVICE);
		skb_copy_from_linear_data(ce->skb, skb->data, len);
		pci_dma_sync_single_for_device(pdev,
					       dma_unmap_addr(ce, dma_addr),
					       dma_unmap_len(ce, dma_len),
					       PCI_DMA_FROMDEVICE);
		recycle_fl_buf(fl, fl->cidx);
		return skb;
	}

use_orig_buf:
	if (fl->credits < 2) {
		recycle_fl_buf(fl, fl->cidx);
		return NULL;
	}

	pci_unmap_single(pdev, dma_unmap_addr(ce, dma_addr),
			 dma_unmap_len(ce, dma_len), PCI_DMA_FROMDEVICE);
	skb = ce->skb;
	prefetch(skb->data);

	skb_put(skb, len);
	return skb;
}

/**
 *	unexpected_offload - handle an unexpected offload packet
 *	@adapter: the adapter
 *	@fl: the free list that received the packet
 *
 *	Called when we receive an unexpected offload packet (e.g., the TOE
 *	function is disabled or the card is a NIC).  Prints a message and
 *	recycles the buffer.
 */
static void unexpected_offload(struct adapter *adapter, struct freelQ *fl)
{
	struct freelQ_ce *ce = &fl->centries[fl->cidx];
	struct sk_buff *skb = ce->skb;

	pci_dma_sync_single_for_cpu(adapter->pdev, dma_unmap_addr(ce, dma_addr),
			    dma_unmap_len(ce, dma_len), PCI_DMA_FROMDEVICE);
	pr_err("%s: unexpected offload packet, cmd %u\n",
	       adapter->name, *skb->data);
	recycle_fl_buf(fl, fl->cidx);
}

/*
 * T1/T2 SGE limits the maximum DMA size per TX descriptor to
 * SGE_TX_DESC_MAX_PLEN (16KB). If the PAGE_SIZE is larger than 16KB, the
 * stack might send more than SGE_TX_DESC_MAX_PLEN in a contiguous manner.
 * Note that the *_large_page_tx_descs stuff will be optimized out when
 * PAGE_SIZE <= SGE_TX_DESC_MAX_PLEN.
 *
 * compute_large_page_descs() computes how many additional descriptors are
 * required to break down the stack's request.
 */
static inline unsigned int compute_large_page_tx_descs(struct sk_buff *skb)
{
	unsigned int count = 0;

	if (PAGE_SIZE > SGE_TX_DESC_MAX_PLEN) {
		unsigned int nfrags = skb_shinfo(skb)->nr_frags;
		unsigned int i, len = skb_headlen(skb);
		while (len > SGE_TX_DESC_MAX_PLEN) {
			count++;
			len -= SGE_TX_DESC_MAX_PLEN;
		}
		for (i = 0; nfrags--; i++) {
			const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			len = skb_frag_size(frag);
			while (len > SGE_TX_DESC_MAX_PLEN) {
				count++;
				len -= SGE_TX_DESC_MAX_PLEN;
			}
		}
	}
	return count;
}

/*
 * Write a cmdQ entry.
 *
 * Since this function writes the 'flags' field, it must not be used to
 * write the first cmdQ entry.
 */
static inline void write_tx_desc(struct cmdQ_e *e, dma_addr_t mapping,
				 unsigned int len, unsigned int gen,
				 unsigned int eop)
{
	BUG_ON(len > SGE_TX_DESC_MAX_PLEN);

	e->addr_lo = (u32)mapping;
	e->addr_hi = (u64)mapping >> 32;
	e->len_gen = V_CMD_LEN(len) | V_CMD_GEN1(gen);
	e->flags = F_CMD_DATAVALID | V_CMD_EOP(eop) | V_CMD_GEN2(gen);
}

/*
 * See comment for previous function.
 *
 * write_tx_descs_large_page() writes additional SGE tx descriptors if
 * *desc_len exceeds HW's capability.
 */
static inline unsigned int write_large_page_tx_descs(unsigned int pidx,
						     struct cmdQ_e **e,
						     struct cmdQ_ce **ce,
						     unsigned int *gen,
						     dma_addr_t *desc_mapping,
						     unsigned int *desc_len,
						     unsigned int nfrags,
						     struct cmdQ *q)
{
	if (PAGE_SIZE > SGE_TX_DESC_MAX_PLEN) {
		struct cmdQ_e *e1 = *e;
		struct cmdQ_ce *ce1 = *ce;

		while (*desc_len > SGE_TX_DESC_MAX_PLEN) {
			*desc_len -= SGE_TX_DESC_MAX_PLEN;
			write_tx_desc(e1, *desc_mapping, SGE_TX_DESC_MAX_PLEN,
				      *gen, nfrags == 0 && *desc_len == 0);
			ce1->skb = NULL;
			dma_unmap_len_set(ce1, dma_len, 0);
			*desc_mapping += SGE_TX_DESC_MAX_PLEN;
			if (*desc_len) {
				ce1++;
				e1++;
				if (++pidx == q->size) {
					pidx = 0;
					*gen ^= 1;
					ce1 = q->centries;
					e1 = q->entries;
				}
			}
		}
		*e = e1;
		*ce = ce1;
	}
	return pidx;
}

/*
 * Write the command descriptors to transmit the given skb starting at
 * descriptor pidx with the given generation.
 */
static inline void write_tx_descs(struct adapter *adapter, struct sk_buff *skb,
				  unsigned int pidx, unsigned int gen,
				  struct cmdQ *q)
{
	dma_addr_t mapping, desc_mapping;
	struct cmdQ_e *e, *e1;
	struct cmdQ_ce *ce;
	unsigned int i, flags, first_desc_len, desc_len,
	    nfrags = skb_shinfo(skb)->nr_frags;

	e = e1 = &q->entries[pidx];
	ce = &q->centries[pidx];

	mapping = pci_map_single(adapter->pdev, skb->data,
				 skb_headlen(skb), PCI_DMA_TODEVICE);

	desc_mapping = mapping;
	desc_len = skb_headlen(skb);

	flags = F_CMD_DATAVALID | F_CMD_SOP |
	    V_CMD_EOP(nfrags == 0 && desc_len <= SGE_TX_DESC_MAX_PLEN) |
	    V_CMD_GEN2(gen);
	first_desc_len = (desc_len <= SGE_TX_DESC_MAX_PLEN) ?
	    desc_len : SGE_TX_DESC_MAX_PLEN;
	e->addr_lo = (u32)desc_mapping;
	e->addr_hi = (u64)desc_mapping >> 32;
	e->len_gen = V_CMD_LEN(first_desc_len) | V_CMD_GEN1(gen);
	ce->skb = NULL;
	dma_unmap_len_set(ce, dma_len, 0);

	if (PAGE_SIZE > SGE_TX_DESC_MAX_PLEN &&
	    desc_len > SGE_TX_DESC_MAX_PLEN) {
		desc_mapping += first_desc_len;
		desc_len -= first_desc_len;
		e1++;
		ce++;
		if (++pidx == q->size) {
			pidx = 0;
			gen ^= 1;
			e1 = q->entries;
			ce = q->centries;
		}
		pidx = write_large_page_tx_descs(pidx, &e1, &ce, &gen,
						 &desc_mapping, &desc_len,
						 nfrags, q);

		if (likely(desc_len))
			write_tx_desc(e1, desc_mapping, desc_len, gen,
				      nfrags == 0);
	}

	ce->skb = NULL;
	dma_unmap_addr_set(ce, dma_addr, mapping);
	dma_unmap_len_set(ce, dma_len, skb_headlen(skb));

	for (i = 0; nfrags--; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		e1++;
		ce++;
		if (++pidx == q->size) {
			pidx = 0;
			gen ^= 1;
			e1 = q->entries;
			ce = q->centries;
		}

		mapping = skb_frag_dma_map(&adapter->pdev->dev, frag, 0,
					   skb_frag_size(frag), DMA_TO_DEVICE);
		desc_mapping = mapping;
		desc_len = skb_frag_size(frag);

		pidx = write_large_page_tx_descs(pidx, &e1, &ce, &gen,
						 &desc_mapping, &desc_len,
						 nfrags, q);
		if (likely(desc_len))
			write_tx_desc(e1, desc_mapping, desc_len, gen,
				      nfrags == 0);
		ce->skb = NULL;
		dma_unmap_addr_set(ce, dma_addr, mapping);
		dma_unmap_len_set(ce, dma_len, skb_frag_size(frag));
	}
	ce->skb = skb;
	wmb();
	e->flags = flags;
}

/*
 * Clean up completed Tx buffers.
 */
static inline void reclaim_completed_tx(struct sge *sge, struct cmdQ *q)
{
	unsigned int reclaim = q->processed - q->cleaned;

	if (reclaim) {
		pr_debug("reclaim_completed_tx processed:%d cleaned:%d\n",
			 q->processed, q->cleaned);
		free_cmdQ_buffers(sge, q, reclaim);
		q->cleaned += reclaim;
	}
}

/*
 * Called from tasklet. Checks the scheduler for any
 * pending skbs that can be sent.
 */
static void restart_sched(unsigned long arg)
{
	struct sge *sge = (struct sge *) arg;
	struct adapter *adapter = sge->adapter;
	struct cmdQ *q = &sge->cmdQ[0];
	struct sk_buff *skb;
	unsigned int credits, queued_skb = 0;

	spin_lock(&q->lock);
	reclaim_completed_tx(sge, q);

	credits = q->size - q->in_use;
	pr_debug("restart_sched credits=%d\n", credits);
	while ((skb = sched_skb(sge, NULL, credits)) != NULL) {
		unsigned int genbit, pidx, count;
	        count = 1 + skb_shinfo(skb)->nr_frags;
		count += compute_large_page_tx_descs(skb);
		q->in_use += count;
		genbit = q->genbit;
		pidx = q->pidx;
		q->pidx += count;
		if (q->pidx >= q->size) {
			q->pidx -= q->size;
			q->genbit ^= 1;
		}
		write_tx_descs(adapter, skb, pidx, genbit, q);
	        credits = q->size - q->in_use;
		queued_skb = 1;
	}

	if (queued_skb) {
		clear_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
		if (test_and_set_bit(CMDQ_STAT_RUNNING, &q->status) == 0) {
			set_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
			writel(F_CMDQ0_ENABLE, adapter->regs + A_SG_DOORBELL);
		}
	}
	spin_unlock(&q->lock);
}

/**
 *	sge_rx - process an ingress ethernet packet
 *	@sge: the sge structure
 *	@fl: the free list that contains the packet buffer
 *	@len: the packet length
 *
 *	Process an ingress ethernet pakcet and deliver it to the stack.
 */
static void sge_rx(struct sge *sge, struct freelQ *fl, unsigned int len)
{
	struct sk_buff *skb;
	const struct cpl_rx_pkt *p;
	struct adapter *adapter = sge->adapter;
	struct sge_port_stats *st;
	struct net_device *dev;

	skb = get_packet(adapter->pdev, fl, len - sge->rx_pkt_pad);
	if (unlikely(!skb)) {
		sge->stats.rx_drops++;
		return;
	}

	p = (const struct cpl_rx_pkt *) skb->data;
	if (p->iff >= adapter->params.nports) {
		kfree_skb(skb);
		return;
	}
	__skb_pull(skb, sizeof(*p));

	st = this_cpu_ptr(sge->port_stats[p->iff]);
	dev = adapter->port[p->iff].dev;

	skb->protocol = eth_type_trans(skb, dev);
	if ((dev->features & NETIF_F_RXCSUM) && p->csum == 0xffff &&
	    skb->protocol == htons(ETH_P_IP) &&
	    (skb->data[9] == IPPROTO_TCP || skb->data[9] == IPPROTO_UDP)) {
		++st->rx_cso_good;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else
		skb_checksum_none_assert(skb);

	if (p->vlan_valid) {
		st->vlan_xtract++;
		__vlan_hwaccel_put_tag(skb, ntohs(p->vlan));
	}
	netif_receive_skb(skb);
}

/*
 * Returns true if a command queue has enough available descriptors that
 * we can resume Tx operation after temporarily disabling its packet queue.
 */
static inline int enough_free_Tx_descs(const struct cmdQ *q)
{
	unsigned int r = q->processed - q->cleaned;

	return q->in_use - r < (q->size >> 1);
}

/*
 * Called when sufficient space has become available in the SGE command queues
 * after the Tx packet schedulers have been suspended to restart the Tx path.
 */
static void restart_tx_queues(struct sge *sge)
{
	struct adapter *adap = sge->adapter;
	int i;

	if (!enough_free_Tx_descs(&sge->cmdQ[0]))
		return;

	for_each_port(adap, i) {
		struct net_device *nd = adap->port[i].dev;

		if (test_and_clear_bit(nd->if_port, &sge->stopped_tx_queues) &&
		    netif_running(nd)) {
			sge->stats.cmdQ_restarted[2]++;
			netif_wake_queue(nd);
		}
	}
}

/*
 * update_tx_info is called from the interrupt handler/NAPI to return cmdQ0
 * information.
 */
static unsigned int update_tx_info(struct adapter *adapter,
					  unsigned int flags,
					  unsigned int pr0)
{
	struct sge *sge = adapter->sge;
	struct cmdQ *cmdq = &sge->cmdQ[0];

	cmdq->processed += pr0;
	if (flags & (F_FL0_ENABLE | F_FL1_ENABLE)) {
		freelQs_empty(sge);
		flags &= ~(F_FL0_ENABLE | F_FL1_ENABLE);
	}
	if (flags & F_CMDQ0_ENABLE) {
		clear_bit(CMDQ_STAT_RUNNING, &cmdq->status);

		if (cmdq->cleaned + cmdq->in_use != cmdq->processed &&
		    !test_and_set_bit(CMDQ_STAT_LAST_PKT_DB, &cmdq->status)) {
			set_bit(CMDQ_STAT_RUNNING, &cmdq->status);
			writel(F_CMDQ0_ENABLE, adapter->regs + A_SG_DOORBELL);
		}
		if (sge->tx_sched)
			tasklet_hi_schedule(&sge->tx_sched->sched_tsk);

		flags &= ~F_CMDQ0_ENABLE;
	}

	if (unlikely(sge->stopped_tx_queues != 0))
		restart_tx_queues(sge);

	return flags;
}

/*
 * Process SGE responses, up to the supplied budget.  Returns the number of
 * responses processed.  A negative budget is effectively unlimited.
 */
static int process_responses(struct adapter *adapter, int budget)
{
	struct sge *sge = adapter->sge;
	struct respQ *q = &sge->respQ;
	struct respQ_e *e = &q->entries[q->cidx];
	int done = 0;
	unsigned int flags = 0;
	unsigned int cmdq_processed[SGE_CMDQ_N] = {0, 0};

	while (done < budget && e->GenerationBit == q->genbit) {
		flags |= e->Qsleeping;

		cmdq_processed[0] += e->Cmdq0CreditReturn;
		cmdq_processed[1] += e->Cmdq1CreditReturn;

		/* We batch updates to the TX side to avoid cacheline
		 * ping-pong of TX state information on MP where the sender
		 * might run on a different CPU than this function...
		 */
		if (unlikely((flags & F_CMDQ0_ENABLE) || cmdq_processed[0] > 64)) {
			flags = update_tx_info(adapter, flags, cmdq_processed[0]);
			cmdq_processed[0] = 0;
		}

		if (unlikely(cmdq_processed[1] > 16)) {
			sge->cmdQ[1].processed += cmdq_processed[1];
			cmdq_processed[1] = 0;
		}

		if (likely(e->DataValid)) {
			struct freelQ *fl = &sge->freelQ[e->FreelistQid];

			BUG_ON(!e->Sop || !e->Eop);
			if (unlikely(e->Offload))
				unexpected_offload(adapter, fl);
			else
				sge_rx(sge, fl, e->BufferLength);

			++done;

			/*
			 * Note: this depends on each packet consuming a
			 * single free-list buffer; cf. the BUG above.
			 */
			if (++fl->cidx == fl->size)
				fl->cidx = 0;
			prefetch(fl->centries[fl->cidx].skb);

			if (unlikely(--fl->credits <
				     fl->size - SGE_FREEL_REFILL_THRESH))
				refill_free_list(sge, fl);
		} else
			sge->stats.pure_rsps++;

		e++;
		if (unlikely(++q->cidx == q->size)) {
			q->cidx = 0;
			q->genbit ^= 1;
			e = q->entries;
		}
		prefetch(e);

		if (++q->credits > SGE_RESPQ_REPLENISH_THRES) {
			writel(q->credits, adapter->regs + A_SG_RSPQUEUECREDIT);
			q->credits = 0;
		}
	}

	flags = update_tx_info(adapter, flags, cmdq_processed[0]);
	sge->cmdQ[1].processed += cmdq_processed[1];

	return done;
}

static inline int responses_pending(const struct adapter *adapter)
{
	const struct respQ *Q = &adapter->sge->respQ;
	const struct respQ_e *e = &Q->entries[Q->cidx];

	return e->GenerationBit == Q->genbit;
}

/*
 * A simpler version of process_responses() that handles only pure (i.e.,
 * non data-carrying) responses.  Such respones are too light-weight to justify
 * calling a softirq when using NAPI, so we handle them specially in hard
 * interrupt context.  The function is called with a pointer to a response,
 * which the caller must ensure is a valid pure response.  Returns 1 if it
 * encounters a valid data-carrying response, 0 otherwise.
 */
static int process_pure_responses(struct adapter *adapter)
{
	struct sge *sge = adapter->sge;
	struct respQ *q = &sge->respQ;
	struct respQ_e *e = &q->entries[q->cidx];
	const struct freelQ *fl = &sge->freelQ[e->FreelistQid];
	unsigned int flags = 0;
	unsigned int cmdq_processed[SGE_CMDQ_N] = {0, 0};

	prefetch(fl->centries[fl->cidx].skb);
	if (e->DataValid)
		return 1;

	do {
		flags |= e->Qsleeping;

		cmdq_processed[0] += e->Cmdq0CreditReturn;
		cmdq_processed[1] += e->Cmdq1CreditReturn;

		e++;
		if (unlikely(++q->cidx == q->size)) {
			q->cidx = 0;
			q->genbit ^= 1;
			e = q->entries;
		}
		prefetch(e);

		if (++q->credits > SGE_RESPQ_REPLENISH_THRES) {
			writel(q->credits, adapter->regs + A_SG_RSPQUEUECREDIT);
			q->credits = 0;
		}
		sge->stats.pure_rsps++;
	} while (e->GenerationBit == q->genbit && !e->DataValid);

	flags = update_tx_info(adapter, flags, cmdq_processed[0]);
	sge->cmdQ[1].processed += cmdq_processed[1];

	return e->GenerationBit == q->genbit;
}

/*
 * Handler for new data events when using NAPI.  This does not need any locking
 * or protection from interrupts as data interrupts are off at this point and
 * other adapter interrupts do not interfere.
 */
int t1_poll(struct napi_struct *napi, int budget)
{
	struct adapter *adapter = container_of(napi, struct adapter, napi);
	int work_done = process_responses(adapter, budget);

	if (likely(work_done < budget)) {
		napi_complete(napi);
		writel(adapter->sge->respQ.cidx,
		       adapter->regs + A_SG_SLEEPING);
	}
	return work_done;
}

irqreturn_t t1_interrupt(int irq, void *data)
{
	struct adapter *adapter = data;
	struct sge *sge = adapter->sge;
	int handled;

	if (likely(responses_pending(adapter))) {
		writel(F_PL_INTR_SGE_DATA, adapter->regs + A_PL_CAUSE);

		if (napi_schedule_prep(&adapter->napi)) {
			if (process_pure_responses(adapter))
				__napi_schedule(&adapter->napi);
			else {
				/* no data, no NAPI needed */
				writel(sge->respQ.cidx, adapter->regs + A_SG_SLEEPING);
				/* undo schedule_prep */
				napi_enable(&adapter->napi);
			}
		}
		return IRQ_HANDLED;
	}

	spin_lock(&adapter->async_lock);
	handled = t1_slow_intr_handler(adapter);
	spin_unlock(&adapter->async_lock);

	if (!handled)
		sge->stats.unhandled_irqs++;

	return IRQ_RETVAL(handled != 0);
}

/*
 * Enqueues the sk_buff onto the cmdQ[qid] and has hardware fetch it.
 *
 * The code figures out how many entries the sk_buff will require in the
 * cmdQ and updates the cmdQ data structure with the state once the enqueue
 * has complete. Then, it doesn't access the global structure anymore, but
 * uses the corresponding fields on the stack. In conjunction with a spinlock
 * around that code, we can make the function reentrant without holding the
 * lock when we actually enqueue (which might be expensive, especially on
 * architectures with IO MMUs).
 *
 * This runs with softirqs disabled.
 */
static int t1_sge_tx(struct sk_buff *skb, struct adapter *adapter,
		     unsigned int qid, struct net_device *dev)
{
	struct sge *sge = adapter->sge;
	struct cmdQ *q = &sge->cmdQ[qid];
	unsigned int credits, pidx, genbit, count, use_sched_skb = 0;

	if (!spin_trylock(&q->lock))
		return NETDEV_TX_LOCKED;

	reclaim_completed_tx(sge, q);

	pidx = q->pidx;
	credits = q->size - q->in_use;
	count = 1 + skb_shinfo(skb)->nr_frags;
	count += compute_large_page_tx_descs(skb);

	/* Ethernet packet */
	if (unlikely(credits < count)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			set_bit(dev->if_port, &sge->stopped_tx_queues);
			sge->stats.cmdQ_full[2]++;
			pr_err("%s: Tx ring full while queue awake!\n",
			       adapter->name);
		}
		spin_unlock(&q->lock);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits - count < q->stop_thres)) {
		netif_stop_queue(dev);
		set_bit(dev->if_port, &sge->stopped_tx_queues);
		sge->stats.cmdQ_full[2]++;
	}

	/* T204 cmdQ0 skbs that are destined for a certain port have to go
	 * through the scheduler.
	 */
	if (sge->tx_sched && !qid && skb->dev) {
use_sched:
		use_sched_skb = 1;
		/* Note that the scheduler might return a different skb than
		 * the one passed in.
		 */
		skb = sched_skb(sge, skb, credits);
		if (!skb) {
			spin_unlock(&q->lock);
			return NETDEV_TX_OK;
		}
		pidx = q->pidx;
		count = 1 + skb_shinfo(skb)->nr_frags;
		count += compute_large_page_tx_descs(skb);
	}

	q->in_use += count;
	genbit = q->genbit;
	pidx = q->pidx;
	q->pidx += count;
	if (q->pidx >= q->size) {
		q->pidx -= q->size;
		q->genbit ^= 1;
	}
	spin_unlock(&q->lock);

	write_tx_descs(adapter, skb, pidx, genbit, q);

	/*
	 * We always ring the doorbell for cmdQ1.  For cmdQ0, we only ring
	 * the doorbell if the Q is asleep. There is a natural race, where
	 * the hardware is going to sleep just after we checked, however,
	 * then the interrupt handler will detect the outstanding TX packet
	 * and ring the doorbell for us.
	 */
	if (qid)
		doorbell_pio(adapter, F_CMDQ1_ENABLE);
	else {
		clear_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
		if (test_and_set_bit(CMDQ_STAT_RUNNING, &q->status) == 0) {
			set_bit(CMDQ_STAT_LAST_PKT_DB, &q->status);
			writel(F_CMDQ0_ENABLE, adapter->regs + A_SG_DOORBELL);
		}
	}

	if (use_sched_skb) {
		if (spin_trylock(&q->lock)) {
			credits = q->size - q->in_use;
			skb = NULL;
			goto use_sched;
		}
	}
	return NETDEV_TX_OK;
}

#define MK_ETH_TYPE_MSS(type, mss) (((mss) & 0x3FFF) | ((type) << 14))

/*
 *	eth_hdr_len - return the length of an Ethernet header
 *	@data: pointer to the start of the Ethernet header
 *
 *	Returns the length of an Ethernet header, including optional VLAN tag.
 */
static inline int eth_hdr_len(const void *data)
{
	const struct ethhdr *e = data;

	return e->h_proto == htons(ETH_P_8021Q) ? VLAN_ETH_HLEN : ETH_HLEN;
}

/*
 * Adds the CPL header to the sk_buff and passes it to t1_sge_tx.
 */
netdev_tx_t t1_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct adapter *adapter = dev->ml_priv;
	struct sge *sge = adapter->sge;
	struct sge_port_stats *st = this_cpu_ptr(sge->port_stats[dev->if_port]);
	struct cpl_tx_pkt *cpl;
	struct sk_buff *orig_skb = skb;
	int ret;

	if (skb->protocol == htons(ETH_P_CPL5))
		goto send;

	/*
	 * We are using a non-standard hard_header_len.
	 * Allocate more header room in the rare cases it is not big enough.
	 */
	if (unlikely(skb_headroom(skb) < dev->hard_header_len - ETH_HLEN)) {
		skb = skb_realloc_headroom(skb, sizeof(struct cpl_tx_pkt_lso));
		++st->tx_need_hdrroom;
		dev_kfree_skb_any(orig_skb);
		if (!skb)
			return NETDEV_TX_OK;
	}

	if (skb_shinfo(skb)->gso_size) {
		int eth_type;
		struct cpl_tx_pkt_lso *hdr;

		++st->tx_tso;

		eth_type = skb_network_offset(skb) == ETH_HLEN ?
			CPL_ETH_II : CPL_ETH_II_VLAN;

		hdr = (struct cpl_tx_pkt_lso *)skb_push(skb, sizeof(*hdr));
		hdr->opcode = CPL_TX_PKT_LSO;
		hdr->ip_csum_dis = hdr->l4_csum_dis = 0;
		hdr->ip_hdr_words = ip_hdr(skb)->ihl;
		hdr->tcp_hdr_words = tcp_hdr(skb)->doff;
		hdr->eth_type_mss = htons(MK_ETH_TYPE_MSS(eth_type,
							  skb_shinfo(skb)->gso_size));
		hdr->len = htonl(skb->len - sizeof(*hdr));
		cpl = (struct cpl_tx_pkt *)hdr;
	} else {
		/*
		 * Packets shorter than ETH_HLEN can break the MAC, drop them
		 * early.  Also, we may get oversized packets because some
		 * parts of the kernel don't handle our unusual hard_header_len
		 * right, drop those too.
		 */
		if (unlikely(skb->len < ETH_HLEN ||
			     skb->len > dev->mtu + eth_hdr_len(skb->data))) {
			pr_debug("%s: packet size %d hdr %d mtu%d\n", dev->name,
				 skb->len, eth_hdr_len(skb->data), dev->mtu);
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}

		if (skb->ip_summed == CHECKSUM_PARTIAL &&
		    ip_hdr(skb)->protocol == IPPROTO_UDP) {
			if (unlikely(skb_checksum_help(skb))) {
				pr_debug("%s: unable to do udp checksum\n", dev->name);
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
		}

		/* Hmmm, assuming to catch the gratious arp... and we'll use
		 * it to flush out stuck espi packets...
		 */
		if ((unlikely(!adapter->sge->espibug_skb[dev->if_port]))) {
			if (skb->protocol == htons(ETH_P_ARP) &&
			    arp_hdr(skb)->ar_op == htons(ARPOP_REQUEST)) {
				adapter->sge->espibug_skb[dev->if_port] = skb;
				/* We want to re-use this skb later. We
				 * simply bump the reference count and it
				 * will not be freed...
				 */
				skb = skb_get(skb);
			}
		}

		cpl = (struct cpl_tx_pkt *)__skb_push(skb, sizeof(*cpl));
		cpl->opcode = CPL_TX_PKT;
		cpl->ip_csum_dis = 1;    /* SW calculates IP csum */
		cpl->l4_csum_dis = skb->ip_summed == CHECKSUM_PARTIAL ? 0 : 1;
		/* the length field isn't used so don't bother setting it */

		st->tx_cso += (skb->ip_summed == CHECKSUM_PARTIAL);
	}
	cpl->iff = dev->if_port;

	if (vlan_tx_tag_present(skb)) {
		cpl->vlan_valid = 1;
		cpl->vlan = htons(vlan_tx_tag_get(skb));
		st->vlan_insert++;
	} else
		cpl->vlan_valid = 0;

send:
	ret = t1_sge_tx(skb, adapter, 0, dev);

	/* If transmit busy, and we reallocated skb's due to headroom limit,
	 * then silently discard to avoid leak.
	 */
	if (unlikely(ret != NETDEV_TX_OK && skb != orig_skb)) {
		dev_kfree_skb_any(skb);
		ret = NETDEV_TX_OK;
	}
	return ret;
}

/*
 * Callback for the Tx buffer reclaim timer.  Runs with softirqs disabled.
 */
static void sge_tx_reclaim_cb(unsigned long data)
{
	int i;
	struct sge *sge = (struct sge *)data;

	for (i = 0; i < SGE_CMDQ_N; ++i) {
		struct cmdQ *q = &sge->cmdQ[i];

		if (!spin_trylock(&q->lock))
			continue;

		reclaim_completed_tx(sge, q);
		if (i == 0 && q->in_use) {    /* flush pending credits */
			writel(F_CMDQ0_ENABLE, sge->adapter->regs + A_SG_DOORBELL);
		}
		spin_unlock(&q->lock);
	}
	mod_timer(&sge->tx_reclaim_timer, jiffies + TX_RECLAIM_PERIOD);
}

/*
 * Propagate changes of the SGE coalescing parameters to the HW.
 */
int t1_sge_set_coalesce_params(struct sge *sge, struct sge_params *p)
{
	sge->fixed_intrtimer = p->rx_coalesce_usecs *
		core_ticks_per_usec(sge->adapter);
	writel(sge->fixed_intrtimer, sge->adapter->regs + A_SG_INTRTIMER);
	return 0;
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
 * Disables the DMA engine.
 */
void t1_sge_stop(struct sge *sge)
{
	int i;
	writel(0, sge->adapter->regs + A_SG_CONTROL);
	readl(sge->adapter->regs + A_SG_CONTROL); /* flush */

	if (is_T2(sge->adapter))
		del_timer_sync(&sge->espibug_timer);

	del_timer_sync(&sge->tx_reclaim_timer);
	if (sge->tx_sched)
		tx_sched_stop(sge);

	for (i = 0; i < MAX_NPORTS; i++)
		kfree_skb(sge->espibug_skb[i]);
}

/*
 * Enables the DMA engine.
 */
void t1_sge_start(struct sge *sge)
{
	refill_free_list(sge, &sge->freelQ[0]);
	refill_free_list(sge, &sge->freelQ[1]);

	writel(sge->sge_control, sge->adapter->regs + A_SG_CONTROL);
	doorbell_pio(sge->adapter, F_FL0_ENABLE | F_FL1_ENABLE);
	readl(sge->adapter->regs + A_SG_CONTROL); /* flush */

	mod_timer(&sge->tx_reclaim_timer, jiffies + TX_RECLAIM_PERIOD);

	if (is_T2(sge->adapter))
		mod_timer(&sge->espibug_timer, jiffies + sge->espibug_timeout);
}

/*
 * Callback for the T2 ESPI 'stuck packet feature' workaorund
 */
static void espibug_workaround_t204(unsigned long data)
{
	struct adapter *adapter = (struct adapter *)data;
	struct sge *sge = adapter->sge;
	unsigned int nports = adapter->params.nports;
	u32 seop[MAX_NPORTS];

	if (adapter->open_device_map & PORT_MASK) {
		int i;

		if (t1_espi_get_mon_t204(adapter, &(seop[0]), 0) < 0)
			return;

		for (i = 0; i < nports; i++) {
			struct sk_buff *skb = sge->espibug_skb[i];

			if (!netif_running(adapter->port[i].dev) ||
			    netif_queue_stopped(adapter->port[i].dev) ||
			    !seop[i] || ((seop[i] & 0xfff) != 0) || !skb)
				continue;

			if (!skb->cb[0]) {
				skb_copy_to_linear_data_offset(skb,
						    sizeof(struct cpl_tx_pkt),
							       ch_mac_addr,
							       ETH_ALEN);
				skb_copy_to_linear_data_offset(skb,
							       skb->len - 10,
							       ch_mac_addr,
							       ETH_ALEN);
				skb->cb[0] = 0xff;
			}

			/* bump the reference count to avoid freeing of
			 * the skb once the DMA has completed.
			 */
			skb = skb_get(skb);
			t1_sge_tx(skb, adapter, 0, adapter->port[i].dev);
		}
	}
	mod_timer(&sge->espibug_timer, jiffies + sge->espibug_timeout);
}

static void espibug_workaround(unsigned long data)
{
	struct adapter *adapter = (struct adapter *)data;
	struct sge *sge = adapter->sge;

	if (netif_running(adapter->port[0].dev)) {
	        struct sk_buff *skb = sge->espibug_skb[0];
	        u32 seop = t1_espi_get_mon(adapter, 0x930, 0);

	        if ((seop & 0xfff0fff) == 0xfff && skb) {
	                if (!skb->cb[0]) {
	                        skb_copy_to_linear_data_offset(skb,
						     sizeof(struct cpl_tx_pkt),
							       ch_mac_addr,
							       ETH_ALEN);
	                        skb_copy_to_linear_data_offset(skb,
							       skb->len - 10,
							       ch_mac_addr,
							       ETH_ALEN);
	                        skb->cb[0] = 0xff;
	                }

	                /* bump the reference count to avoid freeing of the
	                 * skb once the DMA has completed.
	                 */
	                skb = skb_get(skb);
	                t1_sge_tx(skb, adapter, 0, adapter->port[0].dev);
	        }
	}
	mod_timer(&sge->espibug_timer, jiffies + sge->espibug_timeout);
}

/*
 * Creates a t1_sge structure and returns suggested resource parameters.
 */
struct sge * __devinit t1_sge_create(struct adapter *adapter,
				     struct sge_params *p)
{
	struct sge *sge = kzalloc(sizeof(*sge), GFP_KERNEL);
	int i;

	if (!sge)
		return NULL;

	sge->adapter = adapter;
	sge->netdev = adapter->port[0].dev;
	sge->rx_pkt_pad = t1_is_T1B(adapter) ? 0 : 2;
	sge->jumbo_fl = t1_is_T1B(adapter) ? 1 : 0;

	for_each_port(adapter, i) {
		sge->port_stats[i] = alloc_percpu(struct sge_port_stats);
		if (!sge->port_stats[i])
			goto nomem_port;
	}

	init_timer(&sge->tx_reclaim_timer);
	sge->tx_reclaim_timer.data = (unsigned long)sge;
	sge->tx_reclaim_timer.function = sge_tx_reclaim_cb;

	if (is_T2(sge->adapter)) {
		init_timer(&sge->espibug_timer);

		if (adapter->params.nports > 1) {
			tx_sched_init(sge);
			sge->espibug_timer.function = espibug_workaround_t204;
		} else
			sge->espibug_timer.function = espibug_workaround;
		sge->espibug_timer.data = (unsigned long)sge->adapter;

		sge->espibug_timeout = 1;
		/* for T204, every 10ms */
		if (adapter->params.nports > 1)
			sge->espibug_timeout = HZ/100;
	}


	p->cmdQ_size[0] = SGE_CMDQ0_E_N;
	p->cmdQ_size[1] = SGE_CMDQ1_E_N;
	p->freelQ_size[!sge->jumbo_fl] = SGE_FREEL_SIZE;
	p->freelQ_size[sge->jumbo_fl] = SGE_JUMBO_FREEL_SIZE;
	if (sge->tx_sched) {
		if (board_info(sge->adapter)->board == CHBT_BOARD_CHT204)
			p->rx_coalesce_usecs = 15;
		else
			p->rx_coalesce_usecs = 50;
	} else
		p->rx_coalesce_usecs = 50;

	p->coalesce_enable = 0;
	p->sample_interval_usecs = 0;

	return sge;
nomem_port:
	while (i >= 0) {
		free_percpu(sge->port_stats[i]);
		--i;
	}
	kfree(sge);
	return NULL;

}
