/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Cavium Networks
 *
 * Some parts of the code were originally released under BSD license:
 *
 * Copyright (c) 2003-2010 Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of Cavium Networks nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its associated
 * regulations, and may be subject to export or import regulations in other
 * countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION
 * OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/prefetch.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/usb.h>

#include <linux/time.h>
#include <linux/delay.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-iob-defs.h>

#include <linux/usb/hcd.h>

#include <linux/err.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-sysinfo.h>
#include <asm/octeon/cvmx-helper-board.h>

#include "octeon-hcd.h"

/**
 * enum cvmx_usb_speed - the possible USB device speeds
 *
 * @CVMX_USB_SPEED_HIGH: Device is operation at 480Mbps
 * @CVMX_USB_SPEED_FULL: Device is operation at 12Mbps
 * @CVMX_USB_SPEED_LOW:  Device is operation at 1.5Mbps
 */
enum cvmx_usb_speed {
	CVMX_USB_SPEED_HIGH = 0,
	CVMX_USB_SPEED_FULL = 1,
	CVMX_USB_SPEED_LOW = 2,
};

/**
 * enum cvmx_usb_transfer - the possible USB transfer types
 *
 * @CVMX_USB_TRANSFER_CONTROL:	   USB transfer type control for hub and status
 *				   transfers
 * @CVMX_USB_TRANSFER_ISOCHRONOUS: USB transfer type isochronous for low
 *				   priority periodic transfers
 * @CVMX_USB_TRANSFER_BULK:	   USB transfer type bulk for large low priority
 *				   transfers
 * @CVMX_USB_TRANSFER_INTERRUPT:   USB transfer type interrupt for high priority
 *				   periodic transfers
 */
enum cvmx_usb_transfer {
	CVMX_USB_TRANSFER_CONTROL = 0,
	CVMX_USB_TRANSFER_ISOCHRONOUS = 1,
	CVMX_USB_TRANSFER_BULK = 2,
	CVMX_USB_TRANSFER_INTERRUPT = 3,
};

/**
 * enum cvmx_usb_direction - the transfer directions
 *
 * @CVMX_USB_DIRECTION_OUT: Data is transferring from Octeon to the device/host
 * @CVMX_USB_DIRECTION_IN:  Data is transferring from the device/host to Octeon
 */
enum cvmx_usb_direction {
	CVMX_USB_DIRECTION_OUT,
	CVMX_USB_DIRECTION_IN,
};

/**
 * enum cvmx_usb_complete - possible callback function status codes
 *
 * @CVMX_USB_COMPLETE_SUCCESS:	  The transaction / operation finished without
 *				  any errors
 * @CVMX_USB_COMPLETE_SHORT:	  FIXME: This is currently not implemented
 * @CVMX_USB_COMPLETE_CANCEL:	  The transaction was canceled while in flight
 *				  by a user call to cvmx_usb_cancel
 * @CVMX_USB_COMPLETE_ERROR:	  The transaction aborted with an unexpected
 *				  error status
 * @CVMX_USB_COMPLETE_STALL:	  The transaction received a USB STALL response
 *				  from the device
 * @CVMX_USB_COMPLETE_XACTERR:	  The transaction failed with an error from the
 *				  device even after a number of retries
 * @CVMX_USB_COMPLETE_DATATGLERR: The transaction failed with a data toggle
 *				  error even after a number of retries
 * @CVMX_USB_COMPLETE_BABBLEERR:  The transaction failed with a babble error
 * @CVMX_USB_COMPLETE_FRAMEERR:	  The transaction failed with a frame error
 *				  even after a number of retries
 */
enum cvmx_usb_complete {
	CVMX_USB_COMPLETE_SUCCESS,
	CVMX_USB_COMPLETE_SHORT,
	CVMX_USB_COMPLETE_CANCEL,
	CVMX_USB_COMPLETE_ERROR,
	CVMX_USB_COMPLETE_STALL,
	CVMX_USB_COMPLETE_XACTERR,
	CVMX_USB_COMPLETE_DATATGLERR,
	CVMX_USB_COMPLETE_BABBLEERR,
	CVMX_USB_COMPLETE_FRAMEERR,
};

/**
 * struct cvmx_usb_port_status - the USB port status information
 *
 * @port_enabled:	1 = Usb port is enabled, 0 = disabled
 * @port_over_current:	1 = Over current detected, 0 = Over current not
 *			detected. Octeon doesn't support over current detection.
 * @port_powered:	1 = Port power is being supplied to the device, 0 =
 *			power is off. Octeon doesn't support turning port power
 *			off.
 * @port_speed:		Current port speed.
 * @connected:		1 = A device is connected to the port, 0 = No device is
 *			connected.
 * @connect_change:	1 = Device connected state changed since the last set
 *			status call.
 */
struct cvmx_usb_port_status {
	uint32_t reserved		: 25;
	uint32_t port_enabled		: 1;
	uint32_t port_over_current	: 1;
	uint32_t port_powered		: 1;
	enum cvmx_usb_speed port_speed	: 2;
	uint32_t connected		: 1;
	uint32_t connect_change		: 1;
};

/**
 * struct cvmx_usb_iso_packet - descriptor for Isochronous packets
 *
 * @offset:	This is the offset in bytes into the main buffer where this data
 *		is stored.
 * @length:	This is the length in bytes of the data.
 * @status:	This is the status of this individual packet transfer.
 */
struct cvmx_usb_iso_packet {
	int offset;
	int length;
	enum cvmx_usb_complete status;
};

/**
 * enum cvmx_usb_initialize_flags - flags used by the initialization function
 *
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI:    The USB port uses a 12MHz crystal
 *					      as clock source at USB_XO and
 *					      USB_XI.
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND:   The USB port uses 12/24/48MHz 2.5V
 *					      board clock source at USB_XO.
 *					      USB_XI should be tied to GND.
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK: Mask for clock speed field
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ:    Speed of reference clock or
 *					      crystal
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ:    Speed of reference clock
 * @CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ:    Speed of reference clock
 * @CVMX_USB_INITIALIZE_FLAGS_NO_DMA:	      Disable DMA and used polled IO for
 *					      data transfer use for the USB
 */
enum cvmx_usb_initialize_flags {
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI		= 1 << 0,
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND		= 1 << 1,
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK	= 3 << 3,
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ		= 1 << 3,
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ		= 2 << 3,
	CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ		= 3 << 3,
	/* Bits 3-4 used to encode the clock frequency */
	CVMX_USB_INITIALIZE_FLAGS_NO_DMA		= 1 << 5,
};

/**
 * enum cvmx_usb_pipe_flags - internal flags for a pipe.
 *
 * @__CVMX_USB_PIPE_FLAGS_SCHEDULED: Used internally to determine if a pipe is
 *				     actively using hardware. Do not use.
 * @__CVMX_USB_PIPE_FLAGS_NEED_PING: Used internally to determine if a high
 *				     speed pipe is in the ping state. Do not
 *				     use.
 */
enum cvmx_usb_pipe_flags {
	__CVMX_USB_PIPE_FLAGS_SCHEDULED	= 1 << 17,
	__CVMX_USB_PIPE_FLAGS_NEED_PING	= 1 << 18,
};

/* Maximum number of times to retry failed transactions */
#define MAX_RETRIES		3

/* Maximum number of hardware channels supported by the USB block */
#define MAX_CHANNELS		8

/* The highest valid USB device address */
#define MAX_USB_ADDRESS		127

/* The highest valid USB endpoint number */
#define MAX_USB_ENDPOINT	15

/* The highest valid port number on a hub */
#define MAX_USB_HUB_PORT	15

/*
 * The low level hardware can transfer a maximum of this number of bytes in each
 * transfer. The field is 19 bits wide
 */
#define MAX_TRANSFER_BYTES	((1<<19)-1)

/*
 * The low level hardware can transfer a maximum of this number of packets in
 * each transfer. The field is 10 bits wide
 */
#define MAX_TRANSFER_PACKETS	((1<<10)-1)

/**
 * Logical transactions may take numerous low level
 * transactions, especially when splits are concerned. This
 * enum represents all of the possible stages a transaction can
 * be in. Note that split completes are always even. This is so
 * the NAK handler can backup to the previous low level
 * transaction with a simple clearing of bit 0.
 */
enum cvmx_usb_stage {
	CVMX_USB_STAGE_NON_CONTROL,
	CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE,
	CVMX_USB_STAGE_SETUP,
	CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE,
	CVMX_USB_STAGE_DATA,
	CVMX_USB_STAGE_DATA_SPLIT_COMPLETE,
	CVMX_USB_STAGE_STATUS,
	CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE,
};

/**
 * struct cvmx_usb_transaction - describes each pending USB transaction
 *				 regardless of type. These are linked together
 *				 to form a list of pending requests for a pipe.
 *
 * @node:		List node for transactions in the pipe.
 * @type:		Type of transaction, duplicated of the pipe.
 * @flags:		State flags for this transaction.
 * @buffer:		User's physical buffer address to read/write.
 * @buffer_length:	Size of the user's buffer in bytes.
 * @control_header:	For control transactions, physical address of the 8
 *			byte standard header.
 * @iso_start_frame:	For ISO transactions, the starting frame number.
 * @iso_number_packets:	For ISO transactions, the number of packets in the
 *			request.
 * @iso_packets:	For ISO transactions, the sub packets in the request.
 * @actual_bytes:	Actual bytes transfer for this transaction.
 * @stage:		For control transactions, the current stage.
 * @urb:		URB.
 */
struct cvmx_usb_transaction {
	struct list_head node;
	enum cvmx_usb_transfer type;
	uint64_t buffer;
	int buffer_length;
	uint64_t control_header;
	int iso_start_frame;
	int iso_number_packets;
	struct cvmx_usb_iso_packet *iso_packets;
	int xfersize;
	int pktcnt;
	int retries;
	int actual_bytes;
	enum cvmx_usb_stage stage;
	struct urb *urb;
};

/**
 * struct cvmx_usb_pipe - a pipe represents a virtual connection between Octeon
 *			  and some USB device. It contains a list of pending
 *			  request to the device.
 *
 * @node:		List node for pipe list
 * @next:		Pipe after this one in the list
 * @transactions:	List of pending transactions
 * @interval:		For periodic pipes, the interval between packets in
 *			frames
 * @next_tx_frame:	The next frame this pipe is allowed to transmit on
 * @flags:		State flags for this pipe
 * @device_speed:	Speed of device connected to this pipe
 * @transfer_type:	Type of transaction supported by this pipe
 * @transfer_dir:	IN or OUT. Ignored for Control
 * @multi_count:	Max packet in a row for the device
 * @max_packet:		The device's maximum packet size in bytes
 * @device_addr:	USB device address at other end of pipe
 * @endpoint_num:	USB endpoint number at other end of pipe
 * @hub_device_addr:	Hub address this device is connected to
 * @hub_port:		Hub port this device is connected to
 * @pid_toggle:		This toggles between 0/1 on every packet send to track
 *			the data pid needed
 * @channel:		Hardware DMA channel for this pipe
 * @split_sc_frame:	The low order bits of the frame number the split
 *			complete should be sent on
 */
struct cvmx_usb_pipe {
	struct list_head node;
	struct list_head transactions;
	uint64_t interval;
	uint64_t next_tx_frame;
	enum cvmx_usb_pipe_flags flags;
	enum cvmx_usb_speed device_speed;
	enum cvmx_usb_transfer transfer_type;
	enum cvmx_usb_direction transfer_dir;
	int multi_count;
	uint16_t max_packet;
	uint8_t device_addr;
	uint8_t endpoint_num;
	uint8_t hub_device_addr;
	uint8_t hub_port;
	uint8_t pid_toggle;
	uint8_t channel;
	int8_t split_sc_frame;
};

struct cvmx_usb_tx_fifo {
	struct {
		int channel;
		int size;
		uint64_t address;
	} entry[MAX_CHANNELS+1];
	int head;
	int tail;
};

/**
 * struct cvmx_usb_state - the state of the USB block
 *
 * init_flags:		   Flags passed to initialize.
 * index:		   Which USB block this is for.
 * idle_hardware_channels: Bit set for every idle hardware channel.
 * usbcx_hprt:		   Stored port status so we don't need to read a CSR to
 *			   determine splits.
 * pipe_for_channel:	   Map channels to pipes.
 * pipe:		   Storage for pipes.
 * indent:		   Used by debug output to indent functions.
 * port_status:		   Last port status used for change notification.
 * idle_pipes:		   List of open pipes that have no transactions.
 * active_pipes:	   Active pipes indexed by transfer type.
 * frame_number:	   Increments every SOF interrupt for time keeping.
 * active_split:	   Points to the current active split, or NULL.
 */
struct cvmx_usb_state {
	int init_flags;
	int index;
	int idle_hardware_channels;
	union cvmx_usbcx_hprt usbcx_hprt;
	struct cvmx_usb_pipe *pipe_for_channel[MAX_CHANNELS];
	int indent;
	struct cvmx_usb_port_status port_status;
	struct list_head idle_pipes;
	struct list_head active_pipes[4];
	uint64_t frame_number;
	struct cvmx_usb_transaction *active_split;
	struct cvmx_usb_tx_fifo periodic;
	struct cvmx_usb_tx_fifo nonperiodic;
};

struct octeon_hcd {
	spinlock_t lock;
	struct cvmx_usb_state usb;
};

/* This macro spins on a field waiting for it to reach a value */
#define CVMX_WAIT_FOR_FIELD32(address, type, field, op, value, timeout_usec)\
	({int result;							    \
	do {								    \
		uint64_t done = cvmx_get_cycle() + (uint64_t)timeout_usec * \
			octeon_get_clock_rate() / 1000000;		    \
		type c;							    \
		while (1) {						    \
			c.u32 = __cvmx_usb_read_csr32(usb, address);	    \
			if (c.s.field op (value)) {			    \
				result = 0;				    \
				break;					    \
			} else if (cvmx_get_cycle() > done) {		    \
				result = -1;				    \
				break;					    \
			} else						    \
				cvmx_wait(100);				    \
		}							    \
	} while (0);							    \
	result; })

/*
 * This macro logically sets a single field in a CSR. It does the sequence
 * read, modify, and write
 */
#define USB_SET_FIELD32(address, type, field, value)		\
	do {							\
		type c;						\
		c.u32 = __cvmx_usb_read_csr32(usb, address);	\
		c.s.field = value;				\
		__cvmx_usb_write_csr32(usb, address, c.u32);	\
	} while (0)

/* Returns the IO address to push/pop stuff data from the FIFOs */
#define USB_FIFO_ADDRESS(channel, usb_index) \
	(CVMX_USBCX_GOTGCTL(usb_index) + ((channel)+1)*0x1000)

/**
 * struct octeon_temp_buffer - a bounce buffer for USB transfers
 * @temp_buffer: the newly allocated temporary buffer (including meta-data)
 * @orig_buffer: the original buffer passed by the USB stack
 * @data:	 the newly allocated temporary buffer (excluding meta-data)
 *
 * Both the DMA engine and FIFO mode will always transfer full 32-bit words. If
 * the buffer is too short, we need to allocate a temporary one, and this struct
 * represents it.
 */
struct octeon_temp_buffer {
	void *temp_buffer;
	void *orig_buffer;
	u8 data[0];
};

static inline struct octeon_hcd *cvmx_usb_to_octeon(struct cvmx_usb_state *p)
{
	return container_of(p, struct octeon_hcd, usb);
}

static inline struct usb_hcd *octeon_to_hcd(struct octeon_hcd *p)
{
	return container_of((void *)p, struct usb_hcd, hcd_priv);
}

/**
 * octeon_alloc_temp_buffer - allocate a temporary buffer for USB transfer
 *                            (if needed)
 * @urb:	URB.
 * @mem_flags:	Memory allocation flags.
 *
 * This function allocates a temporary bounce buffer whenever it's needed
 * due to HW limitations.
 */
