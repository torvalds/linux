/*
 * Copyright 2004 ATI Technologies Inc., Markham, Ontario
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

#ifdef CONFIG_PPC_PMAC
/* not sure which of these are needed */
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif /* CONFIG_PPC_PMAC */

/* from radeon_encoder.c */
extern uint32_t
radeon_get_encoder_enum(struct drm_device *dev, uint32_t supported_device,
			uint8_t dac);
extern void radeon_link_encoder_connector(struct drm_device *dev);

/* from radeon_connector.c */
extern void
radeon_add_legacy_connector(struct drm_device *dev,
			    uint32_t connector_id,
			    uint32_t supported_device,
			    int connector_type,
			    struct radeon_i2c_bus_rec *i2c_bus,
			    uint16_t connector_object_id,
			    struct radeon_hpd *hpd);

/* from radeon_legacy_encoder.c */
extern void
radeon_add_legacy_encoder(struct drm_device *dev, uint32_t encoder_enum,
			  uint32_t supported_device);

/* old legacy ATI BIOS routines */

/* COMBIOS table offsets */
enum radeon_combios_table_offset {
	/* absolute offset tables */
	COMBIOS_ASIC_INIT_1_TABLE,
	COMBIOS_BIOS_SUPPORT_TABLE,
	COMBIOS_DAC_PROGRAMMING_TABLE,
	COMBIOS_MAX_COLOR_DEPTH_TABLE,
	COMBIOS_CRTC_INFO_TABLE,
	COMBIOS_PLL_INFO_TABLE,
	COMBIOS_TV_INFO_TABLE,
	COMBIOS_DFP_INFO_TABLE,
	COMBIOS_HW_CONFIG_INFO_TABLE,
	COMBIOS_MULTIMEDIA_INFO_TABLE,
	COMBIOS_TV_STD_PATCH_TABLE,
	COMBIOS_LCD_INFO_TABLE,
	COMBIOS_MOBILE_INFO_TABLE,
	COMBIOS_PLL_INIT_TABLE,
	COMBIOS_MEM_CONFIG_TABLE,
	COMBIOS_SAVE_MASK_TABLE,
	COMBIOS_HARDCODED_EDID_TABLE,
	COMBIOS_ASIC_INIT_2_TABLE,
	COMBIOS_CONNECTOR_INFO_TABLE,
	COMBIOS_DYN_CLK_1_TABLE,
	COMBIOS_RESERVED_MEM_TABLE,
	COMBIOS_EXT_TMDS_INFO_TABLE,
	COMBIOS_MEM_CLK_INFO_TABLE,
	COMBIOS_EXT_DAC_INFO_TABLE,
	COMBIOS_MISC_INFO_TABLE,
	COMBIOS_CRT_INFO_TABLE,
	COMBIOS_INTEGRATED_SYSTEM_INFO_TABLE,
	COMBIOS_COMPONENT_VIDEO_INFO_TABLE,
	COMBIOS_FAN_SPEED_INFO_TABLE,
	COMBIOS_OVERDRIVE_INFO_TABLE,
	COMBIOS_OEM_INFO_TABLE,
	COMBIOS_DYN_CLK_2_TABLE,
	COMBIOS_POWER_CONNECTOR_INFO_TABLE,
	COMBIOS_I2C_INFO_TABLE,
	/* relative offset tables */
	COMBIOS_ASIC_INIT_3_TABLE,	/* offset from misc info */
	COMBIOS_ASIC_INIT_4_TABLE,	/* offset from misc info */
	COMBIOS_DETECTED_MEM_TABLE,	/* offset from misc info */
	COMBIOS_ASIC_INIT_5_TABLE,	/* offset from misc info */
	COMBIOS_RAM_RESET_TABLE,	/* offset from mem config */
	COMBIOS_POWERPLAY_INFO_TABLE,	/* offset from mobile info */
	COMBIOS_GPIO_INFO_TABLE,	/* offset from mobile info */
	COMBIOS_LCD_DDC_INFO_TABLE,	/* offset from mobile info */
	COMBIOS_TMDS_POWER_TABLE,	/* offset from mobile info */
	COMBIOS_TMDS_POWER_ON_TABLE,	/* offset from tmds power */
	COMBIOS_TMDS_POWER_OFF_TABLE,	/* offset from tmds power */
};

enum radeon_combios_ddc {
	DDC_NONE_DETECTED,
	DDC_MONID,
	DDC_DVI,
	DDC_VGA,
	DDC_CRT2,
	DDC_LCD,
	DDC_GPIO,
};

enum radeon_combios_connector {
	CONNECTOR_NONE_LEGACY,
	CONNECTOR_PROPRIETARY_LEGACY,
	CONNECTOR_CRT_LEGACY,
	CONNECTOR_DVI_I_LEGACY,
	CONNECTOR_DVI_D_LEGACY,
	CONNECTOR_CTV_LEGACY,
	CONNECTOR_STV_LEGACY,
	CONNECTOR_UNSUPPORTED_LEGACY
};

const int legacy_connector_convert[] = {
	DRM_MODE_CONNECTOR_Unknown,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_VGA,
	DRM_MODE_CONNECTOR_DVII,
	DRM_MODE_CONNECTOR_DVID,
	DRM_MODE_CONNECTOR_Composite,
	DRM_MODE_CONNECTOR_SVIDEO,
	DRM_MODE_CONNECTOR_Unknown,
};

static uint16_t combios_get_table_offset(struct drm_device *dev,
					 enum radeon_combios_table_offset table)
{
	struct radeon_device *rdev = dev->dev_private;
	int rev;
	uint16_t offset = 0, check_offset;

	if (!rdev->bios)
		return 0;

	switch (table) {
		/* absolute offset tables */
	case COMBIOS_ASIC_INIT_1_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0xc);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_BIOS_SUPPORT_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x14);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_DAC_PROGRAMMING_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x2a);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MAX_COLOR_DEPTH_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x2c);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_CRTC_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x2e);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_PLL_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x30);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_TV_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x32);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_DFP_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x34);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_HW_CONFIG_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x36);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MULTIMEDIA_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x38);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_TV_STD_PATCH_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x3e);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_LCD_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x40);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MOBILE_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x42);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_PLL_INIT_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x46);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MEM_CONFIG_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x48);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_SAVE_MASK_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x4a);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_HARDCODED_EDID_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x4c);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_ASIC_INIT_2_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x4e);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_CONNECTOR_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x50);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_DYN_CLK_1_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x52);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_RESERVED_MEM_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x54);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_EXT_TMDS_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x58);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MEM_CLK_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x5a);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_EXT_DAC_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x5c);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_MISC_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x5e);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_CRT_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x60);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_INTEGRATED_SYSTEM_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x62);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_COMPONENT_VIDEO_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x64);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_FAN_SPEED_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x66);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_OVERDRIVE_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x68);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_OEM_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x6a);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_DYN_CLK_2_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x6c);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_POWER_CONNECTOR_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x6e);
		if (check_offset)
			offset = check_offset;
		break;
	case COMBIOS_I2C_INFO_TABLE:
		check_offset = RBIOS16(rdev->bios_header_start + 0x70);
		if (check_offset)
			offset = check_offset;
		break;
		/* relative offset tables */
	case COMBIOS_ASIC_INIT_3_TABLE:	/* offset from misc info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MISC_INFO_TABLE);
		if (check_offset) {
			rev = RBIOS8(check_offset);
			if (rev > 0) {
				check_offset = RBIOS16(check_offset + 0x3);
				if (check_offset)
					offset = check_offset;
			}
		}
		break;
	case COMBIOS_ASIC_INIT_4_TABLE:	/* offset from misc info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MISC_INFO_TABLE);
		if (check_offset) {
			rev = RBIOS8(check_offset);
			if (rev > 0) {
				check_offset = RBIOS16(check_offset + 0x5);
				if (check_offset)
					offset = check_offset;
			}
		}
		break;
	case COMBIOS_DETECTED_MEM_TABLE:	/* offset from misc info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MISC_INFO_TABLE);
		if (check_offset) {
			rev = RBIOS8(check_offset);
			if (rev > 0) {
				check_offset = RBIOS16(check_offset + 0x7);
				if (check_offset)
					offset = check_offset;
			}
		}
		break;
	case COMBIOS_ASIC_INIT_5_TABLE:	/* offset from misc info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MISC_INFO_TABLE);
		if (check_offset) {
			rev = RBIOS8(check_offset);
			if (rev == 2) {
				check_offset = RBIOS16(check_offset + 0x9);
				if (check_offset)
					offset = check_offset;
			}
		}
		break;
	case COMBIOS_RAM_RESET_TABLE:	/* offset from mem config */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MEM_CONFIG_TABLE);
		if (check_offset) {
			while (RBIOS8(check_offset++));
			check_offset += 2;
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_POWERPLAY_INFO_TABLE:	/* offset from mobile info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MOBILE_INFO_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x11);
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_GPIO_INFO_TABLE:	/* offset from mobile info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MOBILE_INFO_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x13);
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_LCD_DDC_INFO_TABLE:	/* offset from mobile info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MOBILE_INFO_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x15);
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_TMDS_POWER_TABLE:	/* offset from mobile info */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_MOBILE_INFO_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x17);
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_TMDS_POWER_ON_TABLE:	/* offset from tmds power */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_TMDS_POWER_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x2);
			if (check_offset)
				offset = check_offset;
		}
		break;
	case COMBIOS_TMDS_POWER_OFF_TABLE:	/* offset from tmds power */
		check_offset =
		    combios_get_table_offset(dev, COMBIOS_TMDS_POWER_TABLE);
		if (check_offset) {
			check_offset = RBIOS16(check_offset + 0x4);
			if (check_offset)
				offset = check_offset;
		}
		break;
	default:
		break;
	}

	return offset;

}

bool radeon_combios_check_hardcoded_edid(struct radeon_device *rdev)
{
	int edid_info, size;
	struct edid *edid;
	unsigned char *raw;
	edid_info = combios_get_table_offset(rdev->ddev, COMBIOS_HARDCODED_EDID_TABLE);
	if (!edid_info)
		return false;

	raw = rdev->bios + edid_info;
	size = EDID_LENGTH * (raw[0x7e] + 1);
	edid = kmalloc(size, GFP_KERNEL);
	if (edid == NULL)
		return false;

	memcpy((unsigned char *)edid, raw, size);

	if (!drm_edid_is_valid(edid)) {
		kfree(edid);
		return false;
	}

	rdev->mode_info.bios_hardcoded_edid = edid;
	rdev->mode_info.bios_hardcoded_edid_size = size;
	return true;
}

/* this is used for atom LCDs as well */
struct edid *
radeon_bios_get_hardcoded_edid(struct radeon_device *rdev)
{
	struct edid *edid;

	if (rdev->mode_info.bios_hardcoded_edid) {
		edid = kmalloc(rdev->mode_info.bios_hardcoded_edid_size, GFP_KERNEL);
		if (edid) {
			memcpy((unsigned char *)edid,
			       (unsigned char *)rdev->mode_info.bios_hardcoded_edid,
			       rdev->mode_info.bios_hardcoded_edid_size);
			return edid;
		}
	}
	return NULL;
}

