/* Cypress West Bridge API header file (cyasstorage.h)
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

#ifndef _INCLUDED_CYASSTORAGE_H_
#define _INCLUDED_CYASSTORAGE_H_

#include "cyasmedia.h"
#include "cyasmisc.h"
#include "cyas_cplus_start.h"


/*@@Storage APIs
  Summary
  This section documents the storage APIs supported by the
  West Bridge API.

  Description
  The storage API is based on some specific concepts which
  are referenced here.
  * <LINK Storage API Overview>
  * Addressing
  * Ownership
  * <LINK Asynchronous Versus Synchronous Operation>
*/

/*@@Storage API Overview
  Summary
  Storage devices are identified by media type. Each media
  type is considered a  single logical device.

  Description
  Each media type has a consistent block size and consists
  of a set of logical blocks numbered from 0 to N - 1 where
  N is the size of the
  media type in blocks. The mass storage APIs defined below
  provide the
  capability to query for devices that are present, and
  read/write data to/from
  these devices.
*/

/*@@Addressing
  Summary
  Blocks within a storage device are address by a hierarchal
  block address.  This
  address consists of the bus number, physical device,
   logical unit, and finally
  block address.

  Description
  While currently only a single device of each media type
  is supported, the address
  space reserves space in the future for multiple devices
  of each type.  Therefore
  the second element of the address is the specific device
  being addressed within
  a given device type.  For this release of the software,
  this value will always be
  zero to address the first device.

  The third element of the address is the logical unit.
  A device being managed
  by West Bridge can be partitioned into multiple logical
  units.  This partition
  information is stored on each device itself.  Currently,
  one of the storage devices
  managed by West Bridge can be partitioned into two
  logical units.

  Finally a logical block address is given within the
  logical unit to address an
  individual block.
*/

/*@@Ownership
  Summary
  While West Bridge supports concurrent block level
  operations from both the USB port and
  the processor port, this is not desirable in most
  situations as the file system
  contained on the storage media cannot be accessed
  concurrently.  To insure access
  by only one of USB and the processor, the West Bridge
  API provides for ownership of storage
  devices based on media type.

  Description
  The processor requests ownership of a given media type
  by calling CyAsStorageClaim().
  The firmware in West Bridge releases control of the
  media and signals the processor through
  the event callback registered with
  CyAsStorageRegisterCallback().  The specific event is
  the CyAsStorageProcessor. The processor can later
  release the media via a call to
  CyAsStorageRelease().  This call is immediate and
  no callback is required.

  If the processor has claimed storage and the USB port
  is connected, West Bridge will need to
  claim the storage to manage the mass storage device.
  West Bridge requests the storage through
  the event callback registered with
  CyAsStorageRegisterCallback().  The specific event is
  CyAsStorageAntioch and is named as such to reflect
  the USB view of storage.  This callback
  is a request for the processor to release storage.
  The storage is not actually released
  until the processor calls CyAsStorageRelease().

  Note that the CyAsStorageAntioch is only sent when the
  USB storage device is enumerated and
  NOT at every USB operation.  The ownership of a given
  storage media type is assumed to belong
  to the processor until the USB connection is established.
  At that point, the storage ownership
  is transferred to West Bridge.  After the USB connection
  is broken, ownership can be transferred
  back to the processor.
*/

/*@@Asynchronous Versus Synchronous Operation
  Summary
  When read or write operations are performed to the
  storage devices, these operations may be
  synchronous or asynchronous.  A synchronous operation
  is an operation where the read or write
  operation is requested and the function does not return
  until the operation is complete.  This
  type of function is the easiest to use but does not
  provide for optimal usage of the P port processor time.

  Description
  An asynchronous operation is one where the function returns
  as soon as the request is started.
  The specific read and write request will complete at some
  time in the future and the P port
  processor will be notified via a callback function.  While
  asynchronous functions provide for
  much better usage of the CPU, these function have more
  stringent requirements for use.  First,
  any buffer use for data transfer must be valid from the
  function call to request the operation
  through when the callback function is called.  This basically
  implies that stack based buffers
  are not acceptable for asynchronous calls.  Second, error
  handling must be deferred until the
  callback function is called indicating any kind of error
  that may have occurred.
*/

/*@@Partitioning
  Summary
  West Bridge API and firmware support the creation of up to
  two logical partitions on one
  of the storage devices that are managed by West Bridge.  The
  partitions are managed through
  the CyAsStorageCreatePPartition and CyAsStorageRemovePPartition
  APIs.

  Description
  The CyAsStorageCreatePPartition API is used to divide the total
  storage on a storage
  device into two logical units or partitions.  Since the partition
  information is stored
  on the storage device in a custom format, partitions should
  only be created on fixed
  storage devices (i.e., no removable SD/MMC cards).  Any data
  stored on the device
  before the creation of the partition, is liable to be lost when
  a partition is created.

  The CyAsStorageRemovePPartition API is used to remove the
  stored partition information,
  so that all of the device's capacity is treated as a single
  partition again.

  When a storage device with two partitions (units) is being
  enumerated as a mass storage
  device through the West Bridge, it is possible to select the
  partitions to be made
  visible to the USB host.  This is done through the
  CyAsUsbSelectMSPartitions API.
*/

/*********************************
 * West Bridge Constants
 **********************************/

/* Summary
   This constants indicates a raw device access to the read/write
   functions

   Description
   When performing reading and writing operations on the
   storage devices attached
   to West Bridge, there are cases where writes need to
   happen to raw devices, versus
   the units contained within a device.  This is
   specifically required to manage
   the partitions within physical devices.  This constant
   is used in calls to
   CyAsStorageRead(), CyAsStorageReadAsync(),
   CyAsStorageWrite() and
   CyAsStorageWriteAsync(), to indicate that the raw
   physical device is being
   accessed and not any specific unit on the device.

   See Also
   * CyAsStorageRead
   * CyAsStorageReadAsync
   * CyAsStorageWrite
   * CyAsStorageWriteAsync
*/
#define CY_AS_LUN_PHYSICAL_DEVICE (0xffffffff)

/* Summary
   This constant represents the maximum DMA burst length
   supported on a storage endpoint

   Description
   West Bridge reserves separate endpoints for accessing
   storage media through the
   CyAsStorageRead() and CyAsStorageWrite() calls. The
   maximum size of these
   endpoints is always 512 bytes, regardless of status
   and speed of the USB
   connection.
*/
#define CY_AS_STORAGE_EP_SIZE (512)

/********************************
 * West Bridge Types
 *******************************/

/* Summary
   This type indicates the type of event in an event
   callback from West Bridge

   Description
   At times West Bridge needs to inform the P port
   processor of events that have
   occurred.  These events are asynchronous to the
   thread of control on the P
   port processor and as such are generally delivered
   via a callback function that
   is called as part of an interrupt handler.  This
   type indicates the resonse for
   the call to the callback function.

   See Also
   * CyAsStorageEventCallback
   * CyAsStorageRegisterCallback
*/
typedef enum cy_as_storage_event {
	/*  This event occurs when the West Bridge device has
	detected a USB connect and has enumerated the
	storage controlled by west bridge to the USB port.
	this event is the signal that the processor
	needs to release the storage media. west bridge will
	not have control of the storage media until the
	processor calls cy_as_release_storage() to release
	the specific media. */
	cy_as_storage_antioch,

	/*  This event occurs when the processor has requested
	ownership of a given media type and west bridge has
	released the media.  this event is an indicator
	that the transfer of ownership is complete and the
	processor now owns the given media type. */
	cy_as_storage_processor,

	/*  This event occurs when a removable media type has
	been removed. */
	cy_as_storage_removed,

	/*  This event occurs when a removable media type has
		been inserted. */
	cy_as_storage_inserted,

	/* This event occurs when the West Bridge device
	 * percieves an interrrupt from an SDIO card */
	cy_as_sdio_interrupt

} cy_as_storage_event;

