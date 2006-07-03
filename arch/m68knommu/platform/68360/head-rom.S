/* arch/m68knommu/platform/68360/head-rom.S
 *
 * Startup code for Motorola 68360
 *
 * Copyright (C) SED Systems, a Division of Calian Ltd.
 * Based on: arch/m68knommu/platform/68328/pilot/crt0_rom.S
 * Based on: arch/m68knommu/platform/68360/uCquicc/crt0_rom.S, 2.0.38.1.pre7
 *           uClinux Kernel
 * Copyright (C) Michael Leslie <mleslie@lineo.com>
 * Based on: arch/m68knommu/platform/68EZ328/ucsimm/crt0_rom.S
 * Copyright (C) 1998  D. Jeff Dionne <jeff@uclinux.org>,
 *
 */

.global _stext
.global _sbss
.global _start

.global _rambase
.global _ramvec
.global _ramstart
.global _ramend

.global _quicc_base
.global _periph_base

#define	RAMEND                      (CONFIG_RAMBASE + CONFIG_RAMSIZE)

#define REGB                        0x1000
#define PEPAR                       (_dprbase + REGB + 0x0016)
#define GMR                         (_dprbase + REGB + 0x0040)
#define OR0                         (_dprbase + REGB + 0x0054)
#define BR0                         (_dprbase + REGB + 0x0050)

#define OR1                         (_dprbase + REGB + 0x0064)
#define BR1                         (_dprbase + REGB + 0x0060)

#define OR2                         (_dprbase + REGB + 0x0074)
#define BR2                         (_dprbase + REGB + 0x0070)

#define OR3                         (_dprbase + REGB + 0x0084)
#define BR3                         (_dprbase + REGB + 0x0080)

#define OR4                         (_dprbase + REGB + 0x0094)
#define BR4                         (_dprbase + REGB + 0x0090)

#define OR5                         (_dprbase + REGB + 0x00A4)
#define BR5                         (_dprbase + REGB + 0x00A0)

#define OR6                         (_dprbase + REGB + 0x00b4)
#define BR6                         (_dprbase + REGB + 0x00b0)

#define OR7                         (_dprbase + REGB + 0x00c4)
#define BR7                         (_dprbase + REGB + 0x00c0)

#define MCR                         (_dprbase + REGB + 0x0000)
#define AVR                         (_dprbase + REGB + 0x0008)

#define SYPCR                       (_dprbase + REGB + 0x0022)

#define PLLCR                       (_dprbase + REGB + 0x0010)
#define CLKOCR                      (_dprbase + REGB + 0x000C)
#define CDVCR                       (_dprbase + REGB + 0x0014)

#define BKAR                        (_dprbase + REGB + 0x0030)
#define BKCR                        (_dprbase + REGB + 0x0034)
#define SWIV                        (_dprbase + REGB + 0x0023)
#define PICR                        (_dprbase + REGB + 0x0026)
#define PITR                        (_dprbase + REGB + 0x002A)

/* Define for all memory configuration */
#define MCU_SIM_GMR                 0x00000000
#define SIM_OR_MASK                 0x0fffffff

/* Defines for chip select zero - the flash */
#define SIM_OR0_MASK                0x20000000
#define SIM_BR0_MASK                0x00000001

/* Defines for chip select one - the RAM */
#define SIM_OR1_MASK                0x10000000
#define SIM_BR1_MASK                0x00000001

#define MCU_SIM_MBAR_ADRS           0x0003ff00
#define MCU_SIM_MBAR_BA_MASK        0xfffff000
#define MCU_SIM_MBAR_AS_MASK        0x00000001

#define MCU_SIM_PEPAR               0x00B4
    
#define MCU_DISABLE_INTRPTS         0x2700
#define MCU_SIM_AVR                 0x00
    
#define MCU_SIM_MCR                 0x00005cff

#define MCU_SIM_CLKOCR              0x00
#define MCU_SIM_PLLCR               0x8000
#define MCU_SIM_CDVCR               0x0000

#define MCU_SIM_SYPCR               0x0000
#define MCU_SIM_SWIV                0x00
#define MCU_SIM_PICR                0x0000
#define MCU_SIM_PITR                0x0000


#include <asm/m68360_regs.h>

	
/*
 * By the time this RAM specific code begins to execute, DPRAM
 * and DRAM should already be mapped and accessible.
 */

	.text
_start:
_stext:
	nop
	ori.w	#MCU_DISABLE_INTRPTS, %sr	/* disable interrupts: */
	/* We should not need to setup the boot stack the reset should do it. */
	movea.l	#RAMEND, %sp		/* set up stack at the end of DRAM:*/


