// SPDX-License-Identifier: GPL-2.0-only
/*
 * FF-A v1.0 proxy to filter out invalid memory-sharing SMC calls issued by
 * the host. FF-A is a slightly more palatable abbreviation of "Arm Firmware
 * Framework for Arm A-profile", which is specified by Arm in document
 * number DEN0077.
 *
 * Copyright (C) 2022 - Google LLC
 * Author: Andrew Walbran <qwandor@google.com>
 *
 * This driver hooks into the SMC trapping logic for the host and intercepts
 * all calls falling within the FF-A range. Each call is either:
 *
 *	- Forwarded on unmodified to the SPMD at EL3
 *	- Rejected as "unsupported"
 *	- Accompanied by a host stage-2 page-table check/update and reissued
 *
 * Consequently, any attempts by the host to make guest memory pages
 * accessible to the secure world using FF-A will be detected either here
 * (in the case that the memory is already owned by the guest) or during
 * donation to the guest (in the case that the memory was previously shared
 * with the secure world).
 *
 * To allow the rolling-back of page-table updates and FF-A calls in the
 * event of failure, operations involving the RXTX buffers are locked for
 * the duration and are therefore serialised.
 */

#include <linux/arm-smccc.h>
#include <linux/arm_ffa.h>
#include <asm/kvm_pkvm.h>

#include <nvhe/ffa.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/trap_handler.h>
#include <nvhe/spinlock.h>

/*
 * "ID value 0 must be returned at the Non-secure physical FF-A instance"
 * We share this ID with the host.
 */
#define HOST_FFA_ID	0

/*
 * A buffer to hold the maximum descriptor size we can see from the host,
 * which is required when the SPMD returns a fragmented FFA_MEM_RETRIEVE_RESP
 * when resolving the handle on the reclaim path.
 */
struct kvm_ffa_descriptor_buffer {
	void	*buf;
	size_t	len;
};

static struct kvm_ffa_descriptor_buffer ffa_desc_buf;

struct kvm_ffa_buffers {
	hyp_spinlock_t lock;
	void *tx;
	void *rx;
};

/*
 * Note that we don't currently lock these buffers explicitly, instead
 * relying on the locking of the host FFA buffers as we only have one
 * client.
 */
static struct kvm_ffa_buffers hyp_buffers;
static struct kvm_ffa_buffers host_buffers;
static u32 hyp_ffa_version;
static bool has_version_negotiated;
static hyp_spinlock_t version_lock;

static void ffa_to_smccc_error(struct arm_smccc_1_2_regs *res, u64 ffa_errno)
{
	*res = (struct arm_smccc_1_2_regs) {
		.a0	= FFA_ERROR,
		.a2	= ffa_errno,
	};
}

static void ffa_to_smccc_res_prop(struct arm_smccc_1_2_regs *res, int ret, u64 prop)
{
	if (ret == FFA_RET_SUCCESS) {
		*res = (struct arm_smccc_1_2_regs) { .a0 = FFA_SUCCESS,
						      .a2 = prop };
	} else {
		ffa_to_smccc_error(res, ret);
	}
}

static void ffa_to_smccc_res(struct arm_smccc_1_2_regs *res, int ret)
{
	ffa_to_smccc_res_prop(res, ret, 0);
}

static void ffa_set_retval(struct kvm_cpu_context *ctxt,
			   struct arm_smccc_1_2_regs *res)
{
	cpu_reg(ctxt, 0) = res->a0;
	cpu_reg(ctxt, 1) = res->a1;
	cpu_reg(ctxt, 2) = res->a2;
	cpu_reg(ctxt, 3) = res->a3;
	cpu_reg(ctxt, 4) = res->a4;
	cpu_reg(ctxt, 5) = res->a5;
	cpu_reg(ctxt, 6) = res->a6;
	cpu_reg(ctxt, 7) = res->a7;

