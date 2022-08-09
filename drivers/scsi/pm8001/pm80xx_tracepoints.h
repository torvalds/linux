/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace events in pm8001 driver.
 *
 * Copyright 2020 Google LLC
 * Author: Akshat Jain <akshatzen@google.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pm80xx

#if !defined(_TRACE_PM80XX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PM80XX_H

#include <linux/tracepoint.h>
#include "pm8001_sas.h"

TRACE_EVENT(pm80xx_request_issue,
	    TP_PROTO(u32 id, u32 phy_id, u32 htag, u32 ctlr_opcode,
		     u16 ata_opcode, int running_req),

	    TP_ARGS(id, phy_id, htag, ctlr_opcode, ata_opcode, running_req),

	    TP_STRUCT__entry(
		    __field(u32, id)
		    __field(u32, phy_id)
		    __field(u32, htag)
		    __field(u32, ctlr_opcode)
		    __field(u16,  ata_opcode)
		    __field(int, running_req)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->phy_id = phy_id;
		    __entry->htag = htag;
		    __entry->ctlr_opcode = ctlr_opcode;
		    __entry->ata_opcode = ata_opcode;
		    __entry->running_req = running_req;
		    ),

	    TP_printk("ctlr_id = %u phy_id = %u htag = %#x, ctlr_opcode = %#x ata_opcode = %#x running_req = %d",
		    __entry->id, __entry->phy_id, __entry->htag,
		    __entry->ctlr_opcode, __entry->ata_opcode,
		    __entry->running_req)
);

TRACE_EVENT(pm80xx_request_complete,
	    TP_PROTO(u32 id, u32 phy_id, u32 htag, u32 ctlr_opcode,
		     u16 ata_opcode, int running_req),

	    TP_ARGS(id, phy_id, htag, ctlr_opcode, ata_opcode, running_req),

	    TP_STRUCT__entry(
		    __field(u32, id)
		    __field(u32, phy_id)
		    __field(u32, htag)
		    __field(u32, ctlr_opcode)
		    __field(u16,  ata_opcode)
		    __field(int, running_req)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->phy_id = phy_id;
		    __entry->htag = htag;
		    __entry->ctlr_opcode = ctlr_opcode;
		    __entry->ata_opcode = ata_opcode;
		    __entry->running_req = running_req;
		    ),

	    TP_printk("ctlr_id = %u phy_id = %u htag = %#x, ctlr_opcode = %#x ata_opcode = %#x running_req = %d",
		    __entry->id, __entry->phy_id, __entry->htag,
		    __entry->ctlr_opcode, __entry->ata_opcode,
		    __entry->running_req)
);

TRACE_EVENT(pm80xx_mpi_build_cmd,
	    TP_PROTO(u32 id, u32 opc, u32 htag, u32 qi, u32 pi, u32 ci),

	    TP_ARGS(id, opc, htag, qi, pi, ci),

	    TP_STRUCT__entry(
		    __field(u32, id)
		    __field(u32, opc)
		    __field(u32, htag)
		    __field(u32, qi)
		    __field(u32, pi)
		    __field(u32, ci)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->opc = opc;
		    __entry->htag = htag;
		    __entry->qi = qi;
		    __entry->pi = pi;
		    __entry->ci = ci;
		    ),

	    TP_printk("ctlr_id = %u opc = %#x htag = %#x QI = %u PI = %u CI = %u",
		    __entry->id, __entry->opc, __entry->htag, __entry->qi,
		    __entry->pi, __entry->ci)
);

#endif /* _TRACE_PM80XX_H_ */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE pm80xx_tracepoints

#include <trace/define_trace.h>
