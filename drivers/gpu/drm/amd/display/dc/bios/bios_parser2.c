/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "core_types.h"

#include "ObjectID.h"
#include "atomfirmware.h"

#include "dc_bios_types.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/bios_parser_interface.h"
#include "include/logger_interface.h"

#include "command_table2.h"

#include "bios_parser_helper.h"
#include "command_table_helper2.h"
#include "bios_parser2.h"
#include "bios_parser_types_internal2.h"
#include "bios_parser_interface.h"

#include "bios_parser_common.h"

#define DC_LOGGER \
	bp->base.ctx->logger

#define LAST_RECORD_TYPE 0xff
#define SMU9_SYSPLL0_ID  0

static enum bp_result get_gpio_i2c_info(struct bios_parser *bp,
	struct atom_i2c_record *record,
	struct graphics_object_i2c_info *info);

static enum bp_result bios_parser_get_firmware_info(
	struct dc_bios *dcb,
	struct dc_firmware_info *info);

static enum bp_result bios_parser_get_encoder_cap_info(
	struct dc_bios *dcb,
	struct graphics_object_id object_id,
	struct bp_encoder_cap_info *info);

static enum bp_result get_firmware_info_v3_1(
	struct bios_parser *bp,
	struct dc_firmware_info *info);

static enum bp_result get_firmware_info_v3_2(
	struct bios_parser *bp,
	struct dc_firmware_info *info);

static enum bp_result get_firmware_info_v3_4(
	struct bios_parser *bp,
	struct dc_firmware_info *info);

static enum bp_result get_firmware_info_v3_5(
	struct bios_parser *bp,
	struct dc_firmware_info *info);

static struct atom_hpd_int_record *get_hpd_record(struct bios_parser *bp,
		struct atom_display_object_path_v2 *object);

static struct atom_encoder_caps_record *get_encoder_cap_record(
	struct bios_parser *bp,
	struct atom_display_object_path_v2 *object);

#define BIOS_IMAGE_SIZE_OFFSET 2
#define BIOS_IMAGE_SIZE_UNIT 512

#define DATA_TABLES(table) (bp->master_data_tbl->listOfdatatables.table)

static void bios_parser2_destruct(struct bios_parser *bp)
{
	kfree(bp->base.bios_local_image);
	kfree(bp->base.integrated_info);
}

static void firmware_parser_destroy(struct dc_bios **dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(*dcb);

	if (!bp) {
		BREAK_TO_DEBUGGER();
		return;
	}

	bios_parser2_destruct(bp);

	kfree(bp);
	*dcb = NULL;
}

static void get_atom_data_table_revision(
	struct atom_common_table_header *atom_data_tbl,
	struct atom_data_revision *tbl_revision)
{
	if (!tbl_revision)
		return;

	/* initialize the revision to 0 which is invalid revision */
	tbl_revision->major = 0;
	tbl_revision->minor = 0;

	if (!atom_data_tbl)
		return;

	tbl_revision->major =
			(uint32_t) atom_data_tbl->format_revision & 0x3f;
	tbl_revision->minor =
			(uint32_t) atom_data_tbl->content_revision & 0x3f;
}

/* BIOS oject table displaypath is per connector.
 * There is extra path not for connector. BIOS fill its encoderid as 0
 */
static uint8_t bios_parser_get_connectors_number(struct dc_bios *dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	unsigned int count = 0;
	unsigned int i;

	switch (bp->object_info_tbl.revision.minor) {
	default:
	case 4:
		for (i = 0; i < bp->object_info_tbl.v1_4->number_of_path; i++)
			if (bp->object_info_tbl.v1_4->display_path[i].encoderobjid != 0)
				count++;

		break;

	case 5:
		for (i = 0; i < bp->object_info_tbl.v1_5->number_of_path; i++)
			if (bp->object_info_tbl.v1_5->display_path[i].encoderobjid != 0)
				count++;

		break;
	}
	return count;
}

static struct graphics_object_id bios_parser_get_connector_id(
	struct dc_bios *dcb,
	uint8_t i)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct graphics_object_id object_id = dal_graphics_object_id_init(
		0, ENUM_ID_UNKNOWN, OBJECT_TYPE_UNKNOWN);
	struct object_info_table *tbl = &bp->object_info_tbl;
	struct display_object_info_table_v1_4 *v1_4 = tbl->v1_4;

	struct display_object_info_table_v1_5 *v1_5 = tbl->v1_5;

	switch (bp->object_info_tbl.revision.minor) {
	default:
	case 4:
		if (v1_4->number_of_path > i) {
			/* If display_objid is generic object id,  the encoderObj
			 * /extencoderobjId should be 0
			 */
			if (v1_4->display_path[i].encoderobjid != 0 &&
			    v1_4->display_path[i].display_objid != 0)
				object_id = object_id_from_bios_object_id(
					v1_4->display_path[i].display_objid);
		}
		break;

	case 5:
		if (v1_5->number_of_path > i) {
			/* If display_objid is generic object id,  the encoderObjId
		 * should be 0
		 */
			if (v1_5->display_path[i].encoderobjid != 0 &&
			    v1_5->display_path[i].display_objid != 0)
				object_id = object_id_from_bios_object_id(
					v1_5->display_path[i].display_objid);
		}
		break;
	}
	return object_id;
}

static enum bp_result bios_parser_get_src_obj(struct dc_bios *dcb,
	struct graphics_object_id object_id, uint32_t index,
	struct graphics_object_id *src_object_id)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	unsigned int i;
	enum bp_result bp_result = BP_RESULT_BADINPUT;
	struct graphics_object_id obj_id = { 0 };
	struct object_info_table *tbl = &bp->object_info_tbl;

	if (!src_object_id)
		return bp_result;

	switch (object_id.type) {
	/* Encoder's Source is GPU.  BIOS does not provide GPU, since all
	 * displaypaths point to same GPU (0x1100).  Hardcode GPU object type
	 */
	case OBJECT_TYPE_ENCODER:
		/* TODO: since num of src must be less than 2.
		 * If found in for loop, should break.
		 * DAL2 implementation may be changed too
		 */
		switch (bp->object_info_tbl.revision.minor) {
		default:
		case 4:
			for (i = 0; i < tbl->v1_4->number_of_path; i++) {
				obj_id = object_id_from_bios_object_id(
					tbl->v1_4->display_path[i].encoderobjid);
				if (object_id.type == obj_id.type &&
				    object_id.id == obj_id.id &&
				    object_id.enum_id == obj_id.enum_id) {
					*src_object_id =
						object_id_from_bios_object_id(
							0x1100);
					/* break; */
				}
			}
			bp_result = BP_RESULT_OK;
			break;

		case 5:
			for (i = 0; i < tbl->v1_5->number_of_path; i++) {
				obj_id = object_id_from_bios_object_id(
					tbl->v1_5->display_path[i].encoderobjid);
				if (object_id.type == obj_id.type &&
				    object_id.id == obj_id.id &&
				    object_id.enum_id == obj_id.enum_id) {
					*src_object_id =
						object_id_from_bios_object_id(
							0x1100);
					/* break; */
				}
			}
			bp_result = BP_RESULT_OK;
			break;
		}
		break;
	case OBJECT_TYPE_CONNECTOR:
		switch (bp->object_info_tbl.revision.minor) {
		default:
		case 4:
			for (i = 0; i < tbl->v1_4->number_of_path; i++) {
				obj_id = object_id_from_bios_object_id(
					tbl->v1_4->display_path[i]
						.display_objid);

				if (object_id.type == obj_id.type &&
				    object_id.id == obj_id.id &&
				    object_id.enum_id == obj_id.enum_id) {
					*src_object_id =
						object_id_from_bios_object_id(
							tbl->v1_4
								->display_path[i]
								.encoderobjid);
					/* break; */
				}
			}
			bp_result = BP_RESULT_OK;
			break;
		}
		bp_result = BP_RESULT_OK;
		break;
		case 5:
			for (i = 0; i < tbl->v1_5->number_of_path; i++) {
				obj_id = object_id_from_bios_object_id(
								       tbl->v1_5->display_path[i].display_objid);

				if (object_id.type == obj_id.type &&
				    object_id.id == obj_id.id &&
				    object_id.enum_id == obj_id.enum_id) {
					*src_object_id = object_id_from_bios_object_id(
										       tbl->v1_5->display_path[i].encoderobjid);
					/* break; */
				}
			}
		bp_result = BP_RESULT_OK;
		break;

	default:
		bp_result = BP_RESULT_OK;
		break;
	}

	return bp_result;
}

/* from graphics_object_id, find display path which includes the object_id */
static struct atom_display_object_path_v2 *get_bios_object(
		struct bios_parser *bp,
		struct graphics_object_id id)
{
	unsigned int i;
	struct graphics_object_id obj_id = {0};

	switch (id.type) {
	case OBJECT_TYPE_ENCODER:
		for (i = 0; i < bp->object_info_tbl.v1_4->number_of_path; i++) {
			obj_id = object_id_from_bios_object_id(
					bp->object_info_tbl.v1_4->display_path[i].encoderobjid);
			if (id.type == obj_id.type && id.id == obj_id.id
					&& id.enum_id == obj_id.enum_id)
				return &bp->object_info_tbl.v1_4->display_path[i];
		}
		fallthrough;
	case OBJECT_TYPE_CONNECTOR:
	case OBJECT_TYPE_GENERIC:
		/* Both Generic and Connector Object ID
		 * will be stored on display_objid
		 */
		for (i = 0; i < bp->object_info_tbl.v1_4->number_of_path; i++) {
			obj_id = object_id_from_bios_object_id(
					bp->object_info_tbl.v1_4->display_path[i].display_objid);
			if (id.type == obj_id.type && id.id == obj_id.id
					&& id.enum_id == obj_id.enum_id)
				return &bp->object_info_tbl.v1_4->display_path[i];
		}
		fallthrough;
	default:
		return NULL;
	}
}

/* from graphics_object_id, find display path which includes the object_id */
static struct atom_display_object_path_v3 *get_bios_object_from_path_v3(struct bios_parser *bp,
									struct graphics_object_id id)
{
	unsigned int i;
	struct graphics_object_id obj_id = {0};

	switch (id.type) {
	case OBJECT_TYPE_ENCODER:
		for (i = 0; i < bp->object_info_tbl.v1_5->number_of_path; i++) {
			obj_id = object_id_from_bios_object_id(
					bp->object_info_tbl.v1_5->display_path[i].encoderobjid);
			if (id.type == obj_id.type && id.id == obj_id.id
					&& id.enum_id == obj_id.enum_id)
				return &bp->object_info_tbl.v1_5->display_path[i];
		}
	break;

	case OBJECT_TYPE_CONNECTOR:
	case OBJECT_TYPE_GENERIC:
		/* Both Generic and Connector Object ID
		 * will be stored on display_objid
		 */
		for (i = 0; i < bp->object_info_tbl.v1_5->number_of_path; i++) {
			obj_id = object_id_from_bios_object_id(
					bp->object_info_tbl.v1_5->display_path[i].display_objid);
			if (id.type == obj_id.type && id.id == obj_id.id
					&& id.enum_id == obj_id.enum_id)
				return &bp->object_info_tbl.v1_5->display_path[i];
		}
	break;

	default:
		return NULL;
	}

	return NULL;
}

static enum bp_result bios_parser_get_i2c_info(struct dc_bios *dcb,
	struct graphics_object_id id,
	struct graphics_object_i2c_info *info)
{
	uint32_t offset;
	struct atom_display_object_path_v2 *object;

	struct atom_display_object_path_v3 *object_path_v3;

	struct atom_common_record_header *header;
	struct atom_i2c_record *record;
	struct atom_i2c_record dummy_record = {0};
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!info)
		return BP_RESULT_BADINPUT;

	if (id.type == OBJECT_TYPE_GENERIC) {
		dummy_record.i2c_id = id.id;

		if (get_gpio_i2c_info(bp, &dummy_record, info) == BP_RESULT_OK)
			return BP_RESULT_OK;
		else
			return BP_RESULT_NORECORD;
	}

	switch (bp->object_info_tbl.revision.minor) {
	case 4:
	default:
		object = get_bios_object(bp, id);

		if (!object)
			return BP_RESULT_BADINPUT;

		offset = object->disp_recordoffset + bp->object_info_tbl_offset;
		break;
	case 5:
		object_path_v3 = get_bios_object_from_path_v3(bp, id);

		if (!object_path_v3)
			return BP_RESULT_BADINPUT;

		offset = object_path_v3->disp_recordoffset + bp->object_info_tbl_offset;
		break;
	}

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return BP_RESULT_BADBIOSTABLE;

		if (header->record_type == LAST_RECORD_TYPE ||
			!header->record_size)
			break;

		if (header->record_type == ATOM_I2C_RECORD_TYPE
			&& sizeof(struct atom_i2c_record) <=
							header->record_size) {
			/* get the I2C info */
			record = (struct atom_i2c_record *) header;

			if (get_gpio_i2c_info(bp, record, info) ==
								BP_RESULT_OK)
				return BP_RESULT_OK;
		}

		offset += header->record_size;
	}

	return BP_RESULT_NORECORD;
}

static enum bp_result get_gpio_i2c_info(
	struct bios_parser *bp,
	struct atom_i2c_record *record,
	struct graphics_object_i2c_info *info)
{
	struct atom_gpio_pin_lut_v2_1 *header;
	uint32_t count = 0;
	unsigned int table_index = 0;
	bool find_valid = false;
	struct atom_gpio_pin_assignment *pin;

	if (!info)
		return BP_RESULT_BADINPUT;

	/* get the GPIO_I2C info */
	if (!DATA_TABLES(gpio_pin_lut))
		return BP_RESULT_BADBIOSTABLE;

	header = GET_IMAGE(struct atom_gpio_pin_lut_v2_1,
					DATA_TABLES(gpio_pin_lut));
	if (!header)
		return BP_RESULT_BADBIOSTABLE;

	if (sizeof(struct atom_common_table_header) +
			sizeof(struct atom_gpio_pin_assignment)	>
			le16_to_cpu(header->table_header.structuresize))
		return BP_RESULT_BADBIOSTABLE;

	/* TODO: is version change? */
	if (header->table_header.content_revision != 1)
		return BP_RESULT_UNSUPPORTED;

	/* get data count */
	count = (le16_to_cpu(header->table_header.structuresize)
			- sizeof(struct atom_common_table_header))
				/ sizeof(struct atom_gpio_pin_assignment);

	pin = (struct atom_gpio_pin_assignment *) header->gpio_pin;

