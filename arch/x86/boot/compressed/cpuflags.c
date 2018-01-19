// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_RANDOMIZE_BASE

#include "../cpuflags.c"

bool has_cpuflag(int flag)
{
	get_cpuflags();

	return test_bit(flag, cpu.flags);
}

#endif
