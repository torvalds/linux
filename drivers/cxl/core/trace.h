// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cxl

#if !defined(_CXL_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CXL_EVENTS_H

#include <linux/tracepoint.h>
#include <asm-generic/unaligned.h>

#include <cxl.h>
#include <cxlmem.h>

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
	TP_PROTO(const struct device *dev, u32 status, u32 fe, u32 *hl),
	TP_ARGS(dev, status, fe, hl),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, status)
		__field(u32, first_error)
		__array(u32, header_log, CXL_HEADERLOG_SIZE_U32)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->status = status;
		__entry->first_error = fe;
		/*
		 * Embed the 512B headerlog data for user app retrieval and
		 * parsing, but no need to print this in the trace buffer.
		 */
		memcpy(__entry->header_log, hl, CXL_HEADERLOG_SIZE);
	),
	TP_printk("%s: status: '%s' first_error: '%s'",
		  __get_str(dev_name),
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
	TP_PROTO(const struct device *dev, u32 status),
	TP_ARGS(dev, status),
	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(u32, status)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->status = status;
	),
	TP_printk("%s: status: '%s'",
		  __get_str(dev_name), show_ce_errs(__entry->status)
	)
);

#define cxl_event_log_type_str(type)				\
	__print_symbolic(type,					\
		{ CXL_EVENT_TYPE_INFO, "Informational" },	\
		{ CXL_EVENT_TYPE_WARN, "Warning" },		\
		{ CXL_EVENT_TYPE_FAIL, "Failure" },		\
		{ CXL_EVENT_TYPE_FATAL, "Fatal" })

TRACE_EVENT(cxl_overflow,

	TP_PROTO(const struct device *dev, enum cxl_event_log_type log,
		 struct cxl_get_event_payload *payload),

	TP_ARGS(dev, log, payload),

	TP_STRUCT__entry(
		__string(dev_name, dev_name(dev))
		__field(int, log)
		__field(u64, first_ts)
		__field(u64, last_ts)
		__field(u16, count)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name(dev));
		__entry->log = log;
		__entry->count = le16_to_cpu(payload->overflow_err_count);
		__entry->first_ts = le64_to_cpu(payload->first_overflow_timestamp);
		__entry->last_ts = le64_to_cpu(payload->last_overflow_timestamp);
	),

	TP_printk("%s: log=%s : %u records from %llu to %llu",
		__get_str(dev_name), cxl_event_log_type_str(__entry->log),
		__entry->count, __entry->first_ts, __entry->last_ts)

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
 *	3) Use CXL_EVT_TP_printk() instead of TP_printk()
 *
 * See the generic_event tracepoint as an example.
 */
#define CXL_EVT_TP_entry					\
	__string(dev_name, dev_name(dev))			\
	__field(int, log)					\
	__field_struct(uuid_t, hdr_uuid)			\
	__field(u32, hdr_flags)					\
	__field(u16, hdr_handle)				\
	__field(u16, hdr_related_handle)			\
	__field(u64, hdr_timestamp)				\
	__field(u8, hdr_length)					\
	__field(u8, hdr_maint_op_class)

#define CXL_EVT_TP_fast_assign(dev, l, hdr)					\
	__assign_str(dev_name, dev_name(dev));					\
	__entry->log = (l);							\
	memcpy(&__entry->hdr_uuid, &(hdr).id, sizeof(uuid_t));			\
	__entry->hdr_length = (hdr).length;					\
	__entry->hdr_flags = get_unaligned_le24((hdr).flags);			\
	__entry->hdr_handle = le16_to_cpu((hdr).handle);			\
	__entry->hdr_related_handle = le16_to_cpu((hdr).related_handle);	\
	__entry->hdr_timestamp = le64_to_cpu((hdr).timestamp);			\
	__entry->hdr_maint_op_class = (hdr).maint_op_class

#define CXL_EVT_TP_printk(fmt, ...) \
	TP_printk("%s log=%s : time=%llu uuid=%pUb len=%d flags='%s' "		\
		"handle=%x related_handle=%x maint_op_class=%u"			\
		" : " fmt,							\
		__get_str(dev_name), cxl_event_log_type_str(__entry->log),	\
		__entry->hdr_timestamp, &__entry->hdr_uuid, __entry->hdr_length,\
		show_hdr_flags(__entry->hdr_flags), __entry->hdr_handle,	\
		__entry->hdr_related_handle, __entry->hdr_maint_op_class,	\
		##__VA_ARGS__)

TRACE_EVENT(cxl_generic_event,

	TP_PROTO(const struct device *dev, enum cxl_event_log_type log,
		 struct cxl_event_record_raw *rec),

	TP_ARGS(dev, log, rec),

	TP_STRUCT__entry(
		CXL_EVT_TP_entry
		__array(u8, data, CXL_EVENT_RECORD_DATA_LENGTH)
	),

	TP_fast_assign(
		CXL_EVT_TP_fast_assign(dev, log, rec->hdr);
		memcpy(__entry->data, &rec->data, CXL_EVENT_RECORD_DATA_LENGTH);
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

	TP_PROTO(const struct device *dev, enum cxl_event_log_type log,
		 struct cxl_event_gen_media *rec),

	TP_ARGS(dev, log, rec),

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
		CXL_EVT_TP_fast_assign(dev, log, rec->hdr);

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

#endif /* _CXL_EVENTS_H */

#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
