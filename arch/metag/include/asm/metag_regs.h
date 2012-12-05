/*
 * asm/metag_regs.h
 *
 * Copyright (C) 2000-2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Various defines for Meta core (non memory-mapped) registers.
 */

#ifndef _ASM_METAG_REGS_H_
#define _ASM_METAG_REGS_H_

/*
 * CHIP Unit Identifiers and Valid/Global register number masks
 * ------------------------------------------------------------
 */
#define TXUCT_ID    0x0     /* Control unit regs */
#ifdef METAC_1_2
#define     TXUCT_MASK  0xFF0FFFFF  /* Valid regs 0..31  */
#else
#define     TXUCT_MASK  0xFF1FFFFF  /* Valid regs 0..31  */
#endif
#define     TGUCT_MASK  0x00000000  /* No global regs    */
#define TXUD0_ID    0x1     /* Data unit regs */
#define TXUD1_ID    0x2
#define     TXUDX_MASK  0xFFFFFFFF  /* Valid regs 0..31 */
#define     TGUDX_MASK  0xFFFF0000  /* Global regs for base inst */
#define     TXUDXDSP_MASK   0x0F0FFFFF  /* Valid DSP regs */
#define     TGUDXDSP_MASK   0x0E0E0000  /* Global DSP ACC regs */
#define TXUA0_ID    0x3     /* Address unit regs */
#define TXUA1_ID    0x4
#define     TXUAX_MASK  0x0000FFFF  /* Valid regs   0-15 */
#define     TGUAX_MASK  0x0000FF00  /* Global regs  8-15 */
#define TXUPC_ID    0x5     /* PC registers */
#define     TXUPC_MASK  0x00000003  /* Valid regs   0- 1 */
#define     TGUPC_MASK  0x00000000  /* No global regs    */
#define TXUPORT_ID  0x6     /* Ports are not registers */
#define TXUTR_ID    0x7
#define     TXUTR_MASK  0x0000005F  /* Valid regs   0-3,4,6 */
#define     TGUTR_MASK  0x00000000  /* No global regs    */
#ifdef METAC_2_1
#define TXUTT_ID    0x8
#define     TXUTT_MASK  0x0000000F  /* Valid regs   0-3 */
#define     TGUTT_MASK  0x00000010  /* Global reg   4   */
#define TXUFP_ID    0x9     /* FPU regs */
#define     TXUFP_MASK  0x0000FFFF  /* Valid regs   0-15 */
#define     TGUFP_MASK  0x00000000  /* No global regs    */
#endif /* METAC_2_1 */

#ifdef METAC_1_2
#define TXUXX_MASKS { TXUCT_MASK, TXUDX_MASK, TXUDX_MASK, TXUAX_MASK, \
		      TXUAX_MASK, TXUPC_MASK,          0, TXUTR_MASK, \
		      0, 0, 0, 0, 0, 0, 0, 0                          }
#define TGUXX_MASKS { TGUCT_MASK, TGUDX_MASK, TGUDX_MASK, TGUAX_MASK, \
		      TGUAX_MASK, TGUPC_MASK,          0, TGUTR_MASK, \
		      0, 0, 0, 0, 0, 0, 0, 0                          }
#else /* METAC_1_2 */
#define TXUXX_MASKS { TXUCT_MASK, TXUDX_MASK, TXUDX_MASK, TXUAX_MASK, \
		      TXUAX_MASK, TXUPC_MASK,          0, TXUTR_MASK, \
		      TXUTT_MASK, TXUFP_MASK,          0,          0, \
			       0,          0,          0,          0  }
#define TGUXX_MASKS { TGUCT_MASK, TGUDX_MASK, TGUDX_MASK, TGUAX_MASK, \
		      TGUAX_MASK, TGUPC_MASK,          0, TGUTR_MASK, \
		      TGUTT_MASK, TGUFP_MASK,          0,          0, \
			       0,          0,          0,          0  }
#endif /* !METAC_1_2 */

#define TXUXXDSP_MASKS { 0, TXUDXDSP_MASK, TXUDXDSP_MASK, 0, 0, 0, 0, 0, \
			 0, 0, 0, 0, 0, 0, 0, 0                          }
#define TGUXXDSP_MASKS { 0, TGUDXDSP_MASK, TGUDXDSP_MASK, 0, 0, 0, 0, 0, \
			 0, 0, 0, 0, 0, 0, 0, 0                          }

/* -------------------------------------------------------------------------
;                          DATA AND ADDRESS UNIT REGISTERS
;  -----------------------------------------------------------------------*/
/*
  Thread local D0 registers
 */
/*   D0.0    ; Holds 32-bit result, can be used as scratch */
#define D0Re0 D0.0
/*   D0.1    ; Used to pass Arg6_32 */
#define D0Ar6 D0.1
/*   D0.2    ; Used to pass Arg4_32 */
#define D0Ar4 D0.2
/*   D0.3    ; Used to pass Arg2_32 to a called routine (see D1.3 below) */
#define D0Ar2 D0.3
/*   D0.4    ; Can be used as scratch; used to save A0FrP in entry sequences */
#define D0FrT D0.4
/*   D0.5    ; C compiler assumes preservation, save with D1.5 if used */
/*   D0.6    ; C compiler assumes preservation, save with D1.6 if used */
/*   D0.7    ; C compiler assumes preservation, save with D1.7 if used */
/*   D0.8    ; Use of D0.8 and above is not encouraged */
/*   D0.9  */
/*   D0.10 */
/*   D0.11 */
/*   D0.12 */
/*   D0.13 */
/*   D0.14 */
/*   D0.15 */
/*
   Thread local D1 registers
 */
/*   D1.0    ; Holds top 32-bits of 64-bit result, can be used as scratch */
#define D1Re0 D1.0
/*   D1.1    ; Used to pass Arg5_32 */
#define D1Ar5 D1.1
/*   D1.2    ; Used to pass Arg3_32 */
#define D1Ar3 D1.2
/*   D1.3    ; Used to pass Arg1_32 (first 32-bit argument) to a called routine */
#define D1Ar1 D1.3
/*   D1.4    ; Used for Return Pointer, save during entry with A0FrP (via D0.4) */
#define D1RtP D1.4
/*   D1.5    ; C compiler assumes preservation, save if used */
/*   D1.6    ; C compiler assumes preservation, save if used */
/*   D1.7    ; C compiler assumes preservation, save if used */
/*   D1.8    ; Use of D1.8 and above is not encouraged */
/*   D1.9  */
/*   D1.10 */
/*   D1.11 */
/*   D1.12 */
/*   D1.13 */
/*   D1.14 */
/*   D1.15 */
/*
   Thread local A0 registers
 */
/*   A0.0    ; Primary stack pointer */
#define A0StP A0.0
/*   A0.1    ; Used as local frame pointer in C, save if used (via D0.4) */
#define A0FrP A0.1
/*   A0.2  */
/*   A0.3  */
/*   A0.4    ; Use of A0.4 and above is not encouraged */
/*   A0.5  */
/*   A0.6  */
/*   A0.7  */
/*
   Thread local A1 registers
 */
/*   A1.0    ; Global static chain pointer - do not modify */
#define A1GbP A1.0
/*   A1.1    ; Local static chain pointer in C, can be used as scratch */
#define A1LbP A1.1
/*   A1.2  */
/*   A1.3  */
/*   A1.4    ; Use of A1.4 and above is not encouraged */
/*   A1.5  */
/*   A1.6  */
/*   A1.7  */
#ifdef METAC_2_1
/* Renameable registers for use with Fast Interrupts */
/* The interrupt stack pointer (usually a global register) */
#define A0IStP A0IReg
/* The interrupt global pointer (usually a global register) */
#define A1IGbP A1IReg
#endif
/*
   Further registers may be globally allocated via linkage/loading tools,
   normally they are not used.
 */
