/* Cypress West Bridge API header file (cyasusb.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASUSB_H_
#define _INCLUDED_CYASUSB_H_

#include "cyasmisc.h"

#include "cyas_cplus_start.h"

/*@@Enumeration Model
  Summary
  The USB enumeration process is the process of communicating
  to the USB host information
  about the capabilities of the connected device.  This
  process is completed by servicing
  requests for various types of descriptors.  In the software
  APIs described below, this
  process is controlled in one of two ways.

  Description
  There are advantages to either type of enumeration
  and this is why both models are supported.
  P Port processor based enumeraton gives the P port
  processor maximum control and flexibility
  for providing USB configuration information.  However,
  this does require (near) real time data
  responses from the P port processor during the enumeration
  process.  West Bridge based enumeration
  requires no real time information from the P port processor,
  ensuring the fastest possible
  enumeration times.

  * P Port Based Enumeration *
  The first method for handling USB enumeration is for the
  processor client to handle all
  endpoint zero requests for descriptors.  This mode is
  configured by indicating to the API
  that the processor wants to handle all endpoint zero
  requests. This is done by setting
  bit 0 in the end_point_mask to a 1.  The processor uses
  CyAsUsbReadDataAsync() to read the request and
  CyAsUsbWriteDataAsync() to write the response.

  * West Bridge Based Enumeration *
  The second method for handling USB enumeration is the
  configuration information method.
  Before enabling a connection from the West Bridge device
  to the USB connector, the P Port
  processor sends information about the USB configuration to
  West Bridge through the configuration
  APIs.  This information is stored within the West Bridge
  device.  When a USB cable is attached,
  the West Bridge device then handles all descriptor requests
  based on the stored information.
  Note that this method of enumeration only supports a single
  USB configuration.

  In either model of enumeration, the processor client is
  responsible for ensuring that
  the system meets USB Chapter 9 compliance requirements. This
  can be done by providing spec
  compliant descriptors, and handling any setup packets that
  are sent to the client
  appropriately.

  Mass storage class compliance will be ensured by the West
  Bridge firmware when the mass
  storage functionality is enabled.
*/

/*@@Endpoint Configuration
  Summary
  The West Bridge device has one 64-byte control endpoint, one
  64-byte low bandwidth endpoint, four bulk
  endpoints dedicated for mass storage usage, and up to ten
  bulk/interrupt/isochronous
  endpoints that can be used for USB-to-Processor communication.

  Description
  The four storage endpoints (Endpoints 2, 4, 6 and 8) are
  reserved for accessing storage
  devices attached to West Bridge and are not available for use
  by the processor.  These are
  used implicitly when using the storage API to read/write to
  the storage media.

  Endpoint 0 is the standard USB control pipe used for all
  enumeration activity.  Though
  the endpoint buffer is not directly accessible from the
  processor, read/write activity
  can be performed on this endpoint through the API layers.
  This endpoint is always
  configured as a bi-directional control endpoint.

  Endpoint 1 is a 64-byte endpoint that can be used for low
  bandwidth bulk/interrupt
  activity.  The physical buffer is not accessible from the
  processor, but can be read/written
  through the API.  As the data coming to this endpoint is
  being handled through the
  software layers, there can be loss of data if a read call
  is not waiting when an OUT
  packet arrives.

  Endpoints 3, 5, 7, 9, 10, 11, 12, 13, 14 and 15 are ten
  configurable endpoints
  mapped to parts of a total 4 KB FIFO buffer space on the
  West Bridge device.  This 4 KB
  physical buffer space is divided into up to four endpoints
  called PEP1, PEP2, PEP3 and PEP4
  in this software document.  There are multiple configurations
  in which this buffer space
  can be used, and the size and number of buffers available to
  each physical endpoint
  vary between these configurations.  See the West Bridge PDD
  for details on the buffer
  orientation corresponding to each configuration.

  * Note *
  PEPs 1, 2, 3 and 4 are called Physical EP 3, 5, 7 and 9 in the
  West Bridge PDD.  The
  sequential number scheme is used in the software to disambiguate
  these from the logical
  endpoint numbers, and also for convenience of array indexing.
*/

#if !defined(__doxygen__)


#endif

/* Summary
   This constants defines the maximum size of a USB descriptor
   when referenced via the CyAsUsbSetDescriptor or
   CyAsUsbGetDescriptor functions.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
*/
#define CY_AS_MAX_USB_DESCRIPTOR_SIZE	(128)

/***************************************
 * West Bridge Types
 ***************************************/


