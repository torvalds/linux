/*
 * handling privileged instructions
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <asm/current.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/sysinfo.h>
#include <asm/ptrace.h>
#include <asm/compat.h>
#include "gaccess.h"
#include "kvm-s390.h"
#include "trace.h"

static int handle_set_prefix(struct kvm_vcpu *vcpu)
{
	u64 operand2;
	u32 address = 0;
	u8 tmp;

	vcpu->stat.instruction_spx++;

	operand2 = kvm_s390_get_base_disp_s(vcpu);

	/* must be word boundary */
	if (operand2 & 3) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	/* get the value */
	if (get_guest(vcpu, address, (u32 *) operand2)) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	address = address & 0x7fffe000u;

	/* make sure that the new value is valid memory */
	if (copy_from_guest_absolute(vcpu, &tmp, address, 1) ||
	   (copy_from_guest_absolute(vcpu, &tmp, address + PAGE_SIZE, 1))) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	kvm_s390_set_prefix(vcpu, address);

	VCPU_EVENT(vcpu, 5, "setting prefix to %x", address);
	trace_kvm_s390_handle_prefix(vcpu, 1, address);
out:
	return 0;
}

static int handle_store_prefix(struct kvm_vcpu *vcpu)
{
	u64 operand2;
	u32 address;

	vcpu->stat.instruction_stpx++;

	operand2 = kvm_s390_get_base_disp_s(vcpu);

	/* must be word boundary */
	if (operand2 & 3) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	address = vcpu->arch.sie_block->prefix;
	address = address & 0x7fffe000u;

	/* get the value */
	if (put_guest(vcpu, address, (u32 *)operand2)) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	VCPU_EVENT(vcpu, 5, "storing prefix to %x", address);
	trace_kvm_s390_handle_prefix(vcpu, 0, address);
out:
	return 0;
}

static int handle_store_cpu_address(struct kvm_vcpu *vcpu)
{
	u64 useraddr;
	int rc;

	vcpu->stat.instruction_stap++;

	useraddr = kvm_s390_get_base_disp_s(vcpu);

	if (useraddr & 1) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	rc = put_guest(vcpu, vcpu->vcpu_id, (u16 *)useraddr);
	if (rc) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	VCPU_EVENT(vcpu, 5, "storing cpu address to %llx", useraddr);
	trace_kvm_s390_handle_stap(vcpu, useraddr);
out:
	return 0;
}

static int handle_skey(struct kvm_vcpu *vcpu)
{
	vcpu->stat.instruction_storage_key++;
	vcpu->arch.sie_block->gpsw.addr -= 4;
	VCPU_EVENT(vcpu, 4, "%s", "retrying storage key operation");
	return 0;
}

static int handle_tpi(struct kvm_vcpu *vcpu)
{
	u64 addr;
	struct kvm_s390_interrupt_info *inti;
	int cc;

	addr = kvm_s390_get_base_disp_s(vcpu);

	inti = kvm_s390_get_io_int(vcpu->kvm, vcpu->run->s.regs.crs[6], 0);
	if (inti) {
		if (addr) {
			/*
			 * Store the two-word I/O interruption code into the
			 * provided area.
			 */
			put_guest(vcpu, inti->io.subchannel_id, (u16 *) addr);
			put_guest(vcpu, inti->io.subchannel_nr, (u16 *) (addr + 2));
			put_guest(vcpu, inti->io.io_int_parm, (u32 *) (addr + 4));
		} else {
			/*
			 * Store the three-word I/O interruption code into
			 * the appropriate lowcore area.
			 */
			put_guest(vcpu, inti->io.subchannel_id, (u16 *) 184);
			put_guest(vcpu, inti->io.subchannel_nr, (u16 *) 186);
			put_guest(vcpu, inti->io.io_int_parm, (u32 *) 188);
			put_guest(vcpu, inti->io.io_int_word, (u32 *) 192);
		}
		cc = 1;
	} else
		cc = 0;
	kfree(inti);
	/* Set condition code and we're done. */
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->arch.sie_block->gpsw.mask |= (cc & 3ul) << 44;
	return 0;
}

