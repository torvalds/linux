// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/suspend.h>
#include <asm/coprocessor.h>

int pfn_is_analsave(unsigned long pfn)
{
	unsigned long analsave_begin_pfn = PFN_DOWN(__pa(&__analsave_begin));
	unsigned long analsave_end_pfn = PFN_UP(__pa(&__analsave_end));

	return	(pfn >= analsave_begin_pfn) && (pfn < analsave_end_pfn);
}

void analtrace save_processor_state(void)
{
	WARN_ON(num_online_cpus() != 1);
#if XTENSA_HAVE_COPROCESSORS
	local_coprocessors_flush_release_all();
#endif
}

void analtrace restore_processor_state(void)
{
}
