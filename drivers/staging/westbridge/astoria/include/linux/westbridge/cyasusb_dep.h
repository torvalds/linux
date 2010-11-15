/* Cypress West Bridge API header file (cyasusb_dep.h)
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

/*
 * This header will contain Antioch specific declaration
 * of the APIs that are deprecated in Astoria SDK. This is
 * for maintaining backward compatibility.
 */

#ifndef __INCLUDED_CYASUSB_DEP_H__
#define __INCLUDED_CYASUSB_DEP_H__

#ifndef __doxygen__

/*
   This data structure is the data passed via the evdata
   paramater on a usb event callback for the inquiry request.
*/

typedef struct cy_as_usb_inquiry_data_dep {
	/* The media for the event */
	cy_as_media_type media;
	/* The EVPD bit from the SCSI INQUIRY request */
	uint8_t evpd;
	/* The codepage in the inquiry request */
	uint8_t codepage;
	/* This bool must be set to CyTrue indicate
	 * that the inquiry data was changed */
	cy_bool updated;
	/* The length of the data */
	uint16_t length;
	/* The inquiry data */
	void *data;
} cy_as_usb_inquiry_data_dep;


typedef struct cy_as_usb_unknown_command_data_dep {
	/* The media for the event */
	cy_as_media_type media;
	/* The length of the requst (should be 16 bytes) */
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
} cy_as_usb_unknown_command_data_dep;


typedef struct cy_as_usb_start_stop_data_dep {
	/* The media type for the event */
	cy_as_media_type media;
	/* CyTrue means start request, CyFalse means stop request */
	cy_bool start;
	/* CyTrue means LoEj bit set, otherwise false */
	cy_bool loej;
} cy_as_usb_start_stop_data_dep;


typedef struct cy_as_usb_enum_control_dep {
	/* The bits in this member determine which mass storage devices
	are enumerated.  see cy_as_usb_mass_storage_enum for more details. */
	uint8_t enum_mass_storage;
	/* If true, West Bridge will control enumeration.  If this is false the
	pport controls enumeration.  if the P port is controlling
	enumeration, traffic will be received via endpoint zero. */
	cy_bool antioch_enumeration;
	/* This is the interface # to use for the mass storage interface,
	if mass storage is enumerated.  if mass storage is not enumerated
	this value should be zero. */
	uint8_t mass_storage_interface;
	/* If true, Inquiry, START/STOP, and unknown mass storage
	requests cause a callback to occur for handling by the
	baseband processor. */
	cy_bool mass_storage_callbacks;
} cy_as_usb_enum_control_dep;


typedef void (*cy_as_usb_event_callback_dep)(
	/* Handle to the device to configure */
	cy_as_device_handle			handle,
	/* The event type being reported */
	cy_as_usb_event			ev,
	/* The data assocaited with the event being reported */
	void *evdata
);



/* Register Callback api */
EXTERN cy_as_return_status_t
cy_as_usb_register_callback_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle				handle,
	/* The function to call */
	cy_as_usb_event_callback_dep		callback
	);


extern cy_as_return_status_t
cy_as_usb_set_enum_config_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle			handle,
	/* The USB configuration information */
	cy_as_usb_enum_control_dep *config_p,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t client
	);


extern cy_as_return_status_t
cy_as_usb_get_enum_config_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle			handle,
	/* The return value for USB congifuration information */
	cy_as_usb_enum_control_dep *config_p,
	/* The callback if async call */
	cy_as_function_callback		cb,
	/* Client supplied data */
	uint32_t					client
	);

extern cy_as_return_status_t
cy_as_usb_get_descriptor_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle	handle,
	/* The type of descriptor */
	cy_as_usb_desc_type		type,
	/* Index for string descriptor */
	uint8_t				index,
	/* The buffer to hold the returned descriptor */
	void *desc_p,
	/* This is an input and output parameter.  Before the code this pointer
	points to a uint32_t that contains the length of the buffer.  after
	the call, this value contains the amount of data actually returned. */
	uint32_t *length_p
	);

extern cy_as_return_status_t
cy_as_usb_set_stall_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_usb_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
);

EXTERN cy_as_return_status_t
cy_as_usb_clear_stall_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_usb_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
	);

EXTERN cy_as_return_status_t
cy_as_usb_set_nak_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_usb_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
);

EXTERN cy_as_return_status_t
cy_as_usb_clear_nak_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle		handle,
	/* The endpoint of interest */
	cy_as_end_point_number_t		ep,
	/* The callback if async call */
	cy_as_usb_function_callback		cb,
	/* Client supplied data */
	uint32_t			client
	);

EXTERN cy_as_return_status_t
cy_as_usb_select_m_s_partitions_dep(
		cy_as_device_handle	handle,
		cy_as_media_type	media,
		uint32_t device,
		cy_as_usb_m_s_type_t type,
		cy_as_function_callback	 cb,
		uint32_t client
		);

#endif /*__doxygen*/

#endif /*__INCLUDED_CYANSTORAGE_DEP_H__*/
