/* SPDX-License-Identifier: GPL-2.0 */
OUTPUT_FORMAT("elf64-ia64-little")
OUTPUT_ARCH(ia64)
ENTRY(_start)
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = 0x100000;

  _text = .;
  .text : { *(__ivt_section) *(.text) }
  _etext = .;

  /* Global data */
  _data = .;
  .rodata : { *(.rodata) *(.rodata.*) }
  .data    : { *(.data) *(.gnu.linkonce.d*) CONSTRUCTORS }
  __gp = ALIGN (8) + 0x200000;
  .got           : { *(.got.plt) *(.got) }
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata     : { *(.sdata) }
  _edata  =  .;

  __bss_start = .;
  .sbss      : { *(.sbss) *(.scommon) }
  .bss       : { *(.bss) *(COMMON) }
  . = ALIGN(64 / 8);
  __bss_stop = .;
  _end = . ;

  /* Stabs deging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  /* DWARF de sections.
     Symbols in the DWARF deging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .de          0 : { *(.de) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .de_srcinfo  0 : { *(.de_srcinfo) }
  .de_sfnames  0 : { *(.de_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .de_aranges  0 : { *(.de_aranges) }
  .de_pubnames 0 : { *(.de_pubnames) }
  /* DWARF 2 */
  .de_info     0 : { *(.de_info) }
  .de_abbrev   0 : { *(.de_abbrev) }
  .de_line     0 : { *(.de_line) }
  .de_frame    0 : { *(.de_frame) }
  .de_str      0 : { *(.de_str) }
  .de_loc      0 : { *(.de_loc) }
  .de_macinfo  0 : { *(.de_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .de_weaknames 0 : { *(.de_weaknames) }
  .de_funcnames 0 : { *(.de_funcnames) }
  .de_typenames 0 : { *(.de_typenames) }
  .de_varnames  0 : { *(.de_varnames) }
  /* These must appear regardless of  .  */
}
