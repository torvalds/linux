/*************************************************************************/ /*!
@Title          Debug driver utilities implementations.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Hotkey stuff
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


#if !defined(LINUX) && !defined(__QNXNTO__)
#include <ntddk.h>
#include <windef.h>
#endif

#include "img_types.h"
#include "pvr_debug.h"
#include "dbgdrvif.h"
#include "dbgdriv.h"
#include "hotkey.h"
#include "hostfunc.h"




/*****************************************************************************
 Global vars
*****************************************************************************/

IMG_UINT32	g_ui32HotKeyFrame = 0xFFFFFFFF;
IMG_BOOL	g_bHotKeyPressed = IMG_FALSE;
IMG_BOOL	g_bHotKeyRegistered = IMG_FALSE;

/* Hotkey stuff */
PRIVATEHOTKEYDATA    g_PrivateHotKeyData;


/*****************************************************************************
 Code
*****************************************************************************/


/******************************************************************************
 * Function Name: ReadInHotKeys
 *
 * Inputs       : none
 * Outputs      : -
 * Returns      : nothing
 * Globals Used : -
 *
 * Description  : Gets Hot key entries from system.ini
 *****************************************************************************/
IMG_VOID ReadInHotKeys(IMG_VOID)
{
	g_PrivateHotKeyData.ui32ScanCode = 0x58;	/* F12	*/
	g_PrivateHotKeyData.ui32ShiftState = 0x0;

	/*
		Find buffer names etc..
	*/
	HostReadRegistryDWORDFromString("DEBUG\\Streams", "ui32ScanCode"  , &g_PrivateHotKeyData.ui32ScanCode);
	HostReadRegistryDWORDFromString("DEBUG\\Streams", "ui32ShiftState", &g_PrivateHotKeyData.ui32ShiftState);
}

/******************************************************************************
 * Function Name: RegisterKeyPressed
 *
 * Inputs       : IMG_UINT32 dwui32ScanCode, PHOTKEYINFO pInfo
 * Outputs      : -
 * Returns      : nothing
 * Globals Used : -
 *
 * Description  : Called when hotkey pressed.
 *****************************************************************************/
IMG_VOID RegisterKeyPressed(IMG_UINT32 dwui32ScanCode, PHOTKEYINFO pInfo)
{
	PDBG_STREAM	psStream;

	PVR_UNREFERENCED_PARAMETER(pInfo);

	if (dwui32ScanCode == g_PrivateHotKeyData.ui32ScanCode)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"PDUMP Hotkey pressed !\n"));

		psStream = (PDBG_STREAM) g_PrivateHotKeyData.sHotKeyInfo.pvStream;

		if (!g_bHotKeyPressed)
		{
			/*
				Capture the next frame.
			*/
			g_ui32HotKeyFrame = psStream->psCtrl->ui32Current + 2;

			/*
				Do the flag.
			*/
			g_bHotKeyPressed = IMG_TRUE;
		}
	}
}

/******************************************************************************
 * Function Name: ActivateHotKeys
 *
 * Inputs       : -
 * Outputs      : -
 * Returns      : -
 * Globals Used : -
 *
 * Description  : Installs HotKey callbacks
 *****************************************************************************/
IMG_VOID ActivateHotKeys(PDBG_STREAM psStream)
{
	/*
		Setup hotkeys.
	*/
	ReadInHotKeys();

	/*
		Has it already been allocated.
	*/
	if (!g_PrivateHotKeyData.sHotKeyInfo.hHotKey)
	{
		if (g_PrivateHotKeyData.ui32ScanCode != 0)
		{
			PVR_DPF((PVR_DBG_MESSAGE,"Activate HotKey for PDUMP.\n"));

			/*
				Add in stream data.
			*/
			g_PrivateHotKeyData.sHotKeyInfo.pvStream = psStream;

			DefineHotKey(g_PrivateHotKeyData.ui32ScanCode, g_PrivateHotKeyData.ui32ShiftState, &g_PrivateHotKeyData.sHotKeyInfo);
		}
		else
		{
			g_PrivateHotKeyData.sHotKeyInfo.hHotKey = 0;
		}
	}
}

/******************************************************************************
 * Function Name: DeactivateHotKeys
 *
 * Inputs       : -
 * Outputs      : -
 * Returns      : -
 * Globals Used : -
 *
 * Description  : Removes HotKey callbacks
 *****************************************************************************/
IMG_VOID DeactivateHotKeys(IMG_VOID)
{
	if (g_PrivateHotKeyData.sHotKeyInfo.hHotKey != 0)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"Deactivate HotKey.\n"));

		RemoveHotKey(g_PrivateHotKeyData.sHotKeyInfo.hHotKey);
		g_PrivateHotKeyData.sHotKeyInfo.hHotKey = 0;
	}
}


/*****************************************************************************
 End of file (HOTKEY.C)
*****************************************************************************/
