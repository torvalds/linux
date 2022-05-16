/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_DDC_REGS_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_DDC_REGS_H_

#include "gpio_regs.h"

/****************************** new register headers */
/*** following in header */

#define DDC_GPIO_REG_LIST_ENTRY(type,cd,id) \
	.type ## _reg =   REG(DC_GPIO_DDC ## id ## _ ## type),\
	.type ## _mask =  DC_GPIO_DDC ## id ## _ ## type ## __DC_GPIO_DDC ## id ## cd ## _ ## type ## _MASK,\
	.type ## _shift = DC_GPIO_DDC ## id ## _ ## type ## __DC_GPIO_DDC ## id ## cd ## _ ## type ## __SHIFT

#define DDC_GPIO_REG_LIST(cd,id) \
	{\
	DDC_GPIO_REG_LIST_ENTRY(MASK,cd,id),\
	DDC_GPIO_REG_LIST_ENTRY(A,cd,id),\
	DDC_GPIO_REG_LIST_ENTRY(EN,cd,id),\
	DDC_GPIO_REG_LIST_ENTRY(Y,cd,id)\
	}

#define DDC_REG_LIST(cd,id) \
	DDC_GPIO_REG_LIST(cd,id),\
	.ddc_setup = REG(DC_I2C_DDC ## id ## _SETUP)

	#define DDC_REG_LIST_DCN2(cd, id) \
	DDC_GPIO_REG_LIST(cd, id),\
	.ddc_setup = REG(DC_I2C_DDC ## id ## _SETUP),\
	.phy_aux_cntl = REG(PHY_AUX_CNTL), \
	.dc_gpio_aux_ctrl_5 = REG(DC_GPIO_AUX_CTRL_5)

#define DDC_GPIO_VGA_REG_LIST_ENTRY(type,cd)\
	.type ## _reg =   REG(DC_GPIO_DDCVGA_ ## type),\
	.type ## _mask =  DC_GPIO_DDCVGA_ ## type ## __DC_GPIO_DDCVGA ## cd ## _ ## type ## _MASK,\
	.type ## _shift = DC_GPIO_DDCVGA_ ## type ## __DC_GPIO_DDCVGA ## cd ## _ ## type ## __SHIFT

#define DDC_GPIO_VGA_REG_LIST(cd) \
	{\
	DDC_GPIO_VGA_REG_LIST_ENTRY(MASK,cd),\
	DDC_GPIO_VGA_REG_LIST_ENTRY(A,cd),\
	DDC_GPIO_VGA_REG_LIST_ENTRY(EN,cd),\
	DDC_GPIO_VGA_REG_LIST_ENTRY(Y,cd)\
	}

#define DDC_VGA_REG_LIST(cd) \
	DDC_GPIO_VGA_REG_LIST(cd),\
	.ddc_setup = mmDC_I2C_DDCVGA_SETUP

#define DDC_GPIO_I2C_REG_LIST_ENTRY(type,cd) \
	.type ## _reg =   REG(DC_GPIO_I2CPAD_ ## type),\
	.type ## _mask =  DC_GPIO_I2CPAD_ ## type ## __DC_GPIO_ ## cd ## _ ## type ## _MASK,\
	.type ## _shift = DC_GPIO_I2CPAD_ ## type ## __DC_GPIO_ ## cd ## _ ## type ## __SHIFT

#define DDC_GPIO_I2C_REG_LIST(cd) \
	{\
	DDC_GPIO_I2C_REG_LIST_ENTRY(MASK,cd),\
	DDC_GPIO_I2C_REG_LIST_ENTRY(A,cd),\
	DDC_GPIO_I2C_REG_LIST_ENTRY(EN,cd),\
	DDC_GPIO_I2C_REG_LIST_ENTRY(Y,cd)\
	}

#define DDC_I2C_REG_LIST(cd) \
	DDC_GPIO_I2C_REG_LIST(cd),\
	.ddc_setup = 0

#define DDC_I2C_REG_LIST_DCN2(cd) \
	DDC_GPIO_I2C_REG_LIST(cd),\
	.ddc_setup = 0,\
	.phy_aux_cntl = REG(PHY_AUX_CNTL), \
	.dc_gpio_aux_ctrl_5 = REG(DC_GPIO_AUX_CTRL_5)
#define DDC_MASK_SH_LIST_COMMON(mask_sh) \
		SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE, mask_sh),\
		SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_EDID_DETECT_ENABLE, mask_sh),\
		SF_DDC(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_EDID_DETECT_MODE, mask_sh),\
		SF_DDC(DC_GPIO_DDC1_MASK, DC_GPIO_DDC1DATA_PD_EN, mask_sh),\
		SF_DDC(DC_GPIO_DDC1_MASK, DC_GPIO_DDC1CLK_PD_EN, mask_sh),\
		SF_DDC(DC_GPIO_DDC1_MASK, AUX_PAD1_MODE, mask_sh)

