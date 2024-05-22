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

#include <linux/slab.h>

#include "dm_services.h"

#include "atom.h"

#include "dc_bios_types.h"
#include "include/gpio_service_interface.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/bios_parser_interface.h"
#include "include/logger_interface.h"

#include "command_table.h"
#include "bios_parser_helper.h"
#include "command_table_helper.h"
#include "bios_parser.h"
#include "bios_parser_types_internal.h"
#include "bios_parser_interface.h"

#include "bios_parser_common.h"

#define THREE_PERCENT_OF_10000 300

#define LAST_RECORD_TYPE 0xff

#define DC_LOGGER \
	bp->base.ctx->logger

#define DATA_TABLES(table) (bp->master_data_tbl->ListOfDataTables.table)

static void get_atom_data_table_revision(
	ATOM_COMMON_TABLE_HEADER *atom_data_tbl,
	struct atom_data_revision *tbl_revision);
static uint32_t get_src_obj_list(struct bios_parser *bp, ATOM_OBJECT *object,
	uint16_t **id_list);
static ATOM_OBJECT *get_bios_object(struct bios_parser *bp,
	struct graphics_object_id id);
static enum bp_result get_gpio_i2c_info(struct bios_parser *bp,
	ATOM_I2C_RECORD *record,
	struct graphics_object_i2c_info *info);
static ATOM_HPD_INT_RECORD *get_hpd_record(struct bios_parser *bp,
	ATOM_OBJECT *object);
static struct device_id device_type_from_device_id(uint16_t device_id);
static uint32_t signal_to_ss_id(enum as_signal_type signal);
static uint32_t get_support_mask_for_device_id(struct device_id device_id);
static ATOM_ENCODER_CAP_RECORD_V2 *get_encoder_cap_record(
	struct bios_parser *bp,
	ATOM_OBJECT *object);

#define BIOS_IMAGE_SIZE_OFFSET 2
#define BIOS_IMAGE_SIZE_UNIT 512

/*****************************************************************************/
static bool bios_parser_construct(
	struct bios_parser *bp,
	struct bp_init_data *init,
	enum dce_version dce_version);

static uint8_t bios_parser_get_connectors_number(
	struct dc_bios *dcb);

static enum bp_result bios_parser_get_embedded_panel_info(
	struct dc_bios *dcb,
	struct embedded_panel_info *info);

/*****************************************************************************/

struct dc_bios *bios_parser_create(
	struct bp_init_data *init,
	enum dce_version dce_version)
{
	struct bios_parser *bp;

	bp = kzalloc(sizeof(struct bios_parser), GFP_KERNEL);
	if (!bp)
		return NULL;

	if (bios_parser_construct(bp, init, dce_version))
		return &bp->base;

	kfree(bp);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static void bios_parser_destruct(struct bios_parser *bp)
{
	kfree(bp->base.bios_local_image);
	kfree(bp->base.integrated_info);
}

static void bios_parser_destroy(struct dc_bios **dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(*dcb);

	if (!bp) {
		BREAK_TO_DEBUGGER();
		return;
	}

	bios_parser_destruct(bp);

	kfree(bp);
	*dcb = NULL;
}

static uint8_t get_number_of_objects(struct bios_parser *bp, uint32_t offset)
{
	ATOM_OBJECT_TABLE *table;

	uint32_t object_table_offset = bp->object_info_tbl_offset + offset;

	table = ((ATOM_OBJECT_TABLE *) bios_get_image(&bp->base,
				object_table_offset,
				struct_size(table, asObjects, 1)));

	if (!table)
		return 0;
	else
		return table->ucNumberOfObjects;
}

static uint8_t bios_parser_get_connectors_number(struct dc_bios *dcb)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	return get_number_of_objects(bp,
		le16_to_cpu(bp->object_info_tbl.v1_1->usConnectorObjectTableOffset));
}

static struct graphics_object_id bios_parser_get_connector_id(
	struct dc_bios *dcb,
	uint8_t i)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	struct graphics_object_id object_id = dal_graphics_object_id_init(
		0, ENUM_ID_UNKNOWN, OBJECT_TYPE_UNKNOWN);
	uint16_t id;

	uint32_t connector_table_offset = bp->object_info_tbl_offset
		+ le16_to_cpu(bp->object_info_tbl.v1_1->usConnectorObjectTableOffset);

	ATOM_OBJECT_TABLE *tbl = ((ATOM_OBJECT_TABLE *) bios_get_image(&bp->base,
				connector_table_offset,
				struct_size(tbl, asObjects, 1)));

	if (!tbl) {
		dm_error("Can't get connector table from atom bios.\n");
		return object_id;
	}

	if (tbl->ucNumberOfObjects <= i) {
		dm_error("Can't find connector id %d in connector table of size %d.\n",
			 i, tbl->ucNumberOfObjects);
		return object_id;
	}

	id = le16_to_cpu(tbl->asObjects[i].usObjectID);
	object_id = object_id_from_bios_object_id(id);
	return object_id;
}

static enum bp_result bios_parser_get_src_obj(struct dc_bios *dcb,
	struct graphics_object_id object_id, uint32_t index,
	struct graphics_object_id *src_object_id)
{
	uint32_t number;
	uint16_t *id;
	ATOM_OBJECT *object;
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!src_object_id)
		return BP_RESULT_BADINPUT;

	object = get_bios_object(bp, object_id);

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object id */
		return BP_RESULT_BADINPUT;
	}

	number = get_src_obj_list(bp, object, &id);

	if (number <= index)
		return BP_RESULT_BADINPUT;

	*src_object_id = object_id_from_bios_object_id(id[index]);

	return BP_RESULT_OK;
}

static enum bp_result bios_parser_get_i2c_info(struct dc_bios *dcb,
	struct graphics_object_id id,
	struct graphics_object_i2c_info *info)
{
	uint32_t offset;
	ATOM_OBJECT *object;
	ATOM_COMMON_RECORD_HEADER *header;
	ATOM_I2C_RECORD *record;
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!info)
		return BP_RESULT_BADINPUT;

	object = get_bios_object(bp, id);

	if (!object)
		return BP_RESULT_BADINPUT;

	offset = le16_to_cpu(object->usRecordOffset)
			+ bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(ATOM_COMMON_RECORD_HEADER, offset);

		if (!header)
			return BP_RESULT_BADBIOSTABLE;

		if (LAST_RECORD_TYPE == header->ucRecordType ||
			!header->ucRecordSize)
			break;

		if (ATOM_I2C_RECORD_TYPE == header->ucRecordType
			&& sizeof(ATOM_I2C_RECORD) <= header->ucRecordSize) {
			/* get the I2C info */
			record = (ATOM_I2C_RECORD *) header;

			if (get_gpio_i2c_info(bp, record, info) == BP_RESULT_OK)
				return BP_RESULT_OK;
		}

		offset += header->ucRecordSize;
	}

	return BP_RESULT_NORECORD;
}

static enum bp_result bios_parser_get_hpd_info(struct dc_bios *dcb,
	struct graphics_object_id id,
	struct graphics_object_hpd_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	ATOM_OBJECT *object;
	ATOM_HPD_INT_RECORD *record = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	object = get_bios_object(bp, id);

	if (!object)
		return BP_RESULT_BADINPUT;

	record = get_hpd_record(bp, object);

	if (record != NULL) {
		info->hpd_int_gpio_uid = record->ucHPDIntGPIOID;
		info->hpd_active = record->ucPlugged_PinState;
		return BP_RESULT_OK;
	}

	return BP_RESULT_NORECORD;
}

static enum bp_result bios_parser_get_device_tag_record(
	struct bios_parser *bp,
	ATOM_OBJECT *object,
	ATOM_CONNECTOR_DEVICE_TAG_RECORD **record)
{
	ATOM_COMMON_RECORD_HEADER *header;
	uint32_t offset;

	offset = le16_to_cpu(object->usRecordOffset)
			+ bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(ATOM_COMMON_RECORD_HEADER, offset);

		if (!header)
			return BP_RESULT_BADBIOSTABLE;

		offset += header->ucRecordSize;

		if (LAST_RECORD_TYPE == header->ucRecordType ||
			!header->ucRecordSize)
			break;

		if (ATOM_CONNECTOR_DEVICE_TAG_RECORD_TYPE !=
			header->ucRecordType)
			continue;

		if (sizeof(ATOM_CONNECTOR_DEVICE_TAG) > header->ucRecordSize)
			continue;

		*record = (ATOM_CONNECTOR_DEVICE_TAG_RECORD *) header;
		return BP_RESULT_OK;
	}

	return BP_RESULT_NORECORD;
}

static enum bp_result bios_parser_get_device_tag(
	struct dc_bios *dcb,
	struct graphics_object_id connector_object_id,
	uint32_t device_tag_index,
	struct connector_device_tag_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	ATOM_OBJECT *object;
	ATOM_CONNECTOR_DEVICE_TAG_RECORD *record = NULL;
	ATOM_CONNECTOR_DEVICE_TAG *device_tag;

	if (!info)
		return BP_RESULT_BADINPUT;

	/* getBiosObject will return MXM object */
	object = get_bios_object(bp, connector_object_id);

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object id */
		return BP_RESULT_BADINPUT;
	}

	if (bios_parser_get_device_tag_record(bp, object, &record)
		!= BP_RESULT_OK)
		return BP_RESULT_NORECORD;

	if (device_tag_index >= record->ucNumberOfDevice)
		return BP_RESULT_NORECORD;

	device_tag = &record->asDeviceTag[device_tag_index];

	info->acpi_device = le32_to_cpu(device_tag->ulACPIDeviceEnum);
	info->dev_id =
		device_type_from_device_id(le16_to_cpu(device_tag->usDeviceID));

	return BP_RESULT_OK;
}

static enum bp_result get_firmware_info_v1_4(
	struct bios_parser *bp,
	struct dc_firmware_info *info);
static enum bp_result get_firmware_info_v2_1(
	struct bios_parser *bp,
	struct dc_firmware_info *info);
static enum bp_result get_firmware_info_v2_2(
	struct bios_parser *bp,
	struct dc_firmware_info *info);

