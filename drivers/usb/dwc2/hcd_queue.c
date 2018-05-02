// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * hcd_queue.c - DesignWare HS OTG Controller host queuing routines
 *
 * Copyright (C) 2004-2013 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the functions to manage Queue Heads and Queue
 * Transfer Descriptors for Host mode
 */
#include <linux/gcd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/usb/hcd.h>
#include <linux/usb/ch11.h>

#include "core.h"
#include "hcd.h"

/* Wait this long before releasing periodic reservation */
#define DWC2_UNRESERVE_DELAY (msecs_to_jiffies(5))

/* If we get a NAK, wait this long before retrying */
#define DWC2_RETRY_WAIT_DELAY (msecs_to_jiffies(1))

/**
 * dwc2_periodic_channel_available() - Checks that a channel is available for a
 * periodic transfer
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_periodic_channel_available(struct dwc2_hsotg *hsotg)
{
	/*
	 * Currently assuming that there is a dedicated host channel for
	 * each periodic transaction plus at least one host channel for
	 * non-periodic transactions
	 */
	int status;
	int num_channels;

	num_channels = hsotg->params.host_channels;
	if ((hsotg->periodic_channels + hsotg->non_periodic_channels <
	     num_channels) && (hsotg->periodic_channels < num_channels - 1)) {
		status = 0;
	} else {
		dev_dbg(hsotg->dev,
			"%s: Total channels: %d, Periodic: %d, Non-periodic: %d\n",
			__func__, num_channels,
			hsotg->periodic_channels, hsotg->non_periodic_channels);
		status = -ENOSPC;
	}

	return status;
}

/**
 * dwc2_check_periodic_bandwidth() - Checks that there is sufficient bandwidth
 * for the specified QH in the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH containing periodic bandwidth required
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * For simplicity, this calculation assumes that all the transfers in the
 * periodic schedule may occur in the same (micro)frame
 */
static int dwc2_check_periodic_bandwidth(struct dwc2_hsotg *hsotg,
					 struct dwc2_qh *qh)
{
	int status;
	s16 max_claimed_usecs;

	status = 0;

	if (qh->dev_speed == USB_SPEED_HIGH || qh->do_split) {
		/*
		 * High speed mode
		 * Max periodic usecs is 80% x 125 usec = 100 usec
		 */
		max_claimed_usecs = 100 - qh->host_us;
	} else {
		/*
		 * Full speed mode
		 * Max periodic usecs is 90% x 1000 usec = 900 usec
		 */
		max_claimed_usecs = 900 - qh->host_us;
	}

	if (hsotg->periodic_usecs > max_claimed_usecs) {
		dev_err(hsotg->dev,
			"%s: already claimed usecs %d, required usecs %d\n",
			__func__, hsotg->periodic_usecs, qh->host_us);
		status = -ENOSPC;
	}

	return status;
}

/**
 * pmap_schedule() - Schedule time in a periodic bitmap (pmap).
 *
 * @map:             The bitmap representing the schedule; will be updated
 *                   upon success.
 * @bits_per_period: The schedule represents several periods.  This is how many
 *                   bits are in each period.  It's assumed that the beginning
 *                   of the schedule will repeat after its end.
 * @periods_in_map:  The number of periods in the schedule.
 * @num_bits:        The number of bits we need per period we want to reserve
 *                   in this function call.
 * @interval:        How often we need to be scheduled for the reservation this
 *                   time.  1 means every period.  2 means every other period.
 *                   ...you get the picture?
 * @start:           The bit number to start at.  Normally 0.  Must be within
 *                   the interval or we return failure right away.
 * @only_one_period: Normally we'll allow picking a start anywhere within the
 *                   first interval, since we can still make all repetition
 *                   requirements by doing that.  However, if you pass true
 *                   here then we'll return failure if we can't fit within
 *                   the period that "start" is in.
 *
 * The idea here is that we want to schedule time for repeating events that all
 * want the same resource.  The resource is divided into fixed-sized periods
 * and the events want to repeat every "interval" periods.  The schedule
 * granularity is one bit.
 *
 * To keep things "simple", we'll represent our schedule with a bitmap that
 * contains a fixed number of periods.  This gets rid of a lot of complexity
 * but does mean that we need to handle things specially (and non-ideally) if
 * the number of the periods in the schedule doesn't match well with the
 * intervals that we're trying to schedule.
 *
 * Here's an explanation of the scheme we'll implement, assuming 8 periods.
 * - If interval is 1, we need to take up space in each of the 8
 *   periods we're scheduling.  Easy.
 * - If interval is 2, we need to take up space in half of the
 *   periods.  Again, easy.
 * - If interval is 3, we actually need to fall back to interval 1.
 *   Why?  Because we might need time in any period.  AKA for the
 *   first 8 periods, we'll be in slot 0, 3, 6.  Then we'll be
 *   in slot 1, 4, 7.  Then we'll be in 2, 5.  Then we'll be back to
 *   0, 3, and 6.  Since we could be in any frame we need to reserve
 *   for all of them.  Sucks, but that's what you gotta do.  Note that
 *   if we were instead scheduling 8 * 3 = 24 we'd do much better, but
 *   then we need more memory and time to do scheduling.
 * - If interval is 4, easy.
 * - If interval is 5, we again need interval 1.  The schedule will be
 *   0, 5, 2, 7, 4, 1, 6, 3, 0
 * - If interval is 6, we need interval 2.  0, 6, 4, 2.
 * - If interval is 7, we need interval 1.
 * - If interval is 8, we need interval 8.
 *
 * If you do the math, you'll see that we need to pretend that interval is
 * equal to the greatest_common_divisor(interval, periods_in_map).
 *
 * Note that at the moment this function tends to front-pack the schedule.
 * In some cases that's really non-ideal (it's hard to schedule things that
 * need to repeat every period).  In other cases it's perfect (you can easily
 * schedule bigger, less often repeating things).
 *
 * Here's the algorithm in action (8 periods, 5 bits per period):
 *  |**   |     |**   |     |**   |     |**   |     |   OK 2 bits, intv 2 at 0
 *  |*****|  ***|*****|  ***|*****|  ***|*****|  ***|   OK 3 bits, intv 3 at 2
 *  |*****|* ***|*****|  ***|*****|* ***|*****|  ***|   OK 1 bits, intv 4 at 5
 *  |**   |*    |**   |     |**   |*    |**   |     | Remv 3 bits, intv 3 at 2
 *  |***  |*    |***  |     |***  |*    |***  |     |   OK 1 bits, intv 6 at 2
 *  |**** |*  * |**** |   * |**** |*  * |**** |   * |   OK 1 bits, intv 1 at 3
 *  |**** |**** |**** | *** |**** |**** |**** | *** |   OK 2 bits, intv 2 at 6
 *  |*****|*****|*****| ****|*****|*****|*****| ****|   OK 1 bits, intv 1 at 4
 *  |*****|*****|*****| ****|*****|*****|*****| ****| FAIL 1 bits, intv 1
 *  |  ***|*****|  ***| ****|  ***|*****|  ***| ****| Remv 2 bits, intv 2 at 0
 *  |  ***| ****|  ***| ****|  ***| ****|  ***| ****| Remv 1 bits, intv 4 at 5
 *  |   **| ****|   **| ****|   **| ****|   **| ****| Remv 1 bits, intv 6 at 2
 *  |    *| ** *|    *| ** *|    *| ** *|    *| ** *| Remv 1 bits, intv 1 at 3
 *  |    *|    *|    *|    *|    *|    *|    *|    *| Remv 2 bits, intv 2 at 6
 *  |     |     |     |     |     |     |     |     | Remv 1 bits, intv 1 at 4
 *  |**   |     |**   |     |**   |     |**   |     |   OK 2 bits, intv 2 at 0
 *  |***  |     |**   |     |***  |     |**   |     |   OK 1 bits, intv 4 at 2
 *  |*****|     |** **|     |*****|     |** **|     |   OK 2 bits, intv 2 at 3
 *  |*****|*    |** **|     |*****|*    |** **|     |   OK 1 bits, intv 4 at 5
 *  |*****|***  |** **| **  |*****|***  |** **| **  |   OK 2 bits, intv 2 at 6
 *  |*****|*****|** **| ****|*****|*****|** **| ****|   OK 2 bits, intv 2 at 8
 *  |*****|*****|*****| ****|*****|*****|*****| ****|   OK 1 bits, intv 4 at 12
 *
 * This function is pretty generic and could be easily abstracted if anything
 * needed similar scheduling.
 *
 * Returns either -ENOSPC or a >= 0 start bit which should be passed to the
 * unschedule routine.  The map bitmap will be updated on a non-error result.
 */
