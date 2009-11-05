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
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#include "atom.h"
#include "atom-bits.h"

/* from radeon_encoder.c */
extern uint32_t
radeon_get_encoder_id(struct drm_device *dev, uint32_t supported_device,
		      uint8_t dac);
extern void radeon_link_encoder_connector(struct drm_device *dev);
extern void
radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_id,
			uint32_t supported_device);

/* from radeon_connector.c */
extern void
radeon_add_atom_connector(struct drm_device *dev,
			  uint32_t connector_id,
			  uint32_t supported_device,
			  int connector_type,
			  struct radeon_i2c_bus_rec *i2c_bus,
			  bool linkb, uint32_t igp_lane_info,
			  uint16_t connector_object_id);

/* from radeon_legacy_encoder.c */
extern void
radeon_add_legacy_encoder(struct drm_device *dev, uint32_t encoder_id,
			  uint32_t supported_device);

union atom_supported_devices {
	struct _ATOM_SUPPORTED_DEVICES_INFO info;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2 info_2;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2d1 info_2d1;
};

static inline struct radeon_i2c_bus_rec radeon_lookup_gpio(struct drm_device
							   *dev, uint8_t id)
{
	struct radeon_device *rdev = dev->dev_private;
	struct atom_context *ctx = rdev->mode_info.atom_context;
	ATOM_GPIO_I2C_ASSIGMENT gpio;
	struct radeon_i2c_bus_rec i2c;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	struct _ATOM_GPIO_I2C_INFO *i2c_info;
	uint16_t data_offset;

	memset(&i2c, 0, sizeof(struct radeon_i2c_bus_rec));
	i2c.valid = false;

	atom_parse_data_header(ctx, index, NULL, NULL, NULL, &data_offset);

	i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

	gpio = i2c_info->asGPIO_Info[id];

	i2c.mask_clk_reg = le16_to_cpu(gpio.usClkMaskRegisterIndex) * 4;
	i2c.mask_data_reg = le16_to_cpu(gpio.usDataMaskRegisterIndex) * 4;
	i2c.put_clk_reg = le16_to_cpu(gpio.usClkEnRegisterIndex) * 4;
	i2c.put_data_reg = le16_to_cpu(gpio.usDataEnRegisterIndex) * 4;
	i2c.get_clk_reg = le16_to_cpu(gpio.usClkY_RegisterIndex) * 4;
	i2c.get_data_reg = le16_to_cpu(gpio.usDataY_RegisterIndex) * 4;
	i2c.a_clk_reg = le16_to_cpu(gpio.usClkA_RegisterIndex) * 4;
	i2c.a_data_reg = le16_to_cpu(gpio.usDataA_RegisterIndex) * 4;
	i2c.mask_clk_mask = (1 << gpio.ucClkMaskShift);
	i2c.mask_data_mask = (1 << gpio.ucDataMaskShift);
	i2c.put_clk_mask = (1 << gpio.ucClkEnShift);
	i2c.put_data_mask = (1 << gpio.ucDataEnShift);
	i2c.get_clk_mask = (1 << gpio.ucClkY_Shift);
	i2c.get_data_mask = (1 << gpio.ucDataY_Shift);
	i2c.a_clk_mask = (1 << gpio.ucClkA_Shift);
	i2c.a_data_mask = (1 << gpio.ucDataA_Shift);
	i2c.valid = true;

	return i2c;
}

static bool radeon_atom_apply_quirks(struct drm_device *dev,
				     uint32_t supported_device,
				     int *connector_type,
				     struct radeon_i2c_bus_rec *i2c_bus,
				     uint16_t *line_mux)
{

	/* Asus M2A-VM HDMI board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x791e) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x826d)) {
		if ((*connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
		    (supported_device == ATOM_DEVICE_DFP3_SUPPORT))
			*connector_type = DRM_MODE_CONNECTOR_DVID;
	}

	/* a-bit f-i90hd - ciaranm on #radeonhd - this board has no DVI */
	if ((dev->pdev->device == 0x7941) &&
	    (dev->pdev->subsystem_vendor == 0x147b) &&
	    (dev->pdev->subsystem_device == 0x2412)) {
		if (*connector_type == DRM_MODE_CONNECTOR_DVII)
			return false;
	}

	/* Falcon NW laptop lists vga ddc line for LVDS */
	if ((dev->pdev->device == 0x5653) &&
	    (dev->pdev->subsystem_vendor == 0x1462) &&
	    (dev->pdev->subsystem_device == 0x0291)) {
		if (*connector_type == DRM_MODE_CONNECTOR_LVDS) {
			i2c_bus->valid = false;
			*line_mux = 53;
		}
	}

	/* Funky macbooks */
	if ((dev->pdev->device == 0x71C5) &&
	    (dev->pdev->subsystem_vendor == 0x106b) &&
	    (dev->pdev->subsystem_device == 0x0080)) {
		if ((supported_device == ATOM_DEVICE_CRT1_SUPPORT) ||
		    (supported_device == ATOM_DEVICE_DFP2_SUPPORT))
			return false;
	}

	/* ASUS HD 3600 XT board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x9598) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x01da)) {
		if (*connector_type == DRM_MODE_CONNECTOR_HDMIA) {
			*connector_type = DRM_MODE_CONNECTOR_DVII;
		}
	}

	/* ASUS HD 3450 board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x95C5) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x01e2)) {
		if (*connector_type == DRM_MODE_CONNECTOR_HDMIA) {
			*connector_type = DRM_MODE_CONNECTOR_DVII;
		}
	}

	/* some BIOSes seem to report DAC on HDMI - usually this is a board with
	 * HDMI + VGA reporting as HDMI
	 */
	if (*connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		if (supported_device & (ATOM_DEVICE_CRT_SUPPORT)) {
			*connector_type = DRM_MODE_CONNECTOR_VGA;
			*line_mux = 0;
		}
	}

	return true;
}