	/*
	 * DEN0028C 2.6: SMC32/HVC32 call from aarch64 must preserve x8-x30.
	 *
	 * In FF-A 1.2, we cannot rely on the function ID sent by the caller to
	 * detect 32-bit calls because the CPU cycle management interfaces (e.g.
	 * FFA_MSG_WAIT, FFA_RUN) are 32-bit only but can have 64-bit responses.
	 *
	 * FFA-1.3 introduces 64-bit variants of the CPU cycle management
	 * interfaces. Moreover, FF-A 1.3 clarifies that SMC32 direct requests
	 * complete with SMC32 direct reponses which *should* allow us use the
	 * function ID sent by the caller to determine whether to return x8-x17.
	 *
	 * Note that we also cannot rely on function IDs in the response.
	 *
	 * Given the above, assume SMC64 and send back x0-x17 unconditionally
	 * as the passthrough code (__kvm_hyp_host_forward_smc) does the same.
	 */
	cpu_reg(ctxt, 8) = res->a8;
	cpu_reg(ctxt, 9) = res->a9;
	cpu_reg(ctxt, 10) = res->a10;
	cpu_reg(ctxt, 11) = res->a11;
	cpu_reg(ctxt, 12) = res->a12;
	cpu_reg(ctxt, 13) = res->a13;
	cpu_reg(ctxt, 14) = res->a14;
	cpu_reg(ctxt, 15) = res->a15;
	cpu_reg(ctxt, 16) = res->a16;
	cpu_reg(ctxt, 17) = res->a17;
}

static bool is_ffa_call(u64 func_id)
{
	return ARM_SMCCC_IS_FAST_CALL(func_id) &&
	       ARM_SMCCC_OWNER_NUM(func_id) == ARM_SMCCC_OWNER_STANDARD &&
	       ARM_SMCCC_FUNC_NUM(func_id) >= FFA_MIN_FUNC_NUM &&
	       ARM_SMCCC_FUNC_NUM(func_id) <= FFA_MAX_FUNC_NUM;
}

static int ffa_map_hyp_buffers(u64 ffa_page_count)
{
	struct arm_smccc_1_2_regs res;

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_FN64_RXTX_MAP,
		.a1 = hyp_virt_to_phys(hyp_buffers.tx),
		.a2 = hyp_virt_to_phys(hyp_buffers.rx),
		.a3 = ffa_page_count,
	}, &res);

	return res.a0 == FFA_SUCCESS ? FFA_RET_SUCCESS : res.a2;
}

static int ffa_unmap_hyp_buffers(void)
{
	struct arm_smccc_1_2_regs res;

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_RXTX_UNMAP,
		.a1 = HOST_FFA_ID,
	}, &res);

	return res.a0 == FFA_SUCCESS ? FFA_RET_SUCCESS : res.a2;
}

static void ffa_mem_frag_tx(struct arm_smccc_1_2_regs *res, u32 handle_lo,
			     u32 handle_hi, u32 fraglen, u32 endpoint_id)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_MEM_FRAG_TX,
		.a1 = handle_lo,
		.a2 = handle_hi,
		.a3 = fraglen,
		.a4 = endpoint_id,
	}, res);
}

static void ffa_mem_frag_rx(struct arm_smccc_1_2_regs *res, u32 handle_lo,
			     u32 handle_hi, u32 fragoff)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_MEM_FRAG_RX,
		.a1 = handle_lo,
		.a2 = handle_hi,
		.a3 = fragoff,
		.a4 = HOST_FFA_ID,
	}, res);
}

static void ffa_mem_xfer(struct arm_smccc_1_2_regs *res, u64 func_id, u32 len,
			  u32 fraglen)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = func_id,
		.a1 = len,
		.a2 = fraglen,
	}, res);
}

static void ffa_mem_reclaim(struct arm_smccc_1_2_regs *res, u32 handle_lo,
			     u32 handle_hi, u32 flags)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_MEM_RECLAIM,
		.a1 = handle_lo,
		.a2 = handle_hi,
		.a3 = flags,
	}, res);
}

static void ffa_retrieve_req(struct arm_smccc_1_2_regs *res, u32 len)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_FN64_MEM_RETRIEVE_REQ,
		.a1 = len,
		.a2 = len,
	}, res);
}

static void ffa_rx_release(struct arm_smccc_1_2_regs *res)
{
	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_RX_RELEASE,
	}, res);
}

