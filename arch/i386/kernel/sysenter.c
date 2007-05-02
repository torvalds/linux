/*
 * linux/arch/i386/kernel/sysenter.c
 *
 * (C) Copyright 2002 Linus Torvalds
 * Portions based on the vdso-randomization code from exec-shield:
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/elf.h>
#include <asm/tlbflush.h>

enum {
	VDSO_DISABLED = 0,
	VDSO_ENABLED = 1,
	VDSO_COMPAT = 2,
};

#ifdef CONFIG_COMPAT_VDSO
#define VDSO_DEFAULT	VDSO_COMPAT
#else
#define VDSO_DEFAULT	VDSO_ENABLED
#endif

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = VDSO_DEFAULT;

EXPORT_SYMBOL_GPL(vdso_enabled);

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);

	return 1;
}

__setup("vdso=", vdso_setup);

extern asmlinkage void sysenter_entry(void);

static __init void reloc_symtab(Elf32_Ehdr *ehdr,
				unsigned offset, unsigned size)
{
	Elf32_Sym *sym = (void *)ehdr + offset;
	unsigned nsym = size / sizeof(*sym);
	unsigned i;

	for(i = 0; i < nsym; i++, sym++) {
		if (sym->st_shndx == SHN_UNDEF ||
		    sym->st_shndx == SHN_ABS)
			continue;  /* skip */

		if (sym->st_shndx > SHN_LORESERVE) {
			printk(KERN_INFO "VDSO: unexpected st_shndx %x\n",
			       sym->st_shndx);
			continue;
		}

		switch(ELF_ST_TYPE(sym->st_info)) {
		case STT_OBJECT:
		case STT_FUNC:
		case STT_SECTION:
		case STT_FILE:
			sym->st_value += VDSO_HIGH_BASE;
		}
	}
}

static __init void reloc_dyn(Elf32_Ehdr *ehdr, unsigned offset)
{
	Elf32_Dyn *dyn = (void *)ehdr + offset;

	for(; dyn->d_tag != DT_NULL; dyn++)
		switch(dyn->d_tag) {
		case DT_PLTGOT:
		case DT_HASH:
		case DT_STRTAB:
		case DT_SYMTAB:
		case DT_RELA:
		case DT_INIT:
		case DT_FINI:
		case DT_REL:
		case DT_DEBUG:
		case DT_JMPREL:
		case DT_VERSYM:
		case DT_VERDEF:
		case DT_VERNEED:
		case DT_ADDRRNGLO ... DT_ADDRRNGHI:
			/* definitely pointers needing relocation */
			dyn->d_un.d_ptr += VDSO_HIGH_BASE;
			break;

		case DT_ENCODING ... OLD_DT_LOOS-1:
		case DT_LOOS ... DT_HIOS-1:
			/* Tags above DT_ENCODING are pointers if
			   they're even */
			if (dyn->d_tag >= DT_ENCODING &&
			    (dyn->d_tag & 1) == 0)
				dyn->d_un.d_ptr += VDSO_HIGH_BASE;
			break;

		case DT_VERDEFNUM:
		case DT_VERNEEDNUM:
		case DT_FLAGS_1:
		case DT_RELACOUNT:
		case DT_RELCOUNT:
		case DT_VALRNGLO ... DT_VALRNGHI:
			/* definitely not pointers */
			break;

		case OLD_DT_LOOS ... DT_LOOS-1:
		case DT_HIOS ... DT_VALRNGLO-1:
		default:
			if (dyn->d_tag > DT_ENCODING)
				printk(KERN_INFO "VDSO: unexpected DT_tag %x\n",
				       dyn->d_tag);
			break;
		}
}

static __init void relocate_vdso(Elf32_Ehdr *ehdr)
{
	Elf32_Phdr *phdr;
	Elf32_Shdr *shdr;
	int i;

	BUG_ON(memcmp(ehdr->e_ident, ELFMAG, 4) != 0 ||
	       !elf_check_arch(ehdr) ||
	       ehdr->e_type != ET_DYN);

	ehdr->e_entry += VDSO_HIGH_BASE;

	/* rebase phdrs */
	phdr = (void *)ehdr + ehdr->e_phoff;
	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr[i].p_vaddr += VDSO_HIGH_BASE;

		/* relocate dynamic stuff */
		if (phdr[i].p_type == PT_DYNAMIC)
			reloc_dyn(ehdr, phdr[i].p_offset);
	}

	/* rebase sections */
	shdr = (void *)ehdr + ehdr->e_shoff;
	for(i = 0; i < ehdr->e_shnum; i++) {
		if (!(shdr[i].sh_flags & SHF_ALLOC))
			continue;

		shdr[i].sh_addr += VDSO_HIGH_BASE;

		if (shdr[i].sh_type == SHT_SYMTAB ||
		    shdr[i].sh_type == SHT_DYNSYM)
			reloc_symtab(ehdr, shdr[i].sh_offset,
				     shdr[i].sh_size);
	}
}

