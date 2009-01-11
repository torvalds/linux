
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#if !defined(__DEBUGLIB_H__)
#define __DEBUGLIB_H__
#include <stdarg.h>
/*
 * define global debug priorities
 */
#define DL_LOG  0x00000001 /* always worth mentioning */
#define DL_FTL  0x00000002 /* always sampled error    */
#define DL_ERR  0x00000004 /* any kind of error       */
#define DL_TRC  0x00000008 /* verbose information     */
#define DL_XLOG  0x00000010 /* old xlog info           */
#define DL_MXLOG 0x00000020 /* maestra xlog info    */
#define DL_FTL_MXLOG 0x00000021 /* fatal maestra xlog info */
#define DL_EVL  0x00000080 /* special NT eventlog msg */
#define DL_COMPAT (DL_MXLOG | DL_XLOG)
#define DL_PRIOR_MASK (DL_EVL | DL_COMPAT | DL_TRC | DL_ERR | DL_FTL | DL_LOG)
#define DLI_LOG  0x0100
#define DLI_FTL  0x0200
#define DLI_ERR  0x0300
#define DLI_TRC  0x0400
#define DLI_XLOG 0x0500
#define DLI_MXLOG 0x0600
#define DLI_FTL_MXLOG 0x0600
#define DLI_EVL  0x0800
/*
 * define OS (operating system interface) debuglevel
 */
#define DL_REG  0x00000100 /* init/query registry     */
#define DL_MEM  0x00000200 /* memory management       */
#define DL_SPL  0x00000400 /* event/spinlock handling */
#define DL_IRP  0x00000800 /* I/O request handling    */
#define DL_TIM  0x00001000 /* timer/watchdog handling */
#define DL_BLK  0x00002000 /* raw data block contents */
#define DL_OS_MASK (DL_BLK | DL_TIM | DL_IRP | DL_SPL | DL_MEM | DL_REG)
#define DLI_REG  0x0900
#define DLI_MEM  0x0A00
#define DLI_SPL  0x0B00
#define DLI_IRP  0x0C00
#define DLI_TIM  0x0D00
#define DLI_BLK  0x0E00
/*
 * define ISDN (connection interface) debuglevel
 */
#define DL_TAPI  0x00010000 /* debug TAPI interface    */
#define DL_NDIS  0x00020000 /* debug NDIS interface    */
#define DL_CONN  0x00040000 /* connection handling     */
#define DL_STAT  0x00080000 /* trace state machines    */
#define DL_SEND  0x00100000 /* trace raw xmitted data  */
#define DL_RECV  0x00200000 /* trace raw received data */
#define DL_DATA  (DL_SEND | DL_RECV)
#define DL_ISDN_MASK (DL_DATA | DL_STAT | DL_CONN | DL_NDIS | DL_TAPI)
#define DLI_TAPI 0x1100
#define DLI_NDIS 0x1200
#define DLI_CONN 0x1300
#define DLI_STAT 0x1400
#define DLI_SEND 0x1500
#define DLI_RECV 0x1600
/*
 * define some private (unspecified) debuglevel
 */
#define DL_PRV0  0x01000000
#define DL_PRV1  0x02000000
#define DL_PRV2  0x04000000
#define DL_PRV3  0x08000000
#define DL_PRIV_MASK (DL_PRV0 | DL_PRV1 | DL_PRV2 | DL_PRV3)
#define DLI_PRV0 0x1900
#define DLI_PRV1 0x1A00
#define DLI_PRV2 0x1B00
#define DLI_PRV3 0x1C00
#define DT_INDEX(x)  ((x) & 0x000F)
#define DL_INDEX(x)  ((((x) >> 8) & 0x00FF) - 1)
#define DLI_NAME(x)  ((x) & 0xFF00)
/*
 * Debug mask for kernel mode tracing, if set the output is also sent to
 * the system debug function. Requires that the project is compiled
 * with _KERNEL_DBG_PRINT_
 */
#define DL_TO_KERNEL    0x40000000

#ifdef DIVA_NO_DEBUGLIB
#define myDbgPrint_LOG(x...) do { } while(0);
#define myDbgPrint_FTL(x...) do { } while(0);
#define myDbgPrint_ERR(x...) do { } while(0);
#define myDbgPrint_TRC(x...) do { } while(0);
#define myDbgPrint_MXLOG(x...) do { } while(0);
#define myDbgPrint_EVL(x...) do { } while(0);
#define myDbgPrint_REG(x...) do { } while(0);
#define myDbgPrint_MEM(x...) do { } while(0);
#define myDbgPrint_SPL(x...) do { } while(0);
#define myDbgPrint_IRP(x...) do { } while(0);
#define myDbgPrint_TIM(x...) do { } while(0);
#define myDbgPrint_BLK(x...) do { } while(0);
#define myDbgPrint_TAPI(x...) do { } while(0);
#define myDbgPrint_NDIS(x...) do { } while(0);
#define myDbgPrint_CONN(x...) do { } while(0);
#define myDbgPrint_STAT(x...) do { } while(0);
#define myDbgPrint_SEND(x...) do { } while(0);
#define myDbgPrint_RECV(x...) do { } while(0);
#define myDbgPrint_PRV0(x...) do { } while(0);
#define myDbgPrint_PRV1(x...) do { } while(0);
#define myDbgPrint_PRV2(x...) do { } while(0);
#define myDbgPrint_PRV3(x...) do { } while(0);
#define DBG_TEST(func,args) do { } while(0);
#define DBG_EVL_ID(args) do { } while(0);

