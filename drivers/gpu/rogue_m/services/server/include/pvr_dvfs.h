/*************************************************************************/ /*!
@File           pvr_dvfs.h
@Title          System level interface for DVFS
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This file defined the API between services and system layer
                required for Ion integration.
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

#ifndef _PVR_DVFS_H_
#define _PVR_DVFS_H_

#include "pvrsrv_error.h"
#include "img_types.h"

typedef IMG_VOID (*PFN_SYS_DEV_DVFS_SET_FREQUENCY)(IMG_UINT32 ui64Freq);
typedef IMG_VOID (*PFN_SYS_DEV_DVFS_SET_VOLTAGE)(IMG_UINT32 ui64Volt);

typedef struct _IMG_OPP_
{
	IMG_UINT32			ui32Volt;
	IMG_UINT32			ui32Freq;
} IMG_OPP;

typedef const IMG_OPP* IMG_OPP_TABLE;

typedef struct _IMG_DVFS_GOVERNOR_CFG_
{
	IMG_UINT32			ui32UpThreshold;
	IMG_UINT32			ui32DownDifferential;
} IMG_DVFS_GOVERNOR_CFG;

typedef struct _IMG_DVFS_DEVICE_CFG_
{
	IMG_OPP_TABLE			pasOPPTable;
	IMG_UINT32			ui32OPPTableSize;

	IMG_UINT32			ui32FreqMin;
	IMG_UINT32			ui32FreqMax;
	IMG_UINT32			ui32PollMs;
	IMG_BOOL			bIdleReq;

	PFN_SYS_DEV_DVFS_SET_FREQUENCY	pfnSetFrequency;
	PFN_SYS_DEV_DVFS_SET_VOLTAGE	pfnSetVoltage;
} IMG_DVFS_DEVICE_CFG;

typedef struct _IMG_DVFS_GOVERNOR_
{
	IMG_BOOL			bEnabled;
} IMG_DVFS_GOVERNOR;

#if defined(__linux__)
typedef struct _IMG_DVFS_DEVICE_
{
	POS_LOCK			hDVFSLock;
	struct dev_pm_opp		*psOPP;
	struct devfreq			*psDevFreq;
	IMG_BOOL			bEnabled;
	IMG_HANDLE			hGpuUtilUserDVFS;
} IMG_DVFS_DEVICE;

typedef struct _IMG_POWER_AVG_
{
	IMG_UINT32			ui32Power;
	IMG_UINT32			ui32Samples;
} IMG_POWER_AVG;

typedef struct _IMG_DVFS_PA_
{
	IMG_UINT32			ui32AllocatedPower;
	IMG_UINT32			*aui32ConversionTable;
	IMG_OPP				sOPPCurrent;
	IMG_INT32			i32Temp;
	IMG_UINT64			ui64StartTime;
	IMG_UINT32			ui32Energy;
	POS_LOCK			hDVFSLock;
	struct power_actor		*psPowerActor;
	IMG_POWER_AVG			sPowerAvg;
} IMG_DVFS_PA;

typedef struct _IMG_DVFS_PA_CFG_
{
	/* Coefficients for a curve defining power leakage due to temperature */
	IMG_INT32			i32Ta;		/* t^3 */
	IMG_INT32			i32Tb;		/* t^2 */
	IMG_INT32			i32Tc;		/* t^1 */
	IMG_INT32			i32Td;		/* const */

	IMG_UINT32			ui32Other;	/* Static losses unrelated to GPU */
	IMG_UINT32			ui32Weight;	/* Power actor weight */
} IMG_DVFS_PA_CFG;

typedef struct _IMG_DVFS_
{
	IMG_DVFS_DEVICE			sDVFSDevice;
	IMG_DVFS_GOVERNOR		sDVFSGovernor;
	IMG_DVFS_DEVICE_CFG		sDVFSDeviceCfg;
	IMG_DVFS_GOVERNOR_CFG		sDVFSGovernorCfg;
#if defined(PVR_POWER_ACTOR)
	IMG_DVFS_PA			sDVFSPA;
	IMG_DVFS_PA_CFG			sDVFSPACfg;
#endif
} PVRSRV_DVFS;
#endif/* (__linux__) */

#endif /* _PVR_DVFS_H_ */
