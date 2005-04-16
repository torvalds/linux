/*
 *  arch/sh/kernel/cpu/sh3/ex.S
 *
 *  The SH-3 exception vector table.

 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/linkage.h>
#include <linux/config.h>

	.align 2
	.data

ENTRY(exception_handling_table)
	.long	exception_error		/* 000 */
	.long	exception_error
#if defined(CONFIG_MMU)
	.long	tlb_miss_load		/* 040 */
	.long	tlb_miss_store
	.long	initial_page_write
	.long	tlb_protection_violation_load
	.long	tlb_protection_violation_store
	.long	address_error_load
	.long	address_error_store	/* 100 */
#else
	.long	exception_error	! tlb miss load		/* 040 */
	.long	exception_error	! tlb miss store
	.long	exception_error	! initial page write
	.long	exception_error	! tlb prot violation load
	.long	exception_error	! tlb prot violation store
	.long	exception_error	! address error load
	.long	exception_error	! address error store	/* 100 */
#endif
	.long	exception_error	! fpu_exception	/* 120 */
	.long	exception_error			/* 140 */
	.long	system_call	! Unconditional Trap	 /* 160 */
	.long	exception_error	! reserved_instruction (filled by trap_init) /* 180 */
	.long	exception_error	! illegal_slot_instruction (filled by trap_init) /*1A0*/
ENTRY(nmi_slot)
#if defined (CONFIG_KGDB_NMI)
	.long	debug_enter	/* 1C0 */	! Allow trap to debugger
#else
	.long	exception_none	/* 1C0 */	! Not implemented yet
#endif
ENTRY(user_break_point_trap)
	.long	break_point_trap	/* 1E0 */
ENTRY(interrupt_table)
	! external hardware
	.long	do_IRQ	! 0000		/* 200 */
	.long	do_IRQ	! 0001
	.long	do_IRQ	! 0010
	.long	do_IRQ	! 0011
	.long	do_IRQ	! 0100
	.long	do_IRQ	! 0101
	.long	do_IRQ	! 0110
	.long	do_IRQ	! 0111
	.long	do_IRQ	! 1000		/* 300 */
	.long	do_IRQ	! 1001
	.long	do_IRQ	! 1010
	.long	do_IRQ	! 1011
	.long	do_IRQ	! 1100
	.long	do_IRQ	! 1101
	.long	do_IRQ	! 1110
	.long	exception_error		
	! Internal hardware
	.long	do_IRQ	! TMU0 tuni0	/* 400 */
	.long	do_IRQ	! TMU1 tuni1
	.long	do_IRQ	! TMU2 tuni2
	.long	do_IRQ	!      ticpi2
	.long	do_IRQ	! RTC  ati
	.long	do_IRQ	!      pri
	.long	do_IRQ	!      cui
	.long	do_IRQ	! SCI  eri
	.long	do_IRQ	!      rxi	/* 500 */
	.long	do_IRQ	!      txi
	.long	do_IRQ	!      tei
	.long	do_IRQ	! WDT  iti	/* 560 */
	.long	do_IRQ	! REF  rcmi
	.long	do_IRQ	!      rovi
	.long	do_IRQ			
	.long	do_IRQ			/* 5E0 */
