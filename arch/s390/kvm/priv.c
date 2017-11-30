/*
 * handling privileged instructions
 *
 * Copyright IBM Corp. 2008, 2013
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
#include <linux/compat.h>
#include <linux/mm_types.h>

#include <asm/asm-offsets.h>
#include <asm/facility.h>
#include <asm/current.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/sysinfo.h>
#include <asm/pgtable.h>
#include <asm/page-states.h>
#include <asm/pgalloc.h>
#include <asm/gmap.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/compat.h>
#include <asm/sclp.h>
#include "gaccess.h"
#include "kvm-s390.h"
#include "trace.h"

static int handle_ri(struct kvm_vcpu *vcpu)
{
	if (test_kvm_facility(vcpu->kvm, 64)) {
		VCPU_EVENT(vcpu, 3, "%s", "ENABLE: RI (lazy)");
		vcpu->arch.sie_block->ecb3 |= ECB3_RI;
		kvm_s390_retry_instr(vcpu);
		return 0;
	} else
		return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);
}

int kvm_s390_handle_aa(struct kvm_vcpu *vcpu)
{
	if ((vcpu->arch.sie_block->ipa & 0xf) <= 4)
		return handle_ri(vcpu);
	else
		return -EOPNOTSUPP;
}

static int handle_gs(struct kvm_vcpu *vcpu)
{
	if (test_kvm_facility(vcpu->kvm, 133)) {
		VCPU_EVENT(vcpu, 3, "%s", "ENABLE: GS (lazy)");
		preempt_disable();
		__ctl_set_bit(2, 4);
		current->thread.gs_cb = (struct gs_cb *)&vcpu->run->s.regs.gscb;
		restore_gs_cb(current->thread.gs_cb);
		preempt_enable();
		vcpu->arch.sie_block->ecb |= ECB_GS;
		vcpu->arch.sie_block->ecd |= ECD_HOSTREGMGMT;
		vcpu->arch.gs_enabled = 1;
		kvm_s390_retry_instr(vcpu);
		return 0;
	} else
		return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);
}

int kvm_s390_handle_e3(struct kvm_vcpu *vcpu)
{
	int code = vcpu->arch.sie_block->ipb & 0xff;

	if (code == 0x49 || code == 0x4d)
		return handle_gs(vcpu);
	else
		return -EOPNOTSUPP;
}
/* Handle SCK (SET CLOCK) interception */
static int handle_set_clock(struct kvm_vcpu *vcpu)
{
	int rc;
	u8 ar;
	u64 op2, val;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	op2 = kvm_s390_get_base_disp_s(vcpu, &ar);
	if (op2 & 7)	/* Operand must be on a doubleword boundary */
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	rc = read_guest(vcpu, op2, ar, &val, sizeof(val));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	VCPU_EVENT(vcpu, 3, "SCK: setting guest TOD to 0x%llx", val);
	kvm_s390_set_tod_clock(vcpu->kvm, val);

	kvm_s390_set_psw_cc(vcpu, 0);
	return 0;
}

static int handle_set_prefix(struct kvm_vcpu *vcpu)
{
	u64 operand2;
	u32 address;
	int rc;
	u8 ar;

	vcpu->stat.instruction_spx++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	operand2 = kvm_s390_get_base_disp_s(vcpu, &ar);

	/* must be word boundary */
	if (operand2 & 3)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	/* get the value */
	rc = read_guest(vcpu, operand2, ar, &address, sizeof(address));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	address &= 0x7fffe000u;

	/*
	 * Make sure the new value is valid memory. We only need to check the
	 * first page, since address is 8k aligned and memory pieces are always
	 * at least 1MB aligned and have at least a size of 1MB.
	 */
	if (kvm_is_error_gpa(vcpu->kvm, address))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

	kvm_s390_set_prefix(vcpu, address);
	trace_kvm_s390_handle_prefix(vcpu, 1, address);
	return 0;
}

static int handle_store_prefix(struct kvm_vcpu *vcpu)
{
	u64 operand2;
	u32 address;
	int rc;
	u8 ar;

	vcpu->stat.instruction_stpx++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	operand2 = kvm_s390_get_base_disp_s(vcpu, &ar);

	/* must be word boundary */
	if (operand2 & 3)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	address = kvm_s390_get_prefix(vcpu);

	/* get the value */
	rc = write_guest(vcpu, operand2, ar, &address, sizeof(address));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	VCPU_EVENT(vcpu, 3, "STPX: storing prefix 0x%x into 0x%llx", address, operand2);
	trace_kvm_s390_handle_prefix(vcpu, 0, address);
	return 0;
}

static int handle_store_cpu_address(struct kvm_vcpu *vcpu)
{
	u16 vcpu_id = vcpu->vcpu_id;
	u64 ga;
	int rc;
	u8 ar;

	vcpu->stat.instruction_stap++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	ga = kvm_s390_get_base_disp_s(vcpu, &ar);

	if (ga & 1)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	rc = write_guest(vcpu, ga, ar, &vcpu_id, sizeof(vcpu_id));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	VCPU_EVENT(vcpu, 3, "STAP: storing cpu address (%u) to 0x%llx", vcpu_id, ga);
	trace_kvm_s390_handle_stap(vcpu, ga);
	return 0;
}

