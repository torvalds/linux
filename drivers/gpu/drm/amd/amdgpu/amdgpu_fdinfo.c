// SPDX-License-Identifier: MIT
/* Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: David Nieto
 *          Roy Sun
 */

#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>

#include <drm/amdgpu_drm.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#include "amdgpu.h"
#include "amdgpu_vm.h"
#include "amdgpu_gem.h"
#include "amdgpu_ctx.h"
#include "amdgpu_fdinfo.h"


static const char *amdgpu_ip_name[AMDGPU_HW_IP_NUM] = {
	[AMDGPU_HW_IP_GFX]	=	"gfx",
	[AMDGPU_HW_IP_COMPUTE]	=	"compute",
	[AMDGPU_HW_IP_DMA]	=	"dma",
	[AMDGPU_HW_IP_UVD]	=	"dec",
	[AMDGPU_HW_IP_VCE]	=	"enc",
	[AMDGPU_HW_IP_UVD_ENC]	=	"enc_1",
	[AMDGPU_HW_IP_VCN_DEC]	=	"dec",
	[AMDGPU_HW_IP_VCN_ENC]	=	"enc",
	[AMDGPU_HW_IP_VCN_JPEG]	=	"jpeg",
	[AMDGPU_HW_IP_VPE]	=	"vpe",
};

void amdgpu_show_fdinfo(struct drm_printer *p, struct drm_file *file)
{
	struct amdgpu_fpriv *fpriv = file->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;

	struct amdgpu_mem_stats stats[__AMDGPU_PL_NUM];
	ktime_t usage[AMDGPU_HW_IP_NUM];
	const char *pl_name[] = {
		[TTM_PL_VRAM] = "vram",
		[TTM_PL_TT] = "gtt",
		[TTM_PL_SYSTEM] = "cpu",
		[AMDGPU_PL_GDS] = "gds",
		[AMDGPU_PL_GWS] = "gws",
		[AMDGPU_PL_OA] = "oa",
		[AMDGPU_PL_DOORBELL] = "doorbell",
		[AMDGPU_PL_MMIO_REMAP] = "mmioremap",
	};
	unsigned int hw_ip, i;

	amdgpu_vm_get_memory(vm, stats);
	amdgpu_ctx_mgr_usage(&fpriv->ctx_mgr, usage);

	/*
	 * ******************************************************************
	 * For text output format description please see drm-usage-stats.rst!
	 * ******************************************************************
	 */

	drm_printf(p, "pasid:\t%u\n", fpriv->vm.pasid);

	for (i = 0; i < ARRAY_SIZE(pl_name); i++) {
		if (!pl_name[i])
			continue;

		drm_print_memory_stats(p,
				       &stats[i].drm,
				       DRM_GEM_OBJECT_RESIDENT |
				       DRM_GEM_OBJECT_PURGEABLE,
				       pl_name[i]);
	}

	/* Legacy amdgpu keys, alias to drm-resident-memory-: */
	drm_printf(p, "drm-memory-vram:\t%llu KiB\n",
		   stats[TTM_PL_VRAM].drm.resident/1024UL);
	drm_printf(p, "drm-memory-gtt: \t%llu KiB\n",
		   stats[TTM_PL_TT].drm.resident/1024UL);
	drm_printf(p, "drm-memory-cpu: \t%llu KiB\n",
		   stats[TTM_PL_SYSTEM].drm.resident/1024UL);

	/* Amdgpu specific memory accounting keys: */
	drm_printf(p, "amd-evicted-vram:\t%llu KiB\n",
		   stats[TTM_PL_VRAM].evicted/1024UL);
	drm_printf(p, "amd-requested-vram:\t%llu KiB\n",
		   (stats[TTM_PL_VRAM].drm.shared +
		    stats[TTM_PL_VRAM].drm.private) / 1024UL);
	drm_printf(p, "amd-requested-gtt:\t%llu KiB\n",
		   (stats[TTM_PL_TT].drm.shared +
		    stats[TTM_PL_TT].drm.private) / 1024UL);

	for (hw_ip = 0; hw_ip < AMDGPU_HW_IP_NUM; ++hw_ip) {
		if (!usage[hw_ip])
			continue;

		drm_printf(p, "drm-engine-%s:\t%lld ns\n", amdgpu_ip_name[hw_ip],
			   ktime_to_ns(usage[hw_ip]));
	}
}
