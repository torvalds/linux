/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_H
#define PVR_FW_H

#include "pvr_fw_info.h"
#include "pvr_fw_trace.h"
#include "pvr_gem.h"

#include <drm/drm_mm.h>

#include <linux/types.h>

/* Forward declarations from "pvr_device.h". */
struct pvr_device;
struct pvr_file;

/* Forward declaration from "pvr_vm.h". */
struct pvr_vm_context;

#define ROGUE_FWIF_FWCCB_NUMCMDS_LOG2 5

#define ROGUE_FWIF_KCCB_NUMCMDS_LOG2_DEFAULT 7

/**
 * struct pvr_fw_object - container for firmware memory allocations
 */
struct pvr_fw_object {
	/** @ref_count: FW object reference counter. */
	struct kref ref_count;

	/** @gem: GEM object backing the FW object. */
	struct pvr_gem_object *gem;

	/**
	 * @fw_mm_node: Node representing mapping in FW address space. @pvr_obj->lock must
	 *              be held when writing.
	 */
	struct drm_mm_node fw_mm_node;

	/**
	 * @fw_addr_offset: Virtual address offset of firmware mapping. Only
	 *                  valid if @flags has %PVR_GEM_OBJECT_FLAGS_FW_MAPPED
	 *                  set.
	 */
	u32 fw_addr_offset;

	/**
	 * @init: Initialisation callback. Will be called on object creation and FW hard reset.
	 *        Object will have been zeroed before this is called.
	 */
	void (*init)(void *cpu_ptr, void *priv);

	/** @init_priv: Private data for initialisation callback. */
	void *init_priv;

	/** @node: Node for firmware object list. */
	struct list_head node;
};

/**
 * struct pvr_fw_defs - FW processor function table and static definitions
 */
struct pvr_fw_defs {
	/**
	 * @init:
	 *
	 * FW processor specific initialisation.
	 * @pvr_dev: Target PowerVR device.
	 *
	 * This function must call pvr_fw_heap_calculate() to initialise the firmware heap for this
	 * FW processor.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * 0 on success, or
	 *  * Any appropriate error on failure.
	 */
	int (*init)(struct pvr_device *pvr_dev);

	/**
	 * @fini:
	 *
	 * FW processor specific finalisation.
	 * @pvr_dev: Target PowerVR device.
	 *
	 * This function is optional.
	 */
	void (*fini)(struct pvr_device *pvr_dev);

	/**
	 * @fw_process:
	 *
	 * Load and process firmware image.
	 * @pvr_dev: Target PowerVR device.
	 * @fw: Pointer to firmware image.
	 * @fw_code_ptr: Pointer to firmware code section.
	 * @fw_data_ptr: Pointer to firmware data section.
	 * @fw_core_code_ptr: Pointer to firmware core code section. May be %NULL.
	 * @fw_core_data_ptr: Pointer to firmware core data section. May be %NULL.
	 * @core_code_alloc_size: Total allocation size of core code section.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * 0 on success, or
	 *  * Any appropriate error on failure.
	 */
	int (*fw_process)(struct pvr_device *pvr_dev, const u8 *fw,
			  u8 *fw_code_ptr, u8 *fw_data_ptr, u8 *fw_core_code_ptr,
			  u8 *fw_core_data_ptr, u32 core_code_alloc_size);

	/**
	 * @vm_map:
	 *
	 * Map FW object into FW processor address space.
	 * @pvr_dev: Target PowerVR device.
	 * @fw_obj: FW object to map.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * 0 on success, or
	 *  * Any appropriate error on failure.
	 */
	int (*vm_map)(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj);

	/**
	 * @vm_unmap:
	 *
	 * Unmap FW object from FW processor address space.
	 * @pvr_dev: Target PowerVR device.
	 * @fw_obj: FW object to map.
	 *
	 * This function is mandatory.
	 */
	void (*vm_unmap)(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj);

	/**
	 * @get_fw_addr_with_offset:
	 *
	 * Called to get address of object in firmware address space, with offset.
	 * @fw_obj: Pointer to object.
	 * @offset: Desired offset from start of object.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * Address in firmware address space.
	 */
	u32 (*get_fw_addr_with_offset)(struct pvr_fw_object *fw_obj, u32 offset);

