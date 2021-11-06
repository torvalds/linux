/*
 * EVENT_LOG system definitions
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _EVENT_LOG_H_
#define _EVENT_LOG_H_

#include <typedefs.h>
#include <event_log_set.h>
#include <event_log_tag.h>
#include <event_log_payload.h>

/* logstrs header */
#define LOGSTRS_MAGIC   0x4C4F4753
#define LOGSTRS_VERSION 0x1

/* max log size */
#define EVENT_LOG_MAX_SIZE		(64u * 1024u)

/* We make sure that the block size will fit in a single packet
 *  (allowing for a bit of overhead on each packet
 */
#if defined(BCMPCIEDEV)
#define EVENT_LOG_MAX_BLOCK_SIZE	1648
#else
#define EVENT_LOG_MAX_BLOCK_SIZE	1400
#endif

#define EVENT_LOG_BLOCK_SIZE_1K		0x400u
#define EVENT_LOG_WL_BLOCK_SIZE		0x200
#define EVENT_LOG_PSM_BLOCK_SIZE	0x200
#define EVENT_LOG_MEM_API_BLOCK_SIZE	0x200
#define EVENT_LOG_BUS_BLOCK_SIZE	0x200
#define EVENT_LOG_ERROR_BLOCK_SIZE	0x400
#define EVENT_LOG_MSCH_BLOCK_SIZE	0x400
#define EVENT_LOG_WBUS_BLOCK_SIZE	0x100
#define EVENT_LOG_PRSV_PERIODIC_BLOCK_SIZE (0x200u)

#define EVENT_LOG_WL_BUF_SIZE		(EVENT_LOG_WL_BLOCK_SIZE * 3u)

#define EVENT_LOG_TOF_INLINE_BLOCK_SIZE	1300u
#define EVENT_LOG_TOF_INLINE_BUF_SIZE (EVENT_LOG_TOF_INLINE_BLOCK_SIZE * 3u)

#define EVENT_LOG_PRSRV_BUF_SIZE	(EVENT_LOG_MAX_BLOCK_SIZE * 2)
#define EVENT_LOG_BUS_PRSRV_BUF_SIZE	(EVENT_LOG_BUS_BLOCK_SIZE * 2)
#define EVENT_LOG_WBUS_PRSRV_BUF_SIZE	(EVENT_LOG_WBUS_BLOCK_SIZE * 2)

#define EVENT_LOG_BLOCK_SIZE_PRSRV_CHATTY	(EVENT_LOG_MAX_BLOCK_SIZE * 1)
#define EVENT_LOG_BLOCK_SIZE_BUS_PRSRV_CHATTY	(EVENT_LOG_MAX_BLOCK_SIZE * 1)

/* Maximum event log record payload size = 1016 bytes or 254 words. */
#define EVENT_LOG_MAX_RECORD_PAYLOAD_SIZE	254

#define EVENT_LOG_EXT_HDR_IND		(0x01)
#define EVENT_LOG_EXT_HDR_BIN_DATA_IND	(0x01 << 1)
/* Format number to send binary data with extended event log header */
#define EVENT_LOG_EXT_HDR_BIN_FMT_NUM	(0x3FFE << 2)

#define EVENT_LOGSET_ID_MASK	0x3F
/* For event_log_get iovar, set values from 240 to 255 mean special commands for a group of sets */
#define EVENT_LOG_GET_IOV_CMD_MASK	(0xF0u)
#define EVENT_LOG_GET_IOV_CMD_ID_MASK	(0xFu)
#define EVENT_LOG_GET_IOV_CMD_ID_FORCE_FLUSH_PRSRV	(0xEu) /* 240 + 14 = 254 */
#define EVENT_LOG_GET_IOV_CMD_ID_FORCE_FLUSH_ALL	(0xFu) /* 240 + 15 = 255 */

/*
 * There are multiple levels of objects define here:
 *   event_log_set - a set of buffers
 *   event log groups - every event log call is part of just one.  All
 *                      event log calls in a group are handled the
 *                      same way.  Each event log group is associated
 *                      with an event log set or is off.
 */

#ifndef __ASSEMBLER__

/* On the external system where the dumper is we need to make sure
 * that these types are the same size as they are on the ARM the
 * produced them
 */
