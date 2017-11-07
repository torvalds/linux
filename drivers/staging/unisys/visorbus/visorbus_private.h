// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VISORBUS_PRIVATE_H__
#define __VISORBUS_PRIVATE_H__

#include <linux/uuid.h>
#include <linux/utsname.h>

#include "controlvmchannel.h"
#include "vbuschannel.h"
#include "visorbus.h"

struct visor_device *visorbus_get_device_by_id(u32 bus_no, u32 dev_no,
					       struct visor_device *from);
int visorbus_create_instance(struct visor_device *dev);
void visorbus_remove_instance(struct visor_device *bus_info);
int create_visor_device(struct visor_device *dev_info);
void remove_visor_device(struct visor_device *dev_info);
int visorchipset_device_pause(struct visor_device *dev_info);
int visorchipset_device_resume(struct visor_device *dev_info);
void visorbus_response(struct visor_device *p, int response, int controlvm_id);
void visorbus_device_changestate_response(struct visor_device *p, int response,
					  struct visor_segment_state state);
int visorbus_init(void);
void visorbus_exit(void);

/* visorchannel access functions */
struct visorchannel *visorchannel_create(u64 physaddr, gfp_t gfp,
					 const guid_t *guid, bool needs_lock);
void visorchannel_destroy(struct visorchannel *channel);
int visorchannel_read(struct visorchannel *channel, ulong offset,
		      void *dest, ulong nbytes);
int visorchannel_write(struct visorchannel *channel, ulong offset,
		       void *dest, ulong nbytes);
u64 visorchannel_get_physaddr(struct visorchannel *channel);
ulong visorchannel_get_nbytes(struct visorchannel *channel);
char *visorchannel_id(struct visorchannel *channel, char *s);
char *visorchannel_zoneid(struct visorchannel *channel, char *s);
u64 visorchannel_get_clientpartition(struct visorchannel *channel);
int visorchannel_set_clientpartition(struct visorchannel *channel,
				     u64 partition_handle);
char *visorchannel_guid_id(const guid_t *guid, char *s);
void *visorchannel_get_header(struct visorchannel *channel);
#endif
