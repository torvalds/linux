/*
 *	vmlinux.lds.S -- master linker script for m68knommu arch
 *
 *	(C) Copyright 2002-2012, Greg Ungerer <gerg@snapgear.com>
 *
 *	This linker script is equipped to build either ROM loaded or RAM
 *	run kernels.
 */

#if defined(CONFIG_RAMKERNEL)
#define	KTEXT_ADDR	CONFIG_KERNELBASE
#endif
#if defined(CONFIG_ROMKERNEL)
#define	KTEXT_ADDR	CONFIG_ROMSTART
#define	KDATA_ADDR	CONFIG_KERNELBASE
#define	LOAD_OFFSET	KDATA_ADDR + (ADDR(.text) + SIZEOF(.text))
#endif

#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(m68k)
ENTRY(_start)

jiffies = jiffies_64 + 4;

SECTIONS {

#ifdef CONFIG_ROMVEC
	. = CONFIG_ROMVEC;
	.romvec : {
		__rom_start = .;
		_romvec = .;
		*(.romvec)
		*(.data..initvect)
	}
#endif

	. = KTEXT_ADDR;

	_text = .;
	_stext = .;
	.text : {
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		*(.fixup)
		. = ALIGN(16);
	}
	_etext = .;

#ifdef KDATA_ADDR
	. = KDATA_ADDR;
#endif

	_sdata = .;
	RO_DATA_SECTION(PAGE_SIZE)
	RW_DATA_SECTION(16, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	EXCEPTION_TABLE(16)
	NOTES

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(16)
	.m68k_fixup : {
		__start_fixup = .;
		*(.m68k_fixup)
		__stop_fixup = .;
	}
	.init.data : {
		. = ALIGN(PAGE_SIZE);
		__init_end = .;
	}

	BSS_SECTION(0, 0, 0)

	_end = .;

	STABS_DEBUG
	.comment 0 : { *(.comment) }

	/* Sections to be discarded */
	DISCARDS
}