static void do_ffa_rxtx_map(struct arm_smccc_1_2_regs *res,
			    struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(phys_addr_t, tx, ctxt, 1);
	DECLARE_REG(phys_addr_t, rx, ctxt, 2);
	DECLARE_REG(u32, npages, ctxt, 3);
	int ret = 0;
	void *rx_virt, *tx_virt;

	if (npages != (KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) / FFA_PAGE_SIZE) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (!PAGE_ALIGNED(tx) || !PAGE_ALIGNED(rx)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	hyp_spin_lock(&host_buffers.lock);
	if (host_buffers.tx) {
		ret = FFA_RET_DENIED;
		goto out_unlock;
	}

	/*
	 * Map our hypervisor buffers into the SPMD before mapping and
	 * pinning the host buffers in our own address space.
	 */
	ret = ffa_map_hyp_buffers(npages);
	if (ret)
		goto out_unlock;

	ret = __pkvm_host_share_hyp(hyp_phys_to_pfn(tx));
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unmap;
	}

	ret = __pkvm_host_share_hyp(hyp_phys_to_pfn(rx));
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unshare_tx;
	}

	tx_virt = hyp_phys_to_virt(tx);
	ret = hyp_pin_shared_mem(tx_virt, tx_virt + 1);
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unshare_rx;
	}

	rx_virt = hyp_phys_to_virt(rx);
	ret = hyp_pin_shared_mem(rx_virt, rx_virt + 1);
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unpin_tx;
	}

	host_buffers.tx = tx_virt;
	host_buffers.rx = rx_virt;

out_unlock:
	hyp_spin_unlock(&host_buffers.lock);
out:
	ffa_to_smccc_res(res, ret);
	return;

err_unpin_tx:
	hyp_unpin_shared_mem(tx_virt, tx_virt + 1);
err_unshare_rx:
	__pkvm_host_unshare_hyp(hyp_phys_to_pfn(rx));
err_unshare_tx:
	__pkvm_host_unshare_hyp(hyp_phys_to_pfn(tx));
err_unmap:
	ffa_unmap_hyp_buffers();
	goto out_unlock;
}

static void do_ffa_rxtx_unmap(struct arm_smccc_1_2_regs *res,
			      struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, id, ctxt, 1);
	int ret = 0;

	if (id != HOST_FFA_ID) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	hyp_spin_lock(&host_buffers.lock);
	if (!host_buffers.tx) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	hyp_unpin_shared_mem(host_buffers.tx, host_buffers.tx + 1);
	WARN_ON(__pkvm_host_unshare_hyp(hyp_virt_to_pfn(host_buffers.tx)));
	host_buffers.tx = NULL;

	hyp_unpin_shared_mem(host_buffers.rx, host_buffers.rx + 1);
	WARN_ON(__pkvm_host_unshare_hyp(hyp_virt_to_pfn(host_buffers.rx)));
	host_buffers.rx = NULL;

	ffa_unmap_hyp_buffers();

out_unlock:
	hyp_spin_unlock(&host_buffers.lock);
out:
	ffa_to_smccc_res(res, ret);
}

static u32 __ffa_host_share_ranges(struct ffa_mem_region_addr_range *ranges,
				   u32 nranges)
{
	u32 i;

	for (i = 0; i < nranges; ++i) {
		struct ffa_mem_region_addr_range *range = &ranges[i];
		u64 sz = (u64)range->pg_cnt * FFA_PAGE_SIZE;
		u64 pfn = hyp_phys_to_pfn(range->address);

		if (!PAGE_ALIGNED(sz))
			break;

		if (__pkvm_host_share_ffa(pfn, sz / PAGE_SIZE))
			break;
	}

	return i;
}

static u32 __ffa_host_unshare_ranges(struct ffa_mem_region_addr_range *ranges,
				     u32 nranges)
{
	u32 i;

	for (i = 0; i < nranges; ++i) {
		struct ffa_mem_region_addr_range *range = &ranges[i];
		u64 sz = (u64)range->pg_cnt * FFA_PAGE_SIZE;
		u64 pfn = hyp_phys_to_pfn(range->address);

		if (!PAGE_ALIGNED(sz))
			break;

		if (__pkvm_host_unshare_ffa(pfn, sz / PAGE_SIZE))
			break;
	}

	return i;
}

