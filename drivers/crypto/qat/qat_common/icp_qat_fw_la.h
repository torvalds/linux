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
#ifndef _ICP_QAT_FW_LA_H_
#define _ICP_QAT_FW_LA_H_
#include "icp_qat_fw.h"

enum icp_qat_fw_la_cmd_id {
	ICP_QAT_FW_LA_CMD_CIPHER = 0,
	ICP_QAT_FW_LA_CMD_AUTH = 1,
	ICP_QAT_FW_LA_CMD_CIPHER_HASH = 2,
	ICP_QAT_FW_LA_CMD_HASH_CIPHER = 3,
	ICP_QAT_FW_LA_CMD_TRNG_GET_RANDOM = 4,
	ICP_QAT_FW_LA_CMD_TRNG_TEST = 5,
	ICP_QAT_FW_LA_CMD_SSL3_KEY_DERIVE = 6,
	ICP_QAT_FW_LA_CMD_TLS_V1_1_KEY_DERIVE = 7,
	ICP_QAT_FW_LA_CMD_TLS_V1_2_KEY_DERIVE = 8,
	ICP_QAT_FW_LA_CMD_MGF1 = 9,
	ICP_QAT_FW_LA_CMD_AUTH_PRE_COMP = 10,
	ICP_QAT_FW_LA_CMD_CIPHER_PRE_COMP = 11,
	ICP_QAT_FW_LA_CMD_DELIMITER = 12
};

#define ICP_QAT_FW_LA_ICV_VER_STATUS_PASS ICP_QAT_FW_COMN_STATUS_FLAG_OK
#define ICP_QAT_FW_LA_ICV_VER_STATUS_FAIL ICP_QAT_FW_COMN_STATUS_FLAG_ERROR
#define ICP_QAT_FW_LA_TRNG_STATUS_PASS ICP_QAT_FW_COMN_STATUS_FLAG_OK
#define ICP_QAT_FW_LA_TRNG_STATUS_FAIL ICP_QAT_FW_COMN_STATUS_FLAG_ERROR

struct icp_qat_fw_la_bulk_req {
	struct icp_qat_fw_comn_req_hdr comn_hdr;
	struct icp_qat_fw_comn_req_hdr_cd_pars cd_pars;
	struct icp_qat_fw_comn_req_mid comn_mid;
	struct icp_qat_fw_comn_req_rqpars serv_specif_rqpars;
	struct icp_qat_fw_comn_req_cd_ctrl cd_ctrl;
};