set_mbar_register:
	moveq.l	#0x07, %d1			/* Setup MBAR */
	movec	%d1, %dfc

	lea.l	MCU_SIM_MBAR_ADRS, %a0
	move.l	#_dprbase, %d0
	andi.l	#MCU_SIM_MBAR_BA_MASK, %d0
	ori.l	#MCU_SIM_MBAR_AS_MASK, %d0
	moves.l	%d0, %a0@

	moveq.l	#0x05, %d1
	movec.l	%d1, %dfc

	/* Now we can begin to access registers in DPRAM */

set_sim_mcr:
	/* Set Module Configuration Register */
	move.l	#MCU_SIM_MCR, MCR

	/* to do:	Determine cause of reset */

	/*
	 *	configure system clock MC68360 p. 6-40
	 *	(value +1)*osc/128 = system clock
	 *                    or
	 *	(value + 1)*osc = system clock
	 *	You do not need to divide the oscillator by 128 unless you want to.
	 */
set_sim_clock:
	move.w	#MCU_SIM_PLLCR, PLLCR
	move.b	#MCU_SIM_CLKOCR, CLKOCR
	move.w	#MCU_SIM_CDVCR, CDVCR

	/* Wait for the PLL to settle */
	move.w	#16384, %d0
pll_settle_wait:
	subi.w	#1, %d0
	bne	pll_settle_wait

	/* Setup the system protection register, and watchdog timer register */
	move.b	#MCU_SIM_SWIV, SWIV
	move.w	#MCU_SIM_PICR, PICR
	move.w	#MCU_SIM_PITR, PITR
	move.w	#MCU_SIM_SYPCR, SYPCR

	/* Clear DPRAM - system + parameter */
	movea.l	#_dprbase, %a0
	movea.l	#_dprbase+0x2000, %a1

	/* Copy 0 to %a0 until %a0 == %a1 */
clear_dpram:
	movel	#0, %a0@+
	cmpal	%a0, %a1
	bhi	clear_dpram

configure_memory_controller:    
	/* Set up Global Memory Register (GMR) */
	move.l	#MCU_SIM_GMR, %d0
	move.l	%d0, GMR

configure_chip_select_0:
	move.l	#0x00400000, %d0
	subq.l	#0x01, %d0
	eori.l	#SIM_OR_MASK, %d0
	ori.l	#SIM_OR0_MASK, %d0
	move.l	%d0, OR0

	move.l	#__rom_start, %d0
	ori.l	#SIM_BR0_MASK, %d0
	move.l	%d0, BR0

	move.l	#0x0, BR1
	move.l	#0x0, BR2
	move.l	#0x0, BR3
	move.l	#0x0, BR4
	move.l	#0x0, BR5
	move.l	#0x0, BR6
	move.l	#0x0, BR7

	move.w	#MCU_SIM_PEPAR, PEPAR 

	/* point to vector table: */
	move.l	#_romvec, %a0
	move.l	#_ramvec, %a1
copy_vectors:
	move.l	%a0@, %d0
	move.l	%d0, %a1@
	move.l	%a0@, %a1@
	addq.l	#0x04, %a0
	addq.l	#0x04, %a1
	cmp.l	#_start, %a0
	blt	copy_vectors

	move.l	#_ramvec, %a1
	movec	%a1, %vbr


	/* Copy data segment from ROM to RAM */
	moveal	#_etext, %a0
	moveal	#_sdata, %a1
	moveal	#_edata, %a2

	/* Copy %a0 to %a1 until %a1 == %a2 */
LD1:
	move.l	%a0@, %d0
	addq.l	#0x04, %a0
	move.l	%d0, %a1@
	addq.l	#0x04, %a1
	cmp.l	#_edata, %a1
	blt	LD1

	moveal	#_sbss, %a0
	moveal	#_ebss, %a1

	/* Copy 0 to %a0 until %a0 == %a1 */
L1:
	movel	#0, %a0@+
	cmpal	%a0, %a1
	bhi	L1

load_quicc:
	move.l	#_dprbase, _quicc_base

store_ram_size:
	/* Set ram size information */
	move.l	#_sdata, _rambase
	move.l	#_ebss, _ramstart
	move.l	#RAMEND, %d0
	sub.l	#0x1000, %d0			/* Reserve 4K for stack space.*/
	move.l	%d0, _ramend			/* Different from RAMEND.*/

	pea	0
	pea	env
	pea	%sp@(4)
	pea	0

	lea	init_thread_union, %a2
	lea	0x2000(%a2), %sp

