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

typedef int (*intercept_handler_t)(struct kvm_vcpu *vcpu);

/* declare vfacilities extern */
extern unsigned long *vfacilities;

int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu);

/* Transactional Memory Execution related macros */
#define IS_TE_ENABLED(vcpu)	((vcpu->arch.sie_block->ecb & 0x10))
#define TDB_ADDR		0x1800UL
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

static inline int __cpu_is_stopped(struct kvm_vcpu *vcpu)
{
	return atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_STOP_INT;
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

static inline void kvm_s390_set_prefix(struct kvm_vcpu *vcpu, u32 prefix)
{
	vcpu->arch.sie_block->prefix = prefix & 0x7fffe000u;
	vcpu->arch.sie_block->ihcpu  = 0xffff;
	kvm_make_request(KVM_REQ_MMU_RELOAD, vcpu);
}

static inline u64 kvm_s390_get_base_disp_s(struct kvm_vcpu *vcpu)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

static inline void kvm_s390_get_base_disp_sse(struct kvm_vcpu *vcpu,
					      u64 *address1, u64 *address2)
{
	u32 base1 = (vcpu->arch.sie_block->ipb & 0xf0000000) >> 28;
	u32 disp1 = (vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16;
	u32 base2 = (vcpu->arch.sie_block->ipb & 0xf000) >> 12;
	u32 disp2 = vcpu->arch.sie_block->ipb & 0x0fff;

	*address1 = (base1 ? vcpu->run->s.regs.gprs[base1] : 0) + disp1;
	*address2 = (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

static inline void kvm_s390_get_regs_rre(struct kvm_vcpu *vcpu, int *r1, int *r2)
{
	if (r1)
		*r1 = (vcpu->arch.sie_block->ipb & 0x00f00000) >> 20;
	if (r2)
		*r2 = (vcpu->arch.sie_block->ipb & 0x000f0000) >> 16;
}

static inline u64 kvm_s390_get_base_disp_rsy(struct kvm_vcpu *vcpu)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16) +
			((vcpu->arch.sie_block->ipb & 0xff00) << 4);
	/* The displacement is a 20bit _SIGNED_ value */
	if (disp2 & 0x80000)
		disp2+=0xfff00000;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + (long)(int)disp2;
}

static inline u64 kvm_s390_get_base_disp_rs(struct kvm_vcpu *vcpu)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

/* Set the condition code in the guest program status word */
static inline void kvm_s390_set_psw_cc(struct kvm_vcpu *vcpu, unsigned long cc)
{
	vcpu->arch.sie_block->gpsw.mask &= ~(3UL << 44);
	vcpu->arch.sie_block->gpsw.mask |= cc << 44;
}

int kvm_s390_handle_wait(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer);
void kvm_s390_tasklet(unsigned long parm);
void kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu);
void kvm_s390_deliver_pending_machine_checks(struct kvm_vcpu *vcpu);
int __must_check kvm_s390_inject_vm(struct kvm *kvm,
				    struct kvm_s390_interrupt *s390int);
int __must_check kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu,
				      struct kvm_s390_interrupt *s390int);
int __must_check kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code);
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 cr6, u64 schid);

/* implemented in priv.c */
int kvm_s390_handle_b2(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e5(struct kvm_vcpu *vcpu);
int kvm_s390_handle_01(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b9(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_eb(struct kvm_vcpu *vcpu);

/* implemented in sigp.c */
int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu);

/* implemented in kvm-s390.c */
int kvm_s390_store_status_unloaded(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr);
void s390_vcpu_block(struct kvm_vcpu *vcpu);
void s390_vcpu_unblock(struct kvm_vcpu *vcpu);
void exit_sie(struct kvm_vcpu *vcpu);
void exit_sie_sync(struct kvm_vcpu *vcpu);
/* implemented in diag.c */
int kvm_s390_handle_diag(struct kvm_vcpu *vcpu);

#endif
