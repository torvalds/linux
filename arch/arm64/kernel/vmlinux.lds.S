/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <asm/hyp_image.h>
#ifdef CONFIG_KVM
#define HYPERVISOR_EXTABLE					\
	. = ALIGN(SZ_8);					\
	__start___kvm_ex_table = .;				\
	*(__kvm_ex_table)					\
	__stop___kvm_ex_table = .;

#define HYPERVISOR_DATA_SECTIONS				\
	HYP_SECTION_NAME(.rodata) : {				\
		. = ALIGN(PAGE_SIZE);				\
		__hyp_rodata_start = .;				\
		*(HYP_SECTION_NAME(.data..ro_after_init))	\
		*(HYP_SECTION_NAME(.rodata))			\
		. = ALIGN(PAGE_SIZE);				\
		__hyp_rodata_end = .;				\
	}

#define HYPERVISOR_PERCPU_SECTION				\
	. = ALIGN(PAGE_SIZE);					\
	HYP_SECTION_NAME(.data..percpu) : {			\
		*(HYP_SECTION_NAME(.data..percpu))		\
	}

#define HYPERVISOR_RELOC_SECTION				\
	.hyp.reloc : ALIGN(4) {					\
		__hyp_reloc_begin = .;				\
		*(.hyp.reloc)					\
		__hyp_reloc_end = .;				\
	}

#define BSS_FIRST_SECTIONS					\
	__hyp_bss_start = .;					\
	*(HYP_SECTION_NAME(.bss))				\
	. = ALIGN(PAGE_SIZE);					\
	__hyp_bss_end = .;

/*
 * We require that __hyp_bss_start and __bss_start are aligned, and enforce it
 * with an assertion. But the BSS_SECTION macro places an empty .sbss section
 * between them, which can in some cases cause the linker to misalign them. To
 * work around the issue, force a page alignment for __bss_start.
 */
#define SBSS_ALIGN			PAGE_SIZE
#else /* CONFIG_KVM */
#define HYPERVISOR_EXTABLE
#define HYPERVISOR_DATA_SECTIONS
#define HYPERVISOR_PERCPU_SECTION
#define HYPERVISOR_RELOC_SECTION
#define SBSS_ALIGN			0
#endif

#define RO_EXCEPTION_TABLE_ALIGN	4
#define RUNTIME_DISCARD_EXIT

#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/kernel-pgtable.h>
#include <asm/kexec.h>
#include <asm/memory.h>
#include <asm/page.h>

#include "image.h"

OUTPUT_ARCH(aarch64)
ENTRY(_text)

jiffies = jiffies_64;

#define HYPERVISOR_TEXT					\
	. = ALIGN(PAGE_SIZE);				\
	__hyp_idmap_text_start = .;			\
	*(.hyp.idmap.text)				\
	__hyp_idmap_text_end = .;			\
	__hyp_text_start = .;				\
	*(.hyp.text)					\
	HYPERVISOR_EXTABLE				\
	. = ALIGN(PAGE_SIZE);				\
	__hyp_text_end = .;

#define IDMAP_TEXT					\
	. = ALIGN(SZ_4K);				\
	__idmap_text_start = .;				\
	*(.idmap.text)					\
	__idmap_text_end = .;

#ifdef CONFIG_HIBERNATION
#define HIBERNATE_TEXT					\
	ALIGN_FUNCTION();				\
	__hibernate_exit_text_start = .;		\
	*(.hibernate_exit.text)				\
	__hibernate_exit_text_end = .;
#else
#define HIBERNATE_TEXT
#endif

#ifdef CONFIG_KEXEC_CORE
#define KEXEC_TEXT					\
	ALIGN_FUNCTION();				\
	__relocate_new_kernel_start = .;		\
	*(.kexec_relocate.text)				\
	__relocate_new_kernel_end = .;
#else
#define KEXEC_TEXT
#endif

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
#define TRAMP_TEXT					\
	. = ALIGN(PAGE_SIZE);				\
	__entry_tramp_text_start = .;			\
	*(.entry.tramp.text)				\
	. = ALIGN(PAGE_SIZE);				\
	__entry_tramp_text_end = .;			\
	*(.entry.tramp.rodata)
#else
#define TRAMP_TEXT
#endif

#ifdef CONFIG_UNWIND_TABLES
#define UNWIND_DATA_SECTIONS				\
	.eh_frame : {					\
		__pi___eh_frame_start = .;		\
		*(.eh_frame)				\
		__pi___eh_frame_end = .;		\
	}
