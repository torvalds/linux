// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2023 Collabora ltd. */

#ifdef CONFIG_ARM_ARCH_TIMER
#include <asm/arch_timer.h>
#endif

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>

#include "panthor_device.h"
#include "panthor_fw.h"
#include "panthor_gem.h"
#include "panthor_gpu.h"
#include "panthor_mmu.h"
#include "panthor_regs.h"
#include "panthor_sched.h"

#define CSF_FW_NAME "mali_csffw.bin"

#define PING_INTERVAL_MS			12000
#define PROGRESS_TIMEOUT_CYCLES			(5ull * 500 * 1024 * 1024)
#define PROGRESS_TIMEOUT_SCALE_SHIFT		10
#define IDLE_HYSTERESIS_US			800
#define PWROFF_HYSTERESIS_US			10000

/**
 * struct panthor_fw_binary_hdr - Firmware binary header.
 */
struct panthor_fw_binary_hdr {
	/** @magic: Magic value to check binary validity. */
	u32 magic;
#define CSF_FW_BINARY_HEADER_MAGIC		0xc3f13a6e

	/** @minor: Minor FW version. */
	u8 minor;

	/** @major: Major FW version. */
	u8 major;
#define CSF_FW_BINARY_HEADER_MAJOR_MAX		0

	/** @padding1: MBZ. */
	u16 padding1;

	/** @version_hash: FW version hash. */
	u32 version_hash;

	/** @padding2: MBZ. */
	u32 padding2;

	/** @size: FW binary size. */
	u32 size;
};

/**
 * enum panthor_fw_binary_entry_type - Firmware binary entry type
 */
enum panthor_fw_binary_entry_type {
	/** @CSF_FW_BINARY_ENTRY_TYPE_IFACE: Host <-> FW interface. */
	CSF_FW_BINARY_ENTRY_TYPE_IFACE = 0,

	/** @CSF_FW_BINARY_ENTRY_TYPE_CONFIG: FW config. */
	CSF_FW_BINARY_ENTRY_TYPE_CONFIG = 1,

	/** @CSF_FW_BINARY_ENTRY_TYPE_FUTF_TEST: Unit-tests. */
	CSF_FW_BINARY_ENTRY_TYPE_FUTF_TEST = 2,

	/** @CSF_FW_BINARY_ENTRY_TYPE_TRACE_BUFFER: Trace buffer interface. */
	CSF_FW_BINARY_ENTRY_TYPE_TRACE_BUFFER = 3,

	/** @CSF_FW_BINARY_ENTRY_TYPE_TIMELINE_METADATA: Timeline metadata interface. */
	CSF_FW_BINARY_ENTRY_TYPE_TIMELINE_METADATA = 4,
};

#define CSF_FW_BINARY_ENTRY_TYPE(ehdr)					((ehdr) & 0xff)
#define CSF_FW_BINARY_ENTRY_SIZE(ehdr)					(((ehdr) >> 8) & 0xff)
#define CSF_FW_BINARY_ENTRY_UPDATE					BIT(30)
#define CSF_FW_BINARY_ENTRY_OPTIONAL					BIT(31)

#define CSF_FW_BINARY_IFACE_ENTRY_RD_RD					BIT(0)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_WR					BIT(1)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_EX					BIT(2)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_NONE			(0 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_CACHED			(1 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_UNCACHED_COHERENT	(2 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_CACHED_COHERENT		(3 << 3)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_MASK			GENMASK(4, 3)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_PROT				BIT(5)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_SHARED				BIT(30)
#define CSF_FW_BINARY_IFACE_ENTRY_RD_ZERO				BIT(31)

#define CSF_FW_BINARY_IFACE_ENTRY_RD_SUPPORTED_FLAGS			\
	(CSF_FW_BINARY_IFACE_ENTRY_RD_RD |				\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_WR |				\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_EX |				\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_MASK |			\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_PROT |				\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_SHARED  |				\
	 CSF_FW_BINARY_IFACE_ENTRY_RD_ZERO)

/**
 * struct panthor_fw_binary_section_entry_hdr - Describes a section of FW binary
 */
struct panthor_fw_binary_section_entry_hdr {
	/** @flags: Section flags. */
	u32 flags;

	/** @va: MCU virtual range to map this binary section to. */
	struct {
		/** @start: Start address. */
		u32 start;

		/** @end: End address. */
		u32 end;
	} va;

	/** @data: Data to initialize the FW section with. */
	struct {
		/** @start: Start offset in the FW binary. */
		u32 start;

		/** @end: End offset in the FW binary. */
		u32 end;
	} data;
};

/**
 * struct panthor_fw_binary_iter - Firmware binary iterator
 *
 * Used to parse a firmware binary.
 */
struct panthor_fw_binary_iter {
	/** @data: FW binary data. */
	const void *data;

	/** @size: FW binary size. */
	size_t size;

	/** @offset: Iterator offset. */
	size_t offset;
};

/**
 * struct panthor_fw_section - FW section
 */
struct panthor_fw_section {
	/** @node: Used to keep track of FW sections. */
	struct list_head node;

	/** @flags: Section flags, as encoded in the FW binary. */
	u32 flags;

	/** @mem: Section memory. */
	struct panthor_kernel_bo *mem;

	/**
	 * @name: Name of the section, as specified in the binary.
	 *
	 * Can be NULL.
	 */
	const char *name;

	/**
	 * @data: Initial data copied to the FW memory.
	 *
	 * We keep data around so we can reload sections after a reset.
	 */
	struct {
		/** @buf: Buffed used to store init data. */
		const void *buf;

		/** @size: Size of @buf in bytes. */
		size_t size;
	} data;
};

#define CSF_MCU_SHARED_REGION_START		0x04000000ULL
#define CSF_MCU_SHARED_REGION_SIZE		0x04000000ULL

#define MIN_CS_PER_CSG				8
#define MIN_CSGS				3
#define MAX_CSG_PRIO				0xf

#define CSF_IFACE_VERSION(major, minor, patch)	\
	(((major) << 24) | ((minor) << 16) | (patch))
#define CSF_IFACE_VERSION_MAJOR(v)		((v) >> 24)
#define CSF_IFACE_VERSION_MINOR(v)		(((v) >> 16) & 0xff)
#define CSF_IFACE_VERSION_PATCH(v)		((v) & 0xffff)