static struct radeon_i2c_bus_rec combios_setup_i2c_bus(struct radeon_device *rdev,
						       enum radeon_combios_ddc ddc,
						       u32 clk_mask,
						       u32 data_mask)
{
	struct radeon_i2c_bus_rec i2c;
	int ddc_line = 0;

	/* ddc id            = mask reg
	 * DDC_NONE_DETECTED = none
	 * DDC_DVI           = RADEON_GPIO_DVI_DDC
	 * DDC_VGA           = RADEON_GPIO_VGA_DDC
	 * DDC_LCD           = RADEON_GPIOPAD_MASK
	 * DDC_GPIO          = RADEON_MDGPIO_MASK
	 * r1xx
	 * DDC_MONID         = RADEON_GPIO_MONID
	 * DDC_CRT2          = RADEON_GPIO_CRT2_DDC
	 * r200
	 * DDC_MONID         = RADEON_GPIO_MONID
	 * DDC_CRT2          = RADEON_GPIO_DVI_DDC
	 * r300/r350
	 * DDC_MONID         = RADEON_GPIO_DVI_DDC
	 * DDC_CRT2          = RADEON_GPIO_DVI_DDC
	 * rv2xx/rv3xx
	 * DDC_MONID         = RADEON_GPIO_MONID
	 * DDC_CRT2          = RADEON_GPIO_MONID
	 * rs3xx/rs4xx
	 * DDC_MONID         = RADEON_GPIOPAD_MASK
	 * DDC_CRT2          = RADEON_GPIO_MONID
	 */
	switch (ddc) {
	case DDC_NONE_DETECTED:
	default:
		ddc_line = 0;
		break;
	case DDC_DVI:
		ddc_line = RADEON_GPIO_DVI_DDC;
		break;
	case DDC_VGA:
		ddc_line = RADEON_GPIO_VGA_DDC;
		break;
	case DDC_LCD:
		ddc_line = RADEON_GPIOPAD_MASK;
		break;
	case DDC_GPIO:
		ddc_line = RADEON_MDGPIO_MASK;
		break;
	case DDC_MONID:
		if (rdev->family == CHIP_RS300 ||
		    rdev->family == CHIP_RS400 ||
		    rdev->family == CHIP_RS480)
			ddc_line = RADEON_GPIOPAD_MASK;
		else if (rdev->family == CHIP_R300 ||
			 rdev->family == CHIP_R350) {
			ddc_line = RADEON_GPIO_DVI_DDC;
			ddc = DDC_DVI;
		} else
			ddc_line = RADEON_GPIO_MONID;
		break;
	case DDC_CRT2:
		if (rdev->family == CHIP_R200 ||
		    rdev->family == CHIP_R300 ||
		    rdev->family == CHIP_R350) {
			ddc_line = RADEON_GPIO_DVI_DDC;
			ddc = DDC_DVI;
		} else if (rdev->family == CHIP_RS300 ||
			   rdev->family == CHIP_RS400 ||
			   rdev->family == CHIP_RS480)
			ddc_line = RADEON_GPIO_MONID;
		else if (rdev->family >= CHIP_RV350) {
			ddc_line = RADEON_GPIO_MONID;
			ddc = DDC_MONID;
		} else
			ddc_line = RADEON_GPIO_CRT2_DDC;
		break;
	}

	if (ddc_line == RADEON_GPIOPAD_MASK) {
		i2c.mask_clk_reg = RADEON_GPIOPAD_MASK;
		i2c.mask_data_reg = RADEON_GPIOPAD_MASK;
		i2c.a_clk_reg = RADEON_GPIOPAD_A;
		i2c.a_data_reg = RADEON_GPIOPAD_A;
		i2c.en_clk_reg = RADEON_GPIOPAD_EN;
		i2c.en_data_reg = RADEON_GPIOPAD_EN;
		i2c.y_clk_reg = RADEON_GPIOPAD_Y;
		i2c.y_data_reg = RADEON_GPIOPAD_Y;
	} else if (ddc_line == RADEON_MDGPIO_MASK) {
		i2c.mask_clk_reg = RADEON_MDGPIO_MASK;
		i2c.mask_data_reg = RADEON_MDGPIO_MASK;
		i2c.a_clk_reg = RADEON_MDGPIO_A;
		i2c.a_data_reg = RADEON_MDGPIO_A;
		i2c.en_clk_reg = RADEON_MDGPIO_EN;
		i2c.en_data_reg = RADEON_MDGPIO_EN;
		i2c.y_clk_reg = RADEON_MDGPIO_Y;
		i2c.y_data_reg = RADEON_MDGPIO_Y;
	} else {
		i2c.mask_clk_reg = ddc_line;
		i2c.mask_data_reg = ddc_line;
		i2c.a_clk_reg = ddc_line;
		i2c.a_data_reg = ddc_line;
		i2c.en_clk_reg = ddc_line;
		i2c.en_data_reg = ddc_line;
		i2c.y_clk_reg = ddc_line;
		i2c.y_data_reg = ddc_line;
	}

	if (clk_mask && data_mask) {
		/* system specific masks */
		i2c.mask_clk_mask = clk_mask;
		i2c.mask_data_mask = data_mask;
		i2c.a_clk_mask = clk_mask;
		i2c.a_data_mask = data_mask;
		i2c.en_clk_mask = clk_mask;
		i2c.en_data_mask = data_mask;
		i2c.y_clk_mask = clk_mask;
		i2c.y_data_mask = data_mask;
	} else if ((ddc_line == RADEON_GPIOPAD_MASK) ||
		   (ddc_line == RADEON_MDGPIO_MASK)) {
		/* default gpiopad masks */
		i2c.mask_clk_mask = (0x20 << 8);
		i2c.mask_data_mask = 0x80;
		i2c.a_clk_mask = (0x20 << 8);
		i2c.a_data_mask = 0x80;
		i2c.en_clk_mask = (0x20 << 8);
		i2c.en_data_mask = 0x80;
		i2c.y_clk_mask = (0x20 << 8);
		i2c.y_data_mask = 0x80;
	} else {
		/* default masks for ddc pads */
		i2c.mask_clk_mask = RADEON_GPIO_EN_1;
		i2c.mask_data_mask = RADEON_GPIO_EN_0;
		i2c.a_clk_mask = RADEON_GPIO_A_1;
		i2c.a_data_mask = RADEON_GPIO_A_0;
		i2c.en_clk_mask = RADEON_GPIO_EN_1;
		i2c.en_data_mask = RADEON_GPIO_EN_0;
		i2c.y_clk_mask = RADEON_GPIO_Y_1;
		i2c.y_data_mask = RADEON_GPIO_Y_0;
	}

	switch (rdev->family) {
	case CHIP_R100:
	case CHIP_RV100:
	case CHIP_RS100:
	case CHIP_RV200:
	case CHIP_RS200:
	case CHIP_RS300:
		switch (ddc_line) {
		case RADEON_GPIO_DVI_DDC:
			i2c.hw_capable = true;
			break;
		default:
			i2c.hw_capable = false;
			break;
		}
		break;
	case CHIP_R200:
		switch (ddc_line) {
		case RADEON_GPIO_DVI_DDC:
		case RADEON_GPIO_MONID:
			i2c.hw_capable = true;
			break;
		default:
			i2c.hw_capable = false;
			break;
		}
		break;
	case CHIP_RV250:
	case CHIP_RV280:
		switch (ddc_line) {
		case RADEON_GPIO_VGA_DDC:
		case RADEON_GPIO_DVI_DDC:
		case RADEON_GPIO_CRT2_DDC:
			i2c.hw_capable = true;
			break;
		default:
			i2c.hw_capable = false;
			break;
		}
		break;
	case CHIP_R300:
	case CHIP_R350:
		switch (ddc_line) {
		case RADEON_GPIO_VGA_DDC:
		case RADEON_GPIO_DVI_DDC:
			i2c.hw_capable = true;
			break;
		default:
			i2c.hw_capable = false;
			break;
		}
		break;
	case CHIP_RV350:
	case CHIP_RV380:
	case CHIP_RS400:
	case CHIP_RS480:
		switch (ddc_line) {
		case RADEON_GPIO_VGA_DDC:
		case RADEON_GPIO_DVI_DDC:
			i2c.hw_capable = true;
			break;
		case RADEON_GPIO_MONID:
			/* hw i2c on RADEON_GPIO_MONID doesn't seem to work
			 * reliably on some pre-r4xx hardware; not sure why.
			 */
			i2c.hw_capable = false;
			break;
		default:
			i2c.hw_capable = false;
			break;
		}
		break;
	default:
		i2c.hw_capable = false;
		break;
	}
	i2c.mm_i2c = false;

	i2c.i2c_id = ddc;
	i2c.hpd = RADEON_HPD_NONE;

	if (ddc_line)
		i2c.valid = true;
	else
		i2c.valid = false;

	return i2c;
}

void radeon_combios_i2c_init(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	struct radeon_i2c_bus_rec i2c;

	/* actual hw pads
	 * r1xx/rs2xx/rs3xx
	 * 0x60, 0x64, 0x68, 0x6c, gpiopads, mm
	 * r200
	 * 0x60, 0x64, 0x68, mm
	 * r300/r350
	 * 0x60, 0x64, mm
	 * rv2xx/rv3xx/rs4xx
	 * 0x60, 0x64, 0x68, gpiopads, mm
	 */

	/* 0x60 */
	i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
	rdev->i2c_bus[0] = radeon_i2c_create(dev, &i2c, "DVI_DDC");
	/* 0x64 */
	i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
	rdev->i2c_bus[1] = radeon_i2c_create(dev, &i2c, "VGA_DDC");

	/* mm i2c */
	i2c.valid = true;
	i2c.hw_capable = true;
	i2c.mm_i2c = true;
	i2c.i2c_id = 0xa0;
	rdev->i2c_bus[2] = radeon_i2c_create(dev, &i2c, "MM_I2C");

	if (rdev->family == CHIP_R300 ||
	    rdev->family == CHIP_R350) {
		/* only 2 sw i2c pads */
	} else if (rdev->family == CHIP_RS300 ||
		   rdev->family == CHIP_RS400 ||
		   rdev->family == CHIP_RS480) {
		u16 offset;
		u8 id, blocks, clk, data;
		int i;

		/* 0x68 */
		i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		rdev->i2c_bus[3] = radeon_i2c_create(dev, &i2c, "MONID");

		offset = combios_get_table_offset(dev, COMBIOS_I2C_INFO_TABLE);
		if (offset) {
			blocks = RBIOS8(offset + 2);
			for (i = 0; i < blocks; i++) {
				id = RBIOS8(offset + 3 + (i * 5) + 0);
				if (id == 136) {
					clk = RBIOS8(offset + 3 + (i * 5) + 3);
					data = RBIOS8(offset + 3 + (i * 5) + 4);
					/* gpiopad */
					i2c = combios_setup_i2c_bus(rdev, DDC_MONID,
								    (1 << clk), (1 << data));
					rdev->i2c_bus[4] = radeon_i2c_create(dev, &i2c, "GPIOPAD_MASK");
					break;
				}
			}
		}
	} else if ((rdev->family == CHIP_R200) ||
		   (rdev->family >= CHIP_R300)) {
		/* 0x68 */
		i2c = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
		rdev->i2c_bus[3] = radeon_i2c_create(dev, &i2c, "MONID");
	} else {
		/* 0x68 */
		i2c = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
		rdev->i2c_bus[3] = radeon_i2c_create(dev, &i2c, "MONID");
		/* 0x6c */
		i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		rdev->i2c_bus[4] = radeon_i2c_create(dev, &i2c, "CRT2_DDC");
	}
}

bool radeon_combios_get_clock_info(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint16_t pll_info;
	struct radeon_pll *p1pll = &rdev->clock.p1pll;
	struct radeon_pll *p2pll = &rdev->clock.p2pll;
	struct radeon_pll *spll = &rdev->clock.spll;
	struct radeon_pll *mpll = &rdev->clock.mpll;
	int8_t rev;
	uint16_t sclk, mclk;

	pll_info = combios_get_table_offset(dev, COMBIOS_PLL_INFO_TABLE);
	if (pll_info) {
		rev = RBIOS8(pll_info);

		/* pixel clocks */
		p1pll->reference_freq = RBIOS16(pll_info + 0xe);
		p1pll->reference_div = RBIOS16(pll_info + 0x10);
		p1pll->pll_out_min = RBIOS32(pll_info + 0x12);
		p1pll->pll_out_max = RBIOS32(pll_info + 0x16);
		p1pll->lcd_pll_out_min = p1pll->pll_out_min;
		p1pll->lcd_pll_out_max = p1pll->pll_out_max;

		if (rev > 9) {
			p1pll->pll_in_min = RBIOS32(pll_info + 0x36);
			p1pll->pll_in_max = RBIOS32(pll_info + 0x3a);
		} else {
			p1pll->pll_in_min = 40;
			p1pll->pll_in_max = 500;
		}
		*p2pll = *p1pll;

		/* system clock */
		spll->reference_freq = RBIOS16(pll_info + 0x1a);
		spll->reference_div = RBIOS16(pll_info + 0x1c);
		spll->pll_out_min = RBIOS32(pll_info + 0x1e);
		spll->pll_out_max = RBIOS32(pll_info + 0x22);

		if (rev > 10) {
			spll->pll_in_min = RBIOS32(pll_info + 0x48);
			spll->pll_in_max = RBIOS32(pll_info + 0x4c);
		} else {
			/* ??? */
			spll->pll_in_min = 40;
			spll->pll_in_max = 500;
		}

		/* memory clock */
		mpll->reference_freq = RBIOS16(pll_info + 0x26);
		mpll->reference_div = RBIOS16(pll_info + 0x28);
		mpll->pll_out_min = RBIOS32(pll_info + 0x2a);
		mpll->pll_out_max = RBIOS32(pll_info + 0x2e);

		if (rev > 10) {
			mpll->pll_in_min = RBIOS32(pll_info + 0x5a);
			mpll->pll_in_max = RBIOS32(pll_info + 0x5e);
		} else {
			/* ??? */
			mpll->pll_in_min = 40;
			mpll->pll_in_max = 500;
		}

		/* default sclk/mclk */
		sclk = RBIOS16(pll_info + 0xa);
		mclk = RBIOS16(pll_info + 0x8);
		if (sclk == 0)
			sclk = 200 * 100;
		if (mclk == 0)
			mclk = 200 * 100;

		rdev->clock.default_sclk = sclk;
		rdev->clock.default_mclk = mclk;

		if (RBIOS32(pll_info + 0x16))
			rdev->clock.max_pixel_clock = RBIOS32(pll_info + 0x16);
		else
			rdev->clock.max_pixel_clock = 35000; /* might need something asic specific */

		return true;
	}
	return false;
}

bool radeon_combios_sideport_present(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	u16 igp_info;

	/* sideport is AMD only */
	if (rdev->family == CHIP_RS400)
		return false;

	igp_info = combios_get_table_offset(dev, COMBIOS_INTEGRATED_SYSTEM_INFO_TABLE);

	if (igp_info) {
		if (RBIOS16(igp_info + 0x4))
			return true;
	}
	return false;
}

static const uint32_t default_primarydac_adj[CHIP_LAST] = {
	0x00000808,		/* r100  */
	0x00000808,		/* rv100 */
	0x00000808,		/* rs100 */
	0x00000808,		/* rv200 */
	0x00000808,		/* rs200 */
	0x00000808,		/* r200  */
	0x00000808,		/* rv250 */
	0x00000000,		/* rs300 */
	0x00000808,		/* rv280 */
	0x00000808,		/* r300  */
	0x00000808,		/* r350  */
	0x00000808,		/* rv350 */
	0x00000808,		/* rv380 */
	0x00000808,		/* r420  */
	0x00000808,		/* r423  */
	0x00000808,		/* rv410 */
	0x00000000,		/* rs400 */
	0x00000000,		/* rs480 */
};

static void radeon_legacy_get_primary_dac_info_from_table(struct radeon_device *rdev,
							  struct radeon_encoder_primary_dac *p_dac)
{
	p_dac->ps2_pdac_adj = default_primarydac_adj[rdev->family];
	return;
}