/* Summary
   This data structure is the data passed via the evdata paramater
   on a usb event callback for the inquiry request.

   Description
   When a SCSI inquiry request arrives via the USB connection and
   the P Port has asked
   to receive inquiry requests, this request is forwarded to the
   client via the USB
   callback.  This callback is called twice, once before the
   inquiry data is forwarded
   to the host (CyAsEventUsbInquiryBefore) and once after the
   inquiry has been sent to the
   USB host (CyAsEventUsbInquiryAfter).  The evdata parameter
   is a pointer to this data
   structure.

   *CyAsEventUsbInquiryBefore*
   If the client just wishes to see the inquiry request and
   associated data, then a simple
   return from the callback will forward the inquiry response
   to the USB host.  If the
   client wishes to change the data returned to the USB host,
   the updated parameter must
   be set to CyTrue and the memory area address by the data
   parameter should be updated.
   The data pointer can be changed to point to a new memory
   area and the length field
   changed to change the amount of data returned from the
   inquiry request.  Note that the
   data area pointed to by the data parameter must remain
   valid and the contents must
   remain consistent until after the CyAsEventUsbInquiryAfter
   event has occurred.  THE LENGTH
   MUST BE LESS THAN 192 BYTES OR THE CUSTOM INQUIRY RESPONSE
   WILL NOT BE RETURNED.  If the
   length is too long, the default inquiry response will be
   returned.

   *CyAsEventUsbInquiryAfter*
   If the client needs to free any data, this event signals that
   the data associated with the inquiry is no longer needed.

   See Also
   * CyAsUsbEventCallback
   * CyAsUsbRegisterCallback
*/
typedef struct cy_as_usb_inquiry_data {
	/* The bus for the event */
	cy_as_bus_number_t bus;
	/* The device the event */
	uint32_t device;
	/* The EVPD bit from the SCSI INQUIRY request */
	uint8_t evpd;
	/* The codepage in the inquiry request */
	uint8_t codepage;
	/* This bool must be set to CyTrue indicate that the inquiry
				   data was changed */
	cy_bool updated;
	/* The length of the data */
	uint16_t length;
	/* The inquiry data */
	void *data;
} cy_as_usb_inquiry_data;


/* Summary
   This data structure is the data passed via the evdata
   parameter on a usb event
   callback for the unknown mass storage request.

   Description
   When a SCSI request is made that the mass storage
   firmware in West Bridge does not
   know how to process, this request is passed to the
   processor for handling via
   the usb callback.  This data structure is used to
   pass the request and the
   associated response.  The user may set the status
   to indicate the status of the
   request.  The status value is the bCSWStatus value
   from the USB mass storage
   Command Status Wrapper (0 = command passed, 1 =
   command failed).  If the status
   is set to command failed (1), the sense information
   should be set as well.  For
   more information about sense information, see the
   USB mass storage specification
   as well as the SCSI specifications for block devices.
   By default the status is
   initialized to 1 (failure) with a sense information
   of 05h/20h/00h which
   indicates INVALID COMMAND.
*/
typedef struct cy_as_usb_unknown_command_data {
	/* The bus for the event */
	cy_as_bus_number_t bus;
	/* The device for the event */
	uint32_t device;

	uint16_t reqlen;
	/* The request */
	void *request;

	/* The returned status value for the command */
	uint8_t status;
	/* If status is failed, the sense key */
	uint8_t key;
	/* If status is failed, the additional sense code */
	uint8_t asc;
	/* If status if failed, the additional sense code qualifier */
	uint8_t ascq;
} cy_as_usb_unknown_command_data;


/* Summary
   This data structure is the data passed via the evdata
   paramater on a usb event callback for the start/stop request.

   Description
   When a SCSI start stop request arrives via the USB connection
   and the P Port has asked

   See Also
   * CyAsUsbEventCallback
   * CyAsUsbRegisterCallback
*/
typedef struct cy_as_usb_start_stop_data {
	/* The bus for the event */
	cy_as_bus_number_t bus;
	/* The device for the event */
	uint32_t device;
	/* CyTrue means start request, CyFalse means stop request */
	cy_bool start;
	/* CyTrue means LoEj bit set, otherwise false */
	cy_bool loej;
} cy_as_usb_start_stop_data;

/* Summary
   This data type is used to indicate which mass storage devices
   are enumerated.

   Description

   See Also
   * CyAsUsbEnumControl
   * CyAsUsbSetEnumConfig
*/
typedef enum cy_as_usb_mass_storage_enum {
	cy_as_usb_nand_enum = 0x01,
	cy_as_usb_sd_enum = 0x02,
	cy_as_usb_mmc_enum = 0x04,
	cy_as_usb_ce_ata_enum = 0x08
} cy_as_usb_mass_storage_enum;

/* Summary
   This data type specifies the type of descriptor to transfer
   to the West Bridge device

   Description
   During enumeration, if West Bridge is handling enumeration,
   the West Bridge device needs to USB descriptors
   to complete the enumeration.  The function CyAsUsbSetDescriptor()
   is used to transfer the descriptors
   to the West Bridge device.  This type is an argument to that
   function and specifies which descriptor
   is being transferred.

   See Also
   * CyAsUsbSetDescriptor
   * CyAsUsbGetDescriptor
*/
typedef enum cy_as_usb_desc_type {
	/* A device descriptor - See USB 2.0 specification Chapter 9 */
	cy_as_usb_desc_device = 1,
	/* A device descriptor qualifier -
	 *  See USB 2.0 specification Chapter 9 */
	cy_as_usb_desc_device_qual = 2,
	/* A configuration descriptor for FS operation -
	 * See USB 2.0 specification Chapter 9 */
	cy_as_usb_desc_f_s_configuration = 3,
	/* A configuration descriptor for HS operation -
	 * See USB 2.0 specification Chapter 9 */
	cy_as_usb_desc_h_s_configuration = 4,
	cy_as_usb_desc_string = 5
} cy_as_usb_desc_type;