static int pmap_schedule(unsigned long *map, int bits_per_period,
			 int periods_in_map, int num_bits,
			 int interval, int start, bool only_one_period)
{
	int interval_bits;
	int to_reserve;
	int first_end;
	int i;

	if (num_bits > bits_per_period)
		return -ENOSPC;

	/* Adjust interval as per description */
	interval = gcd(interval, periods_in_map);

	interval_bits = bits_per_period * interval;
	to_reserve = periods_in_map / interval;

	/* If start has gotten us past interval then we can't schedule */
	if (start >= interval_bits)
		return -ENOSPC;

	if (only_one_period)
		/* Must fit within same period as start; end at begin of next */
		first_end = (start / bits_per_period + 1) * bits_per_period;
	else
		/* Can fit anywhere in the first interval */
		first_end = interval_bits;

	/*
	 * We'll try to pick the first repetition, then see if that time
	 * is free for each of the subsequent repetitions.  If it's not
	 * we'll adjust the start time for the next search of the first
	 * repetition.
	 */
	while (start + num_bits <= first_end) {
		int end;

		/* Need to stay within this period */
		end = (start / bits_per_period + 1) * bits_per_period;

		/* Look for num_bits us in this microframe starting at start */
		start = bitmap_find_next_zero_area(map, end, start, num_bits,
						   0);

		/*
		 * We should get start >= end if we fail.  We might be
		 * able to check the next microframe depending on the
		 * interval, so continue on (start already updated).
		 */
		if (start >= end) {
			start = end;
			continue;
		}

		/* At this point we have a valid point for first one */
		for (i = 1; i < to_reserve; i++) {
			int ith_start = start + interval_bits * i;
			int ith_end = end + interval_bits * i;
			int ret;

			/* Use this as a dumb "check if bits are 0" */
			ret = bitmap_find_next_zero_area(
				map, ith_start + num_bits, ith_start, num_bits,
				0);

			/* We got the right place, continue checking */
			if (ret == ith_start)
				continue;

			/* Move start up for next time and exit for loop */
			ith_start = bitmap_find_next_zero_area(
				map, ith_end, ith_start, num_bits, 0);
			if (ith_start >= ith_end)
				/* Need a while new period next time */
				start = end;
			else
				start = ith_start - interval_bits * i;
			break;
		}

		/* If didn't exit the for loop with a break, we have success */
		if (i == to_reserve)
			break;
	}

	if (start + num_bits > first_end)
		return -ENOSPC;

	for (i = 0; i < to_reserve; i++) {
		int ith_start = start + interval_bits * i;

		bitmap_set(map, ith_start, num_bits);
	}

	return start;
}

/**
 * pmap_unschedule() - Undo work done by pmap_schedule()
 *
 * @map:             See pmap_schedule().
 * @bits_per_period: See pmap_schedule().
 * @periods_in_map:  See pmap_schedule().
 * @num_bits:        The number of bits that was passed to schedule.
 * @interval:        The interval that was passed to schedule.
 * @start:           The return value from pmap_schedule().
 */
static void pmap_unschedule(unsigned long *map, int bits_per_period,
			    int periods_in_map, int num_bits,
			    int interval, int start)
{
	int interval_bits;
	int to_release;
	int i;

	/* Adjust interval as per description in pmap_schedule() */
	interval = gcd(interval, periods_in_map);

	interval_bits = bits_per_period * interval;
	to_release = periods_in_map / interval;

	for (i = 0; i < to_release; i++) {
		int ith_start = start + interval_bits * i;

		bitmap_clear(map, ith_start, num_bits);
	}
}

/**
 * dwc2_get_ls_map() - Get the map used for the given qh
 *
 * @hsotg: The HCD state structure for the DWC OTG controller.
 * @qh:    QH for the periodic transfer.
 *
 * We'll always get the periodic map out of our TT.  Note that even if we're
 * running the host straight in low speed / full speed mode it appears as if
 * a TT is allocated for us, so we'll use it.  If that ever changes we can
 * add logic here to get a map out of "hsotg" if !qh->do_split.
 *
 * Returns: the map or NULL if a map couldn't be found.
 */
static unsigned long *dwc2_get_ls_map(struct dwc2_hsotg *hsotg,
				      struct dwc2_qh *qh)
{
	unsigned long *map;

	/* Don't expect to be missing a TT and be doing low speed scheduling */
	if (WARN_ON(!qh->dwc_tt))
		return NULL;

	/* Get the map and adjust if this is a multi_tt hub */
	map = qh->dwc_tt->periodic_bitmaps;
	if (qh->dwc_tt->usb_tt->multi)
		map += DWC2_ELEMENTS_PER_LS_BITMAP * qh->ttport;

	return map;
}

#ifdef DWC2_PRINT_SCHEDULE
/*
 * cat_printf() - A printf() + strcat() helper
 *
 * This is useful for concatenating a bunch of strings where each string is
 * constructed using printf.
 *
 * @buf:   The destination buffer; will be updated to point after the printed
 *         data.
 * @size:  The number of bytes in the buffer (includes space for '\0').
 * @fmt:   The format for printf.
 * @...:   The args for printf.
 */
static __printf(3, 4)
void cat_printf(char **buf, size_t *size, const char *fmt, ...)
{
	va_list args;
	int i;

	if (*size == 0)
		return;

	va_start(args, fmt);
	i = vsnprintf(*buf, *size, fmt, args);
	va_end(args);

	if (i >= *size) {
		(*buf)[*size - 1] = '\0';
		*buf += *size;
		*size = 0;
	} else {
		*buf += i;
		*size -= i;
	}
}

/*
 * pmap_print() - Print the given periodic map
 *
 * Will attempt to print out the periodic schedule.
 *
 * @map:             See pmap_schedule().
 * @bits_per_period: See pmap_schedule().
 * @periods_in_map:  See pmap_schedule().
 * @period_name:     The name of 1 period, like "uFrame"
 * @units:           The name of the units, like "us".
 * @print_fn:        The function to call for printing.
 * @print_data:      Opaque data to pass to the print function.
 */
static void pmap_print(unsigned long *map, int bits_per_period,
		       int periods_in_map, const char *period_name,
		       const char *units,
		       void (*print_fn)(const char *str, void *data),
		       void *print_data)
{
	int period;

	for (period = 0; period < periods_in_map; period++) {
		char tmp[64];
		char *buf = tmp;
		size_t buf_size = sizeof(tmp);
		int period_start = period * bits_per_period;
		int period_end = period_start + bits_per_period;
		int start = 0;
		int count = 0;
		bool printed = false;
		int i;

		for (i = period_start; i < period_end + 1; i++) {
			/* Handle case when ith bit is set */
			if (i < period_end &&
			    bitmap_find_next_zero_area(map, i + 1,
						       i, 1, 0) != i) {
				if (count == 0)
					start = i - period_start;
				count++;
				continue;
			}

			/* ith bit isn't set; don't care if count == 0 */
			if (count == 0)
				continue;

			if (!printed)
				cat_printf(&buf, &buf_size, "%s %d: ",
					   period_name, period);
			else
				cat_printf(&buf, &buf_size, ", ");
			printed = true;

			cat_printf(&buf, &buf_size, "%d %s -%3d %s", start,
				   units, start + count - 1, units);
			count = 0;
		}

		if (printed)
			print_fn(tmp, print_data);
	}
}

struct dwc2_qh_print_data {
	struct dwc2_hsotg *hsotg;
	struct dwc2_qh *qh;
};

/**
 * dwc2_qh_print() - Helper function for dwc2_qh_schedule_print()
 *
 * @str:  The string to print
 * @data: A pointer to a struct dwc2_qh_print_data
 */
static void dwc2_qh_print(const char *str, void *data)
{
	struct dwc2_qh_print_data *print_data = data;

	dwc2_sch_dbg(print_data->hsotg, "QH=%p ...%s\n", print_data->qh, str);
}

/**
 * dwc2_qh_schedule_print() - Print the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller.
 * @qh:    QH to print.
 */
static void dwc2_qh_schedule_print(struct dwc2_hsotg *hsotg,
				   struct dwc2_qh *qh)
{
	struct dwc2_qh_print_data print_data = { hsotg, qh };
	int i;

	/*
	 * The printing functions are quite slow and inefficient.
	 * If we don't have tracing turned on, don't run unless the special
	 * define is turned on.
	 */

	if (qh->schedule_low_speed) {
		unsigned long *map = dwc2_get_ls_map(hsotg, qh);

		dwc2_sch_dbg(hsotg, "QH=%p LS/FS trans: %d=>%d us @ %d us",
			     qh, qh->device_us,
			     DWC2_ROUND_US_TO_SLICE(qh->device_us),
			     DWC2_US_PER_SLICE * qh->ls_start_schedule_slice);

		if (map) {
			dwc2_sch_dbg(hsotg,
				     "QH=%p Whole low/full speed map %p now:\n",
				     qh, map);
			pmap_print(map, DWC2_LS_PERIODIC_SLICES_PER_FRAME,
				   DWC2_LS_SCHEDULE_FRAMES, "Frame ", "slices",
				   dwc2_qh_print, &print_data);
		}
	}