struct radeon_encoder_primary_dac *radeon_combios_get_primary_dac_info(struct
								       radeon_encoder
								       *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint16_t dac_info;
	uint8_t rev, bg, dac;
	struct radeon_encoder_primary_dac *p_dac = NULL;
	int found = 0;

	p_dac = kzalloc(sizeof(struct radeon_encoder_primary_dac),
			GFP_KERNEL);

	if (!p_dac)
		return NULL;

	/* check CRT table */
	dac_info = combios_get_table_offset(dev, COMBIOS_CRT_INFO_TABLE);
	if (dac_info) {
		rev = RBIOS8(dac_info) & 0x3;
		if (rev < 2) {
			bg = RBIOS8(dac_info + 0x2) & 0xf;
			dac = (RBIOS8(dac_info + 0x2) >> 4) & 0xf;
			p_dac->ps2_pdac_adj = (bg << 8) | (dac);
		} else {
			bg = RBIOS8(dac_info + 0x2) & 0xf;
			dac = RBIOS8(dac_info + 0x3) & 0xf;
			p_dac->ps2_pdac_adj = (bg << 8) | (dac);
		}
		/* if the values are all zeros, use the table */
		if (p_dac->ps2_pdac_adj)
			found = 1;
	}

	if (!found) /* fallback to defaults */
		radeon_legacy_get_primary_dac_info_from_table(rdev, p_dac);

	return p_dac;
}

enum radeon_tv_std
radeon_combios_get_tv_info(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	uint16_t tv_info;
	enum radeon_tv_std tv_std = TV_STD_NTSC;

	tv_info = combios_get_table_offset(dev, COMBIOS_TV_INFO_TABLE);
	if (tv_info) {
		if (RBIOS8(tv_info + 6) == 'T') {
			switch (RBIOS8(tv_info + 7) & 0xf) {
			case 1:
				tv_std = TV_STD_NTSC;
				DRM_DEBUG_KMS("Default TV standard: NTSC\n");
				break;
			case 2:
				tv_std = TV_STD_PAL;
				DRM_DEBUG_KMS("Default TV standard: PAL\n");
				break;
			case 3:
				tv_std = TV_STD_PAL_M;
				DRM_DEBUG_KMS("Default TV standard: PAL-M\n");
				break;
			case 4:
				tv_std = TV_STD_PAL_60;
				DRM_DEBUG_KMS("Default TV standard: PAL-60\n");
				break;
			case 5:
				tv_std = TV_STD_NTSC_J;
				DRM_DEBUG_KMS("Default TV standard: NTSC-J\n");
				break;
			case 6:
				tv_std = TV_STD_SCART_PAL;
				DRM_DEBUG_KMS("Default TV standard: SCART-PAL\n");
				break;
			default:
				tv_std = TV_STD_NTSC;
				DRM_DEBUG_KMS
				    ("Unknown TV standard; defaulting to NTSC\n");
				break;
			}

			switch ((RBIOS8(tv_info + 9) >> 2) & 0x3) {
			case 0:
				DRM_DEBUG_KMS("29.498928713 MHz TV ref clk\n");
				break;
			case 1:
				DRM_DEBUG_KMS("28.636360000 MHz TV ref clk\n");
				break;
			case 2:
				DRM_DEBUG_KMS("14.318180000 MHz TV ref clk\n");
				break;
			case 3:
				DRM_DEBUG_KMS("27.000000000 MHz TV ref clk\n");
				break;
			default:
				break;
			}
		}
	}
	return tv_std;
}

static const uint32_t default_tvdac_adj[CHIP_LAST] = {
	0x00000000,		/* r100  */
	0x00280000,		/* rv100 */
	0x00000000,		/* rs100 */
	0x00880000,		/* rv200 */
	0x00000000,		/* rs200 */
	0x00000000,		/* r200  */
	0x00770000,		/* rv250 */
	0x00290000,		/* rs300 */
	0x00560000,		/* rv280 */
	0x00780000,		/* r300  */
	0x00770000,		/* r350  */
	0x00780000,		/* rv350 */
	0x00780000,		/* rv380 */
	0x01080000,		/* r420  */
	0x01080000,		/* r423  */
	0x01080000,		/* rv410 */
	0x00780000,		/* rs400 */
	0x00780000,		/* rs480 */
};

static void radeon_legacy_get_tv_dac_info_from_table(struct radeon_device *rdev,
						     struct radeon_encoder_tv_dac *tv_dac)
{
	tv_dac->ps2_tvdac_adj = default_tvdac_adj[rdev->family];
	if ((rdev->flags & RADEON_IS_MOBILITY) && (rdev->family == CHIP_RV250))
		tv_dac->ps2_tvdac_adj = 0x00880000;
	tv_dac->pal_tvdac_adj = tv_dac->ps2_tvdac_adj;
	tv_dac->ntsc_tvdac_adj = tv_dac->ps2_tvdac_adj;
	return;
}

struct radeon_encoder_tv_dac *radeon_combios_get_tv_dac_info(struct
							     radeon_encoder
							     *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint16_t dac_info;
	uint8_t rev, bg, dac;
	struct radeon_encoder_tv_dac *tv_dac = NULL;
	int found = 0;

	tv_dac = kzalloc(sizeof(struct radeon_encoder_tv_dac), GFP_KERNEL);
	if (!tv_dac)
		return NULL;

	/* first check TV table */
	dac_info = combios_get_table_offset(dev, COMBIOS_TV_INFO_TABLE);
	if (dac_info) {
		rev = RBIOS8(dac_info + 0x3);
		if (rev > 4) {
			bg = RBIOS8(dac_info + 0xc) & 0xf;
			dac = RBIOS8(dac_info + 0xd) & 0xf;
			tv_dac->ps2_tvdac_adj = (bg << 16) | (dac << 20);

			bg = RBIOS8(dac_info + 0xe) & 0xf;
			dac = RBIOS8(dac_info + 0xf) & 0xf;
			tv_dac->pal_tvdac_adj = (bg << 16) | (dac << 20);

			bg = RBIOS8(dac_info + 0x10) & 0xf;
			dac = RBIOS8(dac_info + 0x11) & 0xf;
			tv_dac->ntsc_tvdac_adj = (bg << 16) | (dac << 20);
			/* if the values are all zeros, use the table */
			if (tv_dac->ps2_tvdac_adj)
				found = 1;
		} else if (rev > 1) {
			bg = RBIOS8(dac_info + 0xc) & 0xf;
			dac = (RBIOS8(dac_info + 0xc) >> 4) & 0xf;
			tv_dac->ps2_tvdac_adj = (bg << 16) | (dac << 20);

			bg = RBIOS8(dac_info + 0xd) & 0xf;
			dac = (RBIOS8(dac_info + 0xd) >> 4) & 0xf;
			tv_dac->pal_tvdac_adj = (bg << 16) | (dac << 20);

			bg = RBIOS8(dac_info + 0xe) & 0xf;
			dac = (RBIOS8(dac_info + 0xe) >> 4) & 0xf;
			tv_dac->ntsc_tvdac_adj = (bg << 16) | (dac << 20);
			/* if the values are all zeros, use the table */
			if (tv_dac->ps2_tvdac_adj)
				found = 1;
		}
		tv_dac->tv_std = radeon_combios_get_tv_info(rdev);
	}
	if (!found) {
		/* then check CRT table */
		dac_info =
		    combios_get_table_offset(dev, COMBIOS_CRT_INFO_TABLE);
		if (dac_info) {
			rev = RBIOS8(dac_info) & 0x3;
			if (rev < 2) {
				bg = RBIOS8(dac_info + 0x3) & 0xf;
				dac = (RBIOS8(dac_info + 0x3) >> 4) & 0xf;
				tv_dac->ps2_tvdac_adj =
				    (bg << 16) | (dac << 20);
				tv_dac->pal_tvdac_adj = tv_dac->ps2_tvdac_adj;
				tv_dac->ntsc_tvdac_adj = tv_dac->ps2_tvdac_adj;
				/* if the values are all zeros, use the table */
				if (tv_dac->ps2_tvdac_adj)
					found = 1;
			} else {
				bg = RBIOS8(dac_info + 0x4) & 0xf;
				dac = RBIOS8(dac_info + 0x5) & 0xf;
				tv_dac->ps2_tvdac_adj =
				    (bg << 16) | (dac << 20);
				tv_dac->pal_tvdac_adj = tv_dac->ps2_tvdac_adj;
				tv_dac->ntsc_tvdac_adj = tv_dac->ps2_tvdac_adj;
				/* if the values are all zeros, use the table */
				if (tv_dac->ps2_tvdac_adj)
					found = 1;
			}
		} else {
			DRM_INFO("No TV DAC info found in BIOS\n");
		}
	}

	if (!found) /* fallback to defaults */
		radeon_legacy_get_tv_dac_info_from_table(rdev, tv_dac);

	return tv_dac;
}

static struct radeon_encoder_lvds *radeon_legacy_get_lvds_info_from_regs(struct
									 radeon_device
									 *rdev)
{
	struct radeon_encoder_lvds *lvds = NULL;
	uint32_t fp_vert_stretch, fp_horz_stretch;
	uint32_t ppll_div_sel, ppll_val;
	uint32_t lvds_ss_gen_cntl = RREG32(RADEON_LVDS_SS_GEN_CNTL);

	lvds = kzalloc(sizeof(struct radeon_encoder_lvds), GFP_KERNEL);

	if (!lvds)
		return NULL;

	fp_vert_stretch = RREG32(RADEON_FP_VERT_STRETCH);
	fp_horz_stretch = RREG32(RADEON_FP_HORZ_STRETCH);

	/* These should be fail-safe defaults, fingers crossed */
	lvds->panel_pwr_delay = 200;
	lvds->panel_vcc_delay = 2000;

	lvds->lvds_gen_cntl = RREG32(RADEON_LVDS_GEN_CNTL);
	lvds->panel_digon_delay = (lvds_ss_gen_cntl >> RADEON_LVDS_PWRSEQ_DELAY1_SHIFT) & 0xf;
	lvds->panel_blon_delay = (lvds_ss_gen_cntl >> RADEON_LVDS_PWRSEQ_DELAY2_SHIFT) & 0xf;

	if (fp_vert_stretch & RADEON_VERT_STRETCH_ENABLE)
		lvds->native_mode.vdisplay =
		    ((fp_vert_stretch & RADEON_VERT_PANEL_SIZE) >>
		     RADEON_VERT_PANEL_SHIFT) + 1;
	else
		lvds->native_mode.vdisplay =
		    (RREG32(RADEON_CRTC_V_TOTAL_DISP) >> 16) + 1;

	if (fp_horz_stretch & RADEON_HORZ_STRETCH_ENABLE)
		lvds->native_mode.hdisplay =
		    (((fp_horz_stretch & RADEON_HORZ_PANEL_SIZE) >>
		      RADEON_HORZ_PANEL_SHIFT) + 1) * 8;
	else
		lvds->native_mode.hdisplay =
		    ((RREG32(RADEON_CRTC_H_TOTAL_DISP) >> 16) + 1) * 8;

	if ((lvds->native_mode.hdisplay < 640) ||
	    (lvds->native_mode.vdisplay < 480)) {
		lvds->native_mode.hdisplay = 640;
		lvds->native_mode.vdisplay = 480;
	}

	ppll_div_sel = RREG8(RADEON_CLOCK_CNTL_INDEX + 1) & 0x3;
	ppll_val = RREG32_PLL(RADEON_PPLL_DIV_0 + ppll_div_sel);
	if ((ppll_val & 0x000707ff) == 0x1bb)
		lvds->use_bios_dividers = false;
	else {
		lvds->panel_ref_divider =
		    RREG32_PLL(RADEON_PPLL_REF_DIV) & 0x3ff;
		lvds->panel_post_divider = (ppll_val >> 16) & 0x7;
		lvds->panel_fb_divider = ppll_val & 0x7ff;

		if ((lvds->panel_ref_divider != 0) &&
		    (lvds->panel_fb_divider > 3))
			lvds->use_bios_dividers = true;
	}
	lvds->panel_vcc_delay = 200;

	DRM_INFO("Panel info derived from registers\n");
	DRM_INFO("Panel Size %dx%d\n", lvds->native_mode.hdisplay,
		 lvds->native_mode.vdisplay);

	return lvds;
}

