/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */
#define COMPILE_OFFSETS

#include <linux/crypto.h>
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

#ifdef CONFIG_XEN
#include <xen/interface/xen.h>
#endif

#ifdef CONFIG_X86_32
# include "asm-offsets_32.c"
#else
# include "asm-offsets_64.c"
#endif

void common(void) {
	BLANK();
	OFFSET(TI_flags, thread_info, flags);
	OFFSET(TI_status, thread_info, status);
	OFFSET(TI_addr_limit, thread_info, addr_limit);

	BLANK();
	OFFSET(crypto_tfm_ctx_offset, crypto_tfm, __crt_ctx);

	BLANK();
	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
	BLANK();
	OFFSET(IA32_SIGCONTEXT_ax, sigcontext_ia32, ax);
	OFFSET(IA32_SIGCONTEXT_bx, sigcontext_ia32, bx);
	OFFSET(IA32_SIGCONTEXT_cx, sigcontext_ia32, cx);
	OFFSET(IA32_SIGCONTEXT_dx, sigcontext_ia32, dx);
	OFFSET(IA32_SIGCONTEXT_si, sigcontext_ia32, si);
	OFFSET(IA32_SIGCONTEXT_di, sigcontext_ia32, di);
	OFFSET(IA32_SIGCONTEXT_bp, sigcontext_ia32, bp);
	OFFSET(IA32_SIGCONTEXT_sp, sigcontext_ia32, sp);
	OFFSET(IA32_SIGCONTEXT_ip, sigcontext_ia32, ip);

	BLANK();
	OFFSET(TI_sysenter_return, thread_info, sysenter_return);

	BLANK();
	OFFSET(IA32_RT_SIGFRAME_sigcontext, rt_sigframe_ia32, uc.uc_mcontext);
#endif

#ifdef CONFIG_PARAVIRT
	BLANK();
	OFFSET(PARAVIRT_enabled, pv_info, paravirt_enabled);
	OFFSET(PARAVIRT_PATCH_pv_cpu_ops, paravirt_patch_template, pv_cpu_ops);
	OFFSET(PARAVIRT_PATCH_pv_irq_ops, paravirt_patch_template, pv_irq_ops);
	OFFSET(PV_IRQ_irq_disable, pv_irq_ops, irq_disable);
	OFFSET(PV_IRQ_irq_enable, pv_irq_ops, irq_enable);
	OFFSET(PV_CPU_iret, pv_cpu_ops, iret);
#ifdef CONFIG_X86_32
	OFFSET(PV_CPU_irq_enable_sysexit, pv_cpu_ops, irq_enable_sysexit);
#endif
	OFFSET(PV_CPU_read_cr0, pv_cpu_ops, read_cr0);
	OFFSET(PV_MMU_read_cr2, pv_mmu_ops, read_cr2);
#endif

#ifdef CONFIG_XEN
	BLANK();
	OFFSET(XEN_vcpu_info_mask, vcpu_info, evtchn_upcall_mask);
	OFFSET(XEN_vcpu_info_pending, vcpu_info, evtchn_upcall_pending);
#endif

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	OFFSET(BP_hardware_subarch, boot_params, hdr.hardware_subarch);
	OFFSET(BP_version, boot_params, hdr.version);
	OFFSET(BP_kernel_alignment, boot_params, hdr.kernel_alignment);
	OFFSET(BP_pref_address, boot_params, hdr.pref_address);
	OFFSET(BP_code32_start, boot_params, hdr.code32_start);

	BLANK();
	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));
}
