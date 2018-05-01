/*
 * Copyright(c) 2015-2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/hrtimer.h>
#include <linux/bitmap.h>
#include <rdma/rdma_vt.h>

#include "hfi.h"
#include "device.h"
#include "common.h"
#include "trace.h"
#include "mad.h"
#include "sdma.h"
#include "debugfs.h"
#include "verbs.h"
#include "aspm.h"
#include "affinity.h"
#include "vnic.h"
#include "exp_rcv.h"

#undef pr_fmt
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#define HFI1_MAX_ACTIVE_WORKQUEUE_ENTRIES 5
/*
 * min buffers we want to have per context, after driver
 */
#define HFI1_MIN_USER_CTXT_BUFCNT 7

#define HFI1_MIN_HDRQ_EGRBUF_CNT 2
#define HFI1_MAX_HDRQ_EGRBUF_CNT 16352
#define HFI1_MIN_EAGER_BUFFER_SIZE (4 * 1024) /* 4KB */
#define HFI1_MAX_EAGER_BUFFER_SIZE (256 * 1024) /* 256KB */

/*
 * Number of user receive contexts we are configured to use (to allow for more
 * pio buffers per ctxt, etc.)  Zero means use one user context per CPU.
 */
int num_user_contexts = -1;
module_param_named(num_user_contexts, num_user_contexts, int, 0444);
MODULE_PARM_DESC(
	num_user_contexts, "Set max number of user contexts to use (default: -1 will use the real (non-HT) CPU count)");

uint krcvqs[RXE_NUM_DATA_VL];
int krcvqsset;
module_param_array(krcvqs, uint, &krcvqsset, S_IRUGO);
MODULE_PARM_DESC(krcvqs, "Array of the number of non-control kernel receive queues by VL");

/* computed based on above array */
unsigned long n_krcvqs;

static unsigned hfi1_rcvarr_split = 25;
module_param_named(rcvarr_split, hfi1_rcvarr_split, uint, S_IRUGO);
MODULE_PARM_DESC(rcvarr_split, "Percent of context's RcvArray entries used for Eager buffers");

static uint eager_buffer_size = (8 << 20); /* 8MB */
module_param(eager_buffer_size, uint, S_IRUGO);
MODULE_PARM_DESC(eager_buffer_size, "Size of the eager buffers, default: 8MB");

static uint rcvhdrcnt = 2048; /* 2x the max eager buffer count */
module_param_named(rcvhdrcnt, rcvhdrcnt, uint, S_IRUGO);
MODULE_PARM_DESC(rcvhdrcnt, "Receive header queue count (default 2048)");

static uint hfi1_hdrq_entsize = 32;
module_param_named(hdrq_entsize, hfi1_hdrq_entsize, uint, S_IRUGO);
MODULE_PARM_DESC(hdrq_entsize, "Size of header queue entries: 2 - 8B, 16 - 64B (default), 32 - 128B");

unsigned int user_credit_return_threshold = 33;	/* default is 33% */
module_param(user_credit_return_threshold, uint, S_IRUGO);
MODULE_PARM_DESC(user_credit_return_threshold, "Credit return threshold for user send contexts, return when unreturned credits passes this many blocks (in percent of allocated blocks, 0 is off)");

static inline u64 encode_rcv_header_entry_size(u16 size);

static struct idr hfi1_unit_table;

static int hfi1_create_kctxt(struct hfi1_devdata *dd,
			     struct hfi1_pportdata *ppd)
{
	struct hfi1_ctxtdata *rcd;
	int ret;

	/* Control context has to be always 0 */
	BUILD_BUG_ON(HFI1_CTRL_CTXT != 0);

	ret = hfi1_create_ctxtdata(ppd, dd->node, &rcd);
	if (ret < 0) {
		dd_dev_err(dd, "Kernel receive context allocation failed\n");
		return ret;
	}

	/*
	 * Set up the kernel context flags here and now because they use
	 * default values for all receive side memories.  User contexts will
	 * be handled as they are created.
	 */
	rcd->flags = HFI1_CAP_KGET(MULTI_PKT_EGR) |
		HFI1_CAP_KGET(NODROP_RHQ_FULL) |
		HFI1_CAP_KGET(NODROP_EGR_FULL) |
		HFI1_CAP_KGET(DMA_RTAIL);

	/* Control context must use DMA_RTAIL */
	if (rcd->ctxt == HFI1_CTRL_CTXT)
		rcd->flags |= HFI1_CAP_DMA_RTAIL;
	rcd->seq_cnt = 1;

	rcd->sc = sc_alloc(dd, SC_ACK, rcd->rcvhdrqentsize, dd->node);
	if (!rcd->sc) {
		dd_dev_err(dd, "Kernel send context allocation failed\n");
		return -ENOMEM;
	}
	hfi1_init_ctxt(rcd->sc);

	return 0;
}

/*
 * Create the receive context array and one or more kernel contexts
 */
int hfi1_create_kctxts(struct hfi1_devdata *dd)
{
	u16 i;
	int ret;

	dd->rcd = kcalloc_node(dd->num_rcv_contexts, sizeof(*dd->rcd),
			       GFP_KERNEL, dd->node);
	if (!dd->rcd)
		return -ENOMEM;

	for (i = 0; i < dd->first_dyn_alloc_ctxt; ++i) {
		ret = hfi1_create_kctxt(dd, dd->pport);
		if (ret)
			goto bail;
	}

	return 0;
bail:
	for (i = 0; dd->rcd && i < dd->first_dyn_alloc_ctxt; ++i)
		hfi1_free_ctxt(dd->rcd[i]);

	/* All the contexts should be freed, free the array */
	kfree(dd->rcd);
	dd->rcd = NULL;
	return ret;
}

/*
 * Helper routines for the receive context reference count (rcd and uctxt).
 */
static void hfi1_rcd_init(struct hfi1_ctxtdata *rcd)
{
	kref_init(&rcd->kref);
}

/**
 * hfi1_rcd_free - When reference is zero clean up.
 * @kref: pointer to an initialized rcd data structure
 *
 */
static void hfi1_rcd_free(struct kref *kref)
{
	unsigned long flags;
	struct hfi1_ctxtdata *rcd =
		container_of(kref, struct hfi1_ctxtdata, kref);

	hfi1_free_ctxtdata(rcd->dd, rcd);

	spin_lock_irqsave(&rcd->dd->uctxt_lock, flags);
	rcd->dd->rcd[rcd->ctxt] = NULL;
	spin_unlock_irqrestore(&rcd->dd->uctxt_lock, flags);

	kfree(rcd);
}

/**
 * hfi1_rcd_put - decrement reference for rcd
 * @rcd: pointer to an initialized rcd data structure
 *
 * Use this to put a reference after the init.
 */
int hfi1_rcd_put(struct hfi1_ctxtdata *rcd)
{
	if (rcd)
		return kref_put(&rcd->kref, hfi1_rcd_free);

	return 0;
}

/**
 * hfi1_rcd_get - increment reference for rcd
 * @rcd: pointer to an initialized rcd data structure
 *
 * Use this to get a reference after the init.
 */
void hfi1_rcd_get(struct hfi1_ctxtdata *rcd)
{
	kref_get(&rcd->kref);
}

/**
 * allocate_rcd_index - allocate an rcd index from the rcd array
 * @dd: pointer to a valid devdata structure
 * @rcd: rcd data structure to assign
 * @index: pointer to index that is allocated
 *
 * Find an empty index in the rcd array, and assign the given rcd to it.
 * If the array is full, we are EBUSY.
 *
 */
static int allocate_rcd_index(struct hfi1_devdata *dd,
			      struct hfi1_ctxtdata *rcd, u16 *index)
{
	unsigned long flags;
	u16 ctxt;

	spin_lock_irqsave(&dd->uctxt_lock, flags);
	for (ctxt = 0; ctxt < dd->num_rcv_contexts; ctxt++)
		if (!dd->rcd[ctxt])
			break;

	if (ctxt < dd->num_rcv_contexts) {
		rcd->ctxt = ctxt;
		dd->rcd[ctxt] = rcd;
		hfi1_rcd_init(rcd);
	}
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	if (ctxt >= dd->num_rcv_contexts)
		return -EBUSY;

	*index = ctxt;

	return 0;
}

/**
 * hfi1_rcd_get_by_index_safe - validate the ctxt index before accessing the
 * array
 * @dd: pointer to a valid devdata structure
 * @ctxt: the index of an possilbe rcd
 *
 * This is a wrapper for hfi1_rcd_get_by_index() to validate that the given
 * ctxt index is valid.
 *
 * The caller is responsible for making the _put().
 *
 */
struct hfi1_ctxtdata *hfi1_rcd_get_by_index_safe(struct hfi1_devdata *dd,
						 u16 ctxt)
{
	if (ctxt < dd->num_rcv_contexts)
		return hfi1_rcd_get_by_index(dd, ctxt);

	return NULL;
}

