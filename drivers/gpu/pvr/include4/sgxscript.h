/*************************************************************************/ /*!
@Title          SGX kernel services structues/functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    SGX initialisation script definitions.
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
#ifndef __SGXSCRIPT_H__
#define __SGXSCRIPT_H__

#if defined (__cplusplus)
extern "C" {
#endif

#define	SGX_MAX_INIT_COMMANDS	64
#define	SGX_MAX_DEINIT_COMMANDS	16

typedef	enum _SGX_INIT_OPERATION
{
	SGX_INIT_OP_ILLEGAL = 0,
	SGX_INIT_OP_WRITE_HW_REG,
	SGX_INIT_OP_READ_HW_REG,
#if defined(PDUMP)
	SGX_INIT_OP_PDUMP_HW_REG,
#endif
	SGX_INIT_OP_HALT
} SGX_INIT_OPERATION;

typedef union _SGX_INIT_COMMAND
{
	SGX_INIT_OPERATION eOp;
	struct {
		SGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
	} sWriteHWReg;
	struct {
		SGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
	} sReadHWReg;
#if defined(PDUMP)
	struct {
		SGX_INIT_OPERATION eOp;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
	} sPDumpHWReg;
#endif
} SGX_INIT_COMMAND;

typedef struct _SGX_INIT_SCRIPTS_
{
	SGX_INIT_COMMAND asInitCommandsPart1[SGX_MAX_INIT_COMMANDS];
	SGX_INIT_COMMAND asInitCommandsPart2[SGX_MAX_INIT_COMMANDS];
	SGX_INIT_COMMAND asDeinitCommands[SGX_MAX_DEINIT_COMMANDS];
} SGX_INIT_SCRIPTS;

#if defined(__cplusplus)
}
#endif

#endif /* __SGXSCRIPT_H__ */

/*****************************************************************************
 End of file (sgxscript.h)
*****************************************************************************/