/* Summary
   This type gives the type of the operation in a storage
   operation callback

   Description
   This type is used in the callback function for asynchronous
   operation.  This type indicates whether it is a
   CyAsStorageRead() or CyAsStorageWrite() operation that
   has completed.

   See Also
   * <LINK Asynchronous Versus Synchronous Operation>
   * CyAsStorageRead
   * CyAsStorageWrite
*/
typedef enum cy_as_oper_type {
	/* A data read operation */
	cy_as_op_read,
	/* A data write operation */
	cy_as_op_write
} cy_as_oper_type;

/* Summary
   This data structure describes a specific type of media

   Description
   This data structure is the return value from the
   CyAsStorageQueryDevice function.  This structure provides
   information about the specific storage device being queried.

   See Also
   * CyAsStorageQueryDevice
*/
typedef struct cy_as_device_desc {
	/* Type of device */
	cy_as_media_type   type;
	/* Is the device removable */
	cy_bool		removable;
	/* Is the device writeable */
	cy_bool		writeable;
	/* Basic block size for device */
	uint16_t		block_size;
	/* Number of LUNs on the device */
	uint32_t		number_units;
	/* Is the device password locked */
	cy_bool		locked;
	 /* Size in bytes of an Erase Unit. Block erase operation
	is only supported for SD storage, and the erase_unit_size
	is invalid for all other kinds of storage. */
	uint32_t		erase_unit_size;
} cy_as_device_desc;

/* Summary
   This data structure describes a specific unit on a
   specific type of media

   Description
   This data structure is the return value from the
   CyAsStorageQueryUnit function. This structure provides
   information about the specific unit.

   See Also
   * CyAsStorageQueryUnit
*/
typedef struct cy_as_unit_desc {
	/* Type of device */
	cy_as_media_type type;
	/* Basic block size for device */
	uint16_t block_size;
	/* Physical start block for LUN */
	uint32_t start_block;
	/* Number of blocks in the LUN */
	uint32_t unit_size;
} cy_as_unit_desc;

/* Summary
   This function type defines a callback to be called after an
   asynchronous operation

   Description
   This function type defines a callback function that is called
   at the completion of any asynchronous read or write operation.

   See Also
   * CyAsStorageReadAsync()
   * CyAsStorageWriteAsync()
*/
typedef void (*cy_as_storage_callback)(
	/* Handle to the device completing the storage operation */
	cy_as_device_handle handle,
	/* The bus completing the operation */
	cy_as_bus_number_t bus,
	/* The device completing the operation */
	uint32_t device,
	/* The unit completing the operation */
	uint32_t unit,
	/* The block number of the completed operation */
	uint32_t block_number,
	/* The type of operation */
	cy_as_oper_type op,
	/* The error status */
	cy_as_return_status_t status
	);

/* Summary
   This function type defines a callback to be called in the
   event of a storage related event

   Description
   At times West Bridge needs to inform the P port processor
   of events that have
   occurred.  These events are asynchronous to the thread of
   control on the P
   port processor and as such are generally delivered via a
   callback function that
   is called as part of an interrupt handler.  This type
   defines the type of function
   that must be provided as a callback function.

   See Also
   * CyAsStorageEvent
   * CyAsStorageRegisterCallback
*/
typedef void (*cy_as_storage_event_callback)(
	/* Handle to the device sending the event notification */
	cy_as_device_handle handle,
	/* The bus where the event happened */
	cy_as_bus_number_t  bus,
	/* The device where the event happened */
	uint32_t device,
	/* The event type */
	cy_as_storage_event evtype,
	/* Event related data */
	void *evdata
	);

/* Summary
   This function type defines a callback to be called after
   an asynchronous sdio operation

   Description
   The Callback function is called at the completion of an
   asynchronous sdio read or write operation.

   See Also
   * CyAsSdioExtendedRead()
   * CyAsSdioExtendedWrite()
*/
typedef void (*cy_as_sdio_callback)(
	/* Handle to the device completing the storage operation */
	cy_as_device_handle handle,
	/* The bus completing the operation */
	cy_as_bus_number_t bus,
	/* The device completing the operation */
	uint32_t device,
	/* The function number of the completing the operation.
	if the status of the operation is either CY_AS_ERROR_IO_ABORTED
	or CY_AS_IO_SUSPENDED then the most significant word parameter will
	contain the number of blocks still pending. */
	uint32_t function,
	/* The base address of the completed operation */
	uint32_t address,
	/* The type of operation */
	cy_as_oper_type op,
	/* The status of the operation */
	cy_as_return_status_t status
	);

/* Summary
   Enumeration of SD/MMC card registers that can be read
   through the API.

   Description
   Some of the registers on the SD/MMC card(s) attached to the
   West Bridge can be read through the API layers. This type
   enumerates the registers that can be read.

   See Also
   * CyAsStorageSDRegisterRead
 */
typedef enum cy_as_sd_card_reg_type {
	cy_as_sd_reg_OCR = 0,
	cy_as_sd_reg_CID,
	cy_as_sd_reg_CSD
} cy_as_sd_card_reg_type;

/* Summary
   Struct encapsulating parameters and return values for a
   CyAsStorageQueryDevice call.

   Description
   This struct holds the input parameters and the return values
   for an asynchronous CyAsStorageQueryDevice call.

   See Also
   * CyAsStorageQueryDevice
 */
typedef struct cy_as_storage_query_device_data {
	/* The bus with the device to query */
	cy_as_bus_number_t	bus;
	/* The logical device number to query */
	uint32_t		device;
	/* The return value for the device descriptor */
	cy_as_device_desc	 desc_p;
} cy_as_storage_query_device_data;


/* Summary
   Struct encapsulating parameters and return values
   for a CyAsStorageQueryUnit call.

   Description
   This struct holds the input parameters and the return
   values for an asynchronous CyAsStorageQueryUnit call.

   See Also
   * CyAsStorageQueryUnit
 */
typedef struct cy_as_storage_query_unit_data {
	/* The bus with the device to query */
	cy_as_bus_number_t	bus;
	/* The logical device number to query */
	uint32_t			device;
	/* The unit to query on the device */
	uint32_t			unit;
	/* The return value for the unit descriptor */
	cy_as_unit_desc	 desc_p;
} cy_as_storage_query_unit_data;

/* Summary
   Struct encapsulating the input parameter and return
   values for a CyAsStorageSDRegisterRead call.

   Description
   This struct holds the input parameter and return
   values for an asynchronous CyAsStorageSDRegisterRead
   call.

   See Also
   * CyAsStorageSDRegisterRead
 */
