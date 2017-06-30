/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_i2c.h"

#include "atom.h"
#include "atom-bits.h"
#include "atombios_encoders.h"
#include "bif/bif_4_1_d.h"

static void amdgpu_atombios_lookup_i2c_gpio_quirks(struct amdgpu_device *adev,
					  ATOM_GPIO_I2C_ASSIGMENT *gpio,
					  u8 index)
{

}

static struct amdgpu_i2c_bus_rec amdgpu_atombios_get_bus_rec_for_i2c_gpio(ATOM_GPIO_I2C_ASSIGMENT *gpio)
{
	struct amdgpu_i2c_bus_rec i2c;

	memset(&i2c, 0, sizeof(struct amdgpu_i2c_bus_rec));

	i2c.mask_clk_reg = le16_to_cpu(gpio->usClkMaskRegisterIndex);
	i2c.mask_data_reg = le16_to_cpu(gpio->usDataMaskRegisterIndex);
	i2c.en_clk_reg = le16_to_cpu(gpio->usClkEnRegisterIndex);
	i2c.en_data_reg = le16_to_cpu(gpio->usDataEnRegisterIndex);
	i2c.y_clk_reg = le16_to_cpu(gpio->usClkY_RegisterIndex);
	i2c.y_data_reg = le16_to_cpu(gpio->usDataY_RegisterIndex);
	i2c.a_clk_reg = le16_to_cpu(gpio->usClkA_RegisterIndex);
	i2c.a_data_reg = le16_to_cpu(gpio->usDataA_RegisterIndex);
	i2c.mask_clk_mask = (1 << gpio->ucClkMaskShift);
	i2c.mask_data_mask = (1 << gpio->ucDataMaskShift);
	i2c.en_clk_mask = (1 << gpio->ucClkEnShift);
	i2c.en_data_mask = (1 << gpio->ucDataEnShift);
	i2c.y_clk_mask = (1 << gpio->ucClkY_Shift);
	i2c.y_data_mask = (1 << gpio->ucDataY_Shift);
	i2c.a_clk_mask = (1 << gpio->ucClkA_Shift);
	i2c.a_data_mask = (1 << gpio->ucDataA_Shift);

	if (gpio->sucI2cId.sbfAccess.bfHW_Capable)
		i2c.hw_capable = true;
	else
		i2c.hw_capable = false;

	if (gpio->sucI2cId.ucAccess == 0xa0)
		i2c.mm_i2c = true;
	else
		i2c.mm_i2c = false;

	i2c.i2c_id = gpio->sucI2cId.ucAccess;

	if (i2c.mask_clk_reg)
		i2c.valid = true;
	else
		i2c.valid = false;

	return i2c;
}

struct amdgpu_i2c_bus_rec amdgpu_atombios_lookup_i2c_gpio(struct amdgpu_device *adev,
							  uint8_t id)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	ATOM_GPIO_I2C_ASSIGMENT *gpio;
	struct amdgpu_i2c_bus_rec i2c;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	struct _ATOM_GPIO_I2C_INFO *i2c_info;
	uint16_t data_offset, size;
	int i, num_indices;

	memset(&i2c, 0, sizeof(struct amdgpu_i2c_bus_rec));
	i2c.valid = false;

	if (amdgpu_atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_I2C_ASSIGMENT);

		gpio = &i2c_info->asGPIO_Info[0];
		for (i = 0; i < num_indices; i++) {

			amdgpu_atombios_lookup_i2c_gpio_quirks(adev, gpio, i);

			if (gpio->sucI2cId.ucAccess == id) {
				i2c = amdgpu_atombios_get_bus_rec_for_i2c_gpio(gpio);
				break;
			}
			gpio = (ATOM_GPIO_I2C_ASSIGMENT *)
				((u8 *)gpio + sizeof(ATOM_GPIO_I2C_ASSIGMENT));
		}
	}

	return i2c;
}

void amdgpu_atombios_i2c_init(struct amdgpu_device *adev)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	ATOM_GPIO_I2C_ASSIGMENT *gpio;
	struct amdgpu_i2c_bus_rec i2c;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	struct _ATOM_GPIO_I2C_INFO *i2c_info;
	uint16_t data_offset, size;
	int i, num_indices;
	char stmp[32];

	if (amdgpu_atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_I2C_ASSIGMENT);

		gpio = &i2c_info->asGPIO_Info[0];
		for (i = 0; i < num_indices; i++) {
			amdgpu_atombios_lookup_i2c_gpio_quirks(adev, gpio, i);

			i2c = amdgpu_atombios_get_bus_rec_for_i2c_gpio(gpio);

			if (i2c.valid) {
				sprintf(stmp, "0x%x", i2c.i2c_id);
				adev->i2c_bus[i] = amdgpu_i2c_create(adev->ddev, &i2c, stmp);
			}
			gpio = (ATOM_GPIO_I2C_ASSIGMENT *)
				((u8 *)gpio + sizeof(ATOM_GPIO_I2C_ASSIGMENT));
		}
	}
}

struct amdgpu_gpio_rec
amdgpu_atombios_lookup_gpio(struct amdgpu_device *adev,
			    u8 id)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	struct amdgpu_gpio_rec gpio;
	int index = GetIndexIntoMasterTable(DATA, GPIO_Pin_LUT);
	struct _ATOM_GPIO_PIN_LUT *gpio_info;
	ATOM_GPIO_PIN_ASSIGNMENT *pin;
	u16 data_offset, size;
	int i, num_indices;

	memset(&gpio, 0, sizeof(struct amdgpu_gpio_rec));
	gpio.valid = false;

	if (amdgpu_atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		gpio_info = (struct _ATOM_GPIO_PIN_LUT *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_PIN_ASSIGNMENT);

		pin = gpio_info->asGPIO_Pin;
		for (i = 0; i < num_indices; i++) {
			if (id == pin->ucGPIO_ID) {
				gpio.id = pin->ucGPIO_ID;
				gpio.reg = le16_to_cpu(pin->usGpioPin_AIndex);
				gpio.shift = pin->ucGpioPinBitShift;
				gpio.mask = (1 << pin->ucGpioPinBitShift);
				gpio.valid = true;
				break;
			}
			pin = (ATOM_GPIO_PIN_ASSIGNMENT *)
				((u8 *)pin + sizeof(ATOM_GPIO_PIN_ASSIGNMENT));
		}
	}

	return gpio;
}

static struct amdgpu_hpd
amdgpu_atombios_get_hpd_info_from_gpio(struct amdgpu_device *adev,
				       struct amdgpu_gpio_rec *gpio)
{
	struct amdgpu_hpd hpd;
	u32 reg;

	memset(&hpd, 0, sizeof(struct amdgpu_hpd));

	reg = amdgpu_display_hpd_get_gpio_reg(adev);

	hpd.gpio = *gpio;
	if (gpio->reg == reg) {
		switch(gpio->mask) {
		case (1 << 0):
			hpd.hpd = AMDGPU_HPD_1;
			break;
		case (1 << 8):
			hpd.hpd = AMDGPU_HPD_2;
			break;
		case (1 << 16):
			hpd.hpd = AMDGPU_HPD_3;
			break;
		case (1 << 24):
			hpd.hpd = AMDGPU_HPD_4;
			break;
		case (1 << 26):
			hpd.hpd = AMDGPU_HPD_5;
			break;
		case (1 << 28):
			hpd.hpd = AMDGPU_HPD_6;
			break;
		default:
			hpd.hpd = AMDGPU_HPD_NONE;
			break;
		}
	} else
		hpd.hpd = AMDGPU_HPD_NONE;
	return hpd;
}

static const int object_connector_convert[] = {
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_DVII,
	DRM_MODE_CONNECTOR_DVII,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_VGA,
	DRM_MODE_CONNECTOR_Composite,
	DRM_MODE_CONNECTOR_SVIDEO,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_9PinDIN,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_HDMIA,
	DRM_MODE_CONNECTOR_HDMIB,
	DRM_MODE_CONNECTOR_LVDS,
	DRM_MODE_CONNECTOR_9PinDIN,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_DisplayPort,
	DRM_MODE_CONNECTOR_eDP,
	DRM_MODE_CONNECTOR_Unknown
};

