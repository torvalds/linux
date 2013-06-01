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
 *
 * <hr>$Revision: 32636 $<hr>
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

#define cvmx_likely likely
#define cvmx_wait_usec udelay
#define cvmx_unlikely unlikely
#define cvmx_le16_to_cpu le16_to_cpu

#define MAX_RETRIES         3   /* Maximum number of times to retry failed transactions */
#define MAX_PIPES           32  /* Maximum number of pipes that can be open at once */
#define MAX_TRANSACTIONS    256 /* Maximum number of outstanding transactions across all pipes */
#define MAX_CHANNELS        8   /* Maximum number of hardware channels supported by the USB block */
#define MAX_USB_ADDRESS     127 /* The highest valid USB device address */
#define MAX_USB_ENDPOINT    15  /* The highest valid USB endpoint number */
#define MAX_USB_HUB_PORT    15  /* The highest valid port number on a hub */
#define MAX_TRANSFER_BYTES  ((1<<19)-1) /* The low level hardware can transfer a maximum of this number of bytes in each transfer. The field is 19 bits wide */
#define MAX_TRANSFER_PACKETS ((1<<10)-1) /* The low level hardware can transfer a maximum of this number of packets in each transfer. The field is 10 bits wide */

/* These defines disable the normal read and write csr. This is so I can add
    extra debug stuff to the usb specific version and I won't use the normal
    version by mistake */
#define cvmx_read_csr use_cvmx_usb_read_csr64_instead_of_cvmx_read_csr
#define cvmx_write_csr use_cvmx_usb_write_csr64_instead_of_cvmx_write_csr

typedef enum
{
    __CVMX_USB_TRANSACTION_FLAGS_IN_USE = 1<<16,
} cvmx_usb_transaction_flags_t;

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
typedef enum
{
    CVMX_USB_STAGE_NON_CONTROL,
    CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE,
    CVMX_USB_STAGE_SETUP,
    CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE,
    CVMX_USB_STAGE_DATA,
    CVMX_USB_STAGE_DATA_SPLIT_COMPLETE,
    CVMX_USB_STAGE_STATUS,
    CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE,
} cvmx_usb_stage_t;

/**
 * This structure describes each pending USB transaction
 * regardless of type. These are linked together to form a list
 * of pending requests for a pipe.
 */
typedef struct cvmx_usb_transaction
{
    struct cvmx_usb_transaction *prev;  /**< Transaction before this one in the pipe */
    struct cvmx_usb_transaction *next;  /**< Transaction after this one in the pipe */
    cvmx_usb_transfer_t type;           /**< Type of transaction, duplicated of the pipe */
    cvmx_usb_transaction_flags_t flags; /**< State flags for this transaction */
    uint64_t buffer;                    /**< User's physical buffer address to read/write */
    int buffer_length;                  /**< Size of the user's buffer in bytes */
    uint64_t control_header;            /**< For control transactions, physical address of the 8 byte standard header */
    int iso_start_frame;                /**< For ISO transactions, the starting frame number */
    int iso_number_packets;             /**< For ISO transactions, the number of packets in the request */
    cvmx_usb_iso_packet_t *iso_packets; /**< For ISO transactions, the sub packets in the request */
    int xfersize;
    int pktcnt;
    int retries;
    int actual_bytes;                   /**< Actual bytes transfer for this transaction */
    cvmx_usb_stage_t stage;             /**< For control transactions, the current stage */
    cvmx_usb_callback_func_t callback;  /**< User's callback function when complete */
    void *callback_data;                /**< User's data */
} cvmx_usb_transaction_t;

/**
 * A pipe represents a virtual connection between Octeon and some
 * USB device. It contains a list of pending request to the device.
 */
typedef struct cvmx_usb_pipe
{
    struct cvmx_usb_pipe *prev;         /**< Pipe before this one in the list */
    struct cvmx_usb_pipe *next;         /**< Pipe after this one in the list */
    cvmx_usb_transaction_t *head;       /**< The first pending transaction */
    cvmx_usb_transaction_t *tail;       /**< The last pending transaction */
    uint64_t interval;                  /**< For periodic pipes, the interval between packets in frames */
    uint64_t next_tx_frame;             /**< The next frame this pipe is allowed to transmit on */
    cvmx_usb_pipe_flags_t flags;        /**< State flags for this pipe */
    cvmx_usb_speed_t device_speed;      /**< Speed of device connected to this pipe */
    cvmx_usb_transfer_t transfer_type;  /**< Type of transaction supported by this pipe */
    cvmx_usb_direction_t transfer_dir;  /**< IN or OUT. Ignored for Control */
    int multi_count;                    /**< Max packet in a row for the device */
    uint16_t max_packet;                /**< The device's maximum packet size in bytes */
    uint8_t device_addr;                /**< USB device address at other end of pipe */
    uint8_t endpoint_num;               /**< USB endpoint number at other end of pipe */
    uint8_t hub_device_addr;            /**< Hub address this device is connected to */
    uint8_t hub_port;                   /**< Hub port this device is connected to */
    uint8_t pid_toggle;                 /**< This toggles between 0/1 on every packet send to track the data pid needed */
    uint8_t channel;                    /**< Hardware DMA channel for this pipe */
    int8_t  split_sc_frame;             /**< The low order bits of the frame number the split complete should be sent on */
} cvmx_usb_pipe_t;

typedef struct
{
    cvmx_usb_pipe_t *head;              /**< Head of the list, or NULL if empty */
    cvmx_usb_pipe_t *tail;              /**< Tail if the list, or NULL if empty */
} cvmx_usb_pipe_list_t;

typedef struct
{
    struct
    {
        int channel;
        int size;
        uint64_t address;
    } entry[MAX_CHANNELS+1];
    int head;
    int tail;
} cvmx_usb_tx_fifo_t;

/**
 * The state of the USB block is stored in this structure
 */
typedef struct
{
    int init_flags;                     /**< Flags passed to initialize */
    int index;                          /**< Which USB block this is for */
    int idle_hardware_channels;         /**< Bit set for every idle hardware channel */
    cvmx_usbcx_hprt_t usbcx_hprt;       /**< Stored port status so we don't need to read a CSR to determine splits */
    cvmx_usb_pipe_t *pipe_for_channel[MAX_CHANNELS];    /**< Map channels to pipes */
    cvmx_usb_transaction_t *free_transaction_head;      /**< List of free transactions head */
    cvmx_usb_transaction_t *free_transaction_tail;      /**< List of free transactions tail */
    cvmx_usb_pipe_t pipe[MAX_PIPES];                    /**< Storage for pipes */
    cvmx_usb_transaction_t transaction[MAX_TRANSACTIONS];       /**< Storage for transactions */
    cvmx_usb_callback_func_t callback[__CVMX_USB_CALLBACK_END]; /**< User global callbacks */
    void *callback_data[__CVMX_USB_CALLBACK_END];               /**< User data for each callback */
    int indent;                         /**< Used by debug output to indent functions */
    cvmx_usb_port_status_t port_status; /**< Last port status used for change notification */
    cvmx_usb_pipe_list_t free_pipes;    /**< List of all pipes that are currently closed */
    cvmx_usb_pipe_list_t idle_pipes;    /**< List of open pipes that have no transactions */
    cvmx_usb_pipe_list_t active_pipes[4]; /**< Active pipes indexed by transfer type */
    uint64_t frame_number;              /**< Increments every SOF interrupt for time keeping */
    cvmx_usb_transaction_t *active_split; /**< Points to the current active split, or NULL */
    cvmx_usb_tx_fifo_t periodic;
    cvmx_usb_tx_fifo_t nonperiodic;
} cvmx_usb_internal_state_t;

/* This macro logs out whenever a function is called if debugging is on */
#define CVMX_USB_LOG_CALLED() \
    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS)) \
        cvmx_dprintf("%*s%s: called\n", 2*usb->indent++, "", __FUNCTION__);

/* This macro logs out each function parameter if debugging is on */
#define CVMX_USB_LOG_PARAM(format, param) \
    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS)) \
        cvmx_dprintf("%*s%s: param %s = " format "\n", 2*usb->indent, "", __FUNCTION__, #param, param);

/* This macro logs out when a function returns a value */
#define CVMX_USB_RETURN(v)                                              \
    do {                                                                \
        typeof(v) r = v;                                                \
        if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS))    \
            cvmx_dprintf("%*s%s: returned %s(%d)\n", 2*--usb->indent, "", __FUNCTION__, #v, r); \
        return r;                                                       \
    } while (0);

/* This macro logs out when a function doesn't return a value */
#define CVMX_USB_RETURN_NOTHING()                                       \
    do {                                                                \
        if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS))    \
            cvmx_dprintf("%*s%s: returned\n", 2*--usb->indent, "", __FUNCTION__); \
        return;                                                         \
    } while (0);

/* This macro spins on a field waiting for it to reach a value */
#define CVMX_WAIT_FOR_FIELD32(address, type, field, op, value, timeout_usec)\
    ({int result;                                                       \
    do {                                                                \
        uint64_t done = cvmx_get_cycle() + (uint64_t)timeout_usec *     \
			octeon_get_clock_rate() / 1000000;		\
        type c;                                                         \
        while (1)                                                       \
        {                                                               \
            c.u32 = __cvmx_usb_read_csr32(usb, address);                \
            if (c.s.field op (value)) {                                 \
                result = 0;                                             \
                break;                                                  \
            } else if (cvmx_get_cycle() > done) {                       \
                result = -1;                                            \
                break;                                                  \
            } else                                                      \
                cvmx_wait(100);                                         \
        }                                                               \
    } while (0);                                                        \
    result;})

/* This macro logically sets a single field in a CSR. It does the sequence
    read, modify, and write */
#define USB_SET_FIELD32(address, type, field, value)\
    do {                                            \
        type c;                                     \
        c.u32 = __cvmx_usb_read_csr32(usb, address);\
        c.s.field = value;                          \
        __cvmx_usb_write_csr32(usb, address, c.u32);\
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
		return USB_CLOCK_TYPE_CRYSTAL_12;
	}

	/* FIXME: This should use CVMX_BOARD_TYPE_UBNT_E100 */
	if (OCTEON_IS_MODEL(OCTEON_CN50XX) &&
	    cvmx_sysinfo_get()->board_type == 20002)
		return USB_CLOCK_TYPE_CRYSTAL_12;

	return USB_CLOCK_TYPE_REF_48;
}

/**
 * @INTERNAL
 * Read a USB 32bit CSR. It performs the necessary address swizzle
 * for 32bit CSRs and logs the value in a readable format if
 * debugging is on.
 *
 * @param usb     USB block this access is for
 * @param address 64bit address to read
 *
 * @return Result of the read
 */
static inline uint32_t __cvmx_usb_read_csr32(cvmx_usb_internal_state_t *usb,
                                             uint64_t address)
{
    uint32_t result = cvmx_read64_uint32(address ^ 4);
    return result;
}


/**
 * @INTERNAL
 * Write a USB 32bit CSR. It performs the necessary address
 * swizzle for 32bit CSRs and logs the value in a readable format
 * if debugging is on.
 *
 * @param usb     USB block this access is for
 * @param address 64bit address to write
 * @param value   Value to write
 */
static inline void __cvmx_usb_write_csr32(cvmx_usb_internal_state_t *usb,
                                          uint64_t address, uint32_t value)
{
    cvmx_write64_uint32(address ^ 4, value);
    cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
}


/**
 * @INTERNAL
 * Read a USB 64bit CSR. It logs the value in a readable format if
 * debugging is on.
 *
 * @param usb     USB block this access is for
 * @param address 64bit address to read
 *
 * @return Result of the read
 */
static inline uint64_t __cvmx_usb_read_csr64(cvmx_usb_internal_state_t *usb,
                                             uint64_t address)
{
    uint64_t result = cvmx_read64_uint64(address);
    return result;
}


/**
 * @INTERNAL
 * Write a USB 64bit CSR. It logs the value in a readable format
 * if debugging is on.
 *
 * @param usb     USB block this access is for
 * @param address 64bit address to write
 * @param value   Value to write
 */
static inline void __cvmx_usb_write_csr64(cvmx_usb_internal_state_t *usb,
                                          uint64_t address, uint64_t value)
{
    cvmx_write64_uint64(address, value);
}


/**
 * @INTERNAL
 * Utility function to convert complete codes into strings
 *
 * @param complete_code
 *               Code to convert
 *
 * @return Human readable string
 */
