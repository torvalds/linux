/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation */
#ifndef _ICP_QAT_FW_COMP_H_
#define _ICP_QAT_FW_COMP_H_
#include "icp_qat_fw.h"

enum icp_qat_fw_comp_cmd_id {
	ICP_QAT_FW_COMP_CMD_STATIC = 0,
	ICP_QAT_FW_COMP_CMD_DYNAMIC = 1,
	ICP_QAT_FW_COMP_CMD_DECOMPRESS = 2,
	ICP_QAT_FW_COMP_CMD_DELIMITER
};

enum icp_qat_fw_comp_20_cmd_id {
	ICP_QAT_FW_COMP_20_CMD_LZ4_COMPRESS = 3,
	ICP_QAT_FW_COMP_20_CMD_LZ4_DECOMPRESS = 4,
	ICP_QAT_FW_COMP_20_CMD_LZ4S_COMPRESS = 5,
	ICP_QAT_FW_COMP_20_CMD_LZ4S_DECOMPRESS = 6,
	ICP_QAT_FW_COMP_20_CMD_RESERVED_7 = 7,
	ICP_QAT_FW_COMP_20_CMD_RESERVED_8 = 8,
	ICP_QAT_FW_COMP_20_CMD_RESERVED_9 = 9,
	ICP_QAT_FW_COMP_23_CMD_ZSTD_COMPRESS = 10,
	ICP_QAT_FW_COMP_23_CMD_ZSTD_DECOMPRESS = 11,
	ICP_QAT_FW_COMP_20_CMD_DELIMITER
};

#define ICP_QAT_FW_COMP_STATELESS_SESSION 0
#define ICP_QAT_FW_COMP_STATEFUL_SESSION 1
#define ICP_QAT_FW_COMP_NOT_AUTO_SELECT_BEST 0
#define ICP_QAT_FW_COMP_AUTO_SELECT_BEST 1
#define ICP_QAT_FW_COMP_NOT_ENH_AUTO_SELECT_BEST 0
#define ICP_QAT_FW_COMP_ENH_AUTO_SELECT_BEST 1
#define ICP_QAT_FW_COMP_NOT_DISABLE_TYPE0_ENH_AUTO_SELECT_BEST 0
#define ICP_QAT_FW_COMP_DISABLE_TYPE0_ENH_AUTO_SELECT_BEST 1
#define ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_USED_AS_INTMD_BUF 1
#define ICP_QAT_FW_COMP_ENABLE_SECURE_RAM_USED_AS_INTMD_BUF 0
#define ICP_QAT_FW_COMP_SESSION_TYPE_BITPOS 2
#define ICP_QAT_FW_COMP_SESSION_TYPE_MASK 0x1
#define ICP_QAT_FW_COMP_AUTO_SELECT_BEST_BITPOS 3
#define ICP_QAT_FW_COMP_AUTO_SELECT_BEST_MASK 0x1
#define ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_BITPOS 4
#define ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_MASK 0x1
#define ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_BITPOS 5
#define ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_MASK 0x1
#define ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_BITPOS 7
#define ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_MASK 0x1
#define ICP_QAT_FW_COMP_AUTO_SELECT_BEST_MAX_VALUE 0xFFFFFFFF

#define ICP_QAT_FW_COMP_FLAGS_BUILD(sesstype, autoselect, enhanced_asb, \
	ret_uncomp, secure_ram) \
	((((sesstype) & ICP_QAT_FW_COMP_SESSION_TYPE_MASK) << \
	ICP_QAT_FW_COMP_SESSION_TYPE_BITPOS) | \
	(((autoselect) & ICP_QAT_FW_COMP_AUTO_SELECT_BEST_MASK) << \
	ICP_QAT_FW_COMP_AUTO_SELECT_BEST_BITPOS) | \
	(((enhanced_asb) & ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_MASK) << \
	ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_BITPOS) | \
	(((ret_uncomp) & ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_MASK) << \
	ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_BITPOS) | \
	(((secure_ram) & ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_MASK) << \
	ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_BITPOS))

#define ICP_QAT_FW_COMP_SESSION_TYPE_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_SESSION_TYPE_BITPOS, \
	ICP_QAT_FW_COMP_SESSION_TYPE_MASK)

