/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2016  Cavium Inc. (support@cavium.com).
 *
 */

/*
 * Module to support operations on bitmap of cores. Coremask can be used to
 * select a specific core, a group of cores, or all available cores, for
 * initialization and differentiation of roles within a single shared binary
 * executable image.
 *
 * The core numbers used in this file are the same value as what is found in
 * the COP0_EBASE register and the rdhwr 0 instruction.
 *
 * For the CN78XX and other multi-node environments the core numbers are not
 * contiguous.  The core numbers for the CN78XX are as follows:
 *
 * Node 0:	Cores 0 - 47
 * Node 1:	Cores 128 - 175
 * Node 2:	Cores 256 - 303
 * Node 3:	Cores 384 - 431
 *
 */

#ifndef __CVMX_COREMASK_H__
#define __CVMX_COREMASK_H__

#define CVMX_MIPS_MAX_CORES 1024
/* bits per holder */
#define CVMX_COREMASK_ELTSZ 64

/* cvmx_coremask_t's size in u64 */
#define CVMX_COREMASK_BMPSZ (CVMX_MIPS_MAX_CORES / CVMX_COREMASK_ELTSZ)


/* cvmx_coremask_t */
struct cvmx_coremask {
	u64 coremask_bitmap[CVMX_COREMASK_BMPSZ];
};

/*
 * Is ``core'' set in the coremask?
 */
static inline bool cvmx_coremask_is_core_set(const struct cvmx_coremask *pcm,
					    int core)
{
	int n, i;

	n = core % CVMX_COREMASK_ELTSZ;
	i = core / CVMX_COREMASK_ELTSZ;

	return (pcm->coremask_bitmap[i] & ((u64)1 << n)) != 0;
}

/*
 * Make a copy of a coremask
 */
static inline void cvmx_coremask_copy(struct cvmx_coremask *dest,
				      const struct cvmx_coremask *src)
{
	memcpy(dest, src, sizeof(*dest));
}

/*
 * Set the lower 64-bit of the coremask.
 */
static inline void cvmx_coremask_set64(struct cvmx_coremask *pcm,
				       uint64_t coremask_64)
{
	pcm->coremask_bitmap[0] = coremask_64;
}

/*
 * Clear ``core'' from the coremask.
 */
static inline void cvmx_coremask_clear_core(struct cvmx_coremask *pcm, int core)
{
	int n, i;

	n = core % CVMX_COREMASK_ELTSZ;
	i = core / CVMX_COREMASK_ELTSZ;
	pcm->coremask_bitmap[i] &= ~(1ull << n);
}

#endif /* __CVMX_COREMASK_H__ */
