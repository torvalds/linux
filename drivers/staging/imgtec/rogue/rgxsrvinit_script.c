/*************************************************************************/ /*!
@File
@Title          Services script routines used at initialisation time
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#include "rgxsrvinit_script.h"
#include "srvinit_osfunc.h"
#include "pvr_debug.h"


/*!
*******************************************************************************

 @Function     OutOfScriptSpace

 @Description  Checks for script space failure

 @Input        psScript

 @Return       IMG_BOOL

******************************************************************************/
static IMG_BOOL OutOfScriptSpace(RGX_SCRIPT_BUILD *psScript)
{
	if (psScript->ui32CurrComm >= psScript->ui32MaxLen)
	{
		psScript->bOutOfSpace = IMG_TRUE;
	}

	return psScript->bOutOfSpace;
}


/*!
*******************************************************************************

 @Function     NextScriptCommand

 @Description  Gets next script command to populate

 @Input        psScript

 @Return       IMG_BOOL

******************************************************************************/
static RGX_INIT_COMMAND* NextScriptCommand(RGX_SCRIPT_BUILD *psScript)
{
	if (OutOfScriptSpace(psScript))
	{
		PVR_DPF((PVR_DBG_ERROR, "NextScriptCommand: Out of space for commands (%d)",
		         psScript->ui32MaxLen));
		return NULL;
	}

	return &psScript->psCommands[psScript->ui32CurrComm++];
}


IMG_BOOL ScriptWriteRGXReg(RGX_SCRIPT_BUILD *psScript,
                           IMG_UINT32 ui32Offset,
                           IMG_UINT32 ui32Value)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sWriteHWReg.eOp = RGX_INIT_OP_WRITE_HW_REG;
		psComm->sWriteHWReg.ui32Offset = ui32Offset;
		psComm->sWriteHWReg.ui32Value = ui32Value;

		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptPoll64RGXReg(RGX_SCRIPT_BUILD *psScript,
                            IMG_UINT32 ui32Offset,
                            IMG_UINT64 ui64Value,
                            IMG_UINT64 ui64PollMask)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sPoll64HWReg.eOp = RGX_INIT_OP_POLL_64_HW_REG;
		psComm->sPoll64HWReg.ui32Offset = ui32Offset;
		psComm->sPoll64HWReg.ui64Value = ui64Value;
		psComm->sPoll64HWReg.ui64Mask = ui64PollMask;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptPollRGXReg(RGX_SCRIPT_BUILD *psScript,
                          IMG_UINT32 ui32Offset,
                          IMG_UINT32 ui32Value,
                          IMG_UINT32 ui32PollMask)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sPollHWReg.eOp = RGX_INIT_OP_POLL_HW_REG;
		psComm->sPollHWReg.ui32Offset = ui32Offset;
		psComm->sPollHWReg.ui32Value = ui32Value;
		psComm->sPollHWReg.ui32Mask = ui32PollMask;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptDBGReadRGXReg(RGX_SCRIPT_BUILD *psScript,
                             RGX_INIT_OPERATION eOp,
                             IMG_UINT32 ui32Offset,
                             IMG_CHAR *pszName)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	PVR_ASSERT(strlen(pszName) < RGX_DBG_CMD_NAME_SIZE);

	if (psComm != NULL)
	{
		PVR_ASSERT((eOp == RGX_INIT_OP_DBG_READ32_HW_REG) ||
		           (eOp == RGX_INIT_OP_DBG_READ64_HW_REG));

		psComm->sDBGReadHWReg.eOp = eOp;
		psComm->sDBGReadHWReg.ui32Offset = ui32Offset;

		strcpy(&psComm->sDBGReadHWReg.aszName[0], pszName);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptDBGCalc(RGX_SCRIPT_BUILD *psScript,
                       RGX_INIT_OPERATION eOp,
                       IMG_UINT32 ui32Offset1,
                       IMG_UINT32 ui32Offset2,
                       IMG_UINT32 ui32Offset3,
                       IMG_CHAR *pszName)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	PVR_ASSERT(strlen(pszName) < RGX_DBG_CMD_NAME_SIZE);

	if (psComm != NULL)
	{
		PVR_ASSERT(eOp == RGX_INIT_OP_DBG_CALC);

		psComm->sDBGCalc.eOp = eOp;
		psComm->sDBGCalc.ui32Offset1 = ui32Offset1;
		psComm->sDBGCalc.ui32Offset2 = ui32Offset2;
		psComm->sDBGCalc.ui32Offset3 = ui32Offset3;
		strcpy(&psComm->sDBGCalc.aszName[0], pszName);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}


#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
IMG_BOOL ScriptWriteRGXRegPDUMPOnly(RGX_SCRIPT_BUILD *psScript,
                                    IMG_UINT32 ui32Offset,
                                    IMG_UINT32 ui32Value)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sPDumpHWReg.eOp = RGX_INIT_OP_PDUMP_HW_REG;
		psComm->sPDumpHWReg.ui32Offset = ui32Offset;
		psComm->sPDumpHWReg.ui32Value = ui32Value;

		return IMG_TRUE;
	}

	return IMG_FALSE;
}


