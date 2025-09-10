/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_sdma.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"
#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_3_0_sh_mask.h"

#define AMDGPU_CSA_SDMA_SIZE 64
/* SDMA CSA reside in the 3rd page of CSA */
#define AMDGPU_CSA_SDMA_OFFSET (4096 * 2)

/*
 * GPU SDMA IP block helpers function.
 */

struct amdgpu_sdma_instance *amdgpu_sdma_get_instance_from_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		if (ring == &adev->sdma.instance[i].ring ||
		    ring == &adev->sdma.instance[i].page)
			return &adev->sdma.instance[i];

	return NULL;
}

int amdgpu_sdma_get_index_from_ring(struct amdgpu_ring *ring, uint32_t *index)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (ring == &adev->sdma.instance[i].ring ||
			ring == &adev->sdma.instance[i].page) {
			*index = i;
			return 0;
		}
	}

	return -EINVAL;
}

uint64_t amdgpu_sdma_get_csa_mc_addr(struct amdgpu_ring *ring,
				     unsigned int vmid)
{
	struct amdgpu_device *adev = ring->adev;
	uint64_t csa_mc_addr;
	uint32_t index = 0;
	int r;

	/* don't enable OS preemption on SDMA under SRIOV */
	if (amdgpu_sriov_vf(adev) || vmid == 0 || !adev->gfx.mcbp)
		return 0;

	r = amdgpu_sdma_get_index_from_ring(ring, &index);

	if (r || index > 31)
		csa_mc_addr = 0;
	else
		csa_mc_addr = amdgpu_csa_vaddr(adev) +
			AMDGPU_CSA_SDMA_OFFSET +
			index * AMDGPU_CSA_SDMA_SIZE;

	return csa_mc_addr;
}

int amdgpu_sdma_ras_late_init(struct amdgpu_device *adev,
			      struct ras_common_if *ras_block)
{
	int r, i;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	if (amdgpu_ras_is_supported(adev, ras_block->block)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			r = amdgpu_irq_get(adev, &adev->sdma.ecc_irq,
				AMDGPU_SDMA_IRQ_INSTANCE0 + i);
			if (r)
				goto late_fini;
		}
	}

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
}

int amdgpu_sdma_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry)
{
	kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);

	if (amdgpu_sriov_vf(adev))
		return AMDGPU_RAS_SUCCESS;

	amdgpu_ras_reset_gpu(adev);

	return AMDGPU_RAS_SUCCESS;
}

int amdgpu_sdma_process_ecc_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->sdma.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;

	amdgpu_ras_interrupt_dispatch(adev, &ih_data);
	return 0;
}

static int amdgpu_sdma_init_inst_ctx(struct amdgpu_sdma_instance *sdma_inst)
{
	uint16_t version_major;
	const struct common_firmware_header *header = NULL;
	const struct sdma_firmware_header_v1_0 *hdr;
	const struct sdma_firmware_header_v2_0 *hdr_v2;
	const struct sdma_firmware_header_v3_0 *hdr_v3;

	header = (const struct common_firmware_header *)
		sdma_inst->fw->data;
	version_major = le16_to_cpu(header->header_version_major);

	switch (version_major) {
	case 1:
		hdr = (const struct sdma_firmware_header_v1_0 *)sdma_inst->fw->data;
		sdma_inst->fw_version = le32_to_cpu(hdr->header.ucode_version);
		sdma_inst->feature_version = le32_to_cpu(hdr->ucode_feature_version);
		break;
	case 2:
		hdr_v2 = (const struct sdma_firmware_header_v2_0 *)sdma_inst->fw->data;
		sdma_inst->fw_version = le32_to_cpu(hdr_v2->header.ucode_version);
		sdma_inst->feature_version = le32_to_cpu(hdr_v2->ucode_feature_version);
		break;
	case 3:
		hdr_v3 = (const struct sdma_firmware_header_v3_0 *)sdma_inst->fw->data;
		sdma_inst->fw_version = le32_to_cpu(hdr_v3->header.ucode_version);
		sdma_inst->feature_version = le32_to_cpu(hdr_v3->ucode_feature_version);
		break;
	default:
		return -EINVAL;
	}

	if (sdma_inst->feature_version >= 20)
		sdma_inst->burst_nop = true;

	return 0;
}