/**
 * hfi1_rcd_get_by_index
 * @dd: pointer to a valid devdata structure
 * @ctxt: the index of an possilbe rcd
 *
 * We need to protect access to the rcd array.  If access is needed to
 * one or more index, get the protecting spinlock and then increment the
 * kref.
 *
 * The caller is responsible for making the _put().
 *
 */
struct hfi1_ctxtdata *hfi1_rcd_get_by_index(struct hfi1_devdata *dd, u16 ctxt)
{
	unsigned long flags;
	struct hfi1_ctxtdata *rcd = NULL;

	spin_lock_irqsave(&dd->uctxt_lock, flags);
	if (dd->rcd[ctxt]) {
		rcd = dd->rcd[ctxt];
		hfi1_rcd_get(rcd);
	}
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	return rcd;
}

/*
 * Common code for user and kernel context create and setup.
 * NOTE: the initial kref is done here (hf1_rcd_init()).
 */
int hfi1_create_ctxtdata(struct hfi1_pportdata *ppd, int numa,
			 struct hfi1_ctxtdata **context)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct hfi1_ctxtdata *rcd;
	unsigned kctxt_ngroups = 0;
	u32 base;

	if (dd->rcv_entries.nctxt_extra >
	    dd->num_rcv_contexts - dd->first_dyn_alloc_ctxt)
		kctxt_ngroups = (dd->rcv_entries.nctxt_extra -
			 (dd->num_rcv_contexts - dd->first_dyn_alloc_ctxt));
	rcd = kzalloc_node(sizeof(*rcd), GFP_KERNEL, numa);
	if (rcd) {
		u32 rcvtids, max_entries;
		u16 ctxt;
		int ret;

		ret = allocate_rcd_index(dd, rcd, &ctxt);
		if (ret) {
			*context = NULL;
			kfree(rcd);
			return ret;
		}

		INIT_LIST_HEAD(&rcd->qp_wait_list);
		hfi1_exp_tid_group_init(&rcd->tid_group_list);
		hfi1_exp_tid_group_init(&rcd->tid_used_list);
		hfi1_exp_tid_group_init(&rcd->tid_full_list);
		rcd->ppd = ppd;
		rcd->dd = dd;
		__set_bit(0, rcd->in_use_ctxts);
		rcd->numa_id = numa;
		rcd->rcv_array_groups = dd->rcv_entries.ngroups;

		mutex_init(&rcd->exp_lock);

		hfi1_cdbg(PROC, "setting up context %u\n", rcd->ctxt);

		/*
		 * Calculate the context's RcvArray entry starting point.
		 * We do this here because we have to take into account all
		 * the RcvArray entries that previous context would have
		 * taken and we have to account for any extra groups assigned
		 * to the static (kernel) or dynamic (vnic/user) contexts.
		 */
		if (ctxt < dd->first_dyn_alloc_ctxt) {
			if (ctxt < kctxt_ngroups) {
				base = ctxt * (dd->rcv_entries.ngroups + 1);
				rcd->rcv_array_groups++;
			} else {
				base = kctxt_ngroups +
					(ctxt * dd->rcv_entries.ngroups);
			}
		} else {
			u16 ct = ctxt - dd->first_dyn_alloc_ctxt;

			base = ((dd->n_krcv_queues * dd->rcv_entries.ngroups) +
				kctxt_ngroups);
			if (ct < dd->rcv_entries.nctxt_extra) {
				base += ct * (dd->rcv_entries.ngroups + 1);
				rcd->rcv_array_groups++;
			} else {
				base += dd->rcv_entries.nctxt_extra +
					(ct * dd->rcv_entries.ngroups);
			}
		}
		rcd->eager_base = base * dd->rcv_entries.group_size;

		rcd->rcvhdrq_cnt = rcvhdrcnt;
		rcd->rcvhdrqentsize = hfi1_hdrq_entsize;
		/*
		 * Simple Eager buffer allocation: we have already pre-allocated
		 * the number of RcvArray entry groups. Each ctxtdata structure
		 * holds the number of groups for that context.
		 *
		 * To follow CSR requirements and maintain cacheline alignment,
		 * make sure all sizes and bases are multiples of group_size.
		 *
		 * The expected entry count is what is left after assigning
		 * eager.
		 */
		max_entries = rcd->rcv_array_groups *
			dd->rcv_entries.group_size;
		rcvtids = ((max_entries * hfi1_rcvarr_split) / 100);
		rcd->egrbufs.count = round_down(rcvtids,
						dd->rcv_entries.group_size);
		if (rcd->egrbufs.count > MAX_EAGER_ENTRIES) {
			dd_dev_err(dd, "ctxt%u: requested too many RcvArray entries.\n",
				   rcd->ctxt);
			rcd->egrbufs.count = MAX_EAGER_ENTRIES;
		}
		hfi1_cdbg(PROC,
			  "ctxt%u: max Eager buffer RcvArray entries: %u\n",
			  rcd->ctxt, rcd->egrbufs.count);

		/*
		 * Allocate array that will hold the eager buffer accounting
		 * data.
		 * This will allocate the maximum possible buffer count based
		 * on the value of the RcvArray split parameter.
		 * The resulting value will be rounded down to the closest
		 * multiple of dd->rcv_entries.group_size.
		 */
		rcd->egrbufs.buffers =
			kcalloc_node(rcd->egrbufs.count,
				     sizeof(*rcd->egrbufs.buffers),
				     GFP_KERNEL, numa);
		if (!rcd->egrbufs.buffers)
			goto bail;
		rcd->egrbufs.rcvtids =
			kcalloc_node(rcd->egrbufs.count,
				     sizeof(*rcd->egrbufs.rcvtids),
				     GFP_KERNEL, numa);
		if (!rcd->egrbufs.rcvtids)
			goto bail;
		rcd->egrbufs.size = eager_buffer_size;
		/*
		 * The size of the buffers programmed into the RcvArray
		 * entries needs to be big enough to handle the highest
		 * MTU supported.
		 */
		if (rcd->egrbufs.size < hfi1_max_mtu) {
			rcd->egrbufs.size = __roundup_pow_of_two(hfi1_max_mtu);
			hfi1_cdbg(PROC,
				  "ctxt%u: eager bufs size too small. Adjusting to %zu\n",
				    rcd->ctxt, rcd->egrbufs.size);
		}
		rcd->egrbufs.rcvtid_size = HFI1_MAX_EAGER_BUFFER_SIZE;

		/* Applicable only for statically created kernel contexts */
		if (ctxt < dd->first_dyn_alloc_ctxt) {
			rcd->opstats = kzalloc_node(sizeof(*rcd->opstats),
						    GFP_KERNEL, numa);
			if (!rcd->opstats)
				goto bail;
		}

		*context = rcd;
		return 0;
	}

bail:
	*context = NULL;
	hfi1_free_ctxt(rcd);
	return -ENOMEM;
}

/**
 * hfi1_free_ctxt
 * @rcd: pointer to an initialized rcd data structure
 *
 * This wrapper is the free function that matches hfi1_create_ctxtdata().
 * When a context is done being used (kernel or user), this function is called
 * for the "final" put to match the kref init from hf1i_create_ctxtdata().
 * Other users of the context do a get/put sequence to make sure that the
 * structure isn't removed while in use.
 */
void hfi1_free_ctxt(struct hfi1_ctxtdata *rcd)
{
	hfi1_rcd_put(rcd);
}

/*
 * Convert a receive header entry size that to the encoding used in the CSR.
 *
 * Return a zero if the given size is invalid.
 */
static inline u64 encode_rcv_header_entry_size(u16 size)
{
	/* there are only 3 valid receive header entry sizes */
	if (size == 2)
		return 1;
	if (size == 16)
		return 2;
	else if (size == 32)
		return 4;
	return 0; /* invalid */
}

/*
 * Select the largest ccti value over all SLs to determine the intra-
 * packet gap for the link.
 *
 * called with cca_timer_lock held (to protect access to cca_timer
 * array), and rcu_read_lock() (to protect access to cc_state).
 */
