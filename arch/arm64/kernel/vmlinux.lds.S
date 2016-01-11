/*
 * ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/cache.h>
#include <asm/kernel-pgtable.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "image.h"

/* .exit.text needed in case of alternative patching */
#define ARM_EXIT_KEEP(x)	x
#define ARM_EXIT_DISCARD(x)

OUTPUT_ARCH(aarch64)
ENTRY(_text)

jiffies = jiffies_64;

#define HYPERVISOR_TEXT					\
	/*						\
	 * Align to 4 KB so that			\
	 * a) the HYP vector table is at its minimum	\
	 *    alignment of 2048 bytes			\
	 * b) the HYP init code will not cross a page	\
	 *    boundary if its size does not exceed	\
	 *    4 KB (see related ASSERT() below)		\
	 */						\
	. = ALIGN(SZ_4K);				\
	VMLINUX_SYMBOL(__hyp_idmap_text_start) = .;	\
	*(.hyp.idmap.text)				\
	VMLINUX_SYMBOL(__hyp_idmap_text_end) = .;	\
	VMLINUX_SYMBOL(__hyp_text_start) = .;		\
	*(.hyp.text)					\
	VMLINUX_SYMBOL(__hyp_text_end) = .;

#define IDMAP_TEXT					\
	. = ALIGN(SZ_4K);				\
	VMLINUX_SYMBOL(__idmap_text_start) = .;		\
	*(.idmap.text)					\
	VMLINUX_SYMBOL(__idmap_text_end) = .;

/*
 * The size of the PE/COFF section that covers the kernel image, which
 * runs from stext to _edata, must be a round multiple of the PE/COFF
 * FileAlignment, which we set to its minimum value of 0x200. 'stext'
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

#if defined(CONFIG_DEBUG_ALIGN_RODATA)
#define ALIGN_DEBUG_RO			. = ALIGN(1<<SECTION_SHIFT);
#define ALIGN_DEBUG_RO_MIN(min)		ALIGN_DEBUG_RO
#elif defined(CONFIG_DEBUG_RODATA)
#define ALIGN_DEBUG_RO			. = ALIGN(1<<PAGE_SHIFT);
#define ALIGN_DEBUG_RO_MIN(min)		ALIGN_DEBUG_RO
#else
#define ALIGN_DEBUG_RO
#define ALIGN_DEBUG_RO_MIN(min)		. = ALIGN(min);
#endif

SECTIONS
{
	/*
	 * XXX: The linker does not define how output sections are
	 * assigned to input sections when there are multiple statements
	 * matching the same input section name.  There is no documented
	 * order of matching.
	 */
	/DISCARD/ : {
		ARM_EXIT_DISCARD(EXIT_TEXT)
		ARM_EXIT_DISCARD(EXIT_DATA)
		EXIT_CALL
		*(.discard)
		*(.discard.*)
	}

	. = PAGE_OFFSET + TEXT_OFFSET;

	.head.text : {
		_text = .;
		HEAD_TEXT
	}
	ALIGN_DEBUG_RO
	.text : {			/* Real text segment		*/
		_stext = .;		/* Text and read-only data	*/
			__exception_text_start = .;
			*(.exception.text)
			__exception_text_end = .;
			IRQENTRY_TEXT
			TEXT_TEXT
			SCHED_TEXT
			LOCK_TEXT
			HYPERVISOR_TEXT
			IDMAP_TEXT
			*(.fixup)
			*(.gnu.warning)
		. = ALIGN(16);
		*(.got)			/* Global offset table		*/
	}

	ALIGN_DEBUG_RO
	RO_DATA(PAGE_SIZE)
	EXCEPTION_TABLE(8)
	NOTES
	ALIGN_DEBUG_RO
	_etext = .;			/* End of text and rodata section */

	ALIGN_DEBUG_RO_MIN(PAGE_SIZE)
	__init_begin = .;

	INIT_TEXT_SECTION(8)
	.exit.text : {
		ARM_EXIT_KEEP(EXIT_TEXT)
	}

	ALIGN_DEBUG_RO_MIN(16)
	.init.data : {
		INIT_DATA
		INIT_SETUP(16)
		INIT_CALLS
		CON_INITCALL
		SECURITY_INITCALL
		INIT_RAM_FS
	}
	.exit.data : {
		ARM_EXIT_KEEP(EXIT_DATA)
	}

	PERCPU_SECTION(L1_CACHE_BYTES)

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	. = ALIGN(4);
	.altinstructions : {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}
	.altinstr_replacement : {
		*(.altinstr_replacement)
	}

	. = ALIGN(PAGE_SIZE);
	_data = .;
	_sdata = .;
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	PECOFF_EDATA_PADDING
	_edata = .;

	BSS_SECTION(0, 0, 0)

	. = ALIGN(PAGE_SIZE);
	idmap_pg_dir = .;
	. += IDMAP_DIR_SIZE;
	swapper_pg_dir = .;
	. += SWAPPER_DIR_SIZE;

	_end = .;

	STABS_DEBUG

	HEAD_SYMBOLS
}

/*
 * The HYP init code and ID map text can't be longer than a page each,
 * and should not cross a page boundary.
 */
ASSERT(__hyp_idmap_text_end - (__hyp_idmap_text_start & ~(SZ_4K - 1)) <= SZ_4K,
	"HYP init code too big or misaligned")
ASSERT(__idmap_text_end - (__idmap_text_start & ~(SZ_4K - 1)) <= SZ_4K,
	"ID map text too big or misaligned")

/*
 * If padding is applied before .head.text, virt<->phys conversions will fail.
 */
ASSERT(_text == (PAGE_OFFSET + TEXT_OFFSET), "HEAD is misaligned")
