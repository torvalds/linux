#include <linux/config.h>
#ifdef CONFIG_PPC64
#include <asm/page.h>
#else
#define PAGE_SIZE	4096
#define KERNELBASE	CONFIG_KERNEL_START
#endif
#include <asm-generic/vmlinux.lds.h>

ENTRY(_stext)

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
    *(.exit.data)
  }

  . = KERNELBASE;

  /* Read-only sections, merged into text segment: */
  .text : {
    *(.text .text.*)
    SCHED_TEXT
    LOCK_TEXT
    KPROBES_TEXT
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
  . = ALIGN(PAGE_SIZE);
  _sdata = .;
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

  . = ALIGN(PAGE_SIZE);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(PAGE_SIZE);
  __nosave_end = .;

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata  =  .;
  PROVIDE (edata = .);

  . = ALIGN(8192);
  .data.init_task : { *(.data.init_task) }
#endif

  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);
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
    *(.init.data);
    __vtop_table_begin = .;
    *(.vtop_fixup);
    __vtop_table_end = .;
    __ptov_table_begin = .;
    *(.ptov_fixup);
    __ptov_table_end = .;
  }

  . = ALIGN(16);
  .init.setup : {
    __setup_start = .;
    *(.init.setup)
    __setup_end = .;
  }

  .initcall.init : {
	__initcall_start = .;
	*(.initcall1.init)
	*(.initcall2.init)
	*(.initcall3.init)
	*(.initcall4.init)
	*(.initcall5.init)
	*(.initcall6.init)
	*(.initcall7.init)
	__initcall_end = .;
  }

  .con_initcall.init : {
    __con_initcall_start = .;
    *(.con_initcall.init)
    __con_initcall_end = .;
  }

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
#endif
  .data.percpu : {
    __per_cpu_start = .;
    *(.data.percpu)
    __per_cpu_end = .;
  }

 . = ALIGN(PAGE_SIZE);
#ifdef CONFIG_PPC64
 . = ALIGN(16384);
 __init_end = .;
 /* freed after init ends here */

 /* Read/write sections */
 . = ALIGN(PAGE_SIZE);
 . = ALIGN(16384);
 _sdata = .;
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
    __bss_start = .;
   *(.sbss) *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
  __bss_stop = .;
  }

#ifdef CONFIG_PPC64
  . = ALIGN(PAGE_SIZE);
#endif
  _end = . ;
#ifdef CONFIG_PPC32
  PROVIDE (end = .);
#endif
}