#define ICP_QAT_FW_COMP_SESSION_TYPE_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_SESSION_TYPE_BITPOS, \
	ICP_QAT_FW_COMP_SESSION_TYPE_MASK)

#define ICP_QAT_FW_COMP_AUTO_SELECT_BEST_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_AUTO_SELECT_BEST_BITPOS, \
	ICP_QAT_FW_COMP_AUTO_SELECT_BEST_MASK)

#define ICP_QAT_FW_COMP_EN_ASB_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_BITPOS, \
	ICP_QAT_FW_COMP_ENHANCED_AUTO_SELECT_BEST_MASK)

#define ICP_QAT_FW_COMP_RET_UNCOMP_GET(flags) \
	QAT_FIELD_GET(flags, \
	ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_BITPOS, \
	ICP_QAT_FW_COMP_RET_DISABLE_TYPE0_HEADER_DATA_MASK)

#define ICP_QAT_FW_COMP_SECURE_RAM_USE_GET(flags) \
	QAT_FIELD_GET(flags, \
	ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_BITPOS, \
	ICP_QAT_FW_COMP_DISABLE_SECURE_RAM_AS_INTMD_BUF_MASK)

struct icp_qat_fw_comp_req_hdr_cd_pars {
	union {
		struct {
			__u64 content_desc_addr;
			__u16 content_desc_resrvd1;
			__u8 content_desc_params_sz;
			__u8 content_desc_hdr_resrvd2;
			__u32 content_desc_resrvd3;
		} s;
		struct {
			__u32 comp_slice_cfg_word[ICP_QAT_FW_NUM_LONGWORDS_2];
			__u32 content_desc_resrvd4;
		} sl;
	} u;
};

struct icp_qat_fw_comp_req_params {
	__u32 comp_len;
	__u32 out_buffer_sz;
	union {
		struct {
			__u32 initial_crc32;
			__u32 initial_adler;
		} legacy;
		__u64 crc_data_addr;
	} crc;
	__u32 req_par_flags;
	__u32 rsrvd;
};

#define ICP_QAT_FW_COMP_REQ_PARAM_FLAGS_BUILD(sop, eop, bfinal, cnv, cnvnr, \
					      cnvdfx, crc, xxhash_acc, \
					      cnv_error_type, append_crc, \
					      drop_data, partial_decomp) \
	((((sop) & ICP_QAT_FW_COMP_SOP_MASK) << \
	ICP_QAT_FW_COMP_SOP_BITPOS) | \
	(((eop) & ICP_QAT_FW_COMP_EOP_MASK) << \
	ICP_QAT_FW_COMP_EOP_BITPOS) | \
	(((bfinal) & ICP_QAT_FW_COMP_BFINAL_MASK) \
	<< ICP_QAT_FW_COMP_BFINAL_BITPOS) | \
	(((cnv) & ICP_QAT_FW_COMP_CNV_MASK) << \
	ICP_QAT_FW_COMP_CNV_BITPOS) | \
	(((cnvnr) & ICP_QAT_FW_COMP_CNVNR_MASK) \
	<< ICP_QAT_FW_COMP_CNVNR_BITPOS) | \
	(((cnvdfx) & ICP_QAT_FW_COMP_CNV_DFX_MASK) \
	<< ICP_QAT_FW_COMP_CNV_DFX_BITPOS) | \
	(((crc) & ICP_QAT_FW_COMP_CRC_MODE_MASK) \
	<< ICP_QAT_FW_COMP_CRC_MODE_BITPOS) | \
	(((xxhash_acc) & ICP_QAT_FW_COMP_XXHASH_ACC_MODE_MASK) \
	<< ICP_QAT_FW_COMP_XXHASH_ACC_MODE_BITPOS) | \
	(((cnv_error_type) & ICP_QAT_FW_COMP_CNV_ERROR_MASK) \
	<< ICP_QAT_FW_COMP_CNV_ERROR_BITPOS) | \
	(((append_crc) & ICP_QAT_FW_COMP_APPEND_CRC_MASK) \
	<< ICP_QAT_FW_COMP_APPEND_CRC_BITPOS) | \
	(((drop_data) & ICP_QAT_FW_COMP_DROP_DATA_MASK) \
	<< ICP_QAT_FW_COMP_DROP_DATA_BITPOS) | \
	(((partial_decomp) & ICP_QAT_FW_COMP_PARTIAL_DECOMP_MASK) \
	<< ICP_QAT_FW_COMP_PARTIAL_DECOMP_BITPOS))

