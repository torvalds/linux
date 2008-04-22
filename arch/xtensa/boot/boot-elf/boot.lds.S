#include <asm/variant/core.h>
OUTPUT_ARCH(xtensa)
ENTRY(_ResetVector)

SECTIONS
{
	.start 0xD0000000 : { *(.start) }

	.text 0xD0000000:
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

	.initrd ALIGN(0x10) :
	{
		boot_initrd_start = . ;
		*(.initrd)
		boot_initrd_end = .;
	}

	. = ALIGN(0x10);
	__image_load = . ;
	.image 0xd0001000:
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

	.ResetVector.text XCHAL_RESET_VECTOR_VADDR :
	{
		*(.ResetVector.text)
	}


	PROVIDE (end = .);
}
