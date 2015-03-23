/*
 * definition for kvm on s390
 *
 * Copyright IBM Corp. 2008, 2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#ifndef ARCH_S390_KVM_S390_H
#define ARCH_S390_KVM_S390_H

#include <linux/hrtimer.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/facility.h>

typedef int (*intercept_handler_t)(struct kvm_vcpu *vcpu);

/* Transactional Memory Execution related macros */
#define IS_TE_ENABLED(vcpu)	((vcpu->arch.sie_block->ecb & 0x10))
#define TDB_FORMAT1		1
#define IS_ITDB_VALID(vcpu)	((*(char *)vcpu->arch.sie_block->itdba == TDB_FORMAT1))

#define VM_EVENT(d_kvm, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_kvm->arch.dbf, d_loglevel, d_string "\n", \
	  d_args); \
} while (0)

#define VCPU_EVENT(d_vcpu, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_vcpu->kvm->arch.dbf, d_loglevel, \
	  "%02d[%016lx-%016lx]: " d_string "\n", d_vcpu->vcpu_id, \
	  d_vcpu->arch.sie_block->gpsw.mask, d_vcpu->arch.sie_block->gpsw.addr,\
	  d_args); \
} while (0)

static inline int is_vcpu_stopped(struct kvm_vcpu *vcpu)
{
	return atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_STOPPED;
}

static inline int kvm_is_ucontrol(struct kvm *kvm)
{
#ifdef CONFIG_KVM_S390_UCONTROL
	if (kvm->arch.gmap)
		return 0;
	return 1;
#else
	return 0;
#endif
}

#define GUEST_PREFIX_SHIFT 13
static inline u32 kvm_s390_get_prefix(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sie_block->prefix << GUEST_PREFIX_SHIFT;
}

static inline void kvm_s390_set_prefix(struct kvm_vcpu *vcpu, u32 prefix)
{
	vcpu->arch.sie_block->prefix = prefix >> GUEST_PREFIX_SHIFT;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	kvm_make_request(KVM_REQ_MMU_RELOAD, vcpu);
}

typedef u8 __bitwise ar_t;

static inline u64 kvm_s390_get_base_disp_s(struct kvm_vcpu *vcpu, ar_t *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

static inline void kvm_s390_get_base_disp_sse(struct kvm_vcpu *vcpu,
					      u64 *address1, u64 *address2,
					      ar_t *ar_b1, ar_t *ar_b2)
{
	u32 base1 = (vcpu->arch.sie_block->ipb & 0xf0000000) >> 28;
	u32 disp1 = (vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16;
	u32 base2 = (vcpu->arch.sie_block->ipb & 0xf000) >> 12;
	u32 disp2 = vcpu->arch.sie_block->ipb & 0x0fff;

	*address1 = (base1 ? vcpu->run->s.regs.gprs[base1] : 0) + disp1;
	*address2 = (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;

	if (ar_b1)
		*ar_b1 = base1;
	if (ar_b2)
		*ar_b2 = base2;
}

static inline void kvm_s390_get_regs_rre(struct kvm_vcpu *vcpu, int *r1, int *r2)
{
	if (r1)
		*r1 = (vcpu->arch.sie_block->ipb & 0x00f00000) >> 20;
	if (r2)
		*r2 = (vcpu->arch.sie_block->ipb & 0x000f0000) >> 16;
}

static inline u64 kvm_s390_get_base_disp_rsy(struct kvm_vcpu *vcpu, ar_t *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16) +
			((vcpu->arch.sie_block->ipb & 0xff00) << 4);
	/* The displacement is a 20bit _SIGNED_ value */
	if (disp2 & 0x80000)
		disp2+=0xfff00000;

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + (long)(int)disp2;
}

static inline u64 kvm_s390_get_base_disp_rs(struct kvm_vcpu *vcpu, ar_t *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

/* Set the condition code in the guest program status word */
static inline void kvm_s390_set_psw_cc(struct kvm_vcpu *vcpu, unsigned long cc)
{
	vcpu->arch.sie_block->gpsw.mask &= ~(3UL << 44);
	vcpu->arch.sie_block->gpsw.mask |= cc << 44;
}

/* test availability of facility in a kvm instance */
static inline int test_kvm_facility(struct kvm *kvm, unsigned long nr)
{
	return __test_facility(nr, kvm->arch.model.fac->mask) &&
		__test_facility(nr, kvm->arch.model.fac->list);
}

static inline int set_kvm_facility(u64 *fac_list, unsigned long nr)
{
	unsigned char *ptr;

	if (nr >= MAX_FACILITY_BIT)
		return -EINVAL;
	ptr = (unsigned char *) fac_list + (nr >> 3);
	*ptr |= (0x80UL >> (nr & 7));
	return 0;
}

/* are cpu states controlled by user space */
static inline int kvm_s390_user_cpu_state_ctrl(struct kvm *kvm)
{
	return kvm->arch.user_cpu_state_ctrl != 0;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_wakeup(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer);
int __must_check kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu);
void kvm_s390_clear_local_irqs(struct kvm_vcpu *vcpu);
void kvm_s390_clear_float_irqs(struct kvm *kvm);
int __must_check kvm_s390_inject_vm(struct kvm *kvm,
				    struct kvm_s390_interrupt *s390int);
int __must_check kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu,
				      struct kvm_s390_irq *irq);
int __must_check kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code);
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 cr6, u64 schid);
int kvm_s390_reinject_io_int(struct kvm *kvm,
			     struct kvm_s390_interrupt_info *inti);
