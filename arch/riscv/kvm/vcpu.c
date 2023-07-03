// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/bitops.h>
#include <linux/entry-kvm.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/cacheflush.h>
#include <asm/hwcap.h>
#include <asm/sbi.h>
#include <asm/vector.h>
#include <asm/kvm_vcpu_vector.h>

const struct _kvm_stats_desc kvm_vcpu_stats_desc[] = {
	KVM_GENERIC_VCPU_STATS(),
	STATS_DESC_COUNTER(VCPU, ecall_exit_stat),
	STATS_DESC_COUNTER(VCPU, wfi_exit_stat),
	STATS_DESC_COUNTER(VCPU, mmio_exit_user),
	STATS_DESC_COUNTER(VCPU, mmio_exit_kernel),
	STATS_DESC_COUNTER(VCPU, csr_exit_user),
	STATS_DESC_COUNTER(VCPU, csr_exit_kernel),
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

#define KVM_RISCV_BASE_ISA_MASK		GENMASK(25, 0)

#define KVM_ISA_EXT_ARR(ext)		[KVM_RISCV_ISA_EXT_##ext] = RISCV_ISA_EXT_##ext

/* Mapping between KVM ISA Extension ID & Host ISA extension ID */
static const unsigned long kvm_isa_ext_arr[] = {
	[KVM_RISCV_ISA_EXT_A] = RISCV_ISA_EXT_a,
	[KVM_RISCV_ISA_EXT_C] = RISCV_ISA_EXT_c,
	[KVM_RISCV_ISA_EXT_D] = RISCV_ISA_EXT_d,
	[KVM_RISCV_ISA_EXT_F] = RISCV_ISA_EXT_f,
	[KVM_RISCV_ISA_EXT_H] = RISCV_ISA_EXT_h,
	[KVM_RISCV_ISA_EXT_I] = RISCV_ISA_EXT_i,
	[KVM_RISCV_ISA_EXT_M] = RISCV_ISA_EXT_m,
	[KVM_RISCV_ISA_EXT_V] = RISCV_ISA_EXT_v,

	KVM_ISA_EXT_ARR(SSAIA),
	KVM_ISA_EXT_ARR(SSTC),
	KVM_ISA_EXT_ARR(SVINVAL),
	KVM_ISA_EXT_ARR(SVPBMT),
	KVM_ISA_EXT_ARR(ZBB),
	KVM_ISA_EXT_ARR(ZIHINTPAUSE),
	KVM_ISA_EXT_ARR(ZICBOM),
	KVM_ISA_EXT_ARR(ZICBOZ),
};

static unsigned long kvm_riscv_vcpu_base2isa_ext(unsigned long base_ext)
{
	unsigned long i;

	for (i = 0; i < KVM_RISCV_ISA_EXT_MAX; i++) {
		if (kvm_isa_ext_arr[i] == base_ext)
			return i;
	}

	return KVM_RISCV_ISA_EXT_MAX;
}

static bool kvm_riscv_vcpu_isa_enable_allowed(unsigned long ext)
{
	switch (ext) {
	case KVM_RISCV_ISA_EXT_H:
		return false;
	case KVM_RISCV_ISA_EXT_V:
		return riscv_v_vstate_ctrl_user_allowed();
	default:
		break;
	}

	return true;
}

static bool kvm_riscv_vcpu_isa_disable_allowed(unsigned long ext)
{
	switch (ext) {
	case KVM_RISCV_ISA_EXT_A:
	case KVM_RISCV_ISA_EXT_C:
	case KVM_RISCV_ISA_EXT_I:
	case KVM_RISCV_ISA_EXT_M:
	case KVM_RISCV_ISA_EXT_SSAIA:
	case KVM_RISCV_ISA_EXT_SSTC:
	case KVM_RISCV_ISA_EXT_SVINVAL:
	case KVM_RISCV_ISA_EXT_ZIHINTPAUSE:
	case KVM_RISCV_ISA_EXT_ZBB:
		return false;
	default:
		break;
	}

	return true;
}

static void kvm_riscv_reset_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;
	struct kvm_vcpu_csr *reset_csr = &vcpu->arch.guest_reset_csr;
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	struct kvm_cpu_context *reset_cntx = &vcpu->arch.guest_reset_context;
	bool loaded;

	/**
	 * The preemption should be disabled here because it races with
	 * kvm_sched_out/kvm_sched_in(called from preempt notifiers) which
	 * also calls vcpu_load/put.
	 */
	get_cpu();
	loaded = (vcpu->cpu != -1);
	if (loaded)
		kvm_arch_vcpu_put(vcpu);

	vcpu->arch.last_exit_cpu = -1;

	memcpy(csr, reset_csr, sizeof(*csr));

	memcpy(cntx, reset_cntx, sizeof(*cntx));

	kvm_riscv_vcpu_fp_reset(vcpu);

	kvm_riscv_vcpu_vector_reset(vcpu);

	kvm_riscv_vcpu_timer_reset(vcpu);

	kvm_riscv_vcpu_aia_reset(vcpu);

	bitmap_zero(vcpu->arch.irqs_pending, KVM_RISCV_VCPU_NR_IRQS);
	bitmap_zero(vcpu->arch.irqs_pending_mask, KVM_RISCV_VCPU_NR_IRQS);

	kvm_riscv_vcpu_pmu_reset(vcpu);

	vcpu->arch.hfence_head = 0;
	vcpu->arch.hfence_tail = 0;
	memset(vcpu->arch.hfence_queue, 0, sizeof(vcpu->arch.hfence_queue));

	/* Reset the guest CSRs for hotplug usecase */
	if (loaded)
		kvm_arch_vcpu_load(vcpu, smp_processor_id());
	put_cpu();
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	int rc;
	struct kvm_cpu_context *cntx;
	struct kvm_vcpu_csr *reset_csr = &vcpu->arch.guest_reset_csr;
	unsigned long host_isa, i;

	/* Mark this VCPU never ran */
	vcpu->arch.ran_atleast_once = false;
	vcpu->arch.mmu_page_cache.gfp_zero = __GFP_ZERO;
	bitmap_zero(vcpu->arch.isa, RISCV_ISA_EXT_MAX);

	/* Setup ISA features available to VCPU */
	for (i = 0; i < ARRAY_SIZE(kvm_isa_ext_arr); i++) {
		host_isa = kvm_isa_ext_arr[i];
		if (__riscv_isa_extension_available(NULL, host_isa) &&
		    kvm_riscv_vcpu_isa_enable_allowed(i))
			set_bit(host_isa, vcpu->arch.isa);
	}

	/* Setup vendor, arch, and implementation details */
	vcpu->arch.mvendorid = sbi_get_mvendorid();
	vcpu->arch.marchid = sbi_get_marchid();
	vcpu->arch.mimpid = sbi_get_mimpid();

	/* Setup VCPU hfence queue */
	spin_lock_init(&vcpu->arch.hfence_lock);

	/* Setup reset state of shadow SSTATUS and HSTATUS CSRs */
	cntx = &vcpu->arch.guest_reset_context;
	cntx->sstatus = SR_SPP | SR_SPIE;
	cntx->hstatus = 0;
	cntx->hstatus |= HSTATUS_VTW;
	cntx->hstatus |= HSTATUS_SPVP;
	cntx->hstatus |= HSTATUS_SPV;

	if (kvm_riscv_vcpu_alloc_vector_context(vcpu, cntx))
		return -ENOMEM;

	/* By default, make CY, TM, and IR counters accessible in VU mode */
	reset_csr->scounteren = 0x7;

	/* Setup VCPU timer */
	kvm_riscv_vcpu_timer_init(vcpu);

	/* setup performance monitoring */
	kvm_riscv_vcpu_pmu_init(vcpu);

	/* Setup VCPU AIA */
	rc = kvm_riscv_vcpu_aia_init(vcpu);
	if (rc)
		return rc;

	/* Reset VCPU */
	kvm_riscv_reset_vcpu(vcpu);

	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
	/**
	 * vcpu with id 0 is the designated boot cpu.
	 * Keep all vcpus with non-zero id in power-off state so that
	 * they can be brought up using SBI HSM extension.
	 */
	if (vcpu->vcpu_idx != 0)
		kvm_riscv_vcpu_power_off(vcpu);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	/* Cleanup VCPU AIA context */
	kvm_riscv_vcpu_aia_deinit(vcpu);

	/* Cleanup VCPU timer */
	kvm_riscv_vcpu_timer_deinit(vcpu);

	kvm_riscv_vcpu_pmu_deinit(vcpu);

	/* Free unused pages pre-allocated for G-stage page table mappings */
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);

	/* Free vector context space for host and guest kernel */
	kvm_riscv_vcpu_free_vector_context(vcpu);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvm_riscv_vcpu_timer_pending(vcpu);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return (kvm_riscv_vcpu_has_interrupts(vcpu, -1UL) &&
		!vcpu->arch.power_off && !vcpu->arch.pause);
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.guest_context.sstatus & SR_SPP) ? true : false;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static int kvm_riscv_vcpu_get_reg_config(struct kvm_vcpu *vcpu,
					 const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CONFIG);
	unsigned long reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	switch (reg_num) {
	case KVM_REG_RISCV_CONFIG_REG(isa):
		reg_val = vcpu->arch.isa[0] & KVM_RISCV_BASE_ISA_MASK;
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicbom_block_size):
		if (!riscv_isa_extension_available(vcpu->arch.isa, ZICBOM))
			return -EINVAL;
		reg_val = riscv_cbom_block_size;
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicboz_block_size):
		if (!riscv_isa_extension_available(vcpu->arch.isa, ZICBOZ))
			return -EINVAL;
		reg_val = riscv_cboz_block_size;
		break;
	case KVM_REG_RISCV_CONFIG_REG(mvendorid):
		reg_val = vcpu->arch.mvendorid;
		break;
	case KVM_REG_RISCV_CONFIG_REG(marchid):
		reg_val = vcpu->arch.marchid;
		break;
	case KVM_REG_RISCV_CONFIG_REG(mimpid):
		reg_val = vcpu->arch.mimpid;
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int kvm_riscv_vcpu_set_reg_config(struct kvm_vcpu *vcpu,
					 const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CONFIG);
	unsigned long i, isa_ext, reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	switch (reg_num) {
	case KVM_REG_RISCV_CONFIG_REG(isa):
		/*
		 * This ONE REG interface is only defined for
		 * single letter extensions.
		 */
		if (fls(reg_val) >= RISCV_ISA_EXT_BASE)
			return -EINVAL;

		if (!vcpu->arch.ran_atleast_once) {
			/* Ignore the enable/disable request for certain extensions */
			for (i = 0; i < RISCV_ISA_EXT_BASE; i++) {
				isa_ext = kvm_riscv_vcpu_base2isa_ext(i);
				if (isa_ext >= KVM_RISCV_ISA_EXT_MAX) {
					reg_val &= ~BIT(i);
					continue;
				}
				if (!kvm_riscv_vcpu_isa_enable_allowed(isa_ext))
					if (reg_val & BIT(i))
						reg_val &= ~BIT(i);
				if (!kvm_riscv_vcpu_isa_disable_allowed(isa_ext))
					if (!(reg_val & BIT(i)))
						reg_val |= BIT(i);
			}
			reg_val &= riscv_isa_extension_base(NULL);
			/* Do not modify anything beyond single letter extensions */
			reg_val = (vcpu->arch.isa[0] & ~KVM_RISCV_BASE_ISA_MASK) |
				  (reg_val & KVM_RISCV_BASE_ISA_MASK);
			vcpu->arch.isa[0] = reg_val;
			kvm_riscv_vcpu_fp_reset(vcpu);
		} else {
			return -EOPNOTSUPP;
		}
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicbom_block_size):
		return -EOPNOTSUPP;
	case KVM_REG_RISCV_CONFIG_REG(zicboz_block_size):
		return -EOPNOTSUPP;
	case KVM_REG_RISCV_CONFIG_REG(mvendorid):
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.mvendorid = reg_val;
		else
			return -EBUSY;
		break;
	case KVM_REG_RISCV_CONFIG_REG(marchid):
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.marchid = reg_val;
		else
			return -EBUSY;
		break;
	case KVM_REG_RISCV_CONFIG_REG(mimpid):
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.mimpid = reg_val;
		else
			return -EBUSY;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int kvm_riscv_vcpu_get_reg_core(struct kvm_vcpu *vcpu,
				       const struct kvm_one_reg *reg)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CORE);
	unsigned long reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;
	if (reg_num >= sizeof(struct kvm_riscv_core) / sizeof(unsigned long))
		return -EINVAL;

	if (reg_num == KVM_REG_RISCV_CORE_REG(regs.pc))
		reg_val = cntx->sepc;
	else if (KVM_REG_RISCV_CORE_REG(regs.pc) < reg_num &&
		 reg_num <= KVM_REG_RISCV_CORE_REG(regs.t6))
		reg_val = ((unsigned long *)cntx)[reg_num];
	else if (reg_num == KVM_REG_RISCV_CORE_REG(mode))
		reg_val = (cntx->sstatus & SR_SPP) ?
				KVM_RISCV_MODE_S : KVM_RISCV_MODE_U;
	else
		return -EINVAL;

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int kvm_riscv_vcpu_set_reg_core(struct kvm_vcpu *vcpu,
				       const struct kvm_one_reg *reg)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CORE);
	unsigned long reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;
	if (reg_num >= sizeof(struct kvm_riscv_core) / sizeof(unsigned long))
		return -EINVAL;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	if (reg_num == KVM_REG_RISCV_CORE_REG(regs.pc))
		cntx->sepc = reg_val;
	else if (KVM_REG_RISCV_CORE_REG(regs.pc) < reg_num &&
		 reg_num <= KVM_REG_RISCV_CORE_REG(regs.t6))
		((unsigned long *)cntx)[reg_num] = reg_val;
	else if (reg_num == KVM_REG_RISCV_CORE_REG(mode)) {
		if (reg_val == KVM_RISCV_MODE_S)
			cntx->sstatus |= SR_SPP;
		else
			cntx->sstatus &= ~SR_SPP;
	} else
		return -EINVAL;

	return 0;
}

