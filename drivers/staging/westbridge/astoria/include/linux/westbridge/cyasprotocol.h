/* Cypress West Bridge API header file (cyasprotocol.h)
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

#ifndef _INCLUDED_CYASPROTOCOL_H_
#define _INCLUDED_CYASPROTOCOL_H_

/*
 * Constants defining the per context buffer sizes
 */
#ifndef __doxygen__
#define CY_CTX_GEN_MAX_DATA_SIZE  (8)
#define CY_CTX_RES_MAX_DATA_SIZE  (8)
#define CY_CTX_STR_MAX_DATA_SIZE  (64)
#define CY_CTX_USB_MAX_DATA_SIZE  (130 + 23)
#define CY_CTX_TUR_MAX_DATA_SIZE  (12)
#endif

/* Summary
   This response indicates a command has been processed
   and returned a status.

   Direction
   West Bridge -> P Port Processor
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = all
   * Response Code = 0

   D0
   * 0 = success (CY_AS_ERROR_SUCCESS)
   * non-zero = error code

   Description
   This response indicates that a request was processed
   and no data was generated as a result of the request
   beyond a single 16 bit status value.  This response
   contains the 16 bit data value.
 */
#define CY_RESP_SUCCESS_FAILURE (0)

/* Summary
   This response indicates an invalid request was sent

   Direction
   West Bridge -> P Port Processor
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = all
   * Response Code = 1

   D0
   * Mailbox contents for invalid request

   Description
   This response is returned when a request is sent
   that contains an invalid
   context or request code.
*/
#define CY_RESP_INVALID_REQUEST	(1)

/* Summary
   This response indicates a request of invalid length was sent

   Direction
   West Bridge -> P Port Processor
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = all
   * Response Code = 2

   D0
   * Mailbox contenxt for invalid request
   * Length for invalid request

   Description
   The software API and firmware sends requests across the
   P Port to West Bridge interface on different contexts.
   Each contexts has a maximum size of the request packet
   that can be received.  The size of a request can be
   determined during the first cycle of a request transfer.
   If the request is larger than can be handled by the
   receiving context this response is returned.  Note that
   the complete request is received before this response is
   sent, but that the request is dropped after this response
   is sent.
*/
#define CY_RESP_INVALID_LENGTH (2)


/* Summary
   This response indicates a request was made to an
   invalid storage address.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
	* Context = all
	* Response Code = 0

   D0
   Bits 15 - 12 : Media Type
		   * 0 = NAND
		   * 1 = SD Flash
		   * 2 = MMC Flash
		   * 3 = CE-ATA

   Bits 11 - 8 : Zero based device index

   Bits 7 - 0 : Zero based unit index

   D1
   Upper 16 bits of block address

   D2
   Lower 16 bits of block address

   D3
   Portion of address that is invalid
		* 0 = Media Type
		* 1 = Device Index
		* 2 = Unit Index
		* 3 = Block Address

   Description
   This response indicates a request to an invalid storage media
   address
 */
#define CY_RESP_NO_SUCH_ADDRESS (3)


/******************************************************/

/*@@General requests
	Summary
	The general requests include:
	* CY_RQT_GET_FIRMWARE_VERSION
	* CY_RQT_SET_TRACE_LEVEL
	* CY_RQT_INITIALIZATION_COMPLETE
	* CY_RQT_READ_MCU_REGISTER
	* CY_RQT_WRITE_MCU_REGISTER
	* CY_RQT_STORAGE_MEDIA_CHANGED
	* CY_RQT_CONTROL_ANTIOCH_HEARTBEAT
	* CY_RQT_PREPARE_FOR_STANDBY
	* CY_RQT_ENTER_SUSPEND_MODE
	* CY_RQT_OUT_OF_SUSPEND
	* CY_RQT_GET_GPIO_STATE
	* CY_RQT_SET_GPIO_STATE
	* CY_RQT_SET_SD_CLOCK_FREQ
	* CY_RQT_WB_DEVICE_MISMATCH
	* CY_RQT_BOOTLOAD_NO_FIRMWARE
	* CY_RQT_RESERVE_LNA_BOOT_AREA
	* CY_RQT_ABORT_P2S_XFER
 */

#ifndef __doxygen__
#define CY_RQT_GENERAL_RQT_CONTEXT	(0)
#endif

/* Summary
   This command returns the firmware version number,
   media types supported and debug/release mode information.

   Direction
   P Port Processor-> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Request Code = 0

   Description
   The response contains the 16-bit major version, the
   16-bit minor version, the 16 bit build number, media
   types supported and release/debug mode information.

   Responses
	* CY_RESP_FIRMWARE_VERSION
 */
#define CY_RQT_GET_FIRMWARE_VERSION	(0)


/* Summary
   This command changes the trace level and trace information
   destination within the West Bridge firmware.

   Direction
   P Port Processor-> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Request Code = 1

   D0
   Trace Level
	* 0 = no trace information
	* 1 = state information
	* 2 = function call
	* 3 = function call with args/return value

   D1
   Bits 12 - 15 : MediaType
	* 0 = NAND
	* 1 = SDIO Flash
	* 2 = MMC Flash
	* 3 = CE-ATA

   Bits 8 - 11 : Zero based device index

   Bits 0 - 7 : Zero based unit index

   Description
   The West Bridge firmware contains debugging facilities that can
   be used to trace the execution of the firmware.  This request
   sets the level of tracing information that is stored and the
   location where it is stored.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_SET_TRACE_LEVEL (1)

/* Summary
   This command indicates that the firmware is up and ready
   for communications with the P port processor.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 0
   * Request Code = 3

   D0
   Major Version

   D1
   Minor Version

   D2
   Build Number

   D3
   Bits 15-8: Media types supported on Bus 1.
   Bits  7-0: Media types supported on Bus 0.
	 Bits 8, 0: NAND support.
	   * 0: NAND is not supported.
	   * 1: NAND is supported.
	 Bits 9, 1: SD memory card support.
	   * 0: SD memory card is not supported.
	   * 1: SD memory card is supported.
	 Bits 10, 2: MMC card support.
	   * 0: MMC card is not supported.
	   * 1: MMC card is supported.
	 Bits 11, 3: CEATA drive support
	   * 0: CEATA drive is not supported.
	   * 1: CEATA drive is supported.
	 Bits 12, 4: SD IO card support.
	   * 0: SD IO card is not supported.
	   * 1: SD IO card is supported.

   D4
   Bits 15 - 8 : MTP information
	 * 0 : MTP not supported in firmware
	 * 1 : MTP supported in firmware
   Bits 7 - 0  : Debug/Release mode information.
	 * 0 : Release mode
	 * 1 : Debug mode

   Description
   When the West Bridge firmware is loaded it being by performing
   initialization.  Initialization must be complete before West
   Bridge is ready to accept requests from the P port processor.
   This request is sent from West Bridge to the P port processor
   to indicate that initialization is complete.

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_INITIALIZATION_COMPLETE	(3)

/* Summary
   This command requests the firmware to read and return the contents
   of a MCU accessible
   register.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 4

   D0
   Address of register to read

   Description
   This debug command allows the processor to read the contents of
   a MCU accessible register.

   Responses
   * CY_RESP_MCU_REGISTER_DATA
 */
#define CY_RQT_READ_MCU_REGISTER	(4)

/* Summary
   This command requests the firmware to write to an MCU
   accessible register.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 5

   D0
   Address of register to be written

   D1
   Bits 15 - 8 : Mask to be applied to existing data.
   Bits 7  - 0 : Data to be ORed with masked data.

   Description
   This debug command allows the processor to write to an MCU
   accessible register.
   Note: This has to be used with caution, and is supported by
   the firmware only in special debug builds.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_WRITE_MCU_REGISTER	(5)

/* Summary
   This command tells the West Bridge firmware that a change in
   storage media has been detected.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 6

   Description
   If the insertion or removal of SD or MMC cards is detected by
   hardware external to West Bridge, this command is used to tell
   the West Bridge firmware to re-initialize the storage controlled
   by the device.

   Responses
   * CY_RESP_SUCCESS_FAILURE
*/
#define CY_RQT_STORAGE_MEDIA_CHANGED (6)

