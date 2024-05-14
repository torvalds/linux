/* SPDX-License-Identifier: GPL-2.0 */

#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)

ENTRY(purgatory_start)

SECTIONS
{
	. = 0;
	.head.text : {
		_head = . ;
		HEAD_TEXT
		_ehead = . ;
	}
	.text :	{
		_text = .;	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		_edata = . ;
	}

	. = ALIGN(256);
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);	/* For convenience during zeroing */
		_ebss = .;
	}
	_end = .;

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.eh_frame)
		*(*__ksymtab*)
		*(___kcrctab*)
	}
}