int kvm_s390_skey_check_enable(struct kvm_vcpu *vcpu)
{
	int rc = 0;
	struct kvm_s390_sie_block *sie_block = vcpu->arch.sie_block;

	trace_kvm_s390_skey_related_inst(vcpu);
	if (!(sie_block->ictl & (ICTL_ISKE | ICTL_SSKE | ICTL_RRBE)) &&
	    !(atomic_read(&sie_block->cpuflags) & CPUSTAT_KSS))
		return rc;

	rc = s390_enable_skey();
	VCPU_EVENT(vcpu, 3, "enabling storage keys for guest: %d", rc);
	if (!rc) {
		if (atomic_read(&sie_block->cpuflags) & CPUSTAT_KSS)
			atomic_andnot(CPUSTAT_KSS, &sie_block->cpuflags);
		else
			sie_block->ictl &= ~(ICTL_ISKE | ICTL_SSKE |
					     ICTL_RRBE);
	}
	return rc;
}

static int try_handle_skey(struct kvm_vcpu *vcpu)
{
	int rc;

	vcpu->stat.instruction_storage_key++;
	rc = kvm_s390_skey_check_enable(vcpu);
	if (rc)
		return rc;
	if (sclp.has_skey) {
		/* with storage-key facility, SIE interprets it for us */
		kvm_s390_retry_instr(vcpu);
		VCPU_EVENT(vcpu, 4, "%s", "retrying storage key operation");
		return -EAGAIN;
	}
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);
	return 0;
}

static int handle_iske(struct kvm_vcpu *vcpu)
{
	unsigned long addr;
	unsigned char key;
	int reg1, reg2;
	int rc;

	rc = try_handle_skey(vcpu);
	if (rc)
		return rc != -EAGAIN ? rc : 0;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	addr = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	addr = kvm_s390_logical_to_effective(vcpu, addr);
	addr = kvm_s390_real_to_abs(vcpu, addr);
	addr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(addr));
	if (kvm_is_error_hva(addr))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

	down_read(&current->mm->mmap_sem);
	rc = get_guest_storage_key(current->mm, addr, &key);
	up_read(&current->mm->mmap_sem);
	if (rc)
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	vcpu->run->s.regs.gprs[reg1] &= ~0xff;
	vcpu->run->s.regs.gprs[reg1] |= key;
	return 0;
}

static int handle_rrbe(struct kvm_vcpu *vcpu)
{
	unsigned long addr;
	int reg1, reg2;
	int rc;

	rc = try_handle_skey(vcpu);
	if (rc)
		return rc != -EAGAIN ? rc : 0;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	addr = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	addr = kvm_s390_logical_to_effective(vcpu, addr);
	addr = kvm_s390_real_to_abs(vcpu, addr);
	addr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(addr));
	if (kvm_is_error_hva(addr))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

	down_read(&current->mm->mmap_sem);
	rc = reset_guest_reference_bit(current->mm, addr);
	up_read(&current->mm->mmap_sem);
	if (rc < 0)
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

	kvm_s390_set_psw_cc(vcpu, rc);
	return 0;
}

#define SSKE_NQ 0x8
#define SSKE_MR 0x4
#define SSKE_MC 0x2
#define SSKE_MB 0x1
static int handle_sske(struct kvm_vcpu *vcpu)
{
	unsigned char m3 = vcpu->arch.sie_block->ipb >> 28;
	unsigned long start, end;
	unsigned char key, oldkey;
	int reg1, reg2;
	int rc;

	rc = try_handle_skey(vcpu);
	if (rc)
		return rc != -EAGAIN ? rc : 0;

	if (!test_kvm_facility(vcpu->kvm, 8))
		m3 &= ~SSKE_MB;
	if (!test_kvm_facility(vcpu->kvm, 10))
		m3 &= ~(SSKE_MC | SSKE_MR);
	if (!test_kvm_facility(vcpu->kvm, 14))
		m3 &= ~SSKE_NQ;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	key = vcpu->run->s.regs.gprs[reg1] & 0xfe;
	start = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	start = kvm_s390_logical_to_effective(vcpu, start);
	if (m3 & SSKE_MB) {
		/* start already designates an absolute address */
		end = (start + _SEGMENT_SIZE) & ~(_SEGMENT_SIZE - 1);
	} else {
		start = kvm_s390_real_to_abs(vcpu, start);
		end = start + PAGE_SIZE;
	}

	while (start != end) {
		unsigned long addr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(start));

		if (kvm_is_error_hva(addr))
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

		down_read(&current->mm->mmap_sem);
		rc = cond_set_guest_storage_key(current->mm, addr, key, &oldkey,
						m3 & SSKE_NQ, m3 & SSKE_MR,
						m3 & SSKE_MC);
		up_read(&current->mm->mmap_sem);
		if (rc < 0)
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		start += PAGE_SIZE;
	}

	if (m3 & (SSKE_MC | SSKE_MR)) {
		if (m3 & SSKE_MB) {
			/* skey in reg1 is unpredictable */
			kvm_s390_set_psw_cc(vcpu, 3);
		} else {
			kvm_s390_set_psw_cc(vcpu, rc);
			vcpu->run->s.regs.gprs[reg1] &= ~0xff00UL;
			vcpu->run->s.regs.gprs[reg1] |= (u64) oldkey << 8;
		}
	}
	if (m3 & SSKE_MB) {
		if (psw_bits(vcpu->arch.sie_block->gpsw).eaba == PSW_BITS_AMODE_64BIT)
			vcpu->run->s.regs.gprs[reg2] &= ~PAGE_MASK;
		else
			vcpu->run->s.regs.gprs[reg2] &= ~0xfffff000UL;
		end = kvm_s390_logical_to_effective(vcpu, end);
		vcpu->run->s.regs.gprs[reg2] |= end;
	}
	return 0;
}

