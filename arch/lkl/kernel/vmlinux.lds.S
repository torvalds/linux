#include <asm/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <linux/export.h>

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

VMLINUX_SYMBOL(jiffies) = VMLINUX_SYMBOL(jiffies_64);

SECTIONS
{
	VMLINUX_SYMBOL(__init_begin) = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	VMLINUX_SYMBOL(__init_end) = .;

	VMLINUX_SYMBOL(_stext) = .;
	VMLINUX_SYMBOL(_text) = . ;
	VMLINUX_SYMBOL(text) = . ;
	.text      :
	{
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		CPUIDLE_TEXT
	}
	VMLINUX_SYMBOL(_etext) = .;

	VMLINUX_SYMBOL(_sdata) = .;
	RO_DATA_SECTION(PAGE_SIZE)
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	VMLINUX_SYMBOL(_edata) = .;

	VMLINUX_SYMBOL(__start_ro_after_init) = .;
	.data..ro_after_init : { *(.data..ro_after_init)}
	EXCEPTION_TABLE(16)
	VMLINUX_SYMBOL(__end_ro_after_init) = .;
	NOTES

	BSS_SECTION(0, 0, 0)
	VMLINUX_SYMBOL(_end) = .;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
