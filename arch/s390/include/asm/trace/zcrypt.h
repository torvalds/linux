/*
 * Tracepoint definitions for the s390 zcrypt device driver
 *
 * Copyright IBM Corp. 2016
 * Author(s): Harald Freudenberger <freude@de.ibm.com>
 *
 * Currently there are two tracepoint events defined here.
 * An s390_zcrypt_req request event occurs as soon as the request is
 * recognized by the zcrypt ioctl function. This event may act as some kind
 * of request-processing-starts-now indication.
 * As late as possible within the zcrypt ioctl function there occurs the
 * s390_zcrypt_rep event which may act as the point in time where the
 * request has been processed by the kernel and the result is about to be
 * transferred back to userspace.
 * The glue which binds together request and reply event is the ptr
 * parameter, which is the local buffer address where the request from
 * userspace has been stored by the ioctl function.
 *
 * The main purpose of this zcrypt tracepoint api is to get some data for
 * performance measurements together with information about on which card
 * and queue the request has been processed. It is not an ffdc interface as
 * there is already code in the zcrypt device driver to serve the s390
 * debug feature interface.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s390

#if !defined(_TRACE_S390_ZCRYPT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_S390_ZCRYPT_H

#include <linux/tracepoint.h>

#define TP_ICARSAMODEXPO  0x0001
#define TP_ICARSACRT	  0x0002
#define TB_ZSECSENDCPRB   0x0003
#define TP_ZSENDEP11CPRB  0x0004
#define TP_HWRNGCPRB	  0x0005

#define show_zcrypt_tp_type(type)				\
	__print_symbolic(type,					\
			 { TP_ICARSAMODEXPO, "ICARSAMODEXPO" }, \
			 { TP_ICARSACRT, "ICARSACRT" },		\
			 { TB_ZSECSENDCPRB, "ZSECSENDCPRB" },	\
			 { TP_ZSENDEP11CPRB, "ZSENDEP11CPRB" }, \
			 { TP_HWRNGCPRB, "HWRNGCPRB" })

/**
 * trace_s390_zcrypt_req - zcrypt request tracepoint function
 * @ptr:  Address of the local buffer where the request from userspace
 *	  is stored. Can be used as a unique id to relate together
 *	  request and reply.
 * @type: One of the TP_ defines above.
 *
 * Called when a request from userspace is recognised within the ioctl
 * function of the zcrypt device driver and may act as an entry
 * timestamp.
 */
TRACE_EVENT(s390_zcrypt_req,
	    TP_PROTO(void *ptr, u32 type),
	    TP_ARGS(ptr, type),
	    TP_STRUCT__entry(
		    __field(void *, ptr)
		    __field(u32, type)),
	    TP_fast_assign(
		    __entry->ptr = ptr;
		    __entry->type = type;),
	    TP_printk("ptr=%p type=%s",
		      __entry->ptr,
		      show_zcrypt_tp_type(__entry->type))
);

/**
 * trace_s390_zcrypt_rep - zcrypt reply tracepoint function
 * @ptr:  Address of the local buffer where the request from userspace
 *	  is stored. Can be used as a unique id to match together
 *	  request and reply.
 * @fc:   Function code.
 * @rc:   The bare returncode as returned by the device driver ioctl
 *	  function.
 * @dev:  The adapter nr where this request was actually processed.
 * @dom:  Domain id of the device where this request was processed.
 *
 * Called upon recognising the reply from the crypto adapter. This
 * message may act as the exit timestamp for the request but also
 * carries some info about on which adapter the request was processed
 * and the returncode from the device driver.
 */
TRACE_EVENT(s390_zcrypt_rep,
	    TP_PROTO(void *ptr, u32 fc, u32 rc, u16 dev, u16 dom),
	    TP_ARGS(ptr, fc, rc, dev, dom),
	    TP_STRUCT__entry(
		    __field(void *, ptr)
		    __field(u32, fc)
		    __field(u32, rc)
		    __field(u16, device)
		    __field(u16, domain)),
	    TP_fast_assign(
		    __entry->ptr = ptr;
		    __entry->fc = fc;
		    __entry->rc = rc;
		    __entry->device = dev;
		    __entry->domain = dom;),
	    TP_printk("ptr=%p fc=0x%04x rc=%d dev=0x%02hx domain=0x%04hx",
		      __entry->ptr,
		      (unsigned int) __entry->fc,
		      (int) __entry->rc,
		      (unsigned short) __entry->device,
		      (unsigned short) __entry->domain)
);

#endif /* _TRACE_S390_ZCRYPT_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm/trace
#define TRACE_INCLUDE_FILE zcrypt

#include <trace/define_trace.h>