static int octeon_alloc_temp_buffer(struct urb *urb, gfp_t mem_flags)
{
	struct octeon_temp_buffer *temp;

	if (urb->num_sgs || urb->sg ||
	    (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP) ||
	    !(urb->transfer_buffer_length % sizeof(u32)))
		return 0;

	temp = kmalloc(ALIGN(urb->transfer_buffer_length, sizeof(u32)) +
		       sizeof(*temp), mem_flags);
	if (!temp)
		return -ENOMEM;

	temp->temp_buffer = temp;
	temp->orig_buffer = urb->transfer_buffer;
	if (usb_urb_dir_out(urb))
		memcpy(temp->data, urb->transfer_buffer,
		       urb->transfer_buffer_length);
	urb->transfer_buffer = temp->data;
	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

/**
 * octeon_free_temp_buffer - free a temporary buffer used by USB transfers.
 * @urb: URB.
 *
 * Frees a buffer allocated by octeon_alloc_temp_buffer().
 */
static void octeon_free_temp_buffer(struct urb *urb)
{
	struct octeon_temp_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;

	temp = container_of(urb->transfer_buffer, struct octeon_temp_buffer,
			    data);
	if (usb_urb_dir_in(urb))
		memcpy(temp->orig_buffer, urb->transfer_buffer,
		       urb->actual_length);
	urb->transfer_buffer = temp->orig_buffer;
	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;
	kfree(temp->temp_buffer);
}

/**
 * octeon_map_urb_for_dma - Octeon-specific map_urb_for_dma().
 * @hcd:	USB HCD structure.
 * @urb:	URB.
 * @mem_flags:	Memory allocation flags.
 */
static int octeon_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				  gfp_t mem_flags)
{
	int ret;

	ret = octeon_alloc_temp_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret)
		octeon_free_temp_buffer(urb);

	return ret;
}

/**
 * octeon_unmap_urb_for_dma - Octeon-specific unmap_urb_for_dma()
 * @hcd:	USB HCD structure.
 * @urb:	URB.
 */
static void octeon_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	octeon_free_temp_buffer(urb);
}

/**
 * Read a USB 32bit CSR. It performs the necessary address swizzle
 * for 32bit CSRs and logs the value in a readable format if
 * debugging is on.
 *
 * @usb:     USB block this access is for
 * @address: 64bit address to read
 *
 * Returns: Result of the read
 */
static inline uint32_t __cvmx_usb_read_csr32(struct cvmx_usb_state *usb,
					     uint64_t address)
{
	uint32_t result = cvmx_read64_uint32(address ^ 4);
	return result;
}


/**
 * Write a USB 32bit CSR. It performs the necessary address
 * swizzle for 32bit CSRs and logs the value in a readable format
 * if debugging is on.
 *
 * @usb:     USB block this access is for
 * @address: 64bit address to write
 * @value:   Value to write
 */
static inline void __cvmx_usb_write_csr32(struct cvmx_usb_state *usb,
					  uint64_t address, uint32_t value)
{
	cvmx_write64_uint32(address ^ 4, value);
	cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
}


/**
 * Read a USB 64bit CSR. It logs the value in a readable format if
 * debugging is on.
 *
 * @usb:     USB block this access is for
 * @address: 64bit address to read
 *
 * Returns: Result of the read
 */
static inline uint64_t __cvmx_usb_read_csr64(struct cvmx_usb_state *usb,
					     uint64_t address)
{
	uint64_t result = cvmx_read64_uint64(address);
	return result;
}


/**
 * Write a USB 64bit CSR. It logs the value in a readable format
 * if debugging is on.
 *
 * @usb:     USB block this access is for
 * @address: 64bit address to write
 * @value:   Value to write
 */
static inline void __cvmx_usb_write_csr64(struct cvmx_usb_state *usb,
					  uint64_t address, uint64_t value)
{
	cvmx_write64_uint64(address, value);
}

/**
 * Return non zero if this pipe connects to a non HIGH speed
 * device through a high speed hub.
 *
 * @usb:    USB block this access is for
 * @pipe:   Pipe to check
 *
 * Returns: Non zero if we need to do split transactions
 */
static inline int __cvmx_usb_pipe_needs_split(struct cvmx_usb_state *usb,
					      struct cvmx_usb_pipe *pipe)
{
	return pipe->device_speed != CVMX_USB_SPEED_HIGH &&
	       usb->usbcx_hprt.s.prtspd == CVMX_USB_SPEED_HIGH;
}


/**
 * Trivial utility function to return the correct PID for a pipe
 *
 * @pipe:   pipe to check
 *
 * Returns: PID for pipe
 */
static inline int __cvmx_usb_get_data_pid(struct cvmx_usb_pipe *pipe)
{
	if (pipe->pid_toggle)
		return 2; /* Data1 */
	return 0; /* Data0 */
}

/**
 * Initialize a USB port for use. This must be called before any
 * other access to the Octeon USB port is made. The port starts
 * off in the disabled state.
 *
 * @usb:	 Pointer to an empty struct cvmx_usb_state
 *		 that will be populated by the initialize call.
 *		 This structure is then passed to all other USB
 *		 functions.
 * @usb_port_number:
 *		 Which Octeon USB port to initialize.
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_initialize(struct cvmx_usb_state *usb,
			       int usb_port_number,
			       enum cvmx_usb_initialize_flags flags)
{
	union cvmx_usbnx_clk_ctl usbn_clk_ctl;
	union cvmx_usbnx_usbp_ctl_status usbn_usbp_ctl_status;
	int i;

	/* At first allow 0-1 for the usb port number */
	if ((usb_port_number < 0) || (usb_port_number > 1))
		return -EINVAL;

	memset(usb, 0, sizeof(*usb));
	usb->init_flags = flags;

	/* Initialize the USB state structure */
	usb->index = usb_port_number;
	INIT_LIST_HEAD(&usb->idle_pipes);
	for (i = 0; i < ARRAY_SIZE(usb->active_pipes); i++)
		INIT_LIST_HEAD(&usb->active_pipes[i]);

	/*
	 * Power On Reset and PHY Initialization
	 *
	 * 1. Wait for DCOK to assert (nothing to do)
	 *
	 * 2a. Write USBN0/1_CLK_CTL[POR] = 1 and
	 *     USBN0/1_CLK_CTL[HRST,PRST,HCLK_RST] = 0
	 */
	usbn_clk_ctl.u64 =
		__cvmx_usb_read_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index));
	usbn_clk_ctl.s.por = 1;
	usbn_clk_ctl.s.hrst = 0;
	usbn_clk_ctl.s.prst = 0;
	usbn_clk_ctl.s.hclk_rst = 0;
	usbn_clk_ctl.s.enable = 0;
	/*
	 * 2b. Select the USB reference clock/crystal parameters by writing
	 *     appropriate values to USBN0/1_CLK_CTL[P_C_SEL, P_RTYPE, P_COM_ON]
	 */
	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND) {
		/*
		 * The USB port uses 12/24/48MHz 2.5V board clock
		 * source at USB_XO. USB_XI should be tied to GND.
		 * Most Octeon evaluation boards require this setting
		 */
		if (OCTEON_IS_MODEL(OCTEON_CN3XXX) ||
		    OCTEON_IS_MODEL(OCTEON_CN56XX) ||
		    OCTEON_IS_MODEL(OCTEON_CN50XX))
			/* From CN56XX,CN50XX,CN31XX,CN30XX manuals */
			usbn_clk_ctl.s.p_rtype = 2; /* p_rclk=1 & p_xenbn=0 */
		else
			/* From CN52XX manual */
			usbn_clk_ctl.s.p_rtype = 1;

		switch (flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK) {
		case CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ:
			usbn_clk_ctl.s.p_c_sel = 0;
			break;
		case CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ:
			usbn_clk_ctl.s.p_c_sel = 1;
			break;
		case CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ:
			usbn_clk_ctl.s.p_c_sel = 2;
			break;
		}
	} else {
		/*
		 * The USB port uses a 12MHz crystal as clock source
		 * at USB_XO and USB_XI
		 */
		if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
			/* From CN31XX,CN30XX manual */
			usbn_clk_ctl.s.p_rtype = 3; /* p_rclk=1 & p_xenbn=1 */
		else
			/* From CN56XX,CN52XX,CN50XX manuals. */
			usbn_clk_ctl.s.p_rtype = 0;

		usbn_clk_ctl.s.p_c_sel = 0;
	}
	/*
	 * 2c. Select the HCLK via writing USBN0/1_CLK_CTL[DIVIDE, DIVIDE2] and
	 *     setting USBN0/1_CLK_CTL[ENABLE] = 1. Divide the core clock down
	 *     such that USB is as close as possible to 125Mhz
	 */
	{
		int divisor = DIV_ROUND_UP(octeon_get_clock_rate(), 125000000);
		/* Lower than 4 doesn't seem to work properly */
		if (divisor < 4)
			divisor = 4;
		usbn_clk_ctl.s.divide = divisor;
		usbn_clk_ctl.s.divide2 = 0;
	}
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	/* 2d. Write USBN0/1_CLK_CTL[HCLK_RST] = 1 */
	usbn_clk_ctl.s.hclk_rst = 1;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	/* 2e.  Wait 64 core-clock cycles for HCLK to stabilize */
	cvmx_wait(64);
	/*
	 * 3. Program the power-on reset field in the USBN clock-control
	 *    register:
	 *    USBN_CLK_CTL[POR] = 0
	 */
	usbn_clk_ctl.s.por = 0;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	/* 4. Wait 1 ms for PHY clock to start */
	mdelay(1);
	/*
	 * 5. Program the Reset input from automatic test equipment field in the
	 *    USBP control and status register:
	 *    USBN_USBP_CTL_STATUS[ATE_RESET] = 1
	 */
	usbn_usbp_ctl_status.u64 = __cvmx_usb_read_csr64(usb,
			CVMX_USBNX_USBP_CTL_STATUS(usb->index));
	usbn_usbp_ctl_status.s.ate_reset = 1;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
			       usbn_usbp_ctl_status.u64);
	/* 6. Wait 10 cycles */
	cvmx_wait(10);
	/*
	 * 7. Clear ATE_RESET field in the USBN clock-control register:
	 *    USBN_USBP_CTL_STATUS[ATE_RESET] = 0
	 */
	usbn_usbp_ctl_status.s.ate_reset = 0;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
			       usbn_usbp_ctl_status.u64);
	/*
	 * 8. Program the PHY reset field in the USBN clock-control register:
	 *    USBN_CLK_CTL[PRST] = 1
	 */
	usbn_clk_ctl.s.prst = 1;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	/*
	 * 9. Program the USBP control and status register to select host or
	 *    device mode. USBN_USBP_CTL_STATUS[HST_MODE] = 0 for host, = 1 for
	 *    device
	 */
	usbn_usbp_ctl_status.s.hst_mode = 0;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
			       usbn_usbp_ctl_status.u64);
	/* 10. Wait 1 us */
	udelay(1);
	/*
	 * 11. Program the hreset_n field in the USBN clock-control register:
	 *     USBN_CLK_CTL[HRST] = 1
	 */
	usbn_clk_ctl.s.hrst = 1;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	/* 12. Proceed to USB core initialization */
	usbn_clk_ctl.s.enable = 1;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	udelay(1);

	/*
	 * USB Core Initialization
	 *
	 * 1. Read USBC_GHWCFG1, USBC_GHWCFG2, USBC_GHWCFG3, USBC_GHWCFG4 to
	 *    determine USB core configuration parameters.
	 *
	 *    Nothing needed
	 *
	 * 2. Program the following fields in the global AHB configuration
	 *    register (USBC_GAHBCFG)
	 *    DMA mode, USBC_GAHBCFG[DMAEn]: 1 = DMA mode, 0 = slave mode
	 *    Burst length, USBC_GAHBCFG[HBSTLEN] = 0
	 *    Nonperiodic TxFIFO empty level (slave mode only),
	 *    USBC_GAHBCFG[NPTXFEMPLVL]
	 *    Periodic TxFIFO empty level (slave mode only),
	 *    USBC_GAHBCFG[PTXFEMPLVL]
	 *    Global interrupt mask, USBC_GAHBCFG[GLBLINTRMSK] = 1
	 */
	{
		union cvmx_usbcx_gahbcfg usbcx_gahbcfg;
		/* Due to an errata, CN31XX doesn't support DMA */
		if (OCTEON_IS_MODEL(OCTEON_CN31XX))
			usb->init_flags |= CVMX_USB_INITIALIZE_FLAGS_NO_DMA;
		usbcx_gahbcfg.u32 = 0;
		usbcx_gahbcfg.s.dmaen = !(usb->init_flags &
					  CVMX_USB_INITIALIZE_FLAGS_NO_DMA);
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			/* Only use one channel with non DMA */
			usb->idle_hardware_channels = 0x1;
		else if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
			/* CN5XXX have an errata with channel 3 */
			usb->idle_hardware_channels = 0xf7;
		else
			usb->idle_hardware_channels = 0xff;
		usbcx_gahbcfg.s.hbstlen = 0;
		usbcx_gahbcfg.s.nptxfemplvl = 1;
		usbcx_gahbcfg.s.ptxfemplvl = 1;
		usbcx_gahbcfg.s.glblintrmsk = 1;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GAHBCFG(usb->index),
				       usbcx_gahbcfg.u32);
	}
	/*
	 * 3. Program the following fields in USBC_GUSBCFG register.
	 *    HS/FS timeout calibration, USBC_GUSBCFG[TOUTCAL] = 0
	 *    ULPI DDR select, USBC_GUSBCFG[DDRSEL] = 0
	 *    USB turnaround time, USBC_GUSBCFG[USBTRDTIM] = 0x5
	 *    PHY low-power clock select, USBC_GUSBCFG[PHYLPWRCLKSEL] = 0
	 */
	{
		union cvmx_usbcx_gusbcfg usbcx_gusbcfg;

		usbcx_gusbcfg.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_GUSBCFG(usb->index));
		usbcx_gusbcfg.s.toutcal = 0;
		usbcx_gusbcfg.s.ddrsel = 0;
		usbcx_gusbcfg.s.usbtrdtim = 0x5;
		usbcx_gusbcfg.s.phylpwrclksel = 0;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index),
				       usbcx_gusbcfg.u32);
	}
	/*
	 * 4. The software must unmask the following bits in the USBC_GINTMSK
	 *    register.
	 *    OTG interrupt mask, USBC_GINTMSK[OTGINTMSK] = 1
	 *    Mode mismatch interrupt mask, USBC_GINTMSK[MODEMISMSK] = 1
	 */
	{
		union cvmx_usbcx_gintmsk usbcx_gintmsk;
		int channel;

		usbcx_gintmsk.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_GINTMSK(usb->index));
		usbcx_gintmsk.s.otgintmsk = 1;
		usbcx_gintmsk.s.modemismsk = 1;
		usbcx_gintmsk.s.hchintmsk = 1;
		usbcx_gintmsk.s.sofmsk = 0;
		/* We need RX FIFO interrupts if we don't have DMA */
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			usbcx_gintmsk.s.rxflvlmsk = 1;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GINTMSK(usb->index),
				       usbcx_gintmsk.u32);

		/*
		 * Disable all channel interrupts. We'll enable them per channel
		 * later.
		 */
		for (channel = 0; channel < 8; channel++)
			__cvmx_usb_write_csr32(usb,
				CVMX_USBCX_HCINTMSKX(channel, usb->index), 0);
	}

	{
		/*
		 * Host Port Initialization
		 *
		 * 1. Program the host-port interrupt-mask field to unmask,
		 *    USBC_GINTMSK[PRTINT] = 1
		 */
		USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
				union cvmx_usbcx_gintmsk, prtintmsk, 1);
		USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
				union cvmx_usbcx_gintmsk, disconnintmsk, 1);
		/*
		 * 2. Program the USBC_HCFG register to select full-speed host
		 *    or high-speed host.
		 */
		{
			union cvmx_usbcx_hcfg usbcx_hcfg;

			usbcx_hcfg.u32 = __cvmx_usb_read_csr32(usb,
					CVMX_USBCX_HCFG(usb->index));
			usbcx_hcfg.s.fslssupp = 0;
			usbcx_hcfg.s.fslspclksel = 0;
			__cvmx_usb_write_csr32(usb,
					CVMX_USBCX_HCFG(usb->index),
					usbcx_hcfg.u32);
		}
		/*
		 * 3. Program the port power bit to drive VBUS on the USB,
		 *    USBC_HPRT[PRTPWR] = 1
		 */
		USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index),
				union cvmx_usbcx_hprt, prtpwr, 1);

		/*
		 * Steps 4-15 from the manual are done later in the port enable
		 */
	}

	return 0;
}


