/*
 * IA32 helper functions
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2000 Asit K. Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 06/16/00	A. Mallick	added csd/ssd/tssd for ia32 thread context
 * 02/19/01	D. Mosberger	dropped tssd; it's not needed
 * 09/14/01	D. Mosberger	fixed memory management for gdt/tss page
 * 09/29/01	D. Mosberger	added ia32_load_segment_descriptors()
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/sched.h>

#include <asm/intrinsics.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#include "ia32priv.h"

extern void die_if_kernel (char *str, struct pt_regs *regs, long err);

struct exec_domain ia32_exec_domain;
struct page *ia32_shared_page[NR_CPUS];
unsigned long *ia32_boot_gdt;
unsigned long *cpu_gdt_table[NR_CPUS];
struct page *ia32_gate_page;

static unsigned long
load_desc (u16 selector)
{
	unsigned long *table, limit, index;

	if (!selector)
		return 0;
	if (selector & IA32_SEGSEL_TI) {
		table = (unsigned long *) IA32_LDT_OFFSET;
		limit = IA32_LDT_ENTRIES;
	} else {
		table = cpu_gdt_table[smp_processor_id()];
		limit = IA32_PAGE_SIZE / sizeof(ia32_boot_gdt[0]);
	}
	index = selector >> IA32_SEGSEL_INDEX_SHIFT;
	if (index >= limit)
		return 0;
	return IA32_SEG_UNSCRAMBLE(table[index]);
}

void
ia32_load_segment_descriptors (struct task_struct *task)
{
	struct pt_regs *regs = task_pt_regs(task);

	/* Setup the segment descriptors */
	regs->r24 = load_desc(regs->r16 >> 16);		/* ESD */
	regs->r27 = load_desc(regs->r16 >>  0);		/* DSD */
	regs->r28 = load_desc(regs->r16 >> 32);		/* FSD */
	regs->r29 = load_desc(regs->r16 >> 48);		/* GSD */
	regs->ar_csd = load_desc(regs->r17 >>  0);	/* CSD */
	regs->ar_ssd = load_desc(regs->r17 >> 16);	/* SSD */
}

int
ia32_clone_tls (struct task_struct *child, struct pt_regs *childregs)
{
	struct desc_struct *desc;
	struct ia32_user_desc info;
	int idx;

	if (copy_from_user(&info, (void __user *)(childregs->r14 & 0xffffffff), sizeof(info)))
		return -EFAULT;
	if (LDT_empty(&info))
		return -EINVAL;

	idx = info.entry_number;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = child->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;
	desc->a = LDT_entry_a(&info);
	desc->b = LDT_entry_b(&info);

	/* XXX: can this be done in a cleaner way ? */
	load_TLS(&child->thread, smp_processor_id());
	ia32_load_segment_descriptors(child);
	load_TLS(&current->thread, smp_processor_id());

	return 0;
}

void
ia32_save_state (struct task_struct *t)
{
	t->thread.eflag = ia64_getreg(_IA64_REG_AR_EFLAG);
	t->thread.fsr   = ia64_getreg(_IA64_REG_AR_FSR);
	t->thread.fcr   = ia64_getreg(_IA64_REG_AR_FCR);
	t->thread.fir   = ia64_getreg(_IA64_REG_AR_FIR);
	t->thread.fdr   = ia64_getreg(_IA64_REG_AR_FDR);
	ia64_set_kr(IA64_KR_IO_BASE, t->thread.old_iob);
	ia64_set_kr(IA64_KR_TSSD, t->thread.old_k1);
}

void
ia32_load_state (struct task_struct *t)
{
	unsigned long eflag, fsr, fcr, fir, fdr, tssd;
	struct pt_regs *regs = task_pt_regs(t);

	eflag = t->thread.eflag;
	fsr = t->thread.fsr;
	fcr = t->thread.fcr;
	fir = t->thread.fir;
	fdr = t->thread.fdr;
	tssd = load_desc(_TSS);					/* TSSD */

	ia64_setreg(_IA64_REG_AR_EFLAG, eflag);
	ia64_setreg(_IA64_REG_AR_FSR, fsr);
	ia64_setreg(_IA64_REG_AR_FCR, fcr);
	ia64_setreg(_IA64_REG_AR_FIR, fir);
	ia64_setreg(_IA64_REG_AR_FDR, fdr);
	current->thread.old_iob = ia64_get_kr(IA64_KR_IO_BASE);
	current->thread.old_k1 = ia64_get_kr(IA64_KR_TSSD);
	ia64_set_kr(IA64_KR_IO_BASE, IA32_IOBASE);
	ia64_set_kr(IA64_KR_TSSD, tssd);

	regs->r17 = (_TSS << 48) | (_LDT << 32) | (__u32) regs->r17;
	regs->r30 = load_desc(_LDT);				/* LDTD */
	load_TLS(&t->thread, smp_processor_id());
}