#ifdef EVENT_LOG_DUMPER
#define _EL_BLOCK_PTR uint32
#define _EL_TYPE_PTR uint32
#define _EL_SET_PTR uint32
#define _EL_TOP_PTR uint32
#else
#define _EL_BLOCK_PTR struct event_log_block *
#define _EL_TYPE_PTR uint32 *
#define _EL_SET_PTR struct event_log_set **
#define _EL_TOP_PTR struct event_log_top *
#endif /* EVENT_LOG_DUMPER */

/* Event log sets (a logical circurlar buffer) consist of one or more
 * event_log_blocks.  The blocks themselves form a logical circular
 * list.  The log entries are placed in each event_log_block until it
 * is full.  Logging continues with the next event_log_block in the
 * event_set until the last event_log_block is reached and then
 * logging starts over with the first event_log_block in the
 * event_set.
 */
typedef struct event_log_block {
	_EL_BLOCK_PTR next_block;
	_EL_BLOCK_PTR prev_block;
	_EL_TYPE_PTR end_ptr;

	/* Start of packet sent for log tracing */
	uint16 pktlen;			/* Size of rest of block */
	uint16 count;			/* Logtrace counter */
	uint32 extra_hdr_info;		/* LSB: 6 bits set id. MSB 24 bits reserved */
	uint32 event_logs;		/* Pointer to BEGINNING of event logs */
	/* Event logs go here. Do not put extra fields below. */
} event_log_block_t;

/* Relative offset of extra_hdr_info field frpm pktlen field in log block */
#define EVENT_LOG_BUF_EXTRA_HDR_INFO_REL_PKTLEN_OFFSET		\
	(OFFSETOF(event_log_block_t, extra_hdr_info) -	OFFSETOF(event_log_block_t, pktlen))

#define EVENT_LOG_SETID_MASK	(0x3Fu)

#define EVENT_LOG_BLOCK_HDRLEN		(sizeof(((event_log_block_t *) 0)->pktlen) \
					+ sizeof(((event_log_block_t *) 0)->count) \
					+ sizeof(((event_log_block_t *) 0)->extra_hdr_info))
#define EVENT_LOG_BLOCK_LEN		(EVENT_LOG_BLOCK_HDRLEN	+ sizeof(event_log_hdr_t))

#define EVENT_LOG_PRESERVE_BLOCK	(1 << 0)
#define EVENT_LOG_BLOCK_FLAG_MASK	0xff000000u
#define EVENT_LOG_BLOCK_FLAG_SHIFT	24u

#define EVENT_LOG_BLOCK_GET_PREV_BLOCK(block)	((_EL_BLOCK_PTR)(((uint32)((block)->prev_block)) & \
	~EVENT_LOG_BLOCK_FLAG_MASK))
#define EVENT_LOG_BLOCK_SET_PREV_BLOCK(block, prev)	((block)->prev_block = \
	((_EL_BLOCK_PTR)((((uint32)(block)->prev_block) & EVENT_LOG_BLOCK_FLAG_MASK) | \
	(((uint32)(prev)) & ~EVENT_LOG_BLOCK_FLAG_MASK))))
#define EVENT_LOG_BLOCK_GET_FLAG(block)	((((uint32)(block)->prev_block) &	\
	EVENT_LOG_BLOCK_FLAG_MASK) >> EVENT_LOG_BLOCK_FLAG_SHIFT)
#define EVENT_LOG_BLOCK_SET_FLAG(block, flag) ((block)->prev_block =	\
		(_EL_BLOCK_PTR)(((uint32)EVENT_LOG_BLOCK_GET_PREV_BLOCK(block)) | flag))
#define EVENT_LOG_BLOCK_OR_FLAG(block, flag)	EVENT_LOG_BLOCK_SET_FLAG(block,	\
		(EVENT_LOG_BLOCK_GET_FLAG(block) | flag) << EVENT_LOG_BLOCK_FLAG_SHIFT)

typedef enum {
	SET_DESTINATION_INVALID = -1,
	SET_DESTINATION_HOST = 0,	/* Eventlog buffer is sent out to host once filled. */
	SET_DESTINATION_NONE = 1,	/* The buffer is not sent out, and it will be overwritten
					   * with new messages.
					   */
	SET_DESTINATION_FORCE_FLUSH_TO_HOST = 2, /* Buffers are sent to host once and then the
						    *  value is reset back to SET_DESTINATION_NONE.
						    */
	SET_DESTINATION_FLUSH_ON_WATERMARK = 3, /* Buffers are sent to host when the watermark is
						 * reached, defined by the feature /chip
						 */
	SET_DESTINATION_MAX
} event_log_set_destination_t;