static int handle_tsch(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_interrupt_info *inti;

	inti = kvm_s390_get_io_int(vcpu->kvm, 0,
				   vcpu->run->s.regs.gprs[1]);

	/*
	 * Prepare exit to userspace.
	 * We indicate whether we dequeued a pending I/O interrupt
	 * so that userspace can re-inject it if the instruction gets
	 * a program check. While this may re-order the pending I/O
	 * interrupts, this is no problem since the priority is kept
	 * intact.
	 */
	vcpu->run->exit_reason = KVM_EXIT_S390_TSCH;
	vcpu->run->s390_tsch.dequeued = !!inti;
	if (inti) {
		vcpu->run->s390_tsch.subchannel_id = inti->io.subchannel_id;
		vcpu->run->s390_tsch.subchannel_nr = inti->io.subchannel_nr;
		vcpu->run->s390_tsch.io_int_parm = inti->io.io_int_parm;
		vcpu->run->s390_tsch.io_int_word = inti->io.io_int_word;
	}
	vcpu->run->s390_tsch.ipb = vcpu->arch.sie_block->ipb;
	kfree(inti);
	return -EREMOTE;
}

static int handle_io_inst(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 4, "%s", "I/O instruction");

	if (vcpu->kvm->arch.css_support) {
		/*
		 * Most I/O instructions will be handled by userspace.
		 * Exceptions are tpi and the interrupt portion of tsch.
		 */
		if (vcpu->arch.sie_block->ipa == 0xb236)
			return handle_tpi(vcpu);
		if (vcpu->arch.sie_block->ipa == 0xb235)
			return handle_tsch(vcpu);
		/* Handle in userspace. */
		return -EOPNOTSUPP;
	} else {
		/*
		 * Set condition code 3 to stop the guest from issueing channel
		 * I/O instructions.
		 */
		vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
		vcpu->arch.sie_block->gpsw.mask |= (3 & 3ul) << 44;
		return 0;
	}
}

static int handle_stfl(struct kvm_vcpu *vcpu)
{
	unsigned int facility_list;
	int rc;

	vcpu->stat.instruction_stfl++;
	/* only pass the facility bits, which we can handle */
	facility_list = S390_lowcore.stfl_fac_list & 0xff00fff3;

	rc = copy_to_guest(vcpu, offsetof(struct _lowcore, stfl_fac_list),
			   &facility_list, sizeof(facility_list));
	if (rc)
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	else {
		VCPU_EVENT(vcpu, 5, "store facility list value %x",
			   facility_list);
		trace_kvm_s390_handle_stfl(vcpu, facility_list);
	}
	return 0;
}

static void handle_new_psw(struct kvm_vcpu *vcpu)
{
	/* Check whether the new psw is enabled for machine checks. */
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_MCHECK)
		kvm_s390_deliver_pending_machine_checks(vcpu);
}

#define PSW_MASK_ADDR_MODE (PSW_MASK_EA | PSW_MASK_BA)
#define PSW_MASK_UNASSIGNED 0xb80800fe7fffffffUL
#define PSW_ADDR_24 0x00000000000fffffUL
#define PSW_ADDR_31 0x000000007fffffffUL

int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu)
{
	u64 addr;
	psw_compat_t new_psw;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);

	addr = kvm_s390_get_base_disp_s(vcpu);

	if (addr & 7) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	if (copy_from_guest(vcpu, &new_psw, addr, sizeof(new_psw))) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	if (!(new_psw.mask & PSW32_MASK_BASE)) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	vcpu->arch.sie_block->gpsw.mask =
		(new_psw.mask & ~PSW32_MASK_BASE) << 32;
	vcpu->arch.sie_block->gpsw.addr = new_psw.addr;

	if ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_UNASSIGNED) ||
	    (!(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_ADDR_MODE) &&
	     (vcpu->arch.sie_block->gpsw.addr & ~PSW_ADDR_24)) ||
	    ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_ADDR_MODE) ==
	     PSW_MASK_EA)) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	handle_new_psw(vcpu);
out:
	return 0;
}

static int handle_lpswe(struct kvm_vcpu *vcpu)
{
	u64 addr;
	psw_t new_psw;

	addr = kvm_s390_get_base_disp_s(vcpu);

	if (addr & 7) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	if (copy_from_guest(vcpu, &new_psw, addr, sizeof(new_psw))) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	vcpu->arch.sie_block->gpsw.mask = new_psw.mask;
	vcpu->arch.sie_block->gpsw.addr = new_psw.addr;

	if ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_UNASSIGNED) ||
	    (((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_ADDR_MODE) ==
	      PSW_MASK_BA) &&
	     (vcpu->arch.sie_block->gpsw.addr & ~PSW_ADDR_31)) ||
	    (!(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_ADDR_MODE) &&
	     (vcpu->arch.sie_block->gpsw.addr & ~PSW_ADDR_24)) ||
	    ((vcpu->arch.sie_block->gpsw.mask & PSW_MASK_ADDR_MODE) ==
	     PSW_MASK_EA)) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	handle_new_psw(vcpu);