/**
 * Shutdown a USB port after a call to cvmx_usb_initialize().
 * The port should be disabled with all pipes closed when this
 * function is called.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_shutdown(struct cvmx_usb_state *usb)
{
	union cvmx_usbnx_clk_ctl usbn_clk_ctl;

	/* Make sure all pipes are closed */
	if (!list_empty(&usb->idle_pipes) ||
	    !list_empty(&usb->active_pipes[CVMX_USB_TRANSFER_ISOCHRONOUS]) ||
	    !list_empty(&usb->active_pipes[CVMX_USB_TRANSFER_INTERRUPT]) ||
	    !list_empty(&usb->active_pipes[CVMX_USB_TRANSFER_CONTROL]) ||
	    !list_empty(&usb->active_pipes[CVMX_USB_TRANSFER_BULK]))
		return -EBUSY;

	/* Disable the clocks and put them in power on reset */
	usbn_clk_ctl.u64 = __cvmx_usb_read_csr64(usb,
			CVMX_USBNX_CLK_CTL(usb->index));
	usbn_clk_ctl.s.enable = 1;
	usbn_clk_ctl.s.por = 1;
	usbn_clk_ctl.s.hclk_rst = 1;
	usbn_clk_ctl.s.prst = 0;
	usbn_clk_ctl.s.hrst = 0;
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
			       usbn_clk_ctl.u64);
	return 0;
}


/**
 * Enable a USB port. After this call succeeds, the USB port is
 * online and servicing requests.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_enable(struct cvmx_usb_state *usb)
{
	union cvmx_usbcx_ghwcfg3 usbcx_ghwcfg3;

	usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HPRT(usb->index));

	/*
	 * If the port is already enabled the just return. We don't need to do
	 * anything
	 */
	if (usb->usbcx_hprt.s.prtena)
		return 0;

	/* If there is nothing plugged into the port then fail immediately */
	if (!usb->usbcx_hprt.s.prtconnsts)
		return -ETIMEDOUT;

	/* Program the port reset bit to start the reset process */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt,
			prtrst, 1);

	/*
	 * Wait at least 50ms (high speed), or 10ms (full speed) for the reset
	 * process to complete.
	 */
	mdelay(50);

	/* Program the port reset bit to 0, USBC_HPRT[PRTRST] = 0 */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt,
			prtrst, 0);

	/* Wait for the USBC_HPRT[PRTENA]. */
	if (CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_HPRT(usb->index),
				union cvmx_usbcx_hprt, prtena, ==, 1, 100000))
		return -ETIMEDOUT;

	/*
	 * Read the port speed field to get the enumerated speed,
	 * USBC_HPRT[PRTSPD].
	 */
	usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HPRT(usb->index));
	usbcx_ghwcfg3.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_GHWCFG3(usb->index));

	/*
	 * 13. Program the USBC_GRXFSIZ register to select the size of the
	 *     receive FIFO (25%).
	 */
	USB_SET_FIELD32(CVMX_USBCX_GRXFSIZ(usb->index),
			union cvmx_usbcx_grxfsiz, rxfdep,
			usbcx_ghwcfg3.s.dfifodepth / 4);
	/*
	 * 14. Program the USBC_GNPTXFSIZ register to select the size and the
	 *     start address of the non- periodic transmit FIFO for nonperiodic
	 *     transactions (50%).
	 */
	{
		union cvmx_usbcx_gnptxfsiz siz;

		siz.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_GNPTXFSIZ(usb->index));
		siz.s.nptxfdep = usbcx_ghwcfg3.s.dfifodepth / 2;
		siz.s.nptxfstaddr = usbcx_ghwcfg3.s.dfifodepth / 4;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index),
				       siz.u32);
	}
	/*
	 * 15. Program the USBC_HPTXFSIZ register to select the size and start
	 *     address of the periodic transmit FIFO for periodic transactions
	 *     (25%).
	 */
	{
		union cvmx_usbcx_hptxfsiz siz;

		siz.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HPTXFSIZ(usb->index));
		siz.s.ptxfsize = usbcx_ghwcfg3.s.dfifodepth / 4;
		siz.s.ptxfstaddr = 3 * usbcx_ghwcfg3.s.dfifodepth / 4;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HPTXFSIZ(usb->index),
				       siz.u32);
	}
	/* Flush all FIFOs */
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index),
			union cvmx_usbcx_grstctl, txfnum, 0x10);
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index),
			union cvmx_usbcx_grstctl, txfflsh, 1);
	CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index),
			      union cvmx_usbcx_grstctl,
			      txfflsh, ==, 0, 100);
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index),
			union cvmx_usbcx_grstctl, rxfflsh, 1);
	CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index),
			      union cvmx_usbcx_grstctl,
			      rxfflsh, ==, 0, 100);

	return 0;
}


/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 * Transactions in process will fail and call their
 * associated callbacks.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_disable(struct cvmx_usb_state *usb)
{
	/* Disable the port */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt,
			prtena, 1);
	return 0;
}


/**
 * Get the current state of the USB port. Use this call to
 * determine if the usb port has anything connected, is enabled,
 * or has some sort of error condition. The return value of this
 * call has "changed" bits to signal of the value of some fields
 * have changed between calls.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: Port status information
 */
static struct cvmx_usb_port_status cvmx_usb_get_status(
		struct cvmx_usb_state *usb)
{
	union cvmx_usbcx_hprt usbc_hprt;
	struct cvmx_usb_port_status result;

	memset(&result, 0, sizeof(result));

	usbc_hprt.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HPRT(usb->index));
	result.port_enabled = usbc_hprt.s.prtena;
	result.port_over_current = usbc_hprt.s.prtovrcurract;
	result.port_powered = usbc_hprt.s.prtpwr;
	result.port_speed = usbc_hprt.s.prtspd;
	result.connected = usbc_hprt.s.prtconnsts;
	result.connect_change =
		(result.connected != usb->port_status.connected);

	return result;
}

/**
 * Open a virtual pipe between the host and a USB device. A pipe
 * must be opened before data can be transferred between a device
 * and Octeon.
 *
 * @usb:	     USB device state populated by cvmx_usb_initialize().
 * @device_addr:
 *		     USB device address to open the pipe to
 *		     (0-127).
 * @endpoint_num:
 *		     USB endpoint number to open the pipe to
 *		     (0-15).
 * @device_speed:
 *		     The speed of the device the pipe is going
 *		     to. This must match the device's speed,
 *		     which may be different than the port speed.
 * @max_packet:	     The maximum packet length the device can
 *		     transmit/receive (low speed=0-8, full
 *		     speed=0-1023, high speed=0-1024). This value
 *		     comes from the standard endpoint descriptor
 *		     field wMaxPacketSize bits <10:0>.
 * @transfer_type:
 *		     The type of transfer this pipe is for.
 * @transfer_dir:
 *		     The direction the pipe is in. This is not
 *		     used for control pipes.
 * @interval:	     For ISOCHRONOUS and INTERRUPT transfers,
 *		     this is how often the transfer is scheduled
 *		     for. All other transfers should specify
 *		     zero. The units are in frames (8000/sec at
 *		     high speed, 1000/sec for full speed).
 * @multi_count:
 *		     For high speed devices, this is the maximum
 *		     allowed number of packet per microframe.
 *		     Specify zero for non high speed devices. This
 *		     value comes from the standard endpoint descriptor
 *		     field wMaxPacketSize bits <12:11>.
 * @hub_device_addr:
 *		     Hub device address this device is connected
 *		     to. Devices connected directly to Octeon
 *		     use zero. This is only used when the device
 *		     is full/low speed behind a high speed hub.
 *		     The address will be of the high speed hub,
 *		     not and full speed hubs after it.
 * @hub_port:	     Which port on the hub the device is
 *		     connected. Use zero for devices connected
 *		     directly to Octeon. Like hub_device_addr,
 *		     this is only used for full/low speed
 *		     devices behind a high speed hub.
 *
 * Returns: A non-NULL value is a pipe. NULL means an error.
 */
static struct cvmx_usb_pipe *cvmx_usb_open_pipe(struct cvmx_usb_state *usb,
						int device_addr,
						int endpoint_num,
						enum cvmx_usb_speed
							device_speed,
						int max_packet,
						enum cvmx_usb_transfer
							transfer_type,
						enum cvmx_usb_direction
							transfer_dir,
						int interval, int multi_count,
						int hub_device_addr,
						int hub_port)
{
	struct cvmx_usb_pipe *pipe;

	if (unlikely((device_addr < 0) || (device_addr > MAX_USB_ADDRESS)))
		return NULL;
	if (unlikely((endpoint_num < 0) || (endpoint_num > MAX_USB_ENDPOINT)))
		return NULL;
	if (unlikely(device_speed > CVMX_USB_SPEED_LOW))
		return NULL;
	if (unlikely((max_packet <= 0) || (max_packet > 1024)))
		return NULL;
	if (unlikely(transfer_type > CVMX_USB_TRANSFER_INTERRUPT))
		return NULL;
	if (unlikely((transfer_dir != CVMX_USB_DIRECTION_OUT) &&
		(transfer_dir != CVMX_USB_DIRECTION_IN)))
		return NULL;
	if (unlikely(interval < 0))
		return NULL;
	if (unlikely((transfer_type == CVMX_USB_TRANSFER_CONTROL) && interval))
		return NULL;
	if (unlikely(multi_count < 0))
		return NULL;
	if (unlikely((device_speed != CVMX_USB_SPEED_HIGH) &&
		(multi_count != 0)))
		return NULL;
	if (unlikely((hub_device_addr < 0) ||
		(hub_device_addr > MAX_USB_ADDRESS)))
		return NULL;
	if (unlikely((hub_port < 0) || (hub_port > MAX_USB_HUB_PORT)))
		return NULL;

	pipe = kzalloc(sizeof(*pipe), GFP_ATOMIC);
	if (!pipe)
		return NULL;
	if ((device_speed == CVMX_USB_SPEED_HIGH) &&
		(transfer_dir == CVMX_USB_DIRECTION_OUT) &&
		(transfer_type == CVMX_USB_TRANSFER_BULK))
		pipe->flags |= __CVMX_USB_PIPE_FLAGS_NEED_PING;
	pipe->device_addr = device_addr;
	pipe->endpoint_num = endpoint_num;
	pipe->device_speed = device_speed;
	pipe->max_packet = max_packet;
	pipe->transfer_type = transfer_type;
	pipe->transfer_dir = transfer_dir;
	INIT_LIST_HEAD(&pipe->transactions);

	/*
	 * All pipes use interval to rate limit NAK processing. Force an
	 * interval if one wasn't supplied
	 */
	if (!interval)
		interval = 1;
	if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
		pipe->interval = interval*8;
		/* Force start splits to be schedule on uFrame 0 */
		pipe->next_tx_frame = ((usb->frame_number+7)&~7) +
					pipe->interval;
	} else {
		pipe->interval = interval;
		pipe->next_tx_frame = usb->frame_number + pipe->interval;
	}
	pipe->multi_count = multi_count;
	pipe->hub_device_addr = hub_device_addr;
	pipe->hub_port = hub_port;
	pipe->pid_toggle = 0;
	pipe->split_sc_frame = -1;
	list_add_tail(&pipe->node, &usb->idle_pipes);

	/*
	 * We don't need to tell the hardware about this pipe yet since
	 * it doesn't have any submitted requests
	 */

	return pipe;
}


/**
 * Poll the RX FIFOs and remove data as needed. This function is only used
 * in non DMA mode. It is very important that this function be called quickly
 * enough to prevent FIFO overflow.
 *
 * @usb:	USB device state populated by cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_rx_fifo(struct cvmx_usb_state *usb)
{
	union cvmx_usbcx_grxstsph rx_status;
	int channel;
	int bytes;
	uint64_t address;
	uint32_t *ptr;

	rx_status.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_GRXSTSPH(usb->index));
	/* Only read data if IN data is there */
	if (rx_status.s.pktsts != 2)
		return;
	/* Check if no data is available */
	if (!rx_status.s.bcnt)
		return;

	channel = rx_status.s.chnum;
	bytes = rx_status.s.bcnt;
	if (!bytes)
		return;

	/* Get where the DMA engine would have written this data */
	address = __cvmx_usb_read_csr64(usb,
			CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8);

	ptr = cvmx_phys_to_ptr(address);
	__cvmx_usb_write_csr64(usb,
			       CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8,
			       address + bytes);

	/* Loop writing the FIFO data for this packet into memory */
	while (bytes > 0) {
		*ptr++ = __cvmx_usb_read_csr32(usb,
				USB_FIFO_ADDRESS(channel, usb->index));
		bytes -= 4;
	}
	CVMX_SYNCW;
}


/**
 * Fill the TX hardware fifo with data out of the software
 * fifos
 *
 * @usb:	    USB device state populated by cvmx_usb_initialize().
 * @fifo:	    Software fifo to use
 * @available:	    Amount of space in the hardware fifo
 *
 * Returns: Non zero if the hardware fifo was too small and needs
 *	    to be serviced again.
 */
static int __cvmx_usb_fill_tx_hw(struct cvmx_usb_state *usb,
				 struct cvmx_usb_tx_fifo *fifo, int available)
{
	/*
	 * We're done either when there isn't anymore space or the software FIFO
	 * is empty
	 */
	while (available && (fifo->head != fifo->tail)) {
		int i = fifo->tail;
		const uint32_t *ptr = cvmx_phys_to_ptr(fifo->entry[i].address);
		uint64_t csr_address = USB_FIFO_ADDRESS(fifo->entry[i].channel,
							usb->index) ^ 4;
		int words = available;

		/* Limit the amount of data to waht the SW fifo has */
		if (fifo->entry[i].size <= available) {
			words = fifo->entry[i].size;
			fifo->tail++;
			if (fifo->tail > MAX_CHANNELS)
				fifo->tail = 0;
		}

		/* Update the next locations and counts */
		available -= words;
		fifo->entry[i].address += words * 4;
		fifo->entry[i].size -= words;

		/*
		 * Write the HW fifo data. The read every three writes is due
		 * to an errata on CN3XXX chips
		 */
		while (words > 3) {
			cvmx_write64_uint32(csr_address, *ptr++);
			cvmx_write64_uint32(csr_address, *ptr++);
			cvmx_write64_uint32(csr_address, *ptr++);
			cvmx_read64_uint64(
					CVMX_USBNX_DMA0_INB_CHN0(usb->index));
			words -= 3;
		}
		cvmx_write64_uint32(csr_address, *ptr++);
		if (--words) {
			cvmx_write64_uint32(csr_address, *ptr++);
			if (--words)
				cvmx_write64_uint32(csr_address, *ptr++);
		}
		cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
	}
	return fifo->head != fifo->tail;
}


