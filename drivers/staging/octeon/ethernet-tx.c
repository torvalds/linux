// SPDX-License-Identifier: GPL-2.0
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2010 Cavium Networks
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/ratelimit.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <net/dst.h>
#ifdef CONFIG_XFRM
#include <linux/xfrm.h>
#include <net/xfrm.h>
#endif /* CONFIG_XFRM */

#include <linux/atomic.h>
#include <net/sch_generic.h>

#include "octeon-ethernet.h"
#include "ethernet-defines.h"
#include "ethernet-tx.h"
#include "ethernet-util.h"

#define CVM_OCT_SKB_CB(skb)	((u64 *)((skb)->cb))

/*
 * You can define GET_SKBUFF_QOS() to override how the skbuff output
 * function determines which output queue is used. The default
 * implementation always uses the base queue for the port. If, for
 * example, you wanted to use the skb->priority field, define
 * GET_SKBUFF_QOS as: #define GET_SKBUFF_QOS(skb) ((skb)->priority)
 */
#ifndef GET_SKBUFF_QOS
#define GET_SKBUFF_QOS(skb) 0
#endif

static void cvm_oct_tx_do_cleanup(unsigned long arg);
static DECLARE_TASKLET(cvm_oct_tx_cleanup_tasklet, cvm_oct_tx_do_cleanup, 0);

/* Maximum number of SKBs to try to free per xmit packet. */
#define MAX_SKB_TO_FREE (MAX_OUT_QUEUE_DEPTH * 2)

static inline int cvm_oct_adjust_skb_to_free(int skb_to_free, int fau)
{
	int undo;

	undo = skb_to_free > 0 ? MAX_SKB_TO_FREE : skb_to_free +
						   MAX_SKB_TO_FREE;
	if (undo > 0)
		cvmx_fau_atomic_add32(fau, -undo);
	skb_to_free = -skb_to_free > MAX_SKB_TO_FREE ? MAX_SKB_TO_FREE :
						       -skb_to_free;
	return skb_to_free;
}

static void cvm_oct_kick_tx_poll_watchdog(void)
{
	union cvmx_ciu_timx ciu_timx;

	ciu_timx.u64 = 0;
	ciu_timx.s.one_shot = 1;
	ciu_timx.s.len = cvm_oct_tx_poll_interval;
	cvmx_write_csr(CVMX_CIU_TIMX(1), ciu_timx.u64);
}

static void cvm_oct_free_tx_skbs(struct net_device *dev)
{
	int skb_to_free;
	int qos, queues_per_port;
	int total_freed = 0;
	int total_remaining = 0;
	unsigned long flags;
	struct octeon_ethernet *priv = netdev_priv(dev);

	queues_per_port = cvmx_pko_get_num_queues(priv->port);
	/* Drain any pending packets in the free list */
	for (qos = 0; qos < queues_per_port; qos++) {
		if (skb_queue_len(&priv->tx_free_list[qos]) == 0)
			continue;
		skb_to_free = cvmx_fau_fetch_and_add32(priv->fau + qos * 4,
						       MAX_SKB_TO_FREE);
		skb_to_free = cvm_oct_adjust_skb_to_free(skb_to_free,
							 priv->fau + qos * 4);
		total_freed += skb_to_free;
		if (skb_to_free > 0) {
			struct sk_buff *to_free_list = NULL;

			spin_lock_irqsave(&priv->tx_free_list[qos].lock, flags);
			while (skb_to_free > 0) {
				struct sk_buff *t;

				t = __skb_dequeue(&priv->tx_free_list[qos]);
				t->next = to_free_list;
				to_free_list = t;
				skb_to_free--;
			}
			spin_unlock_irqrestore(&priv->tx_free_list[qos].lock,
					       flags);
			/* Do the actual freeing outside of the lock. */
			while (to_free_list) {
				struct sk_buff *t = to_free_list;

				to_free_list = to_free_list->next;
				dev_kfree_skb_any(t);
			}
		}
		total_remaining += skb_queue_len(&priv->tx_free_list[qos]);
	}
	if (total_remaining < MAX_OUT_QUEUE_DEPTH && netif_queue_stopped(dev))
		netif_wake_queue(dev);
	if (total_remaining)
		cvm_oct_kick_tx_poll_watchdog();
}

/**
 * cvm_oct_xmit - transmit a packet
 * @skb:    Packet to send
 * @dev:    Device info structure
 *
 * Returns Always returns NETDEV_TX_OK
 */
