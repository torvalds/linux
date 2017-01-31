/* visorchannel_funcs.c
 *
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

/*
 *  This provides s-Par channel communication primitives, which are
 *  independent of the mechanism used to access the channel data.
 */

#include <linux/uuid.h>
#include <linux/io.h>

#include "visorbus.h"
#include "visorbus_private.h"
#include "controlvmchannel.h"

#define MYDRVNAME "visorchannel"

#define SPAR_CONSOLEVIDEO_CHANNEL_PROTOCOL_GUID \
	UUID_LE(0x3cd6e705, 0xd6a2, 0x4aa5,           \
		0xad, 0x5c, 0x7b, 0x8, 0x88, 0x9d, 0xff, 0xe2)
static const uuid_le spar_video_guid = SPAR_CONSOLEVIDEO_CHANNEL_PROTOCOL_GUID;

struct visorchannel {
	u64 physaddr;
	ulong nbytes;
	void *mapped;
	bool requested;
	struct channel_header chan_hdr;
	uuid_le guid;
	bool needs_lock;	/* channel creator knows if more than one */
				/* thread will be inserting or removing */
	spinlock_t insert_lock; /* protect head writes in chan_hdr */
	spinlock_t remove_lock;	/* protect tail writes in chan_hdr */

	struct {
		struct signal_queue_header req_queue;
		struct signal_queue_header rsp_queue;
		struct signal_queue_header event_queue;
		struct signal_queue_header ack_queue;
	} safe_uis_queue;
	uuid_le type;
	uuid_le inst;
};

void
visorchannel_destroy(struct visorchannel *channel)
{
	if (!channel)
		return;
	if (channel->mapped) {
		memunmap(channel->mapped);
		if (channel->requested)
			release_mem_region(channel->physaddr, channel->nbytes);
	}
	kfree(channel);
}

u64
visorchannel_get_physaddr(struct visorchannel *channel)
{
	return channel->physaddr;
}

ulong
visorchannel_get_nbytes(struct visorchannel *channel)
{
	return channel->nbytes;
}

char *
visorchannel_uuid_id(uuid_le *guid, char *s)
{
	sprintf(s, "%pUL", guid);
	return s;
}

char *
visorchannel_id(struct visorchannel *channel, char *s)
{
	return visorchannel_uuid_id(&channel->guid, s);
}

char *
visorchannel_zoneid(struct visorchannel *channel, char *s)
{
	return visorchannel_uuid_id(&channel->chan_hdr.zone_uuid, s);
}

u64
visorchannel_get_clientpartition(struct visorchannel *channel)
{
	return channel->chan_hdr.partition_handle;
}

int
visorchannel_set_clientpartition(struct visorchannel *channel,
				 u64 partition_handle)
{
	channel->chan_hdr.partition_handle = partition_handle;
	return 0;
}

/**
 * visorchannel_get_uuid() - queries the UUID of the designated channel
 * @channel: the channel to query
 *
 * Return: the UUID of the provided channel
 */
uuid_le
visorchannel_get_uuid(struct visorchannel *channel)
{
	return channel->guid;
}
EXPORT_SYMBOL_GPL(visorchannel_get_uuid);

int
visorchannel_read(struct visorchannel *channel, ulong offset,
		  void *dest, ulong nbytes)
{
	if (offset + nbytes > channel->nbytes)
		return -EIO;

	memcpy(dest, channel->mapped + offset, nbytes);

	return 0;
}

int
visorchannel_write(struct visorchannel *channel, ulong offset,
		   void *dest, ulong nbytes)
{
	size_t chdr_size = sizeof(struct channel_header);
	size_t copy_size;

	if (offset + nbytes > channel->nbytes)
		return -EIO;

	if (offset < chdr_size) {
		copy_size = min(chdr_size - offset, nbytes);
		memcpy(((char *)(&channel->chan_hdr)) + offset,
		       dest, copy_size);
	}

	memcpy(channel->mapped + offset, dest, nbytes);

	return 0;
}

void __iomem  *
visorchannel_get_header(struct visorchannel *channel)
{
	return (void __iomem *)&channel->chan_hdr;
}

/*
 * Return offset of a specific SIGNAL_QUEUE_HEADER from the beginning of a
 * channel header
 */
#define SIG_QUEUE_OFFSET(chan_hdr, q) \
	((chan_hdr)->ch_space_offset + \
	 ((q) * sizeof(struct signal_queue_header)))

/*
 * Return offset of a specific queue entry (data) from the beginning of a
 * channel header
 */
#define SIG_DATA_OFFSET(chan_hdr, q, sig_hdr, slot) \
	(SIG_QUEUE_OFFSET(chan_hdr, q) + (sig_hdr)->sig_base_offset + \
	    ((slot) * (sig_hdr)->signal_size))

