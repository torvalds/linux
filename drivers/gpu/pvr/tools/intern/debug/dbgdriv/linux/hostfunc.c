/*************************************************************************/ /*!
@Title          Debug driver file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
#include <linux/mutex.h>
#else
#include <asm/semaphore.h>
#endif
#include <linux/hardirq.h>

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#endif	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */

#include "img_types.h"
#include "pvr_debug.h"

#include "dbgdrvif.h"
#include "hostfunc.h"
#include "dbgdriv.h"

#if defined(MODULE) && defined(DEBUG) && !defined(SUPPORT_DRI_DRM)
IMG_UINT32	gPVRDebugLevel = (DBGPRIV_FATAL | DBGPRIV_ERROR | DBGPRIV_WARNING);

#define PVR_STRING_TERMINATOR		'\0'
#define PVR_IS_FILE_SEPARATOR(character) ( ((character) == '\\') || ((character) == '/') )

/******************************************************************************/


/*!
******************************************************************************

 @Function     PVRSRVDebugPrintf
	
 @Description  To output a debug message to the user

 @Input uDebugLevel: The current debug level
 @Input pszFile:     The source file generating the message
 @Input uLine:       The line of the source file
 @Input pszFormat:   The message format string
 @Input ...:         Zero or more arguments for use by the format string

 @Return none
******************************************************************************/
void PVRSRVDebugPrintf	(
						IMG_UINT32	ui32DebugLevel,
						const IMG_CHAR*	pszFileName,
						IMG_UINT32	ui32Line,
						const IMG_CHAR*	pszFormat,
						...
					)
{
	IMG_BOOL bTrace;
#if !defined(__sh__)
	IMG_CHAR *pszLeafName;

	pszLeafName = (char *)strrchr (pszFileName, '\\');

	if (pszLeafName)
	{
		pszFileName = pszLeafName;
	}
#endif /* __sh__ */

	bTrace = (IMG_BOOL)(ui32DebugLevel & DBGPRIV_CALLTRACE) ? IMG_TRUE : IMG_FALSE;

	if (gPVRDebugLevel & ui32DebugLevel)
	{
		va_list vaArgs;
		char szBuffer[256];
		char *szBufferEnd = szBuffer;
		char *szBufferLimit = szBuffer + sizeof(szBuffer) - 1;

		/* The Limit - End pointer arithmetic we're doing in snprintf
		   ensures that our buffer remains null terminated from this */
		*szBufferLimit = '\0';

		snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "PVR_K:");
		szBufferEnd += strlen(szBufferEnd);

		/* Add in the level of warning */
		if (bTrace == IMG_FALSE)
		{
			switch(ui32DebugLevel)
			{
				case DBGPRIV_FATAL:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Fatal):");
					break;
				}
				case DBGPRIV_ERROR:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Error):");
					break;
				}
				case DBGPRIV_WARNING:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Warning):");
					break;
				}
				case DBGPRIV_MESSAGE:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Message):");
					break;
				}
				case DBGPRIV_VERBOSE:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Verbose):");
					break;
				}
				default:
				{
					snprintf(szBufferEnd, szBufferLimit - szBufferEnd, "(Unknown message level)");
					break;
				}
			}
			szBufferEnd += strlen(szBufferEnd);
		}
		snprintf(szBufferEnd, szBufferLimit - szBufferEnd, " ");
		szBufferEnd += strlen(szBufferEnd);

		va_start (vaArgs, pszFormat);
		vsnprintf(szBufferEnd, szBufferLimit - szBufferEnd, pszFormat, vaArgs);
		va_end (vaArgs);
		szBufferEnd += strlen(szBufferEnd);

 		/*
 		 * Metrics and Traces don't need a location
 		 */
 		if (bTrace == IMG_FALSE)
		{
			snprintf(szBufferEnd, szBufferLimit - szBufferEnd, 
			         " [%d, %s]", (int)ui32Line, pszFileName);
			szBufferEnd += strlen(szBufferEnd);
		}

		printk(KERN_INFO "%s\r\n", szBuffer);
	}
}
#endif	/* defined(DEBUG) && !defined(SUPPORT_DRI_DRM) */

/*!
******************************************************************************

 @Function	HostMemSet

 @Description Function that does the same as the C memset() function

 @Modified *pvDest :	pointer to start of buffer to be set

 @Input    ui8Value:	value to set each byte to

 @Input    ui32Size :	number of bytes to set

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID HostMemSet(IMG_VOID *pvDest, IMG_UINT8 ui8Value, IMG_UINT32 ui32Size)
{
	memset(pvDest, (int) ui8Value, (size_t) ui32Size);
}

/*!
******************************************************************************

 @Function		HostMemCopy

 @Description	Function that does the same as the C memscpy() function

 @Input    pvDst - pointer to dst
 @Output   pvSrc - pointer to src
 @Input    ui32Size - bytes to copy

 @Return  none

******************************************************************************/
IMG_VOID HostMemCopy(IMG_VOID *pvDst, IMG_VOID *pvSrc, IMG_UINT32 ui32Size)
{
#if defined(USE_UNOPTIMISED_MEMCPY)
    unsigned char *src,*dst;
    int i;

    src=(unsigned char *)pvSrc;
    dst=(unsigned char *)pvDst;
    for(i=0;i<ui32Size;i++)
    {
        dst[i]=src[i];
    }
#else
    memcpy(pvDst, pvSrc, ui32Size);
#endif
}