/*-------------------------------------------------------------------------
;                    STACK STRUCTURE and CALLING CONVENTION
; -----------------------------------------------------------------------*/
/*
; Calling convention indicates that the following is the state of the
; stack frame at the start of a routine-
;
;       Arg9_32 [A0StP+#-12]
;       Arg8_32 [A0StP+#- 8]
;       Arg7_32 [A0StP+#- 4]
;   A0StP->
;
; Registers D1.3, D0.3, ..., to D0.1 are used to pass Arg1_32 to Arg6_32
;   respectively. If a routine needs to store them on the stack in order
;   to make sub-calls or because of the general complexity of the routine it
;   is best to dump these registers immediately at the start of a routine
;   using a MSETL or SETL instruction-
;
;   MSETL   [A0StP],D0Ar6,D0Ar4,D0Ar2; Only dump argments expected
;or SETL    [A0StP+#8++],D0Ar2       ; Up to two 32-bit args expected
;
; For non-leaf routines it is always necessary to save and restore at least
; the return address value D1RtP on the stack. Also by convention if the
; frame is saved then a new A0FrP value must be set-up. So for non-leaf
; routines at this point both these registers must be saved onto the stack
; using a SETL instruction and the new A0FrP value is then set-up-
;
;   MOV     D0FrT,A0FrP
;   ADD     A0FrP,A0StP,#0
;   SETL    [A0StP+#8++],D0FrT,D1RtP
;
; Registers D0.5, D1.5, to D1.7 are assumed to be preserved across calls so
;   a SETL or MSETL instruction can be used to save the current state
;   of these registers if they are modified by the current routine-
;
;   MSETL   [A0StP],D0.5,D0.6,D0.7   ; Only save registers modified
;or SETL    [A0StP+#8++],D0.5        ; Only D0.5 and/or D1.5 modified
;
; All of the above sequences can be combined into one maximal case-
;
;   MOV     D0FrT,A0FrP              ; Save and calculate new frame pointer
;   ADD     A0FrP,A0StP,#(ARS)
;   MSETL   [A0StP],D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7
;
; Having completed the above sequence the only remaining task on routine
; entry is to reserve any local and outgoing argment storage space on the
; stack. This instruction may be omitted if the size of this region is zero-
;
;   ADD     A0StP,A0StP,#(LCS)
;
; LCS is the first example use of one of a number of standard local defined
; values that can be created to make assembler code more readable and
; potentially more robust-
;
; #define ARS   0x18                 ; Register arg bytes saved on stack
; #define FRS   0x20                 ; Frame save area size in bytes
; #define LCS   0x00                 ; Locals and Outgoing arg size
; #define ARO   (LCS+FRS)            ; Stack offset to access args
;
; All of the above defines should be undefined (#undef) at the end of each
; routine to avoid accidental use in the next routine.
;
; Given all of the above the following stack structure is expected during
; the body of a routine if all args passed in registers are saved during
; entry-
;
;                                    ; 'Incoming args area'
;         Arg10_32 [A0StP+#-((10*4)+ARO)]       Arg9_32  [A0StP+#-(( 9*4)+ARO)]
;         Arg8_32  [A0StP+#-(( 8*4)+ARO)]       Arg7_32  [A0StP+#-(( 7*4)+ARO)]
;--- Call point
; D0Ar6=  Arg6_32  [A0StP+#-(( 6*4)+ARO)] D1Ar5=Arg5_32  [A0StP+#-(( 5*4)+ARO)]
; D0Ar4=  Arg4_32  [A0StP+#-(( 4*4)+ARO)] D1Ar3=Arg3_32  [A0StP+#-(( 3*4)+ARO)]
; D0Ar2=  Arg2_32  [A0StP+#-(( 2*4)+ARO)] D1Ar2=Arg1_32  [A0StP+#-(( 1*4)+ARO)]
;                                    ; 'Frame area'
; A0FrP-> D0FrT, D1RtP,
;         D0.5, D1.5,
;         D0.6, D1.6,
;         D0.7, D1.7,
;                                    ; 'Locals area'
;         Loc0_32  [A0StP+# (( 0*4)-LCS)],      Loc1_32 [A0StP+# (( 1*4)-LCS)]
;               .... other locals
;         Locn_32  [A0StP+# (( n*4)-LCS)]
;                                    ; 'Outgoing args area'
;         Outm_32  [A0StP+#- ( m*4)]            .... other outgoing args
;         Out8_32  [A0StP+#- ( 1*4)]            Out7_32  [A0StP+#- ( 1*4)]
; A0StP-> (Out1_32-Out6_32 in regs D1Ar1-D0Ar6)
;
; The exit sequence for a non-leaf routine can use the frame pointer created
; in the entry sequence to optimise the recovery of the full state-
;
;   MGETL   D0FrT,D0.5,D0.6,D0.7,[A0FrP]
;   SUB     A0StP,A0FrP,#(ARS+FRS)
;   MOV     A0FrP,D0FrT
;   MOV     PC,D1RtP
;
; Having described the most complex non-leaf case above, it is worth noting
; that if a routine is a leaf and does not use any of the caller-preserved
; state. The routine can be implemented as-
;
;   ADD     A0StP,A0StP,#LCS
;   .... body of routine
;   SUB     A0StP,A0StP,#LCS
;   MOV     PC,D1RtP
;
; The stack adjustments can also be omitted if no local storage is required.
;
; Another exit sequence structure is more applicable if for a leaf routine
; with no local frame pointer saved/generated in which the call saved
; registers need to be saved and restored-
;
;   MSETL   [A0StP],D0.5,D0.6,D0.7   ; Hence FRS is 0x18, ARS is 0x00
;   ADD     A0StP,A0StP,#LCS
;   .... body of routine
;   GETL    D0.5,D1.5,[A0StP+#((0*8)-(FRS+LCS))]
;   GETL    D0.6,D1.6,[A0StP+#((1*8)-(FRS+LCS))]
;   GETL    D0.7,D1.7,[A0StP+#((2*8)-(FRS+LCS))]
;   SUB     A0StP,A0StP,#(ARS+FRS+LCS)
;   MOV     PC,D1RtP
;
; Lastly, to support profiling assembler code should use a fixed entry/exit
; sequence if the trigger define _GMON_ASM is defined-
;
;   #ifndef _GMON_ASM
;   ... optimised entry code
;   #else
;   ; Profiling entry case
;   MOV     D0FrT,A0FrP              ; Save and calculate new frame pointer
;   ADD     A0FrP,A0StP,#(ARS)
;   MSETL   [A0StP],...,D0FrT,... or SETL    [A0FrP],D0FrT,D1RtP
;   CALLR   D0FrT,_mcount_wrapper
;   #endif
;   ... body of routine
;   #ifndef _GMON_ASM
;   ... optimised exit code
;   #else
;   ; Profiling exit case
;   MGETL   D0FrT,...,[A0FrP]     or GETL    D0FrT,D1RtP,[A0FrP++]
;   SUB     A0StP,A0FrP,#(ARS+FRS)
;   MOV     A0FrP,D0FrT
;   MOV     PC,D1RtP
;   #endif


; -------------------------------------------------------------------------
;                         CONTROL UNIT REGISTERS
; -------------------------------------------------------------------------
;
; See the assembler guide, hardware documentation, or the field values
; defined below for some details of the use of these registers.
*/
#define TXENABLE    CT.0    /* Need to define bit-field values in these */
#define TXMODE      CT.1
#define TXSTATUS    CT.2    /* DEFAULT 0x00020000 */
#define TXRPT       CT.3
#define TXTIMER     CT.4
#define TXL1START   CT.5
#define TXL1END     CT.6
#define TXL1COUNT   CT.7
#define TXL2START   CT.8
#define TXL2END     CT.9
#define TXL2COUNT   CT.10
#define TXBPOBITS   CT.11
#define TXMRSIZE    CT.12
#define TXTIMERI    CT.13
#define TXDRCTRL    CT.14  /* DEFAULT 0x0XXXF0F0 */
#define TXDRSIZE    CT.15
#define TXCATCH0    CT.16
#define TXCATCH1    CT.17
#define TXCATCH2    CT.18
#define TXCATCH3    CT.19

