// SPDX-License-Identifier: GPL-2.0-or-later
/*  Paravirtualization interfaces
    Copyright (C) 2006 Rusty Russell IBM Corporation


    2007 - x86_64 support added by Glauber de Oliveira Costa, Red Hat Inc
*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/efi.h>
#include <linux/bcd.h>
#include <linux/highmem.h>
#include <linux/kprobes.h>
#include <linux/pgtable.h>
#include <linux/static_call.h>

#include <asm/bug.h>
#include <asm/paravirt.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/time.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/fixmap.h>
#include <asm/apic.h>
#include <asm/tlbflush.h>
#include <asm/timer.h>
#include <asm/special_insns.h>
#include <asm/tlb.h>
#include <asm/io_bitmap.h>
#include <asm/gsseg.h>

/* stub always returning 0. */
DEFINE_ASM_FUNC(paravirt_ret0, "xor %eax,%eax", .entry.text);

void __init default_banner(void)
{
	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       pv_info.name);
}

#ifdef CONFIG_PARAVIRT_XXL
DEFINE_ASM_FUNC(_paravirt_ident_64, "mov %rdi, %rax", .text);
DEFINE_ASM_FUNC(pv_native_save_fl, "pushf; pop %rax", .noinstr.text);
DEFINE_ASM_FUNC(pv_native_irq_disable, "cli", .noinstr.text);
DEFINE_ASM_FUNC(pv_native_irq_enable, "sti", .noinstr.text);
DEFINE_ASM_FUNC(pv_native_read_cr2, "mov %cr2, %rax", .noinstr.text);
#endif

DEFINE_STATIC_KEY_FALSE(virt_spin_lock_key);

void __init native_pv_lock_init(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		static_branch_enable(&virt_spin_lock_key);
}

static void native_tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	tlb_remove_page(tlb, table);
}

struct static_key paravirt_steal_enabled;
struct static_key paravirt_steal_rq_enabled;

static u64 native_steal_clock(int cpu)
{
	return 0;
}

DEFINE_STATIC_CALL(pv_steal_clock, native_steal_clock);
DEFINE_STATIC_CALL(pv_sched_clock, native_sched_clock);

void paravirt_set_sched_clock(u64 (*func)(void))
{
	static_call_update(pv_sched_clock, func);
}

/* These are in entry.S */
static struct resource reserve_ioports = {
	.start = 0,
	.end = IO_SPACE_LIMIT,
	.name = "paravirt-ioport",
	.flags = IORESOURCE_IO | IORESOURCE_BUSY,
};

/*
 * Reserve the whole legacy IO space to prevent any legacy drivers
 * from wasting time probing for their hardware.  This is a fairly
 * brute-force approach to disabling all non-virtual drivers.
 *
 * Note that this must be called very early to have any effect.
 */
int paravirt_disable_iospace(void)
{
	return request_resource(&ioport_resource, &reserve_ioports);
}

#ifdef CONFIG_PARAVIRT_XXL
static noinstr void pv_native_write_cr2(unsigned long val)
{
	native_write_cr2(val);
}

static noinstr unsigned long pv_native_get_debugreg(int regno)
{
	return native_get_debugreg(regno);
}

static noinstr void pv_native_set_debugreg(int regno, unsigned long val)
{
	native_set_debugreg(regno, val);
}

static noinstr void pv_native_safe_halt(void)
{
	native_safe_halt();
}
#endif

struct pv_info pv_info = {
	.name = "bare hardware",
#ifdef CONFIG_PARAVIRT_XXL
	.extra_user_64bit_cs = __USER_CS,
#endif
};

/* 64-bit pagetable entries */
#define PTE_IDENT	__PV_IS_CALLEE_SAVE(_paravirt_ident_64)

struct paravirt_patch_template pv_ops = {
	/* Cpu ops. */
	.cpu.io_delay		= native_io_delay,

#ifdef CONFIG_PARAVIRT_XXL
	.cpu.cpuid		= native_cpuid,
	.cpu.get_debugreg	= pv_native_get_debugreg,
	.cpu.set_debugreg	= pv_native_set_debugreg,
	.cpu.read_cr0		= native_read_cr0,
	.cpu.write_cr0		= native_write_cr0,
	.cpu.write_cr4		= native_write_cr4,
	.cpu.read_msr		= native_read_msr,
	.cpu.write_msr		= native_write_msr,
	.cpu.read_msr_safe	= native_read_msr_safe,
	.cpu.write_msr_safe	= native_write_msr_safe,
	.cpu.read_pmc		= native_read_pmc,
	.cpu.load_tr_desc	= native_load_tr_desc,
	.cpu.set_ldt		= native_set_ldt,
	.cpu.load_gdt		= native_load_gdt,
	.cpu.load_idt		= native_load_idt,
	.cpu.store_tr		= native_store_tr,
	.cpu.load_tls		= native_load_tls,
	.cpu.load_gs_index	= native_load_gs_index,
	.cpu.write_ldt_entry	= native_write_ldt_entry,
	.cpu.write_gdt_entry	= native_write_gdt_entry,
	.cpu.write_idt_entry	= native_write_idt_entry,

