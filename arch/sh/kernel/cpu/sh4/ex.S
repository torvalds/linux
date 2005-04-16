/*
 *  arch/sh/kernel/cpu/sh4/ex.S
 *
 *  The SH-4 exception vector table.

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
#if defined(CONFIG_SH_FPU)
	.long	do_fpu_error		/* 120 */
#else
	.long	exception_error		/* 120 */
#endif
	.long	exception_error		/* 140 */
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
#if  defined(CONFIG_CPU_SUBTYPE_SH7760)
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error			/* 500 */
	.long	exception_error
	.long	exception_error
#else
	.long	do_IRQ	! RTC  ati
	.long	do_IRQ	!      pri
	.long	do_IRQ	!      cui
	.long	do_IRQ	! SCI  eri
	.long	do_IRQ	!      rxi	/* 500 */
	.long	do_IRQ	!      txi
	.long	do_IRQ	!      tei
#endif
	.long	do_IRQ	! WDT  iti	/* 560 */
	.long	do_IRQ	! REF  rcmi
	.long	do_IRQ	!      rovi
	.long	do_IRQ			
	.long	do_IRQ			/* 5E0 */
	.long	do_IRQ	! 32 Hitachi UDI	/* 600 */
	.long	do_IRQ	! 33 GPIO
	.long	do_IRQ	! 34 DMAC dmte0
	.long	do_IRQ	! 35      dmte1
	.long	do_IRQ	! 36      dmte2
	.long	do_IRQ	! 37      dmte3
	.long	do_IRQ	! 38      dmae
	.long	exception_error			! 39	/* 6E0 */
#if defined(CONFIG_CPU_SUBTYPE_SH7760)
	.long	exception_error				/* 700 */
	.long	exception_error
	.long	exception_error
	.long	exception_error				/* 760 */
#else
	.long	do_IRQ	! 40 SCIF eri		/* 700 */
	.long	do_IRQ	! 41      rxi
	.long	do_IRQ	! 42      bri
	.long	do_IRQ	! 43      txi
#endif
#if CONFIG_NR_ONCHIP_DMA_CHANNELS == 8
	.long	do_IRQ	! 44 DMAC dmte4		/* 780 */
	.long	do_IRQ	! 45      dmte5
	.long	do_IRQ	! 46      dmte6
	.long	do_IRQ	! 47      dmte7		/* 7E0 */
#else
	.long	exception_error			! 44	/* 780 */
	.long	exception_error			! 45
	.long	exception_error			! 46
	.long	exception_error			! 47
#endif
#if defined(CONFIG_SH_FPU)
	.long	do_fpu_state_restore	! 48	/* 800 */
	.long	do_fpu_state_restore	! 49	/* 820 */
#else
	.long	exception_error
	.long	exception_error
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7751)
	.long	exception_error			/* 840 */
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error			/* 900 */
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! PCI serr	/* A00 */
	.long	do_IRQ	!     dma3
	.long	do_IRQ	!     dma2
	.long	do_IRQ	!     dma1
	.long	do_IRQ	!     dma0
	.long	do_IRQ	!     pwon
	.long	do_IRQ	!     pwdwn
	.long	do_IRQ	!     err
	.long	do_IRQ	! TMU3 tuni3	/* B00 */
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! TMU4 tuni4	/* B80 */
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
	.long	do_IRQ	! IRQ	irq6	/* 840 */
	.long	do_IRQ	!	irq7
	.long	do_IRQ	! SCIF	eri0
	.long	do_IRQ	!	rxi0
	.long	do_IRQ	!	bri0
	.long	do_IRQ	!	txi0
	.long	do_IRQ	! HCAN2	cani0	/* 900 */
	.long	do_IRQ	!	cani1
	.long	do_IRQ	! SSI	ssii0
	.long	do_IRQ	!	ssii1
	.long	do_IRQ	! HAC	haci0
	.long	do_IRQ	!	haci1
	.long	do_IRQ	! IIC	iici0
	.long	do_IRQ	!	iici1
	.long	do_IRQ	! USB	usbi	/* A00 */
	.long	do_IRQ	! LCDC	vint
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! DMABRG dmabrgi0
	.long	do_IRQ	!        dmabrgi1
	.long	do_IRQ	!        dmabrgi2
	.long	exception_error
	.long	do_IRQ	! SCIF	eri1	/* B00 */
	.long	do_IRQ	!	rxi1
	.long	do_IRQ	!	bri1
	.long	do_IRQ	!	txi1
	.long	do_IRQ	!	eri2
	.long	do_IRQ	!	rxi2
	.long	do_IRQ	!	bri2
	.long	do_IRQ  !	txi2
	.long	do_IRQ	! SIM	simeri	/* C00 */
	.long	do_IRQ	!	simrxi
	.long	do_IRQ	!	simtxi
	.long	do_IRQ	!	simtei
	.long	do_IRQ	! HSPI	spii
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! MMCIF	mmci0	/* D00 */
	.long	do_IRQ	!	mmci1
	.long	do_IRQ	!	mmci2
	.long	do_IRQ	!	mmci3
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error			/* E00 */
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! MFI	mfii
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	exception_error			/* F00 */
	.long	exception_error
	.long	exception_error
	.long	exception_error
	.long	do_IRQ	! ADC	adi
	.long	do_IRQ	! CMT	cmti	/* FA0 */
