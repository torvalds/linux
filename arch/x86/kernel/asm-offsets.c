// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */
#define COMPILE_OFFSETS

#include <linux/crypto.h>
#include <crypto/aria.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/hardirq.h>
#include <linux/suspend.h>
#include <linux/kbuild.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/sigframe.h>
#include <asm/bootparam.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>
#include <asm/tdx.h>

#ifdef CONFIG_XEN
#include <xen/interface/xen.h>
#endif

#ifdef CONFIG_X86_32
# include "asm-offsets_32.c"
#else
# include "asm-offsets_64.c"
#endif

static void __used common(void)
{
	BLANK();
	OFFSET(TASK_threadsp, task_struct, thread.sp);
#ifdef CONFIG_STACKPROTECTOR
	OFFSET(TASK_stack_canary, task_struct, stack_canary);
#endif

	BLANK();
	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
	BLANK();
	OFFSET(IA32_SIGCONTEXT_ax, sigcontext_32, ax);
	OFFSET(IA32_SIGCONTEXT_bx, sigcontext_32, bx);
	OFFSET(IA32_SIGCONTEXT_cx, sigcontext_32, cx);
	OFFSET(IA32_SIGCONTEXT_dx, sigcontext_32, dx);
	OFFSET(IA32_SIGCONTEXT_si, sigcontext_32, si);
	OFFSET(IA32_SIGCONTEXT_di, sigcontext_32, di);
	OFFSET(IA32_SIGCONTEXT_bp, sigcontext_32, bp);
	OFFSET(IA32_SIGCONTEXT_sp, sigcontext_32, sp);
	OFFSET(IA32_SIGCONTEXT_ip, sigcontext_32, ip);

	BLANK();
	OFFSET(IA32_RT_SIGFRAME_sigcontext, rt_sigframe_ia32, uc.uc_mcontext);
#endif

#ifdef CONFIG_XEN
	BLANK();
	OFFSET(XEN_vcpu_info_mask, vcpu_info, evtchn_upcall_mask);
	OFFSET(XEN_vcpu_info_pending, vcpu_info, evtchn_upcall_pending);
	OFFSET(XEN_vcpu_info_arch_cr2, vcpu_info, arch.cr2);
#endif

	BLANK();
	OFFSET(TDX_MODULE_rcx, tdx_module_args, rcx);
	OFFSET(TDX_MODULE_rdx, tdx_module_args, rdx);
	OFFSET(TDX_MODULE_r8,  tdx_module_args, r8);
	OFFSET(TDX_MODULE_r9,  tdx_module_args, r9);
	OFFSET(TDX_MODULE_r10, tdx_module_args, r10);
	OFFSET(TDX_MODULE_r11, tdx_module_args, r11);
	OFFSET(TDX_MODULE_r12, tdx_module_args, r12);
	OFFSET(TDX_MODULE_r13, tdx_module_args, r13);
	OFFSET(TDX_MODULE_r14, tdx_module_args, r14);
	OFFSET(TDX_MODULE_r15, tdx_module_args, r15);
	OFFSET(TDX_MODULE_rbx, tdx_module_args, rbx);
	OFFSET(TDX_MODULE_rdi, tdx_module_args, rdi);
	OFFSET(TDX_MODULE_rsi, tdx_module_args, rsi);

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_secure_boot, boot_params, secure_boot);
	OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	OFFSET(BP_hardware_subarch, boot_params, hdr.hardware_subarch);
	OFFSET(BP_version, boot_params, hdr.version);
	OFFSET(BP_kernel_alignment, boot_params, hdr.kernel_alignment);
	OFFSET(BP_init_size, boot_params, hdr.init_size);
	OFFSET(BP_pref_address, boot_params, hdr.pref_address);

	BLANK();
	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));

	/* TLB state for the entry code */
	OFFSET(TLB_STATE_user_pcid_flush_mask, tlb_state, user_pcid_flush_mask);

	/* Layout info for cpu_entry_area */
	OFFSET(CPU_ENTRY_AREA_entry_stack, cpu_entry_area, entry_stack_page);
	DEFINE(SIZEOF_entry_stack, sizeof(struct entry_stack));
	DEFINE(MASK_entry_stack, (~(sizeof(struct entry_stack) - 1)));

	/* Offset for fields in tss_struct */
	OFFSET(TSS_sp0, tss_struct, x86_tss.sp0);
	OFFSET(TSS_sp1, tss_struct, x86_tss.sp1);
	OFFSET(TSS_sp2, tss_struct, x86_tss.sp2);
#if IS_ENABLED(CONFIG_CRYPTO_ARIA_AESNI_AVX_X86_64)
	/* Offset for fields in aria_ctx */
	BLANK();
	OFFSET(ARIA_CTX_enc_key, aria_ctx, enc_key);
	OFFSET(ARIA_CTX_dec_key, aria_ctx, dec_key);
	OFFSET(ARIA_CTX_rounds, aria_ctx, rounds);
#endif

}