bool amdgpu_atombios_has_dce_engine_info(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 size, data_offset;
	u8 frev, crev;
	ATOM_DISPLAY_OBJECT_PATH_TABLE *path_obj;
	ATOM_OBJECT_HEADER *obj_header;

	if (!amdgpu_atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset))
		return false;

	if (crev < 2)
		return false;

	obj_header = (ATOM_OBJECT_HEADER *) (ctx->bios + data_offset);
	path_obj = (ATOM_DISPLAY_OBJECT_PATH_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usDisplayPathTableOffset));

	if (path_obj->ucNumOfDispPath)
		return true;
	else
		return false;
}

bool amdgpu_atombios_get_connector_info_from_object_table(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 size, data_offset;
	u8 frev, crev;
	ATOM_CONNECTOR_OBJECT_TABLE *con_obj;
	ATOM_ENCODER_OBJECT_TABLE *enc_obj;
	ATOM_OBJECT_TABLE *router_obj;
	ATOM_DISPLAY_OBJECT_PATH_TABLE *path_obj;
	ATOM_OBJECT_HEADER *obj_header;
	int i, j, k, path_size, device_support;
	int connector_type;
	u16 conn_id, connector_object_id;
	struct amdgpu_i2c_bus_rec ddc_bus;
	struct amdgpu_router router;
	struct amdgpu_gpio_rec gpio;
	struct amdgpu_hpd hpd;

	if (!amdgpu_atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset))
		return false;

	if (crev < 2)
		return false;

	obj_header = (ATOM_OBJECT_HEADER *) (ctx->bios + data_offset);
	path_obj = (ATOM_DISPLAY_OBJECT_PATH_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usDisplayPathTableOffset));
	con_obj = (ATOM_CONNECTOR_OBJECT_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usConnectorObjectTableOffset));
	enc_obj = (ATOM_ENCODER_OBJECT_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usEncoderObjectTableOffset));
	router_obj = (ATOM_OBJECT_TABLE *)
		(ctx->bios + data_offset +
		 le16_to_cpu(obj_header->usRouterObjectTableOffset));
	device_support = le16_to_cpu(obj_header->usDeviceSupport);

	path_size = 0;
	for (i = 0; i < path_obj->ucNumOfDispPath; i++) {
		uint8_t *addr = (uint8_t *) path_obj->asDispPath;
		ATOM_DISPLAY_OBJECT_PATH *path;
		addr += path_size;
		path = (ATOM_DISPLAY_OBJECT_PATH *) addr;
		path_size += le16_to_cpu(path->usSize);

		if (device_support & le16_to_cpu(path->usDeviceTag)) {
			uint8_t con_obj_id, con_obj_num, con_obj_type;

			con_obj_id =
			    (le16_to_cpu(path->usConnObjectId) & OBJECT_ID_MASK)
			    >> OBJECT_ID_SHIFT;
			con_obj_num =
			    (le16_to_cpu(path->usConnObjectId) & ENUM_ID_MASK)
			    >> ENUM_ID_SHIFT;
			con_obj_type =
			    (le16_to_cpu(path->usConnObjectId) &
			     OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT;

			/* Skip TV/CV support */
			if ((le16_to_cpu(path->usDeviceTag) ==
			     ATOM_DEVICE_TV1_SUPPORT) ||
			    (le16_to_cpu(path->usDeviceTag) ==
			     ATOM_DEVICE_CV_SUPPORT))
				continue;

			if (con_obj_id >= ARRAY_SIZE(object_connector_convert)) {
				DRM_ERROR("invalid con_obj_id %d for device tag 0x%04x\n",
					  con_obj_id, le16_to_cpu(path->usDeviceTag));
				continue;
			}

			connector_type =
				object_connector_convert[con_obj_id];
			connector_object_id = con_obj_id;

			if (connector_type == DRM_MODE_CONNECTOR_Unknown)
				continue;

			router.ddc_valid = false;
			router.cd_valid = false;
			for (j = 0; j < ((le16_to_cpu(path->usSize) - 8) / 2); j++) {
				uint8_t grph_obj_id, grph_obj_num, grph_obj_type;

				grph_obj_id =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
				grph_obj_num =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     ENUM_ID_MASK) >> ENUM_ID_SHIFT;
				grph_obj_type =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT;

				if (grph_obj_type == GRAPH_OBJECT_TYPE_ENCODER) {
					for (k = 0; k < enc_obj->ucNumberOfObjects; k++) {
						u16 encoder_obj = le16_to_cpu(enc_obj->asObjects[k].usObjectID);
						if (le16_to_cpu(path->usGraphicObjIds[j]) == encoder_obj) {
							ATOM_COMMON_RECORD_HEADER *record = (ATOM_COMMON_RECORD_HEADER *)
								(ctx->bios + data_offset +
								 le16_to_cpu(enc_obj->asObjects[k].usRecordOffset));
							ATOM_ENCODER_CAP_RECORD *cap_record;
							u16 caps = 0;

							while (record->ucRecordSize > 0 &&
							       record->ucRecordType > 0 &&
							       record->ucRecordType <= ATOM_MAX_OBJECT_RECORD_NUMBER) {
								switch (record->ucRecordType) {
								case ATOM_ENCODER_CAP_RECORD_TYPE:
									cap_record =(ATOM_ENCODER_CAP_RECORD *)
										record;
									caps = le16_to_cpu(cap_record->usEncoderCap);
									break;
								}
								record = (ATOM_COMMON_RECORD_HEADER *)
									((char *)record + record->ucRecordSize);
							}
							amdgpu_display_add_encoder(adev, encoder_obj,
										    le16_to_cpu(path->usDeviceTag),
										    caps);
						}
					}
				} else if (grph_obj_type == GRAPH_OBJECT_TYPE_ROUTER) {
					for (k = 0; k < router_obj->ucNumberOfObjects; k++) {
						u16 router_obj_id = le16_to_cpu(router_obj->asObjects[k].usObjectID);
						if (le16_to_cpu(path->usGraphicObjIds[j]) == router_obj_id) {
							ATOM_COMMON_RECORD_HEADER *record = (ATOM_COMMON_RECORD_HEADER *)
								(ctx->bios + data_offset +
								 le16_to_cpu(router_obj->asObjects[k].usRecordOffset));
							ATOM_I2C_RECORD *i2c_record;
							ATOM_I2C_ID_CONFIG_ACCESS *i2c_config;
							ATOM_ROUTER_DDC_PATH_SELECT_RECORD *ddc_path;
							ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD *cd_path;
							ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *router_src_dst_table =
								(ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *)
								(ctx->bios + data_offset +
								 le16_to_cpu(router_obj->asObjects[k].usSrcDstTableOffset));
							u8 *num_dst_objs = (u8 *)
								((u8 *)router_src_dst_table + 1 +
								 (router_src_dst_table->ucNumberOfSrc * 2));
							u16 *dst_objs = (u16 *)(num_dst_objs + 1);
							int enum_id;

							router.router_id = router_obj_id;
							for (enum_id = 0; enum_id < (*num_dst_objs); enum_id++) {
								if (le16_to_cpu(path->usConnObjectId) ==
								    le16_to_cpu(dst_objs[enum_id]))
									break;
							}

							while (record->ucRecordSize > 0 &&
							       record->ucRecordType > 0 &&
							       record->ucRecordType <= ATOM_MAX_OBJECT_RECORD_NUMBER) {
								switch (record->ucRecordType) {
								case ATOM_I2C_RECORD_TYPE:
									i2c_record =
										(ATOM_I2C_RECORD *)
										record;
									i2c_config =
										(ATOM_I2C_ID_CONFIG_ACCESS *)
										&i2c_record->sucI2cId;
									router.i2c_info =
										amdgpu_atombios_lookup_i2c_gpio(adev,
												       i2c_config->
												       ucAccess);
									router.i2c_addr = i2c_record->ucI2CAddr >> 1;
									break;
								case ATOM_ROUTER_DDC_PATH_SELECT_RECORD_TYPE:
									ddc_path = (ATOM_ROUTER_DDC_PATH_SELECT_RECORD *)
										record;
									router.ddc_valid = true;
									router.ddc_mux_type = ddc_path->ucMuxType;
									router.ddc_mux_control_pin = ddc_path->ucMuxControlPin;
									router.ddc_mux_state = ddc_path->ucMuxState[enum_id];
									break;
								case ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD_TYPE:
									cd_path = (ATOM_ROUTER_DATA_CLOCK_PATH_SELECT_RECORD *)
										record;
									router.cd_valid = true;
									router.cd_mux_type = cd_path->ucMuxType;
									router.cd_mux_control_pin = cd_path->ucMuxControlPin;
									router.cd_mux_state = cd_path->ucMuxState[enum_id];
									break;
								}
								record = (ATOM_COMMON_RECORD_HEADER *)
									((char *)record + record->ucRecordSize);
							}
						}
					}
				}
			}

			/* look up gpio for ddc, hpd */
			ddc_bus.valid = false;
			hpd.hpd = AMDGPU_HPD_NONE;
			if ((le16_to_cpu(path->usDeviceTag) &
			     (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT)) == 0) {
				for (j = 0; j < con_obj->ucNumberOfObjects; j++) {
					if (le16_to_cpu(path->usConnObjectId) ==
					    le16_to_cpu(con_obj->asObjects[j].
							usObjectID)) {
						ATOM_COMMON_RECORD_HEADER
						    *record =
						    (ATOM_COMMON_RECORD_HEADER
						     *)
						    (ctx->bios + data_offset +
						     le16_to_cpu(con_obj->
								 asObjects[j].
								 usRecordOffset));
						ATOM_I2C_RECORD *i2c_record;
						ATOM_HPD_INT_RECORD *hpd_record;
						ATOM_I2C_ID_CONFIG_ACCESS *i2c_config;

						while (record->ucRecordSize > 0 &&
						       record->ucRecordType > 0 &&
						       record->ucRecordType <= ATOM_MAX_OBJECT_RECORD_NUMBER) {
							switch (record->ucRecordType) {
							case ATOM_I2C_RECORD_TYPE:
								i2c_record =
								    (ATOM_I2C_RECORD *)
									record;
								i2c_config =
									(ATOM_I2C_ID_CONFIG_ACCESS *)
									&i2c_record->sucI2cId;
								ddc_bus = amdgpu_atombios_lookup_i2c_gpio(adev,
												 i2c_config->
												 ucAccess);
								break;
							case ATOM_HPD_INT_RECORD_TYPE:
								hpd_record =
									(ATOM_HPD_INT_RECORD *)
									record;
								gpio = amdgpu_atombios_lookup_gpio(adev,
											  hpd_record->ucHPDIntGPIOID);
								hpd = amdgpu_atombios_get_hpd_info_from_gpio(adev, &gpio);
								hpd.plugged_state = hpd_record->ucPlugged_PinState;
								break;
							}
							record =
							    (ATOM_COMMON_RECORD_HEADER
							     *) ((char *)record
								 +
								 record->
								 ucRecordSize);
						}
						break;
					}
				}
			}

			/* needed for aux chan transactions */
			ddc_bus.hpd = hpd.hpd;

			conn_id = le16_to_cpu(path->usConnObjectId);

			amdgpu_display_add_connector(adev,
						      conn_id,
						      le16_to_cpu(path->usDeviceTag),
						      connector_type, &ddc_bus,
						      connector_object_id,
						      &hpd,
						      &router);

		}
	}

	amdgpu_link_encoder_connector(adev->ddev);

	return true;
}

