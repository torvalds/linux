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

struct VISORCHANNEL_Tag {
	MEMREGION *memregion;	/* from visor_memregion_create() */
	CHANNEL_HEADER chan_hdr;
	uuid_le guid;
	ulong size;
	BOOL needs_lock;
	spinlock_t insert_lock;
	spinlock_t remove_lock;

	struct {
		SIGNAL_QUEUE_HEADER req_queue;
		SIGNAL_QUEUE_HEADER rsp_queue;
		SIGNAL_QUEUE_HEADER event_queue;
		SIGNAL_QUEUE_HEADER ack_queue;
	} safe_uis_queue;
};

/* Creates the VISORCHANNEL abstraction for a data area in memory, but does
 * NOT modify this data area.
 */
static VISORCHANNEL *
visorchannel_create_guts(HOSTADDRESS physaddr, ulong channelBytes,
			 VISORCHANNEL *parent, ulong off, uuid_le guid,
			 BOOL needs_lock)
{
	VISORCHANNEL *p = NULL;
	void *rc = NULL;

	p = kmalloc(sizeof(VISORCHANNEL), GFP_KERNEL|__GFP_NORETRY);
	if (p == NULL) {
		ERRDRV("allocation failed: (status=0)\n");
		rc = NULL;
		goto Away;
	}
	p->memregion = NULL;
	p->needs_lock = needs_lock;
	spin_lock_init(&p->insert_lock);
	spin_lock_init(&p->remove_lock);

	/* prepare chan_hdr (abstraction to read/write channel memory) */
	if (parent == NULL)
		p->memregion =
		    visor_memregion_create(physaddr, sizeof(CHANNEL_HEADER));
	else
		p->memregion =
		    visor_memregion_create_overlapped(parent->memregion,
						      off,
						      sizeof(CHANNEL_HEADER));
	if (p->memregion == NULL) {
		ERRDRV("visor_memregion_create failed failed: (status=0)\n");
		rc = NULL;
		goto Away;
	}
	if (visor_memregion_read(p->memregion, 0, &p->chan_hdr,
				 sizeof(CHANNEL_HEADER)) < 0) {
		ERRDRV("visor_memregion_read failed: (status=0)\n");
		rc = NULL;
		goto Away;
	}
	if (channelBytes == 0)
		/* we had better be a CLIENT of this channel */
		channelBytes = (ulong) p->chan_hdr.Size;
	if (uuid_le_cmp(guid, NULL_UUID_LE) == 0)
		/* we had better be a CLIENT of this channel */
		guid = p->chan_hdr.Type;
	if (visor_memregion_resize(p->memregion, channelBytes) < 0) {
		ERRDRV("visor_memregion_resize failed: (status=0)\n");
		rc = NULL;
		goto Away;
	}
	p->size = channelBytes;
	p->guid = guid;

	rc = p;
Away:

	if (rc == NULL) {
		if (p != NULL) {
			visorchannel_destroy(p);
			p = NULL;
		}
	}
	return rc;
}

VISORCHANNEL *
visorchannel_create(HOSTADDRESS physaddr, ulong channelBytes, uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channelBytes, NULL, 0, guid,
					FALSE);
}
EXPORT_SYMBOL_GPL(visorchannel_create);

