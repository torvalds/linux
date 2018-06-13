// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * KVM Xen emulation
 */

#include "x86.h"
#include "xen.h"
#include "hyperv.h"

#include <linux/kvm_host.h>

#include <trace/events/kvm.h>

#include "trace.h"

int kvm_xen_write_hypercall_page(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm *kvm = vcpu->kvm;
	u32 page_num = data & ~PAGE_MASK;
	u64 page_addr = data & PAGE_MASK;

	/*
	 * If Xen hypercall intercept is enabled, fill the hypercall
	 * page with VMCALL/VMMCALL instructions since that's what
	 * we catch. Else the VMM has provided the hypercall pages
	 * with instructions of its own choosing, so use those.
	 */
	if (kvm_xen_hypercall_enabled(kvm)) {
		u8 instructions[32];
		int i;

		if (page_num)
			return 1;

		/* mov imm32, %eax */
		instructions[0] = 0xb8;

		/* vmcall / vmmcall */
		kvm_x86_ops.patch_hypercall(vcpu, instructions + 5);

		/* ret */
		instructions[8] = 0xc3;

		/* int3 to pad */
		memset(instructions + 9, 0xcc, sizeof(instructions) - 9);

		for (i = 0; i < PAGE_SIZE / sizeof(instructions); i++) {
			*(u32 *)&instructions[1] = i;
			if (kvm_vcpu_write_guest(vcpu,
						 page_addr + (i * sizeof(instructions)),
						 instructions, sizeof(instructions)))
				return 1;
		}
	} else {
		int lm = is_long_mode(vcpu);
		u64 blob_addr = lm ? kvm->arch.xen_hvm_config.blob_addr_64
				   : kvm->arch.xen_hvm_config.blob_addr_32;
		u8 blob_size = lm ? kvm->arch.xen_hvm_config.blob_size_64
				  : kvm->arch.xen_hvm_config.blob_size_32;
		u8 *page;

		if (page_num >= blob_size)
			return 1;

		blob_addr += page_num * PAGE_SIZE;

		page = memdup_user((u8 __user *)blob_addr, PAGE_SIZE);
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (kvm_vcpu_write_guest(vcpu, page_addr, page, PAGE_SIZE)) {
			kfree(page);
			return 1;
		}
	}
	return 0;
}

static int kvm_xen_hypercall_set_result(struct kvm_vcpu *vcpu, u64 result)
{
	kvm_rax_write(vcpu, result);
	return kvm_skip_emulated_instruction(vcpu);
}

static int kvm_xen_hypercall_complete_userspace(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (unlikely(!kvm_is_linear_rip(vcpu, vcpu->arch.xen.hypercall_rip)))
		return 1;

	return kvm_xen_hypercall_set_result(vcpu, run->xen.u.hcall.result);
}

int kvm_xen_hypercall(struct kvm_vcpu *vcpu)
{
	bool longmode;
	u64 input, params[6];

	input = (u64)kvm_register_read(vcpu, VCPU_REGS_RAX);

	/* Hyper-V hypercalls get bit 31 set in EAX */
	if ((input & 0x80000000) &&
	    kvm_hv_hypercall_enabled(vcpu->kvm))
		return kvm_hv_hypercall(vcpu);

	longmode = is_64_bit_mode(vcpu);
	if (!longmode) {
		params[0] = (u32)kvm_rbx_read(vcpu);
		params[1] = (u32)kvm_rcx_read(vcpu);
		params[2] = (u32)kvm_rdx_read(vcpu);
		params[3] = (u32)kvm_rsi_read(vcpu);
		params[4] = (u32)kvm_rdi_read(vcpu);
		params[5] = (u32)kvm_rbp_read(vcpu);
	}
#ifdef CONFIG_X86_64
	else {
		params[0] = (u64)kvm_rdi_read(vcpu);
		params[1] = (u64)kvm_rsi_read(vcpu);
		params[2] = (u64)kvm_rdx_read(vcpu);
		params[3] = (u64)kvm_r10_read(vcpu);
		params[4] = (u64)kvm_r8_read(vcpu);
		params[5] = (u64)kvm_r9_read(vcpu);
	}
#endif
	trace_kvm_xen_hypercall(input, params[0], params[1], params[2],
				params[3], params[4], params[5]);

	vcpu->run->exit_reason = KVM_EXIT_XEN;
	vcpu->run->xen.type = KVM_EXIT_XEN_HCALL;
	vcpu->run->xen.u.hcall.longmode = longmode;
	vcpu->run->xen.u.hcall.cpl = kvm_x86_ops.get_cpl(vcpu);
	vcpu->run->xen.u.hcall.input = input;
	vcpu->run->xen.u.hcall.params[0] = params[0];
	vcpu->run->xen.u.hcall.params[1] = params[1];
	vcpu->run->xen.u.hcall.params[2] = params[2];
	vcpu->run->xen.u.hcall.params[3] = params[3];
	vcpu->run->xen.u.hcall.params[4] = params[4];
	vcpu->run->xen.u.hcall.params[5] = params[5];
	vcpu->arch.xen.hypercall_rip = kvm_get_linear_rip(vcpu);
	vcpu->arch.complete_userspace_io =
		kvm_xen_hypercall_complete_userspace;

	return 0;
}