#ifdef METAC_2_1
#define TXDEFR      CT.20
#define TXCPRS      CT.21
#endif

#define TXINTERN0   CT.23
#define TXAMAREG0   CT.24
#define TXAMAREG1   CT.25
#define TXAMAREG2   CT.26
#define TXAMAREG3   CT.27
#define TXDIVTIME   CT.28   /* DEFAULT 0x00000001 */
#define TXPRIVEXT   CT.29   /* DEFAULT 0x003B0000 */
#define TXTACTCYC   CT.30
#define TXIDLECYC   CT.31

/*****************************************************************************
 *                        CONTROL UNIT REGISTER BITS
 ****************************************************************************/
/*
 * The following registers and where appropriate the sub-fields of those
 * registers are defined for pervasive use in controlling program flow.
 */

/*
 * TXENABLE register fields - only the thread id is routinely useful
 */
#define TXENABLE_REGNUM 0
#define TXENABLE_THREAD_BITS       0x00000700
#define TXENABLE_THREAD_S          8
#define TXENABLE_REV_STEP_BITS     0x000000F0
#define TXENABLE_REV_STEP_S        4

/*
 * TXMODE register - controls extensions of the instruction set
 */
#define TXMODE_REGNUM 1
#define     TXMODE_DEFAULT  0   /* All fields default to zero */

/*
 * TXSTATUS register - contains a couple of stable bits that can be used
 *      to determine the privilege processing level and interrupt
 *      processing level of the current thread.
 */
#define TXSTATUS_REGNUM 2
#define TXSTATUS_PSTAT_BIT         0x00020000   /* -> Privilege active      */
#define TXSTATUS_PSTAT_S           17
#define TXSTATUS_ISTAT_BIT         0x00010000   /* -> In interrupt state    */
#define TXSTATUS_ISTAT_S           16

/*
 * These are all relatively boring registers, mostly full 32-bit
 */
#define TXRPT_REGNUM     3  /* Repeat counter for XFR... instructions   */
#define TXTIMER_REGNUM   4  /* Timer-- causes timer trigger on overflow */
#define TXL1START_REGNUM 5  /* Hardware Loop 1 Start-PC/End-PC/Count    */
#define TXL1END_REGNUM   6
#define TXL1COUNT_REGNUM 7
#define TXL2START_REGNUM 8  /* Hardware Loop 2 Start-PC/End-PC/Count    */
#define TXL2END_REGNUM   9
#define TXL2COUNT_REGNUM 10
#define TXBPOBITS_REGNUM 11 /* Branch predict override bits - tune perf */
#define TXTIMERI_REGNUM  13 /* Timer-- time based interrupt trigger     */

/*
 * TXDIVTIME register is routinely read to calculate the time-base for
 * the TXTIMER register.
 */
#define TXDIVTIME_REGNUM 28
#define     TXDIVTIME_DIV_BITS 0x000000FF
#define     TXDIVTIME_DIV_S    0
#define     TXDIVTIME_DIV_MIN  0x00000001   /* Maximum resolution       */
#define     TXDIVTIME_DIV_MAX  0x00000100   /* 1/1 -> 1/256 resolution  */
#define     TXDIVTIME_BASE_HZ  1000000      /* Timers run at 1Mhz @1/1  */

/*
 * TXPRIVEXT register can be consulted to decide if write access to a
 *    part of the threads register set is not permitted when in
 *    unprivileged mode (PSTAT == 0).
 */
#define TXPRIVEXT_REGNUM 29
#define     TXPRIVEXT_COPRO_BITS    0xFF000000 /* Co-processor 0-7 */
#define     TXPRIVEXT_COPRO_S       24
#define     TXPRIVEXT_TXTRIGGER_BIT 0x00020000 /* TXSTAT|TXMASK|TXPOLL */
#define     TXPRIVEXT_TXGBLCREG_BIT 0x00010000 /* Global common regs */
#define     TXPRIVEXT_CBPRIV_BIT    0x00008000 /* Mem i/f dump priv */
#define     TXPRIVEXT_ILOCK_BIT     0x00004000 /* LOCK inst priv */
#define     TXPRIVEXT_TXITACCYC_BIT 0x00002000 /* TXIDLECYC|TXTACTCYC */
#define     TXPRIVEXT_TXDIVTIME_BIT 0x00001000 /* TXDIVTIME priv */
#define     TXPRIVEXT_TXAMAREGX_BIT 0x00000800 /* TXAMAREGX priv */
#define     TXPRIVEXT_TXTIMERI_BIT  0x00000400 /* TXTIMERI  priv */
#define     TXPRIVEXT_TXSTATUS_BIT  0x00000200 /* TXSTATUS  priv */
#define     TXPRIVEXT_TXDISABLE_BIT 0x00000100 /* TXENABLE  priv */
#ifndef METAC_1_2
#define     TXPRIVEXT_MINIMON_BIT   0x00000080 /* Enable Minim features */
#define     TXPRIVEXT_OLDBCCON_BIT  0x00000020 /* Restore Static predictions */
#define     TXPRIVEXT_ALIGNREW_BIT  0x00000010 /* Align & precise checks */
#endif
#define     TXPRIVEXT_KEEPPRI_BIT   0x00000008 /* Use AMA_Priority if ISTAT=1*/
#define     TXPRIVEXT_TXTOGGLEI_BIT 0x00000001 /* TX.....I  priv */

/*
 * TXTACTCYC register - counts instructions issued for this thread
 */
#define TXTACTCYC_REGNUM  30
#define     TXTACTCYC_COUNT_MASK    0x00FFFFFF

/*
 * TXIDLECYC register - counts idle cycles
 */
#define TXIDLECYC_REGNUM  31
#define     TXIDLECYC_COUNT_MASK    0x00FFFFFF

/*****************************************************************************
 *                             DSP EXTENSIONS
 ****************************************************************************/
/*
 * The following values relate to fields and controls that only a program
 * using the DSP extensions of the META instruction set need to know.
 */


#ifndef METAC_1_2
/*
 * Allow co-processor hardware to replace the read pipeline data source in
 * appropriate cases.
 */
#define TXMODE_RDCPEN_BIT       0x00800000
#endif

/*
 * Address unit addressing modes
 */
#define TXMODE_A1ADDR_BITS  0x00007000
#define TXMODE_A1ADDR_S     12
#define TXMODE_A0ADDR_BITS  0x00000700
#define TXMODE_A0ADDR_S     8
#define     TXMODE_AXADDR_MODULO 3
#define     TXMODE_AXADDR_REVB   4
#define     TXMODE_AXADDR_REVW   5
#define     TXMODE_AXADDR_REVD   6
#define     TXMODE_AXADDR_REVL   7

