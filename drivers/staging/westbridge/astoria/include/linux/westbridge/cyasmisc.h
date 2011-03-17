/* Cypress West Bridge API header file (cyasmisc.h)
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

#ifndef _INCLUDED_CYASMISC_H_
#define _INCLUDED_CYASMISC_H_

#include "cyashal.h"
#include "cyastypes.h"
#include "cyasmedia.h"

#include "cyas_cplus_start.h"

#define CY_AS_LEAVE_STANDBY_DELAY_CLOCK	(1)
#define CY_AS_RESET_DELAY_CLOCK	(1)

#define CY_AS_LEAVE_STANDBY_DELAY_CRYSTAL (5)
#define CY_AS_RESET_DELAY_CRYSTAL (5)

/* The maximum number of buses supported */
#define	CY_AS_MAX_BUSES	(2)

/* The maximum number of storage devices supported per bus */
#define	CY_AS_MAX_STORAGE_DEVICES (1)

#define CY_AS_FUNCTCBTYPE_DATA_MASK (0x60000000U)
#define CY_AS_FUNCTCBTYPE_TYPE_MASK (0x1FFFFFFFU)

#define cy_as_funct_c_b_type_get_type(t) \
	((cy_as_funct_c_b_type)((t) & CY_AS_FUNCTCBTYPE_TYPE_MASK))
#define cy_as_funct_c_b_type_contains_data(t) \
	(((cy_as_funct_c_b_type)((t) & \
		CY_AS_FUNCTCBTYPE_DATA_MASK)) == CY_FUNCT_CB_DATA)

/**************************************
 * West Bridge Types
 **************************************/

/* Summary
   Specifies a handle to an West Bridge device

   Description
   This type represents an opaque handle to an West Bridge device.
   This handle is created via the CyAsMiscCreateDevice() function
   and is used in all subsequent calls that communicate to the West
   Bridge device.

   See Also
   * CyAsMiscCreateDevice
   * CyAsMiscDestroyDevice
*/
typedef void *cy_as_device_handle;

/* Summary
   This data type gives the mode for the DACK# signal
*/
typedef enum cy_as_device_dack_mode {
	cy_as_device_dack_ack, /* Operate in the ACK mode */
	cy_as_device_dack_eob /* Operate in the EOB mode */
} cy_as_device_dack_mode;

/* Summary
   This data structure gives the options for all hardware features.

   Description
   This structure contains the information required to initialize the
   West Bridge hardware. Any features of the device that can be
   configured by the caller are specified here.

   See Also
   * CyAsMiscConfigure
*/
typedef struct cy_as_device_config {
	/* If TRUE, the P port is running in SRAM mode. */
	cy_bool	srammode;
	/* If TRUE, the P port is synchronous, otherwise async */
	cy_bool	sync;
	/* If TRUE, DMA req will be delivered via the interrupt signal */
	cy_bool	dmaintr;
	/* Mode for the DACK# signal */
	cy_as_device_dack_mode dackmode;
	/* If TRUE, the DRQ line is active high, otherwise active low */
	cy_bool	drqpol;
	/* If TRUE, the DACK line is active high, otherwise active low */
	cy_bool	dackpol;
	/* If TRUE, the clock is connected to a crystal, otherwise it is
			connected to a clock */
	cy_bool	crystal;
} cy_as_device_config;


/* Summary
   Specifies a resource that can be owned by either the West Bridge
   device or by the processor.

   Description
   This enumerated type identifies a resource that can be owned
   either by the West Bridge device, or by the processor attached to
   the P port of the West Bridge device.

   See Also
   * CyAsMiscAcquireResource
   * CyAsMiscReleaseResource
*/
typedef enum cy_as_resource_type {
	cy_as_bus_u_s_b = 0, /* The USB D+ and D- pins */
	cy_as_bus_1  = 1, /* The SDIO bus */
	cy_as_bus_0  = 2	/* The NAND bus (not implemented) */
} cy_as_resource_type;

/* Summary
   Specifies the reset type for a software reset operation.

   Description
   When the West Bridge device is reset, there are two types of
   reset that arE possible.  This type indicates the type of reset
   requested.

   Notes
   Both of these reset types are software based resets; and are
   distinct from a chip level HARD reset that is applied through
   the reset pin on the West Bridge.

   The CyAsResetSoft type resets only the on-chip micro-controller
   in the West Bridge. In this case, the previously loaded firmware
   will continue running. However, the Storage and USB stack
   operations will need to be restarted, as any state relating to
   these would have been lost.

   The CyAsResetHard type resets the entire West Bridge chip, and will
   need a fresh configuration and firmware download.

   See Also
   * <LINK CyAsMiscReset>
 */

typedef enum cy_as_reset_type {
	/* Just resets the West Bridge micro-controller */
	cy_as_reset_soft,
	/* Resets entire device, firmware must be reloaded and
	the west bridge device must be re-initialized */
	cy_as_reset_hard
} cy_as_reset_type;



/* Summary
   This type specifies the polarity of the SD power pin.

   Description
   Sets the SD power pin ( port C, bit 6) to active low or
   active high.

*/

typedef enum cy_as_misc_signal_polarity {
	cy_as_misc_active_high,
	cy_as_misc_active_low

} cy_as_misc_signal_polarity;



