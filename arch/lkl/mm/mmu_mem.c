// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <asm/host_ops.h>
#include <asm/page.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD];

static const pgprot_t protection_map[16] = {
	[VM_NONE]						= PAGE_NONE,
	[VM_READ]						= PAGE_READONLY,
	[VM_WRITE]						= PAGE_COPY,
	[VM_WRITE | VM_READ]			= PAGE_COPY,
	[VM_EXEC]						= PAGE_READONLY,
	[VM_EXEC | VM_READ]				= PAGE_READONLY,
	[VM_EXEC | VM_WRITE]			= PAGE_COPY,
	[VM_EXEC | VM_WRITE | VM_READ]	= PAGE_COPY,
	[VM_SHARED]						= PAGE_NONE,
	[VM_SHARED | VM_READ]			= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]			= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ] = PAGE_SHARED,
	[VM_SHARED | VM_EXEC]			= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_READ]	 = PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_WRITE] = PAGE_SHARED,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED
};
DECLARE_VM_GET_PAGE_PROT


pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	// There is no user-space & kernel-space virtual memory boundary for LKL.
	if (pgd)
		memcpy(pgd, swapper_pg_dir, sizeof(swapper_pg_dir));

	return pgd;
}

void mmap_pages_for_ptes(unsigned long va, unsigned int nr, pte_t pte)
{
	// TODO: At the moment we mmap memory as RWX. However, we should mmap pages
	// with proper access flags (read-only, read-write, etc)
	enum lkl_prot prot = LKL_PROT_READ | LKL_PROT_WRITE | LKL_PROT_EXEC;
	unsigned long pa = pte.pte & PAGE_MASK;
	unsigned long pg_off = pa - (ARCH_PFN_OFFSET << PAGE_SHIFT);

	void *res = lkl_ops->shmem_mmap((void *)va, pg_off, PAGE_SIZE * nr, prot);

	BUG_ON(res != (void *)va);
}

void munmap_page_for_pte(unsigned long addr, pte_t *xp)
{
	BUG_ON(lkl_ops->munmap((void *)addr, PAGE_SIZE) != 0);
}
