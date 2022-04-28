/*************************************************************************/ /*!
@File           htbuffer_types.h
@Title          Host Trace Buffer types.
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
#ifndef HTBUFFER_TYPES_H
#define HTBUFFER_TYPES_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "htbuffer_sf.h"

/* The group flags array of ints large enough to store all the group flags */
#define HTB_FLAG_NUM_EL (((HTB_GROUP_DBG-1) / HTB_FLAG_NUM_BITS_IN_EL) + 1)
extern IMG_INTERNAL HTB_FLAG_EL_T g_auiHTBGroupEnable[HTB_FLAG_NUM_EL];

#define HTB_GROUP_ENABLED(SF) (g_auiHTBGroupEnable[HTB_LOG_GROUP_FLAG_GROUP(HTB_SF_GID(SF))] & HTB_LOG_GROUP_FLAG(HTB_SF_GID(SF)))

/*************************************************************************/ /*!
 Host Trace Buffer operation mode
 Care must be taken if changing this enum to ensure the MapFlags[] array
 in htbserver.c is kept in-step.
*/ /**************************************************************************/
typedef enum
{
	/*! Undefined operation mode */
	HTB_OPMODE_UNDEF = 0,

	/*! Drop latest, intended for continuous logging to a UM daemon.
	 *  If the daemon does not keep up, the most recent log data
	 *  will be dropped
	 */
	HTB_OPMODE_DROPLATEST,

	/*! Drop oldest, intended for crash logging.
	 *  Data will be continuously written to a circular buffer.
	 *  After a crash the buffer will contain events leading up to the crash
	 */
	HTB_OPMODE_DROPOLDEST,

	/*! Block write if buffer is full */
	HTB_OPMODE_BLOCK,

	HTB_OPMODE_LAST = HTB_OPMODE_BLOCK
} HTB_OPMODE_CTRL;


/*************************************************************************/ /*!
 Host Trace Buffer log mode control
*/ /**************************************************************************/
typedef enum
{
	/*! Undefined log mode, used if update is not applied */
	HTB_LOGMODE_UNDEF = 0,

	/*! Log trace messages for all PIDs. */
	HTB_LOGMODE_ALLPID,

	/*! Log trace messages for specific PIDs only. */
	HTB_LOGMODE_RESTRICTEDPID,

	HTB_LOGMODE_LAST = HTB_LOGMODE_RESTRICTEDPID
} HTB_LOGMODE_CTRL;


#if defined(__cplusplus)
}
#endif

#endif /* HTBUFFER_TYPES_H */

/******************************************************************************
 End of file (htbuffer_types.h)
******************************************************************************/
