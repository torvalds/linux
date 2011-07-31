/*
 * msg.c
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
#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- Bridge Driver */
#include <dspbridge/dspdefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <msgobj.h>
#include <dspbridge/msg.h>

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */

/*
 *  ======== msg_create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 */
int msg_create(struct msg_mgr **msg_man,
		      struct dev_object *hdev_obj, msg_onexit msg_callback)
{
	struct bridge_drv_interface *intf_fxns;
	struct msg_mgr_ *msg_mgr_obj;
	struct msg_mgr *hmsg_mgr;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(msg_man != NULL);
	DBC_REQUIRE(msg_callback != NULL);
	DBC_REQUIRE(hdev_obj != NULL);

	*msg_man = NULL;

	dev_get_intf_fxns(hdev_obj, &intf_fxns);

	/* Let Bridge message module finish the create: */
	status =
	    (*intf_fxns->pfn_msg_create) (&hmsg_mgr, hdev_obj, msg_callback);

	if (!status) {
		/* Fill in DSP API message module's fields of the msg_mgr
		 * structure */
		msg_mgr_obj = (struct msg_mgr_ *)hmsg_mgr;
		msg_mgr_obj->intf_fxns = intf_fxns;

		/* Finally, return the new message manager handle: */
		*msg_man = hmsg_mgr;
	} else {
		status = -EPERM;
	}
	return status;
}

/*
 *  ======== msg_delete ========
 *  Purpose:
 *      Delete a msg_ctrl manager allocated in msg_create().
 */
void msg_delete(struct msg_mgr *hmsg_mgr)
{
	struct msg_mgr_ *msg_mgr_obj = (struct msg_mgr_ *)hmsg_mgr;
	struct bridge_drv_interface *intf_fxns;

	DBC_REQUIRE(refs > 0);

	if (msg_mgr_obj) {
		intf_fxns = msg_mgr_obj->intf_fxns;

		/* Let Bridge message module destroy the msg_mgr: */
		(*intf_fxns->pfn_msg_delete) (hmsg_mgr);
	} else {
		dev_dbg(bridge, "%s: Error hmsg_mgr handle: %p\n",
			__func__, hmsg_mgr);
	}
}

/*
 *  ======== msg_exit ========
 */
void msg_exit(void)
{
	DBC_REQUIRE(refs > 0);
	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== msg_mod_init ========
 */
bool msg_mod_init(void)
{
	DBC_REQUIRE(refs >= 0);

	refs++;

	DBC_ENSURE(refs >= 0);

	return true;
}
