/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CCS static data in-memory data structure definitions
 *
 * Copyright 2019--2020 Intel Corporation
 */

#ifndef __CCS_DATA_H__
#define __CCS_DATA_H__

#include <linux/types.h>

struct device;

/**
 * struct ccs_data_block_version - CCS static data version
 * @version_major: Major version number
 * @version_minor: Minor version number
 * @date_year: Year
 * @date_month: Month
 * @date_day: Day
 */
struct ccs_data_block_version {
	u16 version_major;
	u16 version_minor;
	u16 date_year;
	u8 date_month;
	u8 date_day;
};

/**
 * struct ccs_reg - CCS register value
 * @addr: The 16-bit address of the register
 * @len: Length of the data
 * @value: Data
 */
struct ccs_reg {
	u16 addr;
	u16 len;
	u8 *value;
};

/**
 * struct ccs_if_rule - CCS static data if rule
 * @addr: Register address
 * @value: Register value
 * @mask: Value applied to both actual register value and @value
 */
struct ccs_if_rule {
	u16 addr;
	u8 value;
	u8 mask;
};

/**
 * struct ccs_frame_format_desc - CCS frame format descriptor
 * @pixelcode: The pixelcode; CCS_DATA_BLOCK_FFD_PIXELCODE_*
 * @value: Value related to the pixelcode
 */
struct ccs_frame_format_desc {
	u8 pixelcode;
	u16 value;
};

/**
 * struct ccs_frame_format_descs - A series of CCS frame format descriptors
 * @num_column_descs: Number of column descriptors
 * @num_row_descs: Number of row descriptors
 * @column_descs: Column descriptors
 * @row_descs: Row descriptors
 */
struct ccs_frame_format_descs {
	u8 num_column_descs;
	u8 num_row_descs;
	struct ccs_frame_format_desc *column_descs;
	struct ccs_frame_format_desc *row_descs;
};

/**
 * struct ccs_pdaf_readout - CCS PDAF data readout descriptor
 * @pdaf_readout_info_order: PDAF readout order
 * @ffd: Frame format of PDAF data
 */
struct ccs_pdaf_readout {
	u8 pdaf_readout_info_order;
	struct ccs_frame_format_descs *ffd;
};

/**
 * struct ccs_rule - A CCS static data rule
 * @num_if_rules: Number of if rules
 * @if_rules: If rules
 * @num_read_only_regs: Number of read-only registers
 * @read_only_regs: Read-only registers
 * @num_manufacturer_regs: Number of manufacturer-specific registers
 * @manufacturer_regs: Manufacturer-specific registers
 * @frame_format: Frame format
 * @pdaf_readout: PDAF readout
 */
struct ccs_rule {
	size_t num_if_rules;
	struct ccs_if_rule *if_rules;
	size_t num_read_only_regs;
	struct ccs_reg *read_only_regs;
	size_t num_manufacturer_regs;
	struct ccs_reg *manufacturer_regs;
	struct ccs_frame_format_descs *frame_format;
	struct ccs_pdaf_readout *pdaf_readout;
};

/**
 * struct ccs_pdaf_pix_loc_block_desc - PDAF pixel location block descriptor
 * @block_type_id: Block type identifier, from 0 to n
 * @repeat_x: Number of times this block is repeated to right
 */
struct ccs_pdaf_pix_loc_block_desc {
	u8 block_type_id;
	u16 repeat_x;
};

/**
 * struct ccs_pdaf_pix_loc_block_desc_group - PDAF pixel location block
 *					      descriptor group
 * @repeat_y: Number of times the group is repeated down
 * @num_block_descs: Number of block descriptors in @block_descs
 * @block_descs: Block descriptors
 */
struct ccs_pdaf_pix_loc_block_desc_group {
	u8 repeat_y;
	u16 num_block_descs;
	struct ccs_pdaf_pix_loc_block_desc *block_descs;
};