static int kvm_riscv_vcpu_general_get_csr(struct kvm_vcpu *vcpu,
					  unsigned long reg_num,
					  unsigned long *out_val)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_csr) / sizeof(unsigned long))
		return -EINVAL;

	if (reg_num == KVM_REG_RISCV_CSR_REG(sip)) {
		kvm_riscv_vcpu_flush_interrupts(vcpu);
		*out_val = (csr->hvip >> VSIP_TO_HVIP_SHIFT) & VSIP_VALID_MASK;
		*out_val |= csr->hvip & ~IRQ_LOCAL_MASK;
	} else
		*out_val = ((unsigned long *)csr)[reg_num];

	return 0;
}

static inline int kvm_riscv_vcpu_general_set_csr(struct kvm_vcpu *vcpu,
						 unsigned long reg_num,
						 unsigned long reg_val)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_csr) / sizeof(unsigned long))
		return -EINVAL;

	if (reg_num == KVM_REG_RISCV_CSR_REG(sip)) {
		reg_val &= VSIP_VALID_MASK;
		reg_val <<= VSIP_TO_HVIP_SHIFT;
	}

	((unsigned long *)csr)[reg_num] = reg_val;

	if (reg_num == KVM_REG_RISCV_CSR_REG(sip))
		WRITE_ONCE(vcpu->arch.irqs_pending_mask[0], 0);

	return 0;
}

