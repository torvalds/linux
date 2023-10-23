// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 * Copyright (C) 2023 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/kvm_host.h>
#include <asm/cacheflush.h>
#include <asm/hwcap.h>
#include <asm/kvm_vcpu_vector.h>
#include <asm/vector.h>

#define KVM_RISCV_BASE_ISA_MASK		GENMASK(25, 0)

#define KVM_ISA_EXT_ARR(ext)		\
[KVM_RISCV_ISA_EXT_##ext] = RISCV_ISA_EXT_##ext

/* Mapping between KVM ISA Extension ID & Host ISA extension ID */
static const unsigned long kvm_isa_ext_arr[] = {
	/* Single letter extensions (alphabetically sorted) */
	[KVM_RISCV_ISA_EXT_A] = RISCV_ISA_EXT_a,
	[KVM_RISCV_ISA_EXT_C] = RISCV_ISA_EXT_c,
	[KVM_RISCV_ISA_EXT_D] = RISCV_ISA_EXT_d,
	[KVM_RISCV_ISA_EXT_F] = RISCV_ISA_EXT_f,
	[KVM_RISCV_ISA_EXT_H] = RISCV_ISA_EXT_h,
	[KVM_RISCV_ISA_EXT_I] = RISCV_ISA_EXT_i,
	[KVM_RISCV_ISA_EXT_M] = RISCV_ISA_EXT_m,
	[KVM_RISCV_ISA_EXT_V] = RISCV_ISA_EXT_v,
	/* Multi letter extensions (alphabetically sorted) */
	KVM_ISA_EXT_ARR(SSAIA),
	KVM_ISA_EXT_ARR(SSTC),
	KVM_ISA_EXT_ARR(SVINVAL),
	KVM_ISA_EXT_ARR(SVNAPOT),
	KVM_ISA_EXT_ARR(SVPBMT),
	KVM_ISA_EXT_ARR(ZBA),
	KVM_ISA_EXT_ARR(ZBB),
	KVM_ISA_EXT_ARR(ZBS),
	KVM_ISA_EXT_ARR(ZICBOM),
	KVM_ISA_EXT_ARR(ZICBOZ),
	KVM_ISA_EXT_ARR(ZICNTR),
	KVM_ISA_EXT_ARR(ZICSR),
	KVM_ISA_EXT_ARR(ZIFENCEI),
	KVM_ISA_EXT_ARR(ZIHINTPAUSE),
	KVM_ISA_EXT_ARR(ZIHPM),
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
	case KVM_RISCV_ISA_EXT_SVNAPOT:
	case KVM_RISCV_ISA_EXT_ZBA:
	case KVM_RISCV_ISA_EXT_ZBB:
	case KVM_RISCV_ISA_EXT_ZBS:
	case KVM_RISCV_ISA_EXT_ZICNTR:
	case KVM_RISCV_ISA_EXT_ZICSR:
	case KVM_RISCV_ISA_EXT_ZIFENCEI:
	case KVM_RISCV_ISA_EXT_ZIHINTPAUSE:
	case KVM_RISCV_ISA_EXT_ZIHPM:
		return false;
	default:
		break;
	}

	return true;
}

