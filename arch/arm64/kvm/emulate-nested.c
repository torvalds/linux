// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 - Linaro and Columbia University
 * Author: Jintack Lim <jintack.lim@linaro.org>
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>

#include "hyp/include/hyp/adjust_pc.h"

#include "trace.h"

enum trap_behaviour {
	BEHAVE_HANDLE_LOCALLY	= 0,
	BEHAVE_FORWARD_READ	= BIT(0),
	BEHAVE_FORWARD_WRITE	= BIT(1),
	BEHAVE_FORWARD_ANY	= BEHAVE_FORWARD_READ | BEHAVE_FORWARD_WRITE,
};

struct trap_bits {
	const enum vcpu_sysreg		index;
	const enum trap_behaviour	behaviour;
	const u64			value;
	const u64			mask;
};

/* Coarse Grained Trap definitions */
enum cgt_group_id {
	/* Indicates no coarse trap control */
	__RESERVED__,

	/*
	 * The first batch of IDs denote coarse trapping that are used
	 * on their own instead of being part of a combination of
	 * trap controls.
	 */

	/*
	 * Anything after this point is a combination of coarse trap
	 * controls, which must all be evaluated to decide what to do.
	 */
	__MULTIPLE_CONTROL_BITS__,

	/*
	 * Anything after this point requires a callback evaluating a
	 * complex trap condition. Hopefully we'll never need this...
	 */
	__COMPLEX_CONDITIONS__,

	/* Must be last */
	__NR_CGT_GROUP_IDS__
};

static const struct trap_bits coarse_trap_bits[] = {
};

#define MCB(id, ...)						\
	[id - __MULTIPLE_CONTROL_BITS__]	=		\
		(const enum cgt_group_id[]){			\
		__VA_ARGS__, __RESERVED__			\
		}

static const enum cgt_group_id *coarse_control_combo[] = {
};

typedef enum trap_behaviour (*complex_condition_check)(struct kvm_vcpu *);

#define CCC(id, fn)				\
	[id - __COMPLEX_CONDITIONS__] = fn

static const complex_condition_check ccc[] = {
};

/*
 * Bit assignment for the trap controls. We use a 64bit word with the
 * following layout for each trapped sysreg:
 *
 * [9:0]	enum cgt_group_id (10 bits)
 * [62:10]	Unused (53 bits)
 * [63]		RES0 - Must be zero, as lost on insertion in the xarray
 */
#define TC_CGT_BITS	10

union trap_config {
	u64	val;
	struct {
		unsigned long	cgt:TC_CGT_BITS; /* Coarse Grained Trap id */
		unsigned long	unused:53;	 /* Unused, should be zero */
		unsigned long	mbz:1;		 /* Must Be Zero */
	};
};

struct encoding_to_trap_config {
	const u32			encoding;
	const u32			end;
	const union trap_config		tc;
	const unsigned int		line;
};

#define SR_RANGE_TRAP(sr_start, sr_end, trap_id)			\
	{								\
		.encoding	= sr_start,				\
		.end		= sr_end,				\
		.tc		= {					\
			.cgt		= trap_id,			\
		},							\
		.line = __LINE__,					\
	}

#define SR_TRAP(sr, trap_id)		SR_RANGE_TRAP(sr, sr, trap_id)

/*
 * Map encoding to trap bits for exception reported with EC=0x18.
 * These must only be evaluated when running a nested hypervisor, but
 * that the current context is not a hypervisor context. When the
 * trapped access matches one of the trap controls, the exception is
 * re-injected in the nested hypervisor.
 */
static const struct encoding_to_trap_config encoding_to_cgt[] __initconst = {
};

static DEFINE_XARRAY(sr_forward_xa);

static union trap_config get_trap_config(u32 sysreg)
{
	return (union trap_config) {
		.val = xa_to_value(xa_load(&sr_forward_xa, sysreg)),
	};
}