union firmware_info {
	ATOM_FIRMWARE_INFO info;
	ATOM_FIRMWARE_INFO_V1_2 info_12;
	ATOM_FIRMWARE_INFO_V1_3 info_13;
	ATOM_FIRMWARE_INFO_V1_4 info_14;
	ATOM_FIRMWARE_INFO_V2_1 info_21;
	ATOM_FIRMWARE_INFO_V2_2 info_22;
};

int amdgpu_atombios_get_clock_info(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	uint8_t frev, crev;
	uint16_t data_offset;
	int ret = -EINVAL;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		int i;
		struct amdgpu_pll *ppll = &adev->clock.ppll[0];
		struct amdgpu_pll *spll = &adev->clock.spll;
		struct amdgpu_pll *mpll = &adev->clock.mpll;
		union firmware_info *firmware_info =
			(union firmware_info *)(mode_info->atom_context->bios +
						data_offset);
		/* pixel clocks */
		ppll->reference_freq =
		    le16_to_cpu(firmware_info->info.usReferenceClock);
		ppll->reference_div = 0;

		ppll->pll_out_min =
			le32_to_cpu(firmware_info->info_12.ulMinPixelClockPLL_Output);
		ppll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxPixelClockPLL_Output);

		ppll->lcd_pll_out_min =
			le16_to_cpu(firmware_info->info_14.usLcdMinPixelClockPLL_Output) * 100;
		if (ppll->lcd_pll_out_min == 0)
			ppll->lcd_pll_out_min = ppll->pll_out_min;
		ppll->lcd_pll_out_max =
			le16_to_cpu(firmware_info->info_14.usLcdMaxPixelClockPLL_Output) * 100;
		if (ppll->lcd_pll_out_max == 0)
			ppll->lcd_pll_out_max = ppll->pll_out_max;

		if (ppll->pll_out_min == 0)
			ppll->pll_out_min = 64800;

		ppll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinPixelClockPLL_Input);
		ppll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxPixelClockPLL_Input);

		ppll->min_post_div = 2;
		ppll->max_post_div = 0x7f;
		ppll->min_frac_feedback_div = 0;
		ppll->max_frac_feedback_div = 9;
		ppll->min_ref_div = 2;
		ppll->max_ref_div = 0x3ff;
		ppll->min_feedback_div = 4;
		ppll->max_feedback_div = 0xfff;
		ppll->best_vco = 0;

		for (i = 1; i < AMDGPU_MAX_PPLL; i++)
			adev->clock.ppll[i] = *ppll;

		/* system clock */
		spll->reference_freq =
			le16_to_cpu(firmware_info->info_21.usCoreReferenceClock);
		spll->reference_div = 0;

		spll->pll_out_min =
		    le16_to_cpu(firmware_info->info.usMinEngineClockPLL_Output);
		spll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxEngineClockPLL_Output);

		/* ??? */
		if (spll->pll_out_min == 0)
			spll->pll_out_min = 64800;

		spll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinEngineClockPLL_Input);
		spll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxEngineClockPLL_Input);

		spll->min_post_div = 1;
		spll->max_post_div = 1;
		spll->min_ref_div = 2;
		spll->max_ref_div = 0xff;
		spll->min_feedback_div = 4;
		spll->max_feedback_div = 0xff;
		spll->best_vco = 0;

		/* memory clock */
		mpll->reference_freq =
			le16_to_cpu(firmware_info->info_21.usMemoryReferenceClock);
		mpll->reference_div = 0;

		mpll->pll_out_min =
		    le16_to_cpu(firmware_info->info.usMinMemoryClockPLL_Output);
		mpll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxMemoryClockPLL_Output);

		/* ??? */
		if (mpll->pll_out_min == 0)
			mpll->pll_out_min = 64800;

		mpll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinMemoryClockPLL_Input);
		mpll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxMemoryClockPLL_Input);

		adev->clock.default_sclk =
		    le32_to_cpu(firmware_info->info.ulDefaultEngineClock);
		adev->clock.default_mclk =
		    le32_to_cpu(firmware_info->info.ulDefaultMemoryClock);

		mpll->min_post_div = 1;
		mpll->max_post_div = 1;
		mpll->min_ref_div = 2;
		mpll->max_ref_div = 0xff;
		mpll->min_feedback_div = 4;
		mpll->max_feedback_div = 0xff;
		mpll->best_vco = 0;

		/* disp clock */
		adev->clock.default_dispclk =
			le32_to_cpu(firmware_info->info_21.ulDefaultDispEngineClkFreq);
		/* set a reasonable default for DP */
		if (adev->clock.default_dispclk < 53900) {
			DRM_INFO("Changing default dispclk from %dMhz to 600Mhz\n",
				 adev->clock.default_dispclk / 100);
			adev->clock.default_dispclk = 60000;
		} else if (adev->clock.default_dispclk <= 60000) {
			DRM_INFO("Changing default dispclk from %dMhz to 625Mhz\n",
				 adev->clock.default_dispclk / 100);
			adev->clock.default_dispclk = 62500;
		}
		adev->clock.dp_extclk =
			le16_to_cpu(firmware_info->info_21.usUniphyDPModeExtClkFreq);
		adev->clock.current_dispclk = adev->clock.default_dispclk;

		adev->clock.max_pixel_clock = le16_to_cpu(firmware_info->info.usMaxPixelClock);
		if (adev->clock.max_pixel_clock == 0)
			adev->clock.max_pixel_clock = 40000;

		/* not technically a clock, but... */
		adev->mode_info.firmware_flags =
			le16_to_cpu(firmware_info->info.usFirmwareCapability.susAccess);

		ret = 0;
	}

	adev->pm.current_sclk = adev->clock.default_sclk;
	adev->pm.current_mclk = adev->clock.default_mclk;

	return ret;
}

