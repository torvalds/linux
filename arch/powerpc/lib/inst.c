// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2020, IBM Corporation.
 */

#include <linux/uaccess.h>
#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>

int copy_inst_from_kernel_nofault(struct ppc_inst *inst, struct ppc_inst *src)
{
	unsigned int val, suffix;
	int err;

	err = copy_from_kernel_nofault(&val, src, sizeof(val));
	if (err)
		return err;
	if (IS_ENABLED(CONFIG_PPC64) && get_op(val) == OP_PREFIX) {
		err = copy_from_kernel_nofault(&suffix, (void *)src + 4, 4);
		*inst = ppc_inst_prefix(val, suffix);
	} else {
		*inst = ppc_inst(val);
	}
	return err;
}