/*
 * Data unit OverScale select (default 0 -> normal, 1 -> top 16 bits)
 */
#define TXMODE_DXOVERSCALE_BIT  0x00000080

/*
 * Data unit MX mode select (default 0 -> MX16, 1 -> MX8)
 */
#define TXMODE_M8_BIT         0x00000040

/*
 * Data unit accumulator saturation point (default -> 40 bit accumulator)
 */
#define TXMODE_DXACCSAT_BIT 0x00000020 /* Set for 32-bit accumulator */

/*
 * Data unit accumulator saturation enable (default 0 -> no saturation)
 */
#define TXMODE_DXSAT_BIT    0x00000010

/*
 * Data unit master rounding control (default 0 -> normal, 1 -> convergent)
 */
#define TXMODE_DXROUNDING_BIT   0x00000008

/*
 * Data unit product shift for fractional arithmetic (default off)
 */
#define TXMODE_DXPRODSHIFT_BIT  0x00000004

/*
 * Select the arithmetic mode (multiply mostly) for both data units
 */
#define TXMODE_DXARITH_BITS 0x00000003
#define     TXMODE_DXARITH_32  3
#define     TXMODE_DXARITH_32H 2
#define     TXMODE_DXARITH_S16 1
#define     TXMODE_DXARITH_16  0

/*
 * TXMRSIZE register value only relevant when DSP modulo addressing active
 */
#define TXMRSIZE_REGNUM 12
#define     TXMRSIZE_MIN    0x0002  /* 0, 1 -> normal addressing logic */
#define     TXMRSIZE_MAX    0xFFFF

/*
 * TXDRCTRL register can be used to detect the actaul size of the DSP RAM
 * partitions allocated to this thread.
 */
#define TXDRCTRL_REGNUM 14
#define     TXDRCTRL_SINESIZE_BITS  0x0F000000
#define     TXDRCTRL_SINESIZE_S     24
#define     TXDRCTRL_RAMSZPOW_BITS  0x001F0000  /* Limit = (1<<RAMSZPOW)-1 */
#define     TXDRCTRL_RAMSZPOW_S     16
#define     TXDRCTRL_D1RSZAND_BITS  0x0000F000  /* Mask top 4 bits - D1 */
#define     TXDRCTRL_D1RSZAND_S     12
#define     TXDRCTRL_D0RSZAND_BITS  0x000000F0  /* Mask top 4 bits - D0 */
#define     TXDRCTRL_D0RSZAND_S     4
/* Given extracted RAMSZPOW and DnRSZAND fields this returns the size */
#define     TXDRCTRL_DXSIZE(Pow, AndBits) \
				((((~(AndBits)) & 0x0f) + 1) << ((Pow)-4))

/*
 * TXDRSIZE register provides modulo addressing options for each DSP RAM
 */
#define TXDRSIZE_REGNUM 15
#define     TXDRSIZE_R1MOD_BITS       0xFFFF0000
#define     TXDRSIZE_R1MOD_S          16
#define     TXDRSIZE_R0MOD_BITS       0x0000FFFF
#define     TXDRSIZE_R0MOD_S          0

#define     TXDRSIZE_RBRAD_SCALE_BITS 0x70000000
#define     TXDRSIZE_RBRAD_SCALE_S    28
#define     TXDRSIZE_RBMODSIZE_BITS   0x0FFF0000
#define     TXDRSIZE_RBMODSIZE_S      16
#define     TXDRSIZE_RARAD_SCALE_BITS 0x00007000
#define     TXDRSIZE_RARAD_SCALE_S    12
#define     TXDRSIZE_RAMODSIZE_BITS   0x00000FFF
#define     TXDRSIZE_RAMODSIZE_S      0

/*****************************************************************************
 *                       DEFERRED and BUS ERROR EXTENSION
 ****************************************************************************/

/*
 * TXDEFR register - Deferred exception control
 */
#define TXDEFR_REGNUM 20
#define     TXDEFR_DEFAULT  0   /* All fields default to zero */

/*
 * Bus error state is a multi-bit positive/negative event notification from
 * the bus infrastructure.
 */
#define     TXDEFR_BUS_ERR_BIT    0x80000000  /* Set if error (LSB STATE) */
#define     TXDEFR_BUS_ERRI_BIT   0x40000000  /* Fetch returned error */
#define     TXDEFR_BUS_STATE_BITS 0x3F000000  /* Bus event/state data */
#define     TXDEFR_BUS_STATE_S    24
#define     TXDEFR_BUS_TRIG_BIT   0x00800000  /* Set when bus error seen */

/*
 * Bus events are collected by background code in a deferred manner unless
 * selected to trigger an extended interrupt HALT trigger when they occur.
 */
#define     TXDEFR_BUS_ICTRL_BIT  0x00000080  /* Enable interrupt trigger */

/*
 * CHIP Automatic Mips Allocation control registers
 * ------------------------------------------------
 */

/* CT Bank AMA Registers */
#define TXAMAREG0_REGNUM 24
#ifdef METAC_1_2
#define     TXAMAREG0_CTRL_BITS       0x07000000
#else /* METAC_1_2 */
#define     TXAMAREG0_RCOFF_BIT       0x08000000
#define     TXAMAREG0_DLINEHLT_BIT    0x04000000
#define     TXAMAREG0_DLINEDIS_BIT    0x02000000
#define     TXAMAREG0_CYCSTRICT_BIT   0x01000000
#define     TXAMAREG0_CTRL_BITS       (TXAMAREG0_RCOFF_BIT |    \
				       TXAMAREG0_DLINEHLT_BIT | \
				       TXAMAREG0_DLINEDIS_BIT | \
				       TXAMAREG0_CYCSTRICT_BIT)
#endif /* !METAC_1_2 */
#define     TXAMAREG0_CTRL_S           24
#define     TXAMAREG0_MDM_BIT         0x00400000
#define     TXAMAREG0_MPF_BIT         0x00200000
#define     TXAMAREG0_MPE_BIT         0x00100000
#define     TXAMAREG0_MASK_BITS       (TXAMAREG0_MDM_BIT | \
				       TXAMAREG0_MPF_BIT | \
				       TXAMAREG0_MPE_BIT)
#define     TXAMAREG0_MASK_S          20
#define     TXAMAREG0_SDM_BIT         0x00040000
#define     TXAMAREG0_SPF_BIT         0x00020000
#define     TXAMAREG0_SPE_BIT         0x00010000
#define     TXAMAREG0_STATUS_BITS     (TXAMAREG0_SDM_BIT | \
				       TXAMAREG0_SPF_BIT | \
				       TXAMAREG0_SPE_BIT)
#define     TXAMAREG0_STATUS_S        16
#define     TXAMAREG0_PRIORITY_BITS   0x0000FF00
#define     TXAMAREG0_PRIORITY_S      8
#define     TXAMAREG0_BVALUE_BITS     0x000000FF
#define     TXAMAREG0_BVALUE_S  0

#define TXAMAREG1_REGNUM 25
#define     TXAMAREG1_DELAYC_BITS     0x07FFFFFF
#define     TXAMAREG1_DELAYC_S  0

#define TXAMAREG2_REGNUM 26
#ifdef METAC_1_2
#define     TXAMAREG2_DLINEC_BITS     0x00FFFFFF
#define     TXAMAREG2_DLINEC_S        0
#else /* METAC_1_2 */
#define     TXAMAREG2_IRQPRIORITY_BIT 0xFF000000
#define     TXAMAREG2_IRQPRIORITY_S   24
#define     TXAMAREG2_DLINEC_BITS     0x00FFFFF0
#define     TXAMAREG2_DLINEC_S        4
#endif /* !METAC_1_2 */

