// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cxl

#if !defined(_CXL_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CXL_EVENTS_H

#include <linux/tracepoint.h>
#include <linux/pci.h>
#include <asm-generic/unaligned.h>

#include <cxl.h>
#include <cxlmem.h>
#include "core.h"

#define CXL_RAS_UC_CACHE_DATA_PARITY	BIT(0)
#define CXL_RAS_UC_CACHE_ADDR_PARITY	BIT(1)
#define CXL_RAS_UC_CACHE_BE_PARITY	BIT(2)
#define CXL_RAS_UC_CACHE_DATA_ECC	BIT(3)
#define CXL_RAS_UC_MEM_DATA_PARITY	BIT(4)
#define CXL_RAS_UC_MEM_ADDR_PARITY	BIT(5)
#define CXL_RAS_UC_MEM_BE_PARITY	BIT(6)
#define CXL_RAS_UC_MEM_DATA_ECC		BIT(7)
#define CXL_RAS_UC_REINIT_THRESH	BIT(8)
#define CXL_RAS_UC_RSVD_ENCODE		BIT(9)
#define CXL_RAS_UC_POISON		BIT(10)
#define CXL_RAS_UC_RECV_OVERFLOW	BIT(11)
#define CXL_RAS_UC_INTERNAL_ERR		BIT(14)
#define CXL_RAS_UC_IDE_TX_ERR		BIT(15)
#define CXL_RAS_UC_IDE_RX_ERR		BIT(16)

#define show_uc_errs(status)	__print_flags(status, " | ",		  \
	{ CXL_RAS_UC_CACHE_DATA_PARITY, "Cache Data Parity Error" },	  \
	{ CXL_RAS_UC_CACHE_ADDR_PARITY, "Cache Address Parity Error" },	  \
	{ CXL_RAS_UC_CACHE_BE_PARITY, "Cache Byte Enable Parity Error" }, \
	{ CXL_RAS_UC_CACHE_DATA_ECC, "Cache Data ECC Error" },		  \
	{ CXL_RAS_UC_MEM_DATA_PARITY, "Memory Data Parity Error" },	  \
	{ CXL_RAS_UC_MEM_ADDR_PARITY, "Memory Address Parity Error" },	  \
	{ CXL_RAS_UC_MEM_BE_PARITY, "Memory Byte Enable Parity Error" },  \
	{ CXL_RAS_UC_MEM_DATA_ECC, "Memory Data ECC Error" },		  \
	{ CXL_RAS_UC_REINIT_THRESH, "REINIT Threshold Hit" },		  \
	{ CXL_RAS_UC_RSVD_ENCODE, "Received Unrecognized Encoding" },	  \
	{ CXL_RAS_UC_POISON, "Received Poison From Peer" },		  \
	{ CXL_RAS_UC_RECV_OVERFLOW, "Receiver Overflow" },		  \
	{ CXL_RAS_UC_INTERNAL_ERR, "Component Specific Error" },	  \
	{ CXL_RAS_UC_IDE_TX_ERR, "IDE Tx Error" },			  \
	{ CXL_RAS_UC_IDE_RX_ERR, "IDE Rx Error" }			  \
)

TRACE_EVENT(cxl_aer_uncorrectable_error,
	TP_PROTO(const struct cxl_memdev *cxlmd, u32 status, u32 fe, u32 *hl),
	TP_ARGS(cxlmd, status, fe, hl),
	TP_STRUCT__entry(
		__string(memdev, dev_name(&cxlmd->dev))
		__string(host, dev_name(cxlmd->dev.parent))
		__field(u64, serial)
		__field(u32, status)
		__field(u32, first_error)
		__array(u32, header_log, CXL_HEADERLOG_SIZE_U32)
	),
	TP_fast_assign(
		__assign_str(memdev, dev_name(&cxlmd->dev));
		__assign_str(host, dev_name(cxlmd->dev.parent));
		__entry->serial = cxlmd->cxlds->serial;
		__entry->status = status;
		__entry->first_error = fe;
		/*
		 * Embed the 512B headerlog data for user app retrieval and
		 * parsing, but no need to print this in the trace buffer.
		 */
		memcpy(__entry->header_log, hl, CXL_HEADERLOG_SIZE);
	),
	TP_printk("memdev=%s host=%s serial=%lld: status: '%s' first_error: '%s'",
		  __get_str(memdev), __get_str(host), __entry->serial,
		  show_uc_errs(__entry->status),
		  show_uc_errs(__entry->first_error)
	)
);