/* Summary
	This type specifies the type of the data returned by a Function
	Callback.

	Description
	CY_FUNCT_CB_NODATA - This callback does not return any additional
	information in the data field.
	CY_FUNCT_CB_DATA   - The data field is used, and the CyAsFunctCBType
	will also contain the type of this data.

	See Also
	CyAsFunctionCallback
*/
typedef enum cy_as_funct_c_b_type {
	CY_FUNCT_CB_INVALID = 0x0U,
	/* Data from a CyAsMiscGetFirmwareVersion call. */
	CY_FUNCT_CB_MISC_GETFIRMWAREVERSION,
	/* Data from a CyAsMiscHeartBeatControl call. */
	CY_FUNCT_CB_MISC_HEARTBEATCONTROL,
	/* Data from a CyAsMiscAcquireResource call. */
	CY_FUNCT_CB_MISC_ACQUIRERESOURCE,
	/* Data from a CyAsMiscReadMCURegister call. */
	CY_FUNCT_CB_MISC_READMCUREGISTER,
	/* Data from a CyAsMiscWriteMCURegister call. */
	CY_FUNCT_CB_MISC_WRITEMCUREGISTER,
	/* Data from a CyAsMiscSetTraceLevel call. */
	CY_FUNCT_CB_MISC_SETTRACELEVEL,
	/* Data from a CyAsMiscStorageChanged call. */
	CY_FUNCT_CB_MISC_STORAGECHANGED,
	/* Data from a CyAsMiscGetGpioValue call. */
	CY_FUNCT_CB_MISC_GETGPIOVALUE,
	/* Data from a CyAsMiscSetGpioValue call. */
	CY_FUNCT_CB_MISC_SETGPIOVALUE,
	/* Data from a CyAsMiscDownloadFirmware call. */
	CY_FUNCT_CB_MISC_DOWNLOADFIRMWARE,
	/* Data from a CyAsMiscEnterStandby call. */
	CY_FUNCT_CB_MISC_ENTERSTANDBY,
	/* Data from a CyAsMiscEnterSuspend call. */
	CY_FUNCT_CB_MISC_ENTERSUSPEND,
	/* Data from a CyAsMiscLeaveSuspend call. */
	CY_FUNCT_CB_MISC_LEAVESUSPEND,
	/* Data from a CyAsMiscReset call. */
	CY_FUNCT_CB_MISC_RESET,
	/* Data from a CyAsMiscSetLowSpeedSDFreq or
	 * CyAsMiscSetHighSpeedSDFreq call. */
	CY_FUNCT_CB_MISC_SETSDFREQ,
	/* Data from a CyAsMiscSwitchPnandMode call */
	CY_FUNCT_CB_MISC_RESERVELNABOOTAREA,
	/* Data from a CyAsMiscSetSDPowerPolarity call */
	CY_FUNCT_CB_MISC_SETSDPOLARITY,

	/* Data from a CyAsStorageStart call. */
	CY_FUNCT_CB_STOR_START,
	/* Data from a CyAsStorageStop call. */
	CY_FUNCT_CB_STOR_STOP,
	/* Data from a CyAsStorageClaim call. */
	CY_FUNCT_CB_STOR_CLAIM,
	/* Data from a CyAsStorageRelease call. */
	CY_FUNCT_CB_STOR_RELEASE,
	/* Data from a CyAsStorageQueryMedia call. */
	CY_FUNCT_CB_STOR_QUERYMEDIA,
	/* Data from a CyAsStorageQueryBus call. */
	CY_FUNCT_CB_STOR_QUERYBUS,
	/* Data from a CyAsStorageQueryDevice call. */
	CY_FUNCT_CB_STOR_QUERYDEVICE,
	/* Data from a CyAsStorageQueryUnit call. */
	CY_FUNCT_CB_STOR_QUERYUNIT,
	/* Data from a CyAsStorageDeviceControl call. */
	CY_FUNCT_CB_STOR_DEVICECONTROL,
	/* Data from a CyAsStorageSDRegisterRead call. */
	CY_FUNCT_CB_STOR_SDREGISTERREAD,
	/* Data from a CyAsStorageCreatePartition call. */
	CY_FUNCT_CB_STOR_PARTITION,
	/* Data from a CyAsStorageGetTransferAmount call. */
	CY_FUNCT_CB_STOR_GETTRANSFERAMOUNT,
	/* Data from a CyAsStorageErase call. */
	CY_FUNCT_CB_STOR_ERASE,
	/* Data from a CyAsStorageCancelAsync call. */
	CY_FUNCT_CB_ABORT_P2S_XFER,
	/* Data from a CyAsUsbStart call. */
	CY_FUNCT_CB_USB_START,
	/* Data from a CyAsUsbStop call. */
	CY_FUNCT_CB_USB_STOP,
	/* Data from a CyAsUsbConnect call. */
	CY_FUNCT_CB_USB_CONNECT,
	/* Data from a CyAsUsbDisconnect call. */
	CY_FUNCT_CB_USB_DISCONNECT,
	/* Data from a CyAsUsbSetEnumConfig call. */
	CY_FUNCT_CB_USB_SETENUMCONFIG,
	/* Data from a CyAsUsbGetEnumConfig call. */
	CY_FUNCT_CB_USB_GETENUMCONFIG,
	/* Data from a CyAsUsbSetDescriptor call. */
	CY_FUNCT_CB_USB_SETDESCRIPTOR,
	/* Data from a CyAsUsbGetDescriptor call. */
	CY_FUNCT_CB_USB_GETDESCRIPTOR,
	/* Data from a CyAsUsbCommitConfig call. */
	CY_FUNCT_CB_USB_COMMITCONFIG,
	/* Data from a CyAsUsbGetNak call. */
	CY_FUNCT_CB_USB_GETNAK,
	/* Data from a CyAsUsbGetStall call. */
	CY_FUNCT_CB_USB_GETSTALL,
	/* Data from a CyAsUsbSignalRemoteWakeup call. */
	CY_FUNCT_CB_USB_SIGNALREMOTEWAKEUP,
	/* Data from a CyAnUsbClearDescriptors call. */
	CY_FUNCT_CB_USB_CLEARDESCRIPTORS,
	/* Data from a CyAnUsbSetMSReportThreshold call. */
	CY_FUNCT_CB_USB_SET_MSREPORT_THRESHOLD,
	/* Data from a CyAsMTPStart call. */
	CY_FUNCT_CB_MTP_START,
	/* Data from a CyAsMTPStop call. */
	CY_FUNCT_CB_MTP_STOP,
	/* Data from a CyAsMTPInitSendObject call. */
	CY_FUNCT_CB_MTP_INIT_SEND_OBJECT,
	/* Data from a CyAsMTPCancelSendObject call. */
	CY_FUNCT_CB_MTP_CANCEL_SEND_OBJECT,
	/* Data from a CyAsMTPInitGetObject call. */
	CY_FUNCT_CB_MTP_INIT_GET_OBJECT,
	/* Data from a CyAsMTPCancelGetObject call. */
	CY_FUNCT_CB_MTP_CANCEL_GET_OBJECT,
	/* Data from a CyAsMTPSendBlockTable call. */
	CY_FUNCT_CB_MTP_SEND_BLOCK_TABLE,
	/* Data from a CyAsMTPStopStorageOnly call. */
	CY_FUNCT_CB_MTP_STOP_STORAGE_ONLY,
	CY_FUNCT_CB_NODATA = 0x40000000U,
	CY_FUNCT_CB_DATA =   0x20000000U
} cy_as_funct_c_b_type;

