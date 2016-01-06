/*************************************************************************/ /*!
@Title          Linux trace event helper functions
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

#include <linux/sched.h>

#include "img_types.h"
#include "rgx_fwif_km.h"

#define CREATE_TRACE_POINTS
#include "trace_events.h"

/* This is a helper that calls trace_rogue_fence_update for each fence in an
 * array.
 */
void trace_rogue_fence_updates(const char *cmd, const char *dm, IMG_UINT32 ui32FWContext,
							   IMG_UINT32 ui32Offset,
							   IMG_UINT uCount,
							   PRGXFWIF_UFO_ADDR *pauiAddresses,
							   IMG_UINT32 *paui32Values)
{
	IMG_UINT i;
	for (i = 0; i < uCount; i++)
	{
		trace_rogue_fence_update(current->comm, cmd, dm, ui32FWContext, ui32Offset,
								 pauiAddresses[i].ui32Addr, paui32Values[i]);
	}
}

void trace_rogue_fence_checks(const char *cmd, const char *dm, IMG_UINT32 ui32FWContext,
							  IMG_UINT32 ui32Offset,
							  IMG_UINT uCount,
							  PRGXFWIF_UFO_ADDR *pauiAddresses,
							  IMG_UINT32 *paui32Values)
{
	IMG_UINT i;
	for (i = 0; i < uCount; i++)
	{
		trace_rogue_fence_check(current->comm, cmd, dm, ui32FWContext, ui32Offset,
							  pauiAddresses[i].ui32Addr, paui32Values[i]);
	}
}