void amdgpu_sdma_destroy_inst_ctx(struct amdgpu_device *adev,
				  bool duplicate)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		amdgpu_ucode_release(&adev->sdma.instance[i].fw);
		if (duplicate)
			break;
	}

	memset((void *)adev->sdma.instance, 0,
	       sizeof(struct amdgpu_sdma_instance) * AMDGPU_MAX_SDMA_INSTANCES);
}

int amdgpu_sdma_init_microcode(struct amdgpu_device *adev,
			       u32 instance, bool duplicate)
{
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	int err, i;
	const struct sdma_firmware_header_v2_0 *sdma_hdr;
	const struct sdma_firmware_header_v3_0 *sdma_hv3;
	uint16_t version_major;
	char ucode_prefix[30];

	amdgpu_ucode_ip_version_decode(adev, SDMA0_HWIP, ucode_prefix, sizeof(ucode_prefix));
	if (instance == 0)
		err = amdgpu_ucode_request(adev, &adev->sdma.instance[instance].fw,
					   AMDGPU_UCODE_REQUIRED,
					   "amdgpu/%s.bin", ucode_prefix);
	else
		err = amdgpu_ucode_request(adev, &adev->sdma.instance[instance].fw,
					   AMDGPU_UCODE_REQUIRED,
					   "amdgpu/%s%d.bin", ucode_prefix, instance);
	if (err)
		goto out;

	header = (const struct common_firmware_header *)
		adev->sdma.instance[instance].fw->data;
	version_major = le16_to_cpu(header->header_version_major);

	if ((duplicate && instance) || (!duplicate && version_major > 1)) {
		err = -EINVAL;
		goto out;
	}

	err = amdgpu_sdma_init_inst_ctx(&adev->sdma.instance[instance]);
	if (err)
		goto out;

	if (duplicate) {
		for (i = 1; i < adev->sdma.num_instances; i++)
			memcpy((void *)&adev->sdma.instance[i],
			       (void *)&adev->sdma.instance[0],
			       sizeof(struct amdgpu_sdma_instance));
	}

	DRM_DEBUG("psp_load == '%s'\n",
		  adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ? "true" : "false");

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		switch (version_major) {
		case 1:
			for (i = 0; i < adev->sdma.num_instances; i++) {
				if (!duplicate && (instance != i))
					continue;
				else {
					/* Use a single copy per SDMA firmware type. PSP uses the same instance for all
					 * groups of SDMAs */
					if ((amdgpu_ip_version(adev, SDMA0_HWIP, 0) ==
						IP_VERSION(4, 4, 2) ||
					     amdgpu_ip_version(adev, SDMA0_HWIP, 0) ==
						IP_VERSION(4, 4, 4) ||
					     amdgpu_ip_version(adev, SDMA0_HWIP, 0) ==
						IP_VERSION(4, 4, 5)) &&
					    adev->firmware.load_type ==
						AMDGPU_FW_LOAD_PSP &&
					    adev->sdma.num_inst_per_aid == i) {
						break;
					}
					info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA0 + i];
					info->ucode_id = AMDGPU_UCODE_ID_SDMA0 + i;
					info->fw = adev->sdma.instance[i].fw;
					adev->firmware.fw_size +=
						ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
				}
			}
			break;
		case 2:
			sdma_hdr = (const struct sdma_firmware_header_v2_0 *)
				adev->sdma.instance[0].fw->data;
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA_UCODE_TH0];
			info->ucode_id = AMDGPU_UCODE_ID_SDMA_UCODE_TH0;
			info->fw = adev->sdma.instance[0].fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(sdma_hdr->ctx_ucode_size_bytes), PAGE_SIZE);
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA_UCODE_TH1];
			info->ucode_id = AMDGPU_UCODE_ID_SDMA_UCODE_TH1;
			info->fw = adev->sdma.instance[0].fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(sdma_hdr->ctl_ucode_size_bytes), PAGE_SIZE);
			break;
		case 3:
			sdma_hv3 = (const struct sdma_firmware_header_v3_0 *)
				adev->sdma.instance[0].fw->data;
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA_RS64];
			info->ucode_id = AMDGPU_UCODE_ID_SDMA_RS64;
			info->fw = adev->sdma.instance[0].fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(sdma_hv3->ucode_size_bytes), PAGE_SIZE);
			break;
		default:
			err = -EINVAL;
		}
	}

out:
	if (err)
		amdgpu_sdma_destroy_inst_ctx(adev, duplicate);
	return err;
}