void set_link_ipg(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	struct cc_state *cc_state;
	int i;
	u16 cce, ccti_limit, max_ccti = 0;
	u16 shift, mult;
	u64 src;
	u32 current_egress_rate; /* Mbits /sec */
	u32 max_pkt_time;
	/*
	 * max_pkt_time is the maximum packet egress time in units
	 * of the fabric clock period 1/(805 MHz).
	 */

	cc_state = get_cc_state(ppd);

	if (!cc_state)
		/*
		 * This should _never_ happen - rcu_read_lock() is held,
		 * and set_link_ipg() should not be called if cc_state
		 * is NULL.
		 */
		return;

	for (i = 0; i < OPA_MAX_SLS; i++) {
		u16 ccti = ppd->cca_timer[i].ccti;

		if (ccti > max_ccti)
			max_ccti = ccti;
	}

	ccti_limit = cc_state->cct.ccti_limit;
	if (max_ccti > ccti_limit)
		max_ccti = ccti_limit;

	cce = cc_state->cct.entries[max_ccti].entry;
	shift = (cce & 0xc000) >> 14;
	mult = (cce & 0x3fff);

	current_egress_rate = active_egress_rate(ppd);

	max_pkt_time = egress_cycles(ppd->ibmaxlen, current_egress_rate);

	src = (max_pkt_time >> shift) * mult;

	src &= SEND_STATIC_RATE_CONTROL_CSR_SRC_RELOAD_SMASK;
	src <<= SEND_STATIC_RATE_CONTROL_CSR_SRC_RELOAD_SHIFT;

	write_csr(dd, SEND_STATIC_RATE_CONTROL, src);
}

static enum hrtimer_restart cca_timer_fn(struct hrtimer *t)
{
	struct cca_timer *cca_timer;
	struct hfi1_pportdata *ppd;
	int sl;
	u16 ccti_timer, ccti_min;
	struct cc_state *cc_state;
	unsigned long flags;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	cca_timer = container_of(t, struct cca_timer, hrtimer);
	ppd = cca_timer->ppd;
	sl = cca_timer->sl;

	rcu_read_lock();

	cc_state = get_cc_state(ppd);

	if (!cc_state) {
		rcu_read_unlock();
		return HRTIMER_NORESTART;
	}

	/*
	 * 1) decrement ccti for SL
	 * 2) calculate IPG for link (set_link_ipg())
	 * 3) restart timer, unless ccti is at min value
	 */

	ccti_min = cc_state->cong_setting.entries[sl].ccti_min;
	ccti_timer = cc_state->cong_setting.entries[sl].ccti_timer;

	spin_lock_irqsave(&ppd->cca_timer_lock, flags);

	if (cca_timer->ccti > ccti_min) {
		cca_timer->ccti--;
		set_link_ipg(ppd);
	}

	if (cca_timer->ccti > ccti_min) {
		unsigned long nsec = 1024 * ccti_timer;
		/* ccti_timer is in units of 1.024 usec */
		hrtimer_forward_now(t, ns_to_ktime(nsec));
		ret = HRTIMER_RESTART;
	}

	spin_unlock_irqrestore(&ppd->cca_timer_lock, flags);
	rcu_read_unlock();
	return ret;
}

/*
 * Common code for initializing the physical port structure.
 */
void hfi1_init_pportdata(struct pci_dev *pdev, struct hfi1_pportdata *ppd,
			 struct hfi1_devdata *dd, u8 hw_pidx, u8 port)
{
	int i;
	uint default_pkey_idx;
	struct cc_state *cc_state;

	ppd->dd = dd;
	ppd->hw_pidx = hw_pidx;
	ppd->port = port; /* IB port number, not index */
	ppd->prev_link_width = LINK_WIDTH_DEFAULT;
	/*
	 * There are C_VL_COUNT number of PortVLXmitWait counters.
	 * Adding 1 to C_VL_COUNT to include the PortXmitWait counter.
	 */
	for (i = 0; i < C_VL_COUNT + 1; i++) {
		ppd->port_vl_xmit_wait_last[i] = 0;
		ppd->vl_xmit_flit_cnt[i] = 0;
	}

	default_pkey_idx = 1;

	ppd->pkeys[default_pkey_idx] = DEFAULT_P_KEY;
	ppd->part_enforce |= HFI1_PART_ENFORCE_IN;

	if (loopback) {
		hfi1_early_err(&pdev->dev,
			       "Faking data partition 0x8001 in idx %u\n",
			       !default_pkey_idx);
		ppd->pkeys[!default_pkey_idx] = 0x8001;
	}

	INIT_WORK(&ppd->link_vc_work, handle_verify_cap);
	INIT_WORK(&ppd->link_up_work, handle_link_up);
	INIT_WORK(&ppd->link_down_work, handle_link_down);
	INIT_WORK(&ppd->freeze_work, handle_freeze);
	INIT_WORK(&ppd->link_downgrade_work, handle_link_downgrade);
	INIT_WORK(&ppd->sma_message_work, handle_sma_message);
	INIT_WORK(&ppd->link_bounce_work, handle_link_bounce);
	INIT_DELAYED_WORK(&ppd->start_link_work, handle_start_link);
	INIT_WORK(&ppd->linkstate_active_work, receive_interrupt_work);
	INIT_WORK(&ppd->qsfp_info.qsfp_work, qsfp_event);

	mutex_init(&ppd->hls_lock);
	spin_lock_init(&ppd->qsfp_info.qsfp_lock);

	ppd->qsfp_info.ppd = ppd;
	ppd->sm_trap_qp = 0x0;
	ppd->sa_qp = 0x1;

	ppd->hfi1_wq = NULL;

	spin_lock_init(&ppd->cca_timer_lock);

	for (i = 0; i < OPA_MAX_SLS; i++) {
		hrtimer_init(&ppd->cca_timer[i].hrtimer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		ppd->cca_timer[i].ppd = ppd;
		ppd->cca_timer[i].sl = i;
		ppd->cca_timer[i].ccti = 0;
		ppd->cca_timer[i].hrtimer.function = cca_timer_fn;
	}

	ppd->cc_max_table_entries = IB_CC_TABLE_CAP_DEFAULT;

	spin_lock_init(&ppd->cc_state_lock);
	spin_lock_init(&ppd->cc_log_lock);
	cc_state = kzalloc(sizeof(*cc_state), GFP_KERNEL);
	RCU_INIT_POINTER(ppd->cc_state, cc_state);
	if (!cc_state)
		goto bail;
	return;

bail:

	hfi1_early_err(&pdev->dev,
		       "Congestion Control Agent disabled for port %d\n", port);
}

/*
 * Do initialization for device that is only needed on
 * first detect, not on resets.
 */
static int loadtime_init(struct hfi1_devdata *dd)
{
	return 0;
}

/**
 * init_after_reset - re-initialize after a reset
 * @dd: the hfi1_ib device
 *
 * sanity check at least some of the values after reset, and
 * ensure no receive or transmit (explicitly, in case reset
 * failed
 */
static int init_after_reset(struct hfi1_devdata *dd)
{
	int i;
	struct hfi1_ctxtdata *rcd;
	/*
	 * Ensure chip does no sends or receives, tail updates, or
	 * pioavail updates while we re-initialize.  This is mostly
	 * for the driver data structures, not chip registers.
	 */
	for (i = 0; i < dd->num_rcv_contexts; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		hfi1_rcvctrl(dd, HFI1_RCVCTRL_CTXT_DIS |
			     HFI1_RCVCTRL_INTRAVAIL_DIS |
			     HFI1_RCVCTRL_TAILUPD_DIS, rcd);
		hfi1_rcd_put(rcd);
	}
	pio_send_control(dd, PSC_GLOBAL_DISABLE);
	for (i = 0; i < dd->num_send_contexts; i++)
		sc_disable(dd->send_contexts[i].sc);

	return 0;
}

static void enable_chip(struct hfi1_devdata *dd)
{
	struct hfi1_ctxtdata *rcd;
	u32 rcvmask;
	u16 i;

	/* enable PIO send */
	pio_send_control(dd, PSC_GLOBAL_ENABLE);

	/*
	 * Enable kernel ctxts' receive and receive interrupt.
	 * Other ctxts done as user opens and initializes them.
	 */
	for (i = 0; i < dd->first_dyn_alloc_ctxt; ++i) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		if (!rcd)
			continue;
		rcvmask = HFI1_RCVCTRL_CTXT_ENB | HFI1_RCVCTRL_INTRAVAIL_ENB;
		rcvmask |= HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL) ?
			HFI1_RCVCTRL_TAILUPD_ENB : HFI1_RCVCTRL_TAILUPD_DIS;
		if (!HFI1_CAP_KGET_MASK(rcd->flags, MULTI_PKT_EGR))
			rcvmask |= HFI1_RCVCTRL_ONE_PKT_EGR_ENB;
		if (HFI1_CAP_KGET_MASK(rcd->flags, NODROP_RHQ_FULL))
			rcvmask |= HFI1_RCVCTRL_NO_RHQ_DROP_ENB;
		if (HFI1_CAP_KGET_MASK(rcd->flags, NODROP_EGR_FULL))
			rcvmask |= HFI1_RCVCTRL_NO_EGR_DROP_ENB;
		hfi1_rcvctrl(dd, rcvmask, rcd);
		sc_enable(rcd->sc);
		hfi1_rcd_put(rcd);
	}
}

