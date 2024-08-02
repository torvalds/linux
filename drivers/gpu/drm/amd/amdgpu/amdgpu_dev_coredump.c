// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
 */

#include <generated/utsrelease.h>
#include <linux/devcoredump.h>
#include "amdgpu_dev_coredump.h"
#include "atom.h"

#ifndef CONFIG_DEV_COREDUMP
void amdgpu_coredump(struct amdgpu_device *adev, bool vram_lost,
		     struct amdgpu_reset_context *reset_context)
{
}
#else

const char *hw_ip_names[MAX_HWIP] = {
	[GC_HWIP]		= "GC",
	[HDP_HWIP]		= "HDP",
	[SDMA0_HWIP]		= "SDMA0",
	[SDMA1_HWIP]		= "SDMA1",
	[SDMA2_HWIP]		= "SDMA2",
	[SDMA3_HWIP]		= "SDMA3",
	[SDMA4_HWIP]		= "SDMA4",
	[SDMA5_HWIP]		= "SDMA5",
	[SDMA6_HWIP]		= "SDMA6",
	[SDMA7_HWIP]		= "SDMA7",
	[LSDMA_HWIP]		= "LSDMA",
	[MMHUB_HWIP]		= "MMHUB",
	[ATHUB_HWIP]		= "ATHUB",
	[NBIO_HWIP]		= "NBIO",
	[MP0_HWIP]		= "MP0",
	[MP1_HWIP]		= "MP1",
	[UVD_HWIP]		= "UVD/JPEG/VCN",
	[VCN1_HWIP]		= "VCN1",
	[VCE_HWIP]		= "VCE",
	[VPE_HWIP]		= "VPE",
	[DF_HWIP]		= "DF",
	[DCE_HWIP]		= "DCE",
	[OSSSYS_HWIP]		= "OSSSYS",
	[SMUIO_HWIP]		= "SMUIO",
	[PWR_HWIP]		= "PWR",
	[NBIF_HWIP]		= "NBIF",
	[THM_HWIP]		= "THM",
	[CLK_HWIP]		= "CLK",
	[UMC_HWIP]		= "UMC",
	[RSMU_HWIP]		= "RSMU",
	[XGMI_HWIP]		= "XGMI",
	[DCI_HWIP]		= "DCI",
	[PCIE_HWIP]		= "PCIE",
};

static void amdgpu_devcoredump_fw_info(struct amdgpu_device *adev,
				       struct drm_printer *p)
{
	uint32_t version;
	uint32_t feature;
	uint8_t smu_program, smu_major, smu_minor, smu_debug;
	struct atom_context *ctx = adev->mode_info.atom_context;

