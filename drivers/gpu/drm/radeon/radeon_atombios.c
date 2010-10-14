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
radeon_get_encoder_enum(struct drm_device *dev, uint32_t supported_device,
			uint8_t dac);
extern void radeon_link_encoder_connector(struct drm_device *dev);
extern void
radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_enum,
			uint32_t supported_device);

/* from radeon_connector.c */
extern void
radeon_add_atom_connector(struct drm_device *dev,
			  uint32_t connector_id,
			  uint32_t supported_device,
			  int connector_type,
			  struct radeon_i2c_bus_rec *i2c_bus,
			  uint32_t igp_lane_info,
			  uint16_t connector_object_id,
			  struct radeon_hpd *hpd,
			  struct radeon_router *router);

/* from radeon_legacy_encoder.c */
extern void
radeon_add_legacy_encoder(struct drm_device *dev, uint32_t encoder_enum,
			  uint32_t supported_device);

union atom_supported_devices {
	struct _ATOM_SUPPORTED_DEVICES_INFO info;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2 info_2;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2d1 info_2d1;
};

static inline struct radeon_i2c_bus_rec radeon_lookup_i2c_gpio(struct radeon_device *rdev,
							       uint8_t id)
{
	struct atom_context *ctx = rdev->mode_info.atom_context;
	ATOM_GPIO_I2C_ASSIGMENT *gpio;
	struct radeon_i2c_bus_rec i2c;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	struct _ATOM_GPIO_I2C_INFO *i2c_info;
	uint16_t data_offset, size;
	int i, num_indices;

	memset(&i2c, 0, sizeof(struct radeon_i2c_bus_rec));
	i2c.valid = false;

	if (atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_I2C_ASSIGMENT);

		for (i = 0; i < num_indices; i++) {
			gpio = &i2c_info->asGPIO_Info[i];

			/* some evergreen boards have bad data for this entry */
			if (ASIC_IS_DCE4(rdev)) {
				if ((i == 7) &&
				    (gpio->usClkMaskRegisterIndex == 0x1936) &&
				    (gpio->sucI2cId.ucAccess == 0)) {
					gpio->sucI2cId.ucAccess = 0x97;
					gpio->ucDataMaskShift = 8;
					gpio->ucDataEnShift = 8;
					gpio->ucDataY_Shift = 8;
					gpio->ucDataA_Shift = 8;
				}
			}

			if (gpio->sucI2cId.ucAccess == id) {
				i2c.mask_clk_reg = le16_to_cpu(gpio->usClkMaskRegisterIndex) * 4;
				i2c.mask_data_reg = le16_to_cpu(gpio->usDataMaskRegisterIndex) * 4;
				i2c.en_clk_reg = le16_to_cpu(gpio->usClkEnRegisterIndex) * 4;
				i2c.en_data_reg = le16_to_cpu(gpio->usDataEnRegisterIndex) * 4;
				i2c.y_clk_reg = le16_to_cpu(gpio->usClkY_RegisterIndex) * 4;
				i2c.y_data_reg = le16_to_cpu(gpio->usDataY_RegisterIndex) * 4;
				i2c.a_clk_reg = le16_to_cpu(gpio->usClkA_RegisterIndex) * 4;
				i2c.a_data_reg = le16_to_cpu(gpio->usDataA_RegisterIndex) * 4;
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
				break;
			}
		}
	}

	return i2c;
}

void radeon_atombios_i2c_init(struct radeon_device *rdev)
{
	struct atom_context *ctx = rdev->mode_info.atom_context;
	ATOM_GPIO_I2C_ASSIGMENT *gpio;
	struct radeon_i2c_bus_rec i2c;
	int index = GetIndexIntoMasterTable(DATA, GPIO_I2C_Info);
	struct _ATOM_GPIO_I2C_INFO *i2c_info;
	uint16_t data_offset, size;
	int i, num_indices;
	char stmp[32];

	memset(&i2c, 0, sizeof(struct radeon_i2c_bus_rec));

	if (atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_I2C_ASSIGMENT);

		for (i = 0; i < num_indices; i++) {
			gpio = &i2c_info->asGPIO_Info[i];
			i2c.valid = false;

			/* some evergreen boards have bad data for this entry */
			if (ASIC_IS_DCE4(rdev)) {
				if ((i == 7) &&
				    (gpio->usClkMaskRegisterIndex == 0x1936) &&
				    (gpio->sucI2cId.ucAccess == 0)) {
					gpio->sucI2cId.ucAccess = 0x97;
					gpio->ucDataMaskShift = 8;
					gpio->ucDataEnShift = 8;
					gpio->ucDataY_Shift = 8;
					gpio->ucDataA_Shift = 8;
				}
			}

			i2c.mask_clk_reg = le16_to_cpu(gpio->usClkMaskRegisterIndex) * 4;
			i2c.mask_data_reg = le16_to_cpu(gpio->usDataMaskRegisterIndex) * 4;
			i2c.en_clk_reg = le16_to_cpu(gpio->usClkEnRegisterIndex) * 4;
			i2c.en_data_reg = le16_to_cpu(gpio->usDataEnRegisterIndex) * 4;
			i2c.y_clk_reg = le16_to_cpu(gpio->usClkY_RegisterIndex) * 4;
			i2c.y_data_reg = le16_to_cpu(gpio->usDataY_RegisterIndex) * 4;
			i2c.a_clk_reg = le16_to_cpu(gpio->usClkA_RegisterIndex) * 4;
			i2c.a_data_reg = le16_to_cpu(gpio->usDataA_RegisterIndex) * 4;
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

			if (i2c.mask_clk_reg) {
				i2c.valid = true;
				sprintf(stmp, "0x%x", i2c.i2c_id);
				rdev->i2c_bus[i] = radeon_i2c_create(rdev->ddev, &i2c, stmp);
			}
		}
	}
}

static inline struct radeon_gpio_rec radeon_lookup_gpio(struct radeon_device *rdev,
							u8 id)
{
	struct atom_context *ctx = rdev->mode_info.atom_context;
	struct radeon_gpio_rec gpio;
	int index = GetIndexIntoMasterTable(DATA, GPIO_Pin_LUT);
	struct _ATOM_GPIO_PIN_LUT *gpio_info;
	ATOM_GPIO_PIN_ASSIGNMENT *pin;
	u16 data_offset, size;
	int i, num_indices;

	memset(&gpio, 0, sizeof(struct radeon_gpio_rec));
	gpio.valid = false;

	if (atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		gpio_info = (struct _ATOM_GPIO_PIN_LUT *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_PIN_ASSIGNMENT);

		for (i = 0; i < num_indices; i++) {
			pin = &gpio_info->asGPIO_Pin[i];
			if (id == pin->ucGPIO_ID) {
				gpio.id = pin->ucGPIO_ID;
				gpio.reg = pin->usGpioPin_AIndex * 4;
				gpio.mask = (1 << pin->ucGpioPinBitShift);
				gpio.valid = true;
				break;
			}
		}
	}

	return gpio;
}

static struct radeon_hpd radeon_atom_get_hpd_info_from_gpio(struct radeon_device *rdev,
							    struct radeon_gpio_rec *gpio)
{
	struct radeon_hpd hpd;
	u32 reg;

	memset(&hpd, 0, sizeof(struct radeon_hpd));

	if (ASIC_IS_DCE4(rdev))
		reg = EVERGREEN_DC_GPIO_HPD_A;
	else
		reg = AVIVO_DC_GPIO_HPD_A;

