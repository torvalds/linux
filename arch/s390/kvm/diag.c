/*
 * handling diagnose instructions
 *
 * Copyright IBM Corp. 2008, 2011
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/pgalloc.h>
#include <asm/virtio-ccw.h>
#include "kvm-s390.h"
#include "trace.h"
#include "trace-s390.h"
#include "gaccess.h"

static int diag_release_pages(struct kvm_vcpu *vcpu)
{
	unsigned long start, end;
	unsigned long prefix  = kvm_s390_get_prefix(vcpu);

	start = vcpu->run->s.regs.gprs[(vcpu->arch.sie_block->ipa & 0xf0) >> 4];
	end = vcpu->run->s.regs.gprs[vcpu->arch.sie_block->ipa & 0xf] + 4096;

	if (start & ~PAGE_MASK || end & ~PAGE_MASK || start >= end
	    || start < 2 * PAGE_SIZE)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 5, "diag release pages %lX %lX", start, end);
	vcpu->stat.diagnose_10++;

	/*
	 * We checked for start >= end above, so lets check for the
	 * fast path (no prefix swap page involved)
	 */
	if (end <= prefix || start >= prefix + 2 * PAGE_SIZE) {
		gmap_discard(vcpu->arch.gmap, start, end);
	} else {
		/*
		 * This is slow path.  gmap_discard will check for start
		 * so lets split this into before prefix, prefix, after
		 * prefix and let gmap_discard make some of these calls
		 * NOPs.
		 */
		gmap_discard(vcpu->arch.gmap, start, prefix);
		if (start <= prefix)
			gmap_discard(vcpu->arch.gmap, 0, 4096);
		if (end > prefix + 4096)
			gmap_discard(vcpu->arch.gmap, 4096, 8192);
		gmap_discard(vcpu->arch.gmap, prefix + 2 * PAGE_SIZE, end);
	}
	return 0;
}