union gfx_info {
	ATOM_GFX_INFO_V2_1 info;
};

int amdgpu_atombios_get_gfx_info(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, GFX_Info);
	uint8_t frev, crev;
	uint16_t data_offset;
	int ret = -EINVAL;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		union gfx_info *gfx_info = (union gfx_info *)
			(mode_info->atom_context->bios + data_offset);

		adev->gfx.config.max_shader_engines = gfx_info->info.max_shader_engines;
		adev->gfx.config.max_tile_pipes = gfx_info->info.max_tile_pipes;
		adev->gfx.config.max_cu_per_sh = gfx_info->info.max_cu_per_sh;
		adev->gfx.config.max_sh_per_se = gfx_info->info.max_sh_per_se;
		adev->gfx.config.max_backends_per_se = gfx_info->info.max_backends_per_se;
		adev->gfx.config.max_texture_channel_caches =
			gfx_info->info.max_texture_channel_caches;

		ret = 0;
	}
	return ret;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V6 info_6;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_7 info_7;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_8 info_8;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_9 info_9;
};

/*
 * Return vram width from integrated system info table, if available,
 * or 0 if not.
 */
int amdgpu_atombios_get_vram_width(struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	u16 data_offset, size;
	union igp_info *igp_info;
	u8 frev, crev;

	/* get any igp specific overrides */
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		igp_info = (union igp_info *)
			(mode_info->atom_context->bios + data_offset);
		switch (crev) {
		case 8:
		case 9:
			return igp_info->info_8.ucUMAChannelNumber * 64;
		default:
			return 0;
		}
	}

	return 0;
}

static void amdgpu_atombios_get_igp_ss_overrides(struct amdgpu_device *adev,
						 struct amdgpu_atom_ss *ss,
						 int id)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	u16 data_offset, size;
	union igp_info *igp_info;
	u8 frev, crev;
	u16 percentage = 0, rate = 0;

	/* get any igp specific overrides */
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		igp_info = (union igp_info *)
			(mode_info->atom_context->bios + data_offset);
		switch (crev) {
		case 6:
			switch (id) {
			case ASIC_INTERNAL_SS_ON_TMDS:
				percentage = le16_to_cpu(igp_info->info_6.usDVISSPercentage);
				rate = le16_to_cpu(igp_info->info_6.usDVISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_HDMI:
				percentage = le16_to_cpu(igp_info->info_6.usHDMISSPercentage);
				rate = le16_to_cpu(igp_info->info_6.usHDMISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_LVDS:
				percentage = le16_to_cpu(igp_info->info_6.usLvdsSSPercentage);
				rate = le16_to_cpu(igp_info->info_6.usLvdsSSpreadRateIn10Hz);
				break;
			}
			break;
		case 7:
			switch (id) {
			case ASIC_INTERNAL_SS_ON_TMDS:
				percentage = le16_to_cpu(igp_info->info_7.usDVISSPercentage);
				rate = le16_to_cpu(igp_info->info_7.usDVISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_HDMI:
				percentage = le16_to_cpu(igp_info->info_7.usHDMISSPercentage);
				rate = le16_to_cpu(igp_info->info_7.usHDMISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_LVDS:
				percentage = le16_to_cpu(igp_info->info_7.usLvdsSSPercentage);
				rate = le16_to_cpu(igp_info->info_7.usLvdsSSpreadRateIn10Hz);
				break;
			}
			break;
		case 8:
			switch (id) {
			case ASIC_INTERNAL_SS_ON_TMDS:
				percentage = le16_to_cpu(igp_info->info_8.usDVISSPercentage);
				rate = le16_to_cpu(igp_info->info_8.usDVISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_HDMI:
				percentage = le16_to_cpu(igp_info->info_8.usHDMISSPercentage);
				rate = le16_to_cpu(igp_info->info_8.usHDMISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_LVDS:
				percentage = le16_to_cpu(igp_info->info_8.usLvdsSSPercentage);
				rate = le16_to_cpu(igp_info->info_8.usLvdsSSpreadRateIn10Hz);
				break;
			}
			break;
		case 9:
			switch (id) {
			case ASIC_INTERNAL_SS_ON_TMDS:
				percentage = le16_to_cpu(igp_info->info_9.usDVISSPercentage);
				rate = le16_to_cpu(igp_info->info_9.usDVISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_HDMI:
				percentage = le16_to_cpu(igp_info->info_9.usHDMISSPercentage);
				rate = le16_to_cpu(igp_info->info_9.usHDMISSpreadRateIn10Hz);
				break;
			case ASIC_INTERNAL_SS_ON_LVDS:
				percentage = le16_to_cpu(igp_info->info_9.usLvdsSSPercentage);
				rate = le16_to_cpu(igp_info->info_9.usLvdsSSpreadRateIn10Hz);
				break;
			}
			break;
		default:
			DRM_ERROR("Unsupported IGP table: %d %d\n", frev, crev);
			break;
		}
		if (percentage)
			ss->percentage = percentage;
		if (rate)
			ss->rate = rate;
	}
}

union asic_ss_info {
	struct _ATOM_ASIC_INTERNAL_SS_INFO info;
	struct _ATOM_ASIC_INTERNAL_SS_INFO_V2 info_2;
	struct _ATOM_ASIC_INTERNAL_SS_INFO_V3 info_3;
};

union asic_ss_assignment {
	struct _ATOM_ASIC_SS_ASSIGNMENT v1;
	struct _ATOM_ASIC_SS_ASSIGNMENT_V2 v2;
	struct _ATOM_ASIC_SS_ASSIGNMENT_V3 v3;
};

bool amdgpu_atombios_get_asic_ss_info(struct amdgpu_device *adev,
				      struct amdgpu_atom_ss *ss,
				      int id, u32 clock)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	uint16_t data_offset, size;
	union asic_ss_info *ss_info;
	union asic_ss_assignment *ss_assign;
	uint8_t frev, crev;
	int i, num_indices;

	if (id == ASIC_INTERNAL_MEMORY_SS) {
		if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_MEMORY_CLOCK_SS_SUPPORT))
			return false;
	}
	if (id == ASIC_INTERNAL_ENGINE_SS) {
		if (!(adev->mode_info.firmware_flags & ATOM_BIOS_INFO_ENGINE_CLOCK_SS_SUPPORT))
			return false;
	}

	memset(ss, 0, sizeof(struct amdgpu_atom_ss));
	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, &size,
				   &frev, &crev, &data_offset)) {

		ss_info =
			(union asic_ss_info *)(mode_info->atom_context->bios + data_offset);

		switch (frev) {
		case 1:
			num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
				sizeof(ATOM_ASIC_SS_ASSIGNMENT);

			ss_assign = (union asic_ss_assignment *)((u8 *)&ss_info->info.asSpreadSpectrum[0]);
			for (i = 0; i < num_indices; i++) {
				if ((ss_assign->v1.ucClockIndication == id) &&
				    (clock <= le32_to_cpu(ss_assign->v1.ulTargetClockRange))) {
					ss->percentage =
						le16_to_cpu(ss_assign->v1.usSpreadSpectrumPercentage);
					ss->type = ss_assign->v1.ucSpreadSpectrumMode;
					ss->rate = le16_to_cpu(ss_assign->v1.usSpreadRateInKhz);
					ss->percentage_divider = 100;
					return true;
				}
				ss_assign = (union asic_ss_assignment *)
					((u8 *)ss_assign + sizeof(ATOM_ASIC_SS_ASSIGNMENT));
			}
			break;
		case 2:
			num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
				sizeof(ATOM_ASIC_SS_ASSIGNMENT_V2);
			ss_assign = (union asic_ss_assignment *)((u8 *)&ss_info->info_2.asSpreadSpectrum[0]);
			for (i = 0; i < num_indices; i++) {
				if ((ss_assign->v2.ucClockIndication == id) &&
				    (clock <= le32_to_cpu(ss_assign->v2.ulTargetClockRange))) {
					ss->percentage =
						le16_to_cpu(ss_assign->v2.usSpreadSpectrumPercentage);
					ss->type = ss_assign->v2.ucSpreadSpectrumMode;
					ss->rate = le16_to_cpu(ss_assign->v2.usSpreadRateIn10Hz);
					ss->percentage_divider = 100;
					if ((crev == 2) &&
					    ((id == ASIC_INTERNAL_ENGINE_SS) ||
					     (id == ASIC_INTERNAL_MEMORY_SS)))
						ss->rate /= 100;
					return true;
				}
				ss_assign = (union asic_ss_assignment *)
					((u8 *)ss_assign + sizeof(ATOM_ASIC_SS_ASSIGNMENT_V2));
			}
			break;
		case 3:
			num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
				sizeof(ATOM_ASIC_SS_ASSIGNMENT_V3);
			ss_assign = (union asic_ss_assignment *)((u8 *)&ss_info->info_3.asSpreadSpectrum[0]);
			for (i = 0; i < num_indices; i++) {
				if ((ss_assign->v3.ucClockIndication == id) &&
				    (clock <= le32_to_cpu(ss_assign->v3.ulTargetClockRange))) {
					ss->percentage =
						le16_to_cpu(ss_assign->v3.usSpreadSpectrumPercentage);
					ss->type = ss_assign->v3.ucSpreadSpectrumMode;
					ss->rate = le16_to_cpu(ss_assign->v3.usSpreadRateIn10Hz);
					if (ss_assign->v3.ucSpreadSpectrumMode &
					    SS_MODE_V3_PERCENTAGE_DIV_BY_1000_MASK)
						ss->percentage_divider = 1000;
					else
						ss->percentage_divider = 100;
					if ((id == ASIC_INTERNAL_ENGINE_SS) ||
					    (id == ASIC_INTERNAL_MEMORY_SS))
						ss->rate /= 100;
					if (adev->flags & AMD_IS_APU)
						amdgpu_atombios_get_igp_ss_overrides(adev, ss, id);
					return true;
				}
				ss_assign = (union asic_ss_assignment *)
					((u8 *)ss_assign + sizeof(ATOM_ASIC_SS_ASSIGNMENT_V3));
			}
			break;
		default:
			DRM_ERROR("Unsupported ASIC_InternalSS_Info table: %d %d\n", frev, crev);
			break;
		}

	}
	return false;
}

