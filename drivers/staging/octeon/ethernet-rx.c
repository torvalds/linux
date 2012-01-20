/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2010 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cache.h>
#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/string.h>
#include <linux/prefetch.h>
#include <linux/ratelimit.h>
#include <linux/smp.h>
#include <net/dst.h>
#ifdef CONFIG_XFRM
#include <linux/xfrm.h>
#include <net/xfrm.h>
#endif /* CONFIG_XFRM */

#include <linux/atomic.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "ethernet-mem.h"
#include "ethernet-rx.h"
#include "octeon-ethernet.h"
#include "ethernet-util.h"

#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-wqe.h>
#include <asm/octeon/cvmx-fau.h>
#include <asm/octeon/cvmx-pow.h>
#include <asm/octeon/cvmx-pip.h>
#include <asm/octeon/cvmx-scratch.h>

#include <asm/octeon/cvmx-gmxx-defs.h>

struct cvm_napi_wrapper {
	struct napi_struct napi;
} ____cacheline_aligned_in_smp;

static struct cvm_napi_wrapper cvm_oct_napi[NR_CPUS] __cacheline_aligned_in_smp;

struct cvm_oct_core_state {
	int baseline_cores;
	/*
	 * The number of additional cores that could be processing
	 * input packtes.
	 */
	atomic_t available_cores;
	cpumask_t cpu_state;
} ____cacheline_aligned_in_smp;

static struct cvm_oct_core_state core_state __cacheline_aligned_in_smp;

static void cvm_oct_enable_napi(void *_)
{
	int cpu = smp_processor_id();
	napi_schedule(&cvm_oct_napi[cpu].napi);
}

static void cvm_oct_enable_one_cpu(void)
{
	int v;
	int cpu;

	/* Check to see if more CPUs are available for receive processing... */
	v = atomic_sub_if_positive(1, &core_state.available_cores);
	if (v < 0)
		return;

	/* ... if a CPU is available, Turn on NAPI polling for that CPU.  */
	for_each_online_cpu(cpu) {
		if (!cpu_test_and_set(cpu, core_state.cpu_state)) {
			v = smp_call_function_single(cpu, cvm_oct_enable_napi,
						     NULL, 0);
			if (v)
				panic("Can't enable NAPI.");
			break;
		}
	}
}

static void cvm_oct_no_more_work(void)
{
	int cpu = smp_processor_id();

	/*
	 * CPU zero is special.  It always has the irq enabled when
	 * waiting for incoming packets.
	 */
	if (cpu == 0) {
		enable_irq(OCTEON_IRQ_WORKQ0 + pow_receive_group);
		return;
	}

	cpu_clear(cpu, core_state.cpu_state);
	atomic_add(1, &core_state.available_cores);
}

/**
 * cvm_oct_do_interrupt - interrupt handler.
 *
 * The interrupt occurs whenever the POW has packets in our group.
 *
 */
