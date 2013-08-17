/*************************************************************************/ /*!
@Title          buffer device class API structures and prototypes
                for kernel services to kernel 3rd party buffer device driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	provides display device class API structures and prototypes
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

#if !defined (__KERNELBUFFER_H__)
#define __KERNELBUFFER_H__

#if defined (__cplusplus)
extern "C" {
#endif

/*
	Function table and pointers for SRVKM->BUFFER
*/
typedef PVRSRV_ERROR (*PFN_OPEN_BC_DEVICE)(IMG_UINT32, IMG_HANDLE*);
typedef PVRSRV_ERROR (*PFN_CLOSE_BC_DEVICE)(IMG_UINT32, IMG_HANDLE);
typedef PVRSRV_ERROR (*PFN_GET_BC_INFO)(IMG_HANDLE, BUFFER_INFO*);
typedef PVRSRV_ERROR (*PFN_GET_BC_BUFFER)(IMG_HANDLE, IMG_UINT32, PVRSRV_SYNC_DATA*, IMG_HANDLE*);

typedef struct PVRSRV_BC_SRV2BUFFER_KMJTABLE_TAG
{
	IMG_UINT32							ui32TableSize;
	PFN_OPEN_BC_DEVICE					pfnOpenBCDevice;
	PFN_CLOSE_BC_DEVICE					pfnCloseBCDevice;
	PFN_GET_BC_INFO						pfnGetBCInfo;
	PFN_GET_BC_BUFFER					pfnGetBCBuffer;
	PFN_GET_BUFFER_ADDR					pfnGetBufferAddr;

} PVRSRV_BC_SRV2BUFFER_KMJTABLE;


/*
	Function table and pointers for BUFFER->SRVKM
*/
typedef PVRSRV_ERROR (*PFN_BC_REGISTER_BUFFER_DEV)(PVRSRV_BC_SRV2BUFFER_KMJTABLE*, IMG_UINT32*);
typedef IMG_VOID (*PFN_BC_SCHEDULE_DEVICES)(IMG_VOID);
typedef PVRSRV_ERROR (*PFN_BC_REMOVE_BUFFER_DEV)(IMG_UINT32);	

typedef struct PVRSRV_BC_BUFFER2SRV_KMJTABLE_TAG
{
	IMG_UINT32							ui32TableSize;
	PFN_BC_REGISTER_BUFFER_DEV			pfnPVRSRVRegisterBCDevice;
	PFN_BC_SCHEDULE_DEVICES				pfnPVRSRVScheduleDevices;
	PFN_BC_REMOVE_BUFFER_DEV			pfnPVRSRVRemoveBCDevice;

} PVRSRV_BC_BUFFER2SRV_KMJTABLE, *PPVRSRV_BC_BUFFER2SRV_KMJTABLE;

/* function to retrieve kernel services function table from kernel services */
typedef IMG_BOOL (*PFN_BC_GET_PVRJTABLE) (PPVRSRV_BC_BUFFER2SRV_KMJTABLE); 

/* Prototype for platforms that access the JTable via linkage */
IMG_IMPORT IMG_BOOL PVRGetBufferClassJTable(PVRSRV_BC_BUFFER2SRV_KMJTABLE *psJTable);

#if defined (__cplusplus)
}
#endif

#endif/* #if !defined (__KERNELBUFFER_H__) */