/* Summary
   This command enables/disables the periodic heartbeat message
   from the West Bridge firmware to the processor.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 7

   Description
   This command enables/disables the periodic heartbeat message
   from the West Bridge firmware to the processor. The heartbeat
   message is left enabled by default, and can lead to a loss
   in performance on the P port interface.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_CONTROL_ANTIOCH_HEARTBEAT	 (7)

/* Summary
   This command requests the West Bridge firmware to prepare for
   the device going into standby
   mode.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 8

   Description
   This command is sent by the processor to the West Bridge as
   preparation for going into standby mode. The request allows the
   firmware to complete any pending/cached storage operations before
   going into the low power state.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_PREPARE_FOR_STANDBY (8)

/* Summary
   Requests the firmware to go into suspend mode.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 9

   D0
   Bits 7-0: Wakeup control information.

   Description
   This command is sent by the processor to the West Bridge to
   request the device to be placed in suspend mode. The firmware
   will complete any pending/cached storage operations before
   going into the low power state.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_ENTER_SUSPEND_MODE (9)

/* Summary
   Indicates that the device has left suspend mode.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 10

   Description
   This message is sent by the West Bridge to the Processor
   to indicate that the device has woken up from suspend mode,
   and is ready to accept new requests.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_OUT_OF_SUSPEND	(10)

/* Summary
   Request to get the current state of an West Bridge GPIO pin.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 11

   D0
   Bits 15 - 8 : GPIO pin identifier

   Responses
   * CY_RESP_GPIO_STATE

   Description
   Request from the processor to get the current state of
   an West Bridge GPIO pin.
 */
#define CY_RQT_GET_GPIO_STATE (11)

/* Summary
   Request to update the output value on an West Bridge
   GPIO pin.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 12

   D0
   Bits 15 - 8 : GPIO pin identifier
   Bit  0	  : Desired output state

   Responses
   * CY_RESP_SUCCESS_FAILURE

   Description
   Request from the processor to update the output value on
   an West Bridge GPIO pin.
 */
#define CY_RQT_SET_GPIO_STATE (12)

/* Summary
   Set the clock frequency on the SD interface of the West
   Bridge device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 13

   D0
   Bit 8: Type of SD/MMC media
		  0 = low speed media
		  1 = high speed media
   Bit 0: Clock frequency selection
		  0 = Default frequency
		  1 = Alternate frequency (24 MHz in both cases)

   Description
   This request is sent by the processor to set the operating clock
   frequency used on the SD interface of the device.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_SET_SD_CLOCK_FREQ (13)

/* Summary
   Indicates the firmware downloaded to West Bridge cannot
   run on the active device.

   Direction
   West Bridge -> P Port processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 14

   Description
   Some versions of West Bridge firmware can only run on specific
   types/versions of the West Bridge device.  This error is
   returned when a firmware image is downloaded onto a device that
   does not support it.

   Responses
   * None
 */
#define CY_RQT_WB_DEVICE_MISMATCH	(14)

/* Summary
   This command is indicates that no firmware was found in the
   storage media.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 0
   * Request code = 15

   Description
   The command is received only in case of silicon with bootloader
   ROM. The device sends the request if there is no firmware image
   found in the storage media or the image is  corrupted. The
   device is waiting for P port to download a valid firmware image.

   Responses
   * None
 */
#define CY_RQT_BOOTLOAD_NO_FIRMWARE	(15)

/* Summary
   This command reserves first numzones zones of nand device for
   storing processor boot image.

   Direction
   P Port Processor-> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Request Code = 16

   D0
   Bits 7-0: numzones

   Description
   The first numzones zones in nand device will be used for storing
   proc boot image. LNA firmware in Astoria will work on this nand
   area and boots the processor which will then use the remaining
   nand for usual purposes.

   Responses
	* CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_RESERVE_LNA_BOOT_AREA (16)

/* Summary
   This command cancels the processing of a P2S operation in
   firmware.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Request Code = 17

   Responses
	* CY_RESP_SUCCESS_FAILURE
*/
#define CY_RQT_ABORT_P2S_XFER (17)

/*
 * Used for debugging, ignore for normal operations
 */
#ifndef __doxygen__
#define CY_RQT_DEBUG_MESSAGE (127)
#endif

/******************************************************/

/*@@General responses
   Summary
   The general responses include:
   * CY_RESP_FIRMWARE_VERSION
   * CY_RESP_MCU_REGISTER_DATA
   * CY_RESP_GPIO_STATE
 */


/* Summary
   This response indicates success and contains the firmware
   version number, media types supported by the firmware and
   release/debug mode information.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   MailBox0
	* Context = 0
	* Response Code = 16

   D0
   Major Version

   D1
   Minor Version

   D2
   Build Number

   D3
   Bits 15-8: Media types supported on Bus 1.
   Bits  7-0: Media types supported on Bus 0.
	 Bits 8, 0: NAND support.
	   * 0: NAND is not supported.
	   * 1: NAND is supported.
	 Bits 9, 1: SD memory card support.
	   * 0: SD memory card is not supported.
	   * 1: SD memory card is supported.
	 Bits 10, 2: MMC card support.
	   * 0: MMC card is not supported.
	   * 1: MMC card is supported.
	 Bits 11, 3: CEATA drive support
	   * 0: CEATA drive is not supported.
	   * 1: CEATA drive is supported.
	 Bits 12, 4: SD IO card support.
	   * 0: SD IO card is not supported.
	   * 1: SD IO card is supported.

   D4
   Bits 15 - 8 : MTP information
	 * 0 : MTP not supported in firmware
	 * 1 : MTP supported in firmware
   Bits 7 - 0  : Debug/Release mode information.
	 * 0 : Release mode
	 * 1 : Debug mode

   Description
   This response is sent to return the firmware version
   number to the requestor.
 */
#define CY_RESP_FIRMWARE_VERSION (16)

/* Summary
   This response returns the contents of a MCU accessible
   register to the processor.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Response code = 17

   D0
   Bits 7 - 0 : MCU register contents

   Description
   This response is sent by the firmware in response to the
   CY_RQT_READ_MCU_REGISTER
   command.
 */
#define CY_RESP_MCU_REGISTER_DATA (17)

/* Summary
   Reports the current state of an West Bridge GPIO pin.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
   * Context = 0
   * Request code = 18

   D0
   Bit 0: Current state of the GP input pin

   Description
   This response is sent by the West Bridge to report the
   current state observed on a general purpose input pin.
 */
#define CY_RESP_GPIO_STATE (18)


/* Summary
   This command notifies West Bridge the polarity of the
   SD power pin

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 0
	* Request Code = 19
  D0: CyAnMiscActivehigh / CyAnMiscActivelow

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS

 */

#define CY_RQT_SDPOLARITY (19)

/******************************/

/*@@Resource requests
   Summary

   The resource requests include:
   * CY_RQT_ACQUIRE_RESOURCE
   * CY_RQT_RELEASE_RESOURCE
 */





#ifndef __doxygen__
#define CY_RQT_RESOURCE_RQT_CONTEXT	(1)
#endif


/* Summary
   This command is a request from the P port processor
   for ownership of a resource.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 1
	* Request Code = 0

   D0
   Resource
	* 0 = USB
	* 1 = SDIO/MMC
	* 2 = NAND

   D1
   Force Flag
	* 0 = Normal
	* 1 = Force

   Description
   The resource may be the USB pins, the SDIO/MMC bus,
   or the NAND bus.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_NOT_RELEASED
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_RESOURCE
 */
#define CY_RQT_ACQUIRE_RESOURCE (0)


/* Summary
   This command is a request from the P port processor
   to release ownership of a resource.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 1
	* Request Code = 1

   D0
   Resource
   * 0 = USB
   * 1 = SDIO/MMC
   * 2 = NAND

   Description
   The resource may be the USB pins, the SDIO/MMC bus, or
   the NAND bus.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_NOT_OWNER
 */
