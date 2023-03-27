// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/guest.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/nospec.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <kvm/arm_hypercalls.h>
#include <asm/cputype.h>
#include <linux/uaccess.h>
#include <asm/fpsimd.h>
#include <asm/kvm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>
#include <asm/sigcontext.h>

#include "trace.h"

const struct _kvm_stats_desc kvm_vm_stats_desc[] = {
	KVM_GENERIC_VM_STATS()
};

const struct kvm_stats_header kvm_vm_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vm_stats_desc),
	.id_offset =  sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vm_stats_desc),
};

const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS(),
	STATS_DESC_COUNTER(VCPU, hvc_exit_stat),
	STATS_DESC_COUNTER(VCPU, wfe_exit_stat),
	STATS_DESC_COUNTER(VCPU, wfi_exit_stat),
	STATS_DESC_COUNTER(VCPU, mmio_exit_user),
	STATS_DESC_COUNTER(VCPU, mmio_exit_kernel),
	STATS_DESC_COUNTER(VCPU, signal_exits),
	STATS_DESC_COUNTER(VCPU, exits)
};

const struct kvm_stats_header kvm_vcpu_stats_header = {
	.name_size = KVM_STATS_NAME_SIZE,
	.num_desc = ARRAY_SIZE(kvm_vcpu_stats_desc),
	.id_offset = sizeof(struct kvm_stats_header),
	.desc_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE,
	.data_offset = sizeof(struct kvm_stats_header) + KVM_STATS_NAME_SIZE +
		       sizeof(kvm_vcpu_stats_desc),
};

static bool core_reg_offset_is_vreg(u64 off)
{
	return off >= KVM_REG_ARM_CORE_REG(fp_regs.vregs) &&
		off < KVM_REG_ARM_CORE_REG(fp_regs.fpsr);
}

static u64 core_reg_offset_from_id(u64 id)
{
	return id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK | KVM_REG_ARM_CORE);
}

static int core_reg_size_from_offset(const struct kvm_vcpu *vcpu, u64 off)
{
	int size;

	switch (off) {
	case KVM_REG_ARM_CORE_REG(regs.regs[0]) ...
	     KVM_REG_ARM_CORE_REG(regs.regs[30]):
	case KVM_REG_ARM_CORE_REG(regs.sp):
	case KVM_REG_ARM_CORE_REG(regs.pc):
	case KVM_REG_ARM_CORE_REG(regs.pstate):
	case KVM_REG_ARM_CORE_REG(sp_el1):
	case KVM_REG_ARM_CORE_REG(elr_el1):
	case KVM_REG_ARM_CORE_REG(spsr[0]) ...
	     KVM_REG_ARM_CORE_REG(spsr[KVM_NR_SPSR - 1]):
		size = sizeof(__u64);
		break;

	case KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]) ...
	     KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]):
		size = sizeof(__uint128_t);
		break;

	case KVM_REG_ARM_CORE_REG(fp_regs.fpsr):
	case KVM_REG_ARM_CORE_REG(fp_regs.fpcr):
		size = sizeof(__u32);
		break;

	default:
		return -EINVAL;
	}

	if (!IS_ALIGNED(off, size / sizeof(__u32)))
		return -EINVAL;

	/*
	 * The KVM_REG_ARM64_SVE regs must be used instead of
	 * KVM_REG_ARM_CORE for accessing the FPSIMD V-registers on
	 * SVE-enabled vcpus:
	 */
	if (vcpu_has_sve(vcpu) && core_reg_offset_is_vreg(off))
		return -EINVAL;

	return size;
}