	for (i = 0; i < qh->num_hs_transfers; i++) {
		struct dwc2_hs_transfer_time *trans_time = qh->hs_transfers + i;
		int uframe = trans_time->start_schedule_us /
			     DWC2_HS_PERIODIC_US_PER_UFRAME;
		int rel_us = trans_time->start_schedule_us %
			     DWC2_HS_PERIODIC_US_PER_UFRAME;

		dwc2_sch_dbg(hsotg,
			     "QH=%p HS trans #%d: %d us @ uFrame %d + %d us\n",
			     qh, i, trans_time->duration_us, uframe, rel_us);
	}
	if (qh->num_hs_transfers) {
		dwc2_sch_dbg(hsotg, "QH=%p Whole high speed map now:\n", qh);
		pmap_print(hsotg->hs_periodic_bitmap,
			   DWC2_HS_PERIODIC_US_PER_UFRAME,
			   DWC2_HS_SCHEDULE_UFRAMES, "uFrame", "us",
			   dwc2_qh_print, &print_data);
	}
}
#else
static inline void dwc2_qh_schedule_print(struct dwc2_hsotg *hsotg,
					  struct dwc2_qh *qh) {};
#endif

/**
 * dwc2_ls_pmap_schedule() - Schedule a low speed QH
 *
 * @hsotg:        The HCD state structure for the DWC OTG controller.
 * @qh:           QH for the periodic transfer.
 * @search_slice: We'll start trying to schedule at the passed slice.
 *                Remember that slices are the units of the low speed
 *                schedule (think 25us or so).
 *
 * Wraps pmap_schedule() with the right parameters for low speed scheduling.
 *
 * Normally we schedule low speed devices on the map associated with the TT.
 *
 * Returns: 0 for success or an error code.
 */
static int dwc2_ls_pmap_schedule(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
				 int search_slice)
{
	int slices = DIV_ROUND_UP(qh->device_us, DWC2_US_PER_SLICE);
	unsigned long *map = dwc2_get_ls_map(hsotg, qh);
	int slice;

	if (!map)
		return -EINVAL;

	/*
	 * Schedule on the proper low speed map with our low speed scheduling
	 * parameters.  Note that we use the "device_interval" here since
	 * we want the low speed interval and the only way we'd be in this
	 * function is if the device is low speed.
	 *
	 * If we happen to be doing low speed and high speed scheduling for the
	 * same transaction (AKA we have a split) we always do low speed first.
	 * That means we can always pass "false" for only_one_period (that
	 * parameters is only useful when we're trying to get one schedule to
	 * match what we already planned in the other schedule).
	 */
	slice = pmap_schedule(map, DWC2_LS_PERIODIC_SLICES_PER_FRAME,
			      DWC2_LS_SCHEDULE_FRAMES, slices,
			      qh->device_interval, search_slice, false);

	if (slice < 0)
		return slice;

	qh->ls_start_schedule_slice = slice;
	return 0;
}

/**
 * dwc2_ls_pmap_unschedule() - Undo work done by dwc2_ls_pmap_schedule()
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static void dwc2_ls_pmap_unschedule(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh)
{
	int slices = DIV_ROUND_UP(qh->device_us, DWC2_US_PER_SLICE);
	unsigned long *map = dwc2_get_ls_map(hsotg, qh);

	/* Schedule should have failed, so no worries about no error code */
	if (!map)
		return;

	pmap_unschedule(map, DWC2_LS_PERIODIC_SLICES_PER_FRAME,
			DWC2_LS_SCHEDULE_FRAMES, slices, qh->device_interval,
			qh->ls_start_schedule_slice);
}

/**
 * dwc2_hs_pmap_schedule - Schedule in the main high speed schedule
 *
 * This will schedule something on the main dwc2 schedule.
 *
 * We'll start looking in qh->hs_transfers[index].start_schedule_us.  We'll
 * update this with the result upon success.  We also use the duration from
 * the same structure.
 *
 * @hsotg:           The HCD state structure for the DWC OTG controller.
 * @qh:              QH for the periodic transfer.
 * @only_one_period: If true we will limit ourselves to just looking at
 *                   one period (aka one 100us chunk).  This is used if we have
 *                   already scheduled something on the low speed schedule and
 *                   need to find something that matches on the high speed one.
 * @index:           The index into qh->hs_transfers that we're working with.
 *
 * Returns: 0 for success or an error code.  Upon success the
 *          dwc2_hs_transfer_time specified by "index" will be updated.
 */
static int dwc2_hs_pmap_schedule(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
				 bool only_one_period, int index)
{
	struct dwc2_hs_transfer_time *trans_time = qh->hs_transfers + index;
	int us;

	us = pmap_schedule(hsotg->hs_periodic_bitmap,
			   DWC2_HS_PERIODIC_US_PER_UFRAME,
			   DWC2_HS_SCHEDULE_UFRAMES, trans_time->duration_us,
			   qh->host_interval, trans_time->start_schedule_us,
			   only_one_period);

	if (us < 0)
		return us;

	trans_time->start_schedule_us = us;
	return 0;
}