typedef struct cy_as_storage_sd_reg_read_data {
	/* Pointer to the result buffer. */
	uint8_t *buf_p;
	/* Length of data to be copied in bytes. */
	uint8_t  length;
} cy_as_storage_sd_reg_read_data;

/* Summary
   Controls which pins are used for card detection

   Description
   When a StorageDeviceControl call is made to enable or
   disable card detection this enum is passed in to
   control which pin is used for the detection.

   See Also
   * CyAsStorageDeviceControl
*/
typedef enum cy_as_storage_card_detect {
	cy_as_storage_detect_GPIO,
	cy_as_storage_detect_SDAT_3
} cy_as_storage_card_detect;

#ifndef __doxygen__
#define cy_as_storage_detect_GPIO_0 cy_as_storage_detect_GPIO

/* Length of OCR value in bytes. */
#define CY_AS_SD_REG_OCR_LENGTH		 (4)
/* Length of CID value in bytes. */
#define CY_AS_SD_REG_CID_LENGTH		 (16)
/* Length of CSD value in bytes. */
#define CY_AS_SD_REG_CSD_LENGTH		 (16)
/* Max. length of register response in words. */
#define CY_AS_SD_REG_MAX_RESP_LENGTH	(10)

#endif

/* Summary
   This data structure is the data passed via the evdata
   paramater on a usb event callback for the mass storage
   device progress event.

   Description
   This data structure reports the number of sectors that have
   been written and read on the USB mass storage device since
   the last event report.  The corresponding event is only sent
   when either the number of writes, or the number of reads has
   crossed a pre-set threshold.

   See Also
   * CyAsUsbEventCallback
   * CyAsUsbRegisterCallback
*/
typedef struct cy_as_m_s_c_progress_data {
	/* Number of sectors written since the last event. */
	uint32_t wr_count;
	/* Number of sectors read since the last event. */
	uint32_t rd_count;
} cy_as_m_s_c_progress_data;

/* Summary
Flag to set Direct Write operation to read back from the
address written to.


 See Also
 *CyAsSdioDirectWrite()
*/
#define CY_SDIO_RAW	(0x01)


/* Summary
Flag to set Extended Read and Write to perform IO
using a FIFO i.e. read or write from the specified
address only.

 See Also
 *CyAsSdioExtendedRead()
 *CyAsSdioExtendedWrite()
*/
#define CY_SDIO_OP_FIFO (0x00)

/* Summary
Flag to set Extended Read and Write to perform incremental
IO using the address provided as the base address.


 See Also
 *CyAsSdioExtendedRead()
 *CyAsSdioExtendedWrite()
*/
#define CY_SDIO_OP_INCR		 (0x02)

/* Summary
Flag to set Extended Read and Write to Block Mode operation

 See Also
 *CyAsSdioExtendedRead()
 *CyAsSdioExtendedWrite()
*/
#define CY_SDIO_BLOCKMODE	   (0x04)

/* Summary
Flag to set Extended Read and Write to Byte Mode operation

 See Also
 *CyAsSdioExtendedRead()
 *CyAsSdioExtendedWrite()
*/
#define CY_SDIO_BYTEMODE		(0x00)

/* Summary
Flag to force re/initialization of a function.

Description
If not set a call to CyAsSdioInitFunction()
will not initialize a function that has been previously
initialized.
 See Also
 *CyAsSdioInitFunction()
 */
#define CY_SDIO_FORCE_INIT	  (0x40)

/* Summary
Flag to re-enable the SDIO interrupts.

Description
Used with a direct read or direct write
after the Interrupt triggerred by SDIO has been serviced
and cleared to reset the West Bridge Sdio Interrupt.
 See Also
 *CyAsSdioDirectRead()
 *CyAsSdioDirectWrite()
*/

#define CY_SDIO_REARM_INT	   (0x80)


/* Summary
   Flag to check if 4 bit support is enabled on a
   low speed card
   See Also
   <link CyAsSDIOCard::card_capability>*/
#define CY_SDIO_4BLS	(0x80)

/* Summary
   Flag to check if card is a low speed card
   See Also
   <link CyAsSDIOCard::card_capability>	  */
#define CY_SDIO_LSC	 (0x40)

/* Summary
   Flag to check if interrupt during multiblock data
   transfer is enabled
   See Also
   <link CyAsSDIOCard::card_capability>*/
#define CY_SDIO_E4MI	(0x20)

/* Summary
   Flag to check if interrupt during multiblock data
   transfer is supported
   See Also
   <link CyAsSDIOCard::card_capability>	*/
#define CY_SDIO_S4MI	(0x10)

/* Summary
   Flag to check if card supports function suspending.
   See Also
   <link CyAsSDIOCard::card_capability>	 */
#define CY_SDIO_SBS	 (0x08)

/* Summary
   Flag to check if card supports SDIO Read-Wait
   See Also
   <link CyAsSDIOCard::card_capability>	 */
#define CY_SDIO_SRW	 (0x04)

/* Summary
   Flag to check if card supports multi-block transfers
   See Also
   <link CyAsSDIOCard::card_capability>	*/
#define CY_SDIO_SMB	 (0x02)

/* Summary
   Flag to check if card supports Direct IO commands
   during execution of an Extended
   IO function
   See Also
   <link CyAsSDIOCard::card_capability>*/
#define CY_SDIO_SDC	 (0x01)

/* Summary
   Flag to check if function has a CSA area.
   See Also
   <link CyAsSDIOFunc::csa_bits> */
#define CY_SDIO_CSA_SUP		 (0x40)

/* Summary
   Flag to check if CSA access is enabled.
   See Also
   <link CyAsSDIOFunc::csa_bits> */
#define CY_SDIO_CSA_EN		  (0x80)

/* Summary
   Flag to check if CSA is Write protected.
   See Also
   <link CyAsSDIOFunc::csa_bits> */
#define CY_SDIO_CSA_WP		  (0x01)

/* Summary
   Flag to check if CSA formatting is prohibited.
   See Also
   <link CyAsSDIOFunc::csa_bits>*/
#define CY_SDIO_CSA_NF		  (0x02)

/* Summary
   Flag to check if the function allows wake-up from low
   power mode using some vendor specific method.
   See Also
   <link CyAsSDIOFunc::wakeup_support>*/
#define CY_SDIO_FN_WUS		  (0x01)


/* Summary
   This data structure stores SDIO function 0
   parameters for a SDIO card
*/
typedef struct cy_as_sdio_card {
	/* Number of functions present on the card. */
	uint8_t	 num_functions;
	/* Memory present(Combo card) or not */
	uint8_t	 memory_present;
	/* 16 bit manufacturer ID */
	uint16_t	manufacturer__id;
	/* Additional vendor specific info */
	uint16_t	manufacturer_info;
	/* Max Block size for function 0 */
	uint16_t	maxblocksize;
	/* Block size used for function 0 */
	uint16_t	blocksize;
	/* SDIO version supported by the card */
	uint8_t	 sdio_version;
	/* Card capability flags */
	uint8_t	 card_capability;
} cy_as_sdio_card;

