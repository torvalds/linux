// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <kvm/arm_hypercalls.h>

#include <hyp/adjust_pc.h>
#include <hyp/switch.h>

#include <linux/irqchip/arm-gic-v3.h>
#include <uapi/linux/psci.h>

#include <asm/pgtable-types.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_hypevents.h>
#include <asm/kvm_mmu.h>

#include <nvhe/alloc.h>
#include <nvhe/ffa.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/pkvm.h>
#include <nvhe/trace.h>
#include <nvhe/trap_handler.h>

DEFINE_PER_CPU(struct kvm_nvhe_init_params, kvm_init_params);

/*
 * Define a hypercall handler: handle_<name> unmarshals the arguments from
 * the host context and hands them, correctly typed, to the body that
 * follows the macro. The parameter list is type-checked against the
 * signature declared in <asm/kvm_hcall.h>, so the handler cannot drift
 * from what the typed caller stubs marshal in. Modelled on the syscall
 * wrappers.
 */
/* Truncate the fixed list of argument registers to the declared signature. */
#define KVM_HOST_HCALL_REGS(...)					\
	__KVM_HCALL_MAP_N(COUNT_ARGS(__VA_ARGS__), __KVM_HCALL_ARGS	\
		,, cpu_reg(host_ctxt, 1),, cpu_reg(host_ctxt, 2)	\
		,, cpu_reg(host_ctxt, 3),, cpu_reg(host_ctxt, 4)	\
		,, cpu_reg(host_ctxt, 5),, cpu_reg(host_ctxt, 6))

#define set_cpu_reg_ulong(ctxt, r, v)	{ cpu_reg(ctxt, r) = v; }
#define set_cpu_reg_u64(ctxt, r, v)	{ cpu_reg(ctxt, r) = v; }
#define set_cpu_reg_int(ctxt, r, v)	{ cpu_reg(ctxt, r) = v; }
#define set_cpu_reg_void(ctxt, r, v)	{ v; }
#define set_cpu_reg(ctxt, r, t, v)	set_cpu_reg_##t(ctxt, r, v)

#define DEFINE_KVM_HOST_HCALL(ret, name, ...)				\
	static kvm_host_hcall_sig_##name __do_##name;			\
	static __always_inline						\
	ret __se_##name(__KVM_HCALL_MAP(__KVM_HCALL_LONG, __VA_ARGS__))	\
	{								\
		return __do_##name(__KVM_HCALL_MAP(__KVM_HCALL_CAST, __VA_ARGS__)); \
	}								\
	static void handle_##name(struct kvm_cpu_context *host_ctxt)	\
	{								\
		set_cpu_reg(host_ctxt, 1, ret, __se_##name(KVM_HOST_HCALL_REGS(__VA_ARGS__))); \
	}								\
	static ret __do_##name(__KVM_HCALL_MAP(__KVM_HCALL_DECL, __VA_ARGS__))

#define DEFINE_KVM_HOST_HCALL0(ret, name)				\
	static kvm_host_hcall_sig_##name __do_##name;			\
	static void handle_##name(struct kvm_cpu_context *host_ctxt)	\
	{								\
		set_cpu_reg(host_ctxt, 1, ret, __do_##name());		\
	}								\
	static ret __do_##name(void)

/*
 * Encode a hypervisor request in the host SMCCC return registers for known
 * error numbers.
 *
 * Must be paired with pkvm_call_hyp_req() on the host side.
 */
static int errno_to_smccc(int ret)
{
	struct pkvm_hyp_req req = { .type = PKVM_HYP_NO_REQ };

	switch (ret) {
	case -ENOMEM: {
		u32 nr_pages = hyp_alloc_topup_needed();

		if (nr_pages) {
			req.type = PKVM_HYP_REQ_HYP_ALLOC;
			req.mem.nr_pages = nr_pages;
		}
		break;
	}
	}

	pkvm_hyp_req_to_smccc(host_data_ptr(host_ctxt), &req);

	return ret;
}

/* Number of implemented GICv3 LRs. Used by flush_hyp_vcpu(). */
unsigned int hyp_gicv3_nr_lr;

void __kvm_hyp_host_forward_smc(struct kvm_cpu_context *host_ctxt);

typedef void (*hyp_entry_exit_handler_fn)(struct pkvm_hyp_vcpu *);

static bool pvm_sys64_is_write(u64 esr)
{
	return (esr & ESR_ELx_SYS64_ISS_DIR_MASK) == ESR_ELx_SYS64_ISS_DIR_WRITE;
}

static void handle_pvm_entry_wfx(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	/* Exceptions have priority; the host injects none on WFx. */
	if (vcpu_get_flag(host_vcpu, PENDING_EXCEPTION))
		return;

	if (vcpu_get_flag(host_vcpu, INCREMENT_PC)) {
		vcpu_clear_flag(vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(vcpu);
	}
}

static void handle_pvm_entry_sys64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	bool pc_update;

	/* Exceptions have priority over anything else */
	if (vcpu_get_flag(host_vcpu, PENDING_EXCEPTION)) {
		/* A host-requested exception on SYS64 is always an UNDEF. */
		u32 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT) | ESR_ELx_IL;

		__vcpu_assign_sys_reg(vcpu, ESR_EL1, esr);
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);
		return;
	}

	/* Handle PC increment on a host-emulated access */
	pc_update = vcpu_get_flag(host_vcpu, INCREMENT_PC);
	if (pc_update) {
		vcpu_clear_flag(vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(vcpu);
	}

	/* If the host emulated a read access, update the register */
	if (pc_update && !pvm_sys64_is_write(kvm_vcpu_get_esr(vcpu))) {
		/* r0 as transfer register between the guest and the host. */
		u64 rt_val = READ_ONCE(vcpu_gp_regs(host_vcpu)[0]);
		int rt = kvm_vcpu_sys_get_rt(vcpu);

		vcpu_set_reg(vcpu, rt, rt_val);
	}
}

static void handle_pvm_entry_iabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	unsigned long cpsr = *vcpu_cpsr(vcpu);
	u32 esr = ESR_ELx_IL;

	if (!vcpu_get_flag(host_vcpu, PENDING_EXCEPTION))
		return;

	/* The host's only IABT injection: an external abort. */
	if ((cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
		esr |= (ESR_ELx_EC_IABT_LOW << ESR_ELx_EC_SHIFT);
	else
		esr |= (ESR_ELx_EC_IABT_CUR << ESR_ELx_EC_SHIFT);

	esr |= ESR_ELx_FSC_EXTABT;

	__vcpu_assign_sys_reg(vcpu, ESR_EL1, esr);
	__vcpu_assign_sys_reg(vcpu, FAR_EL1, kvm_vcpu_get_hfar(vcpu));

	/* Injected by __kvm_adjust_pc() on entry. */
	kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);
}

/*
 * Clamp MMIO data to the access width, so a write does not leak the
 * register's upper bits and a read takes no bits beyond the load. The
 * host applies endianness.
 */
static inline u64 kvm_mmio_clamp_data(struct kvm_vcpu *vcpu, u64 val)
{
	unsigned int len = kvm_vcpu_dabt_get_as(vcpu);

	return val & GENMASK_U64(len * 8 - 1, 0);
}

/*
 * Complete an MMIO load: sign-extend from EL2's own syndrome, as the
 * architecture does.
 */
static inline u64 kvm_mmio_read_data(struct kvm_vcpu *vcpu, u64 val)
{
	val = kvm_mmio_clamp_data(vcpu, val);

	if (kvm_vcpu_dabt_issext(vcpu))
		val = sign_extend64(val, kvm_vcpu_dabt_get_as(vcpu) * 8 - 1);

	if (!kvm_vcpu_dabt_issf(vcpu))
		val &= GENMASK_U64(31, 0);

	return val;
}

static void handle_pvm_entry_dabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	bool pc_update;

	/* Exceptions have priority over anything else */
	if (vcpu_get_flag(host_vcpu, PENDING_EXCEPTION)) {
		unsigned long cpsr = *vcpu_cpsr(vcpu);
		u32 esr = ESR_ELx_IL;

		if ((cpsr & PSR_MODE_MASK) == PSR_MODE_EL0t)
			esr |= (ESR_ELx_EC_DABT_LOW << ESR_ELx_EC_SHIFT);
		else
			esr |= (ESR_ELx_EC_DABT_CUR << ESR_ELx_EC_SHIFT);

		esr |= ESR_ELx_FSC_EXTABT;

		__vcpu_assign_sys_reg(vcpu, ESR_EL1, esr);
		__vcpu_assign_sys_reg(vcpu, FAR_EL1, kvm_vcpu_get_hfar(vcpu));

		/* Injected by __kvm_adjust_pc() on entry. */
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);

		/* Cancel any in-flight MMIO */
		vcpu->mmio_needed = false;
		return;
	}

	/* Handle PC increment on MMIO, or on a CMO the host skipped */
	pc_update = vcpu_get_flag(host_vcpu, INCREMENT_PC) &&
		    (vcpu->mmio_needed || esr_dabt_is_cm(kvm_vcpu_get_esr(vcpu)));
	if (pc_update) {
		vcpu_clear_flag(vcpu, PC_UPDATE_REQ);
		kvm_incr_pc(vcpu);
	}

	/* If the host emulated an MMIO read, update the register */
	if (pc_update && vcpu->mmio_needed && !kvm_vcpu_dabt_iswrite(vcpu)) {
		/* r0 as transfer register between the guest and the host. */
		u64 rd_val = READ_ONCE(vcpu_gp_regs(host_vcpu)[0]);
		int rd = kvm_vcpu_dabt_get_rd(vcpu);

		rd_val = kvm_mmio_read_data(vcpu, rd_val);
		vcpu_set_reg(vcpu, rd, rd_val);
	}

	vcpu->mmio_needed = false;
}

