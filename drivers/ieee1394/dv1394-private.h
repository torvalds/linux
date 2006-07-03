/*
 * dv1394-private.h - DV input/output over IEEE 1394 on OHCI chips
 *   Copyright (C)2001 Daniel Maas <dmaas@dcine.com>
 *     receive by Dan Dennedy <dan@dennedy.org>
 *
 * based on:
 *   video1394.h - driver for OHCI 1394 boards
 *   Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                          Peter Schlaile <udbz@rz.uni-karlsruhe.de>
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

#ifndef _DV_1394_PRIVATE_H
#define _DV_1394_PRIVATE_H

#include "ieee1394.h"
#include "ohci1394.h"
#include "dma.h"

/* data structures private to the dv1394 driver */
/* none of this is exposed to user-space */


/*
   the 8-byte CIP (Common Isochronous Packet) header that precedes
   each packet of DV data.

   See the IEC 61883 standard.
*/

struct CIP_header { unsigned char b[8]; };

static inline void fill_cip_header(struct CIP_header *cip,
				   unsigned char source_node_id,
				   unsigned long counter,
				   enum pal_or_ntsc format,
				   unsigned long timestamp)
{
	cip->b[0] = source_node_id;
	cip->b[1] = 0x78; /* packet size in quadlets (480/4) - even for empty packets! */
	cip->b[2] = 0x00;
	cip->b[3] = counter;

	cip->b[4] = 0x80; /* const */

	switch(format) {
	case DV1394_PAL:
		cip->b[5] = 0x80;
		break;
	case DV1394_NTSC:
		cip->b[5] = 0x00;
		break;
	}

	cip->b[6] = timestamp >> 8;
	cip->b[7] = timestamp & 0xFF;
}



/*
   DMA commands used to program the OHCI's DMA engine

   See the Texas Instruments OHCI 1394 chipset documentation.
*/

struct output_more_immediate { u32 q[8]; };
struct output_more { u32 q[4]; };
struct output_last { u32 q[4]; };
struct input_more { u32 q[4]; };
struct input_last { u32 q[4]; };

/* outputs */

static inline void fill_output_more_immediate(struct output_more_immediate *omi,
					      unsigned char tag,
					      unsigned char channel,
					      unsigned char sync_tag,
					      unsigned int  payload_size)
{
	omi->q[0] = cpu_to_le32(0x02000000 | 8); /* OUTPUT_MORE_IMMEDIATE; 8 is the size of the IT header */
	omi->q[1] = 0;
	omi->q[2] = 0;
	omi->q[3] = 0;

	/* IT packet header */
	omi->q[4] = cpu_to_le32(  (0x0 << 16)  /* IEEE1394_SPEED_100 */
				  | (tag << 14)
				  | (channel << 8)
				  | (TCODE_ISO_DATA << 4)
				  | (sync_tag) );

	/* reserved field; mimic behavior of my Sony DSR-40 */
	omi->q[5] = cpu_to_le32((payload_size << 16) | (0x7F << 8) | 0xA0);

	omi->q[6] = 0;
	omi->q[7] = 0;
}

static inline void fill_output_more(struct output_more *om,
				    unsigned int data_size,
				    unsigned long data_phys_addr)
{
	om->q[0] = cpu_to_le32(data_size);
	om->q[1] = cpu_to_le32(data_phys_addr);
	om->q[2] = 0;
	om->q[3] = 0;
}

static inline void fill_output_last(struct output_last *ol,
				    int want_timestamp,
				    int want_interrupt,
				    unsigned int data_size,
				    unsigned long data_phys_addr)
{
	u32 temp = 0;
	temp |= 1 << 28; /* OUTPUT_LAST */

	if (want_timestamp) /* controller will update timestamp at DMA time */
		temp |= 1 << 27;

	if (want_interrupt)
		temp |= 3 << 20;

	temp |= 3 << 18; /* must take branch */
	temp |= data_size;

	ol->q[0] = cpu_to_le32(temp);
	ol->q[1] = cpu_to_le32(data_phys_addr);
	ol->q[2] = 0;
	ol->q[3] = 0;
}

/* inputs */