#define CSF_GROUP_CONTROL_OFFSET		0x1000
#define CSF_STREAM_CONTROL_OFFSET		0x40
#define CSF_UNPRESERVED_REG_COUNT		4

/**
 * struct panthor_fw_iface - FW interfaces
 */
struct panthor_fw_iface {
	/** @global: Global interface. */
	struct panthor_fw_global_iface global;

	/** @groups: Group slot interfaces. */
	struct panthor_fw_csg_iface groups[MAX_CSGS];

	/** @streams: Command stream slot interfaces. */
	struct panthor_fw_cs_iface streams[MAX_CSGS][MAX_CS_PER_CSG];
};

/**
 * struct panthor_fw - Firmware management
 */
struct panthor_fw {
	/** @vm: MCU VM. */
	struct panthor_vm *vm;

	/** @sections: List of FW sections. */
	struct list_head sections;

	/** @shared_section: The section containing the FW interfaces. */
	struct panthor_fw_section *shared_section;

	/** @iface: FW interfaces. */
	struct panthor_fw_iface iface;

	/** @watchdog: Collection of fields relating to the FW watchdog. */
	struct {
		/** @ping_work: Delayed work used to ping the FW. */
		struct delayed_work ping_work;
	} watchdog;

	/**
	 * @req_waitqueue: FW request waitqueue.
	 *
	 * Everytime a request is sent to a command stream group or the global
	 * interface, the caller will first busy wait for the request to be
	 * acknowledged, and then fallback to a sleeping wait.
	 *
	 * This wait queue is here to support the sleeping wait flavor.
	 */
	wait_queue_head_t req_waitqueue;

	/** @booted: True is the FW is booted */
	bool booted;

	/**
	 * @fast_reset: True if the post_reset logic can proceed with a fast reset.
	 *
	 * A fast reset is just a reset where the driver doesn't reload the FW sections.
	 *
	 * Any time the firmware is properly suspended, a fast reset can take place.
	 * On the other hand, if the halt operation failed, the driver will reload
	 * all sections to make sure we start from a fresh state.
	 */
	bool fast_reset;

	/** @irq: Job irq data. */
	struct panthor_irq irq;
};

struct panthor_vm *panthor_fw_vm(struct panthor_device *ptdev)
{
	return ptdev->fw->vm;
}

/**
 * panthor_fw_get_glb_iface() - Get the global interface
 * @ptdev: Device.
 *
 * Return: The global interface.
 */
struct panthor_fw_global_iface *
panthor_fw_get_glb_iface(struct panthor_device *ptdev)
{
	return &ptdev->fw->iface.global;
}

/**
 * panthor_fw_get_csg_iface() - Get a command stream group slot interface
 * @ptdev: Device.
 * @csg_slot: Index of the command stream group slot.
 *
 * Return: The command stream group slot interface.
 */
struct panthor_fw_csg_iface *
panthor_fw_get_csg_iface(struct panthor_device *ptdev, u32 csg_slot)
{
	if (drm_WARN_ON(&ptdev->base, csg_slot >= MAX_CSGS))
		return NULL;

	return &ptdev->fw->iface.groups[csg_slot];
}

/**
 * panthor_fw_get_cs_iface() - Get a command stream slot interface
 * @ptdev: Device.
 * @csg_slot: Index of the command stream group slot.
 * @cs_slot: Index of the command stream slot.
 *
 * Return: The command stream slot interface.
 */
struct panthor_fw_cs_iface *
panthor_fw_get_cs_iface(struct panthor_device *ptdev, u32 csg_slot, u32 cs_slot)
{
	if (drm_WARN_ON(&ptdev->base, csg_slot >= MAX_CSGS || cs_slot >= MAX_CS_PER_CSG))
		return NULL;

	return &ptdev->fw->iface.streams[csg_slot][cs_slot];
}

/**
 * panthor_fw_conv_timeout() - Convert a timeout into a cycle-count
 * @ptdev: Device.
 * @timeout_us: Timeout expressed in micro-seconds.
 *
 * The FW has two timer sources: the GPU counter or arch-timer. We need
 * to express timeouts in term of number of cycles and specify which
 * timer source should be used.
 *
 * Return: A value suitable for timeout fields in the global interface.
 */
static u32 panthor_fw_conv_timeout(struct panthor_device *ptdev, u32 timeout_us)
{
	bool use_cycle_counter = false;
	u32 timer_rate = 0;
	u64 mod_cycles;

#ifdef CONFIG_ARM_ARCH_TIMER
	timer_rate = arch_timer_get_cntfrq();
#endif

	if (!timer_rate) {
		use_cycle_counter = true;
		timer_rate = clk_get_rate(ptdev->clks.core);
	}

	if (drm_WARN_ON(&ptdev->base, !timer_rate)) {
		/* We couldn't get a valid clock rate, let's just pick the
		 * maximum value so the FW still handles the core
		 * power on/off requests.
		 */
		return GLB_TIMER_VAL(~0) |
		       GLB_TIMER_SOURCE_GPU_COUNTER;
	}

	mod_cycles = DIV_ROUND_UP_ULL((u64)timeout_us * timer_rate,
				      1000000ull << 10);
	if (drm_WARN_ON(&ptdev->base, mod_cycles > GLB_TIMER_VAL(~0)))
		mod_cycles = GLB_TIMER_VAL(~0);

	return GLB_TIMER_VAL(mod_cycles) |
	       (use_cycle_counter ? GLB_TIMER_SOURCE_GPU_COUNTER : 0);
}

static int panthor_fw_binary_iter_read(struct panthor_device *ptdev,
				       struct panthor_fw_binary_iter *iter,
				       void *out, size_t size)
{
	size_t new_offset = iter->offset + size;

	if (new_offset > iter->size || new_offset < iter->offset) {
		drm_err(&ptdev->base, "Firmware too small\n");
		return -EINVAL;
	}

	memcpy(out, iter->data + iter->offset, size);
	iter->offset = new_offset;
	return 0;
}

