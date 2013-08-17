/*************************************************************************/ /*!
@Title          Parameter dump internal common functions
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


#ifndef __PDUMP_INT_H__
#define __PDUMP_INT_H__

#if defined (__cplusplus)
extern "C" {
#endif

/*
 *	This file contains internal pdump utility functions which may be accessed
 *	from OS-specific code. The header should not be included outside of srvkm
 *	pdump files.
 */

#if !defined(_UITRON)
/*
 *	No dbgdriver on uitron, so ignore any common functions for communicating
 *	with dbgdriver.
 */
#include "dbgdrvif.h"

/* Callbacks which are registered with the debug driver. */
IMG_EXPORT IMG_VOID PDumpConnectionNotify(IMG_VOID);

#endif /* !defined(_UITRON) */

typedef enum
{
	/* Continuous writes are always captured in the dbgdrv; the buffer will
	 * expand if no client/sink process is running.
	 */
	PDUMP_WRITE_MODE_CONTINUOUS = 0,
	/* Last frame capture */
	PDUMP_WRITE_MODE_LASTFRAME,
	/* Capture frame, binary data */
	PDUMP_WRITE_MODE_BINCM,
	/* Persistent capture, append data to init phase */
	PDUMP_WRITE_MODE_PERSISTENT
} PDUMP_DDWMODE;


IMG_UINT32 DbgWrite(PDBG_STREAM psStream, IMG_UINT8 *pui8Data, IMG_UINT32 ui32BCount, IMG_UINT32 ui32Flags);

IMG_UINT32 PDumpOSDebugDriverWrite(	PDBG_STREAM psStream,
									PDUMP_DDWMODE eDbgDrvWriteMode,
									IMG_UINT8 *pui8Data,
									IMG_UINT32 ui32BCount,
									IMG_UINT32 ui32Level,
									IMG_UINT32 ui32DbgDrvFlags);

#if defined (__cplusplus)
}
#endif
#endif /* __PDUMP_INT_H__ */

/******************************************************************************
 End of file (pdump_int.h)
******************************************************************************/