static const char *__cvmx_usb_complete_to_string(cvmx_usb_complete_t complete_code)
{
    switch (complete_code)
    {
        case CVMX_USB_COMPLETE_SUCCESS: return "SUCCESS";
        case CVMX_USB_COMPLETE_SHORT:   return "SHORT";
        case CVMX_USB_COMPLETE_CANCEL:  return "CANCEL";
        case CVMX_USB_COMPLETE_ERROR:   return "ERROR";
        case CVMX_USB_COMPLETE_STALL:   return "STALL";
        case CVMX_USB_COMPLETE_XACTERR: return "XACTERR";
        case CVMX_USB_COMPLETE_DATATGLERR: return "DATATGLERR";
        case CVMX_USB_COMPLETE_BABBLEERR: return "BABBLEERR";
        case CVMX_USB_COMPLETE_FRAMEERR: return "FRAMEERR";
    }
    return "Update __cvmx_usb_complete_to_string";
}


/**
 * @INTERNAL
 * Return non zero if this pipe connects to a non HIGH speed
 * device through a high speed hub.
 *
 * @param usb    USB block this access is for
 * @param pipe   Pipe to check
 *
 * @return Non zero if we need to do split transactions
 */
static inline int __cvmx_usb_pipe_needs_split(cvmx_usb_internal_state_t *usb, cvmx_usb_pipe_t *pipe)
{
    return ((pipe->device_speed != CVMX_USB_SPEED_HIGH) && (usb->usbcx_hprt.s.prtspd == CVMX_USB_SPEED_HIGH));
}


/**
 * @INTERNAL
 * Trivial utility function to return the correct PID for a pipe
 *
 * @param pipe   pipe to check
 *
 * @return PID for pipe
 */
static inline int __cvmx_usb_get_data_pid(cvmx_usb_pipe_t *pipe)
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
 * cvmx_usb_state_t structures.
 *
 * @return Number of port, zero if usb isn't supported
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
 * @INTERNAL
 * Allocate a usb transaction for use
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Transaction or NULL
 */
static inline cvmx_usb_transaction_t *__cvmx_usb_alloc_transaction(cvmx_usb_internal_state_t *usb)
{
    cvmx_usb_transaction_t *t;
    t = usb->free_transaction_head;
    if (t)
    {
        usb->free_transaction_head = t->next;
        if (!usb->free_transaction_head)
            usb->free_transaction_tail = NULL;
    }
    else if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
        cvmx_dprintf("%s: Failed to allocate a transaction\n", __FUNCTION__);
    if (t)
    {
        memset(t, 0, sizeof(*t));
        t->flags = __CVMX_USB_TRANSACTION_FLAGS_IN_USE;
    }
    return t;
}


/**
 * @INTERNAL
 * Free a usb transaction
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param transaction
 *               Transaction to free
 */
static inline void __cvmx_usb_free_transaction(cvmx_usb_internal_state_t *usb,
                                        cvmx_usb_transaction_t *transaction)
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
 * @INTERNAL
 * Add a pipe to the tail of a list
 * @param list   List to add pipe to
 * @param pipe   Pipe to add
 */
static inline void __cvmx_usb_append_pipe(cvmx_usb_pipe_list_t *list, cvmx_usb_pipe_t *pipe)
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
 * @INTERNAL
 * Remove a pipe from a list
 * @param list   List to remove pipe from
 * @param pipe   Pipe to remove
 */
static inline void __cvmx_usb_remove_pipe(cvmx_usb_pipe_list_t *list, cvmx_usb_pipe_t *pipe)
{
    if (list->head == pipe)
    {
        list->head = pipe->next;
        pipe->next = NULL;
        if (list->head)
            list->head->prev = NULL;
        else
            list->tail = NULL;
    }
    else if (list->tail == pipe)
    {
        list->tail = pipe->prev;
        list->tail->next = NULL;
        pipe->prev = NULL;
    }
    else
    {
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
 * @param state  Pointer to an empty cvmx_usb_state_t structure
 *               that will be populated by the initialize call.
 *               This structure is then passed to all other USB
 *               functions.
 * @param usb_port_number
 *               Which Octeon USB port to initialize.
 * @param flags  Flags to control hardware initialization. See
 *               cvmx_usb_initialize_flags_t for the flag
 *               definitions. Some flags are mandatory.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_initialize(cvmx_usb_state_t *state,
                                      int usb_port_number,
                                      cvmx_usb_initialize_flags_t flags)
{
    cvmx_usbnx_clk_ctl_t usbn_clk_ctl;
    cvmx_usbnx_usbp_ctl_status_t usbn_usbp_ctl_status;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    usb->init_flags = flags;
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", usb_port_number);
    CVMX_USB_LOG_PARAM("0x%x", flags);

    /* Make sure that state is large enough to store the internal state */
    if (sizeof(*state) < sizeof(*usb))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    /* At first allow 0-1 for the usb port number */
    if ((usb_port_number < 0) || (usb_port_number > 1))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    /* For all chips except 52XX there is only one port */
    if (!OCTEON_IS_MODEL(OCTEON_CN52XX) && (usb_port_number > 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    /* Try to determine clock type automatically */
    if ((flags & (CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI |
                  CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND)) == 0)
    {
        if (octeon_usb_get_clock_type() == USB_CLOCK_TYPE_CRYSTAL_12)
            flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI;  /* Only 12 MHZ crystals are supported */
        else
            flags |= CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND;
    }

    if (flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND)
    {
        /* Check for auto ref clock frequency */
        if (!(flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK))
            switch (octeon_usb_get_clock_type())
            {
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
                    CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
                    break;
            }
    }

    memset(usb, 0, sizeof(usb));
    usb->init_flags = flags;

    /* Initialize the USB state structure */
    {
        int i;
        usb->index = usb_port_number;

        /* Initialize the transaction double linked list */
        usb->free_transaction_head = NULL;
        usb->free_transaction_tail = NULL;
        for (i=0; i<MAX_TRANSACTIONS; i++)
            __cvmx_usb_free_transaction(usb, usb->transaction + i);
        for (i=0; i<MAX_PIPES; i++)
            __cvmx_usb_append_pipe(&usb->free_pipes, usb->pipe + i);
    }

    /* Power On Reset and PHY Initialization */

    /* 1. Wait for DCOK to assert (nothing to do) */
    /* 2a. Write USBN0/1_CLK_CTL[POR] = 1 and
        USBN0/1_CLK_CTL[HRST,PRST,HCLK_RST] = 0 */
    usbn_clk_ctl.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index));
    usbn_clk_ctl.s.por = 1;
    usbn_clk_ctl.s.hrst = 0;
    usbn_clk_ctl.s.prst = 0;
    usbn_clk_ctl.s.hclk_rst = 0;
    usbn_clk_ctl.s.enable = 0;
    /* 2b. Select the USB reference clock/crystal parameters by writing
        appropriate values to USBN0/1_CLK_CTL[P_C_SEL, P_RTYPE, P_COM_ON] */
    if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND)
    {
        /* The USB port uses 12/24/48MHz 2.5V board clock
            source at USB_XO. USB_XI should be tied to GND.
            Most Octeon evaluation boards require this setting */
        if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
        {
            usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
            usbn_clk_ctl.cn31xx.p_xenbn = 0;
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
            usbn_clk_ctl.cn56xx.p_rtype = 2; /* From CN56XX,CN50XX manual */
        else
            usbn_clk_ctl.cn52xx.p_rtype = 1; /* From CN52XX manual */

        switch (flags & CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK)
        {
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
    }
    else
    {
        /* The USB port uses a 12MHz crystal as clock source
            at USB_XO and USB_XI */
        if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
        {
            usbn_clk_ctl.cn31xx.p_rclk  = 1; /* From CN31XX,CN30XX manual */
            usbn_clk_ctl.cn31xx.p_xenbn = 1;
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
            usbn_clk_ctl.cn56xx.p_rtype = 0; /* From CN56XX,CN50XX manual */
        else
            usbn_clk_ctl.cn52xx.p_rtype = 0; /* From CN52XX manual */

        usbn_clk_ctl.s.p_c_sel = 0;
    }
    /* 2c. Select the HCLK via writing USBN0/1_CLK_CTL[DIVIDE, DIVIDE2] and
        setting USBN0/1_CLK_CTL[ENABLE] = 1.  Divide the core clock down such
        that USB is as close as possible to 125Mhz */
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
    /* 3. Program the power-on reset field in the USBN clock-control register:
        USBN_CLK_CTL[POR] = 0 */
    usbn_clk_ctl.s.por = 0;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
                           usbn_clk_ctl.u64);
    /* 4. Wait 1 ms for PHY clock to start */
    cvmx_wait_usec(1000);
    /* 5. Program the Reset input from automatic test equipment field in the
        USBP control and status register: USBN_USBP_CTL_STATUS[ATE_RESET] = 1 */
    usbn_usbp_ctl_status.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index));
    usbn_usbp_ctl_status.s.ate_reset = 1;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
                           usbn_usbp_ctl_status.u64);
    /* 6. Wait 10 cycles */
    cvmx_wait(10);
    /* 7. Clear ATE_RESET field in the USBN clock-control register:
        USBN_USBP_CTL_STATUS[ATE_RESET] = 0 */
    usbn_usbp_ctl_status.s.ate_reset = 0;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
                           usbn_usbp_ctl_status.u64);
    /* 8. Program the PHY reset field in the USBN clock-control register:
        USBN_CLK_CTL[PRST] = 1 */
    usbn_clk_ctl.s.prst = 1;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
                           usbn_clk_ctl.u64);
    /* 9. Program the USBP control and status register to select host or
        device mode. USBN_USBP_CTL_STATUS[HST_MODE] = 0 for host, = 1 for
        device */
    usbn_usbp_ctl_status.s.hst_mode = 0;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_USBP_CTL_STATUS(usb->index),
                           usbn_usbp_ctl_status.u64);
    /* 10. Wait 1 us */
    cvmx_wait_usec(1);
    /* 11. Program the hreset_n field in the USBN clock-control register:
        USBN_CLK_CTL[HRST] = 1 */
    usbn_clk_ctl.s.hrst = 1;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
                           usbn_clk_ctl.u64);
    /* 12. Proceed to USB core initialization */
    usbn_clk_ctl.s.enable = 1;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
                           usbn_clk_ctl.u64);
    cvmx_wait_usec(1);

    /* USB Core Initialization */

    /* 1. Read USBC_GHWCFG1, USBC_GHWCFG2, USBC_GHWCFG3, USBC_GHWCFG4 to
        determine USB core configuration parameters. */
    /* Nothing needed */
    /* 2. Program the following fields in the global AHB configuration
        register (USBC_GAHBCFG)
        DMA mode, USBC_GAHBCFG[DMAEn]: 1 = DMA mode, 0 = slave mode
        Burst length, USBC_GAHBCFG[HBSTLEN] = 0
        Nonperiodic TxFIFO empty level (slave mode only),
        USBC_GAHBCFG[NPTXFEMPLVL]
        Periodic TxFIFO empty level (slave mode only),
        USBC_GAHBCFG[PTXFEMPLVL]
        Global interrupt mask, USBC_GAHBCFG[GLBLINTRMSK] = 1 */
    {
        cvmx_usbcx_gahbcfg_t usbcx_gahbcfg;
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
    /* 3. Program the following fields in USBC_GUSBCFG register.
        HS/FS timeout calibration, USBC_GUSBCFG[TOUTCAL] = 0
        ULPI DDR select, USBC_GUSBCFG[DDRSEL] = 0
        USB turnaround time, USBC_GUSBCFG[USBTRDTIM] = 0x5
        PHY low-power clock select, USBC_GUSBCFG[PHYLPWRCLKSEL] = 0 */
    {
        cvmx_usbcx_gusbcfg_t usbcx_gusbcfg;
        usbcx_gusbcfg.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index));
        usbcx_gusbcfg.s.toutcal = 0;
        usbcx_gusbcfg.s.ddrsel = 0;
        usbcx_gusbcfg.s.usbtrdtim = 0x5;
        usbcx_gusbcfg.s.phylpwrclksel = 0;
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_GUSBCFG(usb->index),
                               usbcx_gusbcfg.u32);
    }
    /* 4. The software must unmask the following bits in the USBC_GINTMSK
        register.
        OTG interrupt mask, USBC_GINTMSK[OTGINTMSK] = 1
        Mode mismatch interrupt mask, USBC_GINTMSK[MODEMISMSK] = 1 */
    {
        cvmx_usbcx_gintmsk_t usbcx_gintmsk;
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
        for (channel=0; channel<8; channel++)
            __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), 0);
    }

    {
        /* Host Port Initialization */
        if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
            cvmx_dprintf("%s: USB%d is in host mode\n", __FUNCTION__, usb->index);

        /* 1. Program the host-port interrupt-mask field to unmask,
            USBC_GINTMSK[PRTINT] = 1 */
        USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t,
                        prtintmsk, 1);
        USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t,
                        disconnintmsk, 1);
        /* 2. Program the USBC_HCFG register to select full-speed host or
            high-speed host. */
        {
            cvmx_usbcx_hcfg_t usbcx_hcfg;
            usbcx_hcfg.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCFG(usb->index));
            usbcx_hcfg.s.fslssupp = 0;
            usbcx_hcfg.s.fslspclksel = 0;
            __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCFG(usb->index), usbcx_hcfg.u32);
        }
        /* 3. Program the port power bit to drive VBUS on the USB,
            USBC_HPRT[PRTPWR] = 1 */
        USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), cvmx_usbcx_hprt_t, prtpwr, 1);

        /* Steps 4-15 from the manual are done later in the port enable */
    }

    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Shutdown a USB port after a call to cvmx_usb_initialize().
 * The port should be disabled with all pipes closed when this
 * function is called.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_shutdown(cvmx_usb_state_t *state)
{
    cvmx_usbnx_clk_ctl_t usbn_clk_ctl;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

    /* Make sure all pipes are closed */
    if (usb->idle_pipes.head ||
        usb->active_pipes[CVMX_USB_TRANSFER_ISOCHRONOUS].head ||
        usb->active_pipes[CVMX_USB_TRANSFER_INTERRUPT].head ||
        usb->active_pipes[CVMX_USB_TRANSFER_CONTROL].head ||
        usb->active_pipes[CVMX_USB_TRANSFER_BULK].head)
        CVMX_USB_RETURN(CVMX_USB_BUSY);

    /* Disable the clocks and put them in power on reset */
    usbn_clk_ctl.u64 = __cvmx_usb_read_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index));
    usbn_clk_ctl.s.enable = 1;
    usbn_clk_ctl.s.por = 1;
    usbn_clk_ctl.s.hclk_rst = 1;
    usbn_clk_ctl.s.prst = 0;
    usbn_clk_ctl.s.hrst = 0;
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_CLK_CTL(usb->index),
                           usbn_clk_ctl.u64);
    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Enable a USB port. After this call succeeds, the USB port is
 * online and servicing requests.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_enable(cvmx_usb_state_t *state)
{
    cvmx_usbcx_ghwcfg3_t usbcx_ghwcfg3;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

    usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));

    /* If the port is already enabled the just return. We don't need to do
        anything */
    if (usb->usbcx_hprt.s.prtena)
        CVMX_USB_RETURN(CVMX_USB_SUCCESS);

    /* If there is nothing plugged into the port then fail immediately */
    if (!usb->usbcx_hprt.s.prtconnsts)
    {
        if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
            cvmx_dprintf("%s: USB%d Nothing plugged into the port\n", __FUNCTION__, usb->index);
        CVMX_USB_RETURN(CVMX_USB_TIMEOUT);
    }

    /* Program the port reset bit to start the reset process */
    USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), cvmx_usbcx_hprt_t, prtrst, 1);

    /* Wait at least 50ms (high speed), or 10ms (full speed) for the reset
        process to complete. */
    cvmx_wait_usec(50000);

    /* Program the port reset bit to 0, USBC_HPRT[PRTRST] = 0 */
    USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), cvmx_usbcx_hprt_t, prtrst, 0);

    /* Wait for the USBC_HPRT[PRTENA]. */
    if (CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_HPRT(usb->index), cvmx_usbcx_hprt_t,
                              prtena, ==, 1, 100000))
    {
        if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
            cvmx_dprintf("%s: Timeout waiting for the port to finish reset\n",
                         __FUNCTION__);
        CVMX_USB_RETURN(CVMX_USB_TIMEOUT);
    }

    /* Read the port speed field to get the enumerated speed, USBC_HPRT[PRTSPD]. */
    usb->usbcx_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
        cvmx_dprintf("%s: USB%d is in %s speed mode\n", __FUNCTION__, usb->index,
                     (usb->usbcx_hprt.s.prtspd == CVMX_USB_SPEED_HIGH) ? "high" :
                     (usb->usbcx_hprt.s.prtspd == CVMX_USB_SPEED_FULL) ? "full" :
                     "low");

    usbcx_ghwcfg3.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GHWCFG3(usb->index));

    /* 13. Program the USBC_GRXFSIZ register to select the size of the receive
        FIFO (25%). */
    USB_SET_FIELD32(CVMX_USBCX_GRXFSIZ(usb->index), cvmx_usbcx_grxfsiz_t,
                    rxfdep, usbcx_ghwcfg3.s.dfifodepth / 4);
    /* 14. Program the USBC_GNPTXFSIZ register to select the size and the
        start address of the non- periodic transmit FIFO for nonperiodic
        transactions (50%). */
    {
        cvmx_usbcx_gnptxfsiz_t siz;
        siz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index));
        siz.s.nptxfdep = usbcx_ghwcfg3.s.dfifodepth / 2;
        siz.s.nptxfstaddr = usbcx_ghwcfg3.s.dfifodepth / 4;
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_GNPTXFSIZ(usb->index), siz.u32);
    }
    /* 15. Program the USBC_HPTXFSIZ register to select the size and start
        address of the periodic transmit FIFO for periodic transactions (25%). */
    {
        cvmx_usbcx_hptxfsiz_t siz;
        siz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPTXFSIZ(usb->index));
        siz.s.ptxfsize = usbcx_ghwcfg3.s.dfifodepth / 4;
        siz.s.ptxfstaddr = 3 * usbcx_ghwcfg3.s.dfifodepth / 4;
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_HPTXFSIZ(usb->index), siz.u32);
    }
    /* Flush all FIFOs */
    USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), cvmx_usbcx_grstctl_t, txfnum, 0x10);
    USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), cvmx_usbcx_grstctl_t, txfflsh, 1);
    CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), cvmx_usbcx_grstctl_t,
                          txfflsh, ==, 0, 100);
    USB_SET_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), cvmx_usbcx_grstctl_t, rxfflsh, 1);
    CVMX_WAIT_FOR_FIELD32(CVMX_USBCX_GRSTCTL(usb->index), cvmx_usbcx_grstctl_t,
                          rxfflsh, ==, 0, 100);

    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 * Transactions in process will fail and call their
 * associated callbacks.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_disable(cvmx_usb_state_t *state)
{
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

    /* Disable the port */
    USB_SET_FIELD32(CVMX_USBCX_HPRT(usb->index), cvmx_usbcx_hprt_t, prtena, 1);
    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
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
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Port status information
 */
cvmx_usb_port_status_t cvmx_usb_get_status(cvmx_usb_state_t *state)
{
    cvmx_usbcx_hprt_t usbc_hprt;
    cvmx_usb_port_status_t result;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    memset(&result, 0, sizeof(result));

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

    usbc_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
    result.port_enabled = usbc_hprt.s.prtena;
    result.port_over_current = usbc_hprt.s.prtovrcurract;
    result.port_powered = usbc_hprt.s.prtpwr;
    result.port_speed = usbc_hprt.s.prtspd;
    result.connected = usbc_hprt.s.prtconnsts;
    result.connect_change = (result.connected != usb->port_status.connected);

    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS))
        cvmx_dprintf("%*s%s: returned port enabled=%d, over_current=%d, powered=%d, speed=%d, connected=%d, connect_change=%d\n",
                     2*(--usb->indent), "", __FUNCTION__,
                     result.port_enabled,
                     result.port_over_current,
                     result.port_powered,
                     result.port_speed,
                     result.connected,
                     result.connect_change);
    return result;
}