#else
#define UNWIND_DATA_SECTIONS
#endif

/*
 * The size of the PE/COFF section that covers the kernel image, which
 * runs from _stext to _edata, must be a round multiple of the PE/COFF
 * FileAlignment, which we set to its minimum value of 0x200. '_stext'
 * itself is 4 KB aligned, so padding out _edata to a 0x200 aligned
 * boundary should be sufficient.
 */
PECOFF_FILE_ALIGNMENT = 0x200;

#ifdef CONFIG_EFI
#define PECOFF_EDATA_PADDING	\
	.pecoff_edata_padding : { BYTE(0); . = ALIGN(PECOFF_FILE_ALIGNMENT); }
#else
#define PECOFF_EDATA_PADDING
#endif

SECTIONS
{
	/*
	 * XXX: The linker does not define how output sections are
	 * assigned to input sections when there are multiple statements
	 * matching the same input section name.  There is no documented
	 * order of matching.
	 */
	DISCARDS
	/DISCARD/ : {
		*(.interp .dynamic)
		*(.dynsym .dynstr .hash .gnu.hash)
	}

	. = KIMAGE_VADDR;

	.head.text : {
		_text = .;
		HEAD_TEXT
	}
	.text : ALIGN(SEGMENT_ALIGN) {	/* Real text segment		*/
		_stext = .;		/* Text and read-only data	*/
			IRQENTRY_TEXT
			SOFTIRQENTRY_TEXT
			ENTRY_TEXT
			TEXT_TEXT
			SCHED_TEXT
			LOCK_TEXT
			KPROBES_TEXT
			HYPERVISOR_TEXT
			*(.gnu.warning)
	}

	. = ALIGN(SEGMENT_ALIGN);
	_etext = .;			/* End of text section */

	/* everything from this point to __init_begin will be marked RO NX */
	RO_DATA(PAGE_SIZE)

	HYPERVISOR_DATA_SECTIONS

	.got : { *(.got) }
	/*
	 * Make sure that the .got.plt is either completely empty or it
	 * contains only the lazy dispatch entries.
	 */
	.got.plt : { *(.got.plt) }
	ASSERT(SIZEOF(.got.plt) == 0 || SIZEOF(.got.plt) == 0x18,
	       "Unexpected GOT/PLT entries detected!")

	/* code sections that are never executed via the kernel mapping */
	.rodata.text : {
		TRAMP_TEXT
		HIBERNATE_TEXT
		KEXEC_TEXT
		IDMAP_TEXT
		. = ALIGN(PAGE_SIZE);
	}

	idmap_pg_dir = .;
	. += PAGE_SIZE;

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	tramp_pg_dir = .;
	. += PAGE_SIZE;
#endif

	reserved_pg_dir = .;
	. += PAGE_SIZE;

	swapper_pg_dir = .;
	. += PAGE_SIZE;

	. = ALIGN(SEGMENT_ALIGN);
	__init_begin = .;
	__inittext_begin = .;

	INIT_TEXT_SECTION(8)

	__exittext_begin = .;
	.exit.text : {
		EXIT_TEXT
	}
	__exittext_end = .;

	. = ALIGN(4);
	.altinstructions : {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	UNWIND_DATA_SECTIONS

	. = ALIGN(SEGMENT_ALIGN);
	__inittext_end = .;
	__initdata_begin = .;

	init_idmap_pg_dir = .;
	. += INIT_IDMAP_DIR_SIZE;
	init_idmap_pg_end = .;

	.init.data : {
		INIT_DATA
		INIT_SETUP(16)
		INIT_CALLS
		CON_INITCALL
		INIT_RAM_FS
		*(.init.altinstructions .init.bss)	/* from the EFI stub */
	}
	.exit.data : {
		EXIT_DATA
	}

	RUNTIME_CONST(shift, d_hash_shift)
	RUNTIME_CONST(ptr, dentry_hashtable)

	PERCPU_SECTION(L1_CACHE_BYTES)
	HYPERVISOR_PERCPU_SECTION

	HYPERVISOR_RELOC_SECTION

	.rela.dyn : ALIGN(8) {
		__pi_rela_start = .;
		*(.rela .rela*)
		__pi_rela_end = .;
	}

	.relr.dyn : ALIGN(8) {
		__pi_relr_start = .;
		*(.relr.dyn)
		__pi_relr_end = .;
	}

	. = ALIGN(SEGMENT_ALIGN);
	__initdata_end = .;
	__init_end = .;

	_data = .;
	_sdata = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_ALIGN)

	/*
	 * Data written with the MMU off but read with the MMU on requires
	 * cache lines to be invalidated, discarding up to a Cache Writeback
	 * Granule (CWG) of data from the cache. Keep the section that
	 * requires this type of maintenance to be in its own Cache Writeback
	 * Granule (CWG) area so the cache maintenance operations don't
	 * interfere with adjacent data.
	 */
	.mmuoff.data.write : ALIGN(SZ_2K) {
		__mmuoff_data_start = .;
		*(.mmuoff.data.write)
	}
	. = ALIGN(SZ_2K);
	.mmuoff.data.read : {
		*(.mmuoff.data.read)
		__mmuoff_data_end = .;
	}

	PECOFF_EDATA_PADDING
	__pecoff_data_rawsize = ABSOLUTE(. - __initdata_begin);
	_edata = .;

	/* start of zero-init region */
	BSS_SECTION(SBSS_ALIGN, 0, 0)

	. = ALIGN(PAGE_SIZE);
	init_pg_dir = .;
	. += INIT_DIR_SIZE;
	init_pg_end = .;
	/* end of zero-init region */

	. += SZ_4K;		/* stack for the early C runtime */
	early_init_stack = .;

	. = ALIGN(SEGMENT_ALIGN);
	__pecoff_data_size = ABSOLUTE(. - __initdata_begin);
	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS

	HEAD_SYMBOLS

	/*
	 * Sections that should stay zero sized, which is safer to
	 * explicitly check instead of blindly discarding.
	 */
	.plt : {
		*(.plt) *(.plt.*) *(.iplt) *(.igot .igot.plt)
	}
	ASSERT(SIZEOF(.plt) == 0, "Unexpected run-time procedure linkages detected!")

	.data.rel.ro : { *(.data.rel.ro) }
	ASSERT(SIZEOF(.data.rel.ro) == 0, "Unexpected RELRO detected!")
}