static void *core_reg_addr(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	u64 off = core_reg_offset_from_id(reg->id);
	int size = core_reg_size_from_offset(vcpu, off);

	if (size < 0)
		return NULL;

	if (KVM_REG_SIZE(reg->id) != size)
		return NULL;

	switch (off) {
	case KVM_REG_ARM_CORE_REG(regs.regs[0]) ...
	     KVM_REG_ARM_CORE_REG(regs.regs[30]):
		off -= KVM_REG_ARM_CORE_REG(regs.regs[0]);
		off /= 2;
		return &vcpu->arch.ctxt.regs.regs[off];

	case KVM_REG_ARM_CORE_REG(regs.sp):
		return &vcpu->arch.ctxt.regs.sp;

	case KVM_REG_ARM_CORE_REG(regs.pc):
		return &vcpu->arch.ctxt.regs.pc;

	case KVM_REG_ARM_CORE_REG(regs.pstate):
		return &vcpu->arch.ctxt.regs.pstate;

	case KVM_REG_ARM_CORE_REG(sp_el1):
		return __ctxt_sys_reg(&vcpu->arch.ctxt, SP_EL1);

	case KVM_REG_ARM_CORE_REG(elr_el1):
		return __ctxt_sys_reg(&vcpu->arch.ctxt, ELR_EL1);

	case KVM_REG_ARM_CORE_REG(spsr[KVM_SPSR_EL1]):
		return __ctxt_sys_reg(&vcpu->arch.ctxt, SPSR_EL1);

	case KVM_REG_ARM_CORE_REG(spsr[KVM_SPSR_ABT]):
		return &vcpu->arch.ctxt.spsr_abt;

	case KVM_REG_ARM_CORE_REG(spsr[KVM_SPSR_UND]):
		return &vcpu->arch.ctxt.spsr_und;

	case KVM_REG_ARM_CORE_REG(spsr[KVM_SPSR_IRQ]):
		return &vcpu->arch.ctxt.spsr_irq;

	case KVM_REG_ARM_CORE_REG(spsr[KVM_SPSR_FIQ]):
		return &vcpu->arch.ctxt.spsr_fiq;

	case KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]) ...
	     KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]):
		off -= KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]);
		off /= 4;
		return &vcpu->arch.ctxt.fp_regs.vregs[off];

	case KVM_REG_ARM_CORE_REG(fp_regs.fpsr):
		return &vcpu->arch.ctxt.fp_regs.fpsr;

	case KVM_REG_ARM_CORE_REG(fp_regs.fpcr):
		return &vcpu->arch.ctxt.fp_regs.fpcr;

	default:
		return NULL;
	}
}