/**
 * Set the current state of the USB port. The status is used as
 * a reference for the "changed" bits returned by
 * cvmx_usb_get_status(). Other than serving as a reference, the
 * status passed to this function is not used. No fields can be
 * changed through this call.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param port_status
 *               Port status to set, most like returned by cvmx_usb_get_status()
 */
void cvmx_usb_set_status(cvmx_usb_state_t *state, cvmx_usb_port_status_t port_status)
{
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    usb->port_status = port_status;
    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Convert a USB transaction into a handle
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param transaction
 *               Transaction to get handle for
 *
 * @return Handle
 */
static inline int __cvmx_usb_get_submit_handle(cvmx_usb_internal_state_t *usb,
                                        cvmx_usb_transaction_t *transaction)
{
    return ((unsigned long)transaction - (unsigned long)usb->transaction) /
            sizeof(*transaction);
}


/**
 * @INTERNAL
 * Convert a USB pipe into a handle
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe   Pipe to get handle for
 *
 * @return Handle
 */
static inline int __cvmx_usb_get_pipe_handle(cvmx_usb_internal_state_t *usb,
                                        cvmx_usb_pipe_t *pipe)
{
    return ((unsigned long)pipe - (unsigned long)usb->pipe) / sizeof(*pipe);
}


/**
 * Open a virtual pipe between the host and a USB device. A pipe
 * must be opened before data can be transferred between a device
 * and Octeon.
 *
 * @param state      USB device state populated by
 *                   cvmx_usb_initialize().
 * @param flags      Optional pipe flags defined in
 *                   cvmx_usb_pipe_flags_t.
 * @param device_addr
 *                   USB device address to open the pipe to
 *                   (0-127).
 * @param endpoint_num
 *                   USB endpoint number to open the pipe to
 *                   (0-15).
 * @param device_speed
 *                   The speed of the device the pipe is going
 *                   to. This must match the device's speed,
 *                   which may be different than the port speed.
 * @param max_packet The maximum packet length the device can
 *                   transmit/receive (low speed=0-8, full
 *                   speed=0-1023, high speed=0-1024). This value
 *                   comes from the standard endpoint descriptor
 *                   field wMaxPacketSize bits <10:0>.
 * @param transfer_type
 *                   The type of transfer this pipe is for.
 * @param transfer_dir
 *                   The direction the pipe is in. This is not
 *                   used for control pipes.
 * @param interval   For ISOCHRONOUS and INTERRUPT transfers,
 *                   this is how often the transfer is scheduled
 *                   for. All other transfers should specify
 *                   zero. The units are in frames (8000/sec at
 *                   high speed, 1000/sec for full speed).
 * @param multi_count
 *                   For high speed devices, this is the maximum
 *                   allowed number of packet per microframe.
 *                   Specify zero for non high speed devices. This
 *                   value comes from the standard endpoint descriptor
 *                   field wMaxPacketSize bits <12:11>.
 * @param hub_device_addr
 *                   Hub device address this device is connected
 *                   to. Devices connected directly to Octeon
 *                   use zero. This is only used when the device
 *                   is full/low speed behind a high speed hub.
 *                   The address will be of the high speed hub,
 *                   not and full speed hubs after it.
 * @param hub_port   Which port on the hub the device is
 *                   connected. Use zero for devices connected
 *                   directly to Octeon. Like hub_device_addr,
 *                   this is only used for full/low speed
 *                   devices behind a high speed hub.
 *
 * @return A non negative value is a pipe handle. Negative
 *         values are failure codes from cvmx_usb_status_t.
 */
int cvmx_usb_open_pipe(cvmx_usb_state_t *state, cvmx_usb_pipe_flags_t flags,
                       int device_addr, int endpoint_num,
                       cvmx_usb_speed_t device_speed, int max_packet,
                       cvmx_usb_transfer_t transfer_type,
                       cvmx_usb_direction_t transfer_dir, int interval,
                       int multi_count, int hub_device_addr, int hub_port)
{
    cvmx_usb_pipe_t *pipe;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("0x%x", flags);
    CVMX_USB_LOG_PARAM("%d", device_addr);
    CVMX_USB_LOG_PARAM("%d", endpoint_num);
    CVMX_USB_LOG_PARAM("%d", device_speed);
    CVMX_USB_LOG_PARAM("%d", max_packet);
    CVMX_USB_LOG_PARAM("%d", transfer_type);
    CVMX_USB_LOG_PARAM("%d", transfer_dir);
    CVMX_USB_LOG_PARAM("%d", interval);
    CVMX_USB_LOG_PARAM("%d", multi_count);
    CVMX_USB_LOG_PARAM("%d", hub_device_addr);
    CVMX_USB_LOG_PARAM("%d", hub_port);

    if (cvmx_unlikely((device_addr < 0) || (device_addr > MAX_USB_ADDRESS)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((endpoint_num < 0) || (endpoint_num > MAX_USB_ENDPOINT)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(device_speed > CVMX_USB_SPEED_LOW))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((max_packet <= 0) || (max_packet > 1024)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(transfer_type > CVMX_USB_TRANSFER_INTERRUPT))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((transfer_dir != CVMX_USB_DIRECTION_OUT) &&
        (transfer_dir != CVMX_USB_DIRECTION_IN)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(interval < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((transfer_type == CVMX_USB_TRANSFER_CONTROL) && interval))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(multi_count < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((device_speed != CVMX_USB_SPEED_HIGH) &&
        (multi_count != 0)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((hub_device_addr < 0) || (hub_device_addr > MAX_USB_ADDRESS)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((hub_port < 0) || (hub_port > MAX_USB_HUB_PORT)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Find a free pipe */
    pipe = usb->free_pipes.head;
    if (!pipe)
        CVMX_USB_RETURN(CVMX_USB_NO_MEMORY);
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
    /* All pipes use interval to rate limit NAK processing. Force an interval
        if one wasn't supplied */
    if (!interval)
        interval = 1;
    if (__cvmx_usb_pipe_needs_split(usb, pipe))
    {
        pipe->interval = interval*8;
        /* Force start splits to be schedule on uFrame 0 */
        pipe->next_tx_frame = ((usb->frame_number+7)&~7) + pipe->interval;
    }
    else
    {
        pipe->interval = interval;
        pipe->next_tx_frame = usb->frame_number + pipe->interval;
    }
    pipe->multi_count = multi_count;
    pipe->hub_device_addr = hub_device_addr;
    pipe->hub_port = hub_port;
    pipe->pid_toggle = 0;
    pipe->split_sc_frame = -1;
    __cvmx_usb_append_pipe(&usb->idle_pipes, pipe);

    /* We don't need to tell the hardware about this pipe yet since
        it doesn't have any submitted requests */

    CVMX_USB_RETURN(__cvmx_usb_get_pipe_handle(usb, pipe));
}


/**
 * @INTERNAL
 * Poll the RX FIFOs and remove data as needed. This function is only used
 * in non DMA mode. It is very important that this function be called quickly
 * enough to prevent FIFO overflow.
 *
 * @param usb     USB device state populated by
 *                cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_rx_fifo(cvmx_usb_internal_state_t *usb)
{
    cvmx_usbcx_grxstsph_t rx_status;
    int channel;
    int bytes;
    uint64_t address;
    uint32_t *ptr;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);

    rx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GRXSTSPH(usb->index));
    /* Only read data if IN data is there */
    if (rx_status.s.pktsts != 2)
        CVMX_USB_RETURN_NOTHING();
    /* Check if no data is available */
    if (!rx_status.s.bcnt)
        CVMX_USB_RETURN_NOTHING();

    channel = rx_status.s.chnum;
    bytes = rx_status.s.bcnt;
    if (!bytes)
        CVMX_USB_RETURN_NOTHING();

    /* Get where the DMA engine would have written this data */
    address = __cvmx_usb_read_csr64(usb, CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8);
    ptr = cvmx_phys_to_ptr(address);
    __cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_INB_CHN0(usb->index) + channel*8, address + bytes);

    /* Loop writing the FIFO data for this packet into memory */
    while (bytes > 0)
    {
        *ptr++ = __cvmx_usb_read_csr32(usb, USB_FIFO_ADDRESS(channel, usb->index));
        bytes -= 4;
    }
    CVMX_SYNCW;

    CVMX_USB_RETURN_NOTHING();
}


/**
 * Fill the TX hardware fifo with data out of the software
 * fifos
 *
 * @param usb       USB device state populated by
 *                  cvmx_usb_initialize().
 * @param fifo      Software fifo to use
 * @param available Amount of space in the hardware fifo
 *
 * @return Non zero if the hardware fifo was too small and needs
 *         to be serviced again.
 */
static int __cvmx_usb_fill_tx_hw(cvmx_usb_internal_state_t *usb, cvmx_usb_tx_fifo_t *fifo, int available)
{
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%p", fifo);
    CVMX_USB_LOG_PARAM("%d", available);

    /* We're done either when there isn't anymore space or the software FIFO
        is empty */
    while (available && (fifo->head != fifo->tail))
    {
        int i = fifo->tail;
        const uint32_t *ptr = cvmx_phys_to_ptr(fifo->entry[i].address);
        uint64_t csr_address = USB_FIFO_ADDRESS(fifo->entry[i].channel, usb->index) ^ 4;
        int words = available;

        /* Limit the amount of data to waht the SW fifo has */
        if (fifo->entry[i].size <= available)
        {
            words = fifo->entry[i].size;
            fifo->tail++;
            if (fifo->tail > MAX_CHANNELS)
                fifo->tail = 0;
        }

        /* Update the next locations and counts */
        available -= words;
        fifo->entry[i].address += words * 4;
        fifo->entry[i].size -= words;

        /* Write the HW fifo data. The read every three writes is due
            to an errata on CN3XXX chips */
        while (words > 3)
        {
            cvmx_write64_uint32(csr_address, *ptr++);
            cvmx_write64_uint32(csr_address, *ptr++);
            cvmx_write64_uint32(csr_address, *ptr++);
            cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
            words -= 3;
        }
        cvmx_write64_uint32(csr_address, *ptr++);
        if (--words)
        {
            cvmx_write64_uint32(csr_address, *ptr++);
            if (--words)
                cvmx_write64_uint32(csr_address, *ptr++);
        }
        cvmx_read64_uint64(CVMX_USBNX_DMA0_INB_CHN0(usb->index));
    }
    CVMX_USB_RETURN(fifo->head != fifo->tail);
}


/**
 * Check the hardware FIFOs and fill them as needed
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 */
static void __cvmx_usb_poll_tx_fifo(cvmx_usb_internal_state_t *usb)
{
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);

    if (usb->periodic.head != usb->periodic.tail)
    {
        cvmx_usbcx_hptxsts_t tx_status;
        tx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPTXSTS(usb->index));
        if (__cvmx_usb_fill_tx_hw(usb, &usb->periodic, tx_status.s.ptxfspcavail))
            USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t, ptxfempmsk, 1);
        else
            USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t, ptxfempmsk, 0);
    }

    if (usb->nonperiodic.head != usb->nonperiodic.tail)
    {
        cvmx_usbcx_gnptxsts_t tx_status;
        tx_status.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_GNPTXSTS(usb->index));
        if (__cvmx_usb_fill_tx_hw(usb, &usb->nonperiodic, tx_status.s.nptxfspcavail))
            USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t, nptxfempmsk, 1);
        else
            USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t, nptxfempmsk, 0);
    }

    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Fill the TX FIFO with an outgoing packet
 *
 * @param usb     USB device state populated by
 *                cvmx_usb_initialize().
 * @param channel Channel number to get packet from
 */
