/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Intel Corporation.
 */
#ifndef INTEL_SAR_H
#define INTEL_SAR_H

#define COMMAND_ID_DEV_MODE 1
#define COMMAND_ID_CONFIG_TABLE 2
#define DRVNAME "intc_sar"
#define MAX_DEV_MODES 50
#define MAX_REGULATORY 3
#define SAR_DSM_UUID "82737E72-3A33-4C45-A9C7-57C0411A5F13"
#define SAR_EVENT 0x80
#define SYSFS_DATANAME "intc_data"
#define TOTAL_DATA 4

/**
 * Structure wwan_device_mode_info - device mode information
 * Holds the data that needs to be passed to userspace.
 * The data is updated from the BIOS sensor information.
 * @device_mode: Specific mode of the device
 * @bandtable_index: Index of RF band
 * @antennatable_index: Index of antenna
 * @sartable_index: Index of SAR
 */
struct wwan_device_mode_info {
	int device_mode;
	int bandtable_index;
	int antennatable_index;
	int sartable_index;
};

/**
 * Structure wwan_device_mode_configuration - device configuration
 * Holds the data that is configured and obtained on probe event.
 * The data is updated from the BIOS sensor information.
 * @version: Mode configuration version
 * @total_dev_mode: Total number of device modes
 * @device_mode_info: pointer to structure wwan_device_mode_info
 */
struct wwan_device_mode_configuration {
	int version;
	int total_dev_mode;
	struct wwan_device_mode_info *device_mode_info;
};

/**
 * Structure wwan_supported_info - userspace datastore
 * Holds the data that is obtained from userspace
 * The data is updated from the userspace and send value back in the
 * structure format that is mentioned here.
 * @reg_mode_needed: regulatory mode set by user for tests
 * @bios_table_revision: Version of SAR table
 * @num_supported_modes: Total supported modes based on reg_mode
 */
struct wwan_supported_info {
	int reg_mode_needed;
	int bios_table_revision;
	int num_supported_modes;
};

/**
 * Structure wwan_sar_context - context of SAR
 * Holds the complete context as long as the driver is in existence
 * The context holds instance of the data used for different cases.
 * @guid: Group id
 * @handle: store acpi handle
 * @reg_value: regulatory value
 * Regulatory 0: FCC, 1: CE, 2: ISED
 * @sar_device: platform_device type
 * @sar_kobject: kobject for sysfs
 * @supported_data: wwan_supported_info struct
 * @sar_data: wwan_device_mode_info struct
 * @config_data: wwan_device_mode_configuration array struct
 */
struct wwan_sar_context {
	guid_t guid;
	acpi_handle handle;
	int reg_value;
	struct platform_device *sar_device;
	struct wwan_supported_info supported_data;
	struct wwan_device_mode_info sar_data;
	struct wwan_device_mode_configuration config_data[MAX_REGULATORY];
};
#endif /* INTEL_SAR_H */