static int get_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/*
	 * Because the kvm_regs structure is a mix of 32, 64 and
	 * 128bit fields, we index it as if it was a 32bit
	 * array. Hence below, nr_regs is the number of entries, and
	 * off the index in the "array".
	 */
	__u32 __user *uaddr = (__u32 __user *)(unsigned long)reg->addr;
	int nr_regs = sizeof(struct kvm_regs) / sizeof(__u32);
	void *addr;
	u32 off;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= nr_regs ||
	    (off + (KVM_REG_SIZE(reg->id) / sizeof(__u32))) >= nr_regs)
		return -ENOENT;

	addr = core_reg_addr(vcpu, reg);
	if (!addr)
		return -EINVAL;

	if (copy_to_user(uaddr, addr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int set_core_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	__u32 __user *uaddr = (__u32 __user *)(unsigned long)reg->addr;
	int nr_regs = sizeof(struct kvm_regs) / sizeof(__u32);
	__uint128_t tmp;
	void *valp = &tmp, *addr;
	u64 off;
	int err = 0;

	/* Our ID is an index into the kvm_regs struct. */
	off = core_reg_offset_from_id(reg->id);
	if (off >= nr_regs ||
	    (off + (KVM_REG_SIZE(reg->id) / sizeof(__u32))) >= nr_regs)
		return -ENOENT;

	addr = core_reg_addr(vcpu, reg);
	if (!addr)
		return -EINVAL;

	if (KVM_REG_SIZE(reg->id) > sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(valp, uaddr, KVM_REG_SIZE(reg->id))) {
		err = -EFAULT;
		goto out;
	}

	if (off == KVM_REG_ARM_CORE_REG(regs.pstate)) {
		u64 mode = (*(u64 *)valp) & PSR_AA32_MODE_MASK;
		switch (mode) {
		case PSR_AA32_MODE_USR:
			if (!kvm_supports_32bit_el0())
				return -EINVAL;
			break;
		case PSR_AA32_MODE_FIQ:
		case PSR_AA32_MODE_IRQ:
		case PSR_AA32_MODE_SVC:
		case PSR_AA32_MODE_ABT:
		case PSR_AA32_MODE_UND:
			if (!vcpu_el1_is_32bit(vcpu))
				return -EINVAL;
			break;
		case PSR_MODE_EL2h:
		case PSR_MODE_EL2t:
			if (!vcpu_has_nv(vcpu))
				return -EINVAL;
			fallthrough;
		case PSR_MODE_EL0t:
		case PSR_MODE_EL1t:
		case PSR_MODE_EL1h:
			if (vcpu_el1_is_32bit(vcpu))
				return -EINVAL;
			break;
		default:
			err = -EINVAL;
			goto out;
		}
	}

	memcpy(addr, valp, KVM_REG_SIZE(reg->id));

	if (*vcpu_cpsr(vcpu) & PSR_MODE32_BIT) {
		int i, nr_reg;

		switch (*vcpu_cpsr(vcpu)) {
		/*
		 * Either we are dealing with user mode, and only the
		 * first 15 registers (+ PC) must be narrowed to 32bit.
		 * AArch32 r0-r14 conveniently map to AArch64 x0-x14.
		 */
		case PSR_AA32_MODE_USR:
		case PSR_AA32_MODE_SYS:
			nr_reg = 15;
			break;

		/*
		 * Otherwise, this is a privileged mode, and *all* the
		 * registers must be narrowed to 32bit.
		 */
		default:
			nr_reg = 31;
			break;
		}

		for (i = 0; i < nr_reg; i++)
			vcpu_set_reg(vcpu, i, (u32)vcpu_get_reg(vcpu, i));

		*vcpu_pc(vcpu) = (u32)*vcpu_pc(vcpu);
	}
out:
	return err;
}

#define vq_word(vq) (((vq) - SVE_VQ_MIN) / 64)
#define vq_mask(vq) ((u64)1 << ((vq) - SVE_VQ_MIN) % 64)
#define vq_present(vqs, vq) (!!((vqs)[vq_word(vq)] & vq_mask(vq)))

static int get_sve_vls(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	unsigned int max_vq, vq;
	u64 vqs[KVM_ARM64_SVE_VLS_WORDS];

	if (!vcpu_has_sve(vcpu))
		return -ENOENT;

	if (WARN_ON(!sve_vl_valid(vcpu->arch.sve_max_vl)))
		return -EINVAL;

	memset(vqs, 0, sizeof(vqs));

	max_vq = vcpu_sve_max_vq(vcpu);
	for (vq = SVE_VQ_MIN; vq <= max_vq; ++vq)
		if (sve_vq_available(vq))
			vqs[vq_word(vq)] |= vq_mask(vq);

	if (copy_to_user((void __user *)reg->addr, vqs, sizeof(vqs)))
		return -EFAULT;

	return 0;
}

static int set_sve_vls(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	unsigned int max_vq, vq;
	u64 vqs[KVM_ARM64_SVE_VLS_WORDS];

	if (!vcpu_has_sve(vcpu))
		return -ENOENT;

	if (kvm_arm_vcpu_sve_finalized(vcpu))
		return -EPERM; /* too late! */

	if (WARN_ON(vcpu->arch.sve_state))
		return -EINVAL;

	if (copy_from_user(vqs, (const void __user *)reg->addr, sizeof(vqs)))
		return -EFAULT;

	max_vq = 0;
	for (vq = SVE_VQ_MIN; vq <= SVE_VQ_MAX; ++vq)
		if (vq_present(vqs, vq))
			max_vq = vq;

	if (max_vq > sve_vq_from_vl(kvm_sve_max_vl))
		return -EINVAL;

	/*
	 * Vector lengths supported by the host can't currently be
	 * hidden from the guest individually: instead we can only set a
	 * maximum via ZCR_EL2.LEN.  So, make sure the available vector
	 * lengths match the set requested exactly up to the requested
	 * maximum:
	 */
	for (vq = SVE_VQ_MIN; vq <= max_vq; ++vq)
		if (vq_present(vqs, vq) != sve_vq_available(vq))
			return -EINVAL;

	/* Can't run with no vector lengths at all: */
	if (max_vq < SVE_VQ_MIN)
		return -EINVAL;

	/* vcpu->arch.sve_state will be alloc'd by kvm_vcpu_finalize_sve() */
	vcpu->arch.sve_max_vl = sve_vl_from_vq(max_vq);

	return 0;
}

#define SVE_REG_SLICE_SHIFT	0
#define SVE_REG_SLICE_BITS	5
#define SVE_REG_ID_SHIFT	(SVE_REG_SLICE_SHIFT + SVE_REG_SLICE_BITS)
#define SVE_REG_ID_BITS		5

#define SVE_REG_SLICE_MASK					\
	GENMASK(SVE_REG_SLICE_SHIFT + SVE_REG_SLICE_BITS - 1,	\
		SVE_REG_SLICE_SHIFT)
#define SVE_REG_ID_MASK							\
	GENMASK(SVE_REG_ID_SHIFT + SVE_REG_ID_BITS - 1, SVE_REG_ID_SHIFT)

#define SVE_NUM_SLICES (1 << SVE_REG_SLICE_BITS)

#define KVM_SVE_ZREG_SIZE KVM_REG_SIZE(KVM_REG_ARM64_SVE_ZREG(0, 0))
#define KVM_SVE_PREG_SIZE KVM_REG_SIZE(KVM_REG_ARM64_SVE_PREG(0, 0))

/*
 * Number of register slices required to cover each whole SVE register.
 * NOTE: Only the first slice every exists, for now.
 * If you are tempted to modify this, you must also rework sve_reg_to_region()
 * to match:
 */
#define vcpu_sve_slices(vcpu) 1

/* Bounds of a single SVE register slice within vcpu->arch.sve_state */
struct sve_state_reg_region {
	unsigned int koffset;	/* offset into sve_state in kernel memory */
	unsigned int klen;	/* length in kernel memory */
	unsigned int upad;	/* extra trailing padding in user memory */
};

/*
 * Validate SVE register ID and get sanitised bounds for user/kernel SVE
 * register copy
 */
static int sve_reg_to_region(struct sve_state_reg_region *region,
			     struct kvm_vcpu *vcpu,
			     const struct kvm_one_reg *reg)
{
	/* reg ID ranges for Z- registers */
	const u64 zreg_id_min = KVM_REG_ARM64_SVE_ZREG(0, 0);
	const u64 zreg_id_max = KVM_REG_ARM64_SVE_ZREG(SVE_NUM_ZREGS - 1,
						       SVE_NUM_SLICES - 1);

	/* reg ID ranges for P- registers and FFR (which are contiguous) */
	const u64 preg_id_min = KVM_REG_ARM64_SVE_PREG(0, 0);
	const u64 preg_id_max = KVM_REG_ARM64_SVE_FFR(SVE_NUM_SLICES - 1);

	unsigned int vq;
	unsigned int reg_num;

	unsigned int reqoffset, reqlen; /* User-requested offset and length */
	unsigned int maxlen; /* Maximum permitted length */

	size_t sve_state_size;

	const u64 last_preg_id = KVM_REG_ARM64_SVE_PREG(SVE_NUM_PREGS - 1,
							SVE_NUM_SLICES - 1);

	/* Verify that the P-regs and FFR really do have contiguous IDs: */
	BUILD_BUG_ON(KVM_REG_ARM64_SVE_FFR(0) != last_preg_id + 1);

	/* Verify that we match the UAPI header: */
	BUILD_BUG_ON(SVE_NUM_SLICES != KVM_ARM64_SVE_MAX_SLICES);

	reg_num = (reg->id & SVE_REG_ID_MASK) >> SVE_REG_ID_SHIFT;

	if (reg->id >= zreg_id_min && reg->id <= zreg_id_max) {
		if (!vcpu_has_sve(vcpu) || (reg->id & SVE_REG_SLICE_MASK) > 0)
			return -ENOENT;

		vq = vcpu_sve_max_vq(vcpu);

		reqoffset = SVE_SIG_ZREG_OFFSET(vq, reg_num) -
				SVE_SIG_REGS_OFFSET;
		reqlen = KVM_SVE_ZREG_SIZE;
		maxlen = SVE_SIG_ZREG_SIZE(vq);
	} else if (reg->id >= preg_id_min && reg->id <= preg_id_max) {
		if (!vcpu_has_sve(vcpu) || (reg->id & SVE_REG_SLICE_MASK) > 0)
			return -ENOENT;

		vq = vcpu_sve_max_vq(vcpu);

		reqoffset = SVE_SIG_PREG_OFFSET(vq, reg_num) -
				SVE_SIG_REGS_OFFSET;
		reqlen = KVM_SVE_PREG_SIZE;
		maxlen = SVE_SIG_PREG_SIZE(vq);
	} else {
		return -EINVAL;
	}

	sve_state_size = vcpu_sve_state_size(vcpu);
	if (WARN_ON(!sve_state_size))
		return -EINVAL;

	region->koffset = array_index_nospec(reqoffset, sve_state_size);
	region->klen = min(maxlen, reqlen);
	region->upad = reqlen - region->klen;

	return 0;
}

static int get_sve_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	int ret;
	struct sve_state_reg_region region;
	char __user *uptr = (char __user *)reg->addr;

	/* Handle the KVM_REG_ARM64_SVE_VLS pseudo-reg as a special case: */
	if (reg->id == KVM_REG_ARM64_SVE_VLS)
		return get_sve_vls(vcpu, reg);

	/* Try to interpret reg ID as an architectural SVE register... */
	ret = sve_reg_to_region(&region, vcpu, reg);
	if (ret)
		return ret;

	if (!kvm_arm_vcpu_sve_finalized(vcpu))
		return -EPERM;

	if (copy_to_user(uptr, vcpu->arch.sve_state + region.koffset,
			 region.klen) ||
	    clear_user(uptr + region.klen, region.upad))
		return -EFAULT;

	return 0;
}

