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
#include <drm/radeon_drm.h>
#include "radeon.h"

#include "atom.h"
#include "atom-bits.h"

extern void
radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_enum,
			uint32_t supported_device, u16 caps);

/* from radeon_legacy_encoder.c */
extern void
radeon_add_legacy_encoder(struct drm_device *dev, uint32_t encoder_enum,
			  uint32_t supported_device);

union atom_supported_devices {
	struct _ATOM_SUPPORTED_DEVICES_INFO info;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2 info_2;
	struct _ATOM_SUPPORTED_DEVICES_INFO_2d1 info_2d1;
};

static void radeon_lookup_i2c_gpio_quirks(struct radeon_device *rdev,
					  ATOM_GPIO_I2C_ASSIGMENT *gpio,
					  u8 index)
{
	/* r4xx mask is technically not used by the hw, so patch in the legacy mask bits */
	if ((rdev->family == CHIP_R420) ||
	    (rdev->family == CHIP_R423) ||
	    (rdev->family == CHIP_RV410)) {
		if ((le16_to_cpu(gpio->usClkMaskRegisterIndex) == 0x0018) ||
		    (le16_to_cpu(gpio->usClkMaskRegisterIndex) == 0x0019) ||
		    (le16_to_cpu(gpio->usClkMaskRegisterIndex) == 0x001a)) {
			gpio->ucClkMaskShift = 0x19;
			gpio->ucDataMaskShift = 0x18;
		}
	}

	/* some evergreen boards have bad data for this entry */
	if (ASIC_IS_DCE4(rdev)) {
		if ((index == 7) &&
		    (le16_to_cpu(gpio->usClkMaskRegisterIndex) == 0x1936) &&
		    (gpio->sucI2cId.ucAccess == 0)) {
			gpio->sucI2cId.ucAccess = 0x97;
			gpio->ucDataMaskShift = 8;
			gpio->ucDataEnShift = 8;
			gpio->ucDataY_Shift = 8;
			gpio->ucDataA_Shift = 8;
		}
	}

	/* some DCE3 boards have bad data for this entry */
	if (ASIC_IS_DCE3(rdev)) {
		if ((index == 4) &&
		    (le16_to_cpu(gpio->usClkMaskRegisterIndex) == 0x1fda) &&
		    (gpio->sucI2cId.ucAccess == 0x94))
			gpio->sucI2cId.ucAccess = 0x14;
	}
}

static struct radeon_i2c_bus_rec radeon_get_bus_rec_for_i2c_gpio(ATOM_GPIO_I2C_ASSIGMENT *gpio)
{
	struct radeon_i2c_bus_rec i2c;

	memset(&i2c, 0, sizeof(struct radeon_i2c_bus_rec));

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
	else
		i2c.valid = false;

	return i2c;
}

