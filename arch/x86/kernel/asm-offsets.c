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
	OFFSET(TI_preempt_count, thread_info, preempt_count);

	BLANK();
	OFFSET(crypto_tfm_ctx_offset, crypto_tfm, __crt_ctx);

	BLANK();
	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

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
