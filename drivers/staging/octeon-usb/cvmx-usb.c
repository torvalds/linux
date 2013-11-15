/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Networks (support@cavium.com). All rights
 * reserved.
 *
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

 *   * Neither the name of Cavium Networks nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * @file
 *
 * "cvmx-usb.c" defines a set of low level USB functions to help
 * developers create Octeon USB drivers for various operating
 * systems. These functions provide a generic API to the Octeon
 * USB blocks, hiding the internal hardware specific
 * operations.
 */
#include <linux/delay.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-sysinfo.h>
#include "cvmx-usbnx-defs.h"
#include "cvmx-usbcx-defs.h"
#include "cvmx-usb.h"
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>

#define CVMX_PREFETCH0(address) CVMX_PREFETCH(address, 0)
#define CVMX_PREFETCH128(address) CVMX_PREFETCH(address, 128)
// a normal prefetch
#define CVMX_PREFETCH(address, offset) CVMX_PREFETCH_PREF0(address, offset)
// normal prefetches that use the pref instruction
#define CVMX_PREFETCH_PREFX(X, address, offset) asm volatile ("pref %[type], %[off](%[rbase])" : : [rbase] "d" (address), [off] "I" (offset), [type] "n" (X))
#define CVMX_PREFETCH_PREF0(address, offset) CVMX_PREFETCH_PREFX(0, address, offset)
#define CVMX_CLZ(result, input) asm ("clz %[rd],%[rs]" : [rd] "=d" (result) : [rs] "d" (input))

#define MAX_RETRIES		3		/* Maximum number of times to retry failed transactions */
#define MAX_PIPES		32		/* Maximum number of pipes that can be open at once */
#define MAX_TRANSACTIONS	256		/* Maximum number of outstanding transactions across all pipes */
#define MAX_CHANNELS		8		/* Maximum number of hardware channels supported by the USB block */
#define MAX_USB_ADDRESS		127		/* The highest valid USB device address */
#define MAX_USB_ENDPOINT	15		/* The highest valid USB endpoint number */
#define MAX_USB_HUB_PORT	15		/* The highest valid port number on a hub */
#define MAX_TRANSFER_BYTES	((1<<19)-1)	/* The low level hardware can transfer a maximum of this number of bytes in each transfer. The field is 19 bits wide */
#define MAX_TRANSFER_PACKETS	((1<<10)-1)	/* The low level hardware can transfer a maximum of this number of packets in each transfer. The field is 10 bits wide */

/*
 * These defines disable the normal read and write csr. This is so I can add
 * extra debug stuff to the usb specific version and I won't use the normal
 * version by mistake
 */
#define cvmx_read_csr use_cvmx_usb_read_csr64_instead_of_cvmx_read_csr
#define cvmx_write_csr use_cvmx_usb_write_csr64_instead_of_cvmx_write_csr

enum cvmx_usb_transaction_flags {
	__CVMX_USB_TRANSACTION_FLAGS_IN_USE = 1<<16,
};

enum {
	USB_CLOCK_TYPE_REF_12,
	USB_CLOCK_TYPE_REF_24,
	USB_CLOCK_TYPE_REF_48,
	USB_CLOCK_TYPE_CRYSTAL_12,
};

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
 * @prev:		Transaction before this one in the pipe.
 * @next:		Transaction after this one in the pipe.
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
 * @callback:		User's callback function when complete.
 * @callback_data:	User's data.
 */
struct cvmx_usb_transaction {
	struct cvmx_usb_transaction *prev;
	struct cvmx_usb_transaction *next;
	enum cvmx_usb_transfer type;
	enum cvmx_usb_transaction_flags flags;
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
	cvmx_usb_callback_func_t callback;
	void *callback_data;
};

/**
 * struct cvmx_usb_pipe - a pipe represents a virtual connection between Octeon
 *			  and some USB device. It contains a list of pending
 *			  request to the device.
 *
 * @prev:		Pipe before this one in the list
 * @next:		Pipe after this one in the list
 * @head:		The first pending transaction
 * @tail:		The last pending transaction
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
	struct cvmx_usb_pipe *prev;
	struct cvmx_usb_pipe *next;
	struct cvmx_usb_transaction *head;
	struct cvmx_usb_transaction *tail;
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

/**
 * struct cvmx_usb_pipe_list
 *
 * @head: Head of the list, or NULL if empty.
 * @tail: Tail if the list, or NULL if empty.
 */