	for (table_index = 0; table_index < count; table_index++) {
		if (((record->i2c_id & I2C_HW_CAP) 				== (pin->gpio_id & I2C_HW_CAP)) &&
		    ((record->i2c_id & I2C_HW_ENGINE_ID_MASK)	== (pin->gpio_id & I2C_HW_ENGINE_ID_MASK)) &&
		    ((record->i2c_id & I2C_HW_LANE_MUX) 		== (pin->gpio_id & I2C_HW_LANE_MUX))) {
			/* still valid */
			find_valid = true;
			break;
		}
		pin = (struct atom_gpio_pin_assignment *)((uint8_t *)pin + sizeof(struct atom_gpio_pin_assignment));
	}

	/* If we don't find the entry that we are looking for then
	 *  we will return BP_Result_BadBiosTable.
	 */
	if (find_valid == false)
		return BP_RESULT_BADBIOSTABLE;

	/* get the GPIO_I2C_INFO */
	info->i2c_hw_assist = (record->i2c_id & I2C_HW_CAP) ? true : false;
	info->i2c_line = record->i2c_id & I2C_HW_LANE_MUX;
	info->i2c_engine_id = (record->i2c_id & I2C_HW_ENGINE_ID_MASK) >> 4;
	info->i2c_slave_address = record->i2c_slave_addr;

	/* TODO: check how to get register offset for en, Y, etc. */
	info->gpio_info.clk_a_register_index = le16_to_cpu(pin->data_a_reg_index);
	info->gpio_info.clk_a_shift = pin->gpio_bitshift;

	return BP_RESULT_OK;
}

static struct atom_hpd_int_record *get_hpd_record_for_path_v3(struct bios_parser *bp,
							      struct atom_display_object_path_v3 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = object->disp_recordoffset + bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		if (header->record_type == ATOM_RECORD_END_TYPE ||
			!header->record_size)
			break;

		if (header->record_type == ATOM_HPD_INT_RECORD_TYPE
			&& sizeof(struct atom_hpd_int_record) <=
							header->record_size)
			return (struct atom_hpd_int_record *) header;

		offset += header->record_size;
	}

	return NULL;
}

static enum bp_result bios_parser_get_hpd_info(
	struct dc_bios *dcb,
	struct graphics_object_id id,
	struct graphics_object_hpd_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_display_object_path_v2 *object;
	struct atom_display_object_path_v3 *object_path_v3;
	struct atom_hpd_int_record *record = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	switch (bp->object_info_tbl.revision.minor) {
	case 4:
	default:
		object = get_bios_object(bp, id);

		if (!object)
			return BP_RESULT_BADINPUT;

		record = get_hpd_record(bp, object);
		break;
	case 5:
		object_path_v3 = get_bios_object_from_path_v3(bp, id);

		if (!object_path_v3)
			return BP_RESULT_BADINPUT;

		record = get_hpd_record_for_path_v3(bp, object_path_v3);
		break;
	}

	if (record != NULL) {
		info->hpd_int_gpio_uid = record->pin_id;
		info->hpd_active = record->plugin_pin_state;
		return BP_RESULT_OK;
	}

	return BP_RESULT_NORECORD;
}

static struct atom_hpd_int_record *get_hpd_record(
	struct bios_parser *bp,
	struct atom_display_object_path_v2 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = le16_to_cpu(object->disp_recordoffset)
			+ bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		if (header->record_type == LAST_RECORD_TYPE ||
			!header->record_size)
			break;

		if (header->record_type == ATOM_HPD_INT_RECORD_TYPE
			&& sizeof(struct atom_hpd_int_record) <=
							header->record_size)
			return (struct atom_hpd_int_record *) header;

		offset += header->record_size;
	}

	return NULL;
}

/**
 * bios_parser_get_gpio_pin_info
 * Get GpioPin information of input gpio id
 *
 * @dcb:     pointer to the DC BIOS
 * @gpio_id: GPIO ID
 * @info:    GpioPin information structure
 * return: Bios parser result code
 * note:
 *  to get the GPIO PIN INFO, we need:
 *  1. get the GPIO_ID from other object table, see GetHPDInfo()
 *  2. in DATA_TABLE.GPIO_Pin_LUT, search all records,
 *	to get the registerA  offset/mask
 */
static enum bp_result bios_parser_get_gpio_pin_info(
	struct dc_bios *dcb,
	uint32_t gpio_id,
	struct gpio_pin_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_gpio_pin_lut_v2_1 *header;
	uint32_t count = 0;
	uint32_t i = 0;

	if (!DATA_TABLES(gpio_pin_lut))
		return BP_RESULT_BADBIOSTABLE;

	header = GET_IMAGE(struct atom_gpio_pin_lut_v2_1,
						DATA_TABLES(gpio_pin_lut));
	if (!header)
		return BP_RESULT_BADBIOSTABLE;

	if (sizeof(struct atom_common_table_header) +
			sizeof(struct atom_gpio_pin_assignment)
			> le16_to_cpu(header->table_header.structuresize))
		return BP_RESULT_BADBIOSTABLE;

	if (header->table_header.content_revision != 1)
		return BP_RESULT_UNSUPPORTED;

	/* Temporary hard code gpio pin info */
	count = (le16_to_cpu(header->table_header.structuresize)
			- sizeof(struct atom_common_table_header))
				/ sizeof(struct atom_gpio_pin_assignment);
	for (i = 0; i < count; ++i) {
		if (header->gpio_pin[i].gpio_id != gpio_id)
			continue;

		info->offset =
			(uint32_t) le16_to_cpu(
					header->gpio_pin[i].data_a_reg_index);
		info->offset_y = info->offset + 2;
		info->offset_en = info->offset + 1;
		info->offset_mask = info->offset - 1;

		info->mask = (uint32_t) (1 <<
			header->gpio_pin[i].gpio_bitshift);
		info->mask_y = info->mask + 2;
		info->mask_en = info->mask + 1;
		info->mask_mask = info->mask - 1;

		return BP_RESULT_OK;
	}

	return BP_RESULT_NORECORD;
}

static struct device_id device_type_from_device_id(uint16_t device_id)
{

	struct device_id result_device_id;

	result_device_id.raw_device_tag = device_id;

	switch (device_id) {
	case ATOM_DISPLAY_LCD1_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_LCD;
		result_device_id.enum_id = 1;
		break;

	case ATOM_DISPLAY_LCD2_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_LCD;
		result_device_id.enum_id = 2;
		break;

	case ATOM_DISPLAY_DFP1_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 1;
		break;

	case ATOM_DISPLAY_DFP2_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 2;
		break;

	case ATOM_DISPLAY_DFP3_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 3;
		break;

	case ATOM_DISPLAY_DFP4_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 4;
		break;

	case ATOM_DISPLAY_DFP5_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 5;
		break;

	case ATOM_DISPLAY_DFP6_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 6;
		break;

	default:
		BREAK_TO_DEBUGGER(); /* Invalid device Id */
		result_device_id.device_type = DEVICE_TYPE_UNKNOWN;
		result_device_id.enum_id = 0;
	}
	return result_device_id;
}

static enum bp_result bios_parser_get_device_tag(
	struct dc_bios *dcb,
	struct graphics_object_id connector_object_id,
	uint32_t device_tag_index,
	struct connector_device_tag_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_display_object_path_v2 *object;

	struct atom_display_object_path_v3 *object_path_v3;


	if (!info)
		return BP_RESULT_BADINPUT;

	switch (bp->object_info_tbl.revision.minor) {
	case 4:
	default:
	        /* getBiosObject will return MXM object */
		object = get_bios_object(bp, connector_object_id);

		if (!object) {
			BREAK_TO_DEBUGGER(); /* Invalid object id */
			return BP_RESULT_BADINPUT;
		}

		info->acpi_device = 0; /* BIOS no longer provides this */
		info->dev_id = device_type_from_device_id(object->device_tag);
		break;
	case 5:
		object_path_v3 = get_bios_object_from_path_v3(bp, connector_object_id);

		if (!object_path_v3) {
			BREAK_TO_DEBUGGER(); /* Invalid object id */
			return BP_RESULT_BADINPUT;
		}
		info->acpi_device = 0; /* BIOS no longer provides this */
		info->dev_id = device_type_from_device_id(object_path_v3->device_tag);
		break;
	}

	return BP_RESULT_OK;
}

