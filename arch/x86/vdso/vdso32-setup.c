/*
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
#include <linux/err.h>
#include <linux/module.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/elf.h>
#include <asm/tlbflush.h>
#include <asm/vdso.h>
#include <asm/proto.h>

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

#ifdef CONFIG_X86_64
#define vdso_enabled			sysctl_vsyscall32
#define arch_setup_additional_pages	syscall32_setup_pages
#endif

/*
 * This is the difference between the prelinked addresses in the vDSO images
 * and the VDSO_HIGH_BASE address where CONFIG_COMPAT_VDSO places the vDSO
 * in the user address space.
 */
#define VDSO_ADDR_ADJUST	(VDSO_HIGH_BASE - (unsigned long)VDSO32_PRELINK)

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = VDSO_DEFAULT;

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);

	return 1;
}

/*
 * For consistency, the argument vdso32=[012] affects the 32-bit vDSO
 * behavior on both 64-bit and 32-bit kernels.
 * On 32-bit kernels, vdso=[012] means the same thing.
 */
__setup("vdso32=", vdso_setup);

#ifdef CONFIG_X86_32
__setup_param("vdso=", vdso32_setup, vdso_setup, 0);

EXPORT_SYMBOL_GPL(vdso_enabled);
#endif

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
			sym->st_value += VDSO_ADDR_ADJUST;
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
			dyn->d_un.d_ptr += VDSO_ADDR_ADJUST;
			break;

		case DT_ENCODING ... OLD_DT_LOOS-1:
		case DT_LOOS ... DT_HIOS-1:
			/* Tags above DT_ENCODING are pointers if
			   they're even */
			if (dyn->d_tag >= DT_ENCODING &&
			    (dyn->d_tag & 1) == 0)
				dyn->d_un.d_ptr += VDSO_ADDR_ADJUST;
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

	BUG_ON(memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
	       !elf_check_arch_ia32(ehdr) ||
	       ehdr->e_type != ET_DYN);

	ehdr->e_entry += VDSO_ADDR_ADJUST;

	/* rebase phdrs */
	phdr = (void *)ehdr + ehdr->e_phoff;
	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr[i].p_vaddr += VDSO_ADDR_ADJUST;

		/* relocate dynamic stuff */
		if (phdr[i].p_type == PT_DYNAMIC)
			reloc_dyn(ehdr, phdr[i].p_offset);
	}

	/* rebase sections */
	shdr = (void *)ehdr + ehdr->e_shoff;
	for(i = 0; i < ehdr->e_shnum; i++) {
		if (!(shdr[i].sh_flags & SHF_ALLOC))
			continue;

		shdr[i].sh_addr += VDSO_ADDR_ADJUST;

		if (shdr[i].sh_type == SHT_SYMTAB ||
		    shdr[i].sh_type == SHT_DYNSYM)
			reloc_symtab(ehdr, shdr[i].sh_offset,
				     shdr[i].sh_size);
	}
}

static struct page *vdso32_pages[1];

#ifdef CONFIG_X86_64

#define	vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SYSENTER32))
#define	vdso32_syscall()	(boot_cpu_has(X86_FEATURE_SYSCALL32))

/* May not be __init: called during resume */
void syscall32_cpu_init(void)
{
	/* Load these always in case some future AMD CPU supports
	   SYSENTER from compat mode too. */
	wrmsrl_safe(MSR_IA32_SYSENTER_CS, (u64)__KERNEL_CS);
	wrmsrl_safe(MSR_IA32_SYSENTER_ESP, 0ULL);
	wrmsrl_safe(MSR_IA32_SYSENTER_EIP, (u64)ia32_sysenter_target);

	wrmsrl(MSR_CSTAR, ia32_cstar_target);
}

#define compat_uses_vma		1

static inline void map_compat_vdso(int map)
{
}

#else  /* CONFIG_X86_32 */

#define vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SEP))
#define vdso32_syscall()	(0)

