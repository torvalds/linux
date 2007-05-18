/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright Pantelis Antoniou 2006
 * Copyright (C) IBM Corporation 2006
 *
 * Authors: Pantelis Antoniou <pantelis@embeddedalley.com>
 *	    Hollis Blanchard <hollisb@us.ibm.com>
 *	    Mark A. Greer <mgreer@mvista.com>
 *	    Paul Mackerras <paulus@samba.org>
 */

#include <string.h>
#include <stddef.h>
#include "flatdevtree.h"
#include "flatdevtree_env.h"

#define _ALIGN(x, al)	(((x) + (al) - 1) & ~((al) - 1))

static char *ft_root_node(struct ft_cxt *cxt)
{
	return cxt->rgn[FT_STRUCT].start;
}

/* Routines for keeping node ptrs returned by ft_find_device current */
/* First entry not used b/c it would return 0 and be taken as NULL/error */
static void *ft_get_phandle(struct ft_cxt *cxt, char *node)
{
	unsigned int i;

	if (!node)
		return NULL;

	for (i = 1; i < cxt->nodes_used; i++)	/* already there? */
		if (cxt->node_tbl[i] == node)
			return (void *)i;

	if (cxt->nodes_used < cxt->node_max) {
		cxt->node_tbl[cxt->nodes_used] = node;
		return (void *)cxt->nodes_used++;
	}

	return NULL;
}

static char *ft_node_ph2node(struct ft_cxt *cxt, const void *phandle)
{
	unsigned int i = (unsigned int)phandle;

	if (i < cxt->nodes_used)
		return cxt->node_tbl[i];
	return NULL;
}

static void ft_node_update_before(struct ft_cxt *cxt, char *addr, int shift)
{
	unsigned int i;

	if (shift == 0)
		return;

	for (i = 1; i < cxt->nodes_used; i++)
		if (cxt->node_tbl[i] < addr)
			cxt->node_tbl[i] += shift;
}

static void ft_node_update_after(struct ft_cxt *cxt, char *addr, int shift)
{
	unsigned int i;

	if (shift == 0)
		return;

	for (i = 1; i < cxt->nodes_used; i++)
		if (cxt->node_tbl[i] >= addr)
			cxt->node_tbl[i] += shift;
}

/* Struct used to return info from ft_next() */
struct ft_atom {
	u32 tag;
	const char *name;
	void *data;
	u32 size;
};

/* Set ptrs to current one's info; return addr of next one */
static char *ft_next(struct ft_cxt *cxt, char *p, struct ft_atom *ret)
{
	u32 sz;

	if (p >= cxt->rgn[FT_STRUCT].start + cxt->rgn[FT_STRUCT].size)
		return NULL;

	ret->tag = be32_to_cpu(*(u32 *) p);
	p += 4;

	switch (ret->tag) {	/* Tag */
	case OF_DT_BEGIN_NODE:
		ret->name = p;
		ret->data = (void *)(p - 4);	/* start of node */
		p += _ALIGN(strlen(p) + 1, 4);
		break;
	case OF_DT_PROP:
		ret->size = sz = be32_to_cpu(*(u32 *) p);
		ret->name = cxt->str_anchor + be32_to_cpu(*(u32 *) (p + 4));
		ret->data = (void *)(p + 8);
		p += 8 + _ALIGN(sz, 4);
		break;
	case OF_DT_END_NODE:
	case OF_DT_NOP:
		break;
	case OF_DT_END:
	default:
		p = NULL;
		break;
	}

	return p;
}

#define HDR_SIZE	_ALIGN(sizeof(struct boot_param_header), 8)
#define EXPAND_INCR	1024	/* alloc this much extra when expanding */