int kvm_s390_mask_adapter(struct kvm *kvm, unsigned int id, bool masked);

/* implemented in intercept.c */
void kvm_s390_rewind_psw(struct kvm_vcpu *vcpu, int ilc);
int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu);

/* implemented in priv.c */
int is_valid_psw(psw_t *psw);
int kvm_s390_handle_b2(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e5(struct kvm_vcpu *vcpu);
int kvm_s390_handle_01(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b9(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu);
int kvm_s390_handle_stctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_eb(struct kvm_vcpu *vcpu);

/* implemented in sigp.c */
int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu);
int kvm_s390_handle_sigp_pei(struct kvm_vcpu *vcpu);

/* implemented in kvm-s390.c */
long kvm_arch_fault_in_page(struct kvm_vcpu *vcpu, gpa_t gpa, int writable);
int kvm_s390_store_status_unloaded(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_store_adtl_status_unloaded(struct kvm_vcpu *vcpu,
					unsigned long addr);
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_vcpu_store_adtl_status(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_s390_vcpu_start(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_stop(struct kvm_vcpu *vcpu);
void s390_vcpu_block(struct kvm_vcpu *vcpu);
void s390_vcpu_unblock(struct kvm_vcpu *vcpu);
void exit_sie(struct kvm_vcpu *vcpu);
void exit_sie_sync(struct kvm_vcpu *vcpu);
int kvm_s390_vcpu_setup_cmma(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_unsetup_cmma(struct kvm_vcpu *vcpu);
/* is cmma enabled */
bool kvm_s390_cmma_enabled(struct kvm *kvm);
unsigned long kvm_s390_fac_list_mask_size(void);
extern unsigned long kvm_s390_fac_list_mask[];

/* implemented in diag.c */
int kvm_s390_handle_diag(struct kvm_vcpu *vcpu);
/* implemented in interrupt.c */
int kvm_s390_inject_prog_irq(struct kvm_vcpu *vcpu,
			     struct kvm_s390_pgm_info *pgm_info);

/**
 * kvm_s390_inject_prog_cond - conditionally inject a program check
 * @vcpu: virtual cpu
 * @rc: original return/error code
 *
 * This function is supposed to be used after regular guest access functions
 * failed, to conditionally inject a program check to a vcpu. The typical
 * pattern would look like
 *
 * rc = write_guest(vcpu, addr, data, len);
 * if (rc)
 *	return kvm_s390_inject_prog_cond(vcpu, rc);
 *
 * A negative return code from guest access functions implies an internal error
 * like e.g. out of memory. In these cases no program check should be injected
 * to the guest.
 * A positive value implies that an exception happened while accessing a guest's
 * memory. In this case all data belonging to the corresponding program check
 * has been stored in vcpu->arch.pgm and can be injected with
 * kvm_s390_inject_prog_irq().
 *
 * Returns: - the original @rc value if @rc was negative (internal error)
 *	    - zero if @rc was already zero
 *	    - zero or error code from injecting if @rc was positive
 *	      (program check injected to @vcpu)
 */
static inline int kvm_s390_inject_prog_cond(struct kvm_vcpu *vcpu, int rc)
{
	if (rc <= 0)
		return rc;
	return kvm_s390_inject_prog_irq(vcpu, &vcpu->arch.pgm);
}

int s390int_to_s390irq(struct kvm_s390_interrupt *s390int,
			struct kvm_s390_irq *s390irq);

/* implemented in interrupt.c */
int kvm_s390_vcpu_has_irq(struct kvm_vcpu *vcpu, int exclude_stop);
int psw_extint_disabled(struct kvm_vcpu *vcpu);
void kvm_s390_destroy_adapters(struct kvm *kvm);
int kvm_s390_ext_call_pending(struct kvm_vcpu *vcpu);
extern struct kvm_device_ops kvm_flic_ops;
int kvm_s390_is_stop_irq_pending(struct kvm_vcpu *vcpu);
void kvm_s390_clear_stop_irq(struct kvm_vcpu *vcpu);

/* implemented in guestdbg.c */
void kvm_s390_backup_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_restore_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_patch_guest_per_regs(struct kvm_vcpu *vcpu);
int kvm_s390_import_bp_data(struct kvm_vcpu *vcpu,
			    struct kvm_guest_debug *dbg);
void kvm_s390_clear_bp_data(struct kvm_vcpu *vcpu);
void kvm_s390_prepare_debug_exit(struct kvm_vcpu *vcpu);
void kvm_s390_handle_per_event(struct kvm_vcpu *vcpu);

#endif