/* sub destination for routing at the host */
typedef enum {
	SET_SUB_DESTINATION_0 = 0,
	SET_SUB_DESTINATION_1 = 1,
	SET_SUB_DESTINATION_2 = 2,
	SET_SUB_DESTINATION_3 = 3,
	SET_SUB_DESTINATION_DEFAULT = SET_SUB_DESTINATION_0
} event_log_set_sub_destination_t;

/* There can be multiple event_sets with each logging a set of
 * associated events (i.e, "fast" and "slow" events).
 */
typedef struct event_log_set {
	_EL_BLOCK_PTR first_block; 	/* Pointer to first event_log block */
	_EL_BLOCK_PTR last_block; 	/* Pointer to last event_log block */
	_EL_BLOCK_PTR logtrace_block;	/* next block traced */
	_EL_BLOCK_PTR cur_block;   	/* Pointer to current event_log block */
	_EL_TYPE_PTR cur_ptr;      	/* Current event_log pointer */
	uint32 blockcount;		/* Number of blocks */
	uint16 logtrace_count;		/* Last count for logtrace */
	uint16 blockfill_count;		/* Fill count for logtrace */
	uint32 timestamp;		/* Last timestamp event */
	uint32 cyclecount;		/* Cycles at last timestamp event */
	event_log_set_destination_t destination;
	uint16 size;			/* same size for all buffers in one  set */
	uint16 flags;
	uint16 num_preserve_blocks;
	event_log_set_sub_destination_t sub_destination;
	uint16	water_mark;		/* not used yet: threshold to flush host in percent */
	uint32	period;			/* period to flush host in ms */
	uint32	last_rpt_ts;	/* last time to flush  in ms */
} event_log_set_t;

/* Definition of flags in set */
#define EVENT_LOG_SET_SHRINK_ACTIVE	(1 << 0)
#define EVENT_LOG_SET_CONFIG_PARTIAL_BLK_SEND	(0x1 << 1)
#define EVENT_LOG_SET_CHECK_LOG_RATE	(1 << 2)
#define EVENT_LOG_SET_PERIODIC			(1 << 3)
#define EVENT_LOG_SET_D3PRSV			(1 << 4)

/* Top data structure for access to everything else */
typedef struct event_log_top {
	uint32 magic;
#define EVENT_LOG_TOP_MAGIC 0x474C8669 /* 'EVLG' */
	uint32 version;
#define EVENT_LOG_VERSION 1
	uint32 num_sets;
	uint32 logstrs_size;		/* Size of lognums + logstrs area */
	uint32 timestamp;		/* Last timestamp event */
	uint32 cyclecount;		/* Cycles at last timestamp event */
	_EL_SET_PTR sets;		/* Ptr to array of <num_sets> set ptrs */
	uint16 log_count;		/* Number of event logs from last flush */
	uint16 rate_hc;			/* Max number of prints per second */
	uint32 hc_timestamp;		/* Timestamp of last hc window starting */
	bool cpu_freq_changed;		/* Set to TRUE when CPU freq changed */
	bool hostmem_access_enabled;	/* Is host memory access enabled for log delivery */
	bool event_trace_enabled;	/* WLC_E_TRACE enabled/disabled */
} event_log_top_t;

/* structure of the trailing 3 words in logstrs.bin */
typedef struct {
	uint32 fw_id;		/* FWID will be written by tool later */
	uint32 flags;		/* 0th bit indicates whether encrypted or not */
	/* Keep version and magic last since "header" is appended to the end of logstrs file. */
	uint32 version;		/* Header version */
	uint32 log_magic;	/* MAGIC number for verification 'LOGS' */
} logstr_trailer_t;

/* Data structure of Keeping the Header from logstrs.bin */
typedef struct {
	uint32 logstrs_size;    /* Size of the file */
	uint32 rom_lognums_offset; /* Offset to the ROM lognum */
	uint32 ram_lognums_offset; /* Offset to the RAM lognum */
	uint32 rom_logstrs_offset; /* Offset to the ROM logstr */
	uint32 ram_logstrs_offset; /* Offset to the RAM logstr */
	uint32 fw_id;		/* FWID will be written by tool later */
	uint32 flags;		/* 0th bit indicates whether encrypted or not */
	/* Keep version and magic last since "header" is appended to the end of logstrs file. */
	uint32 version;            /* Header version */
	uint32 log_magic;       /* MAGIC number for verification 'LOGS' */
} logstr_header_t;

