/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <asm/errno.h>

int
sn_msi_init(void)
{
	/*
	 * return error until MSI is supported on altix platforms
	 */
	return -EINVAL;
}