const int supported_devices_connector_convert[] = {
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_VGA,
	DRM_MODE_CONNECTOR_DVII,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_DVIA,
	DRM_MODE_CONNECTOR_SVIDEO,
	DRM_MODE_CONNECTOR_Composite,
	DRM_MODE_CONNECTOR_LVDS,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_HDMIA,
	DRM_MODE_CONNECTOR_HDMIB,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_9PinDIN,
	DRM_MODE_CONNECTOR_DisplayPort
};

const uint16_t supported_devices_connector_object_id_convert[] = {
	CONNECTOR_OBJECT_ID_NONE,
	CONNECTOR_OBJECT_ID_VGA,
	CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I, /* not all boards support DL */
	CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D, /* not all boards support DL */
	CONNECTOR_OBJECT_ID_VGA, /* technically DVI-A */
	CONNECTOR_OBJECT_ID_COMPOSITE,
	CONNECTOR_OBJECT_ID_SVIDEO,
	CONNECTOR_OBJECT_ID_LVDS,
	CONNECTOR_OBJECT_ID_9PIN_DIN,
	CONNECTOR_OBJECT_ID_9PIN_DIN,
	CONNECTOR_OBJECT_ID_DISPLAYPORT,
	CONNECTOR_OBJECT_ID_HDMI_TYPE_A,
	CONNECTOR_OBJECT_ID_HDMI_TYPE_B,
	CONNECTOR_OBJECT_ID_SVIDEO
};

const int object_connector_convert[] = {
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
	DRM_MODE_CONNECTOR_DisplayPort
};

bool radeon_get_atom_connector_info_from_object_table(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	uint16_t size, data_offset;
	uint8_t frev, crev, line_mux = 0;
	ATOM_CONNECTOR_OBJECT_TABLE *con_obj;
	ATOM_DISPLAY_OBJECT_PATH_TABLE *path_obj;
	ATOM_OBJECT_HEADER *obj_header;
	int i, j, path_size, device_support;
	int connector_type;
	uint16_t igp_lane_info, conn_id, connector_object_id;
	bool linkb;
	struct radeon_i2c_bus_rec ddc_bus;

	atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset);

	if (data_offset == 0)
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
	device_support = le16_to_cpu(obj_header->usDeviceSupport);

	path_size = 0;
	for (i = 0; i < path_obj->ucNumOfDispPath; i++) {
		uint8_t *addr = (uint8_t *) path_obj->asDispPath;
		ATOM_DISPLAY_OBJECT_PATH *path;
		addr += path_size;
		path = (ATOM_DISPLAY_OBJECT_PATH *) addr;
		path_size += le16_to_cpu(path->usSize);
		linkb = false;

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

			/* TODO CV support */
			if (le16_to_cpu(path->usDeviceTag) ==
				ATOM_DEVICE_CV_SUPPORT)
				continue;

			/* IGP chips */
			if ((rdev->flags & RADEON_IS_IGP) &&
			    (con_obj_id ==
			     CONNECTOR_OBJECT_ID_PCIE_CONNECTOR)) {
				uint16_t igp_offset = 0;
				ATOM_INTEGRATED_SYSTEM_INFO_V2 *igp_obj;

				index =
				    GetIndexIntoMasterTable(DATA,
							    IntegratedSystemInfo);

				atom_parse_data_header(ctx, index, &size, &frev,
						       &crev, &igp_offset);

				if (crev >= 2) {
					igp_obj =
					    (ATOM_INTEGRATED_SYSTEM_INFO_V2
					     *) (ctx->bios + igp_offset);

					if (igp_obj) {
						uint32_t slot_config, ct;

						if (con_obj_num == 1)
							slot_config =
							    igp_obj->
							    ulDDISlot1Config;
						else
							slot_config =
							    igp_obj->
							    ulDDISlot2Config;

						ct = (slot_config >> 16) & 0xff;
						connector_type =
						    object_connector_convert
						    [ct];
						connector_object_id = ct;
						igp_lane_info =
						    slot_config & 0xffff;
					} else
						continue;
				} else
					continue;
			} else {
				igp_lane_info = 0;
				connector_type =
				    object_connector_convert[con_obj_id];
				connector_object_id = con_obj_id;
			}

			if (connector_type == DRM_MODE_CONNECTOR_Unknown)
				continue;

			for (j = 0; j < ((le16_to_cpu(path->usSize) - 8) / 2);
			     j++) {
				uint8_t enc_obj_id, enc_obj_num, enc_obj_type;

				enc_obj_id =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
				enc_obj_num =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     ENUM_ID_MASK) >> ENUM_ID_SHIFT;
				enc_obj_type =
				    (le16_to_cpu(path->usGraphicObjIds[j]) &
				     OBJECT_TYPE_MASK) >> OBJECT_TYPE_SHIFT;

				/* FIXME: add support for router objects */
				if (enc_obj_type == GRAPH_OBJECT_TYPE_ENCODER) {
					if (enc_obj_num == 2)
						linkb = true;
					else
						linkb = false;

					radeon_add_atom_encoder(dev,
								enc_obj_id,
								le16_to_cpu
								(path->
								 usDeviceTag));

				}
			}

			/* look up gpio for ddc */
			if ((le16_to_cpu(path->usDeviceTag) &
			     (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))
			    == 0) {
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

						while (record->ucRecordType > 0
						       && record->
						       ucRecordType <=
						       ATOM_MAX_OBJECT_RECORD_NUMBER) {
							switch (record->
								ucRecordType) {
							case ATOM_I2C_RECORD_TYPE:
								i2c_record =
								    (ATOM_I2C_RECORD
								     *) record;
								line_mux =
								    i2c_record->
								    sucI2cId.
								    bfI2C_LineMux;
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
			} else
				line_mux = 0;

			if ((le16_to_cpu(path->usDeviceTag) ==
			     ATOM_DEVICE_TV1_SUPPORT)
			    || (le16_to_cpu(path->usDeviceTag) ==
				ATOM_DEVICE_TV2_SUPPORT)
			    || (le16_to_cpu(path->usDeviceTag) ==
				ATOM_DEVICE_CV_SUPPORT))
				ddc_bus.valid = false;
			else
				ddc_bus = radeon_lookup_gpio(dev, line_mux);

			conn_id = le16_to_cpu(path->usConnObjectId);

			if (!radeon_atom_apply_quirks
			    (dev, le16_to_cpu(path->usDeviceTag), &connector_type,
			     &ddc_bus, &conn_id))
				continue;

			radeon_add_atom_connector(dev,
						  conn_id,
						  le16_to_cpu(path->
							      usDeviceTag),
						  connector_type, &ddc_bus,
						  linkb, igp_lane_info,
						  connector_object_id);

		}
	}

	radeon_link_encoder_connector(dev);

	return true;
}