static void __cvmx_usb_fill_tx_fifo(cvmx_usb_internal_state_t *usb, int channel)
{
    cvmx_usbcx_hccharx_t hcchar;
    cvmx_usbcx_hcspltx_t usbc_hcsplt;
    cvmx_usbcx_hctsizx_t usbc_hctsiz;
    cvmx_usb_tx_fifo_t *fifo;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%d", channel);

    /* We only need to fill data on outbound channels */
    hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));
    if (hcchar.s.epdir != CVMX_USB_DIRECTION_OUT)
        CVMX_USB_RETURN_NOTHING();

    /* OUT Splits only have data on the start and not the complete */
    usbc_hcsplt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCSPLTX(channel, usb->index));
    if (usbc_hcsplt.s.spltena && usbc_hcsplt.s.compsplt)
        CVMX_USB_RETURN_NOTHING();

    /* Find out how many bytes we need to fill and convert it into 32bit words */
    usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));
    if (!usbc_hctsiz.s.xfersize)
        CVMX_USB_RETURN_NOTHING();

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

    CVMX_USB_RETURN_NOTHING();
}

/**
 * @INTERNAL
 * Perform channel specific setup for Control transactions. All
 * the generic stuff will already have been done in
 * __cvmx_usb_start_channel()
 *
 * @param usb     USB device state populated by
 *                cvmx_usb_initialize().
 * @param channel Channel to setup
 * @param pipe    Pipe for control transaction
 */
static void __cvmx_usb_start_channel_control(cvmx_usb_internal_state_t *usb,
                                             int channel,
                                             cvmx_usb_pipe_t *pipe)
{
    cvmx_usb_transaction_t *transaction = pipe->head;
    cvmx_usb_control_header_t *header = cvmx_phys_to_ptr(transaction->control_header);
    int bytes_to_transfer = transaction->buffer_length - transaction->actual_bytes;
    int packets_to_transfer;
    cvmx_usbcx_hctsizx_t usbc_hctsiz;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%d", channel);
    CVMX_USB_LOG_PARAM("%p", pipe);

    usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));

    switch (transaction->stage)
    {
        case CVMX_USB_STAGE_NON_CONTROL:
        case CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE:
            cvmx_dprintf("%s: ERROR - Non control stage\n", __FUNCTION__);
            break;
        case CVMX_USB_STAGE_SETUP:
            usbc_hctsiz.s.pid = 3; /* Setup */
            bytes_to_transfer = sizeof(*header);
            /* All Control operations start with a setup going OUT */
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), cvmx_usbcx_hccharx_t, epdir, CVMX_USB_DIRECTION_OUT);
            /* Setup send the control header instead of the buffer data. The
                buffer data will be used in the next stage */
            __cvmx_usb_write_csr64(usb, CVMX_USBNX_DMA0_OUTB_CHN0(usb->index) + channel*8, transaction->control_header);
            break;
        case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
            usbc_hctsiz.s.pid = 3; /* Setup */
            bytes_to_transfer = 0;
            /* All Control operations start with a setup going OUT */
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), cvmx_usbcx_hccharx_t, epdir, CVMX_USB_DIRECTION_OUT);
            USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), cvmx_usbcx_hcspltx_t, compsplt, 1);
            break;
        case CVMX_USB_STAGE_DATA:
            usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
            if (__cvmx_usb_pipe_needs_split(usb, pipe))
            {
                if (header->s.request_type & 0x80)
                    bytes_to_transfer = 0;
                else if (bytes_to_transfer > pipe->max_packet)
                    bytes_to_transfer = pipe->max_packet;
            }
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
                            cvmx_usbcx_hccharx_t, epdir,
                            ((header->s.request_type & 0x80) ?
                             CVMX_USB_DIRECTION_IN :
                             CVMX_USB_DIRECTION_OUT));
            break;
        case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
            usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
            if (!(header->s.request_type & 0x80))
                bytes_to_transfer = 0;
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index),
                            cvmx_usbcx_hccharx_t, epdir,
                            ((header->s.request_type & 0x80) ?
                             CVMX_USB_DIRECTION_IN :
                             CVMX_USB_DIRECTION_OUT));
            USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), cvmx_usbcx_hcspltx_t, compsplt, 1);
            break;
        case CVMX_USB_STAGE_STATUS:
            usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
            bytes_to_transfer = 0;
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), cvmx_usbcx_hccharx_t, epdir,
                            ((header->s.request_type & 0x80) ?
                             CVMX_USB_DIRECTION_OUT :
                             CVMX_USB_DIRECTION_IN));
            break;
        case CVMX_USB_STAGE_STATUS_SPLIT_COMPLETE:
            usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
            bytes_to_transfer = 0;
            USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), cvmx_usbcx_hccharx_t, epdir,
                            ((header->s.request_type & 0x80) ?
                             CVMX_USB_DIRECTION_OUT :
                             CVMX_USB_DIRECTION_IN));
            USB_SET_FIELD32(CVMX_USBCX_HCSPLTX(channel, usb->index), cvmx_usbcx_hcspltx_t, compsplt, 1);
            break;
    }

    /* Make sure the transfer never exceeds the byte limit of the hardware.
        Further bytes will be sent as continued transactions */
    if (bytes_to_transfer > MAX_TRANSFER_BYTES)
    {
        /* Round MAX_TRANSFER_BYTES to a multiple of out packet size */
        bytes_to_transfer = MAX_TRANSFER_BYTES / pipe->max_packet;
        bytes_to_transfer *= pipe->max_packet;
    }

    /* Calculate the number of packets to transfer. If the length is zero
        we still need to transfer one packet */
    packets_to_transfer = (bytes_to_transfer + pipe->max_packet - 1) / pipe->max_packet;
    if (packets_to_transfer == 0)
        packets_to_transfer = 1;
    else if ((packets_to_transfer>1) && (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA))
    {
        /* Limit to one packet when not using DMA. Channels must be restarted
            between every packet for IN transactions, so there is no reason to
            do multiple packets in a row */
        packets_to_transfer = 1;
        bytes_to_transfer = packets_to_transfer * pipe->max_packet;
    }
    else if (packets_to_transfer > MAX_TRANSFER_PACKETS)
    {
        /* Limit the number of packet and data transferred to what the
            hardware can handle */
        packets_to_transfer = MAX_TRANSFER_PACKETS;
        bytes_to_transfer = packets_to_transfer * pipe->max_packet;
    }

    usbc_hctsiz.s.xfersize = bytes_to_transfer;
    usbc_hctsiz.s.pktcnt = packets_to_transfer;

    __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index), usbc_hctsiz.u32);
    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Start a channel to perform the pipe's head transaction
 *
 * @param usb     USB device state populated by
 *                cvmx_usb_initialize().
 * @param channel Channel to setup
 * @param pipe    Pipe to start
 */
