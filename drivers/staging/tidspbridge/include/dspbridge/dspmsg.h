/*
 * dspmsg.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declares the upper edge message class library functions required by
 * all Bridge driver / DSP API interface tables.  These functions are
 * implemented by every class of Bridge driver channel library.
 *
 * Notes:
 *   Function comment headers reside in dspdefs.h.
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

#ifndef DSPMSG_
#define DSPMSG_

#include <dspbridge/msgdefs.h>

extern int bridge_msg_create(struct msg_mgr **msg_man,
				    struct dev_object *hdev_obj,
				    msg_onexit msg_callback);

extern int bridge_msg_create_queue(struct msg_mgr *hmsg_mgr,
				       struct msg_queue **msgq,
				       u32 msgq_id, u32 max_msgs, void *arg);

extern void bridge_msg_delete(struct msg_mgr *hmsg_mgr);

extern void bridge_msg_delete_queue(struct msg_queue *msg_queue_obj);

extern int bridge_msg_get(struct msg_queue *msg_queue_obj,
				 struct dsp_msg *pmsg, u32 utimeout);

extern int bridge_msg_put(struct msg_queue *msg_queue_obj,
				 const struct dsp_msg *pmsg, u32 utimeout);

extern int bridge_msg_register_notify(struct msg_queue *msg_queue_obj,
					  u32 event_mask,
					  u32 notify_type,
					  struct dsp_notification
					  *hnotification);

extern void bridge_msg_set_queue_id(struct msg_queue *msg_queue_obj,
					u32 msgq_id);

#endif /* DSPMSG_ */