static void handle_pvm_entry_hvc64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 ret = READ_ONCE(vcpu_gp_regs(host_vcpu)[0]);
	u32 psci_fn = smccc_get_function(vcpu);

	switch (psci_fn) {
	case PSCI_0_2_FN_CPU_ON:
	case PSCI_0_2_FN64_CPU_ON:
		/*
		 * Roll back a CPU_ON the host failed, unless the target
		 * already reached ON: it is running, and the guest sees
		 * SUCCESS.
		 */
		if (ret != PSCI_RET_SUCCESS) {
			unsigned long cpu_id = smccc_get_arg1(vcpu);
			struct pkvm_hyp_vcpu *target_vcpu;
			struct pkvm_hyp_vm *hyp_vm;
			int prev;

			hyp_vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
			target_vcpu = pkvm_mpidr_to_hyp_vcpu(hyp_vm, cpu_id);

			/*
			 * pvm_psci_vcpu_on() resolved this MPIDR and vcpus[]
			 * entries are never removed, so the lookup cannot miss.
			 * The release orders this vCPU's reset_state writes
			 * before OFF, for the next CPU_ON.
			 */
			prev = cmpxchg_release(&target_vcpu->power_state,
					       PSCI_0_2_AFFINITY_LEVEL_ON_PENDING,
					       PSCI_0_2_AFFINITY_LEVEL_OFF);
			switch (prev) {
			case PSCI_0_2_AFFINITY_LEVEL_ON_PENDING:
				/*
				 * Leave reset_state.reset set: a clear races a
				 * fresh CPU_ON's publish. The stale pc/r0/be are
				 * the guest's own. ALREADY_ON is PSCI's retry
				 * signal for a CPU_ON that raced the CPU_OFF.
				 */
				if (ret != PSCI_RET_ALREADY_ON)
					ret = PSCI_RET_INTERNAL_FAILURE;
				break;
			case PSCI_0_2_AFFINITY_LEVEL_ON:
			case PSCI_0_2_AFFINITY_LEVEL_OFF:
				/* Target already ran (and may have stopped). */
				ret = PSCI_RET_SUCCESS;
				break;
			default:
				ret = PSCI_RET_INTERNAL_FAILURE;
				break;
			}
		}

		break;
	default:
		break;
	}

	vcpu_set_reg(vcpu, 0, ret);
}

/* The host's view of a syndrome: the guest register index is withheld. */
static u64 pvm_host_esr(u64 esr)
{
	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_WFx:
		return esr & ~ESR_ELx_WFx_ISS_RN;
	case ESR_ELx_EC_SYS64:
		return esr & ~ESR_ELx_SYS64_ISS_RT_MASK;
	case ESR_ELx_EC_DABT_LOW:
		return esr & ~ESR_ELx_SRT_MASK;
	default:
		return esr;
	}
}

/*
 * The host's view of PSTATE: the mode, with SErrors masked so that the host
 * pends an SError through HCR_EL2.VSE rather than emulating the entry.
 */
static unsigned long pvm_host_pstate(unsigned long pstate)
{
	return (pstate & PSR_MODE_MASK) | PSR_A_BIT;
}

static void handle_pvm_exit_wfx(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	*vcpu_cpsr(host_vcpu) = pvm_host_pstate(*vcpu_cpsr(vcpu));
}

static void handle_pvm_exit_sys64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 esr = kvm_vcpu_get_esr(vcpu);

	/* The mode is required for the host to emulate some sysregs */
	*vcpu_cpsr(host_vcpu) = pvm_host_pstate(*vcpu_cpsr(vcpu));

	/* r0 as transfer register between the guest and the host. */
	if (pvm_sys64_is_write(esr)) {
		int rt = kvm_vcpu_sys_get_rt(vcpu);
		u64 rt_val = vcpu_get_reg(vcpu, rt);

		vcpu_set_reg(host_vcpu, 0, rt_val);
	}
}

static void handle_pvm_exit_iabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	host_vcpu->arch.fault.hpfar_el2 = vcpu->arch.fault.hpfar_el2;
}

static void handle_pvm_exit_dabt(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	u64 sctlr;

	/*
	 * EL2 has no memslot view: a decodable data abort is prepared as MMIO
	 * for the host to resolve. On unbacked memory the host injects an SEA
	 * for one with ISV clear (LDP/STP, atomics), which EL2 does not
	 * decode, and skips a cache maintenance operation, as for any guest.
	 */
	vcpu->mmio_needed = kvm_vcpu_dabt_isvalid(vcpu);

	/* r0 as transfer register between the guest and the host. */
	if (vcpu->mmio_needed && kvm_vcpu_dabt_iswrite(vcpu)) {
		int rt = kvm_vcpu_dabt_get_rd(vcpu);
		u64 rt_val = vcpu_get_reg(vcpu, rt);

		rt_val = kvm_mmio_clamp_data(vcpu, rt_val);
		vcpu_set_reg(host_vcpu, 0, rt_val);
	}

	*vcpu_cpsr(host_vcpu) = pvm_host_pstate(*vcpu_cpsr(vcpu));
	host_vcpu->arch.fault.far_el2 = vcpu->arch.fault.far_el2 & GENMASK(11, 0);
	host_vcpu->arch.fault.hpfar_el2 = vcpu->arch.fault.hpfar_el2;
	sctlr = __vcpu_sys_reg(vcpu, SCTLR_EL1) & (SCTLR_ELx_EE | SCTLR_EL1_E0E);
	__vcpu_assign_sys_reg(host_vcpu, SCTLR_EL1, sctlr);
}

static void handle_pvm_exit_hvc64(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	int n, i;

	switch (smccc_get_function(vcpu)) {
	/*
	 * CPU_ON: the host uses only the target MPIDR (x1). EL2 resets the
	 * target from its own copy of the entry point and context id.
	 */
	case PSCI_0_2_FN_CPU_ON:
	case PSCI_0_2_FN64_CPU_ON:
		n = 2;
		break;

	case PSCI_0_2_FN_CPU_OFF:
	case PSCI_0_2_FN_SYSTEM_OFF:
	case PSCI_0_2_FN_SYSTEM_RESET:
	case PSCI_0_2_FN_CPU_SUSPEND:
	case PSCI_0_2_FN64_CPU_SUSPEND:
		n = 1;
		break;

	case PSCI_0_2_FN_AFFINITY_INFO:
	case PSCI_0_2_FN64_AFFINITY_INFO:
	case PSCI_1_1_FN_SYSTEM_RESET2:
	case PSCI_1_1_FN64_SYSTEM_RESET2:
		n = 3;
		break;

	/* Unreachable: kvm_handle_pvm_hvc64() forwards only the calls above. */
	default:
		hyp_panic();
	}

	/* Pass the HVC function id (r0) and its arguments. */
	for (i = 0; i < n; i++)
		vcpu_set_reg(host_vcpu, i, vcpu_get_reg(vcpu, i));
}

