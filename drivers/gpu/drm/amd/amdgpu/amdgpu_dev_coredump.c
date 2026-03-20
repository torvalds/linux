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
void amdgpu_coredump(struct amdgpu_device *adev, bool skip_vram_check,
		     bool vram_lost, struct amdgpu_job *job)
{
}
void amdgpu_coredump_init(struct amdgpu_device *adev)
{
}
#else

#define AMDGPU_CORE_DUMP_SIZE_MAX (256 * 1024 * 1024)

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
amdgpu_devcoredump_format(char *buffer, size_t count, struct amdgpu_coredump_info *coredump)
{
	struct drm_printer p;
	struct drm_print_iterator iter;
	struct amdgpu_vm_fault_info *fault_info;
	struct amdgpu_ip_block *ip_block;
	int ver;

	iter.data = buffer;
	iter.offset = 0;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	drm_printf(&p, "**** AMDGPU Device Coredump ****\n");
	drm_printf(&p, "version: " AMDGPU_COREDUMP_VERSION "\n");
	drm_printf(&p, "kernel: " UTS_RELEASE "\n");
	drm_printf(&p, "module: " KBUILD_MODNAME "\n");
	drm_printf(&p, "time: %ptSp\n", &coredump->reset_time);

	if (coredump->reset_task_info.task.pid)
		drm_printf(&p, "process_name: %s PID: %d\n",
			   coredump->reset_task_info.process_name,
			   coredump->reset_task_info.task.pid);

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
	drm_printf(&p, "gtt size: %llu\n", coredump->adev->mman.gtt_mgr.manager.size);

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

	amdgpu_discovery_dump(coredump->adev, &p);

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
		ip_block = &coredump->adev->ip_blocks[i];
		if (ip_block->version->funcs->print_ip_state) {
			drm_printf(&p, "IP: %s\n", ip_block->version->funcs->name);
			ip_block->version->funcs->print_ip_state(ip_block, &p);
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

	if (coredump->skip_vram_check)
		drm_printf(&p, "VRAM lost check is skipped!\n");
	else if (coredump->reset_vram_lost)
		drm_printf(&p, "VRAM is lost due to GPU reset!\n");

	return count - iter.remain;
}

static ssize_t
amdgpu_devcoredump_read(char *buffer, loff_t offset, size_t count,
			void *data, size_t datalen)
{
	struct amdgpu_coredump_info *coredump = data;
	ssize_t byte_copied;

	if (!coredump)
		return -ENODEV;

	if (!coredump->formatted)
		return -ENODEV;

	if (offset >= coredump->formatted_size)
		return 0;

	byte_copied = count < coredump->formatted_size - offset ? count :
		coredump->formatted_size - offset;
	memcpy(buffer, coredump->formatted + offset, byte_copied);

	return byte_copied;
}

static void amdgpu_devcoredump_free(void *data)
{
	struct amdgpu_coredump_info *coredump = data;

	kvfree(coredump->formatted);
	kvfree(data);
}

static void amdgpu_devcoredump_deferred_work(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, typeof(*adev), coredump_work);
	struct amdgpu_coredump_info *coredump = adev->coredump;

	/* Do a one-time preparation of the coredump output because
	 * repeatingly calling drm_coredump_printer is very slow.
	 */
	coredump->formatted_size = amdgpu_devcoredump_format(
		NULL, AMDGPU_CORE_DUMP_SIZE_MAX, coredump);
	coredump->formatted = kvzalloc(coredump->formatted_size, GFP_KERNEL);
	if (!coredump->formatted) {
		amdgpu_devcoredump_free(coredump);
		goto end;
	}

	amdgpu_devcoredump_format(coredump->formatted, coredump->formatted_size, coredump);

	/* If there's an existing coredump for this device, the free function will be
	 * called immediately so coredump might be invalid after the call to dev_coredumpm.
	 */
	dev_coredumpm(coredump->adev->dev, THIS_MODULE, coredump, 0, GFP_NOWAIT,
		      amdgpu_devcoredump_read, amdgpu_devcoredump_free);

end:
	adev->coredump = NULL;
}

void amdgpu_coredump(struct amdgpu_device *adev, bool skip_vram_check,
		     bool vram_lost, struct amdgpu_job *job)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct amdgpu_coredump_info *coredump;
	struct drm_sched_job *s_job;

	/* No need to generate a new coredump if there's one in progress already. */
	if (work_pending(&adev->coredump_work))
		return;

	coredump = kzalloc_obj(*coredump, GFP_NOWAIT);
	if (!coredump)
		return;

	coredump->skip_vram_check = skip_vram_check;
	coredump->reset_vram_lost = vram_lost;

	if (job && job->pasid) {
		struct amdgpu_task_info *ti;

		ti = amdgpu_vm_get_task_info_pasid(adev, job->pasid);
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

	/* Update the current coredump pointer (no lock needed, this function can only be called
	 * from a single thread)
	 */
	adev->coredump = coredump;
	/* Kick off coredump formatting to a worker thread. */
	queue_work(system_unbound_wq, &adev->coredump_work);

	drm_info(dev, "AMDGPU device coredump file has been created\n");
	drm_info(dev, "Check your /sys/class/drm/card%d/device/devcoredump/data\n",
		 dev->primary->index);
}

void amdgpu_coredump_init(struct amdgpu_device *adev)
{
	INIT_WORK(&adev->coredump_work, amdgpu_devcoredump_deferred_work);
}
#endif