/**
 * Check the hardware FIFOs and fill them as needed
 *
 * @usb:	USB device state populated by cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_tx_fifo(struct cvmx_usb_state *usb)
{
	if (usb->periodic.head != usb->periodic.tail) {
		union cvmx_usbcx_hptxsts tx_status;

		tx_status.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HPTXSTS(usb->index));
		if (__cvmx_usb_fill_tx_hw(usb, &usb->periodic,
					  tx_status.s.ptxfspcavail))
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
					union cvmx_usbcx_gintmsk,
					ptxfempmsk, 1);
		else
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
					union cvmx_usbcx_gintmsk,
					ptxfempmsk, 0);
	}

	if (usb->nonperiodic.head != usb->nonperiodic.tail) {
		union cvmx_usbcx_gnptxsts tx_status;

		tx_status.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_GNPTXSTS(usb->index));
		if (__cvmx_usb_fill_tx_hw(usb, &usb->nonperiodic,
					  tx_status.s.nptxfspcavail))
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
					union cvmx_usbcx_gintmsk,
					nptxfempmsk, 1);
		else
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
					union cvmx_usbcx_gintmsk,
					nptxfempmsk, 0);
	}
}


/**
 * Fill the TX FIFO with an outgoing packet
 *
 * @usb:	  USB device state populated by cvmx_usb_initialize().
 * @channel:	  Channel number to get packet from
 */
static void __cvmx_usb_fill_tx_fifo(struct cvmx_usb_state *usb, int channel)
{
	union cvmx_usbcx_hccharx hcchar;
	union cvmx_usbcx_hcspltx usbc_hcsplt;
	union cvmx_usbcx_hctsizx usbc_hctsiz;
	struct cvmx_usb_tx_fifo *fifo;

	/* We only need to fill data on outbound channels */
	hcchar.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCCHARX(channel, usb->index));
	if (hcchar.s.epdir != CVMX_USB_DIRECTION_OUT)
		return;

	/* OUT Splits only have data on the start and not the complete */
	usbc_hcsplt.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCSPLTX(channel, usb->index));
	if (usbc_hcsplt.s.spltena && usbc_hcsplt.s.compsplt)
		return;

	/*
	 * Find out how many bytes we need to fill and convert it into 32bit
	 * words.
	 */
	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCTSIZX(channel, usb->index));
	if (!usbc_hctsiz.s.xfersize)
		return;

	if ((hcchar.s.eptype == CVMX_USB_TRANSFER_INTERRUPT) ||
		(hcchar.s.eptype == CVMX_USB_TRANSFER_ISOCHRONOUS))
		fifo = &usb->periodic;
	else
		fifo = &usb->nonperiodic;

	fifo->entry[fifo->head].channel = channel;
	fifo->entry[fifo->head].address = __cvmx_usb_read_csr64(usb,
			CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8);
	fifo->entry[fifo->head].size = (usbc_hctsiz.s.xfersize+3)>>2;
	fifo->head++;
	if (fifo->head > MAX_CHANNELS)
		fifo->head = 0;

	__cvmx_usb_poll_tx_fifo(usb);
}

/**
 * Perform channel specific setup for Control transactions. All
 * the generic stuff will already have been done in
 * __cvmx_usb_start_channel()
 *
 * @usb:	  USB device state populated by cvmx_usb_initialize().
 * @channel:	  Channel to setup
 * @pipe:	  Pipe for control transaction
 */
static void __cvmx_usb_start_channel_control(struct cvmx_usb_state *usb,
					     int channel,
					     struct cvmx_usb_pipe *pipe)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);
	struct usb_hcd *hcd = octeon_to_hcd(priv);
	struct device *dev = hcd->self.controller;
	struct cvmx_usb_transaction *transaction =
		list_first_entry(&pipe->transactions, typeof(*transaction),
				 node);
	struct usb_ctrlrequest *header =
		cvmx_phys_to_ptr(transaction->control_header);
	int bytes_to_transfer = transaction->buffer_length -
		transaction->actual_bytes;
	int packets_to_transfer;
	union cvmx_usbcx_hctsizx usbc_hctsiz;

	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCTSIZX(channel, usb->index));

	switch (transaction->stage) {
	case CVMX_USB_STAGE_NON_CONTROL:
	case CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE:
		dev_err(dev, "%s: ERROR - Non control stage\n", __func__);
		break;
	case CVMX_USB_STAGE_SETUP:
		usbc_hctsiz.s.pid = 3; /* Setup */
		bytes_to_transfer = sizeof(*header);
		/* All Control operations start with a setup going OUT */
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				CVMX_USB_DIRECTION_OUT);
		/*
		 * Setup send the control header instead of the buffer data. The
		 * buffer data will be used in the next stage
		 */
		__cvmx_usb_write_csr64(usb,
			CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8,
			transaction->control_header);
		break;
	case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = 3; /* Setup */
		bytes_to_transfer = 0;
		/* All Control operations start with a setup going OUT */
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				CVMX_USB_DIRECTION_OUT);

		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index),
				union cvmx_usbcx_hcspltx, compsplt, 1);
		break;
	case CVMX_USB_STAGE_DATA:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
			if (header->bRequestType & USB_DIR_IN)
				bytes_to_transfer = 0;
			else if (bytes_to_transfer > pipe->max_packet)
				bytes_to_transfer = pipe->max_packet;
		}
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->bRequestType & USB_DIR_IN) ?
					CVMX_USB_DIRECTION_IN :
					CVMX_USB_DIRECTION_OUT));
		break;
	case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		if (!(header->bRequestType & USB_DIR_IN))
			bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->bRequestType & USB_DIR_IN) ?
					CVMX_USB_DIRECTION_IN :
					CVMX_USB_DIRECTION_OUT));
		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index),
				union cvmx_usbcx_hcspltx, compsplt, 1);
		break;
	case CVMX_USB_STAGE_STATUS:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->bRequestType & USB_DIR_IN) ?
					CVMX_USB_DIRECTION_OUT :
					CVMX_USB_DIRECTION_IN));
		break;
	case CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->bRequestType & USB_DIR_IN) ?
					CVMX_USB_DIRECTION_OUT :
					CVMX_USB_DIRECTION_IN));
		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index),
				union cvmx_usbcx_hcspltx, compsplt, 1);
		break;
	}

	/*
	 * Make sure the transfer never exceeds the byte limit of the hardware.
	 * Further bytes will be sent as continued transactions
	 */
	if (bytes_to_transfer > MAX_TRANSFER_BYTES) {
		/* Round MAX_TRANSFER_BYTES to a multiple of out packet size */
		bytes_to_transfer = MAX_TRANSFER_BYTES / pipe->max_packet;
		bytes_to_transfer *= pipe->max_packet;
	}

	/*
	 * Calculate the number of packets to transfer. If the length is zero
	 * we still need to transfer one packet
	 */
	packets_to_transfer = DIV_ROUND_UP(bytes_to_transfer,
					   pipe->max_packet);
	if (packets_to_transfer == 0)
		packets_to_transfer = 1;
	else if ((packets_to_transfer > 1) &&
			(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)) {
		/*
		 * Limit to one packet when not using DMA. Channels must be
		 * restarted between every packet for IN transactions, so there
		 * is no reason to do multiple packets in a row
		 */
		packets_to_transfer = 1;
		bytes_to_transfer = packets_to_transfer * pipe->max_packet;
	} else if (packets_to_transfer > MAX_TRANSFER_PACKETS) {
		/*
		 * Limit the number of packet and data transferred to what the
		 * hardware can handle
		 */
		packets_to_transfer = MAX_TRANSFER_PACKETS;
		bytes_to_transfer = packets_to_transfer * pipe->max_packet;
	}

	usbc_hctsiz.s.xfersize = bytes_to_transfer;
	usbc_hctsiz.s.pktcnt = packets_to_transfer;

	__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index),
			       usbc_hctsiz.u32);
}


/**
 * Start a channel to perform the pipe's head transaction
 *
 * @usb:	  USB device state populated by cvmx_usb_initialize().
 * @channel:	  Channel to setup
 * @pipe:	  Pipe to start
 */
static void __cvmx_usb_start_channel(struct cvmx_usb_state *usb,
				     int channel,
				     struct cvmx_usb_pipe *pipe)
{
	struct cvmx_usb_transaction *transaction =
		list_first_entry(&pipe->transactions, typeof(*transaction),
				 node);

	/* Make sure all writes to the DMA region get flushed */
	CVMX_SYNCW;

	/* Attach the channel to the pipe */
	usb->pipe_for_channel[channel] = pipe;
	pipe->channel = channel;
	pipe->flags |= __CVMX_USB_PIPE_FLAGS_SCHEDULED;

	/* Mark this channel as in use */
	usb->idle_hardware_channels &= ~(1<<channel);

	/* Enable the channel interrupt bits */
	{
		union cvmx_usbcx_hcintx usbc_hcint;
		union cvmx_usbcx_hcintmskx usbc_hcintmsk;
		union cvmx_usbcx_haintmsk usbc_haintmsk;

		/* Clear all channel status bits */
		usbc_hcint.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HCINTX(channel, usb->index));

		__cvmx_usb_write_csr32(usb,
				       CVMX_USBCX_HCINTX(channel, usb->index),
				       usbc_hcint.u32);

		usbc_hcintmsk.u32 = 0;
		usbc_hcintmsk.s.chhltdmsk = 1;
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
			/*
			 * Channels need these extra interrupts when we aren't
			 * in DMA mode.
			 */
			usbc_hcintmsk.s.datatglerrmsk = 1;
			usbc_hcintmsk.s.frmovrunmsk = 1;
			usbc_hcintmsk.s.bblerrmsk = 1;
			usbc_hcintmsk.s.xacterrmsk = 1;
			if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
				/*
				 * Splits don't generate xfercompl, so we need
				 * ACK and NYET.
				 */
				usbc_hcintmsk.s.nyetmsk = 1;
				usbc_hcintmsk.s.ackmsk = 1;
			}
			usbc_hcintmsk.s.nakmsk = 1;
			usbc_hcintmsk.s.stallmsk = 1;
			usbc_hcintmsk.s.xfercomplmsk = 1;
		}
		__cvmx_usb_write_csr32(usb,
				CVMX_USBCX_HCINTMSKX(channel, usb->index),
				usbc_hcintmsk.u32);

		/* Enable the channel interrupt to propagate */
		usbc_haintmsk.u32 = __cvmx_usb_read_csr32(usb,
					CVMX_USBCX_HAINTMSK(usb->index));
		usbc_haintmsk.s.haintmsk |= 1<<channel;
		__cvmx_usb_write_csr32(usb,
					CVMX_USBCX_HAINTMSK(usb->index),
					usbc_haintmsk.u32);
	}

	/* Setup the locations the DMA engines use  */
	{
		uint64_t dma_address = transaction->buffer +
					transaction->actual_bytes;

		if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
			dma_address = transaction->buffer +
					transaction->iso_packets[0].offset +
					transaction->actual_bytes;

		__cvmx_usb_write_csr64(usb,
			CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8,
			dma_address);

		__cvmx_usb_write_csr64(usb,
			CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8,
			dma_address);
	}

	/* Setup both the size of the transfer and the SPLIT characteristics */
	{
		union cvmx_usbcx_hcspltx usbc_hcsplt = {.u32 = 0};
		union cvmx_usbcx_hctsizx usbc_hctsiz = {.u32 = 0};
		int packets_to_transfer;
		int bytes_to_transfer = transaction->buffer_length -
			transaction->actual_bytes;

		/*
		 * ISOCHRONOUS transactions store each individual transfer size
		 * in the packet structure, not the global buffer_length
		 */
		if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
			bytes_to_transfer =
				transaction->iso_packets[0].length -
				transaction->actual_bytes;

		/*
		 * We need to do split transactions when we are talking to non
		 * high speed devices that are behind a high speed hub
		 */
		if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
			/*
			 * On the start split phase (stage is even) record the
			 * frame number we will need to send the split complete.
			 * We only store the lower two bits since the time ahead
			 * can only be two frames
			 */
			if ((transaction->stage&1) == 0) {
				if (transaction->type == CVMX_USB_TRANSFER_BULK)
					pipe->split_sc_frame =
						(usb->frame_number + 1) & 0x7f;
				else
					pipe->split_sc_frame =
						(usb->frame_number + 2) & 0x7f;
			} else
				pipe->split_sc_frame = -1;

			usbc_hcsplt.s.spltena = 1;
			usbc_hcsplt.s.hubaddr = pipe->hub_device_addr;
			usbc_hcsplt.s.prtaddr = pipe->hub_port;
			usbc_hcsplt.s.compsplt = (transaction->stage ==
				CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE);

			/*
			 * SPLIT transactions can only ever transmit one data
			 * packet so limit the transfer size to the max packet
			 * size
			 */
			if (bytes_to_transfer > pipe->max_packet)
				bytes_to_transfer = pipe->max_packet;

			/*
			 * ISOCHRONOUS OUT splits are unique in that they limit
			 * data transfers to 188 byte chunks representing the
			 * begin/middle/end of the data or all
			 */
			if (!usbc_hcsplt.s.compsplt &&
				(pipe->transfer_dir ==
				 CVMX_USB_DIRECTION_OUT) &&
				(pipe->transfer_type ==
				 CVMX_USB_TRANSFER_ISOCHRONOUS)) {
				/*
				 * Clear the split complete frame number as
				 * there isn't going to be a split complete
				 */
				pipe->split_sc_frame = -1;
				/*
				 * See if we've started this transfer and sent
				 * data
				 */
				if (transaction->actual_bytes == 0) {
					/*
					 * Nothing sent yet, this is either a
					 * begin or the entire payload
					 */
					if (bytes_to_transfer <= 188)
						/* Entire payload in one go */
						usbc_hcsplt.s.xactpos = 3;
					else
						/* First part of payload */
						usbc_hcsplt.s.xactpos = 2;
				} else {
					/*
					 * Continuing the previous data, we must
					 * either be in the middle or at the end
					 */
					if (bytes_to_transfer <= 188)
						/* End of payload */
						usbc_hcsplt.s.xactpos = 1;
					else
						/* Middle of payload */
						usbc_hcsplt.s.xactpos = 0;
				}
				/*
				 * Again, the transfer size is limited to 188
				 * bytes
				 */
				if (bytes_to_transfer > 188)
					bytes_to_transfer = 188;
			}
		}

		/*
		 * Make sure the transfer never exceeds the byte limit of the
		 * hardware. Further bytes will be sent as continued
		 * transactions
		 */
		if (bytes_to_transfer > MAX_TRANSFER_BYTES) {
			/*
			 * Round MAX_TRANSFER_BYTES to a multiple of out packet
			 * size
			 */
			bytes_to_transfer = MAX_TRANSFER_BYTES /
				pipe->max_packet;
			bytes_to_transfer *= pipe->max_packet;
		}

		/*
		 * Calculate the number of packets to transfer. If the length is
		 * zero we still need to transfer one packet
		 */
		packets_to_transfer =
			DIV_ROUND_UP(bytes_to_transfer, pipe->max_packet);
		if (packets_to_transfer == 0)
			packets_to_transfer = 1;
		else if ((packets_to_transfer > 1) &&
				(usb->init_flags &
				 CVMX_USB_INITIALIZE_FLAGS_NO_DMA)) {
			/*
			 * Limit to one packet when not using DMA. Channels must
			 * be restarted between every packet for IN
			 * transactions, so there is no reason to do multiple
			 * packets in a row
			 */
			packets_to_transfer = 1;
			bytes_to_transfer = packets_to_transfer *
				pipe->max_packet;
		} else if (packets_to_transfer > MAX_TRANSFER_PACKETS) {
			/*
			 * Limit the number of packet and data transferred to
			 * what the hardware can handle
			 */
			packets_to_transfer = MAX_TRANSFER_PACKETS;
			bytes_to_transfer = packets_to_transfer *
				pipe->max_packet;
		}

		usbc_hctsiz.s.xfersize = bytes_to_transfer;
		usbc_hctsiz.s.pktcnt = packets_to_transfer;

		/* Update the DATA0/DATA1 toggle */
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		/*
		 * High speed pipes may need a hardware ping before they start
		 */
		if (pipe->flags & __CVMX_USB_PIPE_FLAGS_NEED_PING)
			usbc_hctsiz.s.dopng = 1;

		__cvmx_usb_write_csr32(usb,
				       CVMX_USBCX_HCSPLTX(channel, usb->index),
				       usbc_hcsplt.u32);
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel,
					usb->index), usbc_hctsiz.u32);
	}

	/* Setup the Host Channel Characteristics Register */
	{
		union cvmx_usbcx_hccharx usbc_hcchar = {.u32 = 0};

		/*
		 * Set the startframe odd/even properly. This is only used for
		 * periodic
		 */
		usbc_hcchar.s.oddfrm = usb->frame_number&1;

		/*
		 * Set the number of back to back packets allowed by this
		 * endpoint. Split transactions interpret "ec" as the number of
		 * immediate retries of failure. These retries happen too
		 * quickly, so we disable these entirely for splits
		 */
		if (__cvmx_usb_pipe_needs_split(usb, pipe))
			usbc_hcchar.s.ec = 1;
		else if (pipe->multi_count < 1)
			usbc_hcchar.s.ec = 1;
		else if (pipe->multi_count > 3)
			usbc_hcchar.s.ec = 3;
		else
			usbc_hcchar.s.ec = pipe->multi_count;

		/* Set the rest of the endpoint specific settings */
		usbc_hcchar.s.devaddr = pipe->device_addr;
		usbc_hcchar.s.eptype = transaction->type;
		usbc_hcchar.s.lspddev =
			(pipe->device_speed == CVMX_USB_SPEED_LOW);
		usbc_hcchar.s.epdir = pipe->transfer_dir;
		usbc_hcchar.s.epnum = pipe->endpoint_num;
		usbc_hcchar.s.mps = pipe->max_packet;
		__cvmx_usb_write_csr32(usb,
				       CVMX_USBCX_HCCHARX(channel, usb->index),
				       usbc_hcchar.u32);
	}

	/* Do transaction type specific fixups as needed */
	switch (transaction->type) {
	case CVMX_USB_TRANSFER_CONTROL:
		__cvmx_usb_start_channel_control(usb, channel, pipe);
		break;
	case CVMX_USB_TRANSFER_BULK:
	case CVMX_USB_TRANSFER_INTERRUPT:
		break;
	case CVMX_USB_TRANSFER_ISOCHRONOUS:
		if (!__cvmx_usb_pipe_needs_split(usb, pipe)) {
			/*
			 * ISO transactions require different PIDs depending on
			 * direction and how many packets are needed
			 */
			if (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) {
				if (pipe->multi_count < 2) /* Need DATA0 */
					USB_SET_FIELD32(
						CVMX_USBCX_HCTSIZX(channel,
								   usb->index),
						union cvmx_usbcx_hctsizx,
						pid, 0);
				else /* Need MDATA */
					USB_SET_FIELD32(
						CVMX_USBCX_HCTSIZX(channel,
								   usb->index),
						union cvmx_usbcx_hctsizx,
						pid, 3);
			}
		}
		break;
	}
	{
		union cvmx_usbcx_hctsizx usbc_hctsiz = {.u32 =
			__cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HCTSIZX(channel, usb->index))};
		transaction->xfersize = usbc_hctsiz.s.xfersize;
		transaction->pktcnt = usbc_hctsiz.s.pktcnt;
	}
	/* Remeber when we start a split transaction */
	if (__cvmx_usb_pipe_needs_split(usb, pipe))
		usb->active_split = transaction;
	USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
			union cvmx_usbcx_hccharx, chena, 1);
	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
		__cvmx_usb_fill_tx_fifo(usb, channel);
}


