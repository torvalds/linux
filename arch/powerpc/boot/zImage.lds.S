/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>

#ifdef CONFIG_PPC64_BOOT_WRAPPER
OUTPUT_ARCH(powerpc:common64)
#else
OUTPUT_ARCH(powerpc:common)
#endif
ENTRY(_zimage_start)
EXTERN(_zimage_start)
SECTIONS
{
  .text      :
  {
    _start = .;
    *(.text)
    *(.fixup)
    _etext = .;
  }
  . = ALIGN(4096);
  .data    :
  {
    *(.rodata*)
    *(.data*)
    *(.sdata*)
#ifndef CONFIG_PPC64_BOOT_WRAPPER
    *(.got2)
#endif
  }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .dynamic :
  {
    __dynamic_start = .;
    *(.dynamic)
  }

#ifdef CONFIG_PPC64_BOOT_WRAPPER
  .got : ALIGN(256)
  {
    *(.got .toc)
  }
#endif

  .hash : { *(.hash) }
  .interp : { *(.interp) }
  .rela.dyn :
  {
#ifdef CONFIG_PPC64_BOOT_WRAPPER
    __rela_dyn_start = .;
#endif
    *(.rela*)
  }

  . = ALIGN(8);
  .kernel:dtb :
  {
    _dtb_start = .;
    *(.kernel:dtb)
    _dtb_end = .;
  }

  . = ALIGN(4096);
  .kernel:vmlinux.strip :
  {
    _vmlinux_start =  .;
    *(.kernel:vmlinux.strip)
    _vmlinux_end =  .;
  }

  . = ALIGN(4096);
  .kernel:initrd :
  {
    _initrd_start =  .;
    *(.kernel:initrd)
    _initrd_end =  .;
  }

  . = ALIGN(4096);
  .kernel:esm_blob :
  {
    _esm_blob_start =  .;
    *(.kernel:esm_blob)
    _esm_blob_end =  .;
  }

  . = ALIGN(4096);
  .bss       :
  {
    _edata  =  .;
    __bss_start = .;
    *(.sbss)
    *(.bss)
    *(COMMON)
    _end = . ;
  }
}