struct cvmx_usb_pipe_list {
	struct cvmx_usb_pipe *head;
	struct cvmx_usb_pipe *tail;
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
 * struct cvmx_usb_internal_state - the state of the USB block
 *
 * init_flags:		   Flags passed to initialize.
 * index:		   Which USB block this is for.
 * idle_hardware_channels: Bit set for every idle hardware channel.
 * usbcx_hprt:		   Stored port status so we don't need to read a CSR to
 *			   determine splits.
 * pipe_for_channel:	   Map channels to pipes.
 * free_transaction_head:  List of free transactions head.
 * free_transaction_tail:  List of free transactions tail.
 * pipe:		   Storage for pipes.
 * transaction:		   Storage for transactions.
 * callback:		   User global callbacks.
 * callback_data:	   User data for each callback.
 * indent:		   Used by debug output to indent functions.
 * port_status:		   Last port status used for change notification.
 * free_pipes:		   List of all pipes that are currently closed.
 * idle_pipes:		   List of open pipes that have no transactions.
 * active_pipes:	   Active pipes indexed by transfer type.
 * frame_number:	   Increments every SOF interrupt for time keeping.
 * active_split:	   Points to the current active split, or NULL.
 */
struct cvmx_usb_internal_state {
	int init_flags;
	int index;
	int idle_hardware_channels;
	union cvmx_usbcx_hprt usbcx_hprt;
	struct cvmx_usb_pipe *pipe_for_channel[MAX_CHANNELS];
	struct cvmx_usb_transaction *free_transaction_head;
	struct cvmx_usb_transaction *free_transaction_tail;
	struct cvmx_usb_pipe pipe[MAX_PIPES];
	struct cvmx_usb_transaction transaction[MAX_TRANSACTIONS];
	cvmx_usb_callback_func_t callback[__CVMX_USB_CALLBACK_END];
	void *callback_data[__CVMX_USB_CALLBACK_END];
	int indent;
	struct cvmx_usb_port_status port_status;
	struct cvmx_usb_pipe_list free_pipes;
	struct cvmx_usb_pipe_list idle_pipes;
	struct cvmx_usb_pipe_list active_pipes[4];
	uint64_t frame_number;
	struct cvmx_usb_transaction *active_split;
	struct cvmx_usb_tx_fifo periodic;
	struct cvmx_usb_tx_fifo nonperiodic;
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
#define USB_FIFO_ADDRESS(channel, usb_index) (CVMX_USBCX_GOTGCTL(usb_index) + ((channel)+1)*0x1000)

static int octeon_usb_get_clock_type(void)
{
	switch (cvmx_sysinfo_get()->board_type) {
	case CVMX_BOARD_TYPE_BBGW_REF:
	case CVMX_BOARD_TYPE_LANAI2_A:
	case CVMX_BOARD_TYPE_LANAI2_U:
	case CVMX_BOARD_TYPE_LANAI2_G:
	case CVMX_BOARD_TYPE_UBNT_E100:
		return USB_CLOCK_TYPE_CRYSTAL_12;
	}
	return USB_CLOCK_TYPE_REF_48;
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
static inline uint32_t __cvmx_usb_read_csr32(struct cvmx_usb_internal_state *usb,
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
static inline void __cvmx_usb_write_csr32(struct cvmx_usb_internal_state *usb,
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
static inline uint64_t __cvmx_usb_read_csr64(struct cvmx_usb_internal_state *usb,
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
static inline void __cvmx_usb_write_csr64(struct cvmx_usb_internal_state *usb,
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
static inline int __cvmx_usb_pipe_needs_split(struct cvmx_usb_internal_state *usb, struct cvmx_usb_pipe *pipe)
{
	return ((pipe->device_speed != CVMX_USB_SPEED_HIGH) && (usb->usbcx_hprt.s.prtspd == CVMX_USB_SPEED_HIGH));
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
	else
		return 0; /* Data0 */
}


/**
 * Return the number of USB ports supported by this Octeon
 * chip. If the chip doesn't support USB, or is not supported
 * by this API, a zero will be returned. Most Octeon chips
 * support one usb port, but some support two ports.
 * cvmx_usb_initialize() must be called on independent
 * struct cvmx_usb_state.
 *
 * Returns: Number of port, zero if usb isn't supported
 */
int cvmx_usb_get_num_ports(void)
{
	int arch_ports = 0;

	if (OCTEON_IS_MODEL(OCTEON_CN56XX))
		arch_ports = 1;
	else if (OCTEON_IS_MODEL(OCTEON_CN52XX))
		arch_ports = 2;
	else if (OCTEON_IS_MODEL(OCTEON_CN50XX))
		arch_ports = 1;
	else if (OCTEON_IS_MODEL(OCTEON_CN31XX))
		arch_ports = 1;
	else if (OCTEON_IS_MODEL(OCTEON_CN30XX))
		arch_ports = 1;
	else
		arch_ports = 0;

	return arch_ports;
}


/**
 * Allocate a usb transaction for use
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 *
 * Returns: Transaction or NULL
 */
static inline struct cvmx_usb_transaction *__cvmx_usb_alloc_transaction(struct cvmx_usb_internal_state *usb)
{
	struct cvmx_usb_transaction *t;
	t = usb->free_transaction_head;
	if (t) {
		usb->free_transaction_head = t->next;
		if (!usb->free_transaction_head)
			usb->free_transaction_tail = NULL;
	}
	if (t) {
		memset(t, 0, sizeof(*t));
		t->flags = __CVMX_USB_TRANSACTION_FLAGS_IN_USE;
	}
	return t;
}


/**
 * Free a usb transaction
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @transaction:
 *		 Transaction to free
 */
static inline void __cvmx_usb_free_transaction(struct cvmx_usb_internal_state *usb,
					       struct cvmx_usb_transaction *transaction)
{
	transaction->flags = 0;
	transaction->prev = NULL;
	transaction->next = NULL;
	if (usb->free_transaction_tail)
		usb->free_transaction_tail->next = transaction;
	else
		usb->free_transaction_head = transaction;
	usb->free_transaction_tail = transaction;
}


/**
 * Add a pipe to the tail of a list
 * @list:   List to add pipe to
 * @pipe:   Pipe to add
 */
static inline void __cvmx_usb_append_pipe(struct cvmx_usb_pipe_list *list, struct cvmx_usb_pipe *pipe)
{
	pipe->next = NULL;
	pipe->prev = list->tail;
	if (list->tail)
		list->tail->next = pipe;
	else
		list->head = pipe;
	list->tail = pipe;
}


/**
 * Remove a pipe from a list
 * @list:   List to remove pipe from
 * @pipe:   Pipe to remove
 */
static inline void __cvmx_usb_remove_pipe(struct cvmx_usb_pipe_list *list, struct cvmx_usb_pipe *pipe)
{
	if (list->head == pipe) {
		list->head = pipe->next;
		pipe->next = NULL;
		if (list->head)
			list->head->prev = NULL;
		else
			list->tail = NULL;
	} else if (list->tail == pipe) {
		list->tail = pipe->prev;
		list->tail->next = NULL;
		pipe->prev = NULL;
	} else {
		pipe->prev->next = pipe->next;
		pipe->next->prev = pipe->prev;
		pipe->prev = NULL;
		pipe->next = NULL;
	}
}


/**
 * Initialize a USB port for use. This must be called before any
 * other access to the Octeon USB port is made. The port starts
 * off in the disabled state.
 *
 * @state:	 Pointer to an empty struct cvmx_usb_state
 *		 that will be populated by the initialize call.
 *		 This structure is then passed to all other USB
 *		 functions.
 * @usb_port_number:
 *		 Which Octeon USB port to initialize.
 * @flags:	 Flags to control hardware initialization. See
 *		 enum cvmx_usb_initialize_flags for the flag
 *		 definitions. Some flags are mandatory.
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_initialize(struct cvmx_usb_state *state, int usb_port_number,
			enum cvmx_usb_initialize_flags flags)
{
	union cvmx_usbnx_clk_ctl usbn_clk_ctl;
	union cvmx_usbnx_usbp_ctl_status usbn_usbp_ctl_status;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	usb->init_flags = flags;

	/* Make sure that state is large enough to store the internal state */
	if (sizeof(*state) < sizeof(*usb))
		return -EINVAL;
	/* At first allow 0-1 for the usb port number */
	if ((usb_port_number < 0) || (usb_port_number > 1))
		return -EINVAL;
	/* For all chips except 52XX there is only one port */
	if (!OCTEON_IS_MODEL(OCTEON_CN52XX) && (usb_port_number > 0))
		return -EINVAL;
	/* Try to determine clock type automatically */
	if ((flags & (CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI |
		      CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND)) == 0) {
		if (octeon_usb_get_clock_type() == USB_CLOCK_TYPE_CRYSTAL_12)
			flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI;  /* Only 12 MHZ crystals are supported */
		else
			flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND;
	}

	if (flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND) {
		/* Check for auto ref clock frequency */
		if (!(flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK))
			switch (octeon_usb_get_clock_type()) {
			case USB_CLOCK_TYPE_REF_12:
				flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ;
				break;
			case USB_CLOCK_TYPE_REF_24:
				flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ;
				break;
			case USB_CLOCK_TYPE_REF_48:
				flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ;
				break;
			default:
				return -EINVAL;
				break;
			}
	}

	memset(usb, 0, sizeof(*usb));
	usb->init_flags = flags;

	/* Initialize the USB state structure */
	{
		int i;
		usb->index = usb_port_number;

		/* Initialize the transaction double linked list */
		usb->free_transaction_head = NULL;
		usb->free_transaction_tail = NULL;
		for (i = 0; i < MAX_TRANSACTIONS; i++)
			__cvmx_usb_free_transaction(usb, usb->transaction + i);
		for (i = 0; i < MAX_PIPES; i++)
			__cvmx_usb_append_pipe(&usb->free_pipes, usb->pipe + i);
	}

	/*
	 * Power On Reset and PHY Initialization
	 *
	 * 1. Wait for DCOK to assert (nothing to do)
	 *
	 * 2a. Write USBN0/1_CLK_CTL[POR] = 1 and
	 *     USBN0/1_CLK_CTL[HRST,PRST,HCLK_RST] = 0
	 */
	usbn_clk_ctl.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index));
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
		if (OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
			usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
			usbn_clk_ctl.cn31xx.p_xenbn = 0;
		} else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
			usbn_clk_ctl.cn56xx.p_rtype = 2; /* From CN56XX,CN50XX manual */
		else
			usbn_clk_ctl.cn52xx.p_rtype = 1; /* From CN52XX manual */

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
		if (OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
			usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
			usbn_clk_ctl.cn31xx.p_xenbn = 1;
		} else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
			usbn_clk_ctl.cn56xx.p_rtype = 0; /* From CN56XX,CN50XX manual */
		else
			usbn_clk_ctl.cn52xx.p_rtype = 0; /* From CN52XX manual */

		usbn_clk_ctl.s.p_c_sel = 0;
	}
	/*
	 * 2c. Select the HCLK via writing USBN0/1_CLK_CTL[DIVIDE, DIVIDE2] and
	 *     setting USBN0/1_CLK_CTL[ENABLE] = 1. Divide the core clock down
	 *     such that USB is as close as possible to 125Mhz
	 */
	{
		int divisor = (octeon_get_clock_rate()+125000000-1)/125000000;
		if (divisor < 4)  /* Lower than 4 doesn't seem to work properly */
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
	usbn_usbp_ctl_status.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index));
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
		usbcx_gahbcfg.s.dmaen = !(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA);
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			usb->idle_hardware_channels = 0x1;  /* Only use one channel with non DMA */
		else if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
			usb->idle_hardware_channels = 0xf7; /* CN5XXX have an errata with channel 3 */
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
		usbcx_gusbcfg.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index));
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

