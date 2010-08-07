/*  Cypress West Bridge API header file (cyanmisc.h)
 ## Version for backward compatibility with previous Antioch SDK releases.
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

#ifndef _INCLUDED_CYANMISC_H_
#define _INCLUDED_CYANMISC_H_

#include "cyantypes.h"
#include <cyasmisc.h>
#include "cyanmedia.h"
#include "cyas_cplus_start.h"

#define CY_AN_LEAVE_STANDBY_DELAY_CLOCK	\
	(CY_AS_LEAVE_STANDBY_DELAY_CLOCK)
#define CY_AN_RESET_DELAY_CLOCK	\
	(CY_AS_RESET_DELAY_CLOCK)

#define CY_AN_LEAVE_STANDBY_DELAY_CRYSTAL \
	(CY_AS_LEAVE_STANDBY_DELAY_CRYSTAL)

#define CY_AN_RESET_DELAY_CRYSTAL \
	(CY_AS_RESET_DELAY_CRYSTAL)

/* Defines to convert the old CyAn names to the new
 * CyAs names
 */
typedef cy_as_device_handle	cy_an_device_handle;

#define cy_an_device_dack_ack	cy_as_device_dack_ack
#define cy_an_device_dack_eob	cy_as_device_dack_eob
typedef cy_as_device_dack_mode cy_an_device_dack_mode;

typedef cy_as_device_config cy_an_device_config;

#define cy_an_resource_u_s_b	cy_as_bus_u_sB
#define cy_an_resource_sdio_MMC	cy_as_bus_1
#define cy_an_resource_nand	cy_as_bus_0
typedef cy_as_resource_type cy_an_resource_type;

#define cy_an_reset_soft	cy_as_reset_soft
#define cy_an_reset_hard	cy_as_reset_hard
typedef cy_as_reset_type cy_an_reset_type;
typedef cy_as_funct_c_b_type cy_an_funct_c_b_type;
typedef cy_as_function_callback cy_an_function_callback;

#define cy_an_event_misc_initialized \
	cy_as_event_misc_initialized
#define cy_an_event_misc_awake	\
	cy_as_event_misc_awake
#define cy_an_event_misc_heart_beat	 \
	cy_as_event_misc_heart_beat
#define cy_an_event_misc_wakeup	\
	cy_as_event_misc_wakeup
#define cy_an_event_misc_device_mismatch \
	cy_as_event_misc_device_mismatch
typedef cy_as_misc_event_type \
	cy_an_misc_event_type;
typedef cy_as_misc_event_callback \
	cy_an_misc_event_callback;

#define cy_an_misc_gpio_0	cy_as_misc_gpio_0
#define cy_an_misc_gpio_1	cy_as_misc_gpio_1
#define cy_an_misc_gpio__nand_CE \
	cy_as_misc_gpio__nand_CE
#define cy_an_misc_gpio__nand_CE2 \
	cy_as_misc_gpio__nand_CE2
#define cy_an_misc_gpio__nand_WP \
	cy_as_misc_gpio__nand_WP
#define cy_an_misc_gpio__nand_CLE \
	cy_as_misc_gpio__nand_CLE
#define cy_an_misc_gpio__nand_ALE \
	cy_as_misc_gpio__nand_ALE
#define cy_an_misc_gpio_U_valid \
	cy_as_misc_gpio_U_valid
#define cy_an_misc_gpio_SD_POW \
	cy_as_misc_gpio_SD_POW
typedef cy_as_misc_gpio cy_an_misc_gpio;

#define CY_AN_SD_DEFAULT_FREQ CY_AS_SD_DEFAULT_FREQ
#define CY_AN_SD_RATED_FREQ	 CY_AS_SD_RATED_FREQ
typedef cy_as_low_speed_sd_freq cy_an_low_speed_sd_freq;

#define CY_AN_HS_SD_FREQ_48	CY_AS_HS_SD_FREQ_48
#define CY_AN_HS_SD_FREQ_24	CY_AS_HS_SD_FREQ_24
typedef cy_as_high_speed_sd_freq \
	cy_an_high_speed_sd_freq;

#define cy_an_misc_active_high cy_as_misc_active_high
#define cy_an_misc_active_low cy_as_misc_active_low
typedef cy_as_misc_signal_polarity cy_an_misc_signal_polarity;

typedef cy_as_get_firmware_version_data \
	cy_an_get_firmware_version_data;

enum {
	CYAN_FW_TRACE_LOG_NONE = 0,
	CYAN_FW_TRACE_LOG_STATE,
	CYAN_FW_TRACE_LOG_CALLS,
	CYAN_FW_TRACE_LOG_STACK_TRACE,
	CYAN_FW_TRACE_MAX_LEVEL
};


