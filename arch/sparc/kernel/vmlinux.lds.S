/* ld script to make SparcLinux kernel */

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>

OUTPUT_FORMAT("elf32-sparc", "elf32-sparc", "elf32-sparc")
OUTPUT_ARCH(sparc)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
	. = 0x10000 + SIZEOF_HEADERS;
	.text 0xf0004000 :
	{
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.gnu.warning)
	} = 0
	_etext = .;
	PROVIDE (etext = .);
	RODATA
	.data :	{
		DATA_DATA
		CONSTRUCTORS
	}
	.data1 : {
		*(.data1)
	}
	_edata = .;
	PROVIDE (edata = .);

	.fixup : {
		__start___fixup = .;
		*(.fixup)
		__stop___fixup = .;
	}
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	NOTES

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	__init_text_end = .;
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

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : {
	__initramfs_start = .;
		*(.init.ramfs)
	__initramfs_end = .;
	}
#endif

	PERCPU(PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	. = ALIGN(32);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}

	__bss_start = .;
	.sbss : {
		*(.sbss)
		*(.scommon) }
	.bss : {
		*(.dynbss)
		*(.bss)
		*(COMMON)
	}
	_end = . ;
	PROVIDE (end = .);
	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
}