		usbcx_gintmsk.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GINTMSK(usb->index));
		usbcx_gintmsk.s.otgintmsk = 1;
		usbcx_gintmsk.s.modemismsk = 1;
		usbcx_gintmsk.s.hchintmsk = 1;
		usbcx_gintmsk.s.sofmsk = 0;
		/* We need RX FIFO interrupts if we don't have DMA */
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
			usbcx_gintmsk.s.rxflvlmsk = 1;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GINTMSK(usb->index),
				       usbcx_gintmsk.u32);

		/* Disable all channel interrupts. We'll enable them per channel later */
		for (channel = 0; channel < 8; channel++)
			__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), 0);
	}

	{
		/*
		 * Host Port Initialization
		 *
		 * 1. Program the host-port interrupt-mask field to unmask,
		 *    USBC_GINTMSK[PRTINT] = 1
		 */
		USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk,
				prtintmsk, 1);
		USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk,
				disconnintmsk, 1);
		/*
		 * 2. Program the USBC_HCFG register to select full-speed host
		 *    or high-speed host.
		 */
		{
			union cvmx_usbcx_hcfg usbcx_hcfg;
			usbcx_hcfg.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCFG(usb->index));
			usbcx_hcfg.s.fslssupp = 0;
			usbcx_hcfg.s.fslspclksel = 0;
			__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCFG(usb->index), usbcx_hcfg.u32);
		}
		/*
		 * 3. Program the port power bit to drive VBUS on the USB,
		 *    USBC_HPRT[PRTPWR] = 1
		 */
		USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt, prtpwr, 1);

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
 * @state: USB device state populated by
 *	   cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_shutdown(struct cvmx_usb_state *state)
{
	union cvmx_usbnx_clk_ctl usbn_clk_ctl;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	/* Make sure all pipes are closed */
	if (usb->idle_pipes.head ||
		usb->active_pipes[CVMX_USB_TRANSFER_ISOCHRONOUS].head ||
		usb->active_pipes[CVMX_USB_TRANSFER_INTERRUPT].head ||
		usb->active_pipes[CVMX_USB_TRANSFER_CONTROL].head ||
		usb->active_pipes[CVMX_USB_TRANSFER_BULK].head)
		return -EBUSY;

	/* Disable the clocks and put them in power on reset */
	usbn_clk_ctl.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index));
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
 * @state: USB device state populated by
 *	   cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_enable(struct cvmx_usb_state *state)
{
	union cvmx_usbcx_ghwcfg3 usbcx_ghwcfg3;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));

	/*
	 * If the port is already enabled the just return. We don't need to do
	 * anything
	 */
	if (usb->usbcx_hprt.s.prtena)
		return 0;

	/* If there is nothing plugged into the port then fail immediately */
	if (!usb->usbcx_hprt.s.prtconnsts) {
		return -ETIMEDOUT;
	}

	/* Program the port reset bit to start the reset process */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt, prtrst, 1);

	/*
	 * Wait at least 50ms (high speed), or 10ms (full speed) for the reset
	 * process to complete.
	 */
	mdelay(50);

	/* Program the port reset bit to 0, USBC_HPRT[PRTRST] = 0 */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt, prtrst, 0);

	/* Wait for the USBC_HPRT[PRTENA]. */
	if (CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt,
				  prtena, ==, 1, 100000))
		return -ETIMEDOUT;

	/* Read the port speed field to get the enumerated speed, USBC_HPRT[PRTSPD]. */
	usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
	usbcx_ghwcfg3.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GHWCFG3(usb->index));

	/*
	 * 13. Program the USBC_GRXFSIZ register to select the size of the
	 *     receive FIFO (25%).
	 */
	USB_SET_FIELD32(CVMX_USBCX_GRXFSIZ(usb->index), union cvmx_usbcx_grxfsiz,
			rxfdep, usbcx_ghwcfg3.s.dfifodepth / 4);
	/*
	 * 14. Program the USBC_GNPTXFSIZ register to select the size and the
	 *     start address of the non- periodic transmit FIFO for nonperiodic
	 *     transactions (50%).
	 */
	{
		union cvmx_usbcx_gnptxfsiz siz;
		siz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index));
		siz.s.nptxfdep = usbcx_ghwcfg3.s.dfifodepth / 2;
		siz.s.nptxfstaddr = usbcx_ghwcfg3.s.dfifodepth / 4;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index), siz.u32);
	}
	/*
	 * 15. Program the USBC_HPTXFSIZ register to select the size and start
	 *     address of the periodic transmit FIFO for periodic transactions
	 *     (25%).
	 */
	{
		union cvmx_usbcx_hptxfsiz siz;
		siz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPTXFSIZ(usb->index));
		siz.s.ptxfsize = usbcx_ghwcfg3.s.dfifodepth / 4;
		siz.s.ptxfstaddr = 3 * usbcx_ghwcfg3.s.dfifodepth / 4;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HPTXFSIZ(usb->index), siz.u32);
	}
	/* Flush all FIFOs */
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), union cvmx_usbcx_grstctl, txfnum, 0x10);
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), union cvmx_usbcx_grstctl, txfflsh, 1);
	CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), union cvmx_usbcx_grstctl,
			      txfflsh, ==, 0, 100);
	USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), union cvmx_usbcx_grstctl, rxfflsh, 1);
	CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), union cvmx_usbcx_grstctl,
			      rxfflsh, ==, 0, 100);

	return 0;
}


/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 * Transactions in process will fail and call their
 * associated callbacks.
 *
 * @state: USB device state populated by
 *	   cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_disable(struct cvmx_usb_state *state)
{
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	/* Disable the port */
	USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), union cvmx_usbcx_hprt, prtena, 1);
	return 0;
}


/**
 * Get the current state of the USB port. Use this call to
 * determine if the usb port has anything connected, is enabled,
 * or has some sort of error condition. The return value of this
 * call has "changed" bits to signal of the value of some fields
 * have changed between calls. These "changed" fields are based
 * on the last call to cvmx_usb_set_status(). In order to clear
 * them, you must update the status through cvmx_usb_set_status().
 *
 * @state: USB device state populated by
 *	   cvmx_usb_initialize().
 *
 * Returns: Port status information
 */
struct cvmx_usb_port_status cvmx_usb_get_status(struct cvmx_usb_state *state)
{
	union cvmx_usbcx_hprt usbc_hprt;
	struct cvmx_usb_port_status result;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	memset(&result, 0, sizeof(result));

	usbc_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
	result.port_enabled = usbc_hprt.s.prtena;
	result.port_over_current = usbc_hprt.s.prtovrcurract;
	result.port_powered = usbc_hprt.s.prtpwr;
	result.port_speed = usbc_hprt.s.prtspd;
	result.connected = usbc_hprt.s.prtconnsts;
	result.connect_change = (result.connected != usb->port_status.connected);

	return result;
}


/**
 * Set the current state of the USB port. The status is used as
 * a reference for the "changed" bits returned by
 * cvmx_usb_get_status(). Other than serving as a reference, the
 * status passed to this function is not used. No fields can be
 * changed through this call.
 *
 * @state:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @port_status:
 *		 Port status to set, most like returned by cvmx_usb_get_status()
 */
void cvmx_usb_set_status(struct cvmx_usb_state *state, struct cvmx_usb_port_status port_status)
{
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	usb->port_status = port_status;
	return;
}


/**
 * Convert a USB transaction into a handle
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @transaction:
 *		 Transaction to get handle for
 *
 * Returns: Handle
 */
static inline int __cvmx_usb_get_submit_handle(struct cvmx_usb_internal_state *usb,
					       struct cvmx_usb_transaction *transaction)
{
	return ((unsigned long)transaction - (unsigned long)usb->transaction) /
			sizeof(*transaction);
}


/**
 * Convert a USB pipe into a handle
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe:	 Pipe to get handle for
 *
 * Returns: Handle
 */
static inline int __cvmx_usb_get_pipe_handle(struct cvmx_usb_internal_state *usb,
					     struct cvmx_usb_pipe *pipe)
{
	return ((unsigned long)pipe - (unsigned long)usb->pipe) / sizeof(*pipe);
}


/**
 * Open a virtual pipe between the host and a USB device. A pipe
 * must be opened before data can be transferred between a device
 * and Octeon.
 *
 * @state:	     USB device state populated by
 *		     cvmx_usb_initialize().
 * @flags:	     Optional pipe flags defined in
 *		     enum cvmx_usb_pipe_flags.
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
 * Returns: A non negative value is a pipe handle. Negative
 *	    values are error codes.
 */
int cvmx_usb_open_pipe(struct cvmx_usb_state *state, enum cvmx_usb_pipe_flags flags,
		       int device_addr, int endpoint_num,
		       enum cvmx_usb_speed device_speed, int max_packet,
		       enum cvmx_usb_transfer transfer_type,
		       enum cvmx_usb_direction transfer_dir, int interval,
		       int multi_count, int hub_device_addr, int hub_port)
{
	struct cvmx_usb_pipe *pipe;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	if (unlikely((device_addr < 0) || (device_addr > MAX_USB_ADDRESS)))
		return -EINVAL;
	if (unlikely((endpoint_num < 0) || (endpoint_num > MAX_USB_ENDPOINT)))
		return -EINVAL;
	if (unlikely(device_speed > CVMX_USB_SPEED_LOW))
		return -EINVAL;
	if (unlikely((max_packet <= 0) || (max_packet > 1024)))
		return -EINVAL;
	if (unlikely(transfer_type > CVMX_USB_TRANSFER_INTERRUPT))
		return -EINVAL;
	if (unlikely((transfer_dir != CVMX_USB_DIRECTION_OUT) &&
		(transfer_dir != CVMX_USB_DIRECTION_IN)))
		return -EINVAL;
	if (unlikely(interval < 0))
		return -EINVAL;
	if (unlikely((transfer_type == CVMX_USB_TRANSFER_CONTROL) && interval))
		return -EINVAL;
	if (unlikely(multi_count < 0))
		return -EINVAL;
	if (unlikely((device_speed != CVMX_USB_SPEED_HIGH) &&
		(multi_count != 0)))
		return -EINVAL;
	if (unlikely((hub_device_addr < 0) || (hub_device_addr > MAX_USB_ADDRESS)))
		return -EINVAL;
	if (unlikely((hub_port < 0) || (hub_port > MAX_USB_HUB_PORT)))
		return -EINVAL;

	/* Find a free pipe */
	pipe = usb->free_pipes.head;
	if (!pipe)
		return -ENOMEM;
	__cvmx_usb_remove_pipe(&usb->free_pipes, pipe);
	pipe->flags = flags | __CVMX_USB_PIPE_FLAGS_OPEN;
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
	/*
	 * All pipes use interval to rate limit NAK processing. Force an
	 * interval if one wasn't supplied
	 */
	if (!interval)
		interval = 1;
	if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
		pipe->interval = interval*8;
		/* Force start splits to be schedule on uFrame 0 */
		pipe->next_tx_frame = ((usb->frame_number+7)&~7) + pipe->interval;
	} else {
		pipe->interval = interval;
		pipe->next_tx_frame = usb->frame_number + pipe->interval;
	}
	pipe->multi_count = multi_count;
	pipe->hub_device_addr = hub_device_addr;
	pipe->hub_port = hub_port;
	pipe->pid_toggle = 0;
	pipe->split_sc_frame = -1;
	__cvmx_usb_append_pipe(&usb->idle_pipes, pipe);

	/*
	 * We don't need to tell the hardware about this pipe yet since
	 * it doesn't have any submitted requests
	 */

	return __cvmx_usb_get_pipe_handle(usb, pipe);
}