int amdgpu_sdma_ras_sw_init(struct amdgpu_device *adev)
{
	int err = 0;
	struct amdgpu_sdma_ras *ras = NULL;

	/* adev->sdma.ras is NULL, which means sdma does not
	 * support ras function, then do nothing here.
	 */
	if (!adev->sdma.ras)
		return 0;

	ras = adev->sdma.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register sdma ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "sdma");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__SDMA;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->sdma.ras_if = &ras->ras_block.ras_comm;

	/* If not define special ras_late_init function, use default ras_late_init */
	if (!ras->ras_block.ras_late_init)
		ras->ras_block.ras_late_init = amdgpu_sdma_ras_late_init;

	/* If not defined special ras_cb function, use default ras_cb */
	if (!ras->ras_block.ras_cb)
		ras->ras_block.ras_cb = amdgpu_sdma_process_ras_data_cb;

	return 0;
}

/*
 * debugfs for to enable/disable sdma job submission to specific core.
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_sdma_sched_mask_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u64 i, num_ring;
	u64 mask = 0;
	struct amdgpu_ring *ring, *page = NULL;

	if (!adev)
		return -ENODEV;

	/* Determine the number of rings per SDMA instance
	 * (1 for sdma gfx ring, 2 if page queue exists)
	 */
	if (adev->sdma.has_page_queue)
		num_ring = 2;
	else
		num_ring = 1;

	/* Calculate the maximum possible mask value
	 * based on the number of SDMA instances and rings
	*/
	mask = BIT_ULL(adev->sdma.num_instances * num_ring) - 1;

	if ((val & mask) == 0)
		return -EINVAL;

	for (i = 0; i < adev->sdma.num_instances; ++i) {
		ring = &adev->sdma.instance[i].ring;
		if (adev->sdma.has_page_queue)
			page = &adev->sdma.instance[i].page;
		if (val & BIT_ULL(i * num_ring))
			ring->sched.ready = true;
		else
			ring->sched.ready = false;

		if (page) {
			if (val & BIT_ULL(i * num_ring + 1))
				page->sched.ready = true;
			else
				page->sched.ready = false;
		}
	}
	/* publish sched.ready flag update effective immediately across smp */
	smp_rmb();
	return 0;
}

static int amdgpu_debugfs_sdma_sched_mask_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u64 i, num_ring;
	u64 mask = 0;
	struct amdgpu_ring *ring, *page = NULL;

	if (!adev)
		return -ENODEV;

	/* Determine the number of rings per SDMA instance
	 * (1 for sdma gfx ring, 2 if page queue exists)
	 */
	if (adev->sdma.has_page_queue)
		num_ring = 2;
	else
		num_ring = 1;

	for (i = 0; i < adev->sdma.num_instances; ++i) {
		ring = &adev->sdma.instance[i].ring;
		if (adev->sdma.has_page_queue)
			page = &adev->sdma.instance[i].page;

		if (ring->sched.ready)
			mask |= BIT_ULL(i * num_ring);
		else
			mask &= ~BIT_ULL(i * num_ring);

		if (page) {
			if (page->sched.ready)
				mask |= BIT_ULL(i * num_ring + 1);
			else
				mask &= ~BIT_ULL(i * num_ring + 1);
		}
	}

	*val = mask;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_debugfs_sdma_sched_mask_fops,
			 amdgpu_debugfs_sdma_sched_mask_get,
			 amdgpu_debugfs_sdma_sched_mask_set, "%llx\n");

#endif

void amdgpu_debugfs_sdma_sched_mask_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	if (!(adev->sdma.num_instances > 1))
		return;
	sprintf(name, "amdgpu_sdma_sched_mask");
	debugfs_create_file(name, 0600, root, adev,
			    &amdgpu_debugfs_sdma_sched_mask_fops);
#endif
}

static ssize_t amdgpu_get_sdma_reset_mask(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (!adev)
		return -ENODEV;

	return amdgpu_show_reset_mask(buf, adev->sdma.supported_reset);
}

static DEVICE_ATTR(sdma_reset_mask, 0444,
		   amdgpu_get_sdma_reset_mask, NULL);

int amdgpu_sdma_sysfs_reset_mask_init(struct amdgpu_device *adev)
{
	int r = 0;

	if (!amdgpu_gpu_recovery)
		return r;

	if (adev->sdma.num_instances) {
		r = device_create_file(adev->dev, &dev_attr_sdma_reset_mask);
		if (r)
			return r;
	}

	return r;
}

