/*
 * Initialization and support routines for self-booting compressed image.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: circularbuf.c 452261 2014-01-29 19:30:23Z $
 */

#include <circularbuf.h>
#include <bcmmsgbuf.h>
#include <osl.h>

#define CIRCULARBUF_READ_SPACE_AT_END(x)		\
			((x->w_ptr >= x->rp_ptr) ? (x->w_ptr - x->rp_ptr) : (x->e_ptr - x->rp_ptr))

#define CIRCULARBUF_READ_SPACE_AVAIL(x)		\
			(((CIRCULARBUF_READ_SPACE_AT_END(x) == 0) && (x->w_ptr < x->rp_ptr)) ? \
				x->w_ptr : CIRCULARBUF_READ_SPACE_AT_END(x))

int cbuf_msg_level = CBUF_ERROR_VAL | CBUF_TRACE_VAL | CBUF_INFORM_VAL;

/* #define CBUF_DEBUG */
#ifdef CBUF_DEBUG
#define CBUF_DEBUG_CHECK(x)	x
#else
#define CBUF_DEBUG_CHECK(x)
#endif	/* CBUF_DEBUG */

/*
 * -----------------------------------------------------------------------------
 * Function   : circularbuf_init
 * Description:
 *
 *
 * Input Args :
 *
 *
 * Return Values :
 *
 * -----------------------------------------------------------------------------
 */
void
circularbuf_init(circularbuf_t *handle, void *buf_base_addr, uint16 total_buf_len)
{
	handle->buf_addr = buf_base_addr;

	handle->depth = handle->e_ptr = HTOL32(total_buf_len);

	/* Initialize Read and Write pointers */
	handle->w_ptr = handle->r_ptr = handle->wp_ptr = handle->rp_ptr = HTOL32(0);
	handle->mb_ring_bell = NULL;
	handle->mb_ctx = NULL;

	return;
}

void
circularbuf_register_cb(circularbuf_t *handle, mb_ring_t mb_ring_func, void *ctx)
{
	handle->mb_ring_bell = mb_ring_func;
	handle->mb_ctx = ctx;
}

#ifdef CBUF_DEBUG
static void
circularbuf_check_sanity(circularbuf_t *handle)
{
	if ((handle->e_ptr > handle->depth) ||
	    (handle->r_ptr > handle->e_ptr) ||
		(handle->rp_ptr > handle->e_ptr) ||
		(handle->w_ptr > handle->e_ptr))
	{
		printf("%s:%d: Pointers are corrupted.\n", __FUNCTION__, __LINE__);
		circularbuf_debug_print(handle);
		ASSERT(0);
	}
	return;
}
#endif /* CBUF_DEBUG */

/*
 * -----------------------------------------------------------------------------
 * Function   : circularbuf_reserve_for_write
 *
 * Description:
 * This function reserves N bytes for write in the circular buffer. The circularbuf
 * implementation will only reserve space in the ciruclar buffer and return
 * the pointer to the address where the new data can be written.
 * The actual write implementation (bcopy/dma) is outside the scope of
 * circularbuf implementation.
 *
 * Input Args :
 *		size - No. of bytes to reserve for write
 *
 * Return Values :
 *		void * : Pointer to the reserved location. This is the address
 *		          that will be used for write (dma/bcopy)
 *
 * -----------------------------------------------------------------------------
 */
void * BCMFASTPATH
circularbuf_reserve_for_write(circularbuf_t *handle, uint16 size)
{
	int16 avail_space;
	void *ret_ptr = NULL;

	CBUF_DEBUG_CHECK(circularbuf_check_sanity(handle));
	ASSERT(size < handle->depth);

	if (handle->wp_ptr >= handle->r_ptr)
		avail_space = handle->depth - handle->wp_ptr;
	else
		avail_space = handle->r_ptr - handle->wp_ptr;

	ASSERT(avail_space <= handle->depth);
	if (avail_space > size)
	{
		/* Great. We have enough space. */
		ret_ptr = CIRCULARBUF_START(handle) + handle->wp_ptr;

		/*
		 * We need to update the wp_ptr for the next guy to write.
		 *
		 * Please Note : We are not updating the write pointer here. This can be
		 * done only after write is complete (In case of DMA, we can only schedule
		 * the DMA. Actual completion will be known only on DMA complete interrupt).
		 */
		handle->wp_ptr += size;
		return ret_ptr;
	}

	/*
	 * If there is no available space, we should check if there is some space left
	 * in the beginning of the circular buffer.  Wrap-around case, where there is
	 * not enough space in the end of the circular buffer. But, there might be
	 * room in the beginning of the buffer.
	 */
	if (handle->wp_ptr >= handle->r_ptr)
	{
		avail_space = handle->r_ptr;
		if (avail_space > size)
		{
			/* OK. There is room in the beginning. Let's go ahead and use that.
			 * But, before that, we have left a hole at the end of the circular
			 * buffer as that was not sufficient to accomodate the requested
			 * size. Let's make sure this is updated in the circularbuf structure
			 * so that consumer does not use the hole.
			 */
			handle->e_ptr  = handle->wp_ptr;
			handle->wp_ptr = size;

			return CIRCULARBUF_START(handle);
		}
	}

	/* We have tried enough to accomodate the new packet. There is no room for now. */
	return NULL;
}