#define CXL_RAS_CE_CACHE_DATA_ECC	BIT(0)
#define CXL_RAS_CE_MEM_DATA_ECC		BIT(1)
#define CXL_RAS_CE_CRC_THRESH		BIT(2)
#define CLX_RAS_CE_RETRY_THRESH		BIT(3)
#define CXL_RAS_CE_CACHE_POISON		BIT(4)
#define CXL_RAS_CE_MEM_POISON		BIT(5)
#define CXL_RAS_CE_PHYS_LAYER_ERR	BIT(6)

#define show_ce_errs(status)	__print_flags(status, " | ",			\
	{ CXL_RAS_CE_CACHE_DATA_ECC, "Cache Data ECC Error" },			\
	{ CXL_RAS_CE_MEM_DATA_ECC, "Memory Data ECC Error" },			\
	{ CXL_RAS_CE_CRC_THRESH, "CRC Threshold Hit" },				\
	{ CLX_RAS_CE_RETRY_THRESH, "Retry Threshold" },				\
	{ CXL_RAS_CE_CACHE_POISON, "Received Cache Poison From Peer" },		\
	{ CXL_RAS_CE_MEM_POISON, "Received Memory Poison From Peer" },		\
	{ CXL_RAS_CE_PHYS_LAYER_ERR, "Received Error From Physical Layer" }	\
)

TRACE_EVENT(cxl_aer_correctable_error,
	TP_PROTO(const struct cxl_memdev *cxlmd, u32 status),
	TP_ARGS(cxlmd, status),
	TP_STRUCT__entry(
		__string(memdev, dev_name(&cxlmd->dev))
		__string(host, dev_name(cxlmd->dev.parent))
		__field(u64, serial)
		__field(u32, status)
	),
	TP_fast_assign(
		__assign_str(memdev, dev_name(&cxlmd->dev));
		__assign_str(host, dev_name(cxlmd->dev.parent));
		__entry->serial = cxlmd->cxlds->serial;
		__entry->status = status;
	),
	TP_printk("memdev=%s host=%s serial=%lld: status: '%s'",
		  __get_str(memdev), __get_str(host), __entry->serial,
		  show_ce_errs(__entry->status)
	)
);

#define cxl_event_log_type_str(type)				\
	__print_symbolic(type,					\
		{ CXL_EVENT_TYPE_INFO, "Informational" },	\
		{ CXL_EVENT_TYPE_WARN, "Warning" },		\
		{ CXL_EVENT_TYPE_FAIL, "Failure" },		\
		{ CXL_EVENT_TYPE_FATAL, "Fatal" })

TRACE_EVENT(cxl_overflow,

	TP_PROTO(const struct cxl_memdev *cxlmd, enum cxl_event_log_type log,
		 struct cxl_get_event_payload *payload),

	TP_ARGS(cxlmd, log, payload),

	TP_STRUCT__entry(
		__string(memdev, dev_name(&cxlmd->dev))
		__string(host, dev_name(cxlmd->dev.parent))
		__field(int, log)
		__field(u64, serial)
		__field(u64, first_ts)
		__field(u64, last_ts)
		__field(u16, count)
	),

	TP_fast_assign(
		__assign_str(memdev, dev_name(&cxlmd->dev));
		__assign_str(host, dev_name(cxlmd->dev.parent));
		__entry->serial = cxlmd->cxlds->serial;
		__entry->log = log;
		__entry->count = le16_to_cpu(payload->overflow_err_count);
		__entry->first_ts = le64_to_cpu(payload->first_overflow_timestamp);
		__entry->last_ts = le64_to_cpu(payload->last_overflow_timestamp);
	),

	TP_printk("memdev=%s host=%s serial=%lld: log=%s : %u records from %llu to %llu",
		__get_str(memdev), __get_str(host), __entry->serial,
		cxl_event_log_type_str(__entry->log), __entry->count,
		__entry->first_ts, __entry->last_ts)

);

/*
 * Common Event Record Format
 * CXL 3.0 section 8.2.9.2.1; Table 8-42
 */
#define CXL_EVENT_RECORD_FLAG_PERMANENT		BIT(2)
#define CXL_EVENT_RECORD_FLAG_MAINT_NEEDED	BIT(3)
#define CXL_EVENT_RECORD_FLAG_PERF_DEGRADED	BIT(4)
#define CXL_EVENT_RECORD_FLAG_HW_REPLACE	BIT(5)
#define show_hdr_flags(flags)	__print_flags(flags, " | ",			   \
	{ CXL_EVENT_RECORD_FLAG_PERMANENT,	"PERMANENT_CONDITION"		}, \
	{ CXL_EVENT_RECORD_FLAG_MAINT_NEEDED,	"MAINTENANCE_NEEDED"		}, \
	{ CXL_EVENT_RECORD_FLAG_PERF_DEGRADED,	"PERFORMANCE_DEGRADED"		}, \
	{ CXL_EVENT_RECORD_FLAG_HW_REPLACE,	"HARDWARE_REPLACEMENT_NEEDED"	}  \
)