static enum bp_result bios_parser_get_firmware_info(
	struct dc_bios *dcb,
	struct dc_firmware_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_BADBIOSTABLE;
	ATOM_COMMON_TABLE_HEADER *header;
	struct atom_data_revision revision;

	if (info && DATA_TABLES(FirmwareInfo)) {
		header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER,
			DATA_TABLES(FirmwareInfo));
		get_atom_data_table_revision(header, &revision);
		switch (revision.major) {
		case 1:
			switch (revision.minor) {
			case 4:
				result = get_firmware_info_v1_4(bp, info);
				break;
			default:
				break;
			}
			break;

		case 2:
			switch (revision.minor) {
			case 1:
				result = get_firmware_info_v2_1(bp, info);
				break;
			case 2:
				result = get_firmware_info_v2_2(bp, info);
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

static enum bp_result get_firmware_info_v1_4(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	ATOM_FIRMWARE_INFO_V1_4 *firmware_info =
		GET_IMAGE(ATOM_FIRMWARE_INFO_V1_4,
			DATA_TABLES(FirmwareInfo));

	if (!info)
		return BP_RESULT_BADINPUT;

	if (!firmware_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	/* Pixel clock pll information. We need to convert from 10KHz units into
	 * KHz units */
	info->pll_info.crystal_frequency =
		le16_to_cpu(firmware_info->usReferenceClock) * 10;
	info->pll_info.min_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmware_info->usMinPixelClockPLL_Input) * 10;
	info->pll_info.max_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmware_info->usMaxPixelClockPLL_Input) * 10;
	info->pll_info.min_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmware_info->ulMinPixelClockPLL_Output) * 10;
	info->pll_info.max_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmware_info->ulMaxPixelClockPLL_Output) * 10;

	if (firmware_info->usFirmwareCapability.sbfAccess.MemoryClockSS_Support)
		/* Since there is no information on the SS, report conservative
		 * value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.memory_clk_ss_percentage = THREE_PERCENT_OF_10000;

	if (firmware_info->usFirmwareCapability.sbfAccess.EngineClockSS_Support)
		/* Since there is no information on the SS,report conservative
		 * value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.engine_clk_ss_percentage = THREE_PERCENT_OF_10000;

	return BP_RESULT_OK;
}

static enum bp_result get_ss_info_v3_1(
	struct bios_parser *bp,
	uint32_t id,
	uint32_t index,
	struct spread_spectrum_info *ss_info);

static enum bp_result get_firmware_info_v2_1(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	ATOM_FIRMWARE_INFO_V2_1 *firmwareInfo =
		GET_IMAGE(ATOM_FIRMWARE_INFO_V2_1, DATA_TABLES(FirmwareInfo));
	struct spread_spectrum_info internalSS;
	uint32_t index;

	if (!info)
		return BP_RESULT_BADINPUT;

	if (!firmwareInfo)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	/* Pixel clock pll information. We need to convert from 10KHz units into
	 * KHz units */
	info->pll_info.crystal_frequency =
		le16_to_cpu(firmwareInfo->usCoreReferenceClock) * 10;
	info->pll_info.min_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmwareInfo->usMinPixelClockPLL_Input) * 10;
	info->pll_info.max_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmwareInfo->usMaxPixelClockPLL_Input) * 10;
	info->pll_info.min_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmwareInfo->ulMinPixelClockPLL_Output) * 10;
	info->pll_info.max_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmwareInfo->ulMaxPixelClockPLL_Output) * 10;
	info->default_display_engine_pll_frequency =
		le32_to_cpu(firmwareInfo->ulDefaultDispEngineClkFreq) * 10;
	info->external_clock_source_frequency_for_dp =
		le16_to_cpu(firmwareInfo->usUniphyDPModeExtClkFreq) * 10;
	info->min_allowed_bl_level = firmwareInfo->ucMinAllowedBL_Level;

	/* There should be only one entry in the SS info table for Memory Clock
	 */
	index = 0;
	if (firmwareInfo->usFirmwareCapability.sbfAccess.MemoryClockSS_Support)
		/* Since there is no information for external SS, report
		 *  conservative value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.memory_clk_ss_percentage = THREE_PERCENT_OF_10000;
	else if (get_ss_info_v3_1(bp,
		ASIC_INTERNAL_MEMORY_SS, index, &internalSS) == BP_RESULT_OK) {
		if (internalSS.spread_spectrum_percentage) {
			info->feature.memory_clk_ss_percentage =
				internalSS.spread_spectrum_percentage;
			if (internalSS.type.CENTER_MODE) {
				/* if it is centermode, the exact SS Percentage
				 * will be round up of half of the percentage
				 * reported in the SS table */
				++info->feature.memory_clk_ss_percentage;
				info->feature.memory_clk_ss_percentage /= 2;
			}
		}
	}

	/* There should be only one entry in the SS info table for Engine Clock
	 */
	index = 1;
	if (firmwareInfo->usFirmwareCapability.sbfAccess.EngineClockSS_Support)
		/* Since there is no information for external SS, report
		 * conservative value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.engine_clk_ss_percentage = THREE_PERCENT_OF_10000;
	else if (get_ss_info_v3_1(bp,
		ASIC_INTERNAL_ENGINE_SS, index, &internalSS) == BP_RESULT_OK) {
		if (internalSS.spread_spectrum_percentage) {
			info->feature.engine_clk_ss_percentage =
				internalSS.spread_spectrum_percentage;
			if (internalSS.type.CENTER_MODE) {
				/* if it is centermode, the exact SS Percentage
				 * will be round up of half of the percentage
				 * reported in the SS table */
				++info->feature.engine_clk_ss_percentage;
				info->feature.engine_clk_ss_percentage /= 2;
			}
		}
	}

	return BP_RESULT_OK;
}

static enum bp_result get_firmware_info_v2_2(
	struct bios_parser *bp,
	struct dc_firmware_info *info)
{
	ATOM_FIRMWARE_INFO_V2_2 *firmware_info;
	struct spread_spectrum_info internal_ss;
	uint32_t index;

	if (!info)
		return BP_RESULT_BADINPUT;

	firmware_info = GET_IMAGE(ATOM_FIRMWARE_INFO_V2_2,
		DATA_TABLES(FirmwareInfo));

	if (!firmware_info)
		return BP_RESULT_BADBIOSTABLE;

	memset(info, 0, sizeof(*info));

	/* Pixel clock pll information. We need to convert from 10KHz units into
	 * KHz units */
	info->pll_info.crystal_frequency =
		le16_to_cpu(firmware_info->usCoreReferenceClock) * 10;
	info->pll_info.min_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmware_info->usMinPixelClockPLL_Input) * 10;
	info->pll_info.max_input_pxl_clk_pll_frequency =
		le16_to_cpu(firmware_info->usMaxPixelClockPLL_Input) * 10;
	info->pll_info.min_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmware_info->ulMinPixelClockPLL_Output) * 10;
	info->pll_info.max_output_pxl_clk_pll_frequency =
		le32_to_cpu(firmware_info->ulMaxPixelClockPLL_Output) * 10;
	info->default_display_engine_pll_frequency =
		le32_to_cpu(firmware_info->ulDefaultDispEngineClkFreq) * 10;
	info->external_clock_source_frequency_for_dp =
		le16_to_cpu(firmware_info->usUniphyDPModeExtClkFreq) * 10;

	/* There should be only one entry in the SS info table for Memory Clock
	 */
	index = 0;
	if (firmware_info->usFirmwareCapability.sbfAccess.MemoryClockSS_Support)
		/* Since there is no information for external SS, report
		 *  conservative value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.memory_clk_ss_percentage = THREE_PERCENT_OF_10000;
	else if (get_ss_info_v3_1(bp,
			ASIC_INTERNAL_MEMORY_SS, index, &internal_ss) == BP_RESULT_OK) {
		if (internal_ss.spread_spectrum_percentage) {
			info->feature.memory_clk_ss_percentage =
					internal_ss.spread_spectrum_percentage;
			if (internal_ss.type.CENTER_MODE) {
				/* if it is centermode, the exact SS Percentage
				 * will be round up of half of the percentage
				 * reported in the SS table */
				++info->feature.memory_clk_ss_percentage;
				info->feature.memory_clk_ss_percentage /= 2;
			}
		}
	}

	/* There should be only one entry in the SS info table for Engine Clock
	 */
	index = 1;
	if (firmware_info->usFirmwareCapability.sbfAccess.EngineClockSS_Support)
		/* Since there is no information for external SS, report
		 * conservative value 3% for bandwidth calculation */
		/* unit of 0.01% */
		info->feature.engine_clk_ss_percentage = THREE_PERCENT_OF_10000;
	else if (get_ss_info_v3_1(bp,
			ASIC_INTERNAL_ENGINE_SS, index, &internal_ss) == BP_RESULT_OK) {
		if (internal_ss.spread_spectrum_percentage) {
			info->feature.engine_clk_ss_percentage =
					internal_ss.spread_spectrum_percentage;
			if (internal_ss.type.CENTER_MODE) {
				/* if it is centermode, the exact SS Percentage
				 * will be round up of half of the percentage
				 * reported in the SS table */
				++info->feature.engine_clk_ss_percentage;
				info->feature.engine_clk_ss_percentage /= 2;
			}
		}
	}

	/* Remote Display */
	info->remote_display_config = firmware_info->ucRemoteDisplayConfig;

	/* Is allowed minimum BL level */
	info->min_allowed_bl_level = firmware_info->ucMinAllowedBL_Level;
	/* Used starting from CI */
	info->smu_gpu_pll_output_freq =
			(uint32_t) (le32_to_cpu(firmware_info->ulGPUPLL_OutputFreq) * 10);

	return BP_RESULT_OK;
}

static enum bp_result get_ss_info_v3_1(
	struct bios_parser *bp,
	uint32_t id,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	ATOM_ASIC_INTERNAL_SS_INFO_V3 *ss_table_header_include;
	ATOM_ASIC_SS_ASSIGNMENT_V3 *tbl;
	uint32_t table_size;
	uint32_t i;
	uint32_t table_index = 0;

	if (!ss_info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		return BP_RESULT_UNSUPPORTED;

	ss_table_header_include = ((ATOM_ASIC_INTERNAL_SS_INFO_V3 *) bios_get_image(&bp->base,
				DATA_TABLES(ASIC_InternalSS_Info),
				struct_size(ss_table_header_include, asSpreadSpectrum, 1)));
	table_size =
		(le16_to_cpu(ss_table_header_include->sHeader.usStructureSize)
				- sizeof(ATOM_COMMON_TABLE_HEADER))
				/ sizeof(ATOM_ASIC_SS_ASSIGNMENT_V3);

	tbl = (ATOM_ASIC_SS_ASSIGNMENT_V3 *)
				&ss_table_header_include->asSpreadSpectrum[0];

	memset(ss_info, 0, sizeof(struct spread_spectrum_info));

	for (i = 0; i < table_size; i++) {
		if (tbl[i].ucClockIndication != (uint8_t) id)
			continue;

		if (table_index != index) {
			table_index++;
			continue;
		}
		/* VBIOS introduced new defines for Version 3, same values as
		 *  before, so now use these new ones for Version 3.
		 * Shouldn't affect field VBIOS's V3 as define values are still
		 *  same.
		 * #define SS_MODE_V3_CENTRE_SPREAD_MASK                0x01
		 * #define SS_MODE_V3_EXTERNAL_SS_MASK                  0x02

		 * Old VBIOS defines:
		 * #define ATOM_SS_CENTRE_SPREAD_MODE_MASK        0x00000001
		 * #define ATOM_EXTERNAL_SS_MASK                  0x00000002
		 */

		if (SS_MODE_V3_EXTERNAL_SS_MASK & tbl[i].ucSpreadSpectrumMode)
			ss_info->type.EXTERNAL = true;

		if (SS_MODE_V3_CENTRE_SPREAD_MASK & tbl[i].ucSpreadSpectrumMode)
			ss_info->type.CENTER_MODE = true;

		/* Older VBIOS (in field) always provides SS percentage in 0.01%
		 * units set Divider to 100 */
		ss_info->spread_percentage_divider = 100;

		/* #define SS_MODE_V3_PERCENTAGE_DIV_BY_1000_MASK 0x10 */
		if (SS_MODE_V3_PERCENTAGE_DIV_BY_1000_MASK
				& tbl[i].ucSpreadSpectrumMode)
			ss_info->spread_percentage_divider = 1000;

		ss_info->type.STEP_AND_DELAY_INFO = false;
		/* convert [10KHz] into [KHz] */
		ss_info->target_clock_range =
				le32_to_cpu(tbl[i].ulTargetClockRange) * 10;
		ss_info->spread_spectrum_percentage =
				(uint32_t)le16_to_cpu(tbl[i].usSpreadSpectrumPercentage);
		ss_info->spread_spectrum_range =
				(uint32_t)(le16_to_cpu(tbl[i].usSpreadRateIn10Hz) * 10);

		return BP_RESULT_OK;
	}
	return BP_RESULT_NORECORD;
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

static enum bp_result bios_parser_adjust_pixel_clock(
	struct dc_bios *dcb,
	struct bp_adjust_pixel_clock_parameters *bp_params)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.adjust_display_pll)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.adjust_display_pll(bp, bp_params);
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

static enum bp_result bios_parser_enable_spread_spectrum_on_ppll(
	struct dc_bios *dcb,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.enable_spread_spectrum_on_ppll)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.enable_spread_spectrum_on_ppll(
			bp, bp_params, enable);

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

static enum bp_result bios_parser_program_display_engine_pll(
	struct dc_bios *dcb,
	struct bp_pixel_clock_parameters *bp_params)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	if (!bp->cmd_tbl.program_clock)
		return BP_RESULT_FAILURE;

	return bp->cmd_tbl.program_clock(bp, bp_params);

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

static bool bios_parser_is_device_id_supported(
	struct dc_bios *dcb,
	struct device_id id)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);

	uint32_t mask = get_support_mask_for_device_id(id);

	return (le16_to_cpu(bp->object_info_tbl.v1_1->usDeviceSupport) & mask) != 0;
}

