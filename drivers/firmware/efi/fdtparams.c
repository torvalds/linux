// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "efi: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>

#include <asm/unaligned.h>

enum {
	SYSTAB,
	MMBASE,
	MMSIZE,
	DCSIZE,
	DCVERS,

	PARAMCOUNT
};

static __initconst const char name[][22] = {
	[SYSTAB] = "System Table         ",
	[MMBASE] = "MemMap Address       ",
	[MMSIZE] = "MemMap Size          ",
	[DCSIZE] = "MemMap Desc. Size    ",
	[DCVERS] = "MemMap Desc. Version ",
};

static __initconst const struct {
	const char	path[17];
	const char	params[PARAMCOUNT][26];
} dt_params[] = {
	{
#ifdef CONFIG_XEN    //  <-------17------>
		.path = "/hypervisor/uefi",
		.params = {
			[SYSTAB] = "xen,uefi-system-table",
			[MMBASE] = "xen,uefi-mmap-start",
			[MMSIZE] = "xen,uefi-mmap-size",
			[DCSIZE] = "xen,uefi-mmap-desc-size",
			[DCVERS] = "xen,uefi-mmap-desc-ver",
		}
	}, {
#endif
		.path = "/chosen",
		.params = {	//  <-----------26----------->
			[SYSTAB] = "linux,uefi-system-table",
			[MMBASE] = "linux,uefi-mmap-start",
			[MMSIZE] = "linux,uefi-mmap-size",
			[DCSIZE] = "linux,uefi-mmap-desc-size",
			[DCVERS] = "linux,uefi-mmap-desc-ver",
		}
	}
};

static int __init efi_get_fdt_prop(const void *fdt, int node, const char *pname,
				   const char *rname, void *var, int size)
{
	const void *prop;
	int len;
	u64 val;

	prop = fdt_getprop(fdt, node, pname, &len);
	if (!prop)
		return 1;

	val = (len == 4) ? (u64)be32_to_cpup(prop) : get_unaligned_be64(prop);

	if (size == 8)
		*(u64 *)var = val;
	else
		*(u32 *)var = (val < U32_MAX) ? val : U32_MAX; // saturate

	if (efi_enabled(EFI_DBG))
		pr_info("  %s: 0x%0*llx\n", rname, size * 2, val);

	return 0;
}

u64 __init efi_get_fdt_params(struct efi_memory_map_data *mm)
{
	const void *fdt = initial_boot_params;
	unsigned long systab;
	int i, j, node;
	struct {
		void	*var;
		int	size;
	} target[] = {
		[SYSTAB] = { &systab,		sizeof(systab) },
		[MMBASE] = { &mm->phys_map,	sizeof(mm->phys_map) },
		[MMSIZE] = { &mm->size,		sizeof(mm->size) },
		[DCSIZE] = { &mm->desc_size,	sizeof(mm->desc_size) },
		[DCVERS] = { &mm->desc_version,	sizeof(mm->desc_version) },
	};

	BUILD_BUG_ON(ARRAY_SIZE(target) != ARRAY_SIZE(name));
	BUILD_BUG_ON(ARRAY_SIZE(target) != ARRAY_SIZE(dt_params[0].params));

	for (i = 0; i < ARRAY_SIZE(dt_params); i++) {
		node = fdt_path_offset(fdt, dt_params[i].path);
		if (node < 0)
			continue;

		if (efi_enabled(EFI_DBG))
			pr_info("Getting UEFI parameters from %s in DT:\n",
				dt_params[i].path);

		for (j = 0; j < ARRAY_SIZE(target); j++) {
			const char *pname = dt_params[i].params[j];

			if (!efi_get_fdt_prop(fdt, node, pname, name[j],
					      target[j].var, target[j].size))
				continue;
			if (!j)
				goto notfound;
			pr_err("Can't find property '%s' in DT!\n", pname);
			return 0;
		}
		return systab;
	}
notfound:
	pr_info("UEFI not found.\n");
	return 0;
}