static enum bp_result get_ss_info_v4_1(
	struct bios_parser *bp,
	uint32_t id,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_1 *disp_cntl_tbl = NULL;
	struct atom_smu_info_v3_3 *smu_info = NULL;

	if (!ss_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl =  GET_IMAGE(struct atom_display_controller_info_v4_1,
							DATA_TABLES(dce_info));
	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;


	ss_info->type.STEP_AND_DELAY_INFO = false;
	ss_info->spread_percentage_divider = 1000;
	/* BIOS no longer uses target clock.  Always enable for now */
	ss_info->target_clock_range = 0xffffffff;

	switch (id) {
	case AS_SIGNAL_TYPE_DVI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->dvi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->dvi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->dvi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_DVI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_HDMI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->hdmi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->hdmi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->hdmi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_HDMI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	/* TODO LVDS not support anymore? */
	case AS_SIGNAL_TYPE_DISPLAY_PORT:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->dp_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->dp_ss_rate_10hz * 10;
		if (disp_cntl_tbl->dp_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_DISPLAY_PORT ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_GPU_PLL:
		/* atom_firmware: DAL only get data from dce_info table.
		 * if data within smu_info is needed for DAL, VBIOS should
		 * copy it into dce_info
		 */
		result = BP_RESULT_UNSUPPORTED;
		break;
	case AS_SIGNAL_TYPE_XGMI:
		smu_info =  GET_IMAGE(struct atom_smu_info_v3_3,
				      DATA_TABLES(smu_info));
		if (!smu_info)
			return BP_RESULT_BADBIOSTABLE;
		DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", smu_info->gpuclk_ss_percentage);
		ss_info->spread_spectrum_percentage =
				smu_info->waflclk_ss_percentage;
		ss_info->spread_spectrum_range =
				smu_info->gpuclk_ss_rate_10hz * 10;
		if (smu_info->waflclk_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_XGMI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	default:
		result = BP_RESULT_UNSUPPORTED;
	}

	return result;
}

static enum bp_result get_ss_info_v4_2(
	struct bios_parser *bp,
	uint32_t id,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_2 *disp_cntl_tbl = NULL;
	struct atom_smu_info_v3_1 *smu_info = NULL;

	if (!ss_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	if (!DATA_TABLES(smu_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl =  GET_IMAGE(struct atom_display_controller_info_v4_2,
							DATA_TABLES(dce_info));
	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	smu_info =  GET_IMAGE(struct atom_smu_info_v3_1, DATA_TABLES(smu_info));
	if (!smu_info)
		return BP_RESULT_BADBIOSTABLE;

	DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", smu_info->gpuclk_ss_percentage);
	ss_info->type.STEP_AND_DELAY_INFO = false;
	ss_info->spread_percentage_divider = 1000;
	/* BIOS no longer uses target clock.  Always enable for now */
	ss_info->target_clock_range = 0xffffffff;

	switch (id) {
	case AS_SIGNAL_TYPE_DVI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->dvi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->dvi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->dvi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_DVI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_HDMI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->hdmi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->hdmi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->hdmi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_HDMI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	/* TODO LVDS not support anymore? */
	case AS_SIGNAL_TYPE_DISPLAY_PORT:
		ss_info->spread_spectrum_percentage =
				smu_info->gpuclk_ss_percentage;
		ss_info->spread_spectrum_range =
				smu_info->gpuclk_ss_rate_10hz * 10;
		if (smu_info->gpuclk_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_DISPLAY_PORT ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_GPU_PLL:
		/* atom_firmware: DAL only get data from dce_info table.
		 * if data within smu_info is needed for DAL, VBIOS should
		 * copy it into dce_info
		 */
		result = BP_RESULT_UNSUPPORTED;
		break;
	default:
		result = BP_RESULT_UNSUPPORTED;
	}

	return result;
}

static enum bp_result get_ss_info_v4_5(
	struct bios_parser *bp,
	uint32_t id,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_5 *disp_cntl_tbl = NULL;

	if (!ss_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl =  GET_IMAGE(struct atom_display_controller_info_v4_5,
							DATA_TABLES(dce_info));
	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	ss_info->type.STEP_AND_DELAY_INFO = false;
	ss_info->spread_percentage_divider = 1000;
	/* BIOS no longer uses target clock.  Always enable for now */
	ss_info->target_clock_range = 0xffffffff;

	switch (id) {
	case AS_SIGNAL_TYPE_DVI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->dvi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->dvi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->dvi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_DVI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_HDMI:
		ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->hdmi_ss_percentage;
		ss_info->spread_spectrum_range =
				disp_cntl_tbl->hdmi_ss_rate_10hz * 10;
		if (disp_cntl_tbl->hdmi_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
			ss_info->type.CENTER_MODE = true;

		DC_LOG_BIOS("AS_SIGNAL_TYPE_HDMI ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_DISPLAY_PORT:
		if (bp->base.integrated_info) {
			DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", bp->base.integrated_info->gpuclk_ss_percentage);
			ss_info->spread_spectrum_percentage =
					bp->base.integrated_info->gpuclk_ss_percentage;
			ss_info->type.CENTER_MODE =
					bp->base.integrated_info->gpuclk_ss_type;
		} else {
			ss_info->spread_spectrum_percentage =
				disp_cntl_tbl->dp_ss_percentage;
			ss_info->spread_spectrum_range =
				disp_cntl_tbl->dp_ss_rate_10hz * 10;
			if (disp_cntl_tbl->dp_ss_mode & ATOM_SS_CENTRE_SPREAD_MODE)
				ss_info->type.CENTER_MODE = true;
		}
		DC_LOG_BIOS("AS_SIGNAL_TYPE_DISPLAY_PORT ss_percentage: %d\n", ss_info->spread_spectrum_percentage);
		break;
	case AS_SIGNAL_TYPE_GPU_PLL:
		/* atom_smu_info_v4_0 does not have fields for SS for SMU Display PLL anymore.
		 * SMU Display PLL supposed to be without spread.
		 * Better place for it would be in atom_display_controller_info_v4_5 table.
		 */
		result = BP_RESULT_UNSUPPORTED;
		break;
	default:
		result = BP_RESULT_UNSUPPORTED;
		break;
	}

	return result;
}

/**
 * bios_parser_get_spread_spectrum_info
 * Get spread spectrum information from the ASIC_InternalSS_Info(ver 2.1 or
 * ver 3.1) or SS_Info table from the VBIOS. Currently ASIC_InternalSS_Info
 * ver 2.1 can co-exist with SS_Info table. Expect ASIC_InternalSS_Info
 * ver 3.1,
 * there is only one entry for each signal /ss id.  However, there is
 * no planning of supporting multiple spread Sprectum entry for EverGreen
 * @dcb:     pointer to the DC BIOS
 * @signal:  ASSignalType to be converted to info index
 * @index:   number of entries that match the converted info index
 * @ss_info: sprectrum information structure,
 * return: Bios parser result code
 */
static enum bp_result bios_parser_get_spread_spectrum_info(
	struct dc_bios *dcb,
	enum as_signal_type signal,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;

	if (!ss_info) /* check for bad input */
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_UNSUPPORTED;

	header = GET_IMAGE(struct atom_common_table_header,
						DATA_TABLES(dce_info));
	get_atom_data_table_revision(header, &tbl_revision);

	switch (tbl_revision.major) {
	case 4:
		switch (tbl_revision.minor) {
		case 1:
			return get_ss_info_v4_1(bp, signal, index, ss_info);
		case 2:
		case 3:
		case 4:
			return get_ss_info_v4_2(bp, signal, index, ss_info);
		case 5:
			return get_ss_info_v4_5(bp, signal, index, ss_info);

		default:
			ASSERT(0);
			break;
		}
		break;
	default:
		break;
	}
	/* there can not be more then one entry for SS Info table */
	return result;
}

static enum bp_result get_soc_bb_info_v4_4(
	struct bios_parser *bp,
	struct bp_soc_bb_info *soc_bb_info)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_4 *disp_cntl_tbl = NULL;

	if (!soc_bb_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	if (!DATA_TABLES(smu_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl =  GET_IMAGE(struct atom_display_controller_info_v4_4,
							DATA_TABLES(dce_info));
	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	soc_bb_info->dram_clock_change_latency_100ns = disp_cntl_tbl->max_mclk_chg_lat;
	soc_bb_info->dram_sr_enter_exit_latency_100ns = disp_cntl_tbl->max_sr_enter_exit_lat;
	soc_bb_info->dram_sr_exit_latency_100ns = disp_cntl_tbl->max_sr_exit_lat;

	return result;
}

static enum bp_result get_soc_bb_info_v4_5(
	struct bios_parser *bp,
	struct bp_soc_bb_info *soc_bb_info)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_5 *disp_cntl_tbl = NULL;

	if (!soc_bb_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl =  GET_IMAGE(struct atom_display_controller_info_v4_5,
							DATA_TABLES(dce_info));
	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	soc_bb_info->dram_clock_change_latency_100ns = disp_cntl_tbl->max_mclk_chg_lat;
	soc_bb_info->dram_sr_enter_exit_latency_100ns = disp_cntl_tbl->max_sr_enter_exit_lat;
	soc_bb_info->dram_sr_exit_latency_100ns = disp_cntl_tbl->max_sr_exit_lat;

	return result;
}

static enum bp_result bios_parser_get_soc_bb_info(
	struct dc_bios *dcb,
	struct bp_soc_bb_info *soc_bb_info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;

	if (!soc_bb_info) /* check for bad input */
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_UNSUPPORTED;

	header = GET_IMAGE(struct atom_common_table_header,
						DATA_TABLES(dce_info));
	get_atom_data_table_revision(header, &tbl_revision);

	switch (tbl_revision.major) {
	case 4:
		switch (tbl_revision.minor) {
		case 1:
		case 2:
		case 3:
			break;
		case 4:
			result = get_soc_bb_info_v4_4(bp, soc_bb_info);
			break;
		case 5:
			result = get_soc_bb_info_v4_5(bp, soc_bb_info);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return result;
}

static enum bp_result get_disp_caps_v4_1(
	struct bios_parser *bp,
	uint8_t *dce_caps)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_1 *disp_cntl_tbl = NULL;

	if (!dce_caps)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl = GET_IMAGE(struct atom_display_controller_info_v4_1,
							DATA_TABLES(dce_info));

	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	*dce_caps = disp_cntl_tbl->display_caps;

	return result;
}

static enum bp_result get_disp_caps_v4_2(
	struct bios_parser *bp,
	uint8_t *dce_caps)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_2 *disp_cntl_tbl = NULL;

	if (!dce_caps)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl = GET_IMAGE(struct atom_display_controller_info_v4_2,
							DATA_TABLES(dce_info));

	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	*dce_caps = disp_cntl_tbl->display_caps;

	return result;
}

static enum bp_result get_disp_caps_v4_3(
	struct bios_parser *bp,
	uint8_t *dce_caps)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_3 *disp_cntl_tbl = NULL;

	if (!dce_caps)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl = GET_IMAGE(struct atom_display_controller_info_v4_3,
							DATA_TABLES(dce_info));

	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	*dce_caps = disp_cntl_tbl->display_caps;

	return result;
}

static enum bp_result get_disp_caps_v4_4(
	struct bios_parser *bp,
	uint8_t *dce_caps)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_4 *disp_cntl_tbl = NULL;

	if (!dce_caps)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl = GET_IMAGE(struct atom_display_controller_info_v4_4,
							DATA_TABLES(dce_info));

	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	*dce_caps = disp_cntl_tbl->display_caps;

	return result;
}

static enum bp_result get_disp_caps_v4_5(
	struct bios_parser *bp,
	uint8_t *dce_caps)
{
	enum bp_result result = BP_RESULT_OK;
	struct atom_display_controller_info_v4_5 *disp_cntl_tbl = NULL;

	if (!dce_caps)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_BADBIOSTABLE;

	disp_cntl_tbl = GET_IMAGE(struct atom_display_controller_info_v4_5,
							DATA_TABLES(dce_info));

	if (!disp_cntl_tbl)
		return BP_RESULT_BADBIOSTABLE;

	*dce_caps = disp_cntl_tbl->display_caps;

	return result;
}

static enum bp_result bios_parser_get_lttpr_interop(
	struct dc_bios *dcb,
	uint8_t *dce_caps)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_UNSUPPORTED;

	header = GET_IMAGE(struct atom_common_table_header,
						DATA_TABLES(dce_info));
	get_atom_data_table_revision(header, &tbl_revision);
	switch (tbl_revision.major) {
	case 4:
		switch (tbl_revision.minor) {
		case 1:
			result = get_disp_caps_v4_1(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE);
			break;
		case 2:
			result = get_disp_caps_v4_2(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE);
			break;
		case 3:
			result = get_disp_caps_v4_3(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE);
			break;
		case 4:
			result = get_disp_caps_v4_4(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE);
			break;
		case 5:
			result = get_disp_caps_v4_5(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE);
			break;

		default:
			break;
		}
		break;
	default:
		break;
	}
	DC_LOG_BIOS("DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE: %d tbl_revision.major = %d tbl_revision.minor = %d\n", *dce_caps, tbl_revision.major, tbl_revision.minor);
	return result;
}

static enum bp_result bios_parser_get_lttpr_caps(
	struct dc_bios *dcb,
	uint8_t *dce_caps)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;

	if (!DATA_TABLES(dce_info))
		return BP_RESULT_UNSUPPORTED;

	*dce_caps  = 0;
	header = GET_IMAGE(struct atom_common_table_header,
						DATA_TABLES(dce_info));
	get_atom_data_table_revision(header, &tbl_revision);
	switch (tbl_revision.major) {
	case 4:
		switch (tbl_revision.minor) {
		case 1:
			result = get_disp_caps_v4_1(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE);
			break;
		case 2:
			result = get_disp_caps_v4_2(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE);
			break;
		case 3:
			result = get_disp_caps_v4_3(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE);
			break;
		case 4:
			result = get_disp_caps_v4_4(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE);
			break;
		case 5:
			result = get_disp_caps_v4_5(bp, dce_caps);
			*dce_caps = !!(*dce_caps & DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	DC_LOG_BIOS("DCE_INFO_CAPS_LTTPR_SUPPORT_ENABLE: %d tbl_revision.major = %d tbl_revision.minor = %d\n", *dce_caps, tbl_revision.major, tbl_revision.minor);
	if (dcb->ctx->dc->config.force_bios_enable_lttpr && *dce_caps == 0) {
		*dce_caps = 1;
		DC_LOG_BIOS("DCE_INFO_CAPS_VBIOS_LTTPR_TRANSPARENT_ENABLE: forced enabled");
	}
	return result;
}

static enum bp_result get_embedded_panel_info_v2_1(
		struct bios_parser *bp,
		struct embedded_panel_info *info)
{
	struct lcd_info_v2_1 *lvds;

	if (!info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(lcd_info))
		return BP_RESULT_UNSUPPORTED;

	lvds = GET_IMAGE(struct lcd_info_v2_1, DATA_TABLES(lcd_info));

	if (!lvds)
		return BP_RESULT_BADBIOSTABLE;

	/* TODO: previous vv1_3, should v2_1 */
	if (!((lvds->table_header.format_revision == 2)
			&& (lvds->table_header.content_revision >= 1)))
		return BP_RESULT_UNSUPPORTED;

	memset(info, 0, sizeof(struct embedded_panel_info));

	/* We need to convert from 10KHz units into KHz units */
	info->lcd_timing.pixel_clk = le16_to_cpu(lvds->lcd_timing.pixclk) * 10;
	/* usHActive does not include borders, according to VBIOS team */
	info->lcd_timing.horizontal_addressable = le16_to_cpu(lvds->lcd_timing.h_active);
	/* usHBlanking_Time includes borders, so we should really be
	 * subtractingborders duing this translation, but LVDS generally
	 * doesn't have borders, so we should be okay leaving this as is for
	 * now.  May need to revisit if we ever have LVDS with borders
	 */
	info->lcd_timing.horizontal_blanking_time = le16_to_cpu(lvds->lcd_timing.h_blanking_time);
	/* usVActive does not include borders, according to VBIOS team*/
	info->lcd_timing.vertical_addressable = le16_to_cpu(lvds->lcd_timing.v_active);
	/* usVBlanking_Time includes borders, so we should really be
	 * subtracting borders duing this translation, but LVDS generally
	 * doesn't have borders, so we should be okay leaving this as is for
	 * now. May need to revisit if we ever have LVDS with borders
	 */
	info->lcd_timing.vertical_blanking_time = le16_to_cpu(lvds->lcd_timing.v_blanking_time);
	info->lcd_timing.horizontal_sync_offset = le16_to_cpu(lvds->lcd_timing.h_sync_offset);
	info->lcd_timing.horizontal_sync_width = le16_to_cpu(lvds->lcd_timing.h_sync_width);
	info->lcd_timing.vertical_sync_offset = le16_to_cpu(lvds->lcd_timing.v_sync_offset);
	info->lcd_timing.vertical_sync_width = le16_to_cpu(lvds->lcd_timing.v_syncwidth);
	info->lcd_timing.horizontal_border = lvds->lcd_timing.h_border;
	info->lcd_timing.vertical_border = lvds->lcd_timing.v_border;

	/* not provided by VBIOS */
	info->lcd_timing.misc_info.HORIZONTAL_CUT_OFF = 0;

	info->lcd_timing.misc_info.H_SYNC_POLARITY = ~(uint32_t) (lvds->lcd_timing.miscinfo
			& ATOM_HSYNC_POLARITY);
	info->lcd_timing.misc_info.V_SYNC_POLARITY = ~(uint32_t) (lvds->lcd_timing.miscinfo
			& ATOM_VSYNC_POLARITY);

	/* not provided by VBIOS */
	info->lcd_timing.misc_info.VERTICAL_CUT_OFF = 0;

	info->lcd_timing.misc_info.H_REPLICATION_BY2 = !!(lvds->lcd_timing.miscinfo
			& ATOM_H_REPLICATIONBY2);
	info->lcd_timing.misc_info.V_REPLICATION_BY2 = !!(lvds->lcd_timing.miscinfo
			& ATOM_V_REPLICATIONBY2);
	info->lcd_timing.misc_info.COMPOSITE_SYNC = !!(lvds->lcd_timing.miscinfo
			& ATOM_COMPOSITESYNC);
	info->lcd_timing.misc_info.INTERLACE = !!(lvds->lcd_timing.miscinfo & ATOM_INTERLACE);

	/* not provided by VBIOS*/
	info->lcd_timing.misc_info.DOUBLE_CLOCK = 0;
	/* not provided by VBIOS*/
	info->ss_id = 0;

	info->realtek_eDPToLVDS = !!(lvds->dplvdsrxid == eDP_TO_LVDS_REALTEK_ID);

	return BP_RESULT_OK;
}

static enum bp_result bios_parser_get_embedded_panel_info(
		struct dc_bios *dcb,
		struct embedded_panel_info *info)
{
	struct bios_parser
	*bp = BP_FROM_DCB(dcb);
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;

	if (!DATA_TABLES(lcd_info))
		return BP_RESULT_FAILURE;

	header = GET_IMAGE(struct atom_common_table_header, DATA_TABLES(lcd_info));

	if (!header)
		return BP_RESULT_BADBIOSTABLE;

	get_atom_data_table_revision(header, &tbl_revision);

	switch (tbl_revision.major) {
	case 2:
		switch (tbl_revision.minor) {
		case 1:
			return get_embedded_panel_info_v2_1(bp, info);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return BP_RESULT_FAILURE;
}

static uint32_t get_support_mask_for_device_id(struct device_id device_id)
{
	enum dal_device_type device_type = device_id.device_type;
	uint32_t enum_id = device_id.enum_id;

	switch (device_type) {
	case DEVICE_TYPE_LCD:
		switch (enum_id) {
		case 1:
			return ATOM_DISPLAY_LCD1_SUPPORT;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_DFP:
		switch (enum_id) {
		case 1:
			return ATOM_DISPLAY_DFP1_SUPPORT;
		case 2:
			return ATOM_DISPLAY_DFP2_SUPPORT;
		case 3:
			return ATOM_DISPLAY_DFP3_SUPPORT;
		case 4:
			return ATOM_DISPLAY_DFP4_SUPPORT;
		case 5:
			return ATOM_DISPLAY_DFP5_SUPPORT;
		case 6:
			return ATOM_DISPLAY_DFP6_SUPPORT;
		default:
			break;
		}
		break;
	default:
		break;
	}

	/* Unidentified device ID, return empty support mask. */
	return 0;
}

static bool bios_parser_is_device_id_supported(
	struct dc_bios *dcb,
	struct device_id id)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	uint32_t mask = get_support_mask_for_device_id(id);

	switch (bp->object_info_tbl.revision.minor) {
	case 4:
	default:
		return (le16_to_cpu(bp->object_info_tbl.v1_4->supporteddevices) & mask) != 0;
		break;
	case 5:
		return (le16_to_cpu(bp->object_info_tbl.v1_5->supporteddevices) & mask) != 0;
		break;
	}
}

static uint32_t bios_parser_get_ss_entry_number(
	struct dc_bios *dcb,
	enum as_signal_type signal)
{
	/* TODO: DAL2 atomfirmware implementation does not need this.
	 * why DAL3 need this?
	 */
	return 1;
}

static enum bp_result bios_parser_transmitter_control(
	struct dc_bios *dcb,
	struct bp_transmitter_control *cntl)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.transmitter_control)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.transmitter_control(bp, cntl);
}

static enum bp_result bios_parser_encoder_control(
	struct dc_bios *dcb,
	struct bp_encoder_control *cntl)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.dig_encoder_control)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.dig_encoder_control(bp, cntl);
}

static enum bp_result bios_parser_set_pixel_clock(
	struct dc_bios *dcb,
	struct bp_pixel_clock_parameters *bp_params)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.set_pixel_clock)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.set_pixel_clock(bp, bp_params);
}

static enum bp_result bios_parser_set_dce_clock(
	struct dc_bios *dcb,
	struct bp_set_dce_clock_parameters *bp_params)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.set_dce_clock)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.set_dce_clock(bp, bp_params);
}

static enum bp_result bios_parser_program_crtc_timing(
	struct dc_bios *dcb,
	struct bp_hw_crtc_timing_parameters *bp_params)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.set_crtc_timing)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.set_crtc_timing(bp, bp_params);
}

static enum bp_result bios_parser_enable_crtc(
	struct dc_bios *dcb,
	enum controller_id id,
	bool enable)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.enable_crtc)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.enable_crtc(bp, id, enable);
}

static enum bp_result bios_parser_enable_disp_power_gating(
	struct dc_bios *dcb,
	enum controller_id controller_id,
	enum bp_pipe_control_action action)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.enable_disp_power_gating)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.enable_disp_power_gating(bp, controller_id,
		action);
}

