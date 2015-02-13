/*
 * Initialization and support routines for self-booting compressed image.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: circularbuf.h 452258 2014-01-29 19:17:57Z $
 */

#ifndef __CIRCULARBUF_H_INCLUDED__
#define __CIRCULARBUF_H_INCLUDED__

#include <osl.h>
#include <typedefs.h>
#include <bcmendian.h>

/* Enumerations of return values provided by MsgBuf implementation */
typedef enum {
	CIRCULARBUF_FAILURE = -1,
	CIRCULARBUF_SUCCESS
} circularbuf_ret_t;

/* Core circularbuf circular buffer structure */
typedef struct circularbuf_s
{
	uint16 depth;	/* Depth of circular buffer */
	uint16 r_ptr;	/* Read Ptr */
	uint16 w_ptr;	/* Write Ptr */
	uint16 e_ptr;	/* End Ptr */
	uint16 wp_ptr;	/* wp_ptr/pending - scheduled for DMA. But, not yet complete. */
	uint16 rp_ptr;	/* rp_ptr/pending - scheduled for DMA. But, not yet complete. */

	uint8  *buf_addr;
	void  *mb_ctx;
	void  (*mb_ring_bell)(void *ctx);
} circularbuf_t;

#define CBUF_ERROR_VAL   0x00000001      /* Error level tracing */
#define CBUF_TRACE_VAL   0x00000002      /* Function level tracing */
#define CBUF_INFORM_VAL  0x00000004      /* debug level tracing */

extern int cbuf_msg_level;

#define CBUF_ERROR(args)         do {if (cbuf_msg_level & CBUF_ERROR_VAL) printf args;} while (0)
#define CBUF_TRACE(args)         do {if (cbuf_msg_level & CBUF_TRACE_VAL) printf args;} while (0)
#define CBUF_INFO(args)          do {if (cbuf_msg_level & CBUF_INFORM_VAL) printf args;} while (0)

#define     CIRCULARBUF_START(x)     ((x)->buf_addr)
#define     CIRCULARBUF_WRITE_PTR(x) ((x)->w_ptr)
#define     CIRCULARBUF_READ_PTR(x)  ((x)->r_ptr)
#define     CIRCULARBUF_END_PTR(x)   ((x)->e_ptr)

#define circularbuf_debug_print(handle)                                 \
			CBUF_INFO(("%s:%d:\t%p  rp=%4d  r=%4d  wp=%4d  w=%4d  e=%4d\n", \
					__FUNCTION__, __LINE__,                             \
					(void *) CIRCULARBUF_START(handle),                 \
					(int) (handle)->rp_ptr, (int) (handle)->r_ptr,          \
					(int) (handle)->wp_ptr, (int) (handle)->w_ptr,          \
					(int) (handle)->e_ptr));


/* Callback registered by application/mail-box with the circularbuf implementation.
 * This will be invoked by the circularbuf implementation when write is complete and
 * ready for informing the peer
 */
typedef void (*mb_ring_t)(void *ctx);


/* Public Functions exposed by circularbuf */
void
circularbuf_init(circularbuf_t *handle, void *buf_base_addr, uint16 total_buf_len);
void
circularbuf_register_cb(circularbuf_t *handle, mb_ring_t mb_ring_func, void *ctx);

/* Write Functions */
void *
circularbuf_reserve_for_write(circularbuf_t *handle, uint16 size);
void
circularbuf_write_complete(circularbuf_t *handle, uint16 bytes_written);

/* Read Functions */
void *
circularbuf_get_read_ptr(circularbuf_t *handle, uint16 *avail_len);
circularbuf_ret_t
circularbuf_read_complete(circularbuf_t *handle, uint16 bytes_read);

/*
 * circularbuf_get_read_ptr() updates rp_ptr by the amount that the consumer
 * is supposed to read. The consumer may not read the entire amount.
 * In such a case, circularbuf_revert_rp_ptr() call follows a corresponding
 * circularbuf_get_read_ptr() call to revert the rp_ptr back to
 * the point till which data has actually been processed.
 * It is not valid if it is preceded by multiple get_read_ptr() calls
 */
circularbuf_ret_t
circularbuf_revert_rp_ptr(circularbuf_t *handle, uint16 bytes);

#endif /* __CIRCULARBUF_H_INCLUDED__ */