/**
 * Poll the RX FIFOs and remove data as needed. This function is only used
 * in non DMA mode. It is very important that this function be called quickly
 * enough to prevent FIFO overflow.
 *
 * @usb:	USB device state populated by
 *		cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_rx_fifo(struct cvmx_usb_internal_state *usb)
{
	union cvmx_usbcx_grxstsph rx_status;
	int channel;
	int bytes;
	uint64_t address;
	uint32_t *ptr;

	rx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GRXSTSPH(usb->index));
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
	address = __cvmx_usb_read_csr64(usb, CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8);
	ptr = cvmx_phys_to_ptr(address);
	__cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8, address + bytes);

	/* Loop writing the FIFO data for this packet into memory */
	while (bytes > 0) {
		*ptr++ = __cvmx_usb_read_csr32(usb, USB_FIFO_ADDRESS(channel, usb->index));
		bytes -= 4;
	}
	CVMX_SYNCW;

	return;
}


/**
 * Fill the TX hardware fifo with data out of the software
 * fifos
 *
 * @usb:	    USB device state populated by
 *		    cvmx_usb_initialize().
 * @fifo:	    Software fifo to use
 * @available:	    Amount of space in the hardware fifo
 *
 * Returns: Non zero if the hardware fifo was too small and needs
 *	    to be serviced again.
 */
static int __cvmx_usb_fill_tx_hw(struct cvmx_usb_internal_state *usb, struct cvmx_usb_tx_fifo *fifo, int available)
{
	/*
	 * We're done either when there isn't anymore space or the software FIFO
	 * is empty
	 */
	while (available && (fifo->head != fifo->tail)) {
		int i = fifo->tail;
		const uint32_t *ptr = cvmx_phys_to_ptr(fifo->entry[i].address);
		uint64_t csr_address = USB_FIFO_ADDRESS(fifo->entry[i].channel, usb->index) ^ 4;
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
			cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
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
 * @usb:	USB device state populated by
 *		cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_tx_fifo(struct cvmx_usb_internal_state *usb)
{
	if (usb->periodic.head != usb->periodic.tail) {
		union cvmx_usbcx_hptxsts tx_status;
		tx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPTXSTS(usb->index));
		if (__cvmx_usb_fill_tx_hw(usb, &usb->periodic, tx_status.s.ptxfspcavail))
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk, ptxfempmsk, 1);
		else
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk, ptxfempmsk, 0);
	}

	if (usb->nonperiodic.head != usb->nonperiodic.tail) {
		union cvmx_usbcx_gnptxsts tx_status;
		tx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GNPTXSTS(usb->index));
		if (__cvmx_usb_fill_tx_hw(usb, &usb->nonperiodic, tx_status.s.nptxfspcavail))
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk, nptxfempmsk, 1);
		else
			USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk, nptxfempmsk, 0);
	}

	return;
}


/**
 * Fill the TX FIFO with an outgoing packet
 *
 * @usb:	  USB device state populated by
 *		  cvmx_usb_initialize().
 * @channel:	  Channel number to get packet from
 */
static void __cvmx_usb_fill_tx_fifo(struct cvmx_usb_internal_state *usb, int channel)
{
	union cvmx_usbcx_hccharx hcchar;
	union cvmx_usbcx_hcspltx usbc_hcsplt;
	union cvmx_usbcx_hctsizx usbc_hctsiz;
	struct cvmx_usb_tx_fifo *fifo;

	/* We only need to fill data on outbound channels */
	hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));
	if (hcchar.s.epdir != CVMX_USB_DIRECTION_OUT)
		return;

	/* OUT Splits only have data on the start and not the complete */
	usbc_hcsplt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCSPLTX(channel, usb->index));
	if (usbc_hcsplt.s.spltena && usbc_hcsplt.s.compsplt)
		return;

	/* Find out how many bytes we need to fill and convert it into 32bit words */
	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));
	if (!usbc_hctsiz.s.xfersize)
		return;

	if ((hcchar.s.eptype == CVMX_USB_TRANSFER_INTERRUPT) ||
		(hcchar.s.eptype == CVMX_USB_TRANSFER_ISOCHRONOUS))
		fifo = &usb->periodic;
	else
		fifo = &usb->nonperiodic;

	fifo->entry[fifo->head].channel = channel;
	fifo->entry[fifo->head].address = __cvmx_usb_read_csr64(usb, CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8);
	fifo->entry[fifo->head].size = (usbc_hctsiz.s.xfersize+3)>>2;
	fifo->head++;
	if (fifo->head > MAX_CHANNELS)
		fifo->head = 0;

	__cvmx_usb_poll_tx_fifo(usb);

	return;
}

/**
 * Perform channel specific setup for Control transactions. All
 * the generic stuff will already have been done in
 * __cvmx_usb_start_channel()
 *
 * @usb:	  USB device state populated by
 *		  cvmx_usb_initialize().
 * @channel:	  Channel to setup
 * @pipe:	  Pipe for control transaction
 */
static void __cvmx_usb_start_channel_control(struct cvmx_usb_internal_state *usb,
					     int channel,
					     struct cvmx_usb_pipe *pipe)
{
	struct cvmx_usb_transaction *transaction = pipe->head;
	union cvmx_usb_control_header *header =
		cvmx_phys_to_ptr(transaction->control_header);
	int bytes_to_transfer = transaction->buffer_length - transaction->actual_bytes;
	int packets_to_transfer;
	union cvmx_usbcx_hctsizx usbc_hctsiz;

	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));

	switch (transaction->stage) {
	case CVMX_USB_STAGE_NON_CONTROL:
	case CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE:
		cvmx_dprintf("%s: ERROR - Non control stage\n", __FUNCTION__);
		break;
	case CVMX_USB_STAGE_SETUP:
		usbc_hctsiz.s.pid = 3; /* Setup */
		bytes_to_transfer = sizeof(*header);
		/* All Control operations start with a setup going OUT */
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), union cvmx_usbcx_hccharx, epdir, CVMX_USB_DIRECTION_OUT);
		/*
		 * Setup send the control header instead of the buffer data. The
		 * buffer data will be used in the next stage
		 */
		__cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8, transaction->control_header);
		break;
	case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = 3; /* Setup */
		bytes_to_transfer = 0;
		/* All Control operations start with a setup going OUT */
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), union cvmx_usbcx_hccharx, epdir, CVMX_USB_DIRECTION_OUT);
		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), union cvmx_usbcx_hcspltx, compsplt, 1);
		break;
	case CVMX_USB_STAGE_DATA:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
			if (header->s.request_type & 0x80)
				bytes_to_transfer = 0;
			else if (bytes_to_transfer > pipe->max_packet)
				bytes_to_transfer = pipe->max_packet;
		}
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->s.request_type & 0x80) ?
					CVMX_USB_DIRECTION_IN :
					CVMX_USB_DIRECTION_OUT));
		break;
	case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		if (!(header->s.request_type & 0x80))
			bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
				union cvmx_usbcx_hccharx, epdir,
				((header->s.request_type & 0x80) ?
					CVMX_USB_DIRECTION_IN :
					CVMX_USB_DIRECTION_OUT));
		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), union cvmx_usbcx_hcspltx, compsplt, 1);
		break;
	case CVMX_USB_STAGE_STATUS:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), union cvmx_usbcx_hccharx, epdir,
				((header->s.request_type & 0x80) ?
					CVMX_USB_DIRECTION_OUT :
					CVMX_USB_DIRECTION_IN));
		break;
	case CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE:
		usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
		bytes_to_transfer = 0;
		USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), union cvmx_usbcx_hccharx, epdir,
				((header->s.request_type & 0x80) ?
					CVMX_USB_DIRECTION_OUT :
					CVMX_USB_DIRECTION_IN));
		USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), union cvmx_usbcx_hcspltx, compsplt, 1);
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
	packets_to_transfer = (bytes_to_transfer + pipe->max_packet - 1) / pipe->max_packet;
	if (packets_to_transfer == 0)
		packets_to_transfer = 1;
	else if ((packets_to_transfer > 1) && (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)) {
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

	__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index), usbc_hctsiz.u32);
	return;
}


/**
 * Start a channel to perform the pipe's head transaction
 *
 * @usb:	  USB device state populated by
 *		  cvmx_usb_initialize().
 * @channel:	  Channel to setup
 * @pipe:	  Pipe to start
 */
