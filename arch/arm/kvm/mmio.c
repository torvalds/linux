/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kvm_host.h>
#include <asm/kvm_mmio.h>
#include <asm/kvm_emulate.h>
#include <trace/events/kvm.h>

#include "trace.h"

static void mmio_write_buf(char *buf, unsigned int len, unsigned long data)
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

static unsigned long mmio_read_buf(char *buf, unsigned int len)
{
	unsigned long data = 0;
	union {
		u16	hword;
		u32	word;
		u64	dword;
	} tmp;

	switch (len) {
	case 1:
		data = buf[0];
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

/**
 * kvm_handle_mmio_return -- Handle MMIO loads after user space emulation
 * @vcpu: The VCPU pointer
 * @run:  The VCPU run struct containing the mmio data
 *
 * This should only be called after returning from userspace for MMIO load
 * emulation.
 */
int kvm_handle_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long data;
	unsigned int len;
	int mask;

	if (!run->mmio.is_write) {
		len = run->mmio.len;
		if (len > sizeof(unsigned long))
			return -EINVAL;

		data = mmio_read_buf(run->mmio.data, len);

		if (vcpu->arch.mmio_decode.sign_extend &&
		    len < sizeof(unsigned long)) {
			mask = 1U << ((len * 8) - 1);
			data = (data ^ mask) - mask;
		}

		trace_kvm_mmio(KVM_TRACE_MMIO_READ, len, run->mmio.phys_addr,
			       data);
		data = vcpu_data_host_to_guest(vcpu, data, len);
		vcpu_set_reg(vcpu, vcpu->arch.mmio_decode.rt, data);
	}

	return 0;
}

static int decode_hsr(struct kvm_vcpu *vcpu, bool *is_write, int *len)
{
	unsigned long rt;
	int access_size;
	bool sign_extend;

	if (kvm_vcpu_dabt_isextabt(vcpu)) {
		/* cache operation on I/O addr, tell guest unsupported */
		kvm_inject_dabt(vcpu, kvm_vcpu_get_hfar(vcpu));
		return 1;
	}

	if (kvm_vcpu_dabt_iss1tw(vcpu)) {
		/* page table accesses IO mem: tell guest to fix its TTBR */
		kvm_inject_dabt(vcpu, kvm_vcpu_get_hfar(vcpu));
		return 1;
	}

	access_size = kvm_vcpu_dabt_get_as(vcpu);
	if (unlikely(access_size < 0))
		return access_size;

	*is_write = kvm_vcpu_dabt_iswrite(vcpu);
	sign_extend = kvm_vcpu_dabt_issext(vcpu);
	rt = kvm_vcpu_dabt_get_rd(vcpu);

	*len = access_size;
	vcpu->arch.mmio_decode.sign_extend = sign_extend;
	vcpu->arch.mmio_decode.rt = rt;

	/*
	 * The MMIO instruction is emulated and should not be re-executed
	 * in the guest.
	 */
	kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
	return 0;
}

int io_mem_abort(struct kvm_vcpu *vcpu, struct kvm_run *run,
		 phys_addr_t fault_ipa)
{
	unsigned long data;
	unsigned long rt;
	int ret;
	bool is_write;
	int len;
	u8 data_buf[8];

	/*
	 * Prepare MMIO operation. First decode the syndrome data we get
	 * from the CPU. Then try if some in-kernel emulation feels
	 * responsible, otherwise let user space do its magic.
	 */
	if (kvm_vcpu_dabt_isvalid(vcpu)) {
		ret = decode_hsr(vcpu, &is_write, &len);
		if (ret)
			return ret;
	} else {
		kvm_err("load/store instruction decoding not implemented\n");
		return -ENOSYS;
	}

	rt = vcpu->arch.mmio_decode.rt;

	if (is_write) {
		data = vcpu_data_guest_to_host(vcpu, vcpu_get_reg(vcpu, rt),
					       len);

		trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, len, fault_ipa, data);
		mmio_write_buf(data_buf, len, data);

		ret = kvm_io_bus_write(vcpu, KVM_MMIO_BUS, fault_ipa, len,
				       data_buf);
	} else {
		trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, len,
			       fault_ipa, 0);

		ret = kvm_io_bus_read(vcpu, KVM_MMIO_BUS, fault_ipa, len,
				      data_buf);
	}

	/* Now prepare kvm_run for the potential return to userland. */
	run->mmio.is_write	= is_write;
	run->mmio.phys_addr	= fault_ipa;
	run->mmio.len		= len;
	if (is_write)
		memcpy(run->mmio.data, data_buf, len);

	if (!ret) {
		/* We handled the access successfully in the kernel. */
		vcpu->stat.mmio_exit_kernel++;
		kvm_handle_mmio_return(vcpu, run);
		return 1;
	} else {
		vcpu->stat.mmio_exit_user++;
	}

	run->exit_reason	= KVM_EXIT_MMIO;
	return 0;
}