static struct radeon_i2c_bus_rec radeon_lookup_i2c_gpio(struct radeon_device *rdev,
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

		gpio = &i2c_info->asGPIO_Info[0];
		for (i = 0; i < num_indices; i++) {

			radeon_lookup_i2c_gpio_quirks(rdev, gpio, i);

			if (gpio->sucI2cId.ucAccess == id) {
				i2c = radeon_get_bus_rec_for_i2c_gpio(gpio);
				break;
			}
			gpio = (ATOM_GPIO_I2C_ASSIGMENT *)
				((u8 *)gpio + sizeof(ATOM_GPIO_I2C_ASSIGMENT));
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

	if (atom_parse_data_header(ctx, index, &size, NULL, NULL, &data_offset)) {
		i2c_info = (struct _ATOM_GPIO_I2C_INFO *)(ctx->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_GPIO_I2C_ASSIGMENT);

		gpio = &i2c_info->asGPIO_Info[0];
		for (i = 0; i < num_indices; i++) {
			radeon_lookup_i2c_gpio_quirks(rdev, gpio, i);

			i2c = radeon_get_bus_rec_for_i2c_gpio(gpio);

			if (i2c.valid) {
				sprintf(stmp, "0x%x", i2c.i2c_id);
				rdev->i2c_bus[i] = radeon_i2c_create(rdev->ddev, &i2c, stmp);
			}
			gpio = (ATOM_GPIO_I2C_ASSIGMENT *)
				((u8 *)gpio + sizeof(ATOM_GPIO_I2C_ASSIGMENT));
		}
	}
}

static struct radeon_gpio_rec radeon_lookup_gpio(struct radeon_device *rdev,
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

		pin = gpio_info->asGPIO_Pin;
		for (i = 0; i < num_indices; i++) {
			if (id == pin->ucGPIO_ID) {
				gpio.id = pin->ucGPIO_ID;
				gpio.reg = le16_to_cpu(pin->usGpioPin_AIndex) * 4;
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

static struct radeon_hpd radeon_atom_get_hpd_info_from_gpio(struct radeon_device *rdev,
							    struct radeon_gpio_rec *gpio)
{
	struct radeon_hpd hpd;
	u32 reg;

	memset(&hpd, 0, sizeof(struct radeon_hpd));

	if (ASIC_IS_DCE6(rdev))
		reg = SI_DC_GPIO_HPD_A;
	else if (ASIC_IS_DCE4(rdev))
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

	/* mac rv630, rv730, others */
	if ((supported_device == ATOM_DEVICE_TV1_SUPPORT) &&
	    (*connector_type == DRM_MODE_CONNECTOR_DVII)) {
		*connector_type = DRM_MODE_CONNECTOR_9PinDIN;
		*line_mux = CONNECTOR_7PIN_DIN_ENUM_ID1;
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

	/* Acer laptop (Acer TravelMate 5730/5730G) has an HDMI port
	 * on the laptop and a DVI port on the docking station and
	 * both share the same encoder, hpd pin, and ddc line.
	 * So while the bios table is technically correct,
	 * we drop the DVI port here since xrandr has no concept of
	 * encoders and will try and drive both connectors
	 * with different crtcs which isn't possible on the hardware
	 * side and leaves no crtcs for LVDS or VGA.
	 */
	if (((dev->pdev->device == 0x95c4) || (dev->pdev->device == 0x9591)) &&
	    (dev->pdev->subsystem_vendor == 0x1025) &&
	    (dev->pdev->subsystem_device == 0x013c)) {
		if ((*connector_type == DRM_MODE_CONNECTOR_DVII) &&
		    (supported_device == ATOM_DEVICE_DFP1_SUPPORT)) {
			/* actually it's a DVI-D port not DVI-I */
			*connector_type = DRM_MODE_CONNECTOR_DVID;
			return false;
		}
	}

	/* XFX Pine Group device rv730 reports no VGA DDC lines
	 * even though they are wired up to record 0x93
	 */
	if ((dev->pdev->device == 0x9498) &&
	    (dev->pdev->subsystem_vendor == 0x1682) &&
	    (dev->pdev->subsystem_device == 0x2452) &&
	    (i2c_bus->valid == false) &&
	    !(supported_device & (ATOM_DEVICE_TV_SUPPORT | ATOM_DEVICE_CV_SUPPORT))) {
		struct radeon_device *rdev = dev->dev_private;
		*i2c_bus = radeon_lookup_i2c_gpio(rdev, 0x93);
	}

	/* Fujitsu D3003-S2 board lists DVI-I as DVI-D and VGA */
	if (((dev->pdev->device == 0x9802) || (dev->pdev->device == 0x9806)) &&
	    (dev->pdev->subsystem_vendor == 0x1734) &&
	    (dev->pdev->subsystem_device == 0x11bd)) {
		if (*connector_type == DRM_MODE_CONNECTOR_VGA) {
			*connector_type = DRM_MODE_CONNECTOR_DVII;
			*line_mux = 0x3103;
		} else if (*connector_type == DRM_MODE_CONNECTOR_DVID) {
			*connector_type = DRM_MODE_CONNECTOR_DVII;
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
	ATOM_ENCODER_OBJECT_TABLE *enc_obj;
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
							radeon_add_atom_encoder(dev,
										encoder_obj,
										le16_to_cpu
										(path->
										 usDeviceTag),
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
										radeon_lookup_i2c_gpio(rdev,
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

	router.ddc_valid = false;
	router.cd_valid = false;

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
						(1 << i),
						0);
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
	ATOM_FIRMWARE_INFO_V2_2 info_22;
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
		}

		p1pll->pll_in_min =
		    le16_to_cpu(firmware_info->info.usMinPixelClockPLL_Input);
		p1pll->pll_in_max =
		    le16_to_cpu(firmware_info->info.usMaxPixelClockPLL_Input);

		*p2pll = *p1pll;

		/* system clock */
		if (ASIC_IS_DCE4(rdev))
			spll->reference_freq =
				le16_to_cpu(firmware_info->info_21.usCoreReferenceClock);
		else
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
		if (ASIC_IS_DCE4(rdev))
			mpll->reference_freq =
				le16_to_cpu(firmware_info->info_21.usMemoryReferenceClock);
		else
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
			if (rdev->clock.default_dispclk == 0) {
				if (ASIC_IS_DCE6(rdev))
					rdev->clock.default_dispclk = 60000; /* 600 Mhz */
				else if (ASIC_IS_DCE5(rdev))
					rdev->clock.default_dispclk = 54000; /* 540 Mhz */
				else
					rdev->clock.default_dispclk = 60000; /* 600 Mhz */
			}
			/* set a reasonable default for DP */
			if (ASIC_IS_DCE6(rdev) && (rdev->clock.default_dispclk < 53900)) {
				DRM_INFO("Changing default dispclk from %dMhz to 600Mhz\n",
					 rdev->clock.default_dispclk / 100);
				rdev->clock.default_dispclk = 60000;
			}
			rdev->clock.dp_extclk =
				le16_to_cpu(firmware_info->info_21.usUniphyDPModeExtClkFreq);
			rdev->clock.current_dispclk = rdev->clock.default_dispclk;
		}
		*dcpll = *p1pll;

		rdev->clock.max_pixel_clock = le16_to_cpu(firmware_info->info.usMaxPixelClock);
		if (rdev->clock.max_pixel_clock == 0)
			rdev->clock.max_pixel_clock = 40000;

		/* not technically a clock, but... */
		rdev->mode_info.firmware_flags =
			le16_to_cpu(firmware_info->info.usFirmwareCapability.susAccess);

		return true;
	}

	return false;
}

union igp_info {
	struct _ATOM_INTEGRATED_SYSTEM_INFO info;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V2 info_2;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V6 info_6;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_7 info_7;
	struct _ATOM_INTEGRATED_SYSTEM_INFO_V1_8 info_8;
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
			if (le32_to_cpu(igp_info->info.ulBootUpMemoryClock))
				return true;
			break;
		case 2:
			if (le32_to_cpu(igp_info->info_2.ulBootUpSidePortClock))
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

bool radeon_atombios_get_ppll_ss_info(struct radeon_device *rdev,
				      struct radeon_atom_ss *ss,
				      int id)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, PPLL_SS_Info);
	uint16_t data_offset, size;
	struct _ATOM_SPREAD_SPECTRUM_INFO *ss_info;
	struct _ATOM_SPREAD_SPECTRUM_ASSIGNMENT *ss_assign;
	uint8_t frev, crev;
	int i, num_indices;

	memset(ss, 0, sizeof(struct radeon_atom_ss));
	if (atom_parse_data_header(mode_info->atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		ss_info =
			(struct _ATOM_SPREAD_SPECTRUM_INFO *)(mode_info->atom_context->bios + data_offset);

		num_indices = (size - sizeof(ATOM_COMMON_TABLE_HEADER)) /
			sizeof(ATOM_SPREAD_SPECTRUM_ASSIGNMENT);
		ss_assign = (struct _ATOM_SPREAD_SPECTRUM_ASSIGNMENT*)
			((u8 *)&ss_info->asSS_Info[0]);
		for (i = 0; i < num_indices; i++) {
			if (ss_assign->ucSS_Id == id) {
				ss->percentage =
					le16_to_cpu(ss_assign->usSpreadSpectrumPercentage);
				ss->type = ss_assign->ucSpreadSpectrumType;
				ss->step = ss_assign->ucSS_Step;
				ss->delay = ss_assign->ucSS_Delay;
				ss->range = ss_assign->ucSS_Range;
				ss->refdiv = ss_assign->ucRecommendedRef_Div;
				return true;
			}
			ss_assign = (struct _ATOM_SPREAD_SPECTRUM_ASSIGNMENT*)
				((u8 *)ss_assign + sizeof(struct _ATOM_SPREAD_SPECTRUM_ASSIGNMENT));
		}
	}
	return false;
}

static void radeon_atombios_get_igp_ss_overrides(struct radeon_device *rdev,
						 struct radeon_atom_ss *ss,
						 int id)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, IntegratedSystemInfo);
	u16 data_offset, size;
	union igp_info *igp_info;
	u8 frev, crev;
	u16 percentage = 0, rate = 0;

	/* get any igp specific overrides */
	if (atom_parse_data_header(mode_info->atom_context, index, &size,
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

bool radeon_atombios_get_asic_ss_info(struct radeon_device *rdev,
				      struct radeon_atom_ss *ss,
				      int id, u32 clock)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info);
	uint16_t data_offset, size;
	union asic_ss_info *ss_info;
	union asic_ss_assignment *ss_assign;
	uint8_t frev, crev;
	int i, num_indices;

	if (id == ASIC_INTERNAL_MEMORY_SS) {
		if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_MEMORY_CLOCK_SS_SUPPORT))
			return false;
	}
	if (id == ASIC_INTERNAL_ENGINE_SS) {
		if (!(rdev->mode_info.firmware_flags & ATOM_BIOS_INFO_ENGINE_CLOCK_SS_SUPPORT))
			return false;
	}

	memset(ss, 0, sizeof(struct radeon_atom_ss));
	if (atom_parse_data_header(mode_info->atom_context, index, &size,
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
					if (rdev->flags & RADEON_IS_IGP)
						radeon_atombios_get_igp_ss_overrides(rdev, ss, id);
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
		lvds->lcd_misc = lvds_info->info.ucLVDS_Misc;

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

		lvds->native_mode.width_mm = le16_to_cpu(lvds_info->info.sLCDTiming.usImageHSize);
		lvds->native_mode.height_mm = le16_to_cpu(lvds_info->info.sLCDTiming.usImageVSize);

		/* set crtc values */
		drm_mode_set_crtcinfo(&lvds->native_mode, CRTC_INTERLACE_HALVE_V);

		lvds->lcd_ss_id = lvds_info->info.ucSS_Id;

		encoder->native_mode = lvds->native_mode;

		if (encoder_enum == 2)
			lvds->linkb = true;
		else
			lvds->linkb = false;

		/* parse the lcd record table */
		if (le16_to_cpu(lvds_info->info.usModePatchTableOffset)) {
			ATOM_FAKE_EDID_PATCH_RECORD *fake_edid_record;
			ATOM_PANEL_RESOLUTION_PATCH_RECORD *panel_res_record;
			bool bad_record = false;
			u8 *record;

			if ((frev == 1) && (crev < 2))
				/* absolute */
				record = (u8 *)(mode_info->atom_context->bios +
						le16_to_cpu(lvds_info->info.usModePatchTableOffset));
			else
				/* relative */
				record = (u8 *)(mode_info->atom_context->bios +
						data_offset +
						le16_to_cpu(lvds_info->info.usModePatchTableOffset));
			while (*record != ATOM_RECORD_END_TYPE) {
				switch (*record) {
				case LCD_MODE_PATCH_RECORD_MODE_TYPE:
					record += sizeof(ATOM_PATCH_RECORD_MODE);
					break;
				case LCD_RTS_RECORD_TYPE:
					record += sizeof(ATOM_LCD_RTS_RECORD);
					break;
				case LCD_CAP_RECORD_TYPE:
					record += sizeof(ATOM_LCD_MODE_CONTROL_CAP);
					break;
				case LCD_FAKE_EDID_PATCH_RECORD_TYPE:
					fake_edid_record = (ATOM_FAKE_EDID_PATCH_RECORD *)record;
					if (fake_edid_record->ucFakeEDIDLength) {
						struct edid *edid;
						int edid_size =
							max((int)EDID_LENGTH, (int)fake_edid_record->ucFakeEDIDLength);
						edid = kmalloc(edid_size, GFP_KERNEL);
						if (edid) {
							memcpy((u8 *)edid, (u8 *)&fake_edid_record->ucFakeEDIDString[0],
							       fake_edid_record->ucFakeEDIDLength);

							if (drm_edid_is_valid(edid)) {
								rdev->mode_info.bios_hardcoded_edid = edid;
								rdev->mode_info.bios_hardcoded_edid_size = edid_size;
							} else
								kfree(edid);
						}
					}
					record += fake_edid_record->ucFakeEDIDLength ?
						fake_edid_record->ucFakeEDIDLength + 2 :
						sizeof(ATOM_FAKE_EDID_PATCH_RECORD);
					break;
				case LCD_PANEL_RESOLUTION_RECORD_TYPE:
					panel_res_record = (ATOM_PANEL_RESOLUTION_PATCH_RECORD *)record;
					lvds->native_mode.width_mm = panel_res_record->usHSize;
					lvds->native_mode.height_mm = panel_res_record->usVSize;
					record += sizeof(ATOM_PANEL_RESOLUTION_PATCH_RECORD);
					break;
				default:
					DRM_ERROR("Bad LCD record %d\n", *record);
					bad_record = true;
					break;
				}
				if (bad_record)
					break;
			}
		}
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

		mode->crtc_clock = mode->clock =
			le16_to_cpu(tv_info->aModeTimings[index].usPixelClock) * 10;

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

		mode->crtc_clock = mode->clock =
			le16_to_cpu(dtd_timings->usPixClk) * 10;
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
			DRM_DEBUG_KMS("Default TV standard: NTSC\n");
			break;
		case ATOM_TV_NTSCJ:
			tv_std = TV_STD_NTSC_J;
			DRM_DEBUG_KMS("Default TV standard: NTSC-J\n");
			break;
		case ATOM_TV_PAL:
			tv_std = TV_STD_PAL;
			DRM_DEBUG_KMS("Default TV standard: PAL\n");
			break;
		case ATOM_TV_PALM:
			tv_std = TV_STD_PAL_M;
			DRM_DEBUG_KMS("Default TV standard: PAL-M\n");
			break;
		case ATOM_TV_PALN:
			tv_std = TV_STD_PAL_N;
			DRM_DEBUG_KMS("Default TV standard: PAL-N\n");
			break;
		case ATOM_TV_PALCN:
			tv_std = TV_STD_PAL_CN;
			DRM_DEBUG_KMS("Default TV standard: PAL-CN\n");
			break;
		case ATOM_TV_PAL60:
			tv_std = TV_STD_PAL_60;
			DRM_DEBUG_KMS("Default TV standard: PAL-60\n");
			break;
		case ATOM_TV_SECAM:
			tv_std = TV_STD_SECAM;
			DRM_DEBUG_KMS("Default TV standard: SECAM\n");
			break;
		default:
			tv_std = TV_STD_NTSC;
			DRM_DEBUG_KMS("Unknown TV standard; defaulting to NTSC\n");
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
	"NONE",
	"External GPIO",
	"Evergreen",
	"emc2103",
	"Sumo",
	"Northern Islands",
	"Southern Islands",
	"lm96163",
	"Sea Islands",
};

union power_info {
	struct _ATOM_POWERPLAY_INFO info;
	struct _ATOM_POWERPLAY_INFO_V2 info_2;
	struct _ATOM_POWERPLAY_INFO_V3 info_3;
	struct _ATOM_PPLIB_POWERPLAYTABLE pplib;
	struct _ATOM_PPLIB_POWERPLAYTABLE2 pplib2;
	struct _ATOM_PPLIB_POWERPLAYTABLE3 pplib3;
};

union pplib_clock_info {
	struct _ATOM_PPLIB_R600_CLOCK_INFO r600;
	struct _ATOM_PPLIB_RS780_CLOCK_INFO rs780;
	struct _ATOM_PPLIB_EVERGREEN_CLOCK_INFO evergreen;
	struct _ATOM_PPLIB_SUMO_CLOCK_INFO sumo;
	struct _ATOM_PPLIB_SI_CLOCK_INFO si;
	struct _ATOM_PPLIB_CI_CLOCK_INFO ci;
};

union pplib_power_state {
	struct _ATOM_PPLIB_STATE v1;
	struct _ATOM_PPLIB_STATE_V2 v2;
};

static void radeon_atombios_parse_misc_flags_1_3(struct radeon_device *rdev,
						 int state_index,
						 u32 misc, u32 misc2)
{
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
}

static int radeon_atombios_parse_power_table_1_3(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	u32 misc, misc2 = 0;
	int num_modes = 0, i;
	int state_index = 0;
	struct radeon_i2c_bus_rec i2c_bus;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return state_index;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	/* add the i2c bus for thermal/fan chip */
	if ((power_info->info.ucOverdriveThermalController > 0) &&
	    (power_info->info.ucOverdriveThermalController < ARRAY_SIZE(thermal_controller_names))) {
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
	if (num_modes == 0)
		return state_index;
	rdev->pm.power_state = kzalloc(sizeof(struct radeon_power_state) * num_modes, GFP_KERNEL);
	if (!rdev->pm.power_state)
		return state_index;
	/* last mode is usually default, array is low to high */
	for (i = 0; i < num_modes; i++) {
		rdev->pm.power_state[state_index].clock_info =
			kzalloc(sizeof(struct radeon_pm_clock_info) * 1, GFP_KERNEL);
		if (!rdev->pm.power_state[state_index].clock_info)
			return state_index;
		rdev->pm.power_state[state_index].num_clock_modes = 1;
		rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_NONE;
		switch (frev) {
		case 1:
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
			radeon_atombios_parse_misc_flags_1_3(rdev, state_index, misc, 0);
			state_index++;
			break;
		case 2:
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
			radeon_atombios_parse_misc_flags_1_3(rdev, state_index, misc, misc2);
			state_index++;
			break;
		case 3:
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
			radeon_atombios_parse_misc_flags_1_3(rdev, state_index, misc, misc2);
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
	return state_index;
}

static void radeon_atombios_add_pplib_thermal_controller(struct radeon_device *rdev,
							 ATOM_PPLIB_THERMALCONTROLLER *controller)
{
	struct radeon_i2c_bus_rec i2c_bus;

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
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_SUMO) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			rdev->pm.int_thermal_type = THERMAL_TYPE_SUMO;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_NISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			rdev->pm.int_thermal_type = THERMAL_TYPE_NI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_SISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			rdev->pm.int_thermal_type = THERMAL_TYPE_SI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_CISLANDS) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			rdev->pm.int_thermal_type = THERMAL_TYPE_CI;
		} else if (controller->ucType == ATOM_PP_THERMALCONTROLLER_KAVERI) {
			DRM_INFO("Internal thermal controller %s fan control\n",
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
			rdev->pm.int_thermal_type = THERMAL_TYPE_KV;
		} else if ((controller->ucType ==
			    ATOM_PP_THERMALCONTROLLER_EXTERNAL_GPIO) ||
			   (controller->ucType ==
			    ATOM_PP_THERMALCONTROLLER_ADT7473_WITH_INTERNAL) ||
			   (controller->ucType ==
			    ATOM_PP_THERMALCONTROLLER_EMC2103_WITH_INTERNAL)) {
			DRM_INFO("Special thermal controller config\n");
		} else if (controller->ucType < ARRAY_SIZE(pp_lib_thermal_controller_names)) {
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
		} else {
			DRM_INFO("Unknown thermal controller type %d at 0x%02x %s fan control\n",
				 controller->ucType,
				 controller->ucI2cAddress >> 1,
				 (controller->ucFanParameters &
				  ATOM_PP_FANPARAMETERS_NOFAN) ? "without" : "with");
		}
	}
}

void radeon_atombios_get_default_voltages(struct radeon_device *rdev,
					  u16 *vddc, u16 *vddci, u16 *mvdd)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	u8 frev, crev;
	u16 data_offset;
	union firmware_info *firmware_info;

	*vddc = 0;
	*vddci = 0;
	*mvdd = 0;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
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

static void radeon_atombios_parse_pplib_non_clock_info(struct radeon_device *rdev,
						       int state_index, int mode_index,
						       struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info)
{
	int j;
	u32 misc = le32_to_cpu(non_clock_info->ulCapsAndSettings);
	u32 misc2 = le16_to_cpu(non_clock_info->usClassification);
	u16 vddc, vddci, mvdd;

	radeon_atombios_get_default_voltages(rdev, &vddc, &vddci, &mvdd);

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
		if ((rdev->family >= CHIP_BARTS) && !(rdev->flags & RADEON_IS_IGP)) {
			/* NI chips post without MC ucode, so default clocks are strobe mode only */
			rdev->pm.default_sclk = rdev->pm.power_state[state_index].clock_info[0].sclk;
			rdev->pm.default_mclk = rdev->pm.power_state[state_index].clock_info[0].mclk;
			rdev->pm.default_vddc = rdev->pm.power_state[state_index].clock_info[0].voltage.voltage;
			rdev->pm.default_vddci = rdev->pm.power_state[state_index].clock_info[0].voltage.vddci;
		} else {
			u16 max_vddci = 0;

			if (ASIC_IS_DCE4(rdev))
				radeon_atom_get_max_voltage(rdev,
							    SET_VOLTAGE_TYPE_ASIC_VDDCI,
							    &max_vddci);
			/* patch the table values with the default sclk/mclk from firmware info */
			for (j = 0; j < mode_index; j++) {
				rdev->pm.power_state[state_index].clock_info[j].mclk =
					rdev->clock.default_mclk;
				rdev->pm.power_state[state_index].clock_info[j].sclk =
					rdev->clock.default_sclk;
				if (vddc)
					rdev->pm.power_state[state_index].clock_info[j].voltage.voltage =
						vddc;
				if (max_vddci)
					rdev->pm.power_state[state_index].clock_info[j].voltage.vddci =
						max_vddci;
			}
		}
	}
}

static bool radeon_atombios_parse_pplib_clock_info(struct radeon_device *rdev,
						   int state_index, int mode_index,
						   union pplib_clock_info *clock_info)
{
	u32 sclk, mclk;
	u16 vddc;

	if (rdev->flags & RADEON_IS_IGP) {
		if (rdev->family >= CHIP_PALM) {
			sclk = le16_to_cpu(clock_info->sumo.usEngineClockLow);
			sclk |= clock_info->sumo.ucEngineClockHigh << 16;
			rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		} else {
			sclk = le16_to_cpu(clock_info->rs780.usLowEngineClockLow);
			sclk |= clock_info->rs780.ucLowEngineClockHigh << 16;
			rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		}
	} else if (rdev->family >= CHIP_BONAIRE) {
		sclk = le16_to_cpu(clock_info->ci.usEngineClockLow);
		sclk |= clock_info->ci.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->ci.usMemoryClockLow);
		mclk |= clock_info->ci.ucMemoryClockHigh << 16;
		rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
			VOLTAGE_NONE;
	} else if (rdev->family >= CHIP_TAHITI) {
		sclk = le16_to_cpu(clock_info->si.usEngineClockLow);
		sclk |= clock_info->si.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->si.usMemoryClockLow);
		mclk |= clock_info->si.ucMemoryClockHigh << 16;
		rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
			VOLTAGE_SW;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage =
			le16_to_cpu(clock_info->si.usVDDC);
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.vddci =
			le16_to_cpu(clock_info->si.usVDDCI);
	} else if (rdev->family >= CHIP_CEDAR) {
		sclk = le16_to_cpu(clock_info->evergreen.usEngineClockLow);
		sclk |= clock_info->evergreen.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->evergreen.usMemoryClockLow);
		mclk |= clock_info->evergreen.ucMemoryClockHigh << 16;
		rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
			VOLTAGE_SW;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage =
			le16_to_cpu(clock_info->evergreen.usVDDC);
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.vddci =
			le16_to_cpu(clock_info->evergreen.usVDDCI);
	} else {
		sclk = le16_to_cpu(clock_info->r600.usEngineClockLow);
		sclk |= clock_info->r600.ucEngineClockHigh << 16;
		mclk = le16_to_cpu(clock_info->r600.usMemoryClockLow);
		mclk |= clock_info->r600.ucMemoryClockHigh << 16;
		rdev->pm.power_state[state_index].clock_info[mode_index].mclk = mclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].sclk = sclk;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.type =
			VOLTAGE_SW;
		rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage =
			le16_to_cpu(clock_info->r600.usVDDC);
	}

	/* patch up vddc if necessary */
	switch (rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage) {
	case ATOM_VIRTUAL_VOLTAGE_ID0:
	case ATOM_VIRTUAL_VOLTAGE_ID1:
	case ATOM_VIRTUAL_VOLTAGE_ID2:
	case ATOM_VIRTUAL_VOLTAGE_ID3:
	case ATOM_VIRTUAL_VOLTAGE_ID4:
	case ATOM_VIRTUAL_VOLTAGE_ID5:
	case ATOM_VIRTUAL_VOLTAGE_ID6:
	case ATOM_VIRTUAL_VOLTAGE_ID7:
		if (radeon_atom_get_max_vddc(rdev, VOLTAGE_TYPE_VDDC,
					     rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage,
					     &vddc) == 0)
			rdev->pm.power_state[state_index].clock_info[mode_index].voltage.voltage = vddc;
		break;
	default:
		break;
	}

	if (rdev->flags & RADEON_IS_IGP) {
		/* skip invalid modes */
		if (rdev->pm.power_state[state_index].clock_info[mode_index].sclk == 0)
			return false;
	} else {
		/* skip invalid modes */
		if ((rdev->pm.power_state[state_index].clock_info[mode_index].mclk == 0) ||
		    (rdev->pm.power_state[state_index].clock_info[mode_index].sclk == 0))
			return false;
	}
	return true;
}