/* See if the regions are in the standard order and non-overlapping */
static int ft_ordered(struct ft_cxt *cxt)
{
	char *p = (char *)cxt->bph + HDR_SIZE;
	enum ft_rgn_id r;

	for (r = FT_RSVMAP; r <= FT_STRINGS; ++r) {
		if (p > cxt->rgn[r].start)
			return 0;
		p = cxt->rgn[r].start + cxt->rgn[r].size;
	}
	return p <= (char *)cxt->bph + cxt->max_size;
}

/* Copy the tree to a newly-allocated region and put things in order */
static int ft_reorder(struct ft_cxt *cxt, int nextra)
{
	unsigned long tot;
	enum ft_rgn_id r;
	char *p, *pend;
	int stroff;

	tot = HDR_SIZE + EXPAND_INCR;
	for (r = FT_RSVMAP; r <= FT_STRINGS; ++r)
		tot += cxt->rgn[r].size;
	if (nextra > 0)
		tot += nextra;
	tot = _ALIGN(tot, 8);

	if (!cxt->realloc)
		return 0;
	p = cxt->realloc(NULL, tot);
	if (!p)
		return 0;

	memcpy(p, cxt->bph, sizeof(struct boot_param_header));
	/* offsets get fixed up later */

	cxt->bph = (struct boot_param_header *)p;
	cxt->max_size = tot;
	pend = p + tot;
	p += HDR_SIZE;

	memcpy(p, cxt->rgn[FT_RSVMAP].start, cxt->rgn[FT_RSVMAP].size);
	cxt->rgn[FT_RSVMAP].start = p;
	p += cxt->rgn[FT_RSVMAP].size;

	memcpy(p, cxt->rgn[FT_STRUCT].start, cxt->rgn[FT_STRUCT].size);
	ft_node_update_after(cxt, cxt->rgn[FT_STRUCT].start,
			p - cxt->rgn[FT_STRUCT].start);
	cxt->p += p - cxt->rgn[FT_STRUCT].start;
	cxt->rgn[FT_STRUCT].start = p;

	p = pend - cxt->rgn[FT_STRINGS].size;
	memcpy(p, cxt->rgn[FT_STRINGS].start, cxt->rgn[FT_STRINGS].size);
	stroff = cxt->str_anchor - cxt->rgn[FT_STRINGS].start;
	cxt->rgn[FT_STRINGS].start = p;
	cxt->str_anchor = p + stroff;

	cxt->isordered = 1;
	return 1;
}

static inline char *prev_end(struct ft_cxt *cxt, enum ft_rgn_id r)
{
	if (r > FT_RSVMAP)
		return cxt->rgn[r - 1].start + cxt->rgn[r - 1].size;
	return (char *)cxt->bph + HDR_SIZE;
}

static inline char *next_start(struct ft_cxt *cxt, enum ft_rgn_id r)
{
	if (r < FT_STRINGS)
		return cxt->rgn[r + 1].start;
	return (char *)cxt->bph + cxt->max_size;
}

/*
 * See if we can expand region rgn by nextra bytes by using up
 * free space after or before the region.
 */
static int ft_shuffle(struct ft_cxt *cxt, char **pp, enum ft_rgn_id rgn,
		int nextra)
{
	char *p = *pp;
	char *rgn_start, *rgn_end;

	rgn_start = cxt->rgn[rgn].start;
	rgn_end = rgn_start + cxt->rgn[rgn].size;
	if (nextra <= 0 || rgn_end + nextra <= next_start(cxt, rgn)) {
		/* move following stuff */
		if (p < rgn_end) {
			if (nextra < 0)
				memmove(p, p - nextra, rgn_end - p + nextra);
			else
				memmove(p + nextra, p, rgn_end - p);
			if (rgn == FT_STRUCT)
				ft_node_update_after(cxt, p, nextra);
		}
		cxt->rgn[rgn].size += nextra;
		if (rgn == FT_STRINGS)
			/* assumes strings only added at beginning */
			cxt->str_anchor += nextra;
		return 1;
	}
	if (prev_end(cxt, rgn) <= rgn_start - nextra) {
		/* move preceding stuff */
		if (p > rgn_start) {
			memmove(rgn_start - nextra, rgn_start, p - rgn_start);
			if (rgn == FT_STRUCT)
				ft_node_update_before(cxt, p, -nextra);
		}
		*pp -= nextra;
		cxt->rgn[rgn].start -= nextra;
		cxt->rgn[rgn].size += nextra;
		return 1;
	}
	return 0;
}

