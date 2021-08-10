// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2018-2021 ARM Limited. All rights reserved.
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
#include "mali_kbase_csf_firmware_cfg.h"
#include "mali_kbase_csf_trace_buffer.h"
#include "mali_kbase_csf_timeout.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_reset_gpu.h"
#include "mali_kbase_ctx_sched.h"
#include "mali_kbase_csf_scheduler.h"
#include "device/mali_kbase_device.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "tl/mali_kbase_timeline_priv.h"
#include "mali_kbase_csf_tl_reader.h"
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"
#include <csf/ipa_control/mali_kbase_csf_ipa_control.h>

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/mutex.h>
#if (KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE)
#include <linux/set_memory.h>
#endif
#include <mmu/mali_kbase_mmu.h>
#include <asm/arch_timer.h>

#define MALI_MAX_FIRMWARE_NAME_LEN ((size_t)20)


static char fw_name[MALI_MAX_FIRMWARE_NAME_LEN] = "mali_csffw.bin";
module_param_string(fw_name, fw_name, sizeof(fw_name), 0644);
MODULE_PARM_DESC(fw_name, "firmware image");

/* The waiting time for firmware to boot */
static unsigned int csf_firmware_boot_timeout_ms = 500;
module_param(csf_firmware_boot_timeout_ms, uint, 0444);
MODULE_PARM_DESC(csf_firmware_boot_timeout_ms,
		 "Maximum time to wait for firmware to boot.");

#ifdef CONFIG_MALI_BIFROST_DEBUG
/* Makes Driver wait indefinitely for an acknowledgment for the different
 * requests it sends to firmware. Otherwise the timeouts interfere with the
 * use of debugger for source-level debugging of firmware as Driver initiates
 * a GPU reset when a request times out, which always happen when a debugger
 * is connected.
 */
bool fw_debug; /* Default value of 0/false */
module_param(fw_debug, bool, 0444);
MODULE_PARM_DESC(fw_debug,
	"Enables effective use of a debugger for debugging firmware code.");
#endif

#define FIRMWARE_HEADER_MAGIC    (0xC3F13A6Eul)
#define FIRMWARE_HEADER_VERSION  (0ul)
#define FIRMWARE_HEADER_LENGTH   (0x14ul)

#define CSF_FIRMWARE_ENTRY_SUPPORTED_FLAGS \
	(CSF_FIRMWARE_ENTRY_READ | \
	 CSF_FIRMWARE_ENTRY_WRITE | \
	 CSF_FIRMWARE_ENTRY_EXECUTE | \
	 CSF_FIRMWARE_ENTRY_PROTECTED | \
	 CSF_FIRMWARE_ENTRY_SHARED | \
	 CSF_FIRMWARE_ENTRY_ZERO | \
	 CSF_FIRMWARE_ENTRY_CACHE_MODE)

#define CSF_FIRMWARE_ENTRY_TYPE_INTERFACE     (0)
#define CSF_FIRMWARE_ENTRY_TYPE_CONFIGURATION (1)
#define CSF_FIRMWARE_ENTRY_TYPE_FUTF_TEST     (2)
#define CSF_FIRMWARE_ENTRY_TYPE_TRACE_BUFFER  (3)
#define CSF_FIRMWARE_ENTRY_TYPE_TIMELINE_METADATA (4)

#define CSF_FIRMWARE_CACHE_MODE_NONE              (0ul << 3)
#define CSF_FIRMWARE_CACHE_MODE_CACHED            (1ul << 3)
#define CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT (2ul << 3)
#define CSF_FIRMWARE_CACHE_MODE_CACHED_COHERENT   (3ul << 3)

#define INTERFACE_ENTRY_NAME_OFFSET (0x14)

#define TL_METADATA_ENTRY_NAME_OFFSET (0x8)

#define CSF_MAX_FW_STOP_LOOPS            (100000)

#define CSF_GLB_REQ_CFG_MASK                                                   \
	(GLB_REQ_CFG_ALLOC_EN_MASK | GLB_REQ_CFG_PROGRESS_TIMER_MASK |         \
	 GLB_REQ_CFG_PWROFF_TIMER_MASK)

static inline u32 input_page_read(const u32 *const input, const u32 offset)
{
	WARN_ON(offset % sizeof(u32));

	return input[offset / sizeof(u32)];
}

static inline void input_page_write(u32 *const input, const u32 offset,
			const u32 value)
{
	WARN_ON(offset % sizeof(u32));

	input[offset / sizeof(u32)] = value;
}

static inline void input_page_partial_write(u32 *const input, const u32 offset,
			u32 value, u32 mask)
{
	WARN_ON(offset % sizeof(u32));

	input[offset / sizeof(u32)] =
		(input_page_read(input, offset) & ~mask) | (value & mask);
}

static inline u32 output_page_read(const u32 *const output, const u32 offset)
{
	WARN_ON(offset % sizeof(u32));

	return output[offset / sizeof(u32)];
}

static unsigned int entry_type(u32 header)
{
	return header & 0xFF;
}
static unsigned int entry_size(u32 header)
{
	return (header >> 8) & 0xFF;
}
static bool entry_update(u32 header)
{
	return (header >> 30) & 0x1;
}
static bool entry_optional(u32 header)
{
	return (header >> 31) & 0x1;
}

/**
 * struct firmware_timeline_metadata -
 * Timeline metadata item within the MCU firmware
 *
 * @node: List head linking all timeline metadata to
 *        kbase_device:csf.firmware_timeline_metadata.
 * @name: NUL-terminated string naming the metadata.
 * @data: Metadata content.
 * @size: Metadata size.
 */
struct firmware_timeline_metadata {
	struct list_head node;
	char *name;
	char *data;
	size_t size;
};

/* The shared interface area, used for communicating with firmware, is managed
 * like a virtual memory zone. Reserve the virtual space from that zone
 * corresponding to shared interface entry parsed from the firmware image.
 * The shared_reg_rbtree should have been initialized before calling this
 * function.
 */
static int setup_shared_iface_static_region(struct kbase_device *kbdev)
{
	struct kbase_csf_firmware_interface *interface =
		kbdev->csf.shared_interface;
	struct kbase_va_region *reg;
	int ret = -ENOMEM;

	if (!interface)
		return -EINVAL;

	reg = kbase_alloc_free_region(&kbdev->csf.shared_reg_rbtree, 0,
			interface->num_pages, KBASE_REG_ZONE_MCU_SHARED);
	if (reg) {
		ret = kbase_add_va_region_rbtree(kbdev, reg,
				interface->virtual, interface->num_pages, 1);
		if (ret)
			kfree(reg);
		else
			reg->flags &= ~KBASE_REG_FREE;
	}

	return ret;
}

static int wait_mcu_status_value(struct kbase_device *kbdev, u32 val)
{
	u32 max_loops = CSF_MAX_FW_STOP_LOOPS;

	/* wait for the MCU_STATUS register to reach the given status value */
	while (--max_loops &&
	       (kbase_reg_read(kbdev, GPU_CONTROL_REG(MCU_STATUS)) != val)) {
	}

	return (max_loops == 0) ? -1 : 0;
}

void kbase_csf_firmware_disable_mcu_wait(struct kbase_device *kbdev)
{
	if (wait_mcu_status_value(kbdev, MCU_CNTRL_DISABLE) < 0)
		dev_err(kbdev->dev, "MCU failed to get disabled");
}

static void wait_for_firmware_stop(struct kbase_device *kbdev)
{
	if (wait_mcu_status_value(kbdev, MCU_CNTRL_DISABLE) < 0) {
		/* This error shall go away once MIDJM-2371 is closed */
		dev_err(kbdev->dev, "Firmware failed to stop");
	}
}

static void stop_csf_firmware(struct kbase_device *kbdev)
{
	/* Stop the MCU firmware */
	kbase_csf_firmware_disable_mcu(kbdev);

	wait_for_firmware_stop(kbdev);
}

static void wait_for_firmware_boot(struct kbase_device *kbdev)
{
	const long wait_timeout =
		kbase_csf_timeout_in_jiffies(csf_firmware_boot_timeout_ms);
	long remaining;

	/* Firmware will generate a global interface interrupt once booting
	 * is complete
	 */
	remaining = wait_event_timeout(kbdev->csf.event_wait,
			kbdev->csf.interrupt_received == true, wait_timeout);

	if (!remaining)
		dev_err(kbdev->dev, "Timed out waiting for fw boot completion");

	kbdev->csf.interrupt_received = false;
}

static void boot_csf_firmware(struct kbase_device *kbdev)
{
	kbase_csf_firmware_enable_mcu(kbdev);

	wait_for_firmware_boot(kbdev);
}

static void wait_ready(struct kbase_device *kbdev)
{
	u32 max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;
	u32 val;

	val = kbase_reg_read(kbdev, MMU_AS_REG(MCU_AS_NR, AS_STATUS));

	/* Wait for a while for the update command to take effect */
	while (--max_loops && (val & AS_STATUS_AS_ACTIVE))
		val = kbase_reg_read(kbdev, MMU_AS_REG(MCU_AS_NR, AS_STATUS));

	if (max_loops == 0)
		dev_err(kbdev->dev, "AS_ACTIVE bit stuck, might be caused by slow/unstable GPU clock or possible faulty FPGA connector\n");
}

static void unload_mmu_tables(struct kbase_device *kbdev)
{
	unsigned long irq_flags;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	if (kbdev->pm.backend.gpu_powered)
		kbase_mmu_disable_as(kbdev, MCU_AS_NR);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
}