void kvm_riscv_vcpu_setup_isa(struct kvm_vcpu *vcpu)
{
	unsigned long host_isa, i;

	for (i = 0; i < ARRAY_SIZE(kvm_isa_ext_arr); i++) {
		host_isa = kvm_isa_ext_arr[i];
		if (__riscv_isa_extension_available(NULL, host_isa) &&
		    kvm_riscv_vcpu_isa_enable_allowed(i))
			set_bit(host_isa, vcpu->arch.isa);
	}
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
			return -ENOENT;
		reg_val = riscv_cbom_block_size;
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicboz_block_size):
		if (!riscv_isa_extension_available(vcpu->arch.isa, ZICBOZ))
			return -ENOENT;
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
	case KVM_REG_RISCV_CONFIG_REG(satp_mode):
		reg_val = satp_mode >> SATP_MODE_SHIFT;
		break;
	default:
		return -ENOENT;
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

		/*
		 * Return early (i.e. do nothing) if reg_val is the same
		 * value retrievable via kvm_riscv_vcpu_get_reg_config().
		 */
		if (reg_val == (vcpu->arch.isa[0] & KVM_RISCV_BASE_ISA_MASK))
			break;

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
			return -EBUSY;
		}
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicbom_block_size):
		if (!riscv_isa_extension_available(vcpu->arch.isa, ZICBOM))
			return -ENOENT;
		if (reg_val != riscv_cbom_block_size)
			return -EINVAL;
		break;
	case KVM_REG_RISCV_CONFIG_REG(zicboz_block_size):
		if (!riscv_isa_extension_available(vcpu->arch.isa, ZICBOZ))
			return -ENOENT;
		if (reg_val != riscv_cboz_block_size)
			return -EINVAL;
		break;
	case KVM_REG_RISCV_CONFIG_REG(mvendorid):
		if (reg_val == vcpu->arch.mvendorid)
			break;
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.mvendorid = reg_val;
		else
			return -EBUSY;
		break;
	case KVM_REG_RISCV_CONFIG_REG(marchid):
		if (reg_val == vcpu->arch.marchid)
			break;
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.marchid = reg_val;
		else
			return -EBUSY;
		break;
	case KVM_REG_RISCV_CONFIG_REG(mimpid):
		if (reg_val == vcpu->arch.mimpid)
			break;
		if (!vcpu->arch.ran_atleast_once)
			vcpu->arch.mimpid = reg_val;
		else
			return -EBUSY;
		break;
	case KVM_REG_RISCV_CONFIG_REG(satp_mode):
		if (reg_val != (satp_mode >> SATP_MODE_SHIFT))
			return -EINVAL;
		break;
	default:
		return -ENOENT;
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
		return -ENOENT;

	if (reg_num == KVM_REG_RISCV_CORE_REG(regs.pc))
		reg_val = cntx->sepc;
	else if (KVM_REG_RISCV_CORE_REG(regs.pc) < reg_num &&
		 reg_num <= KVM_REG_RISCV_CORE_REG(regs.t6))
		reg_val = ((unsigned long *)cntx)[reg_num];
	else if (reg_num == KVM_REG_RISCV_CORE_REG(mode))
		reg_val = (cntx->sstatus & SR_SPP) ?
				KVM_RISCV_MODE_S : KVM_RISCV_MODE_U;
	else
		return -ENOENT;

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
		return -ENOENT;

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
		return -ENOENT;

	return 0;
}

static int kvm_riscv_vcpu_general_get_csr(struct kvm_vcpu *vcpu,
					  unsigned long reg_num,
					  unsigned long *out_val)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_csr) / sizeof(unsigned long))
		return -ENOENT;

	if (reg_num == KVM_REG_RISCV_CSR_REG(sip)) {
		kvm_riscv_vcpu_flush_interrupts(vcpu);
		*out_val = (csr->hvip >> VSIP_TO_HVIP_SHIFT) & VSIP_VALID_MASK;
		*out_val |= csr->hvip & ~IRQ_LOCAL_MASK;
	} else
		*out_val = ((unsigned long *)csr)[reg_num];

	return 0;
}

static int kvm_riscv_vcpu_general_set_csr(struct kvm_vcpu *vcpu,
					  unsigned long reg_num,
					  unsigned long reg_val)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_csr) / sizeof(unsigned long))
		return -ENOENT;

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
		rc = -ENOENT;
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
		rc = -ENOENT;
		break;
	}
	if (rc)
		return rc;

	return 0;
}

static int riscv_vcpu_get_isa_ext_single(struct kvm_vcpu *vcpu,
					 unsigned long reg_num,
					 unsigned long *reg_val)
{
	unsigned long host_isa_ext;

	if (reg_num >= KVM_RISCV_ISA_EXT_MAX ||
	    reg_num >= ARRAY_SIZE(kvm_isa_ext_arr))
		return -ENOENT;

	host_isa_ext = kvm_isa_ext_arr[reg_num];
	if (!__riscv_isa_extension_available(NULL, host_isa_ext))
		return -ENOENT;

	*reg_val = 0;
	if (__riscv_isa_extension_available(vcpu->arch.isa, host_isa_ext))
		*reg_val = 1; /* Mark the given extension as available */

	return 0;
}