static int set_sve_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	int ret;
	struct sve_state_reg_region region;
	const char __user *uptr = (const char __user *)reg->addr;

	/* Handle the KVM_REG_ARM64_SVE_VLS pseudo-reg as a special case: */
	if (reg->id == KVM_REG_ARM64_SVE_VLS)
		return set_sve_vls(vcpu, reg);

	/* Try to interpret reg ID as an architectural SVE register... */
	ret = sve_reg_to_region(&region, vcpu, reg);
	if (ret)
		return ret;

	if (!kvm_arm_vcpu_sve_finalized(vcpu))
		return -EPERM;

	if (copy_from_user(vcpu->arch.sve_state + region.koffset, uptr,
			   region.klen))
		return -EFAULT;

	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

static int copy_core_reg_indices(const struct kvm_vcpu *vcpu,
				 u64 __user *uindices)
{
	unsigned int i;
	int n = 0;

	for (i = 0; i < sizeof(struct kvm_regs) / sizeof(__u32); i++) {
		u64 reg = KVM_REG_ARM64 | KVM_REG_ARM_CORE | i;
		int size = core_reg_size_from_offset(vcpu, i);

		if (size < 0)
			continue;

		switch (size) {
		case sizeof(__u32):
			reg |= KVM_REG_SIZE_U32;
			break;

		case sizeof(__u64):
			reg |= KVM_REG_SIZE_U64;
			break;

		case sizeof(__uint128_t):
			reg |= KVM_REG_SIZE_U128;
			break;

		default:
			WARN_ON(1);
			continue;
		}

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}

		n++;
	}

	return n;
}