static inline void fill_input_more(struct input_more *im,
				   int want_interrupt,
				   unsigned int data_size,
				   unsigned long data_phys_addr)
{
	u32 temp =  2 << 28; /* INPUT_MORE */
	temp |= 8 << 24; /* s = 1, update xferStatus and resCount */
	if (want_interrupt)
		temp |= 0 << 20; /* interrupts, i=0 in packet-per-buffer mode */
	temp |= 0x0 << 16; /* disable branch to address for packet-per-buffer mode */
	                       /* disable wait on sync field, not used in DV :-( */
       	temp |= data_size;

	im->q[0] = cpu_to_le32(temp);
	im->q[1] = cpu_to_le32(data_phys_addr);
	im->q[2] = 0; /* branchAddress and Z not use in packet-per-buffer mode */
	im->q[3] = 0; /* xferStatus & resCount, resCount must be initialize to data_size */
}
 
static inline void fill_input_last(struct input_last *il,
				    int want_interrupt,
				    unsigned int data_size,
				    unsigned long data_phys_addr)
{
	u32 temp =  3 << 28; /* INPUT_LAST */
	temp |= 8 << 24; /* s = 1, update xferStatus and resCount */
	if (want_interrupt)
		temp |= 3 << 20; /* enable interrupts */
	temp |= 0xC << 16; /* enable branch to address */
	                       /* disable wait on sync field, not used in DV :-( */
	temp |= data_size;

	il->q[0] = cpu_to_le32(temp);
	il->q[1] = cpu_to_le32(data_phys_addr);
	il->q[2] = cpu_to_le32(1); /* branchAddress (filled in later) and Z = 1 descriptor in next block */
	il->q[3] = cpu_to_le32(data_size); /* xferStatus & resCount, resCount must be initialize to data_size */
}



/*
   A "DMA descriptor block" consists of several contiguous DMA commands.
   struct DMA_descriptor_block encapsulates all of the commands necessary
   to send one packet of DV data.

   There are three different types of these blocks:

        1) command to send an empty packet (CIP header only, no DV data):

	    OUTPUT_MORE-Immediate <-- contains the iso header in-line
	    OUTPUT_LAST           <-- points to the CIP header

	2) command to send a full packet when the DV data payload does NOT
	   cross a page boundary:

	    OUTPUT_MORE-Immediate <-- contains the iso header in-line
	    OUTPUT_MORE           <-- points to the CIP header
	    OUTPUT_LAST           <-- points to entire DV data payload

	3) command to send a full packet when the DV payload DOES cross
	   a page boundary:

	    OUTPUT_MORE-Immediate <-- contains the iso header in-line
	    OUTPUT_MORE           <-- points to the CIP header
	    OUTPUT_MORE           <-- points to first part of DV data payload
	    OUTPUT_LAST           <-- points to second part of DV data payload

   This struct describes all three block types using unions.

   !!! It is vital that an even number of these descriptor blocks fit on one
   page of memory, since a block cannot cross a page boundary !!!

 */

struct DMA_descriptor_block {

	union {
		struct {
			/*  iso header, common to all output block types */
			struct output_more_immediate omi;

			union {
				/* empty packet */
				struct {
					struct output_last ol;  /* CIP header */
				} empty;

				/* full packet */
				struct {
					struct output_more om;  /* CIP header */

					union {
				               /* payload does not cross page boundary */
						struct {
							struct output_last ol;  /* data payload */
						} nocross;

				               /* payload crosses page boundary */
						struct {
							struct output_more om;  /* data payload */
							struct output_last ol;  /* data payload */
						} cross;
					} u;

				} full;
			} u;
		} out;

		struct {
			struct input_last il;
		} in;

	} u;

	/* ensure that PAGE_SIZE % sizeof(struct DMA_descriptor_block) == 0
	   by padding out to 128 bytes */
	u32 __pad__[12];
};


/* struct frame contains all data associated with one frame in the
   ringbuffer these are allocated when the DMA context is initialized
   do_dv1394_init().  They are re-used after the card finishes
   transmitting the frame. */

struct video_card; /* forward declaration */

struct frame {

	/* points to the struct video_card that owns this frame */
	struct video_card *video;

	/* index of this frame in video_card->frames[] */
	unsigned int frame_num;

	/* FRAME_CLEAR - DMA program not set up, waiting for data
	   FRAME_READY - DMA program written, ready to transmit

	   Changes to these should be locked against the interrupt
	*/
	enum {
		FRAME_CLEAR = 0,
		FRAME_READY
	} state;

	/* whether this frame has been DMA'ed already; used only from
	   the IRQ handler to determine whether the frame can be reset */
	int done;


	/* kernel virtual pointer to the start of this frame's data in
	   the user ringbuffer. Use only for CPU access; to get the DMA
	   bus address you must go through the video->user_dma mapping */
	unsigned long data;

