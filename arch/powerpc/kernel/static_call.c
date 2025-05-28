// SPDX-License-Identifier: GPL-2.0
#include <linux/memory.h>
#include <linux/static_call.h>

#include <asm/text-patching.h>

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	int err;
	bool is_ret0 = (func == __static_call_return0);
	unsigned long _tramp = (unsigned long)tramp;
	unsigned long _func = (unsigned long)func;
	unsigned long _ret0 = _tramp + PPC_SCT_RET0;
	bool is_short = is_offset_in_branch_range((long)func - (long)(site ? : tramp));

	mutex_lock(&text_mutex);

	if (site && tail) {
		if (!func)
			err = patch_instruction(site, ppc_inst(PPC_RAW_BLR()));
		else if (is_ret0)
			err = patch_branch(site, _ret0, 0);
		else if (is_short)
			err = patch_branch(site, _func, 0);
		else if (tramp)
			err = patch_branch(site, _tramp, 0);
		else
			err = 0;
	} else if (site) {
		if (!func)
			err = patch_instruction(site, ppc_inst(PPC_RAW_NOP()));
		else if (is_ret0)
			err = patch_instruction(site, ppc_inst(PPC_RAW_LI(_R3, 0)));
		else if (is_short)
			err = patch_branch(site, _func, BRANCH_SET_LINK);
		else if (tramp)
			err = patch_branch(site, _tramp, BRANCH_SET_LINK);
		else
			err = 0;
	} else if (tramp) {
		if (func && !is_short) {
			err = patch_ulong(tramp + PPC_SCT_DATA, _func);
			if (err)
				goto out;
		}

		if (!func)
			err = patch_instruction(tramp, ppc_inst(PPC_RAW_BLR()));
		else if (is_ret0)
			err = patch_branch(tramp, _ret0, 0);
		else if (is_short)
			err = patch_branch(tramp, _func, 0);
		else
			err = patch_instruction(tramp, ppc_inst(PPC_RAW_NOP()));
	} else {
		err = 0;
	}

out:
	mutex_unlock(&text_mutex);

	if (err)
		panic("%s: patching failed %pS at %pS\n", __func__, func, tramp);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
