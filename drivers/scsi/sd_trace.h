/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Western Digital Corporation or its affiliates.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sd

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE sd_trace

#if !defined(_SD_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/tracepoint.h>

TRACE_EVENT(scsi_prepare_zone_append,

	    TP_PROTO(struct scsi_cmnd *cmnd, sector_t lba,
		     unsigned int wp_offset),

	    TP_ARGS(cmnd, lba, wp_offset),

	    TP_STRUCT__entry(
		     __field( unsigned int, host_no )
		     __field( unsigned int, channel )
		     __field( unsigned int, id )
		     __field( unsigned int, lun )
		     __field( sector_t,     lba )
		     __field( unsigned int, wp_offset )
	    ),

	    TP_fast_assign(
		__entry->host_no	= cmnd->device->host->host_no;
		__entry->channel	= cmnd->device->channel;
		__entry->id		= cmnd->device->id;
		__entry->lun		= cmnd->device->lun;
		__entry->lba		= lba;
		__entry->wp_offset	= wp_offset;
	    ),

	    TP_printk("host_no=%u, channel=%u id=%u lun=%u lba=%llu wp_offset=%u",
		      __entry->host_no, __entry->channel, __entry->id,
		      __entry->lun, __entry->lba, __entry->wp_offset)
);

TRACE_EVENT(scsi_zone_wp_update,

	    TP_PROTO(struct scsi_cmnd *cmnd, sector_t rq_sector,
		     unsigned int wp_offset, unsigned int good_bytes),

	    TP_ARGS(cmnd, rq_sector, wp_offset, good_bytes),

	    TP_STRUCT__entry(
		     __field( unsigned int, host_no )
		     __field( unsigned int, channel )
		     __field( unsigned int, id )
		     __field( unsigned int, lun )
		     __field( sector_t,     rq_sector )
		     __field( unsigned int, wp_offset )
		     __field( unsigned int, good_bytes )
	    ),

	    TP_fast_assign(
		__entry->host_no	= cmnd->device->host->host_no;
		__entry->channel	= cmnd->device->channel;
		__entry->id		= cmnd->device->id;
		__entry->lun		= cmnd->device->lun;
		__entry->rq_sector	= rq_sector;
		__entry->wp_offset	= wp_offset;
		__entry->good_bytes	= good_bytes;
	    ),

	    TP_printk("host_no=%u, channel=%u id=%u lun=%u rq_sector=%llu" \
		      " wp_offset=%u good_bytes=%u",
		      __entry->host_no, __entry->channel, __entry->id,
		      __entry->lun, __entry->rq_sector, __entry->wp_offset,
		      __entry->good_bytes)
);
#endif /* _SD_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/scsi
#include <trace/define_trace.h>
