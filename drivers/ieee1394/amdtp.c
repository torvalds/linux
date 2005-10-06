/* -*- c-basic-offset: 8 -*-
 *
 * amdtp.c - Audio and Music Data Transmission Protocol Driver
 * Copyright (C) 2001 Kristian Høgsberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* OVERVIEW
 * --------
 *
 * The AMDTP driver is designed to expose the IEEE1394 bus as a
 * regular OSS soundcard, i.e. you can link /dev/dsp to /dev/amdtp and
 * then your favourite MP3 player, game or whatever sound program will
 * output to an IEEE1394 isochronous channel.  The signal destination
 * could be a set of IEEE1394 loudspeakers (if and when such things
 * become available) or an amplifier with IEEE1394 input (like the
 * Sony STR-LSA1).  The driver only handles the actual streaming, some
 * connection management is also required for this to actually work.
 * That is outside the scope of this driver, and furthermore it is not
 * really standardized yet.
 *
 * The Audio and Music Data Tranmission Protocol is available at
 *
 *     http://www.1394ta.org/Download/Technology/Specifications/2001/AM20Final-jf2.pdf
 *
 *
 * TODO
 * ----
 *
 * - We should be able to change input sample format between LE/BE, as
 *   we already shift the bytes around when we construct the iso
 *   packets.
 *
 * - Fix DMA stop after bus reset!
 *
 * - Clean up iso context handling in ohci1394.
 *
 *
 * MAYBE TODO
 * ----------
 *
 * - Receive data for local playback or recording.  Playback requires
 *   soft syncing with the sound card.
 *
 * - Signal processing, i.e. receive packets, do some processing, and
 *   transmit them again using the same packet structure and timestamps
 *   offset by processing time.
 *
 * - Maybe make an ALSA interface, that is, create a file_ops
 *   implementation that recognizes ALSA ioctls and uses defaults for
 *   things that can't be controlled through ALSA (iso channel).
 *
 *   Changes:
 *
 * - Audit copy_from_user in amdtp_write.
 *                           Daniele Bellucci <bellucda@tiscali.it>
 *
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/ioctl32.h>
#include <linux/compat.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "hosts.h"
#include "highlevel.h"
#include "ieee1394.h"
#include "ieee1394_core.h"
#include "ohci1394.h"

#include "amdtp.h"
#include "cmp.h"

#define FMT_AMDTP 0x10
#define FDF_AM824 0x00
#define FDF_SFC_32KHZ   0x00
#define FDF_SFC_44K1HZ  0x01
#define FDF_SFC_48KHZ   0x02
#define FDF_SFC_88K2HZ  0x03
#define FDF_SFC_96KHZ   0x04
#define FDF_SFC_176K4HZ 0x05
#define FDF_SFC_192KHZ  0x06

struct descriptor_block {
	struct output_more_immediate {
		u32 control;
		u32 pad0;
		u32 skip;
		u32 pad1;
		u32 header[4];
	} header_desc;

	struct output_last {
		u32 control;
		u32 data_address;
		u32 branch;
		u32 status;
	} payload_desc;
};

struct packet {
	struct descriptor_block *db;
	dma_addr_t db_bus;
	struct iso_packet *payload;
	dma_addr_t payload_bus;
};

#include <asm/byteorder.h>

#if defined __BIG_ENDIAN_BITFIELD

struct iso_packet {
	/* First quadlet */
	unsigned int dbs      : 8;
	unsigned int eoh0     : 2;
	unsigned int sid      : 6;

	unsigned int dbc      : 8;
	unsigned int fn       : 2;
	unsigned int qpc      : 3;
	unsigned int sph      : 1;
	unsigned int reserved : 2;

	/* Second quadlet */
	unsigned int fdf      : 8;
	unsigned int eoh1     : 2;
	unsigned int fmt      : 6;

	unsigned int syt      : 16;

        quadlet_t data[0];
};

#elif defined __LITTLE_ENDIAN_BITFIELD

struct iso_packet {
	/* First quadlet */
	unsigned int sid      : 6;
	unsigned int eoh0     : 2;
	unsigned int dbs      : 8;

	unsigned int reserved : 2;
	unsigned int sph      : 1;
	unsigned int qpc      : 3;
	unsigned int fn       : 2;
	unsigned int dbc      : 8;

	/* Second quadlet */
	unsigned int fmt      : 6;
	unsigned int eoh1     : 2;
	unsigned int fdf      : 8;

	unsigned int syt      : 16;

	quadlet_t data[0];
};

#else

#error Unknown bitfield type

#endif

struct fraction {
	int integer;
	int numerator;
	int denominator;
};

#define PACKET_LIST_SIZE 256
#define MAX_PACKET_LISTS 4

struct packet_list {
	struct list_head link;
	int last_cycle_count;
	struct packet packets[PACKET_LIST_SIZE];
};

#define BUFFER_SIZE 128

/* This implements a circular buffer for incoming samples. */

struct buffer {
	size_t head, tail, length, size;
	unsigned char data[0];
};