static void __cvmx_usb_start_channel(struct cvmx_usb_internal_state *usb,
				     int channel,
				     struct cvmx_usb_pipe *pipe)
{
	struct cvmx_usb_transaction *transaction = pipe->head;

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
		usbc_hcint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index));
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index), usbc_hcint.u32);

		usbc_hcintmsk.u32 = 0;
		usbc_hcintmsk.s.chhltdmsk = 1;
		if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
			/* Channels need these extra interrupts when we aren't in DMA mode */
			usbc_hcintmsk.s.datatglerrmsk = 1;
			usbc_hcintmsk.s.frmovrunmsk = 1;
			usbc_hcintmsk.s.bblerrmsk = 1;
			usbc_hcintmsk.s.xacterrmsk = 1;
			if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
				/* Splits don't generate xfercompl, so we need ACK and NYET */
				usbc_hcintmsk.s.nyetmsk = 1;
				usbc_hcintmsk.s.ackmsk = 1;
			}
			usbc_hcintmsk.s.nakmsk = 1;
			usbc_hcintmsk.s.stallmsk = 1;
			usbc_hcintmsk.s.xfercomplmsk = 1;
		}
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), usbc_hcintmsk.u32);

		/* Enable the channel interrupt to propagate */
		usbc_haintmsk.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HAINTMSK(usb->index));
		usbc_haintmsk.s.haintmsk |= 1<<channel;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HAINTMSK(usb->index), usbc_haintmsk.u32);
	}

	/* Setup the locations the DMA engines use  */
	{
		uint64_t dma_address = transaction->buffer + transaction->actual_bytes;
		if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
			dma_address = transaction->buffer + transaction->iso_packets[0].offset + transaction->actual_bytes;
		__cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8, dma_address);
		__cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8, dma_address);
	}

	/* Setup both the size of the transfer and the SPLIT characteristics */
	{
		union cvmx_usbcx_hcspltx usbc_hcsplt = {.u32 = 0};
		union cvmx_usbcx_hctsizx usbc_hctsiz = {.u32 = 0};
		int packets_to_transfer;
		int bytes_to_transfer = transaction->buffer_length - transaction->actual_bytes;

		/*
		 * ISOCHRONOUS transactions store each individual transfer size
		 * in the packet structure, not the global buffer_length
		 */
		if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
			bytes_to_transfer = transaction->iso_packets[0].length - transaction->actual_bytes;

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
					pipe->split_sc_frame = (usb->frame_number + 1) & 0x7f;
				else
					pipe->split_sc_frame = (usb->frame_number + 2) & 0x7f;
			} else
				pipe->split_sc_frame = -1;

			usbc_hcsplt.s.spltena = 1;
			usbc_hcsplt.s.hubaddr = pipe->hub_device_addr;
			usbc_hcsplt.s.prtaddr = pipe->hub_port;
			usbc_hcsplt.s.compsplt = (transaction->stage == CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE);

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
				(pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) &&
				(pipe->transfer_type == CVMX_USB_TRANSFER_ISOCHRONOUS)) {
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
						usbc_hcsplt.s.xactpos = 3; /* Entire payload in one go */
					else
						usbc_hcsplt.s.xactpos = 2; /* First part of payload */
				} else {
					/*
					 * Continuing the previous data, we must
					 * either be in the middle or at the end
					 */
					if (bytes_to_transfer <= 188)
						usbc_hcsplt.s.xactpos = 1; /* End of payload */
					else
						usbc_hcsplt.s.xactpos = 0; /* Middle of payload */
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
			bytes_to_transfer = MAX_TRANSFER_BYTES / pipe->max_packet;
			bytes_to_transfer *= pipe->max_packet;
		}

		/*
		 * Calculate the number of packets to transfer. If the length is
		 * zero we still need to transfer one packet
		 */
		packets_to_transfer = (bytes_to_transfer + pipe->max_packet - 1) / pipe->max_packet;
		if (packets_to_transfer == 0)
			packets_to_transfer = 1;
		else if ((packets_to_transfer > 1) && (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)) {
			/*
			 * Limit to one packet when not using DMA. Channels must
			 * be restarted between every packet for IN
			 * transactions, so there is no reason to do multiple
			 * packets in a row
			 */
			packets_to_transfer = 1;
			bytes_to_transfer = packets_to_transfer * pipe->max_packet;
		} else if (packets_to_transfer > MAX_TRANSFER_PACKETS) {
			/*
			 * Limit the number of packet and data transferred to
			 * what the hardware can handle
			 */
			packets_to_transfer = MAX_TRANSFER_PACKETS;
			bytes_to_transfer = packets_to_transfer * pipe->max_packet;
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

		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCSPLTX(channel, usb->index), usbc_hcsplt.u32);
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index), usbc_hctsiz.u32);
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
		usbc_hcchar.s.lspddev = (pipe->device_speed == CVMX_USB_SPEED_LOW);
		usbc_hcchar.s.epdir = pipe->transfer_dir;
		usbc_hcchar.s.epnum = pipe->endpoint_num;
		usbc_hcchar.s.mps = pipe->max_packet;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index), usbc_hcchar.u32);
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
					USB_SET_FIELD32(CVMX_USBCX_HCTSIZX(channel, usb->index), union cvmx_usbcx_hctsizx, pid, 0);
				else /* Need MDATA */
					USB_SET_FIELD32(CVMX_USBCX_HCTSIZX(channel, usb->index), union cvmx_usbcx_hctsizx, pid, 3);
			}
		}
		break;
	}
	{
		union cvmx_usbcx_hctsizx usbc_hctsiz = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index))};
		transaction->xfersize = usbc_hctsiz.s.xfersize;
		transaction->pktcnt = usbc_hctsiz.s.pktcnt;
	}
	/* Remeber when we start a split transaction */
	if (__cvmx_usb_pipe_needs_split(usb, pipe))
		usb->active_split = transaction;
	USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), union cvmx_usbcx_hccharx, chena, 1);
	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
		__cvmx_usb_fill_tx_fifo(usb, channel);
	return;
}


/**
 * Find a pipe that is ready to be scheduled to hardware.
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @list:	 Pipe list to search
 * @current_frame:
 *		 Frame counter to use as a time reference.
 *
 * Returns: Pipe or NULL if none are ready
 */
static struct cvmx_usb_pipe *__cvmx_usb_find_ready_pipe(struct cvmx_usb_internal_state *usb, struct cvmx_usb_pipe_list *list, uint64_t current_frame)
{
	struct cvmx_usb_pipe *pipe = list->head;
	while (pipe) {
		if (!(pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED) && pipe->head &&
			(pipe->next_tx_frame <= current_frame) &&
			((pipe->split_sc_frame == -1) || ((((int)current_frame - (int)pipe->split_sc_frame) & 0x7f) < 0x40)) &&
			(!usb->active_split || (usb->active_split == pipe->head))) {
			CVMX_PREFETCH(pipe, 128);
			CVMX_PREFETCH(pipe->head, 0);
			return pipe;
		}
		pipe = pipe->next;
	}
	return NULL;
}


/**
 * Called whenever a pipe might need to be scheduled to the
 * hardware.
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @is_sof:	 True if this schedule was called on a SOF interrupt.
 */
static void __cvmx_usb_schedule(struct cvmx_usb_internal_state *usb, int is_sof)
{
	int channel;
	struct cvmx_usb_pipe *pipe;
	int need_sof;
	enum cvmx_usb_transfer ttype;

	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
		/* Without DMA we need to be careful to not schedule something at the end of a frame and cause an overrun */
		union cvmx_usbcx_hfnum hfnum = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFNUM(usb->index))};
		union cvmx_usbcx_hfir hfir = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFIR(usb->index))};
		if (hfnum.s.frrem < hfir.s.frint/4)
			goto done;
	}

	while (usb->idle_hardware_channels) {
		/* Find an idle channel */
		CVMX_CLZ(channel, usb->idle_hardware_channels);
		channel = 31 - channel;
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
			pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_ISOCHRONOUS, usb->frame_number);
			if (likely(!pipe))
				pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_INTERRUPT, usb->frame_number);
		}
		if (likely(!pipe)) {
			pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_CONTROL, usb->frame_number);
			if (likely(!pipe))
				pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_BULK, usb->frame_number);
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
	for (ttype = CVMX_USB_TRANSFER_CONTROL; ttype <= CVMX_USB_TRANSFER_INTERRUPT; ttype++) {
		pipe = usb->active_pipes[ttype].head;
		while (pipe) {
			if (pipe->next_tx_frame > usb->frame_number) {
				need_sof = 1;
				break;
			}
			pipe = pipe->next;
		}
	}
	USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), union cvmx_usbcx_gintmsk, sofmsk, need_sof);
	return;
}


/**
 * Call a user's callback for a specific reason.
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe:	 Pipe the callback is for or NULL
 * @transaction:
 *		 Transaction the callback is for or NULL
 * @reason:	 Reason this callback is being called
 * @complete_code:
 *		 Completion code for the transaction, if any
 */
static void __cvmx_usb_perform_callback(struct cvmx_usb_internal_state *usb,
					struct cvmx_usb_pipe *pipe,
					struct cvmx_usb_transaction *transaction,
					enum cvmx_usb_callback reason,
					enum cvmx_usb_complete complete_code)
{
	cvmx_usb_callback_func_t callback = usb->callback[reason];
	void *user_data = usb->callback_data[reason];
	int submit_handle = -1;
	int pipe_handle = -1;
	int bytes_transferred = 0;

	if (pipe)
		pipe_handle = __cvmx_usb_get_pipe_handle(usb, pipe);

	if (transaction) {
		submit_handle = __cvmx_usb_get_submit_handle(usb, transaction);
		bytes_transferred = transaction->actual_bytes;
		/* Transactions are allowed to override the default callback */
		if ((reason == CVMX_USB_CALLBACK_TRANSFER_COMPLETE) && transaction->callback) {
			callback = transaction->callback;
			user_data = transaction->callback_data;
		}
	}

