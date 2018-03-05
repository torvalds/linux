#ifdef CONFIG_PPC64
#define PROVIDE32(x)	PROVIDE(__unused__##x)
#else
#define PROVIDE32(x)	PROVIDE(x)
#endif
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

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
	. = KERNELBASE;

/*
 * Text, read only data and other permanent read-only sections
 */

	/* Text and gots */
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		ALIGN_FUNCTION();
		HEAD_TEXT
		_text = .;
		/* careful! __ftr_alt_* sections need to be close to .text */
		*(.text .fixup __ftr_alt_* .ref.text)
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT

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

#ifdef CONFIG_PPC64
	. = ALIGN(8);
	__rfi_flush_fixup : AT(ADDR(__rfi_flush_fixup) - LOAD_OFFSET) {
		__start___rfi_flush_fixup = .;
		*(__rfi_flush_fixup)
		__stop___rfi_flush_fixup = .;
	}
#endif

	EXCEPTION_TABLE(0)

	NOTES :kernel :notes

	/* The dummy segment contents for the bug workaround mentioned above
	   near PHDRS.  */
	.dummy : AT(ADDR(.dummy) - LOAD_OFFSET) {
		LONG(0)
		LONG(0)
		LONG(0)
	} :kernel :dummy

/*
 * Init sections discarded at runtime
 */
	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE) :kernel

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
	}

	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		INIT_SETUP(16)
	}

	.initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
		INIT_CALLS
	}

	.con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
		CON_INITCALL
	}

	SECURITY_INIT

	. = ALIGN(8);
	__ftr_fixup : AT(ADDR(__ftr_fixup) - LOAD_OFFSET) {
		__start___ftr_fixup = .;
		*(__ftr_fixup)
		__stop___ftr_fixup = .;
	}
	. = ALIGN(8);
	__mmu_ftr_fixup : AT(ADDR(__mmu_ftr_fixup) - LOAD_OFFSET) {
		__start___mmu_ftr_fixup = .;
		*(__mmu_ftr_fixup)
		__stop___mmu_ftr_fixup = .;
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
	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		INIT_RAM_FS
	}

	PERCPU_SECTION(L1_CACHE_BYTES)

	. = ALIGN(8);
	.machine.desc : AT(ADDR(.machine.desc) - LOAD_OFFSET) {
		__machine_desc_start = . ;
		*(.machine.desc)
		__machine_desc_end = . ;
	}
#ifdef CONFIG_RELOCATABLE
	. = ALIGN(8);
	.dynsym : AT(ADDR(.dynsym) - LOAD_OFFSET)
	{
#ifdef CONFIG_RELOCATABLE_PPC32
		__dynamic_symtab = .;
#endif
		*(.dynsym)
	}
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
#endif
	/* .exit.data is discarded at runtime, not link time,
	 * to deal with references from .exit.text
	 */
	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
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

	. = ALIGN(256);
	.got : AT(ADDR(.got) - LOAD_OFFSET) {
		__toc_start = .;
#ifndef CONFIG_RELOCATABLE
		__prom_init_toc_start = .;
		arch/powerpc/kernel/prom_init.o*(.toc .got)
		__prom_init_toc_end = .;
#endif
		*(.got)
		*(.toc)
	}
#endif

	/* The initial task and kernel stack */
	INIT_TASK_DATA_SECTION(THREAD_SIZE)

	.data..page_aligned : AT(ADDR(.data..page_aligned) - LOAD_OFFSET) {
		PAGE_ALIGNED_DATA(PAGE_SIZE)
	}

	.data..cacheline_aligned : AT(ADDR(.data..cacheline_aligned) - LOAD_OFFSET) {
		CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)
	}

	.data..read_mostly : AT(ADDR(.data..read_mostly) - LOAD_OFFSET) {
		READ_MOSTLY_DATA(L1_CACHE_BYTES)
	}

	. = ALIGN(PAGE_SIZE);
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		NOSAVE_DATA
	}

	. = ALIGN(PAGE_SIZE);
	_edata  =  .;
	PROVIDE32 (edata = .);

/*
 * And finally the bss
 */

	BSS_SECTION(0, 0, 0)

	. = ALIGN(PAGE_SIZE);
	_end = . ;
	PROVIDE32 (end = .);

	/* Sections to be discarded. */
	DISCARDS
}
