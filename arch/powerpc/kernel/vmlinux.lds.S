#include <linux/config.h>
#ifdef CONFIG_PPC64
#include <asm/page.h>
#endif
#include <asm-generic/vmlinux.lds.h>

#ifdef CONFIG_PPC64
OUTPUT_ARCH(powerpc:common64)
jiffies = jiffies_64;
#else
OUTPUT_ARCH(powerpc:common)
jiffies = jiffies_64 + 4;
#endif
SECTIONS
{
  /* Sections to be discarded. */
  /DISCARD/ : {
    *(.exitcall.exit)
#ifdef CONFIG_PPC32
    *(.exit.data)
#endif
  }


  /* Read-only sections, merged into text segment: */
#ifdef CONFIG_PPC32
  . = + SIZEOF_HEADERS;
  .interp : { *(.interp) }
  .hash          : { *(.hash)		}
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
#endif
  .text : {
#ifdef CONFIG_PPC64
    *(.text .text.*)
#else
    *(.text)
#endif
    SCHED_TEXT
    LOCK_TEXT
#ifdef CONFIG_PPC64
    KPROBES_TEXT
#endif
    *(.fixup)
#ifdef CONFIG_PPC32
    *(.got1)
    __got2_start = .;
    *(.got2)
    __got2_end = .;
#else
    . = ALIGN(PAGE_SIZE);
    _etext = .;
#endif
  }
#ifdef CONFIG_PPC32
  _etext = .;
  PROVIDE (etext = .);

  RODATA
  .fini      : { *(.fini)    } =0
  .ctors     : { *(.ctors)   }
  .dtors     : { *(.dtors)   }

  .fixup   : { *(.fixup) }
#endif

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

#ifdef CONFIG_PPC64
	__ftr_fixup : {
		__start___ftr_fixup = .;
		*(__ftr_fixup)
		__stop___ftr_fixup = .;
	}

  RODATA
#endif

#ifdef CONFIG_PPC32
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
#else
  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);
#endif
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
#ifdef CONFIG_PPC32
  /* .exit.text is discarded at runtime, not link time,
     to deal with references from __bug_table */
  .exit.text : { *(.exit.text) }
#endif
  .init.data : {
#ifdef CONFIG_PPC64
    *(.init.data)
#else
    *(.init.data);
    __vtop_table_begin = .;
    *(.vtop_fixup);
    __vtop_table_end = .;
    __ptov_table_begin = .;
    *(.ptov_fixup);
    __ptov_table_end = .;
#endif
  }

  . = ALIGN(16);
#ifdef CONFIG_PPC32
  __setup_start = .;
#endif
  .init.setup : {
#ifdef CONFIG_PPC64
    __setup_start = .;
#endif
    *(.init.setup)
#ifdef CONFIG_PPC64
    __setup_end = .;
#endif
  }
#ifdef CONFIG_PPC32
  __setup_end = .;

  __initcall_start = .;
#endif
  .initcall.init : {
#ifdef CONFIG_PPC64
	__initcall_start = .;
#endif
	*(.initcall1.init)
	*(.initcall2.init)
	*(.initcall3.init)
	*(.initcall4.init)
	*(.initcall5.init)
	*(.initcall6.init)
	*(.initcall7.init)
#ifdef CONFIG_PPC64
	__initcall_end = .;
#endif
  }
#ifdef CONFIG_PPC32
  __initcall_end = .;

  __con_initcall_start = .;
#endif
  .con_initcall.init : {
#ifdef CONFIG_PPC64
    __con_initcall_start = .;
#endif
    *(.con_initcall.init)
#ifdef CONFIG_PPC64
    __con_initcall_end = .;
#endif
  }
#ifdef CONFIG_PPC32
  __con_initcall_end = .;
#endif

  SECURITY_INIT

#ifdef CONFIG_PPC32
  __start___ftr_fixup = .;
  __ftr_fixup : { *(__ftr_fixup) }
  __stop___ftr_fixup = .;
#else
  . = ALIGN(PAGE_SIZE);
  .init.ramfs : {
    __initramfs_start = .;
    *(.init.ramfs)
    __initramfs_end = .;
  }
#endif

#ifdef CONFIG_PPC32
  . = ALIGN(32);
  __per_cpu_start = .;
#endif
  .data.percpu : {
#ifdef CONFIG_PPC64
    __per_cpu_start = .;
#endif
    *(.data.percpu)
#ifdef CONFIG_PPC64
    __per_cpu_end = .;
#endif
  }
#ifdef CONFIG_PPC32
  __per_cpu_end = .;
#endif

#ifdef CONFIG_PPC64
 . = ALIGN(PAGE_SIZE);
 . = ALIGN(16384);
 __init_end = .;
 /* freed after init ends here */


 /* Read/write sections */
 . = ALIGN(PAGE_SIZE);
 . = ALIGN(16384);
 /* The initial task and kernel stack */
 .data.init_task : {
      *(.data.init_task)
      }

 . = ALIGN(PAGE_SIZE);
 .data.page_aligned : {
      *(.data.page_aligned)
      }

 .data.cacheline_aligned : {
      *(.data.cacheline_aligned)
      }

 .data : {
      *(.data .data.rel* .toc1)
      *(.branch_lt)
      }

 .opd : {
      *(.opd)
      }

 .got : {
      __toc_start = .;
      *(.got)
      *(.toc)
      . = ALIGN(PAGE_SIZE);
      _edata = .;
      }


  . = ALIGN(PAGE_SIZE);
#else
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : {
    *(.init.ramfs)
  }
  __initramfs_end = .;

  . = ALIGN(4096);
  __init_end = .;

  . = ALIGN(4096);
  _sextratext = .;
  _eextratext = .;

  __bss_start = .;
#endif
  .bss : {
#ifdef CONFIG_PPC64
    __bss_start = .;
#else
   *(.sbss) *(.scommon)
   *(.dynbss)
#endif
   *(.bss)
#ifdef CONFIG_PPC32
   *(COMMON)
#else
  __bss_stop = .;
#endif
  }
#ifdef CONFIG_PPC32
  __bss_stop = .;
#endif

#ifdef CONFIG_PPC64
  . = ALIGN(PAGE_SIZE);
#endif
  _end = . ;
#ifdef CONFIG_PPC32
  PROVIDE (end = .);
#endif
}
