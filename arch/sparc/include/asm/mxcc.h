/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mxcc.h:  Definitions of the Viking MXCC registers
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_MXCC_H
#define _SPARC_MXCC_H

/* These registers are accessed through ASI 0x2. */
#define MXCC_DATSTREAM       0x1C00000  /* Data stream register */
#define MXCC_SRCSTREAM       0x1C00100  /* Source stream register */
#define MXCC_DESSTREAM       0x1C00200  /* Destination stream register */
#define MXCC_RMCOUNT         0x1C00300  /* Count of references and misses */
#define MXCC_STEST           0x1C00804  /* Internal self-test */
#define MXCC_CREG            0x1C00A04  /* Control register */
#define MXCC_SREG            0x1C00B00  /* Status register */
#define MXCC_RREG            0x1C00C04  /* Reset register */
#define MXCC_EREG            0x1C00E00  /* Error code register */
#define MXCC_PREG            0x1C00F04  /* Address port register */

/* Some MXCC constants. */
#define MXCC_STREAM_SIZE     0x20       /* Size in bytes of one stream r/w */

/* The MXCC Control Register:
 *
 * ----------------------------------------------------------------------
 * |                                   | RRC | RSV |PRE|MCE|PARE|ECE|RSV|
 * ----------------------------------------------------------------------
 *  31                              10    9    8-6   5   4    3   2  1-0
 *
 * RRC: Controls what you read from MXCC_RMCOUNT reg.
 *      0=Misses 1=References
 * PRE: Prefetch enable
 * MCE: Multiple Command Enable
 * PARE: Parity enable
 * ECE: External cache enable
 */

#define MXCC_CTL_RRC   0x00000200
#define MXCC_CTL_PRE   0x00000020
#define MXCC_CTL_MCE   0x00000010
#define MXCC_CTL_PARE  0x00000008
#define MXCC_CTL_ECE   0x00000004

/* The MXCC Error Register:
 *
 * --------------------------------------------------------
 * |ME| RSV|CE|PEW|PEE|ASE|EIV| MOPC|ECODE|PRIV|RSV|HPADDR|
 * --------------------------------------------------------
 *  31   30 29  28  27  26  25 24-15  14-7   6  5-3   2-0
 *
 * ME: Multiple Errors have occurred
 * CE: Cache consistency Error
 * PEW: Parity Error during a Write operation
 * PEE: Parity Error involving the External cache
 * ASE: ASynchronous Error
 * EIV: This register is toast
 * MOPC: MXCC Operation Code for instance causing error
 * ECODE: The Error CODE
 * PRIV: A privileged mode error? 0=no 1=yes
 * HPADDR: High PhysicalADDRess bits (35-32)
 */

#define MXCC_ERR_ME     0x80000000
#define MXCC_ERR_CE     0x20000000
#define MXCC_ERR_PEW    0x10000000
#define MXCC_ERR_PEE    0x08000000
#define MXCC_ERR_ASE    0x04000000
#define MXCC_ERR_EIV    0x02000000
#define MXCC_ERR_MOPC   0x01FF8000
#define MXCC_ERR_ECODE  0x00007F80
#define MXCC_ERR_PRIV   0x00000040
#define MXCC_ERR_HPADDR 0x0000000f

/* The MXCC Port register:
 *
 * -----------------------------------------------------
 * |                | MID |                            |
 * -----------------------------------------------------
 *  31            21 20-18 17                         0
 *
 * MID: The moduleID of the cpu your read this from.
 */

#ifndef __ASSEMBLER__

static inline void mxcc_set_stream_src(unsigned long *paddr)
{
	unsigned long data0 = paddr[0];
	unsigned long data1 = paddr[1];

	__asm__ __volatile__ ("or %%g0, %0, %%g2\n\t"
			      "or %%g0, %1, %%g3\n\t"
			      "stda %%g2, [%2] %3\n\t" : :
			      "r" (data0), "r" (data1),
			      "r" (MXCC_SRCSTREAM),
			      "i" (ASI_M_MXCC) : "g2", "g3");
}

static inline void mxcc_set_stream_dst(unsigned long *paddr)
{
	unsigned long data0 = paddr[0];
	unsigned long data1 = paddr[1];

	__asm__ __volatile__ ("or %%g0, %0, %%g2\n\t"
			      "or %%g0, %1, %%g3\n\t"
			      "stda %%g2, [%2] %3\n\t" : :
			      "r" (data0), "r" (data1),
			      "r" (MXCC_DESSTREAM),
			      "i" (ASI_M_MXCC) : "g2", "g3");
}

static inline unsigned long mxcc_get_creg(void)
{
	unsigned long mxcc_control;

	__asm__ __volatile__("set 0xffffffff, %%g2\n\t"
			     "set 0xffffffff, %%g3\n\t"
			     "stda %%g2, [%1] %2\n\t"
			     "lda [%3] %2, %0\n\t" :
			     "=r" (mxcc_control) :
			     "r" (MXCC_EREG), "i" (ASI_M_MXCC),
			     "r" (MXCC_CREG) : "g2", "g3");
	return mxcc_control;
}

static inline void mxcc_set_creg(unsigned long mxcc_control)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (mxcc_control), "r" (MXCC_CREG),
			     "i" (ASI_M_MXCC));
}

#endif /* !__ASSEMBLER__ */

#endif /* !(_SPARC_MXCC_H) */
