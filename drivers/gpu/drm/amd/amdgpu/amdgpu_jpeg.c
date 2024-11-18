/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include "amdgpu.h"
#include "amdgpu_jpeg.h"
#include "amdgpu_pm.h"
#include "soc15d.h"
#include "soc15_common.h"

#define JPEG_IDLE_TIMEOUT	msecs_to_jiffies(1000)

static void amdgpu_jpeg_idle_work_handler(struct work_struct *work);

int amdgpu_jpeg_sw_init(struct amdgpu_device *adev)
{
	int i, r;

	INIT_DELAYED_WORK(&adev->jpeg.idle_work, amdgpu_jpeg_idle_work_handler);
	mutex_init(&adev->jpeg.jpeg_pg_lock);
	atomic_set(&adev->jpeg.total_submission_cnt, 0);

	if ((adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) &&
	    (adev->pg_flags & AMD_PG_SUPPORT_JPEG_DPG))
		adev->jpeg.indirect_sram = true;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++) {
		if (adev->jpeg.harvest_config & (1U << i))
			continue;

		if (adev->jpeg.indirect_sram) {
			r = amdgpu_bo_create_kernel(adev, 64 * 2 * 4, PAGE_SIZE,
					AMDGPU_GEM_DOMAIN_VRAM |
					AMDGPU_GEM_DOMAIN_GTT,
					&adev->jpeg.inst[i].dpg_sram_bo,
					&adev->jpeg.inst[i].dpg_sram_gpu_addr,
					&adev->jpeg.inst[i].dpg_sram_cpu_addr);
			if (r) {
				dev_err(adev->dev,
				"JPEG %d (%d) failed to allocate DPG bo\n", i, r);
				return r;
			}
		}
	}

	return 0;
}

int amdgpu_jpeg_sw_fini(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1U << i))
			continue;

		amdgpu_bo_free_kernel(
			&adev->jpeg.inst[i].dpg_sram_bo,
			&adev->jpeg.inst[i].dpg_sram_gpu_addr,
			(void **)&adev->jpeg.inst[i].dpg_sram_cpu_addr);

		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j)
			amdgpu_ring_fini(&adev->jpeg.inst[i].ring_dec[j]);
	}

	mutex_destroy(&adev->jpeg.jpeg_pg_lock);

	return 0;
}

int amdgpu_jpeg_suspend(struct amdgpu_device *adev)
{
	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	return 0;
}

int amdgpu_jpeg_resume(struct amdgpu_device *adev)
{
	return 0;
}

static void amdgpu_jpeg_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, jpeg.idle_work.work);
	unsigned int fences = 0;
	unsigned int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1U << i))
			continue;

		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j)
			fences += amdgpu_fence_count_emitted(&adev->jpeg.inst[i].ring_dec[j]);
	}

	if (!fences && !atomic_read(&adev->jpeg.total_submission_cnt))
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_JPEG,
						       AMD_PG_STATE_GATE);
	else
		schedule_delayed_work(&adev->jpeg.idle_work, JPEG_IDLE_TIMEOUT);
}

void amdgpu_jpeg_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	atomic_inc(&adev->jpeg.total_submission_cnt);
	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	mutex_lock(&adev->jpeg.jpeg_pg_lock);
	amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_JPEG,
						       AMD_PG_STATE_UNGATE);
	mutex_unlock(&adev->jpeg.jpeg_pg_lock);
}

void amdgpu_jpeg_ring_end_use(struct amdgpu_ring *ring)
{
	atomic_dec(&ring->adev->jpeg.total_submission_cnt);
	schedule_delayed_work(&ring->adev->jpeg.idle_work, JPEG_IDLE_TIMEOUT);
}

int amdgpu_jpeg_dec_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	/* JPEG in SRIOV does not support direct register read/write */
	if (amdgpu_sriov_vf(adev))
		return 0;

	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		return r;

	WREG32(adev->jpeg.inst[ring->me].external.jpeg_pitch[ring->pipe], 0xCAFEDEAD);
	/* Add a read register to make sure the write register is executed. */
	RREG32(adev->jpeg.inst[ring->me].external.jpeg_pitch[ring->pipe]);

	amdgpu_ring_write(ring, PACKET0(adev->jpeg.internal.jpeg_pitch[ring->pipe], 0));
	amdgpu_ring_write(ring, 0xABADCAFE);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(adev->jpeg.inst[ring->me].external.jpeg_pitch[ring->pipe]);
		if (tmp == 0xABADCAFE)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

static int amdgpu_jpeg_dec_set_reg(struct amdgpu_ring *ring, uint32_t handle,
		struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	const unsigned ib_size_dw = 16;
	int i, r;

	r = amdgpu_job_alloc_with_ib(ring->adev, NULL, NULL, ib_size_dw * 4,
				     AMDGPU_IB_POOL_DIRECT, &job);
	if (r)
		return r;

	ib = &job->ibs[0];

	ib->ptr[0] = PACKETJ(adev->jpeg.internal.jpeg_pitch[ring->pipe], 0, 0, PACKETJ_TYPE0);
	ib->ptr[1] = 0xDEADBEEF;
	for (i = 2; i < 16; i += 2) {
		ib->ptr[i] = PACKETJ(0, 0, 0, PACKETJ_TYPE6);
		ib->ptr[i+1] = 0;
	}
	ib->length_dw = 16;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

int amdgpu_jpeg_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	struct dma_fence *fence = NULL;
	long r = 0;

	r = amdgpu_jpeg_dec_set_reg(ring, 1, &fence);
	if (r)
		goto error;

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0) {
		r = -ETIMEDOUT;
		goto error;
	} else if (r < 0) {
		goto error;
	} else {
		r = 0;
	}

	if (!amdgpu_sriov_vf(adev)) {
		for (i = 0; i < adev->usec_timeout; i++) {
			tmp = RREG32(adev->jpeg.inst[ring->me].external.jpeg_pitch[ring->pipe]);
			if (tmp == 0xDEADBEEF)
				break;
			udelay(1);
			if (amdgpu_emu_mode == 1)
				udelay(10);
		}

		if (i >= adev->usec_timeout)
			r = -ETIMEDOUT;
	}

	dma_fence_put(fence);
