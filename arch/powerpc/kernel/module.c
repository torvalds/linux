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

#ifdef PPC64_ELF_ABI_v1
	sect = find_section(hdr, sechdrs, ".opd");
	if (sect != NULL) {
		me->arch.start_opd = sect->sh_addr;
		me->arch.end_opd = sect->sh_addr + sect->sh_size;
	}
#endif /* PPC64_ELF_ABI_v1 */

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

static __always_inline void *
__module_alloc(unsigned long size, unsigned long start, unsigned long end)
{
	/*
	 * Don't do huge page allocations for modules yet until more testing
	 * is done. STRICT_MODULE_RWX may require extra work to support this
	 * too.
	 */
	return __vmalloc_node_range(size, 1, start, end, GFP_KERNEL, PAGE_KERNEL_EXEC,
				    VM_FLUSH_RESET_PERMS | VM_NO_HUGE_VMAP,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

void *module_alloc(unsigned long size)
{
#ifdef MODULES_VADDR
	unsigned long limit = (unsigned long)_etext - SZ_32M;
	void *ptr = NULL;

	BUILD_BUG_ON(TASK_SIZE > MODULES_VADDR);

	/* First try within 32M limit from _etext to avoid branch trampolines */
	if (MODULES_VADDR < PAGE_OFFSET && MODULES_END > limit)
		ptr = __module_alloc(size, limit, MODULES_END);

	if (!ptr)
		ptr = __module_alloc(size, MODULES_VADDR, MODULES_END);

	return ptr;
#else
	return __module_alloc(size, VMALLOC_START, VMALLOC_END);
#endif
}
