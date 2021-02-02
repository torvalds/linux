// SPDX-License-Identifier: GPL-2.0

#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/mmu.h>

/* Inserts new slb entries */
static void insert_slb_entry(unsigned long p, int ssize, int page_size)
{
	unsigned long flags;

	flags = SLB_VSID_KERNEL | mmu_psize_defs[page_size].sllp;
	preempt_disable();

	asm volatile("slbmte %0,%1" :
		     : "r" (mk_vsid_data(p, ssize, flags)),
		       "r" (mk_esid_data(p, ssize, SLB_NUM_BOLTED))
		     : "memory");

	asm volatile("slbmte %0,%1" :
			: "r" (mk_vsid_data(p, ssize, flags)),
			  "r" (mk_esid_data(p, ssize, SLB_NUM_BOLTED + 1))
			: "memory");
	preempt_enable();
}

/* Inject slb multihit on vmalloc-ed address i.e 0xD00... */
static int inject_vmalloc_slb_multihit(void)
{
	char *p;

	p = vmalloc(PAGE_SIZE);
	if (!p)
		return -ENOMEM;

	insert_slb_entry((unsigned long)p, MMU_SEGSIZE_1T, mmu_vmalloc_psize);
	/*
	 * This triggers exception, If handled correctly we must recover
	 * from this error.
	 */
	p[0] = '!';
	vfree(p);
	return 0;
}

/* Inject slb multihit on kmalloc-ed address i.e 0xC00... */
static int inject_kmalloc_slb_multihit(void)
{
	char *p;

	p = kmalloc(2048, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	insert_slb_entry((unsigned long)p, MMU_SEGSIZE_1T, mmu_linear_psize);
	/*
	 * This triggers exception, If handled correctly we must recover
	 * from this error.
	 */
	p[0] = '!';
	kfree(p);
	return 0;
}

/*
 * Few initial SLB entries are bolted. Add a test to inject
 * multihit in bolted entry 0.
 */
static void insert_dup_slb_entry_0(void)
{
	unsigned long test_address = PAGE_OFFSET, *test_ptr;
	unsigned long esid, vsid;
	unsigned long i = 0;

	test_ptr = (unsigned long *)test_address;
	preempt_disable();

	asm volatile("slbmfee  %0,%1" : "=r" (esid) : "r" (i));
	asm volatile("slbmfev  %0,%1" : "=r" (vsid) : "r" (i));

	/* for i !=0 we would need to mask out the old entry number */
	asm volatile("slbmte %0,%1" :
			: "r" (vsid),
			  "r" (esid | SLB_NUM_BOLTED)
			: "memory");

	asm volatile("slbmfee  %0,%1" : "=r" (esid) : "r" (i));
	asm volatile("slbmfev  %0,%1" : "=r" (vsid) : "r" (i));

	/* for i !=0 we would need to mask out the old entry number */
	asm volatile("slbmte %0,%1" :
			: "r" (vsid),
			  "r" (esid | (SLB_NUM_BOLTED + 1))
			: "memory");

	pr_info("%s accessing test address 0x%lx: 0x%lx\n",
		__func__, test_address, *test_ptr);

	preempt_enable();
}

void lkdtm_PPC_SLB_MULTIHIT(void)
{
	if (!radix_enabled()) {
		pr_info("Injecting SLB multihit errors\n");
		/*
		 * These need not be separate tests, And they do pretty
		 * much same thing. In any case we must recover from the
		 * errors introduced by these functions, machine would not
		 * survive these tests in case of failure to handle.
		 */
		inject_vmalloc_slb_multihit();
		inject_kmalloc_slb_multihit();
		insert_dup_slb_entry_0();
		pr_info("Recovered from SLB multihit errors\n");
	} else {
		pr_err("XFAIL: This test is for ppc64 and with hash mode MMU only\n");
	}
}
