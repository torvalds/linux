/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/acorn/scsi/scsi.h
 *
 *  Copyright (C) 2002 Russell King
 *
 *  Commonly used scsi driver functions.
 */

#include <linux/scatterlist.h>

#define BELT_AND_BRACES

/*
 * The scatter-gather list handling.  This contains all
 * the yucky stuff that needs to be fixed properly.
 */

/*
 * copy_SCp_to_sg() Assumes contiguous allocation at @sg of at-most @max
 * entries of uninitialized memory. SCp is from scsi-ml and has a valid
 * (possibly chained) sg-list
 */
static inline int copy_SCp_to_sg(struct scatterlist *sg, struct scsi_pointer *SCp, int max)
{
	int bufs = SCp->buffers_residual;

	/* FIXME: It should be easy for drivers to loop on copy_SCp_to_sg().
	 * and to remove this BUG_ON. Use min() in-its-place
	 */
	BUG_ON(bufs + 1 > max);

	sg_set_buf(sg, SCp->ptr, SCp->this_residual);

	if (bufs) {
		struct scatterlist *src_sg;
		unsigned i;

		for_each_sg(sg_next(SCp->buffer), src_sg, bufs, i)
			*(++sg) = *src_sg;
		sg_mark_end(sg);
	}

	return bufs + 1;
}

static inline int next_SCp(struct scsi_pointer *SCp)
{
	int ret = SCp->buffers_residual;
	if (ret) {
		SCp->buffer = sg_next(SCp->buffer);
		SCp->buffers_residual--;
		SCp->ptr = sg_virt(SCp->buffer);
		SCp->this_residual = SCp->buffer->length;
	} else {
		SCp->ptr = NULL;
		SCp->this_residual = 0;
	}
	return ret;
}

static inline unsigned char get_next_SCp_byte(struct scsi_pointer *SCp)
{
	char c = *SCp->ptr;

	SCp->ptr += 1;
	SCp->this_residual -= 1;

	return c;
}

static inline void put_next_SCp_byte(struct scsi_pointer *SCp, unsigned char c)
{
	*SCp->ptr = c;
	SCp->ptr += 1;
	SCp->this_residual -= 1;
}

static inline void init_SCp(struct scsi_cmnd *SCpnt)
{
	memset(&SCpnt->SCp, 0, sizeof(struct scsi_pointer));

	if (scsi_bufflen(SCpnt)) {
		unsigned long len = 0;

		SCpnt->SCp.buffer = scsi_sglist(SCpnt);
		SCpnt->SCp.buffers_residual = scsi_sg_count(SCpnt) - 1;
		SCpnt->SCp.ptr = sg_virt(SCpnt->SCp.buffer);
		SCpnt->SCp.this_residual = SCpnt->SCp.buffer->length;
		SCpnt->SCp.phase = scsi_bufflen(SCpnt);

#ifdef BELT_AND_BRACES
		{	/*
			 * Calculate correct buffer length.  Some commands
			 * come in with the wrong scsi_bufflen.
			 */
			struct scatterlist *sg;
			unsigned i, sg_count = scsi_sg_count(SCpnt);

			scsi_for_each_sg(SCpnt, sg, sg_count, i)
				len += sg->length;

			if (scsi_bufflen(SCpnt) != len) {
				printk(KERN_WARNING
				       "scsi%d.%c: bad request buffer "
				       "length %d, should be %ld\n",
					SCpnt->device->host->host_no,
					'0' + SCpnt->device->id,
					scsi_bufflen(SCpnt), len);
				/*
				 * FIXME: Totaly naive fixup. We should abort
				 * with error
				 */
				SCpnt->SCp.phase =
					min_t(unsigned long, len,
					      scsi_bufflen(SCpnt));
			}
		}
#endif
	} else {
		SCpnt->SCp.ptr = NULL;
		SCpnt->SCp.this_residual = 0;
		SCpnt->SCp.phase = 0;
	}
}
