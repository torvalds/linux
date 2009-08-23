/*	BSDI osd_util.h,v 1.8 1998/06/03 19:14:58 karels Exp	*/

/*
 * Copyright (c) 1996-1999 Distributed Processing Technology Corporation
 * All rights reserved.
 *
 * Redistribution and use in source form, with or without modification, are
 * permitted provided that redistributions of source code must retain the
 * above copyright notice, this list of conditions and the following disclaimer.
 *
 * This software is provided `as is' by Distributed Processing Technology and
 * any express or implied warranties, including, but not limited to, the
 * implied warranties of merchantability and fitness for a particular purpose,
 * are disclaimed. In no event shall Distributed Processing Technology be
 * liable for any direct, indirect, incidental, special, exemplary or
 * consequential damages (including, but not limited to, procurement of
 * substitute goods or services; loss of use, data, or profits; or business
 * interruptions) however caused and on any theory of liability, whether in
 * contract, strict liability, or tort (including negligence or otherwise)
 * arising in any way out of the use of this driver software, even if advised
 * of the possibility of such damage.
 *
 */

#ifndef         __OSD_UTIL_H
#define         __OSD_UTIL_H

/*File - OSD_UTIL.H
 ****************************************************************************
 *
 *Description:
 *
 *      This file contains defines and function prototypes that are
 *operating system dependent.  The resources defined in this file
 *are not specific to any particular application.
 *
 *Copyright Distributed Processing Technology, Corp.
 *        140 Candace Dr.
 *        Maitland, Fl. 32751   USA
 *        Phone: (407) 830-5522  Fax: (407) 260-5366
 *        All Rights Reserved
 *
 *Author:       Doug Anderson
 *Date:         1/7/94
 *
 *Editors:
 *
 *Remarks:
 *
 *
 *****************************************************************************/


/*Definitions - Defines & Constants ----------------------------------------- */

/*----------------------------- */
/* Operating system selections: */
/*----------------------------- */

/*#define               _DPT_MSDOS      */
/*#define               _DPT_WIN_3X     */
/*#define               _DPT_WIN_4X     */
/*#define               _DPT_WIN_NT     */
/*#define               _DPT_NETWARE    */
/*#define               _DPT_OS2        */
/*#define               _DPT_SCO        */
/*#define               _DPT_UNIXWARE   */
/*#define               _DPT_SOLARIS    */
/*#define               _DPT_NEXTSTEP   */
/*#define               _DPT_BANYAN     */

/*-------------------------------- */
/* Include the OS specific defines */
/*-------------------------------- */

/*#define       OS_SELECTION    From Above List */
/*#define       SEMAPHORE_T     ??? */
/*#define       DLL_HANDLE_T    ??? */

#if (defined(KERNEL) && (defined(__FreeBSD__) || defined(__bsdi__)))
# include        "i386/isa/dpt_osd_defs.h"
#else
# include        "osd_defs.h"
#endif

#ifndef DPT_UNALIGNED
   #define      DPT_UNALIGNED
#endif

#ifndef DPT_EXPORT
   #define      DPT_EXPORT
#endif

#ifndef DPT_IMPORT
   #define      DPT_IMPORT
#endif

#ifndef DPT_RUNTIME_IMPORT
   #define      DPT_RUNTIME_IMPORT  DPT_IMPORT
#endif

/*--------------------- */
/* OS dependent defines */
/*--------------------- */

#if defined (_DPT_MSDOS) || defined (_DPT_WIN_3X)
   #define      _DPT_16_BIT
#else
   #define      _DPT_32_BIT
#endif

#if defined (_DPT_SCO) || defined (_DPT_UNIXWARE) || defined (_DPT_SOLARIS) || defined (_DPT_AIX) || defined (SNI_MIPS) || defined (_DPT_BSDI) || defined (_DPT_FREE_BSD) || defined(_DPT_LINUX)
   #define      _DPT_UNIX
#endif

