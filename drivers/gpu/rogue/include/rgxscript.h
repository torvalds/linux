/*************************************************************************/ /*!
@File
@Title          rgx kernel services structues/functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX initialisation script definitions.
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

#ifndef __RGXSCRIPT_H__
#define __RGXSCRIPT_H__

#if defined (__cplusplus)
extern "C" {
#endif

#define	RGX_MAX_INIT_COMMANDS	(256)
#define	RGX_MAX_DBGBUS_COMMANDS	(4096)
#define	RGX_MAX_DEINIT_COMMANDS	(32)
#define RGX_DBG_CMD_NAME_SIZE	(32)

typedef	enum _RGX_INIT_OPERATION
{
	RGX_INIT_OP_ILLEGAL = 0,
	RGX_INIT_OP_WRITE_HW_REG,
	RGX_INIT_OP_POLL_64_HW_REG,
	RGX_INIT_OP_POLL_HW_REG,
	RGX_INIT_OP_COND_POLL_HW_REG,
	RGX_INIT_OP_LOOP_POINT,
	RGX_INIT_OP_COND_BRANCH,
	RGX_INIT_OP_HALT,
	RGX_INIT_OP_DBG_READ32_HW_REG,
	RGX_INIT_OP_DBG_READ64_HW_REG,
	RGX_INIT_OP_DBG_CALC,
	RGX_INIT_OP_DBG_WAIT,
	RGX_INIT_OP_DBG_STRING,
	RGX_INIT_OP_PDUMP_HW_REG,
} RGX_INIT_OPERATION;

typedef union _RGX_INIT_COMMAND_
{
	RGX_INIT_OPERATION eOp;
	
	struct {
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
	} sWriteHWReg;

	struct {
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
	} sPDumpHWReg;
	
	struct 
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT64 ui64Value;
		IMG_UINT64 ui64Mask;		
	} sPoll64HWReg;

	struct 
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
		IMG_UINT32 ui32Mask;		
	} sPollHWReg;
	
	struct 
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32CondOffset;
		IMG_UINT32 ui32CondValue;
		IMG_UINT32 ui32CondMask;		
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
		IMG_UINT32 ui32Mask;		
	} sCondPollHWReg;
	
	struct
	{
		RGX_INIT_OPERATION eOp;
	} sLoopPoint;

	struct
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
		IMG_UINT32 ui32Mask;

	} sConditionalBranchPoint;

	struct 
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_CHAR aszName[RGX_DBG_CMD_NAME_SIZE];
	} sDBGReadHWReg;

	struct
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset1;
		IMG_UINT32 ui32Offset2;
		IMG_UINT32 ui32Offset3;
		IMG_CHAR aszName[RGX_DBG_CMD_NAME_SIZE];
	} sDBGCalc;

	struct
	{
		RGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32WaitInUs;
	} sDBGWait;

	struct
	{
		RGX_INIT_OPERATION eOp;
		IMG_CHAR aszString[RGX_DBG_CMD_NAME_SIZE];
	} sDBGString;

} RGX_INIT_COMMAND;

typedef struct _RGX_INIT_SCRIPTS_
{
	RGX_INIT_COMMAND asInitCommands[RGX_MAX_INIT_COMMANDS];
	RGX_INIT_COMMAND asDbgCommands[RGX_MAX_INIT_COMMANDS];
	RGX_INIT_COMMAND asDbgBusCommands[RGX_MAX_DBGBUS_COMMANDS];
	RGX_INIT_COMMAND asDeinitCommands[RGX_MAX_DEINIT_COMMANDS];
} RGX_SCRIPTS;

#if defined(__cplusplus)
}
#endif

#endif /* __RGXSCRIPT_H__ */

/*****************************************************************************
 End of file (rgxscript.h)
*****************************************************************************/