#define CY_RQT_RELEASE_RESOURCE (1)


/****************************/

/*@@Storage requests
   Summary
   The storage commands include:
	 * CY_RQT_START_STORAGE
	 * CY_RQT_STOP_STORAGE
	 * CY_RQT_CLAIM_STORAGE
	 * CY_RQT_RELEASE_STORAGE
	 * CY_RQT_QUERY_MEDIA
	 * CY_RQT_QUERY_DEVICE
	 * CY_RQT_QUERY_UNIT
	 * CY_RQT_READ_BLOCK
	 * CY_RQT_WRITE_BLOCK
	 * CY_RQT_MEDIA_CHANGED
	 * CY_RQT_ANTIOCH_CLAIM
	 * CY_RQT_ANTIOCH_RELEASE
	 * CY_RQT_SD_INTERFACE_CONTROL
	 * CY_RQT_SD_REGISTER_READ
	 * CY_RQT_CHECK_CARD_LOCK
	 * CY_RQT_QUERY_BUS
	 * CY_RQT_PARTITION_STORAGE
	 * CY_RQT_PARTITION_ERASE
	 * CY_RQT_GET_TRANSFER_AMOUNT
	 * CY_RQT_ERASE
	 * CY_RQT_SDIO_READ_DIRECT
	 * CY_RQT_SDIO_WRITE_DIRECT
	 * CY_RQT_SDIO_READ_EXTENDED
	 * CY_RQT_SDIO_WRITE_EXTENDED
	 * CY_RQT_SDIO_INIT_FUNCTION
	 * CY_RQT_SDIO_QUERY_CARD
	 * CY_RQT_SDIO_GET_TUPLE
	 * CY_RQT_SDIO_ABORT_IO
	 * CY_RQT_SDIO_INTR
	 * CY_RQT_SDIO_SUSPEND
	 * CY_RQT_SDIO_RESUME
	 * CY_RQT_SDIO_RESET_DEV
	 * CY_RQT_P2S_DMA_START
 */
#ifndef __doxygen__
#define CY_RQT_STORAGE_RQT_CONTEXT (2)
#endif

/* Summary
   This command requests initialization of the storage stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 0

   Description
   This command is required before any other storage related command
   can be send to the West Bridge firmware.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_ALREADY_RUNNING
 */
#define CY_RQT_START_STORAGE (0)


/* Summary
   This command requests shutdown of the storage stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 1

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_NOT_RUNNING
 */
#define CY_RQT_STOP_STORAGE (1)


/* Summary
   This command requests ownership of the given media
   type by the P port processor.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 2

   D0
   Bits 12 - 15 : Bus Index
   Bits  8 - 11 : Zero based device index

   Responses
	* CY_RESP_MEDIA_CLAIMED_RELEASED
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_CLAIM_STORAGE (2)


/* Summary
   This command releases ownership of a given media type
   by the P port processor.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 3

   D0
   Bits 12 - 15 : Bus Index
   Bits  8 - 11 : Zero based device index

   Responses
	* CY_RESP_MEDIA_CLAIMED_RELEASED
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_RELEASE_STORAGE (3)


/* Summary
   This command returns the total number of logical devices
   of the given type of media.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 4

   D0
   Bits 12 - 15 : MediaType
	* 0 = NAND
	* 1 = SDIO Flash
	* 2 = MMC Flash
	* 3 = CE-ATA

   Bits 8 - 11 : Not Used

   Bits 0 - 7 : Not Used

   Responses
	* CY_RESP_MEDIA_DESCRIPTOR
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_QUERY_MEDIA (4)


/* Summary
   This command queries a given device to determine
   information about the number of logical units on
   the given device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 5

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index
   Bits 0 - 7   : Not Used

   Responses
	* CY_RESP_DEVICE_DESCRIPTOR
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_INVALID_PARTITION_TABLE
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_QUERY_DEVICE (5)


/* Summary
   This command queries a given device to determine
   information about the size and location of a logical unit
   located on a physical device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 6

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index
   Bits 0 - 7   : Zero based unit index

   Responses
	* CY_RESP_UNIT_DESCRIPTOR
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_INVALID_PARTITION_TABLE
	* CY_RESP_NO_SUCH_ADDRESS
 */
#define CY_RQT_QUERY_UNIT (6)


/* Summary
   This command initiates the read of a specific block
   from the given media,
   device and unit.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
	* Context = 2
	* Request Code = 7

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index
   Bits 0 - 7   : Zero based unit index

   D1
   Upper 16 bits of block address

   D2
   Lower 16 bits of block address

   D3
   BIT 8 - 15 : Upper 8 bits of Number of blocks

   BIT 0 - 7 : Reserved

   * D4 *
   BITS 8 - 15 : Lower 8 bits of Number of blocks
   BITS 1 -  7 : Not Used
   BIT  0	  : Indicates whether this command is a
   part of a P2S only burst.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_ANTIOCH_DEFERRED_ERROR
 */
#define CY_RQT_READ_BLOCK (7)


/* Summary
   This command initiates the write of a specific block
   from the given media, device and unit.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
	* Context = 2
	* Request Code = 8

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11 : Zero based device index
   Bits 0 - 7 : Zero based unit index

   D1
   Upper 16 bits of block address

   D2
   Lower 16 bits of block address

   D3
   BIT 8 - 15 : Upper 8 bits of Number of blocks

   BIT 0 - 7 : Reserved

   * D4 *
   BITS 8 - 15 : Lower 8 bits of Number of blocks
   BITS 1 -  7 : Not Used
   BIT  0	  : Indicates whether this command is a
    part of a P2S only burst.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_ANTIOCH_DEFERRED_ERROR
 */
#define CY_RQT_WRITE_BLOCK (8)

/* Summary
   This request is sent when the West Bridge device detects
   a change in the status of the media.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request Code = 9

   D0
   Bits 12 - 15 : Bus index
   Bits  0 -  7 : Media type

	D1
	Bit 0 : Action
	 * 0 = Inserted
	 * 1 = Removed

	Description
	When the media manager detects the insertion or removal
	of a media from the West Bridge port, this request is sent
	from the West Bridge device to the P Port processor to
	inform the processor of the change in status of the media.
	This request is sent for both an insert operation and a
	removal operation.

	Responses
	* CY_RESPO_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_MEDIA_CHANGED (9)

/* Summary
   This request is sent when the USB module wishes to claim
   storage media.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request Code = 10

   D0
   Bit 0:
	* 0 = do not release NAND
	* 1 = release NAND

   Bit 1:
	* 0 = do not release SD Flash
	* 1 = release SD Flash

   Bit 2:
	* 0 = do not release MMC flash
	* 1 = release MMC flash

   Bit 3:
	* 0 = do not release CE-ATA storage
	* 1 = release CE-ATA storage

   Bit 8:
	* 0 = do not release storage on bus 0
	* 1 = release storage on bus 0

   Bit 9:
	* 0 = do not release storage on bus 1
	* 1 = release storage on bus 1

   Description
   When the USB cable is attached to the West Bridge device,
   West Bridge will enumerate the storage devices per the USB
   initialization of West Bridge.  In order for West Bridge to
   respond to requests received via USB for the mass storage
   devices, the USB module must claim the storeage.  This
   request is a request to the P port processor to release the
   storage medium.  The medium will not be visible on the USB
   host, until it has been released by the processor.
*/
#define CY_RQT_ANTIOCH_CLAIM (10)

