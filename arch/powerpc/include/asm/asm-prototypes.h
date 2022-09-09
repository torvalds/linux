/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_ASM_PROTOTYPES_H
#define _ASM_POWERPC_ASM_PROTOTYPES_H
/*
 * This file is for C prototypes of asm symbols that are EXPORTed.
 * It allows the modversions logic to see their prototype and
 * generate proper CRCs for them.
 *
 * Copyright 2016, Daniel Axtens, IBM Corporation.
 */

#include <linux/threads.h>
#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <linux/uaccess.h>
#include <asm/epapr_hcalls.h>
#include <asm/dcr.h>
#include <asm/mmu_context.h>
#include <asm/ultravisor-api.h>

#include <uapi/asm/ucontext.h>

/* Ultravisor */
#if defined(CONFIG_PPC_POWERNV) || defined(CONFIG_PPC_SVM)
long ucall_norets(unsigned long opcode, ...);
#else
static inline long ucall_norets(unsigned long opcode, ...)
{
	return U_NOT_AVAILABLE;
}
#endif

/* OPAL */
int64_t __opal_call(int64_t a0, int64_t a1, int64_t a2, int64_t a3,
		    int64_t a4, int64_t a5, int64_t a6, int64_t a7,
		    int64_t opcode, uint64_t msr);

/* misc runtime */
extern u64 __bswapdi2(u64);
extern s64 __lshrdi3(s64, int);
extern s64 __ashldi3(s64, int);
extern s64 __ashrdi3(s64, int);
extern int __cmpdi2(s64, s64);
extern int __ucmpdi2(u64, u64);

/* tracing */
void _mcount(void);

/* Transaction memory related */
void tm_enable(void);
void tm_disable(void);
void tm_abort(uint8_t cause);

struct kvm_vcpu;
void _kvmppc_restore_tm_pr(struct kvm_vcpu *vcpu, u64 guest_msr);
void _kvmppc_save_tm_pr(struct kvm_vcpu *vcpu, u64 guest_msr);

/* Patch sites */
extern s32 patch__call_flush_branch_caches1;
extern s32 patch__call_flush_branch_caches2;
extern s32 patch__call_flush_branch_caches3;
extern s32 patch__flush_count_cache_return;
extern s32 patch__flush_link_stack_return;
extern s32 patch__call_kvm_flush_link_stack;
extern s32 patch__call_kvm_flush_link_stack_p9;
extern s32 patch__memset_nocache, patch__memcpy_nocache;

extern long flush_branch_caches;
extern long kvm_flush_link_stack;

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void kvmppc_save_tm_hv(struct kvm_vcpu *vcpu, u64 msr, bool preserve_nv);
void kvmppc_restore_tm_hv(struct kvm_vcpu *vcpu, u64 msr, bool preserve_nv);
#else
static inline void kvmppc_save_tm_hv(struct kvm_vcpu *vcpu, u64 msr,
				     bool preserve_nv) { }
static inline void kvmppc_restore_tm_hv(struct kvm_vcpu *vcpu, u64 msr,
					bool preserve_nv) { }
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

void kvmppc_p9_enter_guest(struct kvm_vcpu *vcpu);

long kvmppc_h_set_dabr(struct kvm_vcpu *vcpu, unsigned long dabr);
long kvmppc_h_set_xdabr(struct kvm_vcpu *vcpu, unsigned long dabr,
			unsigned long dabrx);

#endif /* _ASM_POWERPC_ASM_PROTOTYPES_H */
