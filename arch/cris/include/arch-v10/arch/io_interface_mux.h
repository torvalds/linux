/* SPDX-License-Identifier: GPL-2.0 */
/* IO interface mux allocator for ETRAX100LX.
 * Copyright 2004, Axis Communications AB
 * $Id: io_interface_mux.h,v 1.1 2004/12/13 12:21:53 starvik Exp $
 */


#ifndef _IO_INTERFACE_MUX_H
#define _IO_INTERFACE_MUX_H


/* C.f. ETRAX100LX Designer's Reference 20.9 */

/* The order in enum must match the order of interfaces[] in
 * io_interface_mux.c */
enum cris_io_interface {
	/* Begin Non-multiplexed interfaces */
	if_eth = 0,
	if_serial_0,
	/* End Non-multiplexed interfaces */
	if_serial_1,
	if_serial_2,
	if_serial_3,
	if_sync_serial_1,
	if_sync_serial_3,
	if_shared_ram,
	if_shared_ram_w,
	if_par_0,
	if_par_1,
	if_par_w,
	if_scsi8_0,
	if_scsi8_1,
	if_scsi_w,
	if_ata,
	if_csp,
	if_i2c,
	if_usb_1,
	if_usb_2,
	/* GPIO pins */
	if_gpio_grp_a,
	if_gpio_grp_b,
	if_gpio_grp_c,
	if_gpio_grp_d,
	if_gpio_grp_e,
	if_gpio_grp_f,
	if_max_interfaces,
	if_unclaimed
};

int cris_request_io_interface(enum cris_io_interface ioif, const char *device_id);

void cris_free_io_interface(enum cris_io_interface ioif);

/* port can be 'a', 'b' or 'g' */
int cris_io_interface_allocate_pins(const enum cris_io_interface ioif,
				    const char port,
				    const unsigned start_bit,
				    const unsigned stop_bit);

/* port can be 'a', 'b' or 'g' */
int cris_io_interface_free_pins(const enum cris_io_interface ioif,
                                const char port,
                                const unsigned start_bit,
                                const unsigned stop_bit);

int cris_io_interface_register_watcher(void (*notify)(const unsigned int gpio_in_available,
						      const unsigned int gpio_out_available,
						      const unsigned char pa_available,
						      const unsigned char pb_available));

void cris_io_interface_delete_watcher(void (*notify)(const unsigned int gpio_in_available,
						     const unsigned int gpio_out_available,
                                                     const unsigned char pa_available,
						     const unsigned char pb_available));

#endif /* _IO_INTERFACE_MUX_H */
