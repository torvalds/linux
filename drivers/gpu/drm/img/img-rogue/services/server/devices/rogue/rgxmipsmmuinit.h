/*************************************************************************/ /*!
@File
@Title          Device specific initialisation routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific MMU initialisation for the MIPS firmware
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

/* NB: this file is not to be included arbitrarily.  It exists solely
   for the linkage between rgxinit.c and rgxmmuinit.c, the former
   being otherwise cluttered by the contents of the latter */

#ifndef SRVKM_RGXMIPSMMUINIT_H
#define SRVKM_RGXMIPSMMUINIT_H

#include "device.h"
#include "img_types.h"
#include "mmu_common.h"
#include "img_defs.h"
#include "rgx_mips.h"

/*

		Labelling of fields within virtual address. No PD and PC are used currently for
		the MIPS MMU
*/
/*
Page Table entry #
*/
#define RGX_MIPS_MMUCTRL_VADDR_PT_INDEX_SHIFT        (12U)
#define RGX_MIPS_MMUCTRL_VADDR_PT_INDEX_CLRMSK       (IMG_UINT64_C(0XFFFFFFFF00000FFF))


/* PC entries related definitions */
/* No PC is currently used for MIPS MMU */
#define RGX_MIPS_MMUCTRL_PC_DATA_VALID_EN            (0U)
#define RGX_MIPS_MMUCTRL_PC_DATA_VALID_SHIFT         (0U)
#define RGX_MIPS_MMUCTRL_PC_DATA_VALID_CLRMSK        (0U)

#define RGX_MIPS_MMUCTRL_PC_DATA_READ_ONLY_SHIFT     (0U)
#define RGX_MIPS_MMUCTRL_PC_DATA_READ_ONLY_CLRMSK    (0U)
#define RGX_MIPS_MMUCTRL_PC_DATA_READ_ONLY_EN        (0U)

/* PD entries related definitions */
/* No PD is currently used for MIPS MMU */
#define RGX_MIPS_MMUCTRL_PD_DATA_VALID_EN            (0U)
#define RGX_MIPS_MMUCTRL_PD_DATA_VALID_SHIFT         (0U)
#define RGX_MIPS_MMUCTRL_PD_DATA_VALID_CLRMSK        (0U)

#define RGX_MIPS_MMUCTRL_PD_DATA_READ_ONLY_SHIFT     (0U)
#define RGX_MIPS_MMUCTRL_PD_DATA_READ_ONLY_CLRMSK    (0U)
#define RGX_MIPS_MMUCTRL_PD_DATA_READ_ONLY_EN        (0U)


PVRSRV_ERROR RGXMipsMMUInit_Register(PVRSRV_DEVICE_NODE *psDeviceNode);
PVRSRV_ERROR RGXMipsMMUInit_Unregister(PVRSRV_DEVICE_NODE *psDeviceNode);


#endif /* #ifndef SRVKM_RGXMIPSMMUINIT_H */
