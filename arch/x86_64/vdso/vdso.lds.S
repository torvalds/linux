/*
 * Linker script for vsyscall DSO.  The vsyscall page is an ELF shared
 * object prelinked to its virtual address, and with only one read-only
 * segment (that fits in one page).  This script controls its layout.
 */
#include <asm/asm-offsets.h>
#include "voffset.h"

#define VDSO_PRELINK 0xffffffffff700000

SECTIONS
{
  . = VDSO_PRELINK + SIZEOF_HEADERS;

  .hash           : { *(.hash) }		:text
  .gnu.hash       : { *(.gnu.hash) }
  .dynsym         : { *(.dynsym) }
  .dynstr         : { *(.dynstr) }
  .gnu.version    : { *(.gnu.version) }
  .gnu.version_d  : { *(.gnu.version_d) }
  .gnu.version_r  : { *(.gnu.version_r) }

  /* This linker script is used both with -r and with -shared.
     For the layouts to match, we need to skip more than enough
     space for the dynamic symbol table et al.  If this amount
     is insufficient, ld -shared will barf.  Just increase it here.  */
  . = VDSO_PRELINK + VDSO_TEXT_OFFSET;

  .text           : { *(.text) }		:text
  .text.ptr       : { *(.text.ptr) }		:text
  . = VDSO_PRELINK + 0x900;
  .data           : { *(.data) }		:text
  .bss            : { *(.bss) }			:text

  .altinstructions : { *(.altinstructions) }			:text
  .altinstr_replacement  : { *(.altinstr_replacement) }	:text

  .note		  : { *(.note.*) }		:text :note
  .eh_frame_hdr   : { *(.eh_frame_hdr) }	:text :eh_frame_hdr
  .eh_frame       : { KEEP (*(.eh_frame)) }	:text
  .dynamic        : { *(.dynamic) }		:text :dynamic
  .useless        : {
  	*(.got.plt) *(.got)
	*(.gnu.linkonce.d.*)
	*(.dynbss)
	*(.gnu.linkonce.b.*)
  }						:text
}

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5); /* PF_R|PF_X */
  dynamic PT_DYNAMIC FLAGS(4); /* PF_R */
  note PT_NOTE FLAGS(4); /* PF_R */
  eh_frame_hdr 0x6474e550; /* PT_GNU_EH_FRAME, but ld doesn't match the name */
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
  LINUX_2.6 {
    global:
	clock_gettime;
	__vdso_clock_gettime;
	gettimeofday;
	__vdso_gettimeofday;
	getcpu;
	__vdso_getcpu;
    local: *;
  };
}