static uint16_t atombios_get_connector_object_id(struct drm_device *dev,
						 int connector_type,
						 uint16_t devices)
{
	struct radeon_device *rdev = dev->dev_private;

	if (rdev->flags & RADEON_IS_IGP) {
		return supported_devices_connector_object_id_convert
			[connector_type];
	} else if (((connector_type == DRM_MODE_CONNECTOR_DVII) ||
		    (connector_type == DRM_MODE_CONNECTOR_DVID)) &&
		   (devices & ATOM_DEVICE_DFP2_SUPPORT))  {
		struct radeon_mode_info *mode_info = &rdev->mode_info;
		struct atom_context *ctx = mode_info->atom_context;
		int index = GetIndexIntoMasterTable(DATA, XTMDS_Info);
		uint16_t size, data_offset;
		uint8_t frev, crev;
		ATOM_XTMDS_INFO *xtmds;

		atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset);
		xtmds = (ATOM_XTMDS_INFO *)(ctx->bios + data_offset);

		if (xtmds->ucSupportedLink & ATOM_XTMDS_SUPPORTED_DUALLINK) {
			if (connector_type == DRM_MODE_CONNECTOR_DVII)
				return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I;
			else
				return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D;
		} else {
			if (connector_type == DRM_MODE_CONNECTOR_DVII)
				return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I;
			else
				return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D;
		}
	} else {
		return supported_devices_connector_object_id_convert
			[connector_type];
	}
}

struct bios_connector {
	bool valid;
	uint16_t line_mux;
	uint16_t devices;
	int connector_type;
	struct radeon_i2c_bus_rec ddc_bus;
};

bool radeon_get_atom_connector_info_from_supported_devices_table(struct
								 drm_device
								 *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, SupportedDevicesInfo);
	uint16_t size, data_offset;
	uint8_t frev, crev;
	uint16_t device_support;
	uint8_t dac;
	union atom_supported_devices *supported_devices;
	int i, j;
	struct bios_connector bios_connectors[ATOM_MAX_SUPPORTED_DEVICE];

	atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset);

	supported_devices =
	    (union atom_supported_devices *)(ctx->bios + data_offset);

	device_support = le16_to_cpu(supported_devices->info.usDeviceSupport);

	for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
		ATOM_CONNECTOR_INFO_I2C ci =
		    supported_devices->info.asConnInfo[i];

		bios_connectors[i].valid = false;

		if (!(device_support & (1 << i))) {
			continue;
		}

		if (i == ATOM_DEVICE_CV_INDEX) {
			DRM_DEBUG("Skipping Component Video\n");
			continue;
		}

		bios_connectors[i].connector_type =
		    supported_devices_connector_convert[ci.sucConnectorInfo.
							sbfAccess.
							bfConnectorType];

		if (bios_connectors[i].connector_type ==
		    DRM_MODE_CONNECTOR_Unknown)
			continue;

		dac = ci.sucConnectorInfo.sbfAccess.bfAssociatedDAC;

		if ((rdev->family == CHIP_RS690) ||
		    (rdev->family == CHIP_RS740)) {
			if ((i == ATOM_DEVICE_DFP2_INDEX)
			    && (ci.sucI2cId.sbfAccess.bfI2C_LineMux == 2))
				bios_connectors[i].line_mux =
				    ci.sucI2cId.sbfAccess.bfI2C_LineMux + 1;
			else if ((i == ATOM_DEVICE_DFP3_INDEX)
				 && (ci.sucI2cId.sbfAccess.bfI2C_LineMux == 1))
				bios_connectors[i].line_mux =
				    ci.sucI2cId.sbfAccess.bfI2C_LineMux + 1;
			else
				bios_connectors[i].line_mux =
				    ci.sucI2cId.sbfAccess.bfI2C_LineMux;
		} else
			bios_connectors[i].line_mux =
			    ci.sucI2cId.sbfAccess.bfI2C_LineMux;

		/* give tv unique connector ids */
		if (i == ATOM_DEVICE_TV1_INDEX) {
			bios_connectors[i].ddc_bus.valid = false;
			bios_connectors[i].line_mux = 50;
		} else if (i == ATOM_DEVICE_TV2_INDEX) {
			bios_connectors[i].ddc_bus.valid = false;
			bios_connectors[i].line_mux = 51;
		} else if (i == ATOM_DEVICE_CV_INDEX) {
			bios_connectors[i].ddc_bus.valid = false;
			bios_connectors[i].line_mux = 52;
		} else
			bios_connectors[i].ddc_bus =
			    radeon_lookup_gpio(dev,
					       bios_connectors[i].line_mux);

		/* Always set the connector type to VGA for CRT1/CRT2. if they are
		 * shared with a DVI port, we'll pick up the DVI connector when we
		 * merge the outputs.  Some bioses incorrectly list VGA ports as DVI.
		 */
		if (i == ATOM_DEVICE_CRT1_INDEX || i == ATOM_DEVICE_CRT2_INDEX)
			bios_connectors[i].connector_type =
			    DRM_MODE_CONNECTOR_VGA;

		if (!radeon_atom_apply_quirks
		    (dev, (1 << i), &bios_connectors[i].connector_type,
		     &bios_connectors[i].ddc_bus, &bios_connectors[i].line_mux))
			continue;

		bios_connectors[i].valid = true;
		bios_connectors[i].devices = (1 << i);

		if (ASIC_IS_AVIVO(rdev) || radeon_r4xx_atom)
			radeon_add_atom_encoder(dev,
						radeon_get_encoder_id(dev,
								      (1 << i),
								      dac),
						(1 << i));
		else
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_id(dev,
									(1 <<
									 i),
									dac),
						  (1 << i));
	}

	/* combine shared connectors */
	for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
		if (bios_connectors[i].valid) {
			for (j = 0; j < ATOM_MAX_SUPPORTED_DEVICE; j++) {
				if (bios_connectors[j].valid && (i != j)) {
					if (bios_connectors[i].line_mux ==
					    bios_connectors[j].line_mux) {
						if (((bios_connectors[i].
						      devices &
						      (ATOM_DEVICE_DFP_SUPPORT))
						     && (bios_connectors[j].
							 devices &
							 (ATOM_DEVICE_CRT_SUPPORT)))
						    ||
						    ((bios_connectors[j].
						      devices &
						      (ATOM_DEVICE_DFP_SUPPORT))
						     && (bios_connectors[i].
							 devices &
							 (ATOM_DEVICE_CRT_SUPPORT)))) {
							bios_connectors[i].
							    devices |=
							    bios_connectors[j].
							    devices;
							bios_connectors[i].
							    connector_type =
							    DRM_MODE_CONNECTOR_DVII;
							bios_connectors[j].
							    valid = false;
						}
					}
				}
			}
		}
	}

	/* add the connectors */
	for (i = 0; i < ATOM_MAX_SUPPORTED_DEVICE; i++) {
		if (bios_connectors[i].valid) {
			uint16_t connector_object_id =
				atombios_get_connector_object_id(dev,
						      bios_connectors[i].connector_type,
						      bios_connectors[i].devices);
			radeon_add_atom_connector(dev,
						  bios_connectors[i].line_mux,
						  bios_connectors[i].devices,
						  bios_connectors[i].
						  connector_type,
						  &bios_connectors[i].ddc_bus,
						  false, 0,
						  connector_object_id);
		}
	}

	radeon_link_encoder_connector(dev);

	return true;
}

