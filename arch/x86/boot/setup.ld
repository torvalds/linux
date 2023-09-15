/*
 * setup.ld
 *
 * Linker script for the i386 setup code
 */
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
	. = 0;
	.bstext	: {
		*(.bstext)
		. = 495;
	} =0xffffffff

	.header		: { *(.header) }
	.entrytext	: { *(.entrytext) }
	.inittext	: { *(.inittext) }
	.initdata	: { *(.initdata) }
	__end_init = .;

	.text		: { *(.text .text.*) }
	.text32		: { *(.text32) }

	. = ALIGN(16);
	.rodata		: { *(.rodata*) }

	.videocards	: {
		video_cards = .;
		*(.videocards)
		video_cards_end = .;
	}

	. = ALIGN(16);
	.data		: { *(.data*) }

	.pecompat	: { *(.pecompat) }
	PROVIDE(pecompat_fsize = setup_size - pecompat_fstart);

	.signature	: {
		setup_sig = .;
		LONG(0x5a5aaa55)

		setup_size = ALIGN(ABSOLUTE(.), 4096);
		setup_sects = ABSOLUTE(setup_size / 512);
	}

	. = ALIGN(16);
	.bss		:
	{
		__bss_start = .;
		*(.bss)
		__bss_end = .;
	}
	. = ALIGN(16);
	_end = .;

	/DISCARD/	: {
		*(.note*)
	}

	/*
	 * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
	 */
	. = ASSERT(_end <= 0x8000, "Setup too big!");
	. = ASSERT(hdr == 0x1f1, "The setup header has the wrong offset!");
	/* Necessary for the very-old-loader check to work... */
	. = ASSERT(__end_init <= 5*512, "init sections too big!");

}
