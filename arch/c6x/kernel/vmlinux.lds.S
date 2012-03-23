/*
 * ld script for the c6x kernel
 *
 *  Copyright (C) 2010, 2011 Texas Instruments Incorporated
 *  Mark Salter <msalter@redhat.com>
 */
#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>

ENTRY(_c_int00)

#if defined(CONFIG_CPU_BIG_ENDIAN)
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif

#define	READONLY_SEGMENT_START	\
	. = PAGE_OFFSET;
#define	READWRITE_SEGMENT_START	\
	. = ALIGN(128);		\
	_data_lma = .;

SECTIONS
{
	/*
	 * Start kernel read only segment
	 */
	READONLY_SEGMENT_START

	.vectors :
	{
		_vectors_start = .;
		*(.vectors)
		. = ALIGN(0x400);
		_vectors_end = .;
	}

	. = ALIGN(0x1000);
	.cmdline :
	{
		*(.cmdline)
	}

	/*
	 * This section contains data which may be shared with other
	 * cores. It needs to be a fixed offset from PAGE_OFFSET
	 * regardless of kernel configuration.
	 */
	.virtio_ipc_dev :
	{
		*(.virtio_ipc_dev)
	}

	. = ALIGN(PAGE_SIZE);
	.init :
	{
		_stext = .;
		_sinittext = .;
		HEAD_TEXT
		INIT_TEXT
		_einittext = .;
	}

	__init_begin = _stext;
	INIT_DATA_SECTION(16)

	PERCPU_SECTION(128)

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	.text :
	{
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		IRQENTRY_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
	}

	EXCEPTION_TABLE(16)
	NOTES

	RO_DATA_SECTION(PAGE_SIZE)
	.const :
	{
		*(.const .const.* .gnu.linkonce.r.*)
		*(.switch)
	}

	. = ALIGN (8) ;
	__fdt_blob : AT(ADDR(__fdt_blob) - LOAD_OFFSET)
	{
		_fdt_start = . ;	/* place for fdt blob */
		*(__fdt_blob) ;		/* Any link-placed DTB */
		BYTE(0);		/* section always has contents */
	        . = _fdt_start + 0x4000;	/* Pad up to 16kbyte */
		_fdt_end = . ;
	}

	_etext = .;

	/*
	 * Start kernel read-write segment.
	 */
	READWRITE_SEGMENT_START
	_sdata = .;

	.fardata : AT(ADDR(.fardata) - LOAD_OFFSET)
	{
		INIT_TASK_DATA(THREAD_SIZE)
		NOSAVE_DATA
		PAGE_ALIGNED_DATA(PAGE_SIZE)
		CACHELINE_ALIGNED_DATA(128)
		READ_MOSTLY_DATA(128)
		DATA_DATA
		CONSTRUCTORS
		*(.data1)
		*(.fardata .fardata.*)
		*(.data.debug_bpt)
	}

	.neardata ALIGN(8) : AT(ADDR(.neardata) - LOAD_OFFSET)
	{
		*(.neardata2 .neardata2.* .gnu.linkonce.s2.*)
		*(.neardata .neardata.* .gnu.linkonce.s.*)
		. = ALIGN(8);
	}

	_edata = .;

	__bss_start = .;
	SBSS(8)
	BSS(8)
	.far :
	{
		. = ALIGN(8);
		*(.dynfar)
		*(.far .far.* .gnu.linkonce.b.*)
		. = ALIGN(8);
	}
	__bss_stop = .;

	_end = .;

	DWARF_DEBUG

	/DISCARD/ :
	{
		  EXIT_TEXT
		  EXIT_DATA
		  EXIT_CALL
		  *(.discard)
		  *(.discard.*)
		  *(.interp)
	}
}