static int panthor_fw_binary_sub_iter_init(struct panthor_device *ptdev,
					   struct panthor_fw_binary_iter *iter,
					   struct panthor_fw_binary_iter *sub_iter,
					   size_t size)
{
	size_t new_offset = iter->offset + size;

	if (new_offset > iter->size || new_offset < iter->offset) {
		drm_err(&ptdev->base, "Firmware entry too long\n");
		return -EINVAL;
	}

	sub_iter->offset = 0;
	sub_iter->data = iter->data + iter->offset;
	sub_iter->size = size;
	iter->offset = new_offset;
	return 0;
}

static void panthor_fw_init_section_mem(struct panthor_device *ptdev,
					struct panthor_fw_section *section)
{
	bool was_mapped = !!section->mem->kmap;
	int ret;

	if (!section->data.size &&
	    !(section->flags & CSF_FW_BINARY_IFACE_ENTRY_RD_ZERO))
		return;

	ret = panthor_kernel_bo_vmap(section->mem);
	if (drm_WARN_ON(&ptdev->base, ret))
		return;

	memcpy(section->mem->kmap, section->data.buf, section->data.size);
	if (section->flags & CSF_FW_BINARY_IFACE_ENTRY_RD_ZERO) {
		memset(section->mem->kmap + section->data.size, 0,
		       panthor_kernel_bo_size(section->mem) - section->data.size);
	}

	if (!was_mapped)
		panthor_kernel_bo_vunmap(section->mem);
}

/**
 * panthor_fw_alloc_queue_iface_mem() - Allocate a ring-buffer interfaces.
 * @ptdev: Device.
 * @input: Pointer holding the input interface on success.
 * Should be ignored on failure.
 * @output: Pointer holding the output interface on success.
 * Should be ignored on failure.
 * @input_fw_va: Pointer holding the input interface FW VA on success.
 * Should be ignored on failure.
 * @output_fw_va: Pointer holding the output interface FW VA on success.
 * Should be ignored on failure.
 *
 * Allocates panthor_fw_ringbuf_{input,out}_iface interfaces. The input
 * interface is at offset 0, and the output interface at offset 4096.
 *
 * Return: A valid pointer in case of success, an ERR_PTR() otherwise.
 */
struct panthor_kernel_bo *
panthor_fw_alloc_queue_iface_mem(struct panthor_device *ptdev,
				 struct panthor_fw_ringbuf_input_iface **input,
				 const struct panthor_fw_ringbuf_output_iface **output,
				 u32 *input_fw_va, u32 *output_fw_va)
{
	struct panthor_kernel_bo *mem;
	int ret;

	mem = panthor_kernel_bo_create(ptdev, ptdev->fw->vm, SZ_8K,
				       DRM_PANTHOR_BO_NO_MMAP,
				       DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC |
				       DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED,
				       PANTHOR_VM_KERNEL_AUTO_VA);
	if (IS_ERR(mem))
		return mem;

	ret = panthor_kernel_bo_vmap(mem);
	if (ret) {
		panthor_kernel_bo_destroy(mem);
		return ERR_PTR(ret);
	}

	memset(mem->kmap, 0, panthor_kernel_bo_size(mem));
	*input = mem->kmap;
	*output = mem->kmap + SZ_4K;
	*input_fw_va = panthor_kernel_bo_gpuva(mem);
	*output_fw_va = *input_fw_va + SZ_4K;

	return mem;
}

/**
 * panthor_fw_alloc_suspend_buf_mem() - Allocate a suspend buffer for a command stream group.
 * @ptdev: Device.
 * @size: Size of the suspend buffer.
 *
 * Return: A valid pointer in case of success, an ERR_PTR() otherwise.
 */
struct panthor_kernel_bo *
panthor_fw_alloc_suspend_buf_mem(struct panthor_device *ptdev, size_t size)
{
	return panthor_kernel_bo_create(ptdev, panthor_fw_vm(ptdev), size,
					DRM_PANTHOR_BO_NO_MMAP,
					DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC,
					PANTHOR_VM_KERNEL_AUTO_VA);
}