struct radeon_encoder_lvds *radeon_combios_get_lvds_info(struct radeon_encoder
							 *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint16_t lcd_info;
	uint32_t panel_setup;
	char stmp[30];
	int tmp, i;
	struct radeon_encoder_lvds *lvds = NULL;

	lcd_info = combios_get_table_offset(dev, COMBIOS_LCD_INFO_TABLE);

	if (lcd_info) {
		lvds = kzalloc(sizeof(struct radeon_encoder_lvds), GFP_KERNEL);

		if (!lvds)
			return NULL;

		for (i = 0; i < 24; i++)
			stmp[i] = RBIOS8(lcd_info + i + 1);
		stmp[24] = 0;

		DRM_INFO("Panel ID String: %s\n", stmp);

		lvds->native_mode.hdisplay = RBIOS16(lcd_info + 0x19);
		lvds->native_mode.vdisplay = RBIOS16(lcd_info + 0x1b);

		DRM_INFO("Panel Size %dx%d\n", lvds->native_mode.hdisplay,
			 lvds->native_mode.vdisplay);

		lvds->panel_vcc_delay = RBIOS16(lcd_info + 0x2c);
		lvds->panel_vcc_delay = min_t(u16, lvds->panel_vcc_delay, 2000);

		lvds->panel_pwr_delay = RBIOS8(lcd_info + 0x24);
		lvds->panel_digon_delay = RBIOS16(lcd_info + 0x38) & 0xf;
		lvds->panel_blon_delay = (RBIOS16(lcd_info + 0x38) >> 4) & 0xf;

		lvds->panel_ref_divider = RBIOS16(lcd_info + 0x2e);
		lvds->panel_post_divider = RBIOS8(lcd_info + 0x30);
		lvds->panel_fb_divider = RBIOS16(lcd_info + 0x31);
		if ((lvds->panel_ref_divider != 0) &&
		    (lvds->panel_fb_divider > 3))
			lvds->use_bios_dividers = true;

		panel_setup = RBIOS32(lcd_info + 0x39);
		lvds->lvds_gen_cntl = 0xff00;
		if (panel_setup & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_PANEL_FORMAT;

		if ((panel_setup >> 4) & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_PANEL_TYPE;

		switch ((panel_setup >> 8) & 0x7) {
		case 0:
			lvds->lvds_gen_cntl |= RADEON_LVDS_NO_FM;
			break;
		case 1:
			lvds->lvds_gen_cntl |= RADEON_LVDS_2_GREY;
			break;
		case 2:
			lvds->lvds_gen_cntl |= RADEON_LVDS_4_GREY;
			break;
		default:
			break;
		}

		if ((panel_setup >> 16) & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_FP_POL_LOW;

		if ((panel_setup >> 17) & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_LP_POL_LOW;

		if ((panel_setup >> 18) & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_DTM_POL_LOW;

		if ((panel_setup >> 23) & 0x1)
			lvds->lvds_gen_cntl |= RADEON_LVDS_BL_CLK_SEL;

		lvds->lvds_gen_cntl |= (panel_setup & 0xf0000000);

		for (i = 0; i < 32; i++) {
			tmp = RBIOS16(lcd_info + 64 + i * 2);
			if (tmp == 0)
				break;

			if ((RBIOS16(tmp) == lvds->native_mode.hdisplay) &&
			    (RBIOS16(tmp + 2) == lvds->native_mode.vdisplay)) {
				lvds->native_mode.htotal = lvds->native_mode.hdisplay +
					(RBIOS16(tmp + 17) - RBIOS16(tmp + 19)) * 8;
				lvds->native_mode.hsync_start = lvds->native_mode.hdisplay +
					(RBIOS16(tmp + 21) - RBIOS16(tmp + 19) - 1) * 8;
				lvds->native_mode.hsync_end = lvds->native_mode.hsync_start +
					(RBIOS8(tmp + 23) * 8);

				lvds->native_mode.vtotal = lvds->native_mode.vdisplay +
					(RBIOS16(tmp + 24) - RBIOS16(tmp + 26));
				lvds->native_mode.vsync_start = lvds->native_mode.vdisplay +
					((RBIOS16(tmp + 28) & 0x7ff) - RBIOS16(tmp + 26));
				lvds->native_mode.vsync_end = lvds->native_mode.vsync_start +
					((RBIOS16(tmp + 28) & 0xf800) >> 11);

				lvds->native_mode.clock = RBIOS16(tmp + 9) * 10;
				lvds->native_mode.flags = 0;
				/* set crtc values */
				drm_mode_set_crtcinfo(&lvds->native_mode, CRTC_INTERLACE_HALVE_V);

			}
		}
	} else {
		DRM_INFO("No panel info found in BIOS\n");
		lvds = radeon_legacy_get_lvds_info_from_regs(rdev);
	}

	if (lvds)
		encoder->native_mode = lvds->native_mode;
	return lvds;
}

static const struct radeon_tmds_pll default_tmds_pll[CHIP_LAST][4] = {
	{{12000, 0xa1b}, {0xffffffff, 0xa3f}, {0, 0}, {0, 0}},	/* CHIP_R100  */
	{{12000, 0xa1b}, {0xffffffff, 0xa3f}, {0, 0}, {0, 0}},	/* CHIP_RV100 */
	{{0, 0}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_RS100 */
	{{15000, 0xa1b}, {0xffffffff, 0xa3f}, {0, 0}, {0, 0}},	/* CHIP_RV200 */
	{{12000, 0xa1b}, {0xffffffff, 0xa3f}, {0, 0}, {0, 0}},	/* CHIP_RS200 */
	{{15000, 0xa1b}, {0xffffffff, 0xa3f}, {0, 0}, {0, 0}},	/* CHIP_R200  */
	{{15500, 0x81b}, {0xffffffff, 0x83f}, {0, 0}, {0, 0}},	/* CHIP_RV250 */
	{{0, 0}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_RS300 */
	{{13000, 0x400f4}, {15000, 0x400f7}, {0xffffffff, 0x40111}, {0, 0}},	/* CHIP_RV280 */
	{{0xffffffff, 0xb01cb}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_R300  */
	{{0xffffffff, 0xb01cb}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_R350  */
	{{15000, 0xb0155}, {0xffffffff, 0xb01cb}, {0, 0}, {0, 0}},	/* CHIP_RV350 */
	{{15000, 0xb0155}, {0xffffffff, 0xb01cb}, {0, 0}, {0, 0}},	/* CHIP_RV380 */
	{{0xffffffff, 0xb01cb}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_R420  */
	{{0xffffffff, 0xb01cb}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_R423  */
	{{0xffffffff, 0xb01cb}, {0, 0}, {0, 0}, {0, 0}},	/* CHIP_RV410 */
	{ {0, 0}, {0, 0}, {0, 0}, {0, 0} },	/* CHIP_RS400 */
	{ {0, 0}, {0, 0}, {0, 0}, {0, 0} },	/* CHIP_RS480 */
};

bool radeon_legacy_get_tmds_info_from_table(struct radeon_encoder *encoder,
					    struct radeon_encoder_int_tmds *tmds)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	int i;

	for (i = 0; i < 4; i++) {
		tmds->tmds_pll[i].value =
			default_tmds_pll[rdev->family][i].value;
		tmds->tmds_pll[i].freq = default_tmds_pll[rdev->family][i].freq;
	}

	return true;
}

bool radeon_legacy_get_tmds_info_from_combios(struct radeon_encoder *encoder,
					      struct radeon_encoder_int_tmds *tmds)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint16_t tmds_info;
	int i, n;
	uint8_t ver;

	tmds_info = combios_get_table_offset(dev, COMBIOS_DFP_INFO_TABLE);

	if (tmds_info) {
		ver = RBIOS8(tmds_info);
		DRM_DEBUG_KMS("DFP table revision: %d\n", ver);
		if (ver == 3) {
			n = RBIOS8(tmds_info + 5) + 1;
			if (n > 4)
				n = 4;
			for (i = 0; i < n; i++) {
				tmds->tmds_pll[i].value =
				    RBIOS32(tmds_info + i * 10 + 0x08);
				tmds->tmds_pll[i].freq =
				    RBIOS16(tmds_info + i * 10 + 0x10);
				DRM_DEBUG_KMS("TMDS PLL From COMBIOS %u %x\n",
					  tmds->tmds_pll[i].freq,
					  tmds->tmds_pll[i].value);
			}
		} else if (ver == 4) {
			int stride = 0;
			n = RBIOS8(tmds_info + 5) + 1;
			if (n > 4)
				n = 4;
			for (i = 0; i < n; i++) {
				tmds->tmds_pll[i].value =
				    RBIOS32(tmds_info + stride + 0x08);
				tmds->tmds_pll[i].freq =
				    RBIOS16(tmds_info + stride + 0x10);
				if (i == 0)
					stride += 10;
				else
					stride += 6;
				DRM_DEBUG_KMS("TMDS PLL From COMBIOS %u %x\n",
					  tmds->tmds_pll[i].freq,
					  tmds->tmds_pll[i].value);
			}
		}
	} else {
		DRM_INFO("No TMDS info found in BIOS\n");
		return false;
	}
	return true;
}

bool radeon_legacy_get_ext_tmds_info_from_table(struct radeon_encoder *encoder,
						struct radeon_encoder_ext_tmds *tmds)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_i2c_bus_rec i2c_bus;

	/* default for macs */
	i2c_bus = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
	tmds->i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);

	/* XXX some macs have duallink chips */
	switch (rdev->mode_info.connector_table) {
	case CT_POWERBOOK_EXTERNAL:
	case CT_MINI_EXTERNAL:
	default:
		tmds->dvo_chip = DVO_SIL164;
		tmds->slave_addr = 0x70 >> 1; /* 7 bit addressing */
		break;
	}

	return true;
}

bool radeon_legacy_get_ext_tmds_info_from_combios(struct radeon_encoder *encoder,
						  struct radeon_encoder_ext_tmds *tmds)
{
	struct drm_device *dev = encoder->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint16_t offset;
	uint8_t ver;
	enum radeon_combios_ddc gpio;
	struct radeon_i2c_bus_rec i2c_bus;

	tmds->i2c_bus = NULL;
	if (rdev->flags & RADEON_IS_IGP) {
		i2c_bus = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
		tmds->i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
		tmds->dvo_chip = DVO_SIL164;
		tmds->slave_addr = 0x70 >> 1; /* 7 bit addressing */
	} else {
		offset = combios_get_table_offset(dev, COMBIOS_EXT_TMDS_INFO_TABLE);
		if (offset) {
			ver = RBIOS8(offset);
			DRM_DEBUG_KMS("External TMDS Table revision: %d\n", ver);
			tmds->slave_addr = RBIOS8(offset + 4 + 2);
			tmds->slave_addr >>= 1; /* 7 bit addressing */
			gpio = RBIOS8(offset + 4 + 3);
			if (gpio == DDC_LCD) {
				/* MM i2c */
				i2c_bus.valid = true;
				i2c_bus.hw_capable = true;
				i2c_bus.mm_i2c = true;
				i2c_bus.i2c_id = 0xa0;
			} else
				i2c_bus = combios_setup_i2c_bus(rdev, gpio, 0, 0);
			tmds->i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
		}
	}

	if (!tmds->i2c_bus) {
		DRM_INFO("No valid Ext TMDS info found in BIOS\n");
		return false;
	}

	return true;
}

bool radeon_get_legacy_connector_info_from_table(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_i2c_bus_rec ddc_i2c;
	struct radeon_hpd hpd;

	rdev->mode_info.connector_table = radeon_connector_table;
	if (rdev->mode_info.connector_table == CT_NONE) {
#ifdef CONFIG_PPC_PMAC
		if (of_machine_is_compatible("PowerBook3,3")) {
			/* powerbook with VGA */
			rdev->mode_info.connector_table = CT_POWERBOOK_VGA;
		} else if (of_machine_is_compatible("PowerBook3,4") ||
			   of_machine_is_compatible("PowerBook3,5")) {
			/* powerbook with internal tmds */
			rdev->mode_info.connector_table = CT_POWERBOOK_INTERNAL;
		} else if (of_machine_is_compatible("PowerBook5,1") ||
			   of_machine_is_compatible("PowerBook5,2") ||
			   of_machine_is_compatible("PowerBook5,3") ||
			   of_machine_is_compatible("PowerBook5,4") ||
			   of_machine_is_compatible("PowerBook5,5")) {
			/* powerbook with external single link tmds (sil164) */
			rdev->mode_info.connector_table = CT_POWERBOOK_EXTERNAL;
		} else if (of_machine_is_compatible("PowerBook5,6")) {
			/* powerbook with external dual or single link tmds */
			rdev->mode_info.connector_table = CT_POWERBOOK_EXTERNAL;
		} else if (of_machine_is_compatible("PowerBook5,7") ||
			   of_machine_is_compatible("PowerBook5,8") ||
			   of_machine_is_compatible("PowerBook5,9")) {
			/* PowerBook6,2 ? */
			/* powerbook with external dual link tmds (sil1178?) */
			rdev->mode_info.connector_table = CT_POWERBOOK_EXTERNAL;
		} else if (of_machine_is_compatible("PowerBook4,1") ||
			   of_machine_is_compatible("PowerBook4,2") ||
			   of_machine_is_compatible("PowerBook4,3") ||
			   of_machine_is_compatible("PowerBook6,3") ||
			   of_machine_is_compatible("PowerBook6,5") ||
			   of_machine_is_compatible("PowerBook6,7")) {
			/* ibook */
			rdev->mode_info.connector_table = CT_IBOOK;
		} else if (of_machine_is_compatible("PowerMac4,4")) {
			/* emac */
			rdev->mode_info.connector_table = CT_EMAC;
		} else if (of_machine_is_compatible("PowerMac10,1")) {
			/* mini with internal tmds */
			rdev->mode_info.connector_table = CT_MINI_INTERNAL;
		} else if (of_machine_is_compatible("PowerMac10,2")) {
			/* mini with external tmds */
			rdev->mode_info.connector_table = CT_MINI_EXTERNAL;
		} else if (of_machine_is_compatible("PowerMac12,1")) {
			/* PowerMac8,1 ? */
			/* imac g5 isight */
			rdev->mode_info.connector_table = CT_IMAC_G5_ISIGHT;
		} else if ((rdev->pdev->device == 0x4a48) &&
			   (rdev->pdev->subsystem_vendor == 0x1002) &&
			   (rdev->pdev->subsystem_device == 0x4a48)) {
			/* Mac X800 */
			rdev->mode_info.connector_table = CT_MAC_X800;
		} else if ((of_machine_is_compatible("PowerMac7,2") ||
			    of_machine_is_compatible("PowerMac7,3")) &&
			   (rdev->pdev->device == 0x4150) &&
			   (rdev->pdev->subsystem_vendor == 0x1002) &&
			   (rdev->pdev->subsystem_device == 0x4150)) {
			/* Mac G5 tower 9600 */
			rdev->mode_info.connector_table = CT_MAC_G5_9600;
		} else
#endif /* CONFIG_PPC_PMAC */
#ifdef CONFIG_PPC64
		if (ASIC_IS_RN50(rdev))
			rdev->mode_info.connector_table = CT_RN50_POWER;
		else
#endif
			rdev->mode_info.connector_table = CT_GENERIC;
	}

	switch (rdev->mode_info.connector_table) {
	case CT_GENERIC:
		DRM_INFO("Connector Table: %d (generic)\n",
			 rdev->mode_info.connector_table);
		/* these are the most common settings */
		if (rdev->flags & RADEON_SINGLE_CRTC) {
			/* VGA - primary dac */
			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_CRT1_SUPPORT,
									1),
						  ATOM_DEVICE_CRT1_SUPPORT);
			radeon_add_legacy_connector(dev, 0,
						    ATOM_DEVICE_CRT1_SUPPORT,
						    DRM_MODE_CONNECTOR_VGA,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_VGA,
						    &hpd);
		} else if (rdev->flags & RADEON_IS_MOBILITY) {
			/* LVDS */
			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_NONE_DETECTED, 0, 0);
			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_LCD1_SUPPORT,
									0),
						  ATOM_DEVICE_LCD1_SUPPORT);
			radeon_add_legacy_connector(dev, 0,
						    ATOM_DEVICE_LCD1_SUPPORT,
						    DRM_MODE_CONNECTOR_LVDS,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_LVDS,
						    &hpd);

			/* VGA - primary dac */
			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_CRT1_SUPPORT,
									1),
						  ATOM_DEVICE_CRT1_SUPPORT);
			radeon_add_legacy_connector(dev, 1,
						    ATOM_DEVICE_CRT1_SUPPORT,
						    DRM_MODE_CONNECTOR_VGA,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_VGA,
						    &hpd);
		} else {
			/* DVI-I - tv dac, int tmds */
			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
			hpd.hpd = RADEON_HPD_1;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_DFP1_SUPPORT,
									0),
						  ATOM_DEVICE_DFP1_SUPPORT);
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_CRT2_SUPPORT,
									2),
						  ATOM_DEVICE_CRT2_SUPPORT);
			radeon_add_legacy_connector(dev, 0,
						    ATOM_DEVICE_DFP1_SUPPORT |
						    ATOM_DEVICE_CRT2_SUPPORT,
						    DRM_MODE_CONNECTOR_DVII,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
						    &hpd);

			/* VGA - primary dac */
			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_CRT1_SUPPORT,
									1),
						  ATOM_DEVICE_CRT1_SUPPORT);
			radeon_add_legacy_connector(dev, 1,
						    ATOM_DEVICE_CRT1_SUPPORT,
						    DRM_MODE_CONNECTOR_VGA,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_VGA,
						    &hpd);
		}

		if (rdev->family != CHIP_R100 && rdev->family != CHIP_R200) {
			/* TV - tv dac */
			ddc_i2c.valid = false;
			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_TV1_SUPPORT,
									2),
						  ATOM_DEVICE_TV1_SUPPORT);
			radeon_add_legacy_connector(dev, 2,
						    ATOM_DEVICE_TV1_SUPPORT,
						    DRM_MODE_CONNECTOR_SVIDEO,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_SVIDEO,
						    &hpd);
		}
		break;
	case CT_IBOOK:
		DRM_INFO("Connector Table: %d (ibook)\n",
			 rdev->mode_info.connector_table);
		/* LVDS */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_LCD1_SUPPORT,
								0),
					  ATOM_DEVICE_LCD1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_LCD1_SUPPORT,
					    DRM_MODE_CONNECTOR_LVDS, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_LVDS,
					    &hpd);
		/* VGA - TV DAC */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_POWERBOOK_EXTERNAL:
		DRM_INFO("Connector Table: %d (powerbook external tmds)\n",
			 rdev->mode_info.connector_table);
		/* LVDS */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_LCD1_SUPPORT,
								0),
					  ATOM_DEVICE_LCD1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_LCD1_SUPPORT,
					    DRM_MODE_CONNECTOR_LVDS, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_LVDS,
					    &hpd);
		/* DVI-I - primary dac, ext tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_2; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_DFP2_SUPPORT,
								0),
					  ATOM_DEVICE_DFP2_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT1_SUPPORT,
								1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		/* XXX some are SL */
		radeon_add_legacy_connector(dev, 1,
					    ATOM_DEVICE_DFP2_SUPPORT |
					    ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_POWERBOOK_INTERNAL:
		DRM_INFO("Connector Table: %d (powerbook internal tmds)\n",
			 rdev->mode_info.connector_table);
		/* LVDS */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_LCD1_SUPPORT,
								0),
					  ATOM_DEVICE_LCD1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_LCD1_SUPPORT,
					    DRM_MODE_CONNECTOR_LVDS, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_LVDS,
					    &hpd);
		/* DVI-I - primary dac, int tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_1; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_DFP1_SUPPORT,
								0),
					  ATOM_DEVICE_DFP1_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT1_SUPPORT,
								1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 1,
					    ATOM_DEVICE_DFP1_SUPPORT |
					    ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_POWERBOOK_VGA:
		DRM_INFO("Connector Table: %d (powerbook vga)\n",
			 rdev->mode_info.connector_table);
		/* LVDS */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_LCD1_SUPPORT,
								0),
					  ATOM_DEVICE_LCD1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_LCD1_SUPPORT,
					    DRM_MODE_CONNECTOR_LVDS, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_LVDS,
					    &hpd);
		/* VGA - primary dac */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT1_SUPPORT,
								1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_MINI_EXTERNAL:
		DRM_INFO("Connector Table: %d (mini external tmds)\n",
			 rdev->mode_info.connector_table);
		/* DVI-I - tv dac, ext tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		hpd.hpd = RADEON_HPD_2; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_DFP2_SUPPORT,
								0),
					  ATOM_DEVICE_DFP2_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		/* XXX are any DL? */
		radeon_add_legacy_connector(dev, 0,
					    ATOM_DEVICE_DFP2_SUPPORT |
					    ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_MINI_INTERNAL:
		DRM_INFO("Connector Table: %d (mini internal tmds)\n",
			 rdev->mode_info.connector_table);
		/* DVI-I - tv dac, int tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		hpd.hpd = RADEON_HPD_1; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_DFP1_SUPPORT,
								0),
					  ATOM_DEVICE_DFP1_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 0,
					    ATOM_DEVICE_DFP1_SUPPORT |
					    ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_IMAC_G5_ISIGHT:
		DRM_INFO("Connector Table: %d (imac g5 isight)\n",
			 rdev->mode_info.connector_table);
		/* DVI-D - int tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
		hpd.hpd = RADEON_HPD_1; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_DFP1_SUPPORT,
								0),
					  ATOM_DEVICE_DFP1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_DFP1_SUPPORT,
					    DRM_MODE_CONNECTOR_DVID, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D,
					    &hpd);
		/* VGA - tv dac */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_EMAC:
		DRM_INFO("Connector Table: %d (emac)\n",
			 rdev->mode_info.connector_table);
		/* VGA - primary dac */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT1_SUPPORT,
								1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		/* VGA - tv dac */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	case CT_RN50_POWER:
		DRM_INFO("Connector Table: %d (rn50-power)\n",
			 rdev->mode_info.connector_table);
		/* VGA - primary dac */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT1_SUPPORT,
								1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 0, ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_CRT2, 0, 0);
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_CRT2_SUPPORT,
								2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 1, ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_VGA, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_VGA,
					    &hpd);
		break;
	case CT_MAC_X800:
		DRM_INFO("Connector Table: %d (mac x800)\n",
			 rdev->mode_info.connector_table);
		/* DVI - primary dac, internal tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_1; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_DFP1_SUPPORT,
								  0),
					  ATOM_DEVICE_DFP1_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_CRT1_SUPPORT,
								  1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 0,
					    ATOM_DEVICE_DFP1_SUPPORT |
					    ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* DVI - tv dac, dvo */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
		hpd.hpd = RADEON_HPD_2; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_DFP2_SUPPORT,
								  0),
					  ATOM_DEVICE_DFP2_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_CRT2_SUPPORT,
								  2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 1,
					    ATOM_DEVICE_DFP2_SUPPORT |
					    ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I,
					    &hpd);
		break;
	case CT_MAC_G5_9600:
		DRM_INFO("Connector Table: %d (mac g5 9600)\n",
			 rdev->mode_info.connector_table);
		/* DVI - tv dac, dvo */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
		hpd.hpd = RADEON_HPD_1; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_DFP2_SUPPORT,
								  0),
					  ATOM_DEVICE_DFP2_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_CRT2_SUPPORT,
								  2),
					  ATOM_DEVICE_CRT2_SUPPORT);
		radeon_add_legacy_connector(dev, 0,
					    ATOM_DEVICE_DFP2_SUPPORT |
					    ATOM_DEVICE_CRT2_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* ADC - primary dac, internal tmds */
		ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
		hpd.hpd = RADEON_HPD_2; /* ??? */
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_DFP1_SUPPORT,
								  0),
					  ATOM_DEVICE_DFP1_SUPPORT);
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								  ATOM_DEVICE_CRT1_SUPPORT,
								  1),
					  ATOM_DEVICE_CRT1_SUPPORT);
		radeon_add_legacy_connector(dev, 1,
					    ATOM_DEVICE_DFP1_SUPPORT |
					    ATOM_DEVICE_CRT1_SUPPORT,
					    DRM_MODE_CONNECTOR_DVII, &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
					    &hpd);
		/* TV - TV DAC */
		ddc_i2c.valid = false;
		hpd.hpd = RADEON_HPD_NONE;
		radeon_add_legacy_encoder(dev,
					  radeon_get_encoder_enum(dev,
								ATOM_DEVICE_TV1_SUPPORT,
								2),
					  ATOM_DEVICE_TV1_SUPPORT);
		radeon_add_legacy_connector(dev, 2, ATOM_DEVICE_TV1_SUPPORT,
					    DRM_MODE_CONNECTOR_SVIDEO,
					    &ddc_i2c,
					    CONNECTOR_OBJECT_ID_SVIDEO,
					    &hpd);
		break;
	default:
		DRM_INFO("Connector table: %d (invalid)\n",
			 rdev->mode_info.connector_table);
		return false;
	}

	radeon_link_encoder_connector(dev);

	return true;
}

