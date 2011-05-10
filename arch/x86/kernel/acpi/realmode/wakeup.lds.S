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
	.jump	: {
		*(.jump)
	} = 0x90909090

	. = WAKEUP_HEADER_OFFSET;
	.header : {
		*(.header)
	}

	. = ALIGN(16);
	.text : {
		 *(.text*)
	} = 0x90909090

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

	. = ALIGN(16);
	.bss :	{
		__bss_start = .;
		*(.bss)
		__bss_end = .;
	}

	.signature : {
		*(.signature)
	}

	_end = .;

	/DISCARD/ : {
		*(.note*)
	}
}
