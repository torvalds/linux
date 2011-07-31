/*
 * chnlpriv.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private channel header shared between DSPSYS, DSPAPI and
 * Bridge driver modules.
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

#ifndef CHNLPRIV_
#define CHNLPRIV_

#include <dspbridge/chnldefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/sync.h>

/* Channel manager limits: */
#define CHNL_MAXCHANNELS    32	/* Max channels available per transport */

/*
 *  Trans port channel Id definitions:(must match dsp-side).
 *
 *  For CHNL_MAXCHANNELS = 16:
 *
 *  ChnlIds:
 *      0-15  (PCPY) - transport 0)
 *      16-31 (DDMA) - transport 1)
 *      32-47 (ZCPY) - transport 2)
 */
#define CHNL_PCPY       0	/* Proc-copy transport 0 */

#define CHNL_MAXIRQ     0xff	/* Arbitrarily large number. */

/* The following modes are private: */
#define CHNL_MODEUSEREVENT  0x1000	/* User provided the channel event. */
#define CHNL_MODEMASK       0x1001

/* Higher level channel states: */
#define CHNL_STATEREADY		0	/* Channel ready for I/O. */
#define CHNL_STATECANCEL	1	/* I/O was cancelled. */
#define CHNL_STATEEOS		2	/* End Of Stream reached. */

/* Macros for checking mode: */
#define CHNL_IS_INPUT(mode)      (mode & CHNL_MODEFROMDSP)
#define CHNL_IS_OUTPUT(mode)     (!CHNL_IS_INPUT(mode))

/* Types of channel class libraries: */
#define CHNL_TYPESM         1	/* Shared memory driver. */
#define CHNL_TYPEBM         2	/* Bus Mastering driver. */

/* Max string length of channel I/O completion event name - change if needed */
#define CHNL_MAXEVTNAMELEN  32

/* Max memory pages lockable in CHNL_PrepareBuffer() - change if needed */
#define CHNL_MAXLOCKPAGES   64

/* Channel info. */
struct chnl_info {
	struct chnl_mgr *hchnl_mgr;	/* Owning channel manager. */
	u32 cnhl_id;		/* Channel ID. */
	void *event_obj;	/* Channel I/O completion event. */
	/*Abstraction of I/O completion event. */
	struct sync_object *sync_event;
	s8 dw_mode;		/* Channel mode. */
	u8 dw_state;		/* Current channel state. */
	u32 bytes_tx;		/* Total bytes transferred. */
	u32 cio_cs;		/* Number of IOCs in queue. */
	u32 cio_reqs;		/* Number of IO Requests in queue. */
	u32 process;		/* Process owning this channel. */
};

/* Channel manager info: */
struct chnl_mgrinfo {
	u8 dw_type;		/* Type of channel class library. */
	/* Channel handle, given the channel id. */
	struct chnl_object *chnl_obj;
	u8 open_channels;	/* Number of open channels. */
	u8 max_channels;	/* total # of chnls supported */
};

/* Channel Manager Attrs: */
struct chnl_mgrattrs {
	/* Max number of channels this manager can use. */
	u8 max_channels;
	u32 word_size;		/* DSP Word size. */
};

#endif /* CHNLPRIV_ */
