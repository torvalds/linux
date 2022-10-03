// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

/**
 * hl_get_pb_block - return the relevant block within the block array
 *
 * @hdev: pointer to hl_device structure
 * @mm_reg_addr: register address in the desired block
 * @pb_blocks: blocks array
 * @array_size: blocks array size
 *
 */
static int hl_get_pb_block(struct hl_device *hdev, u32 mm_reg_addr,
		const u32 pb_blocks[], int array_size)
{
	int i;
	u32 start_addr, end_addr;

	for (i = 0 ; i < array_size ; i++) {
		start_addr = pb_blocks[i];
		end_addr = start_addr + HL_BLOCK_SIZE;

		if ((mm_reg_addr >= start_addr) && (mm_reg_addr < end_addr))
			return i;
	}

	dev_err(hdev->dev, "No protection domain was found for 0x%x\n",
			mm_reg_addr);
	return -EDOM;
}

/**
 * hl_unset_pb_in_block - clear a specific protection bit in a block
 *
 * @hdev: pointer to hl_device structure
 * @reg_offset: register offset will be converted to bit offset in pb block
 * @sgs_entry: pb array
 *
 */
static int hl_unset_pb_in_block(struct hl_device *hdev, u32 reg_offset,
				struct hl_block_glbl_sec *sgs_entry)
{
	if ((reg_offset >= HL_BLOCK_SIZE) || (reg_offset & 0x3)) {
		dev_err(hdev->dev,
			"Register offset(%d) is out of range(%d) or invalid\n",
			reg_offset, HL_BLOCK_SIZE);
		return -EINVAL;
	}

	UNSET_GLBL_SEC_BIT(sgs_entry->sec_array,
			 (reg_offset & (HL_BLOCK_SIZE - 1)) >> 2);

	return 0;
}

/**
 * hl_unsecure_register - locate the relevant block for this register and
 *                        remove corresponding protection bit
 *
 * @hdev: pointer to hl_device structure
 * @mm_reg_addr: register address to unsecure
 * @offset: additional offset to the register address
 * @pb_blocks: blocks array
 * @sgs_array: pb array
 * @array_size: blocks array size
 *
 */
int hl_unsecure_register(struct hl_device *hdev, u32 mm_reg_addr, int offset,
		const u32 pb_blocks[], struct hl_block_glbl_sec sgs_array[],
		int array_size)
{
	u32 reg_offset;
	int block_num;

	block_num = hl_get_pb_block(hdev, mm_reg_addr + offset, pb_blocks,
			array_size);
	if (block_num < 0)
		return block_num;

	reg_offset = (mm_reg_addr + offset) - pb_blocks[block_num];

	return hl_unset_pb_in_block(hdev, reg_offset, &sgs_array[block_num]);
}

/**
 * hl_unsecure_register_range - locate the relevant block for this register
 *                              range and remove corresponding protection bit
 *
 * @hdev: pointer to hl_device structure
 * @mm_reg_range: register address range to unsecure
 * @offset: additional offset to the register address
 * @pb_blocks: blocks array
 * @sgs_array: pb array
 * @array_size: blocks array size
 *
 */
static int hl_unsecure_register_range(struct hl_device *hdev,
		struct range mm_reg_range, int offset, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[],
		int array_size)
{
	u32 reg_offset;
	int i, block_num, rc = 0;

	block_num = hl_get_pb_block(hdev,
			mm_reg_range.start + offset, pb_blocks,
			array_size);
	if (block_num < 0)
		return block_num;

	for (i = mm_reg_range.start ; i <= mm_reg_range.end ; i += 4) {
		reg_offset = (i + offset) - pb_blocks[block_num];
		rc |= hl_unset_pb_in_block(hdev, reg_offset,
					&sgs_array[block_num]);
	}

	return rc;
}

/**
 * hl_unsecure_registers - locate the relevant block for all registers and
 *                        remove corresponding protection bit
 *
 * @hdev: pointer to hl_device structure
 * @mm_reg_array: register address array to unsecure
 * @mm_array_size: register array size
 * @offset: additional offset to the register address
 * @pb_blocks: blocks array
 * @sgs_array: pb array
 * @blocks_array_size: blocks array size
 *
 */
int hl_unsecure_registers(struct hl_device *hdev, const u32 mm_reg_array[],
		int mm_array_size, int offset, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[], int blocks_array_size)
{
	int i, rc = 0;

	for (i = 0 ; i < mm_array_size ; i++) {
		rc = hl_unsecure_register(hdev, mm_reg_array[i], offset,
				pb_blocks, sgs_array, blocks_array_size);

		if (rc)
			return rc;
	}

	return rc;
}

/**
 * hl_unsecure_registers_range - locate the relevant block for all register
 *                        ranges and remove corresponding protection bit
 *
 * @hdev: pointer to hl_device structure
 * @mm_reg_range_array: register address range array to unsecure
 * @mm_array_size: register array size
 * @offset: additional offset to the register address
 * @pb_blocks: blocks array
 * @sgs_array: pb array
 * @blocks_array_size: blocks array size
 *
 */