out:
	return 0;
}

static int handle_stidp(struct kvm_vcpu *vcpu)
{
	u64 operand2;
	int rc;

	vcpu->stat.instruction_stidp++;

	operand2 = kvm_s390_get_base_disp_s(vcpu);

	if (operand2 & 7) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	rc = put_guest(vcpu, vcpu->arch.stidp_data, (u64 *)operand2);
	if (rc) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	VCPU_EVENT(vcpu, 5, "%s", "store cpu id");
out:
	return 0;
}

static void handle_stsi_3_2_2(struct kvm_vcpu *vcpu, struct sysinfo_3_2_2 *mem)
{
	struct kvm_s390_float_interrupt *fi = &vcpu->kvm->arch.float_int;
	int cpus = 0;
	int n;

	spin_lock(&fi->lock);
	for (n = 0; n < KVM_MAX_VCPUS; n++)
		if (fi->local_int[n])
			cpus++;
	spin_unlock(&fi->lock);

	/* deal with other level 3 hypervisors */
	if (stsi(mem, 3, 2, 2))
		mem->count = 0;
	if (mem->count < 8)
		mem->count++;
	for (n = mem->count - 1; n > 0 ; n--)
		memcpy(&mem->vm[n], &mem->vm[n - 1], sizeof(mem->vm[0]));

	mem->vm[0].cpus_total = cpus;
	mem->vm[0].cpus_configured = cpus;
	mem->vm[0].cpus_standby = 0;
	mem->vm[0].cpus_reserved = 0;
	mem->vm[0].caf = 1000;
	memcpy(mem->vm[0].name, "KVMguest", 8);
	ASCEBC(mem->vm[0].name, 8);
	memcpy(mem->vm[0].cpi, "KVM/Linux       ", 16);
	ASCEBC(mem->vm[0].cpi, 16);
}

static int handle_stsi(struct kvm_vcpu *vcpu)
{
	int fc = (vcpu->run->s.regs.gprs[0] & 0xf0000000) >> 28;
	int sel1 = vcpu->run->s.regs.gprs[0] & 0xff;
	int sel2 = vcpu->run->s.regs.gprs[1] & 0xffff;
	u64 operand2;
	unsigned long mem;

	vcpu->stat.instruction_stsi++;
	VCPU_EVENT(vcpu, 4, "stsi: fc: %x sel1: %x sel2: %x", fc, sel1, sel2);

	operand2 = kvm_s390_get_base_disp_s(vcpu);

	if (operand2 & 0xfff && fc > 0)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	switch (fc) {
	case 0:
		vcpu->run->s.regs.gprs[0] = 3 << 28;
		vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
		return 0;
	case 1: /* same handling for 1 and 2 */
	case 2:
		mem = get_zeroed_page(GFP_KERNEL);
		if (!mem)
			goto out_fail;
		if (stsi((void *) mem, fc, sel1, sel2))
			goto out_mem;
		break;
	case 3:
		if (sel1 != 2 || sel2 != 2)
			goto out_fail;
		mem = get_zeroed_page(GFP_KERNEL);
		if (!mem)
			goto out_fail;
		handle_stsi_3_2_2(vcpu, (void *) mem);
		break;
	default:
		goto out_fail;
	}

	if (copy_to_guest_absolute(vcpu, operand2, (void *) mem, PAGE_SIZE)) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out_mem;
	}
	trace_kvm_s390_handle_stsi(vcpu, fc, sel1, sel2, operand2);
	free_page(mem);
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->run->s.regs.gprs[0] = 0;
	return 0;
out_mem:
	free_page(mem);
out_fail:
	/* condition code 3 */
	vcpu->arch.sie_block->gpsw.mask |= 3ul << 44;
	return 0;
}

static const intercept_handler_t b2_handlers[256] = {
	[0x02] = handle_stidp,
	[0x10] = handle_set_prefix,
	[0x11] = handle_store_prefix,
	[0x12] = handle_store_cpu_address,
	[0x29] = handle_skey,
	[0x2a] = handle_skey,
	[0x2b] = handle_skey,
	[0x30] = handle_io_inst,
	[0x31] = handle_io_inst,
	[0x32] = handle_io_inst,
	[0x33] = handle_io_inst,
	[0x34] = handle_io_inst,
	[0x35] = handle_io_inst,
	[0x36] = handle_io_inst,
	[0x37] = handle_io_inst,
	[0x38] = handle_io_inst,
	[0x39] = handle_io_inst,
	[0x3a] = handle_io_inst,
	[0x3b] = handle_io_inst,
	[0x3c] = handle_io_inst,
	[0x5f] = handle_io_inst,
	[0x74] = handle_io_inst,
	[0x76] = handle_io_inst,
	[0x7d] = handle_stsi,
	[0xb1] = handle_stfl,
	[0xb2] = handle_lpswe,
};

