// SPDX-License-Identifier: GPL-2.0
/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2010 Cavium Networks
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/string.h>
#include <linux/prefetch.h>
#include <linux/ratelimit.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <net/dst.h>
#ifdef CONFIG_XFRM
#include <linux/xfrm.h>
#include <net/xfrm.h>
#endif /* CONFIG_XFRM */

#include "octeon-ethernet.h"
#include "ethernet-defines.h"
#include "ethernet-mem.h"
#include "ethernet-rx.h"
#include "ethernet-util.h"

static atomic_t oct_rx_ready = ATOMIC_INIT(0);

static struct oct_rx_group {
	int irq;
	int group;
	struct napi_struct napi;
} oct_rx_group[16];

/**
 * cvm_oct_do_interrupt - interrupt handler.
 * @irq: Interrupt number.
 * @napi_id: Cookie to identify the NAPI instance.
 *
 * The interrupt occurs whenever the POW has packets in our group.
 *
 */
static irqreturn_t cvm_oct_do_interrupt(int irq, void *napi_id)
{
	/* Disable the IRQ and start napi_poll. */
	disable_irq_nosync(irq);
	napi_schedule(napi_id);

	return IRQ_HANDLED;
}

/**
 * cvm_oct_check_rcv_error - process receive errors
 * @work: Work queue entry pointing to the packet.
 *
 * Returns Non-zero if the packet can be dropped, zero otherwise.
 */
static inline int cvm_oct_check_rcv_error(cvmx_wqe_t *work)
{
	int port;

	if (octeon_has_feature(OCTEON_FEATURE_PKND))
		port = work->word0.pip.cn68xx.pknd;
	else
		port = work->word1.cn38xx.ipprt;

	if ((work->word2.snoip.err_code == 10) && (work->word1.len <= 64)) {
		/*
		 * Ignore length errors on min size packets. Some
		 * equipment incorrectly pads packets to 64+4FCS
		 * instead of 60+4FCS.  Note these packets still get
		 * counted as frame errors.
		 */
	} else if (work->word2.snoip.err_code == 5 ||
		   work->word2.snoip.err_code == 7) {
		/*
		 * We received a packet with either an alignment error
		 * or a FCS error. This may be signalling that we are
		 * running 10Mbps with GMXX_RXX_FRM_CTL[PRE_CHK]
		 * off. If this is the case we need to parse the
		 * packet to determine if we can remove a non spec
		 * preamble and generate a correct packet.
		 */
		int interface = cvmx_helper_get_interface_num(port);
		int index = cvmx_helper_get_interface_index_num(port);
		union cvmx_gmxx_rxx_frm_ctl gmxx_rxx_frm_ctl;

		gmxx_rxx_frm_ctl.u64 =
		    cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
		if (gmxx_rxx_frm_ctl.s.pre_chk == 0) {
			u8 *ptr =
			    cvmx_phys_to_ptr(work->packet_ptr.s.addr);
			int i = 0;

			while (i < work->word1.len - 1) {
				if (*ptr != 0x55)
					break;
				ptr++;
				i++;
			}

			if (*ptr == 0xd5) {
				/* Port received 0xd5 preamble */
				work->packet_ptr.s.addr += i + 1;
				work->word1.len -= i + 5;
			} else if ((*ptr & 0xf) == 0xd) {
				/* Port received 0xd preamble */
				work->packet_ptr.s.addr += i;
				work->word1.len -= i + 4;
				for (i = 0; i < work->word1.len; i++) {
					*ptr =
					    ((*ptr & 0xf0) >> 4) |
					    ((*(ptr + 1) & 0xf) << 4);
					ptr++;
				}
			} else {
				printk_ratelimited("Port %d unknown preamble, packet dropped\n",
						   port);
				cvm_oct_free_work(work);
				return 1;
			}
		}
	} else {
		printk_ratelimited("Port %d receive error code %d, packet dropped\n",
				   port, work->word2.snoip.err_code);
		cvm_oct_free_work(work);
		return 1;
	}

	return 0;
}