/**
 * create_workqueues - create per port workqueues
 * @dd: the hfi1_ib device
 */
static int create_workqueues(struct hfi1_devdata *dd)
{
	int pidx;
	struct hfi1_pportdata *ppd;

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		if (!ppd->hfi1_wq) {
			ppd->hfi1_wq =
				alloc_workqueue(
				    "hfi%d_%d",
				    WQ_SYSFS | WQ_HIGHPRI | WQ_CPU_INTENSIVE,
				    HFI1_MAX_ACTIVE_WORKQUEUE_ENTRIES,
				    dd->unit, pidx);
			if (!ppd->hfi1_wq)
				goto wq_error;
		}
		if (!ppd->link_wq) {
			/*
			 * Make the link workqueue single-threaded to enforce
			 * serialization.
			 */
			ppd->link_wq =
				alloc_workqueue(
				    "hfi_link_%d_%d",
				    WQ_SYSFS | WQ_MEM_RECLAIM | WQ_UNBOUND,
				    1, /* max_active */
				    dd->unit, pidx);
			if (!ppd->link_wq)
				goto wq_error;
		}
	}
	return 0;
wq_error:
	pr_err("alloc_workqueue failed for port %d\n", pidx + 1);
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		if (ppd->hfi1_wq) {
			destroy_workqueue(ppd->hfi1_wq);
			ppd->hfi1_wq = NULL;
		}
		if (ppd->link_wq) {
			destroy_workqueue(ppd->link_wq);
			ppd->link_wq = NULL;
		}
	}
	return -ENOMEM;
}

/**
 * hfi1_init - do the actual initialization sequence on the chip
 * @dd: the hfi1_ib device
 * @reinit: re-initializing, so don't allocate new memory
 *
 * Do the actual initialization sequence on the chip.  This is done
 * both from the init routine called from the PCI infrastructure, and
 * when we reset the chip, or detect that it was reset internally,
 * or it's administratively re-enabled.
 *
 * Memory allocation here and in called routines is only done in
 * the first case (reinit == 0).  We have to be careful, because even
 * without memory allocation, we need to re-write all the chip registers
 * TIDs, etc. after the reset or enable has completed.
 */
int hfi1_init(struct hfi1_devdata *dd, int reinit)
{
	int ret = 0, pidx, lastfail = 0;
	unsigned long len;
	u16 i;
	struct hfi1_ctxtdata *rcd;
	struct hfi1_pportdata *ppd;

	/* Set up recv low level handlers */
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_EXPECTED] =
						kdeth_process_expected;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_EAGER] =
						kdeth_process_eager;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_IB] = process_receive_ib;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_ERROR] =
						process_receive_error;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_BYPASS] =
						process_receive_bypass;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_INVALID5] =
						process_receive_invalid;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_INVALID6] =
						process_receive_invalid;
	dd->normal_rhf_rcv_functions[RHF_RCV_TYPE_INVALID7] =
						process_receive_invalid;
	dd->rhf_rcv_function_map = dd->normal_rhf_rcv_functions;

	/* Set up send low level handlers */
	dd->process_pio_send = hfi1_verbs_send_pio;
	dd->process_dma_send = hfi1_verbs_send_dma;
	dd->pio_inline_send = pio_copy;
	dd->process_vnic_dma_send = hfi1_vnic_send_dma;

	if (is_ax(dd)) {
		atomic_set(&dd->drop_packet, DROP_PACKET_ON);
		dd->do_drop = 1;
	} else {
		atomic_set(&dd->drop_packet, DROP_PACKET_OFF);
		dd->do_drop = 0;
	}

	/* make sure the link is not "up" */
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		ppd->linkup = 0;
	}

	if (reinit)
		ret = init_after_reset(dd);
	else
		ret = loadtime_init(dd);
	if (ret)
		goto done;

	/* allocate dummy tail memory for all receive contexts */
	dd->rcvhdrtail_dummy_kvaddr = dma_zalloc_coherent(
		&dd->pcidev->dev, sizeof(u64),
		&dd->rcvhdrtail_dummy_dma,
		GFP_KERNEL);

	if (!dd->rcvhdrtail_dummy_kvaddr) {
		dd_dev_err(dd, "cannot allocate dummy tail memory\n");
		ret = -ENOMEM;
		goto done;
	}

	/* dd->rcd can be NULL if early initialization failed */
	for (i = 0; dd->rcd && i < dd->first_dyn_alloc_ctxt; ++i) {
		/*
		 * Set up the (kernel) rcvhdr queue and egr TIDs.  If doing
		 * re-init, the simplest way to handle this is to free
		 * existing, and re-allocate.
		 * Need to re-create rest of ctxt 0 ctxtdata as well.
		 */
		rcd = hfi1_rcd_get_by_index(dd, i);
		if (!rcd)
			continue;

		rcd->do_interrupt = &handle_receive_interrupt;

		lastfail = hfi1_create_rcvhdrq(dd, rcd);
		if (!lastfail)
			lastfail = hfi1_setup_eagerbufs(rcd);
		if (lastfail) {
			dd_dev_err(dd,
				   "failed to allocate kernel ctxt's rcvhdrq and/or egr bufs\n");
			ret = lastfail;
		}
		hfi1_rcd_put(rcd);
	}

	/* Allocate enough memory for user event notification. */
	len = PAGE_ALIGN(dd->chip_rcv_contexts * HFI1_MAX_SHARED_CTXTS *
			 sizeof(*dd->events));
	dd->events = vmalloc_user(len);
	if (!dd->events)
		dd_dev_err(dd, "Failed to allocate user events page\n");
	/*
	 * Allocate a page for device and port status.
	 * Page will be shared amongst all user processes.
	 */
	dd->status = vmalloc_user(PAGE_SIZE);
	if (!dd->status)
		dd_dev_err(dd, "Failed to allocate dev status page\n");
	else
		dd->freezelen = PAGE_SIZE - (sizeof(*dd->status) -
					     sizeof(dd->status->freezemsg));
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		if (dd->status)
			/* Currently, we only have one port */
			ppd->statusp = &dd->status->port;

		set_mtu(ppd);
	}

	/* enable chip even if we have an error, so we can debug cause */
	enable_chip(dd);

done:
	/*
	 * Set status even if port serdes is not initialized
	 * so that diags will work.
	 */
	if (dd->status)
		dd->status->dev |= HFI1_STATUS_CHIP_PRESENT |
			HFI1_STATUS_INITTED;
	if (!ret) {
		/* enable all interrupts from the chip */
		set_intr_state(dd, 1);

		/* chip is OK for user apps; mark it as initialized */
		for (pidx = 0; pidx < dd->num_pports; ++pidx) {
			ppd = dd->pport + pidx;

			/*
			 * start the serdes - must be after interrupts are
			 * enabled so we are notified when the link goes up
			 */
			lastfail = bringup_serdes(ppd);
			if (lastfail)
				dd_dev_info(dd,
					    "Failed to bring up port %u\n",
					    ppd->port);

			/*
			 * Set status even if port serdes is not initialized
			 * so that diags will work.
			 */
			if (ppd->statusp)
				*ppd->statusp |= HFI1_STATUS_CHIP_PRESENT |
							HFI1_STATUS_INITTED;
			if (!ppd->link_speed_enabled)
				continue;
		}
	}

	/* if ret is non-zero, we probably should do some cleanup here... */
	return ret;
}

static inline struct hfi1_devdata *__hfi1_lookup(int unit)
{
	return idr_find(&hfi1_unit_table, unit);
}

struct hfi1_devdata *hfi1_lookup(int unit)
{
	struct hfi1_devdata *dd;
	unsigned long flags;

	spin_lock_irqsave(&hfi1_devs_lock, flags);
	dd = __hfi1_lookup(unit);
	spin_unlock_irqrestore(&hfi1_devs_lock, flags);

	return dd;
}

/*
 * Stop the timers during unit shutdown, or after an error late
 * in initialization.
 */
static void stop_timers(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd;
	int pidx;

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		if (ppd->led_override_timer.function) {
			del_timer_sync(&ppd->led_override_timer);
			atomic_set(&ppd->led_override_timer_active, 0);
		}
	}
}

/**
 * shutdown_device - shut down a device
 * @dd: the hfi1_ib device
 *
 * This is called to make the device quiet when we are about to
 * unload the driver, and also when the device is administratively
 * disabled.   It does not free any data structures.
 * Everything it does has to be setup again by hfi1_init(dd, 1)
 */
