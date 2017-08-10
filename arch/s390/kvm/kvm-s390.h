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
#include <asm/processor.h>
#include <asm/sclp.h>

typedef int (*intercept_handler_t)(struct kvm_vcpu *vcpu);

/* Transactional Memory Execution related macros */
#define IS_TE_ENABLED(vcpu)	((vcpu->arch.sie_block->ecb & ECB_TE))
#define TDB_FORMAT1		1
#define IS_ITDB_VALID(vcpu)	((*(char *)vcpu->arch.sie_block->itdba == TDB_FORMAT1))

extern debug_info_t *kvm_s390_dbf;
#define KVM_EVENT(d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(kvm_s390_dbf, d_loglevel, d_string "\n", \
	  d_args); \
} while (0)

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

static inline int is_vcpu_idle(struct kvm_vcpu *vcpu)
{
	return test_bit(vcpu->vcpu_id, vcpu->arch.local_int.float_int->idle_mask);
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
	VCPU_EVENT(vcpu, 3, "set prefix of cpu %03u to 0x%x", vcpu->vcpu_id,
		   prefix);
	vcpu->arch.sie_block->prefix = prefix >> GUEST_PREFIX_SHIFT;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	kvm_make_request(KVM_REQ_MMU_RELOAD, vcpu);
}

static inline u64 kvm_s390_get_base_disp_s(struct kvm_vcpu *vcpu, u8 *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

static inline void kvm_s390_get_base_disp_sse(struct kvm_vcpu *vcpu,
					      u64 *address1, u64 *address2,
					      u8 *ar_b1, u8 *ar_b2)
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

static inline u64 kvm_s390_get_base_disp_rsy(struct kvm_vcpu *vcpu, u8 *ar)
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

static inline u64 kvm_s390_get_base_disp_rs(struct kvm_vcpu *vcpu, u8 *ar)
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
	return __test_facility(nr, kvm->arch.model.fac_mask) &&
		__test_facility(nr, kvm->arch.model.fac_list);
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

static inline int test_kvm_cpu_feat(struct kvm *kvm, unsigned long nr)
{
	WARN_ON_ONCE(nr >= KVM_S390_VM_CPU_FEAT_NR_BITS);
	return test_bit_inv(nr, kvm->arch.cpu_feat);
}

/* are cpu states controlled by user space */
static inline int kvm_s390_user_cpu_state_ctrl(struct kvm *kvm)
{
	return kvm->arch.user_cpu_state_ctrl != 0;
}

/* implemented in interrupt.c */
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
static inline int kvm_s390_inject_prog_irq(struct kvm_vcpu *vcpu,
					   struct kvm_s390_pgm_info *pgm_info)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_PROGRAM_INT,
		.u.pgm = *pgm_info,
	};

	return kvm_s390_inject_vcpu(vcpu, &irq);
}
static inline int kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_PROGRAM_INT,
		.u.pgm.code = code,
	};

	return kvm_s390_inject_vcpu(vcpu, &irq);
}
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 isc_mask, u32 schid);
int kvm_s390_reinject_io_int(struct kvm *kvm,
			     struct kvm_s390_interrupt_info *inti);
int kvm_s390_mask_adapter(struct kvm *kvm, unsigned int id, bool masked);

/* implemented in intercept.c */
u8 kvm_s390_get_ilen(struct kvm_vcpu *vcpu);
int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu);
static inline void kvm_s390_rewind_psw(struct kvm_vcpu *vcpu, int ilen)
{
	struct kvm_s390_sie_block *sie_block = vcpu->arch.sie_block;

	sie_block->gpsw.addr = __rewind_psw(sie_block->gpsw, ilen);
}
static inline void kvm_s390_forward_psw(struct kvm_vcpu *vcpu, int ilen)
{
	kvm_s390_rewind_psw(vcpu, -ilen);
}
static inline void kvm_s390_retry_instr(struct kvm_vcpu *vcpu)
{
	/* don't inject PER events if we re-execute the instruction */
	vcpu->arch.sie_block->icptstatus &= ~0x02;
	kvm_s390_rewind_psw(vcpu, kvm_s390_get_ilen(vcpu));
}

