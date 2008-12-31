/*
 * This file does the necessary interface mapping between the bootwrapper
 * device tree operations and the interface provided by shared source
 * files flatdevicetree.[ch].
 *
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <stddef.h>
#include <stdio.h>
#include <page.h>
#include <libfdt.h>
#include "ops.h"

#define DEBUG	0
#define BAD_ERROR(err)	(((err) < 0) \
			 && ((err) != -FDT_ERR_NOTFOUND) \
			 && ((err) != -FDT_ERR_EXISTS))

#define check_err(err) \
	({ \
		if (BAD_ERROR(err) || ((err < 0) && DEBUG)) \
			printf("%s():%d  %s\n\r", __func__, __LINE__, \
			       fdt_strerror(err)); \
		if (BAD_ERROR(err)) \
			exit(); \
		(err < 0) ? -1 : 0; \
	})

#define offset_devp(off)	\
	({ \
		int _offset = (off); \
		check_err(_offset) ? NULL : (void *)(_offset+1); \
	})

#define devp_offset_find(devp)	(((int)(devp))-1)
#define devp_offset(devp)	(devp ? ((int)(devp))-1 : 0)

static void *fdt;
static void *buf; /* = NULL */

#define EXPAND_GRANULARITY	1024

static void expand_buf(int minexpand)
{
	int size = fdt_totalsize(fdt);
	int rc;

	size = _ALIGN(size + minexpand, EXPAND_GRANULARITY);
	buf = platform_ops.realloc(buf, size);
	if (!buf)
		fatal("Couldn't find %d bytes to expand device tree\n\r", size);
	rc = fdt_open_into(fdt, buf, size);
	if (rc != 0)
		fatal("Couldn't expand fdt into new buffer: %s\n\r",
		      fdt_strerror(rc));

	fdt = buf;
}

static void *fdt_wrapper_finddevice(const char *path)
{
	return offset_devp(fdt_path_offset(fdt, path));
}

static int fdt_wrapper_getprop(const void *devp, const char *name,
			       void *buf, const int buflen)
{
	const void *p;
	int len;

	p = fdt_getprop(fdt, devp_offset(devp), name, &len);
	if (!p)
		return check_err(len);
	memcpy(buf, p, min(len, buflen));
	return len;
}

static int fdt_wrapper_setprop(const void *devp, const char *name,
			       const void *buf, const int len)
{
	int rc;

	rc = fdt_setprop(fdt, devp_offset(devp), name, buf, len);
	if (rc == -FDT_ERR_NOSPACE) {
		expand_buf(len + 16);
		rc = fdt_setprop(fdt, devp_offset(devp), name, buf, len);
	}

	return check_err(rc);
}

static int fdt_wrapper_del_node(const void *devp)
{
	return fdt_del_node(fdt, devp_offset(devp));
}

static void *fdt_wrapper_get_parent(const void *devp)
{
	return offset_devp(fdt_parent_offset(fdt, devp_offset(devp)));
}

static void *fdt_wrapper_create_node(const void *devp, const char *name)
{
	int offset;

	offset = fdt_add_subnode(fdt, devp_offset(devp), name);
	if (offset == -FDT_ERR_NOSPACE) {
		expand_buf(strlen(name) + 16);
		offset = fdt_add_subnode(fdt, devp_offset(devp), name);
	}

	return offset_devp(offset);
}

static void *fdt_wrapper_find_node_by_prop_value(const void *prev,
						 const char *name,
						 const char *val,
						 int len)
{
	int offset = fdt_node_offset_by_prop_value(fdt, devp_offset_find(prev),
	                                           name, val, len);
	return offset_devp(offset);
}

static void *fdt_wrapper_find_node_by_compatible(const void *prev,
						 const char *val)
{
	int offset = fdt_node_offset_by_compatible(fdt, devp_offset_find(prev),
	                                           val);
	return offset_devp(offset);
}

static char *fdt_wrapper_get_path(const void *devp, char *buf, int len)
{
	int rc;

	rc = fdt_get_path(fdt, devp_offset(devp), buf, len);
	if (check_err(rc))
		return NULL;
	return buf;
}

static unsigned long fdt_wrapper_finalize(void)
{
	int rc;

	rc = fdt_pack(fdt);
	if (rc != 0)
		fatal("Couldn't pack flat tree: %s\n\r",
		      fdt_strerror(rc));
	return (unsigned long)fdt;
}

void fdt_init(void *blob)
{
	int err;
	int bufsize;

	dt_ops.finddevice = fdt_wrapper_finddevice;
	dt_ops.getprop = fdt_wrapper_getprop;
	dt_ops.setprop = fdt_wrapper_setprop;
	dt_ops.get_parent = fdt_wrapper_get_parent;
	dt_ops.create_node = fdt_wrapper_create_node;
	dt_ops.find_node_by_prop_value = fdt_wrapper_find_node_by_prop_value;
	dt_ops.find_node_by_compatible = fdt_wrapper_find_node_by_compatible;
	dt_ops.del_node = fdt_wrapper_del_node;
	dt_ops.get_path = fdt_wrapper_get_path;
	dt_ops.finalize = fdt_wrapper_finalize;

	/* Make sure the dt blob is the right version and so forth */
	fdt = blob;
	bufsize = fdt_totalsize(fdt) + EXPAND_GRANULARITY;
	buf = malloc(bufsize);
	if(!buf)
		fatal("malloc failed. can't relocate the device tree\n\r");

	err = fdt_open_into(fdt, buf, bufsize);

	if (err != 0)
		fatal("fdt_init(): %s\n\r", fdt_strerror(err));

	fdt = buf;
}
