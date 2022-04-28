/*************************************************************************/ /*!
@File           vz_vmm_pvz.h
@Title          System virtualization VM manager management APIs
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides VM manager para-virtz management APIs
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

#ifndef VZ_VMM_PVZ_H
#define VZ_VMM_PVZ_H

#include "img_types.h"
#include "vmm_impl.h"

/*!
*******************************************************************************
 @Function      PvzConnectionInit() and PvzConnectionDeInit()
 @Description   PvzConnectionInit initializes the VM manager para-virt
                which is used subsequently for communication between guest and
                host; depending on the underlying VM setup, this could either
                be a hyper-call or cross-VM call
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR PvzConnectionInit(PVRSRV_DEVICE_CONFIG *psDevConfig);
void PvzConnectionDeInit(void);

/*!
*******************************************************************************
 @Function      PvzConnectionAcquire() and PvzConnectionRelease()
 @Description   These are to acquire/release a handle to the VM manager
                para-virtz connection to make a pvz call; on the client, use it
                it to make the actual pvz call and on the server handler /
                VM manager, use it to complete the processing for the pvz call
                or make a VM manager to host pvzbridge call
@Return         VMM_PVZ_CONNECTION* on success. Otherwise NULL
******************************************************************************/
VMM_PVZ_CONNECTION* PvzConnectionAcquire(void);
void PvzConnectionRelease(VMM_PVZ_CONNECTION *psPvzConnection);

#endif /* VZ_VMM_PVZ_H */

/******************************************************************************
 End of file (vz_vmm_pvz.h)
******************************************************************************/
