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

/*
 * nop stub, which must not clobber anything *including the stack* to
 * avoid confusing the entry prologues.
 */
extern void _paravirt_nop(void);
asm (".pushsection .entry.text, \"ax\"\n"
     ".global _paravirt_nop\n"
     "_paravirt_nop:\n\t"
     "ret\n\t"
     ".size _paravirt_nop, . - _paravirt_nop\n\t"
     ".type _paravirt_nop, @function\n\t"
     ".popsection");

void __init default_banner(void)
{
	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       pv_info.name);
}

/* Undefined instruction for dealing with missing ops pointers. */
static const unsigned char ud2a[] = { 0x0f, 0x0b };

struct branch {
	unsigned char opcode;
	u32 delta;
} __attribute__((packed));

static unsigned paravirt_patch_call(void *insn_buff, const void *target,
				    unsigned long addr, unsigned len)
{
	const int call_len = 5;
	struct branch *b = insn_buff;
	unsigned long delta = (unsigned long)target - (addr+call_len);

	if (len < call_len) {
		pr_warn("paravirt: Failed to patch indirect CALL at %ps\n", (void *)addr);
		/* Kernel might not be viable if patching fails, bail out: */
		BUG_ON(1);
	}

	b->opcode = 0xe8; /* call */
	b->delta = delta;
	BUILD_BUG_ON(sizeof(*b) != call_len);

	return call_len;
}

#ifdef CONFIG_PARAVIRT_XXL
/* identity function, which can be inlined */
u64 notrace _paravirt_ident_64(u64 x)
{
	return x;
}

static unsigned paravirt_patch_jmp(void *insn_buff, const void *target,
				   unsigned long addr, unsigned len)
{
	struct branch *b = insn_buff;
	unsigned long delta = (unsigned long)target - (addr+5);

	if (len < 5) {
#ifdef CONFIG_RETPOLINE
		WARN_ONCE(1, "Failing to patch indirect JMP in %ps\n", (void *)addr);
#endif
		return len;	/* call too long for patch site */
	}

	b->opcode = 0xe9;	/* jmp */
	b->delta = delta;

	return 5;
}
#endif

DEFINE_STATIC_KEY_TRUE(virt_spin_lock_key);

void __init native_pv_lock_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		static_branch_disable(&virt_spin_lock_key);
}

unsigned paravirt_patch_default(u8 type, void *insn_buff,
				unsigned long addr, unsigned len)
{
	/*
	 * Neat trick to map patch type back to the call within the
	 * corresponding structure.
	 */
	void *opfunc = *((void **)&pv_ops + type);
	unsigned ret;

	if (opfunc == NULL)
		/* If there's no function, patch it with a ud2a (BUG) */
		ret = paravirt_patch_insns(insn_buff, len, ud2a, ud2a+sizeof(ud2a));
	else if (opfunc == _paravirt_nop)
		ret = 0;

#ifdef CONFIG_PARAVIRT_XXL
	/* identity functions just return their single argument */
	else if (opfunc == _paravirt_ident_64)
		ret = paravirt_patch_ident_64(insn_buff, len);

	else if (type == PARAVIRT_PATCH(cpu.iret) ||
		 type == PARAVIRT_PATCH(cpu.usergs_sysret64))
		/* If operation requires a jmp, then jmp */
		ret = paravirt_patch_jmp(insn_buff, opfunc, addr, len);
#endif
	else
		/* Otherwise call the function. */
		ret = paravirt_patch_call(insn_buff, opfunc, addr, len);

	return ret;
}

unsigned paravirt_patch_insns(void *insn_buff, unsigned len,
			      const char *start, const char *end)
{
	unsigned insn_len = end - start;

	/* Alternative instruction is too large for the patch site and we cannot continue: */
	BUG_ON(insn_len > len || start == NULL);

	memcpy(insn_buff, start, insn_len);

	return insn_len;
}

struct static_key paravirt_steal_enabled;
struct static_key paravirt_steal_rq_enabled;

static u64 native_steal_clock(int cpu)
{
	return 0;
}