/* Summary
   This data structure stores SDIO function 1-7 parameters
   for a SDIO card
*/
typedef struct cy_as_sdio_func {
	/* SDIO function code. 0 if non standard function */
	uint8_t	 function_code;
	/* Extended function type code for non-standard function */
	uint8_t	 extended_func_code;
	/* Max IO Blocksize supported by the function */
	uint16_t	maxblocksize;
	/* IO Blocksize used by the function */
	uint16_t	blocksize;
	/* 32 bit product serial number for the function */
	uint32_t	card_psn;
	/* Code storage area variables */
	uint8_t	 csa_bits;
	/* Function wake-up support */
	uint8_t	 wakeup_support;
} cy_as_sdio_func;

/***********************************
 * West Bridge Functions
 ************************************/

/* Summary
   This function starts the West Bridge storage module.

   Description
   This function initializes the West Bridge storage software
   stack and readies this module to service storage related
   requests.  If the stack is already running, the reference
   count for the stack is incremented.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_SUCCESS - the module started sucessfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsStorageStop
*/
EXTERN cy_as_return_status_t
cy_as_storage_start(
	/* Handle to the device */
	cy_as_device_handle	handle,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t		client
	);

/* Summary
   This function stops the West Bridge storage module.

   Description
   This function decrements the reference count for the
   storage stack and if this count is zero, the storage
   stack is shut down.  The shutdown frees all resources
   associated with the storage stack.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   While all resources associated with the storage stack
   will be freed is a shutdown occurs,
   resources associated with underlying layers of the
   software will not be freed if they
   are shared by the USB stack and the USB stack is
   active.  Specifically the DMA manager,
   the interrupt manager, and the West Bridge
   communications module are all shared by both the
   USB stack and the storage stack.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge
   *	device has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not
   *	been loaded into West Bridge
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - this module was shut
   *	down sucessfully
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING
   * CY_AS_ERROR_ASYNC_PENDING
   * CY_AS_ERROR_OUT_OF_MEMORY

   See Also
   * CyAsStorageStart
*/
EXTERN cy_as_return_status_t
cy_as_storage_stop(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t		client
	);

/* Summary
   This function is used to register a callback function
   for the storage API.

   Description
   At times West Bridge needs to inform the P port processor
   of events that have occurred.  These events are asynchronous
   to the thread of control on the P
   port processor and as such are generally delivered via a
   callback function that
   is called as part of an interrupt handler.  This function
   registers the callback
   function that is called when an event occurs.  Each call
   to this function
   replaces any old callback function with a new callback
   function supplied on
   the most recent call.  This function can also be called
   with a callback function
   of NULL in order to remove any existing callback function

   * Valid In Asynchronous Callback:YES

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has
   *	not been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle
   *	was passed in
   * CY_AS_ERROR_SUCCESS - the function was registered
   *	sucessfully
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running

   See Also
   * CyAsStorageEventCallback
   * CyAsStorageEvent
*/
EXTERN cy_as_return_status_t
cy_as_storage_register_callback(
	/* Handle to the device of interest */
	cy_as_device_handle			handle,
	/* The callback function to call for async storage events */
	cy_as_storage_event_callback	callback
	);

/* Summary
   This function claims a given media type.

   Description
   This function communicates to West Bridge that the
   processor wants control of the
   given storage media type.  Each media type can be
   claimed or released by the
   processor independently.  As the processor is the
   master for the storage,
   West Bridge should release control of the requested
   media as soon as possible and
   signal the processor via the CyAsStorageProcessor event.

   * Valid In Asynchronous Callback: NO

   Notes
   This function just notifies West Bridge that the storage
   is desired.  The storage
   has not actually been released by West Bridge until the
   registered callback function
   is called with the CyAsStorageProcessor event

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - this request was sucessfully
   *	transmitted to the West Bridge device
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_MEDIA
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_NOT_ACQUIRED

   See Also:
   * CyAsStorageClaim
   * CyAsStorageRelease
*/
EXTERN cy_as_return_status_t
cy_as_storage_claim(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to claim */
	cy_as_bus_number_t	 bus,
	/* The device to claim */
	uint32_t		 device,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t		client
	);

/* Summary
   This function releases a given media type.

   Description
   This function communicates to West Bridge that the
   processor has released control of
   the given storage media type.  Each media type can
   be claimed or released by the
   processor independently.  As the processor is the
   master for the storage, West Bridge
   can now assume ownership of the media type.  No callback
   or event is generated.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle
   *	was passed in
   * CY_AS_ERROR_SUCCESS - the media was sucessfully
   *	released
   * CY_AS_ERROR_MEDIA_NOT_CLAIMED - the media was not
   *	claimed by the P port
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_MEDIA
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsStorageClaim
*/
EXTERN cy_as_return_status_t
cy_as_storage_release(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to release */
	cy_as_bus_number_t	 bus,
	/* The device to release */
	uint32_t		 device,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t		client
	);

/* Summary
   This function information about the number of devices present
   on a given bus

   Description
   This function retrieves information about how many devices on
   on the given
   West Bridge bus.

   * Valid In Asynchronous Callback: NO

   Notes
   While the current implementation of West Bridge only
   supports one of logical device of
   each media type, future versions WestBridge/Antioch may
   support multiple devices.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsStorageQueryDevice
   * CyAsStorageQueryUnit
*/
EXTERN cy_as_return_status_t
cy_as_storage_query_bus(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to query */
	cy_as_bus_number_t		bus,
	/* The return value containing the number of
	devices present for this media type */
	uint32_t *count,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	 cb,
	/* Client data to be passed to the callback */
	uint32_t client
	);

/* Summary
   This function information about the number of devices
   present for a given media type

   Description
   This function retrieves information about how many
   devices of a given media type are attached to West Bridge.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   While the current implementation of West Bridge only
   supports one of logical device of each media type, future
   versions West Bridge may support multiple devices.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsStorageQueryMedia
   * CyAsMediaType
   * CyAsStorageQueryDevice
   * CyAsStorageQueryUnit
*/
EXTERN cy_as_return_status_t
cy_as_storage_query_media(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The type of media to query */
	cy_as_media_type		type,
	/* The return value containing the number of
	devices present for this media type */
	uint32_t *count,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	 cb,
	/* Client data to be passed to the callback */
	uint32_t		client
	);

/* Summary
   This function returns information about a given device
   of a specific media type

   Description
   This function retrieves information about a device of a
   given type of media.  The function is called with a given
   media type and device and a pointer to a media descriptor
   (CyAsDeviceDesc).  This function fills in the data in the
   media descriptor to provide information about the
   attributes of the device of the given device.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   Currently this API only supports a single logical device
   of each media type.  Therefore the only acceptable value
   for the parameter device is zero (0).

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_NO_SUCH_MEDIA
   * CY_AS_ERROR_NO_SUCH_DEVICE
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsMediaType
   * CyAsStorageQueryMedia
   * CyAsStorageQueryUnit
   * CyAsDeviceDesc
*/
EXTERN cy_as_return_status_t
cy_as_storage_query_device(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* Parameters and return value for the query call */
	cy_as_storage_query_device_data *data,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback		cb,
	 /* Client data to be passed to the callback */
	uint32_t			client
	);

