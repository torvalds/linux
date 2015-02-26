/* visorchannel_funcs.c
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

/*
 *  This provides Supervisor channel communication primitives, which are
 *  independent of the mechanism used to access the channel data.  All channel
 *  data is accessed using the memregion abstraction.  (memregion has both
 *  a CM2 implementation and a direct memory implementation.)
 */

#include "globals.h"
#include "visorchannel.h"
#include <linux/uuid.h>

#define MYDRVNAME "visorchannel"

struct visorchannel {
	struct memregion *memregion;	/* from visor_memregion_create() */
	struct channel_header chan_hdr;
	uuid_le guid;
	ulong size;
	BOOL needs_lock;	/* channel creator knows if more than one
				 * thread will be inserting or removing */
	spinlock_t insert_lock; /* protect head writes in chan_hdr */
	spinlock_t remove_lock;	/* protect tail writes in chan_hdr */

	struct {
		struct signal_queue_header req_queue;
		struct signal_queue_header rsp_queue;
		struct signal_queue_header event_queue;
		struct signal_queue_header ack_queue;
	} safe_uis_queue;
};

/* Creates the struct visorchannel abstraction for a data area in memory,
 * but does NOT modify this data area.
 */
static struct visorchannel *
visorchannel_create_guts(HOSTADDRESS physaddr, ulong channel_bytes,
			 struct visorchannel *parent, ulong off, uuid_le guid,
			 BOOL needs_lock)
{
	struct visorchannel *p = NULL;
	void *rc = NULL;

	p = kmalloc(sizeof(*p), GFP_KERNEL|__GFP_NORETRY);
	if (p == NULL) {
		ERRDRV("allocation failed: (status=0)\n");
		rc = NULL;
		goto cleanup;
	}
	p->memregion = NULL;
	p->needs_lock = needs_lock;
	spin_lock_init(&p->insert_lock);
	spin_lock_init(&p->remove_lock);

	/* prepare chan_hdr (abstraction to read/write channel memory) */
	if (parent == NULL)
		p->memregion =
		    visor_memregion_create(physaddr,
					   sizeof(struct channel_header));
	else
		p->memregion =
		    visor_memregion_create_overlapped(parent->memregion,
				off, sizeof(struct channel_header));
	if (p->memregion == NULL) {
		ERRDRV("visor_memregion_create failed failed: (status=0)\n");
		rc = NULL;
		goto cleanup;
	}
	if (visor_memregion_read(p->memregion, 0, &p->chan_hdr,
				 sizeof(struct channel_header)) < 0) {
		ERRDRV("visor_memregion_read failed: (status=0)\n");
		rc = NULL;
		goto cleanup;
	}
	if (channel_bytes == 0)
		/* we had better be a CLIENT of this channel */
		channel_bytes = (ulong)p->chan_hdr.size;
	if (uuid_le_cmp(guid, NULL_UUID_LE) == 0)
		/* we had better be a CLIENT of this channel */
		guid = p->chan_hdr.chtype;
	if (visor_memregion_resize(p->memregion, channel_bytes) < 0) {
		ERRDRV("visor_memregion_resize failed: (status=0)\n");
		rc = NULL;
		goto cleanup;
	}
	p->size = channel_bytes;
	p->guid = guid;

	rc = p;
cleanup:

	if (rc == NULL) {
		if (p != NULL) {
			visorchannel_destroy(p);
			p = NULL;
		}
	}
	return rc;
}

struct visorchannel *
visorchannel_create(HOSTADDRESS physaddr, ulong channel_bytes, uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channel_bytes, NULL, 0, guid,
					FALSE);
}
EXPORT_SYMBOL_GPL(visorchannel_create);

struct visorchannel *
visorchannel_create_with_lock(HOSTADDRESS physaddr, ulong channel_bytes,
			      uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channel_bytes, NULL, 0, guid,
					TRUE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_with_lock);