static int panthor_fw_load_section_entry(struct panthor_device *ptdev,
					 const struct firmware *fw,
					 struct panthor_fw_binary_iter *iter,
					 u32 ehdr)
{
	ssize_t vm_pgsz = panthor_vm_page_size(ptdev->fw->vm);
	struct panthor_fw_binary_section_entry_hdr hdr;
	struct panthor_fw_section *section;
	u32 section_size;
	u32 name_len;
	int ret;

	ret = panthor_fw_binary_iter_read(ptdev, iter, &hdr, sizeof(hdr));
	if (ret)
		return ret;

	if (hdr.data.end < hdr.data.start) {
		drm_err(&ptdev->base, "Firmware corrupted, data.end < data.start (0x%x < 0x%x)\n",
			hdr.data.end, hdr.data.start);
		return -EINVAL;
	}

	if (hdr.va.end < hdr.va.start) {
		drm_err(&ptdev->base, "Firmware corrupted, hdr.va.end < hdr.va.start (0x%x < 0x%x)\n",
			hdr.va.end, hdr.va.start);
		return -EINVAL;
	}

	if (hdr.data.end > fw->size) {
		drm_err(&ptdev->base, "Firmware corrupted, file truncated? data_end=0x%x > fw size=0x%zx\n",
			hdr.data.end, fw->size);
		return -EINVAL;
	}

	if (!IS_ALIGNED(hdr.va.start, vm_pgsz) || !IS_ALIGNED(hdr.va.end, vm_pgsz)) {
		drm_err(&ptdev->base, "Firmware corrupted, virtual addresses not page aligned: 0x%x-0x%x\n",
			hdr.va.start, hdr.va.end);
		return -EINVAL;
	}

	if (hdr.flags & ~CSF_FW_BINARY_IFACE_ENTRY_RD_SUPPORTED_FLAGS) {
		drm_err(&ptdev->base, "Firmware contains interface with unsupported flags (0x%x)\n",
			hdr.flags);
		return -EINVAL;
	}

	if (hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_RD_PROT) {
		drm_warn(&ptdev->base,
			 "Firmware protected mode entry not be supported, ignoring");
		return 0;
	}

	if (hdr.va.start == CSF_MCU_SHARED_REGION_START &&
	    !(hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_RD_SHARED)) {
		drm_err(&ptdev->base,
			"Interface at 0x%llx must be shared", CSF_MCU_SHARED_REGION_START);
		return -EINVAL;
	}

	name_len = iter->size - iter->offset;

	section = drmm_kzalloc(&ptdev->base, sizeof(*section), GFP_KERNEL);
	if (!section)
		return -ENOMEM;

	list_add_tail(&section->node, &ptdev->fw->sections);
	section->flags = hdr.flags;
	section->data.size = hdr.data.end - hdr.data.start;

	if (section->data.size > 0) {
		void *data = drmm_kmalloc(&ptdev->base, section->data.size, GFP_KERNEL);

		if (!data)
			return -ENOMEM;

		memcpy(data, fw->data + hdr.data.start, section->data.size);
		section->data.buf = data;
	}

	if (name_len > 0) {
		char *name = drmm_kmalloc(&ptdev->base, name_len + 1, GFP_KERNEL);

		if (!name)
			return -ENOMEM;

		memcpy(name, iter->data + iter->offset, name_len);
		name[name_len] = '\0';
		section->name = name;
	}

	section_size = hdr.va.end - hdr.va.start;
	if (section_size) {
		u32 cache_mode = hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_MASK;
		struct panthor_gem_object *bo;
		u32 vm_map_flags = 0;
		struct sg_table *sgt;
		u64 va = hdr.va.start;

		if (!(hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_RD_WR))
			vm_map_flags |= DRM_PANTHOR_VM_BIND_OP_MAP_READONLY;

		if (!(hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_RD_EX))
			vm_map_flags |= DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC;

		/* TODO: CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_*_COHERENT are mapped to
		 * non-cacheable for now. We might want to introduce a new
		 * IOMMU_xxx flag (or abuse IOMMU_MMIO, which maps to device
		 * memory and is currently not used by our driver) for
		 * AS_MEMATTR_AARCH64_SHARED memory, so we can take benefit
		 * of IO-coherent systems.
		 */
		if (cache_mode != CSF_FW_BINARY_IFACE_ENTRY_RD_CACHE_MODE_CACHED)
			vm_map_flags |= DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED;

		section->mem = panthor_kernel_bo_create(ptdev, panthor_fw_vm(ptdev),
							section_size,
							DRM_PANTHOR_BO_NO_MMAP,
							vm_map_flags, va);
		if (IS_ERR(section->mem))
			return PTR_ERR(section->mem);

		if (drm_WARN_ON(&ptdev->base, section->mem->va_node.start != hdr.va.start))
			return -EINVAL;

		if (section->flags & CSF_FW_BINARY_IFACE_ENTRY_RD_SHARED) {
			ret = panthor_kernel_bo_vmap(section->mem);
			if (ret)
				return ret;
		}

		panthor_fw_init_section_mem(ptdev, section);

		bo = to_panthor_bo(section->mem->obj);
		sgt = drm_gem_shmem_get_pages_sgt(&bo->base);
		if (IS_ERR(sgt))
			return PTR_ERR(sgt);

		dma_sync_sgtable_for_device(ptdev->base.dev, sgt, DMA_TO_DEVICE);
	}

	if (hdr.va.start == CSF_MCU_SHARED_REGION_START)
		ptdev->fw->shared_section = section;

	return 0;
}

static void
panthor_reload_fw_sections(struct panthor_device *ptdev, bool full_reload)
{
	struct panthor_fw_section *section;

	list_for_each_entry(section, &ptdev->fw->sections, node) {
		struct sg_table *sgt;

		if (!full_reload && !(section->flags & CSF_FW_BINARY_IFACE_ENTRY_RD_WR))
			continue;

		panthor_fw_init_section_mem(ptdev, section);
		sgt = drm_gem_shmem_get_pages_sgt(&to_panthor_bo(section->mem->obj)->base);
		if (!drm_WARN_ON(&ptdev->base, IS_ERR_OR_NULL(sgt)))
			dma_sync_sgtable_for_device(ptdev->base.dev, sgt, DMA_TO_DEVICE);
	}
}

static int panthor_fw_load_entry(struct panthor_device *ptdev,
				 const struct firmware *fw,
				 struct panthor_fw_binary_iter *iter)
{
	struct panthor_fw_binary_iter eiter;
	u32 ehdr;
	int ret;

	ret = panthor_fw_binary_iter_read(ptdev, iter, &ehdr, sizeof(ehdr));
	if (ret)
		return ret;

	if ((iter->offset % sizeof(u32)) ||
	    (CSF_FW_BINARY_ENTRY_SIZE(ehdr) % sizeof(u32))) {
		drm_err(&ptdev->base, "Firmware entry isn't 32 bit aligned, offset=0x%x size=0x%x\n",
			(u32)(iter->offset - sizeof(u32)), CSF_FW_BINARY_ENTRY_SIZE(ehdr));
		return -EINVAL;
	}

	if (panthor_fw_binary_sub_iter_init(ptdev, iter, &eiter,
					    CSF_FW_BINARY_ENTRY_SIZE(ehdr) - sizeof(ehdr)))
		return -EINVAL;

	switch (CSF_FW_BINARY_ENTRY_TYPE(ehdr)) {
	case CSF_FW_BINARY_ENTRY_TYPE_IFACE:
		return panthor_fw_load_section_entry(ptdev, fw, &eiter, ehdr);

	/* FIXME: handle those entry types? */
	case CSF_FW_BINARY_ENTRY_TYPE_CONFIG:
	case CSF_FW_BINARY_ENTRY_TYPE_FUTF_TEST:
	case CSF_FW_BINARY_ENTRY_TYPE_TRACE_BUFFER:
	case CSF_FW_BINARY_ENTRY_TYPE_TIMELINE_METADATA:
		return 0;
	default:
		break;
	}

	if (ehdr & CSF_FW_BINARY_ENTRY_OPTIONAL)
		return 0;

	drm_err(&ptdev->base,
		"Unsupported non-optional entry type %u in firmware\n",
		CSF_FW_BINARY_ENTRY_TYPE(ehdr));
	return -EINVAL;
}