static __init void print_nv_trap_error(const struct encoding_to_trap_config *tc,
				       const char *type, int err)
{
	kvm_err("%s line %d encoding range "
		"(%d, %d, %d, %d, %d) - (%d, %d, %d, %d, %d) (err=%d)\n",
		type, tc->line,
		sys_reg_Op0(tc->encoding), sys_reg_Op1(tc->encoding),
		sys_reg_CRn(tc->encoding), sys_reg_CRm(tc->encoding),
		sys_reg_Op2(tc->encoding),
		sys_reg_Op0(tc->end), sys_reg_Op1(tc->end),
		sys_reg_CRn(tc->end), sys_reg_CRm(tc->end),
		sys_reg_Op2(tc->end),
		err);
}

int __init populate_nv_trap_config(void)
{
	int ret = 0;

	BUILD_BUG_ON(sizeof(union trap_config) != sizeof(void *));
	BUILD_BUG_ON(__NR_CGT_GROUP_IDS__ > BIT(TC_CGT_BITS));

	for (int i = 0; i < ARRAY_SIZE(encoding_to_cgt); i++) {
		const struct encoding_to_trap_config *cgt = &encoding_to_cgt[i];
		void *prev;

		if (cgt->tc.val & BIT(63)) {
			kvm_err("CGT[%d] has MBZ bit set\n", i);
			ret = -EINVAL;
		}

		if (cgt->encoding != cgt->end) {
			prev = xa_store_range(&sr_forward_xa,
					      cgt->encoding, cgt->end,
					      xa_mk_value(cgt->tc.val),
					      GFP_KERNEL);
		} else {
			prev = xa_store(&sr_forward_xa, cgt->encoding,
					xa_mk_value(cgt->tc.val), GFP_KERNEL);
			if (prev && !xa_is_err(prev)) {
				ret = -EINVAL;
				print_nv_trap_error(cgt, "Duplicate CGT", ret);
			}
		}

		if (xa_is_err(prev)) {
			ret = xa_err(prev);
			print_nv_trap_error(cgt, "Failed CGT insertion", ret);
		}
	}

	kvm_info("nv: %ld coarse grained trap handlers\n",
		 ARRAY_SIZE(encoding_to_cgt));

	for (int id = __MULTIPLE_CONTROL_BITS__; id < __COMPLEX_CONDITIONS__; id++) {
		const enum cgt_group_id *cgids;

		cgids = coarse_control_combo[id - __MULTIPLE_CONTROL_BITS__];

		for (int i = 0; cgids[i] != __RESERVED__; i++) {
			if (cgids[i] >= __MULTIPLE_CONTROL_BITS__) {
				kvm_err("Recursive MCB %d/%d\n", id, cgids[i]);
				ret = -EINVAL;
			}
		}
	}

	if (ret)
		xa_destroy(&sr_forward_xa);

	return ret;
}

static enum trap_behaviour get_behaviour(struct kvm_vcpu *vcpu,
					 const struct trap_bits *tb)
{
	enum trap_behaviour b = BEHAVE_HANDLE_LOCALLY;
	u64 val;

	val = __vcpu_sys_reg(vcpu, tb->index);
	if ((val & tb->mask) == tb->value)
		b |= tb->behaviour;

	return b;
}

static enum trap_behaviour __compute_trap_behaviour(struct kvm_vcpu *vcpu,
						    const enum cgt_group_id id,
						    enum trap_behaviour b)
{
	switch (id) {
		const enum cgt_group_id *cgids;

	case __RESERVED__ ... __MULTIPLE_CONTROL_BITS__ - 1:
		if (likely(id != __RESERVED__))
			b |= get_behaviour(vcpu, &coarse_trap_bits[id]);
		break;
	case __MULTIPLE_CONTROL_BITS__ ... __COMPLEX_CONDITIONS__ - 1:
		/* Yes, this is recursive. Don't do anything stupid. */
		cgids = coarse_control_combo[id - __MULTIPLE_CONTROL_BITS__];
		for (int i = 0; cgids[i] != __RESERVED__; i++)
			b |= __compute_trap_behaviour(vcpu, cgids[i], b);
		break;
	default:
		if (ARRAY_SIZE(ccc))
			b |= ccc[id -  __COMPLEX_CONDITIONS__](vcpu);
		break;
	}

	return b;
}