static int ffa_host_share_ranges(struct ffa_mem_region_addr_range *ranges,
				 u32 nranges)
{
	u32 nshared = __ffa_host_share_ranges(ranges, nranges);
	int ret = 0;

	if (nshared != nranges) {
		WARN_ON(__ffa_host_unshare_ranges(ranges, nshared) != nshared);
		ret = FFA_RET_DENIED;
	}

	return ret;
}

static int ffa_host_unshare_ranges(struct ffa_mem_region_addr_range *ranges,
				   u32 nranges)
{
	u32 nunshared = __ffa_host_unshare_ranges(ranges, nranges);
	int ret = 0;

	if (nunshared != nranges) {
		WARN_ON(__ffa_host_share_ranges(ranges, nunshared) != nunshared);
		ret = FFA_RET_DENIED;
	}

	return ret;
}

static void do_ffa_mem_frag_tx(struct arm_smccc_1_2_regs *res,
			       struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, handle_lo, ctxt, 1);
	DECLARE_REG(u32, handle_hi, ctxt, 2);
	DECLARE_REG(u32, fraglen, ctxt, 3);
	DECLARE_REG(u32, endpoint_id, ctxt, 4);
	struct ffa_mem_region_addr_range *buf;
	int ret = FFA_RET_INVALID_PARAMETERS;
	u32 nr_ranges;

	if (fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE)
		goto out;

	if (fraglen % sizeof(*buf))
		goto out;

	hyp_spin_lock(&host_buffers.lock);
	if (!host_buffers.tx)
		goto out_unlock;

	buf = hyp_buffers.tx;
	memcpy(buf, host_buffers.tx, fraglen);
	nr_ranges = fraglen / sizeof(*buf);

	ret = ffa_host_share_ranges(buf, nr_ranges);
	if (ret) {
		/*
		 * We're effectively aborting the transaction, so we need
		 * to restore the global state back to what it was prior to
		 * transmission of the first fragment.
		 */
		ffa_mem_reclaim(res, handle_lo, handle_hi, 0);
		WARN_ON(res->a0 != FFA_SUCCESS);
		goto out_unlock;
	}

	ffa_mem_frag_tx(res, handle_lo, handle_hi, fraglen, endpoint_id);
	if (res->a0 != FFA_SUCCESS && res->a0 != FFA_MEM_FRAG_RX)
		WARN_ON(ffa_host_unshare_ranges(buf, nr_ranges));

out_unlock:
	hyp_spin_unlock(&host_buffers.lock);
out:
	if (ret)
		ffa_to_smccc_res(res, ret);

	/*
	 * If for any reason this did not succeed, we're in trouble as we have
	 * now lost the content of the previous fragments and we can't rollback
	 * the host stage-2 changes. The pages previously marked as shared will
	 * remain stuck in that state forever, hence preventing the host from
	 * sharing/donating them again and may possibly lead to subsequent
	 * failures, but this will not compromise confidentiality.
	 */
	return;
}

