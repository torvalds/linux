/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2001, 2007
 * Authors: 	Fritz Elfert (felfert@millenux.com)
 * 		Peter Tiedemann (ptiedem@de.ibm.com)
 * 	MPC additions :
 *		Belinda Thompson (belindat@us.ibm.com)
 *		Andy Richter (richtera@us.ibm.com)
 */
#ifndef _CTCM_FSMS_H_
#define _CTCM_FSMS_H_

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/bitops.h>

#include <linux/signal.h>
#include <linux/string.h>

#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/dst.h>

#include <linux/io.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <linux/uaccess.h>

#include <asm/idals.h>

#include "fsm.h"
#include "ctcm_main.h"

/*
 * Definitions for the channel statemachine(s) for ctc and ctcmpc
 *
 * To allow better kerntyping, prefix-less definitions for channel states
 * and channel events have been replaced :
 * ch_event... -> ctc_ch_event...
 * CH_EVENT... -> CTC_EVENT...
 * ch_state... -> ctc_ch_state...
 * CH_STATE... -> CTC_STATE...
 */
/*
 * Events of the channel statemachine(s) for ctc and ctcmpc
 */
enum ctc_ch_events {
	/*
	 * Events, representing return code of
	 * I/O operations (ccw_device_start, ccw_device_halt et al.)
	 */
	CTC_EVENT_IO_SUCCESS,
	CTC_EVENT_IO_EBUSY,
	CTC_EVENT_IO_ENODEV,
	CTC_EVENT_IO_UNKNOWN,

	CTC_EVENT_ATTNBUSY,
	CTC_EVENT_ATTN,
	CTC_EVENT_BUSY,
	/*
	 * Events, representing unit-check
	 */
	CTC_EVENT_UC_RCRESET,
	CTC_EVENT_UC_RSRESET,
	CTC_EVENT_UC_TXTIMEOUT,
	CTC_EVENT_UC_TXPARITY,
	CTC_EVENT_UC_HWFAIL,
	CTC_EVENT_UC_RXPARITY,
	CTC_EVENT_UC_ZERO,
	CTC_EVENT_UC_UNKNOWN,
	/*
	 * Events, representing subchannel-check
	 */
	CTC_EVENT_SC_UNKNOWN,
	/*
	 * Events, representing machine checks
	 */
	CTC_EVENT_MC_FAIL,
	CTC_EVENT_MC_GOOD,
	/*
	 * Event, representing normal IRQ
	 */
	CTC_EVENT_IRQ,
	CTC_EVENT_FINSTAT,
	/*
	 * Event, representing timer expiry.
	 */
	CTC_EVENT_TIMER,
	/*
	 * Events, representing commands from upper levels.
	 */
	CTC_EVENT_START,
	CTC_EVENT_STOP,
	CTC_NR_EVENTS,
	/*
	 * additional MPC events
	 */
	CTC_EVENT_SEND_XID = CTC_NR_EVENTS,
	CTC_EVENT_RSWEEP_TIMER,
	/*
	 * MUST be always the last element!!
	 */
	CTC_MPC_NR_EVENTS,
};

/*
 * States of the channel statemachine(s) for ctc and ctcmpc.
 */
enum ctc_ch_states {
	/*
	 * Channel not assigned to any device,
	 * initial state, direction invalid
	 */
	CTC_STATE_IDLE,
	/*
	 * Channel assigned but not operating
	 */
	CTC_STATE_STOPPED,
	CTC_STATE_STARTWAIT,
	CTC_STATE_STARTRETRY,
	CTC_STATE_SETUPWAIT,
	CTC_STATE_RXINIT,
	CTC_STATE_TXINIT,
	CTC_STATE_RX,
	CTC_STATE_TX,
	CTC_STATE_RXIDLE,
	CTC_STATE_TXIDLE,
	CTC_STATE_RXERR,
	CTC_STATE_TXERR,
	CTC_STATE_TERM,
	CTC_STATE_DTERM,
	CTC_STATE_NOTOP,
	CTC_NR_STATES,     /* MUST be the last element of non-expanded states */
	/*
	 * additional MPC states
	 */
	CH_XID0_PENDING = CTC_NR_STATES,
	CH_XID0_INPROGRESS,
	CH_XID7_PENDING,
	CH_XID7_PENDING1,
	CH_XID7_PENDING2,
	CH_XID7_PENDING3,
	CH_XID7_PENDING4,
	CTC_MPC_NR_STATES, /* MUST be the last element of expanded mpc states */
};

extern const char *ctc_ch_event_names[];

extern const char *ctc_ch_state_names[];

void ctcm_ccw_check_rc(struct channel *ch, int rc, char *msg);
void ctcm_purge_skb_queue(struct sk_buff_head *q);
void fsm_action_nop(fsm_instance *fi, int event, void *arg);

/*
 * ----- non-static actions for ctcm channel statemachine -----
 *
 */
void ctcm_chx_txidle(fsm_instance *fi, int event, void *arg);

/*
 * ----- FSM (state/event/action) of the ctcm channel statemachine -----
 */
extern const fsm_node ch_fsm[];
extern int ch_fsm_len;


/*
 * ----- non-static actions for ctcmpc channel statemachine ----
 *
 */
/* shared :
void ctcm_chx_txidle(fsm_instance * fi, int event, void *arg);
 */
void ctcmpc_chx_rxidle(fsm_instance *fi, int event, void *arg);