struct stream {
	int iso_channel;
	int format;
	int rate;
	int dimension;
	int fdf;
	int mode;
	int sample_format;
	struct cmp_pcr *opcr;

	/* Input samples are copied here. */
	struct buffer *input;

	/* ISO Packer state */
	unsigned char dbc;
	struct packet_list *current_packet_list;
	int current_packet;
	struct fraction ready_samples, samples_per_cycle;

	/* We use these to generate control bits when we are packing
	 * iec958 data.
	 */
	int iec958_frame_count;
	int iec958_rate_code;

	/* The cycle_count and cycle_offset fields are used for the
	 * synchronization timestamps (syt) in the cip header.  They
	 * are incremented by at least a cycle every time we put a
	 * time stamp in a packet.  As we don't time stamp all
	 * packages, cycle_count isn't updated in every cycle, and
	 * sometimes it's incremented by 2.  Thus, we have
	 * cycle_count2, which is simply incremented by one with each
	 * packet, so we can compare it to the transmission time
	 * written back in the dma programs.
	 */
	atomic_t cycle_count, cycle_count2;
	struct fraction cycle_offset, ticks_per_syt_offset;
	int syt_interval;
	int stale_count;

	/* Theses fields control the sample output to the DMA engine.
	 * The dma_packet_lists list holds packet lists currently
	 * queued for dma; the head of the list is currently being
	 * processed.  The last program in a packet list generates an
	 * interrupt, which removes the head from dma_packet_lists and
	 * puts it back on the free list.
	 */
	struct list_head dma_packet_lists;
	struct list_head free_packet_lists;
        wait_queue_head_t packet_list_wait;
	spinlock_t packet_list_lock;
	struct ohci1394_iso_tasklet iso_tasklet;
	struct pci_pool *descriptor_pool, *packet_pool;

	/* Streams at a host controller are chained through this field. */
	struct list_head link;
	struct amdtp_host *host;
};

struct amdtp_host {
	struct hpsb_host *host;
	struct ti_ohci *ohci;
	struct list_head stream_list;
	spinlock_t stream_list_lock;
};

static struct hpsb_highlevel amdtp_highlevel;


/* FIXME: This doesn't belong here... */

#define OHCI1394_CONTEXT_CYCLE_MATCH 0x80000000
#define OHCI1394_CONTEXT_RUN         0x00008000
#define OHCI1394_CONTEXT_WAKE        0x00001000
#define OHCI1394_CONTEXT_DEAD        0x00000800
#define OHCI1394_CONTEXT_ACTIVE      0x00000400

static void ohci1394_start_it_ctx(struct ti_ohci *ohci, int ctx,
			   dma_addr_t first_cmd, int z, int cycle_match)
{
	reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, 1 << ctx);
	reg_write(ohci, OHCI1394_IsoXmitCommandPtr + ctx * 16, first_cmd | z);
	reg_write(ohci, OHCI1394_IsoXmitContextControlClear + ctx * 16, ~0);
	wmb();
	reg_write(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16,
		  OHCI1394_CONTEXT_CYCLE_MATCH | (cycle_match << 16) |
		  OHCI1394_CONTEXT_RUN);
}

static void ohci1394_wake_it_ctx(struct ti_ohci *ohci, int ctx)
{
	reg_write(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16,
		  OHCI1394_CONTEXT_WAKE);
}

static void ohci1394_stop_it_ctx(struct ti_ohci *ohci, int ctx, int synchronous)
{
	u32 control;
	int wait;

	reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, 1 << ctx);
	reg_write(ohci, OHCI1394_IsoXmitContextControlClear + ctx * 16,
		  OHCI1394_CONTEXT_RUN);
	wmb();

	if (synchronous) {
		for (wait = 0; wait < 5; wait++) {
			control = reg_read(ohci, OHCI1394_IsoXmitContextControlSet + ctx * 16);
			if ((control & OHCI1394_CONTEXT_ACTIVE) == 0)
				break;

			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);
		}
	}
}

/* Note: we can test if free_packet_lists is empty without aquiring
 * the packet_list_lock.  The interrupt handler only adds to the free
 * list, there is no race condition between testing the list non-empty
 * and acquiring the lock.
 */

static struct packet_list *stream_get_free_packet_list(struct stream *s)
{
	struct packet_list *pl;
	unsigned long flags;

	if (list_empty(&s->free_packet_lists))
		return NULL;

	spin_lock_irqsave(&s->packet_list_lock, flags);
	pl = list_entry(s->free_packet_lists.next, struct packet_list, link);
	list_del(&pl->link);
	spin_unlock_irqrestore(&s->packet_list_lock, flags);

	return pl;
}