static void shutdown_device(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd;
	struct hfi1_ctxtdata *rcd;
	unsigned pidx;
	int i;

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;

		ppd->linkup = 0;
		if (ppd->statusp)
			*ppd->statusp &= ~(HFI1_STATUS_IB_CONF |
					   HFI1_STATUS_IB_READY);
	}
	dd->flags &= ~HFI1_INITTED;

	/* mask and clean up interrupts, but not errors */
	set_intr_state(dd, 0);
	hfi1_clean_up_interrupts(dd);

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;
		for (i = 0; i < dd->num_rcv_contexts; i++) {
			rcd = hfi1_rcd_get_by_index(dd, i);
			hfi1_rcvctrl(dd, HFI1_RCVCTRL_TAILUPD_DIS |
				     HFI1_RCVCTRL_CTXT_DIS |
				     HFI1_RCVCTRL_INTRAVAIL_DIS |
				     HFI1_RCVCTRL_PKEY_DIS |
				     HFI1_RCVCTRL_ONE_PKT_EGR_DIS, rcd);
			hfi1_rcd_put(rcd);
		}
		/*
		 * Gracefully stop all sends allowing any in progress to
		 * trickle out first.
		 */
		for (i = 0; i < dd->num_send_contexts; i++)
			sc_flush(dd->send_contexts[i].sc);
	}

	/*
	 * Enough for anything that's going to trickle out to have actually
	 * done so.
	 */
	udelay(20);

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;

		/* disable all contexts */
		for (i = 0; i < dd->num_send_contexts; i++)
			sc_disable(dd->send_contexts[i].sc);
		/* disable the send device */
		pio_send_control(dd, PSC_GLOBAL_DISABLE);

		shutdown_led_override(ppd);

		/*
		 * Clear SerdesEnable.
		 * We can't count on interrupts since we are stopping.
		 */
		hfi1_quiet_serdes(ppd);

		if (ppd->hfi1_wq) {
			destroy_workqueue(ppd->hfi1_wq);
			ppd->hfi1_wq = NULL;
		}
		if (ppd->link_wq) {
			destroy_workqueue(ppd->link_wq);
			ppd->link_wq = NULL;
		}
	}
	sdma_exit(dd);
}

/**
 * hfi1_free_ctxtdata - free a context's allocated data
 * @dd: the hfi1_ib device
 * @rcd: the ctxtdata structure
 *
 * free up any allocated data for a context
 * It should never change any chip state, or global driver state.
 */
void hfi1_free_ctxtdata(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd)
{
	u32 e;

	if (!rcd)
		return;

	if (rcd->rcvhdrq) {
		dma_free_coherent(&dd->pcidev->dev, rcd->rcvhdrq_size,
				  rcd->rcvhdrq, rcd->rcvhdrq_dma);
		rcd->rcvhdrq = NULL;
		if (rcd->rcvhdrtail_kvaddr) {
			dma_free_coherent(&dd->pcidev->dev, PAGE_SIZE,
					  (void *)rcd->rcvhdrtail_kvaddr,
					  rcd->rcvhdrqtailaddr_dma);
			rcd->rcvhdrtail_kvaddr = NULL;
		}
	}

	/* all the RcvArray entries should have been cleared by now */
	kfree(rcd->egrbufs.rcvtids);
	rcd->egrbufs.rcvtids = NULL;

	for (e = 0; e < rcd->egrbufs.alloced; e++) {
		if (rcd->egrbufs.buffers[e].dma)
			dma_free_coherent(&dd->pcidev->dev,
					  rcd->egrbufs.buffers[e].len,
					  rcd->egrbufs.buffers[e].addr,
					  rcd->egrbufs.buffers[e].dma);
	}
	kfree(rcd->egrbufs.buffers);
	rcd->egrbufs.alloced = 0;
	rcd->egrbufs.buffers = NULL;

	sc_free(rcd->sc);
	rcd->sc = NULL;

	vfree(rcd->subctxt_uregbase);
	vfree(rcd->subctxt_rcvegrbuf);
	vfree(rcd->subctxt_rcvhdr_base);
	kfree(rcd->opstats);

	rcd->subctxt_uregbase = NULL;
	rcd->subctxt_rcvegrbuf = NULL;
	rcd->subctxt_rcvhdr_base = NULL;
	rcd->opstats = NULL;
}

/*
 * Release our hold on the shared asic data.  If we are the last one,
 * return the structure to be finalized outside the lock.  Must be
 * holding hfi1_devs_lock.
 */
static struct hfi1_asic_data *release_asic_data(struct hfi1_devdata *dd)
{
	struct hfi1_asic_data *ad;
	int other;

	if (!dd->asic_data)
		return NULL;
	dd->asic_data->dds[dd->hfi1_id] = NULL;
	other = dd->hfi1_id ? 0 : 1;
	ad = dd->asic_data;
	dd->asic_data = NULL;
	/* return NULL if the other dd still has a link */
	return ad->dds[other] ? NULL : ad;
}

static void finalize_asic_data(struct hfi1_devdata *dd,
			       struct hfi1_asic_data *ad)
{
	clean_up_i2c(dd, ad);
	kfree(ad);
}

static void __hfi1_free_devdata(struct kobject *kobj)
{
	struct hfi1_devdata *dd =
		container_of(kobj, struct hfi1_devdata, kobj);
	struct hfi1_asic_data *ad;
	unsigned long flags;

	spin_lock_irqsave(&hfi1_devs_lock, flags);
	idr_remove(&hfi1_unit_table, dd->unit);
	list_del(&dd->list);
	ad = release_asic_data(dd);
	spin_unlock_irqrestore(&hfi1_devs_lock, flags);
	if (ad)
		finalize_asic_data(dd, ad);
	free_platform_config(dd);
	rcu_barrier(); /* wait for rcu callbacks to complete */
	free_percpu(dd->int_counter);
	free_percpu(dd->rcv_limit);
	free_percpu(dd->send_schedule);
	free_percpu(dd->tx_opstats);
	sdma_clean(dd, dd->num_sdma);
	rvt_dealloc_device(&dd->verbs_dev.rdi);
}

static struct kobj_type hfi1_devdata_type = {
	.release = __hfi1_free_devdata,
};

void hfi1_free_devdata(struct hfi1_devdata *dd)
{
	kobject_put(&dd->kobj);
}

/*
 * Allocate our primary per-unit data structure.  Must be done via verbs
 * allocator, because the verbs cleanup process both does cleanup and
 * free of the data structure.
 * "extra" is for chip-specific data.
 *
 * Use the idr mechanism to get a unit number for this unit.
 */
struct hfi1_devdata *hfi1_alloc_devdata(struct pci_dev *pdev, size_t extra)
{
	unsigned long flags;
	struct hfi1_devdata *dd;
	int ret, nports;

	/* extra is * number of ports */
	nports = extra / sizeof(struct hfi1_pportdata);

	dd = (struct hfi1_devdata *)rvt_alloc_device(sizeof(*dd) + extra,
						     nports);
	if (!dd)
		return ERR_PTR(-ENOMEM);
	dd->num_pports = nports;
	dd->pport = (struct hfi1_pportdata *)(dd + 1);
	dd->pcidev = pdev;
	pci_set_drvdata(pdev, dd);

	INIT_LIST_HEAD(&dd->list);
	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(&hfi1_devs_lock, flags);

	ret = idr_alloc(&hfi1_unit_table, dd, 0, 0, GFP_NOWAIT);
	if (ret >= 0) {
		dd->unit = ret;
		list_add(&dd->list, &hfi1_dev_list);
	}

	spin_unlock_irqrestore(&hfi1_devs_lock, flags);
	idr_preload_end();

	if (ret < 0) {
		hfi1_early_err(&pdev->dev,
			       "Could not allocate unit ID: error %d\n", -ret);
		goto bail;
	}
	rvt_set_ibdev_name(&dd->verbs_dev.rdi, "%s_%d", class_name(), dd->unit);

	/*
	 * Initialize all locks for the device. This needs to be as early as
	 * possible so locks are usable.
	 */
	spin_lock_init(&dd->sc_lock);
	spin_lock_init(&dd->sendctrl_lock);
	spin_lock_init(&dd->rcvctrl_lock);
	spin_lock_init(&dd->uctxt_lock);
	spin_lock_init(&dd->hfi1_diag_trans_lock);
	spin_lock_init(&dd->sc_init_lock);
	spin_lock_init(&dd->dc8051_memlock);
	seqlock_init(&dd->sc2vl_lock);
	spin_lock_init(&dd->sde_map_lock);
	spin_lock_init(&dd->pio_map_lock);
	mutex_init(&dd->dc8051_lock);
	init_waitqueue_head(&dd->event_queue);

	dd->int_counter = alloc_percpu(u64);
	if (!dd->int_counter) {
		ret = -ENOMEM;
		goto bail;
	}

	dd->rcv_limit = alloc_percpu(u64);
	if (!dd->rcv_limit) {
		ret = -ENOMEM;
		goto bail;
	}

	dd->send_schedule = alloc_percpu(u64);
	if (!dd->send_schedule) {
		ret = -ENOMEM;
		goto bail;
	}