static enum bp_result bios_parser_enable_lvtma_control(
	struct dc_bios *dcb,
	uint8_t uc_pwr_on,
	uint8_t pwrseq_instance,
	uint8_t bypass_panel_control_wait)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.enable_lvtma_control)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.enable_lvtma_control(bp, uc_pwr_on, pwrseq_instance, bypass_panel_control_wait);
}

static bool bios_parser_is_accelerated_mode(
	struct dc_bios *dcb)
{
	return bios_is_accelerated_mode(dcb);
}

/**
 * bios_parser_set_scratch_critical_state - update critical state bit
 *                                          in VBIOS scratch register
 *
 * @dcb:   pointer to the DC BIO
 * @state: set or reset state
 */
static void bios_parser_set_scratch_critical_state(
	struct dc_bios *dcb,
	bool state)
{
	bios_set_scratch_critical_state(dcb, state);
}

static enum bp_result bios_parser_get_firmware_info(
	struct dc_bios *dcb,
	struct dc_firmware_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	static enum bp_result result = BP_RESULT_BADBIOSTABLE;
	struct atom_common_table_header *header;

	struct atom_data_revision revision;

	if (info && DATA_TABLES(firmwareinfo)) {
		header = GET_IMAGE(struct atom_common_table_header,
				DATA_TABLES(firmwareinfo));
		get_atom_data_table_revision(header, &revision);
		switch (revision.major) {
		case 3:
			switch (revision.minor) {
			case 1:
				result = get_firmware_info_v3_1(bp, info);
				break;
			case 2:
			case 3:
				result = get_firmware_info_v3_2(bp, info);
				break;
			case 4:
				result = get_firmware_info_v3_4(bp, info);
				break;
			case 5:
				result = get_firmware_info_v3_5(bp, info);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	return result;
}

static enum bp_result get_firmware_info_v3_1(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	struct atom_firmware_info_v3_1 *firmware_info;
	struct atom_display_controller_info_v4_1 *dce_info = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	firmware_info = GET_IMAGE(struct atom_firmware_info_v3_1,
			DATA_TABLES(firmwareinfo));

	dce_info = GET_IMAGE(struct atom_display_controller_info_v4_1,
			DATA_TABLES(dce_info));

	if (!firmware_info || !dce_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	/* Pixel clock pll information. */
	 /* We need to convert from 10KHz units into KHz units */
	info->default_memory_clk = firmware_info->bootup_mclk_in10khz * 10;
	info->default_engine_clk = firmware_info->bootup_sclk_in10khz * 10;

	 /* 27MHz for Vega10: */
	info->pll_info.crystal_frequency = dce_info->dce_refclk_10khz * 10;

	/* Hardcode frequency if BIOS gives no DCE Ref Clk */
	if (info->pll_info.crystal_frequency == 0)
		info->pll_info.crystal_frequency = 27000;
	/*dp_phy_ref_clk is not correct for atom_display_controller_info_v4_2, but we don't use it*/
	info->dp_phy_ref_clk     = dce_info->dpphy_refclk_10khz * 10;
	info->i2c_engine_ref_clk = dce_info->i2c_engine_refclk_10khz * 10;

	/* Get GPU PLL VCO Clock */

	if (bp->cmd_tbl.get_smu_clock_info != NULL) {
		/* VBIOS gives in 10KHz */
		info->smu_gpu_pll_output_freq =
				bp->cmd_tbl.get_smu_clock_info(bp, SMU9_SYSPLL0_ID) * 10;
	}

	info->oem_i2c_present = false;

	return BP_RESULT_OK;
}

static enum bp_result get_firmware_info_v3_2(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	struct atom_firmware_info_v3_2 *firmware_info;
	struct atom_display_controller_info_v4_1 *dce_info = NULL;
	struct atom_common_table_header *header;
	struct atom_data_revision revision;
	struct atom_smu_info_v3_2 *smu_info_v3_2 = NULL;
	struct atom_smu_info_v3_3 *smu_info_v3_3 = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	firmware_info = GET_IMAGE(struct atom_firmware_info_v3_2,
			DATA_TABLES(firmwareinfo));

	dce_info = GET_IMAGE(struct atom_display_controller_info_v4_1,
			DATA_TABLES(dce_info));

	if (!firmware_info || !dce_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(smu_info));
	get_atom_data_table_revision(header, &revision);

	if (revision.minor == 2) {
		/* Vega12 */
		smu_info_v3_2 = GET_IMAGE(struct atom_smu_info_v3_2,
							DATA_TABLES(smu_info));
		if (!smu_info_v3_2)
			return BP_RESULT_BADBIOSTABLE;

		DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", smu_info_v3_2->gpuclk_ss_percentage);

		info->default_engine_clk = smu_info_v3_2->bootup_dcefclk_10khz * 10;
	} else if (revision.minor == 3) {
		/* Vega20 */
		smu_info_v3_3 = GET_IMAGE(struct atom_smu_info_v3_3,
							DATA_TABLES(smu_info));
		if (!smu_info_v3_3)
			return BP_RESULT_BADBIOSTABLE;

		DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", smu_info_v3_3->gpuclk_ss_percentage);

		info->default_engine_clk = smu_info_v3_3->bootup_dcefclk_10khz * 10;
	}

	 // We need to convert from 10KHz units into KHz units.
	info->default_memory_clk = firmware_info->bootup_mclk_in10khz * 10;

	 /* 27MHz for Vega10 & Vega12; 100MHz for Vega20 */
	info->pll_info.crystal_frequency = dce_info->dce_refclk_10khz * 10;
	/* Hardcode frequency if BIOS gives no DCE Ref Clk */
	if (info->pll_info.crystal_frequency == 0) {
		if (revision.minor == 2)
			info->pll_info.crystal_frequency = 27000;
		else if (revision.minor == 3)
			info->pll_info.crystal_frequency = 100000;
	}
	/*dp_phy_ref_clk is not correct for atom_display_controller_info_v4_2, but we don't use it*/
	info->dp_phy_ref_clk     = dce_info->dpphy_refclk_10khz * 10;
	info->i2c_engine_ref_clk = dce_info->i2c_engine_refclk_10khz * 10;

	/* Get GPU PLL VCO Clock */
	if (bp->cmd_tbl.get_smu_clock_info != NULL) {
		if (revision.minor == 2)
			info->smu_gpu_pll_output_freq =
					bp->cmd_tbl.get_smu_clock_info(bp, SMU9_SYSPLL0_ID) * 10;
		else if (revision.minor == 3)
			info->smu_gpu_pll_output_freq =
					bp->cmd_tbl.get_smu_clock_info(bp, SMU11_SYSPLL3_0_ID) * 10;
	}

	if (firmware_info->board_i2c_feature_id == 0x2) {
		info->oem_i2c_present = true;
		info->oem_i2c_obj_id = firmware_info->board_i2c_feature_gpio_id;
	} else {
		info->oem_i2c_present = false;
	}

	return BP_RESULT_OK;
}

static enum bp_result get_firmware_info_v3_4(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	struct atom_firmware_info_v3_4 *firmware_info;
	struct atom_common_table_header *header;
	struct atom_data_revision revision;
	struct atom_display_controller_info_v4_1 *dce_info_v4_1 = NULL;
	struct atom_display_controller_info_v4_4 *dce_info_v4_4 = NULL;

	struct atom_smu_info_v3_5 *smu_info_v3_5 = NULL;
	struct atom_display_controller_info_v4_5 *dce_info_v4_5 = NULL;
	struct atom_smu_info_v4_0 *smu_info_v4_0 = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	firmware_info = GET_IMAGE(struct atom_firmware_info_v3_4,
			DATA_TABLES(firmwareinfo));

	if (!firmware_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(dce_info));

	get_atom_data_table_revision(header, &revision);

	switch (revision.major) {
	case 4:
		switch (revision.minor) {
		case 5:
			dce_info_v4_5 = GET_IMAGE(struct atom_display_controller_info_v4_5,
							DATA_TABLES(dce_info));

			if (!dce_info_v4_5)
				return BP_RESULT_BADBIOSTABLE;

			 /* 100MHz expected */
			info->pll_info.crystal_frequency = dce_info_v4_5->dce_refclk_10khz * 10;
			info->dp_phy_ref_clk             = dce_info_v4_5->dpphy_refclk_10khz * 10;
			 /* 50MHz expected */
			info->i2c_engine_ref_clk         = dce_info_v4_5->i2c_engine_refclk_10khz * 10;

			/* For DCN32/321 Display PLL VCO Frequency from dce_info_v4_5 may not be reliable */
			break;

		case 4:
			dce_info_v4_4 = GET_IMAGE(struct atom_display_controller_info_v4_4,
							DATA_TABLES(dce_info));

			if (!dce_info_v4_4)
				return BP_RESULT_BADBIOSTABLE;

			/* 100MHz expected */
			info->pll_info.crystal_frequency = dce_info_v4_4->dce_refclk_10khz * 10;
			info->dp_phy_ref_clk             = dce_info_v4_4->dpphy_refclk_10khz * 10;
			/* 50MHz expected */
			info->i2c_engine_ref_clk         = dce_info_v4_4->i2c_engine_refclk_10khz * 10;

			/* Get SMU Display PLL VCO Frequency in KHz*/
			info->smu_gpu_pll_output_freq =	dce_info_v4_4->dispclk_pll_vco_freq * 10;
			break;

		default:
			/* should not come here, keep as backup, as was before */
			dce_info_v4_1 = GET_IMAGE(struct atom_display_controller_info_v4_1,
							DATA_TABLES(dce_info));

			if (!dce_info_v4_1)
				return BP_RESULT_BADBIOSTABLE;

			info->pll_info.crystal_frequency = dce_info_v4_1->dce_refclk_10khz * 10;
			info->dp_phy_ref_clk             = dce_info_v4_1->dpphy_refclk_10khz * 10;
			info->i2c_engine_ref_clk         = dce_info_v4_1->i2c_engine_refclk_10khz * 10;
			break;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(smu_info));
	get_atom_data_table_revision(header, &revision);

	switch (revision.major) {
	case 3:
		switch (revision.minor) {
		case 5:
			smu_info_v3_5 = GET_IMAGE(struct atom_smu_info_v3_5,
							DATA_TABLES(smu_info));

			if (!smu_info_v3_5)
				return BP_RESULT_BADBIOSTABLE;
			DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", smu_info_v3_5->gpuclk_ss_percentage);
			info->default_engine_clk = smu_info_v3_5->bootup_dcefclk_10khz * 10;
			break;

		default:
			break;
		}
		break;

	case 4:
		switch (revision.minor) {
		case 0:
			smu_info_v4_0 = GET_IMAGE(struct atom_smu_info_v4_0,
							DATA_TABLES(smu_info));

			if (!smu_info_v4_0)
				return BP_RESULT_BADBIOSTABLE;

			/* For DCN32/321 bootup DCFCLK from smu_info_v4_0 may not be reliable */
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	 // We need to convert from 10KHz units into KHz units.
	info->default_memory_clk = firmware_info->bootup_mclk_in10khz * 10;

	if (firmware_info->board_i2c_feature_id == 0x2) {
		info->oem_i2c_present = true;
		info->oem_i2c_obj_id = firmware_info->board_i2c_feature_gpio_id;
	} else {
		info->oem_i2c_present = false;
	}

	return BP_RESULT_OK;
}

static enum bp_result get_firmware_info_v3_5(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	struct atom_firmware_info_v3_5 *firmware_info;
	struct atom_common_table_header *header;
	struct atom_data_revision revision;
	struct atom_display_controller_info_v4_5 *dce_info_v4_5 = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	firmware_info = GET_IMAGE(struct atom_firmware_info_v3_5,
			DATA_TABLES(firmwareinfo));

	if (!firmware_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	if (firmware_info->board_i2c_feature_id == 0x2) {
		info->oem_i2c_present = true;
		info->oem_i2c_obj_id = firmware_info->board_i2c_feature_gpio_id;
	} else {
		info->oem_i2c_present = false;
	}

	header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(dce_info));

	get_atom_data_table_revision(header, &revision);

	switch (revision.major) {
	case 4:
		switch (revision.minor) {
		case 5:
			dce_info_v4_5 = GET_IMAGE(struct atom_display_controller_info_v4_5,
							DATA_TABLES(dce_info));

			if (!dce_info_v4_5)
				return BP_RESULT_BADBIOSTABLE;

			 /* 100MHz expected */
			info->pll_info.crystal_frequency = dce_info_v4_5->dce_refclk_10khz * 10;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}


	return BP_RESULT_OK;
}

static enum bp_result bios_parser_get_encoder_cap_info(
	struct dc_bios *dcb,
	struct graphics_object_id object_id,
	struct bp_encoder_cap_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_display_object_path_v2 *object;
	struct atom_encoder_caps_record *record = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

#if defined(CONFIG_DRM_AMD_DC_FP)
	/* encoder cap record not available in v1_5 */
	if (bp->object_info_tbl.revision.minor == 5)
		return BP_RESULT_NORECORD;
#endif

	object = get_bios_object(bp, object_id);

	if (!object)
		return BP_RESULT_BADINPUT;

	record = get_encoder_cap_record(bp, object);
	if (!record)
		return BP_RESULT_NORECORD;
	DC_LOG_BIOS("record->encodercaps 0x%x for object_id 0x%x", record->encodercaps, object_id.id);

	info->DP_HBR2_CAP = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_HBR2) ? 1 : 0;
	info->DP_HBR2_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_HBR2_EN) ? 1 : 0;
	info->DP_HBR3_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_HBR3_EN) ? 1 : 0;
	info->HDMI_6GB_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_HDMI6Gbps_EN) ? 1 : 0;
	info->IS_DP2_CAPABLE = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_DP2) ? 1 : 0;
	info->DP_UHBR10_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_UHBR10_EN) ? 1 : 0;
	info->DP_UHBR13_5_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_UHBR13_5_EN) ? 1 : 0;
	info->DP_UHBR20_EN = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_UHBR20_EN) ? 1 : 0;
	info->DP_IS_USB_C = (record->encodercaps &
			ATOM_ENCODER_CAP_RECORD_USB_C_TYPE) ? 1 : 0;
	DC_LOG_BIOS("\t info->DP_IS_USB_C %d", info->DP_IS_USB_C);

	return BP_RESULT_OK;
}


