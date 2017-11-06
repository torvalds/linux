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
 *          Support gpu disable dvfs case.
 *          Add rk_tf_check_version to compatible for rk3328.
 *  L 1.18:
 *			If fix freq,then don't force to drop freq to the lowest.
 *
 *  M 1.21:
 *          Merge 1.5_RTM3604260 DDK code.
 *  M 1.24:
 *          Merge 1.5_ED3653583 DDK code.
 *  M 1.28:
 *	    Merge 1.5_ED3776568 DDK code.
 *  M 1.29
 *	    1. Reopen bEnableRDPowIsland since it doesn't appear splash screen when click the drawerbutton.
 *	    2. Don't set PVR_ANDROID_HAS_SET_BUFFERS_DATASPACE by default.
 *	    3. Remove hGPUUtilLock to avoid dead lock.
 *	    4. Get raw ion_device by IonDevAcquire.
 *  M 1.31
 *	    1. Merge 1.5_ED3830101 DDK code.
 *  M 1.31+
 *	    1. Let Rogue M support kernel 4.4.
 *          2. Close OPEN_GPU_PD temporarily.
 *  M 2.00
 *          Init 1.6_ED3859696 DDK code.
 *  M 2.01
 *	    1. Merge 1.6_ED3861161 DDK code
 *	    2. Add GPU dvfs support.
 *	    3. Adjust the code indentation.
 *	    4. Add GPU pd support.
 *	    5. Disable RD power island.
 *  M 3.00
 *          1. Merge 1.7_ED3904583 DDK code.
 *	    2. Add support for kernel 3.10.
 *	    3. Fix some compile error on DDK 1.7.
 *	    4. Fix some running errors on DDK 1.7.
 *  M 3.01
 *		1. Merge 1.7_ED3957769 DDK code.
 *		2. Fix compile error.
 *		3. Fix USE_CLANG bug in preconfig.mk of km module.
 *		4. Fix bug of missing of brackets.
 *		5. Adjust order to judge Rogue gpu.
 *  N 4.00
 *		1. Merge 1.7_Beta_4200570 DDK code.
 *		2. Support for android n.
 *		3. Fix rk dvfs bug.
 *		4. Adjust code style for rk init.
 *  N 4.01
 *		1. Fix rk_init compile error.
 *  N 4.02
 *		1. Merge 1.7_ED4215145 DDK code.
 *		2. Merge 1.7_ED4239735 DDK code.
 *		3. Merge 1.7_ED4276001 DDK code.
 * N 5.00
 *              Merge 1.8_ED4302432 DDK code.
 * N 5.01
 *		1. Add RK33_DVFS_MODE support.
 *		2. Spinlock should not use for rk33_dvfs_set_clock,
 *			since it will sleep if mutex cann't be got.
 * N 5.02
 *		1. Merge 1.8_Beta_4490825 DDK code.
 *		2. Add new support for kernel 4.4.
 *		3. Close RK_TF_VERSION.
 *		4. Remove dependence of rockchip_ion_dev.
 *		5. Fix show freq bug on kernel 4.4.
 *		6. Fix gpu dvfs bug.
 * N 5.03
 *		1. Enable PVR_DVFS for devfreq framework.
 *		2. Remove some unneed code for devfreq.
 * N 5.04
 *		Merge 1.8_ED4610191 DDK code.
 * N 5.05
 *		1. If freq is equal,but voltage is changed,we also set the new voltage.
 *		2. Only give a warnning when initialize simple power model failed.
 * N 5.06
 *		Rebuild ko.
 * N 5.11
 *		Even in suspend mode,we still set the gpu clk.
 * N 5.12
 *		1. Fix PVRSRVDevicePreClockSpeedChange failed.
 *		2. Apply PP_fix_KM patch from IMG.
 * N 5.13
 *		Add gpu performance interface for cts.
 */

#define PVR_STR(X) #X
#define PVR_STR2(X) PVR_STR(X)

#define PVRVERSION_MAJ               1
#define PVRVERSION_MIN               8

#define PVRVERSION_FAMILY           "rogueddk"
#define PVRVERSION_BRANCHNAME       "1.8.RTM"
#define PVRVERSION_BUILD             4610191
#define PVRVERSION_BSCONTROL        "Rogue_DDK_Android"

#define PVRVERSION_STRING           "Rogue_DDK_Android rogueddk 1.8.RTM@" PVR_STR2(PVRVERSION_BUILD)
#define PVRVERSION_STRING_SHORT     "1.8@" PVR_STR2(PVRVERSION_BUILD) " (1.8.RTM)"

#define COPYRIGHT_TXT               "Copyright (c) Imagination Technologies Ltd. All Rights Reserved."

#define PVRVERSION_BUILD_HI          461
#define PVRVERSION_BUILD_LO          191
#define PVRVERSION_STRING_NUMERIC    PVR_STR2(PVRVERSION_MAJ) "." PVR_STR2(PVRVERSION_MIN) "." PVR_STR2(PVRVERSION_BUILD_HI) "." PVR_STR2(PVRVERSION_BUILD_LO)

#define PVRVERSION_PACK(MAJ,MIN) ((((MAJ)&0xFFFF) << 16) | (((MIN)&0xFFFF) << 0))
#define PVRVERSION_UNPACK_MAJ(VERSION) (((VERSION) >> 16) & 0xFFFF)
#define PVRVERSION_UNPACK_MIN(VERSION) (((VERSION) >> 0) & 0xFFFF)

//chenli:define rockchip version
#define RKVERSION                   "Rogue N 5.13"
#endif /* _PVRVERSION_H_ */
