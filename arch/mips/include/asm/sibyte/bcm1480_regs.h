/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  *********************************************************************
    *  BCM1255/BCM1280/BCM1455/BCM1480 Board Support Package
    *
    *  Register Definitions			File: bcm1480_regs.h
    *
    *  This module contains the addresses of the on-chip peripherals
    *  on the BCM1280 and BCM1480.
    *
    *  BCM1480 specification level:  1X55_1X80-UM100-D4 (11/24/03)
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
    *  Broadcom Corporation. All rights reserved.
    *
    ********************************************************************* */

#ifndef _BCM1480_REGS_H
#define _BCM1480_REGS_H

#include <asm/sibyte/sb1250_defs.h>

/*  *********************************************************************
    *  Pull in the BCM1250's registers since a great deal of the 1480's
    *  functions are the same as the BCM1250.
    ********************************************************************* */

#include <asm/sibyte/sb1250_regs.h>


/*  *********************************************************************
    *  Some general notes:
    *
    *  Register addresses are grouped by function and follow the order
    *  of the User Manual.
    *
    *  For the most part, when there is more than one peripheral
    *  of the same type on the SOC, the constants below will be
    *  offsets from the base of each peripheral.  For example,
    *  the MAC registers are described as offsets from the first
    *  MAC register, and there will be a MAC_REGISTER() macro
    *  to calculate the base address of a given MAC.
    *
    *  The information in this file is based on the BCM1X55/BCM1X80
    *  User Manual, Document 1X55_1X80-UM100-R, 22/12/03.
    *
    *  This file is basically a "what's new" header file.  Since the
    *  BCM1250 and the new BCM1480 (and derivatives) share many common
    *  features, this file contains only what's new or changed from
    *  the 1250.  (above, you can see that we include the 1250 symbols
    *  to get the base functionality).
    *
    *  In software, be sure to use the correct symbols, particularly
    *  for blocks that are different between the two chip families.
    *  All BCM1480-specific symbols have _BCM1480_ in their names,
    *  and all BCM1250-specific and "base" functions that are common in
    *  both chips have no special names (this is for compatibility with
    *  older include files).  Therefore, if you're working with the
    *  SCD, which is very different on each chip, A_SCD_xxx implies
    *  the BCM1250 version and A_BCM1480_SCD_xxx implies the BCM1480
    *  version.
    ********************************************************************* */


/*  *********************************************************************
    * Memory Controller Registers (Section 6)
    ********************************************************************* */

#define A_BCM1480_MC_BASE_0		    0x0010050000
#define A_BCM1480_MC_BASE_1		    0x0010051000
#define A_BCM1480_MC_BASE_2		    0x0010052000
#define A_BCM1480_MC_BASE_3		    0x0010053000
#define BCM1480_MC_REGISTER_SPACING	    0x1000

#define A_BCM1480_MC_BASE(ctlid)	    (A_BCM1480_MC_BASE_0+(ctlid)*BCM1480_MC_REGISTER_SPACING)
#define A_BCM1480_MC_REGISTER(ctlid, reg)    (A_BCM1480_MC_BASE(ctlid)+(reg))

#define R_BCM1480_MC_CONFIG		    0x0000000100
#define R_BCM1480_MC_CS_START		    0x0000000120
#define R_BCM1480_MC_CS_END		    0x0000000140
#define S_BCM1480_MC_CS_STARTEND	    24

#define R_BCM1480_MC_CS01_ROW0		    0x0000000180
#define R_BCM1480_MC_CS01_ROW1		    0x00000001A0
#define R_BCM1480_MC_CS23_ROW0		    0x0000000200
#define R_BCM1480_MC_CS23_ROW1		    0x0000000220
#define R_BCM1480_MC_CS01_COL0		    0x0000000280
#define R_BCM1480_MC_CS01_COL1		    0x00000002A0
#define R_BCM1480_MC_CS23_COL0		    0x0000000300
#define R_BCM1480_MC_CS23_COL1		    0x0000000320

#define R_BCM1480_MC_CSX_BASE		    0x0000000180
#define R_BCM1480_MC_CSX_ROW0		    0x0000000000   /* relative to CSX_BASE */
#define R_BCM1480_MC_CSX_ROW1		    0x0000000020   /* relative to CSX_BASE */
#define R_BCM1480_MC_CSX_COL0		    0x0000000100   /* relative to CSX_BASE */
#define R_BCM1480_MC_CSX_COL1		    0x0000000120   /* relative to CSX_BASE */
#define BCM1480_MC_CSX_SPACING		    0x0000000080   /* CS23 relative to CS01 */

#define R_BCM1480_MC_CS01_BA		    0x0000000380
#define R_BCM1480_MC_CS23_BA		    0x00000003A0
#define R_BCM1480_MC_DRAMCMD		    0x0000000400
#define R_BCM1480_MC_DRAMMODE		    0x0000000420
#define R_BCM1480_MC_CLOCK_CFG		    0x0000000440
#define R_BCM1480_MC_MCLK_CFG		    R_BCM1480_MC_CLOCK_CFG
#define R_BCM1480_MC_TEST_DATA		    0x0000000480
#define R_BCM1480_MC_TEST_ECC		    0x00000004A0
#define R_BCM1480_MC_TIMING1		    0x00000004C0
#define R_BCM1480_MC_TIMING2		    0x00000004E0
#define R_BCM1480_MC_DLL_CFG		    0x0000000500
#define R_BCM1480_MC_DRIVE_CFG		    0x0000000520

#if SIBYTE_HDR_FEATURE(1480, PASS2)
#define R_BCM1480_MC_ODT		    0x0000000460
#define R_BCM1480_MC_ECC_STATUS		    0x0000000540
#endif

/* Global registers (single instance) */
#define A_BCM1480_MC_GLB_CONFIG		    0x0010054100
#define A_BCM1480_MC_GLB_INTLV		    0x0010054120
#define A_BCM1480_MC_GLB_ECC_STATUS	    0x0010054140
#define A_BCM1480_MC_GLB_ECC_ADDR	    0x0010054160
#define A_BCM1480_MC_GLB_ECC_CORRECT	    0x0010054180
#define A_BCM1480_MC_GLB_PERF_CNT_CONTROL   0x00100541A0

/*  *********************************************************************
    * L2 Cache Control Registers (Section 5)
    ********************************************************************* */

#define A_BCM1480_L2_BASE		    0x0010040000

