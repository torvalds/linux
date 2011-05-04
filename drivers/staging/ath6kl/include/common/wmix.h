//------------------------------------------------------------------------------
// <copyright file="wmix.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

/*
 * This file contains extensions of the WMI protocol specified in the
 * Wireless Module Interface (WMI).  It includes definitions of all
 * extended commands and events.  Extensions include useful commands
 * that are not directly related to wireless activities.  They may
 * be hardware-specific, and they might not be supported on all
 * implementations.
 *
 * Extended WMIX commands are encapsulated in a WMI message with
 * cmd=WMI_EXTENSION_CMD.
 */

#ifndef _WMIX_H_
#define _WMIX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dbglog.h"

/*
 * Extended WMI commands are those that are needed during wireless
 * operation, but which are not really wireless commands.  This allows,
 * for instance, platform-specific commands.  Extended WMI commands are
 * embedded in a WMI command message with WMI_COMMAND_ID=WMI_EXTENSION_CMDID.
 * Extended WMI events are similarly embedded in a WMI event message with
 * WMI_EVENT_ID=WMI_EXTENSION_EVENTID.
 */
typedef PREPACK struct {
    u32 commandId;
} POSTPACK WMIX_CMD_HDR;

typedef enum {
    WMIX_DSETOPEN_REPLY_CMDID           = 0x2001,
    WMIX_DSETDATA_REPLY_CMDID,
    WMIX_GPIO_OUTPUT_SET_CMDID,
    WMIX_GPIO_INPUT_GET_CMDID,
    WMIX_GPIO_REGISTER_SET_CMDID,
    WMIX_GPIO_REGISTER_GET_CMDID,
    WMIX_GPIO_INTR_ACK_CMDID,
    WMIX_HB_CHALLENGE_RESP_CMDID,
    WMIX_DBGLOG_CFG_MODULE_CMDID,
    WMIX_PROF_CFG_CMDID,                 /* 0x200a */
    WMIX_PROF_ADDR_SET_CMDID,
    WMIX_PROF_START_CMDID,
    WMIX_PROF_STOP_CMDID,
    WMIX_PROF_COUNT_GET_CMDID,
} WMIX_COMMAND_ID;

typedef enum {
    WMIX_DSETOPENREQ_EVENTID            = 0x3001,
    WMIX_DSETCLOSE_EVENTID,
    WMIX_DSETDATAREQ_EVENTID,
    WMIX_GPIO_INTR_EVENTID,
    WMIX_GPIO_DATA_EVENTID,
    WMIX_GPIO_ACK_EVENTID,
    WMIX_HB_CHALLENGE_RESP_EVENTID,
    WMIX_DBGLOG_EVENTID,
    WMIX_PROF_COUNT_EVENTID,
} WMIX_EVENT_ID;

/*
 * =============DataSet support=================
 */

/*
 * WMIX_DSETOPENREQ_EVENTID
 * DataSet Open Request Event
 */
typedef PREPACK struct {
    u32 dset_id;
    u32 targ_dset_handle;  /* echo'ed, not used by Host, */
    u32 targ_reply_fn;     /* echo'ed, not used by Host, */
    u32 targ_reply_arg;    /* echo'ed, not used by Host, */
} POSTPACK WMIX_DSETOPENREQ_EVENT;

/*
 * WMIX_DSETCLOSE_EVENTID
 * DataSet Close Event
 */
typedef PREPACK struct {
    u32 access_cookie;
} POSTPACK WMIX_DSETCLOSE_EVENT;

/*
 * WMIX_DSETDATAREQ_EVENTID
 * DataSet Data Request Event
 */
typedef PREPACK struct {
    u32 access_cookie;
    u32 offset;
    u32 length;
    u32 targ_buf;         /* echo'ed, not used by Host, */
    u32 targ_reply_fn;    /* echo'ed, not used by Host, */
    u32 targ_reply_arg;   /* echo'ed, not used by Host, */
} POSTPACK WMIX_DSETDATAREQ_EVENT;

typedef PREPACK struct {
    u32 status;
    u32 targ_dset_handle;
    u32 targ_reply_fn;
    u32 targ_reply_arg;
    u32 access_cookie;
    u32 size;
    u32 version;
} POSTPACK WMIX_DSETOPEN_REPLY_CMD;

typedef PREPACK struct {
    u32 status;
    u32 targ_buf;
    u32 targ_reply_fn;
    u32 targ_reply_arg;
    u32 length;
    u8 buf[1];
} POSTPACK WMIX_DSETDATA_REPLY_CMD;