/*!
*******************************************************************************

 @Function      ScriptPrepareReadMetaRegThroughSP

 @Description   Add script entries for reading a reg through Meta slave port

 @Input         psScript
 @Input         ui32RegAddr

 @Return        IMG_BOOL

******************************************************************************/
static IMG_BOOL ScriptPrepareReadMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                                  IMG_UINT32 ui32RegAddr)
{
	IMG_BOOL bCmdAdded = IMG_FALSE;

	/* Wait for Slave Port to be Ready */
	bCmdAdded = ScriptPollRGXReg(psScript,
	                             RGX_CR_META_SP_MSLVCTRL1,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (!bCmdAdded) return IMG_FALSE;
	
	/* Issue a Read */
	bCmdAdded = ScriptWriteRGXReg(psScript,
	                              RGX_CR_META_SP_MSLVCTRL0,
	                              ui32RegAddr | RGX_CR_META_SP_MSLVCTRL0_RD_EN);
	if (!bCmdAdded) return IMG_FALSE;

	/* Wait for Slave Port to be Ready: read complete */
	bCmdAdded = ScriptPollRGXReg(psScript,
	                             RGX_CR_META_SP_MSLVCTRL1,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);

	return bCmdAdded;
}


IMG_BOOL ScriptDBGReadMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                       IMG_UINT32 ui32RegAddr,
                                       IMG_CHAR *pszName)
{
	IMG_BOOL bCmdsAdded = IMG_FALSE;

	/* Issue a Read */
	bCmdsAdded = ScriptPrepareReadMetaRegThroughSP(psScript, ui32RegAddr);
	if (!bCmdsAdded) return IMG_FALSE;

	/* Read the value */
	bCmdsAdded = ScriptDBGReadRGXReg(psScript,
	                                 RGX_INIT_OP_DBG_READ32_HW_REG,
	                                 RGX_CR_META_SP_MSLVDATAX,
	                                 pszName);

	return bCmdsAdded;
}


/*!
*******************************************************************************

 @Function      ScriptCondPollRGXReg

 @Description   Sets up a script entry for a conditional register poll

 @Input         psScript
 @Input         ui32CondOffset
 @Input         ui32CondValue
 @Input         ui32CondPollMask
 @Input         ui32Offset
 @Input         ui32Value
 @Input         ui32PollMask

 @return        IMG_BOOL

******************************************************************************/
static IMG_BOOL ScriptCondPollRGXReg(RGX_SCRIPT_BUILD *psScript,
                                     IMG_UINT32 ui32CondOffset,
                                     IMG_UINT32 ui32CondValue,
                                     IMG_UINT32 ui32CondPollMask,
                                     IMG_UINT32 ui32Offset,
                                     IMG_UINT32 ui32Value,
                                     IMG_UINT32 ui32PollMask)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sCondPollHWReg.eOp = RGX_INIT_OP_COND_POLL_HW_REG;
		psComm->sCondPollHWReg.ui32CondOffset = ui32CondOffset;
		psComm->sCondPollHWReg.ui32CondValue = ui32CondValue;
		psComm->sCondPollHWReg.ui32CondMask = ui32CondPollMask;
		psComm->sCondPollHWReg.ui32Offset = ui32Offset;
		psComm->sCondPollHWReg.ui32Value = ui32Value;
		psComm->sCondPollHWReg.ui32Mask = ui32PollMask;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptMetaRegCondPollRGXReg(RGX_SCRIPT_BUILD *psScript,
                                     IMG_UINT32 ui32MetaRegAddr,
                                     IMG_UINT32 ui32MetaRegValue,
                                     IMG_UINT32 ui32MetaRegMask,
                                     IMG_UINT32 ui32RegAddr,
                                     IMG_UINT32 ui32RegValue,
                                     IMG_UINT32 ui32RegMask)
{
	IMG_BOOL bCmdsAdded = IMG_FALSE;

	/* Issue a Read */
	bCmdsAdded = ScriptPrepareReadMetaRegThroughSP(psScript, ui32MetaRegAddr);
	if (!bCmdsAdded) return IMG_FALSE;

	/* Read the value */
	bCmdsAdded = ScriptCondPollRGXReg(psScript,
	                                  RGX_CR_META_SP_MSLVDATAX,
	                                  ui32MetaRegValue,
	                                  ui32MetaRegMask,
	                                  ui32RegAddr,
	                                  ui32RegValue,
	                                  ui32RegMask);

	return bCmdsAdded;
}


