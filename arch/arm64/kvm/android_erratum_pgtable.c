// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 - Google LLC
 * Author: Will Deacon <willdeacon@google.com>
 */
#include <asm/kvm_host.h>
#include <asm/pgtable.h>

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/memblock.h>

DEFINE_STATIC_KEY_FALSE(pkvm_force_nc);
static int __init early_pkvm_force_nc_cfg(char *arg)
{
	static_branch_enable(&pkvm_force_nc);
	return 0;
}
early_param("kvm-arm.force_nc", early_pkvm_force_nc_cfg);

/*
 * Update the stage-2 memory attributes (cacheability) for a page, usually
 * in response to mapping or unmapping a normal non-cacheable region at stage-1.
 *
 * If 'force_nc' is set, the stage-2 entry is immediately made non-cacheable
 * (and cleaned+invalidated to the PoC) otherwise the entry is unmapped and the
 * cacheability determined based on the stage-1 attribute of the next access
 * (with no cache maintenance being performed).
 */
struct pkvm_host_nc_region {
	phys_addr_t	start;
	phys_addr_t	end;
};

#define PKVM_HOST_MAX_EARLY_NC_REGIONS	8
static struct pkvm_host_nc_region
pkvm_host_early_nc_regions[PKVM_HOST_MAX_EARLY_NC_REGIONS];

static void pkvm_host_track_early_nc_mapping(phys_addr_t addr)
{
	static int idx /*= 0*/;
	struct pkvm_host_nc_region *reg = &pkvm_host_early_nc_regions[idx];

	if (reg->start == reg->end) {
		reg->start = addr;
	} else if (reg->end != addr) {
		if (WARN_ON(idx == PKVM_HOST_MAX_EARLY_NC_REGIONS - 1))
			return;

		reg = &pkvm_host_early_nc_regions[++idx];
		reg->start = addr;
	}

	reg->end = addr + PAGE_SIZE;
}

void pkvm_host_set_stage2_memattr(phys_addr_t addr, bool force_nc)
{
	int err;

	if (kvm_get_mode() != KVM_MODE_PROTECTED)
		return;

	/*
	 * Non-memory regions or carveouts marked as "no-map" are handled
	 * entirely by their corresponding driver, which should avoid the
	 * creation of a cacheable alias in the first place.
	 */
	if (!memblock_is_map_memory(addr))
		return;

	if (!is_pkvm_initialized()) {
		if (!WARN_ON_ONCE(!force_nc))
			pkvm_host_track_early_nc_mapping(addr);
		return;
	}

	err = kvm_call_hyp_nvhe(__pkvm_host_set_stage2_memattr, addr, force_nc);
	WARN_ON(err && err != -EAGAIN);
}
EXPORT_SYMBOL_GPL(pkvm_host_set_stage2_memattr);

int __init pkvm_register_early_nc_mappings(void)
{
	int i;

	if (!is_pkvm_initialized())
		return 0;

	for (i = 0; i < PKVM_HOST_MAX_EARLY_NC_REGIONS; ++i) {
		struct pkvm_host_nc_region *reg = &pkvm_host_early_nc_regions[i];

		if (reg->start == reg->end)
			return 0;

		while (reg->start != reg->end) {
			int err;

			err = kvm_call_hyp_nvhe(__pkvm_host_set_stage2_memattr, reg->start, true);
			if (err)
				return err;

			reg->start += PAGE_SIZE;
		}
	}

	return 0;
}
