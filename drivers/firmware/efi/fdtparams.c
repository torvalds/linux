// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "efi: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/of.h>
#include <linux/of_fdt.h>

#include <asm/early_ioremap.h>

#define UEFI_PARAM(name, prop, field)			   \
	{						   \
		{ name },				   \
		{ prop },				   \
		offsetof(struct efi_fdt_params, field),    \
		sizeof_field(struct efi_fdt_params, field) \
	}

struct params {
	const char name[32];
	const char propname[32];
	int offset;
	int size;
};

static __initdata struct params fdt_params[] = {
	UEFI_PARAM("System Table", "linux,uefi-system-table", system_table),
	UEFI_PARAM("MemMap Address", "linux,uefi-mmap-start", mmap),
	UEFI_PARAM("MemMap Size", "linux,uefi-mmap-size", mmap_size),
	UEFI_PARAM("MemMap Desc. Size", "linux,uefi-mmap-desc-size", desc_size),
	UEFI_PARAM("MemMap Desc. Version", "linux,uefi-mmap-desc-ver", desc_ver)
};

static __initdata struct params xen_fdt_params[] = {
	UEFI_PARAM("System Table", "xen,uefi-system-table", system_table),
	UEFI_PARAM("MemMap Address", "xen,uefi-mmap-start", mmap),
	UEFI_PARAM("MemMap Size", "xen,uefi-mmap-size", mmap_size),
	UEFI_PARAM("MemMap Desc. Size", "xen,uefi-mmap-desc-size", desc_size),
	UEFI_PARAM("MemMap Desc. Version", "xen,uefi-mmap-desc-ver", desc_ver)
};

#define EFI_FDT_PARAMS_SIZE	ARRAY_SIZE(fdt_params)

static __initdata struct {
	const char *uname;
	const char *subnode;
	struct params *params;
} dt_params[] = {
	{ "hypervisor", "uefi", xen_fdt_params },
	{ "chosen", NULL, fdt_params },
};

struct param_info {
	int found;
	void *params;
	const char *missing;
};

static int __init __find_uefi_params(unsigned long node,
				     struct param_info *info,
				     struct params *params)
{
	const void *prop;
	void *dest;
	u64 val;
	int i, len;

	for (i = 0; i < EFI_FDT_PARAMS_SIZE; i++) {
		prop = of_get_flat_dt_prop(node, params[i].propname, &len);
		if (!prop) {
			info->missing = params[i].name;
			return 0;
		}

		dest = info->params + params[i].offset;
		info->found++;

		val = of_read_number(prop, len / sizeof(u32));

		if (params[i].size == sizeof(u32))
			*(u32 *)dest = val;
		else
			*(u64 *)dest = val;

		if (efi_enabled(EFI_DBG))
			pr_info("  %s: 0x%0*llx\n", params[i].name,
				params[i].size * 2, val);
	}

	return 1;
}

static int __init fdt_find_uefi_params(unsigned long node, const char *uname,
				       int depth, void *data)
{
	struct param_info *info = data;
	int i;

	for (i = 0; i < ARRAY_SIZE(dt_params); i++) {
		const char *subnode = dt_params[i].subnode;

		if (depth != 1 || strcmp(uname, dt_params[i].uname) != 0) {
			info->missing = dt_params[i].params[0].name;
			continue;
		}

		if (subnode) {
			int err = of_get_flat_dt_subnode_by_name(node, subnode);

			if (err < 0)
				return 0;

			node = err;
		}

		return __find_uefi_params(node, info, dt_params[i].params);
	}

	return 0;
}

int __init efi_get_fdt_params(struct efi_fdt_params *params)
{
	struct param_info info;
	int ret;

	pr_info("Getting EFI parameters from FDT:\n");

	info.found = 0;
	info.params = params;

	ret = of_scan_flat_dt(fdt_find_uefi_params, &info);
	if (!info.found)
		pr_info("UEFI not found.\n");
	else if (!ret)
		pr_err("Can't find '%s' in device tree!\n",
		       info.missing);

	return ret;
}