/**
 * dwc2_ls_pmap_unschedule() - Undo work done by dwc2_hs_pmap_schedule()
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static void dwc2_hs_pmap_unschedule(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh, int index)
{
	struct dwc2_hs_transfer_time *trans_time = qh->hs_transfers + index;

	pmap_unschedule(hsotg->hs_periodic_bitmap,
			DWC2_HS_PERIODIC_US_PER_UFRAME,
			DWC2_HS_SCHEDULE_UFRAMES, trans_time->duration_us,
			qh->host_interval, trans_time->start_schedule_us);
}

/**
 * dwc2_uframe_schedule_split - Schedule a QH for a periodic split xfer.
 *
 * This is the most complicated thing in USB.  We have to find matching time
 * in both the global high speed schedule for the port and the low speed
 * schedule for the TT associated with the given device.
 *
 * Being here means that the host must be running in high speed mode and the
 * device is in low or full speed mode (and behind a hub).
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static int dwc2_uframe_schedule_split(struct dwc2_hsotg *hsotg,
				      struct dwc2_qh *qh)
{
	int bytecount = dwc2_hb_mult(qh->maxp) * dwc2_max_packet(qh->maxp);
	int ls_search_slice;
	int err = 0;
	int host_interval_in_sched;

	/*
	 * The interval (how often to repeat) in the actual host schedule.
	 * See pmap_schedule() for gcd() explanation.
	 */
	host_interval_in_sched = gcd(qh->host_interval,
				     DWC2_HS_SCHEDULE_UFRAMES);

	/*
	 * We always try to find space in the low speed schedule first, then
	 * try to find high speed time that matches.  If we don't, we'll bump
	 * up the place we start searching in the low speed schedule and try
	 * again.  To start we'll look right at the beginning of the low speed
	 * schedule.
	 *
	 * Note that this will tend to front-load the high speed schedule.
	 * We may eventually want to try to avoid this by either considering
	 * both schedules together or doing some sort of round robin.
	 */
	ls_search_slice = 0;

	while (ls_search_slice < DWC2_LS_SCHEDULE_SLICES) {
		int start_s_uframe;
		int ssplit_s_uframe;
		int second_s_uframe;
		int rel_uframe;
		int first_count;
		int middle_count;
		int end_count;
		int first_data_bytes;
		int other_data_bytes;
		int i;

		if (qh->schedule_low_speed) {
			err = dwc2_ls_pmap_schedule(hsotg, qh, ls_search_slice);

			/*
			 * If we got an error here there's no other magic we
			 * can do, so bail.  All the looping above is only
			 * helpful to redo things if we got a low speed slot
			 * and then couldn't find a matching high speed slot.
			 */
			if (err)
				return err;
		} else {
			/* Must be missing the tt structure?  Why? */
			WARN_ON_ONCE(1);
		}

		/*
		 * This will give us a number 0 - 7 if
		 * DWC2_LS_SCHEDULE_FRAMES == 1, or 0 - 15 if == 2, or ...
		 */
		start_s_uframe = qh->ls_start_schedule_slice /
				 DWC2_SLICES_PER_UFRAME;

		/* Get a number that's always 0 - 7 */
		rel_uframe = (start_s_uframe % 8);

		/*
		 * If we were going to start in uframe 7 then we would need to
		 * issue a start split in uframe 6, which spec says is not OK.
		 * Move on to the next full frame (assuming there is one).
		 *
		 * See 11.18.4 Host Split Transaction Scheduling Requirements
		 * bullet 1.
		 */
		if (rel_uframe == 7) {
			if (qh->schedule_low_speed)
				dwc2_ls_pmap_unschedule(hsotg, qh);
			ls_search_slice =
				(qh->ls_start_schedule_slice /
				 DWC2_LS_PERIODIC_SLICES_PER_FRAME + 1) *
				DWC2_LS_PERIODIC_SLICES_PER_FRAME;
			continue;
		}

		/*
		 * For ISOC in:
		 * - start split            (frame -1)
		 * - complete split w/ data (frame +1)
		 * - complete split w/ data (frame +2)
		 * - ...
		 * - complete split w/ data (frame +num_data_packets)
		 * - complete split w/ data (frame +num_data_packets+1)
		 * - complete split w/ data (frame +num_data_packets+2, max 8)
		 *   ...though if frame was "0" then max is 7...
		 *
		 * For ISOC out we might need to do:
		 * - start split w/ data    (frame -1)
		 * - start split w/ data    (frame +0)
		 * - ...
		 * - start split w/ data    (frame +num_data_packets-2)
		 *
		 * For INTERRUPT in we might need to do:
		 * - start split            (frame -1)
		 * - complete split w/ data (frame +1)
		 * - complete split w/ data (frame +2)
		 * - complete split w/ data (frame +3, max 8)
		 *
		 * For INTERRUPT out we might need to do:
		 * - start split w/ data    (frame -1)
		 * - complete split         (frame +1)
		 * - complete split         (frame +2)
		 * - complete split         (frame +3, max 8)
		 *
		 * Start adjusting!
		 */
		ssplit_s_uframe = (start_s_uframe +
				   host_interval_in_sched - 1) %
				  host_interval_in_sched;
		if (qh->ep_type == USB_ENDPOINT_XFER_ISOC && !qh->ep_is_in)
			second_s_uframe = start_s_uframe;
		else
			second_s_uframe = start_s_uframe + 1;

		/* First data transfer might not be all 188 bytes. */
		first_data_bytes = 188 -
			DIV_ROUND_UP(188 * (qh->ls_start_schedule_slice %
					    DWC2_SLICES_PER_UFRAME),
				     DWC2_SLICES_PER_UFRAME);
		if (first_data_bytes > bytecount)
			first_data_bytes = bytecount;
		other_data_bytes = bytecount - first_data_bytes;

		/*
		 * For now, skip OUT xfers where first xfer is partial
		 *
		 * Main dwc2 code assumes:
		 * - INT transfers never get split in two.
		 * - ISOC transfers can always transfer 188 bytes the first
		 *   time.
		 *
		 * Until that code is fixed, try again if the first transfer
		 * couldn't transfer everything.
		 *
		 * This code can be removed if/when the rest of dwc2 handles
		 * the above cases.  Until it's fixed we just won't be able
		 * to schedule quite as tightly.
		 */
		if (!qh->ep_is_in &&
		    (first_data_bytes != min_t(int, 188, bytecount))) {
			dwc2_sch_dbg(hsotg,
				     "QH=%p avoiding broken 1st xfer (%d, %d)\n",
				     qh, first_data_bytes, bytecount);
			if (qh->schedule_low_speed)
				dwc2_ls_pmap_unschedule(hsotg, qh);
			ls_search_slice = (start_s_uframe + 1) *
				DWC2_SLICES_PER_UFRAME;
			continue;
		}

		/* Start by assuming transfers for the bytes */
		qh->num_hs_transfers = 1 + DIV_ROUND_UP(other_data_bytes, 188);

		/*
		 * Everything except ISOC OUT has extra transfers.  Rules are
		 * complicated.  See 11.18.4 Host Split Transaction Scheduling
		 * Requirements bullet 3.
		 */
		if (qh->ep_type == USB_ENDPOINT_XFER_INT) {
			if (rel_uframe == 6)
				qh->num_hs_transfers += 2;
			else
				qh->num_hs_transfers += 3;

			if (qh->ep_is_in) {
				/*
				 * First is start split, middle/end is data.
				 * Allocate full data bytes for all data.
				 */
				first_count = 4;
				middle_count = bytecount;
				end_count = bytecount;
			} else {
				/*
				 * First is data, middle/end is complete.
				 * First transfer and second can have data.
				 * Rest should just have complete split.
				 */
				first_count = first_data_bytes;
				middle_count = max_t(int, 4, other_data_bytes);
				end_count = 4;
			}
		} else {
			if (qh->ep_is_in) {
				int last;

				/* Account for the start split */
				qh->num_hs_transfers++;

				/* Calculate "L" value from spec */
				last = rel_uframe + qh->num_hs_transfers + 1;

				/* Start with basic case */
				if (last <= 6)
					qh->num_hs_transfers += 2;
				else
					qh->num_hs_transfers += 1;

				/* Adjust downwards */
				if (last >= 6 && rel_uframe == 0)
					qh->num_hs_transfers--;

				/* 1st = start; rest can contain data */
				first_count = 4;
				middle_count = min_t(int, 188, bytecount);
				end_count = middle_count;
			} else {
				/* All contain data, last might be smaller */
				first_count = first_data_bytes;
				middle_count = min_t(int, 188,
						     other_data_bytes);
				end_count = other_data_bytes % 188;
			}
		}

		/* Assign durations per uFrame */
		qh->hs_transfers[0].duration_us = HS_USECS_ISO(first_count);
		for (i = 1; i < qh->num_hs_transfers - 1; i++)
			qh->hs_transfers[i].duration_us =
				HS_USECS_ISO(middle_count);
		if (qh->num_hs_transfers > 1)
			qh->hs_transfers[qh->num_hs_transfers - 1].duration_us =
				HS_USECS_ISO(end_count);

		/*
		 * Assign start us.  The call below to dwc2_hs_pmap_schedule()
		 * will start with these numbers but may adjust within the same
		 * microframe.
		 */
		qh->hs_transfers[0].start_schedule_us =
			ssplit_s_uframe * DWC2_HS_PERIODIC_US_PER_UFRAME;
		for (i = 1; i < qh->num_hs_transfers; i++)
			qh->hs_transfers[i].start_schedule_us =
				((second_s_uframe + i - 1) %
				 DWC2_HS_SCHEDULE_UFRAMES) *
				DWC2_HS_PERIODIC_US_PER_UFRAME;

		/* Try to schedule with filled in hs_transfers above */
		for (i = 0; i < qh->num_hs_transfers; i++) {
			err = dwc2_hs_pmap_schedule(hsotg, qh, true, i);
			if (err)
				break;
		}

		/* If we scheduled all w/out breaking out then we're all good */
		if (i == qh->num_hs_transfers)
			break;

		for (; i >= 0; i--)
			dwc2_hs_pmap_unschedule(hsotg, qh, i);

		if (qh->schedule_low_speed)
			dwc2_ls_pmap_unschedule(hsotg, qh);

		/* Try again starting in the next microframe */
		ls_search_slice = (start_s_uframe + 1) * DWC2_SLICES_PER_UFRAME;
	}

	if (ls_search_slice >= DWC2_LS_SCHEDULE_SLICES)
		return -ENOSPC;

	return 0;
}

/**
 * dwc2_uframe_schedule_hs - Schedule a QH for a periodic high speed xfer.
 *
 * Basically this just wraps dwc2_hs_pmap_schedule() to provide a clean
 * interface.
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static int dwc2_uframe_schedule_hs(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	/* In non-split host and device time are the same */
	WARN_ON(qh->host_us != qh->device_us);
	WARN_ON(qh->host_interval != qh->device_interval);
	WARN_ON(qh->num_hs_transfers != 1);

	/* We'll have one transfer; init start to 0 before calling scheduler */
	qh->hs_transfers[0].start_schedule_us = 0;
	qh->hs_transfers[0].duration_us = qh->host_us;

	return dwc2_hs_pmap_schedule(hsotg, qh, false, 0);
}

/**
 * dwc2_uframe_schedule_ls - Schedule a QH for a periodic low/full speed xfer.
 *
 * Basically this just wraps dwc2_ls_pmap_schedule() to provide a clean
 * interface.
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static int dwc2_uframe_schedule_ls(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	/* In non-split host and device time are the same */
	WARN_ON(qh->host_us != qh->device_us);
	WARN_ON(qh->host_interval != qh->device_interval);
	WARN_ON(!qh->schedule_low_speed);

	/* Run on the main low speed schedule (no split = no hub = no TT) */
	return dwc2_ls_pmap_schedule(hsotg, qh, 0);
}

