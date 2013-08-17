/*
 * msg.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge msg_ctrl Module.
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

#ifndef MSG_
#define MSG_

#include <dspbridge/devdefs.h>
#include <dspbridge/msgdefs.h>

/*
 *  ======== msg_create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object. The msg_ctrl manager must be created before
 *      the IO Manager.
 *  Parameters:
 *      msg_man:            Location to store msg_ctrl manager handle on output.
 *      hdev_obj:         The device object.
 *      msg_callback:        Called whenever an RMS_EXIT message is received.
 *  Returns:
 *  Requires:
 *      msg_man != NULL.
 *      hdev_obj != NULL.
 *      msg_callback != NULL.
 *  Ensures:
 */
extern int msg_create(struct msg_mgr **msg_man,
			     struct dev_object *hdev_obj,
			     msg_onexit msg_callback);

/*
 *  ======== msg_delete ========
 *  Purpose:
 *      Delete a msg_ctrl manager allocated in msg_create().
 *  Parameters:
 *      hmsg_mgr:            Handle returned from msg_create().
 *  Returns:
 *  Requires:
 *      Valid hmsg_mgr.
 *  Ensures:
 */
extern void msg_delete(struct msg_mgr *hmsg_mgr);

#endif /* MSG_ */
