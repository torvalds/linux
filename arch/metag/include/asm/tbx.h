/*
 * asm/tbx.h
 *
 * Copyright (C) 2000-2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Thread binary interface header
 */

#ifndef _ASM_METAG_TBX_H_
#define _ASM_METAG_TBX_H_

/* for CACHEW_* values */
#include <asm/metag_isa.h>
/* for LINSYSEVENT_* addresses */
#include <asm/metag_mem.h>

#ifdef  TBI_1_4
#ifndef TBI_MUTEXES_1_4
#define TBI_MUTEXES_1_4
#endif
#ifndef TBI_SEMAPHORES_1_4
#define TBI_SEMAPHORES_1_4
#endif
#ifndef TBI_ASYNC_SWITCH_1_4
#define TBI_ASYNC_SWITCH_1_4
#endif
#ifndef TBI_FASTINT_1_4
#define TBI_FASTINT_1_4
#endif
#endif


/* Id values in the TBI system describe a segment using an arbitrary
   integer value and flags in the bottom 8 bits, the SIGPOLL value is
   used in cases where control over blocking or polling behaviour is
   needed. */
#define TBID_SIGPOLL_BIT    0x02 /* Set bit in an Id value to poll vs block */
/* Extended segment identifiers use strings in the string table */
#define TBID_IS_SEGSTR( Id ) (((Id) & (TBID_SEGTYPE_BITS>>1)) == 0)

/* Segment identifiers contain the following related bit-fields */
#define TBID_SEGTYPE_BITS   0x0F /* One of the predefined segment types */
#define TBID_SEGTYPE_S      0
#define TBID_SEGSCOPE_BITS  0x30 /* Indicates the scope of the segment */
#define TBID_SEGSCOPE_S     4
#define TBID_SEGGADDR_BITS  0xC0 /* Indicates access possible via pGAddr */
#define TBID_SEGGADDR_S     6

/* Segments of memory can only really contain a few types of data */
#define TBID_SEGTYPE_TEXT   0x02 /* Code segment */
#define TBID_SEGTYPE_DATA   0x04 /* Data segment */
#define TBID_SEGTYPE_STACK  0x06 /* Stack segment */
#define TBID_SEGTYPE_HEAP   0x0A /* Heap segment */
#define TBID_SEGTYPE_ROOT   0x0C /* Root block segments */
#define TBID_SEGTYPE_STRING 0x0E /* String table segment */

/* Segments have one of three possible scopes */
#define TBID_SEGSCOPE_INIT     0 /* Temporary area for initialisation phase */
#define TBID_SEGSCOPE_LOCAL    1 /* Private to this thread */
#define TBID_SEGSCOPE_GLOBAL   2 /* Shared globally throughout the system */
#define TBID_SEGSCOPE_SHARED   3 /* Limited sharing between local/global */

/* For segment specifier a further field in two of the remaining bits
   indicates the usefulness of the pGAddr field in the segment descriptor
   descriptor. */
#define TBID_SEGGADDR_NULL     0 /* pGAddr is NULL -> SEGSCOPE_(LOCAL|INIT) */
#define TBID_SEGGADDR_READ     1 /* Only read    via pGAddr */
#define TBID_SEGGADDR_WRITE    2 /* Full access  via pGAddr */
#define TBID_SEGGADDR_EXEC     3 /* Only execute via pGAddr */

/* The following values are common to both segment and signal Id value and
   live in the top 8 bits of the Id values. */

/* The ISTAT bit indicates if segments are related to interrupt vs
   background level interfaces a thread can still handle all triggers at
   either level, but can also split these up if it wants to. */
#define TBID_ISTAT_BIT    0x01000000
#define TBID_ISTAT_S      24

/* Privilege needed to access a segment is indicated by the next bit.
   
   This bit is set to mirror the current privilege level when starting a
   search for a segment - setting it yourself toggles the automatically
   generated state which is only useful to emulate unprivileged behaviour
   or access unprivileged areas of memory while at privileged level. */
#define TBID_PSTAT_BIT    0x02000000
#define TBID_PSTAT_S      25

/* The top six bits of a signal/segment specifier identifies a thread within
   the system. This represents a segments owner. */
#define TBID_THREAD_BITS  0xFC000000
#define TBID_THREAD_S     26

/* Special thread id values */
#define TBID_THREAD_NULL   (-32) /* Never matches any thread/segment id used */
#define TBID_THREAD_GLOBAL (-31) /* Things global to all threads */
#define TBID_THREAD_HOST   ( -1) /* Host interface */
#define TBID_THREAD_EXTIO  (TBID_THREAD_HOST)   /* Host based ExtIO i/f */

/* Virtual Id's are used for external thread interface structures or the
   above special Id's */
#define TBID_IS_VIRTTHREAD( Id ) ((Id) < 0)

/* Real Id's are used for actual hardware threads that are local */
#define TBID_IS_REALTHREAD( Id ) ((Id) >= 0)

/* Generate a segment Id given Thread, Scope, and Type */
#define TBID_SEG( Thread, Scope, Type )                           (\
    ((Thread)<<TBID_THREAD_S) + ((Scope)<<TBID_SEGSCOPE_S) + (Type))

/* Generate a signal Id given Thread and SigNum */
#define TBID_SIG( Thread, SigNum )                                        (\
    ((Thread)<<TBID_THREAD_S) + ((SigNum)<<TBID_SIGNUM_S) + TBID_SIGNAL_BIT)

/* Generate an Id that solely represents a thread - useful for cache ops */
#define TBID_THD( Thread ) ((Thread)<<TBID_THREAD_S)
#define TBID_THD_NULL      ((TBID_THREAD_NULL)  <<TBID_THREAD_S)
#define TBID_THD_GLOBAL    ((TBID_THREAD_GLOBAL)<<TBID_THREAD_S)

/* Common exception handler (see TBID_SIGNUM_XXF below) receives hardware
   generated fault codes TBIXXF_SIGNUM_xxF in it's SigNum parameter */
#define TBIXXF_SIGNUM_IIF   0x01 /* General instruction fault */
#define TBIXXF_SIGNUM_PGF   0x02 /* Privilege general fault */
#define TBIXXF_SIGNUM_DHF   0x03 /* Data access watchpoint HIT */
#define TBIXXF_SIGNUM_IGF   0x05 /* Code fetch general read failure */
#define TBIXXF_SIGNUM_DGF   0x07 /* Data access general read/write fault */
#define TBIXXF_SIGNUM_IPF   0x09 /* Code fetch page fault */
#define TBIXXF_SIGNUM_DPF   0x0B /* Data access page fault */
#define TBIXXF_SIGNUM_IHF   0x0D /* Instruction breakpoint HIT */
#define TBIXXF_SIGNUM_DWF   0x0F /* Data access read-only fault */

/* Hardware signals communicate events between processing levels within a
   single thread all the _xxF cases are exceptions and are routed via a
   common exception handler, _SWx are software trap events and kicks including
   __TBISignal generated kicks, and finally _TRx are hardware triggers */
#define TBID_SIGNUM_SW0     0x00 /* SWITCH GROUP 0 - Per thread user */
#define TBID_SIGNUM_SW1     0x01 /* SWITCH GROUP 1 - Per thread system */
#define TBID_SIGNUM_SW2     0x02 /* SWITCH GROUP 2 - Internal global request */
#define TBID_SIGNUM_SW3     0x03 /* SWITCH GROUP 3 - External global request */
#ifdef TBI_1_4
#define TBID_SIGNUM_FPE     0x04 /* Deferred exception - Any IEEE 754 exception */
#define TBID_SIGNUM_FPD     0x05 /* Deferred exception - Denormal exception */
/* Reserved 0x6 for a reserved deferred exception */
#define TBID_SIGNUM_BUS     0x07 /* Deferred exception - Bus Error */
/* Reserved 0x08-0x09 */
#else
/* Reserved 0x04-0x09 */
#endif
/* Reserved 0x0A-0x0F */
#define TBID_SIGNUM_TRT     0x10 /* Timer trigger */
#define TBID_SIGNUM_LWK     0x11 /* Low level kick */
#define TBID_SIGNUM_XXF     0x12 /* Fault handler - receives ALL _xxF sigs */
#ifdef TBI_1_4
#define TBID_SIGNUM_DFR     0x13 /* Deferred Exception handler */
#else
#define TBID_SIGNUM_FPE     0x13 /* FPE Exception handler */
#endif
/* External trigger one group 0x14 to 0x17 - per thread */
#define TBID_SIGNUM_TR1(Thread) (0x14+(Thread))
#define TBID_SIGNUM_T10     0x14
#define TBID_SIGNUM_T11     0x15
#define TBID_SIGNUM_T12     0x16
#define TBID_SIGNUM_T13     0x17
/* External trigger two group 0x18 to 0x1b - per thread */
#define TBID_SIGNUM_TR2(Thread) (0x18+(Thread))
#define TBID_SIGNUM_T20     0x18
#define TBID_SIGNUM_T21     0x19
#define TBID_SIGNUM_T22     0x1A
#define TBID_SIGNUM_T23     0x1B
#define TBID_SIGNUM_TR3     0x1C /* External trigger N-4 (global) */
#define TBID_SIGNUM_TR4     0x1D /* External trigger N-3 (global) */
#define TBID_SIGNUM_TR5     0x1E /* External trigger N-2 (global) */
#define TBID_SIGNUM_TR6     0x1F /* External trigger N-1 (global) */
#define TBID_SIGNUM_MAX     0x1F

