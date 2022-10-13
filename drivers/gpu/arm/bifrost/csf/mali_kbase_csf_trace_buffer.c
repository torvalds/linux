// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2022 ARM Limited. All rights reserved.
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

#include "mali_kbase.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_trace_buffer.h"
#include "mali_kbase_reset_gpu.h"
#include "mali_kbase_csf_tl_reader.h"

#include <linux/list.h>
#include <linux/mman.h>

/**
 * struct firmware_trace_buffer - Trace Buffer within the MCU firmware
 *
 * @kbdev:        Pointer to the Kbase device.
 * @node:         List head linking all trace buffers to
 *                kbase_device:csf.firmware_trace_buffers
 * @data_mapping: MCU shared memory mapping used for the data buffer.
 * @updatable:    Indicates whether config items can be updated with
 *                FIRMWARE_CONFIG_UPDATE
 * @type:         The type of the trace buffer.
 * @trace_enable_entry_count: Number of Trace Enable bits.
 * @gpu_va:                 Structure containing all the Firmware addresses
 *                          that are accessed by the MCU.
 * @gpu_va.size_address:    The address where the MCU shall read the size of
 *                          the data buffer.
 * @gpu_va.insert_address:  The address that shall be dereferenced by the MCU
 *                          to write the Insert offset.
 * @gpu_va.extract_address: The address that shall be dereferenced by the MCU
 *                          to read the Extract offset.
 * @gpu_va.data_address:    The address that shall be dereferenced by the MCU
 *                          to write the Trace Buffer.
 * @gpu_va.trace_enable:    The address where the MCU shall read the array of
 *                          Trace Enable bits describing which trace points
 *                          and features shall be enabled.
 * @cpu_va:                 Structure containing CPU addresses of variables
 *                          which are permanently mapped on the CPU address
 *                          space.
 * @cpu_va.insert_cpu_va:   CPU virtual address of the Insert variable.
 * @cpu_va.extract_cpu_va:  CPU virtual address of the Extract variable.
 * @num_pages: Size of the data buffer, in pages.
 * @trace_enable_init_mask: Initial value for the trace enable bit mask.
 * @name:  NULL terminated string which contains the name of the trace buffer.
 *
 * The firmware relays information to the host by writing on memory buffers
 * which are allocated and partially configured by the host. These buffers
 * are called Trace Buffers: each of them has a specific purpose and is
 * identified by a name and a set of memory addresses where the host can
 * set pointers to host-allocated structures.
 */
struct firmware_trace_buffer {
	struct kbase_device *kbdev;
	struct list_head node;
	struct kbase_csf_mapping data_mapping;
	bool updatable;
	u32 type;
	u32 trace_enable_entry_count;
	struct gpu_va {
		u32 size_address;
		u32 insert_address;
		u32 extract_address;
		u32 data_address;
		u32 trace_enable;
	} gpu_va;
	struct cpu_va {
		u32 *insert_cpu_va;
		u32 *extract_cpu_va;
	} cpu_va;
	u32 num_pages;
	u32 trace_enable_init_mask[CSF_FIRMWARE_TRACE_ENABLE_INIT_MASK_MAX];
	char name[1]; /* this field must be last */
};

/**
 * struct firmware_trace_buffer_data - Configuration data for trace buffers
 *
 * @name: Name identifier of the trace buffer
 * @trace_enable_init_mask: Initial value to assign to the trace enable bits
 * @size: Size of the data buffer to allocate for the trace buffer, in pages.
 *        The size of a data buffer must always be a power of 2.
 *
 * Describe how to set up a trace buffer interface.
 * Trace buffers are identified by name and they require a data buffer and
 * an initial mask of values for the trace enable bits.
 */
struct firmware_trace_buffer_data {
	char name[64];
	u32 trace_enable_init_mask[CSF_FIRMWARE_TRACE_ENABLE_INIT_MASK_MAX];
	size_t size;
};

/*
 * Table of configuration data for trace buffers.
 *
 * This table contains the configuration data for the trace buffers that are
 * expected to be parsed from the firmware.
 */
static const struct firmware_trace_buffer_data trace_buffer_data[] = {
#if MALI_UNIT_TEST
	{ "fwutf", { 0 }, 1 },
#endif
	{ FIRMWARE_LOG_BUF_NAME, { 0 }, 4 },
	{ "benchmark", { 0 }, 2 },
	{ "timeline", { 0 }, KBASE_CSF_TL_BUFFER_NR_PAGES },
};

