/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_STATUS_H_
#define _ICE_STATUS_H_

/* Error Codes */
enum ice_status {
	ICE_ERR_PARAM				= -1,
	ICE_ERR_NOT_READY			= -3,
	ICE_ERR_INVAL_SIZE			= -6,
	ICE_ERR_FW_API_VER			= -10,
	ICE_ERR_NO_MEMORY			= -11,
	ICE_ERR_CFG				= -12,
	ICE_ERR_AQ_ERROR			= -100,
	ICE_ERR_AQ_TIMEOUT			= -101,
	ICE_ERR_AQ_FULL				= -102,
	ICE_ERR_AQ_EMPTY			= -104,
};

#endif /* _ICE_STATUS_H_ */