	.cpu.alloc_ldt		= paravirt_nop,
	.cpu.free_ldt		= paravirt_nop,

	.cpu.load_sp0		= native_load_sp0,

#ifdef CONFIG_X86_IOPL_IOPERM
	.cpu.invalidate_io_bitmap	= native_tss_invalidate_io_bitmap,
	.cpu.update_io_bitmap		= native_tss_update_io_bitmap,
#endif

	.cpu.start_context_switch	= paravirt_nop,
	.cpu.end_context_switch		= paravirt_nop,

	/* Irq ops. */
	.irq.save_fl		= __PV_IS_CALLEE_SAVE(pv_native_save_fl),
	.irq.irq_disable	= __PV_IS_CALLEE_SAVE(pv_native_irq_disable),
	.irq.irq_enable		= __PV_IS_CALLEE_SAVE(pv_native_irq_enable),
	.irq.safe_halt		= pv_native_safe_halt,
	.irq.halt		= native_halt,
#endif /* CONFIG_PARAVIRT_XXL */

	/* Mmu ops. */
	.mmu.flush_tlb_user	= native_flush_tlb_local,
	.mmu.flush_tlb_kernel	= native_flush_tlb_global,
	.mmu.flush_tlb_one_user	= native_flush_tlb_one_user,
	.mmu.flush_tlb_multi	= native_flush_tlb_multi,
	.mmu.tlb_remove_table	= native_tlb_remove_table,

	.mmu.exit_mmap		= paravirt_nop,
	.mmu.notify_page_enc_status_changed	= paravirt_nop,

#ifdef CONFIG_PARAVIRT_XXL
	.mmu.read_cr2		= __PV_IS_CALLEE_SAVE(pv_native_read_cr2),
	.mmu.write_cr2		= pv_native_write_cr2,
	.mmu.read_cr3		= __native_read_cr3,
	.mmu.write_cr3		= native_write_cr3,

	.mmu.pgd_alloc		= __paravirt_pgd_alloc,
	.mmu.pgd_free		= paravirt_nop,

	.mmu.alloc_pte		= paravirt_nop,
	.mmu.alloc_pmd		= paravirt_nop,
	.mmu.alloc_pud		= paravirt_nop,
	.mmu.alloc_p4d		= paravirt_nop,
	.mmu.release_pte	= paravirt_nop,
	.mmu.release_pmd	= paravirt_nop,
	.mmu.release_pud	= paravirt_nop,
	.mmu.release_p4d	= paravirt_nop,

	.mmu.set_pte		= native_set_pte,
	.mmu.set_pmd		= native_set_pmd,

	.mmu.ptep_modify_prot_start	= __ptep_modify_prot_start,
	.mmu.ptep_modify_prot_commit	= __ptep_modify_prot_commit,

	.mmu.set_pud		= native_set_pud,

	.mmu.pmd_val		= PTE_IDENT,
	.mmu.make_pmd		= PTE_IDENT,

	.mmu.pud_val		= PTE_IDENT,
	.mmu.make_pud		= PTE_IDENT,

	.mmu.set_p4d		= native_set_p4d,

#if CONFIG_PGTABLE_LEVELS >= 5
	.mmu.p4d_val		= PTE_IDENT,
	.mmu.make_p4d		= PTE_IDENT,

	.mmu.set_pgd		= native_set_pgd,
#endif /* CONFIG_PGTABLE_LEVELS >= 5 */

	.mmu.pte_val		= PTE_IDENT,
	.mmu.pgd_val		= PTE_IDENT,

	.mmu.make_pte		= PTE_IDENT,
	.mmu.make_pgd		= PTE_IDENT,

	.mmu.enter_mmap		= paravirt_nop,

	.mmu.lazy_mode = {
		.enter		= paravirt_nop,
		.leave		= paravirt_nop,
		.flush		= paravirt_nop,
	},

	.mmu.set_fixmap		= native_set_fixmap,
#endif /* CONFIG_PARAVIRT_XXL */

#if defined(CONFIG_PARAVIRT_SPINLOCKS)
	/* Lock ops. */
#ifdef CONFIG_SMP
	.lock.queued_spin_lock_slowpath	= native_queued_spin_lock_slowpath,
	.lock.queued_spin_unlock	=
				PV_CALLEE_SAVE(__native_queued_spin_unlock),
	.lock.wait			= paravirt_nop,
	.lock.kick			= paravirt_nop,
	.lock.vcpu_is_preempted		=
				PV_CALLEE_SAVE(__native_vcpu_is_preempted),
#endif /* SMP */
#endif
};

#ifdef CONFIG_PARAVIRT_XXL
NOKPROBE_SYMBOL(native_load_idt);
#endif

EXPORT_SYMBOL(pv_ops);
EXPORT_SYMBOL_GPL(pv_info);