static int kvm_riscv_vcpu_get_reg_csr(struct kvm_vcpu *vcpu,
				      const struct kvm_one_reg *reg)
{
	int rc;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CSR);
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;
	switch (reg_subtype) {
	case KVM_REG_RISCV_CSR_GENERAL:
		rc = kvm_riscv_vcpu_general_get_csr(vcpu, reg_num, &reg_val);
		break;
	case KVM_REG_RISCV_CSR_AIA:
		rc = kvm_riscv_vcpu_aia_get_csr(vcpu, reg_num, &reg_val);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc)
		return rc;

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int kvm_riscv_vcpu_set_reg_csr(struct kvm_vcpu *vcpu,
				      const struct kvm_one_reg *reg)
{
	int rc;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_CSR);
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;
	switch (reg_subtype) {
	case KVM_REG_RISCV_CSR_GENERAL:
		rc = kvm_riscv_vcpu_general_set_csr(vcpu, reg_num, reg_val);
		break;
	case KVM_REG_RISCV_CSR_AIA:
		rc = kvm_riscv_vcpu_aia_set_csr(vcpu, reg_num, reg_val);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc)
		return rc;

	return 0;
}

static int kvm_riscv_vcpu_get_reg_isa_ext(struct kvm_vcpu *vcpu,
					  const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_ISA_EXT);
	unsigned long reg_val = 0;
	unsigned long host_isa_ext;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (reg_num >= KVM_RISCV_ISA_EXT_MAX ||
	    reg_num >= ARRAY_SIZE(kvm_isa_ext_arr))
		return -EINVAL;

	host_isa_ext = kvm_isa_ext_arr[reg_num];
	if (__riscv_isa_extension_available(vcpu->arch.isa, host_isa_ext))
		reg_val = 1; /* Mark the given extension as available */

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

