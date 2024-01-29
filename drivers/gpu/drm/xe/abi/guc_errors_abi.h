/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_ERRORS_ABI_H
#define _ABI_GUC_ERRORS_ABI_H

enum xe_guc_response_status {
	XE_GUC_RESPONSE_STATUS_SUCCESS = 0x0,
	XE_GUC_RESPONSE_STATUS_GENERIC_FAIL = 0xF000,
};

enum xe_guc_load_status {
	XE_GUC_LOAD_STATUS_DEFAULT                          = 0x00,
	XE_GUC_LOAD_STATUS_START                            = 0x01,
	XE_GUC_LOAD_STATUS_ERROR_DEVID_BUILD_MISMATCH       = 0x02,
	XE_GUC_LOAD_STATUS_GUC_PREPROD_BUILD_MISMATCH       = 0x03,
	XE_GUC_LOAD_STATUS_ERROR_DEVID_INVALID_GUCTYPE      = 0x04,
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

#endif
