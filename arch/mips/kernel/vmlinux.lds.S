#include <asm/asm-offsets.h>
#include <asm-generic/vmlinux.lds.h>

#undef mips
#define mips mips
OUTPUT_ARCH(mips)
ENTRY(kernel_entry)
PHDRS {
	text PT_LOAD FLAGS(7);	/* RWX */
	note PT_NOTE FLAGS(4);	/* R__ */
}
jiffies = JIFFIES;

SECTIONS
{
#ifdef CONFIG_BOOT_ELF64
	/* Read-only sections, merged into text segment: */
	/* . = 0xc000000000000000; */

	/* This is the value for an Origin kernel, taken from an IRIX kernel.  */
	/* . = 0xc00000000001c000; */

	/* Set the vaddr for the text segment to a value
	 *   >= 0xa800 0000 0001 9000 if no symmon is going to configured
	 *   >= 0xa800 0000 0030 0000 otherwise
	 */

	/* . = 0xa800000000300000; */
	. = 0xffffffff80300000;
#endif
	. = LOADADDR;
	/* read-only */
	_text = .;	/* Text and read-only data */
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.text.*)
		*(.fixup)
		*(.gnu.warning)
	} :text = 0
	_etext = .;	/* End of text section */

	/* Exception table */
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	/* Exception table for data bus errors */
	__dbe_table : {
		__start___dbe_table = .;
		*(__dbe_table)
		__stop___dbe_table = .;
	}

	NOTES :text :note
	.dummy : { *(.dummy) } :text

	RODATA

	/* writeable */
	.data : {	/* Data */
		. = . + DATAOFFSET;		/* for CONFIG_MAPPED_KERNEL */
		/*
		 * This ALIGN is needed as a workaround for a bug a
		 * gcc bug upto 4.1 which limits the maximum alignment
		 * to at most 32kB and results in the following
		 * warning:
		 *
		 *  CC      arch/mips/kernel/init_task.o
		 * arch/mips/kernel/init_task.c:30: warning: alignment
		 * of ‘init_thread_union’ is greater than maximum
		 * object file alignment.  Using 32768
		 */
		. = ALIGN(_PAGE_SIZE);
		*(.data.init_task)

		DATA_DATA
		CONSTRUCTORS
	}
	_gp = . + 0x8000;
	.lit8 : {
		*(.lit8)
	}
	.lit4 : {
		*(.lit4)
	}
	/* We want the small data sections together, so single-instruction offsets
	   can access them all, and initialized data all before uninitialized, so
	   we can shorten the on-disk segment size.  */
	.sdata : {
		*(.sdata)
	}

	. = ALIGN(_PAGE_SIZE);
	.data_nosave : {
		__nosave_begin = .;
		*(.data.nosave)
	}
	. = ALIGN(_PAGE_SIZE);
	__nosave_end = .;

	. = ALIGN(32);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	_edata =  .;			/* End of data section */

	/* will be freed after init */
	. = ALIGN(_PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	.init.data : {
		INIT_DATA
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

	/* .exit.text is discarded at runtime, not link time, to deal with
	 * references from .rodata
	 */
	.exit.text : {
		EXIT_TEXT
	}
	.exit.data : {
		EXIT_DATA
	}
#if defined(CONFIG_BLK_DEV_INITRD)
	. = ALIGN(_PAGE_SIZE);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif
	PERCPU(_PAGE_SIZE)
	. = ALIGN(_PAGE_SIZE);
	__init_end = .;
	/* freed after init ends here */

	__bss_start = .;	/* BSS */
	.sbss  : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		*(.bss)
		*(COMMON)
	}
	__bss_stop = .;

	_end = . ;

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.exitcall.exit)

		/* ABI crap starts here */
		*(.MIPS.options)
		*(.options)
		*(.pdr)
		*(.reginfo)
	}

	/* These mark the ABI of the kernel for debuggers.  */
	.mdebug.abi32 : {
		KEEP(*(.mdebug.abi32))
	}
	.mdebug.abi64 : {
		KEEP(*(.mdebug.abi64))
	}

	/* This is the MIPS specific mdebug section.  */
	.mdebug : {
		*(.mdebug)
	}

	STABS_DEBUG
	DWARF_DEBUG

	/* These must appear regardless of  .  */
	.gptab.sdata : {
		*(.gptab.data)
		*(.gptab.sdata)
	}
	.gptab.sbss : {
		*(.gptab.bss)
		*(.gptab.sbss)
	}
}
