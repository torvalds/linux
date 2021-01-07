/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Hewlett Packard Enterprise Development LP. All rights reserved.
 */

#ifndef _ASM_UV_GEO_H
#define _ASM_UV_GEO_H

/* Type declaractions */

/* Size of a geoid_s structure (must be before decl. of geoid_u) */
#define GEOID_SIZE	8

/* Fields common to all substructures */
struct geo_common_s {
	unsigned char type;		/* What type of h/w is named by this geoid_s */
	unsigned char blade;
	unsigned char slot;		/* slot is IRU */
	unsigned char upos;
	unsigned char rack;
};

/* Additional fields for particular types of hardware */
struct geo_node_s {
	struct geo_common_s common;		/* No additional fields needed */
};

struct geo_rtr_s {
	struct geo_common_s common;		/* No additional fields needed */
};

struct geo_iocntl_s {
	struct geo_common_s common;		/* No additional fields needed */
};

struct geo_pcicard_s {
	struct geo_iocntl_s common;
	char bus;				/* Bus/widget number */
	char slot;				/* PCI slot number */
};

/* Subcomponents of a node */
struct geo_cpu_s {
	struct geo_node_s node;
	unsigned char	socket:4,	/* Which CPU on the node */
			thread:4;
	unsigned char	core;
};

struct geo_mem_s {
	struct geo_node_s node;
	char membus;			/* The memory bus on the node */
	char memslot;			/* The memory slot on the bus */
};

union geoid_u {
	struct geo_common_s common;
	struct geo_node_s node;
	struct geo_iocntl_s iocntl;
	struct geo_pcicard_s pcicard;
	struct geo_rtr_s rtr;
	struct geo_cpu_s cpu;
	struct geo_mem_s mem;
	char padsize[GEOID_SIZE];
};

/* Defined constants */

#define GEO_MAX_LEN	48

#define GEO_TYPE_INVALID	0
#define GEO_TYPE_MODULE		1
#define GEO_TYPE_NODE		2
#define GEO_TYPE_RTR		3
#define GEO_TYPE_IOCNTL		4
#define GEO_TYPE_IOCARD		5
#define GEO_TYPE_CPU		6
#define GEO_TYPE_MEM		7
#define GEO_TYPE_MAX		(GEO_TYPE_MEM+1)

static inline int geo_rack(union geoid_u g)
{
	return (g.common.type == GEO_TYPE_INVALID) ?
		-1 : g.common.rack;
}

static inline int geo_slot(union geoid_u g)
{
	return (g.common.type == GEO_TYPE_INVALID) ?
		-1 : g.common.upos;
}

static inline int geo_blade(union geoid_u g)
{
	return (g.common.type == GEO_TYPE_INVALID) ?
		-1 : g.common.blade * 2 + g.common.slot;
}

#endif /* _ASM_UV_GEO_H */
