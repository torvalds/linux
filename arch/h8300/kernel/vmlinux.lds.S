/* SPDX-License-Identifier: GPL-2.0 */

#define RO_EXCEPTION_TABLE_ALIGN	16

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/thread_info.h>

#define ROMTOP 0x000000
#define RAMTOP 0x400000

jiffies = jiffies_64 + 4;

ENTRY(_start)

SECTIONS
{
#if defined(CONFIG_ROMKERNEL)
	. = ROMTOP;
	.vectors :
	{
	_vector = . ;
		*(.vector*)
	}
#else
	. = RAMTOP;
	_ramstart = .;
	. = . + CONFIG_OFFSET;
#endif
	_text = .;
	HEAD_TEXT_SECTION
	.text : {
	_stext = . ;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
#if defined(CONFIG_ROMKERNEL)
		*(.int_redirect)
#endif
	_etext = . ;
	}
	RO_DATA(4)
	ROMEND = .;
#if defined(CONFIG_ROMKERNEL)
	. = RAMTOP;
	_ramstart = .;
#define ADDR(x) ROMEND
#endif
	_sdata = . ;
	__data_start = . ;
	RW_DATA(0, PAGE_SIZE, THREAD_SIZE)
#if defined(CONFIG_ROMKERNEL)
#undef ADDR
#endif
	. = ALIGN(0x4) ;
	__init_begin = .;
	INIT_TEXT_SECTION(4)
	INIT_DATA_SECTION(4)
	__init_end = .;
	_edata = . ;
	_begin_data = LOADADDR(.data);
	_sbss =.;
	BSS_SECTION(0, 0 ,0)
	_ebss =.;
	_ramend = .;
	_end = .;
	DISCARDS
}
