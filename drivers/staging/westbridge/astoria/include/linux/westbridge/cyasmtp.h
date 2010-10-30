/* Cypress West Bridge API header file (cyasmtp.h)
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

#ifndef _INCLUDED_CYASMTP_H_
#define _INCLUDED_CYASMTP_H_

#include "cyasmisc.h"

#include "cyas_cplus_start.h"

/*@@Media Transfer Protocol (MTP) Overview
  Summary
  The MTP API has been designed to allow MTP enabled West Bridge
  devices to implement the MTP protocol while maintaining high
  performance. West Bridge has the capability to enter into a
  Turbo mode during a MTP SendObject or GetObject operation
  enabling it to directly stream the data into or out of the
  attached SD card with minimal involvement from the Processor.

  Description
  The MTP API is designed to act as a pass through implementation
  of the MTP protocol for all operations. Each MTP transaction
  received from the Host is passed through West Bridge and along
  to the Processor. The Processor can then respond to the
  transaction and pass data and/or responses back to the Host
  through West Bridge.

  The MTP API also allows for a high speed handling of MTP
  SendObject and GetObject operations, referred to as Turbo MTP.
  During a Turbo MTP operation West Bridge is responsible for
  reading or writing the data for the MTP operation directly from
  or to the SD card with minimal interaction from the Processor.
  The is done by having the Processor transfer a Block Table
  to West Bridge which contains the locations on the SD card that
  need to be read or written. During the handling of a Turbo
  Operation the Processor will then only periodically need to
  send a new Block Table to West Bridge when the first is used up.
  See the CyAsMTPInitSendObject and CyAsMTPInitGetObject functions
  for more details.

  In order to enable the MTP API you must first have a MTP enabled
  West Bridge loaded with MTP firmware. You then must start the USB
  and Storage APIs before starting the MTP API. See CyAsMTPStart
  for more details.
*/

/*@@Endpoints
  Summary
  When using MTP firmware endpoints 2 and 6 are dedicated
  to bulk MTP traffic and endpoint 1 is available for MTP
  events.

  Description
  When using a MTP enabled West Brdige device endpoints 2 and
  6 are made available for use to implement the MTP protocol.
  These endpoints have a few special restrictions noted below
  but otherwise the existing USB APIs can be used normally with
  these endpoints.

  1. CyAsUsbSetNak, CyAsUsbClearNak, and CyAsUsbGetNak are
	disabled for these endpoints
  2. During a turbo operation CyAsUsbSetStall, CyAsUsbClearStall,
	and CyAsUsbGetStall are disabled.

*/


/* Summary
   This constants defines the maximum number of
   entries in the Block Table used to describe
   the locations for Send/GetObject operations.

   See Also
   * CyAsMtpSendObject
   * CyAsMtpGetObject
*/
#define CY_AS_MAX_BLOCK_TABLE_ENTRIES 64

/* Summary
   Endpoint to be used for MTP reads from the USB host.
 */
#define CY_AS_MTP_READ_ENDPOINT		 (2)

/* Summary
   Endpoint to be used fro MTP writes to the USB host.
 */
#define CY_AS_MTP_WRITE_ENDPOINT		(6)

/******************************************
 * MTP Types
 ******************************************/

/* Summary
   The BlockTable used for turbo operations.

   Description
   This struct is used to specify the blocks
   to be used for both read/write and send/getObject
   operations.

   The start block is a starting Logical Block Address
   and num block is the number of blocks in that contiguous
   region.

   start_blocks[i]->[-------] <- start_blocks[i] + num_blocks[i]

   If you need fewer than CY_AS_MAX_BLOCK_TABLE_ENTRIES
   the remainder should be left empty. Empty is defined
   as num_blocks equal to 0.

   See Also
   * CyAsMTPInitSendObject
   * CyAsMTPInitGetObject

*/
typedef struct cy_as_mtp_block_table {
	uint32_t start_blocks[CY_AS_MAX_BLOCK_TABLE_ENTRIES];
	uint16_t num_blocks[CY_AS_MAX_BLOCK_TABLE_ENTRIES];
} cy_as_mtp_block_table;

