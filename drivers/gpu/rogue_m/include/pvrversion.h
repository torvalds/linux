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
 *  L 1.17:
 *		Support gpu disable dvfs case.
 *		Add rk_tf_check_version to compatible for rk3328.
 *  L 1.18:
 *		If fix freq,then don't force to drop freq to the lowest.
 *
 *  M 1.21:
 *		Merge 1.5_RTM3604260 DDK code.
 *  M 1.24:
 *		Merge 1.5_ED3653583 DDK code.
 *  M 1.28:
 *		Merge 1.5_ED3776568 DDK code.
 *  M 1.29
 *		1. Reopen bEnableRDPowIsland since it doesn't appear splash screen when click the drawerbutton.
 *		2. Don't set PVR_ANDROID_HAS_SET_BUFFERS_DATASPACE by default.
 *		3. Remove hGPUUtilLock to avoid dead lock.
 *		4. Get raw ion_device by IonDevAcquire.
 *  M 1.31
 *		1. Merge 1.5_ED3830101 DDK code.
 *  M 1.31+
 *		1. Let Rogue M support kernel 4.4.
 *		2. Close OPEN_GPU_PD temporarily.
 *  M 1.31_2
 *		1. Add GPU dvfs support.
 *		2. Adjust the code indentation.
 *		3. Use late_initcall instead of module_init to load gpu driver.
 *		4. Add GPU pd support.
 *		5. Disable RD power island.
 */

#define PVR_STR(X) #X
#define PVR_STR2(X) PVR_STR(X)

#define PVRVERSION_MAJ               1
#define PVRVERSION_MIN               5

#define PVRVERSION_FAMILY           "rogueddk"
#define PVRVERSION_BRANCHNAME       "1.5"
#define PVRVERSION_BUILD             3830101
#define PVRVERSION_BSCONTROL        "Rogue_DDK_Android"

#define PVRVERSION_STRING           "Rogue_DDK_Android rogueddk 1.5@" PVR_STR2(PVRVERSION_BUILD)
#define PVRVERSION_STRING_SHORT     "1.5@" PVR_STR2(PVRVERSION_BUILD) ""

#define COPYRIGHT_TXT               "Copyright (c) Imagination Technologies Ltd. All Rights Reserved."

#define PVRVERSION_BUILD_HI          383
#define PVRVERSION_BUILD_LO          101
#define PVRVERSION_STRING_NUMERIC    PVR_STR2(PVRVERSION_MAJ) "." PVR_STR2(PVRVERSION_MIN) "." PVR_STR2(PVRVERSION_BUILD_HI) "." PVR_STR2(PVRVERSION_BUILD_LO)

#define PVRVERSION_PACK(MAJ,MIN) ((((MAJ)&0xFFFF) << 16) | (((MIN)&0xFFFF) << 0))
#define PVRVERSION_UNPACK_MAJ(VERSION) (((VERSION) >> 16) & 0xFFFF)
#define PVRVERSION_UNPACK_MIN(VERSION) (((VERSION) >> 0) & 0xFFFF)

//chenli:define rockchip version
#define RKVERSION                   "Rogue M 1.31+"
#endif /* _PVRVERSION_H_ */
