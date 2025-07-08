// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <trace/events/kvm.h>

#include "trace.h"

void kvm_mmio_write_buf(void *buf, unsigned int len, unsigned long data)
{
	void *datap = NULL;
	union {
		u8	byte;
		u16	hword;
		u32	word;
		u64	dword;
	} tmp;

	switch (len) {
	case 1:
		tmp.byte	= data;
		datap		= &tmp.byte;
		break;
	case 2:
		tmp.hword	= data;
		datap		= &tmp.hword;
		break;
	case 4:
		tmp.word	= data;
		datap		= &tmp.word;
		break;
	case 8:
		tmp.dword	= data;
		datap		= &tmp.dword;
		break;
	}

	memcpy(buf, datap, len);
}

unsigned long kvm_mmio_read_buf(const void *buf, unsigned int len)
{
	unsigned long data = 0;
	union {
		u16	hword;
		u32	word;
		u64	dword;
	} tmp;

	switch (len) {
	case 1:
		data = *(u8 *)buf;
		break;
	case 2:
		memcpy(&tmp.hword, buf, len);
		data = tmp.hword;
		break;
	case 4:
		memcpy(&tmp.word, buf, len);
		data = tmp.word;
		break;
	case 8:
		memcpy(&tmp.dword, buf, len);
		data = tmp.dword;
		break;
	}

	return data;
}

static bool kvm_pending_sync_exception(struct kvm_vcpu *vcpu)
{
	if (!vcpu_get_flag(vcpu, PENDING_EXCEPTION))
		return false;

	if (vcpu_el1_is_32bit(vcpu)) {
		switch (vcpu_get_flag(vcpu, EXCEPT_MASK)) {
		case unpack_vcpu_flag(EXCEPT_AA32_UND):
		case unpack_vcpu_flag(EXCEPT_AA32_IABT):
		case unpack_vcpu_flag(EXCEPT_AA32_DABT):
			return true;
		default:
			return false;
		}
	} else {
		switch (vcpu_get_flag(vcpu, EXCEPT_MASK)) {
		case unpack_vcpu_flag(EXCEPT_AA64_EL1_SYNC):
		case unpack_vcpu_flag(EXCEPT_AA64_EL2_SYNC):
			return true;
		default:
			return false;
		}
	}
}

/**
 * kvm_handle_mmio_return -- Handle MMIO loads after user space emulation
 *			     or in-kernel IO emulation
 *
 * @vcpu: The VCPU pointer
 */
int kvm_handle_mmio_return(struct kvm_vcpu *vcpu)
{
	unsigned long data;
	unsigned int len;
	int mask;

	/*
	 * Detect if the MMIO return was already handled or if userspace aborted
	 * the MMIO access.
	 */
	if (unlikely(!vcpu->mmio_needed || kvm_pending_sync_exception(vcpu)))
		return 1;

	vcpu->mmio_needed = 0;

	if (!kvm_vcpu_dabt_iswrite(vcpu)) {
		struct kvm_run *run = vcpu->run;

		len = kvm_vcpu_dabt_get_as(vcpu);
		data = kvm_mmio_read_buf(run->mmio.data, len);

		if (kvm_vcpu_dabt_issext(vcpu) &&
		    len < sizeof(unsigned long)) {
			mask = 1U << ((len * 8) - 1);
			data = (data ^ mask) - mask;
		}

		if (!kvm_vcpu_dabt_issf(vcpu))
			data = data & 0xffffffff;

		trace_kvm_mmio(KVM_TRACE_MMIO_READ, len, run->mmio.phys_addr,
			       &data);
		data = vcpu_data_host_to_guest(vcpu, data, len);
		vcpu_set_reg(vcpu, kvm_vcpu_dabt_get_rd(vcpu), data);
	}

	/*
	 * The MMIO instruction is emulated and should not be re-executed
	 * in the guest.
	 */
	kvm_incr_pc(vcpu);

	return 1;
}

int io_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa)
{
	struct kvm_run *run = vcpu->run;
	unsigned long data;
	unsigned long rt;
	int ret;
	bool is_write;
	int len;
	u8 data_buf[8];

	/*
	 * No valid syndrome? Ask userspace for help if it has
	 * volunteered to do so, and bail out otherwise.
	 *
	 * In the protected VM case, there isn't much userspace can do
	 * though, so directly deliver an exception to the guest.
	 */
	if (!kvm_vcpu_dabt_isvalid(vcpu)) {
		trace_kvm_mmio_nisv(*vcpu_pc(vcpu), kvm_vcpu_get_esr(vcpu),
				    kvm_vcpu_get_hfar(vcpu), fault_ipa);

		if (vcpu_is_protected(vcpu))
			return kvm_inject_sea_dabt(vcpu, kvm_vcpu_get_hfar(vcpu));

		if (test_bit(KVM_ARCH_FLAG_RETURN_NISV_IO_ABORT_TO_USER,
			     &vcpu->kvm->arch.flags)) {
			run->exit_reason = KVM_EXIT_ARM_NISV;
			run->arm_nisv.esr_iss = kvm_vcpu_dabt_iss_nisv_sanitized(vcpu);
			run->arm_nisv.fault_ipa = fault_ipa;
			return 0;
		}

		return -ENOSYS;
	}

	/*
	 * Prepare MMIO operation. First decode the syndrome data we get
	 * from the CPU. Then try if some in-kernel emulation feels
	 * responsible, otherwise let user space do its magic.
	 */
	is_write = kvm_vcpu_dabt_iswrite(vcpu);
	len = kvm_vcpu_dabt_get_as(vcpu);
	rt = kvm_vcpu_dabt_get_rd(vcpu);

	if (is_write) {
		data = vcpu_data_guest_to_host(vcpu, vcpu_get_reg(vcpu, rt),
					       len);

		trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, len, fault_ipa, &data);
		kvm_mmio_write_buf(data_buf, len, data);

		ret = kvm_io_bus_write(vcpu, KVM_MMIO_BUS, fault_ipa, len,
				       data_buf);
	} else {
		trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, len,
			       fault_ipa, NULL);

		ret = kvm_io_bus_read(vcpu, KVM_MMIO_BUS, fault_ipa, len,
				      data_buf);
	}

	/* Now prepare kvm_run for the potential return to userland. */
	run->mmio.is_write	= is_write;
	run->mmio.phys_addr	= fault_ipa;
	run->mmio.len		= len;
	vcpu->mmio_needed	= 1;

	if (!ret) {
		/* We handled the access successfully in the kernel. */
		if (!is_write)
			memcpy(run->mmio.data, data_buf, len);
		vcpu->stat.mmio_exit_kernel++;
		kvm_handle_mmio_return(vcpu);
		return 1;
	}

	if (is_write)
		memcpy(run->mmio.data, data_buf, len);
	vcpu->stat.mmio_exit_user++;
	run->exit_reason	= KVM_EXIT_MMIO;
	return 0;
}