void enable_sep_cpu(void)
{
	int cpu = get_cpu();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		put_cpu();
		return;
	}

	tss->x86_tss.ss1 = __KERNEL_CS;
	tss->x86_tss.sp1 = sizeof(struct tss_struct) + (unsigned long) tss;
	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->x86_tss.sp1, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) ia32_sysenter_target, 0);
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

	return 0;
}

#define compat_uses_vma		0

static void map_compat_vdso(int map)
{
	static int vdso_mapped;

	if (map == vdso_mapped)
		return;

	vdso_mapped = map;

	__set_fixmap(FIX_VDSO, page_to_pfn(vdso32_pages[0]) << PAGE_SHIFT,
		     map ? PAGE_READONLY_EXEC : PAGE_NONE);

	/* flush stray tlbs */
	flush_tlb_all();
}

#endif	/* CONFIG_X86_64 */

int __init sysenter_setup(void)
{
	void *syscall_page = (void *)get_zeroed_page(GFP_ATOMIC);
	const void *vsyscall;
	size_t vsyscall_len;

	vdso32_pages[0] = virt_to_page(syscall_page);

#ifdef CONFIG_X86_32
	gate_vma_init();
#endif

	if (vdso32_syscall()) {
		vsyscall = &vdso32_syscall_start;
		vsyscall_len = &vdso32_syscall_end - &vdso32_syscall_start;
	} else if (vdso32_sysenter()){
		vsyscall = &vdso32_sysenter_start;
		vsyscall_len = &vdso32_sysenter_end - &vdso32_sysenter_start;
	} else {
		vsyscall = &vdso32_int80_start;
		vsyscall_len = &vdso32_int80_end - &vdso32_int80_start;
	}

	memcpy(syscall_page, vsyscall, vsyscall_len);
	relocate_vdso(syscall_page);

	return 0;
}

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;
	bool compat;

#ifdef CONFIG_X86_X32_ABI
	if (test_thread_flag(TIF_X32))
		return x32_setup_additional_pages(bprm, uses_interp);
#endif

	if (vdso_enabled == VDSO_DISABLED)
		return 0;

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
	}

	current->mm->context.vdso = (void *)addr;

	if (compat_uses_vma || !compat) {
		/*
		 * MAYWRITE to allow gdb to COW and set breakpoints
		 */
		ret = install_special_mapping(mm, addr, PAGE_SIZE,
					      VM_READ|VM_EXEC|
					      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
					      vdso32_pages);

		if (ret)
			goto up_fail;
	}

	current_thread_info()->sysenter_return =
		VDSO32_SYMBOL(addr, SYSENTER_RETURN);

  up_fail:
	if (ret)
		current->mm->context.vdso = NULL;

	up_write(&mm->mmap_sem);

	return ret;
}

#ifdef CONFIG_X86_64

subsys_initcall(sysenter_setup);

#ifdef CONFIG_SYSCTL
/* Register vsyscall32 into the ABI table */
#include <linux/sysctl.h>

static struct ctl_table abi_table2[] = {
	{
		.procname	= "vsyscall32",
		.data		= &sysctl_vsyscall32,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{}
};

static struct ctl_table abi_root_table2[] = {
	{
		.procname = "abi",
		.mode = 0555,
		.child = abi_table2
	},
	{}
};

static __init int ia32_binfmt_init(void)
{
	register_sysctl_table(abi_root_table2);
	return 0;
}
__initcall(ia32_binfmt_init);
#endif

#else  /* CONFIG_X86_32 */

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	return NULL;
}

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	/*
	 * Check to see if the corresponding task was created in compat vdso
	 * mode.
	 */
	if (mm && mm->context.vdso == (void *)VDSO_HIGH_BASE)
		return &gate_vma;
	return NULL;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	const struct vm_area_struct *vma = get_gate_vma(mm);

	return vma && addr >= vma->vm_start && addr < vma->vm_end;
}

int in_gate_area_no_mm(unsigned long addr)
{
	return 0;
}

#endif	/* CONFIG_X86_64 */
