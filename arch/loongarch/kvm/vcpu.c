// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <linux/entry-kvm.h>
#include <asm/fpu.h>
#include <asm/loongarch.h>
#include <asm/setup.h>
#include <asm/time.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

/*
 * kvm_check_requests - check and handle pending vCPU requests
 *
 * Return: RESUME_GUEST if we should enter the guest
 *         RESUME_HOST  if we should exit to userspace
 */
static int kvm_check_requests(struct kvm_vcpu *vcpu)
{
	if (!kvm_request_pending(vcpu))
		return RESUME_GUEST;

	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
		vcpu->arch.vpid = 0;  /* Drop vpid for this vCPU */

	if (kvm_dirty_ring_check_request(vcpu))
		return RESUME_HOST;

	return RESUME_GUEST;
}

/*
 * Check and handle pending signal and vCPU requests etc
 * Run with irq enabled and preempt enabled
 *
 * Return: RESUME_GUEST if we should enter the guest
 *         RESUME_HOST  if we should exit to userspace
 *         < 0 if we should exit to userspace, where the return value
 *         indicates an error
 */
static int kvm_enter_guest_check(struct kvm_vcpu *vcpu)
{
	int ret;

	/*
	 * Check conditions before entering the guest
	 */
	ret = xfer_to_guest_mode_handle_work(vcpu);
	if (ret < 0)
		return ret;

	ret = kvm_check_requests(vcpu);

	return ret;
}

/*
 * Called with irq enabled
 *
 * Return: RESUME_GUEST if we should enter the guest, and irq disabled
 *         Others if we should exit to userspace
 */
static int kvm_pre_enter_guest(struct kvm_vcpu *vcpu)
{
	int ret;

	do {
		ret = kvm_enter_guest_check(vcpu);
		if (ret != RESUME_GUEST)
			break;

		/*
		 * Handle vcpu timer, interrupts, check requests and
		 * check vmid before vcpu enter guest
		 */
		local_irq_disable();
		kvm_acquire_timer(vcpu);
		kvm_deliver_intr(vcpu);
		kvm_deliver_exception(vcpu);
		/* Make sure the vcpu mode has been written */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);
		kvm_check_vpid(vcpu);
		vcpu->arch.host_eentry = csr_read64(LOONGARCH_CSR_EENTRY);
		/* Clear KVM_LARCH_SWCSR_LATEST as CSR will change when enter guest */
		vcpu->arch.aux_inuse &= ~KVM_LARCH_SWCSR_LATEST;

		if (kvm_request_pending(vcpu) || xfer_to_guest_mode_work_pending()) {
			/* make sure the vcpu mode has been written */
			smp_store_mb(vcpu->mode, OUTSIDE_GUEST_MODE);
			local_irq_enable();
			ret = -EAGAIN;
		}
	} while (ret != RESUME_GUEST);

	return ret;
}

/*
 * Return 1 for resume guest and "<= 0" for resume host.
 */
static int kvm_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int ret = RESUME_GUEST;
	unsigned long estat = vcpu->arch.host_estat;
	u32 intr = estat & 0x1fff; /* Ignore NMI */
	u32 ecode = (estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT;

	vcpu->mode = OUTSIDE_GUEST_MODE;

	/* Set a default exit reason */
	run->exit_reason = KVM_EXIT_UNKNOWN;

	guest_timing_exit_irqoff();
	guest_state_exit_irqoff();
	local_irq_enable();

	trace_kvm_exit(vcpu, ecode);
	if (ecode) {
		ret = kvm_handle_fault(vcpu, ecode);
	} else {
		WARN(!intr, "vm exiting with suspicious irq\n");
		++vcpu->stat.int_exits;
	}

	if (ret == RESUME_GUEST)
		ret = kvm_pre_enter_guest(vcpu);

	if (ret != RESUME_GUEST) {
		local_irq_disable();
		return ret;
	}

	guest_timing_enter_irqoff();
	guest_state_enter_irqoff();
	trace_kvm_reenter(vcpu);

	return RESUME_GUEST;
}

static int _kvm_getcsr(struct kvm_vcpu *vcpu, unsigned int id, u64 *val)
{
	unsigned long gintc;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(id) & INVALID_GCSR)
		return -EINVAL;

	if (id == LOONGARCH_CSR_ESTAT) {
		/* ESTAT IP0~IP7 get from GINTC */
		gintc = kvm_read_sw_gcsr(csr, LOONGARCH_CSR_GINTC) & 0xff;
		*val = kvm_read_sw_gcsr(csr, LOONGARCH_CSR_ESTAT) | (gintc << 2);
		return 0;
	}

	/*
	 * Get software CSR state since software state is consistent
	 * with hardware for synchronous ioctl
	 */
	*val = kvm_read_sw_gcsr(csr, id);

	return 0;
}

