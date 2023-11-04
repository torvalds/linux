// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/efi.h>
#include <linux/memblock.h>
#include <linux/screen_info.h>

#include <asm/efi.h>
#include <asm/mach/map.h>
#include <asm/mmu_context.h>

static int __init set_permissions(pte_t *ptep, unsigned long addr, void *data)
{
	efi_memory_desc_t *md = data;
	pte_t pte = *ptep;

	if (md->attribute & EFI_MEMORY_RO)
		pte = set_pte_bit(pte, __pgprot(L_PTE_RDONLY));
	if (md->attribute & EFI_MEMORY_XP)
		pte = set_pte_bit(pte, __pgprot(L_PTE_XN));
	set_pte_ext(ptep, pte, PTE_EXT_NG);
	return 0;
}

int __init efi_set_mapping_permissions(struct mm_struct *mm,
				       efi_memory_desc_t *md,
				       bool ignored)
{
	unsigned long base, size;

	base = md->virt_addr;
	size = md->num_pages << EFI_PAGE_SHIFT;

	/*
	 * We can only use apply_to_page_range() if we can guarantee that the
	 * entire region was mapped using pages. This should be the case if the
	 * region does not cover any naturally aligned SECTION_SIZE sized
	 * blocks.
	 */
	if (round_down(base + size, SECTION_SIZE) <
	    round_up(base, SECTION_SIZE) + SECTION_SIZE)
		return apply_to_page_range(mm, base, size, set_permissions, md);

	return 0;
}

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	struct map_desc desc = {
		.virtual	= md->virt_addr,
		.pfn		= __phys_to_pfn(md->phys_addr),
		.length		= md->num_pages * EFI_PAGE_SIZE,
	};

	/*
	 * Order is important here: memory regions may have all of the
	 * bits below set (and usually do), so we check them in order of
	 * preference.
	 */
	if (md->attribute & EFI_MEMORY_WB)
		desc.type = MT_MEMORY_RWX;
	else if (md->attribute & EFI_MEMORY_WT)
		desc.type = MT_MEMORY_RWX_NONCACHED;
	else if (md->attribute & EFI_MEMORY_WC)
		desc.type = MT_DEVICE_WC;
	else
		desc.type = MT_DEVICE;

	create_mapping_late(mm, &desc, true);

	/*
	 * If stricter permissions were specified, apply them now.
	 */
	if (md->attribute & (EFI_MEMORY_RO | EFI_MEMORY_XP))
		return efi_set_mapping_permissions(mm, md, false);
	return 0;
}

static unsigned long __initdata cpu_state_table = EFI_INVALID_TABLE_ADDR;

const efi_config_table_type_t efi_arch_tables[] __initconst = {
	{LINUX_EFI_ARM_CPU_STATE_TABLE_GUID, &cpu_state_table},
	{}
};

static void __init load_cpu_state_table(void)
{
	if (cpu_state_table != EFI_INVALID_TABLE_ADDR) {
		struct efi_arm_entry_state *state;
		bool dump_state = true;

		state = early_memremap_ro(cpu_state_table,
					  sizeof(struct efi_arm_entry_state));
		if (state == NULL) {
			pr_warn("Unable to map CPU entry state table.\n");
			return;
		}

		if ((state->sctlr_before_ebs & 1) == 0)
			pr_warn(FW_BUG "EFI stub was entered with MMU and Dcache disabled, please fix your firmware!\n");
		else if ((state->sctlr_after_ebs & 1) == 0)
			pr_warn(FW_BUG "ExitBootServices() returned with MMU and Dcache disabled, please fix your firmware!\n");
		else
			dump_state = false;

		if (dump_state || efi_enabled(EFI_DBG)) {
			pr_info("CPSR at EFI stub entry        : 0x%08x\n",
				state->cpsr_before_ebs);
			pr_info("SCTLR at EFI stub entry       : 0x%08x\n",
				state->sctlr_before_ebs);
			pr_info("CPSR after ExitBootServices() : 0x%08x\n",
				state->cpsr_after_ebs);
			pr_info("SCTLR after ExitBootServices(): 0x%08x\n",
				state->sctlr_after_ebs);
		}
		early_memunmap(state, sizeof(struct efi_arm_entry_state));
	}
}

void __init arm_efi_init(void)
{
	efi_init();

	/* ARM does not permit early mappings to persist across paging_init() */
	efi_memmap_unmap();

	load_cpu_state_table();
}