#define TXAMAREG3_REGNUM 27
#define     TXAMAREG2_AMABLOCK_BIT    0x00080000
#define     TXAMAREG2_AMAC_BITS       0x0000FFFF
#define     TXAMAREG2_AMAC_S          0

/*****************************************************************************
 *                                FPU EXTENSIONS
 ****************************************************************************/
/*
 * The following registers only exist in FPU enabled cores.
 */

/*
 * TXMODE register - FPU rounding mode control/status fields
 */
#define     TXMODE_FPURMODE_BITS     0x00030000
#define     TXMODE_FPURMODE_S        16
#define     TXMODE_FPURMODEWRITE_BIT 0x00040000  /* Set to change FPURMODE */

/*
 * TXDEFR register - FPU exception handling/state is a significant source
 *   of deferrable errors. Run-time S/W can move handling to interrupt level
 *   using DEFR instruction to collect state.
 */
#define     TXDEFR_FPE_FE_BITS       0x003F0000  /* Set by FPU_FE events */
#define     TXDEFR_FPE_FE_S          16

#define     TXDEFR_FPE_INEXACT_FE_BIT   0x010000
#define     TXDEFR_FPE_UNDERFLOW_FE_BIT 0x020000
#define     TXDEFR_FPE_OVERFLOW_FE_BIT  0x040000
#define     TXDEFR_FPE_DIVBYZERO_FE_BIT 0x080000
#define     TXDEFR_FPE_INVALID_FE_BIT   0x100000
#define     TXDEFR_FPE_DENORMAL_FE_BIT  0x200000

#define     TXDEFR_FPE_ICTRL_BITS    0x000003F   /* Route to interrupts */
#define     TXDEFR_FPE_ICTRL_S       0

#define     TXDEFR_FPE_INEXACT_ICTRL_BIT   0x01
#define     TXDEFR_FPE_UNDERFLOW_ICTRL_BIT 0x02
#define     TXDEFR_FPE_OVERFLOW_ICTRL_BIT  0x04
#define     TXDEFR_FPE_DIVBYZERO_ICTRL_BIT 0x08
#define     TXDEFR_FPE_INVALID_ICTRL_BIT   0x10
#define     TXDEFR_FPE_DENORMAL_ICTRL_BIT  0x20

/*
 * DETAILED FPU RELATED VALUES
 * ---------------------------
 */

/*
 * Rounding mode field in TXMODE can hold a number of logical values
 */
#define METAG_FPURMODE_TONEAREST  0x0      /* Default */
#define METAG_FPURMODE_TOWARDZERO 0x1
#define METAG_FPURMODE_UPWARD     0x2
#define METAG_FPURMODE_DOWNWARD   0x3

/*
 * In order to set the TXMODE register field that controls the rounding mode
 * an extra bit must be set in the value written versus that read in order
 * to gate writes to the rounding mode field. This allows other non-FPU code
 * to modify TXMODE without knowledge of the FPU units presence and not
 * influence the FPU rounding mode. This macro adds the required bit so new
 * rounding modes are accepted.
 */
#define TXMODE_FPURMODE_SET(FPURMode) \
	(TXMODE_FPURMODEWRITE_BIT + ((FPURMode)<<TXMODE_FPURMODE_S))

/*
 * To successfully restore TXMODE to zero at the end of the function the
 * following value (rather than zero) must be used.
 */
#define TXMODE_FPURMODE_RESET (TXMODE_FPURMODEWRITE_BIT)

/*
 * In TXSTATUS a special bit exists to indicate if FPU H/W has been accessed
 * since it was last reset.
 */
#define TXSTATUS_FPACTIVE_BIT  0x01000000

/*
 * Exception state (see TXDEFR_FPU_FE_*) and enabling (for interrupt
 * level processing (see TXDEFR_FPU_ICTRL_*) are controlled by similar
 * bit mask locations within each field.
 */
#define METAG_FPU_FE_INEXACT   0x01
#define METAG_FPU_FE_UNDERFLOW 0x02
#define METAG_FPU_FE_OVERFLOW  0x04
#define METAG_FPU_FE_DIVBYZERO 0x08
#define METAG_FPU_FE_INVALID   0x10
#define METAG_FPU_FE_DENORMAL  0x20
#define METAG_FPU_FE_ALL_EXCEPT (METAG_FPU_FE_INEXACT   | \
				 METAG_FPU_FE_UNDERFLOW | \
				 METAG_FPU_FE_OVERFLOW  | \
				 METAG_FPU_FE_DIVBYZERO | \
				 METAG_FPU_FE_INVALID   | \
				 METAG_FPU_FE_DENORMAL)

/*****************************************************************************
 *             THREAD CONTROL, ERROR, OR INTERRUPT STATE EXTENSIONS
 ****************************************************************************/
/*
 * The following values are only relevant to code that externally controls
 * threads, handles errors/interrupts, and/or set-up interrupt/error handlers
 * for subsequent use.
 */

/*
 * TXENABLE register fields - only ENABLE_BIT is potentially read/write
 */
#define TXENABLE_MAJOR_REV_BITS    0xFF000000
#define TXENABLE_MAJOR_REV_S       24
#define TXENABLE_MINOR_REV_BITS    0x00FF0000
#define TXENABLE_MINOR_REV_S       16
#define TXENABLE_CLASS_BITS        0x0000F000
#define TXENABLE_CLASS_S           12
#define TXENABLE_CLASS_DSP             0x0 /* -> DSP Thread */
#define TXENABLE_CLASS_LDSP            0x8 /* -> DSP LITE Thread */
#define TXENABLE_CLASS_GP              0xC /* -> General Purpose Thread */
#define     TXENABLE_CLASSALT_LFPU       0x2 /*  Set to indicate LITE FPU */
#define     TXENABLE_CLASSALT_FPUR8      0x1 /*  Set to indicate 8xFPU regs */
#define TXENABLE_MTXARCH_BIT       0x00000800
#define TXENABLE_STEP_REV_BITS     0x000000F0
#define TXENABLE_STEP_REV_S        4
#define TXENABLE_STOPPED_BIT       0x00000004   /* TXOFF due to ENABLE->0 */
#define TXENABLE_OFF_BIT           0x00000002   /* Thread is in off state */
#define TXENABLE_ENABLE_BIT        0x00000001   /* Set if running */

/*
 * TXSTATUS register - used by external/internal interrupt/error handler
 */