/* Summary
   This type specifies the direction of an endpoint

   Description
   This type is used when configuring the endpoint hardware
   to specify the direction
   of the endpoint.

   See Also
   * CyAsUsbEndPointConfig
   * CyAsUsbSetEndPointConfig
   * CyAsUsbGetEndPointConfig
*/
typedef enum cy_as_usb_end_point_dir {
	/* The endpoint direction is IN (West Bridge -> USB Host) */
	cy_as_usb_in = 0,
	/* The endpoint direction is OUT (USB Host -> West Bridge) */
	cy_as_usb_out = 1,
	/* The endpoint direction is IN/OUT (valid only for EP 0 & 1) */
	cy_as_usb_in_out = 2
} cy_as_usb_end_point_dir;

/* Summary
   This type specifies the type of an endpoint

   Description
   This type is used when configuring the endpoint hardware
   to specify the type of endpoint.

   See Also
   * CyAsUsbEndPointConfig
   * CyAsUsbSetEndPointConfig
   * CyAsUsbGetEndPointConfig
*/
typedef enum cy_as_usb_end_point_type {
	cy_as_usb_control,
	cy_as_usb_iso,
	cy_as_usb_bulk,
	cy_as_usb_int
} cy_as_usb_end_point_type;

/* Summary
   This type is a structure used to indicate the top level
   configuration of the USB stack

   Description
   In order to configure the USB stack, the CyAsUsbSetEnumConfig()
   function is called to indicate
   how mass storage is to be handled, the specific number of
   interfaces to be supported if
   West Bridge is handling enumeration, and the end points of
   specifi interest.  This structure
   contains this information.

   See Also
   * CyAsUsbSetConfig
   * CyAsUsbGetConfig
   * <LINK Enumeration Model>
*/
typedef struct cy_as_usb_enum_control {
	/* Designate which devices on which buses to enumerate */
	cy_bool devices_to_enumerate[CY_AS_MAX_BUSES]
		[CY_AS_MAX_STORAGE_DEVICES];
	/* If true, West Bridge will control enumeration.  If this
	 * is false the P port controls enumeration.  if the P port
	 * is controlling enumeration, traffic will be received via
	 * endpoint zero. */
	cy_bool antioch_enumeration;
	/* This is the interface # to use for the mass storage
	 * interface, if mass storage is enumerated.  if mass
	 * storage is not enumerated this value should be zero. */
	uint8_t mass_storage_interface;
	/* This is the interface # to use for the MTP interface,
	 * if MTP is enumerated.  if MTP is not enumerated
	 * this value should be zero. */
	uint8_t mtp_interface;
	/* If true, Inquiry, START/STOP, and unknown mass storage
	 * requests cause a callback to occur for handling by the
	 *  baseband processor. */
	cy_bool mass_storage_callbacks;
} cy_as_usb_enum_control;


/* Summary
   This structure is used to configure a single endpoint

   Description
   This data structure contains all of the information required
   to configure the West Bridge hardware
   associated with a given endpoint.

   See Also
   * CyAsUsbSetEndPointConfig
   * CyAsUsbGetEndPointConfig
*/
typedef struct cy_as_usb_end_point_config {
	/* If true, this endpoint is enabled */
	cy_bool enabled;
	/* The direction of this endpoint */
	cy_as_usb_end_point_dir dir;
	/* The type of endpoint */
	cy_as_usb_end_point_type type;
	/* The physical endpoint #, 1, 2, 3, 4 */
	cy_as_end_point_number_t physical;
	/* The size of the endpoint in bytes */
	uint16_t size;
} cy_as_usb_end_point_config;

/* Summary
   List of partition enumeration combinations that can
   be selected on a partitioned storage device.

   Description
   West Bridge firmware supports creating up to two
   partitions on mass storage devices connected to
   West Bridge.  When there are two partitions on a device,
   the user can choose which of these partitions should be
   made visible to a USB host through the mass storage
   interface.  This enumeration lists the various enumeration
   selections that can be made.

   See Also
   * CyAsStorageCreatePPartition
   * CyAsStorageRemovePPartition
   * CyAsUsbSelectMSPartitions
 */
typedef enum cy_as_usb_m_s_type_t {
	/* Enumerate only partition 0 as CD (autorun) device */
	cy_as_usb_m_s_unit0 = 0,
	/* Enumerate only partition 1 as MS device (default setting) */
	cy_as_usb_m_s_unit1,
	/* Enumerate both units */
	cy_as_usb_m_s_both
} cy_as_usb_m_s_type_t;