/*
 * Define macros for the common header of each CXL event.
 *
 * Tracepoints using these macros must do 3 things:
 *
 *	1) Add CXL_EVT_TP_entry to TP_STRUCT__entry
 *	2) Use CXL_EVT_TP_fast_assign within TP_fast_assign;
 *	   pass the dev, log, and CXL event header
 *	   NOTE: The uuid must be assigned by the specific trace event
 *	3) Use CXL_EVT_TP_printk() instead of TP_printk()
 *
 * See the generic_event tracepoint as an example.
 */
#define CXL_EVT_TP_entry					\
	__string(memdev, dev_name(&cxlmd->dev))			\
	__string(host, dev_name(cxlmd->dev.parent))		\
	__field(int, log)					\
	__field_struct(uuid_t, hdr_uuid)			\
	__field(u64, serial)					\
	__field(u32, hdr_flags)					\
	__field(u16, hdr_handle)				\
	__field(u16, hdr_related_handle)			\
	__field(u64, hdr_timestamp)				\
	__field(u8, hdr_length)					\
	__field(u8, hdr_maint_op_class)

#define CXL_EVT_TP_fast_assign(cxlmd, l, hdr)					\
	__assign_str(memdev, dev_name(&(cxlmd)->dev));				\
	__assign_str(host, dev_name((cxlmd)->dev.parent));			\
	__entry->log = (l);							\
	__entry->serial = (cxlmd)->cxlds->serial;				\
	__entry->hdr_length = (hdr).length;					\
	__entry->hdr_flags = get_unaligned_le24((hdr).flags);			\
	__entry->hdr_handle = le16_to_cpu((hdr).handle);			\
	__entry->hdr_related_handle = le16_to_cpu((hdr).related_handle);	\
	__entry->hdr_timestamp = le64_to_cpu((hdr).timestamp);			\
	__entry->hdr_maint_op_class = (hdr).maint_op_class

#define CXL_EVT_TP_printk(fmt, ...) \
	TP_printk("memdev=%s host=%s serial=%lld log=%s : time=%llu uuid=%pUb "	\
		"len=%d flags='%s' handle=%x related_handle=%x "		\
		"maint_op_class=%u : " fmt,					\
		__get_str(memdev), __get_str(host), __entry->serial,		\
		cxl_event_log_type_str(__entry->log),				\
		__entry->hdr_timestamp, &__entry->hdr_uuid, __entry->hdr_length,\
		show_hdr_flags(__entry->hdr_flags), __entry->hdr_handle,	\
		__entry->hdr_related_handle, __entry->hdr_maint_op_class,	\
		##__VA_ARGS__)

TRACE_EVENT(cxl_generic_event,

	TP_PROTO(const struct cxl_memdev *cxlmd, enum cxl_event_log_type log,
		 const uuid_t *uuid, struct cxl_event_generic *gen_rec),

	TP_ARGS(cxlmd, log, uuid, gen_rec),

	TP_STRUCT__entry(
		CXL_EVT_TP_entry
		__array(u8, data, CXL_EVENT_RECORD_DATA_LENGTH)
	),

	TP_fast_assign(
		CXL_EVT_TP_fast_assign(cxlmd, log, gen_rec->hdr);
		memcpy(&__entry->hdr_uuid, uuid, sizeof(uuid_t));
		memcpy(__entry->data, gen_rec->data, CXL_EVENT_RECORD_DATA_LENGTH);
	),

	CXL_EVT_TP_printk("%s",
		__print_hex(__entry->data, CXL_EVENT_RECORD_DATA_LENGTH))
);

/*
 * Physical Address field masks
 *
 * General Media Event Record
 * CXL rev 3.0 Section 8.2.9.2.1.1; Table 8-43
 *
 * DRAM Event Record
 * CXL rev 3.0 section 8.2.9.2.1.2; Table 8-44
 */
#define CXL_DPA_FLAGS_MASK			0x3F
#define CXL_DPA_MASK				(~CXL_DPA_FLAGS_MASK)

#define CXL_DPA_VOLATILE			BIT(0)
#define CXL_DPA_NOT_REPAIRABLE			BIT(1)
#define show_dpa_flags(flags)	__print_flags(flags, "|",		   \
	{ CXL_DPA_VOLATILE,			"VOLATILE"		}, \
	{ CXL_DPA_NOT_REPAIRABLE,		"NOT_REPAIRABLE"	}  \
)

/*
 * General Media Event Record - GMER
 * CXL rev 3.0 Section 8.2.9.2.1.1; Table 8-43
 */
