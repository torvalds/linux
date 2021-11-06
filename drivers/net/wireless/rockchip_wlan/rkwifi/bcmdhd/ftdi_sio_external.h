/*
 * External driver API to ftdi_sio_brcm driver.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: $
 */

typedef struct usb_serial_port * gpio_handle;

#define BITMODE_RESET 0x00
#define BITMODE_BITBANG 0x01

int ftdi_usb_reset(int handle);
int ftdi_set_bitmode(int handle, unsigned char bitmask, unsigned char mode);
int gpio_write_port(int handle, unsigned char pins);
int gpio_write_port_non_block(int handle, unsigned char pins);
int gpio_read_port(int handle, unsigned char *pins);
int handle_add(gpio_handle pointer);
int handle_remove(gpio_handle pointer);
int get_handle(const char *dev_filename);
gpio_handle get_pointer_by_handle(int handle);