	/* Max # of packets per frame */
#define MAX_PACKETS 500


	/* a PAGE_SIZE memory pool for allocating CIP headers
	   !header_pool must be aligned to PAGE_SIZE! */
	struct CIP_header *header_pool;
	dma_addr_t         header_pool_dma;


	/* a physically contiguous memory pool for allocating DMA
	   descriptor blocks; usually around 64KB in size
	   !descriptor_pool must be aligned to PAGE_SIZE! */
	struct DMA_descriptor_block *descriptor_pool;
	dma_addr_t                   descriptor_pool_dma;
	unsigned long                descriptor_pool_size;


	/* # of packets allocated for this frame */
	unsigned int n_packets;


	/* below are several pointers (kernel virtual addresses, not
	   DMA bus addresses) to parts of the DMA program.  These are
	   set each time the DMA program is written in
	   frame_prepare(). They are used later on, e.g. from the
	   interrupt handler, to check the status of the frame */

	/* points to status/timestamp field of first DMA packet */
	/* (we'll check it later to monitor timestamp accuracy) */
	u32 *frame_begin_timestamp;

	/* the timestamp we assigned to the first packet in the frame */
	u32 assigned_timestamp;

	/* pointer to the first packet's CIP header (where the timestamp goes) */
	struct CIP_header *cip_syt1;

	/* pointer to the second packet's CIP header
	   (only set if the first packet was empty) */
	struct CIP_header *cip_syt2;

	/* in order to figure out what caused an interrupt,
	   store pointers to the status fields of the two packets
	   that can cause interrupts. We'll check these from the
	   interrupt handler.
	*/
	u32 *mid_frame_timestamp;
	u32 *frame_end_timestamp;

	/* branch address field of final packet. This is effectively
	   the "tail" in the chain of DMA descriptor blocks.
	   We will fill it with the address of the first DMA descriptor
	   block in the subsequent frame, once it is ready.
	*/
	u32 *frame_end_branch;

	/* the number of descriptors in the first descriptor block
	   of the frame. Needed to start DMA */
	int first_n_descriptors;
};


struct packet {
	u16	timestamp;
	u16	invalid;
	u16	iso_header;
	u16	data_length;
	u32	cip_h1;
	u32	cip_h2;
	unsigned char data[480];
	unsigned char padding[16]; /* force struct size =512 for page alignment */
};


/* allocate/free a frame */
static struct frame* frame_new(unsigned int frame_num, struct video_card *video);
static void frame_delete(struct frame *f);

/* reset f so that it can be used again */
static void frame_reset(struct frame *f);

/* struct video_card contains all data associated with one instance
   of the dv1394 driver
*/
enum modes {
	MODE_RECEIVE,
	MODE_TRANSMIT
};

struct video_card {

	/* ohci card to which this instance corresponds */
	struct ti_ohci *ohci;

	/* OHCI card id; the link between the VFS inode and a specific video_card
	   (essentially the device minor number) */
	int id;

	/* entry in dv1394_cards */
	struct list_head list;

	/* OHCI card IT DMA context number, -1 if not in use */
	int ohci_it_ctx;
	struct ohci1394_iso_tasklet it_tasklet;

	/* register offsets for current IT DMA context, 0 if not in use */
	u32 ohci_IsoXmitContextControlSet;
	u32 ohci_IsoXmitContextControlClear;
	u32 ohci_IsoXmitCommandPtr;

	/* OHCI card IR DMA context number, -1 if not in use */
	struct ohci1394_iso_tasklet ir_tasklet;
	int ohci_ir_ctx;

	/* register offsets for current IR DMA context, 0 if not in use */
	u32 ohci_IsoRcvContextControlSet;
	u32 ohci_IsoRcvContextControlClear;
	u32 ohci_IsoRcvCommandPtr;
	u32 ohci_IsoRcvContextMatch;


	/* CONCURRENCY CONTROL */

	/* there are THREE levels of locking associated with video_card. */

	/*
	   1) the 'open' flag - this prevents more than one process from
	   opening the device. (the driver currently assumes only one opener).
	   This is a regular int, but use test_and_set_bit() (on bit zero) 
	   for atomicity.
	 */
	unsigned long open;

