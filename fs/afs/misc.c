/* misc.c: miscellaneous bits
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include "errors.h"
#include "internal.h"

/*****************************************************************************/
/*
 * convert an AFS abort code to a Linux error number
 */
int afs_abort_to_error(int abortcode)
{
	switch (abortcode) {
	case VSALVAGE:		return -EIO;
	case VNOVNODE:		return -ENOENT;
	case VNOVOL:		return -ENXIO;
	case VVOLEXISTS:	return -EEXIST;
	case VNOSERVICE:	return -EIO;
	case VOFFLINE:		return -ENOENT;
	case VONLINE:		return -EEXIST;
	case VDISKFULL:		return -ENOSPC;
	case VOVERQUOTA:	return -EDQUOT;
	case VBUSY:		return -EBUSY;
	case VMOVED:		return -ENXIO;
	default:		return -EIO;
	}

} /* end afs_abort_to_error() */