static ATOM_HPD_INT_RECORD *get_hpd_record(struct bios_parser *bp,
	ATOM_OBJECT *object)
{
	ATOM_COMMON_RECORD_HEADER *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = le16_to_cpu(object->usRecordOffset)
			+ bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(ATOM_COMMON_RECORD_HEADER, offset);

		if (!header)
			return NULL;

		if (LAST_RECORD_TYPE == header->ucRecordType ||
			!header->ucRecordSize)
			break;

		if (ATOM_HPD_INT_RECORD_TYPE == header->ucRecordType
			&& sizeof(ATOM_HPD_INT_RECORD) <= header->ucRecordSize)
			return (ATOM_HPD_INT_RECORD *) header;

		offset += header->ucRecordSize;
	}

	return NULL;
}

static enum bp_result get_ss_info_from_ss_info_table(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *ss_info);
static enum bp_result get_ss_info_from_tbl(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *ss_info);
/**
 * bios_parser_get_spread_spectrum_info
 * Get spread spectrum information from the ASIC_InternalSS_Info(ver 2.1 or
 * ver 3.1) or SS_Info table from the VBIOS. Currently ASIC_InternalSS_Info
 * ver 2.1 can co-exist with SS_Info table. Expect ASIC_InternalSS_Info ver 3.1,
 * there is only one entry for each signal /ss id.  However, there is
 * no planning of supporting multiple spread Sprectum entry for EverGreen
 * @dcb:     pointer to the DC BIOS
 * @signal:  ASSignalType to be converted to info index
 * @index:   number of entries that match the converted info index
 * @ss_info: sprectrum information structure,
 * return:   Bios parser result code
 */
static enum bp_result bios_parser_get_spread_spectrum_info(
	struct dc_bios *dcb,
	enum as_signal_type signal,
	uint32_t index,
	struct spread_spectrum_info *ss_info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	uint32_t clk_id_ss = 0;
	ATOM_COMMON_TABLE_HEADER *header;
	struct atom_data_revision tbl_revision;

	if (!ss_info) /* check for bad input */
		return BP_RESULT_BADINPUT;
	/* signal translation */
	clk_id_ss = signal_to_ss_id(signal);

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		if (!index)
			return get_ss_info_from_ss_info_table(bp, clk_id_ss,
				ss_info);

	header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER,
		DATA_TABLES(ASIC_InternalSS_Info));
	get_atom_data_table_revision(header, &tbl_revision);

	switch (tbl_revision.major) {
	case 2:
		switch (tbl_revision.minor) {
		case 1:
			/* there can not be more then one entry for Internal
			 * SS Info table version 2.1 */
			if (!index)
				return get_ss_info_from_tbl(bp, clk_id_ss,
						ss_info);
			break;
		default:
			break;
		}
		break;

	case 3:
		switch (tbl_revision.minor) {
		case 1:
			return get_ss_info_v3_1(bp, clk_id_ss, index, ss_info);
		default:
			break;
		}
		break;
	default:
		break;
	}
	/* there can not be more then one entry for SS Info table */
	return result;
}

static enum bp_result get_ss_info_from_internal_ss_info_tbl_V2_1(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *info);

/**
 * get_ss_info_from_tbl
 * Get spread sprectrum information from the ASIC_InternalSS_Info Ver 2.1 or
 * SS_Info table from the VBIOS
 * There can not be more than 1 entry for  ASIC_InternalSS_Info Ver 2.1 or
 * SS_Info.
 *
 * @bp:      pointer to the BIOS parser
 * @id:      spread sprectrum info index
 * @ss_info: sprectrum information structure,
 * return:   BIOS parser result code
 */
static enum bp_result get_ss_info_from_tbl(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *ss_info)
{
	if (!ss_info) /* check for bad input, if ss_info is not NULL */
		return BP_RESULT_BADINPUT;
	/* for SS_Info table only support DP and LVDS */
	if (id == ASIC_INTERNAL_SS_ON_DP || id == ASIC_INTERNAL_SS_ON_LVDS)
		return get_ss_info_from_ss_info_table(bp, id, ss_info);
	else
		return get_ss_info_from_internal_ss_info_tbl_V2_1(bp, id,
			ss_info);
}

/**
 * get_ss_info_from_internal_ss_info_tbl_V2_1
 * Get spread sprectrum information from the ASIC_InternalSS_Info table Ver 2.1
 * from the VBIOS
 * There will not be multiple entry for Ver 2.1
 *
 * @bp:    pointer to the Bios parser
 * @id:    spread sprectrum info index
 * @info:  sprectrum information structure,
 * return: Bios parser result code
 */
static enum bp_result get_ss_info_from_internal_ss_info_tbl_V2_1(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *info)
{
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	ATOM_ASIC_INTERNAL_SS_INFO_V2 *header;
	ATOM_ASIC_SS_ASSIGNMENT_V2 *tbl;
	uint32_t tbl_size, i;

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		return result;

	header = ((ATOM_ASIC_INTERNAL_SS_INFO_V2 *) bios_get_image(
				&bp->base,
				DATA_TABLES(ASIC_InternalSS_Info),
				struct_size(header, asSpreadSpectrum, 1)));

	memset(info, 0, sizeof(struct spread_spectrum_info));

	tbl_size = (le16_to_cpu(header->sHeader.usStructureSize)
			- sizeof(ATOM_COMMON_TABLE_HEADER))
					/ sizeof(ATOM_ASIC_SS_ASSIGNMENT_V2);

	tbl = (ATOM_ASIC_SS_ASSIGNMENT_V2 *)
					&(header->asSpreadSpectrum[0]);
	for (i = 0; i < tbl_size; i++) {
		result = BP_RESULT_NORECORD;

		if (tbl[i].ucClockIndication != (uint8_t)id)
			continue;

		if (ATOM_EXTERNAL_SS_MASK
			& tbl[i].ucSpreadSpectrumMode) {
			info->type.EXTERNAL = true;
		}
		if (ATOM_SS_CENTRE_SPREAD_MODE_MASK
			& tbl[i].ucSpreadSpectrumMode) {
			info->type.CENTER_MODE = true;
		}
		info->type.STEP_AND_DELAY_INFO = false;
		/* convert [10KHz] into [KHz] */
		info->target_clock_range =
			le32_to_cpu(tbl[i].ulTargetClockRange) * 10;
		info->spread_spectrum_percentage =
			(uint32_t)le16_to_cpu(tbl[i].usSpreadSpectrumPercentage);
		info->spread_spectrum_range =
			(uint32_t)(le16_to_cpu(tbl[i].usSpreadRateIn10Hz) * 10);
		result = BP_RESULT_OK;
		break;
	}

	return result;

}

/**
 * get_ss_info_from_ss_info_table
 * Get spread sprectrum information from the SS_Info table from the VBIOS
 * if the pointer to info is NULL, indicate the caller what to know the number
 * of entries that matches the id
 * for, the SS_Info table, there should not be more than 1 entry match.
 *
 * @bp:      pointer to the Bios parser
 * @id:      spread sprectrum id
 * @ss_info: sprectrum information structure,
 * return:   Bios parser result code
 */
static enum bp_result get_ss_info_from_ss_info_table(
	struct bios_parser *bp,
	uint32_t id,
	struct spread_spectrum_info *ss_info)
{
	enum bp_result result = BP_RESULT_UNSUPPORTED;
	ATOM_SPREAD_SPECTRUM_INFO *tbl;
	ATOM_COMMON_TABLE_HEADER *header;
	uint32_t table_size;
	uint32_t i;
	uint32_t id_local = SS_ID_UNKNOWN;
	struct atom_data_revision revision;

	/* exist of the SS_Info table */
	/* check for bad input, pSSinfo can not be NULL */
	if (!DATA_TABLES(SS_Info) || !ss_info)
		return result;

	header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER, DATA_TABLES(SS_Info));
	get_atom_data_table_revision(header, &revision);

	tbl = GET_IMAGE(ATOM_SPREAD_SPECTRUM_INFO, DATA_TABLES(SS_Info));

	if (1 != revision.major || 2 > revision.minor)
		return result;

	/* have to convert from Internal_SS format to SS_Info format */
	switch (id) {
	case ASIC_INTERNAL_SS_ON_DP:
		id_local = SS_ID_DP1;
		break;
	case ASIC_INTERNAL_SS_ON_LVDS:
	{
		struct embedded_panel_info panel_info;

		if (bios_parser_get_embedded_panel_info(&bp->base, &panel_info)
				== BP_RESULT_OK)
			id_local = panel_info.ss_id;
		break;
	}
	default:
		break;
	}

	if (id_local == SS_ID_UNKNOWN)
		return result;

	table_size = (le16_to_cpu(tbl->sHeader.usStructureSize) -
			sizeof(ATOM_COMMON_TABLE_HEADER)) /
					sizeof(ATOM_SPREAD_SPECTRUM_ASSIGNMENT);

	for (i = 0; i < table_size; i++) {
		if (id_local != (uint32_t)tbl->asSS_Info[i].ucSS_Id)
			continue;

		memset(ss_info, 0, sizeof(struct spread_spectrum_info));

		if (ATOM_EXTERNAL_SS_MASK &
				tbl->asSS_Info[i].ucSpreadSpectrumType)
			ss_info->type.EXTERNAL = true;

		if (ATOM_SS_CENTRE_SPREAD_MODE_MASK &
				tbl->asSS_Info[i].ucSpreadSpectrumType)
			ss_info->type.CENTER_MODE = true;

		ss_info->type.STEP_AND_DELAY_INFO = true;
		ss_info->spread_spectrum_percentage =
			(uint32_t)le16_to_cpu(tbl->asSS_Info[i].usSpreadSpectrumPercentage);
		ss_info->step_and_delay_info.step = tbl->asSS_Info[i].ucSS_Step;
		ss_info->step_and_delay_info.delay =
			tbl->asSS_Info[i].ucSS_Delay;
		ss_info->step_and_delay_info.recommended_ref_div =
			tbl->asSS_Info[i].ucRecommendedRef_Div;
		ss_info->spread_spectrum_range =
			(uint32_t)tbl->asSS_Info[i].ucSS_Range * 10000;

		/* there will be only one entry for each display type in SS_info
		 * table */
		result = BP_RESULT_OK;
		break;
	}

	return result;
}
static enum bp_result get_embedded_panel_info_v1_2(
	struct bios_parser *bp,
	struct embedded_panel_info *info);
static enum bp_result get_embedded_panel_info_v1_3(
	struct bios_parser *bp,
	struct embedded_panel_info *info);