static struct atom_encoder_caps_record *get_encoder_cap_record(
	struct bios_parser *bp,
	struct atom_display_object_path_v2 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = object->encoder_recordoffset + bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		offset += header->record_size;

		if (header->record_type == LAST_RECORD_TYPE ||
				!header->record_size)
			break;

		if (header->record_type != ATOM_ENCODER_CAP_RECORD_TYPE)
			continue;

		if (sizeof(struct atom_encoder_caps_record) <=
							header->record_size)
			return (struct atom_encoder_caps_record *)header;
	}

	return NULL;
}

static struct atom_disp_connector_caps_record *get_disp_connector_caps_record(
	struct bios_parser *bp,
	struct atom_display_object_path_v2 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = object->disp_recordoffset + bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		offset += header->record_size;

		if (header->record_type == LAST_RECORD_TYPE ||
				!header->record_size)
			break;

		if (header->record_type != ATOM_DISP_CONNECTOR_CAPS_RECORD_TYPE)
			continue;

		if (sizeof(struct atom_disp_connector_caps_record) <=
							header->record_size)
			return (struct atom_disp_connector_caps_record *)header;
	}

	return NULL;
}

static struct atom_connector_caps_record *get_connector_caps_record(struct bios_parser *bp,
								    struct atom_display_object_path_v3 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = object->disp_recordoffset + bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		offset += header->record_size;

		if (header->record_type == ATOM_RECORD_END_TYPE ||
				!header->record_size)
			break;

		if (header->record_type != ATOM_CONNECTOR_CAP_RECORD_TYPE)
			continue;

		if (sizeof(struct atom_connector_caps_record) <= header->record_size)
			return (struct atom_connector_caps_record *)header;
	}

	return NULL;
}

static enum bp_result bios_parser_get_disp_connector_caps_info(
	struct dc_bios *dcb,
	struct graphics_object_id object_id,
	struct bp_disp_connector_caps_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_display_object_path_v2 *object;
	struct atom_display_object_path_v3 *object_path_v3;
	struct atom_connector_caps_record *record_path_v3;
	struct atom_disp_connector_caps_record *record = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	switch (bp->object_info_tbl.revision.minor) {
	case 4:
		default:
			object = get_bios_object(bp, object_id);

			if (!object)
				return BP_RESULT_BADINPUT;

			record = get_disp_connector_caps_record(bp, object);
			if (!record)
				return BP_RESULT_NORECORD;

			info->INTERNAL_DISPLAY =
				(record->connectcaps & ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY) ? 1 : 0;
			info->INTERNAL_DISPLAY_BL =
				(record->connectcaps & ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY_BL) ? 1 : 0;
			break;
	case 5:
		object_path_v3 = get_bios_object_from_path_v3(bp, object_id);

		if (!object_path_v3)
			return BP_RESULT_BADINPUT;

		record_path_v3 = get_connector_caps_record(bp, object_path_v3);
		if (!record_path_v3)
			return BP_RESULT_NORECORD;

		info->INTERNAL_DISPLAY = (record_path_v3->connector_caps & ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY)
									? 1 : 0;
		info->INTERNAL_DISPLAY_BL = (record_path_v3->connector_caps & ATOM_CONNECTOR_CAP_INTERNAL_DISPLAY_BL)
										? 1 : 0;
		break;
	}

	return BP_RESULT_OK;
}

static struct atom_connector_speed_record *get_connector_speed_cap_record(struct bios_parser *bp,
									  struct atom_display_object_path_v3 *object)
{
	struct atom_common_record_header *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = object->disp_recordoffset + bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(struct atom_common_record_header, offset);

		if (!header)
			return NULL;

		offset += header->record_size;

		if (header->record_type == ATOM_RECORD_END_TYPE ||
				!header->record_size)
			break;

		if (header->record_type != ATOM_CONNECTOR_SPEED_UPTO)
			continue;

		if (sizeof(struct atom_connector_speed_record) <= header->record_size)
			return (struct atom_connector_speed_record *)header;
	}

	return NULL;
}

static enum bp_result bios_parser_get_connector_speed_cap_info(
	struct dc_bios *dcb,
	struct graphics_object_id object_id,
	struct bp_connector_speed_cap_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct atom_display_object_path_v3 *object_path_v3;
	//struct atom_connector_speed_record *record = NULL;
	struct atom_connector_speed_record *record;

	if (!info)
		return BP_RESULT_BADINPUT;

	object_path_v3 = get_bios_object_from_path_v3(bp, object_id);

	if (!object_path_v3)
		return BP_RESULT_BADINPUT;

	record = get_connector_speed_cap_record(bp, object_path_v3);
	if (!record)
		return BP_RESULT_NORECORD;

	info->DP_HBR2_EN = (record->connector_max_speed >= 5400) ? 1 : 0;
	info->DP_HBR3_EN = (record->connector_max_speed >= 8100) ? 1 : 0;
	info->HDMI_6GB_EN = (record->connector_max_speed >= 5940) ? 1 : 0;
	info->DP_UHBR10_EN = (record->connector_max_speed >= 10000) ? 1 : 0;
	info->DP_UHBR13_5_EN = (record->connector_max_speed >= 13500) ? 1 : 0;
	info->DP_UHBR20_EN = (record->connector_max_speed >= 20000) ? 1 : 0;
	return BP_RESULT_OK;
}

static enum bp_result get_vram_info_v23(
	struct bios_parser *bp,
	struct dc_vram_info *info)
{
	struct atom_vram_info_header_v2_3 *info_v23;
	static enum bp_result result = BP_RESULT_OK;

	info_v23 = GET_IMAGE(struct atom_vram_info_header_v2_3,
						DATA_TABLES(vram_info));

	if (info_v23 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	info->num_chans = info_v23->vram_module[0].channel_num;
	info->dram_channel_width_bytes = (1 << info_v23->vram_module[0].channel_width) / 8;

	return result;
}

static enum bp_result get_vram_info_v24(
	struct bios_parser *bp,
	struct dc_vram_info *info)
{
	struct atom_vram_info_header_v2_4 *info_v24;
	static enum bp_result result = BP_RESULT_OK;

	info_v24 = GET_IMAGE(struct atom_vram_info_header_v2_4,
						DATA_TABLES(vram_info));

	if (info_v24 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	info->num_chans = info_v24->vram_module[0].channel_num;
	info->dram_channel_width_bytes = (1 << info_v24->vram_module[0].channel_width) / 8;

	return result;
}

static enum bp_result get_vram_info_v25(
	struct bios_parser *bp,
	struct dc_vram_info *info)
{
	struct atom_vram_info_header_v2_5 *info_v25;
	static enum bp_result result = BP_RESULT_OK;

	info_v25 = GET_IMAGE(struct atom_vram_info_header_v2_5,
						DATA_TABLES(vram_info));

	if (info_v25 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	info->num_chans = info_v25->vram_module[0].channel_num;
	info->dram_channel_width_bytes = (1 << info_v25->vram_module[0].channel_width) / 8;

	return result;
}

static enum bp_result get_vram_info_v30(
	struct bios_parser *bp,
	struct dc_vram_info *info)
{
	struct atom_vram_info_header_v3_0 *info_v30;
	enum bp_result result = BP_RESULT_OK;

	info_v30 = GET_IMAGE(struct atom_vram_info_header_v3_0,
						DATA_TABLES(vram_info));

	if (info_v30 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	info->num_chans = info_v30->channel_num;
	info->dram_channel_width_bytes = (1 << info_v30->channel_width) / 8;

	return result;
}

static enum bp_result get_vram_info_from_umc_info_v40(
		struct bios_parser *bp,
		struct dc_vram_info *info)
{
	struct atom_umc_info_v4_0 *info_v40;
	enum bp_result result = BP_RESULT_OK;

	info_v40 = GET_IMAGE(struct atom_umc_info_v4_0,
						DATA_TABLES(umc_info));

	if (info_v40 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	info->num_chans = info_v40->channel_num;
	info->dram_channel_width_bytes = (1 << info_v40->channel_width) / 8;

	return result;
}

/*
 * get_integrated_info_v11
 *
 * @brief
 * Get V8 integrated BIOS information
 *
 * @param
 * bios_parser *bp - [in]BIOS parser handler to get master data table
 * integrated_info *info - [out] store and output integrated info
 *
 * @return
 * static enum bp_result - BP_RESULT_OK if information is available,
 *                  BP_RESULT_BADBIOSTABLE otherwise.
 */
static enum bp_result get_integrated_info_v11(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	struct atom_integrated_system_info_v1_11 *info_v11;
	uint32_t i;

	info_v11 = GET_IMAGE(struct atom_integrated_system_info_v1_11,
					DATA_TABLES(integratedsysteminfo));

	if (info_v11 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", info_v11->gpuclk_ss_percentage);

	info->gpu_cap_info =
	le32_to_cpu(info_v11->gpucapinfo);
	/*
	* system_config: Bit[0] = 0 : PCIE power gating disabled
	*                       = 1 : PCIE power gating enabled
	*                Bit[1] = 0 : DDR-PLL shut down disabled
	*                       = 1 : DDR-PLL shut down enabled
	*                Bit[2] = 0 : DDR-PLL power down disabled
	*                       = 1 : DDR-PLL power down enabled
	*/
	info->system_config = le32_to_cpu(info_v11->system_config);
	info->cpu_cap_info = le32_to_cpu(info_v11->cpucapinfo);
	info->memory_type = info_v11->memorytype;
	info->ma_channel_number = info_v11->umachannelnumber;
	info->lvds_ss_percentage =
	le16_to_cpu(info_v11->lvds_ss_percentage);
	info->dp_ss_control =
	le16_to_cpu(info_v11->reserved1);
	info->lvds_sspread_rate_in_10hz =
	le16_to_cpu(info_v11->lvds_ss_rate_10hz);
	info->hdmi_ss_percentage =
	le16_to_cpu(info_v11->hdmi_ss_percentage);
	info->hdmi_sspread_rate_in_10hz =
	le16_to_cpu(info_v11->hdmi_ss_rate_10hz);
	info->dvi_ss_percentage =
	le16_to_cpu(info_v11->dvi_ss_percentage);
	info->dvi_sspread_rate_in_10_hz =
	le16_to_cpu(info_v11->dvi_ss_rate_10hz);
	info->lvds_misc = info_v11->lvds_misc;
	for (i = 0; i < NUMBER_OF_UCHAR_FOR_GUID; ++i) {
		info->ext_disp_conn_info.gu_id[i] =
				info_v11->extdispconninfo.guid[i];
	}

	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; ++i) {
		info->ext_disp_conn_info.path[i].device_connector_id =
		object_id_from_bios_object_id(
		le16_to_cpu(info_v11->extdispconninfo.path[i].connectorobjid));

		info->ext_disp_conn_info.path[i].ext_encoder_obj_id =
		object_id_from_bios_object_id(
			le16_to_cpu(
			info_v11->extdispconninfo.path[i].ext_encoder_objid));

		info->ext_disp_conn_info.path[i].device_tag =
			le16_to_cpu(
				info_v11->extdispconninfo.path[i].device_tag);
		info->ext_disp_conn_info.path[i].device_acpi_enum =
		le16_to_cpu(
			info_v11->extdispconninfo.path[i].device_acpi_enum);
		info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index =
			info_v11->extdispconninfo.path[i].auxddclut_index;
		info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index =
			info_v11->extdispconninfo.path[i].hpdlut_index;
		info->ext_disp_conn_info.path[i].channel_mapping.raw =
			info_v11->extdispconninfo.path[i].channelmapping;
		info->ext_disp_conn_info.path[i].caps =
				le16_to_cpu(info_v11->extdispconninfo.path[i].caps);
	}
	info->ext_disp_conn_info.checksum =
	info_v11->extdispconninfo.checksum;

	info->dp0_ext_hdmi_slv_addr = info_v11->dp0_retimer_set.HdmiSlvAddr;
	info->dp0_ext_hdmi_reg_num = info_v11->dp0_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp0_ext_hdmi_reg_num; i++) {
		info->dp0_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v11->dp0_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp0_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v11->dp0_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp0_ext_hdmi_6g_reg_num = info_v11->dp0_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp0_ext_hdmi_6g_reg_num; i++) {
		info->dp0_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v11->dp0_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp0_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v11->dp0_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}

	info->dp1_ext_hdmi_slv_addr = info_v11->dp1_retimer_set.HdmiSlvAddr;
	info->dp1_ext_hdmi_reg_num = info_v11->dp1_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp1_ext_hdmi_reg_num; i++) {
		info->dp1_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v11->dp1_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp1_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v11->dp1_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp1_ext_hdmi_6g_reg_num = info_v11->dp1_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp1_ext_hdmi_6g_reg_num; i++) {
		info->dp1_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v11->dp1_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp1_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v11->dp1_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}

	info->dp2_ext_hdmi_slv_addr = info_v11->dp2_retimer_set.HdmiSlvAddr;
	info->dp2_ext_hdmi_reg_num = info_v11->dp2_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp2_ext_hdmi_reg_num; i++) {
		info->dp2_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v11->dp2_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp2_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v11->dp2_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp2_ext_hdmi_6g_reg_num = info_v11->dp2_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp2_ext_hdmi_6g_reg_num; i++) {
		info->dp2_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v11->dp2_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp2_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v11->dp2_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}

	info->dp3_ext_hdmi_slv_addr = info_v11->dp3_retimer_set.HdmiSlvAddr;
	info->dp3_ext_hdmi_reg_num = info_v11->dp3_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp3_ext_hdmi_reg_num; i++) {
		info->dp3_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v11->dp3_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp3_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v11->dp3_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp3_ext_hdmi_6g_reg_num = info_v11->dp3_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp3_ext_hdmi_6g_reg_num; i++) {
		info->dp3_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v11->dp3_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp3_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v11->dp3_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}