/* Return the trigger register(TXMASK[I]/TXSTAT[I]) bits related to
   each hardware signal, sometimes this is a many-to-one relationship. */
#define TBI_TRIG_BIT(SigNum)                                      (\
    ((SigNum) >= TBID_SIGNUM_TRT) ? 1<<((SigNum)-TBID_SIGNUM_TRT) :\
    ((SigNum) == TBID_SIGNUM_LWK) ?                                \
                         TXSTAT_KICK_BIT : TXSTATI_BGNDHALT_BIT    )

/* Return the hardware trigger vector number for entries in the
   HWVEC0EXT table that will generate the required internal trigger. */
#define TBI_TRIG_VEC(SigNum)                                      (\
    ((SigNum) >= TBID_SIGNUM_T10) ? ((SigNum)-TBID_SIGNUM_TRT) : -1)

/* Default trigger masks for each thread at background/interrupt level */
#define TBI_TRIGS_INIT( Thread )                           (\
    TXSTAT_KICK_BIT + TBI_TRIG_BIT(TBID_SIGNUM_TR1(Thread)) )
#define TBI_INTS_INIT( Thread )                            (\
    TXSTAT_KICK_BIT + TXSTATI_BGNDHALT_BIT                  \
                    + TBI_TRIG_BIT(TBID_SIGNUM_TR2(Thread)) )

#ifndef __ASSEMBLY__
/* A spin-lock location is a zero-initialised location in memory */
typedef volatile int TBISPIN, *PTBISPIN;

/* A kick location is a hardware location you can write to
 * in order to cause a kick
 */
typedef volatile int *PTBIKICK;

#if defined(METAC_1_0) || defined(METAC_1_1)
/* Macro to perform a kick */
#define TBI_KICK( pKick ) do { pKick[0] = 1; } while (0)
#else
/* #define METAG_LIN_VALUES before including machine.h if required */
#ifdef LINSYSEVENT_WR_COMBINE_FLUSH
/* Macro to perform a kick - write combiners must be flushed */
#define TBI_KICK( pKick )                                                do {\
    volatile int *pFlush = (volatile int *) LINSYSEVENT_WR_COMBINE_FLUSH;    \
    pFlush[0] = 0;                                                           \
    pKick[0]  = 1;                                                } while (0)
#endif
#endif /* if defined(METAC_1_0) || defined(METAC_1_1) */
#endif /* ifndef __ASSEMBLY__ */

#ifndef __ASSEMBLY__
/* 64-bit dual unit state value */
typedef struct _tbidual_tag_ {
    /* 32-bit value from a pair of registers in data or address units */
    int U0, U1;
} TBIDUAL, *PTBIDUAL;
#endif /* ifndef __ASSEMBLY__ */

/* Byte offsets of fields within TBIDUAL */
#define TBIDUAL_U0      (0)
#define TBIDUAL_U1      (4)

#define TBIDUAL_BYTES   (8)

#define TBICTX_CRIT_BIT 0x0001  /* ASync state saved in TBICTX */
#define TBICTX_SOFT_BIT 0x0002  /* Sync state saved in TBICTX (other bits 0) */
#ifdef TBI_FASTINT_1_4
#define TBICTX_FINT_BIT 0x0004  /* Using Fast Interrupts */
#endif
#define TBICTX_FPAC_BIT 0x0010  /* FPU state in TBICTX, FPU active on entry */
#define TBICTX_XMCC_BIT 0x0020  /* Bit to identify a MECC task */
#define TBICTX_CBUF_BIT 0x0040  /* Hardware catch buffer flag from TXSTATUS */
#define TBICTX_CBRP_BIT 0x0080  /* Read pipeline dirty from TXDIVTIME */
#define TBICTX_XDX8_BIT 0x0100  /* Saved DX.8 to DX.15 too */
#define TBICTX_XAXX_BIT 0x0200  /* Save remaining AX registers to AX.7 */
#define TBICTX_XHL2_BIT 0x0400  /* Saved hardware loop registers too */
#define TBICTX_XTDP_BIT 0x0800  /* Saved DSP registers too */
#define TBICTX_XEXT_BIT 0x1000  /* Set if TBICTX.Ext.Ctx contains extended
                                   state save area, otherwise TBICTX.Ext.AX2
                                   just holds normal A0.2 and A1.2 states */
#define TBICTX_WAIT_BIT 0x2000  /* Causes wait for trigger - sticky toggle */
#define TBICTX_XCBF_BIT 0x4000  /* Catch buffer or RD extracted into TBICTX */
#define TBICTX_PRIV_BIT 0x8000  /* Set if system uses 'privileged' model */

#ifdef METAC_1_0
#define TBICTX_XAX3_BIT 0x0200  /* Saved AX.5 to AX.7 for XAXX */
#define TBICTX_AX_REGS  5       /* Ax.0 to Ax.4 are core GP regs on CHORUS */
#else
#define TBICTX_XAX4_BIT 0x0200  /* Saved AX.4 to AX.7 for XAXX */
#define TBICTX_AX_REGS  4       /* Default is Ax.0 to Ax.3 */
#endif

#ifdef TBI_1_4
#define TBICTX_CFGFPU_FX16_BIT  0x00010000               /* Save FX.8 to FX.15 too */

/* The METAC_CORE_ID_CONFIG field indicates omitted DSP resources */
#define METAC_COREID_CFGXCTX_MASK( Value )                                 (\
	( (((Value & METAC_COREID_CFGDSP_BITS)>>                                \
	             METAC_COREID_CFGDSP_S      ) == METAC_COREID_CFGDSP_MIN) ? \
	         ~(TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+                             \
	           TBICTX_XAXX_BIT+TBICTX_XDX8_BIT ) : ~0U )                    )
#endif

/* Extended context state provides a standardised method for registering the
   arguments required by __TBICtxSave to save the additional register states
   currently in use by non general purpose code. The state of the __TBIExtCtx
   variable in the static space of the thread forms an extension of the base
   context of the thread.
   
   If ( __TBIExtCtx.Ctx.SaveMask == 0 ) then pExt is assumed to be NULL and
   the empty state of  __TBIExtCtx is represented by the fact that
   TBICTX.SaveMask does not have the bit TBICTX_XEXT_BIT set.
   
   If ( __TBIExtCtx.Ctx.SaveMask != 0 ) then pExt should point at a suitably
   sized extended context save area (usually at the end of the stack space
   allocated by the current routine). This space should allow for the
   displaced state of A0.2 and A1.2 to be saved along with the other extended
   states indicated via __TBIExtCtx.Ctx.SaveMask. */
#ifndef __ASSEMBLY__
typedef union _tbiextctx_tag_ {
    long long Val;
    TBIDUAL AX2;
    struct _tbiextctxext_tag {
#ifdef TBI_1_4
        short DspramSizes;      /* DSPRAM sizes. Encoding varies between
                                   TBICtxAlloc and the ECH scheme. */
#else
        short Reserved0;
#endif
        short SaveMask;         /* Flag bits for state saved */
        PTBIDUAL pExt;          /* AX[2] state saved first plus Xxxx state */
    
    } Ctx;
    
} TBIEXTCTX, *PTBIEXTCTX;

/* Automatic registration of extended context save for __TBINestInts */
extern TBIEXTCTX __TBIExtCtx;
#endif /* ifndef __ASSEMBLY__ */

/* Byte offsets of fields within TBIEXTCTX */
#define TBIEXTCTX_AX2           (0)
#define TBIEXTCTX_Ctx           (0)
#define TBIEXTCTX_Ctx_SaveMask  (TBIEXTCTX_Ctx + 2)
#define TBIEXTCTX_Ctx_pExt      (TBIEXTCTX_Ctx + 2 + 2)

/* Extended context data size calculation constants */
#define TBICTXEXT_BYTES          (8)
#define TBICTXEXTBB8_BYTES     (8*8)
#define TBICTXEXTAX3_BYTES     (3*8)
#define TBICTXEXTAX4_BYTES     (4*8)
#ifdef METAC_1_0
#define TBICTXEXTAXX_BYTES     TBICTXEXTAX3_BYTES
#else
#define TBICTXEXTAXX_BYTES     TBICTXEXTAX4_BYTES
#endif
#define TBICTXEXTHL2_BYTES     (3*8)
#define TBICTXEXTTDR_BYTES    (27*8)
#define TBICTXEXTTDP_BYTES TBICTXEXTTDR_BYTES

