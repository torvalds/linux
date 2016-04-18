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
 *  This provides Supervisor channel communication primitives, which are
 *  independent of the mechanism used to access the channel data.
 */

#include <linux/uuid.h>
#include <linux/io.h>

#include "version.h"
#include "visorbus.h"
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
	ulong size;
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

/* Creates the struct visorchannel abstraction for a data area in memory,
 * but does NOT modify this data area.
 */
static struct visorchannel *
visorchannel_create_guts(u64 physaddr, unsigned long channel_bytes,
			 gfp_t gfp, unsigned long off,
			 uuid_le guid, bool needs_lock)
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

	/* Video driver constains the efi framebuffer so it will get a
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

	channel->size = channel_bytes;
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
	return visorchannel_create_guts(physaddr, channel_bytes, gfp, 0, guid,
					false);
}
EXPORT_SYMBOL_GPL(visorchannel_create);

struct visorchannel *
visorchannel_create_with_lock(u64 physaddr, unsigned long channel_bytes,
			      gfp_t gfp, uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channel_bytes, gfp, 0, guid,
					true);
}
EXPORT_SYMBOL_GPL(visorchannel_create_with_lock);

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
EXPORT_SYMBOL_GPL(visorchannel_destroy);

u64
visorchannel_get_physaddr(struct visorchannel *channel)
{
	return channel->physaddr;
}
EXPORT_SYMBOL_GPL(visorchannel_get_physaddr);

ulong
visorchannel_get_nbytes(struct visorchannel *channel)
{
	return channel->size;
}
EXPORT_SYMBOL_GPL(visorchannel_get_nbytes);

char *
visorchannel_uuid_id(uuid_le *guid, char *s)
{
	sprintf(s, "%pUL", guid);
	return s;
}
EXPORT_SYMBOL_GPL(visorchannel_uuid_id);

char *
visorchannel_id(struct visorchannel *channel, char *s)
{
	return visorchannel_uuid_id(&channel->guid, s);
}
EXPORT_SYMBOL_GPL(visorchannel_id);

char *
visorchannel_zoneid(struct visorchannel *channel, char *s)
{
	return visorchannel_uuid_id(&channel->chan_hdr.zone_uuid, s);
}
EXPORT_SYMBOL_GPL(visorchannel_zoneid);

u64
visorchannel_get_clientpartition(struct visorchannel *channel)
{
	return channel->chan_hdr.partition_handle;
}
EXPORT_SYMBOL_GPL(visorchannel_get_clientpartition);

int
visorchannel_set_clientpartition(struct visorchannel *channel,
				 u64 partition_handle)
{
	channel->chan_hdr.partition_handle = partition_handle;
	return 0;
}
EXPORT_SYMBOL_GPL(visorchannel_set_clientpartition);

uuid_le
visorchannel_get_uuid(struct visorchannel *channel)
{
	return channel->guid;
}
EXPORT_SYMBOL_GPL(visorchannel_get_uuid);

int
visorchannel_read(struct visorchannel *channel, ulong offset,
		  void *local, ulong nbytes)
{
	if (offset + nbytes > channel->nbytes)
		return -EIO;

	memcpy(local, channel->mapped + offset, nbytes);

	return 0;
}
EXPORT_SYMBOL_GPL(visorchannel_read);

int
visorchannel_write(struct visorchannel *channel, ulong offset,
		   void *local, ulong nbytes)
{
	size_t chdr_size = sizeof(struct channel_header);
	size_t copy_size;

	if (offset + nbytes > channel->nbytes)
		return -EIO;

	if (offset < chdr_size) {
		copy_size = min(chdr_size - offset, nbytes);
		memcpy(((char *)(&channel->chan_hdr)) + offset,
		       local, copy_size);
	}

	memcpy(channel->mapped + offset, local, nbytes);

	return 0;
}
EXPORT_SYMBOL_GPL(visorchannel_write);

int
visorchannel_clear(struct visorchannel *channel, ulong offset, u8 ch,
		   ulong nbytes)
{
	int err;
	int bufsize = PAGE_SIZE;
	int written = 0;
	u8 *buf;

	buf = (u8 *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(buf, ch, bufsize);

	while (nbytes > 0) {
		int thisbytes = bufsize;

		if (nbytes < thisbytes)
			thisbytes = nbytes;
		err = visorchannel_write(channel, offset + written,
					 buf, thisbytes);
		if (err)
			goto out_free_page;

		written += thisbytes;
		nbytes -= thisbytes;
	}
	err = 0;

out_free_page:
	free_page((unsigned long)buf);
	return err;
}
EXPORT_SYMBOL_GPL(visorchannel_clear);

void __iomem  *
visorchannel_get_header(struct visorchannel *channel)
{
	return (void __iomem *)&channel->chan_hdr;
}
EXPORT_SYMBOL_GPL(visorchannel_get_header);

/** Return offset of a specific SIGNAL_QUEUE_HEADER from the beginning of a
 *  channel header
 */