	/** TODO - review **/
	#if 0
	info->boot_up_engine_clock = le32_to_cpu(info_v11->ulBootUpEngineClock)
									* 10;
	info->dentist_vco_freq = le32_to_cpu(info_v11->ulDentistVCOFreq) * 10;
	info->boot_up_uma_clock = le32_to_cpu(info_v8->ulBootUpUMAClock) * 10;

	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->disp_clk_voltage[i].max_supported_clk =
		le32_to_cpu(info_v11->sDISPCLK_Voltage[i].
			ulMaximumSupportedCLK) * 10;
		info->disp_clk_voltage[i].voltage_index =
		le32_to_cpu(info_v11->sDISPCLK_Voltage[i].ulVoltageIndex);
	}

	info->boot_up_req_display_vector =
			le32_to_cpu(info_v11->ulBootUpReqDisplayVector);
	info->boot_up_nb_voltage =
			le16_to_cpu(info_v11->usBootUpNBVoltage);
	info->ext_disp_conn_info_offset =
			le16_to_cpu(info_v11->usExtDispConnInfoOffset);
	info->gmc_restore_reset_time =
			le32_to_cpu(info_v11->ulGMCRestoreResetTime);
	info->minimum_n_clk =
			le32_to_cpu(info_v11->ulNbpStateNClkFreq[0]);
	for (i = 1; i < 4; ++i)
		info->minimum_n_clk =
				info->minimum_n_clk <
				le32_to_cpu(info_v11->ulNbpStateNClkFreq[i]) ?
				info->minimum_n_clk : le32_to_cpu(
					info_v11->ulNbpStateNClkFreq[i]);

	info->idle_n_clk = le32_to_cpu(info_v11->ulIdleNClk);
	info->ddr_dll_power_up_time =
	    le32_to_cpu(info_v11->ulDDR_DLL_PowerUpTime);
	info->ddr_pll_power_up_time =
		le32_to_cpu(info_v11->ulDDR_PLL_PowerUpTime);
	info->pcie_clk_ss_type = le16_to_cpu(info_v11->usPCIEClkSSType);
	info->max_lvds_pclk_freq_in_single_link =
		le16_to_cpu(info_v11->usMaxLVDSPclkFreqInSingleLink);
	info->max_lvds_pclk_freq_in_single_link =
		le16_to_cpu(info_v11->usMaxLVDSPclkFreqInSingleLink);
	info->lvds_pwr_on_seq_dig_on_to_de_in_4ms =
		info_v11->ucLVDSPwrOnSeqDIGONtoDE_in4Ms;
	info->lvds_pwr_on_seq_de_to_vary_bl_in_4ms =
		info_v11->ucLVDSPwrOnSeqDEtoVARY_BL_in4Ms;
	info->lvds_pwr_on_seq_vary_bl_to_blon_in_4ms =
		info_v11->ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms;
	info->lvds_pwr_off_seq_vary_bl_to_de_in4ms =
		info_v11->ucLVDSPwrOffSeqVARY_BLtoDE_in4Ms;
	info->lvds_pwr_off_seq_de_to_dig_on_in4ms =
		info_v11->ucLVDSPwrOffSeqDEtoDIGON_in4Ms;
	info->lvds_pwr_off_seq_blon_to_vary_bl_in_4ms =
		info_v11->ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms;
	info->lvds_off_to_on_delay_in_4ms =
		info_v11->ucLVDSOffToOnDelay_in4Ms;
	info->lvds_bit_depth_control_val =
		le32_to_cpu(info_v11->ulLCDBitDepthControlVal);

	for (i = 0; i < NUMBER_OF_AVAILABLE_SCLK; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->avail_s_clk[i].supported_s_clk =
			le32_to_cpu(info_v11->sAvail_SCLK[i].ulSupportedSCLK)
									* 10;
		info->avail_s_clk[i].voltage_index =
			le16_to_cpu(info_v11->sAvail_SCLK[i].usVoltageIndex);
		info->avail_s_clk[i].voltage_id =
			le16_to_cpu(info_v11->sAvail_SCLK[i].usVoltageID);
	}
	#endif /* TODO*/

	return BP_RESULT_OK;
}

static enum bp_result get_integrated_info_v2_1(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	struct atom_integrated_system_info_v2_1 *info_v2_1;
	uint32_t i;

	info_v2_1 = GET_IMAGE(struct atom_integrated_system_info_v2_1,
					DATA_TABLES(integratedsysteminfo));

	if (info_v2_1 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", info_v2_1->gpuclk_ss_percentage);

	info->gpu_cap_info =
	le32_to_cpu(info_v2_1->gpucapinfo);
	/*
	* system_config: Bit[0] = 0 : PCIE power gating disabled
	*                       = 1 : PCIE power gating enabled
	*                Bit[1] = 0 : DDR-PLL shut down disabled
	*                       = 1 : DDR-PLL shut down enabled
	*                Bit[2] = 0 : DDR-PLL power down disabled
	*                       = 1 : DDR-PLL power down enabled
	*/
	info->system_config = le32_to_cpu(info_v2_1->system_config);
	info->cpu_cap_info = le32_to_cpu(info_v2_1->cpucapinfo);
	info->memory_type = info_v2_1->memorytype;
	info->ma_channel_number = info_v2_1->umachannelnumber;
	info->dp_ss_control =
		le16_to_cpu(info_v2_1->reserved1);

	for (i = 0; i < NUMBER_OF_UCHAR_FOR_GUID; ++i) {
		info->ext_disp_conn_info.gu_id[i] =
				info_v2_1->extdispconninfo.guid[i];
	}

	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; ++i) {
		info->ext_disp_conn_info.path[i].device_connector_id =
		object_id_from_bios_object_id(
		le16_to_cpu(info_v2_1->extdispconninfo.path[i].connectorobjid));

		info->ext_disp_conn_info.path[i].ext_encoder_obj_id =
		object_id_from_bios_object_id(
			le16_to_cpu(
			info_v2_1->extdispconninfo.path[i].ext_encoder_objid));

		info->ext_disp_conn_info.path[i].device_tag =
			le16_to_cpu(
				info_v2_1->extdispconninfo.path[i].device_tag);
		info->ext_disp_conn_info.path[i].device_acpi_enum =
		le16_to_cpu(
			info_v2_1->extdispconninfo.path[i].device_acpi_enum);
		info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index =
			info_v2_1->extdispconninfo.path[i].auxddclut_index;
		info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index =
			info_v2_1->extdispconninfo.path[i].hpdlut_index;
		info->ext_disp_conn_info.path[i].channel_mapping.raw =
			info_v2_1->extdispconninfo.path[i].channelmapping;
		info->ext_disp_conn_info.path[i].caps =
				le16_to_cpu(info_v2_1->extdispconninfo.path[i].caps);
	}

	info->ext_disp_conn_info.checksum =
		info_v2_1->extdispconninfo.checksum;
	info->dp0_ext_hdmi_slv_addr = info_v2_1->dp0_retimer_set.HdmiSlvAddr;
	info->dp0_ext_hdmi_reg_num = info_v2_1->dp0_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp0_ext_hdmi_reg_num; i++) {
		info->dp0_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v2_1->dp0_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp0_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v2_1->dp0_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp0_ext_hdmi_6g_reg_num = info_v2_1->dp0_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp0_ext_hdmi_6g_reg_num; i++) {
		info->dp0_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v2_1->dp0_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp0_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v2_1->dp0_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}
	info->dp1_ext_hdmi_slv_addr = info_v2_1->dp1_retimer_set.HdmiSlvAddr;
	info->dp1_ext_hdmi_reg_num = info_v2_1->dp1_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp1_ext_hdmi_reg_num; i++) {
		info->dp1_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v2_1->dp1_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp1_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v2_1->dp1_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp1_ext_hdmi_6g_reg_num = info_v2_1->dp1_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp1_ext_hdmi_6g_reg_num; i++) {
		info->dp1_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v2_1->dp1_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp1_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v2_1->dp1_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}
	info->dp2_ext_hdmi_slv_addr = info_v2_1->dp2_retimer_set.HdmiSlvAddr;
	info->dp2_ext_hdmi_reg_num = info_v2_1->dp2_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp2_ext_hdmi_reg_num; i++) {
		info->dp2_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v2_1->dp2_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp2_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v2_1->dp2_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp2_ext_hdmi_6g_reg_num = info_v2_1->dp2_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp2_ext_hdmi_6g_reg_num; i++) {
		info->dp2_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v2_1->dp2_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp2_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v2_1->dp2_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}
	info->dp3_ext_hdmi_slv_addr = info_v2_1->dp3_retimer_set.HdmiSlvAddr;
	info->dp3_ext_hdmi_reg_num = info_v2_1->dp3_retimer_set.HdmiRegNum;
	for (i = 0; i < info->dp3_ext_hdmi_reg_num; i++) {
		info->dp3_ext_hdmi_reg_settings[i].i2c_reg_index =
				info_v2_1->dp3_retimer_set.HdmiRegSetting[i].ucI2cRegIndex;
		info->dp3_ext_hdmi_reg_settings[i].i2c_reg_val =
				info_v2_1->dp3_retimer_set.HdmiRegSetting[i].ucI2cRegVal;
	}
	info->dp3_ext_hdmi_6g_reg_num = info_v2_1->dp3_retimer_set.Hdmi6GRegNum;
	for (i = 0; i < info->dp3_ext_hdmi_6g_reg_num; i++) {
		info->dp3_ext_hdmi_6g_reg_settings[i].i2c_reg_index =
				info_v2_1->dp3_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegIndex;
		info->dp3_ext_hdmi_6g_reg_settings[i].i2c_reg_val =
				info_v2_1->dp3_retimer_set.Hdmi6GhzRegSetting[i].ucI2cRegVal;
	}

	info->edp1_info.edp_backlight_pwm_hz =
	le16_to_cpu(info_v2_1->edp1_info.edp_backlight_pwm_hz);
	info->edp1_info.edp_ss_percentage =
	le16_to_cpu(info_v2_1->edp1_info.edp_ss_percentage);
	info->edp1_info.edp_ss_rate_10hz =
	le16_to_cpu(info_v2_1->edp1_info.edp_ss_rate_10hz);
	info->edp1_info.edp_pwr_on_off_delay =
		info_v2_1->edp1_info.edp_pwr_on_off_delay;
	info->edp1_info.edp_pwr_on_vary_bl_to_blon =
		info_v2_1->edp1_info.edp_pwr_on_vary_bl_to_blon;
	info->edp1_info.edp_pwr_down_bloff_to_vary_bloff =
		info_v2_1->edp1_info.edp_pwr_down_bloff_to_vary_bloff;
	info->edp1_info.edp_panel_bpc =
		info_v2_1->edp1_info.edp_panel_bpc;
	info->edp1_info.edp_bootup_bl_level = info_v2_1->edp1_info.edp_bootup_bl_level;

	info->edp2_info.edp_backlight_pwm_hz =
	le16_to_cpu(info_v2_1->edp2_info.edp_backlight_pwm_hz);
	info->edp2_info.edp_ss_percentage =
	le16_to_cpu(info_v2_1->edp2_info.edp_ss_percentage);
	info->edp2_info.edp_ss_rate_10hz =
	le16_to_cpu(info_v2_1->edp2_info.edp_ss_rate_10hz);
	info->edp2_info.edp_pwr_on_off_delay =
		info_v2_1->edp2_info.edp_pwr_on_off_delay;
	info->edp2_info.edp_pwr_on_vary_bl_to_blon =
		info_v2_1->edp2_info.edp_pwr_on_vary_bl_to_blon;
	info->edp2_info.edp_pwr_down_bloff_to_vary_bloff =
		info_v2_1->edp2_info.edp_pwr_down_bloff_to_vary_bloff;
	info->edp2_info.edp_panel_bpc =
		info_v2_1->edp2_info.edp_panel_bpc;
	info->edp2_info.edp_bootup_bl_level =
		info_v2_1->edp2_info.edp_bootup_bl_level;

	return BP_RESULT_OK;
}

static enum bp_result get_integrated_info_v2_2(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	struct atom_integrated_system_info_v2_2 *info_v2_2;
	uint32_t i;

	info_v2_2 = GET_IMAGE(struct atom_integrated_system_info_v2_2,
					DATA_TABLES(integratedsysteminfo));

	if (info_v2_2 == NULL)
		return BP_RESULT_BADBIOSTABLE;

	DC_LOG_BIOS("gpuclk_ss_percentage (unit of 0.001 percent): %d\n", info_v2_2->gpuclk_ss_percentage);

	info->gpu_cap_info =
	le32_to_cpu(info_v2_2->gpucapinfo);
	/*
	* system_config: Bit[0] = 0 : PCIE power gating disabled
	*                       = 1 : PCIE power gating enabled
	*                Bit[1] = 0 : DDR-PLL shut down disabled
	*                       = 1 : DDR-PLL shut down enabled
	*                Bit[2] = 0 : DDR-PLL power down disabled
	*                       = 1 : DDR-PLL power down enabled
	*/
	info->system_config = le32_to_cpu(info_v2_2->system_config);
	info->cpu_cap_info = le32_to_cpu(info_v2_2->cpucapinfo);
	info->memory_type = info_v2_2->memorytype;
	info->ma_channel_number = info_v2_2->umachannelnumber;
	info->dp_ss_control =
		le16_to_cpu(info_v2_2->reserved1);
	info->gpuclk_ss_percentage = info_v2_2->gpuclk_ss_percentage;
	info->gpuclk_ss_type = info_v2_2->gpuclk_ss_type;

	for (i = 0; i < NUMBER_OF_UCHAR_FOR_GUID; ++i) {
		info->ext_disp_conn_info.gu_id[i] =
				info_v2_2->extdispconninfo.guid[i];
	}

	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; ++i) {
		info->ext_disp_conn_info.path[i].device_connector_id =
		object_id_from_bios_object_id(
		le16_to_cpu(info_v2_2->extdispconninfo.path[i].connectorobjid));

		info->ext_disp_conn_info.path[i].ext_encoder_obj_id =
		object_id_from_bios_object_id(
			le16_to_cpu(
			info_v2_2->extdispconninfo.path[i].ext_encoder_objid));

		info->ext_disp_conn_info.path[i].device_tag =
			le16_to_cpu(
				info_v2_2->extdispconninfo.path[i].device_tag);
		info->ext_disp_conn_info.path[i].device_acpi_enum =
		le16_to_cpu(
			info_v2_2->extdispconninfo.path[i].device_acpi_enum);
		info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index =
			info_v2_2->extdispconninfo.path[i].auxddclut_index;
		info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index =
			info_v2_2->extdispconninfo.path[i].hpdlut_index;
		info->ext_disp_conn_info.path[i].channel_mapping.raw =
			info_v2_2->extdispconninfo.path[i].channelmapping;
		info->ext_disp_conn_info.path[i].caps =
				le16_to_cpu(info_v2_2->extdispconninfo.path[i].caps);
	}

	info->ext_disp_conn_info.checksum =
		info_v2_2->extdispconninfo.checksum;
	info->ext_disp_conn_info.fixdpvoltageswing =
		info_v2_2->extdispconninfo.fixdpvoltageswing;

	info->edp1_info.edp_backlight_pwm_hz =
	le16_to_cpu(info_v2_2->edp1_info.edp_backlight_pwm_hz);
	info->edp1_info.edp_ss_percentage =
	le16_to_cpu(info_v2_2->edp1_info.edp_ss_percentage);
	info->edp1_info.edp_ss_rate_10hz =
	le16_to_cpu(info_v2_2->edp1_info.edp_ss_rate_10hz);
	info->edp1_info.edp_pwr_on_off_delay =
		info_v2_2->edp1_info.edp_pwr_on_off_delay;
	info->edp1_info.edp_pwr_on_vary_bl_to_blon =
		info_v2_2->edp1_info.edp_pwr_on_vary_bl_to_blon;
	info->edp1_info.edp_pwr_down_bloff_to_vary_bloff =
		info_v2_2->edp1_info.edp_pwr_down_bloff_to_vary_bloff;
	info->edp1_info.edp_panel_bpc =
		info_v2_2->edp1_info.edp_panel_bpc;
	info->edp1_info.edp_bootup_bl_level =

	info->edp2_info.edp_backlight_pwm_hz =
	le16_to_cpu(info_v2_2->edp2_info.edp_backlight_pwm_hz);
	info->edp2_info.edp_ss_percentage =
	le16_to_cpu(info_v2_2->edp2_info.edp_ss_percentage);
	info->edp2_info.edp_ss_rate_10hz =
	le16_to_cpu(info_v2_2->edp2_info.edp_ss_rate_10hz);
	info->edp2_info.edp_pwr_on_off_delay =
		info_v2_2->edp2_info.edp_pwr_on_off_delay;
	info->edp2_info.edp_pwr_on_vary_bl_to_blon =
		info_v2_2->edp2_info.edp_pwr_on_vary_bl_to_blon;
	info->edp2_info.edp_pwr_down_bloff_to_vary_bloff =
		info_v2_2->edp2_info.edp_pwr_down_bloff_to_vary_bloff;
	info->edp2_info.edp_panel_bpc =
		info_v2_2->edp2_info.edp_panel_bpc;
	info->edp2_info.edp_bootup_bl_level =
		info_v2_2->edp2_info.edp_bootup_bl_level;

	return BP_RESULT_OK;
}

