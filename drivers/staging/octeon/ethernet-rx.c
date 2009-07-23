/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
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
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/string.h>
#include <linux/prefetch.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <net/dst.h>
#ifdef CONFIG_XFRM
#include <linux/xfrm.h>
#include <net/xfrm.h>
#endif /* CONFIG_XFRM */

#include <asm/atomic.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-mem.h"
#include "ethernet-util.h"

#include "cvmx-helper.h"
#include "cvmx-wqe.h"
#include "cvmx-fau.h"
#include "cvmx-pow.h"
#include "cvmx-pip.h"
#include "cvmx-scratch.h"

#include "cvmx-gmxx-defs.h"

struct cvm_tasklet_wrapper {
	struct tasklet_struct t;
};

/*
 * Aligning the tasklet_struct on cachline boundries seems to decrease
 * throughput even though in theory it would reduce contantion on the
 * cache lines containing the locks.
 */

static struct cvm_tasklet_wrapper cvm_oct_tasklet[NR_CPUS];

/**
 * Interrupt handler. The interrupt occurs whenever the POW
 * transitions from 0->1 packets in our group.
 *
 * @cpl:
 * @dev_id:
 * @regs:
 * Returns
 */
irqreturn_t cvm_oct_do_interrupt(int cpl, void *dev_id)
{
	/* Acknowledge the interrupt */
	if (INTERRUPT_LIMIT)
		cvmx_write_csr(CVMX_POW_WQ_INT, 1 << pow_receive_group);
	else
		cvmx_write_csr(CVMX_POW_WQ_INT, 0x10001 << pow_receive_group);
	preempt_disable();
	tasklet_schedule(&cvm_oct_tasklet[smp_processor_id()].t);
	preempt_enable();
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * This is called when the kernel needs to manually poll the
 * device. For Octeon, this is simply calling the interrupt
 * handler. We actually poll all the devices, not just the
 * one supplied.
 *
 * @dev:    Device to poll. Unused
 */
void cvm_oct_poll_controller(struct net_device *dev)
{
	preempt_disable();
	tasklet_schedule(&cvm_oct_tasklet[smp_processor_id()].t);
	preempt_enable();
}
#endif

/**
 * This is called on receive errors, and determines if the packet
 * can be dropped early-on in cvm_oct_tasklet_rx().
 *
 * @work: Work queue entry pointing to the packet.
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
				   DEBUGPRINT("Port %d received 0xd5 preamble\n", work->ipprt);
				 */
				work->packet_ptr.s.addr += i + 1;
				work->len -= i + 5;
			} else if ((*ptr & 0xf) == 0xd) {
				/*
				   DEBUGPRINT("Port %d received 0x?d preamble\n", work->ipprt);
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
				DEBUGPRINT("Port %d unknown preamble, packet "
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
		DEBUGPRINT("Port %d receive error code %d, packet dropped\n",
			   work->ipprt, work->word2.snoip.err_code);
		cvm_oct_free_work(work);
		return 1;
	}

	return 0;
}

/**
 * Tasklet function that is scheduled on a core when an interrupt occurs.
 *
 * @unused:
 */
void cvm_oct_tasklet_rx(unsigned long unused)
{
	const int coreid = cvmx_get_core_num();
	uint64_t old_group_mask;
	uint64_t old_scratch;
	int rx_count = 0;
	int number_to_free;
	int num_freed;
	int packet_not_copied;

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

	if (USE_ASYNC_IOBDMA)
		cvmx_pow_work_request_async(CVMX_SCR_SCRATCH, CVMX_POW_NO_WAIT);

	while (1) {
		struct sk_buff *skb = NULL;
		int skb_in_hw;
		cvmx_wqe_t *work;

		if (USE_ASYNC_IOBDMA) {
			work = cvmx_pow_work_response_async(CVMX_SCR_SCRATCH);
		} else {
			if ((INTERRUPT_LIMIT == 0)
			    || likely(rx_count < MAX_RX_PACKETS))
				work =
				    cvmx_pow_work_request_sync
				    (CVMX_POW_NO_WAIT);
			else
				work = NULL;
		}
		prefetch(work);
		if (work == NULL)
			break;

		/*
		 * Limit each core to processing MAX_RX_PACKETS
		 * packets without a break.  This way the RX can't
		 * starve the TX task.
		 */
		if (USE_ASYNC_IOBDMA) {

			if ((INTERRUPT_LIMIT == 0)
			    || likely(rx_count < MAX_RX_PACKETS))
				cvmx_pow_work_request_async_nocheck
				    (CVMX_SCR_SCRATCH, CVMX_POW_NO_WAIT);
			else {
				cvmx_scratch_write64(CVMX_SCR_SCRATCH,
						     0x8000000000000000ull);
				cvmx_pow_tag_sw_null_nocheck();
			}
		}

		skb_in_hw = USE_SKBUFFS_IN_HW && work->word2.s.bufs == 1;
		if (likely(skb_in_hw)) {
			skb =
			    *(struct sk_buff
			      **)(cvm_oct_get_buffer_ptr(work->packet_ptr) -
				  sizeof(void *));
			prefetch(&skb->head);
			prefetch(&skb->len);
		}
		prefetch(cvm_oct_device[work->ipprt]);

		rx_count++;
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
			/*
			 * This calculation was changed in case the
			 * skb header is using a different address
			 * aliasing type than the buffer. It doesn't
			 * make any differnece now, but the new one is
			 * more correct.
			 */
			skb->data =
			    skb->head + work->packet_ptr.s.addr -
			    cvmx_ptr_to_phys(skb->head);
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
				DEBUGPRINT("Port %d failed to allocate "
					   "skbuff, packet dropped\n",
				     work->ipprt);
				cvm_oct_free_work(work);
				continue;
			}

			/*
			 * Check if we've received a packet that was
			 * entirely stored in the work entry. This is
			 * untested.
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
				union cvmx_buf_ptr segment_ptr =
					work->packet_ptr;
				int len = work->len;

				while (segments--) {
					union cvmx_buf_ptr next_ptr =
					    *(union cvmx_buf_ptr *)
					    cvmx_phys_to_ptr(segment_ptr.s.
							     addr - 8);
			/*
			 * Octeon Errata PKI-100: The segment size is
			 * wrong. Until it is fixed, calculate the
			 * segment size based on the packet pool
			 * buffer size. When it is fixed, the
			 * following line should be replaced with this
			 * one: int segment_size =
			 * segment_ptr.s.size;
			 */
					int segment_size =
					    CVMX_FPA_PACKET_POOL_SIZE -
					    (segment_ptr.s.addr -
					     (((segment_ptr.s.addr >> 7) -
					       segment_ptr.s.back) << 7));
					/* Don't copy more than what is left
					   in the packet */
					if (segment_size > len)
						segment_size = len;
					/* Copy the data into the packet */
					memcpy(skb_put(skb, segment_size),
					       cvmx_phys_to_ptr(segment_ptr.s.
								addr),
					       segment_size);
					/* Reduce the amount of bytes left
					   to copy */
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

			/* Only accept packets for devices
			   that are currently up */
			if (likely(dev->flags & IFF_UP)) {
				skb->protocol = eth_type_trans(skb, dev);
				skb->dev = dev;

				if (unlikely
				    (work->word2.s.not_IP
				     || work->word2.s.IP_exc
				     || work->word2.s.L4_error))
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
			} else {
				/*
				 * Drop any packet received for a
				 * device that isn't up.
				 */
				/*
				   DEBUGPRINT("%s: Device not up, packet dropped\n",
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
			DEBUGPRINT("Port %d not controlled by Linux, packet "
				   "dropped\n",
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

	if (USE_SKBUFFS_IN_HW) {
		/* Refill the packet buffer pool */
		number_to_free =
		    cvmx_fau_fetch_and_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

		if (number_to_free > 0) {
			cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
					      -number_to_free);
			num_freed =
			    cvm_oct_mem_fill_fpa(CVMX_FPA_PACKET_POOL,
						 CVMX_FPA_PACKET_POOL_SIZE,
						 number_to_free);
			if (num_freed != number_to_free) {
				cvmx_fau_atomic_add32
				    (FAU_NUM_PACKET_BUFFERS_TO_FREE,
				     number_to_free - num_freed);
			}
		}
	}
}

void cvm_oct_rx_initialize(void)
{
	int i;
	/* Initialize all of the tasklets */
	for (i = 0; i < NR_CPUS; i++)
		tasklet_init(&cvm_oct_tasklet[i].t, cvm_oct_tasklet_rx, 0);
}

void cvm_oct_rx_shutdown(void)
{
	int i;
	/* Shutdown all of the tasklets */
	for (i = 0; i < NR_CPUS; i++)
		tasklet_kill(&cvm_oct_tasklet[i].t);
}
