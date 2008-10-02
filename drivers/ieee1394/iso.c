/*
 * IEEE 1394 for Linux
 *
 * kernel ISO transmission/reception
 *
 * Copyright (C) 2002 Maas Digital LLC
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "hosts.h"
#include "iso.h"

/**
 * hpsb_iso_stop - stop DMA
 */
void hpsb_iso_stop(struct hpsb_iso *iso)
{
	if (!(iso->flags & HPSB_ISO_DRIVER_STARTED))
		return;

	iso->host->driver->isoctl(iso, iso->type == HPSB_ISO_XMIT ?
				  XMIT_STOP : RECV_STOP, 0);
	iso->flags &= ~HPSB_ISO_DRIVER_STARTED;
}

/**
 * hpsb_iso_shutdown - deallocate buffer and DMA context
 */
void hpsb_iso_shutdown(struct hpsb_iso *iso)
{
	if (iso->flags & HPSB_ISO_DRIVER_INIT) {
		hpsb_iso_stop(iso);
		iso->host->driver->isoctl(iso, iso->type == HPSB_ISO_XMIT ?
					  XMIT_SHUTDOWN : RECV_SHUTDOWN, 0);
		iso->flags &= ~HPSB_ISO_DRIVER_INIT;
	}

	dma_region_free(&iso->data_buf);
	kfree(iso);
}

