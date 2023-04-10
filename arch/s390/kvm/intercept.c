// SPDX-License-Identifier: GPL-2.0
/*
 * in-kernel handling for sie intercepts
 *
 * Copyright IBM Corp. 2008, 2020
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/errno.h>
#include <linux/pagemap.h>

#include <asm/asm-offsets.h>
#include <asm/irq.h>
#include <asm/sysinfo.h>
#include <asm/uv.h>

#include "kvm-s390.h"
#include "gaccess.h"
#include "trace.h"
#include "trace-s390.h"

u8 kvm_s390_get_ilen(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_sie_block *sie_block = vcpu->arch.sie_block;
	u8 ilen = 0;

	switch (vcpu->arch.sie_block->icptcode) {
	case ICPT_INST:
	case ICPT_INSTPROGI:
	case ICPT_OPEREXC:
	case ICPT_PARTEXEC:
	case ICPT_IOINST:
		/* instruction only stored for these icptcodes */
		ilen = insn_length(vcpu->arch.sie_block->ipa >> 8);
		/* Use the length of the EXECUTE instruction if necessary */
		if (sie_block->icptstatus & 1) {
			ilen = (sie_block->icptstatus >> 4) & 0x6;
			if (!ilen)
				ilen = 4;
		}
		break;
	case ICPT_PROGI:
		/* bit 1+2 of pgmilc are the ilc, so we directly get ilen */
		ilen = vcpu->arch.sie_block->pgmilc & 0x6;
		break;
	}
	return ilen;
}

static int handle_stop(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_local_interrupt *li = &vcpu->arch.local_int;
	int rc = 0;
	uint8_t flags, stop_pending;

	vcpu->stat.exit_stop_request++;

	/* delay the stop if any non-stop irq is pending */
	if (kvm_s390_vcpu_has_irq(vcpu, 1))
		return 0;

	/* avoid races with the injection/SIGP STOP code */
	spin_lock(&li->lock);
	flags = li->irq.stop.flags;
	stop_pending = kvm_s390_is_stop_irq_pending(vcpu);
	spin_unlock(&li->lock);

	trace_kvm_s390_stop_request(stop_pending, flags);
	if (!stop_pending)
		return 0;

	if (flags & KVM_S390_STOP_FLAG_STORE_STATUS) {
		rc = kvm_s390_vcpu_store_status(vcpu,
						KVM_S390_STORE_STATUS_NOADDR);
		if (rc)
			return rc;
	}

	/*
	 * no need to check the return value of vcpu_stop as it can only have
	 * an error for protvirt, but protvirt means user cpu state
	 */
	if (!kvm_s390_user_cpu_state_ctrl(vcpu->kvm))
		kvm_s390_vcpu_stop(vcpu);
	return -EOPNOTSUPP;
}

static int handle_validity(struct kvm_vcpu *vcpu)
{
	int viwhy = vcpu->arch.sie_block->ipb >> 16;

	vcpu->stat.exit_validity++;
	trace_kvm_s390_intercept_validity(vcpu, viwhy);
	KVM_EVENT(3, "validity intercept 0x%x for pid %u (kvm 0x%pK)", viwhy,
		  current->pid, vcpu->kvm);

	/* do not warn on invalid runtime instrumentation mode */
	WARN_ONCE(viwhy != 0x44, "kvm: unhandled validity intercept 0x%x\n",
		  viwhy);
	return -EINVAL;
}

