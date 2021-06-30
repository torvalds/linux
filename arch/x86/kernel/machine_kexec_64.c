// SPDX-License-Identifier: GPL-2.0-only
/*
 * handle transition of Linux booting another kernel
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 */

#define pr_fmt(fmt)	"kexec: " fmt

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/reboot.h>
#include <linux/numa.h>
#include <linux/ftrace.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/vmalloc.h>
#include <linux/efi.h>

#include <asm/init.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/io_apic.h>
#include <asm/debugreg.h>
#include <asm/kexec-bzimage64.h>
#include <asm/setup.h>
#include <asm/set_memory.h>

#ifdef CONFIG_ACPI
/*
 * Used while adding mapping for ACPI tables.
 * Can be reused when other iomem regions need be mapped
 */
struct init_pgtable_data {
	struct x86_mapping_info *info;
	pgd_t *level4p;
};

static int mem_region_callback(struct resource *res, void *arg)
{
	struct init_pgtable_data *data = arg;
	unsigned long mstart, mend;

	mstart = res->start;
	mend = mstart + resource_size(res) - 1;

	return kernel_ident_mapping_init(data->info, data->level4p, mstart, mend);
}

static int
map_acpi_tables(struct x86_mapping_info *info, pgd_t *level4p)
{
	struct init_pgtable_data data;
	unsigned long flags;
	int ret;

	data.info = info;
	data.level4p = level4p;
	flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	ret = walk_iomem_res_desc(IORES_DESC_ACPI_TABLES, flags, 0, -1,
				  &data, mem_region_callback);
	if (ret && ret != -EINVAL)
		return ret;

	/* ACPI tables could be located in ACPI Non-volatile Storage region */
	ret = walk_iomem_res_desc(IORES_DESC_ACPI_NV_STORAGE, flags, 0, -1,
				  &data, mem_region_callback);
	if (ret && ret != -EINVAL)
		return ret;

	return 0;
}
#else
static int map_acpi_tables(struct x86_mapping_info *info, pgd_t *level4p) { return 0; }
#endif

#ifdef CONFIG_KEXEC_FILE
const struct kexec_file_ops * const kexec_file_loaders[] = {
		&kexec_bzImage64_ops,
		NULL
};
#endif

static int
map_efi_systab(struct x86_mapping_info *info, pgd_t *level4p)
{
#ifdef CONFIG_EFI
	unsigned long mstart, mend;

	if (!efi_enabled(EFI_BOOT))
		return 0;

	mstart = (boot_params.efi_info.efi_systab |
			((u64)boot_params.efi_info.efi_systab_hi<<32));

	if (efi_enabled(EFI_64BIT))
		mend = mstart + sizeof(efi_system_table_64_t);
	else
		mend = mstart + sizeof(efi_system_table_32_t);

	if (!mstart)
		return 0;

	return kernel_ident_mapping_init(info, level4p, mstart, mend);
#endif
	return 0;
}

static void free_transition_pgtable(struct kimage *image)
{
	free_page((unsigned long)image->arch.p4d);
	image->arch.p4d = NULL;
	free_page((unsigned long)image->arch.pud);
	image->arch.pud = NULL;
	free_page((unsigned long)image->arch.pmd);
	image->arch.pmd = NULL;
	free_page((unsigned long)image->arch.pte);
	image->arch.pte = NULL;
}

static int init_transition_pgtable(struct kimage *image, pgd_t *pgd)
{
	pgprot_t prot = PAGE_KERNEL_EXEC_NOENC;
	unsigned long vaddr, paddr;
	int result = -ENOMEM;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	vaddr = (unsigned long)relocate_kernel;
	paddr = __pa(page_address(image->control_code_page)+PAGE_SIZE);
	pgd += pgd_index(vaddr);
	if (!pgd_present(*pgd)) {
		p4d = (p4d_t *)get_zeroed_page(GFP_KERNEL);
		if (!p4d)
			goto err;
		image->arch.p4d = p4d;
		set_pgd(pgd, __pgd(__pa(p4d) | _KERNPG_TABLE));
	}
	p4d = p4d_offset(pgd, vaddr);
	if (!p4d_present(*p4d)) {
		pud = (pud_t *)get_zeroed_page(GFP_KERNEL);
		if (!pud)
			goto err;
		image->arch.pud = pud;
		set_p4d(p4d, __p4d(__pa(pud) | _KERNPG_TABLE));
	}
	pud = pud_offset(p4d, vaddr);
	if (!pud_present(*pud)) {
		pmd = (pmd_t *)get_zeroed_page(GFP_KERNEL);
		if (!pmd)
			goto err;
		image->arch.pmd = pmd;
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
	}
	pmd = pmd_offset(pud, vaddr);
	if (!pmd_present(*pmd)) {
		pte = (pte_t *)get_zeroed_page(GFP_KERNEL);
		if (!pte)
			goto err;
		image->arch.pte = pte;
		set_pmd(pmd, __pmd(__pa(pte) | _KERNPG_TABLE));
	}
	pte = pte_offset_kernel(pmd, vaddr);

	if (sev_active())
		prot = PAGE_KERNEL_EXEC;

	set_pte(pte, pfn_pte(paddr >> PAGE_SHIFT, prot));
	return 0;
err:
	return result;
}