static int ft_make_space(struct ft_cxt *cxt, char **pp, enum ft_rgn_id rgn,
			 int nextra)
{
	unsigned long size, ssize, tot;
	char *str, *next;
	enum ft_rgn_id r;

	if (!cxt->isordered) {
		unsigned long rgn_off = *pp - cxt->rgn[rgn].start;

		if (!ft_reorder(cxt, nextra))
			return 0;

		*pp = cxt->rgn[rgn].start + rgn_off;
	}
	if (ft_shuffle(cxt, pp, rgn, nextra))
		return 1;

	/* See if there is space after the strings section */
	ssize = cxt->rgn[FT_STRINGS].size;
	if (cxt->rgn[FT_STRINGS].start + ssize
			< (char *)cxt->bph + cxt->max_size) {
		/* move strings up as far as possible */
		str = (char *)cxt->bph + cxt->max_size - ssize;
		cxt->str_anchor += str - cxt->rgn[FT_STRINGS].start;
		memmove(str, cxt->rgn[FT_STRINGS].start, ssize);
		cxt->rgn[FT_STRINGS].start = str;
		/* enough space now? */
		if (rgn >= FT_STRUCT && ft_shuffle(cxt, pp, rgn, nextra))
			return 1;
	}

	/* how much total free space is there following this region? */
	tot = 0;
	for (r = rgn; r < FT_STRINGS; ++r) {
		char *r_end = cxt->rgn[r].start + cxt->rgn[r].size;
		tot += next_start(cxt, rgn) - r_end;
	}

	/* cast is to shut gcc up; we know nextra >= 0 */
	if (tot < (unsigned int)nextra) {
		/* have to reallocate */
		char *newp, *new_start;
		int shift;

		if (!cxt->realloc)
			return 0;
		size = _ALIGN(cxt->max_size + (nextra - tot) + EXPAND_INCR, 8);
		newp = cxt->realloc(cxt->bph, size);
		if (!newp)
			return 0;
		cxt->max_size = size;
		shift = newp - (char *)cxt->bph;

		if (shift) { /* realloc can return same addr */
			cxt->bph = (struct boot_param_header *)newp;
			ft_node_update_after(cxt, cxt->rgn[FT_STRUCT].start,
					shift);
			for (r = FT_RSVMAP; r <= FT_STRINGS; ++r) {
				new_start = cxt->rgn[r].start + shift;
				cxt->rgn[r].start = new_start;
			}
			*pp += shift;
			cxt->str_anchor += shift;
		}

		/* move strings up to the end */
		str = newp + size - ssize;
		cxt->str_anchor += str - cxt->rgn[FT_STRINGS].start;
		memmove(str, cxt->rgn[FT_STRINGS].start, ssize);
		cxt->rgn[FT_STRINGS].start = str;

		if (ft_shuffle(cxt, pp, rgn, nextra))
			return 1;
	}

	/* must be FT_RSVMAP and we need to move FT_STRUCT up */
	if (rgn == FT_RSVMAP) {
		next = cxt->rgn[FT_RSVMAP].start + cxt->rgn[FT_RSVMAP].size
			+ nextra;
		ssize = cxt->rgn[FT_STRUCT].size;
		if (next + ssize >= cxt->rgn[FT_STRINGS].start)
			return 0;	/* "can't happen" */
		memmove(next, cxt->rgn[FT_STRUCT].start, ssize);
		ft_node_update_after(cxt, cxt->rgn[FT_STRUCT].start, nextra);
		cxt->rgn[FT_STRUCT].start = next;

		if (ft_shuffle(cxt, pp, rgn, nextra))
			return 1;
	}

	return 0;		/* "can't happen" */
}

