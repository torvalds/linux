/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/crypto.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <linux/kbuild.h>
#include <asm/ucontext.h>
#include <asm/sigframe.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/bootparam.h>
#include <asm/elf.h>
#include <asm/suspend.h>

#include <xen/interface/xen.h>

#include <linux/lguest.h>
#include "../../../drivers/lguest/lg.h"

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

void foo(void)
{
	OFFSET(IA32_SIGCONTEXT_ax, sigcontext, ax);
	OFFSET(IA32_SIGCONTEXT_bx, sigcontext, bx);
	OFFSET(IA32_SIGCONTEXT_cx, sigcontext, cx);
	OFFSET(IA32_SIGCONTEXT_dx, sigcontext, dx);
	OFFSET(IA32_SIGCONTEXT_si, sigcontext, si);
	OFFSET(IA32_SIGCONTEXT_di, sigcontext, di);
	OFFSET(IA32_SIGCONTEXT_bp, sigcontext, bp);
	OFFSET(IA32_SIGCONTEXT_sp, sigcontext, sp);
	OFFSET(IA32_SIGCONTEXT_ip, sigcontext, ip);
	BLANK();

	OFFSET(CPUINFO_x86, cpuinfo_x86, x86);
	OFFSET(CPUINFO_x86_vendor, cpuinfo_x86, x86_vendor);
	OFFSET(CPUINFO_x86_model, cpuinfo_x86, x86_model);
	OFFSET(CPUINFO_x86_mask, cpuinfo_x86, x86_mask);
	OFFSET(CPUINFO_hard_math, cpuinfo_x86, hard_math);
	OFFSET(CPUINFO_cpuid_level, cpuinfo_x86, cpuid_level);
	OFFSET(CPUINFO_x86_capability, cpuinfo_x86, x86_capability);
	OFFSET(CPUINFO_x86_vendor_id, cpuinfo_x86, x86_vendor_id);
	BLANK();

	OFFSET(TI_task, thread_info, task);
	OFFSET(TI_exec_domain, thread_info, exec_domain);
	OFFSET(TI_flags, thread_info, flags);
	OFFSET(TI_status, thread_info, status);
	OFFSET(TI_preempt_count, thread_info, preempt_count);
	OFFSET(TI_addr_limit, thread_info, addr_limit);
	OFFSET(TI_restart_block, thread_info, restart_block);
	OFFSET(TI_sysenter_return, thread_info, sysenter_return);
	OFFSET(TI_cpu, thread_info, cpu);
	BLANK();

	OFFSET(GDS_size, desc_ptr, size);
	OFFSET(GDS_address, desc_ptr, address);
	BLANK();

	OFFSET(PT_EBX, pt_regs, bx);
	OFFSET(PT_ECX, pt_regs, cx);
	OFFSET(PT_EDX, pt_regs, dx);
	OFFSET(PT_ESI, pt_regs, si);
	OFFSET(PT_EDI, pt_regs, di);
	OFFSET(PT_EBP, pt_regs, bp);
	OFFSET(PT_EAX, pt_regs, ax);
	OFFSET(PT_DS,  pt_regs, ds);
	OFFSET(PT_ES,  pt_regs, es);
	OFFSET(PT_FS,  pt_regs, fs);
	OFFSET(PT_GS,  pt_regs, gs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_ax);
	OFFSET(PT_EIP, pt_regs, ip);
	OFFSET(PT_CS,  pt_regs, cs);
	OFFSET(PT_EFLAGS, pt_regs, flags);
	OFFSET(PT_OLDESP, pt_regs, sp);
	OFFSET(PT_OLDSS,  pt_regs, ss);
	BLANK();

	OFFSET(EXEC_DOMAIN_handler, exec_domain, handler);
	OFFSET(IA32_RT_SIGFRAME_sigcontext, rt_sigframe, uc.uc_mcontext);
	BLANK();

	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

	/* Offset from the sysenter stack to tss.sp0 */
	DEFINE(TSS_sysenter_sp0, offsetof(struct tss_struct, x86_tss.sp0) -
		 sizeof(struct tss_struct));

	DEFINE(PAGE_SIZE_asm, PAGE_SIZE);
	DEFINE(PAGE_SHIFT_asm, PAGE_SHIFT);
	DEFINE(THREAD_SIZE_asm, THREAD_SIZE);

	OFFSET(crypto_tfm_ctx_offset, crypto_tfm, __crt_ctx);

#ifdef CONFIG_PARAVIRT
	BLANK();
	OFFSET(PARAVIRT_enabled, pv_info, paravirt_enabled);
	OFFSET(PARAVIRT_PATCH_pv_cpu_ops, paravirt_patch_template, pv_cpu_ops);
	OFFSET(PARAVIRT_PATCH_pv_irq_ops, paravirt_patch_template, pv_irq_ops);
	OFFSET(PV_IRQ_irq_disable, pv_irq_ops, irq_disable);
	OFFSET(PV_IRQ_irq_enable, pv_irq_ops, irq_enable);
	OFFSET(PV_CPU_iret, pv_cpu_ops, iret);
	OFFSET(PV_CPU_irq_enable_sysexit, pv_cpu_ops, irq_enable_sysexit);
	OFFSET(PV_CPU_read_cr0, pv_cpu_ops, read_cr0);
#endif

#ifdef CONFIG_XEN
	BLANK();
	OFFSET(XEN_vcpu_info_mask, vcpu_info, evtchn_upcall_mask);
	OFFSET(XEN_vcpu_info_pending, vcpu_info, evtchn_upcall_pending);
#endif

#if defined(CONFIG_LGUEST) || defined(CONFIG_LGUEST_GUEST) || defined(CONFIG_LGUEST_MODULE)
	BLANK();
	OFFSET(LGUEST_DATA_irq_enabled, lguest_data, irq_enabled);
	OFFSET(LGUEST_DATA_irq_pending, lguest_data, irq_pending);
	OFFSET(LGUEST_DATA_pgdir, lguest_data, pgdir);

	BLANK();
	OFFSET(LGUEST_PAGES_host_gdt_desc, lguest_pages, state.host_gdt_desc);
	OFFSET(LGUEST_PAGES_host_idt_desc, lguest_pages, state.host_idt_desc);
	OFFSET(LGUEST_PAGES_host_cr3, lguest_pages, state.host_cr3);
	OFFSET(LGUEST_PAGES_host_sp, lguest_pages, state.host_sp);
	OFFSET(LGUEST_PAGES_guest_gdt_desc, lguest_pages,state.guest_gdt_desc);
	OFFSET(LGUEST_PAGES_guest_idt_desc, lguest_pages,state.guest_idt_desc);
	OFFSET(LGUEST_PAGES_guest_gdt, lguest_pages, state.guest_gdt);
	OFFSET(LGUEST_PAGES_regs_trapnum, lguest_pages, regs.trapnum);
	OFFSET(LGUEST_PAGES_regs_errcode, lguest_pages, regs.errcode);
	OFFSET(LGUEST_PAGES_regs, lguest_pages, regs);
#endif

	BLANK();
	OFFSET(BP_scratch, boot_params, scratch);
	OFFSET(BP_loadflags, boot_params, hdr.loadflags);
	OFFSET(BP_hardware_subarch, boot_params, hdr.hardware_subarch);
	OFFSET(BP_version, boot_params, hdr.version);
	OFFSET(BP_kernel_alignment, boot_params, hdr.kernel_alignment);
}
