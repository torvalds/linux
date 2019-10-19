/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>
#include <asm/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-s390", "elf64-s390", "elf64-s390")
OUTPUT_ARCH(s390:64-bit)

ENTRY(startup)

SECTIONS
{
	. = 0;
	.head.text : {
		_head = . ;
		HEAD_TEXT
		_ehead = . ;
	}
	.text :	{
		_text = .;	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		_edata = . ;
	}
	/*
	* .dma section for code, data, ex_table that need to stay below 2 GB,
	* even when the kernel is relocate: above 2 GB.
	*/
	. = ALIGN(PAGE_SIZE);
	_sdma = .;
	.dma.text : {
		_stext_dma = .;
		*(.dma.text)
		. = ALIGN(PAGE_SIZE);
		_etext_dma = .;
	}
	. = ALIGN(16);
	.dma.ex_table : {
		_start_dma_ex_table = .;
		KEEP(*(.dma.ex_table))
		_stop_dma_ex_table = .;
	}
	.dma.data : { *(.dma.data) }
	. = ALIGN(PAGE_SIZE);
	_edma = .;

	BOOT_DATA
	BOOT_DATA_PRESERVED

	/*
	 * uncompressed image info used by the decompressor it should match
	 * struct vmlinux_info. It comes from .vmlinux.info section of
	 * uncompressed vmlinux in a form of info.o
	 */
	. = ALIGN(8);
	.vmlinux.info : {
		_vmlinux_info = .;
		*(.vmlinux.info)
	}

#ifdef CONFIG_KERNEL_UNCOMPRESSED
	. = 0x100000;
#else
	. = ALIGN(8);
#endif
	.rodata.compressed : {
		_compressed_start = .;
		*(.vmlinux.bin.compressed)
		_compressed_end = .;
		FILL(0xff);
		. = ALIGN(4096);
	}
	. = ALIGN(256);
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);	/* For convenience during zeroing */
		_ebss = .;
	}
	_end = .;

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.eh_frame)
		*(__ex_table)
		*(*__ksymtab*)
		*(___kcrctab*)
	}
}
