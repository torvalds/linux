/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoint header for the s390 Common I/O layer (CIO)
 *
 * Copyright IBM Corp. 2015
 * Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 */

#include <linux/kernel.h>
#include <asm/crw.h>
#include <uapi/asm/chpid.h>
#include <uapi/asm/schid.h>
#include "cio.h"
#include "orb.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s390

#if !defined(_TRACE_S390_CIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_S390_CIO_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(s390_class_schib,
	TP_PROTO(struct subchannel_id schid, struct schib *schib, int cc),
	TP_ARGS(schid, schib, cc),
	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field(u16, devno)
		__field_struct(struct schib, schib)
		__field(int, cc)
	),
	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schno = schid.sch_no;
		__entry->devno = schib->pmcw.dev;
		__entry->schib = *schib;
		__entry->cc = cc;
	),
	TP_printk("schid=%x.%x.%04x cc=%d ena=%d st=%d dnv=%d dev=%04x "
		  "lpm=0x%02x pnom=0x%02x lpum=0x%02x pim=0x%02x pam=0x%02x "
		  "pom=0x%02x chpids=%016llx",
		  __entry->cssid, __entry->ssid, __entry->schno, __entry->cc,
		  __entry->schib.pmcw.ena, __entry->schib.pmcw.st,
		  __entry->schib.pmcw.dnv, __entry->schib.pmcw.dev,
		  __entry->schib.pmcw.lpm, __entry->schib.pmcw.pnom,
		  __entry->schib.pmcw.lpum, __entry->schib.pmcw.pim,
		  __entry->schib.pmcw.pam, __entry->schib.pmcw.pom,
		  *((u64 *) __entry->schib.pmcw.chpid)
	)
);