#define A_BCM1480_L2_READ_TAG		    0x0010040018
#define A_BCM1480_L2_ECC_TAG		    0x0010040038
#define A_BCM1480_L2_MISC0_VALUE	    0x0010040058
#define A_BCM1480_L2_MISC1_VALUE	    0x0010040078
#define A_BCM1480_L2_MISC2_VALUE	    0x0010040098
#define A_BCM1480_L2_MISC_CONFIG	    0x0010040040	/* x040 */
#define A_BCM1480_L2_CACHE_DISABLE	    0x0010040060	/* x060 */
#define A_BCM1480_L2_MAKECACHEDISABLE(x)    (A_BCM1480_L2_CACHE_DISABLE | (((x)&0xF) << 12))
#define A_BCM1480_L2_WAY_ENABLE_3_0	    0x0010040080	/* x080 */
#define A_BCM1480_L2_WAY_ENABLE_7_4	    0x00100400A0	/* x0A0 */
#define A_BCM1480_L2_MAKE_WAY_ENABLE_LO(x)  (A_BCM1480_L2_WAY_ENABLE_3_0 | (((x)&0xF) << 12))
#define A_BCM1480_L2_MAKE_WAY_ENABLE_HI(x)  (A_BCM1480_L2_WAY_ENABLE_7_4 | (((x)&0xF) << 12))
#define A_BCM1480_L2_MAKE_WAY_DISABLE_LO(x)  (A_BCM1480_L2_WAY_ENABLE_3_0 | (((~x)&0xF) << 12))
#define A_BCM1480_L2_MAKE_WAY_DISABLE_HI(x)  (A_BCM1480_L2_WAY_ENABLE_7_4 | (((~x)&0xF) << 12))
#define A_BCM1480_L2_WAY_LOCAL_3_0	    0x0010040100	/* x100 */
#define A_BCM1480_L2_WAY_LOCAL_7_4	    0x0010040120	/* x120 */
#define A_BCM1480_L2_WAY_REMOTE_3_0	    0x0010040140	/* x140 */
#define A_BCM1480_L2_WAY_REMOTE_7_4	    0x0010040160	/* x160 */
#define A_BCM1480_L2_WAY_AGENT_3_0	    0x00100400C0	/* xxC0 */
#define A_BCM1480_L2_WAY_AGENT_7_4	    0x00100400E0	/* xxE0 */
#define A_BCM1480_L2_WAY_ENABLE(A, banks)   (A | (((~(banks))&0x0F) << 8))
#define A_BCM1480_L2_BANK_BASE		    0x00D0300000
#define A_BCM1480_L2_BANK_ADDRESS(b)	    (A_BCM1480_L2_BANK_BASE | (((b)&0x7)<<17))
#define A_BCM1480_L2_MGMT_TAG_BASE	    0x00D0000000


/*  *********************************************************************
    * PCI-X Interface Registers (Section 7)
    ********************************************************************* */

#define A_BCM1480_PCI_BASE		    0x0010061400

#define A_BCM1480_PCI_RESET		    0x0010061400
#define A_BCM1480_PCI_DLL		    0x0010061500

#define A_BCM1480_PCI_TYPE00_HEADER	    0x002E000000

/*  *********************************************************************
    * Ethernet MAC Registers (Section 11) and DMA Registers (Section 10.6)
    ********************************************************************* */

/* No register changes with Rev.C BCM1250, but one additional MAC */

#define A_BCM1480_MAC_BASE_2	    0x0010066000

#ifndef A_MAC_BASE_2
#define A_MAC_BASE_2		    A_BCM1480_MAC_BASE_2
#endif

#define A_BCM1480_MAC_BASE_3	    0x0010067000
#define A_MAC_BASE_3		    A_BCM1480_MAC_BASE_3

#define R_BCM1480_MAC_DMA_OODPKTLOST	    0x00000038

#ifndef R_MAC_DMA_OODPKTLOST
#define R_MAC_DMA_OODPKTLOST	    R_BCM1480_MAC_DMA_OODPKTLOST
#endif


/*  *********************************************************************
    * DUART Registers (Section 14)
    ********************************************************************* */

/* No significant differences from BCM1250, two DUARTs */

/*  Conventions, per user manual:
 *     DUART	generic, channels A,B,C,D
 *     DUART0	implementing channels A,B
 *     DUART1	inplementing channels C,D
 */

#define BCM1480_DUART_NUM_PORTS		  4

#define A_BCM1480_DUART0		    0x0010060000
#define A_BCM1480_DUART1		    0x0010060400
#define A_BCM1480_DUART(chan)		    ((((chan)&2) == 0)? A_BCM1480_DUART0 : A_BCM1480_DUART1)

#define BCM1480_DUART_CHANREG_SPACING	    0x100
#define A_BCM1480_DUART_CHANREG(chan, reg)				\
	(A_BCM1480_DUART(chan) +					\
	 BCM1480_DUART_CHANREG_SPACING * (((chan) & 1) + 1) + (reg))
#define A_BCM1480_DUART_CTRLREG(chan, reg)				\
	(A_BCM1480_DUART(chan) +					\
	 BCM1480_DUART_CHANREG_SPACING * 3 + (reg))

#define DUART_IMRISR_SPACING	    0x20
#define DUART_INCHNG_SPACING	    0x10

#define R_BCM1480_DUART_IMRREG(chan)					\
	(R_DUART_IMR_A + ((chan) & 1) * DUART_IMRISR_SPACING)
#define R_BCM1480_DUART_ISRREG(chan)					\
	(R_DUART_ISR_A + ((chan) & 1) * DUART_IMRISR_SPACING)
#define R_BCM1480_DUART_INCHREG(chan)					\
	(R_DUART_IN_CHNG_A + ((chan) & 1) * DUART_INCHNG_SPACING)

#define A_BCM1480_DUART_IMRREG(chan)					\
	(A_BCM1480_DUART_CTRLREG((chan), R_BCM1480_DUART_IMRREG(chan)))
#define A_BCM1480_DUART_ISRREG(chan)					\
	(A_BCM1480_DUART_CTRLREG((chan), R_BCM1480_DUART_ISRREG(chan)))

#define A_BCM1480_DUART_IN_PORT(chan)					\
	(A_BCM1480_DUART_CTRLREG((chan), R_DUART_IN_PORT))

/*
 * These constants are the absolute addresses.
 */

#define A_BCM1480_DUART_MODE_REG_1_C	    0x0010060400
#define A_BCM1480_DUART_MODE_REG_2_C	    0x0010060410
#define A_BCM1480_DUART_STATUS_C	    0x0010060420
#define A_BCM1480_DUART_CLK_SEL_C	    0x0010060430
#define A_BCM1480_DUART_FULL_CTL_C	    0x0010060440
#define A_BCM1480_DUART_CMD_C		    0x0010060450
#define A_BCM1480_DUART_RX_HOLD_C	    0x0010060460
#define A_BCM1480_DUART_TX_HOLD_C	    0x0010060470
#define A_BCM1480_DUART_OPCR_C		    0x0010060480
#define A_BCM1480_DUART_AUX_CTRL_C	    0x0010060490

#define A_BCM1480_DUART_MODE_REG_1_D	    0x0010060500
#define A_BCM1480_DUART_MODE_REG_2_D	    0x0010060510
#define A_BCM1480_DUART_STATUS_D	    0x0010060520
#define A_BCM1480_DUART_CLK_SEL_D	    0x0010060530
#define A_BCM1480_DUART_FULL_CTL_D	    0x0010060540
#define A_BCM1480_DUART_CMD_D		    0x0010060550
#define A_BCM1480_DUART_RX_HOLD_D	    0x0010060560
#define A_BCM1480_DUART_TX_HOLD_D	    0x0010060570
#define A_BCM1480_DUART_OPCR_D		    0x0010060580
#define A_BCM1480_DUART_AUX_CTRL_D	    0x0010060590

