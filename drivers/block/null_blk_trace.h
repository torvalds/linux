/* SPDX-License-Identifier: GPL-2.0 */
/*
 * null_blk device driver tracepoints.
 *
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM nullb

#if !defined(_TRACE_NULLB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NULLB_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "null_blk.h"

const char *nullb_trace_disk_name(struct trace_seq *p, char *name);

#define __print_disk_name(name) nullb_trace_disk_name(p, name)

#ifndef TRACE_HEADER_MULTI_READ
static inline void __assign_disk_name(char *name, struct gendisk *disk)
{
	if (disk)
		memcpy(name, disk->disk_name, DISK_NAME_LEN);
	else
		memset(name, 0, DISK_NAME_LEN);
}
#endif

TRACE_EVENT(nullb_zone_op,
	    TP_PROTO(struct nullb_cmd *cmd, unsigned int zone_no,
		     unsigned int zone_cond),
	    TP_ARGS(cmd, zone_no, zone_cond),
	    TP_STRUCT__entry(
		__array(char, disk, DISK_NAME_LEN)
		__field(enum req_opf, op)
		__field(unsigned int, zone_no)
		__field(unsigned int, zone_cond)
	    ),
	    TP_fast_assign(
		__entry->op = req_op(cmd->rq);
		__entry->zone_no = zone_no;
		__entry->zone_cond = zone_cond;
		__assign_disk_name(__entry->disk, cmd->rq->rq_disk);
	    ),
	    TP_printk("%s req=%-15s zone_no=%u zone_cond=%-10s",
		      __print_disk_name(__entry->disk),
		      blk_op_str(__entry->op),
		      __entry->zone_no,
		      blk_zone_cond_str(__entry->zone_cond))
);

TRACE_EVENT(nullb_report_zones,
	    TP_PROTO(struct nullb *nullb, unsigned int nr_zones),
	    TP_ARGS(nullb, nr_zones),
	    TP_STRUCT__entry(
		__array(char, disk, DISK_NAME_LEN)
		__field(unsigned int, nr_zones)
	    ),
	    TP_fast_assign(
		__entry->nr_zones = nr_zones;
		__assign_disk_name(__entry->disk, nullb->disk);
	    ),
	    TP_printk("%s nr_zones=%u",
		      __print_disk_name(__entry->disk), __entry->nr_zones)
);

#endif /* _TRACE_NULLB_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE null_blk_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
