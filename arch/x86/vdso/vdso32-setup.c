/*
 * (C) Copyright 2002 Linus Torvalds
 * Portions based on the vdso-randomization code from exec-shield:
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>

#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <asm/vdso.h>

#ifdef CONFIG_COMPAT_VDSO
#define VDSO_DEFAULT	0
#else
#define VDSO_DEFAULT	1
#endif

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso32_enabled = VDSO_DEFAULT;

static int __init vdso32_setup(char *s)
{
	vdso32_enabled = simple_strtoul(s, NULL, 0);

	if (vdso32_enabled > 1)
		pr_warn("vdso32 values other than 0 and 1 are no longer allowed; vdso disabled\n");

	return 1;
}

/*
 * For consistency, the argument vdso32=[012] affects the 32-bit vDSO
 * behavior on both 64-bit and 32-bit kernels.
 * On 32-bit kernels, vdso=[012] means the same thing.
 */
__setup("vdso32=", vdso32_setup);

#ifdef CONFIG_X86_32
__setup_param("vdso=", vdso_setup, vdso32_setup, 0);
#endif

#ifdef CONFIG_X86_64

#define	vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SYSENTER32))
#define	vdso32_syscall()	(boot_cpu_has(X86_FEATURE_SYSCALL32))

#else  /* CONFIG_X86_32 */

#define vdso32_sysenter()	(boot_cpu_has(X86_FEATURE_SEP))
#define vdso32_syscall()	(0)

#endif	/* CONFIG_X86_64 */

#if defined(CONFIG_X86_32) || defined(CONFIG_COMPAT)
const struct vdso_image *selected_vdso32;
#endif

int __init sysenter_setup(void)
{
#ifdef CONFIG_COMPAT
	if (vdso32_syscall())
		selected_vdso32 = &vdso_image_32_syscall;
	else
#endif
	if (vdso32_sysenter())
		selected_vdso32 = &vdso_image_32_sysenter;
	else
		selected_vdso32 = &vdso_image_32_int80;

	init_vdso_image(selected_vdso32);

	return 0;
}

#ifdef CONFIG_X86_64

subsys_initcall(sysenter_setup);

#ifdef CONFIG_SYSCTL
/* Register vsyscall32 into the ABI table */
#include <linux/sysctl.h>

static struct ctl_table abi_table2[] = {
	{
		.procname	= "vsyscall32",
		.data		= &vdso32_enabled,
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