static int panthor_fw_load(struct panthor_device *ptdev)
{
	const struct firmware *fw = NULL;
	struct panthor_fw_binary_iter iter = {};
	struct panthor_fw_binary_hdr hdr;
	char fw_path[128];
	int ret;

	snprintf(fw_path, sizeof(fw_path), "arm/mali/arch%d.%d/%s",
		 (u32)GPU_ARCH_MAJOR(ptdev->gpu_info.gpu_id),
		 (u32)GPU_ARCH_MINOR(ptdev->gpu_info.gpu_id),
		 CSF_FW_NAME);

	ret = request_firmware(&fw, fw_path, ptdev->base.dev);
	if (ret) {
		drm_err(&ptdev->base, "Failed to load firmware image '%s'\n",
			CSF_FW_NAME);
		return ret;
	}

	iter.data = fw->data;
	iter.size = fw->size;
	ret = panthor_fw_binary_iter_read(ptdev, &iter, &hdr, sizeof(hdr));
	if (ret)
		goto out;

	if (hdr.magic != CSF_FW_BINARY_HEADER_MAGIC) {
		ret = -EINVAL;
		drm_err(&ptdev->base, "Invalid firmware magic\n");
		goto out;
	}

	if (hdr.major != CSF_FW_BINARY_HEADER_MAJOR_MAX) {
		ret = -EINVAL;
		drm_err(&ptdev->base, "Unsupported firmware binary header version %d.%d (expected %d.x)\n",
			hdr.major, hdr.minor, CSF_FW_BINARY_HEADER_MAJOR_MAX);
		goto out;
	}

	if (hdr.size > iter.size) {
		drm_err(&ptdev->base, "Firmware image is truncated\n");
		goto out;
	}

	iter.size = hdr.size;

	while (iter.offset < hdr.size) {
		ret = panthor_fw_load_entry(ptdev, fw, &iter);
		if (ret)
			goto out;
	}

	if (!ptdev->fw->shared_section) {
		drm_err(&ptdev->base, "Shared interface region not found\n");
		ret = -EINVAL;
		goto out;
	}

out:
	release_firmware(fw);
	return ret;
}

/**
 * iface_fw_to_cpu_addr() - Turn an MCU address into a CPU address
 * @ptdev: Device.
 * @mcu_va: MCU address.
 *
 * Return: NULL if the address is not part of the shared section, non-NULL otherwise.
 */
static void *iface_fw_to_cpu_addr(struct panthor_device *ptdev, u32 mcu_va)
{
	u64 shared_mem_start = panthor_kernel_bo_gpuva(ptdev->fw->shared_section->mem);
	u64 shared_mem_end = shared_mem_start +
			     panthor_kernel_bo_size(ptdev->fw->shared_section->mem);
	if (mcu_va < shared_mem_start || mcu_va >= shared_mem_end)
		return NULL;

	return ptdev->fw->shared_section->mem->kmap + (mcu_va - shared_mem_start);
}

static int panthor_init_cs_iface(struct panthor_device *ptdev,
				 unsigned int csg_idx, unsigned int cs_idx)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
	struct panthor_fw_csg_iface *csg_iface = panthor_fw_get_csg_iface(ptdev, csg_idx);
	struct panthor_fw_cs_iface *cs_iface = &ptdev->fw->iface.streams[csg_idx][cs_idx];
	u64 shared_section_sz = panthor_kernel_bo_size(ptdev->fw->shared_section->mem);
	u32 iface_offset = CSF_GROUP_CONTROL_OFFSET +
			   (csg_idx * glb_iface->control->group_stride) +
			   CSF_STREAM_CONTROL_OFFSET +
			   (cs_idx * csg_iface->control->stream_stride);
	struct panthor_fw_cs_iface *first_cs_iface =
		panthor_fw_get_cs_iface(ptdev, 0, 0);

	if (iface_offset + sizeof(*cs_iface) >= shared_section_sz)
		return -EINVAL;

	spin_lock_init(&cs_iface->lock);
	cs_iface->control = ptdev->fw->shared_section->mem->kmap + iface_offset;
	cs_iface->input = iface_fw_to_cpu_addr(ptdev, cs_iface->control->input_va);
	cs_iface->output = iface_fw_to_cpu_addr(ptdev, cs_iface->control->output_va);

	if (!cs_iface->input || !cs_iface->output) {
		drm_err(&ptdev->base, "Invalid stream control interface input/output VA");
		return -EINVAL;
	}

	if (cs_iface != first_cs_iface) {
		if (cs_iface->control->features != first_cs_iface->control->features) {
			drm_err(&ptdev->base, "Expecting identical CS slots");
			return -EINVAL;
		}
	} else {
		u32 reg_count = CS_FEATURES_WORK_REGS(cs_iface->control->features);

		ptdev->csif_info.cs_reg_count = reg_count;
		ptdev->csif_info.unpreserved_cs_reg_count = CSF_UNPRESERVED_REG_COUNT;
	}

	return 0;
}

static bool compare_csg(const struct panthor_fw_csg_control_iface *a,
			const struct panthor_fw_csg_control_iface *b)
{
	if (a->features != b->features)
		return false;
	if (a->suspend_size != b->suspend_size)
		return false;
	if (a->protm_suspend_size != b->protm_suspend_size)
		return false;
	if (a->stream_num != b->stream_num)
		return false;
	return true;
}

static int panthor_init_csg_iface(struct panthor_device *ptdev,
				  unsigned int csg_idx)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
	struct panthor_fw_csg_iface *csg_iface = &ptdev->fw->iface.groups[csg_idx];
	u64 shared_section_sz = panthor_kernel_bo_size(ptdev->fw->shared_section->mem);
	u32 iface_offset = CSF_GROUP_CONTROL_OFFSET + (csg_idx * glb_iface->control->group_stride);
	unsigned int i;

	if (iface_offset + sizeof(*csg_iface) >= shared_section_sz)
		return -EINVAL;

	spin_lock_init(&csg_iface->lock);
	csg_iface->control = ptdev->fw->shared_section->mem->kmap + iface_offset;
	csg_iface->input = iface_fw_to_cpu_addr(ptdev, csg_iface->control->input_va);
	csg_iface->output = iface_fw_to_cpu_addr(ptdev, csg_iface->control->output_va);

	if (csg_iface->control->stream_num < MIN_CS_PER_CSG ||
	    csg_iface->control->stream_num > MAX_CS_PER_CSG)
		return -EINVAL;

	if (!csg_iface->input || !csg_iface->output) {
		drm_err(&ptdev->base, "Invalid group control interface input/output VA");
		return -EINVAL;
	}

	if (csg_idx > 0) {
		struct panthor_fw_csg_iface *first_csg_iface =
			panthor_fw_get_csg_iface(ptdev, 0);

		if (!compare_csg(first_csg_iface->control, csg_iface->control)) {
			drm_err(&ptdev->base, "Expecting identical CSG slots");
			return -EINVAL;
		}
	}

	for (i = 0; i < csg_iface->control->stream_num; i++) {
		int ret = panthor_init_cs_iface(ptdev, csg_idx, i);

		if (ret)
			return ret;
	}

	return 0;
}

