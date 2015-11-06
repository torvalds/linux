/*
 * KVM Microsoft Hyper-V emulation
 *
 * derived from arch/x86/kvm/x86.c
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2008 Qumranet, Inc.
 * Copyright IBM Corporation, 2008
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Amit Shah    <amit.shah@qumranet.com>
 *   Ben-Ami Yassour <benami@il.ibm.com>
 *   Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "x86.h"
#include "lapic.h"
#include "hyperv.h"

#include <linux/kvm_host.h>
#include <trace/events/kvm.h>

#include "trace.h"

static bool kvm_hv_msr_partition_wide(u32 msr)
{
	bool r = false;

	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
	case HV_X64_MSR_HYPERCALL:
	case HV_X64_MSR_REFERENCE_TSC:
	case HV_X64_MSR_TIME_REF_COUNT:
	case HV_X64_MSR_CRASH_CTL:
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
	case HV_X64_MSR_RESET:
		r = true;
		break;
	}

	return r;
}

static int kvm_hv_msr_get_crash_data(struct kvm_vcpu *vcpu,
				     u32 index, u64 *pdata)
{
	struct kvm_hv *hv = &vcpu->kvm->arch.hyperv;

	if (WARN_ON_ONCE(index >= ARRAY_SIZE(hv->hv_crash_param)))
		return -EINVAL;

	*pdata = hv->hv_crash_param[index];
	return 0;
}

static int kvm_hv_msr_get_crash_ctl(struct kvm_vcpu *vcpu, u64 *pdata)
{
	struct kvm_hv *hv = &vcpu->kvm->arch.hyperv;

	*pdata = hv->hv_crash_ctl;
	return 0;
}

static int kvm_hv_msr_set_crash_ctl(struct kvm_vcpu *vcpu, u64 data, bool host)
{
	struct kvm_hv *hv = &vcpu->kvm->arch.hyperv;

	if (host)
		hv->hv_crash_ctl = data & HV_X64_MSR_CRASH_CTL_NOTIFY;

	if (!host && (data & HV_X64_MSR_CRASH_CTL_NOTIFY)) {

		vcpu_debug(vcpu, "hv crash (0x%llx 0x%llx 0x%llx 0x%llx 0x%llx)\n",
			  hv->hv_crash_param[0],
			  hv->hv_crash_param[1],
			  hv->hv_crash_param[2],
			  hv->hv_crash_param[3],
			  hv->hv_crash_param[4]);

		/* Send notification about crash to user space */
		kvm_make_request(KVM_REQ_HV_CRASH, vcpu);
	}

	return 0;
}

static int kvm_hv_msr_set_crash_data(struct kvm_vcpu *vcpu,
				     u32 index, u64 data)
{
	struct kvm_hv *hv = &vcpu->kvm->arch.hyperv;

	if (WARN_ON_ONCE(index >= ARRAY_SIZE(hv->hv_crash_param)))
		return -EINVAL;

	hv->hv_crash_param[index] = data;
	return 0;
}

static int kvm_hv_set_msr_pw(struct kvm_vcpu *vcpu, u32 msr, u64 data,
			     bool host)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_hv *hv = &kvm->arch.hyperv;

	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
		hv->hv_guest_os_id = data;
		/* setting guest os id to zero disables hypercall page */
		if (!hv->hv_guest_os_id)
			hv->hv_hypercall &= ~HV_X64_MSR_HYPERCALL_ENABLE;
		break;
	case HV_X64_MSR_HYPERCALL: {
		u64 gfn;
		unsigned long addr;
		u8 instructions[4];

		/* if guest os id is not set hypercall should remain disabled */
		if (!hv->hv_guest_os_id)
			break;
		if (!(data & HV_X64_MSR_HYPERCALL_ENABLE)) {
			hv->hv_hypercall = data;
			break;
		}
		gfn = data >> HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT;
		addr = gfn_to_hva(kvm, gfn);
		if (kvm_is_error_hva(addr))
			return 1;
		kvm_x86_ops->patch_hypercall(vcpu, instructions);
		((unsigned char *)instructions)[3] = 0xc3; /* ret */
		if (__copy_to_user((void __user *)addr, instructions, 4))
			return 1;
		hv->hv_hypercall = data;
		mark_page_dirty(kvm, gfn);
		break;
	}
	case HV_X64_MSR_REFERENCE_TSC: {
		u64 gfn;
		HV_REFERENCE_TSC_PAGE tsc_ref;

		memset(&tsc_ref, 0, sizeof(tsc_ref));
		hv->hv_tsc_page = data;
		if (!(data & HV_X64_MSR_TSC_REFERENCE_ENABLE))
			break;
		gfn = data >> HV_X64_MSR_TSC_REFERENCE_ADDRESS_SHIFT;
		if (kvm_write_guest(
				kvm,
				gfn << HV_X64_MSR_TSC_REFERENCE_ADDRESS_SHIFT,
				&tsc_ref, sizeof(tsc_ref)))
			return 1;
		mark_page_dirty(kvm, gfn);
		break;
	}
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
		return kvm_hv_msr_set_crash_data(vcpu,
						 msr - HV_X64_MSR_CRASH_P0,
						 data);
	case HV_X64_MSR_CRASH_CTL:
		return kvm_hv_msr_set_crash_ctl(vcpu, data, host);
	case HV_X64_MSR_RESET:
		if (data == 1) {
			vcpu_debug(vcpu, "hyper-v reset requested\n");
			kvm_make_request(KVM_REQ_HV_RESET, vcpu);
		}
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V uhandled wrmsr: 0x%x data 0x%llx\n",
			    msr, data);
		return 1;
	}
	return 0;
}