#define ICP_QAT_FW_LA_GCM_IV_LEN_12_OCTETS 1
#define ICP_QAT_FW_LA_GCM_IV_LEN_NOT_12_OCTETS 0
#define QAT_FW_LA_ZUC_3G_PROTO_FLAG_BITPOS 12
#define ICP_QAT_FW_LA_ZUC_3G_PROTO 1
#define QAT_FW_LA_ZUC_3G_PROTO_FLAG_MASK 0x1
#define QAT_LA_GCM_IV_LEN_FLAG_BITPOS 11
#define QAT_LA_GCM_IV_LEN_FLAG_MASK 0x1
#define ICP_QAT_FW_LA_DIGEST_IN_BUFFER 1
#define ICP_QAT_FW_LA_NO_DIGEST_IN_BUFFER 0
#define QAT_LA_DIGEST_IN_BUFFER_BITPOS	10
#define QAT_LA_DIGEST_IN_BUFFER_MASK 0x1
#define ICP_QAT_FW_LA_SNOW_3G_PROTO 4
#define ICP_QAT_FW_LA_GCM_PROTO	2
#define ICP_QAT_FW_LA_CCM_PROTO	1
#define ICP_QAT_FW_LA_NO_PROTO 0
#define QAT_LA_PROTO_BITPOS 7
#define QAT_LA_PROTO_MASK 0x7
#define ICP_QAT_FW_LA_CMP_AUTH_RES 1
#define ICP_QAT_FW_LA_NO_CMP_AUTH_RES 0
#define QAT_LA_CMP_AUTH_RES_BITPOS 6
#define QAT_LA_CMP_AUTH_RES_MASK 0x1
#define ICP_QAT_FW_LA_RET_AUTH_RES 1
#define ICP_QAT_FW_LA_NO_RET_AUTH_RES 0
#define QAT_LA_RET_AUTH_RES_BITPOS 5
#define QAT_LA_RET_AUTH_RES_MASK 0x1
#define ICP_QAT_FW_LA_UPDATE_STATE 1
#define ICP_QAT_FW_LA_NO_UPDATE_STATE 0
#define QAT_LA_UPDATE_STATE_BITPOS 4
#define QAT_LA_UPDATE_STATE_MASK 0x1
#define ICP_QAT_FW_CIPH_AUTH_CFG_OFFSET_IN_CD_SETUP 0
#define ICP_QAT_FW_CIPH_AUTH_CFG_OFFSET_IN_SHRAM_CP 1
#define QAT_LA_CIPH_AUTH_CFG_OFFSET_BITPOS 3
#define QAT_LA_CIPH_AUTH_CFG_OFFSET_MASK 0x1
#define ICP_QAT_FW_CIPH_IV_64BIT_PTR 0
#define ICP_QAT_FW_CIPH_IV_16BYTE_DATA 1
#define QAT_LA_CIPH_IV_FLD_BITPOS 2
#define QAT_LA_CIPH_IV_FLD_MASK   0x1
#define ICP_QAT_FW_LA_PARTIAL_NONE 0
#define ICP_QAT_FW_LA_PARTIAL_START 1
#define ICP_QAT_FW_LA_PARTIAL_MID 3
#define ICP_QAT_FW_LA_PARTIAL_END 2
#define QAT_LA_PARTIAL_BITPOS 0
#define QAT_LA_PARTIAL_MASK 0x3
#define ICP_QAT_FW_LA_FLAGS_BUILD(zuc_proto, gcm_iv_len, auth_rslt, proto, \
	cmp_auth, ret_auth, update_state, \
	ciph_iv, ciphcfg, partial) \
	(((zuc_proto & QAT_FW_LA_ZUC_3G_PROTO_FLAG_MASK) << \
	QAT_FW_LA_ZUC_3G_PROTO_FLAG_BITPOS) | \
	((gcm_iv_len & QAT_LA_GCM_IV_LEN_FLAG_MASK) << \
	QAT_LA_GCM_IV_LEN_FLAG_BITPOS) | \
	((auth_rslt & QAT_LA_DIGEST_IN_BUFFER_MASK) << \
	QAT_LA_DIGEST_IN_BUFFER_BITPOS) | \
	((proto & QAT_LA_PROTO_MASK) << \
	QAT_LA_PROTO_BITPOS)	| \
	((cmp_auth & QAT_LA_CMP_AUTH_RES_MASK) << \
	QAT_LA_CMP_AUTH_RES_BITPOS) | \
	((ret_auth & QAT_LA_RET_AUTH_RES_MASK) << \
	QAT_LA_RET_AUTH_RES_BITPOS) | \
	((update_state & QAT_LA_UPDATE_STATE_MASK) << \
	QAT_LA_UPDATE_STATE_BITPOS) | \
	((ciph_iv & QAT_LA_CIPH_IV_FLD_MASK) << \
	QAT_LA_CIPH_IV_FLD_BITPOS) | \
	((ciphcfg & QAT_LA_CIPH_AUTH_CFG_OFFSET_MASK) << \
	QAT_LA_CIPH_AUTH_CFG_OFFSET_BITPOS) | \
	((partial & QAT_LA_PARTIAL_MASK) << \
	QAT_LA_PARTIAL_BITPOS))

