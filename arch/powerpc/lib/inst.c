// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2020, IBM Corporation.
 */

#include <linux/uaccess.h>
#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>

#ifdef CONFIG_PPC64
int probe_user_read_inst(struct ppc_inst *inst,
			 struct ppc_inst __user *nip)
{
	unsigned int val, suffix;
	int err;

	err = probe_user_read(&val, nip, sizeof(val));
	if (err)
		return err;
	if (get_op(val) == OP_PREFIX) {
		err = probe_user_read(&suffix, (void __user *)nip + 4, 4);
		*inst = ppc_inst_prefix(val, suffix);
	} else {
		*inst = ppc_inst(val);
	}
	return err;
}

int probe_kernel_read_inst(struct ppc_inst *inst,
			   struct ppc_inst *src)
{
	unsigned int val, suffix;
	int err;

	err = probe_kernel_read(&val, src, sizeof(val));
	if (err)
		return err;
	if (get_op(val) == OP_PREFIX) {
		err = probe_kernel_read(&suffix, (void *)src + 4, 4);
		*inst = ppc_inst_prefix(val, suffix);
	} else {
		*inst = ppc_inst(val);
	}
	return err;
}
#else /* !CONFIG_PPC64 */
int probe_user_read_inst(struct ppc_inst *inst,
			 struct ppc_inst __user *nip)
{
	unsigned int val;
	int err;

	err = probe_user_read(&val, nip, sizeof(val));
	if (!err)
		*inst = ppc_inst(val);

	return err;
}

int probe_kernel_read_inst(struct ppc_inst *inst,
			   struct ppc_inst *src)
{
	unsigned int val;
	int err;

	err = probe_kernel_read(&val, src, sizeof(val));
	if (!err)
		*inst = ppc_inst(val);

	return err;
}
#endif /* CONFIG_PPC64 */
