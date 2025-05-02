// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_fw_mips.h"
#include "pvr_gem.h"
#include "pvr_rogue_mips.h"
#include "pvr_vm_mips.h"

#include <linux/err.h>
#include <linux/types.h>

#define ROGUE_FW_HEAP_MIPS_BASE 0xC0000000
#define ROGUE_FW_HEAP_MIPS_SHIFT 24 /* 16 MB */
#define ROGUE_FW_HEAP_MIPS_RESERVED_SIZE SZ_1M

static int
pvr_mips_init(struct pvr_device *pvr_dev)
{
	pvr_fw_heap_info_init(pvr_dev, ROGUE_FW_HEAP_MIPS_SHIFT, ROGUE_FW_HEAP_MIPS_RESERVED_SIZE);

	return pvr_vm_mips_init(pvr_dev);
}

static void
pvr_mips_fini(struct pvr_device *pvr_dev)
{
	pvr_vm_mips_fini(pvr_dev);
}

static int
pvr_mips_fw_process(struct pvr_device *pvr_dev, const u8 *fw,
		    u8 *fw_code_ptr, u8 *fw_data_ptr, u8 *fw_core_code_ptr, u8 *fw_core_data_ptr,
		    u32 core_code_alloc_size)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mips_data *mips_data = fw_dev->processor_data.mips_data;
	const struct pvr_fw_layout_entry *boot_code_entry;
	const struct pvr_fw_layout_entry *boot_data_entry;
	const struct pvr_fw_layout_entry *exception_code_entry;
	const struct pvr_fw_layout_entry *stack_entry;
	struct rogue_mipsfw_boot_data *boot_data;
	dma_addr_t dma_addr;
	int err;

	err = pvr_fw_process_elf_command_stream(pvr_dev, fw, fw_code_ptr, fw_data_ptr,
						fw_core_code_ptr, fw_core_data_ptr);
	if (err)
		return err;

	boot_code_entry = pvr_fw_find_layout_entry(pvr_dev, MIPS_BOOT_CODE);
	boot_data_entry = pvr_fw_find_layout_entry(pvr_dev, MIPS_BOOT_DATA);
	exception_code_entry = pvr_fw_find_layout_entry(pvr_dev, MIPS_EXCEPTIONS_CODE);
	if (!boot_code_entry || !boot_data_entry || !exception_code_entry)
		return -EINVAL;

	WARN_ON(pvr_gem_get_dma_addr(fw_dev->mem.code_obj->gem, boot_code_entry->alloc_offset,
				     &mips_data->boot_code_dma_addr));
	WARN_ON(pvr_gem_get_dma_addr(fw_dev->mem.data_obj->gem, boot_data_entry->alloc_offset,
				     &mips_data->boot_data_dma_addr));
	WARN_ON(pvr_gem_get_dma_addr(fw_dev->mem.code_obj->gem,
				     exception_code_entry->alloc_offset,
				     &mips_data->exception_code_dma_addr));

	stack_entry = pvr_fw_find_layout_entry(pvr_dev, MIPS_STACK);
	if (!stack_entry)
		return -EINVAL;

	boot_data = (struct rogue_mipsfw_boot_data *)(fw_data_ptr + boot_data_entry->alloc_offset +
						      ROGUE_MIPSFW_BOOTLDR_CONF_OFFSET);

	WARN_ON(pvr_fw_object_get_dma_addr(fw_dev->mem.data_obj, stack_entry->alloc_offset,
					   &dma_addr));
	boot_data->stack_phys_addr = dma_addr;

	boot_data->reg_base = pvr_dev->regs_resource->start;

	for (u32 page_nr = 0; page_nr < ARRAY_SIZE(boot_data->pt_phys_addr); page_nr++) {
		/* Firmware expects 4k pages, but host page size might be different. */
		u32 src_page_nr = (page_nr * ROGUE_MIPSFW_PAGE_SIZE_4K) >> PAGE_SHIFT;
		u32 page_offset = (page_nr * ROGUE_MIPSFW_PAGE_SIZE_4K) & ~PAGE_MASK;

		boot_data->pt_phys_addr[page_nr] = mips_data->pt_dma_addr[src_page_nr] +
						   page_offset;
	}

	boot_data->pt_log2_page_size = ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K;
	boot_data->pt_num_pages = ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES;
	boot_data->reserved1 = 0;
	boot_data->reserved2 = 0;

	return 0;
}