/* Summary
   This request is sent when the P port has asked West Bridge to
   release storage media, and the West Bridge device has
   completed this.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request Code = 11

   D0
   Bit 0:
	* 0 = No change in ownership of NAND storage
	* 1 = NAND ownership has been given to processor

   Bit 1:
	* 0 = No change in ownership of SD storage
	* 1 = SD ownership has been given to processor

   Bit 2:
	* 0 = No change in ownership of MMC storage
	* 1 = MMC ownership has been given to processor

   Bit 3:
	* 0 = No change in ownership of CE-ATA storage
	* 1 = CE-ATA ownership has been given to processor

   Bit 4:
	* 0 = No change in ownership of SD IO device
	* 1 = SD IO device ownership has been given to processor

   Bit 8:
	* 0 = No change in ownership of storage on bus 0
	* 1 = Bus 0 ownership has been given to processor

   Bit 9:
	* 0 = No change in ownership of storage on bus 1
	* 1 = Bus 1 ownership has been given to processor

   Description
   When the P port asks for control of a particular media, West
   Bridge may be able to release the media immediately.  West
   Bridge may also need to complete the flush of buffers before
   releasing the media.  In the later case, West Bridge will
   indicated a release is not possible immediately and West Bridge
   will send this request to the P port when the release has been
   completed.
*/
#define CY_RQT_ANTIOCH_RELEASE (11)

/* Summary
   This request is sent by the Processor to enable/disable the
   handling of SD card detection and SD card write protection
   by the firmware.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 12

   D0
   Bit 8: Enable/disable handling of card detection.
   Bit 1: SDAT_3 = 0, GIPO_0 = 1
   Bit 0: Enable/disable handling of write protection.

   Description
   This request is sent by the Processor to enable/disable
   the handling of SD card detection and SD card write
   protection by the firmware.
 */
#define CY_RQT_SD_INTERFACE_CONTROL	 (12)

/* Summary
   Request from the processor to read a register on the SD
   card, and return the contents.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 13

   D0
   Bits 12 - 15 : MediaType
	* 0 = Reserved
	* 1 = SDIO Flash
	* 2 = MMC Flash
	* 3 = Reserved

   Bits 8 - 11 : Zero based device index
   Bits 0 - 7  : Type of register to read

   Description
   This request is sent by the processor to instruct the
   West Bridge to read a register on the SD/MMC card, and
   send the contents back through the CY_RESP_SD_REGISTER_DATA
   response.
 */
#define CY_RQT_SD_REGISTER_READ	 (13)

/* Summary
   Check if the SD/MMC card connected to West Bridge is
   password locked.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 14

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index

   Description
   This request is sent by the processor to check if the
   SD/MMC connected to the West Bridge is locked with a
   password.
 */
#define CY_RQT_CHECK_CARD_LOCK	(14)

/* Summary
   This command returns the total number of logical devices on the
   given bus

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 15

   D0
   Bits 12 - 15 : Bus Number

   Bits 0 - 11: Not Used

   Responses
	* CY_RESP_BUS_DESCRIPTOR
	* CY_RESP_NO_SUCH_BUS
 */
#define CY_RQT_QUERY_BUS	(15)

/* Summary
   Divide a storage device into two partitions.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
   * Context = 2
   * Request code = 16

   D0
   Bits 12 - 15 : Bus number
   Bits  8 - 11 : Device number
   Bits  0 -  7 : Not used

   D1
   Size of partition 0 (MS word)

   D2
   Size of partition 0 (LS word)

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_PARTITION_STORAGE			 (16)

/* Summary
   Remove the partition table and unify all partitions on
   a storage device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
   * Context = 2
   * Request code = 17

   D0
   Bits 12 - 15 : Bus number
   Bits  8 - 11 : Device number

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_PARTITION_ERASE (17)

/* Summary
   Requests the current transfer amount.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
   * Context = 2
   * Request code = 18

   D0
   Bits 12 - 15 : Bus number
   Bits  8 - 11 : Device number

   Responses
   * CY_RESP_TRANSFER_COUNT
 */
#define CY_RQT_GET_TRANSFER_AMOUNT  (18)

/* Summary
   Erases.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
   * Context = 2
   * Request code = 19

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11 : Zero based device index
   Bits 0 - 7 : Zero based unit index

   D1
   Upper 16 bits of erase unit

   D2
   Lower 16 bits of erase unit

   D3
   BIT 8 - 15 : Upper 8 bits of Number of erase units
   BIT 0 - 7 : Reserved

   * D4 *
   BIT 8 - 15 : Lower 8 bits of Number of erase units
   BIT 0 - 7 : Not Used

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
 */
#define CY_RQT_ERASE (19)

/* Summary
   This command reads 1 byte from an SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 23

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   D1
   Bits 8 - 15  : 0
   Bit  7	   : 0 to indicate a read
   Bits 4 - 6   : Function number
   Bit  3	   : 0
   Bit  2	   : 1 if SDIO interrupt needs to be re-enabled.
   Bits 0 -  1  : Two Most significant bits of Read address

   D2
   Bits 1 - 15  : 15 Least significant bits of Read address
   Bit  0	   : 0


   Responses
	* CY_RESP_SUCCESS_FAILURE
	* CY_RESP_SDIO_DIRECT
*/
#define CY_RQT_SDIO_READ_DIRECT	 (23)

/* Summary
   This command writes 1 byte to an SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 24

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   D1
   Bits 8 - 15  : Data to write
   Bit  7	   : 1 to indicate a write
   Bits 4 - 6   : Function number
   Bit  3	   : 1 if Read after write is enabled
   Bit  2	   : 1 if SDIO interrupt needs to be re-enabled.
   Bits 0 - 1   : Two Most significant bits of write address

   D2
   Bits 1 - 15  : 15 Least significant bits of write address
   Bit  0	   : 0


   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SDIO_DIRECT
*/
#define CY_RQT_SDIO_WRITE_DIRECT (24)

/* Summary
   This command reads performs a multi block/byte read from
   an SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 25

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   D1
   Bit  15	  : 0 to indicate a read
   Bit  12 - 14 : Function Number
   Bit  11	  : Block Mode
   Bit  10	  : OpCode
   Bits  0 -  9 : 10 Most significant bits of Read address

   D2
   Bits 9 - 15  : 7 Least significant bits of address
   Bits 0 -  8  : Block/Byte Count


   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SDIO_EXT
*/
#define CY_RQT_SDIO_READ_EXTENDED (25)

/* Summary
   This command reads performs a multi block/byte write
   to an SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 26

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   D1
   Bit  15	  : 1 to indicate a write
   Bit  12 - 14 : Function Number
   Bit  11	  : Block Mode
   Bit  10	  : OpCode
   Bits  0 -  9 : 10 Most significant bits of Read address

   D2
   Bits 9 - 15  : 7 Least significant bits of address
   Bits 0 -  8  : Block/Byte Count


   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SDIO_EXT
*/
#define CY_RQT_SDIO_WRITE_EXTENDED	(26)

/* Summary
   This command initialises an IO function on the SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 27

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number


   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SDIO_INIT_FUNCTION	 (27)

/* Summary
   This command gets properties of the SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 28

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_QUERY_CARD
*/
#define CY_RQT_SDIO_QUERY_CARD (28)

/* Summary
   This command reads a tuple from the CIS of an SDIO card.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 29

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   D1
   Bits  8 - 15 : Tuple ID to read

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SDIO_GET_TUPLE
*/
#define CY_RQT_SDIO_GET_TUPLE (29)

/* Summary
   This command Aborts an IO operation.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 30

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number


   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SDIO_ABORT_IO (30)

/* Summary
   SDIO Interrupt request sent to the processor from the West Bridge device.

   Direction
   West Bridge ->P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 31

   D0
   Bits 0 - 7 : Bus Index

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SDIO_INTR (31)

/* Summary
   This command Suspends an IO operation.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 32

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SDIO_SUSPEND	 (32)

/* Summary
   This command resumes a suspended operation.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 33

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Zero based function number

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SDIO_RESUME
*/
#define CY_RQT_SDIO_RESUME	(33)

/* Summary
   This command resets an SDIO device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request Code = 34

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : 0

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SDIO_RESET_DEV (34)

/* Summary
   This command asks the API to start the DMA transfer
   for a P2S operation.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Request code = 35

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_P2S_DMA_START (35)

/******************************************************/

