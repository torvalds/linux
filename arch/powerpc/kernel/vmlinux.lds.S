#ifdef CONFIG_PPC64
#include <asm/page.h>
#define PROVIDE32(x)	PROVIDE(__unused__##x)
#else
#define PAGE_SIZE	4096
#define KERNELBASE	CONFIG_KERNEL_START
#define PROVIDE32(x)	PROVIDE(x)
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

/*
 * Text, read only data and other permanent read-only sections
 */

	/* Text and gots */
	.text : {
		_text = .;
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
#endif /* CONFIG_PPC32 */

		. = ALIGN(PAGE_SIZE);
		_etext = .;
		PROVIDE32 (etext = .);
	}

	/* Read-only data */
	RODATA

	/* Exception & bug tables */
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

/*
 * Init sections discarded at runtime
 */
	. = ALIGN(PAGE_SIZE);
	__init_begin = .;

	.init.text : {
		_sinittext = .;
		*(.init.text)
		_einittext = .;
	}

	/* .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	 */
	.exit.text : { *(.exit.text) }

	.init.data : {
		*(.init.data);
		__vtop_table_begin = .;
		*(.vtop_fixup);
		__vtop_table_end = .;
		__ptov_table_begin = .;
		*(.ptov_fixup);
		__ptov_table_end = .;
#ifdef CONFIG_PPC_ISERIES
		__dt_strings_start = .;
		*(.dt_strings);
		__dt_strings_end = .;
#endif
	}

	. = ALIGN(16);
	.init.setup : {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
		}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	SECURITY_INIT

	. = ALIGN(8);
	__ftr_fixup : {
		__start___ftr_fixup = .;
		*(__ftr_fixup)
		__stop___ftr_fixup = .;
	}
#ifdef CONFIG_PPC64
	. = ALIGN(8);
	__fw_ftr_fixup : {
		__start___fw_ftr_fixup = .;
		*(__fw_ftr_fixup)
		__stop___fw_ftr_fixup = .;
	}
#endif

	. = ALIGN(PAGE_SIZE);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}

#ifdef CONFIG_PPC32
	. = ALIGN(32);
#else
	. = ALIGN(128);
#endif
	.data.percpu : {
		__per_cpu_start = .;
		*(.data.percpu)
		__per_cpu_end = .;
	}

	. = ALIGN(8);
	.machine.desc : {
		__machine_desc_start = . ;
		*(.machine.desc)
		__machine_desc_end = . ;
	}

	/* freed after init ends here */
	. = ALIGN(PAGE_SIZE);
	__init_end = .;

/*
 * And now the various read/write data
 */

	. = ALIGN(PAGE_SIZE);
	_sdata = .;

#ifdef CONFIG_PPC32
	.data    :
	{
		*(.data)
		*(.sdata)
		*(.got.plt) *(.got)
	}
#else
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
	}
#endif

	. = ALIGN(PAGE_SIZE);
	_edata  =  .;
	PROVIDE32 (edata = .);

	/* The initial task and kernel stack */
#ifdef CONFIG_PPC32
	. = ALIGN(8192);
#else
	. = ALIGN(16384);
#endif
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

	. = ALIGN(PAGE_SIZE);
	__data_nosave : {
		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;
	}

/*
 * And finally the bss
 */

	.bss : {
		__bss_start = .;
		*(.sbss) *(.scommon)
		*(.dynbss)
		*(.bss)
		*(COMMON)
		__bss_stop = .;
	}

	. = ALIGN(PAGE_SIZE);
	_end = . ;
	PROVIDE32 (end = .);
}