static int radeon_atombios_parse_power_table_4_5(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j;
	int state_index = 0, mode_index = 0;
	union pplib_clock_info *clock_info;
	bool valid;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return state_index;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	radeon_atombios_add_pplib_thermal_controller(rdev, &power_info->pplib.sThermalController);
	if (power_info->pplib.ucNumStates == 0)
		return state_index;
	rdev->pm.power_state = kzalloc(sizeof(struct radeon_power_state) *
				       power_info->pplib.ucNumStates, GFP_KERNEL);
	if (!rdev->pm.power_state)
		return state_index;
	/* first mode is usually default, followed by low to high */
	for (i = 0; i < power_info->pplib.ucNumStates; i++) {
		mode_index = 0;
		power_state = (union pplib_power_state *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usStateArrayOffset) +
			 i * power_info->pplib.ucStateEntrySize);
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			(mode_info->atom_context->bios + data_offset +
			 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset) +
			 (power_state->v1.ucNonClockStateIndex *
			  power_info->pplib.ucNonClockSize));
		rdev->pm.power_state[i].clock_info = kzalloc(sizeof(struct radeon_pm_clock_info) *
							     ((power_info->pplib.ucStateEntrySize - 1) ?
							      (power_info->pplib.ucStateEntrySize - 1) : 1),
							     GFP_KERNEL);
		if (!rdev->pm.power_state[i].clock_info)
			return state_index;
		if (power_info->pplib.ucStateEntrySize - 1) {
			for (j = 0; j < (power_info->pplib.ucStateEntrySize - 1); j++) {
				clock_info = (union pplib_clock_info *)
					(mode_info->atom_context->bios + data_offset +
					 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset) +
					 (power_state->v1.ucClockStateIndices[j] *
					  power_info->pplib.ucClockInfoSize));
				valid = radeon_atombios_parse_pplib_clock_info(rdev,
									       state_index, mode_index,
									       clock_info);
				if (valid)
					mode_index++;
			}
		} else {
			rdev->pm.power_state[state_index].clock_info[0].mclk =
				rdev->clock.default_mclk;
			rdev->pm.power_state[state_index].clock_info[0].sclk =
				rdev->clock.default_sclk;
			mode_index++;
		}
		rdev->pm.power_state[state_index].num_clock_modes = mode_index;
		if (mode_index) {
			radeon_atombios_parse_pplib_non_clock_info(rdev, state_index, mode_index,
								   non_clock_info);
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
	return state_index;
}