/* Summary
   This function returns information about a given unit on a
   specific device

   Description
   This function retrieves information about a device of a
   given logical unit.  The function is called with a given
   media type, device address, unit address,  and a pointer
   to a unit descriptor (CyAsUnitDesc).  This function fills
   in the data in the unit descriptor to provide information
   about the attributes of the device of the given logical
   unit.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_NO_SUCH_DEVICE
   * CY_AS_ERROR_NO_SUCH_UNIT
   * CY_AS_ERROR_INVALID_RESPONSE


   See Also
   * CyAsMediaType
   * CyAsStorageQueryMedia
   * CyAsStorageQueryDevice
   * CyAsUnitDesc
*/
EXTERN cy_as_return_status_t
cy_as_storage_query_unit(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* Parameters and return value for the query call */
	cy_as_storage_query_unit_data *data_p,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback */
	uint32_t			client
	);

/* Summary
   This function enables/disables the handling of SD/MMC card
   detection and SD/MMC write protection in West Bridge Firmware.

   Description
   If the detection of SD/MMC card insertion or removal is being
   done by the Processor directly, the West Bridge firmware needs
   to be instructed to disable the card detect feature. Also, if
   the hardware design does not use the SD_WP GPIO of the West
   Bridge to handle SD card's write protect notch, the handling
   of write protection if firmware should be disabled. This API
   is used to enable/disable the card detect and write protect
   support in West Bridge firmware.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the feature controls were
   *	set successfully
   * CY_AS_ERROR_NO_SUCH_BUS - the specified bus is invalid
   * CY_AS_ERROR_NOT_SUPPORTED - function not supported on
   *	the device in the specified bus
   * CY_AS_ERROR_IN_SUSPEND - the West Brdige device is in
   *	suspended mode
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

*/
EXTERN cy_as_return_status_t
cy_as_storage_device_control(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* The bus to control */
	cy_as_bus_number_t	 bus,
	/* The device to control */
	uint32_t		 device,
	/* Enable/disable control for card detection */
	cy_bool		card_detect_en,
	/* Enable/disable control for write protect handling */
	cy_bool		write_prot_en,
	/* Control which pin is used for card detection */
	cy_as_storage_card_detect	config_detect,
	 /* Callback to be called when the operation is complete */
	cy_as_function_callback	 cb,
	/* Client data to be passed to the callback */
	uint32_t		client
		);

/* Summary
   This function reads one or more blocks of data from
   the storage system.

   Description
   This function synchronously reads one or more blocks
   of data from the given media
   type/device and places the data into the data buffer
   given.  This function does not
   return until the data is read and placed into the buffer.

   * Valid In Asynchronous Callback: NO

   Notes
   If the Samsung CEATA drive is the target for a
   read/write operation, the maximum
   number of sectors that can be accessed through a
   single API call is limited to 2047.
   Longer accesses addressed to a Samsung CEATA drive
   can result in time-out errors.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle
   *	was passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified
   *	does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified
   *	media/device pair does not exist
   * CY_AS_ERROR_NO_SUCH_UNIT - the unit specified
   *	does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation
   *	is pending
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was
   *	error in reading from the media
   * CY_AS_ERROR_MEDIA_WRITE_PROTECTED - the media is
   *	write protected
   * CY_AS_ERROR_INVALID_PARAMETER - Reads/Writes greater
   *	than 4095 logic blocks are not allowed

   See Also
   * CyAsStorageReadAsync
   * CyAsStorageWrite
   * CyAsStorageWriteAsync
   * CyAsStorageCancelAsync
   * <LINK Asynchronous Versus Synchronous Operation>
*/
EXTERN cy_as_return_status_t
cy_as_storage_read(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to access */
	cy_as_bus_number_t		bus,
	/* The device to access */
	uint32_t		device,
	/* The unit to access */
	uint32_t		unit,
	/* The first block to access */
	uint32_t		block,
	/* The buffer where data will be placed */
	void *data_p,
	/* The number of blocks to be read */
	uint16_t		num_blocks
	);

/* Summary
   This function asynchronously reads one or more blocks of data
   from the storage system.

   Description
   This function asynchronously reads one or more blocks of
   data from the given media
   type/device and places the data into the data buffer given.
   This function returns
   as soon as the request is transmitted to the West Bridge
   device but before the data is
   available.  When the read is complete, the callback function
   is called to indicate the
   data has been placed into the data buffer.  Note that the
   data buffer must remain
   valid from when the read is requested until the callback
   function is called.

   * Valid In Asynchronous Callback: YES

   Notes
   If the Samsung CEATA drive is the target for a read/write
   operation, the maximum
   number of sectors that can be accessed through a single API
   call is limited to 2047.
   Longer accesses addressed to a Samsung CEATA drive can
   result in time-out errors.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle
   *	was passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_ASYNC_PENDING - an async operation
   *	is pending
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error
   *	in reading from the media
   * CY_AS_ERROR_MEDIA_WRITE_PROTECTED - the media is
   *	write protected
   * CY_AS_ERROR_QUERY_DEVICE_NEEDED - Before an
   *	asynchronous read can be issue a call to
   * CyAsStorageQueryDevice must be made
   * CY_AS_ERROR_INVALID_PARAMETER - Reads/Writes greater
   * than 4095 logic blocks are not allowed

   See Also
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageWriteAsync
   * CyAsStorageCancelAsync
   * CyAsStorageQueryDevice
   * <LINK Asynchronous Versus Synchronous Operation>
*/
EXTERN cy_as_return_status_t
cy_as_storage_read_async(
	/* Handle to the device of interest */
	cy_as_device_handle handle,
	/* The bus to access */
	cy_as_bus_number_t	bus,
	/* The device to access */
	uint32_t device,
	/* The unit to access */
	uint32_t unit,
	/* The first block to access */
	uint32_t block,
	/* The buffer where data will be placed */
	void *data_p,
	/* The number of blocks to be read */
	uint16_t num_blocks,
	/* The function to call when the read is complete
	or an error occurs */
	cy_as_storage_callback		callback
	);

/* Summary
   This function writes one or more blocks of data
   to the storage system.

   Description
   This function synchronously writes one or more blocks of
   data to the given media/device.
   This function does not return until the data is written
   into the media.

   * Valid In Asynchronous Callback: NO

   Notes
   If the Samsung CEATA drive is the target for a read/write
   operation, the maximum
   number of sectors that can be accessed through a single
   API call is limited to 2047.
   Longer accesses addressed to a Samsung CEATA drive can
   result in time-out errors.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does
   *	not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified
   *	media/device pair does not exist
   * CY_AS_ERROR_NO_SUCH_UNIT - the unit specified
   *	does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation
   *	is pending
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error
   *	in reading from the media
   * CY_AS_ERROR_MEDIA_WRITE_PROTECTED - the media is
   *	write protected
   * CY_AS_ERROR_INVALID_PARAMETER - Reads/Writes greater
   *	than 4095 logic blocks are not allowed

   See Also
   * CyAsStorageRead
   * CyAsStorageReadAsync
   * CyAsStorageWriteAsync
   * CyAsStorageCancelAsync
   * <LINK Asynchronous Versus Synchronous Operation>
*/
EXTERN cy_as_return_status_t
cy_as_storage_write(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to access */
	cy_as_bus_number_t bus,
	/* The device to access */
	uint32_t device,
	/* The unit to access */
	uint32_t unit,
	/* The first block to access */
	uint32_t block,
	/* The buffer containing the data to be written */
	void *data_p,
	/* The number of blocks to be written */
	uint16_t num_blocks
	);