struct visorchannel *
visorchannel_create_overlapped(ulong channel_bytes,
			       struct visorchannel *parent, ulong off,
			       uuid_le guid)
{
	return visorchannel_create_guts(0, channel_bytes, parent, off, guid,
					FALSE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_overlapped);

struct visorchannel *
visorchannel_create_overlapped_with_lock(ulong channel_bytes,
					 struct visorchannel *parent, ulong off,
					 uuid_le guid)
{
	return visorchannel_create_guts(0, channel_bytes, parent, off, guid,
					TRUE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_overlapped_with_lock);

void
visorchannel_destroy(struct visorchannel *channel)
{
	if (channel == NULL)
		return;
	if (channel->memregion != NULL) {
		visor_memregion_destroy(channel->memregion);
		channel->memregion = NULL;
	}
	kfree(channel);
}
EXPORT_SYMBOL_GPL(visorchannel_destroy);

HOSTADDRESS
visorchannel_get_physaddr(struct visorchannel *channel)
{
	return visor_memregion_get_physaddr(channel->memregion);
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

HOSTADDRESS
visorchannel_get_clientpartition(struct visorchannel *channel)
{
	return channel->chan_hdr.partition_handle;
}
EXPORT_SYMBOL_GPL(visorchannel_get_clientpartition);

uuid_le
visorchannel_get_uuid(struct visorchannel *channel)
{
	return channel->guid;
}
EXPORT_SYMBOL_GPL(visorchannel_get_uuid);

struct memregion *
visorchannel_get_memregion(struct visorchannel *channel)
{
	return channel->memregion;
}
EXPORT_SYMBOL_GPL(visorchannel_get_memregion);

int
visorchannel_read(struct visorchannel *channel, ulong offset,
		  void *local, ulong nbytes)
{
	int rc = visor_memregion_read(channel->memregion, offset,
				      local, nbytes);
	if ((rc >= 0) && (offset == 0) &&
	    (nbytes >= sizeof(struct channel_header))) {
		memcpy(&channel->chan_hdr, local,
		       sizeof(struct channel_header));
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_read);

int
visorchannel_write(struct visorchannel *channel, ulong offset,
		   void *local, ulong nbytes)
{
	if (offset == 0 && nbytes >= sizeof(struct channel_header))
		memcpy(&channel->chan_hdr, local,
		       sizeof(struct channel_header));
	return visor_memregion_write(channel->memregion, offset, local, nbytes);
}
EXPORT_SYMBOL_GPL(visorchannel_write);

int
visorchannel_clear(struct visorchannel *channel, ulong offset, u8 ch,
		   ulong nbytes)
{
	int rc = -1;
	int bufsize = 65536;
	int written = 0;
	u8 *buf = vmalloc(bufsize);

	if (buf == NULL) {
		ERRDRV("%s failed memory allocation", __func__);
		goto cleanup;
	}
	memset(buf, ch, bufsize);
	while (nbytes > 0) {
		ulong thisbytes = bufsize;
		int x = -1;

		if (nbytes < thisbytes)
			thisbytes = nbytes;
		x = visor_memregion_write(channel->memregion, offset + written,
					  buf, thisbytes);
		if (x < 0) {
			rc = x;
			goto cleanup;
		}
		written += thisbytes;
		nbytes -= thisbytes;
	}
	rc = 0;

cleanup:
	if (buf != NULL) {
		vfree(buf);
		buf = NULL;
	}
	return rc;
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
#define SIG_WRITE_FIELD(channel, queue, sig_hdr, FIELD)			\
	(visor_memregion_write(channel->memregion,			\
			       SIG_QUEUE_OFFSET(&channel->chan_hdr, queue)+ \
			       offsetof(struct signal_queue_header, FIELD),\
			       &((sig_hdr)->FIELD),			\
			       sizeof((sig_hdr)->FIELD)) >= 0)

static BOOL
sig_read_header(struct visorchannel *channel, u32 queue,
		struct signal_queue_header *sig_hdr)
{
	BOOL rc = FALSE;

	if (channel->chan_hdr.ch_space_offset < sizeof(struct channel_header)) {
		ERRDRV("oChannelSpace too small: (status=%d)\n", rc);
		goto cleanup;
	}

	/* Read the appropriate SIGNAL_QUEUE_HEADER into local memory. */

	if (visor_memregion_read(channel->memregion,
				 SIG_QUEUE_OFFSET(&channel->chan_hdr, queue),
				 sig_hdr,
				 sizeof(struct signal_queue_header)) < 0) {
		ERRDRV("queue=%d SIG_QUEUE_OFFSET=%d",
		       queue, (int)SIG_QUEUE_OFFSET(&channel->chan_hdr, queue));
		ERRDRV("visor_memregion_read of signal queue failed: (status=%d)\n",
		       rc);
		goto cleanup;
	}
	rc = TRUE;
cleanup:
	return rc;
}

static BOOL
sig_do_data(struct visorchannel *channel, u32 queue,
	    struct signal_queue_header *sig_hdr, u32 slot, void *data,
	    BOOL is_write)
{
	BOOL rc = FALSE;
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);
	if (is_write) {
		if (visor_memregion_write(channel->memregion,
					  signal_data_offset,
					  data, sig_hdr->signal_size) < 0) {
			ERRDRV("visor_memregion_write of signal data failed: (status=%d)\n",
			       rc);
			goto cleanup;
		}
	} else {
		if (visor_memregion_read(channel->memregion, signal_data_offset,
					 data, sig_hdr->signal_size) < 0) {
			ERRDRV("visor_memregion_read of signal data failed: (status=%d)\n",
			       rc);
			goto cleanup;
		}
	}
	rc = TRUE;
cleanup:
	return rc;
}

static inline BOOL
sig_read_data(struct visorchannel *channel, u32 queue,
	      struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	return sig_do_data(channel, queue, sig_hdr, slot, data, FALSE);
}

static inline BOOL
sig_write_data(struct visorchannel *channel, u32 queue,
	       struct signal_queue_header *sig_hdr, u32 slot, void *data)
{
	return sig_do_data(channel, queue, sig_hdr, slot, data, TRUE);
}

static inline unsigned char
safe_sig_queue_validate(struct signal_queue_header *psafe_sqh,
			struct signal_queue_header *punsafe_sqh,
			u32 *phead, u32 *ptail)
{
	if ((*phead >= psafe_sqh->max_slots) ||
	    (*ptail >= psafe_sqh->max_slots)) {
		/* Choose 0 or max, maybe based on current tail value */
		*phead = 0;
		*ptail = 0;

		/* Sync with client as necessary */
		punsafe_sqh->head = *phead;
		punsafe_sqh->tail = *ptail;

		ERRDRV("safe_sig_queue_validate: head = 0x%x, tail = 0x%x, MaxSlots = 0x%x",
		       *phead, *ptail, psafe_sqh->max_slots);
		return 0;
	}
	return 1;
}				/* end safe_sig_queue_validate */

static BOOL
signalremove_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr)) {
		return FALSE;
	}
	if (sig_hdr.head == sig_hdr.tail)
		return FALSE;	/* no signals to remove */

	sig_hdr.tail = (sig_hdr.tail + 1) % sig_hdr.max_slots;
	if (!sig_read_data(channel, queue, &sig_hdr, sig_hdr.tail, msg)) {
		ERRDRV("sig_read_data failed\n");
		return FALSE;
	}
	sig_hdr.num_received++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, tail)) {
		ERRDRV("visor_memregion_write of Tail failed\n");
		return FALSE;
	}
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_received)) {
		ERRDRV("visor_memregion_write of NumSignalsReceived failed\n");
		return FALSE;
	}
	return TRUE;
}