static void load_mmu_tables(struct kbase_device *kbdev)
{
	unsigned long irq_flags;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, irq_flags);
	kbase_mmu_update(kbdev, &kbdev->csf.mcu_mmu, MCU_AS_NR);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, irq_flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* Wait for a while for the update command to take effect */
	wait_ready(kbdev);
}

/**
 * convert_mem_flags() - Convert firmware memory flags to GPU region flags
 *
 * Return: GPU memory region flags
 *
 * @kbdev: Instance of GPU platform device (used to determine system coherency)
 * @flags: Flags of an "interface memory setup" section in a firmware image
 * @cm:    appropriate cache mode chosen for the "interface memory setup"
 *         section, which could be different from the cache mode requested by
 *         firmware.
 */
static unsigned long convert_mem_flags(const struct kbase_device * const kbdev,
	const u32 flags, u32 *cm)
{
	unsigned long mem_flags = 0;
	u32 cache_mode = flags & CSF_FIRMWARE_ENTRY_CACHE_MODE;
	bool is_shared = (flags & CSF_FIRMWARE_ENTRY_SHARED) ? true : false;

	/* The memory flags control the access permissions for the MCU, the
	 * shader cores/tiler are not expected to access this memory
	 */
	if (flags & CSF_FIRMWARE_ENTRY_READ)
		mem_flags |= KBASE_REG_GPU_RD;

	if (flags & CSF_FIRMWARE_ENTRY_WRITE)
		mem_flags |= KBASE_REG_GPU_WR;

	if ((flags & CSF_FIRMWARE_ENTRY_EXECUTE) == 0)
		mem_flags |= KBASE_REG_GPU_NX;

	if (flags & CSF_FIRMWARE_ENTRY_PROTECTED)
		mem_flags |= KBASE_REG_PROTECTED;

	/* Substitute uncached coherent memory for cached coherent memory if
	 * the system does not support ACE coherency.
	 */
	if ((cache_mode == CSF_FIRMWARE_CACHE_MODE_CACHED_COHERENT) &&
		(kbdev->system_coherency != COHERENCY_ACE))
		cache_mode = CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT;

	/* Substitute uncached incoherent memory for uncached coherent memory
	 * if the system does not support ACE-Lite coherency.
	 */
	if ((cache_mode == CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT) &&
		(kbdev->system_coherency == COHERENCY_NONE))
		cache_mode = CSF_FIRMWARE_CACHE_MODE_NONE;

	*cm = cache_mode;

	switch (cache_mode) {
	case CSF_FIRMWARE_CACHE_MODE_NONE:
		mem_flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
		break;
	case CSF_FIRMWARE_CACHE_MODE_CACHED:
		mem_flags |=
			KBASE_REG_MEMATTR_INDEX(
			AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY);
		break;
	case CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT:
	case CSF_FIRMWARE_CACHE_MODE_CACHED_COHERENT:
		WARN_ON(!is_shared);
		mem_flags |= KBASE_REG_SHARE_BOTH |
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_SHARED);
		break;
	default:
		dev_err(kbdev->dev,
			"Firmware contains interface with unsupported cache mode\n");
		break;
	}
	return mem_flags;
}

static void load_fw_image_section(struct kbase_device *kbdev, const u8 *data,
		struct tagged_addr *phys, u32 num_pages, u32 flags,
		u32 data_start, u32 data_end)
{
	u32 data_pos = data_start;
	u32 data_len = data_end - data_start;
	u32 page_num;
	u32 page_limit;

	if (flags & CSF_FIRMWARE_ENTRY_ZERO)
		page_limit = num_pages;
	else
		page_limit = (data_len + PAGE_SIZE - 1) / PAGE_SIZE;

	for (page_num = 0; page_num < page_limit; ++page_num) {
		struct page *const page = as_page(phys[page_num]);
		char *const p = kmap_atomic(page);
		u32 const copy_len = min_t(u32, PAGE_SIZE, data_len);

		if (copy_len > 0) {
			memcpy(p, data + data_pos, copy_len);
			data_pos += copy_len;
			data_len -= copy_len;
		}

		if (flags & CSF_FIRMWARE_ENTRY_ZERO) {
			u32 const zi_len = PAGE_SIZE - copy_len;

			memset(p + copy_len, 0, zi_len);
		}

		kbase_sync_single_for_device(kbdev, kbase_dma_addr(page),
				PAGE_SIZE, DMA_TO_DEVICE);
		kunmap_atomic(p);
	}
}

static int reload_fw_data_sections(struct kbase_device *kbdev)
{
	const u32 magic = FIRMWARE_HEADER_MAGIC;
	struct kbase_csf_firmware_interface *interface;
	const struct firmware *firmware;
	int ret = 0;

	if (request_firmware(&firmware, fw_name, kbdev->dev) != 0) {
		dev_err(kbdev->dev,
			"Failed to reload firmware image '%s'\n",
			fw_name);
		return -ENOENT;
	}

	/* Do couple of basic sanity checks */
	if (firmware->size < FIRMWARE_HEADER_LENGTH) {
		dev_err(kbdev->dev, "Firmware image unexpectedly too small\n");
		ret = -EINVAL;
		goto out;
	}

	if (memcmp(firmware->data, &magic, sizeof(magic)) != 0) {
		dev_err(kbdev->dev, "Incorrect magic value, firmware image could have been corrupted\n");
		ret = -EINVAL;
		goto out;
	}

	list_for_each_entry(interface, &kbdev->csf.firmware_interfaces, node) {
		/* Skip reload of text & read only data sections */
		if ((interface->flags & CSF_FIRMWARE_ENTRY_EXECUTE) ||
		    !(interface->flags & CSF_FIRMWARE_ENTRY_WRITE))
			continue;

		load_fw_image_section(kbdev, firmware->data, interface->phys,
			interface->num_pages, interface->flags,
			interface->data_start, interface->data_end);
	}

	kbase_csf_firmware_reload_trace_buffers_data(kbdev);

out:
	release_firmware(firmware);
	return ret;
}

/**
 * parse_memory_setup_entry() - Process an "interface memory setup" section
 *
 * Read an "interface memory setup" section from the firmware image and create
 * the necessary memory region including the MMU page tables. If successful
 * the interface will be added to the kbase_device:csf.firmware_interfaces list.
 *
 * Return: 0 if successful, negative error code on failure
 *
 * @kbdev: Kbase device structure
 * @fw: The firmware image containing the section
 * @entry: Pointer to the start of the section
 * @size: Size (in bytes) of the section
 */
