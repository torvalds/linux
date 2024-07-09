// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/init.h>
#include <linux/libfdt.h>

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
