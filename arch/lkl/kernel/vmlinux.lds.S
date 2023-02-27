/* SPDX-License-Identifier: GPL-2.0 */
// When building LKL with libFuzzer instrumentation (-fsanitize=fuzzer) provide
// an empty linker script to avoid section merging. For additional information
// refer to arch/lkl/Makefile.postlink.
#if !defined(CONFIG_LKL_FUZZING) || defined(LKL_FUZZING_POSTLINK)
#include <asm/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <linux/export.h>

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

jiffies = jiffies_64;

/* FIXME: symbols defined here outside individual sections don't have a final
 * address assigned because vmlinux is linked incrementally. In the final
 * linking step (when linked against an application) they can be rearranged away
 * from the other adjacent symbols or sections in this file which makes them
 * bogus.
 *
 * Fortunatelly, in most cases their are used for informational purposes only,
 * e.g. printing the init/code/data size early during boot.
 */
SECTIONS
{
	/*
	 * Keep all text symbols in the same section, including init, to keep
	 * kallsyms partially working - at least for functions. This
	 * allows us to get non-bogus stack traces.
	 */
	.text :
	{
		_stext = .;
		_text = . ;
		text = . ;

		. = ALIGN(PAGE_SIZE);
		_sinittext = .;
		INIT_TEXT
		_einittext = .;

		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		CPUIDLE_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT

		_etext = .;
	}

	__init_begin = .;
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

	_sdata = .;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	__start_ro_after_init = .;
	.data..ro_after_init : { *(.data..ro_after_init)}
	EXCEPTION_TABLE(16)
	__end_ro_after_init = .;
	NOTES

	BSS_SECTION(0, 0, 0)
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG

	// Don't include DISCARDS section to build the target with code coverage.
#if !defined(CONFIG_LKL_FUZZING)
	DISCARDS
#endif // !defined(CONFIG_LKL_FUZZING)
}

#endif // !defined(CONFIG_LKL_FUZZING) || defined(LKL_FUZZING_POSTLINK)