/* These are in entry.S */
extern void native_iret(void);
extern void native_usergs_sysret64(void);

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

static DEFINE_PER_CPU(enum paravirt_lazy_mode, paravirt_lazy_mode) = PARAVIRT_LAZY_NONE;

static inline void enter_lazy(enum paravirt_lazy_mode mode)
{
	BUG_ON(this_cpu_read(paravirt_lazy_mode) != PARAVIRT_LAZY_NONE);

	this_cpu_write(paravirt_lazy_mode, mode);
}

static void leave_lazy(enum paravirt_lazy_mode mode)
{
	BUG_ON(this_cpu_read(paravirt_lazy_mode) != mode);

	this_cpu_write(paravirt_lazy_mode, PARAVIRT_LAZY_NONE);
}

void paravirt_enter_lazy_mmu(void)
{
	enter_lazy(PARAVIRT_LAZY_MMU);
}

void paravirt_leave_lazy_mmu(void)
{
	leave_lazy(PARAVIRT_LAZY_MMU);
}

void paravirt_flush_lazy_mmu(void)
{
	preempt_disable();

	if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU) {
		arch_leave_lazy_mmu_mode();
		arch_enter_lazy_mmu_mode();
	}

	preempt_enable();
}

#ifdef CONFIG_PARAVIRT_XXL
void paravirt_start_context_switch(struct task_struct *prev)
{
	BUG_ON(preemptible());

	if (this_cpu_read(paravirt_lazy_mode) == PARAVIRT_LAZY_MMU) {
		arch_leave_lazy_mmu_mode();
		set_ti_thread_flag(task_thread_info(prev), TIF_LAZY_MMU_UPDATES);
	}
	enter_lazy(PARAVIRT_LAZY_CPU);
}

void paravirt_end_context_switch(struct task_struct *next)
{
	BUG_ON(preemptible());

	leave_lazy(PARAVIRT_LAZY_CPU);

	if (test_and_clear_ti_thread_flag(task_thread_info(next), TIF_LAZY_MMU_UPDATES))
		arch_enter_lazy_mmu_mode();
}
#endif

enum paravirt_lazy_mode paravirt_get_lazy_mode(void)
{
	if (in_interrupt())
		return PARAVIRT_LAZY_NONE;

	return this_cpu_read(paravirt_lazy_mode);
}

struct pv_info pv_info = {
	.name = "bare hardware",
#ifdef CONFIG_PARAVIRT_XXL
	.kernel_rpl = 0,
	.shared_kernel_pmd = 1,	/* Only used when CONFIG_X86_PAE is set */

#ifdef CONFIG_X86_64
	.extra_user_64bit_cs = __USER_CS,
#endif
#endif
};

/* 64-bit pagetable entries */
#define PTE_IDENT	__PV_IS_CALLEE_SAVE(_paravirt_ident_64)

struct paravirt_patch_template pv_ops = {
	/* Init ops. */
	.init.patch		= native_patch,

	/* Time ops. */
	.time.sched_clock	= native_sched_clock,
	.time.steal_clock	= native_steal_clock,

	/* Cpu ops. */
	.cpu.io_delay		= native_io_delay,

#ifdef CONFIG_PARAVIRT_XXL
	.cpu.cpuid		= native_cpuid,
	.cpu.get_debugreg	= native_get_debugreg,
	.cpu.set_debugreg	= native_set_debugreg,
	.cpu.read_cr0		= native_read_cr0,
	.cpu.write_cr0		= native_write_cr0,
	.cpu.write_cr4		= native_write_cr4,
	.cpu.wbinvd		= native_wbinvd,
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
#ifdef CONFIG_X86_64
	.cpu.load_gs_index	= native_load_gs_index,
#endif
	.cpu.write_ldt_entry	= native_write_ldt_entry,
	.cpu.write_gdt_entry	= native_write_gdt_entry,
	.cpu.write_idt_entry	= native_write_idt_entry,

	.cpu.alloc_ldt		= paravirt_nop,
	.cpu.free_ldt		= paravirt_nop,

