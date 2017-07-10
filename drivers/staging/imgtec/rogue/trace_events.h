/*************************************************************************/ /*!
@Title          Linux trace events and event helper functions
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

#if !defined(TRACE_EVENTS_H)
#define TRACE_EVENTS_H

#include "rgx_fwif_km.h"
#include "rgx_hwperf_km.h"

/* We need to make these functions do nothing if CONFIG_EVENT_TRACING isn't
 * enabled, just like the actual trace event functions that the kernel
 * defines for us.
 */
#ifdef CONFIG_EVENT_TRACING
bool trace_rogue_are_fence_checks_traced(void);

bool trace_rogue_are_fence_updates_traced(void);

void trace_rogue_fence_updates(const char *cmd, const char *dm,
							   IMG_UINT32 ui32FWContext,
							   IMG_UINT32 ui32Offset,
							   IMG_UINT uCount,
							   PRGXFWIF_UFO_ADDR *pauiAddresses,
							   IMG_UINT32 *paui32Values);

void trace_rogue_fence_checks(const char *cmd, const char *dm,
							  IMG_UINT32 ui32FWContext,
							  IMG_UINT32 ui32Offset,
							  IMG_UINT uCount,
							  PRGXFWIF_UFO_ADDR *pauiAddresses,
							  IMG_UINT32 *paui32Values);

void trace_rogue_ufo_updates(IMG_UINT64 ui64OSTimestamp,
							 IMG_UINT32 ui32FWCtx,
							 IMG_UINT32 ui32JobId,
							 IMG_UINT32 ui32UFOCount,
							 const RGX_HWPERF_UFO_DATA_ELEMENT *puData);

void trace_rogue_ufo_checks_success(IMG_UINT64 ui64OSTimestamp,
									IMG_UINT32 ui32FWCtx,
									IMG_UINT32 ui32JobId,
									IMG_BOOL bPrEvent,
									IMG_UINT32 ui32UFOCount,
									const RGX_HWPERF_UFO_DATA_ELEMENT *puData);

void trace_rogue_ufo_checks_fail(IMG_UINT64 ui64OSTimestamp,
								 IMG_UINT32 ui32FWCtx,
								 IMG_UINT32 ui32JobId,
								 IMG_BOOL bPrEvent,
								 IMG_UINT32 ui32UFOCount,
								 const RGX_HWPERF_UFO_DATA_ELEMENT *puData);

#else  /* CONFIG_TRACE_EVENTS */
static inline
bool trace_rogue_are_fence_checks_traced(void)
{
	return false;
}

static inline
bool trace_rogue_are_fence_updates_traced(void)
{
	return false;
}

static inline
void trace_rogue_fence_updates(const char *cmd, const char *dm,
							   IMG_UINT32 ui32FWContext,
							   IMG_UINT32 ui32Offset,
							   IMG_UINT uCount,
							   PRGXFWIF_UFO_ADDR *pauiAddresses,
							   IMG_UINT32 *paui32Values)
{
}

static inline
void trace_rogue_fence_checks(const char *cmd, const char *dm,
							  IMG_UINT32 ui32FWContext,
							  IMG_UINT32 ui32Offset,
							  IMG_UINT uCount,
							  PRGXFWIF_UFO_ADDR *pauiAddresses,
							  IMG_UINT32 *paui32Values)
{
}

static inline
void trace_rogue_ufo_updates(IMG_UINT64 ui64OSTimestamp,
							 IMG_UINT32 ui32FWCtx,
							 IMG_UINT32 ui32JobId,
							 IMG_UINT32 ui32UFOCount,
							 const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
}

static inline
void trace_rogue_ufo_checks_success(IMG_UINT64 ui64OSTimestamp,
									IMG_UINT32 ui32FWCtx,
									IMG_UINT32 ui32JobId,
									IMG_BOOL bPrEvent,
									IMG_UINT32 ui32UFOCount,
									const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
}

static inline
void trace_rogue_ufo_checks_fail(IMG_UINT64 ui64OSTimestamp,
								 IMG_UINT32 ui32FWCtx,
								 IMG_UINT32 ui32JobId,
								 IMG_BOOL bPrEvent,
								 IMG_UINT32 ui32UFOCount,
								 const RGX_HWPERF_UFO_DATA_ELEMENT *puData)
{
}
#endif /* CONFIG_TRACE_EVENTS */

#endif /* TRACE_EVENTS_H */
