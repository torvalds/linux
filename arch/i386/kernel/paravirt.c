/*  Paravirtualization interfaces
    Copyright (C) 2006 Rusty Russell IBM Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/bcd.h>
#include <linux/start_kernel.h>

#include <asm/bug.h>
#include <asm/paravirt.h>
#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/arch_hooks.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/fixmap.h>
#include <asm/apic.h>
#include <asm/tlbflush.h>
#include <asm/timer.h>

/* nop stub */
void _paravirt_nop(void)
{
}

static void __init default_banner(void)
{
	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       paravirt_ops.name);
}

char *memory_setup(void)
{
	return paravirt_ops.memory_setup();
}

/* Simple instruction patching code. */
#define DEF_NATIVE(name, code)					\
	extern const char start_##name[], end_##name[];		\
	asm("start_" #name ": " code "; end_" #name ":")
DEF_NATIVE(cli, "cli");
DEF_NATIVE(sti, "sti");
DEF_NATIVE(popf, "push %eax; popf");
DEF_NATIVE(pushf, "pushf; pop %eax");
DEF_NATIVE(pushf_cli, "pushf; pop %eax; cli");
DEF_NATIVE(iret, "iret");
DEF_NATIVE(sti_sysexit, "sti; sysexit");

static const struct native_insns
{
	const char *start, *end;
} native_insns[] = {
	[PARAVIRT_IRQ_DISABLE] = { start_cli, end_cli },
	[PARAVIRT_IRQ_ENABLE] = { start_sti, end_sti },
	[PARAVIRT_RESTORE_FLAGS] = { start_popf, end_popf },
	[PARAVIRT_SAVE_FLAGS] = { start_pushf, end_pushf },
	[PARAVIRT_SAVE_FLAGS_IRQ_DISABLE] = { start_pushf_cli, end_pushf_cli },
	[PARAVIRT_INTERRUPT_RETURN] = { start_iret, end_iret },
	[PARAVIRT_STI_SYSEXIT] = { start_sti_sysexit, end_sti_sysexit },
};

static unsigned native_patch(u8 type, u16 clobbers, void *insns, unsigned len)
{
	unsigned int insn_len;

	/* Don't touch it if we don't have a replacement */
	if (type >= ARRAY_SIZE(native_insns) || !native_insns[type].start)
		return len;

	insn_len = native_insns[type].end - native_insns[type].start;

	/* Similarly if we can't fit replacement. */
	if (len < insn_len)
		return len;

	memcpy(insns, native_insns[type].start, insn_len);
	return insn_len;
}

void init_IRQ(void)
{
	paravirt_ops.init_IRQ();
}

static void native_flush_tlb(void)
{
	__native_flush_tlb();
}

/*
 * Global pages have to be flushed a bit differently. Not a real
 * performance problem because this does not happen often.
 */
static void native_flush_tlb_global(void)
{
	__native_flush_tlb_global();
}

static void native_flush_tlb_single(u32 addr)
{
	__native_flush_tlb_single(addr);
}

/* These are in entry.S */
extern void native_iret(void);
extern void native_irq_enable_sysexit(void);

static int __init print_banner(void)
{
	paravirt_ops.banner();
	return 0;
}
core_initcall(print_banner);

struct paravirt_ops paravirt_ops = {
	.name = "bare hardware",
	.paravirt_enabled = 0,
	.kernel_rpl = 0,
	.shared_kernel_pmd = 1,	/* Only used when CONFIG_X86_PAE is set */

 	.patch = native_patch,
	.banner = default_banner,
	.arch_setup = paravirt_nop,
	.memory_setup = machine_specific_memory_setup,
	.get_wallclock = native_get_wallclock,
	.set_wallclock = native_set_wallclock,
	.time_init = hpet_time_init,
	.init_IRQ = native_init_IRQ,

	.cpuid = native_cpuid,
	.get_debugreg = native_get_debugreg,
	.set_debugreg = native_set_debugreg,
	.clts = native_clts,
	.read_cr0 = native_read_cr0,
	.write_cr0 = native_write_cr0,
	.read_cr2 = native_read_cr2,
	.write_cr2 = native_write_cr2,
	.read_cr3 = native_read_cr3,
	.write_cr3 = native_write_cr3,
	.read_cr4 = native_read_cr4,
	.read_cr4_safe = native_read_cr4_safe,
	.write_cr4 = native_write_cr4,
	.save_fl = native_save_fl,
	.restore_fl = native_restore_fl,
	.irq_disable = native_irq_disable,
	.irq_enable = native_irq_enable,
	.safe_halt = native_safe_halt,
	.halt = native_halt,
	.wbinvd = native_wbinvd,
	.read_msr = native_read_msr_safe,
	.write_msr = native_write_msr_safe,
	.read_tsc = native_read_tsc,
	.read_pmc = native_read_pmc,
	.get_scheduled_cycles = native_read_tsc,
	.get_cpu_khz = native_calculate_cpu_khz,
	.load_tr_desc = native_load_tr_desc,
	.set_ldt = native_set_ldt,
	.load_gdt = native_load_gdt,
	.load_idt = native_load_idt,
	.store_gdt = native_store_gdt,
	.store_idt = native_store_idt,
	.store_tr = native_store_tr,
	.load_tls = native_load_tls,
	.write_ldt_entry = write_dt_entry,
	.write_gdt_entry = write_dt_entry,
	.write_idt_entry = write_dt_entry,
	.load_esp0 = native_load_esp0,

	.set_iopl_mask = native_set_iopl_mask,
	.io_delay = native_io_delay,

#ifdef CONFIG_X86_LOCAL_APIC
	.apic_write = native_apic_write,
	.apic_write_atomic = native_apic_write_atomic,
	.apic_read = native_apic_read,
	.setup_boot_clock = setup_boot_APIC_clock,
	.setup_secondary_clock = setup_secondary_APIC_clock,
#endif
	.set_lazy_mode = paravirt_nop,

	.pagetable_setup_start = native_pagetable_setup_start,
	.pagetable_setup_done = native_pagetable_setup_done,

	.flush_tlb_user = native_flush_tlb,
	.flush_tlb_kernel = native_flush_tlb_global,
	.flush_tlb_single = native_flush_tlb_single,

	.map_pt_hook = paravirt_nop,

	.alloc_pt = paravirt_nop,
	.alloc_pd = paravirt_nop,
	.alloc_pd_clone = paravirt_nop,
	.release_pt = paravirt_nop,
	.release_pd = paravirt_nop,

	.set_pte = native_set_pte,
	.set_pte_at = native_set_pte_at,
	.set_pmd = native_set_pmd,
	.pte_update = paravirt_nop,
	.pte_update_defer = paravirt_nop,

	.ptep_get_and_clear = native_ptep_get_and_clear,

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = native_set_pte_atomic,
	.set_pte_present = native_set_pte_present,
	.set_pud = native_set_pud,
	.pte_clear = native_pte_clear,
	.pmd_clear = native_pmd_clear,

	.pmd_val = native_pmd_val,
	.make_pmd = native_make_pmd,
#endif

	.pte_val = native_pte_val,
	.pgd_val = native_pgd_val,

	.make_pte = native_make_pte,
	.make_pgd = native_make_pgd,

	.irq_enable_sysexit = native_irq_enable_sysexit,
	.iret = native_iret,

	.startup_ipi_hook = paravirt_nop,
};

/*
 * NOTE: CONFIG_PARAVIRT is experimental and the paravirt_ops
 * semantics are subject to change. Hence we only do this
 * internal-only export of this, until it gets sorted out and
 * all lowlevel CPU ops used by modules are separately exported.
 */
EXPORT_SYMBOL_GPL(paravirt_ops);
