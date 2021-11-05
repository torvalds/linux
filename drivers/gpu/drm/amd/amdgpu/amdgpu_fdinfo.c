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
	struct amdgpu_fpriv *fpriv;
	uint32_t bus, dev, fn, i, domain;
	uint64_t vram_mem = 0, gtt_mem = 0, cpu_mem = 0;
	struct drm_file *file = f->private_data;
	struct amdgpu_device *adev = drm_to_adev(file->minor->dev);
	struct amdgpu_bo *root;
	int ret;

	ret = amdgpu_file_to_fpriv(f, &fpriv);
	if (ret)
		return;
	bus = adev->pdev->bus->number;
	domain = pci_domain_nr(adev->pdev->bus);
	dev = PCI_SLOT(adev->pdev->devfn);
	fn = PCI_FUNC(adev->pdev->devfn);

	root = amdgpu_bo_ref(fpriv->vm.root.bo);
	if (!root)
		return;

	ret = amdgpu_bo_reserve(root, false);
	if (ret) {
		DRM_ERROR("Fail to reserve bo\n");
		return;
	}
	amdgpu_vm_get_memory(&fpriv->vm, &vram_mem, &gtt_mem, &cpu_mem);
	amdgpu_bo_unreserve(root);
	amdgpu_bo_unref(&root);

	seq_printf(m, "pdev:\t%04x:%02x:%02x.%d\npasid:\t%u\n", domain, bus,
			dev, fn, fpriv->vm.pasid);
	seq_printf(m, "vram mem:\t%llu kB\n", vram_mem/1024UL);
	seq_printf(m, "gtt mem:\t%llu kB\n", gtt_mem/1024UL);
	seq_printf(m, "cpu mem:\t%llu kB\n", cpu_mem/1024UL);
	for (i = 0; i < AMDGPU_HW_IP_NUM; i++) {
		uint32_t count = amdgpu_ctx_num_entities[i];
		int idx = 0;
		uint64_t total = 0, min = 0;
		uint32_t perc, frac;

		for (idx = 0; idx < count; idx++) {
			total = amdgpu_ctx_mgr_fence_usage(&fpriv->ctx_mgr,
				i, idx, &min);
			if ((total == 0) || (min == 0))
				continue;

			perc = div64_u64(10000 * total, min);
			frac = perc % 100;

			seq_printf(m, "%s%d:\t%d.%d%%\n",
					amdgpu_ip_name[i],
					idx, perc/100, frac);
		}
	}
}
