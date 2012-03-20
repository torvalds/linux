/*
 * tcm-sita.c
 *
 * SImple Tiler Allocator (SiTA): 2D and 1D allocation(reservation) algorithm
 *
 * Authors: Ravi Ramachandra <r.ramachandra@ti.com>,
 *          Lajos Molnar <molnar@ti.com>
 *
 * Copyright (C) 2009-2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "tcm-sita.h"

#define ALIGN_DOWN(value, align) ((value) & ~((align) - 1))

/* Individual selection criteria for different scan areas */
static s32 CR_L2R_T2B = CR_BIAS_HORIZONTAL;
static s32 CR_R2L_T2B = CR_DIAGONAL_BALANCE;

/*********************************************
 *	TCM API - Sita Implementation
 *********************************************/
static s32 sita_reserve_2d(struct tcm *tcm, u16 h, u16 w, u8 align,
			   struct tcm_area *area);
static s32 sita_reserve_1d(struct tcm *tcm, u32 slots, struct tcm_area *area);
static s32 sita_free(struct tcm *tcm, struct tcm_area *area);
static void sita_deinit(struct tcm *tcm);

/*********************************************
 *	Main Scanner functions
 *********************************************/
static s32 scan_areas_and_find_fit(struct tcm *tcm, u16 w, u16 h, u16 align,
				   struct tcm_area *area);

static s32 scan_l2r_t2b(struct tcm *tcm, u16 w, u16 h, u16 align,
			struct tcm_area *field, struct tcm_area *area);

static s32 scan_r2l_t2b(struct tcm *tcm, u16 w, u16 h, u16 align,
			struct tcm_area *field, struct tcm_area *area);

static s32 scan_r2l_b2t_one_dim(struct tcm *tcm, u32 num_slots,
			struct tcm_area *field, struct tcm_area *area);

/*********************************************
 *	Support Infrastructure Methods
 *********************************************/
static s32 is_area_free(struct tcm_area ***map, u16 x0, u16 y0, u16 w, u16 h);

static s32 update_candidate(struct tcm *tcm, u16 x0, u16 y0, u16 w, u16 h,
			    struct tcm_area *field, s32 criteria,
			    struct score *best);

static void get_nearness_factor(struct tcm_area *field,
				struct tcm_area *candidate,
				struct nearness_factor *nf);

static void get_neighbor_stats(struct tcm *tcm, struct tcm_area *area,
			       struct neighbor_stats *stat);

static void fill_area(struct tcm *tcm,
				struct tcm_area *area, struct tcm_area *parent);


/*********************************************/

/*********************************************
 *	Utility Methods
 *********************************************/
struct tcm *sita_init(u16 width, u16 height, struct tcm_pt *attr)
{
	struct tcm *tcm;
	struct sita_pvt *pvt;
	struct tcm_area area = {0};
	s32 i;

	if (width == 0 || height == 0)
		return NULL;

	tcm = kmalloc(sizeof(*tcm), GFP_KERNEL);
	pvt = kmalloc(sizeof(*pvt), GFP_KERNEL);
	if (!tcm || !pvt)
		goto error;

	memset(tcm, 0, sizeof(*tcm));
	memset(pvt, 0, sizeof(*pvt));

	/* Updating the pointers to SiTA implementation APIs */
	tcm->height = height;
	tcm->width = width;
	tcm->reserve_2d = sita_reserve_2d;
	tcm->reserve_1d = sita_reserve_1d;
	tcm->free = sita_free;
	tcm->deinit = sita_deinit;
	tcm->pvt = (void *)pvt;

	spin_lock_init(&(pvt->lock));

	/* Creating tam map */
	pvt->map = kmalloc(sizeof(*pvt->map) * tcm->width, GFP_KERNEL);
	if (!pvt->map)
		goto error;

	for (i = 0; i < tcm->width; i++) {
		pvt->map[i] =
			kmalloc(sizeof(**pvt->map) * tcm->height,
								GFP_KERNEL);
		if (pvt->map[i] == NULL) {
			while (i--)
				kfree(pvt->map[i]);
			kfree(pvt->map);
			goto error;
		}
	}

