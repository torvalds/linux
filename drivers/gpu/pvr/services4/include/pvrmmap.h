/*************************************************************************/ /*!
@Title          Main include file for PVRMMAP library.
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
#ifndef __PVRMMAP_H__
#define __PVRMMAP_H__

/*!
 **************************************************************************
 @brief         map kernel memory into user memory. 

 @param			hModule - a handle to the device supplying the kernel memory
 @param			ppvLinAddr - pointer to where the user mode address should be placed
 @param			pvLinAddrKM - the base of kernel address range to map
 @param			phMappingInfo - pointer to mapping information handle
 @param			hMHandle - handle associated with memory to be mapped

 @return		PVRSRV_OK, or error code.
 ***************************************************************************/

PVRSRV_ERROR PVRPMapKMem(IMG_HANDLE hModule, IMG_VOID **ppvLinAddr, IMG_VOID *pvLinAddrKM, IMG_HANDLE *phMappingInfo, IMG_HANDLE hMHandle);


/*!
 **************************************************************************
 @brief	        Removes a kernel to userspace memory mapping.

 @param		hModule - a handle to the device supplying the kernel memory
 @param		hMappingInfo - mapping information handle
 @param		hMHandle - handle associated with memory to be mapped

 @return	IMG_BOOL indicating success or otherwise.
 ***************************************************************************/
IMG_BOOL PVRUnMapKMem(IMG_HANDLE hModule, IMG_HANDLE hMappingInfo, IMG_HANDLE hMHandle);

#endif /* _PVRMMAP_H_ */

