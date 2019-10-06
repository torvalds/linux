/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016,2017 IBM Corporation.
 */
#ifndef _ASM_POWERPC_XIVE_REGS_H
#define _ASM_POWERPC_XIVE_REGS_H

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
 *
 * Each interrupt source has a 2-bit state machine called ESB
 * which can be controlled by MMIO. It's made of 2 bits, P and
 * Q. P indicates that an interrupt is pending (has been sent
 * to a queue and is waiting for an EOI). Q indicates that the
 * interrupt has been triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee
 * that a given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 *
 * The following offsets into the ESB MMIO allow to read or
 * manipulate the PQ bits. They must be used with an 8-bytes
 * load instruction. They all return the previous state of the
 * interrupt (atomically).
 *
 * Additionally, some ESB pages support doing an EOI via a
 * store at 0 and some ESBs support doing a trigger via a
 * separate trigger page.
 */
#define XIVE_ESB_STORE_EOI	0x400 /* Store */
#define XIVE_ESB_LOAD_EOI	0x000 /* Load */
#define XIVE_ESB_GET		0x800 /* Load */
#define XIVE_ESB_SET_PQ_00	0xc00 /* Load */
#define XIVE_ESB_SET_PQ_01	0xd00 /* Load */
#define XIVE_ESB_SET_PQ_10	0xe00 /* Load */
#define XIVE_ESB_SET_PQ_11	0xf00 /* Load */

#define XIVE_ESB_VAL_P		0x2
#define XIVE_ESB_VAL_Q		0x1

/*
 * Thread Management (aka "TM") registers
 */

/* TM register offsets */
#define TM_QW0_USER		0x000 /* All rings */
#define TM_QW1_OS		0x010 /* Ring 0..2 */
#define TM_QW2_HV_POOL		0x020 /* Ring 0..1 */
#define TM_QW3_HV_PHYS		0x030 /* Ring 0..1 */

/* Byte offsets inside a QW             QW0 QW1 QW2 QW3 */
#define TM_NSR			0x0  /*  +   +   -   +  */
#define TM_CPPR			0x1  /*  -   +   -   +  */
#define TM_IPB			0x2  /*  -   +   +   +  */
#define TM_LSMFB		0x3  /*  -   +   +   +  */
#define TM_ACK_CNT		0x4  /*  -   +   -   -  */
#define TM_INC			0x5  /*  -   +   -   +  */
#define TM_AGE			0x6  /*  -   +   -   +  */
#define TM_PIPR			0x7  /*  -   +   -   +  */

#define TM_WORD0		0x0
#define TM_WORD1		0x4

/*
 * QW word 2 contains the valid bit at the top and other fields
 * depending on the QW.
 */
#define TM_WORD2		0x8
#define   TM_QW0W2_VU		PPC_BIT32(0)
#define   TM_QW0W2_LOGIC_SERV	PPC_BITMASK32(1,31) // XX 2,31 ?
#define   TM_QW1W2_VO		PPC_BIT32(0)
#define   TM_QW1W2_OS_CAM	PPC_BITMASK32(8,31)
#define   TM_QW2W2_VP		PPC_BIT32(0)
#define   TM_QW2W2_POOL_CAM	PPC_BITMASK32(8,31)
#define   TM_QW3W2_VT		PPC_BIT32(0)
#define   TM_QW3W2_LP		PPC_BIT32(6)
#define   TM_QW3W2_LE		PPC_BIT32(7)
#define   TM_QW3W2_T		PPC_BIT32(31)

/*
 * In addition to normal loads to "peek" and writes (only when invalid)
 * using 4 and 8 bytes accesses, the above registers support these
 * "special" byte operations:
 *
 *   - Byte load from QW0[NSR] - User level NSR (EBB)
 *   - Byte store to QW0[NSR] - User level NSR (EBB)
 *   - Byte load/store to QW1[CPPR] and QW3[CPPR] - CPPR access
 *   - Byte load from QW3[TM_WORD2] - Read VT||00000||LP||LE on thrd 0
 *                                    otherwise VT||0000000
 *   - Byte store to QW3[TM_WORD2] - Set VT bit (and LP/LE if present)
 *
 * Then we have all these "special" CI ops at these offset that trigger
 * all sorts of side effects:
 */
#define TM_SPC_ACK_EBB		0x800	/* Load8 ack EBB to reg*/
#define TM_SPC_ACK_OS_REG	0x810	/* Load16 ack OS irq to reg */
#define TM_SPC_PUSH_USR_CTX	0x808	/* Store32 Push/Validate user context */
#define TM_SPC_PULL_USR_CTX	0x808	/* Load32 Pull/Invalidate user context */
#define TM_SPC_SET_OS_PENDING	0x812	/* Store8 Set OS irq pending bit */
#define TM_SPC_PULL_OS_CTX	0x818	/* Load32/Load64 Pull/Invalidate OS context to reg */
#define TM_SPC_PULL_POOL_CTX	0x828	/* Load32/Load64 Pull/Invalidate Pool context to reg*/
#define TM_SPC_ACK_HV_REG	0x830	/* Load16 ack HV irq to reg */
#define TM_SPC_PULL_USR_CTX_OL	0xc08	/* Store8 Pull/Inval usr ctx to odd line */
#define TM_SPC_ACK_OS_EL	0xc10	/* Store8 ack OS irq to even line */
#define TM_SPC_ACK_HV_POOL_EL	0xc20	/* Store8 ack HV evt pool to even line */
#define TM_SPC_ACK_HV_EL	0xc30	/* Store8 ack HV irq to even line */
/* XXX more... */

/* NSR fields for the various QW ack types */
#define TM_QW0_NSR_EB		PPC_BIT8(0)
#define TM_QW1_NSR_EO		PPC_BIT8(0)
#define TM_QW3_NSR_HE		PPC_BITMASK8(0,1)
#define  TM_QW3_NSR_HE_NONE	0
#define  TM_QW3_NSR_HE_POOL	1
#define  TM_QW3_NSR_HE_PHYS	2
#define  TM_QW3_NSR_HE_LSI	3
#define TM_QW3_NSR_I		PPC_BIT8(2)
#define TM_QW3_NSR_GRP_LVL	PPC_BIT8(3,7)

#endif /* _ASM_POWERPC_XIVE_REGS_H */