	if (!callback)
		return;

	callback((struct cvmx_usb_state *)usb, reason, complete_code, pipe_handle, submit_handle,
		 bytes_transferred, user_data);
}


/**
 * Signal the completion of a transaction and free it. The
 * transaction will be removed from the pipe transaction list.
 *
 * @usb:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe:	 Pipe the transaction is on
 * @transaction:
 *		 Transaction that completed
 * @complete_code:
 *		 Completion code
 */
static void __cvmx_usb_perform_complete(struct cvmx_usb_internal_state *usb,
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
		if ((transaction->iso_number_packets > 1) && (complete_code == CVMX_USB_COMPLETE_SUCCESS)) {
			transaction->actual_bytes = 0;	   /* No bytes transferred for this packet as of yet */
			transaction->iso_number_packets--; /* One less ISO waiting to transfer */
			transaction->iso_packets++;	   /* Increment to the next location in our packet array */
			transaction->stage = CVMX_USB_STAGE_NON_CONTROL;
			goto done;
		}
	}

	/* Remove the transaction from the pipe list */
	if (transaction->next)
		transaction->next->prev = transaction->prev;
	else
		pipe->tail = transaction->prev;
	if (transaction->prev)
		transaction->prev->next = transaction->next;
	else
		pipe->head = transaction->next;
	if (!pipe->head) {
		__cvmx_usb_remove_pipe(usb->active_pipes + pipe->transfer_type, pipe);
		__cvmx_usb_append_pipe(&usb->idle_pipes, pipe);

	}
	__cvmx_usb_perform_callback(usb, pipe, transaction,
				    CVMX_USB_CALLBACK_TRANSFER_COMPLETE,
				    complete_code);
	__cvmx_usb_free_transaction(usb, transaction);
done:
	return;
}


/**
 * Submit a usb transaction to a pipe. Called for all types
 * of transactions.
 *
 * @usb:
 * @pipe_handle:
 *		    Which pipe to submit to. Will be validated in this function.
 * @type:	    Transaction type
 * @flags:	    Flags for the transaction
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
 * @callback:	    User callback to call when the transaction completes
 * @user_data:	    User's data for the callback
 *
 * Returns: Submit handle or negative on failure. Matches the result
 *	    in the external API.
 */
static int __cvmx_usb_submit_transaction(struct cvmx_usb_internal_state *usb,
					 int pipe_handle,
					 enum cvmx_usb_transfer type,
					 int flags,
					 uint64_t buffer,
					 int buffer_length,
					 uint64_t control_header,
					 int iso_start_frame,
					 int iso_number_packets,
					 struct cvmx_usb_iso_packet *iso_packets,
					 cvmx_usb_callback_func_t callback,
					 void *user_data)
{
	int submit_handle;
	struct cvmx_usb_transaction *transaction;
	struct cvmx_usb_pipe *pipe = usb->pipe + pipe_handle;

	if (unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
		return -EINVAL;
	/* Fail if the pipe isn't open */
	if (unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
		return -EINVAL;
	if (unlikely(pipe->transfer_type != type))
		return -EINVAL;

	transaction = __cvmx_usb_alloc_transaction(usb);
	if (unlikely(!transaction))
		return -ENOMEM;

	transaction->type = type;
	transaction->flags |= flags;
	transaction->buffer = buffer;
	transaction->buffer_length = buffer_length;
	transaction->control_header = control_header;
	transaction->iso_start_frame = iso_start_frame; // FIXME: This is not used, implement it
	transaction->iso_number_packets = iso_number_packets;
	transaction->iso_packets = iso_packets;
	transaction->callback = callback;
	transaction->callback_data = user_data;
	if (transaction->type == CVMX_USB_TRANSFER_CONTROL)
		transaction->stage = CVMX_USB_STAGE_SETUP;
	else
		transaction->stage = CVMX_USB_STAGE_NON_CONTROL;

	transaction->next = NULL;
	if (pipe->tail) {
		transaction->prev = pipe->tail;
		transaction->prev->next = transaction;
	} else {
		if (pipe->next_tx_frame < usb->frame_number)
			pipe->next_tx_frame = usb->frame_number + pipe->interval -
				(usb->frame_number - pipe->next_tx_frame) % pipe->interval;
		transaction->prev = NULL;
		pipe->head = transaction;
		__cvmx_usb_remove_pipe(&usb->idle_pipes, pipe);
		__cvmx_usb_append_pipe(usb->active_pipes + pipe->transfer_type, pipe);
	}
	pipe->tail = transaction;

	submit_handle = __cvmx_usb_get_submit_handle(usb, transaction);

	/* We may need to schedule the pipe if this was the head of the pipe */
	if (!transaction->prev)
		__cvmx_usb_schedule(usb, 0);

	return submit_handle;
}


/**
 * Call to submit a USB Bulk transfer to a pipe.
 *
 * @state:	    USB device state populated by
 *		    cvmx_usb_initialize().
 * @pipe_handle:
 *		    Handle to the pipe for the transfer.
 * @buffer:	    Physical address of the data buffer in
 *		    memory. Note that this is NOT A POINTER, but
 *		    the full 64bit physical address of the
 *		    buffer. This may be zero if buffer_length is
 *		    zero.
 * @buffer_length:
 *		    Length of buffer in bytes.
 * @callback:	    Function to call when this transaction
 *		    completes. If the return value of this
 *		    function isn't an error, then this function
 *		    is guaranteed to be called when the
 *		    transaction completes. If this parameter is
 *		    NULL, then the generic callback registered
 *		    through cvmx_usb_register_callback is
 *		    called. If both are NULL, then there is no
 *		    way to know when a transaction completes.
 * @user_data:	    User supplied data returned when the
 *		    callback is called. This is only used if
 *		    callback in not NULL.
 *
 * Returns: A submitted transaction handle or negative on
 *	    failure. Negative values are error codes.
 */
int cvmx_usb_submit_bulk(struct cvmx_usb_state *state, int pipe_handle,
			 uint64_t buffer, int buffer_length,
			 cvmx_usb_callback_func_t callback,
			 void *user_data)
{
	int submit_handle;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	/* Pipe handle checking is done later in a common place */
	if (unlikely(!buffer))
		return -EINVAL;
	if (unlikely(buffer_length < 0))
		return -EINVAL;

	submit_handle = __cvmx_usb_submit_transaction(usb, pipe_handle,
						      CVMX_USB_TRANSFER_BULK,
						      0, /* flags */
						      buffer,
						      buffer_length,
						      0, /* control_header */
						      0, /* iso_start_frame */
						      0, /* iso_number_packets */
						      NULL, /* iso_packets */
						      callback,
						      user_data);
	return submit_handle;
}


/**
 * Call to submit a USB Interrupt transfer to a pipe.
 *
 * @state:	    USB device state populated by
 *		    cvmx_usb_initialize().
 * @pipe_handle:
 *		    Handle to the pipe for the transfer.
 * @buffer:	    Physical address of the data buffer in
 *		    memory. Note that this is NOT A POINTER, but
 *		    the full 64bit physical address of the
 *		    buffer. This may be zero if buffer_length is
 *		    zero.
 * @buffer_length:
 *		    Length of buffer in bytes.
 * @callback:	    Function to call when this transaction
 *		    completes. If the return value of this
 *		    function isn't an error, then this function
 *		    is guaranteed to be called when the
 *		    transaction completes. If this parameter is
 *		    NULL, then the generic callback registered
 *		    through cvmx_usb_register_callback is
 *		    called. If both are NULL, then there is no
 *		    way to know when a transaction completes.
 * @user_data:	    User supplied data returned when the
 *		    callback is called. This is only used if
 *		    callback in not NULL.
 *
 * Returns: A submitted transaction handle or negative on
 *	    failure. Negative values are error codes.
 */
int cvmx_usb_submit_interrupt(struct cvmx_usb_state *state, int pipe_handle,
			      uint64_t buffer, int buffer_length,
			      cvmx_usb_callback_func_t callback,
			      void *user_data)
{
	int submit_handle;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	/* Pipe handle checking is done later in a common place */
	if (unlikely(!buffer))
		return -EINVAL;
	if (unlikely(buffer_length < 0))
		return -EINVAL;

	submit_handle = __cvmx_usb_submit_transaction(usb, pipe_handle,
						      CVMX_USB_TRANSFER_INTERRUPT,
						      0, /* flags */
						      buffer,
						      buffer_length,
						      0, /* control_header */
						      0, /* iso_start_frame */
						      0, /* iso_number_packets */
						      NULL, /* iso_packets */
						      callback,
						      user_data);
	return submit_handle;
}


/**
 * Call to submit a USB Control transfer to a pipe.
 *
 * @state:	    USB device state populated by
 *		    cvmx_usb_initialize().
 * @pipe_handle:
 *		    Handle to the pipe for the transfer.
 * @control_header:
 *		    USB 8 byte control header physical address.
 *		    Note that this is NOT A POINTER, but the
 *		    full 64bit physical address of the buffer.
 * @buffer:	    Physical address of the data buffer in
 *		    memory. Note that this is NOT A POINTER, but
 *		    the full 64bit physical address of the
 *		    buffer. This may be zero if buffer_length is
 *		    zero.
 * @buffer_length:
 *		    Length of buffer in bytes.
 * @callback:	    Function to call when this transaction
 *		    completes. If the return value of this
 *		    function isn't an error, then this function
 *		    is guaranteed to be called when the
 *		    transaction completes. If this parameter is
 *		    NULL, then the generic callback registered
 *		    through cvmx_usb_register_callback is
 *		    called. If both are NULL, then there is no
 *		    way to know when a transaction completes.
 * @user_data:	    User supplied data returned when the
 *		    callback is called. This is only used if
 *		    callback in not NULL.
 *
 * Returns: A submitted transaction handle or negative on
 *	    failure. Negative values are error codes.
 */
int cvmx_usb_submit_control(struct cvmx_usb_state *state, int pipe_handle,
			    uint64_t control_header,
			    uint64_t buffer, int buffer_length,
			    cvmx_usb_callback_func_t callback,
			    void *user_data)
{
	int submit_handle;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	union cvmx_usb_control_header *header =
		cvmx_phys_to_ptr(control_header);

	/* Pipe handle checking is done later in a common place */
	if (unlikely(!control_header))
		return -EINVAL;
	/* Some drivers send a buffer with a zero length. God only knows why */
	if (unlikely(buffer && (buffer_length < 0)))
		return -EINVAL;
	if (unlikely(!buffer && (buffer_length != 0)))
		return -EINVAL;
	if ((header->s.request_type & 0x80) == 0)
		buffer_length = le16_to_cpu(header->s.length);

	submit_handle = __cvmx_usb_submit_transaction(usb, pipe_handle,
						      CVMX_USB_TRANSFER_CONTROL,
						      0, /* flags */
						      buffer,
						      buffer_length,
						      control_header,
						      0, /* iso_start_frame */
						      0, /* iso_number_packets */
						      NULL, /* iso_packets */
						      callback,
						      user_data);
	return submit_handle;
}


/**
 * Call to submit a USB Isochronous transfer to a pipe.
 *
 * @state:	    USB device state populated by
 *		    cvmx_usb_initialize().
 * @pipe_handle:
 *		    Handle to the pipe for the transfer.
 * @start_frame:
 *		    Number of frames into the future to schedule
 *		    this transaction.
 * @flags:	    Flags to control the transfer. See
 *		    enum cvmx_usb_isochronous_flags for the flag
 *		    definitions.
 * @number_packets:
 *		    Number of sequential packets to transfer.
 *		    "packets" is a pointer to an array of this
 *		    many packet structures.
 * @packets:	    Description of each transfer packet as
 *		    defined by struct cvmx_usb_iso_packet. The array
 *		    pointed to here must stay valid until the
 *		    complete callback is called.
 * @buffer:	    Physical address of the data buffer in
 *		    memory. Note that this is NOT A POINTER, but
 *		    the full 64bit physical address of the
 *		    buffer. This may be zero if buffer_length is
 *		    zero.
 * @buffer_length:
 *		    Length of buffer in bytes.
 * @callback:	    Function to call when this transaction
 *		    completes. If the return value of this
 *		    function isn't an error, then this function
 *		    is guaranteed to be called when the
 *		    transaction completes. If this parameter is
 *		    NULL, then the generic callback registered
 *		    through cvmx_usb_register_callback is
 *		    called. If both are NULL, then there is no
 *		    way to know when a transaction completes.
 * @user_data:	    User supplied data returned when the
 *		    callback is called. This is only used if
 *		    callback in not NULL.
 *
 * Returns: A submitted transaction handle or negative on
 *	    failure. Negative values are error codes.
 */
int cvmx_usb_submit_isochronous(struct cvmx_usb_state *state, int pipe_handle,
				int start_frame, int flags,
				int number_packets,
				struct cvmx_usb_iso_packet packets[],
				uint64_t buffer, int buffer_length,
				cvmx_usb_callback_func_t callback,
				void *user_data)
{
	int submit_handle;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	/* Pipe handle checking is done later in a common place */
	if (unlikely(start_frame < 0))
		return -EINVAL;
	if (unlikely(flags & ~(CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT | CVMX_USB_ISOCHRONOUS_FLAGS_ASAP)))
		return -EINVAL;
	if (unlikely(number_packets < 1))
		return -EINVAL;
	if (unlikely(!packets))
		return -EINVAL;
	if (unlikely(!buffer))
		return -EINVAL;
	if (unlikely(buffer_length < 0))
		return -EINVAL;

	submit_handle = __cvmx_usb_submit_transaction(usb, pipe_handle,
						      CVMX_USB_TRANSFER_ISOCHRONOUS,
						      flags,
						      buffer,
						      buffer_length,
						      0, /* control_header */
						      start_frame,
						      number_packets,
						      packets,
						      callback,
						      user_data);
	return submit_handle;
}


/**
 * Cancel one outstanding request in a pipe. Canceling a request
 * can fail if the transaction has already completed before cancel
 * is called. Even after a successful cancel call, it may take
 * a frame or two for the cvmx_usb_poll() function to call the
 * associated callback.
 *
 * @state:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe_handle:
 *		 Pipe handle to cancel requests in.
 * @submit_handle:
 *		 Handle to transaction to cancel, returned by the submit function.
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_cancel(struct cvmx_usb_state *state, int pipe_handle, int submit_handle)
{
	struct cvmx_usb_transaction *transaction;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	struct cvmx_usb_pipe *pipe = usb->pipe + pipe_handle;

	if (unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
		return -EINVAL;
	if (unlikely((submit_handle < 0) || (submit_handle >= MAX_TRANSACTIONS)))
		return -EINVAL;

	/* Fail if the pipe isn't open */
	if (unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
		return -EINVAL;

	transaction = usb->transaction + submit_handle;

	/* Fail if this transaction already completed */
	if (unlikely((transaction->flags & __CVMX_USB_TRANSACTION_FLAGS_IN_USE) == 0))
		return -EINVAL;

	/*
	 * If the transaction is the HEAD of the queue and scheduled. We need to
	 * treat it special
	 */
	if ((pipe->head == transaction) &&
		(pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED)) {
		union cvmx_usbcx_hccharx usbc_hcchar;

		usb->pipe_for_channel[pipe->channel] = NULL;
		pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

		CVMX_SYNCW;

		usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(pipe->channel, usb->index));
		/* If the channel isn't enabled then the transaction already completed */
		if (usbc_hcchar.s.chena) {
			usbc_hcchar.s.chdis = 1;
			__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(pipe->channel, usb->index), usbc_hcchar.u32);
		}
	}
	__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_CANCEL);
	return 0;
}