/**
 * Find a pipe that is ready to be scheduled to hardware.
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @list:	 Pipe list to search
 * @current_frame:
 *		 Frame counter to use as a time reference.
 *
 * Returns: Pipe or NULL if none are ready
 */
static struct cvmx_usb_pipe *__cvmx_usb_find_ready_pipe(
		struct cvmx_usb_state *usb,
		struct list_head *list,
		uint64_t current_frame)
{
	struct cvmx_usb_pipe *pipe;

	list_for_each_entry(pipe, list, node) {
		struct cvmx_usb_transaction *t =
			list_first_entry(&pipe->transactions, typeof(*t),
					 node);
		if (!(pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED) && t &&
			(pipe->next_tx_frame <= current_frame) &&
			((pipe->split_sc_frame == -1) ||
			 ((((int)current_frame - (int)pipe->split_sc_frame)
			   & 0x7f) < 0x40)) &&
			(!usb->active_split || (usb->active_split == t))) {
			prefetch(t);
			return pipe;
		}
	}
	return NULL;
}


/**
 * Called whenever a pipe might need to be scheduled to the
 * hardware.
 *
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @is_sof:	 True if this schedule was called on a SOF interrupt.
 */
static void __cvmx_usb_schedule(struct cvmx_usb_state *usb, int is_sof)
{
	int channel;
	struct cvmx_usb_pipe *pipe;
	int need_sof;
	enum cvmx_usb_transfer ttype;

	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
		/*
		 * Without DMA we need to be careful to not schedule something
		 * at the end of a frame and cause an overrun.
		 */
		union cvmx_usbcx_hfnum hfnum = {
			.u32 = __cvmx_usb_read_csr32(usb,
						CVMX_USBCX_HFNUM(usb->index))
		};

		union cvmx_usbcx_hfir hfir = {
			.u32 = __cvmx_usb_read_csr32(usb,
						CVMX_USBCX_HFIR(usb->index))
		};

		if (hfnum.s.frrem < hfir.s.frint/4)
			goto done;
	}

	while (usb->idle_hardware_channels) {
		/* Find an idle channel */
		channel = __fls(usb->idle_hardware_channels);
		if (unlikely(channel > 7))
			break;

		/* Find a pipe needing service */
		pipe = NULL;
		if (is_sof) {
			/*
			 * Only process periodic pipes on SOF interrupts. This
			 * way we are sure that the periodic data is sent in the
			 * beginning of the frame
			 */
			pipe = __cvmx_usb_find_ready_pipe(usb,
					usb->active_pipes +
					CVMX_USB_TRANSFER_ISOCHRONOUS,
					usb->frame_number);
			if (likely(!pipe))
				pipe = __cvmx_usb_find_ready_pipe(usb,
						usb->active_pipes +
						CVMX_USB_TRANSFER_INTERRUPT,
						usb->frame_number);
		}
		if (likely(!pipe)) {
			pipe = __cvmx_usb_find_ready_pipe(usb,
					usb->active_pipes +
					CVMX_USB_TRANSFER_CONTROL,
					usb->frame_number);
			if (likely(!pipe))
				pipe = __cvmx_usb_find_ready_pipe(usb,
						usb->active_pipes +
						CVMX_USB_TRANSFER_BULK,
						usb->frame_number);
		}
		if (!pipe)
			break;

		__cvmx_usb_start_channel(usb, channel, pipe);
	}

done:
	/*
	 * Only enable SOF interrupts when we have transactions pending in the
	 * future that might need to be scheduled
	 */
	need_sof = 0;
	for (ttype = CVMX_USB_TRANSFER_CONTROL;
			ttype <= CVMX_USB_TRANSFER_INTERRUPT; ttype++) {
		list_for_each_entry(pipe, &usb->active_pipes[ttype], node) {
			if (pipe->next_tx_frame > usb->frame_number) {
				need_sof = 1;
				break;
			}
		}
	}
	USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index),
			union cvmx_usbcx_gintmsk, sofmsk, need_sof);
}

static void octeon_usb_urb_complete_callback(struct cvmx_usb_state *usb,
					     enum cvmx_usb_complete status,
					     struct cvmx_usb_pipe *pipe,
					     struct cvmx_usb_transaction
						*transaction,
					     int bytes_transferred,
					     struct urb *urb)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);
	struct usb_hcd *hcd = octeon_to_hcd(priv);
	struct device *dev = hcd->self.controller;

	if (likely(status == CVMX_USB_COMPLETE_SUCCESS))
		urb->actual_length = bytes_transferred;
	else
		urb->actual_length = 0;

	urb->hcpriv = NULL;

	/* For Isochronous transactions we need to update the URB packet status
	   list from data in our private copy */
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;
		/*
		 * The pointer to the private list is stored in the setup_packet
		 * field.
		 */
		struct cvmx_usb_iso_packet *iso_packet =
			(struct cvmx_usb_iso_packet *) urb->setup_packet;
		/* Recalculate the transfer size by adding up each packet */
		urb->actual_length = 0;
		for (i = 0; i < urb->number_of_packets; i++) {
			if (iso_packet[i].status ==
					CVMX_USB_COMPLETE_SUCCESS) {
				urb->iso_frame_desc[i].status = 0;
				urb->iso_frame_desc[i].actual_length =
					iso_packet[i].length;
				urb->actual_length +=
					urb->iso_frame_desc[i].actual_length;
			} else {
				dev_dbg(dev, "ISOCHRONOUS packet=%d of %d status=%d pipe=%p transaction=%p size=%d\n",
					i, urb->number_of_packets,
					iso_packet[i].status, pipe,
					transaction, iso_packet[i].length);
				urb->iso_frame_desc[i].status = -EREMOTEIO;
			}
		}
		/* Free the private list now that we don't need it anymore */
		kfree(iso_packet);
		urb->setup_packet = NULL;
	}

	switch (status) {
	case CVMX_USB_COMPLETE_SUCCESS:
		urb->status = 0;
		break;
	case CVMX_USB_COMPLETE_CANCEL:
		if (urb->status == 0)
			urb->status = -ENOENT;
		break;
	case CVMX_USB_COMPLETE_STALL:
		dev_dbg(dev, "status=stall pipe=%p transaction=%p size=%d\n",
			pipe, transaction, bytes_transferred);
		urb->status = -EPIPE;
		break;
	case CVMX_USB_COMPLETE_BABBLEERR:
		dev_dbg(dev, "status=babble pipe=%p transaction=%p size=%d\n",
			pipe, transaction, bytes_transferred);
		urb->status = -EPIPE;
		break;
	case CVMX_USB_COMPLETE_SHORT:
		dev_dbg(dev, "status=short pipe=%p transaction=%p size=%d\n",
			pipe, transaction, bytes_transferred);
		urb->status = -EREMOTEIO;
		break;
	case CVMX_USB_COMPLETE_ERROR:
	case CVMX_USB_COMPLETE_XACTERR:
	case CVMX_USB_COMPLETE_DATATGLERR:
	case CVMX_USB_COMPLETE_FRAMEERR:
		dev_dbg(dev, "status=%d pipe=%p transaction=%p size=%d\n",
			status, pipe, transaction, bytes_transferred);
		urb->status = -EPROTO;
		break;
	}
	usb_hcd_unlink_urb_from_ep(octeon_to_hcd(priv), urb);
	spin_unlock(&priv->lock);
	usb_hcd_giveback_urb(octeon_to_hcd(priv), urb, urb->status);
	spin_lock(&priv->lock);
}

/**
 * Signal the completion of a transaction and free it. The
 * transaction will be removed from the pipe transaction list.
 *
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @pipe:	 Pipe the transaction is on
 * @transaction:
 *		 Transaction that completed
 * @complete_code:
 *		 Completion code
 */
static void __cvmx_usb_perform_complete(
				struct cvmx_usb_state *usb,
				struct cvmx_usb_pipe *pipe,
				struct cvmx_usb_transaction *transaction,
				enum cvmx_usb_complete complete_code)
{
	/* If this was a split then clear our split in progress marker */
	if (usb->active_split == transaction)
		usb->active_split = NULL;

	/*
	 * Isochronous transactions need extra processing as they might not be
	 * done after a single data transfer
	 */
	if (unlikely(transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)) {
		/* Update the number of bytes transferred in this ISO packet */
		transaction->iso_packets[0].length = transaction->actual_bytes;
		transaction->iso_packets[0].status = complete_code;

		/*
		 * If there are more ISOs pending and we succeeded, schedule the
		 * next one
		 */
		if ((transaction->iso_number_packets > 1) &&
			(complete_code == CVMX_USB_COMPLETE_SUCCESS)) {
			/* No bytes transferred for this packet as of yet */
			transaction->actual_bytes = 0;
			/* One less ISO waiting to transfer */
			transaction->iso_number_packets--;
			/* Increment to the next location in our packet array */
			transaction->iso_packets++;
			transaction->stage = CVMX_USB_STAGE_NON_CONTROL;
			return;
		}
	}

	/* Remove the transaction from the pipe list */
	list_del(&transaction->node);
	if (list_empty(&pipe->transactions))
		list_move_tail(&pipe->node, &usb->idle_pipes);
	octeon_usb_urb_complete_callback(usb, complete_code, pipe,
					 transaction,
					 transaction->actual_bytes,
					 transaction->urb);
	kfree(transaction);
}


/**
 * Submit a usb transaction to a pipe. Called for all types
 * of transactions.
 *
 * @usb:
 * @pipe:	    Which pipe to submit to.
 * @type:	    Transaction type
 * @buffer:	    User buffer for the transaction
 * @buffer_length:
 *		    User buffer's length in bytes
 * @control_header:
 *		    For control transactions, the 8 byte standard header
 * @iso_start_frame:
 *		    For ISO transactions, the start frame
 * @iso_number_packets:
 *		    For ISO, the number of packet in the transaction.
 * @iso_packets:
 *		    A description of each ISO packet
 * @urb:	    URB for the callback
 *
 * Returns: Transaction or NULL on failure.
 */
static struct cvmx_usb_transaction *__cvmx_usb_submit_transaction(
				struct cvmx_usb_state *usb,
				struct cvmx_usb_pipe *pipe,
				enum cvmx_usb_transfer type,
				uint64_t buffer,
				int buffer_length,
				uint64_t control_header,
				int iso_start_frame,
				int iso_number_packets,
				struct cvmx_usb_iso_packet *iso_packets,
				struct urb *urb)
{
	struct cvmx_usb_transaction *transaction;

	if (unlikely(pipe->transfer_type != type))
		return NULL;

	transaction = kzalloc(sizeof(*transaction), GFP_ATOMIC);
	if (unlikely(!transaction))
		return NULL;

	transaction->type = type;
	transaction->buffer = buffer;
	transaction->buffer_length = buffer_length;
	transaction->control_header = control_header;
	/* FIXME: This is not used, implement it. */
	transaction->iso_start_frame = iso_start_frame;
	transaction->iso_number_packets = iso_number_packets;
	transaction->iso_packets = iso_packets;
	transaction->urb = urb;
	if (transaction->type == CVMX_USB_TRANSFER_CONTROL)
		transaction->stage = CVMX_USB_STAGE_SETUP;
	else
		transaction->stage = CVMX_USB_STAGE_NON_CONTROL;

	if (!list_empty(&pipe->transactions)) {
		list_add_tail(&transaction->node, &pipe->transactions);
	} else {
		list_add_tail(&transaction->node, &pipe->transactions);
		list_move_tail(&pipe->node,
			       &usb->active_pipes[pipe->transfer_type]);

		/*
		 * We may need to schedule the pipe if this was the head of the
		 * pipe.
		 */
		__cvmx_usb_schedule(usb, 0);
	}

	return transaction;
}


/**
 * Call to submit a USB Bulk transfer to a pipe.
 *
 * @usb:	    USB device state populated by cvmx_usb_initialize().
 * @pipe:	    Handle to the pipe for the transfer.
 * @urb:	    URB.
 *
 * Returns: A submitted transaction or NULL on failure.
 */
