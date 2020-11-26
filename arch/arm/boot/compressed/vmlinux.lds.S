/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2000 Russell King
 */
#include <asm/vmlinux.lds.h>

#ifdef CONFIG_CPU_ENDIAN_BE8
#define ZIMAGE_MAGIC(x) ( (((x) >> 24) & 0x000000ff) | \
			  (((x) >>  8) & 0x0000ff00) | \
			  (((x) <<  8) & 0x00ff0000) | \
			  (((x) << 24) & 0xff000000) )
#else
#define ZIMAGE_MAGIC(x) (x)
#endif

OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  /DISCARD/ : {
    COMMON_DISCARDS
    *(.ARM.exidx*)
    *(.ARM.extab*)
    *(.note.*)
    *(.rel.*)
    /*
     * Discard any r/w data - this produces a link error if we have any,
     * which is required for PIC decompression.  Local data generates
     * GOTOFF relocations, which prevents it being relocated independently
     * of the text/got segments.
     */
    *(.data)
  }

  . = TEXT_START;
  _text = .;

  .text : {
    _start = .;
    *(.start)
    *(.text)
    *(.text.*)
    ARM_STUBS_TEXT
  }
  .table : ALIGN(4) {
    _table_start = .;
    LONG(ZIMAGE_MAGIC(6))
    LONG(ZIMAGE_MAGIC(0x5a534c4b))
    LONG(ZIMAGE_MAGIC(__piggy_size_addr - _start))
    LONG(ZIMAGE_MAGIC(_kernel_bss_size))
    LONG(ZIMAGE_MAGIC(TEXT_OFFSET))
    LONG(ZIMAGE_MAGIC(MALLOC_SIZE))
    LONG(0)
    _table_end = .;
  }
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    *(.data.rel.ro)
  }
  .piggydata : {
    *(.piggydata)
    __piggy_size_addr = . - 4;
  }

  . = ALIGN(4);
  _etext = .;

  .got.plt		: { *(.got.plt) }
#ifndef CONFIG_EFI_STUB
  _got_start = .;
  .got			: { *(.got) }
  _got_end = .;
#endif

  /* ensure the zImage file size is always a multiple of 64 bits */
  /* (without a dummy byte, ld just ignores the empty section) */
  .pad			: { BYTE(0); . = ALIGN(8); }

#ifdef CONFIG_EFI_STUB
  .data : ALIGN(4096) {
    __pecoff_data_start = .;
    _got_start = .;
    *(.got)
    _got_end = .;
    /*
     * The EFI stub always executes from RAM, and runs strictly before the
     * decompressor, so we can make an exception for its r/w data, and keep it
     */
    *(.data.efistub .bss.efistub)
    __pecoff_data_end = .;

    /*
     * PE/COFF mandates a file size which is a multiple of 512 bytes if the
     * section size equals or exceeds 4 KB
     */
    . = ALIGN(512);
  }
  __pecoff_data_rawsize = . - ADDR(.data);
#endif

  _edata = .;

  /*
   * The image_end section appears after any additional loadable sections
   * that the linker may decide to insert in the binary image.  Having
   * this symbol allows further debug in the near future.
   */
  .image_end (NOLOAD) : {
    /*
     * EFI requires that the image is aligned to 512 bytes, and appended
     * DTB requires that we know where the end of the image is.  Ensure
     * that both are satisfied by ensuring that there are no additional
     * sections emitted into the decompressor image.
     */
    _edata_real = .;
  }

  _magic_sig = ZIMAGE_MAGIC(0x016f2818);
  _magic_start = ZIMAGE_MAGIC(_start);
  _magic_end = ZIMAGE_MAGIC(_edata);
  _magic_table = ZIMAGE_MAGIC(_table_start - _start);

  . = BSS_START;
  __bss_start = .;
  .bss			: { *(.bss) }
  _end = .;

  . = ALIGN(8);		/* the stack must be 64-bit aligned */
  .stack		: { *(.stack) }

  PROVIDE(__pecoff_data_size = ALIGN(512) - ADDR(.data));
  PROVIDE(__pecoff_end = ALIGN(512));

  STABS_DEBUG
  DWARF_DEBUG
  ARM_DETAILS

  ARM_ASSERTS
}
ASSERT(_edata_real == _edata, "error: zImage file size is incorrect");
