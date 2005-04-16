OUTPUT_ARCH(powerpc:common)
SEARCH_DIR(/lib); SEARCH_DIR(/usr/lib); SEARCH_DIR(/usr/local/lib); SEARCH_DIR(/usr/local/powerpc-any-elf/lib);
/* Do we need any of these for elf?
   __DYNAMIC = 0;    */
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = + SIZEOF_HEADERS;
  .interp : { *(.interp) }
  .hash          : { *(.hash)      }
  .dynsym        : { *(.dynsym)        }
  .dynstr        : { *(.dynstr)        }
  .rel.text      : { *(.rel.text)      }
  .rela.text     : { *(.rela.text)     }
  .rel.data      : { *(.rel.data)      }
  .rela.data     : { *(.rela.data)     }
  .rel.rodata    : { *(.rel.rodata)    }
  .rela.rodata   : { *(.rela.rodata)   }
  .rel.got       : { *(.rel.got)       }
  .rela.got      : { *(.rela.got)      }
  .rel.ctors     : { *(.rel.ctors) }
  .rela.ctors    : { *(.rela.ctors)    }
  .rel.dtors     : { *(.rel.dtors) }
  .rela.dtors    : { *(.rela.dtors)    }
  .rel.bss       : { *(.rel.bss)       }
  .rela.bss      : { *(.rela.bss)      }
  .rel.plt       : { *(.rel.plt)       }
  .rela.plt      : { *(.rela.plt)      }
  .plt : { *(.plt) }
  .text      :
  {
    *(.text)
    *(.fixup)
    *(.got1)
  }
  . = ALIGN(4096);
  _etext = .;
  PROVIDE (etext = .);
  .rodata    :
  {
    *(.rodata)
    *(.rodata1)
  }
  .kstrtab   : { *(.kstrtab) }
  __vermagic : { *(__vermagic) }
  .fini      : { *(.fini)    } =0
  .ctors     : { *(.ctors)   }
  .dtors     : { *(.dtors)   }
  /* Read-write section, merged into data segment: */
  . = ALIGN(4096);
  .data    :
  {
    *(.data)
    *(.data1)
    *(.sdata)
    *(.sdata2)
    *(.got.plt) *(.got)
    *(.dynamic)
    CONSTRUCTORS
  }

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
  PROVIDE (edata = .);

  .fixup   : { *(.fixup) }

  . = ALIGN(4096);
  __bss_start = .;
  .bss       :
  {
   *(.sbss) *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }
  . = ALIGN(4096);
  _end = . ;
  PROVIDE (end = .);
}
