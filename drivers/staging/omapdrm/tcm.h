/*
 * tcm.h
 *
 * TILER container manager specification and support functions for TI
 * TILER driver.
 *
 * Author: Lajos Molnar <molnar@ti.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TCM_H
#define TCM_H

struct tcm;

/* point */
struct tcm_pt {
	u16 x;
	u16 y;
};

/* 1d or 2d area */
struct tcm_area {
	bool is2d;		/* whether area is 1d or 2d */
	struct tcm    *tcm;	/* parent */
	struct tcm_pt  p0;
	struct tcm_pt  p1;
};

struct tcm {
	u16 width, height;	/* container dimensions */
	int lut_id;		/* Lookup table identifier */

	/* 'pvt' structure shall contain any tcm details (attr) along with
	linked list of allocated areas and mutex for mutually exclusive access
	to the list.  It may also contain copies of width and height to notice
	any changes to the publicly available width and height fields. */
	void *pvt;

	/* function table */
	s32 (*reserve_2d)(struct tcm *tcm, u16 height, u16 width, u8 align,
			  struct tcm_area *area);
	s32 (*reserve_1d)(struct tcm *tcm, u32 slots, struct tcm_area *area);
	s32 (*free)      (struct tcm *tcm, struct tcm_area *area);
	void (*deinit)   (struct tcm *tcm);
};

/*=============================================================================
    BASIC TILER CONTAINER MANAGER INTERFACE
=============================================================================*/

/*
 * NOTE:
 *
 * Since some basic parameter checking is done outside the TCM algorithms,
 * TCM implementation do NOT have to check the following:
 *
 *   area pointer is NULL
 *   width and height fits within container
 *   number of pages is more than the size of the container
 *
 */

struct tcm *sita_init(u16 width, u16 height, struct tcm_pt *attr);


/**
 * Deinitialize tiler container manager.
 *
 * @param tcm	Pointer to container manager.
 *
 * @return 0 on success, non-0 error value on error.  The call
 *	   should free as much memory as possible and meaningful
 *	   even on failure.  Some error codes: -ENODEV: invalid
 *	   manager.
 */
static inline void tcm_deinit(struct tcm *tcm)
{
	if (tcm)
		tcm->deinit(tcm);
}

/**
 * Reserves a 2D area in the container.
 *
 * @param tcm		Pointer to container manager.
 * @param height	Height(in pages) of area to be reserved.
 * @param width		Width(in pages) of area to be reserved.
 * @param align		Alignment requirement for top-left corner of area. Not
 *			all values may be supported by the container manager,
 *			but it must support 0 (1), 32 and 64.
 *			0 value is equivalent to 1.
 * @param area		Pointer to where the reserved area should be stored.
 *
 * @return 0 on success.  Non-0 error code on failure.  Also,
 *	   the tcm field of the area will be set to NULL on
 *	   failure.  Some error codes: -ENODEV: invalid manager,
 *	   -EINVAL: invalid area, -ENOMEM: not enough space for
 *	    allocation.
 */
static inline s32 tcm_reserve_2d(struct tcm *tcm, u16 width, u16 height,
				 u16 align, struct tcm_area *area)
{
	/* perform rudimentary error checking */
	s32 res = tcm  == NULL ? -ENODEV :
		(area == NULL || width == 0 || height == 0 ||
		 /* align must be a 2 power */
		 (align & (align - 1))) ? -EINVAL :
		(height > tcm->height || width > tcm->width) ? -ENOMEM : 0;

	if (!res) {
		area->is2d = true;
		res = tcm->reserve_2d(tcm, height, width, align, area);
		area->tcm = res ? NULL : tcm;
	}

	return res;
}

/**
 * Reserves a 1D area in the container.
 *
 * @param tcm		Pointer to container manager.
 * @param slots		Number of (contiguous) slots to reserve.
 * @param area		Pointer to where the reserved area should be stored.
 *
 * @return 0 on success.  Non-0 error code on failure.  Also,
 *	   the tcm field of the area will be set to NULL on
 *	   failure.  Some error codes: -ENODEV: invalid manager,
 *	   -EINVAL: invalid area, -ENOMEM: not enough space for
 *	    allocation.
 */
static inline s32 tcm_reserve_1d(struct tcm *tcm, u32 slots,
				 struct tcm_area *area)
{
	/* perform rudimentary error checking */
	s32 res = tcm  == NULL ? -ENODEV :
		(area == NULL || slots == 0) ? -EINVAL :
		slots > (tcm->width * (u32) tcm->height) ? -ENOMEM : 0;

	if (!res) {
		area->is2d = false;
		res = tcm->reserve_1d(tcm, slots, area);
		area->tcm = res ? NULL : tcm;
	}

	return res;
}

/**
 * Free a previously reserved area from the container.
 *
 * @param area	Pointer to area reserved by a prior call to
 *		tcm_reserve_1d or tcm_reserve_2d call, whether
 *		it was successful or not. (Note: all fields of
 *		the structure must match.)
 *
 * @return 0 on success.  Non-0 error code on failure.  Also, the tcm
 *	   field of the area is set to NULL on success to avoid subsequent
 *	   freeing.  This call will succeed even if supplying
 *	   the area from a failed reserved call.
 */