union firmware_info {
	ATOM_FIRMWARE_INFO info;
	ATOM_FIRMWARE_INFO_V1_2 info_12;
	ATOM_FIRMWARE_INFO_V1_3 info_13;
	ATOM_FIRMWARE_INFO_V1_4 info_14;
};

bool radeon_atom_get_clock_info(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	union firmware_info *firmware_info;
	uint8_t frev, crev;
	struct radeon_pll *p1pll = &rdev->clock.p1pll;
	struct radeon_pll *p2pll = &rdev->clock.p2pll;
	struct radeon_pll *spll = &rdev->clock.spll;
	struct radeon_pll *mpll = &rdev->clock.mpll;
	uint16_t data_offset;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev,
			       &crev, &data_offset);

	firmware_info =
	    (union firmware_info *)(mode_info->atom_context->bios +
				    data_offset);

	if (firmware_info) {
		/* pixel clocks */
		p1pll->reference_freq =
		    le16_to_cpu(firmware_info->info.usReferenceClock);
		p1pll->reference_div = 0;

		if (crev < 2)
			p1pll->pll_out_min =
				le16_to_cpu(firmware_info->info.usMinPixelClockPLL_Output);
		else
			p1pll->pll_out_min =
				le32_to_cpu(firmware_info->info_12.ulMinPixelClockPLL_Output);
		p1pll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxPixelClockPLL_Output);

		if (p1pll->pll_out_min == 0) {
			if (ASIC_IS_AVIVO(rdev))
				p1pll->pll_out_min = 64800;
			else
				p1pll->pll_out_min = 20000;
		} else if (p1pll->pll_out_min > 64800) {
			/* Limiting the pll output range is a good thing generally as
			 * it limits the number of possible pll combinations for a given
			 * frequency presumably to the ones that work best on each card.
			 * However, certain duallink DVI monitors seem to like
			 * pll combinations that would be limited by this at least on
			 * pre-DCE 3.0 r6xx hardware.  This might need to be adjusted per
			 * family.
			 */
			p1pll->pll_out_min = 64800;
		}

		p1pll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinPixelClockPLL_Input);
		p1pll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxPixelClockPLL_Input);

		*p2pll = *p1pll;

		/* system clock */
		spll->reference_freq =
		    le16_to_cpu(firmware_info->info.usReferenceClock);
		spll->reference_div = 0;

		spll->pll_out_min =
		    le16_to_cpu(firmware_info->info.usMinEngineClockPLL_Output);
		spll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxEngineClockPLL_Output);

		/* ??? */
		if (spll->pll_out_min == 0) {
			if (ASIC_IS_AVIVO(rdev))
				spll->pll_out_min = 64800;
			else
				spll->pll_out_min = 20000;
		}

		spll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinEngineClockPLL_Input);
		spll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxEngineClockPLL_Input);

		/* memory clock */
		mpll->reference_freq =
		    le16_to_cpu(firmware_info->info.usReferenceClock);
		mpll->reference_div = 0;

		mpll->pll_out_min =
		    le16_to_cpu(firmware_info->info.usMinMemoryClockPLL_Output);
		mpll->pll_out_max =
		    le32_to_cpu(firmware_info->info.ulMaxMemoryClockPLL_Output);

		/* ??? */
		if (mpll->pll_out_min == 0) {
			if (ASIC_IS_AVIVO(rdev))
				mpll->pll_out_min = 64800;
			else
				mpll->pll_out_min = 20000;
		}

		mpll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinMemoryClockPLL_Input);
		mpll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxMemoryClockPLL_Input);

		rdev->clock.default_sclk =
		    le32_to_cpu(firmware_info->info.ulDefaultEngineClock);
		rdev->clock.default_mclk =
		    le32_to_cpu(firmware_info->info.ulDefaultMemoryClock);

		return true;
	}
	return false;
}