error:
	return r;
}

int amdgpu_jpeg_process_poison_irq(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->jpeg.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;
	amdgpu_ras_interrupt_dispatch(adev, &ih_data);

	return 0;
}

int amdgpu_jpeg_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r, i;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	if (amdgpu_ras_is_supported(adev, ras_block->block)) {
		for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
			if (adev->jpeg.harvest_config & (1 << i) ||
			    !adev->jpeg.inst[i].ras_poison_irq.funcs)
				continue;

			r = amdgpu_irq_get(adev, &adev->jpeg.inst[i].ras_poison_irq, 0);
			if (r)
				goto late_fini;
		}
	}
	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
}

int amdgpu_jpeg_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_jpeg_ras *ras;

	if (!adev->jpeg.ras)
		return 0;

	ras = adev->jpeg.ras;
	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register jpeg ras block!\n");
		return err;
	}

	strcpy(ras->ras_block.ras_comm.name, "jpeg");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__JPEG;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__POISON;
	adev->jpeg.ras_if = &ras->ras_block.ras_comm;

	if (!ras->ras_block.ras_late_init)
		ras->ras_block.ras_late_init = amdgpu_jpeg_ras_late_init;

	return 0;
}

int amdgpu_jpeg_psp_update_sram(struct amdgpu_device *adev, int inst_idx,
			       enum AMDGPU_UCODE_ID ucode_id)
{
	struct amdgpu_firmware_info ucode = {
		.ucode_id = AMDGPU_UCODE_ID_JPEG_RAM,
		.mc_addr = adev->jpeg.inst[inst_idx].dpg_sram_gpu_addr,
		.ucode_size = ((uintptr_t)adev->jpeg.inst[inst_idx].dpg_sram_curr_addr -
			      (uintptr_t)adev->jpeg.inst[inst_idx].dpg_sram_cpu_addr),
	};

	return psp_execute_ip_fw_load(&adev->psp, &ucode);
}

/*
 * debugfs for to enable/disable jpeg job submission to specific core.
 */
#if defined(CONFIG_DEBUG_FS)
static int amdgpu_debugfs_jpeg_sched_mask_set(void *data, u64 val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i, j;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;

	mask = (1ULL << (adev->jpeg.num_jpeg_inst * adev->jpeg.num_jpeg_rings)) - 1;
	if ((val & mask) == 0)
		return -EINVAL;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			if (val & (1 << ((i * adev->jpeg.num_jpeg_rings) + j)))
				ring->sched.ready = true;
			else
				ring->sched.ready = false;
		}
	}
	/* publish sched.ready flag update effective immediately across smp */
	smp_rmb();
	return 0;
}

static int amdgpu_debugfs_jpeg_sched_mask_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	u32 i, j;
	u64 mask = 0;
	struct amdgpu_ring *ring;

	if (!adev)
		return -ENODEV;
	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			if (ring->sched.ready)
				mask |= 1ULL << ((i * adev->jpeg.num_jpeg_rings) + j);
		}
	}
	*val = mask;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_debugfs_jpeg_sched_mask_fops,
			 amdgpu_debugfs_jpeg_sched_mask_get,
			 amdgpu_debugfs_jpeg_sched_mask_set, "%llx\n");

#endif

void amdgpu_debugfs_jpeg_sched_mask_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	if (!(adev->jpeg.num_jpeg_inst > 1) && !(adev->jpeg.num_jpeg_rings > 1))
		return;
	sprintf(name, "amdgpu_jpeg_sched_mask");
	debugfs_create_file(name, 0600, root, adev,
			    &amdgpu_debugfs_jpeg_sched_mask_fops);
#endif
}

static ssize_t amdgpu_get_jpeg_reset_mask(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	if (!adev)
		return -ENODEV;

	return amdgpu_show_reset_mask(buf, adev->jpeg.supported_reset);
}

static DEVICE_ATTR(jpeg_reset_mask, 0444,
		   amdgpu_get_jpeg_reset_mask, NULL);

int amdgpu_jpeg_sysfs_reset_mask_init(struct amdgpu_device *adev)
{
	int r = 0;

	if (adev->jpeg.num_jpeg_inst) {
		r = device_create_file(adev->dev, &dev_attr_jpeg_reset_mask);
		if (r)
			return r;
	}

	return r;
}

void amdgpu_jpeg_sysfs_reset_mask_fini(struct amdgpu_device *adev)
{
	if (adev->jpeg.num_jpeg_inst)
		device_remove_file(adev->dev, &dev_attr_jpeg_reset_mask);
}
