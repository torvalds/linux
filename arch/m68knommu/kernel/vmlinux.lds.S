/*
 *	vmlinux.lds.S -- master linker script for m68knommu arch
 *
 *	(C) Copyright 2002-2004, Greg Ungerer <gerg@snapgear.com>
 *
 *	This ends up looking compilcated, because of the number of
 *	address variations for ram and rom/flash layouts. The real
 *	work of the linker script is all at the end, and reasonably
 *	strait forward.
 */

#include <linux/config.h>
#include <asm-generic/vmlinux.lds.h>

/*
 *	Original Palm pilot (same for Xcopilot).
 *	There is really only a rom target for this.
 */
#ifdef CONFIG_PILOT3
#define	ROMVEC_START	0x10c00000
#define	ROMVEC_LENGTH	0x10400
#define	ROM_START	0x10c10400
#define	ROM_LENGTH	0xfec00
#define	ROM_END		0x10d00000
#define	RAMVEC_START	0x00000000
#define	RAMVEC_LENGTH	0x400
#define	RAM_START	0x10000400
#define	RAM_LENGTH	0xffc00
#define	RAM_END		0x10100000
#define _ramend	_ram_end_notused
#define	DATA_ADDR	RAM_START
#endif

/*
 *	Same setup on both the uCsimm and uCdimm.
 */
#if defined(CONFIG_UCSIMM) || defined(CONFIG_UCDIMM)
#ifdef CONFIG_RAMKERNEL
#define	ROMVEC_START	0x10c10000
#define	ROMVEC_LENGTH	0x400
#define	ROM_START	0x10c10400
#define	ROM_LENGTH	0x1efc00
#define	ROM_END		0x10e00000
#define	RAMVEC_START	0x00000000
#define	RAMVEC_LENGTH	0x400
#define	RAM_START	0x00020400
#define	RAM_LENGTH	0x7dfc00
#define	RAM_END		0x00800000
#endif
#ifdef CONFIG_ROMKERNEL
#define	ROMVEC_START	0x10c10000
#define	ROMVEC_LENGTH	0x400
#define	ROM_START	0x10c10400
#define	ROM_LENGTH	0x1efc00
#define	ROM_END		0x10e00000
#define	RAMVEC_START	0x00000000
#define	RAMVEC_LENGTH	0x400
#define	RAM_START	0x00020000
#define	RAM_LENGTH	0x600000
#define	RAM_END		0x00800000
#endif
#ifdef CONFIG_HIMEMKERNEL
#define	ROMVEC_START	0x00600000
#define	ROMVEC_LENGTH	0x400
#define	ROM_START	0x00600400
#define	ROM_LENGTH	0x1efc00
#define	ROM_END		0x007f0000
#define	RAMVEC_START	0x00000000
#define	RAMVEC_LENGTH	0x400
#define	RAM_START	0x00020000
#define	RAM_LENGTH	0x5e0000
#define	RAM_END		0x00600000
#endif
#endif

#ifdef CONFIG_DRAGEN2
#define	RAM_START	0x10000
#define	RAM_LENGTH	0x7f0000
#endif

#ifdef CONFIG_UCQUICC
#define	ROMVEC_START	0x00000000
#define	ROMVEC_LENGTH	0x404
#define	ROM_START	0x00000404
#define	ROM_LENGTH	0x1ff6fc
#define	ROM_END		0x00200000
#define	RAMVEC_START	0x00200000
#define	RAMVEC_LENGTH	0x404
#define	RAM_START	0x00200404
#define	RAM_LENGTH	0x1ff6fc
#define	RAM_END		0x00400000
#endif

/*
 *	The standard Arnewsh 5206 board only has 1MiB of ram. Not normally
 *	enough to be useful. Assume the user has fitted something larger,
 *	at least 4MiB in size. No point in not letting the kernel completely
 *	link, it will be obvious if it is too big when they go to load it.
 */
#if defined(CONFIG_ARN5206)
#define	RAM_START	0x10000
#define	RAM_LENGTH	0x3f0000
#endif

/*
 *	The Motorola 5206eLITE board only has 1MiB of static RAM.
 */
#if defined(CONFIG_ELITE)
#define	RAM_START	0x30020000
#define	RAM_LENGTH	0xe0000
#endif

/*
 *	All the Motorola eval boards have the same basic arrangement.
 *	The end of RAM will vary depending on how much ram is fitted,
 *	but this isn't important here, we assume at least 4MiB.
 */