static const hyp_entry_exit_handler_fn entry_hyp_pvm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= NULL,
	[ESR_ELx_EC_WFx]		= handle_pvm_entry_wfx,
	[ESR_ELx_EC_SYS64]		= handle_pvm_entry_sys64,
	[ESR_ELx_EC_IABT_LOW]		= handle_pvm_entry_iabt,
	[ESR_ELx_EC_DABT_LOW]		= handle_pvm_entry_dabt,
	[ESR_ELx_EC_HVC64]		= handle_pvm_entry_hvc64,
};

static const hyp_entry_exit_handler_fn exit_hyp_pvm_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]		= NULL,
	[ESR_ELx_EC_WFx]		= handle_pvm_exit_wfx,
	[ESR_ELx_EC_SYS64]		= handle_pvm_exit_sys64,
	[ESR_ELx_EC_IABT_LOW]		= handle_pvm_exit_iabt,
	[ESR_ELx_EC_DABT_LOW]		= handle_pvm_exit_dabt,
	[ESR_ELx_EC_HVC64]		= handle_pvm_exit_hvc64,
};

static void __hyp_sve_save_guest(struct kvm_vcpu *vcpu)
{
	__vcpu_assign_sys_reg(vcpu, ZCR_EL1, read_sysreg_el1(SYS_ZCR));
	/*
	 * On saving/restoring guest sve state, always use the maximum VL for
	 * the guest. The layout of the data when saving the sve state depends
	 * on the VL, so use a consistent (i.e., the maximum) guest VL.
	 */
	sve_cond_update_zcr_vq(vcpu_sve_max_vq(vcpu) - 1, SYS_ZCR_EL2);
	sve_save_state(kern_hyp_va(vcpu->arch.sve_state), true);
	fpsimd_save_common(&vcpu->arch.ctxt.fp_regs);
	write_sysreg_s(sve_vq_from_vl(kvm_host_sve_max_vl) - 1, SYS_ZCR_EL2);
}

static void __hyp_sve_restore_host(void)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);
	struct arm64_sve_state *sve_regs = *host_data_ptr(sve_regs);

	/*
	 * On saving/restoring host sve state, always use the maximum VL for
	 * the host. The layout of the data when saving the sve state depends
	 * on the VL, so use a consistent (i.e., the maximum) host VL.
	 *
	 * Note that this constrains the PE to the maximum shared VL
	 * that was discovered, if we wish to use larger VLs this will
	 * need to be revisited.
	 */
	write_sysreg_s(sve_vq_from_vl(kvm_host_sve_max_vl) - 1, SYS_ZCR_EL2);
	sve_load_state(sve_regs, true);
	fpsimd_load_common(&hctxt->fp_regs);
	write_sysreg_el1(ctxt_sys_reg(hctxt, ZCR_EL1), SYS_ZCR);
}

static void fpsimd_sve_flush(void)
{
	*host_data_ptr(fp_owner) = FP_STATE_HOST_OWNED;
}

static void fpsimd_sve_sync(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *hctxt = host_data_ptr(host_ctxt);
	bool has_fpmr;

	if (!guest_owns_fp_regs())
		return;

	/*
	 * Traps have been disabled by __deactivate_cptr_traps(), but there
	 * hasn't necessarily been a context synchronization event yet.
	 */
	isb();

	if (vcpu_has_sve(vcpu))
		__hyp_sve_save_guest(vcpu);
	else
		fpsimd_save_state(&vcpu->arch.ctxt.fp_regs);

	has_fpmr = kvm_has_fpmr(kern_hyp_va(vcpu->kvm));
	if (has_fpmr)
		__vcpu_assign_sys_reg(vcpu, FPMR, read_sysreg_s(SYS_FPMR));

	if (system_supports_sve())
		__hyp_sve_restore_host();
	else
		fpsimd_load_state(&hctxt->fp_regs);

	if (has_fpmr)
		write_sysreg_s(ctxt_sys_reg(hctxt, FPMR), SYS_FPMR);

	*host_data_ptr(fp_owner) = FP_STATE_HOST_OWNED;
}

static void flush_hyp_vgic_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct vgic_v3_cpu_if *host_cpu_if, *hyp_cpu_if;
	unsigned int used_lrs, i;

	host_cpu_if	= &host_vcpu->arch.vgic_cpu.vgic_v3;
	hyp_cpu_if	= &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;

	used_lrs	= host_cpu_if->used_lrs;
	used_lrs	= min(used_lrs, hyp_gicv3_nr_lr);

	hyp_cpu_if->vgic_hcr	= host_cpu_if->vgic_hcr;
	/* Should be a one-off */
	hyp_cpu_if->vgic_sre	= (ICC_SRE_EL1_DIB |
				   ICC_SRE_EL1_DFB |
				   ICC_SRE_EL1_SRE);
	hyp_cpu_if->used_lrs	= used_lrs;

	for (i = 0; i < used_lrs; i++)
		hyp_cpu_if->vgic_lr[i] = host_cpu_if->vgic_lr[i];
}

static void sync_hyp_vgic_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	struct vgic_v3_cpu_if *host_cpu_if, *hyp_cpu_if;
	unsigned int i;

	host_cpu_if	= &host_vcpu->arch.vgic_cpu.vgic_v3;
	hyp_cpu_if	= &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;

	host_cpu_if->vgic_hcr = hyp_cpu_if->vgic_hcr;
	host_cpu_if->vgic_vmcr = hyp_cpu_if->vgic_vmcr;

	for (i = 0; i < hyp_cpu_if->used_lrs; i++)
		host_cpu_if->vgic_lr[i] = hyp_cpu_if->vgic_lr[i];
}

static void flush_hyp_timer_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	/* A hyp vcpu has no offset, and sees vtime == ptime. */
	write_sysreg(0, cntvoff_el2);
	write_sysreg_el0(__vcpu_sys_reg(vcpu, CNTV_CVAL_EL0), SYS_CNTV_CVAL);
	isb();
	write_sysreg_el0(__vcpu_sys_reg(vcpu, CNTV_CTL_EL0), SYS_CNTV_CTL);
}

static void sync_hyp_timer_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;

	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	/*
	 * Preserve the vtimer state so that it is always correct,
	 * even if the host tries to make a mess.
	 */
	__vcpu_assign_sys_reg(vcpu, CNTV_CVAL_EL0, read_sysreg_el0(SYS_CNTV_CVAL));
	__vcpu_assign_sys_reg(vcpu, CNTV_CTL_EL0, read_sysreg_el0(SYS_CNTV_CTL));
}

static void __copy_vcpu_state(const struct kvm_vcpu *from_vcpu,
			      struct kvm_vcpu *to_vcpu)
{
	int i;

	to_vcpu->arch.ctxt.regs		= from_vcpu->arch.ctxt.regs;
	to_vcpu->arch.ctxt.spsr_abt	= from_vcpu->arch.ctxt.spsr_abt;
	to_vcpu->arch.ctxt.spsr_und	= from_vcpu->arch.ctxt.spsr_und;
	to_vcpu->arch.ctxt.spsr_irq	= from_vcpu->arch.ctxt.spsr_irq;
	to_vcpu->arch.ctxt.spsr_fiq	= from_vcpu->arch.ctxt.spsr_fiq;
	to_vcpu->arch.ctxt.fp_regs	= from_vcpu->arch.ctxt.fp_regs;

	/*
	 * Copy the sysregs, but don't mess with the timer state which
	 * is directly handled by EL1 and is expected to be preserved.
	 * enum vcpu_sysreg is sparse: VNCR-mapped registers take values
	 * derived from their VNCR page offset, so the timer registers do
	 * not form a contiguous numeric range and must be skipped by name.
	 */
	for (i = 1; i < NR_SYS_REGS; i++) {
		switch (i) {
		case CNTVOFF_EL2:
		case CNTV_CVAL_EL0:
		case CNTV_CTL_EL0:
		case CNTP_CVAL_EL0:
		case CNTP_CTL_EL0:
			continue;
		}
		to_vcpu->arch.ctxt.sys_regs[i] = from_vcpu->arch.ctxt.sys_regs[i];
	}
}