/* Summary
   This function asynchronously writes one or more blocks
   of data to the storage system

   Description
   This function asynchronously writes one or more blocks of
   data to the given media type/device.
   This function returns as soon as the request is transmitted
   to the West Bridge device
   but before the data is actually written.  When the write is
   complete, the callback
   function is called to indicate the data has been physically
   written into the media.

   * Valid In Asynchronous Callback: YES

   Notes
   If the Samsung CEATA drive is the target for a read/write
   operation, the maximum
   number of sectors that can be accessed through a single API
   call is limited to 2047.
   Longer accesses addressed to a Samsung CEATA drive can
   result in time-out errors.

   Notes
   The data buffer must remain valid from when the write is
   requested until the callback function is called.

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has
   *	not been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_ASYNC_PENDING - an async operation is
   *	pending
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_MEDIA_WRITE_PROTECTED - the media is write
   *	protected
   * CY_AS_ERROR_QUERY_DEVICE_NEEDED - A query device call is
   *	required before async writes are allowed
   * CY_AS_ERROR_INVALID_PARAMETER - Reads/Writes greater
   *	than 4095 logic blocks are not allowed

   See Also
   * CyAsStorageRead
   * CyAsStorageWrite
   * CyAsStorageReadAsync
   * CyAsStorageCancelAsync
   * CyAsStorageQueryDevice
   * <LINK Asynchronous Versus Synchronous Operation>
*/
EXTERN cy_as_return_status_t
cy_as_storage_write_async(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The bus to access */
	cy_as_bus_number_t	bus,
	/* The device to access */
	uint32_t	device,
	/* The unit to access */
	uint32_t	unit,
	/* The first block to access */
	uint32_t	block,
	/* The buffer where the data to be written is stored */
	void *data_p,
	/* The number of blocks to be written */
	uint16_t num_blocks,
	/* The function to call when the write is complete
		or an error occurs */
	cy_as_storage_callback	callback
	);

/* Summary
   This function aborts any outstanding asynchronous operation

   Description
   This function aborts any asynchronous block read or block
   write operation.  As only a single asynchronous block read
   or write operation is possible at one time, this aborts
   the single operation in progress.

   * Valid In Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_OPERATION_PENDING - no asynchronous
   *	operation is pending

   See Also
   * CyAsStorageRead
   * CyAsStorageReadAsync
   * CyAsStorageWrite
   * CyAsStorageWriteAsync
   * <LINK Asynchronous Versus Synchronous Operation>
*/
EXTERN cy_as_return_status_t
cy_as_storage_cancel_async(
	/* Handle to the device with outstanding async request */
	cy_as_device_handle		handle
	);

/* Summary
   This function is used to read the content of SD registers

   Description
   This function is used to read the contents of CSD, CID and
   CSD registers of the SD Card.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the read operation was successful
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_IN_SUSPEND - The West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device pair
   *	does not exist
   * CY_AS_ERROR_INVALID_PARAMETER - The register type is invalid
   *	or the media is not supported on the bus
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to get memory to process
   *	request
   * CY_AS_ERROR_INVALID_RESPONSE - communication failure with
   *	West Bridge firmware

   See Also
   * CyAsStorageSDRegReadData
 */
EXTERN cy_as_return_status_t
cy_as_storage_sd_register_read(
	/* Handle to the West Bridge device. */
	cy_as_device_handle	handle,
	/* The bus to query */
	cy_as_bus_number_t	bus,
	/* The device to query */
	uint8_t	device,
	/* The type of register to read. */
	cy_as_sd_card_reg_type reg_type,
	/* Output data buffer and length. */
	cy_as_storage_sd_reg_read_data	 *data_p,
	/* Callback function to call when done. */
	cy_as_function_callback		cb,
	/* Call context to send to the cb function. */
	uint32_t client
	);

/* Summary
   Creates a partition starting at the given block and using the
   remaining blocks on the card.

   Description
   Storage devices attached to West Bridge can be partitioned
   into two units.
   The visibility of these units through the mass storage
   interface can be
   individually controlled.  This API is used to partition
   a device into two.

   * Valid in Asynchronous Callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the partition was successfully created
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_IN_SUSPEND - The West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_USB_RUNNING - Partition cannot be created while
   *	USB stack is active
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to get memory to
   *	process request
   * CY_AS_ERROR_INVALID_REQUEST - feature not supported by
   *	active device or firmware
   * CY_AS_ERROR_INVALID_RESPONSE - communication failure with
   *	West Bridge firmware
   * CY_AS_ERROR_ALREADY_PARTITIONED - the storage device already
   *	has been partitioned
   * CY_AS_ERROR_INVALID_BLOCK - Size specified for the partition
   *	exceeds the actual device capacity

   See Also
   * <LINK Partitioning>
   * CyAsStorageRemovePPartition
 */
EXTERN cy_as_return_status_t
cy_as_storage_create_p_partition(
	/* Handle to the device of interest */
	cy_as_device_handle handle,
	/* Bus on which the device to be partitioned is connected */
	cy_as_bus_number_t bus,
	/* Device number to be partitioned */
	uint32_t device,
	/* Size of partition number 0 in blocks */
	uint32_t size,
	/* Callback in case of async call */
	cy_as_function_callback cb,
	/* Client context to pass to the callback */
	uint32_t client
	);

/* Summary
   Removes the partition table on a storage device connected
   to the West Bridge.

   Description
   Storage devices attached to West Bridge can be partitioned
   into two units.This partition information is stored on the
   device and is non-volatile.  This API is used to remove the
   stored partition information and make the entire device
   visible as a single partition (unit).

   * Valid in Asynchronous Callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the partition was successfully
   *	deleted
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_IN_SUSPEND - The West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_USB_RUNNING - Partition cannot be created
   *	while USB stack is active
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to get memory to
   *	process request
   * CY_AS_ERROR_INVALID_REQUEST - operation not supported
   *	by active device/firmware
   * CY_AS_ERROR_NO_SUCH_UNIT - the addressed device is
   *	not partitioned

   See Also
   * <LINK Partitioning>
   * CyAsStorageCreatePPartition
 */
EXTERN cy_as_return_status_t
cy_as_storage_remove_p_partition(
	/* Handle to the device of interest */
	cy_as_device_handle handle,
	/* Bus on which device of interest is connected */
	cy_as_bus_number_t  bus,
	/* Device number of interest */
	uint32_t device,
	/* Callback in case of async call */
	cy_as_function_callback cb,
	/* Client context to pass to the callback */
	uint32_t client
	);

