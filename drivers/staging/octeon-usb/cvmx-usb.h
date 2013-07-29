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
 * "cvmx-usb.h" defines a set of low level USB functions to help
 * developers create Octeon USB drivers for various operating
 * systems. These functions provide a generic API to the Octeon
 * USB blocks, hiding the internal hardware specific
 * operations.
 *
 * At a high level the device driver needs to:
 *
 * - Call cvmx_usb_get_num_ports() to get the number of
 *   supported ports.
 * - Call cvmx_usb_initialize() for each Octeon USB port.
 * - Enable the port using cvmx_usb_enable().
 * - Either periodically, or in an interrupt handler, call
 *   cvmx_usb_poll() to service USB events.
 * - Manage pipes using cvmx_usb_open_pipe() and
 *   cvmx_usb_close_pipe().
 * - Manage transfers using cvmx_usb_submit_*() and
 *   cvmx_usb_cancel*().
 * - Shutdown USB on unload using cvmx_usb_shutdown().
 *
 * To monitor USB status changes, the device driver must use
 * cvmx_usb_register_callback() to register for events that it
 * is interested in. Below are a few hints on successfully
 * implementing a driver on top of this API.
 *
 * == Initialization ==
 *
 * When a driver is first loaded, it is normally not necessary
 * to bring up the USB port completely. Most operating systems
 * expect to initialize and enable the port in two independent
 * steps. Normally an operating system will probe hardware,
 * initialize anything found, and then enable the hardware.
 *
 * In the probe phase you should:
 * - Use cvmx_usb_get_num_ports() to determine the number of
 *   USB port to be supported.
 * - Allocate space for a cvmx_usb_state_t structure for each
 *   port.
 * - Tell the operating system about each port
 *
 * In the initialization phase you should:
 * - Use cvmx_usb_initialize() on each port.
 * - Do not call cvmx_usb_enable(). This leaves the USB port in
 *   the disabled state until the operating system is ready.
 *
 * Finally, in the enable phase you should:
 * - Call cvmx_usb_enable() on the appropriate port.
 * - Note that some operating system use a RESET instead of an
 *   enable call. To implement RESET, you should call
 *   cvmx_usb_disable() followed by cvmx_usb_enable().
 *
 * == Locking ==
 *
 * All of the functions in the cvmx-usb API assume exclusive
 * access to the USB hardware and internal data structures. This
 * means that the driver must provide locking as necessary.
 *
 * In the single CPU state it is normally enough to disable
 * interrupts before every call to cvmx_usb*() and enable them
 * again after the call is complete. Keep in mind that it is
 * very common for the callback handlers to make additional
 * calls into cvmx-usb, so the disable/enable must be protected
 * against recursion. As an example, the Linux kernel
 * local_irq_save() and local_irq_restore() are perfect for this
 * in the non SMP case.
 *
 * In the SMP case, locking is more complicated. For SMP you not
 * only need to disable interrupts on the local core, but also
 * take a lock to make sure that another core cannot call
 * cvmx-usb.
 *
 * == Port callback ==
 *
 * The port callback prototype needs to look as follows:
 *
 * void port_callback(cvmx_usb_state_t *usb,
 *                    cvmx_usb_callback_t reason,
 *                    cvmx_usb_complete_t status,
 *                    int pipe_handle,
 *                    int submit_handle,
 *                    int bytes_transferred,
 *                    void *user_data);
 * - "usb" is the cvmx_usb_state_t for the port.
 * - "reason" will always be CVMX_USB_CALLBACK_PORT_CHANGED.
 * - "status" will always be CVMX_USB_COMPLETE_SUCCESS.
 * - "pipe_handle" will always be -1.
 * - "submit_handle" will always be -1.
 * - "bytes_transferred" will always be 0.
 * - "user_data" is the void pointer originally passed along
 *   with the callback. Use this for any state information you
 *   need.
 *
 * The port callback will be called whenever the user plugs /
 * unplugs a device from the port. It will not be called when a
 * device is plugged / unplugged from a hub connected to the
 * root port. Normally all the callback needs to do is tell the
 * operating system to poll the root hub for status. Under
 * Linux, this is performed by calling usb_hcd_poll_rh_status().
 * In the Linux driver we use "user_data". to pass around the
 * Linux "hcd" structure. Once the port callback completes,
 * Linux automatically calls octeon_usb_hub_status_data() which
 * uses cvmx_usb_get_status() to determine the root port status.
 *
 * == Complete callback ==
 *
 * The completion callback prototype needs to look as follows:
 *
 * void complete_callback(cvmx_usb_state_t *usb,
 *                        cvmx_usb_callback_t reason,
 *                        cvmx_usb_complete_t status,
 *                        int pipe_handle,
 *                        int submit_handle,
 *                        int bytes_transferred,
 *                        void *user_data);
 * - "usb" is the cvmx_usb_state_t for the port.
 * - "reason" will always be CVMX_USB_CALLBACK_TRANSFER_COMPLETE.
 * - "status" will be one of the cvmx_usb_complete_t enumerations.
 * - "pipe_handle" is the handle to the pipe the transaction
 *   was originally submitted on.
 * - "submit_handle" is the handle returned by the original
 *   cvmx_usb_submit_* call.
 * - "bytes_transferred" is the number of bytes successfully
 *   transferred in the transaction. This will be zero on most
 *   error conditions.
 * - "user_data" is the void pointer originally passed along
 *   with the callback. Use this for any state information you
 *   need. For example, the Linux "urb" is stored in here in the
 *   Linux driver.
 *
 * In general your callback handler should use "status" and
 * "bytes_transferred" to tell the operating system the how the
 * transaction completed. Normally the pipe is not changed in
 * this callback.
 *
 * == Canceling transactions ==
 *
 * When a transaction is cancelled using cvmx_usb_cancel*(), the
 * actual length of time until the complete callback is called
 * can vary greatly. It may be called before cvmx_usb_cancel*()
 * returns, or it may be called a number of usb frames in the
 * future once the hardware frees the transaction. In either of
 * these cases, the complete handler will receive
 * CVMX_USB_COMPLETE_CANCEL.
 *
 * == Handling pipes ==
 *
 * USB "pipes" is a software construct created by this API to
 * enable the ordering of usb transactions to a device endpoint.
 * Octeon's underlying hardware doesn't have any concept
 * equivalent to "pipes". The hardware instead has eight
 * channels that can be used simultaneously to have up to eight
 * transaction in process at the same time. In order to maintain
 * ordering in a pipe, the transactions for a pipe will only be
 * active in one hardware channel at a time. From an API user's
 * perspective, this doesn't matter but it can be helpful to
 * keep this in mind when you are probing hardware while
 * debugging.
 *
 * Also keep in mind that usb transactions contain state
 * information about the previous transaction to the same
 * endpoint. Each transaction has a PID toggle that changes 0/1
 * between each sub packet. This is maintained in the pipe data
 * structures. For this reason, you generally cannot create and
 * destroy a pipe for every transaction. A sequence of
 * transaction to the same endpoint must use the same pipe.
 *
 * == Root Hub ==
 *
 * Some operating systems view the usb root port as a normal usb
 * hub. These systems attempt to control the root hub with
 * messages similar to the usb 2.0 spec for hub control and
 * status. For these systems it may be necessary to write
 * function to decode standard usb control messages into
 * equivalent cvmx-usb API calls.
 *
 * == Interrupts ==
 *
 * If you plan on using usb interrupts, cvmx_usb_poll() must be
 * called on every usb interrupt. It will read the usb state,
 * call any needed callbacks, and schedule transactions as
 * needed. Your device driver needs only to hookup an interrupt
 * handler and call cvmx_usb_poll(). Octeon's usb port 0 causes
 * CIU bit CIU_INT*_SUM0[USB] to be set (bit 56). For port 1,
 * CIU bit CIU_INT_SUM1[USB1] is set (bit 17). How these bits
 * are turned into interrupt numbers is operating system
 * specific. For Linux, there are the convenient defines
 * OCTEON_IRQ_USB0 and OCTEON_IRQ_USB1 for the IRQ numbers.
 *
 * If you aren't using interrupts, simple call cvmx_usb_poll()
 * in your main processing loop.
 */