bool radeon_atombios_get_tmds_info(struct radeon_encoder *encoder,
				   struct radeon_encoder_int_tmds *tmds)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, TMDS_Info);
	uint16_t data_offset;
	struct _ATOM_TMDS_INFO *tmds_info;
	uint8_t frev, crev;
	uint16_t maxfreq;
	int i;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev,
			       &crev, &data_offset);

	tmds_info =
	    (struct _ATOM_TMDS_INFO *)(mode_info->atom_context->bios +
				       data_offset);

	if (tmds_info) {
		maxfreq = le16_to_cpu(tmds_info->usMaxFrequency);
		for (i = 0; i < 4; i++) {
			tmds->tmds_pll[i].freq =
			    le16_to_cpu(tmds_info->asMiscInfo[i].usFrequency);
			tmds->tmds_pll[i].value =
			    tmds_info->asMiscInfo[i].ucPLL_ChargePump & 0x3f;
			tmds->tmds_pll[i].value |=
			    (tmds_info->asMiscInfo[i].
			     ucPLL_VCO_Gain & 0x3f) << 6;
			tmds->tmds_pll[i].value |=
			    (tmds_info->asMiscInfo[i].
			     ucPLL_DutyCycle & 0xf) << 12;
			tmds->tmds_pll[i].value |=
			    (tmds_info->asMiscInfo[i].
			     ucPLL_VoltageSwing & 0xf) << 16;

			DRM_DEBUG("TMDS PLL From ATOMBIOS %u %x\n",
				  tmds->tmds_pll[i].freq,
				  tmds->tmds_pll[i].value);

			if (maxfreq == tmds->tmds_pll[i].freq) {
				tmds->tmds_pll[i].freq = 0xffffffff;
				break;
			}
		}
		return true;
	}
	return false;
}

static struct radeon_atom_ss *radeon_atombios_get_ss_info(struct
							  radeon_encoder
							  *encoder,
							  int id)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, PPLL_SS_Info);
	uint16_t data_offset;
	struct _ATOM_SPREAD_SPECTRUM_INFO *ss_info;
	uint8_t frev, crev;
	struct radeon_atom_ss *ss = NULL;

	if (id > ATOM_MAX_SS_ENTRY)
		return NULL;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev,
			       &crev, &data_offset);

	ss_info =
	    (struct _ATOM_SPREAD_SPECTRUM_INFO *)(mode_info->atom_context->bios + data_offset);

	if (ss_info) {
		ss =
		    kzalloc(sizeof(struct radeon_atom_ss), GFP_KERNEL);

		if (!ss)
			return NULL;

		ss->percentage = le16_to_cpu(ss_info->asSS_Info[id].usSpreadSpectrumPercentage);
		ss->type = ss_info->asSS_Info[id].ucSpreadSpectrumType;
		ss->step = ss_info->asSS_Info[id].ucSS_Step;
		ss->delay = ss_info->asSS_Info[id].ucSS_Delay;
		ss->range = ss_info->asSS_Info[id].ucSS_Range;
		ss->refdiv = ss_info->asSS_Info[id].ucRecommendedRef_Div;
	}
	return ss;
}

union lvds_info {
	struct _ATOM_LVDS_INFO info;
	struct _ATOM_LVDS_INFO_V12 info_12;
};

struct radeon_encoder_atom_dig *radeon_atombios_get_lvds_info(struct
							      radeon_encoder
							      *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, LVDS_Info);
	uint16_t data_offset;
	union lvds_info *lvds_info;
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *lvds = NULL;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev,
			       &crev, &data_offset);

	lvds_info =
	    (union lvds_info *)(mode_info->atom_context->bios + data_offset);

	if (lvds_info) {
		lvds =
		    kzalloc(sizeof(struct radeon_encoder_atom_dig), GFP_KERNEL);

		if (!lvds)
			return NULL;

		lvds->native_mode.clock =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usPixClk) * 10;
		lvds->native_mode.hdisplay =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usHActive);
		lvds->native_mode.vdisplay =
		    le16_to_cpu(lvds_info->info.sLCDTiming.usVActive);
		lvds->native_mode.htotal = lvds->native_mode.hdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHBlanking_Time);
		lvds->native_mode.hsync_start = lvds->native_mode.hdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHSyncOffset);
		lvds->native_mode.hsync_end = lvds->native_mode.hsync_start +
			le16_to_cpu(lvds_info->info.sLCDTiming.usHSyncWidth);
		lvds->native_mode.vtotal = lvds->native_mode.vdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVBlanking_Time);
		lvds->native_mode.vsync_start = lvds->native_mode.vdisplay +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncWidth);
		lvds->native_mode.vsync_end = lvds->native_mode.vsync_start +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncWidth);
		lvds->panel_pwr_delay =
		    le16_to_cpu(lvds_info->info.usOffDelayInMs);
		lvds->lvds_misc = lvds_info->info.ucLVDS_Misc;
		/* set crtc values */
		drm_mode_set_crtcinfo(&lvds->native_mode, CRTC_INTERLACE_HALVE_V);

		lvds->ss = radeon_atombios_get_ss_info(encoder, lvds_info->info.ucSS_Id);

		encoder->native_mode = lvds->native_mode;
	}
	return lvds;
}

