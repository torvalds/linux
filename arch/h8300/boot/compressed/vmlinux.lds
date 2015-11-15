SECTIONS
{
        .text :
        {
        __stext = . ;
	__text = .;
	       *(.text..startup)
	       *(.text)
        __etext = . ;
        }

	.rodata :
	{
		*(.rodata)
	}
        . = ALIGN(0x4) ;
        .data :

        {
        . = ALIGN(0x4) ;
        __sdata = . ;
        ___data_start = . ;
                *(.data.*)
	}
        . = ALIGN(0x4) ;
        .bss :
        {
        __sbss = . ;
                *(.bss*)
        . = ALIGN(0x4) ;
        __ebss = . ;
        }
        _end = . ;
}
