/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) uv-based functions.
 *
 *      Architecture specific implementation of common functions.
 *
 */

#include "xp.h"

static enum xp_retval
xp_remote_memcpy_uv(void *vdst, const void *psrc, size_t len)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

enum xp_retval
xp_init_uv(void)
{
	BUG_ON(!is_uv());

	xp_max_npartitions = XP_MAX_NPARTITIONS_UV;

	xp_remote_memcpy = xp_remote_memcpy_uv;

	return xpSuccess;
}

void
xp_exit_uv(void)
{
	BUG_ON(!is_uv());
}