static void *alloc_pgt_page(void *data)
{
	struct kimage *image = (struct kimage *)data;
	struct page *page;
	void *p = NULL;

	page = kimage_alloc_control_pages(image, 0);
	if (page) {
		p = page_address(page);
		clear_page(p);
	}

	return p;
}

static int init_pgtable(struct kimage *image, unsigned long start_pgtable)
{
	struct x86_mapping_info info = {
		.alloc_pgt_page	= alloc_pgt_page,
		.context	= image,
		.page_flag	= __PAGE_KERNEL_LARGE_EXEC,
		.kernpg_flag	= _KERNPG_TABLE_NOENC,
	};
	unsigned long mstart, mend;
	pgd_t *level4p;
	int result;
	int i;

	level4p = (pgd_t *)__va(start_pgtable);
	clear_page(level4p);

	if (sev_active()) {
		info.page_flag   |= _PAGE_ENC;
		info.kernpg_flag |= _PAGE_ENC;
	}

	if (direct_gbpages)
		info.direct_gbpages = true;

	for (i = 0; i < nr_pfn_mapped; i++) {
		mstart = pfn_mapped[i].start << PAGE_SHIFT;
		mend   = pfn_mapped[i].end << PAGE_SHIFT;

		result = kernel_ident_mapping_init(&info,
						 level4p, mstart, mend);
		if (result)
			return result;
	}

	/*
	 * segments's mem ranges could be outside 0 ~ max_pfn,
	 * for example when jump back to original kernel from kexeced kernel.
	 * or first kernel is booted with user mem map, and second kernel
	 * could be loaded out of that range.
	 */
	for (i = 0; i < image->nr_segments; i++) {
		mstart = image->segment[i].mem;
		mend   = mstart + image->segment[i].memsz;

		result = kernel_ident_mapping_init(&info,
						 level4p, mstart, mend);

		if (result)
			return result;
	}

	/*
	 * Prepare EFI systab and ACPI tables for kexec kernel since they are
	 * not covered by pfn_mapped.
	 */
	result = map_efi_systab(&info, level4p);
	if (result)
		return result;

	result = map_acpi_tables(&info, level4p);
	if (result)
		return result;

	return init_transition_pgtable(image, level4p);
}

static void set_idt(void *newidt, u16 limit)
{
	struct desc_ptr curidt;

	/* x86-64 supports unaligned loads & stores */
	curidt.size    = limit;
	curidt.address = (unsigned long)newidt;

	__asm__ __volatile__ (
		"lidtq %0\n"
		: : "m" (curidt)
		);
};


static void set_gdt(void *newgdt, u16 limit)
{
	struct desc_ptr curgdt;

	/* x86-64 supports unaligned loads & stores */
	curgdt.size    = limit;
	curgdt.address = (unsigned long)newgdt;

	__asm__ __volatile__ (
		"lgdtq %0\n"
		: : "m" (curgdt)
		);
};

static void load_segments(void)
{
	__asm__ __volatile__ (
		"\tmovl %0,%%ds\n"
		"\tmovl %0,%%es\n"
		"\tmovl %0,%%ss\n"
		"\tmovl %0,%%fs\n"
		"\tmovl %0,%%gs\n"
		: : "a" (__KERNEL_DS) : "memory"
		);
}

