/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * based on machine_kexec.c from other architectures in linux-2.6.18
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <linux/highmem.h>
#include <linux/mmu_context.h>
#include <linux/io.h>
#include <linux/timex.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <hv/hypervisor.h>


/*
 * This stuff is not in elf.h and is not in any other kernel include.
 * This stuff is needed below in the little boot notes parser to
 * extract the command line so we can pass it to the hypervisor.
 */
struct Elf32_Bhdr {
	Elf32_Word b_signature;
	Elf32_Word b_size;
	Elf32_Half b_checksum;
	Elf32_Half b_records;
};
#define ELF_BOOT_MAGIC		0x0E1FB007
#define EBN_COMMAND_LINE	0x00000004
#define roundupsz(X) (((X) + 3) & ~3)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


void machine_shutdown(void)
{
	/*
	 * Normally we would stop all the other processors here, but
	 * the check in machine_kexec_prepare below ensures we'll only
	 * get this far if we've been booted with "nosmp" on the
	 * command line or without CONFIG_SMP so there's nothing to do
	 * here (for now).
	 */
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	/*
	 * Cannot happen.  This type of kexec is disabled on this
	 * architecture (and enforced in machine_kexec_prepare below).
	 */
}


int machine_kexec_prepare(struct kimage *image)
{
	if (num_online_cpus() > 1) {
		pr_warning("%s: detected attempt to kexec "
		       "with num_online_cpus() > 1\n",
		       __func__);
		return -ENOSYS;
	}
	if (image->type != KEXEC_TYPE_DEFAULT) {
		pr_warning("%s: detected attempt to kexec "
		       "with unsupported type: %d\n",
		       __func__,
		       image->type);
		return -ENOSYS;
	}
	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
	/*
	 * We did nothing in machine_kexec_prepare,
	 * so we have nothing to do here.
	 */
}

/*
 * If we can find elf boot notes on this page, return the command
 * line.  Otherwise, silently return null.  Somewhat kludgy, but no
 * good way to do this without significantly rearchitecting the
 * architecture-independent kexec code.
 */

static unsigned char *kexec_bn2cl(void *pg)
{
	struct Elf32_Bhdr *bhdrp;
	Elf32_Nhdr *nhdrp;
	unsigned char *desc;
	unsigned char *command_line;
	__sum16 csum;

	bhdrp = (struct Elf32_Bhdr *) pg;

	/*
	 * This routine is invoked for every source page, so make
	 * sure to quietly ignore every impossible page.
	 */
	if (bhdrp->b_signature != ELF_BOOT_MAGIC ||
	    bhdrp->b_size > PAGE_SIZE)
		return 0;

	/*
	 * If we get a checksum mismatch, warn with the checksum
	 * so we can diagnose better.
	 */
	csum = ip_compute_csum(pg, bhdrp->b_size);
	if (csum != 0) {
		pr_warning("%s: bad checksum %#x (size %d)\n",
			   __func__, csum, bhdrp->b_size);
		return 0;
	}

	nhdrp = (Elf32_Nhdr *) (bhdrp + 1);

	while (nhdrp->n_type != EBN_COMMAND_LINE) {

		desc = (unsigned char *) (nhdrp + 1);
		desc += roundupsz(nhdrp->n_descsz);

		nhdrp = (Elf32_Nhdr *) desc;

		/* still in bounds? */
		if ((unsigned char *) (nhdrp + 1) >
		    ((unsigned char *) pg) + bhdrp->b_size) {

			pr_info("%s: out of bounds\n", __func__);
			return 0;
		}
	}

	command_line = (unsigned char *) (nhdrp + 1);
	desc = command_line;

	while (*desc != '\0') {
		desc++;
		if (((unsigned long)desc & PAGE_MASK) != (unsigned long)pg) {
			pr_info("%s: ran off end of page\n",
			       __func__);
			return 0;
		}
	}

	return command_line;
}

static void kexec_find_and_set_command_line(struct kimage *image)
{
	kimage_entry_t *ptr, entry;

	unsigned char *command_line = 0;
	unsigned char *r;
	HV_Errno hverr;

	for (ptr = &image->head;
	     (entry = *ptr) && !(entry & IND_DONE);
	     ptr = (entry & IND_INDIRECTION) ?
		     phys_to_virt((entry & PAGE_MASK)) : ptr + 1) {

		if ((entry & IND_SOURCE)) {
			void *va =
				kmap_atomic_pfn(entry >> PAGE_SHIFT);
			r = kexec_bn2cl(va);
			if (r) {
				command_line = r;
				break;
			}
			kunmap_atomic(va);
		}
	}

	if (command_line != 0) {
		pr_info("setting new command line to \"%s\"\n",
		       command_line);

		hverr = hv_set_command_line(
			(HV_VirtAddr) command_line, strlen(command_line));
		kunmap_atomic(command_line);
	} else {
		pr_info("%s: no command line found; making empty\n",
		       __func__);
		hverr = hv_set_command_line((HV_VirtAddr) command_line, 0);
	}
	if (hverr)
		pr_warning("%s: hv_set_command_line returned error: %d\n",
			   __func__, hverr);
}

/*
 * The kexec code range-checks all its PAs, so to avoid having it run
 * amok and allocate memory and then sequester it from every other
 * controller, we force it to come from controller zero.  We also
 * disable the oom-killer since if we do end up running out of memory,
 * that almost certainly won't help.
 */
struct page *kimage_alloc_pages_arch(gfp_t gfp_mask, unsigned int order)
{
	gfp_mask |= __GFP_THISNODE | __GFP_NORETRY;
	return alloc_pages_node(0, gfp_mask, order);
}

static void setup_quasi_va_is_pa(void)
{
	HV_PTE *pgtable;
	HV_PTE pte;
	int i;

	/*
	 * Flush our TLB to prevent conflicts between the previous contents
	 * and the new stuff we're about to add.
	 */
	local_flush_tlb_all();

	/* setup VA is PA, at least up to PAGE_OFFSET */

	pgtable = (HV_PTE *)current->mm->pgd;
	pte = hv_pte(_PAGE_KERNEL | _PAGE_HUGE_PAGE);
	pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);

	for (i = 0; i < pgd_index(PAGE_OFFSET); i++) {
		unsigned long pfn = i << (HPAGE_SHIFT - PAGE_SHIFT);
		if (pfn_valid(pfn))
			__set_pte(&pgtable[i], pfn_pte(pfn, pte));
	}
}


NORET_TYPE void machine_kexec(struct kimage *image)
{
	void *reboot_code_buffer;
	NORET_TYPE void (*rnk)(unsigned long, void *, unsigned long)
		ATTRIB_NORET;

	/* Mask all interrupts before starting to reboot. */
	interrupt_mask_set_mask(~0ULL);

	kexec_find_and_set_command_line(image);

	/*
	 * Adjust the home caching of the control page to be cached on
	 * this cpu, and copy the assembly helper into the control
	 * code page, which we map in the vmalloc area.
	 */
	homecache_change_page_home(image->control_code_page, 0,
				   smp_processor_id());
	reboot_code_buffer = vmap(&image->control_code_page, 1, 0,
				  __pgprot(_PAGE_KERNEL | _PAGE_EXECUTABLE));
	memcpy(reboot_code_buffer, relocate_new_kernel,
	       relocate_new_kernel_size);
	__flush_icache_range(
		(unsigned long) reboot_code_buffer,
		(unsigned long) reboot_code_buffer + relocate_new_kernel_size);

	setup_quasi_va_is_pa();

	/* now call it */
	rnk = reboot_code_buffer;
	(*rnk)(image->head, reboot_code_buffer, image->start);
}