static enum bp_result bios_parser_get_embedded_panel_info(
	struct dc_bios *dcb,
	struct embedded_panel_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	ATOM_COMMON_TABLE_HEADER *hdr;

	if (!DATA_TABLES(LCD_Info))
		return BP_RESULT_FAILURE;

	hdr = GET_IMAGE(ATOM_COMMON_TABLE_HEADER, DATA_TABLES(LCD_Info));

	if (!hdr)
		return BP_RESULT_BADBIOSTABLE;

	switch (hdr->ucTableFormatRevision) {
	case 1:
		switch (hdr->ucTableContentRevision) {
		case 0:
		case 1:
		case 2:
			return get_embedded_panel_info_v1_2(bp, info);
		case 3:
			return get_embedded_panel_info_v1_3(bp, info);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return BP_RESULT_FAILURE;
}

static enum bp_result get_embedded_panel_info_v1_2(
	struct bios_parser *bp,
	struct embedded_panel_info *info)
{
	ATOM_LVDS_INFO_V12 *lvds;

	if (!info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(LVDS_Info))
		return BP_RESULT_UNSUPPORTED;

	lvds =
		GET_IMAGE(ATOM_LVDS_INFO_V12, DATA_TABLES(LVDS_Info));

	if (!lvds)
		return BP_RESULT_BADBIOSTABLE;

	if (1 != lvds->sHeader.ucTableFormatRevision
		|| 2 > lvds->sHeader.ucTableContentRevision)
		return BP_RESULT_UNSUPPORTED;

	memset(info, 0, sizeof(struct embedded_panel_info));

	/* We need to convert from 10KHz units into KHz units*/
	info->lcd_timing.pixel_clk =
		le16_to_cpu(lvds->sLCDTiming.usPixClk) * 10;
	/* usHActive does not include borders, according to VBIOS team*/
	info->lcd_timing.horizontal_addressable =
		le16_to_cpu(lvds->sLCDTiming.usHActive);
	/* usHBlanking_Time includes borders, so we should really be subtracting
	 * borders duing this translation, but LVDS generally*/
	/* doesn't have borders, so we should be okay leaving this as is for
	 * now.  May need to revisit if we ever have LVDS with borders*/
	info->lcd_timing.horizontal_blanking_time =
			le16_to_cpu(lvds->sLCDTiming.usHBlanking_Time);
	/* usVActive does not include borders, according to VBIOS team*/
	info->lcd_timing.vertical_addressable =
			le16_to_cpu(lvds->sLCDTiming.usVActive);
	/* usVBlanking_Time includes borders, so we should really be subtracting
	 * borders duing this translation, but LVDS generally*/
	/* doesn't have borders, so we should be okay leaving this as is for
	 * now. May need to revisit if we ever have LVDS with borders*/
	info->lcd_timing.vertical_blanking_time =
		le16_to_cpu(lvds->sLCDTiming.usVBlanking_Time);
	info->lcd_timing.horizontal_sync_offset =
		le16_to_cpu(lvds->sLCDTiming.usHSyncOffset);
	info->lcd_timing.horizontal_sync_width =
		le16_to_cpu(lvds->sLCDTiming.usHSyncWidth);
	info->lcd_timing.vertical_sync_offset =
		le16_to_cpu(lvds->sLCDTiming.usVSyncOffset);
	info->lcd_timing.vertical_sync_width =
		le16_to_cpu(lvds->sLCDTiming.usVSyncWidth);
	info->lcd_timing.horizontal_border = lvds->sLCDTiming.ucHBorder;
	info->lcd_timing.vertical_border = lvds->sLCDTiming.ucVBorder;
	info->lcd_timing.misc_info.HORIZONTAL_CUT_OFF =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.HorizontalCutOff;
	info->lcd_timing.misc_info.H_SYNC_POLARITY =
		~(uint32_t)
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.HSyncPolarity;
	info->lcd_timing.misc_info.V_SYNC_POLARITY =
		~(uint32_t)
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.VSyncPolarity;
	info->lcd_timing.misc_info.VERTICAL_CUT_OFF =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.VerticalCutOff;
	info->lcd_timing.misc_info.H_REPLICATION_BY2 =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.H_ReplicationBy2;
	info->lcd_timing.misc_info.V_REPLICATION_BY2 =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.V_ReplicationBy2;
	info->lcd_timing.misc_info.COMPOSITE_SYNC =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.CompositeSync;
	info->lcd_timing.misc_info.INTERLACE =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.Interlace;
	info->lcd_timing.misc_info.DOUBLE_CLOCK =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.DoubleClock;
	info->ss_id = lvds->ucSS_Id;

	{
		uint8_t rr = le16_to_cpu(lvds->usSupportedRefreshRate);
		/* Get minimum supported refresh rate*/
		if (SUPPORTED_LCD_REFRESHRATE_30Hz & rr)
			info->supported_rr.REFRESH_RATE_30HZ = 1;
		else if (SUPPORTED_LCD_REFRESHRATE_40Hz & rr)
			info->supported_rr.REFRESH_RATE_40HZ = 1;
		else if (SUPPORTED_LCD_REFRESHRATE_48Hz & rr)
			info->supported_rr.REFRESH_RATE_48HZ = 1;
		else if (SUPPORTED_LCD_REFRESHRATE_50Hz & rr)
			info->supported_rr.REFRESH_RATE_50HZ = 1;
		else if (SUPPORTED_LCD_REFRESHRATE_60Hz & rr)
			info->supported_rr.REFRESH_RATE_60HZ = 1;
	}

	/*Drr panel support can be reported by VBIOS*/
	if (LCDPANEL_CAP_DRR_SUPPORTED
			& lvds->ucLCDPanel_SpecialHandlingCap)
		info->drr_enabled = 1;

	if (ATOM_PANEL_MISC_DUAL & lvds->ucLVDS_Misc)
		info->lcd_timing.misc_info.DOUBLE_CLOCK = true;

	if (ATOM_PANEL_MISC_888RGB & lvds->ucLVDS_Misc)
		info->lcd_timing.misc_info.RGB888 = true;

	info->lcd_timing.misc_info.GREY_LEVEL =
		(uint32_t) (ATOM_PANEL_MISC_GREY_LEVEL &
			lvds->ucLVDS_Misc) >> ATOM_PANEL_MISC_GREY_LEVEL_SHIFT;

	if (ATOM_PANEL_MISC_SPATIAL & lvds->ucLVDS_Misc)
		info->lcd_timing.misc_info.SPATIAL = true;

	if (ATOM_PANEL_MISC_TEMPORAL & lvds->ucLVDS_Misc)
		info->lcd_timing.misc_info.TEMPORAL = true;

	if (ATOM_PANEL_MISC_API_ENABLED & lvds->ucLVDS_Misc)
		info->lcd_timing.misc_info.API_ENABLED = true;

	return BP_RESULT_OK;
}

static enum bp_result get_embedded_panel_info_v1_3(
	struct bios_parser *bp,
	struct embedded_panel_info *info)
{
	ATOM_LCD_INFO_V13 *lvds;

	if (!info)
		return BP_RESULT_BADINPUT;

	if (!DATA_TABLES(LCD_Info))
		return BP_RESULT_UNSUPPORTED;

	lvds = GET_IMAGE(ATOM_LCD_INFO_V13, DATA_TABLES(LCD_Info));

	if (!lvds)
		return BP_RESULT_BADBIOSTABLE;

	if (!((1 == lvds->sHeader.ucTableFormatRevision)
			&& (3 <= lvds->sHeader.ucTableContentRevision)))
		return BP_RESULT_UNSUPPORTED;

	memset(info, 0, sizeof(struct embedded_panel_info));

	/* We need to convert from 10KHz units into KHz units */
	info->lcd_timing.pixel_clk =
			le16_to_cpu(lvds->sLCDTiming.usPixClk) * 10;
	/* usHActive does not include borders, according to VBIOS team */
	info->lcd_timing.horizontal_addressable =
			le16_to_cpu(lvds->sLCDTiming.usHActive);
	/* usHBlanking_Time includes borders, so we should really be subtracting
	 * borders duing this translation, but LVDS generally*/
	/* doesn't have borders, so we should be okay leaving this as is for
	 * now.  May need to revisit if we ever have LVDS with borders*/
	info->lcd_timing.horizontal_blanking_time =
		le16_to_cpu(lvds->sLCDTiming.usHBlanking_Time);
	/* usVActive does not include borders, according to VBIOS team*/
	info->lcd_timing.vertical_addressable =
		le16_to_cpu(lvds->sLCDTiming.usVActive);
	/* usVBlanking_Time includes borders, so we should really be subtracting
	 * borders duing this translation, but LVDS generally*/
	/* doesn't have borders, so we should be okay leaving this as is for
	 * now. May need to revisit if we ever have LVDS with borders*/
	info->lcd_timing.vertical_blanking_time =
		le16_to_cpu(lvds->sLCDTiming.usVBlanking_Time);
	info->lcd_timing.horizontal_sync_offset =
		le16_to_cpu(lvds->sLCDTiming.usHSyncOffset);
	info->lcd_timing.horizontal_sync_width =
		le16_to_cpu(lvds->sLCDTiming.usHSyncWidth);
	info->lcd_timing.vertical_sync_offset =
		le16_to_cpu(lvds->sLCDTiming.usVSyncOffset);
	info->lcd_timing.vertical_sync_width =
		le16_to_cpu(lvds->sLCDTiming.usVSyncWidth);
	info->lcd_timing.horizontal_border = lvds->sLCDTiming.ucHBorder;
	info->lcd_timing.vertical_border = lvds->sLCDTiming.ucVBorder;
	info->lcd_timing.misc_info.HORIZONTAL_CUT_OFF =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.HorizontalCutOff;
	info->lcd_timing.misc_info.H_SYNC_POLARITY =
		~(uint32_t)
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.HSyncPolarity;
	info->lcd_timing.misc_info.V_SYNC_POLARITY =
		~(uint32_t)
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.VSyncPolarity;
	info->lcd_timing.misc_info.VERTICAL_CUT_OFF =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.VerticalCutOff;
	info->lcd_timing.misc_info.H_REPLICATION_BY2 =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.H_ReplicationBy2;
	info->lcd_timing.misc_info.V_REPLICATION_BY2 =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.V_ReplicationBy2;
	info->lcd_timing.misc_info.COMPOSITE_SYNC =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.CompositeSync;
	info->lcd_timing.misc_info.INTERLACE =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.Interlace;
	info->lcd_timing.misc_info.DOUBLE_CLOCK =
		lvds->sLCDTiming.susModeMiscInfo.sbfAccess.DoubleClock;
	info->ss_id = lvds->ucSS_Id;

	/* Drr panel support can be reported by VBIOS*/
	if (LCDPANEL_CAP_V13_DRR_SUPPORTED
			& lvds->ucLCDPanel_SpecialHandlingCap)
		info->drr_enabled = 1;

	/* Get supported refresh rate*/
	if (info->drr_enabled == 1) {
		uint8_t min_rr =
				lvds->sRefreshRateSupport.ucMinRefreshRateForDRR;
		uint8_t rr = lvds->sRefreshRateSupport.ucSupportedRefreshRate;

		if (min_rr != 0) {
			if (SUPPORTED_LCD_REFRESHRATE_30Hz & min_rr)
				info->supported_rr.REFRESH_RATE_30HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_40Hz & min_rr)
				info->supported_rr.REFRESH_RATE_40HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_48Hz & min_rr)
				info->supported_rr.REFRESH_RATE_48HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_50Hz & min_rr)
				info->supported_rr.REFRESH_RATE_50HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_60Hz & min_rr)
				info->supported_rr.REFRESH_RATE_60HZ = 1;
		} else {
			if (SUPPORTED_LCD_REFRESHRATE_30Hz & rr)
				info->supported_rr.REFRESH_RATE_30HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_40Hz & rr)
				info->supported_rr.REFRESH_RATE_40HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_48Hz & rr)
				info->supported_rr.REFRESH_RATE_48HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_50Hz & rr)
				info->supported_rr.REFRESH_RATE_50HZ = 1;
			else if (SUPPORTED_LCD_REFRESHRATE_60Hz & rr)
				info->supported_rr.REFRESH_RATE_60HZ = 1;
		}
	}

	if (ATOM_PANEL_MISC_V13_DUAL & lvds->ucLCD_Misc)
		info->lcd_timing.misc_info.DOUBLE_CLOCK = true;

	if (ATOM_PANEL_MISC_V13_8BIT_PER_COLOR & lvds->ucLCD_Misc)
		info->lcd_timing.misc_info.RGB888 = true;

	info->lcd_timing.misc_info.GREY_LEVEL =
			(uint32_t) (ATOM_PANEL_MISC_V13_GREY_LEVEL &
				lvds->ucLCD_Misc) >> ATOM_PANEL_MISC_V13_GREY_LEVEL_SHIFT;

	return BP_RESULT_OK;
}

/**
 * bios_parser_get_encoder_cap_info - get encoder capability
 *                                    information of input object id
 *
 * @dcb:       pointer to the DC BIOS
 * @object_id: object id
 * @info:      encoder cap information structure
 *
 * return: Bios parser result code
 */
static enum bp_result bios_parser_get_encoder_cap_info(
	struct dc_bios *dcb,
	struct graphics_object_id object_id,
	struct bp_encoder_cap_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	ATOM_OBJECT *object;
	ATOM_ENCODER_CAP_RECORD_V2 *record = NULL;

	if (!info)
		return BP_RESULT_BADINPUT;

	object = get_bios_object(bp, object_id);

	if (!object)
		return BP_RESULT_BADINPUT;

	record = get_encoder_cap_record(bp, object);
	if (!record)
		return BP_RESULT_NORECORD;

	info->DP_HBR2_EN = record->usHBR2En;
	info->DP_HBR3_EN = record->usHBR3En;
	info->HDMI_6GB_EN = record->usHDMI6GEn;
	return BP_RESULT_OK;
}