static void sync_hyp_vcpu_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	__copy_vcpu_state(&hyp_vcpu->vcpu, hyp_vcpu->host_vcpu);
}

static void flush_hyp_vcpu_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	__copy_vcpu_state(hyp_vcpu->host_vcpu, &hyp_vcpu->vcpu);
}

static void flush_debug_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	hyp_vcpu->vcpu.arch.debug_owner = host_vcpu->arch.debug_owner;

	if (kvm_guest_owns_debug_regs(&hyp_vcpu->vcpu)) {
		hyp_vcpu->vcpu.arch.vcpu_debug_state = host_vcpu->arch.vcpu_debug_state;
	} else if (kvm_host_owns_debug_regs(&hyp_vcpu->vcpu)) {
		hyp_vcpu->vcpu.arch.external_debug_state = host_vcpu->arch.external_debug_state;
		/*
		 * The world switch loads MDSCR_EL1 from external_mdscr_el1
		 * (ctxt_mdscr_el1()).
		 */
		hyp_vcpu->vcpu.arch.external_mdscr_el1 = host_vcpu->arch.external_mdscr_el1;
	}
}

static void sync_debug_state(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	if (kvm_guest_owns_debug_regs(&hyp_vcpu->vcpu))
		host_vcpu->arch.vcpu_debug_state = hyp_vcpu->vcpu.arch.vcpu_debug_state;
	else if (kvm_host_owns_debug_regs(&hyp_vcpu->vcpu))
		host_vcpu->arch.external_debug_state = hyp_vcpu->vcpu.arch.external_debug_state;
}

static void flush_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	u64 host_hcr_mask = PKVM_HCR_EL2_HOST_PVM;
	hyp_entry_exit_handler_fn ec_handler;
	u8 esr_ec;

	fpsimd_sve_flush();
	flush_debug_state(hyp_vcpu);

	/*
	 * If we deal with a non-protected guest and the state is potentially
	 * dirty (from a host perspective), copy the state back into the hyp
	 * vcpu.
	 */
	if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		if (vcpu_get_flag(host_vcpu, PKVM_HOST_STATE_DIRTY))
			flush_hyp_vcpu_state(hyp_vcpu);

		hyp_vcpu->vcpu.arch.mdcr_el2 = host_vcpu->arch.mdcr_el2;
		hyp_vcpu->vcpu.arch.iflags = host_vcpu->arch.iflags;
		host_hcr_mask = PKVM_HCR_EL2_HOST_NPVM;
	}

	/* __hyp_running_vcpu must be NULL in a guest context. */
	hyp_vcpu->vcpu.arch.ctxt.__hyp_running_vcpu = NULL;

	/*
	 * A host-injected vSError is masked by the guest's own PSTATE.A, so it
	 * applies to protected guests too.
	 */
	hyp_vcpu->vcpu.arch.hcr_el2 &= ~host_hcr_mask;
	hyp_vcpu->vcpu.arch.hcr_el2 |= READ_ONCE(host_vcpu->arch.hcr_el2) & host_hcr_mask;
	hyp_vcpu->vcpu.arch.vsesr_el2 = host_vcpu->arch.vsesr_el2;

	flush_hyp_vgic_state(hyp_vcpu);
	flush_hyp_timer_state(hyp_vcpu);

	hyp_vcpu->vcpu.arch.pid = host_vcpu->arch.pid;

	switch (ARM_EXCEPTION_CODE(hyp_vcpu->exit_code)) {
	case ARM_EXCEPTION_IRQ:
	case ARM_EXCEPTION_EL1_SERROR:
	case ARM_EXCEPTION_IL:
		break;
	case ARM_EXCEPTION_TRAP:
		/* Nothing was marshalled for this trap, see sync_hyp_vcpu(). */
		if (ARM_SERROR_PENDING(hyp_vcpu->exit_code))
			break;

		if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
			esr_ec = ESR_ELx_EC(kvm_vcpu_get_esr(&hyp_vcpu->vcpu));
			ec_handler = entry_hyp_pvm_handlers[esr_ec];
			if (ec_handler)
				ec_handler(hyp_vcpu);
		}
		break;
	default:
		BUG();
	}

	hyp_vcpu->exit_code = 0;
}

static void sync_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu, u32 exit_reason)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;
	hyp_entry_exit_handler_fn ec_handler;
	u8 esr_ec;

	fpsimd_sve_sync(&hyp_vcpu->vcpu);
	sync_debug_state(hyp_vcpu);

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		/*
		 * Protected: the host sees ESR_EL2 as EL2 took it, register
		 * index withheld; the fault addresses stay withheld unless the
		 * EC handler below adds them.
		 */
		host_vcpu->arch.fault = (struct kvm_vcpu_fault_info) {
			.esr_el2 = pvm_host_esr(hyp_vcpu->vcpu.arch.fault.esr_el2),
			.disr_el1 = hyp_vcpu->vcpu.arch.fault.disr_el1,
		};
	} else {
		/* Non-protected: the host gets the full fault. */
		host_vcpu->arch.fault = hyp_vcpu->vcpu.arch.fault;
		host_vcpu->arch.iflags = hyp_vcpu->vcpu.arch.iflags;
		/*
		 * PC feeds trace_kvm_exit(), PSTATE.SS the host software-step
		 * machine, and both run before the next on-demand ctxt sync.
		 */
		host_vcpu->arch.ctxt.regs.pc = hyp_vcpu->vcpu.arch.ctxt.regs.pc;
		host_vcpu->arch.ctxt.regs.pstate = hyp_vcpu->vcpu.arch.ctxt.regs.pstate;
	}

	switch (ARM_EXCEPTION_CODE(exit_reason)) {
	case ARM_EXCEPTION_IRQ:
		break;
	case ARM_EXCEPTION_TRAP:
		/* SError pending: not handled at EL2, the guest replays it. */
		if (ARM_SERROR_PENDING(exit_reason))
			break;

		/* Per-EC marshalling is for protected guests only. */
		if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
			esr_ec = ESR_ELx_EC(kvm_vcpu_get_esr(&hyp_vcpu->vcpu));
			ec_handler = exit_hyp_pvm_handlers[esr_ec];
			if (ec_handler)
				ec_handler(hyp_vcpu);
		}
		break;
	case ARM_EXCEPTION_EL1_SERROR:
	case ARM_EXCEPTION_IL:
		break;
	default:
		BUG();
	}

	/* Cleared by hardware once the guest takes the vSError. */
	host_vcpu->arch.hcr_el2 &= ~HCR_VSE;
	host_vcpu->arch.hcr_el2 |= hyp_vcpu->vcpu.arch.hcr_el2 & HCR_VSE;

	sync_hyp_vgic_state(hyp_vcpu);
	sync_hyp_timer_state(hyp_vcpu);

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		vcpu_clear_flag(host_vcpu, PC_UPDATE_REQ);

	hyp_vcpu->exit_code = exit_reason;
}

DEFINE_KVM_HOST_HCALL(void, __pkvm_vcpu_load,
	pkvm_handle_t, handle, unsigned int, vcpu_idx, u64, hcr_el2)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = pkvm_load_hyp_vcpu(handle, vcpu_idx);
	if (!hyp_vcpu)
		return;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu)) {
		u64 dfr0 = read_sysreg(id_aa64dfr0_el1);

		/* Propagate WFx trapping flags */
		hyp_vcpu->vcpu.arch.hcr_el2 &= ~(HCR_TWE | HCR_TWI);
		hyp_vcpu->vcpu.arch.hcr_el2 |= hcr_el2 & (HCR_TWE | HCR_TWI);

		/* HPMN == 0 is reserved without FEAT_HPMN0. */
		if (pmuv3_implemented(SYS_FIELD_GET(ID_AA64DFR0_EL1, PMUVer, dfr0)))
			u64p_replace_bits(&hyp_vcpu->vcpu.arch.mdcr_el2,
					  FIELD_GET(ARMV8_PMU_PMCR_N, read_sysreg(pmcr_el0)),
					  MDCR_EL2_HPMN);
	} else {
		memcpy(&hyp_vcpu->vcpu.arch.fgt, hyp_vcpu->host_vcpu->arch.fgt,
		       sizeof(hyp_vcpu->vcpu.arch.fgt));
	}
}