static void stream_start_dma(struct stream *s, struct packet_list *pl)
{
	u32 syt_cycle, cycle_count, start_cycle;

	cycle_count = reg_read(s->host->ohci,
			       OHCI1394_IsochronousCycleTimer) >> 12;
	syt_cycle = (pl->last_cycle_count - PACKET_LIST_SIZE + 1) & 0x0f;

	/* We program the DMA controller to start transmission at
	 * least 17 cycles from now - this happens when the lower four
	 * bits of cycle_count is 0x0f and syt_cycle is 0, in this
	 * case the start cycle is cycle_count - 15 + 32. */
	start_cycle = (cycle_count & ~0x0f) + 32 + syt_cycle;
	if ((start_cycle & 0x1fff) >= 8000)
		start_cycle = start_cycle - 8000 + 0x2000;

	ohci1394_start_it_ctx(s->host->ohci, s->iso_tasklet.context,
			      pl->packets[0].db_bus, 3,
			      start_cycle & 0x7fff);
}

static void stream_put_dma_packet_list(struct stream *s,
				       struct packet_list *pl)
{
	unsigned long flags;
	struct packet_list *prev;

	/* Remember the cycle_count used for timestamping the last packet. */
	pl->last_cycle_count = atomic_read(&s->cycle_count2) - 1;
	pl->packets[PACKET_LIST_SIZE - 1].db->payload_desc.branch = 0;

	spin_lock_irqsave(&s->packet_list_lock, flags);
	list_add_tail(&pl->link, &s->dma_packet_lists);
	spin_unlock_irqrestore(&s->packet_list_lock, flags);

	prev = list_entry(pl->link.prev, struct packet_list, link);
	if (pl->link.prev != &s->dma_packet_lists) {
		struct packet *last = &prev->packets[PACKET_LIST_SIZE - 1];
		last->db->payload_desc.branch = pl->packets[0].db_bus | 3;
		last->db->header_desc.skip = pl->packets[0].db_bus | 3;
		ohci1394_wake_it_ctx(s->host->ohci, s->iso_tasklet.context);
	}
	else
		stream_start_dma(s, pl);
}

static void stream_shift_packet_lists(unsigned long l)
{
	struct stream *s = (struct stream *) l;
	struct packet_list *pl;
	struct packet *last;
	int diff;

	if (list_empty(&s->dma_packet_lists)) {
		HPSB_ERR("empty dma_packet_lists in %s", __FUNCTION__);
		return;
	}

	/* Now that we know the list is non-empty, we can get the head
	 * of the list without locking, because the process context
	 * only adds to the tail.
	 */
	pl = list_entry(s->dma_packet_lists.next, struct packet_list, link);
	last = &pl->packets[PACKET_LIST_SIZE - 1];

	/* This is weird... if we stop dma processing in the middle of
	 * a packet list, the dma context immediately generates an
	 * interrupt if we enable it again later.  This only happens
	 * when amdtp_release is interrupted while waiting for dma to
	 * complete, though.  Anyway, we detect this by seeing that
	 * the status of the dma descriptor that we expected an
	 * interrupt from is still 0.
	 */
	if (last->db->payload_desc.status == 0) {
		HPSB_INFO("weird interrupt...");
		return;
	}

	/* If the last descriptor block does not specify a branch
	 * address, we have a sample underflow.
	 */
	if (last->db->payload_desc.branch == 0)
		HPSB_INFO("FIXME: sample underflow...");

	/* Here we check when (which cycle) the last packet was sent
	 * and compare it to what the iso packer was using at the
	 * time.  If there is a mismatch, we adjust the cycle count in
	 * the iso packer.  However, there are still up to
	 * MAX_PACKET_LISTS packet lists queued with bad time stamps,
	 * so we disable time stamp monitoring for the next
	 * MAX_PACKET_LISTS packet lists.
	 */
	diff = (last->db->payload_desc.status - pl->last_cycle_count) & 0xf;
	if (diff > 0 && s->stale_count == 0) {
		atomic_add(diff, &s->cycle_count);
		atomic_add(diff, &s->cycle_count2);
		s->stale_count = MAX_PACKET_LISTS;
	}

	if (s->stale_count > 0)
		s->stale_count--;

	/* Finally, we move the packet list that was just processed
	 * back to the free list, and notify any waiters.
	 */
	spin_lock(&s->packet_list_lock);
	list_del(&pl->link);
	list_add_tail(&pl->link, &s->free_packet_lists);
	spin_unlock(&s->packet_list_lock);

	wake_up_interruptible(&s->packet_list_wait);
}

static struct packet *stream_current_packet(struct stream *s)
{
	if (s->current_packet_list == NULL &&
	    (s->current_packet_list = stream_get_free_packet_list(s)) == NULL)
		return NULL;

	return &s->current_packet_list->packets[s->current_packet];
}

static void stream_queue_packet(struct stream *s)
{
	s->current_packet++;
	if (s->current_packet == PACKET_LIST_SIZE) {
		stream_put_dma_packet_list(s, s->current_packet_list);
		s->current_packet_list = NULL;
		s->current_packet = 0;
	}
}

/* Integer fractional math.  When we transmit a 44k1Hz signal we must
 * send 5 41/80 samples per isochronous cycle, as these occur 8000
 * times a second.  Of course, we must send an integral number of
 * samples in a packet, so we use the integer math to alternate
 * between sending 5 and 6 samples per packet.
 */