	dd->tx_opstats = alloc_percpu(struct hfi1_opcode_stats_perctx);
	if (!dd->tx_opstats) {
		ret = -ENOMEM;
		goto bail;
	}

	kobject_init(&dd->kobj, &hfi1_devdata_type);
	return dd;

bail:
	if (!list_empty(&dd->list))
		list_del_init(&dd->list);
	rvt_dealloc_device(&dd->verbs_dev.rdi);
	return ERR_PTR(ret);
}

/*
 * Called from freeze mode handlers, and from PCI error
 * reporting code.  Should be paranoid about state of
 * system and data structures.
 */
void hfi1_disable_after_error(struct hfi1_devdata *dd)
{
	if (dd->flags & HFI1_INITTED) {
		u32 pidx;

		dd->flags &= ~HFI1_INITTED;
		if (dd->pport)
			for (pidx = 0; pidx < dd->num_pports; ++pidx) {
				struct hfi1_pportdata *ppd;

				ppd = dd->pport + pidx;
				if (dd->flags & HFI1_PRESENT)
					set_link_state(ppd, HLS_DN_DISABLE);

				if (ppd->statusp)
					*ppd->statusp &= ~HFI1_STATUS_IB_READY;
			}
	}

	/*
	 * Mark as having had an error for driver, and also
	 * for /sys and status word mapped to user programs.
	 * This marks unit as not usable, until reset.
	 */
	if (dd->status)
		dd->status->dev |= HFI1_STATUS_HWERROR;
}

static void remove_one(struct pci_dev *);
static int init_one(struct pci_dev *, const struct pci_device_id *);

#define DRIVER_LOAD_MSG "Intel " DRIVER_NAME " loaded: "
#define PFX DRIVER_NAME ": "

const struct pci_device_id hfi1_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL1) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, hfi1_pci_tbl);

static struct pci_driver hfi1_pci_driver = {
	.name = DRIVER_NAME,
	.probe = init_one,
	.remove = remove_one,
	.id_table = hfi1_pci_tbl,
	.err_handler = &hfi1_pci_err_handler,
};

static void __init compute_krcvqs(void)
{
	int i;

	for (i = 0; i < krcvqsset; i++)
		n_krcvqs += krcvqs[i];
}

/*
 * Do all the generic driver unit- and chip-independent memory
 * allocation and initialization.
 */
static int __init hfi1_mod_init(void)
{
	int ret;

	ret = dev_init();
	if (ret)
		goto bail;

	ret = node_affinity_init();
	if (ret)
		goto bail;

	/* validate max MTU before any devices start */
	if (!valid_opa_max_mtu(hfi1_max_mtu)) {
		pr_err("Invalid max_mtu 0x%x, using 0x%x instead\n",
		       hfi1_max_mtu, HFI1_DEFAULT_MAX_MTU);
		hfi1_max_mtu = HFI1_DEFAULT_MAX_MTU;
	}
	/* valid CUs run from 1-128 in powers of 2 */
	if (hfi1_cu > 128 || !is_power_of_2(hfi1_cu))
		hfi1_cu = 1;
	/* valid credit return threshold is 0-100, variable is unsigned */
	if (user_credit_return_threshold > 100)
		user_credit_return_threshold = 100;

	compute_krcvqs();
	/*
	 * sanitize receive interrupt count, time must wait until after
	 * the hardware type is known
	 */
	if (rcv_intr_count > RCV_HDR_HEAD_COUNTER_MASK)
		rcv_intr_count = RCV_HDR_HEAD_COUNTER_MASK;
	/* reject invalid combinations */
	if (rcv_intr_count == 0 && rcv_intr_timeout == 0) {
		pr_err("Invalid mode: both receive interrupt count and available timeout are zero - setting interrupt count to 1\n");
		rcv_intr_count = 1;
	}
	if (rcv_intr_count > 1 && rcv_intr_timeout == 0) {
		/*
		 * Avoid indefinite packet delivery by requiring a timeout
		 * if count is > 1.
		 */
		pr_err("Invalid mode: receive interrupt count greater than 1 and available timeout is zero - setting available timeout to 1\n");
		rcv_intr_timeout = 1;
	}
	if (rcv_intr_dynamic && !(rcv_intr_count > 1 && rcv_intr_timeout > 0)) {
		/*
		 * The dynamic algorithm expects a non-zero timeout
		 * and a count > 1.
		 */
		pr_err("Invalid mode: dynamic receive interrupt mitigation with invalid count and timeout - turning dynamic off\n");
		rcv_intr_dynamic = 0;
	}

	/* sanitize link CRC options */
	link_crc_mask &= SUPPORTED_CRCS;

	/*
	 * These must be called before the driver is registered with
	 * the PCI subsystem.
	 */
	idr_init(&hfi1_unit_table);

	hfi1_dbg_init();
	ret = hfi1_wss_init();
	if (ret < 0)
		goto bail_wss;
	ret = pci_register_driver(&hfi1_pci_driver);
	if (ret < 0) {
		pr_err("Unable to register driver: error %d\n", -ret);
		goto bail_dev;
	}
	goto bail; /* all OK */

bail_dev:
	hfi1_wss_exit();
bail_wss:
	hfi1_dbg_exit();
	idr_destroy(&hfi1_unit_table);
	dev_cleanup();
bail:
	return ret;
}

module_init(hfi1_mod_init);

/*
 * Do the non-unit driver cleanup, memory free, etc. at unload.
 */
static void __exit hfi1_mod_cleanup(void)
{
	pci_unregister_driver(&hfi1_pci_driver);
	node_affinity_destroy();
	hfi1_wss_exit();
	hfi1_dbg_exit();

	idr_destroy(&hfi1_unit_table);
	dispose_firmware();	/* asymmetric with obtain_firmware() */
	dev_cleanup();
}

module_exit(hfi1_mod_cleanup);

/* this can only be called after a successful initialization */
static void cleanup_device_data(struct hfi1_devdata *dd)
{
	int ctxt;
	int pidx;

	/* users can't do anything more with chip */
	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		struct hfi1_pportdata *ppd = &dd->pport[pidx];
		struct cc_state *cc_state;
		int i;

		if (ppd->statusp)
			*ppd->statusp &= ~HFI1_STATUS_CHIP_PRESENT;

		for (i = 0; i < OPA_MAX_SLS; i++)
			hrtimer_cancel(&ppd->cca_timer[i].hrtimer);

		spin_lock(&ppd->cc_state_lock);
		cc_state = get_cc_state_protected(ppd);
		RCU_INIT_POINTER(ppd->cc_state, NULL);
		spin_unlock(&ppd->cc_state_lock);

		if (cc_state)
			kfree_rcu(cc_state, rcu);
	}

	free_credit_return(dd);

	if (dd->rcvhdrtail_dummy_kvaddr) {
		dma_free_coherent(&dd->pcidev->dev, sizeof(u64),
				  (void *)dd->rcvhdrtail_dummy_kvaddr,
				  dd->rcvhdrtail_dummy_dma);
		dd->rcvhdrtail_dummy_kvaddr = NULL;
	}

	/*
	 * Free any resources still in use (usually just kernel contexts)
	 * at unload; we do for ctxtcnt, because that's what we allocate.
	 */
	for (ctxt = 0; dd->rcd && ctxt < dd->num_rcv_contexts; ctxt++) {
		struct hfi1_ctxtdata *rcd = dd->rcd[ctxt];

		if (rcd) {
			hfi1_clear_tids(rcd);
			hfi1_free_ctxt(rcd);
		}
	}

	kfree(dd->rcd);
	dd->rcd = NULL;

	free_pio_map(dd);
	/* must follow rcv context free - need to remove rcv's hooks */
	for (ctxt = 0; ctxt < dd->num_send_contexts; ctxt++)
		sc_free(dd->send_contexts[ctxt].sc);
	dd->num_send_contexts = 0;
	kfree(dd->send_contexts);
	dd->send_contexts = NULL;
	kfree(dd->hw_to_sw);
	dd->hw_to_sw = NULL;
	kfree(dd->boardname);
	vfree(dd->events);
	vfree(dd->status);
}

/*
 * Clean up on unit shutdown, or error during unit load after
 * successful initialization.
 */
static void postinit_cleanup(struct hfi1_devdata *dd)
{
	hfi1_start_cleanup(dd);

	hfi1_pcie_ddcleanup(dd);
	hfi1_pcie_cleanup(dd->pcidev);

	cleanup_device_data(dd);

	hfi1_free_devdata(dd);
}

