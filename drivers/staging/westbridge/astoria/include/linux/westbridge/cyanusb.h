/*  Cypress West Bridge API header file (cyanusb.h)
 ## Header for backward compatibility with previous Antioch SDK releases.
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

#ifndef _INCLUDED_CYANUSB_H_
#define _INCLUDED_CYANUSB_H_

#if !defined(__doxygen__)

#include "cyanmisc.h"
#include "cyasusb.h"
#include "cyas_cplus_start.h"

#define CY_AN_MAX_USB_DESCRIPTOR_SIZE (CY_AS_MAX_USB_DESCRIPTOR_SIZE)

typedef cy_as_usb_inquiry_data_dep cy_an_usb_inquiry_data;
typedef cy_as_usb_unknown_command_data_dep \
	cy_an_usb_unknown_command_data ;
typedef cy_as_usb_start_stop_data_dep cy_an_usb_start_stop_data ;
typedef cy_as_m_s_c_progress_data cy_an_m_s_c_progress_data ;

#define cy_an_usb_nand_enum cy_as_usb_nand_enum
#define cy_an_usb_sd_enum cy_as_usb_sd_enum
#define cy_an_usb_mmc_enum cy_as_usb_mmc_enum
#define cy_an_usb_ce_ata_enum cy_as_usb_ce_ata_enum
typedef cy_as_usb_mass_storage_enum cy_an_usb_mass_storage_enum;

#define cy_an_usb_desc_device cy_as_usb_desc_device
#define cy_an_usb_desc_device_qual cy_as_usb_desc_device_qual
#define cy_an_usb_desc_f_s_configuration \
	cy_as_usb_desc_f_s_configuration
#define cy_an_usb_desc_h_s_configuration \
	cy_as_usb_desc_h_s_configuration
#define cy_an_usb_desc_string cy_as_usb_desc_string
typedef cy_as_usb_desc_type cy_an_usb_desc_type ;

#define cy_an_usb_in	cy_as_usb_in
#define cy_an_usb_out	cy_as_usb_out
#define cy_an_usb_in_out	cy_as_usb_in_out
typedef cy_as_usb_end_point_dir cy_an_usb_end_point_dir ;


#define cy_an_usb_control cy_as_usb_control
#define cy_an_usb_iso cy_as_usb_iso
#define cy_an_usb_bulk cy_as_usb_bulk
#define cy_an_usb_int cy_as_usb_int
typedef cy_as_usb_end_point_type cy_an_usb_end_point_type ;


typedef cy_as_usb_enum_control_dep cy_an_usb_enum_control ;
typedef cy_as_usb_end_point_config cy_an_usb_end_point_config ;

#define cy_an_usb_m_s_unit0	cy_as_usb_m_s_unit0
#define cy_an_usb_m_s_unit1	cy_as_usb_m_s_unit1
#define cy_an_usb_m_s_both cy_as_usb_m_s_both
typedef cy_as_usb_m_s_type_t cy_an_usb_m_s_type_t ;

#define cy_an_event_usb_suspend	cy_as_event_usb_suspend
#define cy_an_event_usb_resume cy_as_event_usb_resume
#define cy_an_event_usb_reset cy_as_event_usb_reset
#define cy_an_event_usb_set_config cy_as_event_usb_set_config
#define cy_an_event_usb_speed_change cy_as_event_usb_speed_change
#define cy_an_event_usb_setup_packet cy_as_event_usb_setup_packet
#define cy_an_event_usb_status_packet cy_as_event_usb_status_packet
#define cy_an_event_usb_inquiry_before	cy_as_event_usb_inquiry_before
#define cy_an_event_usb_inquiry_after cy_as_event_usb_inquiry_after
#define cy_an_event_usb_start_stop cy_as_event_usb_start_stop
#define cy_an_event_usb_unknown_storage	cy_as_event_usb_unknown_storage
#define cy_an_event_usb_m_s_c_progress cy_as_event_usb_m_s_c_progress
typedef cy_as_usb_event cy_an_usb_event;

typedef cy_as_usb_event_callback_dep cy_an_usb_event_callback ;

typedef cy_as_usb_io_callback cy_an_usb_io_callback;
typedef cy_as_usb_function_callback cy_an_usb_function_callback;

/******* USB Functions ********************/

