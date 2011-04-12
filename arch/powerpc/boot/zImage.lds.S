OUTPUT_ARCH(powerpc:common)
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
    *(.got2)
  }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .dynamic :
  {
    __dynamic_start = .;
    *(.dynamic)
  }
  .hash : { *(.hash) }
  .interp : { *(.interp) }
  .rela.dyn : { *(.rela*) }

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
