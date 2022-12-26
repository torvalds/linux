// SPDX-License-Identifier: GPL-2.0
/*
 * Common EFI memory map functions.
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#include <asm/early_ioremap.h>
#include <asm/efi.h>

#ifndef __efi_memmap_free
#define __efi_memmap_free(phys, size, flags) do { } while (0)
#endif

/**
 * __efi_memmap_init - Common code for mapping the EFI memory map
 * @data: EFI memory map data
 *
 * This function takes care of figuring out which function to use to
 * map the EFI memory map in efi.memmap based on how far into the boot
 * we are.
 *
 * During bootup EFI_MEMMAP_LATE in data->flags should be clear since we
 * only have access to the early_memremap*() functions as the vmalloc
 * space isn't setup.  Once the kernel is fully booted we can fallback
 * to the more robust memremap*() API.
 *
 * Returns zero on success, a negative error code on failure.
 */
int __init __efi_memmap_init(struct efi_memory_map_data *data)
{
	struct efi_memory_map map;
	phys_addr_t phys_map;

	phys_map = data->phys_map;

	if (data->flags & EFI_MEMMAP_LATE)
		map.map = memremap(phys_map, data->size, MEMREMAP_WB);
	else
		map.map = early_memremap(phys_map, data->size);

	if (!map.map) {
		pr_err("Could not map the memory map!\n");
		return -ENOMEM;
	}

	if (efi.memmap.flags & (EFI_MEMMAP_MEMBLOCK | EFI_MEMMAP_SLAB))
		__efi_memmap_free(efi.memmap.phys_map,
				  efi.memmap.desc_size * efi.memmap.nr_map,
				  efi.memmap.flags);

	map.phys_map = data->phys_map;
	map.nr_map = data->size / data->desc_size;
	map.map_end = map.map + data->size;

	map.desc_version = data->desc_version;
	map.desc_size = data->desc_size;
	map.flags = data->flags;

	set_bit(EFI_MEMMAP, &efi.flags);

	efi.memmap = map;

	return 0;
}

/**
 * efi_memmap_init_early - Map the EFI memory map data structure
 * @data: EFI memory map data
 *
 * Use early_memremap() to map the passed in EFI memory map and assign
 * it to efi.memmap.
 */
int __init efi_memmap_init_early(struct efi_memory_map_data *data)
{
	/* Cannot go backwards */
	WARN_ON(efi.memmap.flags & EFI_MEMMAP_LATE);

	data->flags = 0;
	return __efi_memmap_init(data);
}

void __init efi_memmap_unmap(void)
{
	if (!efi_enabled(EFI_MEMMAP))
		return;

	if (!(efi.memmap.flags & EFI_MEMMAP_LATE)) {
		unsigned long size;

		size = efi.memmap.desc_size * efi.memmap.nr_map;
		early_memunmap(efi.memmap.map, size);
	} else {
		memunmap(efi.memmap.map);
	}

	efi.memmap.map = NULL;
	clear_bit(EFI_MEMMAP, &efi.flags);
}

/**
 * efi_memmap_init_late - Map efi.memmap with memremap()
 * @phys_addr: Physical address of the new EFI memory map
 * @size: Size in bytes of the new EFI memory map
 *
 * Setup a mapping of the EFI memory map using ioremap_cache(). This
 * function should only be called once the vmalloc space has been
 * setup and is therefore not suitable for calling during early EFI
 * initialise, e.g. in efi_init(). Additionally, it expects
 * efi_memmap_init_early() to have already been called.
 *
 * The reason there are two EFI memmap initialisation
 * (efi_memmap_init_early() and this late version) is because the
 * early EFI memmap should be explicitly unmapped once EFI
 * initialisation is complete as the fixmap space used to map the EFI
 * memmap (via early_memremap()) is a scarce resource.
 *
 * This late mapping is intended to persist for the duration of
 * runtime so that things like efi_mem_desc_lookup() and
 * efi_mem_attributes() always work.
 *
 * Returns zero on success, a negative error code on failure.
 */
int __init efi_memmap_init_late(phys_addr_t addr, unsigned long size)
{
	struct efi_memory_map_data data = {
		.phys_map = addr,
		.size = size,
		.flags = EFI_MEMMAP_LATE,
	};

	/* Did we forget to unmap the early EFI memmap? */
	WARN_ON(efi.memmap.map);

	/* Were we already called? */
	WARN_ON(efi.memmap.flags & EFI_MEMMAP_LATE);

	/*
	 * It makes no sense to allow callers to register different
	 * values for the following fields. Copy them out of the
	 * existing early EFI memmap.
	 */
	data.desc_version = efi.memmap.desc_version;
	data.desc_size = efi.memmap.desc_size;

	return __efi_memmap_init(&data);
}
