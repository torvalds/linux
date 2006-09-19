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

#include "types.h"

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

#endif /* FLATDEVTREE_H */