static int kvm_riscv_vcpu_set_reg_isa_ext(struct kvm_vcpu *vcpu,
					  const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_ISA_EXT);
	unsigned long reg_val;
	unsigned long host_isa_ext;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (reg_num >= KVM_RISCV_ISA_EXT_MAX ||
	    reg_num >= ARRAY_SIZE(kvm_isa_ext_arr))
		return -EINVAL;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	host_isa_ext = kvm_isa_ext_arr[reg_num];
	if (!__riscv_isa_extension_available(NULL, host_isa_ext))
		return	-EOPNOTSUPP;

	if (!vcpu->arch.ran_atleast_once) {
		/*
		 * All multi-letter extension and a few single letter
		 * extension can be disabled
		 */
		if (reg_val == 1 &&
		    kvm_riscv_vcpu_isa_enable_allowed(reg_num))
			set_bit(host_isa_ext, vcpu->arch.isa);
		else if (!reg_val &&
			 kvm_riscv_vcpu_isa_disable_allowed(reg_num))
			clear_bit(host_isa_ext, vcpu->arch.isa);
		else
			return -EINVAL;
		kvm_riscv_vcpu_fp_reset(vcpu);
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int kvm_riscv_vcpu_set_reg(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg)
{
	switch (reg->id & KVM_REG_RISCV_TYPE_MASK) {
	case KVM_REG_RISCV_CONFIG:
		return kvm_riscv_vcpu_set_reg_config(vcpu, reg);
	case KVM_REG_RISCV_CORE:
		return kvm_riscv_vcpu_set_reg_core(vcpu, reg);
	case KVM_REG_RISCV_CSR:
		return kvm_riscv_vcpu_set_reg_csr(vcpu, reg);
	case KVM_REG_RISCV_TIMER:
		return kvm_riscv_vcpu_set_reg_timer(vcpu, reg);
	case KVM_REG_RISCV_FP_F:
		return kvm_riscv_vcpu_set_reg_fp(vcpu, reg,
						 KVM_REG_RISCV_FP_F);
	case KVM_REG_RISCV_FP_D:
		return kvm_riscv_vcpu_set_reg_fp(vcpu, reg,
						 KVM_REG_RISCV_FP_D);
	case KVM_REG_RISCV_ISA_EXT:
		return kvm_riscv_vcpu_set_reg_isa_ext(vcpu, reg);
	case KVM_REG_RISCV_SBI_EXT:
		return kvm_riscv_vcpu_set_reg_sbi_ext(vcpu, reg);
	case KVM_REG_RISCV_VECTOR:
		return kvm_riscv_vcpu_set_reg_vector(vcpu, reg,
						 KVM_REG_RISCV_VECTOR);
	default:
		break;
	}

	return -EINVAL;
}

static int kvm_riscv_vcpu_get_reg(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg)
{
	switch (reg->id & KVM_REG_RISCV_TYPE_MASK) {
	case KVM_REG_RISCV_CONFIG:
		return kvm_riscv_vcpu_get_reg_config(vcpu, reg);
	case KVM_REG_RISCV_CORE:
		return kvm_riscv_vcpu_get_reg_core(vcpu, reg);
	case KVM_REG_RISCV_CSR:
		return kvm_riscv_vcpu_get_reg_csr(vcpu, reg);
	case KVM_REG_RISCV_TIMER:
		return kvm_riscv_vcpu_get_reg_timer(vcpu, reg);
	case KVM_REG_RISCV_FP_F:
		return kvm_riscv_vcpu_get_reg_fp(vcpu, reg,
						 KVM_REG_RISCV_FP_F);
	case KVM_REG_RISCV_FP_D:
		return kvm_riscv_vcpu_get_reg_fp(vcpu, reg,
						 KVM_REG_RISCV_FP_D);
	case KVM_REG_RISCV_ISA_EXT:
		return kvm_riscv_vcpu_get_reg_isa_ext(vcpu, reg);
	case KVM_REG_RISCV_SBI_EXT:
		return kvm_riscv_vcpu_get_reg_sbi_ext(vcpu, reg);
	case KVM_REG_RISCV_VECTOR:
		return kvm_riscv_vcpu_get_reg_vector(vcpu, reg,
						 KVM_REG_RISCV_VECTOR);
	default:
		break;
	}

	return -EINVAL;
}

long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	if (ioctl == KVM_INTERRUPT) {
		struct kvm_interrupt irq;

		if (copy_from_user(&irq, argp, sizeof(irq)))
			return -EFAULT;

		if (irq.irq == KVM_INTERRUPT_SET)
			return kvm_riscv_vcpu_set_interrupt(vcpu, IRQ_VS_EXT);
		else
			return kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_EXT);
	}

	return -ENOIOCTLCMD;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r = -EINVAL;

	switch (ioctl) {
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;

		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_riscv_vcpu_set_reg(vcpu, &reg);
		else
			r = kvm_riscv_vcpu_get_reg(vcpu, &reg);
		break;
	}
	default:
		break;
	}

	return r;
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

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	return -EINVAL;
}

