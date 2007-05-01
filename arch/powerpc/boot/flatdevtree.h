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
 */

#ifndef FLATDEVTREE_H
#define FLATDEVTREE_H

#include "flatdevtree_env.h"

/* Definitions used by the flattened device tree */
#define OF_DT_HEADER            0xd00dfeed      /* marker */
#define OF_DT_BEGIN_NODE        0x1     /* Start of node, full name */
#define OF_DT_END_NODE          0x2     /* End node */
#define OF_DT_PROP              0x3     /* Property: name off, size, content */
#define OF_DT_NOP               0x4     /* nop */
#define OF_DT_END               0x9

#define OF_DT_VERSION           0x10

struct boot_param_header {
	u32 magic;              /* magic word OF_DT_HEADER */
	u32 totalsize;          /* total size of DT block */
	u32 off_dt_struct;      /* offset to structure */
	u32 off_dt_strings;     /* offset to strings */
	u32 off_mem_rsvmap;     /* offset to memory reserve map */
	u32 version;            /* format version */
	u32 last_comp_version;  /* last compatible version */
	/* version 2 fields below */
	u32 boot_cpuid_phys;    /* Physical CPU id we're booting on */
	/* version 3 fields below */
	u32 dt_strings_size;    /* size of the DT strings block */
};

struct ft_reserve {
	u64 start;
	u64 len;
};

struct ft_region {
	char *start;
	unsigned long size;
};

enum ft_rgn_id {
	FT_RSVMAP,
	FT_STRUCT,
	FT_STRINGS,
	FT_N_REGION
};

#define FT_MAX_DEPTH	50

struct ft_cxt {
	struct boot_param_header *bph;
	int max_size;           /* maximum size of tree */
	int isordered;		/* everything in standard order */
	void *(*realloc)(void *, unsigned long);
	char *str_anchor;
	char *p;		/* current insertion point in structs */
	struct ft_region rgn[FT_N_REGION];
	void *genealogy[FT_MAX_DEPTH+1];
	char **node_tbl;
	unsigned int node_max;
	unsigned int nodes_used;
};

int ft_begin_node(struct ft_cxt *cxt, const char *name);
void ft_end_node(struct ft_cxt *cxt);

void ft_begin_tree(struct ft_cxt *cxt);
void ft_end_tree(struct ft_cxt *cxt);

void ft_nop(struct ft_cxt *cxt);
int ft_prop(struct ft_cxt *cxt, const char *name,
	    const void *data, unsigned int sz);
int ft_prop_str(struct ft_cxt *cxt, const char *name, const char *str);
int ft_prop_int(struct ft_cxt *cxt, const char *name, unsigned int val);
void ft_begin(struct ft_cxt *cxt, void *blob, unsigned int max_size,
	      void *(*realloc_fn)(void *, unsigned long));
int ft_open(struct ft_cxt *cxt, void *blob, unsigned int max_size,
		unsigned int max_find_device,
		void *(*realloc_fn)(void *, unsigned long));
int ft_add_rsvmap(struct ft_cxt *cxt, u64 physaddr, u64 size);

void ft_dump_blob(const void *bphp);
void ft_merge_blob(struct ft_cxt *cxt, void *blob);
void *ft_find_device(struct ft_cxt *cxt, const char *srch_path);
void *ft_find_device_rel(struct ft_cxt *cxt, const void *top,
                         const char *srch_path);
void *ft_find_descendent(struct ft_cxt *cxt, void *top, const char *srch_path);
int ft_get_prop(struct ft_cxt *cxt, const void *phandle, const char *propname,
		void *buf, const unsigned int buflen);
int ft_set_prop(struct ft_cxt *cxt, const void *phandle, const char *propname,
		const void *buf, const unsigned int buflen);
void *ft_get_parent(struct ft_cxt *cxt, const void *phandle);
void *ft_find_node_by_prop_value(struct ft_cxt *cxt, const void *prev,
                                 const char *propname, const char *propval,
                                 int proplen);
void *ft_create_node(struct ft_cxt *cxt, const void *parent, const char *name);

#endif /* FLATDEVTREE_H */
