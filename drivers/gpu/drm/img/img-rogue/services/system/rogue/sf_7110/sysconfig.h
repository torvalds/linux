/*************************************************************************/ /*!
@Title          System Description Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides system-specific declarations and macros
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
#include <linux/clk.h>
#include <linux/reset.h>

#include "pvrsrv_device.h"
#include "rgxdevice.h"

#if !defined(__SYSCCONFIG_H__)
#define __SYSCCONFIG_H__

#define STARFIVE_7110_IRQ_GPU 82  //77 + 5
#define STARFIVE_7110_GPU_SIZE 0x00100000
#define STARFIVE_7110_GPU_PBASE 0x18000000

struct sf7110_cfg {
	void __iomem *gpu_reg_base;
	resource_size_t gpu_reg_start;
	resource_size_t gpu_reg_size;

	struct clk *clk_apb;
	struct clk *clk_rtc;
	struct clk *clk_core;
	struct clk *clk_sys;
	struct clk *clk_axi;
	struct reset_control *rst_apb;
	struct reset_control *rst_doma;

	/* mutex protect for set power state */
	struct mutex set_power_state;

	/* for gpu device freq/volt update, to be fill later */
	//struct clk **top_clk;
	struct device *dev;
};

#define mk_crg_offset(x)  ((x) - (U0_SYS_CRG__SAIF_BD_APBS__BASE_ADDR))

#if  defined(__FPGA_VERIFICATION__)
#define RGX_STARFIVE_7100_CORE_CLOCK_SPEED (8 * 1000 * 1000)//actually the CLK is 8M on FPGA platform 
#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (80000)
#else
#define RGX_STARFIVE_7100_CORE_CLOCK_SPEED (409.6 * 1000 * 1000)//maybe 400M?
#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (100)
#endif

extern void do_sifive_l2_flush64_range(unsigned long start, unsigned long len);

/*****************************************************************************
 * system specific data structures
 *****************************************************************************/

#endif	/* __SYSCCONFIG_H__ */
