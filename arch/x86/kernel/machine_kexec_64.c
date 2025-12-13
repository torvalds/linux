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
#include <linux/cc_platform.h>

#include <asm/init.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/io_apic.h>
#include <asm/debugreg.h>
#include <asm/kexec-bzimage64.h>
#include <asm/setup.h>
#include <asm/set_memory.h>
#include <asm/cpu.h>
#include <asm/efi.h>
#include <asm/processor.h>

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

	return kernel_ident_mapping_init(data->info, data->level4p,
					 res->start, res->end + 1);
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

static int map_mmio_serial(struct x86_mapping_info *info, pgd_t *level4p)
{
	unsigned long mstart, mend;

	if (!kexec_debug_8250_mmio32)
		return 0;

	mstart = kexec_debug_8250_mmio32 & PAGE_MASK;
	mend = (kexec_debug_8250_mmio32 + PAGE_SIZE + 23) & PAGE_MASK;
	pr_info("Map PCI serial at %lx - %lx\n", mstart, mend);
	return kernel_ident_mapping_init(info, level4p, mstart, mend);
}

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
	void *kaddr;
	int ret;

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

	ret = kernel_ident_mapping_init(info, level4p, mstart, mend);
	if (ret)
		return ret;

	kaddr = memremap(mstart, mend - mstart, MEMREMAP_WB);
	if (!kaddr) {
		pr_err("Could not map UEFI system table\n");
		return -ENOMEM;
	}

	mstart = efi_config_table;

	if (efi_enabled(EFI_64BIT)) {
		efi_system_table_64_t *stbl = (efi_system_table_64_t *)kaddr;

		mend = mstart + sizeof(efi_config_table_64_t) * stbl->nr_tables;
	} else {
		efi_system_table_32_t *stbl = (efi_system_table_32_t *)kaddr;

		mend = mstart + sizeof(efi_config_table_32_t) * stbl->nr_tables;
	}

	memunmap(kaddr);

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

static int init_transition_pgtable(struct kimage *image, pgd_t *pgd,
				   unsigned long control_page)
{
	pgprot_t prot = PAGE_KERNEL_EXEC_NOENC;
	unsigned long vaddr, paddr;
	int result = -ENOMEM;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * For the transition to the identity mapped page tables, the control
	 * code page also needs to be mapped at the virtual address it starts
	 * off running from.
	 */
	vaddr = (unsigned long)__va(control_page);
	paddr = control_page;
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

	if (cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT))
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

static int init_pgtable(struct kimage *image, unsigned long control_page)
{
	struct x86_mapping_info info = {
		.alloc_pgt_page	= alloc_pgt_page,
		.context	= image,
		.page_flag	= __PAGE_KERNEL_LARGE_EXEC,
		.kernpg_flag	= _KERNPG_TABLE_NOENC,
	};
	unsigned long mstart, mend;
	int result;
	int i;

	image->arch.pgd = alloc_pgt_page(image);
	if (!image->arch.pgd)
		return -ENOMEM;

	if (cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT)) {
		info.page_flag   |= _PAGE_ENC;
		info.kernpg_flag |= _PAGE_ENC;
	}

	if (direct_gbpages)
		info.direct_gbpages = true;

	for (i = 0; i < nr_pfn_mapped; i++) {
		mstart = pfn_mapped[i].start << PAGE_SHIFT;
		mend   = pfn_mapped[i].end << PAGE_SHIFT;

		result = kernel_ident_mapping_init(&info, image->arch.pgd,
						   mstart, mend);
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

		result = kernel_ident_mapping_init(&info, image->arch.pgd,
						   mstart, mend);

		if (result)
			return result;
	}

	/*
	 * Prepare EFI systab and ACPI tables for kexec kernel since they are
	 * not covered by pfn_mapped.
	 */
	result = map_efi_systab(&info, image->arch.pgd);
	if (result)
		return result;

	result = map_acpi_tables(&info, image->arch.pgd);
	if (result)
		return result;

	result = map_mmio_serial(&info, image->arch.pgd);
	if (result)
		return result;

	/*
	 * This must be last because the intermediate page table pages it
	 * allocates will not be control pages and may overlap the image.
	 */
	return init_transition_pgtable(image, image->arch.pgd, control_page);
}

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

static void prepare_debug_idt(unsigned long control_page, unsigned long vec_ofs)
{
	gate_desc idtentry = { 0 };
	int i;

	idtentry.bits.p		= 1;
	idtentry.bits.type	= GATE_TRAP;
	idtentry.segment	= __KERNEL_CS;
	idtentry.offset_low	= (control_page & 0xFFFF) + vec_ofs;
	idtentry.offset_middle	= (control_page >> 16) & 0xFFFF;
	idtentry.offset_high	= control_page >> 32;

	for (i = 0; i < 16; i++) {
		kexec_debug_idt[i] = idtentry;
		idtentry.offset_low += KEXEC_DEBUG_EXC_HANDLER_SIZE;
	}
}

