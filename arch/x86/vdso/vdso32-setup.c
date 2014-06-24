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
#include <linux/slab.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/elf.h>
#include <asm/tlbflush.h>
#include <asm/vdso.h>
#include <asm/proto.h>
#include <asm/fixmap.h>
#include <asm/hpet.h>
#include <asm/vvar.h>

#ifdef CONFIG_COMPAT_VDSO
#define VDSO_DEFAULT	0
#else
#define VDSO_DEFAULT	1
#endif

#ifdef CONFIG_X86_64
#define vdso_enabled			sysctl_vsyscall32
#define arch_setup_additional_pages	syscall32_setup_pages
extern int sysctl_ldt16;
#endif

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = VDSO_DEFAULT;

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);

	if (vdso_enabled > 1)
		pr_warn("vdso32 values other than 0 and 1 are no longer allowed; vdso disabled\n");

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

static struct page **vdso32_pages;
static unsigned vdso32_size;

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

#endif	/* CONFIG_X86_64 */

int __init sysenter_setup(void)
{
	char *vdso32_start, *vdso32_end;
	int npages, i;

#ifdef CONFIG_COMPAT
	if (vdso32_syscall()) {
		vdso32_start = vdso32_syscall_start;
		vdso32_end = vdso32_syscall_end;
		vdso32_pages = vdso32_syscall_pages;
	} else
#endif
	if (vdso32_sysenter()) {
		vdso32_start = vdso32_sysenter_start;
		vdso32_end = vdso32_sysenter_end;
		vdso32_pages = vdso32_sysenter_pages;
	} else {
		vdso32_start = vdso32_int80_start;
		vdso32_end = vdso32_int80_end;
		vdso32_pages = vdso32_int80_pages;
	}

	npages = ((vdso32_end - vdso32_start) + PAGE_SIZE - 1) / PAGE_SIZE;
	vdso32_size = npages << PAGE_SHIFT;
	for (i = 0; i < npages; i++)
		vdso32_pages[i] = virt_to_page(vdso32_start + i*PAGE_SIZE);

	patch_vdso32(vdso32_start, vdso32_size);

	return 0;
}

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;
	struct vm_area_struct *vma;
	static struct page *no_pages[] = {NULL};

#ifdef CONFIG_X86_X32_ABI
	if (test_thread_flag(TIF_X32))
		return x32_setup_additional_pages(bprm, uses_interp);
#endif

	if (vdso_enabled != 1)  /* Other values all mean "disabled" */
		return 0;

	down_write(&mm->mmap_sem);

	addr = get_unmapped_area(NULL, 0, vdso32_size + VDSO_OFFSET(VDSO_PREV_PAGES), 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	addr += VDSO_OFFSET(VDSO_PREV_PAGES);

	current->mm->context.vdso = (void *)addr;

	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 */
	ret = install_special_mapping(mm,
			addr,
			vdso32_size,
			VM_READ|VM_EXEC|
			VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
			vdso32_pages);

	if (ret)
		goto up_fail;

	vma = _install_special_mapping(mm,
			addr -  VDSO_OFFSET(VDSO_PREV_PAGES),
			VDSO_OFFSET(VDSO_PREV_PAGES),
			VM_READ,
			no_pages);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	ret = remap_pfn_range(vma,
		addr - VDSO_OFFSET(VDSO_VVAR_PAGE),
		__pa_symbol(&__vvar_page) >> PAGE_SHIFT,
		PAGE_SIZE,
		PAGE_READONLY);

	if (ret)
		goto up_fail;

#ifdef CONFIG_HPET_TIMER
	if (hpet_address) {
		ret = io_remap_pfn_range(vma,
			addr - VDSO_OFFSET(VDSO_HPET_PAGE),
			hpet_address >> PAGE_SHIFT,
			PAGE_SIZE,
			pgprot_noncached(PAGE_READONLY));

		if (ret)
			goto up_fail;
	}
#endif

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
	{
		.procname	= "ldt16",
		.data		= &sysctl_ldt16,
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
	return NULL;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	return 0;
}

int in_gate_area_no_mm(unsigned long addr)
{
	return 0;
}

#endif	/* CONFIG_X86_64 */
