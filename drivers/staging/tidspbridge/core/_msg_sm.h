/*
 * _msg_sm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private header file defining msg_ctrl manager objects and defines needed
 * by IO manager.
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

#ifndef _MSG_SM_
#define _MSG_SM_

#include <dspbridge/list.h>
#include <dspbridge/msgdefs.h>

/*
 *  These target side symbols define the beginning and ending addresses
 *  of the section of shared memory used for messages. They are
 *  defined in the *cfg.cmd file by cdb code.
 */
#define MSG_SHARED_BUFFER_BASE_SYM      "_MSG_BEG"
#define MSG_SHARED_BUFFER_LIMIT_SYM     "_MSG_END"

#ifndef _CHNL_WORDSIZE
#define _CHNL_WORDSIZE 4	/* default _CHNL_WORDSIZE is 2 bytes/word */
#endif

/*
 *  ======== msg_ctrl ========
 *  There is a control structure for messages to the DSP, and a control
 *  structure for messages from the DSP. The shared memory region for
 *  transferring messages is partitioned as follows:
 *
 *  ----------------------------------------------------------
 *  |Control | Messages from DSP | Control | Messages to DSP |
 *  ----------------------------------------------------------
 *
 *  msg_ctrl control structure for messages to the DSP is used in the following
 *  way:
 *
 *  buf_empty -      This flag is set to FALSE by the GPP after it has output
 *                  messages for the DSP. The DSP host driver sets it to
 *                  TRUE after it has copied the messages.
 *  post_swi -       Set to 1 by the GPP after it has written the messages,
 *                  set the size, and set buf_empty to FALSE.
 *                  The DSP Host driver uses SWI_andn of the post_swi field
 *                  when a host interrupt occurs. The host driver clears
 *                  this after posting the SWI.
 *  size -          Number of messages to be read by the DSP.
 *
 *  For messages from the DSP:
 *  buf_empty -      This flag is set to FALSE by the DSP after it has output
 *                  messages for the GPP. The DPC on the GPP sets it to
 *                  TRUE after it has copied the messages.
 *  post_swi -       Set to 1 the DPC on the GPP after copying the messages.
 *  size -          Number of messages to be read by the GPP.
 */
struct msg_ctrl {
	u32 buf_empty;		/* to/from DSP buffer is empty */
	u32 post_swi;		/* Set to "1" to post msg_ctrl SWI */
	u32 size;		/* Number of messages to/from the DSP */
	u32 resvd;
};

/*
 *  ======== msg_mgr ========
 *  The msg_mgr maintains a list of all MSG_QUEUEs. Each NODE object can
 *  have msg_queue to hold all messages that come up from the corresponding
 *  node on the DSP. The msg_mgr also has a shared queue of messages
 *  ready to go to the DSP.
 */
struct msg_mgr {
	/* The first field must match that in msgobj.h */

	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;

	struct io_mgr *hio_mgr;	/* IO manager */
	struct lst_list *queue_list;	/* List of MSG_QUEUEs */
	spinlock_t msg_mgr_lock;	/* For critical sections */
	/* Signalled when MsgFrame is available */
	struct sync_object *sync_event;
	struct lst_list *msg_free_list;	/* Free MsgFrames ready to be filled */
	struct lst_list *msg_used_list;	/* MsgFrames ready to go to DSP */
	u32 msgs_pending;	/* # of queued messages to go to DSP */
	u32 max_msgs;		/* Max # of msgs that fit in buffer */
	msg_onexit on_exit;	/* called when RMS_EXIT is received */
};

/*
 *  ======== msg_queue ========
 *  Each NODE has a msg_queue for receiving messages from the
 *  corresponding node on the DSP. The msg_queue object maintains a list
 *  of messages that have been sent to the host, but not yet read (MSG_Get),
 *  and a list of free frames that can be filled when new messages arrive
 *  from the DSP.
 *  The msg_queue's hSynEvent gets posted when a message is ready.
 */
struct msg_queue {
	struct list_head list_elem;
	struct msg_mgr *hmsg_mgr;
	u32 max_msgs;		/* Node message depth */
	u32 msgq_id;		/* Node environment pointer */
	struct lst_list *msg_free_list;	/* Free MsgFrames ready to be filled */
	/* Filled MsgFramess waiting to be read */
	struct lst_list *msg_used_list;
	void *arg;		/* Handle passed to mgr on_exit callback */
	struct sync_object *sync_event;	/* Signalled when message is ready */
	struct sync_object *sync_done;	/* For synchronizing cleanup */
	struct sync_object *sync_done_ack;	/* For synchronizing cleanup */
	struct ntfy_object *ntfy_obj;	/* For notification of message ready */
	bool done;		/* TRUE <==> deleting the object */
	u32 io_msg_pend;	/* Number of pending MSG_get/put calls */
};

/*
 *  ======== msg_dspmsg ========
 */
struct msg_dspmsg {
	struct dsp_msg msg;
	u32 msgq_id;		/* Identifies the node the message goes to */
};

/*
 *  ======== msg_frame ========
 */
struct msg_frame {
	struct list_head list_elem;
	struct msg_dspmsg msg_data;
};

#endif /* _MSG_SM_ */
