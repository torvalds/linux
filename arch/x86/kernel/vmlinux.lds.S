/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ld script for the x86 kernel
 *
 * Historic 32-bit version written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Modernisation, unification and other changes and fixes:
 *   Copyright (C) 2007-2009  Sam Ravnborg <sam@ravnborg.org>
 *
 *
 * Don't define absolute symbols until and unless you know that symbol
 * value is should remain constant even if kernel image is relocated
 * at run time. Absolute symbols are not relocated. If symbol value should
 * change if kernel is relocated, make the symbol section relative and
 * put it inside the section definition.
 */

#define LOAD_OFFSET __START_KERNEL_map

#define RUNTIME_DISCARD_EXIT
#define EMITS_PT_NOTE
#define RO_EXCEPTION_TABLE_ALIGN	16

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/page_types.h>
#include <asm/orc_lookup.h>
#include <asm/cache.h>
#include <asm/boot.h>

#undef i386     /* in case the preprocessor is a 32bit one */

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT)

#ifdef CONFIG_X86_32
OUTPUT_ARCH(i386)
ENTRY(phys_startup_32)
#else
OUTPUT_ARCH(i386:x86-64)
ENTRY(phys_startup_64)
#endif

jiffies = jiffies_64;
const_pcpu_hot = pcpu_hot;

#if defined(CONFIG_X86_64)
/*
 * On 64-bit, align RODATA to 2MB so we retain large page mappings for
 * boundaries spanning kernel text, rodata and data sections.
 *
 * However, kernel identity mappings will have different RWX permissions
 * to the pages mapping to text and to the pages padding (which are freed) the
 * text section. Hence kernel identity mappings will be broken to smaller
 * pages. For 64-bit, kernel text and kernel identity mappings are different,
 * so we can enable protection checks as well as retain 2MB large page
 * mappings for kernel text.
 */
#define X86_ALIGN_RODATA_BEGIN	. = ALIGN(HPAGE_SIZE);

#define X86_ALIGN_RODATA_END					\
		. = ALIGN(HPAGE_SIZE);				\
		__end_rodata_hpage_align = .;			\
		__end_rodata_aligned = .;

#define ALIGN_ENTRY_TEXT_BEGIN	. = ALIGN(PMD_SIZE);
#define ALIGN_ENTRY_TEXT_END	. = ALIGN(PMD_SIZE);

/*
 * This section contains data which will be mapped as decrypted. Memory
 * encryption operates on a page basis. Make this section PMD-aligned
 * to avoid splitting the pages while mapping the section early.
 *
 * Note: We use a separate section so that only this section gets
 * decrypted to avoid exposing more than we wish.
 */
#define BSS_DECRYPTED						\
	. = ALIGN(PMD_SIZE);					\
	__start_bss_decrypted = .;				\
	*(.bss..decrypted);					\
	. = ALIGN(PAGE_SIZE);					\
	__start_bss_decrypted_unused = .;			\
	. = ALIGN(PMD_SIZE);					\
	__end_bss_decrypted = .;				\

#else

#define X86_ALIGN_RODATA_BEGIN
#define X86_ALIGN_RODATA_END					\
		. = ALIGN(PAGE_SIZE);				\
		__end_rodata_aligned = .;

#define ALIGN_ENTRY_TEXT_BEGIN
#define ALIGN_ENTRY_TEXT_END
#define BSS_DECRYPTED

#endif

PHDRS {
	text PT_LOAD FLAGS(5);          /* R_E */
	data PT_LOAD FLAGS(6);          /* RW_ */
#ifdef CONFIG_X86_64
#ifdef CONFIG_SMP
	percpu PT_LOAD FLAGS(6);        /* RW_ */
#endif
	init PT_LOAD FLAGS(7);          /* RWE */
#endif
	note PT_NOTE FLAGS(0);          /* ___ */
}