/* Summary
   Returns the amount of data read/written to the given
   device from the USB host.

   Description

   * Valid in Asynchronous Callback: Yes (if cb supplied)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - API call completed successfully
   * CY_AS_ERROR_INVALID_HANDLE - Invalid West Bridge device
   *	handle
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - No firmware image has been
   * loaded on West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - Storage stack has not been
   *	started
   * CY_AS_ERROR_NOT_SUPPORTED - This function is not
   *	supported by active firmware version
   * CY_AS_ERROR_OUT_OF_MEMORY - Failed to get memory to
   *	process the request
   * CY_AS_ERROR_TIMEOUT - West Bridge firmware did not
   *	respond to request
   * CY_AS_ERROR_INVALID_RESPONSE - Unexpected reply from
   *	West Bridge firmware

   See Also
   * CyAsUsbSetMSReportThreshold
*/
EXTERN cy_as_return_status_t
cy_as_storage_get_transfer_amount(
	/* Handle to the device of interest */
	cy_as_device_handle handle,
	/* Bus on which device of interest is connected */
	cy_as_bus_number_t  bus,
	/* Device number of interest */
	uint32_t device,
	/* Return value containing read/write sector counts. */
	cy_as_m_s_c_progress_data *data_p,
	/* Callback in case of async call */
	cy_as_function_callback cb,
	/* Client context to pass to the callback */
	uint32_t client
	);

/* Summary
   Performs a Sector Erase on an attached SD Card

   Description
   This allows you to erase an attached SD card. The area to erase
   is specified in terms of a starting Erase Unit and a number of
   Erase Units. The size of each Erase Unit is defined in the
   DeviceDesc returned from a StorageQueryDevice call and it can
   differ between SD cards.

   A large erase can take a while to complete depending on the SD
   card. In such a case it is reccomended that an async call is made.

   Returns
   * CY_AS_ERROR_SUCCESS - API call completed successfully
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed in
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_ASYNC_PENDING - an async operation is pending
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   * reading from the media
   * CY_AS_ERROR_MEDIA_WRITE_PROTECTED - the media is write protected
   * CY_AS_ERROR_QUERY_DEVICE_NEEDED - A query device call is
   *	required before erase is allowed
   * CY_AS_ERROR_NO_SUCH_BUS
   * CY_AS_ERROR_NO_SUCH_DEVICE
   * CY_AS_ERROR_NOT_SUPPORTED - Erase is currenly only supported
   *	on SD and using SD only firmware
   * CY_AS_ERROR_OUT_OF_MEMORY

   See Also
   * CyAsStorageSDRegisterRead
*/
EXTERN cy_as_return_status_t
cy_as_storage_erase(
	/* Handle to the device of interest */
	cy_as_device_handle	 handle,
	/* Bus on which device of interest is connected */
	cy_as_bus_number_t	bus,
	/* Device number of interest */
	uint32_t device,
	/* Erase Unit to start the erase */
	uint32_t erase_unit,
	/* Number of Erase Units to erase */
	uint16_t num_erase_units,
	/* Callback in case of async call */
	cy_as_function_callback cb,
	/* Client context to pass to the callback */
	uint32_t client
	);

/* Summary
   This function is used to read a Tuple from the SDIO CIS area.

   Description
   This function is used to read a Tuple from the SDIO CIS area.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device
   *	is in suspend mode
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation is pending
   * CY_AS_ERROR_INVALID_REQUEST - an invalid IO request
   *	type was made
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made to
   *	an invalid function
   * CY_AS_ERROR_INVALID_ENDPOINT - A DMA request was made to
   * an invalid endpoint
   * CY_AS_ERROR_ENDPOINT_DISABLED - A DMA request was made to
   *	a disabled endpoint

*/
cy_as_return_status_t
cy_as_sdio_get_c_i_s_info(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t		bus,
	/* Device number */
	uint32_t		device,
	 /* IO function Number */
	uint8_t			n_function_no,
	/* Id of tuple to be fetched */
	uint16_t		tuple_id,
	/* Buffer to hold tuple read from card.
	 should be at least 256 bytes in size */
	uint8_t *data_p
	);


/* Summary
   This function is used to read properties of the SDIO card.

   Description
   This function is used to read properties of the SDIO card
   into a CyAsSDIOCard structure.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   * loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_SUCCESS - the card information was returned
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   * suspend mode
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware

*/
cy_as_return_status_t
cy_as_sdio_query_card(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	 bus,
	/* Device number */
	uint32_t		device,
	/* Buffer to store card properties */
	cy_as_sdio_card		*data_p
		);

/* Summary
   This function is used to reset a SDIO card.

   Description
   This function is used to reset a SDIO card by writing to
   the reset bit in the CCCR and reinitializing the card. This
   function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   */
cy_as_return_status_t
cy_as_sdio_reset_card(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device
	);

/* Summary
   This function performs a Synchronous 1 byte read from the sdio
   device function.

   Description
   This function is used to perform a synchronous 1 byte read
   from an SDIO card function. This function is to be used only
   for IO to an SDIO card as other media will not respond to the
   SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed
   *	in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device pair
   *	does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was recieved
   *	from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in reading
   *	from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made to an
   *	invalid function
   * CY_AS_ERROR_FUNCTION_SUSPENDED - The function to which read
   *	was attempted is in suspend
*/
cy_as_return_status_t
cy_as_sdio_direct_read(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t		 bus,
	/* Device number */
	uint32_t		device,
	/* IO function Number */
	uint8_t			n_function_no,
	/* Address for IO */
	uint32_t		address,
	/* Set to CY_SDIO_REARM_INT to reinitialize SDIO interrupt */
	uint8_t			misc_buf,
	/* Buffer to hold byte read from card */
	uint8_t *data_p
		);

/* Summary
   This function performs a Synchronous 1 byte write to the
   sdio device function.

   Description
   This function is used to perform a synchronous 1 byte write
   to an SDIO card function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   * not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   * loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   * started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   * passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   * suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   * with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   * pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was recieved
   * from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   * reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made to
   * an invalid function
   * CY_AS_ERROR_FUNCTION_SUSPENDED - The function to which
   * write was attempted is in suspend
*/
cy_as_return_status_t
cy_as_sdio_direct_write(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t		device,
	/* IO function Number */
	uint8_t			n_function_no,
	/* Address for IO */
	uint32_t		address,
	/* Set to CY_SDIO_REARM_INT to reinitialize SDIO interrupt,
	set to CY_SDIO_RAW for read after write */
	uint8_t			misc_buf,
	/* Byte to write */
	uint16_t		argument,
	/* Buffer to hold byte read from card in Read after write mode */
	uint8_t *data_p
	);

/* Summary
   This function is used to set the blocksize of an SDIO function.

   Description
   This function is used to set the blocksize of an SDIO function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory
   *	available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   * to an invalid function
   * CY_AS_ERROR_INVALID_BLOCKSIZE - An incorrect blocksize
   * was passed to the function.
   * CY_AS_ERROR_FUNCTION_SUSPENDED - The function to which
   * write was attempted is in suspend
*/
cy_as_return_status_t
cy_as_sdio_set_blocksize(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Block size to set. */
	uint16_t blocksize
	);

