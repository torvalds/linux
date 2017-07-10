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

#if !defined(__INTERRUPT_SUPPORT_H__)
#define __INTERRUPT_SUPPORT_H__

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_device.h"

#define SYS_IRQ_FLAG_TRIGGER_DEFAULT (0x0 << 0)
#define SYS_IRQ_FLAG_TRIGGER_LOW     (0x1 << 0)
#define SYS_IRQ_FLAG_TRIGGER_HIGH    (0x2 << 0)
#define SYS_IRQ_FLAG_TRIGGER_MASK    (SYS_IRQ_FLAG_TRIGGER_DEFAULT | \
                                      SYS_IRQ_FLAG_TRIGGER_LOW | \
                                      SYS_IRQ_FLAG_TRIGGER_HIGH)
#define SYS_IRQ_FLAG_SHARED          (0x1 << 8)

#define SYS_IRQ_FLAG_MASK            (SYS_IRQ_FLAG_TRIGGER_MASK | \
                                      SYS_IRQ_FLAG_SHARED)

typedef IMG_BOOL (*PFN_SYS_LISR)(void *pvData);

typedef struct _SYS_INTERRUPT_DATA_
{
	void			*psSysData;
	const IMG_CHAR	*pszName;
	PFN_SYS_LISR	pfnLISR;
	void			*pvData;
	IMG_UINT32		ui32InterruptFlag;
#if defined(SUPPORT_PVRSRV_GPUVIRT)
	IMG_UINT32		ui32IRQ;
#endif
} SYS_INTERRUPT_DATA;

/*************************************************************************/ /*!
@Function       OSInstallSystemLISR
@Description    Installs a system low-level interrupt handler
@Output         phLISR                  On return, contains a handle to the
                                        installed LISR
@Input          ui32IRQ                 The IRQ number for which the
                                        interrupt handler should be installed
@Input          pszDevName              Name of the device for which the handler
                                        is being installed
@Input          pfnLISR                 A pointer to an interrupt handler
                                        function
@Input          pvData                  A pointer to data that should be passed
                                        to pfnLISR when it is called
@Input          ui32Flags               Interrupt flags
@Return         PVRSRV_OK on success, a failure code otherwise
*/ /**************************************************************************/
PVRSRV_ERROR OSInstallSystemLISR(IMG_HANDLE *phLISR, 
				 IMG_UINT32 ui32IRQ,
				 const IMG_CHAR *pszDevName, 
				 PFN_SYS_LISR pfnLISR, 
				 void *pvData,
				 IMG_UINT32 ui32Flags);

/*************************************************************************/ /*!
@Function       OSUninstallSystemLISR
@Description    Uninstalls a system low-level interrupt handler
@Input          hLISRData              The handle to the LISR to uninstall
@Return         PVRSRV_OK on success, a failure code otherwise
*/ /**************************************************************************/
PVRSRV_ERROR OSUninstallSystemLISR(IMG_HANDLE hLISRData);
#endif /* !defined(__INTERRUPT_SUPPORT_H__) */
