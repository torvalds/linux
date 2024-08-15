/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD MP2 1.1 communication interfaces
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef AMD_SFH_INTERFACE_H
#define AMD_SFH_INTERFACE_H

#include "../amd_sfh_common.h"

#define SENSOR_DATA_MEM_SIZE_DEFAULT		256
#define TOTAL_STATIC_MEM_DEFAULT		1024
#define OFFSET_SFH_INFO_BASE_DEFAULT		0
#define OFFSET_SENSOR_DATA_DEFAULT		(OFFSET_SFH_INFO_BASE_DEFAULT + \
							TOTAL_STATIC_MEM_DEFAULT)
enum sensor_index {
	ACCEL_IDX,
	GYRO_IDX,
	MAG_IDX,
	ALS_IDX = 4,
	HPD_IDX = 5,
	MAX_IDX = 15,
};

struct sfh_cmd_base {
	union {
		u32 ul;
		struct {
			u32 sensor_id		: 4;
			u32 cmd_id		: 4;
			u32 sub_cmd_id		: 8;
			u32 sub_cmd_value	: 12;
			u32 rsvd		: 3;
			u32 intr_disable	: 1;
		} cmd;
	};
};

struct sfh_cmd_response {
	union {
		u32 resp;
		struct {
			u32 response	: 8;
			u32 sensor_id	: 4;
			u32 cmd_id	: 4;
			u32 sub_cmd	: 6;
			u32 rsvd2	: 10;
		} response;
	};
};

struct sfh_platform_info {
	union {
		u32 pi;
		struct {
			u32 cust_id		: 16;
			u32 plat_id		: 6;
			u32 interface_id	: 4;
			u32 rsvd		: 6;
		} pinfo;
	};
};

struct sfh_firmware_info {
	union {
		u32 fw_ver;
		struct {
			u32 minor_rev : 8;
			u32 major_rev : 8;
			u32 minor_ver : 8;
			u32 major_ver : 8;
		} fver;
	};
};

struct sfh_sensor_list {
	union {
		u32 slist;
		struct {
			u32 sensors	: 16;
			u32 rsvd	: 16;
		} sl;
	};
};

struct sfh_base_info {
	union {
		u32 sfh_base[24];
		struct {
			struct sfh_platform_info plat_info;
			struct sfh_firmware_info  fw_info;
			struct sfh_sensor_list s_list;
		} sbase;
	};
};

struct sfh_common_data {
	u64 timestamp;
	u32 intr_cnt;
	u32 featvalid		: 16;
	u32 rsvd		: 13;
	u32 sensor_state	: 3;
};

struct sfh_float32 {
	u32 x;
	u32 y;
	u32 z;
};

struct sfh_accel_data {
	struct sfh_common_data commondata;
	struct sfh_float32 acceldata;
	u32 accelstatus;
};

struct sfh_gyro_data {
	struct sfh_common_data commondata;
	struct sfh_float32 gyrodata;
	u32 result;
};

struct sfh_mag_data {
	struct sfh_common_data commondata;
	struct sfh_float32 magdata;
	u32 accuracy;
};

struct sfh_als_data {
	struct sfh_common_data commondata;
	u32 lux;
};

struct hpd_status {
	union {
		struct {
			u32 distance			: 16;
			u32 probablity			: 8;
			u32 presence			: 2;
			u32 rsvd			: 5;
			u32 state			: 1;
		} shpd;
		u32 val;
	};
};

void sfh_interface_init(struct amd_mp2_dev *mp2);
void amd_sfh1_1_set_desc_ops(struct amd_mp2_ops *mp2_ops);
#endif