#define TXSTATUS_CB1MARKER_BIT     0x00800000   /* -> int level mem state */
#define TXSTATUS_CBMARKER_BIT      0x00400000   /* -> mem i/f state dumped */
#define TXSTATUS_MEM_FAULT_BITS    0x00300000
#define TXSTATUS_MEM_FAULT_S       20
#define     TXSTATUS_MEMFAULT_NONE  0x0 /* -> No memory fault       */
#define     TXSTATUS_MEMFAULT_GEN   0x1 /* -> General fault         */
#define     TXSTATUS_MEMFAULT_PF    0x2 /* -> Page fault            */
#define     TXSTATUS_MEMFAULT_RO    0x3 /* -> Read only fault       */
#define TXSTATUS_MAJOR_HALT_BITS   0x000C0000
#define TXSTATUS_MAJOR_HALT_S      18
#define     TXSTATUS_MAJHALT_TRAP 0x0   /* -> SWITCH inst used      */
#define     TXSTATUS_MAJHALT_INST 0x1   /* -> Unknown inst or fetch */
#define     TXSTATUS_MAJHALT_PRIV 0x2   /* -> Internal privilege    */
#define     TXSTATUS_MAJHALT_MEM  0x3   /* -> Memory i/f fault      */
#define TXSTATUS_L_STEP_BITS       0x00000800   /* -> Progress of L oper    */
#define TXSTATUS_LSM_STEP_BITS     0x00000700   /* -> Progress of L/S mult  */
#define TXSTATUS_LSM_STEP_S        8
#define TXSTATUS_FLAG_BITS         0x0000001F   /* -> All the flags         */
#define TXSTATUS_SCC_BIT           0x00000010   /* -> Split-16 flags ...    */
#define TXSTATUS_SCF_LZ_BIT        0x00000008   /* -> Split-16 Low  Z flag  */
#define TXSTATUS_SCF_HZ_BIT        0x00000004   /* -> Split-16 High Z flag  */
#define TXSTATUS_SCF_HC_BIT        0x00000002   /* -> Split-16 High C flag  */
#define TXSTATUS_SCF_LC_BIT        0x00000001   /* -> Split-16 Low  C flag  */
#define TXSTATUS_CF_Z_BIT          0x00000008   /* -> Condition Z flag      */
#define TXSTATUS_CF_N_BIT          0x00000004   /* -> Condition N flag      */
#define TXSTATUS_CF_O_BIT          0x00000002   /* -> Condition O flag      */
#define TXSTATUS_CF_C_BIT          0x00000001   /* -> Condition C flag      */

/*
 * TXCATCH0-3 register contents may store information on a memory operation
 * that has failed if the bit TXSTATUS_CBMARKER_BIT is set.
 */
#define TXCATCH0_REGNUM 16
#define TXCATCH1_REGNUM 17
#define     TXCATCH1_ADDR_BITS   0xFFFFFFFF   /* TXCATCH1 is Addr 0-31 */
#define     TXCATCH1_ADDR_S      0
#define TXCATCH2_REGNUM 18
#define     TXCATCH2_DATA0_BITS  0xFFFFFFFF   /* TXCATCH2 is Data 0-31 */
#define     TXCATCH2_DATA0_S     0
#define TXCATCH3_REGNUM 19
#define     TXCATCH3_DATA1_BITS  0xFFFFFFFF   /* TXCATCH3 is Data 32-63 */
#define     TXCATCH3_DATA1_S     0

/*
 * Detailed catch state information
 * --------------------------------
 */

/* Contents of TXCATCH0 register */
#define     TXCATCH0_LDRXX_BITS  0xF8000000  /* Load destination reg 0-31 */
#define     TXCATCH0_LDRXX_S     27
#define     TXCATCH0_LDDST_BITS  0x07FF0000  /* Load destination bits */
#define     TXCATCH0_LDDST_S     16
#define         TXCATCH0_LDDST_D1DSP 0x400   /* One bit set if it's a LOAD */
#define         TXCATCH0_LDDST_D0DSP 0x200
#define         TXCATCH0_LDDST_TMPLT 0x100
#define         TXCATCH0_LDDST_TR    0x080
#ifdef METAC_2_1
#define         TXCATCH0_LDDST_FPU   0x040
#endif
#define         TXCATCH0_LDDST_PC    0x020
#define         TXCATCH0_LDDST_A1    0x010
#define         TXCATCH0_LDDST_A0    0x008
#define         TXCATCH0_LDDST_D1    0x004
#define         TXCATCH0_LDDST_D0    0x002
#define         TXCATCH0_LDDST_CT    0x001
#ifdef METAC_2_1
#define     TXCATCH0_WATCHSTOP_BIT 0x00004000  /* Set if Data Watch set fault */
#endif
#define     TXCATCH0_WATCHS_BIT  0x00004000  /* Set if Data Watch set fault */
#define     TXCATCH0_WATCH1_BIT  0x00002000  /* Set if Data Watch 1 matches */
#define     TXCATCH0_WATCH0_BIT  0x00001000  /* Set if Data Watch 0 matches */
#define     TXCATCH0_FAULT_BITS  0x00000C00  /* See TXSTATUS_MEMFAULT_*     */
#define     TXCATCH0_FAULT_S     10
#define     TXCATCH0_PRIV_BIT    0x00000200  /* Privilege of transaction    */
#define     TXCATCH0_READ_BIT    0x00000100  /* Set for Read or Load cases  */

#ifdef METAC_2_1
/* LNKGET Marker bit in TXCATCH0 */
#define   TXCATCH0_LNKGET_MARKER_BIT 0x00000008
#define       TXCATCH0_PREPROC_BIT  0x00000004
#endif

/* Loads are indicated by one of the LDDST bits being set */
#define     TXCATCH0_LDM16_BIT   0x00000004  /* Load M16 flag */
#define     TXCATCH0_LDL2L1_BITS 0x00000003  /* Load data size L2,L1 */
#define     TXCATCH0_LDL2L1_S    0

/* Reads are indicated by the READ bit being set without LDDST bits */
#define     TXCATCH0_RAXX_BITS   0x0000001F  /* RAXX issue port for read */
#define     TXCATCH0_RAXX_S      0

/* Write operations are all that remain if READ bit is not set */
#define     TXCATCH0_WMASK_BITS  0x000000FF  /* Write byte lane mask */
#define     TXCATCH0_WMASK_S     0

#ifdef METAC_2_1

/* When a FPU exception is signalled then FPUSPEC == FPUSPEC_TAG */
#define     TXCATCH0_FPURDREG_BITS    0xF8000000
#define     TXCATCH0_FPURDREG_S       27
#define     TXCATCH0_FPUR1REG_BITS    0x07C00000
#define     TXCATCH0_FPUR1REG_S       22
#define     TXCATCH0_FPUSPEC_BITS     0x000F0000
#define     TXCATCH0_FPUSPEC_S        16
#define         TXCATCH0_FPUSPEC_TAG      0xF
#define     TXCATCH0_FPUINSTA_BIT     0x00001000
#define     TXCATCH0_FPUINSTQ_BIT     0x00000800
#define     TXCATCH0_FPUINSTZ_BIT     0x00000400
#define     TXCATCH0_FPUINSTN_BIT     0x00000200
#define     TXCATCH0_FPUINSTO3O_BIT   0x00000100
#define     TXCATCH0_FPUWIDTH_BITS    0x000000C0
#define     TXCATCH0_FPUWIDTH_S       6
#define         TXCATCH0_FPUWIDTH_FLOAT   0
#define         TXCATCH0_FPUWIDTH_DOUBLE  1
#define         TXCATCH0_FPUWIDTH_PAIRED  2
#define     TXCATCH0_FPUOPENC_BITS    0x0000003F
#define     TXCATCH0_FPUOPENC_S       0
#define         TXCATCH0_FPUOPENC_ADD     0  /* rop1=Rs1, rop3=Rs2 */
#define         TXCATCH0_FPUOPENC_SUB     1  /* rop1=Rs1, rop3=Rs2 */
#define         TXCATCH0_FPUOPENC_MUL     2  /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_ATOI    3  /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_ATOX    4  /* rop3=Rs, uses #Imm */
#define         TXCATCH0_FPUOPENC_ITOA    5  /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_XTOA    6  /* rop3=Rs, uses #Imm */
#define         TXCATCH0_FPUOPENC_ATOH    7  /* rop2=Rs */
#define         TXCATCH0_FPUOPENC_HTOA    8  /* rop2=Rs */
#define         TXCATCH0_FPUOPENC_DTOF    9  /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_FTOD    10 /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_DTOL    11 /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_LTOD    12 /* rop3=Rs */
#define         TXCATCH0_FPUOPENC_DTOXL   13 /* rop3=Rs, uses #imm */
#define         TXCATCH0_FPUOPENC_XLTOD   14 /* rop3=Rs, uses #imm */
#define         TXCATCH0_FPUOPENC_CMP     15 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_MIN     16 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_MAX     17 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_ADDRE   18 /* rop1=Rs1, rop3=Rs2 */
#define         TXCATCH0_FPUOPENC_SUBRE   19 /* rop1=Rs1, rop3=Rs2 */
#define         TXCATCH0_FPUOPENC_MULRE   20 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_MXA     21 /* rop1=Rs1, rop2=Rs2, rop3=Rs3*/
#define         TXCATCH0_FPUOPENC_MXAS    22 /* rop1=Rs1, rop2=Rs2, rop3=Rs3*/
#define         TXCATCH0_FPUOPENC_MAR     23 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_MARS    24 /* rop1=Rs1, rop2=Rs2 */
#define         TXCATCH0_FPUOPENC_MUZ     25 /* rop1=Rs1, rop2=Rs2, rop3=Rs3*/
#define         TXCATCH0_FPUOPENC_MUZS    26 /* rop1=Rs1, rop2=Rs2, rop3=Rs3*/
#define         TXCATCH0_FPUOPENC_RCP     27 /* rop2=Rs */
#define         TXCATCH0_FPUOPENC_RSQ     28 /* rop2=Rs */

