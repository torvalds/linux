/* The industrial I/O simple minimally locked ring buffer.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This code is deliberately kept separate from the main industrialio I/O core
 * as it is intended that in the future a number of different software ring
 * buffer implementations will exist with different characteristics to suit
 * different applications.
 *
 * This particular one was designed for a data capture application where it was
 * particularly important that no userspace reads would interrupt the capture
 * process. To this end the ring is not locked during a read.
 *
 * Comments on this buffer design welcomed. It's far from efficient and some of
 * my understanding of the effects of scheduling on this are somewhat limited.
 * Frankly, to my mind, this is the current weak point in the industrial I/O
 * patch set.
 */

#ifndef _IIO_RING_SW_H_
#define _IIO_RING_SW_H_
/* NEEDS COMMENTS */
/* The intention is that this should be a separate module from the iio core.
 * This is a bit like supporting algorithms dependent on what the device
 * driver requests - some may support multiple options */


#include "iio.h"
#include "ring_generic.h"

#if defined CONFIG_IIO_SW_RING || defined CONFIG_IIO_SW_RING_MODULE

/**
 * iio_create_sw_rb() - software ring buffer allocation
 * @r:		pointer to ring buffer pointer
 **/
int iio_create_sw_rb(struct iio_ring_buffer **r);

/**
 * iio_init_sw_rb() - initialize the software ring buffer
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 * @indio_dev:		industrial I/O device structure
 **/
int iio_init_sw_rb(struct iio_ring_buffer *r, struct iio_dev *indio_dev);

/**
 * iio_exit_sw_rb() - reverse what was done in iio_init_sw_rb
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
void iio_exit_sw_rb(struct iio_ring_buffer *r);

/**
 * iio_free_sw_rb() - free memory occupied by the core ring buffer struct
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
void iio_free_sw_rb(struct iio_ring_buffer *r);

/**
 * iio_mark_sw_rb_in_use() - reference counting to prevent incorrect chances
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
void iio_mark_sw_rb_in_use(struct iio_ring_buffer *r);

/**
 *  iio_unmark_sw_rb_in_use() - notify the ring buffer that we don't care anymore
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
void iio_unmark_sw_rb_in_use(struct iio_ring_buffer *r);

/**
 * iio_read_last_from_sw_rb() - attempt to read the last stored datum from the rb
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 * @data:	where to store the last datum
 **/
int iio_read_last_from_sw_rb(struct iio_ring_buffer *r, u8 *data);

/**
 * iio_store_to_sw_rb() - store a new datum to the ring buffer
 * @r:		pointer to ring buffer instance
 * @data:	the datum to be stored including timestamp if relevant
 * @timestamp:	timestamp which will be attached to buffer events if relevant
 **/
int iio_store_to_sw_rb(struct iio_ring_buffer *r, u8 *data, s64 timestamp);

/**
 * iio_rip_sw_rb() - attempt to read data from the ring buffer
 * @r:			ring buffer instance
 * @count:		number of datum's to try and read
 * @data:		where the data will be stored.
 * @dead_offset:	how much of the stored data was possibly invalidated by
 *			the end of the copy.
 **/
int iio_rip_sw_rb(struct iio_ring_buffer *r,
		  size_t count,
		  u8 **data,
		  int *dead_offset);

/**
 * iio_request_update_sw_rb() - update params if update needed
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
int iio_request_update_sw_rb(struct iio_ring_buffer *r);

/**
 * iio_mark_update_needed_sw_rb() - tell the ring buffer it needs a param update
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
int iio_mark_update_needed_sw_rb(struct iio_ring_buffer *r);


/**
 * iio_get_bytes_per_datum_sw_rb() - get the datum size in bytes
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
int iio_get_bytes_per_datum_sw_rb(struct iio_ring_buffer *r);

/**
 * iio_set_bytes_per_datum_sw_rb() - set the datum size in bytes
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 * @bpd:	bytes per datum value
 **/
int iio_set_bytes_per_datum_sw_rb(struct iio_ring_buffer *r, size_t bpd);

/**
 * iio_get_length_sw_rb() - get how many datums the rb may contain
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 **/
int iio_get_length_sw_rb(struct iio_ring_buffer *r);

/**
 * iio_set_length_sw_rb() - set how many datums the rb may contain
 * @r:		pointer to a software ring buffer created by an
 *		iio_create_sw_rb call
 * @length:	max number of data items for the ring buffer
 **/
int iio_set_length_sw_rb(struct iio_ring_buffer *r, int length);

/**
 * iio_ring_sw_register_funcs() - helper function to set up rb access
 * @ra:		pointer to @iio_ring_access_funcs
 **/
static inline void iio_ring_sw_register_funcs(struct iio_ring_access_funcs *ra)
{
	ra->mark_in_use = &iio_mark_sw_rb_in_use;
	ra->unmark_in_use = &iio_unmark_sw_rb_in_use;

	ra->store_to = &iio_store_to_sw_rb;
	ra->read_last = &iio_read_last_from_sw_rb;
	ra->rip_lots = &iio_rip_sw_rb;

	ra->mark_param_change = &iio_mark_update_needed_sw_rb;
	ra->request_update = &iio_request_update_sw_rb;

	ra->get_bytes_per_datum = &iio_get_bytes_per_datum_sw_rb;
	ra->set_bytes_per_datum = &iio_set_bytes_per_datum_sw_rb;

	ra->get_length = &iio_get_length_sw_rb;
	ra->set_length = &iio_set_length_sw_rb;
};

/**
 * struct iio_sw_ring_buffer - software ring buffer
 * @buf:		generic ring buffer elements
 * @data:		the ring buffer memory
 * @read_p:		read pointer (oldest available)
 * @write_p:		write pointer
 * @last_written_p:	read pointer (newest available)
 * @half_p:		half buffer length behind write_p (event generation)
 * @use_count:		reference count to prevent resizing when in use
 * @update_needed:	flag to indicated change in size requested
 * @use_lock:		lock to prevent change in size when in use
 *
 * Note that the first element of all ring buffers must be a
 * struct iio_ring_buffer.
**/

struct iio_sw_ring_buffer {
	struct iio_ring_buffer  buf;
	unsigned char		*data;
	unsigned char		*read_p;
	unsigned char		*write_p;
	unsigned char		*last_written_p;
	/* used to act as a point at which to signal an event */
	unsigned char		*half_p;
	int			use_count;
	int			update_needed;
	spinlock_t		use_lock;
};

#define iio_to_sw_ring(r) container_of(r, struct iio_sw_ring_buffer, buf)

struct iio_ring_buffer *iio_sw_rb_allocate(struct iio_dev *indio_dev);
void iio_sw_rb_free(struct iio_ring_buffer *ring);

int iio_sw_ring_preenable(struct iio_dev *indio_dev);

struct iio_sw_ring_helper_state {
	struct work_struct		work_trigger_to_ring;
	struct iio_dev			*indio_dev;
	int (*get_ring_element)(struct iio_sw_ring_helper_state *st, u8 *buf);
	s64				last_timestamp;
};

void iio_sw_poll_func_th(struct iio_dev *indio_dev, s64 time);
void iio_sw_trigger_bh_to_ring(struct work_struct *work_s);

#else /* CONFIG_IIO_RING_BUFFER*/
struct iio_sw_ring_helper_state {
	struct iio_dev			*indio_dev;
};
#endif /* !CONFIG_IIO_RING_BUFFER */
#endif /* _IIO_RING_SW_H_ */
