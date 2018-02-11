/* SPDX-License-Identifier: GPL-2.0 */
MEMORY 
	{
	flash : ORIGIN = 0x00000000,
	        LENGTH = 0x00100000
	}

SECTIONS
{
	.text :
	{
		stext = . ;
		*(.text)
		etext = . ;
	} > flash
	.data :
	{
		*(.data)
		edata = . ;
	} > flash
}
