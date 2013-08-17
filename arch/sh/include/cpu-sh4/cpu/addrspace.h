/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Kaz Kojima
 *
 * Defitions for the address spaces of the SH-4 CPUs.
 */
#ifndef __ASM_CPU_SH4_ADDRSPACE_H
#define __ASM_CPU_SH4_ADDRSPACE_H

#define P0SEG		0x00000000
#define P1SEG		0x80000000
#define P2SEG		0xa0000000
#define P3SEG		0xc0000000
#define P4SEG		0xe0000000

/* Detailed P4SEG  */
#define P4SEG_STORE_QUE	(P4SEG)
#define P4SEG_IC_ADDR	0xf0000000
#define P4SEG_IC_DATA	0xf1000000
#define P4SEG_ITLB_ADDR	0xf2000000
#define P4SEG_ITLB_DATA	0xf3000000
#define P4SEG_OC_ADDR	0xf4000000
#define P4SEG_OC_DATA	0xf5000000
#define P4SEG_TLB_ADDR	0xf6000000
#define P4SEG_TLB_DATA	0xf7000000
#define P4SEG_REG_BASE	0xff000000

#define PA_AREA0	0x00000000
#define PA_AREA1	0x04000000
#define PA_AREA2	0x08000000
#define PA_AREA3	0x0c000000
#define PA_AREA4	0x10000000
#define PA_AREA5	0x14000000
#define PA_AREA6	0x18000000
#define PA_AREA7	0x1c000000

#define PA_AREA5_IO	0xb4000000	/* Area 5 IO Memory */
#define PA_AREA6_IO	0xb8000000	/* Area 6 IO Memory */

#endif /* __ASM_CPU_SH4_ADDRSPACE_H */