int cvm_oct_xmit(struct sk_buff *skb, struct net_device *dev)
{
	union cvmx_pko_command_word0 pko_command;
	union cvmx_buf_ptr hw_buffer;
	u64 old_scratch;
	u64 old_scratch2;
	int qos;
	int i;
	enum {QUEUE_CORE, QUEUE_HW, QUEUE_DROP} queue_type;
	struct octeon_ethernet *priv = netdev_priv(dev);
	struct sk_buff *to_free_list;
	int skb_to_free;
	int buffers_to_free;
	u32 total_to_clean;
	unsigned long flags;
#if REUSE_SKBUFFS_WITHOUT_FREE
	unsigned char *fpa_head;
#endif

	/*
	 * Prefetch the private data structure.  It is larger than the
	 * one cache line.
	 */
	prefetch(priv);

	/*
	 * The check on CVMX_PKO_QUEUES_PER_PORT_* is designed to
	 * completely remove "qos" in the event neither interface
	 * supports multiple queues per port.
	 */
	if ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 > 1) ||
	    (CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 > 1)) {
		qos = GET_SKBUFF_QOS(skb);
		if (qos <= 0)
			qos = 0;
		else if (qos >= cvmx_pko_get_num_queues(priv->port))
			qos = 0;
	} else {
		qos = 0;
	}

	if (USE_ASYNC_IOBDMA) {
		/* Save scratch in case userspace is using it */
		CVMX_SYNCIOBDMA;
		old_scratch = cvmx_scratch_read64(CVMX_SCR_SCRATCH);
		old_scratch2 = cvmx_scratch_read64(CVMX_SCR_SCRATCH + 8);

		/*
		 * Fetch and increment the number of packets to be
		 * freed.
		 */
		cvmx_fau_async_fetch_and_add32(CVMX_SCR_SCRATCH + 8,
					       FAU_NUM_PACKET_BUFFERS_TO_FREE,
					       0);
		cvmx_fau_async_fetch_and_add32(CVMX_SCR_SCRATCH,
					       priv->fau + qos * 4,
					       MAX_SKB_TO_FREE);
	}

	/*
	 * We have space for 6 segment pointers, If there will be more
	 * than that, we must linearize.
	 */
	if (unlikely(skb_shinfo(skb)->nr_frags > 5)) {
		if (unlikely(__skb_linearize(skb))) {
			queue_type = QUEUE_DROP;
			if (USE_ASYNC_IOBDMA) {
				/*
				 * Get the number of skbuffs in use
				 * by the hardware
				 */
				CVMX_SYNCIOBDMA;
				skb_to_free =
					cvmx_scratch_read64(CVMX_SCR_SCRATCH);
			} else {
				/*
				 * Get the number of skbuffs in use
				 * by the hardware
				 */
				skb_to_free =
				     cvmx_fau_fetch_and_add32(priv->fau +
							      qos * 4,
							      MAX_SKB_TO_FREE);
			}
			skb_to_free = cvm_oct_adjust_skb_to_free(skb_to_free,
								 priv->fau +
								 qos * 4);
			spin_lock_irqsave(&priv->tx_free_list[qos].lock, flags);
			goto skip_xmit;
		}
	}

	/*
	 * The CN3XXX series of parts has an errata (GMX-401) which
	 * causes the GMX block to hang if a collision occurs towards
	 * the end of a <68 byte packet. As a workaround for this, we
	 * pad packets to be 68 bytes whenever we are in half duplex
	 * mode. We don't handle the case of having a small packet but
	 * no room to add the padding.  The kernel should always give
	 * us at least a cache line
	 */
	if ((skb->len < 64) && OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		union cvmx_gmxx_prtx_cfg gmx_prt_cfg;
		int interface = INTERFACE(priv->port);
		int index = INDEX(priv->port);

		if (interface < 2) {
			/* We only need to pad packet in half duplex mode */
			gmx_prt_cfg.u64 =
			    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
			if (gmx_prt_cfg.s.duplex == 0) {
				int add_bytes = 64 - skb->len;

				if ((skb_tail_pointer(skb) + add_bytes) <=
				    skb_end_pointer(skb))
					__skb_put_zero(skb, add_bytes);
			}
		}
	}

	/* Build the PKO command */
	pko_command.u64 = 0;
#ifdef __LITTLE_ENDIAN
	pko_command.s.le = 1;