#else /* DIVA_NO_DEBUGLIB */
/*
 * define low level macros for formatted & raw debugging
 */
#define DBG_DECL(func) extern void  myDbgPrint_##func (char *, ...) ;
DBG_DECL(LOG)
DBG_DECL(FTL)
DBG_DECL(ERR)
DBG_DECL(TRC)
DBG_DECL(MXLOG)
DBG_DECL(FTL_MXLOG)
extern void  myDbgPrint_EVL (long, ...) ;
DBG_DECL(REG)
DBG_DECL(MEM)
DBG_DECL(SPL)
DBG_DECL(IRP)
DBG_DECL(TIM)
DBG_DECL(BLK)
DBG_DECL(TAPI)
DBG_DECL(NDIS)
DBG_DECL(CONN)
DBG_DECL(STAT)
DBG_DECL(SEND)
DBG_DECL(RECV)
DBG_DECL(PRV0)
DBG_DECL(PRV1)
DBG_DECL(PRV2)
DBG_DECL(PRV3)
#ifdef  _KERNEL_DBG_PRINT_
/*
 * tracing to maint and kernel if selected in the trace mask.
 */
#define DBG_TEST(func,args) \
{ if ( (myDriverDebugHandle.dbgMask) & (unsigned long)DL_##func ) \
 { \
        if ( (myDriverDebugHandle.dbgMask) & DL_TO_KERNEL ) \
            {DbgPrint args; DbgPrint ("\r\n");} \
        myDbgPrint_##func args ; \
} }
#else
/*
 * Standard tracing to maint driver.
 */
#define DBG_TEST(func,args) \
{ if ( (myDriverDebugHandle.dbgMask) & (unsigned long)DL_##func ) \
 { myDbgPrint_##func args ; \
} }
#endif
/*
 * For event level debug use a separate define, the parameter are
 * different and cause compiler errors on some systems.
 */
#define DBG_EVL_ID(args) \
{ if ( (myDriverDebugHandle.dbgMask) & (unsigned long)DL_EVL ) \
 { myDbgPrint_EVL args ; \
} }

#endif /* DIVA_NO_DEBUGLIB */

#define DBG_LOG(args)  DBG_TEST(LOG, args)
#define DBG_FTL(args)  DBG_TEST(FTL, args)
#define DBG_ERR(args)  DBG_TEST(ERR, args)
#define DBG_TRC(args)  DBG_TEST(TRC, args)
#define DBG_MXLOG(args)  DBG_TEST(MXLOG, args)
#define DBG_FTL_MXLOG(args) DBG_TEST(FTL_MXLOG, args)
#define DBG_EVL(args)  DBG_EVL_ID(args)
#define DBG_REG(args)  DBG_TEST(REG, args)
#define DBG_MEM(args)  DBG_TEST(MEM, args)
#define DBG_SPL(args)  DBG_TEST(SPL, args)
#define DBG_IRP(args)  DBG_TEST(IRP, args)
#define DBG_TIM(args)  DBG_TEST(TIM, args)
#define DBG_BLK(args)  DBG_TEST(BLK, args)
#define DBG_TAPI(args)  DBG_TEST(TAPI, args)
#define DBG_NDIS(args)  DBG_TEST(NDIS, args)
#define DBG_CONN(args)  DBG_TEST(CONN, args)
#define DBG_STAT(args)  DBG_TEST(STAT, args)
#define DBG_SEND(args)  DBG_TEST(SEND, args)
#define DBG_RECV(args)  DBG_TEST(RECV, args)
#define DBG_PRV0(args)  DBG_TEST(PRV0, args)
#define DBG_PRV1(args)  DBG_TEST(PRV1, args)
#define DBG_PRV2(args)  DBG_TEST(PRV2, args)
#define DBG_PRV3(args)  DBG_TEST(PRV3, args)
/*
 * prototypes for debug register/deregister functions in "debuglib.c"
 */
#ifdef DIVA_NO_DEBUGLIB
#define DbgRegister(name,tag, mask) do { } while(0)
#define DbgDeregister() do { } while(0)
#define DbgSetLevel(mask) do { } while(0)
#else
extern DIVA_DI_PRINTF dprintf;
extern int  DbgRegister (char *drvName, char *drvTag, unsigned long dbgMask) ;
extern void DbgDeregister (void) ;
extern void DbgSetLevel (unsigned long dbgMask) ;
#endif
/*
 * driver internal structure for debug handling;
 * in client drivers this structure is maintained in "debuglib.c",
 * in the debug driver "debug.c" maintains a chain of such structs.
 */
typedef struct _DbgHandle_ *pDbgHandle ;
typedef void ( * DbgEnd) (pDbgHandle) ;
typedef void ( * DbgLog) (unsigned short, int, char *, va_list) ;
typedef void ( * DbgOld) (unsigned short, char *, va_list) ;
typedef void ( * DbgEv)  (unsigned short, unsigned long, va_list) ;
typedef void ( * DbgIrq) (unsigned short, int, char *, va_list) ;
typedef struct _DbgHandle_
{ char    Registered ; /* driver successfully registered */
#define DBG_HANDLE_REG_NEW 0x01  /* this (new) structure    */
#define DBG_HANDLE_REG_OLD 0x7f  /* old structure (see below)  */
 char    Version;  /* version of this structure  */
#define DBG_HANDLE_VERSION 1   /* contains dbg_old function now */
#define DBG_HANDLE_VER_EXT  2           /* pReserved points to extended info*/
 short               id ;   /* internal id of registered driver */
  struct _DbgHandle_ *next ;   /* ptr to next registered driver    */
 struct /*LARGE_INTEGER*/ {
  unsigned long LowPart;
  long          HighPart;
 }     regTime ;  /* timestamp for registration       */
 void               *pIrp ;   /* ptr to pending i/o request       */
 unsigned long       dbgMask ;  /* current debug mask               */
 char                drvName[16] ; /* ASCII name of registered driver  */
 char                drvTag[64] ; /* revision string     */
 DbgEnd              dbg_end ;  /* function for debug closing       */
 DbgLog              dbg_prt ;  /* function for debug appending     */
 DbgOld              dbg_old ;  /* function for old debug appending */
 DbgEv       dbg_ev ;  /* function for Windows NT Eventlog */
 DbgIrq    dbg_irq ;  /* function for irql checked debug  */
 void      *pReserved3 ;
} _DbgHandle_ ;
extern _DbgHandle_ myDriverDebugHandle ;
typedef struct _OldDbgHandle_
{ struct _OldDbgHandle_ *next ;
 void                *pIrp ;
 long    regTime[2] ;
 unsigned long       dbgMask ;
 short               id ;
 char                drvName[78] ;
 DbgEnd              dbg_end ;
 DbgLog              dbg_prt ;
} _OldDbgHandle_ ;
/* the differences in DbgHandles
   old:    tmp:     new:
 0 long next  char Registered  char Registered
       char filler   char Version
       short id    short id
 4 long pIrp  long    regTime.lo  long next
 8 long    regTime.lo long    regTime.hi  long    regTime.lo
 12 long    regTime.hi long next   long regTime.hi
 16 long dbgMask  long pIrp   long pIrp
 20 short id   long dbgMask   long dbgMask
 22 char    drvName[78] ..
 24 ..     char drvName[16]  char drvName[16]
 40 ..     char drvTag[64]  char drvTag[64]
 100 void *dbg_end ..      ..
 104 void *dbg_prt void *dbg_end  void *dbg_end
 108 ..     void *dbg_prt  void *dbg_prt
 112 ..     ..      void *dbg_old
 116 ..     ..      void *dbg_ev
 120 ..     ..      void *dbg_irq
 124 ..     ..      void *pReserved3
 ( new->id == 0 && *((short *)&new->dbgMask) == -1 ) identifies "old",
 new->Registered and new->Version overlay old->next,
 new->next overlays old->pIrp, new->regTime matches old->regTime and
 thus these fields can be maintained in new struct whithout trouble;
 id, dbgMask, drvName, dbg_end and dbg_prt need special handling !
*/
#define DBG_EXT_TYPE_CARD_TRACE     0x00000001
typedef struct
{
    unsigned long       ExtendedType;
    union
    {
        /* DBG_EXT_TYPE_CARD_TRACE */
        struct
        {
            void ( * MaskChangedNotify) (void *pContext);
            unsigned long   ModuleTxtMask;
            unsigned long   DebugLevel;
            unsigned long   B_ChannelMask;
            unsigned long   LogBufferSize;
        } CardTrace;
    }Data;     
} _DbgExtendedInfo_;
#ifndef DIVA_NO_DEBUGLIB
/* -------------------------------------------------------------
    Function used for xlog-style debug
   ------------------------------------------------------------- */
#define XDI_USE_XLOG 1
void  xdi_dbg_xlog (char* x, ...);
#endif /* DIVA_NO_DEBUGLIB */
#endif /* __DEBUGLIB_H__ */
