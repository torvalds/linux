/*************************************************************************/ /*!
@File
@Title          BVNC handling specific header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the BVNC related work
                (see hwdefs/km/rgx_bvnc_table_km.h and
                hwdefs/km/rgx_bvnc_defs_km.h
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

#if !defined(RGXBVNC_H)
#define RGXBVNC_H

#include "pvrsrv_error.h"
#include "img_types.h"
#include "rgxdevice.h"

/*************************************************************************/ /*!
@brief		This function detects the Rogue variant and configures the
			essential config info associated with such a device.
			The config info includes features, errata, etc
@param		psDeviceNode - Device Node pointer
@return		PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXBvncInitialiseConfiguration(PVRSRV_DEVICE_NODE *psDeviceNode);

/*************************************************************************/ /*!
@brief		This function checks if a particular feature is available on
			the given rgx device
@param		psDeviceNode - Device Node pointer
@param		ui64FeatureMask - feature to be checked
@return		true if feature is supported, false otherwise
*/ /**************************************************************************/
IMG_BOOL RGXBvncCheckFeatureSupported(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT64 ui64FeatureMask);

/*************************************************************************/ /*!
@brief		This function returns the value of a feature on the given
			rgx device
@param		psDeviceNode - Device Node pointer
@param		ui64FeatureMask - feature for which to return the value
@return		the value for the specified feature
*/ /**************************************************************************/
IMG_INT32 RGXBvncGetSupportedFeatureValue(PVRSRV_DEVICE_NODE *psDeviceNode, RGX_FEATURE_WITH_VALUE_INDEX eFeatureIndex);

/*************************************************************************/ /*!
@brief		This function validates that the BVNC values in CORE_ID regs are
			consistent and correct.
@param		psDeviceNode - Device Node pointer
@param		GivenBVNC - BVNC to be verified against as supplied by caller
@param		CoreIdMask - mask of components to pull from CORE_ID register
@return		success or fail
*/ /**************************************************************************/
PVRSRV_ERROR RGXVerifyBVNC(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_UINT64 ui64GivenBVNC, IMG_UINT64 ui64CoreIdMask);

#endif /* RGXBVNC_H */