static irqreturn_t cvm_oct_do_interrupt(int cpl, void *dev_id)
{
	/* Disable the IRQ and start napi_poll. */
	disable_irq_nosync(OCTEON_IRQ_WORKQ0 + pow_receive_group);
	cvm_oct_enable_napi(NULL);

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
	if ((work->word2.snoip.err_code == 10) && (work->len <= 64)) {
		/*
		 * Ignore length errors on min size packets. Some
		 * equipment incorrectly pads packets to 64+4FCS
		 * instead of 60+4FCS.  Note these packets still get
		 * counted as frame errors.
		 */
	} else
	    if (USE_10MBPS_PREAMBLE_WORKAROUND
		&& ((work->word2.snoip.err_code == 5)
		    || (work->word2.snoip.err_code == 7))) {

		/*
		 * We received a packet with either an alignment error
		 * or a FCS error. This may be signalling that we are
		 * running 10Mbps with GMXX_RXX_FRM_CTL[PRE_CHK}
		 * off. If this is the case we need to parse the
		 * packet to determine if we can remove a non spec
		 * preamble and generate a correct packet.
		 */
		int interface = cvmx_helper_get_interface_num(work->ipprt);
		int index = cvmx_helper_get_interface_index_num(work->ipprt);
		union cvmx_gmxx_rxx_frm_ctl gmxx_rxx_frm_ctl;
		gmxx_rxx_frm_ctl.u64 =
		    cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
		if (gmxx_rxx_frm_ctl.s.pre_chk == 0) {

			uint8_t *ptr =
			    cvmx_phys_to_ptr(work->packet_ptr.s.addr);
			int i = 0;

			while (i < work->len - 1) {
				if (*ptr != 0x55)
					break;
				ptr++;
				i++;
			}

			if (*ptr == 0xd5) {
				/*
				  printk_ratelimited("Port %d received 0xd5 preamble\n", work->ipprt);
				 */
				work->packet_ptr.s.addr += i + 1;
				work->len -= i + 5;
			} else if ((*ptr & 0xf) == 0xd) {
				/*
				  printk_ratelimited("Port %d received 0x?d preamble\n", work->ipprt);
				 */
				work->packet_ptr.s.addr += i;
				work->len -= i + 4;
				for (i = 0; i < work->len; i++) {
					*ptr =
					    ((*ptr & 0xf0) >> 4) |
					    ((*(ptr + 1) & 0xf) << 4);
					ptr++;
				}
			} else {
				printk_ratelimited("Port %d unknown preamble, packet "
						   "dropped\n",
						   work->ipprt);
				/*
				   cvmx_helper_dump_packet(work);
				 */
				cvm_oct_free_work(work);
				return 1;
			}
		}
	} else {
		printk_ratelimited("Port %d receive error code %d, packet dropped\n",
				   work->ipprt, work->word2.snoip.err_code);
		cvm_oct_free_work(work);
		return 1;
	}

	return 0;
}

/**
 * cvm_oct_napi_poll - the NAPI poll function.
 * @napi: The NAPI instance, or null if called from cvm_oct_poll_controller
 * @budget: Maximum number of packets to receive.
 *
 * Returns the number of packets processed.
 */