int kbase_csf_firmware_trace_buffers_init(struct kbase_device *kbdev)
{
	struct firmware_trace_buffer *trace_buffer;
	int ret = 0;
	u32 mcu_rw_offset = 0, mcu_write_offset = 0;
	const u32 cache_line_alignment = kbase_get_cache_line_alignment(kbdev);

	if (list_empty(&kbdev->csf.firmware_trace_buffers.list)) {
		dev_dbg(kbdev->dev, "No trace buffers to initialise\n");
		return 0;
	}

	/* GPU-readable,writable memory used for Extract variables */
	ret = kbase_csf_firmware_mcu_shared_mapping_init(
			kbdev, 1, PROT_WRITE,
			KBASE_REG_GPU_RD | KBASE_REG_GPU_WR,
			&kbdev->csf.firmware_trace_buffers.mcu_rw);
	if (ret != 0) {
		dev_err(kbdev->dev, "Failed to map GPU-rw MCU shared memory\n");
		goto out;
	}

	/* GPU-writable memory used for Insert variables */
	ret = kbase_csf_firmware_mcu_shared_mapping_init(
			kbdev, 1, PROT_READ, KBASE_REG_GPU_WR,
			&kbdev->csf.firmware_trace_buffers.mcu_write);
	if (ret != 0) {
		dev_err(kbdev->dev, "Failed to map GPU-writable MCU shared memory\n");
		goto out;
	}

	list_for_each_entry(trace_buffer, &kbdev->csf.firmware_trace_buffers.list, node) {
		u32 extract_gpu_va, insert_gpu_va, data_buffer_gpu_va,
			trace_enable_size_dwords;
		u32 *extract_cpu_va, *insert_cpu_va;
		unsigned int i;

		/* GPU-writable data buffer for the individual trace buffer */
		ret = kbase_csf_firmware_mcu_shared_mapping_init(
				kbdev, trace_buffer->num_pages, PROT_READ, KBASE_REG_GPU_WR,
				&trace_buffer->data_mapping);
		if (ret) {
			dev_err(kbdev->dev, "Failed to map GPU-writable MCU shared memory for a trace buffer\n");
			goto out;
		}

		extract_gpu_va =
			(kbdev->csf.firmware_trace_buffers.mcu_rw.va_reg->start_pfn << PAGE_SHIFT) +
			mcu_rw_offset;
		extract_cpu_va = (u32 *)(
			kbdev->csf.firmware_trace_buffers.mcu_rw.cpu_addr +
			mcu_rw_offset);
		insert_gpu_va =
			(kbdev->csf.firmware_trace_buffers.mcu_write.va_reg->start_pfn << PAGE_SHIFT) +
			mcu_write_offset;
		insert_cpu_va = (u32 *)(
			kbdev->csf.firmware_trace_buffers.mcu_write.cpu_addr +
			mcu_write_offset);
		data_buffer_gpu_va =
			(trace_buffer->data_mapping.va_reg->start_pfn << PAGE_SHIFT);

		/* Initialize the Extract variable */
		*extract_cpu_va = 0;

		/* Each FW address shall be mapped and set individually, as we can't
		 * assume anything about their location in the memory address space.
		 */
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.data_address, data_buffer_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.insert_address, insert_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.extract_address, extract_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.size_address,
				trace_buffer->num_pages << PAGE_SHIFT);

		trace_enable_size_dwords =
				(trace_buffer->trace_enable_entry_count + 31) >> 5;

		for (i = 0; i < trace_enable_size_dwords; i++) {
			kbase_csf_update_firmware_memory(
					kbdev, trace_buffer->gpu_va.trace_enable + i*4,
					trace_buffer->trace_enable_init_mask[i]);
		}

		/* Store CPU virtual addresses for permanently mapped variables */
		trace_buffer->cpu_va.insert_cpu_va = insert_cpu_va;
		trace_buffer->cpu_va.extract_cpu_va = extract_cpu_va;

		/* Update offsets */
		mcu_write_offset += cache_line_alignment;
		mcu_rw_offset += cache_line_alignment;
	}