	if (attr && attr->x <= tcm->width && attr->y <= tcm->height) {
		pvt->div_pt.x = attr->x;
		pvt->div_pt.y = attr->y;

	} else {
		/* Defaulting to 3:1 ratio on width for 2D area split */
		/* Defaulting to 3:1 ratio on height for 2D and 1D split */
		pvt->div_pt.x = (tcm->width * 3) / 4;
		pvt->div_pt.y = (tcm->height * 3) / 4;
	}

	spin_lock(&(pvt->lock));
	assign(&area, 0, 0, width - 1, height - 1);
	fill_area(tcm, &area, NULL);
	spin_unlock(&(pvt->lock));
	return tcm;

error:
	kfree(tcm);
	kfree(pvt);
	return NULL;
}

static void sita_deinit(struct tcm *tcm)
{
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct tcm_area area = {0};
	s32 i;

	area.p1.x = tcm->width - 1;
	area.p1.y = tcm->height - 1;

	spin_lock(&(pvt->lock));
	fill_area(tcm, &area, NULL);
	spin_unlock(&(pvt->lock));

	for (i = 0; i < tcm->height; i++)
		kfree(pvt->map[i]);
	kfree(pvt->map);
	kfree(pvt);
}

/**
 * Reserve a 1D area in the container
 *
 * @param num_slots	size of 1D area
 * @param area		pointer to the area that will be populated with the
 *			reserved area
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 sita_reserve_1d(struct tcm *tcm, u32 num_slots,
			   struct tcm_area *area)
{
	s32 ret;
	struct tcm_area field = {0};
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	spin_lock(&(pvt->lock));

	/* Scanning entire container */
	assign(&field, tcm->width - 1, tcm->height - 1, 0, 0);

	ret = scan_r2l_b2t_one_dim(tcm, num_slots, &field, area);
	if (!ret)
		/* update map */
		fill_area(tcm, area, area);

	spin_unlock(&(pvt->lock));
	return ret;
}

/**
 * Reserve a 2D area in the container
 *
 * @param w	width
 * @param h	height
 * @param area	pointer to the area that will be populated with the reesrved
 *		area
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 sita_reserve_2d(struct tcm *tcm, u16 h, u16 w, u8 align,
			   struct tcm_area *area)
{
	s32 ret;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	/* not supporting more than 64 as alignment */
	if (align > 64)
		return -EINVAL;

	/* we prefer 1, 32 and 64 as alignment */
	align = align <= 1 ? 1 : align <= 32 ? 32 : 64;

	spin_lock(&(pvt->lock));
	ret = scan_areas_and_find_fit(tcm, w, h, align, area);
	if (!ret)
		/* update map */
		fill_area(tcm, area, area);

	spin_unlock(&(pvt->lock));
	return ret;
}

/**
 * Unreserve a previously allocated 2D or 1D area
 * @param area	area to be freed
 * @return 0 - success
 */
static s32 sita_free(struct tcm *tcm, struct tcm_area *area)
{
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	spin_lock(&(pvt->lock));

	/* check that this is in fact an existing area */
	WARN_ON(pvt->map[area->p0.x][area->p0.y] != area ||
		pvt->map[area->p1.x][area->p1.y] != area);

	/* Clear the contents of the associated tiles in the map */
	fill_area(tcm, area, NULL);

	spin_unlock(&(pvt->lock));

	return 0;
}

/**
 * Note: In general the cordinates in the scan field area relevant to the can
 * sweep directions. The scan origin (e.g. top-left corner) will always be
 * the p0 member of the field.  Therfore, for a scan from top-left p0.x <= p1.x
 * and p0.y <= p1.y; whereas, for a scan from bottom-right p1.x <= p0.x and p1.y
 * <= p0.y
 */