	.cpu.load_sp0		= native_load_sp0,

#ifdef CONFIG_X86_64
	.cpu.usergs_sysret64	= native_usergs_sysret64,
#endif
	.cpu.iret		= native_iret,
	.cpu.swapgs		= native_swapgs,

#ifdef CONFIG_X86_IOPL_IOPERM
	.cpu.invalidate_io_bitmap	= native_tss_invalidate_io_bitmap,
	.cpu.update_io_bitmap		= native_tss_update_io_bitmap,
#endif

	.cpu.start_context_switch	= paravirt_nop,
	.cpu.end_context_switch		= paravirt_nop,

	/* Irq ops. */
	.irq.save_fl		= __PV_IS_CALLEE_SAVE(native_save_fl),
	.irq.restore_fl		= __PV_IS_CALLEE_SAVE(native_restore_fl),
	.irq.irq_disable	= __PV_IS_CALLEE_SAVE(native_irq_disable),
	.irq.irq_enable		= __PV_IS_CALLEE_SAVE(native_irq_enable),
	.irq.safe_halt		= native_safe_halt,
	.irq.halt		= native_halt,
#endif /* CONFIG_PARAVIRT_XXL */

	/* Mmu ops. */
	.mmu.flush_tlb_user	= native_flush_tlb_local,
	.mmu.flush_tlb_kernel	= native_flush_tlb_global,
	.mmu.flush_tlb_one_user	= native_flush_tlb_one_user,
	.mmu.flush_tlb_others	= native_flush_tlb_others,
	.mmu.tlb_remove_table	=
			(void (*)(struct mmu_gather *, void *))tlb_remove_page,

	.mmu.exit_mmap		= paravirt_nop,

#ifdef CONFIG_PARAVIRT_XXL
	.mmu.read_cr2		= __PV_IS_CALLEE_SAVE(native_read_cr2),
	.mmu.write_cr2		= native_write_cr2,
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
	.mmu.set_pte_at		= native_set_pte_at,
	.mmu.set_pmd		= native_set_pmd,

	.mmu.ptep_modify_prot_start	= __ptep_modify_prot_start,
	.mmu.ptep_modify_prot_commit	= __ptep_modify_prot_commit,

#if CONFIG_PGTABLE_LEVELS >= 3
#ifdef CONFIG_X86_PAE
	.mmu.set_pte_atomic	= native_set_pte_atomic,
	.mmu.pte_clear		= native_pte_clear,
	.mmu.pmd_clear		= native_pmd_clear,
#endif
	.mmu.set_pud		= native_set_pud,

	.mmu.pmd_val		= PTE_IDENT,
	.mmu.make_pmd		= PTE_IDENT,

#if CONFIG_PGTABLE_LEVELS >= 4
	.mmu.pud_val		= PTE_IDENT,
	.mmu.make_pud		= PTE_IDENT,

	.mmu.set_p4d		= native_set_p4d,

#if CONFIG_PGTABLE_LEVELS >= 5
	.mmu.p4d_val		= PTE_IDENT,
	.mmu.make_p4d		= PTE_IDENT,

	.mmu.set_pgd		= native_set_pgd,
#endif /* CONFIG_PGTABLE_LEVELS >= 5 */
#endif /* CONFIG_PGTABLE_LEVELS >= 4 */
#endif /* CONFIG_PGTABLE_LEVELS >= 3 */

	.mmu.pte_val		= PTE_IDENT,
	.mmu.pgd_val		= PTE_IDENT,

	.mmu.make_pte		= PTE_IDENT,
	.mmu.make_pgd		= PTE_IDENT,

	.mmu.dup_mmap		= paravirt_nop,
	.mmu.activate_mm	= paravirt_nop,

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
/* At this point, native_get/set_debugreg has real function entries */
NOKPROBE_SYMBOL(native_get_debugreg);
NOKPROBE_SYMBOL(native_set_debugreg);
NOKPROBE_SYMBOL(native_load_idt);
#endif

EXPORT_SYMBOL(pv_ops);
EXPORT_SYMBOL_GPL(pv_info);