/***********************************/
/***********************************/
/*	FUNCTIONS					*/
/***********************************/
/***********************************/


EXTERN cy_an_return_status_t
cy_an_misc_create_device(
		cy_an_device_handle *handle_p,
		cy_an_hal_device_tag tag
		) ;
#define cy_an_misc_create_device(h, tag) \
	cy_as_misc_create_device((cy_as_device_handle *)(h), \
	(cy_as_hal_device_tag)(tag))

EXTERN cy_an_return_status_t
cy_an_misc_destroy_device(
	cy_an_device_handle  handle
	) ;
#define cy_an_misc_destroy_device(h) \
	cy_as_misc_destroy_device((cy_as_device_handle)(h))

EXTERN cy_an_return_status_t
cy_an_misc_configure_device(
		cy_an_device_handle		handle,
		cy_an_device_config		*config_p
		) ;
#define cy_an_misc_configure_device(h, cfg) \
	cy_as_misc_configure_device((cy_as_device_handle)(h), \
	(cy_as_device_config *)(cfg))

EXTERN cy_an_return_status_t
cy_an_misc_in_standby(
		cy_an_device_handle		handle,
		cy_bool					*standby
		) ;
#define cy_an_misc_in_standby(h, standby) \
	cy_as_misc_in_standby((cy_as_device_handle)(h), (standby))

/* Sync version of Download Firmware */
EXTERN cy_an_return_status_t
cy_an_misc_download_firmware(
		cy_an_device_handle		handle,
		const void			 *fw_p,
		uint16_t			size
		) ;

#define cy_an_misc_download_firmware(handle, fw_p, size) \
	cy_as_misc_download_firmware((cy_as_device_handle)\
	(handle), (fw_p), (size), 0, 0)

/* Async version of Download Firmware */
EXTERN cy_an_return_status_t
cy_an_misc_download_firmware_e_x(
		cy_an_device_handle		handle,
		const void			 *fw_p,
		uint16_t			size,
		cy_an_function_callback		cb,
		uint32_t			client
		) ;

#define cy_an_misc_download_firmware_e_x(h, fw_p, size, cb, client) \
	cy_as_misc_download_firmware((cy_as_device_handle)(h), \
	(fw_p), (size), (cy_as_function_callback)(cb), (client))

/* Sync version of Get Firmware Version */
EXTERN cy_an_return_status_t
cy_as_misc_get_firmware_version_dep(
				cy_as_device_handle handle,
				 uint16_t *major,
				 uint16_t *minor,
				 uint16_t *build,
				 uint8_t *media_type,
				 cy_bool *is_debug_mode);

#define cy_an_misc_get_firmware_version\
	(h, major, minor, bld, type, mode)	\
	cy_as_misc_get_firmware_version_dep((cy_as_device_handle)(h), \
	(major), (minor), (bld), (type), (mode))

/* Async version of Get Firmware Version*/
EXTERN cy_an_return_status_t
cy_an_misc_get_firmware_version_e_x(
		cy_an_device_handle		handle,
		cy_an_get_firmware_version_data *data,
		cy_an_function_callback			cb,
		uint32_t			client
		) ;
#define cy_an_misc_get_firmware_version_e_x\
	(h, data, cb, client) \
	cy_as_misc_get_firmware_version((cy_as_device_handle)(h), \
	(data), (cy_as_function_callback)(cb), (client))

/* Sync version of Read MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_read_m_c_u_register(
	cy_an_device_handle	handle,
	uint16_t			address,
	uint8_t				*value
	) ;

#define cy_an_misc_read_m_c_u_register(handle, address, value) \
	cy_as_misc_read_m_c_u_register((cy_as_device_handle)(handle), \
	(address), (value), 0, 0)

/* Async version of Read MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_read_m_c_u_register_e_x(
	cy_an_device_handle		handle,
	uint16_t			address,
	uint8_t				*value,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;

#define cy_an_misc_read_m_c_u_register_e_x\
	(h, addr, val, cb, client) \
	cy_as_misc_read_m_c_u_register((cy_as_device_handle)(h), \
	(addr), (val), (cy_as_function_callback)(cb), (client))

/* Sync version of Write MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_write_m_c_u_register(
		cy_an_device_handle	handle,
		uint16_t			address,
		uint8_t				mask,
		uint8_t			 value
		) ;
#define cy_an_misc_write_m_c_u_register\
	(handle, address, mask, value) \
	cy_as_misc_write_m_c_u_register((cy_as_device_handle)(handle), \
	(address), (mask), (value), 0, 0)

/* Async version of Write MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_write_m_c_u_register_e_x(
		cy_an_device_handle	 handle,
		uint16_t			 address,
		uint8_t			 mask,
		uint8_t			  value,
		cy_an_function_callback cb,
		uint32_t		 client
		) ;
#define cy_an_misc_write_m_c_u_register_e_x\
	(h, addr, mask, val, cb, client)	  \
	cy_as_misc_write_m_c_u_register((cy_as_device_handle)(h), \
	(addr), (mask), (val), (cy_as_function_callback)(cb), (client))

/* Sync version of Write MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_reset(
	cy_an_device_handle		handle,
	cy_an_reset_type			type,
	cy_bool				flush
	) ;
#define cy_an_misc_reset(handle, type, flush) \
	cy_as_misc_reset((cy_as_device_handle)(handle), \
	(type), (flush), 0, 0)

/* Async version of Write MCU Register*/
EXTERN cy_an_return_status_t
cy_an_misc_reset_e_x(
	cy_an_device_handle	handle,
	cy_an_reset_type		type,
	cy_bool				flush,
	cy_an_function_callback	cb,
	uint32_t		client
	) ;