static void copy_segments_to_skb(cvmx_wqe_t *work, struct sk_buff *skb)
{
	int segments = work->word2.s.bufs;
	union cvmx_buf_ptr segment_ptr = work->packet_ptr;
	int len = work->word1.len;
	int segment_size;

	while (segments--) {
		union cvmx_buf_ptr next_ptr;

		next_ptr = *(union cvmx_buf_ptr *)
			cvmx_phys_to_ptr(segment_ptr.s.addr - 8);

		/*
		 * Octeon Errata PKI-100: The segment size is wrong.
		 *
		 * Until it is fixed, calculate the segment size based on
		 * the packet pool buffer size.
		 * When it is fixed, the following line should be replaced
		 * with this one:
		 * int segment_size = segment_ptr.s.size;
		 */
		segment_size =
			CVMX_FPA_PACKET_POOL_SIZE -
			(segment_ptr.s.addr -
			 (((segment_ptr.s.addr >> 7) -
			   segment_ptr.s.back) << 7));

		/* Don't copy more than what is left in the packet */
		if (segment_size > len)
			segment_size = len;

		/* Copy the data into the packet */
		skb_put_data(skb, cvmx_phys_to_ptr(segment_ptr.s.addr),
			     segment_size);
		len -= segment_size;
		segment_ptr = next_ptr;
	}
}