/* Data structure of Keeping the Header from logstrs.bin */
typedef struct {
	uint32 logstrs_size;    /* Size of the file */
	uint32 rom_lognums_offset; /* Offset to the ROM lognum */
	uint32 ram_lognums_offset; /* Offset to the RAM lognum */
	uint32 rom_logstrs_offset; /* Offset to the ROM logstr */
	uint32 ram_logstrs_offset; /* Offset to the RAM logstr */
	/* Keep version and magic last since "header" is appended to the end of logstrs file. */
	uint32 version;            /* Header version */
	uint32 log_magic;       /* MAGIC number for verification 'LOGS' */
} logstr_header_v1_t;

/* Event log configuration table */
typedef struct evt_log_tag_entry {
	uint16	tag; /* Tag value. */
	uint8	set; /* Set number. */
	uint8	refcnt; /* Ref_count if sdc is used */
} evt_log_tag_entry_t;

#ifdef BCMDRIVER
/* !!! The following section is for kernel mode code only !!! */
#include <osl_decl.h>

extern bool d3_preserve_enab;
#if defined(ROM_ENAB_RUNTIME_CHECK)
	#define D3_PRESERVE_ENAB()   (d3_preserve_enab)
#elif defined(EVENTLOG_D3_PRESERVE_DISABLED)
	#define D3_PRESERVE_ENAB()   (0)
#else
	#define D3_PRESERVE_ENAB()   (1)
#endif

#if defined(EVENTLOG_PRSV_PERIODIC)
extern bool prsv_periodic_enab;
#if defined(ROM_ENAB_RUNTIME_CHECK)
	#define PRSV_PRD_ENAB()   (prsv_periodic_enab)
#elif defined(EVENTLOG_PRSV_PERIODIC_DISABLED)
	#define PRSV_PRD_ENAB()   (0)
#else
	#define PRSV_PRD_ENAB()   (1)
#endif
#endif /* EVENTLOG_PRSV_PERIODIC */

/*
 * Use the following macros for generating log events.
 *
 * The FAST versions check the enable of the tag before evaluating the arguments and calling the
 * event_log function.  This adds 5 instructions.  The COMPACT versions evaluate the arguments
 * and call the event_log function unconditionally.  The event_log function will then skip logging
 * if this tag is disabled.
 *
 * To support easy usage of existing debugging (e.g. msglevel) via macro re-definition there are
 * two variants of these macros to help.
 *
 * First there are the CAST versions.  The event_log function normally logs uint32 values or else
 * they have to be cast to uint32.  The CAST versions blindly cast for you so you don't have to edit
 * any existing code.
 *
 * Second there are the PAREN_ARGS versions.  These expect the logging format string and arguments
 * to be enclosed in parentheses.  This allows us to make the following mapping of an existing
 * msglevel macro:
 *  #define WL_ERROR(args)   EVENT_LOG_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ERROR, args)
 *
 * The versions of the macros without FAST or COMPACT in their name are just synonyms for the
 * COMPACT versions.
 *
 * You should use the COMPACT macro (or its synonym) in cases where there is some preceding logic
 * that prevents the execution of the macro, e.g. WL_ERROR by definition rarely gets executed.
 * Use the FAST macro in performance sensitive paths. The key concept here is that you should be
 * assuming that your macro usage is compiled into ROM and can't be changed ... so choose wisely.
 *
 */

#if !defined(EVENT_LOG_DUMPER) && !defined(DHD_EFI)

#ifndef EVENT_LOG_COMPILE

/* Null define if no tracing */
#define EVENT_LOG(tag, fmt, ...)
#define EVENT_LOG_FAST(tag, fmt, ...)
#define EVENT_LOG_COMPACT(tag, fmt, ...)

#define EVENT_LOG_CAST(tag, fmt, ...)
#define EVENT_LOG_FAST_CAST(tag, fmt, ...)
#define EVENT_LOG_COMPACT_CAST(tag, fmt, ...)

#define EVENT_LOG_CAST_PAREN_ARGS(tag, pargs)
#define EVENT_LOG_FAST_CAST_PAREN_ARGS(tag, pargs)
#define EVENT_LOG_COMPACT_CAST_PAREN_ARGS(tag, pargs)

#define EVENT_LOG_IF_READY(tag, fmt, ...)
#define EVENT_LOG_IS_ON(tag)		0
#define EVENT_LOG_IS_LOG_ON(tag)	0