union get_clock_dividers {
	struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS v1;
	struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V2 v2;
	struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V3 v3;
	struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V4 v4;
	struct _COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V5 v5;
	struct _COMPUTE_GPU_CLOCK_INPUT_PARAMETERS_V1_6 v6_in;
	struct _COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6 v6_out;
};

int amdgpu_atombios_get_clock_dividers(struct amdgpu_device *adev,
				       u8 clock_type,
				       u32 clock,
				       bool strobe_mode,
				       struct atom_clock_dividers *dividers)
{
	union get_clock_dividers args;
	int index = GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL);
	u8 frev, crev;

	memset(&args, 0, sizeof(args));
	memset(dividers, 0, sizeof(struct atom_clock_dividers));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 2:
	case 3:
	case 5:
		/* r6xx, r7xx, evergreen, ni, si.
		 * TODO: add support for asic_type <= CHIP_RV770*/
		if (clock_type == COMPUTE_ENGINE_PLL_PARAM) {
			args.v3.ulClockParams = cpu_to_le32((clock_type << 24) | clock);

			amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

			dividers->post_div = args.v3.ucPostDiv;
			dividers->enable_post_div = (args.v3.ucCntlFlag &
						     ATOM_PLL_CNTL_FLAG_PLL_POST_DIV_EN) ? true : false;
			dividers->enable_dithen = (args.v3.ucCntlFlag &
						   ATOM_PLL_CNTL_FLAG_FRACTION_DISABLE) ? false : true;
			dividers->whole_fb_div = le16_to_cpu(args.v3.ulFbDiv.usFbDiv);
			dividers->frac_fb_div = le16_to_cpu(args.v3.ulFbDiv.usFbDivFrac);
			dividers->ref_div = args.v3.ucRefDiv;
			dividers->vco_mode = (args.v3.ucCntlFlag &
					      ATOM_PLL_CNTL_FLAG_MPLL_VCO_MODE) ? 1 : 0;
		} else {
			/* for SI we use ComputeMemoryClockParam for memory plls */
			if (adev->asic_type >= CHIP_TAHITI)
				return -EINVAL;
			args.v5.ulClockParams = cpu_to_le32((clock_type << 24) | clock);
			if (strobe_mode)
				args.v5.ucInputFlag = ATOM_PLL_INPUT_FLAG_PLL_STROBE_MODE_EN;

			amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

			dividers->post_div = args.v5.ucPostDiv;
			dividers->enable_post_div = (args.v5.ucCntlFlag &
						     ATOM_PLL_CNTL_FLAG_PLL_POST_DIV_EN) ? true : false;
			dividers->enable_dithen = (args.v5.ucCntlFlag &
						   ATOM_PLL_CNTL_FLAG_FRACTION_DISABLE) ? false : true;
			dividers->whole_fb_div = le16_to_cpu(args.v5.ulFbDiv.usFbDiv);
			dividers->frac_fb_div = le16_to_cpu(args.v5.ulFbDiv.usFbDivFrac);
			dividers->ref_div = args.v5.ucRefDiv;
			dividers->vco_mode = (args.v5.ucCntlFlag &
					      ATOM_PLL_CNTL_FLAG_MPLL_VCO_MODE) ? 1 : 0;
		}
		break;
	case 4:
		/* fusion */
		args.v4.ulClock = cpu_to_le32(clock);	/* 10 khz */

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		dividers->post_divider = dividers->post_div = args.v4.ucPostDiv;
		dividers->real_clock = le32_to_cpu(args.v4.ulClock);
		break;
	case 6:
		/* CI */
		/* COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK, COMPUTE_GPUCLK_INPUT_FLAG_SCLK */
		args.v6_in.ulClock.ulComputeClockFlag = clock_type;
		args.v6_in.ulClock.ulClockFreq = cpu_to_le32(clock);	/* 10 khz */

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		dividers->whole_fb_div = le16_to_cpu(args.v6_out.ulFbDiv.usFbDiv);
		dividers->frac_fb_div = le16_to_cpu(args.v6_out.ulFbDiv.usFbDivFrac);
		dividers->ref_div = args.v6_out.ucPllRefDiv;
		dividers->post_div = args.v6_out.ucPllPostDiv;
		dividers->flags = args.v6_out.ucPllCntlFlag;
		dividers->real_clock = le32_to_cpu(args.v6_out.ulClock.ulClock);
		dividers->post_divider = args.v6_out.ulClock.ucPostDiv;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int amdgpu_atombios_get_memory_pll_dividers(struct amdgpu_device *adev,
					    u32 clock,
					    bool strobe_mode,
					    struct atom_mpll_param *mpll_param)
{
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1 args;
	int index = GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam);
	u8 frev, crev;

	memset(&args, 0, sizeof(args));
	memset(mpll_param, 0, sizeof(struct atom_mpll_param));

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (frev) {
	case 2:
		switch (crev) {
		case 1:
			/* SI */
			args.ulClock = cpu_to_le32(clock);	/* 10 khz */
			args.ucInputFlag = 0;
			if (strobe_mode)
				args.ucInputFlag |= MPLL_INPUT_FLAG_STROBE_MODE_EN;

			amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

			mpll_param->clkfrac = le16_to_cpu(args.ulFbDiv.usFbDivFrac);
			mpll_param->clkf = le16_to_cpu(args.ulFbDiv.usFbDiv);
			mpll_param->post_div = args.ucPostDiv;
			mpll_param->dll_speed = args.ucDllSpeed;
			mpll_param->bwcntl = args.ucBWCntl;
			mpll_param->vco_mode =
				(args.ucPllCntlFlag & MPLL_CNTL_FLAG_VCO_MODE_MASK);
			mpll_param->yclk_sel =
				(args.ucPllCntlFlag & MPLL_CNTL_FLAG_BYPASS_DQ_PLL) ? 1 : 0;
			mpll_param->qdr =
				(args.ucPllCntlFlag & MPLL_CNTL_FLAG_QDR_ENABLE) ? 1 : 0;
			mpll_param->half_rate =
				(args.ucPllCntlFlag & MPLL_CNTL_FLAG_AD_HALF_RATE) ? 1 : 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

void amdgpu_atombios_set_engine_dram_timings(struct amdgpu_device *adev,
					     u32 eng_clock, u32 mem_clock)
{
	SET_ENGINE_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings);
	u32 tmp;

	memset(&args, 0, sizeof(args));

	tmp = eng_clock & SET_CLOCK_FREQ_MASK;
	tmp |= (COMPUTE_ENGINE_PLL_PARAM << 24);

	args.ulTargetEngineClock = cpu_to_le32(tmp);
	if (mem_clock)
		args.sReserved.ulClock = cpu_to_le32(mem_clock & SET_CLOCK_FREQ_MASK);

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);
}