static unsigned long num_core_regs(const struct kvm_vcpu *vcpu)
{
	return copy_core_reg_indices(vcpu, NULL);
}

/**
 * ARM64 versions of the TIMER registers, always available on arm64
 */

#define NUM_TIMER_REGS 3

static bool is_timer_reg(u64 index)
{
	switch (index) {
	case KVM_REG_ARM_TIMER_CTL:
	case KVM_REG_ARM_TIMER_CNT:
	case KVM_REG_ARM_TIMER_CVAL:
		return true;
	}
	return false;
}

static int copy_timer_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	if (put_user(KVM_REG_ARM_TIMER_CTL, uindices))
		return -EFAULT;
	uindices++;
	if (put_user(KVM_REG_ARM_TIMER_CNT, uindices))
		return -EFAULT;
	uindices++;
	if (put_user(KVM_REG_ARM_TIMER_CVAL, uindices))
		return -EFAULT;

	return 0;
}

static int set_timer_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;
	int ret;

	ret = copy_from_user(&val, uaddr, KVM_REG_SIZE(reg->id));
	if (ret != 0)
		return -EFAULT;

	return kvm_arm_timer_set_reg(vcpu, reg->id, val);
}

static int get_timer_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(long)reg->addr;
	u64 val;

	val = kvm_arm_timer_get_reg(vcpu, reg->id);
	return copy_to_user(uaddr, &val, KVM_REG_SIZE(reg->id)) ? -EFAULT : 0;
}

