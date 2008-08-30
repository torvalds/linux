#ifdef CONFIG_PPC64
#define PROVIDE32(x)	PROVIDE(__unused__##x)
#else
#define PROVIDE32(x)	PROVIDE(x)
#endif
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>

ENTRY(_stext)

PHDRS {
	kernel PT_LOAD FLAGS(7); /* RWX */
	notes PT_NOTE FLAGS(0);
	dummy PT_NOTE FLAGS(0);

	/* binutils < 2.18 has a bug that makes it misbehave when taking an
	   ELF file with all segments at load address 0 as input.  This
	   happens when running "strip" on vmlinux, because of the AT() magic
	   in this linker script.  People using GCC >= 4.2 won't run into
	   this problem, because the "build-id" support will put some data
	   into the "notes" segment (at a non-zero load address).

	   To work around this, we force some data into both the "dummy"
	   segment and the kernel segment, so the dummy segment will get a
	   non-zero load address.  It's not enough to always create the
	   "notes" segment, since if nothing gets assigned to it, its load
	   address will be zero.  */
}

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
	EXIT_DATA
	}

	. = KERNELBASE;

/*
 * Text, read only data and other permanent read-only sections
 */

	/* Text and gots */
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		ALIGN_FUNCTION();
		*(.text.head)
		_text = .;
		*(.text .fixup .text.init.refok .exit.text.refok __ftr_alt_*)
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT

#ifdef CONFIG_PPC32
		*(.got1)
		__got2_start = .;
		*(.got2)
		__got2_end = .;
#endif /* CONFIG_PPC32 */

	} :kernel

	. = ALIGN(PAGE_SIZE);
	_etext = .;
	PROVIDE32 (etext = .);

	/* Read-only data */
	RODATA

	/* Exception & bug tables */
	__ex_table : AT(ADDR(__ex_table) - LOAD_OFFSET) {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	NOTES :kernel :notes

	/* The dummy segment contents for the bug workaround mentioned above
	   near PHDRS.  */
	.dummy : AT(ADDR(.dummy) - LOAD_OFFSET) {
		LONG(0xf177)
	} :kernel :dummy

/*
 * Init sections discarded at runtime
 */
	. = ALIGN(PAGE_SIZE);
	__init_begin = .;

	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	} :kernel

	/* .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
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
	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
		}

	.con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	SECURITY_INIT

	. = ALIGN(8);
	__ftr_fixup : AT(ADDR(__ftr_fixup) - LOAD_OFFSET) {
		__start___ftr_fixup = .;
		*(__ftr_fixup)
		__stop___ftr_fixup = .;
	}
	. = ALIGN(8);
	__lwsync_fixup : AT(ADDR(__lwsync_fixup) - LOAD_OFFSET) {
		__start___lwsync_fixup = .;
		*(__lwsync_fixup)
		__stop___lwsync_fixup = .;
	}
#ifdef CONFIG_PPC64
	. = ALIGN(8);
	__fw_ftr_fixup : AT(ADDR(__fw_ftr_fixup) - LOAD_OFFSET) {
		__start___fw_ftr_fixup = .;
		*(__fw_ftr_fixup)
		__stop___fw_ftr_fixup = .;
	}
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif
	. = ALIGN(PAGE_SIZE);
	.data.percpu  : AT(ADDR(.data.percpu) - LOAD_OFFSET) {
		__per_cpu_start = .;
		*(.data.percpu)
		*(.data.percpu.shared_aligned)
		__per_cpu_end = .;
	}

	. = ALIGN(8);
	.machine.desc : AT(ADDR(.machine.desc) - LOAD_OFFSET) {
		__machine_desc_start = . ;
		*(.machine.desc)
		__machine_desc_end = . ;
	}
	. = ALIGN(8);
	.dynsym : AT(ADDR(.dynsym) - LOAD_OFFSET) { *(.dynsym) }
	.dynstr : AT(ADDR(.dynstr) - LOAD_OFFSET) { *(.dynstr) }
	.dynamic : AT(ADDR(.dynamic) - LOAD_OFFSET)
	{
		__dynamic_start = .;
		*(.dynamic)
	}
	.hash : AT(ADDR(.hash) - LOAD_OFFSET) { *(.hash) }
	.interp : AT(ADDR(.interp) - LOAD_OFFSET) { *(.interp) }
	.rela.dyn : AT(ADDR(.rela.dyn) - LOAD_OFFSET)
	{
		__rela_dyn_start = .;
		*(.rela*)
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
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		DATA_DATA
		*(.sdata)
		*(.got.plt) *(.got)
	}
#else
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		DATA_DATA
		*(.data.rel*)
		*(.toc1)
		*(.branch_lt)
	}

	.opd : AT(ADDR(.opd) - LOAD_OFFSET) {
		*(.opd)
	}

	.got : AT(ADDR(.got) - LOAD_OFFSET) {
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
	.data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
		*(.data.init_task)
	}

	. = ALIGN(PAGE_SIZE);
	.data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET) {
		*(.data.page_aligned)
	}

	.data.cacheline_aligned : AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET) {
		*(.data.cacheline_aligned)
	}

	. = ALIGN(L1_CACHE_BYTES);
	.data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET) {
		*(.data.read_mostly)
	}

	. = ALIGN(PAGE_SIZE);
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;
	}

/*
 * And finally the bss
 */

	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
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
