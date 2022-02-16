// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Implementation of the dummy job execution workaround for the GPU hang issue.
 */

#include <mali_kbase.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_dummy_job_wa.h>

#include <linux/firmware.h>
#include <linux/delay.h>

#define DUMMY_JOB_WA_BINARY_NAME "valhall-1691526.wa"

struct wa_header {
	u16 signature;
	u16 version;
	u32 info_offset;
} __packed;

struct wa_v2_info {
	u64 jc;
	u32 js;
	u32 blob_offset;
	u64 flags;
} __packed;

struct wa_blob {
	u64 base;
	u32 size;
	u32 map_flags;
	u32 payload_offset;
	u32 blob_offset;
} __packed;

static bool in_range(const u8 *base, const u8 *end, off_t off, size_t sz)
{
	return !(end - base - off < sz);
}

static u32 wait_any(struct kbase_device *kbdev, off_t offset, u32 bits)
{
	int loop;
	const int timeout = 100;
	u32 val;

	for (loop = 0; loop < timeout; loop++) {
		val = kbase_reg_read(kbdev, offset);
		if (val & bits)
			break;
		udelay(10);
	}

	if (loop == timeout) {
		dev_err(kbdev->dev,
			"Timeout reading register 0x%lx, bits 0x%lx, last read was 0x%lx\n",
			(unsigned long)offset, (unsigned long)bits,
			(unsigned long)val);
	}

	return (val & bits);
}

static int wait(struct kbase_device *kbdev, off_t offset, u32 bits, bool set)
{
	int loop;
	const int timeout = 100;
	u32 val;
	u32 target = 0;

	if (set)
		target = bits;

	for (loop = 0; loop < timeout; loop++) {
		val = kbase_reg_read(kbdev, (offset));
		if ((val & bits) == target)
			break;

		udelay(10);
	}

	if (loop == timeout) {
		dev_err(kbdev->dev,
			"Timeout reading register 0x%lx, bits 0x%lx, last read was 0x%lx\n",
			(unsigned long)offset, (unsigned long)bits,
			(unsigned long)val);
		return -ETIMEDOUT;
	}

	return 0;
}

static inline int run_job(struct kbase_device *kbdev, int as, int slot,
			  u64 cores, u64 jc)
{
	u32 done;

	/* setup job */
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_HEAD_NEXT_LO),
			jc & U32_MAX);
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_HEAD_NEXT_HI),
			jc >> 32);
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_AFFINITY_NEXT_LO),
			cores & U32_MAX);
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_AFFINITY_NEXT_HI),
			cores >> 32);
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_CONFIG_NEXT),
			JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK | as);

	/* go */
	kbase_reg_write(kbdev, JOB_SLOT_REG(slot, JS_COMMAND_NEXT),
			JS_COMMAND_START);

	/* wait for the slot to finish (done, error) */
	done = wait_any(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT),
			(1ul << (16+slot)) | (1ul << slot));
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), done);

	if (done != (1ul << slot)) {
		dev_err(kbdev->dev,
			"Failed to run WA job on slot %d cores 0x%llx: done 0x%lx\n",
			slot, (unsigned long long)cores,
			(unsigned long)done);
		dev_err(kbdev->dev, "JS_STATUS on failure: 0x%x\n",
			kbase_reg_read(kbdev, JOB_SLOT_REG(slot, JS_STATUS)));

		return -EFAULT;
	} else {
		return 0;
	}
}