#if  defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709) || \
     defined(CONFIG_CPU_SUBTYPE_SH7300) || defined(CONFIG_CPU_SUBTYPE_SH7705)
	.long	do_IRQ	! 32 IRQ  irq0	/* 600 */
	.long	do_IRQ	! 33      irq1
	.long	do_IRQ	! 34      irq2
	.long	do_IRQ	! 35      irq3
	.long	do_IRQ	! 36      irq4
	.long	do_IRQ	! 37      irq5
	.long	do_IRQ	! 38
	.long	do_IRQ	! 39
	.long	do_IRQ	! 40 PINT pint0-7	/* 700 */
	.long	do_IRQ	! 41      pint8-15
	.long	do_IRQ	! 42
	.long	do_IRQ	! 43
	.long	do_IRQ	! 44
	.long	do_IRQ	! 45	
	.long	do_IRQ	! 46
	.long	do_IRQ	! 47
	.long	do_IRQ	! 48 DMAC dei0	/* 800 */
	.long	do_IRQ	! 49      dei1
	.long	do_IRQ	! 50      dei2
	.long	do_IRQ	! 51      dei3
	.long	do_IRQ	! 52 IrDA eri1
	.long	do_IRQ	! 53      rxi1
	.long	do_IRQ	! 54      bri1
	.long	do_IRQ	! 55      txi1
	.long	do_IRQ	! 56 SCIF eri2
	.long	do_IRQ	! 57      rxi2
	.long	do_IRQ	! 58      bri2
	.long	do_IRQ	! 59      txi2
	.long	do_IRQ	! 60 ADC  adi	/* 980 */
#if defined(CONFIG_CPU_SUBTYPE_SH7705)
	.long	exception_none	! 61	/* 9A0 */
	.long	exception_none	! 62
	.long	exception_none	! 63
	.long	exception_none	! 64	/* A00 */
	.long	do_IRQ	! 65 USB  usi0
	.long	do_IRQ	! 66      usi1
	.long	exception_none	! 67
	.long	exception_none	! 68
	.long	exception_none	! 69
	.long	exception_none	! 70
	.long	exception_none	! 71
	.long	exception_none	! 72	/* B00 */
	.long	exception_none	! 73
	.long	exception_none	! 74
	.long	exception_none	! 75
	.long	exception_none	! 76
	.long	exception_none	! 77
	.long	exception_none	! 78
	.long	exception_none	! 79
	.long	do_IRQ	! 80 TPU0 tpi0	/* C00 */
	.long	do_IRQ	! 81 TPU1 tpi1
	.long	exception_none	! 82
	.long	exception_none	! 83
	.long	do_IRQ	! 84 TPU2 tpi2
	.long	do_IRQ	! 85 TPU3 tpi3	/* CA0 */
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7300)
	.long   do_IRQ	! 61 LCDC lcdi	/* 9A0 */
	.long   do_IRQ	! 62 PCC  pcc0i
	.long   do_IRQ	! 63      pcc1i	/* 9E0 */
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7300)
	.long   do_IRQ	! 64
	.long   do_IRQ	! 65
	.long   do_IRQ	! 66
	.long   do_IRQ	! 67
	.long   do_IRQ	! 68
	.long   do_IRQ	! 69
	.long   do_IRQ	! 70
	.long   do_IRQ	! 71
	.long   do_IRQ	! 72
	.long   do_IRQ	! 73
	.long   do_IRQ	! 74
	.long   do_IRQ	! 75
	.long   do_IRQ	! 76
	.long   do_IRQ	! 77
	.long   do_IRQ	! 78
	.long   do_IRQ	! 79
	.long   do_IRQ	! 80 SCIF0(SH7300)
	.long   do_IRQ	! 81
	.long   do_IRQ	! 82
	.long   do_IRQ	! 83
	.long   do_IRQ	! 84
	.long   do_IRQ	! 85
	.long   do_IRQ	! 86
	.long   do_IRQ	! 87
	.long   do_IRQ	! 88
	.long   do_IRQ	! 89
	.long   do_IRQ	! 90
	.long   do_IRQ	! 91
	.long   do_IRQ	! 92
	.long   do_IRQ	! 93
	.long   do_IRQ	! 94
	.long   do_IRQ	! 95
	.long   do_IRQ	! 96
	.long   do_IRQ	! 97
	.long   do_IRQ	! 98
	.long   do_IRQ	! 99
	.long   do_IRQ	! 100
	.long   do_IRQ	! 101
	.long   do_IRQ	! 102
	.long   do_IRQ	! 103
	.long   do_IRQ	! 104
	.long   do_IRQ	! 105
	.long   do_IRQ	! 106
	.long   do_IRQ	! 107
	.long   do_IRQ	! 108
#endif
#endif

