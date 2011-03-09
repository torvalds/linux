/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/slab.h>

#include "driver.h"
#include "dumprequest.h"

/*
	Set "dump in progress" flag.
*/
void line6_dump_started(struct line6_dump_request *l6dr, int dest)
{
	l6dr->in_progress = dest;
}

/*
	Invalidate current channel, i.e., set "dump in progress" flag.
	Reading from the "dump" special file blocks until dump is completed.
*/
void line6_invalidate_current(struct line6_dump_request *l6dr)
{
	line6_dump_started(l6dr, LINE6_DUMP_CURRENT);
}

/*
	Clear "dump in progress" flag and notify waiting processes.
*/
void line6_dump_finished(struct line6_dump_request *l6dr)
{
	l6dr->in_progress = LINE6_DUMP_NONE;
	wake_up(&l6dr->wait);
}

/*
	Send an asynchronous channel dump request.
*/
int line6_dump_request_async(struct line6_dump_request *l6dr,
			     struct usb_line6 *line6, int num, int dest)
{
	int ret;
	line6_dump_started(l6dr, dest);
	ret = line6_send_raw_message_async(line6, l6dr->reqbufs[num].buffer,
					   l6dr->reqbufs[num].length);

	if (ret < 0)
		line6_dump_finished(l6dr);

	return ret;
}

/*
	Wait for completion (interruptible).
*/
int line6_dump_wait_interruptible(struct line6_dump_request *l6dr)
{
	return wait_event_interruptible(l6dr->wait,
					l6dr->in_progress == LINE6_DUMP_NONE);
}

/*
	Wait for completion.
*/
void line6_dump_wait(struct line6_dump_request *l6dr)
{
	wait_event(l6dr->wait, l6dr->in_progress == LINE6_DUMP_NONE);
}

/*
	Wait for completion (with timeout).
*/
int line6_dump_wait_timeout(struct line6_dump_request *l6dr, long timeout)
{
	return wait_event_timeout(l6dr->wait,
				  l6dr->in_progress == LINE6_DUMP_NONE,
				  timeout);
}

/*
	Initialize dump request buffer.
*/
int line6_dumpreq_initbuf(struct line6_dump_request *l6dr, const void *buf,
			  size_t len, int num)
{
	l6dr->reqbufs[num].buffer = kmemdup(buf, len, GFP_KERNEL);
	if (l6dr->reqbufs[num].buffer == NULL)
		return -ENOMEM;
	l6dr->reqbufs[num].length = len;
	return 0;
}

/*
	Initialize dump request data structure (including one buffer).
*/
int line6_dumpreq_init(struct line6_dump_request *l6dr, const void *buf,
		       size_t len)
{
	int ret;
	ret = line6_dumpreq_initbuf(l6dr, buf, len, 0);
	if (ret < 0)
		return ret;
	init_waitqueue_head(&l6dr->wait);
	return 0;
}

/*
	Destruct dump request data structure.
*/
void line6_dumpreq_destructbuf(struct line6_dump_request *l6dr, int num)
{
	if (l6dr == NULL)
		return;
	if (l6dr->reqbufs[num].buffer == NULL)
		return;
	kfree(l6dr->reqbufs[num].buffer);
	l6dr->reqbufs[num].buffer = NULL;
}

/*
	Destruct dump request data structure.
*/
void line6_dumpreq_destruct(struct line6_dump_request *l6dr)
{
	if (l6dr->reqbufs[0].buffer == NULL)
		return;
	line6_dumpreq_destructbuf(l6dr, 0);
}