SECTIONS
{
	. = __START_KERNEL;
#ifdef CONFIG_X86_32
	phys_startup_32 = ABSOLUTE(startup_32 - LOAD_OFFSET);
#else
	phys_startup_64 = ABSOLUTE(startup_64 - LOAD_OFFSET);
#endif

	/* Text and read-only data */
	.text :  AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;
		_stext = .;
		/* bootstrapping code */
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		SOFTIRQENTRY_TEXT
#ifdef CONFIG_MITIGATION_RETPOLINE
		*(.text..__x86.indirect_thunk)
		*(.text..__x86.return_thunk)
#endif
		STATIC_CALL_TEXT

		ALIGN_ENTRY_TEXT_BEGIN
		*(.text..__x86.rethunk_untrain)
		ENTRY_TEXT

#ifdef CONFIG_MITIGATION_SRSO
		/*
		 * See the comment above srso_alias_untrain_ret()'s
		 * definition.
		 */
		. = srso_alias_untrain_ret | (1 << 2) | (1 << 8) | (1 << 14) | (1 << 20);
		*(.text..__x86.rethunk_safe)
#endif
		ALIGN_ENTRY_TEXT_END
		*(.gnu.warning)

	} :text = 0xcccccccc

	/* End of text section, which should occupy whole number of pages */
	_etext = .;
	. = ALIGN(PAGE_SIZE);

	X86_ALIGN_RODATA_BEGIN
	RO_DATA(PAGE_SIZE)
	X86_ALIGN_RODATA_END

	/* Data */
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		/* Start of data section */
		_sdata = .;

		/* init_task */
		INIT_TASK_DATA(THREAD_SIZE)

		/* equivalent to task_pt_regs(&init_task) */
		__top_init_kernel_stack = __end_init_stack - TOP_OF_KERNEL_STACK_PADDING - PTREGS_SIZE;

#ifdef CONFIG_X86_32
		/* 32 bit has nosave before _edata */
		NOSAVE_DATA
#endif

		PAGE_ALIGNED_DATA(PAGE_SIZE)

		CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)

		DATA_DATA
		CONSTRUCTORS

		/* rarely changed data like cpu maps */
		READ_MOSTLY_DATA(INTERNODE_CACHE_BYTES)

		/* End of data section */
		_edata = .;
	} :data

	BUG_TABLE

	ORC_UNWIND_TABLE

	/* Init code and data - will be freed after init */
	. = ALIGN(PAGE_SIZE);
	.init.begin : AT(ADDR(.init.begin) - LOAD_OFFSET) {
		__init_begin = .; /* paired with __init_end */
	}

#if defined(CONFIG_X86_64) && defined(CONFIG_SMP)
	/*
	 * percpu offsets are zero-based on SMP.  PERCPU_VADDR() changes the
	 * output PHDR, so the next output section - .init.text - should
	 * start another segment - init.
	 */
	PERCPU_VADDR(INTERNODE_CACHE_BYTES, 0, :percpu)
	ASSERT(SIZEOF(.data..percpu) < CONFIG_PHYSICAL_START,
	       "per-CPU data too large - increase CONFIG_PHYSICAL_START")
#endif

	INIT_TEXT_SECTION(PAGE_SIZE)
#ifdef CONFIG_X86_64
	:init
#endif

	/*
	 * Section for code used exclusively before alternatives are run. All
	 * references to such code must be patched out by alternatives, normally
	 * by using X86_FEATURE_ALWAYS CPU feature bit.
	 *
	 * See static_cpu_has() for an example.
	 */
	.altinstr_aux : AT(ADDR(.altinstr_aux) - LOAD_OFFSET) {
		*(.altinstr_aux)
	}

	INIT_DATA_SECTION(16)

	.x86_cpu_dev.init : AT(ADDR(.x86_cpu_dev.init) - LOAD_OFFSET) {
		__x86_cpu_dev_start = .;
		*(.x86_cpu_dev.init)
		__x86_cpu_dev_end = .;
	}

#ifdef CONFIG_X86_INTEL_MID
	.x86_intel_mid_dev.init : AT(ADDR(.x86_intel_mid_dev.init) - \
								LOAD_OFFSET) {
		__x86_intel_mid_dev_start = .;
		*(.x86_intel_mid_dev.init)
		__x86_intel_mid_dev_end = .;
	}
#endif

#ifdef CONFIG_MITIGATION_RETPOLINE
	/*
	 * List of instructions that call/jmp/jcc to retpoline thunks
	 * __x86_indirect_thunk_*(). These instructions can be patched along
	 * with alternatives, after which the section can be freed.
	 */
	. = ALIGN(8);
	.retpoline_sites : AT(ADDR(.retpoline_sites) - LOAD_OFFSET) {
		__retpoline_sites = .;
		*(.retpoline_sites)
		__retpoline_sites_end = .;
	}

	. = ALIGN(8);
	.return_sites : AT(ADDR(.return_sites) - LOAD_OFFSET) {
		__return_sites = .;
		*(.return_sites)
		__return_sites_end = .;
	}

	. = ALIGN(8);
	.call_sites : AT(ADDR(.call_sites) - LOAD_OFFSET) {
		__call_sites = .;
		*(.call_sites)
		__call_sites_end = .;
	}