static void ft_put_word(struct ft_cxt *cxt, u32 v)
{
	*(u32 *) cxt->p = cpu_to_be32(v);
	cxt->p += 4;
}

static void ft_put_bin(struct ft_cxt *cxt, const void *data, unsigned int sz)
{
	unsigned long sza = _ALIGN(sz, 4);

	/* zero out the alignment gap if necessary */
	if (sz < sza)
		*(u32 *) (cxt->p + sza - 4) = 0;

	/* copy in the data */
	memcpy(cxt->p, data, sz);

	cxt->p += sza;
}

int ft_begin_node(struct ft_cxt *cxt, const char *name)
{
	unsigned long nlen = strlen(name) + 1;
	unsigned long len = 8 + _ALIGN(nlen, 4);

	if (!ft_make_space(cxt, &cxt->p, FT_STRUCT, len))
		return -1;
	ft_put_word(cxt, OF_DT_BEGIN_NODE);
	ft_put_bin(cxt, name, strlen(name) + 1);
	return 0;
}

void ft_end_node(struct ft_cxt *cxt)
{
	ft_put_word(cxt, OF_DT_END_NODE);
}

void ft_nop(struct ft_cxt *cxt)
{
	if (ft_make_space(cxt, &cxt->p, FT_STRUCT, 4))
		ft_put_word(cxt, OF_DT_NOP);
}

#define NO_STRING	0x7fffffff

static int lookup_string(struct ft_cxt *cxt, const char *name)
{
	char *p, *end;

	p = cxt->rgn[FT_STRINGS].start;
	end = p + cxt->rgn[FT_STRINGS].size;
	while (p < end) {
		if (strcmp(p, (char *)name) == 0)
			return p - cxt->str_anchor;
		p += strlen(p) + 1;
	}

	return NO_STRING;
}

/* lookup string and insert if not found */
static int map_string(struct ft_cxt *cxt, const char *name)
{
	int off;
	char *p;

	off = lookup_string(cxt, name);
	if (off != NO_STRING)
		return off;
	p = cxt->rgn[FT_STRINGS].start;
	if (!ft_make_space(cxt, &p, FT_STRINGS, strlen(name) + 1))
		return NO_STRING;
	strcpy(p, name);
	return p - cxt->str_anchor;
}

int ft_prop(struct ft_cxt *cxt, const char *name, const void *data,
		unsigned int sz)
{
	int off, len;

	off = map_string(cxt, name);
	if (off == NO_STRING)
		return -1;

	len = 12 + _ALIGN(sz, 4);
	if (!ft_make_space(cxt, &cxt->p, FT_STRUCT, len))
		return -1;

	ft_put_word(cxt, OF_DT_PROP);
	ft_put_word(cxt, sz);
	ft_put_word(cxt, off);
	ft_put_bin(cxt, data, sz);
	return 0;
}

int ft_prop_str(struct ft_cxt *cxt, const char *name, const char *str)
{
	return ft_prop(cxt, name, str, strlen(str) + 1);
}

int ft_prop_int(struct ft_cxt *cxt, const char *name, unsigned int val)
{
	u32 v = cpu_to_be32((u32) val);

	return ft_prop(cxt, name, &v, 4);
}

/* Calculate the size of the reserved map */
static unsigned long rsvmap_size(struct ft_cxt *cxt)
{
	struct ft_reserve *res;

	res = (struct ft_reserve *)cxt->rgn[FT_RSVMAP].start;
	while (res->start || res->len)
		++res;
	return (char *)(res + 1) - cxt->rgn[FT_RSVMAP].start;
}