/**
 * s390_cio_stsch -  Store Subchannel instruction (STSCH) was performed
 * @schid: Subchannel ID
 * @schib: Subchannel-Information block
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schib, s390_cio_stsch,
	TP_PROTO(struct subchannel_id schid, struct schib *schib, int cc),
	TP_ARGS(schid, schib, cc)
);

/**
 * s390_cio_msch -  Modify Subchannel instruction (MSCH) was performed
 * @schid: Subchannel ID
 * @schib: Subchannel-Information block
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schib, s390_cio_msch,
	TP_PROTO(struct subchannel_id schid, struct schib *schib, int cc),
	TP_ARGS(schid, schib, cc)
);

/**
 * s390_cio_tsch - Test Subchannel instruction (TSCH) was performed
 * @schid: Subchannel ID
 * @irb: Interruption-Response Block
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_tsch,
	TP_PROTO(struct subchannel_id schid, struct irb *irb, int cc),
	TP_ARGS(schid, irb, cc),
	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field_struct(struct irb, irb)
		__field(u8, scsw_dcc)
		__field(u8, scsw_pno)
		__field(u8, scsw_fctl)
		__field(u8, scsw_actl)
		__field(u8, scsw_stctl)
		__field(u8, scsw_dstat)
		__field(u8, scsw_cstat)
		__field(int, cc)
	),
	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schno = schid.sch_no;
		__entry->irb = *irb;
		__entry->scsw_dcc = scsw_cc(&irb->scsw);
		__entry->scsw_pno = scsw_pno(&irb->scsw);
		__entry->scsw_fctl = scsw_fctl(&irb->scsw);
		__entry->scsw_actl = scsw_actl(&irb->scsw);
		__entry->scsw_stctl = scsw_stctl(&irb->scsw);
		__entry->scsw_dstat = scsw_dstat(&irb->scsw);
		__entry->scsw_cstat = scsw_cstat(&irb->scsw);
		__entry->cc = cc;
	),
	TP_printk("schid=%x.%x.%04x cc=%d dcc=%d pno=%d fctl=0x%x actl=0x%x "
		  "stctl=0x%x dstat=0x%x cstat=0x%x",
		  __entry->cssid, __entry->ssid, __entry->schno, __entry->cc,
		  __entry->scsw_dcc, __entry->scsw_pno,
		  __entry->scsw_fctl, __entry->scsw_actl,
		  __entry->scsw_stctl,
		  __entry->scsw_dstat, __entry->scsw_cstat
	)
);

/**
 * s390_cio_tpi - Test Pending Interruption instruction (TPI) was performed
 * @addr: Address of the I/O interruption code or %NULL
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_tpi,
	TP_PROTO(struct tpi_info *addr, int cc),
	TP_ARGS(addr, cc),
	TP_STRUCT__entry(
		__field(int, cc)
		__field_struct(struct tpi_info, tpi_info)
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field(u8, adapter_IO)
		__field(u8, isc)
		__field(u8, type)
	),
	TP_fast_assign(
		__entry->cc = cc;
		if (cc != 0)
			memset(&__entry->tpi_info, 0, sizeof(struct tpi_info));
		else if (addr)
			__entry->tpi_info = *addr;
		else {
			memcpy(&__entry->tpi_info, &S390_lowcore.subchannel_id,
			       sizeof(struct tpi_info));
		}
		__entry->cssid = __entry->tpi_info.schid.cssid;
		__entry->ssid = __entry->tpi_info.schid.ssid;
		__entry->schno = __entry->tpi_info.schid.sch_no;
		__entry->adapter_IO = __entry->tpi_info.adapter_IO;
		__entry->isc = __entry->tpi_info.isc;
		__entry->type = __entry->tpi_info.type;
	),
	TP_printk("schid=%x.%x.%04x cc=%d a=%d isc=%d type=%d",
		  __entry->cssid, __entry->ssid, __entry->schno, __entry->cc,
		  __entry->adapter_IO, __entry->isc,
		  __entry->type
	)
);

/**
 * s390_cio_ssch - Start Subchannel instruction (SSCH) was performed
 * @schid: Subchannel ID
 * @orb: Operation-Request Block
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_ssch,
	TP_PROTO(struct subchannel_id schid, union orb *orb, int cc),
	TP_ARGS(schid, orb, cc),
	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field_struct(union orb, orb)
		__field(int, cc)
	),
	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schno = schid.sch_no;
		__entry->orb = *orb;
		__entry->cc = cc;
	),
	TP_printk("schid=%x.%x.%04x cc=%d", __entry->cssid, __entry->ssid,
		  __entry->schno, __entry->cc
	)
);

DECLARE_EVENT_CLASS(s390_class_schid,
	TP_PROTO(struct subchannel_id schid, int cc),
	TP_ARGS(schid, cc),
	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field(int, cc)
	),
	TP_fast_assign(
		__entry->cssid = schid.cssid;
		__entry->ssid = schid.ssid;
		__entry->schno = schid.sch_no;
		__entry->cc = cc;
	),
	TP_printk("schid=%x.%x.%04x cc=%d", __entry->cssid, __entry->ssid,
		  __entry->schno, __entry->cc
	)
);

/**
 * s390_cio_csch - Clear Subchannel instruction (CSCH) was performed
 * @schid: Subchannel ID
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schid, s390_cio_csch,
	TP_PROTO(struct subchannel_id schid, int cc),
	TP_ARGS(schid, cc)
);

/**
 * s390_cio_hsch - Halt Subchannel instruction (HSCH) was performed
 * @schid: Subchannel ID
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schid, s390_cio_hsch,
	TP_PROTO(struct subchannel_id schid, int cc),
	TP_ARGS(schid, cc)
);

/**
 * s390_cio_xsch - Cancel Subchannel instruction (XSCH) was performed
 * @schid: Subchannel ID
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schid, s390_cio_xsch,
	TP_PROTO(struct subchannel_id schid, int cc),
	TP_ARGS(schid, cc)
);

/**
 * s390_cio_rsch - Resume Subchannel instruction (RSCH) was performed
 * @schid: Subchannel ID
 * @cc: Condition code
 */
DEFINE_EVENT(s390_class_schid, s390_cio_rsch,
	TP_PROTO(struct subchannel_id schid, int cc),
	TP_ARGS(schid, cc)
);