static int init_validate_rcvhdrcnt(struct device *dev, uint thecnt)
{
	if (thecnt <= HFI1_MIN_HDRQ_EGRBUF_CNT) {
		hfi1_early_err(dev, "Receive header queue count too small\n");
		return -EINVAL;
	}

	if (thecnt > HFI1_MAX_HDRQ_EGRBUF_CNT) {
		hfi1_early_err(dev,
			       "Receive header queue count cannot be greater than %u\n",
			       HFI1_MAX_HDRQ_EGRBUF_CNT);
		return -EINVAL;
	}

	if (thecnt % HDRQ_INCREMENT) {
		hfi1_early_err(dev, "Receive header queue count %d must be divisible by %lu\n",
			       thecnt, HDRQ_INCREMENT);
		return -EINVAL;
	}

	return 0;
}

static int init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret = 0, j, pidx, initfail;
	struct hfi1_devdata *dd;
	struct hfi1_pportdata *ppd;

	/* First, lock the non-writable module parameters */
	HFI1_CAP_LOCK();

	/* Validate dev ids */
	if (!(ent->device == PCI_DEVICE_ID_INTEL0 ||
	      ent->device == PCI_DEVICE_ID_INTEL1)) {
		hfi1_early_err(&pdev->dev,
			       "Failing on unknown Intel deviceid 0x%x\n",
			       ent->device);
		ret = -ENODEV;
		goto bail;
	}

	/* Validate some global module parameters */
	ret = init_validate_rcvhdrcnt(&pdev->dev, rcvhdrcnt);
	if (ret)
		goto bail;

	/* use the encoding function as a sanitization check */
	if (!encode_rcv_header_entry_size(hfi1_hdrq_entsize)) {
		hfi1_early_err(&pdev->dev, "Invalid HdrQ Entry size %u\n",
			       hfi1_hdrq_entsize);
		ret = -EINVAL;
		goto bail;
	}

	/* The receive eager buffer size must be set before the receive
	 * contexts are created.
	 *
	 * Set the eager buffer size.  Validate that it falls in a range
	 * allowed by the hardware - all powers of 2 between the min and
	 * max.  The maximum valid MTU is within the eager buffer range
	 * so we do not need to cap the max_mtu by an eager buffer size
	 * setting.
	 */
	if (eager_buffer_size) {
		if (!is_power_of_2(eager_buffer_size))
			eager_buffer_size =
				roundup_pow_of_two(eager_buffer_size);
		eager_buffer_size =
			clamp_val(eager_buffer_size,
				  MIN_EAGER_BUFFER * 8,
				  MAX_EAGER_BUFFER_TOTAL);
		hfi1_early_info(&pdev->dev, "Eager buffer size %u\n",
				eager_buffer_size);
	} else {
		hfi1_early_err(&pdev->dev, "Invalid Eager buffer size of 0\n");
		ret = -EINVAL;
		goto bail;
	}

	/* restrict value of hfi1_rcvarr_split */
	hfi1_rcvarr_split = clamp_val(hfi1_rcvarr_split, 0, 100);

	ret = hfi1_pcie_init(pdev, ent);
	if (ret)
		goto bail;

	/*
	 * Do device-specific initialization, function table setup, dd
	 * allocation, etc.
	 */
	dd = hfi1_init_dd(pdev, ent);

	if (IS_ERR(dd)) {
		ret = PTR_ERR(dd);
		goto clean_bail; /* error already printed */
	}

	ret = create_workqueues(dd);
	if (ret)
		goto clean_bail;

	/* do the generic initialization */
	initfail = hfi1_init(dd, 0);

	/* setup vnic */
	hfi1_vnic_setup(dd);

	ret = hfi1_register_ib_device(dd);

	/*
	 * Now ready for use.  this should be cleared whenever we
	 * detect a reset, or initiate one.  If earlier failure,
	 * we still create devices, so diags, etc. can be used
	 * to determine cause of problem.
	 */
	if (!initfail && !ret) {
		dd->flags |= HFI1_INITTED;
		/* create debufs files after init and ib register */
		hfi1_dbg_ibdev_init(&dd->verbs_dev);
	}

	j = hfi1_device_create(dd);
	if (j)
		dd_dev_err(dd, "Failed to create /dev devices: %d\n", -j);

	if (initfail || ret) {
		hfi1_clean_up_interrupts(dd);
		stop_timers(dd);
		flush_workqueue(ib_wq);
		for (pidx = 0; pidx < dd->num_pports; ++pidx) {
			hfi1_quiet_serdes(dd->pport + pidx);
			ppd = dd->pport + pidx;
			if (ppd->hfi1_wq) {
				destroy_workqueue(ppd->hfi1_wq);
				ppd->hfi1_wq = NULL;
			}
			if (ppd->link_wq) {
				destroy_workqueue(ppd->link_wq);
				ppd->link_wq = NULL;
			}
		}
		if (!j)
			hfi1_device_remove(dd);
		if (!ret)
			hfi1_unregister_ib_device(dd);
		hfi1_vnic_cleanup(dd);
		postinit_cleanup(dd);
		if (initfail)
			ret = initfail;
		goto bail;	/* everything already cleaned */
	}

	sdma_start(dd);

	return 0;

clean_bail:
	hfi1_pcie_cleanup(pdev);
bail:
	return ret;
}

static void wait_for_clients(struct hfi1_devdata *dd)
{
	/*
	 * Remove the device init value and complete the device if there is
	 * no clients or wait for active clients to finish.
	 */
	if (atomic_dec_and_test(&dd->user_refcount))
		complete(&dd->user_comp);

	wait_for_completion(&dd->user_comp);
}

static void remove_one(struct pci_dev *pdev)
{
	struct hfi1_devdata *dd = pci_get_drvdata(pdev);

	/* close debugfs files before ib unregister */
	hfi1_dbg_ibdev_exit(&dd->verbs_dev);

	/* remove the /dev hfi1 interface */
	hfi1_device_remove(dd);

	/* wait for existing user space clients to finish */
	wait_for_clients(dd);

	/* unregister from IB core */
	hfi1_unregister_ib_device(dd);

	/* cleanup vnic */
	hfi1_vnic_cleanup(dd);

	/*
	 * Disable the IB link, disable interrupts on the device,
	 * clear dma engines, etc.
	 */
	shutdown_device(dd);

	stop_timers(dd);

	/* wait until all of our (qsfp) queue_work() calls complete */
	flush_workqueue(ib_wq);

	postinit_cleanup(dd);
}

/**
 * hfi1_create_rcvhdrq - create a receive header queue
 * @dd: the hfi1_ib device
 * @rcd: the context data
 *
 * This must be contiguous memory (from an i/o perspective), and must be
 * DMA'able (which means for some systems, it will go through an IOMMU,
 * or be forced into a low address range).
 */
int hfi1_create_rcvhdrq(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd)
{
	unsigned amt;
	u64 reg;

	if (!rcd->rcvhdrq) {
		dma_addr_t dma_hdrqtail;
		gfp_t gfp_flags;

		/*
		 * rcvhdrqentsize is in DWs, so we have to convert to bytes
		 * (* sizeof(u32)).
		 */
		amt = PAGE_ALIGN(rcd->rcvhdrq_cnt * rcd->rcvhdrqentsize *
				 sizeof(u32));

		if (rcd->ctxt < dd->first_dyn_alloc_ctxt || rcd->is_vnic)
			gfp_flags = GFP_KERNEL;
		else
			gfp_flags = GFP_USER;
		rcd->rcvhdrq = dma_zalloc_coherent(
			&dd->pcidev->dev, amt, &rcd->rcvhdrq_dma,
			gfp_flags | __GFP_COMP);

		if (!rcd->rcvhdrq) {
			dd_dev_err(dd,
				   "attempt to allocate %d bytes for ctxt %u rcvhdrq failed\n",
				   amt, rcd->ctxt);
			goto bail;
		}

		if (HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL)) {
			rcd->rcvhdrtail_kvaddr = dma_zalloc_coherent(
				&dd->pcidev->dev, PAGE_SIZE, &dma_hdrqtail,
				gfp_flags);
			if (!rcd->rcvhdrtail_kvaddr)
				goto bail_free;
			rcd->rcvhdrqtailaddr_dma = dma_hdrqtail;
		}

		rcd->rcvhdrq_size = amt;
	}
	/*
	 * These values are per-context:
	 *	RcvHdrCnt
	 *	RcvHdrEntSize
	 *	RcvHdrSize
	 */
	reg = ((u64)(rcd->rcvhdrq_cnt >> HDRQ_SIZE_SHIFT)
			& RCV_HDR_CNT_CNT_MASK)
		<< RCV_HDR_CNT_CNT_SHIFT;
	write_kctxt_csr(dd, rcd->ctxt, RCV_HDR_CNT, reg);
	reg = (encode_rcv_header_entry_size(rcd->rcvhdrqentsize)
			& RCV_HDR_ENT_SIZE_ENT_SIZE_MASK)
		<< RCV_HDR_ENT_SIZE_ENT_SIZE_SHIFT;
	write_kctxt_csr(dd, rcd->ctxt, RCV_HDR_ENT_SIZE, reg);
	reg = (dd->rcvhdrsize & RCV_HDR_SIZE_HDR_SIZE_MASK)
		<< RCV_HDR_SIZE_HDR_SIZE_SHIFT;
	write_kctxt_csr(dd, rcd->ctxt, RCV_HDR_SIZE, reg);

	/*
	 * Program dummy tail address for every receive context
	 * before enabling any receive context
	 */
	write_kctxt_csr(dd, rcd->ctxt, RCV_HDR_TAIL_ADDR,
			dd->rcvhdrtail_dummy_dma);

	return 0;