#ifdef TBI_1_4
#define TBICTXEXTFX8_BYTES	(4*8)
#define TBICTXEXTFPAC_BYTES	(1*4 + 2*2 + 4*8)
#define TBICTXEXTFACF_BYTES	(3*8)
#endif

/* Maximum flag bits to be set via the TBICTX_EXTSET macro */
#define TBICTXEXT_MAXBITS  (TBICTX_XEXT_BIT|                \
                            TBICTX_XDX8_BIT|TBICTX_XAXX_BIT|\
                            TBICTX_XHL2_BIT|TBICTX_XTDP_BIT )

/* Maximum size of the extended context save area for current variant */
#define TBICTXEXT_MAXBYTES (TBICTXEXT_BYTES+TBICTXEXTBB8_BYTES+\
                         TBICTXEXTAXX_BYTES+TBICTXEXTHL2_BYTES+\
                                            TBICTXEXTTDP_BYTES )

#ifdef TBI_FASTINT_1_4
/* Maximum flag bits to be set via the TBICTX_EXTSET macro */
#define TBICTX2EXT_MAXBITS (TBICTX_XDX8_BIT|TBICTX_XAXX_BIT|\
                            TBICTX_XHL2_BIT|TBICTX_XTDP_BIT )

/* Maximum size of the extended context save area for current variant */
#define TBICTX2EXT_MAXBYTES (TBICTXEXTBB8_BYTES+TBICTXEXTAXX_BYTES\
                             +TBICTXEXTHL2_BYTES+TBICTXEXTTDP_BYTES )
#endif

/* Specify extended resources being used by current routine, code must be
   assembler generated to utilise extended resources-

        MOV     D0xxx,A0StP             ; Perform alloca - routine should
        ADD     A0StP,A0StP,#SaveSize   ; setup/use A0FrP to access locals
        MOVT    D1xxx,#SaveMask         ; TBICTX_XEXT_BIT MUST be set
        SETL    [A1GbP+#OG(___TBIExtCtx)],D0xxx,D1xxx
        
    NB: OG(___TBIExtCtx) is a special case supported for SETL/GETL operations
        on 64-bit sizes structures only, other accesses must be based on use
        of OGA(___TBIExtCtx). 

   At exit of routine-
   
        MOV     D0xxx,#0                ; Clear extended context save state
        MOV     D1xxx,#0
        SETL    [A1GbP+#OG(___TBIExtCtx)],D0xxx,D1xxx
        SUB     A0StP,A0StP,#SaveSize   ; If original A0StP required
        
    NB: Both the setting and clearing of the whole __TBIExtCtx MUST be done
        atomically in one 64-bit write operation.

   For simple interrupt handling only via __TBINestInts there should be no
   impact of the __TBIExtCtx system. If pre-emptive scheduling is being
   performed however (assuming __TBINestInts has already been called earlier
   on) then the following logic will correctly call __TBICtxSave if required
   and clear out the currently selected background task-
   
        if ( __TBIExtCtx.Ctx.SaveMask & TBICTX_XEXT_BIT )
        {
            / * Store extended states in pCtx * /
            State.Sig.SaveMask |= __TBIExtCtx.Ctx.SaveMask;
        
            (void) __TBICtxSave( State, (void *) __TBIExtCtx.Ctx.pExt );
            __TBIExtCtx.Val   = 0;
        }
        
    and when restoring task states call __TBICtxRestore-
    
        / * Restore state from pCtx * /
        State.Sig.pCtx     = pCtx;
        State.Sig.SaveMask = pCtx->SaveMask;

        if ( State.Sig.SaveMask & TBICTX_XEXT_BIT )
        {
            / * Restore extended states from pCtx * /
            __TBIExtCtx.Val = pCtx->Ext.Val;
            
            (void) __TBICtxRestore( State, (void *) __TBIExtCtx.Ctx.pExt );
        }   
   
 */

/* Critical thread state save area */
#ifndef __ASSEMBLY__
typedef struct _tbictx_tag_ {
    /* TXSTATUS_FLAG_BITS and TXSTATUS_LSM_STEP_BITS from TXSTATUS */
    short Flags;
    /* Mask indicates any extended context state saved; 0 -> Never run */
    short SaveMask;
    /* Saved PC value */
    int CurrPC;
    /* Saved critical register states */
    TBIDUAL DX[8];
    /* Background control register states - for cores without catch buffer
       base in DIVTIME the TXSTATUS bits RPVALID and RPMASK are stored with
       the real state TXDIVTIME in CurrDIVTIME */
    int CurrRPT, CurrBPOBITS, CurrMODE, CurrDIVTIME;
    /* Saved AX register states */
    TBIDUAL AX[2];
    TBIEXTCTX Ext;
    TBIDUAL AX3[TBICTX_AX_REGS-3];
    
    /* Any CBUF state to be restored by a handler return must be stored here.
       Other extended state can be stored anywhere - see __TBICtxSave and
       __TBICtxRestore. */
    
} TBICTX, *PTBICTX;

#ifdef TBI_FASTINT_1_4
typedef struct _tbictx2_tag_ {
    TBIDUAL AX[2];    /* AU.0, AU.1 */
    TBIDUAL DX[2];    /* DU.0, DU.4 */
    int     CurrMODE;
    int     CurrRPT;
    int     CurrSTATUS;
    void   *CurrPC;   /* PC in PC address space */
} TBICTX2, *PTBICTX2;
/* TBICTX2 is followed by:
 *   TBICTXEXTCB0                if TXSTATUS.CBMarker
 *   TBIDUAL * TXSTATUS.IRPCount if TXSTATUS.IRPCount > 0
 *   TBICTXGP                    if using __TBIStdRootIntHandler or __TBIStdCtxSwitchRootIntHandler
 */

typedef struct _tbictxgp_tag_ {
    short    DspramSizes;
    short    SaveMask;
    void    *pExt;
    TBIDUAL  DX[6]; /* DU.1-DU.3, DU.5-DU.7 */
    TBIDUAL  AX[2]; /* AU.2-AU.3 */
} TBICTXGP, *PTBICTXGP;

#define TBICTXGP_DspramSizes (0)
#define TBICTXGP_SaveMask    (TBICTXGP_DspramSizes + 2)
#define TBICTXGP_MAX_BYTES   (2 + 2 + 4 + 8*(6+2))

#endif
#endif /* ifndef __ASSEMBLY__ */

/* Byte offsets of fields within TBICTX */
#define TBICTX_Flags            (0)
#define TBICTX_SaveMask         (2)
#define TBICTX_CurrPC           (4)
#define TBICTX_DX               (2 + 2 + 4)
#define TBICTX_CurrRPT          (2 + 2 + 4 + 8 * 8)
#define TBICTX_CurrMODE         (2 + 2 + 4 + 8 * 8 + 4 + 4)
#define TBICTX_AX               (2 + 2 + 4 + 8 * 8 + 4 + 4 + 4 + 4)
#define TBICTX_Ext              (2 + 2 + 4 + 8 * 8 + 4 + 4 + 4 + 4 + 2 * 8)
#define TBICTX_Ext_AX2          (TBICTX_Ext + TBIEXTCTX_AX2)
#define TBICTX_Ext_AX2_U0       (TBICTX_Ext + TBIEXTCTX_AX2 + TBIDUAL_U0)
#define TBICTX_Ext_AX2_U1       (TBICTX_Ext + TBIEXTCTX_AX2 + TBIDUAL_U1)
#define TBICTX_Ext_Ctx_pExt     (TBICTX_Ext + TBIEXTCTX_Ctx_pExt)
#define TBICTX_Ext_Ctx_SaveMask (TBICTX_Ext + TBIEXTCTX_Ctx_SaveMask)

#ifdef TBI_FASTINT_1_4
#define TBICTX2_BYTES (8 * 2 + 8 * 2 + 4 + 4 + 4 + 4)
#define TBICTXEXTCB0_BYTES (4 + 4 + 8)

#define TBICTX2_CRIT_MAX_BYTES (TBICTX2_BYTES + TBICTXEXTCB0_BYTES + 6 * TBIDUAL_BYTES)
#define TBI_SWITCH_NEXT_PC(PC, EXTRA) ((PC) + (EXTRA & 1) ? 8 : 4)
#endif

#ifndef __ASSEMBLY__
/* Extended thread state save areas - catch buffer state element */
typedef struct _tbictxextcb0_tag_ {
    /* Flags data and address value - see METAC_CATCH_VALUES in machine.h */
    unsigned long CBFlags, CBAddr;
    /* 64-bit data */
    TBIDUAL CBData;
    
} TBICTXEXTCB0, *PTBICTXEXTCB0;

