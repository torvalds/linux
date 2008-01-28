/*
 * The SH64 TLB miss.
 *
 * Original code from fault.c
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * Fast PTE->TLB refill path
 * Copyright (C) 2003 Richard.Curnow@superh.com
 *
 * IMPORTANT NOTES :
 * The do_fast_page_fault function is called from a context in entry.S
 * where very few registers have been saved.  In particular, the code in
 * this file must be compiled not to use ANY caller-save registers that
 * are not part of the restricted save set.  Also, it means that code in
 * this file must not make calls to functions elsewhere in the kernel, or
 * else the excepting context will see corruption in its caller-save
 * registers.  Plus, the entry.S save area is non-reentrant, so this code
 * has to run with SR.BL==1, i.e. no interrupts taken inside it and panic
 * on any exception.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/tlb.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cpu/registers.h>

/* Callable from fault.c, so not static */
inline void __do_tlb_refill(unsigned long address,
                            unsigned long long is_text_not_data, pte_t *pte)
{
	unsigned long long ptel;
	unsigned long long pteh=0;
	struct tlb_info *tlbp;
	unsigned long long next;

	/* Get PTEL first */
	ptel = pte_val(*pte);

	/*
	 * Set PTEH register
	 */
	pteh = address & MMU_VPN_MASK;

	/* Sign extend based on neff. */
#if (NEFF == 32)
	/* Faster sign extension */
	pteh = (unsigned long long)(signed long long)(signed long)pteh;
#else
	/* General case */
	pteh = (pteh & NEFF_SIGN) ? (pteh | NEFF_MASK) : pteh;
#endif

	/* Set the ASID. */
	pteh |= get_asid() << PTEH_ASID_SHIFT;
	pteh |= PTEH_VALID;

	/* Set PTEL register, set_pte has performed the sign extension */
	ptel &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */

	tlbp = is_text_not_data ? &(cpu_data->itlb) : &(cpu_data->dtlb);
	next = tlbp->next;
	__flush_tlb_slot(next);
	asm volatile ("putcfg %0,1,%2\n\n\t"
		      "putcfg %0,0,%1\n"
		      :  : "r" (next), "r" (pteh), "r" (ptel) );

	next += TLB_STEP;
	if (next > tlbp->last) next = tlbp->first;
	tlbp->next = next;

}

static int handle_vmalloc_fault(struct mm_struct *mm,
				unsigned long protection_flags,
                                unsigned long long textaccess,
				unsigned long address)
{
	pgd_t *dir;
	pud_t *pud;
	pmd_t *pmd;
	static pte_t *pte;
	pte_t entry;

	dir = pgd_offset_k(address);

	pud = pud_offset(dir, address);
	if (pud_none_or_clear_bad(pud))
		return 0;

	pmd = pmd_offset(pud, address);
	if (pmd_none_or_clear_bad(pmd))
		return 0;

	pte = pte_offset_kernel(pmd, address);
	entry = *pte;

	if (pte_none(entry) || !pte_present(entry))
		return 0;
	if ((pte_val(entry) & protection_flags) != protection_flags)
		return 0;

        __do_tlb_refill(address, textaccess, pte);

	return 1;
}

static int handle_tlbmiss(struct mm_struct *mm,
			  unsigned long long protection_flags,
			  unsigned long long textaccess,
			  unsigned long address)
{
	pgd_t *dir;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	/* NB. The PGD currently only contains a single entry - there is no
	   page table tree stored for the top half of the address space since
	   virtual pages in that region should never be mapped in user mode.
	   (In kernel mode, the only things in that region are the 512Mb super
	   page (locked in), and vmalloc (modules) +  I/O device pages (handled
	   by handle_vmalloc_fault), so no PGD for the upper half is required
	   by kernel mode either).

	   See how mm->pgd is allocated and initialised in pgd_alloc to see why
	   the next test is necessary.  - RPC */
	if (address >= (unsigned long) TASK_SIZE)
		/* upper half - never has page table entries. */
		return 0;

	dir = pgd_offset(mm, address);
	if (pgd_none(*dir) || !pgd_present(*dir))
		return 0;
	if (!pgd_present(*dir))
		return 0;

	pud = pud_offset(dir, address);
	if (pud_none(*pud) || !pud_present(*pud))
		return 0;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || !pmd_present(*pmd))
		return 0;

	pte = pte_offset_kernel(pmd, address);
	entry = *pte;

	if (pte_none(entry) || !pte_present(entry))
		return 0;

	/*
	 * If the page doesn't have sufficient protection bits set to
	 * service the kind of fault being handled, there's not much
	 * point doing the TLB refill.  Punt the fault to the general
	 * handler.
	 */
	if ((pte_val(entry) & protection_flags) != protection_flags)
		return 0;

        __do_tlb_refill(address, textaccess, pte);

	return 1;
}