static bool radeon_apply_legacy_quirks(struct drm_device *dev,
				       int bios_index,
				       enum radeon_combios_connector
				       *legacy_connector,
				       struct radeon_i2c_bus_rec *ddc_i2c,
				       struct radeon_hpd *hpd)
{

	/* Certain IBM chipset RN50s have a BIOS reporting two VGAs,
	   one with VGA DDC and one with CRT2 DDC. - kill the CRT2 DDC one */
	if (dev->pdev->device == 0x515e &&
	    dev->pdev->subsystem_vendor == 0x1014) {
		if (*legacy_connector == CONNECTOR_CRT_LEGACY &&
		    ddc_i2c->mask_clk_reg == RADEON_GPIO_CRT2_DDC)
			return false;
	}

	/* X300 card with extra non-existent DVI port */
	if (dev->pdev->device == 0x5B60 &&
	    dev->pdev->subsystem_vendor == 0x17af &&
	    dev->pdev->subsystem_device == 0x201e && bios_index == 2) {
		if (*legacy_connector == CONNECTOR_DVI_I_LEGACY)
			return false;
	}

	return true;
}

static bool radeon_apply_legacy_tv_quirks(struct drm_device *dev)
{
	/* Acer 5102 has non-existent TV port */
	if (dev->pdev->device == 0x5975 &&
	    dev->pdev->subsystem_vendor == 0x1025 &&
	    dev->pdev->subsystem_device == 0x009f)
		return false;

	/* HP dc5750 has non-existent TV port */
	if (dev->pdev->device == 0x5974 &&
	    dev->pdev->subsystem_vendor == 0x103c &&
	    dev->pdev->subsystem_device == 0x280a)
		return false;

	/* MSI S270 has non-existent TV port */
	if (dev->pdev->device == 0x5955 &&
	    dev->pdev->subsystem_vendor == 0x1462 &&
	    dev->pdev->subsystem_device == 0x0131)
		return false;

	return true;
}

static uint16_t combios_check_dl_dvi(struct drm_device *dev, int is_dvi_d)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t ext_tmds_info;

	if (rdev->flags & RADEON_IS_IGP) {
		if (is_dvi_d)
			return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D;
		else
			return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I;
	}
	ext_tmds_info = combios_get_table_offset(dev, COMBIOS_EXT_TMDS_INFO_TABLE);
	if (ext_tmds_info) {
		uint8_t rev = RBIOS8(ext_tmds_info);
		uint8_t flags = RBIOS8(ext_tmds_info + 4 + 5);
		if (rev >= 3) {
			if (is_dvi_d)
				return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D;
			else
				return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I;
		} else {
			if (flags & 1) {
				if (is_dvi_d)
					return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D;
				else
					return CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I;
			}
		}
	}
	if (is_dvi_d)
		return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D;
	else
		return CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I;
}

