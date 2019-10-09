/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef IMX258_EEPROM_HEAD_H
#define IMX258_EEPROM_HEAD_H

#define SLAVE_ADDRESS_GZ	0x50
#define GZ_INFO_FLAG_REG	0X0000
#define GZ_ID_REG		0X0005
#define GZ_LENS_ID_REG		0X0006
#define GZ_PRODUCT_YEAR_REG	0X000A
#define GZ_PRODUCT_MONTH_REG	0X000B
#define GZ_PRODUCT_DAY_REG	0X000C
#define GZ_AWB_FLAG_REG		0x001c
#define GZ_CUR_R_REG		0x001d
#define GZ_CUR_GR_REG		0x001e
#define GZ_CUR_GB_REG		0x001f
#define GZ_CUR_B_REG		0x0020
#define GZ_GOLDEN_R_REG		0x0021
#define GZ_GOLDEN_GR_REG	0x0022
#define GZ_GOLDEN_GB_REG	0x0023
#define GZ_GOLDEN_B_REG		0x0024
#define GZ_AWB_CHECKSUM_REG	0x0025
#define GZ_LSC_FLAG_REG		0X003A
#define GZ_LSC_DATA_START_REG	0x003B
#define GZ_LSC_CHECKSUM_REG	0x0233
#define GZ_VCM_FLAG_REG		0X0788
#define GZ_VCM_DIR_REG		0X0789
#define GZ_VCM_START_REG	0X078C
#define GZ_VCM_END_REG		0X078A
#define GZ_VCM_CHECKSUM_REG	0x0790
#define GZ_SPC_FLAG_REG		0X0CE1
#define GZ_SPC_DATA_START_REG	0x0CE2
#define GZ_SPC_CHECKSUM_REG	0x0d60

struct imx258_otp_info {
	u32 flag; //bit[7]: info bit[6]:wb bit[5]:vcm bit[4]:lenc bit[3]:spc
	u32 module_id;
	u32 lens_id;
	u32 year;
	u32 month;
	u32 day;
	u32 rg_ratio;
	u32 bg_ratio;
	u32 rg_golden;
	u32 bg_golden;
	int vcm_start;
	int vcm_end;
	int vcm_dir;
	u8  lenc[504];
	u8  spc[126];
};

/* imx258_eeprom device structure */
struct imx258_eeprom_device {
	struct v4l2_subdev sd;
	struct i2c_client *client;
	struct imx258_otp_info *otp;
};

#endif /* IMX258_EEPROM_HEAD_H */