static int cvm_oct_poll(struct oct_rx_group *rx_group, int budget)
{
	const int	coreid = cvmx_get_core_num();
	u64	old_group_mask;
	u64	old_scratch;
	int		rx_count = 0;
	int		did_work_request = 0;
	int		packet_not_copied;

	/* Prefetch cvm_oct_device since we know we need it soon */
	prefetch(cvm_oct_device);

	if (USE_ASYNC_IOBDMA) {
		/* Save scratch in case userspace is using it */
		CVMX_SYNCIOBDMA;
		old_scratch = cvmx_scratch_read64(CVMX_SCR_SCRATCH);
	}

	/* Only allow work for our group (and preserve priorities) */
	if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
		old_group_mask = cvmx_read_csr(CVMX_SSO_PPX_GRP_MSK(coreid));
		cvmx_write_csr(CVMX_SSO_PPX_GRP_MSK(coreid),
			       BIT(rx_group->group));
		cvmx_read_csr(CVMX_SSO_PPX_GRP_MSK(coreid)); /* Flush */
	} else {
		old_group_mask = cvmx_read_csr(CVMX_POW_PP_GRP_MSKX(coreid));
		cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid),
			       (old_group_mask & ~0xFFFFull) |
			       BIT(rx_group->group));
	}

	if (USE_ASYNC_IOBDMA) {
		cvmx_pow_work_request_async(CVMX_SCR_SCRATCH, CVMX_POW_NO_WAIT);
		did_work_request = 1;
	}

	while (rx_count < budget) {
		struct sk_buff *skb = NULL;
		struct sk_buff **pskb = NULL;
		int skb_in_hw;
		cvmx_wqe_t *work;
		int port;

		if (USE_ASYNC_IOBDMA && did_work_request)
			work = cvmx_pow_work_response_async(CVMX_SCR_SCRATCH);
		else
			work = cvmx_pow_work_request_sync(CVMX_POW_NO_WAIT);

		prefetch(work);
		did_work_request = 0;
		if (!work) {
			if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
				cvmx_write_csr(CVMX_SSO_WQ_IQ_DIS,
					       BIT(rx_group->group));
				cvmx_write_csr(CVMX_SSO_WQ_INT,
					       BIT(rx_group->group));
			} else {
				union cvmx_pow_wq_int wq_int;

				wq_int.u64 = 0;
				wq_int.s.iq_dis = BIT(rx_group->group);
				wq_int.s.wq_int = BIT(rx_group->group);
				cvmx_write_csr(CVMX_POW_WQ_INT, wq_int.u64);
			}
			break;
		}
		pskb = (struct sk_buff **)
			(cvm_oct_get_buffer_ptr(work->packet_ptr) -
			sizeof(void *));
		prefetch(pskb);

		if (USE_ASYNC_IOBDMA && rx_count < (budget - 1)) {
			cvmx_pow_work_request_async_nocheck(CVMX_SCR_SCRATCH,
							    CVMX_POW_NO_WAIT);
			did_work_request = 1;
		}
		rx_count++;

		skb_in_hw = work->word2.s.bufs == 1;
		if (likely(skb_in_hw)) {
			skb = *pskb;
			prefetch(&skb->head);
			prefetch(&skb->len);
		}

		if (octeon_has_feature(OCTEON_FEATURE_PKND))
			port = work->word0.pip.cn68xx.pknd;
		else
			port = work->word1.cn38xx.ipprt;

		prefetch(cvm_oct_device[port]);

		/* Immediately throw away all packets with receive errors */
		if (unlikely(work->word2.snoip.rcv_error)) {
			if (cvm_oct_check_rcv_error(work))
				continue;
		}

		/*
		 * We can only use the zero copy path if skbuffs are
		 * in the FPA pool and the packet fits in a single
		 * buffer.
		 */
		if (likely(skb_in_hw)) {
			skb->data = skb->head + work->packet_ptr.s.addr -
				cvmx_ptr_to_phys(skb->head);
			prefetch(skb->data);
			skb->len = work->word1.len;
			skb_set_tail_pointer(skb, skb->len);
			packet_not_copied = 1;
		} else {
			/*
			 * We have to copy the packet. First allocate
			 * an skbuff for it.
			 */
			skb = dev_alloc_skb(work->word1.len);
			if (!skb) {
				cvm_oct_free_work(work);
				continue;
			}

			/*
			 * Check if we've received a packet that was
			 * entirely stored in the work entry.
			 */
			if (unlikely(work->word2.s.bufs == 0)) {
				u8 *ptr = work->packet_data;

				if (likely(!work->word2.s.not_IP)) {
					/*
					 * The beginning of the packet
					 * moves for IP packets.
					 */
					if (work->word2.s.is_v6)
						ptr += 2;
					else
						ptr += 6;
				}
				skb_put_data(skb, ptr, work->word1.len);
				/* No packet buffers to free */
			} else {
				copy_segments_to_skb(work, skb);
			}
			packet_not_copied = 0;
		}
		if (likely((port < TOTAL_NUMBER_OF_PORTS) &&
			   cvm_oct_device[port])) {
			struct net_device *dev = cvm_oct_device[port];

			/*
			 * Only accept packets for devices that are
			 * currently up.
			 */
			if (likely(dev->flags & IFF_UP)) {
				skb->protocol = eth_type_trans(skb, dev);
				skb->dev = dev;

				if (unlikely(work->word2.s.not_IP ||
					     work->word2.s.IP_exc ||
					     work->word2.s.L4_error ||
					     !work->word2.s.tcp_or_udp))
					skb->ip_summed = CHECKSUM_NONE;
				else
					skb->ip_summed = CHECKSUM_UNNECESSARY;

				/* Increment RX stats for virtual ports */
				if (port >= CVMX_PIP_NUM_INPUT_PORTS) {
					dev->stats.rx_packets++;
					dev->stats.rx_bytes += skb->len;
				}
				netif_receive_skb(skb);
			} else {
				/*
				 * Drop any packet received for a device that
				 * isn't up.
				 */
				dev->stats.rx_dropped++;
				dev_kfree_skb_irq(skb);
			}
		} else {
			/*
			 * Drop any packet received for a device that
			 * doesn't exist.
			 */
			printk_ratelimited("Port %d not controlled by Linux, packet dropped\n",
					   port);
			dev_kfree_skb_irq(skb);
		}
		/*
		 * Check to see if the skbuff and work share the same
		 * packet buffer.
		 */
		if (likely(packet_not_copied)) {
			/*
			 * This buffer needs to be replaced, increment
			 * the number of buffers we need to free by
			 * one.
			 */
			cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
					      1);

			cvmx_fpa_free(work, CVMX_FPA_WQE_POOL, 1);
		} else {
			cvm_oct_free_work(work);
		}
	}
	/* Restore the original POW group mask */
	if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
		cvmx_write_csr(CVMX_SSO_PPX_GRP_MSK(coreid), old_group_mask);
		cvmx_read_csr(CVMX_SSO_PPX_GRP_MSK(coreid)); /* Flush */
	} else {
		cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid), old_group_mask);
	}

	if (USE_ASYNC_IOBDMA) {
		/* Restore the scratch area */
		cvmx_scratch_write64(CVMX_SCR_SCRATCH, old_scratch);
	}
	cvm_oct_rx_refill_pool(0);

	return rx_count;
}

/**
 * cvm_oct_napi_poll - the NAPI poll function.
 * @napi: The NAPI instance.
 * @budget: Maximum number of packets to receive.
 *
 * Returns the number of packets processed.
 */