#define EVENT_LOG_BUFFER(tag, buf, size)
#define EVENT_LOG_PRSRV_FLUSH()
#define EVENT_LOG_FORCE_FLUSH_ALL()
#define EVENT_LOG_FORCE_FLUSH_PRSRV_LOG_ALL()

#else  /* EVENT_LOG_COMPILE */

/* The first few _EVENT_LOGX() macros are special because they can be done more
 * efficiently this way and they are the common case.  Once there are too many
 * parameters the code size starts to be an issue and a loop is better
 * The trailing arguments to the _EVENT_LOGX() macros are the format string, 'fmt',
 * followed by the variable parameters for the format. The format string is not
 * needed in the event_logX() replacement text, so fmt is dropped in all cases.
 */
#define _EVENT_LOG0(tag, fmt_num, fmt)			\
	event_log0(tag, fmt_num)
#define _EVENT_LOG1(tag, fmt_num, fmt, t1)		\
	event_log1(tag, fmt_num, t1)
#define _EVENT_LOG2(tag, fmt_num, fmt, t1, t2)		\
	event_log2(tag, fmt_num, t1, t2)
#define _EVENT_LOG3(tag, fmt_num, fmt, t1, t2, t3)	\
	event_log3(tag, fmt_num, t1, t2, t3)
#define _EVENT_LOG4(tag, fmt_num, fmt, t1, t2, t3, t4)	\
	event_log4(tag, fmt_num, t1, t2, t3, t4)

/* The rest call the generic routine that takes a count */
#define _EVENT_LOG5(tag, fmt_num, fmt, ...) event_logn(5, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG6(tag, fmt_num, fmt, ...) event_logn(6, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG7(tag, fmt_num, fmt, ...) event_logn(7, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG8(tag, fmt_num, fmt, ...) event_logn(8, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG9(tag, fmt_num, fmt, ...) event_logn(9, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGA(tag, fmt_num, fmt, ...) event_logn(10, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGB(tag, fmt_num, fmt, ...) event_logn(11, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGC(tag, fmt_num, fmt, ...) event_logn(12, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGD(tag, fmt_num, fmt, ...) event_logn(13, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGE(tag, fmt_num, fmt, ...) event_logn(14, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGF(tag, fmt_num, fmt, ...) event_logn(15, tag, fmt_num, __VA_ARGS__)

/* Casting  low level macros */
#define _EVENT_LOG_CAST0(tag, fmt_num, fmt)		\
	event_log0(tag, fmt_num)
#define _EVENT_LOG_CAST1(tag, fmt_num, fmt, t1)		\
	event_log1(tag, fmt_num, (uint32)(t1))
#define _EVENT_LOG_CAST2(tag, fmt_num, fmt, t1, t2)	\
	event_log2(tag, fmt_num, (uint32)(t1), (uint32)(t2))
#define _EVENT_LOG_CAST3(tag, fmt_num, fmt, t1, t2, t3)	\
	event_log3(tag, fmt_num, (uint32)(t1), (uint32)(t2), (uint32)(t3))
#define _EVENT_LOG_CAST4(tag, fmt_num, fmt, t1, t2, t3, t4)	\
	event_log4(tag, fmt_num, (uint32)(t1), (uint32)(t2), (uint32)(t3), (uint32)(t4))

/* The rest call the generic routine that takes a count */
#define _EVENT_LOG_CAST5(tag, fmt_num, ...) _EVENT_LOG5(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CAST6(tag, fmt_num, ...) _EVENT_LOG6(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CAST7(tag, fmt_num, ...) _EVENT_LOG7(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CAST8(tag, fmt_num, ...) _EVENT_LOG8(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CAST9(tag, fmt_num, ...) _EVENT_LOG9(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTA(tag, fmt_num, ...) _EVENT_LOGA(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTB(tag, fmt_num, ...) _EVENT_LOGB(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTC(tag, fmt_num, ...) _EVENT_LOGC(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTD(tag, fmt_num, ...) _EVENT_LOGD(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTE(tag, fmt_num, ...) _EVENT_LOGE(tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG_CASTF(tag, fmt_num, ...) _EVENT_LOGF(tag, fmt_num, __VA_ARGS__)