/*@@Storage responses
   Summary
   The storage responses include:
	  * CY_RESP_MEDIA_CLAIMED_RELEASED
	  * CY_RESP_MEDIA_DESCRIPTOR
	  * CY_RESP_DEVICE_DESCRIPTOR
	  * CY_RESP_UNIT_DESCRIPTOR
	  * CY_RESP_ANTIOCH_DEFERRED_ERROR
	  * CY_RESP_SD_REGISTER_DATA
	  * CY_RESP_SD_LOCK_STATUS
	  * CY_RESP_BUS_DESCRIPTOR
	  * CY_RESP_TRANSFER_COUNT
	  * CY_RESP_SDIO_EXT
	  * CY_RESP_SDIO_INIT_FUNCTION
	  * CY_RESP_SDIO_QUERY_CARD
	  * CY_RESP_SDIO_GET_TUPLE
	  * CY_RESP_SDIO_DIRECT
	  * CY_RESP_SDIO_INVALID_FUNCTION
	  * CY_RESP_SDIO_RESUME
 */

/* Summary
   Based on the request sent, the state of a given media was
   changed as indicated by this response.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Response Code = 16

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index

   D1
   State of Media
	* 0 = released
	* 1 = claimed
 */
#define	CY_RESP_MEDIA_CLAIMED_RELEASED	(16)


/* Summary
   This response gives the number of physical devices
   associated with a given media type.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Response Code = 17

   D0
   Media Type
	Bits 12 - 15
	* 0 = NAND
	* 1 = SDIO Flash
	* 2 = MMC Flash
	* 3 = CE-ATA

   D1
   Number of devices
 */
#define CY_RESP_MEDIA_DESCRIPTOR (17)


/* Summary
   This response gives description of a physical device.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   MailBox0
	* Context = 2
	* Response Code = 18

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Type of media present on bus

   D1
   Block Size in bytes

   D2
   Bit  15	: Is device removable
   Bit  9	 : Is device password locked
   Bit  8	 : Is device writeable
   Bits 0 - 7 : Number Of Units

   D3
   ERASE_UNIT_SIZE high 16 bits

   D4
   ERASE_UNIT_SIZE low 16 bits

 */
#define CY_RESP_DEVICE_DESCRIPTOR (18)


/* Summary
   This response gives description of a unit on a
   physical device.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   6

   MailBox0
	* Context = 2
	* Response Code = 19

   D0
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index
   Bits 0 - 7   : Zero based unit index

   D1
   Bits 0 - 7   : Media type
	 * 1  = NAND
	 * 2  = SD FLASH
	 * 4  = MMC FLASH
	 * 8  = CEATA
	 * 16 = SD IO

   D2
   Block Size in bytes

   D3
   Start Block Low 16 bits

   D4
   Start Block High 16 bits

   D5
   Unit Size Low 16 bits

   D6
   Unit Size High 16 bits
 */
#define CY_RESP_UNIT_DESCRIPTOR (19)


/* Summary
   This response is sent as error status for P2S
   Storage operation.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 2
   * Request Code = 20

   D0
   Bit 8 : Type of operation (Read / Write)
   Bits 7 - 0 : Error code

   D1
   Bits 12 - 15 : Bus index
   Bits 8 - 11  : Zero based device index
   Bits 0 - 7   : Zero based unit index

   *D2 - D3*
   Address where the error occurred.

   D4
   Length of the operation in blocks.

   Description
   This error is returned by the West Bridge to the
   processor if a storage operation fails due to a
   medium error.
*/
#define CY_RESP_ANTIOCH_DEFERRED_ERROR	 (20)

/* Summary
   Contents of a register on the SD/MMC card connected to
   West Bridge.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   Variable

   Mailbox0
   * Context = 2
   * Request code = 21

   D0
   Length of data in bytes

   D1 - Dn
   The register contents

   Description
   This is the response to a CY_RQT_SD_REGISTER_READ
	request.
*/
#define CY_RESP_SD_REGISTER_DATA (21)

/* Summary
   Status of whether the SD card is password locked.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 22

   D0
   Bit 0 : The card's lock status

   Description
   Status of whether the SD card is password locked.
*/
#define CY_RESP_SD_LOCK_STATUS	(22)


/* Summary
   This response gives the types of physical devices
   attached to a given bus.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 2
	* Response Code = 23

   D0
   Bus Number
	Bits 12 - 15

   D1
   Media present on addressed bus
 */
#define CY_RESP_BUS_DESCRIPTOR	(23)

/* Summary
   Amount of data read/written through the USB mass
   storage/MTP device.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   MailBox0
   * Context = 2
   * Request code = 24

   D0
   MS 16 bits of number of sectors written

   D1
   LS 16 bits of number of sectors written

   D2
   MS 16 bits of number of sectors read

   D3
   LS 16 bits of number of sectors read

   Description
   This is the response to the CY_RQT_GET_TRANSFER_AMOUNT
   request, and represents the number of sectors of data
   that has been written to or read from the storage device
   through the USB Mass storage or MTP interface.
 */
#define CY_RESP_TRANSFER_COUNT (24)

/* Summary
   Status of SDIO Extended read/write operation.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 34

   D0
   Bit 8 : 1 if Read response, 0 if write response
   Bits 0-7: Error Status

   Description
   Status of SDIO Extended read write operation.
*/

#define CY_RESP_SDIO_EXT (34)

/* Summary
   Status of SDIO operation to Initialize a function

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 2
   * Request code = 35


   D0
   Bits 8-15 : Function Interface Code
   Bits 0-7: Extended Function Interface Code

   D1
   Bits 0-15 : Function Block Size

   D2
   Bits 0-15 : Most significant Word of Function PSN

   D3
   Bits 0-15 : Least significant Word of Function PSN

   D4
   Bit 15 : CSA Enabled Status
   Bit 14 : CSA Support Status
   Bit 9  : CSA No Format Status
   Bit 8  : CSA Write Protect Status
   Bit 0  : Function Wake Up Support status

   Description
   Status of SDIO Function Initialization operation.
*/
#define CY_RESP_SDIO_INIT_FUNCTION	(35)

/* Summary
   Status of SDIO operation to query the Card

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 2
   * Request code = 36


   D0
   Bits 8-15 : Number of IO functions present
   Bit 0: 1 if memory is present

   D1
   Bits 0-15 : Card Manufacturer ID

   D2
   Bits 0-15 : Card Manufacturer Additional Information

   D3
   Bits 0-15 : Function 0 Block Size

   D4
   Bits 8-15 :SDIO Card Capability register
   Bits 0-7: SDIO Version


   Description
   Status of SDIO Card Query operation.
   */
#define CY_RESP_SDIO_QUERY_CARD	 (36)
/* Summary
   Status of SDIO CIS read operation

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 37

   D0
   Bit 8 : 1
   Bits 0-7: Error Status

   D1
   Bits 0 - 7 : Size of data read.

   Description
   Status of SDIO Get Tuple Read operation.
 */
#define CY_RESP_SDIO_GET_TUPLE (37)

/* Summary
   Status of SDIO Direct read/write operation.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 38

   D0
   Bit 8 : Error Status
   Bits 0-7: Data Read(If any)

   Description
   Status of SDIO Direct read write operation.

*/
#define CY_RESP_SDIO_DIRECT	 (38)

/* Summary
   Indicates an un-initialized function has been used for IO

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 39

   Description
   Indicates an IO request on an uninitialized function.
*/
#define CY_RESP_SDIO_INVALID_FUNCTION (39)

/* Summary
   Response to a Resume request

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 2
   * Request code = 40

   D0
   Bits 8-15 : Error Status
   Bit 0: 1 if data is available. 0 otherwise.

   Description
   Response to a Resume request. Indicates if data is
   available after resum or not.
*/
#define CY_RESP_SDIO_RESUME	 (40)

/******************************************************/

