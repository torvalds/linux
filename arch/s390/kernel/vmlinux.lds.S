/* SPDX-License-Identifier: GPL-2.0 */
/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ftrace.lds.h>

/*
 * Put .bss..swapper_pg_dir as the first thing in .bss. This will
 * make sure it has 16k alignment.
 */
#define BSS_FIRST_SECTIONS *(.bss..swapper_pg_dir) \
			   *(.bss..invalid_pg_dir)

/* Handle ro_after_init data on our own. */
#define RO_AFTER_INIT_DATA

#define EMITS_PT_NOTE

#include <asm-generic/vmlinux.lds.h>
#include <asm/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)
ENTRY(startup_continue)
jiffies = jiffies_64;

PHDRS {
	text PT_LOAD FLAGS(5);	/* R_E */
	data PT_LOAD FLAGS(7);	/* RWE */
	note PT_NOTE FLAGS(0);	/* ___ */
}

SECTIONS
{
	. = 0x100000;
	.text : {
		_stext = .;		/* Start of text section */
		_text = .;		/* Text and read-only data */
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		FTRACE_HOTPATCH_TRAMPOLINES_TEXT
		*(.text.*_indirect_*)
		*(.fixup)
		*(.gnu.warning)
		. = ALIGN(PAGE_SIZE);
		_etext = .;		/* End of text section */
	} :text = 0x0700

	RO_DATA(PAGE_SIZE)

	. = ALIGN(PAGE_SIZE);
	_sdata = .;		/* Start of data section */

	. = ALIGN(PAGE_SIZE);
	__start_ro_after_init = .;
	.data..ro_after_init : {
		 *(.data..ro_after_init)
		JUMP_TABLE_DATA
	} :data
	EXCEPTION_TABLE(16)
	. = ALIGN(PAGE_SIZE);
	__end_ro_after_init = .;

	RW_DATA(0x100, PAGE_SIZE, THREAD_SIZE)
	BOOT_DATA_PRESERVED

	. = ALIGN(8);
	.amode31.refs : {
		_start_amode31_refs = .;
		*(.amode31.refs)
		_end_amode31_refs = .;
	}

	_edata = .;		/* End of data section */

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);	/* Init code and data */
	__init_begin = .;

	. = ALIGN(PAGE_SIZE);
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		_sinittext = .;
		INIT_TEXT
		. = ALIGN(PAGE_SIZE);
		_einittext = .;
	}

	/*
	 * .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	*/
	.exit.text : {
		EXIT_TEXT
	}

	.exit.data : {
		EXIT_DATA
	}

	/*
	 * struct alt_inst entries. From the header (alternative.h):
	 * "Alternative instructions for different CPU types or capabilities"
	 * Think locking instructions on spinlocks.
	 * Note, that it is a part of __init region.
	 */
	. = ALIGN(8);
	.altinstructions : {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	/*
	 * And here are the replacement instructions. The linker sticks
	 * them as binary blobs. The .altinstructions has enough data to
	 * get the address and the length of them to patch the kernel safely.
	 * Note, that it is a part of __init region.
	 */
	.altinstr_replacement : {
		*(.altinstr_replacement)
	}

	/*
	 * Table with the patch locations to undo expolines
	*/
	.nospec_call_table : {
		__nospec_call_start = . ;
		*(.s390_indirect*)
		__nospec_call_end = . ;
	}
	.nospec_return_table : {
		__nospec_return_start = . ;
		*(.s390_return*)
		__nospec_return_end = . ;
	}

	BOOT_DATA

	/*
	 * .amode31 section for code, data, ex_table that need to stay
	 * below 2 GB, even when the kernel is relocated above 2 GB.
	 */
	. = ALIGN(PAGE_SIZE);
	_samode31 = .;
	.amode31.text : {
		_stext_amode31 = .;
		*(.amode31.text)
		*(.amode31.text.*_indirect_*)
		. = ALIGN(PAGE_SIZE);
		_etext_amode31 = .;
	}
	. = ALIGN(16);
	.amode31.ex_table : {
		_start_amode31_ex_table = .;
		KEEP(*(.amode31.ex_table))
		_stop_amode31_ex_table = .;
	}
	. = ALIGN(PAGE_SIZE);
	.amode31.data : {
		*(.amode31.data)
	}
	. = ALIGN(PAGE_SIZE);
	_eamode31 = .;

	/* early.c uses stsi, which requires page aligned data. */
	. = ALIGN(PAGE_SIZE);
	INIT_DATA_SECTION(0x100)

	PERCPU_SECTION(0x100)

	.dynsym ALIGN(8) : {
		__dynsym_start = .;
		*(.dynsym)
		__dynsym_end = .;
	}
	.rela.dyn ALIGN(8) : {
		__rela_dyn_start = .;
		*(.rela*)
		__rela_dyn_end = .;
	}

	. = ALIGN(PAGE_SIZE);
	__init_end = .;		/* freed after init ends here */

	BSS_SECTION(PAGE_SIZE, 4 * PAGE_SIZE, PAGE_SIZE)

	_end = . ;

	/*
	 * uncompressed image info used by the decompressor
	 * it should match struct vmlinux_info
	 */
	.vmlinux.info 0 (INFO) : {
		QUAD(_stext)					/* default_lma */
		QUAD(startup_continue)				/* entry */
		QUAD(__bss_start - _stext)			/* image_size */
		QUAD(__bss_stop - __bss_start)			/* bss_size */
		QUAD(__boot_data_start)				/* bootdata_off */
		QUAD(__boot_data_end - __boot_data_start)	/* bootdata_size */
		QUAD(__boot_data_preserved_start)		/* bootdata_preserved_off */
		QUAD(__boot_data_preserved_end -
		     __boot_data_preserved_start)		/* bootdata_preserved_size */
		QUAD(__dynsym_start)				/* dynsym_start */
		QUAD(__rela_dyn_start)				/* rela_dyn_start */
		QUAD(__rela_dyn_end)				/* rela_dyn_end */
	} :NONE

	/* Debugging sections.	*/
	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	/* Sections to be discarded */
	DISCARDS
	/DISCARD/ : {
		*(.eh_frame)
	}
}
