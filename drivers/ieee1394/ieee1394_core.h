#ifndef _IEEE1394_CORE_H
#define _IEEE1394_CORE_H

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <asm/atomic.h>

#include "hosts.h"
#include "ieee1394_types.h"

struct hpsb_packet {
	/* This struct is basically read-only for hosts with the exception of
	 * the data buffer contents and xnext - see below. */

	/* This can be used for host driver internal linking.
	 *
	 * NOTE: This must be left in init state when the driver is done
	 * with it (e.g. by using list_del_init()), since the core does
	 * some sanity checks to make sure the packet is not on a
	 * driver_list when free'ing it. */
	struct list_head driver_list;

	nodeid_t node_id;

	/* Async and Iso types should be clear, raw means send-as-is, do not
	 * CRC!  Byte swapping shall still be done in this case. */
	enum { hpsb_async, hpsb_iso, hpsb_raw } __attribute__((packed)) type;

	/* Okay, this is core internal and a no care for hosts.
	 * queued   = queued for sending
	 * pending  = sent, waiting for response
	 * complete = processing completed, successful or not
	 */
	enum {
		hpsb_unused, hpsb_queued, hpsb_pending, hpsb_complete
	} __attribute__((packed)) state;

	/* These are core internal. */
	signed char tlabel;
	signed char ack_code;
	unsigned char tcode;

	unsigned expect_response:1;
	unsigned no_waiter:1;

	/* Speed to transmit with: 0 = 100Mbps, 1 = 200Mbps, 2 = 400Mbps */
	unsigned speed_code:2;

	/*
	 * *header and *data are guaranteed to be 32-bit DMAable and may be
	 * overwritten to allow in-place byte swapping.  Neither of these is
	 * CRCed (the sizes also don't include CRC), but contain space for at
	 * least one additional quadlet to allow in-place CRCing.  The memory is
	 * also guaranteed to be DMA mappable.
	 */
	quadlet_t *header;
	quadlet_t *data;
	size_t header_size;
	size_t data_size;

	struct hpsb_host *host;
	unsigned int generation;

	atomic_t refcnt;

	/* Function (and possible data to pass to it) to call when this
	 * packet is completed.  */
	void (*complete_routine)(void *);
	void *complete_data;

	/* XXX This is just a hack at the moment */
	struct sk_buff *skb;

	/* Store jiffies for implementing bus timeouts. */
	unsigned long sendtime;

	quadlet_t embedded_header[5];
};

/* Set a task for when a packet completes */
void hpsb_set_packet_complete_task(struct hpsb_packet *packet,
				   void (*routine)(void *), void *data);

static inline struct hpsb_packet *driver_packet(struct list_head *l)
{
	return list_entry(l, struct hpsb_packet, driver_list);
}

void abort_timedouts(unsigned long __opaque);

struct hpsb_packet *hpsb_alloc_packet(size_t data_size);
void hpsb_free_packet(struct hpsb_packet *packet);

/*
 * Generation counter for the complete 1394 subsystem.  Generation gets
 * incremented on every change in the subsystem (e.g. bus reset).
 *
 * Use the functions, not the variable.
 */
static inline unsigned int get_hpsb_generation(struct hpsb_host *host)
{
	return atomic_read(&host->generation);
}

/*
 * Send a PHY configuration packet, return 0 on success, negative
 * errno on failure.
 */
int hpsb_send_phy_config(struct hpsb_host *host, int rootid, int gapcnt);

/*
 * Queue packet for transmitting, return 0 on success, negative errno
 * on failure.
 */
int hpsb_send_packet(struct hpsb_packet *packet);

/*
 * Queue packet for transmitting, and block until the transaction
 * completes. Return 0 on success, negative errno on failure.
 */
int hpsb_send_packet_and_wait(struct hpsb_packet *packet);

/* Initiate bus reset on the given host.  Returns 1 if bus reset already in
 * progress, 0 otherwise. */
int hpsb_reset_bus(struct hpsb_host *host, int type);

int hpsb_read_cycle_timer(struct hpsb_host *host, u32 *cycle_timer,
			  u64 *local_time);

