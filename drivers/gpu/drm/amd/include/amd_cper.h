/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __AMD_CPER_H__
#define __AMD_CPER_H__

#include <linux/uuid.h>

#define CPER_HDR_REV_1          (0x100)
#define CPER_SEC_MINOR_REV_1    (0x01)
#define CPER_SEC_MAJOR_REV_22   (0x22)
#define CPER_MAX_OAM_COUNT      (8)

#define CPER_CTX_TYPE_CRASH     (1)
#define CPER_CTX_TYPE_BOOT      (9)

#define CPER_CREATOR_ID_AMDGPU	"amdgpu"

#define CPER_NOTIFY_MCE                                               \
	GUID_INIT(0xE8F56FFE, 0x919C, 0x4cc5, 0xBA, 0x88, 0x65, 0xAB, \
		  0xE1, 0x49, 0x13, 0xBB)
#define CPER_NOTIFY_CMC                                               \
	GUID_INIT(0x2DCE8BB1, 0xBDD7, 0x450e, 0xB9, 0xAD, 0x9C, 0xF4, \
		  0xEB, 0xD4, 0xF8, 0x90)
#define BOOT_TYPE                                                     \
	GUID_INIT(0x3D61A466, 0xAB40, 0x409a, 0xA6, 0x98, 0xF3, 0x62, \
		  0xD4, 0x64, 0xB3, 0x8F)

#define AMD_CRASHDUMP                                                 \
	GUID_INIT(0x32AC0C78, 0x2623, 0x48F6, 0xB0, 0xD0, 0x73, 0x65, \
		  0x72, 0x5F, 0xD6, 0xAE)
#define AMD_GPU_NONSTANDARD_ERROR                                     \
	GUID_INIT(0x32AC0C78, 0x2623, 0x48F6, 0x81, 0xA2, 0xAC, 0x69, \
		  0x17, 0x80, 0x55, 0x1D)
#define PROC_ERR_SECTION_TYPE                                         \
	GUID_INIT(0xDC3EA0B0, 0xA144, 0x4797, 0xB9, 0x5B, 0x53, 0xFA, \
		  0x24, 0x2B, 0x6E, 0x1D)

enum cper_error_severity {
	CPER_SEV_NON_FATAL_UNCORRECTED = 0,
	CPER_SEV_FATAL                 = 1,
	CPER_SEV_NON_FATAL_CORRECTED   = 2,
	CPER_SEV_NUM                   = 3,

	CPER_SEV_UNUSED = 10,
};

enum cper_aca_reg {
	CPER_ACA_REG_CTL_LO    = 0,
	CPER_ACA_REG_CTL_HI    = 1,
	CPER_ACA_REG_STATUS_LO = 2,
	CPER_ACA_REG_STATUS_HI = 3,
	CPER_ACA_REG_ADDR_LO   = 4,
	CPER_ACA_REG_ADDR_HI   = 5,
	CPER_ACA_REG_MISC0_LO  = 6,
	CPER_ACA_REG_MISC0_HI  = 7,
	CPER_ACA_REG_CONFIG_LO = 8,
	CPER_ACA_REG_CONFIG_HI = 9,
	CPER_ACA_REG_IPID_LO   = 10,
	CPER_ACA_REG_IPID_HI   = 11,
	CPER_ACA_REG_SYND_LO   = 12,
	CPER_ACA_REG_SYND_HI   = 13,

	CPER_ACA_REG_COUNT     = 32,
};

#pragma pack(push, 1)

struct cper_timestamp {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t flag;
	uint8_t day;
	uint8_t month;
	uint8_t year;
	uint8_t century;
};

struct cper_hdr {
	char                     signature[4];  /* "CPER"  */
	uint16_t                 revision;
	uint32_t                 signature_end; /* 0xFFFFFFFF */
	uint16_t                 sec_cnt;
	enum cper_error_severity error_severity;
	union {
		struct {
			uint32_t platform_id	: 1;
			uint32_t timestamp	: 1;
			uint32_t partition_id	: 1;
			uint32_t reserved	: 29;
		} valid_bits;
		uint32_t valid_mask;
	};
	uint32_t		record_length;    /* Total size of CPER Entry */
	struct cper_timestamp	timestamp;
	char			platform_id[16];
	guid_t			partition_id;     /* Reserved */
	char			creator_id[16];
	guid_t			notify_type;      /* CMC, MCE */
	char			record_id[8];     /* Unique CPER Entry ID */
	uint32_t		flags;            /* Reserved */
	uint64_t		persistence_info; /* Reserved */
	uint8_t			reserved[12];     /* Reserved */
};

