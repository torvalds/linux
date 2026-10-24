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
#include "log_ring.h"

#define CPER_UUID_MAX_SIZE 16
struct ras_cper_guid {
	uint8_t b[CPER_UUID_MAX_SIZE];
};

#define CPER_GUID__INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)			\
	(struct ras_cper_guid)								\
	{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
		(b) & 0xff, ((b) >> 8) & 0xff,					\
		(c) & 0xff, ((c) >> 8) & 0xff,					\
		(d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }}

#define CPER_HDR__REV_1          (0x100)
#define CPER_HDR__REV_AMD_CPU    (0x101)
#define CPER_SEC__MINOR_REV_1    (0x01)
#define CPER_SEC__MAJOR_REV_22   (0x22)
#define CPER_SEC__REV_AMD_CPU    (0x0100)
#define CPER_OAM_MAX_COUNT      (8)

#define CPER_CTX_TYPE__CRASH     (1)
#define CPER_CTX_TYPE__BOOT      (9)

#define CPER_CREATOR_ID__AMDGPU	"amdgpu"

/* Cper Notification Type */
#define CPER_NOTIFY__MCE                                               \
	CPER_GUID__INIT(0xE8F56FFE, 0x919C, 0x4cc5, 0xBA, 0x88, 0x65, 0xAB, \
		  0xE1, 0x49, 0x13, 0xBB)
#define CPER_NOTIFY__CMC                                               \
	CPER_GUID__INIT(0x2DCE8BB1, 0xBDD7, 0x450e, 0xB9, 0xAD, 0x9C, 0xF4, \
		  0xEB, 0xD4, 0xF8, 0x90)
#define BOOT__TYPE                                                     \
	CPER_GUID__INIT(0x3D61A466, 0xAB40, 0x409a, 0xA6, 0x98, 0xF3, 0x62, \
		  0xD4, 0x64, 0xB3, 0x8F)

/* Cper Section Type */
#define GPU__CRASHDUMP                                                 \
	CPER_GUID__INIT(0x32AC0C78, 0x2623, 0x48F6, 0xB0, 0xD0, 0x73, 0x65, \
		  0x72, 0x5F, 0xD6, 0xAE)
#define GPU__NONSTANDARD_ERROR                                     \
	CPER_GUID__INIT(0x32AC0C78, 0x2623, 0x48F6, 0x81, 0xA2, 0xAC, 0x69, \
		  0x17, 0x80, 0x55, 0x1D)
#define PROC_ERR__SECTION_TYPE                                         \
	CPER_GUID__INIT(0xDC3EA0B0, 0xA144, 0x4797, 0xB9, 0x5B, 0x53, 0xFA, \
		  0x24, 0x2B, 0x6E, 0x1D)
#define PROC_ERR__MS_CHECK_TYPE                                        \
	CPER_GUID__INIT(0x48AB7F57, 0xDC34, 0x4F6C, 0xA7, 0xD3, 0xB0, 0xB5, \
		  0xB0, 0xA7, 0x43, 0x14)
#define PLATFORM_MEM__SECTION_TYPE                                     \
	CPER_GUID__INIT(0xA5BC1114, 0x6F64, 0x4EDE, 0xB8, 0x63, 0x3E, 0x83, \
		  0xED, 0x7C, 0x83, 0xB1)
#define PCIE_ERR__SECTION_TYPE                                         \
	CPER_GUID__INIT(0xD995E954, 0xBBC1, 0x430F, 0xAD, 0x91, 0xB4, 0x4D, \
		  0xCB, 0x3C, 0x6F, 0x35)
#define SMN_ERR__SECTION_TYPE                                          \
	CPER_GUID__INIT(0xA2860CC1, 0x8987, 0x4B7C, 0xB8, 0x6A, 0xD5, 0x08, \
		  0xB1, 0x76, 0xBA, 0x70)

#define CPER_PROC_VALID_APIC_ID		BIT_ULL(0)
#define CPER_PROC_ERR_INFO_COUNT(x)	(((u64)(x) & 0x3f) << 2)
#define CPER_PROC_CONTEXT_COUNT(x)	(((u64)(x) & 0x3f) << 8)
#define CPER_AMD_CONTEXT_COUNT(x)	(((u64)(x) & 0x3f) << 8)
#define CPER_AMD_ERR_INFO_COUNT(x)	(((u64)(x) & 0x3f) << 2)

#define CPER_PROC_INFO_VALID_CHECK_INFO	BIT_ULL(0)

#define CPER_MS_CHECK_VALID_ERR_TYPE	BIT_ULL(0)
#define CPER_MS_CHECK_VALID_PCC		BIT_ULL(1)
#define CPER_MS_CHECK_VALID_UNCORRECTED	BIT_ULL(2)
#define CPER_MS_CHECK_VALID_OVERFLOW	BIT_ULL(5)
#define CPER_MS_CHECK_ERR_TYPE_INTERNAL	(5ULL << 16)
#define CPER_MS_CHECK_PCC		BIT_ULL(19)
#define CPER_MS_CHECK_UNCORRECTED	BIT_ULL(20)
#define CPER_MS_CHECK_OVERFLOW		BIT_ULL(23)

#define CPER_MCA_STATUS_OVERFLOW	BIT_ULL(62)
#define CPER_MCA_STATUS_UNCORRECTED	BIT_ULL(61)
#define CPER_MCA_STATUS_PCC		BIT_ULL(57)
#define CPER_MCA_STATUS_DEFERRED	BIT_ULL(44)