#define ICP_QAT_FW_COMP_NOT_SOP 0
#define ICP_QAT_FW_COMP_SOP 1
#define ICP_QAT_FW_COMP_NOT_EOP 0
#define ICP_QAT_FW_COMP_EOP 1
#define ICP_QAT_FW_COMP_NOT_BFINAL 0
#define ICP_QAT_FW_COMP_BFINAL 1
#define ICP_QAT_FW_COMP_NO_CNV 0
#define ICP_QAT_FW_COMP_CNV 1
#define ICP_QAT_FW_COMP_NO_CNV_RECOVERY 0
#define ICP_QAT_FW_COMP_CNV_RECOVERY 1
#define ICP_QAT_FW_COMP_NO_CNV_DFX 0
#define ICP_QAT_FW_COMP_CNV_DFX 1
#define ICP_QAT_FW_COMP_CRC_MODE_LEGACY 0
#define ICP_QAT_FW_COMP_CRC_MODE_E2E 1
#define ICP_QAT_FW_COMP_NO_XXHASH_ACC 0
#define ICP_QAT_FW_COMP_XXHASH_ACC 1
#define ICP_QAT_FW_COMP_APPEND_CRC 1
#define ICP_QAT_FW_COMP_NO_APPEND_CRC 0
#define ICP_QAT_FW_COMP_DROP_DATA 1
#define ICP_QAT_FW_COMP_NO_DROP_DATA 0
#define ICP_QAT_FW_COMP_PARTIAL_DECOMPRESS 1
#define ICP_QAT_FW_COMP_NO_PARTIAL_DECOMPRESS 0
#define ICP_QAT_FW_COMP_SOP_BITPOS 0
#define ICP_QAT_FW_COMP_SOP_MASK 0x1
#define ICP_QAT_FW_COMP_EOP_BITPOS 1
#define ICP_QAT_FW_COMP_EOP_MASK 0x1
#define ICP_QAT_FW_COMP_BFINAL_BITPOS 6
#define ICP_QAT_FW_COMP_BFINAL_MASK 0x1
#define ICP_QAT_FW_COMP_CNV_BITPOS 16
#define ICP_QAT_FW_COMP_CNV_MASK 0x1
#define ICP_QAT_FW_COMP_CNVNR_BITPOS 17
#define ICP_QAT_FW_COMP_CNVNR_MASK 0x1
#define ICP_QAT_FW_COMP_CNV_DFX_BITPOS 18
#define ICP_QAT_FW_COMP_CNV_DFX_MASK 0x1
#define ICP_QAT_FW_COMP_CRC_MODE_BITPOS 19
#define ICP_QAT_FW_COMP_CRC_MODE_MASK 0x1
#define ICP_QAT_FW_COMP_XXHASH_ACC_MODE_BITPOS 20
#define ICP_QAT_FW_COMP_XXHASH_ACC_MODE_MASK 0x1
#define ICP_QAT_FW_COMP_CNV_ERROR_BITPOS 21
#define ICP_QAT_FW_COMP_CNV_ERROR_MASK 0b111
#define ICP_QAT_FW_COMP_CNV_ERROR_NONE 0b000
#define ICP_QAT_FW_COMP_CNV_ERROR_CHECKSUM 0b001
#define ICP_QAT_FW_COMP_CNV_ERROR_DCPR_OBC_DIFF 0b010
#define ICP_QAT_FW_COMP_CNV_ERROR_DCPR 0b011
#define ICP_QAT_FW_COMP_CNV_ERROR_XLT 0b100
#define ICP_QAT_FW_COMP_CNV_ERROR_DCPR_IBC_DIFF 0b101
#define ICP_QAT_FW_COMP_APPEND_CRC_BITPOS 24
#define ICP_QAT_FW_COMP_APPEND_CRC_MASK 0x1
#define ICP_QAT_FW_COMP_DROP_DATA_BITPOS 25
#define ICP_QAT_FW_COMP_DROP_DATA_MASK 0x1
#define ICP_QAT_FW_COMP_PARTIAL_DECOMP_BITPOS 27
#define ICP_QAT_FW_COMP_PARTIAL_DECOMP_MASK 0x1