static int riscv_vcpu_set_isa_ext_single(struct kvm_vcpu *vcpu,
					 unsigned long reg_num,
					 unsigned long reg_val)
{
	unsigned long host_isa_ext;

	if (reg_num >= KVM_RISCV_ISA_EXT_MAX ||
	    reg_num >= ARRAY_SIZE(kvm_isa_ext_arr))
		return -ENOENT;

	host_isa_ext = kvm_isa_ext_arr[reg_num];
	if (!__riscv_isa_extension_available(NULL, host_isa_ext))
		return -ENOENT;

	if (reg_val == test_bit(host_isa_ext, vcpu->arch.isa))
		return 0;

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
		return -EBUSY;
	}

	return 0;
}

static int riscv_vcpu_get_isa_ext_multi(struct kvm_vcpu *vcpu,
					unsigned long reg_num,
					unsigned long *reg_val)
{
	unsigned long i, ext_id, ext_val;

	if (reg_num > KVM_REG_RISCV_ISA_MULTI_REG_LAST)
		return -ENOENT;

	for (i = 0; i < BITS_PER_LONG; i++) {
		ext_id = i + reg_num * BITS_PER_LONG;
		if (ext_id >= KVM_RISCV_ISA_EXT_MAX)
			break;

		ext_val = 0;
		riscv_vcpu_get_isa_ext_single(vcpu, ext_id, &ext_val);
		if (ext_val)
			*reg_val |= KVM_REG_RISCV_ISA_MULTI_MASK(ext_id);
	}

	return 0;
}

static int riscv_vcpu_set_isa_ext_multi(struct kvm_vcpu *vcpu,
					unsigned long reg_num,
					unsigned long reg_val, bool enable)
{
	unsigned long i, ext_id;

	if (reg_num > KVM_REG_RISCV_ISA_MULTI_REG_LAST)
		return -ENOENT;

	for_each_set_bit(i, &reg_val, BITS_PER_LONG) {
		ext_id = i + reg_num * BITS_PER_LONG;
		if (ext_id >= KVM_RISCV_ISA_EXT_MAX)
			break;

		riscv_vcpu_set_isa_ext_single(vcpu, ext_id, enable);
	}

	return 0;
}

static int kvm_riscv_vcpu_get_reg_isa_ext(struct kvm_vcpu *vcpu,
					  const struct kvm_one_reg *reg)
{
	int rc;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_ISA_EXT);
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	reg_val = 0;
	switch (reg_subtype) {
	case KVM_REG_RISCV_ISA_SINGLE:
		rc = riscv_vcpu_get_isa_ext_single(vcpu, reg_num, &reg_val);
		break;
	case KVM_REG_RISCV_ISA_MULTI_EN:
	case KVM_REG_RISCV_ISA_MULTI_DIS:
		rc = riscv_vcpu_get_isa_ext_multi(vcpu, reg_num, &reg_val);
		if (!rc && reg_subtype == KVM_REG_RISCV_ISA_MULTI_DIS)
			reg_val = ~reg_val;
		break;
	default:
		rc = -ENOENT;
	}
	if (rc)
		return rc;

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
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	switch (reg_subtype) {
	case KVM_REG_RISCV_ISA_SINGLE:
		return riscv_vcpu_set_isa_ext_single(vcpu, reg_num, reg_val);
	case KVM_REG_RISCV_SBI_MULTI_EN:
		return riscv_vcpu_set_isa_ext_multi(vcpu, reg_num, reg_val, true);
	case KVM_REG_RISCV_SBI_MULTI_DIS:
		return riscv_vcpu_set_isa_ext_multi(vcpu, reg_num, reg_val, false);
	default:
		return -ENOENT;
	}

	return 0;
}

static int copy_config_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	int n = 0;

	for (int i = 0; i < sizeof(struct kvm_riscv_config)/sizeof(unsigned long);
		 i++) {
		u64 size;
		u64 reg;

		/*
		 * Avoid reporting config reg if the corresponding extension
		 * was not available.
		 */
		if (i == KVM_REG_RISCV_CONFIG_REG(zicbom_block_size) &&
			!riscv_isa_extension_available(vcpu->arch.isa, ZICBOM))
			continue;
		else if (i == KVM_REG_RISCV_CONFIG_REG(zicboz_block_size) &&
			!riscv_isa_extension_available(vcpu->arch.isa, ZICBOZ))
			continue;

		size = IS_ENABLED(CONFIG_32BIT) ? KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		reg = KVM_REG_RISCV | size | KVM_REG_RISCV_CONFIG | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}

		n++;
	}

	return n;
}

static unsigned long num_config_regs(const struct kvm_vcpu *vcpu)
{
	return copy_config_reg_indices(vcpu, NULL);
}

static inline unsigned long num_core_regs(void)
{
	return sizeof(struct kvm_riscv_core) / sizeof(unsigned long);
}

static int copy_core_reg_indices(u64 __user *uindices)
{
	int n = num_core_regs();

	for (int i = 0; i < n; i++) {
		u64 size = IS_ENABLED(CONFIG_32BIT) ?
			   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_CORE | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	return n;
}

static inline unsigned long num_csr_regs(const struct kvm_vcpu *vcpu)
{
	unsigned long n = sizeof(struct kvm_riscv_csr) / sizeof(unsigned long);

	if (riscv_isa_extension_available(vcpu->arch.isa, SSAIA))
		n += sizeof(struct kvm_riscv_aia_csr) / sizeof(unsigned long);

	return n;
}

static int copy_csr_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	int n1 = sizeof(struct kvm_riscv_csr) / sizeof(unsigned long);
	int n2 = 0;

	/* copy general csr regs */
	for (int i = 0; i < n1; i++) {
		u64 size = IS_ENABLED(CONFIG_32BIT) ?
			   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_CSR |
				  KVM_REG_RISCV_CSR_GENERAL | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	/* copy AIA csr regs */
	if (riscv_isa_extension_available(vcpu->arch.isa, SSAIA)) {
		n2 = sizeof(struct kvm_riscv_aia_csr) / sizeof(unsigned long);

		for (int i = 0; i < n2; i++) {
			u64 size = IS_ENABLED(CONFIG_32BIT) ?
				   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
			u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_CSR |
					  KVM_REG_RISCV_CSR_AIA | i;

			if (uindices) {
				if (put_user(reg, uindices))
					return -EFAULT;
				uindices++;
			}
		}
	}

	return n1 + n2;
}

static inline unsigned long num_timer_regs(void)
{
	return sizeof(struct kvm_riscv_timer) / sizeof(u64);
}

static int copy_timer_reg_indices(u64 __user *uindices)
{
	int n = num_timer_regs();

	for (int i = 0; i < n; i++) {
		u64 reg = KVM_REG_RISCV | KVM_REG_SIZE_U64 |
			  KVM_REG_RISCV_TIMER | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	return n;
}

static inline unsigned long num_fp_f_regs(const struct kvm_vcpu *vcpu)
{
	const struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;

	if (riscv_isa_extension_available(vcpu->arch.isa, f))
		return sizeof(cntx->fp.f) / sizeof(u32);
	else
		return 0;
}

static int copy_fp_f_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	int n = num_fp_f_regs(vcpu);

	for (int i = 0; i < n; i++) {
		u64 reg = KVM_REG_RISCV | KVM_REG_SIZE_U32 |
			  KVM_REG_RISCV_FP_F | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	return n;
}

static inline unsigned long num_fp_d_regs(const struct kvm_vcpu *vcpu)
{
	const struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;

	if (riscv_isa_extension_available(vcpu->arch.isa, d))
		return sizeof(cntx->fp.d.f) / sizeof(u64) + 1;
	else
		return 0;
}

static int copy_fp_d_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	int i;
	int n = num_fp_d_regs(vcpu);
	u64 reg;

	/* copy fp.d.f indices */
	for (i = 0; i < n-1; i++) {
		reg = KVM_REG_RISCV | KVM_REG_SIZE_U64 |
		      KVM_REG_RISCV_FP_D | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	/* copy fp.d.fcsr indices */
	reg = KVM_REG_RISCV | KVM_REG_SIZE_U32 | KVM_REG_RISCV_FP_D | i;
	if (uindices) {
		if (put_user(reg, uindices))
			return -EFAULT;
		uindices++;
	}

	return n;
}

static int copy_isa_ext_reg_indices(const struct kvm_vcpu *vcpu,
				u64 __user *uindices)
{
	unsigned int n = 0;
	unsigned long isa_ext;

	for (int i = 0; i < KVM_RISCV_ISA_EXT_MAX; i++) {
		u64 size = IS_ENABLED(CONFIG_32BIT) ?
			   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_ISA_EXT | i;

		isa_ext = kvm_isa_ext_arr[i];
		if (!__riscv_isa_extension_available(NULL, isa_ext))
			continue;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}

		n++;
	}

	return n;
}

static inline unsigned long num_isa_ext_regs(const struct kvm_vcpu *vcpu)
{
	return copy_isa_ext_reg_indices(vcpu, NULL);;
}

static inline unsigned long num_sbi_ext_regs(void)
{
	/*
	 * number of KVM_REG_RISCV_SBI_SINGLE +
	 * 2 x (number of KVM_REG_RISCV_SBI_MULTI)
	 */
	return KVM_RISCV_SBI_EXT_MAX + 2*(KVM_REG_RISCV_SBI_MULTI_REG_LAST+1);
}

static int copy_sbi_ext_reg_indices(u64 __user *uindices)
{
	int n;

	/* copy KVM_REG_RISCV_SBI_SINGLE */
	n = KVM_RISCV_SBI_EXT_MAX;
	for (int i = 0; i < n; i++) {
		u64 size = IS_ENABLED(CONFIG_32BIT) ?
			   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_SBI_EXT |
			  KVM_REG_RISCV_SBI_SINGLE | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	/* copy KVM_REG_RISCV_SBI_MULTI */
	n = KVM_REG_RISCV_SBI_MULTI_REG_LAST + 1;
	for (int i = 0; i < n; i++) {
		u64 size = IS_ENABLED(CONFIG_32BIT) ?
			   KVM_REG_SIZE_U32 : KVM_REG_SIZE_U64;
		u64 reg = KVM_REG_RISCV | size | KVM_REG_RISCV_SBI_EXT |
			  KVM_REG_RISCV_SBI_MULTI_EN | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}

		reg = KVM_REG_RISCV | size | KVM_REG_RISCV_SBI_EXT |
			  KVM_REG_RISCV_SBI_MULTI_DIS | i;

		if (uindices) {
			if (put_user(reg, uindices))
				return -EFAULT;
			uindices++;
		}
	}

	return num_sbi_ext_regs();
}

/*
 * kvm_riscv_vcpu_num_regs - how many registers do we present via KVM_GET/SET_ONE_REG
 *
 * This is for all registers.
 */
unsigned long kvm_riscv_vcpu_num_regs(struct kvm_vcpu *vcpu)
{
	unsigned long res = 0;

	res += num_config_regs(vcpu);
	res += num_core_regs();
	res += num_csr_regs(vcpu);
	res += num_timer_regs();
	res += num_fp_f_regs(vcpu);
	res += num_fp_d_regs(vcpu);
	res += num_isa_ext_regs(vcpu);
	res += num_sbi_ext_regs();

	return res;
}

/*
 * kvm_riscv_vcpu_copy_reg_indices - get indices of all registers.
 */
int kvm_riscv_vcpu_copy_reg_indices(struct kvm_vcpu *vcpu,
				    u64 __user *uindices)
{
	int ret;

	ret = copy_config_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_core_reg_indices(uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_csr_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_timer_reg_indices(uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_fp_f_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_fp_d_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_isa_ext_reg_indices(vcpu, uindices);
	if (ret < 0)
		return ret;
	uindices += ret;

	ret = copy_sbi_ext_reg_indices(uindices);
	if (ret < 0)
		return ret;

	return 0;
}

int kvm_riscv_vcpu_set_reg(struct kvm_vcpu *vcpu,
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
		return kvm_riscv_vcpu_set_reg_vector(vcpu, reg);
	default:
		break;
	}

	return -ENOENT;
}

int kvm_riscv_vcpu_get_reg(struct kvm_vcpu *vcpu,
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
		return kvm_riscv_vcpu_get_reg_vector(vcpu, reg);
	default:
		break;
	}

	return -ENOENT;
}