static int cvm_oct_napi_poll(struct napi_struct *napi, int budget)
{
	const int	coreid = cvmx_get_core_num();
	uint64_t	old_group_mask;
	uint64_t	old_scratch;
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
	old_group_mask = cvmx_read_csr(CVMX_POW_PP_GRP_MSKX(coreid));
	cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid),
		       (old_group_mask & ~0xFFFFull) | 1 << pow_receive_group);

	if (USE_ASYNC_IOBDMA) {
		cvmx_pow_work_request_async(CVMX_SCR_SCRATCH, CVMX_POW_NO_WAIT);
		did_work_request = 1;
	}

	while (rx_count < budget) {
		struct sk_buff *skb = NULL;
		struct sk_buff **pskb = NULL;
		int skb_in_hw;
		cvmx_wqe_t *work;

		if (USE_ASYNC_IOBDMA && did_work_request)
			work = cvmx_pow_work_response_async(CVMX_SCR_SCRATCH);
		else
			work = cvmx_pow_work_request_sync(CVMX_POW_NO_WAIT);

		prefetch(work);
		did_work_request = 0;
		if (work == NULL) {
			union cvmx_pow_wq_int wq_int;
			wq_int.u64 = 0;
			wq_int.s.iq_dis = 1 << pow_receive_group;
			wq_int.s.wq_int = 1 << pow_receive_group;
			cvmx_write_csr(CVMX_POW_WQ_INT, wq_int.u64);
			break;
		}
		pskb = (struct sk_buff **)(cvm_oct_get_buffer_ptr(work->packet_ptr) - sizeof(void *));
		prefetch(pskb);

		if (USE_ASYNC_IOBDMA && rx_count < (budget - 1)) {
			cvmx_pow_work_request_async_nocheck(CVMX_SCR_SCRATCH, CVMX_POW_NO_WAIT);
			did_work_request = 1;
		}

		if (rx_count == 0) {
			/*
			 * First time through, see if there is enough
			 * work waiting to merit waking another
			 * CPU.
			 */
			union cvmx_pow_wq_int_cntx counts;
			int backlog;
			int cores_in_use = core_state.baseline_cores - atomic_read(&core_state.available_cores);
			counts.u64 = cvmx_read_csr(CVMX_POW_WQ_INT_CNTX(pow_receive_group));
			backlog = counts.s.iq_cnt + counts.s.ds_cnt;
			if (backlog > budget * cores_in_use && napi != NULL)
				cvm_oct_enable_one_cpu();
		}

		skb_in_hw = USE_SKBUFFS_IN_HW && work->word2.s.bufs == 1;
		if (likely(skb_in_hw)) {
			skb = *pskb;
			prefetch(&skb->head);
			prefetch(&skb->len);
		}
		prefetch(cvm_oct_device[work->ipprt]);

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
			skb->data = skb->head + work->packet_ptr.s.addr - cvmx_ptr_to_phys(skb->head);
			prefetch(skb->data);
			skb->len = work->len;
			skb_set_tail_pointer(skb, skb->len);
			packet_not_copied = 1;
		} else {
			/*
			 * We have to copy the packet. First allocate
			 * an skbuff for it.
			 */
			skb = dev_alloc_skb(work->len);
			if (!skb) {
				printk_ratelimited("Port %d failed to allocate "
						   "skbuff, packet dropped\n",
						   work->ipprt);
				cvm_oct_free_work(work);
				continue;
			}

			/*
			 * Check if we've received a packet that was
			 * entirely stored in the work entry.
			 */
			if (unlikely(work->word2.s.bufs == 0)) {
				uint8_t *ptr = work->packet_data;

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
				memcpy(skb_put(skb, work->len), ptr, work->len);
				/* No packet buffers to free */
			} else {
				int segments = work->word2.s.bufs;
				union cvmx_buf_ptr segment_ptr = work->packet_ptr;
				int len = work->len;

				while (segments--) {
					union cvmx_buf_ptr next_ptr =
					    *(union cvmx_buf_ptr *)cvmx_phys_to_ptr(segment_ptr.s.addr - 8);

			/*
			 * Octeon Errata PKI-100: The segment size is
			 * wrong. Until it is fixed, calculate the
			 * segment size based on the packet pool
			 * buffer size. When it is fixed, the
			 * following line should be replaced with this
			 * one: int segment_size =
			 * segment_ptr.s.size;
			 */
					int segment_size = CVMX_FPA_PACKET_POOL_SIZE -
						(segment_ptr.s.addr - (((segment_ptr.s.addr >> 7) - segment_ptr.s.back) << 7));
					/*
					 * Don't copy more than what
					 * is left in the packet.
					 */
					if (segment_size > len)
						segment_size = len;
					/* Copy the data into the packet */
					memcpy(skb_put(skb, segment_size),
					       cvmx_phys_to_ptr(segment_ptr.s.addr),
					       segment_size);
					len -= segment_size;
					segment_ptr = next_ptr;
				}
			}
			packet_not_copied = 0;
		}

		if (likely((work->ipprt < TOTAL_NUMBER_OF_PORTS) &&
			   cvm_oct_device[work->ipprt])) {
			struct net_device *dev = cvm_oct_device[work->ipprt];
			struct octeon_ethernet *priv = netdev_priv(dev);

			/*
			 * Only accept packets for devices that are
			 * currently up.
			 */
			if (likely(dev->flags & IFF_UP)) {
				skb->protocol = eth_type_trans(skb, dev);
				skb->dev = dev;

				if (unlikely(work->word2.s.not_IP || work->word2.s.IP_exc ||
					work->word2.s.L4_error || !work->word2.s.tcp_or_udp))
					skb->ip_summed = CHECKSUM_NONE;
				else
					skb->ip_summed = CHECKSUM_UNNECESSARY;

				/* Increment RX stats for virtual ports */
				if (work->ipprt >= CVMX_PIP_NUM_INPUT_PORTS) {
#ifdef CONFIG_64BIT
					atomic64_add(1, (atomic64_t *)&priv->stats.rx_packets);
					atomic64_add(skb->len, (atomic64_t *)&priv->stats.rx_bytes);
#else
					atomic_add(1, (atomic_t *)&priv->stats.rx_packets);
					atomic_add(skb->len, (atomic_t *)&priv->stats.rx_bytes);
#endif
				}
				netif_receive_skb(skb);
				rx_count++;
			} else {
				/* Drop any packet received for a device that isn't up */
				/*
				  printk_ratelimited("%s: Device not up, packet dropped\n",
					   dev->name);
				*/
#ifdef CONFIG_64BIT
				atomic64_add(1, (atomic64_t *)&priv->stats.rx_dropped);
#else
				atomic_add(1, (atomic_t *)&priv->stats.rx_dropped);
#endif
				dev_kfree_skb_irq(skb);
			}
		} else {
			/*
			 * Drop any packet received for a device that
			 * doesn't exist.
			 */
			printk_ratelimited("Port %d not controlled by Linux, packet dropped\n",
				   work->ipprt);
			dev_kfree_skb_irq(skb);
		}
		/*
		 * Check to see if the skbuff and work share the same
		 * packet buffer.
		 */
		if (USE_SKBUFFS_IN_HW && likely(packet_not_copied)) {
			/*
			 * This buffer needs to be replaced, increment
			 * the number of buffers we need to free by
			 * one.
			 */
			cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
					      1);

			cvmx_fpa_free(work, CVMX_FPA_WQE_POOL,
				      DONT_WRITEBACK(1));
		} else {
			cvm_oct_free_work(work);
		}
	}
	/* Restore the original POW group mask */
	cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid), old_group_mask);
	if (USE_ASYNC_IOBDMA) {
		/* Restore the scratch area */
		cvmx_scratch_write64(CVMX_SCR_SCRATCH, old_scratch);
	}
	cvm_oct_rx_refill_pool(0);

	if (rx_count < budget && napi != NULL) {
		/* No more work */
		napi_complete(napi);
		cvm_oct_no_more_work();
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
	cvm_oct_napi_poll(NULL, 16);
}
#endif