int machine_kexec_prepare(struct kimage *image)
{
	void *control_page = page_address(image->control_code_page);
	unsigned long reloc_start = (unsigned long)__relocate_kernel_start;
	unsigned long reloc_end = (unsigned long)__relocate_kernel_end;
	int result;

	/*
	 * Some early TDX-capable platforms have an erratum.  A kernel
	 * partial write (a write transaction of less than cacheline
	 * lands at memory controller) to TDX private memory poisons that
	 * memory, and a subsequent read triggers a machine check.
	 *
	 * On those platforms the old kernel must reset TDX private
	 * memory before jumping to the new kernel otherwise the new
	 * kernel may see unexpected machine check.  For simplicity
	 * just fail kexec/kdump on those platforms.
	 */
	if (boot_cpu_has_bug(X86_BUG_TDX_PW_MCE)) {
		pr_info_once("Not allowed on platform with tdx_pw_mce bug\n");
		return -EOPNOTSUPP;
	}

	/* Setup the identity mapped 64bit page table */
	result = init_pgtable(image, __pa(control_page));
	if (result)
		return result;
	kexec_va_control_page = (unsigned long)control_page;
	kexec_pa_table_page = (unsigned long)__pa(image->arch.pgd);

	if (image->type == KEXEC_TYPE_DEFAULT)
		kexec_pa_swap_page = page_to_pfn(image->swap_page) << PAGE_SHIFT;

	prepare_debug_idt((unsigned long)__pa(control_page),
			  (unsigned long)kexec_debug_exc_vectors - reloc_start);

	__memcpy(control_page, __relocate_kernel_start, reloc_end - reloc_start);

	set_memory_rox((unsigned long)control_page, 1);

	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
	void *control_page = page_address(image->control_code_page);

	set_memory_nx((unsigned long)control_page, 1);
	set_memory_rw((unsigned long)control_page, 1);

	free_transition_pgtable(image);
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
void __nocfi machine_kexec(struct kimage *image)
{
	unsigned long reloc_start = (unsigned long)__relocate_kernel_start;
	relocate_kernel_fn *relocate_kernel_ptr;
	unsigned int relocate_kernel_flags;
	int save_ftrace_enabled;
	void *control_page;

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		save_processor_state();
#endif

	save_ftrace_enabled = __ftrace_enabled_save();

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();
	hw_breakpoint_disable();
	cet_disable();

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

	control_page = page_address(image->control_code_page);

	/*
	 * Allow for the possibility that relocate_kernel might not be at
	 * the very start of the page.
	 */
	relocate_kernel_ptr = control_page + (unsigned long)relocate_kernel - reloc_start;

	relocate_kernel_flags = 0;
	if (image->preserve_context)
		relocate_kernel_flags |= RELOC_KERNEL_PRESERVE_CONTEXT;

	/*
	 * This must be done before load_segments() since it resets
	 * GS to 0 and percpu data needs the correct GS to work.
	 */
	if (this_cpu_read(cache_state_incoherent))
		relocate_kernel_flags |= RELOC_KERNEL_CACHE_INCOHERENT;

	/*
	 * The segment registers are funny things, they have both a
	 * visible and an invisible part.  Whenever the visible part is
	 * set to a specific selector, the invisible part is loaded
	 * with from a table in memory.  At no other time is the
	 * descriptor table in memory accessed.
	 *
	 * Take advantage of this here by force loading the segments,
	 * before the GDT is zapped with an invalid value.
	 *
	 * load_segments() resets GS to 0.  Don't make any function call
	 * after here since call depth tracking uses percpu variables to
	 * operate (relocate_kernel() is explicitly ignored by call depth
	 * tracking).
	 */
	load_segments();

	/* now call it */
	image->start = relocate_kernel_ptr((unsigned long)image->head,
					   virt_to_phys(control_page),
					   image->start,
					   relocate_kernel_flags);

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		restore_processor_state();
#endif

	__ftrace_enabled_restore(save_ftrace_enabled);
}
/*
 * Handover to the next kernel, no CFI concern.
 */
ANNOTATE_NOCFI_SYM(machine_kexec);

/* arch-dependent functionality related to kexec file-based syscall */

#ifdef CONFIG_KEXEC_FILE
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

int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	vfree(image->elf_headers);
	image->elf_headers = NULL;
	image->elf_headers_sz = 0;

	return kexec_image_post_load_cleanup_default(image);
}
#endif /* CONFIG_KEXEC_FILE */

#ifdef CONFIG_CRASH_DUMP

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
	kexec_mark_range(crashk_res.start, control - 1, protect);
	control += KEXEC_CONTROL_PAGE_SIZE;
	kexec_mark_range(control, crashk_res.end, protect);
}

/* make the memory storing dm crypt keys in/accessible */
static void kexec_mark_dm_crypt_keys(bool protect)
{
	unsigned long start_paddr, end_paddr;
	unsigned int nr_pages;

	if (kexec_crash_image->dm_crypt_keys_addr) {
		start_paddr = kexec_crash_image->dm_crypt_keys_addr;
		end_paddr = start_paddr + kexec_crash_image->dm_crypt_keys_sz - 1;
		nr_pages = (PAGE_ALIGN(end_paddr) - PAGE_ALIGN_DOWN(start_paddr))/PAGE_SIZE;
		if (protect)
			set_memory_np((unsigned long)phys_to_virt(start_paddr), nr_pages);
		else
			__set_memory_prot(
				(unsigned long)phys_to_virt(start_paddr),
				nr_pages,
				__pgprot(_PAGE_PRESENT | _PAGE_NX | _PAGE_RW));
	}
}

void arch_kexec_protect_crashkres(void)
{
	kexec_mark_crashkres(true);
	kexec_mark_dm_crypt_keys(true);
}

void arch_kexec_unprotect_crashkres(void)
{
	kexec_mark_dm_crypt_keys(false);
	kexec_mark_crashkres(false);
}
#endif

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
	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		return 0;

	/*
	 * If host memory encryption is active we need to be sure that kexec
	 * pages are not encrypted because when we boot to the new kernel the
	 * pages won't be accessed encrypted (initially).
	 */
	return set_memory_decrypted((unsigned long)vaddr, pages);
}

void arch_kexec_pre_free_pages(void *vaddr, unsigned int pages)
{
	if (!cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		return;

	/*
	 * If host memory encryption is active we need to reset the pages back
	 * to being an encrypted mapping before freeing them.
	 */
	set_memory_encrypted((unsigned long)vaddr, pages);
}
