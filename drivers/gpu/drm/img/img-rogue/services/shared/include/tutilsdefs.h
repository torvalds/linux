/*************************************************************************/ /*!
@File           tutilsdefs.h
@Title          Testing utils bridge defines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Shared structures and constants between client and server sides
                of tutils bridge
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
#ifndef TUTILSDEFS_H
#define TUTILSDEFS_H


#include "pvrsrv_tlcommon.h"
#include "pvrsrv_sync_km.h"

/******************************************************************************
 *
 * TEST Related definitions and constants
 */
#define PVR_TL_TEST_STREAM_BRIDGE_NAME "TLBRIDGE_TEST"
#define PVR_TL_TEST_UMBASE             0x00202000
#define PVR_TL_TEST_OFFSET             0x0008
#define PVR_TL_TEST_LEN                0x0010

#define PVR_TL_TEST_STREAM2_NAME	"TLSTREAM2_TEST"
#define PVR_TL_TEST_STREAM2_SIZE	2

#define PVR_TL_TEST_STREAM3_NAME	"TLSTREAM3_TEST"
#define PVR_TL_TEST_STREAM3_SIZE	256

// This constant, when used as a parameter in StreamCreate, lessens the size of
// the buffer that is created for a stream, to avoid going over a page boundary.
#define PVR_TL_TEST_STREAM_BUFFER_REDUCTION 32

#define PVR_TL_TEST_CMD_SOURCE_START			10
typedef struct _PVR_TL_TEST_CMD_SOURCE_START_IN_
{
	/* Stream name must always be first in struct */
	IMG_CHAR    pszStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
	IMG_UINT16  uiStreamSizeInPages;  /* # of 4Kb pages */
	IMG_UINT16  uiInterval;           /* in milliseconds */
	IMG_UINT16  uiCallbackKicks;      /* 0 for no limit of timer call backs */
	IMG_UINT16  uiEOSMarkerKicks;     /* Insert EOS Marker every N Kicks, 0 for none */
	IMG_UINT16  uiPacketSizeInBytes;  /* 0 for random size between 1..255 size in bytes */
	IMG_UINT32  uiStreamCreateFlags;  /* See TLStreamCreate() */
	IMG_UINT16  uiStartDelay;         /* 0 for normal uiInterval delay, one off delay in ms */
	IMG_BOOL    bDoNotDeleteStream;   /* When true the stream is not deleted on self
	                                   * cleanup sources only the timers and other resources are */
	IMG_BOOL    bDelayStreamCreate;   /* When true the stream used in the source is created
	                                   * in the first kick. False for normal behaviour where
	                                   * the stream is created in the bridge source start context */
} PVR_TL_TEST_CMD_SOURCE_START_IN;


#define PVR_TL_TEST_CMD_SOURCE_STOP		11
typedef struct _PVR_TL_TEST_CMD_SOURCE_STOP_IN_
{
	/* Stream name must always be first in struct */
	IMG_CHAR  pszStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
	IMG_BOOL  bDoNotDeleteStream;
} PVR_TL_TEST_CMD_SOURCE_STOP_IN;

#define PVR_TL_TEST_CMD_SOURCE_START2	12	/* Uses two stage data submit */
typedef PVR_TL_TEST_CMD_SOURCE_START_IN PVR_TL_TEST_CMD_SOURCE_START2_IN;

#define PVR_TL_TEST_CMD_DEBUG_LEVEL	13
/* No typedef, uses integer uiIn1 in union */

#define PVR_TL_TEST_CMD_DUMP_TL_STATE	14
/* No typedef, uses integer uiIn1 in union */

#define PVR_TL_TEST_CMD_STREAM_CREATE	15
typedef struct _PVR_TL_TEST_CMD_STREAM_CREATE_IN_
{
	/* Stream name must always be first in struct */
	IMG_CHAR    pszStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
	IMG_UINT16  uiStreamSizeInPages;
	IMG_UINT32  uiStreamCreateFlags;
	IMG_BOOL    bWithOpenCallback;
} PVR_TL_TEST_CMD_STREAM_CREATE_IN;

#define PVR_TL_TEST_CMD_STREAM_CLOSE	16
typedef struct _PVR_TL_TEST_CMD_STREAM_NAME_IN_
{
	IMG_CHAR  pszStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
} PVR_TL_TEST_CMD_STREAM_NAME_IN;

#define PVR_TL_TEST_CMD_STREAM_OPEN 17

#define PVR_TL_TEST_CMD_DUMP_HWPERF_STATE 18

#define PVR_TL_TEST_CMD_FLUSH_HWPERF_FWBUF 19

#define PVR_TL_TEST_CMD_DUMP_PDUMP_STATE 21