static int handle_ipte_interlock(struct kvm_vcpu *vcpu)
{
	vcpu->stat.instruction_ipte_interlock++;
	if (psw_bits(vcpu->arch.sie_block->gpsw).pstate)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);
	wait_event(vcpu->kvm->arch.ipte_wq, !ipte_lock_held(vcpu));
	kvm_s390_retry_instr(vcpu);
	VCPU_EVENT(vcpu, 4, "%s", "retrying ipte interlock operation");
	return 0;
}

static int handle_test_block(struct kvm_vcpu *vcpu)
{
	gpa_t addr;
	int reg2;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	kvm_s390_get_regs_rre(vcpu, NULL, &reg2);
	addr = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	addr = kvm_s390_logical_to_effective(vcpu, addr);
	if (kvm_s390_check_low_addr_prot_real(vcpu, addr))
		return kvm_s390_inject_prog_irq(vcpu, &vcpu->arch.pgm);
	addr = kvm_s390_real_to_abs(vcpu, addr);

	if (kvm_is_error_gpa(vcpu->kvm, addr))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	/*
	 * We don't expect errors on modern systems, and do not care
	 * about storage keys (yet), so let's just clear the page.
	 */
	if (kvm_clear_guest(vcpu->kvm, addr, PAGE_SIZE))
		return -EFAULT;
	kvm_s390_set_psw_cc(vcpu, 0);
	vcpu->run->s.regs.gprs[0] = 0;
	return 0;
}

static int handle_tpi(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_interrupt_info *inti;
	unsigned long len;
	u32 tpi_data[3];
	int rc;
	u64 addr;
	u8 ar;

	addr = kvm_s390_get_base_disp_s(vcpu, &ar);
	if (addr & 3)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	inti = kvm_s390_get_io_int(vcpu->kvm, vcpu->arch.sie_block->gcr[6], 0);
	if (!inti) {
		kvm_s390_set_psw_cc(vcpu, 0);
		return 0;
	}

	tpi_data[0] = inti->io.subchannel_id << 16 | inti->io.subchannel_nr;
	tpi_data[1] = inti->io.io_int_parm;
	tpi_data[2] = inti->io.io_int_word;
	if (addr) {
		/*
		 * Store the two-word I/O interruption code into the
		 * provided area.
		 */
		len = sizeof(tpi_data) - 4;
		rc = write_guest(vcpu, addr, ar, &tpi_data, len);
		if (rc) {
			rc = kvm_s390_inject_prog_cond(vcpu, rc);
			goto reinject_interrupt;
		}
	} else {
		/*
		 * Store the three-word I/O interruption code into
		 * the appropriate lowcore area.
		 */
		len = sizeof(tpi_data);
		if (write_guest_lc(vcpu, __LC_SUBCHANNEL_ID, &tpi_data, len)) {
			/* failed writes to the low core are not recoverable */
			rc = -EFAULT;
			goto reinject_interrupt;
		}
	}

	/* irq was successfully handed to the guest */
	kfree(inti);
	kvm_s390_set_psw_cc(vcpu, 1);
	return 0;
reinject_interrupt:
	/*
	 * If we encounter a problem storing the interruption code, the
	 * instruction is suppressed from the guest's view: reinject the
	 * interrupt.
	 */
	if (kvm_s390_reinject_io_int(vcpu->kvm, inti)) {
		kfree(inti);
		rc = -EFAULT;
	}
	/* don't set the cc, a pgm irq was injected or we drop to user space */
	return rc ? -EFAULT : 0;
}

static int handle_tsch(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_interrupt_info *inti = NULL;
	const u64 isc_mask = 0xffUL << 24; /* all iscs set */

	/* a valid schid has at least one bit set */
	if (vcpu->run->s.regs.gprs[1])
		inti = kvm_s390_get_io_int(vcpu->kvm, isc_mask,
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

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

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
		 * Set condition code 3 to stop the guest from issuing channel
		 * I/O instructions.
		 */
		kvm_s390_set_psw_cc(vcpu, 3);
		return 0;
	}
}

static int handle_stfl(struct kvm_vcpu *vcpu)
{
	int rc;
	unsigned int fac;

	vcpu->stat.instruction_stfl++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	/*
	 * We need to shift the lower 32 facility bits (bit 0-31) from a u64
	 * into a u32 memory representation. They will remain bits 0-31.
	 */
	fac = *vcpu->kvm->arch.model.fac_list >> 32;
	rc = write_guest_lc(vcpu, offsetof(struct lowcore, stfl_fac_list),
			    &fac, sizeof(fac));
	if (rc)
		return rc;
	VCPU_EVENT(vcpu, 3, "STFL: store facility list 0x%x", fac);
	trace_kvm_s390_handle_stfl(vcpu, fac);
	return 0;
}

