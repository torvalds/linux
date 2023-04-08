/*
 * EVENT_LOG system definitions
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: event_log.h 692339 2017-03-27 17:04:27Z $
 */

#ifndef _EVENT_LOG_H_
#define _EVENT_LOG_H_

#include <typedefs.h>
#include <event_log_set.h>
#include <event_log_tag.h>
#include <event_log_payload.h>
#include <osl_decl.h>

/* logstrs header */
#define LOGSTRS_MAGIC   0x4C4F4753
#define LOGSTRS_VERSION 0x1

/* We make sure that the block size will fit in a single packet
 *  (allowing for a bit of overhead on each packet
 */
#if defined(BCMPCIEDEV)
#define EVENT_LOG_MAX_BLOCK_SIZE	1648
#else
#define EVENT_LOG_MAX_BLOCK_SIZE	1400
#endif // endif
#define EVENT_LOG_WL_BLOCK_SIZE		0x200
#define EVENT_LOG_PSM_BLOCK_SIZE	0x200
#define EVENT_LOG_BUS_BLOCK_SIZE	0x200
#define EVENT_LOG_ERROR_BLOCK_SIZE	0x200
/* Maximum event log record payload size = 1016 bytes or 254 words. */
#define EVENT_LOG_MAX_RECORD_PAYLOAD_SIZE	254

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
	uint32 event_logs;
} event_log_block_t;
#define EVENT_LOG_BLOCK_HDRLEN		8 /* pktlen 2 + count 2 + extra_hdr_info 4 */

#define EVENT_LOG_BLOCK_LEN 12

typedef enum {
	SET_DESTINATION_INVALID = -1,
	SET_DESTINATION_HOST = 0,
	SET_DESTINATION_NONE = 1,
	SET_DESTINATION_MAX
} event_log_set_destination_t;

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
} event_log_set_t;

/* logstr_hdr_flags */
#define LOGSTRS_ENCRYPTED 0x1

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
	logstr_trailer_t trailer;
} logstr_header_t;

/* Ver 1 Header from logstrs.bin */
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

#if !defined(EVENT_LOG_DUMPER)

#ifndef EVENT_LOG_COMPILE

/* Null define if no tracing */
#define EVENT_LOG(format, ...)
#define EVENT_LOG_FAST(tag, fmt, ...)
#define EVENT_LOG_COMPACT(tag, fmt, ...)

#define EVENT_LOG_CAST(tag, fmt, ...)
#define EVENT_LOG_FAST_CAST(tag, fmt, ...)
#define EVENT_LOG_COMPACT_CAST(tag, fmt, ...)

#define EVENT_LOG_CAST_PAREN_ARGS(tag, pargs)
#define EVENT_LOG_FAST_CAST_PAREN_ARGS(tag, pargs)
#define EVENT_LOG_COMPACT_CAST_PAREN_ARGS(tag, pargs)

#define EVENT_LOG_IS_ON(tag)		0
#define EVENT_LOG_IS_LOG_ON(tag)	0

#define EVENT_LOG_BUFFER(tag, buf, size)

#else  /* EVENT_LOG_COMPILE */

/* The first few are special because they can be done more efficiently
 * this way and they are the common case.  Once there are too many
 * parameters the code size starts to be an issue and a loop is better
 */
#define _EVENT_LOG0(tag, fmt_num) 			\
	event_log0(tag, fmt_num)
#define _EVENT_LOG1(tag, fmt_num, t1) 			\
	event_log1(tag, fmt_num, t1)
#define _EVENT_LOG2(tag, fmt_num, t1, t2) 		\
	event_log2(tag, fmt_num, t1, t2)
#define _EVENT_LOG3(tag, fmt_num, t1, t2, t3) 		\
	event_log3(tag, fmt_num, t1, t2, t3)
#define _EVENT_LOG4(tag, fmt_num, t1, t2, t3, t4) 	\
	event_log4(tag, fmt_num, t1, t2, t3, t4)

/* The rest call the generic routine that takes a count */
#define _EVENT_LOG5(tag, fmt_num, ...) event_logn(5, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG6(tag, fmt_num, ...) event_logn(6, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG7(tag, fmt_num, ...) event_logn(7, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG8(tag, fmt_num, ...) event_logn(8, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOG9(tag, fmt_num, ...) event_logn(9, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGA(tag, fmt_num, ...) event_logn(10, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGB(tag, fmt_num, ...) event_logn(11, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGC(tag, fmt_num, ...) event_logn(12, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGD(tag, fmt_num, ...) event_logn(13, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGE(tag, fmt_num, ...) event_logn(14, tag, fmt_num, __VA_ARGS__)
#define _EVENT_LOGF(tag, fmt_num, ...) event_logn(15, tag, fmt_num, __VA_ARGS__)

/* Casting  low level macros */
#define _EVENT_LOG_CAST0(tag, fmt_num)			\
	event_log0(tag, fmt_num)
#define _EVENT_LOG_CAST1(tag, fmt_num, t1)		\
	event_log1(tag, fmt_num, (uint32)(t1))
#define _EVENT_LOG_CAST2(tag, fmt_num, t1, t2)		\
	event_log2(tag, fmt_num, (uint32)(t1), (uint32)(t2))
#define _EVENT_LOG_CAST3(tag, fmt_num, t1, t2, t3)	\
	event_log3(tag, fmt_num, (uint32)(t1), (uint32)(t2), (uint32)(t3))
#define _EVENT_LOG_CAST4(tag, fmt_num, t1, t2, t3, t4)	\
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
 */

