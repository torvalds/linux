/*************************************************************************/ /*!
@File           htbuffer.h
@Title          Host Trace Buffer shared API.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Host Trace Buffer provides a mechanism to log Host events to a
                buffer in a similar way to the Firmware Trace mechanism.
                Host Trace Buffer logs data using a Transport Layer buffer.
                The Transport Layer and pvrtld tool provides the mechanism to
                retrieve the trace data.
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
#ifndef HTBUFFER_H
#define HTBUFFER_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "htbuffer_sf.h"
#include "htbuffer_types.h"
#include "htbuffer_init.h"

#if defined(__KERNEL__)
#define HTBLOGK(SF, args...) do { if (HTB_GROUP_ENABLED(SF)) HTBLogSimple((IMG_HANDLE) NULL, SF, ## args); } while (0)

/* Host Trace Buffer name */
#define HTB_STREAM_NAME	"PVRHTBuffer"

#else
#define HTBLOG(handle, SF, args...) do { if (HTB_GROUP_ENABLED(SF)) HTBLogSimple(handle, SF, ## args); } while (0)
#endif

/* macros to cast 64 or 32-bit pointers into 32-bit integer components for Host Trace */
#define HTBLOG_PTR_BITS_HIGH(p) ((IMG_UINT32)((((IMG_UINT64)((uintptr_t)p))>>32)&0xffffffff))
#define HTBLOG_PTR_BITS_LOW(p)  ((IMG_UINT32)(((IMG_UINT64)((uintptr_t)p))&0xffffffff))

/* macros to cast 64-bit integers into 32-bit integer components for Host Trace */
#define HTBLOG_U64_BITS_HIGH(u) ((IMG_UINT32)((u>>32)&0xffffffff))
#define HTBLOG_U64_BITS_LOW(u)  ((IMG_UINT32)(u&0xffffffff))

/*************************************************************************/ /*!
 @Function      HTBLog
 @Description   Record a Host Trace Buffer log event

 @Input         PID             The PID of the process the event is associated
                                with. This is provided as an argument rather
                                than querying internally so that events associated
                                with a particular process, but performed by
                                another can be logged correctly.

 @Input         TID             The TID (Thread ID) of the thread the event is
                                associated with.

 @Input         TimeStampus     The timestamp in us for this event

 @Input         SF              The log event ID

 @Input         ...             Log parameters

 @Return        PVRSRV_OK       Success.

*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HTBLog(IMG_HANDLE hSrvHandle, IMG_UINT32 PID, IMG_UINT32 TID, IMG_UINT64 ui64TimeStampns, IMG_UINT32 SF, ...);


/*************************************************************************/ /*!
 @Function      HTBLogSimple
 @Description   Record a Host Trace Buffer log event with implicit PID and Timestamp

 @Input         SF              The log event ID

 @Input         ...             Log parameters

 @Return        PVRSRV_OK       Success.

*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
HTBLogSimple(IMG_HANDLE hSrvHandle, IMG_UINT32 SF, ...);



/*  DEBUG log group enable */
#if !defined(HTB_DEBUG_LOG_GROUP)
#undef HTB_LOG_TYPE_DBG    /* No trace statements in this log group should be checked in */
#define HTB_LOG_TYPE_DBG    __BUILDERROR__
#endif


#if defined(__cplusplus)
}
#endif

#endif /* HTBUFFER_H */
/*****************************************************************************
 End of file (htbuffer.h)
*****************************************************************************/