/* For floating point exceptions TXCATCH1 is used to carry extra data */
#define     TXCATCH1_FPUR2REG_BITS    0xF8000000
#define     TXCATCH1_FPUR2REG_S       27
#define     TXCATCH1_FPUR3REG_BITS    0x07C00000  /* Undefined if O3O set */
#define     TXCATCH1_FPUR3REG_S       22
#define     TXCATCH1_FPUIMM16_BITS    0x0000FFFF
#define     TXCATCH1_FPUIMM16_S       0

#endif /* METAC_2_1 */

/*
 * TXDIVTIME register used to hold the partial base address of memory i/f
 * state dump area. Now deprecated.
 */
#define     TXDIVTIME_CBBASE_MASK    0x03FFFE00
#define     TXDIVTIME_CBBASE_LINBASE 0x80000000
#define     TXDIVTIME_CBBASE_LINBOFF 0x00000000 /* BGnd state */
#define     TXDIVTIME_CBBASE_LINIOFF 0x00000100 /* Int  state */

/*
 * TXDIVTIME register used to indicate if the read pipeline was dirty when a
 * thread was interrupted, halted, or generated an exception. It is invalid
 * to attempt to issue a further pipeline read address while the read
 * pipeline is in the dirty state.
 */
#define     TXDIVTIME_RPDIRTY_BIT   0x80000000

/*
 * Further bits in the TXDIVTIME register allow interrupt handling code to
 * short-cut the discovery the most significant bit last read from TXSTATI.
 *
 * This is the bit number of the trigger line that a low level interrupt
 * handler should acknowledge and then perhaps the index of a corresponding
 * handler function.
 */
#define     TXDIVTIME_IRQENC_BITS   0x0F000000
#define     TXDIVTIME_IRQENC_S      24

/*
 * If TXDIVTIME_RPVALID_BIT is set the read pipeline contained significant
 * information when the thread was interrupted|halted|exceptioned. Each slot
 * containing data is indicated by a one bit in the corresponding
 * TXDIVTIME_RPMASK_BITS bit (least significance bit relates to first
 * location in read pipeline - most likely to have the 1 state). Empty slots
 * contain zeroes with no interlock applied on reads if RPDIRTY is currently
 * set with RPMASK itself being read-only state.
 */
#define     TXDIVTIME_RPMASK_BITS 0x003F0000   /* -> Full (1) Empty (0) */
#define     TXDIVTIME_RPMASK_S    16

/*
 * TXPRIVEXT register can be used to single step thread execution and
 * enforce synchronous memory i/f address checking for debugging purposes.
 */
#define     TXPRIVEXT_TXSTEP_BIT    0x00000004
#define     TXPRIVEXT_MEMCHECK_BIT  0x00000002

/*
 * TXINTERNx registers holds internal state information for H/W debugging only
 */
#define TXINTERN0_REGNUM 23
#define     TXINTERN0_LOCK2_BITS  0xF0000000
#define     TXINTERN0_LOCK2_S     28
#define     TXINTERN0_LOCK1_BITS  0x0F000000
#define     TXINTERN0_LOCK1_S     24
#define     TXINTERN0_TIFDF_BITS  0x0000F000
#define     TXINTERN0_TIFDF_S     12
#define     TXINTERN0_TIFIB_BITS  0x00000F00
#define     TXINTERN0_TIFIB_S     8
#define     TXINTERN0_TIFAF_BITS  0x000000F0
#define     TXINTERN0_TIFAF_S     4
#define     TXINTERN0_MSTATE_BITS 0x0000000F
#define     TXINTERN0_MSTATE_S    0

/*
 * TXSTAT, TXMASK, TXPOLL, TXSTATI, TXMASKI, TXPOLLI registers from trigger
 * bank all have similar contents (upper kick count bits not in MASK regs)
 */
#define TXSTAT_REGNUM  0
#define     TXSTAT_TIMER_BIT    0x00000001
#define     TXSTAT_TIMER_S      0
#define     TXSTAT_KICK_BIT     0x00000002
#define     TXSTAT_KICK_S       1
#define     TXSTAT_DEFER_BIT    0x00000008
#define     TXSTAT_DEFER_S      3
#define     TXSTAT_EXTTRIG_BITS 0x0000FFF0
#define     TXSTAT_EXTTRIG_S    4
#define     TXSTAT_FPE_BITS     0x003F0000
#define     TXSTAT_FPE_S        16
#define     TXSTAT_FPE_DENORMAL_BIT    0x00200000
#define     TXSTAT_FPE_DENORMAL_S      21
#define     TXSTAT_FPE_INVALID_BIT     0x00100000
#define     TXSTAT_FPE_INVALID_S       20
#define     TXSTAT_FPE_DIVBYZERO_BIT   0x00080000
#define     TXSTAT_FPE_DIVBYZERO_S     19
#define     TXSTAT_FPE_OVERFLOW_BIT    0x00040000
#define     TXSTAT_FPE_OVERFLOW_S      18
#define     TXSTAT_FPE_UNDERFLOW_BIT   0x00020000
#define     TXSTAT_FPE_UNDERFLOW_S     17
#define     TXSTAT_FPE_INEXACT_BIT     0x00010000
#define     TXSTAT_FPE_INEXACT_S       16
#define     TXSTAT_BUSERR_BIT          0x00800000   /* Set if bus error/ack state */
#define     TXSTAT_BUSERR_S            23
#define         TXSTAT_BUSSTATE_BITS     0xFF000000 /* Read only */
#define         TXSTAT_BUSSTATE_S        24
#define     TXSTAT_KICKCNT_BITS 0xFFFF0000
#define     TXSTAT_KICKCNT_S    16
#define TXMASK_REGNUM  1
#define TXSTATI_REGNUM 2
#define     TXSTATI_BGNDHALT_BIT    0x00000004
#define TXMASKI_REGNUM 3
#define TXPOLL_REGNUM  4
#define TXPOLLI_REGNUM 6

