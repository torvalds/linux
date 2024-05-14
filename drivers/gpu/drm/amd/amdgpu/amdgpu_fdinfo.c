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
};

void amdgpu_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct drm_file *file = f->private_data;
	struct amdgpu_device *adev = drm_to_adev(file->minor->dev);
	struct amdgpu_fpriv *fpriv = file->driver_priv;
	struct amdgpu_vm *vm = &fpriv->vm;

	uint64_t vram_mem = 0, gtt_mem = 0, cpu_mem = 0;
	ktime_t usage[AMDGPU_HW_IP_NUM];
	uint32_t bus, dev, fn, domain;
	unsigned int hw_ip;
	int ret;

	bus = adev->pdev->bus->number;
	domain = pci_domain_nr(adev->pdev->bus);
	dev = PCI_SLOT(adev->pdev->devfn);
	fn = PCI_FUNC(adev->pdev->devfn);

	ret = amdgpu_bo_reserve(vm->root.bo, false);
	if (ret)
		return;

	amdgpu_vm_get_memory(vm, &vram_mem, &gtt_mem, &cpu_mem);
	amdgpu_bo_unreserve(vm->root.bo);

	amdgpu_ctx_mgr_usage(&fpriv->ctx_mgr, usage);

	/*
	 * ******************************************************************
	 * For text output format description please see drm-usage-stats.rst!
	 * ******************************************************************
	 */

	seq_printf(m, "pasid:\t%u\n", fpriv->vm.pasid);
	seq_printf(m, "drm-driver:\t%s\n", file->minor->dev->driver->name);
	seq_printf(m, "drm-pdev:\t%04x:%02x:%02x.%d\n", domain, bus, dev, fn);
	seq_printf(m, "drm-client-id:\t%Lu\n", vm->immediate.fence_context);
	seq_printf(m, "drm-memory-vram:\t%llu KiB\n", vram_mem/1024UL);
	seq_printf(m, "drm-memory-gtt: \t%llu KiB\n", gtt_mem/1024UL);
	seq_printf(m, "drm-memory-cpu: \t%llu KiB\n", cpu_mem/1024UL);
	for (hw_ip = 0; hw_ip < AMDGPU_HW_IP_NUM; ++hw_ip) {
		if (!usage[hw_ip])
			continue;

		seq_printf(m, "drm-engine-%s:\t%Ld ns\n", amdgpu_ip_name[hw_ip],
			   ktime_to_ns(usage[hw_ip]));
	}
}