/* Summary
   This type specifies the type of MTP event that has occurred.

   Description
   MTP events are used to communicate that West Bridge has
   either finished the handling of the given operation, or
   that it requires additional data to complete the operation.

   In no case does West Bridge send any MTP protocol responses,
   this always remain the responsibility of the client.

   See Also
   * CyAsMTPInitSendObject
   * CyAsMTPInitGetObject
   * CyAsMTPSendBlockTable

*/
typedef enum cy_as_mtp_event {
	/* This event is sent when West Bridge
	has finished writing the data from a
	send_object. west bridge will -not- send
	the MTP response. */
	cy_as_mtp_send_object_complete,

	/* This event is sent when West Bridge
	has finished sending the data for a
	get_object operation. west bridge will
	-not- send the MTP response. */
	cy_as_mtp_get_object_complete,

	/* This event is called when West Bridge
	needs a new block_table. this is only a
	notification, to transfer a block_table
	to west bridge the cy_as_mtp_send_block_table
	use the function. while west bridge is waiting
	for a block_table during a send_object it
	may need to NAK the endpoint. it is important
	that the cy_as_mtp_send_block_table call is made
	in a timely manner as eventually a delay
	will result in an USB reset. this event has
	no data */
	cy_as_mtp_block_table_needed
} cy_as_mtp_event;

/* Summary
   Data for the CyAsMTPSendObjectComplete event.

   Description
   Notification that a SendObject operation has been
   completed. The status of the operation is given
   (to distinguish between a cancelled and a success
   for example) as well as the block count. The blocks
   are used in order based on the current block table.
   If more than one block table was used for a given
   SendObject the count will include the total number
   of blocks written.

   This callback will be made only once per SendObject
   operation and it will only be called after all of
   the data has been committed to the SD card.

   See Also
   * CyAsMTPEvent

 */
typedef struct cy_as_mtp_send_object_complete_data {
	cy_as_return_status_t status;
	uint32_t byte_count;
	uint32_t transaction_id;
} cy_as_mtp_send_object_complete_data;

/*  Summary
	Data for the CyAsMTPGetObjectComplete event.

	Description
	Notification that a GetObject has finished. This
	event allows the P side to know when to send the MTP
	response for the GetObject operation.

	See Also
	* CyAsMTPEvent

*/
typedef struct cy_as_mtp_get_object_complete_data {
	cy_as_return_status_t status;
	uint32_t byte_count;
} cy_as_mtp_get_object_complete_data;

/*  Summary
	MTP Event callback.

	Description
	Callback used to communicate that a SendObject
	operation has finished.

	See Also
	* CyAsMTPEvent
*/
typedef void (*cy_as_mtp_event_callback)(
	cy_as_device_handle handle,
	cy_as_mtp_event evtype,
	void *evdata
	);

/* Summary
   This is the callback function called after asynchronous API
   functions have completed.

   Description
   When calling API functions from callback routines (interrupt
   handlers usually) the async version of these functions must
   be used. This callback is called when an asynchronous API
   function has completed.
*/
typedef void (*cy_as_mtp_function_callback)(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* The error status of the operation */
	cy_as_return_status_t	status,
	/* A client supplied 32 bit tag */
	uint32_t client
);

/**************************************
 * MTP Functions
 **************************************/

/* Summary
   This function starts the MTP stack.

   Description
   Initializes West Bridge for MTP activity and registers the MTP
   event callback.

   Before calling CyAsMTPStart, CyAsUsbStart and CyAsStorageStart must be
   called (in either order).

   MTPStart must be called before the device is enumerated. Please
   see the documentation for CyAsUsbSetEnumConfig and CyAsUsbEnumControl
   for details on enumerating a device for MTP.

   Calling MTPStart will not affect any ongoing P<->S traffic.

   This requires a MTP firmware image to be loaded on West Bridge.

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_IN_SUSPEND
   * CY_AS_ERROR_INVALID_IN_CALLBACK
   * CY_AS_ERROR_STARTSTOP_PENDING
   * CY_AS_ERROR_NOT_RUNNING - CyAsUsbStart or CyAsStorageStart
   *	have not been called
   * CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
   *	firmware with MTP support
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE


   See Also
   * CyAsMTPStop
   * CyAsUsbStart
   * CyAsStorageStart
   * CyAsUsbSetEnumConfig
   * CyAsUsbEnumControl
*/
cy_as_return_status_t
cy_as_mtp_start(
	cy_as_device_handle handle,
	cy_as_mtp_event_callback event_c_b,
	cy_as_function_callback cb,
	uint32_t client
	);


