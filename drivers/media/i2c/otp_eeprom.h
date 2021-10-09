/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef OTP_EEPROM_H
#define OTP_EEPROM_H

#define SLAVE_ADDRESS	0x50

#define INFO_FLAG_REG	0X0000
#define INFO_ID_REG		0X0001
#define SMARTISAN_PN_REG	0X000A
#define SMARTISAN_PN_SIZE	0x000C		//12
#define MOUDLE_ID_REG	0X0016
#define MOUDLE_ID_SIZE	0X0010		//16
#define MIRROR_FLIP_REG	0X0026
#define FULL_SIZE_WIGHT_REG	0X0027
#define FULL_SIZE_HEIGHT_REG	0X0029
#define INFO_CHECKSUM_REG	0X0033

#define AWB_FLAG_REG		0x0034
#define AWB_VERSION_REG	0x0035
#define CUR_R_REG		0x0036
#define CUR_B_REG		0x0038
#define CUR_G_REG		0x003A
#define GOLDEN_R_REG		0x003C
#define GOLDEN_B_REG		0x003E
#define GOLDEN_G_REG		0x0040
#define AWB_CHECKSUM_REG	0x0062

#define LSC_FLAG_REG		0X0063
#define LSC_VERSION_REG	0x0064
#define LSC_TABLE_SIZE_REG	0x0065
#define LSC_DATA_START_REG	0x0067
#define LSC_DATA_SIZE	0x0908		//2312
#define LSC_CHECKSUM_REG	0x097B

#define SFR_FLAG_REG		0X097C
#define SFR_EQUIQ_NUM_REG	0X097D
#define SFR_C_HOR_REG	0X097E
#define SFR_C_VER_REG	0X0980
#define SFR_TOP_L_HOR_REG	0X0982
#define SFR_TOP_L_VER_REG	0X0984
#define SFR_TOP_R_HOR_REG	0X0986
#define SFR_TOP_R_VER_REG	0X0988
#define SFR_BOTTOM_L_HOR_REG	0X098A
#define SFR_BOTTOM_L_VER_REG	0X098C
#define SFR_BOTTOM_R_HOR_REG	0X098E
#define SFR_BOTTOM_R_VER_REG	0X0990
#define SFR_CHECKSUM_REG	0x09BE

#define TOTAL_CHECKSUM_REG	0x09BF

struct id_defination {
	u32 supplier_id;
	u32 year;
	u32 month;
	u32 day;
	u32 sensor_id;
	u32 lens_id;
	u32 vcm_id;
	u32 driver_ic_id;
	u32 color_temperature_id;
};

struct full_size {
	u16 width;
	u16 height;
};

struct basic_info {
	u32 flag;
	struct id_defination id;
	u32 smartisan_pn[SMARTISAN_PN_SIZE];
	u32 modul_id[MOUDLE_ID_SIZE];
	u32 mirror_flip;
	struct full_size size;
	u32 checksum;
};

struct awb_otp_info {
	u32 flag;
	u32 version;
	u32 r_ratio;
	u32 b_ratio;
	u32 g_ratio;
	u32 r_golden;
	u32 b_golden;
	u32 g_golden;
	u32 checksum;
};

struct lsc_otp_info {
	u32 flag;
	u32 version;
	u16 table_size;
	u8 data[LSC_DATA_SIZE];
	u32 checksum;
};

struct sfr_data {
	u32 top_l_horizontal;
	u32 top_l_vertical;
	u32 top_r_horizontal;
	u32 top_r_vertical;
	u32 bottom_l_horizontal;
	u32 bottom_l_vertical;
	u32 bottom_r_horizontal;
	u32 bottom_r_vertical;
};

struct sfr_otp_info {
	u32 flag;
	u32 equip_num;
	u32 center_horizontal;
	u32 center_vertical;
	struct sfr_data data[3];
	u32 checksum;
};

struct otp_info {
	struct basic_info basic_data;
	struct awb_otp_info awb_data;
	struct lsc_otp_info lsc_data;
	struct sfr_otp_info sfr_otp_data;
	u32 total_checksum;
};

/* eeprom device structure */
struct eeprom_device {
	struct v4l2_subdev sd;
	struct i2c_client *client;
	struct otp_info *otp;
	struct proc_dir_entry *procfs;
	char name[128];
};

#endif /* OTP_EEPROM_H */