/*@@USB requests
   Summary
   The USB requests include:
	* CY_RQT_START_USB
	* CY_RQT_STOP_USB
	* CY_RQT_SET_CONNECT_STATE
	* CY_RQT_GET_CONNECT_STATE
	* CY_RQT_SET_USB_CONFIG
	* CY_RQT_GET_USB_CONFIG
	* CY_RQT_STALL_ENDPOINT
	* CY_RQT_GET_STALL
	* CY_RQT_SET_DESCRIPTOR
	* CY_RQT_GET_DESCRIPTOR
	* CY_RQT_SET_USB_CONFIG_REGISTERS
	* CY_RQT_USB_EVENT
	* CY_RQT_USB_EP_DATA
	* CY_RQT_ENDPOINT_SET_NAK
	* CY_RQT_GET_ENDPOINT_NAK
	* CY_RQT_ACK_SETUP_PACKET
	* CY_RQT_SCSI_INQUIRY_COMMAND
	* CY_RQT_SCSI_START_STOP_COMMAND
	* CY_RQT_SCSI_UNKNOWN_COMMAND
	* CY_RQT_USB_REMOTE_WAKEUP
	* CY_RQT_CLEAR_DESCRIPTORS
	* CY_RQT_USB_STORAGE_MONITOR
	* CY_RQT_USB_ACTIVITY_UPDATE
	* CY_RQT_MS_PARTITION_SELECT
 */
#ifndef __doxygen__
#define CY_RQT_USB_RQT_CONTEXT (3)
#endif

/* Summary
   This command requests initialization of the USB stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 0

   Description
   This command is required before any other USB related command can be
   sent to the West Bridge firmware.

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
   * CY_RESP_SUCCESS_FAILURE:CY_RESP_ALREADY_RUNNING
 */
#define CY_RQT_START_USB (0)


/* Summary
   This command requests shutdown of the USB stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 1

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_STOP_USB	 (1)


/* Summary
   This command requests that the USB pins be connected
   or disconnected to/from the West Bridge device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 2

   D0
   Desired Connect State
	* 0 = DISCONNECTED
	* 1 = CONNECTED

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_SET_CONNECT_STATE (2)


/* Summary
   This command requests the connection state of the
   West Bridge USB pins.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 3

   Responses
	* CY_RESP_CONNECT_STATE
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_GET_CONNECT_STATE (3)


/* Summary
   This request configures the USB subsystem.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
	* Context = 3
	* Request Code = 4

   D0
   Bits 8 - 15: Media to enumerate (bit mask)
   Bits 0 - 7: Enumerate Mass Storage (bit mask)
	* 1 = Enumerate device on bus 0
	* 2 = Enumerate device on bus 1

   D1
   Enumeration Methodology
	* 1 = West Bridge enumeration
	* 0 = P Port enumeration

   D2
   Mass storage interface number - Interface number to
   be used for the mass storage interface

   D3
   Mass storage callbacks
	* 1 = relay to P port
	* 0 = completely handle in firmware

   Description
   This indicates how enumeration should be handled.
   Enumeration can be handled by the West Bridge device
   or by the P port processor.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_INVALID_MASK
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_INVALID_STORAGE_MEDIA
 */
#define CY_RQT_SET_USB_CONFIG (4)


/* Summary
   This request retrieves the current USB configuration from
   the West Bridge device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 5

   Responses
	* CY_RESP_USB_CONFIG
 */
#define CY_RQT_GET_USB_CONFIG (5)


/* Summary
   This request stalls the given endpoint.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 6

   D0
   Endpoint Number

   D1
	* 1 = Stall Endpoint
	* 0 = Clear Stall

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_INVALID_ENDPOINT
 */
#define CY_RQT_STALL_ENDPOINT (6)


/* Summary
   This request retrieves the stall status of the
   requested endpoint.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 7

   D0
   Endpoint number

   Responses
	* CY_RESP_ENDPOINT_STALL
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_INVALID_ENDPOINT
 */
#define CY_RQT_GET_STALL (7)


/* Summary
   This command sets the contents of a descriptor.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 8

   D0
	Bit 15 - Bit 8
	Descriptor Index

	Bit 7 - Bit 0
	Descriptor Type
	* Device = 1
	* Device Qualifier = 2
	* Full Speed Configuration = 3
	* High Speed Configuration = 4

   * D1 - DN *
   Actual data for the descriptor

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_TYPE
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_INDEX
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_LENGTH
 */
#define CY_RQT_SET_DESCRIPTOR (8)

/* Summary
   This command gets the contents of a descriptor.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 9

   D0
	Bit 15 - Bit 8
	Descriptor Index

	Bit 7 - Bit 0
	Descriptor Type
	* Device = 1
	* Device Qualifier = 2
	* Full Speed Configuration = 3
	* High Speed Configuration = 4

   Responses
	* CY_RESP_USB_DESCRIPTOR
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_TYPE
	* CY_RESP_SUCCESS_FAILURE:CY_ERR_BAD_INDEX
 */
#define CY_RQT_GET_DESCRIPTOR (9)

/* Summary
   This request is sent from the P port processor to the
   West Bridge device to physically configure the endpoints
   in the device.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   3

   MailBox0
   * Context = 3
   * Request Code = 10

   D0
	Bit 15 - Bit 8
	  EP1OUTCFG register value
	Bit 7 - Bit 0
	  EP1INCFG register value

   * D1 - D2 *
   PEPxCFS register values where x = 3, 5, 7, 9

   * D3 - D7 *
   LEPxCFG register values where x = 3, 5, 7, 9, 10,
	11, 12, 13, 14, 15

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_SET_USB_CONFIG_REGISTERS	(10)

/* Summary
   This request is sent to the P port processor when a
   USB event occurs and needs to be relayed to the
   P port.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Request Code = 11

   D0
   Event Type
   * 0 = Reserved
   * 1 = Reserved
   * 2 = USB Suspend
   * 3 = USB Resume
   * 4 = USB Reset
   * 5 = USB Set Configuration
   * 6 = USB Speed change

   D1
   If EventTYpe is USB Speed change
   * 0 = Full Speed
   * 1 = High Speed

   If EventType is USB Set Configuration
   * The number of the configuration to use
   * (may be zero to unconfigure)

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_USB_EVENT (11)

/* Summary
   This request is sent in both directions to transfer
   endpoint data for endpoints 0 and 1.

   Direction
   West Bridge -> P Port Processor
   P Port Processor -> West Bridge

   Length (in transfers)
   Variable

   Mailbox0
   * Context = 3
   * Request Code = 12

   D0
   Bit 15 - 14 Data Type
   * 0 = Setup (payload should be the 8 byte setup packet)
   * 1 = Data
   * 2 = Status  (payload should be empty)

	Bit 13 Endpoint Number (only 0 and 1 supported)
	Bit 12 First Packet (only supported for Host ->
	West Bridge traffic)
	Bit 11 Last Packet (only supported for Host ->
	West Bridge traffic)

   Bit 9 - 0 Data Length (real max data length is 64 bytes
   for EP0 and EP1)

   *D1-Dn*
   Endpoint data
*/
#define CY_RQT_USB_EP_DATA (12)


/* Summary
   This request sets the NAK bit on an endpoint.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 13

   D0
   Endpoint Number

   D1
	* 1 = NAK Endpoint
	* 0 = Clear NAK

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_INVALID_ENDPOINT
 */
#define CY_RQT_ENDPOINT_SET_NAK	(13)


/* Summary
   This request retrieves the NAK config status of the
   requested endpoint.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Request Code = 14

   D0
   Endpoint number

   Responses
	* CY_RESP_ENDPOINT_NAK
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_INVALID_ENDPOINT
 */
#define CY_RQT_GET_ENDPOINT_NAK (14)

