/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 *
 * libfdt is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 *
 *  a) This library is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This library is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this library; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 *     MA 02110-1301 USA
 *
 * Alternatively,
 *
 *  b) Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *     1. Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *     2. Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

#define CHECK_HEADER(fdt) \
	{ \
		int err; \
		if ((err = fdt_check_header(fdt)) != 0) \
			return err; \
	}

static int nodename_eq(const void *fdt, int offset,
		       const char *s, int len)
{
	const char *p = fdt_offset_ptr(fdt, offset, len+1);

	if (! p)
		/* short match */
		return 0;

	if (memcmp(p, s, len) != 0)
		return 0;

	if (p[len] == '\0')
		return 1;
	else if (!memchr(s, '@', len) && (p[len] == '@'))
		return 1;
	else
		return 0;
}

const char *fdt_string(const void *fdt, int stroffset)
{
	return (char *)fdt + fdt_off_dt_strings(fdt) + stroffset;
}

int fdt_get_mem_rsv(const void *fdt, int n, uint64_t *address, uint64_t *size)
{
	CHECK_HEADER(fdt);
	*address = fdt64_to_cpu(_fdt_mem_rsv(fdt, n)->address);
	*size = fdt64_to_cpu(_fdt_mem_rsv(fdt, n)->size);
	return 0;
}

int fdt_num_mem_rsv(const void *fdt)
{
	int i = 0;

	while (fdt64_to_cpu(_fdt_mem_rsv(fdt, i)->size) != 0)
		i++;
	return i;
}

int fdt_subnode_offset_namelen(const void *fdt, int parentoffset,
			       const char *name, int namelen)
{
	int level = 0;
	uint32_t tag;
	int offset, nextoffset;

	CHECK_HEADER(fdt);

	tag = fdt_next_tag(fdt, parentoffset, &nextoffset);
	if (tag != FDT_BEGIN_NODE)
		return -FDT_ERR_BADOFFSET;

	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		switch (tag) {
		case FDT_END:
			return -FDT_ERR_TRUNCATED;

		case FDT_BEGIN_NODE:
			level++;
			if (level != 1)
				continue;
			if (nodename_eq(fdt, offset+FDT_TAGSIZE, name, namelen))
				/* Found it! */
				return offset;
			break;

		case FDT_END_NODE:
			level--;
			break;

		case FDT_PROP:
		case FDT_NOP:
			break;

		default:
			return -FDT_ERR_BADSTRUCTURE;
		}
	} while (level >= 0);

	return -FDT_ERR_NOTFOUND;
}

int fdt_subnode_offset(const void *fdt, int parentoffset,
		       const char *name)
{
	return fdt_subnode_offset_namelen(fdt, parentoffset, name, strlen(name));
}

int fdt_path_offset(const void *fdt, const char *path)
{
	const char *end = path + strlen(path);
	const char *p = path;
	int offset = 0;

	CHECK_HEADER(fdt);

	if (*path != '/')
		return -FDT_ERR_BADPATH;

	while (*p) {
		const char *q;

		while (*p == '/')
			p++;
		if (! *p)
			return offset;
		q = strchr(p, '/');
		if (! q)
			q = end;

		offset = fdt_subnode_offset_namelen(fdt, offset, p, q-p);
		if (offset < 0)
			return offset;

		p = q;
	}

	return offset;
}

const char *fdt_get_name(const void *fdt, int nodeoffset, int *len)
{
	const struct fdt_node_header *nh;
	int err;

	if ((err = fdt_check_header(fdt)) != 0)
		goto fail;

	err = -FDT_ERR_BADOFFSET;
	nh = fdt_offset_ptr(fdt, nodeoffset, sizeof(*nh));
	if (!nh || (fdt32_to_cpu(nh->tag) != FDT_BEGIN_NODE))
		goto fail;

	if (len)
		*len = strlen(nh->name);

	return nh->name;

 fail:
	if (len)
		*len = err;
	return NULL;
}