#define ICP_QAT_FW_COMP_SOP_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_SOP_BITPOS, \
	ICP_QAT_FW_COMP_SOP_MASK)

#define ICP_QAT_FW_COMP_SOP_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_SOP_BITPOS, \
	ICP_QAT_FW_COMP_SOP_MASK)

#define ICP_QAT_FW_COMP_EOP_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_EOP_BITPOS, \
	ICP_QAT_FW_COMP_EOP_MASK)

#define ICP_QAT_FW_COMP_EOP_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_EOP_BITPOS, \
	ICP_QAT_FW_COMP_EOP_MASK)

#define ICP_QAT_FW_COMP_BFINAL_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_BFINAL_BITPOS, \
	ICP_QAT_FW_COMP_BFINAL_MASK)

#define ICP_QAT_FW_COMP_BFINAL_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_BFINAL_BITPOS, \
	ICP_QAT_FW_COMP_BFINAL_MASK)

#define ICP_QAT_FW_COMP_CNV_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_CNV_BITPOS, \
	ICP_QAT_FW_COMP_CNV_MASK)

#define ICP_QAT_FW_COMP_CNVNR_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_CNVNR_BITPOS, \
	ICP_QAT_FW_COMP_CNVNR_MASK)

#define ICP_QAT_FW_COMP_CNV_DFX_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_CNV_DFX_BITPOS, \
	ICP_QAT_FW_COMP_CNV_DFX_MASK)

#define ICP_QAT_FW_COMP_CNV_DFX_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_CNV_DFX_BITPOS, \
	ICP_QAT_FW_COMP_CNV_DFX_MASK)

#define ICP_QAT_FW_COMP_CRC_MODE_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_CRC_MODE_BITPOS, \
	ICP_QAT_FW_COMP_CRC_MODE_MASK)

#define ICP_QAT_FW_COMP_XXHASH_ACC_MODE_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_XXHASH_ACC_MODE_BITPOS, \
	ICP_QAT_FW_COMP_XXHASH_ACC_MODE_MASK)

#define ICP_QAT_FW_COMP_XXHASH_ACC_MODE_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_XXHASH_ACC_MODE_BITPOS, \
	ICP_QAT_FW_COMP_XXHASH_ACC_MODE_MASK)

#define ICP_QAT_FW_COMP_CNV_ERROR_TYPE_GET(flags) \
	QAT_FIELD_GET(flags, ICP_QAT_FW_COMP_CNV_ERROR_BITPOS, \
	ICP_QAT_FW_COMP_CNV_ERROR_MASK)

#define ICP_QAT_FW_COMP_CNV_ERROR_TYPE_SET(flags, val) \
	QAT_FIELD_SET(flags, val, ICP_QAT_FW_COMP_CNV_ERROR_BITPOS, \
	ICP_QAT_FW_COMP_CNV_ERROR_MASK)

struct icp_qat_fw_xlt_req_params {
	__u64 inter_buff_ptr;
};

struct icp_qat_fw_comp_cd_hdr {
	__u16 ram_bank_flags;
	__u8 comp_cfg_offset;
	__u8 next_curr_id;
	__u32 resrvd;
	__u64 comp_state_addr;
	__u64 ram_banks_addr;
};

#define COMP_CPR_INITIAL_CRC 0
#define COMP_CPR_INITIAL_ADLER 1

struct icp_qat_fw_xlt_cd_hdr {
	__u16 resrvd1;
	__u8 resrvd2;
	__u8 next_curr_id;
	__u32 resrvd3;
};

struct icp_qat_fw_comp_req {
	struct icp_qat_fw_comn_req_hdr comn_hdr;
	struct icp_qat_fw_comp_req_hdr_cd_pars cd_pars;
	struct icp_qat_fw_comn_req_mid comn_mid;
	struct icp_qat_fw_comp_req_params comp_pars;
	union {
		struct icp_qat_fw_xlt_req_params xlt_pars;
		__u32 resrvd1[ICP_QAT_FW_NUM_LONGWORDS_2];
		struct {
			__u32 partial_decompress_length;
			__u32 partial_decompress_offset;
		} partial_decompress;
	} u1;
	union {
		__u32 resrvd2[ICP_QAT_FW_NUM_LONGWORDS_2];
		struct {
			__u32 asb_value;
			__u32 reserved;
		} asb_threshold;
	} u3;
	struct icp_qat_fw_comp_cd_hdr comp_cd_ctrl;
	union {
		struct icp_qat_fw_xlt_cd_hdr xlt_cd_ctrl;
		__u32 resrvd3[ICP_QAT_FW_NUM_LONGWORDS_2];
	} u2;
};