static int __diag_page_ref_service(struct kvm_vcpu *vcpu)
{
	struct prs_parm {
		u16 code;
		u16 subcode;
		u16 parm_len;
		u16 parm_version;
		u64 token_addr;
		u64 select_mask;
		u64 compare_mask;
		u64 zarch;
	};
	struct prs_parm parm;
	int rc;
	u16 rx = (vcpu->arch.sie_block->ipa & 0xf0) >> 4;
	u16 ry = (vcpu->arch.sie_block->ipa & 0x0f);

	if (vcpu->run->s.regs.gprs[rx] & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	rc = read_guest(vcpu, vcpu->run->s.regs.gprs[rx], rx, &parm, sizeof(parm));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	if (parm.parm_version != 2 || parm.parm_len < 5 || parm.code != 0x258)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	switch (parm.subcode) {
	case 0: /* TOKEN */
		if (vcpu->arch.pfault_token != KVM_S390_PFAULT_TOKEN_INVALID) {
			/*
			 * If the pagefault handshake is already activated,
			 * the token must not be changed.  We have to return
			 * decimal 8 instead, as mandated in SC24-6084.
			 */
			vcpu->run->s.regs.gprs[ry] = 8;
			return 0;
		}

		if ((parm.compare_mask & parm.select_mask) != parm.compare_mask ||
		    parm.token_addr & 7 || parm.zarch != 0x8000000000000000ULL)
			return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

		if (kvm_is_error_gpa(vcpu->kvm, parm.token_addr))
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

		vcpu->arch.pfault_token = parm.token_addr;
		vcpu->arch.pfault_select = parm.select_mask;
		vcpu->arch.pfault_compare = parm.compare_mask;
		vcpu->run->s.regs.gprs[ry] = 0;
		rc = 0;
		break;
	case 1: /*
		 * CANCEL
		 * Specification allows to let already pending tokens survive
		 * the cancel, therefore to reduce code complexity, we assume
		 * all outstanding tokens are already pending.
		 */
		if (parm.token_addr || parm.select_mask ||
		    parm.compare_mask || parm.zarch)
			return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

		vcpu->run->s.regs.gprs[ry] = 0;
		/*
		 * If the pfault handling was not established or is already
		 * canceled SC24-6084 requests to return decimal 4.
		 */
		if (vcpu->arch.pfault_token == KVM_S390_PFAULT_TOKEN_INVALID)
			vcpu->run->s.regs.gprs[ry] = 4;
		else
			vcpu->arch.pfault_token = KVM_S390_PFAULT_TOKEN_INVALID;

		rc = 0;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

static int __diag_time_slice_end(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 5, "%s", "diag time slice end");
	vcpu->stat.diagnose_44++;
	kvm_vcpu_on_spin(vcpu);
	return 0;
}

static int __diag_time_slice_end_directed(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tcpu;
	int tid;
	int i;

	tid = vcpu->run->s.regs.gprs[(vcpu->arch.sie_block->ipa & 0xf0) >> 4];
	vcpu->stat.diagnose_9c++;
	VCPU_EVENT(vcpu, 5, "diag time slice end directed to %d", tid);

	if (tid == vcpu->vcpu_id)
		return 0;

	kvm_for_each_vcpu(i, tcpu, kvm)
		if (tcpu->vcpu_id == tid) {
			kvm_vcpu_yield_to(tcpu);
			break;
		}

	return 0;
}

static int __diag_ipl_functions(struct kvm_vcpu *vcpu)
{
	unsigned int reg = vcpu->arch.sie_block->ipa & 0xf;
	unsigned long subcode = vcpu->run->s.regs.gprs[reg] & 0xffff;

	VCPU_EVENT(vcpu, 5, "diag ipl functions, subcode %lx", subcode);
	switch (subcode) {
	case 3:
		vcpu->run->s390_reset_flags = KVM_S390_RESET_CLEAR;
		break;
	case 4:
		vcpu->run->s390_reset_flags = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!kvm_s390_user_cpu_state_ctrl(vcpu->kvm))
		kvm_s390_vcpu_stop(vcpu);
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_SUBSYSTEM;
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_IPL;
	vcpu->run->s390_reset_flags |= KVM_S390_RESET_CPU_INIT;
	vcpu->run->exit_reason = KVM_EXIT_S390_RESET;
	VCPU_EVENT(vcpu, 3, "requesting userspace resets %llx",
	  vcpu->run->s390_reset_flags);
	trace_kvm_s390_request_resets(vcpu->run->s390_reset_flags);
	return -EREMOTE;
}

static int __diag_virtio_hypercall(struct kvm_vcpu *vcpu)
{
	int ret;

	/* No virtio-ccw notification? Get out quickly. */
	if (!vcpu->kvm->arch.css_support ||
	    (vcpu->run->s.regs.gprs[1] != KVM_S390_VIRTIO_CCW_NOTIFY))
		return -EOPNOTSUPP;

	/*
	 * The layout is as follows:
	 * - gpr 2 contains the subchannel id (passed as addr)
	 * - gpr 3 contains the virtqueue index (passed as datamatch)
	 * - gpr 4 contains the index on the bus (optionally)
	 */
	ret = kvm_io_bus_write_cookie(vcpu->kvm, KVM_VIRTIO_CCW_NOTIFY_BUS,
				      vcpu->run->s.regs.gprs[2] & 0xffffffff,
				      8, &vcpu->run->s.regs.gprs[3],
				      vcpu->run->s.regs.gprs[4]);

	/*
	 * Return cookie in gpr 2, but don't overwrite the register if the
	 * diagnose will be handled by userspace.
	 */
	if (ret != -EOPNOTSUPP)
		vcpu->run->s.regs.gprs[2] = ret;
	/* kvm_io_bus_write_cookie returns -EOPNOTSUPP if it found no match. */
	return ret < 0 ? ret : 0;
}

int kvm_s390_handle_diag(struct kvm_vcpu *vcpu)
{
	int code = kvm_s390_get_base_disp_rs(vcpu, NULL) & 0xffff;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	trace_kvm_s390_handle_diag(vcpu, code);
	switch (code) {
	case 0x10:
		return diag_release_pages(vcpu);
	case 0x44:
		return __diag_time_slice_end(vcpu);
	case 0x9c:
		return __diag_time_slice_end_directed(vcpu);
	case 0x258:
		return __diag_page_ref_service(vcpu);
	case 0x308:
		return __diag_ipl_functions(vcpu);
	case 0x500:
		return __diag_virtio_hypercall(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}
