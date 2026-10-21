/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/setup.h>

OUTPUT_FORMAT("elf64-alpha")
OUTPUT_ARCH(alpha)
ENTRY(__start)

SECTIONS
{
	/*
	 * Link the stub at the address the real, uncompressed vmlinux has
	 * always occupied. aboot derives the OSF/1 parameter-page
	 * (ZERO_PGE) and initial stack (INIT_STACK) addresses from the
	 * loaded image's own p_vaddr, so a stub linked anywhere else would
	 * hand the real kernel a parameter page it does not expect.
	 */
	. = START_ADDR;

	.head.text : {
		_head = .;
		HEAD_TEXT
		_ehead = .;
	}

	. = ALIGN(8);
	.rodata.compressed : {
		*(.rodata.compressed)
	}

	. = ALIGN(PAGE_SIZE);
	.text : {
		_text = .;
		*(.text)
		*(.text.*)
		_etext = .;
	}
	. = ALIGN(8);
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	}
	. = ALIGN(8);
	.data : {
		*(.data)
		*(.data.*)
	}
	.got : {
		*(.got)
	}
	.sdata : {
		*(.sdata)
	}
	/*
	 * misc.c decompresses to _ebss, so nothing that the stub still
	 * needs may be placed after .bss. --orphan-handling=warn keeps a
	 * new section from silently landing there.
	 */
	. = ALIGN(8);
	.bss : {
		_bss = .;
		*(.sbss)
		*(.scommon)
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);
		_ebss = .;
	}

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
	/DISCARD/ : {
		*(.eh_frame)
		*(.modinfo)
		*(.note)
		*(.note.*)
	}
}