/* Read pipeline state saved on later cores after single catch buffer slot */
typedef struct _tbictxextrp6_tag_ {
    /* RPMask is TXSTATUS_RPMASK_BITS only, reserved is undefined */
    unsigned long RPMask, Reserved0;
    TBIDUAL CBData[6];
    
} TBICTXEXTRP6, *PTBICTXEXTRP6;

/* Extended thread state save areas - 8 DU register pairs */
typedef struct _tbictxextbb8_tag_ {
    /* Remaining Data unit registers in 64-bit pairs */
    TBIDUAL UX[8];
    
} TBICTXEXTBB8, *PTBICTXEXTBB8;

/* Extended thread state save areas - 3 AU register pairs */
typedef struct _tbictxextbb3_tag_ {
    /* Remaining Address unit registers in 64-bit pairs */
    TBIDUAL UX[3];
    
} TBICTXEXTBB3, *PTBICTXEXTBB3;

/* Extended thread state save areas - 4 AU register pairs or 4 FX pairs */
typedef struct _tbictxextbb4_tag_ {
    /* Remaining Address unit or FPU registers in 64-bit pairs */
    TBIDUAL UX[4];
    
} TBICTXEXTBB4, *PTBICTXEXTBB4;

/* Extended thread state save areas - Hardware loop states (max 2) */
typedef struct _tbictxexthl2_tag_ {
    /* Hardware looping register states */
    TBIDUAL Start, End, Count;
    
} TBICTXEXTHL2, *PTBICTXEXTHL2;

/* Extended thread state save areas - DSP register states */
typedef struct _tbictxexttdp_tag_ {
    /* DSP 32-bit accumulator register state (Bits 31:0 of ACX.0) */
    TBIDUAL Acc32[1];
    /* DSP > 32-bit accumulator bits 63:32 of ACX.0 (zero-extended) */
    TBIDUAL Acc64[1];
    /* Twiddle register state, and three phase increment states */
    TBIDUAL PReg[4];
    /* Modulo region size, padded to 64-bits */
    int CurrMRSIZE, Reserved0;
    
} TBICTXEXTTDP, *PTBICTXEXTTDP;

/* Extended thread state save areas - DSP register states including DSP RAM */
typedef struct _tbictxexttdpr_tag_ {
    /* DSP 32-bit accumulator register state (Bits 31:0 of ACX.0) */
    TBIDUAL Acc32[1];
    /* DSP 40-bit accumulator register state (Bits 39:8 of ACX.0) */
    TBIDUAL Acc40[1];
    /* DSP RAM Pointers */
    TBIDUAL RP0[2],  WP0[2],  RP1[2],  WP1[2];
    /* DSP RAM Increments */
    TBIDUAL RPI0[2], WPI0[2], RPI1[2], WPI1[2];
    /* Template registers */
    unsigned long Tmplt[16];
    /* Modulo address region size and DSP RAM module region sizes */
    int CurrMRSIZE, CurrDRSIZE;
    
} TBICTXEXTTDPR, *PTBICTXEXTTDPR;

#ifdef TBI_1_4
/* The METAC_ID_CORE register state is a marker for the FPU
   state that is then stored after this core header structure.  */
#define TBICTXEXTFPU_CONFIG_MASK  ( (METAC_COREID_NOFPACC_BIT+     \
                                     METAC_COREID_CFGFPU_BITS ) << \
                                     METAC_COREID_CONFIG_BITS       )

/* Recorded FPU exception state from TXDEFR in DefrFpu */
#define TBICTXEXTFPU_DEFRFPU_MASK (TXDEFR_FPU_FE_BITS)

/* Extended thread state save areas - FPU register states */
typedef struct _tbictxextfpu_tag_ {
    /* Stored METAC_CORE_ID CONFIG */
    int CfgFpu;
    /* Stored deferred TXDEFR bits related to FPU
     *
     * This is encoded as follows in order to fit into 16-bits:
     * DefrFPU:15 - 14 <= 0
     *        :13 -  8 <= TXDEFR:21-16
     *        : 7 -  6 <= 0
     *        : 5 -  0 <= TXDEFR:5-0
     */
    short DefrFpu;

    /* TXMODE bits related to FPU */
    short ModeFpu;
    
    /* FPU Even/Odd register states */
    TBIDUAL FX[4];
   
    /* if CfgFpu & TBICTX_CFGFPU_FX16_BIT  -> 1 then TBICTXEXTBB4 holds FX.8-15 */
    /* if CfgFpu & TBICTX_CFGFPU_NOACF_BIT -> 0 then TBICTXEXTFPACC holds state */
} TBICTXEXTFPU, *PTBICTXEXTFPU;

/* Extended thread state save areas - FPU accumulator state */
typedef struct _tbictxextfpacc_tag_ {
    /* FPU accumulator register state - three 64-bit parts */
    TBIDUAL FAcc32[3];
    
} TBICTXEXTFPACC, *PTBICTXEXTFPACC;
#endif

/* Prototype TBI structure */
struct _tbi_tag_ ;

/* A 64-bit return value used commonly in the TBI APIs */
typedef union _tbires_tag_ {
    /* Save and load this value to get/set the whole result quickly */
    long long Val;

    /* Parameter of a fnSigs or __TBICtx* call */
    struct _tbires_sig_tag_ { 
        /* TXMASK[I] bits zeroed upto and including current trigger level */
        unsigned short TrigMask;
        /* Control bits for handlers - see PTBIAPIFN documentation below */
        unsigned short SaveMask;
        /* Pointer to the base register context save area of the thread */
        PTBICTX pCtx;
    } Sig;

    /* Result of TBIThrdPrivId call */
    struct _tbires_thrdprivid_tag_ {
        /* Basic thread identifier; just TBID_THREAD_BITS */
        int Id;
        /* None thread number bits; TBID_ISTAT_BIT+TBID_PSTAT_BIT */
        int Priv;
    } Thrd;

    /* Parameter and Result of a __TBISwitch call */
    struct _tbires_switch_tag_ { 
        /* Parameter passed across context switch */
        void *pPara;
        /* Thread context of other Thread includng restore flags */
        PTBICTX pCtx;
    } Switch;
    
    /* For extended S/W events only */
    struct _tbires_ccb_tag_ {
        void *pCCB;
        int COff;
    } CCB;

    struct _tbires_tlb_tag_ {
        int Leaf;  /* TLB Leaf data */
        int Flags; /* TLB Flags */
    } Tlb;

#ifdef TBI_FASTINT_1_4
    struct _tbires_intr_tag_ {
      short    TrigMask;
      short    SaveMask;
      PTBICTX2 pCtx;
    } Intr;
#endif

} TBIRES, *PTBIRES;
#endif /* ifndef __ASSEMBLY__ */

#ifndef __ASSEMBLY__
/* Prototype for all signal handler functions, called via ___TBISyncTrigger or
   ___TBIASyncTrigger.
   
   State.Sig.TrigMask will indicate the bits set within TXMASKI at
          the time of the handler call that have all been cleared to prevent
          nested interrupt occuring immediately.
   
   State.Sig.SaveMask is a bit-mask which will be set to Zero when a trigger
          occurs at background level and TBICTX_CRIT_BIT and optionally
          TBICTX_CBUF_BIT when a trigger occurs at interrupt level.
          
          TBICTX_CBUF_BIT reflects the state of TXSTATUS_CBMARKER_BIT for
          the interrupted background thread.
   
   State.Sig.pCtx will point at a TBICTX structure generated to hold the
          critical state of the interrupted thread at interrupt level and
          should be set to NULL when called at background level.
        
   Triggers will indicate the status of TXSTAT or TXSTATI sampled by the
          code that called the handler.
          
   Inst is defined as 'Inst' if the SigNum is TBID_SIGNUM_SWx and holds the
          actual SWITCH instruction detected, in other cases the value of this
          parameter is undefined.
   
   pTBI   points at the PTBI structure related to the thread and processing
          level involved.

   TBIRES return value at both processing levels is similar in terms of any
          changes that the handler makes. By default the State argument value
          passed in should be returned.
          
      Sig.TrigMask value is bits to OR back into TXMASKI when the handler
          completes to enable currently disabled interrupts.
          
      Sig.SaveMask value is ignored.
   
      Sig.pCtx is ignored.

 */
typedef TBIRES (*PTBIAPIFN)( TBIRES State, int SigNum,
                             int Triggers, int Inst,
                             volatile struct _tbi_tag_ *pTBI );
#endif /* ifndef __ASSEMBLY__ */