#if defined (_DPT_WIN_3x) || defined (_DPT_WIN_4X) || defined (_DPT_WIN_NT) \
	    || defined (_DPT_OS2)
   #define      _DPT_DLL_SUPPORT
#endif

#if !defined (_DPT_MSDOS) && !defined (_DPT_WIN_3X) && !defined (_DPT_NETWARE)
   #define      _DPT_PREEMPTIVE
#endif

#if !defined (_DPT_MSDOS) && !defined (_DPT_WIN_3X)
   #define      _DPT_MULTI_THREADED
#endif

#if !defined (_DPT_MSDOS)
   #define      _DPT_MULTI_TASKING
#endif

  /* These exist for platforms that   */
  /* chunk when accessing mis-aligned */
  /* data                             */
#if defined (SNI_MIPS) || defined (_DPT_SOLARIS)
   #if defined (_DPT_BIG_ENDIAN)
	#if !defined (_DPT_STRICT_ALIGN)
            #define _DPT_STRICT_ALIGN
	#endif
   #endif
#endif

  /* Determine if in C or C++ mode */
#ifdef  __cplusplus
   #define      _DPT_CPP
#else
   #define      _DPT_C
#endif

/*-------------------------------------------------------------------*/
/* Under Solaris the compiler refuses to accept code like:           */
/*   { {"DPT"}, 0, NULL .... },                                      */
/* and complains about the {"DPT"} part by saying "cannot use { }    */
/* to initialize char*".                                             */
/*                                                                   */
/* By defining these ugly macros we can get around this and also     */
/* not have to copy and #ifdef large sections of code.  I know that  */
/* these macros are *really* ugly, but they should help reduce       */
/* maintenance in the long run.                                      */
/*                                                                   */
/*-------------------------------------------------------------------*/
#if !defined (DPTSQO)
   #if defined (_DPT_SOLARIS)
      #define DPTSQO
      #define DPTSQC
   #else
      #define DPTSQO {
      #define DPTSQC }
   #endif  /* solaris */
#endif  /* DPTSQO */


/*---------------------- */
/* OS dependent typedefs */
/*---------------------- */

#if defined (_DPT_MSDOS) || defined (_DPT_SCO)
   #define BYTE unsigned char
   #define WORD unsigned short
#endif

#ifndef _DPT_TYPEDEFS
   #define _DPT_TYPEDEFS
   typedef unsigned char   uCHAR;
   typedef unsigned short  uSHORT;
   typedef unsigned int    uINT;
   typedef unsigned long   uLONG;

   typedef union {
	 uCHAR        u8[4];
	 uSHORT       u16[2];
	 uLONG        u32;
   } access_U;
#endif

#if !defined (NULL)
   #define      NULL    0
#endif


/*Prototypes - function ----------------------------------------------------- */

