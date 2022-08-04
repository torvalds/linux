// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/suspend.h>
#include <asm/coprocessor.h>

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = PFN_DOWN(__pa(&__nosave_begin));
	unsigned long nosave_end_pfn = PFN_UP(__pa(&__nosave_end));

	return	(pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

void notrace save_processor_state(void)
{
	WARN_ON(num_online_cpus() != 1);
#if XTENSA_HAVE_COPROCESSORS
	local_coprocessors_flush_release_all();
#endif
}

void notrace restore_processor_state(void)
{
}