struct icp_qat_fw_resp_comp_pars {
	__u32 input_byte_counter;
	__u32 output_byte_counter;
	union {
		struct {
			__u32 curr_crc32;
			__u32 curr_adler_32;
		} legacy;
		__u32 resrvd[ICP_QAT_FW_NUM_LONGWORDS_2];
	} crc;
};

struct icp_qat_fw_comp_state {
	__u32 rd8_counter;
	__u32 status_flags;
	__u32 in_counter;
	__u32 out_counter;
	__u64 intermediate_state;
	__u32 lobc;
	__u32 replaybc;
	__u64 pcrc64_poly;
	__u32 crc32;
	__u32 adler_xxhash32;
	__u64 pcrc64_xorout;
	__u32 out_buf_size;
	__u32 in_buf_size;
	__u64 in_pcrc64;
	__u64 out_pcrc64;
	__u32 lobs;
	__u32 libc;
	__u64 reserved;
	__u32 xxhash_state[4];
	__u32 cleartext[4];
};

struct icp_qat_fw_comp_resp {
	struct icp_qat_fw_comn_resp_hdr comn_resp;
	__u64 opaque_data;
	struct icp_qat_fw_resp_comp_pars comp_resp_pars;
};

#define QAT_FW_COMP_BANK_FLAG_MASK 0x1
#define QAT_FW_COMP_BANK_I_BITPOS 8
#define QAT_FW_COMP_BANK_H_BITPOS 7
#define QAT_FW_COMP_BANK_G_BITPOS 6
#define QAT_FW_COMP_BANK_F_BITPOS 5
#define QAT_FW_COMP_BANK_E_BITPOS 4
#define QAT_FW_COMP_BANK_D_BITPOS 3
#define QAT_FW_COMP_BANK_C_BITPOS 2
#define QAT_FW_COMP_BANK_B_BITPOS 1
#define QAT_FW_COMP_BANK_A_BITPOS 0

enum icp_qat_fw_comp_bank_enabled {
	ICP_QAT_FW_COMP_BANK_DISABLED = 0,
	ICP_QAT_FW_COMP_BANK_ENABLED = 1,
	ICP_QAT_FW_COMP_BANK_DELIMITER = 2
};

#define ICP_QAT_FW_COMP_RAM_FLAGS_BUILD(bank_i_enable, bank_h_enable, \
					bank_g_enable, bank_f_enable, \
					bank_e_enable, bank_d_enable, \
					bank_c_enable, bank_b_enable, \
					bank_a_enable) \
	((((bank_i_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_I_BITPOS) | \
	(((bank_h_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_H_BITPOS) | \
	(((bank_g_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_G_BITPOS) | \
	(((bank_f_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_F_BITPOS) | \
	(((bank_e_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_E_BITPOS) | \
	(((bank_d_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_D_BITPOS) | \
	(((bank_c_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_C_BITPOS) | \
	(((bank_b_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_B_BITPOS) | \
	(((bank_a_enable) & QAT_FW_COMP_BANK_FLAG_MASK) << \
	QAT_FW_COMP_BANK_A_BITPOS))

struct icp_qat_fw_comp_crc_data_struct {
	__u32 crc32;
	union {
		__u32 adler;
		__u32 xxhash;
	} adler_xxhash_u;
	__u32 cpr_in_crc_lo;
	__u32 cpr_in_crc_hi;
	__u32 cpr_out_crc_lo;
	__u32 cpr_out_crc_hi;
	__u32 xlt_in_crc_lo;
	__u32 xlt_in_crc_hi;
	__u32 xlt_out_crc_lo;
	__u32 xlt_out_crc_hi;
	__u32 prog_crc_poly_lo;
	__u32 prog_crc_poly_hi;
	__u32 xor_out_lo;
	__u32 xor_out_hi;
	__u32 append_crc_lo;
	__u32 append_crc_hi;
};

struct xxhash_acc_state_buff {
	__u32 in_counter;
	__u32 out_counter;
	__u32 xxhash_state[4];
	__u32 clear_txt[4];
};

#endif
