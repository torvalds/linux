/*
 * Trace messages sent over HBUS
 *
 * Copyright (C) 1999-2013, Broadcom Corporation
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
 * $Id: msgtrace.h 369735 2012-11-19 22:50:22Z $
 */

#ifndef	_MSGTRACE_H
#define	_MSGTRACE_H

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif


/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define MSGTRACE_VERSION 1

/* Message trace header */
typedef BWL_PRE_PACKED_STRUCT struct msgtrace_hdr {
	uint8	version;
	uint8   trace_type;
#define MSGTRACE_HDR_TYPE_MSG 0
#define MSGTRACE_HDR_TYPE_LOG 1
	uint16	len;	/* Len of the trace */
	uint32	seqnum;	/* Sequence number of message. Useful if the messsage has been lost
			 * because of DMA error or a bus reset (ex: SDIO Func2)
			 */
	/* Msgtrace type  only */
	uint32  discarded_bytes;  /* Number of discarded bytes because of trace overflow  */
	uint32  discarded_printf; /* Number of discarded printf because of trace overflow */
} BWL_POST_PACKED_STRUCT msgtrace_hdr_t;

#define MSGTRACE_HDRLEN 	sizeof(msgtrace_hdr_t)

/* The hbus driver generates traces when sending a trace message. This causes endless traces.
 * This flag must be set to TRUE in any hbus traces. The flag is reset in the function msgtrace_put.
 * This prevents endless traces but generates hasardous lost of traces only in bus device code.
 * It is recommendat to set this flag in macro SD_TRACE but not in SD_ERROR for avoiding missing
 * hbus error traces. hbus error trace should not generates endless traces.
 */
extern bool msgtrace_hbus_trace;

typedef void (*msgtrace_func_send_t)(void *hdl1, void *hdl2, uint8 *hdr,
                                     uint16 hdrlen, uint8 *buf, uint16 buflen);
extern void msgtrace_start(void);
extern void msgtrace_stop(void);
extern int msgtrace_sent(void);
extern void msgtrace_put(char *buf, int count);
extern void msgtrace_init(void *hdl1, void *hdl2, msgtrace_func_send_t func_send);
extern bool msgtrace_event_enabled(void);

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif	/* _MSGTRACE_H */