/* Summary
   This type specifies the general West Bridge function callback.

   Description
   This callback is supplied as an argument to all asynchronous
   functions in the API. It iS called after the asynchronous function
   has completed.

   See Also
   CyAsFunctCBType
*/
typedef void (*cy_as_function_callback)(
	cy_as_device_handle	handle,
	cy_as_return_status_t	status,
	uint32_t		client,
	cy_as_funct_c_b_type	type,
	void	*data);

/* Summary
   This type specifies the general West Bridge event that has
   occurred.

   Description
   This type is used in the West Bridge misc callback function to
   indicate the type of callback.

   See Also
*/
typedef enum cy_as_misc_event_type {
	/* This event is sent when West Bridge has finished
	initialization and is ready to respond to API calls. */
	cy_as_event_misc_initialized = 0,

	/* This event is sent when West Bridge has left the
	standby state and is ready to respond to commands again. */
	cy_as_event_misc_awake,

	/* This event is sent periodically from the firmware
	to the processor. */
	cy_as_event_misc_heart_beat,

	/* This event is sent when the West Bridge has left the
	suspend mode and is ready to respond to commands
	again. */
	cy_as_event_misc_wakeup,

	 /* This event is sent when the firmware image downloaded
	cannot run on the active west bridge device. */
	cy_as_event_misc_device_mismatch
} cy_as_misc_event_type;

/* Summary
   This type is the type of a callback function that is called when a
   West Bridge misc event occurs.

   Description
   At times West Bridge needs to inform the P port processor of events
   that have occurred. These events are asynchronous to the thread of
   control on the P port processor and as such are generally delivered
   via a callback function that is called as part of an interrupt
   handler. This type defines the type of function that must be provided
   as a callback function for West Bridge misc events.

   See Also
   * CyAsMiscEventType
*/
typedef void (*cy_as_misc_event_callback)(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* The event type being reported */
	cy_as_misc_event_type		ev,
	/* The data assocaited with the event being reported */
	void *evdata
);

#ifndef __doxygen__
/* Summary
   This enum provides info of various firmware trace levels.

   Description

   See Also
   * CyAsMiscSetTraceLevel
*/
enum {
	CYAS_FW_TRACE_LOG_NONE = 0,	/* Log nothing. */
	CYAS_FW_TRACE_LOG_STATE,	/* Log state information. */
	CYAS_FW_TRACE_LOG_CALLS,	/* Log function calls. */
	CYAS_FW_TRACE_LOG_STACK_TRACE,	/* Log function calls with args. */
	CYAS_FW_TRACE_MAX_LEVEL		/* Max trace level sentinel. */
};
#endif

/* Summary
   This enum lists the controllable GPIOs of the West Bridge device.

   Description
   The West Bridge device has GPIOs that can be used for user defined functions.
   This enumeration lists the GPIOs that are available on the device.

   Notes
   All of the GPIOs except UVALID can only be accessed when using West Bridge
   firmware images that support only SD/MMC/MMC+ storage devices. This
   functionality is not supported in firmware images that support NAND
   storage.

   See Also
   * CyAsMiscGetGpioValue
   * CyAsMiscSetGpioValue
 */
typedef enum {
	cy_as_misc_gpio_0 = 0,	/* GPIO[0] pin */
	cy_as_misc_gpio_1, /* GPIO[1] pin */
	cy_as_misc_gpio__nand_CE, /* NAND_CE pin, output only */
	cy_as_misc_gpio__nand_CE2, /* NAND_CE2 pin, output only */
	cy_as_misc_gpio__nand_WP, /* NAND_WP pin, output only */
	cy_as_misc_gpio__nand_CLE, /* NAND_CLE pin, output only */
	cy_as_misc_gpio__nand_ALE, /* NAND_ALE pin, output only */
	/* SD_POW pin, output only, do not drive low while storage is active */
	cy_as_misc_gpio_SD_POW,
	cy_as_misc_gpio_U_valid				 /* UVALID pin */
} cy_as_misc_gpio;

/* Summary
   This enum lists the set of clock frequencies that are supported for
   working with low speed SD media.

   Description
   West Bridge firmware uses a clock frequency less than the maximum
   possible rate for low speed SD media.  This can be changed to a
   setting equal to the maximum frequency as desired by the user. This
   enumeration lists the different frequency settings that are
   supported.

   See Also
   * CyAsMiscSetLowSpeedSDFreq
 */
typedef enum cy_as_low_speed_sd_freq {
	/* Approx. 21.82 MHz, default value */
	CY_AS_SD_DEFAULT_FREQ = 0,
	/* 24 MHz */
	CY_AS_SD_RATED_FREQ
} cy_as_low_speed_sd_freq;

