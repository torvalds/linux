/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2000 Russell King
 */

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
    *(.ARM.exidx*)
    *(.ARM.extab*)
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
    *(.gnu.warning)
    *(.glue_7t)
    *(.glue_7)
  }
  .table : ALIGN(4) {
    _table_start = .;
    LONG(ZIMAGE_MAGIC(2))
    LONG(ZIMAGE_MAGIC(0x5a534c4b))
    LONG(ZIMAGE_MAGIC(__piggy_size_addr - _start))
    LONG(ZIMAGE_MAGIC(_kernel_bss_size))
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
  _got_start = .;
  .got			: { *(.got) }
  _got_end = .;

  /* ensure the zImage file size is always a multiple of 64 bits */
  /* (without a dummy byte, ld just ignores the empty section) */
  .pad			: { BYTE(0); . = ALIGN(8); }

#ifdef CONFIG_EFI_STUB
  .data : ALIGN(4096) {
    __pecoff_data_start = .;
    /*
     * The EFI stub always executes from RAM, and runs strictly before the
     * decompressor, so we can make an exception for its r/w data, and keep it
     */
    *(.data.efistub)
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

  .stab 0		: { *(.stab) }
  .stabstr 0		: { *(.stabstr) }
  .stab.excl 0		: { *(.stab.excl) }
  .stab.exclstr 0	: { *(.stab.exclstr) }
  .stab.index 0		: { *(.stab.index) }
  .stab.indexstr 0	: { *(.stab.indexstr) }
  .comment 0		: { *(.comment) }
}
ASSERT(_edata_real == _edata, "error: zImage file size is incorrect");
