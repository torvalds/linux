/*
 * io_sm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * IO dispatcher for a shared memory channel driver.
 * Also, includes macros to simulate shm via port io calls.
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

#ifndef IOSM_
#define IOSM_

#include <dspbridge/_chnl_sm.h>
#include <dspbridge/host_os.h>

#include <dspbridge/io.h>
#include <dspbridge/mbx_sh.h>	/* shared mailbox codes */

/* Magic code used to determine if DSP signaled exception. */
#define DEH_BASE        MBX_DEH_BASE
#define DEH_LIMIT       MBX_DEH_LIMIT

#define IO_INPUT            0
#define IO_OUTPUT           1
#define IO_SERVICE          2

#ifdef CONFIG_TIDSPBRIDGE_DVFS
/* The maximum number of OPPs that are supported */
extern s32 dsp_max_opps;
/* The Vdd1 opp table information */
extern u32 vdd1_dsp_freq[6][4];
#endif

/*
 *  ======== io_cancel_chnl ========
 *  Purpose:
 *      Cancel IO on a given channel.
 *  Parameters:
 *      hio_mgr:     IO Manager.
 *      chnl:       Index of channel to cancel IO on.
 *  Returns:
 *  Requires:
 *      Valid hio_mgr.
 *  Ensures:
 */
extern void io_cancel_chnl(struct io_mgr *hio_mgr, u32 chnl);

/*
 *  ======== io_dpc ========
 *  Purpose:
 *      Deferred procedure call for shared memory channel driver ISR.  Carries
 *      out the dispatch of I/O.
 *  Parameters:
 *      ref_data:   Pointer to reference data registered via a call to
 *                  DPC_Create().
 *  Returns:
 *  Requires:
 *      Must not block.
 *      Must not acquire resources.
 *      All data touched must be locked in memory if running in kernel mode.
 *  Ensures:
 *      Non-preemptible (but interruptible).
 */
extern void io_dpc(unsigned long ref_data);

/*
 *  ======== io_mbox_msg ========
 *  Purpose:
 *	Main message handler for the shared memory Bridge channel manager.
 *	Determine if this message is ours, then schedules a DPC to
 *	dispatch I/O.
 *  Parameters:
 *	self:	Pointer to its own notifier_block struct.
 *	len:	Length of message.
 *	msg:	Message code received.
 *  Returns:
 *	NOTIFY_OK if handled; NOTIFY_BAD otherwise.
 */
int io_mbox_msg(struct notifier_block *self, unsigned long len, void *msg);

/*
 *  ======== io_request_chnl ========
 *  Purpose:
 *      Request I/O from the DSP. Sets flags in shared memory, then interrupts
 *      the DSP.
 *  Parameters:
 *      hio_mgr:     IO manager handle.
 *      pchnl:      Ptr to the channel requesting I/O.
 *      io_mode:      Mode of channel: {IO_INPUT | IO_OUTPUT}.
 *  Returns:
 *  Requires:
 *      pchnl != NULL
 *  Ensures:
 */
extern void io_request_chnl(struct io_mgr *io_manager,
			    struct chnl_object *pchnl,
			    u8 io_mode, u16 *mbx_val);

/*
 *  ======== iosm_schedule ========
 *  Purpose:
 *      Schedule DPC for IO.
 *  Parameters:
 *      pio_mgr:     Ptr to a I/O manager.
 *  Returns:
 *  Requires:
 *      pchnl != NULL
 *  Ensures:
 */
extern void iosm_schedule(struct io_mgr *io_manager);

/*
 *  ======== io_sh_msetting ========
 *  Purpose:
 *      Sets the shared memory setting
 *  Parameters:
 *      hio_mgr:         Handle to a I/O manager.
 *      desc:             Shared memory type
 *      pargs:          Ptr to shm setting
 *  Returns:
 *  Requires:
 *      hio_mgr != NULL
 *      pargs != NULL
 *  Ensures:
 */
extern int io_sh_msetting(struct io_mgr *hio_mgr, u8 desc, void *pargs);

/*
 *  Misc functions for the CHNL_IO shared memory library:
 */

/* Maximum channel bufsize that can be used. */
extern u32 io_buf_size(struct io_mgr *hio_mgr);

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
/*
 *  ========print_dsp_trace_buffer ========
 *      Print DSP tracebuffer.
 */
extern int print_dsp_trace_buffer(struct bridge_dev_context
					 *hbridge_context);

int dump_dsp_stack(struct bridge_dev_context *bridge_context);

void dump_dl_modules(struct bridge_dev_context *bridge_context);

void print_dsp_debug_trace(struct io_mgr *hio_mgr);
#endif

#endif /* IOSM_ */