/*
 * TXDRCTRL register can be used to partition the DSP RAM space available to
 * this thread at startup. This is achieved by offsetting the region allocated
 * to each thread.
 */
#define     TXDRCTRL_D1PARTOR_BITS  0x00000F00  /* OR's into top 4 bits */
#define     TXDRCTRL_D1PARTOR_S     8
#define     TXDRCTRL_D0PARTOR_BITS  0x0000000F  /* OR's into top 4 bits */
#define     TXDRCTRL_D0PARTOR_S     0
/* Given extracted Pow and Or fields this is threads base within DSP RAM */
#define     TXDRCTRL_DXBASE(Pow, Or)  ((Or)<<((Pow)-4))

/*****************************************************************************
 *                      RUN TIME TRACE CONTROL REGISTERS
 ****************************************************************************/
/*
 * The following values are only relevant to code that implements run-time
 *  trace features within the META Core
 */
#define TTEXEC      TT.0
#define TTCTRL      TT.1
#define TTMARK      TT.2
#define TTREC       TT.3
#define GTEXEC      TT.4

#define TTEXEC_REGNUM               0
#define     TTEXEC_EXTTRIGAND_BITS      0x7F000000
#define     TTEXEC_EXTTRIGAND_S         24
#define     TTEXEC_EXTTRIGEN_BIT        0x00008000
#define     TTEXEC_EXTTRIGMATCH_BITS    0x00007F00
#define     TTEXEC_EXTTRIGMATCH_S       8
#define     TTEXEC_TCMODE_BITS          0x00000003
#define     TTEXEC_TCMODE_S             0

#define TTCTRL_REGNUM               1
#define     TTCTRL_TRACETT_BITS         0x00008000
#define     TTCTRL_TRACETT_S            15
#define     TTCTRL_TRACEALL_BITS        0x00002000
#define     TTCTRL_TRACEALL_S           13
#ifdef METAC_2_1
#define     TTCTRL_TRACEALLTAG_BITS     0x00000400
#define     TTCTRL_TRACEALLTAG_S        10
#endif /* METAC_2_1 */
#define     TTCTRL_TRACETAG_BITS        0x00000200
#define     TTCTRL_TRACETAG_S           9
#define     TTCTRL_TRACETTPC_BITS       0x00000080
#define     TTCTRL_TRACETTPC_S          7
#define     TTCTRL_TRACEMPC_BITS        0x00000020
#define     TTCTRL_TRACEMPC_S           5
#define     TTCTRL_TRACEEN_BITS         0x00000008
#define     TTCTRL_TRACEEN_S            3
#define     TTCTRL_TRACEEN1_BITS        0x00000004
#define     TTCTRL_TRACEEN1_S           2
#define     TTCTRL_TRACEPC_BITS         0x00000002
#define     TTCTRL_TRACEPC_S            1

#ifdef METAC_2_1
#define TTMARK_REGNUM   2
#define TTMARK_BITS                 0xFFFFFFFF
#define TTMARK_S                    0x0

#define TTREC_REGNUM    3
#define TTREC_BITS                  0xFFFFFFFFFFFFFFFF
#define TTREC_S                     0x0
#endif /* METAC_2_1 */

#define GTEXEC_REGNUM               4
#define     GTEXEC_DCRUN_BITS           0x80000000
#define     GTEXEC_DCRUN_S              31
#define     GTEXEC_ICMODE_BITS          0x0C000000
#define     GTEXEC_ICMODE_S             26
#define     GTEXEC_TCMODE_BITS          0x03000000
#define     GTEXEC_TCMODE_S             24
#define     GTEXEC_PERF1CMODE_BITS      0x00040000
#define     GTEXEC_PERF1CMODE_S         18
#define     GTEXEC_PERF0CMODE_BITS      0x00010000
#define     GTEXEC_PERF0CMODE_S         16
#define     GTEXEC_REFMSEL_BITS         0x0000F000
#define     GTEXEC_REFMSEL_S            12
#define     GTEXEC_METRICTH_BITS        0x000003FF
#define     GTEXEC_METRICTH_S           0

#ifdef METAC_2_1
/*
 * Clock Control registers
 * -----------------------
 */
#define TXCLKCTRL_REGNUM        22

/*
 * Default setting is with clocks always on (DEFON), turning all clocks off
 * can only be done from external devices (OFF), enabling automatic clock
 * gating will allow clocks to stop as units fall idle.
 */
#define TXCLKCTRL_ALL_OFF       0x02222222
#define TXCLKCTRL_ALL_DEFON     0x01111111
#define TXCLKCTRL_ALL_AUTO      0x02222222

/*
 * Individual fields control caches, floating point and main data/addr units
 */
#define TXCLKCTRL_CLOCKIC_BITS  0x03000000
#define TXCLKCTRL_CLOCKIC_S     24
#define TXCLKCTRL_CLOCKDC_BITS  0x00300000
#define TXCLKCTRL_CLOCKDC_S     20
#define TXCLKCTRL_CLOCKFP_BITS  0x00030000
#define TXCLKCTRL_CLOCKFP_S     16
#define TXCLKCTRL_CLOCKD1_BITS  0x00003000
#define TXCLKCTRL_CLOCKD1_S     12
#define TXCLKCTRL_CLOCKD0_BITS  0x00000300
#define TXCLKCTRL_CLOCKD0_S     8
#define TXCLKCTRL_CLOCKA1_BITS  0x00000030
#define TXCLKCTRL_CLOCKA1_S     4
#define TXCLKCTRL_CLOCKA0_BITS  0x00000003
#define TXCLKCTRL_CLOCKA0_S     0

/*
 * Individual settings for each field are common
 */
#define TXCLKCTRL_CLOCKxx_OFF   0
#define TXCLKCTRL_CLOCKxx_DEFON 1
#define TXCLKCTRL_CLOCKxx_AUTO  2

#endif /* METAC_2_1 */

#ifdef METAC_2_1
/*
 * Fast interrupt new bits
 * ------------------------------------
 */
#define TXSTATUS_IPTOGGLE_BIT           0x80000000 /* Prev PToggle of TXPRIVEXT */
#define TXSTATUS_ISTATE_BIT             0x40000000 /* IState bit */
#define TXSTATUS_IWAIT_BIT              0x20000000 /* wait indefinitely in decision step*/
#define TXSTATUS_IEXCEPT_BIT            0x10000000 /* Indicate an exception occured */
#define TXSTATUS_IRPCOUNT_BITS          0x0E000000 /* Number of 'dirty' date entries*/
#define TXSTATUS_IRPCOUNT_S             25
#define TXSTATUS_IRQSTAT_BITS           0x0000F000 /* IRQEnc bits, trigger or interrupts */
#define TXSTATUS_IRQSTAT_S              12
#define TXSTATUS_LNKSETOK_BIT           0x00000020 /* LNKSetOK bit, successful LNKSET */

/* New fields in TXDE for fast interrupt system */
#define TXDIVTIME_IACTIVE_BIT           0x00008000 /* Enable new interrupt system */
#define TXDIVTIME_INONEST_BIT           0x00004000 /* Gate nested interrupt */
#define TXDIVTIME_IREGIDXGATE_BIT       0x00002000 /* gate of the IRegIdex field */
#define TXDIVTIME_IREGIDX_BITS          0x00001E00 /* Index of A0.0/1 replaces */
#define TXDIVTIME_IREGIDX_S             9
#define TXDIVTIME_NOST_BIT              0x00000100 /* disable superthreading bit */
#endif

#endif /* _ASM_METAG_REGS_H_ */