const struct fdt_property *fdt_get_property(const void *fdt,
					    int nodeoffset,
					    const char *name, int *lenp)
{
	uint32_t tag;
	const struct fdt_property *prop;
	int namestroff;
	int offset, nextoffset;
	int err;

	if ((err = fdt_check_header(fdt)) != 0)
		goto fail;

	err = -FDT_ERR_BADOFFSET;
	if (nodeoffset % FDT_TAGSIZE)
		goto fail;

	tag = fdt_next_tag(fdt, nodeoffset, &nextoffset);
	if (tag != FDT_BEGIN_NODE)
		goto fail;

	do {
		offset = nextoffset;

		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag) {
		case FDT_END:
			err = -FDT_ERR_TRUNCATED;
			goto fail;

		case FDT_BEGIN_NODE:
		case FDT_END_NODE:
		case FDT_NOP:
			break;

		case FDT_PROP:
			err = -FDT_ERR_BADSTRUCTURE;
			prop = fdt_offset_ptr(fdt, offset, sizeof(*prop));
			if (! prop)
				goto fail;
			namestroff = fdt32_to_cpu(prop->nameoff);
			if (streq(fdt_string(fdt, namestroff), name)) {
				/* Found it! */
				int len = fdt32_to_cpu(prop->len);
				prop = fdt_offset_ptr(fdt, offset,
						      sizeof(*prop)+len);
				if (! prop)
					goto fail;

				if (lenp)
					*lenp = len;

				return prop;
			}
			break;

		default:
			err = -FDT_ERR_BADSTRUCTURE;
			goto fail;
		}
	} while ((tag != FDT_BEGIN_NODE) && (tag != FDT_END_NODE));

	err = -FDT_ERR_NOTFOUND;
 fail:
	if (lenp)
		*lenp = err;
	return NULL;
}

const void *fdt_getprop(const void *fdt, int nodeoffset,
		  const char *name, int *lenp)
{
	const struct fdt_property *prop;

	prop = fdt_get_property(fdt, nodeoffset, name, lenp);
	if (! prop)
		return NULL;

	return prop->data;
}

uint32_t fdt_get_phandle(const void *fdt, int nodeoffset)
{
	const uint32_t *php;
	int len;

	php = fdt_getprop(fdt, nodeoffset, "linux,phandle", &len);
	if (!php || (len != sizeof(*php)))
		return 0;

	return fdt32_to_cpu(*php);
}

int fdt_get_path(const void *fdt, int nodeoffset, char *buf, int buflen)
{
	uint32_t tag;
	int p = 0, overflow = 0;
	int offset, nextoffset, namelen;
	const char *name;

	CHECK_HEADER(fdt);

	tag = fdt_next_tag(fdt, 0, &nextoffset);
	if (tag != FDT_BEGIN_NODE)
		return -FDT_ERR_BADSTRUCTURE;

	if (buflen < 2)
		return -FDT_ERR_NOSPACE;
	buf[0] = '/';
	p = 1;

	while (nextoffset <= nodeoffset) {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag) {
		case FDT_END:
			return -FDT_ERR_BADOFFSET;

		case FDT_BEGIN_NODE:
			name = fdt_get_name(fdt, offset, &namelen);
			if (!name)
				return namelen;
			if (overflow || ((p + namelen + 1) > buflen)) {
				overflow++;
				break;
			}
			memcpy(buf + p, name, namelen);
			p += namelen;
			buf[p++] = '/';
			break;

		case FDT_END_NODE:
			if (overflow) {
				overflow--;
				break;
			}
			do {
				p--;
			} while  (buf[p-1] != '/');
			break;

		case FDT_PROP:
		case FDT_NOP:
			break;

		default:
			return -FDT_ERR_BADSTRUCTURE;
		}
	}

	if (overflow)
		return -FDT_ERR_NOSPACE;

	if (p > 1) /* special case so that root path is "/", not "" */
		p--;
	buf[p] = '\0';
	return p;
}

int fdt_supernode_atdepth_offset(const void *fdt, int nodeoffset,
				 int supernodedepth, int *nodedepth)
{
	int level = -1;
	uint32_t tag;
	int offset, nextoffset = 0;
	int supernodeoffset = -FDT_ERR_INTERNAL;

	CHECK_HEADER(fdt);

	if (supernodedepth < 0)
		return -FDT_ERR_NOTFOUND;

	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag) {
		case FDT_END:
			return -FDT_ERR_BADOFFSET;

		case FDT_BEGIN_NODE:
			level++;
			if (level == supernodedepth)
				supernodeoffset = offset;
			break;

		case FDT_END_NODE:
			level--;
			break;

		case FDT_PROP:
		case FDT_NOP:
			break;

		default:
			return -FDT_ERR_BADSTRUCTURE;
		}
	} while (offset < nodeoffset);

	if (nodedepth)
		*nodedepth = level;

	if (supernodedepth > level)
		return -FDT_ERR_NOTFOUND;
	return supernodeoffset;
}