/* Summary
   This type specifies the type of USB event that has occurred

   Description
   This type is used in the USB event callback function to
   indicate the type of USB event that has occurred.  The callback
   function includes both this reasons for the callback and a data
   parameter associated with the reason.  The data parameter is used
   in a reason specific way and is documented below with each reason.

   See Also
   * CyAsUsbIoCallback
*/
typedef enum cy_as_usb_event {
	/* This event is sent when West Bridge is put into the suspend
	state by the USB host.  the data parameter is not used and
	will be zero. */
	cy_as_event_usb_suspend,
	/* This event is sent when West Bridge is taken out of the
	suspend state by the USB host.  the data parameter is not
	used and will be zero. */
	cy_as_event_usb_resume,
	/* This event is sent when a USB reset request is received
	by the west bridge device.  the data parameter is not used and
	will be zero. */
	cy_as_event_usb_reset,
	/* This event is sent when a USB set configuration request is made.
	the data parameter is a pointer to a uint16_t that contains the
	configuration number.  the configuration number may be zero to
	indicate an unconfigure operation. */
	cy_as_event_usb_set_config,
	/* This event is sent when the USB connection changes speed. This is
	generally a transition from full speed to high speed.  the parameter
	to this event is a pointer to uint16_t that gives the speed of the
	USB connection.  zero indicates full speed, one indicates high speed */
	cy_as_event_usb_speed_change,
	/* This event is sent when a setup packet is received.
	 * The data parameter is a pointer to the eight bytes of setup data. */
	cy_as_event_usb_setup_packet,
	/* This event is sent when a status packet is received.  The data
	parameter is not used. */
	cy_as_event_usb_status_packet,
	/* This event is sent when mass storage receives an inquiry
	request and we have asked to see these requests. */
	cy_as_event_usb_inquiry_before,
	/* This event is sent when mass storage has finished processing an
	inquiry request and any data associated with the request is no longer
	required. */
	cy_as_event_usb_inquiry_after,
	/* This event is sent when mass storage receives a start/stop
	 * request and we have asked to see these requests */
	cy_as_event_usb_start_stop,
	/* This event is sent when a Clear Feature request is received.
	 * The data parameter is the endpoint number. */
	cy_as_event_usb_clear_feature,
	/* This event is sent when mass storage receives a request
	 * that is not known and we have asked to see these requests */
	cy_as_event_usb_unknown_storage,
	/* This event is sent when the read/write activity on the USB mass
	storage has crossed a pre-set level */
	cy_as_event_usb_m_s_c_progress
} cy_as_usb_event;

/* Summary
   This type is the type of a callback function that is
   called when a USB event occurs

   Description
   At times West Bridge needs to inform the P port processor
   of events that have
   occurred.  These events are asynchronous to the thread of
   control on the P
   port processor and as such are generally delivered via a
   callback function that
   is called as part of an interrupt handler.  This type
   defines the type of function
   that must be provided as a callback function for USB events.

   See Also
   * CyAsUsbEvent
*/
typedef void (*cy_as_usb_event_callback)(
	/* Handle to the device to configure */
	cy_as_device_handle			handle,
	/* The event type being reported */
	cy_as_usb_event			ev,
	/* The data assocaited with the event being reported */
	void *evdata
);


/* Summary
   This type is the callback function called after an
   asynchronous USB read/write operation

   Description
   This function type defines a callback function that is
   called at the completion of any
   asynchronous read or write operation.

   See Also
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
   * CY_AS_ERROR_CANCELED
*/
typedef void (*cy_as_usb_io_callback)(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* The endpoint that has completed an operation */
	cy_as_end_point_number_t ep,
	/* THe amount of data transferred to/from USB */
	uint32_t count,
	/* The data buffer for the operation */
	void *buffer,
	/* The error status of the operation */
	cy_as_return_status_t status
);

/* Summary
   This type is the callback function called after asynchronous
   API functions have completed.

   Description
   When calling API functions from callback routines (interrupt
   handlers usually) the async version of
   these functions must be used.  This callback is called when an
   asynchronous API function has completed.
*/
typedef void (*cy_as_usb_function_callback)(
	/* Handle to the device to configure */
	cy_as_device_handle			handle,
	/* The error status of the operation */
	cy_as_return_status_t			status,
	/* A client supplied 32 bit tag */
	uint32_t				client
);


/********************************************
 * West Bridge Functions
 ********************************************/

/* Summary
   This function starts the USB stack

   Description
   This function initializes the West Bridge USB software
   stack if it has not yet been stared.
   This initializes any required data structures and powers
   up any USB specific portions of
   the West Bridge hardware.  If the stack had already been
   started, the USB stack reference count
   is incremented.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   This function cannot be called from any type of West Bridge
   callback.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_SUCCESS - the stack initialized and is ready
   *	for use
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device

   See Also
   * CyAsUsbStop
*/
EXTERN cy_as_return_status_t
cy_as_usb_start(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
	);

/* Summary
   This function stops the USB stack

   Description
   This function decrements the reference count for
   the USB stack and if this count
   is zero, the USB stack is shut down.  The shutdown
   frees all resources associated
   with the USB stack.

   * Valid In Asynchronous Callback: NO

   Notes
   While all resources associated with the USB stack will
   be freed is a shutdown occurs,
   resources associated with underlying layers of the software
   will not be freed if they
   are shared by the storage stack and the storage stack is active.
   Specifically the DMA manager,
   the interrupt manager, and the West Bridge communications module
   are all shared by both the
   USB stack and the storage stack.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device

   See Also
   * CyAsUsbStart
*/
EXTERN cy_as_return_status_t
cy_as_usb_stop(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback		cb,
	 /* Client supplied data */
	uint32_t			client
	);

