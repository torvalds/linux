/* ld script to make s390 Linux kernel
 * Written by Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm-generic/vmlinux.lds.h>

#ifndef CONFIG_64BIT
OUTPUT_FORMAT("elf32-s390", "elf32-s390", "elf32-s390")
OUTPUT_ARCH(s390)
ENTRY(_start)
jiffies = jiffies_64 + 4;
#else
OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)
ENTRY(_start)
jiffies = jiffies_64;
#endif

PHDRS {
	text PT_LOAD FLAGS(5);	/* R_E */
	data PT_LOAD FLAGS(7);	/* RWE */
	note PT_NOTE FLAGS(0);	/* ___ */
}

SECTIONS
{
	. = 0x00000000;
	.text : {
	_text = .;		/* Text and read-only data */
		*(.text.head)
	TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :text = 0x0700

	_etext = .;		/* End of text section */

	NOTES :text :note

	RODATA

#ifdef CONFIG_SHARED_KERNEL
	. = ALIGN(0x100000);	/* VM shared segments are 1MB aligned */
#endif

	. = ALIGN(PAGE_SIZE);
	_eshared = .;		/* End of shareable data */

	. = ALIGN(16);		/* Exception table */
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	} :data

	.data : {		/* Data */
		DATA_DATA
		CONSTRUCTORS
	}

	. = ALIGN(PAGE_SIZE);
	.data_nosave : {
	__nosave_begin = .;
		*(.data.nosave)
	}
	. = ALIGN(PAGE_SIZE);
	__nosave_end = .;

	. = ALIGN(PAGE_SIZE);
	.data.page_aligned : {
		*(.data.idt)
	}

	. = ALIGN(0x100);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}

	. = ALIGN(0x100);
	.data.read_mostly : {
		*(.data.read_mostly)
	}
	_edata = .;		/* End of data section */

	. = ALIGN(THREAD_SIZE);	/* init_task */
	.data.init_task : {
		*(.data.init_task)
	}

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);	/* Init code and data */
	__init_begin = .;
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	/*
	 * .exit.text is discarded at runtime, not link time,
	 * to deal with references from __bug_table
	*/
	.exit.text : {
		EXIT_TEXT
	}

	.init.data : {
		INIT_DATA
	}
	. = ALIGN(0x100);
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

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(0x100);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		. = ALIGN(2);
		__initramfs_end = .;
	}
#endif

	PERCPU(PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;		/* freed after init ends here */

	/* BSS */
	.bss : {
		__bss_start = .;
		*(.bss)
		. = ALIGN(2);
		__bss_stop = .;
	}

	_end = . ;

	/* Sections to be discarded */
	/DISCARD/ : {
		EXIT_DATA
		*(.exitcall.exit)
	}

	/* Debugging sections.	*/
	STABS_DEBUG
	DWARF_DEBUG
}