/* To be called after power up & MMU init, but before everything else */
int kbase_dummy_job_wa_execute(struct kbase_device *kbdev, u64 cores)
{
	int as;
	int slot;
	u64 jc;
	int failed = 0;
	int runs = 0;
	u32 old_gpu_mask;
	u32 old_job_mask;

	if (!kbdev)
		return -EFAULT;

	if (!kbdev->dummy_job_wa.ctx)
		return -EFAULT;

	as = kbdev->dummy_job_wa.ctx->as_nr;
	slot = kbdev->dummy_job_wa.slot;
	jc = kbdev->dummy_job_wa.jc;

	/* mask off all but MMU IRQs */
	old_gpu_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
	old_job_mask = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK));
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), 0);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), 0);

	/* power up requested cores */
	kbase_reg_write(kbdev, SHADER_PWRON_LO, (cores & U32_MAX));
	kbase_reg_write(kbdev, SHADER_PWRON_HI, (cores >> 32));

	if (kbdev->dummy_job_wa.flags & KBASE_DUMMY_JOB_WA_FLAG_WAIT_POWERUP) {
		/* wait for power-ups */
		wait(kbdev, SHADER_READY_LO, (cores & U32_MAX), true);
		if (cores >> 32)
			wait(kbdev, SHADER_READY_HI, (cores >> 32), true);
	}

	if (kbdev->dummy_job_wa.flags & KBASE_DUMMY_JOB_WA_FLAG_SERIALIZE) {
		int i;

		/* do for each requested core */
		for (i = 0; i < sizeof(cores) * 8; i++) {
			u64 affinity;

			affinity = 1ull << i;

			if (!(cores & affinity))
				continue;

			if (run_job(kbdev, as, slot, affinity, jc))
				failed++;
			runs++;
		}

	} else {
		if (run_job(kbdev, as, slot, cores, jc))
			failed++;
		runs++;
	}

	if (kbdev->dummy_job_wa.flags &
			KBASE_DUMMY_JOB_WA_FLAG_LOGICAL_SHADER_POWER) {
		/* power off shader cores (to reduce any dynamic leakage) */
		kbase_reg_write(kbdev, SHADER_PWROFF_LO, (cores & U32_MAX));
		kbase_reg_write(kbdev, SHADER_PWROFF_HI, (cores >> 32));

		/* wait for power off complete */
		wait(kbdev, SHADER_READY_LO, (cores & U32_MAX), false);
		wait(kbdev, SHADER_PWRTRANS_LO, (cores & U32_MAX), false);
		if (cores >> 32) {
			wait(kbdev, SHADER_READY_HI, (cores >> 32), false);
			wait(kbdev, SHADER_PWRTRANS_HI, (cores >> 32), false);
		}
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), U32_MAX);
	}

	/* restore IRQ masks */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), old_gpu_mask);
	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), old_job_mask);

	if (failed)
		dev_err(kbdev->dev,
			"WA complete with %d failures out of %d runs\n", failed,
			runs);

	return failed ? -EFAULT : 0;
}

static ssize_t dummy_job_wa_info_show(struct device * const dev,
		struct device_attribute * const attr, char * const buf)
{
	struct kbase_device *const kbdev = dev_get_drvdata(dev);
	int err;

	if (!kbdev || !kbdev->dummy_job_wa.ctx)
		return -ENODEV;

	err = scnprintf(buf, PAGE_SIZE, "slot %u flags %llx\n",
			kbdev->dummy_job_wa.slot, kbdev->dummy_job_wa.flags);

	return err;
}

static DEVICE_ATTR_RO(dummy_job_wa_info);

static bool wa_blob_load_needed(struct kbase_device *kbdev)
{
	if (of_machine_is_compatible("arm,juno"))
		return false;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_3485))
		return true;

	return false;
}