void kvm_riscv_vcpu_flush_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;
	unsigned long mask, val;

	if (READ_ONCE(vcpu->arch.irqs_pending_mask[0])) {
		mask = xchg_acquire(&vcpu->arch.irqs_pending_mask[0], 0);
		val = READ_ONCE(vcpu->arch.irqs_pending[0]) & mask;

		csr->hvip &= ~mask;
		csr->hvip |= val;
	}

	/* Flush AIA high interrupts */
	kvm_riscv_vcpu_aia_flush_interrupts(vcpu);
}

void kvm_riscv_vcpu_sync_interrupts(struct kvm_vcpu *vcpu)
{
	unsigned long hvip;
	struct kvm_vcpu_arch *v = &vcpu->arch;
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	/* Read current HVIP and VSIE CSRs */
	csr->vsie = csr_read(CSR_VSIE);

	/* Sync-up HVIP.VSSIP bit changes does by Guest */
	hvip = csr_read(CSR_HVIP);
	if ((csr->hvip ^ hvip) & (1UL << IRQ_VS_SOFT)) {
		if (hvip & (1UL << IRQ_VS_SOFT)) {
			if (!test_and_set_bit(IRQ_VS_SOFT,
					      v->irqs_pending_mask))
				set_bit(IRQ_VS_SOFT, v->irqs_pending);
		} else {
			if (!test_and_set_bit(IRQ_VS_SOFT,
					      v->irqs_pending_mask))
				clear_bit(IRQ_VS_SOFT, v->irqs_pending);
		}
	}

	/* Sync-up AIA high interrupts */
	kvm_riscv_vcpu_aia_sync_interrupts(vcpu);

	/* Sync-up timer CSRs */
	kvm_riscv_vcpu_timer_sync(vcpu);
}

