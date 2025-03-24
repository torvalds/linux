// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/ctype.h>

#include "pi.h"

u64 get_kaslr_seed(uintptr_t dtb_pa)
{
	int node, len;
	fdt64_t *prop;
	u64 ret;

	node = fdt_path_offset((void *)dtb_pa, "/chosen");
	if (node < 0)
		return 0;

	prop = fdt_getprop_w((void *)dtb_pa, node, "kaslr-seed", &len);
	if (!prop || len != sizeof(u64))
		return 0;

	ret = fdt64_to_cpu(*prop);
	*prop = 0;
	return ret;
}

/**
 *  fdt_device_is_available - check if a device is available for use
 *
 * @fdt: pointer to the device tree blob
 * @node: offset of the node whose property to find
 *
 *  Returns true if the status property is absent or set to "okay" or "ok",
 *  false otherwise
 */
static bool fdt_device_is_available(const void *fdt, int node)
{
	const char *status;
	int statlen;

	status = fdt_getprop(fdt, node, "status", &statlen);
	if (!status)
		return true;

	if (statlen > 0) {
		if (!strcmp(status, "okay") || !strcmp(status, "ok"))
			return true;
	}

	return false;
}

/* Copy of fdt_nodename_eq_ */
static int fdt_node_name_eq(const void *fdt, int offset,
			    const char *s)
{
	int olen;
	int len = strlen(s);
	const char *p = fdt_get_name(fdt, offset, &olen);

	if (!p || olen < len)
		/* short match */
		return 0;

	if (memcmp(p, s, len) != 0)
		return 0;

	if (p[len] == '\0')
		return 1;
	else if (!memchr(s, '@', len) && (p[len] == '@'))
		return 1;
	else
		return 0;
}

/**
 *  isa_string_contains - check if isa string contains an extension
 *
 * @isa_str: isa string to search
 * @ext_name: the extension to search for
 *
 *  Returns true if the extension is in the given isa string,
 *  false otherwise
 */
static bool isa_string_contains(const char *isa_str, const char *ext_name)
{
	size_t i, single_end, len = strlen(ext_name);
	char ext_end;

	/* Error must contain rv32/64 */
	if (strlen(isa_str) < 4)
		return false;

	if (len == 1) {
		single_end = strcspn(isa_str, "sSxXzZ");
		/* Search for single chars between rv32/64 and multi-letter extensions */
		for (i = 4; i < single_end; i++) {
			if (tolower(isa_str[i]) == ext_name[0])
				return true;
		}
		return false;
	}

	/* Skip to start of multi-letter extensions */
	isa_str = strpbrk(isa_str, "sSxXzZ");
	while (isa_str) {
		if (strncasecmp(isa_str, ext_name, len) == 0) {
			ext_end = isa_str[len];
			/* Check if matches the whole extension. */
			if (ext_end == '\0' || ext_end == '_')
				return true;
		}
		/* Multi-letter extensions must be split from other multi-letter
		 * extensions with an "_", the end of a multi-letter extension will
		 * either be the null character or the "_" at the start of the next
		 * multi-letter extension.
		 */
		isa_str = strchr(isa_str, '_');
		if (isa_str)
			isa_str++;
	}

	return false;
}

/**
 *  early_cpu_isa_ext_available - check if cpu node has an extension
 *
 * @fdt: pointer to the device tree blob
 * @node: offset of the cpu node
 * @ext_name: the extension to search for
 *
 *  Returns true if the cpu node has the extension,
 *  false otherwise
 */
static bool early_cpu_isa_ext_available(const void *fdt, int node, const char *ext_name)
{
	const void *prop;
	int len;

	prop = fdt_getprop(fdt, node, "riscv,isa-extensions", &len);
	if (prop && fdt_stringlist_contains(prop, len, ext_name))
		return true;

	prop = fdt_getprop(fdt, node, "riscv,isa", &len);
	if (prop && isa_string_contains(prop, ext_name))
		return true;

	return false;
}

/**
 *  fdt_early_match_extension_isa - check if all cpu nodes have an extension
 *
 * @fdt: pointer to the device tree blob
 * @ext_name: the extension to search for
 *
 *  Returns true if the all available the cpu nodes have the extension,
 *  false otherwise
 */
bool fdt_early_match_extension_isa(const void *fdt, const char *ext_name)
{
	int node, parent;
	bool ret = false;

	parent = fdt_path_offset(fdt, "/cpus");
	if (parent < 0)
		return false;

	fdt_for_each_subnode(node, fdt, parent) {
		if (!fdt_node_name_eq(fdt, node, "cpu"))
			continue;

		if (!fdt_device_is_available(fdt, node))
			continue;

		if (!early_cpu_isa_ext_available(fdt, node, ext_name))
			return false;

		ret = true;
	}

	return ret;
}
