/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_STATUS_H_
#define _ICE_STATUS_H_

/* Error Codes */
enum ice_status {
	ICE_SUCCESS				= 0,

	/* Generic codes : Range -1..-49 */
	ICE_ERR_PARAM				= -1,
	ICE_ERR_NOT_IMPL			= -2,
	ICE_ERR_NOT_READY			= -3,
	ICE_ERR_NOT_SUPPORTED			= -4,
	ICE_ERR_BAD_PTR				= -5,
	ICE_ERR_INVAL_SIZE			= -6,
	ICE_ERR_DEVICE_NOT_SUPPORTED		= -8,
	ICE_ERR_RESET_FAILED			= -9,
	ICE_ERR_FW_API_VER			= -10,
	ICE_ERR_NO_MEMORY			= -11,
	ICE_ERR_CFG				= -12,
	ICE_ERR_OUT_OF_RANGE			= -13,
	ICE_ERR_ALREADY_EXISTS			= -14,
	ICE_ERR_DOES_NOT_EXIST			= -15,
	ICE_ERR_IN_USE				= -16,
	ICE_ERR_MAX_LIMIT			= -17,
	ICE_ERR_RESET_ONGOING			= -18,
	ICE_ERR_NVM_CHECKSUM			= -51,
	ICE_ERR_BUF_TOO_SHORT			= -52,
	ICE_ERR_NVM_BLANK_MODE			= -53,
	ICE_ERR_AQ_ERROR			= -100,
	ICE_ERR_AQ_TIMEOUT			= -101,
	ICE_ERR_AQ_FULL				= -102,
	ICE_ERR_AQ_NO_WORK			= -103,
	ICE_ERR_AQ_EMPTY			= -104,
};

#endif /* _ICE_STATUS_H_ */