#elif defined(CONFIG_CPU_SUBTYPE_SH73180)
	.long	do_IRQ	!  50 0x840
	.long	do_IRQ	!  51 0x860
	.long	do_IRQ	!  52 0x880
	.long	do_IRQ	!  53 0x8a0
	.long	do_IRQ	!  54 0x8c0
	.long	do_IRQ	!  55 0x8e0
	.long	do_IRQ	!  56 0x900
	.long	do_IRQ	!  57 0x920
	.long	do_IRQ	!  58 0x940
	.long	do_IRQ	!  59 0x960
	.long	do_IRQ	!  60 0x980
	.long	do_IRQ	!  61 0x9a0
	.long	do_IRQ	!  62 0x9c0
	.long	do_IRQ	!  63 0x9e0
	.long	do_IRQ	!  64 0xa00
	.long	do_IRQ	!  65 0xa20
	.long	do_IRQ	!  66 0xa40
	.long	do_IRQ	!  67 0xa60
	.long	do_IRQ	!  68 0xa80
	.long	do_IRQ	!  69 0xaa0
	.long	do_IRQ	!  70 0xac0
	.long	do_IRQ	!  71 0xae0
	.long	do_IRQ	!  72 0xb00
	.long	do_IRQ	!  73 0xb20
	.long	do_IRQ	!  74 0xb40
	.long	do_IRQ	!  75 0xb60
	.long	do_IRQ	!  76 0xb80
	.long	do_IRQ	!  77 0xba0
	.long	do_IRQ	!  78 0xbc0
	.long	do_IRQ	!  79 0xbe0
	.long	do_IRQ	!  80 0xc00
	.long	do_IRQ	!  81 0xc20
	.long	do_IRQ	!  82 0xc40
	.long	do_IRQ	!  83 0xc60
	.long	do_IRQ	!  84 0xc80
	.long	do_IRQ	!  85 0xca0
	.long	do_IRQ	!  86 0xcc0
	.long	do_IRQ	!  87 0xce0
	.long	do_IRQ	!  88 0xd00
	.long	do_IRQ	!  89 0xd20
	.long	do_IRQ	!  90 0xd40
	.long	do_IRQ	!  91 0xd60
	.long	do_IRQ	!  92 0xd80
	.long	do_IRQ	!  93 0xda0
	.long	do_IRQ	!  94 0xdc0
	.long	do_IRQ	!  95 0xde0
	.long	do_IRQ	!  96 0xe00
	.long	do_IRQ	!  97 0xe20
	.long	do_IRQ	!  98 0xe40
	.long	do_IRQ	!  99 0xe60
	.long	do_IRQ	! 100 0xe80
	.long	do_IRQ	! 101 0xea0
	.long	do_IRQ	! 102 0xec0
	.long	do_IRQ	! 103 0xee0
	.long	do_IRQ	! 104 0xf00
	.long	do_IRQ	! 105 0xf20
	.long	do_IRQ	! 106 0xf40
	.long	do_IRQ	! 107 0xf60
	.long	do_IRQ	! 108 0xf80