static int parse_memory_setup_entry(struct kbase_device *kbdev,
		const struct firmware *fw,
		const u32 *entry, unsigned int size)
{
	int ret = 0;
	const u32 flags = entry[0];
	const u32 virtual_start = entry[1];
	const u32 virtual_end = entry[2];
	const u32 data_start = entry[3];
	const u32 data_end = entry[4];
	u32 num_pages;
	char *name;
	struct tagged_addr *phys = NULL;
	struct kbase_csf_firmware_interface *interface = NULL;
	bool allocated_pages = false, protected_mode = false;
	unsigned long mem_flags = 0;
	u32 cache_mode = 0;
	struct protected_memory_allocation **pma = NULL;

	if (data_end < data_start) {
		dev_err(kbdev->dev, "Firmware corrupt, data_end < data_start (0x%x<0x%x)\n",
				data_end, data_start);
		return -EINVAL;
	}
	if (virtual_end < virtual_start) {
		dev_err(kbdev->dev, "Firmware corrupt, virtual_end < virtual_start (0x%x<0x%x)\n",
				virtual_end, virtual_start);
		return -EINVAL;
	}
	if (data_end > fw->size) {
		dev_err(kbdev->dev, "Firmware corrupt, file truncated? data_end=0x%x > fw->size=0x%zx\n",
				data_end, fw->size);
		return -EINVAL;
	}

	if ((virtual_start & ~PAGE_MASK) != 0 ||
			(virtual_end & ~PAGE_MASK) != 0) {
		dev_err(kbdev->dev, "Firmware corrupt: virtual addresses not page aligned: 0x%x-0x%x\n",
				virtual_start, virtual_end);
		return -EINVAL;
	}

	if ((flags & CSF_FIRMWARE_ENTRY_SUPPORTED_FLAGS) != flags) {
		dev_err(kbdev->dev, "Firmware contains interface with unsupported flags (0x%x)\n",
				flags);
		return -EINVAL;
	}

	if (flags & CSF_FIRMWARE_ENTRY_PROTECTED)
		protected_mode = true;

	if (protected_mode && kbdev->csf.pma_dev == NULL) {
		dev_err(kbdev->dev,
			"Protected memory allocator not found, Firmware protected mode entry will not be supported");
		return 0;
	}

	num_pages = (virtual_end - virtual_start)
		>> PAGE_SHIFT;

	phys = kmalloc_array(num_pages, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		return -ENOMEM;

	if (protected_mode) {
		pma = kbase_csf_protected_memory_alloc(kbdev, phys, num_pages);

		if (pma == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	} else {
		ret = kbase_mem_pool_alloc_pages(
			&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			num_pages, phys, false);
		if (ret < 0)
			goto out;
	}

	allocated_pages = true;
	load_fw_image_section(kbdev, fw->data, phys, num_pages, flags,
			data_start, data_end);

	/* Allocate enough memory for the struct kbase_csf_firmware_interface and
	 * the name of the interface. An extra byte is allocated to place a
	 * NUL-terminator in. This should already be included according to the
	 * specification but here we add it anyway to be robust against a
	 * corrupt firmware image.
	 */
	interface = kmalloc(sizeof(*interface) +
			size - INTERFACE_ENTRY_NAME_OFFSET + 1, GFP_KERNEL);
	if (!interface) {
		ret = -ENOMEM;
		goto out;
	}
	name = (void *)(interface + 1);
	memcpy(name, entry + (INTERFACE_ENTRY_NAME_OFFSET / sizeof(*entry)),
			size - INTERFACE_ENTRY_NAME_OFFSET);
	name[size - INTERFACE_ENTRY_NAME_OFFSET] = 0;

	interface->name = name;
	interface->phys = phys;
	interface->num_pages = num_pages;
	interface->virtual = virtual_start;
	interface->kernel_map = NULL;
	interface->flags = flags;
	interface->data_start = data_start;
	interface->data_end = data_end;
	interface->pma = pma;

	mem_flags = convert_mem_flags(kbdev, flags, &cache_mode);

	if (flags & CSF_FIRMWARE_ENTRY_SHARED) {
		struct page **page_list;
		u32 i;
		pgprot_t cpu_map_prot;
		u32 mem_attr_index = KBASE_REG_MEMATTR_VALUE(mem_flags);

		/* Since SHARED memory type was used for mapping shared memory
		 * on GPU side, it can be mapped as cached on CPU side on both
		 * types of coherent platforms.
		 */
		if ((cache_mode == CSF_FIRMWARE_CACHE_MODE_CACHED_COHERENT) ||
		    (cache_mode == CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT)) {
			WARN_ON(mem_attr_index !=
					AS_MEMATTR_INDEX_SHARED);
			cpu_map_prot = PAGE_KERNEL;
		} else {
			WARN_ON(mem_attr_index !=
					AS_MEMATTR_INDEX_NON_CACHEABLE);
			cpu_map_prot = pgprot_writecombine(PAGE_KERNEL);
		}

		page_list = kmalloc_array(num_pages, sizeof(*page_list),
				GFP_KERNEL);
		if (!page_list) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < num_pages; i++)
			page_list[i] = as_page(phys[i]);

		interface->kernel_map = vmap(page_list, num_pages, VM_MAP,
				cpu_map_prot);

		kfree(page_list);

		if (!interface->kernel_map) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* Start location of the shared interface area is fixed and is
	 * specified in firmware spec, and so there shall only be a
	 * single entry with that start address.
	 */
	if (virtual_start == (KBASE_REG_ZONE_MCU_SHARED_BASE << PAGE_SHIFT))
		kbdev->csf.shared_interface = interface;

	list_add(&interface->node, &kbdev->csf.firmware_interfaces);

	ret = kbase_mmu_insert_pages_no_flush(kbdev, &kbdev->csf.mcu_mmu,
			virtual_start >> PAGE_SHIFT, phys, num_pages, mem_flags,
			KBASE_MEM_GROUP_CSF_FW);

	if (ret != 0) {
		dev_err(kbdev->dev, "Failed to insert firmware pages\n");
		/* The interface has been added to the list, so cleanup will
		 * be handled by firmware unloading
		 */
	}

	dev_dbg(kbdev->dev, "Processed section '%s'", name);

	return ret;

out:
	if (allocated_pages) {
		if (protected_mode) {
			kbase_csf_protected_memory_free(kbdev, pma, num_pages);
		} else {
			kbase_mem_pool_free_pages(
				&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
				num_pages, phys, false, false);
		}
	}

	kfree(phys);
	kfree(interface);
	return ret;
}

/**
 * parse_timeline_metadata_entry() - Process a "timeline metadata" section
 *
 * Return: 0 if successful, negative error code on failure
 *
 * @kbdev: Kbase device structure
 * @fw:    Firmware image containing the section
 * @entry: Pointer to the section
 * @size:  Size (in bytes) of the section
 */
static int parse_timeline_metadata_entry(struct kbase_device *kbdev,
	const struct firmware *fw, const u32 *entry, unsigned int size)
{
	const u32 data_start = entry[0];
	const u32 data_size = entry[1];
	const u32 data_end = data_start + data_size;
	const char *name = (char *)&entry[2];
	struct firmware_timeline_metadata *metadata;
	const unsigned int name_len =
		size - TL_METADATA_ENTRY_NAME_OFFSET;
	size_t allocation_size = sizeof(*metadata) + name_len + 1 + data_size;

	if (data_end > fw->size) {
		dev_err(kbdev->dev,
			"Firmware corrupt, file truncated? data_end=0x%x > fw->size=0x%zx",
			data_end, fw->size);
		return -EINVAL;
	}

	/* Allocate enough space for firmware_timeline_metadata,
	 * its name and the content.
	 */
	metadata = kmalloc(allocation_size, GFP_KERNEL);
	if (!metadata)
		return -ENOMEM;

	metadata->name = (char *)(metadata + 1);
	metadata->data = (char *)(metadata + 1) + name_len + 1;
	metadata->size = data_size;

	memcpy(metadata->name, name, name_len);
	metadata->name[name_len] = 0;

	/* Copy metadata's content. */
	memcpy(metadata->data, fw->data + data_start, data_size);

	list_add(&metadata->node, &kbdev->csf.firmware_timeline_metadata);

	dev_dbg(kbdev->dev, "Timeline metadata '%s'", metadata->name);

	return 0;
}

/**
 * load_firmware_entry() - Process an entry from a firmware image
 *
 * Read an entry from a firmware image and do any necessary work (e.g. loading
 * the data into page accessible to the MCU).
 *
 * Unknown entries are ignored if the 'optional' flag is set within the entry,
 * otherwise the function will fail with -EINVAL
 *
 * Return: 0 if successful, negative error code on failure
 *
 * @kbdev:  Kbase device
 * @fw:     Firmware image containing the entry
 * @offset: Byte offset within the image of the entry to load
 * @header: Header word of the entry
 */
static int load_firmware_entry(struct kbase_device *kbdev,
		const struct firmware *fw,
		u32 offset, u32 header)
{
	const unsigned int type = entry_type(header);
	unsigned int size = entry_size(header);
	const bool optional = entry_optional(header);
	/* Update is used with configuration and tracebuffer entries to
	 * initiate a FIRMWARE_CONFIG_UPDATE, instead of triggering a
	 * silent reset.
	 */
	const bool updatable = entry_update(header);
	const u32 *entry = (void *)(fw->data + offset);

	if ((offset % sizeof(*entry)) || (size % sizeof(*entry))) {
		dev_err(kbdev->dev, "Firmware entry isn't 32 bit aligned, offset=0x%x size=0x%x\n",
				offset, size);
		return -EINVAL;
	}

	if (size < sizeof(*entry)) {
		dev_err(kbdev->dev, "Size field too small: %u\n", size);
		return -EINVAL;
	}

	/* Remove the header */
	entry++;
	size -= sizeof(*entry);

	switch (type) {
	case CSF_FIRMWARE_ENTRY_TYPE_INTERFACE:
		/* Interface memory setup */
		if (size < INTERFACE_ENTRY_NAME_OFFSET + sizeof(*entry)) {
			dev_err(kbdev->dev, "Interface memory setup entry too short (size=%u)\n",
					size);
			return -EINVAL;
		}
		return parse_memory_setup_entry(kbdev, fw, entry, size);
	case CSF_FIRMWARE_ENTRY_TYPE_CONFIGURATION:
		/* Configuration option */
		if (size < CONFIGURATION_ENTRY_NAME_OFFSET + sizeof(*entry)) {
			dev_err(kbdev->dev, "Configuration option entry too short (size=%u)\n",
					size);
			return -EINVAL;
		}
		return kbase_csf_firmware_cfg_option_entry_parse(
			kbdev, fw, entry, size, updatable);
	case CSF_FIRMWARE_ENTRY_TYPE_FUTF_TEST:
#ifndef MALI_KBASE_BUILD
		/* FW UTF option */
		if (size < 2*sizeof(*entry)) {
			dev_err(kbdev->dev, "FW UTF entry too short (size=%u)\n",
					size);
			return -EINVAL;
		}
		return mali_kutf_process_fw_utf_entry(kbdev, fw->data,
						      fw->size, entry);
#endif
		break;
	case CSF_FIRMWARE_ENTRY_TYPE_TRACE_BUFFER:
		/* Trace buffer */
		if (size < TRACE_BUFFER_ENTRY_NAME_OFFSET + sizeof(*entry)) {
			dev_err(kbdev->dev, "Trace Buffer entry too short (size=%u)\n",
				size);
			return -EINVAL;
		}
		return kbase_csf_firmware_parse_trace_buffer_entry(
			kbdev, entry, size, updatable);
	case CSF_FIRMWARE_ENTRY_TYPE_TIMELINE_METADATA:
		/* Meta data section */
		if (size < TL_METADATA_ENTRY_NAME_OFFSET + sizeof(*entry)) {
			dev_err(kbdev->dev, "Timeline metadata entry too short (size=%u)\n",
				size);
			return -EINVAL;
		}
		return parse_timeline_metadata_entry(kbdev, fw, entry, size);
	}

	if (!optional) {
		dev_err(kbdev->dev,
			"Unsupported non-optional entry type %u in firmware\n",
			type);
		return -EINVAL;
	}

	return 0;
}

static void free_global_iface(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *iface = &kbdev->csf.global_iface;

	if (iface->groups) {
		unsigned int gid;

		for (gid = 0; gid < iface->group_num; ++gid)
			kfree(iface->groups[gid].streams);

		kfree(iface->groups);
		iface->groups = NULL;
	}
}

/**
 * iface_gpu_va_to_cpu - Convert a GPU VA address within the shared interface
 *                       region to a CPU address, using the existing mapping.
 * @kbdev: Device pointer
 * @gpu_va: GPU VA to convert
 *
 * Return: A CPU pointer to the location within the shared interface region, or
 *         NULL on failure.
 */
static inline void *iface_gpu_va_to_cpu(struct kbase_device *kbdev, u32 gpu_va)
{
	struct kbase_csf_firmware_interface *interface =
		kbdev->csf.shared_interface;
	u8 *kernel_base = interface->kernel_map;

	if (gpu_va < interface->virtual ||
	    gpu_va >= interface->virtual + interface->num_pages * PAGE_SIZE) {
		dev_err(kbdev->dev,
				"Interface address 0x%x not within %u-page region at 0x%x",
				gpu_va, interface->num_pages,
				interface->virtual);
		return NULL;
	}

	return (void *)(kernel_base + (gpu_va - interface->virtual));
}

static int parse_cmd_stream_info(struct kbase_device *kbdev,
		struct kbase_csf_cmd_stream_info *sinfo,
		u32 *stream_base)
{
	sinfo->kbdev = kbdev;
	sinfo->features = stream_base[STREAM_FEATURES/4];
	sinfo->input = iface_gpu_va_to_cpu(kbdev,
			stream_base[STREAM_INPUT_VA/4]);
	sinfo->output = iface_gpu_va_to_cpu(kbdev,
			stream_base[STREAM_OUTPUT_VA/4]);

	if (sinfo->input == NULL || sinfo->output == NULL)
		return -EINVAL;

	return 0;
}

static int parse_cmd_stream_group_info(struct kbase_device *kbdev,
		struct kbase_csf_cmd_stream_group_info *ginfo,
		u32 *group_base, u32 group_stride)
{
	unsigned int sid;

	ginfo->kbdev = kbdev;
	ginfo->features = group_base[GROUP_FEATURES/4];
	ginfo->input = iface_gpu_va_to_cpu(kbdev,
			group_base[GROUP_INPUT_VA/4]);
	ginfo->output = iface_gpu_va_to_cpu(kbdev,
			group_base[GROUP_OUTPUT_VA/4]);

	if (ginfo->input == NULL || ginfo->output == NULL)
		return -ENOMEM;

	ginfo->suspend_size = group_base[GROUP_SUSPEND_SIZE/4];
	ginfo->protm_suspend_size = group_base[GROUP_PROTM_SUSPEND_SIZE/4];
	ginfo->stream_num = group_base[GROUP_STREAM_NUM/4];

	if (ginfo->stream_num < MIN_SUPPORTED_STREAMS_PER_GROUP ||
			ginfo->stream_num > MAX_SUPPORTED_STREAMS_PER_GROUP) {
		dev_err(kbdev->dev, "CSG with %u CSs out of range %u-%u",
				ginfo->stream_num,
				MIN_SUPPORTED_STREAMS_PER_GROUP,
				MAX_SUPPORTED_STREAMS_PER_GROUP);
		return -EINVAL;
	}

	ginfo->stream_stride = group_base[GROUP_STREAM_STRIDE/4];

	if (ginfo->stream_num * ginfo->stream_stride > group_stride) {
		dev_err(kbdev->dev,
				"group stride of 0x%x exceeded by %u CSs with stride 0x%x",
				group_stride, ginfo->stream_num,
				ginfo->stream_stride);
		return -EINVAL;
	}

	ginfo->streams = kmalloc_array(ginfo->stream_num,
			sizeof(*ginfo->streams), GFP_KERNEL);

	if (!ginfo->streams)
		return -ENOMEM;

	for (sid = 0; sid < ginfo->stream_num; sid++) {
		int err;
		u32 *stream_base = group_base + (STREAM_CONTROL_0 +
				ginfo->stream_stride * sid) / 4;

		err = parse_cmd_stream_info(kbdev, &ginfo->streams[sid],
				stream_base);
		if (err < 0) {
			/* caller will free the memory for CSs array */
			return err;
		}
	}

	return 0;
}

static u32 get_firmware_version(struct kbase_device *kbdev)
{
	struct kbase_csf_firmware_interface *interface =
		kbdev->csf.shared_interface;
	u32 *shared_info = interface->kernel_map;

	return shared_info[GLB_VERSION/4];
}

static int parse_capabilities(struct kbase_device *kbdev)
{
	struct kbase_csf_firmware_interface *interface =
		kbdev->csf.shared_interface;
	u32 *shared_info = interface->kernel_map;
	struct kbase_csf_global_iface *iface = &kbdev->csf.global_iface;
	unsigned int gid;

	/* All offsets are in bytes, so divide by 4 for access via a u32 pointer
	 */

	/* The version number of the global interface is expected to be a
	 * non-zero value. If it's not, the firmware may not have booted.
	 */
	iface->version = get_firmware_version(kbdev);
	if (!iface->version) {
		dev_err(kbdev->dev, "Version check failed. Firmware may have failed to boot.");
		return -EINVAL;
	}


	iface->kbdev = kbdev;
	iface->features = shared_info[GLB_FEATURES/4];
	iface->input = iface_gpu_va_to_cpu(kbdev, shared_info[GLB_INPUT_VA/4]);
	iface->output = iface_gpu_va_to_cpu(kbdev,
			shared_info[GLB_OUTPUT_VA/4]);

	if (iface->input == NULL || iface->output == NULL)
		return -ENOMEM;

	iface->group_num = shared_info[GLB_GROUP_NUM/4];

	if (iface->group_num < MIN_SUPPORTED_CSGS ||
			iface->group_num > MAX_SUPPORTED_CSGS) {
		dev_err(kbdev->dev,
				"Interface containing %u CSGs outside of range %u-%u",
				iface->group_num, MIN_SUPPORTED_CSGS,
				MAX_SUPPORTED_CSGS);
		return -EINVAL;
	}

	iface->group_stride = shared_info[GLB_GROUP_STRIDE/4];
	iface->prfcnt_size = shared_info[GLB_PRFCNT_SIZE/4];

	if (iface->version >= kbase_csf_interface_version(1, 1, 0)) {
		iface->instr_features = shared_info[GLB_INSTR_FEATURES / 4];
	} else {
		iface->instr_features = 0;
	}

	if ((GROUP_CONTROL_0 +
		(unsigned long)iface->group_num * iface->group_stride) >
			(interface->num_pages * PAGE_SIZE)) {
		dev_err(kbdev->dev,
				"interface size of %u pages exceeded by %u CSGs with stride 0x%x",
				interface->num_pages, iface->group_num,
				iface->group_stride);
		return -EINVAL;
	}

	WARN_ON(iface->groups);

	iface->groups = kcalloc(iface->group_num, sizeof(*iface->groups),
				GFP_KERNEL);
	if (!iface->groups)
		return -ENOMEM;

	for (gid = 0; gid < iface->group_num; gid++) {
		int err;
		u32 *group_base = shared_info + (GROUP_CONTROL_0 +
				iface->group_stride * gid) / 4;

		err = parse_cmd_stream_group_info(kbdev, &iface->groups[gid],
				group_base, iface->group_stride);
		if (err < 0) {
			free_global_iface(kbdev);
			return err;
		}
	}

	return 0;
}

static inline void access_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 *value, const bool read)
{
	struct kbase_csf_firmware_interface *interface;

	list_for_each_entry(interface, &kbdev->csf.firmware_interfaces, node) {
		if ((gpu_addr >= interface->virtual) &&
			(gpu_addr < interface->virtual + (interface->num_pages << PAGE_SHIFT))) {
			u32 offset_bytes = gpu_addr - interface->virtual;
			u32 page_num = offset_bytes >> PAGE_SHIFT;
			u32 offset_in_page = offset_bytes & ~PAGE_MASK;
			struct page *target_page = as_page(
				interface->phys[page_num]);
			u32 *cpu_addr = kmap_atomic(target_page);

			if (read) {
				kbase_sync_single_for_device(kbdev,
					kbase_dma_addr(target_page) + offset_in_page,
					sizeof(u32), DMA_BIDIRECTIONAL);

				*value = cpu_addr[offset_in_page >> 2];
			} else {
				cpu_addr[offset_in_page >> 2] = *value;

				kbase_sync_single_for_device(kbdev,
					kbase_dma_addr(target_page) + offset_in_page,
					sizeof(u32), DMA_BIDIRECTIONAL);
			}

			kunmap_atomic(cpu_addr);
			return;
		}
	}
	dev_warn(kbdev->dev, "Invalid GPU VA %x passed\n", gpu_addr);
}

void kbase_csf_read_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 *value)
{
	access_firmware_memory(kbdev, gpu_addr, value, true);
}

