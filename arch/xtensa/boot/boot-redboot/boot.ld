OUTPUT_ARCH(xtensa)

SECTIONS
{
	.start 0xD1000000 : { *(.start) }

	.text :
	{
		__reloc_start = . ;
		_text_start = . ;
		*(.literal .text.literal .text)
		_text_end = . ;
	}

	.rodata ALIGN(0x04):
	{
		*(.rodata)
		*(.rodata1)
	}

	.data ALIGN(0x04):
	{
		*(.data)
		*(.data1)
		*(.sdata)
		*(.sdata2)
		*(.got.plt)
		*(.got)
		*(.dynamic)
	}

	__reloc_end = . ;

	. = ALIGN(0x10);
	__image_load = . ;
	.image 0xd0003000: AT(__image_load)
	{
		_image_start = .;
		*(image)
		. = (. + 3) & ~ 3;
		_image_end = .	;
	}


	.bss ((LOADADDR(.image) + SIZEOF(.image) + 3) & ~ 3):
	{
		__bss_start = .;
		*(.sbss)
		*(.scommon)
		*(.dynbss)
		*(.bss)
		__bss_end = .;
	}
	_end = .;
	_param_start = .;


	PROVIDE (end = .);
}