/**
 * dwc2_uframe_schedule - Schedule a QH for a periodic xfer.
 *
 * Calls one of the 3 sub-function depending on what type of transfer this QH
 * is for.  Also adds some printing.
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static int dwc2_uframe_schedule(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int ret;

	if (qh->dev_speed == USB_SPEED_HIGH)
		ret = dwc2_uframe_schedule_hs(hsotg, qh);
	else if (!qh->do_split)
		ret = dwc2_uframe_schedule_ls(hsotg, qh);
	else
		ret = dwc2_uframe_schedule_split(hsotg, qh);

	if (ret)
		dwc2_sch_dbg(hsotg, "QH=%p Failed to schedule %d\n", qh, ret);
	else
		dwc2_qh_schedule_print(hsotg, qh);

	return ret;
}

/**
 * dwc2_uframe_unschedule - Undoes dwc2_uframe_schedule().
 *
 * @hsotg:       The HCD state structure for the DWC OTG controller.
 * @qh:          QH for the periodic transfer.
 */
static void dwc2_uframe_unschedule(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int i;

	for (i = 0; i < qh->num_hs_transfers; i++)
		dwc2_hs_pmap_unschedule(hsotg, qh, i);

	if (qh->schedule_low_speed)
		dwc2_ls_pmap_unschedule(hsotg, qh);

	dwc2_sch_dbg(hsotg, "QH=%p Unscheduled\n", qh);
}

/**
 * dwc2_pick_first_frame() - Choose 1st frame for qh that's already scheduled
 *
 * Takes a qh that has already been scheduled (which means we know we have the
 * bandwdith reserved for us) and set the next_active_frame and the
 * start_active_frame.
 *
 * This is expected to be called on qh's that weren't previously actively
 * running.  It just picks the next frame that we can fit into without any
 * thought about the past.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for a periodic endpoint
 *
 */
static void dwc2_pick_first_frame(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	u16 frame_number;
	u16 earliest_frame;
	u16 next_active_frame;
	u16 relative_frame;
	u16 interval;

	/*
	 * Use the real frame number rather than the cached value as of the
	 * last SOF to give us a little extra slop.
	 */
	frame_number = dwc2_hcd_get_frame_number(hsotg);

	/*
	 * We wouldn't want to start any earlier than the next frame just in
	 * case the frame number ticks as we're doing this calculation.
	 *
	 * NOTE: if we could quantify how long till we actually get scheduled
	 * we might be able to avoid the "+ 1" by looking at the upper part of
	 * HFNUM (the FRREM field).  For now we'll just use the + 1 though.
	 */
	earliest_frame = dwc2_frame_num_inc(frame_number, 1);
	next_active_frame = earliest_frame;

	/* Get the "no microframe schduler" out of the way... */
	if (!hsotg->params.uframe_sched) {
		if (qh->do_split)
			/* Splits are active at microframe 0 minus 1 */
			next_active_frame |= 0x7;
		goto exit;
	}

	if (qh->dev_speed == USB_SPEED_HIGH || qh->do_split) {
		/*
		 * We're either at high speed or we're doing a split (which
		 * means we're talking high speed to a hub).  In any case
		 * the first frame should be based on when the first scheduled
		 * event is.
		 */
		WARN_ON(qh->num_hs_transfers < 1);

		relative_frame = qh->hs_transfers[0].start_schedule_us /
				 DWC2_HS_PERIODIC_US_PER_UFRAME;

		/* Adjust interval as per high speed schedule */
		interval = gcd(qh->host_interval, DWC2_HS_SCHEDULE_UFRAMES);

	} else {
		/*
		 * Low or full speed directly on dwc2.  Just about the same
		 * as high speed but on a different schedule and with slightly
		 * different adjustments.  Note that this works because when
		 * the host and device are both low speed then frames in the
		 * controller tick at low speed.
		 */
		relative_frame = qh->ls_start_schedule_slice /
				 DWC2_LS_PERIODIC_SLICES_PER_FRAME;
		interval = gcd(qh->host_interval, DWC2_LS_SCHEDULE_FRAMES);
	}

	/* Scheduler messed up if frame is past interval */
	WARN_ON(relative_frame >= interval);

	/*
	 * We know interval must divide (HFNUM_MAX_FRNUM + 1) now that we've
	 * done the gcd(), so it's safe to move to the beginning of the current
	 * interval like this.
	 *
	 * After this we might be before earliest_frame, but don't worry,
	 * we'll fix it...
	 */
	next_active_frame = (next_active_frame / interval) * interval;

	/*
	 * Actually choose to start at the frame number we've been
	 * scheduled for.
	 */
	next_active_frame = dwc2_frame_num_inc(next_active_frame,
					       relative_frame);

	/*
	 * We actually need 1 frame before since the next_active_frame is
	 * the frame number we'll be put on the ready list and we won't be on
	 * the bus until 1 frame later.
	 */
	next_active_frame = dwc2_frame_num_dec(next_active_frame, 1);

	/*
	 * By now we might actually be before the earliest_frame.  Let's move
	 * up intervals until we're not.
	 */
	while (dwc2_frame_num_gt(earliest_frame, next_active_frame))
		next_active_frame = dwc2_frame_num_inc(next_active_frame,
						       interval);

exit:
	qh->next_active_frame = next_active_frame;
	qh->start_active_frame = next_active_frame;

	dwc2_sch_vdbg(hsotg, "QH=%p First fn=%04x nxt=%04x\n",
		      qh, frame_number, qh->next_active_frame);
}

/**
 * dwc2_do_reserve() - Make a periodic reservation
 *
 * Try to allocate space in the periodic schedule.  Depending on parameters
 * this might use the microframe scheduler or the dumb scheduler.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer.
 *
 * Returns: 0 upon success; error upon failure.
 */
static int dwc2_do_reserve(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;

	if (hsotg->params.uframe_sched) {
		status = dwc2_uframe_schedule(hsotg, qh);
	} else {
		status = dwc2_periodic_channel_available(hsotg);
		if (status) {
			dev_info(hsotg->dev,
				 "%s: No host channel available for periodic transfer\n",
				 __func__);
			return status;
		}

		status = dwc2_check_periodic_bandwidth(hsotg, qh);
	}

	if (status) {
		dev_dbg(hsotg->dev,
			"%s: Insufficient periodic bandwidth for periodic transfer\n",
			__func__);
		return status;
	}

	if (!hsotg->params.uframe_sched)
		/* Reserve periodic channel */
		hsotg->periodic_channels++;

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs += qh->host_us;

	dwc2_pick_first_frame(hsotg, qh);

	return 0;
}

/**
 * dwc2_do_unreserve() - Actually release the periodic reservation
 *
 * This function actually releases the periodic bandwidth that was reserved
 * by the given qh.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer.
 */
static void dwc2_do_unreserve(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	assert_spin_locked(&hsotg->lock);

	WARN_ON(!qh->unreserve_pending);

	/* No more unreserve pending--we're doing it */
	qh->unreserve_pending = false;

	if (WARN_ON(!list_empty(&qh->qh_list_entry)))
		list_del_init(&qh->qh_list_entry);

	/* Update claimed usecs per (micro)frame */
	hsotg->periodic_usecs -= qh->host_us;

	if (hsotg->params.uframe_sched) {
		dwc2_uframe_unschedule(hsotg, qh);
	} else {
		/* Release periodic channel reservation */
		hsotg->periodic_channels--;
	}
}

/**
 * dwc2_unreserve_timer_fn() - Timer function to release periodic reservation
 *
 * According to the kernel doc for usb_submit_urb() (specifically the part about
 * "Reserved Bandwidth Transfers"), we need to keep a reservation active as
 * long as a device driver keeps submitting.  Since we're using HCD_BH to give
 * back the URB we need to give the driver a little bit of time before we
 * release the reservation.  This worker is called after the appropriate
 * delay.
 *
 * @work: Pointer to a qh unreserve_work.
 */