void amdgpu_atombios_get_default_voltages(struct amdgpu_device *adev,
					  u16 *vddc, u16 *vddci, u16 *mvdd)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	u8 frev, crev;
	u16 data_offset;
	union firmware_info *firmware_info;

	*vddc = 0;
	*vddci = 0;
	*mvdd = 0;

	if (amdgpu_atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		firmware_info =
			(union firmware_info *)(mode_info->atom_context->bios +
						data_offset);
		*vddc = le16_to_cpu(firmware_info->info_14.usBootUpVDDCVoltage);
		if ((frev == 2) && (crev >= 2)) {
			*vddci = le16_to_cpu(firmware_info->info_22.usBootUpVDDCIVoltage);
			*mvdd = le16_to_cpu(firmware_info->info_22.usBootUpMVDDCVoltage);
		}
	}
}

union set_voltage {
	struct _SET_VOLTAGE_PS_ALLOCATION alloc;
	struct _SET_VOLTAGE_PARAMETERS v1;
	struct _SET_VOLTAGE_PARAMETERS_V2 v2;
	struct _SET_VOLTAGE_PARAMETERS_V1_3 v3;
};

int amdgpu_atombios_get_max_vddc(struct amdgpu_device *adev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev;

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 1:
		return -EINVAL;
	case 2:
		args.v2.ucVoltageType = SET_VOLTAGE_GET_MAX_VOLTAGE;
		args.v2.ucVoltageMode = 0;
		args.v2.usVoltageLevel = 0;

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		*voltage = le16_to_cpu(args.v2.usVoltageLevel);
		break;
	case 3:
		args.v3.ucVoltageType = voltage_type;
		args.v3.ucVoltageMode = ATOM_GET_VOLTAGE_LEVEL;
		args.v3.usVoltageLevel = cpu_to_le16(voltage_id);

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		*voltage = le16_to_cpu(args.v3.usVoltageLevel);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

	return 0;
}

int amdgpu_atombios_get_leakage_vddc_based_on_leakage_idx(struct amdgpu_device *adev,
						      u16 *voltage,
						      u16 leakage_idx)
{
	return amdgpu_atombios_get_max_vddc(adev, VOLTAGE_TYPE_VDDC, leakage_idx, voltage);
}

int amdgpu_atombios_get_leakage_id_from_vbios(struct amdgpu_device *adev,
					      u16 *leakage_id)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev;

	if (!amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 3:
	case 4:
		args.v3.ucVoltageType = 0;
		args.v3.ucVoltageMode = ATOM_GET_LEAKAGE_ID;
		args.v3.usVoltageLevel = 0;

		amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

		*leakage_id = le16_to_cpu(args.v3.usVoltageLevel);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

	return 0;
}

int amdgpu_atombios_get_leakage_vddc_based_on_leakage_params(struct amdgpu_device *adev,
							     u16 *vddc, u16 *vddci,
							     u16 virtual_voltage_id,
							     u16 vbios_voltage_id)
{
	int index = GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo);
	u8 frev, crev;
	u16 data_offset, size;
	int i, j;
	ATOM_ASIC_PROFILING_INFO_V2_1 *profile;
	u16 *leakage_bin, *vddc_id_buf, *vddc_buf, *vddci_id_buf, *vddci_buf;

	*vddc = 0;
	*vddci = 0;

	if (!amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				    &frev, &crev, &data_offset))
		return -EINVAL;

	profile = (ATOM_ASIC_PROFILING_INFO_V2_1 *)
		(adev->mode_info.atom_context->bios + data_offset);

	switch (frev) {
	case 1:
		return -EINVAL;
	case 2:
		switch (crev) {
		case 1:
			if (size < sizeof(ATOM_ASIC_PROFILING_INFO_V2_1))
				return -EINVAL;
			leakage_bin = (u16 *)
				(adev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usLeakageBinArrayOffset));
			vddc_id_buf = (u16 *)
				(adev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDC_IdArrayOffset));
			vddc_buf = (u16 *)
				(adev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDC_LevelArrayOffset));
			vddci_id_buf = (u16 *)
				(adev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDCI_IdArrayOffset));
			vddci_buf = (u16 *)
				(adev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDCI_LevelArrayOffset));

			if (profile->ucElbVDDC_Num > 0) {
				for (i = 0; i < profile->ucElbVDDC_Num; i++) {
					if (vddc_id_buf[i] == virtual_voltage_id) {
						for (j = 0; j < profile->ucLeakageBinNum; j++) {
							if (vbios_voltage_id <= leakage_bin[j]) {
								*vddc = vddc_buf[j * profile->ucElbVDDC_Num + i];
								break;
							}
						}
						break;
					}
				}
			}
			if (profile->ucElbVDDCI_Num > 0) {
				for (i = 0; i < profile->ucElbVDDCI_Num; i++) {
					if (vddci_id_buf[i] == virtual_voltage_id) {
						for (j = 0; j < profile->ucLeakageBinNum; j++) {
							if (vbios_voltage_id <= leakage_bin[j]) {
								*vddci = vddci_buf[j * profile->ucElbVDDCI_Num + i];
								break;
							}
						}
						break;
					}
				}
			}
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			return -EINVAL;
		}
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

	return 0;
}

union get_voltage_info {
	struct _GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2 in;
	struct _GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2 evv_out;
};

int amdgpu_atombios_get_voltage_evv(struct amdgpu_device *adev,
				    u16 virtual_voltage_id,
				    u16 *voltage)
{
	int index = GetIndexIntoMasterTable(COMMAND, GetVoltageInfo);
	u32 entry_id;
	u32 count = adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.count;
	union get_voltage_info args;

	for (entry_id = 0; entry_id < count; entry_id++) {
		if (adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[entry_id].v ==
		    virtual_voltage_id)
			break;
	}

	if (entry_id >= count)
		return -EINVAL;

	args.in.ucVoltageType = VOLTAGE_TYPE_VDDC;
	args.in.ucVoltageMode = ATOM_GET_VOLTAGE_EVV_VOLTAGE;
	args.in.usVoltageLevel = cpu_to_le16(virtual_voltage_id);
	args.in.ulSCLKFreq =
		cpu_to_le32(adev->pm.dpm.dyn_state.vddc_dependency_on_sclk.entries[entry_id].clk);

	amdgpu_atom_execute_table(adev->mode_info.atom_context, index, (uint32_t *)&args);

	*voltage = le16_to_cpu(args.evv_out.usVoltageLevel);

	return 0;
}

