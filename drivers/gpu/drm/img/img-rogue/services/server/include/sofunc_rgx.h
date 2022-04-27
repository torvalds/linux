/*************************************************************************/ /*!
@File
@Title          SO Interface header file for devices/RGX functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Contains SO interface functions. These functions are defined in
                the common devices layer and are called from the env layer OS
                specific implementation.
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

#if !defined(SOFUNC_RGX_H_)
#define SOFUNC_RGX_H_

#include "img_types.h"
#include "pvrsrv_error.h"


#if !defined(NO_HARDWARE)
/*!
*******************************************************************************

 @Function     SORgxGpuUtilStatsRegister

 @Description  SO Interface function called from the OS layer implementation.
               Initialise data used to compute GPU utilisation statistics
               for a particular user (identified by the handle passed as
               argument). This function must be called only once for each
               different user/handle.

 @Input        phGpuUtilUser - Pointer to handle used to identify a user of
                               RGXGetGpuUtilStats

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR SORgxGpuUtilStatsRegister(IMG_HANDLE *phGpuUtilUser);


/*!
*******************************************************************************

 @Function     SORgxGpuUtilStatsUnregister

 @Description  SO Interface function called from the OS layer implementation.
               Free data previously used to compute GPU utilisation statistics
               for a particular user (identified by the handle passed as
               argument).

 @Input        hGpuUtilUser - Handle used to identify a user of
                              RGXGetGpuUtilStats

 @Return       PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR SORgxGpuUtilStatsUnregister(IMG_HANDLE hGpuUtilUser);
#endif /* !defined(NO_HARDWARE) */



#endif /* SOFUNC_RGX_H_ */