/* Sync Usb Start */
extern cy_an_return_status_t
cy_an_usb_start(
	cy_an_device_handle		handle
	) ;
#define cy_an_usb_start(handle) \
	cy_as_usb_start((cy_as_device_handle)(handle), 0, 0)

/*Async Usb Start */
extern cy_an_return_status_t
cy_an_usb_start_e_x(
	cy_an_device_handle		handle,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_start_e_x(h, cb, client) \
	cy_as_usb_start((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Sync Usb Stop */
extern cy_an_return_status_t
cy_an_usb_stop(
	cy_an_device_handle		handle
	) ;
#define cy_an_usb_stop(handle) \
	cy_as_usb_stop((cy_as_device_handle)(handle), 0, 0)

/*Async Usb Stop */
extern cy_an_return_status_t
cy_an_usb_stop_e_x(
	cy_an_device_handle		handle,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_stop_e_x(h, cb, client) \
	cy_as_usb_stop((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Register USB event callback */
EXTERN cy_an_return_status_t
cy_an_usb_register_callback(
	cy_an_device_handle		handle,
	cy_an_usb_event_callback		callback
	) ;
#define cy_an_usb_register_callback(h, cb) \
	cy_as_usb_register_callback_dep((cy_as_device_handle)(h), \
	(cy_as_usb_event_callback_dep)(cb))

/*Sync Usb connect */
EXTERN cy_an_return_status_t
cy_an_usb_connect(
	cy_an_device_handle		handle
	) ;
#define cy_an_usb_connect(handle) \
	cy_as_usb_connect((cy_as_device_handle)(handle), 0, 0)

/*Async Usb connect */
extern cy_an_return_status_t
cy_an_usb_connect_e_x(
	cy_an_device_handle		handle,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_connect_e_x(h, cb, client)		\
	cy_as_usb_connect((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/*Sync Usb disconnect */
EXTERN cy_an_return_status_t
cy_an_usb_disconnect(
	cy_an_device_handle		handle
	) ;
#define cy_an_usb_disconnect(handle) \
	cy_as_usb_disconnect((cy_as_device_handle)(handle), 0, 0)

/*Async Usb disconnect */
extern cy_an_return_status_t
cy_an_usb_disconnect_e_x(
	cy_an_device_handle		handle,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_disconnect_e_x(h, cb, client)	\
	cy_as_usb_disconnect((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of set enum config */
EXTERN cy_an_return_status_t
cy_an_usb_set_enum_config(
	cy_an_device_handle	handle,
	cy_an_usb_enum_control *config_p
	) ;
#define cy_an_usb_set_enum_config(handle, config_p) \
	cy_as_usb_set_enum_config_dep((cy_as_device_handle)(handle), \
	(cy_as_usb_enum_control_dep *)(config_p), 0, 0)

/* Async version of set enum config */
extern cy_an_return_status_t
cy_an_usb_set_enum_config_e_x(
	cy_an_device_handle		handle,
	cy_an_usb_enum_control *config_p,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_set_enum_config_e_x(h, config_p, cb, client) \
	cy_as_usb_set_enum_config_dep((cy_as_device_handle)(h), \
	(cy_as_usb_enum_control_dep *)(config_p),	 \
		(cy_as_function_callback)(cb), (client))

/* Sync version of get enum config */
EXTERN cy_an_return_status_t
cy_an_usb_get_enum_config(
	cy_an_device_handle		handle,
	cy_an_usb_enum_control *config_p
	) ;
#define cy_an_usb_get_enum_config(handle, config_p) \
	cy_as_usb_get_enum_config_dep((cy_as_device_handle)(handle), \
	(cy_as_usb_enum_control_dep *)(config_p), 0, 0)

/* Async version of get enum config */
extern cy_an_return_status_t
cy_an_usb_get_enum_config_e_x(
	cy_an_device_handle		handle,
	cy_an_usb_enum_control *config_p,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_get_enum_config_e_x(h, config_p, cb, client) \
	cy_as_usb_get_enum_config_dep((cy_as_device_handle)(h), \
	(cy_as_usb_enum_control_dep *)(config_p),	 \
	(cy_as_function_callback)(cb), (client))

/* Sync Version of Set descriptor */
EXTERN cy_an_return_status_t
cy_an_usb_set_descriptor(
	cy_an_device_handle		handle,
	cy_an_usb_desc_type			type,
	uint8_t				index,
	void *desc_p,
	uint16_t			length
	) ;
#define cy_an_usb_set_descriptor(handle, type, index, desc_p, length) \
	cy_as_usb_set_descriptor((cy_as_device_handle)(handle), \
	(cy_as_usb_desc_type)(type), (index), (desc_p), (length), 0, 0)

/* Async Version of Set descriptor */
extern cy_an_return_status_t
cy_an_usb_set_descriptor_e_x(
	cy_an_device_handle		handle,
	cy_an_usb_desc_type			type,
	uint8_t				index,
	void *desc_p,
	uint16_t			length,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_set_descriptor_e_x\
	(h, type, index, desc_p, length, cb, client) \
	cy_as_usb_set_descriptor((cy_as_device_handle)(h), \
	(cy_as_usb_desc_type)(type), (index), (desc_p), (length), \
		(cy_as_function_callback)(cb), (client))

/* Only version of clear descriptors */
EXTERN cy_an_return_status_t
cy_an_usb_clear_descriptors(
	cy_an_device_handle		handle,
		cy_an_function_callback			cb,
		uint32_t						client
	) ;
#define cy_an_usb_clear_descriptors(h, cb, client) \
	cy_as_usb_clear_descriptors((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of get descriptor*/
EXTERN cy_an_return_status_t
cy_an_usb_get_descriptor(
	cy_an_device_handle	handle,
	cy_an_usb_desc_type		type,
	uint8_t			index,
	void *desc_p,
	uint32_t *length_p
	) ;
#define cy_an_usb_get_descriptor(h, type, index, desc_p, length_p)	\
	cy_as_usb_get_descriptor_dep((cy_as_device_handle)(h), \
	(cy_as_usb_desc_type)(type), (index), (desc_p), (length_p))

typedef cy_as_get_descriptor_data cy_an_get_descriptor_data ;

/* Async version of get descriptor */
extern cy_an_return_status_t
cy_an_usb_get_descriptor_e_x(
	cy_an_device_handle		handle,
	cy_an_usb_desc_type			type,
	uint8_t				index,
	cy_an_get_descriptor_data *data,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_get_descriptor_e_x\
	(h, type, index, data, cb, client) \
	cy_as_usb_get_descriptor((cy_as_device_handle)(h), \
	(cy_as_usb_desc_type)(type), (index), \
	(cy_as_get_descriptor_data *)(data), \
		(cy_as_function_callback)(cb), (client))

EXTERN cy_an_return_status_t
cy_an_usb_set_physical_configuration(
	cy_an_device_handle		handle,
	uint8_t			config
	) ;
#define cy_an_usb_set_physical_configuration(h, config)	\
	cy_as_usb_set_physical_configuration\
	((cy_as_device_handle)(h), (config))

EXTERN cy_an_return_status_t
cy_an_usb_set_end_point_config(
	cy_an_device_handle			handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_end_point_config *config_p
	) ;
#define cy_an_usb_set_end_point_config(h, ep, config_p)	\
	cy_as_usb_set_end_point_config((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_end_point_config *)(config_p))

EXTERN cy_an_return_status_t
cy_an_usb_get_end_point_config(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_end_point_config *config_p
	) ;
#define cy_an_usb_get_end_point_config(h, ep, config_p)	\
	cy_as_usb_get_end_point_config((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_end_point_config *)(config_p))

/* Sync version of commit */
EXTERN cy_an_return_status_t
cy_an_usb_commit_config(
	cy_an_device_handle		handle
	) ;
#define cy_an_usb_commit_config(handle) \
	cy_as_usb_commit_config((cy_as_device_handle)(handle), 0, 0)

/* Async version of commit */
extern cy_an_return_status_t
cy_an_usb_commit_config_e_x(
	cy_an_device_handle		handle,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_commit_config_e_x(h, cb, client)	\
	cy_as_usb_commit_config((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

EXTERN cy_an_return_status_t
cy_an_usb_read_data(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_bool				pktread,
	uint32_t			dsize,
	uint32_t *dataread,
	void *data
	) ;
#define cy_an_usb_read_data(h, ep, pkt, dsize, dataread, data_p) \
	cy_as_usb_read_data((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (pkt), (dsize), \
	(dataread), (data_p))

EXTERN cy_an_return_status_t
cy_an_usb_read_data_async(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_bool				pktread,
	uint32_t			dsize,
	void *data,
	cy_an_usb_io_callback		callback
	) ;
#define cy_an_usb_read_data_async(h, ep, pkt, dsize, data_p, cb) \
	cy_as_usb_read_data_async((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (pkt), (dsize), (data_p), \
		(cy_as_usb_io_callback)(cb))

EXTERN cy_an_return_status_t
cy_an_usb_write_data(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	uint32_t			dsize,
	void *data
	) ;
#define cy_an_usb_write_data(h, ep, dsize, data_p) \
	cy_as_usb_write_data((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (dsize), (data_p))

EXTERN cy_an_return_status_t
cy_an_usb_write_data_async(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	uint32_t			dsize,
	void *data,
	cy_bool				spacket,
	cy_an_usb_io_callback		callback
	) ;
#define cy_an_usb_write_data_async(h, ep, dsize, data_p, spacket, cb) \
	cy_as_usb_write_data_async((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (dsize), (data_p), (spacket), \
		(cy_as_usb_io_callback)(cb))

EXTERN cy_an_return_status_t
cy_an_usb_cancel_async(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep
	) ;
#define cy_an_usb_cancel_async(h, ep) \
	cy_as_usb_cancel_async((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep))

/* Sync version of set stall */
EXTERN cy_an_return_status_t
cy_an_usb_set_stall(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_function_callback		cb,
	uint32_t			client
) ;
#define cy_an_usb_set_stall(h, ep, cb, client)	\
	cy_as_usb_set_stall_dep((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_function_callback)(cb), (client))

/* Async version of set stall */
extern cy_an_return_status_t
cy_an_usb_set_stall_e_x(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_function_callback		cb,
	uint32_t			client
) ;
#define cy_an_usb_set_stall_e_x(h, ep, cb, client)	\
	cy_as_usb_set_stall((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_function_callback)(cb), (client))

/*Sync version of clear stall */
EXTERN cy_an_return_status_t
cy_an_usb_clear_stall(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_clear_stall(h, ep, cb, client)	\
	cy_as_usb_clear_stall_dep((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_function_callback)(cb), (client))

/*Sync version of clear stall */
extern cy_an_return_status_t
cy_an_usb_clear_stall_e_x(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_clear_stall_e_x(h, ep, cb, client) \
	cy_as_usb_clear_stall((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_function_callback)(cb), (client))

/* Sync get stall */
EXTERN cy_an_return_status_t
cy_an_usb_get_stall(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_bool *stall_p
	) ;
#define cy_an_usb_get_stall(handle, ep, stall_p) \
	cy_as_usb_get_stall((cy_as_device_handle)(handle), \
	(cy_as_end_point_number_t)(ep), (stall_p), 0, 0)

/* Async get stall */
extern cy_an_return_status_t
cy_an_usb_get_stall_e_x(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_bool *stall_p,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_get_stall_e_x(h, ep, stall_p, cb, client)	\
	cy_as_usb_get_stall((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (stall_p), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of Set Nak */
EXTERN cy_an_return_status_t
cy_an_usb_set_nak(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_function_callback		cb,
	uint32_t			client
) ;

#define cy_an_usb_set_nak(h, ep, cb, client) \
	cy_as_usb_set_nak_dep((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_function_callback)(cb), (client))

/* Async version of Set Nak */
extern cy_an_return_status_t
cy_an_usb_set_nak_e_x(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_function_callback		cb,
	uint32_t			client
) ;
#define cy_an_usb_set_nak_e_x(h, ep, cb, client) \
	cy_as_usb_set_nak((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of clear nak */
EXTERN cy_an_return_status_t
cy_an_usb_clear_nak(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_usb_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_clear_nak(h, ep, cb, client) \
	cy_as_usb_clear_nak_dep((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_usb_function_callback)(cb), (client))

/* Sync version of clear nak */
extern cy_an_return_status_t
cy_an_usb_clear_nak_e_x(
	cy_an_device_handle		handle,
	cy_an_end_point_number_t		ep,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_usb_clear_nak_e_x(h, ep, cb, client) \
	cy_as_usb_clear_nak((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), \
	(cy_as_function_callback)(cb), (client))

/* Sync Get NAK */
EXTERN cy_an_return_status_t
cy_an_usb_get_nak(
	cy_an_device_handle			handle,
	cy_an_end_point_number_t		ep,
	cy_bool *nak_p
) ;
#define cy_an_usb_get_nak(handle, ep, nak_p) \
	cy_as_usb_get_nak((cy_as_device_handle)(handle), \
	(cy_as_end_point_number_t)(ep), (nak_p), 0, 0)

/* Async Get NAK */
EXTERN cy_an_return_status_t
cy_an_usb_get_nak_e_x(
	cy_an_device_handle			handle,
	cy_an_end_point_number_t		ep,
	cy_bool *nak_p,
	cy_an_function_callback		cb,
	uint32_t				client
) ;
#define cy_an_usb_get_nak_e_x(h, ep, nak_p, cb, client)	\
	cy_as_usb_get_nak((cy_as_device_handle)(h), \
	(cy_as_end_point_number_t)(ep), (nak_p), \
	(cy_as_function_callback)(cb), (client))

/* Sync remote wakup */
EXTERN cy_an_return_status_t
cy_an_usb_signal_remote_wakeup(
		cy_an_device_handle			handle
		) ;
#define cy_an_usb_signal_remote_wakeup(handle) \
	cy_as_usb_signal_remote_wakeup((cy_as_device_handle)(handle), 0, 0)

/* Async remote wakup */
EXTERN cy_an_return_status_t
cy_an_usb_signal_remote_wakeup_e_x(
		cy_an_device_handle			handle,
		cy_an_function_callback		cb,
		uint32_t					client
		) ;
#define cy_an_usb_signal_remote_wakeup_e_x(h, cb, client)	\
	cy_as_usb_signal_remote_wakeup((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Only version of SetMSReportThreshold */
EXTERN cy_an_return_status_t
cy_an_usb_set_m_s_report_threshold(
		cy_an_device_handle			handle,
		uint32_t					wr_sectors,
		uint32_t					rd_sectors,
		cy_an_function_callback		cb,
		uint32_t					client
		) ;
#define cy_an_usb_set_m_s_report_threshold\
	(h, wr_cnt, rd_cnt, cb, client) \
	cy_as_usb_set_m_s_report_threshold((cy_as_device_handle)(h), \
	wr_cnt, rd_cnt, (cy_as_function_callback)(cb), (client))

/* Select storage partitions to be enumerated. */
EXTERN cy_an_return_status_t
cy_an_usb_select_m_s_partitions(
		cy_an_device_handle				handle,
		cy_an_media_type				   media,
		uint32_t						device,
		cy_an_usb_m_s_type_t				 type,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;
#define cy_an_usb_select_m_s_partitions(h, media, dev, type, cb, client) \
	cy_as_usb_select_m_s_partitions_dep((cy_as_device_handle)(h), \
	(cy_as_media_type)(media), (dev),		\
	(cy_as_usb_m_s_type_t)(type), (cy_as_function_callback)(cb), (client))

#include "cyas_cplus_end.h"
#endif /*__doxygen__*/
#endif	/*_INCLUDED_CYANUSB_H_*/
