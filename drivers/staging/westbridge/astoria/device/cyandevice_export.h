/*
## cyandevice_export.h - Linux Antioch device driver file
##
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
 * Export Misc APIs that can be used from the other driver modules.
 * The APIs to create a device handle and download firmware are not exported
 * because they are expected to be used only by this kernel module.
 */
EXPORT_SYMBOL(cy_as_misc_get_firmware_version);
EXPORT_SYMBOL(cy_as_misc_read_m_c_u_register);
EXPORT_SYMBOL(cy_as_misc_reset);
EXPORT_SYMBOL(cy_as_misc_acquire_resource);
EXPORT_SYMBOL(cy_as_misc_release_resource);
EXPORT_SYMBOL(cy_as_misc_enter_standby);
EXPORT_SYMBOL(cy_as_misc_leave_standby);
EXPORT_SYMBOL(cy_as_misc_enter_suspend);
EXPORT_SYMBOL(cy_as_misc_leave_suspend);
EXPORT_SYMBOL(cy_as_misc_storage_changed);
EXPORT_SYMBOL(cy_as_misc_heart_beat_control);
EXPORT_SYMBOL(cy_as_misc_get_gpio_value);
EXPORT_SYMBOL(cy_as_misc_set_gpio_value);
EXPORT_SYMBOL(cy_as_misc_set_low_speed_sd_freq);
EXPORT_SYMBOL(cy_as_misc_set_high_speed_sd_freq);

/*
 * Export the USB APIs that can be used by the dependent kernel modules.
 */
EXPORT_SYMBOL(cy_as_usb_set_end_point_config);
EXPORT_SYMBOL(cy_as_usb_read_data_async);
EXPORT_SYMBOL(cy_as_usb_write_data_async);
EXPORT_SYMBOL(cy_as_usb_cancel_async);
EXPORT_SYMBOL(cy_as_usb_set_stall);
EXPORT_SYMBOL(cy_as_usb_clear_stall);
EXPORT_SYMBOL(cy_as_usb_connect);
EXPORT_SYMBOL(cy_as_usb_disconnect);
EXPORT_SYMBOL(cy_as_usb_start);
EXPORT_SYMBOL(cy_as_usb_stop);
EXPORT_SYMBOL(cy_as_usb_set_enum_config);
EXPORT_SYMBOL(cy_as_usb_get_enum_config);
EXPORT_SYMBOL(cy_as_usb_set_physical_configuration);
EXPORT_SYMBOL(cy_as_usb_register_callback);
EXPORT_SYMBOL(cy_as_usb_commit_config);
EXPORT_SYMBOL(cy_as_usb_set_descriptor);
EXPORT_SYMBOL(cy_as_usb_clear_descriptors);
EXPORT_SYMBOL(cy_as_usb_get_descriptor);
EXPORT_SYMBOL(cy_as_usb_get_end_point_config);
EXPORT_SYMBOL(cy_as_usb_read_data);
EXPORT_SYMBOL(cy_as_usb_write_data);
EXPORT_SYMBOL(cy_as_usb_get_stall);
EXPORT_SYMBOL(cy_as_usb_set_nak);
EXPORT_SYMBOL(cy_as_usb_clear_nak);
EXPORT_SYMBOL(cy_as_usb_get_nak);
EXPORT_SYMBOL(cy_as_usb_signal_remote_wakeup);
EXPORT_SYMBOL(cy_as_usb_set_m_s_report_threshold);
EXPORT_SYMBOL(cy_as_usb_select_m_s_partitions);

/*
 * Export all Storage APIs that can be used by dependent kernel modules.
 */
EXPORT_SYMBOL(cy_as_storage_start);
EXPORT_SYMBOL(cy_as_storage_stop);
EXPORT_SYMBOL(cy_as_storage_register_callback);
EXPORT_SYMBOL(cy_as_storage_query_bus);
EXPORT_SYMBOL(cy_as_storage_query_media);
EXPORT_SYMBOL(cy_as_storage_query_device);
EXPORT_SYMBOL(cy_as_storage_query_unit);
EXPORT_SYMBOL(cy_as_storage_device_control);
EXPORT_SYMBOL(cy_as_storage_claim);
EXPORT_SYMBOL(cy_as_storage_release);
EXPORT_SYMBOL(cy_as_storage_read);
EXPORT_SYMBOL(cy_as_storage_write);
EXPORT_SYMBOL(cy_as_storage_read_async);
EXPORT_SYMBOL(cy_as_storage_write_async);
EXPORT_SYMBOL(cy_as_storage_cancel_async);
EXPORT_SYMBOL(cy_as_storage_sd_register_read);
EXPORT_SYMBOL(cy_as_storage_create_p_partition);
EXPORT_SYMBOL(cy_as_storage_remove_p_partition);
EXPORT_SYMBOL(cy_as_storage_get_transfer_amount);
EXPORT_SYMBOL(cy_as_storage_erase);

EXPORT_SYMBOL(cy_as_sdio_query_card);
EXPORT_SYMBOL(cy_as_sdio_init_function);
EXPORT_SYMBOL(cy_as_sdio_set_blocksize);
EXPORT_SYMBOL(cy_as_sdio_direct_read);
EXPORT_SYMBOL(cy_as_sdio_direct_write);
EXPORT_SYMBOL(cy_as_sdio_extended_read);
EXPORT_SYMBOL(cy_as_sdio_extended_write);

EXPORT_SYMBOL(cy_as_hal_alloc);
EXPORT_SYMBOL(cy_as_hal_free);
EXPORT_SYMBOL(cy_as_hal_sleep);
EXPORT_SYMBOL(cy_as_hal_create_sleep_channel);
EXPORT_SYMBOL(cy_as_hal_destroy_sleep_channel);
EXPORT_SYMBOL(cy_as_hal_sleep_on);
EXPORT_SYMBOL(cy_as_hal_wake);
EXPORT_SYMBOL(cy_as_hal_mem_set);

EXPORT_SYMBOL(cy_as_mtp_storage_only_start);
EXPORT_SYMBOL(cy_as_mtp_storage_only_stop);
EXPORT_SYMBOL(cy_as_mtp_start);
EXPORT_SYMBOL(cy_as_mtp_init_send_object);
EXPORT_SYMBOL(cy_as_mtp_init_get_object);
EXPORT_SYMBOL(cy_as_mtp_cancel_send_object);
EXPORT_SYMBOL(cy_as_mtp_cancel_get_object);

#ifdef __CY_ASTORIA_SCM_KERNEL_HAL__
/* Functions in the SCM kernel HAL implementation only. */
EXPORT_SYMBOL(cy_as_hal_enable_scatter_list);
EXPORT_SYMBOL(cy_as_hal_disable_scatter_list);
#endif

/*[]*/