bool radeon_get_legacy_connector_info_from_bios(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t conn_info, entry, devices;
	uint16_t tmp, connector_object_id;
	enum radeon_combios_ddc ddc_type;
	enum radeon_combios_connector connector;
	int i = 0;
	struct radeon_i2c_bus_rec ddc_i2c;
	struct radeon_hpd hpd;

	conn_info = combios_get_table_offset(dev, COMBIOS_CONNECTOR_INFO_TABLE);
	if (conn_info) {
		for (i = 0; i < 4; i++) {
			entry = conn_info + 2 + i * 2;

			if (!RBIOS16(entry))
				break;

			tmp = RBIOS16(entry);

			connector = (tmp >> 12) & 0xf;

			ddc_type = (tmp >> 8) & 0xf;
			ddc_i2c = combios_setup_i2c_bus(rdev, ddc_type, 0, 0);

			switch (connector) {
			case CONNECTOR_PROPRIETARY_LEGACY:
			case CONNECTOR_DVI_I_LEGACY:
			case CONNECTOR_DVI_D_LEGACY:
				if ((tmp >> 4) & 0x1)
					hpd.hpd = RADEON_HPD_2;
				else
					hpd.hpd = RADEON_HPD_1;
				break;
			default:
				hpd.hpd = RADEON_HPD_NONE;
				break;
			}

			if (!radeon_apply_legacy_quirks(dev, i, &connector,
							&ddc_i2c, &hpd))
				continue;

			switch (connector) {
			case CONNECTOR_PROPRIETARY_LEGACY:
				if ((tmp >> 4) & 0x1)
					devices = ATOM_DEVICE_DFP2_SUPPORT;
				else
					devices = ATOM_DEVICE_DFP1_SUPPORT;
				radeon_add_legacy_encoder(dev,
							  radeon_get_encoder_enum
							  (dev, devices, 0),
							  devices);
				radeon_add_legacy_connector(dev, i, devices,
							    legacy_connector_convert
							    [connector],
							    &ddc_i2c,
							    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D,
							    &hpd);
				break;
			case CONNECTOR_CRT_LEGACY:
				if (tmp & 0x1) {
					devices = ATOM_DEVICE_CRT2_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_CRT2_SUPPORT,
								   2),
								  ATOM_DEVICE_CRT2_SUPPORT);
				} else {
					devices = ATOM_DEVICE_CRT1_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_CRT1_SUPPORT,
								   1),
								  ATOM_DEVICE_CRT1_SUPPORT);
				}
				radeon_add_legacy_connector(dev,
							    i,
							    devices,
							    legacy_connector_convert
							    [connector],
							    &ddc_i2c,
							    CONNECTOR_OBJECT_ID_VGA,
							    &hpd);
				break;
			case CONNECTOR_DVI_I_LEGACY:
				devices = 0;
				if (tmp & 0x1) {
					devices |= ATOM_DEVICE_CRT2_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_CRT2_SUPPORT,
								   2),
								  ATOM_DEVICE_CRT2_SUPPORT);
				} else {
					devices |= ATOM_DEVICE_CRT1_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_CRT1_SUPPORT,
								   1),
								  ATOM_DEVICE_CRT1_SUPPORT);
				}
				if ((tmp >> 4) & 0x1) {
					devices |= ATOM_DEVICE_DFP2_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_DFP2_SUPPORT,
								   0),
								  ATOM_DEVICE_DFP2_SUPPORT);
					connector_object_id = combios_check_dl_dvi(dev, 0);
				} else {
					devices |= ATOM_DEVICE_DFP1_SUPPORT;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_DFP1_SUPPORT,
								   0),
								  ATOM_DEVICE_DFP1_SUPPORT);
					connector_object_id = CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I;
				}
				radeon_add_legacy_connector(dev,
							    i,
							    devices,
							    legacy_connector_convert
							    [connector],
							    &ddc_i2c,
							    connector_object_id,
							    &hpd);
				break;
			case CONNECTOR_DVI_D_LEGACY:
				if ((tmp >> 4) & 0x1) {
					devices = ATOM_DEVICE_DFP2_SUPPORT;
					connector_object_id = combios_check_dl_dvi(dev, 1);
				} else {
					devices = ATOM_DEVICE_DFP1_SUPPORT;
					connector_object_id = CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I;
				}
				radeon_add_legacy_encoder(dev,
							  radeon_get_encoder_enum
							  (dev, devices, 0),
							  devices);
				radeon_add_legacy_connector(dev, i, devices,
							    legacy_connector_convert
							    [connector],
							    &ddc_i2c,
							    connector_object_id,
							    &hpd);
				break;
			case CONNECTOR_CTV_LEGACY:
			case CONNECTOR_STV_LEGACY:
				radeon_add_legacy_encoder(dev,
							  radeon_get_encoder_enum
							  (dev,
							   ATOM_DEVICE_TV1_SUPPORT,
							   2),
							  ATOM_DEVICE_TV1_SUPPORT);
				radeon_add_legacy_connector(dev, i,
							    ATOM_DEVICE_TV1_SUPPORT,
							    legacy_connector_convert
							    [connector],
							    &ddc_i2c,
							    CONNECTOR_OBJECT_ID_SVIDEO,
							    &hpd);
				break;
			default:
				DRM_ERROR("Unknown connector type: %d\n",
					  connector);
				continue;
			}

		}
	} else {
		uint16_t tmds_info =
		    combios_get_table_offset(dev, COMBIOS_DFP_INFO_TABLE);
		if (tmds_info) {
			DRM_DEBUG_KMS("Found DFP table, assuming DVI connector\n");

			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_CRT1_SUPPORT,
									1),
						  ATOM_DEVICE_CRT1_SUPPORT);
			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_DFP1_SUPPORT,
									0),
						  ATOM_DEVICE_DFP1_SUPPORT);

			ddc_i2c = combios_setup_i2c_bus(rdev, DDC_DVI, 0, 0);
			hpd.hpd = RADEON_HPD_1;
			radeon_add_legacy_connector(dev,
						    0,
						    ATOM_DEVICE_CRT1_SUPPORT |
						    ATOM_DEVICE_DFP1_SUPPORT,
						    DRM_MODE_CONNECTOR_DVII,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I,
						    &hpd);
		} else {
			uint16_t crt_info =
				combios_get_table_offset(dev, COMBIOS_CRT_INFO_TABLE);
			DRM_DEBUG_KMS("Found CRT table, assuming VGA connector\n");
			if (crt_info) {
				radeon_add_legacy_encoder(dev,
							  radeon_get_encoder_enum(dev,
										ATOM_DEVICE_CRT1_SUPPORT,
										1),
							  ATOM_DEVICE_CRT1_SUPPORT);
				ddc_i2c = combios_setup_i2c_bus(rdev, DDC_VGA, 0, 0);
				hpd.hpd = RADEON_HPD_NONE;
				radeon_add_legacy_connector(dev,
							    0,
							    ATOM_DEVICE_CRT1_SUPPORT,
							    DRM_MODE_CONNECTOR_VGA,
							    &ddc_i2c,
							    CONNECTOR_OBJECT_ID_VGA,
							    &hpd);
			} else {
				DRM_DEBUG_KMS("No connector info found\n");
				return false;
			}
		}
	}

	if (rdev->flags & RADEON_IS_MOBILITY || rdev->flags & RADEON_IS_IGP) {
		uint16_t lcd_info =
		    combios_get_table_offset(dev, COMBIOS_LCD_INFO_TABLE);
		if (lcd_info) {
			uint16_t lcd_ddc_info =
			    combios_get_table_offset(dev,
						     COMBIOS_LCD_DDC_INFO_TABLE);

			radeon_add_legacy_encoder(dev,
						  radeon_get_encoder_enum(dev,
									ATOM_DEVICE_LCD1_SUPPORT,
									0),
						  ATOM_DEVICE_LCD1_SUPPORT);

			if (lcd_ddc_info) {
				ddc_type = RBIOS8(lcd_ddc_info + 2);
				switch (ddc_type) {
				case DDC_LCD:
					ddc_i2c =
						combios_setup_i2c_bus(rdev,
								      DDC_LCD,
								      RBIOS32(lcd_ddc_info + 3),
								      RBIOS32(lcd_ddc_info + 7));
					radeon_i2c_add(rdev, &ddc_i2c, "LCD");
					break;
				case DDC_GPIO:
					ddc_i2c =
						combios_setup_i2c_bus(rdev,
								      DDC_GPIO,
								      RBIOS32(lcd_ddc_info + 3),
								      RBIOS32(lcd_ddc_info + 7));
					radeon_i2c_add(rdev, &ddc_i2c, "LCD");
					break;
				default:
					ddc_i2c =
						combios_setup_i2c_bus(rdev, ddc_type, 0, 0);
					break;
				}
				DRM_DEBUG_KMS("LCD DDC Info Table found!\n");
			} else
				ddc_i2c.valid = false;

			hpd.hpd = RADEON_HPD_NONE;
			radeon_add_legacy_connector(dev,
						    5,
						    ATOM_DEVICE_LCD1_SUPPORT,
						    DRM_MODE_CONNECTOR_LVDS,
						    &ddc_i2c,
						    CONNECTOR_OBJECT_ID_LVDS,
						    &hpd);
		}
	}

	/* check TV table */
	if (rdev->family != CHIP_R100 && rdev->family != CHIP_R200) {
		uint32_t tv_info =
		    combios_get_table_offset(dev, COMBIOS_TV_INFO_TABLE);
		if (tv_info) {
			if (RBIOS8(tv_info + 6) == 'T') {
				if (radeon_apply_legacy_tv_quirks(dev)) {
					hpd.hpd = RADEON_HPD_NONE;
					ddc_i2c.valid = false;
					radeon_add_legacy_encoder(dev,
								  radeon_get_encoder_enum
								  (dev,
								   ATOM_DEVICE_TV1_SUPPORT,
								   2),
								  ATOM_DEVICE_TV1_SUPPORT);
					radeon_add_legacy_connector(dev, 6,
								    ATOM_DEVICE_TV1_SUPPORT,
								    DRM_MODE_CONNECTOR_SVIDEO,
								    &ddc_i2c,
								    CONNECTOR_OBJECT_ID_SVIDEO,
								    &hpd);
				}
			}
		}
	}

	radeon_link_encoder_connector(dev);

	return true;
}

static const char *thermal_controller_names[] = {
	"NONE",
	"lm63",
	"adm1032",
};

void radeon_combios_get_power_modes(struct radeon_device *rdev)
{
	struct drm_device *dev = rdev->ddev;
	u16 offset, misc, misc2 = 0;
	u8 rev, blocks, tmp;
	int state_index = 0;
	struct radeon_i2c_bus_rec i2c_bus;

	rdev->pm.default_power_state_index = -1;

	/* allocate 2 power states */
	rdev->pm.power_state = kzalloc(sizeof(struct radeon_power_state) * 2, GFP_KERNEL);
	if (!rdev->pm.power_state) {
		rdev->pm.default_power_state_index = state_index;
		rdev->pm.num_power_states = 0;

		rdev->pm.current_power_state_index = rdev->pm.default_power_state_index;
		rdev->pm.current_clock_mode_index = 0;
		return;
	}

	/* check for a thermal chip */
	offset = combios_get_table_offset(dev, COMBIOS_OVERDRIVE_INFO_TABLE);
	if (offset) {
		u8 thermal_controller = 0, gpio = 0, i2c_addr = 0, clk_bit = 0, data_bit = 0;

		rev = RBIOS8(offset);

		if (rev == 0) {
			thermal_controller = RBIOS8(offset + 3);
			gpio = RBIOS8(offset + 4) & 0x3f;
			i2c_addr = RBIOS8(offset + 5);
		} else if (rev == 1) {
			thermal_controller = RBIOS8(offset + 4);
			gpio = RBIOS8(offset + 5) & 0x3f;
			i2c_addr = RBIOS8(offset + 6);
		} else if (rev == 2) {
			thermal_controller = RBIOS8(offset + 4);
			gpio = RBIOS8(offset + 5) & 0x3f;
			i2c_addr = RBIOS8(offset + 6);
			clk_bit = RBIOS8(offset + 0xa);
			data_bit = RBIOS8(offset + 0xb);
		}
		if ((thermal_controller > 0) && (thermal_controller < 3)) {
			DRM_INFO("Possible %s thermal controller at 0x%02x\n",
				 thermal_controller_names[thermal_controller],
				 i2c_addr >> 1);
			if (gpio == DDC_LCD) {
				/* MM i2c */
				i2c_bus.valid = true;
				i2c_bus.hw_capable = true;
				i2c_bus.mm_i2c = true;
				i2c_bus.i2c_id = 0xa0;
			} else if (gpio == DDC_GPIO)
				i2c_bus = combios_setup_i2c_bus(rdev, gpio, 1 << clk_bit, 1 << data_bit);
			else
				i2c_bus = combios_setup_i2c_bus(rdev, gpio, 0, 0);
			rdev->pm.i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
			if (rdev->pm.i2c_bus) {
				struct i2c_board_info info = { };
				const char *name = thermal_controller_names[thermal_controller];
				info.addr = i2c_addr >> 1;
				strlcpy(info.type, name, sizeof(info.type));
				i2c_new_device(&rdev->pm.i2c_bus->adapter, &info);
			}
		}
	} else {
		/* boards with a thermal chip, but no overdrive table */

		/* Asus 9600xt has an f75375 on the monid bus */
		if ((dev->pdev->device == 0x4152) &&
		    (dev->pdev->subsystem_vendor == 0x1043) &&
		    (dev->pdev->subsystem_device == 0xc002)) {
			i2c_bus = combios_setup_i2c_bus(rdev, DDC_MONID, 0, 0);
			rdev->pm.i2c_bus = radeon_i2c_lookup(rdev, &i2c_bus);
			if (rdev->pm.i2c_bus) {
				struct i2c_board_info info = { };
				const char *name = "f75375";
				info.addr = 0x28;
				strlcpy(info.type, name, sizeof(info.type));
				i2c_new_device(&rdev->pm.i2c_bus->adapter, &info);
				DRM_INFO("Possible %s thermal controller at 0x%02x\n",
					 name, info.addr);
			}
		}
	}

	if (rdev->flags & RADEON_IS_MOBILITY) {
		offset = combios_get_table_offset(dev, COMBIOS_POWERPLAY_INFO_TABLE);
		if (offset) {
			rev = RBIOS8(offset);
			blocks = RBIOS8(offset + 0x2);
			/* power mode 0 tends to be the only valid one */
			rdev->pm.power_state[state_index].num_clock_modes = 1;
			rdev->pm.power_state[state_index].clock_info[0].mclk = RBIOS32(offset + 0x5 + 0x2);
			rdev->pm.power_state[state_index].clock_info[0].sclk = RBIOS32(offset + 0x5 + 0x6);
			if ((rdev->pm.power_state[state_index].clock_info[0].mclk == 0) ||
			    (rdev->pm.power_state[state_index].clock_info[0].sclk == 0))
				goto default_mode;
			rdev->pm.power_state[state_index].type =
				POWER_STATE_TYPE_BATTERY;
			misc = RBIOS16(offset + 0x5 + 0x0);
			if (rev > 4)
				misc2 = RBIOS16(offset + 0x5 + 0xe);
			rdev->pm.power_state[state_index].misc = misc;
			rdev->pm.power_state[state_index].misc2 = misc2;
			if (misc & 0x4) {
				rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_GPIO;
				if (misc & 0x8)
					rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
						true;
				else
					rdev->pm.power_state[state_index].clock_info[0].voltage.active_high =
						false;
				rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.valid = true;
				if (rev < 6) {
					rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.reg =
						RBIOS16(offset + 0x5 + 0xb) * 4;
					tmp = RBIOS8(offset + 0x5 + 0xd);
					rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.mask = (1 << tmp);
				} else {
					u8 entries = RBIOS8(offset + 0x5 + 0xb);
					u16 voltage_table_offset = RBIOS16(offset + 0x5 + 0xc);
					if (entries && voltage_table_offset) {
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.reg =
							RBIOS16(voltage_table_offset) * 4;
						tmp = RBIOS8(voltage_table_offset + 0x2);
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.mask = (1 << tmp);
					} else
						rdev->pm.power_state[state_index].clock_info[0].voltage.gpio.valid = false;
				}
				switch ((misc2 & 0x700) >> 8) {
				case 0:
				default:
					rdev->pm.power_state[state_index].clock_info[0].voltage.delay = 0;
					break;
				case 1:
					rdev->pm.power_state[state_index].clock_info[0].voltage.delay = 33;
					break;
				case 2:
					rdev->pm.power_state[state_index].clock_info[0].voltage.delay = 66;
					break;
				case 3:
					rdev->pm.power_state[state_index].clock_info[0].voltage.delay = 99;
					break;
				case 4:
					rdev->pm.power_state[state_index].clock_info[0].voltage.delay = 132;
					break;
				}
			} else
				rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_NONE;
			if (rev > 6)
				rdev->pm.power_state[state_index].pcie_lanes =
					RBIOS8(offset + 0x5 + 0x10);
			rdev->pm.power_state[state_index].flags = RADEON_PM_STATE_SINGLE_DISPLAY_ONLY;
			state_index++;
		} else {
			/* XXX figure out some good default low power mode for mobility cards w/out power tables */
		}
	} else {
		/* XXX figure out some good default low power mode for desktop cards */
	}

default_mode:
	/* add the default mode */
	rdev->pm.power_state[state_index].type =
		POWER_STATE_TYPE_DEFAULT;
	rdev->pm.power_state[state_index].num_clock_modes = 1;
	rdev->pm.power_state[state_index].clock_info[0].mclk = rdev->clock.default_mclk;
	rdev->pm.power_state[state_index].clock_info[0].sclk = rdev->clock.default_sclk;
	rdev->pm.power_state[state_index].default_clock_mode = &rdev->pm.power_state[state_index].clock_info[0];
	if ((state_index > 0) &&
	    (rdev->pm.power_state[0].clock_info[0].voltage.type == VOLTAGE_GPIO))
		rdev->pm.power_state[state_index].clock_info[0].voltage =
			rdev->pm.power_state[0].clock_info[0].voltage;
	else
		rdev->pm.power_state[state_index].clock_info[0].voltage.type = VOLTAGE_NONE;
	rdev->pm.power_state[state_index].pcie_lanes = 16;
	rdev->pm.power_state[state_index].flags = 0;
	rdev->pm.default_power_state_index = state_index;
	rdev->pm.num_power_states = state_index + 1;

	rdev->pm.current_power_state_index = rdev->pm.default_power_state_index;
	rdev->pm.current_clock_mode_index = 0;
}