static void dwc2_unreserve_timer_fn(struct timer_list *t)
{
	struct dwc2_qh *qh = from_timer(qh, t, unreserve_timer);
	struct dwc2_hsotg *hsotg = qh->hsotg;
	unsigned long flags;

	/*
	 * Wait for the lock, or for us to be scheduled again.  We
	 * could be scheduled again if:
	 * - We started executing but didn't get the lock yet.
	 * - A new reservation came in, but cancel didn't take effect
	 *   because we already started executing.
	 * - The timer has been kicked again.
	 * In that case cancel and wait for the next call.
	 */
	while (!spin_trylock_irqsave(&hsotg->lock, flags)) {
		if (timer_pending(&qh->unreserve_timer))
			return;
	}

	/*
	 * Might be no more unreserve pending if:
	 * - We started executing but didn't get the lock yet.
	 * - A new reservation came in, but cancel didn't take effect
	 *   because we already started executing.
	 *
	 * We can't put this in the loop above because unreserve_pending needs
	 * to be accessed under lock, so we can only check it once we got the
	 * lock.
	 */
	if (qh->unreserve_pending)
		dwc2_do_unreserve(hsotg, qh);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/**
 * dwc2_check_max_xfer_size() - Checks that the max transfer size allowed in a
 * host channel is large enough to handle the maximum data transfer in a single
 * (micro)frame for a periodic transfer
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for a periodic endpoint
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_check_max_xfer_size(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh)
{
	u32 max_xfer_size;
	u32 max_channel_xfer_size;
	int status = 0;

	max_xfer_size = dwc2_max_packet(qh->maxp) * dwc2_hb_mult(qh->maxp);
	max_channel_xfer_size = hsotg->params.max_transfer_size;

	if (max_xfer_size > max_channel_xfer_size) {
		dev_err(hsotg->dev,
			"%s: Periodic xfer length %d > max xfer length for channel %d\n",
			__func__, max_xfer_size, max_channel_xfer_size);
		status = -ENOSPC;
	}

	return status;
}

/**
 * dwc2_schedule_periodic() - Schedules an interrupt or isochronous transfer in
 * the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    QH for the periodic transfer. The QH should already contain the
 *         scheduling information.
 *
 * Return: 0 if successful, negative error code otherwise
 */
static int dwc2_schedule_periodic(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;

	status = dwc2_check_max_xfer_size(hsotg, qh);
	if (status) {
		dev_dbg(hsotg->dev,
			"%s: Channel max transfer size too small for periodic transfer\n",
			__func__);
		return status;
	}

	/* Cancel pending unreserve; if canceled OK, unreserve was pending */
	if (del_timer(&qh->unreserve_timer))
		WARN_ON(!qh->unreserve_pending);

	/*
	 * Only need to reserve if there's not an unreserve pending, since if an
	 * unreserve is pending then by definition our old reservation is still
	 * valid.  Unreserve might still be pending even if we didn't cancel if
	 * dwc2_unreserve_timer_fn() already started.  Code in the timer handles
	 * that case.
	 */
	if (!qh->unreserve_pending) {
		status = dwc2_do_reserve(hsotg, qh);
		if (status)
			return status;
	} else {
		/*
		 * It might have been a while, so make sure that frame_number
		 * is still good.  Note: we could also try to use the similar
		 * dwc2_next_periodic_start() but that schedules much more
		 * tightly and we might need to hurry and queue things up.
		 */
		if (dwc2_frame_num_le(qh->next_active_frame,
				      hsotg->frame_number))
			dwc2_pick_first_frame(hsotg, qh);
	}

	qh->unreserve_pending = 0;

	if (hsotg->params.dma_desc_enable)
		/* Don't rely on SOF and start in ready schedule */
		list_add_tail(&qh->qh_list_entry, &hsotg->periodic_sched_ready);
	else
		/* Always start in inactive schedule */
		list_add_tail(&qh->qh_list_entry,
			      &hsotg->periodic_sched_inactive);

	return 0;
}

/**
 * dwc2_deschedule_periodic() - Removes an interrupt or isochronous transfer
 * from the periodic schedule
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:	   QH for the periodic transfer
 */
static void dwc2_deschedule_periodic(struct dwc2_hsotg *hsotg,
				     struct dwc2_qh *qh)
{
	bool did_modify;

	assert_spin_locked(&hsotg->lock);

	/*
	 * Schedule the unreserve to happen in a little bit.  Cases here:
	 * - Unreserve worker might be sitting there waiting to grab the lock.
	 *   In this case it will notice it's been schedule again and will
	 *   quit.
	 * - Unreserve worker might not be scheduled.
	 *
	 * We should never already be scheduled since dwc2_schedule_periodic()
	 * should have canceled the scheduled unreserve timer (hence the
	 * warning on did_modify).
	 *
	 * We add + 1 to the timer to guarantee that at least 1 jiffy has
	 * passed (otherwise if the jiffy counter might tick right after we
	 * read it and we'll get no delay).
	 */
	did_modify = mod_timer(&qh->unreserve_timer,
			       jiffies + DWC2_UNRESERVE_DELAY + 1);
	WARN_ON(did_modify);
	qh->unreserve_pending = 1;

	list_del_init(&qh->qh_list_entry);
}

/**
 * dwc2_wait_timer_fn() - Timer function to re-queue after waiting
 *
 * As per the spec, a NAK indicates that "a function is temporarily unable to
 * transmit or receive data, but will eventually be able to do so without need
 * of host intervention".
 *
 * That means that when we encounter a NAK we're supposed to retry.
 *
 * ...but if we retry right away (from the interrupt handler that saw the NAK)
 * then we can end up with an interrupt storm (if the other side keeps NAKing
 * us) because on slow enough CPUs it could take us longer to get out of the
 * interrupt routine than it takes for the device to send another NAK.  That
 * leads to a constant stream of NAK interrupts and the CPU locks.
 *
 * ...so instead of retrying right away in the case of a NAK we'll set a timer
 * to retry some time later.  This function handles that timer and moves the
 * qh back to the "inactive" list, then queues transactions.
 *
 * @t: Pointer to wait_timer in a qh.
 */
static void dwc2_wait_timer_fn(struct timer_list *t)
{
	struct dwc2_qh *qh = from_timer(qh, t, wait_timer);
	struct dwc2_hsotg *hsotg = qh->hsotg;
	unsigned long flags;

	spin_lock_irqsave(&hsotg->lock, flags);

	/*
	 * We'll set wait_timer_cancel to true if we want to cancel this
	 * operation in dwc2_hcd_qh_unlink().
	 */
	if (!qh->wait_timer_cancel) {
		enum dwc2_transaction_type tr_type;

		qh->want_wait = false;

		list_move(&qh->qh_list_entry,
			  &hsotg->non_periodic_sched_inactive);

		tr_type = dwc2_hcd_select_transactions(hsotg);
		if (tr_type != DWC2_TRANSACTION_NONE)
			dwc2_hcd_queue_transactions(hsotg, tr_type);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

/**
 * dwc2_qh_init() - Initializes a QH structure
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to init
 * @urb:   Holds the information about the device/endpoint needed to initialize
 *         the QH
 * @mem_flags: Flags for allocating memory.
 */
static void dwc2_qh_init(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			 struct dwc2_hcd_urb *urb, gfp_t mem_flags)
{
	int dev_speed = dwc2_host_get_speed(hsotg, urb->priv);
	u8 ep_type = dwc2_hcd_get_pipe_type(&urb->pipe_info);
	bool ep_is_in = !!dwc2_hcd_is_pipe_in(&urb->pipe_info);
	bool ep_is_isoc = (ep_type == USB_ENDPOINT_XFER_ISOC);
	bool ep_is_int = (ep_type == USB_ENDPOINT_XFER_INT);
	u32 hprt = dwc2_readl(hsotg->regs + HPRT0);
	u32 prtspd = (hprt & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
	bool do_split = (prtspd == HPRT0_SPD_HIGH_SPEED &&
			 dev_speed != USB_SPEED_HIGH);
	int maxp = dwc2_hcd_get_mps(&urb->pipe_info);
	int bytecount = dwc2_hb_mult(maxp) * dwc2_max_packet(maxp);
	char *speed, *type;

	/* Initialize QH */
	qh->hsotg = hsotg;
	timer_setup(&qh->unreserve_timer, dwc2_unreserve_timer_fn, 0);
	timer_setup(&qh->wait_timer, dwc2_wait_timer_fn, 0);
	qh->ep_type = ep_type;
	qh->ep_is_in = ep_is_in;

	qh->data_toggle = DWC2_HC_PID_DATA0;
	qh->maxp = maxp;
	INIT_LIST_HEAD(&qh->qtd_list);
	INIT_LIST_HEAD(&qh->qh_list_entry);

	qh->do_split = do_split;
	qh->dev_speed = dev_speed;

	if (ep_is_int || ep_is_isoc) {
		/* Compute scheduling parameters once and save them */
		int host_speed = do_split ? USB_SPEED_HIGH : dev_speed;
		struct dwc2_tt *dwc_tt = dwc2_host_get_tt_info(hsotg, urb->priv,
							       mem_flags,
							       &qh->ttport);
		int device_ns;

		qh->dwc_tt = dwc_tt;

		qh->host_us = NS_TO_US(usb_calc_bus_time(host_speed, ep_is_in,
				       ep_is_isoc, bytecount));
		device_ns = usb_calc_bus_time(dev_speed, ep_is_in,
					      ep_is_isoc, bytecount);

		if (do_split && dwc_tt)
			device_ns += dwc_tt->usb_tt->think_time;
		qh->device_us = NS_TO_US(device_ns);

		qh->device_interval = urb->interval;
		qh->host_interval = urb->interval * (do_split ? 8 : 1);

		/*
		 * Schedule low speed if we're running the host in low or
		 * full speed OR if we've got a "TT" to deal with to access this
		 * device.
		 */
		qh->schedule_low_speed = prtspd != HPRT0_SPD_HIGH_SPEED ||
					 dwc_tt;

		if (do_split) {
			/* We won't know num transfers until we schedule */
			qh->num_hs_transfers = -1;
		} else if (dev_speed == USB_SPEED_HIGH) {
			qh->num_hs_transfers = 1;
		} else {
			qh->num_hs_transfers = 0;
		}

		/* We'll schedule later when we have something to do */
	}

	switch (dev_speed) {
	case USB_SPEED_LOW:
		speed = "low";
		break;
	case USB_SPEED_FULL:
		speed = "full";
		break;
	case USB_SPEED_HIGH:
		speed = "high";
		break;
	default:
		speed = "?";
		break;
	}

	switch (qh->ep_type) {
	case USB_ENDPOINT_XFER_ISOC:
		type = "isochronous";
		break;
	case USB_ENDPOINT_XFER_INT:
		type = "interrupt";
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		type = "control";
		break;
	case USB_ENDPOINT_XFER_BULK:
		type = "bulk";
		break;
	default:
		type = "?";
		break;
	}

	dwc2_sch_dbg(hsotg, "QH=%p Init %s, %s speed, %d bytes:\n", qh, type,
		     speed, bytecount);
	dwc2_sch_dbg(hsotg, "QH=%p ...addr=%d, ep=%d, %s\n", qh,
		     dwc2_hcd_get_dev_addr(&urb->pipe_info),
		     dwc2_hcd_get_ep_num(&urb->pipe_info),
		     ep_is_in ? "IN" : "OUT");
	if (ep_is_int || ep_is_isoc) {
		dwc2_sch_dbg(hsotg,
			     "QH=%p ...duration: host=%d us, device=%d us\n",
			     qh, qh->host_us, qh->device_us);
		dwc2_sch_dbg(hsotg, "QH=%p ...interval: host=%d, device=%d\n",
			     qh, qh->host_interval, qh->device_interval);
		if (qh->schedule_low_speed)
			dwc2_sch_dbg(hsotg, "QH=%p ...low speed schedule=%p\n",
				     qh, dwc2_get_ls_map(hsotg, qh));
	}
}

/**
 * dwc2_hcd_qh_create() - Allocates and initializes a QH
 *
 * @hsotg:        The HCD state structure for the DWC OTG controller
 * @urb:          Holds the information about the device/endpoint needed
 *                to initialize the QH
 * @atomic_alloc: Flag to do atomic allocation if needed
 *
 * Return: Pointer to the newly allocated QH, or NULL on error
 */
struct dwc2_qh *dwc2_hcd_qh_create(struct dwc2_hsotg *hsotg,
				   struct dwc2_hcd_urb *urb,
					  gfp_t mem_flags)
{
	struct dwc2_qh *qh;

	if (!urb->priv)
		return NULL;

	/* Allocate memory */
	qh = kzalloc(sizeof(*qh), mem_flags);
	if (!qh)
		return NULL;

	dwc2_qh_init(hsotg, qh, urb, mem_flags);

	if (hsotg->params.dma_desc_enable &&
	    dwc2_hcd_qh_init_ddma(hsotg, qh, mem_flags) < 0) {
		dwc2_hcd_qh_free(hsotg, qh);
		return NULL;
	}

	return qh;
}

/**
 * dwc2_hcd_qh_free() - Frees the QH
 *
 * @hsotg: HCD instance
 * @qh:    The QH to free
 *
 * QH should already be removed from the list. QTD list should already be empty
 * if called from URB Dequeue.
 *
 * Must NOT be called with interrupt disabled or spinlock held
 */
void dwc2_hcd_qh_free(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	/* Make sure any unreserve work is finished. */
	if (del_timer_sync(&qh->unreserve_timer)) {
		unsigned long flags;

		spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_do_unreserve(hsotg, qh);
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	/*
	 * We don't have the lock so we can safely wait until the wait timer
	 * finishes.  Of course, at this point in time we'd better have set
	 * wait_timer_active to false so if this timer was still pending it
	 * won't do anything anyway, but we want it to finish before we free
	 * memory.
	 */
	del_timer_sync(&qh->wait_timer);

	dwc2_host_put_tt_info(hsotg, qh->dwc_tt);

	if (qh->desc_list)
		dwc2_hcd_qh_free_ddma(hsotg, qh);
	kfree(qh);
}

/**
 * dwc2_hcd_qh_add() - Adds a QH to either the non periodic or periodic
 * schedule if it is not already in the schedule. If the QH is already in
 * the schedule, no action is taken.
 *
 * @hsotg: The HCD state structure for the DWC OTG controller
 * @qh:    The QH to add
 *
 * Return: 0 if successful, negative error code otherwise
 */
int dwc2_hcd_qh_add(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	int status;
	u32 intr_mask;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (!list_empty(&qh->qh_list_entry))
		/* QH already in a schedule */
		return 0;

	/* Add the new QH to the appropriate schedule */
	if (dwc2_qh_is_non_per(qh)) {
		/* Schedule right away */
		qh->start_active_frame = hsotg->frame_number;
		qh->next_active_frame = qh->start_active_frame;

		if (qh->want_wait) {
			list_add_tail(&qh->qh_list_entry,
				      &hsotg->non_periodic_sched_waiting);
			qh->wait_timer_cancel = false;
			mod_timer(&qh->wait_timer,
				  jiffies + DWC2_RETRY_WAIT_DELAY + 1);
		} else {
			list_add_tail(&qh->qh_list_entry,
				      &hsotg->non_periodic_sched_inactive);
		}
		return 0;
	}

	status = dwc2_schedule_periodic(hsotg, qh);
	if (status)
		return status;
	if (!hsotg->periodic_qh_count) {
		intr_mask = dwc2_readl(hsotg->regs + GINTMSK);
		intr_mask |= GINTSTS_SOF;
		dwc2_writel(intr_mask, hsotg->regs + GINTMSK);
	}
	hsotg->periodic_qh_count++;

	return 0;
}

/**
 * dwc2_hcd_qh_unlink() - Removes a QH from either the non-periodic or periodic
 * schedule. Memory is not freed.
 *
 * @hsotg: The HCD state structure
 * @qh:    QH to remove from schedule
 */
void dwc2_hcd_qh_unlink(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	u32 intr_mask;

	dev_vdbg(hsotg->dev, "%s()\n", __func__);

	/* If the wait_timer is pending, this will stop it from acting */
	qh->wait_timer_cancel = true;

	if (list_empty(&qh->qh_list_entry))
		/* QH is not in a schedule */
		return;

	if (dwc2_qh_is_non_per(qh)) {
		if (hsotg->non_periodic_qh_ptr == &qh->qh_list_entry)
			hsotg->non_periodic_qh_ptr =
					hsotg->non_periodic_qh_ptr->next;
		list_del_init(&qh->qh_list_entry);
		return;
	}

	dwc2_deschedule_periodic(hsotg, qh);
	hsotg->periodic_qh_count--;
	if (!hsotg->periodic_qh_count &&
	    !hsotg->params.dma_desc_enable) {
		intr_mask = dwc2_readl(hsotg->regs + GINTMSK);
		intr_mask &= ~GINTSTS_SOF;
		dwc2_writel(intr_mask, hsotg->regs + GINTMSK);
	}
}

/**
 * dwc2_next_for_periodic_split() - Set next_active_frame midway thru a split.
 *
 * This is called for setting next_active_frame for periodic splits for all but
 * the first packet of the split.  Confusing?  I thought so...
 *
 * Periodic splits are single low/full speed transfers that we end up splitting
 * up into several high speed transfers.  They always fit into one full (1 ms)
 * frame but might be split over several microframes (125 us each).  We to put
 * each of the parts on a very specific high speed frame.
 *
 * This function figures out where the next active uFrame needs to be.
 *
 * @hsotg:        The HCD state structure
 * @qh:           QH for the periodic transfer.
 * @frame_number: The current frame number.
 *
 * Return: number missed by (or 0 if we didn't miss).
 */
static int dwc2_next_for_periodic_split(struct dwc2_hsotg *hsotg,
					struct dwc2_qh *qh, u16 frame_number)
{
	u16 old_frame = qh->next_active_frame;
	u16 prev_frame_number = dwc2_frame_num_dec(frame_number, 1);
	int missed = 0;
	u16 incr;

	/*
	 * See dwc2_uframe_schedule_split() for split scheduling.
	 *
	 * Basically: increment 1 normally, but 2 right after the start split
	 * (except for ISOC out).
	 */
	if (old_frame == qh->start_active_frame &&
	    !(qh->ep_type == USB_ENDPOINT_XFER_ISOC && !qh->ep_is_in))
		incr = 2;
	else
		incr = 1;

	qh->next_active_frame = dwc2_frame_num_inc(old_frame, incr);

	/*
	 * Note that it's OK for frame_number to be 1 frame past
	 * next_active_frame.  Remember that next_active_frame is supposed to
	 * be 1 frame _before_ when we want to be scheduled.  If we're 1 frame
	 * past it just means schedule ASAP.
	 *
	 * It's _not_ OK, however, if we're more than one frame past.
	 */
	if (dwc2_frame_num_gt(prev_frame_number, qh->next_active_frame)) {
		/*
		 * OOPS, we missed.  That's actually pretty bad since
		 * the hub will be unhappy; try ASAP I guess.
		 */
		missed = dwc2_frame_num_dec(prev_frame_number,
					    qh->next_active_frame);
		qh->next_active_frame = frame_number;
	}

	return missed;
}

/**
 * dwc2_next_periodic_start() - Set next_active_frame for next transfer start
 *
 * This is called for setting next_active_frame for a periodic transfer for
 * all cases other than midway through a periodic split.  This will also update
 * start_active_frame.
 *
 * Since we _always_ keep start_active_frame as the start of the previous
 * transfer this is normally pretty easy: we just add our interval to
 * start_active_frame and we've got our answer.
 *
 * The tricks come into play if we miss.  In that case we'll look for the next
 * slot we can fit into.
 *
 * @hsotg:        The HCD state structure
 * @qh:           QH for the periodic transfer.
 * @frame_number: The current frame number.
 *
 * Return: number missed by (or 0 if we didn't miss).
 */
static int dwc2_next_periodic_start(struct dwc2_hsotg *hsotg,
				    struct dwc2_qh *qh, u16 frame_number)
{
	int missed = 0;
	u16 interval = qh->host_interval;
	u16 prev_frame_number = dwc2_frame_num_dec(frame_number, 1);

	qh->start_active_frame = dwc2_frame_num_inc(qh->start_active_frame,
						    interval);

	/*
	 * The dwc2_frame_num_gt() function used below won't work terribly well
	 * with if we just incremented by a really large intervals since the
	 * frame counter only goes to 0x3fff.  It's terribly unlikely that we
	 * will have missed in this case anyway.  Just go to exit.  If we want
	 * to try to do better we'll need to keep track of a bigger counter
	 * somewhere in the driver and handle overflows.
	 */
	if (interval >= 0x1000)
		goto exit;

	/*
	 * Test for misses, which is when it's too late to schedule.
	 *
	 * A few things to note:
	 * - We compare against prev_frame_number since start_active_frame
	 *   and next_active_frame are always 1 frame before we want things
	 *   to be active and we assume we can still get scheduled in the
	 *   current frame number.
	 * - It's possible for start_active_frame (now incremented) to be
	 *   next_active_frame if we got an EO MISS (even_odd miss) which
	 *   basically means that we detected there wasn't enough time for
	 *   the last packet and dwc2_hc_set_even_odd_frame() rescheduled us
	 *   at the last second.  We want to make sure we don't schedule
	 *   another transfer for the same frame.  My test webcam doesn't seem
	 *   terribly upset by missing a transfer but really doesn't like when
	 *   we do two transfers in the same frame.
	 * - Some misses are expected.  Specifically, in order to work
	 *   perfectly dwc2 really needs quite spectacular interrupt latency
	 *   requirements.  It needs to be able to handle its interrupts
	 *   completely within 125 us of them being asserted. That not only
	 *   means that the dwc2 interrupt handler needs to be fast but it
	 *   means that nothing else in the system has to block dwc2 for a long
	 *   time.  We can help with the dwc2 parts of this, but it's hard to
	 *   guarantee that a system will have interrupt latency < 125 us, so
	 *   we have to be robust to some misses.
	 */
	if (qh->start_active_frame == qh->next_active_frame ||
	    dwc2_frame_num_gt(prev_frame_number, qh->start_active_frame)) {
		u16 ideal_start = qh->start_active_frame;
		int periods_in_map;

		/*
		 * Adjust interval as per gcd with map size.
		 * See pmap_schedule() for more details here.
		 */
		if (qh->do_split || qh->dev_speed == USB_SPEED_HIGH)
			periods_in_map = DWC2_HS_SCHEDULE_UFRAMES;
		else
			periods_in_map = DWC2_LS_SCHEDULE_FRAMES;
		interval = gcd(interval, periods_in_map);

		do {
			qh->start_active_frame = dwc2_frame_num_inc(
				qh->start_active_frame, interval);
		} while (dwc2_frame_num_gt(prev_frame_number,
					   qh->start_active_frame));

		missed = dwc2_frame_num_dec(qh->start_active_frame,
					    ideal_start);
	}

exit:
	qh->next_active_frame = qh->start_active_frame;

	return missed;
}

/*
 * Deactivates a QH. For non-periodic QHs, removes the QH from the active
 * non-periodic schedule. The QH is added to the inactive non-periodic
 * schedule if any QTDs are still attached to the QH.
 *
 * For periodic QHs, the QH is removed from the periodic queued schedule. If
 * there are any QTDs still attached to the QH, the QH is added to either the
 * periodic inactive schedule or the periodic ready schedule and its next
 * scheduled frame is calculated. The QH is placed in the ready schedule if
 * the scheduled frame has been reached already. Otherwise it's placed in the
 * inactive schedule. If there are no QTDs attached to the QH, the QH is
 * completely removed from the periodic schedule.
 */
void dwc2_hcd_qh_deactivate(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh,
			    int sched_next_periodic_split)
{
	u16 old_frame = qh->next_active_frame;
	u16 frame_number;
	int missed;

	if (dbg_qh(qh))
		dev_vdbg(hsotg->dev, "%s()\n", __func__);

	if (dwc2_qh_is_non_per(qh)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		if (!list_empty(&qh->qtd_list))
			/* Add back to inactive/waiting non-periodic schedule */
			dwc2_hcd_qh_add(hsotg, qh);
		return;
	}

	/*
	 * Use the real frame number rather than the cached value as of the
	 * last SOF just to get us a little closer to reality.  Note that
	 * means we don't actually know if we've already handled the SOF
	 * interrupt for this frame.
	 */
	frame_number = dwc2_hcd_get_frame_number(hsotg);

	if (sched_next_periodic_split)
		missed = dwc2_next_for_periodic_split(hsotg, qh, frame_number);
	else
		missed = dwc2_next_periodic_start(hsotg, qh, frame_number);

	dwc2_sch_vdbg(hsotg,
		      "QH=%p next(%d) fn=%04x, sch=%04x=>%04x (%+d) miss=%d %s\n",
		     qh, sched_next_periodic_split, frame_number, old_frame,
		     qh->next_active_frame,
		     dwc2_frame_num_dec(qh->next_active_frame, old_frame),
		missed, missed ? "MISS" : "");

	if (list_empty(&qh->qtd_list)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
		return;
	}

	/*
	 * Remove from periodic_sched_queued and move to
	 * appropriate queue
	 *
	 * Note: we purposely use the frame_number from the "hsotg" structure
	 * since we know SOF interrupt will handle future frames.
	 */
	if (dwc2_frame_num_le(qh->next_active_frame, hsotg->frame_number))
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_ready);
	else
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_inactive);
}

/**
 * dwc2_hcd_qtd_init() - Initializes a QTD structure
 *
 * @qtd: The QTD to initialize
 * @urb: The associated URB
 */
void dwc2_hcd_qtd_init(struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb)
{
	qtd->urb = urb;
	if (dwc2_hcd_get_pipe_type(&urb->pipe_info) ==
			USB_ENDPOINT_XFER_CONTROL) {
		/*
		 * The only time the QTD data toggle is used is on the data
		 * phase of control transfers. This phase always starts with
		 * DATA1.
		 */
		qtd->data_toggle = DWC2_HC_PID_DATA1;
		qtd->control_phase = DWC2_CONTROL_SETUP;
	}

	/* Start split */
	qtd->complete_split = 0;
	qtd->isoc_split_pos = DWC2_HCSPLT_XACTPOS_ALL;
	qtd->isoc_split_offset = 0;
	qtd->in_process = 0;

	/* Store the qtd ptr in the urb to reference the QTD */
	urb->qtd = qtd;
}

/**
 * dwc2_hcd_qtd_add() - Adds a QTD to the QTD-list of a QH
 *			Caller must hold driver lock.
 *
 * @hsotg:        The DWC HCD structure
 * @qtd:          The QTD to add
 * @qh:           Queue head to add qtd to
 *
 * Return: 0 if successful, negative error code otherwise
 *
 * If the QH to which the QTD is added is not currently scheduled, it is placed
 * into the proper schedule based on its EP type.
 */
int dwc2_hcd_qtd_add(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
		     struct dwc2_qh *qh)
{
	int retval;

	if (unlikely(!qh)) {
		dev_err(hsotg->dev, "%s: Invalid QH\n", __func__);
		retval = -EINVAL;
		goto fail;
	}

	retval = dwc2_hcd_qh_add(hsotg, qh);
	if (retval)
		goto fail;

	qtd->qh = qh;
	list_add_tail(&qtd->qtd_list_entry, &qh->qtd_list);

	return 0;
fail:
	return retval;
}