static void __do_ffa_mem_xfer(const u64 func_id,
			      struct arm_smccc_1_2_regs *res,
			      struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, len, ctxt, 1);
	DECLARE_REG(u32, fraglen, ctxt, 2);
	DECLARE_REG(u64, addr_mbz, ctxt, 3);
	DECLARE_REG(u32, npages_mbz, ctxt, 4);
	struct ffa_mem_region_attributes *ep_mem_access;
	struct ffa_composite_mem_region *reg;
	struct ffa_mem_region *buf;
	u32 offset, nr_ranges, checked_offset;
	int ret = 0;

	if (addr_mbz || npages_mbz || fraglen > len ||
	    fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (fraglen < sizeof(struct ffa_mem_region) +
		      sizeof(struct ffa_mem_region_attributes)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	hyp_spin_lock(&host_buffers.lock);
	if (!host_buffers.tx) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (len > ffa_desc_buf.len) {
		ret = FFA_RET_NO_MEMORY;
		goto out_unlock;
	}

	buf = hyp_buffers.tx;
	memcpy(buf, host_buffers.tx, fraglen);

	ep_mem_access = (void *)buf +
			ffa_mem_desc_offset(buf, 0, hyp_ffa_version);
	offset = ep_mem_access->composite_off;
	if (!offset || buf->ep_count != 1 || buf->sender_id != HOST_FFA_ID) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (check_add_overflow(offset, sizeof(struct ffa_composite_mem_region), &checked_offset)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (fraglen < checked_offset) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	reg = (void *)buf + offset;
	nr_ranges = ((void *)buf + fraglen) - (void *)reg->constituents;
	if (nr_ranges % sizeof(reg->constituents[0])) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	nr_ranges /= sizeof(reg->constituents[0]);
	ret = ffa_host_share_ranges(reg->constituents, nr_ranges);
	if (ret)
		goto out_unlock;

	ffa_mem_xfer(res, func_id, len, fraglen);
	if (fraglen != len) {
		if (res->a0 != FFA_MEM_FRAG_RX)
			goto err_unshare;

		if (res->a3 != fraglen)
			goto err_unshare;
	} else if (res->a0 != FFA_SUCCESS) {
		goto err_unshare;
	}

out_unlock:
	hyp_spin_unlock(&host_buffers.lock);
out:
	if (ret)
		ffa_to_smccc_res(res, ret);
	return;

err_unshare:
	WARN_ON(ffa_host_unshare_ranges(reg->constituents, nr_ranges));
	goto out_unlock;
}

#define do_ffa_mem_xfer(fid, res, ctxt)				\
	do {							\
		BUILD_BUG_ON((fid) != FFA_FN64_MEM_SHARE &&	\
			     (fid) != FFA_FN64_MEM_LEND);	\
		__do_ffa_mem_xfer((fid), (res), (ctxt));	\
	} while (0);

static void do_ffa_mem_reclaim(struct arm_smccc_1_2_regs *res,
			       struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, handle_lo, ctxt, 1);
	DECLARE_REG(u32, handle_hi, ctxt, 2);
	DECLARE_REG(u32, flags, ctxt, 3);
	struct ffa_mem_region_attributes *ep_mem_access;
	struct ffa_composite_mem_region *reg;
	u32 offset, len, fraglen, fragoff;
	struct ffa_mem_region *buf;
	int ret = 0;
	u64 handle;

	handle = PACK_HANDLE(handle_lo, handle_hi);

	hyp_spin_lock(&host_buffers.lock);

	buf = hyp_buffers.tx;
	*buf = (struct ffa_mem_region) {
		.sender_id	= HOST_FFA_ID,
		.handle		= handle,
	};

	ffa_retrieve_req(res, sizeof(*buf));
	buf = hyp_buffers.rx;
	if (res->a0 != FFA_MEM_RETRIEVE_RESP)
		goto out_unlock;

	len = res->a1;
	fraglen = res->a2;

	ep_mem_access = (void *)buf +
			ffa_mem_desc_offset(buf, 0, hyp_ffa_version);
	offset = ep_mem_access->composite_off;
	/*
	 * We can trust the SPMD to get this right, but let's at least
	 * check that we end up with something that doesn't look _completely_
	 * bogus.
	 */
	if (WARN_ON(offset > len ||
		    fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE)) {
		ret = FFA_RET_ABORTED;
		ffa_rx_release(res);
		goto out_unlock;
	}

	if (len > ffa_desc_buf.len) {
		ret = FFA_RET_NO_MEMORY;
		ffa_rx_release(res);
		goto out_unlock;
	}

	buf = ffa_desc_buf.buf;
	memcpy(buf, hyp_buffers.rx, fraglen);
	ffa_rx_release(res);

	for (fragoff = fraglen; fragoff < len; fragoff += fraglen) {
		ffa_mem_frag_rx(res, handle_lo, handle_hi, fragoff);
		if (res->a0 != FFA_MEM_FRAG_TX) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out_unlock;
		}

		fraglen = res->a3;
		memcpy((void *)buf + fragoff, hyp_buffers.rx, fraglen);
		ffa_rx_release(res);
	}

	ffa_mem_reclaim(res, handle_lo, handle_hi, flags);
	if (res->a0 != FFA_SUCCESS)
		goto out_unlock;

	reg = (void *)buf + offset;
	/* If the SPMD was happy, then we should be too. */
	WARN_ON(ffa_host_unshare_ranges(reg->constituents,
					reg->addr_range_cnt));
out_unlock:
	hyp_spin_unlock(&host_buffers.lock);

	if (ret)
		ffa_to_smccc_res(res, ret);
}

