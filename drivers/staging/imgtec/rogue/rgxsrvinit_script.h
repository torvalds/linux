/*************************************************************************/ /*!
@File
@Title          Header for Services script routines used at initialisation time
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the connections between the various parts of the
                initialisation server.
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
#ifndef __RGXSRVINIT_SCRIPT_H__
#define __RGXSRVINIT_SCRIPT_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "rgxscript.h"
#include "rgx_firmware_processor.h"
#include "rgxdefs_km.h"


typedef struct _RGX_SCRIPT_BUILD
{
	IMG_UINT32 ui32MaxLen;
	IMG_UINT32 ui32CurrComm;
	IMG_BOOL bOutOfSpace;
	RGX_INIT_COMMAND *psCommands;
} RGX_SCRIPT_BUILD;


/*!
*******************************************************************************

 @Function      ScriptWriteRGXReg

 @Description   Sets up a script entry for register write

 @Input         psScript
 @Input         ui32Offset
 @Input         ui32Value

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptWriteRGXReg(RGX_SCRIPT_BUILD *psScript,
                           IMG_UINT32 ui32Offset,
                           IMG_UINT32 ui32Value);

/*!
*******************************************************************************

 @Function      ScriptPoll64RGXReg

 @Description   Sets up a script entry for register poll

 @Input         psScript
 @Input         ui32Offset
 @Input         ui32Value
 @Input         ui32PollMask

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptPoll64RGXReg(RGX_SCRIPT_BUILD *psScript,
                            IMG_UINT32 ui32Offset,
                            IMG_UINT64 ui64Value,
                            IMG_UINT64 ui64PollMask);

/*!
*******************************************************************************

 @Function      ScriptPollRGXReg

 @Description   Sets up a script entry for register poll

 @Input         psScript
 @Input         ui32Offset
 @Input         ui32Value
 @Input         ui32PollMask

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptPollRGXReg(RGX_SCRIPT_BUILD *psScript,
                          IMG_UINT32 ui32Offset,
                          IMG_UINT32 ui32Value,
                          IMG_UINT32 ui32PollMask);

/*!
*******************************************************************************

 @Function      ScriptDBGReadRGXReg

 @Description   Sets up a script entry for register setup

 @Input         psScript
 @Input         eOp
 @Input         ui32Offset
 @Input         ui32Value

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptDBGReadRGXReg(RGX_SCRIPT_BUILD *psScript,
                             RGX_INIT_OPERATION eOp,
                             IMG_UINT32 ui32Offset,
                             IMG_CHAR *pszName);

/*!
*******************************************************************************

 @Function      ScriptDBGCalc

 @Description   Sets up a script for calculation

 @Input         psScript
 @Input         eOp
 @Input         ui32Offset1
 @Input         ui32Offset2
 @Input         ui32Offset3

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptDBGCalc(RGX_SCRIPT_BUILD *psScript,
                       RGX_INIT_OPERATION eOp,
                       IMG_UINT32 ui32Offset1,
                       IMG_UINT32 ui32Offset2,
                       IMG_UINT32 ui32Offset3,
                       IMG_CHAR *pszName);


#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
/*!
*******************************************************************************

 @Function      ScriptWriteRGXReg

 @Description   Sets up a script entry for register setup

 @Input         psScript
 @Input         ui32Offset
 @Input         ui32Value

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptWriteRGXRegPDUMPOnly(RGX_SCRIPT_BUILD *psScript,
                                    IMG_UINT32 ui32Offset,
                                    IMG_UINT32 ui32Value);

/*!
*******************************************************************************

 @Function      ScriptDBGReadMetaRegThroughSP

 @Description   Add script entries for reading a reg through Meta slave port

 @Input         psScript
 @Input         ui32RegAddr
 @Input         pszName

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptDBGReadMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                       IMG_UINT32 ui32RegAddr,
                                       IMG_CHAR *pszName);

/*!
*******************************************************************************

 @Function      ScriptDBGReadMetaRegThroughSP

 @Description   Add script entries for polling a reg through Meta slave port

 @Input         psScript
 @Input         ui32RegAddr
 @Input         pszName

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptMetaRegCondPollRGXReg(RGX_SCRIPT_BUILD *psScript,
                                     IMG_UINT32 ui32MetaRegAddr,
                                     IMG_UINT32 ui32MetaRegValue,
                                     IMG_UINT32 ui32MetaRegMask,
                                     IMG_UINT32 ui32RegAddr,
                                     IMG_UINT32 ui32RegValue,
                                     IMG_UINT32 ui32RegMask);

/*!
*******************************************************************************

 @Function      ScriptWriteMetaRegThroughSP

 @Description   Add script entries for writing a reg through Meta slave port

 @Input         psScript
 @Input         ui32RegAddr
 @Input         pszName

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptWriteMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                     IMG_UINT32 ui32RegAddr,
                                     IMG_UINT32 ui32RegValue);

/*!
*******************************************************************************

 @Function      ScriptPollMetaRegThroughSP

 @Description   Polls a Core Garten register through the slave port

 @Input         psScript

 @Return        void

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptPollMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                    IMG_UINT32 ui32Offset,
                                    IMG_UINT32 ui32PollValue,
                                    IMG_UINT32 ui32PollMask);

/*!
*******************************************************************************

 @Function      ScriptDBGReadMetaRegThroughSP

 @Description   Adds script entries reading a reg through Meta slave port

 @Input         psScript
 @Input         ui32RegAddr
 @Input         pszName

 @Return        IMG_BOOL

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptDBGReadMetaCoreReg(RGX_SCRIPT_BUILD *psScript,
                                  IMG_UINT32 ui32RegAddr,
                                  IMG_CHAR *pszName);
#endif /* RGX_FEATURE_META */


/*!
*******************************************************************************

 @Function      ScriptDBGString

 @Description   Adds a debug print to the script

 @Input         psScript
 @Input         pszName

 @Return        IMG_BOOL

******************************************************************************/

IMG_INTERNAL
IMG_BOOL ScriptDBGString(RGX_SCRIPT_BUILD *psScript,
                         const IMG_CHAR *aszString);


/*!
*******************************************************************************

 @Function      ScriptHalt

 @Description   Add a cmd to finish the script

 @Input         psScript

 @Return        IMG_BOOL True if it runs out of cmds when building the script

******************************************************************************/
IMG_INTERNAL
IMG_BOOL ScriptHalt(RGX_SCRIPT_BUILD *psScript);


#if defined (__cplusplus)
}
#endif

#endif /* __RGXSRVINIT_SCRIPT_H__ */

