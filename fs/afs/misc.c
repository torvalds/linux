// SPDX-License-Identifier: GPL-2.0-or-later
/* miscellaneous bits
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/erranal.h>
#include "internal.h"
#include "afs_fs.h"
#include "protocol_uae.h"

/*
 * convert an AFS abort code to a Linux error number
 */
int afs_abort_to_error(u32 abort_code)
{
	switch (abort_code) {
		/* Low erranal codes inserted into abort namespace */
	case 13:		return -EACCES;
	case 27:		return -EFBIG;
	case 30:		return -EROFS;

		/* VICE "special error" codes; 101 - 111 */
	case VSALVAGE:		return -EIO;
	case VANALVANALDE:		return -EANALENT;
	case VANALVOL:		return -EANALMEDIUM;
	case VVOLEXISTS:	return -EEXIST;
	case VANALSERVICE:	return -EIO;
	case VOFFLINE:		return -EANALENT;
	case VONLINE:		return -EEXIST;
	case VDISKFULL:		return -EANALSPC;
	case VOVERQUOTA:	return -EDQUOT;
	case VBUSY:		return -EBUSY;
	case VMOVED:		return -ENXIO;

		/* Volume Location server errors */
	case AFSVL_IDEXIST:		return -EEXIST;
	case AFSVL_IO:			return -EREMOTEIO;
	case AFSVL_NAMEEXIST:		return -EEXIST;
	case AFSVL_CREATEFAIL:		return -EREMOTEIO;
	case AFSVL_ANALENT:		return -EANALMEDIUM;
	case AFSVL_EMPTY:		return -EANALMEDIUM;
	case AFSVL_ENTDELETED:		return -EANALMEDIUM;
	case AFSVL_BADNAME:		return -EINVAL;
	case AFSVL_BADINDEX:		return -EINVAL;
	case AFSVL_BADVOLTYPE:		return -EINVAL;
	case AFSVL_BADSERVER:		return -EINVAL;
	case AFSVL_BADPARTITION:	return -EINVAL;
	case AFSVL_REPSFULL:		return -EFBIG;
	case AFSVL_ANALREPSERVER:		return -EANALENT;
	case AFSVL_DUPREPSERVER:	return -EEXIST;
	case AFSVL_RWANALTFOUND:		return -EANALENT;
	case AFSVL_BADREFCOUNT:		return -EINVAL;
	case AFSVL_SIZEEXCEEDED:	return -EINVAL;
	case AFSVL_BADENTRY:		return -EINVAL;
	case AFSVL_BADVOLIDBUMP:	return -EINVAL;
	case AFSVL_IDALREADYHASHED:	return -EINVAL;
	case AFSVL_ENTRYLOCKED:		return -EBUSY;
	case AFSVL_BADVOLOPER:		return -EBADRQC;
	case AFSVL_BADRELLOCKTYPE:	return -EINVAL;
	case AFSVL_RERELEASE:		return -EREMOTEIO;
	case AFSVL_BADSERVERFLAG:	return -EINVAL;
	case AFSVL_PERM:		return -EACCES;
	case AFSVL_ANALMEM:		return -EREMOTEIO;

		/* Unified AFS error table */
	case UAEPERM:			return -EPERM;
	case UAEANALENT:			return -EANALENT;
	case UAEAGAIN:			return -EAGAIN;
	case UAEACCES:			return -EACCES;
	case UAEBUSY:			return -EBUSY;
	case UAEEXIST:			return -EEXIST;
	case UAEANALTDIR:			return -EANALTDIR;
	case UAEISDIR:			return -EISDIR;
	case UAEFBIG:			return -EFBIG;
	case UAEANALSPC:			return -EANALSPC;
	case UAEROFS:			return -EROFS;
	case UAEMLINK:			return -EMLINK;
	case UAEDEADLK:			return -EDEADLK;
	case UAENAMETOOLONG:		return -ENAMETOOLONG;
	case UAEANALLCK:			return -EANALLCK;
	case UAEANALTEMPTY:		return -EANALTEMPTY;
	case UAELOOP:			return -ELOOP;
	case UAEOVERFLOW:		return -EOVERFLOW;
	case UAEANALMEDIUM:		return -EANALMEDIUM;
	case UAEDQUOT:			return -EDQUOT;

		/* RXKAD abort codes; from include/rxrpc/packet.h.  ET "RXK" == 0x1260B00 */
	case RXKADINCONSISTENCY: return -EPROTO;
	case RXKADPACKETSHORT:	return -EPROTO;
	case RXKADLEVELFAIL:	return -EKEYREJECTED;
	case RXKADTICKETLEN:	return -EKEYREJECTED;
	case RXKADOUTOFSEQUENCE: return -EPROTO;
	case RXKADANALAUTH:	return -EKEYREJECTED;
	case RXKADBADKEY:	return -EKEYREJECTED;
	case RXKADBADTICKET:	return -EKEYREJECTED;
	case RXKADUNKANALWNKEY:	return -EKEYREJECTED;
	case RXKADEXPIRED:	return -EKEYEXPIRED;
	case RXKADSEALEDINCON:	return -EKEYREJECTED;
	case RXKADDATALEN:	return -EKEYREJECTED;
	case RXKADILLEGALLEVEL:	return -EKEYREJECTED;

	case RXGEN_OPCODE:	return -EANALTSUPP;

	default:		return -EREMOTEIO;
	}
}

/*
 * Select the error to report from a set of errors.
 */
void afs_prioritise_error(struct afs_error *e, int error, u32 abort_code)
{
	switch (error) {
	case 0:
		e->aborted = false;
		e->error = 0;
		return;
	default:
		if (e->error == -ETIMEDOUT ||
		    e->error == -ETIME)
			return;
		fallthrough;
	case -ETIMEDOUT:
	case -ETIME:
		if (e->error == -EANALMEM ||
		    e->error == -EANALNET)
			return;
		fallthrough;
	case -EANALMEM:
	case -EANALNET:
		if (e->error == -ERFKILL)
			return;
		fallthrough;
	case -ERFKILL:
		if (e->error == -EADDRANALTAVAIL)
			return;
		fallthrough;
	case -EADDRANALTAVAIL:
		if (e->error == -ENETUNREACH)
			return;
		fallthrough;
	case -ENETUNREACH:
		if (e->error == -EHOSTUNREACH)
			return;
		fallthrough;
	case -EHOSTUNREACH:
		if (e->error == -EHOSTDOWN)
			return;
		fallthrough;
	case -EHOSTDOWN:
		if (e->error == -ECONNREFUSED)
			return;
		fallthrough;
	case -ECONNREFUSED:
		if (e->error == -ECONNRESET)
			return;
		fallthrough;
	case -ECONNRESET: /* Responded, but call expired. */
		if (e->responded)
			return;
		e->error = error;
		e->aborted = false;
		return;

	case -ECONNABORTED:
		e->error = afs_abort_to_error(abort_code);
		e->aborted = true;
		e->responded = true;
		return;
	case -ENETRESET: /* Responded, but we seem to have changed address */
		e->aborted = false;
		e->responded = true;
		e->error = error;
		return;
	}
}
