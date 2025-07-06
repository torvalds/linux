// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include <linux/module.h>
#include <linux/debugfs.h>
#include <drm/drm_exec.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_umsch_mm.h"
#include "umsch_mm_v4_0.h"

MODULE_FIRMWARE("amdgpu/umsch_mm_4_0_0.bin");

int amdgpu_umsch_mm_submit_pkt(struct amdgpu_umsch_mm *umsch, void *pkt, int ndws)
{
	struct amdgpu_ring *ring = &umsch->ring;

	if (amdgpu_ring_alloc(ring, ndws))
		return -ENOMEM;

	amdgpu_ring_write_multiple(ring, pkt, ndws);
	amdgpu_ring_commit(ring);

	return 0;
}

int amdgpu_umsch_mm_query_fence(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_ring *ring = &umsch->ring;
	struct amdgpu_device *adev = ring->adev;
	int r;

	r = amdgpu_fence_wait_polling(ring, ring->fence_drv.sync_seq, adev->usec_timeout);
	if (r < 1) {
		dev_err(adev->dev, "ring umsch timeout, emitted fence %u\n",
			ring->fence_drv.sync_seq);
		return -ETIMEDOUT;
	}

	return 0;
}

static void umsch_mm_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		WDOORBELL32(ring->doorbell_index, ring->wptr << 2);
	else
		WREG32(umsch->rb_wptr, ring->wptr << 2);
}

static u64 umsch_mm_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	return RREG32(umsch->rb_rptr);
}

static u64 umsch_mm_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	return RREG32(umsch->rb_wptr);
}

static const struct amdgpu_ring_funcs umsch_v4_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_UMSCH_MM,
	.align_mask = 0,
	.nop = 0,
	.support_64bit_ptrs = false,
	.get_rptr = umsch_mm_ring_get_rptr,
	.get_wptr = umsch_mm_ring_get_wptr,
	.set_wptr = umsch_mm_ring_set_wptr,
	.insert_nop = amdgpu_ring_insert_nop,
};

int amdgpu_umsch_mm_ring_init(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = container_of(umsch, struct amdgpu_device, umsch_mm);
	struct amdgpu_ring *ring = &umsch->ring;

	ring->vm_hub = AMDGPU_MMHUB0(0);
	ring->use_doorbell = true;
	ring->no_scheduler = true;
	ring->doorbell_index = (AMDGPU_NAVI10_DOORBELL64_VCN0_1 << 1) + 6;

	snprintf(ring->name, sizeof(ring->name), "umsch");

	return amdgpu_ring_init(adev, ring, 1024, NULL, 0, AMDGPU_RING_PRIO_DEFAULT, NULL);
}

int amdgpu_umsch_mm_init_microcode(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const char *fw_name = NULL;
	int r;

	switch (amdgpu_ip_version(adev, VCN_HWIP, 0)) {
	case IP_VERSION(4, 0, 5):
	case IP_VERSION(4, 0, 6):
		fw_name = "4_0_0";
		break;
	default:
		return -EINVAL;
	}

	r = amdgpu_ucode_request(adev, &adev->umsch_mm.fw, AMDGPU_UCODE_REQUIRED,
				 "amdgpu/umsch_mm_%s.bin", fw_name);
	if (r) {
		release_firmware(adev->umsch_mm.fw);
		adev->umsch_mm.fw = NULL;
		return r;
	}

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)adev->umsch_mm.fw->data;

	adev->umsch_mm.ucode_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes);
	adev->umsch_mm.data_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes);

	adev->umsch_mm.irq_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_irq_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_irq_start_addr_hi)) << 32);
	adev->umsch_mm.uc_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_uc_start_addr_hi)) << 32);
	adev->umsch_mm.data_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_data_start_addr_hi)) << 32);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		struct amdgpu_firmware_info *info;

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_UMSCH_MM_UCODE];
		info->ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_UCODE;
		info->fw = adev->umsch_mm.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_UMSCH_MM_DATA];
		info->ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_DATA;
		info->fw = adev->umsch_mm.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes), PAGE_SIZE);
	}

	return 0;
}

int amdgpu_umsch_mm_allocate_ucode_buffer(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const __le32 *fw_data;
	uint32_t fw_size;
	int r;

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)
		       adev->umsch_mm.fw->data;

	fw_data = (const __le32 *)(adev->umsch_mm.fw->data +
		  le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_offset_bytes));
	fw_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      4 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->umsch_mm.ucode_fw_obj,
				      &adev->umsch_mm.ucode_fw_gpu_addr,
				      (void **)&adev->umsch_mm.ucode_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create umsch_mm fw ucode bo\n", r);
		return r;
	}

	memcpy(adev->umsch_mm.ucode_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->umsch_mm.ucode_fw_obj);
	amdgpu_bo_unreserve(adev->umsch_mm.ucode_fw_obj);
	return 0;
}

