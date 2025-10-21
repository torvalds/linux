/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoint definitions for s390 ap bus related trace events
 *
 * There are two AP bus related tracepoint events defined here:
 * There is a tracepoint s390_ap_nqap event immediately after a request
 * has been pushed into the AP firmware queue with the NQAP AP command.
 * The other tracepoint s390_ap_dqap event fires immediately after a
 * reply has been pulled out of the AP firmware queue via DQAP AP command.
 * The idea of these two trace events focuses on performance to measure
 * the runtime of a crypto request/reply as close as possible at the
 * firmware level. In combination with the two zcrypt tracepoints (see the
 * zcrypt.h trace event definition file) this gives measurement data about
 * the runtime of a request/reply within the zcrpyt and AP bus layer.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s390

#if !defined(_TRACE_S390_AP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_S390_AP_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(s390_ap_nqapdqap_template,
		    TP_PROTO(u16 card, u16 dom, u32 status, u64 psmid),
		    TP_ARGS(card, dom, status, psmid),
		    TP_STRUCT__entry(
			    __field(u16, card)
			    __field(u16, dom)
			    __field(u32, status)
			    __field(u64, psmid)),
		    TP_fast_assign(
			    __entry->card = card;
			    __entry->dom = dom;
			    __entry->status = status;
			    __entry->psmid = psmid;),
		    TP_printk("card=%u dom=%u status=0x%08x psmid=0x%016lx",
			      (unsigned short)__entry->card,
			      (unsigned short)__entry->dom,
			      (unsigned int)__entry->status,
			      (unsigned long)__entry->psmid)
);

/**
 * trace_s390_ap_nqap - ap msg nqap tracepoint function
 * @card:   Crypto card number addressed.
 * @dom:    Domain within the crypto card addressed.
 * @status: AP queue status (GR1 on return of nqap).
 * @psmid:  Unique id identifying this request/reply.
 *
 * Called immediately after a request has been enqueued into
 * the AP firmware queue with the NQAP command.
 */
DEFINE_EVENT(s390_ap_nqapdqap_template,
	     s390_ap_nqap,
	     TP_PROTO(u16 card, u16 dom, u32 status, u64 psmid),
	     TP_ARGS(card, dom, status, psmid)
);

/**
 * trace_s390_ap_dqap - ap msg dqap tracepoint function
 * @card:  Crypto card number addressed.
 * @dom:   Domain within the crypto card addressed.
 * @status: AP queue status (GR1 on return of dqap).
 * @psmid: Unique id identifying this request/reply.
 *
 * Called immediately after a reply has been dequeued from
 * the AP firmware queue with the DQAP command.
 */
DEFINE_EVENT(s390_ap_nqapdqap_template,
	     s390_ap_dqap,
	     TP_PROTO(u16 card, u16 dom, u32 status, u64 psmid),
	     TP_ARGS(card, dom, status, psmid)
);

#endif /* _TRACE_S390_AP_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm/trace
#define TRACE_INCLUDE_FILE ap

#include <trace/define_trace.h>