struct radeon_encoder_primary_dac *
radeon_atombios_get_primary_dac_info(struct radeon_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, CompassionateData);
	uint16_t data_offset;
	struct _COMPASSIONATE_DATA *dac_info;
	uint8_t frev, crev;
	uint8_t bg, dac;
	struct radeon_encoder_primary_dac *p_dac = NULL;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev, &crev, &data_offset);

	dac_info = (struct _COMPASSIONATE_DATA *)(mode_info->atom_context->bios + data_offset);

	if (dac_info) {
		p_dac = kzalloc(sizeof(struct radeon_encoder_primary_dac), GFP_KERNEL);

		if (!p_dac)
			return NULL;

		bg = dac_info->ucDAC1_BG_Adjustment;
		dac = dac_info->ucDAC1_DAC_Adjustment;
		p_dac->ps2_pdac_adj = (bg << 8) | (dac);

	}
	return p_dac;
}

bool radeon_atom_get_tv_timings(struct radeon_device *rdev, int index,
				struct drm_display_mode *mode)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	ATOM_ANALOG_TV_INFO *tv_info;
	ATOM_ANALOG_TV_INFO_V1_2 *tv_info_v1_2;
	ATOM_DTD_FORMAT *dtd_timings;
	int data_index = GetIndexIntoMasterTable(DATA, AnalogTV_Info);
	u8 frev, crev;
	u16 data_offset, misc;

	atom_parse_data_header(mode_info->atom_context, data_index, NULL, &frev, &crev, &data_offset);

	switch (crev) {
	case 1:
		tv_info = (ATOM_ANALOG_TV_INFO *)(mode_info->atom_context->bios + data_offset);
		if (index > MAX_SUPPORTED_TV_TIMING)
			return false;

		mode->crtc_htotal = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_H_Total);
		mode->crtc_hdisplay = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_H_Disp);
		mode->crtc_hsync_start = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_H_SyncStart);
		mode->crtc_hsync_end = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_H_SyncStart) +
			le16_to_cpu(tv_info->aModeTimings[index].usCRTC_H_SyncWidth);

		mode->crtc_vtotal = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_V_Total);
		mode->crtc_vdisplay = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_V_Disp);
		mode->crtc_vsync_start = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_V_SyncStart);
		mode->crtc_vsync_end = le16_to_cpu(tv_info->aModeTimings[index].usCRTC_V_SyncStart) +
			le16_to_cpu(tv_info->aModeTimings[index].usCRTC_V_SyncWidth);

		mode->flags = 0;
		misc = le16_to_cpu(tv_info->aModeTimings[index].susModeMiscInfo.usAccess);
		if (misc & ATOM_VSYNC_POLARITY)
			mode->flags |= DRM_MODE_FLAG_NVSYNC;
		if (misc & ATOM_HSYNC_POLARITY)
			mode->flags |= DRM_MODE_FLAG_NHSYNC;
		if (misc & ATOM_COMPOSITESYNC)
			mode->flags |= DRM_MODE_FLAG_CSYNC;
		if (misc & ATOM_INTERLACE)
			mode->flags |= DRM_MODE_FLAG_INTERLACE;
		if (misc & ATOM_DOUBLE_CLOCK_MODE)
			mode->flags |= DRM_MODE_FLAG_DBLSCAN;

		mode->clock = le16_to_cpu(tv_info->aModeTimings[index].usPixelClock) * 10;

		if (index == 1) {
			/* PAL timings appear to have wrong values for totals */
			mode->crtc_htotal -= 1;
			mode->crtc_vtotal -= 1;
		}
		break;
	case 2:
		tv_info_v1_2 = (ATOM_ANALOG_TV_INFO_V1_2 *)(mode_info->atom_context->bios + data_offset);
		if (index > MAX_SUPPORTED_TV_TIMING_V1_2)
			return false;

		dtd_timings = &tv_info_v1_2->aModeTimings[index];
		mode->crtc_htotal = le16_to_cpu(dtd_timings->usHActive) +
			le16_to_cpu(dtd_timings->usHBlanking_Time);
		mode->crtc_hdisplay = le16_to_cpu(dtd_timings->usHActive);
		mode->crtc_hsync_start = le16_to_cpu(dtd_timings->usHActive) +
			le16_to_cpu(dtd_timings->usHSyncOffset);
		mode->crtc_hsync_end = mode->crtc_hsync_start +
			le16_to_cpu(dtd_timings->usHSyncWidth);

		mode->crtc_vtotal = le16_to_cpu(dtd_timings->usVActive) +
			le16_to_cpu(dtd_timings->usVBlanking_Time);
		mode->crtc_vdisplay = le16_to_cpu(dtd_timings->usVActive);
		mode->crtc_vsync_start = le16_to_cpu(dtd_timings->usVActive) +
			le16_to_cpu(dtd_timings->usVSyncOffset);
		mode->crtc_vsync_end = mode->crtc_vsync_start +
			le16_to_cpu(dtd_timings->usVSyncWidth);

		mode->flags = 0;
		misc = le16_to_cpu(dtd_timings->susModeMiscInfo.usAccess);
		if (misc & ATOM_VSYNC_POLARITY)
			mode->flags |= DRM_MODE_FLAG_NVSYNC;
		if (misc & ATOM_HSYNC_POLARITY)
			mode->flags |= DRM_MODE_FLAG_NHSYNC;
		if (misc & ATOM_COMPOSITESYNC)
			mode->flags |= DRM_MODE_FLAG_CSYNC;
		if (misc & ATOM_INTERLACE)
			mode->flags |= DRM_MODE_FLAG_INTERLACE;
		if (misc & ATOM_DOUBLE_CLOCK_MODE)
			mode->flags |= DRM_MODE_FLAG_DBLSCAN;

		mode->clock = le16_to_cpu(dtd_timings->usPixClk) * 10;
		break;
	}
	return true;
}