int amdgpu_umsch_mm_allocate_ucode_data_buffer(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const __le32 *fw_data;
	uint32_t fw_size;
	int r;

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)
		       adev->umsch_mm.fw->data;

	fw_data = (const __le32 *)(adev->umsch_mm.fw->data +
		  le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_offset_bytes));
	fw_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      64 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->umsch_mm.data_fw_obj,
				      &adev->umsch_mm.data_fw_gpu_addr,
				      (void **)&adev->umsch_mm.data_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create umsch_mm fw data bo\n", r);
		return r;
	}

	memcpy(adev->umsch_mm.data_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->umsch_mm.data_fw_obj);
	amdgpu_bo_unreserve(adev->umsch_mm.data_fw_obj);
	return 0;
}

int amdgpu_umsch_mm_psp_execute_cmd_buf(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = umsch->ring.adev;
	struct amdgpu_firmware_info ucode = {
		.ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_CMD_BUFFER,
		.mc_addr = adev->umsch_mm.cmd_buf_gpu_addr,
		.ucode_size = ((uintptr_t)adev->umsch_mm.cmd_buf_curr_ptr -
			      (uintptr_t)adev->umsch_mm.cmd_buf_ptr),
	};

	return psp_execute_ip_fw_load(&adev->psp, &ucode);
}

static void umsch_mm_agdb_index_init(struct amdgpu_device *adev)
{
	uint32_t umsch_mm_agdb_start;
	int i;

	umsch_mm_agdb_start = adev->doorbell_index.max_assignment + 1;
	umsch_mm_agdb_start = roundup(umsch_mm_agdb_start, 1024);
	umsch_mm_agdb_start += (AMDGPU_NAVI10_DOORBELL64_VCN0_1 << 1);

	for (i = 0; i < CONTEXT_PRIORITY_NUM_LEVELS; i++)
		adev->umsch_mm.agdb_index[i] = umsch_mm_agdb_start + i;
}

static int umsch_mm_init(struct amdgpu_device *adev)
{
	int r;

	adev->umsch_mm.vmid_mask_mm_vpe = 0xf00;
	adev->umsch_mm.engine_mask = (1 << UMSCH_SWIP_ENGINE_TYPE_VPE);
	adev->umsch_mm.vpe_hqd_mask = 0xfe;

	r = amdgpu_device_wb_get(adev, &adev->umsch_mm.wb_index);
	if (r) {
		dev_err(adev->dev, "failed to alloc wb for umsch: %d\n", r);
		return r;
	}

	adev->umsch_mm.sch_ctx_gpu_addr = adev->wb.gpu_addr +
					  (adev->umsch_mm.wb_index * 4);

	r = amdgpu_bo_create_kernel(adev, PAGE_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &adev->umsch_mm.cmd_buf_obj,
				    &adev->umsch_mm.cmd_buf_gpu_addr,
				    (void **)&adev->umsch_mm.cmd_buf_ptr);
	if (r) {
		dev_err(adev->dev, "failed to allocate cmdbuf bo %d\n", r);
		amdgpu_device_wb_free(adev, adev->umsch_mm.wb_index);
		return r;
	}

	r = amdgpu_bo_create_kernel(adev, AMDGPU_UMSCHFW_LOG_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT,
				    &adev->umsch_mm.dbglog_bo,
				    &adev->umsch_mm.log_gpu_addr,
				    &adev->umsch_mm.log_cpu_addr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate umsch debug bo\n", r);
		return r;
	}

	mutex_init(&adev->umsch_mm.mutex_hidden);

	umsch_mm_agdb_index_init(adev);

	return 0;
}


static int umsch_mm_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	switch (amdgpu_ip_version(adev, VCN_HWIP, 0)) {
	case IP_VERSION(4, 0, 5):
	case IP_VERSION(4, 0, 6):
		umsch_mm_v4_0_set_funcs(&adev->umsch_mm);
		break;
	default:
		return -EINVAL;
	}

	adev->umsch_mm.ring.funcs = &umsch_v4_0_ring_funcs;
	umsch_mm_set_regs(&adev->umsch_mm);

	return 0;
}

static int umsch_mm_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_in_reset(adev) || adev->in_s0ix || adev->in_suspend)
		return 0;

	return 0;
}

static int umsch_mm_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = umsch_mm_init(adev);
	if (r)
		return r;

	amdgpu_umsch_fwlog_init(&adev->umsch_mm);
	r = umsch_mm_ring_init(&adev->umsch_mm);
	if (r)
		return r;

	r = umsch_mm_init_microcode(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int umsch_mm_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	release_firmware(adev->umsch_mm.fw);
	adev->umsch_mm.fw = NULL;

	amdgpu_ring_fini(&adev->umsch_mm.ring);

	mutex_destroy(&adev->umsch_mm.mutex_hidden);

	amdgpu_bo_free_kernel(&adev->umsch_mm.cmd_buf_obj,
			      &adev->umsch_mm.cmd_buf_gpu_addr,
			      (void **)&adev->umsch_mm.cmd_buf_ptr);

	amdgpu_bo_free_kernel(&adev->umsch_mm.dbglog_bo,
				    &adev->umsch_mm.log_gpu_addr,
				    (void **)&adev->umsch_mm.log_cpu_addr);

	amdgpu_device_wb_free(adev, adev->umsch_mm.wb_index);

	return 0;
}