#define A_BCM1480_DUART_INPORT_CHNG_CD	    0x0010060600
#define A_BCM1480_DUART_AUX_CTRL_CD	    0x0010060610
#define A_BCM1480_DUART_ISR_C		    0x0010060620
#define A_BCM1480_DUART_IMR_C		    0x0010060630
#define A_BCM1480_DUART_ISR_D		    0x0010060640
#define A_BCM1480_DUART_IMR_D		    0x0010060650
#define A_BCM1480_DUART_OUT_PORT_CD	    0x0010060660
#define A_BCM1480_DUART_OPCR_CD		    0x0010060670
#define A_BCM1480_DUART_IN_PORT_CD	    0x0010060680
#define A_BCM1480_DUART_ISR_CD		    0x0010060690
#define A_BCM1480_DUART_IMR_CD		    0x00100606A0
#define A_BCM1480_DUART_SET_OPR_CD	    0x00100606B0
#define A_BCM1480_DUART_CLEAR_OPR_CD	    0x00100606C0
#define A_BCM1480_DUART_INPORT_CHNG_C	    0x00100606D0
#define A_BCM1480_DUART_INPORT_CHNG_D	    0x00100606E0


/*  *********************************************************************
    * Generic Bus Registers (Section 15) and PCMCIA Registers (Section 16)
    ********************************************************************* */

#define A_BCM1480_IO_PCMCIA_CFG_B	0x0010061A58
#define A_BCM1480_IO_PCMCIA_STATUS_B	0x0010061A68

/*  *********************************************************************
    * GPIO Registers (Section 17)
    ********************************************************************* */

/* One additional GPIO register, placed _before_ the BCM1250's GPIO block base */

#define A_BCM1480_GPIO_INT_ADD_TYPE	    0x0010061A78
#define R_BCM1480_GPIO_INT_ADD_TYPE	    (-8)

#define A_GPIO_INT_ADD_TYPE	A_BCM1480_GPIO_INT_ADD_TYPE
#define R_GPIO_INT_ADD_TYPE	R_BCM1480_GPIO_INT_ADD_TYPE

/*  *********************************************************************
    * SMBus Registers (Section 18)
    ********************************************************************* */

/* No changes from BCM1250 */

/*  *********************************************************************
    * Timer Registers (Sections 4.6)
    ********************************************************************* */

/* BCM1480 has two additional watchdogs */

/* Watchdog timers */

#define A_BCM1480_SCD_WDOG_2		    0x0010022050
#define A_BCM1480_SCD_WDOG_3		    0x0010022150

#define BCM1480_SCD_NUM_WDOGS		    4

#define A_BCM1480_SCD_WDOG_BASE(w)	 (A_BCM1480_SCD_WDOG_0+((w)&2)*0x1000 + ((w)&1)*0x100)
#define A_BCM1480_SCD_WDOG_REGISTER(w, r) (A_BCM1480_SCD_WDOG_BASE(w) + (r))

#define A_BCM1480_SCD_WDOG_INIT_2	0x0010022050
#define A_BCM1480_SCD_WDOG_CNT_2	0x0010022058
#define A_BCM1480_SCD_WDOG_CFG_2	0x0010022060

#define A_BCM1480_SCD_WDOG_INIT_3	0x0010022150
#define A_BCM1480_SCD_WDOG_CNT_3	0x0010022158
#define A_BCM1480_SCD_WDOG_CFG_3	0x0010022160

/* BCM1480 has two additional compare registers */

#define A_BCM1480_SCD_ZBBUS_CYCLE_COUNT		A_SCD_ZBBUS_CYCLE_COUNT
#define A_BCM1480_SCD_ZBBUS_CYCLE_CP_BASE	0x0010020C00
#define A_BCM1480_SCD_ZBBUS_CYCLE_CP0		A_SCD_ZBBUS_CYCLE_CP0
#define A_BCM1480_SCD_ZBBUS_CYCLE_CP1		A_SCD_ZBBUS_CYCLE_CP1
#define A_BCM1480_SCD_ZBBUS_CYCLE_CP2		0x0010020C10
#define A_BCM1480_SCD_ZBBUS_CYCLE_CP3		0x0010020C18

/*  *********************************************************************
    * System Control Registers (Section 4.2)
    ********************************************************************* */

/* Scratch register in different place */

#define A_BCM1480_SCD_SCRATCH		0x100200A0

/*  *********************************************************************
    * System Address Trap Registers (Section 4.9)
    ********************************************************************* */

/* No changes from BCM1250 */

/*  *********************************************************************
    * System Interrupt Mapper Registers (Sections 4.3-4.5)
    ********************************************************************* */

#define A_BCM1480_IMR_CPU0_BASE		    0x0010020000
#define A_BCM1480_IMR_CPU1_BASE		    0x0010022000
#define A_BCM1480_IMR_CPU2_BASE		    0x0010024000
#define A_BCM1480_IMR_CPU3_BASE		    0x0010026000
#define BCM1480_IMR_REGISTER_SPACING	    0x2000
#define BCM1480_IMR_REGISTER_SPACING_SHIFT  13

#define A_BCM1480_IMR_MAPPER(cpu)	(A_BCM1480_IMR_CPU0_BASE+(cpu)*BCM1480_IMR_REGISTER_SPACING)
#define A_BCM1480_IMR_REGISTER(cpu, reg) (A_BCM1480_IMR_MAPPER(cpu)+(reg))

/* Most IMR registers are 128 bits, implemented as non-contiguous
   64-bit registers high (_H) and low (_L) */
#define BCM1480_IMR_HL_SPACING			0x1000

#define R_BCM1480_IMR_INTERRUPT_DIAG_H		0x0010
#define R_BCM1480_IMR_LDT_INTERRUPT_H		0x0018
#define R_BCM1480_IMR_LDT_INTERRUPT_CLR_H	0x0020
#define R_BCM1480_IMR_INTERRUPT_MASK_H		0x0028
#define R_BCM1480_IMR_INTERRUPT_TRACE_H		0x0038
#define R_BCM1480_IMR_INTERRUPT_SOURCE_STATUS_H 0x0040
#define R_BCM1480_IMR_LDT_INTERRUPT_SET		0x0048
#define R_BCM1480_IMR_MAILBOX_0_CPU		0x00C0
#define R_BCM1480_IMR_MAILBOX_0_SET_CPU		0x00C8
#define R_BCM1480_IMR_MAILBOX_0_CLR_CPU		0x00D0
#define R_BCM1480_IMR_MAILBOX_1_CPU		0x00E0
#define R_BCM1480_IMR_MAILBOX_1_SET_CPU		0x00E8
#define R_BCM1480_IMR_MAILBOX_1_CLR_CPU		0x00F0
#define R_BCM1480_IMR_INTERRUPT_STATUS_BASE_H	0x0100
#define BCM1480_IMR_INTERRUPT_STATUS_COUNT	8
#define R_BCM1480_IMR_INTERRUPT_MAP_BASE_H	0x0200
#define BCM1480_IMR_INTERRUPT_MAP_COUNT		64