/**
 * get_encoder_cap_record - Get encoder cap record for the object
 *
 * @bp:      pointer to the BIOS parser
 * @object:  ATOM object
 * return:   atom encoder cap record
 * note:     search all records to find the ATOM_ENCODER_CAP_RECORD_V2 record
 */
static ATOM_ENCODER_CAP_RECORD_V2 *get_encoder_cap_record(
	struct bios_parser *bp,
	ATOM_OBJECT *object)
{
	ATOM_COMMON_RECORD_HEADER *header;
	uint32_t offset;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object */
		return NULL;
	}

	offset = le16_to_cpu(object->usRecordOffset)
					+ bp->object_info_tbl_offset;

	for (;;) {
		header = GET_IMAGE(ATOM_COMMON_RECORD_HEADER, offset);

		if (!header)
			return NULL;

		offset += header->ucRecordSize;

		if (LAST_RECORD_TYPE == header->ucRecordType ||
				!header->ucRecordSize)
			break;

		if (ATOM_ENCODER_CAP_RECORD_TYPE != header->ucRecordType)
			continue;

		if (sizeof(ATOM_ENCODER_CAP_RECORD_V2) <= header->ucRecordSize)
			return (ATOM_ENCODER_CAP_RECORD_V2 *)header;
	}

	return NULL;
}

static uint32_t get_ss_entry_number(
	struct bios_parser *bp,
	uint32_t id);
static uint32_t get_ss_entry_number_from_internal_ss_info_tbl_v2_1(
	struct bios_parser *bp,
	uint32_t id);
static uint32_t get_ss_entry_number_from_internal_ss_info_tbl_V3_1(
	struct bios_parser *bp,
	uint32_t id);
static uint32_t get_ss_entry_number_from_ss_info_tbl(
	struct bios_parser *bp,
	uint32_t id);

/**
 * bios_parser_get_ss_entry_number
 * Get Number of SpreadSpectrum Entry from the ASIC_InternalSS_Info table from
 * the VBIOS that match the SSid (to be converted from signal)
 *
 * @dcb:    pointer to the DC BIOS
 * @signal: ASSignalType to be converted to SSid
 * return: number of SS Entry that match the signal
 */
static uint32_t bios_parser_get_ss_entry_number(
	struct dc_bios *dcb,
	enum as_signal_type signal)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	uint32_t ss_id = 0;
	ATOM_COMMON_TABLE_HEADER *header;
	struct atom_data_revision revision;

	ss_id = signal_to_ss_id(signal);

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		return get_ss_entry_number_from_ss_info_tbl(bp, ss_id);

	header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER,
			DATA_TABLES(ASIC_InternalSS_Info));
	get_atom_data_table_revision(header, &revision);

	switch (revision.major) {
	case 2:
		switch (revision.minor) {
		case 1:
			return get_ss_entry_number(bp, ss_id);
		default:
			break;
		}
		break;
	case 3:
		switch (revision.minor) {
		case 1:
			return
				get_ss_entry_number_from_internal_ss_info_tbl_V3_1(
						bp, ss_id);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

/**
 * get_ss_entry_number_from_ss_info_tbl
 * Get Number of spread spectrum entry from the SS_Info table from the VBIOS.
 *
 * @bp:  pointer to the BIOS parser
 * @id:  spread spectrum id
 * return: number of SS Entry that match the id
 * note: There can only be one entry for each id for SS_Info Table
 */
static uint32_t get_ss_entry_number_from_ss_info_tbl(
	struct bios_parser *bp,
	uint32_t id)
{
	ATOM_SPREAD_SPECTRUM_INFO *tbl;
	ATOM_COMMON_TABLE_HEADER *header;
	uint32_t table_size;
	uint32_t i;
	uint32_t number = 0;
	uint32_t id_local = SS_ID_UNKNOWN;
	struct atom_data_revision revision;

	/* SS_Info table exist */
	if (!DATA_TABLES(SS_Info))
		return number;

	header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER,
			DATA_TABLES(SS_Info));
	get_atom_data_table_revision(header, &revision);

	tbl = GET_IMAGE(ATOM_SPREAD_SPECTRUM_INFO,
			DATA_TABLES(SS_Info));

	if (1 != revision.major || 2 > revision.minor)
		return number;

	/* have to convert from Internal_SS format to SS_Info format */
	switch (id) {
	case ASIC_INTERNAL_SS_ON_DP:
		id_local = SS_ID_DP1;
		break;
	case ASIC_INTERNAL_SS_ON_LVDS: {
		struct embedded_panel_info panel_info;

		if (bios_parser_get_embedded_panel_info(&bp->base, &panel_info)
				== BP_RESULT_OK)
			id_local = panel_info.ss_id;
		break;
	}
	default:
		break;
	}

	if (id_local == SS_ID_UNKNOWN)
		return number;

	table_size = (le16_to_cpu(tbl->sHeader.usStructureSize) -
			sizeof(ATOM_COMMON_TABLE_HEADER)) /
					sizeof(ATOM_SPREAD_SPECTRUM_ASSIGNMENT);

	for (i = 0; i < table_size; i++)
		if (id_local == (uint32_t)tbl->asSS_Info[i].ucSS_Id) {
			number = 1;
			break;
		}

	return number;
}

/**
 * get_ss_entry_number
 * Get spread sprectrum information from the ASIC_InternalSS_Info Ver 2.1 or
 * SS_Info table from the VBIOS
 * There can not be more than 1 entry for  ASIC_InternalSS_Info Ver 2.1 or
 * SS_Info.
 *
 * @bp:    pointer to the BIOS parser
 * @id:    spread sprectrum info index
 * return: Bios parser result code
 */
static uint32_t get_ss_entry_number(struct bios_parser *bp, uint32_t id)
{
	if (id == ASIC_INTERNAL_SS_ON_DP || id == ASIC_INTERNAL_SS_ON_LVDS)
		return get_ss_entry_number_from_ss_info_tbl(bp, id);

	return get_ss_entry_number_from_internal_ss_info_tbl_v2_1(bp, id);
}

/**
 * get_ss_entry_number_from_internal_ss_info_tbl_v2_1
 * Get NUmber of spread sprectrum entry from the ASIC_InternalSS_Info table
 * Ver 2.1 from the VBIOS
 * There will not be multiple entry for Ver 2.1
 *
 * @bp:    pointer to the BIOS parser
 * @id:    spread sprectrum info index
 * return: number of SS Entry that match the id
 */
static uint32_t get_ss_entry_number_from_internal_ss_info_tbl_v2_1(
	struct bios_parser *bp,
	uint32_t id)
{
	ATOM_ASIC_INTERNAL_SS_INFO_V2 *header_include;
	ATOM_ASIC_SS_ASSIGNMENT_V2 *tbl;
	uint32_t size;
	uint32_t i;

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		return 0;

	header_include = ((ATOM_ASIC_INTERNAL_SS_INFO_V2 *) bios_get_image(
				&bp->base,
				DATA_TABLES(ASIC_InternalSS_Info),
				struct_size(header_include, asSpreadSpectrum, 1)));

	size = (le16_to_cpu(header_include->sHeader.usStructureSize)
			- sizeof(ATOM_COMMON_TABLE_HEADER))
						/ sizeof(ATOM_ASIC_SS_ASSIGNMENT_V2);

	tbl = (ATOM_ASIC_SS_ASSIGNMENT_V2 *)
				&header_include->asSpreadSpectrum[0];
	for (i = 0; i < size; i++)
		if (tbl[i].ucClockIndication == (uint8_t)id)
			return 1;

	return 0;
}

/**
 * get_ss_entry_number_from_internal_ss_info_tbl_V3_1
 * Get Number of SpreadSpectrum Entry from the ASIC_InternalSS_Info table of
 * the VBIOS that matches id
 *
 * @bp:    pointer to the BIOS parser
 * @id:    spread sprectrum id
 * return: number of SS Entry that match the id
 */
static uint32_t get_ss_entry_number_from_internal_ss_info_tbl_V3_1(
	struct bios_parser *bp,
	uint32_t id)
{
	uint32_t number = 0;
	ATOM_ASIC_INTERNAL_SS_INFO_V3 *header_include;
	ATOM_ASIC_SS_ASSIGNMENT_V3 *tbl;
	uint32_t size;
	uint32_t i;

	if (!DATA_TABLES(ASIC_InternalSS_Info))
		return number;

	header_include = ((ATOM_ASIC_INTERNAL_SS_INFO_V3 *) bios_get_image(&bp->base,
				DATA_TABLES(ASIC_InternalSS_Info),
				struct_size(header_include, asSpreadSpectrum, 1)));
	size = (le16_to_cpu(header_include->sHeader.usStructureSize) -
			sizeof(ATOM_COMMON_TABLE_HEADER)) /
					sizeof(ATOM_ASIC_SS_ASSIGNMENT_V3);

	tbl = (ATOM_ASIC_SS_ASSIGNMENT_V3 *)
				&header_include->asSpreadSpectrum[0];

	for (i = 0; i < size; i++)
		if (tbl[i].ucClockIndication == (uint8_t)id)
			number++;

	return number;
}

/**
 * bios_parser_get_gpio_pin_info
 * Get GpioPin information of input gpio id
 *
 * @dcb:     pointer to the DC BIOS
 * @gpio_id: GPIO ID
 * @info:    GpioPin information structure
 * return:   Bios parser result code
 * note:
 *  to get the GPIO PIN INFO, we need:
 *  1. get the GPIO_ID from other object table, see GetHPDInfo()
 *  2. in DATA_TABLE.GPIO_Pin_LUT, search all records, to get the registerA
 *  offset/mask
 */
static enum bp_result bios_parser_get_gpio_pin_info(
	struct dc_bios *dcb,
	uint32_t gpio_id,
	struct gpio_pin_info *info)
{
	struct bios_parser *bp = BP_FROM_DCB(dcb);
	ATOM_GPIO_PIN_LUT *header;
	uint32_t count = 0;
	uint32_t i = 0;

	if (!DATA_TABLES(GPIO_Pin_LUT))
		return BP_RESULT_BADBIOSTABLE;

	header = ((ATOM_GPIO_PIN_LUT *) bios_get_image(&bp->base,
				DATA_TABLES(GPIO_Pin_LUT),
				struct_size(header, asGPIO_Pin, 1)));
	if (!header)
		return BP_RESULT_BADBIOSTABLE;

	if (sizeof(ATOM_COMMON_TABLE_HEADER) + struct_size(header, asGPIO_Pin, 1)
			> le16_to_cpu(header->sHeader.usStructureSize))
		return BP_RESULT_BADBIOSTABLE;

	if (1 != header->sHeader.ucTableContentRevision)
		return BP_RESULT_UNSUPPORTED;

	count = (le16_to_cpu(header->sHeader.usStructureSize)
			- sizeof(ATOM_COMMON_TABLE_HEADER))
				/ sizeof(ATOM_GPIO_PIN_ASSIGNMENT);
	for (i = 0; i < count; ++i) {
		if (header->asGPIO_Pin[i].ucGPIO_ID != gpio_id)
			continue;

		info->offset =
			(uint32_t) le16_to_cpu(header->asGPIO_Pin[i].usGpioPin_AIndex);
		info->offset_y = info->offset + 2;
		info->offset_en = info->offset + 1;
		info->offset_mask = info->offset - 1;

		info->mask = (uint32_t) (1 <<
			header->asGPIO_Pin[i].ucGpioPinBitShift);
		info->mask_y = info->mask + 2;
		info->mask_en = info->mask + 1;
		info->mask_mask = info->mask - 1;

		return BP_RESULT_OK;
	}

	return BP_RESULT_NORECORD;
}

static enum bp_result get_gpio_i2c_info(struct bios_parser *bp,
	ATOM_I2C_RECORD *record,
	struct graphics_object_i2c_info *info)
{
	ATOM_GPIO_I2C_INFO *header;
	uint32_t count = 0;

	if (!info)
		return BP_RESULT_BADINPUT;

	/* get the GPIO_I2C info */
	if (!DATA_TABLES(GPIO_I2C_Info))
		return BP_RESULT_BADBIOSTABLE;

	header = GET_IMAGE(ATOM_GPIO_I2C_INFO, DATA_TABLES(GPIO_I2C_Info));
	if (!header)
		return BP_RESULT_BADBIOSTABLE;

	if (sizeof(ATOM_COMMON_TABLE_HEADER) + sizeof(ATOM_GPIO_I2C_ASSIGMENT)
			> le16_to_cpu(header->sHeader.usStructureSize))
		return BP_RESULT_BADBIOSTABLE;

	if (1 != header->sHeader.ucTableContentRevision)
		return BP_RESULT_UNSUPPORTED;

	/* get data count */
	count = (le16_to_cpu(header->sHeader.usStructureSize)
			- sizeof(ATOM_COMMON_TABLE_HEADER))
				/ sizeof(ATOM_GPIO_I2C_ASSIGMENT);
	if (count < record->sucI2cId.bfI2C_LineMux)
		return BP_RESULT_BADBIOSTABLE;

	/* get the GPIO_I2C_INFO */
	info->i2c_hw_assist = record->sucI2cId.bfHW_Capable;
	info->i2c_line = record->sucI2cId.bfI2C_LineMux;
	info->i2c_engine_id = record->sucI2cId.bfHW_EngineID;
	info->i2c_slave_address = record->ucI2CAddr;

	info->gpio_info.clk_mask_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usClkMaskRegisterIndex);
	info->gpio_info.clk_en_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usClkEnRegisterIndex);
	info->gpio_info.clk_y_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usClkY_RegisterIndex);
	info->gpio_info.clk_a_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usClkA_RegisterIndex);
	info->gpio_info.data_mask_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usDataMaskRegisterIndex);
	info->gpio_info.data_en_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usDataEnRegisterIndex);
	info->gpio_info.data_y_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usDataY_RegisterIndex);
	info->gpio_info.data_a_register_index =
			le16_to_cpu(header->asGPIO_Info[info->i2c_line].usDataA_RegisterIndex);

	info->gpio_info.clk_mask_shift =
			header->asGPIO_Info[info->i2c_line].ucClkMaskShift;
	info->gpio_info.clk_en_shift =
			header->asGPIO_Info[info->i2c_line].ucClkEnShift;
	info->gpio_info.clk_y_shift =
			header->asGPIO_Info[info->i2c_line].ucClkY_Shift;
	info->gpio_info.clk_a_shift =
			header->asGPIO_Info[info->i2c_line].ucClkA_Shift;
	info->gpio_info.data_mask_shift =
			header->asGPIO_Info[info->i2c_line].ucDataMaskShift;
	info->gpio_info.data_en_shift =
			header->asGPIO_Info[info->i2c_line].ucDataEnShift;
	info->gpio_info.data_y_shift =
			header->asGPIO_Info[info->i2c_line].ucDataY_Shift;
	info->gpio_info.data_a_shift =
			header->asGPIO_Info[info->i2c_line].ucDataA_Shift;

	return BP_RESULT_OK;
}