	/**
	 * @wrapper_init:
	 *
	 * Called to initialise FW wrapper.
	 * @pvr_dev: Target PowerVR device.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * 0 on success.
	 *  * Any appropriate error on failure.
	 */
	int (*wrapper_init)(struct pvr_device *pvr_dev);

	/**
	 * @has_fixed_data_addr:
	 *
	 * Called to check if firmware fixed data must be loaded at the address given by the
	 * firmware layout table.
	 *
	 * This function is mandatory.
	 *
	 * Returns:
	 *  * %true if firmware fixed data must be loaded at the address given by the firmware
	 *    layout table.
	 *  * %false otherwise.
	 */
	bool (*has_fixed_data_addr)(void);

	/**
	 * @irq: FW Interrupt information.
	 *
	 * Those are processor dependent, and should be initialized by the
	 * processor backend in pvr_fw_funcs::init().
	 */
	struct {
		/** @enable_reg: FW interrupt enable register. */
		u32 enable_reg;

		/** @status_reg: FW interrupt status register. */
		u32 status_reg;

		/**
		 * @clear_reg: FW interrupt clear register.
		 *
		 * If @status_reg == @clear_reg, we clear by write a bit to zero,
		 * otherwise we clear by writing a bit to one.
		 */
		u32 clear_reg;

		/** @event_mask: Bitmask of events to listen for. */
		u32 event_mask;

		/** @clear_mask: Value to write to the clear_reg in order to clear FW IRQs. */
		u32 clear_mask;
	} irq;
};

/**
 * struct pvr_fw_mem - FW memory allocations
 */
struct pvr_fw_mem {
	/** @code_obj: Object representing firmware code. */
	struct pvr_fw_object *code_obj;

	/** @data_obj: Object representing firmware data. */
	struct pvr_fw_object *data_obj;

	/**
	 * @core_code_obj: Object representing firmware core code. May be
	 *                 %NULL if firmware does not contain this section.
	 */
	struct pvr_fw_object *core_code_obj;

	/**
	 * @core_data_obj: Object representing firmware core data. May be
	 *                 %NULL if firmware does not contain this section.
	 */
	struct pvr_fw_object *core_data_obj;

	/** @code: Driver-side copy of firmware code. */
	u8 *code;

	/** @data: Driver-side copy of firmware data. */
	u8 *data;

	/**
	 * @core_code: Driver-side copy of firmware core code. May be %NULL if firmware does not
	 *             contain this section.
	 */
	u8 *core_code;

	/**
	 * @core_data: Driver-side copy of firmware core data. May be %NULL if firmware does not
	 *             contain this section.
	 */
	u8 *core_data;

	/** @code_alloc_size: Allocation size of firmware code section. */
	u32 code_alloc_size;

	/** @data_alloc_size: Allocation size of firmware data section. */
	u32 data_alloc_size;

	/** @core_code_alloc_size: Allocation size of firmware core code section. */
	u32 core_code_alloc_size;

	/** @core_data_alloc_size: Allocation size of firmware core data section. */
	u32 core_data_alloc_size;

	/**
	 * @fwif_connection_ctl_obj: Object representing FWIF connection control
	 *                           structure.
	 */
	struct pvr_fw_object *fwif_connection_ctl_obj;

	/** @osinit_obj: Object representing FW OSINIT structure. */
	struct pvr_fw_object *osinit_obj;

	/** @sysinit_obj: Object representing FW SYSINIT structure. */
	struct pvr_fw_object *sysinit_obj;

	/** @osdata_obj: Object representing FW OSDATA structure. */
	struct pvr_fw_object *osdata_obj;

	/** @hwrinfobuf_obj: Object representing FW hwrinfobuf structure. */
	struct pvr_fw_object *hwrinfobuf_obj;

	/** @sysdata_obj: Object representing FW SYSDATA structure. */
	struct pvr_fw_object *sysdata_obj;

	/** @power_sync_obj: Object representing power sync state. */
	struct pvr_fw_object *power_sync_obj;

	/** @fault_page_obj: Object representing FW fault page. */
	struct pvr_fw_object *fault_page_obj;

	/** @gpu_util_fwcb_obj: Object representing FW GPU utilisation control structure. */
	struct pvr_fw_object *gpu_util_fwcb_obj;

	/** @runtime_cfg_obj: Object representing FW runtime config structure. */
	struct pvr_fw_object *runtime_cfg_obj;