#define R_BCM1480_IMR_INTERRUPT_DIAG_L		0x1010
#define R_BCM1480_IMR_LDT_INTERRUPT_L		0x1018
#define R_BCM1480_IMR_LDT_INTERRUPT_CLR_L	0x1020
#define R_BCM1480_IMR_INTERRUPT_MASK_L		0x1028
#define R_BCM1480_IMR_INTERRUPT_TRACE_L		0x1038
#define R_BCM1480_IMR_INTERRUPT_SOURCE_STATUS_L 0x1040
#define R_BCM1480_IMR_INTERRUPT_STATUS_BASE_L	0x1100
#define R_BCM1480_IMR_INTERRUPT_MAP_BASE_L	0x1200

#define A_BCM1480_IMR_ALIAS_MAILBOX_CPU0_BASE	0x0010028000
#define A_BCM1480_IMR_ALIAS_MAILBOX_CPU1_BASE	0x0010028100
#define A_BCM1480_IMR_ALIAS_MAILBOX_CPU2_BASE	0x0010028200
#define A_BCM1480_IMR_ALIAS_MAILBOX_CPU3_BASE	0x0010028300
#define BCM1480_IMR_ALIAS_MAILBOX_SPACING	0100

#define A_BCM1480_IMR_ALIAS_MAILBOX(cpu)     (A_BCM1480_IMR_ALIAS_MAILBOX_CPU0_BASE + \
					(cpu)*BCM1480_IMR_ALIAS_MAILBOX_SPACING)
#define A_BCM1480_IMR_ALIAS_MAILBOX_REGISTER(cpu, reg) (A_BCM1480_IMR_ALIAS_MAILBOX(cpu)+(reg))

#define R_BCM1480_IMR_ALIAS_MAILBOX_0		0x0000
#define R_BCM1480_IMR_ALIAS_MAILBOX_0_SET	0x0008

/*
 * these macros work together to build the address of a mailbox
 * register, e.g., A_BCM1480_MAILBOX_REGISTER(0,R_BCM1480_IMR_MAILBOX_SET,2)
 * for mbox_0_set_cpu2 returns 0x00100240C8
 */
#define R_BCM1480_IMR_MAILBOX_CPU	  0x00
#define R_BCM1480_IMR_MAILBOX_SET	  0x08
#define R_BCM1480_IMR_MAILBOX_CLR	  0x10
#define R_BCM1480_IMR_MAILBOX_NUM_SPACING 0x20
#define A_BCM1480_MAILBOX_REGISTER(num, reg, cpu) \
    (A_BCM1480_IMR_CPU0_BASE + \
     (num * R_BCM1480_IMR_MAILBOX_NUM_SPACING) + \
     (cpu * BCM1480_IMR_REGISTER_SPACING) + \
     (R_BCM1480_IMR_MAILBOX_0_CPU + reg))

/*  *********************************************************************
    * System Performance Counter Registers (Section 4.7)
    ********************************************************************* */

/* BCM1480 has four more performance counter registers, and two control
   registers. */

#define A_BCM1480_SCD_PERF_CNT_BASE	    0x00100204C0

#define A_BCM1480_SCD_PERF_CNT_CFG0	    0x00100204C0
#define A_BCM1480_SCD_PERF_CNT_CFG_0	    A_BCM1480_SCD_PERF_CNT_CFG0
#define A_BCM1480_SCD_PERF_CNT_CFG1	    0x00100204C8
#define A_BCM1480_SCD_PERF_CNT_CFG_1	    A_BCM1480_SCD_PERF_CNT_CFG1

#define A_BCM1480_SCD_PERF_CNT_0	    A_SCD_PERF_CNT_0
#define A_BCM1480_SCD_PERF_CNT_1	    A_SCD_PERF_CNT_1
#define A_BCM1480_SCD_PERF_CNT_2	    A_SCD_PERF_CNT_2
#define A_BCM1480_SCD_PERF_CNT_3	    A_SCD_PERF_CNT_3

#define A_BCM1480_SCD_PERF_CNT_4	    0x00100204F0
#define A_BCM1480_SCD_PERF_CNT_5	    0x00100204F8
#define A_BCM1480_SCD_PERF_CNT_6	    0x0010020500
#define A_BCM1480_SCD_PERF_CNT_7	    0x0010020508

#define BCM1480_SCD_NUM_PERF_CNT 8
#define BCM1480_SCD_PERF_CNT_SPACING 8
#define A_BCM1480_SCD_PERF_CNT(n) (A_SCD_PERF_CNT_0+(n*BCM1480_SCD_PERF_CNT_SPACING))

/*  *********************************************************************
    * System Bus Watcher Registers (Section 4.8)
    ********************************************************************* */


/* Same as 1250 except BUS_ERR_STATUS_DEBUG is in a different place. */

#define A_BCM1480_BUS_ERR_STATUS_DEBUG	    0x00100208D8

/*  *********************************************************************
    * System Debug Controller Registers (Section 19)
    ********************************************************************* */

/* Same as 1250 */

/*  *********************************************************************
    * System Trace Unit Registers (Sections 4.10)
    ********************************************************************* */

/* Same as 1250 */

/*  *********************************************************************
    * Data Mover DMA Registers (Section 10.7)
    ********************************************************************* */

/* Same as 1250 */


/*  *********************************************************************
    * HyperTransport Interface Registers (Section 8)
    ********************************************************************* */

#define BCM1480_HT_NUM_PORTS		   3
#define BCM1480_HT_PORT_SPACING		   0x800
#define A_BCM1480_HT_PORT_HEADER(x)	   (A_BCM1480_HT_PORT0_HEADER + ((x)*BCM1480_HT_PORT_SPACING))

#define A_BCM1480_HT_PORT0_HEADER	   0x00FE000000
#define A_BCM1480_HT_PORT1_HEADER	   0x00FE000800
#define A_BCM1480_HT_PORT2_HEADER	   0x00FE001000
#define A_BCM1480_HT_TYPE00_HEADER	   0x00FE002000


/*  *********************************************************************
    * Node Controller Registers (Section 9)
    ********************************************************************* */

#define A_BCM1480_NC_BASE		    0x00DFBD0000

#define A_BCM1480_NC_RLD_FIELD		    0x00DFBD0000
#define A_BCM1480_NC_RLD_TRIGGER	    0x00DFBD0020
#define A_BCM1480_NC_RLD_BAD_ERROR	    0x00DFBD0040
#define A_BCM1480_NC_RLD_COR_ERROR	    0x00DFBD0060
#define A_BCM1480_NC_RLD_ECC_STATUS	    0x00DFBD0080
#define A_BCM1480_NC_RLD_WAY_ENABLE	    0x00DFBD00A0
#define A_BCM1480_NC_RLD_RANDOM_LFSR	    0x00DFBD00C0

#define A_BCM1480_NC_INTERRUPT_STATUS	    0x00DFBD00E0
#define A_BCM1480_NC_INTERRUPT_ENABLE	    0x00DFBD0100
#define A_BCM1480_NC_TIMEOUT_COUNTER	    0x00DFBD0120
#define A_BCM1480_NC_TIMEOUT_COUNTER_SEL    0x00DFBD0140

#define A_BCM1480_NC_CREDIT_STATUS_REG0	    0x00DFBD0200
#define A_BCM1480_NC_CREDIT_STATUS_REG1	    0x00DFBD0220
#define A_BCM1480_NC_CREDIT_STATUS_REG2	    0x00DFBD0240
#define A_BCM1480_NC_CREDIT_STATUS_REG3	    0x00DFBD0260
#define A_BCM1480_NC_CREDIT_STATUS_REG4	    0x00DFBD0280
#define A_BCM1480_NC_CREDIT_STATUS_REG5	    0x00DFBD02A0
#define A_BCM1480_NC_CREDIT_STATUS_REG6	    0x00DFBD02C0
#define A_BCM1480_NC_CREDIT_STATUS_REG7	    0x00DFBD02E0
#define A_BCM1480_NC_CREDIT_STATUS_REG8	    0x00DFBD0300
#define A_BCM1480_NC_CREDIT_STATUS_REG9	    0x00DFBD0320
#define A_BCM1480_NC_CREDIT_STATUS_REG10    0x00DFBE0000
#define A_BCM1480_NC_CREDIT_STATUS_REG11    0x00DFBE0020
#define A_BCM1480_NC_CREDIT_STATUS_REG12    0x00DFBE0040

#define A_BCM1480_NC_SR_TIMEOUT_COUNTER	    0x00DFBE0060
#define A_BCM1480_NC_SR_TIMEOUT_COUNTER_SEL 0x00DFBE0080


/*  *********************************************************************
    * H&R Block Configuration Registers (Section 12.4)
    ********************************************************************* */

#define A_BCM1480_HR_BASE_0		    0x00DF820000
#define A_BCM1480_HR_BASE_1		    0x00DF8A0000
#define A_BCM1480_HR_BASE_2		    0x00DF920000
#define BCM1480_HR_REGISTER_SPACING	    0x80000

#define A_BCM1480_HR_BASE(idx)		    (A_BCM1480_HR_BASE_0 + ((idx)*BCM1480_HR_REGISTER_SPACING))
#define A_BCM1480_HR_REGISTER(idx, reg)	     (A_BCM1480_HR_BASE(idx) + (reg))

#define R_BCM1480_HR_CFG		    0x0000000000

#define R_BCM1480_HR_MAPPING		    0x0000010010

#define BCM1480_HR_RULE_SPACING		    0x0000000010
#define BCM1480_HR_NUM_RULES		    16
#define BCM1480_HR_OP_OFFSET		    0x0000000100
#define BCM1480_HR_TYPE_OFFSET		    0x0000000108
#define R_BCM1480_HR_RULE_OP(idx)	    (BCM1480_HR_OP_OFFSET + ((idx)*BCM1480_HR_RULE_SPACING))
#define R_BCM1480_HR_RULE_TYPE(idx)	    (BCM1480_HR_TYPE_OFFSET + ((idx)*BCM1480_HR_RULE_SPACING))

#define BCM1480_HR_LEAF_SPACING		    0x0000000010
#define BCM1480_HR_NUM_LEAVES		    10
#define BCM1480_HR_LEAF_OFFSET		    0x0000000300
#define R_BCM1480_HR_HA_LEAF0(idx)	    (BCM1480_HR_LEAF_OFFSET + ((idx)*BCM1480_HR_LEAF_SPACING))

#define R_BCM1480_HR_EX_LEAF0		    0x00000003A0

#define BCM1480_HR_PATH_SPACING		    0x0000000010
#define BCM1480_HR_NUM_PATHS		    16
#define BCM1480_HR_PATH_OFFSET		    0x0000000600
#define R_BCM1480_HR_PATH(idx)		    (BCM1480_HR_PATH_OFFSET + ((idx)*BCM1480_HR_PATH_SPACING))

#define R_BCM1480_HR_PATH_DEFAULT	    0x0000000700

#define BCM1480_HR_ROUTE_SPACING	    8
#define BCM1480_HR_NUM_ROUTES		    512
#define BCM1480_HR_ROUTE_OFFSET		    0x0000001000
#define R_BCM1480_HR_RT_WORD(idx)	    (BCM1480_HR_ROUTE_OFFSET + ((idx)*BCM1480_HR_ROUTE_SPACING))


/* checked to here - ehs */
/*  *********************************************************************
    * Packet Manager DMA Registers (Section 12.5)
    ********************************************************************* */

#define A_BCM1480_PM_BASE		    0x0010056000

#define A_BCM1480_PMI_LCL_0		    0x0010058000
#define A_BCM1480_PMO_LCL_0		    0x001005C000
#define A_BCM1480_PMI_OFFSET_0		    (A_BCM1480_PMI_LCL_0 - A_BCM1480_PM_BASE)
#define A_BCM1480_PMO_OFFSET_0		    (A_BCM1480_PMO_LCL_0 - A_BCM1480_PM_BASE)

#define BCM1480_PM_LCL_REGISTER_SPACING	    0x100
#define BCM1480_PM_NUM_CHANNELS		    32

#define A_BCM1480_PMI_LCL_BASE(idx)		(A_BCM1480_PMI_LCL_0 + ((idx)*BCM1480_PM_LCL_REGISTER_SPACING))
#define A_BCM1480_PMI_LCL_REGISTER(idx, reg)	 (A_BCM1480_PMI_LCL_BASE(idx) + (reg))
#define A_BCM1480_PMO_LCL_BASE(idx)		(A_BCM1480_PMO_LCL_0 + ((idx)*BCM1480_PM_LCL_REGISTER_SPACING))
#define A_BCM1480_PMO_LCL_REGISTER(idx, reg)	 (A_BCM1480_PMO_LCL_BASE(idx) + (reg))

#define BCM1480_PM_INT_PACKING		    8
#define BCM1480_PM_INT_FUNCTION_SPACING	    0x40
#define BCM1480_PM_INT_NUM_FUNCTIONS	    3

/*
 * DMA channel registers relative to A_BCM1480_PMI_LCL_BASE(n) and A_BCM1480_PMO_LCL_BASE(n)
 */