#include "image-vars.h"

/*
 * The HYP init code and ID map text can't be longer than a page each. The
 * former is page-aligned, but the latter may not be with 16K or 64K pages, so
 * it should also not cross a page boundary.
 */
ASSERT(__hyp_idmap_text_end - __hyp_idmap_text_start <= PAGE_SIZE,
	"HYP init code too big")
ASSERT(__idmap_text_end - (__idmap_text_start & ~(SZ_4K - 1)) <= SZ_4K,
	"ID map text too big or misaligned")
#ifdef CONFIG_HIBERNATION
ASSERT(__hibernate_exit_text_end - __hibernate_exit_text_start <= SZ_4K,
       "Hibernate exit text is bigger than 4 KiB")
ASSERT(__hibernate_exit_text_start == swsusp_arch_suspend_exit,
       "Hibernate exit text does not start with swsusp_arch_suspend_exit")
#endif
#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
ASSERT((__entry_tramp_text_end - __entry_tramp_text_start) <= 3*PAGE_SIZE,
	"Entry trampoline text too big")
#endif
#ifdef CONFIG_KVM
ASSERT(__hyp_bss_start == __bss_start, "HYP and Host BSS are misaligned")
#endif
/*
 * If padding is applied before .head.text, virt<->phys conversions will fail.
 */
ASSERT(_text == KIMAGE_VADDR, "HEAD is misaligned")

ASSERT(swapper_pg_dir - reserved_pg_dir == RESERVED_SWAPPER_OFFSET,
       "RESERVED_SWAPPER_OFFSET is wrong!")

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
ASSERT(swapper_pg_dir - tramp_pg_dir == TRAMP_SWAPPER_OFFSET,
       "TRAMP_SWAPPER_OFFSET is wrong!")
#endif

#ifdef CONFIG_KEXEC_CORE
/* kexec relocation code should fit into one KEXEC_CONTROL_PAGE_SIZE */
ASSERT(__relocate_new_kernel_end - __relocate_new_kernel_start <= SZ_4K,
       "kexec relocation code is bigger than 4 KiB")
ASSERT(KEXEC_CONTROL_PAGE_SIZE >= SZ_4K, "KEXEC_CONTROL_PAGE_SIZE is broken")
ASSERT(__relocate_new_kernel_start == arm64_relocate_new_kernel,
       "kexec control page does not start with arm64_relocate_new_kernel")
#endif
