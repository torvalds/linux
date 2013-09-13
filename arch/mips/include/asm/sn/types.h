/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_SN_TYPES_H
#define _ASM_SN_TYPES_H

#include <linux/types.h>

typedef unsigned long	cpuid_t;
typedef unsigned long	cnodemask_t;
typedef signed short	nasid_t;	/* node id in numa-as-id space */
typedef signed short	cnodeid_t;	/* node id in compact-id space */
typedef signed char	partid_t;	/* partition ID type */
typedef signed short	moduleid_t;	/* user-visible module number type */
typedef signed short	cmoduleid_t;	/* kernel compact module id type */
typedef unsigned char	clusterid_t;	/* Clusterid of the cell */

typedef dev_t		vertex_hdl_t;	/* hardware graph vertex handle */

#endif /* _ASM_SN_TYPES_H */