BOOL
visorchannel_signalremove(struct visorchannel *channel, u32 queue, void *msg)
{
	BOOL rc;

	if (channel->needs_lock) {
		spin_lock(&channel->remove_lock);
		rc = signalremove_inner(channel, queue, msg);
		spin_unlock(&channel->remove_lock);
	} else {
		rc = signalremove_inner(channel, queue, msg);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalremove);

static BOOL
signalinsert_inner(struct visorchannel *channel, u32 queue, void *msg)
{
	struct signal_queue_header sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr)) {
		return FALSE;
	}

	sig_hdr.head = ((sig_hdr.head + 1) % sig_hdr.max_slots);
	if (sig_hdr.head == sig_hdr.tail) {
		sig_hdr.num_overflows++;
		if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_overflows))
			ERRDRV("visor_memregion_write of NumOverflows failed\n");

		return FALSE;
	}

	if (!sig_write_data(channel, queue, &sig_hdr, sig_hdr.head, msg)) {
		ERRDRV("sig_write_data failed\n");
		return FALSE;
	}
	sig_hdr.num_sent++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, head)) {
		ERRDRV("visor_memregion_write of Head failed\n");
		return FALSE;
	}
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, num_sent)) {
		ERRDRV("visor_memregion_write of NumSignalsSent failed\n");
		return FALSE;
	}

	return TRUE;
}

BOOL
visorchannel_signalinsert(struct visorchannel *channel, u32 queue, void *msg)
{
	BOOL rc;

	if (channel->needs_lock) {
		spin_lock(&channel->insert_lock);
		rc = signalinsert_inner(channel, queue, msg);
		spin_unlock(&channel->insert_lock);
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
	slots_used = (head - tail);
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
	HOSTADDRESS addr = 0;
	ulong nbytes = 0, nbytes_region = 0;
	struct memregion *memregion = NULL;
	struct channel_header hdr;
	struct channel_header *phdr = &hdr;
	int i = 0;
	int errcode = 0;

	if (channel == NULL) {
		ERRDRV("%s no channel", __func__);
		return;
	}
	memregion = channel->memregion;
	if (memregion == NULL) {
		ERRDRV("%s no memregion", __func__);
		return;
	}
	addr = visor_memregion_get_physaddr(memregion);
	nbytes_region = visor_memregion_get_nbytes(memregion);
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

void
visorchannel_dump_section(struct visorchannel *chan, char *s,
			  int off, int len, struct seq_file *seq)
{
	char *buf, *tbuf, *fmtbuf;
	int fmtbufsize = 0;
	int i;
	int errcode = 0;

	fmtbufsize = 100 * COVQ(len, 16);
	buf = kmalloc(len, GFP_KERNEL|__GFP_NORETRY);
	if (!buf)
		return;
	fmtbuf = kmalloc(fmtbufsize, GFP_KERNEL|__GFP_NORETRY);
	if (!fmtbuf)
		goto fmt_failed;

	errcode = visorchannel_read(chan, off, buf, len);
	if (errcode < 0) {
		ERRDRV("%s failed to read %s from channel errcode=%d",
		       s, __func__, errcode);
		goto read_failed;
	}
	seq_printf(seq, "channel %s:\n", s);
	tbuf = buf;
	while (len > 0) {
		i = (len < 16) ? len : 16;
		hex_dump_to_buffer(tbuf, i, 16, 1, fmtbuf, fmtbufsize, TRUE);
		seq_printf(seq, "%s\n", fmtbuf);
		tbuf += 16;
		len -= 16;
	}

read_failed:
	kfree(fmtbuf);
fmt_failed:
	kfree(buf);
}
EXPORT_SYMBOL_GPL(visorchannel_dump_section);