/* Hack to make the proper routine call when variadic macros get
 * passed.  Note the max of 15 arguments.  More than that can't be
 * handled by the event_log entries anyways so best to catch it at compile
 * time
 *
 * Here is what happens with this macro: when _EVENT_LOG expands this macro,
 * its __VA_ARGS__ argument is expanded. If __VA_ARGS__ contains only ONE
 * argument, for example, then F maps to _1, E maps to _2, and so on, so that
 * N maps to 0, and the macro expands to BASE ## N or BASE ## 0 which is
 * EVENT_LOG0. If __VA_ARGS__ contains two arguments, then everything is
 * shifted down by one, because the second argument in __VA_ARGS__ now maps
 * to _1, so F maps to _2, E maps to _3, and so on, and 1 (instead of 0) maps
 * to N, and this macro expands to become _EVENT_LOG1. This continues all
 * the way up until __VA_ARGS__ has 15 arguments, in which case, stuff in
 * __VA_ARGS__ maps to all of the values _1 through _F, which makes F (in'
 * the _EVENT_LOG macro) map to N, and this macro then expands to EVENT_LOGF.
 */

#define _EVENT_LOG_VA_NUM_ARGS(BASE, _FMT, _1, _2, _3, _4, _5, _6, _7, _8, _9, \
			       _A, _B, _C, _D, _E, _F, N, ...) BASE ## N

/* Take a variable number of args and replace with only the first */
#define FIRST_ARG(a1, ...) a1

/* base = _EVENT_LOG for no casting
 * base = _EVENT_LOG_CAST for casting of fmt arguments to uint32.
 *        Only first 4 arguments are cast to uint32. event_logn() is called
 *        if more than 4 arguments are present. This function internally assumes
 *        all arguments are uint32
 *
 * The variable args in this call are the format string followed by the variable
 * parameters for the format. E.g.
 *
 *     __VA_ARGS__ = "answer: %d", 42
 *
 * This means __VA_ARGS__ always has one or more arguments. Guaranteeing a non-empty
 * __VA_ARGS__ means the special use of " , ## __VA_ARGS__" is not required to deal
 * with a dangling comma --- the comma will always be followed with at leaset the format
 * string. The use of ## caused issues when the format args contained a function like
 * macro that expanded to more than one arg. The ## prevented macro expansion, so the
 * _EVENT_LOG_VA_NUM_ARGS() calculation of the number of args was incorrect.
 * Without the ##, the __VA_ARGS__ are macro replaced, and the num args calculation is
 * accurate.
 *
 * This macro is setup so that if __VA_ARGS__ is as short as possible, then the "0" will
 * map to "N" in the _EVENT_LOG_VA_NUM_ARGS macro, and that macro then expands to become
 * _EVENT_LOG0. As __VA_ARGS__ gets longer, then the item that gets mapped to "N" gets
 * pushed further and further up, so that by the time __VA_ARGS__ has 15 additional
 * arguments, then "F" maps to "N" in the _EVENT_LOG_VA_NUM_ARGS macro.
 */
#define _EVENT_LOG(base, tag, ...)					\
	static char logstr[] __attribute__ ((section(".logstrs"))) = FIRST_ARG(__VA_ARGS__); \
	static uint32 fmtnum __attribute__ ((section(".lognums"))) = (uint32) &logstr; \
	_EVENT_LOG_VA_NUM_ARGS(base, __VA_ARGS__,			\
			       F, E, D, C, B, A, 9, 8,			\
			       7, 6, 5, 4, 3, 2, 1, 0)			\
	(tag, (int) &fmtnum, __VA_ARGS__)

#define EVENT_LOG_FAST(tag, ...)					\
	do {								\
		if (event_log_tag_sets != NULL) {			\
			uint8 tag_flag = *(event_log_tag_sets + tag);	\
			if ((tag_flag & ~EVENT_LOG_TAG_FLAG_SET_MASK) != 0) {	\
				_EVENT_LOG(_EVENT_LOG, tag, __VA_ARGS__);	\
			}						\
		}							\
	} while (0)

#define EVENT_LOG_COMPACT(tag, ...)					\
	do {								\
		_EVENT_LOG(_EVENT_LOG, tag, __VA_ARGS__);		\
	} while (0)

/* Event log macro with casting to uint32 of arguments */
#define EVENT_LOG_FAST_CAST(tag, ...)					\
	do {								\
		if (event_log_tag_sets != NULL) {			\
			uint8 tag_flag = *(event_log_tag_sets + tag);	\
			if ((tag_flag & ~EVENT_LOG_TAG_FLAG_SET_MASK) != 0) {	\
				_EVENT_LOG(_EVENT_LOG_CAST, tag, __VA_ARGS__);	\
			}						\
		}							\
	} while (0)