#endif

#ifdef CONFIG_X86_KERNEL_IBT
	. = ALIGN(8);
	.ibt_endbr_seal : AT(ADDR(.ibt_endbr_seal) - LOAD_OFFSET) {
		__ibt_endbr_seal = .;
		*(.ibt_endbr_seal)
		__ibt_endbr_seal_end = .;
	}
#endif

#ifdef CONFIG_FINEIBT
	. = ALIGN(8);
	.cfi_sites : AT(ADDR(.cfi_sites) - LOAD_OFFSET) {
		__cfi_sites = .;
		*(.cfi_sites)
		__cfi_sites_end = .;
	}
#endif

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 */
	. = ALIGN(8);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	/*
	 * And here are the replacement instructions. The linker sticks
	 * them as binary blobs. The .altinstructions has enough data to
	 * get the address and the length of them to patch the kernel safely.
	 */
	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
	}

	. = ALIGN(8);
	.apicdrivers : AT(ADDR(.apicdrivers) - LOAD_OFFSET) {
		__apicdrivers = .;
		*(.apicdrivers);
		__apicdrivers_end = .;
	}

	. = ALIGN(8);
	/*
	 * .exit.text is discarded at runtime, not link time, to deal with
	 *  references from .altinstructions
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}

#if !defined(CONFIG_X86_64) || !defined(CONFIG_SMP)
	PERCPU_SECTION(INTERNODE_CACHE_BYTES)
#endif

	RUNTIME_CONST_VARIABLES
	RUNTIME_CONST(ptr, USER_PTR_MAX)

	. = ALIGN(PAGE_SIZE);

	/* freed after init ends here */
	.init.end : AT(ADDR(.init.end) - LOAD_OFFSET) {
		__init_end = .;
	}

	/*
	 * smp_locks might be freed after init
	 * start/end must be page aligned
	 */
	. = ALIGN(PAGE_SIZE);
	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		__smp_locks = .;
		*(.smp_locks)
		. = ALIGN(PAGE_SIZE);
		__smp_locks_end = .;
	}

#ifdef CONFIG_X86_64
	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		NOSAVE_DATA
	}
#endif

	/* BSS */
	. = ALIGN(PAGE_SIZE);
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		__bss_start = .;
		*(.bss..page_aligned)
		. = ALIGN(PAGE_SIZE);
		*(BSS_MAIN)
		BSS_DECRYPTED
		. = ALIGN(PAGE_SIZE);
		__bss_stop = .;
	}

	/*
	 * The memory occupied from _text to here, __end_of_kernel_reserve, is
	 * automatically reserved in setup_arch(). Anything after here must be
	 * explicitly reserved using memblock_reserve() or it will be discarded
	 * and treated as available memory.
	 */
	__end_of_kernel_reserve = .;

	. = ALIGN(PAGE_SIZE);
	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.bss..brk)		/* areas brk users have reserved */
		__brk_limit = .;
	}

	. = ALIGN(PAGE_SIZE);		/* keep VO_INIT_SIZE page aligned */
	_end = .;

#ifdef CONFIG_AMD_MEM_ENCRYPT
	/*
	 * Early scratch/workarea section: Lives outside of the kernel proper
	 * (_text - _end).
	 *
	 * Resides after _end because even though the .brk section is after
	 * __end_of_kernel_reserve, the .brk section is later reserved as a
	 * part of the kernel. Since it is located after __end_of_kernel_reserve
	 * it will be discarded and become part of the available memory. As
	 * such, it can only be used by very early boot code and must not be
	 * needed afterwards.
	 *
	 * Currently used by SME for performing in-place encryption of the
	 * kernel during boot. Resides on a 2MB boundary to simplify the
	 * pagetable setup used for SME in-place encryption.
	 */
	. = ALIGN(HPAGE_SIZE);
	.init.scratch : AT(ADDR(.init.scratch) - LOAD_OFFSET) {
		__init_scratch_begin = .;
		*(.init.scratch)
		. = ALIGN(HPAGE_SIZE);
		__init_scratch_end = .;
	}
#endif

	STABS_DEBUG
	DWARF_DEBUG