/*
 * ----- FSM (state/event/action) of the ctcmpc channel statemachine -----
 */
extern const fsm_node ctcmpc_ch_fsm[];
extern int mpc_ch_fsm_len;

/*
 * Definitions for the device interface statemachine for ctc and mpc
 */

/*
 * States of the device interface statemachine.
 */
enum dev_states {
	DEV_STATE_STOPPED,
	DEV_STATE_STARTWAIT_RXTX,
	DEV_STATE_STARTWAIT_RX,
	DEV_STATE_STARTWAIT_TX,
	DEV_STATE_STOPWAIT_RXTX,
	DEV_STATE_STOPWAIT_RX,
	DEV_STATE_STOPWAIT_TX,
	DEV_STATE_RUNNING,
	/*
	 * MUST be always the last element!!
	 */
	CTCM_NR_DEV_STATES
};

extern const char *dev_state_names[];

/*
 * Events of the device interface statemachine.
 * ctcm and ctcmpc
 */
enum dev_events {
	DEV_EVENT_START,
	DEV_EVENT_STOP,
	DEV_EVENT_RXUP,
	DEV_EVENT_TXUP,
	DEV_EVENT_RXDOWN,
	DEV_EVENT_TXDOWN,
	DEV_EVENT_RESTART,
	/*
	 * MUST be always the last element!!
	 */
	CTCM_NR_DEV_EVENTS
};

extern const char *dev_event_names[];

/*
 * Actions for the device interface statemachine.
 * ctc and ctcmpc
 */
/*
static void dev_action_start(fsm_instance * fi, int event, void *arg);
static void dev_action_stop(fsm_instance * fi, int event, void *arg);
static void dev_action_restart(fsm_instance *fi, int event, void *arg);
static void dev_action_chup(fsm_instance * fi, int event, void *arg);
static void dev_action_chdown(fsm_instance * fi, int event, void *arg);
*/

/*
 * The (state/event/action) fsm table of the device interface statemachine.
 * ctcm and ctcmpc
 */
extern const fsm_node dev_fsm[];
extern int dev_fsm_len;


/*
 * Definitions for the MPC Group statemachine
 */

/*
 * MPC Group Station FSM States

State Name		When In This State
======================	=======================================
MPCG_STATE_RESET	Initial State When Driver Loaded
			We receive and send NOTHING

MPCG_STATE_INOP         INOP Received.
			Group level non-recoverable error

MPCG_STATE_READY	XID exchanges for at least 1 write and
			1 read channel have completed.
			Group is ready for data transfer.

States from ctc_mpc_alloc_channel
==============================================================
MPCG_STATE_XID2INITW	Awaiting XID2(0) Initiation
			      ATTN from other side will start
			      XID negotiations.
			      Y-side protocol only.

MPCG_STATE_XID2INITX	XID2(0) negotiations are in progress.
			      At least 1, but not all, XID2(0)'s
			      have been received from partner.

MPCG_STATE_XID7INITW	XID2(0) complete
			      No XID2(7)'s have yet been received.
			      XID2(7) negotiations pending.

MPCG_STATE_XID7INITX	XID2(7) negotiations in progress.
			      At least 1, but not all, XID2(7)'s
			      have been received from partner.

MPCG_STATE_XID7INITF	XID2(7) negotiations complete.
			      Transitioning to READY.

MPCG_STATE_READY	      Ready for Data Transfer.


States from ctc_mpc_establish_connectivity call
==============================================================
MPCG_STATE_XID0IOWAIT	Initiating XID2(0) negotiations.
			      X-side protocol only.
			      ATTN-BUSY from other side will convert
			      this to Y-side protocol and the
			      ctc_mpc_alloc_channel flow will begin.

MPCG_STATE_XID0IOWAIX	XID2(0) negotiations are in progress.
			      At least 1, but not all, XID2(0)'s
			      have been received from partner.

MPCG_STATE_XID7INITI	XID2(0) complete
			      No XID2(7)'s have yet been received.
			      XID2(7) negotiations pending.

MPCG_STATE_XID7INITZ	XID2(7) negotiations in progress.
			      At least 1, but not all, XID2(7)'s
			      have been received from partner.

MPCG_STATE_XID7INITF	XID2(7) negotiations complete.
			      Transitioning to READY.

MPCG_STATE_READY	      Ready for Data Transfer.

*/

enum mpcg_events {
	MPCG_EVENT_INOP,
	MPCG_EVENT_DISCONC,
	MPCG_EVENT_XID0DO,
	MPCG_EVENT_XID2,
	MPCG_EVENT_XID2DONE,
	MPCG_EVENT_XID7DONE,
	MPCG_EVENT_TIMER,
	MPCG_EVENT_DOIO,
	MPCG_NR_EVENTS,
};

enum mpcg_states {
	MPCG_STATE_RESET,
	MPCG_STATE_INOP,
	MPCG_STATE_XID2INITW,
	MPCG_STATE_XID2INITX,
	MPCG_STATE_XID7INITW,
	MPCG_STATE_XID7INITX,
	MPCG_STATE_XID0IOWAIT,
	MPCG_STATE_XID0IOWAIX,
	MPCG_STATE_XID7INITI,
	MPCG_STATE_XID7INITZ,
	MPCG_STATE_XID7INITF,
	MPCG_STATE_FLOWC,
	MPCG_STATE_READY,
	MPCG_NR_STATES,
};

#endif
/* --- This is the END my friend --- */