/*
 * Is a given FFA function supported, either by forwarding on directly
 * or by handling at EL2?
 */
static bool ffa_call_supported(u64 func_id)
{
	switch (func_id) {
	/* Unsupported memory management calls */
	case FFA_FN64_MEM_RETRIEVE_REQ:
	case FFA_MEM_RETRIEVE_RESP:
	case FFA_MEM_RELINQUISH:
	case FFA_MEM_OP_PAUSE:
	case FFA_MEM_OP_RESUME:
	case FFA_MEM_FRAG_RX:
	case FFA_FN64_MEM_DONATE:
	/* Indirect message passing via RX/TX buffers */
	case FFA_MSG_SEND:
	case FFA_MSG_POLL:
	case FFA_MSG_WAIT:
	/* 32-bit variants of 64-bit calls */
	case FFA_MSG_SEND_DIRECT_RESP:
	case FFA_RXTX_MAP:
	case FFA_MEM_DONATE:
	case FFA_MEM_RETRIEVE_REQ:
       /* Optional notification interfaces added in FF-A 1.1 */
	case FFA_NOTIFICATION_BITMAP_CREATE:
	case FFA_NOTIFICATION_BITMAP_DESTROY:
	case FFA_NOTIFICATION_BIND:
	case FFA_NOTIFICATION_UNBIND:
	case FFA_NOTIFICATION_SET:
	case FFA_NOTIFICATION_GET:
	case FFA_NOTIFICATION_INFO_GET:
	/* Optional interfaces added in FF-A 1.2 */
	case FFA_MSG_SEND_DIRECT_REQ2:		/* Optional per 7.5.1 */
	case FFA_MSG_SEND_DIRECT_RESP2:		/* Optional per 7.5.1 */
	case FFA_CONSOLE_LOG:			/* Optional per 13.1: not in Table 13.1 */
	case FFA_PARTITION_INFO_GET_REGS:	/* Optional for virtual instances per 13.1 */
		return false;
	}

	return true;
}

static bool do_ffa_features(struct arm_smccc_1_2_regs *res,
			    struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, id, ctxt, 1);
	u64 prop = 0;
	int ret = 0;

	if (!ffa_call_supported(id)) {
		ret = FFA_RET_NOT_SUPPORTED;
		goto out_handled;
	}

	switch (id) {
	case FFA_MEM_SHARE:
	case FFA_FN64_MEM_SHARE:
	case FFA_MEM_LEND:
	case FFA_FN64_MEM_LEND:
		ret = FFA_RET_SUCCESS;
		prop = 0; /* No support for dynamic buffers */
		goto out_handled;
	default:
		return false;
	}

out_handled:
	ffa_to_smccc_res_prop(res, ret, prop);
	return true;
}

static int hyp_ffa_post_init(void)
{
	size_t min_rxtx_sz;
	struct arm_smccc_1_2_regs res;

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs){
		.a0 = FFA_ID_GET,
	}, &res);
	if (res.a0 != FFA_SUCCESS)
		return -EOPNOTSUPP;

	if (res.a2 != HOST_FFA_ID)
		return -EINVAL;

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs){
		.a0 = FFA_FEATURES,
		.a1 = FFA_FN64_RXTX_MAP,
	}, &res);
	if (res.a0 != FFA_SUCCESS)
		return -EOPNOTSUPP;

	switch (res.a2 & FFA_FEAT_RXTX_MIN_SZ_MASK) {
	case FFA_FEAT_RXTX_MIN_SZ_4K:
		min_rxtx_sz = SZ_4K;
		break;
	case FFA_FEAT_RXTX_MIN_SZ_16K:
		min_rxtx_sz = SZ_16K;
		break;
	case FFA_FEAT_RXTX_MIN_SZ_64K:
		min_rxtx_sz = SZ_64K;
		break;
	default:
		return -EINVAL;
	}

	if (min_rxtx_sz > PAGE_SIZE)
		return -EOPNOTSUPP;

	return 0;
}

