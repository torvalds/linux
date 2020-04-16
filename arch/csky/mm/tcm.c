// SPDX-License-Identifier: GPL-2.0

#include <linux/highmem.h>
#include <linux/genalloc.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>

#if (CONFIG_ITCM_RAM_BASE == 0xffffffff)
#error "You should define ITCM_RAM_BASE"
#endif

#ifdef CONFIG_HAVE_DTCM
#if (CONFIG_DTCM_RAM_BASE == 0xffffffff)
#error "You should define DTCM_RAM_BASE"
#endif

#if (CONFIG_DTCM_RAM_BASE == CONFIG_ITCM_RAM_BASE)
#error "You should define correct DTCM_RAM_BASE"
#endif
#endif

extern char __tcm_start, __tcm_end, __dtcm_start;

static struct gen_pool *tcm_pool;

static void __init tcm_mapping_init(void)
{
	pte_t *tcm_pte;
	unsigned long vaddr, paddr;
	int i;

	paddr = CONFIG_ITCM_RAM_BASE;

	if (pfn_valid(PFN_DOWN(CONFIG_ITCM_RAM_BASE)))
		goto panic;

#ifndef CONFIG_HAVE_DTCM
	for (i = 0; i < TCM_NR_PAGES; i++) {
#else
	for (i = 0; i < CONFIG_ITCM_NR_PAGES; i++) {
#endif
		vaddr = __fix_to_virt(FIX_TCM - i);

		tcm_pte =
			pte_offset_kernel((pmd_t *)pgd_offset_k(vaddr), vaddr);

		set_pte(tcm_pte, pfn_pte(__phys_to_pfn(paddr), PAGE_KERNEL));

		flush_tlb_one(vaddr);

		paddr = paddr + PAGE_SIZE;
	}

#ifdef CONFIG_HAVE_DTCM
	if (pfn_valid(PFN_DOWN(CONFIG_DTCM_RAM_BASE)))
		goto panic;

	paddr = CONFIG_DTCM_RAM_BASE;

	for (i = 0; i < CONFIG_DTCM_NR_PAGES; i++) {
		vaddr = __fix_to_virt(FIX_TCM - CONFIG_ITCM_NR_PAGES - i);

		tcm_pte =
			pte_offset_kernel((pmd_t *) pgd_offset_k(vaddr), vaddr);

		set_pte(tcm_pte, pfn_pte(__phys_to_pfn(paddr), PAGE_KERNEL));

		flush_tlb_one(vaddr);

		paddr = paddr + PAGE_SIZE;
	}
#endif

#ifndef CONFIG_HAVE_DTCM
	memcpy((void *)__fix_to_virt(FIX_TCM),
				&__tcm_start, &__tcm_end - &__tcm_start);

	pr_info("%s: mapping tcm va:0x%08lx to pa:0x%08x\n",
			__func__, __fix_to_virt(FIX_TCM), CONFIG_ITCM_RAM_BASE);

	pr_info("%s: __tcm_start va:0x%08lx size:%d\n",
			__func__, (unsigned long)&__tcm_start, &__tcm_end - &__tcm_start);
#else
	memcpy((void *)__fix_to_virt(FIX_TCM),
				&__tcm_start, &__dtcm_start - &__tcm_start);

	pr_info("%s: mapping itcm va:0x%08lx to pa:0x%08x\n",
			__func__, __fix_to_virt(FIX_TCM), CONFIG_ITCM_RAM_BASE);

	pr_info("%s: __itcm_start va:0x%08lx size:%d\n",
			__func__, (unsigned long)&__tcm_start, &__dtcm_start - &__tcm_start);

	memcpy((void *)__fix_to_virt(FIX_TCM - CONFIG_ITCM_NR_PAGES),
				&__dtcm_start, &__tcm_end - &__dtcm_start);

	pr_info("%s: mapping dtcm va:0x%08lx to pa:0x%08x\n",
			__func__, __fix_to_virt(FIX_TCM - CONFIG_ITCM_NR_PAGES),
						CONFIG_DTCM_RAM_BASE);

	pr_info("%s: __dtcm_start va:0x%08lx size:%d\n",
			__func__, (unsigned long)&__dtcm_start, &__tcm_end - &__dtcm_start);

#endif
	return;
panic:
	panic("TCM init error");
}

void *tcm_alloc(size_t len)
{
	unsigned long vaddr;

	if (!tcm_pool)
		return NULL;

	vaddr = gen_pool_alloc(tcm_pool, len);
	if (!vaddr)
		return NULL;

	return (void *) vaddr;
}
EXPORT_SYMBOL(tcm_alloc);

void tcm_free(void *addr, size_t len)
{
	gen_pool_free(tcm_pool, (unsigned long) addr, len);
}
EXPORT_SYMBOL(tcm_free);

static int __init tcm_setup_pool(void)
{
#ifndef CONFIG_HAVE_DTCM
	u32 pool_size = (u32) (TCM_NR_PAGES * PAGE_SIZE)
				- (u32) (&__tcm_end - &__tcm_start);

	u32 tcm_pool_start = __fix_to_virt(FIX_TCM)
				+ (u32) (&__tcm_end - &__tcm_start);
#else
	u32 pool_size = (u32) (CONFIG_DTCM_NR_PAGES * PAGE_SIZE)
				- (u32) (&__tcm_end - &__dtcm_start);

	u32 tcm_pool_start = __fix_to_virt(FIX_TCM - CONFIG_ITCM_NR_PAGES)
				+ (u32) (&__tcm_end - &__dtcm_start);
#endif
	int ret;

	tcm_pool = gen_pool_create(2, -1);

	ret = gen_pool_add(tcm_pool, tcm_pool_start, pool_size, -1);
	if (ret) {
		pr_err("%s: gen_pool add failed!\n", __func__);
		return ret;
	}

	pr_info("%s: Added %d bytes @ 0x%08x to memory pool\n",
		__func__, pool_size, tcm_pool_start);

	return 0;
}

static int __init tcm_init(void)
{
	tcm_mapping_init();

	tcm_setup_pool();

	return 0;
}
arch_initcall(tcm_init);
