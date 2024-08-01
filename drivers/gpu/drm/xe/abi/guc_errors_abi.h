/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_ERRORS_ABI_H
#define _ABI_GUC_ERRORS_ABI_H

enum xe_guc_response_status {
	XE_GUC_RESPONSE_STATUS_SUCCESS                      = 0x0,
	XE_GUC_RESPONSE_ERROR_PROTOCOL                      = 0x04,
	XE_GUC_RESPONSE_INVALID_STATE                       = 0x0A,
	XE_GUC_RESPONSE_UNSUPPORTED_VERSION                 = 0x0B,
	XE_GUC_RESPONSE_INVALID_VFID                        = 0x0C,
	XE_GUC_RESPONSE_UNPROVISIONED_VF                    = 0x0D,
	XE_GUC_RESPONSE_INVALID_EVENT                       = 0x0E,
	XE_GUC_RESPONSE_NOT_SUPPORTED                       = 0x20,
	XE_GUC_RESPONSE_UNKNOWN_ACTION                      = 0x30,
	XE_GUC_RESPONSE_ACTION_ABORTED                      = 0x31,
	XE_GUC_RESPONSE_NO_PERMISSION                       = 0x40,
	XE_GUC_RESPONSE_CANNOT_COMPLETE_ACTION              = 0x41,
	XE_GUC_RESPONSE_INVALID_KLV_DATA                    = 0x50,
	XE_GUC_RESPONSE_INVALID_PARAMS                      = 0x60,
	XE_GUC_RESPONSE_INVALID_BUFFER_RANGE                = 0x70,
	XE_GUC_RESPONSE_INVALID_BUFFER                      = 0x71,
	XE_GUC_RESPONSE_INVALID_GGTT_ADDRESS                = 0x80,
	XE_GUC_RESPONSE_PENDING_ACTION                      = 0x90,
	XE_GUC_RESPONSE_INVALID_SIZE                        = 0x102,
	XE_GUC_RESPONSE_MALFORMED_KLV                       = 0x103,
	XE_GUC_RESPONSE_INVALID_KLV_KEY                     = 0x105,
	XE_GUC_RESPONSE_DATA_TOO_LARGE                      = 0x106,
	XE_GUC_RESPONSE_VF_MIGRATED                         = 0x107,
	XE_GUC_RESPONSE_NO_ATTRIBUTE_TABLE                  = 0x201,
	XE_GUC_RESPONSE_NO_DECRYPTION_KEY                   = 0x202,
	XE_GUC_RESPONSE_DECRYPTION_FAILED                   = 0x204,
	XE_GUC_RESPONSE_VGT_DISABLED                        = 0x300,
	XE_GUC_RESPONSE_CTB_FULL                            = 0x301,
	XE_GUC_RESPONSE_VGT_UNAUTHORIZED_REQUEST            = 0x302,
	XE_GUC_RESPONSE_CTB_INVALID                         = 0x303,
	XE_GUC_RESPONSE_CTB_NOT_REGISTERED                  = 0x304,
	XE_GUC_RESPONSE_CTB_IN_USE                          = 0x305,
	XE_GUC_RESPONSE_CTB_INVALID_DESC                    = 0x306,
	XE_GUC_RESPONSE_CTB_SOURCE_INVALID_DESCRIPTOR       = 0x30D,
	XE_GUC_RESPONSE_CTB_DESTINATION_INVALID_DESCRIPTOR  = 0x30E,
	XE_GUC_RESPONSE_INVALID_CONFIG_STATE                = 0x30F,
	XE_GUC_RESPONSE_STATUS_GENERIC_FAIL                 = 0xF000,
};

enum xe_guc_load_status {
	XE_GUC_LOAD_STATUS_DEFAULT                          = 0x00,
	XE_GUC_LOAD_STATUS_START                            = 0x01,
	XE_GUC_LOAD_STATUS_ERROR_DEVID_BUILD_MISMATCH       = 0x02,
	XE_GUC_LOAD_STATUS_GUC_PREPROD_BUILD_MISMATCH       = 0x03,
	XE_GUC_LOAD_STATUS_ERROR_DEVID_INVALID_GUCTYPE      = 0x04,
	XE_GUC_LOAD_STATUS_HWCONFIG_START                   = 0x05,
	XE_GUC_LOAD_STATUS_HWCONFIG_DONE                    = 0x06,
	XE_GUC_LOAD_STATUS_HWCONFIG_ERROR                   = 0x07,
	XE_GUC_LOAD_STATUS_GDT_DONE                         = 0x10,
	XE_GUC_LOAD_STATUS_IDT_DONE                         = 0x20,
	XE_GUC_LOAD_STATUS_LAPIC_DONE                       = 0x30,
	XE_GUC_LOAD_STATUS_GUCINT_DONE                      = 0x40,
	XE_GUC_LOAD_STATUS_DPC_READY                        = 0x50,
	XE_GUC_LOAD_STATUS_DPC_ERROR                        = 0x60,
	XE_GUC_LOAD_STATUS_EXCEPTION                        = 0x70,
	XE_GUC_LOAD_STATUS_INIT_DATA_INVALID                = 0x71,
	XE_GUC_LOAD_STATUS_PXP_TEARDOWN_CTRL_ENABLED        = 0x72,
	XE_GUC_LOAD_STATUS_INVALID_INIT_DATA_RANGE_START,
	XE_GUC_LOAD_STATUS_MPU_DATA_INVALID                 = 0x73,
	XE_GUC_LOAD_STATUS_INIT_MMIO_SAVE_RESTORE_INVALID   = 0x74,
	XE_GUC_LOAD_STATUS_INVALID_INIT_DATA_RANGE_END,

	XE_GUC_LOAD_STATUS_READY                            = 0xF0,
};

enum xe_bootrom_load_status {
	XE_BOOTROM_STATUS_NO_KEY_FOUND                      = 0x13,
	XE_BOOTROM_STATUS_AES_PROD_KEY_FOUND                = 0x1A,
	XE_BOOTROM_STATUS_PROD_KEY_CHECK_FAILURE            = 0x2B,
	XE_BOOTROM_STATUS_RSA_FAILED                        = 0x50,
	XE_BOOTROM_STATUS_PAVPC_FAILED                      = 0x73,
	XE_BOOTROM_STATUS_WOPCM_FAILED                      = 0x74,
	XE_BOOTROM_STATUS_LOADLOC_FAILED                    = 0x75,
	XE_BOOTROM_STATUS_JUMP_PASSED                       = 0x76,
	XE_BOOTROM_STATUS_JUMP_FAILED                       = 0x77,
	XE_BOOTROM_STATUS_RC6CTXCONFIG_FAILED               = 0x79,
	XE_BOOTROM_STATUS_MPUMAP_INCORRECT                  = 0x7A,
	XE_BOOTROM_STATUS_EXCEPTION                         = 0x7E,
};

#endif