int kvm_riscv_vcpu_set_interrupt(struct kvm_vcpu *vcpu, unsigned int irq)
{
	/*
	 * We only allow VS-mode software, timer, and external
	 * interrupts when irq is one of the local interrupts
	 * defined by RISC-V privilege specification.
	 */
	if (irq < IRQ_LOCAL_MAX &&
	    irq != IRQ_VS_SOFT &&
	    irq != IRQ_VS_TIMER &&
	    irq != IRQ_VS_EXT)
		return -EINVAL;

	set_bit(irq, vcpu->arch.irqs_pending);
	smp_mb__before_atomic();
	set_bit(irq, vcpu->arch.irqs_pending_mask);

	kvm_vcpu_kick(vcpu);

	return 0;
}

int kvm_riscv_vcpu_unset_interrupt(struct kvm_vcpu *vcpu, unsigned int irq)
{
	/*
	 * We only allow VS-mode software, timer, and external
	 * interrupts when irq is one of the local interrupts
	 * defined by RISC-V privilege specification.
	 */
	if (irq < IRQ_LOCAL_MAX &&
	    irq != IRQ_VS_SOFT &&
	    irq != IRQ_VS_TIMER &&
	    irq != IRQ_VS_EXT)
		return -EINVAL;

	clear_bit(irq, vcpu->arch.irqs_pending);
	smp_mb__before_atomic();
	set_bit(irq, vcpu->arch.irqs_pending_mask);

	return 0;
}

bool kvm_riscv_vcpu_has_interrupts(struct kvm_vcpu *vcpu, u64 mask)
{
	unsigned long ie;

	ie = ((vcpu->arch.guest_csr.vsie & VSIP_VALID_MASK)
		<< VSIP_TO_HVIP_SHIFT) & (unsigned long)mask;
	ie |= vcpu->arch.guest_csr.vsie & ~IRQ_LOCAL_MASK &
		(unsigned long)mask;
	if (READ_ONCE(vcpu->arch.irqs_pending[0]) & ie)
		return true;

	/* Check AIA high interrupts */
	return kvm_riscv_vcpu_aia_has_interrupts(vcpu, mask);
}

void kvm_riscv_vcpu_power_off(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = true;
	kvm_make_request(KVM_REQ_SLEEP, vcpu);
	kvm_vcpu_kick(vcpu);
}

void kvm_riscv_vcpu_power_on(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = false;
	kvm_vcpu_wake_up(vcpu);
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	if (vcpu->arch.power_off)
		mp_state->mp_state = KVM_MP_STATE_STOPPED;
	else
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = 0;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.power_off = false;
		break;
	case KVM_MP_STATE_STOPPED:
		kvm_riscv_vcpu_power_off(vcpu);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	/* TODO; To be implemented later. */
	return -EINVAL;
}

