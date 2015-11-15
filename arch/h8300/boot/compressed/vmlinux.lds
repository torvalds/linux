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
        .data :

        {
        . = ALIGN(0x4) ;
        __sdata = . ;
        ___data_start = . ;
                *(.data.*)
	}
        .bss :
        {
        . = ALIGN(0x4) ;
        __sbss = . ;
                *(.bss*)
        . = ALIGN(0x4) ;
        __ebss = . ;
        }
        _end = . ;
}