	/** @mmucache_sync_obj: Object used as the sync parameter in an MMU cache operation. */
	struct pvr_fw_object *mmucache_sync_obj;
};

struct pvr_fw_device {
	/** @firmware: Handle to the firmware loaded into the device. */
	const struct firmware *firmware;

	/** @header: Pointer to firmware header. */
	const struct pvr_fw_info_header *header;

	/** @layout_entries: Pointer to firmware layout. */
	const struct pvr_fw_layout_entry *layout_entries;

	/** @mem: Structure containing objects representing firmware memory allocations. */
	struct pvr_fw_mem mem;

	/** @booted: %true if the firmware has been booted, %false otherwise. */
	bool booted;

	/**
	 * @processor_type: FW processor type for this device. Must be one of
	 *                  %PVR_FW_PROCESSOR_TYPE_*.
	 */
	u16 processor_type;

	/** @funcs: Function table for the FW processor used by this device. */
	const struct pvr_fw_defs *defs;

	/** @processor_data: Pointer to data specific to FW processor. */
	union {
		/** @mips_data: Pointer to MIPS-specific data. */
		struct pvr_fw_mips_data *mips_data;
	} processor_data;

	/** @fw_heap_info: Firmware heap information. */
	struct {
		/** @gpu_addr: Base address of firmware heap in GPU address space. */
		u64 gpu_addr;

		/** @size: Size of main area of heap. */
		u32 size;

		/** @offset_mask: Mask for offsets within FW heap. */
		u32 offset_mask;

		/** @raw_size: Raw size of heap, including reserved areas. */
		u32 raw_size;

		/** @log2_size: Log2 of raw size of heap. */
		u32 log2_size;

		/** @config_offset: Offset of config area within heap. */
		u32 config_offset;

		/** @reserved_size: Size of reserved area in heap. */
		u32 reserved_size;
	} fw_heap_info;

	/** @fw_mm: Firmware address space allocator. */
	struct drm_mm fw_mm;

	/** @fw_mm_lock: Lock protecting access to &fw_mm. */
	spinlock_t fw_mm_lock;

	/** @fw_mm_base: Base address of address space managed by @fw_mm. */
	u64 fw_mm_base;

	/**
	 * @fwif_connection_ctl: Pointer to CPU mapping of FWIF connection
	 *                       control structure.
	 */
	struct rogue_fwif_connection_ctl *fwif_connection_ctl;

	/** @fwif_sysinit: Pointer to CPU mapping of FW SYSINIT structure. */
	struct rogue_fwif_sysinit *fwif_sysinit;

	/** @fwif_sysdata: Pointer to CPU mapping of FW SYSDATA structure. */
	struct rogue_fwif_sysdata *fwif_sysdata;

	/** @fwif_osinit: Pointer to CPU mapping of FW OSINIT structure. */
	struct rogue_fwif_osinit *fwif_osinit;

	/** @fwif_osdata: Pointer to CPU mapping of FW OSDATA structure. */
	struct rogue_fwif_osdata *fwif_osdata;

	/** @power_sync: Pointer to CPU mapping of power sync state. */
	u32 *power_sync;

	/** @hwrinfobuf: Pointer to CPU mapping of FW HWR info buffer. */
	struct rogue_fwif_hwrinfobuf *hwrinfobuf;

	/** @fw_trace: Device firmware trace buffer state. */
	struct pvr_fw_trace fw_trace;

	/** @fw_objs: Structure tracking FW objects. */
	struct {
		/** @list: Head of FW object list. */
		struct list_head list;

		/** @lock: Lock protecting access to FW object list. */
		struct mutex lock;
	} fw_objs;
};