union voltage_object_info {
	struct _ATOM_VOLTAGE_OBJECT_INFO v1;
	struct _ATOM_VOLTAGE_OBJECT_INFO_V2 v2;
	struct _ATOM_VOLTAGE_OBJECT_INFO_V3_1 v3;
};

union voltage_object {
	struct _ATOM_VOLTAGE_OBJECT v1;
	struct _ATOM_VOLTAGE_OBJECT_V2 v2;
	union _ATOM_VOLTAGE_OBJECT_V3 v3;
};


static ATOM_VOLTAGE_OBJECT_V3 *amdgpu_atombios_lookup_voltage_object_v3(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *v3,
									u8 voltage_type, u8 voltage_mode)
{
	u32 size = le16_to_cpu(v3->sHeader.usStructureSize);
	u32 offset = offsetof(ATOM_VOLTAGE_OBJECT_INFO_V3_1, asVoltageObj[0]);
	u8 *start = (u8*)v3;

	while (offset < size) {
		ATOM_VOLTAGE_OBJECT_V3 *vo = (ATOM_VOLTAGE_OBJECT_V3 *)(start + offset);
		if ((vo->asGpioVoltageObj.sHeader.ucVoltageType == voltage_type) &&
		    (vo->asGpioVoltageObj.sHeader.ucVoltageMode == voltage_mode))
			return vo;
		offset += le16_to_cpu(vo->asGpioVoltageObj.sHeader.usSize);
	}
	return NULL;
}

int amdgpu_atombios_get_svi2_info(struct amdgpu_device *adev,
			      u8 voltage_type,
			      u8 *svd_gpio_id, u8 *svc_gpio_id)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(adev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 3:
			switch (crev) {
			case 1:
				voltage_object = (union voltage_object *)
					amdgpu_atombios_lookup_voltage_object_v3(&voltage_info->v3,
								      voltage_type,
								      VOLTAGE_OBJ_SVID2);
				if (voltage_object) {
					*svd_gpio_id = voltage_object->v3.asSVID2Obj.ucSVDGpioId;
					*svc_gpio_id = voltage_object->v3.asSVID2Obj.ucSVCGpioId;
				} else {
					return -EINVAL;
				}
				break;
			default:
				DRM_ERROR("unknown voltage object table\n");
				return -EINVAL;
			}
			break;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return -EINVAL;
		}

	}
	return 0;
}

bool
amdgpu_atombios_is_voltage_gpio(struct amdgpu_device *adev,
				u8 voltage_type, u8 voltage_mode)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(adev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 3:
			switch (crev) {
			case 1:
				if (amdgpu_atombios_lookup_voltage_object_v3(&voltage_info->v3,
								  voltage_type, voltage_mode))
					return true;
				break;
			default:
				DRM_ERROR("unknown voltage object table\n");
				return false;
			}
			break;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return false;
		}

	}
	return false;
}

int amdgpu_atombios_get_voltage_table(struct amdgpu_device *adev,
				      u8 voltage_type, u8 voltage_mode,
				      struct atom_voltage_table *voltage_table)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	int i;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(adev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 3:
			switch (crev) {
			case 1:
				voltage_object = (union voltage_object *)
					amdgpu_atombios_lookup_voltage_object_v3(&voltage_info->v3,
								      voltage_type, voltage_mode);
				if (voltage_object) {
					ATOM_GPIO_VOLTAGE_OBJECT_V3 *gpio =
						&voltage_object->v3.asGpioVoltageObj;
					VOLTAGE_LUT_ENTRY_V2 *lut;
					if (gpio->ucGpioEntryNum > MAX_VOLTAGE_ENTRIES)
						return -EINVAL;
					lut = &gpio->asVolGpioLut[0];
					for (i = 0; i < gpio->ucGpioEntryNum; i++) {
						voltage_table->entries[i].value =
							le16_to_cpu(lut->usVoltageValue);
						voltage_table->entries[i].smio_low =
							le32_to_cpu(lut->ulVoltageId);
						lut = (VOLTAGE_LUT_ENTRY_V2 *)
							((u8 *)lut + sizeof(VOLTAGE_LUT_ENTRY_V2));
					}
					voltage_table->mask_low = le32_to_cpu(gpio->ulGpioMaskVal);
					voltage_table->count = gpio->ucGpioEntryNum;
					voltage_table->phase_delay = gpio->ucPhaseDelay;
					return 0;
				}
				break;
			default:
				DRM_ERROR("unknown voltage object table\n");
				return -EINVAL;
			}
			break;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return -EINVAL;
		}
	}
	return -EINVAL;
}

union vram_info {
	struct _ATOM_VRAM_INFO_V3 v1_3;
	struct _ATOM_VRAM_INFO_V4 v1_4;
	struct _ATOM_VRAM_INFO_HEADER_V2_1 v2_1;
};

#define MEM_ID_MASK           0xff000000
#define MEM_ID_SHIFT          24
#define CLOCK_RANGE_MASK      0x00ffffff
#define CLOCK_RANGE_SHIFT     0
#define LOW_NIBBLE_MASK       0xf
#define DATA_EQU_PREV         0
#define DATA_FROM_TABLE       4

int amdgpu_atombios_init_mc_reg_table(struct amdgpu_device *adev,
				      u8 module_index,
				      struct atom_mc_reg_table *reg_table)
{
	int index = GetIndexIntoMasterTable(DATA, VRAM_Info);
	u8 frev, crev, num_entries, t_mem_id, num_ranges = 0;
	u32 i = 0, j;
	u16 data_offset, size;
	union vram_info *vram_info;

	memset(reg_table, 0, sizeof(struct atom_mc_reg_table));

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		vram_info = (union vram_info *)
			(adev->mode_info.atom_context->bios + data_offset);
		switch (frev) {
		case 1:
			DRM_ERROR("old table version %d, %d\n", frev, crev);
			return -EINVAL;
		case 2:
			switch (crev) {
			case 1:
				if (module_index < vram_info->v2_1.ucNumOfVRAMModule) {
					ATOM_INIT_REG_BLOCK *reg_block =
						(ATOM_INIT_REG_BLOCK *)
						((u8 *)vram_info + le16_to_cpu(vram_info->v2_1.usMemClkPatchTblOffset));
					ATOM_MEMORY_SETTING_DATA_BLOCK *reg_data =
						(ATOM_MEMORY_SETTING_DATA_BLOCK *)
						((u8 *)reg_block + (2 * sizeof(u16)) +
						 le16_to_cpu(reg_block->usRegIndexTblSize));
					ATOM_INIT_REG_INDEX_FORMAT *format = &reg_block->asRegIndexBuf[0];
					num_entries = (u8)((le16_to_cpu(reg_block->usRegIndexTblSize)) /
							   sizeof(ATOM_INIT_REG_INDEX_FORMAT)) - 1;
					if (num_entries > VBIOS_MC_REGISTER_ARRAY_SIZE)
						return -EINVAL;
					while (i < num_entries) {
						if (format->ucPreRegDataLength & ACCESS_PLACEHOLDER)
							break;
						reg_table->mc_reg_address[i].s1 =
							(u16)(le16_to_cpu(format->usRegIndex));
						reg_table->mc_reg_address[i].pre_reg_data =
							(u8)(format->ucPreRegDataLength);
						i++;
						format = (ATOM_INIT_REG_INDEX_FORMAT *)
							((u8 *)format + sizeof(ATOM_INIT_REG_INDEX_FORMAT));
					}
					reg_table->last = i;
					while ((le32_to_cpu(*(u32 *)reg_data) != END_OF_REG_DATA_BLOCK) &&
					       (num_ranges < VBIOS_MAX_AC_TIMING_ENTRIES)) {
						t_mem_id = (u8)((le32_to_cpu(*(u32 *)reg_data) & MEM_ID_MASK)
								>> MEM_ID_SHIFT);
						if (module_index == t_mem_id) {
							reg_table->mc_reg_table_entry[num_ranges].mclk_max =
								(u32)((le32_to_cpu(*(u32 *)reg_data) & CLOCK_RANGE_MASK)
								      >> CLOCK_RANGE_SHIFT);
							for (i = 0, j = 1; i < reg_table->last; i++) {
								if ((reg_table->mc_reg_address[i].pre_reg_data & LOW_NIBBLE_MASK) == DATA_FROM_TABLE) {
									reg_table->mc_reg_table_entry[num_ranges].mc_data[i] =
										(u32)le32_to_cpu(*((u32 *)reg_data + j));
									j++;
								} else if ((reg_table->mc_reg_address[i].pre_reg_data & LOW_NIBBLE_MASK) == DATA_EQU_PREV) {
									reg_table->mc_reg_table_entry[num_ranges].mc_data[i] =
										reg_table->mc_reg_table_entry[num_ranges].mc_data[i - 1];
								}
							}
							num_ranges++;
						}
						reg_data = (ATOM_MEMORY_SETTING_DATA_BLOCK *)
							((u8 *)reg_data + le16_to_cpu(reg_block->usRegDataBlkSize));
					}
					if (le32_to_cpu(*(u32 *)reg_data) != END_OF_REG_DATA_BLOCK)
						return -EINVAL;
					reg_table->num_entries = num_ranges;
				} else
					return -EINVAL;
				break;
			default:
				DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
				return -EINVAL;
			}
			break;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			return -EINVAL;
		}
		return 0;
	}
	return -EINVAL;
}