VISORCHANNEL *
visorchannel_create_with_lock(HOSTADDRESS physaddr, ulong channelBytes,
			      uuid_le guid)
{
	return visorchannel_create_guts(physaddr, channelBytes, NULL, 0, guid,
					TRUE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_with_lock);

VISORCHANNEL *
visorchannel_create_overlapped(ulong channelBytes,
			       VISORCHANNEL *parent, ulong off, uuid_le guid)
{
	return visorchannel_create_guts(0, channelBytes, parent, off, guid,
					FALSE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_overlapped);

VISORCHANNEL *
visorchannel_create_overlapped_with_lock(ulong channelBytes,
					 VISORCHANNEL *parent, ulong off,
					 uuid_le guid)
{
	return visorchannel_create_guts(0, channelBytes, parent, off, guid,
					TRUE);
}
EXPORT_SYMBOL_GPL(visorchannel_create_overlapped_with_lock);

void
visorchannel_destroy(VISORCHANNEL *channel)
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
visorchannel_get_physaddr(VISORCHANNEL *channel)
{
	return visor_memregion_get_physaddr(channel->memregion);
}
EXPORT_SYMBOL_GPL(visorchannel_get_physaddr);

ulong
visorchannel_get_nbytes(VISORCHANNEL *channel)
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
visorchannel_id(VISORCHANNEL *channel, char *s)
{
	return visorchannel_uuid_id(&channel->guid, s);
}
EXPORT_SYMBOL_GPL(visorchannel_id);

char *
visorchannel_zoneid(VISORCHANNEL *channel, char *s)
{
	return visorchannel_uuid_id(&channel->chan_hdr.ZoneGuid, s);
}
EXPORT_SYMBOL_GPL(visorchannel_zoneid);

HOSTADDRESS
visorchannel_get_clientpartition(VISORCHANNEL *channel)
{
	return channel->chan_hdr.PartitionHandle;
}
EXPORT_SYMBOL_GPL(visorchannel_get_clientpartition);

uuid_le
visorchannel_get_uuid(VISORCHANNEL *channel)
{
	return channel->guid;
}
EXPORT_SYMBOL_GPL(visorchannel_get_uuid);

MEMREGION *
visorchannel_get_memregion(VISORCHANNEL *channel)
{
	return channel->memregion;
}
EXPORT_SYMBOL_GPL(visorchannel_get_memregion);

int
visorchannel_read(VISORCHANNEL *channel, ulong offset,
		  void *local, ulong nbytes)
{
	int rc = visor_memregion_read(channel->memregion, offset,
				      local, nbytes);
	if ((rc >= 0) && (offset == 0) && (nbytes >= sizeof(CHANNEL_HEADER)))
		memcpy(&channel->chan_hdr, local, sizeof(CHANNEL_HEADER));
	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_read);

int
visorchannel_write(VISORCHANNEL *channel, ulong offset,
		   void *local, ulong nbytes)
{
	if (offset == 0 && nbytes >= sizeof(CHANNEL_HEADER))
		memcpy(&channel->chan_hdr, local, sizeof(CHANNEL_HEADER));
	return visor_memregion_write(channel->memregion, offset, local, nbytes);
}
EXPORT_SYMBOL_GPL(visorchannel_write);

int
visorchannel_clear(VISORCHANNEL *channel, ulong offset, u8 ch, ulong nbytes)
{
	int rc = -1;
	int bufsize = 65536;
	int written = 0;
	u8 *buf = vmalloc(bufsize);

	if (buf == NULL) {
		ERRDRV("%s failed memory allocation", __func__);
		goto Away;
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
			goto Away;
		}
		written += thisbytes;
		nbytes -= thisbytes;
	}
	rc = 0;

Away:
	if (buf != NULL) {
		vfree(buf);
		buf = NULL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_clear);

void __iomem  *
visorchannel_get_header(VISORCHANNEL *channel)
{
	return (void __iomem *) &(channel->chan_hdr);
}
EXPORT_SYMBOL_GPL(visorchannel_get_header);

/** Return offset of a specific SIGNAL_QUEUE_HEADER from the beginning of a
 *  channel header
 */
#define SIG_QUEUE_OFFSET(chan_hdr, q) \
	((chan_hdr)->oChannelSpace + ((q) * sizeof(SIGNAL_QUEUE_HEADER)))

/** Return offset of a specific queue entry (data) from the beginning of a
 *  channel header
 */
#define SIG_DATA_OFFSET(chan_hdr, q, sig_hdr, slot) \
	(SIG_QUEUE_OFFSET(chan_hdr, q) + (sig_hdr)->oSignalBase + \
	    ((slot) * (sig_hdr)->SignalSize))

/** Write the contents of a specific field within a SIGNAL_QUEUE_HEADER back
 *  into host memory
 */
#define SIG_WRITE_FIELD(channel, queue, sig_hdr, FIELD)			\
	(visor_memregion_write(channel->memregion,			\
			       SIG_QUEUE_OFFSET(&channel->chan_hdr, queue)+ \
			       offsetof(SIGNAL_QUEUE_HEADER, FIELD),	\
			       &((sig_hdr)->FIELD),			\
			       sizeof((sig_hdr)->FIELD)) >= 0)

static BOOL
sig_read_header(VISORCHANNEL *channel, u32 queue,
		SIGNAL_QUEUE_HEADER *sig_hdr)
{
	BOOL rc = FALSE;

	if (channel->chan_hdr.oChannelSpace < sizeof(CHANNEL_HEADER)) {
		ERRDRV("oChannelSpace too small: (status=%d)\n", rc);
		goto Away;
	}

	/* Read the appropriate SIGNAL_QUEUE_HEADER into local memory. */

	if (visor_memregion_read(channel->memregion,
				 SIG_QUEUE_OFFSET(&channel->chan_hdr, queue),
				 sig_hdr, sizeof(SIGNAL_QUEUE_HEADER)) < 0) {
		ERRDRV("queue=%d SIG_QUEUE_OFFSET=%d",
		       queue, (int)SIG_QUEUE_OFFSET(&channel->chan_hdr, queue));
		ERRDRV("visor_memregion_read of signal queue failed: (status=%d)\n", rc);
		goto Away;
	}
	rc = TRUE;
Away:
	return rc;
}

static BOOL
sig_do_data(VISORCHANNEL *channel, u32 queue,
	    SIGNAL_QUEUE_HEADER *sig_hdr, u32 slot, void *data, BOOL is_write)
{
	BOOL rc = FALSE;
	int signal_data_offset = SIG_DATA_OFFSET(&channel->chan_hdr, queue,
						 sig_hdr, slot);
	if (is_write) {
		if (visor_memregion_write(channel->memregion,
					  signal_data_offset,
					  data, sig_hdr->SignalSize) < 0) {
			ERRDRV("visor_memregion_write of signal data failed: (status=%d)\n", rc);
			goto Away;
		}
	} else {
		if (visor_memregion_read(channel->memregion, signal_data_offset,
					 data, sig_hdr->SignalSize) < 0) {
			ERRDRV("visor_memregion_read of signal data failed: (status=%d)\n", rc);
			goto Away;
		}
	}
	rc = TRUE;
Away:
	return rc;
}

static inline BOOL
sig_read_data(VISORCHANNEL *channel, u32 queue,
	      SIGNAL_QUEUE_HEADER *sig_hdr, u32 slot, void *data)
{
	return sig_do_data(channel, queue, sig_hdr, slot, data, FALSE);
}

static inline BOOL
sig_write_data(VISORCHANNEL *channel, u32 queue,
	       SIGNAL_QUEUE_HEADER *sig_hdr, u32 slot, void *data)
{
	return sig_do_data(channel, queue, sig_hdr, slot, data, TRUE);
}

static inline unsigned char
safe_sig_queue_validate(pSIGNAL_QUEUE_HEADER psafe_sqh,
			pSIGNAL_QUEUE_HEADER punsafe_sqh,
			u32 *phead, u32 *ptail)
{
	if ((*phead >= psafe_sqh->MaxSignalSlots)
	    || (*ptail >= psafe_sqh->MaxSignalSlots)) {
		/* Choose 0 or max, maybe based on current tail value */
		*phead = 0;
		*ptail = 0;

		/* Sync with client as necessary */
		punsafe_sqh->Head = *phead;
		punsafe_sqh->Tail = *ptail;

		ERRDRV("safe_sig_queue_validate: head = 0x%x, tail = 0x%x, MaxSlots = 0x%x",
		     *phead, *ptail, psafe_sqh->MaxSignalSlots);
		return 0;
	}
	return 1;
}				/* end safe_sig_queue_validate */

BOOL
visorchannel_signalremove(VISORCHANNEL *channel, u32 queue, void *msg)
{
	BOOL rc = FALSE;
	SIGNAL_QUEUE_HEADER sig_hdr;

	if (channel->needs_lock)
		spin_lock(&channel->remove_lock);

	if (!sig_read_header(channel, queue, &sig_hdr)) {
		rc = FALSE;
		goto Away;
	}
	if (sig_hdr.Head == sig_hdr.Tail) {
		rc = FALSE;	/* no signals to remove */
		goto Away;
	}
	sig_hdr.Tail = (sig_hdr.Tail + 1) % sig_hdr.MaxSignalSlots;
	if (!sig_read_data(channel, queue, &sig_hdr, sig_hdr.Tail, msg)) {
		ERRDRV("sig_read_data failed: (status=%d)\n", rc);
		goto Away;
	}
	sig_hdr.NumSignalsReceived++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, Tail)) {
		ERRDRV("visor_memregion_write of Tail failed: (status=%d)\n",
		       rc);
		goto Away;
	}
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, NumSignalsReceived)) {
		ERRDRV("visor_memregion_write of NumSignalsReceived failed: (status=%d)\n", rc);
		goto Away;
	}
	rc = TRUE;
