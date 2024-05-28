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

	struct amdgpu_mem_stats stats;
	ktime_t usage[AMDGPU_HW_IP_NUM];
	unsigned int hw_ip;
	int ret;

	memset(&stats, 0, sizeof(stats));

	ret = amdgpu_bo_reserve(vm->root.bo, false);
	if (ret)
		return;

	amdgpu_vm_get_memory(vm, &stats);
	amdgpu_bo_unreserve(vm->root.bo);

	amdgpu_ctx_mgr_usage(&fpriv->ctx_mgr, usage);

	/*
	 * ******************************************************************
	 * For text output format description please see drm-usage-stats.rst!
	 * ******************************************************************
	 */

	drm_printf(p, "pasid:\t%u\n", fpriv->vm.pasid);
	drm_printf(p, "drm-memory-vram:\t%llu KiB\n", stats.vram/1024UL);
	drm_printf(p, "drm-memory-gtt: \t%llu KiB\n", stats.gtt/1024UL);
	drm_printf(p, "drm-memory-cpu: \t%llu KiB\n", stats.cpu/1024UL);
	drm_printf(p, "amd-memory-visible-vram:\t%llu KiB\n",
		   stats.visible_vram/1024UL);
	drm_printf(p, "amd-evicted-vram:\t%llu KiB\n",
		   stats.evicted_vram/1024UL);
	drm_printf(p, "amd-evicted-visible-vram:\t%llu KiB\n",
		   stats.evicted_visible_vram/1024UL);
	drm_printf(p, "amd-requested-vram:\t%llu KiB\n",
		   stats.requested_vram/1024UL);
	drm_printf(p, "amd-requested-visible-vram:\t%llu KiB\n",
		   stats.requested_visible_vram/1024UL);
	drm_printf(p, "amd-requested-gtt:\t%llu KiB\n",
		   stats.requested_gtt/1024UL);
	drm_printf(p, "drm-shared-vram:\t%llu KiB\n", stats.vram_shared/1024UL);
	drm_printf(p, "drm-shared-gtt:\t%llu KiB\n", stats.gtt_shared/1024UL);
	drm_printf(p, "drm-shared-cpu:\t%llu KiB\n", stats.cpu_shared/1024UL);

	for (hw_ip = 0; hw_ip < AMDGPU_HW_IP_NUM; ++hw_ip) {
		if (!usage[hw_ip])
			continue;

		drm_printf(p, "drm-engine-%s:\t%lld ns\n", amdgpu_ip_name[hw_ip],
			   ktime_to_ns(usage[hw_ip]));
	}
}
