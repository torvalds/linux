// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_fw_info.h"
#include "pvr_fw_meta.h"
#include "pvr_gem.h"
#include "pvr_rogue_cr_defs.h"
#include "pvr_rogue_meta.h"
#include "pvr_vm.h"

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/ktime.h>
#include <linux/types.h>

#define ROGUE_FW_HEAP_META_SHIFT 25 /* 32 MB */

#define POLL_TIMEOUT_USEC 1000000

/**
 * pvr_meta_cr_read32() - Read a META register via the Slave Port
 * @pvr_dev: Device pointer.
 * @reg_addr: Address of register to read.
 * @reg_value_out: Pointer to location to store register value.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_cr_poll_reg32().
 */
int
pvr_meta_cr_read32(struct pvr_device *pvr_dev, u32 reg_addr, u32 *reg_value_out)
{
	int err;

	/* Wait for Slave Port to be Ready. */
	err = pvr_cr_poll_reg32(pvr_dev, ROGUE_CR_META_SP_MSLVCTRL1,
				ROGUE_CR_META_SP_MSLVCTRL1_READY_EN |
					ROGUE_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
				ROGUE_CR_META_SP_MSLVCTRL1_READY_EN |
					ROGUE_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
				POLL_TIMEOUT_USEC);
	if (err)
		return err;

	/* Issue a Read. */
	pvr_cr_write32(pvr_dev, ROGUE_CR_META_SP_MSLVCTRL0,
		       reg_addr | ROGUE_CR_META_SP_MSLVCTRL0_RD_EN);
	(void)pvr_cr_read32(pvr_dev, ROGUE_CR_META_SP_MSLVCTRL0); /* Fence write. */

	/* Wait for Slave Port to be Ready. */
	err = pvr_cr_poll_reg32(pvr_dev, ROGUE_CR_META_SP_MSLVCTRL1,
				ROGUE_CR_META_SP_MSLVCTRL1_READY_EN |
					ROGUE_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
				ROGUE_CR_META_SP_MSLVCTRL1_READY_EN |
					ROGUE_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
				POLL_TIMEOUT_USEC);
	if (err)
		return err;

	*reg_value_out = pvr_cr_read32(pvr_dev, ROGUE_CR_META_SP_MSLVDATAX);

	return 0;
}

static int
pvr_meta_wrapper_init(struct pvr_device *pvr_dev)
{
	u64 garten_config;

	/* Configure META to Master boot. */
	pvr_cr_write64(pvr_dev, ROGUE_CR_META_BOOT, ROGUE_CR_META_BOOT_MODE_EN);

	/* Set Garten IDLE to META idle and Set the Garten Wrapper BIF Fence address. */

	/* Garten IDLE bit controlled by META. */
	garten_config = ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META;

	/* The fence addr is set during the fw init sequence. */

	/* Set PC = 0 for fences. */
	garten_config &=
		ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_PC_BASE_CLRMSK;
	garten_config |=
		(u64)MMU_CONTEXT_MAPPING_FWPRIV
		<< ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_PC_BASE_SHIFT;

	/* Set SLC DM=META. */
	garten_config |= ((u64)ROGUE_FW_SEGMMU_META_BIFDM_ID)
			 << ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_DM_SHIFT;

	pvr_cr_write64(pvr_dev, ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG, garten_config);

	return 0;
}

static __always_inline void
add_boot_arg(u32 **boot_conf, u32 param, u32 data)
{
	*(*boot_conf)++ = param;
	*(*boot_conf)++ = data;
}

