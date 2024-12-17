// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_sbi.h>

#ifndef CONFIG_RISCV_SBI_V01
static const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_v01 = {
	.extid_start = -1UL,
	.extid_end = -1UL,
	.handler = NULL,
};
#endif

#ifndef CONFIG_RISCV_PMU_SBI
static const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_pmu = {
	.extid_start = -1UL,
	.extid_end = -1UL,
	.handler = NULL,
};
#endif

struct kvm_riscv_sbi_extension_entry {
	enum KVM_RISCV_SBI_EXT_ID ext_idx;
	const struct kvm_vcpu_sbi_extension *ext_ptr;
};

static const struct kvm_riscv_sbi_extension_entry sbi_ext[] = {
	{
		.ext_idx = KVM_RISCV_SBI_EXT_V01,
		.ext_ptr = &vcpu_sbi_ext_v01,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_MAX, /* Can't be disabled */
		.ext_ptr = &vcpu_sbi_ext_base,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_TIME,
		.ext_ptr = &vcpu_sbi_ext_time,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_IPI,
		.ext_ptr = &vcpu_sbi_ext_ipi,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_RFENCE,
		.ext_ptr = &vcpu_sbi_ext_rfence,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_SRST,
		.ext_ptr = &vcpu_sbi_ext_srst,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_HSM,
		.ext_ptr = &vcpu_sbi_ext_hsm,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_PMU,
		.ext_ptr = &vcpu_sbi_ext_pmu,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_DBCN,
		.ext_ptr = &vcpu_sbi_ext_dbcn,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_STA,
		.ext_ptr = &vcpu_sbi_ext_sta,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_EXPERIMENTAL,
		.ext_ptr = &vcpu_sbi_ext_experimental,
	},
	{
		.ext_idx = KVM_RISCV_SBI_EXT_VENDOR,
		.ext_ptr = &vcpu_sbi_ext_vendor,
	},
};

static const struct kvm_riscv_sbi_extension_entry *
riscv_vcpu_get_sbi_ext(struct kvm_vcpu *vcpu, unsigned long idx)
{
	const struct kvm_riscv_sbi_extension_entry *sext = NULL;

	if (idx >= KVM_RISCV_SBI_EXT_MAX)
		return NULL;

	for (int i = 0; i < ARRAY_SIZE(sbi_ext); i++) {
		if (sbi_ext[i].ext_idx == idx) {
			sext = &sbi_ext[i];
			break;
		}
	}

	return sext;
}

bool riscv_vcpu_supports_sbi_ext(struct kvm_vcpu *vcpu, int idx)
{
	struct kvm_vcpu_sbi_context *scontext = &vcpu->arch.sbi_context;
	const struct kvm_riscv_sbi_extension_entry *sext;

	sext = riscv_vcpu_get_sbi_ext(vcpu, idx);

	return sext && scontext->ext_status[sext->ext_idx] != KVM_RISCV_SBI_EXT_STATUS_UNAVAILABLE;
}

void kvm_riscv_vcpu_sbi_forward(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	vcpu->arch.sbi_context.return_handled = 0;
	vcpu->stat.ecall_exit_stat++;
	run->exit_reason = KVM_EXIT_RISCV_SBI;
	run->riscv_sbi.extension_id = cp->a7;
	run->riscv_sbi.function_id = cp->a6;
	run->riscv_sbi.args[0] = cp->a0;
	run->riscv_sbi.args[1] = cp->a1;
	run->riscv_sbi.args[2] = cp->a2;
	run->riscv_sbi.args[3] = cp->a3;
	run->riscv_sbi.args[4] = cp->a4;
	run->riscv_sbi.args[5] = cp->a5;
	run->riscv_sbi.ret[0] = SBI_ERR_NOT_SUPPORTED;
	run->riscv_sbi.ret[1] = 0;
}

