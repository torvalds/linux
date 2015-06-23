/*************************************************************************/ /*!
@File
@Title          Version numbers and strings.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Version numbers and strings for PVR Consumer services
                components.
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

#ifndef _PVRVERSION_H_
#define _PVRVERSION_H_

/*
 *  Rogue KM Version Note
 *
 *  L 0.16:
 *          Support gpu disable dvfs case.
 *          Add rk_tf_check_version to compatible for rk3328.
 *  L 0.17:
 *          merge 1.4_ED3573678 DDK code
 *  L 0.18:
 *          If fix freq,then don't force to drop freq to the lowest.
 */

#define PVR_STR(X) #X
#define PVR_STR2(X) PVR_STR(X)

#define PVRVERSION_MAJ               1
#define PVRVERSION_MIN               4

#define PVRVERSION_FAMILY           "rogueddk"
#define PVRVERSION_BRANCHNAME       "1.4"
#define PVRVERSION_BUILD             3573678
#define PVRVERSION_BSCONTROL        "Rogue_DDK_Android_RSCompute"

#define PVRVERSION_STRING           "Rogue_DDK_Android_RSCompute rogueddk 1.4@" PVR_STR2(PVRVERSION_BUILD)
#define PVRVERSION_STRING_SHORT     "1.4@" PVR_STR2(PVRVERSION_BUILD) ""

#define COPYRIGHT_TXT               "Copyright (c) Imagination Technologies Ltd. All Rights Reserved."

#define PVRVERSION_BUILD_HI          357
#define PVRVERSION_BUILD_LO          3678
#define PVRVERSION_STRING_NUMERIC    PVR_STR2(PVRVERSION_MAJ) "." PVR_STR2(PVRVERSION_MIN) "." PVR_STR2(PVRVERSION_BUILD_HI) "." PVR_STR2(PVRVERSION_BUILD_LO)

#define PVRVERSION_PACK(MAJ,MIN) ((((MAJ)&0xFFFF) << 16) | (((MIN)&0xFFFF) << 0))
#define PVRVERSION_UNPACK_MAJ(VERSION) (((VERSION) >> 16) & 0xFFFF)
#define PVRVERSION_UNPACK_MIN(VERSION) (((VERSION) >> 0) & 0xFFFF)

//chenli:define rockchip version
#define RKVERSION                   "Rogue L 0.18"
#endif /* _PVRVERSION_H_ */