static int umsch_mm_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = umsch_mm_load_microcode(&adev->umsch_mm);
	if (r)
		return r;

	umsch_mm_ring_start(&adev->umsch_mm);

	r = umsch_mm_set_hw_resources(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int umsch_mm_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	umsch_mm_ring_stop(&adev->umsch_mm);

	amdgpu_bo_free_kernel(&adev->umsch_mm.data_fw_obj,
			      &adev->umsch_mm.data_fw_gpu_addr,
			      (void **)&adev->umsch_mm.data_fw_ptr);

	amdgpu_bo_free_kernel(&adev->umsch_mm.ucode_fw_obj,
			      &adev->umsch_mm.ucode_fw_gpu_addr,
			      (void **)&adev->umsch_mm.ucode_fw_ptr);
	return 0;
}

static int umsch_mm_suspend(struct amdgpu_ip_block *ip_block)
{
	return umsch_mm_hw_fini(ip_block);
}

static int umsch_mm_resume(struct amdgpu_ip_block *ip_block)
{
	return umsch_mm_hw_init(ip_block);
}

void amdgpu_umsch_fwlog_init(struct amdgpu_umsch_mm *umsch_mm)
{
#if defined(CONFIG_DEBUG_FS)
	void *fw_log_cpu_addr = umsch_mm->log_cpu_addr;
	volatile struct amdgpu_umsch_fwlog *log_buf = fw_log_cpu_addr;

	log_buf->header_size = sizeof(struct amdgpu_umsch_fwlog);
	log_buf->buffer_size = AMDGPU_UMSCHFW_LOG_SIZE;
	log_buf->rptr = log_buf->header_size;
	log_buf->wptr = log_buf->header_size;
	log_buf->wrapped = 0;
#endif
}

/*
 * debugfs for mapping umsch firmware log buffer.
 */
#if defined(CONFIG_DEBUG_FS)
static ssize_t amdgpu_debugfs_umsch_fwlog_read(struct file *f, char __user *buf,
					     size_t size, loff_t *pos)
{
	struct amdgpu_umsch_mm *umsch_mm;
	void *log_buf;
	volatile struct amdgpu_umsch_fwlog *plog;
	unsigned int read_pos, write_pos, available, i, read_bytes = 0;
	unsigned int read_num[2] = {0};

	umsch_mm = file_inode(f)->i_private;
	if (!umsch_mm)
		return -ENODEV;

	if (!umsch_mm->log_cpu_addr)
		return -EFAULT;

	log_buf = umsch_mm->log_cpu_addr;

	plog = (volatile struct amdgpu_umsch_fwlog *)log_buf;
	read_pos = plog->rptr;
	write_pos = plog->wptr;

	if (read_pos > AMDGPU_UMSCHFW_LOG_SIZE || write_pos > AMDGPU_UMSCHFW_LOG_SIZE)
		return -EFAULT;

	if (!size || (read_pos == write_pos))
		return 0;

	if (write_pos > read_pos) {
		available = write_pos - read_pos;
		read_num[0] = min_t(size_t, size, available);
	} else {
		read_num[0] = AMDGPU_UMSCHFW_LOG_SIZE - read_pos;
		available = read_num[0] + write_pos - plog->header_size;
		if (size > available)
			read_num[1] = write_pos - plog->header_size;
		else if (size > read_num[0])
			read_num[1] = size - read_num[0];
		else
			read_num[0] = size;
	}

	for (i = 0; i < 2; i++) {
		if (read_num[i]) {
			if (read_pos == AMDGPU_UMSCHFW_LOG_SIZE)
				read_pos = plog->header_size;
			if (read_num[i] == copy_to_user((buf + read_bytes),
							(log_buf + read_pos), read_num[i]))
				return -EFAULT;

			read_bytes += read_num[i];
			read_pos += read_num[i];
		}
	}

	plog->rptr = read_pos;
	*pos += read_bytes;
	return read_bytes;
}

static const struct file_operations amdgpu_debugfs_umschfwlog_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_umsch_fwlog_read,
	.llseek = default_llseek
};
#endif

void amdgpu_debugfs_umsch_fwlog_init(struct amdgpu_device *adev,
			struct amdgpu_umsch_mm *umsch_mm)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	char name[32];

	sprintf(name, "amdgpu_umsch_fwlog");
	debugfs_create_file_size(name, S_IFREG | 0444, root, umsch_mm,
				 &amdgpu_debugfs_umschfwlog_fops,
				 AMDGPU_UMSCHFW_LOG_SIZE);
#endif
}

static const struct amd_ip_funcs umsch_mm_v4_0_ip_funcs = {
	.name = "umsch_mm_v4_0",
	.early_init = umsch_mm_early_init,
	.late_init = umsch_mm_late_init,
	.sw_init = umsch_mm_sw_init,
	.sw_fini = umsch_mm_sw_fini,
	.hw_init = umsch_mm_hw_init,
	.hw_fini = umsch_mm_hw_fini,
	.suspend = umsch_mm_suspend,
	.resume = umsch_mm_resume,
};

const struct amdgpu_ip_block_version umsch_mm_v4_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_UMSCH_MM,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &umsch_mm_v4_0_ip_funcs,
};
