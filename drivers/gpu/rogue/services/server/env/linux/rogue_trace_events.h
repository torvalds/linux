/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rogue

#if !defined(_ROGUE_TRACE_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _ROGUE_TRACE_EVENTS_H

#include <linux/tracepoint.h>
#include <linux/time.h>

TRACE_EVENT(rogue_fence_update,

	TP_PROTO(const char *comm, const char *dm, u32 fw_ctx, u32 offset,
		u32 sync_fwaddr, u32 sync_value),

	TP_ARGS(comm, dm, fw_ctx, offset, sync_fwaddr, sync_value),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       dm,             dm              )
		__field(        u32,            fw_ctx          )
		__field(        u32,            offset          )
		__field(        u32,            sync_fwaddr     )
		__field(        u32,            sync_value      )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(dm, dm);
		__entry->fw_ctx = fw_ctx;
		__entry->offset = offset;
		__entry->sync_fwaddr = sync_fwaddr;
		__entry->sync_value = sync_value;
	),

	TP_printk("comm=%s dm=%s fw_ctx=%lx offset=%lu sync_fwaddr=%lx sync_value=%lx",
		__get_str(comm),
		__get_str(dm),
		(unsigned long)__entry->fw_ctx,
		(unsigned long)__entry->offset,
		(unsigned long)__entry->sync_fwaddr,
		(unsigned long)__entry->sync_value)
);

TRACE_EVENT(rogue_fence_check,

	TP_PROTO(const char *comm, const char *dm, u32 fw_ctx, u32 offset,
		u32 sync_fwaddr, u32 sync_value),

	TP_ARGS(comm, dm, fw_ctx, offset, sync_fwaddr, sync_value),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       dm,             dm              )
		__field(        u32,            fw_ctx          )
		__field(        u32,            offset          )
		__field(        u32,            sync_fwaddr     )
		__field(        u32,            sync_value      )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(dm, dm);
		__entry->fw_ctx = fw_ctx;
		__entry->offset = offset;
		__entry->sync_fwaddr = sync_fwaddr;
		__entry->sync_value = sync_value;
	),

	TP_printk("comm=%s dm=%s fw_ctx=%lx offset=%lu sync_fwaddr=%lx sync_value=%lx",
		__get_str(comm),
		__get_str(dm),
		(unsigned long)__entry->fw_ctx,
		(unsigned long)__entry->offset,
		(unsigned long)__entry->sync_fwaddr,
		(unsigned long)__entry->sync_value)
);

TRACE_EVENT(rogue_create_fw_context,

	TP_PROTO(const char *comm, const char *dm, u32 fw_ctx),

	TP_ARGS(comm, dm, fw_ctx),

	TP_STRUCT__entry(
		__string(       comm,           comm            )
		__string(       dm,             dm              )
		__field(        u32,            fw_ctx          )
	),

	TP_fast_assign(
		__assign_str(comm, comm);
		__assign_str(dm, dm);
		__entry->fw_ctx = fw_ctx;
	),

	TP_printk("comm=%s dm=%s fw_ctx=%lx",
		__get_str(comm),
		__get_str(dm),
		(unsigned long)__entry->fw_ctx)
);

#endif /* _ROGUE_TRACE_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .

/* This is needed because the name of this file doesn't match TRACE_SYSTEM. */
#define TRACE_INCLUDE_FILE rogue_trace_events

/* This part must be outside protection */
#include <trace/define_trace.h>
