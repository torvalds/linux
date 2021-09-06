/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_ASM_PROTOTYPES_H
#define _ASM_POWERPC_ASM_PROTOTYPES_H
/*
 * This file is for prototypes of C functions that are only called
 * from asm, and any associated variables.
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

/* SMP */
extern struct task_struct *current_set[NR_CPUS];
extern struct task_struct *secondary_current;
void start_secondary(void *unused);

/* kexec */
struct paca_struct;
struct kimage;
extern struct paca_struct kexec_paca;
void kexec_copy_flush(struct kimage *image);

/* pseries hcall tracing */
extern struct static_key hcall_tracepoint_key;
void __trace_hcall_entry(unsigned long opcode, unsigned long *args);
void __trace_hcall_exit(long opcode, long retval, unsigned long *retbuf);

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

/* VMX copying */
int enter_vmx_usercopy(void);
int exit_vmx_usercopy(void);
int enter_vmx_ops(void);
void *exit_vmx_ops(void *dest);

/* signals, syscalls and interrupts */
long sys_swapcontext(struct ucontext __user *old_ctx,
		    struct ucontext __user *new_ctx,
		    long ctx_size);
#ifdef CONFIG_PPC32
long sys_debug_setcontext(struct ucontext __user *ctx,
			  int ndbg, struct sig_dbg_op __user *dbg);
int
ppc_select(int n, fd_set __user *inp, fd_set __user *outp, fd_set __user *exp,
	   struct __kernel_old_timeval __user *tvp);
unsigned long __init early_init(unsigned long dt_ptr);
void __init machine_init(u64 dt_ptr);
#endif
long system_call_exception(long r3, long r4, long r5, long r6, long r7, long r8, unsigned long r0, struct pt_regs *regs);
notrace unsigned long syscall_exit_prepare(unsigned long r3, struct pt_regs *regs, long scv);
notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs, unsigned long msr);
notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs, unsigned long msr);

long ppc_fadvise64_64(int fd, int advice, u32 offset_high, u32 offset_low,
		      u32 len_high, u32 len_low);
long sys_switch_endian(void);

/* prom_init (OpenFirmware) */
unsigned long __init prom_init(unsigned long r3, unsigned long r4,
			       unsigned long pp,
			       unsigned long r6, unsigned long r7,
			       unsigned long kbase);

/* setup */
void __init early_setup(unsigned long dt_ptr);
void early_setup_secondary(void);

/* misc runtime */
extern u64 __bswapdi2(u64);
extern s64 __lshrdi3(s64, int);
extern s64 __ashldi3(s64, int);
extern s64 __ashrdi3(s64, int);
extern int __cmpdi2(s64, s64);
extern int __ucmpdi2(u64, u64);

/* tracing */
void _mcount(void);
unsigned long prepare_ftrace_return(unsigned long parent, unsigned long ip,
						unsigned long sp);

void pnv_power9_force_smt4_catch(void);
void pnv_power9_force_smt4_release(void);

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

void kvmhv_save_host_pmu(void);
void kvmhv_load_host_pmu(void);
void kvmhv_save_guest_pmu(struct kvm_vcpu *vcpu, bool pmu_in_use);
void kvmhv_load_guest_pmu(struct kvm_vcpu *vcpu);

int __kvmhv_vcpu_entry_p9(struct kvm_vcpu *vcpu);

long kvmppc_h_set_dabr(struct kvm_vcpu *vcpu, unsigned long dabr);
long kvmppc_h_set_xdabr(struct kvm_vcpu *vcpu, unsigned long dabr,
			unsigned long dabrx);

#endif /* _ASM_POWERPC_ASM_PROTOTYPES_H */