static void __cvmx_usb_start_channel(cvmx_usb_internal_state_t *usb,
                                     int channel,
                                     cvmx_usb_pipe_t *pipe)
{
    cvmx_usb_transaction_t *transaction = pipe->head;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%d", channel);
    CVMX_USB_LOG_PARAM("%p", pipe);

    if (cvmx_unlikely((usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS) ||
        (pipe->flags & CVMX_USB_PIPE_FLAGS_DEBUG_TRANSFERS)))
        cvmx_dprintf("%s: Channel %d started. Pipe %d transaction %d stage %d\n",
                     __FUNCTION__, channel, __cvmx_usb_get_pipe_handle(usb, pipe),
                     __cvmx_usb_get_submit_handle(usb, transaction),
                     transaction->stage);

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
        cvmx_usbcx_hcintx_t usbc_hcint;
        cvmx_usbcx_hcintmskx_t usbc_hcintmsk;
        cvmx_usbcx_haintmsk_t usbc_haintmsk;

        /* Clear all channel status bits */
        usbc_hcint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index));
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index), usbc_hcint.u32);

        usbc_hcintmsk.u32 = 0;
        usbc_hcintmsk.s.chhltdmsk = 1;
        if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
        {
            /* Channels need these extra interrupts when we aren't in DMA mode */
            usbc_hcintmsk.s.datatglerrmsk = 1;
            usbc_hcintmsk.s.frmovrunmsk = 1;
            usbc_hcintmsk.s.bblerrmsk = 1;
            usbc_hcintmsk.s.xacterrmsk = 1;
            if (__cvmx_usb_pipe_needs_split(usb, pipe))
            {
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
        cvmx_usbcx_hcspltx_t usbc_hcsplt = {.u32 = 0};
        cvmx_usbcx_hctsizx_t usbc_hctsiz = {.u32 = 0};
        int packets_to_transfer;
        int bytes_to_transfer = transaction->buffer_length - transaction->actual_bytes;

        /* ISOCHRONOUS transactions store each individual transfer size in the
            packet structure, not the global buffer_length */
        if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
            bytes_to_transfer = transaction->iso_packets[0].length - transaction->actual_bytes;

        /* We need to do split transactions when we are talking to non high
            speed devices that are behind a high speed hub */
        if (__cvmx_usb_pipe_needs_split(usb, pipe))
        {
            /* On the start split phase (stage is even) record the frame number we
                will need to send the split complete. We only store the lower two bits
                since the time ahead can only be two frames */
            if ((transaction->stage&1) == 0)
            {
                if (transaction->type == CVMX_USB_TRANSFER_BULK)
                    pipe->split_sc_frame = (usb->frame_number + 1) & 0x7f;
                else
                    pipe->split_sc_frame = (usb->frame_number + 2) & 0x7f;
            }
            else
                pipe->split_sc_frame = -1;

            usbc_hcsplt.s.spltena = 1;
            usbc_hcsplt.s.hubaddr = pipe->hub_device_addr;
            usbc_hcsplt.s.prtaddr = pipe->hub_port;
            usbc_hcsplt.s.compsplt = (transaction->stage == CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE);

            /* SPLIT transactions can only ever transmit one data packet so
                limit the transfer size to the max packet size */
            if (bytes_to_transfer > pipe->max_packet)
                bytes_to_transfer = pipe->max_packet;

            /* ISOCHRONOUS OUT splits are unique in that they limit
                data transfers to 188 byte chunks representing the
                begin/middle/end of the data or all */
            if (!usbc_hcsplt.s.compsplt &&
                (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) &&
                (pipe->transfer_type == CVMX_USB_TRANSFER_ISOCHRONOUS))
            {
                /* Clear the split complete frame number as there isn't going
                    to be a split complete */
                pipe->split_sc_frame = -1;
                /* See if we've started this transfer and sent data */
                if (transaction->actual_bytes == 0)
                {
                    /* Nothing sent yet, this is either a begin or the
                        entire payload */
                    if (bytes_to_transfer <= 188)
                        usbc_hcsplt.s.xactpos = 3; /* Entire payload in one go */
                    else
                        usbc_hcsplt.s.xactpos = 2; /* First part of payload */
                }
                else
                {
                    /* Continuing the previous data, we must either be
                        in the middle or at the end */
                    if (bytes_to_transfer <= 188)
                        usbc_hcsplt.s.xactpos = 1; /* End of payload */
                    else
                        usbc_hcsplt.s.xactpos = 0; /* Middle of payload */
                }
                /* Again, the transfer size is limited to 188 bytes */
                if (bytes_to_transfer > 188)
                    bytes_to_transfer = 188;
            }
        }

        /* Make sure the transfer never exceeds the byte limit of the hardware.
            Further bytes will be sent as continued transactions */
        if (bytes_to_transfer > MAX_TRANSFER_BYTES)
        {
            /* Round MAX_TRANSFER_BYTES to a multiple of out packet size */
            bytes_to_transfer = MAX_TRANSFER_BYTES / pipe->max_packet;
            bytes_to_transfer *= pipe->max_packet;
        }

        /* Calculate the number of packets to transfer. If the length is zero
            we still need to transfer one packet */
        packets_to_transfer = (bytes_to_transfer + pipe->max_packet - 1) / pipe->max_packet;
        if (packets_to_transfer == 0)
            packets_to_transfer = 1;
        else if ((packets_to_transfer>1) && (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA))
        {
            /* Limit to one packet when not using DMA. Channels must be restarted
                between every packet for IN transactions, so there is no reason to
                do multiple packets in a row */
            packets_to_transfer = 1;
            bytes_to_transfer = packets_to_transfer * pipe->max_packet;
        }
        else if (packets_to_transfer > MAX_TRANSFER_PACKETS)
        {
            /* Limit the number of packet and data transferred to what the
                hardware can handle */
            packets_to_transfer = MAX_TRANSFER_PACKETS;
            bytes_to_transfer = packets_to_transfer * pipe->max_packet;
        }

        usbc_hctsiz.s.xfersize = bytes_to_transfer;
        usbc_hctsiz.s.pktcnt = packets_to_transfer;

        /* Update the DATA0/DATA1 toggle */
        usbc_hctsiz.s.pid = __cvmx_usb_get_data_pid(pipe);
        /* High speed pipes may need a hardware ping before they start */
        if (pipe->flags & __CVMX_USB_PIPE_FLAGS_NEED_PING)
            usbc_hctsiz.s.dopng = 1;

        __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCSPLTX(channel, usb->index), usbc_hcsplt.u32);
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index), usbc_hctsiz.u32);
    }

    /* Setup the Host Channel Characteristics Register */
    {
        cvmx_usbcx_hccharx_t usbc_hcchar = {.u32 = 0};

        /* Set the startframe odd/even properly. This is only used for periodic */
        usbc_hcchar.s.oddfrm = usb->frame_number&1;

        /* Set the number of back to back packets allowed by this endpoint.
            Split transactions interpret "ec" as the number of immediate
            retries of failure. These retries happen too quickly, so we
            disable these entirely for splits */
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
    switch (transaction->type)
    {
        case CVMX_USB_TRANSFER_CONTROL:
            __cvmx_usb_start_channel_control(usb, channel, pipe);
            break;
        case CVMX_USB_TRANSFER_BULK:
        case CVMX_USB_TRANSFER_INTERRUPT:
            break;
        case CVMX_USB_TRANSFER_ISOCHRONOUS:
            if (!__cvmx_usb_pipe_needs_split(usb, pipe))
            {
                /* ISO transactions require different PIDs depending on direction
                    and how many packets are needed */
                if (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT)
                {
                    if (pipe->multi_count < 2) /* Need DATA0 */
                        USB_SET_FIELD32(CVMX_USBCX_HCTSIZX(channel, usb->index), cvmx_usbcx_hctsizx_t, pid, 0);
                    else /* Need MDATA */
                        USB_SET_FIELD32(CVMX_USBCX_HCTSIZX(channel, usb->index), cvmx_usbcx_hctsizx_t, pid, 3);
                }
            }
            break;
    }
    {
        cvmx_usbcx_hctsizx_t usbc_hctsiz = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index))};
        transaction->xfersize = usbc_hctsiz.s.xfersize;
        transaction->pktcnt = usbc_hctsiz.s.pktcnt;
    }
    /* Remeber when we start a split transaction */
    if (__cvmx_usb_pipe_needs_split(usb, pipe))
        usb->active_split = transaction;
    USB_SET_FIELD32(CVMX_USBCX_HCCHARX(channel, usb->index), cvmx_usbcx_hccharx_t, chena, 1);
    if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
        __cvmx_usb_fill_tx_fifo(usb, channel);
    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Find a pipe that is ready to be scheduled to hardware.
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param list   Pipe list to search
 * @param current_frame
 *               Frame counter to use as a time reference.
 *
 * @return Pipe or NULL if none are ready
 */
static cvmx_usb_pipe_t *__cvmx_usb_find_ready_pipe(cvmx_usb_internal_state_t *usb, cvmx_usb_pipe_list_t *list, uint64_t current_frame)
{
    cvmx_usb_pipe_t *pipe = list->head;
    while (pipe)
    {
        if (!(pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED) && pipe->head &&
            (pipe->next_tx_frame <= current_frame) &&
            ((pipe->split_sc_frame == -1) || ((((int)current_frame - (int)pipe->split_sc_frame) & 0x7f) < 0x40)) &&
            (!usb->active_split || (usb->active_split == pipe->head)))
        {
            CVMX_PREFETCH(pipe, 128);
            CVMX_PREFETCH(pipe->head, 0);
            return pipe;
        }
        pipe = pipe->next;
    }
    return NULL;
}


/**
 * @INTERNAL
 * Called whenever a pipe might need to be scheduled to the
 * hardware.
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param is_sof True if this schedule was called on a SOF interrupt.
 */
