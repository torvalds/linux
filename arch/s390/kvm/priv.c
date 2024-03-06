// SPDX-License-Identifier: GPL-2.0
/*
 * handling privileged instructions
 *
 * Copyright IBM Corp. 2008, 2020
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */

#include <linux/kvm.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <linux/io.h>
#include <asm/asm-offsets.h>
#include <asm/facility.h>
#include <asm/current.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/sysinfo.h>
#include <asm/page-states.h>
#include <asm/gmap.h>
#include <asm/ptrace.h>
#include <asm/sclp.h>
#include <asm/ap.h>
#include "gaccess.h"
#include "kvm-s390.h"
#include "trace.h"

static int handle_ri(struct kvm_vcpu *vcpu)
{
	vcpu->stat.instruction_ri++;

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
	vcpu->stat.instruction_gs++;

	if (test_kvm_facility(vcpu->kvm, 133)) {
		VCPU_EVENT(vcpu, 3, "%s", "ENABLE: GS (lazy)");
		preempt_disable();
		local_ctl_set_bit(2, CR2_GUARDED_STORAGE_BIT);
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
	struct kvm_s390_vm_tod_clock gtod = { 0 };
	int rc;
	u8 ar;
	u64 op2;

	vcpu->stat.instruction_sck++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	op2 = kvm_s390_get_base_disp_s(vcpu, &ar);
	if (op2 & 7)	/* Operand must be on a doubleword boundary */
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	rc = read_guest(vcpu, op2, ar, &gtod.tod, sizeof(gtod.tod));
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);

	VCPU_EVENT(vcpu, 3, "SCK: setting guest TOD to 0x%llx", gtod.tod);
	/*
	 * To set the TOD clock the kvm lock must be taken, but the vcpu lock
	 * is already held in handle_set_clock. The usual lock order is the
	 * opposite.  As SCK is deprecated and should not be used in several
	 * cases, for example when the multiple epoch facility or TOD clock
	 * steering facility is installed (see Principles of Operation),  a
	 * slow path can be used.  If the lock can not be taken via try_lock,
	 * the instruction will be retried via -EAGAIN at a later point in
	 * time.
	 */
	if (!kvm_s390_try_set_tod_clock(vcpu->kvm, &gtod)) {
		kvm_s390_retry_instr(vcpu);
		return -EAGAIN;
	}

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
	int rc;

	trace_kvm_s390_skey_related_inst(vcpu);
	/* Already enabled? */
	if (vcpu->arch.skey_enabled)
		return 0;

	rc = s390_enable_skey();
	VCPU_EVENT(vcpu, 3, "enabling storage keys for guest: %d", rc);
	if (rc)
		return rc;

	if (kvm_s390_test_cpuflags(vcpu, CPUSTAT_KSS))
		kvm_s390_clear_cpuflags(vcpu, CPUSTAT_KSS);
	if (!vcpu->kvm->arch.use_skf)
		vcpu->arch.sie_block->ictl |= ICTL_ISKE | ICTL_SSKE | ICTL_RRBE;
	else
		vcpu->arch.sie_block->ictl &= ~(ICTL_ISKE | ICTL_SSKE | ICTL_RRBE);
	vcpu->arch.skey_enabled = true;
	return 0;
}

static int try_handle_skey(struct kvm_vcpu *vcpu)
{
	int rc;

	rc = kvm_s390_skey_check_enable(vcpu);
	if (rc)
		return rc;
	if (vcpu->kvm->arch.use_skf) {
		/* with storage-key facility, SIE interprets it for us */
		kvm_s390_retry_instr(vcpu);
		VCPU_EVENT(vcpu, 4, "%s", "retrying storage key operation");
		return -EAGAIN;
	}
	return 0;
}

static int handle_iske(struct kvm_vcpu *vcpu)
{
	unsigned long gaddr, vmaddr;
	unsigned char key;
	int reg1, reg2;
	bool unlocked;
	int rc;

	vcpu->stat.instruction_iske++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	rc = try_handle_skey(vcpu);
	if (rc)
		return rc != -EAGAIN ? rc : 0;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	gaddr = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	gaddr = kvm_s390_logical_to_effective(vcpu, gaddr);
	gaddr = kvm_s390_real_to_abs(vcpu, gaddr);
	vmaddr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(gaddr));
	if (kvm_is_error_hva(vmaddr))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
