/*
 * ePAPR para-virtualization support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 */

#include <linux/of.h>
#include <asm/epapr_hcalls.h>
#include <asm/cacheflush.h>
#include <asm/code-patching.h>

bool epapr_paravirt_enabled;

static int __init epapr_paravirt_init(void)
{
	struct device_node *hyper_node;
	const u32 *insts;
	int len, i;

	hyper_node = of_find_node_by_path("/hypervisor");
	if (!hyper_node)
		return -ENODEV;

	insts = of_get_property(hyper_node, "hcall-instructions", &len);
	if (!insts)
		return -ENODEV;

	if (len % 4 || len > (4 * 4))
		return -ENODEV;

	for (i = 0; i < (len / 4); i++)
		patch_instruction(epapr_hypercall_start + i, insts[i]);

	epapr_paravirt_enabled = true;

	return 0;
}

early_initcall(epapr_paravirt_init);