/* Summary
   This request acknowledges a setup packet that does not
   require any data transfer.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox
   * Context = 3
   * Request Code = 15

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_ACK_SETUP_PACKET (15)

/* Summary
   This request is sent when the USB storage driver within
   West Bridge receives an Inquiry request.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   x - variable

   Mailbox0
   * Context = 3
   * Request Code = 16

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Media type being addressed

   D1
   Bits 8	  : EVPD bit from request
   Bits 0 - 7  : Codepage from the inquiry request

   D2
   Length of the inquiry response in bytes

   * D3 - Dn *
   The inquiry response

   Description
   When the West Bridge firmware receives an SCSI Inquiry
   request from the USB host, the response to this mass
   storage command is created by West Bridge and forwarded to
   the P port processor.  The P port processor may change
   this response before it is returned to the USB host. This
   request is the method by which this may happen.
*/
#define CY_RQT_SCSI_INQUIRY_COMMAND (16)

/* Summary
   This request is sent when the USB storage driver within
   West Bridge receives a Start/Stop request.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Request Code = 17

   D0
   Bits 12 - 15 : Bus index
   Bits  8 - 11 : Zero based device index
   Bits  0 -  7 : Media type being addressed

   D1
   Bit 1
   * LoEj Bit (See SCSI-3 specification)

   Bit 0
   * Start Bit (See SCSI-3 specification)

   Description
   When the West Bridge firmware received a SCSI Start/Stop
   request from the USB host, this request is relayed to the
   P port processor. This request is used to relay the command.
   The USB firmware will not response to the USB command until
   the response to this request is recevied by the firmware.
*/
#define CY_RQT_SCSI_START_STOP_COMMAND	(17)

/* Summary
   This request is sent when the USB storage driver
   receives an unknown CBW on mass storage.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   4

   Mailbox0
   * Context = 3
   * Request Code = 18

   D0
   Bits 12 - 15 : MediaType
	* 0 = NAND
	* 1 = SDIO Flash
	* 2 = MMC Flash
	* 3 = CE-ATA

   D1
   The length of the request in bytes

   D2 - Dn
   CBW command block from the SCSI host controller.

   Description
   When the firmware recevies a SCSI request that is not
   understood, this request is relayed to the
   P port processor.
*/
#define CY_RQT_SCSI_UNKNOWN_COMMAND	(18)

/* Summary
   Request the West Bridge to signal remote wakeup
   to the USB host.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Request code = 19

   Description
   Request from the processor to West Bridge, to signal
   remote wakeup to the USB host.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_USB_REMOTE_WAKEUP	 (19)

/* Summary
   Request the West Bridge to clear all descriptors tha
    were set previously
   using the Set Descriptor calls.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Request code = 20

   Description
   Request from the processor to West Bridge, to clear
   all descriptor information that was previously stored
   on the West Bridge using CyAnUsbSetDescriptor calls.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_CLEAR_DESCRIPTORS (20)

/* Summary
   Request the West Bridge to monitor USB to storage activity
   and send periodic updates.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   Mailbox0
   * Context = 3
   * Request code = 21

   D0
   Upper 16 bits of write threshold

   D1
   Lower 16 bits of write threshold

   D2
   Upper 16 bits of read threshold

   D3
   Lower 16 bits of read threshold

   Description
   Request from the processor to West Bridge, to start
   monitoring the level of read/write activity on the
   USB mass storage drive and to set the threshold
   level at which progress reports are sent.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_USB_STORAGE_MONITOR	(21)

/* Summary
   Event from the West Bridge showing that U2S activity
   since the last event has crossed the threshold.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 3
   * Request code = 22

   D0
   Upper 16 bits of sectors written since last event.

   D1
   Lower 16 bits of sectors written since last event.

   D2
   Upper 16 bits of sectors read since last event.

   D3
   Lower 16 bits of sectors read since last event.

   Description
   Event notification from the West Bridge indicating
   that the number of read/writes on the USB mass
   storage device have crossed a pre-defined threshold
   level.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_USB_ACTIVITY_UPDATE	(22)

/* Summary
   Request to select the partitions to be enumerated on a
   storage device with partitions.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Request code = 23

   D0
   Bits 8-15 : Bus index
   Bits 0- 7 : Device index

   D1
   Bits 8-15 : Control whether to enumerate partition 1.
   Bits 0- 7 : Control whether to enumerate partition 0.

   Responses
   * CY_RESP_SUCCESS_FAILURE
 */
#define CY_RQT_MS_PARTITION_SELECT	(23)

/************/

/*@@USB responses
   Summary
   The USB responses include:
   * CY_RESP_USB_CONFIG
   * CY_RESP_ENDPOINT_CONFIG
   * CY_RESP_ENDPOINT_STALL
   * CY_RESP_CONNECT_STATE
   * CY_RESP_USB_DESCRIPTOR
   * CY_RESP_USB_INVALID_EVENT
   * CY_RESP_ENDPOINT_NAK
   * CY_RESP_INQUIRY_DATA
   * CY_RESP_UNKNOWN_SCSI_COMMAND
 */

/* Summary
   This response contains the enumeration configuration
   information for the USB module.

   Direction
   8051->P

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Response Code = 32

   D0
   Bits 8 - 15: Media to enumerate (bit mask)
   Bits 0 -  7: Buses to enumerate (bit mask)
	* 1 = Bus 0
	* 2 = Bus 1

   D1
   Enumeration Methodology
	* 0 = West Bridge enumeration
	* 1 = P Port enumeration

   D2
   Bits 7 - 0  : Interface Count - the number of interfaces
   Bits 15 - 8 : Mass storage callbacks

 */
#define CY_RESP_USB_CONFIG (32)


/* Summary
   This response contains the configuration information
   for the specified endpoint.

   Direction
   8051->P

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Response Code = 33

   D0
   Bits 15 - 12 : Endpoint Number (0 - 15)

   Bits 11 - 10 : Endpoint Type
	* 0 = Control
	* 1 = Bulk
	* 2 = Interrupt
	* 3 = Isochronous

   Bits 9 : Endpoint Size
	* 0 = 512
	* 1 = 1024

   Bits 8 - 7 : Buffering
	* 0 = Double
	* 1 = Triple
	* 2 = Quad

   Bits 6 : Bit Direction
	* 0 = Input
	* 1 = Output
 */
#define CY_RESP_ENDPOINT_CONFIG (33)


/* Summary
   This response contains the stall status for
   the specified endpoint.

   Direction
   8051->P

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Response Code = 34

   D0
   Stall status
	* 0 = Active
	* 1 = Stalled
 */
#define CY_RESP_ENDPOINT_STALL (34)


/* Summary
   This response contains the connected/disconnected
   state of the West Bridge USB pins.

   Direction
   8051->P

   Length (in transfers)
   1

   MailBox0
	* Context = 3
	* Response Code = 35

   D0
   Connect state
	* 0 = Disconnected
	* 1 = Connected
 */
#define CY_RESP_CONNECT_STATE (35)

/* Summary
   This response contains the information
   about the USB configuration

   Direction
   West Bridge -> P Port Processor

   Length
   x bytes

   Mailbox0
   * Context = 3
   * Response Code = 36

   D0
   Length in bytes of the descriptor

   * D1 - DN *
   Descriptor contents
*/
#define CY_RESP_USB_DESCRIPTOR (36)

/* Summary
   This response is sent in response to a bad USB event code

   Direction
   P Port Processor -> West Bridge

   Length
   1 word (2 bytes)

   Mailbox0
   * Context = 3
   * Response Code = 37

   D0
   The invalid event code in the request
*/
#define CY_RESP_USB_INVALID_EVENT	(37)

/* Summary
   This response contains the current NAK status of
   a USB endpoint.

   Direction
   West Bridge -> P port processor

   Length
   1 transfer

   Mailbox0
   * Context = 3
   * Response Code = 38

   D0
   The NAK status of the endpoint
   1 : NAK bit set
   0 : NAK bit clear
*/
#define CY_RESP_ENDPOINT_NAK (38)

/* Summary
   This response gives the contents of the inquiry
   data back to West Bridge to returns to the USB host.

   Direction
   West Bridge -> P Port Processor

   Length
   Variable

   MailBox0
   * Context = 3
   * Response Code = 39

   D0
   Length of the inquiry response

   *D1 - Dn*
   Inquiry data
*/
#define CY_RESP_INQUIRY_DATA (39)