/**
 * struct ccs_pdaf_pix_loc_pixel_desc - PDAF pixel location block descriptor
 * @pixel_type: Type of the pixel; CCS_DATA_PDAF_PIXEL_TYPE_*
 * @small_offset_x: offset X coordinate
 * @small_offset_y: offset Y coordinate
 */
struct ccs_pdaf_pix_loc_pixel_desc {
	u8 pixel_type;
	u8 small_offset_x;
	u8 small_offset_y;
};

/**
 * struct ccs_pdaf_pix_loc_pixel_desc_group - PDAF pixel location pixel
 *					      descriptor group
 * @num_descs: Number of descriptors in @descs
 * @descs: PDAF pixel location pixel descriptors
 */
struct ccs_pdaf_pix_loc_pixel_desc_group {
	u8 num_descs;
	struct ccs_pdaf_pix_loc_pixel_desc *descs;
};

/**
 * struct ccs_pdaf_pix_loc - PDAF pixel locations
 * @main_offset_x: Start X coordinate of PDAF pixel blocks
 * @main_offset_y: Start Y coordinate of PDAF pixel blocks
 * @global_pdaf_type: PDAF pattern type
 * @block_width: Width of a block in pixels
 * @block_height: Heigth of a block in pixels
 * @num_block_desc_groups: Number of block descriptor groups
 * @block_desc_groups: Block descriptor groups
 * @num_pixel_desc_grups: Number of pixel descriptor groups
 * @pixel_desc_groups: Pixel descriptor groups
 */
struct ccs_pdaf_pix_loc {
	u16 main_offset_x;
	u16 main_offset_y;
	u8 global_pdaf_type;
	u8 block_width;
	u8 block_height;
	u16 num_block_desc_groups;
	struct ccs_pdaf_pix_loc_block_desc_group *block_desc_groups;
	u8 num_pixel_desc_grups;
	struct ccs_pdaf_pix_loc_pixel_desc_group *pixel_desc_groups;
};

/**
 * struct ccs_data_container - In-memory CCS static data
 * @version: CCS static data version
 * @num_sensor_read_only_regs: Number of the read-only registers for the sensor
 * @sensor_read_only_regs: Read-only registers for the sensor
 * @num_sensor_manufacturer_regs: Number of the manufacturer-specific registers
 *				  for the sensor
 * @sensor_manufacturer_regs: Manufacturer-specific registers for the sensor
 * @num_sensor_rules: Number of rules for the sensor
 * @sensor_rules: Rules for the sensor
 * @num_module_read_only_regs: Number of the read-only registers for the module
 * @module_read_only_regs: Read-only registers for the module
 * @num_module_manufacturer_regs: Number of the manufacturer-specific registers
 *				  for the module
 * @module_manufacturer_regs: Manufacturer-specific registers for the module
 * @num_module_rules: Number of rules for the module
 * @module_rules: Rules for the module
 * @sensor_pdaf: PDAF data for the sensor
 * @module_pdaf: PDAF data for the module
 * @license_length: Lenght of the license data
 * @license: License data
 * @end: Whether or not there's an end block
 * @backing: Raw data, pointed to from elsewhere so keep it around
 */
struct ccs_data_container {
	struct ccs_data_block_version *version;
	size_t num_sensor_read_only_regs;
	struct ccs_reg *sensor_read_only_regs;
	size_t num_sensor_manufacturer_regs;
	struct ccs_reg *sensor_manufacturer_regs;
	size_t num_sensor_rules;
	struct ccs_rule *sensor_rules;
	size_t num_module_read_only_regs;
	struct ccs_reg *module_read_only_regs;
	size_t num_module_manufacturer_regs;
	struct ccs_reg *module_manufacturer_regs;
	size_t num_module_rules;
	struct ccs_rule *module_rules;
	struct ccs_pdaf_pix_loc *sensor_pdaf;
	struct ccs_pdaf_pix_loc *module_pdaf;
	size_t license_length;
	char *license;
	bool end;
	void *backing;
};

int ccs_data_parse(struct ccs_data_container *ccsdata, const void *data,
		   size_t len, struct device *dev, bool verbose);

#endif /* __CCS_DATA_H__ */