#if defined(CONFIG_M5206eC3) || defined(CONFIG_M5249C3) || \
    defined(CONFIG_M5272C3) || defined(CONFIG_M5307C3) || \
    defined(CONFIG_ARN5307) || defined(CONFIG_M5407C3) || \
    defined(CONFIG_M5271EVB) || defined(CONFIG_M5275EVB) || \
    defined(CONFIG_M5235EVB)
#define	RAM_START	0x20000
#define	RAM_LENGTH	0x3e0000
#endif

/*
 *	The Freescale 5208EVB board has 32MB of RAM.
 */
#if defined(CONFIG_M5208EVB)
#define	RAM_START	0x40020000
#define	RAM_LENGTH	0x01e00000
#endif

/*
 *	The senTec COBRA5272 board has nearly the same memory layout as 
 *	the M5272C3. We assume 16MiB ram.
 */
#if defined(CONFIG_COBRA5272)
#define RAM_START   0x20000
#define RAM_LENGTH  0xfe0000
#endif

#if defined(CONFIG_M5282EVB)
#define	RAM_START	0x10000
#define	RAM_LENGTH	0x3f0000
#endif

/*
 *	The senTec COBRA5282 board has the same memory layout as the M5282EVB.
 */
#if defined(CONFIG_COBRA5282)
#define  RAM_START   0x10000
#define  RAM_LENGTH  0x3f0000
#endif


/*
 *	The EMAC SoM-5282EM module.
 */
#if defined(CONFIG_SOM5282EM)
#define  RAM_START   0x10000
#define  RAM_LENGTH  0xff0000
#endif


/*
 *	These flash boot boards use all of ram for operation. Again the
 *	actual memory size is not important here, assume at least 4MiB.
 *	They currently have no support for running in flash.
 */
#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
    defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3) || \
    defined(CONFIG_HW_FEITH)
#define	RAM_START	0x400
#define	RAM_LENGTH	0x3ffc00
#endif

/*
 *	Sneha Boards mimimun memory
 *	The end of RAM will vary depending on how much ram is fitted,
 *	but this isn't important here, we assume at least 4MiB.
 */
#if defined(CONFIG_CPU16B)   
#define	RAM_START	0x20000
#define	RAM_LENGTH	0x3e0000
#endif

#if defined(CONFIG_MOD5272)
#define RAM_START	0x02000000
#define RAM_LENGTH	0x00800000
#define RAMVEC_START	0x20000000
#define RAMVEC_LENGTH	0x00000400
#endif

#if defined(CONFIG_RAMKERNEL)
#define	TEXT		ram
#define	DATA		ram
#define	INIT		ram
#define	BSS		ram
#endif
#if defined(CONFIG_ROMKERNEL) || defined(CONFIG_HIMEMKERNEL)
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
#ifdef RAMVEC_START
	ramvec	: ORIGIN = RAMVEC_START, LENGTH = RAMVEC_LENGTH
#endif
	ram	: ORIGIN = RAM_START, LENGTH = RAM_LENGTH
#ifdef RAM_END
	eram	: ORIGIN = RAM_END, LENGTH = 0
#endif
#ifdef ROM_START
	romvec	: ORIGIN = ROMVEC_START, LENGTH = ROMVEC_LENGTH
	rom	: ORIGIN = ROM_START, LENGTH = ROM_LENGTH
	erom	: ORIGIN = ROM_END, LENGTH = 0
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

		/* Kernel symbol table: Normal symbols */
		__start___kcrctab = .;
		*(__kcrctab)
		__stop___kcrctab = .;

		/* Kernel symbol table: GPL-only symbols */
		__start___kcrctab_gpl = .;
		*(__kcrctab_gpl)
		__stop___kcrctab_gpl = .;

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

#ifdef ROM_END
	. = ROM_END ;
	.erom : {
		__rom_end = . ;
	} > erom
#endif
#ifdef RAMVEC_START
	. = RAMVEC_START ;
	.ramvec : {
		__ramvec = .;
	} > ramvec
#endif

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
		*(.initcall1.init)
		*(.initcall2.init)
		*(.initcall3.init)
		*(.initcall4.init)
		*(.initcall5.init)
		*(.initcall6.init)
		*(.initcall7.init)
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

#ifdef RAM_END
	. = RAM_END ;
	.eram : {
		__ramend = . ;
		_ramend = . ;
	} > eram
#endif
}