/* Calculate the size of the struct region by stepping through it */
static unsigned long struct_size(struct ft_cxt *cxt)
{
	char *p = cxt->rgn[FT_STRUCT].start;
	char *next;
	struct ft_atom atom;

	/* make check in ft_next happy */
	if (cxt->rgn[FT_STRUCT].size == 0)
		cxt->rgn[FT_STRUCT].size = 0xfffffffful - (unsigned long)p;

	while ((next = ft_next(cxt, p, &atom)) != NULL)
		p = next;
	return p + 4 - cxt->rgn[FT_STRUCT].start;
}

/* add `adj' on to all string offset values in the struct area */
static void adjust_string_offsets(struct ft_cxt *cxt, int adj)
{
	char *p = cxt->rgn[FT_STRUCT].start;
	char *next;
	struct ft_atom atom;
	int off;

	while ((next = ft_next(cxt, p, &atom)) != NULL) {
		if (atom.tag == OF_DT_PROP) {
			off = be32_to_cpu(*(u32 *) (p + 8));
			*(u32 *) (p + 8) = cpu_to_be32(off + adj);
		}
		p = next;
	}
}

/* start construction of the flat OF tree from scratch */
void ft_begin(struct ft_cxt *cxt, void *blob, unsigned int max_size,
		void *(*realloc_fn) (void *, unsigned long))
{
	struct boot_param_header *bph = blob;
	char *p;
	struct ft_reserve *pres;

	/* clear the cxt */
	memset(cxt, 0, sizeof(*cxt));

	cxt->bph = bph;
	cxt->max_size = max_size;
	cxt->realloc = realloc_fn;
	cxt->isordered = 1;

	/* zero everything in the header area */
	memset(bph, 0, sizeof(*bph));

	bph->magic = cpu_to_be32(OF_DT_HEADER);
	bph->version = cpu_to_be32(0x10);
	bph->last_comp_version = cpu_to_be32(0x10);

	/* start pointers */
	cxt->rgn[FT_RSVMAP].start = p = blob + HDR_SIZE;
	cxt->rgn[FT_RSVMAP].size = sizeof(struct ft_reserve);
	pres = (struct ft_reserve *)p;
	cxt->rgn[FT_STRUCT].start = p += sizeof(struct ft_reserve);
	cxt->rgn[FT_STRUCT].size = 4;
	cxt->rgn[FT_STRINGS].start = blob + max_size;
	cxt->rgn[FT_STRINGS].size = 0;

	/* init rsvmap and struct */
	pres->start = 0;
	pres->len = 0;
	*(u32 *) p = cpu_to_be32(OF_DT_END);

	cxt->str_anchor = blob;
}

/* open up an existing blob to be examined or modified */
int ft_open(struct ft_cxt *cxt, void *blob, unsigned int max_size,
		unsigned int max_find_device,
		void *(*realloc_fn) (void *, unsigned long))
{
	struct boot_param_header *bph = blob;

	/* can't cope with version < 16 */
	if (be32_to_cpu(bph->version) < 16)
		return -1;

	/* clear the cxt */
	memset(cxt, 0, sizeof(*cxt));

	/* alloc node_tbl to track node ptrs returned by ft_find_device */
	++max_find_device;
	cxt->node_tbl = realloc_fn(NULL, max_find_device * sizeof(char *));
	if (!cxt->node_tbl)
		return -1;
	memset(cxt->node_tbl, 0, max_find_device * sizeof(char *));
	cxt->node_max = max_find_device;
	cxt->nodes_used = 1;	/* don't use idx 0 b/c looks like NULL */

	cxt->bph = bph;
	cxt->max_size = max_size;
	cxt->realloc = realloc_fn;

	cxt->rgn[FT_RSVMAP].start = blob + be32_to_cpu(bph->off_mem_rsvmap);
	cxt->rgn[FT_RSVMAP].size = rsvmap_size(cxt);
	cxt->rgn[FT_STRUCT].start = blob + be32_to_cpu(bph->off_dt_struct);
	cxt->rgn[FT_STRUCT].size = struct_size(cxt);
	cxt->rgn[FT_STRINGS].start = blob + be32_to_cpu(bph->off_dt_strings);
	cxt->rgn[FT_STRINGS].size = be32_to_cpu(bph->dt_strings_size);
	/* Leave as '0' to force first ft_make_space call to do a ft_reorder
	 * and move dt to an area allocated by realloc.
	cxt->isordered = ft_ordered(cxt);
	*/

	cxt->p = cxt->rgn[FT_STRUCT].start;
	cxt->str_anchor = cxt->rgn[FT_STRINGS].start;

	return 0;
}