static void fraction_init(struct fraction *f, int numerator, int denominator)
{
	f->integer = numerator / denominator;
	f->numerator = numerator % denominator;
	f->denominator = denominator;
}

static __inline__ void fraction_add(struct fraction *dst,
				    struct fraction *src1,
				    struct fraction *src2)
{
	/* assert: src1->denominator == src2->denominator */

	int sum, denom;

	/* We use these two local variables to allow gcc to optimize
	 * the division and the modulo into only one division. */

	sum = src1->numerator + src2->numerator;
	denom = src1->denominator;
	dst->integer = src1->integer + src2->integer + sum / denom;
	dst->numerator = sum % denom;
	dst->denominator = denom;
}

static __inline__ void fraction_sub_int(struct fraction *dst,
					struct fraction *src, int integer)
{
	dst->integer = src->integer - integer;
	dst->numerator = src->numerator;
	dst->denominator = src->denominator;
}

static __inline__ int fraction_floor(struct fraction *frac)
{
	return frac->integer;
}

static __inline__ int fraction_ceil(struct fraction *frac)
{
	return frac->integer + (frac->numerator > 0 ? 1 : 0);
}

static void packet_initialize(struct packet *p, struct packet *next)
{
	/* Here we initialize the dma descriptor block for
	 * transferring one iso packet.  We use two descriptors per
	 * packet: an OUTPUT_MORE_IMMMEDIATE descriptor for the
	 * IEEE1394 iso packet header and an OUTPUT_LAST descriptor
	 * for the payload.
	 */

	p->db->header_desc.control =
		DMA_CTL_OUTPUT_MORE | DMA_CTL_IMMEDIATE | 8;

	if (next) {
		p->db->payload_desc.control =
			DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH;
		p->db->payload_desc.branch = next->db_bus | 3;
		p->db->header_desc.skip = next->db_bus | 3;
	}
	else {
		p->db->payload_desc.control =
			DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH |
			DMA_CTL_UPDATE | DMA_CTL_IRQ;
		p->db->payload_desc.branch = 0;
		p->db->header_desc.skip = 0;
	}
	p->db->payload_desc.data_address = p->payload_bus;
	p->db->payload_desc.status = 0;
}

static struct packet_list *packet_list_alloc(struct stream *s)
{
	int i;
	struct packet_list *pl;
	struct packet *next;

	pl = kmalloc(sizeof *pl, SLAB_KERNEL);
	if (pl == NULL)
		return NULL;

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		struct packet *p = &pl->packets[i];
		p->db = pci_pool_alloc(s->descriptor_pool, SLAB_KERNEL,
				       &p->db_bus);
		p->payload = pci_pool_alloc(s->packet_pool, SLAB_KERNEL,
					    &p->payload_bus);
	}

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		if (i < PACKET_LIST_SIZE - 1)
			next = &pl->packets[i + 1];
		else
			next = NULL;
		packet_initialize(&pl->packets[i], next);
	}

	return pl;
}

static void packet_list_free(struct packet_list *pl, struct stream *s)
{
	int i;

	for (i = 0; i < PACKET_LIST_SIZE; i++) {
		struct packet *p = &pl->packets[i];
		pci_pool_free(s->descriptor_pool, p->db, p->db_bus);
		pci_pool_free(s->packet_pool, p->payload, p->payload_bus);
	}
	kfree(pl);
}

static struct buffer *buffer_alloc(int size)
{
	struct buffer *b;

	b = kmalloc(sizeof *b + size, SLAB_KERNEL);
	if (b == NULL)
		return NULL;
	b->head = 0;
	b->tail = 0;
	b->length = 0;
	b->size = size;

	return b;
}

static unsigned char *buffer_get_bytes(struct buffer *buffer, int size)
{
	unsigned char *p;

	if (buffer->head + size > buffer->size)
		BUG();

	p = &buffer->data[buffer->head];
	buffer->head += size;
	if (buffer->head == buffer->size)
		buffer->head = 0;
	buffer->length -= size;

	return p;
}

static unsigned char *buffer_put_bytes(struct buffer *buffer,
				       size_t max, size_t *actual)
{
	size_t length;
	unsigned char *p;

	p = &buffer->data[buffer->tail];
	length = min(buffer->size - buffer->length, max);
	if (buffer->tail + length < buffer->size) {
		*actual = length;
		buffer->tail += length;
	}
	else {
		*actual = buffer->size - buffer->tail;
		 buffer->tail = 0;
	}

	buffer->length += *actual;
	return p;
}