#define pvr_fw_irq_read_reg(pvr_dev, name) \
	pvr_cr_read32((pvr_dev), (pvr_dev)->fw_dev.defs->irq.name ## _reg)

#define pvr_fw_irq_write_reg(pvr_dev, name, value) \
	pvr_cr_write32((pvr_dev), (pvr_dev)->fw_dev.defs->irq.name ## _reg, value)

#define pvr_fw_irq_pending(pvr_dev) \
	(pvr_fw_irq_read_reg(pvr_dev, status) & (pvr_dev)->fw_dev.defs->irq.event_mask)

#define pvr_fw_irq_clear(pvr_dev) \
	pvr_fw_irq_write_reg(pvr_dev, clear, (pvr_dev)->fw_dev.defs->irq.clear_mask)

#define pvr_fw_irq_enable(pvr_dev) \
	pvr_fw_irq_write_reg(pvr_dev, enable, (pvr_dev)->fw_dev.defs->irq.event_mask)

#define pvr_fw_irq_disable(pvr_dev) \
	pvr_fw_irq_write_reg(pvr_dev, enable, 0)

extern const struct pvr_fw_defs pvr_fw_defs_meta;
extern const struct pvr_fw_defs pvr_fw_defs_mips;

int pvr_fw_validate_init_device_info(struct pvr_device *pvr_dev);
int pvr_fw_init(struct pvr_device *pvr_dev);
void pvr_fw_fini(struct pvr_device *pvr_dev);

int pvr_wait_for_fw_boot(struct pvr_device *pvr_dev);

int
pvr_fw_hard_reset(struct pvr_device *pvr_dev);

void pvr_fw_mts_schedule(struct pvr_device *pvr_dev, u32 val);

void
pvr_fw_heap_info_init(struct pvr_device *pvr_dev, u32 log2_size, u32 reserved_size);

const struct pvr_fw_layout_entry *
pvr_fw_find_layout_entry(struct pvr_device *pvr_dev, enum pvr_fw_section_id id);
int
pvr_fw_find_mmu_segment(struct pvr_device *pvr_dev, u32 addr, u32 size, void *fw_code_ptr,
			void *fw_data_ptr, void *fw_core_code_ptr, void *fw_core_data_ptr,
			void **host_addr_out);

int
pvr_fw_structure_cleanup(struct pvr_device *pvr_dev, u32 type, struct pvr_fw_object *fw_obj,
			 u32 offset);

int pvr_fw_object_create(struct pvr_device *pvr_dev, size_t size, u64 flags,
			 void (*init)(void *cpu_ptr, void *priv), void *init_priv,
			 struct pvr_fw_object **pvr_obj_out);

void *pvr_fw_object_create_and_map(struct pvr_device *pvr_dev, size_t size, u64 flags,
				   void (*init)(void *cpu_ptr, void *priv),
				   void *init_priv, struct pvr_fw_object **pvr_obj_out);

void *
pvr_fw_object_create_and_map_offset(struct pvr_device *pvr_dev, u32 dev_offset, size_t size,
				    u64 flags, void (*init)(void *cpu_ptr, void *priv),
				    void *init_priv, struct pvr_fw_object **pvr_obj_out);

static __always_inline void *
pvr_fw_object_vmap(struct pvr_fw_object *fw_obj)
{
	return pvr_gem_object_vmap(fw_obj->gem);
}

static __always_inline void
pvr_fw_object_vunmap(struct pvr_fw_object *fw_obj)
{
	pvr_gem_object_vunmap(fw_obj->gem);
}

void pvr_fw_object_destroy(struct pvr_fw_object *fw_obj);

static __always_inline void
pvr_fw_object_unmap_and_destroy(struct pvr_fw_object *fw_obj)
{
	pvr_fw_object_vunmap(fw_obj);
	pvr_fw_object_destroy(fw_obj);
}

/**
 * pvr_fw_object_get_dma_addr() - Get DMA address for given offset in firmware
 * object.
 * @fw_obj: Pointer to object to lookup address in.
 * @offset: Offset within object to lookup address at.
 * @dma_addr_out: Pointer to location to store DMA address.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if object is not currently backed, or if @offset is out of valid
 *    range for this object.
 */
static __always_inline int
pvr_fw_object_get_dma_addr(struct pvr_fw_object *fw_obj, u32 offset, dma_addr_t *dma_addr_out)
{
	return pvr_gem_get_dma_addr(fw_obj->gem, offset, dma_addr_out);
}

void pvr_fw_object_get_fw_addr_offset(struct pvr_fw_object *fw_obj, u32 offset, u32 *fw_addr_out);

static __always_inline void
pvr_fw_object_get_fw_addr(struct pvr_fw_object *fw_obj, u32 *fw_addr_out)
{
	pvr_fw_object_get_fw_addr_offset(fw_obj, 0, fw_addr_out);
}

#endif /* PVR_FW_H */