/*
 * Setup IA32 GDT and TSS
 */
void
ia32_gdt_init (void)
{
	int cpu = smp_processor_id();

	ia32_shared_page[cpu] = alloc_page(GFP_KERNEL);
	if (!ia32_shared_page[cpu])
		panic("failed to allocate ia32_shared_page[%d]\n", cpu);

	cpu_gdt_table[cpu] = page_address(ia32_shared_page[cpu]);

	/* Copy from the boot cpu's GDT */
	memcpy(cpu_gdt_table[cpu], ia32_boot_gdt, PAGE_SIZE);
}


/*
 * Setup IA32 GDT and TSS
 */
static void
ia32_boot_gdt_init (void)
{
	unsigned long ldt_size;

	ia32_shared_page[0] = alloc_page(GFP_KERNEL);
	if (!ia32_shared_page[0])
		panic("failed to allocate ia32_shared_page[0]\n");

	ia32_boot_gdt = page_address(ia32_shared_page[0]);
	cpu_gdt_table[0] = ia32_boot_gdt;

	/* CS descriptor in IA-32 (scrambled) format */
	ia32_boot_gdt[__USER_CS >> 3]
		= IA32_SEG_DESCRIPTOR(0, (IA32_GATE_END-1) >> IA32_PAGE_SHIFT,
				      0xb, 1, 3, 1, 1, 1, 1);

	/* DS descriptor in IA-32 (scrambled) format */
	ia32_boot_gdt[__USER_DS >> 3]
		= IA32_SEG_DESCRIPTOR(0, (IA32_GATE_END-1) >> IA32_PAGE_SHIFT,
				      0x3, 1, 3, 1, 1, 1, 1);

	ldt_size = PAGE_ALIGN(IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE);
	ia32_boot_gdt[TSS_ENTRY] = IA32_SEG_DESCRIPTOR(IA32_TSS_OFFSET, 235,
						       0xb, 0, 3, 1, 1, 1, 0);
	ia32_boot_gdt[LDT_ENTRY] = IA32_SEG_DESCRIPTOR(IA32_LDT_OFFSET, ldt_size - 1,
						       0x2, 0, 3, 1, 1, 1, 0);
}

static void
ia32_gate_page_init(void)
{
	unsigned long *sr;

	ia32_gate_page = alloc_page(GFP_KERNEL);
	sr = page_address(ia32_gate_page);
	/* This is popl %eax ; movl $,%eax ; int $0x80 */
	*sr++ = 0xb858 | (__IA32_NR_sigreturn << 16) | (0x80cdUL << 48);

	/* This is movl $,%eax ; int $0x80 */
	*sr = 0xb8 | (__IA32_NR_rt_sigreturn << 8) | (0x80cdUL << 40);
}

void
ia32_mem_init(void)
{
	ia32_boot_gdt_init();
	ia32_gate_page_init();
}

/*
 * Handle bad IA32 interrupt via syscall
 */
void
ia32_bad_interrupt (unsigned long int_num, struct pt_regs *regs)
{
	siginfo_t siginfo;

	die_if_kernel("Bad IA-32 interrupt", regs, int_num);

	siginfo.si_signo = SIGTRAP;
	siginfo.si_errno = int_num;	/* XXX is it OK to abuse si_errno like this? */
	siginfo.si_flags = 0;
	siginfo.si_isr = 0;
	siginfo.si_addr = NULL;
	siginfo.si_imm = 0;
	siginfo.si_code = TRAP_BRKPT;
	force_sig_info(SIGTRAP, &siginfo, current);
}

void
ia32_cpu_init (void)
{
	/* initialize global ia32 state - CR0 and CR4 */
	ia64_setreg(_IA64_REG_AR_CFLAG, (((ulong) IA32_CR4 << 32) | IA32_CR0));
}

static int __init
ia32_init (void)
{
	ia32_exec_domain.name = "Linux/x86";
	ia32_exec_domain.handler = NULL;
	ia32_exec_domain.pers_low = PER_LINUX32;
	ia32_exec_domain.pers_high = PER_LINUX32;
	ia32_exec_domain.signal_map = default_exec_domain.signal_map;
	ia32_exec_domain.signal_invmap = default_exec_domain.signal_invmap;
	register_exec_domain(&ia32_exec_domain);

#if PAGE_SHIFT > IA32_PAGE_SHIFT
	{
		extern struct kmem_cache *ia64_partial_page_cachep;

		ia64_partial_page_cachep = kmem_cache_create("ia64_partial_page_cache",
					sizeof(struct ia64_partial_page),
					0, SLAB_PANIC, NULL);
	}
#endif
	return 0;
}

__initcall(ia32_init);