static u32 get_iec958_header_bits(struct stream *s, int sub_frame, u32 sample)
{
	int csi, parity, shift;
	int block_start;
	u32 bits;

	switch (s->iec958_frame_count) {
	case 1:
		csi = s->format == AMDTP_FORMAT_IEC958_AC3;
		break;
	case 2:
	case 9:
		csi = 1;
		break;
	case 24 ... 27:
		csi = (s->iec958_rate_code >> (27 - s->iec958_frame_count)) & 0x01;
		break;
	default:
		csi = 0;
		break;
	}

	block_start = (s->iec958_frame_count == 0 && sub_frame == 0);

	/* The parity bit is the xor of the sample bits and the
	 * channel status info bit. */
	for (shift = 16, parity = sample ^ csi; shift > 0; shift >>= 1)
		parity ^= (parity >> shift);

	bits =  (block_start << 5) |		/* Block start bit */
		((sub_frame == 0) << 4) |	/* Subframe bit */
		((parity & 1) << 3) |		/* Parity bit */
		(csi << 2);			/* Channel status info bit */

	return bits;
}

static u32 get_header_bits(struct stream *s, int sub_frame, u32 sample)
{
	switch (s->format) {
	case AMDTP_FORMAT_IEC958_PCM:
	case AMDTP_FORMAT_IEC958_AC3:
		return get_iec958_header_bits(s, sub_frame, sample);

	case AMDTP_FORMAT_RAW:
		return 0x40;

	default:
		return 0;
	}
}

static void fill_payload_le16(struct stream *s, quadlet_t *data, int nevents)
{
	quadlet_t *event, sample, bits;
	unsigned char *p;
	int i, j;

	for (i = 0, event = data; i < nevents; i++) {

		for (j = 0; j < s->dimension; j++) {
			p = buffer_get_bytes(s->input, 2);
			sample = (p[1] << 16) | (p[0] << 8);
			bits = get_header_bits(s, j, sample);
			event[j] = cpu_to_be32((bits << 24) | sample);
		}

		event += s->dimension;
		if (++s->iec958_frame_count == 192)
			s->iec958_frame_count = 0;
	}
}

static void fill_packet(struct stream *s, struct packet *packet, int nevents)
{
	int syt_index, syt, size;
	u32 control;

	size = (nevents * s->dimension + 2) * sizeof(quadlet_t);

	/* Update DMA descriptors */
	packet->db->payload_desc.status = 0;
	control = packet->db->payload_desc.control & 0xffff0000;
	packet->db->payload_desc.control = control | size;

	/* Fill IEEE1394 headers */
	packet->db->header_desc.header[0] =
		(IEEE1394_SPEED_100 << 16) | (0x01 << 14) |
		(s->iso_channel << 8) | (TCODE_ISO_DATA << 4);
	packet->db->header_desc.header[1] = size << 16;

	/* Calculate synchronization timestamp (syt). First we
	 * determine syt_index, that is, the index in the packet of
	 * the sample for which the timestamp is valid. */
	syt_index = (s->syt_interval - s->dbc) & (s->syt_interval - 1);
	if (syt_index < nevents) {
		syt = ((atomic_read(&s->cycle_count) << 12) |
		       s->cycle_offset.integer) & 0xffff;
		fraction_add(&s->cycle_offset,
			     &s->cycle_offset, &s->ticks_per_syt_offset);

		/* This next addition should be modulo 8000 (0x1f40),
		 * but we only use the lower 4 bits of cycle_count, so
		 * we don't need the modulo. */
		atomic_add(s->cycle_offset.integer / 3072, &s->cycle_count);
		s->cycle_offset.integer %= 3072;
	}
	else
		syt = 0xffff;

	atomic_inc(&s->cycle_count2);

	/* Fill cip header */
	packet->payload->eoh0 = 0;
	packet->payload->sid = s->host->host->node_id & 0x3f;
	packet->payload->dbs = s->dimension;
	packet->payload->fn = 0;
	packet->payload->qpc = 0;
	packet->payload->sph = 0;
	packet->payload->reserved = 0;
	packet->payload->dbc = s->dbc;
	packet->payload->eoh1 = 2;
	packet->payload->fmt = FMT_AMDTP;
	packet->payload->fdf = s->fdf;
	packet->payload->syt = cpu_to_be16(syt);

	switch (s->sample_format) {
	case AMDTP_INPUT_LE16:
		fill_payload_le16(s, packet->payload->data, nevents);
		break;
	}

	s->dbc += nevents;
}

static void stream_flush(struct stream *s)
{
	struct packet *p;
	int nevents;
	struct fraction next;

	/* The AMDTP specifies two transmission modes: blocking and
	 * non-blocking.  In blocking mode you always transfer
	 * syt_interval or zero samples, whereas in non-blocking mode
	 * you send as many samples as you have available at transfer
	 * time.
	 *
	 * The fraction samples_per_cycle specifies the number of
	 * samples that become available per cycle.  We add this to
	 * the fraction ready_samples, which specifies the number of
	 * leftover samples from the previous transmission.  The sum,
	 * stored in the fraction next, specifies the number of
	 * samples available for transmission, and from this we
	 * determine the number of samples to actually transmit.
	 */

	while (1) {
		fraction_add(&next, &s->ready_samples, &s->samples_per_cycle);
		if (s->mode == AMDTP_MODE_BLOCKING) {
			if (fraction_floor(&next) >= s->syt_interval)
				nevents = s->syt_interval;
			else
				nevents = 0;
		}
		else
			nevents = fraction_floor(&next);

		p = stream_current_packet(s);
		if (s->input->length < nevents * s->dimension * 2 || p == NULL)
			break;

		fill_packet(s, p, nevents);
		stream_queue_packet(s);

		/* Now that we have successfully queued the packet for
		 * transmission, we update the fraction ready_samples. */
		fraction_sub_int(&s->ready_samples, &next, nevents);
	}
}