#define R_BCM1480_PM_BASE_SIZE		    0x0000000000
#define R_BCM1480_PM_CNT		    0x0000000008
#define R_BCM1480_PM_PFCNT		    0x0000000010
#define R_BCM1480_PM_LAST		    0x0000000018
#define R_BCM1480_PM_PFINDX		    0x0000000020
#define R_BCM1480_PM_INT_WMK		    0x0000000028
#define R_BCM1480_PM_CONFIG0		    0x0000000030
#define R_BCM1480_PM_LOCALDEBUG		    0x0000000078
#define R_BCM1480_PM_CACHEABILITY	    0x0000000080   /* PMI only */
#define R_BCM1480_PM_INT_CNFG		    0x0000000088
#define R_BCM1480_PM_DESC_MERGE_TIMER	    0x0000000090
#define R_BCM1480_PM_LOCALDEBUG_PIB	    0x00000000F8   /* PMI only */
#define R_BCM1480_PM_LOCALDEBUG_POB	    0x00000000F8   /* PMO only */

/*
 * Global Registers (Not Channelized)
 */

#define A_BCM1480_PMI_GLB_0		    0x0010056000
#define A_BCM1480_PMO_GLB_0		    0x0010057000

/*
 * PM to TX Mapping Register relative to A_BCM1480_PMI_GLB_0 and A_BCM1480_PMO_GLB_0
 */

#define R_BCM1480_PM_PMO_MAPPING	    0x00000008C8   /* PMO only */

#define A_BCM1480_PM_PMO_MAPPING	(A_BCM1480_PMO_GLB_0 + R_BCM1480_PM_PMO_MAPPING)

/*
 * Interrupt mapping registers
 */


#define A_BCM1480_PMI_INT_0		    0x0010056800
#define A_BCM1480_PMI_INT(q)		    (A_BCM1480_PMI_INT_0 + ((q>>8)<<8))
#define A_BCM1480_PMI_INT_OFFSET_0	    (A_BCM1480_PMI_INT_0 - A_BCM1480_PM_BASE)
#define A_BCM1480_PMO_INT_0		    0x0010057800
#define A_BCM1480_PMO_INT(q)		    (A_BCM1480_PMO_INT_0 + ((q>>8)<<8))
#define A_BCM1480_PMO_INT_OFFSET_0	    (A_BCM1480_PMO_INT_0 - A_BCM1480_PM_BASE)

/*
 * Interrupt registers relative to A_BCM1480_PMI_INT_0 and A_BCM1480_PMO_INT_0
 */

#define R_BCM1480_PM_INT_ST		    0x0000000000
#define R_BCM1480_PM_INT_MSK		    0x0000000040
#define R_BCM1480_PM_INT_CLR		    0x0000000080
#define R_BCM1480_PM_MRGD_INT		    0x00000000C0

/*
 * Debug registers (global)
 */

#define A_BCM1480_PM_GLOBALDEBUGMODE_PMI    0x0010056000
#define A_BCM1480_PM_GLOBALDEBUG_PID	    0x00100567F8
#define A_BCM1480_PM_GLOBALDEBUG_PIB	    0x0010056FF8
#define A_BCM1480_PM_GLOBALDEBUGMODE_PMO    0x0010057000
#define A_BCM1480_PM_GLOBALDEBUG_POD	    0x00100577F8
#define A_BCM1480_PM_GLOBALDEBUG_POB	    0x0010057FF8

/*  *********************************************************************
    *  Switch performance counters
    ********************************************************************* */

#define A_BCM1480_SWPERF_CFG	0xdfb91800
#define A_BCM1480_SWPERF_CNT0	0xdfb91880
#define A_BCM1480_SWPERF_CNT1	0xdfb91888
#define A_BCM1480_SWPERF_CNT2	0xdfb91890
#define A_BCM1480_SWPERF_CNT3	0xdfb91898


/*  *********************************************************************
    *  Switch Trace Unit
    ********************************************************************* */

#define A_BCM1480_SWTRC_MATCH_CONTROL_0		0xDFB91000
#define A_BCM1480_SWTRC_MATCH_DATA_VALUE_0	0xDFB91100
#define A_BCM1480_SWTRC_MATCH_DATA_MASK_0	0xDFB91108
#define A_BCM1480_SWTRC_MATCH_TAG_VALUE_0	0xDFB91200
#define A_BCM1480_SWTRC_MATCH_TAG_MAKS_0	0xDFB91208
#define A_BCM1480_SWTRC_EVENT_0			0xDFB91300
#define A_BCM1480_SWTRC_SEQUENCE_0		0xDFB91400

#define A_BCM1480_SWTRC_CFG			0xDFB91500
#define A_BCM1480_SWTRC_READ			0xDFB91508

#define A_BCM1480_SWDEBUG_SCHEDSTOP		0xDFB92000

#define A_BCM1480_SWTRC_MATCH_CONTROL(x) (A_BCM1480_SWTRC_MATCH_CONTROL_0 + ((x)*8))
#define A_BCM1480_SWTRC_EVENT(x) (A_BCM1480_SWTRC_EVENT_0 + ((x)*8))
#define A_BCM1480_SWTRC_SEQUENCE(x) (A_BCM1480_SWTRC_SEQUENCE_0 + ((x)*8))

#define A_BCM1480_SWTRC_MATCH_DATA_VALUE(x) (A_BCM1480_SWTRC_MATCH_DATA_VALUE_0 + ((x)*16))
#define A_BCM1480_SWTRC_MATCH_DATA_MASK(x) (A_BCM1480_SWTRC_MATCH_DATA_MASK_0 + ((x)*16))
#define A_BCM1480_SWTRC_MATCH_TAG_VALUE(x) (A_BCM1480_SWTRC_MATCH_TAG_VALUE_0 + ((x)*16))
#define A_BCM1480_SWTRC_MATCH_TAG_MASK(x) (A_BCM1480_SWTRC_MATCH_TAG_MASK_0 + ((x)*16))



/*  *********************************************************************
    *  High-Speed Port Registers (Section 13)
    ********************************************************************* */

#define A_BCM1480_HSP_BASE_0		    0x00DF810000
#define A_BCM1480_HSP_BASE_1		    0x00DF890000
#define A_BCM1480_HSP_BASE_2		    0x00DF910000
#define BCM1480_HSP_REGISTER_SPACING	    0x80000

#define A_BCM1480_HSP_BASE(idx)		    (A_BCM1480_HSP_BASE_0 + ((idx)*BCM1480_HSP_REGISTER_SPACING))
#define A_BCM1480_HSP_REGISTER(idx, reg)     (A_BCM1480_HSP_BASE(idx) + (reg))

#define R_BCM1480_HSP_RX_SPI4_CFG_0	      0x0000000000
#define R_BCM1480_HSP_RX_SPI4_CFG_1	      0x0000000008
#define R_BCM1480_HSP_RX_SPI4_DESKEW_OVERRIDE 0x0000000010
#define R_BCM1480_HSP_RX_SPI4_DESKEW_DATAPATH 0x0000000018
#define R_BCM1480_HSP_RX_SPI4_PORT_INT_EN     0x0000000020
#define R_BCM1480_HSP_RX_SPI4_PORT_INT_STATUS 0x0000000028

#define R_BCM1480_HSP_RX_SPI4_CALENDAR_0      0x0000000200
#define R_BCM1480_HSP_RX_SPI4_CALENDAR_1      0x0000000208

