/*
 *
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef __PVRUSB2_IO_H
#define __PVRUSB2_IO_H

#include <linux/usb.h>
#include <linux/list.h>

typedef void (*pvr2_stream_callback)(void *);

enum pvr2_buffer_state {
	pvr2_buffer_state_none = 0,   // Not on any list
	pvr2_buffer_state_idle = 1,   // Buffer is ready to be used again
	pvr2_buffer_state_queued = 2, // Buffer has been queued for filling
	pvr2_buffer_state_ready = 3,  // Buffer has data available
};

struct pvr2_stream;
struct pvr2_buffer;

struct pvr2_stream_stats {
	unsigned int buffers_in_queue;
	unsigned int buffers_in_idle;
	unsigned int buffers_in_ready;
	unsigned int buffers_processed;
	unsigned int buffers_failed;
	unsigned int bytes_processed;
};

/* Initialize / tear down stream structure */
struct pvr2_stream *pvr2_stream_create(void);
void pvr2_stream_destroy(struct pvr2_stream *);
void pvr2_stream_setup(struct pvr2_stream *,
		       struct usb_device *dev,int endpoint,
		       unsigned int tolerance);
void pvr2_stream_set_callback(struct pvr2_stream *,
			      pvr2_stream_callback func,
			      void *data);
void pvr2_stream_get_stats(struct pvr2_stream *,
			   struct pvr2_stream_stats *,
			   int zero_counts);

/* Query / set the nominal buffer count */
int pvr2_stream_get_buffer_count(struct pvr2_stream *);
int pvr2_stream_set_buffer_count(struct pvr2_stream *,unsigned int);

/* Get a pointer to a buffer that is either idle, ready, or is specified
   named. */
struct pvr2_buffer *pvr2_stream_get_idle_buffer(struct pvr2_stream *);
struct pvr2_buffer *pvr2_stream_get_ready_buffer(struct pvr2_stream *);
struct pvr2_buffer *pvr2_stream_get_buffer(struct pvr2_stream *sp,int id);

/* Find out how many buffers are idle or ready */
int pvr2_stream_get_ready_count(struct pvr2_stream *);


/* Kill all pending buffers and throw away any ready buffers as well */
void pvr2_stream_kill(struct pvr2_stream *);

/* Set up the actual storage for a buffer */
int pvr2_buffer_set_buffer(struct pvr2_buffer *,void *ptr,unsigned int cnt);

/* Find out size of data in the given ready buffer */
unsigned int pvr2_buffer_get_count(struct pvr2_buffer *);

/* Retrieve completion code for given ready buffer */
int pvr2_buffer_get_status(struct pvr2_buffer *);

/* Retrieve ID of given buffer */
int pvr2_buffer_get_id(struct pvr2_buffer *);

/* Start reading into given buffer (kill it if needed) */
int pvr2_buffer_queue(struct pvr2_buffer *);

#endif /* __PVRUSB2_IO_H */

/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