#endif
	pko_command.s.n2 = 1;	/* Don't pollute L2 with the outgoing packet */
	pko_command.s.segs = 1;
	pko_command.s.total_bytes = skb->len;
	pko_command.s.size0 = CVMX_FAU_OP_SIZE_32;
	pko_command.s.subone0 = 1;

	pko_command.s.dontfree = 1;

	/* Build the PKO buffer pointer */
	hw_buffer.u64 = 0;
	if (skb_shinfo(skb)->nr_frags == 0) {
		hw_buffer.s.addr = XKPHYS_TO_PHYS((uintptr_t)skb->data);
		hw_buffer.s.pool = 0;
		hw_buffer.s.size = skb->len;
	} else {
		hw_buffer.s.addr = XKPHYS_TO_PHYS((uintptr_t)skb->data);
		hw_buffer.s.pool = 0;
		hw_buffer.s.size = skb_headlen(skb);
		CVM_OCT_SKB_CB(skb)[0] = hw_buffer.u64;
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *fs = skb_shinfo(skb)->frags + i;

			hw_buffer.s.addr =
				XKPHYS_TO_PHYS((uintptr_t)skb_frag_address(fs));
			hw_buffer.s.size = skb_frag_size(fs);
			CVM_OCT_SKB_CB(skb)[i + 1] = hw_buffer.u64;
		}
		hw_buffer.s.addr =
			XKPHYS_TO_PHYS((uintptr_t)CVM_OCT_SKB_CB(skb));
		hw_buffer.s.size = skb_shinfo(skb)->nr_frags + 1;
		pko_command.s.segs = skb_shinfo(skb)->nr_frags + 1;
		pko_command.s.gather = 1;
		goto dont_put_skbuff_in_hw;
	}

	/*
	 * See if we can put this skb in the FPA pool. Any strange
	 * behavior from the Linux networking stack will most likely
	 * be caused by a bug in the following code. If some field is
	 * in use by the network stack and gets carried over when a
	 * buffer is reused, bad things may happen.  If in doubt and
	 * you dont need the absolute best performance, disable the
	 * define REUSE_SKBUFFS_WITHOUT_FREE. The reuse of buffers has
	 * shown a 25% increase in performance under some loads.
	 */
#if REUSE_SKBUFFS_WITHOUT_FREE
	fpa_head = skb->head + 256 - ((unsigned long)skb->head & 0x7f);
	if (unlikely(skb->data < fpa_head)) {
		/* TX buffer beginning can't meet FPA alignment constraints */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely
	    ((skb_end_pointer(skb) - fpa_head) < CVMX_FPA_PACKET_POOL_SIZE)) {
		/* TX buffer isn't large enough for the FPA */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely(skb_shared(skb))) {
		/* TX buffer sharing data with someone else */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely(skb_cloned(skb))) {
		/* TX buffer has been cloned */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely(skb_header_cloned(skb))) {
		/* TX buffer header has been cloned */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely(skb->destructor)) {
		/* TX buffer has a destructor */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely(skb_shinfo(skb)->nr_frags)) {
		/* TX buffer has fragments */
		goto dont_put_skbuff_in_hw;
	}
	if (unlikely
	    (skb->truesize !=
	     sizeof(*skb) + skb_end_offset(skb))) {
		/* TX buffer truesize has been changed */
		goto dont_put_skbuff_in_hw;
	}

	/*
	 * We can use this buffer in the FPA.  We don't need the FAU
	 * update anymore
	 */
	pko_command.s.dontfree = 0;

	hw_buffer.s.back = ((unsigned long)skb->data >> 7) -
			   ((unsigned long)fpa_head >> 7);

	*(struct sk_buff **)(fpa_head - sizeof(void *)) = skb;

	/*
	 * The skbuff will be reused without ever being freed. We must
	 * cleanup a bunch of core things.
	 */
	dst_release(skb_dst(skb));
	skb_dst_set(skb, NULL);
	skb_ext_reset(skb);
	nf_reset_ct(skb);
	skb_reset_redirect(skb);

#ifdef CONFIG_NET_SCHED
	skb->tc_index = 0;
#endif /* CONFIG_NET_SCHED */
#endif /* REUSE_SKBUFFS_WITHOUT_FREE */