static int hl_unsecure_registers_range(struct hl_device *hdev,
		const struct range mm_reg_range_array[], int mm_array_size,
		int offset, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[], int blocks_array_size)
{
	int i, rc = 0;

	for (i = 0 ; i < mm_array_size ; i++) {
		rc = hl_unsecure_register_range(hdev, mm_reg_range_array[i],
			offset, pb_blocks, sgs_array, blocks_array_size);

		if (rc)
			return rc;
	}

	return rc;
}

/**
 * hl_ack_pb_security_violations - Ack security violation
 *
 * @hdev: pointer to hl_device structure
 * @pb_blocks: blocks array
 * @block_offset: additional offset to the block
 * @array_size: blocks array size
 *
 */
static void hl_ack_pb_security_violations(struct hl_device *hdev,
		const u32 pb_blocks[], u32 block_offset, int array_size)
{
	int i;
	u32 cause, addr, block_base;

	for (i = 0 ; i < array_size ; i++) {
		block_base = pb_blocks[i] + block_offset;
		cause = RREG32(block_base + HL_BLOCK_GLBL_ERR_CAUSE);
		if (cause) {
			addr = RREG32(block_base + HL_BLOCK_GLBL_ERR_ADDR);
			hdev->asic_funcs->pb_print_security_errors(hdev,
					block_base, cause, addr);
			WREG32(block_base + HL_BLOCK_GLBL_ERR_CAUSE, cause);
		}
	}
}

/**
 * hl_config_glbl_sec - set pb in HW according to given pb array
 *
 * @hdev: pointer to hl_device structure
 * @pb_blocks: blocks array
 * @sgs_array: pb array
 * @block_offset: additional offset to the block
 * @array_size: blocks array size
 *
 */
void hl_config_glbl_sec(struct hl_device *hdev, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[], u32 block_offset,
		int array_size)
{
	int i, j;
	u32 sgs_base;

	if (hdev->pldm)
		usleep_range(100, 1000);

	for (i = 0 ; i < array_size ; i++) {
		sgs_base = block_offset + pb_blocks[i] +
				HL_BLOCK_GLBL_SEC_OFFS;

		for (j = 0 ; j < HL_BLOCK_GLBL_SEC_LEN ; j++)
			WREG32(sgs_base + j * sizeof(u32),
				sgs_array[i].sec_array[j]);
	}
}

/**
 * hl_secure_block - locally memsets a block to 0
 *
 * @hdev: pointer to hl_device structure
 * @sgs_array: pb array to clear
 * @array_size: blocks array size
 *
 */
void hl_secure_block(struct hl_device *hdev,
		struct hl_block_glbl_sec sgs_array[], int array_size)
{
	int i;

	for (i = 0 ; i < array_size ; i++)
		memset((char *)(sgs_array[i].sec_array), 0,
			HL_BLOCK_GLBL_SEC_SIZE);
}

/**
 * hl_init_pb_with_mask - set selected pb instances with mask in HW according
 *                        to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_array: register array
 * @regs_array_size: register array size
 * @mask: enabled instances mask: 1- enabled, 0- disabled
 */
int hl_init_pb_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size, u64 mask)
{
	int i, j;
	struct hl_block_glbl_sec *glbl_sec;

	glbl_sec = kcalloc(blocks_array_size,
			sizeof(struct hl_block_glbl_sec),
			GFP_KERNEL);
	if (!glbl_sec)
		return -ENOMEM;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	hl_unsecure_registers(hdev, regs_array, regs_array_size, 0, pb_blocks,
			glbl_sec, blocks_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < num_dcores ; i++) {
		for (j = 0 ; j < num_instances ; j++) {
			int seq = i * num_instances + j;

			if (!(mask & BIT_ULL(seq)))
				continue;

			hl_config_glbl_sec(hdev, pb_blocks, glbl_sec,
					i * dcore_offset + j * instance_offset,
					blocks_array_size);
		}
	}

	kfree(glbl_sec);

	return 0;
}

/**
 * hl_init_pb - set pb in HW according to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_array: register array
 * @regs_array_size: register array size
 *
 */
int hl_init_pb(struct hl_device *hdev, u32 num_dcores, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size)
{
	return hl_init_pb_with_mask(hdev, num_dcores, dcore_offset,
			num_instances, instance_offset, pb_blocks,
			blocks_array_size, regs_array, regs_array_size,
			ULLONG_MAX);
}

/**
 * hl_init_pb_ranges_with_mask - set pb instances using mask in HW according to
 *                               given configuration unsecurring registers
 *                               ranges instead of specific registers
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_range_array: register range array
 * @regs_range_array_size: register range array size
 * @mask: enabled instances mask: 1- enabled, 0- disabled
 */