static int _kvm_setcsr(struct kvm_vcpu *vcpu, unsigned int id, u64 val)
{
	int ret = 0, gintc;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(id) & INVALID_GCSR)
		return -EINVAL;

	if (id == LOONGARCH_CSR_ESTAT) {
		/* ESTAT IP0~IP7 inject through GINTC */
		gintc = (val >> 2) & 0xff;
		kvm_set_sw_gcsr(csr, LOONGARCH_CSR_GINTC, gintc);

		gintc = val & ~(0xffUL << 2);
		kvm_set_sw_gcsr(csr, LOONGARCH_CSR_ESTAT, gintc);

		return ret;
	}

	kvm_write_sw_gcsr(csr, id, val);

	return ret;
}

static int kvm_get_one_reg(struct kvm_vcpu *vcpu,
		const struct kvm_one_reg *reg, u64 *v)
{
	int id, ret = 0;
	u64 type = reg->id & KVM_REG_LOONGARCH_MASK;

	switch (type) {
	case KVM_REG_LOONGARCH_CSR:
		id = KVM_GET_IOC_CSR_IDX(reg->id);
		ret = _kvm_getcsr(vcpu, id, v);
		break;
	case KVM_REG_LOONGARCH_CPUCFG:
		id = KVM_GET_IOC_CPUCFG_IDX(reg->id);
		if (id >= 0 && id < KVM_MAX_CPUCFG_REGS)
			*v = vcpu->arch.cpucfg[id];
		else
			ret = -EINVAL;
		break;
	case KVM_REG_LOONGARCH_KVM:
		switch (reg->id) {
		case KVM_REG_LOONGARCH_COUNTER:
			*v = drdtime() + vcpu->kvm->arch.time_offset;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int kvm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	int ret = 0;
	u64 v, size = reg->id & KVM_REG_SIZE_MASK;

	switch (size) {
	case KVM_REG_SIZE_U64:
		ret = kvm_get_one_reg(vcpu, reg, &v);
		if (ret)
			return ret;
		ret = put_user(v, (u64 __user *)(long)reg->addr);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int kvm_set_one_reg(struct kvm_vcpu *vcpu,
			const struct kvm_one_reg *reg, u64 v)
{
	int id, ret = 0;
	u64 type = reg->id & KVM_REG_LOONGARCH_MASK;

	switch (type) {
	case KVM_REG_LOONGARCH_CSR:
		id = KVM_GET_IOC_CSR_IDX(reg->id);
		ret = _kvm_setcsr(vcpu, id, v);
		break;
	case KVM_REG_LOONGARCH_CPUCFG:
		id = KVM_GET_IOC_CPUCFG_IDX(reg->id);
		if (id >= 0 && id < KVM_MAX_CPUCFG_REGS)
			vcpu->arch.cpucfg[id] = (u32)v;
		else
			ret = -EINVAL;
		break;
	case KVM_REG_LOONGARCH_KVM:
		switch (reg->id) {
		case KVM_REG_LOONGARCH_COUNTER:
			/*
			 * gftoffset is relative with board, not vcpu
			 * only set for the first time for smp system
			 */
			if (vcpu->vcpu_id == 0)
				vcpu->kvm->arch.time_offset = (signed long)(v - drdtime());
			break;
		case KVM_REG_LOONGARCH_VCPU_RESET:
			kvm_reset_timer(vcpu);
			memset(&vcpu->arch.irq_pending, 0, sizeof(vcpu->arch.irq_pending));
			memset(&vcpu->arch.irq_clear, 0, sizeof(vcpu->arch.irq_clear));
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int kvm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	int ret = 0;
	u64 v, size = reg->id & KVM_REG_SIZE_MASK;

	switch (size) {
	case KVM_REG_SIZE_U64:
		ret = get_user(v, (u64 __user *)(long)reg->addr);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return kvm_set_one_reg(vcpu, reg, v);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		regs->gpr[i] = vcpu->arch.gprs[i];

	regs->pc = vcpu->arch.pc;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		vcpu->arch.gprs[i] = regs->gpr[i];

	vcpu->arch.gprs[0] = 0; /* zero is special, and cannot be set. */
	vcpu->arch.pc = regs->pc;

	return 0;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	/* FPU is enabled by default, will support LSX/LASX later. */
	return -EINVAL;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	long r;
	void __user *argp = (void __user *)arg;
	struct kvm_vcpu *vcpu = filp->private_data;

	/*
	 * Only software CSR should be modified
	 *
	 * If any hardware CSR register is modified, vcpu_load/vcpu_put pair
	 * should be used. Since CSR registers owns by this vcpu, if switch
	 * to other vcpus, other vcpus need reload CSR registers.
	 *
	 * If software CSR is modified, bit KVM_LARCH_HWCSR_USABLE should
	 * be clear in vcpu->arch.aux_inuse, and vcpu_load will check
	 * aux_inuse flag and reload CSR registers form software.
	 */

	switch (ioctl) {
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;
		if (ioctl == KVM_SET_ONE_REG) {
			r = kvm_set_reg(vcpu, &reg);
			vcpu->arch.aux_inuse &= ~KVM_LARCH_HWCSR_USABLE;
		} else
			r = kvm_get_reg(vcpu, &reg);
		break;
	}
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			break;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}
	default:
		r = -ENOIOCTLCMD;
		break;
	}

	return r;
}

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	unsigned long timer_hz;
	struct loongarch_csrs *csr;

	vcpu->arch.vpid = 0;

	hrtimer_init(&vcpu->arch.swtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	vcpu->arch.swtimer.function = kvm_swtimer_wakeup;

	vcpu->arch.handle_exit = kvm_handle_exit;
	vcpu->arch.guest_eentry = (unsigned long)kvm_loongarch_ops->exc_entry;
	vcpu->arch.csr = kzalloc(sizeof(struct loongarch_csrs), GFP_KERNEL);
	if (!vcpu->arch.csr)
		return -ENOMEM;

	/*
	 * All kvm exceptions share one exception entry, and host <-> guest
	 * switch also switch ECFG.VS field, keep host ECFG.VS info here.
	 */
	vcpu->arch.host_ecfg = (read_csr_ecfg() & CSR_ECFG_VS);

	/* Init */
	vcpu->arch.last_sched_cpu = -1;

	/*
	 * Initialize guest register state to valid architectural reset state.
	 */
	timer_hz = calc_const_freq();
	kvm_init_timer(vcpu, timer_hz);

	/* Set Initialize mode for guest */
	csr = vcpu->arch.csr;
	kvm_write_sw_gcsr(csr, LOONGARCH_CSR_CRMD, CSR_CRMD_DA);

	/* Set cpuid */
	kvm_write_sw_gcsr(csr, LOONGARCH_CSR_TMID, vcpu->vcpu_id);

	/* Start with no pending virtual guest interrupts */
	csr->csrs[LOONGARCH_CSR_GINTC] = 0;

	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int cpu;
	struct kvm_context *context;

	hrtimer_cancel(&vcpu->arch.swtimer);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
	kfree(vcpu->arch.csr);

	/*
	 * If the vCPU is freed and reused as another vCPU, we don't want the
	 * matching pointer wrongly hanging around in last_vcpu.
	 */
	for_each_possible_cpu(cpu) {
		context = per_cpu_ptr(vcpu->kvm->arch.vmcs, cpu);
		if (context->last_vcpu == vcpu)
			context->last_vcpu = NULL;
	}
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	int r = -EINTR;
	struct kvm_run *run = vcpu->run;

	if (vcpu->mmio_needed) {
		if (!vcpu->mmio_is_write)
			kvm_complete_mmio_read(vcpu, run);
		vcpu->mmio_needed = 0;
	}

	if (run->exit_reason == KVM_EXIT_LOONGARCH_IOCSR) {
		if (!run->iocsr_io.is_write)
			kvm_complete_iocsr_read(vcpu, run);
	}

	if (run->immediate_exit)
		return r;

	/* Clear exit_reason */
	run->exit_reason = KVM_EXIT_UNKNOWN;
	lose_fpu(1);
	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	r = kvm_pre_enter_guest(vcpu);
	if (r != RESUME_GUEST)
		goto out;

	guest_timing_enter_irqoff();
	guest_state_enter_irqoff();
	trace_kvm_enter(vcpu);
	r = kvm_loongarch_ops->enter_guest(run, vcpu);

	trace_kvm_out(vcpu);
	/*
	 * Guest exit is already recorded at kvm_handle_exit()
	 * return value must not be RESUME_GUEST
	 */
	local_irq_enable();
out:
	kvm_sigset_deactivate(vcpu);
	vcpu_put(vcpu);

	return r;
}