static struct hpsb_iso *hpsb_iso_common_init(struct hpsb_host *host,
					     enum hpsb_iso_type type,
					     unsigned int data_buf_size,
					     unsigned int buf_packets,
					     int channel, int dma_mode,
					     int irq_interval,
					     void (*callback) (struct hpsb_iso
							       *))
{
	struct hpsb_iso *iso;
	int dma_direction;

	/* make sure driver supports the ISO API */
	if (!host->driver->isoctl) {
		printk(KERN_INFO
		       "ieee1394: host driver '%s' does not support the rawiso API\n",
		       host->driver->name);
		return NULL;
	}

	/* sanitize parameters */

	if (buf_packets < 2)
		buf_packets = 2;

	if ((dma_mode < HPSB_ISO_DMA_DEFAULT)
	    || (dma_mode > HPSB_ISO_DMA_PACKET_PER_BUFFER))
		dma_mode = HPSB_ISO_DMA_DEFAULT;

	if ((irq_interval < 0) || (irq_interval > buf_packets / 4))
		irq_interval = buf_packets / 4;
	if (irq_interval == 0)	/* really interrupt for each packet */
		irq_interval = 1;

	if (channel < -1 || channel >= 64)
		return NULL;

	/* channel = -1 is OK for multi-channel recv but not for xmit */
	if (type == HPSB_ISO_XMIT && channel < 0)
		return NULL;

	/* allocate and write the struct hpsb_iso */

	iso =
	    kmalloc(sizeof(*iso) +
		    buf_packets * sizeof(struct hpsb_iso_packet_info),
		    GFP_KERNEL);
	if (!iso)
		return NULL;

	iso->infos = (struct hpsb_iso_packet_info *)(iso + 1);

	iso->type = type;
	iso->host = host;
	iso->hostdata = NULL;
	iso->callback = callback;
	init_waitqueue_head(&iso->waitq);
	iso->channel = channel;
	iso->irq_interval = irq_interval;
	iso->dma_mode = dma_mode;
	dma_region_init(&iso->data_buf);
	iso->buf_size = PAGE_ALIGN(data_buf_size);
	iso->buf_packets = buf_packets;
	iso->pkt_dma = 0;
	iso->first_packet = 0;
	spin_lock_init(&iso->lock);

	if (iso->type == HPSB_ISO_XMIT) {
		iso->n_ready_packets = iso->buf_packets;
		dma_direction = PCI_DMA_TODEVICE;
	} else {
		iso->n_ready_packets = 0;
		dma_direction = PCI_DMA_FROMDEVICE;
	}

	atomic_set(&iso->overflows, 0);
	iso->bytes_discarded = 0;
	iso->flags = 0;
	iso->prebuffer = 0;

	/* allocate the packet buffer */
	if (dma_region_alloc
	    (&iso->data_buf, iso->buf_size, host->pdev, dma_direction))
		goto err;

	return iso;

      err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

/**
 * hpsb_iso_n_ready - returns number of packets ready to send or receive
 */
int hpsb_iso_n_ready(struct hpsb_iso *iso)
{
	unsigned long flags;
	int val;

	spin_lock_irqsave(&iso->lock, flags);
	val = iso->n_ready_packets;
	spin_unlock_irqrestore(&iso->lock, flags);

	return val;
}

/**
 * hpsb_iso_xmit_init - allocate the buffer and DMA context
 */
struct hpsb_iso *hpsb_iso_xmit_init(struct hpsb_host *host,
				    unsigned int data_buf_size,
				    unsigned int buf_packets,
				    int channel,
				    int speed,
				    int irq_interval,
				    void (*callback) (struct hpsb_iso *))
{
	struct hpsb_iso *iso = hpsb_iso_common_init(host, HPSB_ISO_XMIT,
						    data_buf_size, buf_packets,
						    channel,
						    HPSB_ISO_DMA_DEFAULT,
						    irq_interval, callback);
	if (!iso)
		return NULL;

	iso->speed = speed;

	/* tell the driver to start working */
	if (host->driver->isoctl(iso, XMIT_INIT, 0))
		goto err;

	iso->flags |= HPSB_ISO_DRIVER_INIT;
	return iso;

      err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

/**
 * hpsb_iso_recv_init - allocate the buffer and DMA context
 *
 * Note, if channel = -1, multi-channel receive is enabled.
 */
struct hpsb_iso *hpsb_iso_recv_init(struct hpsb_host *host,
				    unsigned int data_buf_size,
				    unsigned int buf_packets,
				    int channel,
				    int dma_mode,
				    int irq_interval,
				    void (*callback) (struct hpsb_iso *))
{
	struct hpsb_iso *iso = hpsb_iso_common_init(host, HPSB_ISO_RECV,
						    data_buf_size, buf_packets,
						    channel, dma_mode,
						    irq_interval, callback);
	if (!iso)
		return NULL;

	/* tell the driver to start working */
	if (host->driver->isoctl(iso, RECV_INIT, 0))
		goto err;

	iso->flags |= HPSB_ISO_DRIVER_INIT;
	return iso;

      err:
	hpsb_iso_shutdown(iso);
	return NULL;
}

/**
 * hpsb_iso_recv_listen_channel
 *
 * multi-channel only
 */
int hpsb_iso_recv_listen_channel(struct hpsb_iso *iso, unsigned char channel)
{
	if (iso->type != HPSB_ISO_RECV || iso->channel != -1 || channel >= 64)
		return -EINVAL;
	return iso->host->driver->isoctl(iso, RECV_LISTEN_CHANNEL, channel);
}

/**
 * hpsb_iso_recv_unlisten_channel
 *
 * multi-channel only
 */
int hpsb_iso_recv_unlisten_channel(struct hpsb_iso *iso, unsigned char channel)
{
	if (iso->type != HPSB_ISO_RECV || iso->channel != -1 || channel >= 64)
		return -EINVAL;
	return iso->host->driver->isoctl(iso, RECV_UNLISTEN_CHANNEL, channel);
}

/**
 * hpsb_iso_recv_set_channel_mask
 *
 * multi-channel only
 */
int hpsb_iso_recv_set_channel_mask(struct hpsb_iso *iso, u64 mask)
{
	if (iso->type != HPSB_ISO_RECV || iso->channel != -1)
		return -EINVAL;
	return iso->host->driver->isoctl(iso, RECV_SET_CHANNEL_MASK,
					 (unsigned long)&mask);
}

/**
 * hpsb_iso_recv_flush - check for arrival of new packets
 *
 * check for arrival of new packets immediately (even if irq_interval
 * has not yet been reached)
 */
int hpsb_iso_recv_flush(struct hpsb_iso *iso)
{
	if (iso->type != HPSB_ISO_RECV)
		return -EINVAL;
	return iso->host->driver->isoctl(iso, RECV_FLUSH, 0);
}

static int do_iso_xmit_start(struct hpsb_iso *iso, int cycle)
{
	int retval = iso->host->driver->isoctl(iso, XMIT_START, cycle);
	if (retval)
		return retval;

	iso->flags |= HPSB_ISO_DRIVER_STARTED;
	return retval;
}

/**
 * hpsb_iso_xmit_start - start DMA
 */
int hpsb_iso_xmit_start(struct hpsb_iso *iso, int cycle, int prebuffer)
{
	if (iso->type != HPSB_ISO_XMIT)
		return -1;

	if (iso->flags & HPSB_ISO_DRIVER_STARTED)
		return 0;

	if (cycle < -1)
		cycle = -1;
	else if (cycle >= 8000)
		cycle %= 8000;

	iso->xmit_cycle = cycle;

	if (prebuffer < 0)
		prebuffer = iso->buf_packets - 1;
	else if (prebuffer == 0)
		prebuffer = 1;

	if (prebuffer >= iso->buf_packets)
		prebuffer = iso->buf_packets - 1;

	iso->prebuffer = prebuffer;

	/* remember the starting cycle; DMA will commence from xmit_queue_packets()
	   once enough packets have been buffered */
	iso->start_cycle = cycle;

	return 0;
}

/**
 * hpsb_iso_recv_start - start DMA
 */
int hpsb_iso_recv_start(struct hpsb_iso *iso, int cycle, int tag_mask, int sync)
{
	int retval = 0;
	int isoctl_args[3];

	if (iso->type != HPSB_ISO_RECV)
		return -1;

	if (iso->flags & HPSB_ISO_DRIVER_STARTED)
		return 0;

	if (cycle < -1)
		cycle = -1;
	else if (cycle >= 8000)
		cycle %= 8000;

	isoctl_args[0] = cycle;

	if (tag_mask < 0)
		/* match all tags */
		tag_mask = 0xF;
	isoctl_args[1] = tag_mask;

	isoctl_args[2] = sync;

	retval =
	    iso->host->driver->isoctl(iso, RECV_START,
				      (unsigned long)&isoctl_args[0]);
	if (retval)
		return retval;

	iso->flags |= HPSB_ISO_DRIVER_STARTED;
	return retval;
}

/* check to make sure the user has not supplied bogus values of offset/len
 * that would cause the kernel to access memory outside the buffer */
static int hpsb_iso_check_offset_len(struct hpsb_iso *iso,
				     unsigned int offset, unsigned short len,
				     unsigned int *out_offset,
				     unsigned short *out_len)
{
	if (offset >= iso->buf_size)
		return -EFAULT;

	/* make sure the packet does not go beyond the end of the buffer */
	if (offset + len > iso->buf_size)
		return -EFAULT;

	/* check for wrap-around */
	if (offset + len < offset)
		return -EFAULT;

	/* now we can trust 'offset' and 'length' */
	*out_offset = offset;
	*out_len = len;

	return 0;
}

/**
 * hpsb_iso_xmit_queue_packet - queue a packet for transmission.
 *
 * @offset is relative to the beginning of the DMA buffer, where the packet's
 * data payload should already have been placed.
 */
int hpsb_iso_xmit_queue_packet(struct hpsb_iso *iso, u32 offset, u16 len,
			       u8 tag, u8 sy)
{
	struct hpsb_iso_packet_info *info;
	unsigned long flags;
	int rv;

	if (iso->type != HPSB_ISO_XMIT)
		return -EINVAL;

	/* is there space in the buffer? */
	if (iso->n_ready_packets <= 0) {
		return -EBUSY;
	}

	info = &iso->infos[iso->first_packet];

	/* check for bogus offset/length */
	if (hpsb_iso_check_offset_len
	    (iso, offset, len, &info->offset, &info->len))
		return -EFAULT;

	info->tag = tag;
	info->sy = sy;

	spin_lock_irqsave(&iso->lock, flags);

	rv = iso->host->driver->isoctl(iso, XMIT_QUEUE, (unsigned long)info);
	if (rv)
		goto out;

	/* increment cursors */
	iso->first_packet = (iso->first_packet + 1) % iso->buf_packets;
	iso->xmit_cycle = (iso->xmit_cycle + 1) % 8000;
	iso->n_ready_packets--;

	if (iso->prebuffer != 0) {
		iso->prebuffer--;
		if (iso->prebuffer <= 0) {
			iso->prebuffer = 0;
			rv = do_iso_xmit_start(iso, iso->start_cycle);
		}
	}

      out:
	spin_unlock_irqrestore(&iso->lock, flags);
	return rv;
}

/**
 * hpsb_iso_xmit_sync - wait until all queued packets have been transmitted
 */
int hpsb_iso_xmit_sync(struct hpsb_iso *iso)
{
	if (iso->type != HPSB_ISO_XMIT)
		return -EINVAL;

	return wait_event_interruptible(iso->waitq,
					hpsb_iso_n_ready(iso) ==
					iso->buf_packets);
}

/**
 * hpsb_iso_packet_sent
 *
 * Available to low-level drivers.
 *
 * Call after a packet has been transmitted to the bus (interrupt context is
 * OK).  @cycle is the _exact_ cycle the packet was sent on.  @error should be
 * non-zero if some sort of error occurred when sending the packet.
 */
void hpsb_iso_packet_sent(struct hpsb_iso *iso, int cycle, int error)
{
	unsigned long flags;
	spin_lock_irqsave(&iso->lock, flags);

	/* predict the cycle of the next packet to be queued */

	/* jump ahead by the number of packets that are already buffered */
	cycle += iso->buf_packets - iso->n_ready_packets;
	cycle %= 8000;

	iso->xmit_cycle = cycle;
	iso->n_ready_packets++;
	iso->pkt_dma = (iso->pkt_dma + 1) % iso->buf_packets;

	if (iso->n_ready_packets == iso->buf_packets || error != 0) {
		/* the buffer has run empty! */
		atomic_inc(&iso->overflows);
	}

	spin_unlock_irqrestore(&iso->lock, flags);
}

/**
 * hpsb_iso_packet_received
 *
 * Available to low-level drivers.
 *
 * Call after a packet has been received (interrupt context is OK).
 */
void hpsb_iso_packet_received(struct hpsb_iso *iso, u32 offset, u16 len,
			      u16 total_len, u16 cycle, u8 channel, u8 tag,
			      u8 sy)
{
	unsigned long flags;
	spin_lock_irqsave(&iso->lock, flags);

	if (iso->n_ready_packets == iso->buf_packets) {
		/* overflow! */
		atomic_inc(&iso->overflows);
		/* Record size of this discarded packet */
		iso->bytes_discarded += total_len;
	} else {
		struct hpsb_iso_packet_info *info = &iso->infos[iso->pkt_dma];
		info->offset = offset;
		info->len = len;
		info->total_len = total_len;
		info->cycle = cycle;
		info->channel = channel;
		info->tag = tag;
		info->sy = sy;

		iso->pkt_dma = (iso->pkt_dma + 1) % iso->buf_packets;
		iso->n_ready_packets++;
	}

	spin_unlock_irqrestore(&iso->lock, flags);
}

/**
 * hpsb_iso_recv_release_packets - release packets, reuse buffer
 *
 * @n_packets have been read out of the buffer, re-use the buffer space
 */
int hpsb_iso_recv_release_packets(struct hpsb_iso *iso, unsigned int n_packets)
{
	unsigned long flags;
	unsigned int i;
	int rv = 0;

	if (iso->type != HPSB_ISO_RECV)
		return -1;

	spin_lock_irqsave(&iso->lock, flags);
	for (i = 0; i < n_packets; i++) {
		rv = iso->host->driver->isoctl(iso, RECV_RELEASE,
					       (unsigned long)&iso->infos[iso->
									  first_packet]);
		if (rv)
			break;

		iso->first_packet = (iso->first_packet + 1) % iso->buf_packets;
		iso->n_ready_packets--;

		/* release memory from packets discarded when queue was full  */
		if (iso->n_ready_packets == 0) {	/* Release only after all prior packets handled */
			if (iso->bytes_discarded != 0) {
				struct hpsb_iso_packet_info inf;
				inf.total_len = iso->bytes_discarded;
				iso->host->driver->isoctl(iso, RECV_RELEASE,
							  (unsigned long)&inf);
				iso->bytes_discarded = 0;
			}
		}
	}
	spin_unlock_irqrestore(&iso->lock, flags);
	return rv;
}

/**
 * hpsb_iso_wake
 *
 * Available to low-level drivers.
 *
 * Call to wake waiting processes after buffer space has opened up.
 */
void hpsb_iso_wake(struct hpsb_iso *iso)
{
	wake_up_interruptible(&iso->waitq);

	if (iso->callback)
		iso->callback(iso);
}
