/*************************************************************************/ /*!
@File           rgxshader.h
@Title          TQ Shader Load
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Shader code and info are shared for all context on the device.
                If allocation doesn't already exist, read shader data from file
                and allocate PMR memory. PMR memory is not deallocated until
                device deinit.
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

#if !defined(RGXSHADER_H)
#define RGXSHADER_H

#include "fwload.h"
#include "rgxtransfer_shader.h"
#include "connection_server.h"

/*************************************************************************/ /*!
@Function       PVRSRVTQLoadShaders
@Description    If PMR is not allocated, reads shader binary data from file
                and allocates new PMR memory.
@Input          psDeviceNode Device node
@Return         PVRSRV_ERROR Returns PVRSRV_OK on success.
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVTQLoadShaders(PVRSRV_DEVICE_NODE *psDeviceNode);

/*************************************************************************/ /*!
@Function       PVRSRVTQAcquireShaders
@Description    Get handle to ready allocated shader PMR memory
@Input          psDeviceNode Device node
@Output         ppsCLIPMRMem Shader data used by CPU client side.
@Output         ppsUSCPMRMem Shader usc code used by GPU.
*/ /**************************************************************************/
void
PVRSRVTQAcquireShaders(PVRSRV_DEVICE_NODE  *psDeviceNode,
                       PMR                **ppsCLIPMRMem,
                       PMR                **ppsUSCPMRMem);

/*************************************************************************/ /*!
@Function       PVRSRVTQUnLoadShaders
@Description    Unref PMR memory.
@Input          psDeviceNode Device node
*/ /**************************************************************************/
void PVRSRVTQUnloadShaders(PVRSRV_DEVICE_NODE *psDeviceNode);

#endif /* RGXSHADER_H */
