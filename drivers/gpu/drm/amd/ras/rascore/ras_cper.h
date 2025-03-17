/* SPDX-License-Identifier: MIT */
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
#ifndef __RAS_CPER_H__
#define __RAS_CPER_H__

#define CPER_UUID_MAX_SIZE 16
struct ras_cper_guid {
	uint8_t b[CPER_UUID_MAX_SIZE];
};

#define CPER_GUID__INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)			\
	((struct ras_cper_guid)								\
	{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
		(b) & 0xff, ((b) >> 8) & 0xff,					\
		(c) & 0xff, ((c) >> 8) & 0xff,					\
		(d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})

#define CPER_HDR__REV_1          (0x100)
#define CPER_SEC__MINOR_REV_1    (0x01)
#define CPER_SEC__MAJOR_REV_22   (0x22)
#define CPER_OAM_MAX_COUNT      (8)

#define CPER_CTX_TYPE__CRASH     (1)
#define CPER_CTX_TYPE__BOOT      (9)

#define CPER_CREATOR_ID__AMDGPU	"amdgpu"

#define CPER_NOTIFY__MCE                                               \
	CPER_GUID__INIT(0xE8F56FFE, 0x919C, 0x4cc5, 0xBA, 0x88, 0x65, 0xAB, \
		  0xE1, 0x49, 0x13, 0xBB)
#define CPER_NOTIFY__CMC                                               \
	CPER_GUID__INIT(0x2DCE8BB1, 0xBDD7, 0x450e, 0xB9, 0xAD, 0x9C, 0xF4, \
		  0xEB, 0xD4, 0xF8, 0x90)
#define BOOT__TYPE                                                     \
	CPER_GUID__INIT(0x3D61A466, 0xAB40, 0x409a, 0xA6, 0x98, 0xF3, 0x62, \
		  0xD4, 0x64, 0xB3, 0x8F)

#define GPU__CRASHDUMP                                                 \
	CPER_GUID__INIT(0x32AC0C78, 0x2623, 0x48F6, 0xB0, 0xD0, 0x73, 0x65, \
		  0x72, 0x5F, 0xD6, 0xAE)
#define GPU__NONSTANDARD_ERROR                                     \
	CPER_GUID__INIT(0x32AC0C78, 0x2623, 0x48F6, 0x81, 0xA2, 0xAC, 0x69, \
		  0x17, 0x80, 0x55, 0x1D)
#define PROC_ERR__SECTION_TYPE                                         \
	CPER_GUID__INIT(0xDC3EA0B0, 0xA144, 0x4797, 0xB9, 0x5B, 0x53, 0xFA, \
		  0x24, 0x2B, 0x6E, 0x1D)

enum ras_cper_type {
	RAS_CPER_TYPE_RUNTIME,
	RAS_CPER_TYPE_FATAL,
	RAS_CPER_TYPE_BOOT,
	RAS_CPER_TYPE_RMA,
};

enum ras_cper_severity {
	RAS_CPER_SEV_NON_FATAL_UE   = 0,
	RAS_CPER_SEV_FATAL_UE       = 1,
	RAS_CPER_SEV_NON_FATAL_CE   = 2,
	RAS_CPER_SEV_RMA            = 3,

	RAS_CPER_SEV_UNUSED = 10,
};

enum ras_cper_aca_reg {
	RAS_CPER_ACA_REG_CTL    = 0,
	RAS_CPER_ACA_REG_STATUS = 1,
	RAS_CPER_ACA_REG_ADDR   = 2,
	RAS_CPER_ACA_REG_MISC0  = 3,
	RAS_CPER_ACA_REG_CONFIG = 4,
	RAS_CPER_ACA_REG_IPID   = 5,
	RAS_CPER_ACA_REG_SYND   = 6,
	RAS_CPER_ACA_REG_DESTAT	= 8,
	RAS_CPER_ACA_REG_DEADDR	= 9,
	RAS_CPER_ACA_REG_MASK	= 10,

	RAS_CPER_ACA_REG_COUNT     = 16,
};

#pragma pack(push, 1)

struct ras_cper_timestamp {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t flag;
	uint8_t day;
	uint8_t month;
	uint8_t year;
	uint8_t century;
};

struct cper_section_hdr {
	char                     signature[4];  /* "CPER"  */
	uint16_t                 revision;
	uint32_t                 signature_end; /* 0xFFFFFFFF */
	uint16_t                 sec_cnt;
	enum ras_cper_severity error_severity;
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
	struct ras_cper_timestamp timestamp;
	char			platform_id[16];
	struct ras_cper_guid			partition_id;     /* Reserved */
	char			creator_id[16];
	struct ras_cper_guid			notify_type;      /* CMC, MCE */
	char			record_id[8];     /* Unique CPER Entry ID */
	uint32_t		flags;            /* Reserved */
	uint64_t		persistence_info; /* Reserved */
	uint8_t			reserved[12];     /* Reserved */
};

struct cper_section_descriptor {
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
	struct ras_cper_guid			sec_type;
	char				fru_id[16];
	enum ras_cper_severity severity;
	char				fru_text[20];
};

struct runtime_hdr {
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

struct runtime_descriptor {
	struct ras_cper_guid error_type;
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

struct runtime_error_reg {
	uint16_t reg_ctx_type;
	uint16_t reg_arr_size;
	uint32_t msr_addr;
	uint64_t mm_reg_addr;
	uint64_t reg_dump[RAS_CPER_ACA_REG_COUNT];
};

struct cper_section_runtime {
	struct runtime_hdr  hdr;
	struct runtime_descriptor descriptor;
	struct runtime_error_reg  reg;
};

struct crashdump_hdr {
	uint64_t reserved1;
	uint64_t reserved2;
	char     fw_id[48];
	uint64_t reserved3[8];
};

struct fatal_reg_info {
	uint64_t status;
	uint64_t addr;
	uint64_t ipid;
	uint64_t synd;
};

struct crashdump_fatal {
	uint16_t reg_ctx_type;
	uint16_t reg_arr_size;
	uint32_t reserved1;
	uint64_t reserved2;
	struct fatal_reg_info reg;
};

struct crashdump_boot {
	uint16_t reg_ctx_type;
	uint16_t reg_arr_size;
	uint32_t reserved1;
	uint64_t reserved2;
	uint64_t msg[CPER_OAM_MAX_COUNT];
};

struct cper_section_fatal {
	struct crashdump_hdr    hdr;
	struct crashdump_fatal  data;
};

struct cper_section_boot {
	struct crashdump_hdr  hdr;
	struct crashdump_boot data;
};

struct ras_cper_fatal_record {
	struct cper_section_hdr hdr;
	struct cper_section_descriptor descriptor;
	struct cper_section_fatal fatal;
};
#pragma pack(pop)

#define RAS_HDR_LEN				(sizeof(struct cper_section_hdr))
#define RAS_SEC_DESC_LEN			(sizeof(struct cper_sec_desc))

#define RAS_BOOT_SEC_LEN			(sizeof(struct cper_sec_crashdump_boot))
#define RAS_FATAL_SEC_LEN			(sizeof(struct cper_sec_crashdump_fatal))
#define RAS_NONSTD_SEC_LEN			(sizeof(struct cper_sec_nonstd_err))

#define RAS_SEC_DESC_OFFSET(idx)		(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * idx))

#define RAS_BOOT_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_BOOT_SEC_LEN * idx))
#define RAS_FATAL_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_FATAL_SEC_LEN * idx))
#define RAS_NONSTD_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_NONSTD_SEC_LEN * idx))

struct ras_core_context;
struct ras_log_info;
int ras_cper_generate_cper(struct ras_core_context *ras_core,
		struct ras_log_info **trace_list, uint32_t count,
		uint8_t *buf, uint32_t buf_len, uint32_t *real_data_len);
#endif