/**
 * Raster scan horizontally right to left from top to bottom to find a place for
 * a 2D area of given size inside a scan field.
 *
 * @param w	width of desired area
 * @param h	height of desired area
 * @param align	desired area alignment
 * @param area	pointer to the area that will be set to the best position
 * @param field	area to scan (inclusive)
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 scan_r2l_t2b(struct tcm *tcm, u16 w, u16 h, u16 align,
			struct tcm_area *field, struct tcm_area *area)
{
	s32 x, y;
	s16 start_x, end_x, start_y, end_y, found_x = -1;
	struct tcm_area ***map = ((struct sita_pvt *)tcm->pvt)->map;
	struct score best = {{0}, {0}, {0}, 0};

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p0.x < field->p1.x ||
	    field->p1.y < field->p0.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > LEN(start_x, end_x) || h > LEN(end_y, start_y))
		return -ENOSPC;

	/* adjust start_x and end_y, as allocation would not fit beyond */
	start_x = ALIGN_DOWN(start_x - w + 1, align); /* - 1 to be inclusive */
	end_y = end_y - h + 1;

	/* check if allocation would still fit in scan area */
	if (start_x < end_x)
		return -ENOSPC;

	/* scan field top-to-bottom, right-to-left */
	for (y = start_y; y <= end_y; y++) {
		for (x = start_x; x >= end_x; x -= align) {
			if (is_area_free(map, x, y, w, h)) {
				found_x = x;

				/* update best candidate */
				if (update_candidate(tcm, x, y, w, h, field,
							CR_R2L_T2B, &best))
					goto done;

				/* change upper x bound */
				end_x = x + 1;
				break;
			} else if (map[x][y] && map[x][y]->is2d) {
				/* step over 2D areas */
				x = ALIGN(map[x][y]->p0.x - w + 1, align);
			}
		}

		/* break if you find a free area shouldering the scan field */
		if (found_x == start_x)
			break;
	}

	if (!best.a.tcm)
		return -ENOSPC;
done:
	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}

/**
 * Raster scan horizontally left to right from top to bottom to find a place for
 * a 2D area of given size inside a scan field.
 *
 * @param w	width of desired area
 * @param h	height of desired area
 * @param align	desired area alignment
 * @param area	pointer to the area that will be set to the best position
 * @param field	area to scan (inclusive)
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 scan_l2r_t2b(struct tcm *tcm, u16 w, u16 h, u16 align,
			struct tcm_area *field, struct tcm_area *area)
{
	s32 x, y;
	s16 start_x, end_x, start_y, end_y, found_x = -1;
	struct tcm_area ***map = ((struct sita_pvt *)tcm->pvt)->map;
	struct score best = {{0}, {0}, {0}, 0};

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p1.x < field->p0.x ||
	    field->p1.y < field->p0.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > LEN(end_x, start_x) || h > LEN(end_y, start_y))
		return -ENOSPC;

	start_x = ALIGN(start_x, align);

	/* check if allocation would still fit in scan area */
	if (w > LEN(end_x, start_x))
		return -ENOSPC;

	/* adjust end_x and end_y, as allocation would not fit beyond */
	end_x = end_x - w + 1; /* + 1 to be inclusive */
	end_y = end_y - h + 1;

	/* scan field top-to-bottom, left-to-right */
	for (y = start_y; y <= end_y; y++) {
		for (x = start_x; x <= end_x; x += align) {
			if (is_area_free(map, x, y, w, h)) {
				found_x = x;

				/* update best candidate */
				if (update_candidate(tcm, x, y, w, h, field,
							CR_L2R_T2B, &best))
					goto done;
				/* change upper x bound */
				end_x = x - 1;

				break;
			} else if (map[x][y] && map[x][y]->is2d) {
				/* step over 2D areas */
				x = ALIGN_DOWN(map[x][y]->p1.x, align);
			}
		}

		/* break if you find a free area shouldering the scan field */
		if (found_x == start_x)
			break;
	}

	if (!best.a.tcm)
		return -ENOSPC;
done:
	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}