#define CXL_GMER_EVT_DESC_UNCORECTABLE_EVENT		BIT(0)
#define CXL_GMER_EVT_DESC_THRESHOLD_EVENT		BIT(1)
#define CXL_GMER_EVT_DESC_POISON_LIST_OVERFLOW		BIT(2)
#define show_event_desc_flags(flags)	__print_flags(flags, "|",		   \
	{ CXL_GMER_EVT_DESC_UNCORECTABLE_EVENT,		"UNCORRECTABLE_EVENT"	}, \
	{ CXL_GMER_EVT_DESC_THRESHOLD_EVENT,		"THRESHOLD_EVENT"	}, \
	{ CXL_GMER_EVT_DESC_POISON_LIST_OVERFLOW,	"POISON_LIST_OVERFLOW"	}  \
)

#define CXL_GMER_MEM_EVT_TYPE_ECC_ERROR			0x00
#define CXL_GMER_MEM_EVT_TYPE_INV_ADDR			0x01
#define CXL_GMER_MEM_EVT_TYPE_DATA_PATH_ERROR		0x02
#define show_mem_event_type(type)	__print_symbolic(type,			\
	{ CXL_GMER_MEM_EVT_TYPE_ECC_ERROR,		"ECC Error" },		\
	{ CXL_GMER_MEM_EVT_TYPE_INV_ADDR,		"Invalid Address" },	\
	{ CXL_GMER_MEM_EVT_TYPE_DATA_PATH_ERROR,	"Data Path Error" }	\
)

#define CXL_GMER_TRANS_UNKNOWN				0x00
#define CXL_GMER_TRANS_HOST_READ			0x01
#define CXL_GMER_TRANS_HOST_WRITE			0x02
#define CXL_GMER_TRANS_HOST_SCAN_MEDIA			0x03
#define CXL_GMER_TRANS_HOST_INJECT_POISON		0x04
#define CXL_GMER_TRANS_INTERNAL_MEDIA_SCRUB		0x05
#define CXL_GMER_TRANS_INTERNAL_MEDIA_MANAGEMENT	0x06
#define show_trans_type(type)	__print_symbolic(type,					\
	{ CXL_GMER_TRANS_UNKNOWN,			"Unknown" },			\
	{ CXL_GMER_TRANS_HOST_READ,			"Host Read" },			\
	{ CXL_GMER_TRANS_HOST_WRITE,			"Host Write" },			\
	{ CXL_GMER_TRANS_HOST_SCAN_MEDIA,		"Host Scan Media" },		\
	{ CXL_GMER_TRANS_HOST_INJECT_POISON,		"Host Inject Poison" },		\
	{ CXL_GMER_TRANS_INTERNAL_MEDIA_SCRUB,		"Internal Media Scrub" },	\
	{ CXL_GMER_TRANS_INTERNAL_MEDIA_MANAGEMENT,	"Internal Media Management" }	\
)

#define CXL_GMER_VALID_CHANNEL				BIT(0)
#define CXL_GMER_VALID_RANK				BIT(1)
#define CXL_GMER_VALID_DEVICE				BIT(2)
#define CXL_GMER_VALID_COMPONENT			BIT(3)
#define show_valid_flags(flags)	__print_flags(flags, "|",		   \
	{ CXL_GMER_VALID_CHANNEL,			"CHANNEL"	}, \
	{ CXL_GMER_VALID_RANK,				"RANK"		}, \
	{ CXL_GMER_VALID_DEVICE,			"DEVICE"	}, \
	{ CXL_GMER_VALID_COMPONENT,			"COMPONENT"	}  \
)