DEFINE_KVM_HOST_HCALL0(void, __pkvm_vcpu_put)
{
	struct pkvm_hyp_vcpu *hyp_vcpu = pkvm_get_loaded_hyp_vcpu();

	if (hyp_vcpu) {
		struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

		if (!pkvm_hyp_vcpu_is_protected(hyp_vcpu) &&
		    !vcpu_get_flag(host_vcpu, PKVM_HOST_STATE_DIRTY)) {
			sync_hyp_vcpu_state(hyp_vcpu);
		}

		pkvm_put_hyp_vcpu(hyp_vcpu);
	}
}

DEFINE_KVM_HOST_HCALL0(void, __pkvm_vcpu_sync_state)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return;

	sync_hyp_vcpu_state(hyp_vcpu);
}

static struct kvm_vcpu *__get_host_hyp_vcpus(struct kvm_vcpu *host_vcpu,
					     struct pkvm_hyp_vcpu **hyp_vcpup)
{
	struct pkvm_hyp_vcpu *hyp_vcpu = NULL;

	if (unlikely(is_protected_kvm_enabled())) {
		hyp_vcpu = pkvm_get_loaded_hyp_vcpu();

		if (!hyp_vcpu || hyp_vcpu->host_vcpu != host_vcpu) {
			hyp_vcpu = NULL;
			host_vcpu = NULL;
		}
	}

	*hyp_vcpup = hyp_vcpu;
	return host_vcpu;
}

static struct kvm_vcpu *
__get_host_hyp_vcpus_from_vgic_v3_cpu_if(struct vgic_v3_cpu_if __kern *cpu_if,
					 struct pkvm_hyp_vcpu **hyp_vcpup)
{
	struct vgic_v3_cpu_if *host_cpu_if = kern_hyp_va_host(cpu_if);
	struct kvm_vcpu *host_vcpu = container_of(host_cpu_if, struct kvm_vcpu,
						  arch.vgic_cpu.vgic_v3);

	return __get_host_hyp_vcpus(host_vcpu, hyp_vcpup);
}

DEFINE_KVM_HOST_HCALL(int, __kvm_vcpu_run, struct kvm_vcpu __kern *, vcpu)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;
	int ret = ARM_EXCEPTION_IL;

	host_vcpu = __get_host_hyp_vcpus(kern_hyp_va_host(vcpu), &hyp_vcpu);
	if (!host_vcpu)
		return ret;

	if (unlikely(hyp_vcpu)) {
		/*
		 * KVM (and pKVM) doesn't support SME guests for now, and
		 * ensures that SME features aren't enabled in pstate when
		 * loading a vcpu. Therefore, if SME features enabled the host
		 * is misbehaving.
		 */
		if (unlikely(system_supports_sme() && read_sysreg_s(SYS_SVCR)))
			return ret;

		/*
		 * ON has a single writer, pkvm_reset_vcpu() on this CPU, so
		 * READ_ONCE suffices. ON_PENDING takes the reset; -ECANCELED
		 * is a rollback that raced it.
		 */
		switch (READ_ONCE(hyp_vcpu->power_state)) {
		case PSCI_0_2_AFFINITY_LEVEL_ON:
			break;
		case PSCI_0_2_AFFINITY_LEVEL_ON_PENDING:
			if (pkvm_reset_vcpu(hyp_vcpu))
				return ret;
			break;
		default:
			return ret;
		}

		flush_hyp_vcpu(hyp_vcpu);

		ret = __kvm_vcpu_run(&hyp_vcpu->vcpu);

		sync_hyp_vcpu(hyp_vcpu, ret);
	} else {
		/* The host is fully trusted, run its vCPU directly. */
		fpsimd_lazy_switch_to_guest(host_vcpu);
		ret = __kvm_vcpu_run(host_vcpu);
		fpsimd_lazy_switch_to_host(host_vcpu);
	}

	return ret;
}

static int pkvm_refill_memcache(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct kvm_vcpu *host_vcpu = hyp_vcpu->host_vcpu;

	return refill_memcache(&hyp_vcpu->vcpu.arch.stage2_mc,
			       host_vcpu->arch.stage2_mc.nr_pages,
			       &host_vcpu->arch.stage2_mc);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_donate_guest,
	u64, pfn, u64, gfn)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || !pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return -EINVAL;

	ret = pkvm_refill_memcache(hyp_vcpu);
	if (ret)
		return ret;

	return __pkvm_host_donate_guest(pfn, gfn, hyp_vcpu);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_share_guest,
	u64, pfn, u64, gfn, u64, nr_pages, u64, prot)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	int ret;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return -EINVAL;

	ret = pkvm_refill_memcache(hyp_vcpu);
	if (ret)
		return ret;

	return __pkvm_host_share_guest(pfn, gfn, nr_pages, hyp_vcpu, prot);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_unshare_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages)
{
	struct pkvm_hyp_vm *hyp_vm;
	int ret;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		return -EINVAL;

	ret = __pkvm_host_unshare_guest(gfn, nr_pages, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);

	return ret;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_relax_perms_guest,
	u64, gfn, u64, prot)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return -EINVAL;

	return __pkvm_host_relax_perms_guest(gfn, hyp_vcpu, prot);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_wrprotect_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages)
{
	struct pkvm_hyp_vm *hyp_vm;
	int ret;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		return -EINVAL;

	ret = __pkvm_host_wrprotect_guest(gfn, nr_pages, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);

	return ret;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_test_clear_young_guest,
	pkvm_handle_t, handle, u64, gfn, u64, nr_pages, bool, mkold)
{
	struct pkvm_hyp_vm *hyp_vm;
	int ret;

	hyp_vm = get_np_pkvm_hyp_vm(handle);
	if (!hyp_vm)
		return -EINVAL;

	ret = __pkvm_host_test_clear_young_guest(gfn, nr_pages, mkold, hyp_vm);
	put_pkvm_hyp_vm(hyp_vm);

	return ret;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_mkyoung_guest,
	u64, gfn)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return -EINVAL;

	return __pkvm_host_mkyoung_guest(gfn, hyp_vcpu);
}

/*
 * PKVM_HOST_STATE_DIRTY names the authoritative copy, the host's when set.
 * A loaded protected vCPU takes the request at its next entry instead.
 */
struct kvm_vcpu *kvm_adjust_pc_get(struct kvm_vcpu *vcpu)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;

	if (!is_protected_kvm_enabled())
		return vcpu;

	hyp_vcpu = pkvm_get_loaded_hyp_vcpu();
	if (!hyp_vcpu || vcpu == &hyp_vcpu->vcpu)
		return vcpu;

	if (pkvm_hyp_vcpu_is_protected(hyp_vcpu))
		return NULL;

	if (vcpu_get_flag(vcpu, PKVM_HOST_STATE_DIRTY))
		return vcpu;

	vcpu_copy_flag(&hyp_vcpu->vcpu, vcpu, PC_UPDATE_REQ);
	return &hyp_vcpu->vcpu;
}

/* Reflect the consumed request back, otherwise it stays pending. */
void kvm_adjust_pc_put(struct kvm_vcpu *vcpu, struct kvm_vcpu *target)
{
	if (target != vcpu)
		vcpu_copy_flag(vcpu, target, PC_UPDATE_REQ);
}

DEFINE_KVM_HOST_HCALL(void, __kvm_adjust_pc,
	struct kvm_vcpu __kern *, vcpu)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;

	host_vcpu = __get_host_hyp_vcpus(kern_hyp_va_host(vcpu), &hyp_vcpu);
	if (host_vcpu) {
		__kvm_adjust_pc(host_vcpu);
		return;
	}

	/*
	 * With no hyp vCPU loaded for it, the host vCPU may be unpinned,
	 * and so unmapped at EL2: its first run pins it. A pin fails only
	 * for memory the host isn't sharing, a bad pointer, so the request
	 * is dropped.
	 */
	host_vcpu = kern_hyp_va_host(vcpu);
	if (hyp_pin_shared_mem(host_vcpu, host_vcpu + 1))
		return;

	__kvm_adjust_pc(host_vcpu);
	hyp_unpin_shared_mem(host_vcpu, host_vcpu + 1);
}

