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
#include <linux/highmem.h>

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

DEF_NATIVE(irq_disable, "cli");
DEF_NATIVE(irq_enable, "sti");
DEF_NATIVE(restore_fl, "push %eax; popf");
DEF_NATIVE(save_fl, "pushf; pop %eax");
DEF_NATIVE(iret, "iret");
DEF_NATIVE(irq_enable_sysexit, "sti; sysexit");
DEF_NATIVE(read_cr2, "mov %cr2, %eax");
DEF_NATIVE(write_cr3, "mov %eax, %cr3");
DEF_NATIVE(read_cr3, "mov %cr3, %eax");
DEF_NATIVE(clts, "clts");
DEF_NATIVE(read_tsc, "rdtsc");

DEF_NATIVE(ud2a, "ud2a");

static unsigned native_patch(u8 type, u16 clobbers, void *insns, unsigned len)
{
	const unsigned char *start, *end;
	unsigned ret;

	switch(type) {
#define SITE(x)	case PARAVIRT_PATCH(x):	start = start_##x; end = end_##x; goto patch_site
		SITE(irq_disable);
		SITE(irq_enable);
		SITE(restore_fl);
		SITE(save_fl);
		SITE(iret);
		SITE(irq_enable_sysexit);
		SITE(read_cr2);
		SITE(read_cr3);
		SITE(write_cr3);
		SITE(clts);
		SITE(read_tsc);
#undef SITE

	patch_site:
		ret = paravirt_patch_insns(insns, len, start, end);
		break;

	case PARAVIRT_PATCH(make_pgd):
	case PARAVIRT_PATCH(make_pte):
	case PARAVIRT_PATCH(pgd_val):
	case PARAVIRT_PATCH(pte_val):
#ifdef CONFIG_X86_PAE
	case PARAVIRT_PATCH(make_pmd):
	case PARAVIRT_PATCH(pmd_val):
#endif
		/* These functions end up returning exactly what
		   they're passed, in the same registers. */
		ret = paravirt_patch_nop();
		break;

	default:
		ret = paravirt_patch_default(type, clobbers, insns, len);
		break;
	}

	return ret;
}

unsigned paravirt_patch_nop(void)
{
	return 0;
}

unsigned paravirt_patch_ignore(unsigned len)
{
	return len;
}

unsigned paravirt_patch_call(void *target, u16 tgt_clobbers,
			     void *site, u16 site_clobbers,
			     unsigned len)
{
	unsigned char *call = site;
	unsigned long delta = (unsigned long)target - (unsigned long)(call+5);

	if (tgt_clobbers & ~site_clobbers)
		return len;	/* target would clobber too much for this site */
	if (len < 5)
		return len;	/* call too long for patch site */

	*call++ = 0xe8;		/* call */
	*(unsigned long *)call = delta;

	return 5;
}

unsigned paravirt_patch_jmp(void *target, void *site, unsigned len)
{
	unsigned char *jmp = site;
	unsigned long delta = (unsigned long)target - (unsigned long)(jmp+5);

	if (len < 5)
		return len;	/* call too long for patch site */

	*jmp++ = 0xe9;		/* jmp */
	*(unsigned long *)jmp = delta;

	return 5;
}

unsigned paravirt_patch_default(u8 type, u16 clobbers, void *site, unsigned len)
{
	void *opfunc = *((void **)&paravirt_ops + type);
	unsigned ret;

	if (opfunc == NULL)
		/* If there's no function, patch it with a ud2a (BUG) */
		ret = paravirt_patch_insns(site, len, start_ud2a, end_ud2a);
	else if (opfunc == paravirt_nop)
		/* If the operation is a nop, then nop the callsite */
		ret = paravirt_patch_nop();
	else if (type == PARAVIRT_PATCH(iret) ||
		 type == PARAVIRT_PATCH(irq_enable_sysexit))
		/* If operation requires a jmp, then jmp */
		ret = paravirt_patch_jmp(opfunc, site, len);
	else
		/* Otherwise call the function; assume target could
		   clobber any caller-save reg */
		ret = paravirt_patch_call(opfunc, CLBR_ANY,
					  site, clobbers, len);

	return ret;
}

unsigned paravirt_patch_insns(void *site, unsigned len,
			      const char *start, const char *end)
{
	unsigned insn_len = end - start;

	if (insn_len > len || start == NULL)
		insn_len = len;
	else
		memcpy(site, start, insn_len);

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

static void native_flush_tlb_single(unsigned long addr)
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

static struct resource reserve_ioports = {
	.start = 0,
	.end = IO_SPACE_LIMIT,
	.name = "paravirt-ioport",
	.flags = IORESOURCE_IO | IORESOURCE_BUSY,
};

static struct resource reserve_iomem = {
	.start = 0,
	.end = -1,
	.name = "paravirt-iomem",
	.flags = IORESOURCE_MEM | IORESOURCE_BUSY,
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
	int ret;

	ret = request_resource(&ioport_resource, &reserve_ioports);
	if (ret == 0) {
		ret = request_resource(&iomem_resource, &reserve_iomem);
		if (ret)
			release_resource(&reserve_ioports);
	}

	return ret;
}

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
	.sched_clock = native_sched_clock,
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
	.startup_ipi_hook = paravirt_nop,
#endif
	.set_lazy_mode = paravirt_nop,

	.pagetable_setup_start = native_pagetable_setup_start,
	.pagetable_setup_done = native_pagetable_setup_done,

	.flush_tlb_user = native_flush_tlb,
	.flush_tlb_kernel = native_flush_tlb_global,
	.flush_tlb_single = native_flush_tlb_single,
	.flush_tlb_others = native_flush_tlb_others,

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

#ifdef CONFIG_HIGHPTE
	.kmap_atomic_pte = kmap_atomic,
#endif

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

	.dup_mmap = paravirt_nop,
	.exit_mmap = paravirt_nop,
	.activate_mm = paravirt_nop,
};

EXPORT_SYMBOL(paravirt_ops);