TRACE_EVENT(cxl_general_media,

	TP_PROTO(const struct cxl_memdev *cxlmd, enum cxl_event_log_type log,
		 struct cxl_event_gen_media *rec),

	TP_ARGS(cxlmd, log, rec),

	TP_STRUCT__entry(
		CXL_EVT_TP_entry
		/* General Media */
		__field(u64, dpa)
		__field(u8, descriptor)
		__field(u8, type)
		__field(u8, transaction_type)
		__field(u8, channel)
		__field(u32, device)
		__array(u8, comp_id, CXL_EVENT_GEN_MED_COMP_ID_SIZE)
		__field(u16, validity_flags)
		/* Following are out of order to pack trace record */
		__field(u8, rank)
		__field(u8, dpa_flags)
	),

	TP_fast_assign(
		CXL_EVT_TP_fast_assign(cxlmd, log, rec->hdr);
		__entry->hdr_uuid = CXL_EVENT_GEN_MEDIA_UUID;

		/* General Media */
		__entry->dpa = le64_to_cpu(rec->phys_addr);
		__entry->dpa_flags = __entry->dpa & CXL_DPA_FLAGS_MASK;
		/* Mask after flags have been parsed */
		__entry->dpa &= CXL_DPA_MASK;
		__entry->descriptor = rec->descriptor;
		__entry->type = rec->type;
		__entry->transaction_type = rec->transaction_type;
		__entry->channel = rec->channel;
		__entry->rank = rec->rank;
		__entry->device = get_unaligned_le24(rec->device);
		memcpy(__entry->comp_id, &rec->component_id,
			CXL_EVENT_GEN_MED_COMP_ID_SIZE);
		__entry->validity_flags = get_unaligned_le16(&rec->validity_flags);
	),

	CXL_EVT_TP_printk("dpa=%llx dpa_flags='%s' " \
		"descriptor='%s' type='%s' transaction_type='%s' channel=%u rank=%u " \
		"device=%x comp_id=%s validity_flags='%s'",
		__entry->dpa, show_dpa_flags(__entry->dpa_flags),
		show_event_desc_flags(__entry->descriptor),
		show_mem_event_type(__entry->type),
		show_trans_type(__entry->transaction_type),
		__entry->channel, __entry->rank, __entry->device,
		__print_hex(__entry->comp_id, CXL_EVENT_GEN_MED_COMP_ID_SIZE),
		show_valid_flags(__entry->validity_flags)
	)
);

/*
 * DRAM Event Record - DER
 *
 * CXL rev 3.0 section 8.2.9.2.1.2; Table 8-44
 */
/*
 * DRAM Event Record defines many fields the same as the General Media Event
 * Record.  Reuse those definitions as appropriate.
 */
#define CXL_DER_VALID_CHANNEL				BIT(0)
#define CXL_DER_VALID_RANK				BIT(1)
#define CXL_DER_VALID_NIBBLE				BIT(2)
#define CXL_DER_VALID_BANK_GROUP			BIT(3)
#define CXL_DER_VALID_BANK				BIT(4)
#define CXL_DER_VALID_ROW				BIT(5)
#define CXL_DER_VALID_COLUMN				BIT(6)
#define CXL_DER_VALID_CORRECTION_MASK			BIT(7)
#define show_dram_valid_flags(flags)	__print_flags(flags, "|",			   \
	{ CXL_DER_VALID_CHANNEL,			"CHANNEL"		}, \
	{ CXL_DER_VALID_RANK,				"RANK"			}, \
	{ CXL_DER_VALID_NIBBLE,				"NIBBLE"		}, \
	{ CXL_DER_VALID_BANK_GROUP,			"BANK GROUP"		}, \
	{ CXL_DER_VALID_BANK,				"BANK"			}, \
	{ CXL_DER_VALID_ROW,				"ROW"			}, \
	{ CXL_DER_VALID_COLUMN,				"COLUMN"		}, \
	{ CXL_DER_VALID_CORRECTION_MASK,		"CORRECTION MASK"	}  \
)

TRACE_EVENT(cxl_dram,

	TP_PROTO(const struct cxl_memdev *cxlmd, enum cxl_event_log_type log,
		 struct cxl_event_dram *rec),

	TP_ARGS(cxlmd, log, rec),

	TP_STRUCT__entry(
		CXL_EVT_TP_entry
		/* DRAM */
		__field(u64, dpa)
		__field(u8, descriptor)
		__field(u8, type)
		__field(u8, transaction_type)
		__field(u8, channel)
		__field(u16, validity_flags)
		__field(u16, column)	/* Out of order to pack trace record */
		__field(u32, nibble_mask)
		__field(u32, row)
		__array(u8, cor_mask, CXL_EVENT_DER_CORRECTION_MASK_SIZE)
		__field(u8, rank)	/* Out of order to pack trace record */
		__field(u8, bank_group)	/* Out of order to pack trace record */
		__field(u8, bank)	/* Out of order to pack trace record */
		__field(u8, dpa_flags)	/* Out of order to pack trace record */
	),

	TP_fast_assign(
		CXL_EVT_TP_fast_assign(cxlmd, log, rec->hdr);
		__entry->hdr_uuid = CXL_EVENT_DRAM_UUID;

		/* DRAM */
		__entry->dpa = le64_to_cpu(rec->phys_addr);
		__entry->dpa_flags = __entry->dpa & CXL_DPA_FLAGS_MASK;
		__entry->dpa &= CXL_DPA_MASK;
		__entry->descriptor = rec->descriptor;
		__entry->type = rec->type;
		__entry->transaction_type = rec->transaction_type;
		__entry->validity_flags = get_unaligned_le16(rec->validity_flags);
		__entry->channel = rec->channel;
		__entry->rank = rec->rank;
		__entry->nibble_mask = get_unaligned_le24(rec->nibble_mask);
		__entry->bank_group = rec->bank_group;
		__entry->bank = rec->bank;
		__entry->row = get_unaligned_le24(rec->row);
		__entry->column = get_unaligned_le16(rec->column);
		memcpy(__entry->cor_mask, &rec->correction_mask,
			CXL_EVENT_DER_CORRECTION_MASK_SIZE);
	),

	CXL_EVT_TP_printk("dpa=%llx dpa_flags='%s' descriptor='%s' type='%s' " \
		"transaction_type='%s' channel=%u rank=%u nibble_mask=%x " \
		"bank_group=%u bank=%u row=%u column=%u cor_mask=%s " \
		"validity_flags='%s'",
		__entry->dpa, show_dpa_flags(__entry->dpa_flags),
		show_event_desc_flags(__entry->descriptor),
		show_mem_event_type(__entry->type),
		show_trans_type(__entry->transaction_type),
		__entry->channel, __entry->rank, __entry->nibble_mask,
		__entry->bank_group, __entry->bank,
		__entry->row, __entry->column,
		__print_hex(__entry->cor_mask, CXL_EVENT_DER_CORRECTION_MASK_SIZE),
		show_dram_valid_flags(__entry->validity_flags)
	)
);