dont_put_skbuff_in_hw:

	/* Check if we can use the hardware checksumming */
	if ((skb->protocol == htons(ETH_P_IP)) &&
	    (ip_hdr(skb)->version == 4) &&
	    (ip_hdr(skb)->ihl == 5) &&
	    ((ip_hdr(skb)->frag_off == 0) ||
	     (ip_hdr(skb)->frag_off == htons(1 << 14))) &&
	    ((ip_hdr(skb)->protocol == IPPROTO_TCP) ||
	     (ip_hdr(skb)->protocol == IPPROTO_UDP))) {
		/* Use hardware checksum calc */
		pko_command.s.ipoffp1 = skb_network_offset(skb) + 1;
	}

	if (USE_ASYNC_IOBDMA) {
		/* Get the number of skbuffs in use by the hardware */
		CVMX_SYNCIOBDMA;
		skb_to_free = cvmx_scratch_read64(CVMX_SCR_SCRATCH);
		buffers_to_free = cvmx_scratch_read64(CVMX_SCR_SCRATCH + 8);
	} else {
		/* Get the number of skbuffs in use by the hardware */
		skb_to_free = cvmx_fau_fetch_and_add32(priv->fau + qos * 4,
						       MAX_SKB_TO_FREE);
		buffers_to_free =
		    cvmx_fau_fetch_and_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);
	}

	skb_to_free = cvm_oct_adjust_skb_to_free(skb_to_free,
						 priv->fau + qos * 4);

	/*
	 * If we're sending faster than the receive can free them then
	 * don't do the HW free.
	 */
	if ((buffers_to_free < -100) && !pko_command.s.dontfree)
		pko_command.s.dontfree = 1;

	if (pko_command.s.dontfree) {
		queue_type = QUEUE_CORE;
		pko_command.s.reg0 = priv->fau + qos * 4;
	} else {
		queue_type = QUEUE_HW;
	}
	if (USE_ASYNC_IOBDMA)
		cvmx_fau_async_fetch_and_add32(CVMX_SCR_SCRATCH,
					       FAU_TOTAL_TX_TO_CLEAN, 1);

	spin_lock_irqsave(&priv->tx_free_list[qos].lock, flags);

	/* Drop this packet if we have too many already queued to the HW */
	if (unlikely(skb_queue_len(&priv->tx_free_list[qos]) >=
		     MAX_OUT_QUEUE_DEPTH)) {
		if (dev->tx_queue_len != 0) {
			/* Drop the lock when notifying the core.  */
			spin_unlock_irqrestore(&priv->tx_free_list[qos].lock,
					       flags);
			netif_stop_queue(dev);
			spin_lock_irqsave(&priv->tx_free_list[qos].lock,
					  flags);
		} else {
			/* If not using normal queueing.  */
			queue_type = QUEUE_DROP;
			goto skip_xmit;
		}
	}

	cvmx_pko_send_packet_prepare(priv->port, priv->queue + qos,
				     CVMX_PKO_LOCK_NONE);

	/* Send the packet to the output queue */
	if (unlikely(cvmx_pko_send_packet_finish(priv->port,
						 priv->queue + qos,
						 pko_command, hw_buffer,
						 CVMX_PKO_LOCK_NONE))) {
		printk_ratelimited("%s: Failed to send the packet\n",
				   dev->name);
		queue_type = QUEUE_DROP;
	}
skip_xmit:
	to_free_list = NULL;

	switch (queue_type) {
	case QUEUE_DROP:
		skb->next = to_free_list;
		to_free_list = skb;
		dev->stats.tx_dropped++;
		break;
	case QUEUE_HW:
		cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, -1);
		break;
	case QUEUE_CORE:
		__skb_queue_tail(&priv->tx_free_list[qos], skb);
		break;
	default:
		BUG();
	}

	while (skb_to_free > 0) {
		struct sk_buff *t = __skb_dequeue(&priv->tx_free_list[qos]);

		t->next = to_free_list;
		to_free_list = t;
		skb_to_free--;
	}

	spin_unlock_irqrestore(&priv->tx_free_list[qos].lock, flags);

	/* Do the actual freeing outside of the lock. */
	while (to_free_list) {
		struct sk_buff *t = to_free_list;

		to_free_list = to_free_list->next;
		dev_kfree_skb_any(t);
	}

	if (USE_ASYNC_IOBDMA) {
		CVMX_SYNCIOBDMA;
		total_to_clean = cvmx_scratch_read64(CVMX_SCR_SCRATCH);
		/* Restore the scratch area */
		cvmx_scratch_write64(CVMX_SCR_SCRATCH, old_scratch);
		cvmx_scratch_write64(CVMX_SCR_SCRATCH + 8, old_scratch2);
	} else {
		total_to_clean =
			cvmx_fau_fetch_and_add32(FAU_TOTAL_TX_TO_CLEAN, 1);
	}

	if (total_to_clean & 0x3ff) {
		/*
		 * Schedule the cleanup tasklet every 1024 packets for
		 * the pathological case of high traffic on one port
		 * delaying clean up of packets on a different port
		 * that is blocked waiting for the cleanup.
		 */
		tasklet_schedule(&cvm_oct_tx_cleanup_tasklet);
	}

	cvm_oct_kick_tx_poll_watchdog();

	return NETDEV_TX_OK;
}

