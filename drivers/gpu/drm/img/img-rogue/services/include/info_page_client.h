/*************************************************************************/ /*!
@File
@Title          Kernel/User mode general purpose shared memory.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    General purpose shared memory (i.e. information page) mapped by
                kernel space driver and user space clients. All info page
                entries are sizeof(IMG_UINT32) on both 32/64-bit environments.
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

#ifndef INFO_PAGE_CLIENT_H
#define INFO_PAGE_CLIENT_H

#include "device_connection.h"
#include "info_page_defs.h"
#if defined(__KERNEL__)
#include "pvrsrv.h"
#endif

/*************************************************************************/ /*!
@Function      GetInfoPage

@Description   Return Info Page address

@Input         hDevConnection - Services device connection

@Return        Info Page address
*/
/*****************************************************************************/
static INLINE IMG_PUINT32 GetInfoPage(SHARED_DEV_CONNECTION hDevConnection)
{
#if defined(__KERNEL__)
	return (PVRSRVGetPVRSRVData())->pui32InfoPage;
#else
    return hDevConnection->pui32InfoPage;
#endif
}

/*************************************************************************/ /*!
@Function      GetInfoPageDebugFlags

@Description   Return Info Page debug flags

@Input         hDevConnection - Services device connection

@Return        Info Page debug flags
*/
/*****************************************************************************/
static INLINE IMG_UINT32 GetInfoPageDebugFlags(SHARED_DEV_CONNECTION hDevConnection)
{
	return GetInfoPage(hDevConnection)[DEBUG_FEATURE_FLAGS];
}

#endif /* INFO_PAGE_CLIENT_H */