#define R_BCM1480_HSP_RX_PLL_CNFG	      0x0000000800
#define R_BCM1480_HSP_RX_CALIBRATION	      0x0000000808
#define R_BCM1480_HSP_RX_TEST		      0x0000000810
#define R_BCM1480_HSP_RX_DIAG_DETAILS	      0x0000000818
#define R_BCM1480_HSP_RX_DIAG_CRC_0	      0x0000000820
#define R_BCM1480_HSP_RX_DIAG_CRC_1	      0x0000000828
#define R_BCM1480_HSP_RX_DIAG_HTCMD	      0x0000000830
#define R_BCM1480_HSP_RX_DIAG_PKTCTL	      0x0000000838

#define R_BCM1480_HSP_RX_VIS_FLCTRL_COUNTER   0x0000000870

#define R_BCM1480_HSP_RX_PKT_RAMALLOC_0	      0x0000020020
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_1	      0x0000020028
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_2	      0x0000020030
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_3	      0x0000020038
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_4	      0x0000020040
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_5	      0x0000020048
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_6	      0x0000020050
#define R_BCM1480_HSP_RX_PKT_RAMALLOC_7	      0x0000020058
#define R_BCM1480_HSP_RX_PKT_RAMALLOC(idx)    (R_BCM1480_HSP_RX_PKT_RAMALLOC_0 + 8*(idx))

/* XXX Following registers were shuffled.  Renamed/renumbered per errata. */
#define R_BCM1480_HSP_RX_HT_RAMALLOC_0	    0x0000020078
#define R_BCM1480_HSP_RX_HT_RAMALLOC_1	    0x0000020080
#define R_BCM1480_HSP_RX_HT_RAMALLOC_2	    0x0000020088
#define R_BCM1480_HSP_RX_HT_RAMALLOC_3	    0x0000020090
#define R_BCM1480_HSP_RX_HT_RAMALLOC_4	    0x0000020098
#define R_BCM1480_HSP_RX_HT_RAMALLOC_5	    0x00000200A0

#define R_BCM1480_HSP_RX_SPI_WATERMARK_0      0x00000200B0
#define R_BCM1480_HSP_RX_SPI_WATERMARK_1      0x00000200B8
#define R_BCM1480_HSP_RX_SPI_WATERMARK_2      0x00000200C0
#define R_BCM1480_HSP_RX_SPI_WATERMARK_3      0x00000200C8
#define R_BCM1480_HSP_RX_SPI_WATERMARK_4      0x00000200D0
#define R_BCM1480_HSP_RX_SPI_WATERMARK_5      0x00000200D8
#define R_BCM1480_HSP_RX_SPI_WATERMARK_6      0x00000200E0
#define R_BCM1480_HSP_RX_SPI_WATERMARK_7      0x00000200E8
#define R_BCM1480_HSP_RX_SPI_WATERMARK(idx)   (R_BCM1480_HSP_RX_SPI_WATERMARK_0 + 8*(idx))

#define R_BCM1480_HSP_RX_VIS_CMDQ_0	      0x00000200F0
#define R_BCM1480_HSP_RX_VIS_CMDQ_1	      0x00000200F8
#define R_BCM1480_HSP_RX_VIS_CMDQ_2	      0x0000020100
#define R_BCM1480_HSP_RX_RAM_READCTL	      0x0000020108
#define R_BCM1480_HSP_RX_RAM_READWINDOW	      0x0000020110
#define R_BCM1480_HSP_RX_RF_READCTL	      0x0000020118
#define R_BCM1480_HSP_RX_RF_READWINDOW	      0x0000020120

#define R_BCM1480_HSP_TX_SPI4_CFG_0	      0x0000040000
#define R_BCM1480_HSP_TX_SPI4_CFG_1	      0x0000040008
#define R_BCM1480_HSP_TX_SPI4_TRAINING_FMT    0x0000040010

#define R_BCM1480_HSP_TX_PKT_RAMALLOC_0	      0x0000040020
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_1	      0x0000040028
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_2	      0x0000040030
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_3	      0x0000040038
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_4	      0x0000040040
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_5	      0x0000040048
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_6	      0x0000040050
#define R_BCM1480_HSP_TX_PKT_RAMALLOC_7	      0x0000040058
#define R_BCM1480_HSP_TX_PKT_RAMALLOC(idx)    (R_BCM1480_HSP_TX_PKT_RAMALLOC_0 + 8*(idx))
#define R_BCM1480_HSP_TX_NPC_RAMALLOC	      0x0000040078
#define R_BCM1480_HSP_TX_RSP_RAMALLOC	      0x0000040080
#define R_BCM1480_HSP_TX_PC_RAMALLOC	      0x0000040088
#define R_BCM1480_HSP_TX_HTCC_RAMALLOC_0      0x0000040090
#define R_BCM1480_HSP_TX_HTCC_RAMALLOC_1      0x0000040098
#define R_BCM1480_HSP_TX_HTCC_RAMALLOC_2      0x00000400A0

#define R_BCM1480_HSP_TX_PKT_RXPHITCNT_0      0x00000400B0
#define R_BCM1480_HSP_TX_PKT_RXPHITCNT_1      0x00000400B8
#define R_BCM1480_HSP_TX_PKT_RXPHITCNT_2      0x00000400C0
#define R_BCM1480_HSP_TX_PKT_RXPHITCNT_3      0x00000400C8
#define R_BCM1480_HSP_TX_PKT_RXPHITCNT(idx)   (R_BCM1480_HSP_TX_PKT_RXPHITCNT_0 + 8*(idx))
#define R_BCM1480_HSP_TX_HTIO_RXPHITCNT	      0x00000400D0
#define R_BCM1480_HSP_TX_HTCC_RXPHITCNT	      0x00000400D8

#define R_BCM1480_HSP_TX_PKT_TXPHITCNT_0      0x00000400E0
#define R_BCM1480_HSP_TX_PKT_TXPHITCNT_1      0x00000400E8
#define R_BCM1480_HSP_TX_PKT_TXPHITCNT_2      0x00000400F0
#define R_BCM1480_HSP_TX_PKT_TXPHITCNT_3      0x00000400F8
#define R_BCM1480_HSP_TX_PKT_TXPHITCNT(idx)   (R_BCM1480_HSP_TX_PKT_TXPHITCNT_0 + 8*(idx))
#define R_BCM1480_HSP_TX_HTIO_TXPHITCNT	      0x0000040100
#define R_BCM1480_HSP_TX_HTCC_TXPHITCNT	      0x0000040108

#define R_BCM1480_HSP_TX_SPI4_CALENDAR_0      0x0000040200
#define R_BCM1480_HSP_TX_SPI4_CALENDAR_1      0x0000040208

#define R_BCM1480_HSP_TX_PLL_CNFG	      0x0000040800
#define R_BCM1480_HSP_TX_CALIBRATION	      0x0000040808
#define R_BCM1480_HSP_TX_TEST		      0x0000040810

