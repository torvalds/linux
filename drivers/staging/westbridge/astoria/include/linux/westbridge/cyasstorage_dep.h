/* Cypress West Bridge API header file (cyanstorage_dep.h)
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

/* This header will contain Antioch specific declaration
 * of the APIs that are deprecated in Astoria SDK. This is
 * for maintaining backward compatibility
 */
#ifndef __INCLUDED_CYANSTORAGE_DEP_H__
#define __INCLUDED_CYANSTORAGE_DEP_H__

#ifndef __doxygen__

typedef void (*cy_as_storage_callback_dep)(
/* Handle to the device completing the storage operation */
	cy_as_device_handle handle,
	/* The media type completing the operation */
	cy_as_media_type type,
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
	) ;

typedef void (*cy_as_storage_event_callback_dep)(
	/* Handle to the device sending the event notification */
	cy_as_device_handle handle,
	/* The media type */
	cy_as_media_type type,
	/* The event type */
	cy_as_storage_event evtype,
	/* Event related data */
	void *evdata
	) ;

typedef struct cy_as_storage_query_device_data_dep {
	/* The type of media to query */
	cy_as_media_type	type ;
	/* The logical device number to query */
	uint32_t		device ;
	/* The return value for the device descriptor */
	cy_as_device_desc	 desc_p ;
} cy_as_storage_query_device_data_dep ;

typedef struct cy_as_storage_query_unit_data_dep {
	/* The type of media to query */
	cy_as_media_type	type ;
	/* The logical device number to query */
	uint32_t	device ;
	/* The unit to query on the device */
	uint32_t	unit ;
	/* The return value for the unit descriptor */
	cy_as_unit_desc	 desc_p ;
} cy_as_storage_query_unit_data_dep ;


/************ FUNCTIONS *********************/

EXTERN cy_as_return_status_t
cy_as_storage_register_callback_dep(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The callback function to call for async storage events */
	cy_as_storage_event_callback_dep callback
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_claim_dep(cy_as_device_handle handle,
		   cy_as_media_type type
		   );

EXTERN cy_as_return_status_t
cy_as_storage_claim_dep_EX(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* The type of media to claim */
	cy_as_media_type	*type,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t	client
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_release_dep(cy_as_device_handle handle,
			 cy_as_media_type type
			 );

EXTERN cy_as_return_status_t
cy_as_storage_release_dep_EX(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* Handle to the device of interest */
	cy_as_media_type	*type,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback */
	uint32_t			client
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_query_device_dep(
			cy_as_device_handle handle,
			 cy_as_media_type media,
			 uint32_t device,
			 cy_as_device_desc *desc_p
			 );

EXTERN cy_as_return_status_t
cy_as_storage_query_device_dep_EX(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* Parameters and return value for the query call */
	cy_as_storage_query_device_data_dep *data,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback		cb,
	/* Client data to be passed to the callback */
	uint32_t client
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_query_unit_dep(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The type of media to query */
	cy_as_media_type		type,
	/* The logical device number to query */
	uint32_t			device,
	/* The unit to query on the device */
	uint32_t			unit,
	/* The return value for the unit descriptor */
	cy_as_unit_desc *unit_p
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_query_unit_dep_EX(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* Parameters and return value for the query call */
	cy_as_storage_query_unit_data_dep *data_p,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback	cb,
	/* Client data to be passed to the callback */
	uint32_t client
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_device_control_dep(
	/* Handle to the West Bridge device */
	cy_as_device_handle	   handle,
	/* Enable/disable control for card detection */
	cy_bool				 card_detect_en,
	/* Enable/disable control for write protect handling */
	cy_bool				 write_prot_en,
	/* Callback to be called when the operation is complete */
	cy_as_function_callback   cb,
	/* Client data to be passed to the callback */
	uint32_t			   client
	) ;


EXTERN cy_as_return_status_t
cy_as_storage_read_dep(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The type of media to access */
	cy_as_media_type type,
	/* The device to access */
	uint32_t device,
	/* The unit to access */
	uint32_t unit,
	/* The first block to access */
	uint32_t block,
	/* The buffer where data will be placed */
	void *data_p,
	/* The number of blocks to be read */
	uint16_t			num_blocks
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_read_async_dep(
	/* Handle to the device of interest */
	cy_as_device_handle		handle,
	/* The type of media to access */
	cy_as_media_type	type,
	/* The device to access */
	uint32_t	device,
	/* The unit to access */
	uint32_t	unit,
	/* The first block to access */
	uint32_t		block,
	/* The buffer where data will be placed */
	void *data_p,
	/* The number of blocks to be read */
	uint16_t num_blocks,
	/* The function to call when the read is complete
		or an error occurs */
	cy_as_storage_callback_dep		callback
	) ;
EXTERN cy_as_return_status_t
cy_as_storage_write_dep(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The type of media to access */
	cy_as_media_type type,
	/* The device to access */
	uint32_t	device,
	/* The unit to access */
	uint32_t	unit,
	/* The first block to access */
	uint32_t	block,
	/* The buffer containing the data to be written */
	void	*data_p,
	/* The number of blocks to be written */
	uint16_t num_blocks
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_write_async_dep(
	/* Handle to the device of interest */
	cy_as_device_handle	handle,
	/* The type of media to access */
	cy_as_media_type	type,
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
	cy_as_storage_callback_dep			callback
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_sd_register_read_dep(
		cy_as_device_handle		handle,
		cy_as_media_type		   type,
		uint8_t				 device,
		cy_as_sd_card_reg_type	   reg_type,
		uint8_t				 read_len,
		uint8_t				 *data_p
		);

EXTERN cy_as_return_status_t
cy_as_storage_sd_register_read_dep_EX(
	/* Handle to the West Bridge device. */
	cy_as_device_handle	handle,
	/* The type of media to query */
	cy_as_media_type type,
	/* The device to query */
	uint8_t	device,
	/* The type of register to read. */
	cy_as_sd_card_reg_type	reg_type,
	/* Output data buffer and length. */
	cy_as_storage_sd_reg_read_data	 *data_p,
	/* Callback function to call when done. */
	cy_as_function_callback	cb,
	/* Call context to send to the cb function. */
	uint32_t	client
	) ;

EXTERN cy_as_return_status_t
cy_as_storage_create_p_partition_dep(
		cy_as_device_handle	 handle,
		cy_as_media_type		media,
		uint32_t			 device,
		uint32_t			 size,
		cy_as_function_callback cb,
		uint32_t			 client) ;

EXTERN cy_as_return_status_t
cy_as_storage_remove_p_partition_dep(
		cy_as_device_handle		handle,
		cy_as_media_type		   media,
		uint32_t				device,
		cy_as_function_callback	cb,
		uint32_t				client) ;

#endif /*__doxygen*/

#endif /*__INCLUDED_CYANSTORAGE_DEP_H__*/