static void kvm_riscv_vcpu_update_config(const unsigned long *isa)
{
	u64 henvcfg = 0;

	if (riscv_isa_extension_available(isa, SVPBMT))
		henvcfg |= ENVCFG_PBMTE;

	if (riscv_isa_extension_available(isa, SSTC))
		henvcfg |= ENVCFG_STCE;

	if (riscv_isa_extension_available(isa, ZICBOM))
		henvcfg |= (ENVCFG_CBIE | ENVCFG_CBCFE);

	if (riscv_isa_extension_available(isa, ZICBOZ))
		henvcfg |= ENVCFG_CBZE;

	csr_write(CSR_HENVCFG, henvcfg);
#ifdef CONFIG_32BIT
	csr_write(CSR_HENVCFGH, henvcfg >> 32);
#endif
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	csr_write(CSR_VSSTATUS, csr->vsstatus);
	csr_write(CSR_VSIE, csr->vsie);
	csr_write(CSR_VSTVEC, csr->vstvec);
	csr_write(CSR_VSSCRATCH, csr->vsscratch);
	csr_write(CSR_VSEPC, csr->vsepc);
	csr_write(CSR_VSCAUSE, csr->vscause);
	csr_write(CSR_VSTVAL, csr->vstval);
	csr_write(CSR_HVIP, csr->hvip);
	csr_write(CSR_VSATP, csr->vsatp);

	kvm_riscv_vcpu_update_config(vcpu->arch.isa);

	kvm_riscv_gstage_update_hgatp(vcpu);

	kvm_riscv_vcpu_timer_restore(vcpu);

	kvm_riscv_vcpu_host_fp_save(&vcpu->arch.host_context);
	kvm_riscv_vcpu_guest_fp_restore(&vcpu->arch.guest_context,
					vcpu->arch.isa);
	kvm_riscv_vcpu_host_vector_save(&vcpu->arch.host_context);
	kvm_riscv_vcpu_guest_vector_restore(&vcpu->arch.guest_context,
					    vcpu->arch.isa);

	kvm_riscv_vcpu_aia_load(vcpu, cpu);

	vcpu->cpu = cpu;
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	vcpu->cpu = -1;

	kvm_riscv_vcpu_aia_put(vcpu);

	kvm_riscv_vcpu_guest_fp_save(&vcpu->arch.guest_context,
				     vcpu->arch.isa);
	kvm_riscv_vcpu_host_fp_restore(&vcpu->arch.host_context);

	kvm_riscv_vcpu_timer_save(vcpu);
	kvm_riscv_vcpu_guest_vector_save(&vcpu->arch.guest_context,
					 vcpu->arch.isa);
	kvm_riscv_vcpu_host_vector_restore(&vcpu->arch.host_context);

	csr->vsstatus = csr_read(CSR_VSSTATUS);
	csr->vsie = csr_read(CSR_VSIE);
	csr->vstvec = csr_read(CSR_VSTVEC);
	csr->vsscratch = csr_read(CSR_VSSCRATCH);
	csr->vsepc = csr_read(CSR_VSEPC);
	csr->vscause = csr_read(CSR_VSCAUSE);
	csr->vstval = csr_read(CSR_VSTVAL);
	csr->hvip = csr_read(CSR_HVIP);
	csr->vsatp = csr_read(CSR_VSATP);
}

static void kvm_riscv_check_vcpu_requests(struct kvm_vcpu *vcpu)
{
	struct rcuwait *wait = kvm_arch_vcpu_get_wait(vcpu);

	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_SLEEP, vcpu)) {
			kvm_vcpu_srcu_read_unlock(vcpu);
			rcuwait_wait_event(wait,
				(!vcpu->arch.power_off) && (!vcpu->arch.pause),
				TASK_INTERRUPTIBLE);
			kvm_vcpu_srcu_read_lock(vcpu);

			if (vcpu->arch.power_off || vcpu->arch.pause) {
				/*
				 * Awaken to handle a signal, request to
				 * sleep again later.
				 */
				kvm_make_request(KVM_REQ_SLEEP, vcpu);
			}
		}

		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_riscv_reset_vcpu(vcpu);

		if (kvm_check_request(KVM_REQ_UPDATE_HGATP, vcpu))
			kvm_riscv_gstage_update_hgatp(vcpu);

		if (kvm_check_request(KVM_REQ_FENCE_I, vcpu))
			kvm_riscv_fence_i_process(vcpu);

		/*
		 * The generic KVM_REQ_TLB_FLUSH is same as
		 * KVM_REQ_HFENCE_GVMA_VMID_ALL
		 */
		if (kvm_check_request(KVM_REQ_HFENCE_GVMA_VMID_ALL, vcpu))
			kvm_riscv_hfence_gvma_vmid_all_process(vcpu);

		if (kvm_check_request(KVM_REQ_HFENCE_VVMA_ALL, vcpu))
			kvm_riscv_hfence_vvma_all_process(vcpu);

		if (kvm_check_request(KVM_REQ_HFENCE, vcpu))
			kvm_riscv_hfence_process(vcpu);
	}
}

static void kvm_riscv_update_hvip(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	csr_write(CSR_HVIP, csr->hvip);
	kvm_riscv_vcpu_aia_update_hvip(vcpu);
}

/*
 * Actually run the vCPU, entering an RCU extended quiescent state (EQS) while
 * the vCPU is running.
 *
 * This must be noinstr as instrumentation may make use of RCU, and this is not
 * safe during the EQS.
 */