struct radeon_encoder_tv_dac *
radeon_atombios_get_tv_dac_info(struct radeon_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, CompassionateData);
	uint16_t data_offset;
	struct _COMPASSIONATE_DATA *dac_info;
	uint8_t frev, crev;
	uint8_t bg, dac;
	struct radeon_encoder_tv_dac *tv_dac = NULL;

	atom_parse_data_header(mode_info->atom_context, index, NULL, &frev, &crev, &data_offset);

	dac_info = (struct _COMPASSIONATE_DATA *)(mode_info->atom_context->bios + data_offset);

	if (dac_info) {
		tv_dac = kzalloc(sizeof(struct radeon_encoder_tv_dac), GFP_KERNEL);

		if (!tv_dac)
			return NULL;

		bg = dac_info->ucDAC2_CRT2_BG_Adjustment;
		dac = dac_info->ucDAC2_CRT2_DAC_Adjustment;
		tv_dac->ps2_tvdac_adj = (bg << 16) | (dac << 20);

		bg = dac_info->ucDAC2_PAL_BG_Adjustment;
		dac = dac_info->ucDAC2_PAL_DAC_Adjustment;
		tv_dac->pal_tvdac_adj = (bg << 16) | (dac << 20);

		bg = dac_info->ucDAC2_NTSC_BG_Adjustment;
		dac = dac_info->ucDAC2_NTSC_DAC_Adjustment;
		tv_dac->ntsc_tvdac_adj = (bg << 16) | (dac << 20);

	}
	return tv_dac;
}

void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable)
{
	DYNAMIC_CLOCK_GATING_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, DynamicClockGating);

	args.ucEnable = enable;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_static_pwrmgt_setup(struct radeon_device *rdev, int enable)
{
	ENABLE_ASIC_STATIC_PWR_MGT_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, EnableASIC_StaticPwrMgt);

	args.ucEnable = enable;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_set_engine_clock(struct radeon_device *rdev,
				  uint32_t eng_clock)
{
	SET_ENGINE_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetEngineClock);

	args.ulTargetEngineClock = eng_clock;	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_set_memory_clock(struct radeon_device *rdev,
				  uint32_t mem_clock)
{
	SET_MEMORY_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetMemoryClock);

	if (rdev->flags & RADEON_IS_IGP)
		return;

	args.ulTargetMemoryClock = mem_clock;	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_initialize_bios_scratch_regs(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t bios_2_scratch, bios_6_scratch;

	if (rdev->family >= CHIP_R600) {
		bios_2_scratch = RREG32(R600_BIOS_2_SCRATCH);
		bios_6_scratch = RREG32(R600_BIOS_6_SCRATCH);
	} else {
		bios_2_scratch = RREG32(RADEON_BIOS_2_SCRATCH);
		bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);
	}

	/* let the bios control the backlight */
	bios_2_scratch &= ~ATOM_S2_VRI_BRIGHT_ENABLE;

	/* tell the bios not to handle mode switching */
	bios_6_scratch |= (ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH | ATOM_S6_ACC_MODE);

	if (rdev->family >= CHIP_R600) {
		WREG32(R600_BIOS_2_SCRATCH, bios_2_scratch);
		WREG32(R600_BIOS_6_SCRATCH, bios_6_scratch);
	} else {
		WREG32(RADEON_BIOS_2_SCRATCH, bios_2_scratch);
		WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
	}

}

void radeon_save_bios_scratch_regs(struct radeon_device *rdev)
{
	uint32_t scratch_reg;
	int i;

	if (rdev->family >= CHIP_R600)
		scratch_reg = R600_BIOS_0_SCRATCH;
	else
		scratch_reg = RADEON_BIOS_0_SCRATCH;

	for (i = 0; i < RADEON_BIOS_NUM_SCRATCH; i++)
		rdev->bios_scratch[i] = RREG32(scratch_reg + (i * 4));
}

void radeon_restore_bios_scratch_regs(struct radeon_device *rdev)
{
	uint32_t scratch_reg;
	int i;

	if (rdev->family >= CHIP_R600)
		scratch_reg = R600_BIOS_0_SCRATCH;
	else
		scratch_reg = RADEON_BIOS_0_SCRATCH;

	for (i = 0; i < RADEON_BIOS_NUM_SCRATCH; i++)
		WREG32(scratch_reg + (i * 4), rdev->bios_scratch[i]);
}

void radeon_atom_output_lock(struct drm_encoder *encoder, bool lock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t bios_6_scratch;

	if (rdev->family >= CHIP_R600)
		bios_6_scratch = RREG32(R600_BIOS_6_SCRATCH);
	else
		bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);

	if (lock)
		bios_6_scratch |= ATOM_S6_CRITICAL_STATE;
	else
		bios_6_scratch &= ~ATOM_S6_CRITICAL_STATE;

	if (rdev->family >= CHIP_R600)
		WREG32(R600_BIOS_6_SCRATCH, bios_6_scratch);
	else
		WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
}

