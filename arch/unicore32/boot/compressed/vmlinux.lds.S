/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore/boot/compressed/vmlinux.lds.in
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
OUTPUT_ARCH(unicore32)
ENTRY(_start)
SECTIONS
{
  /DISCARD/ : {
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
    *(.fixup)
    *(.gnu.warning)
    *(.rodata)
    *(.rodata.*)
    *(.piggydata)
    . = ALIGN(4);
  }

  _etext = .;

  /* Assume size of decompressed image is 4x the compressed image */
  _image_size = (_etext - _text) * 4;

  _got_start = .;
  .got			: { *(.got) }
  _got_end = .;
  .got.plt		: { *(.got.plt) }
  _edata = .;

  . = BSS_START;
  __bss_start = .;
  .bss			: { *(.bss) }
  _end = .;

  .stack		: { *(.stack) }
  .comment 0		: { *(.comment) }
}