Away:
	if (channel->needs_lock)
		spin_unlock(&channel->remove_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalremove);

BOOL
visorchannel_signalinsert(VISORCHANNEL *channel, u32 queue, void *msg)
{
	BOOL rc = FALSE;
	SIGNAL_QUEUE_HEADER sig_hdr;

	if (channel->needs_lock)
		spin_lock(&channel->insert_lock);

	if (!sig_read_header(channel, queue, &sig_hdr)) {
		rc = FALSE;
		goto Away;
	}

	sig_hdr.Head = ((sig_hdr.Head + 1) % sig_hdr.MaxSignalSlots);
	if (sig_hdr.Head == sig_hdr.Tail) {
		sig_hdr.NumOverflows++;
		if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, NumOverflows)) {
			ERRDRV("visor_memregion_write of NumOverflows failed: (status=%d)\n", rc);
			goto Away;
		}
		rc = FALSE;
		goto Away;
	}

	if (!sig_write_data(channel, queue, &sig_hdr, sig_hdr.Head, msg)) {
		ERRDRV("sig_write_data failed: (status=%d)\n", rc);
		goto Away;
	}
	sig_hdr.NumSignalsSent++;

	/* For each data field in SIGNAL_QUEUE_HEADER that was modified,
	 * update host memory.
	 */
	mb(); /* required for channel synch */
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, Head)) {
		ERRDRV("visor_memregion_write of Head failed: (status=%d)\n",
		       rc);
		goto Away;
	}
	if (!SIG_WRITE_FIELD(channel, queue, &sig_hdr, NumSignalsSent)) {
		ERRDRV("visor_memregion_write of NumSignalsSent failed: (status=%d)\n", rc);
		goto Away;
	}
	rc = TRUE;