static int
pvr_mips_wrapper_init(struct pvr_device *pvr_dev)
{
	struct pvr_fw_mips_data *mips_data = pvr_dev->fw_dev.processor_data.mips_data;
	const u64 remap_settings = ROGUE_MIPSFW_BOOT_REMAP_LOG2_SEGMENT_SIZE;
	u32 phys_bus_width;

	int err = PVR_FEATURE_VALUE(pvr_dev, phys_bus_width, &phys_bus_width);

	if (WARN_ON(err))
		return err;

	/* Currently MIPS FW only supported with physical bus width > 32 bits. */
	if (WARN_ON(phys_bus_width <= 32))
		return -EINVAL;

	pvr_cr_write32(pvr_dev, ROGUE_CR_MIPS_WRAPPER_CONFIG,
		       (ROGUE_MIPSFW_REGISTERS_VIRTUAL_BASE >>
			ROGUE_MIPSFW_WRAPPER_CONFIG_REGBANK_ADDR_ALIGN) |
		       ROGUE_CR_MIPS_WRAPPER_CONFIG_BOOT_ISA_MODE_MICROMIPS);

	/* Configure remap for boot code, boot data and exceptions code areas. */
	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG1,
		       ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN |
		       ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG1_MODE_ENABLE_EN);
	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG2,
		       (mips_data->boot_code_dma_addr &
			~ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG2_ADDR_OUT_CLRMSK) | remap_settings);

	if (PVR_HAS_QUIRK(pvr_dev, 63553)) {
		/*
		 * WA always required on 36 bit cores, to avoid continuous unmapped memory accesses
		 * to address 0x0.
		 */
		WARN_ON(phys_bus_width != 36);

		pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG1,
			       ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG1_MODE_ENABLE_EN);
		pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG2,
			       (mips_data->boot_code_dma_addr &
				~ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG2_ADDR_OUT_CLRMSK) |
			       remap_settings);
	}

	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG1,
		       ROGUE_MIPSFW_DATA_REMAP_PHYS_ADDR_IN |
		       ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG1_MODE_ENABLE_EN);
	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG2,
		       (mips_data->boot_data_dma_addr &
			~ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG2_ADDR_OUT_CLRMSK) | remap_settings);

	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG1,
		       ROGUE_MIPSFW_CODE_REMAP_PHYS_ADDR_IN |
		       ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG1_MODE_ENABLE_EN);
	pvr_cr_write64(pvr_dev, ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG2,
		       (mips_data->exception_code_dma_addr &
			~ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG2_ADDR_OUT_CLRMSK) | remap_settings);

	/* Garten IDLE bit controlled by MIPS. */
	pvr_cr_write64(pvr_dev, ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG,
		       ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META);

	/* Turn on the EJTAG probe. */
	pvr_cr_write32(pvr_dev, ROGUE_CR_MIPS_DEBUG_CONFIG, 0);

	return 0;
}

static u32
pvr_mips_get_fw_addr_with_offset(struct pvr_fw_object *fw_obj, u32 offset)
{
	struct pvr_device *pvr_dev = to_pvr_device(gem_from_pvr_gem(fw_obj->gem)->dev);

	/* MIPS cacheability is determined by page table. */
	return ((fw_obj->fw_addr_offset + offset) & pvr_dev->fw_dev.fw_heap_info.offset_mask) |
	       ROGUE_FW_HEAP_MIPS_BASE;
}

static bool
pvr_mips_irq_pending(struct pvr_device *pvr_dev)
{
	return pvr_cr_read32(pvr_dev, ROGUE_CR_MIPS_WRAPPER_IRQ_STATUS) &
	       ROGUE_CR_MIPS_WRAPPER_IRQ_STATUS_EVENT_EN;
}

static void
pvr_mips_irq_clear(struct pvr_device *pvr_dev)
{
	pvr_cr_write32(pvr_dev, ROGUE_CR_MIPS_WRAPPER_IRQ_CLEAR,
		       ROGUE_CR_MIPS_WRAPPER_IRQ_CLEAR_EVENT_EN);
}

const struct pvr_fw_defs pvr_fw_defs_mips = {
	.init = pvr_mips_init,
	.fini = pvr_mips_fini,
	.fw_process = pvr_mips_fw_process,
	.vm_map = pvr_vm_mips_map,
	.vm_unmap = pvr_vm_mips_unmap,
	.get_fw_addr_with_offset = pvr_mips_get_fw_addr_with_offset,
	.wrapper_init = pvr_mips_wrapper_init,
	.irq_pending = pvr_mips_irq_pending,
	.irq_clear = pvr_mips_irq_clear,
	.has_fixed_data_addr = true,
};