#define EVENT_LOG_COMPACT_CAST(tag, ...)				\
	do {								\
		_EVENT_LOG(_EVENT_LOG_CAST, tag, __VA_ARGS__);		\
	} while (0)

#define EVENT_LOG(tag, ...) EVENT_LOG_COMPACT(tag, __VA_ARGS__)

#define EVENT_LOG_CAST(tag, ...) EVENT_LOG_COMPACT_CAST(tag, __VA_ARGS__)

#define _EVENT_LOG_REMOVE_PAREN(...) __VA_ARGS__
#define EVENT_LOG_REMOVE_PAREN(args) _EVENT_LOG_REMOVE_PAREN args

#define EVENT_LOG_CAST_PAREN_ARGS(tag, pargs)				\
		EVENT_LOG_CAST(tag, EVENT_LOG_REMOVE_PAREN(pargs))

#define EVENT_LOG_FAST_CAST_PAREN_ARGS(tag, pargs)			\
		EVENT_LOG_FAST_CAST(tag, EVENT_LOG_REMOVE_PAREN(pargs))

#define EVENT_LOG_COMPACT_CAST_PAREN_ARGS(tag, pargs)			\
		EVENT_LOG_COMPACT_CAST(tag, EVENT_LOG_REMOVE_PAREN(pargs))

/* Minimal event logging. Event log internally calls event_logx()
 * log return address in caller.
 * Note that the if(0){..} below is to avoid compiler warnings
 * due to unused variables caused by this macro
 */
#define EVENT_LOG_RA(tag, args)						\
	do {								\
		if (0) {						\
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(tag, args);	\
		}							\
		event_log_caller_return_address(tag);			\
	} while (0)

#define EVENT_LOG_IF_READY(_tag, ...) \
	do {                                \
		if (event_log_is_ready()) {             \
			EVENT_LOG(_tag, __VA_ARGS__); \
		}                           \
	}                               \
	while (0)

#define EVENT_LOG_IS_ON(tag) (*(event_log_tag_sets + (tag)) & ~EVENT_LOG_TAG_FLAG_SET_MASK)
#define EVENT_LOG_IS_LOG_ON(tag) (*(event_log_tag_sets + (tag)) & EVENT_LOG_TAG_FLAG_LOG)

#define EVENT_LOG_BUFFER(tag, buf, size)	event_log_buffer(tag, buf, size)
#define EVENT_DUMP	event_log_buffer

/* EVENT_LOG_PRSRV_FLUSH() will be deprecated. Use EVENT_LOG_FORCE_FLUSH_ALL instead */
#define EVENT_LOG_PRSRV_FLUSH()		event_log_force_flush_all()
#define EVENT_LOG_FORCE_FLUSH_ALL()	event_log_force_flush_all()

#ifdef PRESERVE_LOG
#define EVENT_LOG_FORCE_FLUSH_PRSRV_LOG_ALL()	event_log_force_flush_preserve_all()
#else
#define EVENT_LOG_FORCE_FLUSH_PRSRV_LOG_ALL()
#endif /* PRESERVE_LOG */

extern uint8 *event_log_tag_sets;

extern int event_log_init(osl_t *osh);
extern int event_log_set_init(osl_t *osh, int set_num, int size);
extern int event_log_set_expand(osl_t *osh, int set_num, int size);
extern int event_log_set_shrink(osl_t *osh, int set_num, int size);

extern int event_log_tag_start(int tag, int set_num, int flags);
extern int event_log_tag_set_retrieve(int tag);
extern int event_log_tag_flags_retrieve(int tag);
extern int event_log_tag_stop(int tag);

typedef void (*event_log_logtrace_trigger_fn_t)(void *ctx);
void event_log_set_logtrace_trigger_fn(event_log_logtrace_trigger_fn_t fn, void *ctx);

event_log_top_t *event_log_get_top(void);

extern int event_log_get(int set_num, int buflen, void *buf);

extern uint8 *event_log_next_logtrace(int set_num);
extern uint32 event_log_logtrace_max_buf_count(int set_num);
extern int event_log_set_type(int set_num, uint8 *type, int is_get);
extern int event_log_flush_set(wl_el_set_flush_prsrv_t *flush, int is_set);