static unsigned long num_sve_regs(const struct kvm_vcpu *vcpu)
{
	const unsigned int slices = vcpu_sve_slices(vcpu);

	if (!vcpu_has_sve(vcpu))
		return 0;

	/* Policed by KVM_GET_REG_LIST: */
	WARN_ON(!kvm_arm_vcpu_sve_finalized(vcpu));

	return slices * (SVE_NUM_PREGS + SVE_NUM_ZREGS + 1 /* FFR */)
		+ 1; /* KVM_REG_ARM64_SVE_VLS */
}

static int copy_sve_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	const unsigned int slices = vcpu_sve_slices(vcpu);
	u64 reg;
	unsigned int i, n;
	int num_regs = 0;

	if (!vcpu_has_sve(vcpu))
		return 0;

	/* Policed by KVM_GET_REG_LIST: */
	WARN_ON(!kvm_arm_vcpu_sve_finalized(vcpu));

	/*
	 * Enumerate this first, so that userspace can save/restore in
	 * the order reported by KVM_GET_REG_LIST:
	 */
	reg = KVM_REG_ARM64_SVE_VLS;
	if (put_user(reg, uindices++))
		return -EFAULT;
	++num_regs;

	for (i = 0; i < slices; i++) {
		for (n = 0; n < SVE_NUM_ZREGS; n++) {
			reg = KVM_REG_ARM64_SVE_ZREG(n, i);
			if (put_user(reg, uindices++))
				return -EFAULT;
			num_regs++;
		}

		for (n = 0; n < SVE_NUM_PREGS; n++) {
			reg = KVM_REG_ARM64_SVE_PREG(n, i);
			if (put_user(reg, uindices++))
				return -EFAULT;
			num_regs++;
		}

		reg = KVM_REG_ARM64_SVE_FFR(i);
		if (put_user(reg, uindices++))
			return -EFAULT;
		num_regs++;
	}

	return num_regs;
}

/**
 * kvm_arm_num_regs - how many registers do we present via KVM_GET_ONE_REG
 *
 * This is for all registers.
 */
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu)
{
	unsigned long res = 0;

	res += num_core_regs(vcpu);
	res += num_sve_regs(vcpu);
	res += kvm_arm_num_sys_reg_descs(vcpu);
	res += kvm_arm_get_fw_num_regs(vcpu);
	res += NUM_TIMER_REGS;

	return res;
}

/**
 * kvm_arm_copy_reg_indices - get indices of all registers.
 *
 * We do core registers right here, then we append system regs.
 */
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	int ret;

	ret = copy_core_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_sve_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = kvm_arm_copy_fw_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += kvm_arm_get_fw_num_regs(vcpu);

	ret = copy_timer_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += NUM_TIMER_REGS;

	return kvm_arm_copy_sys_reg_indices(vcpu, uindices);
}

int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM64 >> 32)
		return -EINVAL;

	switch (reg->id & KVM_REG_ARM_COPROC_MASK) {
	case KVM_REG_ARM_CORE:	return get_core_reg(vcpu, reg);
	case KVM_REG_ARM_FW:
	case KVM_REG_ARM_FW_FEAT_BMAP:
		return kvm_arm_get_fw_reg(vcpu, reg);
	case KVM_REG_ARM64_SVE:	return get_sve_reg(vcpu, reg);
	}

	if (is_timer_reg(reg->id))
		return get_timer_reg(vcpu, reg);

	return kvm_arm_sys_reg_get_reg(vcpu, reg);
}

int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	/* We currently use nothing arch-specific in upper 32 bits */
	if ((reg->id & ~KVM_REG_SIZE_MASK) >> 32 != KVM_REG_ARM64 >> 32)
		return -EINVAL;

	switch (reg->id & KVM_REG_ARM_COPROC_MASK) {
	case KVM_REG_ARM_CORE:	return set_core_reg(vcpu, reg);
	case KVM_REG_ARM_FW:
	case KVM_REG_ARM_FW_FEAT_BMAP:
		return kvm_arm_set_fw_reg(vcpu, reg);
	case KVM_REG_ARM64_SVE:	return set_sve_reg(vcpu, reg);
	}

	if (is_timer_reg(reg->id))
		return set_timer_reg(vcpu, reg);

	return kvm_arm_sys_reg_set_reg(vcpu, reg);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -EINVAL;
}