/* Summary
   This function registers a callback function to be called when an
   asynchronous USB event occurs

   Description
   When asynchronous USB events occur, a callback function can be
   called to alert the calling program.  This
   functions allows the calling program to register a callback.

   * Valid In Asynchronous Callback: YES
*/
EXTERN cy_as_return_status_t
cy_as_usb_register_callback(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The function to call */
	cy_as_usb_event_callback callback
	);


/* Summary
   This function connects the West Bridge device D+ and D- signals
   physically to the USB host.

   Description
   The West Bridge device has the ability to programmatically
   disconnect the USB pins on the device
   from the USB host.  This feature allows for re-enumeration of
   the West Bridge device as a different
   device when necessary.  This function connects the D+ and D-
   signal physically to the USB host
   if they have been previously disconnected.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running

   See Also
   * CyAsUsbDisconnect
*/
EXTERN cy_as_return_status_t
cy_as_usb_connect(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function disconnects the West Bridge device D+ and D-
   signals physically from the USB host.

   Description
   The West Bridge device has the ability to programmatically
   disconnect the USB pins on the device
   from the USB host.  This feature allows for re-enumeration
   of the West Bridge device as a different
   device when necessary.  This function disconnects the D+
   and D- signal physically from the USB host
   if they have been previously connected.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running

   See Also
   * CyAsUsbConnect
*/
EXTERN cy_as_return_status_t
cy_as_usb_disconnect(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function configures the USB stack

   Description
   This function is used to configure the USB stack.  It is
   used to indicate which endpoints are going to
   be used, and how to deal with the mass storage USB device
   within West Bridge.

   * Valid In Asynchronous Callback: Yes (if cb supplied)

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running

   See Also
   * CyAsUsbGetEnumConfig
   * CyAsUsbEnumControl
 */
EXTERN cy_as_return_status_t
cy_as_usb_set_enum_config(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The USB configuration information */
	cy_as_usb_enum_control *config_p,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function retreives the current configuration of
   the USB stack

   Description
   This function sends a request to West Bridge to retrieve
   the current configuration

   * Valid In Asynchronous Callback: Yes (if cb supplied)

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running

   See Also
   * CyAsUsbSetConfig
   * CyAsUsbConfig
 */
EXTERN cy_as_return_status_t
cy_as_usb_get_enum_config(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The return value for USB congifuration information */
	cy_as_usb_enum_control *config_p,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function sets the USB descriptor

   Description
   This function is used to set the various descriptors
   assocaited with the USB enumeration
   process.  This function should only be called when the
   West Bridge enumeration model is selected.
   Descriptors set using this function can be cleared by
   stopping the USB stack, or by calling
   the CyAsUsbClearDescriptors function.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   These descriptors are described in the USB 2.0 specification,
   Chapter 9.

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_DESCRIPTOR - the descriptor passed is
   *	not valid
   * CY_AS_ERROR_BAD_INDEX - a bad index was given for the type
   *	of descriptor given
   * CY_AS_ERROR_BAD_ENUMERATION_MODE - this function cannot be
   *	called if the P port processor doing enumeration

   See Also
   * CyAsUsbGetDescriptor
   * CyAsUsbClearDescriptors
   * <LINK Enumeration Model>
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_descriptor(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The type of descriptor */
	cy_as_usb_desc_type	type,
	/* Only valid for string descriptors */
	uint8_t	index,
	/* The descriptor to be transferred */
	void *desc_p,
	/* The length of the descriptor in bytes */
	uint16_t length,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function clears all user descriptors stored
   on the West Bridge.

   Description
   This function is used to clear all descriptors that
   were previously
   stored on the West Bridge through CyAsUsbSetDescriptor
   calls, and go back
   to the default descriptor setup in the firmware.  This
   function should
   only be called when the Antioch enumeration model is
   selected.

   * Valid In Asynchronous Callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - all descriptors cleared successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_BAD_ENUMERATION_MODE - this function cannot be
   * called if the P port processor is doing enumeration

   See Also
   * CyAsUsbSetDescriptor
   * <LINK Enumeration Model>
*/
EXTERN cy_as_return_status_t
cy_as_usb_clear_descriptors(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);
/* Summary
   This structure contains the descriptor buffer to be
   filled by CyAsUsbGetDescriptor API.

   Description
   This data structure the buffer to hold the descriptor
   data, and an in/out parameter ti indicate the
   length of the buffer and descriptor data in bytes.

   See Also
   * CyAsUsbGetDescriptor
*/
typedef struct cy_as_get_descriptor_data {
	/* The buffer to hold the returned descriptor */
	void *desc_p;
	/* This is an input and output parameter.
	 * Before the code this pointer points to a uint32_t
	 * that contains the length of the buffer.  after
	 * the call, this value contains the amount of data
	 * actually returned. */
	uint32_t	 length;

} cy_as_get_descriptor_data;

/* Summary
   This function retreives a given descriptor from the
   West Bridge device

   Description
   This function retreives a USB descriptor from the West
   Bridge device.  This function should only be called when the
   West Bridge enumeration model is selected.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   These descriptors are described in the USB 2.0 specification,
   Chapter 9.

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_BAD_INDEX - a bad index was given for the type of
   * descriptor given
   * CY_AS_ERROR_BAD_ENUMERATION_MODE - this function cannot be
   * called if the P port processor doing enumeration

   See Also
   * CyAsUsbSetDescriptor
   * <LINK Enumeration Model>
*/

EXTERN cy_as_return_status_t
cy_as_usb_get_descriptor(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The type of descriptor */
	cy_as_usb_desc_type	type,
	/* Index for string descriptor */
	uint8_t	index,
	/* Parameters and return value for the get descriptor call */
	cy_as_get_descriptor_data *data,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function sets the configuration of the physical
   endpoints into one of the twelve supported configuration

   Description
   USB endpoints are mapped onto one of four physical
   endpoints in the device.  Therefore
   USB endpoints are known as logical endpoints and these
   logical endpoints are mapped to
   one of four physical endpoints.  In support of these
   four physical endpoints there is
   four kilo-bytes of buffer spaces that can be used as
   buffers for these physical endpoints.
   This 4K of buffer space can be configured in one of
   twelve ways.  This function sets the
   buffer configuration for the physical endpoints.

   * Config  1: PEP1 (2 * 512), PEP2 (2 * 512),
   *	PEP3 (2 * 512), PEP4 (2 * 512)
   * Config  2: PEP1 (2 * 512), PEP2 (2 * 512),
   *	PEP3 (4 * 512), PEP4 (N/A)
   * Config  3: PEP1 (2 * 512), PEP2 (2 * 512),
   *	PEP3 (2 * 1024), PEP4(N/A)
   * Config  4: PEP1 (4 * 512), PEP2 (N/A),
   *	PEP3 (2 * 512), PEP4 (2 * 512)
   * Config  5: PEP1 (4 * 512), PEP2 (N/A),
   *	PEP3 (4 * 512), PEP4 (N/A)
   * Config  6: PEP1 (4 * 512), PEP2 (N/A),
   *	PEP3 (2 * 1024), PEP4 (N/A)
   * Config  7: PEP1 (2 * 1024), PEP2 (N/A),
   *	PEP3 (2 * 512), PEP4 (2 * 512)
   * Config  8: PEP1 (2 * 1024), PEP2 (N/A),
   *	PEP3 (4 * 512), PEP4 (N/A)
   * Config  9: PEP1 (2 * 1024), PEP2 (N/A),
   *	PEP3 (2 * 1024), PEP4 (N/A)
   * Config 10: PEP1 (3 * 512), PEP2 (N/A),
   *	PEP3 (3 * 512), PEP4 (2 * 512)
   * Config 11: PEP1 (3 * 1024), PEP2 (N/A),
   *	PEP3 (N/A), PEP4 (2 * 512)
   * Config 12: PEP1 (4 * 1024), PEP2 (N/A),
   *	PEP3 (N/A), PEP4 (N/A)

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_CONFIGURATION - the configuration given
   *	is not between 1 and 12
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_physical_configuration(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The physical endpoint configuration number */
	uint8_t			config
	);

/* Summary
   This function sets the hardware configuration for a given endpoint

   Description
   This function sets the hardware configuration for a given endpoint.
   This is the method to set the direction of the endpoint, the type
   of endpoint, the size of the endpoint buffer, and the buffering
   style for the endpoint.

   * Valid In Asynchronous Callback: NO

   Notes
   Add documentation about endpoint configuration limitations

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is invalid
   * CY_AS_ERROR_INVALID_CONFIGURATION - the endpoint configuration
   *	given is not valid
   * CY_AS_ERROR_ENDPOINT_CONFIG_NOT_SET - the physical endpoint
   *	configuration is not set

   See Also
   * CyAsUsbGetEndPointConfig
   * CyAsUsbEndPointConfig
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_end_point_config(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t ep,
	/* The configuration information for the endpoint */
	cy_as_usb_end_point_config *config_p
	);

/* Summary
   This function retreives the hardware configuration for
   a given endpoint

   Description
   This function gets the hardware configuration for the given
   endpoint.  This include information about the direction of
   the endpoint, the type of endpoint, the size of the endpoint
   buffer, and the buffering style for the endpoint.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is
   *	invalid

   See Also
   * CyAsUsbSetEndPointConfig
   * CyAsUsbEndPointConfig
*/
EXTERN cy_as_return_status_t
cy_as_usb_get_end_point_config(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest*/
	cy_as_end_point_number_t ep,
	/* The return value containing the endpoint config
	 * information */
	cy_as_usb_end_point_config *config_p
	);

/* Summary
   This function commits the configuration information that
   has previously been set.

   Description
   The initialization process involves calling CyAsUsbSetEnumConfig()
   and CyAsUsbSetEndPointConfig(). These
   functions do not actually send the configuration information to
   the West Bridge device.  Instead, these
   functions store away the configuration information and this
   CyAsUsbCommitConfig() actually finds the
   best hardware configuration based on the requested endpoint
   configuration and sends this optimal
   confiuration down to the West Bridge device.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - a configuration was found and sent
   *	to West Bridge
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_INVALID_CONFIGURATION - the configuration requested
   *	is not possible
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running

   See Also
   * CyAsUsbSetEndPointConfig
   * CyAsUsbSetEnumConfig
*/

EXTERN cy_as_return_status_t
cy_as_usb_commit_config(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function reads data from a USB endpoint.

   Description
   This function reads data from an OUT.  This function blocks
   until the read is complete.
   If this is a packet read, a single received USB packet will
   complete the read.  If this
   is not a packet read, this function will block until all of
   the data requested has been
   recevied.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is
   *	invalid

   See Also
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteData
   * CyAsUsbWriteDataAsync
*/
EXTERN cy_as_return_status_t
cy_as_usb_read_data(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* If CyTrue, this is a packet read */
	cy_bool				pktread,
	/* The amount of data to read */
	uint32_t			dsize,
	/* The amount of data read */
	uint32_t *dataread,
	/* The buffer to hold the data read */
	void *data
	);

/* Summary
   This function reads data from a USB endpoint

   Description
   This function reads data from an OUT endpoint. This
   function will return immediately and the callback
   provided will be called when the read is complete.
   If this is a packet read, then the callback will be
   called on the next received packet.  If this is not a
   packet read, the callback will be called when the
   requested data is received.

   * Valid In Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is
   *	invalid

   See Also
   * CyAsUsbReadData
   * CyAsUsbWriteData
   * CyAsUsbWriteDataAsync
*/
EXTERN cy_as_return_status_t
cy_as_usb_read_data_async(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* If CyTrue, this is a packet read */
	cy_bool		pktread,
	/* The amount of data to read */
	uint32_t	dsize,
	/* The buffer for storing the data */
	void *data,
	/* The callback function to call when the data is read */
	cy_as_usb_io_callback		callback
	);

/* Summary
   This function writes data to a USB endpoint

   Description
   This function writes data to an IN endpoint data buffer.
   Multiple USB packets may be sent until all data requeste
   has been sent.  This function blocks until all of the data
   has been sent.

   * Valid In Asynchronous Callback: NO

   Notes
   Calling this function with a dsize of zero will result in
   a zero length packet transmitted to the USB host.

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is
   *	invalid

   See Also
   * CyAsUsbReadData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteDataAsync
*/
EXTERN cy_as_return_status_t
cy_as_usb_write_data(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint to write data to */
	cy_as_end_point_number_t		ep,
	/* The size of the data to write */
	uint32_t			dsize,
	/* The data buffer */
	void *data
	);

/* Summary
   This function writes data to a USB endpoint

   Description
   This function writes data to an IN endpoint data buffer.
   This function returns immediately and when the write
   completes, or if an error occurs, the callback function
   is called to indicate  completion of the write operation.

   * Valid In Asynchronous Callback: YES

   Notes
   Calling this function with a dsize of zero will result
   in a zero length packet transmitted to the USB host.

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down successfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint parameter is
   *	invalid

   See Also
   * CyAsUsbReadData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteData
*/
EXTERN cy_as_return_status_t
cy_as_usb_write_data_async(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint to write data to */
	cy_as_end_point_number_t ep,
	/* The size of the data */
	uint32_t dsize,
	/* The buffer containing the data */
	void *data,
	/* If true, send a short packet to terminate data */
	cy_bool	spacket,
	/* The callback to call when the data is written */
	cy_as_usb_io_callback		callback
	);

/* Summary
   This function aborts an outstanding asynchronous
   operation on a given endpoint

   Description
   This function aborts any outstanding operation that is
   pending on the given endpoint.

   * Valid In Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_SUCCESS - this module was shut down
   *	successfully
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not
   *	running
   * CY_AS_ERROR_ASYNC_NOT_PENDING - no asynchronous USB
   *	operation was pending

   See Also
   * CyAsUsbReadData
   * CyAsUsbReadDataAsync
   * CyAsUsbWriteData
   * CyAsUsbWriteDataAsync
*/
EXTERN cy_as_return_status_t
cy_as_usb_cancel_async(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep
	);

/* Summary
   This function sets a stall condition on a given endpoint

   Description
   This function sets a stall condition on the given endpoint.
   If the callback function is not zero, the function is
   executed asynchronously and the callback is called when
   the function is completed.  If the callback function is
   zero, this function executes synchronously and will not
   return until the function has completed.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was invalid,
   *	or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK (only if no cb supplied)
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbGetStall
   * CyAsUsbClearStall
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_stall(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
);

/* Summary
   This function clears a stall condition on a given endpoint

   Description
   This function clears a stall condition on the given endpoint.
   If the callback function is not zero, the function is
   executed asynchronously and the callback is called when the
   function is completed.  If the callback function is zero, this
   function executes synchronously and will not return until the
   function has completed.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was invalid,
   *	or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK (only if no cb supplied)
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbGetStall
   * CyAsUsbSetStall
*/

EXTERN cy_as_return_status_t
cy_as_usb_clear_stall(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t				client
	);


/* Summary
   This function returns the stall status for a given endpoint

   Description
   This function returns the stall status for a given endpoint

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was invalid,
   *	or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbGetStall
   * CyAsUsbSetStall
   * CyAsUsbClearStall
*/

EXTERN cy_as_return_status_t
cy_as_usb_get_stall(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The return value for the stall state */
	cy_bool *stall_p,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function sets a NAK condition on a given endpoint

   Description
   This function sets a NAK condition on the given endpoint.
   If the callback function is not zero, the function is
   executed asynchronously and the callback is called when
   the function is completed.  If the callback function is
   zero, this function executes synchronously and will not
   return until the function has completed.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was
   *	invalid, or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK (only if no cb supplied)
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbGetNak
   * CyAsUsbClearNak
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_nak(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
);

/* Summary
   This function clears a NAK condition on a given endpoint

   Description
   This function clears a NAK condition on the given endpoint.
   If the callback function is not zero, the function is
   executed asynchronously and the callback is called when the
   function is completed.  If the callback function is zero,
   this function executes synchronously and will not return
   until the function has completed.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was invalid,
   *	or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK (only if no cb supplied)
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbGetNak
   * CyAsUsbSetNak
*/
EXTERN cy_as_return_status_t
cy_as_usb_clear_nak(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t	ep,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

/* Summary
   This function returns the NAK status for a given endpoint

   Description
   This function returns the NAK status for a given endpoint

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_INVALID_ENDPOINT - the endpoint given was invalid,
   *	or was not configured as an OUT endpoint
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsUsbSetNak
   * CyAsUsbClearNak
*/
EXTERN cy_as_return_status_t
cy_as_usb_get_nak(
	/* Handle to the West Bridge device */
	cy_as_device_handle			handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The return value for the stall state */
	cy_bool *nak_p,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t					client
);

/* Summary
   This function triggers a USB remote wakeup from the Processor
   API

   Description
   When there is a Suspend condition on the USB bus, this function
   programmatically takes the USB bus out of thi suspend state.

  * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_INVALID_IN_CALLBACK
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_IN_SUSPEND

*/
EXTERN cy_as_return_status_t
cy_as_usb_signal_remote_wakeup(
	 /* Handle to the West Bridge device */
	cy_as_device_handle			handle,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t					client
	);

/* Summary
   This function sets the threshold levels for mass storage progress
   reports from the West Bridge.

   Description
   The West Bridge firmware can be configured to track the amount of
   read/write activity on the mass storage device, and send progress
   reports when the activity level has crossed a threshold level.
   This function sets the threshold levels for the progress reports.
   Set wr_sectors and rd_sectors to 0, if the progress reports are to
   be turned off.

   * Valid In Asynchronous Callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the function succeeded
   * CY_AS_ERROR_NOT_RUNNING - the USB stack is not running
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE - Bad handle
   * CY_AS_ERROR_INVALID_IN_CALLBACK - Synchronous call made
   *	while in callback
   * CY_AS_ERROR_OUT_OF_MEMORY - Failed allocating memory for
   *	request processing
   * CY_AS_ERROR_NOT_SUPPORTED - Firmware version does not support
   *	mass storage progress tracking
   * CY_AS_ERROR_INVALID_RESPONSE - Unexpected response from
   *	Firmware

   See Also
   * CyAsUsbMSCProgressData
   * CyAsEventUsbMSCProgress
*/
EXTERN cy_as_return_status_t
cy_as_usb_set_m_s_report_threshold(
	/* Handle to the West Bridge device */
	cy_as_device_handle handle,
	/* Number of sectors written before report is sent */
	uint32_t wr_sectors,
	/* Number of sectors read before report is sent */
	uint32_t rd_sectors,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t					client
	);

/* Summary
   Specify which of the partitions on a partitioned mass storage
   device should be made visible to USB.

   Description
   West Bridge firmware supports the creation of up to two
   partitions on mass storage devices connected to the West Bridge
   device.  When there are two partitions on a device, the user can
   choose which of these partitions should be made visible to the
   USB host through the USB mass storage interface.  This function
   allows the user to configure the partitions that should be
   enumerated.  At least one partition should be selected through
   this API.  If neither partition needs to be enumerated, use
   CyAsUsbSetEnumConfig to control this.

   * Valid in Asynchronous callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - operation completed successfully
   * CY_AS_ERROR_INVALID_HANDLE - invalid handle to the West
   *	Bridge device
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - no firmware running on West
   *	Bridge device
   * CY_AS_ERROR_NOT_RUNNING - USB stack has not been started
   * CY_AS_ERROR_IN_SUSPEND - West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_INVALID_CALL_SEQUENCE - this API has to be
   *	called before CyAsUsbSetEnumConfig
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to get memory to
   *	process the request
   * CY_AS_ERROR_NO_SUCH_UNIT - Storage device addressed has
   *	not been partitioned
   * CY_AS_ERROR_NOT_SUPPORTED - operation is not supported by
   *	active device/firmware.

   See Also
   * CyAsStorageCreatePPartition
   * CyAsStorageRemovePPartition
   * CyAsUsbMsType_t
 */
EXTERN cy_as_return_status_t
cy_as_usb_select_m_s_partitions(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Bus index of the device being addressed */
	cy_as_bus_number_t bus,
	/* Device id of the device being addressed */
	uint32_t device,
	/* Selection of partitions to be enumerated */
	cy_as_usb_m_s_type_t type,
	/* The callback, if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

extern cy_as_media_type
cy_as_storage_get_media_from_address(uint16_t v);

extern cy_as_bus_number_t
cy_as_storage_get_bus_from_address(uint16_t v);

extern uint32_t
cy_as_storage_get_device_from_address(uint16_t v);

/* For supporting deprecated functions */
#include "cyasusb_dep.h"

#include "cyas_cplus_end.h"

#endif				/* _INCLUDED_CYASUSB_H_ */