#ifndef __ASSEMBLY__
/* The global memory map is described by a list of segment descriptors */
typedef volatile struct _tbiseg_tag_ {
    volatile struct _tbiseg_tag_ *pLink;
    int Id;                           /* Id of the segment */
    TBISPIN Lock;                     /* Spin-lock for struct (normally 0) */
    unsigned int Bytes;               /* Size of region in bytes */
    void *pGAddr;                     /* Base addr of region in global space */
    void *pLAddr;                     /* Base addr of region in local space */
    int Data[2];                      /* Segment specific data (may be extended) */

} TBISEG, *PTBISEG;
#endif /* ifndef __ASSEMBLY__ */

/* Offsets of fields in TBISEG structure */
#define TBISEG_pLink    ( 0)
#define TBISEG_Id       ( 4)
#define TBISEG_Lock     ( 8)
#define TBISEG_Bytes    (12)
#define TBISEG_pGAddr   (16)
#define TBISEG_pLAddr   (20)
#define TBISEG_Data     (24)

#ifndef __ASSEMBLY__
typedef volatile struct _tbi_tag_ {
    int SigMask;                      /* Bits set to represent S/W events */
    PTBIKICK pKick;                   /* Kick addr for S/W events */
    void *pCCB;                       /* Extended S/W events */
    PTBISEG pSeg;                     /* Related segment structure */
    PTBIAPIFN fnSigs[TBID_SIGNUM_MAX+1];/* Signal handler API table */
} *PTBI, TBI;
#endif /* ifndef __ASSEMBLY__ */

/* Byte offsets of fields within TBI */
#define TBI_SigMask     (0)
#define TBI_pKick       (4)
#define TBI_pCCB        (8)
#define TBI_pSeg       (12)
#define TBI_fnSigs     (16)

#ifdef TBI_1_4
#ifndef __ASSEMBLY__
/* This handler should be used for TBID_SIGNUM_DFR */
extern TBIRES __TBIHandleDFR ( TBIRES State, int SigNum,
                               int Triggers, int Inst,
                               volatile struct _tbi_tag_ *pTBI );
#endif
#endif

/* String table entry - special values */
#define METAG_TBI_STRS (0x5300) /* Tag      : If entry is valid */
#define METAG_TBI_STRE (0x4500) /* Tag      : If entry is end of table */
#define METAG_TBI_STRG (0x4700) /* Tag      : If entry is a gap */
#define METAG_TBI_STRX (0x5A00) /* TransLen : If no translation present */

#ifndef __ASSEMBLY__
typedef volatile struct _tbistr_tag_ {
    short Bytes;                      /* Length of entry in Bytes */
    short Tag;                        /* Normally METAG_TBI_STRS(0x5300) */
    short Len;                        /* Length of the string entry (incl null) */
    short TransLen;                   /* Normally METAG_TBI_STRX(0x5A00) */
    char String[8];                   /* Zero terminated (may-be bigger) */

} TBISTR, *PTBISTR;
#endif /* ifndef __ASSEMBLY__ */

/* Cache size information - available as fields of Data[1] of global heap
   segment */
#define METAG_TBI_ICACHE_SIZE_S    0             /* see comments below */
#define METAG_TBI_ICACHE_SIZE_BITS 0x0000000F
#define METAG_TBI_ICACHE_FILL_S    4
#define METAG_TBI_ICACHE_FILL_BITS 0x000000F0
#define METAG_TBI_DCACHE_SIZE_S    8
#define METAG_TBI_DCACHE_SIZE_BITS 0x00000F00
#define METAG_TBI_DCACHE_FILL_S    12
#define METAG_TBI_DCACHE_FILL_BITS 0x0000F000

/* METAG_TBI_xCACHE_SIZE
   Describes the physical cache size rounded up to the next power of 2
   relative to a 16K (2^14) cache. These sizes are encoded as a signed addend
   to this base power of 2, for example
      4K -> 2^12 -> -2  (i.e. 12-14)
      8K -> 2^13 -> -1
     16K -> 2^14 ->  0
     32K -> 2^15 -> +1
     64K -> 2^16 -> +2
    128K -> 2^17 -> +3

   METAG_TBI_xCACHE_FILL
   Describes the physical cache size within the power of 2 area given by
   the value above. For example a 10K cache may be represented as having
   nearest size 16K with a fill of 10 sixteenths. This is encoded as the
   number of unused 1/16ths, for example
     0000 ->  0 -> 16/16
     0001 ->  1 -> 15/16
     0010 ->  2 -> 14/16
     ...
     1111 -> 15 ->  1/16
 */

#define METAG_TBI_CACHE_SIZE_BASE_LOG2 14

/* Each declaration made by this macro generates a TBISTR entry */
#ifndef __ASSEMBLY__
#define TBISTR_DECL( Name, Str )                                       \
    __attribute__ ((__section__ (".tbistr") )) const char Name[] = #Str
#endif

/* META timer values - see below for Timer support routines */
#define TBI_TIMERWAIT_MIN (-16)         /* Minimum 'recommended' period */
#define TBI_TIMERWAIT_MAX (-0x7FFFFFFF) /* Maximum 'recommended' period */

#ifndef __ASSEMBLY__
/* These macros allow direct access from C to any register known to the
   assembler or defined in machine.h. Example candidates are TXTACTCYC,
   TXIDLECYC, and TXPRIVEXT. Note that when higher level macros and routines
   like the timer and trigger handling features below these should be used in
   preference to this direct low-level access mechanism. */
#define TBI_GETREG( Reg )                                  __extension__ ({\
   int __GRValue;                                                          \
   __asm__ volatile ("MOV\t%0," #Reg "\t/* (*TBI_GETREG OK) */" :          \
                     "=r" (__GRValue) );                                   \
    __GRValue;                                                            })

#define TBI_SETREG( Reg, Value )                                       do {\
   int __SRValue = Value;                                                  \
   __asm__ volatile ("MOV\t" #Reg ",%0\t/* (*TBI_SETREG OK) */" :          \
                     : "r" (__SRValue) );                       } while (0)

#define TBI_SWAPREG( Reg, Value )                                      do {\
   int __XRValue = (Value);                                                \
   __asm__ volatile ("SWAP\t" #Reg ",%0\t/* (*TBI_SWAPREG OK) */" :        \
                     "=r" (__XRValue) : "0" (__XRValue) );                 \
   Value = __XRValue;                                           } while (0)

/* Obtain and/or release global critical section lock given that interrupts
   are already disabled and/or should remain disabled. */
#define TBI_NOINTSCRITON                                             do {\
   __asm__ volatile ("LOCK1\t\t/* (*TBI_NOINTSCRITON OK) */");} while (0)
#define TBI_NOINTSCRITOFF                                             do {\
   __asm__ volatile ("LOCK0\t\t/* (*TBI_NOINTSCRITOFF OK) */");} while (0)
/* Optimised in-lining versions of the above macros */

#define TBI_LOCK( TrigState )                                          do {\
   int __TRValue;                                                          \
   int __ALOCKHI = LINSYSEVENT_WR_ATOMIC_LOCK & 0xFFFF0000;                \
   __asm__ volatile ("MOV %0,#0\t\t/* (*TBI_LOCK ... */\n\t"               \
                     "SWAP\t%0,TXMASKI\t/* ... */\n\t"                     \
                     "LOCK2\t\t/* ... */\n\t"                              \
                     "SETD\t[%1+#0x40],D1RtP /* ... OK) */" :              \
                     "=r&" (__TRValue) : "u" (__ALOCKHI) );                \
   TrigState = __TRValue;                                       } while (0)
#define TBI_CRITON( TrigState )                                        do {\
   int __TRValue;                                                          \
   __asm__ volatile ("MOV %0,#0\t\t/* (*TBI_CRITON ... */\n\t"             \
                     "SWAP\t%0,TXMASKI\t/* ... */\n\t"                     \
                     "LOCK1\t\t/* ... OK) */" :                            \
                     "=r" (__TRValue) );                                   \
   TrigState = __TRValue;                                       } while (0)

#define TBI_INTSX( TrigState )                                         do {\
   int __TRValue = TrigState;                                              \
   __asm__ volatile ("SWAP\t%0,TXMASKI\t/* (*TBI_INTSX OK) */" :           \
                     "=r" (__TRValue) : "0" (__TRValue) );                 \
   TrigState = __TRValue;                                       } while (0)

#define TBI_UNLOCK( TrigState )                                        do {\
   int __TRValue = TrigState;                                              \
   int __ALOCKHI = LINSYSEVENT_WR_ATOMIC_LOCK & 0xFFFF0000;                \
   __asm__ volatile ("SETD\t[%1+#0x00],D1RtP\t/* (*TBI_UNLOCK ... */\n\t"  \
                     "LOCK0\t\t/* ... */\n\t"                              \
                     "MOV\tTXMASKI,%0\t/* ... OK) */" :                    \
                     : "r" (__TRValue), "u" (__ALOCKHI) );      } while (0)