static bool dal_graphics_object_id_is_valid(struct graphics_object_id id)
{
	bool rc = true;

	switch (id.type) {
	case OBJECT_TYPE_UNKNOWN:
		rc = false;
		break;
	case OBJECT_TYPE_GPU:
	case OBJECT_TYPE_ENGINE:
		/* do NOT check for id.id == 0 */
		if (id.enum_id == ENUM_ID_UNKNOWN)
			rc = false;
		break;
	default:
		if (id.id == 0 || id.enum_id == ENUM_ID_UNKNOWN)
			rc = false;
		break;
	}

	return rc;
}

static bool dal_graphics_object_id_is_equal(
	struct graphics_object_id id1,
	struct graphics_object_id id2)
{
	if (false == dal_graphics_object_id_is_valid(id1)) {
		dm_output_to_console(
		"%s: Warning: comparing invalid object 'id1'!\n", __func__);
		return false;
	}

	if (false == dal_graphics_object_id_is_valid(id2)) {
		dm_output_to_console(
		"%s: Warning: comparing invalid object 'id2'!\n", __func__);
		return false;
	}

	if (id1.id == id2.id && id1.enum_id == id2.enum_id
		&& id1.type == id2.type)
		return true;

	return false;
}

static ATOM_OBJECT *get_bios_object(struct bios_parser *bp,
	struct graphics_object_id id)
{
	uint32_t offset;
	ATOM_OBJECT_TABLE *tbl;
	uint32_t i;

	switch (id.type) {
	case OBJECT_TYPE_ENCODER:
		offset = le16_to_cpu(bp->object_info_tbl.v1_1->usEncoderObjectTableOffset);
		break;

	case OBJECT_TYPE_CONNECTOR:
		offset = le16_to_cpu(bp->object_info_tbl.v1_1->usConnectorObjectTableOffset);
		break;

	case OBJECT_TYPE_ROUTER:
		offset = le16_to_cpu(bp->object_info_tbl.v1_1->usRouterObjectTableOffset);
		break;

	case OBJECT_TYPE_GENERIC:
		if (bp->object_info_tbl.revision.minor < 3)
			return NULL;
		offset = le16_to_cpu(bp->object_info_tbl.v1_3->usMiscObjectTableOffset);
		break;

	default:
		return NULL;
	}

	offset += bp->object_info_tbl_offset;

	tbl = ((ATOM_OBJECT_TABLE *) bios_get_image(&bp->base, offset,
				struct_size(tbl, asObjects, 1)));
	if (!tbl)
		return NULL;

	for (i = 0; i < tbl->ucNumberOfObjects; i++)
		if (dal_graphics_object_id_is_equal(id,
				object_id_from_bios_object_id(
						le16_to_cpu(tbl->asObjects[i].usObjectID))))
			return &tbl->asObjects[i];

	return NULL;
}

static uint32_t get_src_obj_list(struct bios_parser *bp, ATOM_OBJECT *object,
	uint16_t **id_list)
{
	uint32_t offset;
	uint8_t *number;

	if (!object) {
		BREAK_TO_DEBUGGER(); /* Invalid object id */
		return 0;
	}

	offset = le16_to_cpu(object->usSrcDstTableOffset)
					+ bp->object_info_tbl_offset;

	number = GET_IMAGE(uint8_t, offset);
	if (!number)
		return 0;

	offset += sizeof(uint8_t);
	*id_list = (uint16_t *)bios_get_image(&bp->base, offset, *number * sizeof(uint16_t));

	if (!*id_list)
		return 0;

	return *number;
}

static struct device_id device_type_from_device_id(uint16_t device_id)
{

	struct device_id result_device_id = {0};