#ifdef  __cplusplus
   extern "C" {         /* Declare all these functions as "C" functions */
#endif

/*------------------------ */
/* Byte reversal functions */
/*------------------------ */

  /* Reverses the byte ordering of a 2 byte variable */
#if (!defined(osdSwap2))
 uSHORT       osdSwap2(DPT_UNALIGNED uSHORT *);
#endif  // !osdSwap2

  /* Reverses the byte ordering of a 4 byte variable and shifts left 8 bits */
#if (!defined(osdSwap3))
 uLONG        osdSwap3(DPT_UNALIGNED uLONG *);
#endif  // !osdSwap3


#ifdef  _DPT_NETWARE
   #include "novpass.h" /* For DPT_Bswapl() prototype */
	/* Inline the byte swap */
   #ifdef __cplusplus
	 inline uLONG osdSwap4(uLONG *inLong) {
	 return *inLong = DPT_Bswapl(*inLong);
	 }
   #else
	 #define osdSwap4(inLong)       DPT_Bswapl(inLong)
   #endif  // cplusplus
#else
	/* Reverses the byte ordering of a 4 byte variable */
# if (!defined(osdSwap4))
   uLONG        osdSwap4(DPT_UNALIGNED uLONG *);
# endif  // !osdSwap4

  /* The following functions ALWAYS swap regardless of the *
   * presence of DPT_BIG_ENDIAN                            */

   uSHORT       trueSwap2(DPT_UNALIGNED uSHORT *);
   uLONG        trueSwap4(DPT_UNALIGNED uLONG *);

#endif  // netware


/*-------------------------------------*
 * Network order swap functions        *
 *                                     *
 * These functions/macros will be used *
 * by the structure insert()/extract() *
 * functions.                          *
 *
 * We will enclose all structure       *
 * portability modifications inside    *
 * #ifdefs.  When we are ready, we     *
 * will #define DPT_PORTABLE to begin  *
 * using the modifications.            *
 *-------------------------------------*/
uLONG	netSwap4(uLONG val);

#if defined (_DPT_BIG_ENDIAN)

// for big-endian we need to swap

#ifndef NET_SWAP_2
#define NET_SWAP_2(x) (((x) >> 8) | ((x) << 8))
#endif  // NET_SWAP_2

#ifndef NET_SWAP_4
#define NET_SWAP_4(x) netSwap4((x))
#endif  // NET_SWAP_4

#else

// for little-endian we don't need to do anything

#ifndef NET_SWAP_2
#define NET_SWAP_2(x) (x)
#endif  // NET_SWAP_2

#ifndef NET_SWAP_4
#define NET_SWAP_4(x) (x)
#endif  // NET_SWAP_4

#endif  // big endian



/*----------------------------------- */
/* Run-time loadable module functions */
/*----------------------------------- */

  /* Loads the specified run-time loadable DLL */
DLL_HANDLE_T    osdLoadModule(uCHAR *);
  /* Unloads the specified run-time loadable DLL */
uSHORT          osdUnloadModule(DLL_HANDLE_T);
  /* Returns a pointer to a function inside a run-time loadable DLL */
void *          osdGetFnAddr(DLL_HANDLE_T,uCHAR *);

/*--------------------------------------- */
/* Mutually exclusive semaphore functions */
/*--------------------------------------- */

  /* Create a named semaphore */
SEMAPHORE_T     osdCreateNamedSemaphore(char *);
  /* Create a mutually exlusive semaphore */
SEMAPHORE_T     osdCreateSemaphore(void);
	/* create an event semaphore */
SEMAPHORE_T              osdCreateEventSemaphore(void);
	/* create a named event semaphore */
SEMAPHORE_T             osdCreateNamedEventSemaphore(char *);

  /* Destroy the specified mutually exclusive semaphore object */
uSHORT          osdDestroySemaphore(SEMAPHORE_T);
  /* Request access to the specified mutually exclusive semaphore */
uLONG           osdRequestSemaphore(SEMAPHORE_T,uLONG);
  /* Release access to the specified mutually exclusive semaphore */
uSHORT          osdReleaseSemaphore(SEMAPHORE_T);
	/* wait for a event to happen */
uLONG                            osdWaitForEventSemaphore(SEMAPHORE_T, uLONG);
	/* signal an event */
uLONG                            osdSignalEventSemaphore(SEMAPHORE_T);
	/* reset the event */
uLONG                            osdResetEventSemaphore(SEMAPHORE_T);

/*----------------- */
/* Thread functions */
/*----------------- */

  /* Releases control to the task switcher in non-preemptive */
  /* multitasking operating systems. */
void            osdSwitchThreads(void);

  /* Starts a thread function */
uLONG   osdStartThread(void *,void *);

/* what is my thread id */
uLONG osdGetThreadID(void);

/* wakes up the specifed thread */
void osdWakeThread(uLONG);

/* osd sleep for x milliseconds */
void osdSleep(uLONG);

#define DPT_THREAD_PRIORITY_LOWEST 0x00
#define DPT_THREAD_PRIORITY_NORMAL 0x01
#define DPT_THREAD_PRIORITY_HIGHEST 0x02

uCHAR osdSetThreadPriority(uLONG tid, uCHAR priority);

#ifdef __cplusplus
   }    /* end the xtern "C" declaration */
#endif

#endif  /* osd_util_h */
