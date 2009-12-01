/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __FC_SP_H__
#define __FC_SP_H__

#include <protocol/types.h>

#pragma pack(1)

enum auth_els_flags{
	FC_AUTH_ELS_MORE_FRAGS_FLAG 	= 0x80,	/*! bit-7. More Fragments
						 * Follow
						 */
	FC_AUTH_ELS_CONCAT_FLAG 	= 0x40,	/*! bit-6. Concatenation Flag */
	FC_AUTH_ELS_SEQ_NUM_FLAG 	= 0x01 	/*! bit-0. Sequence Number */
};

enum auth_msg_codes{
	FC_AUTH_MC_AUTH_RJT		= 0x0A,	/*! Auth Reject */
	FC_AUTH_MC_AUTH_NEG 		= 0x0B, /*! Auth Negotiate */
	FC_AUTH_MC_AUTH_DONE 		= 0x0C, /*! Auth Done */

	FC_AUTH_MC_DHCHAP_CHAL 		= 0x10, /*! DHCHAP Challenge */
	FC_AUTH_MC_DHCHAP_REPLY 	= 0x11, /*! DHCHAP Reply */
	FC_AUTH_MC_DHCHAP_SUCC 		= 0x12, /*! DHCHAP Success */

	FC_AUTH_MC_FCAP_REQ 		= 0x13, /*! FCAP Request */
	FC_AUTH_MC_FCAP_ACK 		= 0x14, /*! FCAP Acknowledge */
	FC_AUTH_MC_FCAP_CONF 		= 0x15, /*! FCAP Confirm */

	FC_AUTH_MC_FCPAP_INIT 		= 0x16, /*! FCPAP Init */
	FC_AUTH_MC_FCPAP_ACC 		= 0x17, /*! FCPAP Accept */
	FC_AUTH_MC_FCPAP_COMP 		= 0x18, /*! FCPAP Complete */

	FC_AUTH_MC_IKE_SA_INIT 		= 0x22, /*! IKE SA INIT */
	FC_AUTH_MC_IKE_SA_AUTH 		= 0x23, /*! IKE SA Auth */
	FC_AUTH_MC_IKE_CREATE_CHILD_SA	= 0x24, /*! IKE Create Child SA */
	FC_AUTH_MC_IKE_INFO 		= 0x25, /*! IKE informational */
};

enum auth_proto_version{
	FC_AUTH_PROTO_VER_1 	= 1,	/*! Protocol Version 1 */
};

enum {
	FC_AUTH_ELS_COMMAND_CODE = 0x90,/*! Authentication ELS Command code  */
	FC_AUTH_PROTO_PARAM_LEN_SZ = 4,	/*! Size of Proto Parameter Len Field */
	FC_AUTH_PROTO_PARAM_VAL_SZ = 4,	/*! Size of Proto Parameter Val Field */
	FC_MAX_AUTH_SECRET_LEN     = 256,
					/*! Maximum secret string length */
	FC_AUTH_NUM_USABLE_PROTO_LEN_SZ = 4,
					/*! Size of usable protocols field */
	FC_AUTH_RESP_VALUE_LEN_SZ	= 4,
					/*! Size of response value length */
	FC_MAX_CHAP_KEY_LEN	= 256,	/*! Maximum md5 digest length */
	FC_MAX_AUTH_RETRIES     = 3,	/*! Maximum number of retries */
	FC_MD5_DIGEST_LEN       = 16,	/*! MD5 digest length */
	FC_SHA1_DIGEST_LEN      = 20,	/*! SHA1 digest length */
	FC_MAX_DHG_SUPPORTED    = 1,	/*! Maximum DH Groups supported */
	FC_MAX_ALG_SUPPORTED    = 1,	/*! Maximum algorithms supported */
	FC_MAX_PROTO_SUPPORTED  = 1,	/*! Maximum protocols supported */
	FC_START_TXN_ID         = 2,	/*! Starting transaction ID */
};

enum auth_proto_id{
	FC_AUTH_PROTO_DHCHAP		= 0x00000001,
	FC_AUTH_PROTO_FCAP 		= 0x00000002,
	FC_AUTH_PROTO_FCPAP 		= 0x00000003,
	FC_AUTH_PROTO_IKEv2 		= 0x00000004,
	FC_AUTH_PROTO_IKEv2_AUTH 	= 0x00000005,
};

struct auth_name_s{
	u16	name_tag;	/*! Name Tag = 1 for Authentication */
	u16	name_len;	/*! Name Length = 8 for Authentication
					 */
	wwn_t		name;  		/*! Name. TODO - is this PWWN */
};


enum auth_hash_func{
	FC_AUTH_HASH_FUNC_MD5 		= 0x00000005,
	FC_AUTH_HASH_FUNC_SHA_1 	= 0x00000006,
};

