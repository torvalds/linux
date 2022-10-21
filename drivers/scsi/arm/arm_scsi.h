/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2002 Russell King
 *
 *  Commonly used functions by the ARM SCSI-II drivers.
 */

#include <linux/scatterlist.h>

#define BELT_AND_BRACES

struct arm_cmd_priv {
	struct scsi_pointer scsi_pointer;
};

static inline struct scsi_pointer *arm_scsi_pointer(struct scsi_cmnd *cmd)
{
	struct arm_cmd_priv *acmd = scsi_cmd_priv(cmd);

	return &acmd->scsi_pointer;
}

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
	struct scsi_pointer *scsi_pointer = arm_scsi_pointer(SCpnt);

	memset(scsi_pointer, 0, sizeof(struct scsi_pointer));

	if (scsi_bufflen(SCpnt)) {
		unsigned long len = 0;

		scsi_pointer->buffer = scsi_sglist(SCpnt);
		scsi_pointer->buffers_residual = scsi_sg_count(SCpnt) - 1;
		scsi_pointer->ptr = sg_virt(scsi_pointer->buffer);
		scsi_pointer->this_residual = scsi_pointer->buffer->length;
		scsi_pointer->phase = scsi_bufflen(SCpnt);

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
				scsi_pointer->phase =
					min_t(unsigned long, len,
					      scsi_bufflen(SCpnt));
			}
		}
#endif
	} else {
		scsi_pointer->ptr = NULL;
		scsi_pointer->this_residual = 0;
		scsi_pointer->phase = 0;
	}
}