#ifndef __CVMX_USB_H__
#define __CVMX_USB_H__

/**
 * Enumerations representing the possible USB device speeds
 */
typedef enum
{
    CVMX_USB_SPEED_HIGH = 0,        /**< Device is operation at 480Mbps */
    CVMX_USB_SPEED_FULL = 1,        /**< Device is operation at 12Mbps */
    CVMX_USB_SPEED_LOW = 2,         /**< Device is operation at 1.5Mbps */
} cvmx_usb_speed_t;

/**
 * Enumeration representing the possible USB transfer types.
 */
typedef enum
{
    CVMX_USB_TRANSFER_CONTROL = 0,      /**< USB transfer type control for hub and status transfers */
    CVMX_USB_TRANSFER_ISOCHRONOUS = 1,  /**< USB transfer type isochronous for low priority periodic transfers */
    CVMX_USB_TRANSFER_BULK = 2,         /**< USB transfer type bulk for large low priority transfers */
    CVMX_USB_TRANSFER_INTERRUPT = 3,    /**< USB transfer type interrupt for high priority periodic transfers */
} cvmx_usb_transfer_t;

/**
 * Enumeration of the transfer directions
 */
typedef enum
{
    CVMX_USB_DIRECTION_OUT,         /**< Data is transferring from Octeon to the device/host */
    CVMX_USB_DIRECTION_IN,          /**< Data is transferring from the device/host to Octeon */
} cvmx_usb_direction_t;