enum auth_dh_gid{
	FC_AUTH_DH_GID_0_DHG_NULL	= 0x00000000,
	FC_AUTH_DH_GID_1_DHG_1024	= 0x00000001,
	FC_AUTH_DH_GID_2_DHG_1280	= 0x00000002,
	FC_AUTH_DH_GID_3_DHG_1536	= 0x00000003,
	FC_AUTH_DH_GID_4_DHG_2048	= 0x00000004,
	FC_AUTH_DH_GID_6_DHG_3072	= 0x00000006,
	FC_AUTH_DH_GID_7_DHG_4096	= 0x00000007,
	FC_AUTH_DH_GID_8_DHG_6144	= 0x00000008,
	FC_AUTH_DH_GID_9_DHG_8192	= 0x00000009,
};

struct auth_els_msg_s {
	u8		auth_els_code;	/*  Authentication ELS Code (0x90) */
	u8 	auth_els_flag; 	/*  Authentication ELS Flags */
	u8 	auth_msg_code; 	/*  Authentication Message Code */
	u8 	proto_version; 	/*  Protocol Version */
	u32	msg_len; 	/*  Message Length */
	u32	trans_id; 	/*  Transaction Identifier (T_ID) */

	/* Msg payload follows... */
};


enum auth_neg_param_tags {
	FC_AUTH_NEG_DHCHAP_HASHLIST 	= 0x0001,
	FC_AUTH_NEG_DHCHAP_DHG_ID_LIST 	= 0x0002,
};


struct dhchap_param_format_s {
	u16	tag;		/*! Parameter Tag. See
					 * auth_neg_param_tags_t
					 */
	u16	word_cnt;

	/* followed by variable length parameter value... */
};

struct auth_proto_params_s {
	u32	proto_param_len;
	u32	proto_id;

	/*
	 * Followed by variable length Protocol specific parameters. DH-CHAP
	 * uses dhchap_param_format_t
	 */
};

struct auth_neg_msg_s {
	struct auth_name_s	auth_ini_name;
	u32		usable_auth_protos;
	struct auth_proto_params_s proto_params[1]; /*! (1..usable_auth_proto)
						     * protocol params
						     */
};

struct auth_dh_val_s {
	u32 dh_val_len;
	u32 dh_val[1];
};

struct auth_dhchap_chal_msg_s {
	struct auth_els_msg_s	hdr;
	struct auth_name_s auth_responder_name;	/* TODO VRK - is auth_name_t
						 * type OK?
						 */
	u32 	hash_id;
	u32 	dh_grp_id;
	u32 	chal_val_len;
	char		chal_val[1];

	/* ...followed by variable Challenge length/value and DH length/value */
};


enum auth_rjt_codes {
	FC_AUTH_RJT_CODE_AUTH_FAILURE 	= 0x01,
	FC_AUTH_RJT_CODE_LOGICAL_ERR	= 0x02,
};

enum auth_rjt_code_exps {
	FC_AUTH_CEXP_AUTH_MECH_NOT_USABLE	= 0x01,
	FC_AUTH_CEXP_DH_GROUP_NOT_USABLE 	= 0x02,
	FC_AUTH_CEXP_HASH_FUNC_NOT_USABLE 	= 0x03,
	FC_AUTH_CEXP_AUTH_XACT_STARTED		= 0x04,
	FC_AUTH_CEXP_AUTH_FAILED 		= 0x05,
	FC_AUTH_CEXP_INCORRECT_PLD 		= 0x06,
	FC_AUTH_CEXP_INCORRECT_PROTO_MSG 	= 0x07,
	FC_AUTH_CEXP_RESTART_AUTH_PROTO 	= 0x08,
	FC_AUTH_CEXP_AUTH_CONCAT_NOT_SUPP 	= 0x09,
	FC_AUTH_CEXP_PROTO_VER_NOT_SUPP 	= 0x0A,
};

enum auth_status {
	FC_AUTH_STATE_INPROGRESS = 0, 	/*! authentication in progress 	*/
	FC_AUTH_STATE_FAILED	= 1, 	/*! authentication failed */
	FC_AUTH_STATE_SUCCESS	= 2 	/*! authentication successful	*/
};

struct auth_rjt_msg_s {
	struct auth_els_msg_s	hdr;
	u8		reason_code;
	u8		reason_code_exp;
	u8		rsvd[2];
};


struct auth_dhchap_neg_msg_s {
	struct auth_els_msg_s hdr;
	struct auth_neg_msg_s nego;
};

struct auth_dhchap_reply_msg_s {
	struct auth_els_msg_s	hdr;

	/*
	 * followed by response value length & Value + DH Value Length & Value
	 */
};

#pragma pack()

#endif /* __FC_SP_H__ */
