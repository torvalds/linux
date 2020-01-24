#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include "sizes.h"

#ifndef CONFIG_64BIT
OUTPUT_FORMAT("elf32-hppa-linux")
OUTPUT_ARCH(hppa)
#else
OUTPUT_FORMAT("elf64-hppa-linux")
OUTPUT_ARCH(hppa:hppa2.0w)
#endif

ENTRY(startup)

SECTIONS
{
	/* palo loads at 0x60000 */
	/* loaded kernel will move to 0x10000 */
	. = 0xe0000;    /* should not overwrite palo code */

	.head.text : {
		_head = . ;
		HEAD_TEXT
		_ehead = . ;
	}

	/* keep __gp below 0x1000000 */
#ifdef CONFIG_64BIT
	. = ALIGN(16);
	/* Linkage tables */
	.opd : {
		__start_opd = .;
		*(.opd)
		__end_opd = .;
	} PROVIDE (__gp = .);
	.plt : {
		*(.plt)
	}
	.dlt : {
		*(.dlt)
	}
#endif
	_startcode_end = .;

	/* vmlinux.bin.gz is here */
	. = ALIGN(8);
	.rodata.compressed : {
		*(.rodata.compressed)
	}

	/* bootloader code and data starts at least behind area of extracted kernel */
	. = MAX(ABSOLUTE(.), (SZ_end - SZparisc_kernel_start + KERNEL_BINARY_TEXT_START));

	/* align on next page boundary */
	. = ALIGN(4096);
	.text :	{
		_text = .;	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
	. = ALIGN(8);
	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		_edata = . ;
	}
	. = ALIGN(8);
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	. = ALIGN(8);
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4096);
		_ebss = .;
	}

	STABS_DEBUG
	.note 0 : { *(.note) }

	/* Sections to be discarded */
	DISCARDS
	/DISCARD/ : {
#ifdef CONFIG_64BIT
		/* temporary hack until binutils is fixed to not emit these
		 * for static binaries
		 */
		*(.PARISC.unwind)	/* no unwind data */
		*(.interp)
		*(.dynsym)
		*(.dynstr)
		*(.dynamic)
		*(.hash)
		*(.gnu.hash)
#endif
	}
}