static void do_ffa_version(struct arm_smccc_1_2_regs *res,
			   struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, ffa_req_version, ctxt, 1);

	if (FFA_MAJOR_VERSION(ffa_req_version) != 1) {
		res->a0 = FFA_RET_NOT_SUPPORTED;
		return;
	}

	hyp_spin_lock(&version_lock);
	if (has_version_negotiated) {
		if (FFA_MINOR_VERSION(ffa_req_version) < FFA_MINOR_VERSION(hyp_ffa_version))
			res->a0 = FFA_RET_NOT_SUPPORTED;
		else
			res->a0 = hyp_ffa_version;
		goto unlock;
	}

	/*
	 * If the client driver tries to downgrade the version, we need to ask
	 * first if TEE supports it.
	 */
	if (FFA_MINOR_VERSION(ffa_req_version) < FFA_MINOR_VERSION(hyp_ffa_version)) {
		arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
			.a0 = FFA_VERSION,
			.a1 = ffa_req_version,
		}, res);
		if (res->a0 == FFA_RET_NOT_SUPPORTED)
			goto unlock;

		hyp_ffa_version = ffa_req_version;
	}

	if (hyp_ffa_post_init()) {
		res->a0 = FFA_RET_NOT_SUPPORTED;
	} else {
		smp_store_release(&has_version_negotiated, true);
		res->a0 = hyp_ffa_version;
	}
unlock:
	hyp_spin_unlock(&version_lock);
}

static void do_ffa_part_get(struct arm_smccc_1_2_regs *res,
			    struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, uuid0, ctxt, 1);
	DECLARE_REG(u32, uuid1, ctxt, 2);
	DECLARE_REG(u32, uuid2, ctxt, 3);
	DECLARE_REG(u32, uuid3, ctxt, 4);
	DECLARE_REG(u32, flags, ctxt, 5);
	u32 count, partition_sz, copy_sz;

	hyp_spin_lock(&host_buffers.lock);
	if (!host_buffers.rx) {
		ffa_to_smccc_res(res, FFA_RET_BUSY);
		goto out_unlock;
	}

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_PARTITION_INFO_GET,
		.a1 = uuid0,
		.a2 = uuid1,
		.a3 = uuid2,
		.a4 = uuid3,
		.a5 = flags,
	}, res);

	if (res->a0 != FFA_SUCCESS)
		goto out_unlock;

	count = res->a2;
	if (!count)
		goto out_unlock;

	if (hyp_ffa_version > FFA_VERSION_1_0) {
		/* Get the number of partitions deployed in the system */
		if (flags & 0x1)
			goto out_unlock;

		partition_sz  = res->a3;
	} else {
		/* FFA_VERSION_1_0 lacks the size in the response */
		partition_sz = FFA_1_0_PARTITON_INFO_SZ;
	}

	copy_sz = partition_sz * count;
	if (copy_sz > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) {
		ffa_to_smccc_res(res, FFA_RET_ABORTED);
		goto out_unlock;
	}

	memcpy(host_buffers.rx, hyp_buffers.rx, copy_sz);
out_unlock:
	hyp_spin_unlock(&host_buffers.lock);
}