/* at some point we may want to break this out into individual functions */
void
radeon_atombios_connected_scratch_regs(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       bool connected)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector =
	    to_radeon_connector(connector);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_0_scratch, bios_3_scratch, bios_6_scratch;

	if (rdev->family >= CHIP_R600) {
		bios_0_scratch = RREG32(R600_BIOS_0_SCRATCH);
		bios_3_scratch = RREG32(R600_BIOS_3_SCRATCH);
		bios_6_scratch = RREG32(R600_BIOS_6_SCRATCH);
	} else {
		bios_0_scratch = RREG32(RADEON_BIOS_0_SCRATCH);
		bios_3_scratch = RREG32(RADEON_BIOS_3_SCRATCH);
		bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);
	}

	if ((radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_TV1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("TV1 connected\n");
			bios_3_scratch |= ATOM_S3_TV1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_TV1;
		} else {
			DRM_DEBUG("TV1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_TV1_MASK;
			bios_3_scratch &= ~ATOM_S3_TV1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_TV1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CV_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("CV connected\n");
			bios_3_scratch |= ATOM_S3_CV_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CV;
		} else {
			DRM_DEBUG("CV disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CV_MASK;
			bios_3_scratch &= ~ATOM_S3_CV_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CV;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_LCD1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("LCD1 connected\n");
			bios_0_scratch |= ATOM_S0_LCD1;
			bios_3_scratch |= ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_LCD1;
		} else {
			DRM_DEBUG("LCD1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_LCD1;
			bios_3_scratch &= ~ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_LCD1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("CRT1 connected\n");
			bios_0_scratch |= ATOM_S0_CRT1_COLOR;
			bios_3_scratch |= ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT1;
		} else {
			DRM_DEBUG("CRT1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT1_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("CRT2 connected\n");
			bios_0_scratch |= ATOM_S0_CRT2_COLOR;
			bios_3_scratch |= ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT2;
		} else {
			DRM_DEBUG("CRT2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT2_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT2;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("DFP1 connected\n");
			bios_0_scratch |= ATOM_S0_DFP1;
			bios_3_scratch |= ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP1;
		} else {
			DRM_DEBUG("DFP1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP1;
			bios_3_scratch &= ~ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("DFP2 connected\n");
			bios_0_scratch |= ATOM_S0_DFP2;
			bios_3_scratch |= ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP2;
		} else {
			DRM_DEBUG("DFP2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP2;
			bios_3_scratch &= ~ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP2;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP3_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP3_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("DFP3 connected\n");
			bios_0_scratch |= ATOM_S0_DFP3;
			bios_3_scratch |= ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP3;
		} else {
			DRM_DEBUG("DFP3 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP3;
			bios_3_scratch &= ~ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP3;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP4_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP4_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("DFP4 connected\n");
			bios_0_scratch |= ATOM_S0_DFP4;
			bios_3_scratch |= ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP4;
		} else {
			DRM_DEBUG("DFP4 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP4;
			bios_3_scratch &= ~ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP4;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP5_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP5_SUPPORT)) {
		if (connected) {
			DRM_DEBUG("DFP5 connected\n");
			bios_0_scratch |= ATOM_S0_DFP5;
			bios_3_scratch |= ATOM_S3_DFP5_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP5;
		} else {
			DRM_DEBUG("DFP5 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP5;
			bios_3_scratch &= ~ATOM_S3_DFP5_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP5;
		}
	}

	if (rdev->family >= CHIP_R600) {
		WREG32(R600_BIOS_0_SCRATCH, bios_0_scratch);
		WREG32(R600_BIOS_3_SCRATCH, bios_3_scratch);
		WREG32(R600_BIOS_6_SCRATCH, bios_6_scratch);
	} else {
		WREG32(RADEON_BIOS_0_SCRATCH, bios_0_scratch);
		WREG32(RADEON_BIOS_3_SCRATCH, bios_3_scratch);
		WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
	}
}

void
radeon_atombios_encoder_crtc_scratch_regs(struct drm_encoder *encoder, int crtc)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_3_scratch;

	if (rdev->family >= CHIP_R600)
		bios_3_scratch = RREG32(R600_BIOS_3_SCRATCH);
	else
		bios_3_scratch = RREG32(RADEON_BIOS_3_SCRATCH);

	if (radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_TV1_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 18);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_CV_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 24);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_CRT1_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 16);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_CRT2_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 20);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_LCD1_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 17);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_DFP1_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 19);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_DFP2_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 23);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP3_SUPPORT) {
		bios_3_scratch &= ~ATOM_S3_DFP3_CRTC_ACTIVE;
		bios_3_scratch |= (crtc << 25);
	}

	if (rdev->family >= CHIP_R600)
		WREG32(R600_BIOS_3_SCRATCH, bios_3_scratch);
	else
		WREG32(RADEON_BIOS_3_SCRATCH, bios_3_scratch);
}

void
radeon_atombios_encoder_dpms_scratch_regs(struct drm_encoder *encoder, bool on)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_2_scratch;

	if (rdev->family >= CHIP_R600)
		bios_2_scratch = RREG32(R600_BIOS_2_SCRATCH);
	else
		bios_2_scratch = RREG32(RADEON_BIOS_2_SCRATCH);

	if (radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_TV1_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_TV1_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_CV_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_CV_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_CRT1_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_CRT1_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_CRT2_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_CRT2_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_LCD1_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_LCD1_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_DFP1_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_DFP1_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_DFP2_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_DFP2_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP3_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_DFP3_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_DFP3_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP4_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_DFP4_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_DFP4_DPMS_STATE;
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP5_SUPPORT) {
		if (on)
			bios_2_scratch &= ~ATOM_S2_DFP5_DPMS_STATE;
		else
			bios_2_scratch |= ATOM_S2_DFP5_DPMS_STATE;
	}

	if (rdev->family >= CHIP_R600)
		WREG32(R600_BIOS_2_SCRATCH, bios_2_scratch);
	else
		WREG32(RADEON_BIOS_2_SCRATCH, bios_2_scratch);
}
