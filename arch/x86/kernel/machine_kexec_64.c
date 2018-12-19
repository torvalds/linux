/*
 * handle transition of Linux booting another kernel
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
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

#include <asm/init.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/io_apic.h>
#include <asm/debugreg.h>
#include <asm/kexec-bzimage64.h>
#include <asm/setup.h>

#ifdef CONFIG_KEXEC_FILE
static struct kexec_file_ops *kexec_file_loaders[] = {
		&kexec_bzImage64_ops,
};
#endif

static void free_transition_pgtable(struct kimage *image)
{
	free_page((unsigned long)image->arch.pud);
	image->arch.pud = NULL;
	free_page((unsigned long)image->arch.pmd);
	image->arch.pmd = NULL;
	free_page((unsigned long)image->arch.pte);
	image->arch.pte = NULL;
}

static int init_transition_pgtable(struct kimage *image, pgd_t *pgd)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long vaddr, paddr;
	int result = -ENOMEM;

	vaddr = (unsigned long)relocate_kernel;
	paddr = __pa(page_address(image->control_code_page)+PAGE_SIZE);
	pgd += pgd_index(vaddr);
	if (!pgd_present(*pgd)) {
		pud = (pud_t *)get_zeroed_page(GFP_KERNEL);
		if (!pud)
			goto err;
		image->arch.pud = pud;
		set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE));
	}
	pud = pud_offset(pgd, vaddr);
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
	set_pte(pte, pfn_pte(paddr >> PAGE_SHIFT, PAGE_KERNEL_EXEC));
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
		.pmd_flag	= __PAGE_KERNEL_LARGE_EXEC,
	};
	unsigned long mstart, mend;
	pgd_t *level4p;
	int result;
	int i;

	level4p = (pgd_t *)__va(start_pgtable);
	clear_page(level4p);
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

	return init_transition_pgtable(image, level4p);
}

static void set_idt(void *newidt, u16 limit)
{
	struct desc_ptr curidt;

	/* x86-64 supports unaliged loads & stores */
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

#ifdef CONFIG_KEXEC_FILE
/* Update purgatory as needed after various image segments have been prepared */
static int arch_update_purgatory(struct kimage *image)
{
	int ret = 0;

	if (!image->file_mode)
		return 0;

	/* Setup copying of backup region */
	if (image->type == KEXEC_TYPE_CRASH) {
		ret = kexec_purgatory_get_set_symbol(image, "backup_dest",
				&image->arch.backup_load_addr,
				sizeof(image->arch.backup_load_addr), 0);
		if (ret)
			return ret;

		ret = kexec_purgatory_get_set_symbol(image, "backup_src",
				&image->arch.backup_src_start,
				sizeof(image->arch.backup_src_start), 0);
		if (ret)
			return ret;

		ret = kexec_purgatory_get_set_symbol(image, "backup_sz",
				&image->arch.backup_src_sz,
				sizeof(image->arch.backup_src_sz), 0);
		if (ret)
			return ret;
	}

	return ret;
}
#else /* !CONFIG_KEXEC_FILE */
static inline int arch_update_purgatory(struct kimage *image)
{
	return 0;
}
#endif /* CONFIG_KEXEC_FILE */

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

	/* update purgatory as needed */
	result = arch_update_purgatory(image);
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
		 * paths already have calls to disable_IO_APIC() in
		 * one form or other. kexec jump path also need
		 * one.
		 */
		disable_IO_APIC();
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
				       image->preserve_context);

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		restore_processor_state();
#endif

	__ftrace_enabled_restore(save_ftrace_enabled);
}

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_SYMBOL(phys_base);
	VMCOREINFO_SYMBOL(init_level4_pgt);

#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n",
			      kaslr_offset());
}

/* arch-dependent functionality related to kexec file-based syscall */

