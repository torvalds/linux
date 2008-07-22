/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/crypto.h>
#include <linux/sched.h> 
#include <linux/stddef.h>
#include <linux/errno.h> 
#include <linux/hardirq.h>
#include <linux/suspend.h>
#include <linux/kbuild.h>
#include <asm/pda.h>
#include <asm/processor.h>
#include <asm/segment.h>
#include <asm/thread_info.h>
#include <asm/ia32.h>
#include <asm/bootparam.h>

#include <xen/interface/xen.h>

#define __NO_STUBS 1
#undef __SYSCALL
#undef _ASM_X86_64_UNISTD_H_
#define __SYSCALL(nr, sym) [nr] = 1,
static char syscalls[] = {
#include <asm/unistd.h>
};

int main(void)
{
#define ENTRY(entry) DEFINE(tsk_ ## entry, offsetof(struct task_struct, entry))
	ENTRY(state);
	ENTRY(flags); 
	ENTRY(pid);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(TI_ ## entry, offsetof(struct thread_info, entry))
	ENTRY(flags);
	ENTRY(addr_limit);
	ENTRY(preempt_count);
	ENTRY(status);
#ifdef CONFIG_IA32_EMULATION
	ENTRY(sysenter_return);
#endif
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(pda_ ## entry, offsetof(struct x8664_pda, entry))
	ENTRY(kernelstack); 
	ENTRY(oldrsp); 
	ENTRY(pcurrent); 
	ENTRY(irqcount);
	ENTRY(cpunumber);
	ENTRY(irqstackptr);
	ENTRY(data_offset);
	BLANK();
#undef ENTRY
#ifdef CONFIG_PARAVIRT
	BLANK();
	OFFSET(PARAVIRT_enabled, pv_info, paravirt_enabled);
	OFFSET(PARAVIRT_PATCH_pv_cpu_ops, paravirt_patch_template, pv_cpu_ops);
	OFFSET(PARAVIRT_PATCH_pv_irq_ops, paravirt_patch_template, pv_irq_ops);
	OFFSET(PV_IRQ_irq_disable, pv_irq_ops, irq_disable);
	OFFSET(PV_IRQ_irq_enable, pv_irq_ops, irq_enable);
	OFFSET(PV_IRQ_adjust_exception_frame, pv_irq_ops, adjust_exception_frame);
	OFFSET(PV_CPU_iret, pv_cpu_ops, iret);
	OFFSET(PV_CPU_usergs_sysret32, pv_cpu_ops, usergs_sysret32);
	OFFSET(PV_CPU_usergs_sysret64, pv_cpu_ops, usergs_sysret64);
	OFFSET(PV_CPU_irq_enable_sysexit, pv_cpu_ops, irq_enable_sysexit);
	OFFSET(PV_CPU_swapgs, pv_cpu_ops, swapgs);
	OFFSET(PV_MMU_read_cr2, pv_mmu_ops, read_cr2);
#endif


#ifdef CONFIG_IA32_EMULATION
#define ENTRY(entry) DEFINE(IA32_SIGCONTEXT_ ## entry, offsetof(struct sigcontext_ia32, entry))
	ENTRY(ax);
	ENTRY(bx);
	ENTRY(cx);
	ENTRY(dx);
	ENTRY(si);
	ENTRY(di);
	ENTRY(bp);
	ENTRY(sp);
	ENTRY(ip);
	BLANK();
#undef ENTRY
	DEFINE(IA32_RT_SIGFRAME_sigcontext,
	       offsetof (struct rt_sigframe32, uc.uc_mcontext));
	BLANK();
#endif
	DEFINE(pbe_address, offsetof(struct pbe, address));
	DEFINE(pbe_orig_address, offsetof(struct pbe, orig_address));
	DEFINE(pbe_next, offsetof(struct pbe, next));
	BLANK();
#define ENTRY(entry) DEFINE(pt_regs_ ## entry, offsetof(struct pt_regs, entry))
	ENTRY(bx);
	ENTRY(bx);
	ENTRY(cx);
	ENTRY(dx);
	ENTRY(sp);
	ENTRY(bp);
	ENTRY(si);
	ENTRY(di);
	ENTRY(r8);
	ENTRY(r9);
	ENTRY(r10);
	ENTRY(r11);
	ENTRY(r12);
	ENTRY(r13);
	ENTRY(r14);
	ENTRY(r15);
	ENTRY(flags);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(saved_context_ ## entry, offsetof(struct saved_context, entry))
	ENTRY(cr0);
	ENTRY(cr2);
	ENTRY(cr3);
	ENTRY(cr4);
	ENTRY(cr8);
	BLANK();
#undef ENTRY
	DEFINE(TSS_ist, offsetof(struct tss_struct, x86_tss.ist));
	BLANK();
	DEFINE(crypto_tfm_ctx_offset, offsetof(struct crypto_tfm, __crt_ctx));
	BLANK();
	DEFINE(__NR_syscall_max, sizeof(syscalls) - 1);

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	OFFSET(BP_hardware_subarch, boot_params, hdr.hardware_subarch);
	OFFSET(BP_version, boot_params, hdr.version);

	BLANK();
	DEFINE(PAGE_SIZE_asm, PAGE_SIZE);
#ifdef CONFIG_XEN
	BLANK();
	OFFSET(XEN_vcpu_info_mask, vcpu_info, evtchn_upcall_mask);
	OFFSET(XEN_vcpu_info_pending, vcpu_info, evtchn_upcall_pending);
#undef ENTRY
#endif
	return 0;
}