static int radeon_atombios_parse_power_table_6(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	struct _ATOM_PPLIB_NONCLOCK_INFO *non_clock_info;
	union pplib_power_state *power_state;
	int i, j, non_clock_array_index, clock_array_index;
	int state_index = 0, mode_index = 0;
	union pplib_clock_info *clock_info;
	struct _StateArray *state_array;
	struct _ClockInfoArray *clock_info_array;
	struct _NonClockInfoArray *non_clock_info_array;
	bool valid;
	union power_info *power_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
        u16 data_offset;
	u8 frev, crev;
	u8 *power_state_offset;

	if (!atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset))
		return state_index;
	power_info = (union power_info *)(mode_info->atom_context->bios + data_offset);

	radeon_atombios_add_pplib_thermal_controller(rdev, &power_info->pplib.sThermalController);
	state_array = (struct _StateArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usStateArrayOffset));
	clock_info_array = (struct _ClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usClockInfoArrayOffset));
	non_clock_info_array = (struct _NonClockInfoArray *)
		(mode_info->atom_context->bios + data_offset +
		 le16_to_cpu(power_info->pplib.usNonClockInfoArrayOffset));
	if (state_array->ucNumEntries == 0)
		return state_index;
	rdev->pm.power_state = kzalloc(sizeof(struct radeon_power_state) *
				       state_array->ucNumEntries, GFP_KERNEL);
	if (!rdev->pm.power_state)
		return state_index;
	power_state_offset = (u8 *)state_array->states;
	for (i = 0; i < state_array->ucNumEntries; i++) {
		mode_index = 0;
		power_state = (union pplib_power_state *)power_state_offset;
		non_clock_array_index = power_state->v2.nonClockInfoIndex;
		non_clock_info = (struct _ATOM_PPLIB_NONCLOCK_INFO *)
			&non_clock_info_array->nonClockInfo[non_clock_array_index];
		rdev->pm.power_state[i].clock_info = kzalloc(sizeof(struct radeon_pm_clock_info) *
							     (power_state->v2.ucNumDPMLevels ?
							      power_state->v2.ucNumDPMLevels : 1),
							     GFP_KERNEL);
		if (!rdev->pm.power_state[i].clock_info)
			return state_index;
		if (power_state->v2.ucNumDPMLevels) {
			for (j = 0; j < power_state->v2.ucNumDPMLevels; j++) {
				clock_array_index = power_state->v2.clockInfoIndex[j];
				clock_info = (union pplib_clock_info *)
					&clock_info_array->clockInfo[clock_array_index * clock_info_array->ucEntrySize];
				valid = radeon_atombios_parse_pplib_clock_info(rdev,
									       state_index, mode_index,
									       clock_info);
				if (valid)
					mode_index++;
			}
		} else {
			rdev->pm.power_state[state_index].clock_info[0].mclk =
				rdev->clock.default_mclk;
			rdev->pm.power_state[state_index].clock_info[0].sclk =
				rdev->clock.default_sclk;
			mode_index++;
		}
		rdev->pm.power_state[state_index].num_clock_modes = mode_index;
		if (mode_index) {
			radeon_atombios_parse_pplib_non_clock_info(rdev, state_index, mode_index,
								   non_clock_info);
			state_index++;
		}
		power_state_offset += 2 + power_state->v2.ucNumDPMLevels;
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
	return state_index;
}

