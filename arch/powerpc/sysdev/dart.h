/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 */

#ifndef _POWERPC_SYSDEV_DART_H
#define _POWERPC_SYSDEV_DART_H


/* Offset from base to control register */
#define DART_CNTL	0

/* Offset from base to exception register */
#define DART_EXCP_U3	0x10
/* Offset from base to TLB tag registers */
#define DART_TAGS_U3	0x1000

/* U4 registers */
#define DART_BASE_U4	0x10
#define DART_SIZE_U4	0x20
#define DART_EXCP_U4	0x30
#define DART_TAGS_U4	0x1000

/* Control Register fields */

/* U3 registers */
#define DART_CNTL_U3_BASE_MASK	0xfffff
#define DART_CNTL_U3_BASE_SHIFT	12
#define DART_CNTL_U3_FLUSHTLB	0x400
#define DART_CNTL_U3_ENABLE	0x200
#define DART_CNTL_U3_SIZE_MASK	0x1ff
#define DART_CNTL_U3_SIZE_SHIFT	0

/* U4 registers */
#define DART_BASE_U4_BASE_MASK	0xffffff
#define DART_BASE_U4_BASE_SHIFT	0
#define DART_CNTL_U4_ENABLE	0x80000000
#define DART_CNTL_U4_IONE	0x40000000
#define DART_CNTL_U4_FLUSHTLB	0x20000000
#define DART_CNTL_U4_IDLE	0x10000000
#define DART_CNTL_U4_PAR_EN	0x08000000
#define DART_CNTL_U4_IONE_MASK	0x07ffffff
#define DART_SIZE_U4_SIZE_MASK	0x1fff
#define DART_SIZE_U4_SIZE_SHIFT	0

#define DART_REG(r)	(dart + ((r) >> 2))
#define DART_IN(r)	(in_be32(DART_REG(r)))
#define DART_OUT(r,v)	(out_be32(DART_REG(r), (v)))


/* size of table in pages */


/* DART table fields */

#define DARTMAP_VALID   0x80000000
#define DARTMAP_RPNMASK 0x00ffffff


#define DART_PAGE_SHIFT		12
#define DART_PAGE_SIZE		(1 << DART_PAGE_SHIFT)


#endif /* _POWERPC_SYSDEV_DART_H */