#define TBI_CRITOFF( TrigState )                                       do {\
   int __TRValue = TrigState;                                              \
   __asm__ volatile ("LOCK0\t\t/* (*TBI_CRITOFF ... */\n\t"                \
                     "MOV\tTXMASKI,%0\t/* ... OK) */" :                    \
                     : "r" (__TRValue) );                       } while (0)

#define TBI_TRIGSX( SrcDst ) do { TBI_SWAPREG( TXMASK, SrcDst );} while (0)

/* Composite macros to perform logic ops on INTS or TRIGS masks */
#define TBI_INTSOR( Bits )                                              do {\
    int __TT = 0; TBI_INTSX(__TT);                                          \
    __TT |= (Bits); TBI_INTSX(__TT);                             } while (0)
    
#define TBI_INTSAND( Bits )                                             do {\
    int __TT = 0; TBI_INTSX(__TT);                                          \
    __TT &= (Bits); TBI_INTSX(__TT);                             } while (0)

#ifdef TBI_1_4
#define TBI_DEFRICTRLSOR( Bits )                                        do {\
    int __TT = TBI_GETREG( CT.20 );                                         \
    __TT |= (Bits); TBI_SETREG( CT.20, __TT);                    } while (0)
    
#define TBI_DEFRICTRLSAND( Bits )                                       do {\
    int __TT = TBI_GETREG( TXDEFR );                                        \
    __TT &= (Bits); TBI_SETREG( CT.20, __TT);                    } while (0)
#endif

#define TBI_TRIGSOR( Bits )                                             do {\
    int __TT = TBI_GETREG( TXMASK );                                        \
    __TT |= (Bits); TBI_SETREG( TXMASK, __TT);                   } while (0)
    
#define TBI_TRIGSAND( Bits )                                            do {\
    int __TT = TBI_GETREG( TXMASK );                                        \
    __TT &= (Bits); TBI_SETREG( TXMASK, __TT);                   } while (0)

/* Macros to disable and re-enable interrupts using TBI_INTSX, deliberate
   traps and exceptions can still be handled within the critical section. */
#define TBI_STOPINTS( Value )                                           do {\
    int __TT = TBI_GETREG( TXMASKI );                                       \
    __TT &= TXSTATI_BGNDHALT_BIT; TBI_INTSX( __TT );                        \
    Value = __TT;                                                } while (0)
#define TBI_RESTINTS( Value )                                           do {\
    int __TT = Value; TBI_INTSX( __TT );                         } while (0)

/* Return pointer to segment list at current privilege level */
PTBISEG __TBISegList( void );

/* Search the segment list for a match given Id, pStart can be NULL */
PTBISEG __TBIFindSeg( PTBISEG pStart, int Id );

/* Prepare a new segment structure using space from within another */
PTBISEG __TBINewSeg( PTBISEG pFromSeg, int Id, unsigned int Bytes );

/* Prepare a new segment using any global or local heap segments available */
PTBISEG __TBIMakeNewSeg( int Id, unsigned int Bytes );

/* Insert a new segment into the segment list so __TBIFindSeg can locate it */
void __TBIAddSeg( PTBISEG pSeg );
#define __TBIADDSEG_DEF     /* Some versions failed to define this */

/* Return Id of current thread; TBID_ISTAT_BIT+TBID_THREAD_BITS */
int __TBIThreadId( void );

/* Return TBIRES.Thrd data for current thread */
TBIRES __TBIThrdPrivId( void );

/* Return pointer to current threads TBI root block.
   Id implies whether Int or Background root block is required */
PTBI __TBI( int Id );

/* Try to set Mask bit using the spin-lock protocol, return 0 if fails and 
   new state if succeeds */
int __TBIPoll( PTBISPIN pLock, int Mask );

/* Set Mask bits via the spin-lock protocol in *pLock, return new state */
int __TBISpin( PTBISPIN pLock, int Mask );

/* Default handler set up for all TBI.fnSigs entries during initialisation */
TBIRES __TBIUnExpXXX( TBIRES State, int SigNum,
                   int Triggers, int Inst, PTBI pTBI );

/* Call this routine to service triggers at background processing level. The
   TBID_POLL_BIT of the Id parameter value will be used to indicate that the
   routine should return if no triggers need to be serviced initially. If this
   bit is not set the routine will block until one trigger handler is serviced
   and then behave like the poll case servicing any remaining triggers
   actually outstanding before returning. Normally the State parameter should
   be simply initialised to zero and the result should be ignored, other
   values/options are for internal use only. */
TBIRES __TBISyncTrigger( TBIRES State, int Id );

/* Call this routine to enable processing of triggers by signal handlers at
   interrupt level. The State parameter value passed is returned by this
   routine. The State.Sig.TrigMask field also specifies the initial
   state of the interrupt mask register TXMASKI to be setup by the call.
   The other parts of the State parameter are ignored unless the PRIV bit is
   set in the SaveMask field. In this case the State.Sig.pCtx field specifies
   the base of the stack to which the interrupt system should switch into
   as it saves the state of the previously executing code. In the case the
   thread will be unprivileged as it continues execution at the return
   point of this routine and it's future state will be effectively never
   trusted to be valid. */
TBIRES __TBIASyncTrigger( TBIRES State );

/* Call this to swap soft threads executing at the background processing level.
   The TBIRES returned to the new thread will be the same as the NextThread
   value specified to the call. The NextThread.Switch.pCtx value specifies
   which thread context to restore and the NextThread.Switch.Para value can
   hold an arbitrary expression to be passed between the threads. The saved
   state of the previous thread will be stored in a TBICTX descriptor created
   on it's stack and the address of this will be stored into the *rpSaveCtx
   location specified. */
TBIRES __TBISwitch( TBIRES NextThread, PTBICTX *rpSaveCtx );

/* Call this to initialise a stack frame ready for further use, up to four
   32-bit arguments may be specified after the fixed args to be passed via
   the new stack pStack to the routine specified via fnMain. If the
   main-line routine ever returns the thread will operate as if main itself
   had returned and terminate with the return code given. */
typedef int (*PTBIMAINFN)( TBIRES Arg /*, <= 4 additional 32-bit args */ );
PTBICTX __TBISwitchInit( void *pStack, PTBIMAINFN fnMain, ... );

/* Call this to resume a thread from a saved synchronous TBICTX state.
   The TBIRES returned to the new thread will be the same as the NextThread
   value specified to the call. The NextThread.Switch.pCtx value specifies
   which thread context to restore and the NextThread.Switch.Para value can
   hold an arbitrary expression to be passed between the threads. The context
   of the calling thread is lost and this routine never returns to the
   caller. The TrigsMask value supplied is ored into TXMASKI to enable
   interrupts after the context of the new thread is established. */
void __TBISyncResume( TBIRES NextThread, int TrigsMask );

/* Call these routines to save and restore the extended states of
   scheduled tasks. */
void *__TBICtxSave( TBIRES State, void *pExt );
void *__TBICtxRestore( TBIRES State, void *pExt );

#ifdef TBI_1_4
#ifdef TBI_FASTINT_1_4
/* Call these routines to copy the GP state to a separate buffer
 * Only necessary for context switching.
 */
PTBICTXGP __TBICtx2SaveCrit( PTBICTX2 pCurrentCtx, PTBICTX2 pSaveCtx );
void *__TBICtx2SaveGP( PTBICTXGP pCurrentCtxGP, PTBICTXGP pSaveCtxGP );

/* Call these routines to save and restore the extended states of
   scheduled tasks. */
void *__TBICtx2Save( PTBICTXGP pCtxGP, short SaveMask, void *pExt );
void *__TBICtx2Restore( PTBICTX2 pCtx, short SaveMask, void *pExt );
#endif

/* If FPAC flag is set then significant FPU context exists. Call these routine
   to save and restore it */
void *__TBICtxFPUSave( TBIRES State, void *pExt );
void *__TBICtxFPURestore( TBIRES State, void *pExt );

#ifdef TBI_FASTINT_1_4
extern void *__TBICtx2FPUSave (PTBICTXGP, short, void*);
extern void *__TBICtx2FPURestore (PTBICTXGP, short, void*);
#endif
#endif

#ifdef TBI_1_4
/* Call these routines to save and restore DSPRAM. */
void *__TBIDspramSaveA (short DspramSizes, void *pExt);
void *__TBIDspramSaveB (short DspramSizes, void *pExt);
void *__TBIDspramRestoreA (short DspramSizes, void *pExt);
void *__TBIDspramRestoreB (short DspramSizes, void *pExt);
#endif