retry:
	unlocked = false;
	mmap_read_lock(current->mm);
	rc = get_guest_storage_key(current->mm, vmaddr, &key);

	if (rc) {
		rc = fixup_user_fault(current->mm, vmaddr,
				      FAULT_FLAG_WRITE, &unlocked);
		if (!rc) {
			mmap_read_unlock(current->mm);
			goto retry;
		}
	}
	mmap_read_unlock(current->mm);
	if (rc == -EFAULT)
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	if (rc < 0)
		return rc;
	vcpu->run->s.regs.gprs[reg1] &= ~0xff;
	vcpu->run->s.regs.gprs[reg1] |= key;
	return 0;
}

static int handle_rrbe(struct kvm_vcpu *vcpu)
{
	unsigned long vmaddr, gaddr;
	int reg1, reg2;
	bool unlocked;
	int rc;

	vcpu->stat.instruction_rrbe++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	rc = try_handle_skey(vcpu);
	if (rc)
		return rc != -EAGAIN ? rc : 0;

	kvm_s390_get_regs_rre(vcpu, &reg1, &reg2);

	gaddr = vcpu->run->s.regs.gprs[reg2] & PAGE_MASK;
	gaddr = kvm_s390_logical_to_effective(vcpu, gaddr);
	gaddr = kvm_s390_real_to_abs(vcpu, gaddr);
	vmaddr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(gaddr));
	if (kvm_is_error_hva(vmaddr))
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
retry:
	unlocked = false;
	mmap_read_lock(current->mm);
	rc = reset_guest_reference_bit(current->mm, vmaddr);
	if (rc < 0) {
		rc = fixup_user_fault(current->mm, vmaddr,
				      FAULT_FLAG_WRITE, &unlocked);
		if (!rc) {
			mmap_read_unlock(current->mm);
			goto retry;
		}
	}
	mmap_read_unlock(current->mm);
	if (rc == -EFAULT)
		return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	if (rc < 0)
		return rc;
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
	bool unlocked;
	int rc;

	vcpu->stat.instruction_sske++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

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
		unsigned long vmaddr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(start));
		unlocked = false;

		if (kvm_is_error_hva(vmaddr))
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

		mmap_read_lock(current->mm);
		rc = cond_set_guest_storage_key(current->mm, vmaddr, key, &oldkey,
						m3 & SSKE_NQ, m3 & SSKE_MR,
						m3 & SSKE_MC);

		if (rc < 0) {
			rc = fixup_user_fault(current->mm, vmaddr,
					      FAULT_FLAG_WRITE, &unlocked);
			rc = !rc ? -EAGAIN : rc;
		}
		mmap_read_unlock(current->mm);
		if (rc == -EFAULT)
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		if (rc == -EAGAIN)
			continue;
		if (rc < 0)
			return rc;
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
	wait_event(vcpu->kvm->arch.ipte_wq, !ipte_lock_held(vcpu->kvm));
	kvm_s390_retry_instr(vcpu);
	VCPU_EVENT(vcpu, 4, "%s", "retrying ipte interlock operation");
	return 0;
}