/* implemented in priv.c */
int is_valid_psw(psw_t *psw);
int kvm_s390_handle_aa(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b2(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e3(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e5(struct kvm_vcpu *vcpu);
int kvm_s390_handle_01(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b9(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu);
int kvm_s390_handle_stctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_eb(struct kvm_vcpu *vcpu);
int kvm_s390_skey_check_enable(struct kvm_vcpu *vcpu);

/* implemented in vsie.c */
int kvm_s390_handle_vsie(struct kvm_vcpu *vcpu);
void kvm_s390_vsie_kick(struct kvm_vcpu *vcpu);
void kvm_s390_vsie_gmap_notifier(struct gmap *gmap, unsigned long start,
				 unsigned long end);
void kvm_s390_vsie_init(struct kvm *kvm);
void kvm_s390_vsie_destroy(struct kvm *kvm);

/* implemented in sigp.c */
int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu);
int kvm_s390_handle_sigp_pei(struct kvm_vcpu *vcpu);

/* implemented in sthyi.c */
int handle_sthyi(struct kvm_vcpu *vcpu);

/* implemented in kvm-s390.c */
void kvm_s390_set_tod_clock(struct kvm *kvm, u64 tod);
long kvm_arch_fault_in_page(struct kvm_vcpu *vcpu, gpa_t gpa, int writable);
int kvm_s390_store_status_unloaded(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr);
void kvm_s390_vcpu_start(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_stop(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_unblock(struct kvm_vcpu *vcpu);
void exit_sie(struct kvm_vcpu *vcpu);
void kvm_s390_sync_request(int req, struct kvm_vcpu *vcpu);
int kvm_s390_vcpu_setup_cmma(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_unsetup_cmma(struct kvm_vcpu *vcpu);
unsigned long kvm_s390_fac_list_mask_size(void);
extern unsigned long kvm_s390_fac_list_mask[];
void kvm_s390_set_cpu_timer(struct kvm_vcpu *vcpu, __u64 cputm);
__u64 kvm_s390_get_cpu_timer(struct kvm_vcpu *vcpu);

/* implemented in diag.c */
int kvm_s390_handle_diag(struct kvm_vcpu *vcpu);

static inline void kvm_s390_vcpu_block_all(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	WARN_ON(!mutex_is_locked(&kvm->lock));
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_s390_vcpu_block(vcpu);
}

static inline void kvm_s390_vcpu_unblock_all(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_s390_vcpu_unblock(vcpu);
}

static inline u64 kvm_s390_get_tod_clock_fast(struct kvm *kvm)
{
	u64 rc;

	preempt_disable();
	rc = get_tod_clock_fast() + kvm->arch.epoch;
	preempt_enable();
	return rc;
}

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
int kvm_s390_set_irq_state(struct kvm_vcpu *vcpu,
			   void __user *buf, int len);
int kvm_s390_get_irq_state(struct kvm_vcpu *vcpu,
			   __u8 __user *buf, int len);

/* implemented in guestdbg.c */
void kvm_s390_backup_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_restore_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_patch_guest_per_regs(struct kvm_vcpu *vcpu);
int kvm_s390_import_bp_data(struct kvm_vcpu *vcpu,
			    struct kvm_guest_debug *dbg);
void kvm_s390_clear_bp_data(struct kvm_vcpu *vcpu);
void kvm_s390_prepare_debug_exit(struct kvm_vcpu *vcpu);
int kvm_s390_handle_per_ifetch_icpt(struct kvm_vcpu *vcpu);
int kvm_s390_handle_per_event(struct kvm_vcpu *vcpu);

/* support for Basic/Extended SCA handling */
static inline union ipte_control *kvm_s390_get_ipte_control(struct kvm *kvm)
{
	struct bsca_block *sca = kvm->arch.sca; /* SCA version doesn't matter */

	return &sca->ipte_control;
}
static inline int kvm_s390_use_sca_entries(void)
{
	/*
	 * Without SIGP interpretation, only SRS interpretation (if available)
	 * might use the entries. By not setting the entries and keeping them
	 * invalid, hardware will not access them but intercept.
	 */
	return sclp.has_sigpif;
}
void kvm_s390_reinject_machine_check(struct kvm_vcpu *vcpu,
				     struct mcck_volatile_info *mcck_info);
#endif