#elif defined(CONFIG_CPU_SUBTYPE_ST40STB1)
	.long	exception_error			!  50 0x840
	.long	exception_error			!  51 0x860
	.long	exception_error			!  52 0x880
	.long	exception_error			!  53 0x8a0
	.long	exception_error			!  54 0x8c0
	.long	exception_error			!  55 0x8e0
	.long	exception_error			!  56 0x900
	.long	exception_error			!  57 0x920
	.long	exception_error			!  58 0x940
	.long	exception_error			!  59 0x960
	.long	exception_error			!  60 0x980
	.long	exception_error			!  61 0x9a0
	.long	exception_error			!  62 0x9c0
	.long	exception_error			!  63 0x9e0
	.long	do_IRQ	!  64 0xa00 PCI serr
	.long	do_IRQ	!  65 0xa20     err
	.long	do_IRQ	!  66 0xa40     ad
	.long	do_IRQ	!  67 0xa60     pwr_dwn
	.long	exception_error			!  68 0xa80
	.long	exception_error			!  69 0xaa0
	.long	exception_error			!  70 0xac0
	.long	exception_error			!  71 0xae0
	.long	do_IRQ	!  72 0xb00 DMA INT0
	.long	do_IRQ	!  73 0xb20     INT1
	.long	do_IRQ	!  74 0xb40     INT2
	.long	do_IRQ	!  75 0xb60     INT3
	.long	do_IRQ	!  76 0xb80     INT4
	.long	exception_error			!  77 0xba0
	.long	do_IRQ	!  78 0xbc0 DMA ERR
	.long	exception_error			!  79 0xbe0
	.long	do_IRQ	!  80 0xc00 PIO0
	.long	do_IRQ	!  81 0xc20 PIO1
	.long	do_IRQ	!  82 0xc40 PIO2
	.long	exception_error			!  83 0xc60
	.long	exception_error			!  84 0xc80
	.long	exception_error			!  85 0xca0
	.long	exception_error			!  86 0xcc0
	.long	exception_error			!  87 0xce0
	.long	exception_error			!  88 0xd00
	.long	exception_error			!  89 0xd20
	.long	exception_error			!  90 0xd40
	.long	exception_error			!  91 0xd60
	.long	exception_error			!  92 0xd80
	.long	exception_error			!  93 0xda0
	.long	exception_error			!  94 0xdc0
	.long	exception_error			!  95 0xde0
	.long	exception_error			!  96 0xe00
	.long	exception_error			!  97 0xe20
	.long	exception_error			!  98 0xe40
	.long	exception_error			!  99 0xe60
	.long	exception_error			! 100 0xe80
	.long	exception_error			! 101 0xea0
	.long	exception_error			! 102 0xec0
	.long	exception_error			! 103 0xee0
	.long	exception_error			! 104 0xf00
	.long	exception_error			! 105 0xf20
	.long	exception_error			! 106 0xf40
	.long	exception_error			! 107 0xf60
	.long	exception_error			! 108 0xf80
	.long	exception_error			! 109 0xfa0
	.long	exception_error			! 110 0xfc0
	.long	exception_error			! 111 0xfe0
	.long	do_IRQ	! 112 0x1000 Mailbox
	.long	exception_error			! 113 0x1020
	.long	exception_error			! 114 0x1040
	.long	exception_error			! 115 0x1060
	.long	exception_error			! 116 0x1080
	.long	exception_error			! 117 0x10a0
	.long	exception_error			! 118 0x10c0
	.long	exception_error			! 119 0x10e0
	.long	exception_error			! 120 0x1100
	.long	exception_error			! 121 0x1120
	.long	exception_error			! 122 0x1140
	.long	exception_error			! 123 0x1160
	.long	exception_error			! 124 0x1180
	.long	exception_error			! 125 0x11a0
	.long	exception_error			! 126 0x11c0
	.long	exception_error			! 127 0x11e0
	.long	exception_error			! 128 0x1200
	.long	exception_error			! 129 0x1220
	.long	exception_error			! 130 0x1240
	.long	exception_error			! 131 0x1260
	.long	exception_error			! 132 0x1280
	.long	exception_error			! 133 0x12a0
	.long	exception_error			! 134 0x12c0
	.long	exception_error			! 135 0x12e0
	.long	exception_error			! 136 0x1300
	.long	exception_error			! 137 0x1320
	.long	exception_error			! 138 0x1340
	.long	exception_error			! 139 0x1360
	.long	do_IRQ	! 140 0x1380 EMPI INV_ADDR
	.long	exception_error			! 141 0x13a0
	.long	exception_error			! 142 0x13c0
	.long	exception_error			! 143 0x13e0
#endif