#define cy_an_misc_reset_e_x(h, type, flush, cb, client) \
	cy_as_misc_reset((cy_as_device_handle)(h), \
	(cy_as_reset_type)(type), (flush), \
	(cy_as_function_callback)(cb), (client))

/*  Synchronous version of CyAnMiscAcquireResource. */
EXTERN cy_an_return_status_t
cy_an_misc_acquire_resource(
	cy_an_device_handle		handle,
	cy_an_resource_type		type,
	cy_bool				force
	) ;
#define cy_an_misc_acquire_resource(h, type, force)		\
	cy_as_misc_acquire_resource_dep((cy_as_device_handle)(h), \
	(cy_as_resource_type)(type), (force))

/* Asynchronous version of CyAnMiscAcquireResource. */
EXTERN cy_an_return_status_t
cy_an_misc_acquire_resource_e_x(
	cy_an_device_handle		handle,
	cy_an_resource_type *type,
	cy_bool				force,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_misc_acquire_resource_e_x\
	(h, type_p, force, cb, client) \
	cy_as_misc_acquire_resource((cy_as_device_handle)(h), \
	(cy_as_resource_type *)(type_p), \
	(force), (cy_as_function_callback)(cb), (client))

/* The one and only version of Release resource */
EXTERN cy_an_return_status_t
cy_an_misc_release_resource(
	cy_an_device_handle		handle,
	cy_an_resource_type		type
	) ;
#define cy_an_misc_release_resource(h, type)\
	cy_as_misc_release_resource((cy_as_device_handle)(h), \
	(cy_as_resource_type)(type))

/* Synchronous version of CyAnMiscSetTraceLevel. */
EXTERN cy_an_return_status_t
cy_an_misc_set_trace_level(
	cy_an_device_handle	handle,
	uint8_t			level,
	cy_an_media_type		media,
	uint32_t		 device,
	uint32_t		unit
	) ;

#define cy_an_misc_set_trace_level\
	(handle, level, media, device, unit) \
	cy_as_misc_set_trace_level_dep((cy_as_device_handle)(handle), \
	(level), (cy_as_media_type)(media), (device), (unit), 0, 0)

/* Asynchronous version of CyAnMiscSetTraceLevel. */
EXTERN cy_an_return_status_t
cy_an_misc_set_trace_level_e_x(
	cy_an_device_handle		handle,
	uint8_t				level,
	cy_an_media_type			media,
	uint32_t			device,
	uint32_t			unit,
	cy_an_function_callback		cb,
	uint32_t			client
	) ;
#define cy_an_misc_set_trace_level_e_x\
	(h, level, media, device, unit, cb, client)	\
	cy_as_misc_set_trace_level_dep((cy_as_device_handle)(h), \
	(level), (cy_as_media_type)(media), (device), (unit),	\
		(cy_as_function_callback)(cb), (client))

/* Synchronous version of CyAnMiscEnterStandby. */
EXTERN cy_an_return_status_t
cy_an_misc_enter_standby(
	cy_an_device_handle	handle,
	cy_bool			pin
	) ;
#define cy_an_misc_enter_standby(handle, pin) \
	cy_as_misc_enter_standby(\
		(cy_as_device_handle)(handle), (pin), 0, 0)

/* Synchronous version of CyAnMiscEnterStandby. */
EXTERN cy_an_return_status_t
cy_an_misc_enter_standby_e_x(
	cy_an_device_handle	handle,
	cy_bool			pin,
	cy_an_function_callback	cb,
	uint32_t		client
	) ;
#define cy_an_misc_enter_standby_e_x(h, pin, cb, client) \
	cy_as_misc_enter_standby((cy_as_device_handle)(h), \
	(pin), (cy_as_function_callback)(cb), (client))

/* Only one version of CyAnMiscLeaveStandby. */
EXTERN cy_an_return_status_t
cy_an_misc_leave_standby(
	cy_an_device_handle		handle,
	cy_an_resource_type		type
	) ;
#define cy_an_misc_leave_standby(h, type)				 \
	cy_as_misc_leave_standby((cy_as_device_handle)(h), \
	(cy_as_resource_type)(type))

/* The one version of Misc Register Callback */
EXTERN cy_an_return_status_t
cy_an_misc_register_callback(
	cy_an_device_handle	handle,
	cy_an_misc_event_callback	callback
	) ;
#define cy_an_misc_register_callback(h, cb)			\
	cy_as_misc_register_callback((cy_as_device_handle)(h), \
	(cy_as_misc_event_callback)(cb))

/* The only version of SetLogLevel */
EXTERN void
cy_an_misc_set_log_level(
	uint8_t	level
	) ;
#define cy_an_misc_set_log_level(level) \
	cy_as_misc_set_log_level(level)

/* Sync version of Misc Storage Changed */
EXTERN cy_an_return_status_t
cy_an_misc_storage_changed(
	cy_an_device_handle		handle
	) ;
#define cy_an_misc_storage_changed(handle) \
	cy_as_misc_storage_changed((cy_as_device_handle)(handle), 0, 0)

/* Async version of Misc Storage Changed */
EXTERN cy_an_return_status_t
cy_an_misc_storage_changed_e_x(
	cy_an_device_handle	handle,
	cy_an_function_callback	cb,
	uint32_t		client
	) ;
#define cy_an_misc_storage_changed_e_x(h, cb, client) \
	cy_as_misc_storage_changed((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of Heartbeat control */
EXTERN cy_an_return_status_t
cy_an_misc_heart_beat_control(
		cy_an_device_handle				handle,
		cy_bool						  enable
		) ;
#define cy_an_misc_heart_beat_control(handle, enable) \
	cy_as_misc_heart_beat_control((cy_as_device_handle)\
	(handle), (enable), 0, 0)

/* Async version of Heartbeat control */
EXTERN cy_an_return_status_t
cy_an_misc_heart_beat_control_e_x(
		cy_an_device_handle		   handle,
		cy_bool					 enable,
		cy_an_function_callback	   cb,
		uint32_t		client
		) ;
#define cy_an_misc_heart_beat_control_e_x(h, enable, cb, client) \
	cy_as_misc_heart_beat_control((cy_as_device_handle)(h), \
	(enable), (cy_as_function_callback)(cb), (client))

/* Sync version of Get Gpio */
EXTERN cy_an_return_status_t
cy_an_misc_get_gpio_value(
		cy_an_device_handle				handle,
		cy_an_misc_gpio					pin,
		uint8_t						*value
		) ;
#define cy_an_misc_get_gpio_value(handle, pin, value) \
	cy_as_misc_get_gpio_value((cy_as_device_handle)(handle), \
	(cy_as_misc_gpio)(pin), (value), 0, 0)

/* Async version of Get Gpio */
EXTERN cy_an_return_status_t
cy_an_misc_get_gpio_value_e_x(
		cy_an_device_handle				handle,
		cy_an_misc_gpio					pin,
		uint8_t						*value,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;
#define cy_an_misc_get_gpio_value_e_x(h, pin, value, cb, client) \
	cy_as_misc_get_gpio_value((cy_as_device_handle)(h), \
	(cy_as_misc_gpio)(pin), (value), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of Set Gpio */
EXTERN cy_an_return_status_t
cy_an_misc_set_gpio_value(
		cy_an_device_handle handle,
		cy_an_misc_gpio	 pin,
		uint8_t		  value
		) ;
#define cy_an_misc_set_gpio_value(handle, pin, value) \
	cy_as_misc_set_gpio_value((cy_as_device_handle)(handle), \
	(cy_as_misc_gpio)(pin), (value), 0, 0)

/* Async version of Set Gpio */
EXTERN cy_an_return_status_t
cy_an_misc_set_gpio_value_e_x(
		cy_an_device_handle				handle,
		cy_an_misc_gpio					pin,
		uint8_t						 value,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;
#define cy_an_misc_set_gpio_value_e_x\
	(h, pin, value, cb, client)	\
	cy_as_misc_set_gpio_value((cy_as_device_handle)(h), \
	(cy_as_misc_gpio)(pin), (value), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of Enter suspend */
EXTERN cy_an_return_status_t
cy_an_misc_enter_suspend(
		cy_an_device_handle	handle,
		cy_bool	usb_wakeup_en,
		cy_bool	gpio_wakeup_en
		) ;
#define cy_an_misc_enter_suspend(handle, usb_wakeup_en, \
	gpio_wakeup_en) \
	cy_as_misc_enter_suspend((cy_as_device_handle)(handle), \
	(usb_wakeup_en), (gpio_wakeup_en), 0, 0)

/* Async version of Enter suspend */
EXTERN cy_an_return_status_t
cy_an_misc_enter_suspend_e_x(
		cy_an_device_handle	handle,
		cy_bool	usb_wakeup_en,
		cy_bool	gpio_wakeup_en,
		cy_an_function_callback	cb,
		uint32_t client
		) ;
#define cy_an_misc_enter_suspend_e_x(h, usb_en, gpio_en, cb, client)\
	cy_as_misc_enter_suspend((cy_as_device_handle)(h), (usb_en), \
	(gpio_en), (cy_as_function_callback)(cb), (client))

/* Sync version of Enter suspend */
EXTERN cy_an_return_status_t
cy_an_misc_leave_suspend(
		cy_an_device_handle				handle
		) ;
#define cy_an_misc_leave_suspend(handle) \
	cy_as_misc_leave_suspend((cy_as_device_handle)(handle), 0, 0)

/* Async version of Enter suspend */
EXTERN cy_an_return_status_t
cy_an_misc_leave_suspend_e_x(
		cy_an_device_handle				handle,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;

#define cy_an_misc_leave_suspend_e_x(h, cb, client)		\
	cy_as_misc_leave_suspend((cy_as_device_handle)(h), \
	(cy_as_function_callback)(cb), (client))

/* Sync version of SetLowSpeedSDFreq */
EXTERN cy_an_return_status_t
cy_an_misc_set_low_speed_sd_freq(
		cy_an_device_handle				handle,
		cy_an_low_speed_sd_freq			  setting
		) ;
#define cy_an_misc_set_low_speed_sd_freq(h, setting)		   \
	cy_as_misc_set_low_speed_sd_freq((cy_as_device_handle)(h), \
	(cy_as_low_speed_sd_freq)(setting), 0, 0)

/* Async version of SetLowSpeedSDFreq */
EXTERN cy_an_return_status_t
cy_an_misc_set_low_speed_sd_freq_e_x(
		cy_an_device_handle				handle,
		cy_an_low_speed_sd_freq			  setting,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;
#define cy_an_misc_set_low_speed_sd_freq_e_x\
(h, setting, cb, client)	\
	cy_as_misc_set_low_speed_sd_freq((cy_as_device_handle)(h), \
	(cy_as_low_speed_sd_freq)(setting),	 \
			(cy_as_function_callback)(cb), (client))

/* SetHighSpeedSDFreq */
EXTERN cy_an_return_status_t
cy_an_misc_set_high_speed_sd_freq(
		cy_an_device_handle				handle,
		cy_an_high_speed_sd_freq			 setting,
		cy_an_function_callback			cb,
		uint32_t						client
		) ;
#define cy_an_misc_set_high_speed_sd_freq(h, setting, cb, client) \
	cy_as_misc_set_high_speed_sd_freq((cy_as_device_handle)(h), \
	(cy_as_high_speed_sd_freq)(setting),   \
			(cy_as_function_callback)(cb), (client))

/* ReserveLNABootArea */
EXTERN cy_an_return_status_t
cy_an_misc_reserve_l_n_a_boot_area(
		cy_an_device_handle handle,
		uint8_t numzones,
		cy_an_function_callback cb,
		uint32_t client);
#define cy_an_misc_reserve_l_n_a_boot_area(h, num, cb, client) \
	cy_as_misc_reserve_l_n_a_boot_area((cy_as_device_handle)(h), \
	num, (cy_as_function_callback)(cb), (client))

/* SetSDPowerPolarity */
EXTERN cy_an_return_status_t
cy_an_misc_set_sd_power_polarity(
		cy_an_device_handle	   handle,
		cy_an_misc_signal_polarity polarity,
		cy_an_function_callback   cb,
		uint32_t			   client);
#define cy_an_misc_set_sd_power_polarity(h, pol, cb, client) \
	cy_as_misc_set_sd_power_polarity((cy_as_device_handle)(h), \
	(cy_as_misc_signal_polarity)(pol),	\
	(cy_as_function_callback)(cb), (client))

#include "cyas_cplus_end.h"

#endif