#define ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_CIPH_IV_FLD_BITPOS, \
	QAT_LA_CIPH_IV_FLD_MASK)

#define ICP_QAT_FW_LA_CIPH_AUTH_CFG_OFFSET_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_CIPH_AUTH_CFG_OFFSET_BITPOS, \
	QAT_LA_CIPH_AUTH_CFG_OFFSET_MASK)

#define ICP_QAT_FW_LA_ZUC_3G_PROTO_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, QAT_FW_LA_ZUC_3G_PROTO_FLAG_BITPOS, \
	QAT_FW_LA_ZUC_3G_PROTO_FLAG_MASK)

#define ICP_QAT_FW_LA_GCM_IV_LEN_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_GCM_IV_LEN_FLAG_BITPOS, \
	QAT_LA_GCM_IV_LEN_FLAG_MASK)

#define ICP_QAT_FW_LA_PROTO_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_PROTO_BITPOS, QAT_LA_PROTO_MASK)

#define ICP_QAT_FW_LA_CMP_AUTH_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_CMP_AUTH_RES_BITPOS, \
	QAT_LA_CMP_AUTH_RES_MASK)

#define ICP_QAT_FW_LA_RET_AUTH_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_RET_AUTH_RES_BITPOS, \
	QAT_LA_RET_AUTH_RES_MASK)

#define ICP_QAT_FW_LA_DIGEST_IN_BUFFER_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_DIGEST_IN_BUFFER_BITPOS, \
	QAT_LA_DIGEST_IN_BUFFER_MASK)

#define ICP_QAT_FW_LA_UPDATE_STATE_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_UPDATE_STATE_BITPOS, \
	QAT_LA_UPDATE_STATE_MASK)

#define ICP_QAT_FW_LA_PARTIAL_GET(flags) \
	QAT_FIELD_GET(flags, QAT_LA_PARTIAL_BITPOS, \
	QAT_LA_PARTIAL_MASK)

#define ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_CIPH_IV_FLD_BITPOS, \
	QAT_LA_CIPH_IV_FLD_MASK)

#define ICP_QAT_FW_LA_CIPH_AUTH_CFG_OFFSET_FLAG_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_CIPH_AUTH_CFG_OFFSET_BITPOS, \
	QAT_LA_CIPH_AUTH_CFG_OFFSET_MASK)

#define ICP_QAT_FW_LA_ZUC_3G_PROTO_FLAG_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_FW_LA_ZUC_3G_PROTO_FLAG_BITPOS, \
	QAT_FW_LA_ZUC_3G_PROTO_FLAG_MASK)

#define ICP_QAT_FW_LA_GCM_IV_LEN_FLAG_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_GCM_IV_LEN_FLAG_BITPOS, \
	QAT_LA_GCM_IV_LEN_FLAG_MASK)

#define ICP_QAT_FW_LA_PROTO_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_PROTO_BITPOS, \
	QAT_LA_PROTO_MASK)

#define ICP_QAT_FW_LA_CMP_AUTH_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_CMP_AUTH_RES_BITPOS, \
	QAT_LA_CMP_AUTH_RES_MASK)

#define ICP_QAT_FW_LA_RET_AUTH_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_RET_AUTH_RES_BITPOS, \
	QAT_LA_RET_AUTH_RES_MASK)

#define ICP_QAT_FW_LA_DIGEST_IN_BUFFER_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_DIGEST_IN_BUFFER_BITPOS, \
	QAT_LA_DIGEST_IN_BUFFER_MASK)

#define ICP_QAT_FW_LA_UPDATE_STATE_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_UPDATE_STATE_BITPOS, \
	QAT_LA_UPDATE_STATE_MASK)

#define ICP_QAT_FW_LA_PARTIAL_SET(flags, val) \
	QAT_FIELD_SET(flags, val, QAT_LA_PARTIAL_BITPOS, \
	QAT_LA_PARTIAL_MASK)

