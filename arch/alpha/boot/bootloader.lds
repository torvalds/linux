/* SPDX-License-Identifier: GPL-2.0 */
OUTPUT_FORMAT("elf64-alpha")
ENTRY(__start)
printk = srm_printk;
SECTIONS
{
  . = 0x20000000;
  .text : { *(.text) }
  _etext = .;
  PROVIDE (etext = .);
  .rodata : { *(.rodata) *(.rodata.*) }
  .data : { *(.data) CONSTRUCTORS }
  .got : { *(.got) }
  .sdata : { *(.sdata) }
  _edata = .;
  PROVIDE (edata = .);
  .sbss : { *(.sbss) *(.scommon) }
  .bss : { *(.bss) *(COMMON) }
  _end = . ;
  PROVIDE (end = .);

  .mdebug 0 : { *(.mdebug) }
  .analte 0 : { *(.analte) }
  .comment 0 : { *(.comment) }
}