static int handle_test_block(struct kvm_vcpu *vcpu)
{
	gpa_t addr;
	int reg2;

	vcpu->stat.instruction_tb++;

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

	vcpu->stat.instruction_tpi++;

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

	vcpu->stat.instruction_tsch++;

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
		vcpu->stat.instruction_io_other++;
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

/*
 * handle_pqap: Handling pqap interception
 * @vcpu: the vcpu having issue the pqap instruction
 *
 * We now support PQAP/AQIC instructions and we need to correctly
 * answer the guest even if no dedicated driver's hook is available.
 *
 * The intercepting code calls a dedicated callback for this instruction
 * if a driver did register one in the CRYPTO satellite of the
 * SIE block.
 *
 * If no callback is available, the queues are not available, return this
 * response code to the caller and set CC to 3.
 * Else return the response code returned by the callback.
 */
static int handle_pqap(struct kvm_vcpu *vcpu)
{
	struct ap_queue_status status = {};
	crypto_hook pqap_hook;
	unsigned long reg0;
	int ret;
	uint8_t fc;

	/* Verify that the AP instruction are available */
	if (!ap_instructions_available())
		return -EOPNOTSUPP;
	/* Verify that the guest is allowed to use AP instructions */
	if (!(vcpu->arch.sie_block->eca & ECA_APIE))
		return -EOPNOTSUPP;
	/*
	 * The only possibly intercepted functions when AP instructions are
	 * available for the guest are AQIC and TAPQ with the t bit set
	 * since we do not set IC.3 (FIII) we currently will only intercept
	 * the AQIC function code.
	 * Note: running nested under z/VM can result in intercepts for other
	 * function codes, e.g. PQAP(QCI). We do not support this and bail out.
	 */
	reg0 = vcpu->run->s.regs.gprs[0];
	fc = (reg0 >> 24) & 0xff;
	if (fc != 0x03)
		return -EOPNOTSUPP;

	/* PQAP instruction is allowed for guest kernel only */
	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	/* Common PQAP instruction specification exceptions */
	/* bits 41-47 must all be zeros */
	if (reg0 & 0x007f0000UL)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	/* APFT not install and T bit set */
	if (!test_kvm_facility(vcpu->kvm, 15) && (reg0 & 0x00800000UL))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);
	/* APXA not installed and APID greater 64 or APQI greater 16 */
	if (!(vcpu->kvm->arch.crypto.crycbd & 0x02) && (reg0 & 0x0000c0f0UL))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	/* AQIC function code specific exception */
	/* facility 65 not present for AQIC function code */
	if (!test_kvm_facility(vcpu->kvm, 65))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	/*
	 * If the hook callback is registered, there will be a pointer to the
	 * hook function pointer in the kvm_s390_crypto structure. Lock the
	 * owner, retrieve the hook function pointer and call the hook.
	 */
	down_read(&vcpu->kvm->arch.crypto.pqap_hook_rwsem);
	if (vcpu->kvm->arch.crypto.pqap_hook) {
		pqap_hook = *vcpu->kvm->arch.crypto.pqap_hook;
		ret = pqap_hook(vcpu);
		if (!ret) {
			if (vcpu->run->s.regs.gprs[1] & 0x00ff0000)
				kvm_s390_set_psw_cc(vcpu, 3);
			else
				kvm_s390_set_psw_cc(vcpu, 0);
		}
		up_read(&vcpu->kvm->arch.crypto.pqap_hook_rwsem);
		return ret;
	}
	up_read(&vcpu->kvm->arch.crypto.pqap_hook_rwsem);
	/*
	 * A vfio_driver must register a hook.
	 * No hook means no driver to enable the SIE CRYCB and no queues.
	 * We send this response to the guest.
	 */
	status.response_code = 0x01;
	memcpy(&vcpu->run->s.regs.gprs[1], &status, sizeof(status));
	kvm_s390_set_psw_cc(vcpu, 3);
	return 0;
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

	vcpu->stat.instruction_lpsw++;

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

	vcpu->stat.instruction_lpswe++;

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

	/* Bailout forbidden function codes */
	if (fc > 3 && fc != 15)
		goto out_no_data;

	/*
	 * fc 15 is provided only with
	 *   - PTF/CPU topology support through facility 15
	 *   - KVM_CAP_S390_USER_STSI
	 */
	if (fc == 15 && (!test_kvm_facility(vcpu->kvm, 11) ||
			 !vcpu->kvm->arch.user_stsi))
		goto out_no_data;

	if (vcpu->run->s.regs.gprs[0] & 0x0fffff00
	    || vcpu->run->s.regs.gprs[1] & 0xffff0000)
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	if (fc == 0) {
		vcpu->run->s.regs.gprs[0] = 3 << 28;
		kvm_s390_set_psw_cc(vcpu, 0);
		return 0;
	}

	operand2 = kvm_s390_get_base_disp_s(vcpu, &ar);

	if (!kvm_s390_pv_cpu_is_protected(vcpu) && (operand2 & 0xfff))
		return kvm_s390_inject_program_int(vcpu, PGM_SPECIFICATION);

	switch (fc) {
	case 1: /* same handling for 1 and 2 */
	case 2:
		mem = get_zeroed_page(GFP_KERNEL_ACCOUNT);
		if (!mem)
			goto out_no_data;
		if (stsi((void *) mem, fc, sel1, sel2))
			goto out_no_data;
		break;
	case 3:
		if (sel1 != 2 || sel2 != 2)
			goto out_no_data;
		mem = get_zeroed_page(GFP_KERNEL_ACCOUNT);
		if (!mem)
			goto out_no_data;
		handle_stsi_3_2_2(vcpu, (void *) mem);
		break;
	case 15: /* fc 15 is fully handled in userspace */
		insert_stsi_usr_data(vcpu, operand2, ar, fc, sel1, sel2);
		trace_kvm_s390_handle_stsi(vcpu, fc, sel1, sel2, operand2);
		return -EREMOTE;
	}
	if (kvm_s390_pv_cpu_is_protected(vcpu)) {
		memcpy(sida_addr(vcpu->arch.sie_block), (void *)mem, PAGE_SIZE);
		rc = 0;
	} else {
		rc = write_guest(vcpu, operand2, ar, (void *)mem, PAGE_SIZE);
	}
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

int kvm_s390_handle_b2(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->ipa & 0x00ff) {
	case 0x02:
		return handle_stidp(vcpu);
	case 0x04:
		return handle_set_clock(vcpu);
	case 0x10:
		return handle_set_prefix(vcpu);
	case 0x11:
		return handle_store_prefix(vcpu);
	case 0x12:
		return handle_store_cpu_address(vcpu);
	case 0x14:
		return kvm_s390_handle_vsie(vcpu);
	case 0x21:
	case 0x50:
		return handle_ipte_interlock(vcpu);
	case 0x29:
		return handle_iske(vcpu);
	case 0x2a:
		return handle_rrbe(vcpu);
	case 0x2b:
		return handle_sske(vcpu);
	case 0x2c:
		return handle_test_block(vcpu);
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x35:
	case 0x36:
	case 0x37:
	case 0x38:
	case 0x39:
	case 0x3a:
	case 0x3b:
	case 0x3c:
	case 0x5f:
	case 0x74:
	case 0x76:
		return handle_io_inst(vcpu);
	case 0x56:
		return handle_sthyi(vcpu);
	case 0x7d:
		return handle_stsi(vcpu);
	case 0xaf:
		return handle_pqap(vcpu);
	case 0xb1:
		return handle_stfl(vcpu);
	case 0xb2:
		return handle_lpswe(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}

static int handle_epsw(struct kvm_vcpu *vcpu)
{
	int reg1, reg2;

	vcpu->stat.instruction_epsw++;

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
		unsigned long vmaddr;
		bool unlocked = false;

		/* Translate guest address to host address */
		vmaddr = gfn_to_hva(vcpu->kvm, gpa_to_gfn(start));
		if (kvm_is_error_hva(vmaddr))
			return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);

		if (vcpu->run->s.regs.gprs[reg1] & PFMF_CF) {
			if (kvm_clear_guest(vcpu->kvm, start, PAGE_SIZE))
				return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
		}

		if (vcpu->run->s.regs.gprs[reg1] & PFMF_SK) {
			int rc = kvm_s390_skey_check_enable(vcpu);

			if (rc)
				return rc;
			mmap_read_lock(current->mm);
			rc = cond_set_guest_storage_key(current->mm, vmaddr,
							key, NULL, nq, mr, mc);
			if (rc < 0) {
				rc = fixup_user_fault(current->mm, vmaddr,
						      FAULT_FLAG_WRITE, &unlocked);
				rc = !rc ? -EAGAIN : rc;
			}
			mmap_read_unlock(current->mm);
			if (rc == -EFAULT)
				return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
			if (rc == -EAGAIN)
				continue;
			if (rc < 0)
				return rc;
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

/*
 * Must be called with relevant read locks held (kvm->mm->mmap_lock, kvm->srcu)
 */
static inline int __do_essa(struct kvm_vcpu *vcpu, const int orc)
{
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
		struct kvm_memory_slot *ms = gfn_to_memslot(vcpu->kvm, gfn);

		/* Increment only if we are really flipping the bit */
		if (ms && !test_and_set_bit(gfn - ms->base_gfn, kvm_second_dirty_bitmap(ms)))
			atomic64_inc(&vcpu->kvm->arch.cmma_dirty_pages);
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

	if (!vcpu->kvm->arch.migration_mode) {
		/*
		 * CMMA is enabled in the KVM settings, but is disabled in
		 * the SIE block and in the mm_context, and we are not doing
		 * a migration. Enable CMMA in the mm_context.
		 * Since we need to take a write lock to write to the context
		 * to avoid races with storage keys handling, we check if the
		 * value really needs to be written to; if the value is
		 * already correct, we do nothing and avoid the lock.
		 */
		if (vcpu->kvm->mm->context.uses_cmm == 0) {
			mmap_write_lock(vcpu->kvm->mm);
			vcpu->kvm->mm->context.uses_cmm = 1;
			mmap_write_unlock(vcpu->kvm->mm);
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
		int srcu_idx;

		mmap_read_lock(vcpu->kvm->mm);
		srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		i = __do_essa(vcpu, orc);
		srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
		mmap_read_unlock(vcpu->kvm->mm);
		if (i < 0)
			return i;
		/* Account for the possible extra cbrl entry */
		entries += i;
	}
	vcpu->arch.sie_block->cbrlo &= PAGE_MASK;	/* reset nceo */
	cbrlo = phys_to_virt(vcpu->arch.sie_block->cbrlo);
	mmap_read_lock(gmap->mm);
	for (i = 0; i < entries; ++i)
		__gmap_zap(gmap, cbrlo[i]);
	mmap_read_unlock(gmap->mm);
	return 0;
}

int kvm_s390_handle_b9(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->ipa & 0x00ff) {
	case 0x8a:
	case 0x8e:
	case 0x8f:
		return handle_ipte_interlock(vcpu);
	case 0x8d:
		return handle_epsw(vcpu);
	case 0xab:
		return handle_essa(vcpu);
	case 0xaf:
		return handle_pfmf(vcpu);
	default:
		return -EOPNOTSUPP;
	}
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

int kvm_s390_handle_eb(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->ipb & 0x000000ff) {
	case 0x25:
		return handle_stctg(vcpu);
	case 0x2f:
		return handle_lctlg(vcpu);
	case 0x60:
	case 0x61:
	case 0x62:
		return handle_ri(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}

static int handle_tprot(struct kvm_vcpu *vcpu)
{
	u64 address, operand2;
	unsigned long gpa;
	u8 access_key;
	bool writable;
	int ret, cc;
	u8 ar;

	vcpu->stat.instruction_tprot++;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE)
		return kvm_s390_inject_program_int(vcpu, PGM_PRIVILEGED_OP);

	kvm_s390_get_base_disp_sse(vcpu, &address, &operand2, &ar, NULL);
	access_key = (operand2 & 0xf0) >> 4;

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		ipte_lock(vcpu->kvm);

	ret = guest_translate_address_with_key(vcpu, address, ar, &gpa,
					       GACC_STORE, access_key);
	if (ret == 0) {
		gfn_to_hva_prot(vcpu->kvm, gpa_to_gfn(gpa), &writable);
	} else if (ret == PGM_PROTECTION) {
		writable = false;
		/* Write protected? Try again with read-only... */
		ret = guest_translate_address_with_key(vcpu, address, ar, &gpa,
						       GACC_FETCH, access_key);
	}
	if (ret >= 0) {
		cc = -1;

		/* Fetching permitted; storing permitted */
		if (ret == 0 && writable)
			cc = 0;
		/* Fetching permitted; storing not permitted */
		else if (ret == 0 && !writable)
			cc = 1;
		/* Fetching not permitted; storing not permitted */
		else if (ret == PGM_PROTECTION)
			cc = 2;
		/* Translation not available */
		else if (ret != PGM_ADDRESSING && ret != PGM_TRANSLATION_SPEC)
			cc = 3;

		if (cc != -1) {
			kvm_s390_set_psw_cc(vcpu, cc);
			ret = 0;
		} else {
			ret = kvm_s390_inject_program_int(vcpu, ret);
		}
	}

	if (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_DAT)
		ipte_unlock(vcpu->kvm);
	return ret;
}

int kvm_s390_handle_e5(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->ipa & 0x00ff) {
	case 0x01:
		return handle_tprot(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}

static int handle_sckpf(struct kvm_vcpu *vcpu)
{
	u32 value;

	vcpu->stat.instruction_sckpf++;

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
	vcpu->stat.instruction_ptff++;

	/* we don't emulate any control instructions yet */
	kvm_s390_set_psw_cc(vcpu, 3);
	return 0;
}

int kvm_s390_handle_01(struct kvm_vcpu *vcpu)
{
	switch (vcpu->arch.sie_block->ipa & 0x00ff) {
	case 0x04:
		return handle_ptff(vcpu);
	case 0x07:
		return handle_sckpf(vcpu);
	default:
		return -EOPNOTSUPP;
	}
}