#define PSW_MASK_ADDR_MODE (PSW_MASK_EA | PSW_MASK_BA)
#define PSW_MASK_UNASSIGNED 0xb80800fe7fffffffUL
#define PSW_ADDR_24 0x0000000000ffffffUL
#define PSW_ADDR_31 0x000000007fffffffUL

int is_valid_psw(psw_t *psw)
{
	if (psw->mask & PSW_MASK_UNASSIGNED)
		return 0;
	if ((psw->mask & PSW_MASK_ADDR_MODE) == PSW_MASK_BA) {
		if (psw->addr & ~PSW_ADDR_31)
			return 0;
	}
	if (!(psw->mask & PSW_MASK_ADDR_MODE) && (psw->addr & ~PSW_ADDR_24))
		return 0;
	if ((psw->mask & PSW_MASK_ADDR_MODE) ==  PSW_MASK_EA)
		return 0;
	if (psw->addr & 1)
		return 0;
	return 1;
}

int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu)
{
	psw_t *gpsw = &vcpu->arch.sie_block->gpsw;
	psw_compat_t new_psw;
	u64 addr;
	int rc;
	u8 ar;

	if (gpsw->mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	addr = kvm_s390_get_base_disp_s(vcpu, &ar);
	if (addr & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	rc = read_guest(vcpu, addr, ar, &new_psw, sizeof(new_psw));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	if (!(new_psw.mask & PSW32_MASK_BASE))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	gpsw->mask = (new_psw.mask & ~PSW32_MASK_BASE) << 32;
	gpsw->mask |= new_psw.addr & PSW32_ADDR_AMODE;
	gpsw->addr = new_psw.addr & ~PSW32_ADDR_AMODE;
	if (!is_valid_psw(gpsw))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	return 0;
}

static int handle_lpswe(struct kvm_vcpu *vcpu)
{
	psw_t new_psw;
	u64 addr;
	int rc;
	u8 ar;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	addr = kvm_s390_get_base_disp_s(vcpu, &ar);
	if (addr & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	rc = read_guest(vcpu, addr, ar, &new_psw, sizeof(new_psw));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	vcpu->arch.sie_block->gpsw = new_psw;
	if (!is_valid_psw(&vcpu->arch.sie_block->gpsw))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	return 0;
}

static int handle_stidp(struct kvm_vcpu *vcpu)
{
	u64 stidp_data = vcpu->kvm->arch.model.cpuid;
	u64 operand2;
	int rc;
	u8 ar;

	vcpu->stat.instruction_stidp++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	operand2 = kvm_s390_get_base_disp_s(vcpu, &ar);

	if (operand2 & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	rc = write_guest(vcpu, operand2, ar, &stidp_data, sizeof(stidp_data));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	VCPU_EVENT(vcpu, 3, "STIDP: store cpu id 0x%llx", stidp_data);
	return 0;
}

static void handle_stsi_3_2_2(struct kvm_vcpu *vcpu, struct sysinfo_3_2_2 *mem)
{
	int cpus = 0;
	int n;

	cpus = atomic_read(&vcpu->kvm->online_vcpus);

	/* deal with other level 3 hypervisors */
	if (stsi(mem, 3, 2, 2))
		mem->count = 0;
	if (mem->count < 8)
		mem->count++;
	for (n = mem->count - 1; n > 0 ; n--)
		memcpy(&mem->vm[n], &mem->vm[n - 1], sizeof(mem->vm[0]));

	memset(&mem->vm[0], 0, sizeof(mem->vm[0]));
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

static void insert_stsi_usr_data(struct kvm_vcpu *vcpu, u64 addr, u8 ar,
				 u8 fc, u8 sel1, u16 sel2)
{
	vcpu->run->exit_reason = KVM_EXIT_S390_STSI;
	vcpu->run->s390_stsi.addr = addr;
	vcpu->run->s390_stsi.ar = ar;
	vcpu->run->s390_stsi.fc = fc;
	vcpu->run->s390_stsi.sel1 = sel1;
	vcpu->run->s390_stsi.sel2 = sel2;
}

static int handle_stsi(struct kvm_vcpu *vcpu)
{
	int fc = (vcpu->run->s.regs.gprs[0] & 0xf0000000) >> 28;
	int sel1 = vcpu->run->s.regs.gprs[0] & 0xff;
	int sel2 = vcpu->run->s.regs.gprs[1] & 0xffff;
	unsigned long mem = 0;
	u64 operand2;
	int rc = 0;
	u8 ar;

	vcpu->stat.instruction_stsi++;
	VCPU_EVENT(vcpu, 3, "STSI: fc: %u sel1: %u sel2: %u", fc, sel1, sel2);

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	if (fc > 3) {
		kvm_s390_set_psw_cc(vcpu, 3);
		return 0;
	}

	if (vcpu->run->s.regs.gprs[0] & 0x0fffff00
	    || vcpu->run->s.regs.gprs[1] & 0xffff0000)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	if (fc == 0) {
		vcpu->run->s.regs.gprs[0] = 3 << 28;
		kvm_s390_set_psw_cc(vcpu, 0);
		return 0;
	}

	operand2 = kvm_s390_get_base_disp_s(vcpu, &ar);

	if (operand2 & 0xfff)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	switch (fc) {
	case 1: /* same handling for 1 and 2 */
	case 2:
		mem = get_zeroed_page(GFP_KERNEL);
		if (!mem)
			goto out_no_data;
		if (stsi((void *) mem, fc, sel1, sel2))
			goto out_no_data;
		break;
	case 3:
		if (sel1 != 2 || sel2 != 2)
			goto out_no_data;
		mem = get_zeroed_page(GFP_KERNEL);
		if (!mem)
			goto out_no_data;
		handle_stsi_3_2_2(vcpu, (void *) mem);
		break;
	}

	rc = write_guest(vcpu, operand2, ar, (void *)mem, PAGE_SIZE);
	if (rc) {
		rc = kvm_s390_inject_prog_cond(vcpu, rc);
		goto out;
	}
	if (vcpu->kvm->arch.user_stsi) {
		insert_stsi_usr_data(vcpu, operand2, ar, fc, sel1, sel2);
		rc = -EREMOTE;
	}
	trace_kvm_s390_handle_stsi(vcpu, fc, sel1, sel2, operand2);
	free_page(mem);
	kvm_s390_set_psw_cc(vcpu, 0);
	vcpu->run->s.regs.gprs[0] = 0;
	return rc;
out_no_data:
	kvm_s390_set_psw_cc(vcpu, 3);
out:
	free_page(mem);
	return rc;
}

static const intercept_handler_t b2_handlers[256] = {
	[0x02] = handle_stidp,
	[0x04] = handle_set_clock,
	[0x10] = handle_set_prefix,
	[0x11] = handle_store_prefix,
	[0x12] = handle_store_cpu_address,
	[0x14] = kvm_s390_handle_vsie,
	[0x21] = handle_ipte_interlock,
	[0x29] = handle_iske,
	[0x2a] = handle_rrbe,
	[0x2b] = handle_sske,
	[0x2c] = handle_test_block,
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
	[0x50] = handle_ipte_interlock,
	[0x56] = handle_sthyi,
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
	 * A lot of B2 instructions are priviledged. Here we check for
	 * the privileged ones, that we can handle in the kernel.
	 * Anything else goes to userspace.
	 */
	handler = b2_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler)
		return handler(vcpu);

	return -EOPNOTSUPP;
}

static int handle_epsw(struct kvm_vcpu *vcpu)
{
	int reg1, reg2;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	/* This basically extracts the mask half of the psw. */
	vcpu->run->s.regs.gprs[reg1] &= 0xffffffff00000000UL;
	vcpu->run->s.regs.gprs[reg1] |= vcpu->arch.sie_block->gpsw.mask >> 32;
	if (reg2) {
		vcpu->run->s.regs.gprs[reg2] &= 0xffffffff00000000UL;
		vcpu->run->s.regs.gprs[reg2] |=
			vcpu->arch.sie_block->gpsw.mask & 0x00000000ffffffffUL;
	}
	return 0;
}

#define PFMF_RESERVED   0xfffc0101UL
#define PFMF_SK         0x00020000UL
#define PFMF_CF         0x00010000UL
#define PFMF_UI         0x00008000UL
#define PFMF_FSC        0x00007000UL
#define PFMF_NQ         0x00000800UL
#define PFMF_MR         0x00000400UL
#define PFMF_MC         0x00000200UL
#define PFMF_KEY        0x000000feUL

static int handle_pfmf(struct kvm_vcpu *vcpu)
{
	bool mr = false, mc = false, nq;
	int reg1, reg2;
	unsigned long start, end;
	unsigned char key;

	vcpu->stat.instruction_pfmf++;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	if (!test_kvm_facility(vcpu->kvm, 8))
		return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	if (vcpu->run->s.regs.gprs[reg1] & PFMF_RESERVED)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	/* Only provide non-quiescing support if enabled for the guest */
	if (vcpu->run->s.regs.gprs[reg1] & PFMF_NQ &&
	    !test_kvm_facility(vcpu->kvm, 14))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	/* Only provide conditional-SSKE support if enabled for the guest */
	if (vcpu->run->s.regs.gprs[reg1] & PFMF_SK &&
	    test_kvm_facility(vcpu->kvm, 10)) {
		mr = vcpu->run->s.regs.gprs[reg1] & PFMF_MR;
		mc = vcpu->run->s.regs.gprs[reg1] & PFMF_MC;
	}

	nq = vcpu->run->s.regs.gprs[reg1] & PFMF_NQ;
	key = vcpu->run->s.regs.gprs[reg1] & PFMF_KEY;
	start = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	start = kvm_s390_logical_to_effective(vcpu, start);

	if (vcpu->run->s.regs.gprs[reg1] & PFMF_CF) {
		if (kvm_s390_check_low_addr_prot_real(vcpu, start))
			return kvm_s390_inject_prog_irq(vcpu, &vcpu->arch.pgm);
	}

	switch (vcpu->run->s.regs.gprs[reg1] & PFMF_FSC) {
	case 0x00000000:
		/* only 4k frames specify a real address */
		start = kvm_s390_real_to_abs(vcpu, start);
		end = (start + PAGE_SIZE) & ~(PAGE_SIZE - 1);
		break;
	case 0x00001000:
		end = (start + _SEGMENT_SIZE) & ~(_SEGMENT_SIZE - 1);
		break;
	case 0x00002000:
		/* only support 2G frame size if EDAT2 is available and we are
		   not in 24-bit addressing mode */
		if (!test_kvm_facility(vcpu->kvm, 78) ||
		    psw_bits(vcpu->arch.sie_block->gpsw).eaba == PSW_BITS_AMODE_24BIT)
			return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
		end = (start + _REGION3_SIZE) & ~(_REGION3_SIZE - 1);
		break;
	default:
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	}

	while (start != end) {
		unsigned long useraddr;

		/* Translate guest address to host address */
		useraddr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(start));
		if (kvm_is_error_hva(useraddr))
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

		if (vcpu->run->s.regs.gprs[reg1] & PFMF_CF) {
			if (clear_user((void __user *)useraddr, PAGE_SIZE))
				return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		}

		if (vcpu->run->s.regs.gprs[reg1] & PFMF_SK) {
			int rc = kvm_s390_skey_check_enable(vcpu);

			if (rc)
				return rc;
			down_read(&current->mm->mmap_sem);
			rc = cond_set_guest_storage_key(current->mm, useraddr,
							key, NULL, nq, mr, mc);
			up_read(&current->mm->mmap_sem);
			if (rc < 0)
				return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		}

		start += PAGE_SIZE;
	}
	if (vcpu->run->s.regs.gprs[reg1] & PFMF_FSC) {
		if (psw_bits(vcpu->arch.sie_block->gpsw).eaba == PSW_BITS_AMODE_64BIT) {
			vcpu->run->s.regs.gprs[reg2] = end;
		} else {
			vcpu->run->s.regs.gprs[reg2] &= ~0xffffffffUL;
			end = kvm_s390_logical_to_effective(vcpu, end);
			vcpu->run->s.regs.gprs[reg2] |= end;
		}
	}
	return 0;
}

static inline int do_essa(struct kvm_vcpu *vcpu, const int orc)
{
	struct kvm_s390_migration_state *ms = vcpu->kvm->arch.migration_state;
	int r1, r2, nappended, entries;
	unsigned long gfn, hva, res, pgstev, ptev;
	unsigned long *cbrlo;

	/*
	 * We don't need to set SD.FPF.SK to 1 here, because if we have a
	 * machine check here we either handle it or crash
	 */

	kvm_s390_get_regs_rre(vcpu, &r1, &r2);
	gfn = vcpu->run->s.regs.gprs[r2] >> PAGE_SHIFT;
	hva = gfn_to_hva(vcpu->kvm, gfn);
	entries = (vcpu->arch.sie_block->cbrlo & ~PAGE_MASK) >> 3;

	if (kvm_is_error_hva(hva))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

	nappended = pgste_perform_essa(vcpu->kvm->mm, hva, orc, &ptev, &pgstev);
	if (nappended < 0) {
		res = orc ? 0x10 : 0;
		vcpu->run->s.regs.gprs[r1] = res; /* Exception Indication */
		return 0;
	}
	res = (pgstev & _PGSTE_GPS_USAGE_MASK) >> 22;
	/*
	 * Set the block-content state part of the result. 0 means resident, so
	 * nothing to do if the page is valid. 2 is for preserved pages
	 * (non-present and non-zero), and 3 for zero pages (non-present and
	 * zero).
	 */
	if (ptev & _PAGE_INVALID) {
		res |= 2;
		if (pgstev & _PGSTE_GPS_ZERO)
			res |= 1;
	}
	if (pgstev & _PGSTE_GPS_NODAT)
		res |= 0x20;
	vcpu->run->s.regs.gprs[r1] = res;
	/*
	 * It is possible that all the normal 511 slots were full, in which case
	 * we will now write in the 512th slot, which is reserved for host use.
	 * In both cases we let the normal essa handling code process all the
	 * slots, including the reserved one, if needed.
	 */
	if (nappended > 0) {
		cbrlo = phys_to_virt(vcpu->arch.sie_block->cbrlo & PAGE_MASK);
		cbrlo[entries] = gfn << PAGE_SHIFT;
	}

	if (orc) {
		/* increment only if we are really flipping the bit to 1 */
		if (!test_and_set_bit(gfn, ms->pgste_bitmap))
			atomic64_inc(&ms->dirty_pages);
	}

	return nappended;
}

static int handle_essa(struct kvm_vcpu *vcpu)
{
	/* entries expected to be 1FF */
	int entries = (vcpu->arch.sie_block->cbrlo & ~PAGE_MASK) >> 3;
	unsigned long *cbrlo;
	struct gmap *gmap;
	int i, orc;

	VCPU_EVENT(vcpu, 4, "ESSA: release %d pages", entries);
	gmap = vcpu->arch.gmap;
	vcpu->stat.instruction_essa++;
	if (!vcpu->kvm->arch.use_cmma)
		return kvm_s390_inject_program_int(vcpu, PGM_OPERATION);

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);
	/* Check for invalid operation request code */
	orc = (vcpu->arch.sie_block->ipb & 0xf0000000) >> 28;
	/* ORCs 0-6 are always valid */
	if (orc > (test_kvm_facility(vcpu->kvm, 147) ? ESSA_SET_STABLE_NODAT
						: ESSA_SET_STABLE_IF_RESIDENT))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	if (likely(!vcpu->kvm->arch.migration_state)) {
		/*
		 * CMMA is enabled in the KVM settings, but is disabled in
		 * the SIE block and in the mm_context, and we are not doing
		 * a migration. Enable CMMA in the mm_context.
		 * Since we need to take a write lock to write to the context
		 * to avoid races with storage keys handling, we check if the
		 * value really needs to be written to; if the value is
		 * already correct, we do nothing and avoid the lock.
		 */
		if (vcpu->kvm->mm->context.use_cmma == 0) {
			down_write(&vcpu->kvm->mm->mmap_sem);
			vcpu->kvm->mm->context.use_cmma = 1;
			up_write(&vcpu->kvm->mm->mmap_sem);
		}
		/*
		 * If we are here, we are supposed to have CMMA enabled in
		 * the SIE block. Enabling CMMA works on a per-CPU basis,
		 * while the context use_cmma flag is per process.
		 * It's possible that the context flag is enabled and the
		 * SIE flag is not, so we set the flag always; if it was
		 * already set, nothing changes, otherwise we enable it
		 * on this CPU too.
		 */
		vcpu->arch.sie_block->ecb2 |= ECB2_CMMA;
		/* Retry the ESSA instruction */
		kvm_s390_retry_instr(vcpu);
	} else {
		/* Account for the possible extra cbrl entry */
		i = do_essa(vcpu, orc);
		if (i < 0)
			return i;
		entries += i;
	}
	vcpu->arch.sie_block->cbrlo &= PAGE_MASK;	/* reset nceo */
	cbrlo = phys_to_virt(vcpu->arch.sie_block->cbrlo);
	down_read(&gmap->mm->mmap_sem);
	for (i = 0; i < entries; ++i)
		__gmap_zap(gmap, cbrlo[i]);
	up_read(&gmap->mm->mmap_sem);
	return 0;
}

static const intercept_handler_t b9_handlers[256] = {
	[0x8a] = handle_ipte_interlock,
	[0x8d] = handle_epsw,
	[0x8e] = handle_ipte_interlock,
	[0x8f] = handle_ipte_interlock,
	[0xab] = handle_essa,
	[0xaf] = handle_pfmf,
};

int kvm_s390_handle_b9(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	/* This is handled just as for the B2 instructions. */
	handler = b9_handlers[vcpu->arch.sie_block->ipa & 0x00ff];
	if (handler)
		return handler(vcpu);

	return -EOPNOTSUPP;
}

int kvm_s390_handle_lctl(struct kvm_vcpu *vcpu)
{
	int reg1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int reg3 = vcpu->arch.sie_block->ipa & 0x000f;
	int reg, rc, nr_regs;
	u32 ctl_array[16];
	u64 ga;
	u8 ar;

	vcpu->stat.instruction_lctl++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	ga = kvm_s390_get_base_disp_rs(vcpu, &ar);

	if (ga & 3)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 4, "LCTL: r1:%d, r3:%d, addr: 0x%llx", reg1, reg3, ga);
	trace_kvm_s390_handle_lctl(vcpu, 0, reg1, reg3, ga);

	nr_regs = ((reg3 - reg1) & 0xf) + 1;
	rc = read_guest(vcpu, ga, ar, ctl_array, nr_regs * sizeof(u32));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	reg = reg1;
	nr_regs = 0;
	do {
		vcpu->arch.sie_block->gcr[reg] &= 0xffffffff00000000ul;
		vcpu->arch.sie_block->gcr[reg] |= ctl_array[nr_regs++];
		if (reg == reg3)
			break;
		reg = (reg + 1) % 16;
	} while (1);
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	return 0;
}

int kvm_s390_handle_stctl(struct kvm_vcpu *vcpu)
{
	int reg1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int reg3 = vcpu->arch.sie_block->ipa & 0x000f;
	int reg, rc, nr_regs;
	u32 ctl_array[16];
	u64 ga;
	u8 ar;

	vcpu->stat.instruction_stctl++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	ga = kvm_s390_get_base_disp_rs(vcpu, &ar);

	if (ga & 3)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 4, "STCTL r1:%d, r3:%d, addr: 0x%llx", reg1, reg3, ga);
	trace_kvm_s390_handle_stctl(vcpu, 0, reg1, reg3, ga);

	reg = reg1;
	nr_regs = 0;
	do {
		ctl_array[nr_regs++] = vcpu->arch.sie_block->gcr[reg];
		if (reg == reg3)
			break;
		reg = (reg + 1) % 16;
	} while (1);
	rc = write_guest(vcpu, ga, ar, ctl_array, nr_regs * sizeof(u32));
	return rc ? kvm_s390_inject_prog_cond(vcpu, rc) : 0;
}

