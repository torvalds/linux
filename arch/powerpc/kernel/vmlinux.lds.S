/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_PPC64
#define PROVIDE32(x)	PROVIDE(__unused__##x)
#else
#define PROVIDE32(x)	PROVIDE(x)
#endif

#define BSS_FIRST_SECTIONS *(.bss.prominit)
#define EMITS_PT_NOTE
#define RO_EXCEPTION_TABLE_ALIGN	0

#define SOFT_MASK_TABLE(align)						\
	. = ALIGN(align);						\
	__soft_mask_table : AT(ADDR(__soft_mask_table) - LOAD_OFFSET) {	\
		__start___soft_mask_table = .;				\
		KEEP(*(__soft_mask_table))				\
		__stop___soft_mask_table = .;				\
	}

#define RESTART_TABLE(align)						\
	. = ALIGN(align);						\
	__restart_table : AT(ADDR(__restart_table) - LOAD_OFFSET) {	\
		__start___restart_table = .;				\
		KEEP(*(__restart_table))				\
		__stop___restart_table = .;				\
	}

#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/thread_info.h>

#define STRICT_ALIGN_SIZE	(1 << CONFIG_DATA_SHIFT)

ENTRY(_stext)

PHDRS {
	text PT_LOAD FLAGS(7); /* RWX */
	note PT_NOTE FLAGS(0);
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

	_text = .;
	_stext = .;

	/*
	 * Head text.
	 * This needs to be in its own output section to avoid ld placing
	 * branch trampoline stubs randomly throughout the fixed sections,
	 * which it will do (even if the branch comes from another section)
	 * in order to optimize stub generation.
	 */
	.head.text : AT(ADDR(.head.text) - LOAD_OFFSET) {
#ifdef CONFIG_PPC64
		KEEP(*(.head.text.first_256B));
#ifdef CONFIG_PPC_BOOK3E
#else
		KEEP(*(.head.text.real_vectors));
		*(.head.text.real_trampolines);
		KEEP(*(.head.text.virt_vectors));
		*(.head.text.virt_trampolines);
# if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
		KEEP(*(.head.data.fwnmi_page));
# endif
#endif
#else /* !CONFIG_PPC64 */
		HEAD_TEXT
#endif
	} :text

	__head_end = .;

#ifdef CONFIG_PPC64
	/*
	 * ALIGN(0) overrides the default output section alignment because
	 * this needs to start right after .head.text in order for fixed
	 * section placement to work.
	 */
	.text ALIGN(0) : AT(ADDR(.text) - LOAD_OFFSET) {
#ifdef CONFIG_LD_HEAD_STUB_CATCH
		KEEP(*(.linker_stub_catch));
		. = . ;
#endif

#else
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		ALIGN_FUNCTION();
#endif
		/* careful! __ftr_alt_* sections need to be close to .text */
		*(.text.hot .text.hot.* TEXT_MAIN .text.fixup .text.unlikely .text.unlikely.* .fixup __ftr_alt_* .ref.text);
#ifdef CONFIG_PPC64
		*(.tramp.ftrace.text);
#endif
		NOINSTR_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		/*
		 * -Os builds call FP save/restore functions. The powerpc64
		 * linker generates those on demand in the .sfpr section.
		 * .sfpr gets placed at the beginning of a group of input
		 * sections, which can break start-of-text offset if it is
		 * included with the main text sections, so put it by itself.
		 */
		*(.sfpr);
		MEM_KEEP(init.text)
		MEM_KEEP(exit.text)

#ifdef CONFIG_PPC32
		*(.got1)
		__got2_start = .;
		*(.got2)
		__got2_end = .;
#endif /* CONFIG_PPC32 */

	} :text

	. = ALIGN(PAGE_SIZE);
	_etext = .;
	PROVIDE32 (etext = .);

	/* Read-only data */
	RO_DATA(PAGE_SIZE)

#ifdef CONFIG_PPC64
	SOFT_MASK_TABLE(8)
	RESTART_TABLE(8)

	.opd : AT(ADDR(.opd) - LOAD_OFFSET) {
		__start_opd = .;
		KEEP(*(.opd))
		__end_opd = .;
	}

	. = ALIGN(8);
	__stf_entry_barrier_fixup : AT(ADDR(__stf_entry_barrier_fixup) - LOAD_OFFSET) {
		__start___stf_entry_barrier_fixup = .;
		*(__stf_entry_barrier_fixup)
		__stop___stf_entry_barrier_fixup = .;
	}

	. = ALIGN(8);
	__uaccess_flush_fixup : AT(ADDR(__uaccess_flush_fixup) - LOAD_OFFSET) {
		__start___uaccess_flush_fixup = .;
		*(__uaccess_flush_fixup)
		__stop___uaccess_flush_fixup = .;
	}

	. = ALIGN(8);
	__entry_flush_fixup : AT(ADDR(__entry_flush_fixup) - LOAD_OFFSET) {
		__start___entry_flush_fixup = .;
		*(__entry_flush_fixup)
		__stop___entry_flush_fixup = .;
	}

	. = ALIGN(8);
	__scv_entry_flush_fixup : AT(ADDR(__scv_entry_flush_fixup) - LOAD_OFFSET) {
		__start___scv_entry_flush_fixup = .;
		*(__scv_entry_flush_fixup)
		__stop___scv_entry_flush_fixup = .;
	}

	. = ALIGN(8);
	__stf_exit_barrier_fixup : AT(ADDR(__stf_exit_barrier_fixup) - LOAD_OFFSET) {
		__start___stf_exit_barrier_fixup = .;
		*(__stf_exit_barrier_fixup)
		__stop___stf_exit_barrier_fixup = .;
	}

	. = ALIGN(8);
	__rfi_flush_fixup : AT(ADDR(__rfi_flush_fixup) - LOAD_OFFSET) {
		__start___rfi_flush_fixup = .;
		*(__rfi_flush_fixup)
		__stop___rfi_flush_fixup = .;
	}
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC_BARRIER_NOSPEC
	. = ALIGN(8);
	__spec_barrier_fixup : AT(ADDR(__spec_barrier_fixup) - LOAD_OFFSET) {
		__start___barrier_nospec_fixup = .;
		*(__barrier_nospec_fixup)
		__stop___barrier_nospec_fixup = .;
	}
#endif /* CONFIG_PPC_BARRIER_NOSPEC */

#ifdef CONFIG_PPC_FSL_BOOK3E
	. = ALIGN(8);
	__spec_btb_flush_fixup : AT(ADDR(__spec_btb_flush_fixup) - LOAD_OFFSET) {
		__start__btb_flush_fixup = .;
		*(__btb_flush_fixup)
		__stop__btb_flush_fixup = .;
	}
#endif

/*
 * Init sections discarded at runtime
 */
	. = ALIGN(STRICT_ALIGN_SIZE);
	__init_begin = .;
	. = ALIGN(PAGE_SIZE);
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		_sinittext = .;
		INIT_TEXT

		/*
		 *.init.text might be RO so we must ensure this section ends on
		 * a page boundary.
		 */
		. = ALIGN(PAGE_SIZE);
		_einittext = .;
#ifdef CONFIG_PPC64
		*(.tramp.ftrace.init);
#endif
	} :text

	/* .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	. = ALIGN(PAGE_SIZE);

	INIT_DATA_SECTION(16)

	. = ALIGN(8);
	__ftr_fixup : AT(ADDR(__ftr_fixup) - LOAD_OFFSET) {
		__start___ftr_fixup = .;
		KEEP(*(__ftr_fixup))
		__stop___ftr_fixup = .;
	}
	. = ALIGN(8);
	__mmu_ftr_fixup : AT(ADDR(__mmu_ftr_fixup) - LOAD_OFFSET) {
		__start___mmu_ftr_fixup = .;
		KEEP(*(__mmu_ftr_fixup))
		__stop___mmu_ftr_fixup = .;
	}
	. = ALIGN(8);
	__lwsync_fixup : AT(ADDR(__lwsync_fixup) - LOAD_OFFSET) {
		__start___lwsync_fixup = .;
		KEEP(*(__lwsync_fixup))
		__stop___lwsync_fixup = .;
	}
#ifdef CONFIG_PPC64
	. = ALIGN(8);
	__fw_ftr_fixup : AT(ADDR(__fw_ftr_fixup) - LOAD_OFFSET) {
		__start___fw_ftr_fixup = .;
		KEEP(*(__fw_ftr_fixup))
		__stop___fw_ftr_fixup = .;
	}
#endif

	PERCPU_SECTION(L1_CACHE_BYTES)

	. = ALIGN(8);
	.machine.desc : AT(ADDR(.machine.desc) - LOAD_OFFSET) {
		__machine_desc_start = . ;
		KEEP(*(.machine.desc))
		__machine_desc_end = . ;
	}
#ifdef CONFIG_RELOCATABLE
	. = ALIGN(8);
	.dynsym : AT(ADDR(.dynsym) - LOAD_OFFSET)
	{
#ifdef CONFIG_PPC32
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
	.gnu.hash : AT(ADDR(.gnu.hash) - LOAD_OFFSET) { *(.gnu.hash) }
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
#ifdef CONFIG_UBSAN
		*(.data..Lubsan_data*)
		*(.data..Lubsan_type*)
#endif
		*(.data.rel*)
		*(SDATA_MAIN)
		*(.sdata2)
		*(.got.plt) *(.got)
		*(.plt)
		*(.branch_lt)
	}
#else
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		DATA_DATA
#ifdef CONFIG_UBSAN
		*(.data..Lubsan_data*)
		*(.data..Lubsan_type*)
#endif
		*(.data.rel*)
		*(.toc1)
		*(.branch_lt)
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
	INIT_TASK_DATA_SECTION(THREAD_ALIGN)

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

	BUG_TABLE

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

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
	/DISCARD/ : {
		*(*.EMB.apuinfo)
		*(.glink .iplt .plt .rela* .comment)
		*(.gnu.version*)
		*(.gnu.attributes)
		*(.eh_frame)
	}
}