static int
meta_ldr_cmd_loadmem(struct drm_device *drm_dev, const u8 *fw,
		     struct rogue_meta_ldr_l1_data_blk *l1_data, u32 coremem_size, u8 *fw_code_ptr,
		     u8 *fw_data_ptr, u8 *fw_core_code_ptr, u8 *fw_core_data_ptr, const u32 fw_size)
{
	struct rogue_meta_ldr_l2_data_blk *l2_block =
		(struct rogue_meta_ldr_l2_data_blk *)(fw +
						      l1_data->cmd_data[1]);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	u32 offset = l1_data->cmd_data[0];
	u32 data_size;
	void *write_addr;
	int err;

	/* Verify header is within bounds. */
	if (((u8 *)l2_block - fw) >= fw_size || ((u8 *)(l2_block + 1) - fw) >= fw_size)
		return -EINVAL;

	data_size = l2_block->length - 6 /* L2 Tag length and checksum */;

	/* Verify data is within bounds. */
	if (((u8 *)l2_block->block_data - fw) >= fw_size ||
	    ((((u8 *)l2_block->block_data) + data_size) - fw) >= fw_size)
		return -EINVAL;

	if (!ROGUE_META_IS_COREMEM_CODE(offset, coremem_size) &&
	    !ROGUE_META_IS_COREMEM_DATA(offset, coremem_size)) {
		/* Global range is aliased to local range */
		offset &= ~META_MEM_GLOBAL_RANGE_BIT;
	}

	err = pvr_fw_find_mmu_segment(pvr_dev, offset, data_size, fw_code_ptr, fw_data_ptr,
				      fw_core_code_ptr, fw_core_data_ptr, &write_addr);
	if (err) {
		drm_err(drm_dev,
			"Addr 0x%x (size: %d) not found in any firmware segment",
			offset, data_size);
		return err;
	}

	memcpy(write_addr, l2_block->block_data, data_size);

	return 0;
}

static int
meta_ldr_cmd_zeromem(struct drm_device *drm_dev,
		     struct rogue_meta_ldr_l1_data_blk *l1_data, u32 coremem_size,
		     u8 *fw_code_ptr, u8 *fw_data_ptr, u8 *fw_core_code_ptr, u8 *fw_core_data_ptr)
{
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	u32 offset = l1_data->cmd_data[0];
	u32 byte_count = l1_data->cmd_data[1];
	void *write_addr;
	int err;

	if (ROGUE_META_IS_COREMEM_DATA(offset, coremem_size)) {
		/* cannot zero coremem directly */
		return 0;
	}

	/* Global range is aliased to local range */
	offset &= ~META_MEM_GLOBAL_RANGE_BIT;

	err = pvr_fw_find_mmu_segment(pvr_dev, offset, byte_count, fw_code_ptr, fw_data_ptr,
				      fw_core_code_ptr, fw_core_data_ptr, &write_addr);
	if (err) {
		drm_err(drm_dev,
			"Addr 0x%x (size: %d) not found in any firmware segment",
			offset, byte_count);
		return err;
	}

	memset(write_addr, 0, byte_count);

	return 0;
}

static int
meta_ldr_cmd_config(struct drm_device *drm_dev, const u8 *fw,
		    struct rogue_meta_ldr_l1_data_blk *l1_data,
		    const u32 fw_size, u32 **boot_conf_ptr)
{
	struct rogue_meta_ldr_l2_data_blk *l2_block =
		(struct rogue_meta_ldr_l2_data_blk *)(fw +
						      l1_data->cmd_data[0]);
	struct rogue_meta_ldr_cfg_blk *config_command;
	u32 l2_block_size;
	u32 curr_block_size = 0;
	u32 *boot_conf = boot_conf_ptr ? *boot_conf_ptr : NULL;

	/* Verify block header is within bounds. */
	if (((u8 *)l2_block - fw) >= fw_size || ((u8 *)(l2_block + 1) - fw) >= fw_size)
		return -EINVAL;

	l2_block_size = l2_block->length - 6 /* L2 Tag length and checksum */;
	config_command = (struct rogue_meta_ldr_cfg_blk *)l2_block->block_data;

	if (((u8 *)config_command - fw) >= fw_size ||
	    ((((u8 *)config_command) + l2_block_size) - fw) >= fw_size)
		return -EINVAL;

	while (l2_block_size >= 12) {
		if (config_command->type != ROGUE_META_LDR_CFG_WRITE)
			return -EINVAL;

		/*
		 * Only write to bootloader if we got a valid pointer to the FW
		 * code allocation.
		 */
		if (boot_conf) {
			u32 register_offset = config_command->block_data[0];
			u32 register_value = config_command->block_data[1];

			/* Do register write */
			add_boot_arg(&boot_conf, register_offset,
				     register_value);
		}

		curr_block_size = 12;
		l2_block_size -= curr_block_size;
		config_command = (struct rogue_meta_ldr_cfg_blk
					  *)((uintptr_t)config_command +
					     curr_block_size);
	}

	if (boot_conf_ptr)
		*boot_conf_ptr = boot_conf;

	return 0;
}