static struct cvmx_usb_transaction *cvmx_usb_submit_bulk(
						struct cvmx_usb_state *usb,
						struct cvmx_usb_pipe *pipe,
						struct urb *urb)
{
	return __cvmx_usb_submit_transaction(usb, pipe, CVMX_USB_TRANSFER_BULK,
					     urb->transfer_dma,
					     urb->transfer_buffer_length,
					     0, /* control_header */
					     0, /* iso_start_frame */
					     0, /* iso_number_packets */
					     NULL, /* iso_packets */
					     urb);
}


/**
 * Call to submit a USB Interrupt transfer to a pipe.
 *
 * @usb:	    USB device state populated by cvmx_usb_initialize().
 * @pipe:	    Handle to the pipe for the transfer.
 * @urb:	    URB returned when the callback is called.
 *
 * Returns: A submitted transaction or NULL on failure.
 */
static struct cvmx_usb_transaction *cvmx_usb_submit_interrupt(
						struct cvmx_usb_state *usb,
						struct cvmx_usb_pipe *pipe,
						struct urb *urb)
{
	return __cvmx_usb_submit_transaction(usb, pipe,
					     CVMX_USB_TRANSFER_INTERRUPT,
					     urb->transfer_dma,
					     urb->transfer_buffer_length,
					     0, /* control_header */
					     0, /* iso_start_frame */
					     0, /* iso_number_packets */
					     NULL, /* iso_packets */
					     urb);
}


/**
 * Call to submit a USB Control transfer to a pipe.
 *
 * @usb:	    USB device state populated by cvmx_usb_initialize().
 * @pipe:	    Handle to the pipe for the transfer.
 * @urb:	    URB.
 *
 * Returns: A submitted transaction or NULL on failure.
 */
static struct cvmx_usb_transaction *cvmx_usb_submit_control(
						struct cvmx_usb_state *usb,
						struct cvmx_usb_pipe *pipe,
						struct urb *urb)
{
	int buffer_length = urb->transfer_buffer_length;
	uint64_t control_header = urb->setup_dma;
	struct usb_ctrlrequest *header = cvmx_phys_to_ptr(control_header);

	if ((header->bRequestType & USB_DIR_IN) == 0)
		buffer_length = le16_to_cpu(header->wLength);

	return __cvmx_usb_submit_transaction(usb, pipe,
					     CVMX_USB_TRANSFER_CONTROL,
					     urb->transfer_dma, buffer_length,
					     control_header,
					     0, /* iso_start_frame */
					     0, /* iso_number_packets */
					     NULL, /* iso_packets */
					     urb);
}


/**
 * Call to submit a USB Isochronous transfer to a pipe.
 *
 * @usb:	    USB device state populated by cvmx_usb_initialize().
 * @pipe:	    Handle to the pipe for the transfer.
 * @urb:	    URB returned when the callback is called.
 *
 * Returns: A submitted transaction or NULL on failure.
 */
static struct cvmx_usb_transaction *cvmx_usb_submit_isochronous(
						struct cvmx_usb_state *usb,
						struct cvmx_usb_pipe *pipe,
						struct urb *urb)
{
	struct cvmx_usb_iso_packet *packets;

	packets = (struct cvmx_usb_iso_packet *) urb->setup_packet;
	return __cvmx_usb_submit_transaction(usb, pipe,
					     CVMX_USB_TRANSFER_ISOCHRONOUS,
					     urb->transfer_dma,
					     urb->transfer_buffer_length,
					     0, /* control_header */
					     urb->start_frame,
					     urb->number_of_packets,
					     packets, urb);
}


/**
 * Cancel one outstanding request in a pipe. Canceling a request
 * can fail if the transaction has already completed before cancel
 * is called. Even after a successful cancel call, it may take
 * a frame or two for the cvmx_usb_poll() function to call the
 * associated callback.
 *
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @pipe:	 Pipe to cancel requests in.
 * @transaction: Transaction to cancel, returned by the submit function.
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_cancel(struct cvmx_usb_state *usb,
			   struct cvmx_usb_pipe *pipe,
			   struct cvmx_usb_transaction *transaction)
{
	/*
	 * If the transaction is the HEAD of the queue and scheduled. We need to
	 * treat it special
	 */
	if (list_first_entry(&pipe->transactions, typeof(*transaction), node) ==
	    transaction && (pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED)) {
		union cvmx_usbcx_hccharx usbc_hcchar;

		usb->pipe_for_channel[pipe->channel] = NULL;
		pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

		CVMX_SYNCW;

		usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HCCHARX(pipe->channel, usb->index));
		/*
		 * If the channel isn't enabled then the transaction already
		 * completed.
		 */
		if (usbc_hcchar.s.chena) {
			usbc_hcchar.s.chdis = 1;
			__cvmx_usb_write_csr32(usb,
					CVMX_USBCX_HCCHARX(pipe->channel,
						usb->index),
					usbc_hcchar.u32);
		}
	}
	__cvmx_usb_perform_complete(usb, pipe, transaction,
				    CVMX_USB_COMPLETE_CANCEL);
	return 0;
}


/**
 * Cancel all outstanding requests in a pipe. Logically all this
 * does is call cvmx_usb_cancel() in a loop.
 *
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @pipe:	 Pipe to cancel requests in.
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_cancel_all(struct cvmx_usb_state *usb,
			       struct cvmx_usb_pipe *pipe)
{
	struct cvmx_usb_transaction *transaction, *next;

	/* Simply loop through and attempt to cancel each transaction */
	list_for_each_entry_safe(transaction, next, &pipe->transactions, node) {
		int result = cvmx_usb_cancel(usb, pipe, transaction);

		if (unlikely(result != 0))
			return result;
	}
	return 0;
}


/**
 * Close a pipe created with cvmx_usb_open_pipe().
 *
 * @usb:	 USB device state populated by cvmx_usb_initialize().
 * @pipe:	 Pipe to close.
 *
 * Returns: 0 or a negative error code. EBUSY is returned if the pipe has
 *	    outstanding transfers.
 */
static int cvmx_usb_close_pipe(struct cvmx_usb_state *usb,
			       struct cvmx_usb_pipe *pipe)
{
	/* Fail if the pipe has pending transactions */
	if (!list_empty(&pipe->transactions))
		return -EBUSY;

	list_del(&pipe->node);
	kfree(pipe);

	return 0;
}

/**
 * Get the current USB protocol level frame number. The frame
 * number is always in the range of 0-0x7ff.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: USB frame number
 */
static int cvmx_usb_get_frame_number(struct cvmx_usb_state *usb)
{
	int frame_number;
	union cvmx_usbcx_hfnum usbc_hfnum;

	usbc_hfnum.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HFNUM(usb->index));
	frame_number = usbc_hfnum.s.frnum;

	return frame_number;
}


/**
 * Poll a channel for status
 *
 * @usb:     USB device
 * @channel: Channel to poll
 *
 * Returns: Zero on success
 */
static int __cvmx_usb_poll_channel(struct cvmx_usb_state *usb, int channel)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);
	struct usb_hcd *hcd = octeon_to_hcd(priv);
	struct device *dev = hcd->self.controller;
	union cvmx_usbcx_hcintx usbc_hcint;
	union cvmx_usbcx_hctsizx usbc_hctsiz;
	union cvmx_usbcx_hccharx usbc_hcchar;
	struct cvmx_usb_pipe *pipe;
	struct cvmx_usb_transaction *transaction;
	int bytes_this_transfer;
	int bytes_in_last_packet;
	int packets_processed;
	int buffer_space_left;

	/* Read the interrupt status bits for the channel */
	usbc_hcint.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCINTX(channel, usb->index));

	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
		usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HCCHARX(channel, usb->index));

		if (usbc_hcchar.s.chena && usbc_hcchar.s.chdis) {
			/*
			 * There seems to be a bug in CN31XX which can cause
			 * interrupt IN transfers to get stuck until we do a
			 * write of HCCHARX without changing things
			 */
			__cvmx_usb_write_csr32(usb,
					CVMX_USBCX_HCCHARX(channel,
							   usb->index),
					usbc_hcchar.u32);
			return 0;
		}

		/*
		 * In non DMA mode the channels don't halt themselves. We need
		 * to manually disable channels that are left running
		 */
		if (!usbc_hcint.s.chhltd) {
			if (usbc_hcchar.s.chena) {
				union cvmx_usbcx_hcintmskx hcintmsk;
				/* Disable all interrupts except CHHLTD */
				hcintmsk.u32 = 0;
				hcintmsk.s.chhltdmsk = 1;
				__cvmx_usb_write_csr32(usb,
						CVMX_USBCX_HCINTMSKX(channel,
							usb->index),
						hcintmsk.u32);
				usbc_hcchar.s.chdis = 1;
				__cvmx_usb_write_csr32(usb,
						CVMX_USBCX_HCCHARX(channel,
							usb->index),
						usbc_hcchar.u32);
				return 0;
			} else if (usbc_hcint.s.xfercompl) {
				/*
				 * Successful IN/OUT with transfer complete.
				 * Channel halt isn't needed.
				 */
			} else {
				dev_err(dev, "USB%d: Channel %d interrupt without halt\n",
					usb->index, channel);
				return 0;
			}
		}
	} else {
		/*
		 * There is are no interrupts that we need to process when the
		 * channel is still running
		 */
		if (!usbc_hcint.s.chhltd)
			return 0;
	}

	/* Disable the channel interrupts now that it is done */
	__cvmx_usb_write_csr32(usb,
				CVMX_USBCX_HCINTMSKX(channel, usb->index),
				0);
	usb->idle_hardware_channels |= (1<<channel);

	/* Make sure this channel is tied to a valid pipe */
	pipe = usb->pipe_for_channel[channel];
	prefetch(pipe);
	if (!pipe)
		return 0;
	transaction = list_first_entry(&pipe->transactions,
				       typeof(*transaction),
				       node);
	prefetch(transaction);

	/*
	 * Disconnect this pipe from the HW channel. Later the schedule
	 * function will figure out which pipe needs to go
	 */
	usb->pipe_for_channel[channel] = NULL;
	pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

	/*
	 * Read the channel config info so we can figure out how much data
	 * transferred
	 */
	usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCCHARX(channel, usb->index));
	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb,
			CVMX_USBCX_HCTSIZX(channel, usb->index));

	/*
	 * Calculating the number of bytes successfully transferred is dependent
	 * on the transfer direction
	 */
	packets_processed = transaction->pktcnt - usbc_hctsiz.s.pktcnt;
	if (usbc_hcchar.s.epdir) {
		/*
		 * IN transactions are easy. For every byte received the
		 * hardware decrements xfersize. All we need to do is subtract
		 * the current value of xfersize from its starting value and we
		 * know how many bytes were written to the buffer
		 */
		bytes_this_transfer = transaction->xfersize -
			usbc_hctsiz.s.xfersize;
	} else {
		/*
		 * OUT transaction don't decrement xfersize. Instead pktcnt is
		 * decremented on every successful packet send. The hardware
		 * does this when it receives an ACK, or NYET. If it doesn't
		 * receive one of these responses pktcnt doesn't change
		 */
		bytes_this_transfer = packets_processed * usbc_hcchar.s.mps;
		/*
		 * The last packet may not be a full transfer if we didn't have
		 * enough data
		 */
		if (bytes_this_transfer > transaction->xfersize)
			bytes_this_transfer = transaction->xfersize;
	}
	/* Figure out how many bytes were in the last packet of the transfer */
	if (packets_processed)
		bytes_in_last_packet = bytes_this_transfer -
			(packets_processed - 1) * usbc_hcchar.s.mps;
	else
		bytes_in_last_packet = bytes_this_transfer;

	/*
	 * As a special case, setup transactions output the setup header, not
	 * the user's data. For this reason we don't count setup data as bytes
	 * transferred
	 */
	if ((transaction->stage == CVMX_USB_STAGE_SETUP) ||
		(transaction->stage == CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE))
		bytes_this_transfer = 0;

	/*
	 * Add the bytes transferred to the running total. It is important that
	 * bytes_this_transfer doesn't count any data that needs to be
	 * retransmitted
	 */
	transaction->actual_bytes += bytes_this_transfer;
	if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
		buffer_space_left = transaction->iso_packets[0].length -
			transaction->actual_bytes;
	else
		buffer_space_left = transaction->buffer_length -
			transaction->actual_bytes;

	/*
	 * We need to remember the PID toggle state for the next transaction.
	 * The hardware already updated it for the next transaction
	 */
	pipe->pid_toggle = !(usbc_hctsiz.s.pid == 0);

	/*
	 * For high speed bulk out, assume the next transaction will need to do
	 * a ping before proceeding. If this isn't true the ACK processing below
	 * will clear this flag
	 */
	if ((pipe->device_speed == CVMX_USB_SPEED_HIGH) &&
		(pipe->transfer_type == CVMX_USB_TRANSFER_BULK) &&
		(pipe->transfer_dir == CVMX_USB_DIRECTION_OUT))
		pipe->flags |= __CVMX_USB_PIPE_FLAGS_NEED_PING;

	if (usbc_hcint.s.stall) {
		/*
		 * STALL as a response means this transaction cannot be
		 * completed because the device can't process transactions. Tell
		 * the user. Any data that was transferred will be counted on
		 * the actual bytes transferred
		 */
		pipe->pid_toggle = 0;
		__cvmx_usb_perform_complete(usb, pipe, transaction,
					    CVMX_USB_COMPLETE_STALL);
	} else if (usbc_hcint.s.xacterr) {
		/*
		 * We know at least one packet worked if we get a ACK or NAK.
		 * Reset the retry counter
		 */
		if (usbc_hcint.s.nak || usbc_hcint.s.ack)
			transaction->retries = 0;
		transaction->retries++;
		if (transaction->retries > MAX_RETRIES) {
			/*
			 * XactErr as a response means the device signaled
			 * something wrong with the transfer. For example, PID
			 * toggle errors cause these
			 */
			__cvmx_usb_perform_complete(usb, pipe, transaction,
						    CVMX_USB_COMPLETE_XACTERR);
		} else {
			/*
			 * If this was a split then clear our split in progress
			 * marker
			 */
			if (usb->active_split == transaction)
				usb->active_split = NULL;
			/*
			 * Rewind to the beginning of the transaction by anding
			 * off the split complete bit
			 */
			transaction->stage &= ~1;
			pipe->split_sc_frame = -1;
			pipe->next_tx_frame += pipe->interval;
			if (pipe->next_tx_frame < usb->frame_number)
				pipe->next_tx_frame =
					usb->frame_number + pipe->interval -
					(usb->frame_number -
					 pipe->next_tx_frame) % pipe->interval;
		}
	} else if (usbc_hcint.s.bblerr) {
		/* Babble Error (BblErr) */
		__cvmx_usb_perform_complete(usb, pipe, transaction,
					    CVMX_USB_COMPLETE_BABBLEERR);
	} else if (usbc_hcint.s.datatglerr) {
		/* Data toggle error */
		__cvmx_usb_perform_complete(usb, pipe, transaction,
					    CVMX_USB_COMPLETE_DATATGLERR);
	} else if (usbc_hcint.s.nyet) {
		/*
		 * NYET as a response is only allowed in three cases: as a
		 * response to a ping, as a response to a split transaction, and
		 * as a response to a bulk out. The ping case is handled by
		 * hardware, so we only have splits and bulk out
		 */
		if (!__cvmx_usb_pipe_needs_split(usb, pipe)) {
			transaction->retries = 0;
			/*
			 * If there is more data to go then we need to try
			 * again. Otherwise this transaction is complete
			 */
			if ((buffer_space_left == 0) ||
				(bytes_in_last_packet < pipe->max_packet))
				__cvmx_usb_perform_complete(usb, pipe,
						transaction,
						CVMX_USB_COMPLETE_SUCCESS);
		} else {
			/*
			 * Split transactions retry the split complete 4 times
			 * then rewind to the start split and do the entire
			 * transactions again
			 */
			transaction->retries++;
			if ((transaction->retries & 0x3) == 0) {
				/*
				 * Rewind to the beginning of the transaction by
				 * anding off the split complete bit
				 */
				transaction->stage &= ~1;
				pipe->split_sc_frame = -1;
			}
		}
	} else if (usbc_hcint.s.ack) {
		transaction->retries = 0;
		/*
		 * The ACK bit can only be checked after the other error bits.
		 * This is because a multi packet transfer may succeed in a
		 * number of packets and then get a different response on the
		 * last packet. In this case both ACK and the last response bit
		 * will be set. If none of the other response bits is set, then
		 * the last packet must have been an ACK
		 *
		 * Since we got an ACK, we know we don't need to do a ping on
		 * this pipe
		 */
		pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_NEED_PING;

		switch (transaction->type) {
		case CVMX_USB_TRANSFER_CONTROL:
			switch (transaction->stage) {
			case CVMX_USB_STAGE_NON_CONTROL:
			case CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE:
				/* This should be impossible */
				__cvmx_usb_perform_complete(usb, pipe,
					transaction, CVMX_USB_COMPLETE_ERROR);
				break;
			case CVMX_USB_STAGE_SETUP:
				pipe->pid_toggle = 1;
				if (__cvmx_usb_pipe_needs_split(usb, pipe))
					transaction->stage =
						CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE;
				else {
					struct usb_ctrlrequest *header =
						cvmx_phys_to_ptr(transaction->control_header);
					if (header->wLength)
						transaction->stage =
							CVMX_USB_STAGE_DATA;
					else
						transaction->stage =
							CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
				{
					struct usb_ctrlrequest *header =
						cvmx_phys_to_ptr(transaction->control_header);
					if (header->wLength)
						transaction->stage =
							CVMX_USB_STAGE_DATA;
					else
						transaction->stage =
							CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_DATA:
				if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
					transaction->stage =
						CVMX_USB_STAGE_DATA_SPLIT_COMPLETE;
					/*
					 * For setup OUT data that are splits,
					 * the hardware doesn't appear to count
					 * transferred data. Here we manually
					 * update the data transferred
					 */
					if (!usbc_hcchar.s.epdir) {
						if (buffer_space_left < pipe->max_packet)
							transaction->actual_bytes +=
								buffer_space_left;
						else
							transaction->actual_bytes +=
								pipe->max_packet;
					}
				} else if ((buffer_space_left == 0) ||
						(bytes_in_last_packet <
						 pipe->max_packet)) {
					pipe->pid_toggle = 1;
					transaction->stage =
						CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
				if ((buffer_space_left == 0) ||
						(bytes_in_last_packet <
						 pipe->max_packet)) {
					pipe->pid_toggle = 1;
					transaction->stage =
						CVMX_USB_STAGE_STATUS;
				} else {
					transaction->stage =
						CVMX_USB_STAGE_DATA;
				}
				break;
			case CVMX_USB_STAGE_STATUS:
				if (__cvmx_usb_pipe_needs_split(usb, pipe))
					transaction->stage =
						CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE;
				else
					__cvmx_usb_perform_complete(usb, pipe,
						transaction,
						CVMX_USB_COMPLETE_SUCCESS);
				break;
			case CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE:
				__cvmx_usb_perform_complete(usb, pipe,
						transaction,
						CVMX_USB_COMPLETE_SUCCESS);
				break;
			}
			break;
		case CVMX_USB_TRANSFER_BULK:
		case CVMX_USB_TRANSFER_INTERRUPT:
			/*
			 * The only time a bulk transfer isn't complete when it
			 * finishes with an ACK is during a split transaction.
			 * For splits we need to continue the transfer if more
			 * data is needed
			 */
			if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
				if (transaction->stage ==
						CVMX_USB_STAGE_NON_CONTROL)
					transaction->stage =
						CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
				else {
					if (buffer_space_left &&
						(bytes_in_last_packet ==
						 pipe->max_packet))
						transaction->stage =
							CVMX_USB_STAGE_NON_CONTROL;
					else {
						if (transaction->type ==
							CVMX_USB_TRANSFER_INTERRUPT)
							pipe->next_tx_frame +=
								pipe->interval;
							__cvmx_usb_perform_complete(
								usb,
								pipe,
								transaction,
								CVMX_USB_COMPLETE_SUCCESS);
					}
				}
			} else {
				if ((pipe->device_speed ==
					CVMX_USB_SPEED_HIGH) &&
				    (pipe->transfer_type ==
				     CVMX_USB_TRANSFER_BULK) &&
				    (pipe->transfer_dir ==
				     CVMX_USB_DIRECTION_OUT) &&
				    (usbc_hcint.s.nak))
					pipe->flags |=
						__CVMX_USB_PIPE_FLAGS_NEED_PING;
				if (!buffer_space_left ||
					(bytes_in_last_packet <
					 pipe->max_packet)) {
					if (transaction->type ==
						CVMX_USB_TRANSFER_INTERRUPT)
						pipe->next_tx_frame +=
							pipe->interval;
					__cvmx_usb_perform_complete(usb,
						pipe,
						transaction,
						CVMX_USB_COMPLETE_SUCCESS);
				}
			}
			break;
		case CVMX_USB_TRANSFER_ISOCHRONOUS:
			if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
				/*
				 * ISOCHRONOUS OUT splits don't require a
				 * complete split stage. Instead they use a
				 * sequence of begin OUT splits to transfer the
				 * data 188 bytes at a time. Once the transfer
				 * is complete, the pipe sleeps until the next
				 * schedule interval
				 */
				if (pipe->transfer_dir ==
					CVMX_USB_DIRECTION_OUT) {
					/*
					 * If no space left or this wasn't a max
					 * size packet then this transfer is
					 * complete. Otherwise start it again to
					 * send the next 188 bytes
					 */
					if (!buffer_space_left ||
						(bytes_this_transfer < 188)) {
						pipe->next_tx_frame +=
							pipe->interval;
						__cvmx_usb_perform_complete(
							usb,
							pipe,
							transaction,
							CVMX_USB_COMPLETE_SUCCESS);
					}
				} else {
					if (transaction->stage ==
						CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE) {
						/*
						 * We are in the incoming data
						 * phase. Keep getting data
						 * until we run out of space or
						 * get a small packet
						 */
						if ((buffer_space_left == 0) ||
							(bytes_in_last_packet <
							 pipe->max_packet)) {
							pipe->next_tx_frame +=
								pipe->interval;
							__cvmx_usb_perform_complete(
								usb,
								pipe,
								transaction,
								CVMX_USB_COMPLETE_SUCCESS);
						}
					} else
						transaction->stage =
							CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
				}
			} else {
				pipe->next_tx_frame += pipe->interval;
				__cvmx_usb_perform_complete(usb,
						pipe,
						transaction,
						CVMX_USB_COMPLETE_SUCCESS);
			}
			break;
		}
	} else if (usbc_hcint.s.nak) {
		/*
		 * If this was a split then clear our split in progress marker.
		 */
		if (usb->active_split == transaction)
			usb->active_split = NULL;
		/*
		 * NAK as a response means the device couldn't accept the
		 * transaction, but it should be retried in the future. Rewind
		 * to the beginning of the transaction by anding off the split
		 * complete bit. Retry in the next interval
		 */
		transaction->retries = 0;
		transaction->stage &= ~1;
		pipe->next_tx_frame += pipe->interval;
		if (pipe->next_tx_frame < usb->frame_number)
			pipe->next_tx_frame = usb->frame_number +
				pipe->interval -
				(usb->frame_number - pipe->next_tx_frame) %
				pipe->interval;
	} else {
		struct cvmx_usb_port_status port;

		port = cvmx_usb_get_status(usb);
		if (port.port_enabled) {
			/* We'll retry the exact same transaction again */
			transaction->retries++;
		} else {
			/*
			 * We get channel halted interrupts with no result bits
			 * sets when the cable is unplugged
			 */
			__cvmx_usb_perform_complete(usb, pipe, transaction,
					CVMX_USB_COMPLETE_ERROR);
		}
	}
	return 0;
}