	switch (device_id) {
	case ATOM_DEVICE_LCD1_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_LCD;
		result_device_id.enum_id = 1;
		break;

	case ATOM_DEVICE_LCD2_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_LCD;
		result_device_id.enum_id = 2;
		break;

	case ATOM_DEVICE_CRT1_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_CRT;
		result_device_id.enum_id = 1;
		break;

	case ATOM_DEVICE_CRT2_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_CRT;
		result_device_id.enum_id = 2;
		break;

	case ATOM_DEVICE_DFP1_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 1;
		break;

	case ATOM_DEVICE_DFP2_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 2;
		break;

	case ATOM_DEVICE_DFP3_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 3;
		break;

	case ATOM_DEVICE_DFP4_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 4;
		break;

	case ATOM_DEVICE_DFP5_SUPPORT:
		result_device_id.device_type = DEVICE_TYPE_DFP;
		result_device_id.enum_id = 5;
		break;

	case ATOM_DEVICE_DFP6_SUPPORT:
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

static void get_atom_data_table_revision(
	ATOM_COMMON_TABLE_HEADER *atom_data_tbl,
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
			(uint32_t) GET_DATA_TABLE_MAJOR_REVISION(atom_data_tbl);
	tbl_revision->minor =
			(uint32_t) GET_DATA_TABLE_MINOR_REVISION(atom_data_tbl);
}

static uint32_t signal_to_ss_id(enum as_signal_type signal)
{
	uint32_t clk_id_ss = 0;

	switch (signal) {
	case AS_SIGNAL_TYPE_DVI:
		clk_id_ss = ASIC_INTERNAL_SS_ON_TMDS;
		break;
	case AS_SIGNAL_TYPE_HDMI:
		clk_id_ss = ASIC_INTERNAL_SS_ON_HDMI;
		break;
	case AS_SIGNAL_TYPE_LVDS:
		clk_id_ss = ASIC_INTERNAL_SS_ON_LVDS;
		break;
	case AS_SIGNAL_TYPE_DISPLAY_PORT:
		clk_id_ss = ASIC_INTERNAL_SS_ON_DP;
		break;
	case AS_SIGNAL_TYPE_GPU_PLL:
		clk_id_ss = ASIC_INTERNAL_GPUPLL_SS;
		break;
	default:
		break;
	}
	return clk_id_ss;
}

static uint32_t get_support_mask_for_device_id(struct device_id device_id)
{
	enum dal_device_type device_type = device_id.device_type;
	uint32_t enum_id = device_id.enum_id;

	switch (device_type) {
	case DEVICE_TYPE_LCD:
		switch (enum_id) {
		case 1:
			return ATOM_DEVICE_LCD1_SUPPORT;
		case 2:
			return ATOM_DEVICE_LCD2_SUPPORT;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_CRT:
		switch (enum_id) {
		case 1:
			return ATOM_DEVICE_CRT1_SUPPORT;
		case 2:
			return ATOM_DEVICE_CRT2_SUPPORT;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_DFP:
		switch (enum_id) {
		case 1:
			return ATOM_DEVICE_DFP1_SUPPORT;
		case 2:
			return ATOM_DEVICE_DFP2_SUPPORT;
		case 3:
			return ATOM_DEVICE_DFP3_SUPPORT;
		case 4:
			return ATOM_DEVICE_DFP4_SUPPORT;
		case 5:
			return ATOM_DEVICE_DFP5_SUPPORT;
		case 6:
			return ATOM_DEVICE_DFP6_SUPPORT;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_CV:
		switch (enum_id) {
		case 1:
			return ATOM_DEVICE_CV_SUPPORT;
		default:
			break;
		}
		break;
	case DEVICE_TYPE_TV:
		switch (enum_id) {
		case 1:
			return ATOM_DEVICE_TV1_SUPPORT;
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

/**
 * bios_parser_set_scratch_critical_state - update critical state
 *                                          bit in VBIOS scratch register
 * @dcb:    pointer to the DC BIOS
 * @state:  set or reset state
 */
static void bios_parser_set_scratch_critical_state(
	struct dc_bios *dcb,
	bool state)
{
	bios_set_scratch_critical_state(dcb, state);
}

/*
 * get_integrated_info_v8
 *
 * @brief
 * Get V8 integrated BIOS information
 *
 * @param
 * bios_parser *bp - [in]BIOS parser handler to get master data table
 * integrated_info *info - [out] store and output integrated info
 *
 * return:
 * enum bp_result - BP_RESULT_OK if information is available,
 *                  BP_RESULT_BADBIOSTABLE otherwise.
 */
static enum bp_result get_integrated_info_v8(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	ATOM_INTEGRATED_SYSTEM_INFO_V1_8 *info_v8;
	uint32_t i;

	info_v8 = GET_IMAGE(ATOM_INTEGRATED_SYSTEM_INFO_V1_8,
			bp->master_data_tbl->ListOfDataTables.IntegratedSystemInfo);

	if (info_v8 == NULL)
		return BP_RESULT_BADBIOSTABLE;
	info->boot_up_engine_clock = le32_to_cpu(info_v8->ulBootUpEngineClock) * 10;
	info->dentist_vco_freq = le32_to_cpu(info_v8->ulDentistVCOFreq) * 10;
	info->boot_up_uma_clock = le32_to_cpu(info_v8->ulBootUpUMAClock) * 10;

	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->disp_clk_voltage[i].max_supported_clk =
			le32_to_cpu(info_v8->sDISPCLK_Voltage[i].
				    ulMaximumSupportedCLK) * 10;
		info->disp_clk_voltage[i].voltage_index =
			le32_to_cpu(info_v8->sDISPCLK_Voltage[i].ulVoltageIndex);
	}

	info->boot_up_req_display_vector =
		le32_to_cpu(info_v8->ulBootUpReqDisplayVector);
	info->gpu_cap_info =
		le32_to_cpu(info_v8->ulGPUCapInfo);

	/*
	 * system_config: Bit[0] = 0 : PCIE power gating disabled
	 *                       = 1 : PCIE power gating enabled
	 *                Bit[1] = 0 : DDR-PLL shut down disabled
	 *                       = 1 : DDR-PLL shut down enabled
	 *                Bit[2] = 0 : DDR-PLL power down disabled
	 *                       = 1 : DDR-PLL power down enabled
	 */
	info->system_config = le32_to_cpu(info_v8->ulSystemConfig);
	info->cpu_cap_info = le32_to_cpu(info_v8->ulCPUCapInfo);
	info->boot_up_nb_voltage =
		le16_to_cpu(info_v8->usBootUpNBVoltage);
	info->ext_disp_conn_info_offset =
		le16_to_cpu(info_v8->usExtDispConnInfoOffset);
	info->memory_type = info_v8->ucMemoryType;
	info->ma_channel_number = info_v8->ucUMAChannelNumber;
	info->gmc_restore_reset_time =
		le32_to_cpu(info_v8->ulGMCRestoreResetTime);

	info->minimum_n_clk =
		le32_to_cpu(info_v8->ulNbpStateNClkFreq[0]);
	for (i = 1; i < 4; ++i)
		info->minimum_n_clk =
			info->minimum_n_clk < le32_to_cpu(info_v8->ulNbpStateNClkFreq[i]) ?
			info->minimum_n_clk : le32_to_cpu(info_v8->ulNbpStateNClkFreq[i]);

	info->idle_n_clk = le32_to_cpu(info_v8->ulIdleNClk);
	info->ddr_dll_power_up_time =
		le32_to_cpu(info_v8->ulDDR_DLL_PowerUpTime);
	info->ddr_pll_power_up_time =
		le32_to_cpu(info_v8->ulDDR_PLL_PowerUpTime);
	info->pcie_clk_ss_type = le16_to_cpu(info_v8->usPCIEClkSSType);
	info->lvds_ss_percentage =
		le16_to_cpu(info_v8->usLvdsSSPercentage);
	info->lvds_sspread_rate_in_10hz =
		le16_to_cpu(info_v8->usLvdsSSpreadRateIn10Hz);
	info->hdmi_ss_percentage =
		le16_to_cpu(info_v8->usHDMISSPercentage);
	info->hdmi_sspread_rate_in_10hz =
		le16_to_cpu(info_v8->usHDMISSpreadRateIn10Hz);
	info->dvi_ss_percentage =
		le16_to_cpu(info_v8->usDVISSPercentage);
	info->dvi_sspread_rate_in_10_hz =
		le16_to_cpu(info_v8->usDVISSpreadRateIn10Hz);

	info->max_lvds_pclk_freq_in_single_link =
		le16_to_cpu(info_v8->usMaxLVDSPclkFreqInSingleLink);
	info->lvds_misc = info_v8->ucLvdsMisc;
	info->lvds_pwr_on_seq_dig_on_to_de_in_4ms =
		info_v8->ucLVDSPwrOnSeqDIGONtoDE_in4Ms;
	info->lvds_pwr_on_seq_de_to_vary_bl_in_4ms =
		info_v8->ucLVDSPwrOnSeqDEtoVARY_BL_in4Ms;
	info->lvds_pwr_on_seq_vary_bl_to_blon_in_4ms =
		info_v8->ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms;
	info->lvds_pwr_off_seq_vary_bl_to_de_in4ms =
		info_v8->ucLVDSPwrOffSeqVARY_BLtoDE_in4Ms;
	info->lvds_pwr_off_seq_de_to_dig_on_in4ms =
		info_v8->ucLVDSPwrOffSeqDEtoDIGON_in4Ms;
	info->lvds_pwr_off_seq_blon_to_vary_bl_in_4ms =
		info_v8->ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms;
	info->lvds_off_to_on_delay_in_4ms =
		info_v8->ucLVDSOffToOnDelay_in4Ms;
	info->lvds_bit_depth_control_val =
		le32_to_cpu(info_v8->ulLCDBitDepthControlVal);

	for (i = 0; i < NUMBER_OF_AVAILABLE_SCLK; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->avail_s_clk[i].supported_s_clk =
			le32_to_cpu(info_v8->sAvail_SCLK[i].ulSupportedSCLK) * 10;
		info->avail_s_clk[i].voltage_index =
			le16_to_cpu(info_v8->sAvail_SCLK[i].usVoltageIndex);
		info->avail_s_clk[i].voltage_id =
			le16_to_cpu(info_v8->sAvail_SCLK[i].usVoltageID);
	}

	for (i = 0; i < NUMBER_OF_UCHAR_FOR_GUID; ++i) {
		info->ext_disp_conn_info.gu_id[i] =
			info_v8->sExtDispConnInfo.ucGuid[i];
	}

	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; ++i) {
		info->ext_disp_conn_info.path[i].device_connector_id =
			object_id_from_bios_object_id(
				le16_to_cpu(info_v8->sExtDispConnInfo.sPath[i].usDeviceConnector));

		info->ext_disp_conn_info.path[i].ext_encoder_obj_id =
			object_id_from_bios_object_id(
				le16_to_cpu(info_v8->sExtDispConnInfo.sPath[i].usExtEncoderObjId));

		info->ext_disp_conn_info.path[i].device_tag =
			le16_to_cpu(info_v8->sExtDispConnInfo.sPath[i].usDeviceTag);
		info->ext_disp_conn_info.path[i].device_acpi_enum =
			le16_to_cpu(info_v8->sExtDispConnInfo.sPath[i].usDeviceACPIEnum);
		info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index =
			info_v8->sExtDispConnInfo.sPath[i].ucExtAUXDDCLutIndex;
		info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index =
			info_v8->sExtDispConnInfo.sPath[i].ucExtHPDPINLutIndex;
		info->ext_disp_conn_info.path[i].channel_mapping.raw =
			info_v8->sExtDispConnInfo.sPath[i].ucChannelMapping;
	}
	info->ext_disp_conn_info.checksum =
		info_v8->sExtDispConnInfo.ucChecksum;

	return BP_RESULT_OK;
}

/*
 * get_integrated_info_v8
 *
 * @brief
 * Get V8 integrated BIOS information
 *
 * @param
 * bios_parser *bp - [in]BIOS parser handler to get master data table
 * integrated_info *info - [out] store and output integrated info
 *
 * return:
 * enum bp_result - BP_RESULT_OK if information is available,
 *                  BP_RESULT_BADBIOSTABLE otherwise.
 */
static enum bp_result get_integrated_info_v9(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	ATOM_INTEGRATED_SYSTEM_INFO_V1_9 *info_v9;
	uint32_t i;

	info_v9 = GET_IMAGE(ATOM_INTEGRATED_SYSTEM_INFO_V1_9,
			bp->master_data_tbl->ListOfDataTables.IntegratedSystemInfo);

	if (!info_v9)
		return BP_RESULT_BADBIOSTABLE;

	info->boot_up_engine_clock = le32_to_cpu(info_v9->ulBootUpEngineClock) * 10;
	info->dentist_vco_freq = le32_to_cpu(info_v9->ulDentistVCOFreq) * 10;
	info->boot_up_uma_clock = le32_to_cpu(info_v9->ulBootUpUMAClock) * 10;

	for (i = 0; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->disp_clk_voltage[i].max_supported_clk =
			le32_to_cpu(info_v9->sDISPCLK_Voltage[i].ulMaximumSupportedCLK) * 10;
		info->disp_clk_voltage[i].voltage_index =
			le32_to_cpu(info_v9->sDISPCLK_Voltage[i].ulVoltageIndex);
	}

	info->boot_up_req_display_vector =
		le32_to_cpu(info_v9->ulBootUpReqDisplayVector);
	info->gpu_cap_info = le32_to_cpu(info_v9->ulGPUCapInfo);

	/*
	 * system_config: Bit[0] = 0 : PCIE power gating disabled
	 *                       = 1 : PCIE power gating enabled
	 *                Bit[1] = 0 : DDR-PLL shut down disabled
	 *                       = 1 : DDR-PLL shut down enabled
	 *                Bit[2] = 0 : DDR-PLL power down disabled
	 *                       = 1 : DDR-PLL power down enabled
	 */
	info->system_config = le32_to_cpu(info_v9->ulSystemConfig);
	info->cpu_cap_info = le32_to_cpu(info_v9->ulCPUCapInfo);
	info->boot_up_nb_voltage = le16_to_cpu(info_v9->usBootUpNBVoltage);
	info->ext_disp_conn_info_offset = le16_to_cpu(info_v9->usExtDispConnInfoOffset);
	info->memory_type = info_v9->ucMemoryType;
	info->ma_channel_number = info_v9->ucUMAChannelNumber;
	info->gmc_restore_reset_time = le32_to_cpu(info_v9->ulGMCRestoreResetTime);

	info->minimum_n_clk = le32_to_cpu(info_v9->ulNbpStateNClkFreq[0]);
	for (i = 1; i < 4; ++i)
		info->minimum_n_clk =
			info->minimum_n_clk < le32_to_cpu(info_v9->ulNbpStateNClkFreq[i]) ?
			info->minimum_n_clk : le32_to_cpu(info_v9->ulNbpStateNClkFreq[i]);

	info->idle_n_clk = le32_to_cpu(info_v9->ulIdleNClk);
	info->ddr_dll_power_up_time = le32_to_cpu(info_v9->ulDDR_DLL_PowerUpTime);
	info->ddr_pll_power_up_time = le32_to_cpu(info_v9->ulDDR_PLL_PowerUpTime);
	info->pcie_clk_ss_type = le16_to_cpu(info_v9->usPCIEClkSSType);
	info->lvds_ss_percentage = le16_to_cpu(info_v9->usLvdsSSPercentage);
	info->lvds_sspread_rate_in_10hz = le16_to_cpu(info_v9->usLvdsSSpreadRateIn10Hz);
	info->hdmi_ss_percentage = le16_to_cpu(info_v9->usHDMISSPercentage);
	info->hdmi_sspread_rate_in_10hz = le16_to_cpu(info_v9->usHDMISSpreadRateIn10Hz);
	info->dvi_ss_percentage = le16_to_cpu(info_v9->usDVISSPercentage);
	info->dvi_sspread_rate_in_10_hz = le16_to_cpu(info_v9->usDVISSpreadRateIn10Hz);

	info->max_lvds_pclk_freq_in_single_link =
		le16_to_cpu(info_v9->usMaxLVDSPclkFreqInSingleLink);
	info->lvds_misc = info_v9->ucLvdsMisc;
	info->lvds_pwr_on_seq_dig_on_to_de_in_4ms =
		info_v9->ucLVDSPwrOnSeqDIGONtoDE_in4Ms;
	info->lvds_pwr_on_seq_de_to_vary_bl_in_4ms =
		info_v9->ucLVDSPwrOnSeqDEtoVARY_BL_in4Ms;
	info->lvds_pwr_on_seq_vary_bl_to_blon_in_4ms =
		info_v9->ucLVDSPwrOnSeqVARY_BLtoBLON_in4Ms;
	info->lvds_pwr_off_seq_vary_bl_to_de_in4ms =
		info_v9->ucLVDSPwrOffSeqVARY_BLtoDE_in4Ms;
	info->lvds_pwr_off_seq_de_to_dig_on_in4ms =
		info_v9->ucLVDSPwrOffSeqDEtoDIGON_in4Ms;
	info->lvds_pwr_off_seq_blon_to_vary_bl_in_4ms =
		info_v9->ucLVDSPwrOffSeqBLONtoVARY_BL_in4Ms;
	info->lvds_off_to_on_delay_in_4ms =
		info_v9->ucLVDSOffToOnDelay_in4Ms;
	info->lvds_bit_depth_control_val =
		le32_to_cpu(info_v9->ulLCDBitDepthControlVal);

	for (i = 0; i < NUMBER_OF_AVAILABLE_SCLK; ++i) {
		/* Convert [10KHz] into [KHz] */
		info->avail_s_clk[i].supported_s_clk =
			le32_to_cpu(info_v9->sAvail_SCLK[i].ulSupportedSCLK) * 10;
		info->avail_s_clk[i].voltage_index =
			le16_to_cpu(info_v9->sAvail_SCLK[i].usVoltageIndex);
		info->avail_s_clk[i].voltage_id =
			le16_to_cpu(info_v9->sAvail_SCLK[i].usVoltageID);
	}

	for (i = 0; i < NUMBER_OF_UCHAR_FOR_GUID; ++i) {
		info->ext_disp_conn_info.gu_id[i] =
			info_v9->sExtDispConnInfo.ucGuid[i];
	}

	for (i = 0; i < MAX_NUMBER_OF_EXT_DISPLAY_PATH; ++i) {
		info->ext_disp_conn_info.path[i].device_connector_id =
			object_id_from_bios_object_id(
				le16_to_cpu(info_v9->sExtDispConnInfo.sPath[i].usDeviceConnector));

		info->ext_disp_conn_info.path[i].ext_encoder_obj_id =
			object_id_from_bios_object_id(
				le16_to_cpu(info_v9->sExtDispConnInfo.sPath[i].usExtEncoderObjId));

		info->ext_disp_conn_info.path[i].device_tag =
			le16_to_cpu(info_v9->sExtDispConnInfo.sPath[i].usDeviceTag);
		info->ext_disp_conn_info.path[i].device_acpi_enum =
			le16_to_cpu(info_v9->sExtDispConnInfo.sPath[i].usDeviceACPIEnum);
		info->ext_disp_conn_info.path[i].ext_aux_ddc_lut_index =
			info_v9->sExtDispConnInfo.sPath[i].ucExtAUXDDCLutIndex;
		info->ext_disp_conn_info.path[i].ext_hpd_pin_lut_index =
			info_v9->sExtDispConnInfo.sPath[i].ucExtHPDPINLutIndex;
		info->ext_disp_conn_info.path[i].channel_mapping.raw =
			info_v9->sExtDispConnInfo.sPath[i].ucChannelMapping;
	}
	info->ext_disp_conn_info.checksum =
		info_v9->sExtDispConnInfo.ucChecksum;

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
 * return:
 * enum bp_result - BP_RESULT_OK if information is available,
 *                  BP_RESULT_BADBIOSTABLE otherwise.
 */
static enum bp_result construct_integrated_info(
	struct bios_parser *bp,
	struct integrated_info *info)
{
	enum bp_result result = BP_RESULT_BADBIOSTABLE;

	ATOM_COMMON_TABLE_HEADER *header;
	struct atom_data_revision revision;

	if (bp->master_data_tbl->ListOfDataTables.IntegratedSystemInfo) {
		header = GET_IMAGE(ATOM_COMMON_TABLE_HEADER,
				bp->master_data_tbl->ListOfDataTables.IntegratedSystemInfo);

		get_atom_data_table_revision(header, &revision);

		/* Don't need to check major revision as they are all 1 */
		switch (revision.minor) {
		case 8:
			result = get_integrated_info_v8(bp, info);
			break;
		case 9:
			result = get_integrated_info_v9(bp, info);
			break;
		default:
			return result;

		}
	}

	/* Sort voltage table from low to high*/
	if (result == BP_RESULT_OK) {
		uint32_t i;
		uint32_t j;

		for (i = 1; i < NUMBER_OF_DISP_CLK_VOLTAGE; ++i) {
			for (j = i; j > 0; --j) {
				if (
						info->disp_clk_voltage[j].max_supported_clk <
						info->disp_clk_voltage[j-1].max_supported_clk) {
					/* swap j and j - 1*/
					swap(info->disp_clk_voltage[j - 1],
					     info->disp_clk_voltage[j]);
				}
			}
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

static enum bp_result update_slot_layout_info(struct dc_bios *dcb,
					      unsigned int i,
					      struct slot_layout_info *slot_layout_info,
					      unsigned int record_offset)
{
	unsigned int j;
	struct bios_parser *bp;
	ATOM_BRACKET_LAYOUT_RECORD *record;
	ATOM_COMMON_RECORD_HEADER *record_header;
	enum bp_result result = BP_RESULT_NORECORD;

	bp = BP_FROM_DCB(dcb);
	record = NULL;
	record_header = NULL;

	for (;;) {

		record_header = GET_IMAGE(ATOM_COMMON_RECORD_HEADER, record_offset);
		if (record_header == NULL) {
			result = BP_RESULT_BADBIOSTABLE;
			break;
		}

		/* the end of the list */
		if (record_header->ucRecordType == 0xff ||
			record_header->ucRecordSize == 0)	{
			break;
		}

		if (record_header->ucRecordType ==
			ATOM_BRACKET_LAYOUT_RECORD_TYPE &&
			struct_size(record, asConnInfo, 1)
			<= record_header->ucRecordSize) {
			record = (ATOM_BRACKET_LAYOUT_RECORD *)
				(record_header);
			result = BP_RESULT_OK;
			break;
		}

		record_offset += record_header->ucRecordSize;
	}

	/* return if the record not found */
	if (result != BP_RESULT_OK)
		return result;

	/* get slot sizes */
	slot_layout_info->length = record->ucLength;
	slot_layout_info->width = record->ucWidth;

	/* get info for each connector in the slot */
	slot_layout_info->num_of_connectors = record->ucConnNum;
	for (j = 0; j < slot_layout_info->num_of_connectors; ++j) {
		slot_layout_info->connectors[j].connector_type =
			(enum connector_layout_type)
			(record->asConnInfo[j].ucConnectorType);
		switch (record->asConnInfo[j].ucConnectorType) {
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
			record->asConnInfo[j].ucPosition;
		slot_layout_info->connectors[j].connector_id =
			object_id_from_bios_object_id(
				record->asConnInfo[j].usConnectorObjectId);
	}
	return result;
}


static enum bp_result get_bracket_layout_record(struct dc_bios *dcb,
						unsigned int bracket_layout_id,
						struct slot_layout_info *slot_layout_info)
{
	unsigned int i;
	unsigned int record_offset;
	struct bios_parser *bp;
	enum bp_result result;
	ATOM_OBJECT *object;
	ATOM_OBJECT_TABLE *object_table;
	unsigned int genericTableOffset;

	bp = BP_FROM_DCB(dcb);
	object = NULL;
	if (slot_layout_info == NULL) {
		DC_LOG_DETECTION_EDID_PARSER("Invalid slot_layout_info\n");
		return BP_RESULT_BADINPUT;
	}


	genericTableOffset = bp->object_info_tbl_offset +
		bp->object_info_tbl.v1_3->usMiscObjectTableOffset;
	object_table = ((ATOM_OBJECT_TABLE *) bios_get_image(&bp->base,
				genericTableOffset,
				struct_size(object_table, asObjects, 1)));
	if (!object_table)
		return BP_RESULT_FAILURE;

	result = BP_RESULT_NORECORD;
	for (i = 0; i < object_table->ucNumberOfObjects; ++i) {

		if (bracket_layout_id ==
			object_table->asObjects[i].usObjectID) {

			object = &object_table->asObjects[i];
			record_offset = object->usRecordOffset +
				bp->object_info_tbl_offset;

			result = update_slot_layout_info(dcb, i,
				slot_layout_info, record_offset);
			break;
		}
	}
	return result;
}

static enum bp_result bios_get_board_layout_info(
	struct dc_bios *dcb,
	struct board_layout_info *board_layout_info)
{
	unsigned int i;
	struct bios_parser *bp;
	enum bp_result record_result;

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

	for (i = 0; i < MAX_BOARD_SLOTS; ++i) {
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

/******************************************************************************/

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

	/* bios scratch register communication */
	.is_accelerated_mode = bios_is_accelerated_mode,

	.set_scratch_critical_state = bios_parser_set_scratch_critical_state,

	.is_device_id_supported = bios_parser_is_device_id_supported,

	/* COMMANDS */
	.encoder_control = bios_parser_encoder_control,

	.transmitter_control = bios_parser_transmitter_control,

	.enable_crtc = bios_parser_enable_crtc,

	.adjust_pixel_clock = bios_parser_adjust_pixel_clock,

	.set_pixel_clock = bios_parser_set_pixel_clock,

	.set_dce_clock = bios_parser_set_dce_clock,

	.enable_spread_spectrum_on_ppll = bios_parser_enable_spread_spectrum_on_ppll,

	.program_crtc_timing = bios_parser_program_crtc_timing, /* still use.  should probably retire and program directly */

	.program_display_engine_pll = bios_parser_program_display_engine_pll,

	.enable_disp_power_gating = bios_parser_enable_disp_power_gating,

	/* SW init and patch */

	.bios_parser_destroy = bios_parser_destroy,

	.get_board_layout_info = bios_get_board_layout_info,

	.get_atom_dc_golden_table = NULL
};

static bool bios_parser_construct(
	struct bios_parser *bp,
	struct bp_init_data *init,
	enum dce_version dce_version)
{
	uint16_t *rom_header_offset = NULL;
	ATOM_ROM_HEADER *rom_header = NULL;
	ATOM_OBJECT_HEADER *object_info_tbl;
	struct atom_data_revision tbl_rev = {0};

	if (!init)
		return false;

	if (!init->bios)
		return false;

	bp->base.funcs = &vbios_funcs;
	bp->base.bios = init->bios;
	bp->base.bios_size = bp->base.bios[BIOS_IMAGE_SIZE_OFFSET] * BIOS_IMAGE_SIZE_UNIT;

	bp->base.ctx = init->ctx;
	bp->base.bios_local_image = NULL;

	rom_header_offset =
	GET_IMAGE(uint16_t, OFFSET_TO_POINTER_TO_ATOM_ROM_HEADER);

	if (!rom_header_offset)
		return false;

	rom_header = GET_IMAGE(ATOM_ROM_HEADER, *rom_header_offset);

	if (!rom_header)
		return false;

	get_atom_data_table_revision(&rom_header->sHeader, &tbl_rev);
	if (tbl_rev.major >= 2 && tbl_rev.minor >= 2)
		return false;

	bp->master_data_tbl =
	GET_IMAGE(ATOM_MASTER_DATA_TABLE,
		rom_header->usMasterDataTableOffset);

	if (!bp->master_data_tbl)
		return false;

	bp->object_info_tbl_offset = DATA_TABLES(Object_Header);

	if (!bp->object_info_tbl_offset)
		return false;

	object_info_tbl =
	GET_IMAGE(ATOM_OBJECT_HEADER, bp->object_info_tbl_offset);

	if (!object_info_tbl)
		return false;

	get_atom_data_table_revision(&object_info_tbl->sHeader,
		&bp->object_info_tbl.revision);

	if (bp->object_info_tbl.revision.major == 1
		&& bp->object_info_tbl.revision.minor >= 3) {
		ATOM_OBJECT_HEADER_V3 *tbl_v3;

		tbl_v3 = GET_IMAGE(ATOM_OBJECT_HEADER_V3,
			bp->object_info_tbl_offset);
		if (!tbl_v3)
			return false;

		bp->object_info_tbl.v1_3 = tbl_v3;
	} else if (bp->object_info_tbl.revision.major == 1
		&& bp->object_info_tbl.revision.minor >= 1)
		bp->object_info_tbl.v1_1 = object_info_tbl;
	else
		return false;

	dal_bios_parser_init_cmd_tbl(bp);
	dal_bios_parser_init_cmd_tbl_helper(&bp->cmd_helper, dce_version);

	bp->base.integrated_info = bios_parser_create_integrated_info(&bp->base);
	bp->base.fw_info_valid = bios_parser_get_firmware_info(&bp->base, &bp->base.fw_info) == BP_RESULT_OK;

	return true;
}

/******************************************************************************/