struct icp_qat_fw_cipher_req_hdr_cd_pars {
	union {
		struct {
			uint64_t content_desc_addr;
			uint16_t content_desc_resrvd1;
			uint8_t content_desc_params_sz;
			uint8_t content_desc_hdr_resrvd2;
			uint32_t content_desc_resrvd3;
		} s;
		struct {
			uint32_t cipher_key_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		} s1;
	} u;
};

struct icp_qat_fw_cipher_auth_req_hdr_cd_pars {
	union {
		struct {
			uint64_t content_desc_addr;
			uint16_t content_desc_resrvd1;
			uint8_t content_desc_params_sz;
			uint8_t content_desc_hdr_resrvd2;
			uint32_t content_desc_resrvd3;
		} s;
		struct {
			uint32_t cipher_key_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		} sl;
	} u;
};

struct icp_qat_fw_cipher_cd_ctrl_hdr {
	uint8_t cipher_state_sz;
	uint8_t cipher_key_sz;
	uint8_t cipher_cfg_offset;
	uint8_t next_curr_id;
	uint8_t cipher_padding_sz;
	uint8_t resrvd1;
	uint16_t resrvd2;
	uint32_t resrvd3[ICP_QAT_FW_NUM_LONGWORDS_3];
};

struct icp_qat_fw_auth_cd_ctrl_hdr {
	uint32_t resrvd1;
	uint8_t resrvd2;
	uint8_t hash_flags;
	uint8_t hash_cfg_offset;
	uint8_t next_curr_id;
	uint8_t resrvd3;
	uint8_t outer_prefix_sz;
	uint8_t final_sz;
	uint8_t inner_res_sz;
	uint8_t resrvd4;
	uint8_t inner_state1_sz;
	uint8_t inner_state2_offset;
	uint8_t inner_state2_sz;
	uint8_t outer_config_offset;
	uint8_t outer_state1_sz;
	uint8_t outer_res_sz;
	uint8_t outer_prefix_offset;
};

struct icp_qat_fw_cipher_auth_cd_ctrl_hdr {
	uint8_t cipher_state_sz;
	uint8_t cipher_key_sz;
	uint8_t cipher_cfg_offset;
	uint8_t next_curr_id_cipher;
	uint8_t cipher_padding_sz;
	uint8_t hash_flags;
	uint8_t hash_cfg_offset;
	uint8_t next_curr_id_auth;
	uint8_t resrvd1;
	uint8_t outer_prefix_sz;
	uint8_t final_sz;
	uint8_t inner_res_sz;
	uint8_t resrvd2;
	uint8_t inner_state1_sz;
	uint8_t inner_state2_offset;
	uint8_t inner_state2_sz;
	uint8_t outer_config_offset;
	uint8_t outer_state1_sz;
	uint8_t outer_res_sz;
	uint8_t outer_prefix_offset;
};

#define ICP_QAT_FW_AUTH_HDR_FLAG_DO_NESTED 1
#define ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED 0
#define ICP_QAT_FW_CCM_GCM_AAD_SZ_MAX	240
#define ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET \
	(sizeof(struct icp_qat_fw_la_cipher_req_params_t))
#define ICP_QAT_FW_CIPHER_REQUEST_PARAMETERS_OFFSET (0)

struct icp_qat_fw_la_cipher_req_params {
	uint32_t cipher_offset;
	uint32_t cipher_length;
	union {
		uint32_t cipher_IV_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			uint64_t cipher_IV_ptr;
			uint64_t resrvd1;
		} s;
	} u;
};

struct icp_qat_fw_la_auth_req_params {
	uint32_t auth_off;
	uint32_t auth_len;
	union {
		uint64_t auth_partial_st_prefix;
		uint64_t aad_adr;
	} u1;
	uint64_t auth_res_addr;
	union {
		uint8_t inner_prefix_sz;
		uint8_t aad_sz;
	} u2;
	uint8_t resrvd1;
	uint8_t hash_state_sz;
	uint8_t auth_res_sz;
} __packed;

