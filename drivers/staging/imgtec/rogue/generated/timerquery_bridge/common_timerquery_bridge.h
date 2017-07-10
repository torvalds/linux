/*************************************************************************/ /*!
@File
@Title          Common bridge header for timerquery
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for timerquery
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

#ifndef COMMON_TIMERQUERY_BRIDGE_H
#define COMMON_TIMERQUERY_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"


#define PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST			0
#define PVRSRV_BRIDGE_TIMERQUERY_RGXBEGINTIMERQUERY			PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST+0
#define PVRSRV_BRIDGE_TIMERQUERY_RGXENDTIMERQUERY			PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST+1
#define PVRSRV_BRIDGE_TIMERQUERY_RGXQUERYTIMER			PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST+2
#define PVRSRV_BRIDGE_TIMERQUERY_RGXCURRENTTIME			PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST+3
#define PVRSRV_BRIDGE_TIMERQUERY_CMD_LAST			(PVRSRV_BRIDGE_TIMERQUERY_CMD_FIRST+3)


/*******************************************
            RGXBeginTimerQuery          
 *******************************************/

/* Bridge in structure for RGXBeginTimerQuery */
typedef struct PVRSRV_BRIDGE_IN_RGXBEGINTIMERQUERY_TAG
{
	IMG_UINT32 ui32QueryId;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXBEGINTIMERQUERY;

/* Bridge out structure for RGXBeginTimerQuery */
typedef struct PVRSRV_BRIDGE_OUT_RGXBEGINTIMERQUERY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXBEGINTIMERQUERY;


/*******************************************
            RGXEndTimerQuery          
 *******************************************/

/* Bridge in structure for RGXEndTimerQuery */
typedef struct PVRSRV_BRIDGE_IN_RGXENDTIMERQUERY_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXENDTIMERQUERY;

/* Bridge out structure for RGXEndTimerQuery */
typedef struct PVRSRV_BRIDGE_OUT_RGXENDTIMERQUERY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXENDTIMERQUERY;


/*******************************************
            RGXQueryTimer          
 *******************************************/

/* Bridge in structure for RGXQueryTimer */
typedef struct PVRSRV_BRIDGE_IN_RGXQUERYTIMER_TAG
{
	IMG_UINT32 ui32QueryId;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXQUERYTIMER;

/* Bridge out structure for RGXQueryTimer */
typedef struct PVRSRV_BRIDGE_OUT_RGXQUERYTIMER_TAG
{
	IMG_UINT64 ui64StartTime;
	IMG_UINT64 ui64EndTime;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXQUERYTIMER;


/*******************************************
            RGXCurrentTime          
 *******************************************/

/* Bridge in structure for RGXCurrentTime */
typedef struct PVRSRV_BRIDGE_IN_RGXCURRENTTIME_TAG
{
	 IMG_UINT32 ui32EmptyStructPlaceholder;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCURRENTTIME;

/* Bridge out structure for RGXCurrentTime */
typedef struct PVRSRV_BRIDGE_OUT_RGXCURRENTTIME_TAG
{
	IMG_UINT64 ui64Time;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCURRENTTIME;


#endif /* COMMON_TIMERQUERY_BRIDGE_H */