/*
 * -----------------------------------------------------------------------------
 * Function   : circularbuf_write_complete
 *
 * Description:
 * This function has to be called by the producer end of circularbuf to indicate to
 * the circularbuf layer that data has been written and the write pointer can be
 * updated. In the process, if there was a doorbell callback registered, that
 * function would also be invoked.
 *
 * Input Args :
 *		dest_addr	  : Address where the data was written. This would be the
 *					    same address that was reserved earlier.
 *		bytes_written : Length of data written
 *
 * -----------------------------------------------------------------------------
 */
void BCMFASTPATH
circularbuf_write_complete(circularbuf_t *handle, uint16 bytes_written)
{
	CBUF_DEBUG_CHECK(circularbuf_check_sanity(handle));

	/* Update the write pointer */
	if ((handle->w_ptr + bytes_written) >= handle->depth) {
		OSL_CACHE_FLUSH((void *) CIRCULARBUF_START(handle), bytes_written);
		handle->w_ptr = bytes_written;
	} else {
		OSL_CACHE_FLUSH((void *) (CIRCULARBUF_START(handle) + handle->w_ptr),
			bytes_written);
		handle->w_ptr += bytes_written;
	}

	/* And ring the door bell (mail box interrupt) to indicate to the peer that
	 * message is available for consumption.
	 */
	if (handle->mb_ring_bell)
		handle->mb_ring_bell(handle->mb_ctx);
}

/*
 * -----------------------------------------------------------------------------
 * Function   : circularbuf_get_read_ptr
 *
 * Description:
 * This function will be called by the consumer of circularbuf for reading data from
 * the circular buffer. This will typically be invoked when the consumer gets a
 * doorbell interrupt.
 * Please note that the function only returns the pointer (and length) from
 * where the data can be read. Actual read implementation is upto the
 * consumer. It could be a bcopy or dma.
 *
 * Input Args :
 *		void *			: Address from where the data can be read.
 *		available_len	: Length of data available for read.
 *
 * -----------------------------------------------------------------------------
 */
void * BCMFASTPATH
circularbuf_get_read_ptr(circularbuf_t *handle, uint16 *available_len)
{
	uint8 *ret_addr;

	CBUF_DEBUG_CHECK(circularbuf_check_sanity(handle));

	/* First check if there is any data available in the circular buffer */
	*available_len = CIRCULARBUF_READ_SPACE_AVAIL(handle);
	if (*available_len == 0)
		return NULL;

	/*
	 * Although there might be data in the circular buffer for read, in
	 * cases of write wrap-around and read still in the end of the circular
	 * buffer, we might have to wrap around the read pending pointer also.
	 */
	if (CIRCULARBUF_READ_SPACE_AT_END(handle) == 0)
		handle->rp_ptr = 0;

	ret_addr = CIRCULARBUF_START(handle) + handle->rp_ptr;

	/*
	 * Please note that we do not update the read pointer here. Only
	 * read pending pointer is updated, so that next reader knows where
	 * to read data from.
	 * read pointer can only be updated when the read is complete.
	 */
	handle->rp_ptr = (uint16)(ret_addr - CIRCULARBUF_START(handle) + *available_len);

	ASSERT(*available_len <= handle->depth);

	OSL_CACHE_INV((void *) ret_addr, *available_len);

	return ret_addr;
}

/*
 * -----------------------------------------------------------------------------
 * Function   : circularbuf_read_complete
 * Description:
 * This function has to be called by the consumer end of circularbuf to indicate
 * that data has been consumed and the read pointer can be updated.
 *
 * Input Args :
 *		bytes_read : No. of bytes consumed by the consumer. This has to match
 *					 the length returned by circularbuf_get_read_ptr
 *
 * Return Values :
 *		CIRCULARBUF_SUCCESS		: Otherwise
 *
 * -----------------------------------------------------------------------------
 */
circularbuf_ret_t BCMFASTPATH
circularbuf_read_complete(circularbuf_t *handle, uint16 bytes_read)
{
	CBUF_DEBUG_CHECK(circularbuf_check_sanity(handle));
	ASSERT(bytes_read < handle->depth);

	/* Update the read pointer */
	if ((handle->r_ptr + bytes_read) >= handle->depth)
		handle->r_ptr = bytes_read;
	else
		handle->r_ptr += bytes_read;

	return CIRCULARBUF_SUCCESS;
}
/*
 * -----------------------------------------------------------------------------
 * Function	: circularbuf_revert_rp_ptr
 *
 * Description:
 * The rp_ptr update during circularbuf_get_read_ptr() is done to reflect the amount of data
 * that is sent out to be read by the consumer. But the consumer may not always read the
 * entire data. In such a case, the rp_ptr needs to be reverted back by 'left' bytes, where
 * 'left' is the no. of bytes left unread.
 *
 * Input args:
 * 	bytes : The no. of bytes left unread by the consumer
 *
 * -----------------------------------------------------------------------------
 */
circularbuf_ret_t
circularbuf_revert_rp_ptr(circularbuf_t *handle, uint16 bytes)
{
	CBUF_DEBUG_CHECK(circularbuf_check_sanity(handle));
	ASSERT(bytes < handle->depth);

	handle->rp_ptr -= bytes;

	return CIRCULARBUF_SUCCESS;
}