lp:
	jsr	start_kernel

_exit:
	jmp	_exit


	.data
	.align 4
env:
	.long	0
_quicc_base:
	.long	0
_periph_base:
	.long	0
_ramvec:
	.long   0
_rambase:
	.long   0
_ramstart:
	.long   0
_ramend:
	.long   0
_dprbase:
	.long	0xffffe000


	.text

    /*
     * These are the exception vectors at boot up, they are copied into RAM
     * and then overwritten as needed.
     */
 
.section ".data.initvect","awx"
    .long   RAMEND	/* Reset: Initial Stack Pointer                 - 0.  */
    .long   _start      /* Reset: Initial Program Counter               - 1.  */
    .long   buserr      /* Bus Error                                    - 2.  */
    .long   trap        /* Address Error                                - 3.  */
    .long   trap        /* Illegal Instruction                          - 4.  */
    .long   trap        /* Divide by zero                               - 5.  */
    .long   trap        /* CHK, CHK2 Instructions                       - 6.  */
    .long   trap        /* TRAPcc, TRAPV Instructions                   - 7.  */
    .long   trap        /* Privilege Violation                          - 8.  */
    .long   trap        /* Trace                                        - 9.  */
    .long   trap        /* Line 1010 Emulator                           - 10. */
    .long   trap        /* Line 1111 Emualtor                           - 11. */
    .long   trap        /* Harware Breakpoint                           - 12. */
    .long   trap        /* (Reserved for Coprocessor Protocol Violation)- 13. */
    .long   trap        /* Format Error                                 - 14. */
    .long   trap        /* Uninitialized Interrupt                      - 15. */
    .long   trap        /* (Unassigned, Reserver)                       - 16. */
    .long   trap        /* (Unassigned, Reserver)                       - 17. */
    .long   trap        /* (Unassigned, Reserver)                       - 18. */
    .long   trap        /* (Unassigned, Reserver)                       - 19. */
    .long   trap        /* (Unassigned, Reserver)                       - 20. */
    .long   trap        /* (Unassigned, Reserver)                       - 21. */
    .long   trap        /* (Unassigned, Reserver)                       - 22. */
    .long   trap        /* (Unassigned, Reserver)                       - 23. */
    .long   trap        /* Spurious Interrupt                           - 24. */
    .long   trap        /* Level 1 Interrupt Autovector                 - 25. */
    .long   trap        /* Level 2 Interrupt Autovector                 - 26. */
    .long   trap        /* Level 3 Interrupt Autovector                 - 27. */
    .long   trap        /* Level 4 Interrupt Autovector                 - 28. */
    .long   trap        /* Level 5 Interrupt Autovector                 - 29. */
    .long   trap        /* Level 6 Interrupt Autovector                 - 30. */
    .long   trap        /* Level 7 Interrupt Autovector                 - 31. */
    .long   system_call /* Trap Instruction Vectors 0                   - 32. */
    .long   trap        /* Trap Instruction Vectors 1                   - 33. */
    .long   trap        /* Trap Instruction Vectors 2                   - 34. */
    .long   trap        /* Trap Instruction Vectors 3                   - 35. */
    .long   trap        /* Trap Instruction Vectors 4                   - 36. */
    .long   trap        /* Trap Instruction Vectors 5                   - 37. */
    .long   trap        /* Trap Instruction Vectors 6                   - 38. */
    .long   trap        /* Trap Instruction Vectors 7                   - 39. */
    .long   trap        /* Trap Instruction Vectors 8                   - 40. */
    .long   trap        /* Trap Instruction Vectors 9                   - 41. */
    .long   trap        /* Trap Instruction Vectors 10                  - 42. */
    .long   trap        /* Trap Instruction Vectors 11                  - 43. */
    .long   trap        /* Trap Instruction Vectors 12                  - 44. */
    .long   trap        /* Trap Instruction Vectors 13                  - 45. */
    .long   trap        /* Trap Instruction Vectors 14                  - 46. */
    .long   trap        /* Trap Instruction Vectors 15                  - 47. */
    .long   0           /* (Reserved for Coprocessor)                   - 48. */
    .long   0           /* (Reserved for Coprocessor)                   - 49. */
    .long   0           /* (Reserved for Coprocessor)                   - 50. */
    .long   0           /* (Reserved for Coprocessor)                   - 51. */
    .long   0           /* (Reserved for Coprocessor)                   - 52. */
    .long   0           /* (Reserved for Coprocessor)                   - 53. */
    .long   0           /* (Reserved for Coprocessor)                   - 54. */
    .long   0           /* (Reserved for Coprocessor)                   - 55. */
    .long   0           /* (Reserved for Coprocessor)                   - 56. */
    .long   0           /* (Reserved for Coprocessor)                   - 57. */
    .long   0           /* (Reserved for Coprocessor)                   - 58. */
    .long   0           /* (Unassigned, Reserved)                       - 59. */
    .long   0           /* (Unassigned, Reserved)                       - 60. */
    .long   0           /* (Unassigned, Reserved)                       - 61. */
    .long   0           /* (Unassigned, Reserved)                       - 62. */
    .long   0           /* (Unassigned, Reserved)                       - 63. */
    /*                  The assignment of these vectors to the CPM is         */
    /*                  dependent on the configuration of the CPM vba         */
    /*                          fields.                                       */
    .long   0           /* (User-Defined Vectors 1) CPM Error           - 64. */
    .long   0           /* (User-Defined Vectors 2) CPM Parallel IO PC11- 65. */
    .long   0           /* (User-Defined Vectors 3) CPM Parallel IO PC10- 66. */
    .long   0           /* (User-Defined Vectors 4) CPM SMC2 / PIP      - 67. */
    .long   0           /* (User-Defined Vectors 5) CPM SMC1            - 68. */
    .long   0           /* (User-Defined Vectors 6) CPM SPI             - 69. */
    .long   0           /* (User-Defined Vectors 7) CPM Parallel IO PC9 - 70. */
    .long   0           /* (User-Defined Vectors 8) CPM Timer 4         - 71. */
    .long   0           /* (User-Defined Vectors 9) CPM Reserved        - 72. */
    .long   0           /* (User-Defined Vectors 10) CPM Parallel IO PC8- 73. */
    .long   0           /* (User-Defined Vectors 11) CPM Parallel IO PC7- 74. */
    .long   0           /* (User-Defined Vectors 12) CPM Parallel IO PC6- 75. */
    .long   0           /* (User-Defined Vectors 13) CPM Timer 3        - 76. */
    .long   0           /* (User-Defined Vectors 14) CPM Reserved       - 77. */
    .long   0           /* (User-Defined Vectors 15) CPM Parallel IO PC5- 78. */
    .long   0           /* (User-Defined Vectors 16) CPM Parallel IO PC4- 79. */
    .long   0           /* (User-Defined Vectors 17) CPM Reserved       - 80. */
    .long   0           /* (User-Defined Vectors 18) CPM RISC Timer Tbl - 81. */
    .long   0           /* (User-Defined Vectors 19) CPM Timer 2        - 82. */
    .long   0           /* (User-Defined Vectors 21) CPM Reserved       - 83. */
    .long   0           /* (User-Defined Vectors 22) CPM IDMA2          - 84. */
    .long   0           /* (User-Defined Vectors 23) CPM IDMA1          - 85. */
    .long   0           /* (User-Defined Vectors 24) CPM SDMA Bus Err   - 86. */
    .long   0           /* (User-Defined Vectors 25) CPM Parallel IO PC3- 87. */
    .long   0           /* (User-Defined Vectors 26) CPM Parallel IO PC2- 88. */
    .long   0           /* (User-Defined Vectors 27) CPM Timer 1        - 89. */
    .long   0           /* (User-Defined Vectors 28) CPM Parallel IO PC1- 90. */
    .long   0           /* (User-Defined Vectors 29) CPM SCC 4          - 91. */
    .long   0           /* (User-Defined Vectors 30) CPM SCC 3          - 92. */
    .long   0           /* (User-Defined Vectors 31) CPM SCC 2          - 93. */
    .long   0           /* (User-Defined Vectors 32) CPM SCC 1          - 94. */
    .long   0           /* (User-Defined Vectors 33) CPM Parallel IO PC0- 95. */
    /*                  I don't think anything uses the vectors after here.   */
    .long   0           /* (User-Defined Vectors 34)                    - 96. */
    .long   0,0,0,0,0               /* (User-Defined Vectors 35  -  39). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 40  -  49). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 50  -  59). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 60  -  69). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 70  -  79). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 80  -  89). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 90  -  99). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 100 - 109). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 110 - 119). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 120 - 129). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 130 - 139). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 140 - 149). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 150 - 159). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 160 - 169). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 170 - 179). */
    .long   0,0,0,0,0,0,0,0,0,0     /* (User-Defined Vectors 180 - 189). */
    .long   0,0,0                   /* (User-Defined Vectors 190 - 192). */
.text
ignore: rte