static u32 panthor_get_instr_features(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);

	if (glb_iface->control->version < CSF_IFACE_VERSION(1, 1, 0))
		return 0;

	return glb_iface->control->instr_features;
}

static int panthor_fw_init_ifaces(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = &ptdev->fw->iface.global;
	unsigned int i;

	if (!ptdev->fw->shared_section->mem->kmap)
		return -EINVAL;

	spin_lock_init(&glb_iface->lock);
	glb_iface->control = ptdev->fw->shared_section->mem->kmap;

	if (!glb_iface->control->version) {
		drm_err(&ptdev->base, "Firmware version is 0. Firmware may have failed to boot");
		return -EINVAL;
	}

	glb_iface->input = iface_fw_to_cpu_addr(ptdev, glb_iface->control->input_va);
	glb_iface->output = iface_fw_to_cpu_addr(ptdev, glb_iface->control->output_va);
	if (!glb_iface->input || !glb_iface->output) {
		drm_err(&ptdev->base, "Invalid global control interface input/output VA");
		return -EINVAL;
	}

	if (glb_iface->control->group_num > MAX_CSGS ||
	    glb_iface->control->group_num < MIN_CSGS) {
		drm_err(&ptdev->base, "Invalid number of control groups");
		return -EINVAL;
	}

	for (i = 0; i < glb_iface->control->group_num; i++) {
		int ret = panthor_init_csg_iface(ptdev, i);

		if (ret)
			return ret;
	}

	drm_info(&ptdev->base, "CSF FW v%d.%d.%d, Features %#x Instrumentation features %#x",
		 CSF_IFACE_VERSION_MAJOR(glb_iface->control->version),
		 CSF_IFACE_VERSION_MINOR(glb_iface->control->version),
		 CSF_IFACE_VERSION_PATCH(glb_iface->control->version),
		 glb_iface->control->features,
		 panthor_get_instr_features(ptdev));
	return 0;
}

static void panthor_fw_init_global_iface(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);

	/* Enable all cores. */
	glb_iface->input->core_en_mask = ptdev->gpu_info.shader_present;

	/* Setup timers. */
	glb_iface->input->poweroff_timer = panthor_fw_conv_timeout(ptdev, PWROFF_HYSTERESIS_US);
	glb_iface->input->progress_timer = PROGRESS_TIMEOUT_CYCLES >> PROGRESS_TIMEOUT_SCALE_SHIFT;
	glb_iface->input->idle_timer = panthor_fw_conv_timeout(ptdev, IDLE_HYSTERESIS_US);

	/* Enable interrupts we care about. */
	glb_iface->input->ack_irq_mask = GLB_CFG_ALLOC_EN |
					 GLB_PING |
					 GLB_CFG_PROGRESS_TIMER |
					 GLB_CFG_POWEROFF_TIMER |
					 GLB_IDLE_EN |
					 GLB_IDLE;

	panthor_fw_update_reqs(glb_iface, req, GLB_IDLE_EN, GLB_IDLE_EN);
	panthor_fw_toggle_reqs(glb_iface, req, ack,
			       GLB_CFG_ALLOC_EN |
			       GLB_CFG_POWEROFF_TIMER |
			       GLB_CFG_PROGRESS_TIMER);

	gpu_write(ptdev, CSF_DOORBELL(CSF_GLB_DOORBELL_ID), 1);

	/* Kick the watchdog. */
	mod_delayed_work(ptdev->reset.wq, &ptdev->fw->watchdog.ping_work,
			 msecs_to_jiffies(PING_INTERVAL_MS));
}

static void panthor_job_irq_handler(struct panthor_device *ptdev, u32 status)
{
	if (!ptdev->fw->booted && (status & JOB_INT_GLOBAL_IF))
		ptdev->fw->booted = true;

	wake_up_all(&ptdev->fw->req_waitqueue);

	/* If the FW is not booted, don't process IRQs, just flag the FW as booted. */
	if (!ptdev->fw->booted)
		return;

	panthor_sched_report_fw_events(ptdev, status);
}
PANTHOR_IRQ_HANDLER(job, JOB, panthor_job_irq_handler);

static int panthor_fw_start(struct panthor_device *ptdev)
{
	bool timedout = false;

	ptdev->fw->booted = false;
	panthor_job_irq_resume(&ptdev->fw->irq, ~0);
	gpu_write(ptdev, MCU_CONTROL, MCU_CONTROL_AUTO);

	if (!wait_event_timeout(ptdev->fw->req_waitqueue,
				ptdev->fw->booted,
				msecs_to_jiffies(1000))) {
		if (!ptdev->fw->booted &&
		    !(gpu_read(ptdev, JOB_INT_STAT) & JOB_INT_GLOBAL_IF))
			timedout = true;
	}

	if (timedout) {
		static const char * const status_str[] = {
			[MCU_STATUS_DISABLED] = "disabled",
			[MCU_STATUS_ENABLED] = "enabled",
			[MCU_STATUS_HALT] = "halt",
			[MCU_STATUS_FATAL] = "fatal",
		};
		u32 status = gpu_read(ptdev, MCU_STATUS);

		drm_err(&ptdev->base, "Failed to boot MCU (status=%s)",
			status < ARRAY_SIZE(status_str) ? status_str[status] : "unknown");
		return -ETIMEDOUT;
	}

	return 0;
}

static void panthor_fw_stop(struct panthor_device *ptdev)
{
	u32 status;

	gpu_write(ptdev, MCU_CONTROL, MCU_CONTROL_DISABLE);
	if (readl_poll_timeout(ptdev->iomem + MCU_STATUS, status,
			       status == MCU_STATUS_DISABLED, 10, 100000))
		drm_err(&ptdev->base, "Failed to stop MCU");
}