#define SIG_QUEUE_OFFSET(chan_hdr, q) \
	((chan_hdr)->ch_space_offset + \
	 ((q) * sizeof(struct signal_queue_header)))

/** Return offset of a specific queue entry (data) from the beginning of a
 *  channel header
 */
#define SIG_DATA_OFFSET(chan_hdr, q, sig_hdr, slot) \
	(SIG_QUEUE_OFFSET(chan_hdr, q) + (sig_hdr)->sig_base_offset + \
	    ((slot) * (sig_hdr)->signal_size))

/** Write the contents of a specific field within a SIGNAL_QUEUE_HEADER back
 *  into host memory
 */
#define SIG_WRITE_FIELD(channel, queue, sig_hdr, FIELD)			 \
	(visorchannel_write(channel,					 \
			    SIG_QUEUE_OFFSET(&channel->chan_hdr, queue) +\
			    offsetof(struct signal_queue_header, FIELD), \
			    &((sig_hdr)->FIELD),			 \
			    sizeof((sig_hdr)->FIELD)) >= 0)

static bool
sig_read_header(struct visorchannel *channel, u32 queue,
		struct signal_queue_header *sig_hdr)
{
	int err;

	if (channel->chan_hdr.ch_space_offset < sizeof(struct channel_header))
		return false;

	/* Read the appropriate SIGNAL_QUEUE_HEADER into local memory. */
	err = visorchannel_read(channel,
				SIG_QUEUE_OFFSET(&channel->chan_hdr, queue),
				sig_hdr, sizeof(struct signal_queue_header));
	if (err)
		return false;

	return true;
}

static inline bool
sig_read_data(struct visorchannel *channel, u32 queue,
	      struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	int err;
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);

	err = visorchannel_read(channel, signal_data_offset,
				data, sig_hdr->signal_size);
	if (err)
		return false;

	return true;
}

static inline bool
sig_write_data(struct visorchannel *channel, u32 queue,
	       struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	int err;
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);

	err = visorchannel_write(channel, signal_data_offset,
				 data, sig_hdr->signal_size);
	if (err)
		return false;

	return true;
}

static bool
signalremove_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return false;
	if (sig_hdr.head == sig_hdr.tail)
		return false;	/* no signals to remove */

	sig_hdr.tail = (sig_hdr.tail + 1) % sig_hdr.max_slots;
	if (!sig_read_data(channel, queue, &sig_hdr, sig_hdr.tail, msg))
		return false;
	sig_hdr.num_received++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, tail))
		return false;
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_received))
		return false;
	return true;
}

bool
visorchannel_signalremove(struct visorchannel *channel, u32 queue, void *msg)
{
	bool rc;
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

bool
visorchannel_signalempty(struct visorchannel *channel, u32 queue)
{
	unsigned long flags = 0;
	struct signal_queue_header sig_hdr;
	bool rc = false;

	if (channel->needs_lock)
		spin_lock_irqsave(&channel->remove_lock, flags);

	if (!sig_read_header(channel, queue, &sig_hdr))
		rc = true;
	if (sig_hdr.head == sig_hdr.tail)
		rc = true;
	if (channel->needs_lock)
		spin_unlock_irqrestore(&channel->remove_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalempty);

static bool
signalinsert_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return false;

	sig_hdr.head = (sig_hdr.head + 1) % sig_hdr.max_slots;
	if (sig_hdr.head == sig_hdr.tail) {
		sig_hdr.num_overflows++;
		visorchannel_write(channel,
				   SIG_QUEUE_OFFSET(&channel->chan_hdr, queue) +
				   offsetof(struct signal_queue_header,
					    num_overflows),
				   &sig_hdr.num_overflows,
				   sizeof(sig_hdr.num_overflows));
		return false;
	}

	if (!sig_write_data(channel, queue, &sig_hdr, sig_hdr.head, msg))
		return false;

	sig_hdr.num_sent++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, head))
		return false;
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_sent))
		return false;

	return true;
}

