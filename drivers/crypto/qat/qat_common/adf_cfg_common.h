/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_CFG_COMMON_H_
#define ADF_CFG_COMMON_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define ADF_CFG_MAX_STR_LEN 64
#define ADF_CFG_MAX_KEY_LEN_IN_BYTES ADF_CFG_MAX_STR_LEN
#define ADF_CFG_MAX_VAL_LEN_IN_BYTES ADF_CFG_MAX_STR_LEN
#define ADF_CFG_MAX_SECTION_LEN_IN_BYTES ADF_CFG_MAX_STR_LEN
#define ADF_CFG_BASE_DEC 10
#define ADF_CFG_BASE_HEX 16
#define ADF_CFG_ALL_DEVICES 0xFE
#define ADF_CFG_NO_DEVICE 0xFF
#define ADF_CFG_AFFINITY_WHATEVER 0xFF
#define MAX_DEVICE_NAME_SIZE 32
#define ADF_MAX_DEVICES (32 * 32)
#define ADF_DEVS_ARRAY_SIZE BITS_TO_LONGS(ADF_MAX_DEVICES)

#define ADF_CFG_SERV_RING_PAIR_0_SHIFT 0
#define ADF_CFG_SERV_RING_PAIR_1_SHIFT 3
#define ADF_CFG_SERV_RING_PAIR_2_SHIFT 6
#define ADF_CFG_SERV_RING_PAIR_3_SHIFT 9
enum adf_cfg_service_type {
	UNUSED = 0,
	CRYPTO,
	COMP,
	SYM,
	ASYM,
	USED
};

enum adf_cfg_val_type {
	ADF_DEC,
	ADF_HEX,
	ADF_STR
};

enum adf_device_type {
	DEV_UNKNOWN = 0,
	DEV_DH895XCC,
	DEV_DH895XCCVF,
	DEV_C62X,
	DEV_C62XVF,
	DEV_C3XXX,
	DEV_C3XXXVF,
	DEV_4XXX,
};

struct adf_dev_status_info {
	enum adf_device_type type;
	__u32 accel_id;
	__u32 instance_id;
	__u8 num_ae;
	__u8 num_accel;
	__u8 num_logical_accel;
	__u8 banks_per_accel;
	__u8 state;
	__u8 bus;
	__u8 dev;
	__u8 fun;
	char name[MAX_DEVICE_NAME_SIZE];
};

#define ADF_CTL_IOC_MAGIC 'a'
#define IOCTL_CONFIG_SYS_RESOURCE_PARAMETERS _IOW(ADF_CTL_IOC_MAGIC, 0, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_STOP_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 1, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_START_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 2, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_STATUS_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 3, __u32)
#define IOCTL_GET_NUM_DEVICES _IOW(ADF_CTL_IOC_MAGIC, 4, __s32)
#endif