int machine_kexec_prepare(struct kimage *image)
{
	unsigned long start_pgtable;
	int result;

	/* Calculate the offsets */
	start_pgtable = page_to_pfn(image->control_code_page) << PAGE_SHIFT;

	/* Setup the identity mapped 64bit page table */
	result = init_pgtable(image, start_pgtable);
	if (result)
		return result;

	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
	free_transition_pgtable(image);
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
void machine_kexec(struct kimage *image)
{
	unsigned long page_list[PAGES_NR];
	void *control_page;
	int save_ftrace_enabled;

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		save_processor_state();
#endif

	save_ftrace_enabled = __ftrace_enabled_save();

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();
	hw_breakpoint_disable();

	if (image->preserve_context) {
#ifdef CONFIG_X86_IO_APIC
		/*
		 * We need to put APICs in legacy mode so that we can
		 * get timer interrupts in second kernel. kexec/kdump
		 * paths already have calls to restore_boot_irq_mode()
		 * in one form or other. kexec jump path also need one.
		 */
		clear_IO_APIC();
		restore_boot_irq_mode();
#endif
	}

	control_page = page_address(image->control_code_page) + PAGE_SIZE;
	memcpy(control_page, relocate_kernel, KEXEC_CONTROL_CODE_MAX_SIZE);

	page_list[PA_CONTROL_PAGE] = virt_to_phys(control_page);
	page_list[VA_CONTROL_PAGE] = (unsigned long)control_page;
	page_list[PA_TABLE_PAGE] =
	  (unsigned long)__pa(page_address(image->control_code_page));

	if (image->type == KEXEC_TYPE_DEFAULT)
		page_list[PA_SWAP_PAGE] = (page_to_pfn(image->swap_page)
						<< PAGE_SHIFT);

	/*
	 * The segment registers are funny things, they have both a
	 * visible and an invisible part.  Whenever the visible part is
	 * set to a specific selector, the invisible part is loaded
	 * with from a table in memory.  At no other time is the
	 * descriptor table in memory accessed.
	 *
	 * I take advantage of this here by force loading the
	 * segments, before I zap the gdt with an invalid value.
	 */
	load_segments();
	/*
	 * The gdt & idt are now invalid.
	 * If you want to load them you must set up your own idt & gdt.
	 */
	set_gdt(phys_to_virt(0), 0);
	set_idt(phys_to_virt(0), 0);

	/* now call it */
	image->start = relocate_kernel((unsigned long)image->head,
				       (unsigned long)page_list,
				       image->start,
				       image->preserve_context,
				       sme_active());

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		restore_processor_state();
#endif

	__ftrace_enabled_restore(save_ftrace_enabled);
}

/* arch-dependent functionality related to kexec file-based syscall */

#ifdef CONFIG_KEXEC_FILE
void *arch_kexec_kernel_image_load(struct kimage *image)
{
	vfree(image->elf_headers);
	image->elf_headers = NULL;

	if (!image->fops || !image->fops->load)
		return ERR_PTR(-ENOEXEC);

	return image->fops->load(image, image->kernel_buf,
				 image->kernel_buf_len, image->initrd_buf,
				 image->initrd_buf_len, image->cmdline_buf,
				 image->cmdline_buf_len);
}

/*
 * Apply purgatory relocations.
 *
 * @pi:		Purgatory to be relocated.
 * @section:	Section relocations applying to.
 * @relsec:	Section containing RELAs.
 * @symtabsec:	Corresponding symtab.
 *
 * TODO: Some of the code belongs to generic code. Move that in kexec.c.
 */
int arch_kexec_apply_relocations_add(struct purgatory_info *pi,
				     Elf_Shdr *section, const Elf_Shdr *relsec,
				     const Elf_Shdr *symtabsec)
{
	unsigned int i;
	Elf64_Rela *rel;
	Elf64_Sym *sym;
	void *location;
	unsigned long address, sec_base, value;
	const char *strtab, *name, *shstrtab;
	const Elf_Shdr *sechdrs;

	/* String & section header string table */
	sechdrs = (void *)pi->ehdr + pi->ehdr->e_shoff;
	strtab = (char *)pi->ehdr + sechdrs[symtabsec->sh_link].sh_offset;
	shstrtab = (char *)pi->ehdr + sechdrs[pi->ehdr->e_shstrndx].sh_offset;

	rel = (void *)pi->ehdr + relsec->sh_offset;

	pr_debug("Applying relocate section %s to %u\n",
		 shstrtab + relsec->sh_name, relsec->sh_info);

	for (i = 0; i < relsec->sh_size / sizeof(*rel); i++) {

		/*
		 * rel[i].r_offset contains byte offset from beginning
		 * of section to the storage unit affected.
		 *
		 * This is location to update. This is temporary buffer
		 * where section is currently loaded. This will finally be
		 * loaded to a different address later, pointed to by
		 * ->sh_addr. kexec takes care of moving it
		 *  (kexec_load_segment()).
		 */
		location = pi->purgatory_buf;
		location += section->sh_offset;
		location += rel[i].r_offset;

		/* Final address of the location */
		address = section->sh_addr + rel[i].r_offset;

		/*
		 * rel[i].r_info contains information about symbol table index
		 * w.r.t which relocation must be made and type of relocation
		 * to apply. ELF64_R_SYM() and ELF64_R_TYPE() macros get
		 * these respectively.
		 */
		sym = (void *)pi->ehdr + symtabsec->sh_offset;
		sym += ELF64_R_SYM(rel[i].r_info);

		if (sym->st_name)
			name = strtab + sym->st_name;
		else
			name = shstrtab + sechdrs[sym->st_shndx].sh_name;

		pr_debug("Symbol: %s info: %02x shndx: %02x value=%llx size: %llx\n",
			 name, sym->st_info, sym->st_shndx, sym->st_value,
			 sym->st_size);

		if (sym->st_shndx == SHN_UNDEF) {
			pr_err("Undefined symbol: %s\n", name);
			return -ENOEXEC;
		}

		if (sym->st_shndx == SHN_COMMON) {
			pr_err("symbol '%s' in common section\n", name);
			return -ENOEXEC;
		}

		if (sym->st_shndx == SHN_ABS)
			sec_base = 0;
		else if (sym->st_shndx >= pi->ehdr->e_shnum) {
			pr_err("Invalid section %d for symbol %s\n",
			       sym->st_shndx, name);
			return -ENOEXEC;
		} else
			sec_base = pi->sechdrs[sym->st_shndx].sh_addr;

		value = sym->st_value;
		value += sec_base;
		value += rel[i].r_addend;

		switch (ELF64_R_TYPE(rel[i].r_info)) {
		case R_X86_64_NONE:
			break;
		case R_X86_64_64:
			*(u64 *)location = value;
			break;
		case R_X86_64_32:
			*(u32 *)location = value;
			if (value != *(u32 *)location)
				goto overflow;
			break;
		case R_X86_64_32S:
			*(s32 *)location = value;
			if ((s64)value != *(s32 *)location)
				goto overflow;
			break;
		case R_X86_64_PC32:
		case R_X86_64_PLT32:
			value -= (u64)address;
			*(u32 *)location = value;
			break;
		default:
			pr_err("Unknown rela relocation: %llu\n",
			       ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;

overflow:
	pr_err("Overflow in relocation type %d value 0x%lx\n",
	       (int)ELF64_R_TYPE(rel[i].r_info), value);
	return -ENOEXEC;
}
#endif /* CONFIG_KEXEC_FILE */

static int
kexec_mark_range(unsigned long start, unsigned long end, bool protect)
{
	struct page *page;
	unsigned int nr_pages;

	/*
	 * For physical range: [start, end]. We must skip the unassigned
	 * crashk resource with zero-valued "end" member.
	 */
	if (!end || start > end)
		return 0;

	page = pfn_to_page(start >> PAGE_SHIFT);
	nr_pages = (end >> PAGE_SHIFT) - (start >> PAGE_SHIFT) + 1;
	if (protect)
		return set_pages_ro(page, nr_pages);
	else
		return set_pages_rw(page, nr_pages);
}

static void kexec_mark_crashkres(bool protect)
{
	unsigned long control;

	kexec_mark_range(crashk_low_res.start, crashk_low_res.end, protect);

	/* Don't touch the control code page used in crash_kexec().*/
	control = PFN_PHYS(page_to_pfn(kexec_crash_image->control_code_page));
	/* Control code page is located in the 2nd page. */
	kexec_mark_range(crashk_res.start, control + PAGE_SIZE - 1, protect);
	control += KEXEC_CONTROL_PAGE_SIZE;
	kexec_mark_range(control, crashk_res.end, protect);
}

void arch_kexec_protect_crashkres(void)
{
	kexec_mark_crashkres(true);
}

void arch_kexec_unprotect_crashkres(void)
{
	kexec_mark_crashkres(false);
}

/*
 * During a traditional boot under SME, SME will encrypt the kernel,
 * so the SME kexec kernel also needs to be un-encrypted in order to
 * replicate a normal SME boot.
 *
 * During a traditional boot under SEV, the kernel has already been
 * loaded encrypted, so the SEV kexec kernel needs to be encrypted in
 * order to replicate a normal SEV boot.
 */
int arch_kexec_post_alloc_pages(void *vaddr, unsigned int pages, gfp_t gfp)
{
	if (sev_active())
		return 0;

	/*
	 * If SME is active we need to be sure that kexec pages are
	 * not encrypted because when we boot to the new kernel the
	 * pages won't be accessed encrypted (initially).
	 */
	return set_memory_decrypted((unsigned long)vaddr, pages);
}

void arch_kexec_pre_free_pages(void *vaddr, unsigned int pages)
{
	if (sev_active())
		return;

	/*
	 * If SME is active we need to reset the pages back to being
	 * an encrypted mapping before freeing them.
	 */
	set_memory_encrypted((unsigned long)vaddr, pages);
}