void cvm_oct_rx_initialize(void)
{
	int i;
	struct net_device *dev_for_napi = NULL;
	union cvmx_pow_wq_int_thrx int_thr;
	union cvmx_pow_wq_int_pc int_pc;

	for (i = 0; i < TOTAL_NUMBER_OF_PORTS; i++) {
		if (cvm_oct_device[i]) {
			dev_for_napi = cvm_oct_device[i];
			break;
		}
	}

	if (NULL == dev_for_napi)
		panic("No net_devices were allocated.");

	if (max_rx_cpus > 1  && max_rx_cpus < num_online_cpus())
		atomic_set(&core_state.available_cores, max_rx_cpus);
	else
		atomic_set(&core_state.available_cores, num_online_cpus());
	core_state.baseline_cores = atomic_read(&core_state.available_cores);

	core_state.cpu_state = CPU_MASK_NONE;
	for_each_possible_cpu(i) {
		netif_napi_add(dev_for_napi, &cvm_oct_napi[i].napi,
			       cvm_oct_napi_poll, rx_napi_weight);
		napi_enable(&cvm_oct_napi[i].napi);
	}
	/* Register an IRQ hander for to receive POW interrupts */
	i = request_irq(OCTEON_IRQ_WORKQ0 + pow_receive_group,
			cvm_oct_do_interrupt, 0, "Ethernet", cvm_oct_device);

	if (i)
		panic("Could not acquire Ethernet IRQ %d\n",
		      OCTEON_IRQ_WORKQ0 + pow_receive_group);

	disable_irq_nosync(OCTEON_IRQ_WORKQ0 + pow_receive_group);

	int_thr.u64 = 0;
	int_thr.s.tc_en = 1;
	int_thr.s.tc_thr = 1;
	/* Enable POW interrupt when our port has at least one packet */
	cvmx_write_csr(CVMX_POW_WQ_INT_THRX(pow_receive_group), int_thr.u64);

	int_pc.u64 = 0;
	int_pc.s.pc_thr = 5;
	cvmx_write_csr(CVMX_POW_WQ_INT_PC, int_pc.u64);


	/* Scheduld NAPI now.  This will indirectly enable interrupts. */
	cvm_oct_enable_one_cpu();
}

void cvm_oct_rx_shutdown(void)
{
	int i;
	/* Shutdown all of the NAPIs */
	for_each_possible_cpu(i)
		netif_napi_del(&cvm_oct_napi[i].napi);
}