/*  Summary
	This function stops the MTP stack.

	Description
	Stops all MTP activity. Any ongoing transfers are
	canceled.

	This will not cause a UsbDisconnect but all
	MTP activity (both pass through and turbo) will
	stop.

	Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_IN_SUSPEND
   * CY_AS_ERROR_INVALID_IN_CALLBACK
   * CY_AS_ERROR_STARTSTOP_PENDING
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE


	See Also
	* CyAsMTPStart
*/
cy_as_return_status_t
cy_as_mtp_stop(
	cy_as_device_handle handle,
	cy_as_function_callback cb,
	uint32_t client
	);

/*  Summary
	This function sets up a Turbo SendObject operation.

	Description
	Calling this function will setup West Bridge to
	enable Tubo handling of the next SendObject
	operation received. This will pass down the initial
	block table to the firmware and setup a direct u->s
	write for the SendObject operation.

	If this function is not called before a SendObject
	operation is seen  the SendObject operation and data
	will be passed along to the P port like any other MTP
	command. It would then be the responsibility of the
	client to perform a normal StorageWrite call to
	store the data on the SD card. N.B. This will be
	very slow compared with the Turbo handling.

	The completion of this function only signals that
	West Bridge has been set up to receive the next SendObject
	operation. When the SendObject operation has been fully
	handled and the data written to the SD card a separate
	event will be triggered.

	Returns
	* CY_AS_ERROR_SUCCESS
	* CY_AS_ERROR_INVALID_HANDLE
	* CY_AS_ERROR_NOT_CONFIGURED
	* CY_AS_ERROR_NO_FIRMWARE
	* CY_AS_ERROR_IN_SUSPEND
	* CY_AS_ERROR_NOT_RUNNING
	* CY_AS_ERROR_OUT_OF_MEMORY
	* CY_AS_ERROR_ASYNC_PENDING
	* CY_AS_ERROR_INVALID_RESPONSE
	* CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
	*	firmware with MTP support

	See Also
	* CyAsMTPCancelSendObject
	* CyAsMTPInitGetObject
	* CyAsMTPEvent
	* CyAsMTPSendBlockTable
*/
cy_as_return_status_t
cy_as_mtp_init_send_object(
	cy_as_device_handle handle,
	cy_as_mtp_block_table *blk_table,
	uint32_t num_bytes,
	cy_as_function_callback cb,
	uint32_t client
	);

/* Summary
   This function cancels an ongoing MTP operation.

   Description
   Causes West Bridge to cancel an ongoing SendObject
   operation. Note this is only a cancel to West Bridge,
   the MTP operation still needs to be canceled by
   sending a response.

   West Bridge will automatically set a Stall on the endpoint
   when the cancel is received.

   This function is only valid after CyAsMTPInitSendObject
   has been called, but before the CyAsMTPSendObjectComplete
   event has been sent.

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
   *	firmware with MTP support
   * CY_AS_ERROR_NO_OPERATION_PENDING

   See Also
   * CyAsMTPInitSendObject
*/
cy_as_return_status_t
cy_as_mtp_cancel_send_object(
	cy_as_device_handle handle,
	cy_as_function_callback cb,
	uint32_t client
	);

/* Summary
   This function sets up a turbo GetObject operation.

   Description
   Called by the P in response to a GetObject
   operation. This provides West Bridge with the block
   addresses for the Object data that needs to be
   transferred.

   It is the responsibility of the Processor to send the MTP
   operation before calling CyAsMTPInitGetObject. West Bridge
   will then send the data phase of the transaction,
   automatically creating the required container for Data.
   Once all of the Data has been transferred a callback will
   be issued to inform the Processor that the Data phase has
   completed allowing it to send the required MTP response.

   If an entire Block Table is used then after the
   last block is transferred the CyAsMTPBtCallback
   will be called to allow an additional Block Table(s)
   to be specified.

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_IN_SUSPEND
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_ASYNC_PENDING
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
   *	firmware with MTP support

   See Also
   * CyAsMTPInitSendObject
   * CyAsMTPCancelGetObject
   * CyAsMTPEvent
   * CyAsMTPSendBlockTable
*/
cy_as_return_status_t
cy_as_mtp_init_get_object(
	cy_as_device_handle handle,
	cy_as_mtp_block_table *table_p,
	uint32_t num_bytes,
	uint32_t transaction_id,
	cy_as_function_callback cb,
	uint32_t client
	);

