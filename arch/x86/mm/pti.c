/*
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This code is based in part on work published here:
 *
 *	https://github.com/IAIK/KAISER
 *
 * The original work was written by and and signed off by for the Linux
 * kernel by:
 *
 *   Signed-off-by: Richard Fellner <richard.fellner@student.tugraz.at>
 *   Signed-off-by: Moritz Lipp <moritz.lipp@iaik.tugraz.at>
 *   Signed-off-by: Daniel Gruss <daniel.gruss@iaik.tugraz.at>
 *   Signed-off-by: Michael Schwarz <michael.schwarz@iaik.tugraz.at>
 *
 * Major changes to the original code by: Dave Hansen <dave.hansen@intel.com>
 * Mostly rewritten by Thomas Gleixner <tglx@linutronix.de> and
 *		       Andy Lutomirsky <luto@amacapital.net>
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <asm/cpufeature.h>
#include <asm/hypervisor.h>
#include <asm/cmdline.h>
#include <asm/pti.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>

#undef pr_fmt
#define pr_fmt(fmt)     "Kernel/User page tables isolation: " fmt

static void __init pti_print_if_insecure(const char *reason)
{
	if (boot_cpu_has_bug(X86_BUG_CPU_INSECURE))
		pr_info("%s\n", reason);
}

static void __init pti_print_if_secure(const char *reason)
{
	if (!boot_cpu_has_bug(X86_BUG_CPU_INSECURE))
		pr_info("%s\n", reason);
}

void __init pti_check_boottime_disable(void)
{
	char arg[5];
	int ret;

	if (hypervisor_is_type(X86_HYPER_XEN_PV)) {
		pti_print_if_insecure("disabled on XEN PV.");
		return;
	}

	ret = cmdline_find_option(boot_command_line, "pti", arg, sizeof(arg));
	if (ret > 0)  {
		if (ret == 3 && !strncmp(arg, "off", 3)) {
			pti_print_if_insecure("disabled on command line.");
			return;
		}
		if (ret == 2 && !strncmp(arg, "on", 2)) {
			pti_print_if_secure("force enabled on command line.");
			goto enable;
		}
		if (ret == 4 && !strncmp(arg, "auto", 4))
			goto autosel;
	}

	if (cmdline_find_option_bool(boot_command_line, "nopti")) {
		pti_print_if_insecure("disabled on command line.");
		return;
	}

autosel:
	if (!boot_cpu_has_bug(X86_BUG_CPU_INSECURE))
		return;
enable:
	setup_force_cpu_cap(X86_FEATURE_PTI);
}

pgd_t __pti_set_user_pgd(pgd_t *pgdp, pgd_t pgd)
{
	/*
	 * Changes to the high (kernel) portion of the kernelmode page
	 * tables are not automatically propagated to the usermode tables.
	 *
	 * Users should keep in mind that, unlike the kernelmode tables,
	 * there is no vmalloc_fault equivalent for the usermode tables.
	 * Top-level entries added to init_mm's usermode pgd after boot
	 * will not be automatically propagated to other mms.
	 */
	if (!pgdp_maps_userspace(pgdp))
		return pgd;

	/*
	 * The user page tables get the full PGD, accessible from
	 * userspace:
	 */
	kernel_to_user_pgdp(pgdp)->pgd = pgd.pgd;

	/*
	 * If this is normal user memory, make it NX in the kernel
	 * pagetables so that, if we somehow screw up and return to
	 * usermode with the kernel CR3 loaded, we'll get a page fault
	 * instead of allowing user code to execute with the wrong CR3.
	 *
	 * As exceptions, we don't set NX if:
	 *  - _PAGE_USER is not set.  This could be an executable
	 *     EFI runtime mapping or something similar, and the kernel
	 *     may execute from it
	 *  - we don't have NX support
	 *  - we're clearing the PGD (i.e. the new pgd is not present).
	 */
	if ((pgd.pgd & (_PAGE_USER|_PAGE_PRESENT)) == (_PAGE_USER|_PAGE_PRESENT) &&
	    (__supported_pte_mask & _PAGE_NX))
		pgd.pgd |= _PAGE_NX;

	/* return the copy of the PGD we want the kernel to use: */
	return pgd;
}

/*
 * Initialize kernel page table isolation
 */
void __init pti_init(void)
{
	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	pr_info("enabled\n");
}