#define CPER_SMCA_MC0_STATUS_MSR	(0xc0002001U)
#define CPER_SMCA_BANK_STRIDE		(0x10U)

enum ras_cper_type {
	RAS_CPER_TYPE_RUNTIME,
	RAS_CPER_TYPE_FATAL,
	RAS_CPER_TYPE_BOOT,
	RAS_CPER_TYPE_RMA,
};

/* Cper Error Severity */
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

struct cper_section_desc {
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
	u64 valid_bits;
	u32 pcie_device_id;
	u32 pldm_bundle;
	char     fw_id[48];
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

struct crashdump_error_info {
	struct ras_cper_guid error_type;
	u64 valid_bits;
	u64 check_info;
	u8 fru_part_number[16];
	u8 redfish_event_id[16];
};

struct cper_section_fatal {
	struct crashdump_hdr hdr;
	struct crashdump_error_info error_info;
	struct crashdump_fatal data;
};

struct cper_section_boot {
	struct crashdump_hdr hdr;
	struct crashdump_error_info error_info;
	struct crashdump_boot data;
};

struct cper_processor_section {
	u64 valid_bits;
	u64 apic_id;
	u8 cpuid[48];
};

struct cper_processor_error_info {
	struct ras_cper_guid error_type;
	u64 valid_bits;
	u64 check_info;
	u64 target_id;
	u64 requester_id;
	u64 responder_id;
	u64 instruction_pointer;
};

struct cper_processor_context {
	u16 reg_ctx_type;
	u16 reg_arr_size;
	u32 msr_addr;
	u64 mm_reg_addr;
	u64 reg_dump[RAS_CPER_ACA_REG_COUNT];
};

struct cper_section_processor {
	struct cper_processor_section processor;
	struct cper_processor_error_info error_info;
	struct cper_processor_context context;
};

struct ras_cper_fatal_record {
	struct cper_section_hdr hdr;
	struct cper_section_desc descriptor;
	struct cper_section_fatal fatal;
};

struct ras_cper_boot_record {
	struct cper_section_hdr hdr;
	struct cper_section_desc descriptor;
	struct cper_section_boot boot;
};

struct ras_cper_processor_record {
	struct cper_section_hdr hdr;
	struct cper_section_desc descriptor;
	struct cper_section_processor processor;
};

#pragma pack(pop)

struct ras_core_context;
struct ras_log_info;
enum ras_log_event;
/*
 * Per-event description of how to build its CPER record(s).
 *
 * Fixed fields (severity/notify_type/sec_type) are used as-is unless the matching
 * optional hook (get_severity/get_notify_type/get_sec_type) is provided.
 */
struct ras_cper_profile {
	enum ras_cper_type     cper_type;
	enum ras_cper_severity severity;
	struct ras_cper_guid   notify_type;
	struct ras_cper_guid   sec_type;
	uint32_t section_size;
	/* true: one CPER record per log */
	bool build_record_per_log;

	int (*get_severity)(struct ras_log_info *log, enum ras_cper_severity *out);
	int (*get_notify_type)(struct ras_log_info *log, struct ras_cper_guid *out);
	int (*get_sec_type)(struct ras_log_info *log, struct ras_cper_guid *out);

	/* Required: fill the error-type specific section body for one log */
	int (*fill_section)(struct ras_core_context *ras_core, void *section,
			    struct ras_log_info *log, enum ras_cper_severity sev);
};

struct ras_event_profile_map {
	uint32_t event;
	struct ras_cper_profile *profile;
};

struct ras_cper {
	struct ras_cper_profile *profiles;
	uint32_t nr_profiles;
	struct mutex profile_mutex;
};

#define RAS_HDR_LEN				(sizeof(struct cper_section_hdr))
#define RAS_SEC_DESC_LEN			(sizeof(struct cper_section_desc))

#define RAS_BOOT_SEC_LEN			(sizeof(struct cper_section_boot))
#define RAS_FATAL_SEC_LEN			(sizeof(struct cper_section_fatal))
#define RAS_NONSTD_SEC_LEN			(sizeof(struct cper_section_runtime))
#define RAS_PROC_SEC_LEN			(sizeof(struct cper_section_processor))

#define RAS_SEC_DESC_OFFSET(idx)		(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * idx))

#define RAS_BOOT_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_BOOT_SEC_LEN * idx))
#define RAS_FATAL_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_FATAL_SEC_LEN * idx))
#define RAS_NONSTD_SEC_OFFSET(count, idx) \
	(RAS_HDR_LEN + (RAS_SEC_DESC_LEN * count) + (RAS_NONSTD_SEC_LEN * idx))

int ras_cper_sw_init(struct ras_core_context *ras_core);
int ras_cper_sw_fini(struct ras_core_context *ras_core);
int ras_cper_generate_batch_cper(struct ras_core_context *ras_core,
		struct ras_log_info *batch_logs, uint32_t nr_batch_logs,
		uint8_t *buf, uint32_t buf_len, uint32_t *real_data_len);
int ras_cper_register_profile(struct ras_core_context *ras_core,
	enum ras_log_event event, struct ras_cper_profile *profile);
int ras_cper_unregister_profile(struct ras_core_context *ras_core,
	enum ras_log_event event);
#endif