/**
 * process_ldr_command_stream() - Process LDR firmware image and populate
 *                                firmware sections
 * @pvr_dev: Device pointer.
 * @fw: Pointer to firmware image.
 * @fw_code_ptr: Pointer to FW code section.
 * @fw_data_ptr: Pointer to FW data section.
 * @fw_core_code_ptr: Pointer to FW coremem code section.
 * @fw_core_data_ptr: Pointer to FW coremem data section.
 * @boot_conf_ptr: Pointer to boot config argument pointer.
 *
 * Returns :
 *  * 0 on success, or
 *  * -EINVAL on any error in LDR command stream.
 */
static int
process_ldr_command_stream(struct pvr_device *pvr_dev, const u8 *fw, u8 *fw_code_ptr,
			   u8 *fw_data_ptr, u8 *fw_core_code_ptr,
			   u8 *fw_core_data_ptr, u32 **boot_conf_ptr)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct rogue_meta_ldr_block_hdr *ldr_header =
		(struct rogue_meta_ldr_block_hdr *)fw;
	struct rogue_meta_ldr_l1_data_blk *l1_data =
		(struct rogue_meta_ldr_l1_data_blk *)(fw + ldr_header->sl_data);
	const u32 fw_size = pvr_dev->fw_dev.firmware->size;
	int err;

	u32 *boot_conf = boot_conf_ptr ? *boot_conf_ptr : NULL;
	u32 coremem_size;

	err = PVR_FEATURE_VALUE(pvr_dev, meta_coremem_size, &coremem_size);
	if (err)
		return err;

	coremem_size *= SZ_1K;

	while (l1_data) {
		/* Verify block header is within bounds. */
		if (((u8 *)l1_data - fw) >= fw_size || ((u8 *)(l1_data + 1) - fw) >= fw_size)
			return -EINVAL;

		if (ROGUE_META_LDR_BLK_IS_COMMENT(l1_data->cmd)) {
			/* Don't process comment blocks */
			goto next_block;
		}

		switch (l1_data->cmd & ROGUE_META_LDR_CMD_MASK)
		case ROGUE_META_LDR_CMD_LOADMEM: {
			err = meta_ldr_cmd_loadmem(drm_dev, fw, l1_data,
						   coremem_size,
						   fw_code_ptr, fw_data_ptr,
						   fw_core_code_ptr,
						   fw_core_data_ptr, fw_size);
			if (err)
				return err;
			break;

		case ROGUE_META_LDR_CMD_START_THREADS:
			/* Don't process this block */
			break;

		case ROGUE_META_LDR_CMD_ZEROMEM:
			err = meta_ldr_cmd_zeromem(drm_dev, l1_data,
						   coremem_size,
						   fw_code_ptr, fw_data_ptr,
						   fw_core_code_ptr,
						   fw_core_data_ptr);
			if (err)
				return err;
			break;

		case ROGUE_META_LDR_CMD_CONFIG:
			err = meta_ldr_cmd_config(drm_dev, fw, l1_data, fw_size,
						  &boot_conf);
			if (err)
				return err;
			break;

		default:
			return -EINVAL;
		}

next_block:
		if (l1_data->next == 0xFFFFFFFF)
			break;

		l1_data = (struct rogue_meta_ldr_l1_data_blk *)(fw +
								l1_data->next);
	}

	if (boot_conf_ptr)
		*boot_conf_ptr = boot_conf;

	return 0;
}

static void
configure_seg_id(u64 seg_out_addr, u32 seg_base, u32 seg_limit, u32 seg_id,
		 u32 **boot_conf_ptr)
{
	u32 seg_out_addr0 = seg_out_addr & 0x00000000FFFFFFFFUL;
	u32 seg_out_addr1 = (seg_out_addr >> 32) & 0x00000000FFFFFFFFUL;
	u32 *boot_conf = *boot_conf_ptr;

	/* META segments have a minimum size. */
	u32 limit_off = max(seg_limit, ROGUE_FW_SEGMMU_ALIGN);

	/* The limit is an offset, therefore off = size - 1. */
	limit_off -= 1;

	seg_base |= ROGUE_FW_SEGMMU_ALLTHRS_WRITEABLE;

	add_boot_arg(&boot_conf, META_CR_MMCU_SEGMENT_N_BASE(seg_id), seg_base);
	add_boot_arg(&boot_conf, META_CR_MMCU_SEGMENT_N_LIMIT(seg_id), limit_off);
	add_boot_arg(&boot_conf, META_CR_MMCU_SEGMENT_N_OUTA0(seg_id), seg_out_addr0);
	add_boot_arg(&boot_conf, META_CR_MMCU_SEGMENT_N_OUTA1(seg_id), seg_out_addr1);

	*boot_conf_ptr = boot_conf;
}