/* Calculate cpu time spent by current task in 100ns units */
static u64 current_task_runtime_100ns(void)
{
	cputime_t utime, stime;

	task_cputime_adjusted(current, &utime, &stime);
	return div_u64(cputime_to_nsecs(utime + stime), 100);
}

static int kvm_hv_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data, bool host)
{
	struct kvm_vcpu_hv *hv = &vcpu->arch.hyperv;

	switch (msr) {
	case HV_X64_MSR_APIC_ASSIST_PAGE: {
		u64 gfn;
		unsigned long addr;

		if (!(data & HV_X64_MSR_APIC_ASSIST_PAGE_ENABLE)) {
			hv->hv_vapic = data;
			if (kvm_lapic_enable_pv_eoi(vcpu, 0))
				return 1;
			break;
		}
		gfn = data >> HV_X64_MSR_APIC_ASSIST_PAGE_ADDRESS_SHIFT;
		addr = kvm_vcpu_gfn_to_hva(vcpu, gfn);
		if (kvm_is_error_hva(addr))
			return 1;
		if (__clear_user((void __user *)addr, PAGE_SIZE))
			return 1;
		hv->hv_vapic = data;
		kvm_vcpu_mark_page_dirty(vcpu, gfn);
		if (kvm_lapic_enable_pv_eoi(vcpu,
					    gfn_to_gpa(gfn) | KVM_MSR_ENABLED))
			return 1;
		break;
	}
	case HV_X64_MSR_EOI:
		return kvm_hv_vapic_msr_write(vcpu, APIC_EOI, data);
	case HV_X64_MSR_ICR:
		return kvm_hv_vapic_msr_write(vcpu, APIC_ICR, data);
	case HV_X64_MSR_TPR:
		return kvm_hv_vapic_msr_write(vcpu, APIC_TASKPRI, data);
	case HV_X64_MSR_VP_RUNTIME:
		if (!host)
			return 1;
		hv->runtime_offset = data - current_task_runtime_100ns();
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V uhandled wrmsr: 0x%x data 0x%llx\n",
			    msr, data);
		return 1;
	}

	return 0;
}

static int kvm_hv_get_msr_pw(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data = 0;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_hv *hv = &kvm->arch.hyperv;

	switch (msr) {
	case HV_X64_MSR_GUEST_OS_ID:
		data = hv->hv_guest_os_id;
		break;
	case HV_X64_MSR_HYPERCALL:
		data = hv->hv_hypercall;
		break;
	case HV_X64_MSR_TIME_REF_COUNT: {
		data =
		     div_u64(get_kernel_ns() + kvm->arch.kvmclock_offset, 100);
		break;
	}
	case HV_X64_MSR_REFERENCE_TSC:
		data = hv->hv_tsc_page;
		break;
	case HV_X64_MSR_CRASH_P0 ... HV_X64_MSR_CRASH_P4:
		return kvm_hv_msr_get_crash_data(vcpu,
						 msr - HV_X64_MSR_CRASH_P0,
						 pdata);
	case HV_X64_MSR_CRASH_CTL:
		return kvm_hv_msr_get_crash_ctl(vcpu, pdata);
	case HV_X64_MSR_RESET:
		data = 0;
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V unhandled rdmsr: 0x%x\n", msr);
		return 1;
	}

	*pdata = data;
	return 0;
}

