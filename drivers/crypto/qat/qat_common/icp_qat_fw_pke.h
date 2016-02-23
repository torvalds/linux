/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in
	  the documentation and/or other materials provided with the
	  distribution.
	* Neither the name of Intel Corporation nor the names of its
	  contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef _ICP_QAT_FW_PKE_
#define _ICP_QAT_FW_PKE_

#include "icp_qat_fw.h"

struct icp_qat_fw_req_hdr_pke_cd_pars {
	u64 content_desc_addr;
	u32 content_desc_resrvd;
	u32 func_id;
};

struct icp_qat_fw_req_pke_mid {
	u64 opaque;
	u64 src_data_addr;
	u64 dest_data_addr;
};

struct icp_qat_fw_req_pke_hdr {
	u8 resrvd1;
	u8 resrvd2;
	u8 service_type;
	u8 hdr_flags;
	u16 comn_req_flags;
	u16 resrvd4;
	struct icp_qat_fw_req_hdr_pke_cd_pars cd_pars;
};

struct icp_qat_fw_pke_request {
	struct icp_qat_fw_req_pke_hdr pke_hdr;
	struct icp_qat_fw_req_pke_mid pke_mid;
	u8 output_param_count;
	u8 input_param_count;
	u16 resrvd1;
	u32 resrvd2;
	u64 next_req_adr;
};

struct icp_qat_fw_resp_pke_hdr {
	u8 resrvd1;
	u8 resrvd2;
	u8 response_type;
	u8 hdr_flags;
	u16 comn_resp_flags;
	u16 resrvd4;
};

struct icp_qat_fw_pke_resp {
	struct icp_qat_fw_resp_pke_hdr pke_resp_hdr;
	u64 opaque;
	u64 src_data_addr;
	u64 dest_data_addr;
};

#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_BITPOS              7
#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_MASK                0x1
#define ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(status_word) \
	QAT_FIELD_GET(((status_word >> ICP_QAT_FW_COMN_ONE_BYTE_SHIFT) & \
		ICP_QAT_FW_COMN_SINGLE_BYTE_MASK), \
		QAT_COMN_RESP_PKE_STATUS_BITPOS, \
		QAT_COMN_RESP_PKE_STATUS_MASK)

#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_SET(hdr_t, val) \
	QAT_FIELD_SET((hdr_t.hdr_flags), (val), \
		ICP_QAT_FW_PKE_HDR_VALID_FLAG_BITPOS, \
		ICP_QAT_FW_PKE_HDR_VALID_FLAG_MASK)
#endif