void radeon_atombios_get_power_modes(struct radeon_device *rdev)
{
	struct radeon_mode_info *mode_info = &rdev->mode_info;
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);
	u16 data_offset;
	u8 frev, crev;
	int state_index = 0;

	rdev->pm.default_power_state_index = -1;

	if (atom_parse_data_header(mode_info->atom_context, index, NULL,
				   &frev, &crev, &data_offset)) {
		switch (frev) {
		case 1:
		case 2:
		case 3:
			state_index = radeon_atombios_parse_power_table_1_3(rdev);
			break;
		case 4:
		case 5:
			state_index = radeon_atombios_parse_power_table_4_5(rdev);
			break;
		case 6:
			state_index = radeon_atombios_parse_power_table_6(rdev);
			break;
		default:
			break;
		}
	}

	if (state_index == 0) {
		rdev->pm.power_state = kzalloc(sizeof(struct radeon_power_state), GFP_KERNEL);
		if (rdev->pm.power_state) {
			rdev->pm.power_state[0].clock_info =
				kzalloc(sizeof(struct radeon_pm_clock_info) * 1, GFP_KERNEL);
			if (rdev->pm.power_state[0].clock_info) {
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
		}
	}

	rdev->pm.num_power_states = state_index;

	rdev->pm.current_power_state_index = rdev->pm.default_power_state_index;
	rdev->pm.current_clock_mode_index = 0;
	if (rdev->pm.default_power_state_index >= 0)
		rdev->pm.current_vddc =
			rdev->pm.power_state[rdev->pm.default_power_state_index].clock_info[0].voltage.voltage;
	else
		rdev->pm.current_vddc = 0;
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

int radeon_atom_get_clock_dividers(struct radeon_device *rdev,
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

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 1:
		/* r4xx, r5xx */
		args.v1.ucAction = clock_type;
		args.v1.ulClock = cpu_to_le32(clock);	/* 10 khz */

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		dividers->post_div = args.v1.ucPostDiv;
		dividers->fb_div = args.v1.ucFbDiv;
		dividers->enable_post_div = true;
		break;
	case 2:
	case 3:
	case 5:
		/* r6xx, r7xx, evergreen, ni, si */
		if (rdev->family <= CHIP_RV770) {
			args.v2.ucAction = clock_type;
			args.v2.ulClock = cpu_to_le32(clock);	/* 10 khz */

			atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

			dividers->post_div = args.v2.ucPostDiv;
			dividers->fb_div = le16_to_cpu(args.v2.usFbDiv);
			dividers->ref_div = args.v2.ucAction;
			if (rdev->family == CHIP_RV770) {
				dividers->enable_post_div = (le32_to_cpu(args.v2.ulClock) & (1 << 24)) ?
					true : false;
				dividers->vco_mode = (le32_to_cpu(args.v2.ulClock) & (1 << 25)) ? 1 : 0;
			} else
				dividers->enable_post_div = (dividers->fb_div & 1) ? true : false;
		} else {
			if (clock_type == COMPUTE_ENGINE_PLL_PARAM) {
				args.v3.ulClockParams = cpu_to_le32((clock_type << 24) | clock);

				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

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
				if (rdev->family >= CHIP_TAHITI)
					return -EINVAL;
				args.v5.ulClockParams = cpu_to_le32((clock_type << 24) | clock);
				if (strobe_mode)
					args.v5.ucInputFlag = ATOM_PLL_INPUT_FLAG_PLL_STROBE_MODE_EN;

				atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

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
		}
		break;
	case 4:
		/* fusion */
		args.v4.ulClock = cpu_to_le32(clock);	/* 10 khz */

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		dividers->post_divider = dividers->post_div = args.v4.ucPostDiv;
		dividers->real_clock = le32_to_cpu(args.v4.ulClock);
		break;
	case 6:
		/* CI */
		/* COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK, COMPUTE_GPUCLK_INPUT_FLAG_SCLK */
		args.v6_in.ulClock.ulComputeClockFlag = clock_type;
		args.v6_in.ulClock.ulClockFreq = cpu_to_le32(clock);	/* 10 khz */

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

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

int radeon_atom_get_memory_pll_dividers(struct radeon_device *rdev,
					u32 clock,
					bool strobe_mode,
					struct atom_mpll_param *mpll_param)
{
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1 args;
	int index = GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam);
	u8 frev, crev;

	memset(&args, 0, sizeof(args));
	memset(mpll_param, 0, sizeof(struct atom_mpll_param));

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
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

			atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

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
	return le32_to_cpu(args.ulReturnEngineClock);
}

uint32_t radeon_atom_get_memory_clock(struct radeon_device *rdev)
{
	GET_MEMORY_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, GetMemoryClock);

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	return le32_to_cpu(args.ulReturnMemoryClock);
}

void radeon_atom_set_engine_clock(struct radeon_device *rdev,
				  uint32_t eng_clock)
{
	SET_ENGINE_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetEngineClock);

	args.ulTargetEngineClock = cpu_to_le32(eng_clock);	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_set_memory_clock(struct radeon_device *rdev,
				  uint32_t mem_clock)
{
	SET_MEMORY_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, SetMemoryClock);

	if (rdev->flags & RADEON_IS_IGP)
		return;

	args.ulTargetMemoryClock = cpu_to_le32(mem_clock);	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_set_engine_dram_timings(struct radeon_device *rdev,
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

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_update_memory_dll(struct radeon_device *rdev,
				   u32 mem_clock)
{
	u32 args;
	int index = GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings);

	args = cpu_to_le32(mem_clock);	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

void radeon_atom_set_ac_timing(struct radeon_device *rdev,
			       u32 mem_clock)
{
	SET_MEMORY_CLOCK_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings);
	u32 tmp = mem_clock | (COMPUTE_MEMORY_PLL_PARAM << 24);

	args.ulTargetMemoryClock = cpu_to_le32(tmp);	/* 10 khz */

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

union set_voltage {
	struct _SET_VOLTAGE_PS_ALLOCATION alloc;
	struct _SET_VOLTAGE_PARAMETERS v1;
	struct _SET_VOLTAGE_PARAMETERS_V2 v2;
	struct _SET_VOLTAGE_PARAMETERS_V1_3 v3;
};

void radeon_atom_set_voltage(struct radeon_device *rdev, u16 voltage_level, u8 voltage_type)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev, volt_index = voltage_level;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return;

	/* 0xff01 is a flag rather then an actual voltage */
	if (voltage_level == 0xff01)
		return;

	switch (crev) {
	case 1:
		args.v1.ucVoltageType = voltage_type;
		args.v1.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_ALL_SOURCE;
		args.v1.ucVoltageIndex = volt_index;
		break;
	case 2:
		args.v2.ucVoltageType = voltage_type;
		args.v2.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_SET_VOLTAGE;
		args.v2.usVoltageLevel = cpu_to_le16(voltage_level);
		break;
	case 3:
		args.v3.ucVoltageType = voltage_type;
		args.v3.ucVoltageMode = ATOM_SET_VOLTAGE;
		args.v3.usVoltageLevel = cpu_to_le16(voltage_level);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return;
	}

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
}

int radeon_atom_get_max_vddc(struct radeon_device *rdev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 1:
		return -EINVAL;
	case 2:
		args.v2.ucVoltageType = SET_VOLTAGE_GET_MAX_VOLTAGE;
		args.v2.ucVoltageMode = 0;
		args.v2.usVoltageLevel = 0;

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		*voltage = le16_to_cpu(args.v2.usVoltageLevel);
		break;
	case 3:
		args.v3.ucVoltageType = voltage_type;
		args.v3.ucVoltageMode = ATOM_GET_VOLTAGE_LEVEL;
		args.v3.usVoltageLevel = cpu_to_le16(voltage_id);

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		*voltage = le16_to_cpu(args.v3.usVoltageLevel);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

	return 0;
}

int radeon_atom_get_leakage_vddc_based_on_leakage_idx(struct radeon_device *rdev,
						      u16 *voltage,
						      u16 leakage_idx)
{
	return radeon_atom_get_max_vddc(rdev, VOLTAGE_TYPE_VDDC, leakage_idx, voltage);
}

int radeon_atom_get_leakage_id_from_vbios(struct radeon_device *rdev,
					  u16 *leakage_id)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 3:
	case 4:
		args.v3.ucVoltageType = 0;
		args.v3.ucVoltageMode = ATOM_GET_LEAKAGE_ID;
		args.v3.usVoltageLevel = 0;

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		*leakage_id = le16_to_cpu(args.v3.usVoltageLevel);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

	return 0;
}