/**
 * Enumeration of all possible status codes passed to callback
 * functions.
 */
typedef enum
{
    CVMX_USB_COMPLETE_SUCCESS,      /**< The transaction / operation finished without any errors */
    CVMX_USB_COMPLETE_SHORT,        /**< FIXME: This is currently not implemented */
    CVMX_USB_COMPLETE_CANCEL,       /**< The transaction was canceled while in flight by a user call to cvmx_usb_cancel* */
    CVMX_USB_COMPLETE_ERROR,        /**< The transaction aborted with an unexpected error status */
    CVMX_USB_COMPLETE_STALL,        /**< The transaction received a USB STALL response from the device */
    CVMX_USB_COMPLETE_XACTERR,      /**< The transaction failed with an error from the device even after a number of retries */
    CVMX_USB_COMPLETE_DATATGLERR,   /**< The transaction failed with a data toggle error even after a number of retries */
    CVMX_USB_COMPLETE_BABBLEERR,    /**< The transaction failed with a babble error */
    CVMX_USB_COMPLETE_FRAMEERR,     /**< The transaction failed with a frame error even after a number of retries */
} cvmx_usb_complete_t;

/**
 * Structure returned containing the USB port status information.
 */
typedef struct
{
    uint32_t reserved           : 25;
    uint32_t port_enabled       : 1; /**< 1 = Usb port is enabled, 0 = disabled */
    uint32_t port_over_current  : 1; /**< 1 = Over current detected, 0 = Over current not detected. Octeon doesn't support over current detection */
    uint32_t port_powered       : 1; /**< 1 = Port power is being supplied to the device, 0 = power is off. Octeon doesn't support turning port power off */
    cvmx_usb_speed_t port_speed : 2; /**< Current port speed */
    uint32_t connected          : 1; /**< 1 = A device is connected to the port, 0 = No device is connected */
    uint32_t connect_change     : 1; /**< 1 = Device connected state changed since the last set status call */
} cvmx_usb_port_status_t;