/*
 * Put all this information into one structure so that everything is just
 * arithmetic relative to a single base address.  This reduces the number
 * of movi/shori pairs needed just to load addresses of static data.
 */
struct expevt_lookup {
	unsigned short protection_flags[8];
	unsigned char  is_text_access[8];
	unsigned char  is_write_access[8];
};

#define PRU (1<<9)
#define PRW (1<<8)
#define PRX (1<<7)
#define PRR (1<<6)

#define DIRTY (_PAGE_DIRTY | _PAGE_ACCESSED)
#define YOUNG (_PAGE_ACCESSED)

/* Sized as 8 rather than 4 to allow checking the PTE's PRU bit against whether
   the fault happened in user mode or privileged mode. */
static struct expevt_lookup expevt_lookup_table = {
	.protection_flags = {PRX, PRX, 0, 0, PRR, PRR, PRW, PRW},
	.is_text_access   = {1,   1,   0, 0, 0,   0,   0,   0}
};

/*
   This routine handles page faults that can be serviced just by refilling a
   TLB entry from an existing page table entry.  (This case represents a very
   large majority of page faults.) Return 1 if the fault was successfully
   handled.  Return 0 if the fault could not be handled.  (This leads into the
   general fault handling in fault.c which deals with mapping file-backed
   pages, stack growth, segmentation faults, swapping etc etc)
 */
asmlinkage int do_fast_page_fault(unsigned long long ssr_md,
				  unsigned long long expevt,
			          unsigned long address)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	unsigned long long textaccess;
	unsigned long long protection_flags;
	unsigned long long index;
	unsigned long long expevt4;

	/* The next few lines implement a way of hashing EXPEVT into a
	 * small array index which can be used to lookup parameters
	 * specific to the type of TLBMISS being handled.
	 *
	 * Note:
	 *	ITLBMISS has EXPEVT==0xa40
	 *	RTLBMISS has EXPEVT==0x040
	 *	WTLBMISS has EXPEVT==0x060
	 */
	expevt4 = (expevt >> 4);
	/* TODO : xor ssr_md into this expression too. Then we can check
	 * that PRU is set when it needs to be. */
	index = expevt4 ^ (expevt4 >> 5);
	index &= 7;
	protection_flags = expevt_lookup_table.protection_flags[index];
	textaccess       = expevt_lookup_table.is_text_access[index];

	/* SIM
	 * Note this is now called with interrupts still disabled
	 * This is to cope with being called for a missing IO port
	 * address with interrupts disabled. This should be fixed as
	 * soon as we have a better 'fast path' miss handler.
	 *
	 * Plus take care how you try and debug this stuff.
	 * For example, writing debug data to a port which you
	 * have just faulted on is not going to work.
	 */

	tsk = current;
	mm = tsk->mm;

	if ((address >= VMALLOC_START && address < VMALLOC_END) ||
	    (address >= IOBASE_VADDR  && address < IOBASE_END)) {
		if (ssr_md)
			/*
			 * Process-contexts can never have this address
			 * range mapped
			 */
			if (handle_vmalloc_fault(mm, protection_flags,
						 textaccess, address))
				return 1;
	} else if (!in_interrupt() && mm) {
		if (handle_tlbmiss(mm, protection_flags, textaccess, address))
			return 1;
	}

	return 0;
}