/* add a reserver physical area to the rsvmap */
int ft_add_rsvmap(struct ft_cxt *cxt, u64 physaddr, u64 size)
{
	char *p;
	struct ft_reserve *pres;

	p = cxt->rgn[FT_RSVMAP].start + cxt->rgn[FT_RSVMAP].size
		- sizeof(struct ft_reserve);
	if (!ft_make_space(cxt, &p, FT_RSVMAP, sizeof(struct ft_reserve)))
		return -1;

	pres = (struct ft_reserve *)p;
	pres->start = cpu_to_be64(physaddr);
	pres->len = cpu_to_be64(size);

	return 0;
}

void ft_begin_tree(struct ft_cxt *cxt)
{
	cxt->p = ft_root_node(cxt);
}

void ft_end_tree(struct ft_cxt *cxt)
{
	struct boot_param_header *bph = cxt->bph;
	char *p, *oldstr, *str, *endp;
	unsigned long ssize;
	int adj;

	if (!cxt->isordered)
		return;		/* we haven't touched anything */

	/* adjust string offsets */
	oldstr = cxt->rgn[FT_STRINGS].start;
	adj = cxt->str_anchor - oldstr;
	if (adj)
		adjust_string_offsets(cxt, adj);

	/* make strings end on 8-byte boundary */
	ssize = cxt->rgn[FT_STRINGS].size;
	endp = (char *)_ALIGN((unsigned long)cxt->rgn[FT_STRUCT].start
			+ cxt->rgn[FT_STRUCT].size + ssize, 8);
	str = endp - ssize;

	/* move strings down to end of structs */
	memmove(str, oldstr, ssize);
	cxt->str_anchor = str;
	cxt->rgn[FT_STRINGS].start = str;

	/* fill in header fields */
	p = (char *)bph;
	bph->totalsize = cpu_to_be32(endp - p);
	bph->off_mem_rsvmap = cpu_to_be32(cxt->rgn[FT_RSVMAP].start - p);
	bph->off_dt_struct = cpu_to_be32(cxt->rgn[FT_STRUCT].start - p);
	bph->off_dt_strings = cpu_to_be32(cxt->rgn[FT_STRINGS].start - p);
	bph->dt_strings_size = cpu_to_be32(ssize);
}

void *ft_find_device(struct ft_cxt *cxt, const char *srch_path)
{
	char *node;

	/* require absolute path */
	if (srch_path[0] != '/')
		return NULL;
	node = ft_find_descendent(cxt, ft_root_node(cxt), srch_path);
	return ft_get_phandle(cxt, node);
}

void *ft_find_device_rel(struct ft_cxt *cxt, const void *top,
                         const char *srch_path)
{
	char *node;

	node = ft_node_ph2node(cxt, top);
	if (node == NULL)
		return NULL;

	node = ft_find_descendent(cxt, node, srch_path);
	return ft_get_phandle(cxt, node);
}

