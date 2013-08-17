/*************************************************************************/ /*!
@Title          PVR Debug Declarations
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides debug functionality
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#ifndef __PVR_DEBUG_H__
#define __PVR_DEBUG_H__


#include "img_types.h"


#if defined (__cplusplus)
extern "C" {
#endif

#define PVR_MAX_DEBUG_MESSAGE_LEN	(512)

/* These are privately used by pvr_debug, use the PVR_DBG_ defines instead */
#define DBGPRIV_FATAL			0x001UL
#define DBGPRIV_ERROR			0x002UL
#define DBGPRIV_WARNING			0x004UL
#define DBGPRIV_MESSAGE			0x008UL
#define DBGPRIV_VERBOSE			0x010UL
#define DBGPRIV_CALLTRACE		0x020UL
#define DBGPRIV_ALLOC			0x040UL
#define DBGPRIV_BUFFERED		0x080UL
#define DBGPRIV_DBGDRV_MESSAGE	0x100UL

#define DBGPRIV_DBGLEVEL_COUNT	9

#if !defined(PVRSRV_NEED_PVR_ASSERT) && defined(DEBUG)
#define PVRSRV_NEED_PVR_ASSERT
#endif

#if defined(PVRSRV_NEED_PVR_ASSERT) && !defined(PVRSRV_NEED_PVR_DPF)
#define PVRSRV_NEED_PVR_DPF
#endif

#if !defined(PVRSRV_NEED_PVR_TRACE) && (defined(DEBUG) || defined(TIMING))
#define PVRSRV_NEED_PVR_TRACE
#endif

/* PVR_ASSERT() and PVR_DBG_BREAK handling */

#if defined(PVRSRV_NEED_PVR_ASSERT)

#if defined(LINUX) && defined(__KERNEL__)
/* In Linux kernel mode, use BUG() directly. This produces the correct
   filename and line number in the panic message. */
#define PVR_ASSERT(EXPR) do											\
	{																\
		if (!(EXPR))												\
		{															\
			PVRSRVDebugPrintf(DBGPRIV_FATAL, __FILE__, __LINE__,	\
							  "Debug assertion failed!");			\
			BUG();													\
		}															\
	} while (0)

#else /* defined(LINUX) && defined(__KERNEL__) */

IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVDebugAssertFail(const IMG_CHAR *pszFile,
													   IMG_UINT32 ui32Line);

#if defined(LINUX)
	#define PVR_ASSERT(EXPR) do								\
		{													\
			if (!(EXPR))									\
				PVRSRVDebugAssertFail(__FILE__, __LINE__);	\
		} while (0)
#else
    #if defined (__QNXNTO__)
	    #define PVR_ASSERT(EXPR) if (!(EXPR)) PVRSRVDebugAssertFail(__FILE__, __LINE__);
    #else
	    #define PVR_ASSERT(EXPR) if (!(EXPR)) PVRSRVDebugAssertFail(__FILE__, __LINE__)
    #endif
#endif

#endif /* defined(LINUX) && defined(__KERNEL__) */


			#if defined(LINUX) && defined(__KERNEL__)
				#define PVR_DBG_BREAK BUG()
			#else
				#define PVR_DBG_BREAK PVRSRVDebugAssertFail(__FILE__, __LINE__)
			#endif

#else  /* defined(PVRSRV_NEED_PVR_ASSERT) */

	#define PVR_ASSERT(EXPR)
	#define PVR_DBG_BREAK

#endif /* defined(PVRSRV_NEED_PVR_ASSERT) */


/* PVR_DPF() handling */

#if defined(PVRSRV_NEED_PVR_DPF)

#if defined(PVRSRV_NEW_PVR_DPF)

	/* New logging mechanism */
	#define PVR_DBG_FATAL		DBGPRIV_FATAL
	#define PVR_DBG_ERROR		DBGPRIV_ERROR
	#define PVR_DBG_WARNING		DBGPRIV_WARNING
	#define PVR_DBG_MESSAGE		DBGPRIV_MESSAGE
	#define PVR_DBG_VERBOSE		DBGPRIV_VERBOSE
	#define PVR_DBG_CALLTRACE	DBGPRIV_CALLTRACE
	#define PVR_DBG_ALLOC		DBGPRIV_ALLOC
	#define PVR_DBG_BUFFERED	DBGPRIV_BUFFERED
	#define PVR_DBGDRIV_MESSAGE	DBGPRIV_DBGDRV_MESSAGE

	/* These levels are always on with PVRSRV_NEED_PVR_DPF */
	#define __PVR_DPF_0x001UL(x...) PVRSRVDebugPrintf(DBGPRIV_FATAL, x)
	#define __PVR_DPF_0x002UL(x...) PVRSRVDebugPrintf(DBGPRIV_ERROR, x)
	#define __PVR_DPF_0x080UL(x...) PVRSRVDebugPrintf(DBGPRIV_BUFFERED, x)

	/* Some are compiled out completely in release builds */
