/*
 * RPA Hot Plug Virtual I/O device functions 
 * Copyright (C) 2004 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <asm/vio.h>
#include "rpaphp.h"

/*
 * get_vio_adapter_status - get  the status of a slot
 * 
 * status:
 * 
 * 1-- adapter is configured
 * 2-- adapter is not configured
 * 3-- not valid
 */
inline int rpaphp_get_vio_adapter_status(struct slot *slot, int is_init, u8 *value)
{
	*value = slot->state;
	return 0;
}

int rpaphp_unconfig_vio_adapter(struct slot *slot)
{
	int retval = 0;

	dbg("Entry %s: slot[%s]\n", __FUNCTION__, slot->name);
	if (!slot->dev.vio_dev) {
		info("%s: no VIOA in slot[%s]\n", __FUNCTION__, slot->name);
		retval = -EINVAL;
		goto exit;
	}
	/* remove the device from the vio core */
	vio_unregister_device(slot->dev.vio_dev);
	slot->state = NOT_CONFIGURED;
	info("%s: adapter in slot[%s] unconfigured.\n", __FUNCTION__, slot->name);
exit:
	dbg("Exit %s, rc=0x%x\n", __FUNCTION__, retval);
	return retval;
}

static int setup_vio_hotplug_slot_info(struct slot *slot)
{
	slot->hotplug_slot->info->power_status = 1;
	rpaphp_get_vio_adapter_status(slot, 1,
		&slot->hotplug_slot->info->adapter_status); 
	return 0;
}

int register_vio_slot(struct device_node *dn)
{
	u32 *index;
	char *name;
	int rc = -EINVAL;
	struct slot *slot = NULL;
	
	rc = rpaphp_get_drc_props(dn, NULL, &name, NULL, NULL);
	if (rc < 0)
		goto exit_rc;
	index = (u32 *) get_property(dn, "ibm,my-drc-index", NULL);
	if (!index)
		goto exit_rc;
	if (!(slot = alloc_slot_struct(dn, *index, name, 0))) {
		rc = -ENOMEM;
		goto exit_rc;
	}
	slot->dev_type = VIO_DEV;
	slot->dev.vio_dev = vio_find_node(dn);
	if (slot->dev.vio_dev) {
		/*
		 * rpaphp is the only owner of vio devices and
		 * does not need extra reference taken by
		 * vio_find_node
		 */
		put_device(&slot->dev.vio_dev->dev);
	} else
		slot->dev.vio_dev = vio_register_device_node(dn);
	if (slot->dev.vio_dev)
		slot->state = CONFIGURED;
	else
		slot->state = NOT_CONFIGURED;
	if (setup_vio_hotplug_slot_info(slot))
		goto exit_rc;
	strcpy(slot->name, slot->dev.vio_dev->dev.bus_id);
	info("%s: registered VIO device[name=%s vio_dev=%p]\n",
		__FUNCTION__, slot->name, slot->dev.vio_dev); 
	rc = register_slot(slot);
exit_rc:
	if (rc && slot)
		dealloc_slot_struct(slot);
	return (rc);
}

int rpaphp_enable_vio_slot(struct slot *slot)
{
	int retval = 0;

	if ((slot->dev.vio_dev = vio_register_device_node(slot->dn))) {
		info("%s: VIO adapter %s in slot[%s] has been configured\n",
			__FUNCTION__, slot->dn->name, slot->name);
		slot->state = CONFIGURED;
	} else {
		info("%s: no vio_dev struct for adapter in slot[%s]\n",
			__FUNCTION__, slot->name);
		slot->state = NOT_CONFIGURED;
	}
	
	return retval;
}