static int handle_lctlg(struct kvm_vcpu *vcpu)
{
	int reg1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int reg3 = vcpu->arch.sie_block->ipa & 0x000f;
	int reg, rc, nr_regs;
	u64 ctl_array[16];
	u64 ga;
	u8 ar;

	vcpu->stat.instruction_lctlg++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	ga = kvm_s390_get_base_disp_rsy(vcpu, &ar);

	if (ga & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 4, "LCTLG: r1:%d, r3:%d, addr: 0x%llx", reg1, reg3, ga);
	trace_kvm_s390_handle_lctl(vcpu, 1, reg1, reg3, ga);

	nr_regs = ((reg3 - reg1) & 0xf) + 1;
	rc = read_guest(vcpu, ga, ar, ctl_array, nr_regs * sizeof(u64));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	reg = reg1;
	nr_regs = 0;
	do {
		vcpu->arch.sie_block->gcr[reg] = ctl_array[nr_regs++];
		if (reg == reg3)
			break;
		reg = (reg + 1) % 16;
	} while (1);
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	return 0;
}

static int handle_stctg(struct kvm_vcpu *vcpu)
{
	int reg1 = (vcpu->arch.sie_block->ipa & 0x00f0) >> 4;
	int reg3 = vcpu->arch.sie_block->ipa & 0x000f;
	int reg, rc, nr_regs;
	u64 ctl_array[16];
	u64 ga;
	u8 ar;

	vcpu->stat.instruction_stctg++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	ga = kvm_s390_get_base_disp_rsy(vcpu, &ar);

	if (ga & 7)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	VCPU_EVENT(vcpu, 4, "STCTG r1:%d, r3:%d, addr: 0x%llx", reg1, reg3, ga);
	trace_kvm_s390_handle_stctl(vcpu, 1, reg1, reg3, ga);

	reg = reg1;
	nr_regs = 0;
	do {
		ctl_array[nr_regs++] = vcpu->arch.sie_block->gcr[reg];
		if (reg == reg3)
			break;
		reg = (reg + 1) % 16;
	} while (1);
	rc = write_guest(vcpu, ga, ar, ctl_array, nr_regs * sizeof(u64));
	return rc ? kvm_s390_inject_prog_cond(vcpu, rc) : 0;
}

