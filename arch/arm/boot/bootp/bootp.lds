/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/boot/bootp/bootp.lds
 *
 *  Copyright (C) 2000-2002 Russell King
 */
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : {
   _stext = .;
   *(.start)
   *(.text)
   initrd_size = initrd_end - initrd_start;
   _etext = .;
  }

  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