static void noinstr kvm_riscv_vcpu_enter_exit(struct kvm_vcpu *vcpu)
{
	guest_state_enter_irqoff();
	__kvm_riscv_switch_to(&vcpu->arch);
	vcpu->arch.last_exit_cpu = vcpu->cpu;
	guest_state_exit_irqoff();
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	int ret;
	struct kvm_cpu_trap trap;
	struct kvm_run *run = vcpu->run;

	/* Mark this VCPU ran at least once */
	vcpu->arch.ran_atleast_once = true;

	kvm_vcpu_srcu_read_lock(vcpu);

	switch (run->exit_reason) {
	case KVM_EXIT_MMIO:
		/* Process MMIO value returned from user-space */
		ret = kvm_riscv_vcpu_mmio_return(vcpu, vcpu->run);
		break;
	case KVM_EXIT_RISCV_SBI:
		/* Process SBI value returned from user-space */
		ret = kvm_riscv_vcpu_sbi_return(vcpu, vcpu->run);
		break;
	case KVM_EXIT_RISCV_CSR:
		/* Process CSR value returned from user-space */
		ret = kvm_riscv_vcpu_csr_return(vcpu, vcpu->run);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret) {
		kvm_vcpu_srcu_read_unlock(vcpu);
		return ret;
	}

	if (run->immediate_exit) {
		kvm_vcpu_srcu_read_unlock(vcpu);
		return -EINTR;
	}

	vcpu_load(vcpu);

	kvm_sigset_activate(vcpu);

	ret = 1;
	run->exit_reason = KVM_EXIT_UNKNOWN;
	while (ret > 0) {
		/* Check conditions before entering the guest */
		ret = xfer_to_guest_mode_handle_work(vcpu);
		if (ret)
			continue;
		ret = 1;

		kvm_riscv_gstage_vmid_update(vcpu);

		kvm_riscv_check_vcpu_requests(vcpu);

		preempt_disable();

		/* Update AIA HW state before entering guest */
		ret = kvm_riscv_vcpu_aia_update(vcpu);
		if (ret <= 0) {
			preempt_enable();
			continue;
		}

		local_irq_disable();

		/*
		 * Ensure we set mode to IN_GUEST_MODE after we disable
		 * interrupts and before the final VCPU requests check.
		 * See the comment in kvm_vcpu_exiting_guest_mode() and
		 * Documentation/virt/kvm/vcpu-requests.rst
		 */
		vcpu->mode = IN_GUEST_MODE;

		kvm_vcpu_srcu_read_unlock(vcpu);
		smp_mb__after_srcu_read_unlock();

		/*
		 * We might have got VCPU interrupts updated asynchronously
		 * so update it in HW.
		 */
		kvm_riscv_vcpu_flush_interrupts(vcpu);

		/* Update HVIP CSR for current CPU */
		kvm_riscv_update_hvip(vcpu);

		if (ret <= 0 ||
		    kvm_riscv_gstage_vmid_ver_changed(&vcpu->kvm->arch.vmid) ||
		    kvm_request_pending(vcpu) ||
		    xfer_to_guest_mode_work_pending()) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			local_irq_enable();
			preempt_enable();
			kvm_vcpu_srcu_read_lock(vcpu);
			continue;
		}

		/*
		 * Cleanup stale TLB enteries
		 *
		 * Note: This should be done after G-stage VMID has been
		 * updated using kvm_riscv_gstage_vmid_ver_changed()
		 */
		kvm_riscv_local_tlb_sanitize(vcpu);

		guest_timing_enter_irqoff();

		kvm_riscv_vcpu_enter_exit(vcpu);

		vcpu->mode = OUTSIDE_GUEST_MODE;
		vcpu->stat.exits++;

		/*
		 * Save SCAUSE, STVAL, HTVAL, and HTINST because we might
		 * get an interrupt between __kvm_riscv_switch_to() and
		 * local_irq_enable() which can potentially change CSRs.
		 */
		trap.sepc = vcpu->arch.guest_context.sepc;
		trap.scause = csr_read(CSR_SCAUSE);
		trap.stval = csr_read(CSR_STVAL);
		trap.htval = csr_read(CSR_HTVAL);
		trap.htinst = csr_read(CSR_HTINST);

		/* Syncup interrupts state with HW */
		kvm_riscv_vcpu_sync_interrupts(vcpu);

		/*
		 * We must ensure that any pending interrupts are taken before
		 * we exit guest timing so that timer ticks are accounted as
		 * guest time. Transiently unmask interrupts so that any
		 * pending interrupts are taken.
		 *
		 * There's no barrier which ensures that pending interrupts are
		 * recognised, so we just hope that the CPU takes any pending
		 * interrupts between the enable and disable.
		 */
		local_irq_enable();
		local_irq_disable();

		guest_timing_exit_irqoff();

		local_irq_enable();

		preempt_enable();

		kvm_vcpu_srcu_read_lock(vcpu);

		ret = kvm_riscv_vcpu_exit(vcpu, run, &trap);
	}

	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);

	kvm_vcpu_srcu_read_unlock(vcpu);

	return ret;
}
