/*************************************************************************/ /*!
@Title          Timed Trace header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Timed Trace token header. Contains defines for all the tokens 
                used.
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

#ifndef __TTRACE_TOKENS_H__
#define __TTRACE_TOKENS_H__

/* All defines should use decimal so to not confuse the post processing tool */

/* Trace groups */
#define PVRSRV_TRACE_GROUP_KICK		0
#define PVRSRV_TRACE_GROUP_TRANSFER	1
#define PVRSRV_TRACE_GROUP_QUEUE	2
#define PVRSRV_TRACE_GROUP_POWER	3
#define PVRSRV_TRACE_GROUP_MKSYNC	4

#define PVRSRV_TRACE_GROUP_PADDING	255

/* Trace classes */
#define PVRSRV_TRACE_CLASS_FUNCTION_ENTER	0
#define PVRSRV_TRACE_CLASS_FUNCTION_EXIT	1
#define PVRSRV_TRACE_CLASS_SYNC			2
#define PVRSRV_TRACE_CLASS_CCB			3
#define PVRSRV_TRACE_CLASS_CMD_START		4
#define PVRSRV_TRACE_CLASS_CMD_END		5
#define PVRSRV_TRACE_CLASS_CMD_COMP_START	6
#define PVRSRV_TRACE_CLASS_CMD_COMP_END		7
#define PVRSRV_TRACE_CLASS_FLAGS			8

#define PVRSRV_TRACE_CLASS_NONE			255

/* Operation about to happen on the sync object */
#define PVRSRV_SYNCOP_SAMPLE		0
#define PVRSRV_SYNCOP_COMPLETE		1
#define PVRSRV_SYNCOP_DUMP		2

/*
 * Trace tokens
 * ------------
 * These only need to unique within a group.
 */

/* Kick group tokens */
#define KICK_TOKEN_DOKICK		0
#define KICK_TOKEN_CCB_OFFSET		1
#define KICK_TOKEN_TA3D_SYNC		2
#define KICK_TOKEN_TA_SYNC		3
#define KICK_TOKEN_3D_SYNC		4
#define KICK_TOKEN_SRC_SYNC		5
#define KICK_TOKEN_DST_SYNC		6
#define KICK_TOKEN_FIRST_KICK	7
#define KICK_TOKEN_LAST_KICK	8

/* Transfer Queue group tokens */
#define TRANSFER_TOKEN_SUBMIT		0
#define TRANSFER_TOKEN_TA_SYNC		1
#define TRANSFER_TOKEN_3D_SYNC		2
#define TRANSFER_TOKEN_SRC_SYNC		3
#define TRANSFER_TOKEN_DST_SYNC		4
#define TRANSFER_TOKEN_CCB_OFFSET	5

/* Queue group tokens */
#define QUEUE_TOKEN_GET_SPACE		0
#define QUEUE_TOKEN_INSERTKM		1
#define QUEUE_TOKEN_SUBMITKM		2
#define QUEUE_TOKEN_PROCESS_COMMAND	3
#define QUEUE_TOKEN_PROCESS_QUEUES	4
#define QUEUE_TOKEN_COMMAND_COMPLETE	5
#define QUEUE_TOKEN_UPDATE_DST		6
#define QUEUE_TOKEN_UPDATE_SRC		7
#define QUEUE_TOKEN_SRC_SYNC		8
#define QUEUE_TOKEN_DST_SYNC		9
#define QUEUE_TOKEN_COMMAND_TYPE	10

/* uKernel Sync tokens */
#define MKSYNC_TOKEN_KERNEL_CCB_OFFSET	0
#define MKSYNC_TOKEN_CORE_CLK		1
#define MKSYNC_TOKEN_UKERNEL_CLK	2

#endif /* __TTRACE_TOKENS_H__ */
