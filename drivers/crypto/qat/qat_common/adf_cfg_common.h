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
	DEV_C3XXXVF
};

struct adf_dev_status_info {
	enum adf_device_type type;
	u32 accel_id;
	u32 instance_id;
	uint8_t num_ae;
	uint8_t num_accel;
	uint8_t num_logical_accel;
	uint8_t banks_per_accel;
	uint8_t state;
	uint8_t bus;
	uint8_t dev;
	uint8_t fun;
	char name[MAX_DEVICE_NAME_SIZE];
};

#define ADF_CTL_IOC_MAGIC 'a'
#define IOCTL_CONFIG_SYS_RESOURCE_PARAMETERS _IOW(ADF_CTL_IOC_MAGIC, 0, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_STOP_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 1, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_START_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 2, \
		struct adf_user_cfg_ctl_data)
#define IOCTL_STATUS_ACCEL_DEV _IOW(ADF_CTL_IOC_MAGIC, 3, uint32_t)
#define IOCTL_GET_NUM_DEVICES _IOW(ADF_CTL_IOC_MAGIC, 4, int32_t)
#endif
