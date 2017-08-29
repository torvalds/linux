/*
 * Copyright 2016-17 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_VAS_H
#define _ASM_POWERPC_VAS_H

/*
 * Min and max FIFO sizes are based on Version 1.05 Section 3.1.4.25
 * (Local FIFO Size Register) of the VAS workbook.
 */
#define VAS_RX_FIFO_SIZE_MIN	(1 << 10)	/* 1KB */
#define VAS_RX_FIFO_SIZE_MAX	(8 << 20)	/* 8MB */

/*
 * Threshold Control Mode: Have paste operation fail if the number of
 * requests in receive FIFO exceeds a threshold.
 *
 * NOTE: No special error code yet if paste is rejected because of these
 *	 limits. So users can't distinguish between this and other errors.
 */
#define VAS_THRESH_DISABLED		0
#define VAS_THRESH_FIFO_GT_HALF_FULL	1
#define VAS_THRESH_FIFO_GT_QTR_FULL	2
#define VAS_THRESH_FIFO_GT_EIGHTH_FULL	3

/*
 * Get/Set bit fields
 */
#define GET_FIELD(m, v)                (((v) & (m)) >> MASK_LSH(m))
#define MASK_LSH(m)            (__builtin_ffsl(m) - 1)
#define SET_FIELD(m, v, val)   \
		(((v) & ~(m)) | ((((typeof(v))(val)) << MASK_LSH(m)) & (m)))

/*
 * Co-processor Engine type.
 */
enum vas_cop_type {
	VAS_COP_TYPE_FAULT,
	VAS_COP_TYPE_842,
	VAS_COP_TYPE_842_HIPRI,
	VAS_COP_TYPE_GZIP,
	VAS_COP_TYPE_GZIP_HIPRI,
	VAS_COP_TYPE_FTW,
	VAS_COP_TYPE_MAX,
};

#endif /* __ASM_POWERPC_VAS_H */