/* 
 * =============GPIO support=================
 * All masks are 18-bit masks with bit N operating on GPIO pin N.
 */


/*
 * Set GPIO pin output state.
 * In order for output to be driven, a pin must be enabled for output.
 * This can be done during initialization through the GPIO Configuration
 * DataSet, or during operation with the enable_mask.
 *
 * If a request is made to simultaneously set/clear or set/disable or
 * clear/disable or disable/enable, results are undefined.
 */
typedef PREPACK struct {
    u32 set_mask;             /* pins to set */
    u32 clear_mask;           /* pins to clear */
    u32 enable_mask;          /* pins to enable for output */
    u32 disable_mask;         /* pins to disable/tristate */
} POSTPACK WMIX_GPIO_OUTPUT_SET_CMD;

/* 
 * Set a GPIO register.  For debug/exceptional cases.
 * Values for gpioreg_id are GPIO_REGISTER_IDs, defined in a
 * platform-dependent header.
 */
typedef PREPACK struct {
    u32 gpioreg_id;           /* GPIO register ID */
    u32 value;                /* value to write */
} POSTPACK WMIX_GPIO_REGISTER_SET_CMD;

/* Get a GPIO register.  For debug/exceptional cases. */
typedef PREPACK struct {
    u32 gpioreg_id;           /* GPIO register to read */
} POSTPACK WMIX_GPIO_REGISTER_GET_CMD;

/*
 * Host acknowledges and re-arms GPIO interrupts.  A single
 * message should be used to acknowledge all interrupts that
 * were delivered in an earlier WMIX_GPIO_INTR_EVENT message.
 */
typedef PREPACK struct {
    u32 ack_mask;             /* interrupts to acknowledge */
} POSTPACK WMIX_GPIO_INTR_ACK_CMD;

/*
 * Target informs Host of GPIO interrupts that have occurred since the
 * last WMIX_GIPO_INTR_ACK_CMD was received.  Additional information --
 * the current GPIO input values is provided -- in order to support
 * use of a GPIO interrupt as a Data Valid signal for other GPIO pins.
 */
typedef PREPACK struct {
    u32 intr_mask;            /* pending GPIO interrupts */
    u32 input_values;         /* recent GPIO input values */
} POSTPACK WMIX_GPIO_INTR_EVENT;

/*
 * Target responds to Host's earlier WMIX_GPIO_INPUT_GET_CMDID request
 * using a GPIO_DATA_EVENT with
 *   value set to the mask of GPIO pin inputs and
 *   reg_id set to GPIO_ID_NONE
 * 
 *
 * Target responds to Hosts's earlier WMIX_GPIO_REGISTER_GET_CMDID request
 * using a GPIO_DATA_EVENT with
 *   value set to the value of the requested register and
 *   reg_id identifying the register (reflects the original request)
 * NB: reg_id supports the future possibility of unsolicited
 * WMIX_GPIO_DATA_EVENTs (for polling GPIO input), and it may
 * simplify Host GPIO support.
 */
typedef PREPACK struct {
    u32 value;
    u32 reg_id;
} POSTPACK WMIX_GPIO_DATA_EVENT;

/*
 * =============Error Detection support=================
 */

/*
 * WMIX_HB_CHALLENGE_RESP_CMDID
 * Heartbeat Challenge Response command
 */
typedef PREPACK struct {
    u32 cookie;
    u32 source;
} POSTPACK WMIX_HB_CHALLENGE_RESP_CMD;

/*
 * WMIX_HB_CHALLENGE_RESP_EVENTID
 * Heartbeat Challenge Response Event
 */
#define WMIX_HB_CHALLENGE_RESP_EVENT WMIX_HB_CHALLENGE_RESP_CMD

typedef PREPACK struct {
    struct dbglog_config_s config;
} POSTPACK WMIX_DBGLOG_CFG_MODULE_CMD;

/*
 * =============Target Profiling support=================
 */

typedef PREPACK struct {
    u32 period; /* Time (in 30.5us ticks) between samples */
    u32 nbins;
} POSTPACK WMIX_PROF_CFG_CMD;

typedef PREPACK struct {
    u32 addr;
} POSTPACK WMIX_PROF_ADDR_SET_CMD;

/*
 * Target responds to Hosts's earlier WMIX_PROF_COUNT_GET_CMDID request
 * using a WMIX_PROF_COUNT_EVENT with
 *   addr set to the next address
 *   count set to the corresponding count
 */
typedef PREPACK struct {
    u32 addr;
    u32 count;
} POSTPACK WMIX_PROF_COUNT_EVENT;


#ifdef __cplusplus
}
#endif

#endif /* _WMIX_H_ */