int kvm_s390_handle_b2(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	/*
	 * a lot of B2 instructions are priviledged. We first check for
	 * the privileged ones, that we can handle in the kernel. If the
	 * kernel can handle this instruction, we check for the problem
	 * state bit and (a) handle the instruction or (b) send a code 2
	 * program check.
	 * Anything else goes to userspace.*/
	handler = b2_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler) {
		if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
			return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);
		else
			return handler(vcpu);
	}
	return -EOPNOTSUPP;
}

static int handle_epsw(struct kvm_vcpu *vcpu)
{
	int reg1, reg2;

	reg1 = (vcpu->arch.sie_block->ipb & 0x00f00000) >> 24;
	reg2 = (vcpu->arch.sie_block->ipb & 0x000f0000) >> 16;

	/* This basically extracts the mask half of the psw. */
	vcpu->run->s.regs.gprs[reg1] &= 0xffffffff00000000;
	vcpu->run->s.regs.gprs[reg1] |= vcpu->arch.sie_block->gpsw.mask >> 32;
	if (reg2) {
		vcpu->run->s.regs.gprs[reg2] &= 0xffffffff00000000;
		vcpu->run->s.regs.gprs[reg2] |=
			vcpu->arch.sie_block->gpsw.mask & 0x00000000ffffffff;
	}
	return 0;
}

static const intercept_handler_t b9_handlers[256] = {
	[0x8d] = handle_epsw,
	[0x9c] = handle_io_inst,
};

int kvm_s390_handle_b9(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	/* This is handled just as for the B2 instructions. */
	handler = b9_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler) {
		if ((handler != handle_epsw) &&
		    (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE))
			return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);
		else
			return handler(vcpu);
	}
	return -EOPNOTSUPP;
}

static const intercept_handler_t eb_handlers[256] = {
	[0x8a] = handle_io_inst,
};

int kvm_s390_handle_priv_eb(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	/* All eb instructions that end up here are privileged. */
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);
	handler = eb_handlers[vcpu->arch.sie_block->ipb & 0xff];
	if (handler)
		return handler(vcpu);
	return -EOPNOTSUPP;
}

static int handle_tprot(struct kvm_vcpu *vcpu)
{
	u64 address1, address2;
	struct vm_area_struct *vma;
	unsigned long user_address;

	vcpu->stat.instruction_tprot++;

	kvm_s390_get_base_disp_sse(vcpu, &address1, &address2);

	/* we only handle the Linux memory detection case:
	 * access key == 0
	 * guest DAT == off
	 * everything else goes to userspace. */
	if (address2 & 0xf0)
		return -EOPNOTSUPP;
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		return -EOPNOTSUPP;

	down_read(&current->mm->mmap_sem);
	user_address = __gmap_translate(address1, vcpu->arch.gmap);
	if (IS_ERR_VALUE(user_address))
		goto out_inject;
	vma = find_vma(current->mm, user_address);
	if (!vma)
		goto out_inject;
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	if (!(vma->vm_flags & VM_WRITE) && (vma->vm_flags & VM_READ))
		vcpu->arch.sie_block->gpsw.mask |= (1ul << 44);
	if (!(vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_READ))
		vcpu->arch.sie_block->gpsw.mask |= (2ul << 44);

	up_read(&current->mm->mmap_sem);
	return 0;

out_inject:
	up_read(&current->mm->mmap_sem);
	return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
}

int kvm_s390_handle_e5(struct kvm_vcpu *vcpu)
{
	/* For e5xx... instructions we only handle TPROT */
	if ((vcpu->arch.sie_block->ipa & 0x00ff) == 0x01)
		return handle_tprot(vcpu);
	return -EOPNOTSUPP;
}

static int handle_sckpf(struct kvm_vcpu *vcpu)
{
	u32 value;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);

	if (vcpu->run->s.regs.gprs[0] & 0x00000000ffff0000)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_SPECIFICATION);

	value = vcpu->run->s.regs.gprs[0] & 0x000000000000ffff;
	vcpu->arch.sie_block->todpr = value;

	return 0;
}

static const intercept_handler_t x01_handlers[256] = {
	[0x07] = handle_sckpf,
};

int kvm_s390_handle_01(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	handler = x01_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler)
		return handler(vcpu);
	return -EOPNOTSUPP;
}