int __kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events)
{
	events->exception.serror_pending = !!(vcpu->arch.hcr_el2 & HCR_VSE);
	events->exception.serror_has_esr = cpus_have_const_cap(ARM64_HAS_RAS_EXTN);

	if (events->exception.serror_pending && events->exception.serror_has_esr)
		events->exception.serror_esr = vcpu_get_vsesr(vcpu);

	/*
	 * We never return a pending ext_dabt here because we deliver it to
	 * the virtual CPU directly when setting the event and it's no longer
	 * 'pending' at this point.
	 */

	return 0;
}

int __kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events)
{
	bool serror_pending = events->exception.serror_pending;
	bool has_esr = events->exception.serror_has_esr;
	bool ext_dabt_pending = events->exception.ext_dabt_pending;

	if (serror_pending && has_esr) {
		if (!cpus_have_const_cap(ARM64_HAS_RAS_EXTN))
			return -EINVAL;

		if (!((events->exception.serror_esr) & ~ESR_ELx_ISS_MASK))
			kvm_set_sei_esr(vcpu, events->exception.serror_esr);
		else
			return -EINVAL;
	} else if (serror_pending) {
		kvm_inject_vabt(vcpu);
	}

	if (ext_dabt_pending)
		kvm_inject_dabt(vcpu, kvm_vcpu_get_hfar(vcpu));

	return 0;
}

u32 __attribute_const__ kvm_target_cpu(void)
{
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_number = read_cpuid_part_number();

	switch (implementor) {
	case ARM_CPU_IMP_ARM:
		switch (part_number) {
		case ARM_CPU_PART_AEM_V8:
			return KVM_ARM_TARGET_AEM_V8;
		case ARM_CPU_PART_FOUNDATION:
			return KVM_ARM_TARGET_FOUNDATION_V8;
		case ARM_CPU_PART_CORTEX_A53:
			return KVM_ARM_TARGET_CORTEX_A53;
		case ARM_CPU_PART_CORTEX_A57:
			return KVM_ARM_TARGET_CORTEX_A57;
		}
		break;
	case ARM_CPU_IMP_APM:
		switch (part_number) {
		case APM_CPU_PART_POTENZA:
			return KVM_ARM_TARGET_XGENE_POTENZA;
		}
		break;
	}

	/* Return a default generic target */
	return KVM_ARM_TARGET_GENERIC_V8;
}

void kvm_vcpu_preferred_target(struct kvm_vcpu_init *init)
{
	u32 target = kvm_target_cpu();

	memset(init, 0, sizeof(*init));

	/*
	 * For now, we don't return any features.
	 * In future, we might use features to return target
	 * specific features available for the preferred
	 * target type.
	 */
	init->target = (__u32)target;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return -EINVAL;
}

/**
 * kvm_arch_vcpu_ioctl_set_guest_debug - set up guest debugging
 * @kvm:	pointer to the KVM struct
 * @kvm_guest_debug: the ioctl data buffer
 *
 * This sets up and enables the VM for guest debugging. Userspace
 * passes in a control flag to enable different debug types and
 * potentially other architecture specific information in the rest of
 * the structure.
 */
int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	int ret = 0;

	trace_kvm_set_guest_debug(vcpu, dbg->control);

	if (dbg->control & ~KVM_GUESTDBG_VALID_MASK) {
		ret = -EINVAL;
		goto out;
	}

	if (dbg->control & KVM_GUESTDBG_ENABLE) {
		vcpu->guest_debug = dbg->control;

		/* Hardware assisted Break and Watch points */
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW) {
			vcpu->arch.external_debug_state = dbg->arch;
		}

	} else {
		/* If not enabled clear all flags */
		vcpu->guest_debug = 0;
		vcpu_clear_flag(vcpu, DBG_SS_ACTIVE_PENDING);
	}

out:
	return ret;
}

