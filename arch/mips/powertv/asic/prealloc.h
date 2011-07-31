/*
 * Definitions for memory preallocations
 *
 * Copyright (C) 2005-2009 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _ARCH_MIPS_POWERTV_ASIC_PREALLOC_H
#define _ARCH_MIPS_POWERTV_ASIC_PREALLOC_H

#define KIBIBYTE(n) ((n) * 1024)    /* Number of kibibytes */
#define MEBIBYTE(n) ((n) * KIBIBYTE(1024)) /* Number of mebibytes */

/* "struct resource" array element definition */
#define PREALLOC(NAME, START, END, FLAGS) {	\
		.name = (NAME),			\
		.start = (START),		\
		.end = (END),			\
		.flags = (FLAGS)		\
	},

/* Individual resources in the preallocated resource arrays are defined using
 *  macros.  These macros are conditionally defined based on their
 *  corresponding kernel configuration flag:
 *    - CONFIG_PREALLOC_NORMAL: preallocate resources for a normal settop box
 *    - CONFIG_PREALLOC_TFTP: preallocate the TFTP download resource
 *    - CONFIG_PREALLOC_DOCSIS: preallocate the DOCSIS resource
 *    - CONFIG_PREALLOC_PMEM: reserve space for persistent memory
 */
#ifdef CONFIG_PREALLOC_NORMAL
#define PREALLOC_NORMAL(name, start, end, flags) \
   PREALLOC(name, start, end, flags)
#else
#define PREALLOC_NORMAL(name, start, end, flags)
#endif

#ifdef CONFIG_PREALLOC_TFTP
#define PREALLOC_TFTP(name, start, end, flags) \
   PREALLOC(name, start, end, flags)
#else
#define PREALLOC_TFTP(name, start, end, flags)
#endif

#ifdef CONFIG_PREALLOC_DOCSIS
#define PREALLOC_DOCSIS(name, start, end, flags) \
   PREALLOC(name, start, end, flags)
#else
#define PREALLOC_DOCSIS(name, start, end, flags)
#endif

#ifdef CONFIG_PREALLOC_PMEM
#define PREALLOC_PMEM(name, start, end, flags) \
   PREALLOC(name, start, end, flags)
#else
#define PREALLOC_PMEM(name, start, end, flags)
#endif
#endif