#define _EVENT_LOG_VA_NUM_ARGS(F, _1, _2, _3, _4, _5, _6, _7, _8, _9,	\
			       _A, _B, _C, _D, _E, _F, N, ...) F ## N

/* cast = _EVENT_LOG for no casting
 * cast = _EVENT_LOG_CAST for casting of fmt arguments to uint32.
 *        Only first 4 arguments are casted to uint32. event_logn() is called
 *        if more than 4 arguments are present. This function internally assumes
 *        all arguments are uint32
 */
#define _EVENT_LOG(cast, tag, fmt, ...)					\
	static char logstr[] __attribute__ ((section(".logstrs"))) = fmt; \
	static uint32 fmtnum __attribute__ ((section(".lognums"))) = (uint32) &logstr; \
	_EVENT_LOG_VA_NUM_ARGS(cast, ##__VA_ARGS__,			\
			       F, E, D, C, B, A, 9, 8,			\
			       7, 6, 5, 4, 3, 2, 1, 0)			\
	(tag, (int) &fmtnum , ## __VA_ARGS__)

#define EVENT_LOG_FAST(tag, fmt, ...)					\
	do {								\
		if (event_log_tag_sets != NULL) {			\
			uint8 tag_flag = *(event_log_tag_sets + tag);	\
			if ((tag_flag & ~EVENT_LOG_TAG_FLAG_SET_MASK) != 0) {		\
				_EVENT_LOG(_EVENT_LOG, tag, fmt , ## __VA_ARGS__);	\
			}						\
		}							\
	} while (0)

#define EVENT_LOG_COMPACT(tag, fmt, ...)				\
	do {								\
		_EVENT_LOG(_EVENT_LOG, tag, fmt , ## __VA_ARGS__);	\
	} while (0)

/* Event log macro with casting to uint32 of arguments */
#define EVENT_LOG_FAST_CAST(tag, fmt, ...)				\
	do {								\
		if (event_log_tag_sets != NULL) {			\
			uint8 tag_flag = *(event_log_tag_sets + tag);	\
			if ((tag_flag & ~EVENT_LOG_TAG_FLAG_SET_MASK) != 0) {		\
				_EVENT_LOG(_EVENT_LOG_CAST, tag, fmt , ## __VA_ARGS__);	\
			}						\
		}							\
	} while (0)

#define EVENT_LOG_COMPACT_CAST(tag, fmt, ...)				\
	do {								\
		_EVENT_LOG(_EVENT_LOG_CAST, tag, fmt , ## __VA_ARGS__);	\
	} while (0)

#define EVENT_LOG(tag, fmt, ...) EVENT_LOG_COMPACT(tag, fmt , ## __VA_ARGS__)

#define EVENT_LOG_CAST(tag, fmt, ...) EVENT_LOG_COMPACT_CAST(tag, fmt , ## __VA_ARGS__)

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

#define EVENT_LOG_IS_ON(tag) (*(event_log_tag_sets + (tag)) & ~EVENT_LOG_TAG_FLAG_SET_MASK)
#define EVENT_LOG_IS_LOG_ON(tag) (*(event_log_tag_sets + (tag)) & EVENT_LOG_TAG_FLAG_LOG)

#define EVENT_LOG_BUFFER(tag, buf, size)	event_log_buffer(tag, buf, size)
#define EVENT_DUMP	event_log_buffer

extern uint8 *event_log_tag_sets;

extern int event_log_init(osl_t *osh);
extern int event_log_set_init(osl_t *osh, int set_num, int size);
extern int event_log_set_expand(osl_t *osh, int set_num, int size);
extern int event_log_set_shrink(osl_t *osh, int set_num, int size);

extern int event_log_tag_start(int tag, int set_num, int flags);
extern int event_log_tag_set_retrieve(int tag);
extern int event_log_tag_stop(int tag);

typedef void (*event_log_logtrace_trigger_fn_t)(void *ctx);
void event_log_set_logtrace_trigger_fn(event_log_logtrace_trigger_fn_t fn, void *ctx);

event_log_top_t *event_log_get_top(void);

extern int event_log_get(int set_num, int buflen, void *buf);

extern uint8 *event_log_next_logtrace(int set_num);

extern void event_log0(int tag, int fmtNum);
extern void event_log1(int tag, int fmtNum, uint32 t1);
extern void event_log2(int tag, int fmtNum, uint32 t1, uint32 t2);
extern void event_log3(int tag, int fmtNum, uint32 t1, uint32 t2, uint32 t3);
extern void event_log4(int tag, int fmtNum, uint32 t1, uint32 t2, uint32 t3, uint32 t4);
extern void event_logn(int num_args, int tag, int fmtNum, ...);

extern void event_log_time_sync(uint32 ms);
extern void event_log_buffer(int tag, uint8 *buf, int size);
extern void event_log_caller_return_address(int tag);
extern int event_log_set_destination_set(int set, event_log_set_destination_t dest);
extern event_log_set_destination_t event_log_set_destination_get(int set);
extern int event_log_flush_log_buffer(int set);
extern uint16 event_log_get_available_space(int set);
extern bool event_log_is_set_configured(int set_num);
extern bool event_log_is_tag_valid(int tag);
/* returns number of blocks available for writing */
extern int event_log_free_blocks_get(int set);
extern bool event_log_is_ready(void);

#endif /* EVENT_LOG_DUMPER */

#endif /* EVENT_LOG_COMPILE */

#endif /* __ASSEMBLER__ */

#endif /* _EVENT_LOG_H_ */
