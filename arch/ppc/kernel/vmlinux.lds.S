#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(powerpc:common)
jiffies = jiffies_64 + 4;
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = + SIZEOF_HEADERS;
  .interp : { *(.interp) }
  .hash          : { *(.hash)		}
  .gnu.hash      : { *(.gnu.hash)	}
  .dynsym        : { *(.dynsym)		}
  .dynstr        : { *(.dynstr)		}
  .rel.text      : { *(.rel.text)		}
  .rela.text     : { *(.rela.text) 	}
  .rel.data      : { *(.rel.data)		}
  .rela.data     : { *(.rela.data) 	}
  .rel.rodata    : { *(.rel.rodata) 	}
  .rela.rodata   : { *(.rela.rodata) 	}
  .rel.got       : { *(.rel.got)		}
  .rela.got      : { *(.rela.got)		}
  .rel.ctors     : { *(.rel.ctors)	}
  .rela.ctors    : { *(.rela.ctors)	}
  .rel.dtors     : { *(.rel.dtors)	}
  .rela.dtors    : { *(.rela.dtors)	}
  .rel.bss       : { *(.rel.bss)		}
  .rela.bss      : { *(.rela.bss)		}
  .rel.plt       : { *(.rel.plt)		}
  .rela.plt      : { *(.rela.plt)		}
/*  .init          : { *(.init)	} =0*/
  .plt : { *(.plt) }
  .text      :
  {
    *(.text)
    SCHED_TEXT
    LOCK_TEXT
    *(.fixup)
    *(.got1)
    __got2_start = .;
    *(.got2)
    __got2_end = .;
  }
  _etext = .;
  PROVIDE (etext = .);

  RODATA
  .fini      : { *(.fini)    } =0
  .ctors     : { *(.ctors)   }
  .dtors     : { *(.dtors)   }

  .fixup   : { *(.fixup) }

	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	__bug_table : {
		__start___bug_table = .;
		*(__bug_table)
		__stop___bug_table = .;
	}

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
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata  =  .;
  PROVIDE (edata = .);

  . = ALIGN(8192);
  .data.init_task : { *(.data.init_task) }

  . = ALIGN(4096);
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  /* .exit.text is discarded at runtime, not link time,
     to deal with references from __bug_table */
  .exit.text : { *(.exit.text) }
  .init.data : {
    *(.init.data);
    __vtop_table_begin = .;
    *(.vtop_fixup);
    __vtop_table_end = .;
    __ptov_table_begin = .;
    *(.ptov_fixup);
    __ptov_table_end = .;
  }
  . = ALIGN(16);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : {
	INITCALLS
  }
  __initcall_end = .;

  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;

  SECURITY_INIT

  __start___ftr_fixup = .;
  __ftr_fixup : { *(__ftr_fixup) }
  __stop___ftr_fixup = .;

  . = ALIGN(32);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;

  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;

  . = ALIGN(4096);
  __init_end = .;

  . = ALIGN(4096);
  _sextratext = .;
  _eextratext = .;

  __bss_start = .;
  .bss       :
  {
   *(.sbss) *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }
  __bss_stop = .;

  _end = . ;
  PROVIDE (end = .);

  /* Sections to be discarded. */
  /DISCARD/ : {
    *(.exitcall.exit)
    *(.exit.data)
  }
}
