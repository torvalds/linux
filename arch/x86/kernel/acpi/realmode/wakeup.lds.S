/*
 * wakeup.ld
 *
 * Linker script for the real-mode wakeup code
 */
#undef i386
#include "wakeup.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
	. = 0;
	.text : {
		 *(.text*)
	}

	. = ALIGN(16);
	.rodata : {
		*(.rodata*)
	}

	.videocards : {
		video_cards = .;
		*(.videocards)
		video_cards_end = .;
	}

	. = ALIGN(16);
	.data : {
		 *(.data*)
	}

	.signature : {
		end_signature = .;
		LONG(0x65a22c82)
	}

	. = ALIGN(16);
	.bss :	{
		__bss_start = .;
		*(.bss)
		__bss_end = .;
	}

	. = HEADER_OFFSET;
	.header : {
		*(.header)
	}

	. = ALIGN(16);
	_end = .;

	/DISCARD/ : {
		*(.note*)
	}

	. = ASSERT(_end <= WAKEUP_SIZE, "Wakeup too big!");
}