static void __cvmx_usb_schedule(cvmx_usb_internal_state_t *usb, int is_sof)
{
    int channel;
    cvmx_usb_pipe_t *pipe;
    int need_sof;
    cvmx_usb_transfer_t ttype;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);

    if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
    {
        /* Without DMA we need to be careful to not schedule something at the end of a frame and cause an overrun */
        cvmx_usbcx_hfnum_t hfnum = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFNUM(usb->index))};
        cvmx_usbcx_hfir_t hfir = {.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFIR(usb->index))};
        if (hfnum.s.frrem < hfir.s.frint/4)
            goto done;
    }

    while (usb->idle_hardware_channels)
    {
        /* Find an idle channel */
        CVMX_CLZ(channel, usb->idle_hardware_channels);
        channel = 31 - channel;
        if (cvmx_unlikely(channel > 7))
        {
            if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO))
                cvmx_dprintf("%s: Idle hardware channels has a channel higher than 7. This is wrong\n", __FUNCTION__);
            break;
        }

        /* Find a pipe needing service */
        pipe = NULL;
        if (is_sof)
        {
            /* Only process periodic pipes on SOF interrupts. This way we are
                sure that the periodic data is sent in the beginning of the
                frame */
            pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_ISOCHRONOUS, usb->frame_number);
            if (cvmx_likely(!pipe))
                pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_INTERRUPT, usb->frame_number);
        }
        if (cvmx_likely(!pipe))
        {
            pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_CONTROL, usb->frame_number);
            if (cvmx_likely(!pipe))
                pipe = __cvmx_usb_find_ready_pipe(usb, usb->active_pipes + CVMX_USB_TRANSFER_BULK, usb->frame_number);
        }
        if (!pipe)
            break;

        CVMX_USB_LOG_PARAM("%d", channel);
        CVMX_USB_LOG_PARAM("%p", pipe);

        if (cvmx_unlikely((usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS) ||
            (pipe->flags & CVMX_USB_PIPE_FLAGS_DEBUG_TRANSFERS)))
        {
            cvmx_usb_transaction_t *transaction = pipe->head;
            const cvmx_usb_control_header_t *header = (transaction->control_header) ? cvmx_phys_to_ptr(transaction->control_header) : NULL;
            const char *dir = (pipe->transfer_dir == CVMX_USB_DIRECTION_IN) ? "IN" : "OUT";
            const char *type;
            switch (pipe->transfer_type)
            {
                case CVMX_USB_TRANSFER_CONTROL:
                    type = "SETUP";
                    dir = (header->s.request_type & 0x80) ? "IN" : "OUT";
                    break;
                case CVMX_USB_TRANSFER_ISOCHRONOUS:
                    type = "ISOCHRONOUS";
                    break;
                case CVMX_USB_TRANSFER_BULK:
                    type = "BULK";
                    break;
                default: /* CVMX_USB_TRANSFER_INTERRUPT */
                    type = "INTERRUPT";
                    break;
            }
            cvmx_dprintf("%s: Starting pipe %d, transaction %d on channel %d. %s %s len=%d header=0x%llx\n",
                         __FUNCTION__, __cvmx_usb_get_pipe_handle(usb, pipe),
                         __cvmx_usb_get_submit_handle(usb, transaction),
                         channel, type, dir,
                         transaction->buffer_length,
                         (header) ? (unsigned long long)header->u64 : 0ull);
        }
        __cvmx_usb_start_channel(usb, channel, pipe);
    }

done:
    /* Only enable SOF interrupts when we have transactions pending in the
        future that might need to be scheduled */
    need_sof = 0;
    for (ttype=CVMX_USB_TRANSFER_CONTROL; ttype<=CVMX_USB_TRANSFER_INTERRUPT; ttype++)
    {
        pipe = usb->active_pipes[ttype].head;
        while (pipe)
        {
            if (pipe->next_tx_frame > usb->frame_number)
            {
                need_sof = 1;
                break;
            }
            pipe=pipe->next;
        }
    }
    USB_SET_FIELD32(CVMX_USBCX_GINTMSK(usb->index), cvmx_usbcx_gintmsk_t, sofmsk, need_sof);
    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Call a user's callback for a specific reason.
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe   Pipe the callback is for or NULL
 * @param transaction
 *               Transaction the callback is for or NULL
 * @param reason Reason this callback is being called
 * @param complete_code
 *               Completion code for the transaction, if any
 */
static void __cvmx_usb_perform_callback(cvmx_usb_internal_state_t *usb,
                                        cvmx_usb_pipe_t *pipe,
                                        cvmx_usb_transaction_t *transaction,
                                        cvmx_usb_callback_t reason,
                                        cvmx_usb_complete_t complete_code)
{
    cvmx_usb_callback_func_t callback = usb->callback[reason];
    void *user_data = usb->callback_data[reason];
    int submit_handle = -1;
    int pipe_handle = -1;
    int bytes_transferred = 0;

    if (pipe)
        pipe_handle = __cvmx_usb_get_pipe_handle(usb, pipe);

    if (transaction)
    {
        submit_handle = __cvmx_usb_get_submit_handle(usb, transaction);
        bytes_transferred = transaction->actual_bytes;
        /* Transactions are allowed to override the default callback */
        if ((reason == CVMX_USB_CALLBACK_TRANSFER_COMPLETE) && transaction->callback)
        {
            callback = transaction->callback;
            user_data = transaction->callback_data;
        }
    }

    if (!callback)
        return;

    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLBACKS))
        cvmx_dprintf("%*s%s: calling callback %p(usb=%p, complete_code=%s, "
                     "pipe_handle=%d, submit_handle=%d, bytes_transferred=%d, user_data=%p);\n",
                     2*usb->indent, "", __FUNCTION__, callback, usb,
                     __cvmx_usb_complete_to_string(complete_code),
                     pipe_handle, submit_handle, bytes_transferred, user_data);

    callback((cvmx_usb_state_t *)usb, reason, complete_code, pipe_handle, submit_handle,
             bytes_transferred, user_data);

    if (cvmx_unlikely(usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLBACKS))
        cvmx_dprintf("%*s%s: callback %p complete\n", 2*usb->indent, "",
                      __FUNCTION__, callback);
}


/**
 * @INTERNAL
 * Signal the completion of a transaction and free it. The
 * transaction will be removed from the pipe transaction list.
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe   Pipe the transaction is on
 * @param transaction
 *               Transaction that completed
 * @param complete_code
 *               Completion code
 */