bool amdgpu_atombios_has_gpu_virtualization_table(struct amdgpu_device *adev)
{
	int index = GetIndexIntoMasterTable(DATA, GPUVirtualizationInfo);
	u8 frev, crev;
	u16 data_offset, size;

	if (amdgpu_atom_parse_data_header(adev->mode_info.atom_context, index, &size,
					  &frev, &crev, &data_offset))
		return true;

	return false;
}

void amdgpu_atombios_scratch_regs_lock(struct amdgpu_device *adev, bool lock)
{
	uint32_t bios_6_scratch;

	bios_6_scratch = RREG32(adev->bios_scratch_reg_offset + 6);

	if (lock) {
		bios_6_scratch |= ATOM_S6_CRITICAL_STATE;
		bios_6_scratch &= ~ATOM_S6_ACC_MODE;
	} else {
		bios_6_scratch &= ~ATOM_S6_CRITICAL_STATE;
		bios_6_scratch |= ATOM_S6_ACC_MODE;
	}

	WREG32(adev->bios_scratch_reg_offset + 6, bios_6_scratch);
}

void amdgpu_atombios_scratch_regs_init(struct amdgpu_device *adev)
{
	uint32_t bios_2_scratch, bios_6_scratch;

	adev->bios_scratch_reg_offset = mmBIOS_SCRATCH_0;

	bios_2_scratch = RREG32(adev->bios_scratch_reg_offset + 2);
	bios_6_scratch = RREG32(adev->bios_scratch_reg_offset + 6);

	/* let the bios control the backlight */
	bios_2_scratch &= ~ATOM_S2_VRI_BRIGHT_ENABLE;

	/* tell the bios not to handle mode switching */
	bios_6_scratch |= ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH;

	/* clear the vbios dpms state */
	bios_2_scratch &= ~ATOM_S2_DEVICE_DPMS_STATE;

	WREG32(adev->bios_scratch_reg_offset + 2, bios_2_scratch);
	WREG32(adev->bios_scratch_reg_offset + 6, bios_6_scratch);
}

void amdgpu_atombios_scratch_regs_save(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < AMDGPU_BIOS_NUM_SCRATCH; i++)
		adev->bios_scratch[i] = RREG32(adev->bios_scratch_reg_offset + i);
}

void amdgpu_atombios_scratch_regs_restore(struct amdgpu_device *adev)
{
	int i;

	/*
	 * VBIOS will check ASIC_INIT_COMPLETE bit to decide if
	 * execute ASIC_Init posting via driver
	 */
	adev->bios_scratch[7] &= ~ATOM_S7_ASIC_INIT_COMPLETE_MASK;

	for (i = 0; i < AMDGPU_BIOS_NUM_SCRATCH; i++)
		WREG32(adev->bios_scratch_reg_offset + i, adev->bios_scratch[i]);
}

void amdgpu_atombios_scratch_regs_engine_hung(struct amdgpu_device *adev,
					      bool hung)
{
	u32 tmp = RREG32(adev->bios_scratch_reg_offset + 3);

	if (hung)
		tmp |= ATOM_S3_ASIC_GUI_ENGINE_HUNG;
	else
		tmp &= ~ATOM_S3_ASIC_GUI_ENGINE_HUNG;

	WREG32(adev->bios_scratch_reg_offset + 3, tmp);
}

bool amdgpu_atombios_scratch_need_asic_init(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(adev->bios_scratch_reg_offset + 7);

	if (tmp & ATOM_S7_ASIC_INIT_COMPLETE_MASK)
		return false;
	else
		return true;
}

/* Atom needs data in little endian format
 * so swap as appropriate when copying data to
 * or from atom. Note that atom operates on
 * dw units.
 */
void amdgpu_atombios_copy_swap(u8 *dst, u8 *src, u8 num_bytes, bool to_le)
{
#ifdef __BIG_ENDIAN
	u8 src_tmp[20], dst_tmp[20]; /* used for byteswapping */
	u32 *dst32, *src32;
	int i;

	memcpy(src_tmp, src, num_bytes);
	src32 = (u32 *)src_tmp;
	dst32 = (u32 *)dst_tmp;
	if (to_le) {
		for (i = 0; i < ((num_bytes + 3) / 4); i++)
			dst32[i] = cpu_to_le32(src32[i]);
		memcpy(dst, dst_tmp, num_bytes);
	} else {
		u8 dws = num_bytes & ~3;
		for (i = 0; i < ((num_bytes + 3) / 4); i++)
			dst32[i] = le32_to_cpu(src32[i]);
		memcpy(dst, dst_tmp, dws);
		if (num_bytes % 4) {
			for (i = 0; i < (num_bytes % 4); i++)
				dst[dws+i] = dst_tmp[dws+i];
		}
	}
#else
	memcpy(dst, src, num_bytes);
#endif
}

int amdgpu_atombios_allocate_fb_scratch(struct amdgpu_device *adev)
{
	struct atom_context *ctx = adev->mode_info.atom_context;
	int index = GetIndexIntoMasterTable(DATA, VRAM_UsageByFirmware);
	uint16_t data_offset;
	int usage_bytes = 0;
	struct _ATOM_VRAM_USAGE_BY_FIRMWARE *firmware_usage;

	if (amdgpu_atom_parse_data_header(ctx, index, NULL, NULL, NULL, &data_offset)) {
		firmware_usage = (struct _ATOM_VRAM_USAGE_BY_FIRMWARE *)(ctx->bios + data_offset);

		DRM_DEBUG("atom firmware requested %08x %dkb\n",
			  le32_to_cpu(firmware_usage->asFirmwareVramReserveInfo[0].ulStartAddrUsedByFirmware),
			  le16_to_cpu(firmware_usage->asFirmwareVramReserveInfo[0].usFirmwareUseInKb));

		usage_bytes = le16_to_cpu(firmware_usage->asFirmwareVramReserveInfo[0].usFirmwareUseInKb) * 1024;
	}
	ctx->scratch_size_bytes = 0;
	if (usage_bytes == 0)
		usage_bytes = 20 * 1024;
	/* allocate some scratch memory */
	ctx->scratch = kzalloc(usage_bytes, GFP_KERNEL);
	if (!ctx->scratch)
		return -ENOMEM;
	ctx->scratch_size_bytes = usage_bytes;
	return 0;
}