int kbase_dummy_job_wa_load(struct kbase_device *kbdev)
{
	const struct firmware *firmware;
	static const char wa_name[] = DUMMY_JOB_WA_BINARY_NAME;
	const u32 signature = 0x4157;
	const u32 version = 2;
	const u8 *fw_end;
	const u8 *fw;
	const struct wa_header *header;
	const struct wa_v2_info *v2_info;
	u32 blob_offset;
	int err;
	struct kbase_context *kctx;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	lockdep_assert_held(&kbdev->fw_load_lock);

	if (!wa_blob_load_needed(kbdev))
		return 0;

	/* load the wa */
	err = request_firmware(&firmware, wa_name, kbdev->dev);

	if (err) {
		dev_err(kbdev->dev, "WA blob missing. Please refer to the Arm Mali DDK Valhall Release Notes, "
				    "Part number DC-06002 or contact support-mali@arm.com - driver probe will be failed");
		return -ENODEV;
	}

	kctx = kbase_create_context(kbdev, true,
				    BASE_CONTEXT_CREATE_FLAG_NONE, 0,
				    NULL);

	if (!kctx) {
		dev_err(kbdev->dev, "Failed to create WA context\n");
		goto no_ctx;
	}

	fw = firmware->data;
	fw_end = fw + firmware->size;

	dev_dbg(kbdev->dev, "Loaded firmware of size %zu bytes\n",
		firmware->size);

	if (!in_range(fw, fw_end, 0, sizeof(*header))) {
		dev_err(kbdev->dev, "WA too small\n");
		goto bad_fw;
	}

	header = (const struct wa_header *)(fw + 0);

	if (header->signature != signature) {
		dev_err(kbdev->dev, "WA signature failure: 0x%lx\n",
			(unsigned long)header->signature);
		goto bad_fw;
	}

	if (header->version != version) {
		dev_err(kbdev->dev, "WA version 0x%lx not supported\n",
			(unsigned long)header->version);
		goto bad_fw;
	}

	if (!in_range(fw, fw_end, header->info_offset, sizeof(*v2_info))) {
		dev_err(kbdev->dev, "WA info offset out of bounds\n");
		goto bad_fw;
	}

	v2_info = (const struct wa_v2_info *)(fw + header->info_offset);

	if (v2_info->flags & ~KBASE_DUMMY_JOB_WA_FLAGS) {
		dev_err(kbdev->dev, "Unsupported WA flag(s): 0x%llx\n",
			(unsigned long long)v2_info->flags);
		goto bad_fw;
	}

	kbdev->dummy_job_wa.slot = v2_info->js;
	kbdev->dummy_job_wa.jc = v2_info->jc;
	kbdev->dummy_job_wa.flags = v2_info->flags;

	blob_offset = v2_info->blob_offset;

	while (blob_offset) {
		const struct wa_blob *blob;
		size_t nr_pages;
		u64 flags;
		u64 gpu_va;
		struct kbase_va_region *va_region;

		if (!in_range(fw, fw_end, blob_offset, sizeof(*blob))) {
			dev_err(kbdev->dev, "Blob offset out-of-range: 0x%lx\n",
				(unsigned long)blob_offset);
			goto bad_fw;
		}

		blob = (const struct wa_blob *)(fw + blob_offset);
		if (!in_range(fw, fw_end, blob->payload_offset, blob->size)) {
			dev_err(kbdev->dev, "Payload out-of-bounds\n");
			goto bad_fw;
		}

		gpu_va = blob->base;
		if (PAGE_ALIGN(gpu_va) != gpu_va) {
			dev_err(kbdev->dev, "blob not page aligned\n");
			goto bad_fw;
		}
		nr_pages = PFN_UP(blob->size);
		flags = blob->map_flags | BASE_MEM_FLAG_MAP_FIXED;

		va_region = kbase_mem_alloc(kctx, nr_pages, nr_pages, 0, &flags,
					    &gpu_va, mmu_sync_info);

		if (!va_region) {
			dev_err(kbdev->dev, "Failed to allocate for blob\n");
		} else {
			struct kbase_vmap_struct vmap = { 0 };
			const u8 *payload;
			void *dst;

			/* copy the payload,  */
			payload = fw + blob->payload_offset;

			dst = kbase_vmap(kctx,
					 va_region->start_pfn << PAGE_SHIFT,
					 nr_pages << PAGE_SHIFT, &vmap);

			if (dst) {
				memcpy(dst, payload, blob->size);
				kbase_vunmap(kctx, &vmap);
			} else {
				dev_err(kbdev->dev,
					"Failed to copy payload\n");
			}

		}
		blob_offset = blob->blob_offset; /* follow chain */
	}

	release_firmware(firmware);

	kbasep_js_schedule_privileged_ctx(kbdev, kctx);

	kbdev->dummy_job_wa.ctx = kctx;

	err = sysfs_create_file(&kbdev->dev->kobj,
				&dev_attr_dummy_job_wa_info.attr);
	if (err)
		dev_err(kbdev->dev, "SysFS file creation for dummy job wa failed\n");

	return 0;

bad_fw:
	kbase_destroy_context(kctx);
no_ctx:
	release_firmware(firmware);
	return -EFAULT;
}

void kbase_dummy_job_wa_cleanup(struct kbase_device *kbdev)
{
	struct kbase_context *wa_ctx;

	/* return if the dummy job has not been loaded */
	if (kbdev->dummy_job_wa_loaded == false)
		return;

	/* Can be safely called even if the file wasn't created on probe */
	sysfs_remove_file(&kbdev->dev->kobj, &dev_attr_dummy_job_wa_info.attr);

	wa_ctx = READ_ONCE(kbdev->dummy_job_wa.ctx);
	WRITE_ONCE(kbdev->dummy_job_wa.ctx, NULL);
	/* make this write visible before we tear down the ctx */
	smp_mb();

	if (wa_ctx) {
		kbasep_js_release_privileged_ctx(kbdev, wa_ctx);
		kbase_destroy_context(wa_ctx);
	}
}