/**
 * This is the structure of a Control packet header
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t request_type   : 8;  /**< Bit 7 tells the direction: 1=IN, 0=OUT */
        uint64_t request        : 8;  /**< The standard usb request to make */
        uint64_t value          : 16; /**< Value parameter for the request in little endian format */
        uint64_t index          : 16; /**< Index for the request in little endian format */
        uint64_t length         : 16; /**< Length of the data associated with this request in little endian format */
    } s;
} cvmx_usb_control_header_t;

/**
 * Descriptor for Isochronous packets
 */
typedef struct
{
    int offset;                     /**< This is the offset in bytes into the main buffer where this data is stored */
    int length;                     /**< This is the length in bytes of the data */
    cvmx_usb_complete_t status;     /**< This is the status of this individual packet transfer */
} cvmx_usb_iso_packet_t;

/**
 * Possible callback reasons for the USB API.
 */
typedef enum
{
    CVMX_USB_CALLBACK_TRANSFER_COMPLETE,
                                    /**< A callback of this type is called when a submitted transfer
                                        completes. The completion callback will be called even if the
                                        transfer fails or is canceled. The status parameter will
                                        contain details of why he callback was called. */
    CVMX_USB_CALLBACK_PORT_CHANGED, /**< The status of the port changed. For example, someone may have
                                        plugged a device in. The status parameter contains
                                        CVMX_USB_COMPLETE_SUCCESS. Use cvmx_usb_get_status() to get
                                        the new port status. */
    __CVMX_USB_CALLBACK_END         /**< Do not use. Used internally for array bounds */
} cvmx_usb_callback_t;

/**
 * USB state internal data. The contents of this structure
 * may change in future SDKs. No data in it should be referenced
 * by user's of this API.
 */
typedef struct
{
    char data[65536];
} cvmx_usb_state_t;

/**
 * USB callback functions are always of the following type.
 * The parameters are as follows:
 *      - state = USB device state populated by
 *        cvmx_usb_initialize().
 *      - reason = The cvmx_usb_callback_t used to register
 *        the callback.
 *      - status = The cvmx_usb_complete_t representing the
 *        status code of a transaction.
 *      - pipe_handle = The Pipe that caused this callback, or
 *        -1 if this callback wasn't associated with a pipe.
 *      - submit_handle = Transfer submit handle causing this
 *        callback, or -1 if this callback wasn't associated
 *        with a transfer.
 *      - Actual number of bytes transfer.
 *      - user_data = The user pointer supplied to the
 *        function cvmx_usb_submit() or
 *        cvmx_usb_register_callback() */
typedef void (*cvmx_usb_callback_func_t)(cvmx_usb_state_t *state,
                                         cvmx_usb_callback_t reason,
                                         cvmx_usb_complete_t status,
                                         int pipe_handle, int submit_handle,
                                         int bytes_transferred, void *user_data);

/**
 * Flags to pass the initialization function.
 */
typedef enum
{
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI = 1<<0,       /**< The USB port uses a 12MHz crystal as clock source
                                                            at USB_XO and USB_XI. */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND = 1<<1,      /**< The USB port uses 12/24/48MHz 2.5V board clock
                                                            source at USB_XO. USB_XI should be tied to GND.*/
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_AUTO = 0,           /**< Automatically determine clock type based on function
                                                             in cvmx-helper-board.c. */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK  = 3<<3,       /**< Mask for clock speed field */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ = 1<<3,       /**< Speed of reference clock or crystal */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ = 2<<3,       /**< Speed of reference clock */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ = 3<<3,       /**< Speed of reference clock */
    /* Bits 3-4 used to encode the clock frequency */
    CVMX_USB_INITIALIZE_FLAGS_NO_DMA = 1<<5,            /**< Disable DMA and used polled IO for data transfer use for the USB  */
} cvmx_usb_initialize_flags_t;