static int handle_instruction(struct kvm_vcpu *vcpu)
{
	vcpu->stat.exit_instruction++;
	trace_kvm_s390_intercept_instruction(vcpu,
					     vcpu->arch.sie_block->ipa,
					     vcpu->arch.sie_block->ipb);

	switch (vcpu->arch.sie_block->ipa >> 8) {
	case 0x01:
		return kvm_s390_handle_01(vcpu);
	case 0x82:
		return kvm_s390_handle_lpsw(vcpu);
	case 0x83:
		return kvm_s390_handle_diag(vcpu);
	case 0xaa:
		return kvm_s390_handle_aa(vcpu);
	case 0xae:
		return kvm_s390_handle_sigp(vcpu);
	case 0xb2:
		return kvm_s390_handle_b2(vcpu);
	case 0xb6:
		return kvm_s390_handle_stctl(vcpu);
	case 0xb7:
		return kvm_s390_handle_lctl(vcpu);
	case 0xb9:
		return kvm_s390_handle_b9(vcpu);
	case 0xe3:
		return kvm_s390_handle_e3(vcpu);
	case 0xe5:
		return kvm_s390_handle_e5(vcpu);
	case 0xeb:
		return kvm_s390_handle_eb(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}

static int inject_prog_on_prog_intercept(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_pgm_info pgm_info = {
		.code = vcpu->arch.sie_block->iprcc,
		/* the PSW has already been rewound */
		.flags = KVM_S390_PGM_FLAGS_NO_REWIND,
	};

	switch (vcpu->arch.sie_block->iprcc & ~PGM_PER) {
	case PGM_AFX_TRANSLATION:
	case PGM_ASX_TRANSLATION:
	case PGM_EX_TRANSLATION:
	case PGM_LFX_TRANSLATION:
	case PGM_LSTE_SEQUENCE:
	case PGM_LSX_TRANSLATION:
	case PGM_LX_TRANSLATION:
	case PGM_PRIMARY_AUTHORITY:
	case PGM_SECONDARY_AUTHORITY:
	case PGM_SPACE_SWITCH:
		pgm_info.trans_exc_code = vcpu->arch.sie_block->tecmc;
		break;
	case PGM_ALEN_TRANSLATION:
	case PGM_ALE_SEQUENCE:
	case PGM_ASTE_INSTANCE:
	case PGM_ASTE_SEQUENCE:
	case PGM_ASTE_VALIDITY:
	case PGM_EXTENDED_AUTHORITY:
		pgm_info.exc_access_id = vcpu->arch.sie_block->eai;
		break;
	case PGM_ASCE_TYPE:
	case PGM_PAGE_TRANSLATION:
	case PGM_REGION_FIRST_TRANS:
	case PGM_REGION_SECOND_TRANS:
	case PGM_REGION_THIRD_TRANS:
	case PGM_SEGMENT_TRANSLATION:
		pgm_info.trans_exc_code = vcpu->arch.sie_block->tecmc;
		pgm_info.exc_access_id  = vcpu->arch.sie_block->eai;
		pgm_info.op_access_id  = vcpu->arch.sie_block->oai;
		break;
	case PGM_MONITOR:
		pgm_info.mon_class_nr = vcpu->arch.sie_block->mcn;
		pgm_info.mon_code = vcpu->arch.sie_block->tecmc;
		break;
	case PGM_VECTOR_PROCESSING:
	case PGM_DATA:
		pgm_info.data_exc_code = vcpu->arch.sie_block->dxc;
		break;
	case PGM_PROTECTION:
		pgm_info.trans_exc_code = vcpu->arch.sie_block->tecmc;
		pgm_info.exc_access_id  = vcpu->arch.sie_block->eai;
		break;
	default:
		break;
	}

	if (vcpu->arch.sie_block->iprcc & PGM_PER) {
		pgm_info.per_code = vcpu->arch.sie_block->perc;
		pgm_info.per_atmid = vcpu->arch.sie_block->peratmid;
		pgm_info.per_address = vcpu->arch.sie_block->peraddr;
		pgm_info.per_access_id = vcpu->arch.sie_block->peraid;
	}
	return kvm_s390_inject_prog_irq(vcpu, &pgm_info);
}

/*
 * restore ITDB to program-interruption TDB in guest lowcore
 * and set TX abort indication if required
*/
static int handle_itdb(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_itdb *itdb;
	int rc;

	if (!IS_TE_ENABLED(vcpu) || !IS_ITDB_VALID(vcpu))
		return 0;
	if (current->thread.per_flags & PER_FLAG_NO_TE)
		return 0;
	itdb = phys_to_virt(vcpu->arch.sie_block->itdba);
	rc = write_guest_lc(vcpu, __LC_PGM_TDB, itdb, sizeof(*itdb));
	if (rc)
		return rc;
	memset(itdb, 0, sizeof(*itdb));

	return 0;
}

#define per_event(vcpu) (vcpu->arch.sie_block->iprcc & PGM_PER)

static int handle_prog(struct kvm_vcpu *vcpu)
{
	psw_t psw;
	int rc;

	vcpu->stat.exit_program_interruption++;

	/*
	 * Intercept 8 indicates a loop of specification exceptions
	 * for protected guests.
	 */
	if (kvm_s390_pv_cpu_is_protected(vcpu))
		return -EOPNOTSUPP;

	if (guestdbg_enabled(vcpu) && per_event(vcpu)) {
		rc = kvm_s390_handle_per_event(vcpu);
		if (rc)
			return rc;
		/* the interrupt might have been filtered out completely */
		if (vcpu->arch.sie_block->iprcc == 0)
			return 0;
	}

	trace_kvm_s390_intercept_prog(vcpu, vcpu->arch.sie_block->iprcc);
	if (vcpu->arch.sie_block->iprcc == PGM_SPECIFICATION) {
		rc = read_guest_lc(vcpu, __LC_PGM_NEW_PSW, &psw, sizeof(psw_t));
		if (rc)
			return rc;
		/* Avoid endless loops of specification exceptions */
		if (!is_valid_psw(&psw))
			return -EOPNOTSUPP;
	}
	rc = handle_itdb(vcpu);
	if (rc)
		return rc;

	return inject_prog_on_prog_intercept(vcpu);
}

/**
 * handle_external_interrupt - used for external interruption interceptions
 * @vcpu: virtual cpu
 *
 * This interception occurs if:
 * - the CPUSTAT_EXT_INT bit was already set when the external interrupt
 *   occurred. In this case, the interrupt needs to be injected manually to
 *   preserve interrupt priority.
 * - the external new PSW has external interrupts enabled, which will cause an
 *   interruption loop. We drop to userspace in this case.
 *
 * The latter case can be detected by inspecting the external mask bit in the
 * external new psw.
 *
 * Under PV, only the latter case can occur, since interrupt priorities are
 * handled in the ultravisor.
 */
static int handle_external_interrupt(struct kvm_vcpu *vcpu)
{
	u16 eic = vcpu->arch.sie_block->eic;
	struct kvm_s390_irq irq;
	psw_t newpsw;
	int rc;

	vcpu->stat.exit_external_interrupt++;

	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		newpsw = vcpu->arch.sie_block->gpsw;
	} else {
		rc = read_guest_lc(vcpu, __LC_EXT_NEW_PSW, &newpsw, sizeof(psw_t));
		if (rc)
			return rc;
	}

	/*
	 * Clock comparator or timer interrupt with external interrupt enabled
	 * will cause interrupt loop. Drop to userspace.
	 */
	if ((eic == EXT_IRQ_CLK_COMP || eic == EXT_IRQ_CPU_TIMER) &&
	    (newpsw.mask & PSW_MASK_EXT))
		return -EOPNOTSUPP;

	switch (eic) {
	case EXT_IRQ_CLK_COMP:
		irq.type = KVM_S390_INT_CLOCK_COMP;
		break;
	case EXT_IRQ_CPU_TIMER:
		irq.type = KVM_S390_INT_CPU_TIMER;
		break;
	case EXT_IRQ_EXTERNAL_CALL:
		irq.type = KVM_S390_INT_EXTERNAL_CALL;
		irq.u.extcall.code = vcpu->arch.sie_block->extcpuaddr;
		rc = kvm_s390_inject_vcpu(vcpu, &irq);
		/* ignore if another external call is already pending */
		if (rc == -EBUSY)
			return 0;
		return rc;
	default:
		return -EOPNOTSUPP;
	}

	return kvm_s390_inject_vcpu(vcpu, &irq);
}

/**
 * handle_mvpg_pei - Handle MOVE PAGE partial execution interception.
 * @vcpu: virtual cpu
 *
 * This interception can only happen for guests with DAT disabled and
 * addresses that are currently not mapped in the host. Thus we try to
 * set up the mappings for the corresponding user pages here (or throw
 * addressing exceptions in case of illegal guest addresses).
 */
static int handle_mvpg_pei(struct kvm_vcpu *vcpu)
{
	unsigned long srcaddr, dstaddr;
	int reg1, reg2, rc;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	/* Ensure that the source is paged-in, no actual access -> no key checking */
	rc = guest_translate_address_with_key(vcpu, vcpu->run->s.regs.gprs[reg2],
					      reg2, &srcaddr, GACC_FETCH, 0);
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	rc = kvm_arch_fault_in_page(vcpu, srcaddr, 0);
	if (rc != 0)
		return rc;

	/* Ensure that the source is paged-in, no actual access -> no key checking */
	rc = guest_translate_address_with_key(vcpu, vcpu->run->s.regs.gprs[reg1],
					      reg1, &dstaddr, GACC_STORE, 0);
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	rc = kvm_arch_fault_in_page(vcpu, dstaddr, 1);
	if (rc != 0)
		return rc;

	kvm_s390_retry_instr(vcpu);

	return 0;
}

static int handle_partial_execution(struct kvm_vcpu *vcpu)
{
	vcpu->stat.exit_pei++;

	if (vcpu->arch.sie_block->ipa == 0xb254)	/* MVPG */
		return handle_mvpg_pei(vcpu);
	if (vcpu->arch.sie_block->ipa >> 8 == 0xae)	/* SIGP */
		return kvm_s390_handle_sigp_pei(vcpu);

	return -EOPNOTSUPP;
}

/*
 * Handle the sthyi instruction that provides the guest with system
 * information, like current CPU resources available at each level of
 * the machine.
 */
int handle_sthyi(struct kvm_vcpu *vcpu)
{
	int reg1, reg2, r = 0;
	u64 code, addr, cc = 0, rc = 0;
	struct sthyi_sctns *sctns = NULL;

	if (!test_kvm_facility(vcpu->kvm, 74))
		return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);
	code = vcpu->run->s.regs.gprs[reg1];
	addr = vcpu->run->s.regs.gprs[reg2];

	vcpu->stat.instruction_sthyi++;
	VCPU_EVENT(vcpu, 3, "STHYI: fc: %llu addr: 0x%016llx", code, addr);
	trace_kvm_s390_handle_sthyi(vcpu, code, addr);

	if (reg1 == reg2 || reg1 & 1 || reg2 & 1)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	if (code & 0xffff) {
		cc = 3;
		rc = 4;
		goto out;
	}

	if (!kvm_s390_pv_cpu_is_protected(vcpu) && (addr & ~PAGE_MASK))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	sctns = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!sctns)
		return -ENOMEM;

	cc = sthyi_fill(sctns, &rc);