void enable_sep_cpu(void)
{
	int cpu = get_cpu();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		put_cpu();
		return;
	}

	tss->x86_tss.ss1 = __KERNEL_CS;
	tss->x86_tss.esp1 = sizeof(struct tss_struct) + (unsigned long) tss;
	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->x86_tss.esp1, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) sysenter_entry, 0);
	put_cpu();	
}

static struct vm_area_struct gate_vma;

static int __init gate_vma_init(void)
{
	gate_vma.vm_mm = NULL;
	gate_vma.vm_start = FIXADDR_USER_START;
	gate_vma.vm_end = FIXADDR_USER_END;
	gate_vma.vm_flags = VM_READ | VM_MAYREAD | VM_EXEC | VM_MAYEXEC;
	gate_vma.vm_page_prot = __P101;
	/*
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	gate_vma.vm_flags |= VM_ALWAYSDUMP;
	return 0;
}

/*
 * These symbols are defined by vsyscall.o to mark the bounds
 * of the ELF DSO images included therein.
 */
extern const char vsyscall_int80_start, vsyscall_int80_end;
extern const char vsyscall_sysenter_start, vsyscall_sysenter_end;
static struct page *syscall_pages[1];

static void map_compat_vdso(int map)
{
	static int vdso_mapped;

	if (map == vdso_mapped)
		return;

	vdso_mapped = map;

	__set_fixmap(FIX_VDSO, page_to_pfn(syscall_pages[0]) << PAGE_SHIFT,
		     map ? PAGE_READONLY_EXEC : PAGE_NONE);

	/* flush stray tlbs */
	flush_tlb_all();
}

int __init sysenter_setup(void)
{
	void *syscall_page = (void *)get_zeroed_page(GFP_ATOMIC);
	const void *vsyscall;
	size_t vsyscall_len;

	syscall_pages[0] = virt_to_page(syscall_page);

	gate_vma_init();

	printk("Compat vDSO mapped to %08lx.\n", __fix_to_virt(FIX_VDSO));

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		vsyscall = &vsyscall_int80_start;
		vsyscall_len = &vsyscall_int80_end - &vsyscall_int80_start;
	} else {
		vsyscall = &vsyscall_sysenter_start;
		vsyscall_len = &vsyscall_sysenter_end - &vsyscall_sysenter_start;
	}

	memcpy(syscall_page, vsyscall, vsyscall_len);
	relocate_vdso(syscall_page);

	return 0;
}

/* Defined in vsyscall-sysenter.S */
extern void SYSENTER_RETURN;

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int exstack)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;
	bool compat;

	down_write(&mm->mmap_sem);

	/* Test compat mode once here, in case someone
	   changes it via sysctl */
	compat = (vdso_enabled == VDSO_COMPAT);

	map_compat_vdso(compat);

	if (compat)
		addr = VDSO_HIGH_BASE;
	else {
		addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
		if (IS_ERR_VALUE(addr)) {
			ret = addr;
			goto up_fail;
		}

		/*
		 * MAYWRITE to allow gdb to COW and set breakpoints
		 *
		 * Make sure the vDSO gets into every core dump.
		 * Dumping its contents makes post-mortem fully
		 * interpretable later without matching up the same
		 * kernel and hardware config to see what PC values
		 * meant.
		 */
		ret = install_special_mapping(mm, addr, PAGE_SIZE,
					      VM_READ|VM_EXEC|
					      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
					      VM_ALWAYSDUMP,
					      syscall_pages);

		if (ret)
			goto up_fail;
	}

	current->mm->context.vdso = (void *)addr;
	current_thread_info()->sysenter_return =
		(void *)VDSO_SYM(&SYSENTER_RETURN);

  up_fail:
	up_write(&mm->mmap_sem);

	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	return NULL;
}

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;

	/* Check to see if this task was created in compat vdso mode */
	if (mm && mm->context.vdso == (void *)VDSO_HIGH_BASE)
		return &gate_vma;
	return NULL;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	return 0;
}

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}