#ifdef CONFIG_KEXEC_FILE
int arch_kexec_kernel_image_probe(struct kimage *image, void *buf,
				  unsigned long buf_len)
{
	int i, ret = -ENOEXEC;
	struct kexec_file_ops *fops;

	for (i = 0; i < ARRAY_SIZE(kexec_file_loaders); i++) {
		fops = kexec_file_loaders[i];
		if (!fops || !fops->probe)
			continue;

		ret = fops->probe(buf, buf_len);
		if (!ret) {
			image->fops = fops;
			return ret;
		}
	}

	return ret;
}

void *arch_kexec_kernel_image_load(struct kimage *image)
{
	vfree(image->arch.elf_headers);
	image->arch.elf_headers = NULL;

	if (!image->fops || !image->fops->load)
		return ERR_PTR(-ENOEXEC);

	return image->fops->load(image, image->kernel_buf,
				 image->kernel_buf_len, image->initrd_buf,
				 image->initrd_buf_len, image->cmdline_buf,
				 image->cmdline_buf_len);
}

int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	if (!image->fops || !image->fops->cleanup)
		return 0;

	return image->fops->cleanup(image->image_loader_data);
}

int arch_kexec_kernel_verify_sig(struct kimage *image, void *kernel,
				 unsigned long kernel_len)
{
	if (!image->fops || !image->fops->verify_sig) {
		pr_debug("kernel loader does not support signature verification.");
		return -EKEYREJECTED;
	}

	return image->fops->verify_sig(kernel, kernel_len);
}

/*
 * Apply purgatory relocations.
 *
 * ehdr: Pointer to elf headers
 * sechdrs: Pointer to section headers.
 * relsec: section index of SHT_RELA section.
 *
 * TODO: Some of the code belongs to generic code. Move that in kexec.c.
 */
int arch_kexec_apply_relocations_add(const Elf64_Ehdr *ehdr,
				     Elf64_Shdr *sechdrs, unsigned int relsec)
{
	unsigned int i;
	Elf64_Rela *rel;
	Elf64_Sym *sym;
	void *location;
	Elf64_Shdr *section, *symtabsec;
	unsigned long address, sec_base, value;
	const char *strtab, *name, *shstrtab;

	/*
	 * ->sh_offset has been modified to keep the pointer to section
	 * contents in memory
	 */
	rel = (void *)sechdrs[relsec].sh_offset;

	/* Section to which relocations apply */
	section = &sechdrs[sechdrs[relsec].sh_info];

	pr_debug("Applying relocate section %u to %u\n", relsec,
		 sechdrs[relsec].sh_info);

	/* Associated symbol table */
	symtabsec = &sechdrs[sechdrs[relsec].sh_link];

	/* String table */
	if (symtabsec->sh_link >= ehdr->e_shnum) {
		/* Invalid strtab section number */
		pr_err("Invalid string table section index %d\n",
		       symtabsec->sh_link);
		return -ENOEXEC;
	}

	strtab = (char *)sechdrs[symtabsec->sh_link].sh_offset;

	/* section header string table */
	shstrtab = (char *)sechdrs[ehdr->e_shstrndx].sh_offset;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {

		/*
		 * rel[i].r_offset contains byte offset from beginning
		 * of section to the storage unit affected.
		 *
		 * This is location to update (->sh_offset). This is temporary
		 * buffer where section is currently loaded. This will finally
		 * be loaded to a different address later, pointed to by
		 * ->sh_addr. kexec takes care of moving it
		 *  (kexec_load_segment()).
		 */
		location = (void *)(section->sh_offset + rel[i].r_offset);

		/* Final address of the location */
		address = section->sh_addr + rel[i].r_offset;

		/*
		 * rel[i].r_info contains information about symbol table index
		 * w.r.t which relocation must be made and type of relocation
		 * to apply. ELF64_R_SYM() and ELF64_R_TYPE() macros get
		 * these respectively.
		 */
		sym = (Elf64_Sym *)symtabsec->sh_offset +
				ELF64_R_SYM(rel[i].r_info);

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
		else if (sym->st_shndx >= ehdr->e_shnum) {
			pr_err("Invalid section %d for symbol %s\n",
			       sym->st_shndx, name);
			return -ENOEXEC;
		} else
			sec_base = sechdrs[sym->st_shndx].sh_addr;

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