	hpd.gpio = *gpio;
	if (gpio->reg == reg) {
		switch(gpio->mask) {
		case (1 << 0):
			hpd.hpd = RADEON_HPD_1;
			break;
		case (1 << 8):
			hpd.hpd = RADEON_HPD_2;
			break;
		case (1 << 16):
			hpd.hpd = RADEON_HPD_3;
			break;
		case (1 << 24):
			hpd.hpd = RADEON_HPD_4;
			break;
		case (1 << 26):
			hpd.hpd = RADEON_HPD_5;
			break;
		case (1 << 28):
			hpd.hpd = RADEON_HPD_6;
			break;
		default:
			hpd.hpd = RADEON_HPD_NONE;
			break;
		}
	} else
		hpd.hpd = RADEON_HPD_NONE;
	return hpd;
}

static bool radeon_atom_apply_quirks(struct drm_device *dev,
				     uint32_t supported_device,
				     int *connector_type,
				     struct radeon_i2c_bus_rec *i2c_bus,
				     uint16_t *line_mux,
				     struct radeon_hpd *hpd)
{
	struct radeon_device *rdev = dev->dev_private;

	/* Asus M2A-VM HDMI board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x791e) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x826d)) {
		if ((*connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
		    (supported_device == ATOM_DEVICE_DFP3_SUPPORT))
			*connector_type = DRM_MODE_CONNECTOR_DVID;
	}

	/* Asrock RS600 board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x7941) &&
	    (dev->pdev->subsystem_vendor == 0x1849) &&
	    (dev->pdev->subsystem_device == 0x7941)) {
		if ((*connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
		    (supported_device == ATOM_DEVICE_DFP3_SUPPORT))
			*connector_type = DRM_MODE_CONNECTOR_DVID;
	}

	/* MSI K9A2GM V2/V3 board has no HDMI or DVI */
	if ((dev->pdev->device == 0x796e) &&
	    (dev->pdev->subsystem_vendor == 0x1462) &&
	    (dev->pdev->subsystem_device == 0x7302)) {
		if ((supported_device == ATOM_DEVICE_DFP2_SUPPORT) ||
		    (supported_device == ATOM_DEVICE_DFP3_SUPPORT))
			return false;
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

	/* HIS X1300 is DVI+VGA, not DVI+DVI */
	if ((dev->pdev->device == 0x7146) &&
	    (dev->pdev->subsystem_vendor == 0x17af) &&
	    (dev->pdev->subsystem_device == 0x2058)) {
		if (supported_device == ATOM_DEVICE_DFP1_SUPPORT)
			return false;
	}

	/* Gigabyte X1300 is DVI+VGA, not DVI+DVI */
	if ((dev->pdev->device == 0x7142) &&
	    (dev->pdev->subsystem_vendor == 0x1458) &&
	    (dev->pdev->subsystem_device == 0x2134)) {
		if (supported_device == ATOM_DEVICE_DFP1_SUPPORT)
			return false;
	}


	/* Funky macbooks */
	if ((dev->pdev->device == 0x71C5) &&
	    (dev->pdev->subsystem_vendor == 0x106b) &&
	    (dev->pdev->subsystem_device == 0x0080)) {
		if ((supported_device == ATOM_DEVICE_CRT1_SUPPORT) ||
		    (supported_device == ATOM_DEVICE_DFP2_SUPPORT))
			return false;
		if (supported_device == ATOM_DEVICE_CRT2_SUPPORT)
			*line_mux = 0x90;
	}

	/* ASUS HD 3600 XT board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x9598) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x01da)) {
		if (*connector_type == DRM_MODE_CONNECTOR_HDMIA) {
			*connector_type = DRM_MODE_CONNECTOR_DVII;
		}
	}

	/* ASUS HD 3600 board lists the DVI port as HDMI */
	if ((dev->pdev->device == 0x9598) &&
	    (dev->pdev->subsystem_vendor == 0x1043) &&
	    (dev->pdev->subsystem_device == 0x01e4)) {
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

	/* Acer laptop reports DVI-D as DVI-I and hpd pins reversed */
	if ((dev->pdev->device == 0x95c4) &&
	    (dev->pdev->subsystem_vendor == 0x1025) &&
	    (dev->pdev->subsystem_device == 0x013c)) {
		struct radeon_gpio_rec gpio;

		if ((*connector_type == DRM_MODE_CONNECTOR_DVII) &&
		    (supported_device == ATOM_DEVICE_DFP1_SUPPORT)) {
			gpio = radeon_lookup_gpio(rdev, 6);
			*hpd = radeon_atom_get_hpd_info_from_gpio(rdev, &gpio);
			*connector_type = DRM_MODE_CONNECTOR_DVID;
		} else if ((*connector_type == DRM_MODE_CONNECTOR_HDMIA) &&
			   (supported_device == ATOM_DEVICE_DFP1_SUPPORT)) {
			gpio = radeon_lookup_gpio(rdev, 7);
			*hpd = radeon_atom_get_hpd_info_from_gpio(rdev, &gpio);
		}
	}

	/* XFX Pine Group device rv730 reports no VGA DDC lines
	 * even though they are wired up to record 0x93
	 */
	if ((dev->pdev->device == 0x9498) &&
	    (dev->pdev->subsystem_vendor == 0x1682) &&
	    (dev->pdev->subsystem_device == 0x2452)) {
		struct radeon_device *rdev = dev->dev_private;
		*i2c_bus = radeon_lookup_i2c_gpio(rdev, 0x93);
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
	DRM_MODE_CONNECTOR_DisplayPort,
	DRM_MODE_CONNECTOR_eDP,
	DRM_MODE_CONNECTOR_Unknown
};

bool radeon_get_atom_connector_info_from_object_table(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct atom_context *ctx = mode_info->atom_context;
	int index = GetIndexIntoMasterTable(DATA, Object_Header);
	u16 size, data_offset;
	u8 frev, crev;
	ATOM_CONNECTOR_OBJECT_TABLE *con_obj;
	ATOM_OBJECT_TABLE *router_obj;
	ATOM_DISPLAY_OBJECT_PATH_TABLE *path_obj;
	ATOM_OBJECT_HEADER *obj_header;
	int i, j, k, path_size, device_support;
	int connector_type;
	u16 igp_lane_info, conn_id, connector_object_id;
	struct radeon_i2c_bus_rec ddc_bus;
	struct radeon_router router;
	struct radeon_gpio_rec gpio;
	struct radeon_hpd hpd;

	if (!atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset))
		return false;

	if (crev < 2)
		return false;

	router.valid = false;

	obj_header = (ATOM_OBJECT_HEADER *) (ctx->bios + data_offset);
	path_obj = (ATOM_DISPLAY_OBJECT_PATH_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usDisplayPathTableOffset));
	con_obj = (ATOM_CONNECTOR_OBJECT_TABLE *)
	    (ctx->bios + data_offset +
	     le16_to_cpu(obj_header->usConnectorObjectTableOffset));
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

				if (atom_parse_data_header(ctx, index, &size, &frev,
							   &crev, &igp_offset)) {

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
			} else {
				igp_lane_info = 0;
				connector_type =
				    object_connector_convert[con_obj_id];
				connector_object_id = con_obj_id;
			}

			if (connector_type == DRM_MODE_CONNECTOR_Unknown)
				continue;

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
					u16 encoder_obj = le16_to_cpu(path->usGraphicObjIds[j]);

					radeon_add_atom_encoder(dev,
								encoder_obj,
								le16_to_cpu
								(path->
								 usDeviceTag));

				} else if (grph_obj_type == GRAPH_OBJECT_TYPE_ROUTER) {
					router.valid = false;
					for (k = 0; k < router_obj->ucNumberOfObjects; k++) {
						u16 router_obj_id = le16_to_cpu(router_obj->asObjects[j].usObjectID);
						if (le16_to_cpu(path->usGraphicObjIds[j]) == router_obj_id) {
							ATOM_COMMON_RECORD_HEADER *record = (ATOM_COMMON_RECORD_HEADER *)
								(ctx->bios + data_offset +
								 le16_to_cpu(router_obj->asObjects[k].usRecordOffset));
							ATOM_I2C_RECORD *i2c_record;
							ATOM_I2C_ID_CONFIG_ACCESS *i2c_config;
							ATOM_ROUTER_DDC_PATH_SELECT_RECORD *ddc_path;
							ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *router_src_dst_table =
								(ATOM_SRC_DST_TABLE_FOR_ONE_OBJECT *)
								(ctx->bios + data_offset +
								 le16_to_cpu(router_obj->asObjects[k].usSrcDstTableOffset));
							int enum_id;

							router.router_id = router_obj_id;
							for (enum_id = 0; enum_id < router_src_dst_table->ucNumberOfDst;
							     enum_id++) {
								if (le16_to_cpu(path->usConnObjectId) ==
								    le16_to_cpu(router_src_dst_table->usDstObjectID[enum_id]))
									break;
							}

							while (record->ucRecordType > 0 &&
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
										radeon_lookup_i2c_gpio(rdev,
												       i2c_config->
												       ucAccess);
									router.i2c_addr = i2c_record->ucI2CAddr >> 1;
									break;
								case ATOM_ROUTER_DDC_PATH_SELECT_RECORD_TYPE:
									ddc_path = (ATOM_ROUTER_DDC_PATH_SELECT_RECORD *)
										record;
									router.valid = true;
									router.mux_type = ddc_path->ucMuxType;
									router.mux_control_pin = ddc_path->ucMuxControlPin;
									router.mux_state = ddc_path->ucMuxState[enum_id];
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
			hpd.hpd = RADEON_HPD_NONE;
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

						while (record->ucRecordType > 0
						       && record->
						       ucRecordType <=
						       ATOM_MAX_OBJECT_RECORD_NUMBER) {
							switch (record->ucRecordType) {
							case ATOM_I2C_RECORD_TYPE:
								i2c_record =
								    (ATOM_I2C_RECORD *)
									record;
								i2c_config =
									(ATOM_I2C_ID_CONFIG_ACCESS *)
									&i2c_record->sucI2cId;
								ddc_bus = radeon_lookup_i2c_gpio(rdev,
												 i2c_config->
												 ucAccess);
								break;
							case ATOM_HPD_INT_RECORD_TYPE:
								hpd_record =
									(ATOM_HPD_INT_RECORD *)
									record;
								gpio = radeon_lookup_gpio(rdev,
											  hpd_record->ucHPDIntGPIOID);
								hpd = radeon_atom_get_hpd_info_from_gpio(rdev, &gpio);
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

			if (!radeon_atom_apply_quirks
			    (dev, le16_to_cpu(path->usDeviceTag), &connector_type,
			     &ddc_bus, &conn_id, &hpd))
				continue;

			radeon_add_atom_connector(dev,
						  conn_id,
						  le16_to_cpu(path->
							      usDeviceTag),
						  connector_type, &ddc_bus,
						  igp_lane_info,
						  connector_object_id,
						  &hpd,
						  &router);

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

		if (atom_parse_data_header(ctx, index, &size, &frev, &crev, &data_offset)) {
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
		} else
			return supported_devices_connector_object_id_convert
				[connector_type];
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
	struct radeon_hpd hpd;
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
	int i, j, max_device;
	struct bios_connector *bios_connectors;
	size_t bc_size = sizeof(*bios_connectors) * ATOM_MAX_SUPPORTED_DEVICE;
	struct radeon_router router;

	router.valid = false;

	bios_connectors = kzalloc(bc_size, GFP_KERNEL);
	if (!bios_connectors)
		return false;

	if (!atom_parse_data_header(ctx, index, &size, &frev, &crev,
				    &data_offset)) {
		kfree(bios_connectors);
		return false;
	}

	supported_devices =
	    (union atom_supported_devices *)(ctx->bios + data_offset);

	device_support = le16_to_cpu(supported_devices->info.usDeviceSupport);

	if (frev > 1)
		max_device = ATOM_MAX_SUPPORTED_DEVICE;
	else
		max_device = ATOM_MAX_SUPPORTED_DEVICE_INFO;

	for (i = 0; i < max_device; i++) {
		ATOM_CONNECTOR_INFO_I2C ci =
		    supported_devices->info.asConnInfo[i];

		bios_connectors[i].valid = false;

		if (!(device_support & (1 << i))) {
			continue;
		}

		if (i == ATOM_DEVICE_CV_INDEX) {
			DRM_DEBUG_KMS("Skipping Component Video\n");
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

		bios_connectors[i].line_mux =
			ci.sucI2cId.ucAccess;

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
			    radeon_lookup_i2c_gpio(rdev,
						   bios_connectors[i].line_mux);

		if ((crev > 1) && (frev > 1)) {
			u8 isb = supported_devices->info_2d1.asIntSrcInfo[i].ucIntSrcBitmap;
			switch (isb) {
			case 0x4:
				bios_connectors[i].hpd.hpd = RADEON_HPD_1;
				break;
			case 0xa:
				bios_connectors[i].hpd.hpd = RADEON_HPD_2;
				break;
			default:
				bios_connectors[i].hpd.hpd = RADEON_HPD_NONE;
				break;
			}
		} else {
			if (i == ATOM_DEVICE_DFP1_INDEX)
				bios_connectors[i].hpd.hpd = RADEON_HPD_1;
			else if (i == ATOM_DEVICE_DFP2_INDEX)
				bios_connectors[i].hpd.hpd = RADEON_HPD_2;
			else
				bios_connectors[i].hpd.hpd = RADEON_HPD_NONE;
		}

		/* Always set the connector type to VGA for CRT1/CRT2. if they are
		 * shared with a DVI port, we'll pick up the DVI connector when we
		 * merge the outputs.  Some bioses incorrectly list VGA ports as DVI.
		 */
		if (i == ATOM_DEVICE_CRT1_INDEX || i == ATOM_DEVICE_CRT2_INDEX)
			bios_connectors[i].connector_type =
			    DRM_MODE_CONNECTOR_VGA;

		if (!radeon_atom_apply_quirks
		    (dev, (1 << i), &bios_connectors[i].connector_type,
		     &bios_connectors[i].ddc_bus, &bios_connectors[i].line_mux,
		     &bios_connectors[i].hpd))
			continue;

		bios_connectors[i].valid = true;
		bios_connectors[i].devices = (1 << i);

		if (ASIC_IS_AVIVO(rdev) || radeon_r4xx_atom)
			radeon_add_atom_encoder(dev,
						radeon_get_encoder_enum(dev,
								      (1 << i),
								      dac),
						(1 << i));
		else
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									(1 << i),
									dac),
						  (1 << i));
	}

	/* combine shared connectors */
	for (i = 0; i < max_device; i++) {
		if (bios_connectors[i].valid) {
			for (j = 0; j < max_device; j++) {
				if (bios_connectors[j].valid && (i != j)) {
					if (bios_connectors[i].line_mux ==
					    bios_connectors[j].line_mux) {
						/* make sure not to combine LVDS */
						if (bios_connectors[i].devices & (ATOM_DEVICE_LCD_SUPPORT)) {
							bios_connectors[i].line_mux = 53;
							bios_connectors[i].ddc_bus.valid = false;
							continue;
						}
						if (bios_connectors[j].devices & (ATOM_DEVICE_LCD_SUPPORT)) {
							bios_connectors[j].line_mux = 53;
							bios_connectors[j].ddc_bus.valid = false;
							continue;
						}
						/* combine analog and digital for DVI-I */
						if (((bios_connectors[i].devices & (ATOM_DEVICE_DFP_SUPPORT)) &&
						     (bios_connectors[j].devices & (ATOM_DEVICE_CRT_SUPPORT))) ||
						    ((bios_connectors[j].devices & (ATOM_DEVICE_DFP_SUPPORT)) &&
						     (bios_connectors[i].devices & (ATOM_DEVICE_CRT_SUPPORT)))) {
							bios_connectors[i].devices |=
								bios_connectors[j].devices;
							bios_connectors[i].connector_type =
								DRM_MODE_CONNECTOR_DVII;
							if (bios_connectors[j].devices & (ATOM_DEVICE_DFP_SUPPORT))
								bios_connectors[i].hpd =
									bios_connectors[j].hpd;
							bios_connectors[j].valid = false;
						}
					}
				}
			}
		}
	}

	/* add the connectors */
	for (i = 0; i < max_device; i++) {
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
						  0,
						  connector_object_id,
						  &bios_connectors[i].hpd,
						  &router);
		}
	}

	radeon_link_encoder_connector(dev);

	kfree(bios_connectors);
	return true;
}

union firmware_info {
	ATOM_FIRMWARE_INFO info;
	ATOM_FIRMWARE_INFO_V1_2 info_12;
	ATOM_FIRMWARE_INFO_V1_3 info_13;
	ATOM_FIRMWARE_INFO_V1_4 info_14;
	ATOM_FIRMWARE_INFO_V2_1 info_21;
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
	struct radeon_pll *dcpll = &rdev->clock.dcpll;
	struct radeon_pll *spll = &rdev->clock.spll;
	struct radeon_pll *mpll = &rdev->clock.mpll;
	uint16_t data_offset;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		firmware_info =
			(union firmware_info *)(mode_info->atom_context->bios +
						data_offset);
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

		if (crev >= 4) {
			p1pll->lcd_pll_out_min =
				le16_to_cpu(firmware_info->info_14.usLcdMinPixelClockPLL_Output) * 100;
			if (p1pll->lcd_pll_out_min == 0)
				p1pll->lcd_pll_out_min = p1pll->pll_out_min;
			p1pll->lcd_pll_out_max =
				le16_to_cpu(firmware_info->info_14.usLcdMaxPixelClockPLL_Output) * 100;
			if (p1pll->lcd_pll_out_max == 0)
				p1pll->lcd_pll_out_max = p1pll->pll_out_max;
		} else {
			p1pll->lcd_pll_out_min = p1pll->pll_out_min;
			p1pll->lcd_pll_out_max = p1pll->pll_out_max;
		}

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
			if (!radeon_new_pll)
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

		if (ASIC_IS_DCE4(rdev)) {
			rdev->clock.default_dispclk =
				le32_to_cpu(firmware_info->info_21.ulDefaultDispEngineClkFreq);
			if (rdev->clock.default_dispclk == 0)
				rdev->clock.default_dispclk = 60000; /* 600 Mhz */
			rdev->clock.dp_extclk =
				le16_to_cpu(firmware_info->info_21.usUniphyDPModeExtClkFreq);
		}
		*dcpll = *p1pll;

		return true;
	}

	return false;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
};

bool radeon_atombios_sideport_present(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	union igp_info *igp_info;
	u8 frev, crev;
	u16 data_offset;

	/* sideport is AMD only */
	if (rdev->family == CHIP_RS600)
		return false;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		igp_info = (union igp_info *)(mode_info->atom_context->bios +
				      data_offset);
		switch (crev) {
		case 1:
			if (igp_info->info.ulBootUpMemoryClock)
				return true;
			break;
		case 2:
			if (igp_info->info_2.ulBootUpSidePortClock)
				return true;
			break;
		default:
			DRM_ERROR("Unsupported IGP table: %d %d\n", frev, crev);
			break;
		}
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

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		tmds_info =
			(struct _ATOM_TMDS_INFO *)(mode_info->atom_context->bios +
						   data_offset);

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

			DRM_DEBUG_KMS("TMDS PLL From ATOMBIOS %u %x\n",
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
	int i;

	if (id > ATOM_MAX_SS_ENTRY)
		return NULL;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		ss_info =
			(struct _ATOM_SPREAD_SPECTRUM_INFO *)(mode_info->atom_context->bios + data_offset);

		ss =
		    kzalloc(sizeof(struct radeon_atom_ss), GFP_KERNEL);

		if (!ss)
			return NULL;

		for (i = 0; i < ATOM_MAX_SS_ENTRY; i++) {
			if (ss_info->asSS_Info[i].ucSS_Id == id) {
				ss->percentage =
					le16_to_cpu(ss_info->asSS_Info[i].usSpreadSpectrumPercentage);
				ss->type = ss_info->asSS_Info[i].ucSpreadSpectrumType;
				ss->step = ss_info->asSS_Info[i].ucSS_Step;
				ss->delay = ss_info->asSS_Info[i].ucSS_Delay;
				ss->range = ss_info->asSS_Info[i].ucSS_Range;
				ss->refdiv = ss_info->asSS_Info[i].ucRecommendedRef_Div;
				break;
			}
		}
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
	uint16_t data_offset, misc;
	union lvds_info *lvds_info;
	uint8_t frev, crev;
	struct radeon_encoder_atom_dig *lvds = NULL;
	int encoder_enum = (encoder->encoder_enum & ENUM_ID_MASK) >> ENUM_ID_SHIFT;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		lvds_info =
			(union lvds_info *)(mode_info->atom_context->bios + data_offset);
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
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncOffset);
		lvds->native_mode.vsync_end = lvds->native_mode.vsync_start +
			le16_to_cpu(lvds_info->info.sLCDTiming.usVSyncWidth);
		lvds->panel_pwr_delay =
		    le16_to_cpu(lvds_info->info.usOffDelayInMs);
		lvds->lvds_misc = lvds_info->info.ucLVDS_Misc;

		misc = le16_to_cpu(lvds_info->info.sLCDTiming.susModeMiscInfo.usAccess);
		if (misc & ATOM_VSYNC_POLARITY)
			lvds->native_mode.flags |= DRM_MODE_FLAG_NVSYNC;
		if (misc & ATOM_HSYNC_POLARITY)
			lvds->native_mode.flags |= DRM_MODE_FLAG_NHSYNC;
		if (misc & ATOM_COMPOSITESYNC)
			lvds->native_mode.flags |= DRM_MODE_FLAG_CSYNC;
		if (misc & ATOM_INTERLACE)
			lvds->native_mode.flags |= DRM_MODE_FLAG_INTERLACE;
		if (misc & ATOM_DOUBLE_CLOCK_MODE)
			lvds->native_mode.flags |= DRM_MODE_FLAG_DBLSCAN;

		/* set crtc values */
		drm_mode_set_crtcinfo(&lvds->native_mode, CRTC_INTERLACE_HALVE_V);

		lvds->ss = radeon_atombios_get_ss_info(encoder, lvds_info->info.ucSS_Id);

		if (ASIC_IS_AVIVO(rdev)) {
			if (radeon_new_pll == 0)
				lvds->pll_algo = PLL_ALGO_LEGACY;
			else
				lvds->pll_algo = PLL_ALGO_NEW;
		} else {
			if (radeon_new_pll == 1)
				lvds->pll_algo = PLL_ALGO_NEW;
			else
				lvds->pll_algo = PLL_ALGO_LEGACY;
		}

		encoder->native_mode = lvds->native_mode;

		if (encoder_enum == 2)
			lvds->linkb = true;
		else
			lvds->linkb = false;

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

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		dac_info = (struct _COMPASSIONATE_DATA *)
			(mode_info->atom_context->bios + data_offset);

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

	if (!atom_parse_data_header(mode_info->atom_context, data_index, NULL,
				    &frev, &crev, &data_offset))
		return false;

	switch (crev) {
	case 1:
		tv_info = (ATOM_ANALOG_TV_INFO *)(mode_info->atom_context->bios + data_offset);
		if (index >= MAX_SUPPORTED_TV_TIMING)
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
		if (index >= MAX_SUPPORTED_TV_TIMING_V1_2)
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

enum radeon_tv_std
radeon_atombios_get_tv_info(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, AnalogTV_Info);
	uint16_t data_offset;
	uint8_t frev, crev;
	struct _ATOM_ANALOG_TV_INFO *tv_info;
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {

		tv_info = (struct _ATOM_ANALOG_TV_INFO *)
			(mode_info->atom_context->bios + data_offset);

		switch (tv_info->ucTV_BootUpDefaultStandard) {
		case ATOM_TV_NTSC:
			tv_std = TV_STD_NTSC;
			DRM_INFO("Default TV standard: NTSC\n");
			break;
		case ATOM_TV_NTSCJ:
			tv_std = TV_STD_NTSC_J;
			DRM_INFO("Default TV standard: NTSC-J\n");
			break;
		case ATOM_TV_PAL:
			tv_std = TV_STD_PAL;
			DRM_INFO("Default TV standard: PAL\n");
			break;
		case ATOM_TV_PALM:
			tv_std = TV_STD_PAL_M;
			DRM_INFO("Default TV standard: PAL-M\n");
			break;
		case ATOM_TV_PALN:
			tv_std = TV_STD_PAL_N;
			DRM_INFO("Default TV standard: PAL-N\n");
			break;
		case ATOM_TV_PALCN:
			tv_std = TV_STD_PAL_CN;
			DRM_INFO("Default TV standard: PAL-CN\n");
			break;
		case ATOM_TV_PAL60:
			tv_std = TV_STD_PAL_60;
			DRM_INFO("Default TV standard: PAL-60\n");
			break;
		case ATOM_TV_SECAM:
			tv_std = TV_STD_SECAM;
			DRM_INFO("Default TV standard: SECAM\n");
			break;
		default:
			tv_std = TV_STD_NTSC;
			DRM_INFO("Unknown TV standard; defaulting to NTSC\n");
			break;
		}
	}
	return tv_std;
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

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {

		dac_info = (struct _COMPASSIONATE_DATA *)
			(mode_info->atom_context->bios + data_offset);

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

		tv_dac->tv_std = radeon_atombios_get_tv_info(rdev);
	}
	return tv_dac;
}

static const char *thermal_controller_names[] = {
	"NONE",
	"lm63",
	"adm1032",
	"adm1030",
	"max6649",
	"lm64",
	"f75375",
	"asc7xxx",
};

static const char *pp_lib_thermal_controller_names[] = {
	"NONE",
	"lm63",
	"adm1032",
	"adm1030",
	"max6649",
	"lm64",
	"f75375",
	"RV6xx",
	"RV770",
	"adt7473",
	"External GPIO",
	"Evergreen",
	"adt7473 with internal",

};

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE info_4;
};

void radeon_atombios_get_power_modes(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	u16 data_offset;
	u8 frev, crev;
	u32 misc, misc2 = 0, sclk, mclk;
	union power_info *power_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	struct _ATOM_PPLIB_STATE *power_state;
	int num_modes = 0, i, j;
	int state_index = 0, mode_index = 0;
	struct radeon_i2c_bus_rec i2c_bus;

	rdev->pm.default_power_state_index = -1;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);
		if (frev < 4) {
			/* add the i2c bus for thermal/fan chip */
			if (power_info->info.ucOverdriveThermalController > 0) {
				DRM_INFO("Possible %s thermal controller at 0x%02x\n",
					 thermal_controller_names[power_info->info.ucOverdriveThermalController],
					 power_info->info.ucOverdriveControllerAddress >> 1);
				i2c_bus = radeon_lookup_i2c_gpio(rdev, power_info->info.ucOverdriveI2cLine);
				rdev->pm.i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
				if (rdev->pm.i2c_bus) {
					struct i2c_board_info info = { };
					const char *name = thermal_controller_names[power_info->info.
										    ucOverdriveThermalController];
					info.addr = power_info->info.ucOverdriveControllerAddress >> 1;
					strlcpy(info.type, name, sizeof(info.type));
					i2c_new_device(&rdev->pm.i2c_bus->adapter, &info);
				}
			}
			num_modes = power_info->info.ucNumOfPowerModeEntries;
			if (num_modes > ATOM_MAX_NUMBEROF_POWER_BLOCK)
				num_modes = ATOM_MAX_NUMBEROF_POWER_BLOCK;
			/* last mode is usually default, array is low to high */
			for (i = 0; i < num_modes; i++) {
				rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_NONE;
				switch (frev) {
				case 1:
					rdev->pm.power_state[state_index].num_clock_modes = 1;
					rdev->pm.power_state[state_index].clock_info[0].mclk =
						le16_to_cpu(power_info->info.asPowerPlayInfo[i].usMemoryClock);
					rdev->pm.power_state[state_index].clock_info[0].sclk =
						le16_to_cpu(power_info->info.asPowerPlayInfo[i].usEngineClock);
					/* skip invalid modes */
					if ((rdev->pm.power_state[state_index].clock_info[0].mclk == 0) ||
					    (rdev->pm.power_state[state_index].clock_info[0].sclk == 0))
						continue;
					rdev->pm.power_state[state_index].pcie_lanes =
						power_info->info.asPowerPlayInfo[i].ucNumPciELanes;
					misc = le32_to_cpu(power_info->info.asPowerPlayInfo[i].ulMiscInfo);
					if ((misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_SUPPORT) ||
					    (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_GPIO;
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio =
							radeon_lookup_gpio(rdev,
							power_info->info.asPowerPlayInfo[i].ucVoltageDropIndex);
						if (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								true;
						else
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								false;
					} else if (misc & ATOM_PM_MISCINFO_PROGRAM_VOLTAGE) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_VDDC;
						rdev->pm.power_state[state_index].clock_info[0].voltage.vddc_id =
							power_info->info.asPowerPlayInfo[i].ucVoltageDropIndex;
					}
					rdev->pm.power_state[state_index].flags = RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					rdev->pm.power_state[state_index].misc = misc;
					/* order matters! */
					if (misc & ATOM_PM_MISCINFO_POWER_SAVING_MODE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_POWERSAVE;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_LOW_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_LOAD_BALANCE_EN)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
					if (misc & ATOM_PM_MISCINFO_3D_ACCELERATION_EN) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_PERFORMANCE;
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					}
					if (misc & ATOM_PM_MISCINFO_DRIVER_DEFAULT_MODE) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_DEFAULT;
						rdev->pm.default_power_state_index = state_index;
						rdev->pm.power_state[state_index].default_clock_mode =
							&rdev->pm.power_state[state_index].clock_info[0];
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					} else if (state_index == 0) {
						rdev->pm.power_state[state_index].clock_info[0].flags |=
							RADEON_PM_MODE_NO_DISPLAY;
					}
					state_index++;
					break;
				case 2:
					rdev->pm.power_state[state_index].num_clock_modes = 1;
					rdev->pm.power_state[state_index].clock_info[0].mclk =
						le32_to_cpu(power_info->info_2.asPowerPlayInfo[i].ulMemoryClock);
					rdev->pm.power_state[state_index].clock_info[0].sclk =
						le32_to_cpu(power_info->info_2.asPowerPlayInfo[i].ulEngineClock);
					/* skip invalid modes */
					if ((rdev->pm.power_state[state_index].clock_info[0].mclk == 0) ||
					    (rdev->pm.power_state[state_index].clock_info[0].sclk == 0))
						continue;
					rdev->pm.power_state[state_index].pcie_lanes =
						power_info->info_2.asPowerPlayInfo[i].ucNumPciELanes;
					misc = le32_to_cpu(power_info->info_2.asPowerPlayInfo[i].ulMiscInfo);
					misc2 = le32_to_cpu(power_info->info_2.asPowerPlayInfo[i].ulMiscInfo2);
					if ((misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_SUPPORT) ||
					    (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_GPIO;
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio =
							radeon_lookup_gpio(rdev,
							power_info->info_2.asPowerPlayInfo[i].ucVoltageDropIndex);
						if (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								true;
						else
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								false;
					} else if (misc & ATOM_PM_MISCINFO_PROGRAM_VOLTAGE) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_VDDC;
						rdev->pm.power_state[state_index].clock_info[0].voltage.vddc_id =
							power_info->info_2.asPowerPlayInfo[i].ucVoltageDropIndex;
					}
					rdev->pm.power_state[state_index].flags = RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					rdev->pm.power_state[state_index].misc = misc;
					rdev->pm.power_state[state_index].misc2 = misc2;
					/* order matters! */
					if (misc & ATOM_PM_MISCINFO_POWER_SAVING_MODE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_POWERSAVE;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_LOW_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_LOAD_BALANCE_EN)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
					if (misc & ATOM_PM_MISCINFO_3D_ACCELERATION_EN) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_PERFORMANCE;
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					}
					if (misc2 & ATOM_PM_MISCINFO2_SYSTEM_AC_LITE_MODE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
					if (misc2 & ATOM_PM_MISCINFO2_MULTI_DISPLAY_SUPPORT)
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					if (misc & ATOM_PM_MISCINFO_DRIVER_DEFAULT_MODE) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_DEFAULT;
						rdev->pm.default_power_state_index = state_index;
						rdev->pm.power_state[state_index].default_clock_mode =
							&rdev->pm.power_state[state_index].clock_info[0];
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					} else if (state_index == 0) {
						rdev->pm.power_state[state_index].clock_info[0].flags |=
							RADEON_PM_MODE_NO_DISPLAY;
					}
					state_index++;
					break;
				case 3:
					rdev->pm.power_state[state_index].num_clock_modes = 1;
					rdev->pm.power_state[state_index].clock_info[0].mclk =
						le32_to_cpu(power_info->info_3.asPowerPlayInfo[i].ulMemoryClock);
					rdev->pm.power_state[state_index].clock_info[0].sclk =
						le32_to_cpu(power_info->info_3.asPowerPlayInfo[i].ulEngineClock);
					/* skip invalid modes */
					if ((rdev->pm.power_state[state_index].clock_info[0].mclk == 0) ||
					    (rdev->pm.power_state[state_index].clock_info[0].sclk == 0))
						continue;
					rdev->pm.power_state[state_index].pcie_lanes =
						power_info->info_3.asPowerPlayInfo[i].ucNumPciELanes;
					misc = le32_to_cpu(power_info->info_3.asPowerPlayInfo[i].ulMiscInfo);
					misc2 = le32_to_cpu(power_info->info_3.asPowerPlayInfo[i].ulMiscInfo2);
					if ((misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_SUPPORT) ||
					    (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_GPIO;
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio =
							radeon_lookup_gpio(rdev,
							power_info->info_3.asPowerPlayInfo[i].ucVoltageDropIndex);
						if (misc & ATOM_PM_MISCINFO_VOLTAGE_DROP_ACTIVE_HIGH)
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								true;
						else
							rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
								false;
					} else if (misc & ATOM_PM_MISCINFO_PROGRAM_VOLTAGE) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.type =
							VOLTAGE_VDDC;
						rdev->pm.power_state[state_index].clock_info[0].voltage.vddc_id =
							power_info->info_3.asPowerPlayInfo[i].ucVoltageDropIndex;
						if (misc2 & ATOM_PM_MISCINFO2_VDDCI_DYNAMIC_VOLTAGE_EN) {
							rdev->pm.power_state[state_index].clock_info[0].voltage.vddci_enabled =
								true;
							rdev->pm.power_state[state_index].clock_info[0].voltage.vddci_id =
							power_info->info_3.asPowerPlayInfo[i].ucVDDCI_VoltageDropIndex;
						}
					}
					rdev->pm.power_state[state_index].flags = RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					rdev->pm.power_state[state_index].misc = misc;
					rdev->pm.power_state[state_index].misc2 = misc2;
					/* order matters! */
					if (misc & ATOM_PM_MISCINFO_POWER_SAVING_MODE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_POWERSAVE;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_DEFAULT_LOW_DC_STATE_ENTRY_TRUE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
					if (misc & ATOM_PM_MISCINFO_LOAD_BALANCE_EN)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
					if (misc & ATOM_PM_MISCINFO_3D_ACCELERATION_EN) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_PERFORMANCE;
						rdev->pm.power_state[state_index].flags &=
							~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					}
					if (misc2 & ATOM_PM_MISCINFO2_SYSTEM_AC_LITE_MODE)
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
					if (misc & ATOM_PM_MISCINFO_DRIVER_DEFAULT_MODE) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_DEFAULT;
						rdev->pm.default_power_state_index = state_index;
						rdev->pm.power_state[state_index].default_clock_mode =
							&rdev->pm.power_state[state_index].clock_info[0];
					} else if (state_index == 0) {
						rdev->pm.power_state[state_index].clock_info[0].flags |=
							RADEON_PM_MODE_NO_DISPLAY;
					}
					state_index++;
					break;
				}
			}
			/* last mode is usually default */
			if (rdev->pm.default_power_state_index == -1) {
				rdev->pm.power_state[state_index - 1].type =
					POWER_STATE_TYPE_DEFAULT;
				rdev->pm.default_power_state_index = state_index - 1;
				rdev->pm.power_state[state_index - 1].default_clock_mode =
					&rdev->pm.power_state[state_index - 1].clock_info[0];
				rdev->pm.power_state[state_index].flags &=
					~RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
				rdev->pm.power_state[state_index].misc = 0;
				rdev->pm.power_state[state_index].misc2 = 0;
			}
		} else {
			int fw_index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
			uint8_t fw_frev, fw_crev;
			uint16_t fw_data_offset, vddc = 0;
			union firmware_info *firmware_info;
			ATOM_PPLIB_THERMALCONTROLLER *controller = &power_info->info_4.sThermalController;

			if (atom_parse_data_header(mode_info->atom_context, fw_index, NULL,
						   &fw_frev, &fw_crev, &fw_data_offset)) {
				firmware_info =
					(union firmware_info *)(mode_info->atom_context->bios +
								fw_data_offset);
				vddc = firmware_info->info_14.usBootUpVDDCVoltage;
			}

			/* add the i2c bus for thermal/fan chip */
			if (controller->ucType > 0) {
				if (controller->ucType == ATOM_PP_THERMALCONTROLLER_RV6xx) {
					DRM_INFO("Internal thermal controller %s fan control\n",
						 (controller->ucFanParameters &
						  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
					rdev->pm.int_thermal_type = THERMAL_TYPE_RV6XX;
				} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_RV770) {
					DRM_INFO("Internal thermal controller %s fan control\n",
						 (controller->ucFanParameters &
						  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
					rdev->pm.int_thermal_type = THERMAL_TYPE_RV770;
				} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_EVERGREEN) {
					DRM_INFO("Internal thermal controller %s fan control\n",
						 (controller->ucFanParameters &
						  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
					rdev->pm.int_thermal_type = THERMAL_TYPE_EVERGREEN;
				} else if ((controller->ucType ==
					    ATOM_PP_THERMALCONTROLLER_EXTERNAL_GPIO) ||
					   (controller->ucType ==
					    ATOM_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL)) {
					DRM_INFO("Special thermal controller config\n");
				} else {
					DRM_INFO("Possible %s thermal controller at 0x%02x %s fan control\n",
						 pp_lib_thermal_controller_names[controller->ucType],
						 controller->ucI2cAddress >> 1,
						 (controller->ucFanParameters &
						  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
					i2c_bus = radeon_lookup_i2c_gpio(rdev, controller->ucI2cLine);
					rdev->pm.i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
					if (rdev->pm.i2c_bus) {
						struct i2c_board_info info = { };
						const char *name = pp_lib_thermal_controller_names[controller->ucType];
						info.addr = controller->ucI2cAddress >> 1;
						strlcpy(info.type, name, sizeof(info.type));
						i2c_new_device(&rdev->pm.i2c_bus->adapter, &info);
					}

				}
			}
			/* first mode is usually default, followed by low to high */
			for (i = 0; i < power_info->info_4.ucNumStates; i++) {
				mode_index = 0;
				power_state = (struct _ATOM_PPLIB_STATE *)
					(mode_info->atom_context->bios +
					 data_offset +
					 le16_to_cpu(power_info->info_4.usStateArrayOffset) +
					 i * power_info->info_4.ucStateEntrySize);
				non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
					(mode_info->atom_context->bios +
					 data_offset +
					 le16_to_cpu(power_info->info_4.usNonClockInfoArrayOffset) +
					 (power_state->ucNonClockStateIndex *
					  power_info->info_4.ucNonClockSize));
				for (j = 0; j < (power_info->info_4.ucStateEntrySize - 1); j++) {
					if (rdev->flags & RADEON_IS_IGP) {
						struct _ATOM_PPLIB_RS780_CLOCK_INFO *clock_info =
							(struct _ATOM_PPLIB_RS780_CLOCK_INFO *)
							(mode_info->atom_context->bios +
							 data_offset +
							 le16_to_cpu(power_info->info_4.usClockInfoArrayOffset) +
							 (power_state->ucClockStateIndices[j] *
							  power_info->info_4.ucClockInfoSize));
						sclk = le16_to_cpu(clock_info->usLowEngineClockLow);
						sclk |= clock_info->ucLowEngineClockHigh << 16;
						rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
						/* skip invalid modes */
						if (rdev->pm.power_state[state_index].clock_info[mode_index].sclk == 0)
							continue;
						/* voltage works differently on IGPs */
						mode_index++;
					} else if (ASIC_IS_DCE4(rdev)) {
						struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO *clock_info =
							(struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO *)
							(mode_info->atom_context->bios +
							 data_offset +
							 le16_to_cpu(power_info->info_4.usClockInfoArrayOffset) +
							 (power_state->ucClockStateIndices[j] *
							  power_info->info_4.ucClockInfoSize));
						sclk = le16_to_cpu(clock_info->usEngineClockLow);
						sclk |= clock_info->ucEngineClockHigh << 16;
						mclk = le16_to_cpu(clock_info->usMemoryClockLow);
						mclk |= clock_info->ucMemoryClockHigh << 16;
						rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
						rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
						/* skip invalid modes */
						if ((rdev->pm.power_state[state_index].clock_info[mode_index].mclk == 0) ||
						    (rdev->pm.power_state[state_index].clock_info[mode_index].sclk == 0))
							continue;
						rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
							VOLTAGE_SW;
						rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage =
							clock_info->usVDDC;
						/* XXX usVDDCI */
						mode_index++;
					} else {
						struct _ATOM_PPLIB_R600_CLOCK_INFO *clock_info =
							(struct _ATOM_PPLIB_R600_CLOCK_INFO *)
							(mode_info->atom_context->bios +
							 data_offset +
							 le16_to_cpu(power_info->info_4.usClockInfoArrayOffset) +
							 (power_state->ucClockStateIndices[j] *
							  power_info->info_4.ucClockInfoSize));
						sclk = le16_to_cpu(clock_info->usEngineClockLow);
						sclk |= clock_info->ucEngineClockHigh << 16;
						mclk = le16_to_cpu(clock_info->usMemoryClockLow);
						mclk |= clock_info->ucMemoryClockHigh << 16;
						rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
						rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
						/* skip invalid modes */
						if ((rdev->pm.power_state[state_index].clock_info[mode_index].mclk == 0) ||
						    (rdev->pm.power_state[state_index].clock_info[mode_index].sclk == 0))
							continue;
						rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
							VOLTAGE_SW;
						rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage =
							clock_info->usVDDC;
						mode_index++;
					}
				}
				rdev->pm.power_state[state_index].num_clock_modes = mode_index;
				if (mode_index) {
					misc = le32_to_cpu(non_clock_info->ulCapsAndSettings);
					misc2 = le16_to_cpu(non_clock_info->usClassification);
					rdev->pm.power_state[state_index].misc = misc;
					rdev->pm.power_state[state_index].misc2 = misc2;
					rdev->pm.power_state[state_index].pcie_lanes =
						((misc & ATOM_PPLIB_PCIE_LINK_WIDTH_MASK) >>
						ATOM_PPLIB_PCIE_LINK_WIDTH_SHIFT) + 1;
					switch (misc2 & ATOM_PPLIB_CLASSIFICATION_UI_MASK) {
					case ATOM_PPLIB_CLASSIFICATION_UI_BATTERY:
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BATTERY;
						break;
					case ATOM_PPLIB_CLASSIFICATION_UI_BALANCED:
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_BALANCED;
						break;
					case ATOM_PPLIB_CLASSIFICATION_UI_PERFORMANCE:
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_PERFORMANCE;
						break;
					case ATOM_PPLIB_CLASSIFICATION_UI_NONE:
						if (misc2 & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
							rdev->pm.power_state[state_index].type =
								POWER_STATE_TYPE_PERFORMANCE;
						break;
					}
					rdev->pm.power_state[state_index].flags = 0;
					if (misc & ATOM_PPLIB_SINGLE_DISPLAY_ONLY)
						rdev->pm.power_state[state_index].flags |=
							RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
					if (misc2 & ATOM_PPLIB_CLASSIFICATION_BOOT) {
						rdev->pm.power_state[state_index].type =
							POWER_STATE_TYPE_DEFAULT;
						rdev->pm.default_power_state_index = state_index;
						rdev->pm.power_state[state_index].default_clock_mode =
							&rdev->pm.power_state[state_index].clock_info[mode_index - 1];
						/* patch the table values with the default slck/mclk from firmware info */
						for (j = 0; j < mode_index; j++) {
							rdev->pm.power_state[state_index].clock_info[j].mclk =
								rdev->clock.default_mclk;
							rdev->pm.power_state[state_index].clock_info[j].sclk =
								rdev->clock.default_sclk;
							if (vddc)
								rdev->pm.power_state[state_index].clock_info[j].voltage.voltage =
									vddc;
						}
					}
					state_index++;
				}
			}
			/* if multiple clock modes, mark the lowest as no display */
			for (i = 0; i < state_index; i++) {
				if (rdev->pm.power_state[i].num_clock_modes > 1)
					rdev->pm.power_state[i].clock_info[0].flags |=
						RADEON_PM_MODE_NO_DISPLAY;
			}
			/* first mode is usually default */
			if (rdev->pm.default_power_state_index == -1) {
				rdev->pm.power_state[0].type =
					POWER_STATE_TYPE_DEFAULT;
				rdev->pm.default_power_state_index = 0;
				rdev->pm.power_state[0].default_clock_mode =
					&rdev->pm.power_state[0].clock_info[0];
			}
		}
	} else {
		/* add the default mode */
		rdev->pm.power_state[state_index].type =
			POWER_STATE_TYPE_DEFAULT;
		rdev->pm.power_state[state_index].num_clock_modes = 1;
		rdev->pm.power_state[state_index].clock_info[0].mclk = rdev->clock.default_mclk;
		rdev->pm.power_state[state_index].clock_info[0].sclk = rdev->clock.default_sclk;
		rdev->pm.power_state[state_index].default_clock_mode =
			&rdev->pm.power_state[state_index].clock_info[0];
		rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_NONE;
		rdev->pm.power_state[state_index].pcie_lanes = 16;
		rdev->pm.default_power_state_index = state_index;
		rdev->pm.power_state[state_index].flags = 0;
		state_index++;
	}

	rdev->pm.num_power_states = state_index;

	rdev->pm.current_power_state_index = rdev->pm.default_power_state_index;
	rdev->pm.current_clock_mode_index = 0;
	rdev->pm.current_vddc = rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.voltage;
}

void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable)
{
	DYNAMIC_CLOCK_GATING_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, DynamicClockGating);

	args.ucEnable = enable;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

uint32_t radeon_atom_get_engine_clock(struct radeon_device *rdev)
{
	GET_ENGINE_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, GetEngineClock);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	return args.ulReturnEngineClock;
}

uint32_t radeon_atom_get_memory_clock(struct radeon_device *rdev)
{
	GET_MEMORY_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, GetMemoryClock);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	return args.ulReturnMemoryClock;
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

union set_voltage {
	struct _SET_VOLTAGE_PS_ALLOCATION alloc;
	struct _SET_VOLTAGE_PARAMETERS v1;
	struct _SET_VOLTAGE_PARAMETERS_V2 v2;
};

void radeon_atom_set_voltage(struct radeon_device *rdev, u16 level)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev, volt_index = level;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	switch (crev) {
	case 1:
		args.v1.ucVoltageType = SET_VOLTAGE_TYPE_ASIC_VDDC;
		args.v1.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_ALL_SOURCE;
		args.v1.ucVoltageIndex = volt_index;
		break;
	case 2:
		args.v2.ucVoltageType = SET_VOLTAGE_TYPE_ASIC_VDDC;
		args.v2.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_SET_VOLTAGE;
		args.v2.usVoltageLevel = cpu_to_le16(level);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return;
	}

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
			DRM_DEBUG_KMS("TV1 connected\n");
			bios_3_scratch |= ATOM_S3_TV1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_TV1;
		} else {
			DRM_DEBUG_KMS("TV1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_TV1_MASK;
			bios_3_scratch &= ~ATOM_S3_TV1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_TV1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CV_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CV_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CV connected\n");
			bios_3_scratch |= ATOM_S3_CV_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CV;
		} else {
			DRM_DEBUG_KMS("CV disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CV_MASK;
			bios_3_scratch &= ~ATOM_S3_CV_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CV;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_LCD1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("LCD1 connected\n");
			bios_0_scratch |= ATOM_S0_LCD1;
			bios_3_scratch |= ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_LCD1;
		} else {
			DRM_DEBUG_KMS("LCD1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_LCD1;
			bios_3_scratch &= ~ATOM_S3_LCD1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_LCD1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT1 connected\n");
			bios_0_scratch |= ATOM_S0_CRT1_COLOR;
			bios_3_scratch |= ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT1;
		} else {
			DRM_DEBUG_KMS("CRT1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT1_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT2 connected\n");
			bios_0_scratch |= ATOM_S0_CRT2_COLOR;
			bios_3_scratch |= ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_CRT2;
		} else {
			DRM_DEBUG_KMS("CRT2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_CRT2_MASK;
			bios_3_scratch &= ~ATOM_S3_CRT2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_CRT2;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP1 connected\n");
			bios_0_scratch |= ATOM_S0_DFP1;
			bios_3_scratch |= ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP1;
		} else {
			DRM_DEBUG_KMS("DFP1 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP1;
			bios_3_scratch &= ~ATOM_S3_DFP1_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP2 connected\n");
			bios_0_scratch |= ATOM_S0_DFP2;
			bios_3_scratch |= ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP2;
		} else {
			DRM_DEBUG_KMS("DFP2 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP2;
			bios_3_scratch &= ~ATOM_S3_DFP2_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP2;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP3_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP3_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP3 connected\n");
			bios_0_scratch |= ATOM_S0_DFP3;
			bios_3_scratch |= ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP3;
		} else {
			DRM_DEBUG_KMS("DFP3 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP3;
			bios_3_scratch &= ~ATOM_S3_DFP3_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP3;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP4_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP4_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP4 connected\n");
			bios_0_scratch |= ATOM_S0_DFP4;
			bios_3_scratch |= ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP4;
		} else {
			DRM_DEBUG_KMS("DFP4 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP4;
			bios_3_scratch &= ~ATOM_S3_DFP4_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP4;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP5_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP5_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP5 connected\n");
			bios_0_scratch |= ATOM_S0_DFP5;
			bios_3_scratch |= ATOM_S3_DFP5_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP5;
		} else {
			DRM_DEBUG_KMS("DFP5 disconnected\n");
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