#ifdef CONFIG_PROPELLER_CLANG
	.llvm_bb_addr_map : { *(.llvm_bb_addr_map) }
#endif

	ELF_DETAILS

	DISCARDS

	/*
	 * Make sure that the .got.plt is either completely empty or it
	 * contains only the lazy dispatch entries.
	 */
	.got.plt (INFO) : { *(.got.plt) }
	ASSERT(SIZEOF(.got.plt) == 0 ||
#ifdef CONFIG_X86_64
	       SIZEOF(.got.plt) == 0x18,
#else
	       SIZEOF(.got.plt) == 0xc,
#endif
	       "Unexpected GOT/PLT entries detected!")

	/*
	 * Sections that should stay zero sized, which is safer to
	 * explicitly check instead of blindly discarding.
	 */
	.got : {
		*(.got) *(.igot.*)
	}
	ASSERT(SIZEOF(.got) == 0, "Unexpected GOT entries detected!")

	.plt : {
		*(.plt) *(.plt.*) *(.iplt)
	}
	ASSERT(SIZEOF(.plt) == 0, "Unexpected run-time procedure linkages detected!")

	.rel.dyn : {
		*(.rel.*) *(.rel_*)
	}
	ASSERT(SIZEOF(.rel.dyn) == 0, "Unexpected run-time relocations (.rel) detected!")

	.rela.dyn : {
		*(.rela.*) *(.rela_*)
	}
	ASSERT(SIZEOF(.rela.dyn) == 0, "Unexpected run-time relocations (.rela) detected!")
}

/*
 * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
 */
. = ASSERT((_end - LOAD_OFFSET <= KERNEL_IMAGE_SIZE),
	   "kernel image bigger than KERNEL_IMAGE_SIZE");

/* needed for Clang - see arch/x86/entry/entry.S */
PROVIDE(__ref_stack_chk_guard = __stack_chk_guard);

#ifdef CONFIG_X86_64
/*
 * Per-cpu symbols which need to be offset from __per_cpu_load
 * for the boot processor.
 */
#define INIT_PER_CPU(x) init_per_cpu__##x = ABSOLUTE(x) + __per_cpu_load
INIT_PER_CPU(gdt_page);
INIT_PER_CPU(fixed_percpu_data);
INIT_PER_CPU(irq_stack_backing_store);

#ifdef CONFIG_SMP
. = ASSERT((fixed_percpu_data == 0),
           "fixed_percpu_data is not at start of per-cpu area");
#endif

#ifdef CONFIG_MITIGATION_UNRET_ENTRY
. = ASSERT((retbleed_return_thunk & 0x3f) == 0, "retbleed_return_thunk not cacheline-aligned");
#endif

#ifdef CONFIG_MITIGATION_SRSO
. = ASSERT((srso_safe_ret & 0x3f) == 0, "srso_safe_ret not cacheline-aligned");
/*
 * GNU ld cannot do XOR until 2.41.
 * https://sourceware.org/git/?p=binutils-gdb.git;a=commit;h=f6f78318fca803c4907fb8d7f6ded8295f1947b1
 *
 * LLVM lld cannot do XOR until lld-17.
 * https://github.com/llvm/llvm-project/commit/fae96104d4378166cbe5c875ef8ed808a356f3fb
 *
 * Instead do: (A | B) - (A & B) in order to compute the XOR
 * of the two function addresses:
 */
. = ASSERT(((ABSOLUTE(srso_alias_untrain_ret) | srso_alias_safe_ret) -
		(ABSOLUTE(srso_alias_untrain_ret) & srso_alias_safe_ret)) == ((1 << 2) | (1 << 8) | (1 << 14) | (1 << 20)),
		"SRSO function pair won't alias");
#endif

#endif /* CONFIG_X86_64 */

/*
 * The symbols below are referenced using relative relocations in the
 * respective ELF notes. This produces build time constants that the
 * linker will never mark as relocatable. (Using just ABSOLUTE() is not
 * sufficient for that).
 */
#ifdef CONFIG_XEN_PV
xen_elfnote_entry_value =
	ABSOLUTE(xen_elfnote_entry) + ABSOLUTE(startup_xen);
#endif
#ifdef CONFIG_PVH
xen_elfnote_phys32_entry_value =
	ABSOLUTE(xen_elfnote_phys32_entry) + ABSOLUTE(pvh_start_xen - LOAD_OFFSET);
#endif