#define DDC_MASK_SH_LIST(mask_sh) \
		DDC_MASK_SH_LIST_COMMON(mask_sh),\
		SF_DDC(DC_GPIO_I2CPAD_MASK, DC_GPIO_SDA_PD_DIS, mask_sh),\
		SF_DDC(DC_GPIO_I2CPAD_MASK, DC_GPIO_SCL_PD_DIS, mask_sh)

#define DDC_MASK_SH_LIST_DCN2(mask_sh, cd) \
	{DDC_MASK_SH_LIST_COMMON(mask_sh),\
	0,\
	0,\
	(PHY_AUX_CNTL__AUX## cd ##_PAD_RXSEL## mask_sh),\
	(DC_GPIO_AUX_CTRL_5__DDC_PAD## cd ##_I2CMODE## mask_sh)}

struct ddc_registers {
	struct gpio_registers gpio;
	uint32_t ddc_setup;
	uint32_t phy_aux_cntl;
	uint32_t dc_gpio_aux_ctrl_5;
};

struct ddc_sh_mask {
	/* i2c_dd_setup */
	uint32_t DC_I2C_DDC1_ENABLE;
	uint32_t DC_I2C_DDC1_EDID_DETECT_ENABLE;
	uint32_t DC_I2C_DDC1_EDID_DETECT_MODE;
	/* ddc1_mask */
	uint32_t DC_GPIO_DDC1DATA_PD_EN;
	uint32_t DC_GPIO_DDC1CLK_PD_EN;
	uint32_t AUX_PAD1_MODE;
	/* i2cpad_mask */
	uint32_t DC_GPIO_SDA_PD_DIS;
	uint32_t DC_GPIO_SCL_PD_DIS;
	//phy_aux_cntl
	uint32_t AUX_PAD_RXSEL;
	uint32_t DDC_PAD_I2CMODE;
};



/*** following in dc_resource */

#define ddc_data_regs(id) \
{\
	DDC_REG_LIST(DATA,id)\
}

#define ddc_clk_regs(id) \
{\
	DDC_REG_LIST(CLK,id)\
}

#define ddc_vga_data_regs \
{\
	DDC_VGA_REG_LIST(DATA)\
}

#define ddc_vga_clk_regs \
{\
	DDC_VGA_REG_LIST(CLK)\
}

#define ddc_i2c_data_regs \
{\
	DDC_I2C_REG_LIST(SDA)\
}

#define ddc_i2c_clk_regs \
{\
	DDC_I2C_REG_LIST(SCL)\
}
#define ddc_data_regs_dcn2(id) \
{\
	DDC_REG_LIST_DCN2(DATA, id)\
}

#define ddc_clk_regs_dcn2(id) \
{\
	DDC_REG_LIST_DCN2(CLK, id)\
}

#define ddc_i2c_data_regs_dcn2 \
{\
	DDC_I2C_REG_LIST_DCN2(SDA)\
}

#define ddc_i2c_clk_regs_dcn2 \
{\
	DDC_REG_LIST_DCN2(SCL)\
}


#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_GPIO_DDC_REGS_H_ */