static u64 get_fw_obj_gpu_addr(struct pvr_fw_object *fw_obj)
{
	struct pvr_device *pvr_dev = to_pvr_device(gem_from_pvr_gem(fw_obj->gem)->dev);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	return fw_obj->fw_addr_offset + fw_dev->fw_heap_info.gpu_addr;
}

static void
configure_seg_mmu(struct pvr_device *pvr_dev, u32 **boot_conf_ptr)
{
	const struct pvr_fw_layout_entry *layout_entries = pvr_dev->fw_dev.layout_entries;
	u32 num_layout_entries = pvr_dev->fw_dev.header->layout_entry_num;
	u64 seg_out_addr_top;

	seg_out_addr_top =
		ROGUE_FW_SEGMMU_OUTADDR_TOP_SLC(MMU_CONTEXT_MAPPING_FWPRIV,
						ROGUE_FW_SEGMMU_META_BIFDM_ID);

	for (u32 i = 0; i < num_layout_entries; i++) {
		/*
		 * FW code is using the bootloader segment which is already
		 * configured on boot. FW coremem code and data don't use the
		 * segment MMU. Only the FW data segment needs to be configured.
		 */
		if (layout_entries[i].type == FW_DATA) {
			u32 seg_id = ROGUE_FW_SEGMMU_DATA_ID;
			u64 seg_out_addr = get_fw_obj_gpu_addr(pvr_dev->fw_dev.mem.data_obj);

			seg_out_addr += layout_entries[i].alloc_offset;
			seg_out_addr |= seg_out_addr_top;

			/* Write the sequence to the bootldr. */
			configure_seg_id(seg_out_addr,
					 layout_entries[i].base_addr,
					 layout_entries[i].alloc_size, seg_id,
					 boot_conf_ptr);

			break;
		}
	}
}

static void
configure_meta_caches(u32 **boot_conf_ptr)
{
	u32 *boot_conf = *boot_conf_ptr;
	u32 d_cache_t0, i_cache_t0;
	u32 d_cache_t1, i_cache_t1;
	u32 d_cache_t2, i_cache_t2;
	u32 d_cache_t3, i_cache_t3;

	/* Initialise I/Dcache settings */
	d_cache_t0 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	d_cache_t1 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	d_cache_t2 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	d_cache_t3 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	i_cache_t0 = 0;
	i_cache_t1 = 0;
	i_cache_t2 = 0;
	i_cache_t3 = 0;

	d_cache_t0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
	i_cache_t0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;

	/* Local region MMU enhanced bypass: WIN-3 mode for code and data caches */
	add_boot_arg(&boot_conf, META_CR_MMCU_LOCAL_EBCTRL,
		     META_CR_MMCU_LOCAL_EBCTRL_ICWIN |
			     META_CR_MMCU_LOCAL_EBCTRL_DCWIN);

	/* Data cache partitioning thread 0 to 3 */
	add_boot_arg(&boot_conf, META_CR_SYSC_DCPART(0), d_cache_t0);
	add_boot_arg(&boot_conf, META_CR_SYSC_DCPART(1), d_cache_t1);
	add_boot_arg(&boot_conf, META_CR_SYSC_DCPART(2), d_cache_t2);
	add_boot_arg(&boot_conf, META_CR_SYSC_DCPART(3), d_cache_t3);

	/* Enable data cache hits */
	add_boot_arg(&boot_conf, META_CR_MMCU_DCACHE_CTRL,
		     META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN);

	/* Instruction cache partitioning thread 0 to 3 */
	add_boot_arg(&boot_conf, META_CR_SYSC_ICPART(0), i_cache_t0);
	add_boot_arg(&boot_conf, META_CR_SYSC_ICPART(1), i_cache_t1);
	add_boot_arg(&boot_conf, META_CR_SYSC_ICPART(2), i_cache_t2);
	add_boot_arg(&boot_conf, META_CR_SYSC_ICPART(3), i_cache_t3);

	/* Enable instruction cache hits */
	add_boot_arg(&boot_conf, META_CR_MMCU_ICACHE_CTRL,
		     META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN);

	add_boot_arg(&boot_conf, 0x040000C0, 0);

	*boot_conf_ptr = boot_conf;
}