static int stream_alloc_packet_lists(struct stream *s)
{
	int max_nevents, max_packet_size, i;

	if (s->mode == AMDTP_MODE_BLOCKING)
		max_nevents = s->syt_interval;
	else
		max_nevents = fraction_ceil(&s->samples_per_cycle);

	max_packet_size = max_nevents * s->dimension * 4 + 8;
	s->packet_pool = pci_pool_create("packet pool", s->host->ohci->dev,
					 max_packet_size, 0, 0);

	if (s->packet_pool == NULL)
		return -1;

	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);
	for (i = 0; i < MAX_PACKET_LISTS; i++) {
		struct packet_list *pl = packet_list_alloc(s);
		if (pl == NULL)
			break;
		list_add_tail(&pl->link, &s->free_packet_lists);
	}

	return i < MAX_PACKET_LISTS ? -1 : 0;
}

static void stream_free_packet_lists(struct stream *s)
{
	struct packet_list *packet_l, *packet_l_next;

	if (s->current_packet_list != NULL)
		packet_list_free(s->current_packet_list, s);
	list_for_each_entry_safe(packet_l, packet_l_next, &s->dma_packet_lists, link)
		packet_list_free(packet_l, s);
	list_for_each_entry_safe(packet_l, packet_l_next, &s->free_packet_lists, link)
		packet_list_free(packet_l, s);
	if (s->packet_pool != NULL)
		pci_pool_destroy(s->packet_pool);

	s->current_packet_list = NULL;
	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);
	s->packet_pool = NULL;
}

static void plug_update(struct cmp_pcr *plug, void *data)
{
	struct stream *s = data;

	HPSB_INFO("plug update: p2p_count=%d, channel=%d",
		  plug->p2p_count, plug->channel);
	s->iso_channel = plug->channel;
	if (plug->p2p_count > 0) {
		struct packet_list *pl;

		pl = list_entry(s->dma_packet_lists.next, struct packet_list, link);
		stream_start_dma(s, pl);
	}
	else {
		ohci1394_stop_it_ctx(s->host->ohci, s->iso_tasklet.context, 0);
	}
}

static int stream_configure(struct stream *s, int cmd, struct amdtp_ioctl *cfg)
{
	const int transfer_delay = 9000;

	if (cfg->format <= AMDTP_FORMAT_IEC958_AC3)
		s->format = cfg->format;
	else
		return -EINVAL;

	switch (cfg->rate) {
	case 32000:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_32KHZ;
		s->iec958_rate_code = 0x0c;
		break;
	case 44100:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_44K1HZ;
		s->iec958_rate_code = 0x00;
		break;
	case 48000:
		s->syt_interval = 8;
		s->fdf = FDF_SFC_48KHZ;
		s->iec958_rate_code = 0x04;
		break;
	case 88200:
		s->syt_interval = 16;
		s->fdf = FDF_SFC_88K2HZ;
		s->iec958_rate_code = 0x00;
		break;
	case 96000:
		s->syt_interval = 16;
		s->fdf = FDF_SFC_96KHZ;
		s->iec958_rate_code = 0x00;
		break;
	case 176400:
		s->syt_interval = 32;
		s->fdf = FDF_SFC_176K4HZ;
		s->iec958_rate_code = 0x00;
		break;
	case 192000:
		s->syt_interval = 32;
		s->fdf = FDF_SFC_192KHZ;
		s->iec958_rate_code = 0x00;
		break;

	default:
		return -EINVAL;
	}

	s->rate = cfg->rate;
	fraction_init(&s->samples_per_cycle, s->rate, 8000);
	fraction_init(&s->ready_samples, 0, 8000);

	/* The ticks_per_syt_offset is initialized to the number of
	 * ticks between syt_interval events.  The number of ticks per
	 * second is 24.576e6, so the number of ticks between
	 * syt_interval events is 24.576e6 * syt_interval / rate.
	 */
	fraction_init(&s->ticks_per_syt_offset,
		      24576000 * s->syt_interval, s->rate);
	fraction_init(&s->cycle_offset, (transfer_delay % 3072) * s->rate, s->rate);
	atomic_set(&s->cycle_count, transfer_delay / 3072);
	atomic_set(&s->cycle_count2, 0);

	s->mode = cfg->mode;
	s->sample_format = AMDTP_INPUT_LE16;

	/* When using the AM824 raw subformat we can stream signals of
	 * any dimension.  The IEC958 subformat, however, only
	 * supports 2 channels.
	 */
	if (s->format == AMDTP_FORMAT_RAW || cfg->dimension == 2)
		s->dimension = cfg->dimension;
	else
		return -EINVAL;

	if (s->opcr != NULL) {
		cmp_unregister_opcr(s->host->host, s->opcr);
		s->opcr = NULL;
	}

	switch(cmd) {
	case AMDTP_IOC_PLUG:
		s->opcr = cmp_register_opcr(s->host->host, cfg->u.plug,
					   /*payload*/ 12, plug_update, s);
		if (s->opcr == NULL)
			return -EINVAL;
		s->iso_channel = s->opcr->channel;
		break;

	case AMDTP_IOC_CHANNEL:
		if (cfg->u.channel >= 0 && cfg->u.channel < 64)
			s->iso_channel = cfg->u.channel;
		else
			return -EINVAL;
		break;
	}

	/* The ioctl settings were all valid, so we realloc the packet
	 * lists to make sure the packet size is big enough.
	 */
	if (s->packet_pool != NULL)
		stream_free_packet_lists(s);

	if (stream_alloc_packet_lists(s) < 0) {
		stream_free_packet_lists(s);
		return -ENOMEM;
	}

	return 0;
}