static void octeon_usb_port_callback(struct cvmx_usb_state *usb)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);

	spin_unlock(&priv->lock);
	usb_hcd_poll_rh_status(octeon_to_hcd(priv));
	spin_lock(&priv->lock);
}

/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @usb: USB device state populated by cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
static int cvmx_usb_poll(struct cvmx_usb_state *usb)
{
	union cvmx_usbcx_hfnum usbc_hfnum;
	union cvmx_usbcx_gintsts usbc_gintsts;

	prefetch_range(usb, sizeof(*usb));

	/* Update the frame counter */
	usbc_hfnum.u32 = __cvmx_usb_read_csr32(usb,
						CVMX_USBCX_HFNUM(usb->index));
	if ((usb->frame_number&0x3fff) > usbc_hfnum.s.frnum)
		usb->frame_number += 0x4000;
	usb->frame_number &= ~0x3fffull;
	usb->frame_number |= usbc_hfnum.s.frnum;

	/* Read the pending interrupts */
	usbc_gintsts.u32 = __cvmx_usb_read_csr32(usb,
						CVMX_USBCX_GINTSTS(usb->index));

	/* Clear the interrupts now that we know about them */
	__cvmx_usb_write_csr32(usb,
				CVMX_USBCX_GINTSTS(usb->index),
				usbc_gintsts.u32);

	if (usbc_gintsts.s.rxflvl) {
		/*
		 * RxFIFO Non-Empty (RxFLvl)
		 * Indicates that there is at least one packet pending to be
		 * read from the RxFIFO.
		 *
		 * In DMA mode this is handled by hardware
		 */
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			__cvmx_usb_poll_rx_fifo(usb);
	}
	if (usbc_gintsts.s.ptxfemp || usbc_gintsts.s.nptxfemp) {
		/* Fill the Tx FIFOs when not in DMA mode */
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			__cvmx_usb_poll_tx_fifo(usb);
	}
	if (usbc_gintsts.s.disconnint || usbc_gintsts.s.prtint) {
		union cvmx_usbcx_hprt usbc_hprt;
		/*
		 * Disconnect Detected Interrupt (DisconnInt)
		 * Asserted when a device disconnect is detected.
		 *
		 * Host Port Interrupt (PrtInt)
		 * The core sets this bit to indicate a change in port status of
		 * one of the O2P USB core ports in Host mode. The application
		 * must read the Host Port Control and Status (HPRT) register to
		 * determine the exact event that caused this interrupt. The
		 * application must clear the appropriate status bit in the Host
		 * Port Control and Status register to clear this bit.
		 *
		 * Call the user's port callback
		 */
		octeon_usb_port_callback(usb);
		/* Clear the port change bits */
		usbc_hprt.u32 = __cvmx_usb_read_csr32(usb,
				CVMX_USBCX_HPRT(usb->index));
		usbc_hprt.s.prtena = 0;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HPRT(usb->index),
				       usbc_hprt.u32);
	}
	if (usbc_gintsts.s.hchint) {
		/*
		 * Host Channels Interrupt (HChInt)
		 * The core sets this bit to indicate that an interrupt is
		 * pending on one of the channels of the core (in Host mode).
		 * The application must read the Host All Channels Interrupt
		 * (HAINT) register to determine the exact number of the channel
		 * on which the interrupt occurred, and then read the
		 * corresponding Host Channel-n Interrupt (HCINTn) register to
		 * determine the exact cause of the interrupt. The application
		 * must clear the appropriate status bit in the HCINTn register
		 * to clear this bit.
		 */
		union cvmx_usbcx_haint usbc_haint;

		usbc_haint.u32 = __cvmx_usb_read_csr32(usb,
					CVMX_USBCX_HAINT(usb->index));
		while (usbc_haint.u32) {
			int channel;

			channel = __fls(usbc_haint.u32);
			__cvmx_usb_poll_channel(usb, channel);
			usbc_haint.u32 ^= 1<<channel;
		}
	}

	__cvmx_usb_schedule(usb, usbc_gintsts.s.sof);

	return 0;
}

/* convert between an HCD pointer and the corresponding struct octeon_hcd */
static inline struct octeon_hcd *hcd_to_octeon(struct usb_hcd *hcd)
{
	return (struct octeon_hcd *)(hcd->hcd_priv);
}

static irqreturn_t octeon_usb_irq(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_poll(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;
}

static int octeon_usb_start(struct usb_hcd *hcd)
{
	hcd->state = HC_STATE_RUNNING;
	return 0;
}

static void octeon_usb_stop(struct usb_hcd *hcd)
{
	hcd->state = HC_STATE_HALT;
}

static int octeon_usb_get_frame_number(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);

	return cvmx_usb_get_frame_number(&priv->usb);
}