typedef union _PVR_TL_TEST_CMD_IN_
{
	PVR_TL_TEST_CMD_SOURCE_START_IN sStart;
	PVR_TL_TEST_CMD_SOURCE_STOP_IN  sStop;
/*	PVR_TL_TEST_CMD_SOURCE_START_IN sStart2;  Used by #12, use sStart instead */
	IMG_UINT32	uiIn1;						 /* Used by #13, #14 */
	PVR_TL_TEST_CMD_STREAM_CREATE_IN  sCreate;
	PVR_TL_TEST_CMD_STREAM_NAME_IN sName;
	IMG_UINT32 uiParams[6];
} PVR_TL_TEST_CMD_IN;

/* Has to be the largest test IN structure */
#define PVR_TL_TEST_PARAM_MAX_SIZE  (sizeof(PVR_TL_TEST_CMD_IN)+4)

#define PVR_TL_TEST_CMD_SET_PWR_STATE              22
#define PVR_TL_TEST_CMD_GET_PWR_STATE              23
#define PVR_TL_TEST_CMD_SET_DWT_PWR_CHANGE_COUNTER 24
#define PVR_TL_TEST_CMD_GET_DWT_PWR_CHANGE_COUNTER 25

#define PVR_TL_TEST_PWR_STATE_ON  1
#define PVR_TL_TEST_PWR_STATE_OFF 0

/****************************************************************************
 * PowMonTestThread IOCTL calls and constants
 */

#define PVR_POWMON_CMD_GET_ESTIMATES	1
#define PVR_POWMON_CMD_SET_THREAD_LATENCY	2
#define PVR_POWMON_CMD_TEST_THREAD_UPDATE_STATE	3

#define PVR_POWMON_TEST_THREAD_RESUME	1
#define PVR_POWMON_TEST_THREAD_PAUSE	0

/****************************************************************************
 * PowerTestThread IOCTL calls and constants
 */

#define PVR_POWER_TEST_CMD_DVFS                 1
#define PVR_POWER_TEST_CMD_FORCED_IDLE          2
#define PVR_POWER_TEST_CMD_CANCEL_FORCED_IDLE   3
#define PVR_POWER_TEST_CMD_POWER_ON             4
#define PVR_POWER_TEST_CMD_POWER_OFF            5
#define PVR_POWER_TEST_CMD_APM_LATENCY          6
#define PVR_POWER_TEST_CMD_INVALID              7

#define PVR_POWER_TEST_NON_FORCED 0
#define PVR_POWER_TEST_FORCED     1

/****************************************************************************
 * SyncCheckpointTest IOCTL types
 */

#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_CONTEXT_CREATE		26
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_CONTEXT_DESTROY		27
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_REGISTER_FUNCS		28
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_CREATE				29
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_CREATE_NULL_CTXT	30
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_CREATE_NULL_RTRN	31
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_DESTROY				32
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_SIGNAL				33
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_ERROR				34
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_IS_SIGNALLED		35
#define PVR_TL_TEST_CMD_SYNC_CHECKPOINT_IS_ERRORED			36

typedef struct _PVR_TL_TEST_CMD_CHECKPOINT_CREATE_IN_
{
	/* Checkpoint name must always be first in struct */
	IMG_CHAR    pszCheckpointName[PVRSRV_SYNC_NAME_LENGTH];
	IMG_UINT16  uiStreamSizeInPages;
	IMG_UINT32  uiStreamCreateFlags;
} PVR_TL_TEST_CMD_CHECKPOINT_CREATE_IN;

#define PVR_TL_TEST_CMD_SET_STREAM_OPEN_COUNTER 37
#define PVR_TL_TEST_CMD_GET_STREAM_OPEN_COUNTER 38

typedef struct _PVR_TL_TEST_CMD_STREAM_OPEN_COUNTER_IN_
{
	IMG_CHAR    pszStreamName[PRVSRVTL_MAX_STREAM_NAME_SIZE];
	IMG_UINT32  ui32Counter;
} PVR_TL_TEST_CMD_STREAM_OPEN_COUNTER_IN;

/****************************************************************************
 * KmallocThreshold IOCTL types
 */

#define PVR_TL_TEST_CMD_KMALLOC 39

typedef struct _PVR_TL_TEST_CMD_KMALLOC_IN_
{
	IMG_UINT32 uiAllocCount;
	IMG_UINT32 uiAllocSize;
	IMG_UINT32 uiFailedAllocThreshold;
	IMG_UINT32 uiFailedAllocFrequency;
} PVR_TL_TEST_CMD_KMALLOC_IN;

#endif /* TUTILSDEFS_H */

/******************************************************************************
 End of file (tutilsdefs.h)
******************************************************************************/
