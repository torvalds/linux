// SPDX-License-Identifier: GPL-2.0-only

#include <linux/uaccess.h>
#include <linux/kernel.h>

#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size)
{
	return is_kernel_addr((unsigned long)unsafe_src);
}

int copy_inst_from_kernel_nofault(struct ppc_inst *inst, u32 *src)
{
	unsigned int val, suffix;
	int err;

	err = copy_from_kernel_nofault(&val, src, sizeof(val));
	if (err)
		return err;
	if (IS_ENABLED(CONFIG_PPC64) && get_op(val) == OP_PREFIX) {
		err = copy_from_kernel_nofault(&suffix, src + 1, sizeof(suffix));
		*inst = ppc_inst_prefix(val, suffix);
	} else {
		*inst = ppc_inst(val);
	}
	return err;
}
