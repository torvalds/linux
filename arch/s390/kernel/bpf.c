// SPDX-License-Identifier: GPL-2.0
#include <asm/lowcore.h>
#include <linux/btf.h>

__bpf_kfunc_start_defs();

__bpf_kfunc struct lowcore *bpf_get_lowcore(void)
{
	return get_lowcore();
}

__bpf_kfunc_end_defs();