/*
 * Write the contents of a specific field within a SIGNAL_QUEUE_HEADER back
 * into host memory
 */
#define SIG_WRITE_FIELD(channel, queue, sig_hdr, FIELD)			 \
	visorchannel_write(channel,					 \
			   SIG_QUEUE_OFFSET(&channel->chan_hdr, queue) +\
			   offsetof(struct signal_queue_header, FIELD), \
			   &((sig_hdr)->FIELD),			 \
			   sizeof((sig_hdr)->FIELD))

static int
sig_read_header(struct visorchannel *channel, u32 queue,
		struct signal_queue_header *sig_hdr)
{
	if (channel->chan_hdr.ch_space_offset < sizeof(struct channel_header))
		return -EINVAL;

	/* Read the appropriate SIGNAL_QUEUE_HEADER into local memory. */
	return visorchannel_read(channel,
				 SIG_QUEUE_OFFSET(&channel->chan_hdr, queue),
				 sig_hdr, sizeof(struct signal_queue_header));
}

static inline int
sig_read_data(struct visorchannel *channel, u32 queue,
	      struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);

	return visorchannel_read(channel, signal_data_offset,
				 data, sig_hdr->signal_size);
}

static inline int
sig_write_data(struct visorchannel *channel, u32 queue,
	       struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);

	return visorchannel_write(channel, signal_data_offset,
				  data, sig_hdr->signal_size);
}

static int
signalremove_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;
	int error;

	error = sig_read_header(channel, queue, &sig_hdr);
	if (error)
		return error;

	/* No signals to remove; have caller try again. */
	if (sig_hdr.head == sig_hdr.tail)
		return -EAGAIN;

	sig_hdr.tail = (sig_hdr.tail + 1) % sig_hdr.max_slots;

	error = sig_read_data(channel, queue, &sig_hdr, sig_hdr.tail, msg);
	if (error)
		return error;

	sig_hdr.num_received++;

	/*
	 * For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */

	error = SIG_WRITE_FIELD(channel, queue, &sig_hdr, tail);
	if (error)
		return error;
	error = SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_received);
	if (error)
		return error;

	return 0;
}

/**
 * visorchannel_signalremove() - removes a message from the designated
 *                               channel/queue
 * @channel: the channel the message will be removed from
 * @queue:   the queue the message will be removed from
 * @msg:     the message to remove
 *
 * Return: integer error code indicating the status of the removal
 */
int
visorchannel_signalremove(struct visorchannel *channel, u32 queue, void *msg)
{
	int rc;
	unsigned long flags;

	if (channel->needs_lock) {
		spin_lock_irqsave(&channel->remove_lock, flags);
		rc = signalremove_inner(channel, queue, msg);
		spin_unlock_irqrestore(&channel->remove_lock, flags);
	} else {
		rc = signalremove_inner(channel, queue, msg);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalremove);

/**
 * visorchannel_signalempty() - checks if the designated channel/queue
 *                              contains any messages
 * @channel: the channel to query
 * @queue:   the queue in the channel to query
 *
 * Return: boolean indicating whether any messages in the designated
 *         channel/queue are present
 */

static bool
queue_empty(struct visorchannel *channel, u32 queue)
{
	struct signal_queue_header sig_hdr;

	if (sig_read_header(channel, queue, &sig_hdr))
		return true;

	return (sig_hdr.head == sig_hdr.tail);
}

bool
visorchannel_signalempty(struct visorchannel *channel, u32 queue)
{
	bool rc;
	unsigned long flags;

	if (!channel->needs_lock)
		return queue_empty(channel, queue);

	spin_lock_irqsave(&channel->remove_lock, flags);
	rc = queue_empty(channel, queue);
	spin_unlock_irqrestore(&channel->remove_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalempty);

static int
signalinsert_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;
	int error;

	error = sig_read_header(channel, queue, &sig_hdr);
	if (error)
		return error;

	sig_hdr.head = (sig_hdr.head + 1) % sig_hdr.max_slots;
	if (sig_hdr.head == sig_hdr.tail) {
		sig_hdr.num_overflows++;
		visorchannel_write(channel,
				   SIG_QUEUE_OFFSET(&channel->chan_hdr, queue) +
				   offsetof(struct signal_queue_header,
					    num_overflows),
				   &sig_hdr.num_overflows,
				   sizeof(sig_hdr.num_overflows));
		return -EIO;
	}

	error = sig_write_data(channel, queue, &sig_hdr, sig_hdr.head, msg);
	if (error)
		return error;

	sig_hdr.num_sent++;

	/*
	 * For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */

	error = SIG_WRITE_FIELD(channel, queue, &sig_hdr, head);
	if (error)
		return error;
	error = SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_sent);
	if (error)
		return error;

	return 0;
}