DEFINE_KVM_HOST_HCALL0(void, __kvm_flush_vm_context)
{
	__kvm_flush_vm_context();
}

DEFINE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_ipa,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, ipa, int, level)
{
	__kvm_tlb_flush_vmid_ipa(kern_hyp_va_host(mmu), ipa, level);
}

DEFINE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_ipa_nsh,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, ipa, int, level)
{
	__kvm_tlb_flush_vmid_ipa_nsh(kern_hyp_va_host(mmu), ipa, level);
}

DEFINE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid_range,
	struct kvm_s2_mmu __kern *, mmu, phys_addr_t, start, unsigned long, pages)
{
	__kvm_tlb_flush_vmid_range(kern_hyp_va_host(mmu), start, pages);
}

DEFINE_KVM_HOST_HCALL(void, __kvm_tlb_flush_vmid,
	struct kvm_s2_mmu __kern *, mmu)
{
	__kvm_tlb_flush_vmid(kern_hyp_va_host(mmu));
}

DEFINE_KVM_HOST_HCALL(void, __pkvm_tlb_flush_vmid,
	pkvm_handle_t, handle)
{
	struct pkvm_hyp_vm *hyp_vm = get_np_pkvm_hyp_vm(handle);

	if (!hyp_vm)
		return;

	__kvm_tlb_flush_vmid(&hyp_vm->kvm.arch.mmu);
	put_pkvm_hyp_vm(hyp_vm);
}

DEFINE_KVM_HOST_HCALL(void, __kvm_flush_cpu_context,
	struct kvm_s2_mmu __kern *, mmu)
{
	__kvm_flush_cpu_context(kern_hyp_va_host(mmu));
}

DEFINE_KVM_HOST_HCALL(void, __kvm_timer_set_cntvoff,
	u64, cntvoff)
{
	__kvm_timer_set_cntvoff(cntvoff);
}

DEFINE_KVM_HOST_HCALL0(void, __kvm_enable_ssbs)
{
	u64 tmp;

	tmp = read_sysreg_el2(SYS_SCTLR);
	tmp |= SCTLR_ELx_DSSBS;
	write_sysreg_el2(tmp, SYS_SCTLR);
}

DEFINE_KVM_HOST_HCALL0(u64, __vgic_v3_get_gic_config)
{
	return __vgic_v3_get_gic_config();
}

DEFINE_KVM_HOST_HCALL0(void, __vgic_v3_init_lrs)
{
	__vgic_v3_init_lrs();
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v3_save_aprs,
	struct vgic_v3_cpu_if __kern *, cpu_if)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;

	host_vcpu = __get_host_hyp_vcpus_from_vgic_v3_cpu_if(cpu_if, &hyp_vcpu);
	if (!host_vcpu)
		return;

	if (unlikely(hyp_vcpu)) {
		struct vgic_v3_cpu_if *hyp_cpu_if, *host_cpu_if;
		int i;

		hyp_cpu_if = &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;
		__vgic_v3_save_aprs(hyp_cpu_if);

		host_cpu_if = &host_vcpu->arch.vgic_cpu.vgic_v3;
		host_cpu_if->vgic_vmcr = hyp_cpu_if->vgic_vmcr;
		for (i = 0; i < ARRAY_SIZE(host_cpu_if->vgic_ap0r); i++) {
			host_cpu_if->vgic_ap0r[i] = hyp_cpu_if->vgic_ap0r[i];
			host_cpu_if->vgic_ap1r[i] = hyp_cpu_if->vgic_ap1r[i];
		}
	} else {
		__vgic_v3_save_aprs(&host_vcpu->arch.vgic_cpu.vgic_v3);
	}
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v3_restore_vmcr_aprs,
	struct vgic_v3_cpu_if __kern *, cpu_if)
{
	struct pkvm_hyp_vcpu *hyp_vcpu;
	struct kvm_vcpu *host_vcpu;

	host_vcpu = __get_host_hyp_vcpus_from_vgic_v3_cpu_if(cpu_if, &hyp_vcpu);
	if (!host_vcpu)
		return;

	if (unlikely(hyp_vcpu)) {
		struct vgic_v3_cpu_if *hyp_cpu_if, *host_cpu_if;
		int i;

		hyp_cpu_if = &hyp_vcpu->vcpu.arch.vgic_cpu.vgic_v3;
		host_cpu_if = &host_vcpu->arch.vgic_cpu.vgic_v3;

		hyp_cpu_if->vgic_vmcr = host_cpu_if->vgic_vmcr;
		/* Should be a one-off */
		hyp_cpu_if->vgic_sre = (ICC_SRE_EL1_DIB |
					ICC_SRE_EL1_DFB |
					ICC_SRE_EL1_SRE);
		for (i = 0; i < ARRAY_SIZE(host_cpu_if->vgic_ap0r); i++) {
			hyp_cpu_if->vgic_ap0r[i] = host_cpu_if->vgic_ap0r[i];
			hyp_cpu_if->vgic_ap1r[i] = host_cpu_if->vgic_ap1r[i];
		}

		__vgic_v3_restore_vmcr_aprs(hyp_cpu_if);
	} else {
		__vgic_v3_restore_vmcr_aprs(&host_vcpu->arch.vgic_cpu.vgic_v3);
	}
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_init,
	phys_addr_t, phys, unsigned long, size,
	unsigned long *, per_cpu_base, u32, hyp_va_bits)
{
	/*
	 * __pkvm_init() will return only if an error occurred, otherwise it
	 * will tail-call in __pkvm_init_finalise() which will have to deal
	 * with the host context directly.
	 */
	return __pkvm_init(phys, size, per_cpu_base, hyp_va_bits);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_cpu_set_vector,
	enum arm64_hyp_spectre_vector, slot)
{
	return pkvm_cpu_set_vector(slot);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_share_hyp,
	u64, pfn)
{
	return __pkvm_host_share_hyp(pfn);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_host_unshare_hyp,
	u64, pfn)
{
	return __pkvm_host_unshare_hyp(pfn);
}

DEFINE_KVM_HOST_HCALL(ulong, __pkvm_create_private_mapping,
	phys_addr_t, phys, size_t, size, u64, prot)
{
	/*
	 * __pkvm_create_private_mapping() populates a pointer with the
	 * hypervisor start address of the allocation.
	 *
	 * However, handle___pkvm_create_private_mapping() hypercall crosses the
	 * EL1/EL2 boundary so the pointer would not be valid in this context.
	 *
	 * Instead pass the allocation address as the return value (or return
	 * ERR_PTR() on failure).
	 */
	ulong haddr;
	int err = __pkvm_create_private_mapping(phys, size, prot, &haddr);

	if (err)
		haddr = (ulong)ERR_PTR(err);

	return haddr;
}

DEFINE_KVM_HOST_HCALL0(int, __pkvm_prot_finalize)
{
	return __pkvm_prot_finalize();
}

DEFINE_KVM_HOST_HCALL0(int, __pkvm_reserve_vm)
{
	return __pkvm_reserve_vm();
}

DEFINE_KVM_HOST_HCALL(void, __pkvm_unreserve_vm,
	pkvm_handle_t, handle)
{
	__pkvm_unreserve_vm(handle);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_init_vm,
	struct kvm __kern *, host_kvm, void __kern *, pgd_hva)
{
	return errno_to_smccc(__pkvm_init_vm(kern_hyp_va_host(host_kvm), pgd_hva));
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_init_vcpu,
	pkvm_handle_t, handle, struct kvm_vcpu __kern *, host_vcpu)
{
	return errno_to_smccc(__pkvm_init_vcpu(handle, kern_hyp_va_host(host_vcpu)));
}

