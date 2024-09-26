/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * u_serial.h - interface to USB gadget "serial port"/TTY utilities
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 */

#ifndef __U_SERIAL_H
#define __U_SERIAL_H

#include <linux/usb/composite.h>
#include <linux/usb/cdc.h>

#define MAX_U_SERIAL_PORTS	8

struct f_serial_opts {
	struct usb_function_instance func_inst;
	u8 port_num;
	u8 protocol;

	struct mutex lock; /* protect instances */
	int instances;
};

/*
 * One non-multiplexed "serial" I/O port ... there can be several of these
 * on any given USB peripheral device, if it provides enough endpoints.
 *
 * The "u_serial" utility component exists to do one thing:  manage TTY
 * style I/O using the USB peripheral endpoints listed here, including
 * hookups to sysfs and /dev for each logical "tty" device.
 *
 * REVISIT at least ACM could support tiocmget() if needed.
 *
 * REVISIT someday, allow multiplexing several TTYs over these endpoints.
 */
struct gserial {
	struct usb_function		func;

	/* port is managed by gserial_{connect,disconnect} */
	struct gs_port			*ioport;

	struct usb_ep			*in;
	struct usb_ep			*out;

	/* REVISIT avoid this CDC-ACM support harder ... */
	struct usb_cdc_line_coding port_line_coding;	/* 9600-8-N-1 etc */

	/* notification callbacks */
	void (*connect)(struct gserial *p);
	void (*disconnect)(struct gserial *p);
	int (*send_break)(struct gserial *p, int duration);
};

/* utilities to allocate/free request and buffer */
struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned len, gfp_t flags);
void gs_free_req(struct usb_ep *, struct usb_request *req);

/* management of individual TTY ports */
int gserial_alloc_line_no_console(unsigned char *port_line);
int gserial_alloc_line(unsigned char *port_line);
void gserial_free_line(unsigned char port_line);

#ifdef CONFIG_U_SERIAL_CONSOLE

ssize_t gserial_set_console(unsigned char port_num, const char *page, size_t count);
ssize_t gserial_get_console(unsigned char port_num, char *page);

#endif /* CONFIG_U_SERIAL_CONSOLE */

/* connect/disconnect is handled by individual functions */
int gserial_connect(struct gserial *, u8 port_num);
void gserial_disconnect(struct gserial *);
void gserial_suspend(struct gserial *p);
void gserial_resume(struct gserial *p);

#endif /* __U_SERIAL_H */