static void __cvmx_usb_perform_complete(cvmx_usb_internal_state_t * usb,
                                        cvmx_usb_pipe_t *pipe,
                                        cvmx_usb_transaction_t *transaction,
                                        cvmx_usb_complete_t complete_code)
{
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%p", pipe);
    CVMX_USB_LOG_PARAM("%p", transaction);
    CVMX_USB_LOG_PARAM("%d", complete_code);

    /* If this was a split then clear our split in progress marker */
    if (usb->active_split == transaction)
        usb->active_split = NULL;

    /* Isochronous transactions need extra processing as they might not be done
        after a single data transfer */
    if (cvmx_unlikely(transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS))
    {
        /* Update the number of bytes transferred in this ISO packet */
        transaction->iso_packets[0].length = transaction->actual_bytes;
        transaction->iso_packets[0].status = complete_code;

        /* If there are more ISOs pending and we succeeded, schedule the next
            one */
        if ((transaction->iso_number_packets > 1) && (complete_code == CVMX_USB_COMPLETE_SUCCESS))
        {
            transaction->actual_bytes = 0;      /* No bytes transferred for this packet as of yet */
            transaction->iso_number_packets--;  /* One less ISO waiting to transfer */
            transaction->iso_packets++;         /* Increment to the next location in our packet array */
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
    if (!pipe->head)
    {
        __cvmx_usb_remove_pipe(usb->active_pipes + pipe->transfer_type, pipe);
        __cvmx_usb_append_pipe(&usb->idle_pipes, pipe);

    }
    __cvmx_usb_perform_callback(usb, pipe, transaction,
                                CVMX_USB_CALLBACK_TRANSFER_COMPLETE,
                                complete_code);
    __cvmx_usb_free_transaction(usb, transaction);
done:
    CVMX_USB_RETURN_NOTHING();
}


/**
 * @INTERNAL
 * Submit a usb transaction to a pipe. Called for all types
 * of transactions.
 *
 * @param usb
 * @param pipe_handle
 *                  Which pipe to submit to. Will be validated in this function.
 * @param type      Transaction type
 * @param flags     Flags for the transaction
 * @param buffer    User buffer for the transaction
 * @param buffer_length
 *                  User buffer's length in bytes
 * @param control_header
 *                  For control transactions, the 8 byte standard header
 * @param iso_start_frame
 *                  For ISO transactions, the start frame
 * @param iso_number_packets
 *                  For ISO, the number of packet in the transaction.
 * @param iso_packets
 *                  A description of each ISO packet
 * @param callback  User callback to call when the transaction completes
 * @param user_data User's data for the callback
 *
 * @return Submit handle or negative on failure. Matches the result
 *         in the external API.
 */
static int __cvmx_usb_submit_transaction(cvmx_usb_internal_state_t *usb,
                                         int pipe_handle,
                                         cvmx_usb_transfer_t type,
                                         int flags,
                                         uint64_t buffer,
                                         int buffer_length,
                                         uint64_t control_header,
                                         int iso_start_frame,
                                         int iso_number_packets,
                                         cvmx_usb_iso_packet_t *iso_packets,
                                         cvmx_usb_callback_func_t callback,
                                         void *user_data)
{
    int submit_handle;
    cvmx_usb_transaction_t *transaction;
    cvmx_usb_pipe_t *pipe = usb->pipe + pipe_handle;

    CVMX_USB_LOG_CALLED();
    if (cvmx_unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    /* Fail if the pipe isn't open */
    if (cvmx_unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(pipe->transfer_type != type))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    transaction = __cvmx_usb_alloc_transaction(usb);
    if (cvmx_unlikely(!transaction))
        CVMX_USB_RETURN(CVMX_USB_NO_MEMORY);

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
    if (pipe->tail)
    {
        transaction->prev = pipe->tail;
        transaction->prev->next = transaction;
    }
    else
    {
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

    CVMX_USB_RETURN(submit_handle);
}


/**
 * Call to submit a USB Bulk transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
int cvmx_usb_submit_bulk(cvmx_usb_state_t *state, int pipe_handle,
                                uint64_t buffer, int buffer_length,
                                cvmx_usb_callback_func_t callback,
                                void *user_data)
{
    int submit_handle;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    CVMX_USB_LOG_PARAM("0x%llx", (unsigned long long)buffer);
    CVMX_USB_LOG_PARAM("%d", buffer_length);

    /* Pipe handle checking is done later in a common place */
    if (cvmx_unlikely(!buffer))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(buffer_length < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

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
    CVMX_USB_RETURN(submit_handle);
}


/**
 * Call to submit a USB Interrupt transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
int cvmx_usb_submit_interrupt(cvmx_usb_state_t *state, int pipe_handle,
                              uint64_t buffer, int buffer_length,
                              cvmx_usb_callback_func_t callback,
                              void *user_data)
{
    int submit_handle;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    CVMX_USB_LOG_PARAM("0x%llx", (unsigned long long)buffer);
    CVMX_USB_LOG_PARAM("%d", buffer_length);

    /* Pipe handle checking is done later in a common place */
    if (cvmx_unlikely(!buffer))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(buffer_length < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

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
    CVMX_USB_RETURN(submit_handle);
}


/**
 * Call to submit a USB Control transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param control_header
 *                  USB 8 byte control header physical address.
 *                  Note that this is NOT A POINTER, but the
 *                  full 64bit physical address of the buffer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
int cvmx_usb_submit_control(cvmx_usb_state_t *state, int pipe_handle,
                            uint64_t control_header,
                            uint64_t buffer, int buffer_length,
                            cvmx_usb_callback_func_t callback,
                            void *user_data)
{
    int submit_handle;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    cvmx_usb_control_header_t *header = cvmx_phys_to_ptr(control_header);

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    CVMX_USB_LOG_PARAM("0x%llx", (unsigned long long)control_header);
    CVMX_USB_LOG_PARAM("0x%llx", (unsigned long long)buffer);
    CVMX_USB_LOG_PARAM("%d", buffer_length);

    /* Pipe handle checking is done later in a common place */
    if (cvmx_unlikely(!control_header))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    /* Some drivers send a buffer with a zero length. God only knows why */
    if (cvmx_unlikely(buffer && (buffer_length < 0)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(!buffer && (buffer_length != 0)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if ((header->s.request_type & 0x80) == 0)
        buffer_length = cvmx_le16_to_cpu(header->s.length);

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
    CVMX_USB_RETURN(submit_handle);
}


/**
 * Call to submit a USB Isochronous transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param start_frame
 *                  Number of frames into the future to schedule
 *                  this transaction.
 * @param flags     Flags to control the transfer. See
 *                  cvmx_usb_isochronous_flags_t for the flag
 *                  definitions.
 * @param number_packets
 *                  Number of sequential packets to transfer.
 *                  "packets" is a pointer to an array of this
 *                  many packet structures.
 * @param packets   Description of each transfer packet as
 *                  defined by cvmx_usb_iso_packet_t. The array
 *                  pointed to here must stay valid until the
 *                  complete callback is called.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
int cvmx_usb_submit_isochronous(cvmx_usb_state_t *state, int pipe_handle,
                                int start_frame, int flags,
                                int number_packets,
                                cvmx_usb_iso_packet_t packets[],
                                uint64_t buffer, int buffer_length,
                                cvmx_usb_callback_func_t callback,
                                void *user_data)
{
    int submit_handle;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    CVMX_USB_LOG_PARAM("%d", start_frame);
    CVMX_USB_LOG_PARAM("0x%x", flags);
    CVMX_USB_LOG_PARAM("%d", number_packets);
    CVMX_USB_LOG_PARAM("%p", packets);
    CVMX_USB_LOG_PARAM("0x%llx", (unsigned long long)buffer);
    CVMX_USB_LOG_PARAM("%d", buffer_length);

    /* Pipe handle checking is done later in a common place */
    if (cvmx_unlikely(start_frame < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(flags & ~(CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT | CVMX_USB_ISOCHRONOUS_FLAGS_ASAP)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(number_packets < 1))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(!packets))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(!buffer))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(buffer_length < 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

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
    CVMX_USB_RETURN(submit_handle);
}


/**
 * Cancel one outstanding request in a pipe. Canceling a request
 * can fail if the transaction has already completed before cancel
 * is called. Even after a successful cancel call, it may take
 * a frame or two for the cvmx_usb_poll() function to call the
 * associated callback.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to cancel requests in.
 * @param submit_handle
 *               Handle to transaction to cancel, returned by the submit function.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_cancel(cvmx_usb_state_t *state, int pipe_handle,
                                  int submit_handle)
{
    cvmx_usb_transaction_t *transaction;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    cvmx_usb_pipe_t *pipe = usb->pipe + pipe_handle;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    CVMX_USB_LOG_PARAM("%d", submit_handle);

    if (cvmx_unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely((submit_handle < 0) || (submit_handle >= MAX_TRANSACTIONS)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Fail if the pipe isn't open */
    if (cvmx_unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    transaction = usb->transaction + submit_handle;

    /* Fail if this transaction already completed */
    if (cvmx_unlikely((transaction->flags & __CVMX_USB_TRANSACTION_FLAGS_IN_USE) == 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* If the transaction is the HEAD of the queue and scheduled. We need to
        treat it special */
    if ((pipe->head == transaction) &&
        (pipe->flags & __CVMX_USB_PIPE_FLAGS_SCHEDULED))
    {
        cvmx_usbcx_hccharx_t usbc_hcchar;

        usb->pipe_for_channel[pipe->channel] = NULL;
        pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

        CVMX_SYNCW;

        usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(pipe->channel, usb->index));
        /* If the channel isn't enabled then the transaction already completed */
        if (usbc_hcchar.s.chena)
        {
            usbc_hcchar.s.chdis = 1;
            __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(pipe->channel, usb->index), usbc_hcchar.u32);
        }
    }
    __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_CANCEL);
    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Cancel all outstanding requests in a pipe. Logically all this
 * does is call cvmx_usb_cancel() in a loop.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to cancel requests in.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_cancel_all(cvmx_usb_state_t *state, int pipe_handle)
{
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    cvmx_usb_pipe_t *pipe = usb->pipe + pipe_handle;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    if (cvmx_unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Fail if the pipe isn't open */
    if (cvmx_unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Simply loop through and attempt to cancel each transaction */
    while (pipe->head)
    {
        cvmx_usb_status_t result = cvmx_usb_cancel(state, pipe_handle,
            __cvmx_usb_get_submit_handle(usb, pipe->head));
        if (cvmx_unlikely(result != CVMX_USB_SUCCESS))
            CVMX_USB_RETURN(result);
    }
    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Close a pipe created with cvmx_usb_open_pipe().
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to close.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t. CVMX_USB_BUSY is returned if the
 *         pipe has outstanding transfers.
 */
cvmx_usb_status_t cvmx_usb_close_pipe(cvmx_usb_state_t *state, int pipe_handle)
{
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    cvmx_usb_pipe_t *pipe = usb->pipe + pipe_handle;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", pipe_handle);
    if (cvmx_unlikely((pipe_handle < 0) || (pipe_handle >= MAX_PIPES)))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Fail if the pipe isn't open */
    if (cvmx_unlikely((pipe->flags & __CVMX_USB_PIPE_FLAGS_OPEN) == 0))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    /* Fail if the pipe has pending transactions */
    if (cvmx_unlikely(pipe->head))
        CVMX_USB_RETURN(CVMX_USB_BUSY);

    pipe->flags = 0;
    __cvmx_usb_remove_pipe(&usb->idle_pipes, pipe);
    __cvmx_usb_append_pipe(&usb->free_pipes, pipe);

    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Register a function to be called when various USB events occur.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param reason    Which event to register for.
 * @param callback  Function to call when the event occurs.
 * @param user_data User data parameter to the function.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_register_callback(cvmx_usb_state_t *state,
                                             cvmx_usb_callback_t reason,
                                             cvmx_usb_callback_func_t callback,
                                             void *user_data)
{
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);
    CVMX_USB_LOG_PARAM("%d", reason);
    CVMX_USB_LOG_PARAM("%p", callback);
    CVMX_USB_LOG_PARAM("%p", user_data);
    if (cvmx_unlikely(reason >= __CVMX_USB_CALLBACK_END))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);
    if (cvmx_unlikely(!callback))
        CVMX_USB_RETURN(CVMX_USB_INVALID_PARAM);

    usb->callback[reason] = callback;
    usb->callback_data[reason] = user_data;

    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}


/**
 * Get the current USB protocol level frame number. The frame
 * number is always in the range of 0-0x7ff.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return USB frame number
 */
int cvmx_usb_get_frame_number(cvmx_usb_state_t *state)
{
    int frame_number;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;
    cvmx_usbcx_hfnum_t usbc_hfnum;

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

    usbc_hfnum.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HFNUM(usb->index));
    frame_number = usbc_hfnum.s.frnum;

    CVMX_USB_RETURN(frame_number);
}


/**
 * @INTERNAL
 * Poll a channel for status
 *
 * @param usb     USB device
 * @param channel Channel to poll
 *
 * @return Zero on success
 */
static int __cvmx_usb_poll_channel(cvmx_usb_internal_state_t *usb, int channel)
{
    cvmx_usbcx_hcintx_t usbc_hcint;
    cvmx_usbcx_hctsizx_t usbc_hctsiz;
    cvmx_usbcx_hccharx_t usbc_hcchar;
    cvmx_usb_pipe_t *pipe;
    cvmx_usb_transaction_t *transaction;
    int bytes_this_transfer;
    int bytes_in_last_packet;
    int packets_processed;
    int buffer_space_left;
    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", usb);
    CVMX_USB_LOG_PARAM("%d", channel);

    /* Read the interrupt status bits for the channel */
    usbc_hcint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCINTX(channel, usb->index));

    if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
    {
        usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));

        if (usbc_hcchar.s.chena && usbc_hcchar.s.chdis)
        {
            /* There seems to be a bug in CN31XX which can cause interrupt
                IN transfers to get stuck until we do a write of HCCHARX
                without changing things */
            __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index), usbc_hcchar.u32);
            CVMX_USB_RETURN(0);
        }

        /* In non DMA mode the channels don't halt themselves. We need to
            manually disable channels that are left running */
        if (!usbc_hcint.s.chhltd)
        {
            if (usbc_hcchar.s.chena)
            {
                cvmx_usbcx_hcintmskx_t hcintmsk;
                /* Disable all interrupts except CHHLTD */
                hcintmsk.u32 = 0;
                hcintmsk.s.chhltdmsk = 1;
                __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), hcintmsk.u32);
                usbc_hcchar.s.chdis = 1;
                __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index), usbc_hcchar.u32);
                CVMX_USB_RETURN(0);
            }
            else if (usbc_hcint.s.xfercompl)
            {
                /* Successful IN/OUT with transfer complete. Channel halt isn't needed */
            }
            else
            {
                cvmx_dprintf("USB%d: Channel %d interrupt without halt\n", usb->index, channel);
                CVMX_USB_RETURN(0);
            }
        }
    }
    else
    {
        /* There is are no interrupts that we need to process when the channel is
            still running */
        if (!usbc_hcint.s.chhltd)
            CVMX_USB_RETURN(0);
    }

    /* Disable the channel interrupts now that it is done */
    __cvmx_usb_write_csr32(usb, CVMX_USBCX_HCINTMSKX(channel, usb->index), 0);
    usb->idle_hardware_channels |= (1<<channel);

    /* Make sure this channel is tied to a valid pipe */
    pipe = usb->pipe_for_channel[channel];
    CVMX_PREFETCH(pipe, 0);
    CVMX_PREFETCH(pipe, 128);
    if (!pipe)
        CVMX_USB_RETURN(0);
    transaction = pipe->head;
    CVMX_PREFETCH0(transaction);

    /* Disconnect this pipe from the HW channel. Later the schedule function will
        figure out which pipe needs to go */
    usb->pipe_for_channel[channel] = NULL;
    pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_SCHEDULED;

    /* Read the channel config info so we can figure out how much data
        transfered */
    usbc_hcchar.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCCHARX(channel, usb->index));
    usbc_hctsiz.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HCTSIZX(channel, usb->index));

    /* Calculating the number of bytes successfully transferred is dependent on
        the transfer direction */
    packets_processed = transaction->pktcnt - usbc_hctsiz.s.pktcnt;
    if (usbc_hcchar.s.epdir)
    {
        /* IN transactions are easy. For every byte received the hardware
            decrements xfersize. All we need to do is subtract the current
            value of xfersize from its starting value and we know how many
            bytes were written to the buffer */
        bytes_this_transfer = transaction->xfersize - usbc_hctsiz.s.xfersize;
    }
    else
    {
        /* OUT transaction don't decrement xfersize. Instead pktcnt is
            decremented on every successful packet send. The hardware does
            this when it receives an ACK, or NYET. If it doesn't
            receive one of these responses pktcnt doesn't change */
        bytes_this_transfer = packets_processed * usbc_hcchar.s.mps;
        /* The last packet may not be a full transfer if we didn't have
            enough data */
        if (bytes_this_transfer > transaction->xfersize)
            bytes_this_transfer = transaction->xfersize;
    }
    /* Figure out how many bytes were in the last packet of the transfer */
    if (packets_processed)
        bytes_in_last_packet = bytes_this_transfer - (packets_processed-1) * usbc_hcchar.s.mps;
    else
        bytes_in_last_packet = bytes_this_transfer;

    /* As a special case, setup transactions output the setup header, not
        the user's data. For this reason we don't count setup data as bytes
        transferred */
    if ((transaction->stage == CVMX_USB_STAGE_SETUP) ||
        (transaction->stage == CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE))
        bytes_this_transfer = 0;

    /* Optional debug output */
    if (cvmx_unlikely((usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS) ||
        (pipe->flags & CVMX_USB_PIPE_FLAGS_DEBUG_TRANSFERS)))
        cvmx_dprintf("%s: Channel %d halted. Pipe %d transaction %d stage %d bytes=%d\n",
                     __FUNCTION__, channel,
                     __cvmx_usb_get_pipe_handle(usb, pipe),
                     __cvmx_usb_get_submit_handle(usb, transaction),
                     transaction->stage, bytes_this_transfer);

    /* Add the bytes transferred to the running total. It is important that
        bytes_this_transfer doesn't count any data that needs to be
        retransmitted */
    transaction->actual_bytes += bytes_this_transfer;
    if (transaction->type == CVMX_USB_TRANSFER_ISOCHRONOUS)
        buffer_space_left = transaction->iso_packets[0].length - transaction->actual_bytes;
    else
        buffer_space_left = transaction->buffer_length - transaction->actual_bytes;

    /* We need to remember the PID toggle state for the next transaction. The
        hardware already updated it for the next transaction */
    pipe->pid_toggle = !(usbc_hctsiz.s.pid == 0);

    /* For high speed bulk out, assume the next transaction will need to do a
        ping before proceeding. If this isn't true the ACK processing below
        will clear this flag */
    if ((pipe->device_speed == CVMX_USB_SPEED_HIGH) &&
        (pipe->transfer_type == CVMX_USB_TRANSFER_BULK) &&
        (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT))
        pipe->flags |= __CVMX_USB_PIPE_FLAGS_NEED_PING;

    if (usbc_hcint.s.stall)
    {
        /* STALL as a response means this transaction cannot be completed
            because the device can't process transactions. Tell the user. Any
            data that was transferred will be counted on the actual bytes
            transferred */
        pipe->pid_toggle = 0;
        __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_STALL);
    }
    else if (usbc_hcint.s.xacterr)
    {
        /* We know at least one packet worked if we get a ACK or NAK. Reset the retry counter */
        if (usbc_hcint.s.nak || usbc_hcint.s.ack)
            transaction->retries = 0;
        transaction->retries++;
        if (transaction->retries > MAX_RETRIES)
        {
            /* XactErr as a response means the device signaled something wrong with
                the transfer. For example, PID toggle errors cause these */
            __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_XACTERR);
        }
        else
        {
            /* If this was a split then clear our split in progress marker */
            if (usb->active_split == transaction)
                usb->active_split = NULL;
            /* Rewind to the beginning of the transaction by anding off the
                split complete bit */
            transaction->stage &= ~1;
            pipe->split_sc_frame = -1;
            pipe->next_tx_frame += pipe->interval;
            if (pipe->next_tx_frame < usb->frame_number)
                pipe->next_tx_frame = usb->frame_number + pipe->interval -
                    (usb->frame_number - pipe->next_tx_frame) % pipe->interval;
        }
    }
    else if (usbc_hcint.s.bblerr)
    {
        /* Babble Error (BblErr) */
        __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_BABBLEERR);
    }
    else if (usbc_hcint.s.datatglerr)
    {
        /* We'll retry the exact same transaction again */
        transaction->retries++;
    }
    else if (usbc_hcint.s.nyet)
    {
        /* NYET as a response is only allowed in three cases: as a response to
            a ping, as a response to a split transaction, and as a response to
            a bulk out. The ping case is handled by hardware, so we only have
            splits and bulk out */
        if (!__cvmx_usb_pipe_needs_split(usb, pipe))
        {
            transaction->retries = 0;
            /* If there is more data to go then we need to try again. Otherwise
                this transaction is complete */
            if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet))
                __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
        }
        else
        {
            /* Split transactions retry the split complete 4 times then rewind
                to the start split and do the entire transactions again */
            transaction->retries++;
            if ((transaction->retries & 0x3) == 0)
            {
                /* Rewind to the beginning of the transaction by anding off the
                    split complete bit */
                transaction->stage &= ~1;
                pipe->split_sc_frame = -1;
            }
        }
    }
    else if (usbc_hcint.s.ack)
    {
        transaction->retries = 0;
        /* The ACK bit can only be checked after the other error bits. This is
            because a multi packet transfer may succeed in a number of packets
            and then get a different response on the last packet. In this case
            both ACK and the last response bit will be set. If none of the
            other response bits is set, then the last packet must have been an
            ACK */

        /* Since we got an ACK, we know we don't need to do a ping on this
            pipe */
        pipe->flags &= ~__CVMX_USB_PIPE_FLAGS_NEED_PING;

        switch (transaction->type)
        {
            case CVMX_USB_TRANSFER_CONTROL:
                switch (transaction->stage)
                {
                    case CVMX_USB_STAGE_NON_CONTROL:
                    case CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE:
                        /* This should be impossible */
                        __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_ERROR);
                        break;
                    case CVMX_USB_STAGE_SETUP:
                        pipe->pid_toggle = 1;
                        if (__cvmx_usb_pipe_needs_split(usb, pipe))
                            transaction->stage = CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE;
                        else
                        {
                            cvmx_usb_control_header_t *header = cvmx_phys_to_ptr(transaction->control_header);
                            if (header->s.length)
                                transaction->stage = CVMX_USB_STAGE_DATA;
                            else
                                transaction->stage = CVMX_USB_STAGE_STATUS;
                        }
                        break;
                    case CVMX_USB_STAGE_SETUP_SPLIT_COMPLETE:
                        {
                            cvmx_usb_control_header_t *header = cvmx_phys_to_ptr(transaction->control_header);
                            if (header->s.length)
                                transaction->stage = CVMX_USB_STAGE_DATA;
                            else
                                transaction->stage = CVMX_USB_STAGE_STATUS;
                        }
                        break;
                    case CVMX_USB_STAGE_DATA:
                        if (__cvmx_usb_pipe_needs_split(usb, pipe))
                        {
                            transaction->stage = CVMX_USB_STAGE_DATA_SPLIT_COMPLETE;
                            /* For setup OUT data that are splits, the hardware
                                doesn't appear to count transferred data. Here
                                we manually update the data transferred */
                            if (!usbc_hcchar.s.epdir)
                            {
                                if (buffer_space_left < pipe->max_packet)
                                    transaction->actual_bytes += buffer_space_left;
                                else
                                    transaction->actual_bytes += pipe->max_packet;
                            }
                        }
                        else if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet))
                        {
                            pipe->pid_toggle = 1;
                            transaction->stage = CVMX_USB_STAGE_STATUS;
                        }
                        break;
                    case CVMX_USB_STAGE_DATA_SPLIT_COMPLETE:
                        if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet))
                        {
                            pipe->pid_toggle = 1;
                            transaction->stage = CVMX_USB_STAGE_STATUS;
                        }
                        else
                        {
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
                /* The only time a bulk transfer isn't complete when
                    it finishes with an ACK is during a split transaction. For
                    splits we need to continue the transfer if more data is
                    needed */
                if (__cvmx_usb_pipe_needs_split(usb, pipe))
                {
                    if (transaction->stage == CVMX_USB_STAGE_NON_CONTROL)
                        transaction->stage = CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
                    else
                    {
                        if (buffer_space_left && (bytes_in_last_packet == pipe->max_packet))
                            transaction->stage = CVMX_USB_STAGE_NON_CONTROL;
                        else
                        {
                            if (transaction->type == CVMX_USB_TRANSFER_INTERRUPT)
                                pipe->next_tx_frame += pipe->interval;
                            __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
                        }
                    }
                }
                else
                {
                    if ((pipe->device_speed == CVMX_USB_SPEED_HIGH) &&
                        (pipe->transfer_type == CVMX_USB_TRANSFER_BULK) &&
                        (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT) &&
                        (usbc_hcint.s.nak))
                        pipe->flags |= __CVMX_USB_PIPE_FLAGS_NEED_PING;
                    if (!buffer_space_left || (bytes_in_last_packet < pipe->max_packet))
                    {
                        if (transaction->type == CVMX_USB_TRANSFER_INTERRUPT)
                            pipe->next_tx_frame += pipe->interval;
                        __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
                    }
                }
                break;
            case CVMX_USB_TRANSFER_ISOCHRONOUS:
                if (__cvmx_usb_pipe_needs_split(usb, pipe))
                {
                    /* ISOCHRONOUS OUT splits don't require a complete split stage.
                        Instead they use a sequence of begin OUT splits to transfer
                        the data 188 bytes at a time. Once the transfer is complete,
                        the pipe sleeps until the next schedule interval */
                    if (pipe->transfer_dir == CVMX_USB_DIRECTION_OUT)
                    {
                        /* If no space left or this wasn't a max size packet then
                            this transfer is complete. Otherwise start it again
                            to send the next 188 bytes */
                        if (!buffer_space_left || (bytes_this_transfer < 188))
                        {
                            pipe->next_tx_frame += pipe->interval;
                            __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
                        }
                    }
                    else
                    {
                        if (transaction->stage == CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE)
                        {
                            /* We are in the incoming data phase. Keep getting
                                data until we run out of space or get a small
                                packet */
                            if ((buffer_space_left == 0) || (bytes_in_last_packet < pipe->max_packet))
                            {
                                pipe->next_tx_frame += pipe->interval;
                                __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
                            }
                        }
                        else
                            transaction->stage = CVMX_USB_STAGE_NON_CONTROL_SPLIT_COMPLETE;
                    }
                }
                else
                {
                    pipe->next_tx_frame += pipe->interval;
                    __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_SUCCESS);
                }
                break;
        }
    }
    else if (usbc_hcint.s.nak)
    {
        /* If this was a split then clear our split in progress marker */
        if (usb->active_split == transaction)
            usb->active_split = NULL;
        /* NAK as a response means the device couldn't accept the transaction,
            but it should be retried in the future. Rewind to the beginning of
            the transaction by anding off the split complete bit. Retry in the
            next interval */
        transaction->retries = 0;
        transaction->stage &= ~1;
        pipe->next_tx_frame += pipe->interval;
        if (pipe->next_tx_frame < usb->frame_number)
            pipe->next_tx_frame = usb->frame_number + pipe->interval -
                (usb->frame_number - pipe->next_tx_frame) % pipe->interval;
    }
    else
    {
        cvmx_usb_port_status_t port;
        port = cvmx_usb_get_status((cvmx_usb_state_t *)usb);
        if (port.port_enabled)
        {
            /* We'll retry the exact same transaction again */
            transaction->retries++;
        }
        else
        {
            /* We get channel halted interrupts with no result bits sets when the
                cable is unplugged */
            __cvmx_usb_perform_complete(usb, pipe, transaction, CVMX_USB_COMPLETE_ERROR);
        }
    }
    CVMX_USB_RETURN(0);
}