bail_free:
	dd_dev_err(dd,
		   "attempt to allocate 1 page for ctxt %u rcvhdrqtailaddr failed\n",
		   rcd->ctxt);
	dma_free_coherent(&dd->pcidev->dev, amt, rcd->rcvhdrq,
			  rcd->rcvhdrq_dma);
	rcd->rcvhdrq = NULL;
bail:
	return -ENOMEM;
}

/**
 * allocate eager buffers, both kernel and user contexts.
 * @rcd: the context we are setting up.
 *
 * Allocate the eager TID buffers and program them into hip.
 * They are no longer completely contiguous, we do multiple allocation
 * calls.  Otherwise we get the OOM code involved, by asking for too
 * much per call, with disastrous results on some kernels.
 */
int hfi1_setup_eagerbufs(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 max_entries, egrtop, alloced_bytes = 0, idx = 0;
	gfp_t gfp_flags;
	u16 order;
	int ret = 0;
	u16 round_mtu = roundup_pow_of_two(hfi1_max_mtu);

	/*
	 * GFP_USER, but without GFP_FS, so buffer cache can be
	 * coalesced (we hope); otherwise, even at order 4,
	 * heavy filesystem activity makes these fail, and we can
	 * use compound pages.
	 */
	gfp_flags = __GFP_RECLAIM | __GFP_IO | __GFP_COMP;

	/*
	 * The minimum size of the eager buffers is a groups of MTU-sized
	 * buffers.
	 * The global eager_buffer_size parameter is checked against the
	 * theoretical lower limit of the value. Here, we check against the
	 * MTU.
	 */
	if (rcd->egrbufs.size < (round_mtu * dd->rcv_entries.group_size))
		rcd->egrbufs.size = round_mtu * dd->rcv_entries.group_size;
	/*
	 * If using one-pkt-per-egr-buffer, lower the eager buffer
	 * size to the max MTU (page-aligned).
	 */
	if (!HFI1_CAP_KGET_MASK(rcd->flags, MULTI_PKT_EGR))
		rcd->egrbufs.rcvtid_size = round_mtu;

	/*
	 * Eager buffers sizes of 1MB or less require smaller TID sizes
	 * to satisfy the "multiple of 8 RcvArray entries" requirement.
	 */
	if (rcd->egrbufs.size <= (1 << 20))
		rcd->egrbufs.rcvtid_size = max((unsigned long)round_mtu,
			rounddown_pow_of_two(rcd->egrbufs.size / 8));

	while (alloced_bytes < rcd->egrbufs.size &&
	       rcd->egrbufs.alloced < rcd->egrbufs.count) {
		rcd->egrbufs.buffers[idx].addr =
			dma_zalloc_coherent(&dd->pcidev->dev,
					    rcd->egrbufs.rcvtid_size,
					    &rcd->egrbufs.buffers[idx].dma,
					    gfp_flags);
		if (rcd->egrbufs.buffers[idx].addr) {
			rcd->egrbufs.buffers[idx].len =
				rcd->egrbufs.rcvtid_size;
			rcd->egrbufs.rcvtids[rcd->egrbufs.alloced].addr =
				rcd->egrbufs.buffers[idx].addr;
			rcd->egrbufs.rcvtids[rcd->egrbufs.alloced].dma =
				rcd->egrbufs.buffers[idx].dma;
			rcd->egrbufs.alloced++;
			alloced_bytes += rcd->egrbufs.rcvtid_size;
			idx++;
		} else {
			u32 new_size, i, j;
			u64 offset = 0;

			/*
			 * Fail the eager buffer allocation if:
			 *   - we are already using the lowest acceptable size
			 *   - we are using one-pkt-per-egr-buffer (this implies
			 *     that we are accepting only one size)
			 */
			if (rcd->egrbufs.rcvtid_size == round_mtu ||
			    !HFI1_CAP_KGET_MASK(rcd->flags, MULTI_PKT_EGR)) {
				dd_dev_err(dd, "ctxt%u: Failed to allocate eager buffers\n",
					   rcd->ctxt);
				ret = -ENOMEM;
				goto bail_rcvegrbuf_phys;
			}

			new_size = rcd->egrbufs.rcvtid_size / 2;

			/*
			 * If the first attempt to allocate memory failed, don't
			 * fail everything but continue with the next lower
			 * size.
			 */
			if (idx == 0) {
				rcd->egrbufs.rcvtid_size = new_size;
				continue;
			}

			/*
			 * Re-partition already allocated buffers to a smaller
			 * size.
			 */
			rcd->egrbufs.alloced = 0;
			for (i = 0, j = 0, offset = 0; j < idx; i++) {
				if (i >= rcd->egrbufs.count)
					break;
				rcd->egrbufs.rcvtids[i].dma =
					rcd->egrbufs.buffers[j].dma + offset;
				rcd->egrbufs.rcvtids[i].addr =
					rcd->egrbufs.buffers[j].addr + offset;
				rcd->egrbufs.alloced++;
				if ((rcd->egrbufs.buffers[j].dma + offset +
				     new_size) ==
				    (rcd->egrbufs.buffers[j].dma +
				     rcd->egrbufs.buffers[j].len)) {
					j++;
					offset = 0;
				} else {
					offset += new_size;
				}
			}
			rcd->egrbufs.rcvtid_size = new_size;
		}
	}
	rcd->egrbufs.numbufs = idx;
	rcd->egrbufs.size = alloced_bytes;

	hfi1_cdbg(PROC,
		  "ctxt%u: Alloced %u rcv tid entries @ %uKB, total %zuKB\n",
		  rcd->ctxt, rcd->egrbufs.alloced,
		  rcd->egrbufs.rcvtid_size / 1024, rcd->egrbufs.size / 1024);

	/*
	 * Set the contexts rcv array head update threshold to the closest
	 * power of 2 (so we can use a mask instead of modulo) below half
	 * the allocated entries.
	 */
	rcd->egrbufs.threshold =
		rounddown_pow_of_two(rcd->egrbufs.alloced / 2);
	/*
	 * Compute the expected RcvArray entry base. This is done after
	 * allocating the eager buffers in order to maximize the
	 * expected RcvArray entries for the context.
	 */
	max_entries = rcd->rcv_array_groups * dd->rcv_entries.group_size;
	egrtop = roundup(rcd->egrbufs.alloced, dd->rcv_entries.group_size);
	rcd->expected_count = max_entries - egrtop;
	if (rcd->expected_count > MAX_TID_PAIR_ENTRIES * 2)
		rcd->expected_count = MAX_TID_PAIR_ENTRIES * 2;

	rcd->expected_base = rcd->eager_base + egrtop;
	hfi1_cdbg(PROC, "ctxt%u: eager:%u, exp:%u, egrbase:%u, expbase:%u\n",
		  rcd->ctxt, rcd->egrbufs.alloced, rcd->expected_count,
		  rcd->eager_base, rcd->expected_base);

	if (!hfi1_rcvbuf_validate(rcd->egrbufs.rcvtid_size, PT_EAGER, &order)) {
		hfi1_cdbg(PROC,
			  "ctxt%u: current Eager buffer size is invalid %u\n",
			  rcd->ctxt, rcd->egrbufs.rcvtid_size);
		ret = -EINVAL;
		goto bail_rcvegrbuf_phys;
	}

	for (idx = 0; idx < rcd->egrbufs.alloced; idx++) {
		hfi1_put_tid(dd, rcd->eager_base + idx, PT_EAGER,
			     rcd->egrbufs.rcvtids[idx].dma, order);
		cond_resched();
	}

	return 0;

bail_rcvegrbuf_phys:
	for (idx = 0; idx < rcd->egrbufs.alloced &&
	     rcd->egrbufs.buffers[idx].addr;
	     idx++) {
		dma_free_coherent(&dd->pcidev->dev,
				  rcd->egrbufs.buffers[idx].len,
				  rcd->egrbufs.buffers[idx].addr,
				  rcd->egrbufs.buffers[idx].dma);
		rcd->egrbufs.buffers[idx].addr = NULL;
		rcd->egrbufs.buffers[idx].dma = 0;
		rcd->egrbufs.buffers[idx].len = 0;
	}

	return ret;
}