/* This routine should be used at the entrypoint of interrupt handlers to
   re-enable higher priority interrupts and/or save state from the previously
   executing background code. State is a TBIRES.Sig parameter with NoNestMask
   indicating the triggers (if any) that should remain disabled and SaveMask
   CBUF bit indicating the if the hardware catch buffer is dirty. Optionally
   any number of extended state bits X??? including XCBF can be specified to
   force a nested state save call to __TBICtxSave before the current routine
   continues. (In the latter case __TBICtxRestore should be called to restore
   any extended states before the background thread of execution is resumed) 
   
   By default (no X??? bits specified in SaveMask) this routine performs a
   sub-call to __TBICtxSave with the pExt and State parameters specified IF
   some triggers could be serviced while the current interrupt handler
   executes and the hardware catch buffer is actually dirty. In this case
   this routine provides the XCBF bit in State.Sig.SaveMask to force the
   __TBICtxSave to extract the current catch state.
   
   The NoNestMask parameter should normally indicate that the same or lower
   triggers than those provoking the current handler call should not be
   serviced in nested calls, zero may be specified if all possible interrupts
   are to be allowed.
   
   The TBIRES.Sig value returned will be similar to the State parameter
   specified with the XCBF bit ORed into it's SaveMask if a context save was
   required and fewer bits set in it's TrigMask corresponding to the same/lower
   priority interrupt triggers still not enabled. */
TBIRES __TBINestInts( TBIRES State, void *pExt, int NoNestMask );

/* This routine causes the TBICTX structure specified in State.Sig.pCtx to
   be restored. This implies that execution will not return to the caller.
   The State.Sig.TrigMask field will be restored during the context switch
   such that any immediately occuring interrupts occur in the context of the
   newly specified task. The State.Sig.SaveMask parameter is ignored. */
void __TBIASyncResume( TBIRES State );

/* Call this routine to enable fastest possible processing of one or more
   interrupt triggers via a unified signal handler. The handler concerned
   must simple return after servicing the related hardware.
   The State.Sig.TrigMask parameter indicates the interrupt triggers to be
   enabled and the Thin.Thin.fnHandler specifies the routine to call and
   the whole Thin parameter value will be passed to this routine unaltered as
   it's first parameter. */
void __TBIASyncThin( TBIRES State, TBIRES Thin );

/* Do this before performing your own direct spin-lock access - use TBI_LOCK */
int __TBILock( void );

/* Do this after performing your own direct spin-lock access - use TBI_UNLOCK */
void __TBIUnlock( int TrigState );

/* Obtain and release global critical section lock - only stops execution
   of interrupts on this thread and similar critical section code on other
   local threads - use TBI_CRITON or TBI_CRITOFF */
int __TBICritOn( void );
void __TBICritOff( int TrigState );

/* Change INTS (TXMASKI) - return old state - use TBI_INTSX */
int __TBIIntsX( int NewMask );

/* Change TRIGS (TXMASK) - return old state - use TBI_TRIGSX */
int __TBITrigsX( int NewMask );

/* This function initialises a timer for first use, only the TBID_ISTAT_BIT
   of the Id parameter is used to indicate which timer is to be modified. The
   Wait value should either be zero to disable the timer concerned or be in
   the recommended TBI_TIMERWAIT_* range to specify the delay required before
   the first timer trigger occurs.
      
   The TBID_ISTAT_BIT of the Id parameter similar effects all other timer
   support functions (see below). */
void __TBITimerCtrl( int Id, int Wait );

/* This routine returns a 64-bit time stamp value that is initialised to zero
   via a __TBITimerCtrl timer enabling call. */
long long __TBITimeStamp( int Id );

/* To manage a periodic timer each period elapsed should be subracted from
   the current timer value to attempt to set up the next timer trigger. The
   Wait parameter should be a value in the recommended TBI_TIMERWAIT_* range.
   The return value is the new aggregate value that the timer was updated to,
   if this is less than zero then a timer trigger is guaranteed to be
   generated after the number of ticks implied, if a positive result is
   returned either itterative or step-wise corrective action must be taken to
   resynchronise the timer and hence provoke a future timer trigger. */
int __TBITimerAdd( int Id, int Wait );

/* String table search function, pStart is first entry to check or NULL,
   pStr is string data to search for and MatchLen is either length of string
   to compare for an exact match or negative length to compare for partial
   match. */
const TBISTR *__TBIFindStr( const TBISTR *pStart,
                            const char *pStr, int MatchLen );

/* String table translate function, pStr is text to translate and Len is
   it's length. Value returned may not be a string pointer if the
   translation value is really some other type, 64-bit alignment of the return
   pointer is guaranteed so almost any type including a structure could be
   located with this routine. */ 
const void *__TBITransStr( const char *pStr, int Len );



/* Arbitrary physical memory access windows, use different Channels to avoid
   conflict/thrashing within a single piece of code. */
void *__TBIPhysAccess( int Channel, int PhysAddr, int Bytes );
void __TBIPhysRelease( int Channel, void *pLinAddr );

#ifdef METAC_1_0
/* Data cache function nullified because data cache is off */
#define TBIDCACHE_FLUSH( pAddr )
#define TBIDCACHE_PRELOAD( Type, pAddr ) ((Type) (pAddr))
#define TBIDCACHE_REFRESH( Type, pAddr ) ((Type) (pAddr))
#endif
#ifdef METAC_1_1
/* To flush a single cache line from the data cache using a linear address */
#define TBIDCACHE_FLUSH( pAddr )          ((volatile char *) \
                 (((unsigned int) (pAddr))>>LINSYSLFLUSH_S))[0] = 0

extern void * __builtin_dcache_preload (void *);

/* Try to ensure that the data at the address concerned is in the cache */
#define TBIDCACHE_PRELOAD( Type, Addr )                                    \
  ((Type) __builtin_dcache_preload ((void *)(Addr)))

extern void * __builtin_dcache_refresh (void *);

/* Flush any old version of data from address and re-load a new copy */
#define TBIDCACHE_REFRESH( Type, Addr )                   __extension__ ({ \
  Type __addr = (Type)(Addr);                                              \
  (void)__builtin_dcache_refresh ((void *)(((unsigned int)(__addr))>>6));  \
  __addr; })

#endif
#ifndef METAC_1_0
#ifndef METAC_1_1
/* Support for DCACHE builtin */
extern void __builtin_dcache_flush (void *);

/* To flush a single cache line from the data cache using a linear address */
#define TBIDCACHE_FLUSH( Addr )                                            \
  __builtin_dcache_flush ((void *)(Addr))

extern void * __builtin_dcache_preload (void *);

/* Try to ensure that the data at the address concerned is in the cache */
#define TBIDCACHE_PRELOAD( Type, Addr )                                    \
  ((Type) __builtin_dcache_preload ((void *)(Addr)))

extern void * __builtin_dcache_refresh (void *);

/* Flush any old version of data from address and re-load a new copy */
#define TBIDCACHE_REFRESH( Type, Addr )                                    \
  ((Type) __builtin_dcache_refresh ((void *)(Addr)))

#endif
#endif

/* Flush the MMCU cache */
#define TBIMCACHE_FLUSH() { ((volatile int *) LINSYSCFLUSH_MMCU)[0] = 0; }

#ifdef METAC_2_1
/* Obtain the MMU table entry for the specified address */
#define TBIMTABLE_LEAFDATA(ADDR) TBIXCACHE_RD((int)(ADDR) & (-1<<6))

#ifndef __ASSEMBLY__
/* Obtain the full MMU table entry for the specified address */
#define TBIMTABLE_DATA(ADDR) __extension__ ({ TBIRES __p;                     \
                                              __p.Val = TBIXCACHE_RL((int)(ADDR) & (-1<<6));   \
                                              __p; })
#endif
#endif

/* Combine a physical base address, and a linear address
 * Internal use only
 */
#define _TBIMTABLE_LIN2PHYS(PHYS, LIN, LMASK) (void*)(((int)(PHYS)&0xFFFFF000)\
                                               +((int)(LIN)&(LMASK)))

/* Convert a linear to a physical address */
#define TBIMTABLE_LIN2PHYS(LEAFDATA, ADDR)                                    \
          (((LEAFDATA) & CRLINPHY0_VAL_BIT)                                   \
              ? _TBIMTABLE_LIN2PHYS(LEAFDATA, ADDR, 0x00000FFF)               \
              : 0)

/* Debug support - using external debugger or host */
void __TBIDumpSegListEntries( void );
void __TBILogF( const char *pFmt, ... );
void __TBIAssert( const char *pFile, int LineNum, const char *pExp );
void __TBICont( const char *pMsg, ... ); /* TBIAssert -> 'wait for continue' */

/* Array of signal name data for debug messages */
extern const char __TBISigNames[];
#endif /* ifndef __ASSEMBLY__ */



/* Scale of sub-strings in the __TBISigNames string list */
#define TBI_SIGNAME_SCALE   4
#define TBI_SIGNAME_SCALE_S 2

