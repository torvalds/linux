/*
 *  linux/drivers/acorn/scsi/scsi.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Commonly used scsi driver functions.
 */

#include <linux/scatterlist.h>

#define BELT_AND_BRACES

/*
 * The scatter-gather list handling.  This contains all
 * the yucky stuff that needs to be fixed properly.
 */
static inline int copy_SCp_to_sg(struct scatterlist *sg, Scsi_Pointer *SCp, int max)
{
	int bufs = SCp->buffers_residual;

	BUG_ON(bufs + 1 > max);

	sg_set_buf(sg, SCp->ptr, SCp->this_residual);

	if (bufs)
		memcpy(sg + 1, SCp->buffer + 1,
		       sizeof(struct scatterlist) * bufs);
	return bufs + 1;
}

static inline int next_SCp(Scsi_Pointer *SCp)
{
	int ret = SCp->buffers_residual;
	if (ret) {
		SCp->buffer++;
		SCp->buffers_residual--;
		SCp->ptr = (char *)
			 (page_address(SCp->buffer->page) +
			  SCp->buffer->offset);
		SCp->this_residual = SCp->buffer->length;
	} else {
		SCp->ptr = NULL;
		SCp->this_residual = 0;
	}
	return ret;
}

static inline unsigned char get_next_SCp_byte(Scsi_Pointer *SCp)
{
	char c = *SCp->ptr;

	SCp->ptr += 1;
	SCp->this_residual -= 1;

	return c;
}

static inline void put_next_SCp_byte(Scsi_Pointer *SCp, unsigned char c)
{
	*SCp->ptr = c;
	SCp->ptr += 1;
	SCp->this_residual -= 1;
}

static inline void init_SCp(Scsi_Cmnd *SCpnt)
{
	memset(&SCpnt->SCp, 0, sizeof(struct scsi_pointer));

	if (SCpnt->use_sg) {
		unsigned long len = 0;
		int buf;

		SCpnt->SCp.buffer = (struct scatterlist *) SCpnt->buffer;
		SCpnt->SCp.buffers_residual = SCpnt->use_sg - 1;
		SCpnt->SCp.ptr = (char *)
			 (page_address(SCpnt->SCp.buffer->page) +
			  SCpnt->SCp.buffer->offset);
		SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;

#ifdef BELT_AND_BRACES
		/*
		 * Calculate correct buffer length.  Some commands
		 * come in with the wrong request_bufflen.
		 */
		for (buf = 0; buf <= SCpnt->SCp.buffers_residual; buf++)
			len += SCpnt->SCp.buffer[buf].length;

		if (SCpnt->request_bufflen != len)
			printk(KERN_WARNING "scsi%d.%c: bad request buffer "
			       "length %d, should be %ld\n", SCpnt->device->host->host_no,
			       '0' + SCpnt->device->id, SCpnt->request_bufflen, len);
		SCpnt->request_bufflen = len;
#endif
	} else {
		SCpnt->SCp.ptr = (unsigned char *)SCpnt->request_buffer;
		SCpnt->SCp.this_residual = SCpnt->request_bufflen;
	}

	/*
	 * If the upper SCSI layers pass a buffer, but zero length,
	 * we aren't interested in the buffer pointer.
	 */
	if (SCpnt->SCp.this_residual == 0 && SCpnt->SCp.ptr) {
#if 0 //def BELT_AND_BRACES
		printk(KERN_WARNING "scsi%d.%c: zero length buffer passed for "
		       "command ", SCpnt->host->host_no, '0' + SCpnt->target);
		__scsi_print_command(SCpnt->cmnd);
#endif
		SCpnt->SCp.ptr = NULL;
	}
}
