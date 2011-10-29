/*
 * priv.c - handling privileged instructions
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
#include "gaccess.h"
#include "kvm-s390.h"

static int handle_set_prefix(struct kvm_vcpu *vcpu)
{
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u64 operand2;
	u32 address = 0;
	u8 tmp;

	vcpu->stat.instruction_spx++;

	operand2 = disp2;
	if (base2)
		operand2 += vcpu->arch.guest_gprs[base2];

	/* must be word boundary */
	if (operand2 & 3) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	/* get the value */
	if (get_guest_u32(vcpu, operand2, &address)) {
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

	vcpu->arch.sie_block->prefix = address;
	vcpu->arch.sie_block->ihcpu = 0xffff;

	VCPU_EVENT(vcpu, 5, "setting prefix to %x", address);
out:
	return 0;
}

static int handle_store_prefix(struct kvm_vcpu *vcpu)
{
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u64 operand2;
	u32 address;

	vcpu->stat.instruction_stpx++;
	operand2 = disp2;
	if (base2)
		operand2 += vcpu->arch.guest_gprs[base2];

	/* must be word boundary */
	if (operand2 & 3) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	address = vcpu->arch.sie_block->prefix;
	address = address & 0x7fffe000u;

	/* get the value */
	if (put_guest_u32(vcpu, operand2, address)) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	VCPU_EVENT(vcpu, 5, "storing prefix to %x", address);
out:
	return 0;
}

static int handle_store_cpu_address(struct kvm_vcpu *vcpu)
{
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u64 useraddr;
	int rc;

	vcpu->stat.instruction_stap++;
	useraddr = disp2;
	if (base2)
		useraddr += vcpu->arch.guest_gprs[base2];

	if (useraddr & 1) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	rc = put_guest_u16(vcpu, useraddr, vcpu->vcpu_id);
	if (rc == -EFAULT) {
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		goto out;
	}

	VCPU_EVENT(vcpu, 5, "storing cpu address to %llx", useraddr);
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

static int handle_stsch(struct kvm_vcpu *vcpu)
{
	vcpu->stat.instruction_stsch++;
	VCPU_EVENT(vcpu, 4, "%s", "store subchannel - CC3");
	/* condition code 3 */
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->arch.sie_block->gpsw.mask |= (3 & 3ul) << 44;
	return 0;
}

static int handle_chsc(struct kvm_vcpu *vcpu)
{
	vcpu->stat.instruction_chsc++;
	VCPU_EVENT(vcpu, 4, "%s", "channel subsystem call - CC3");
	/* condition code 3 */
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->arch.sie_block->gpsw.mask |= (3 & 3ul) << 44;
	return 0;
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
	if (rc == -EFAULT)
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	else
		VCPU_EVENT(vcpu, 5, "store facility list value %x",
			   facility_list);
	return 0;
}

static int handle_stidp(struct kvm_vcpu *vcpu)
{
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u64 operand2;
	int rc;

	vcpu->stat.instruction_stidp++;
	operand2 = disp2;
	if (base2)
		operand2 += vcpu->arch.guest_gprs[base2];

	if (operand2 & 7) {
		kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		goto out;
	}

	rc = put_guest_u64(vcpu, operand2, vcpu->arch.stidp_data);
	if (rc == -EFAULT) {
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
	if (stsi(mem, 3, 2, 2) == -ENOSYS)
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
	int fc = (vcpu->arch.guest_gprs[0] & 0xf0000000) >> 28;
	int sel1 = vcpu->arch.guest_gprs[0] & 0xff;
	int sel2 = vcpu->arch.guest_gprs[1] & 0xffff;
	int base2 = vcpu->arch.sie_block->ipb >> 28;
	int disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);
	u64 operand2;
	unsigned long mem;

	vcpu->stat.instruction_stsi++;
	VCPU_EVENT(vcpu, 4, "stsi: fc: %x sel1: %x sel2: %x", fc, sel1, sel2);

	operand2 = disp2;
	if (base2)
		operand2 += vcpu->arch.guest_gprs[base2];

	if (operand2 & 0xfff && fc > 0)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	switch (fc) {
	case 0:
		vcpu->arch.guest_gprs[0] = 3 << 28;
		vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
		return 0;
	case 1: /* same handling for 1 and 2 */
	case 2:
		mem = get_zeroed_page(GFP_KERNEL);
		if (!mem)
			goto out_fail;
		if (stsi((void *) mem, fc, sel1, sel2) == -ENOSYS)
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
	free_page(mem);
	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	vcpu->arch.guest_gprs[0] = 0;
	return 0;
out_mem:
	free_page(mem);
out_fail:
	/* condition code 3 */
	vcpu->arch.sie_block->gpsw.mask |= 3ul << 44;
	return 0;
}

static intercept_handler_t priv_handlers[256] = {
	[0x02] = handle_stidp,
	[0x10] = handle_set_prefix,
	[0x11] = handle_store_prefix,
	[0x12] = handle_store_cpu_address,
	[0x29] = handle_skey,
	[0x2a] = handle_skey,
	[0x2b] = handle_skey,
	[0x34] = handle_stsch,
	[0x5f] = handle_chsc,
	[0x7d] = handle_stsi,
	[0xb1] = handle_stfl,
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
	handler = priv_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler) {
		if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
			return kvm_s390_inject_program_int(vcpu,
						   PGM_PRIVILEGED_OPERATION);
		else
			return handler(vcpu);
	}
	return -EOPNOTSUPP;
}

static int handle_tprot(struct kvm_vcpu *vcpu)
{
	int base1 = (vcpu->arch.sie_block->ipb & 0xf0000000) >> 28;
	int disp1 = (vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16;
	int base2 = (vcpu->arch.sie_block->ipb & 0xf000) >> 12;
	int disp2 = vcpu->arch.sie_block->ipb & 0x0fff;
	u64 address1 = disp1 + base1 ? vcpu->arch.guest_gprs[base1] : 0;
	u64 address2 = disp2 + base2 ? vcpu->arch.guest_gprs[base2] : 0;
	struct vm_area_struct *vma;

	vcpu->stat.instruction_tprot++;

	/* we only handle the Linux memory detection case:
	 * access key == 0
	 * guest DAT == off
	 * everything else goes to userspace. */
	if (address2 & 0xf0)
		return -EOPNOTSUPP;
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		return -EOPNOTSUPP;


	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm,
			(unsigned long) __guestaddr_to_user(vcpu, address1));
	if (!vma) {
		up_read(&current->mm->mmap_sem);
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	}

	vcpu->arch.sie_block->gpsw.mask &= ~(3ul << 44);
	if (!(vma->vm_flags & VM_WRITE) && (vma->vm_flags & VM_READ))
		vcpu->arch.sie_block->gpsw.mask |= (1ul << 44);
	if (!(vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_READ))
		vcpu->arch.sie_block->gpsw.mask |= (2ul << 44);

	up_read(&current->mm->mmap_sem);
	return 0;
}

int kvm_s390_handle_e5(struct kvm_vcpu *vcpu)
{
	/* For e5xx... instructions we only handle TPROT */
	if ((vcpu->arch.sie_block->ipa & 0x00ff) == 0x01)
		return handle_tprot(vcpu);
	return -EOPNOTSUPP;
}

