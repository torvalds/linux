/*************************************************************************/ /*!
@File
@Title          Linked list shared functions implementation.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implementation of the list iterators for types shared among
                more than one file in the services code.
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

#include "lists.h"

/*===================================================================
  LIST ITERATOR FUNCTIONS USED IN MORE THAN ONE FILE (those used just
  once are implemented locally).
  ===================================================================*/

IMPLEMENT_LIST_ANY(PVRSRV_DEVICE_NODE)
IMPLEMENT_LIST_ANY_2(PVRSRV_DEVICE_NODE, PVRSRV_ERROR, PVRSRV_OK)
IMPLEMENT_LIST_ANY_VA(PVRSRV_DEVICE_NODE)
IMPLEMENT_LIST_ANY_VA_2(PVRSRV_DEVICE_NODE, PVRSRV_ERROR, PVRSRV_OK)
IMPLEMENT_LIST_FOR_EACH(PVRSRV_DEVICE_NODE)
IMPLEMENT_LIST_FOR_EACH_VA(PVRSRV_DEVICE_NODE)
IMPLEMENT_LIST_INSERT(PVRSRV_DEVICE_NODE)
IMPLEMENT_LIST_REMOVE(PVRSRV_DEVICE_NODE)

IMPLEMENT_LIST_ANY_VA(PVRSRV_POWER_DEV)
IMPLEMENT_LIST_ANY_VA_2(PVRSRV_POWER_DEV, PVRSRV_ERROR, PVRSRV_OK)
IMPLEMENT_LIST_INSERT(PVRSRV_POWER_DEV)
IMPLEMENT_LIST_REMOVE(PVRSRV_POWER_DEV)


/*===================================================================
  BELOW ARE IMPLEMENTED SOME COMMON CALLBACKS USED IN DIFFERENT FILES
  ===================================================================*/


/*************************************************************************/ /*!
@Function       MatchDeviceKM_AnyVaCb
@Description    Matchs a device node with an id and optionally a class.
@Input          psDeviceNode  Pointer to the device node.
@Input          va            Variable argument list, with te following values:
                                ui32DevIndex  Index of de device to match.
                                bIgnoreClass  Flag indicating if there's
                                              no need to check the device class.
                                eDevClass     Device class, ONLY present if
                                              bIgnoreClass was IMG_FALSE.
@Return         The pointer to the device node if it matchs, IMG_NULL
                otherwise.
*/ /**************************************************************************/
IMG_VOID* MatchDeviceKM_AnyVaCb(PVRSRV_DEVICE_NODE* psDeviceNode, va_list va)
{
	IMG_UINT32 ui32DevIndex;
	IMG_BOOL bIgnoreClass;
	PVRSRV_DEVICE_CLASS eDevClass;

	ui32DevIndex = va_arg(va, IMG_UINT32);
	bIgnoreClass = va_arg(va, IMG_BOOL);
	if (!bIgnoreClass)
	{
		eDevClass = va_arg(va, PVRSRV_DEVICE_CLASS);
	}
	else
	{
		/*this value will never be used, since the short circuit evaluation
		of the first clause will stop because bIgnoreClass is true, but the
		compiler complains if it's not initialized.*/
		eDevClass = PVRSRV_DEVICE_CLASS_FORCE_I32;
	}

	if ((bIgnoreClass || psDeviceNode->sDevId.eDeviceClass == eDevClass) &&
		psDeviceNode->sDevId.ui32DeviceIndex == ui32DevIndex)
	{
		return psDeviceNode;
	}
	return IMG_NULL;
}

/*!
******************************************************************************
@Function	MatchPowerDeviceIndex_AnyVaCb
@Description    Matches a power device with its device index.
@Input          va               Variable argument list
                ui32DeviceIndex  Device index
@Return         The pointer to the device it matched, IMG_NULL otherwise.
******************************************************************************/
IMG_VOID* MatchPowerDeviceIndex_AnyVaCb(PVRSRV_POWER_DEV *psPowerDev, va_list va)
{
	IMG_UINT32 ui32DeviceIndex;

	ui32DeviceIndex = va_arg(va, IMG_UINT32);

	if (psPowerDev->ui32DeviceIndex == ui32DeviceIndex)
	{
		return psPowerDev;
	}
	else
	{
		return IMG_NULL;
	}
}