int kvm_arm_vcpu_arch_set_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_ARM_VCPU_PMU_V3_CTRL:
		mutex_lock(&vcpu->kvm->arch.config_lock);
		ret = kvm_arm_pmu_v3_set_attr(vcpu, attr);
		mutex_unlock(&vcpu->kvm->arch.config_lock);
		break;
	case KVM_ARM_VCPU_TIMER_CTRL:
		ret = kvm_arm_timer_set_attr(vcpu, attr);
		break;
	case KVM_ARM_VCPU_PVTIME_CTRL:
		ret = kvm_arm_pvtime_set_attr(vcpu, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

int kvm_arm_vcpu_arch_get_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_ARM_VCPU_PMU_V3_CTRL:
		ret = kvm_arm_pmu_v3_get_attr(vcpu, attr);
		break;
	case KVM_ARM_VCPU_TIMER_CTRL:
		ret = kvm_arm_timer_get_attr(vcpu, attr);
		break;
	case KVM_ARM_VCPU_PVTIME_CTRL:
		ret = kvm_arm_pvtime_get_attr(vcpu, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

int kvm_arm_vcpu_arch_has_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_ARM_VCPU_PMU_V3_CTRL:
		ret = kvm_arm_pmu_v3_has_attr(vcpu, attr);
		break;
	case KVM_ARM_VCPU_TIMER_CTRL:
		ret = kvm_arm_timer_has_attr(vcpu, attr);
		break;
	case KVM_ARM_VCPU_PVTIME_CTRL:
		ret = kvm_arm_pvtime_has_attr(vcpu, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

long kvm_vm_ioctl_mte_copy_tags(struct kvm *kvm,
				struct kvm_arm_copy_mte_tags *copy_tags)
{
	gpa_t guest_ipa = copy_tags->guest_ipa;
	size_t length = copy_tags->length;
	void __user *tags = copy_tags->addr;
	gpa_t gfn;
	bool write = !(copy_tags->flags & KVM_ARM_TAGS_FROM_GUEST);
	int ret = 0;

	if (!kvm_has_mte(kvm))
		return -EINVAL;

	if (copy_tags->reserved[0] || copy_tags->reserved[1])
		return -EINVAL;

	if (copy_tags->flags & ~KVM_ARM_TAGS_FROM_GUEST)
		return -EINVAL;

	if (length & ~PAGE_MASK || guest_ipa & ~PAGE_MASK)
		return -EINVAL;

	gfn = gpa_to_gfn(guest_ipa);

	mutex_lock(&kvm->slots_lock);

	while (length > 0) {
		kvm_pfn_t pfn = gfn_to_pfn_prot(kvm, gfn, write, NULL);
		void *maddr;
		unsigned long num_tags;
		struct page *page;

		if (is_error_noslot_pfn(pfn)) {
			ret = -EFAULT;
			goto out;
		}

		page = pfn_to_online_page(pfn);
		if (!page) {
			/* Reject ZONE_DEVICE memory */
			ret = -EFAULT;
			goto out;
		}
		maddr = page_address(page);

		if (!write) {
			if (page_mte_tagged(page))
				num_tags = mte_copy_tags_to_user(tags, maddr,
							MTE_GRANULES_PER_PAGE);
			else
				/* No tags in memory, so write zeros */
				num_tags = MTE_GRANULES_PER_PAGE -
					clear_user(tags, MTE_GRANULES_PER_PAGE);
			kvm_release_pfn_clean(pfn);
		} else {
			/*
			 * Only locking to serialise with a concurrent
			 * set_pte_at() in the VMM but still overriding the
			 * tags, hence ignoring the return value.
			 */
			try_page_mte_tagging(page);
			num_tags = mte_copy_tags_from_user(maddr, tags,
							MTE_GRANULES_PER_PAGE);

			/* uaccess failed, don't leave stale tags */
			if (num_tags != MTE_GRANULES_PER_PAGE)
				mte_clear_page_tags(maddr);
			set_page_mte_tagged(page);

			kvm_release_pfn_dirty(pfn);
		}

		if (num_tags != MTE_GRANULES_PER_PAGE) {
			ret = -EFAULT;
			goto out;
		}

		gfn++;
		tags += num_tags;
		length -= PAGE_SIZE;
	}

out:
	mutex_unlock(&kvm->slots_lock);
	/* If some data has been copied report the number of bytes copied */
	if (length != copy_tags->length)
		return copy_tags->length - length;
	return ret;
}
