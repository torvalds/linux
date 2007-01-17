/*
 *	vmlinux.lds.S -- master linker script for m68knommu arch
 *
 *	(C) Copyright 2002-2006, Greg Ungerer <gerg@snapgear.com>
 *
 *	This linker script is equiped to build either ROM loaded or RAM
 *	run kernels.
 */

#include <asm-generic/vmlinux.lds.h>

#if defined(CONFIG_RAMKERNEL)
#define	RAM_START	CONFIG_KERNELBASE
#define	RAM_LENGTH	(CONFIG_RAMBASE + CONFIG_RAMSIZE - CONFIG_KERNELBASE)
#define	TEXT		ram
#define	DATA		ram
#define	INIT		ram
#define	BSS		ram
#endif
#if defined(CONFIG_ROMKERNEL) || defined(CONFIG_HIMEMKERNEL)
#define	RAM_START	CONFIG_RAMBASE
#define	RAM_LENGTH	CONFIG_RAMSIZE
#define	ROMVEC_START	CONFIG_ROMVEC
#define	ROMVEC_LENGTH	CONFIG_ROMVECSIZE
#define	ROM_START	CONFIG_ROMSTART
#define	ROM_LENGTH	CONFIG_ROMSIZE
#define	TEXT		rom
#define	DATA		ram
#define	INIT		ram
#define	BSS		ram
#endif

#ifndef DATA_ADDR
#define	DATA_ADDR
#endif


OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
	ram	: ORIGIN = RAM_START, LENGTH = RAM_LENGTH
#ifdef ROM_START
	romvec	: ORIGIN = ROMVEC_START, LENGTH = ROMVEC_LENGTH
	rom	: ORIGIN = ROM_START, LENGTH = ROM_LENGTH
#endif
}

jiffies = jiffies_64 + 4;

SECTIONS {

#ifdef ROMVEC_START
	. = ROMVEC_START ;
	.romvec : {
		__rom_start = . ;
		_romvec = .;
		*(.data.initvect)
	} > romvec
#endif

	.text : {
		_text = .;
		_stext = . ;
        	*(.text)
		SCHED_TEXT
        	*(.text.lock)

		. = ALIGN(16);          /* Exception table              */
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;

		*(.rodata) *(.rodata.*)
		*(__vermagic)		/* Kernel version magic */
		*(.rodata1)
		*(.rodata.str1.1)

		/* Kernel symbol table: Normal symbols */
		. = ALIGN(4);
		__start___ksymtab = .;
		*(__ksymtab)
		__stop___ksymtab = .;

		/* Kernel symbol table: GPL-only symbols */
		__start___ksymtab_gpl = .;
		*(__ksymtab_gpl)
		__stop___ksymtab_gpl = .;

		/* Kernel symbol table: GPL-future symbols */
		__start___ksymtab_gpl_future = .;
		*(__ksymtab_gpl_future)
		__stop___ksymtab_gpl_future = .;

		/* Kernel symbol table: Normal symbols */
		__start___kcrctab = .;
		*(__kcrctab)
		__stop___kcrctab = .;

		/* Kernel symbol table: GPL-only symbols */
		__start___kcrctab_gpl = .;
		*(__kcrctab_gpl)
		__stop___kcrctab_gpl = .;

		/* Kernel symbol table: GPL-future symbols */
		__start___kcrctab_gpl_future = .;
		*(__kcrctab_gpl_future)
		__stop___kcrctab_gpl_future = .;

		/* Kernel symbol table: strings */
		*(__ksymtab_strings)

		/* Built-in module parameters */
		. = ALIGN(4) ;
		__start___param = .;
		*(__param)
		__stop___param = .;

		. = ALIGN(4) ;
		_etext = . ;
	} > TEXT

	.data DATA_ADDR : {
		. = ALIGN(4);
		_sdata = . ;
		*(.data)
		. = ALIGN(8192) ;
		*(.data.init_task)
		_edata = . ;
	} > DATA

	.init : {
		. = ALIGN(4096);
		__init_begin = .;
		_sinittext = .;
		*(.init.text)
		_einittext = .;
		*(.init.data)
		. = ALIGN(16);
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
		__security_initcall_start = .;
		*(.security_initcall.init)
		__security_initcall_end = .;
		. = ALIGN(4);
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		. = ALIGN(4096);
		__init_end = .;
	} > INIT

	/DISCARD/ : {
		*(.exit.text)
		*(.exit.data)
		*(.exitcall.exit)
	}

	.bss : {
		. = ALIGN(4);
		_sbss = . ;
		*(.bss)
		*(COMMON)
		. = ALIGN(4) ;
		_ebss = . ;
	} > BSS

}