struct icp_qat_fw_la_auth_req_params_resrvd_flds {
	uint32_t resrvd[ICP_QAT_FW_NUM_LONGWORDS_6];
	union {
		uint8_t inner_prefix_sz;
		uint8_t aad_sz;
	} u2;
	uint8_t resrvd1;
	uint16_t resrvd2;
};

struct icp_qat_fw_la_resp {
	struct icp_qat_fw_comn_resp_hdr comn_resp;
	uint64_t opaque_data;
	uint32_t resrvd[ICP_QAT_FW_NUM_LONGWORDS_4];
};

#define ICP_QAT_FW_CIPHER_NEXT_ID_GET(cd_ctrl_hdr_t) \
	((((cd_ctrl_hdr_t)->next_curr_id_cipher) & \
	  ICP_QAT_FW_COMN_NEXT_ID_MASK) >> (ICP_QAT_FW_COMN_NEXT_ID_BITPOS))

#define ICP_QAT_FW_CIPHER_NEXT_ID_SET(cd_ctrl_hdr_t, val) \
{ (cd_ctrl_hdr_t)->next_curr_id_cipher = \
	((((cd_ctrl_hdr_t)->next_curr_id_cipher) \
	& ICP_QAT_FW_COMN_CURR_ID_MASK) | \
	((val << ICP_QAT_FW_COMN_NEXT_ID_BITPOS) \
	& ICP_QAT_FW_COMN_NEXT_ID_MASK)) }

#define ICP_QAT_FW_CIPHER_CURR_ID_GET(cd_ctrl_hdr_t) \
	(((cd_ctrl_hdr_t)->next_curr_id_cipher) \
	& ICP_QAT_FW_COMN_CURR_ID_MASK)

#define ICP_QAT_FW_CIPHER_CURR_ID_SET(cd_ctrl_hdr_t, val) \
{ (cd_ctrl_hdr_t)->next_curr_id_cipher = \
	((((cd_ctrl_hdr_t)->next_curr_id_cipher) \
	& ICP_QAT_FW_COMN_NEXT_ID_MASK) | \
	((val) & ICP_QAT_FW_COMN_CURR_ID_MASK)) }

#define ICP_QAT_FW_AUTH_NEXT_ID_GET(cd_ctrl_hdr_t) \
	((((cd_ctrl_hdr_t)->next_curr_id_auth) & ICP_QAT_FW_COMN_NEXT_ID_MASK) \
	>> (ICP_QAT_FW_COMN_NEXT_ID_BITPOS))

#define ICP_QAT_FW_AUTH_NEXT_ID_SET(cd_ctrl_hdr_t, val) \
{ (cd_ctrl_hdr_t)->next_curr_id_auth = \
	((((cd_ctrl_hdr_t)->next_curr_id_auth) \
	& ICP_QAT_FW_COMN_CURR_ID_MASK) | \
	((val << ICP_QAT_FW_COMN_NEXT_ID_BITPOS) \
	& ICP_QAT_FW_COMN_NEXT_ID_MASK)) }

#define ICP_QAT_FW_AUTH_CURR_ID_GET(cd_ctrl_hdr_t) \
	(((cd_ctrl_hdr_t)->next_curr_id_auth) \
	& ICP_QAT_FW_COMN_CURR_ID_MASK)

#define ICP_QAT_FW_AUTH_CURR_ID_SET(cd_ctrl_hdr_t, val) \
{ (cd_ctrl_hdr_t)->next_curr_id_auth = \
	((((cd_ctrl_hdr_t)->next_curr_id_auth) \
	& ICP_QAT_FW_COMN_NEXT_ID_MASK) | \
	((val) & ICP_QAT_FW_COMN_CURR_ID_MASK)) }

#endif