#define TBI_1_3 

#ifdef TBI_1_3

#ifndef __ASSEMBLY__
#define TBIXCACHE_RD(ADDR)                                 __extension__ ({\
    void * __Addr = (void *)(ADDR);                                        \
    int __Data;                                                            \
    __asm__ volatile ( "CACHERD\t%0,[%1+#0]" :                             \
                       "=r" (__Data) : "r" (__Addr) );                     \
    __Data;                                                               })

#define TBIXCACHE_RL(ADDR)                                 __extension__ ({\
    void * __Addr = (void *)(ADDR);                                        \
    long long __Data;                                                      \
    __asm__ volatile ( "CACHERL\t%0,%t0,[%1+#0]" :                         \
                       "=d" (__Data) : "r" (__Addr) );                     \
    __Data;                                                               })

#define TBIXCACHE_WD(ADDR, DATA)                                      do {\
    void * __Addr = (void *)(ADDR);                                       \
    int __Data = DATA;                                                    \
    __asm__ volatile ( "CACHEWD\t[%0+#0],%1" :                            \
                       : "r" (__Addr), "r" (__Data) );          } while(0)

#define TBIXCACHE_WL(ADDR, DATA)                                      do {\
    void * __Addr = (void *)(ADDR);                                       \
    long long __Data = DATA;                                              \
    __asm__ volatile ( "CACHEWL\t[%0+#0],%1,%t1" :                        \
                       : "r" (__Addr), "r" (__Data) );          } while(0)

#ifdef TBI_4_0

#define TBICACHE_FLUSH_L1D_L2(ADDR)                                       \
  TBIXCACHE_WD(ADDR, CACHEW_FLUSH_L1D_L2)
#define TBICACHE_WRITEBACK_L1D_L2(ADDR)                                   \
  TBIXCACHE_WD(ADDR, CACHEW_WRITEBACK_L1D_L2)
#define TBICACHE_INVALIDATE_L1D(ADDR)                                     \
  TBIXCACHE_WD(ADDR, CACHEW_INVALIDATE_L1D)
#define TBICACHE_INVALIDATE_L1D_L2(ADDR)                                  \
  TBIXCACHE_WD(ADDR, CACHEW_INVALIDATE_L1D_L2)
#define TBICACHE_INVALIDATE_L1DTLB(ADDR)                                  \
  TBIXCACHE_WD(ADDR, CACHEW_INVALIDATE_L1DTLB)
#define TBICACHE_INVALIDATE_L1I(ADDR)                                     \
  TBIXCACHE_WD(ADDR, CACHEW_INVALIDATE_L1I)
#define TBICACHE_INVALIDATE_L1ITLB(ADDR)                                  \
  TBIXCACHE_WD(ADDR, CACHEW_INVALIDATE_L1ITLB)

#endif /* TBI_4_0 */
#endif /* ifndef __ASSEMBLY__ */

/* 
 * Calculate linear PC value from real PC and Minim mode control, the LSB of
 * the result returned indicates if address compression has occured.
 */
#ifndef __ASSEMBLY__
#define METAG_LINPC( PCVal )                                              (\
    ( (TBI_GETREG(TXPRIVEXT) & TXPRIVEXT_MINIMON_BIT) != 0 ) ?           ( \
        ( ((PCVal) & 0x00900000) == 0x00900000 ) ?                         \
          (((PCVal) & 0xFFE00000) + (((PCVal) & 0x001FFFFC)>>1) + 1) :     \
        ( ((PCVal) & 0x00800000) == 0x00000000 ) ?                         \
          (((PCVal) & 0xFF800000) + (((PCVal) & 0x007FFFFC)>>1) + 1) :     \
                                                             (PCVal)   )   \
                                                                 : (PCVal) )
#define METAG_LINPC_X2BIT 0x00000001       /* Make (Size>>1) if compressed */

/* Convert an arbitrary Linear address into a valid Minim PC or return 0 */
#define METAG_PCMINIM( LinVal )                                           (\
        (((LinVal) & 0x00980000) == 0x00880000) ?                          \
            (((LinVal) & 0xFFE00000) + (((LinVal) & 0x000FFFFE)<<1)) :     \
        (((LinVal) & 0x00C00000) == 0x00000000) ?                          \
            (((LinVal) & 0xFF800000) + (((LinVal) & 0x003FFFFE)<<1)) : 0   )

/* Reverse a METAG_LINPC conversion step to return the original PCVal */
#define METAG_PCLIN( LinVal )                              ( 0xFFFFFFFC & (\
        ( (LinVal & METAG_LINPC_X2BIT) != 0 ) ? METAG_PCMINIM( LinVal ) :  \
                                                               (LinVal)   ))

/*
 * Flush the MMCU Table cache privately for each thread. On cores that do not
 * support per-thread flushing it will flush all threads mapping data.
 */
#define TBIMCACHE_TFLUSH(Thread)                                   do {\
    ((volatile int *)( LINSYSCFLUSH_TxMMCU_BASE            +           \
                      (LINSYSCFLUSH_TxMMCU_STRIDE*(Thread)) ))[0] = 0; \
                                                             } while(0)

/*
 * To flush a single linear-matched cache line from the code cache. In
 * cases where Minim is possible the METAC_LINPC operation must be used
 * to pre-process the address being flushed.
 */
#define TBIICACHE_FLUSH( pAddr ) TBIXCACHE_WD (pAddr, CACHEW_ICACHE_BIT)

/* To flush a single linear-matched mapping from code/data MMU table cache */
#define TBIMCACHE_AFLUSH( pAddr, SegType )                                \
    TBIXCACHE_WD(pAddr, CACHEW_TLBFLUSH_BIT + (                           \
                 ((SegType) == TBID_SEGTYPE_TEXT) ? CACHEW_ICACHE_BIT : 0 ))

/*
 * To flush translation data corresponding to a range of addresses without
 * using TBITCACHE_FLUSH to flush all of this threads translation data. It
 * is necessary to know what stride (>= 4K) must be used to flush a specific
 * region.
 *
 * For example direct mapped regions use the maximum page size (512K) which may
 * mean that only one flush is needed to cover the sub-set of the direct
 * mapped area used since it was setup.
 *
 * The function returns the stride on which flushes should be performed.
 *
 * If 0 is returned then the region is not subject to MMU caching, if -1 is
 * returned then this indicates that only TBIMCACHE_TFLUSH can be used to
 * flush the region concerned rather than TBIMCACHE_AFLUSH which this
 * function is designed to support.
 */
int __TBIMMUCacheStride( const void *pStart, int Bytes );

/*
 * This function will use the above lower level functions to achieve a MMU
 * table data flush in an optimal a fashion as possible. On a system that
 * supports linear address based caching this function will also call the
 * code or data cache flush functions to maintain address/data coherency.
 *
 * SegType should be TBID_SEGTYPE_TEXT if the address range is for code or
 * any other value such as TBID_SEGTYPE_DATA for data. If an area is
 * used in both ways then call this function twice; once for each.
 */
void __TBIMMUCacheFlush( const void *pStart, int Bytes, int SegType );

/*
 * Cached Core mode setup and flush functions allow one code and one data
 * region of the corresponding global or local cache partion size to be
 * locked into the corresponding cache memory. This prevents normal LRU
 * logic discarding the code or data and avoids write-thru bandwidth in
 * data areas. Code mappings are selected by specifying TBID_SEGTYPE_TEXT
 * for SegType, otherwise data mappings are created.
 * 
 * Mode supplied should always contain the VALID bit and WINx selection data.
 * Data areas will be mapped read-only if the WRITE bit is not added.
 *
 * The address returned by the Opt function will either be the same as that
 * passed in (if optimisation cannot be supported) or the base of the new core
 * cached region in linear address space. The returned address must be passed
 * into the End function to remove the mapping when required. If a non-core
 * cached memory address is passed into it the End function has no effect.
 * Note that the region accessed MUST be flushed from the appropriate cache
 * before the End function is called to deliver correct operation.
 */
void *__TBICoreCacheOpt( const void *pStart, int Bytes, int SegType, int Mode );
void __TBICoreCacheEnd( const void *pOpt, int Bytes, int SegType );

/*
 * Optimise physical access channel and flush side effects before releasing
 * the channel. If pStart is NULL the whole region must be flushed and this is
 * done automatically by the channel release function if optimisation is
 * enabled. Flushing the specific region that may have been accessed before
 * release should optimises this process. On physically cached systems we do
 * not flush the code/data caches only the MMU table data needs flushing.
 */
void __TBIPhysOptim( int Channel, int IMode, int DMode );
void __TBIPhysFlush( int Channel, const void *pStart, int Bytes );
#endif
#endif /* ifdef TBI_1_3 */

#endif /* _ASM_METAG_TBX_H_ */