/*
 * Memory Module Event Record - MMER
 *
 * CXL res 3.0 section 8.2.9.2.1.3; Table 8-45
 */
#define CXL_MMER_HEALTH_STATUS_CHANGE		0x00
#define CXL_MMER_MEDIA_STATUS_CHANGE		0x01
#define CXL_MMER_LIFE_USED_CHANGE		0x02
#define CXL_MMER_TEMP_CHANGE			0x03
#define CXL_MMER_DATA_PATH_ERROR		0x04
#define CXL_MMER_LSA_ERROR			0x05
#define show_dev_evt_type(type)	__print_symbolic(type,			   \
	{ CXL_MMER_HEALTH_STATUS_CHANGE,	"Health Status Change"	}, \
	{ CXL_MMER_MEDIA_STATUS_CHANGE,		"Media Status Change"	}, \
	{ CXL_MMER_LIFE_USED_CHANGE,		"Life Used Change"	}, \
	{ CXL_MMER_TEMP_CHANGE,			"Temperature Change"	}, \
	{ CXL_MMER_DATA_PATH_ERROR,		"Data Path Error"	}, \
	{ CXL_MMER_LSA_ERROR,			"LSA Error"		}  \
)

/*
 * Device Health Information - DHI
 *
 * CXL res 3.0 section 8.2.9.8.3.1; Table 8-100
 */
#define CXL_DHI_HS_MAINTENANCE_NEEDED				BIT(0)
#define CXL_DHI_HS_PERFORMANCE_DEGRADED				BIT(1)
#define CXL_DHI_HS_HW_REPLACEMENT_NEEDED			BIT(2)
#define show_health_status_flags(flags)	__print_flags(flags, "|",	   \
	{ CXL_DHI_HS_MAINTENANCE_NEEDED,	"MAINTENANCE_NEEDED"	}, \
	{ CXL_DHI_HS_PERFORMANCE_DEGRADED,	"PERFORMANCE_DEGRADED"	}, \
	{ CXL_DHI_HS_HW_REPLACEMENT_NEEDED,	"REPLACEMENT_NEEDED"	}  \
)

#define CXL_DHI_MS_NORMAL							0x00
#define CXL_DHI_MS_NOT_READY							0x01
#define CXL_DHI_MS_WRITE_PERSISTENCY_LOST					0x02
#define CXL_DHI_MS_ALL_DATA_LOST						0x03
#define CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_EVENT_POWER_LOSS			0x04
#define CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_EVENT_SHUTDOWN			0x05
#define CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_IMMINENT				0x06
#define CXL_DHI_MS_WRITE_ALL_DATA_LOSS_EVENT_POWER_LOSS				0x07
#define CXL_DHI_MS_WRITE_ALL_DATA_LOSS_EVENT_SHUTDOWN				0x08
#define CXL_DHI_MS_WRITE_ALL_DATA_LOSS_IMMINENT					0x09
#define show_media_status(ms)	__print_symbolic(ms,			   \
	{ CXL_DHI_MS_NORMAL,						   \
		"Normal"						}, \
	{ CXL_DHI_MS_NOT_READY,						   \
		"Not Ready"						}, \
	{ CXL_DHI_MS_WRITE_PERSISTENCY_LOST,				   \
		"Write Persistency Lost"				}, \
	{ CXL_DHI_MS_ALL_DATA_LOST,					   \
		"All Data Lost"						}, \
	{ CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_EVENT_POWER_LOSS,		   \
		"Write Persistency Loss in the Event of Power Loss"	}, \
	{ CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_EVENT_SHUTDOWN,		   \
		"Write Persistency Loss in Event of Shutdown"		}, \
	{ CXL_DHI_MS_WRITE_PERSISTENCY_LOSS_IMMINENT,			   \
		"Write Persistency Loss Imminent"			}, \
	{ CXL_DHI_MS_WRITE_ALL_DATA_LOSS_EVENT_POWER_LOSS,		   \
		"All Data Loss in Event of Power Loss"			}, \
	{ CXL_DHI_MS_WRITE_ALL_DATA_LOSS_EVENT_SHUTDOWN,		   \
		"All Data loss in the Event of Shutdown"		}, \
	{ CXL_DHI_MS_WRITE_ALL_DATA_LOSS_IMMINENT,			   \
		"All Data Loss Imminent"				}  \
)