/* Summary
   This enum lists the set of clock frequencies that are supported
   for working with high speed SD media.

   Description
   West Bridge firmware uses a 48 MHz clock by default to interface
   with high speed SD/MMC media.  This can be changed to 24 MHz if
   so desired by the user. This enum lists the different frequencies
   that are supported.

   See Also
   * CyAsMiscSetHighSpeedSDFreq
 */
typedef enum cy_as_high_speed_sd_freq {
	CY_AS_HS_SD_FREQ_48, /* 48 MHz, default value */
	CY_AS_HS_SD_FREQ_24	/* 24 MHz */
} cy_as_high_speed_sd_freq;

/* Summary
   Struct encapsulating all information returned by the
   CyAsMiscGetFirmwareVersion call.

   Description
   This struct encapsulates all return values from the asynchronous
   CyAsMiscGetFirmwareVersion call, so that a single data argument
   can be passed to the user provided callback function.

   See Also
   * CyAsMiscGetFirmwareVersion
 */
typedef struct cy_as_get_firmware_version_data {
	/* Return value for major version number for the firmware */
	uint16_t	 major;
	/* Return value for minor version number for the firmware */
	uint16_t	 minor;
	/* Return value for build version number for the firmware */
	uint16_t	 build;
	/* Return value for media types supported in the current firmware */
	uint8_t	 media_type;
	/* Return value to indicate the release or debug mode of firmware */
	cy_bool	is_debug_mode;
} cy_as_get_firmware_version_data;


/*****************************
 * West Bridge Functions
 *****************************/

/* Summary
   This function creates a new West Bridge device and returns a
   handle to the device.

   Description
   This function initializes the API object that represents the West
   Bridge device and returns a handle to this device.  This handle is
   required for all West Bridge related functions to identify the
   specific West Bridge device.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_OUT_OF_MEMORY
*/
EXTERN cy_as_return_status_t
cy_as_misc_create_device(
	/* Return value for handle to created device */
	cy_as_device_handle *handle_p,
	/* The HAL specific tag for this device */
	cy_as_hal_device_tag		tag
	);

/* Summary
   This functions destroys a previously created West Bridge device.

   Description
   When an West Bridge device is created, an opaque handle is returned
   that represents the device.  This function destroys that handle and
   frees all resources associated with the handle.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_STILL_RUNNING - The USB or STORAGE stacks are still
   *	running, they must be stopped before the device can be destroyed
   * CY_AS_ERROR_DESTROY_SLEEP_CHANNEL_FAILED - the HAL layer failed to
   *	destroy a sleep channel
*/
EXTERN cy_as_return_status_t
cy_as_misc_destroy_device(
	/* Handle to the device to destroy */
	cy_as_device_handle		handle
	);

/* Summary
   This function initializes the hardware for basic communication with
   West Bridge.

   Description
   This function initializes the hardware to establish basic
   communication with the West Bridge device.  This is always the first
   function called to initialize communication with the West Bridge
   device.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - the basic initialization was completed
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_IN_STANDBY
   * CY_AS_ERROR_ALREADY_RUNNING
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_NO_ANTIOCH - cannot find the West Bridge device
   * CY_AS_ERROR_CREATE_SLEEP_CHANNEL_FAILED -
   *	the HAL layer falied to create a sleep channel

   See Also
   * CyAsDeviceConfig
*/
EXTERN cy_as_return_status_t
cy_as_misc_configure_device(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* Configuration information */
	cy_as_device_config		*config_p
	);

/* Summary
   This function returns non-zero if West Bridge is in standby and
   zero otherwise.

   Description
   West Bridge supports a standby mode.  This function is used to
   query West Bridge to determine if West Bridge is in a standby
   mode.

   * Valid In Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
*/
EXTERN cy_as_return_status_t
cy_as_misc_in_standby(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* Return value for standby state */
	cy_bool					*standby
	);

/* Summary
   This function downloads the firmware to West Bridge device.

   Description
   This function downloads firmware from a given location and with a
   given size to the West Bridge device.  After the firmware is
   downloaded the West Bridge device is moved out of configuration
   mode causing the firmware to be executed.  It is an error to call
   this function when the device is not in configuration mode.  The
   device is in configuration mode on power up and may be placed in
   configuration mode after power up with a hard reset.

   Notes
   The firmware must be on a word align boundary.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the firmware was sucessfully downloaded
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device
   *	was not configured
   * CY_AS_ERROR_NOT_IN_CONFIG_MODE
   * CY_AS_ERROR_INVALID_SIZE - the size of the firmware
   *	exceeded 32768 bytes
   * CY_AS_ERROR_ALIGNMENT_ERROR
   * CY_AS_ERROR_IN_STANDBY - trying to download
   *	while in standby mode
   * CY_AS_ERROR_TIMEOUT

   See Also
   * CyAsMiscReset
*/
EXTERN cy_as_return_status_t
cy_as_misc_download_firmware(
	/* Handle to the device to configure */
	cy_as_device_handle	  handle,
	/* Pointer to the firmware to be downloaded */
	const void			   *fw_p,
	/* The size of the firmware in bytes */
	uint16_t			  size,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback  cb,
	/* Client data to be passed to the callback. */
	uint32_t			  client
	);