	/*
	   2) the spinlock - this provides mutual exclusion between the interrupt
	   handler and process-context operations. Generally you must take the
	   spinlock under the following conditions:
	     1) DMA (and hence the interrupt handler) may be running
	     AND
	     2) you need to operate on the video_card, especially active_frame

	     It is OK to play with video_card without taking the spinlock if
	     you are certain that DMA is not running. Even if DMA is running,
	     it is OK to *read* active_frame with the lock, then drop it
	     immediately. This is safe because the interrupt handler will never
	     advance active_frame onto a frame that is not READY (and the spinlock
	     must be held while marking a frame READY).

	     spinlock is also used to protect ohci_it_ctx and ohci_ir_ctx,
	     which can be accessed from both process and interrupt context
	 */
	spinlock_t spinlock;

	/* flag to prevent spurious interrupts (which OHCI seems to
	   generate a lot :) from accessing the struct */
	int dma_running;

	/*
	  3) the sleeping mutex 'mtx' - this is used from process context only,
	  to serialize various operations on the video_card. Even though only one
	  open() is allowed, we still need to prevent multiple threads of execution
	  from entering calls like read, write, ioctl, etc.

	  I honestly can't think of a good reason to use dv1394 from several threads
	  at once, but we need to serialize anyway to prevent oopses =).

	  NOTE: if you need both spinlock and mtx, take mtx first to avoid deadlock!
	 */
	struct mutex mtx;

	/* people waiting for buffer space, please form a line here... */
	wait_queue_head_t waitq;

	/* support asynchronous I/O signals (SIGIO) */
	struct fasync_struct *fasync;

	/* the large, non-contiguous (rvmalloc()) ringbuffer for DV
           data, exposed to user-space via mmap() */
	unsigned long      dv_buf_size;
	struct dma_region  dv_buf;

	/* next byte in the ringbuffer that a write() call will fill */
	size_t write_off;

	struct frame *frames[DV1394_MAX_FRAMES];

	/* n_frames also serves as an indicator that this struct video_card is
	   initialized and ready to run DMA buffers */

	int n_frames;

	/* this is the frame that is currently "owned" by the OHCI DMA controller
	   (set to -1 iff DMA is not running)

	   ! must lock against the interrupt handler when accessing it !

	   RULES:

	       Only the interrupt handler may change active_frame if DMA
	          is running; if not, process may change it

	       If the next frame is READY, the interrupt handler will advance
	       active_frame when the current frame is finished.

	       If the next frame is CLEAR, the interrupt handler will re-transmit
	       the current frame, and the dropped_frames counter will be  incremented.

	       The interrupt handler will NEVER advance active_frame to a
	       frame that is not READY.
	*/
	int active_frame;
	int first_run;

	/* the same locking rules apply to these three fields also: */

	/* altered ONLY from process context. Must check first_clear_frame->state;
	   if it's READY, that means the ringbuffer is full with READY frames;
	   if it's CLEAR, that means one or more ringbuffer frames are CLEAR */
	unsigned int first_clear_frame;

	/* altered both by process and interrupt */
	unsigned int n_clear_frames;

	/* only altered by the interrupt */
	unsigned int dropped_frames;



	/* the CIP accumulator and continuity counter are properties
	   of the DMA stream as a whole (not a single frame), so they
	   are stored here in the video_card */

	unsigned long cip_accum;
	unsigned long cip_n, cip_d;
	unsigned int syt_offset;
	unsigned int continuity_counter;

	enum pal_or_ntsc pal_or_ntsc;

	/* redundant, but simplifies the code somewhat */
	unsigned int frame_size; /* in bytes */

	/* the isochronous channel to use, -1 if video card is inactive */
	int channel;


	/* physically contiguous packet ringbuffer for receive */
	struct dma_region packet_buf;
	unsigned long  packet_buf_size;

	unsigned int current_packet;
	int first_frame; 	/* received first start frame marker? */
	enum modes mode;
};

/*
   if the video_card is not initialized, then the ONLY fields that are valid are:
   ohci
   open
   n_frames
*/

static inline int video_card_initialized(struct video_card *v)
{
	return v->n_frames > 0;
}

static int do_dv1394_init(struct video_card *video, struct dv1394_init *init);
static int do_dv1394_init_default(struct video_card *video);
static void do_dv1394_shutdown(struct video_card *video, int free_user_buf);


/* NTSC empty packet rate accurate to within 0.01%,
   calibrated against a Sony DSR-40 DVCAM deck */

#define CIP_N_NTSC   68000000
#define CIP_D_NTSC 1068000000

#define CIP_N_PAL  1
#define CIP_D_PAL 16

#endif /* _DV_1394_PRIVATE_H */