Away:
	if (channel->needs_lock)
		spin_unlock(&channel->insert_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(visorchannel_signalinsert);


int
visorchannel_signalqueue_slots_avail(VISORCHANNEL *channel, u32 queue)
{
	SIGNAL_QUEUE_HEADER sig_hdr;
	u32 slots_avail, slots_used;
	u32 head, tail;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return 0;
	head = sig_hdr.Head;
	tail = sig_hdr.Tail;
	if (head < tail)
		head = head + sig_hdr.MaxSignalSlots;
	slots_used = (head - tail);
	slots_avail = sig_hdr.MaxSignals - slots_used;
	return (int) slots_avail;
}
EXPORT_SYMBOL_GPL(visorchannel_signalqueue_slots_avail);

int
visorchannel_signalqueue_max_slots(VISORCHANNEL *channel, u32 queue)
{
	SIGNAL_QUEUE_HEADER sig_hdr;

	if (!sig_read_header(channel, queue, &sig_hdr))
		return 0;
	return (int) sig_hdr.MaxSignals;
}
EXPORT_SYMBOL_GPL(visorchannel_signalqueue_max_slots);

static void
sigqueue_debug(SIGNAL_QUEUE_HEADER *q, int which, struct seq_file *seq)
{
	seq_printf(seq, "Signal Queue #%d\n", which);
	seq_printf(seq, "   VersionId          = %lu\n", (ulong) q->VersionId);
	seq_printf(seq, "   Type               = %lu\n", (ulong) q->Type);
	seq_printf(seq, "   oSignalBase        = %llu\n",
		   (long long) q->oSignalBase);
	seq_printf(seq, "   SignalSize         = %lu\n", (ulong) q->SignalSize);
	seq_printf(seq, "   MaxSignalSlots     = %lu\n",
		   (ulong) q->MaxSignalSlots);
	seq_printf(seq, "   MaxSignals         = %lu\n", (ulong) q->MaxSignals);
	seq_printf(seq, "   FeatureFlags       = %-16.16Lx\n",
		   (long long) q->FeatureFlags);
	seq_printf(seq, "   NumSignalsSent     = %llu\n",
		   (long long) q->NumSignalsSent);
	seq_printf(seq, "   NumSignalsReceived = %llu\n",
		   (long long) q->NumSignalsReceived);
	seq_printf(seq, "   NumOverflows       = %llu\n",
		   (long long) q->NumOverflows);
	seq_printf(seq, "   Head               = %lu\n", (ulong) q->Head);
	seq_printf(seq, "   Tail               = %lu\n", (ulong) q->Tail);
}

void
visorchannel_debug(VISORCHANNEL *channel, int nQueues,
		   struct seq_file *seq, u32 off)
{
	HOSTADDRESS addr = 0;
	ulong nbytes = 0, nbytes_region = 0;
	MEMREGION *memregion = NULL;
	CHANNEL_HEADER hdr;
	CHANNEL_HEADER *phdr = &hdr;
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
				    phdr, sizeof(CHANNEL_HEADER));
	if (errcode < 0) {
		seq_printf(seq,
			   "Read of channel header failed with errcode=%d)\n",
			   errcode);
		if (off == 0) {
			phdr = &channel->chan_hdr;
			seq_puts(seq, "(following data may be stale)\n");
		} else
			return;
	}
	nbytes = (ulong) (phdr->Size);
	seq_printf(seq, "--- Begin channel @0x%-16.16Lx for 0x%lx bytes (region=0x%lx bytes) ---\n",
		   addr + off, nbytes, nbytes_region);
	seq_printf(seq, "Type            = %pUL\n", &phdr->Type);
	seq_printf(seq, "ZoneGuid        = %pUL\n", &phdr->ZoneGuid);
	seq_printf(seq, "Signature       = 0x%-16.16Lx\n",
		   (long long) phdr->Signature);
	seq_printf(seq, "LegacyState     = %lu\n", (ulong) phdr->LegacyState);
	seq_printf(seq, "SrvState        = %lu\n", (ulong) phdr->SrvState);
	seq_printf(seq, "CliStateBoot    = %lu\n", (ulong) phdr->CliStateBoot);
	seq_printf(seq, "CliStateOS      = %lu\n", (ulong) phdr->CliStateOS);
	seq_printf(seq, "HeaderSize      = %lu\n", (ulong) phdr->HeaderSize);
	seq_printf(seq, "Size            = %llu\n", (long long) phdr->Size);
	seq_printf(seq, "Features        = 0x%-16.16llx\n",
		   (long long) phdr->Features);
	seq_printf(seq, "PartitionHandle = 0x%-16.16llx\n",
		   (long long) phdr->PartitionHandle);
	seq_printf(seq, "Handle          = 0x%-16.16llx\n",
		   (long long) phdr->Handle);
	seq_printf(seq, "VersionId       = %lu\n", (ulong) phdr->VersionId);
	seq_printf(seq, "oChannelSpace   = %llu\n",
		   (long long) phdr->oChannelSpace);
	if ((phdr->oChannelSpace == 0) || (errcode < 0))
		;
	else
		for (i = 0; i < nQueues; i++) {
			SIGNAL_QUEUE_HEADER q;

			errcode = visorchannel_read(channel,
						    off + phdr->oChannelSpace +
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
visorchannel_dump_section(VISORCHANNEL *chan, char *s,
			  int off, int len, struct seq_file *seq)
{
	char *buf, *tbuf, *fmtbuf;
	int fmtbufsize = 0;
	int i;
	int errcode = 0;

	fmtbufsize = 100 * COVQ(len, 16);
	buf = kmalloc(len, GFP_KERNEL|__GFP_NORETRY);
	fmtbuf = kmalloc(fmtbufsize, GFP_KERNEL|__GFP_NORETRY);
	if (buf == NULL || fmtbuf == NULL)
		goto Away;

	errcode = visorchannel_read(chan, off, buf, len);
	if (errcode < 0) {
		ERRDRV("%s failed to read %s from channel errcode=%d",
		       s, __func__, errcode);
		goto Away;
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

Away:
	if (buf != NULL) {
		kfree(buf);
		buf = NULL;
	}
	if (fmtbuf != NULL) {
		kfree(fmtbuf);
		fmtbuf = NULL;
	}
}
EXPORT_SYMBOL_GPL(visorchannel_dump_section);