out:
	return ret;
}

void kbase_csf_firmware_trace_buffers_term(struct kbase_device *kbdev)
{
	if (list_empty(&kbdev->csf.firmware_trace_buffers.list))
		return;

	while (!list_empty(&kbdev->csf.firmware_trace_buffers.list)) {
		struct firmware_trace_buffer *trace_buffer;

		trace_buffer = list_first_entry(&kbdev->csf.firmware_trace_buffers.list,
				struct firmware_trace_buffer, node);
		kbase_csf_firmware_mcu_shared_mapping_term(kbdev, &trace_buffer->data_mapping);
		list_del(&trace_buffer->node);

		kfree(trace_buffer);
	}

	kbase_csf_firmware_mcu_shared_mapping_term(
			kbdev, &kbdev->csf.firmware_trace_buffers.mcu_rw);
	kbase_csf_firmware_mcu_shared_mapping_term(
			kbdev, &kbdev->csf.firmware_trace_buffers.mcu_write);
}

int kbase_csf_firmware_parse_trace_buffer_entry(struct kbase_device *kbdev,
						const u32 *entry,
						unsigned int size,
						bool updatable)
{
	const char *name = (char *)&entry[7];
	const unsigned int name_len = size - TRACE_BUFFER_ENTRY_NAME_OFFSET;
	struct firmware_trace_buffer *trace_buffer;
	unsigned int i;

	/* Allocate enough space for struct firmware_trace_buffer and the
	 * trace buffer name (with NULL termination).
	 */
	trace_buffer =
		kmalloc(sizeof(*trace_buffer) + name_len + 1, GFP_KERNEL);

	if (!trace_buffer)
		return -ENOMEM;

	memcpy(&trace_buffer->name, name, name_len);
	trace_buffer->name[name_len] = '\0';

	for (i = 0; i < ARRAY_SIZE(trace_buffer_data); i++) {
		if (!strcmp(trace_buffer_data[i].name, trace_buffer->name)) {
			unsigned int j;

			trace_buffer->kbdev = kbdev;
			trace_buffer->updatable = updatable;
			trace_buffer->type = entry[0];
			trace_buffer->gpu_va.size_address = entry[1];
			trace_buffer->gpu_va.insert_address = entry[2];
			trace_buffer->gpu_va.extract_address = entry[3];
			trace_buffer->gpu_va.data_address = entry[4];
			trace_buffer->gpu_va.trace_enable = entry[5];
			trace_buffer->trace_enable_entry_count = entry[6];
			trace_buffer->num_pages = trace_buffer_data[i].size;

			for (j = 0; j < CSF_FIRMWARE_TRACE_ENABLE_INIT_MASK_MAX; j++) {
				trace_buffer->trace_enable_init_mask[j] =
					trace_buffer_data[i].trace_enable_init_mask[j];
			}
			break;
		}
	}

	if (i < ARRAY_SIZE(trace_buffer_data)) {
		list_add(&trace_buffer->node, &kbdev->csf.firmware_trace_buffers.list);
		dev_dbg(kbdev->dev, "Trace buffer '%s'", trace_buffer->name);
	} else {
		dev_dbg(kbdev->dev, "Unknown trace buffer '%s'", trace_buffer->name);
		kfree(trace_buffer);
	}

	return 0;
}

