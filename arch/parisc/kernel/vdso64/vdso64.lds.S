/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is the infamous ld script for the 64 bits vdso library
 */
#include <asm/vdso.h>

/* Default link addresses for the vDSOs */
OUTPUT_FORMAT("elf64-hppa-linux")
OUTPUT_ARCH(hppa:hppa2.0w)
ENTRY(_start)

SECTIONS
{
  . = VDSO_LBASE + SIZEOF_HEADERS;
  .hash           : { *(.hash) }			:text
  .gnu.hash       : { *(.gnu.hash) }
  .dynsym         : { *(.dynsym) }
  .dynstr         : { *(.dynstr) }
  .gnu.version    : { *(.gnu.version) }
  .gnu.version_d  : { *(.gnu.version_d) }
  .gnu.version_r  : { *(.gnu.version_r) }

  .note		  : { *(.note.*) } 			:text	:note

  . = ALIGN (16);
  .text :
  {
    *(.text .stub .text.* .gnu.linkonce.t.*)
  }
  PROVIDE (__etext = .);
  PROVIDE (_etext = .);
  PROVIDE (etext = .);

  /* Other stuff is appended to the text segment: */
  .rodata		: { *(.rodata .rodata.* .gnu.linkonce.r.*) }
  .rodata1		: { *(.rodata1) }

  .eh_frame_hdr		: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
  .eh_frame		: { KEEP (*(.eh_frame)) }	:text
  .gcc_except_table	: { *(.gcc_except_table) }
  .fixup		: { *(.fixup) }

  .dynamic		: { *(.dynamic) }		:text	:dynamic
  .plt : { *(.plt) }
  .got : { *(.got) }

  _end = .;
  __end = .;
  PROVIDE (end = .);


  /* Stabs debugging sections are here too
   */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  .debug 0 : { *(.debug) }
  .line 0 : { *(.line) }

  .debug_srcinfo 0 : { *(.debug_srcinfo) }
  .debug_sfnames 0 : { *(.debug_sfnames) }

  .debug_aranges 0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }

  .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev 0 : { *(.debug_abbrev) }
  .debug_line 0 : { *(.debug_line) }
  .debug_frame 0 : { *(.debug_frame) }
  .debug_str 0 : { *(.debug_str) }
  .debug_loc 0 : { *(.debug_loc) }
  .debug_macinfo 0 : { *(.debug_macinfo) }

  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames 0 : { *(.debug_varnames) }

  /DISCARD/ : { *(.note.GNU-stack) }
  /DISCARD/ : { *(.data .data.* .gnu.linkonce.d.* .sdata*) }
  /DISCARD/ : { *(.bss .sbss .dynbss .dynsbss) }
}


PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5); /* PF_R|PF_X */
  note PT_NOTE FLAGS(4); /* PF_R */
  dynamic PT_DYNAMIC FLAGS(4); /* PF_R */
  eh_frame_hdr PT_GNU_EH_FRAME;
}


/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
  VDSO_VERSION_STRING {
    global:
	__kernel_sigtramp_rt64;
	__kernel_restart_syscall64;
    local: *;
  };
}