/* Summary
   This response gives the status of an unknown SCSI command.
   This also gives three bytes of sense information.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 3
   * Response Code = 40

   D0
   The length of the reply in bytes

   D1
   * Status of the command
   * Sense Key

   D2
   * Additional Sense Code (ASC)
   * Additional Sense Code Qualifier (ASCQ)
*/
#define CY_RESP_UNKNOWN_SCSI_COMMAND (40)
/*******************************************************/

/*@@Turbo requests
   Summary
   The Turbo requests include:
	* CY_RQT_START_MTP
	* CY_RQT_STOP_MTP
	* CY_RQT_INIT_SEND_OBJECT
	* CY_RQT_CANCEL_SEND_OBJECT
	* CY_RQT_INIT_GET_OBJECT
	* CY_RQT_CANCEL_GET_OBJECT
	* CY_RQT_SEND_BLOCK_TABLE
	* CY_RQT_MTP_EVENT
	* CY_RQT_TURBO_CMD_FROM_HOST
	* CY_RQT_TURBO_SEND_RESP_DATA_TO_HOST
	* CY_RQT_TURBO_SWITCH_ENDPOINT
	* CY_RQT_TURBO_START_WRITE_DMA
	* CY_RQT_ENABLE_USB_PATH
	* CY_RQT_CANCEL_ASYNC_TRANSFER
 */
#ifndef __doxygen__
#define CY_RQT_TUR_RQT_CONTEXT (4)
#endif

/* Summary
   This command requests initialization of the MTP stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 0

   Description
   This command is required before any other MTP related
   command can be sent to the West Bridge firmware.

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
   * CY_RESP_SUCCESS_FAILURE:CY_RESP_ALREADY_RUNNING
 */
#define CY_RQT_START_MTP (0)

/* Summary
   This command requests shutdown of the MTP stack.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 1

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_STOP_MTP (1)

/* Summary
   This command sets up an MTP SendObject operation.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 2

   D0
   Total bytes for send object Low 16 bits

   D1
   Total bytes for send object High 16 bits

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_INIT_SEND_OBJECT (2)

/* Summary
   This command cancels West Bridges handling of
   an ongoing MTP SendObject operation. This
   does NOT send an MTP response.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 3

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_NO_OPERATION_PENDING
 */
#define CY_RQT_CANCEL_SEND_OBJECT (3)

/* Summary
   This command sets up an MTP GetObject operation.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
	* Context = 4
	* Request Code = 4

   D0
   Total bytes for get object Low 16 bits

   D1
   Total bytes for get object High 16 bits

   D2
   Transaction Id for get object Low 16 bits

   D3
   Transaction Id for get object High 16 bits

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_INIT_GET_OBJECT (4)

/* Summary
   This command notifies West Bridge of a new
   BlockTable transfer.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 5

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_SEND_BLOCK_TABLE (5)

/* Summary
   This request is sent to the P port processor when a MTP event occurs
   and needs to be relayed to the P port.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   2

   Mailbox0
   * Context = 4
   * Request Code = 6

   D0
   Bits 15 - 8 : Return Status for GetObject/SendObject
   Bits 7 - 0  : Event Type
   * 0 = MTP SendObject Complete
   * 1 = MTP GetObject Complete
   * 2 = BlockTable Needed

   D1
   Lower 16 bits of the length of the data that got transferred
   in the Turbo Endpoint.(Applicable to "MTP SendObject Complete"
   and "MTP GetObject Complete" events)

   D2
   Upper 16 bits of the length of the data that got transferred
   in the Turbo Endpoint. (Applicable to "MTP SendObject Complete"
   and "MTP GetObject Complete" events)

   D3
   Lower 16 bits of the Transaction Id of the MTP_SEND_OBJECT
   command. (Applicable to "MTP SendObject Complete" event)

   D4
   Upper 16 bits of the Transaction Id of the MTP_SEND_OBJECT
   command. (Applicable to "MTP SendObject Complete" event)

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_MTP_EVENT (6)

/* Summary
   This request is sent to the P port processor when a command
   is received from Host in a Turbo Endpoint. Upon receiving
   this event, P port should read the data from the endpoint as
   soon as possible.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   Mailbox0
   * Context = 4
   * Request Code = 7

   D0
   This contains the EP number. (This will be always two now).

   D1
   Length of the data available in the Turbo Endpoint.

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_TURBO_CMD_FROM_HOST  (7)

/* Summary
   This request is sent to the West Bridge when the P port
   needs to send data to the Host in a Turbo Endpoint.
   Upon receiving this event, Firmware will make the end point
   available for the P port. If the length is zero, then
   firmware will send a zero length packet.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   Mailbox0
   * Context = 4
   * Request Code = 8

   D0
   This contains the EP number. (This will be always six now).

   D1
	Lower 16 bits of the length of the data that needs to be
	sent in the Turbo Endpoint.

   D2
	Upper 16 bits of the length of the data that needs to be
	sent in the Turbo Endpoint.

   Responses
   * CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
*/
#define CY_RQT_TURBO_SEND_RESP_DATA_TO_HOST	(8)

/* Summary
   This command cancels West Bridges handling of
   an ongoing MTP GetObject operation. This
   does NOT send an MTP response.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 9

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_NO_OPERATION_PENDING
 */
#define CY_RQT_CANCEL_GET_OBJECT (9)

/* Summary
   This command switches a Turbo endpoint
   from the U port to the P port. If no data
   is in the endpoint the endpoint is
   primed to switch as soon as data is placed
   in the endpoint. The endpoint will continue
   to switch until all data has been transferd.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   2

   MailBox0
	* Context = 4
	* Request Code = 10

   D0
   Whether the read is a packet read.

   D1
	Lower 16 bits of the length of the data to switch
	the Turbo Endpoint for.

   D2
	Upper 16 bits of the length of the data to switch
	the Turbo Endpoint for.

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
	* CY_RESP_SUCCESS_FAILURE:CY_RESP_NOT_RUNNING
 */
#define CY_RQT_TURBO_SWITCH_ENDPOINT (10)

/* Summary
   This command requests the API to start the DMA
   transfer of a packet of MTP data to the Antioch.

   Direction
   West Bridge -> P Port Processor

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request Code = 11

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
 */
#define CY_RQT_TURBO_START_WRITE_DMA (11)

/* Summary
   This command requests the firmware to switch the
   internal data paths to enable USB access to the
   Mass storage / MTP endpoints.

   Direction
   P Port Processor -> West Bridge

   Length (in transfers)
   1

   MailBox0
	* Context = 4
	* Request code = 12

   Responses
	* CY_RESP_SUCCESS_FAILURE:CY_AS_ERROR_SUCCESS
 */
#define CY_RQT_ENABLE_USB_PATH	(12)

/* Summary
   Request to cancel an asynchronous MTP write from
   the processor side.

   Direction
   P Port processor -> West Bridge

   Length (in transfers)
   1

   Mailbox0
   * Context = 4
   * Request code = 13

   D0
   * EP number

   Description
   This is a request to the firmware to update internal
   state so that a pending write on the MTP endpoint
   can be cancelled.
 */
#define CY_RQT_CANCEL_ASYNC_TRANSFER (13)

/******************************************************/

/*@@Turbo responses
   Summary
   The Turbo responses include:
   * CY_RESP_MTP_INVALID_EVENT
 */

/* Summary
   This response is sent in response to a bad MTP event code

   Direction
   P Port Processor -> West Bridge

   Length
   1 word (2 bytes)

   Mailbox0
   * Context = 4
   * Response Code = 16

   D0
   The invalid event code in the request
*/
#define CY_RESP_MTP_INVALID_EVENT (16)

#ifndef __doxygen__
#define CY_RQT_CONTEXT_COUNT (5)
#endif

#endif			/* _INCLUDED_CYASPROTOCOL_H_ */