/**
 * panthor_fw_pre_reset() - Call before a reset.
 * @ptdev: Device.
 * @on_hang: true if the reset was triggered on a GPU hang.
 *
 * If the reset is not triggered on a hang, we try to gracefully halt the
 * MCU, so we can do a fast-reset when panthor_fw_post_reset() is called.
 */
void panthor_fw_pre_reset(struct panthor_device *ptdev, bool on_hang)
{
	/* Make sure we won't be woken up by a ping. */
	cancel_delayed_work_sync(&ptdev->fw->watchdog.ping_work);

	ptdev->fw->fast_reset = false;

	if (!on_hang) {
		struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
		u32 status;

		panthor_fw_update_reqs(glb_iface, req, GLB_HALT, GLB_HALT);
		gpu_write(ptdev, CSF_DOORBELL(CSF_GLB_DOORBELL_ID), 1);
		if (!readl_poll_timeout(ptdev->iomem + MCU_STATUS, status,
					status == MCU_STATUS_HALT, 10, 100000) &&
		    glb_iface->output->halt_status == PANTHOR_FW_HALT_OK) {
			ptdev->fw->fast_reset = true;
		} else {
			drm_warn(&ptdev->base, "Failed to cleanly suspend MCU");
		}

		/* The FW detects 0 -> 1 transitions. Make sure we reset
		 * the HALT bit before the FW is rebooted.
		 */
		panthor_fw_update_reqs(glb_iface, req, 0, GLB_HALT);
	}

	panthor_job_irq_suspend(&ptdev->fw->irq);
}

/**
 * panthor_fw_post_reset() - Call after a reset.
 * @ptdev: Device.
 *
 * Start the FW. If this is not a fast reset, all FW sections are reloaded to
 * make sure we can recover from a memory corruption.
 */
int panthor_fw_post_reset(struct panthor_device *ptdev)
{
	int ret;

	/* Make the MCU VM active. */
	ret = panthor_vm_active(ptdev->fw->vm);
	if (ret)
		return ret;

	/* If this is a fast reset, try to start the MCU without reloading
	 * the FW sections. If it fails, go for a full reset.
	 */
	if (ptdev->fw->fast_reset) {
		ret = panthor_fw_start(ptdev);
		if (!ret)
			goto out;

		/* Forcibly reset the MCU and force a slow reset, so we get a
		 * fresh boot on the next panthor_fw_start() call.
		 */
		panthor_fw_stop(ptdev);
		ptdev->fw->fast_reset = false;
		drm_err(&ptdev->base, "FW fast reset failed, trying a slow reset");

		ret = panthor_vm_flush_all(ptdev->fw->vm);
		if (ret) {
			drm_err(&ptdev->base, "FW slow reset failed (couldn't flush FW's AS l2cache)");
			return ret;
		}
	}

	/* Reload all sections, including RO ones. We're not supposed
	 * to end up here anyway, let's just assume the overhead of
	 * reloading everything is acceptable.
	 */
	panthor_reload_fw_sections(ptdev, true);

	ret = panthor_fw_start(ptdev);
	if (ret) {
		drm_err(&ptdev->base, "FW slow reset failed (couldn't start the FW )");
		return ret;
	}

out:
	/* We must re-initialize the global interface even on fast-reset. */
	panthor_fw_init_global_iface(ptdev);
	return 0;
}

/**
 * panthor_fw_unplug() - Called when the device is unplugged.
 * @ptdev: Device.
 *
 * This function must make sure all pending operations are flushed before
 * will release device resources, thus preventing any interaction with
 * the HW.
 *
 * If there is still FW-related work running after this function returns,
 * they must use drm_dev_{enter,exit}() and skip any HW access when
 * drm_dev_enter() returns false.
 */
void panthor_fw_unplug(struct panthor_device *ptdev)
{
	struct panthor_fw_section *section;

	cancel_delayed_work_sync(&ptdev->fw->watchdog.ping_work);

	/* Make sure the IRQ handler can be called after that point. */
	if (ptdev->fw->irq.irq)
		panthor_job_irq_suspend(&ptdev->fw->irq);

	panthor_fw_stop(ptdev);

	list_for_each_entry(section, &ptdev->fw->sections, node)
		panthor_kernel_bo_destroy(section->mem);

	/* We intentionally don't call panthor_vm_idle() and let
	 * panthor_mmu_unplug() release the AS we acquired with
	 * panthor_vm_active() so we don't have to track the VM active/idle
	 * state to keep the active_refcnt balanced.
	 */
	panthor_vm_put(ptdev->fw->vm);
	ptdev->fw->vm = NULL;

	panthor_gpu_power_off(ptdev, L2, ptdev->gpu_info.l2_present, 20000);
}

/**
 * panthor_fw_wait_acks() - Wait for requests to be acknowledged by the FW.
 * @req_ptr: Pointer to the req register.
 * @ack_ptr: Pointer to the ack register.
 * @wq: Wait queue to use for the sleeping wait.
 * @req_mask: Mask of requests to wait for.
 * @acked: Pointer to field that's updated with the acked requests.
 * If the function returns 0, *acked == req_mask.
 * @timeout_ms: Timeout expressed in milliseconds.
 *
 * Return: 0 on success, -ETIMEDOUT otherwise.
 */
static int panthor_fw_wait_acks(const u32 *req_ptr, const u32 *ack_ptr,
				wait_queue_head_t *wq,
				u32 req_mask, u32 *acked,
				u32 timeout_ms)
{
	u32 ack, req = READ_ONCE(*req_ptr) & req_mask;
	int ret;

	/* Busy wait for a few µsecs before falling back to a sleeping wait. */
	*acked = req_mask;
	ret = read_poll_timeout_atomic(READ_ONCE, ack,
				       (ack & req_mask) == req,
				       0, 10, 0,
				       *ack_ptr);
	if (!ret)
		return 0;

	if (wait_event_timeout(*wq, (READ_ONCE(*ack_ptr) & req_mask) == req,
			       msecs_to_jiffies(timeout_ms)))
		return 0;

	/* Check one last time, in case we were not woken up for some reason. */
	ack = READ_ONCE(*ack_ptr);
	if ((ack & req_mask) == req)
		return 0;

	*acked = ~(req ^ ack) & req_mask;
	return -ETIMEDOUT;
}