static inline s32 tcm_free(struct tcm_area *area)
{
	s32 res = 0; /* free succeeds by default */

	if (area && area->tcm) {
		res = area->tcm->free(area->tcm, area);
		if (res == 0)
			area->tcm = NULL;
	}

	return res;
}

/*=============================================================================
    HELPER FUNCTION FOR ANY TILER CONTAINER MANAGER
=============================================================================*/

/**
 * This method slices off the topmost 2D slice from the parent area, and stores
 * it in the 'slice' parameter.  The 'parent' parameter will get modified to
 * contain the remaining portion of the area.  If the whole parent area can
 * fit in a 2D slice, its tcm pointer is set to NULL to mark that it is no
 * longer a valid area.
 *
 * @param parent	Pointer to a VALID parent area that will get modified
 * @param slice		Pointer to the slice area that will get modified
 */
static inline void tcm_slice(struct tcm_area *parent, struct tcm_area *slice)
{
	*slice = *parent;

	/* check if we need to slice */
	if (slice->tcm && !slice->is2d &&
		slice->p0.y != slice->p1.y &&
		(slice->p0.x || (slice->p1.x != slice->tcm->width - 1))) {
		/* set end point of slice (start always remains) */
		slice->p1.x = slice->tcm->width - 1;
		slice->p1.y = (slice->p0.x) ? slice->p0.y : slice->p1.y - 1;
		/* adjust remaining area */
		parent->p0.x = 0;
		parent->p0.y = slice->p1.y + 1;
	} else {
		/* mark this as the last slice */
		parent->tcm = NULL;
	}
}

/* Verify if a tcm area is logically valid */
static inline bool tcm_area_is_valid(struct tcm_area *area)
{
	return area && area->tcm &&
		/* coordinate bounds */
		area->p1.x < area->tcm->width &&
		area->p1.y < area->tcm->height &&
		area->p0.y <= area->p1.y &&
		/* 1D coordinate relationship + p0.x check */
		((!area->is2d &&
		  area->p0.x < area->tcm->width &&
		  area->p0.x + area->p0.y * area->tcm->width <=
		  area->p1.x + area->p1.y * area->tcm->width) ||
		 /* 2D coordinate relationship */
		 (area->is2d &&
		  area->p0.x <= area->p1.x));
}

/* see if a coordinate is within an area */
static inline bool __tcm_is_in(struct tcm_pt *p, struct tcm_area *a)
{
	u16 i;

	if (a->is2d) {
		return p->x >= a->p0.x && p->x <= a->p1.x &&
		       p->y >= a->p0.y && p->y <= a->p1.y;
	} else {
		i = p->x + p->y * a->tcm->width;
		return i >= a->p0.x + a->p0.y * a->tcm->width &&
		       i <= a->p1.x + a->p1.y * a->tcm->width;
	}
}

/* calculate area width */
static inline u16 __tcm_area_width(struct tcm_area *area)
{
	return area->p1.x - area->p0.x + 1;
}

/* calculate area height */
static inline u16 __tcm_area_height(struct tcm_area *area)
{
	return area->p1.y - area->p0.y + 1;
}

/* calculate number of slots in an area */
static inline u16 __tcm_sizeof(struct tcm_area *area)
{
	return area->is2d ?
		__tcm_area_width(area) * __tcm_area_height(area) :
		(area->p1.x - area->p0.x + 1) + (area->p1.y - area->p0.y) *
							area->tcm->width;
}
#define tcm_sizeof(area) __tcm_sizeof(&(area))
#define tcm_awidth(area) __tcm_area_width(&(area))
#define tcm_aheight(area) __tcm_area_height(&(area))
#define tcm_is_in(pt, area) __tcm_is_in(&(pt), &(area))

/* limit a 1D area to the first N pages */
static inline s32 tcm_1d_limit(struct tcm_area *a, u32 num_pg)
{
	if (__tcm_sizeof(a) < num_pg)
		return -ENOMEM;
	if (!num_pg)
		return -EINVAL;

	a->p1.x = (a->p0.x + num_pg - 1) % a->tcm->width;
	a->p1.y = a->p0.y + ((a->p0.x + num_pg - 1) / a->tcm->width);
	return 0;
}

/**
 * Iterate through 2D slices of a valid area. Behaves
 * syntactically as a for(;;) statement.
 *
 * @param var		Name of a local variable of type 'struct
 *			tcm_area *' that will get modified to
 *			contain each slice.
 * @param area		Pointer to the VALID parent area. This
 *			structure will not get modified
 *			throughout the loop.
 *
 */
#define tcm_for_each_slice(var, area, safe) \
	for (safe = area, \
	     tcm_slice(&safe, &var); \
	     var.tcm; tcm_slice(&safe, &var))

#endif
