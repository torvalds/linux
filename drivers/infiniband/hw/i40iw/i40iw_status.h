/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#ifndef I40IW_STATUS_H
#define I40IW_STATUS_H

/* Error Codes */
enum i40iw_status_code {
	I40IW_SUCCESS = 0,
	I40IW_ERR_NVM = -1,
	I40IW_ERR_NVM_CHECKSUM = -2,
	I40IW_ERR_CONFIG = -4,
	I40IW_ERR_PARAM = -5,
	I40IW_ERR_DEVICE_NOT_SUPPORTED = -6,
	I40IW_ERR_RESET_FAILED = -7,
	I40IW_ERR_SWFW_SYNC = -8,
	I40IW_ERR_NO_MEMORY = -9,
	I40IW_ERR_BAD_PTR = -10,
	I40IW_ERR_INVALID_PD_ID = -11,
	I40IW_ERR_INVALID_QP_ID = -12,
	I40IW_ERR_INVALID_CQ_ID = -13,
	I40IW_ERR_INVALID_CEQ_ID = -14,
	I40IW_ERR_INVALID_AEQ_ID = -15,
	I40IW_ERR_INVALID_SIZE = -16,
	I40IW_ERR_INVALID_ARP_INDEX = -17,
	I40IW_ERR_INVALID_FPM_FUNC_ID = -18,
	I40IW_ERR_QP_INVALID_MSG_SIZE = -19,
	I40IW_ERR_QP_TOOMANY_WRS_POSTED = -20,
	I40IW_ERR_INVALID_FRAG_COUNT = -21,
	I40IW_ERR_QUEUE_EMPTY = -22,
	I40IW_ERR_INVALID_ALIGNMENT = -23,
	I40IW_ERR_FLUSHED_QUEUE = -24,
	I40IW_ERR_INVALID_PUSH_PAGE_INDEX = -25,
	I40IW_ERR_INVALID_INLINE_DATA_SIZE = -26,
	I40IW_ERR_TIMEOUT = -27,
	I40IW_ERR_OPCODE_MISMATCH = -28,
	I40IW_ERR_CQP_COMPL_ERROR = -29,
	I40IW_ERR_INVALID_VF_ID = -30,
	I40IW_ERR_INVALID_HMCFN_ID = -31,
	I40IW_ERR_BACKING_PAGE_ERROR = -32,
	I40IW_ERR_NO_PBLCHUNKS_AVAILABLE = -33,
	I40IW_ERR_INVALID_PBLE_INDEX = -34,
	I40IW_ERR_INVALID_SD_INDEX = -35,
	I40IW_ERR_INVALID_PAGE_DESC_INDEX = -36,
	I40IW_ERR_INVALID_SD_TYPE = -37,
	I40IW_ERR_MEMCPY_FAILED = -38,
	I40IW_ERR_INVALID_HMC_OBJ_INDEX = -39,
	I40IW_ERR_INVALID_HMC_OBJ_COUNT = -40,
	I40IW_ERR_INVALID_SRQ_ARM_LIMIT = -41,
	I40IW_ERR_SRQ_ENABLED = -42,
	I40IW_ERR_BUF_TOO_SHORT = -43,
	I40IW_ERR_BAD_IWARP_CQE = -44,
	I40IW_ERR_NVM_BLANK_MODE = -45,
	I40IW_ERR_NOT_IMPLEMENTED = -46,
	I40IW_ERR_PE_DOORBELL_NOT_ENABLED = -47,
	I40IW_ERR_NOT_READY = -48,
	I40IW_NOT_SUPPORTED = -49,
	I40IW_ERR_FIRMWARE_API_VERSION = -50,
	I40IW_ERR_RING_FULL = -51,
	I40IW_ERR_MPA_CRC = -61,
	I40IW_ERR_NO_TXBUFS = -62,
	I40IW_ERR_SEQ_NUM = -63,
	I40IW_ERR_list_empty = -64,
	I40IW_ERR_INVALID_MAC_ADDR = -65,
	I40IW_ERR_BAD_STAG      = -66,
	I40IW_ERR_CQ_COMPL_ERROR = -67,
	I40IW_ERR_QUEUE_DESTROYED = -68,
	I40IW_ERR_INVALID_FEAT_CNT = -69

};
#endif
