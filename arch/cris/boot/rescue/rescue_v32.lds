/*#OUTPUT_FORMAT(elf32-us-cris) */
OUTPUT_ARCH (crisv32)
/* Now that NAND support has been stripped, this file could be simplified,
 * but it doesn't do any harm on the other hand so why bother. */

MEMORY
	{
	bootblk : ORIGIN = 0x38000000,
	          LENGTH = 0x00004000
	intmem  : ORIGIN = 0x38004000,
		  LENGTH = 0x00005000
	}

SECTIONS
{
	.text :
	{
		_stext = . ;
		*(.text)
		*(.init.text)
		*(.rodata)
		*(.rodata.*)
		_etext = . ;
	} > bootblk
	.data :
	{
		*(.data)
		_edata = . ;
	} > bootblk
	.bss :
	{
		_bss = . ;
		*(.bss)
		_end = ALIGN( 0x10 ) ;
	} > intmem

	/* Get rid of stuff from EXPORT_SYMBOL(foo). */
	/DISCARD/ :
	{
	        *(__ksymtab_strings)
	        *(__ksymtab)
	}
}