/**
 * visorchannel_create_guts() - creates the struct visorchannel abstraction
 *                              for a data area in memory, but does NOT modify
 *                              this data area
 * @physaddr:      physical address of start of channel
 * @channel_bytes: size of the channel in bytes; this may 0 if the channel has
 *                 already been initialized in memory (which is true for all
 *                 channels provided to guest environments by the s-Par
 *                 back-end), in which case the actual channel size will be
 *                 read from the channel header in memory
 * @gfp:           gfp_t to use when allocating memory for the data struct
 * @guid:          uuid that identifies channel type; this may 0 if the channel
 *                 has already been initialized in memory (which is true for all
 *                 channels provided to guest environments by the s-Par
 *                 back-end), in which case the actual channel guid will be
 *                 read from the channel header in memory
 * @needs_lock:    must specify true if you have multiple threads of execution
 *                 that will be calling visorchannel methods of this
 *                 visorchannel at the same time
 *
 * Return: pointer to visorchannel that was created if successful,
 *         otherwise NULL
 */
static struct visorchannel *
visorchannel_create_guts(u64 physaddr, unsigned long channel_bytes,
			 gfp_t gfp, uuid_le guid, bool needs_lock)
{
	struct visorchannel *channel;
	int err;
	size_t size = sizeof(struct channel_header);

	if (physaddr == 0)
		return NULL;

	channel = kzalloc(sizeof(*channel), gfp);
	if (!channel)
		return NULL;

	channel->needs_lock = needs_lock;
	spin_lock_init(&channel->insert_lock);
	spin_lock_init(&channel->remove_lock);

	/*
	 * Video driver constains the efi framebuffer so it will get a
	 * conflict resource when requesting its full mem region. Since
	 * we are only using the efi framebuffer for video we can ignore
	 * this. Remember that we haven't requested it so we don't try to
	 * release later on.
	 */
	channel->requested = request_mem_region(physaddr, size, MYDRVNAME);
	if (!channel->requested) {
		if (uuid_le_cmp(guid, spar_video_guid)) {
			/* Not the video channel we care about this */
			goto err_destroy_channel;
		}
	}

	channel->mapped = memremap(physaddr, size, MEMREMAP_WB);
	if (!channel->mapped) {
		release_mem_region(physaddr, size);
		goto err_destroy_channel;
	}

	channel->physaddr = physaddr;
	channel->nbytes = size;

	err = visorchannel_read(channel, 0, &channel->chan_hdr,
				sizeof(struct channel_header));
	if (err)
		goto err_destroy_channel;

	/* we had better be a CLIENT of this channel */
	if (channel_bytes == 0)
		channel_bytes = (ulong)channel->chan_hdr.size;
	if (uuid_le_cmp(guid, NULL_UUID_LE) == 0)
		guid = channel->chan_hdr.chtype;

	memunmap(channel->mapped);
	if (channel->requested)
		release_mem_region(channel->physaddr, channel->nbytes);
	channel->mapped = NULL;
	channel->requested = request_mem_region(channel->physaddr,
						channel_bytes, MYDRVNAME);
	if (!channel->requested) {
		if (uuid_le_cmp(guid, spar_video_guid)) {
			/* Different we care about this */
			goto err_destroy_channel;
		}
	}

	channel->mapped = memremap(channel->physaddr, channel_bytes,
			MEMREMAP_WB);
	if (!channel->mapped) {
		release_mem_region(channel->physaddr, channel_bytes);
		goto err_destroy_channel;
	}

	channel->nbytes = channel_bytes;
	channel->guid = guid;
	return channel;

err_destroy_channel:
	visorchannel_destroy(channel);
	return NULL;
}

struct visorchannel *
visorchannel_create(u64 physaddr, unsigned long channel_bytes,
		    gfp_t gfp, uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channel_bytes, gfp, guid,
					false);
}

struct visorchannel *
visorchannel_create_with_lock(u64 physaddr, unsigned long channel_bytes,
			      gfp_t gfp, uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channel_bytes, gfp, guid,
					true);
}

/**
 * visorchannel_signalinsert() - inserts a message into the designated
 *                               channel/queue
 * @channel: the channel the message will be added to
 * @queue:   the queue the message will be added to
 * @msg:     the message to insert
 *
 * Return: integer error code indicating the status of the insertion
 */
int
visorchannel_signalinsert(struct visorchannel *channel, u32 queue, void *msg)
{
	int rc;
	unsigned long flags;

	if (channel->needs_lock) {
		spin_lock_irqsave(&channel->insert_lock, flags);
		rc = signalinsert_inner(channel, queue, msg);
		spin_unlock_irqrestore(&channel->insert_lock, flags);
	} else {
		rc = signalinsert_inner(channel, queue, msg);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalinsert);