/*
 * The following functions are exported for host driver module usage.  All of
 * them are safe to use in interrupt contexts, although some are quite
 * complicated so you may want to run them in bottom halves instead of calling
 * them directly.
 */

/* Notify a bus reset to the core.  Returns 1 if bus reset already in progress,
 * 0 otherwise. */
int hpsb_bus_reset(struct hpsb_host *host);

/*
 * Hand over received selfid packet to the core.  Complement check (second
 * quadlet is complement of first) is expected to be done and successful.
 */
void hpsb_selfid_received(struct hpsb_host *host, quadlet_t sid);

/*
 * Notify completion of SelfID stage to the core and report new physical ID
 * and whether host is root now.
 */
void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot);

/*
 * Notify core of sending a packet.  Ackcode is the ack code returned for async
 * transmits or ACKX_SEND_ERROR if the transmission failed completely; ACKX_NONE
 * for other cases (internal errors that don't justify a panic).  Safe to call
 * from within a transmit packet routine.
 */
void hpsb_packet_sent(struct hpsb_host *host, struct hpsb_packet *packet,
		      int ackcode);

/*
 * Hand over received packet to the core.  The contents of data are expected to
 * be the full packet but with the CRCs left out (data block follows header
 * immediately), with the header (i.e. the first four quadlets) in machine byte
 * order and the data block in big endian.  *data can be safely overwritten
 * after this call.
 *
 * If the packet is a write request, write_acked is to be set to true if it was
 * ack_complete'd already, false otherwise.  This arg is ignored for any other
 * packet type.
 */
void hpsb_packet_received(struct hpsb_host *host, quadlet_t *data, size_t size,
			  int write_acked);


/*
 * CHARACTER DEVICE DISPATCHING
 *
 * All ieee1394 character device drivers share the same major number
 * (major 171).  The 256 minor numbers are allocated to the various
 * task-specific interfaces (raw1394, video1394, dv1394, etc) in
 * blocks of 16.
 *
 * The core ieee1394.o module allocates the device number region
 * 171:0-255, the various drivers must then cdev_add() their cdev
 * objects to handle their respective sub-regions.
 *
 * Minor device number block allocations:
 *
 * Block 0  (  0- 15)  raw1394
 * Block 1  ( 16- 31)  video1394
 * Block 2  ( 32- 47)  dv1394
 *
 * Blocks 3-14 free for future allocation
 *
 * Block 15 (240-255)  reserved for drivers under development, etc.
 */

#define IEEE1394_MAJOR			 171

#define IEEE1394_MINOR_BLOCK_RAW1394	   0
#define IEEE1394_MINOR_BLOCK_VIDEO1394	   1
#define IEEE1394_MINOR_BLOCK_DV1394	   2
#define IEEE1394_MINOR_BLOCK_EXPERIMENTAL 15

#define IEEE1394_CORE_DEV	  MKDEV(IEEE1394_MAJOR, 0)
#define IEEE1394_RAW1394_DEV	  MKDEV(IEEE1394_MAJOR, \
					IEEE1394_MINOR_BLOCK_RAW1394 * 16)
#define IEEE1394_VIDEO1394_DEV	  MKDEV(IEEE1394_MAJOR, \
					IEEE1394_MINOR_BLOCK_VIDEO1394 * 16)
#define IEEE1394_DV1394_DEV	  MKDEV(IEEE1394_MAJOR, \
					IEEE1394_MINOR_BLOCK_DV1394 * 16)
#define IEEE1394_EXPERIMENTAL_DEV MKDEV(IEEE1394_MAJOR, \
					IEEE1394_MINOR_BLOCK_EXPERIMENTAL * 16)

/* return the index (within a minor number block) of a file */
static inline unsigned char ieee1394_file_to_instance(struct file *file)
{
	return file->f_path.dentry->d_inode->i_cindex;
}

extern int hpsb_disable_irm;

/* Our sysfs bus entry */
extern struct bus_type ieee1394_bus_type;
extern struct class hpsb_host_class;
extern struct class *hpsb_protocol_class;

#endif /* _IEEE1394_CORE_H */