#define CXL_DHI_AS_NORMAL		0x0
#define CXL_DHI_AS_WARNING		0x1
#define CXL_DHI_AS_CRITICAL		0x2
#define show_two_bit_status(as) __print_symbolic(as,	   \
	{ CXL_DHI_AS_NORMAL,		"Normal"	}, \
	{ CXL_DHI_AS_WARNING,		"Warning"	}, \
	{ CXL_DHI_AS_CRITICAL,		"Critical"	}  \
)
#define show_one_bit_status(as) __print_symbolic(as,	   \
	{ CXL_DHI_AS_NORMAL,		"Normal"	}, \
	{ CXL_DHI_AS_WARNING,		"Warning"	}  \
)

#define CXL_DHI_AS_LIFE_USED(as)			(as & 0x3)
#define CXL_DHI_AS_DEV_TEMP(as)				((as & 0xC) >> 2)
#define CXL_DHI_AS_COR_VOL_ERR_CNT(as)			((as & 0x10) >> 4)
#define CXL_DHI_AS_COR_PER_ERR_CNT(as)			((as & 0x20) >> 5)

TRACE_EVENT(cxl_memory_module,

	TP_PROTO(const struct cxl_memdev *cxlmd, enum cxl_event_log_type log,
		 struct cxl_event_mem_module *rec),

	TP_ARGS(cxlmd, log, rec),

	TP_STRUCT__entry(
		CXL_EVT_TP_entry

		/* Memory Module Event */
		__field(u8, event_type)

		/* Device Health Info */
		__field(u8, health_status)
		__field(u8, media_status)
		__field(u8, life_used)
		__field(u32, dirty_shutdown_cnt)
		__field(u32, cor_vol_err_cnt)
		__field(u32, cor_per_err_cnt)
		__field(s16, device_temp)
		__field(u8, add_status)
	),

	TP_fast_assign(
		CXL_EVT_TP_fast_assign(cxlmd, log, rec->hdr);
		__entry->hdr_uuid = CXL_EVENT_MEM_MODULE_UUID;

		/* Memory Module Event */
		__entry->event_type = rec->event_type;

		/* Device Health Info */
		__entry->health_status = rec->info.health_status;
		__entry->media_status = rec->info.media_status;
		__entry->life_used = rec->info.life_used;
		__entry->dirty_shutdown_cnt = get_unaligned_le32(rec->info.dirty_shutdown_cnt);
		__entry->cor_vol_err_cnt = get_unaligned_le32(rec->info.cor_vol_err_cnt);
		__entry->cor_per_err_cnt = get_unaligned_le32(rec->info.cor_per_err_cnt);
		__entry->device_temp = get_unaligned_le16(rec->info.device_temp);
		__entry->add_status = rec->info.add_status;
	),

	CXL_EVT_TP_printk("event_type='%s' health_status='%s' media_status='%s' " \
		"as_life_used=%s as_dev_temp=%s as_cor_vol_err_cnt=%s " \
		"as_cor_per_err_cnt=%s life_used=%u device_temp=%d " \
		"dirty_shutdown_cnt=%u cor_vol_err_cnt=%u cor_per_err_cnt=%u",
		show_dev_evt_type(__entry->event_type),
		show_health_status_flags(__entry->health_status),
		show_media_status(__entry->media_status),
		show_two_bit_status(CXL_DHI_AS_LIFE_USED(__entry->add_status)),
		show_two_bit_status(CXL_DHI_AS_DEV_TEMP(__entry->add_status)),
		show_one_bit_status(CXL_DHI_AS_COR_VOL_ERR_CNT(__entry->add_status)),
		show_one_bit_status(CXL_DHI_AS_COR_PER_ERR_CNT(__entry->add_status)),
		__entry->life_used, __entry->device_temp,
		__entry->dirty_shutdown_cnt, __entry->cor_vol_err_cnt,
		__entry->cor_per_err_cnt
	)
);