#if defined(DEBUG)
	#define __PVR_DPF_0x004UL(x...) PVRSRVDebugPrintf(DBGPRIV_WARNING, x)
	#define __PVR_DPF_0x008UL(x...) PVRSRVDebugPrintf(DBGPRIV_MESSAGE, x)
	#define __PVR_DPF_0x010UL(x...) PVRSRVDebugPrintf(DBGPRIV_VERBOSE, x)
	#define __PVR_DPF_0x020UL(x...) PVRSRVDebugPrintf(DBGPRIV_CALLTRACE, x)
	#define __PVR_DPF_0x040UL(x...) PVRSRVDebugPrintf(DBGPRIV_ALLOC, x)
	#define __PVR_DPF_0x100UL(x...) PVRSRVDebugPrintf(DBGPRIV_DBGDRV_MESSAGE, x)
#else
	#define __PVR_DPF_0x004UL(x...)
	#define __PVR_DPF_0x008UL(x...)
	#define __PVR_DPF_0x010UL(x...)
	#define __PVR_DPF_0x020UL(x...)
	#define __PVR_DPF_0x040UL(x...)
	#define __PVR_DPF_0x100UL(x...)
#endif

	/* Translate the different log levels to separate macros
	 * so they can each be compiled out.
	 */
#if defined(DEBUG)
	#define __PVR_DPF(lvl, x...) __PVR_DPF_ ## lvl (__FILE__, __LINE__, x)
#else
	#define __PVR_DPF(lvl, x...) __PVR_DPF_ ## lvl ("", 0, x)
#endif

	/* Get rid of the double bracketing */
	#define PVR_DPF(x) __PVR_DPF x

#else /* defined(PVRSRV_NEW_PVR_DPF) */

	/* Old logging mechanism */
	#define PVR_DBG_FATAL		DBGPRIV_FATAL,__FILE__, __LINE__
	#define PVR_DBG_ERROR		DBGPRIV_ERROR,__FILE__, __LINE__
	#define PVR_DBG_WARNING		DBGPRIV_WARNING,__FILE__, __LINE__
	#define PVR_DBG_MESSAGE		DBGPRIV_MESSAGE,__FILE__, __LINE__
	#define PVR_DBG_VERBOSE		DBGPRIV_VERBOSE,__FILE__, __LINE__
	#define PVR_DBG_CALLTRACE	DBGPRIV_CALLTRACE,__FILE__, __LINE__
	#define PVR_DBG_ALLOC		DBGPRIV_ALLOC,__FILE__, __LINE__
	#define PVR_DBG_BUFFERED	DBGPRIV_BUFFERED,__FILE__, __LINE__
	#define PVR_DBGDRIV_MESSAGE	DBGPRIV_DBGDRV_MESSAGE, "", 0

	#define PVR_DPF(X)			PVRSRVDebugPrintf X

#endif /* defined(PVRSRV_NEW_PVR_DPF) */

IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVDebugPrintf(IMG_UINT32 ui32DebugLevel,
												   const IMG_CHAR *pszFileName,
												   IMG_UINT32 ui32Line,
												   const IMG_CHAR *pszFormat,
												   ...) IMG_FORMAT_PRINTF(4, 5);

IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVDebugPrintfDumpCCB(void);

#else  /* defined(PVRSRV_NEED_PVR_DPF) */

	#define PVR_DPF(X)

#endif /* defined(PVRSRV_NEED_PVR_DPF) */

/* PVR_TRACE() handling */

#if defined(PVRSRV_NEED_PVR_TRACE)

	#define PVR_TRACE(X)	PVRSRVTrace X

IMG_IMPORT IMG_VOID IMG_CALLCONV PVRSRVTrace(const IMG_CHAR* pszFormat, ... )
	IMG_FORMAT_PRINTF(1, 2);

#else /* defined(PVRSRV_NEED_PVR_TRACE) */

	#define PVR_TRACE(X)

#endif /* defined(PVRSRV_NEED_PVR_TRACE) */


#if defined (__cplusplus)
}
#endif

#endif	/* __PVR_DEBUG_H__ */

/******************************************************************************
 End of file (pvr_debug.h)
******************************************************************************/