/**
 * s390_cio_rchp - Reset Channel Path (RCHP) instruction was performed
 * @chpid: Channel-Path Identifier
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_rchp,
	TP_PROTO(struct chp_id chpid, int cc),
	TP_ARGS(chpid, cc),
	TP_STRUCT__entry(
		__field(u8, cssid)
		__field(u8, id)
		__field(int, cc)
	),
	TP_fast_assign(
		__entry->cssid = chpid.cssid;
		__entry->id = chpid.id;
		__entry->cc = cc;
	),
	TP_printk("chpid=%x.%02x cc=%d", __entry->cssid, __entry->id,
		  __entry->cc
	)
);

#define CHSC_MAX_REQUEST_LEN		64
#define CHSC_MAX_RESPONSE_LEN		64

/**
 * s390_cio_chsc - Channel Subsystem Call (CHSC) instruction was performed
 * @chsc: CHSC block
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_chsc,
	TP_PROTO(struct chsc_header *chsc, int cc),
	TP_ARGS(chsc, cc),
	TP_STRUCT__entry(
		__field(int, cc)
		__field(u16, code)
		__field(u16, rcode)
		__array(u8, request, CHSC_MAX_REQUEST_LEN)
		__array(u8, response, CHSC_MAX_RESPONSE_LEN)
	),
	TP_fast_assign(
		__entry->cc = cc;
		__entry->code = chsc->code;
		memcpy(&entry->request, chsc,
		       min_t(u16, chsc->length, CHSC_MAX_REQUEST_LEN));
		chsc = (struct chsc_header *) ((char *) chsc + chsc->length);
		__entry->rcode = chsc->code;
		memcpy(&entry->response, chsc,
		       min_t(u16, chsc->length, CHSC_MAX_RESPONSE_LEN));
	),
	TP_printk("code=0x%04x cc=%d rcode=0x%04x", __entry->code,
		  __entry->cc, __entry->rcode)
);

/**
 * s390_cio_interrupt - An I/O interrupt occurred
 * @tpi_info: Address of the I/O interruption code
 */
TRACE_EVENT(s390_cio_interrupt,
	TP_PROTO(struct tpi_info *tpi_info),
	TP_ARGS(tpi_info),
	TP_STRUCT__entry(
		__field_struct(struct tpi_info, tpi_info)
		__field(u8, cssid)
		__field(u8, ssid)
		__field(u16, schno)
		__field(u8, isc)
		__field(u8, type)
	),
	TP_fast_assign(
		__entry->tpi_info = *tpi_info;
		__entry->cssid = tpi_info->schid.cssid;
		__entry->ssid = tpi_info->schid.ssid;
		__entry->schno = tpi_info->schid.sch_no;
		__entry->isc = tpi_info->isc;
		__entry->type = tpi_info->type;
	),
	TP_printk("schid=%x.%x.%04x isc=%d type=%d",
		  __entry->cssid, __entry->ssid, __entry->schno,
		  __entry->isc, __entry->type
	)
);

/**
 * s390_cio_adapter_int - An adapter interrupt occurred
 * @tpi_info: Address of the I/O interruption code
 */
TRACE_EVENT(s390_cio_adapter_int,
	TP_PROTO(struct tpi_info *tpi_info),
	TP_ARGS(tpi_info),
	TP_STRUCT__entry(
		__field_struct(struct tpi_info, tpi_info)
		__field(u8, isc)
	),
	TP_fast_assign(
		__entry->tpi_info = *tpi_info;
		__entry->isc = tpi_info->isc;
	),
	TP_printk("isc=%d", __entry->isc)
);

/**
 * s390_cio_stcrw - Store Channel Report Word (STCRW) was performed
 * @crw: Channel Report Word
 * @cc: Condition code
 */
TRACE_EVENT(s390_cio_stcrw,
	TP_PROTO(struct crw *crw, int cc),
	TP_ARGS(crw, cc),
	TP_STRUCT__entry(
		__field_struct(struct crw, crw)
		__field(int, cc)
		__field(u8, slct)
		__field(u8, oflw)
		__field(u8, chn)
		__field(u8, rsc)
		__field(u8, anc)
		__field(u8, erc)
		__field(u16, rsid)
	),
	TP_fast_assign(
		__entry->crw = *crw;
		__entry->cc = cc;
		__entry->slct = crw->slct;
		__entry->oflw = crw->oflw;
		__entry->chn = crw->chn;
		__entry->rsc = crw->rsc;
		__entry->anc = crw->anc;
		__entry->erc = crw->erc;
		__entry->rsid = crw->rsid;
	),
	TP_printk("cc=%d slct=%d oflw=%d chn=%d rsc=%d anc=%d erc=0x%x "
		  "rsid=0x%x",
		  __entry->cc, __entry->slct, __entry->oflw,
		  __entry->chn, __entry->rsc,  __entry->anc,
		  __entry->erc, __entry->rsid
	)
);

#endif /* _TRACE_S390_CIO_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
