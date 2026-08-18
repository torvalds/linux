// SPDX-License-Identifier: GPL-2.0

#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_64S_HASH_MMU
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
	isync();

	asm volatile("slbmte %0,%1" :
			: "r" (mk_vsid_data(p, ssize, flags)),
			  "r" (mk_esid_data(p, ssize, SLB_NUM_BOLTED + 1))
			: "memory");
	isync();

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
	isync();

	asm volatile("slbmfee  %0,%1" : "=r" (esid) : "r" (i));
	asm volatile("slbmfev  %0,%1" : "=r" (vsid) : "r" (i));

	/* for i !=0 we would need to mask out the old entry number */
	asm volatile("slbmte %0,%1" :
			: "r" (vsid),
			  "r" (esid | (SLB_NUM_BOLTED + 1))
			: "memory");
	isync();

	pr_info("%s accessing test address 0x%lx: 0x%lx\n",
		__func__, test_address, *test_ptr);

	preempt_enable();
}
#endif /* CONFIG_PPC_64S_HASH_MMU */

static __always_inline void tlbiel_va(unsigned long va,
				      unsigned long pid,
				      unsigned long ap,
				      unsigned long ric)
{
	unsigned long rb, rs, prs, r;

	rb = va & ~(PPC_BITMASK(52, 63));
	rb |= ap << PPC_BITLSHIFT(58);
	rs = pid << PPC_BITLSHIFT(31);

	prs = 1; /* process scoped */
	r = 1;   /* radix format */

	/*
	 * Trigger an MCE by issuing radix tlbiel with an invalid operand combination.
	 * The combination of RIC = 2 with IS = 0 (Invalidation selector specified
	 * in the RB register) is invalid.
	 * This invalid combination causes hardware to raise a machine check.
	 */
	asm volatile(PPC_TLBIEL(%0, %4, %3, %2, %1)
			: : "r"(rb), "i"(r), "i"(prs), "i"(ric), "r"(rs) : "memory");
}

static void lkdtm_PPC_SLB_MULTIHIT(void)
{
#ifdef CONFIG_PPC_64S_HASH_MMU
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
#else
	pr_err("XFAIL: This test requires CONFIG_PPC_64S_HASH_MMU\n");
#endif
}

static void lkdtm_PPC_RADIX_TLBIEL(void)
{
	unsigned long addr = PAGE_OFFSET;

	if (radix_enabled()) {
		pr_info("Injecting Radix TLB invalidation MCE\n");
		tlbiel_va(addr, 0, 0, RIC_FLUSH_ALL);
		pr_info("Recovered from radix tlbiel attempt\n");
	} else {
		pr_err("XFAIL: This test is for ppc64 and with radix mode MMU only\n");
	}
}

static struct crashtype crashtypes[] = {
	CRASHTYPE(PPC_SLB_MULTIHIT),
	CRASHTYPE(PPC_RADIX_TLBIEL),
};

struct crashtype_category powerpc_crashtypes = {
	.crashtypes = crashtypes,
	.len	    = ARRAY_SIZE(crashtypes),
};
