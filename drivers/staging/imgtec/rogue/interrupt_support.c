/*************************************************************************/ /*!
@File
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

#include <linux/interrupt.h>

#include "pvr_debug.h"
#include "allocmem.h"
#include "interrupt_support.h"

typedef struct LISR_DATA_TAG
{
	IMG_UINT32	ui32IRQ;
	PFN_SYS_LISR	pfnLISR;
	void		*pvData;
} LISR_DATA;

static irqreturn_t SystemISRWrapper(int irq, void *dev_id)
{
	LISR_DATA *psLISRData = (LISR_DATA *)dev_id;

	PVR_UNREFERENCED_PARAMETER(irq);

	if (psLISRData)
	{
		if (psLISRData->pfnLISR(psLISRData->pvData))
		{
			return IRQ_HANDLED;
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Missing interrupt data", __FUNCTION__));
	}

	return IRQ_NONE;
}

PVRSRV_ERROR OSInstallSystemLISR(IMG_HANDLE *phLISR,
				 IMG_UINT32 ui32IRQ,
				 const IMG_CHAR *pszDevName,
				 PFN_SYS_LISR pfnLISR,
				 void *pvData,
				 IMG_UINT32 ui32Flags)
{
	LISR_DATA *psLISRData;
	unsigned long ulIRQFlags = 0;

	if (pfnLISR == NULL || pvData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32Flags & ~SYS_IRQ_FLAG_MASK)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	switch (ui32Flags & SYS_IRQ_FLAG_TRIGGER_MASK)
	{
		case SYS_IRQ_FLAG_TRIGGER_DEFAULT:
			break;
		case SYS_IRQ_FLAG_TRIGGER_LOW:
			ulIRQFlags |= IRQF_TRIGGER_LOW;
			break;
		case SYS_IRQ_FLAG_TRIGGER_HIGH:
			ulIRQFlags |= IRQF_TRIGGER_HIGH;
			break;
		default:
			return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32Flags & SYS_IRQ_FLAG_SHARED)
	{
		ulIRQFlags |= IRQF_SHARED;
	}

	psLISRData = OSAllocMem(sizeof *psLISRData);
	if (psLISRData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psLISRData->ui32IRQ = ui32IRQ;
	psLISRData->pfnLISR = pfnLISR;
	psLISRData->pvData = pvData;

	if (request_irq(ui32IRQ, SystemISRWrapper, ulIRQFlags, pszDevName, psLISRData))
	{
		OSFreeMem(psLISRData);

		return PVRSRV_ERROR_UNABLE_TO_REGISTER_ISR_HANDLER;
	}

	*phLISR = (IMG_HANDLE)psLISRData;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSUninstallSystemLISR(IMG_HANDLE hLISR)
{
	LISR_DATA *psLISRData = (LISR_DATA *)hLISR;

	if (psLISRData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	free_irq(psLISRData->ui32IRQ, psLISRData);

	OSFreeMem(psLISRData);

	return PVRSRV_OK;
}