/**
 * panthor_fw_glb_wait_acks() - Wait for global requests to be acknowledged.
 * @ptdev: Device.
 * @req_mask: Mask of requests to wait for.
 * @acked: Pointer to field that's updated with the acked requests.
 * If the function returns 0, *acked == req_mask.
 * @timeout_ms: Timeout expressed in milliseconds.
 *
 * Return: 0 on success, -ETIMEDOUT otherwise.
 */
int panthor_fw_glb_wait_acks(struct panthor_device *ptdev,
			     u32 req_mask, u32 *acked,
			     u32 timeout_ms)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);

	/* GLB_HALT doesn't get acked through the FW interface. */
	if (drm_WARN_ON(&ptdev->base, req_mask & (~GLB_REQ_MASK | GLB_HALT)))
		return -EINVAL;

	return panthor_fw_wait_acks(&glb_iface->input->req,
				    &glb_iface->output->ack,
				    &ptdev->fw->req_waitqueue,
				    req_mask, acked, timeout_ms);
}

/**
 * panthor_fw_csg_wait_acks() - Wait for command stream group requests to be acknowledged.
 * @ptdev: Device.
 * @csg_slot: CSG slot ID.
 * @req_mask: Mask of requests to wait for.
 * @acked: Pointer to field that's updated with the acked requests.
 * If the function returns 0, *acked == req_mask.
 * @timeout_ms: Timeout expressed in milliseconds.
 *
 * Return: 0 on success, -ETIMEDOUT otherwise.
 */
int panthor_fw_csg_wait_acks(struct panthor_device *ptdev, u32 csg_slot,
			     u32 req_mask, u32 *acked, u32 timeout_ms)
{
	struct panthor_fw_csg_iface *csg_iface = panthor_fw_get_csg_iface(ptdev, csg_slot);
	int ret;

	if (drm_WARN_ON(&ptdev->base, req_mask & ~CSG_REQ_MASK))
		return -EINVAL;

	ret = panthor_fw_wait_acks(&csg_iface->input->req,
				   &csg_iface->output->ack,
				   &ptdev->fw->req_waitqueue,
				   req_mask, acked, timeout_ms);

	/*
	 * Check that all bits in the state field were updated, if any mismatch
	 * then clear all bits in the state field. This allows code to do
	 * (acked & CSG_STATE_MASK) and get the right value.
	 */

	if ((*acked & CSG_STATE_MASK) != CSG_STATE_MASK)
		*acked &= ~CSG_STATE_MASK;

	return ret;
}

/**
 * panthor_fw_ring_csg_doorbells() - Ring command stream group doorbells.
 * @ptdev: Device.
 * @csg_mask: Bitmask encoding the command stream group doorbells to ring.
 *
 * This function is toggling bits in the doorbell_req and ringing the
 * global doorbell. It doesn't require a user doorbell to be attached to
 * the group.
 */
void panthor_fw_ring_csg_doorbells(struct panthor_device *ptdev, u32 csg_mask)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);

	panthor_fw_toggle_reqs(glb_iface, doorbell_req, doorbell_ack, csg_mask);
	gpu_write(ptdev, CSF_DOORBELL(CSF_GLB_DOORBELL_ID), 1);
}

static void panthor_fw_ping_work(struct work_struct *work)
{
	struct panthor_fw *fw = container_of(work, struct panthor_fw, watchdog.ping_work.work);
	struct panthor_device *ptdev = fw->irq.ptdev;
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
	u32 acked;
	int ret;

	if (panthor_device_reset_is_pending(ptdev))
		return;

	panthor_fw_toggle_reqs(glb_iface, req, ack, GLB_PING);
	gpu_write(ptdev, CSF_DOORBELL(CSF_GLB_DOORBELL_ID), 1);

	ret = panthor_fw_glb_wait_acks(ptdev, GLB_PING, &acked, 100);
	if (ret) {
		panthor_device_schedule_reset(ptdev);
		drm_err(&ptdev->base, "FW ping timeout, scheduling a reset");
	} else {
		mod_delayed_work(ptdev->reset.wq, &fw->watchdog.ping_work,
				 msecs_to_jiffies(PING_INTERVAL_MS));
	}
}

/**
 * panthor_fw_init() - Initialize FW related data.
 * @ptdev: Device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_fw_init(struct panthor_device *ptdev)
{
	struct panthor_fw *fw;
	int ret, irq;

	fw = drmm_kzalloc(&ptdev->base, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	ptdev->fw = fw;
	init_waitqueue_head(&fw->req_waitqueue);
	INIT_LIST_HEAD(&fw->sections);
	INIT_DELAYED_WORK(&fw->watchdog.ping_work, panthor_fw_ping_work);

	irq = platform_get_irq_byname(to_platform_device(ptdev->base.dev), "job");
	if (irq <= 0)
		return -ENODEV;

	ret = panthor_request_job_irq(ptdev, &fw->irq, irq, 0);
	if (ret) {
		drm_err(&ptdev->base, "failed to request job irq");
		return ret;
	}

	ret = panthor_gpu_l2_power_on(ptdev);
	if (ret)
		return ret;

	fw->vm = panthor_vm_create(ptdev, true,
				   0, SZ_4G,
				   CSF_MCU_SHARED_REGION_START,
				   CSF_MCU_SHARED_REGION_SIZE);
	if (IS_ERR(fw->vm)) {
		ret = PTR_ERR(fw->vm);
		fw->vm = NULL;
		goto err_unplug_fw;
	}

	ret = panthor_fw_load(ptdev);
	if (ret)
		goto err_unplug_fw;

	ret = panthor_vm_active(fw->vm);
	if (ret)
		goto err_unplug_fw;

	ret = panthor_fw_start(ptdev);
	if (ret)
		goto err_unplug_fw;

	ret = panthor_fw_init_ifaces(ptdev);
	if (ret)
		goto err_unplug_fw;

	panthor_fw_init_global_iface(ptdev);
	return 0;

err_unplug_fw:
	panthor_fw_unplug(ptdev);
	return ret;
}

MODULE_FIRMWARE("arm/mali/arch10.8/mali_csffw.bin");