int hl_init_pb_ranges_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array, u32 regs_range_array_size,
		u64 mask)
{
	int i, j, rc = 0;
	struct hl_block_glbl_sec *glbl_sec;

	glbl_sec = kcalloc(blocks_array_size,
			sizeof(struct hl_block_glbl_sec),
			GFP_KERNEL);
	if (!glbl_sec)
		return -ENOMEM;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	rc = hl_unsecure_registers_range(hdev, regs_range_array,
			regs_range_array_size, 0, pb_blocks, glbl_sec,
			blocks_array_size);
	if (rc)
		goto free_glbl_sec;

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < num_dcores ; i++) {
		for (j = 0 ; j < num_instances ; j++) {
			int seq = i * num_instances + j;

			if (!(mask & BIT_ULL(seq)))
				continue;

			hl_config_glbl_sec(hdev, pb_blocks, glbl_sec,
					i * dcore_offset + j * instance_offset,
					blocks_array_size);
		}
	}

free_glbl_sec:
	kfree(glbl_sec);

	return rc;
}

/**
 * hl_init_pb_ranges - set pb in HW according to given configuration unsecurring
 *                     registers ranges instead of specific registers
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_range_array: register range array
 * @regs_range_array_size: register range array size
 *
 */
int hl_init_pb_ranges(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array, u32 regs_range_array_size)
{
	return hl_init_pb_ranges_with_mask(hdev, num_dcores, dcore_offset,
			num_instances, instance_offset, pb_blocks,
			blocks_array_size, regs_range_array,
			regs_range_array_size, ULLONG_MAX);
}

/**
 * hl_init_pb_single_dcore - set pb for a single docre in HW
 * according to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @dcore_offset: offset from the dcore0
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_array: register array
 * @regs_array_size: register array size
 *
 */
int hl_init_pb_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size)
{
	int i, rc = 0;
	struct hl_block_glbl_sec *glbl_sec;

	glbl_sec = kcalloc(blocks_array_size,
			sizeof(struct hl_block_glbl_sec),
			GFP_KERNEL);
	if (!glbl_sec)
		return -ENOMEM;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	rc = hl_unsecure_registers(hdev, regs_array, regs_array_size, 0,
			pb_blocks, glbl_sec, blocks_array_size);
	if (rc)
		goto free_glbl_sec;

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < num_instances ; i++)
		hl_config_glbl_sec(hdev, pb_blocks, glbl_sec,
				dcore_offset + i * instance_offset,
				blocks_array_size);

free_glbl_sec:
	kfree(glbl_sec);

	return rc;
}

/**
 * hl_init_pb_ranges_single_dcore - set pb for a single docre in HW according
 *                                  to given configuration unsecurring
 *                                  registers ranges instead of specific
 *                                  registers
 *
 * @hdev: pointer to hl_device structure
 * @dcore_offset: offset from the dcore0
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @regs_range_array: register range array
 * @regs_range_array_size: register range array size
 *
 */
int hl_init_pb_ranges_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array, u32 regs_range_array_size)
{
	int i;
	struct hl_block_glbl_sec *glbl_sec;

	glbl_sec = kcalloc(blocks_array_size,
			sizeof(struct hl_block_glbl_sec),
			GFP_KERNEL);
	if (!glbl_sec)
		return -ENOMEM;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	hl_unsecure_registers_range(hdev, regs_range_array,
			regs_range_array_size, 0, pb_blocks, glbl_sec,
			blocks_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < num_instances ; i++)
		hl_config_glbl_sec(hdev, pb_blocks, glbl_sec,
				dcore_offset + i * instance_offset,
				blocks_array_size);

	kfree(glbl_sec);

	return 0;
}

/**
 * hl_ack_pb_with_mask - ack pb with mask in HW according to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 * @mask: enabled instances mask: 1- enabled, 0- disabled
 *
 */
void hl_ack_pb_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size, u64 mask)
{
	int i, j;

	/* ack all blocks */
	for (i = 0 ; i < num_dcores ; i++) {
		for (j = 0 ; j < num_instances ; j++) {
			int seq = i * num_instances + j;

			if (!(mask & BIT_ULL(seq)))
				continue;

			hl_ack_pb_security_violations(hdev, pb_blocks,
					i * dcore_offset + j * instance_offset,
					blocks_array_size);
		}
	}
}

/**
 * hl_ack_pb - ack pb in HW according to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @num_dcores: number of decores to apply configuration to
 *              set to HL_PB_SHARED if need to apply only once
 * @dcore_offset: offset between dcores
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 *
 */
void hl_ack_pb(struct hl_device *hdev, u32 num_dcores, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size)
{
	hl_ack_pb_with_mask(hdev, num_dcores, dcore_offset, num_instances,
			instance_offset, pb_blocks, blocks_array_size,
			ULLONG_MAX);
}

/**
 * hl_ack_pb_single_dcore - ack pb for single docre in HW
 * according to given configuration
 *
 * @hdev: pointer to hl_device structure
 * @dcore_offset: offset from dcore0
 * @num_instances: number of instances to apply configuration to
 * @instance_offset: offset between instances
 * @pb_blocks: blocks array
 * @blocks_array_size: blocks array size
 *
 */
void hl_ack_pb_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size)
{
	int i;

	/* ack all blocks */
	for (i = 0 ; i < num_instances ; i++)
		hl_ack_pb_security_violations(hdev, pb_blocks,
				dcore_offset + i * instance_offset,
				blocks_array_size);

}
