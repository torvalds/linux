/*************************************************************************/ /*!
@File           pvr_dvfs.h
@Title          System level interface for DVFS
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#ifndef PVR_DVFS_H
#define PVR_DVFS_H

#include <linux/version.h>

#if defined(SUPPORT_LINUX_DVFS)
 #include <linux/devfreq.h>
 #include <linux/thermal.h>

 #if defined(CONFIG_DEVFREQ_THERMAL)
  #include <linux/devfreq_cooling.h>
 #endif

 #if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
  #include <linux/opp.h>
 #else
  #include <linux/pm_opp.h>
 #endif
#endif

#include "img_types.h"

typedef void (*PFN_SYS_DEV_DVFS_SET_FREQUENCY)(IMG_UINT32 ui32Freq);
typedef void (*PFN_SYS_DEV_DVFS_SET_VOLTAGE)(IMG_UINT32 ui32Volt);

typedef struct _IMG_OPP_
{
	IMG_UINT32			ui32Volt;
	/*
	 * Unit of frequency in Hz.
	 */
	IMG_UINT32			ui32Freq;
} IMG_OPP;

typedef struct _IMG_DVFS_DEVICE_CFG_
{
	const IMG_OPP  *pasOPPTable;
	IMG_UINT32      ui32OPPTableSize;
#if defined(SUPPORT_LINUX_DVFS)
	IMG_UINT32      ui32PollMs;
#endif
	IMG_BOOL        bIdleReq;
	PFN_SYS_DEV_DVFS_SET_FREQUENCY  pfnSetFrequency;
	PFN_SYS_DEV_DVFS_SET_VOLTAGE    pfnSetVoltage;

#if defined(CONFIG_DEVFREQ_THERMAL) && defined(SUPPORT_LINUX_DVFS)
	struct devfreq_cooling_power *psPowerOps;
#endif
} IMG_DVFS_DEVICE_CFG;

#if defined(SUPPORT_LINUX_DVFS)
typedef struct _IMG_DVFS_GOVERNOR_
{
	IMG_BOOL			bEnabled;
} IMG_DVFS_GOVERNOR;

typedef struct _IMG_DVFS_GOVERNOR_CFG_
{
	IMG_UINT32			ui32UpThreshold;
	IMG_UINT32			ui32DownDifferential;
} IMG_DVFS_GOVERNOR_CFG;
#endif

#if defined(__linux__)
#if defined(SUPPORT_LINUX_DVFS)
typedef struct _IMG_DVFS_DEVICE_
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	struct opp				*psOPP;
#else
	struct dev_pm_opp		*psOPP;
#endif
	struct devfreq			*psDevFreq;
	IMG_BOOL			bEnabled;
	IMG_HANDLE			hGpuUtilUserDVFS;
	struct devfreq_simple_ondemand_data data;
#if defined(CONFIG_DEVFREQ_THERMAL)
	struct thermal_cooling_device	*psDevfreqCoolingDevice;
#endif
} IMG_DVFS_DEVICE;
#endif

typedef struct _IMG_DVFS_
{
#if defined(SUPPORT_LINUX_DVFS)
	IMG_DVFS_DEVICE			sDVFSDevice;
	IMG_DVFS_GOVERNOR		sDVFSGovernor;
	IMG_DVFS_GOVERNOR_CFG	sDVFSGovernorCfg;
#endif
	IMG_DVFS_DEVICE_CFG		sDVFSDeviceCfg;
} PVRSRV_DVFS;
#endif/* (__linux__) */

#endif /* PVR_DVFS_H */
