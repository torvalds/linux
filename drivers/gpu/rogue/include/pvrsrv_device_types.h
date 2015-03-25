/*************************************************************************/ /*!
@File
@Title          PowerVR device type definitions
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

#if !defined(__PVRSRV_DEVICE_TYPES_H__)
#define __PVRSRV_DEVICE_TYPES_H__

#include "img_types.h"

#define PVRSRV_MAX_DEVICES		16	/*!< Largest supported number of devices on the system */

/*!
 ******************************************************************************
 * List of known device types.
 *****************************************************************************/
typedef enum PVRSRV_DEVICE_TYPE
{
	PVRSRV_DEVICE_TYPE_UNKNOWN			= 0,  /*!< Unknown device type */
	PVRSRV_DEVICE_TYPE_MBX1				= 1,  /*!< MBX1 */
	PVRSRV_DEVICE_TYPE_MBX1_LITE		= 2,  /*!< MBX1 Lite */
	PVRSRV_DEVICE_TYPE_M24VA			= 3,  /*!< M24VA */
	PVRSRV_DEVICE_TYPE_MVDA2			= 4,  /*!< MVDA2 */
	PVRSRV_DEVICE_TYPE_MVED1			= 5,  /*!< MVED1 */
	PVRSRV_DEVICE_TYPE_MSVDX			= 6,  /*!< MSVDX */
	PVRSRV_DEVICE_TYPE_SGX				= 7,  /*!< SGX */
	PVRSRV_DEVICE_TYPE_VGX				= 8,  /*!< VGX */
	PVRSRV_DEVICE_TYPE_EXT				= 9,  /*!< 3rd party devices take ext type */
	PVRSRV_DEVICE_TYPE_RGX				= 10, /*!< RGX */

    PVRSRV_DEVICE_TYPE_LAST             = 10, /*!< Last device type */

	PVRSRV_DEVICE_TYPE_FORCE_I32		= 0x7fffffff /*!< Force enum to be 32-bit width */

} PVRSRV_DEVICE_TYPE;


/*!
 *****************************************************************************
 * List of known device classes.
 *****************************************************************************/
typedef enum _PVRSRV_DEVICE_CLASS_
{
	PVRSRV_DEVICE_CLASS_3D				= 0 ,       /*!< 3D Device Class */
	PVRSRV_DEVICE_CLASS_DISPLAY			= 1 ,       /*!< Display Device Class */
	PVRSRV_DEVICE_CLASS_BUFFER			= 2 ,       /*!< Buffer Class */
	PVRSRV_DEVICE_CLASS_VIDEO			= 3 ,       /*!< Video Device Class */

	PVRSRV_DEVICE_CLASS_FORCE_I32 		= 0x7fffffff /* Force enum to be at least 32-bits wide */

} PVRSRV_DEVICE_CLASS;


/*!
 ******************************************************************************
 * Device identifier structure
 *****************************************************************************/
typedef struct _PVRSRV_DEVICE_IDENTIFIER_
{
	PVRSRV_DEVICE_TYPE		eDeviceType;		/*!< Identifies the type of the device */
	PVRSRV_DEVICE_CLASS		eDeviceClass;		/*!< Identifies more general class of device - display/3d/mpeg etc */
	IMG_UINT32				ui32DeviceIndex;	/*!< Index of the device within the system */
	IMG_CHAR				*pszPDumpDevName;	/*!< Pdump memory bank name */
	IMG_CHAR				*pszPDumpRegName;	/*!< Pdump register bank name */

} PVRSRV_DEVICE_IDENTIFIER;


#if defined(KERNEL) && defined(ANDROID)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#endif /* __PVRSRV_DEVICE_TYPES_H__ */

