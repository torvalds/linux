/* miscellaneous bits
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
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
#include <rxrpc/packet.h>
#include "internal.h"
#include "afs_fs.h"

/*
 * convert an AFS abort code to a Linux error number
 */
int afs_abort_to_error(u32 abort_code)
{
	switch (abort_code) {
	/* low errno codes inserted into abort namespace */
	case 13:		return -EACCES;
	case 27:		return -EFBIG;
	case 30:		return -EROFS;

	/* VICE "special error" codes; 101 - 111 */
	case VSALVAGE:		return -EIO;
	case VNOVNODE:		return -ENOENT;
	case VNOVOL:		return -ENOMEDIUM;
	case VVOLEXISTS:	return -EEXIST;
	case VNOSERVICE:	return -EIO;
	case VOFFLINE:		return -ENOENT;
	case VONLINE:		return -EEXIST;
	case VDISKFULL:		return -ENOSPC;
	case VOVERQUOTA:	return -EDQUOT;
	case VBUSY:		return -EBUSY;
	case VMOVED:		return -ENXIO;

	/* Unified AFS error table; ET "uae" == 0x2f6df00 */
	case 0x2f6df00:		return -EPERM;
	case 0x2f6df01:		return -ENOENT;
	case 0x2f6df04:		return -EIO;
	case 0x2f6df0a:		return -EAGAIN;
	case 0x2f6df0b:		return -ENOMEM;
	case 0x2f6df0c:		return -EACCES;
	case 0x2f6df0f:		return -EBUSY;
	case 0x2f6df10:		return -EEXIST;
	case 0x2f6df11:		return -EXDEV;
	case 0x2f6df12:		return -ENODEV;
	case 0x2f6df13:		return -ENOTDIR;
	case 0x2f6df14:		return -EISDIR;
	case 0x2f6df15:		return -EINVAL;
	case 0x2f6df1a:		return -EFBIG;
	case 0x2f6df1b:		return -ENOSPC;
	case 0x2f6df1d:		return -EROFS;
	case 0x2f6df1e:		return -EMLINK;
	case 0x2f6df20:		return -EDOM;
	case 0x2f6df21:		return -ERANGE;
	case 0x2f6df22:		return -EDEADLK;
	case 0x2f6df23:		return -ENAMETOOLONG;
	case 0x2f6df24:		return -ENOLCK;
	case 0x2f6df26:		return -ENOTEMPTY;
	case 0x2f6df28:		return -EWOULDBLOCK;
	case 0x2f6df69:		return -ENOTCONN;
	case 0x2f6df6c:		return -ETIMEDOUT;
	case 0x2f6df78:		return -EDQUOT;

	/* RXKAD abort codes; from include/rxrpc/packet.h.  ET "RXK" == 0x1260B00 */
	case RXKADINCONSISTENCY: return -EPROTO;
	case RXKADPACKETSHORT:	return -EPROTO;
	case RXKADLEVELFAIL:	return -EKEYREJECTED;
	case RXKADTICKETLEN:	return -EKEYREJECTED;
	case RXKADOUTOFSEQUENCE: return -EPROTO;
	case RXKADNOAUTH:	return -EKEYREJECTED;
	case RXKADBADKEY:	return -EKEYREJECTED;
	case RXKADBADTICKET:	return -EKEYREJECTED;
	case RXKADUNKNOWNKEY:	return -EKEYREJECTED;
	case RXKADEXPIRED:	return -EKEYEXPIRED;
	case RXKADSEALEDINCON:	return -EKEYREJECTED;
	case RXKADDATALEN:	return -EKEYREJECTED;
	case RXKADILLEGALLEVEL:	return -EKEYREJECTED;

	case RXGEN_OPCODE:	return -ENOTSUPP;

	default:		return -EREMOTEIO;
	}
}
