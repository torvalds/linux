
static void ident_pmd_init(unsigned long pmd_flag, pmd_t *pmd_page,
			   unsigned long addr, unsigned long end)
{
	addr &= PMD_MASK;
	for (; addr < end; addr += PMD_SIZE) {
		pmd_t *pmd = pmd_page + pmd_index(addr);

		if (!pmd_present(*pmd))
			set_pmd(pmd, __pmd(addr | pmd_flag));
	}
}
static int ident_pud_init(struct x86_mapping_info *info, pud_t *pud_page,
			  unsigned long addr, unsigned long end)
{
	unsigned long next;

	for (; addr < end; addr = next) {
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;

		next = (addr & PUD_MASK) + PUD_SIZE;
		if (next > end)
			next = end;

		if (pud_present(*pud)) {
			pmd = pmd_offset(pud, 0);
			ident_pmd_init(info->pmd_flag, pmd, addr, next);
			continue;
		}
		pmd = (pmd_t *)info->alloc_pgt_page(info->context);
		if (!pmd)
			return -ENOMEM;
		ident_pmd_init(info->pmd_flag, pmd, addr, next);
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
	}

	return 0;
}

int kernel_ident_mapping_init(struct x86_mapping_info *info, pgd_t *pgd_page,
			      unsigned long addr, unsigned long end)
{
	unsigned long next;
	int result;
	int off = info->kernel_mapping ? pgd_index(__PAGE_OFFSET) : 0;

	for (; addr < end; addr = next) {
		pgd_t *pgd = pgd_page + pgd_index(addr) + off;
		pud_t *pud;

		next = (addr & PGDIR_MASK) + PGDIR_SIZE;
		if (next > end)
			next = end;

		if (pgd_present(*pgd)) {
			pud = pud_offset(pgd, 0);
			result = ident_pud_init(info, pud, addr, next);
			if (result)
				return result;
			continue;
		}

		pud = (pud_t *)info->alloc_pgt_page(info->context);
		if (!pud)
			return -ENOMEM;
		result = ident_pud_init(info, pud, addr, next);
		if (result)
			return result;
		set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE));
	}

	return 0;
}