/*
 * construct_integrated_info
 *
 * @brief
 * Get integrated BIOS information based on table revision
 *
 * @param
 * bios_parser *bp - [in]BIOS parser handler to get master data table
 * integrated_info *info - [out] store and output integrated info
 *
 * @return
 * static enum bp_result - BP_RESULT_OK if information is available,
 *                  BP_RESULT_BADBIOSTABLE otherwise.
 */
static enum bp_result construct_integrated_info(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	static enum bp_result result = BP_RESULT_BADBIOSTABLE;

	struct atom_common_table_header *header;
	struct atom_data_revision revision;

	int32_t i;
	int32_t j;

	if (!info)
		return result;

	if (info && DATA_TABLES(integratedsysteminfo)) {
		header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(integratedsysteminfo));

		get_atom_data_table_revision(header, &revision);

		switch (revision.major) {
		case 1:
			switch (revision.minor) {
			case 11:
			case 12:
				result = get_integrated_info_v11(bp, info);
				break;
			default:
				return result;
			}
			break;
		case 2:
			switch (revision.minor) {
			case 1:
				result = get_integrated_info_v2_1(bp, info);
				break;
			case 2:
			case 3:
				result = get_integrated_info_v2_2(bp, info);
				break;
			default:
				return result;
			}
			break;
		default:
			return result;
		}
		if (result == BP_RESULT_OK) {

			DC_LOG_BIOS("edp1:\n"
						"\tedp_pwr_on_off_delay = %d\n"
						"\tedp_pwr_on_vary_bl_to_blon = %d\n"
						"\tedp_pwr_down_bloff_to_vary_bloff = %d\n"
						"\tedp_bootup_bl_level = %d\n",
						info->edp1_info.edp_pwr_on_off_delay,
						info->edp1_info.edp_pwr_on_vary_bl_to_blon,
						info->edp1_info.edp_pwr_down_bloff_to_vary_bloff,
						info->edp1_info.edp_bootup_bl_level);
			DC_LOG_BIOS("edp2:\n"
						"\tedp_pwr_on_off_delayv = %d\n"
						"\tedp_pwr_on_vary_bl_to_blon = %d\n"
						"\tedp_pwr_down_bloff_to_vary_bloff = %d\n"
						"\tedp_bootup_bl_level = %d\n",
						info->edp2_info.edp_pwr_on_off_delay,
						info->edp2_info.edp_pwr_on_vary_bl_to_blon,
						info->edp2_info.edp_pwr_down_bloff_to_vary_bloff,
						info->edp2_info.edp_bootup_bl_level);
		}
	}

	if (result != BP_RESULT_OK)
		return result;
	else {
		// Log each external path
		for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; i++) {
			if (info->ext_disp_conn_info.path[i].device_tag != 0)
				DC_LOG_BIOS("integrated_info:For EXTERNAL DISPLAY PATH %d --------------\n"
						"DEVICE_TAG: 0x%x\n"
						"DEVICE_ACPI_ENUM: 0x%x\n"
						"DEVICE_CONNECTOR_ID: 0x%x\n"
						"EXT_AUX_DDC_LUT_INDEX: %d\n"
						"EXT_HPD_PIN_LUT_INDEX: %d\n"
						"EXT_ENCODER_OBJ_ID: 0x%x\n"
						"Encoder CAPS: 0x%x\n",
						i,
						info->ext_disp_conn_info.path[i].device_tag,
						info->ext_disp_conn_info.path[i].device_acpi_enum,
						info->ext_disp_conn_info.path[i].device_connector_id.id,
						info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index,
						info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index,
						info->ext_disp_conn_info.path[i].ext_encoder_obj_id.id,
						info->ext_disp_conn_info.path[i].caps
						);
			if (info->ext_disp_conn_info.path[i].caps & EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN)
				DC_LOG_BIOS("BIOS EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN on path %d\n", i);
			else if (bp->base.ctx->dc->config.force_bios_fixed_vs) {
				info->ext_disp_conn_info.path[i].caps |= EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN;
				DC_LOG_BIOS("driver forced EXT_DISPLAY_PATH_CAPS__DP_FIXED_VS_EN on path %d\n", i);
			}
		}
		// Log the Checksum and Voltage Swing
		DC_LOG_BIOS("Integrated info table CHECKSUM: %d\n"
					"Integrated info table FIX_DP_VOLTAGE_SWING: %d\n",
					info->ext_disp_conn_info.checksum,
					info->ext_disp_conn_info.fixdpvoltageswing);
		if (bp->base.ctx->dc->config.force_bios_fixed_vs && info->ext_disp_conn_info.fixdpvoltageswing == 0) {
			info->ext_disp_conn_info.fixdpvoltageswing = bp->base.ctx->dc->config.force_bios_fixed_vs & 0xF;
			DC_LOG_BIOS("driver forced fixdpvoltageswing = %d\n", info->ext_disp_conn_info.fixdpvoltageswing);
		}
	}
	/* Sort voltage table from low to high*/
	for (i = 1; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		for (j = i; j > 0; --j) {
			if (info->disp_clk_voltage[j].max_supported_clk <
			    info->disp_clk_voltage[j-1].max_supported_clk)
				swap(info->disp_clk_voltage[j-1], info->disp_clk_voltage[j]);
		}
	}

	return result;
}

