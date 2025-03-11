/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trace.h - USB UVC Gadget Trace Support
 *
 * Copyright (C) 2024 Pengutronix e.K.
 *
 * Author: Michael Grzeschik <m.grzeschik@pengutronix.de>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM uvcg

#if !defined(__UVCG_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __UVCG_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/usb/gadget.h>
#include <asm/byteorder.h>

DECLARE_EVENT_CLASS(uvcg_video_req,
	TP_PROTO(struct usb_request *req, u32 queued),
	TP_ARGS(req, queued),
	TP_STRUCT__entry(
		__field(struct usb_request *, req)
		__field(u32, length)
		__field(u32, queued)
	),
	TP_fast_assign(
		__entry->req = req;
		__entry->length = req->length;
		__entry->queued = queued;
	),
	TP_printk("req %p length %u queued %u",
		__entry->req,
		__entry->length,
		__entry->queued)
);

DEFINE_EVENT(uvcg_video_req, uvcg_video_complete,
	TP_PROTO(struct usb_request *req, u32 queued),
	TP_ARGS(req, queued)
);

DEFINE_EVENT(uvcg_video_req, uvcg_video_queue,
	TP_PROTO(struct usb_request *req, u32 queued),
	TP_ARGS(req, queued)
);

#endif /* __UVCG_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE uvc_trace

#include <trace/define_trace.h>