static int octeon_usb_urb_enqueue(struct usb_hcd *hcd,
				  struct urb *urb,
				  gfp_t mem_flags)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	struct device *dev = hcd->self.controller;
	struct cvmx_usb_transaction *transaction = NULL;
	struct cvmx_usb_pipe *pipe;
	unsigned long flags;
	struct cvmx_usb_iso_packet *iso_packet;
	struct usb_host_endpoint *ep = urb->ep;
	int rc;

	urb->status = 0;
	spin_lock_irqsave(&priv->lock, flags);

	rc = usb_hcd_link_urb_to_ep(hcd, urb);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	if (!ep->hcpriv) {
		enum cvmx_usb_transfer transfer_type;
		enum cvmx_usb_speed speed;
		int split_device = 0;
		int split_port = 0;

		switch (usb_pipetype(urb->pipe)) {
		case PIPE_ISOCHRONOUS:
			transfer_type = CVMX_USB_TRANSFER_ISOCHRONOUS;
			break;
		case PIPE_INTERRUPT:
			transfer_type = CVMX_USB_TRANSFER_INTERRUPT;
			break;
		case PIPE_CONTROL:
			transfer_type = CVMX_USB_TRANSFER_CONTROL;
			break;
		default:
			transfer_type = CVMX_USB_TRANSFER_BULK;
			break;
		}
		switch (urb->dev->speed) {
		case USB_SPEED_LOW:
			speed = CVMX_USB_SPEED_LOW;
			break;
		case USB_SPEED_FULL:
			speed = CVMX_USB_SPEED_FULL;
			break;
		default:
			speed = CVMX_USB_SPEED_HIGH;
			break;
		}
		/*
		 * For slow devices on high speed ports we need to find the hub
		 * that does the speed translation so we know where to send the
		 * split transactions.
		 */
		if (speed != CVMX_USB_SPEED_HIGH) {
			/*
			 * Start at this device and work our way up the usb
			 * tree.
			 */
			struct usb_device *dev = urb->dev;

			while (dev->parent) {
				/*
				 * If our parent is high speed then he'll
				 * receive the splits.
				 */
				if (dev->parent->speed == USB_SPEED_HIGH) {
					split_device = dev->parent->devnum;
					split_port = dev->portnum;
					break;
				}
				/*
				 * Move up the tree one level. If we make it all
				 * the way up the tree, then the port must not
				 * be in high speed mode and we don't need a
				 * split.
				 */
				dev = dev->parent;
			}
		}
		pipe = cvmx_usb_open_pipe(&priv->usb, usb_pipedevice(urb->pipe),
					  usb_pipeendpoint(urb->pipe), speed,
					  le16_to_cpu(ep->desc.wMaxPacketSize)
					  & 0x7ff,
					  transfer_type,
					  usb_pipein(urb->pipe) ?
						CVMX_USB_DIRECTION_IN :
						CVMX_USB_DIRECTION_OUT,
					  urb->interval,
					  (le16_to_cpu(ep->desc.wMaxPacketSize)
					   >> 11) & 0x3,
					  split_device, split_port);
		if (!pipe) {
			usb_hcd_unlink_urb_from_ep(hcd, urb);
			spin_unlock_irqrestore(&priv->lock, flags);
			dev_dbg(dev, "Failed to create pipe\n");
			return -ENOMEM;
		}
		ep->hcpriv = pipe;
	} else {
		pipe = ep->hcpriv;
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		dev_dbg(dev, "Submit isochronous to %d.%d\n",
			usb_pipedevice(urb->pipe),
			usb_pipeendpoint(urb->pipe));
		/*
		 * Allocate a structure to use for our private list of
		 * isochronous packets.
		 */
		iso_packet = kmalloc(urb->number_of_packets *
				     sizeof(struct cvmx_usb_iso_packet),
				     GFP_ATOMIC);
		if (iso_packet) {
			int i;
			/* Fill the list with the data from the URB */
			for (i = 0; i < urb->number_of_packets; i++) {
				iso_packet[i].offset =
					urb->iso_frame_desc[i].offset;
				iso_packet[i].length =
					urb->iso_frame_desc[i].length;
				iso_packet[i].status =
					CVMX_USB_COMPLETE_ERROR;
			}
			/*
			 * Store a pointer to the list in the URB setup_packet
			 * field. We know this currently isn't being used and
			 * this saves us a bunch of logic.
			 */
			urb->setup_packet = (char *)iso_packet;
			transaction = cvmx_usb_submit_isochronous(&priv->usb,
								  pipe, urb);
			/*
			 * If submit failed we need to free our private packet
			 * list.
			 */
			if (!transaction) {
				urb->setup_packet = NULL;
				kfree(iso_packet);
			}
		}
		break;
	case PIPE_INTERRUPT:
		dev_dbg(dev, "Submit interrupt to %d.%d\n",
			usb_pipedevice(urb->pipe),
			usb_pipeendpoint(urb->pipe));
		transaction = cvmx_usb_submit_interrupt(&priv->usb, pipe, urb);
		break;
	case PIPE_CONTROL:
		dev_dbg(dev, "Submit control to %d.%d\n",
			usb_pipedevice(urb->pipe),
			usb_pipeendpoint(urb->pipe));
		transaction = cvmx_usb_submit_control(&priv->usb, pipe, urb);
		break;
	case PIPE_BULK:
		dev_dbg(dev, "Submit bulk to %d.%d\n",
			usb_pipedevice(urb->pipe),
			usb_pipeendpoint(urb->pipe));
		transaction = cvmx_usb_submit_bulk(&priv->usb, pipe, urb);
		break;
	}
	if (!transaction) {
		usb_hcd_unlink_urb_from_ep(hcd, urb);
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_dbg(dev, "Failed to submit\n");
		return -ENOMEM;
	}
	urb->hcpriv = transaction;
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static int octeon_usb_urb_dequeue(struct usb_hcd *hcd,
				  struct urb *urb,
				  int status)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;
	int rc;

	if (!urb->dev)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc)
		goto out;

	urb->status = status;
	cvmx_usb_cancel(&priv->usb, urb->ep->hcpriv, urb->hcpriv);

out:
	spin_unlock_irqrestore(&priv->lock, flags);

	return rc;
}

static void octeon_usb_endpoint_disable(struct usb_hcd *hcd,
					struct usb_host_endpoint *ep)
{
	struct device *dev = hcd->self.controller;

	if (ep->hcpriv) {
		struct octeon_hcd *priv = hcd_to_octeon(hcd);
		struct cvmx_usb_pipe *pipe = ep->hcpriv;
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);
		cvmx_usb_cancel_all(&priv->usb, pipe);
		if (cvmx_usb_close_pipe(&priv->usb, pipe))
			dev_dbg(dev, "Closing pipe %p failed\n", pipe);
		spin_unlock_irqrestore(&priv->lock, flags);
		ep->hcpriv = NULL;
	}
}

static int octeon_usb_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	struct cvmx_usb_port_status port_status;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	port_status = cvmx_usb_get_status(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	buf[0] = 0;
	buf[0] = port_status.connect_change << 1;

	return buf[0] != 0;
}

static int octeon_usb_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
				u16 wIndex, char *buf, u16 wLength)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	struct device *dev = hcd->self.controller;
	struct cvmx_usb_port_status usb_port_status;
	int port_status;
	struct usb_hub_descriptor *desc;
	unsigned long flags;

	switch (typeReq) {
	case ClearHubFeature:
		dev_dbg(dev, "ClearHubFeature\n");
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;
		default:
			return -EINVAL;
		}
		break;
	case ClearPortFeature:
		dev_dbg(dev, "ClearPortFeature\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			dev_dbg(dev, " ENABLE\n");
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_disable(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(dev, " SUSPEND\n");
			/* Not supported on Octeon */
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(dev, " POWER\n");
			/* Not supported on Octeon */
			break;
		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(dev, " INDICATOR\n");
			/* Port inidicator not supported */
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			dev_dbg(dev, " C_CONNECTION\n");
			/* Clears drivers internal connect status change flag */
			spin_lock_irqsave(&priv->lock, flags);
			priv->usb.port_status =
				cvmx_usb_get_status(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_RESET:
			dev_dbg(dev, " C_RESET\n");
			/*
			 * Clears the driver's internal Port Reset Change flag.
			 */
			spin_lock_irqsave(&priv->lock, flags);
			priv->usb.port_status =
				cvmx_usb_get_status(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			dev_dbg(dev, " C_ENABLE\n");
			/*
			 * Clears the driver's internal Port Enable/Disable
			 * Change flag.
			 */
			spin_lock_irqsave(&priv->lock, flags);
			priv->usb.port_status =
				cvmx_usb_get_status(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			dev_dbg(dev, " C_SUSPEND\n");
			/*
			 * Clears the driver's internal Port Suspend Change
			 * flag, which is set when resume signaling on the host
			 * port is complete.
			 */
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(dev, " C_OVER_CURRENT\n");
			/* Clears the driver's overcurrent Change flag */
			spin_lock_irqsave(&priv->lock, flags);
			priv->usb.port_status =
				cvmx_usb_get_status(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		default:
			dev_dbg(dev, " UNKNOWN\n");
			return -EINVAL;
		}
		break;
	case GetHubDescriptor:
		dev_dbg(dev, "GetHubDescriptor\n");
		desc = (struct usb_hub_descriptor *)buf;
		desc->bDescLength = 9;
		desc->bDescriptorType = 0x29;
		desc->bNbrPorts = 1;
		desc->wHubCharacteristics = cpu_to_le16(0x08);
		desc->bPwrOn2PwrGood = 1;
		desc->bHubContrCurrent = 0;
		desc->u.hs.DeviceRemovable[0] = 0;
		desc->u.hs.DeviceRemovable[1] = 0xff;
		break;
	case GetHubStatus:
		dev_dbg(dev, "GetHubStatus\n");
		*(__le32 *) buf = 0;
		break;
	case GetPortStatus:
		dev_dbg(dev, "GetPortStatus\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		spin_lock_irqsave(&priv->lock, flags);
		usb_port_status = cvmx_usb_get_status(&priv->usb);
		spin_unlock_irqrestore(&priv->lock, flags);
		port_status = 0;

		if (usb_port_status.connect_change) {
			port_status |= (1 << USB_PORT_FEAT_C_CONNECTION);
			dev_dbg(dev, " C_CONNECTION\n");
		}

		if (usb_port_status.port_enabled) {
			port_status |= (1 << USB_PORT_FEAT_C_ENABLE);
			dev_dbg(dev, " C_ENABLE\n");
		}

		if (usb_port_status.connected) {
			port_status |= (1 << USB_PORT_FEAT_CONNECTION);
			dev_dbg(dev, " CONNECTION\n");
		}

		if (usb_port_status.port_enabled) {
			port_status |= (1 << USB_PORT_FEAT_ENABLE);
			dev_dbg(dev, " ENABLE\n");
		}

		if (usb_port_status.port_over_current) {
			port_status |= (1 << USB_PORT_FEAT_OVER_CURRENT);
			dev_dbg(dev, " OVER_CURRENT\n");
		}

		if (usb_port_status.port_powered) {
			port_status |= (1 << USB_PORT_FEAT_POWER);
			dev_dbg(dev, " POWER\n");
		}

		if (usb_port_status.port_speed == CVMX_USB_SPEED_HIGH) {
			port_status |= USB_PORT_STAT_HIGH_SPEED;
			dev_dbg(dev, " HIGHSPEED\n");
		} else if (usb_port_status.port_speed == CVMX_USB_SPEED_LOW) {
			port_status |= (1 << USB_PORT_FEAT_LOWSPEED);
			dev_dbg(dev, " LOWSPEED\n");
		}

		*((__le32 *) buf) = cpu_to_le32(port_status);
		break;
	case SetHubFeature:
		dev_dbg(dev, "SetHubFeature\n");
		/* No HUB features supported */
		break;
	case SetPortFeature:
		dev_dbg(dev, "SetPortFeature\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(dev, " SUSPEND\n");
			return -EINVAL;
		case USB_PORT_FEAT_POWER:
			dev_dbg(dev, " POWER\n");
			return -EINVAL;
		case USB_PORT_FEAT_RESET:
			dev_dbg(dev, " RESET\n");
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_disable(&priv->usb);
			if (cvmx_usb_enable(&priv->usb))
				dev_dbg(dev, "Failed to enable the port\n");
			spin_unlock_irqrestore(&priv->lock, flags);
			return 0;
		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(dev, " INDICATOR\n");
			/* Not supported */
			break;
		default:
			dev_dbg(dev, " UNKNOWN\n");
			return -EINVAL;
		}
		break;
	default:
		dev_dbg(dev, "Unknown root hub request\n");
		return -EINVAL;
	}
	return 0;
}

static const struct hc_driver octeon_hc_driver = {
	.description		= "Octeon USB",
	.product_desc		= "Octeon Host Controller",
	.hcd_priv_size		= sizeof(struct octeon_hcd),
	.irq			= octeon_usb_irq,
	.flags			= HCD_MEMORY | HCD_USB2,
	.start			= octeon_usb_start,
	.stop			= octeon_usb_stop,
	.urb_enqueue		= octeon_usb_urb_enqueue,
	.urb_dequeue		= octeon_usb_urb_dequeue,
	.endpoint_disable	= octeon_usb_endpoint_disable,
	.get_frame_number	= octeon_usb_get_frame_number,
	.hub_status_data	= octeon_usb_hub_status_data,
	.hub_control		= octeon_usb_hub_control,
	.map_urb_for_dma	= octeon_map_urb_for_dma,
	.unmap_urb_for_dma	= octeon_unmap_urb_for_dma,
};

static int octeon_usb_probe(struct platform_device *pdev)
{
	int status;
	int initialize_flags;
	int usb_num;
	struct resource *res_mem;
	struct device_node *usbn_node;
	int irq = platform_get_irq(pdev, 0);
	struct device *dev = &pdev->dev;
	struct octeon_hcd *priv;
	struct usb_hcd *hcd;
	unsigned long flags;
	u32 clock_rate = 48000000;
	bool is_crystal_clock = false;
	const char *clock_type;
	int i;

	if (dev->of_node == NULL) {
		dev_err(dev, "Error: empty of_node\n");
		return -ENXIO;
	}
	usbn_node = dev->of_node->parent;

	i = of_property_read_u32(usbn_node,
				 "refclk-frequency", &clock_rate);
	if (i) {
		dev_err(dev, "No USBN \"refclk-frequency\"\n");
		return -ENXIO;
	}
	switch (clock_rate) {
	case 12000000:
		initialize_flags = CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ;
		break;
	case 24000000:
		initialize_flags = CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ;
		break;
	case 48000000:
		initialize_flags = CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ;
		break;
	default:
		dev_err(dev, "Illebal USBN \"refclk-frequency\" %u\n",
				clock_rate);
		return -ENXIO;

	}

	i = of_property_read_string(usbn_node,
				    "refclk-type", &clock_type);

	if (!i && strcmp("crystal", clock_type) == 0)
		is_crystal_clock = true;

	if (is_crystal_clock)
		initialize_flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI;
	else
		initialize_flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem == NULL) {
		dev_err(dev, "found no memory resource\n");
		return -ENXIO;
	}
	usb_num = (res_mem->start >> 44) & 1;

	if (irq < 0) {
		/* Defective device tree, but we know how to fix it. */
		irq_hw_number_t hwirq = usb_num ? (1 << 6) + 17 : 56;

		irq = irq_create_mapping(NULL, hwirq);
	}

	/*
	 * Set the DMA mask to 64bits so we get buffers already translated for
	 * DMA.
	 */
	dev->coherent_dma_mask = ~0;
	dev->dma_mask = &dev->coherent_dma_mask;

	/*
	 * Only cn52XX and cn56XX have DWC_OTG USB hardware and the
	 * IOB priority registers.  Under heavy network load USB
	 * hardware can be starved by the IOB causing a crash.  Give
	 * it a priority boost if it has been waiting more than 400
	 * cycles to avoid this situation.
	 *
	 * Testing indicates that a cnt_val of 8192 is not sufficient,
	 * but no failures are seen with 4096.  We choose a value of
	 * 400 to give a safety factor of 10.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)) {
		union cvmx_iob_n2c_l2c_pri_cnt pri_cnt;

		pri_cnt.u64 = 0;
		pri_cnt.s.cnt_enb = 1;
		pri_cnt.s.cnt_val = 400;
		cvmx_write_csr(CVMX_IOB_N2C_L2C_PRI_CNT, pri_cnt.u64);
	}

	hcd = usb_create_hcd(&octeon_hc_driver, dev, dev_name(dev));
	if (!hcd) {
		dev_dbg(dev, "Failed to allocate memory for HCD\n");
		return -1;
	}
	hcd->uses_new_polling = 1;
	priv = (struct octeon_hcd *)hcd->hcd_priv;

	spin_lock_init(&priv->lock);

	status = cvmx_usb_initialize(&priv->usb, usb_num, initialize_flags);
	if (status) {
		dev_dbg(dev, "USB initialization failed with %d\n", status);
		kfree(hcd);
		return -1;
	}

	/* This delay is needed for CN3010, but I don't know why... */
	mdelay(10);

	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_poll(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);

	status = usb_add_hcd(hcd, irq, 0);
	if (status) {
		dev_dbg(dev, "USB add HCD failed with %d\n", status);
		kfree(hcd);
		return -1;
	}
	device_wakeup_enable(hcd->self.controller);

	dev_info(dev, "Registered HCD for port %d on irq %d\n", usb_num, irq);

	return 0;
}

static int octeon_usb_remove(struct platform_device *pdev)
{
	int status;
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	usb_remove_hcd(hcd);
	spin_lock_irqsave(&priv->lock, flags);
	status = cvmx_usb_shutdown(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	if (status)
		dev_dbg(dev, "USB shutdown failed with %d\n", status);

	kfree(hcd);

	return 0;
}

static struct of_device_id octeon_usb_match[] = {
	{
		.compatible = "cavium,octeon-5750-usbc",
	},
	{},
};

static struct platform_driver octeon_usb_driver = {
	.driver = {
		.name       = "OcteonUSB",
		.of_match_table = octeon_usb_match,
	},
	.probe      = octeon_usb_probe,
	.remove     = octeon_usb_remove,
};

static int __init octeon_usb_driver_init(void)
{
	if (usb_disabled())
		return 0;

	return platform_driver_register(&octeon_usb_driver);
}
module_init(octeon_usb_driver_init);

static void __exit octeon_usb_driver_exit(void)
{
	if (usb_disabled())
		return;

	platform_driver_unregister(&octeon_usb_driver);
}
module_exit(octeon_usb_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium, Inc. <support@cavium.com>");
MODULE_DESCRIPTION("Cavium Inc. OCTEON USB Host driver.");