static enum bp_result bios_parser_get_vram_info(
		struct dc_bios *dcb,
		struct dc_vram_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	static enum bp_result result = BP_RESULT_BADBIOSTABLE;
	struct atom_common_table_header *header;
	struct atom_data_revision revision;

	// vram info moved to umc_info for DCN4x
	if (info && DATA_TABLES(umc_info)) {
		header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(umc_info));

		get_atom_data_table_revision(header, &revision);

		switch (revision.major) {
		case 4:
			switch (revision.minor) {
			case 0:
				result = get_vram_info_from_umc_info_v40(bp, info);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	if (result != BP_RESULT_OK && info && DATA_TABLES(vram_info)) {
		header = GET_IMAGE(struct atom_common_table_header,
					DATA_TABLES(vram_info));

		get_atom_data_table_revision(header, &revision);

		switch (revision.major) {
		case 2:
			switch (revision.minor) {
			case 3:
				result = get_vram_info_v23(bp, info);
				break;
			case 4:
				result = get_vram_info_v24(bp, info);
				break;
			case 5:
				result = get_vram_info_v25(bp, info);
				break;
			default:
				break;
			}
			break;

		case 3:
			switch (revision.minor) {
			case 0:
				result = get_vram_info_v30(bp, info);
				break;
			default:
				break;
			}
			break;

		default:
			return result;
		}

	}
	return result;
}

static struct integrated_info *bios_parser_create_integrated_info(
	struct dc_bios *dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct integrated_info *info;

	info = kzalloc(sizeof(struct integrated_info), GFP_KERNEL);

	if (info == NULL) {
		ASSERT_CRITICAL(0);
		return NULL;
	}

	if (construct_integrated_info(bp, info) == BP_RESULT_OK)
		return info;

	kfree(info);

	return NULL;
}

static enum bp_result update_slot_layout_info(
	struct dc_bios *dcb,
	unsigned int i,
	struct slot_layout_info *slot_layout_info)
{
	unsigned int record_offset;
	unsigned int j;
	struct atom_display_object_path_v2 *object;
	struct atom_bracket_layout_record *record;
	struct atom_common_record_header *record_header;
	static enum bp_result result;
	struct bios_parser *bp;
	struct object_info_table *tbl;
	struct display_object_info_table_v1_4 *v1_4;

	record = NULL;
	record_header = NULL;
	result = BP_RESULT_NORECORD;

	bp = BP_FROM_DCB(dcb);
	tbl = &bp->object_info_tbl;
	v1_4 = tbl->v1_4;

	object = &v1_4->display_path[i];
	record_offset = (unsigned int)
		(object->disp_recordoffset) +
		(unsigned int)(bp->object_info_tbl_offset);

	for (;;) {

		record_header = (struct atom_common_record_header *)
			GET_IMAGE(struct atom_common_record_header,
			record_offset);
		if (record_header == NULL) {
			result = BP_RESULT_BADBIOSTABLE;
			break;
		}

		/* the end of the list */
		if (record_header->record_type == 0xff ||
			record_header->record_size == 0)	{
			break;
		}

		if (record_header->record_type ==
			ATOM_BRACKET_LAYOUT_RECORD_TYPE &&
			sizeof(struct atom_bracket_layout_record)
			<= record_header->record_size) {
			record = (struct atom_bracket_layout_record *)
				(record_header);
			result = BP_RESULT_OK;
			break;
		}

		record_offset += record_header->record_size;
	}

	/* return if the record not found */
	if (result != BP_RESULT_OK)
		return result;

	/* get slot sizes */
	slot_layout_info->length = record->bracketlen;
	slot_layout_info->width = record->bracketwidth;

	/* get info for each connector in the slot */
	slot_layout_info->num_of_connectors = record->conn_num;
	for (j = 0; j < slot_layout_info->num_of_connectors; ++j) {
		slot_layout_info->connectors[j].connector_type =
			(enum connector_layout_type)
			(record->conn_info[j].connector_type);
		switch (record->conn_info[j].connector_type) {
		case CONNECTOR_TYPE_DVI_D:
			slot_layout_info->connectors[j].connector_type =
				CONNECTOR_LAYOUT_TYPE_DVI_D;
			slot_layout_info->connectors[j].length =
				CONNECTOR_SIZE_DVI;
			break;

		case CONNECTOR_TYPE_HDMI:
			slot_layout_info->connectors[j].connector_type =
				CONNECTOR_LAYOUT_TYPE_HDMI;
			slot_layout_info->connectors[j].length =
				CONNECTOR_SIZE_HDMI;
			break;

		case CONNECTOR_TYPE_DISPLAY_PORT:
			slot_layout_info->connectors[j].connector_type =
				CONNECTOR_LAYOUT_TYPE_DP;
			slot_layout_info->connectors[j].length =
				CONNECTOR_SIZE_DP;
			break;

		case CONNECTOR_TYPE_MINI_DISPLAY_PORT:
			slot_layout_info->connectors[j].connector_type =
				CONNECTOR_LAYOUT_TYPE_MINI_DP;
			slot_layout_info->connectors[j].length =
				CONNECTOR_SIZE_MINI_DP;
			break;

		default:
			slot_layout_info->connectors[j].connector_type =
				CONNECTOR_LAYOUT_TYPE_UNKNOWN;
			slot_layout_info->connectors[j].length =
				CONNECTOR_SIZE_UNKNOWN;
		}

		slot_layout_info->connectors[j].position =
			record->conn_info[j].position;
		slot_layout_info->connectors[j].connector_id =
			object_id_from_bios_object_id(
				record->conn_info[j].connectorobjid);
	}
	return result;
}

static enum bp_result update_slot_layout_info_v2(
	struct dc_bios *dcb,
	unsigned int i,
	struct slot_layout_info *slot_layout_info)
{
	unsigned int record_offset;
	struct atom_display_object_path_v3 *object;
	struct atom_bracket_layout_record_v2 *record;
	struct atom_common_record_header *record_header;
	static enum bp_result result;
	struct bios_parser *bp;
	struct object_info_table *tbl;
	struct display_object_info_table_v1_5 *v1_5;
	struct graphics_object_id connector_id;

	record = NULL;
	record_header = NULL;
	result = BP_RESULT_NORECORD;

	bp = BP_FROM_DCB(dcb);
	tbl = &bp->object_info_tbl;
	v1_5 = tbl->v1_5;

	object = &v1_5->display_path[i];
	record_offset = (unsigned int)
		(object->disp_recordoffset) +
		(unsigned int)(bp->object_info_tbl_offset);

	for (;;) {

		record_header = (struct atom_common_record_header *)
			GET_IMAGE(struct atom_common_record_header,
			record_offset);
		if (record_header == NULL) {
			result = BP_RESULT_BADBIOSTABLE;
			break;
		}

		/* the end of the list */
		if (record_header->record_type == ATOM_RECORD_END_TYPE ||
			record_header->record_size == 0)	{
			break;
		}

		if (record_header->record_type ==
			ATOM_BRACKET_LAYOUT_V2_RECORD_TYPE &&
			sizeof(struct atom_bracket_layout_record_v2)
			<= record_header->record_size) {
			record = (struct atom_bracket_layout_record_v2 *)
				(record_header);
			result = BP_RESULT_OK;
			break;
		}

		record_offset += record_header->record_size;
	}

	/* return if the record not found */
	if (result != BP_RESULT_OK)
		return result;

	/* get slot sizes */
	connector_id = object_id_from_bios_object_id(object->display_objid);

	slot_layout_info->length = record->bracketlen;
	slot_layout_info->width = record->bracketwidth;
	slot_layout_info->num_of_connectors = v1_5->number_of_path;
	slot_layout_info->connectors[i].position = record->conn_num;
	slot_layout_info->connectors[i].connector_id = connector_id;

	switch (connector_id.id) {
	case CONNECTOR_ID_SINGLE_LINK_DVID:
	case CONNECTOR_ID_DUAL_LINK_DVID:
		slot_layout_info->connectors[i].connector_type = CONNECTOR_LAYOUT_TYPE_DVI_D;
		slot_layout_info->connectors[i].length = CONNECTOR_SIZE_DVI;
		break;

	case CONNECTOR_ID_HDMI_TYPE_A:
		slot_layout_info->connectors[i].connector_type = CONNECTOR_LAYOUT_TYPE_HDMI;
		slot_layout_info->connectors[i].length = CONNECTOR_SIZE_HDMI;
		break;

	case CONNECTOR_ID_DISPLAY_PORT:
	case CONNECTOR_ID_USBC:
		if (record->mini_type == MINI_TYPE_NORMAL) {
			slot_layout_info->connectors[i].connector_type = CONNECTOR_LAYOUT_TYPE_DP;
			slot_layout_info->connectors[i].length = CONNECTOR_SIZE_DP;
		} else {
			slot_layout_info->connectors[i].connector_type = CONNECTOR_LAYOUT_TYPE_MINI_DP;
			slot_layout_info->connectors[i].length = CONNECTOR_SIZE_MINI_DP;
		}
		break;

	default:
		slot_layout_info->connectors[i].connector_type = CONNECTOR_LAYOUT_TYPE_UNKNOWN;
		slot_layout_info->connectors[i].length = CONNECTOR_SIZE_UNKNOWN;
	}
	return result;
}

static enum bp_result get_bracket_layout_record(
	struct dc_bios *dcb,
	unsigned int bracket_layout_id,
	struct slot_layout_info *slot_layout_info)
{
	unsigned int i;
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	static enum bp_result result;
	struct object_info_table *tbl;
	struct display_object_info_table_v1_4 *v1_4;
	struct display_object_info_table_v1_5 *v1_5;

	if (slot_layout_info == NULL) {
		DC_LOG_DETECTION_EDID_PARSER("Invalid slot_layout_info\n");
		return BP_RESULT_BADINPUT;
	}

	tbl = &bp->object_info_tbl;
	v1_4 = tbl->v1_4;
	v1_5 = tbl->v1_5;

	result = BP_RESULT_NORECORD;
	switch (bp->object_info_tbl.revision.minor) {
	case 4:
	default:
		for (i = 0; i < v1_4->number_of_path; ++i) {
			if (bracket_layout_id == v1_4->display_path[i].display_objid) {
				result = update_slot_layout_info(dcb, i, slot_layout_info);
				break;
			}
		}
		break;
	case 5:
		for (i = 0; i < v1_5->number_of_path; ++i)
			result = update_slot_layout_info_v2(dcb, i, slot_layout_info);
		break;
	}

	return result;
}

static enum bp_result bios_get_board_layout_info(
	struct dc_bios *dcb,
	struct board_layout_info *board_layout_info)
{
	unsigned int i;
	struct bios_parser *bp;
	static enum bp_result record_result;
	unsigned int max_slots;

	const unsigned int slot_index_to_vbios_id[MAX_BOARD_SLOTS] = {
		GENERICOBJECT_BRACKET_LAYOUT_ENUM_ID1,
		GENERICOBJECT_BRACKET_LAYOUT_ENUM_ID2,
		0, 0
	};

	bp = BP_FROM_DCB(dcb);

	if (board_layout_info == NULL) {
		DC_LOG_DETECTION_EDID_PARSER("Invalid board_layout_info\n");
		return BP_RESULT_BADINPUT;
	}

	board_layout_info->num_of_slots = 0;
	max_slots = MAX_BOARD_SLOTS;

	// Assume single slot on v1_5
	if (bp->object_info_tbl.revision.minor == 5) {
		max_slots = 1;
	}

	for (i = 0; i < max_slots; ++i) {
		record_result = get_bracket_layout_record(dcb,
			slot_index_to_vbios_id[i],
			&board_layout_info->slots[i]);

		if (record_result == BP_RESULT_NORECORD && i > 0)
			break; /* no more slots present in bios */
		else if (record_result != BP_RESULT_OK)
			return record_result;  /* fail */

		++board_layout_info->num_of_slots;
	}

	/* all data is valid */
	board_layout_info->is_number_of_slots_valid = 1;
	board_layout_info->is_slots_size_valid = 1;
	board_layout_info->is_connector_offsets_valid = 1;
	board_layout_info->is_connector_lengths_valid = 1;

	return BP_RESULT_OK;
}


static uint16_t bios_parser_pack_data_tables(
	struct dc_bios *dcb,
	void *dst)
{
	// TODO: There is data bytes alignment issue, disable it for now.
	return 0;
}

static struct atom_dc_golden_table_v1 *bios_get_golden_table(
		struct bios_parser *bp,
		uint32_t rev_major,
		uint32_t rev_minor,
		uint16_t *dc_golden_table_ver)
{
	struct atom_display_controller_info_v4_4 *disp_cntl_tbl_4_4 = NULL;
	uint32_t dc_golden_offset = 0;
	*dc_golden_table_ver = 0;

	if (!DATA_TABLES(dce_info))
		return NULL;

	/* ver.4.4 or higher */
	switch (rev_major) {
	case 4:
		switch (rev_minor) {
		case 4:
			disp_cntl_tbl_4_4 = GET_IMAGE(struct atom_display_controller_info_v4_4,
									DATA_TABLES(dce_info));
			if (!disp_cntl_tbl_4_4)
				return NULL;
			dc_golden_offset = DATA_TABLES(dce_info) + disp_cntl_tbl_4_4->dc_golden_table_offset;
			*dc_golden_table_ver = disp_cntl_tbl_4_4->dc_golden_table_ver;
			break;
		case 5:
		default:
			/* For atom_display_controller_info_v4_5 there is no need to get golden table from
			 * dc_golden_table_offset as all these fields previously in golden table used for AUX
			 * pre-charge settings are now available directly in atom_display_controller_info_v4_5.
			 */
			break;
		}
		break;
	}

	if (!dc_golden_offset)
		return NULL;

	if (*dc_golden_table_ver != 1)
		return NULL;

	return GET_IMAGE(struct atom_dc_golden_table_v1,
			dc_golden_offset);
}

static enum bp_result bios_get_atom_dc_golden_table(
	struct dc_bios *dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_OK;
	struct atom_dc_golden_table_v1 *atom_dc_golden_table = NULL;
	struct atom_common_table_header *header;
	struct atom_data_revision tbl_revision;
	uint16_t dc_golden_table_ver = 0;

	header = GET_IMAGE(struct atom_common_table_header,
							DATA_TABLES(dce_info));
	if (!header)
		return BP_RESULT_UNSUPPORTED;

	get_atom_data_table_revision(header, &tbl_revision);

	atom_dc_golden_table = bios_get_golden_table(bp,
			tbl_revision.major,
			tbl_revision.minor,
			&dc_golden_table_ver);

	if (!atom_dc_golden_table)
		return BP_RESULT_UNSUPPORTED;

	dcb->golden_table.dc_golden_table_ver = dc_golden_table_ver;
	dcb->golden_table.aux_dphy_rx_control0_val = atom_dc_golden_table->aux_dphy_rx_control0_val;
	dcb->golden_table.aux_dphy_rx_control1_val = atom_dc_golden_table->aux_dphy_rx_control1_val;
	dcb->golden_table.aux_dphy_tx_control_val = atom_dc_golden_table->aux_dphy_tx_control_val;
	dcb->golden_table.dc_gpio_aux_ctrl_0_val = atom_dc_golden_table->dc_gpio_aux_ctrl_0_val;
	dcb->golden_table.dc_gpio_aux_ctrl_1_val = atom_dc_golden_table->dc_gpio_aux_ctrl_1_val;
	dcb->golden_table.dc_gpio_aux_ctrl_2_val = atom_dc_golden_table->dc_gpio_aux_ctrl_2_val;
	dcb->golden_table.dc_gpio_aux_ctrl_3_val = atom_dc_golden_table->dc_gpio_aux_ctrl_3_val;
	dcb->golden_table.dc_gpio_aux_ctrl_4_val = atom_dc_golden_table->dc_gpio_aux_ctrl_4_val;
	dcb->golden_table.dc_gpio_aux_ctrl_5_val = atom_dc_golden_table->dc_gpio_aux_ctrl_5_val;

	return result;
}


static const struct dc_vbios_funcs vbios_funcs = {
	.get_connectors_number = bios_parser_get_connectors_number,

	.get_connector_id = bios_parser_get_connector_id,

	.get_src_obj = bios_parser_get_src_obj,

	.get_i2c_info = bios_parser_get_i2c_info,

	.get_hpd_info = bios_parser_get_hpd_info,

	.get_device_tag = bios_parser_get_device_tag,

	.get_spread_spectrum_info = bios_parser_get_spread_spectrum_info,

	.get_ss_entry_number = bios_parser_get_ss_entry_number,

	.get_embedded_panel_info = bios_parser_get_embedded_panel_info,

	.get_gpio_pin_info = bios_parser_get_gpio_pin_info,

	.get_encoder_cap_info = bios_parser_get_encoder_cap_info,

	.is_device_id_supported = bios_parser_is_device_id_supported,

	.is_accelerated_mode = bios_parser_is_accelerated_mode,

	.set_scratch_critical_state = bios_parser_set_scratch_critical_state,


/*	 COMMANDS */
	.encoder_control = bios_parser_encoder_control,

	.transmitter_control = bios_parser_transmitter_control,

	.enable_crtc = bios_parser_enable_crtc,

	.set_pixel_clock = bios_parser_set_pixel_clock,

	.set_dce_clock = bios_parser_set_dce_clock,

	.program_crtc_timing = bios_parser_program_crtc_timing,

	.enable_disp_power_gating = bios_parser_enable_disp_power_gating,

	.bios_parser_destroy = firmware_parser_destroy,

	.get_board_layout_info = bios_get_board_layout_info,
	.pack_data_tables = bios_parser_pack_data_tables,

	.get_atom_dc_golden_table = bios_get_atom_dc_golden_table,

	.enable_lvtma_control = bios_parser_enable_lvtma_control,

	.get_soc_bb_info = bios_parser_get_soc_bb_info,

	.get_disp_connector_caps_info = bios_parser_get_disp_connector_caps_info,

	.get_lttpr_caps = bios_parser_get_lttpr_caps,

	.get_lttpr_interop = bios_parser_get_lttpr_interop,

	.get_connector_speed_cap_info = bios_parser_get_connector_speed_cap_info,
};

static bool bios_parser2_construct(
	struct bios_parser *bp,
	struct bp_init_data *init,
	enum dce_version dce_version)
{
	uint16_t *rom_header_offset = NULL;
	struct atom_rom_header_v2_2 *rom_header = NULL;
	struct display_object_info_table_v1_4 *object_info_tbl;
	struct atom_data_revision tbl_rev = {0};

	if (!init)
		return false;

	if (!init->bios)
		return false;

	bp->base.funcs = &vbios_funcs;
	bp->base.bios = init->bios;
	bp->base.bios_size = bp->base.bios[OFFSET_TO_ATOM_ROM_IMAGE_SIZE] * BIOS_IMAGE_SIZE_UNIT;

	bp->base.ctx = init->ctx;

	bp->base.bios_local_image = NULL;

	rom_header_offset =
			GET_IMAGE(uint16_t, OFFSET_TO_ATOM_ROM_HEADER_POINTER);

	if (!rom_header_offset)
		return false;

	rom_header = GET_IMAGE(struct atom_rom_header_v2_2, *rom_header_offset);

	if (!rom_header)
		return false;

	get_atom_data_table_revision(&rom_header->table_header, &tbl_rev);
	if (!(tbl_rev.major >= 2 && tbl_rev.minor >= 2))
		return false;

	bp->master_data_tbl =
		GET_IMAGE(struct atom_master_data_table_v2_1,
				rom_header->masterdatatable_offset);

	if (!bp->master_data_tbl)
		return false;

	bp->object_info_tbl_offset = DATA_TABLES(displayobjectinfo);

	if (!bp->object_info_tbl_offset)
		return false;

	object_info_tbl =
			GET_IMAGE(struct display_object_info_table_v1_4,
						bp->object_info_tbl_offset);

	if (!object_info_tbl)
		return false;

	get_atom_data_table_revision(&object_info_tbl->table_header,
		&bp->object_info_tbl.revision);

	if (bp->object_info_tbl.revision.major == 1
		&& bp->object_info_tbl.revision.minor == 4) {
		struct display_object_info_table_v1_4 *tbl_v1_4;

		tbl_v1_4 = GET_IMAGE(struct display_object_info_table_v1_4,
			bp->object_info_tbl_offset);
		if (!tbl_v1_4)
			return false;

		bp->object_info_tbl.v1_4 = tbl_v1_4;
	} else if (bp->object_info_tbl.revision.major == 1
		&& bp->object_info_tbl.revision.minor == 5) {
		struct display_object_info_table_v1_5 *tbl_v1_5;

		tbl_v1_5 = GET_IMAGE(struct display_object_info_table_v1_5,
			bp->object_info_tbl_offset);
		if (!tbl_v1_5)
			return false;

		bp->object_info_tbl.v1_5 = tbl_v1_5;
	} else {
		ASSERT(0);
		return false;
	}

	dal_firmware_parser_init_cmd_tbl(bp);
	dal_bios_parser_init_cmd_tbl_helper2(&bp->cmd_helper, dce_version);

	bp->base.integrated_info = bios_parser_create_integrated_info(&bp->base);
	bp->base.fw_info_valid = bios_parser_get_firmware_info(&bp->base, &bp->base.fw_info) == BP_RESULT_OK;
	bios_parser_get_vram_info(&bp->base, &bp->base.vram_info);
	bios_parser_get_soc_bb_info(&bp->base, &bp->base.bb_info);
	return true;
}

struct dc_bios *firmware_parser_create(
	struct bp_init_data *init,
	enum dce_version dce_version)
{
	struct bios_parser *bp;

	bp = kzalloc(sizeof(struct bios_parser), GFP_KERNEL);
	if (!bp)
		return NULL;

	if (bios_parser2_construct(bp, init, dce_version))
		return &bp->base;

	kfree(bp);
	return NULL;
}