static struct stream *stream_alloc(struct amdtp_host *host)
{
	struct stream *s;
	unsigned long flags;

        s = kmalloc(sizeof(struct stream), SLAB_KERNEL);
        if (s == NULL)
                return NULL;

        memset(s, 0, sizeof(struct stream));
	s->host = host;

	s->input = buffer_alloc(BUFFER_SIZE);
	if (s->input == NULL) {
		kfree(s);
		return NULL;
	}

	s->descriptor_pool = pci_pool_create("descriptor pool", host->ohci->dev,
					     sizeof(struct descriptor_block),
					     16, 0);

	if (s->descriptor_pool == NULL) {
		kfree(s->input);
		kfree(s);
		return NULL;
	}

	INIT_LIST_HEAD(&s->free_packet_lists);
	INIT_LIST_HEAD(&s->dma_packet_lists);

        init_waitqueue_head(&s->packet_list_wait);
        spin_lock_init(&s->packet_list_lock);

	ohci1394_init_iso_tasklet(&s->iso_tasklet, OHCI_ISO_TRANSMIT,
				  stream_shift_packet_lists,
				  (unsigned long) s);

	if (ohci1394_register_iso_tasklet(host->ohci, &s->iso_tasklet) < 0) {
		pci_pool_destroy(s->descriptor_pool);
		kfree(s->input);
		kfree(s);
		return NULL;
	}

	spin_lock_irqsave(&host->stream_list_lock, flags);
	list_add_tail(&s->link, &host->stream_list);
	spin_unlock_irqrestore(&host->stream_list_lock, flags);

	return s;
}

static void stream_free(struct stream *s)
{
	unsigned long flags;

	/* Stop the DMA.  We wait for the dma packet list to become
	 * empty and let the dma controller run out of programs.  This
	 * seems to be more reliable than stopping it directly, since
	 * that sometimes generates an it transmit interrupt if we
	 * later re-enable the context.
	 */
	wait_event_interruptible(s->packet_list_wait,
				 list_empty(&s->dma_packet_lists));

	ohci1394_stop_it_ctx(s->host->ohci, s->iso_tasklet.context, 1);
	ohci1394_unregister_iso_tasklet(s->host->ohci, &s->iso_tasklet);

	if (s->opcr != NULL)
		cmp_unregister_opcr(s->host->host, s->opcr);

	spin_lock_irqsave(&s->host->stream_list_lock, flags);
	list_del(&s->link);
	spin_unlock_irqrestore(&s->host->stream_list_lock, flags);

	kfree(s->input);

	stream_free_packet_lists(s);
	pci_pool_destroy(s->descriptor_pool);

	kfree(s);
}

/* File operations */

static ssize_t amdtp_write(struct file *file, const char __user *buffer, size_t count,
			   loff_t *offset_is_ignored)
{
	struct stream *s = file->private_data;
	unsigned char *p;
	int i;
	size_t length;

	if (s->packet_pool == NULL)
		return -EBADFD;

	/* Fill the circular buffer from the input buffer and call the
	 * iso packer when the buffer is full.  The iso packer may
	 * leave bytes in the buffer for two reasons: either the
	 * remaining bytes wasn't enough to build a new packet, or
	 * there were no free packet lists.  In the first case we
	 * re-fill the buffer and call the iso packer again or return
	 * if we used all the data from userspace.  In the second
	 * case, the wait_event_interruptible will block until the irq
	 * handler frees a packet list.
	 */

	for (i = 0; i < count; i += length) {
		p = buffer_put_bytes(s->input, count - i, &length);
		if (copy_from_user(p, buffer + i, length))
			return -EFAULT;
		if (s->input->length < s->input->size)
			continue;

		stream_flush(s);

		if (s->current_packet_list != NULL)
			continue;

		if (file->f_flags & O_NONBLOCK)
			return i + length > 0 ? i + length : -EAGAIN;

		if (wait_event_interruptible(s->packet_list_wait,
					     !list_empty(&s->free_packet_lists)))
			return -EINTR;
	}

	return count;
}