void radeon_external_tmds_setup(struct drm_encoder *encoder)
{
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_ext_tmds *tmds = radeon_encoder->enc_priv;

	if (!tmds)
		return;

	switch (tmds->dvo_chip) {
	case DVO_SIL164:
		/* sil 164 */
		radeon_i2c_put_byte(tmds->i2c_bus,
				    tmds->slave_addr,
				    0x08, 0x30);
		radeon_i2c_put_byte(tmds->i2c_bus,
				       tmds->slave_addr,
				       0x09, 0x00);
		radeon_i2c_put_byte(tmds->i2c_bus,
				    tmds->slave_addr,
				    0x0a, 0x90);
		radeon_i2c_put_byte(tmds->i2c_bus,
				    tmds->slave_addr,
				    0x0c, 0x89);
		radeon_i2c_put_byte(tmds->i2c_bus,
				       tmds->slave_addr,
				       0x08, 0x3b);
		break;
	case DVO_SIL1178:
		/* sil 1178 - untested */
		/*
		 * 0x0f, 0x44
		 * 0x0f, 0x4c
		 * 0x0e, 0x01
		 * 0x0a, 0x80
		 * 0x09, 0x30
		 * 0x0c, 0xc9
		 * 0x0d, 0x70
		 * 0x08, 0x32
		 * 0x08, 0x33
		 */
		break;
	default:
		break;
	}

}

bool radeon_combios_external_tmds_setup(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint16_t offset;
	uint8_t blocks, slave_addr, rev;
	uint32_t index, id;
	uint32_t reg, val, and_mask, or_mask;
	struct radeon_encoder_ext_tmds *tmds = radeon_encoder->enc_priv;

	if (!tmds)
		return false;

	if (rdev->flags & RADEON_IS_IGP) {
		offset = combios_get_table_offset(dev, COMBIOS_TMDS_POWER_ON_TABLE);
		rev = RBIOS8(offset);
		if (offset) {
			rev = RBIOS8(offset);
			if (rev > 1) {
				blocks = RBIOS8(offset + 3);
				index = offset + 4;
				while (blocks > 0) {
					id = RBIOS16(index);
					index += 2;
					switch (id >> 13) {
					case 0:
						reg = (id & 0x1fff) * 4;
						val = RBIOS32(index);
						index += 4;
						WREG32(reg, val);
						break;
					case 2:
						reg = (id & 0x1fff) * 4;
						and_mask = RBIOS32(index);
						index += 4;
						or_mask = RBIOS32(index);
						index += 4;
						val = RREG32(reg);
						val = (val & and_mask) | or_mask;
						WREG32(reg, val);
						break;
					case 3:
						val = RBIOS16(index);
						index += 2;
						udelay(val);
						break;
					case 4:
						val = RBIOS16(index);
						index += 2;
						udelay(val * 1000);
						break;
					case 6:
						slave_addr = id & 0xff;
						slave_addr >>= 1; /* 7 bit addressing */
						index++;
						reg = RBIOS8(index);
						index++;
						val = RBIOS8(index);
						index++;
						radeon_i2c_put_byte(tmds->i2c_bus,
								    slave_addr,
								    reg, val);
						break;
					default:
						DRM_ERROR("Unknown id %d\n", id >> 13);
						break;
					}
					blocks--;
				}
				return true;
			}
		}
	} else {
		offset = combios_get_table_offset(dev, COMBIOS_EXT_TMDS_INFO_TABLE);
		if (offset) {
			index = offset + 10;
			id = RBIOS16(index);
			while (id != 0xffff) {
				index += 2;
				switch (id >> 13) {
				case 0:
					reg = (id & 0x1fff) * 4;
					val = RBIOS32(index);
					WREG32(reg, val);
					break;
				case 2:
					reg = (id & 0x1fff) * 4;
					and_mask = RBIOS32(index);
					index += 4;
					or_mask = RBIOS32(index);
					index += 4;
					val = RREG32(reg);
					val = (val & and_mask) | or_mask;
					WREG32(reg, val);
					break;
				case 4:
					val = RBIOS16(index);
					index += 2;
					udelay(val);
					break;
				case 5:
					reg = id & 0x1fff;
					and_mask = RBIOS32(index);
					index += 4;
					or_mask = RBIOS32(index);
					index += 4;
					val = RREG32_PLL(reg);
					val = (val & and_mask) | or_mask;
					WREG32_PLL(reg, val);
					break;
				case 6:
					reg = id & 0x1fff;
					val = RBIOS8(index);
					index += 1;
					radeon_i2c_put_byte(tmds->i2c_bus,
							    tmds->slave_addr,
							    reg, val);
					break;
				default:
					DRM_ERROR("Unknown id %d\n", id >> 13);
					break;
				}
				id = RBIOS16(index);
			}
			return true;
		}
	}
	return false;
}