int radeon_atom_get_leakage_vddc_based_on_leakage_params(struct radeon_device *rdev,
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

	if (!atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				    &frev, &crev, &data_offset))
		return -EINVAL;

	profile = (ATOM_ASIC_PROFILING_INFO_V2_1 *)
		(rdev->mode_info.atom_context->bios + data_offset);

	switch (frev) {
	case 1:
		return -EINVAL;
	case 2:
		switch (crev) {
		case 1:
			if (size < sizeof(ATOM_ASIC_PROFILING_INFO_V2_1))
				return -EINVAL;
			leakage_bin = (u16 *)
				(rdev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usLeakageBinArrayOffset));
			vddc_id_buf = (u16 *)
				(rdev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDC_IdArrayOffset));
			vddc_buf = (u16 *)
				(rdev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDC_LevelArrayOffset));
			vddci_id_buf = (u16 *)
				(rdev->mode_info.atom_context->bios + data_offset +
				 le16_to_cpu(profile->usElbVDDCI_IdArrayOffset));
			vddci_buf = (u16 *)
				(rdev->mode_info.atom_context->bios + data_offset +
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

int radeon_atom_get_voltage_gpio_settings(struct radeon_device *rdev,
					  u16 voltage_level, u8 voltage_type,
					  u32 *gpio_value, u32 *gpio_mask)
{
	union set_voltage args;
	int index = GetIndexIntoMasterTable(COMMAND, SetVoltage);
	u8 frev, crev;

	if (!atom_parse_cmd_header(rdev->mode_info.atom_context, index, &frev, &crev))
		return -EINVAL;

	switch (crev) {
	case 1:
		return -EINVAL;
	case 2:
		args.v2.ucVoltageType = voltage_type;
		args.v2.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_GET_GPIOMASK;
		args.v2.usVoltageLevel = cpu_to_le16(voltage_level);

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		*gpio_mask = le32_to_cpu(*(u32 *)&args.v2);

		args.v2.ucVoltageType = voltage_type;
		args.v2.ucVoltageMode = SET_ASIC_VOLTAGE_MODE_GET_GPIOVAL;
		args.v2.usVoltageLevel = cpu_to_le16(voltage_level);

		atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

		*gpio_value = le32_to_cpu(*(u32 *)&args.v2);
		break;
	default:
		DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
		return -EINVAL;
	}

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

static ATOM_VOLTAGE_OBJECT *atom_lookup_voltage_object_v1(ATOM_VOLTAGE_OBJECT_INFO *v1,
							  u8 voltage_type)
{
	u32 size = le16_to_cpu(v1->sHeader.usStructureSize);
	u32 offset = offsetof(ATOM_VOLTAGE_OBJECT_INFO, asVoltageObj[0]);
	u8 *start = (u8 *)v1;

	while (offset < size) {
		ATOM_VOLTAGE_OBJECT *vo = (ATOM_VOLTAGE_OBJECT *)(start + offset);
		if (vo->ucVoltageType == voltage_type)
			return vo;
		offset += offsetof(ATOM_VOLTAGE_OBJECT, asFormula.ucVIDAdjustEntries) +
			vo->asFormula.ucNumOfVoltageEntries;
	}
	return NULL;
}

static ATOM_VOLTAGE_OBJECT_V2 *atom_lookup_voltage_object_v2(ATOM_VOLTAGE_OBJECT_INFO_V2 *v2,
							     u8 voltage_type)
{
	u32 size = le16_to_cpu(v2->sHeader.usStructureSize);
	u32 offset = offsetof(ATOM_VOLTAGE_OBJECT_INFO_V2, asVoltageObj[0]);
	u8 *start = (u8*)v2;

	while (offset < size) {
		ATOM_VOLTAGE_OBJECT_V2 *vo = (ATOM_VOLTAGE_OBJECT_V2 *)(start + offset);
		if (vo->ucVoltageType == voltage_type)
			return vo;
		offset += offsetof(ATOM_VOLTAGE_OBJECT_V2, asFormula.asVIDAdjustEntries) +
			(vo->asFormula.ucNumOfVoltageEntries * sizeof(VOLTAGE_LUT_ENTRY));
	}
	return NULL;
}

static ATOM_VOLTAGE_OBJECT_V3 *atom_lookup_voltage_object_v3(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *v3,
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

bool
radeon_atom_is_voltage_gpio(struct radeon_device *rdev,
			    u8 voltage_type, u8 voltage_mode)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 1:
		case 2:
			switch (crev) {
			case 1:
				voltage_object = (union voltage_object *)
					atom_lookup_voltage_object_v1(&voltage_info->v1, voltage_type);
				if (voltage_object &&
				    (voltage_object->v1.asControl.ucVoltageControlId == VOLTAGE_CONTROLLED_BY_GPIO))
					return true;
				break;
			case 2:
				voltage_object = (union voltage_object *)
					atom_lookup_voltage_object_v2(&voltage_info->v2, voltage_type);
				if (voltage_object &&
				    (voltage_object->v2.asControl.ucVoltageControlId == VOLTAGE_CONTROLLED_BY_GPIO))
					return true;
				break;
			default:
				DRM_ERROR("unknown voltage object table\n");
				return false;
			}
			break;
		case 3:
			switch (crev) {
			case 1:
				if (atom_lookup_voltage_object_v3(&voltage_info->v3,
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

int radeon_atom_get_svi2_info(struct radeon_device *rdev,
			      u8 voltage_type,
			      u8 *svd_gpio_id, u8 *svc_gpio_id)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 3:
			switch (crev) {
			case 1:
				voltage_object = (union voltage_object *)
					atom_lookup_voltage_object_v3(&voltage_info->v3,
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

int radeon_atom_get_max_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *max_voltage)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (crev) {
		case 1:
			voltage_object = (union voltage_object *)
				atom_lookup_voltage_object_v1(&voltage_info->v1, voltage_type);
			if (voltage_object) {
				ATOM_VOLTAGE_FORMULA *formula =
					&voltage_object->v1.asFormula;
				if (formula->ucFlag & 1)
					*max_voltage =
						le16_to_cpu(formula->usVoltageBaseLevel) +
						formula->ucNumOfVoltageEntries / 2 *
						le16_to_cpu(formula->usVoltageStep);
				else
					*max_voltage =
						le16_to_cpu(formula->usVoltageBaseLevel) +
						(formula->ucNumOfVoltageEntries - 1) *
						le16_to_cpu(formula->usVoltageStep);
				return 0;
			}
			break;
		case 2:
			voltage_object = (union voltage_object *)
				atom_lookup_voltage_object_v2(&voltage_info->v2, voltage_type);
			if (voltage_object) {
				ATOM_VOLTAGE_FORMULA_V2 *formula =
					&voltage_object->v2.asFormula;
				if (formula->ucNumOfVoltageEntries) {
					VOLTAGE_LUT_ENTRY *lut = (VOLTAGE_LUT_ENTRY *)
						((u8 *)&formula->asVIDAdjustEntries[0] +
						 (sizeof(VOLTAGE_LUT_ENTRY) * (formula->ucNumOfVoltageEntries - 1)));
					*max_voltage =
						le16_to_cpu(lut->usVoltageValue);
					return 0;
				}
			}
			break;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return -EINVAL;
		}

	}
	return -EINVAL;
}

int radeon_atom_get_min_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *min_voltage)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (crev) {
		case 1:
			voltage_object = (union voltage_object *)
				atom_lookup_voltage_object_v1(&voltage_info->v1, voltage_type);
			if (voltage_object) {
				ATOM_VOLTAGE_FORMULA *formula =
					&voltage_object->v1.asFormula;
				*min_voltage =
					le16_to_cpu(formula->usVoltageBaseLevel);
				return 0;
			}
			break;
		case 2:
			voltage_object = (union voltage_object *)
				atom_lookup_voltage_object_v2(&voltage_info->v2, voltage_type);
			if (voltage_object) {
				ATOM_VOLTAGE_FORMULA_V2 *formula =
					&voltage_object->v2.asFormula;
				if (formula->ucNumOfVoltageEntries) {
					*min_voltage =
						le16_to_cpu(formula->asVIDAdjustEntries[
								    0
								    ].usVoltageValue);
					return 0;
				}
			}
			break;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return -EINVAL;
		}

	}
	return -EINVAL;
}

int radeon_atom_get_voltage_step(struct radeon_device *rdev,
				 u8 voltage_type, u16 *voltage_step)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (crev) {
		case 1:
			voltage_object = (union voltage_object *)
				atom_lookup_voltage_object_v1(&voltage_info->v1, voltage_type);
			if (voltage_object) {
				ATOM_VOLTAGE_FORMULA *formula =
					&voltage_object->v1.asFormula;
				if (formula->ucFlag & 1)
					*voltage_step =
						(le16_to_cpu(formula->usVoltageStep) + 1) / 2;
				else
					*voltage_step =
						le16_to_cpu(formula->usVoltageStep);
				return 0;
			}
			break;
		case 2:
			return -EINVAL;
		default:
			DRM_ERROR("unknown voltage object table\n");
			return -EINVAL;
		}

	}
	return -EINVAL;
}