extern void event_log0(int tag, int fmtNum);
extern void event_log1(int tag, int fmtNum, uint32 t1);
extern void event_log2(int tag, int fmtNum, uint32 t1, uint32 t2);
extern void event_log3(int tag, int fmtNum, uint32 t1, uint32 t2, uint32 t3);
extern void event_log4(int tag, int fmtNum, uint32 t1, uint32 t2, uint32 t3, uint32 t4);
extern void event_logn(int num_args, int tag, int fmtNum, ...);
#ifdef ROM_COMPAT_MSCH_PROFILER
/* For compatibility with ROM, for old msch event log function to pass parameters in stack */
extern void event_logv(int num_args, int tag, int fmtNum, va_list ap);
#endif /* ROM_COMPAT_MSCH_PROFILER */

extern void event_log_time_sync(uint32 ms);
extern bool event_log_time_sync_required(void);
extern void event_log_cpu_freq_changed(void);
extern void event_log_buffer(int tag, const uint8 *buf, int size);
extern void event_log_caller_return_address(int tag);
extern int event_log_set_destination_set(int set, event_log_set_destination_t dest);
extern event_log_set_destination_t event_log_set_destination_get(int set);
extern int event_log_set_sub_destination_set(uint set, event_log_set_sub_destination_t dest);
extern event_log_set_sub_destination_t event_log_set_sub_destination_get(uint set);
extern int event_log_flush_log_buffer(int set);
extern int event_log_force_flush_all(void);
extern int event_log_force_flush(int set);

extern uint16 event_log_get_available_space(int set);
extern bool event_log_is_tag_valid(int tag);
/* returns number of blocks available for writing */
extern int event_log_free_blocks_get(int set);
extern bool event_log_is_ready(void);
extern bool event_log_is_preserve_active(uint set);
extern uint event_log_get_percentage_available_space(uint set);
extern bool event_log_set_watermark_reached(int set_num);

extern void event_log_set_config(int set, uint32 period, uint16 watermark, uint32 config_flags);
#ifdef EVENTLOG_D3_PRESERVE
#define EVENT_LOG_PRESERVE_EXPAND_SIZE	5u
extern int event_log_preserve_set_shrink(osl_t *osh, int set_num);
extern void event_log_d3_preserve_active_set(osl_t* osh, int set, bool active);
extern void event_log_d3_prsv_set_all(osl_t *osh, bool active);
#endif /* EVENTLOG_D3_PRESERVE */

#ifdef EVENTLOG_PRSV_PERIODIC
#define EVENT_LOG_SET_SIZE_INVALID	0xFFFFFFFFu
#define EVENT_LOG_DEFAULT_PERIOD	3000u
extern void event_log_prsv_periodic_wd_trigger(osl_t *osh);
#endif /* EVENTLOG_PRSV_PERIODIC */

/* Enable/disable rate health check for a set */
#ifdef EVENT_LOG_RATE_HC
extern int event_log_enable_hc_for_set(int set_num, bool enable);
extern void event_log_set_hc_rate(uint16 num_prints);
extern uint16 event_log_get_hc_rate(void);
#endif /* EVENT_LOG_RATE_HC */

/* Configure a set with ability to send partial log blocks */
extern int event_log_send_partial_block_set(int set_num);

/* Get number of log blocks associated to a log set */
extern int event_log_num_blocks_get(int set, uint32 *num_blocks);

/* Get a log buffer of a desired set */
extern int event_log_block_get(int set, uint32 **buf, uint16 *len);
extern uint32 event_log_get_maxsets(void);

/* For all other non-logtrace consumers */
extern int event_log_set_is_valid(int set);

/* To be used by logtrace only */
extern int event_log_get_num_sets(void);

/* Given a buffer, return to which set it belongs to */
extern int event_log_get_set_for_buffer(const void *buf);

extern int event_log_flush_multiple_sets(const int *sets, uint16 num_sets);
extern int event_log_force_flush_preserve_all(void);
extern int event_log_get_iovar_handler(int set);
extern int event_log_enable_hostmem_access(bool hostmem_access_enabled);
extern int event_log_enable_event_trace(bool event_trace_enabled);
#endif /* EVENT_LOG_COMPILE */

#endif /* !EVENT_LOG_DUMPER && !DHD_EFI */

#endif /* BCMDRIVER */

#endif /* __ASSEMBLER__ */

#endif /* _EVENT_LOG_H_ */