static long amdtp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct stream *s = file->private_data;
	struct amdtp_ioctl cfg;
	int err;
	lock_kernel();
	switch(cmd)
	{
	case AMDTP_IOC_PLUG:
	case AMDTP_IOC_CHANNEL:
		if (copy_from_user(&cfg, (struct amdtp_ioctl __user *) arg, sizeof cfg))
			err = -EFAULT;
		else
			err = stream_configure(s, cmd, &cfg);
		break;

	default:
		err = -EINVAL;
		break;
	}
	unlock_kernel();
	return err;
}

static unsigned int amdtp_poll(struct file *file, poll_table *pt)
{
	struct stream *s = file->private_data;

	poll_wait(file, &s->packet_list_wait, pt);

	if (!list_empty(&s->free_packet_lists))
		return POLLOUT | POLLWRNORM;
	else
		return 0;
}

static int amdtp_open(struct inode *inode, struct file *file)
{
	struct amdtp_host *host;
	int i = ieee1394_file_to_instance(file);

	host = hpsb_get_hostinfo_bykey(&amdtp_highlevel, i);
	if (host == NULL)
		return -ENODEV;

	file->private_data = stream_alloc(host);
	if (file->private_data == NULL)
		return -ENOMEM;

	return 0;
}

static int amdtp_release(struct inode *inode, struct file *file)
{
	struct stream *s = file->private_data;

	stream_free(s);

	return 0;
}

static struct cdev amdtp_cdev;
static struct file_operations amdtp_fops =
{
	.owner =	THIS_MODULE,
	.write =	amdtp_write,
	.poll =		amdtp_poll,
	.unlocked_ioctl = amdtp_ioctl,
	.compat_ioctl = amdtp_ioctl, /* All amdtp ioctls are compatible */
	.open =		amdtp_open,
	.release =	amdtp_release
};

/* IEEE1394 Subsystem functions */

static void amdtp_add_host(struct hpsb_host *host)
{
	struct amdtp_host *ah;
	int minor;

	if (strcmp(host->driver->name, OHCI1394_DRIVER_NAME) != 0)
		return;

	ah = hpsb_create_hostinfo(&amdtp_highlevel, host, sizeof(*ah));
	if (!ah) {
		HPSB_ERR("amdtp: Unable able to alloc hostinfo");
		return;
	}

	ah->host = host;
	ah->ohci = host->hostdata;

	hpsb_set_hostinfo_key(&amdtp_highlevel, host, ah->host->id);

	minor = IEEE1394_MINOR_BLOCK_AMDTP * 16 + ah->host->id;

	INIT_LIST_HEAD(&ah->stream_list);
	spin_lock_init(&ah->stream_list_lock);

	devfs_mk_cdev(MKDEV(IEEE1394_MAJOR, minor),
			S_IFCHR|S_IRUSR|S_IWUSR, "amdtp/%d", ah->host->id);
}

static void amdtp_remove_host(struct hpsb_host *host)
{
	struct amdtp_host *ah = hpsb_get_hostinfo(&amdtp_highlevel, host);

	if (ah)
		devfs_remove("amdtp/%d", ah->host->id);

	return;
}

static struct hpsb_highlevel amdtp_highlevel = {
	.name =		"amdtp",
	.add_host =	amdtp_add_host,
	.remove_host =	amdtp_remove_host,
};

/* Module interface */

MODULE_AUTHOR("Kristian Hogsberg <hogsberg@users.sf.net>");
MODULE_DESCRIPTION("Driver for Audio & Music Data Transmission Protocol "
		   "on OHCI boards.");
MODULE_SUPPORTED_DEVICE("amdtp");
MODULE_LICENSE("GPL");

static int __init amdtp_init_module (void)
{
	cdev_init(&amdtp_cdev, &amdtp_fops);
	amdtp_cdev.owner = THIS_MODULE;
	kobject_set_name(&amdtp_cdev.kobj, "amdtp");
	if (cdev_add(&amdtp_cdev, IEEE1394_AMDTP_DEV, 16)) {
		HPSB_ERR("amdtp: unable to add char device");
 		return -EIO;
 	}

	devfs_mk_dir("amdtp");

	hpsb_register_highlevel(&amdtp_highlevel);

	HPSB_INFO("Loaded AMDTP driver");

	return 0;
}

static void __exit amdtp_exit_module (void)
{
        hpsb_unregister_highlevel(&amdtp_highlevel);
	devfs_remove("amdtp");
	cdev_del(&amdtp_cdev);

	HPSB_INFO("Unloaded AMDTP driver");
}

module_init(amdtp_init_module);
module_exit(amdtp_exit_module);
