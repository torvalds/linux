// SPDX-License-Identifier: GPL-2.0-or-later
/*  Kernel module help for powerpc.
    Copyright (C) 2001, 2003 Rusty Russell IBM Corporation.
    Copyright (C) 2008 Freescale Semiconductor, Inc.

*/
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/execmem.h>
#include <asm/module.h>
#include <linux/uaccess.h>
#include <asm/firmware.h>
#include <linux/sort.h>
#include <asm/setup.h>
#include <asm/sections.h>

static LIST_HEAD(module_bug_list);

static const Elf_Shdr *find_section(const Elf_Ehdr *hdr,
				    const Elf_Shdr *sechdrs,
				    const char *name)
{
	char *secstrings;
	unsigned int i;

	secstrings = (char *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	for (i = 1; i < hdr->e_shnum; i++)
		if (strcmp(secstrings+sechdrs[i].sh_name, name) == 0)
			return &sechdrs[i];
	return NULL;
}

int module_finalize(const Elf_Ehdr *hdr,
		const Elf_Shdr *sechdrs, struct module *me)
{
	const Elf_Shdr *sect;
	int rc;

	rc = module_finalize_ftrace(me, sechdrs);
	if (rc)
		return rc;

	/* Apply feature fixups */
	sect = find_section(hdr, sechdrs, "__ftr_fixup");
	if (sect != NULL)
		do_feature_fixups(cur_cpu_spec->cpu_features,
				  (void *)sect->sh_addr,
				  (void *)sect->sh_addr + sect->sh_size);

	sect = find_section(hdr, sechdrs, "__mmu_ftr_fixup");
	if (sect != NULL)
		do_feature_fixups(cur_cpu_spec->mmu_features,
				  (void *)sect->sh_addr,
				  (void *)sect->sh_addr + sect->sh_size);

#ifdef CONFIG_PPC64
	sect = find_section(hdr, sechdrs, "__fw_ftr_fixup");
	if (sect != NULL)
		do_feature_fixups(powerpc_firmware_features,
				  (void *)sect->sh_addr,
				  (void *)sect->sh_addr + sect->sh_size);
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC64_ELF_ABI_V1
	sect = find_section(hdr, sechdrs, ".opd");
	if (sect != NULL) {
		me->arch.start_opd = sect->sh_addr;
		me->arch.end_opd = sect->sh_addr + sect->sh_size;
	}
#endif /* CONFIG_PPC64_ELF_ABI_V1 */

#ifdef CONFIG_PPC_BARRIER_NOSPEC
	sect = find_section(hdr, sechdrs, "__spec_barrier_fixup");
	if (sect != NULL)
		do_barrier_nospec_fixups_range(barrier_nospec_enabled,
				  (void *)sect->sh_addr,
				  (void *)sect->sh_addr + sect->sh_size);
#endif /* CONFIG_PPC_BARRIER_NOSPEC */

	sect = find_section(hdr, sechdrs, "__lwsync_fixup");
	if (sect != NULL)
		do_lwsync_fixups(cur_cpu_spec->cpu_features,
				 (void *)sect->sh_addr,
				 (void *)sect->sh_addr + sect->sh_size);

	return 0;
}

static struct execmem_info execmem_info __ro_after_init;

struct execmem_info __init *execmem_arch_setup(void)
{
	pgprot_t prot = strict_module_rwx_enabled() ? PAGE_KERNEL : PAGE_KERNEL_EXEC;
	unsigned long fallback_start = 0, fallback_end = 0;
	unsigned long start, end;

	/*
	 * BOOK3S_32 and 8xx define MODULES_VADDR for text allocations and
	 * allow allocating data in the entire vmalloc space
	 */
#ifdef MODULES_VADDR
	unsigned long limit = (unsigned long)_etext - SZ_32M;

	BUILD_BUG_ON(TASK_SIZE > MODULES_VADDR);

	/* First try within 32M limit from _etext to avoid branch trampolines */
	if (MODULES_VADDR < PAGE_OFFSET && MODULES_END > limit) {
		start = limit;
		fallback_start = MODULES_VADDR;
		fallback_end = MODULES_END;
	} else {
		start = MODULES_VADDR;
	}

	end = MODULES_END;
#else
	start = VMALLOC_START;
	end = VMALLOC_END;
#endif

	execmem_info = (struct execmem_info){
		.ranges = {
			[EXECMEM_DEFAULT] = {
				.start	= start,
				.end	= end,
				.pgprot	= prot,
				.alignment = 1,
				.fallback_start	= fallback_start,
				.fallback_end	= fallback_end,
			},
			[EXECMEM_MODULE_DATA] = {
				.start	= VMALLOC_START,
				.end	= VMALLOC_END,
				.pgprot	= PAGE_KERNEL,
				.alignment = 1,
			},
		},
	};

	return &execmem_info;
}