out:
	if (!cc) {
		if (kvm_s390_pv_cpu_is_protected(vcpu)) {
			memcpy(sida_addr(vcpu->arch.sie_block), sctns, PAGE_SIZE);
		} else {
			r = write_guest(vcpu, addr, reg2, sctns, PAGE_SIZE);
			if (r) {
				free_page((unsigned long)sctns);
				return kvm_s390_inject_prog_cond(vcpu, r);
			}
		}
	}

	free_page((unsigned long)sctns);
	vcpu->run->s.regs.gprs[reg2 + 1] = rc;
	kvm_s390_set_psw_cc(vcpu, cc);
	return r;
}

static int handle_operexc(struct kvm_vcpu *vcpu)
{
	psw_t oldpsw, newpsw;
	int rc;

	vcpu->stat.exit_operation_exception++;
	trace_kvm_s390_handle_operexc(vcpu, vcpu->arch.sie_block->ipa,
				      vcpu->arch.sie_block->ipb);

	if (vcpu->arch.sie_block->ipa == 0xb256)
		return handle_sthyi(vcpu);

	if (vcpu->arch.sie_block->ipa == 0 && vcpu->kvm->arch.user_instr0)
		return -EOPNOTSUPP;
	rc = read_guest_lc(vcpu, __LC_PGM_NEW_PSW, &newpsw, sizeof(psw_t));
	if (rc)
		return rc;
	/*
	 * Avoid endless loops of operation exceptions, if the pgm new
	 * PSW will cause a new operation exception.
	 * The heuristic checks if the pgm new psw is within 6 bytes before
	 * the faulting psw address (with same DAT, AS settings) and the
	 * new psw is not a wait psw and the fault was not triggered by
	 * problem state.
	 */
	oldpsw = vcpu->arch.sie_block->gpsw;
	if (oldpsw.addr - newpsw.addr <= 6 &&
	    !(newpsw.mask & PSW_MASK_WAIT) &&
	    !(oldpsw.mask & PSW_MASK_PSTATE) &&
	    (newpsw.mask & PSW_MASK_ASC) == (oldpsw.mask & PSW_MASK_ASC) &&
	    (newpsw.mask & PSW_MASK_DAT) == (oldpsw.mask & PSW_MASK_DAT))
		return -EOPNOTSUPP;

	return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);
}

