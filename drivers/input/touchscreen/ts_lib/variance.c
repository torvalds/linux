/*
 *  tslib/variance.c
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 *
 * Variance filter for touchscreen values.
 *
 * Problem: some touchscreens are sampled very roughly, thus even if
 * you hold the pen still, the samples can differ, sometimes substantially.
 * The worst happens when electric noise during sampling causes the result
 * to be substantially different from the real pen position; this causes
 * the mouse cursor to suddenly "jump" and then return back.
 *
 * Solution: delay sampled data by one timeslot. If we see that the last
 * sample read differs too much, we mark it as "suspicious". If next sample
 * read is close to the sample before the "suspicious", the suspicious sample
 * is dropped, otherwise we consider that a quick pen movement is in progress
 * and pass through both the "suspicious" sample and the sample after it.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
//#include <asm/typedef.h>
#include <mach/iomux.h>
#include <asm/uaccess.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/delay.h>
#include "tslib.h"

//#define DEBUG

#define VAR_PENDOWN			0x00000001
#define VAR_LASTVALID		0x00000002
#define VAR_NOISEVALID		0x00000004
#define VAR_SUBMITNOISE		0x00000008
void variance_clear(struct tslib_info *info)
{
	struct ts_sample cur;
	struct tslib_variance *var = info->var;
	
	cur.pressure = 0;

	/* Flush the queue immediately when the pen is just
	 * released, otherwise the previous layer will
	 * get the pen up notification too late. This 
	 * will happen if info->next->ops->read() blocks.
	 */
	if (var->flags & VAR_PENDOWN) {
		var->flags |= VAR_SUBMITNOISE;
		var->noise = cur;
	}
	/* Reset the state machine on pen up events. */
	var->flags &= ~(VAR_PENDOWN | VAR_NOISEVALID | VAR_LASTVALID);
	var->noise = cur;
	var->last = cur;

	return;
}
int variance_read(struct tslib_info *info, struct ts_sample *samp, int nr)
{
	struct tslib_variance *var = info->var;
	struct ts_sample cur;
	int count = 0, dist;

	while (count < nr) {
		if (var->flags & VAR_SUBMITNOISE) {
			cur = var->noise;
			var->flags &= ~VAR_SUBMITNOISE;
		} else {
			if (info->raw_read(info, &cur, 1) < 1)
				return count;
		}

		if (cur.pressure == 0) {
			/* Flush the queue immediately when the pen is just
			 * released, otherwise the previous layer will
			 * get the pen up notification too late. This 
			 * will happen if info->next->ops->read() blocks.
			 */
			if (var->flags & VAR_PENDOWN) {
				var->flags |= VAR_SUBMITNOISE;
				var->noise = cur;
			}
			/* Reset the state machine on pen up events. */
			var->flags &= ~(VAR_PENDOWN | VAR_NOISEVALID | VAR_LASTVALID);
			goto acceptsample;
		} else
			var->flags |= VAR_PENDOWN;

		if (!(var->flags & VAR_LASTVALID)) {
			var->last = cur;
			var->flags |= VAR_LASTVALID;
			continue;
		}

		if (var->flags & VAR_PENDOWN) {
			/* Compute the distance between last sample and current */
			dist = sqr(cur.x - var->last.x) +
			       sqr(cur.y - var->last.y);

			if (dist > var->delta) {
				/* Do we suspect the previous sample was a noise? */
				if (var->flags & VAR_NOISEVALID) {
					/* Two "noises": it's just a quick pen movement */
					samp [count++] = var->last = var->noise;
					var->flags = (var->flags & ~VAR_NOISEVALID) |
						VAR_SUBMITNOISE;
				} else
					var->flags |= VAR_NOISEVALID;

				/* The pen jumped too far, maybe it's a noise ... */
				var->noise = cur;
				continue;
			} else
				var->flags &= ~VAR_NOISEVALID;
		}

acceptsample:
#ifdef DEBUG
		printk("VARIANCE----------------> %d %d %d\n",
			var->last.x, var->last.y, var->last.pressure);
#endif
		samp [count++] = var->last;
		var->last = cur;
	}

	return count;
}