#define R_BCM1480_HSP_TX_VIS_CMDQ_0	      0x0000040840
#define R_BCM1480_HSP_TX_VIS_CMDQ_1	      0x0000040848
#define R_BCM1480_HSP_TX_VIS_CMDQ_2	      0x0000040850
#define R_BCM1480_HSP_TX_RAM_READCTL	      0x0000040860
#define R_BCM1480_HSP_TX_RAM_READWINDOW	      0x0000040868
#define R_BCM1480_HSP_TX_RF_READCTL	      0x0000040870
#define R_BCM1480_HSP_TX_RF_READWINDOW	      0x0000040878

#define R_BCM1480_HSP_TX_SPI4_PORT_INT_STATUS 0x0000040880
#define R_BCM1480_HSP_TX_SPI4_PORT_INT_EN     0x0000040888

#define R_BCM1480_HSP_TX_NEXT_ADDR_BASE 0x000040400
#define R_BCM1480_HSP_TX_NEXT_ADDR_REGISTER(x)	(R_BCM1480_HSP_TX_NEXT_ADDR_BASE+ 8*(x))



/*  *********************************************************************
    *  Physical Address Map (Table 10 and Figure 7)
    ********************************************************************* */

#define A_BCM1480_PHYS_MEMORY_0			_SB_MAKE64(0x0000000000)
#define A_BCM1480_PHYS_MEMORY_SIZE		_SB_MAKE64((256*1024*1024))
#define A_BCM1480_PHYS_SYSTEM_CTL		_SB_MAKE64(0x0010000000)
#define A_BCM1480_PHYS_IO_SYSTEM		_SB_MAKE64(0x0010060000)
#define A_BCM1480_PHYS_GENBUS			_SB_MAKE64(0x0010090000)
#define A_BCM1480_PHYS_GENBUS_END		_SB_MAKE64(0x0028000000)
#define A_BCM1480_PHYS_PCI_MISC_MATCH_BYTES	_SB_MAKE64(0x0028000000)
#define A_BCM1480_PHYS_PCI_IACK_MATCH_BYTES	_SB_MAKE64(0x0029000000)
#define A_BCM1480_PHYS_PCI_IO_MATCH_BYTES	_SB_MAKE64(0x002C000000)
#define A_BCM1480_PHYS_PCI_CFG_MATCH_BYTES	_SB_MAKE64(0x002E000000)
#define A_BCM1480_PHYS_PCI_OMAP_MATCH_BYTES	_SB_MAKE64(0x002F000000)
#define A_BCM1480_PHYS_PCI_MEM_MATCH_BYTES	_SB_MAKE64(0x0030000000)
#define A_BCM1480_PHYS_HT_MEM_MATCH_BYTES	_SB_MAKE64(0x0040000000)
#define A_BCM1480_PHYS_HT_MEM_MATCH_BITS	_SB_MAKE64(0x0060000000)
#define A_BCM1480_PHYS_MEMORY_1			_SB_MAKE64(0x0080000000)
#define A_BCM1480_PHYS_MEMORY_2			_SB_MAKE64(0x0090000000)
#define A_BCM1480_PHYS_PCI_MISC_MATCH_BITS	_SB_MAKE64(0x00A8000000)
#define A_BCM1480_PHYS_PCI_IACK_MATCH_BITS	_SB_MAKE64(0x00A9000000)
#define A_BCM1480_PHYS_PCI_IO_MATCH_BITS	_SB_MAKE64(0x00AC000000)
#define A_BCM1480_PHYS_PCI_CFG_MATCH_BITS	_SB_MAKE64(0x00AE000000)
#define A_BCM1480_PHYS_PCI_OMAP_MATCH_BITS	_SB_MAKE64(0x00AF000000)
#define A_BCM1480_PHYS_PCI_MEM_MATCH_BITS	_SB_MAKE64(0x00B0000000)
#define A_BCM1480_PHYS_MEMORY_3			_SB_MAKE64(0x00C0000000)
#define A_BCM1480_PHYS_L2_CACHE_TEST		_SB_MAKE64(0x00D0000000)
#define A_BCM1480_PHYS_HT_SPECIAL_MATCH_BYTES	_SB_MAKE64(0x00D8000000)
#define A_BCM1480_PHYS_HT_IO_MATCH_BYTES	_SB_MAKE64(0x00DC000000)
#define A_BCM1480_PHYS_HT_CFG_MATCH_BYTES	_SB_MAKE64(0x00DE000000)
#define A_BCM1480_PHYS_HS_SUBSYS		_SB_MAKE64(0x00DF000000)
#define A_BCM1480_PHYS_HT_SPECIAL_MATCH_BITS	_SB_MAKE64(0x00F8000000)
#define A_BCM1480_PHYS_HT_IO_MATCH_BITS		_SB_MAKE64(0x00FC000000)
#define A_BCM1480_PHYS_HT_CFG_MATCH_BITS	_SB_MAKE64(0x00FE000000)
#define A_BCM1480_PHYS_MEMORY_EXP		_SB_MAKE64(0x0100000000)
#define A_BCM1480_PHYS_MEMORY_EXP_SIZE		_SB_MAKE64((508*1024*1024*1024))
#define A_BCM1480_PHYS_PCI_UPPER		_SB_MAKE64(0x1000000000)
#define A_BCM1480_PHYS_HT_UPPER_MATCH_BYTES	_SB_MAKE64(0x2000000000)
#define A_BCM1480_PHYS_HT_UPPER_MATCH_BITS	_SB_MAKE64(0x3000000000)
#define A_BCM1480_PHYS_HT_NODE_ALIAS		_SB_MAKE64(0x4000000000)
#define A_BCM1480_PHYS_HT_FULLACCESS		_SB_MAKE64(0xF000000000)


/*  *********************************************************************
    *  L2 Cache as RAM (Table 54)
    ********************************************************************* */

#define A_BCM1480_PHYS_L2CACHE_WAY_SIZE		_SB_MAKE64(0x0000020000)
#define BCM1480_PHYS_L2CACHE_NUM_WAYS		8
#define A_BCM1480_PHYS_L2CACHE_TOTAL_SIZE	_SB_MAKE64(0x0000100000)
#define A_BCM1480_PHYS_L2CACHE_WAY0		_SB_MAKE64(0x00D0300000)
#define A_BCM1480_PHYS_L2CACHE_WAY1		_SB_MAKE64(0x00D0320000)
#define A_BCM1480_PHYS_L2CACHE_WAY2		_SB_MAKE64(0x00D0340000)
#define A_BCM1480_PHYS_L2CACHE_WAY3		_SB_MAKE64(0x00D0360000)
#define A_BCM1480_PHYS_L2CACHE_WAY4		_SB_MAKE64(0x00D0380000)
#define A_BCM1480_PHYS_L2CACHE_WAY5		_SB_MAKE64(0x00D03A0000)
#define A_BCM1480_PHYS_L2CACHE_WAY6		_SB_MAKE64(0x00D03C0000)
#define A_BCM1480_PHYS_L2CACHE_WAY7		_SB_MAKE64(0x00D03E0000)

#endif /* _BCM1480_REGS_H */