int radeon_atom_round_to_true_voltage(struct radeon_device *rdev,
				      u8 voltage_type,
				      u16 nominal_voltage,
				      u16 *true_voltage)
{
	u16 min_voltage, max_voltage, voltage_step;

	if (radeon_atom_get_max_voltage(rdev, voltage_type, &max_voltage))
		return -EINVAL;
	if (radeon_atom_get_min_voltage(rdev, voltage_type, &min_voltage))
		return -EINVAL;
	if (radeon_atom_get_voltage_step(rdev, voltage_type, &voltage_step))
		return -EINVAL;

	if (nominal_voltage <= min_voltage)
		*true_voltage = min_voltage;
	else if (nominal_voltage >= max_voltage)
		*true_voltage = max_voltage;
	else
		*true_voltage = min_voltage +
			((nominal_voltage - min_voltage) / voltage_step) *
			voltage_step;

	return 0;
}

int radeon_atom_get_voltage_table(struct radeon_device *rdev,
				  u8 voltage_type, u8 voltage_mode,
				  struct atom_voltage_table *voltage_table)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 data_offset, size;
	int i, ret;
	union voltage_object_info *voltage_info;
	union voltage_object *voltage_object = NULL;

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		voltage_info = (union voltage_object_info *)
			(rdev->mode_info.atom_context->bios + data_offset);

		switch (frev) {
		case 1:
		case 2:
			switch (crev) {
			case 1:
				DRM_ERROR("old table version %d, %d\n", frev, crev);
				return -EINVAL;
			case 2:
				voltage_object = (union voltage_object *)
					atom_lookup_voltage_object_v2(&voltage_info->v2, voltage_type);
				if (voltage_object) {
					ATOM_VOLTAGE_FORMULA_V2 *formula =
						&voltage_object->v2.asFormula;
					VOLTAGE_LUT_ENTRY *lut;
					if (formula->ucNumOfVoltageEntries > MAX_VOLTAGE_ENTRIES)
						return -EINVAL;
					lut = &formula->asVIDAdjustEntries[0];
					for (i = 0; i < formula->ucNumOfVoltageEntries; i++) {
						voltage_table->entries[i].value =
							le16_to_cpu(lut->usVoltageValue);
						ret = radeon_atom_get_voltage_gpio_settings(rdev,
											    voltage_table->entries[i].value,
											    voltage_type,
											    &voltage_table->entries[i].smio_low,
											    &voltage_table->mask_low);
						if (ret)
							return ret;
						lut = (VOLTAGE_LUT_ENTRY *)
							((u8 *)lut + sizeof(VOLTAGE_LUT_ENTRY));
					}
					voltage_table->count = formula->ucNumOfVoltageEntries;
					return 0;
				}
				break;
			default:
				DRM_ERROR("unknown voltage object table\n");
				return -EINVAL;
			}
			break;
		case 3:
			switch (crev) {
			case 1:
				voltage_object = (union voltage_object *)
					atom_lookup_voltage_object_v3(&voltage_info->v3,
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

int radeon_atom_get_memory_info(struct radeon_device *rdev,
				u8 module_index, struct atom_memory_info *mem_info)
{
	int index = GetIndexIntoMasterTable(DATA, VRAM_Info);
	u8 frev, crev, i;
	u16 data_offset, size;
	union vram_info *vram_info;

	memset(mem_info, 0, sizeof(struct atom_memory_info));

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		vram_info = (union vram_info *)
			(rdev->mode_info.atom_context->bios + data_offset);
		switch (frev) {
		case 1:
			switch (crev) {
			case 3:
				/* r6xx */
				if (module_index < vram_info->v1_3.ucNumOfVRAMModule) {
					ATOM_VRAM_MODULE_V3 *vram_module =
						(ATOM_VRAM_MODULE_V3 *)vram_info->v1_3.aVramInfo;

					for (i = 0; i < module_index; i++) {
						if (le16_to_cpu(vram_module->usSize) == 0)
							return -EINVAL;
						vram_module = (ATOM_VRAM_MODULE_V3 *)
							((u8 *)vram_module + le16_to_cpu(vram_module->usSize));
					}
					mem_info->mem_vendor = vram_module->asMemory.ucMemoryVenderID & 0xf;
					mem_info->mem_type = vram_module->asMemory.ucMemoryType & 0xf0;
				} else
					return -EINVAL;
				break;
			case 4:
				/* r7xx, evergreen */
				if (module_index < vram_info->v1_4.ucNumOfVRAMModule) {
					ATOM_VRAM_MODULE_V4 *vram_module =
						(ATOM_VRAM_MODULE_V4 *)vram_info->v1_4.aVramInfo;

					for (i = 0; i < module_index; i++) {
						if (le16_to_cpu(vram_module->usModuleSize) == 0)
							return -EINVAL;
						vram_module = (ATOM_VRAM_MODULE_V4 *)
							((u8 *)vram_module + le16_to_cpu(vram_module->usModuleSize));
					}
					mem_info->mem_vendor = vram_module->ucMemoryVenderID & 0xf;
					mem_info->mem_type = vram_module->ucMemoryType & 0xf0;
				} else
					return -EINVAL;
				break;
			default:
				DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
				return -EINVAL;
			}
			break;
		case 2:
			switch (crev) {
			case 1:
				/* ni */
				if (module_index < vram_info->v2_1.ucNumOfVRAMModule) {
					ATOM_VRAM_MODULE_V7 *vram_module =
						(ATOM_VRAM_MODULE_V7 *)vram_info->v2_1.aVramInfo;

					for (i = 0; i < module_index; i++) {
						if (le16_to_cpu(vram_module->usModuleSize) == 0)
							return -EINVAL;
						vram_module = (ATOM_VRAM_MODULE_V7 *)
							((u8 *)vram_module + le16_to_cpu(vram_module->usModuleSize));
					}
					mem_info->mem_vendor = vram_module->ucMemoryVenderID & 0xf;
					mem_info->mem_type = vram_module->ucMemoryType & 0xf0;
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

int radeon_atom_get_mclk_range_table(struct radeon_device *rdev,
				     bool gddr5, u8 module_index,
				     struct atom_memory_clock_range_table *mclk_range_table)
{
	int index = GetIndexIntoMasterTable(DATA, VRAM_Info);
	u8 frev, crev, i;
	u16 data_offset, size;
	union vram_info *vram_info;
	u32 mem_timing_size = gddr5 ?
		sizeof(ATOM_MEMORY_TIMING_FORMAT_V2) : sizeof(ATOM_MEMORY_TIMING_FORMAT);

	memset(mclk_range_table, 0, sizeof(struct atom_memory_clock_range_table));

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		vram_info = (union vram_info *)
			(rdev->mode_info.atom_context->bios + data_offset);
		switch (frev) {
		case 1:
			switch (crev) {
			case 3:
				DRM_ERROR("old table version %d, %d\n", frev, crev);
				return -EINVAL;
			case 4:
				/* r7xx, evergreen */
				if (module_index < vram_info->v1_4.ucNumOfVRAMModule) {
					ATOM_VRAM_MODULE_V4 *vram_module =
						(ATOM_VRAM_MODULE_V4 *)vram_info->v1_4.aVramInfo;
					ATOM_MEMORY_TIMING_FORMAT *format;

					for (i = 0; i < module_index; i++) {
						if (le16_to_cpu(vram_module->usModuleSize) == 0)
							return -EINVAL;
						vram_module = (ATOM_VRAM_MODULE_V4 *)
							((u8 *)vram_module + le16_to_cpu(vram_module->usModuleSize));
					}
					mclk_range_table->num_entries = (u8)
						((le16_to_cpu(vram_module->usModuleSize) - offsetof(ATOM_VRAM_MODULE_V4, asMemTiming)) /
						 mem_timing_size);
					format = &vram_module->asMemTiming[0];
					for (i = 0; i < mclk_range_table->num_entries; i++) {
						mclk_range_table->mclk[i] = le32_to_cpu(format->ulClkRange);
						format = (ATOM_MEMORY_TIMING_FORMAT *)
							((u8 *)format + mem_timing_size);
					}
				} else
					return -EINVAL;
				break;
			default:
				DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
				return -EINVAL;
			}
			break;
		case 2:
			DRM_ERROR("new table version %d, %d\n", frev, crev);
			return -EINVAL;
		default:
			DRM_ERROR("Unknown table version %d, %d\n", frev, crev);
			return -EINVAL;
		}
		return 0;
	}
	return -EINVAL;
}

#define MEM_ID_MASK           0xff000000
#define MEM_ID_SHIFT          24
#define CLOCK_RANGE_MASK      0x00ffffff
#define CLOCK_RANGE_SHIFT     0
#define LOW_NIBBLE_MASK       0xf
#define DATA_EQU_PREV         0
#define DATA_FROM_TABLE       4

int radeon_atom_init_mc_reg_table(struct radeon_device *rdev,
				  u8 module_index,
				  struct atom_mc_reg_table *reg_table)
{
	int index = GetIndexIntoMasterTable(DATA, VRAM_Info);
	u8 frev, crev, num_entries, t_mem_id, num_ranges = 0;
	u32 i = 0, j;
	u16 data_offset, size;
	union vram_info *vram_info;

	memset(reg_table, 0, sizeof(struct atom_mc_reg_table));

	if (atom_parse_data_header(rdev->mode_info.atom_context, index, &size,
				   &frev, &crev, &data_offset)) {
		vram_info = (union vram_info *)
			(rdev->mode_info.atom_context->bios + data_offset);
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
	bios_6_scratch |= ATOM_S6_ACC_BLOCK_DISPLAY_SWITCH;

	/* clear the vbios dpms state */
	if (ASIC_IS_DCE4(rdev))
		bios_2_scratch &= ~ATOM_S2_DEVICE_DPMS_STATE;

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

	if (lock) {
		bios_6_scratch |= ATOM_S6_CRITICAL_STATE;
		bios_6_scratch &= ~ATOM_S6_ACC_MODE;
	} else {
		bios_6_scratch &= ~ATOM_S6_CRITICAL_STATE;
		bios_6_scratch |= ATOM_S6_ACC_MODE;
	}

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
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP6_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP6_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP6 connected\n");
			bios_0_scratch |= ATOM_S0_DFP6;
			bios_3_scratch |= ATOM_S3_DFP6_ACTIVE;
			bios_6_scratch |= ATOM_S6_ACC_REQ_DFP6;
		} else {
			DRM_DEBUG_KMS("DFP6 disconnected\n");
			bios_0_scratch &= ~ATOM_S0_DFP6;
			bios_3_scratch &= ~ATOM_S3_DFP6_ACTIVE;
			bios_6_scratch &= ~ATOM_S6_ACC_REQ_DFP6;
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

	if (ASIC_IS_DCE4(rdev))
		return;

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

	if (ASIC_IS_DCE4(rdev))
		return;

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
