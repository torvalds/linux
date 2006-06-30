/*
 * ld script to make compressed SuperH/shmedia Linux kernel+decompression
 *		bootstrap
 * Modified by Stuart Menefy from arch/sh/vmlinux.lds.S written by Niibe Yutaka
 */


#ifdef CONFIG_LITTLE_ENDIAN
/* OUTPUT_FORMAT("elf32-sh64l-linux", "elf32-sh64l-linux", "elf32-sh64l-linux") */
#define NOP 0x6ff0fff0
#else
/* OUTPUT_FORMAT("elf32-sh64", "elf32-sh64", "elf32-sh64") */
#define NOP 0xf0fff06f
#endif

OUTPUT_FORMAT("elf32-sh64-linux")
OUTPUT_ARCH(sh)
ENTRY(_start)

#define ALIGNED_GAP(section, align) (((ADDR(section)+SIZEOF(section)+(align)-1) & ~((align)-1))-ADDR(section))
#define FOLLOWING(section, align) AT (LOADADDR(section) + ALIGNED_GAP(section,align))

SECTIONS
{
  _text = .;			/* Text and read-only data */

  .text : {
	*(.text)
	*(.text64)
	*(.text..SHmedia32)
	*(.fixup)
	*(.gnu.warning)
	} = NOP
  . = ALIGN(4);
  .rodata : { *(.rodata) }

  /* There is no 'real' reason for eight byte alignment, four would work
   * as well, but gdb downloads much (*4) faster with this.
   */
  . = ALIGN(8);
  .image : { *(.image) }
  . = ALIGN(4);
  _etext = .;			/* End of text section */

  .data :			/* Data */
	FOLLOWING(.image, 4)
	{
	_data = .;
	*(.data)
	}
  _data_image = LOADADDR(.data);/* Address of data section in ROM */

  _edata = .;			/* End of data section */

  .stack : { stack = .;  _stack = .; }

  . = ALIGN(4);
  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss)
	}
  . = ALIGN(4);
  _end = . ;
}