static int
pvr_meta_fw_process(struct pvr_device *pvr_dev, const u8 *fw,
		    u8 *fw_code_ptr, u8 *fw_data_ptr, u8 *fw_core_code_ptr, u8 *fw_core_data_ptr,
		    u32 core_code_alloc_size)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	u32 *boot_conf;
	int err;

	boot_conf = ((u32 *)fw_code_ptr) + ROGUE_FW_BOOTLDR_CONF_OFFSET;

	/* Slave port and JTAG accesses are privileged. */
	add_boot_arg(&boot_conf, META_CR_SYSC_JTAG_THREAD,
		     META_CR_SYSC_JTAG_THREAD_PRIV_EN);

	configure_seg_mmu(pvr_dev, &boot_conf);

	/* Populate FW sections from LDR image. */
	err = process_ldr_command_stream(pvr_dev, fw, fw_code_ptr, fw_data_ptr, fw_core_code_ptr,
					 fw_core_data_ptr, &boot_conf);
	if (err)
		return err;

	configure_meta_caches(&boot_conf);

	/* End argument list. */
	add_boot_arg(&boot_conf, 0, 0);

	if (fw_dev->mem.core_code_obj) {
		u32 core_code_fw_addr;

		pvr_fw_object_get_fw_addr(fw_dev->mem.core_code_obj, &core_code_fw_addr);
		add_boot_arg(&boot_conf, core_code_fw_addr, core_code_alloc_size);
	} else {
		add_boot_arg(&boot_conf, 0, 0);
	}
	/* None of the cores supported by this driver have META DMA. */
	add_boot_arg(&boot_conf, 0, 0);

	return 0;
}

static int
pvr_meta_init(struct pvr_device *pvr_dev)
{
	pvr_fw_heap_info_init(pvr_dev, ROGUE_FW_HEAP_META_SHIFT, 0);

	return 0;
}

static u32
pvr_meta_get_fw_addr_with_offset(struct pvr_fw_object *fw_obj, u32 offset)
{
	u32 fw_addr = fw_obj->fw_addr_offset + offset + ROGUE_FW_SEGMMU_DATA_BASE_ADDRESS;

	/* META cacheability is determined by address. */
	if (fw_obj->gem->flags & PVR_BO_FW_FLAGS_DEVICE_UNCACHED)
		fw_addr |= ROGUE_FW_SEGMMU_DATA_META_UNCACHED |
			   ROGUE_FW_SEGMMU_DATA_VIVT_SLC_UNCACHED;

	return fw_addr;
}

static int
pvr_meta_vm_map(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;

	return pvr_vm_map(pvr_dev->kernel_vm_ctx, pvr_obj, 0, fw_obj->fw_mm_node.start,
			  pvr_gem_object_size(pvr_obj));
}

static void
pvr_meta_vm_unmap(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;

	pvr_vm_unmap_obj(pvr_dev->kernel_vm_ctx, pvr_obj,
			 fw_obj->fw_mm_node.start, fw_obj->fw_mm_node.size);
}

static bool
pvr_meta_irq_pending(struct pvr_device *pvr_dev)
{
	return pvr_cr_read32(pvr_dev, ROGUE_CR_META_SP_MSLVIRQSTATUS) &
	       ROGUE_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_EN;
}

static void
pvr_meta_irq_clear(struct pvr_device *pvr_dev)
{
	pvr_cr_write32(pvr_dev, ROGUE_CR_META_SP_MSLVIRQSTATUS,
		       ROGUE_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_CLRMSK);
}

const struct pvr_fw_defs pvr_fw_defs_meta = {
	.init = pvr_meta_init,
	.fw_process = pvr_meta_fw_process,
	.vm_map = pvr_meta_vm_map,
	.vm_unmap = pvr_meta_vm_unmap,
	.get_fw_addr_with_offset = pvr_meta_get_fw_addr_with_offset,
	.wrapper_init = pvr_meta_wrapper_init,
	.irq_pending = pvr_meta_irq_pending,
	.irq_clear = pvr_meta_irq_clear,
	.has_fixed_data_addr = false,
};