bool
visorchannel_signalinsert(struct visorchannel *channel, u32 queue, void *msg)
{
	bool rc;
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

int
visorchannel_signalqueue_slots_avail(struct visorchannel *channel, u32 queue)
{
	struct signal_queue_header sig_hdr;
	u32 slots_avail, slots_used;
	u32 head, tail;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return 0;
	head = sig_hdr.head;
	tail = sig_hdr.tail;
	if (head < tail)
		head = head + sig_hdr.max_slots;
	slots_used = head - tail;
	slots_avail = sig_hdr.max_signals - slots_used;
	return (int)slots_avail;
}
EXPORT_SYMBOL_GPL(visorchannel_signalqueue_slots_avail);

int
visorchannel_signalqueue_max_slots(struct visorchannel *channel, u32 queue)
{
	struct signal_queue_header sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return 0;
	return (int)sig_hdr.max_signals;
}
EXPORT_SYMBOL_GPL(visorchannel_signalqueue_max_slots);

static void
sigqueue_debug(struct signal_queue_header *q, int which, struct seq_file *seq)
{
	seq_printf(seq, "Signal Queue #%d\n", which);
	seq_printf(seq, "   VersionId          = %lu\n", (ulong)q->version);
	seq_printf(seq, "   Type               = %lu\n", (ulong)q->chtype);
	seq_printf(seq, "   oSignalBase        = %llu\n",
		   (long long)q->sig_base_offset);
	seq_printf(seq, "   SignalSize         = %lu\n", (ulong)q->signal_size);
	seq_printf(seq, "   MaxSignalSlots     = %lu\n",
		   (ulong)q->max_slots);
	seq_printf(seq, "   MaxSignals         = %lu\n", (ulong)q->max_signals);
	seq_printf(seq, "   FeatureFlags       = %-16.16Lx\n",
		   (long long)q->features);
	seq_printf(seq, "   NumSignalsSent     = %llu\n",
		   (long long)q->num_sent);
	seq_printf(seq, "   NumSignalsReceived = %llu\n",
		   (long long)q->num_received);
	seq_printf(seq, "   NumOverflows       = %llu\n",
		   (long long)q->num_overflows);
	seq_printf(seq, "   Head               = %lu\n", (ulong)q->head);
	seq_printf(seq, "   Tail               = %lu\n", (ulong)q->tail);
}

void
visorchannel_debug(struct visorchannel *channel, int num_queues,
		   struct seq_file *seq, u32 off)
{
	u64 addr = 0;
	ulong nbytes = 0, nbytes_region = 0;
	struct channel_header hdr;
	struct channel_header *phdr = &hdr;
	int i = 0;
	int errcode = 0;

	if (!channel)
		return;

	addr = visorchannel_get_physaddr(channel);
	nbytes_region = visorchannel_get_nbytes(channel);
	errcode = visorchannel_read(channel, off,
				    phdr, sizeof(struct channel_header));
	if (errcode < 0) {
		seq_printf(seq,
			   "Read of channel header failed with errcode=%d)\n",
			   errcode);
		if (off == 0) {
			phdr = &channel->chan_hdr;
			seq_puts(seq, "(following data may be stale)\n");
		} else {
			return;
		}
	}
	nbytes = (ulong)(phdr->size);
	seq_printf(seq, "--- Begin channel @0x%-16.16Lx for 0x%lx bytes (region=0x%lx bytes) ---\n",
		   addr + off, nbytes, nbytes_region);
	seq_printf(seq, "Type            = %pUL\n", &phdr->chtype);
	seq_printf(seq, "ZoneGuid        = %pUL\n", &phdr->zone_uuid);
	seq_printf(seq, "Signature       = 0x%-16.16Lx\n",
		   (long long)phdr->signature);
	seq_printf(seq, "LegacyState     = %lu\n", (ulong)phdr->legacy_state);
	seq_printf(seq, "SrvState        = %lu\n", (ulong)phdr->srv_state);
	seq_printf(seq, "CliStateBoot    = %lu\n", (ulong)phdr->cli_state_boot);
	seq_printf(seq, "CliStateOS      = %lu\n", (ulong)phdr->cli_state_os);
	seq_printf(seq, "HeaderSize      = %lu\n", (ulong)phdr->header_size);
	seq_printf(seq, "Size            = %llu\n", (long long)phdr->size);
	seq_printf(seq, "Features        = 0x%-16.16llx\n",
		   (long long)phdr->features);
	seq_printf(seq, "PartitionHandle = 0x%-16.16llx\n",
		   (long long)phdr->partition_handle);
	seq_printf(seq, "Handle          = 0x%-16.16llx\n",
		   (long long)phdr->handle);
	seq_printf(seq, "VersionId       = %lu\n", (ulong)phdr->version_id);
	seq_printf(seq, "oChannelSpace   = %llu\n",
		   (long long)phdr->ch_space_offset);
	if ((phdr->ch_space_offset == 0) || (errcode < 0))
		;
	else
		for (i = 0; i < num_queues; i++) {
			struct signal_queue_header q;

			errcode = visorchannel_read(channel,
						    off +
						    phdr->ch_space_offset +
						    (i * sizeof(q)),
						    &q, sizeof(q));
			if (errcode < 0) {
				seq_printf(seq,
					   "failed to read signal queue #%d from channel @0x%-16.16Lx errcode=%d\n",
					   i, addr, errcode);
				continue;
			}
			sigqueue_debug(&q, i, seq);
		}
	seq_printf(seq, "--- End   channel @0x%-16.16Lx for 0x%lx bytes ---\n",
		   addr + off, nbytes);
}
EXPORT_SYMBOL_GPL(visorchannel_debug);