/**
 * Raster scan horizontally right to left from bottom to top to find a place
 * for a 1D area of given size inside a scan field.
 *
 * @param num_slots	size of desired area
 * @param align		desired area alignment
 * @param area		pointer to the area that will be set to the best
 *			position
 * @param field		area to scan (inclusive)
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 scan_r2l_b2t_one_dim(struct tcm *tcm, u32 num_slots,
				struct tcm_area *field, struct tcm_area *area)
{
	s32 found = 0;
	s16 x, y;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct tcm_area *p;

	/* check scan area co-ordinates */
	if (field->p0.y < field->p1.y)
		return -EINVAL;

	/**
	 * Currently we only support full width 1D scan field, which makes sense
	 * since 1D slot-ordering spans the full container width.
	 */
	if (tcm->width != field->p0.x - field->p1.x + 1)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (num_slots > tcm->width * LEN(field->p0.y, field->p1.y))
		return -ENOSPC;

	x = field->p0.x;
	y = field->p0.y;

	/* find num_slots consecutive free slots to the left */
	while (found < num_slots) {
		if (y < 0)
			return -ENOSPC;

		/* remember bottom-right corner */
		if (found == 0) {
			area->p1.x = x;
			area->p1.y = y;
		}

		/* skip busy regions */
		p = pvt->map[x][y];
		if (p) {
			/* move to left of 2D areas, top left of 1D */
			x = p->p0.x;
			if (!p->is2d)
				y = p->p0.y;

			/* start over */
			found = 0;
		} else {
			/* count consecutive free slots */
			found++;
			if (found == num_slots)
				break;
		}

		/* move to the left */
		if (x == 0)
			y--;
		x = (x ? : tcm->width) - 1;

	}

	/* set top-left corner */
	area->p0.x = x;
	area->p0.y = y;
	return 0;
}

/**
 * Find a place for a 2D area of given size inside a scan field based on its
 * alignment needs.
 *
 * @param w	width of desired area
 * @param h	height of desired area
 * @param align	desired area alignment
 * @param area	pointer to the area that will be set to the best position
 *
 * @return 0 on success, non-0 error value on failure.
 */
static s32 scan_areas_and_find_fit(struct tcm *tcm, u16 w, u16 h, u16 align,
				   struct tcm_area *area)
{
	s32 ret = 0;
	struct tcm_area field = {0};
	u16 boundary_x, boundary_y;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	if (align > 1) {
		/* prefer top-left corner */
		boundary_x = pvt->div_pt.x - 1;
		boundary_y = pvt->div_pt.y - 1;

		/* expand width and height if needed */
		if (w > pvt->div_pt.x)
			boundary_x = tcm->width - 1;
		if (h > pvt->div_pt.y)
			boundary_y = tcm->height - 1;

		assign(&field, 0, 0, boundary_x, boundary_y);
		ret = scan_l2r_t2b(tcm, w, h, align, &field, area);

		/* scan whole container if failed, but do not scan 2x */
		if (ret != 0 && (boundary_x != tcm->width - 1 ||
				 boundary_y != tcm->height - 1)) {
			/* scan the entire container if nothing found */
			assign(&field, 0, 0, tcm->width - 1, tcm->height - 1);
			ret = scan_l2r_t2b(tcm, w, h, align, &field, area);
		}
	} else if (align == 1) {
		/* prefer top-right corner */
		boundary_x = pvt->div_pt.x;
		boundary_y = pvt->div_pt.y - 1;

		/* expand width and height if needed */
		if (w > (tcm->width - pvt->div_pt.x))
			boundary_x = 0;
		if (h > pvt->div_pt.y)
			boundary_y = tcm->height - 1;

		assign(&field, tcm->width - 1, 0, boundary_x, boundary_y);
		ret = scan_r2l_t2b(tcm, w, h, align, &field, area);

		/* scan whole container if failed, but do not scan 2x */
		if (ret != 0 && (boundary_x != 0 ||
				 boundary_y != tcm->height - 1)) {
			/* scan the entire container if nothing found */
			assign(&field, tcm->width - 1, 0, 0, tcm->height - 1);
			ret = scan_r2l_t2b(tcm, w, h, align, &field,
					   area);
		}
	}

	return ret;
}

/* check if an entire area is free */
static s32 is_area_free(struct tcm_area ***map, u16 x0, u16 y0, u16 w, u16 h)
{
	u16 x = 0, y = 0;
	for (y = y0; y < y0 + h; y++) {
		for (x = x0; x < x0 + w; x++) {
			if (map[x][y])
				return false;
		}
	}
	return true;
}

