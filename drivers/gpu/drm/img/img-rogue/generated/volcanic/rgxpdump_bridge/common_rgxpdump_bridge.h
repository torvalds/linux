/*******************************************************************************
@File
@Title          Common bridge header for rgxpdump
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxpdump
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
*******************************************************************************/

#ifndef COMMON_RGXPDUMP_BRIDGE_H
#define COMMON_RGXPDUMP_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"

#define PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPTRACEBUFFER			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPSIGNATUREBUFFER			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPCOMPUTECRCSIGNATURECHECK			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPCRCSIGNATURECHECK			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPRECOMMAND			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPOSTCOMMAND			PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXPDUMP_CMD_LAST			(PVRSRV_BRIDGE_RGXPDUMP_CMD_FIRST+5)

/*******************************************
            PDumpTraceBuffer
 *******************************************/

/* Bridge in structure for PDumpTraceBuffer */
typedef struct PVRSRV_BRIDGE_IN_PDUMPTRACEBUFFER_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPTRACEBUFFER;

/* Bridge out structure for PDumpTraceBuffer */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPTRACEBUFFER_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPTRACEBUFFER;

/*******************************************
            PDumpSignatureBuffer
 *******************************************/

/* Bridge in structure for PDumpSignatureBuffer */
typedef struct PVRSRV_BRIDGE_IN_PDUMPSIGNATUREBUFFER_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPSIGNATUREBUFFER;

/* Bridge out structure for PDumpSignatureBuffer */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPSIGNATUREBUFFER_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPSIGNATUREBUFFER;

/*******************************************
            PDumpComputeCRCSignatureCheck
 *******************************************/

/* Bridge in structure for PDumpComputeCRCSignatureCheck */
typedef struct PVRSRV_BRIDGE_IN_PDUMPCOMPUTECRCSIGNATURECHECK_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPCOMPUTECRCSIGNATURECHECK;

/* Bridge out structure for PDumpComputeCRCSignatureCheck */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPCOMPUTECRCSIGNATURECHECK_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPCOMPUTECRCSIGNATURECHECK;

/*******************************************
            PDumpCRCSignatureCheck
 *******************************************/

/* Bridge in structure for PDumpCRCSignatureCheck */
typedef struct PVRSRV_BRIDGE_IN_PDUMPCRCSIGNATURECHECK_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPCRCSIGNATURECHECK;

/* Bridge out structure for PDumpCRCSignatureCheck */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPCRCSIGNATURECHECK_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPCRCSIGNATURECHECK;

/*******************************************
            PDumpValCheckPreCommand
 *******************************************/

/* Bridge in structure for PDumpValCheckPreCommand */
typedef struct PVRSRV_BRIDGE_IN_PDUMPVALCHECKPRECOMMAND_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPVALCHECKPRECOMMAND;

/* Bridge out structure for PDumpValCheckPreCommand */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPRECOMMAND_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPRECOMMAND;

/*******************************************
            PDumpValCheckPostCommand
 *******************************************/

/* Bridge in structure for PDumpValCheckPostCommand */
typedef struct PVRSRV_BRIDGE_IN_PDUMPVALCHECKPOSTCOMMAND_TAG
{
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_PDUMPVALCHECKPOSTCOMMAND;

/* Bridge out structure for PDumpValCheckPostCommand */
typedef struct PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPOSTCOMMAND_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPOSTCOMMAND;

#endif /* COMMON_RGXPDUMP_BRIDGE_H */