static enum trap_behaviour compute_trap_behaviour(struct kvm_vcpu *vcpu,
						  const union trap_config tc)
{
	enum trap_behaviour b = BEHAVE_HANDLE_LOCALLY;

	return __compute_trap_behaviour(vcpu, tc.cgt, b);
}

bool __check_nv_sr_forward(struct kvm_vcpu *vcpu)
{
	union trap_config tc;
	enum trap_behaviour b;
	bool is_read;
	u32 sysreg;
	u64 esr;

	if (!vcpu_has_nv(vcpu) || is_hyp_ctxt(vcpu))
		return false;

	esr = kvm_vcpu_get_esr(vcpu);
	sysreg = esr_sys64_to_sysreg(esr);
	is_read = (esr & ESR_ELx_SYS64_ISS_DIR_MASK) == ESR_ELx_SYS64_ISS_DIR_READ;

	tc = get_trap_config(sysreg);

	/*
	 * A value of 0 for the whole entry means that we know nothing
	 * for this sysreg, and that it cannot be re-injected into the
	 * nested hypervisor. In this situation, let's cut it short.
	 *
	 * Note that ultimately, we could also make use of the xarray
	 * to store the index of the sysreg in the local descriptor
	 * array, avoiding another search... Hint, hint...
	 */
	if (!tc.val)
		return false;

	b = compute_trap_behaviour(vcpu, tc);

	if (((b & BEHAVE_FORWARD_READ) && is_read) ||
	    ((b & BEHAVE_FORWARD_WRITE) && !is_read))
		goto inject;

	return false;

inject:
	trace_kvm_forward_sysreg_trap(vcpu, sysreg, is_read);

	kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));
	return true;
}

static u64 kvm_check_illegal_exception_return(struct kvm_vcpu *vcpu, u64 spsr)
{
	u64 mode = spsr & PSR_MODE_MASK;

	/*
	 * Possible causes for an Illegal Exception Return from EL2:
	 * - trying to return to EL3
	 * - trying to return to an illegal M value
	 * - trying to return to a 32bit EL
	 * - trying to return to EL1 with HCR_EL2.TGE set
	 */
	if (mode == PSR_MODE_EL3t   || mode == PSR_MODE_EL3h ||
	    mode == 0b00001         || (mode & BIT(1))       ||
	    (spsr & PSR_MODE32_BIT) ||
	    (vcpu_el2_tge_is_set(vcpu) && (mode == PSR_MODE_EL1t ||
					   mode == PSR_MODE_EL1h))) {
		/*
		 * The guest is playing with our nerves. Preserve EL, SP,
		 * masks, flags from the existing PSTATE, and set IL.
		 * The HW will then generate an Illegal State Exception
		 * immediately after ERET.
		 */
		spsr = *vcpu_cpsr(vcpu);

		spsr &= (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT |
			 PSR_N_BIT | PSR_Z_BIT | PSR_C_BIT | PSR_V_BIT |
			 PSR_MODE_MASK | PSR_MODE32_BIT);
		spsr |= PSR_IL_BIT;
	}

	return spsr;
}

void kvm_emulate_nested_eret(struct kvm_vcpu *vcpu)
{
	u64 spsr, elr, mode;
	bool direct_eret;

	/*
	 * Going through the whole put/load motions is a waste of time
	 * if this is a VHE guest hypervisor returning to its own
	 * userspace, or the hypervisor performing a local exception
	 * return. No need to save/restore registers, no need to
	 * switch S2 MMU. Just do the canonical ERET.
	 */
	spsr = vcpu_read_sys_reg(vcpu, SPSR_EL2);
	spsr = kvm_check_illegal_exception_return(vcpu, spsr);

	mode = spsr & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_eret  = (mode == PSR_MODE_EL0t &&
			vcpu_el2_e2h_is_set(vcpu) &&
			vcpu_el2_tge_is_set(vcpu));
	direct_eret |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_eret) {
		*vcpu_pc(vcpu) = vcpu_read_sys_reg(vcpu, ELR_EL2);
		*vcpu_cpsr(vcpu) = spsr;
		trace_kvm_nested_eret(vcpu, *vcpu_pc(vcpu), spsr);
		return;
	}

	preempt_disable();
	kvm_arch_vcpu_put(vcpu);

	elr = __vcpu_sys_reg(vcpu, ELR_EL2);

	trace_kvm_nested_eret(vcpu, elr, spsr);

	/*
	 * Note that the current exception level is always the virtual EL2,
	 * since we set HCR_EL2.NV bit only when entering the virtual EL2.
	 */
	*vcpu_pc(vcpu) = elr;
	*vcpu_cpsr(vcpu) = spsr;

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();
}

