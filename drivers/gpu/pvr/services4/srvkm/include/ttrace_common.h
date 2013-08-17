/*************************************************************************/ /*!
@Title          Timed Trace header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Timed Trace common header. Contains shared defines and 
                structures which are shared with the post processing tool.
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
#include "img_types.h"

#ifndef __TTRACE_COMMON_H__
#define __TTRACE_COMMON_H__

/*
 * Trace item
 * ==========
 *
 * A trace item contains a trace header, a timestamp, a UID and a
 * data header all of which are 32-bit and mandatory. If there
 * is no data then the data header size is set to 0.
 *
 * Trace header
 * ------------
 * 31   27   23   19   15   11   7    3
 * GGGG GGGG CCCC CCCC TTTT TTTT TTTT TTTT
 *
 * G = group
 *     Note:
 *     Group 0xff means the message is padding
 *
 * C = class
 * T = Token
 *
 * Data header
 *-----------
 * 31   27   23   19   15   11   7    3
 * SSSS SSSS SSSS SSSS TTTT CCCC CCCC CCCC
 *
 * S = data packet size
 * T = Type
 *		0000 - 8 bit
 *		0001 - 16 bit
 *		0010 - 32 bit
 *		0011 - 64 bit
 *
 * C = data item count
 *
 * Note: It might look strange having both the packet
 *       size and the data item count, but the idea
 *       is the you might have a "special" data type
 *       who's size might not be known by the post
 *       processing program and rather then fail
 *       processing the buffer after that point if we
 *       know the size we can just skip it and move to
 *       the next item.
 */


#define PVRSRV_TRACE_HEADER		0
#define PVRSRV_TRACE_TIMESTAMP		1
#define PVRSRV_TRACE_HOSTUID		2
#define PVRSRV_TRACE_DATA_HEADER	3
#define PVRSRV_TRACE_DATA_PAYLOAD	4

#define PVRSRV_TRACE_ITEM_SIZE		16

#define PVRSRV_TRACE_GROUP_MASK		0xff
#define PVRSRV_TRACE_CLASS_MASK		0xff
#define PVRSRV_TRACE_TOKEN_MASK		0xffff

#define PVRSRV_TRACE_GROUP_SHIFT	24
#define PVRSRV_TRACE_CLASS_SHIFT	16
#define PVRSRV_TRACE_TOKEN_SHIFT	0

#define PVRSRV_TRACE_SIZE_MASK		0xffff
#define PVRSRV_TRACE_TYPE_MASK		0xf
#define PVRSRV_TRACE_COUNT_MASK		0xfff

#define PVRSRV_TRACE_SIZE_SHIFT		16
#define PVRSRV_TRACE_TYPE_SHIFT		12
#define PVRSRV_TRACE_COUNT_SHIFT	0


#define WRITE_HEADER(n,m) \
	((m & PVRSRV_TRACE_##n##_MASK) << PVRSRV_TRACE_##n##_SHIFT)

#define READ_HEADER(n,m) \
	((m & (PVRSRV_TRACE_##n##_MASK << PVRSRV_TRACE_##n##_SHIFT)) >> PVRSRV_TRACE_##n##_SHIFT)

#define TIME_TRACE_BUFFER_SIZE		4096

/* Type defines for trace items */
#define PVRSRV_TRACE_TYPE_UI8		0
#define PVRSRV_TRACE_TYPE_UI16		1
#define PVRSRV_TRACE_TYPE_UI32		2
#define PVRSRV_TRACE_TYPE_UI64		3

#define PVRSRV_TRACE_TYPE_SYNC		15
 #define PVRSRV_TRACE_SYNC_UID		0
 #define PVRSRV_TRACE_SYNC_WOP		1
 #define PVRSRV_TRACE_SYNC_WOC		2
 #define PVRSRV_TRACE_SYNC_ROP		3
 #define PVRSRV_TRACE_SYNC_ROC		4
 #define PVRSRV_TRACE_SYNC_WO_DEV_VADDR	5
 #define PVRSRV_TRACE_SYNC_RO_DEV_VADDR	6
 #define PVRSRV_TRACE_SYNC_OP		7
 #define PVRSRV_TRACE_SYNC_RO2P		8
 #define PVRSRV_TRACE_SYNC_RO2C		9
 #define PVRSRV_TRACE_SYNC_RO2_DEV_VADDR 10
#define PVRSRV_TRACE_TYPE_SYNC_SIZE	((PVRSRV_TRACE_SYNC_RO2_DEV_VADDR + 1) * sizeof(IMG_UINT32))

#endif /* __TTRACE_COMMON_H__*/