static int handle_pv_spx(struct kvm_vcpu *vcpu)
{
	u32 pref = *(u32 *)sida_addr(vcpu->arch.sie_block);

	kvm_s390_set_prefix(vcpu, pref);
	trace_kvm_s390_handle_prefix(vcpu, 1, pref);
	return 0;
}

static int handle_pv_sclp(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;

	spin_lock(&fi->lock);
	/*
	 * 2 cases:
	 * a: an sccb answering interrupt was already pending or in flight.
	 *    As the sccb value is not known we can simply set some value to
	 *    trigger delivery of a saved SCCB. UV will then use its saved
	 *    copy of the SCCB value.
	 * b: an error SCCB interrupt needs to be injected so we also inject
	 *    a fake SCCB address. Firmware will use the proper one.
	 * This makes sure, that both errors and real sccb returns will only
	 * be delivered after a notification intercept (instruction has
	 * finished) but not after others.
	 */
	fi->srv_signal.ext_params |= 0x43000;
	set_bit(IRQ_PEND_EXT_SERVICE, &fi->pending_irqs);
	clear_bit(IRQ_PEND_EXT_SERVICE, &fi->masked_irqs);
	spin_unlock(&fi->lock);
	return 0;
}

static int handle_pv_uvc(struct kvm_vcpu *vcpu)
{
	struct uv_cb_share *guest_uvcb = sida_addr(vcpu->arch.sie_block);
	struct uv_cb_cts uvcb = {
		.header.cmd	= UVC_CMD_UNPIN_PAGE_SHARED,
		.header.len	= sizeof(uvcb),
		.guest_handle	= kvm_s390_pv_get_handle(vcpu->kvm),
		.gaddr		= guest_uvcb->paddr,
	};
	int rc;

	if (guest_uvcb->header.cmd != UVC_CMD_REMOVE_SHARED_ACCESS) {
		WARN_ONCE(1, "Unexpected notification intercept for UVC 0x%x\n",
			  guest_uvcb->header.cmd);
		return 0;
	}
	rc = gmap_make_secure(vcpu->arch.gmap, uvcb.gaddr, &uvcb);
	/*
	 * If the unpin did not succeed, the guest will exit again for the UVC
	 * and we will retry the unpin.
	 */
	if (rc == -EINVAL)
		return 0;
	/*
	 * If we got -EAGAIN here, we simply return it. It will eventually
	 * get propagated all the way to userspace, which should then try
	 * again.
	 */
	return rc;
}

