/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * Global header for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MFC_ERRNO_H
#define __MFC_ERRNO_H __FILE__

enum mfc_ret_code {
	MFC_OK = 1,
	MFC_FAIL = -1000,
	MFC_OPEN_FAIL = -1001,
	MFC_CLOSE_FAIL = -1002,

	MFC_DEC_INIT_FAIL = -2000,
	MFC_DEC_EXE_TIME_OUT = -2001,
	MFC_DEC_EXE_ERR = -2002,
	MFC_DEC_GET_INBUF_FAIL = 2003,
	MFC_DEC_SET_INBUF_FAIL = 2004,
	MFC_DEC_GET_OUTBUF_FAIL = -2005,
	MFC_DEC_GET_CONF_FAIL = -2006,
	MFC_DEC_SET_CONF_FAIL = -2007,

	MFC_ENC_INIT_FAIL = -3000,
	MFC_ENC_EXE_TIME_OUT = -3001,
	MFC_ENC_EXE_ERR = -3002,
	MFC_ENC_GET_INBUF_FAIL = -3003,
	MFC_ENC_SET_INBUF_FAIL = -3004,
	MFC_ENC_GET_OUTBUF_FAIL = -3005,
	MFC_ENC_SET_OUTBUF_FAIL = -3006,
	MFC_ENC_GET_CONF_FAIL = -3007,
	MFC_ENC_SET_CONF_FAIL = -3008,

	MFC_STATE_INVALID = -4000,
	MFC_DEC_HEADER_FAIL = -4001,
	MFC_DEC_INIT_BUF_FAIL = -4002,
	MFC_ENC_HEADER_FAIL = -5000,
	MFC_ENC_PARAM_FAIL = -5001,
	MFC_FRM_BUF_SIZE_FAIL = -6000,
	MFC_FW_LOAD_FAIL = -6001,
	MFC_FW_INIT_FAIL = -6002,
	MFC_INST_NUM_EXCEEDED_FAIL = -6003,
	MFC_MEM_ALLOC_FAIL = -6004,
	MFC_MEM_INVALID_ADDR_FAIL = -6005,
	MFC_MEM_MAPPING_FAIL = -6006,
	MFC_GET_CONF_FAIL = -6007,
	MFC_SET_CONF_FAIL = -6008,
	MFC_INVALID_PARAM_FAIL = -6009,
	MFC_API_FAIL = -9000,

	MFC_CMD_FAIL = -1003,
	MFC_SLEEP_FAIL = -1010,
	MFC_WAKEUP_FAIL = -1020,

	MFC_CLK_ON_FAIL = -1030,
	MFC_CLK_OFF_FAIL = -1030,
	MFC_PWR_ON_FAIL = -1040,
	MFC_PWR_OFF_FAIL = -1041,
} ;

#endif /* __MFC_ERRNO_H */