/**
 * Cancel all outstanding requests in a pipe. Logically all this
 * does is call cvmx_usb_cancel() in a loop.
 *
 * @state:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe_handle:
 *		 Pipe handle to cancel requests in.
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_cancel_all(struct cvmx_usb_state *state, int pipe_handle)
{
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	struct cvmx_usb_pipe *pipe = usb->pipe + pipe_handle;

	if (unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
		return -EINVAL;

	/* Fail if the pipe isn't open */
	if (unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
		return -EINVAL;

	/* Simply loop through and attempt to cancel each transaction */
	while (pipe->head) {
		int result = cvmx_usb_cancel(state, pipe_handle,
			__cvmx_usb_get_submit_handle(usb, pipe->head));
		if (unlikely(result != 0))
			return result;
	}
	return 0;
}


/**
 * Close a pipe created with cvmx_usb_open_pipe().
 *
 * @state:	 USB device state populated by
 *		 cvmx_usb_initialize().
 * @pipe_handle:
 *		 Pipe handle to close.
 *
 * Returns: 0 or a negative error code. EBUSY is returned if the pipe has
 *	    outstanding transfers.
 */
int cvmx_usb_close_pipe(struct cvmx_usb_state *state, int pipe_handle)
{
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	struct cvmx_usb_pipe *pipe = usb->pipe + pipe_handle;

	if (unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
		return -EINVAL;

	/* Fail if the pipe isn't open */
	if (unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
		return -EINVAL;

	/* Fail if the pipe has pending transactions */
	if (unlikely(pipe->head))
		return -EBUSY;

	pipe->flags = 0;
	__cvmx_usb_remove_pipe(&usb->idle_pipes, pipe);
	__cvmx_usb_append_pipe(&usb->free_pipes, pipe);

	return 0;
}


/**
 * Register a function to be called when various USB events occur.
 *
 * @state:     USB device state populated by
 *	       cvmx_usb_initialize().
 * @reason:    Which event to register for.
 * @callback:  Function to call when the event occurs.
 * @user_data: User data parameter to the function.
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_register_callback(struct cvmx_usb_state *state,
			       enum cvmx_usb_callback reason,
			       cvmx_usb_callback_func_t callback,
			       void *user_data)
{
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	if (unlikely(reason >= __CVMX_USB_CALLBACK_END))
		return -EINVAL;
	if (unlikely(!callback))
		return -EINVAL;

	usb->callback[reason] = callback;
	usb->callback_data[reason] = user_data;

	return 0;
}


/**
 * Get the current USB protocol level frame number. The frame
 * number is always in the range of 0-0x7ff.
 *
 * @state: USB device state populated by
 *	   cvmx_usb_initialize().
 *
 * Returns: USB frame number
 */
int cvmx_usb_get_frame_number(struct cvmx_usb_state *state)
{
	int frame_number;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;
	union cvmx_usbcx_hfnum usbc_hfnum;

	usbc_hfnum.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFNUM(usb->index));
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
static int __cvmx_usb_poll_channel(struct cvmx_usb_internal_state *usb, int channel)
{
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
	usbc_hcint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index));

	if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA) {
		usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));

		if (usbc_hcchar.s.chena && usbc_hcchar.s.chdis) {
			/*
			 * There seems to be a bug in CN31XX which can cause
			 * interrupt IN transfers to get stuck until we do a
			 * write of HCCHARX without changing things
			 */
			__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index), usbc_hcchar.u32);
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
				__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), hcintmsk.u32);
				usbc_hcchar.s.chdis = 1;
				__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index), usbc_hcchar.u32);
				return 0;
			} else if (usbc_hcint.s.xfercompl) {
				/* Successful IN/OUT with transfer complete. Channel halt isn't needed */
			} else {
				cvmx_dprintf("USB%d: Channel %d interrupt without halt\n", usb->index, channel);
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
	__cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), 0);
	usb->idle_hardware_channels |= (1<<channel);

	/* Make sure this channel is tied to a valid pipe */
	pipe = usb->pipe_for_channel[channel];
	CVMX_PREFETCH(pipe, 0);
	CVMX_PREFETCH(pipe, 128);
	if (!pipe)
		return 0;
	transaction = pipe->head;
	CVMX_PREFETCH0(transaction);

	/*
	 * Disconnect this pipe from the HW channel. Later the schedule
	 * function will figure out which pipe needs to go
	 */
	usb->pipe_for_channel[channel] = NULL;
	pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

	/*
	 * Read the channel config info so we can figure out how much data
	 * transfered
	 */
	usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));
	usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));

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
		bytes_this_transfer = transaction->xfersize - usbc_hctsiz.s.xfersize;
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
		bytes_in_last_packet = bytes_this_transfer - (packets_processed-1) * usbc_hcchar.s.mps;
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
		buffer_space_left = transaction->iso_packets[0].length - transaction->actual_bytes;
	else
		buffer_space_left = transaction->buffer_length - transaction->actual_bytes;

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
		__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_STALL);
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
			__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_XACTERR);
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
				pipe->next_tx_frame = usb->frame_number + pipe->interval -
						      (usb->frame_number - pipe->next_tx_frame) % pipe->interval;
		}
	} else if (usbc_hcint.s.bblerr) {
		/* Babble Error (BblErr) */
		__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_BABBLEERR);
	} else if (usbc_hcint.s.datatglerr) {
		/* We'll retry the exact same transaction again */
		transaction->retries++;
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
			if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet))
				__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
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
				__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_ERROR);
				break;
			case CVMX_USB_STAGE_SETUP:
				pipe->pid_toggle = 1;
				if (__cvmx_usb_pipe_needs_split(usb, pipe))
					transaction->stage = CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE;
				else {
					union cvmx_usb_control_header *header =
						cvmx_phys_to_ptr(transaction->control_header);
					if (header->s.length)
						transaction->stage = CVMX_USB_STAGE_DATA;
					else
						transaction->stage = CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
				{
					union cvmx_usb_control_header *header =
						cvmx_phys_to_ptr(transaction->control_header);
					if (header->s.length)
						transaction->stage = CVMX_USB_STAGE_DATA;
					else
						transaction->stage = CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_DATA:
				if (__cvmx_usb_pipe_needs_split(usb, pipe)) {
					transaction->stage = CVMX_USB_STAGE_DATA_SPLIT_COMPLETE;
					/*
					 * For setup OUT data that are splits,
					 * the hardware doesn't appear to count
					 * transferred data. Here we manually
					 * update the data transferred
					 */
					if (!usbc_hcchar.s.epdir) {
						if (buffer_space_left < pipe->max_packet)
							transaction->actual_bytes += buffer_space_left;
						else
							transaction->actual_bytes += pipe->max_packet;
					}
				} else if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet)) {
					pipe->pid_toggle = 1;
					transaction->stage = CVMX_USB_STAGE_STATUS;
				}
				break;
			case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
				if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet)) {
					pipe->pid_toggle = 1;
					transaction->stage = CVMX_USB_STAGE_STATUS;
				} else {
					transaction->stage = CVMX_USB_STAGE_DATA;
				}
				break;
			case CVMX_USB_STAGE_STATUS:
				if (__cvmx_usb_pipe_needs_split(usb, pipe))
					transaction->stage = CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE;
				else
					__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
				break;
			case CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE:
				__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
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
				if (transaction->stage == CVMX_USB_STAGE_NON_CONTROL)
					transaction->stage = CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
				else {
					if (buffer_space_left && (bytes_in_last_packet == pipe->max_packet))
						transaction->stage = CVMX_USB_STAGE_NON_CONTROL;
					else {
						if (transaction->type == CVMX_USB_TRANSFER_INTERRUPT)
							pipe->next_tx_frame += pipe->interval;
							__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
					}
				}
			} else {
				if ((pipe->device_speed == CVMX_USB_SPEED_HIGH) &&
				    (pipe->transfer_type == CVMX_USB_TRANSFER_BULK) &&
				    (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) &&
				    (usbc_hcint.s.nak))
					pipe->flags |= __CVMX_USB_PIPE_FLAGS_NEED_PING;
				if (!buffer_space_left || (bytes_in_last_packet < pipe->max_packet)) {
					if (transaction->type == CVMX_USB_TRANSFER_INTERRUPT)
						pipe->next_tx_frame += pipe->interval;
					__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
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
				if (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) {
					/*
					 * If no space left or this wasn't a max
					 * size packet then this transfer is
					 * complete. Otherwise start it again to
					 * send the next 188 bytes
					 */
					if (!buffer_space_left || (bytes_this_transfer < 188)) {
						pipe->next_tx_frame += pipe->interval;
						__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
					}
				} else {
					if (transaction->stage == CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE) {
						/*
						 * We are in the incoming data
						 * phase. Keep getting data
						 * until we run out of space or
						 * get a small packet
						 */
						if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet)) {
							pipe->next_tx_frame += pipe->interval;
							__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
						}
					} else
						transaction->stage = CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
				}
			} else {
				pipe->next_tx_frame += pipe->interval;
				__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
			}
			break;
		}
	} else if (usbc_hcint.s.nak) {
		/* If this was a split then clear our split in progress marker */
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
			pipe->next_tx_frame = usb->frame_number + pipe->interval -
				(usb->frame_number - pipe->next_tx_frame) % pipe->interval;
	} else {
		struct cvmx_usb_port_status port;
		port = cvmx_usb_get_status((struct cvmx_usb_state *)usb);
		if (port.port_enabled) {
			/* We'll retry the exact same transaction again */
			transaction->retries++;
		} else {
			/*
			 * We get channel halted interrupts with no result bits
			 * sets when the cable is unplugged
			 */
			__cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_ERROR);
		}
	}
	return 0;
}