static int kvm_hv_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data = 0;
	struct kvm_vcpu_hv *hv = &vcpu->arch.hyperv;

	switch (msr) {
	case HV_X64_MSR_VP_INDEX: {
		int r;
		struct kvm_vcpu *v;

		kvm_for_each_vcpu(r, v, vcpu->kvm) {
			if (v == vcpu) {
				data = r;
				break;
			}
		}
		break;
	}
	case HV_X64_MSR_EOI:
		return kvm_hv_vapic_msr_read(vcpu, APIC_EOI, pdata);
	case HV_X64_MSR_ICR:
		return kvm_hv_vapic_msr_read(vcpu, APIC_ICR, pdata);
	case HV_X64_MSR_TPR:
		return kvm_hv_vapic_msr_read(vcpu, APIC_TASKPRI, pdata);
	case HV_X64_MSR_APIC_ASSIST_PAGE:
		data = hv->hv_vapic;
		break;
	case HV_X64_MSR_VP_RUNTIME:
		data = current_task_runtime_100ns() + hv->runtime_offset;
		break;
	default:
		vcpu_unimpl(vcpu, "Hyper-V unhandled rdmsr: 0x%x\n", msr);
		return 1;
	}
	*pdata = data;
	return 0;
}

int kvm_hv_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data, bool host)
{
	if (kvm_hv_msr_partition_wide(msr)) {
		int r;

		mutex_lock(&vcpu->kvm->lock);
		r = kvm_hv_set_msr_pw(vcpu, msr, data, host);
		mutex_unlock(&vcpu->kvm->lock);
		return r;
	} else
		return kvm_hv_set_msr(vcpu, msr, data, host);
}

int kvm_hv_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	if (kvm_hv_msr_partition_wide(msr)) {
		int r;

		mutex_lock(&vcpu->kvm->lock);
		r = kvm_hv_get_msr_pw(vcpu, msr, pdata);
		mutex_unlock(&vcpu->kvm->lock);
		return r;
	} else
		return kvm_hv_get_msr(vcpu, msr, pdata);
}

bool kvm_hv_hypercall_enabled(struct kvm *kvm)
{
	return kvm->arch.hyperv.hv_hypercall & HV_X64_MSR_HYPERCALL_ENABLE;
}

int kvm_hv_hypercall(struct kvm_vcpu *vcpu)
{
	u64 param, ingpa, outgpa, ret;
	uint16_t code, rep_idx, rep_cnt, res = HV_STATUS_SUCCESS, rep_done = 0;
	bool fast, longmode;

	/*
	 * hypercall generates UD from non zero cpl and real mode
	 * per HYPER-V spec
	 */
	if (kvm_x86_ops->get_cpl(vcpu) != 0 || !is_protmode(vcpu)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 0;
	}

	longmode = is_64_bit_mode(vcpu);

	if (!longmode) {
		param = ((u64)kvm_register_read(vcpu, VCPU_REGS_RDX) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RAX) & 0xffffffff);
		ingpa = ((u64)kvm_register_read(vcpu, VCPU_REGS_RBX) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RCX) & 0xffffffff);
		outgpa = ((u64)kvm_register_read(vcpu, VCPU_REGS_RDI) << 32) |
			(kvm_register_read(vcpu, VCPU_REGS_RSI) & 0xffffffff);
	}
#ifdef CONFIG_X86_64
	else {
		param = kvm_register_read(vcpu, VCPU_REGS_RCX);
		ingpa = kvm_register_read(vcpu, VCPU_REGS_RDX);
		outgpa = kvm_register_read(vcpu, VCPU_REGS_R8);
	}
#endif

	code = param & 0xffff;
	fast = (param >> 16) & 0x1;
	rep_cnt = (param >> 32) & 0xfff;
	rep_idx = (param >> 48) & 0xfff;

	trace_kvm_hv_hypercall(code, fast, rep_cnt, rep_idx, ingpa, outgpa);

	switch (code) {
	case HV_X64_HV_NOTIFY_LONG_SPIN_WAIT:
		kvm_vcpu_on_spin(vcpu);
		break;
	default:
		res = HV_STATUS_INVALID_HYPERCALL_CODE;
		break;
	}

	ret = res | (((u64)rep_done & 0xfff) << 32);
	if (longmode) {
		kvm_register_write(vcpu, VCPU_REGS_RAX, ret);
	} else {
		kvm_register_write(vcpu, VCPU_REGS_RDX, ret >> 32);
		kvm_register_write(vcpu, VCPU_REGS_RAX, ret & 0xffffffff);
	}

	return 1;
}