/* Summary
   This function returns the version number of the firmware running in
   the West Bridge device.

   Description
   This function queries the West Bridge device and retreives the
   firmware version number.  If the firmware is not loaded an error is
   returned indicated no firmware has been loaded.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the firmware version number was retreived
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE - the firmware has not been downloaded
   *	to the device
   * CY_AS_ERROR_IN_STANDBY
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a response
   *	from the West Bridge firmware
*/
EXTERN cy_as_return_status_t
cy_as_misc_get_firmware_version(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* Return values indicating the firmware version. */
	cy_as_get_firmware_version_data	*data,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

#if !defined(__doxygen__)

/* Summary
   This function reads and returns the contents of an MCU accessible
   register on the West Bridge.

   Description
   This function requests the firmware to read and return the contents
   of an MCU accessible register through the mailboxes.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the register content was retrieved.
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a response
   *	from the West Bridge firmware
   * CY_AS_ERROR_INVALID_RESPONSE - the firmware build does not
   *	support this command.
*/
EXTERN cy_as_return_status_t
cy_as_misc_read_m_c_u_register(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* Address of the register to read */
	uint16_t			address,
	/* Return value for the MCU register content */
	uint8_t				*value,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
	);

/* Summary
   This function writes to an MCU accessible register on the West Bridge.

   Description
   This function requests the firmware to write a specified value to an
   MCU accessible register through the mailboxes.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Notes
   This function is only for internal use by the West Bridge API layer.
   Calling this function directly can cause device malfunction.

   Returns
   * CY_AS_ERROR_SUCCESS - the register content was updated.
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a response
   *	from the West Bridge firmware
   * CY_AS_ERROR_INVALID_RESPONSE - the firmware build does not support
   * this command.
*/
EXTERN cy_as_return_status_t
cy_as_misc_write_m_c_u_register(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* Address of the register to write */
	uint16_t	address,
	/* Mask to be applied on the register contents. */
	uint8_t		mask,
	/* Data to be ORed with the register contents. */
	uint8_t		value,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t	client
	);

#endif

/* Summary
   This function will reset the West Bridge device and software API.

   Description
   This function will reset the West Bridge device and software API.
   The reset operation can be a hard reset or a soft reset.  A hard
   reset will reset all aspects of the West Bridge device. The device
   will enter the configuration state and the firmware will have to be
   reloaded.  The device will also have to be re-initialized.  A soft
   reset just resets the West Bridge micro-controller.

   * Valid In Asynchronous Callback: NO

   Notes
   When a hard reset is issued, the firmware that may have been
   previously loaded will be lost and any configuration information set
   via CyAsMiscConfigureDevice() will be lost.  This will be reflected
   in the API maintained state of the device.  In order to re-establish
   communications with the West Bridge device, CyAsMiscConfigureDevice()
   and CyAsMiscDownloadFirmware() must be called again.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the device has been reset
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_NOT_YET_SUPPORTED - current soft reset is not supported
   * CY_AS_ERROR_ASYNC_PENDING - Reset is unable to flush pending async
   *	reads/writes in polling mode.


	  See Also
   * CyAsMiscReset
*/
EXTERN cy_as_return_status_t
cy_as_misc_reset(
	/* Handle to the device to configure */
	cy_as_device_handle handle,
	/* The type of reset to perform */
	cy_as_reset_type type,
	/* If true, flush all pending writes to mass storage
	 before performing the reset. */
	cy_bool flush,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   This function acquires a given resource.

   Description
   There are resources in the system that are shared between the
   West Bridge device and the processor attached to the P port of
   the West Bridge device.  This API provides a mechanism for the
   P port processor to acquire ownership of a resource.

   Notes
   The ownership of the resources controlled by CyAsMiscAcquireResource()
   and CyAsMiscReleaseResource() defaults to a known state at hardware
   reset.  After the firmware is loaded and begins execution the state of
   these resources may change.  At any point if the P Port processor needs
   to acquire a resource it should do so explicitly to be sure of
   ownership.

   Returns
   * CY_AS_ERROR_SUCCESS - the p port sucessfully acquired the
   * 	resource of interest
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_INVALID_RESOURCE
   * CY_AS_ERROR_RESOURCE_ALREADY_OWNED - the p port already
   *	owns this resource
   * CY_AS_ERROR_NOT_ACQUIRED - the resource cannot be acquired
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a
   *	response from the West Bridge firmware

   See Also
   * CyAsResourceType
*/
EXTERN cy_as_return_status_t
cy_as_misc_acquire_resource(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* The resource to acquire */
	cy_as_resource_type	*resource,
	/* If true, force West Bridge to release the resource */
	cy_bool				force,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
	);

/* Summary
   This function releases a given resource.

   Description
   There are resources in the system that are shared between the
   West Bridge device and the processor attached to the P port of
   the West Bridge device.  This API provides a mechanism for the
   P port processor to release a resource that has previously been
   acquired via the CyAsMiscAcquireResource() call.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - the p port sucessfully released
   *	the resource of interest
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_INVALID_RESOURCE
   * CY_AS_ERROR_RESOURCE_NOT_OWNED - the p port does not own the
   *	resource of interest

   See Also
   * CyAsResourceType
   * CyAsMiscAcquireResource
*/
EXTERN cy_as_return_status_t
cy_as_misc_release_resource(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* The resource to release */
	cy_as_resource_type		resource
	);

#ifndef __doxygen__
/* Summary
   This function sets the trace level for the West Bridge firmware.

   Description
   The West Bridge firmware has the ability to store information
   about the state and execution path of the firmware on a mass storage
   device attached to the West Bridge device.  This function configures
   the specific mass storage device to be used and the type of information
   to be stored.  This state information is used for debugging purposes
   and must be interpreted by a Cypress provided tool.

   *Trace Level*
   The trace level indicates the amount of information to output.
   * 0 = no trace information is output
   * 1 = state information is output
   * 2 = function call information is output
   * 3 = function call, arguments, and return value information is output

   * Valid In Asynchronous Callback: NO

   Notes
   The media device and unit specified in this call will be overwritten
   and any data currently stored on this device and unit will be lost.

   * NOT IMPLEMENTED YET

   Returns
   * CY_AS_ERROR_SUCCESS - the trace configuration has been
   *	sucessfully changed
   * CY_AS_ERROR_NO_SUCH_BUS - the bus specified does not exist
   * CY_AS_ERROR_NO_SUCH_DEVICE - the specified media/device
   *	pair does not exist
   * CY_AS_ERROR_NO_SUCH_UNIT - the unit specified does not exist
   * CY_AS_ERROR_INVALID_TRACE_LEVEL - the trace level requested
   *	does not exist
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a
   *	response from the West Bridge firmware
*/
EXTERN cy_as_return_status_t
cy_as_misc_set_trace_level(
	/* Handle to the device to configure */
	cy_as_device_handle	handle,
	/* The trace level */
	uint8_t	level,
	/* The bus for the output */
	cy_as_bus_number_t	bus,
	/* The device for the output */
	uint32_t device,
	/* The unit for the output */
	uint32_t unit,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
	);
#endif

/* Summary
   This function places West Bridge into the low power standby mode.

   Description
   This function places West Bridge into a low power (sleep) mode, and
   cannot be called while the USB stack is active.  This function first
   instructs the West Bridge firmware that the device is about to be
   placed into sleep mode.  This allows West Bridge to complete any pending
   storage operations.  After the West Bridge device has responded that
   pending operations are complete, the device is placed in standby mode.

   There are two methods of placing the device in standby mode.  If the
   WAKEUP pin of the West Bridge is connected to a GPIO on the processor,
   the pin is de-asserted (via the HAL layer) and West Bridge enters into
   a sleep mode.  If the WAKEUP pin is not accessible, the processor can
   write into the power management control/status register on the West
   Bridge to put the device into sleep mode.

   * Valid In Asynchronous Callback: YES (if cb supplied)
   * Nestable: YES

   Returns
   * CY_AS_ERROR_SUCCESS - the function completed and West Bridge
   *	is in sleep mode
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_ALREADY_STANDBY - the West Bridge device is already
   *	in sleep mode
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a response
   *	from the West Bridge firmware
   * CY_AS_ERROR_NOT_SUPPORTED - the HAL layer does not support changing
   *	the WAKEUP pin
   * CY_AS_ERROR_USB_RUNNING - The USB stack is still running when the
   *	EnterStandby call is made
   * CY_AS_ERROR_ASYNC_PENDING
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE
   * CY_AS_ERROR_SETTING_WAKEUP_PIN
   * CY_AS_ERROR_ASYNC_PENDING - In polling mode EnterStandby can not
   *	be called until all pending storage read/write requests have
   *	finished.

   See Also
   * CyAsMiscLeaveStandby
*/
EXTERN cy_as_return_status_t
cy_as_misc_enter_standby_e_x_u(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* If true, use the wakeup pin, otherwise use the register */
	cy_bool				pin,
	/* Set true to enable specific usages of the
	UVALID signal, please refer to AN xx or ERRATA xx */
	cy_bool				uvalid_special,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
	);

/* Summary
   This function is provided for backwards compatibility.

   Description
   Calling this function is the same as calling CyAsMiscEnterStandbyEx
   with True for the lowpower parameter.

   See Also
   * CyAsMiscEnterStandbyEx
*/
EXTERN cy_as_return_status_t
cy_as_misc_enter_standby(cy_as_device_handle handle,
			cy_bool pin,
			cy_as_function_callback cb,
			uint32_t client
			);

/* Summary
   This function brings West Bridge out of sleep mode.

   Description
   This function asserts the WAKEUP pin (via the HAL layer). This
   brings the West Bridge out of the sleep state and allows the
   West Bridge firmware to process the event causing the wakeup.
   When all processing associated with the wakeup is complete, a
   callback function is called to tell the P port software that
   the firmware processing associated with wakeup is complete.

   * Valid In Asynchronous Callback: NO

   Returns:
   * CY_AS_ERROR_SUCCESS - the function completed and West Bridge
   *	is in sleep mode
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_SETTING_WAKEUP_PIN
   * CY_AS_ERROR_NOT_IN_STANDBY - the West Bridge device is not in
   *	the sleep state
   * CY_AS_ERROR_TIMEOUT - there was a timeout waiting for a
   *	response from the West Bridge firmware
   * CY_AS_ERROR_NOT_SUPPORTED - the HAL layer does not support
   *	changing the WAKEUP pin

   See Also
   * CyAsMiscEnterStandby
*/
EXTERN cy_as_return_status_t
cy_as_misc_leave_standby(
	/* Handle to the device to configure */
	cy_as_device_handle		handle,
	/* The resource causing the wakeup */
	cy_as_resource_type		resource
	);

/* Summary
   This function registers a callback function to be called when an
   asynchronous West Bridge MISC event occurs.

   Description
   When asynchronous misc events occur, a callback function can be
   called to alert the calling program.  This functions allows the
   calling program to register a callback.

   * Valid In Asynchronous Callback: NO

   Returns:
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
*/
EXTERN cy_as_return_status_t
cy_as_misc_register_callback(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The function to call */
	cy_as_misc_event_callback		callback
	);

/* Summary
   This function sets the logging level for log messages.

   Description
   The API can print messages via the CyAsHalPrintMessage capability.
   This function sets the level of detail seen when printing messages
   from the API.

   * Valid In Asynchronous Callback:NO
*/
EXTERN void
cy_as_misc_set_log_level(
	/* Level to set, 0 is fewer messages, 255 is all */
	uint8_t	level
	);


/* Summary
   This function tells West Bridge that SD or MMC media has been
   inserted or removed.

   Description
   In some hardware configurations, SD or MMC media detection is
   handled outside of the West Bridge device.  This function is called
   when a change is detected to inform the West Bridge firmware to check
   for storage media changes.

   * Valid In Asynchronous Callback: NO

   Returns:
   * CY_AS_ERROR_SUCCESS
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_IN_STANDBY
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_INVALID_RESPONSE

   See Also
   * CyAsMiscStorageChanged

*/
EXTERN cy_as_return_status_t
cy_as_misc_storage_changed(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
	);

/* Summary
   This function instructs the West Bridge firmware to start/stop
   sending periodic heartbeat messages to the processor.

   Description
   The West Bridge firmware can send heartbeat messages through the
   mailbox register once every 500 ms. This message can be an overhead
   as it causes regular Mailbox interrupts to happen, and is turned
   off by default. The message can be used to test and verify that the
   West Bridge firmware is alive. This API can be used to enable or
   disable the heartbeat message.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - the function completed successfully
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED
   * CY_AS_ERROR_NO_FIRMWARE
   * CY_AS_ERROR_OUT_OF_MEMORY
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured yet
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded to
   *	the West Bridge device

*/
EXTERN cy_as_return_status_t
cy_as_misc_heart_beat_control(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Message enable/disable selection */
	cy_bool	enable,
	 /* Callback to call when the operation is complete. */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback. */
	uint32_t			client
		);

/* Summary
   This function gets the current state of a GPIO pin on the
   West Bridge device.

   Description
   The West Bridge device has GPIO pins that can be used for user
   defined functions. This function gets the current state of the
   specified GPIO pin. Calling this function will configure the
   corresponding pin as an input.

   * Valid In Asynchronous Callback: NO

   Notes
   Only GPIO[0], GPIO[1] and UVALID pins can be used as GP inputs.
   Of these pins, only the UVALID pin is supported by firmware images
   that include NAND storage support.

   Returns
   * CY_AS_ERROR_SUCCESS - the function completed successfully
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured yet
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded
   *	to the West Bridge device
   * CY_AS_ERROR_BAD_INDEX - an invalid GPIO was specified
   * CY_AS_ERROR_NOT_SUPPORTED - this feature is not supported
   *	by the firmware

   See Also
   * CyAsMiscGpio
   * CyAsMiscSetGpioValue
 */
EXTERN cy_as_return_status_t
cy_as_misc_get_gpio_value(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Id of the GPIO pin to query */
	cy_as_misc_gpio	pin,
	/* Current value of the GPIO pin */
	uint8_t	*value,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   This function updates the state of a GPIO pin on the West
   Bridge device.

   Description
   The West Bridge device has GPIO pins that can be used for
   user defined functions. This function updates the output
   value driven on a specified GPIO pin. Calling this function
   will configure the corresponding pin as an output.

   * Valid In Asynchronous Callback: NO

   Notes
   All of the pins listed under CyAsMiscGpio can be used as GP
   outputs. This feature is note supported by firmware images
   that include NAND storage device support.

   Returns
   * CY_AS_ERROR_SUCCESS - the function completed successfully
   * CY_AS_ERROR_INVALID_HANDLE
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	been configured yet
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded
   *	to the West Bridge device
   * CY_AS_ERROR_BAD_INDEX - an invalid GPIO was specified
   * CY_AS_ERROR_NOT_SUPPORTED - this feature is not supported
   *	by firmware.

   See Also
   * CyAsMiscGpio
   * CyAsMiscGetGpioValue
 */
EXTERN cy_as_return_status_t
cy_as_misc_set_gpio_value(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Id of the GPIO pin to set */
	cy_as_misc_gpio	pin,
	/* Value to be set on the GPIO pin */
	uint8_t	 value,
	/* Callback to call when the operation is complete. */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   Set the West Bridge device in the low power suspend mode.

   Description
   The West Bridge device has a low power suspend mode where the USB
   core and the internal microcontroller are powered down. This
   function sets the West Bridge device into this low power mode.
   This mode can only be entered when there is no active USB
   connection; i.e., when USB has not been connected or is suspended;
   and there are no pending USB or storage asynchronous calls. The
   device will exit the suspend mode and resume handling USB and
   processor requests when any activity is detected on the CE#, D+/D-
   or GPIO[0] lines.

   * Valid In Asynchronous Callback: NO

   Notes
   The GPIO[0] pin needs to be configured as an input for the gpio
   wakeup to work. This flag should not be enabled if the pin is
   being used as a GP output.

   Returns
   * CY_AS_ERROR_SUCCESS - the device was placed in suspend mode.
   * CY_AS_ERROR_INVALID_HANDLE - the West Bridge handle passed
   *	in is invalid.
   * CY_AS_ERROR_NOT_CONFIGURED - the West Bridge device has not
   *	yet been configured.
   * CY_AS_ERROR_NO_FIRMWARE - no firmware has been downloaded
   *	to the device.
   * CY_AS_ERROR_IN_STANDBY - the device is already in sleep mode.
   * CY_AS_ERROR_USB_CONNECTED - the USB connection is active.
   * CY_AS_ERROR_ASYNC_PENDING - asynchronous storage/USB calls
   *	are pending.
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to allocate memory for
   *	the operation.
   * CY_AS_ERROR_INVALID_RESPONSE - command not recognised by
   *	firmware.

   See Also
   * CyAsMiscLeaveSuspend
 */
EXTERN cy_as_return_status_t
cy_as_misc_enter_suspend(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Control the USB wakeup source */
	cy_bool	usb_wakeup_en,
	/* Control the GPIO[0] wakeup source */
	cy_bool gpio_wakeup_en,
	/* Callback to call when suspend mode entry is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   Wake up the West Bridge device from suspend mode.

   Description
   This call wakes up the West Bridge device from suspend mode,
   and makes it ready for accepting other commands from the API.
   A CyAsEventMiscWakeup event will be delivered to the callback
   registered with CyAsMiscRegisterCallback to indicate that the
   wake up is complete.

   The CyAsEventMiscWakeup event will also be delivered if the
   wakeup happens due to USB or GPIO activity.

   * Valid In Asynchronous Callback: NO

   Returns
   * CY_AS_ERROR_SUCCESS - the device was woken up from
   *	suspend mode.
   * CY_AS_ERROR_INVALID_HANDLE - invalid device handle
   *	passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has
   *	not been configured.
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been
   *	downloaded to the device.
   * CY_AS_ERROR_NOT_IN_SUSPEND - the device is not in
   *	suspend mode.
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to allocate memory
   *	for the operation.
   * CY_AS_ERROR_TIMEOUT - failed to wake up the device.

   See Also
   * CyAsMiscEnterSuspend
 */
EXTERN cy_as_return_status_t
cy_as_misc_leave_suspend(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Callback to call when device has resumed operation. */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   Reserve first numzones zones of nand device for storing
   processor boot image. LNA firmware works  on the first
   numzones zones of nand to enable the processor to boot.

   Description
   This function reserves first numzones zones of nand device
   for storing processor boot image. This fonction MUST be
   completed before starting the storage stack for the setting
   to be taken into account.

   * Valid In Asynchronous Callback: YES

   Returns
   * CY_AS_ERROR_SUCCESS- zones are reserved.

*/
EXTERN cy_as_return_status_t
cy_as_misc_reserve_l_n_a_boot_area(
	/* Handle to the West Bridge device */
	cy_as_device_handle	 handle,
	/* number of nand zones to reserve */
	uint8_t numzones,
	/* Callback to call when device has resumed operation. */
	cy_as_function_callback cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* Summary
   Select the clock frequency to be used when talking to low
   speed (non-high speed) SD media.

   Description
   West Bridge firmware uses a clock frequency less than the
   maximum possible rate for low speed SD media.  This function
   selects the frequency setting from between the default speed
   and the maximum speed. This fonction MUST be completed before
   starting the storage stack for the setting to be taken into
   account.

   * Valid in Asynchronous Callback: Yes (if cb is non-zero)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the operation completed successfully.
   * CY_AS_ERROR_INVALID_HANDLE - invalid device handle passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not been
   *	configured.
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded
   *	to the device.
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to allocate memory for
   *	the operation.
   * CY_AS_ERROR_IN_SUSPEND - West Bridge is in low power suspend
   *	mode.
   * CY_AS_ERROR_INVALID_PARAMETER - invalid frequency setting
   *	desired.
   * CY_AS_ERROR_TIMEOUT - West Bridge device did not respond to
   *	the operation.
   * CY_AS_ERROR_INVALID_RESPONSE - active firmware does not support
   *	the operation.

   See Also
   * CyAsLowSpeedSDFreq
 */
EXTERN cy_as_return_status_t
cy_as_misc_set_low_speed_sd_freq(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Frequency setting desired for low speed SD cards */
	cy_as_low_speed_sd_freq	setting,
	/* Callback to call on completion */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t	client
	);

/* Summary
   Select the clock frequency to be used when talking to high speed
   SD/MMC media.

   Description
   West Bridge firmware uses a 48 MHz clock to interface with high
   speed SD/MMC media.  This clock rate can be restricted to 24 MHz
   if desired.  This function selects the frequency setting to be
   used. This fonction MUST be completed before starting the storage
   stack for the setting to be taken into account.

   * Valid in Asynchronous Callback: Yes (if cb is non-zero)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the operation completed successfully.
   * CY_AS_ERROR_INVALID_HANDLE - invalid device handle passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not been
   *	configured.
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded to
   *	the device.
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to allocate memory for the
   *	operation.
   * CY_AS_ERROR_IN_SUSPEND - West Bridge is in low power suspend mode.
   * CY_AS_ERROR_INVALID_PARAMETER - invalid frequency setting desired.
   * CY_AS_ERROR_TIMEOUT - West Bridge device did not respond to the
   *	operation.
   * CY_AS_ERROR_INVALID_RESPONSE - active firmware does not support
   *	the operation.

   See Also
   * CyAsLowSpeedSDFreq
 */
EXTERN cy_as_return_status_t
cy_as_misc_set_high_speed_sd_freq(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* Frequency setting desired for high speed SD cards */
	cy_as_high_speed_sd_freq setting,
	/* Callback to call on completion */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t client
	);
/* Summary
   Select the polarity of the SD_POW output driven by West Bridge.

   Description
   The SD_POW signal driven by West Bridge can be used to control
   the supply of Vcc to the SD/MMC media connected to the device.
   This signal is driven as an active high signal by default. This
   function can be used to change the polarity of this signal if
   required. This fonction MUST be completed before starting the
   storage stack for the setting to be taken into account.

   * Valid in Asynchronous Callback: Yes (if cb is non-zero)
   * Nestable: Yes

   Returns
   * CY_AS_ERROR_SUCCESS - the operation completed successfully.
   * CY_AS_ERROR_INVALID_HANDLE - invalid device handle passed in.
   * CY_AS_ERROR_NOT_CONFIGURED - West Bridge device has not been
   *	configured.
   * CY_AS_ERROR_NO_FIRMWARE - firmware has not been downloaded
   *	to the device.
   * CY_AS_ERROR_OUT_OF_MEMORY - failed to allocate memory for
   *	the operation.
   * CY_AS_ERROR_IN_SUSPEND - West Bridge is in low power
   *	suspend mode.
   * CY_AS_ERROR_INVALID_PARAMETER - invalid frequency setting
   *	desired.
   * CY_AS_ERROR_TIMEOUT - West Bridge device did not respond to
   *	the operation.
   * CY_AS_ERROR_INVALID_RESPONSE - active firmware does not
   *	support the operation.

   See Also
   * CyAsMiscSignalPolarity
 */
EXTERN cy_as_return_status_t
cy_as_misc_set_sd_power_polarity(
	/* Handle to the West Bridge device */
	cy_as_device_handle handle,
	/* Desired polarity setting to the SD_POW signal. */
	cy_as_misc_signal_polarity polarity,
	/* Callback to call on completion. */
	cy_as_function_callback cb,
	/* Client data to be passed to the callback. */
	uint32_t client
	);

/* For supporting deprecated functions */
#include "cyasmisc_dep.h"

#include "cyas_cplus_end.h"

#endif				/* _INCLUDED_CYASMISC_H_ */