/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @state:	USB device state populated by
 *		cvmx_usb_initialize().
 *
 * Returns: 0 or a negative error code.
 */
int cvmx_usb_poll(struct cvmx_usb_state *state)
{
	union cvmx_usbcx_hfnum usbc_hfnum;
	union cvmx_usbcx_gintsts usbc_gintsts;
	struct cvmx_usb_internal_state *usb = (struct cvmx_usb_internal_state *)state;

	CVMX_PREFETCH(usb, 0);
	CVMX_PREFETCH(usb, 1*128);
	CVMX_PREFETCH(usb, 2*128);
	CVMX_PREFETCH(usb, 3*128);
	CVMX_PREFETCH(usb, 4*128);

	/* Update the frame counter */
	usbc_hfnum.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFNUM(usb->index));
	if ((usb->frame_number&0x3fff) > usbc_hfnum.s.frnum)
		usb->frame_number += 0x4000;
	usb->frame_number &= ~0x3fffull;
	usb->frame_number |= usbc_hfnum.s.frnum;

	/* Read the pending interrupts */
	usbc_gintsts.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GINTSTS(usb->index));

	/* Clear the interrupts now that we know about them */
	__cvmx_usb_write_csr32(usb, CVMX_USBCX_GINTSTS(usb->index), usbc_gintsts.u32);

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
		__cvmx_usb_perform_callback(usb, NULL, NULL,
					    CVMX_USB_CALLBACK_PORT_CHANGED,
					    CVMX_USB_COMPLETE_SUCCESS);
		/* Clear the port change bits */
		usbc_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
		usbc_hprt.s.prtena = 0;
		__cvmx_usb_write_csr32(usb, CVMX_USBCX_HPRT(usb->index), usbc_hprt.u32);
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
		usbc_haint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HAINT(usb->index));
		while (usbc_haint.u32) {
			int channel;
			CVMX_CLZ(channel, usbc_haint.u32);
			channel = 31 - channel;
			__cvmx_usb_poll_channel(usb, channel);
			usbc_haint.u32 ^= 1<<channel;
		}
	}

	__cvmx_usb_schedule(usb, usbc_gintsts.s.sof);

	return 0;
}
