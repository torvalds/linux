#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(powerpc:common64)
jiffies = jiffies_64;
SECTIONS
{
  /* Sections to be discarded. */
  /DISCARD/ : {
	*(.exitcall.exit)
	}


  /* Read-only sections, merged into text segment: */
  .text : {
	*(.text .text.*)
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	. = ALIGN(PAGE_SIZE);
	_etext = .;
	}

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

  __ftr_fixup : {
	__start___ftr_fixup = .;
	*(__ftr_fixup)
	__stop___ftr_fixup = .;
	}

  RODATA


  /* will be freed after init */
  . = ALIGN(PAGE_SIZE);
  __init_begin = .;

  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
	}

  .init.data : {
	*(.init.data)
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

  . = ALIGN(PAGE_SIZE);
  .init.ramfs : {
	__initramfs_start = .;
	*(.init.ramfs)
	__initramfs_end = .;
	}

  .data.percpu : {
	__per_cpu_start = .;
	*(.data.percpu)
	__per_cpu_end = .;
	}

  . = ALIGN(PAGE_SIZE);
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
  .bss : {
	__bss_start = .;
	*(.bss)
	__bss_stop = .;
	}

  . = ALIGN(PAGE_SIZE);
  _end = . ;
}