IMG_BOOL ScriptWriteMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                     IMG_UINT32 ui32RegAddr,
                                     IMG_UINT32 ui32RegValue)
{
	IMG_BOOL bCmdAdded = IMG_FALSE;

	/* Wait for Slave Port to be Ready */
	bCmdAdded = ScriptPollRGXReg(psScript,
	                             RGX_CR_META_SP_MSLVCTRL1,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                             RGX_CR_META_SP_MSLVCTRL1_READY_EN |
	                             RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (!bCmdAdded) return IMG_FALSE;

	/* Issue a Write */
	bCmdAdded = ScriptWriteRGXReg(psScript,
	                              RGX_CR_META_SP_MSLVCTRL0,
	                              ui32RegAddr);
	if (!bCmdAdded) return IMG_FALSE;

	bCmdAdded = ScriptWriteRGXReg(psScript,
	                              RGX_CR_META_SP_MSLVDATAT,
	                              ui32RegValue);

	/* Wait for complete to be done on the next attempt to read/write */

	return bCmdAdded;
}


/*!
*******************************************************************************

 @Function      ScriptInsertLoopPoint

 @Description   Inserts a loop point in the startup script

 @Input         psScript

 @Return        IMG_BOOL

******************************************************************************/
static IMG_BOOL ScriptInsertLoopPoint(RGX_SCRIPT_BUILD *psScript)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->eOp = RGX_INIT_OP_LOOP_POINT;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


/*!
*******************************************************************************

 @Function      ScriptConditionalBranchOnReg

 @Description   Conditionally branches back to the last loop point in the script.
                Condition is satisfied by the contents of a register

 @Input         psScript
 @Input         ui32Offset
 @Input         ui32Value
 @Input         ui32Mask

 @Return        IMG_BOOL

******************************************************************************/
static IMG_BOOL ScriptConditionalBranchOnReg(RGX_SCRIPT_BUILD *psScript,
                                             IMG_UINT32 ui32Offset,
                                             IMG_UINT32 ui32Value,
                                             IMG_UINT32 ui32Mask)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->eOp = RGX_INIT_OP_COND_BRANCH;
		psComm->sConditionalBranchPoint.ui32Offset = ui32Offset;
		psComm->sConditionalBranchPoint.ui32Value = ui32Value;
		psComm->sConditionalBranchPoint.ui32Mask = ui32Mask;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptPollMetaRegThroughSP(RGX_SCRIPT_BUILD *psScript,
                                    IMG_UINT32 ui32Offset,
                                    IMG_UINT32 ui32PollValue,
                                    IMG_UINT32 ui32PollMask)
{
	IMG_BOOL bCmdsAdded = IMG_FALSE;

	bCmdsAdded = ScriptInsertLoopPoint(psScript);
	if (!bCmdsAdded) return IMG_FALSE;

	bCmdsAdded = ScriptPrepareReadMetaRegThroughSP(psScript, ui32Offset);
	if (!bCmdsAdded) return IMG_FALSE;

	bCmdsAdded = ScriptConditionalBranchOnReg(psScript,
	                                          RGX_CR_META_SP_MSLVDATAX,
	                                          ui32PollValue,
	                                          ui32PollMask);
	return bCmdsAdded;
}


IMG_BOOL ScriptDBGReadMetaCoreReg(RGX_SCRIPT_BUILD *psScript,
                                  IMG_UINT32 ui32RegAddr,
                                  IMG_CHAR *pszName)
{
	IMG_BOOL bCmdsAdded = IMG_FALSE;

	/* Core Read Ready? */
	bCmdsAdded = ScriptPollMetaRegThroughSP(psScript,
	                                        META_CR_TXUXXRXRQ_OFFSET,
	                                        META_CR_TXUXXRXRQ_DREADY_BIT,
	                                        META_CR_TXUXXRXRQ_DREADY_BIT);

	/* Set the reg we are interested in reading */
	bCmdsAdded = ScriptWriteMetaRegThroughSP(psScript,
	                                         META_CR_TXUXXRXRQ_OFFSET,
	                                         ui32RegAddr | META_CR_TXUXXRXRQ_RDnWR_BIT);
	if (!bCmdsAdded) return IMG_FALSE;

	/* Core Read Done? */
	bCmdsAdded = ScriptPollMetaRegThroughSP(psScript,
	                                        META_CR_TXUXXRXRQ_OFFSET,
	                                        META_CR_TXUXXRXRQ_DREADY_BIT,
	                                        META_CR_TXUXXRXRQ_DREADY_BIT);

	/* Read the value */
	ScriptDBGReadMetaRegThroughSP(psScript, META_CR_TXUXXRXDT_OFFSET, pszName);

	return IMG_TRUE;

}
#endif /* RGX_FEATURE_META */


IMG_BOOL ScriptDBGString(RGX_SCRIPT_BUILD *psScript,
                         const IMG_CHAR *aszString)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->sDBGString.eOp = RGX_INIT_OP_DBG_STRING;
		strcpy(psComm->sDBGString.aszString, aszString);
		if (strlen(aszString) >= (sizeof(psComm->sDBGString.aszString) - 2))
		{
			psComm->sDBGString.aszString[RGX_DBG_CMD_NAME_SIZE-1] = '\0';
		}
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


IMG_BOOL ScriptHalt(RGX_SCRIPT_BUILD *psScript)
{
	RGX_INIT_COMMAND *psComm = NextScriptCommand(psScript);

	if (psComm != NULL)
	{
		psComm->eOp = RGX_INIT_OP_HALT;
		return IMG_TRUE;
	}

	return IMG_FALSE;
}


/******************************************************************************
 End of file (rgxsrvinit_script.c)
******************************************************************************/