DEFINE_KVM_HOST_HCALL0(int, __pkvm_vcpu_in_poison_fault)
{
	struct pkvm_hyp_vcpu *hyp_vcpu = pkvm_get_loaded_hyp_vcpu();

	return hyp_vcpu ? __pkvm_vcpu_in_poison_fault(hyp_vcpu) : -EINVAL;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_force_reclaim_guest_page,
	phys_addr_t, phys)
{
	return __pkvm_host_force_reclaim_page_guest(phys);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_reclaim_dying_guest_page,
	pkvm_handle_t, handle, u64, gfn)
{
	return __pkvm_reclaim_dying_guest_page(handle, gfn);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_start_teardown_vm,
	pkvm_handle_t, handle)
{
	return __pkvm_start_teardown_vm(handle);
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_finalize_teardown_vm,
	pkvm_handle_t, handle)
{
	return __pkvm_finalize_teardown_vm(handle);
}

DEFINE_KVM_HOST_HCALL0(int, __pkvm_hyp_alloc_selftest)
{
	struct pkvm_hyp_req req = { .type = PKVM_HYP_NO_REQ };
	int ret = -EPERM;

#ifdef CONFIG_NVHE_EL2_DEBUG
	ret = hyp_allocator_selftest();
	if (ret == -ENOMEM) {
		req.type = PKVM_HYP_REQ_HYP_ALLOC_SELFTEST;
		req.mem.nr_pages = hyp_alloc_selftest_topup_needed();
	}
#endif
	pkvm_hyp_req_to_smccc(host_data_ptr(host_ctxt), &req);

	return ret;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_hyp_topup,
	enum pkvm_topup_id, id, phys_addr_t, head, unsigned long, nr_pages)
{
	struct kvm_cpu_context *host_ctxt = host_data_ptr(host_ctxt);
	struct kvm_hyp_memcache host_mc = {
		.head = head,
		.nr_pages = nr_pages,
	};
	int ret;

	switch (id) {
	case PKVM_TOPUP_HYP_ALLOC:
		ret = hyp_alloc_topup(&host_mc);
		break;
	case PKVM_TOPUP_HYP_ALLOC_SELFTEST:
		ret = hyp_alloc_selftest_topup(&host_mc);
		break;
	default:
		ret = -EINVAL;
	}

	cpu_reg(host_ctxt, 2) = host_mc.head;
	cpu_reg(host_ctxt, 3) = host_mc.nr_pages;

	return ret;
}

DEFINE_KVM_HOST_HCALL(int, __pkvm_hyp_reclaim,
	enum pkvm_topup_id, id, unsigned long, target)
{
	struct kvm_cpu_context *host_ctxt = host_data_ptr(host_ctxt);
	struct kvm_hyp_memcache host_mc = {};
	int ret = 0;

	switch (id) {
	case PKVM_TOPUP_HYP_ALLOC:
		hyp_alloc_reclaim(&host_mc, target);
		break;
	case PKVM_TOPUP_HYP_ALLOC_SELFTEST:
		hyp_alloc_selftest_reclaim(&host_mc, target);
		break;
	default:
		ret = -EINVAL;
	}

	cpu_reg(host_ctxt, 2) = host_mc.head;
	cpu_reg(host_ctxt, 3) = host_mc.nr_pages;

	return ret;
}

DEFINE_KVM_HOST_HCALL(ulong, __pkvm_hyp_reclaimable,
	enum pkvm_topup_id, id)
{
	switch (id) {
	case PKVM_TOPUP_HYP_ALLOC:
		return hyp_alloc_reclaimable();
	default:
		return 0;
	}
}

DEFINE_KVM_HOST_HCALL(int, __tracing_load,
	void __kern *, desc_hva, size_t, desc_size)
{
	return errno_to_smccc(__tracing_load(desc_hva, desc_size));
}

DEFINE_KVM_HOST_HCALL0(void, __tracing_unload)
{
	__tracing_unload();
}

DEFINE_KVM_HOST_HCALL(int, __tracing_enable,
	bool, enable)
{
	return __tracing_enable(enable);
}

DEFINE_KVM_HOST_HCALL(int, __tracing_swap_reader,
	unsigned int, cpu)
{
	return __tracing_swap_reader(cpu);
}

DEFINE_KVM_HOST_HCALL(void, __tracing_update_clock,
	u32, mult, u32, shift, u64, epoch_ns, u64, epoch_cyc)
{
	__tracing_update_clock(mult, shift, epoch_ns, epoch_cyc);
}

DEFINE_KVM_HOST_HCALL(int, __tracing_reset,
	unsigned int, cpu)
{
	return __tracing_reset(cpu);
}

DEFINE_KVM_HOST_HCALL(int, __tracing_enable_event,
	unsigned short, id, bool, enable)
{
	return __tracing_enable_event(id, enable);
}

DEFINE_KVM_HOST_HCALL(void, __tracing_write_event,
	u64, id)
{
	trace_selftest(id);
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v5_make_resident,
		      struct vgic_v5_cpu_if __kern *, cpu_if)
{
	if (unlikely(is_protected_kvm_enabled()))
		return;

	__vgic_v5_make_resident(kern_hyp_va_host(cpu_if));
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v5_make_non_resident,
		      struct vgic_v5_cpu_if __kern *, cpu_if)
{
	if (unlikely(is_protected_kvm_enabled()))
		return;

	__vgic_v5_make_non_resident(kern_hyp_va_host(cpu_if));
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v5_save_apr,
	struct vgic_v5_cpu_if __kern *, cpu_if)
{
	__vgic_v5_save_apr(kern_hyp_va_host(cpu_if));
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v5_restore_vmcr_apr,
	struct vgic_v5_cpu_if __kern *, cpu_if)
{
	__vgic_v5_restore_vmcr_apr(kern_hyp_va_host(cpu_if));
}

DEFINE_KVM_HOST_HCALL(void, __vgic_v5_vdpend,
		      u32, intid, bool, pending, u16, vm)
{
	if (unlikely(is_protected_kvm_enabled()))
		return;

	__vgic_v5_vdpend(intid, pending, vm);
}

typedef void (*hcall_t)(struct kvm_cpu_context *);

#define HANDLE_FUNC(x)	[__KVM_HOST_SMCCC_FUNC_##x] = (hcall_t)handle_##x

static const hcall_t host_hcall[] = {
	/* ___kvm_hyp_init */
	HANDLE_FUNC(__pkvm_init),
	HANDLE_FUNC(__pkvm_create_private_mapping),
	HANDLE_FUNC(__pkvm_cpu_set_vector),
	HANDLE_FUNC(__kvm_enable_ssbs),
	HANDLE_FUNC(__vgic_v3_init_lrs),
	HANDLE_FUNC(__vgic_v3_get_gic_config),
	HANDLE_FUNC(__pkvm_hyp_alloc_selftest),
	HANDLE_FUNC(__pkvm_prot_finalize),

	HANDLE_FUNC(__kvm_adjust_pc),
	HANDLE_FUNC(__kvm_vcpu_run),
	HANDLE_FUNC(__kvm_flush_vm_context),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_ipa),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_ipa_nsh),
	HANDLE_FUNC(__kvm_tlb_flush_vmid),
	HANDLE_FUNC(__kvm_tlb_flush_vmid_range),
	HANDLE_FUNC(__kvm_flush_cpu_context),
	HANDLE_FUNC(__kvm_timer_set_cntvoff),
	HANDLE_FUNC(__tracing_load),
	HANDLE_FUNC(__tracing_unload),
	HANDLE_FUNC(__tracing_enable),
	HANDLE_FUNC(__tracing_swap_reader),
	HANDLE_FUNC(__tracing_update_clock),
	HANDLE_FUNC(__tracing_reset),
	HANDLE_FUNC(__tracing_enable_event),
	HANDLE_FUNC(__tracing_write_event),
	HANDLE_FUNC(__vgic_v3_save_aprs),
	HANDLE_FUNC(__vgic_v3_restore_vmcr_aprs),
	HANDLE_FUNC(__vgic_v5_make_resident),
	HANDLE_FUNC(__vgic_v5_make_non_resident),
	HANDLE_FUNC(__vgic_v5_vdpend),
	HANDLE_FUNC(__vgic_v5_save_apr),
	HANDLE_FUNC(__vgic_v5_restore_vmcr_apr),

	HANDLE_FUNC(__pkvm_hyp_topup),
	HANDLE_FUNC(__pkvm_hyp_reclaim),
	HANDLE_FUNC(__pkvm_hyp_reclaimable),
	HANDLE_FUNC(__pkvm_host_share_hyp),
	HANDLE_FUNC(__pkvm_host_unshare_hyp),

	HANDLE_FUNC(__pkvm_host_donate_guest),
	HANDLE_FUNC(__pkvm_host_share_guest),
	HANDLE_FUNC(__pkvm_host_unshare_guest),
	HANDLE_FUNC(__pkvm_host_relax_perms_guest),
	HANDLE_FUNC(__pkvm_host_wrprotect_guest),
	HANDLE_FUNC(__pkvm_host_test_clear_young_guest),
	HANDLE_FUNC(__pkvm_host_mkyoung_guest),
	HANDLE_FUNC(__pkvm_reserve_vm),
	HANDLE_FUNC(__pkvm_unreserve_vm),
	HANDLE_FUNC(__pkvm_init_vm),
	HANDLE_FUNC(__pkvm_init_vcpu),
	HANDLE_FUNC(__pkvm_vcpu_in_poison_fault),
	HANDLE_FUNC(__pkvm_force_reclaim_guest_page),
	HANDLE_FUNC(__pkvm_reclaim_dying_guest_page),
	HANDLE_FUNC(__pkvm_start_teardown_vm),
	HANDLE_FUNC(__pkvm_finalize_teardown_vm),
	HANDLE_FUNC(__pkvm_vcpu_load),
	HANDLE_FUNC(__pkvm_vcpu_put),
	HANDLE_FUNC(__pkvm_vcpu_sync_state),
	HANDLE_FUNC(__pkvm_tlb_flush_vmid),
};

