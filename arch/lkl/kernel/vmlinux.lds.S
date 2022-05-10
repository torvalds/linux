#include <asm/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <linux/export.h>

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

jiffies = jiffies_64;

SECTIONS
{
	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = .;

	_stext = .;
	_text = . ;
	text = . ;
	.text      :
	{
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		CPUIDLE_TEXT
	}
	_etext = .;

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

	DISCARDS
}