/**
 * cvm_oct_xmit_pow - transmit a packet to the POW
 * @skb:    Packet to send
 * @dev:    Device info structure

 * Returns Always returns zero
 */
int cvm_oct_xmit_pow(struct sk_buff *skb, struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	void *packet_buffer;
	void *copy_location;

	/* Get a work queue entry */
	struct cvmx_wqe *work = cvmx_fpa_alloc(CVMX_FPA_WQE_POOL);

	if (unlikely(!work)) {
		printk_ratelimited("%s: Failed to allocate a work queue entry\n",
				   dev->name);
		dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return 0;
	}

	/* Get a packet buffer */
	packet_buffer = cvmx_fpa_alloc(CVMX_FPA_PACKET_POOL);
	if (unlikely(!packet_buffer)) {
		printk_ratelimited("%s: Failed to allocate a packet buffer\n",
				   dev->name);
		cvmx_fpa_free(work, CVMX_FPA_WQE_POOL, 1);
		dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return 0;
	}

	/*
	 * Calculate where we need to copy the data to. We need to
	 * leave 8 bytes for a next pointer (unused). We also need to
	 * include any configure skip. Then we need to align the IP
	 * packet src and dest into the same 64bit word. The below
	 * calculation may add a little extra, but that doesn't
	 * hurt.
	 */
	copy_location = packet_buffer + sizeof(u64);
	copy_location += ((CVMX_HELPER_FIRST_MBUFF_SKIP + 7) & 0xfff8) + 6;

	/*
	 * We have to copy the packet since whoever processes this
	 * packet will free it to a hardware pool. We can't use the
	 * trick of counting outstanding packets like in
	 * cvm_oct_xmit.
	 */
	memcpy(copy_location, skb->data, skb->len);

	/*
	 * Fill in some of the work queue fields. We may need to add
	 * more if the software at the other end needs them.
	 */
	if (!OCTEON_IS_MODEL(OCTEON_CN68XX))
		work->word0.pip.cn38xx.hw_chksum = skb->csum;
	work->word1.len = skb->len;
	cvmx_wqe_set_port(work, priv->port);
	cvmx_wqe_set_qos(work, priv->port & 0x7);
	cvmx_wqe_set_grp(work, pow_send_group);
	work->word1.tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	work->word1.tag = pow_send_group;	/* FIXME */
	/* Default to zero. Sets of zero later are commented out */
	work->word2.u64 = 0;
	work->word2.s.bufs = 1;
	work->packet_ptr.u64 = 0;
	work->packet_ptr.s.addr = cvmx_ptr_to_phys(copy_location);
	work->packet_ptr.s.pool = CVMX_FPA_PACKET_POOL;
	work->packet_ptr.s.size = CVMX_FPA_PACKET_POOL_SIZE;
	work->packet_ptr.s.back = (copy_location - packet_buffer) >> 7;

	if (skb->protocol == htons(ETH_P_IP)) {
		work->word2.s.ip_offset = 14;
#if 0
		work->word2.s.vlan_valid = 0;	/* FIXME */
		work->word2.s.vlan_cfi = 0;	/* FIXME */
		work->word2.s.vlan_id = 0;	/* FIXME */
		work->word2.s.dec_ipcomp = 0;	/* FIXME */
#endif
		work->word2.s.tcp_or_udp =
		    (ip_hdr(skb)->protocol == IPPROTO_TCP) ||
		    (ip_hdr(skb)->protocol == IPPROTO_UDP);
#if 0
		/* FIXME */
		work->word2.s.dec_ipsec = 0;
		/* We only support IPv4 right now */
		work->word2.s.is_v6 = 0;
		/* Hardware would set to zero */
		work->word2.s.software = 0;
		/* No error, packet is internal */
		work->word2.s.L4_error = 0;
#endif
		work->word2.s.is_frag = !((ip_hdr(skb)->frag_off == 0) ||
					  (ip_hdr(skb)->frag_off ==
					      cpu_to_be16(1 << 14)));
#if 0
		/* Assume Linux is sending a good packet */
		work->word2.s.IP_exc = 0;
#endif
		work->word2.s.is_bcast = (skb->pkt_type == PACKET_BROADCAST);
		work->word2.s.is_mcast = (skb->pkt_type == PACKET_MULTICAST);
#if 0
		/* This is an IP packet */
		work->word2.s.not_IP = 0;
		/* No error, packet is internal */
		work->word2.s.rcv_error = 0;
		/* No error, packet is internal */
		work->word2.s.err_code = 0;
#endif

		/*
		 * When copying the data, include 4 bytes of the
		 * ethernet header to align the same way hardware
		 * does.
		 */
		memcpy(work->packet_data, skb->data + 10,
		       sizeof(work->packet_data));
	} else {
#if 0
		work->word2.snoip.vlan_valid = 0;	/* FIXME */
		work->word2.snoip.vlan_cfi = 0;	/* FIXME */
		work->word2.snoip.vlan_id = 0;	/* FIXME */
		work->word2.snoip.software = 0;	/* Hardware would set to zero */
#endif
		work->word2.snoip.is_rarp = skb->protocol == htons(ETH_P_RARP);
		work->word2.snoip.is_arp = skb->protocol == htons(ETH_P_ARP);
		work->word2.snoip.is_bcast =
		    (skb->pkt_type == PACKET_BROADCAST);
		work->word2.snoip.is_mcast =
		    (skb->pkt_type == PACKET_MULTICAST);
		work->word2.snoip.not_IP = 1;	/* IP was done up above */
#if 0
		/* No error, packet is internal */
		work->word2.snoip.rcv_error = 0;
		/* No error, packet is internal */
		work->word2.snoip.err_code = 0;
#endif
		memcpy(work->packet_data, skb->data, sizeof(work->packet_data));
	}

	/* Submit the packet to the POW */
	cvmx_pow_work_submit(work, work->word1.tag, work->word1.tag_type,
			     cvmx_wqe_get_qos(work), cvmx_wqe_get_grp(work));
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	dev_consume_skb_any(skb);
	return 0;
}

