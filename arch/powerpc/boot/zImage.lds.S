OUTPUT_ARCH(powerpc:common)
ENTRY(_zimage_start)
EXTERN(_zimage_start)
SECTIONS
{
  _start = .;
  .text      :
  {
    *(.text)
    *(.fixup)
  }
  _etext = .;
  . = ALIGN(4096);
  .data    :
  {
    *(.rodata*)
    *(.data*)
    *(.sdata*)
    __got2_start = .;
    *(.got2)
    __got2_end = .;
  }

  . = ALIGN(8);
  _dtb_start = .;
  .kernel:dtb : { *(.kernel:dtb) }
  _dtb_end = .;

  . = ALIGN(4096);
  _vmlinux_start =  .;
  .kernel:vmlinux.strip : { *(.kernel:vmlinux.strip) }
  _vmlinux_end =  .;

  . = ALIGN(4096);
  _initrd_start =  .;
  .kernel:initrd : { *(.kernel:initrd) }
  _initrd_end =  .;

  . = ALIGN(4096);
  _edata  =  .;

  . = ALIGN(4096);
  __bss_start = .;
  .bss       :
  {
   *(.sbss)
   *(.bss)
  }
  . = ALIGN(4096);
  _end = . ;
}