int fdt_node_depth(const void *fdt, int nodeoffset)
{
	int nodedepth;
	int err;

	err = fdt_supernode_atdepth_offset(fdt, nodeoffset, 0, &nodedepth);
	if (err)
		return (err < 0) ? err : -FDT_ERR_INTERNAL;
	return nodedepth;
}

int fdt_parent_offset(const void *fdt, int nodeoffset)
{
	int nodedepth = fdt_node_depth(fdt, nodeoffset);

	if (nodedepth < 0)
		return nodedepth;
	return fdt_supernode_atdepth_offset(fdt, nodeoffset,
					    nodedepth - 1, NULL);
}

int fdt_node_offset_by_prop_value(const void *fdt, int startoffset,
				  const char *propname,
				  const void *propval, int proplen)
{
	uint32_t tag;
	int offset, nextoffset;
	const void *val;
	int len;

	CHECK_HEADER(fdt);

	if (startoffset >= 0) {
		tag = fdt_next_tag(fdt, startoffset, &nextoffset);
		if (tag != FDT_BEGIN_NODE)
			return -FDT_ERR_BADOFFSET;
	} else {
		nextoffset = 0;
	}

	/* FIXME: The algorithm here is pretty horrible: we scan each
	 * property of a node in fdt_getprop(), then if that didn't
	 * find what we want, we scan over them again making our way
	 * to the next node.  Still it's the easiest to implement
	 * approach; performance can come later. */
	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		switch (tag) {
		case FDT_BEGIN_NODE:
			val = fdt_getprop(fdt, offset, propname, &len);
			if (val
			    && (len == proplen)
			    && (memcmp(val, propval, len) == 0))
				return offset;
			break;

		case FDT_PROP:
		case FDT_END:
		case FDT_END_NODE:
		case FDT_NOP:
			break;

		default:
			return -FDT_ERR_BADSTRUCTURE;
		}
	} while (tag != FDT_END);

	return -FDT_ERR_NOTFOUND;
}

int fdt_node_offset_by_phandle(const void *fdt, uint32_t phandle)
{
	if ((phandle == 0) || (phandle == -1))
		return -FDT_ERR_BADPHANDLE;
	phandle = cpu_to_fdt32(phandle);
	return fdt_node_offset_by_prop_value(fdt, -1, "linux,phandle",
					     &phandle, sizeof(phandle));
}

int _stringlist_contains(const void *strlist, int listlen, const char *str)
{
	int len = strlen(str);
	const void *p;

	while (listlen >= len) {
		if (memcmp(str, strlist, len+1) == 0)
			return 1;
		p = memchr(strlist, '\0', listlen);
		if (!p)
			return 0; /* malformed strlist.. */
		listlen -= (p-strlist) + 1;
		strlist = p + 1;
	}
	return 0;
}

int fdt_node_check_compatible(const void *fdt, int nodeoffset,
			      const char *compatible)
{
	const void *prop;
	int len;

	prop = fdt_getprop(fdt, nodeoffset, "compatible", &len);
	if (!prop)
		return len;
	if (_stringlist_contains(prop, len, compatible))
		return 0;
	else
		return 1;
}

int fdt_node_offset_by_compatible(const void *fdt, int startoffset,
				  const char *compatible)
{
	uint32_t tag;
	int offset, nextoffset;
	int err;

	CHECK_HEADER(fdt);

	if (startoffset >= 0) {
		tag = fdt_next_tag(fdt, startoffset, &nextoffset);
		if (tag != FDT_BEGIN_NODE)
			return -FDT_ERR_BADOFFSET;
	} else {
		nextoffset = 0;
	}

	/* FIXME: The algorithm here is pretty horrible: we scan each
	 * property of a node in fdt_node_check_compatible(), then if
	 * that didn't find what we want, we scan over them again
	 * making our way to the next node.  Still it's the easiest to
	 * implement approach; performance can come later. */
	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		switch (tag) {
		case FDT_BEGIN_NODE:
			err = fdt_node_check_compatible(fdt, offset,
							compatible);
			if ((err < 0)
			    && (err != -FDT_ERR_NOTFOUND))
				return err;
			else if (err == 0)
				return offset;
			break;

		case FDT_PROP:
		case FDT_END:
		case FDT_END_NODE:
		case FDT_NOP:
			break;

		default:
			return -FDT_ERR_BADSTRUCTURE;
		}
	} while (tag != FDT_END);

	return -FDT_ERR_NOTFOUND;
}