static void combios_parse_mmio_table(struct drm_device *dev, uint16_t offset)
{
	struct radeon_device *rdev = dev->dev_private;

	if (offset) {
		while (RBIOS16(offset)) {
			uint16_t cmd = ((RBIOS16(offset) & 0xe000) >> 13);
			uint32_t addr = (RBIOS16(offset) & 0x1fff);
			uint32_t val, and_mask, or_mask;
			uint32_t tmp;

			offset += 2;
			switch (cmd) {
			case 0:
				val = RBIOS32(offset);
				offset += 4;
				WREG32(addr, val);
				break;
			case 1:
				val = RBIOS32(offset);
				offset += 4;
				WREG32(addr, val);
				break;
			case 2:
				and_mask = RBIOS32(offset);
				offset += 4;
				or_mask = RBIOS32(offset);
				offset += 4;
				tmp = RREG32(addr);
				tmp &= and_mask;
				tmp |= or_mask;
				WREG32(addr, tmp);
				break;
			case 3:
				and_mask = RBIOS32(offset);
				offset += 4;
				or_mask = RBIOS32(offset);
				offset += 4;
				tmp = RREG32(addr);
				tmp &= and_mask;
				tmp |= or_mask;
				WREG32(addr, tmp);
				break;
			case 4:
				val = RBIOS16(offset);
				offset += 2;
				udelay(val);
				break;
			case 5:
				val = RBIOS16(offset);
				offset += 2;
				switch (addr) {
				case 8:
					while (val--) {
						if (!
						    (RREG32_PLL
						     (RADEON_CLK_PWRMGT_CNTL) &
						     RADEON_MC_BUSY))
							break;
					}
					break;
				case 9:
					while (val--) {
						if ((RREG32(RADEON_MC_STATUS) &
						     RADEON_MC_IDLE))
							break;
					}
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		}
	}
}

static void combios_parse_pll_table(struct drm_device *dev, uint16_t offset)
{
	struct radeon_device *rdev = dev->dev_private;

	if (offset) {
		while (RBIOS8(offset)) {
			uint8_t cmd = ((RBIOS8(offset) & 0xc0) >> 6);
			uint8_t addr = (RBIOS8(offset) & 0x3f);
			uint32_t val, shift, tmp;
			uint32_t and_mask, or_mask;

			offset++;
			switch (cmd) {
			case 0:
				val = RBIOS32(offset);
				offset += 4;
				WREG32_PLL(addr, val);
				break;
			case 1:
				shift = RBIOS8(offset) * 8;
				offset++;
				and_mask = RBIOS8(offset) << shift;
				and_mask |= ~(0xff << shift);
				offset++;
				or_mask = RBIOS8(offset) << shift;
				offset++;
				tmp = RREG32_PLL(addr);
				tmp &= and_mask;
				tmp |= or_mask;
				WREG32_PLL(addr, tmp);
				break;
			case 2:
			case 3:
				tmp = 1000;
				switch (addr) {
				case 1:
					udelay(150);
					break;
				case 2:
					udelay(1000);
					break;
				case 3:
					while (tmp--) {
						if (!
						    (RREG32_PLL
						     (RADEON_CLK_PWRMGT_CNTL) &
						     RADEON_MC_BUSY))
							break;
					}
					break;
				case 4:
					while (tmp--) {
						if (RREG32_PLL
						    (RADEON_CLK_PWRMGT_CNTL) &
						    RADEON_DLL_READY)
							break;
					}
					break;
				case 5:
					tmp =
					    RREG32_PLL(RADEON_CLK_PWRMGT_CNTL);
					if (tmp & RADEON_CG_NO1_DEBUG_0) {
#if 0
						uint32_t mclk_cntl =
						    RREG32_PLL
						    (RADEON_MCLK_CNTL);
						mclk_cntl &= 0xffff0000;
						/*mclk_cntl |= 0x00001111;*//* ??? */
						WREG32_PLL(RADEON_MCLK_CNTL,
							   mclk_cntl);
						udelay(10000);
#endif
						WREG32_PLL
						    (RADEON_CLK_PWRMGT_CNTL,
						     tmp &
						     ~RADEON_CG_NO1_DEBUG_0);
						udelay(10000);
					}
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		}
	}
}

static void combios_parse_ram_reset_table(struct drm_device *dev,
					  uint16_t offset)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t tmp;

	if (offset) {
		uint8_t val = RBIOS8(offset);
		while (val != 0xff) {
			offset++;

			if (val == 0x0f) {
				uint32_t channel_complete_mask;

				if (ASIC_IS_R300(rdev))
					channel_complete_mask =
					    R300_MEM_PWRUP_COMPLETE;
				else
					channel_complete_mask =
					    RADEON_MEM_PWRUP_COMPLETE;
				tmp = 20000;
				while (tmp--) {
					if ((RREG32(RADEON_MEM_STR_CNTL) &
					     channel_complete_mask) ==
					    channel_complete_mask)
						break;
				}
			} else {
				uint32_t or_mask = RBIOS16(offset);
				offset += 2;

				tmp = RREG32(RADEON_MEM_SDRAM_MODE_REG);
				tmp &= RADEON_SDRAM_MODE_MASK;
				tmp |= or_mask;
				WREG32(RADEON_MEM_SDRAM_MODE_REG, tmp);

				or_mask = val << 24;
				tmp = RREG32(RADEON_MEM_SDRAM_MODE_REG);
				tmp &= RADEON_B3MEM_RESET_MASK;
				tmp |= or_mask;
				WREG32(RADEON_MEM_SDRAM_MODE_REG, tmp);
			}
			val = RBIOS8(offset);
		}
	}
}

static uint32_t combios_detect_ram(struct drm_device *dev, int ram,
				   int mem_addr_mapping)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t mem_cntl;
	uint32_t mem_size;
	uint32_t addr = 0;

	mem_cntl = RREG32(RADEON_MEM_CNTL);
	if (mem_cntl & RV100_HALF_MODE)
		ram /= 2;
	mem_size = ram;
	mem_cntl &= ~(0xff << 8);
	mem_cntl |= (mem_addr_mapping & 0xff) << 8;
	WREG32(RADEON_MEM_CNTL, mem_cntl);
	RREG32(RADEON_MEM_CNTL);

	/* sdram reset ? */

	/* something like this????  */
	while (ram--) {
		addr = ram * 1024 * 1024;
		/* write to each page */
		WREG32(RADEON_MM_INDEX, (addr) | RADEON_MM_APER);
		WREG32(RADEON_MM_DATA, 0xdeadbeef);
		/* read back and verify */
		WREG32(RADEON_MM_INDEX, (addr) | RADEON_MM_APER);
		if (RREG32(RADEON_MM_DATA) != 0xdeadbeef)
			return 0;
	}

	return mem_size;
}

static void combios_write_ram_size(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint8_t rev;
	uint16_t offset;
	uint32_t mem_size = 0;
	uint32_t mem_cntl = 0;

	/* should do something smarter here I guess... */
	if (rdev->flags & RADEON_IS_IGP)
		return;

	/* first check detected mem table */
	offset = combios_get_table_offset(dev, COMBIOS_DETECTED_MEM_TABLE);
	if (offset) {
		rev = RBIOS8(offset);
		if (rev < 3) {
			mem_cntl = RBIOS32(offset + 1);
			mem_size = RBIOS16(offset + 5);
			if ((rdev->family < CHIP_R200) &&
			    !ASIC_IS_RN50(rdev))
				WREG32(RADEON_MEM_CNTL, mem_cntl);
		}
	}

	if (!mem_size) {
		offset =
		    combios_get_table_offset(dev, COMBIOS_MEM_CONFIG_TABLE);
		if (offset) {
			rev = RBIOS8(offset - 1);
			if (rev < 1) {
				if ((rdev->family < CHIP_R200)
				    && !ASIC_IS_RN50(rdev)) {
					int ram = 0;
					int mem_addr_mapping = 0;

					while (RBIOS8(offset)) {
						ram = RBIOS8(offset);
						mem_addr_mapping =
						    RBIOS8(offset + 1);
						if (mem_addr_mapping != 0x25)
							ram *= 2;
						mem_size =
						    combios_detect_ram(dev, ram,
								       mem_addr_mapping);
						if (mem_size)
							break;
						offset += 2;
					}
				} else
					mem_size = RBIOS8(offset);
			} else {
				mem_size = RBIOS8(offset);
				mem_size *= 2;	/* convert to MB */
			}
		}
	}

	mem_size *= (1024 * 1024);	/* convert to bytes */
	WREG32(RADEON_CONFIG_MEMSIZE, mem_size);
}

void radeon_combios_dyn_clk_setup(struct drm_device *dev, int enable)
{
	uint16_t dyn_clk_info =
	    combios_get_table_offset(dev, COMBIOS_DYN_CLK_1_TABLE);

	if (dyn_clk_info)
		combios_parse_pll_table(dev, dyn_clk_info);
}

void radeon_combios_asic_init(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint16_t table;

	/* port hardcoded mac stuff from radeonfb */
	if (rdev->bios == NULL)
		return;

	/* ASIC INIT 1 */
	table = combios_get_table_offset(dev, COMBIOS_ASIC_INIT_1_TABLE);
	if (table)
		combios_parse_mmio_table(dev, table);

	/* PLL INIT */
	table = combios_get_table_offset(dev, COMBIOS_PLL_INIT_TABLE);
	if (table)
		combios_parse_pll_table(dev, table);

	/* ASIC INIT 2 */
	table = combios_get_table_offset(dev, COMBIOS_ASIC_INIT_2_TABLE);
	if (table)
		combios_parse_mmio_table(dev, table);

	if (!(rdev->flags & RADEON_IS_IGP)) {
		/* ASIC INIT 4 */
		table =
		    combios_get_table_offset(dev, COMBIOS_ASIC_INIT_4_TABLE);
		if (table)
			combios_parse_mmio_table(dev, table);

		/* RAM RESET */
		table = combios_get_table_offset(dev, COMBIOS_RAM_RESET_TABLE);
		if (table)
			combios_parse_ram_reset_table(dev, table);

		/* ASIC INIT 3 */
		table =
		    combios_get_table_offset(dev, COMBIOS_ASIC_INIT_3_TABLE);
		if (table)
			combios_parse_mmio_table(dev, table);

		/* write CONFIG_MEMSIZE */
		combios_write_ram_size(dev);
	}

	/* quirk for rs4xx HP nx6125 laptop to make it resume
	 * - it hangs on resume inside the dynclk 1 table.
	 */
	if (rdev->family == CHIP_RS480 &&
	    rdev->pdev->subsystem_vendor == 0x103c &&
	    rdev->pdev->subsystem_device == 0x308b)
		return;

	/* quirk for rs4xx HP dv5000 laptop to make it resume
	 * - it hangs on resume inside the dynclk 1 table.
	 */
	if (rdev->family == CHIP_RS480 &&
	    rdev->pdev->subsystem_vendor == 0x103c &&
	    rdev->pdev->subsystem_device == 0x30a4)
		return;

	/* DYN CLK 1 */
	table = combios_get_table_offset(dev, COMBIOS_DYN_CLK_1_TABLE);
	if (table)
		combios_parse_pll_table(dev, table);

}

void radeon_combios_initialize_bios_scratch_regs(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	uint32_t bios_0_scratch, bios_6_scratch, bios_7_scratch;

	bios_0_scratch = RREG32(RADEON_BIOS_0_SCRATCH);
	bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);
	bios_7_scratch = RREG32(RADEON_BIOS_7_SCRATCH);

	/* let the bios control the backlight */
	bios_0_scratch &= ~RADEON_DRIVER_BRIGHTNESS_EN;

	/* tell the bios not to handle mode switching */
	bios_6_scratch |= (RADEON_DISPLAY_SWITCHING_DIS |
			   RADEON_ACC_MODE_CHANGE);

	/* tell the bios a driver is loaded */
	bios_7_scratch |= RADEON_DRV_LOADED;

	WREG32(RADEON_BIOS_0_SCRATCH, bios_0_scratch);
	WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
	WREG32(RADEON_BIOS_7_SCRATCH, bios_7_scratch);
}

void radeon_combios_output_lock(struct drm_encoder *encoder, bool lock)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t bios_6_scratch;

	bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);

	if (lock)
		bios_6_scratch |= RADEON_DRIVER_CRITICAL;
	else
		bios_6_scratch &= ~RADEON_DRIVER_CRITICAL;

	WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
}

void
radeon_combios_connected_scratch_regs(struct drm_connector *connector,
				      struct drm_encoder *encoder,
				      bool connected)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_connector *radeon_connector =
	    to_radeon_connector(connector);
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_4_scratch = RREG32(RADEON_BIOS_4_SCRATCH);
	uint32_t bios_5_scratch = RREG32(RADEON_BIOS_5_SCRATCH);

	if ((radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_TV1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("TV1 connected\n");
			/* fix me */
			bios_4_scratch |= RADEON_TV1_ATTACHED_SVIDEO;
			/*save->bios_4_scratch |= RADEON_TV1_ATTACHED_COMP; */
			bios_5_scratch |= RADEON_TV1_ON;
			bios_5_scratch |= RADEON_ACC_REQ_TV1;
		} else {
			DRM_DEBUG_KMS("TV1 disconnected\n");
			bios_4_scratch &= ~RADEON_TV1_ATTACHED_MASK;
			bios_5_scratch &= ~RADEON_TV1_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_TV1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_LCD1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("LCD1 connected\n");
			bios_4_scratch |= RADEON_LCD1_ATTACHED;
			bios_5_scratch |= RADEON_LCD1_ON;
			bios_5_scratch |= RADEON_ACC_REQ_LCD1;
		} else {
			DRM_DEBUG_KMS("LCD1 disconnected\n");
			bios_4_scratch &= ~RADEON_LCD1_ATTACHED;
			bios_5_scratch &= ~RADEON_LCD1_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_LCD1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT1 connected\n");
			bios_4_scratch |= RADEON_CRT1_ATTACHED_COLOR;
			bios_5_scratch |= RADEON_CRT1_ON;
			bios_5_scratch |= RADEON_ACC_REQ_CRT1;
		} else {
			DRM_DEBUG_KMS("CRT1 disconnected\n");
			bios_4_scratch &= ~RADEON_CRT1_ATTACHED_MASK;
			bios_5_scratch &= ~RADEON_CRT1_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_CRT1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_CRT2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("CRT2 connected\n");
			bios_4_scratch |= RADEON_CRT2_ATTACHED_COLOR;
			bios_5_scratch |= RADEON_CRT2_ON;
			bios_5_scratch |= RADEON_ACC_REQ_CRT2;
		} else {
			DRM_DEBUG_KMS("CRT2 disconnected\n");
			bios_4_scratch &= ~RADEON_CRT2_ATTACHED_MASK;
			bios_5_scratch &= ~RADEON_CRT2_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_CRT2;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP1_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP1 connected\n");
			bios_4_scratch |= RADEON_DFP1_ATTACHED;
			bios_5_scratch |= RADEON_DFP1_ON;
			bios_5_scratch |= RADEON_ACC_REQ_DFP1;
		} else {
			DRM_DEBUG_KMS("DFP1 disconnected\n");
			bios_4_scratch &= ~RADEON_DFP1_ATTACHED;
			bios_5_scratch &= ~RADEON_DFP1_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_DFP1;
		}
	}
	if ((radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) &&
	    (radeon_connector->devices & ATOM_DEVICE_DFP2_SUPPORT)) {
		if (connected) {
			DRM_DEBUG_KMS("DFP2 connected\n");
			bios_4_scratch |= RADEON_DFP2_ATTACHED;
			bios_5_scratch |= RADEON_DFP2_ON;
			bios_5_scratch |= RADEON_ACC_REQ_DFP2;
		} else {
			DRM_DEBUG_KMS("DFP2 disconnected\n");
			bios_4_scratch &= ~RADEON_DFP2_ATTACHED;
			bios_5_scratch &= ~RADEON_DFP2_ON;
			bios_5_scratch &= ~RADEON_ACC_REQ_DFP2;
		}
	}
	WREG32(RADEON_BIOS_4_SCRATCH, bios_4_scratch);
	WREG32(RADEON_BIOS_5_SCRATCH, bios_5_scratch);
}

void
radeon_combios_encoder_crtc_scratch_regs(struct drm_encoder *encoder, int crtc)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_5_scratch = RREG32(RADEON_BIOS_5_SCRATCH);

	if (radeon_encoder->devices & ATOM_DEVICE_TV1_SUPPORT) {
		bios_5_scratch &= ~RADEON_TV1_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_TV1_CRTC_SHIFT);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT1_SUPPORT) {
		bios_5_scratch &= ~RADEON_CRT1_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_CRT1_CRTC_SHIFT);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_CRT2_SUPPORT) {
		bios_5_scratch &= ~RADEON_CRT2_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_CRT2_CRTC_SHIFT);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_LCD1_SUPPORT) {
		bios_5_scratch &= ~RADEON_LCD1_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_LCD1_CRTC_SHIFT);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP1_SUPPORT) {
		bios_5_scratch &= ~RADEON_DFP1_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_DFP1_CRTC_SHIFT);
	}
	if (radeon_encoder->devices & ATOM_DEVICE_DFP2_SUPPORT) {
		bios_5_scratch &= ~RADEON_DFP2_CRTC_MASK;
		bios_5_scratch |= (crtc << RADEON_DFP2_CRTC_SHIFT);
	}
	WREG32(RADEON_BIOS_5_SCRATCH, bios_5_scratch);
}

void
radeon_combios_encoder_dpms_scratch_regs(struct drm_encoder *encoder, bool on)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	uint32_t bios_6_scratch = RREG32(RADEON_BIOS_6_SCRATCH);

	if (radeon_encoder->devices & (ATOM_DEVICE_TV_SUPPORT)) {
		if (on)
			bios_6_scratch |= RADEON_TV_DPMS_ON;
		else
			bios_6_scratch &= ~RADEON_TV_DPMS_ON;
	}
	if (radeon_encoder->devices & (ATOM_DEVICE_CRT_SUPPORT)) {
		if (on)
			bios_6_scratch |= RADEON_CRT_DPMS_ON;
		else
			bios_6_scratch &= ~RADEON_CRT_DPMS_ON;
	}
	if (radeon_encoder->devices & (ATOM_DEVICE_LCD_SUPPORT)) {
		if (on)
			bios_6_scratch |= RADEON_LCD_DPMS_ON;
		else
			bios_6_scratch &= ~RADEON_LCD_DPMS_ON;
	}
	if (radeon_encoder->devices & (ATOM_DEVICE_DFP_SUPPORT)) {
		if (on)
			bios_6_scratch |= RADEON_DFP_DPMS_ON;
		else
			bios_6_scratch &= ~RADEON_DFP_DPMS_ON;
	}
	WREG32(RADEON_BIOS_6_SCRATCH, bios_6_scratch);
}