static void handle_host_hcall(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(unsigned long, id, host_ctxt, 0);
	unsigned long hcall_min = 0, hcall_max = __KVM_HOST_SMCCC_FUNC_MAX;
	hcall_t hfn;

	BUILD_BUG_ON(ARRAY_SIZE(host_hcall) != __KVM_HOST_SMCCC_FUNC_MAX);

	/*
	 * If pKVM has been initialised then reject any calls to the
	 * early "privileged" hypercalls. Note that we cannot reject
	 * calls to __pkvm_prot_finalize for two reasons: (1) The static
	 * key used to determine initialisation must be toggled prior to
	 * finalisation and (2) finalisation is performed on a per-CPU
	 * basis. This is all fine, however, since __pkvm_prot_finalize
	 * returns -EPERM after the first call for a given CPU.
	 */
	if (static_branch_unlikely(&kvm_protected_mode_initialized)) {
		hcall_min = __KVM_HOST_SMCCC_FUNC_MIN_PKVM;
	} else {
		hcall_max = __KVM_HOST_SMCCC_FUNC_PKVM_ONLY;
	}

	id &= ~ARM_SMCCC_CALL_HINTS;
	id -= KVM_HOST_SMCCC_ID(0);

	if (unlikely(id < hcall_min || id >= hcall_max))
		goto inval;

	hfn = host_hcall[id];
	if (unlikely(!hfn))
		goto inval;

	cpu_reg(host_ctxt, 0) = SMCCC_RET_SUCCESS;
	hfn(host_ctxt);

	return;
inval:
	cpu_reg(host_ctxt, 0) = SMCCC_RET_NOT_SUPPORTED;
}

static void default_host_smc_handler(struct kvm_cpu_context *host_ctxt)
{
	trace_hyp_exit(host_ctxt, HYP_REASON_SMC);
	__kvm_hyp_host_forward_smc(host_ctxt);
	trace_hyp_enter(host_ctxt, HYP_REASON_SMC);
}

static void handle_host_smc(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, func_id, host_ctxt, 0);
	u64 esr = read_sysreg_el2(SYS_ESR);
	bool handled;

	if (esr & ESR_ELx_xVC_IMM_MASK) {
		cpu_reg(host_ctxt, 0) = SMCCC_RET_NOT_SUPPORTED;
		goto exit_skip_instr;
	}

	func_id &= ~ARM_SMCCC_CALL_HINTS;
	if (upper_32_bits(func_id)) {
		cpu_reg(host_ctxt, 0) = SMCCC_RET_NOT_SUPPORTED;
		goto exit_skip_instr;
	}

	handled = kvm_host_psci_handler(host_ctxt, func_id);
	if (!handled)
		handled = kvm_host_ffa_handler(host_ctxt, func_id);
	if (!handled)
		default_host_smc_handler(host_ctxt);

exit_skip_instr:
	/* SMC was trapped, move ELR past the current PC. */
	kvm_skip_host_instr();
}

void inject_host_exception(u64 esr)
{
	u64 sctlr, spsr_el1, spsr_el2, exc_offset = except_type_sync;
	const u64 spsr_mask = PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT |
			      PSR_V_BIT | PSR_DIT_BIT | PSR_PAN_BIT;

	spsr_el1 = spsr_el2 = read_sysreg_el2(SYS_SPSR);
	switch (spsr_el1 & (PSR_MODE_MASK | PSR_MODE32_BIT)) {
	case PSR_MODE_EL0t:
		exc_offset += LOWER_EL_AArch64_VECTOR;
		break;
	case PSR_MODE_EL0t | PSR_MODE32_BIT:
		exc_offset += LOWER_EL_AArch32_VECTOR;
		break;
	default:
		exc_offset += CURRENT_EL_SP_ELx_VECTOR;
	}

	spsr_el2 &= spsr_mask;
	spsr_el2 |= PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT |
		    PSR_MODE_EL1h;

	sctlr = read_sysreg_el1(SYS_SCTLR);
	if (!(sctlr & SCTLR_EL1_SPAN))
		spsr_el2 |= PSR_PAN_BIT;

	if (sctlr & SCTLR_ELx_DSSBS)
		spsr_el2 |= PSR_SSBS_BIT;

	if (system_supports_mte())
		spsr_el2 |= PSR_TCO_BIT;

	if (esr_fsc_is_translation_fault(esr))
		write_sysreg_el1(read_sysreg_el2(SYS_FAR), SYS_FAR);

	write_sysreg_el1(esr, SYS_ESR);
	write_sysreg_el1(read_sysreg_el2(SYS_ELR), SYS_ELR);
	write_sysreg_el1(spsr_el1, SYS_SPSR);
	write_sysreg_el2(read_sysreg_el1(SYS_VBAR) + exc_offset, SYS_ELR);
	write_sysreg_el2(spsr_el2, SYS_SPSR);
}

static void inject_host_undef64(void)
{
	inject_host_exception((ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT) |
			       ESR_ELx_IL);
}

static bool handle_host_mte(u64 esr)
{
	switch (esr_sys64_to_sysreg(esr)) {
	case SYS_RGSR_EL1:
	case SYS_GCR_EL1:
	case SYS_TFSR_EL1:
	case SYS_TFSRE0_EL1:
		/* If we're here for any reason other than MTE, it's a bug. */
		if (read_sysreg(HCR_EL2) & HCR_ATA)
			return false;
		break;
	case SYS_GMID_EL1:
		/* If we're here for any reason other than MTE, it's a bug. */
		if (!(read_sysreg(HCR_EL2) & HCR_TID5))
			return false;
		break;
	default:
		return false;
	}

	inject_host_undef64();
	return true;
}

void handle_trap(struct kvm_cpu_context *host_ctxt)
{
	u64 esr = read_sysreg_el2(SYS_ESR);


	switch (ESR_ELx_EC(esr)) {
	case ESR_ELx_EC_HVC64:
		trace_hyp_enter(host_ctxt, HYP_REASON_HVC);
		handle_host_hcall(host_ctxt);
		break;
	case ESR_ELx_EC_SMC64:
		trace_hyp_enter(host_ctxt, HYP_REASON_SMC);
		handle_host_smc(host_ctxt);
		break;
	case ESR_ELx_EC_IABT_LOW:
	case ESR_ELx_EC_DABT_LOW:
		trace_hyp_enter(host_ctxt, HYP_REASON_HOST_ABORT);
		handle_host_mem_abort(host_ctxt);
		break;
	case ESR_ELx_EC_SYS64:
		trace_hyp_enter(host_ctxt, HYP_REASON_SYS);
		if (handle_host_mte(esr))
			break;
		fallthrough;
	default:
		BUG();
	}

	trace_hyp_exit(host_ctxt, HYP_REASON_ERET_HOST);
}
