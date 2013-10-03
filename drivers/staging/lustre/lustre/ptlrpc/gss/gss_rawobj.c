/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/gss/gss_rawobj.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include <linux/mutex.h>

#include <obd.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre_sec.h>

#include "gss_internal.h"

int rawobj_empty(rawobj_t *obj)
{
	LASSERT(equi(obj->len, obj->data));
	return (obj->len == 0);
}

int rawobj_alloc(rawobj_t *obj, char *buf, int len)
{
	LASSERT(obj);
	LASSERT(len >= 0);

	obj->len = len;
	if (len) {
		OBD_ALLOC_LARGE(obj->data, len);
		if (!obj->data) {
			obj->len = 0;
			return -ENOMEM;
		}
		memcpy(obj->data, buf, len);
	} else
		obj->data = NULL;
	return 0;
}

void rawobj_free(rawobj_t *obj)
{
	LASSERT(obj);

	if (obj->len) {
		LASSERT(obj->data);
		OBD_FREE_LARGE(obj->data, obj->len);
		obj->len = 0;
		obj->data = NULL;
	} else
		LASSERT(!obj->data);
}

int rawobj_equal(rawobj_t *a, rawobj_t *b)
{
	LASSERT(a && b);

	return (a->len == b->len &&
		(!a->len || !memcmp(a->data, b->data, a->len)));
}

int rawobj_dup(rawobj_t *dest, rawobj_t *src)
{
	LASSERT(src && dest);

	dest->len = src->len;
	if (dest->len) {
		OBD_ALLOC_LARGE(dest->data, dest->len);
		if (!dest->data) {
			dest->len = 0;
			return -ENOMEM;
		}
		memcpy(dest->data, src->data, dest->len);
	} else
		dest->data = NULL;
	return 0;
}

int rawobj_serialize(rawobj_t *obj, __u32 **buf, __u32 *buflen)
{
	__u32 len;

	LASSERT(obj);
	LASSERT(buf);
	LASSERT(buflen);

	len = cfs_size_round4(obj->len);

	if (*buflen < 4 + len) {
		CERROR("buflen %u <  %u\n", *buflen, 4 + len);
		return -EINVAL;
	}

	*(*buf)++ = cpu_to_le32(obj->len);
	memcpy(*buf, obj->data, obj->len);
	*buf += (len >> 2);
	*buflen -= (4 + len);

	return 0;
}

static int __rawobj_extract(rawobj_t *obj, __u32 **buf, __u32 *buflen,
			    int alloc, int local)
{
	__u32 len;

	if (*buflen < sizeof(__u32)) {
		CERROR("buflen %u\n", *buflen);
		return -EINVAL;
	}

	obj->len = *(*buf)++;
	if (!local)
		obj->len = le32_to_cpu(obj->len);
	*buflen -= sizeof(__u32);

	if (!obj->len) {
		obj->data = NULL;
		return 0;
	}

	len = local ? obj->len : cfs_size_round4(obj->len);
	if (*buflen < len) {
		CERROR("buflen %u < %u\n", *buflen, len);
		obj->len = 0;
		return -EINVAL;
	}

	if (!alloc)
		obj->data = (__u8 *) *buf;
	else {
		OBD_ALLOC_LARGE(obj->data, obj->len);
		if (!obj->data) {
			CERROR("fail to alloc %u bytes\n", obj->len);
			obj->len = 0;
			return -ENOMEM;
		}
		memcpy(obj->data, *buf, obj->len);
	}

	*((char **)buf) += len;
	*buflen -= len;

	return 0;
}

int rawobj_extract(rawobj_t *obj, __u32 **buf, __u32 *buflen)
{
	return __rawobj_extract(obj, buf, buflen, 0, 0);
}

int rawobj_extract_alloc(rawobj_t *obj, __u32 **buf, __u32 *buflen)
{
	return __rawobj_extract(obj, buf, buflen, 1, 0);
}

int rawobj_extract_local(rawobj_t *obj, __u32 **buf, __u32 *buflen)
{
	return __rawobj_extract(obj, buf, buflen, 0, 1);
}

int rawobj_extract_local_alloc(rawobj_t *obj, __u32 **buf, __u32 *buflen)
{
	return __rawobj_extract(obj, buf, buflen, 1, 1);
}

int rawobj_from_netobj(rawobj_t *rawobj, netobj_t *netobj)
{
	rawobj->len = netobj->len;
	rawobj->data = netobj->data;
	return 0;
}

int rawobj_from_netobj_alloc(rawobj_t *rawobj, netobj_t *netobj)
{
	rawobj->len = 0;
	rawobj->data = NULL;

	if (netobj->len == 0)
		return 0;

	OBD_ALLOC_LARGE(rawobj->data, netobj->len);
	if (rawobj->data == NULL)
		return -ENOMEM;

	rawobj->len = netobj->len;
	memcpy(rawobj->data, netobj->data, netobj->len);
	return 0;
}

/****************************************
 * misc more			    *
 ****************************************/

int buffer_extract_bytes(const void **buf, __u32 *buflen,
			 void *res, __u32 reslen)
{
	if (*buflen < reslen) {
		CERROR("buflen %u < %u\n", *buflen, reslen);
		return -EINVAL;
	}

	memcpy(res, *buf, reslen);
	*buf += reslen;
	*buflen -= reslen;
	return 0;
}
