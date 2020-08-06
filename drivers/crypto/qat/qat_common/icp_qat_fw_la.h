/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
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
			__u64 content_desc_addr;
			__u16 content_desc_resrvd1;
			__u8 content_desc_params_sz;
			__u8 content_desc_hdr_resrvd2;
			__u32 content_desc_resrvd3;
		} s;
		struct {
			__u32 cipher_key_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		} s1;
	} u;
};

struct icp_qat_fw_cipher_auth_req_hdr_cd_pars {
	union {
		struct {
			__u64 content_desc_addr;
			__u16 content_desc_resrvd1;
			__u8 content_desc_params_sz;
			__u8 content_desc_hdr_resrvd2;
			__u32 content_desc_resrvd3;
		} s;
		struct {
			__u32 cipher_key_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		} sl;
	} u;
};

struct icp_qat_fw_cipher_cd_ctrl_hdr {
	__u8 cipher_state_sz;
	__u8 cipher_key_sz;
	__u8 cipher_cfg_offset;
	__u8 next_curr_id;
	__u8 cipher_padding_sz;
	__u8 resrvd1;
	__u16 resrvd2;
	__u32 resrvd3[ICP_QAT_FW_NUM_LONGWORDS_3];
};

struct icp_qat_fw_auth_cd_ctrl_hdr {
	__u32 resrvd1;
	__u8 resrvd2;
	__u8 hash_flags;
	__u8 hash_cfg_offset;
	__u8 next_curr_id;
	__u8 resrvd3;
	__u8 outer_prefix_sz;
	__u8 final_sz;
	__u8 inner_res_sz;
	__u8 resrvd4;
	__u8 inner_state1_sz;
	__u8 inner_state2_offset;
	__u8 inner_state2_sz;
	__u8 outer_config_offset;
	__u8 outer_state1_sz;
	__u8 outer_res_sz;
	__u8 outer_prefix_offset;
};

struct icp_qat_fw_cipher_auth_cd_ctrl_hdr {
	__u8 cipher_state_sz;
	__u8 cipher_key_sz;
	__u8 cipher_cfg_offset;
	__u8 next_curr_id_cipher;
	__u8 cipher_padding_sz;
	__u8 hash_flags;
	__u8 hash_cfg_offset;
	__u8 next_curr_id_auth;
	__u8 resrvd1;
	__u8 outer_prefix_sz;
	__u8 final_sz;
	__u8 inner_res_sz;
	__u8 resrvd2;
	__u8 inner_state1_sz;
	__u8 inner_state2_offset;
	__u8 inner_state2_sz;
	__u8 outer_config_offset;
	__u8 outer_state1_sz;
	__u8 outer_res_sz;
	__u8 outer_prefix_offset;
};

#define ICP_QAT_FW_AUTH_HDR_FLAG_DO_NESTED 1
#define ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED 0
#define ICP_QAT_FW_CCM_GCM_AAD_SZ_MAX	240
#define ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET \
	(sizeof(struct icp_qat_fw_la_cipher_req_params_t))
#define ICP_QAT_FW_CIPHER_REQUEST_PARAMETERS_OFFSET (0)

struct icp_qat_fw_la_cipher_req_params {
	__u32 cipher_offset;
	__u32 cipher_length;
	union {
		__u32 cipher_IV_array[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			__u64 cipher_IV_ptr;
			__u64 resrvd1;
		} s;
	} u;
};

struct icp_qat_fw_la_auth_req_params {
	__u32 auth_off;
	__u32 auth_len;
	union {
		__u64 auth_partial_st_prefix;
		__u64 aad_adr;
	} u1;
	__u64 auth_res_addr;
	union {
		__u8 inner_prefix_sz;
		__u8 aad_sz;
	} u2;
	__u8 resrvd1;
	__u8 hash_state_sz;
	__u8 auth_res_sz;
} __packed;

struct icp_qat_fw_la_auth_req_params_resrvd_flds {
	__u32 resrvd[ICP_QAT_FW_NUM_LONGWORDS_6];
	union {
		__u8 inner_prefix_sz;
		__u8 aad_sz;
	} u2;
	__u8 resrvd1;
	__u16 resrvd2;
};

struct icp_qat_fw_la_resp {
	struct icp_qat_fw_comn_resp_hdr comn_resp;
	__u64 opaque_data;
	__u32 resrvd[ICP_QAT_FW_NUM_LONGWORDS_4];
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