static const intercept_handler_t eb_handlers[256] = {
	[0x2f] = handle_lctlg,
	[0x25] = handle_stctg,
	[0x60] = handle_ri,
	[0x61] = handle_ri,
	[0x62] = handle_ri,
};

int kvm_s390_handle_eb(struct kvm_vcpu *vcpu)
{
	intercept_handler_t handler;

	handler = eb_handlers[vcpu->arch.sie_block->ipb & 0xff];
	if (handler)
		return handler(vcpu);
	return -EOPNOTSUPP;
}

static int handle_tprot(struct kvm_vcpu *vcpu)
{
	u64 address1, address2;
	unsigned long hva, gpa;
	int ret = 0, cc = 0;
	bool writable;
	u8 ar;

	vcpu->stat.instruction_tprot++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	kvm_s390_get_base_disp_sse(vcpu, &address1, &address2, &ar, NULL);

	/* we only handle the Linux memory detection case:
	 * access key == 0
	 * everything else goes to userspace. */
	if (address2 & 0xf0)
		return -EOPNOTSUPP;
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		ipte_lock(vcpu);
	ret = guest_translate_address(vcpu, address1, ar, &gpa, GACC_STORE);
	if (ret == PGM_PROTECTION) {
		/* Write protected? Try again with read-only... */
		cc = 1;
		ret = guest_translate_address(vcpu, address1, ar, &gpa,
					      GACC_FETCH);
	}
	if (ret) {
		if (ret == PGM_ADDRESSING || ret == PGM_TRANSLATION_SPEC) {
			ret = kvm_s390_inject_program_int(vcpu, ret);
		} else if (ret > 0) {
			/* Translation not available */
			kvm_s390_set_psw_cc(vcpu, 3);
			ret = 0;
		}
		goto out_unlock;
	}

	hva = gfn_to_hva_prot(vcpu->kvm, gpa_to_gfn(gpa), &writable);
	if (kvm_is_error_hva(hva)) {
		ret = kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	} else {
		if (!writable)
			cc = 1;		/* Write not permitted ==> read-only */
		kvm_s390_set_psw_cc(vcpu, cc);
		/* Note: CC2 only occurs for storage keys (not supported yet) */
	}
out_unlock:
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		ipte_unlock(vcpu);
	return ret;
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
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	if (vcpu->run->s.regs.gprs[0] & 0x00000000ffff0000)
		return kvm_s390_inject_program_int(vcpu,
						   PGM_SPECIFICATION);

	value = vcpu->run->s.regs.gprs[0] & 0x000000000000ffff;
	vcpu->arch.sie_block->todpr = value;

	return 0;
}

static int handle_ptff(struct kvm_vcpu *vcpu)
{
	/* we don't emulate any control instructions yet */
	kvm_s390_set_psw_cc(vcpu, 3);
	return 0;
}

static const intercept_handler_t x01_handlers[256] = {
	[0x04] = handle_ptff,
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