void kbase_csf_update_firmware_memory(struct kbase_device *kbdev,
	u32 gpu_addr, u32 value)
{
	access_firmware_memory(kbdev, gpu_addr, &value, false);
}

void kbase_csf_firmware_cs_input(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset,
	const u32 value)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "cs input w: reg %08x val %08x\n", offset, value);
	input_page_write(info->input, offset, value);
}

u32 kbase_csf_firmware_cs_input_read(
	const struct kbase_csf_cmd_stream_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = input_page_read(info->input, offset);

	dev_dbg(kbdev->dev, "cs input r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_cs_input_mask(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset,
	const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "cs input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);
	input_page_partial_write(info->input, offset, value, mask);
}

u32 kbase_csf_firmware_cs_output(
	const struct kbase_csf_cmd_stream_info *const info, const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = output_page_read(info->output, offset);

	dev_dbg(kbdev->dev, "cs output r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_csg_input(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset, const u32 value)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "csg input w: reg %08x val %08x\n",
			offset, value);
	input_page_write(info->input, offset, value);
}

u32 kbase_csf_firmware_csg_input_read(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = input_page_read(info->input, offset);

	dev_dbg(kbdev->dev, "csg input r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_csg_input_mask(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset, const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = info->kbdev;

	dev_dbg(kbdev->dev, "csg input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);
	input_page_partial_write(info->input, offset, value, mask);
}

u32 kbase_csf_firmware_csg_output(
	const struct kbase_csf_cmd_stream_group_info *const info,
	const u32 offset)
{
	const struct kbase_device * const kbdev = info->kbdev;
	u32 const val = output_page_read(info->output, offset);

	dev_dbg(kbdev->dev, "csg output r: reg %08x val %08x\n", offset, val);
	return val;
}

void kbase_csf_firmware_global_input(
	const struct kbase_csf_global_iface *const iface, const u32 offset,
	const u32 value)
{
	const struct kbase_device * const kbdev = iface->kbdev;

	dev_dbg(kbdev->dev, "glob input w: reg %08x val %08x\n", offset, value);
	input_page_write(iface->input, offset, value);
}

void kbase_csf_firmware_global_input_mask(
	const struct kbase_csf_global_iface *const iface, const u32 offset,
	const u32 value, const u32 mask)
{
	const struct kbase_device * const kbdev = iface->kbdev;

	dev_dbg(kbdev->dev, "glob input w: reg %08x val %08x mask %08x\n",
			offset, value, mask);
	input_page_partial_write(iface->input, offset, value, mask);
}

u32 kbase_csf_firmware_global_input_read(
	const struct kbase_csf_global_iface *const iface, const u32 offset)
{
	const struct kbase_device * const kbdev = iface->kbdev;
	u32 const val = input_page_read(iface->input, offset);

	dev_dbg(kbdev->dev, "glob input r: reg %08x val %08x\n", offset, val);
	return val;
}

u32 kbase_csf_firmware_global_output(
	const struct kbase_csf_global_iface *const iface, const u32 offset)
{
	const struct kbase_device * const kbdev = iface->kbdev;
	u32 const val = output_page_read(iface->output, offset);

	dev_dbg(kbdev->dev, "glob output r: reg %08x val %08x\n", offset, val);
	return val;
}

/**
 * handle_internal_firmware_fatal - Handler for CS internal firmware fault.
 *
 * @kbdev:  Pointer to kbase device
 *
 * Report group fatal error to user space for all GPU command queue groups
 * in the device, terminate them and reset GPU.
 */
static void handle_internal_firmware_fatal(struct kbase_device *const kbdev)
{
	int as;

	for (as = 0; as < kbdev->nr_hw_address_spaces; as++) {
		unsigned long flags;
		struct kbase_context *kctx;
		struct kbase_fault fault;

		if (as == MCU_AS_NR)
			continue;

		/* Only handle the fault for an active address space. Lock is
		 * taken here to atomically get reference to context in an
		 * active address space and retain its refcount.
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kctx = kbase_ctx_sched_as_to_ctx_nolock(kbdev, as);

		if (kctx) {
			kbase_ctx_sched_retain_ctx_refcount(kctx);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			continue;
		}

		fault = (struct kbase_fault) {
			.status = GPU_EXCEPTION_TYPE_SW_FAULT_1,
		};

		kbase_csf_ctx_handle_fault(kctx, &fault);
		kbase_ctx_sched_release_ctx_lock(kctx);
	}

	if (kbase_prepare_to_reset_gpu(kbdev,
				       RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
		kbase_reset_gpu(kbdev);
}

/**
 * firmware_error_worker - Worker function for handling firmware internal error
 *
 * @data: Pointer to a work_struct embedded in kbase device.
 *
 * Handle the CS internal firmware error
 */
static void firmware_error_worker(struct work_struct *const data)
{
	struct kbase_device *const kbdev =
		container_of(data, struct kbase_device, csf.fw_error_work);

	handle_internal_firmware_fatal(kbdev);
}

static bool global_request_complete(struct kbase_device *const kbdev,
				    u32 const req_mask)
{
	struct kbase_csf_global_iface *global_iface =
				&kbdev->csf.global_iface;
	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_firmware_global_output(global_iface, GLB_ACK) &
	     req_mask) ==
	    (kbase_csf_firmware_global_input_read(global_iface, GLB_REQ) &
	     req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}

static int wait_for_global_request(struct kbase_device *const kbdev,
				   u32 const req_mask)
{
	const long wait_timeout =
		kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	long remaining;
	int err = 0;

	remaining = wait_event_timeout(kbdev->csf.event_wait,
				       global_request_complete(kbdev, req_mask),
				       wait_timeout);

	if (!remaining) {
		dev_warn(kbdev->dev, "Timed out waiting for global request %x to complete",
			 req_mask);
		err = -ETIMEDOUT;
	}

	return err;
}

static void set_global_request(
	const struct kbase_csf_global_iface *const global_iface,
	u32 const req_mask)
{
	u32 glb_req;

	kbase_csf_scheduler_spin_lock_assert_held(global_iface->kbdev);

	glb_req = kbase_csf_firmware_global_output(global_iface, GLB_ACK);
	glb_req ^= req_mask;
	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ, glb_req,
					     req_mask);
}

static void enable_endpoints_global(
	const struct kbase_csf_global_iface *const global_iface,
	u64 const shader_core_mask)
{
	kbase_csf_firmware_global_input(global_iface, GLB_ALLOC_EN_LO,
		shader_core_mask & U32_MAX);
	kbase_csf_firmware_global_input(global_iface, GLB_ALLOC_EN_HI,
		shader_core_mask >> 32);

	set_global_request(global_iface, GLB_REQ_CFG_ALLOC_EN_MASK);
}

static void enable_shader_poweroff_timer(struct kbase_device *const kbdev,
	const struct kbase_csf_global_iface *const global_iface)
{
	u32 pwroff_reg;

	if (kbdev->csf.firmware_hctl_core_pwr)
		pwroff_reg =
		    GLB_PWROFF_TIMER_TIMER_SOURCE_SET(DISABLE_GLB_PWROFF_TIMER,
			       GLB_PWROFF_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		pwroff_reg = kbdev->csf.mcu_core_pwroff_dur_count;

	kbase_csf_firmware_global_input(global_iface, GLB_PWROFF_TIMER,
					pwroff_reg);
	set_global_request(global_iface, GLB_REQ_CFG_PWROFF_TIMER_MASK);

	/* Save the programed reg value in its shadow field */
	kbdev->csf.mcu_core_pwroff_reg_shadow = pwroff_reg;

	dev_dbg(kbdev->dev, "GLB_PWROFF_TIMER set to 0x%.8x\n", pwroff_reg);
}

static void set_timeout_global(
	const struct kbase_csf_global_iface *const global_iface,
	u64 const timeout)
{
	kbase_csf_firmware_global_input(global_iface, GLB_PROGRESS_TIMER,
		timeout / GLB_PROGRESS_TIMER_TIMEOUT_SCALE);

	set_global_request(global_iface, GLB_REQ_CFG_PROGRESS_TIMER_MASK);
}

static void global_init(struct kbase_device *const kbdev, u64 core_mask)
{
	u32 const ack_irq_mask = GLB_ACK_IRQ_MASK_CFG_ALLOC_EN_MASK |
				 GLB_ACK_IRQ_MASK_PING_MASK |
				 GLB_ACK_IRQ_MASK_CFG_PROGRESS_TIMER_MASK |
				 GLB_ACK_IRQ_MASK_PROTM_ENTER_MASK |
				 GLB_ACK_IRQ_MASK_PROTM_EXIT_MASK |
				 GLB_ACK_IRQ_MASK_FIRMWARE_CONFIG_UPDATE_MASK |
				 GLB_ACK_IRQ_MASK_CFG_PWROFF_TIMER_MASK |
				 GLB_ACK_IRQ_MASK_IDLE_EVENT_MASK;

	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	/* Set the coherency mode for protected mode execution */
	WARN_ON(kbdev->system_coherency == COHERENCY_ACE);
	kbase_csf_firmware_global_input(global_iface, GLB_PROTM_COHERENCY,
					kbdev->system_coherency);

	/* Update shader core allocation enable mask */
	enable_endpoints_global(global_iface, core_mask);
	enable_shader_poweroff_timer(kbdev, global_iface);

	set_timeout_global(global_iface, kbase_csf_timeout_get(kbdev));

	/* Unmask the interrupts */
	kbase_csf_firmware_global_input(global_iface,
		GLB_ACK_IRQ_MASK, ack_irq_mask);

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);

	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

/**
 * global_init_on_boot - Sends a global request to control various features.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface
 *
 * Currently only the request to enable endpoints and timeout for GPU progress
 * timer is sent.
 *
 * Return: 0 on success, or negative on failure.
 */
static int global_init_on_boot(struct kbase_device *const kbdev)
{
	unsigned long flags;
	u64 core_mask;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	core_mask = kbase_pm_ca_get_core_mask(kbdev);
	kbdev->csf.firmware_hctl_core_pwr =
				kbase_pm_no_mcu_core_pwroff(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	global_init(kbdev, core_mask);

	return wait_for_global_request(kbdev, CSF_GLB_REQ_CFG_MASK);
}

void kbase_csf_firmware_global_reinit(struct kbase_device *kbdev,
				      u64 core_mask)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->csf.glb_init_request_pending = true;
	kbdev->csf.firmware_hctl_core_pwr =
				kbase_pm_no_mcu_core_pwroff(kbdev);
	global_init(kbdev, core_mask);
}

bool kbase_csf_firmware_global_reinit_complete(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	WARN_ON(!kbdev->csf.glb_init_request_pending);

	if (global_request_complete(kbdev, CSF_GLB_REQ_CFG_MASK))
		kbdev->csf.glb_init_request_pending = false;

	return !kbdev->csf.glb_init_request_pending;
}

void kbase_csf_firmware_update_core_attr(struct kbase_device *kbdev,
		bool update_core_pwroff_timer, bool update_core_mask, u64 core_mask)
{
	unsigned long flags;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	if (update_core_mask)
		enable_endpoints_global(&kbdev->csf.global_iface, core_mask);
	if (update_core_pwroff_timer)
		enable_shader_poweroff_timer(kbdev, &kbdev->csf.global_iface);

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

bool kbase_csf_firmware_core_attr_updated(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return global_request_complete(kbdev, GLB_REQ_CFG_ALLOC_EN_MASK |
					      GLB_REQ_CFG_PWROFF_TIMER_MASK);
}

/**
 * kbase_csf_firmware_reload_worker() -
 * reload the fw image and re-enable the MCU
 * @work: CSF Work item for reloading the firmware.
 *
 * This helper function will reload the firmware image and re-enable the MCU.
 * It is supposed to be called after MCU(GPU) has been reset.
 * Unlike the initial boot the firmware binary image is not parsed completely.
 * Only the data sections, which were loaded in memory during the initial boot,
 * are re-initialized either by zeroing them or copying their data from the
 * firmware binary image. The memory allocation for the firmware pages and
 * MMU programming is not needed for the reboot, presuming the firmware binary
 * file on the filesystem would not change.
 */
static void kbase_csf_firmware_reload_worker(struct work_struct *work)
{
	struct kbase_device *kbdev = container_of(work, struct kbase_device,
						  csf.firmware_reload_work);
	int err;

	dev_info(kbdev->dev, "reloading firmware");

	/* Reload just the data sections from firmware binary image */
	err = reload_fw_data_sections(kbdev);
	if (err)
		return;

	kbase_csf_tl_reader_reset(&kbdev->timeline->csf_tl_reader);

	/* Reboot the firmware */
	kbase_csf_firmware_enable_mcu(kbdev);
}

void kbase_csf_firmware_trigger_reload(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->csf.firmware_reloaded = false;

	if (kbdev->csf.firmware_reload_needed) {
		kbdev->csf.firmware_reload_needed = false;
		queue_work(system_wq, &kbdev->csf.firmware_reload_work);
	} else {
		kbase_csf_firmware_enable_mcu(kbdev);
	}
}

void kbase_csf_firmware_reload_completed(struct kbase_device *kbdev)
{
	u32 version;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (unlikely(!kbdev->csf.firmware_inited))
		return;

	/* Check firmware rebooted properly: we do not expect
	 * the version number to change with a running reboot.
	 */
	version = get_firmware_version(kbdev);

	if (version != kbdev->csf.global_iface.version)
		dev_err(kbdev->dev, "Version check failed in firmware reboot.");

	KBASE_KTRACE_ADD(kbdev, FIRMWARE_REBOOT, NULL, 0u);

	/* Tell MCU state machine to transit to next state */
	kbdev->csf.firmware_reloaded = true;
	kbase_pm_update_state(kbdev);
}

static u32 convert_dur_to_idle_count(struct kbase_device *kbdev, const u32 dur_ms)
{
#define HYSTERESIS_VAL_UNIT_SHIFT (10)
	/* Get the cntfreq_el0 value, which drives the SYSTEM_TIMESTAMP */
	u64 freq = arch_timer_get_cntfrq();
	u64 dur_val = dur_ms;
	u32 cnt_val_u32, reg_val_u32;
	bool src_system_timestamp = freq > 0;

	if (!src_system_timestamp) {
		/* Get the cycle_counter source alternative */
		spin_lock(&kbdev->pm.clk_rtm.lock);
		if (kbdev->pm.clk_rtm.clks[0])
			freq = kbdev->pm.clk_rtm.clks[0]->clock_val;
		else
			dev_warn(kbdev->dev, "No GPU clock, unexpected intregration issue!");
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		dev_info(kbdev->dev, "Can't get the timestamp frequency, "
			 "use cycle counter format with firmware idle hysteresis!");
	}

	/* Formula for dur_val = ((dur_ms/1000) * freq_HZ) >> 10) */
	dur_val = (dur_val * freq) >> HYSTERESIS_VAL_UNIT_SHIFT;
	dur_val = div_u64(dur_val, 1000);

	/* Interface limits the value field to S32_MAX */
	cnt_val_u32 = (dur_val > S32_MAX) ? S32_MAX : (u32)dur_val;

	reg_val_u32 = GLB_IDLE_TIMER_TIMEOUT_SET(0, cnt_val_u32);
	/* add the source flag */
	if (src_system_timestamp)
		reg_val_u32 = GLB_IDLE_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_IDLE_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		reg_val_u32 = GLB_IDLE_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_IDLE_TIMER_TIMER_SOURCE_GPU_COUNTER);

	return reg_val_u32;
}

u32 kbase_csf_firmware_get_gpu_idle_hysteresis_time(struct kbase_device *kbdev)
{
	return kbdev->csf.gpu_idle_hysteresis_ms;
}

u32 kbase_csf_firmware_set_gpu_idle_hysteresis_time(struct kbase_device *kbdev, u32 dur)
{
	unsigned long flags;
	const u32 hysteresis_val = convert_dur_to_idle_count(kbdev, dur);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbdev->csf.gpu_idle_hysteresis_ms = dur;
	kbdev->csf.gpu_idle_dur_count = hysteresis_val;
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	dev_dbg(kbdev->dev, "CSF set firmware idle hysteresis count-value: 0x%.8x",
		hysteresis_val);

	return hysteresis_val;
}

static u32 convert_dur_to_core_pwroff_count(struct kbase_device *kbdev, const u32 dur_us)
{
#define PWROFF_VAL_UNIT_SHIFT (10)
	/* Get the cntfreq_el0 value, which drives the SYSTEM_TIMESTAMP */
	u64 freq = arch_timer_get_cntfrq();
	u64 dur_val = dur_us;
	u32 cnt_val_u32, reg_val_u32;
	bool src_system_timestamp = freq > 0;

	if (!src_system_timestamp) {
		/* Get the cycle_counter source alternative */
		spin_lock(&kbdev->pm.clk_rtm.lock);
		if (kbdev->pm.clk_rtm.clks[0])
			freq = kbdev->pm.clk_rtm.clks[0]->clock_val;
		else
			dev_warn(kbdev->dev, "No GPU clock, unexpected integration issue!");
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		dev_info(kbdev->dev, "Can't get the timestamp frequency, "
			 "use cycle counter with MCU Core Poweroff timer!");
	}

	/* Formula for dur_val = ((dur_us/1e6) * freq_HZ) >> 10) */
	dur_val = (dur_val * freq) >> HYSTERESIS_VAL_UNIT_SHIFT;
	dur_val = div_u64(dur_val, 1000000);

	/* Interface limits the value field to S32_MAX */
	cnt_val_u32 = (dur_val > S32_MAX) ? S32_MAX : (u32)dur_val;

	reg_val_u32 = GLB_PWROFF_TIMER_TIMEOUT_SET(0, cnt_val_u32);
	/* add the source flag */
	if (src_system_timestamp)
		reg_val_u32 = GLB_PWROFF_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_PWROFF_TIMER_TIMER_SOURCE_SYSTEM_TIMESTAMP);
	else
		reg_val_u32 = GLB_PWROFF_TIMER_TIMER_SOURCE_SET(reg_val_u32,
				GLB_PWROFF_TIMER_TIMER_SOURCE_GPU_COUNTER);

	return reg_val_u32;
}

u32 kbase_csf_firmware_get_mcu_core_pwroff_time(struct kbase_device *kbdev)
{
	return kbdev->csf.mcu_core_pwroff_dur_us;
}

u32 kbase_csf_firmware_set_mcu_core_pwroff_time(struct kbase_device *kbdev, u32 dur)
{
	unsigned long flags;
	const u32 pwroff = convert_dur_to_core_pwroff_count(kbdev, dur);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->csf.mcu_core_pwroff_dur_us = dur;
	kbdev->csf.mcu_core_pwroff_dur_count = pwroff;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "MCU Core Poweroff input update: 0x%.8x", pwroff);

	return pwroff;
}


int kbase_csf_firmware_early_init(struct kbase_device *kbdev)
{
	init_waitqueue_head(&kbdev->csf.event_wait);
	kbdev->csf.interrupt_received = false;
	kbdev->csf.fw_timeout_ms = CSF_FIRMWARE_TIMEOUT_MS;

	INIT_LIST_HEAD(&kbdev->csf.firmware_interfaces);
	INIT_LIST_HEAD(&kbdev->csf.firmware_config);
	INIT_LIST_HEAD(&kbdev->csf.firmware_timeline_metadata);
	INIT_LIST_HEAD(&kbdev->csf.firmware_trace_buffers.list);
	INIT_WORK(&kbdev->csf.firmware_reload_work,
		  kbase_csf_firmware_reload_worker);
	INIT_WORK(&kbdev->csf.fw_error_work, firmware_error_worker);

	mutex_init(&kbdev->csf.reg_lock);

	return 0;
}

int kbase_csf_firmware_init(struct kbase_device *kbdev)
{
	const struct firmware *firmware;
	const u32 magic = FIRMWARE_HEADER_MAGIC;
	u8 version_major, version_minor;
	u32 version_hash;
	u32 entry_end_offset;
	u32 entry_offset;
	int ret;

	lockdep_assert_held(&kbdev->fw_load_lock);

	if (WARN_ON((kbdev->as_free & MCU_AS_BITMASK) == 0))
		return -EINVAL;
	kbdev->as_free &= ~MCU_AS_BITMASK;

	ret = kbase_mmu_init(kbdev, &kbdev->csf.mcu_mmu, NULL,
		BASE_MEM_GROUP_DEFAULT);

	if (ret != 0) {
		/* Release the address space */
		kbdev->as_free |= MCU_AS_BITMASK;
		return ret;
	}

	kbdev->csf.gpu_idle_hysteresis_ms = FIRMWARE_IDLE_HYSTERESIS_TIME_MS;
	kbdev->csf.gpu_idle_dur_count = convert_dur_to_idle_count(
		kbdev, FIRMWARE_IDLE_HYSTERESIS_TIME_MS);

	kbdev->csf.mcu_core_pwroff_dur_us = DEFAULT_GLB_PWROFF_TIMEOUT_US;
	kbdev->csf.mcu_core_pwroff_dur_count = convert_dur_to_core_pwroff_count(
		kbdev, DEFAULT_GLB_PWROFF_TIMEOUT_US);

	ret = kbase_mcu_shared_interface_region_tracker_init(kbdev);
	if (ret != 0) {
		dev_err(kbdev->dev,
			"Failed to setup the rb tree for managing shared interface segment\n");
		goto error;
	}

	if (request_firmware(&firmware, fw_name, kbdev->dev) != 0) {
		dev_err(kbdev->dev,
				"Failed to load firmware image '%s'\n",
				fw_name);
		ret = -ENOENT;
		goto error;
	}

	if (firmware->size < FIRMWARE_HEADER_LENGTH) {
		dev_err(kbdev->dev, "Firmware too small\n");
		ret = -EINVAL;
		goto error;
	}

	if (memcmp(firmware->data, &magic, sizeof(magic)) != 0) {
		dev_err(kbdev->dev, "Incorrect firmware magic\n");
		ret = -EINVAL;
		goto error;
	}

	version_minor = firmware->data[4];
	version_major = firmware->data[5];

	if (version_major != FIRMWARE_HEADER_VERSION) {
		dev_err(kbdev->dev,
				"Firmware header version %d.%d not understood\n",
				version_major, version_minor);
		ret = -EINVAL;
		goto error;
	}

	memcpy(&version_hash, &firmware->data[8], sizeof(version_hash));

	dev_notice(kbdev->dev, "Loading Mali firmware 0x%x", version_hash);

	memcpy(&entry_end_offset, &firmware->data[0x10],
			sizeof(entry_end_offset));

	if (entry_end_offset > firmware->size) {
		dev_err(kbdev->dev, "Firmware image is truncated\n");
		ret = -EINVAL;
		goto error;
	}

	entry_offset = FIRMWARE_HEADER_LENGTH;
	while (entry_offset < entry_end_offset) {
		u32 header;
		unsigned int size;

		memcpy(&header, &firmware->data[entry_offset], sizeof(header));

		size = entry_size(header);

		ret = load_firmware_entry(kbdev, firmware, entry_offset,
				header);
		if (ret != 0) {
			dev_err(kbdev->dev, "Failed to load firmware image\n");
			goto error;
		}
		entry_offset += size;
	}

	if (!kbdev->csf.shared_interface) {
		dev_err(kbdev->dev, "Shared interface region not found\n");
		ret = -EINVAL;
		goto error;
	} else {
		ret = setup_shared_iface_static_region(kbdev);
		if (ret != 0) {
			dev_err(kbdev->dev, "Failed to insert a region for shared iface entry parsed from fw image\n");
			goto error;
		}
	}

	ret = kbase_csf_firmware_trace_buffers_init(kbdev);
	if (ret != 0) {
		dev_err(kbdev->dev, "Failed to initialize trace buffers\n");
		goto error;
	}

	/* Make sure L2 cache is powered up */
	kbase_pm_wait_for_l2_powered(kbdev);

	/* Load the MMU tables into the selected address space */
	load_mmu_tables(kbdev);

	boot_csf_firmware(kbdev);

	ret = parse_capabilities(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_doorbell_mapping_init(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_scheduler_init(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_setup_dummy_user_reg_page(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_timeout_init(kbdev);
	if (ret != 0)
		goto error;

	ret = global_init_on_boot(kbdev);
	if (ret != 0)
		goto error;

	ret = kbase_csf_firmware_cfg_init(kbdev);
	if (ret != 0)
		goto error;


	/* Firmware loaded successfully */
	release_firmware(firmware);
	KBASE_KTRACE_ADD(kbdev, FIRMWARE_BOOT, NULL,
			(((u64)version_hash) << 32) |
			(((u64)version_major) << 8) | version_minor);
	return 0;

error:
	kbase_csf_firmware_term(kbdev);
	release_firmware(firmware);
	return ret;
}

void kbase_csf_firmware_term(struct kbase_device *kbdev)
{
	unsigned long flags;
	int ret = 0;

	cancel_work_sync(&kbdev->csf.fw_error_work);

	ret = kbase_reset_gpu_wait(kbdev);

	WARN(ret, "failed to wait for GPU reset");

	kbase_csf_firmware_cfg_term(kbdev);

	kbase_csf_timeout_term(kbdev);

	kbase_csf_free_dummy_user_reg_page(kbdev);

	kbase_csf_scheduler_term(kbdev);

	kbase_csf_doorbell_mapping_term(kbdev);

	/* Explicitly trigger the disabling of MCU through the state machine and
	 * wait for its completion. It may not have been disabled yet due to the
	 * power policy.
	 */
	kbdev->pm.backend.mcu_desired = false;
	kbase_pm_wait_for_desired_state(kbdev);

	free_global_iface(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->csf.firmware_inited = false;
	if (WARN_ON(kbdev->pm.backend.mcu_state != KBASE_MCU_OFF)) {
		kbdev->pm.backend.mcu_state = KBASE_MCU_OFF;
		stop_csf_firmware(kbdev);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	unload_mmu_tables(kbdev);

	kbase_csf_firmware_trace_buffers_term(kbdev);

	while (!list_empty(&kbdev->csf.firmware_interfaces)) {
		struct kbase_csf_firmware_interface *interface;

		interface =
			list_first_entry(&kbdev->csf.firmware_interfaces,
					 struct kbase_csf_firmware_interface,
					 node);
		list_del(&interface->node);

		vunmap(interface->kernel_map);
		if (interface->flags & CSF_FIRMWARE_ENTRY_PROTECTED) {
			kbase_csf_protected_memory_free(kbdev, interface->pma,
				interface->num_pages);
		} else {
			kbase_mem_pool_free_pages(
				&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
				interface->num_pages, interface->phys,
				true, false);
		}

		kfree(interface->phys);
		kfree(interface);
	}

	while (!list_empty(&kbdev->csf.firmware_timeline_metadata)) {
		struct firmware_timeline_metadata *metadata;

		metadata = list_first_entry(
			&kbdev->csf.firmware_timeline_metadata,
			struct firmware_timeline_metadata,
			node);
		list_del(&metadata->node);

		kfree(metadata);
	}

#ifndef MALI_KBASE_BUILD
	mali_kutf_fw_utf_entry_cleanup(kbdev);
#endif

	/* This will also free up the region allocated for the shared interface
	 * entry parsed from the firmware image.
	 */
	kbase_mcu_shared_interface_region_tracker_term(kbdev);

	mutex_destroy(&kbdev->csf.reg_lock);

	kbase_mmu_term(kbdev, &kbdev->csf.mcu_mmu);

	/* Release the address space */
	kbdev->as_free |= MCU_AS_BITMASK;
}

void kbase_csf_firmware_enable_gpu_idle_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	const u32 glb_req =
		kbase_csf_firmware_global_input_read(global_iface, GLB_REQ);

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	/* The scheduler is assumed to only call the enable when its internal
	 * state indicates that the idle timer has previously been disabled. So
	 * on entry the expected field values are:
	 *   1. GLOBAL_INPUT_BLOCK.GLB_REQ.IDLE_ENABLE: 0
	 *   2. GLOBAL_OUTPUT_BLOCK.GLB_ACK.IDLE_ENABLE: 0, or, on 1 -> 0
	 */

	if (glb_req & GLB_REQ_IDLE_ENABLE_MASK)
		dev_err(kbdev->dev, "Incoherent scheduler state on REQ_IDLE_ENABLE!");

	kbase_csf_firmware_global_input(global_iface, GLB_IDLE_TIMER,
					kbdev->csf.gpu_idle_dur_count);

	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ,
				GLB_REQ_REQ_IDLE_ENABLE, GLB_REQ_IDLE_ENABLE_MASK);

	dev_dbg(kbdev->dev, "Enabling GPU idle timer with count-value: 0x%.8x",
		kbdev->csf.gpu_idle_dur_count);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
}

void kbase_csf_firmware_disable_gpu_idle_timer(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;

	kbase_csf_scheduler_spin_lock_assert_held(kbdev);

	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ,
					GLB_REQ_REQ_IDLE_DISABLE,
					GLB_REQ_IDLE_DISABLE_MASK);
	dev_dbg(kbdev->dev, "Sending request to disable gpu idle timer");

	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
}

void kbase_csf_firmware_ping(struct kbase_device *const kbdev)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_PING_MASK);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

int kbase_csf_firmware_ping_wait(struct kbase_device *const kbdev)
{
	kbase_csf_firmware_ping(kbdev);
	return wait_for_global_request(kbdev, GLB_REQ_PING_MASK);
}

int kbase_csf_firmware_set_timeout(struct kbase_device *const kbdev,
	u64 const timeout)
{
	const struct kbase_csf_global_iface *const global_iface =
		&kbdev->csf.global_iface;
	unsigned long flags;
	int err;

	/* The 'reg_lock' is also taken and is held till the update is not
	 * complete, to ensure the update of timeout value by multiple Users
	 * gets serialized.
	 */
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_timeout_global(global_iface, timeout);
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	err = wait_for_global_request(kbdev, GLB_REQ_CFG_PROGRESS_TIMER_MASK);
	mutex_unlock(&kbdev->csf.reg_lock);

	return err;
}

void kbase_csf_enter_protected_mode(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;
	int err;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_PROTM_ENTER_MASK);
	dev_dbg(kbdev->dev, "Sending request to enter protected mode");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	err = wait_for_global_request(kbdev, GLB_REQ_PROTM_ENTER_MASK);

	if (!err) {
		unsigned long irq_flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbdev->protected_mode = true;
		kbase_ipa_protection_mode_switch_event(kbdev);
		kbase_ipa_control_protm_entered(kbdev);

		kbase_csf_scheduler_spin_lock(kbdev, &irq_flags);
		kbase_hwcnt_backend_csf_protm_entered(&kbdev->hwcnt_gpu_iface);
		kbase_csf_scheduler_spin_unlock(kbdev, irq_flags);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
}

void kbase_csf_firmware_trigger_mcu_halt(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	set_global_request(global_iface, GLB_REQ_HALT_MASK);
	dev_dbg(kbdev->dev, "Sending request to HALT MCU");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

int kbase_csf_trigger_firmware_config_update(struct kbase_device *kbdev)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	unsigned long flags;
	int err = 0;

	/* Ensure GPU is powered-up until we complete config update.*/
	kbase_csf_scheduler_pm_active(kbdev);

	/* The 'reg_lock' is also taken and is held till the update is
	 * complete, to ensure the config update gets serialized.
	 */
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	set_global_request(global_iface, GLB_REQ_FIRMWARE_CONFIG_UPDATE_MASK);
	dev_dbg(kbdev->dev, "Sending request for FIRMWARE_CONFIG_UPDATE");
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	err = wait_for_global_request(kbdev,
				      GLB_REQ_FIRMWARE_CONFIG_UPDATE_MASK);
	mutex_unlock(&kbdev->csf.reg_lock);

	kbase_csf_scheduler_pm_idle(kbdev);
	return err;
}

/**
 * copy_grp_and_stm - Copy CS and/or group data
 *
 * @iface:                Global CSF interface provided by the firmware.
 * @group_data:           Pointer where to store all the group data
 *                        (sequentially).
 * @max_group_num:        The maximum number of groups to be read. Can be 0, in
 *                        which case group_data is unused.
 * @stream_data:          Pointer where to store all the CS data
 *                        (sequentially).
 * @max_total_stream_num: The maximum number of CSs to be read.
 *                        Can be 0, in which case stream_data is unused.
 *
 * Return: Total number of CSs, summed across all groups.
 */
static u32 copy_grp_and_stm(
	const struct kbase_csf_global_iface * const iface,
	struct basep_cs_group_control * const group_data,
	u32 max_group_num,
	struct basep_cs_stream_control * const stream_data,
	u32 max_total_stream_num)
{
	u32 i, total_stream_num = 0;

	if (WARN_ON((max_group_num > 0) && !group_data))
		max_group_num = 0;

	if (WARN_ON((max_total_stream_num > 0) && !stream_data))
		max_total_stream_num = 0;

	for (i = 0; i < iface->group_num; i++) {
		u32 j;

		if (i < max_group_num) {
			group_data[i].features = iface->groups[i].features;
			group_data[i].stream_num = iface->groups[i].stream_num;
			group_data[i].suspend_size =
				iface->groups[i].suspend_size;
		}
		for (j = 0; j < iface->groups[i].stream_num; j++) {
			if (total_stream_num < max_total_stream_num)
				stream_data[total_stream_num].features =
					iface->groups[i].streams[j].features;
			total_stream_num++;
		}
	}

	return total_stream_num;
}

u32 kbase_csf_firmware_get_glb_iface(
	struct kbase_device *kbdev,
	struct basep_cs_group_control *const group_data,
	u32 const max_group_num,
	struct basep_cs_stream_control *const stream_data,
	u32 const max_total_stream_num, u32 *const glb_version,
	u32 *const features, u32 *const group_num, u32 *const prfcnt_size,
	u32 *instr_features)
{
	const struct kbase_csf_global_iface * const iface =
		&kbdev->csf.global_iface;

	if (WARN_ON(!glb_version) || WARN_ON(!features) ||
	    WARN_ON(!group_num) || WARN_ON(!prfcnt_size) ||
	    WARN_ON(!instr_features))
		return 0;

	*glb_version = iface->version;
	*features = iface->features;
	*group_num = iface->group_num;
	*prfcnt_size = iface->prfcnt_size;
	*instr_features = iface->instr_features;

	return copy_grp_and_stm(iface, group_data, max_group_num,
		stream_data, max_total_stream_num);
}

const char *kbase_csf_firmware_get_timeline_metadata(
	struct kbase_device *kbdev, const char *name, size_t *size)
{
	struct firmware_timeline_metadata *metadata;

	list_for_each_entry(
		metadata, &kbdev->csf.firmware_timeline_metadata, node) {
		if (!strcmp(metadata->name, name)) {
			*size = metadata->size;
			return metadata->data;
		}
	}

	*size = 0;
	return NULL;
}

int kbase_csf_firmware_mcu_shared_mapping_init(
		struct kbase_device *kbdev,
		unsigned int num_pages,
		unsigned long cpu_map_properties,
		unsigned long gpu_map_properties,
		struct kbase_csf_mapping *csf_mapping)
{
	struct tagged_addr *phys;
	struct kbase_va_region *va_reg;
	struct page **page_list;
	void *cpu_addr;
	int i, ret = 0;
	pgprot_t cpu_map_prot = PAGE_KERNEL;
	unsigned long gpu_map_prot;

	if (cpu_map_properties & PROT_READ)
		cpu_map_prot = PAGE_KERNEL_RO;

	if (kbdev->system_coherency == COHERENCY_ACE) {
		gpu_map_prot =
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT_ACE);
	} else {
		gpu_map_prot =
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
		cpu_map_prot = pgprot_writecombine(cpu_map_prot);
	};

	phys = kmalloc_array(num_pages, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		goto out;

	page_list = kmalloc_array(num_pages, sizeof(*page_list), GFP_KERNEL);
	if (!page_list)
		goto page_list_alloc_error;

	ret = kbase_mem_pool_alloc_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
		num_pages, phys, false);
	if (ret <= 0)
		goto phys_mem_pool_alloc_error;

	for (i = 0; i < num_pages; i++)
		page_list[i] = as_page(phys[i]);

	cpu_addr = vmap(page_list, num_pages, VM_MAP, cpu_map_prot);
	if (!cpu_addr)
		goto vmap_error;

	va_reg = kbase_alloc_free_region(&kbdev->csf.shared_reg_rbtree, 0,
			num_pages, KBASE_REG_ZONE_MCU_SHARED);
	if (!va_reg)
		goto va_region_alloc_error;

	mutex_lock(&kbdev->csf.reg_lock);
	ret = kbase_add_va_region_rbtree(kbdev, va_reg, 0, num_pages, 1);
	va_reg->flags &= ~KBASE_REG_FREE;
	if (ret)
		goto va_region_add_error;
	mutex_unlock(&kbdev->csf.reg_lock);

	gpu_map_properties &= (KBASE_REG_GPU_RD | KBASE_REG_GPU_WR);
	gpu_map_properties |= gpu_map_prot;

	ret = kbase_mmu_insert_pages_no_flush(kbdev, &kbdev->csf.mcu_mmu,
			va_reg->start_pfn, &phys[0], num_pages,
			gpu_map_properties, KBASE_MEM_GROUP_CSF_FW);
	if (ret)
		goto mmu_insert_pages_error;

	kfree(page_list);
	csf_mapping->phys = phys;
	csf_mapping->cpu_addr = cpu_addr;
	csf_mapping->va_reg = va_reg;
	csf_mapping->num_pages = num_pages;

	return 0;

mmu_insert_pages_error:
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_remove_va_region(va_reg);
va_region_add_error:
	kbase_free_alloced_region(va_reg);
	mutex_unlock(&kbdev->csf.reg_lock);
va_region_alloc_error:
	vunmap(cpu_addr);
vmap_error:
	kbase_mem_pool_free_pages(
		&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
		num_pages, phys, false, false);

phys_mem_pool_alloc_error:
	kfree(page_list);
page_list_alloc_error:
	kfree(phys);
out:
	/* Zero-initialize the mapping to make sure that the termination
	 * function doesn't try to unmap or free random addresses.
	 */
	csf_mapping->phys = NULL;
	csf_mapping->cpu_addr = NULL;
	csf_mapping->va_reg = NULL;
	csf_mapping->num_pages = 0;

	return -ENOMEM;
}

void kbase_csf_firmware_mcu_shared_mapping_term(
		struct kbase_device *kbdev, struct kbase_csf_mapping *csf_mapping)
{
	if (csf_mapping->va_reg) {
		mutex_lock(&kbdev->csf.reg_lock);
		kbase_remove_va_region(csf_mapping->va_reg);
		kbase_free_alloced_region(csf_mapping->va_reg);
		mutex_unlock(&kbdev->csf.reg_lock);
	}

	if (csf_mapping->phys) {
		kbase_mem_pool_free_pages(
			&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW],
			csf_mapping->num_pages, csf_mapping->phys, false,
			false);
	}

	vunmap(csf_mapping->cpu_addr);
	kfree(csf_mapping->phys);
}