/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
cvmx_usb_status_t cvmx_usb_poll(cvmx_usb_state_t *state)
{
    cvmx_usbcx_hfnum_t usbc_hfnum;
    cvmx_usbcx_gintsts_t usbc_gintsts;
    cvmx_usb_internal_state_t *usb = (cvmx_usb_internal_state_t*)state;

    CVMX_PREFETCH(usb, 0);
    CVMX_PREFETCH(usb, 1*128);
    CVMX_PREFETCH(usb, 2*128);
    CVMX_PREFETCH(usb, 3*128);
    CVMX_PREFETCH(usb, 4*128);

    CVMX_USB_LOG_CALLED();
    CVMX_USB_LOG_PARAM("%p", state);

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

    if (usbc_gintsts.s.rxflvl)
    {
        /* RxFIFO Non-Empty (RxFLvl)
            Indicates that there is at least one packet pending to be read
            from the RxFIFO. */
        /* In DMA mode this is handled by hardware */
        if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
            __cvmx_usb_poll_rx_fifo(usb);
    }
    if (usbc_gintsts.s.ptxfemp || usbc_gintsts.s.nptxfemp)
    {
        /* Fill the Tx FIFOs when not in DMA mode */
        if (usb->init_flags & CVMX_USB_INITIALIZE_FLAGS_NO_DMA)
            __cvmx_usb_poll_tx_fifo(usb);
    }
    if (usbc_gintsts.s.disconnint || usbc_gintsts.s.prtint)
    {
        cvmx_usbcx_hprt_t usbc_hprt;
        /* Disconnect Detected Interrupt (DisconnInt)
            Asserted when a device disconnect is detected. */

        /* Host Port Interrupt (PrtInt)
            The core sets this bit to indicate a change in port status of one
            of the O2P USB core ports in Host mode. The application must
            read the Host Port Control and Status (HPRT) register to
            determine the exact event that caused this interrupt. The
            application must clear the appropriate status bit in the Host Port
            Control and Status register to clear this bit. */

        /* Call the user's port callback */
        __cvmx_usb_perform_callback(usb, NULL, NULL,
                                    CVMX_USB_CALLBACK_PORT_CHANGED,
                                    CVMX_USB_COMPLETE_SUCCESS);
        /* Clear the port change bits */
        usbc_hprt.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HPRT(usb->index));
        usbc_hprt.s.prtena = 0;
        __cvmx_usb_write_csr32(usb, CVMX_USBCX_HPRT(usb->index), usbc_hprt.u32);
    }
    if (usbc_gintsts.s.hchint)
    {
        /* Host Channels Interrupt (HChInt)
            The core sets this bit to indicate that an interrupt is pending on
            one of the channels of the core (in Host mode). The application
            must read the Host All Channels Interrupt (HAINT) register to
            determine the exact number of the channel on which the
            interrupt occurred, and then read the corresponding Host
            Channel-n Interrupt (HCINTn) register to determine the exact
            cause of the interrupt. The application must clear the
            appropriate status bit in the HCINTn register to clear this bit. */
        cvmx_usbcx_haint_t usbc_haint;
        usbc_haint.u32 = __cvmx_usb_read_csr32(usb, CVMX_USBCX_HAINT(usb->index));
        while (usbc_haint.u32)
        {
            int channel;
            CVMX_CLZ(channel, usbc_haint.u32);
            channel = 31 - channel;
            __cvmx_usb_poll_channel(usb, channel);
            usbc_haint.u32 ^= 1<<channel;
        }
    }

    __cvmx_usb_schedule(usb, usbc_gintsts.s.sof);

    CVMX_USB_RETURN(CVMX_USB_SUCCESS);
}