void amdgpu_sdma_sysfs_reset_mask_fini(struct amdgpu_device *adev)
{
	if (!amdgpu_gpu_recovery)
		return;

	if (adev->dev->kobj.sd) {
		if (adev->sdma.num_instances)
			device_remove_file(adev->dev, &dev_attr_sdma_reset_mask);
	}
}

struct amdgpu_ring *amdgpu_sdma_get_shared_ring(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	if (adev->sdma.has_page_queue &&
	    (ring->me < adev->sdma.num_instances) &&
	    (ring == &adev->sdma.instance[ring->me].ring))
		return &adev->sdma.instance[ring->me].page;
	else
		return NULL;
}

/**
* amdgpu_sdma_is_shared_inv_eng - Check if a ring is an SDMA ring that shares a VM invalidation engine
* @adev: Pointer to the AMDGPU device structure
* @ring: Pointer to the ring structure to check
*
* This function checks if the given ring is an SDMA ring that shares a VM invalidation engine.
* It returns true if the ring is such an SDMA ring, false otherwise.
*/
bool amdgpu_sdma_is_shared_inv_eng(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	int i = ring->me;

	if (!adev->sdma.has_page_queue || i >= adev->sdma.num_instances)
		return false;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 5, 0))
		return (ring == &adev->sdma.instance[i].page);
	else
		return false;
}

static int amdgpu_sdma_soft_reset(struct amdgpu_device *adev, u32 instance_id)
{
	struct amdgpu_sdma_instance *sdma_instance = &adev->sdma.instance[instance_id];

	if (sdma_instance->funcs->soft_reset_kernel_queue)
		return sdma_instance->funcs->soft_reset_kernel_queue(adev, instance_id);

	return -EOPNOTSUPP;
}

/**
 * amdgpu_sdma_reset_engine - Reset a specific SDMA engine
 * @adev: Pointer to the AMDGPU device
 * @instance_id: Logical ID of the SDMA engine instance to reset
 * @caller_handles_kernel_queues: Skip kernel queue processing. Caller
 * will handle it.
 *
 * Returns: 0 on success, or a negative error code on failure.
 */
int amdgpu_sdma_reset_engine(struct amdgpu_device *adev, uint32_t instance_id,
			     bool caller_handles_kernel_queues)
{
	int ret = 0;
	struct amdgpu_sdma_instance *sdma_instance = &adev->sdma.instance[instance_id];
	struct amdgpu_ring *gfx_ring = &sdma_instance->ring;
	struct amdgpu_ring *page_ring = &sdma_instance->page;

	mutex_lock(&sdma_instance->engine_reset_mutex);

	if (!caller_handles_kernel_queues) {
		/* Stop the scheduler's work queue for the GFX and page rings if they are running.
		 * This ensures that no new tasks are submitted to the queues while
		 * the reset is in progress.
		 */
		drm_sched_wqueue_stop(&gfx_ring->sched);

		if (adev->sdma.has_page_queue)
			drm_sched_wqueue_stop(&page_ring->sched);
	}

	if (sdma_instance->funcs->stop_kernel_queue) {
		sdma_instance->funcs->stop_kernel_queue(gfx_ring);
		if (adev->sdma.has_page_queue)
			sdma_instance->funcs->stop_kernel_queue(page_ring);
	}

	/* Perform the SDMA reset for the specified instance */
	ret = amdgpu_sdma_soft_reset(adev, instance_id);
	if (ret) {
		dev_err(adev->dev, "Failed to reset SDMA logical instance %u\n", instance_id);
		goto exit;
	}

	if (sdma_instance->funcs->start_kernel_queue) {
		sdma_instance->funcs->start_kernel_queue(gfx_ring);
		if (adev->sdma.has_page_queue)
			sdma_instance->funcs->start_kernel_queue(page_ring);
	}

exit:
	if (!caller_handles_kernel_queues) {
		/* Restart the scheduler's work queue for the GFX and page rings
		 * if they were stopped by this function. This allows new tasks
		 * to be submitted to the queues after the reset is complete.
		 */
		if (!ret) {
			amdgpu_fence_driver_force_completion(gfx_ring);
			drm_sched_wqueue_start(&gfx_ring->sched);
			if (adev->sdma.has_page_queue) {
				amdgpu_fence_driver_force_completion(page_ring);
				drm_sched_wqueue_start(&page_ring->sched);
			}
		}
	}
	mutex_unlock(&sdma_instance->engine_reset_mutex);

	return ret;
}