static int cvm_oct_napi_poll(struct napi_struct *napi, int budget)
{
	struct oct_rx_group *rx_group = container_of(napi, struct oct_rx_group,
						     napi);
	int rx_count;

	rx_count = cvm_oct_poll(rx_group, budget);

	if (rx_count < budget) {
		/* No more work */
		napi_complete_done(napi, rx_count);
		enable_irq(rx_group->irq);
	}
	return rx_count;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * cvm_oct_poll_controller - poll for receive packets
 * device.
 *
 * @dev:    Device to poll. Unused
 */
void cvm_oct_poll_controller(struct net_device *dev)
{
	int i;

	if (!atomic_read(&oct_rx_ready))
		return;

	for (i = 0; i < ARRAY_SIZE(oct_rx_group); i++) {
		if (!(pow_receive_groups & BIT(i)))
			continue;

		cvm_oct_poll(&oct_rx_group[i], 16);
	}
}
#endif

void cvm_oct_rx_initialize(void)
{
	int i;
	struct net_device *dev_for_napi = NULL;

	for (i = 0; i < TOTAL_NUMBER_OF_PORTS; i++) {
		if (cvm_oct_device[i]) {
			dev_for_napi = cvm_oct_device[i];
			break;
		}
	}

	if (!dev_for_napi)
		panic("No net_devices were allocated.");

	for (i = 0; i < ARRAY_SIZE(oct_rx_group); i++) {
		int ret;

		if (!(pow_receive_groups & BIT(i)))
			continue;

		netif_napi_add(dev_for_napi, &oct_rx_group[i].napi,
			       cvm_oct_napi_poll, rx_napi_weight);
		napi_enable(&oct_rx_group[i].napi);

		oct_rx_group[i].irq = OCTEON_IRQ_WORKQ0 + i;
		oct_rx_group[i].group = i;

		/* Register an IRQ handler to receive POW interrupts */
		ret = request_irq(oct_rx_group[i].irq, cvm_oct_do_interrupt, 0,
				  "Ethernet", &oct_rx_group[i].napi);
		if (ret)
			panic("Could not acquire Ethernet IRQ %d\n",
			      oct_rx_group[i].irq);

		disable_irq_nosync(oct_rx_group[i].irq);

		/* Enable POW interrupt when our port has at least one packet */
		if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
			union cvmx_sso_wq_int_thrx int_thr;
			union cvmx_pow_wq_int_pc int_pc;

			int_thr.u64 = 0;
			int_thr.s.tc_en = 1;
			int_thr.s.tc_thr = 1;
			cvmx_write_csr(CVMX_SSO_WQ_INT_THRX(i), int_thr.u64);

			int_pc.u64 = 0;
			int_pc.s.pc_thr = 5;
			cvmx_write_csr(CVMX_SSO_WQ_INT_PC, int_pc.u64);
		} else {
			union cvmx_pow_wq_int_thrx int_thr;
			union cvmx_pow_wq_int_pc int_pc;

			int_thr.u64 = 0;
			int_thr.s.tc_en = 1;
			int_thr.s.tc_thr = 1;
			cvmx_write_csr(CVMX_POW_WQ_INT_THRX(i), int_thr.u64);

			int_pc.u64 = 0;
			int_pc.s.pc_thr = 5;
			cvmx_write_csr(CVMX_POW_WQ_INT_PC, int_pc.u64);
		}

		/* Schedule NAPI now. This will indirectly enable the
		 * interrupt.
		 */
		napi_schedule(&oct_rx_group[i].napi);
	}
	atomic_inc(&oct_rx_ready);
}

void cvm_oct_rx_shutdown(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(oct_rx_group); i++) {
		if (!(pow_receive_groups & BIT(i)))
			continue;

		/* Disable POW interrupt */
		if (OCTEON_IS_MODEL(OCTEON_CN68XX))
			cvmx_write_csr(CVMX_SSO_WQ_INT_THRX(i), 0);
		else
			cvmx_write_csr(CVMX_POW_WQ_INT_THRX(i), 0);

		/* Free the interrupt handler */
		free_irq(oct_rx_group[i].irq, cvm_oct_device);

		netif_napi_del(&oct_rx_group[i].napi);
	}
}