void kbase_csf_firmware_reload_trace_buffers_data(struct kbase_device *kbdev)
{
	struct firmware_trace_buffer *trace_buffer;
	u32 mcu_rw_offset = 0, mcu_write_offset = 0;
	const u32 cache_line_alignment = kbase_get_cache_line_alignment(kbdev);

	list_for_each_entry(trace_buffer, &kbdev->csf.firmware_trace_buffers.list, node) {
		u32 extract_gpu_va, insert_gpu_va, data_buffer_gpu_va,
			trace_enable_size_dwords;
		u32 *extract_cpu_va, *insert_cpu_va;
		unsigned int i;

		/* Rely on the fact that all required mappings already exist */
		extract_gpu_va =
			(kbdev->csf.firmware_trace_buffers.mcu_rw.va_reg->start_pfn << PAGE_SHIFT) +
			mcu_rw_offset;
		extract_cpu_va = (u32 *)(
			kbdev->csf.firmware_trace_buffers.mcu_rw.cpu_addr +
			mcu_rw_offset);
		insert_gpu_va =
			(kbdev->csf.firmware_trace_buffers.mcu_write.va_reg->start_pfn << PAGE_SHIFT) +
			mcu_write_offset;
		insert_cpu_va = (u32 *)(
			kbdev->csf.firmware_trace_buffers.mcu_write.cpu_addr +
			mcu_write_offset);
		data_buffer_gpu_va =
			(trace_buffer->data_mapping.va_reg->start_pfn << PAGE_SHIFT);

		/* Notice that the function only re-updates firmware memory locations
		 * with information that allows access to the trace buffers without
		 * really resetting their state. For instance, the Insert offset will
		 * not change and, as a consequence, the Extract offset is not going
		 * to be reset to keep consistency.
		 */

		/* Each FW address shall be mapped and set individually, as we can't
		 * assume anything about their location in the memory address space.
		 */
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.data_address, data_buffer_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.insert_address, insert_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.extract_address, extract_gpu_va);
		kbase_csf_update_firmware_memory(
				kbdev, trace_buffer->gpu_va.size_address,
				trace_buffer->num_pages << PAGE_SHIFT);

		trace_enable_size_dwords =
				(trace_buffer->trace_enable_entry_count + 31) >> 5;

		for (i = 0; i < trace_enable_size_dwords; i++) {
			kbase_csf_update_firmware_memory(
					kbdev, trace_buffer->gpu_va.trace_enable + i*4,
					trace_buffer->trace_enable_init_mask[i]);
		}

		/* Store CPU virtual addresses for permanently mapped variables,
		 * as they might have slightly changed.
		 */
		trace_buffer->cpu_va.insert_cpu_va = insert_cpu_va;
		trace_buffer->cpu_va.extract_cpu_va = extract_cpu_va;

		/* Update offsets */
		mcu_write_offset += cache_line_alignment;
		mcu_rw_offset += cache_line_alignment;
	}
}

struct firmware_trace_buffer *kbase_csf_firmware_get_trace_buffer(
	struct kbase_device *kbdev, const char *name)
{
	struct firmware_trace_buffer *trace_buffer;

	list_for_each_entry(trace_buffer, &kbdev->csf.firmware_trace_buffers.list, node) {
		if (!strcmp(trace_buffer->name, name))
			return trace_buffer;
	}

	return NULL;
}
EXPORT_SYMBOL(kbase_csf_firmware_get_trace_buffer);

unsigned int kbase_csf_firmware_trace_buffer_get_trace_enable_bits_count(
	const struct firmware_trace_buffer *trace_buffer)
{
	return trace_buffer->trace_enable_entry_count;
}
EXPORT_SYMBOL(kbase_csf_firmware_trace_buffer_get_trace_enable_bits_count);

static void kbasep_csf_firmware_trace_buffer_update_trace_enable_bit(
	struct firmware_trace_buffer *tb, unsigned int bit, bool value)
{
	struct kbase_device *kbdev = tb->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (bit < tb->trace_enable_entry_count) {
		unsigned int trace_enable_reg_offset = bit >> 5;
		u32 trace_enable_bit_mask = 1u << (bit & 0x1F);

		if (value) {
			tb->trace_enable_init_mask[trace_enable_reg_offset] |=
				trace_enable_bit_mask;
		} else {
			tb->trace_enable_init_mask[trace_enable_reg_offset] &=
				~trace_enable_bit_mask;
		}

		/* This is not strictly needed as the caller is supposed to
		 * reload the firmware image (through GPU reset) after updating
		 * the bitmask. Otherwise there is no guarantee that firmware
		 * will take into account the updated bitmask for all types of
		 * trace buffers, since firmware could continue to use the
		 * value of bitmask it cached after the boot.
		 */
		kbase_csf_update_firmware_memory(
			kbdev,
			tb->gpu_va.trace_enable + trace_enable_reg_offset * 4,
			tb->trace_enable_init_mask[trace_enable_reg_offset]);
	}
}

