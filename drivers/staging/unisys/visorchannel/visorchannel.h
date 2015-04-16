/* visorchannel.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
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
 */

#ifndef __VISORCHANNEL_H__
#define __VISORCHANNEL_H__

#include <linux/uuid.h>

#include "memregion.h"
#include "channel.h"
#ifndef HOSTADDRESS
#define HOSTADDRESS u64
#endif
#ifndef BOOL
#define BOOL int
#endif

/* Note that for visorchannel_create() and visorchannel_create_overlapped(),
 * <channel_bytes> and <guid> arguments may be 0 if we are a channel CLIENT.
 * In this case, the values can simply be read from the channel header.
 */
struct visorchannel *visorchannel_create(HOSTADDRESS physaddr,
					 ulong channel_bytes, uuid_le guid);
struct visorchannel *visorchannel_create_overlapped(ulong channel_bytes,
						    struct visorchannel *parent,
						    ulong off, uuid_le guid);
struct visorchannel *visorchannel_create_with_lock(HOSTADDRESS physaddr,
						   ulong channel_bytes,
						   uuid_le guid);
struct visorchannel *visorchannel_create_overlapped_with_lock(
				ulong channel_bytes,
				struct visorchannel *parent,
				ulong off, uuid_le guid);
void visorchannel_destroy(struct visorchannel *channel);
int visorchannel_read(struct visorchannel *channel, ulong offset,
		      void *local, ulong nbytes);
int visorchannel_write(struct visorchannel *channel, ulong offset,
		       void *local, ulong nbytes);
int visorchannel_clear(struct visorchannel *channel, ulong offset,
		       u8 ch, ulong nbytes);
BOOL visorchannel_signalremove(struct visorchannel *channel, u32 queue,
			       void *msg);
BOOL visorchannel_signalinsert(struct visorchannel *channel, u32 queue,
			       void *msg);
int visorchannel_signalqueue_slots_avail(struct visorchannel *channel,
					 u32 queue);
int visorchannel_signalqueue_max_slots(struct visorchannel *channel, u32 queue);
HOSTADDRESS visorchannel_get_physaddr(struct visorchannel *channel);
ulong visorchannel_get_nbytes(struct visorchannel *channel);
char *visorchannel_id(struct visorchannel *channel, char *s);
char *visorchannel_zoneid(struct visorchannel *channel, char *s);
u64 visorchannel_get_clientpartition(struct visorchannel *channel);
uuid_le visorchannel_get_uuid(struct visorchannel *channel);
struct memregion *visorchannel_get_memregion(struct visorchannel *channel);
char *visorchannel_uuid_id(uuid_le *guid, char *s);
void visorchannel_debug(struct visorchannel *channel, int num_queues,
			struct seq_file *seq, u32 off);
void visorchannel_dump_section(struct visorchannel *chan, char *s,
			       int off, int len, struct seq_file *seq);
void __iomem *visorchannel_get_header(struct visorchannel *channel);

#endif