bool kvm_host_ffa_handler(struct kvm_cpu_context *host_ctxt, u32 func_id)
{
	struct arm_smccc_1_2_regs res;

	/*
	 * There's no way we can tell what a non-standard SMC call might
	 * be up to. Ideally, we would terminate these here and return
	 * an error to the host, but sadly devices make use of custom
	 * firmware calls for things like power management, debugging,
	 * RNG access and crash reporting.
	 *
	 * Given that the architecture requires us to trust EL3 anyway,
	 * we forward unrecognised calls on under the assumption that
	 * the firmware doesn't expose a mechanism to access arbitrary
	 * non-secure memory. Short of a per-device table of SMCs, this
	 * is the best we can do.
	 */
	if (!is_ffa_call(func_id))
		return false;

	if (func_id != FFA_VERSION &&
	    !smp_load_acquire(&has_version_negotiated)) {
		ffa_to_smccc_error(&res, FFA_RET_INVALID_PARAMETERS);
		goto out_handled;
	}

	switch (func_id) {
	case FFA_FEATURES:
		if (!do_ffa_features(&res, host_ctxt))
			return false;
		goto out_handled;
	/* Memory management */
	case FFA_FN64_RXTX_MAP:
		do_ffa_rxtx_map(&res, host_ctxt);
		goto out_handled;
	case FFA_RXTX_UNMAP:
		do_ffa_rxtx_unmap(&res, host_ctxt);
		goto out_handled;
	case FFA_MEM_SHARE:
	case FFA_FN64_MEM_SHARE:
		do_ffa_mem_xfer(FFA_FN64_MEM_SHARE, &res, host_ctxt);
		goto out_handled;
	case FFA_MEM_RECLAIM:
		do_ffa_mem_reclaim(&res, host_ctxt);
		goto out_handled;
	case FFA_MEM_LEND:
	case FFA_FN64_MEM_LEND:
		do_ffa_mem_xfer(FFA_FN64_MEM_LEND, &res, host_ctxt);
		goto out_handled;
	case FFA_MEM_FRAG_TX:
		do_ffa_mem_frag_tx(&res, host_ctxt);
		goto out_handled;
	case FFA_VERSION:
		do_ffa_version(&res, host_ctxt);
		goto out_handled;
	case FFA_PARTITION_INFO_GET:
		do_ffa_part_get(&res, host_ctxt);
		goto out_handled;
	}

	if (ffa_call_supported(func_id))
		return false; /* Pass through */

	ffa_to_smccc_error(&res, FFA_RET_NOT_SUPPORTED);
out_handled:
	ffa_set_retval(host_ctxt, &res);
	return true;
}

int hyp_ffa_init(void *pages)
{
	struct arm_smccc_1_2_regs res;
	void *tx, *rx;

	if (kvm_host_psci_config.smccc_version < ARM_SMCCC_VERSION_1_2)
		return 0;

	arm_smccc_1_2_smc(&(struct arm_smccc_1_2_regs) {
		.a0 = FFA_VERSION,
		.a1 = FFA_VERSION_1_2,
	}, &res);
	if (res.a0 == FFA_RET_NOT_SUPPORTED)
		return 0;

	/*
	 * Firmware returns the maximum supported version of the FF-A
	 * implementation. Check that the returned version is
	 * backwards-compatible with the hyp according to the rules in DEN0077A
	 * v1.1 REL0 13.2.1.
	 *
	 * Of course, things are never simple when dealing with firmware. v1.1
	 * broke ABI with v1.0 on several structures, which is itself
	 * incompatible with the aforementioned versioning scheme. The
	 * expectation is that v1.x implementations that do not support the v1.0
	 * ABI return NOT_SUPPORTED rather than a version number, according to
	 * DEN0077A v1.1 REL0 18.6.4.
	 */
	if (FFA_MAJOR_VERSION(res.a0) != 1)
		return -EOPNOTSUPP;

	if (FFA_MINOR_VERSION(res.a0) < FFA_MINOR_VERSION(FFA_VERSION_1_2))
		hyp_ffa_version = res.a0;
	else
		hyp_ffa_version = FFA_VERSION_1_2;

	tx = pages;
	pages += KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE;
	rx = pages;
	pages += KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE;

	ffa_desc_buf = (struct kvm_ffa_descriptor_buffer) {
		.buf	= pages,
		.len	= PAGE_SIZE *
			  (hyp_ffa_proxy_pages() - (2 * KVM_FFA_MBOX_NR_PAGES)),
	};

	hyp_buffers = (struct kvm_ffa_buffers) {
		.lock	= __HYP_SPIN_LOCK_UNLOCKED,
		.tx	= tx,
		.rx	= rx,
	};

	host_buffers = (struct kvm_ffa_buffers) {
		.lock	= __HYP_SPIN_LOCK_UNLOCKED,
	};

	version_lock = __HYP_SPIN_LOCK_UNLOCKED;
	return 0;
}