int kbase_csf_firmware_trace_buffer_update_trace_enable_bit(
	struct firmware_trace_buffer *tb, unsigned int bit, bool value)
{
	struct kbase_device *kbdev = tb->kbdev;
	int err = 0;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* If trace buffer update cannot be performed with
	 * FIRMWARE_CONFIG_UPDATE then we need to do a
	 * silent reset before we update the memory.
	 */
	if (!tb->updatable) {
		/* If there is already a GPU reset pending then inform
		 * the User to retry the update.
		 */
		if (kbase_reset_gpu_silent(kbdev)) {
			dev_warn(
				kbdev->dev,
				"GPU reset already in progress when enabling firmware timeline.");
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			return -EAGAIN;
		}
	}

	kbasep_csf_firmware_trace_buffer_update_trace_enable_bit(tb, bit,
								 value);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (tb->updatable)
		err = kbase_csf_trigger_firmware_config_update(kbdev);

	return err;
}
EXPORT_SYMBOL(kbase_csf_firmware_trace_buffer_update_trace_enable_bit);

bool kbase_csf_firmware_trace_buffer_is_empty(
	const struct firmware_trace_buffer *trace_buffer)
{
	return *(trace_buffer->cpu_va.insert_cpu_va) ==
			*(trace_buffer->cpu_va.extract_cpu_va);
}
EXPORT_SYMBOL(kbase_csf_firmware_trace_buffer_is_empty);

unsigned int kbase_csf_firmware_trace_buffer_read_data(
	struct firmware_trace_buffer *trace_buffer, u8 *data, unsigned int num_bytes)
{
	unsigned int bytes_copied;
	u8 *data_cpu_va = trace_buffer->data_mapping.cpu_addr;
	u32 extract_offset = *(trace_buffer->cpu_va.extract_cpu_va);
	u32 insert_offset = *(trace_buffer->cpu_va.insert_cpu_va);
	u32 buffer_size = trace_buffer->num_pages << PAGE_SHIFT;

	if (insert_offset >= extract_offset) {
		bytes_copied = min_t(unsigned int, num_bytes,
			(insert_offset - extract_offset));
		memcpy(data, &data_cpu_va[extract_offset], bytes_copied);
		extract_offset += bytes_copied;
	} else {
		unsigned int bytes_copied_head, bytes_copied_tail;

		bytes_copied_tail = min_t(unsigned int, num_bytes,
			(buffer_size - extract_offset));
		memcpy(data, &data_cpu_va[extract_offset], bytes_copied_tail);

		bytes_copied_head = min_t(unsigned int,
			(num_bytes - bytes_copied_tail), insert_offset);
		memcpy(&data[bytes_copied_tail], data_cpu_va, bytes_copied_head);

		bytes_copied = bytes_copied_head + bytes_copied_tail;
		extract_offset += bytes_copied;
		if (extract_offset >= buffer_size)
			extract_offset = bytes_copied_head;
	}

	*(trace_buffer->cpu_va.extract_cpu_va) = extract_offset;

	return bytes_copied;
}
EXPORT_SYMBOL(kbase_csf_firmware_trace_buffer_read_data);

static void update_trace_buffer_active_mask64(struct firmware_trace_buffer *tb, u64 mask)
{
	unsigned int i;

	for (i = 0; i < tb->trace_enable_entry_count; i++)
		kbasep_csf_firmware_trace_buffer_update_trace_enable_bit(tb, i, (mask >> i) & 1);
}

#define U32_BITS 32
u64 kbase_csf_firmware_trace_buffer_get_active_mask64(struct firmware_trace_buffer *tb)
{
	u64 active_mask = tb->trace_enable_init_mask[0];

	if (tb->trace_enable_entry_count > U32_BITS)
		active_mask |= (u64)tb->trace_enable_init_mask[1] << U32_BITS;

	return active_mask;
}

int kbase_csf_firmware_trace_buffer_set_active_mask64(struct firmware_trace_buffer *tb, u64 mask)
{
	struct kbase_device *kbdev = tb->kbdev;
	unsigned long flags;
	int err = 0;

	if (!tb->updatable) {
		/* If there is already a GPU reset pending, need a retry */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		if (kbase_reset_gpu_silent(kbdev))
			err = -EAGAIN;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	if (!err) {
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		update_trace_buffer_active_mask64(tb, mask);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		/* if we can update the config we need to just trigger
		 * FIRMWARE_CONFIG_UPDATE.
		 */
		if (tb->updatable)
			err = kbase_csf_trigger_firmware_config_update(kbdev);
	}

	return err;
}