/* Summary
   This function is used to read Multibyte/Block data from a
   IO function.

   Description
   This function is used to read Multibyte/Block data from a
   IO function. This function is to be used only for IO to an
   SDIO card as other media will not respond to the SDIO
   command set.

   * Valid in Asynchronous Callback: YES
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating with
   *	the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation is pending
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was recieved
   *	from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made to
   *	an invalid function
   * CY_AS_ERROR_INVALID_BLOCKSIZE - An incorrect blocksize or
   *	block count was passed to the function.
   * CY_AS_ERROR_FUNCTION_SUSPENDED - The function to which
   *	write was attempted is in suspend
   * CY_AS_ERROR_IO_ABORTED - The IO operation was aborted
   * CY_AS_ERROR_IO_SUSPENDED - The IO operation was suspended
   * CY_AS_ERROR_INVALID_REQUEST - An invalid request was
   *	passed to the card.

*/
cy_as_return_status_t
cy_as_sdio_extended_read(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Base Address for IO */
	uint32_t address,
	/* Set to CY_SDIO_BLOCKMODE for block IO,
	CY_SDIO_BYTEMODE for multibyte IO,
	CY_SDIO_OP_FIFO to read multiple bytes from the
	same address, CY_SDIO_OP_INCR to read bytes from
	the incrementing addresses */
	uint8_t			misc_buf,
	/* Block/Byte count to read */
	uint16_t		argument,
	/* Buffer to hold data read from card */
	uint8_t *data_p,
	/* Callback in case of Asyncronous call. 0 if Synchronous */
	cy_as_sdio_callback		callback
	);

/* Summary
   This function is used to write Multibyte/Block data
   to a IO function.

   Description
   This function is used to write Multibyte/Block data
   to a IO function. This function is to be used only
   for IO to an SDIO card as other media will not respond
   to the SDIO command set.

   * Valid in Asynchronous Callback: YES
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation is pending
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   *	to an invalid function
   * CY_AS_ERROR_INVALID_BLOCKSIZE - An incorrect blocksize or
   *	block count was passed to the function.
   * CY_AS_ERROR_FUNCTION_SUSPENDED - The function to which
   *	write was attempted is in suspend
   * CY_AS_ERROR_IO_ABORTED - The IO operation was aborted
   * CY_AS_ERROR_IO_SUSPENDED - The IO operation was suspended
   * CY_AS_ERROR_INVALID_REQUEST - An invalid request was
   *	passed to the card.
*/
cy_as_return_status_t
cy_as_sdio_extended_write(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Base Address for IO */
	uint32_t address,
	/* Set to CY_SDIO_BLOCKMODE for block IO,
	CY_SDIO_BYTEMODE for multibyte IO,
	CY_SDIO_OP_FIFO to write multiple bytes to the same address,
	CY_SDIO_OP_INCR to write multiple bytes to incrementing
	addresses */
	uint8_t	 misc_buf,
	/* Block/Byte count to write
	in case of byte mode the count should not exceed the block size
	or 512, whichever is smaller.
	in case of block mode, maximum number of blocks is 511. */
	uint16_t		argument,
	/* Buffer to hold data to be written to card. */
	uint8_t *data_p,
	/* Callback in case of Asyncronous call. 0 if Synchronous */
	cy_as_sdio_callback		callback
	);

/* Summary
   This function is used to initialize a SDIO card function.

   Description
   This function is used to initialize a SDIO card function
   (1 - 7). This function is to be used only for IO to an
   SDIO card as other media will not respond to the SDIO
   command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed
   *	in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   * pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error in
   *	reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   *	to an invalid function
*/
cy_as_return_status_t
cy_as_sdio_init_function(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Set to CY_SDIO_FORCE_INIT to reinitialize function */
	uint8_t	misc_buf
	);

/* Summary
   This function is used to get properties of a SDIO card function.

   Description
   This function is used to get properties of a SDIO card functio
   (1 - 7) into a CyAsSDIOFunc structure. This function is to be
   used only for IO to an SDIO card as other media will not respond
   to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been loaded
   *	into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not been
   *	started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was passed
   *	in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the media specified does
   *	not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device pair
   *	does not exist
   * CY_AS_ERROR_INVALID_FUNCTION - An IO request was made to
   *	an invalid function
*/
cy_as_return_status_t
cy_as_sdio_query_function(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Buffer to store function properties */
	cy_as_sdio_func *data_p
	);

/* Summary
   This function is used to Abort the current IO function.

   Description
   This function is used to Abort the current IO function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified
   *	media/device pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory
   *	available
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   *	to an invalid function
*/
cy_as_return_status_t
cy_as_sdio_abort_function(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t  bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no
		);

/* Summary
   This function is used to Disable IO to an SDIO function.

   Description
   This function is used to Disable IO to an SDIO function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is
   *	in suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   *	to an invalid function
*/
cy_as_return_status_t
cy_as_sdio_de_init_function(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no
	);

/* Summary
   This function is used to Suspend the current IO function.

   Description
   This function is used to Suspend the current IO function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has
   *	not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is in
   *	suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred communicating
   *	with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not
   *	exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified
   *	media/device pair does not exist
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory
   *	available
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was made
   *	to an invalid function
*/
cy_as_return_status_t
cy_as_sdio_suspend(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t  bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no
	);

/* Summary
   This function is used to resume a Suspended IO function.

   Description
   This function is used to resume a Suspended IO function.
   This function is to be used only for IO to an SDIO card as
   other media will not respond to the SDIO command set.

   * Valid in Asynchronous Callback: NO
   * Valid on Antioch device: NO

   Returns
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	has not been configured
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been
   *	loaded into West Bridge
   * CY_AS_ERROR_NOT_RUNNING - the storage stack has not
   *	been started
   * CY_AS_ERROR_INVALID_HANDLE - an invalid handle was
   *	passed in
   * CY_AS_ERROR_IN_SUSPEND - the West Bridge device is
   *	in suspend mode
   * CY_AS_ERROR_SUCCESS - the media information was
   *	returned
   * CY_AS_ERROR_TIMEOUT - a timeout occurred
   *	communicating with the West Bridge device
   * CY_AS_ERROR_NOT_RUNNING - the stack is not running
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified
   *	does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified
   *	media/device pair does not exist
   * CY_AS_ERROR_ASYNC_PENDING - an async operation
   *	is pending
   * CY_AS_ERROR_OUT_OF_MEMORY - insufficient memory
   *	available
   * CY_AS_ERROR_INVALID_RESPONSE - an error message was
   *	recieved from the firmware
   * CY_AS_ERROR_MEDIA_ACCESS_FAILURE - there was error
   *	in reading from the media
   * CY_AS_ERROR_INVALID_FUNCTION - An IO attempt was
   *	made to an invalid function
   * CY_AS_ERROR_IO_ABORTED - The IO operation was
   *	aborted
   * CY_AS_ERROR_IO_SUSPENDED - The IO operation was
   *	suspended
   * CY_AS_ERROR_INVALID_REQUEST - An invalid request was
   *	passed to the card.

*/
cy_as_return_status_t
cy_as_sdio_resume(
	/* Handle to the Westbridge device */
	cy_as_device_handle	handle,
	/* Bus to use */
	cy_as_bus_number_t	bus,
	/* Device number */
	uint32_t device,
	/* IO function Number */
	uint8_t	n_function_no,
	/* Operation to resume (Read or Write) */
	cy_as_oper_type	op,
	/* Micellaneous buffer same as for Extended read and Write */
	uint8_t	misc_buf,
	/* Number of pending blocks for IO. Should be less
	than or equal to the maximum defined for extended
	read and write */
	uint16_t pendingblockcount,
	 /* Buffer to continue the Suspended IO operation */
	uint8_t		 *data_p
	);



/* For supporting deprecated functions */
#include "cyasstorage_dep.h"

#include "cyas_cplus_end.h"

#endif				/* _INCLUDED_CYASSTORAGE_H_ */