IMG_UINT32 HostReadRegistryDWORDFromString(char *pcKey, char *pcValueName, IMG_UINT32 *pui32Data)
{
    /* FIXME: Not yet implemented */
	return 0;
}

IMG_VOID * HostPageablePageAlloc(IMG_UINT32 ui32Pages)
{
    return (void*)vmalloc(ui32Pages * PAGE_SIZE);/*, GFP_KERNEL);*/
}

IMG_VOID HostPageablePageFree(IMG_VOID * pvBase)
{
    vfree(pvBase);
}

IMG_VOID * HostNonPageablePageAlloc(IMG_UINT32 ui32Pages)
{
    return (void*)vmalloc(ui32Pages * PAGE_SIZE);/*, GFP_KERNEL);*/
}

IMG_VOID HostNonPageablePageFree(IMG_VOID * pvBase)
{
    vfree(pvBase);
}

IMG_VOID * HostMapKrnBufIntoUser(IMG_VOID * pvKrnAddr, IMG_UINT32 ui32Size, IMG_VOID **ppvMdl)
{
    /* FIXME: Not yet implemented */
	return IMG_NULL;
}

IMG_VOID HostUnMapKrnBufFromUser(IMG_VOID * pvUserAddr, IMG_VOID * pvMdl, IMG_VOID * pvProcess)
{
    /* FIXME: Not yet implemented */
}

IMG_VOID HostCreateRegDeclStreams(IMG_VOID)
{
    /* FIXME: Not yet implemented */
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
typedef	struct mutex		MUTEX;
#define	INIT_MUTEX(m)		mutex_init(m)
#define	DOWN_TRYLOCK(m)		(!mutex_trylock(m))
#define	DOWN(m)			mutex_lock(m)
#define UP(m)			mutex_unlock(m)
#else
typedef	struct semaphore	MUTEX;
#define	INIT_MUTEX(m)		init_MUTEX(m)
#define	DOWN_TRYLOCK(m)		down_trylock(m)
#define	DOWN(m)			down(m)
#define UP(m)			up(m)
#endif

IMG_VOID *HostCreateMutex(IMG_VOID)
{
	MUTEX *psMutex;

	psMutex = kmalloc(sizeof(*psMutex), GFP_KERNEL);
	if (psMutex)
	{
		INIT_MUTEX(psMutex);
	}

	return psMutex;
}

IMG_VOID HostAquireMutex(IMG_VOID * pvMutex)
{
	BUG_ON(in_interrupt());

#if defined(PVR_DEBUG_DBGDRV_DETECT_HOST_MUTEX_COLLISIONS)
	if (DOWN_TRYLOCK((MUTEX *)pvMutex))
	{
		printk(KERN_INFO "HostAquireMutex: Waiting for mutex\n");
		DOWN((MUTEX *)pvMutex);
	}
#else
	DOWN((MUTEX *)pvMutex);
#endif
}

IMG_VOID HostReleaseMutex(IMG_VOID * pvMutex)
{
	UP((MUTEX *)pvMutex);
}

IMG_VOID HostDestroyMutex(IMG_VOID * pvMutex)
{
	if (pvMutex)
	{
		kfree(pvMutex);
	}
}

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)

#define	EVENT_WAIT_TIMEOUT_MS	500
#define	EVENT_WAIT_TIMEOUT_JIFFIES	(EVENT_WAIT_TIMEOUT_MS * HZ / 1000)

static int iStreamData;
static wait_queue_head_t sStreamDataEvent;

IMG_INT32 HostCreateEventObjects(IMG_VOID)
{
	init_waitqueue_head(&sStreamDataEvent);

	return 0;
}

IMG_VOID HostWaitForEvent(DBG_EVENT eEvent)
{
	switch(eEvent)
	{
		case DBG_EVENT_STREAM_DATA:
			/*
			 * More than one process may be woken up.
			 * Any process that wakes up should consume
			 * all the data from the streams.
			 */
			wait_event_interruptible_timeout(sStreamDataEvent, iStreamData != 0, EVENT_WAIT_TIMEOUT_JIFFIES);
			iStreamData = 0;
			break;
		default:
			/*
			 * For unknown events, enter an interruptible sleep.
			 */
			msleep_interruptible(EVENT_WAIT_TIMEOUT_MS);
			break;
	}
}

IMG_VOID HostSignalEvent(DBG_EVENT eEvent)
{
	switch(eEvent)
	{
		case DBG_EVENT_STREAM_DATA:
			iStreamData = 1;
			wake_up_interruptible(&sStreamDataEvent);
			break;
		default:
			break;
	}
}

IMG_VOID HostDestroyEventObjects(IMG_VOID)
{
}
#endif	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */
