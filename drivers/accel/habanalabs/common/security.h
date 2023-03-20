/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef SECURITY_H_
#define SECURITY_H_

#include <linux/io-64-nonatomic-lo-hi.h>

extern struct hl_device *hdev;

/* special blocks */
#define HL_MAX_NUM_OF_GLBL_ERR_CAUSE		10
#define HL_GLBL_ERR_ADDRESS_MASK		GENMASK(11, 0)
/* GLBL_ERR_ADDR register offset from the start of the block */
#define HL_GLBL_ERR_ADDR_OFFSET		0xF44
/* GLBL_ERR_CAUSE register offset from the start of the block */
#define HL_GLBL_ERR_CAUSE_OFFSET	0xF48

/*
 * struct hl_special_block_info - stores address details of a particular type of
 * IP block which has a SPECIAL part.
 *
 * @block_type: block type as described in every ASIC's block_types enum.
 * @base_addr: base address of the first block of particular type,
 *             e.g., address of NIC0_UMR0_0 of 'NIC_UMR' block.
 * @major: number of major blocks of particular type.
 * @minor: number of minor blocks of particular type.
 * @sub_minor: number of sub minor blocks of particular type.
 * @major_offset: address gap between 2 consecutive major blocks of particular type,
 *                e.g., offset between NIC0_UMR0_0 and NIC1_UMR0_0 is 0x80000.
 * @minor_offset: address gap between 2 consecutive minor blocks of particular type,
 *                e.g., offset between NIC0_UMR0_0 and NIC0_UMR1_0 is 0x20000.
 * @sub_minor_offset: address gap between 2 consecutive sub_minor blocks of particular
 *                    type, e.g., offset between NIC0_UMR0_0 and NIC0_UMR0_1 is 0x1000.
 *
 * e.g., in Gaudi2, NIC_UMR blocks can be interpreted as:
 * NIC<major>_UMR<minor>_<sub_minor> where major=12, minor=2, sub_minor=15.
 * In other words, for each of 12 major numbers (i.e 0 to 11) there are
 * 2 blocks with different minor numbers (i.e. 0 to 1). Again, for each minor
 * number there are 15 blocks with different sub_minor numbers (i.e. 0 to 14).
 * So different blocks are NIC0_UMR0_0, NIC0_UMR0_1, ..., NIC0_UMR1_0, ....,
 * NIC11_UMR1_14.
 *
 * Struct's formatted data is located in the SOL-based auto-generated protbits headers.
 */
struct hl_special_block_info {
	int block_type;
	u32 base_addr;
	u32 major;
	u32 minor;
	u32 sub_minor;
	u32 major_offset;
	u32 minor_offset;
	u32 sub_minor_offset;
};

/*
 * struct hl_automated_pb_cfg - represents configurations of a particular type
 * of IP block which has protection bits.
 *
 * @addr: address details as described in hl_automation_pb_addr struct.
 * @prot_map: each bit corresponds to one among 32 protection configuration regs
 *            (e.g., SPECIAL_GLBL_PRIV). '1' means 0xffffffff and '0' means 0x0
 *            to be written into the corresponding protection configuration reg.
 *            This bit is meaningful if same bit in data_map is 0, otherwise ignored.
 * @data_map: each bit corresponds to one among 32 protection configuration regs
 *            (e.g., SPECIAL_GLBL_PRIV). '1' means corresponding protection
 *            configuration reg is to be written with a value in array pointed
 *            by 'data', otherwise the value is decided by 'prot_map'.
 * @data: pointer to data array which stores the config value(s) to be written
 *            to corresponding protection configuration reg(s).
 * @data_size: size of the data array.
 *
 * Each bit of 'data_map' and 'prot_map' fields corresponds to one among 32
 * protection configuration registers e.g., SPECIAL GLBL PRIV regs (starting at
 * offset 0xE80). '1' in 'data_map' means protection configuration to be done
 * using configuration in data array. '0' in 'data_map" means protection
 * configuration to be done as per the value of corresponding bit in 'prot_map'.
 * '1' in 'prot_map' means the register to be programmed with 0xFFFFFFFF
 * (all non-protected). '0' in 'prot_map' means the register to be programmed
 * with 0x0 (all protected).
 *
 * e.g., prot_map = 0x00000001, data_map = 0xC0000000 , data = {0xff, 0x12}
 * SPECIAL_GLBL_PRIV[0] = 0xFFFFFFFF
 * SPECIAL_GLBL_PRIV[1..29] = 0x0
 * SPECIAL_GLBL_PRIV[30] = 0xFF
 * SPECIAL_GLBL_PRIV[31] = 0x12
 */
struct hl_automated_pb_cfg {
	struct hl_special_block_info addr;
	u32 prot_map;
	u32 data_map;
	const u32 *data;
	u8 data_size;
};

/* struct hl_special_blocks_cfg - holds special blocks cfg data.
 *
 * @priv_automated_pb_cfg: points to the main privileged PB array.
 * @sec_automated_pb_cfg: points to the main secured PB array.
 * @skip_blocks_cfg: holds arrays of block types & block ranges to be excluded.
 * @priv_cfg_size: size of the main privileged PB array.
 * @sec_cfg_size: size of the main secured PB array.
 * @prot_lvl_priv: indication if it's a privileged/secured PB configurations.
 */
struct hl_special_blocks_cfg {
	struct hl_automated_pb_cfg *priv_automated_pb_cfg;
	struct hl_automated_pb_cfg *sec_automated_pb_cfg;
	struct hl_skip_blocks_cfg *skip_blocks_cfg;
	u32 priv_cfg_size;
	u32 sec_cfg_size;
	u8 prot_lvl_priv;
};

/* Automated security */

/* struct hl_skip_blocks_cfg - holds arrays of block types & block ranges to be
 * excluded from special blocks configurations.
 *
 * @block_types: an array of block types NOT to be configured.
 * @block_types_len: len of an array of block types not to be configured.
 * @block_ranges: an array of block ranges not to be configured.
 * @block_ranges_len: len of an array of block ranges not to be configured.
 * @skip_block_hook: hook that will be called before initializing special blocks.
 */
struct hl_skip_blocks_cfg {
	int *block_types;
	size_t block_types_len;
	struct range *block_ranges;
	size_t block_ranges_len;
	bool (*skip_block_hook)(struct hl_device *hdev,
				struct hl_special_blocks_cfg *special_blocks_cfg,
				u32 blk_idx, u32 major, u32 minor, u32 sub_minor);
};

/**
 * struct iterate_special_ctx - HW module special block iterator
 * @fn: function to apply to each HW module special block instance
 * @data: optional internal data to the function iterator
 */
struct iterate_special_ctx {
	/*
	 * callback for the HW module special block iterator
	 * @hdev: pointer to the habanalabs device structure
	 * @block_id: block (ASIC specific definition can be dcore/hdcore)
	 * @major: major block index within block_id
	 * @minor: minor block index within the major block
	 * @sub_minor: sub_minor block index within the minor block
	 * @data: function specific data
	 */
	int (*fn)(struct hl_device *hdev, u32 block_id, u32 major, u32 minor,
						u32 sub_minor, void *data);
	void *data;
};

int hl_iterate_special_blocks(struct hl_device *hdev, struct iterate_special_ctx *ctx);
void hl_check_for_glbl_errors(struct hl_device *hdev);

#endif /* SECURITY_H_ */
