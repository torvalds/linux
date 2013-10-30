/*
 * Machine check exception header file.
 *
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#ifndef __ASM_PPC64_MCE_H__
#define __ASM_PPC64_MCE_H__

#include <linux/bitops.h>

/*
 * Machine Check bits on power7 and power8
 */
#define P7_SRR1_MC_LOADSTORE(srr1)	((srr1) & PPC_BIT(42)) /* P8 too */

/* SRR1 bits for machine check (On Power7 and Power8) */
#define P7_SRR1_MC_IFETCH(srr1)	((srr1) & PPC_BITMASK(43, 45)) /* P8 too */

#define P7_SRR1_MC_IFETCH_UE		(0x1 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_PARITY	(0x2 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_MULTIHIT	(0x3 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_BOTH	(0x4 << PPC_BITLSHIFT(45))
#define P7_SRR1_MC_IFETCH_TLB_MULTIHIT	(0x5 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_TLB_RELOAD	(0x6 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_IFU_INTERNAL	(0x7 << PPC_BITLSHIFT(45))

/* SRR1 bits for machine check (On Power8) */
#define P8_SRR1_MC_IFETCH_ERAT_MULTIHIT	(0x4 << PPC_BITLSHIFT(45))

/* DSISR bits for machine check (On Power7 and Power8) */
#define P7_DSISR_MC_UE			(PPC_BIT(48))	/* P8 too */
#define P7_DSISR_MC_UE_TABLEWALK	(PPC_BIT(49))	/* P8 too */
#define P7_DSISR_MC_ERAT_MULTIHIT	(PPC_BIT(52))	/* P8 too */
#define P7_DSISR_MC_TLB_MULTIHIT_MFTLB	(PPC_BIT(53))	/* P8 too */
#define P7_DSISR_MC_SLB_PARITY_MFSLB	(PPC_BIT(55))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT	(PPC_BIT(56))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT_PARITY	(PPC_BIT(57))	/* P8 too */

/*
 * DSISR bits for machine check (Power8) in addition to above.
 * Secondary DERAT Multihit
 */
#define P8_DSISR_MC_ERAT_MULTIHIT_SEC	(PPC_BIT(54))

/* SLB error bits */
#define P7_DSISR_MC_SLB_ERRORS		(P7_DSISR_MC_ERAT_MULTIHIT | \
					 P7_DSISR_MC_SLB_PARITY_MFSLB | \
					 P7_DSISR_MC_SLB_MULTIHIT | \
					 P7_DSISR_MC_SLB_MULTIHIT_PARITY)

#endif /* __ASM_PPC64_MCE_H__ */
