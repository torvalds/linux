/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>

#ifdef CONFIG_HOTPLUG_CPU
#define ARM_CPU_DISCARD(x)
#define ARM_CPU_KEEP(x)		x
#else
#define ARM_CPU_DISCARD(x)	x
#define ARM_CPU_KEEP(x)
#endif

#if (defined(CONFIG_SMP_ON_UP) && !defined(CONFIG_DEBUG_SPINLOCK)) || \
	defined(CONFIG_GENERIC_BUG) || defined(CONFIG_JUMP_LABEL)
#define ARM_EXIT_KEEP(x)	x
#define ARM_EXIT_DISCARD(x)
#else
#define ARM_EXIT_KEEP(x)
#define ARM_EXIT_DISCARD(x)	x
#endif

#ifdef CONFIG_MMU
#define ARM_MMU_KEEP(x)		x
#define ARM_MMU_DISCARD(x)
#else
#define ARM_MMU_KEEP(x)
#define ARM_MMU_DISCARD(x)	x
#endif

/* Set start/end symbol names to the LMA for the section */
#define ARM_LMA(sym, section)						\
	sym##_start = LOADADDR(section);				\
	sym##_end = LOADADDR(section) + SIZEOF(section)

#define PROC_INFO							\
		. = ALIGN(4);						\
		__proc_info_begin = .;					\
		*(.proc.info.init)					\
		__proc_info_end = .;

#define IDMAP_TEXT							\
		ALIGN_FUNCTION();					\
		__idmap_text_start = .;					\
		*(.idmap.text)						\
		__idmap_text_end = .;					\

#define ARM_DISCARD							\
		*(.ARM.exidx.exit.text)					\
		*(.ARM.extab.exit.text)					\
		*(.ARM.exidx.text.exit)					\
		*(.ARM.extab.text.exit)					\
		ARM_CPU_DISCARD(*(.ARM.exidx.cpuexit.text))		\
		ARM_CPU_DISCARD(*(.ARM.extab.cpuexit.text))		\
		ARM_EXIT_DISCARD(EXIT_TEXT)				\
		ARM_EXIT_DISCARD(EXIT_DATA)				\
		EXIT_CALL						\
		ARM_MMU_DISCARD(*(.text.fixup))				\
		ARM_MMU_DISCARD(*(__ex_table))				\
		COMMON_DISCARDS

/*
 * Sections that should stay zero sized, which is safer to explicitly
 * check instead of blindly discarding.
 */
#define ARM_ASSERTS							\
	.plt : {							\
		*(.iplt) *(.rel.iplt) *(.iplt) *(.igot.plt)		\
	}								\
	ASSERT(SIZEOF(.plt) == 0,					\
	       "Unexpected run-time procedure linkages detected!")

#define ARM_DETAILS							\
		ELF_DETAILS						\
		.ARM.attributes 0 : { *(.ARM.attributes) }

#define ARM_STUBS_TEXT							\
		*(.gnu.warning)						\
		*(.glue_7)						\
		*(.glue_7t)						\
		*(.vfp11_veneer)                                        \
		*(.v4_bx)

#define ARM_TEXT							\
		IDMAP_TEXT						\
		__entry_text_start = .;					\
		*(.entry.text)						\
		__entry_text_end = .;					\
		IRQENTRY_TEXT						\
		SOFTIRQENTRY_TEXT					\
		TEXT_TEXT						\
		SCHED_TEXT						\
		CPUIDLE_TEXT						\
		LOCK_TEXT						\
		KPROBES_TEXT						\
		ARM_STUBS_TEXT						\
		. = ALIGN(4);						\
		*(.got)			/* Global offset table */	\
		ARM_CPU_KEEP(PROC_INFO)

/* Stack unwinding tables */
#define ARM_UNWIND_SECTIONS						\
	. = ALIGN(8);							\
	.ARM.unwind_idx : {						\
		__start_unwind_idx = .;					\
		*(.ARM.exidx*)						\
		__stop_unwind_idx = .;					\
	}								\
	.ARM.unwind_tab : {						\
		__start_unwind_tab = .;					\
		*(.ARM.extab*)						\
		__stop_unwind_tab = .;					\
	}

/*
 * The vectors and stubs are relocatable code, and the
 * only thing that matters is their relative offsets
 */
#define ARM_VECTORS							\
	__vectors_lma = .;						\
	.vectors 0xffff0000 : AT(__vectors_start) {			\
		*(.vectors)						\
	}								\
	ARM_LMA(__vectors, .vectors);					\
	. = __vectors_lma + SIZEOF(.vectors);				\
									\
	__stubs_lma = .;						\
	.stubs ADDR(.vectors) + 0x1000 : AT(__stubs_lma) {		\
		*(.stubs)						\
	}								\
	ARM_LMA(__stubs, .stubs);					\
	. = __stubs_lma + SIZEOF(.stubs);				\
									\
	PROVIDE(vector_fiq_offset = vector_fiq - ADDR(.vectors));

#define ARM_TCM								\
	__itcm_start = ALIGN(4);					\
	.text_itcm ITCM_OFFSET : AT(__itcm_start - LOAD_OFFSET) {	\
		__sitcm_text = .;					\
		*(.tcm.text)						\
		*(.tcm.rodata)						\
		. = ALIGN(4);						\
		__eitcm_text = .;					\
	}								\
	. = __itcm_start + SIZEOF(.text_itcm);				\
									\
	__dtcm_start = .;						\
	.data_dtcm DTCM_OFFSET : AT(__dtcm_start - LOAD_OFFSET) {	\
		__sdtcm_data = .;					\
		*(.tcm.data)						\
		. = ALIGN(4);						\
		__edtcm_data = .;					\
	}								\
	. = __dtcm_start + SIZEOF(.data_dtcm);
