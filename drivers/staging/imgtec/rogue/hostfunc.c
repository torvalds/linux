/*************************************************************************/ /*!
@File
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
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/hardirq.h>

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#endif	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */

#include "img_types.h"
#include "pvr_debug.h"

#include "dbgdrvif_srv5.h"
#include "hostfunc.h"
#include "dbgdriv.h"


/*!
******************************************************************************

 @Function	HostMemSet

 @Description Function that does the same as the C memset() functions

 @Modified *pvDest :	pointer to start of buffer to be set

 @Input    ui8Value:	value to set each byte to

 @Input    ui32Size :	number of bytes to set

 @Return   void

******************************************************************************/
void HostMemSet(void *pvDest, IMG_UINT8 ui8Value, IMG_UINT32 ui32Size)
{
	memset(pvDest, (int) ui8Value, (size_t) ui32Size);
}

/*!
******************************************************************************

 @Function		HostMemCopy

 @Description	Copies memory around

 @Input    pvDst - pointer to dst
 @Output   pvSrc - pointer to src
 @Input    ui32Size - bytes to copy

 @Return  none

******************************************************************************/
void HostMemCopy(void *pvDst, void *pvSrc, IMG_UINT32 ui32Size)
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
    /* XXX Not yet implemented */
	return 0;
}

void * HostPageablePageAlloc(IMG_UINT32 ui32Pages)
{
    return (void*)vmalloc(ui32Pages * PAGE_SIZE);/*, GFP_KERNEL);*/
}

void HostPageablePageFree(void * pvBase)
{
    vfree(pvBase);
}

void * HostNonPageablePageAlloc(IMG_UINT32 ui32Pages)
{
    return (void*)vmalloc(ui32Pages * PAGE_SIZE);/*, GFP_KERNEL);*/
}

void HostNonPageablePageFree(void * pvBase)
{
    vfree(pvBase);
}

void * HostMapKrnBufIntoUser(void * pvKrnAddr, IMG_UINT32 ui32Size, void **ppvMdl)
{
    /* XXX Not yet implemented */
	return NULL;
}

void HostUnMapKrnBufFromUser(void * pvUserAddr, void * pvMdl, void * pvProcess)
{
    /* XXX Not yet implemented */
}

void HostCreateRegDeclStreams(void)
{
    /* XXX Not yet implemented */
}

#if defined(SUPPORT_DBGDRV_EVENT_OBJECTS)

#define	EVENT_WAIT_TIMEOUT_MS	500
#define	EVENT_WAIT_TIMEOUT_JIFFIES	(EVENT_WAIT_TIMEOUT_MS * HZ / 1000)

static int iStreamData;
static wait_queue_head_t sStreamDataEvent;

IMG_INT32 HostCreateEventObjects(void)
{
	init_waitqueue_head(&sStreamDataEvent);

	return 0;
}

void HostWaitForEvent(DBG_EVENT eEvent)
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

void HostSignalEvent(DBG_EVENT eEvent)
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

void HostDestroyEventObjects(void)
{
}
#endif	/* defined(SUPPORT_DBGDRV_EVENT_OBJECTS) */