void kvm_riscv_vcpu_sbi_system_reset(struct kvm_vcpu *vcpu,
				     struct kvm_run *run,
				     u32 type, u64 reason)
{
	unsigned long i;
	struct kvm_vcpu *tmp;

	kvm_for_each_vcpu(i, tmp, vcpu->kvm) {
		spin_lock(&vcpu->arch.mp_state_lock);
		WRITE_ONCE(tmp->arch.mp_state.mp_state, KVM_MP_STATE_STOPPED);
		spin_unlock(&vcpu->arch.mp_state_lock);
	}
	kvm_make_all_cpus_request(vcpu->kvm, KVM_REQ_SLEEP);

	memset(&run->system_event, 0, sizeof(run->system_event));
	run->system_event.type = type;
	run->system_event.ndata = 1;
	run->system_event.data[0] = reason;
	run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

int kvm_riscv_vcpu_sbi_return(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	/* Handle SBI return only once */
	if (vcpu->arch.sbi_context.return_handled)
		return 0;
	vcpu->arch.sbi_context.return_handled = 1;

	/* Update return values */
	cp->a0 = run->riscv_sbi.ret[0];
	cp->a1 = run->riscv_sbi.ret[1];

	/* Move to next instruction */
	vcpu->arch.guest_context.sepc += 4;

	return 0;
}

static int riscv_vcpu_set_sbi_ext_single(struct kvm_vcpu *vcpu,
					 unsigned long reg_num,
					 unsigned long reg_val)
{
	struct kvm_vcpu_sbi_context *scontext = &vcpu->arch.sbi_context;
	const struct kvm_riscv_sbi_extension_entry *sext;

	if (reg_val != 1 && reg_val != 0)
		return -EINVAL;

	sext = riscv_vcpu_get_sbi_ext(vcpu, reg_num);
	if (!sext || scontext->ext_status[sext->ext_idx] == KVM_RISCV_SBI_EXT_STATUS_UNAVAILABLE)
		return -ENOENT;

	scontext->ext_status[sext->ext_idx] = (reg_val) ?
			KVM_RISCV_SBI_EXT_STATUS_ENABLED :
			KVM_RISCV_SBI_EXT_STATUS_DISABLED;

	return 0;
}

static int riscv_vcpu_get_sbi_ext_single(struct kvm_vcpu *vcpu,
					 unsigned long reg_num,
					 unsigned long *reg_val)
{
	struct kvm_vcpu_sbi_context *scontext = &vcpu->arch.sbi_context;
	const struct kvm_riscv_sbi_extension_entry *sext;

	sext = riscv_vcpu_get_sbi_ext(vcpu, reg_num);
	if (!sext || scontext->ext_status[sext->ext_idx] == KVM_RISCV_SBI_EXT_STATUS_UNAVAILABLE)
		return -ENOENT;

	*reg_val = scontext->ext_status[sext->ext_idx] ==
				KVM_RISCV_SBI_EXT_STATUS_ENABLED;

	return 0;
}

static int riscv_vcpu_set_sbi_ext_multi(struct kvm_vcpu *vcpu,
					unsigned long reg_num,
					unsigned long reg_val, bool enable)
{
	unsigned long i, ext_id;

	if (reg_num > KVM_REG_RISCV_SBI_MULTI_REG_LAST)
		return -ENOENT;

	for_each_set_bit(i, &reg_val, BITS_PER_LONG) {
		ext_id = i + reg_num * BITS_PER_LONG;
		if (ext_id >= KVM_RISCV_SBI_EXT_MAX)
			break;

		riscv_vcpu_set_sbi_ext_single(vcpu, ext_id, enable);
	}

	return 0;
}

static int riscv_vcpu_get_sbi_ext_multi(struct kvm_vcpu *vcpu,
					unsigned long reg_num,
					unsigned long *reg_val)
{
	unsigned long i, ext_id, ext_val;

	if (reg_num > KVM_REG_RISCV_SBI_MULTI_REG_LAST)
		return -ENOENT;

	for (i = 0; i < BITS_PER_LONG; i++) {
		ext_id = i + reg_num * BITS_PER_LONG;
		if (ext_id >= KVM_RISCV_SBI_EXT_MAX)
			break;

		ext_val = 0;
		riscv_vcpu_get_sbi_ext_single(vcpu, ext_id, &ext_val);
		if (ext_val)
			*reg_val |= KVM_REG_RISCV_SBI_MULTI_MASK(ext_id);
	}

	return 0;
}

int kvm_riscv_vcpu_set_reg_sbi_ext(struct kvm_vcpu *vcpu,
				   const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_SBI_EXT);
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (vcpu->arch.ran_atleast_once)
		return -EBUSY;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	switch (reg_subtype) {
	case KVM_REG_RISCV_SBI_SINGLE:
		return riscv_vcpu_set_sbi_ext_single(vcpu, reg_num, reg_val);
	case KVM_REG_RISCV_SBI_MULTI_EN:
		return riscv_vcpu_set_sbi_ext_multi(vcpu, reg_num, reg_val, true);
	case KVM_REG_RISCV_SBI_MULTI_DIS:
		return riscv_vcpu_set_sbi_ext_multi(vcpu, reg_num, reg_val, false);
	default:
		return -ENOENT;
	}

	return 0;
}

int kvm_riscv_vcpu_get_reg_sbi_ext(struct kvm_vcpu *vcpu,
				   const struct kvm_one_reg *reg)
{
	int rc;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_SBI_EXT);
	unsigned long reg_val, reg_subtype;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	reg_val = 0;
	switch (reg_subtype) {
	case KVM_REG_RISCV_SBI_SINGLE:
		rc = riscv_vcpu_get_sbi_ext_single(vcpu, reg_num, &reg_val);
		break;
	case KVM_REG_RISCV_SBI_MULTI_EN:
	case KVM_REG_RISCV_SBI_MULTI_DIS:
		rc = riscv_vcpu_get_sbi_ext_multi(vcpu, reg_num, &reg_val);
		if (!rc && reg_subtype == KVM_REG_RISCV_SBI_MULTI_DIS)
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

int kvm_riscv_vcpu_set_reg_sbi(struct kvm_vcpu *vcpu,
			       const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_SBI_STATE);
	unsigned long reg_subtype, reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	switch (reg_subtype) {
	case KVM_REG_RISCV_SBI_STA:
		return kvm_riscv_vcpu_set_reg_sbi_sta(vcpu, reg_num, reg_val);
	default:
		return -EINVAL;
	}

	return 0;
}

int kvm_riscv_vcpu_get_reg_sbi(struct kvm_vcpu *vcpu,
			       const struct kvm_one_reg *reg)
{
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_SBI_STATE);
	unsigned long reg_subtype, reg_val;
	int ret;

	if (KVM_REG_SIZE(reg->id) != sizeof(unsigned long))
		return -EINVAL;

	reg_subtype = reg_num & KVM_REG_RISCV_SUBTYPE_MASK;
	reg_num &= ~KVM_REG_RISCV_SUBTYPE_MASK;

	switch (reg_subtype) {
	case KVM_REG_RISCV_SBI_STA:
		ret = kvm_riscv_vcpu_get_reg_sbi_sta(vcpu, reg_num, &reg_val);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

const struct kvm_vcpu_sbi_extension *kvm_vcpu_sbi_find_ext(
				struct kvm_vcpu *vcpu, unsigned long extid)
{
	struct kvm_vcpu_sbi_context *scontext = &vcpu->arch.sbi_context;
	const struct kvm_riscv_sbi_extension_entry *entry;
	const struct kvm_vcpu_sbi_extension *ext;
	int i;

	for (i = 0; i < ARRAY_SIZE(sbi_ext); i++) {
		entry = &sbi_ext[i];
		ext = entry->ext_ptr;

		if (ext->extid_start <= extid && ext->extid_end >= extid) {
			if (entry->ext_idx >= KVM_RISCV_SBI_EXT_MAX ||
			    scontext->ext_status[entry->ext_idx] ==
						KVM_RISCV_SBI_EXT_STATUS_ENABLED)
				return ext;

			return NULL;
		}
	}

	return NULL;
}

int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int ret = 1;
	bool next_sepc = true;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	const struct kvm_vcpu_sbi_extension *sbi_ext;
	struct kvm_cpu_trap utrap = {0};
	struct kvm_vcpu_sbi_return sbi_ret = {
		.out_val = 0,
		.err_val = 0,
		.utrap = &utrap,
	};
	bool ext_is_v01 = false;

	sbi_ext = kvm_vcpu_sbi_find_ext(vcpu, cp->a7);
	if (sbi_ext && sbi_ext->handler) {
#ifdef CONFIG_RISCV_SBI_V01
		if (cp->a7 >= SBI_EXT_0_1_SET_TIMER &&
		    cp->a7 <= SBI_EXT_0_1_SHUTDOWN)
			ext_is_v01 = true;
#endif
		ret = sbi_ext->handler(vcpu, run, &sbi_ret);
	} else {
		/* Return error for unsupported SBI calls */
		cp->a0 = SBI_ERR_NOT_SUPPORTED;
		goto ecall_done;
	}

	/*
	 * When the SBI extension returns a Linux error code, it exits the ioctl
	 * loop and forwards the error to userspace.
	 */
	if (ret < 0) {
		next_sepc = false;
		goto ecall_done;
	}

	/* Handle special error cases i.e trap, exit or userspace forward */
	if (sbi_ret.utrap->scause) {
		/* No need to increment sepc or exit ioctl loop */
		ret = 1;
		sbi_ret.utrap->sepc = cp->sepc;
		kvm_riscv_vcpu_trap_redirect(vcpu, sbi_ret.utrap);
		next_sepc = false;
		goto ecall_done;
	}

	/* Exit ioctl loop or Propagate the error code the guest */
	if (sbi_ret.uexit) {
		next_sepc = false;
		ret = 0;
	} else {
		cp->a0 = sbi_ret.err_val;
		ret = 1;
	}
ecall_done:
	if (next_sepc)
		cp->sepc += 4;
	/* a1 should only be updated when we continue the ioctl loop */
	if (!ext_is_v01 && ret == 1)
		cp->a1 = sbi_ret.out_val;

	return ret;
}

void kvm_riscv_vcpu_sbi_init(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_sbi_context *scontext = &vcpu->arch.sbi_context;
	const struct kvm_riscv_sbi_extension_entry *entry;
	const struct kvm_vcpu_sbi_extension *ext;
	int idx, i;

	for (i = 0; i < ARRAY_SIZE(sbi_ext); i++) {
		entry = &sbi_ext[i];
		ext = entry->ext_ptr;
		idx = entry->ext_idx;

		if (idx < 0 || idx >= ARRAY_SIZE(scontext->ext_status))
			continue;

		if (ext->probe && !ext->probe(vcpu)) {
			scontext->ext_status[idx] = KVM_RISCV_SBI_EXT_STATUS_UNAVAILABLE;
			continue;
		}

		scontext->ext_status[idx] = ext->default_disabled ?
					KVM_RISCV_SBI_EXT_STATUS_DISABLED :
					KVM_RISCV_SBI_EXT_STATUS_ENABLED;
	}
}