void *ft_find_descendent(struct ft_cxt *cxt, void *top, const char *srch_path)
{
	struct ft_atom atom;
	char *p;
	const char *cp, *q;
	int cl;
	int depth = -1;
	int dmatch = 0;
	const char *path_comp[FT_MAX_DEPTH];

	cp = srch_path;
	cl = 0;
	p = top;

	while ((p = ft_next(cxt, p, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
			++depth;
			if (depth != dmatch)
				break;
			cxt->genealogy[depth] = atom.data;
			cxt->genealogy[depth + 1] = NULL;
			if (depth && !(strncmp(atom.name, cp, cl) == 0
					&& (atom.name[cl] == '/'
						|| atom.name[cl] == '\0'
						|| atom.name[cl] == '@')))
				break;
			path_comp[dmatch] = cp;
			/* it matches so far, advance to next path component */
			cp += cl;
			/* skip slashes */
			while (*cp == '/')
				++cp;
			/* we're done if this is the end of the string */
			if (*cp == 0)
				return atom.data;
			/* look for end of this component */
			q = strchr(cp, '/');
			if (q)
				cl = q - cp;
			else
				cl = strlen(cp);
			++dmatch;
			break;
		case OF_DT_END_NODE:
			if (depth == 0)
				return NULL;
			if (dmatch > depth) {
				--dmatch;
				cl = cp - path_comp[dmatch] - 1;
				cp = path_comp[dmatch];
				while (cl > 0 && cp[cl - 1] == '/')
					--cl;
			}
			--depth;
			break;
		}
	}
	return NULL;
}

void *__ft_get_parent(struct ft_cxt *cxt, void *node)
{
	int d;
	struct ft_atom atom;
	char *p;

	for (d = 0; cxt->genealogy[d] != NULL; ++d)
		if (cxt->genealogy[d] == node)
			return d > 0 ? cxt->genealogy[d - 1] : NULL;

	/* have to do it the hard way... */
	p = ft_root_node(cxt);
	d = 0;
	while ((p = ft_next(cxt, p, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
			cxt->genealogy[d] = atom.data;
			if (node == atom.data) {
				/* found it */
				cxt->genealogy[d + 1] = NULL;
				return d > 0 ? cxt->genealogy[d - 1] : NULL;
			}
			++d;
			break;
		case OF_DT_END_NODE:
			--d;
			break;
		}
	}
	return NULL;
}

void *ft_get_parent(struct ft_cxt *cxt, const void *phandle)
{
	void *node = ft_node_ph2node(cxt, phandle);
	if (node == NULL)
		return NULL;

	node = __ft_get_parent(cxt, node);
	return ft_get_phandle(cxt, node);
}

static const void *__ft_get_prop(struct ft_cxt *cxt, void *node,
                                 const char *propname, unsigned int *len)
{
	struct ft_atom atom;
	int depth = 0;

	while ((node = ft_next(cxt, node, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
			++depth;
			break;

		case OF_DT_PROP:
			if (depth != 1 || strcmp(atom.name, propname))
				break;

			if (len)
				*len = atom.size;

			return atom.data;

		case OF_DT_END_NODE:
			if (--depth <= 0)
				return NULL;
		}
	}

	return NULL;
}

int ft_get_prop(struct ft_cxt *cxt, const void *phandle, const char *propname,
		void *buf, const unsigned int buflen)
{
	const void *data;
	unsigned int size;

	void *node = ft_node_ph2node(cxt, phandle);
	if (!node)
		return -1;

	data = __ft_get_prop(cxt, node, propname, &size);
	if (data) {
		unsigned int clipped_size = min(size, buflen);
		memcpy(buf, data, clipped_size);
		return size;
	}

	return -1;
}

void *__ft_find_node_by_prop_value(struct ft_cxt *cxt, void *prev,
                                   const char *propname, const char *propval,
                                   unsigned int proplen)
{
	struct ft_atom atom;
	char *p = ft_root_node(cxt);
	char *next;
	int past_prev = prev ? 0 : 1;
	int depth = -1;

	while ((next = ft_next(cxt, p, &atom)) != NULL) {
		const void *data;
		unsigned int size;

		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
			depth++;

			if (prev == p) {
				past_prev = 1;
				break;
			}

			if (!past_prev || depth < 1)
				break;

			data = __ft_get_prop(cxt, p, propname, &size);
			if (!data || size != proplen)
				break;
			if (memcmp(data, propval, size))
				break;

			return p;

		case OF_DT_END_NODE:
			if (depth-- == 0)
				return NULL;

			break;
		}

		p = next;
	}

	return NULL;
}

void *ft_find_node_by_prop_value(struct ft_cxt *cxt, const void *prev,
                                 const char *propname, const char *propval,
                                 int proplen)
{
	void *node = NULL;

	if (prev) {
		node = ft_node_ph2node(cxt, prev);

		if (!node)
			return NULL;
	}

	node = __ft_find_node_by_prop_value(cxt, node, propname,
	                                    propval, proplen);
	return ft_get_phandle(cxt, node);
}

int ft_set_prop(struct ft_cxt *cxt, const void *phandle, const char *propname,
		const void *buf, const unsigned int buflen)
{
	struct ft_atom atom;
	void *node;
	char *p, *next;
	int nextra;

	node = ft_node_ph2node(cxt, phandle);
	if (node == NULL)
		return -1;

	next = ft_next(cxt, node, &atom);
	if (atom.tag != OF_DT_BEGIN_NODE)
		/* phandle didn't point to a node */
		return -1;
	p = next;

	while ((next = ft_next(cxt, p, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE: /* properties must go before subnodes */
		case OF_DT_END_NODE:
			/* haven't found the property, insert here */
			cxt->p = p;
			return ft_prop(cxt, propname, buf, buflen);
		case OF_DT_PROP:
			if (strcmp(atom.name, propname))
				break;
			/* found an existing property, overwrite it */
			nextra = _ALIGN(buflen, 4) - _ALIGN(atom.size, 4);
			cxt->p = atom.data;
			if (nextra && !ft_make_space(cxt, &cxt->p, FT_STRUCT,
						nextra))
				return -1;
			*(u32 *) (cxt->p - 8) = cpu_to_be32(buflen);
			ft_put_bin(cxt, buf, buflen);
			return 0;
		}
		p = next;
	}
	return -1;
}

int ft_del_prop(struct ft_cxt *cxt, const void *phandle, const char *propname)
{
	struct ft_atom atom;
	void *node;
	char *p, *next;
	int size;

	node = ft_node_ph2node(cxt, phandle);
	if (node == NULL)
		return -1;

	p = node;
	while ((next = ft_next(cxt, p, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
		case OF_DT_END_NODE:
			return -1;
		case OF_DT_PROP:
			if (strcmp(atom.name, propname))
				break;
			/* found the property, remove it */
			size = 12 + -_ALIGN(atom.size, 4);
			cxt->p = p;
			if (!ft_make_space(cxt, &cxt->p, FT_STRUCT, -size))
				return -1;
			return 0;
		}
		p = next;
	}
	return -1;
}

void *ft_create_node(struct ft_cxt *cxt, const void *parent, const char *name)
{
	struct ft_atom atom;
	char *p, *next;
	int depth = 0;

	if (parent) {
		p = ft_node_ph2node(cxt, parent);
		if (!p)
			return NULL;
	} else {
		p = ft_root_node(cxt);
	}

	while ((next = ft_next(cxt, p, &atom)) != NULL) {
		switch (atom.tag) {
		case OF_DT_BEGIN_NODE:
			++depth;
			if (depth == 1 && strcmp(atom.name, name) == 0)
				/* duplicate node name, return error */
				return NULL;
			break;
		case OF_DT_END_NODE:
			--depth;
			if (depth > 0)
				break;
			/* end of node, insert here */
			cxt->p = p;
			ft_begin_node(cxt, name);
			ft_end_node(cxt);
			return p;
		}
		p = next;
	}
	return NULL;
}
