/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef DUMPREQUEST_H
#define DUMPREQUEST_H


#include <linux/usb.h>
#include <linux/wait.h>

#include <sound/core.h>


enum {
	LINE6_DUMP_NONE,
	LINE6_DUMP_CURRENT
};


struct line6_dump_reqbuf {
	/**
		 Buffer for dump requests.
	*/
	unsigned char *buffer;

	/**
		 Size of dump request.
	*/
	size_t length;
};

/**
	 Provides the functionality to request channel/model/... dump data from a
	 Line6 device.
*/
struct line6_dump_request {
	/**
		 Wait queue for access to program dump data.
	*/
	wait_queue_head_t wait;

	/**
		 Indicates an unfinished program dump request.
		 0: no dump
		 1: dump current settings
		 Other device-specific values are also allowed.
	*/
	int in_progress;

	/**
		 Timer for delayed dump request.
	*/
	struct timer_list timer;

	/**
		 Flag if initial dump request has been successful.
	*/
	char ok;

	/**
		 Dump request buffers
	*/
	struct line6_dump_reqbuf reqbufs[1];
};

extern void line6_dump_finished(struct line6_dump_request *l6dr);
extern int line6_dump_request_async(struct line6_dump_request *l6dr,
				    struct usb_line6 *line6, int num);
extern void line6_dump_started(struct line6_dump_request *l6dr, int dest);
extern void line6_dumpreq_destruct(struct line6_dump_request *l6dr);
extern void line6_dumpreq_destructbuf(struct line6_dump_request *l6dr, int num);
extern int line6_dumpreq_init(struct line6_dump_request *l6dr, const void *buf,
			      size_t len);
extern int line6_dumpreq_initbuf(struct line6_dump_request *l6dr,
				 const void *buf, size_t len, int num);
extern void line6_invalidate_current(struct line6_dump_request *l6dr);
extern void line6_startup_delayed(struct line6_dump_request *l6dr, int seconds,
				  void (*function)(unsigned long), void *data);
extern int line6_wait_dump(struct line6_dump_request *l6dr, int nonblock);


#endif