/**
 * Flags for passing when a pipe is created. Currently no flags
 * need to be passed.
 */
typedef enum
{
    __CVMX_USB_PIPE_FLAGS_OPEN = 1<<16,         /**< Used internally to determine if a pipe is open. Do not use */
    __CVMX_USB_PIPE_FLAGS_SCHEDULED = 1<<17,    /**< Used internally to determine if a pipe is actively using hardware. Do not use */
    __CVMX_USB_PIPE_FLAGS_NEED_PING = 1<<18,    /**< Used internally to determine if a high speed pipe is in the ping state. Do not use */
} cvmx_usb_pipe_flags_t;

extern int cvmx_usb_get_num_ports(void);
extern int cvmx_usb_initialize(cvmx_usb_state_t *state, int usb_port_number,
			       cvmx_usb_initialize_flags_t flags);
extern int cvmx_usb_shutdown(cvmx_usb_state_t *state);
extern int cvmx_usb_enable(cvmx_usb_state_t *state);
extern int cvmx_usb_disable(cvmx_usb_state_t *state);
extern cvmx_usb_port_status_t cvmx_usb_get_status(cvmx_usb_state_t *state);
extern void cvmx_usb_set_status(cvmx_usb_state_t *state, cvmx_usb_port_status_t port_status);
extern int cvmx_usb_open_pipe(cvmx_usb_state_t *state,
                              cvmx_usb_pipe_flags_t flags,
                              int device_addr, int endpoint_num,
                              cvmx_usb_speed_t device_speed, int max_packet,
                              cvmx_usb_transfer_t transfer_type,
                              cvmx_usb_direction_t transfer_dir, int interval,
                              int multi_count, int hub_device_addr,
                              int hub_port);
extern int cvmx_usb_submit_bulk(cvmx_usb_state_t *state, int pipe_handle,
                                uint64_t buffer, int buffer_length,
                                cvmx_usb_callback_func_t callback,
                                void *user_data);
extern int cvmx_usb_submit_interrupt(cvmx_usb_state_t *state, int pipe_handle,
                                     uint64_t buffer, int buffer_length,
                                     cvmx_usb_callback_func_t callback,
                                     void *user_data);
extern int cvmx_usb_submit_control(cvmx_usb_state_t *state, int pipe_handle,
                                   uint64_t control_header,
                                   uint64_t buffer, int buffer_length,
                                   cvmx_usb_callback_func_t callback,
                                   void *user_data);

/**
 * Flags to pass the cvmx_usb_submit_isochronous() function.
 */
typedef enum
{
    CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT = 1<<0,  /**< Do not return an error if a transfer is less than the maximum packet size of the device */
    CVMX_USB_ISOCHRONOUS_FLAGS_ASAP = 1<<1,         /**< Schedule the transaction as soon as possible */
} cvmx_usb_isochronous_flags_t;

extern int cvmx_usb_submit_isochronous(cvmx_usb_state_t *state, int pipe_handle,
                                       int start_frame, int flags,
                                       int number_packets,
                                       cvmx_usb_iso_packet_t packets[],
                                       uint64_t buffer, int buffer_length,
                                       cvmx_usb_callback_func_t callback,
                                       void *user_data);
extern int cvmx_usb_cancel(cvmx_usb_state_t *state, int pipe_handle,
			   int submit_handle);
extern int cvmx_usb_cancel_all(cvmx_usb_state_t *state, int pipe_handle);
extern int cvmx_usb_close_pipe(cvmx_usb_state_t *state, int pipe_handle);
extern int cvmx_usb_register_callback(cvmx_usb_state_t *state,
				      cvmx_usb_callback_t reason,
				      cvmx_usb_callback_func_t callback,
				      void *user_data);
extern int cvmx_usb_get_frame_number(cvmx_usb_state_t *state);
extern int cvmx_usb_poll(cvmx_usb_state_t *state);

#endif  /* __CVMX_USB_H__ */
