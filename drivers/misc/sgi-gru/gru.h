/*
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __GRU_H__
#define __GRU_H__

/*
 * GRU architectural definitions
 */
#define GRU_CACHE_LINE_BYTES		64
#define GRU_HANDLE_STRIDE		256
#define GRU_CB_BASE			0
#define GRU_DS_BASE			0x20000

/*
 * Size used to map GRU GSeg
 */
#if defined(CONFIG_IA64)
#define GRU_GSEG_PAGESIZE	(256 * 1024UL)
#elif defined(CONFIG_X86_64)
#define GRU_GSEG_PAGESIZE	(256 * 1024UL)		/* ZZZ 2MB ??? */
#else
#error "Unsupported architecture"
#endif

/*
 * Structure for obtaining GRU resource information
 */
struct gru_chiplet_info {
	int	node;
	int	chiplet;
	int	blade;
	int	total_dsr_bytes;
	int	total_cbr;
	int	total_user_dsr_bytes;
	int	total_user_cbr;
	int	free_user_dsr_bytes;
	int	free_user_cbr;
};

/* Flags for GRU options on the gru_create_context() call */
/* Select one of the follow 4 options to specify how TLB misses are handled */
#define GRU_OPT_MISS_DEFAULT	0x0000	/* Use default mode */
#define GRU_OPT_MISS_USER_POLL	0x0001	/* User will poll CB for faults */
#define GRU_OPT_MISS_FMM_INTR	0x0002	/* Send interrupt to cpu to
					   handle fault */
#define GRU_OPT_MISS_FMM_POLL	0x0003	/* Use system polling thread */
#define GRU_OPT_MISS_MASK	0x0003	/* Mask for TLB MISS option */



#endif		/* __GRU_H__ */