/* fills an area with a parent tcm_area */
static void fill_area(struct tcm *tcm, struct tcm_area *area,
			struct tcm_area *parent)
{
	s32 x, y;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct tcm_area a, a_;

	/* set area's tcm; otherwise, enumerator considers it invalid */
	area->tcm = tcm;

	tcm_for_each_slice(a, *area, a_) {
		for (x = a.p0.x; x <= a.p1.x; ++x)
			for (y = a.p0.y; y <= a.p1.y; ++y)
				pvt->map[x][y] = parent;

	}
}

/**
 * Compares a candidate area to the current best area, and if it is a better
 * fit, it updates the best to this one.
 *
 * @param x0, y0, w, h		top, left, width, height of candidate area
 * @param field			scan field
 * @param criteria		scan criteria
 * @param best			best candidate and its scores
 *
 * @return 1 (true) if the candidate area is known to be the final best, so no
 * more searching should be performed
 */
static s32 update_candidate(struct tcm *tcm, u16 x0, u16 y0, u16 w, u16 h,
			    struct tcm_area *field, s32 criteria,
			    struct score *best)
{
	struct score me;	/* score for area */

	/*
	 * NOTE: For horizontal bias we always give the first found, because our
	 * scan is horizontal-raster-based and the first candidate will always
	 * have the horizontal bias.
	 */
	bool first = criteria & CR_BIAS_HORIZONTAL;

	assign(&me.a, x0, y0, x0 + w - 1, y0 + h - 1);

	/* calculate score for current candidate */
	if (!first) {
		get_neighbor_stats(tcm, &me.a, &me.n);
		me.neighs = me.n.edge + me.n.busy;
		get_nearness_factor(field, &me.a, &me.f);
	}

	/* the 1st candidate is always the best */
	if (!best->a.tcm)
		goto better;

	BUG_ON(first);

	/* diagonal balance check */
	if ((criteria & CR_DIAGONAL_BALANCE) &&
		best->neighs <= me.neighs &&
		(best->neighs < me.neighs ||
		 /* this implies that neighs and occupied match */
		 best->n.busy < me.n.busy ||
		 (best->n.busy == me.n.busy &&
		  /* check the nearness factor */
		  best->f.x + best->f.y > me.f.x + me.f.y)))
		goto better;

	/* not better, keep going */
	return 0;

better:
	/* save current area as best */
	memcpy(best, &me, sizeof(me));
	best->a.tcm = tcm;
	return first;
}

/**
 * Calculate the nearness factor of an area in a search field.  The nearness
 * factor is smaller if the area is closer to the search origin.
 */
static void get_nearness_factor(struct tcm_area *field, struct tcm_area *area,
				struct nearness_factor *nf)
{
	/**
	 * Using signed math as field coordinates may be reversed if
	 * search direction is right-to-left or bottom-to-top.
	 */
	nf->x = (s32)(area->p0.x - field->p0.x) * 1000 /
		(field->p1.x - field->p0.x);
	nf->y = (s32)(area->p0.y - field->p0.y) * 1000 /
		(field->p1.y - field->p0.y);
}

/* get neighbor statistics */
static void get_neighbor_stats(struct tcm *tcm, struct tcm_area *area,
			 struct neighbor_stats *stat)
{
	s16 x = 0, y = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	/* Clearing any exisiting values */
	memset(stat, 0, sizeof(*stat));

	/* process top & bottom edges */
	for (x = area->p0.x; x <= area->p1.x; x++) {
		if (area->p0.y == 0)
			stat->edge++;
		else if (pvt->map[x][area->p0.y - 1])
			stat->busy++;

		if (area->p1.y == tcm->height - 1)
			stat->edge++;
		else if (pvt->map[x][area->p1.y + 1])
			stat->busy++;
	}

	/* process left & right edges */
	for (y = area->p0.y; y <= area->p1.y; ++y) {
		if (area->p0.x == 0)
			stat->edge++;
		else if (pvt->map[area->p0.x - 1][y])
			stat->busy++;

		if (area->p1.x == tcm->width - 1)
			stat->edge++;
		else if (pvt->map[area->p1.x + 1][y])
			stat->busy++;
	}
}