static void kvm_inject_el2_exception(struct kvm_vcpu *vcpu, u64 esr_el2,
				     enum exception_type type)
{
	trace_kvm_inject_nested_exception(vcpu, esr_el2, type);

	switch (type) {
	case except_type_sync:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_SYNC);
		vcpu_write_sys_reg(vcpu, esr_el2, ESR_EL2);
		break;
	case except_type_irq:
		kvm_pend_exception(vcpu, EXCEPT_AA64_EL2_IRQ);
		break;
	default:
		WARN_ONCE(1, "Unsupported EL2 exception injection %d\n", type);
	}
}

/*
 * Emulate taking an exception to EL2.
 * See ARM ARM J8.1.2 AArch64.TakeException()
 */
static int kvm_inject_nested(struct kvm_vcpu *vcpu, u64 esr_el2,
			     enum exception_type type)
{
	u64 pstate, mode;
	bool direct_inject;

	if (!vcpu_has_nv(vcpu)) {
		kvm_err("Unexpected call to %s for the non-nesting configuration\n",
				__func__);
		return -EINVAL;
	}

	/*
	 * As for ERET, we can avoid doing too much on the injection path by
	 * checking that we either took the exception from a VHE host
	 * userspace or from vEL2. In these cases, there is no change in
	 * translation regime (or anything else), so let's do as little as
	 * possible.
	 */
	pstate = *vcpu_cpsr(vcpu);
	mode = pstate & (PSR_MODE_MASK | PSR_MODE32_BIT);

	direct_inject  = (mode == PSR_MODE_EL0t &&
			  vcpu_el2_e2h_is_set(vcpu) &&
			  vcpu_el2_tge_is_set(vcpu));
	direct_inject |= (mode == PSR_MODE_EL2h || mode == PSR_MODE_EL2t);

	if (direct_inject) {
		kvm_inject_el2_exception(vcpu, esr_el2, type);
		return 1;
	}

	preempt_disable();

	/*
	 * We may have an exception or PC update in the EL0/EL1 context.
	 * Commit it before entering EL2.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_put(vcpu);

	kvm_inject_el2_exception(vcpu, esr_el2, type);

	/*
	 * A hard requirement is that a switch between EL1 and EL2
	 * contexts has to happen between a put/load, so that we can
	 * pick the correct timer and interrupt configuration, among
	 * other things.
	 *
	 * Make sure the exception actually took place before we load
	 * the new context.
	 */
	__kvm_adjust_pc(vcpu);

	kvm_arch_vcpu_load(vcpu, smp_processor_id());
	preempt_enable();

	return 1;
}

int kvm_inject_nested_sync(struct kvm_vcpu *vcpu, u64 esr_el2)
{
	return kvm_inject_nested(vcpu, esr_el2, except_type_sync);
}

int kvm_inject_nested_irq(struct kvm_vcpu *vcpu)
{
	/*
	 * Do not inject an irq if the:
	 *  - Current exception level is EL2, and
	 *  - virtual HCR_EL2.TGE == 0
	 *  - virtual HCR_EL2.IMO == 0
	 *
	 * See Table D1-17 "Physical interrupt target and masking when EL3 is
	 * not implemented and EL2 is implemented" in ARM DDI 0487C.a.
	 */

	if (vcpu_is_el2(vcpu) && !vcpu_el2_tge_is_set(vcpu) &&
	    !(__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_IMO))
		return 1;

	/* esr_el2 value doesn't matter for exits due to irqs. */
	return kvm_inject_nested(vcpu, 0, except_type_irq);
}
