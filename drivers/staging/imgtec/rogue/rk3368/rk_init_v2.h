/*************************************************************************/ /*!
@Title          System Configuration
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
@Description    System Configuration functions
*/ /**************************************************************************/

#if !defined(__SUNXI_INIT__)
#define __SUNXI_INIT__

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_device.h"
#include "servicesext.h"
#include <linux/version.h>

#define OPEN_GPU_PD				1
#define RK33_DEFAULT_CLOCK			400
#define ONE_KHZ					1000
#define ONE_MHZ					1000000
#define HZ_TO_MHZ(m)				((m) / ONE_MHZ)

struct rk_context {
	PVRSRV_DEVICE_CONFIG	*dev_config;
#if OPEN_GPU_PD
	IMG_BOOL			bEnablePd;
	struct clk			*pd_gpu_0;
	struct clk			*pd_gpu_1;
#endif
	struct clk			*aclk_gpu_mem;
	struct clk			*aclk_gpu_cfg;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	struct clk			    *clk_gpu;
	struct dvfs_node		    *gpu_clk_node;
#else
	struct clk			*sclk_gpu_core;
	struct regulator		*gpu_reg;
#endif

	/*To calculate utilization for x sec */
	IMG_BOOL			gpu_active;
};

#if defined(CONFIG_DEVFREQ_THERMAL) && defined(PVR_DVFS)
int rk_power_model_simple_init(struct device *dev);
#endif

long int GetConfigFreq(void);
IMG_UINT32 AwClockFreqGet(IMG_HANDLE hSysData);
struct rk_context * RgxRkInit(PVRSRV_DEVICE_CONFIG* psDevConfig);
void RgxRkUnInit(struct rk_context *platform);
void RgxResume(struct rk_context *platform);
void RgxSuspend(struct rk_context *platform);
PVRSRV_ERROR RkPrePowerState(IMG_HANDLE hSysData,
							 PVRSRV_DEV_POWER_STATE eNewPowerState,
							 PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							 IMG_BOOL bForced);
PVRSRV_ERROR RkPostPowerState(IMG_HANDLE hSysData,
							  PVRSRV_DEV_POWER_STATE eNewPowerState,
							  PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							  IMG_BOOL bForced);
void rkSetFrequency(IMG_UINT32 ui32Frequency);
void rkSetVoltage(IMG_UINT32 ui32Voltage);
#endif	/* __SUNXI_INIT__ */