static int handle_pv_notification(struct kvm_vcpu *vcpu)
{
	int ret;

	if (vcpu->arch.sie_block->ipa == 0xb210)
		return handle_pv_spx(vcpu);
	if (vcpu->arch.sie_block->ipa == 0xb220)
		return handle_pv_sclp(vcpu);
	if (vcpu->arch.sie_block->ipa == 0xb9a4)
		return handle_pv_uvc(vcpu);
	if (vcpu->arch.sie_block->ipa >> 8 == 0xae) {
		/*
		 * Besides external call, other SIGP orders also cause a
		 * 108 (pv notify) intercept. In contrast to external call,
		 * these orders need to be emulated and hence the appropriate
		 * place to handle them is in handle_instruction().
		 * So first try kvm_s390_handle_sigp_pei() and if that isn't
		 * successful, go on with handle_instruction().
		 */
		ret = kvm_s390_handle_sigp_pei(vcpu);
		if (!ret)
			return ret;
	}

	return handle_instruction(vcpu);
}

int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu)
{
	int rc, per_rc = 0;

	if (kvm_is_ucontrol(vcpu->kvm))
		return -EOPNOTSUPP;

	switch (vcpu->arch.sie_block->icptcode) {
	case ICPT_EXTREQ:
		vcpu->stat.exit_external_request++;
		return 0;
	case ICPT_IOREQ:
		vcpu->stat.exit_io_request++;
		return 0;
	case ICPT_INST:
		rc = handle_instruction(vcpu);
		break;
	case ICPT_PROGI:
		return handle_prog(vcpu);
	case ICPT_EXTINT:
		return handle_external_interrupt(vcpu);
	case ICPT_WAIT:
		return kvm_s390_handle_wait(vcpu);
	case ICPT_VALIDITY:
		return handle_validity(vcpu);
	case ICPT_STOP:
		return handle_stop(vcpu);
	case ICPT_OPEREXC:
		rc = handle_operexc(vcpu);
		break;
	case ICPT_PARTEXEC:
		rc = handle_partial_execution(vcpu);
		break;
	case ICPT_KSS:
		rc = kvm_s390_skey_check_enable(vcpu);
		break;
	case ICPT_MCHKREQ:
	case ICPT_INT_ENABLE:
		/*
		 * PSW bit 13 or a CR (0, 6, 14) changed and we might
		 * now be able to deliver interrupts. The pre-run code
		 * will take care of this.
		 */
		rc = 0;
		break;
	case ICPT_PV_INSTR:
		rc = handle_instruction(vcpu);
		break;
	case ICPT_PV_NOTIFY:
		rc = handle_pv_notification(vcpu);
		break;
	case ICPT_PV_PREF:
		rc = 0;
		gmap_convert_to_secure(vcpu->arch.gmap,
				       kvm_s390_get_prefix(vcpu));
		gmap_convert_to_secure(vcpu->arch.gmap,
				       kvm_s390_get_prefix(vcpu) + PAGE_SIZE);
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* process PER, also if the instrution is processed in user space */
	if (vcpu->arch.sie_block->icptstatus & 0x02 &&
	    (!rc || rc == -EOPNOTSUPP))
		per_rc = kvm_s390_handle_per_ifetch_icpt(vcpu);
	return per_rc ? per_rc : rc;
}
