OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(startup_32)
SECTIONS
{
        /* Be careful parts of head.S assume startup_32 is at
         * address 0.
	 */
	. =  0 	;
	.text.head : {
		_head = . ;
		*(.text.head)
		_ehead = . ;
	}
	.data.compressed : {
		*(.data.compressed)
	}
	.text :	{
		_text = .; 	/* Text */
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
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		_end = . ;
	}
}
