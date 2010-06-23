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
 *      phMsgMgr:           Location to store msg_ctrl manager handle on output.
 *      hdev_obj:         The device object.
 *      msgCallback:        Called whenever an RMS_EXIT message is received.
 *  Returns:
 *  Requires:
 *      msg_mod_init(void) called.
 *      phMsgMgr != NULL.
 *      hdev_obj != NULL.
 *      msgCallback != NULL.
 *  Ensures:
 */
extern int msg_create(OUT struct msg_mgr **phMsgMgr,
			     struct dev_object *hdev_obj,
			     msg_onexit msgCallback);

/*
 *  ======== msg_delete ========
 *  Purpose:
 *      Delete a msg_ctrl manager allocated in msg_create().
 *  Parameters:
 *      hmsg_mgr:            Handle returned from msg_create().
 *  Returns:
 *  Requires:
 *      msg_mod_init(void) called.
 *      Valid hmsg_mgr.
 *  Ensures:
 */
extern void msg_delete(struct msg_mgr *hmsg_mgr);

/*
 *  ======== msg_exit ========
 *  Purpose:
 *      Discontinue usage of msg_ctrl module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      msg_mod_init(void) successfully called before.
 *  Ensures:
 *      Any resources acquired in msg_mod_init(void) will be freed when last
 *      msg_ctrl client calls msg_exit(void).
 */
extern void msg_exit(void);

/*
 *  ======== msg_mod_init ========
 *  Purpose:
 *      Initialize the msg_ctrl module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
extern bool msg_mod_init(void);

#endif /* MSG_ */