	drm_printf(p, "VCE feature version: %u, fw version: 0x%08x\n",
		   adev->vce.fb_version, adev->vce.fw_version);
	drm_printf(p, "UVD feature version: %u, fw version: 0x%08x\n", 0,
		   adev->uvd.fw_version);
	drm_printf(p, "GMC feature version: %u, fw version: 0x%08x\n", 0,
		   adev->gmc.fw_version);
	drm_printf(p, "ME feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.me_feature_version, adev->gfx.me_fw_version);
	drm_printf(p, "PFP feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.pfp_feature_version, adev->gfx.pfp_fw_version);
	drm_printf(p, "CE feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.ce_feature_version, adev->gfx.ce_fw_version);
	drm_printf(p, "RLC feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlc_feature_version, adev->gfx.rlc_fw_version);

	drm_printf(p, "RLC SRLC feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlc_srlc_feature_version,
		   adev->gfx.rlc_srlc_fw_version);
	drm_printf(p, "RLC SRLG feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlc_srlg_feature_version,
		   adev->gfx.rlc_srlg_fw_version);
	drm_printf(p, "RLC SRLS feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlc_srls_feature_version,
		   adev->gfx.rlc_srls_fw_version);
	drm_printf(p, "RLCP feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlcp_ucode_feature_version,
		   adev->gfx.rlcp_ucode_version);
	drm_printf(p, "RLCV feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.rlcv_ucode_feature_version,
		   adev->gfx.rlcv_ucode_version);
	drm_printf(p, "MEC feature version: %u, fw version: 0x%08x\n",
		   adev->gfx.mec_feature_version, adev->gfx.mec_fw_version);

	if (adev->gfx.mec2_fw)
		drm_printf(p, "MEC2 feature version: %u, fw version: 0x%08x\n",
			   adev->gfx.mec2_feature_version,
			   adev->gfx.mec2_fw_version);

	drm_printf(p, "IMU feature version: %u, fw version: 0x%08x\n", 0,
		   adev->gfx.imu_fw_version);
	drm_printf(p, "PSP SOS feature version: %u, fw version: 0x%08x\n",
		   adev->psp.sos.feature_version, adev->psp.sos.fw_version);
	drm_printf(p, "PSP ASD feature version: %u, fw version: 0x%08x\n",
		   adev->psp.asd_context.bin_desc.feature_version,
		   adev->psp.asd_context.bin_desc.fw_version);

	drm_printf(p, "TA XGMI feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.xgmi_context.context.bin_desc.feature_version,
		   adev->psp.xgmi_context.context.bin_desc.fw_version);
	drm_printf(p, "TA RAS feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.ras_context.context.bin_desc.feature_version,
		   adev->psp.ras_context.context.bin_desc.fw_version);
	drm_printf(p, "TA HDCP feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.hdcp_context.context.bin_desc.feature_version,
		   adev->psp.hdcp_context.context.bin_desc.fw_version);
	drm_printf(p, "TA DTM feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.dtm_context.context.bin_desc.feature_version,
		   adev->psp.dtm_context.context.bin_desc.fw_version);
	drm_printf(p, "TA RAP feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.rap_context.context.bin_desc.feature_version,
		   adev->psp.rap_context.context.bin_desc.fw_version);
	drm_printf(p,
		   "TA SECURE DISPLAY feature version: 0x%08x, fw version: 0x%08x\n",
		   adev->psp.securedisplay_context.context.bin_desc.feature_version,
		   adev->psp.securedisplay_context.context.bin_desc.fw_version);

	/* SMC firmware */
	version = adev->pm.fw_version;

	smu_program = (version >> 24) & 0xff;
	smu_major = (version >> 16) & 0xff;
	smu_minor = (version >> 8) & 0xff;
	smu_debug = (version >> 0) & 0xff;
	drm_printf(p,
		   "SMC feature version: %u, program: %d, fw version: 0x%08x (%d.%d.%d)\n",
		   0, smu_program, version, smu_major, smu_minor, smu_debug);

	/* SDMA firmware */
	for (int i = 0; i < adev->sdma.num_instances; i++) {
		drm_printf(p,
			   "SDMA%d feature version: %u, firmware version: 0x%08x\n",
			   i, adev->sdma.instance[i].feature_version,
			   adev->sdma.instance[i].fw_version);
	}

	drm_printf(p, "VCN feature version: %u, fw version: 0x%08x\n", 0,
		   adev->vcn.fw_version);
	drm_printf(p, "DMCU feature version: %u, fw version: 0x%08x\n", 0,
		   adev->dm.dmcu_fw_version);
	drm_printf(p, "DMCUB feature version: %u, fw version: 0x%08x\n", 0,
		   adev->dm.dmcub_fw_version);
	drm_printf(p, "PSP TOC feature version: %u, fw version: 0x%08x\n",
		   adev->psp.toc.feature_version, adev->psp.toc.fw_version);

	version = adev->mes.kiq_version & AMDGPU_MES_VERSION_MASK;
	feature = (adev->mes.kiq_version & AMDGPU_MES_FEAT_VERSION_MASK) >>
		  AMDGPU_MES_FEAT_VERSION_SHIFT;
	drm_printf(p, "MES_KIQ feature version: %u, fw version: 0x%08x\n",
		   feature, version);

	version = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;
	feature = (adev->mes.sched_version & AMDGPU_MES_FEAT_VERSION_MASK) >>
		  AMDGPU_MES_FEAT_VERSION_SHIFT;
	drm_printf(p, "MES feature version: %u, fw version: 0x%08x\n", feature,
		   version);

	drm_printf(p, "VPE feature version: %u, fw version: 0x%08x\n",
		   adev->vpe.feature_version, adev->vpe.fw_version);

	drm_printf(p, "\nVBIOS Information\n");
	drm_printf(p, "vbios name       : %s\n", ctx->name);
	drm_printf(p, "vbios pn         : %s\n", ctx->vbios_pn);
	drm_printf(p, "vbios version    : %d\n", ctx->version);
	drm_printf(p, "vbios ver_str    : %s\n", ctx->vbios_ver_str);
	drm_printf(p, "vbios date       : %s\n", ctx->date);
}

static ssize_t
amdgpu_devcoredump_read(char *buffer, loff_t offset, size_t count,
			void *data, size_t datalen)
{
	struct drm_printer p;
	struct amdgpu_coredump_info *coredump = data;
	struct drm_print_iterator iter;
	struct amdgpu_vm_fault_info *fault_info;
	int i, ver;

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	drm_printf(&p, "**** AMDGPU Device Coredump ****\n");
	drm_printf(&p, "version: " AMDGPU_COREDUMP_VERSION "\n");
	drm_printf(&p, "kernel: " UTS_RELEASE "\n");
	drm_printf(&p, "module: " KBUILD_MODNAME "\n");
	drm_printf(&p, "time: %lld.%09ld\n", coredump->reset_time.tv_sec,
		   coredump->reset_time.tv_nsec);

	if (coredump->reset_task_info.pid)
		drm_printf(&p, "process_name: %s PID: %d\n",
			   coredump->reset_task_info.process_name,
			   coredump->reset_task_info.pid);

	/* SOC Information */
	drm_printf(&p, "\nSOC Information\n");
	drm_printf(&p, "SOC Device id: %d\n", coredump->adev->pdev->device);
	drm_printf(&p, "SOC PCI Revision id: %d\n", coredump->adev->pdev->revision);
	drm_printf(&p, "SOC Family: %d\n", coredump->adev->family);
	drm_printf(&p, "SOC Revision id: %d\n", coredump->adev->rev_id);
	drm_printf(&p, "SOC External Revision id: %d\n", coredump->adev->external_rev_id);

	/* Memory Information */
	drm_printf(&p, "\nSOC Memory Information\n");
	drm_printf(&p, "real vram size: %llu\n", coredump->adev->gmc.real_vram_size);
	drm_printf(&p, "visible vram size: %llu\n", coredump->adev->gmc.visible_vram_size);
	drm_printf(&p, "visible vram size: %llu\n", coredump->adev->mman.gtt_mgr.manager.size);

	/* GDS Config */
	drm_printf(&p, "\nGDS Config\n");
	drm_printf(&p, "gds: total size: %d\n", coredump->adev->gds.gds_size);
	drm_printf(&p, "gds: compute partition size: %d\n", coredump->adev->gds.gds_size);
	drm_printf(&p, "gds: gws per compute partition: %d\n", coredump->adev->gds.gws_size);
	drm_printf(&p, "gds: os per compute partition: %d\n", coredump->adev->gds.oa_size);

	/* HWIP Version Information */
	drm_printf(&p, "\nHW IP Version Information\n");
	for (int i = 1; i < MAX_HWIP; i++) {
		for (int j = 0; j < HWIP_MAX_INSTANCE; j++) {
			ver = coredump->adev->ip_versions[i][j];
			if (ver)
				drm_printf(&p, "HWIP: %s[%d][%d]: v%d.%d.%d.%d.%d\n",
					   hw_ip_names[i], i, j,
					   IP_VERSION_MAJ(ver),
					   IP_VERSION_MIN(ver),
					   IP_VERSION_REV(ver),
					   IP_VERSION_VARIANT(ver),
					   IP_VERSION_SUBREV(ver));
		}
	}

	/* IP firmware information */
	drm_printf(&p, "\nIP Firmwares\n");
	amdgpu_devcoredump_fw_info(coredump->adev, &p);

	if (coredump->ring) {
		drm_printf(&p, "\nRing timed out details\n");
		drm_printf(&p, "IP Type: %d Ring Name: %s\n",
			   coredump->ring->funcs->type,
			   coredump->ring->name);
	}

	/* Add page fault information */
	fault_info = &coredump->adev->vm_manager.fault_info;
	drm_printf(&p, "\n[%s] Page fault observed\n",
		   fault_info->vmhub ? "mmhub" : "gfxhub");
	drm_printf(&p, "Faulty page starting at address: 0x%016llx\n", fault_info->addr);
	drm_printf(&p, "Protection fault status register: 0x%x\n\n", fault_info->status);

	/* dump the ip state for each ip */
	drm_printf(&p, "IP Dump\n");
	for (int i = 0; i < coredump->adev->num_ip_blocks; i++) {
		if (coredump->adev->ip_blocks[i].version->funcs->print_ip_state) {
			drm_printf(&p, "IP: %s\n",
				   coredump->adev->ip_blocks[i]
					   .version->funcs->name);
			coredump->adev->ip_blocks[i]
				.version->funcs->print_ip_state(
					(void *)coredump->adev, &p);
			drm_printf(&p, "\n");
		}
	}

	/* Add ring buffer information */
	drm_printf(&p, "Ring buffer information\n");
	for (int i = 0; i < coredump->adev->num_rings; i++) {
		int j = 0;
		struct amdgpu_ring *ring = coredump->adev->rings[i];

		drm_printf(&p, "ring name: %s\n", ring->name);
		drm_printf(&p, "Rptr: 0x%llx Wptr: 0x%llx RB mask: %x\n",
			   amdgpu_ring_get_rptr(ring),
			   amdgpu_ring_get_wptr(ring),
			   ring->buf_mask);
		drm_printf(&p, "Ring size in dwords: %d\n",
			   ring->ring_size / 4);
		drm_printf(&p, "Ring contents\n");
		drm_printf(&p, "Offset \t Value\n");

		while (j < ring->ring_size) {
			drm_printf(&p, "0x%x \t 0x%x\n", j, ring->ring[j / 4]);
			j += 4;
		}
	}

	if (coredump->reset_vram_lost)
		drm_printf(&p, "VRAM is lost due to GPU reset!\n");
	if (coredump->adev->reset_info.num_regs) {
		drm_printf(&p, "AMDGPU register dumps:\nOffset:     Value:\n");

		for (i = 0; i < coredump->adev->reset_info.num_regs; i++)
			drm_printf(&p, "0x%08x: 0x%08x\n",
				   coredump->adev->reset_info.reset_dump_reg_list[i],
				   coredump->adev->reset_info.reset_dump_reg_value[i]);
	}

	return count - iter.remain;
}

static void amdgpu_devcoredump_free(void *data)
{
	kfree(data);
}

void amdgpu_coredump(struct amdgpu_device *adev, bool vram_lost,
		     struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_coredump_info *coredump;
	struct drm_device *dev = adev_to_drm(adev);
	struct amdgpu_job *job = reset_context->job;
	struct drm_sched_job *s_job;

	coredump = kzalloc(sizeof(*coredump), GFP_NOWAIT);

	if (!coredump) {
		DRM_ERROR("%s: failed to allocate memory for coredump\n", __func__);
		return;
	}

	coredump->reset_vram_lost = vram_lost;

	if (reset_context->job && reset_context->job->vm) {
		struct amdgpu_task_info *ti;
		struct amdgpu_vm *vm = reset_context->job->vm;

		ti = amdgpu_vm_get_task_info_vm(vm);
		if (ti) {
			coredump->reset_task_info = *ti;
			amdgpu_vm_put_task_info(ti);
		}
	}

	if (job) {
		s_job = &job->base;
		coredump->ring = to_amdgpu_ring(s_job->sched);
	}

	coredump->adev = adev;

	ktime_get_ts64(&coredump->reset_time);

	dev_coredumpm(dev->dev, THIS_MODULE, coredump, 0, GFP_NOWAIT,
		      amdgpu_devcoredump_read, amdgpu_devcoredump_free);
}
#endif
