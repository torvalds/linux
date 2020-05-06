// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2020, IBM Corporation.
 */

#include <linux/uaccess.h>
#include <asm/inst.h>

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