#define show_poison_trace_type(type)			\
	__print_symbolic(type,				\
	{ CXL_POISON_TRACE_LIST,	"List"   },	\
	{ CXL_POISON_TRACE_INJECT,	"Inject" },	\
	{ CXL_POISON_TRACE_CLEAR,	"Clear"  })

#define __show_poison_source(source)                          \
	__print_symbolic(source,                              \
		{ CXL_POISON_SOURCE_UNKNOWN,   "Unknown"  },  \
		{ CXL_POISON_SOURCE_EXTERNAL,  "External" },  \
		{ CXL_POISON_SOURCE_INTERNAL,  "Internal" },  \
		{ CXL_POISON_SOURCE_INJECTED,  "Injected" },  \
		{ CXL_POISON_SOURCE_VENDOR,    "Vendor"   })

#define show_poison_source(source)			     \
	(((source > CXL_POISON_SOURCE_INJECTED) &&	     \
	 (source != CXL_POISON_SOURCE_VENDOR)) ? "Reserved"  \
	 : __show_poison_source(source))

#define show_poison_flags(flags)                             \
	__print_flags(flags, "|",                            \
		{ CXL_POISON_FLAG_MORE,      "More"     },   \
		{ CXL_POISON_FLAG_OVERFLOW,  "Overflow"  },  \
		{ CXL_POISON_FLAG_SCANNING,  "Scanning"  })

#define __cxl_poison_addr(record)					\
	(le64_to_cpu(record->address))
#define cxl_poison_record_dpa(record)					\
	(__cxl_poison_addr(record) & CXL_POISON_START_MASK)
#define cxl_poison_record_source(record)				\
	(__cxl_poison_addr(record)  & CXL_POISON_SOURCE_MASK)
#define cxl_poison_record_dpa_length(record)				\
	(le32_to_cpu(record->length) * CXL_POISON_LEN_MULT)
#define cxl_poison_overflow(flags, time)				\
	(flags & CXL_POISON_FLAG_OVERFLOW ? le64_to_cpu(time) : 0)

u64 cxl_trace_hpa(struct cxl_region *cxlr, struct cxl_memdev *memdev, u64 dpa);

TRACE_EVENT(cxl_poison,

	TP_PROTO(struct cxl_memdev *cxlmd, struct cxl_region *region,
		 const struct cxl_poison_record *record, u8 flags,
		 __le64 overflow_ts, enum cxl_poison_trace_type trace_type),

	TP_ARGS(cxlmd, region, record, flags, overflow_ts, trace_type),

	TP_STRUCT__entry(
		__string(memdev, dev_name(&cxlmd->dev))
		__string(host, dev_name(cxlmd->dev.parent))
		__field(u64, serial)
		__field(u8, trace_type)
		__string(region, region)
		__field(u64, overflow_ts)
		__field(u64, hpa)
		__field(u64, dpa)
		__field(u32, dpa_length)
		__array(char, uuid, 16)
		__field(u8, source)
		__field(u8, flags)
	    ),

	TP_fast_assign(
		__assign_str(memdev, dev_name(&cxlmd->dev));
		__assign_str(host, dev_name(cxlmd->dev.parent));
		__entry->serial = cxlmd->cxlds->serial;
		__entry->overflow_ts = cxl_poison_overflow(flags, overflow_ts);
		__entry->dpa = cxl_poison_record_dpa(record);
		__entry->dpa_length = cxl_poison_record_dpa_length(record);
		__entry->source = cxl_poison_record_source(record);
		__entry->trace_type = trace_type;
		__entry->flags = flags;
		if (region) {
			__assign_str(region, dev_name(&region->dev));
			memcpy(__entry->uuid, &region->params.uuid, 16);
			__entry->hpa = cxl_trace_hpa(region, cxlmd,
						     __entry->dpa);
		} else {
			__assign_str(region, "");
			memset(__entry->uuid, 0, 16);
			__entry->hpa = ULLONG_MAX;
		}
	    ),

	TP_printk("memdev=%s host=%s serial=%lld trace_type=%s region=%s "  \
		"region_uuid=%pU hpa=0x%llx dpa=0x%llx dpa_length=0x%x "    \
		"source=%s flags=%s overflow_time=%llu",
		__get_str(memdev),
		__get_str(host),
		__entry->serial,
		show_poison_trace_type(__entry->trace_type),
		__get_str(region),
		__entry->uuid,
		__entry->hpa,
		__entry->dpa,
		__entry->dpa_length,
		show_poison_source(__entry->source),
		show_poison_flags(__entry->flags),
		__entry->overflow_ts
	)
);

#endif /* _CXL_EVENTS_H */

#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
