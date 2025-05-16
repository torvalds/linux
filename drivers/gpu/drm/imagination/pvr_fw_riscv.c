// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright (c) 2024 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_fw_info.h"
#include "pvr_fw_mips.h"
#include "pvr_gem.h"
#include "pvr_rogue_cr_defs.h"
#include "pvr_rogue_riscv.h"
#include "pvr_vm.h"

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/ktime.h>
#include <linux/types.h>

#define ROGUE_FW_HEAP_RISCV_SHIFT 25 /* 32 MB */
#define ROGUE_FW_HEAP_RISCV_SIZE (1u << ROGUE_FW_HEAP_RISCV_SHIFT)

static int
pvr_riscv_wrapper_init(struct pvr_device *pvr_dev)
{
	const u64 common_opts =
		((u64)(ROGUE_FW_HEAP_RISCV_SIZE >> FWCORE_ADDR_REMAP_CONFIG0_SIZE_ALIGNSHIFT)
		 << ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG0_SIZE_SHIFT) |
		((u64)MMU_CONTEXT_MAPPING_FWPRIV
		 << FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT);

	u64 code_addr = pvr_fw_obj_get_gpu_addr(pvr_dev->fw_dev.mem.code_obj);
	u64 data_addr = pvr_fw_obj_get_gpu_addr(pvr_dev->fw_dev.mem.data_obj);

	/* This condition allows us to OR the addresses into the register directly. */
	static_assert(ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG1_DEVVADDR_SHIFT ==
		      ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG1_DEVVADDR_ALIGNSHIFT);

	WARN_ON(code_addr & ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG1_DEVVADDR_CLRMSK);
	WARN_ON(data_addr & ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG1_DEVVADDR_CLRMSK);

	pvr_cr_write64(pvr_dev, ROGUE_RISCVFW_REGION_REMAP_CR(BOOTLDR_CODE),
		       code_addr | common_opts | ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG0_FETCH_EN_EN);

	pvr_cr_write64(pvr_dev, ROGUE_RISCVFW_REGION_REMAP_CR(BOOTLDR_DATA),
		       data_addr | common_opts |
			       ROGUE_CR_FWCORE_ADDR_REMAP_CONFIG0_LOAD_STORE_EN_EN);

	/* Garten IDLE bit controlled by RISC-V. */
	pvr_cr_write64(pvr_dev, ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG,
		       ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META);

	return 0;
}

struct rogue_riscv_fw_boot_data {
	u64 coremem_code_dev_vaddr;
	u64 coremem_data_dev_vaddr;
	u32 coremem_code_fw_addr;
	u32 coremem_data_fw_addr;
	u32 coremem_code_size;
	u32 coremem_data_size;
	u32 flags;
	u32 reserved;
};

static int
pvr_riscv_fw_process(struct pvr_device *pvr_dev, const u8 *fw,
		     u8 *fw_code_ptr, u8 *fw_data_ptr, u8 *fw_core_code_ptr, u8 *fw_core_data_ptr,
		     u32 core_code_alloc_size)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mem *fw_mem = &fw_dev->mem;
	struct rogue_riscv_fw_boot_data *boot_data;
	int err;

	err = pvr_fw_process_elf_command_stream(pvr_dev, fw, fw_code_ptr, fw_data_ptr,
						fw_core_code_ptr, fw_core_data_ptr);
	if (err)
		goto err_out;

	boot_data = (struct rogue_riscv_fw_boot_data *)fw_data_ptr;

	if (fw_mem->core_code_obj) {
		boot_data->coremem_code_dev_vaddr = pvr_fw_obj_get_gpu_addr(fw_mem->core_code_obj);
		pvr_fw_object_get_fw_addr(fw_mem->core_code_obj, &boot_data->coremem_code_fw_addr);
		boot_data->coremem_code_size = pvr_fw_obj_get_object_size(fw_mem->core_code_obj);
	}

	if (fw_mem->core_data_obj) {
		boot_data->coremem_data_dev_vaddr = pvr_fw_obj_get_gpu_addr(fw_mem->core_data_obj);
		pvr_fw_object_get_fw_addr(fw_mem->core_data_obj, &boot_data->coremem_data_fw_addr);
		boot_data->coremem_data_size = pvr_fw_obj_get_object_size(fw_mem->core_data_obj);
	}

	return 0;

err_out:
	return err;
}

static int
pvr_riscv_init(struct pvr_device *pvr_dev)
{
	pvr_fw_heap_info_init(pvr_dev, ROGUE_FW_HEAP_RISCV_SHIFT, 0);

	return 0;
}

static u32
pvr_riscv_get_fw_addr_with_offset(struct pvr_fw_object *fw_obj, u32 offset)
{
	u32 fw_addr = fw_obj->fw_addr_offset + offset;

	/* RISC-V cacheability is determined by address. */
	if (fw_obj->gem->flags & PVR_BO_FW_FLAGS_DEVICE_UNCACHED)
		fw_addr |= ROGUE_RISCVFW_REGION_BASE(SHARED_UNCACHED_DATA);
	else
		fw_addr |= ROGUE_RISCVFW_REGION_BASE(SHARED_CACHED_DATA);

	return fw_addr;
}

static int
pvr_riscv_vm_map(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;

	return pvr_vm_map(pvr_dev->kernel_vm_ctx, pvr_obj, 0, fw_obj->fw_mm_node.start,
			  pvr_gem_object_size(pvr_obj));
}

static void
pvr_riscv_vm_unmap(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;

	pvr_vm_unmap_obj(pvr_dev->kernel_vm_ctx, pvr_obj,
			 fw_obj->fw_mm_node.start, fw_obj->fw_mm_node.size);
}

static bool
pvr_riscv_irq_pending(struct pvr_device *pvr_dev)
{
	return pvr_cr_read32(pvr_dev, ROGUE_CR_IRQ_OS0_EVENT_STATUS) &
	       ROGUE_CR_IRQ_OS0_EVENT_STATUS_SOURCE_EN;
}

static void
pvr_riscv_irq_clear(struct pvr_device *pvr_dev)
{
	pvr_cr_write32(pvr_dev, ROGUE_CR_IRQ_OS0_EVENT_CLEAR,
		       ROGUE_CR_IRQ_OS0_EVENT_CLEAR_SOURCE_EN);
}

const struct pvr_fw_defs pvr_fw_defs_riscv = {
	.init = pvr_riscv_init,
	.fw_process = pvr_riscv_fw_process,
	.vm_map = pvr_riscv_vm_map,
	.vm_unmap = pvr_riscv_vm_unmap,
	.get_fw_addr_with_offset = pvr_riscv_get_fw_addr_with_offset,
	.wrapper_init = pvr_riscv_wrapper_init,
	.irq_pending = pvr_riscv_irq_pending,
	.irq_clear = pvr_riscv_irq_clear,
	.has_fixed_data_addr = false,
};