struct cper_sec_desc {
	uint32_t sec_offset;     /* Offset from the start of CPER entry */
	uint32_t sec_length;
	uint8_t  revision_minor; /* CPER_SEC_MINOR_REV_1 */
	uint8_t  revision_major; /* CPER_SEC_MAJOR_REV_22 */
	union {
		struct {
			uint8_t fru_id		: 1;
			uint8_t fru_text	: 1;
			uint8_t reserved	: 6;
		} valid_bits;
		uint8_t valid_mask;
	};
	uint8_t reserved;
	union {
		struct {
			uint32_t primary		: 1;
			uint32_t reserved1		: 2;
			uint32_t exceed_err_threshold	: 1;
			uint32_t latent_err		: 1;
			uint32_t reserved2		: 27;
		} flag_bits;
		uint32_t flag_mask;
	};
	guid_t				sec_type;
	char				fru_id[16];
	enum cper_error_severity	severity;
	char				fru_text[20];
};

struct cper_sec_nonstd_err_hdr {
	union {
		struct {
			uint64_t apic_id		: 1;
			uint64_t fw_id			: 1;
			uint64_t err_info_cnt		: 6;
			uint64_t err_context_cnt	: 6;
		} valid_bits;
		uint64_t valid_mask;
	};
	uint64_t apic_id;
	char     fw_id[48];
};

struct cper_sec_nonstd_err_info {
	guid_t error_type;
	union {
		struct {
			uint64_t ms_chk			: 1;
			uint64_t target_addr_id		: 1;
			uint64_t req_id			: 1;
			uint64_t resp_id		: 1;
			uint64_t instr_ptr		: 1;
			uint64_t reserved		: 59;
		} valid_bits;
		uint64_t        valid_mask;
	};
	union {
		struct {
			uint64_t err_type_valid		: 1;
			uint64_t pcc_valid		: 1;
			uint64_t uncorr_valid		: 1;
			uint64_t precise_ip_valid	: 1;
			uint64_t restartable_ip_valid	: 1;
			uint64_t overflow_valid		: 1;
			uint64_t reserved1		: 10;
			uint64_t err_type		: 2;
			uint64_t pcc			: 1;
			uint64_t uncorr			: 1;
			uint64_t precised_ip		: 1;
			uint64_t restartable_ip		: 1;
			uint64_t overflow		: 1;
			uint64_t reserved2		: 41;
		} ms_chk_bits;
		uint64_t ms_chk_mask;
	};
	uint64_t target_addr_id;
	uint64_t req_id;
	uint64_t resp_id;
	uint64_t instr_ptr;
};

struct cper_sec_nonstd_err_ctx {
	uint16_t reg_ctx_type;
	uint16_t reg_arr_size;
	uint32_t msr_addr;
	uint64_t mm_reg_addr;
	uint32_t reg_dump[CPER_ACA_REG_COUNT];
};

struct cper_sec_nonstd_err {
	struct cper_sec_nonstd_err_hdr  hdr;
	struct cper_sec_nonstd_err_info info;
	struct cper_sec_nonstd_err_ctx  ctx;
};

struct cper_sec_crashdump_hdr {
	uint64_t reserved1;
	uint64_t reserved2;
	char     fw_id[48];
	uint64_t reserved3[8];
};

struct cper_sec_crashdump_reg_data {
	uint32_t status_lo;
	uint32_t status_hi;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t ipid_lo;
	uint32_t ipid_hi;
	uint32_t synd_lo;
	uint32_t synd_hi;
};

struct cper_sec_crashdump_body_fatal {
	uint16_t                           reg_ctx_type;
	uint16_t                           reg_arr_size;
	uint32_t                           reserved1;
	uint64_t                           reserved2;
	struct cper_sec_crashdump_reg_data data;
};

struct cper_sec_crashdump_body_boot {
	uint16_t reg_ctx_type;
	uint16_t reg_arr_size;
	uint32_t reserved1;
	uint64_t reserved2;
	uint64_t msg[CPER_MAX_OAM_COUNT];
};

struct cper_sec_crashdump_fatal {
	struct cper_sec_crashdump_hdr        hdr;
	struct cper_sec_crashdump_body_fatal body;
};

struct cper_sec_crashdump_boot {
	struct cper_sec_crashdump_hdr       hdr;
	struct cper_sec_crashdump_body_boot body;
};

#pragma pack(pop)

#endif
