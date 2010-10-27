/*
 * chnldefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * System-wide channel objects and constants.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef CHNLDEFS_
#define CHNLDEFS_

/* Channel id option. */
#define CHNL_PICKFREE       (~0UL)	/* Let manager pick a free channel. */

/* Channel manager limits: */
#define CHNL_INITIOREQS      4	/* Default # of I/O requests. */

/* Channel modes */
#define CHNL_MODETODSP		0	/* Data streaming to the DSP. */
#define CHNL_MODEFROMDSP	1	/* Data streaming from the DSP. */

/* GetIOCompletion flags */
#define CHNL_IOCINFINITE     0xffffffff	/* Wait forever for IO completion. */
#define CHNL_IOCNOWAIT       0x0	/* Dequeue an IOC, if available. */

/* IO Completion Record status: */
#define CHNL_IOCSTATCOMPLETE 0x0000	/* IO Completed. */
#define CHNL_IOCSTATCANCEL   0x0002	/* IO was cancelled */
#define CHNL_IOCSTATTIMEOUT  0x0008	/* Wait for IOC timed out. */
#define CHNL_IOCSTATEOS      0x8000	/* End Of Stream reached. */

/* Macros for checking I/O Completion status: */
#define CHNL_IS_IO_COMPLETE(ioc)  (!(ioc.status & ~CHNL_IOCSTATEOS))
#define CHNL_IS_IO_CANCELLED(ioc) (ioc.status & CHNL_IOCSTATCANCEL)
#define CHNL_IS_TIMED_OUT(ioc)    (ioc.status & CHNL_IOCSTATTIMEOUT)

/* Channel attributes: */
struct chnl_attr {
	u32 uio_reqs;		/* Max # of preallocated I/O requests. */
	void *event_obj;	/* User supplied auto-reset event object. */
	char *pstr_event_name;	/* Ptr to name of user event object. */
	void *reserved1;	/* Reserved for future use. */
	u32 reserved2;		/* Reserved for future use. */

};

/* I/O completion record: */
struct chnl_ioc {
	void *pbuf;		/* Buffer to be filled/emptied. */
	u32 byte_size;		/* Bytes transferred. */
	u32 buf_size;		/* Actual buffer size in bytes */
	u32 status;		/* Status of IO completion. */
	u32 dw_arg;		/* User argument associated with pbuf. */
};

#endif /* CHNLDEFS_ */
