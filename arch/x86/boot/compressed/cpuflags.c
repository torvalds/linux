#ifdef CONFIG_RANDOMIZE_BASE

#include "../cpuflags.c"

bool has_cpuflag(int flag)
{
	get_flags();

	return test_bit(flag, cpu.flags);
}

#endif