/* Summary
   This function cancels an ongoing turbo GetObject
   operation.

   Description
   Causes West Bridge to cancel an ongoing GetObject
   operation. Note this is only a cancel to West Bridge,
   the MTP operation still needs to be canceled by
   sending a response.

   This function is only valid after CyAsMTPGetSendObject
   has been called, but before the CyAsMTPGetObjectComplete
   event has been sent.

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
   *	firmware with MTP support
   * CY_AS_ERROR_NO_OPERATION_PENDING

   See Also
   * CyAsMTPInitGetObject
*/
cy_as_return_status_t
cy_as_mtp_cancel_get_object(
	cy_as_device_handle handle,
	cy_as_function_callback cb,
	uint32_t client
	);

/* Summary
   This function is used to transfer a BlockTable as part of
   an ongoing MTP operation.

   Description
   This function is called in response to the
   CyAsMTPBlockTableNeeded event. This allows the client to
   pass in a BlockTable structure to West Bridge.

   The memory associated with the table will be copied and
   can be safely disposed of when the function returns if
   called synchronously, or when the callback is made if
   called asynchronously.

   This function is used for both SendObject and GetObject
   as both can generate the CyAsMTPBlockTableNeeded event.

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_IN_SUSPEND
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_ASYNC_PENDING
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_SUPPORTED - West Bridge is not running
   *	firmware with MTP support

   See Also
   * CyAsMTPInitSendObject
   * CyAsMTPInitGetObject
*/
cy_as_return_status_t
cy_as_mtp_send_block_table(
	cy_as_device_handle handle,
	cy_as_mtp_block_table *table,
	cy_as_function_callback cb,
	uint32_t client
	);

/* Summary
   This function is used to mark the start of a storage
   read/write burst from the P port processor.

   Description
   This function is used to mark the start of a storage
   read/write burst from the processor. All USB host access
   into the mass storage / MTP endpoints will be blocked
   while the read/write burst is ongoing, and will be allowed
   to resume only after CyAsMTPStorageOnlyStop is called.
   The burst mode is used to reduce the firmware overhead
   due to configuring the internal data paths repeatedly,
   and can help improve performance when a sequence of
   read/writes is performed in a burst.

   This function will not generate a special mailbox request,
   it will only set a flag on the next Storage Read/Write
   operation. Until such a call is made West Bridge will
   continue to accept incoming packets from the Host.

   * Valid in Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_INVALID_HANDLE - Invalid West Bridge device
   *	handle was passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not
   *	been configured.
   * CY_AS_ERROR_NO_FIRMWARE - Firmware is not active on West
   *	Bridge device.
   * CY_AS_ERROR_NOT_RUNNING - Storage stack is not running.
   * CY_AS_ERROR_SUCCESS - Burst mode has been started.

   See Also
   * CyAsStorageReadWriteBurstStop
 */
cy_as_return_status_t
cy_as_mtp_storage_only_start(
	/* Handle to the West Bridge device. */
	cy_as_device_handle handle
	);

/* Summary
   This function is used to mark the end of a storage read/write
   burst from the P port processor.

   Description
   This function is used to mark the end of a storage read/write
   burst from the processor.  At this point, USB access to the
   mass storage / MTP endpoints on the West Bridge device will be
   re-enabled.

   * Valid in Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_INVALID_HANDLE - Invalid West Bridge device handle
   *	was passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not been
   *	configured.
   * CY_AS_ERROR_NO_FIRMWARE - Firmware is not active on West Bridge
   *	device.
   * CY_AS_ERROR_NOT_RUNNING - Storage stack is not running.
   * CY_AS_ERROR_INVALID_IN_CALLBACK - This API cannot be called
   *	from a callback.
   * CY_AS_ERROR_OUT_OF_MEMORY - Failed to allocate memory to
   *	process the request.
   * CY_AS_ERROR_TIMEOUT - Failed to send request to firmware.
   * CY_AS_ERROR_SUCCESS - Burst mode has been stopped.

   See Also
   * CyAsStorageReadWriteBurstStart
 */
cy_as_return_status_t
cy_as_mtp_storage_only_stop(
	/* Handle to the West Bridge device. */
	cy_as_device_handle handle,
	cy_as_function_callback cb,
	uint32_t client
	);

#include "cyas_cplus_end.h"

#endif