/**
 * cvm_oct_tx_shutdown_dev - free all skb that are currently queued for TX.
 * @dev:    Device being shutdown
 *
 */
void cvm_oct_tx_shutdown_dev(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	unsigned long flags;
	int qos;

	for (qos = 0; qos < 16; qos++) {
		spin_lock_irqsave(&priv->tx_free_list[qos].lock, flags);
		while (skb_queue_len(&priv->tx_free_list[qos]))
			dev_kfree_skb_any(__skb_dequeue
					  (&priv->tx_free_list[qos]));
		spin_unlock_irqrestore(&priv->tx_free_list[qos].lock, flags);
	}
}

static void cvm_oct_tx_do_cleanup(unsigned long arg)
{
	int port;

	for (port = 0; port < TOTAL_NUMBER_OF_PORTS; port++) {
		if (cvm_oct_device[port]) {
			struct net_device *dev = cvm_oct_device[port];

			cvm_oct_free_tx_skbs(dev);
		}
	}
}

static irqreturn_t cvm_oct_tx_cleanup_watchdog(int cpl, void *dev_id)
{
	/* Disable the interrupt.  */
	cvmx_write_csr(CVMX_CIU_TIMX(1), 0);
	/* Do the work in the tasklet.  */
	tasklet_schedule(&cvm_oct_tx_cleanup_tasklet);
	return IRQ_HANDLED;
}

void cvm_oct_tx_initialize(void)
{
	int i;

	/* Disable the interrupt.  */
	cvmx_write_csr(CVMX_CIU_TIMX(1), 0);
	/* Register an IRQ handler to receive CIU_TIMX(1) interrupts */
	i = request_irq(OCTEON_IRQ_TIMER1,
			cvm_oct_tx_cleanup_watchdog, 0,
			"Ethernet", cvm_oct_device);

	if (i)
		panic("Could not acquire Ethernet IRQ %d\n", OCTEON_IRQ_TIMER1);
}

void cvm_oct_tx_shutdown(void)
{
	/* Free the interrupt handler */
	free_irq(OCTEON_IRQ_TIMER1, cvm_oct_device);
}
