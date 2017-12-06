/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * Adjunct processor bus inline assemblies.
 */

#ifndef _AP_ASM_H_
#define _AP_ASM_H_

#include <asm/isc.h>

/**
 * ap_intructions_available() - Test if AP instructions are available.
 *
 * Returns 0 if the AP instructions are installed.
 */
static inline int ap_instructions_available(void)
{
	register unsigned long reg0 asm ("0") = AP_MKQID(0, 0);
	register unsigned long reg1 asm ("1") = -ENODEV;
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(
		"   .long 0xb2af0000\n"		/* PQAP(TAPQ) */
		"0: la    %1,0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (reg0), "+d" (reg1), "+d" (reg2) : : "cc");
	return reg1;
}

/**
 * ap_tapq(): Test adjunct processor queue.
 * @qid: The AP queue number
 * @info: Pointer to queue descriptor
 *
 * Returns AP queue status structure.
 */
static inline struct ap_queue_status ap_tapq(ap_qid_t qid, unsigned long *info)
{
	register unsigned long reg0 asm ("0") = qid;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(".long 0xb2af0000"		/* PQAP(TAPQ) */
		     : "+d" (reg0), "=d" (reg1), "+d" (reg2) : : "cc");
	if (info)
		*info = reg2;
	return reg1;
}

/**
 * ap_pqap_rapq(): Reset adjunct processor queue.
 * @qid: The AP queue number
 *
 * Returns AP queue status structure.
 */
static inline struct ap_queue_status ap_rapq(ap_qid_t qid)
{
	register unsigned long reg0 asm ("0") = qid | 0x01000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(
		".long 0xb2af0000"		/* PQAP(RAPQ) */
		: "+d" (reg0), "=d" (reg1), "+d" (reg2) : : "cc");
	return reg1;
}

/**
 * ap_aqic(): Control interruption for a specific AP.
 * @qid: The AP queue number
 * @qirqctrl: struct ap_qirq_ctrl (64 bit value)
 * @ind: The notification indicator byte
 *
 * Returns AP queue status.
 */
static inline struct ap_queue_status ap_aqic(ap_qid_t qid,
					     struct ap_qirq_ctrl qirqctrl,
					     void *ind)
{
	register unsigned long reg0 asm ("0") = qid | (3UL << 24);
	register struct ap_qirq_ctrl reg1_in asm ("1") = qirqctrl;
	register struct ap_queue_status reg1_out asm ("1");
	register void *reg2 asm ("2") = ind;

	asm volatile(
		".long 0xb2af0000"		/* PQAP(AQIC) */
		: "+d" (reg0), "+d" (reg1_in), "=d" (reg1_out), "+d" (reg2)
		:
		: "cc");
	return reg1_out;
}

/**
 * ap_qci(): Get AP configuration data
 *
 * Returns 0 on success, or -EOPNOTSUPP.
 */
static inline int ap_qci(void *config)
{
	register unsigned long reg0 asm ("0") = 0x04000000UL;
	register unsigned long reg1 asm ("1") = -EINVAL;
	register void *reg2 asm ("2") = (void *) config;

	asm volatile(
		".long 0xb2af0000\n"		/* PQAP(QCI) */
		"0: la    %1,0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (reg0), "+d" (reg1), "+d" (reg2)
		:
		: "cc", "memory");

	return reg1;
}

/**
 * ap_nqap(): Send message to adjunct processor queue.
 * @qid: The AP queue number
 * @psmid: The program supplied message identifier
 * @msg: The message text
 * @length: The message length
 *
 * Returns AP queue status structure.
 * Condition code 1 on NQAP can't happen because the L bit is 1.
 * Condition code 2 on NQAP also means the send is incomplete,
 * because a segment boundary was reached. The NQAP is repeated.
 */
static inline struct ap_queue_status ap_nqap(ap_qid_t qid,
					     unsigned long long psmid,
					     void *msg, size_t length)
{
	register unsigned long reg0 asm ("0") = qid | 0x40000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = (unsigned long) msg;
	register unsigned long reg3 asm ("3") = (unsigned long) length;
	register unsigned long reg4 asm ("4") = (unsigned int) (psmid >> 32);
	register unsigned long reg5 asm ("5") = psmid & 0xffffffff;

	asm volatile (
		"0: .long 0xb2ad0042\n"		/* NQAP */
		"   brc   2,0b"
		: "+d" (reg0), "=d" (reg1), "+d" (reg2), "+d" (reg3)
		: "d" (reg4), "d" (reg5)
		: "cc", "memory");
	return reg1;
}

/**
 * ap_dqap(): Receive message from adjunct processor queue.
 * @qid: The AP queue number
 * @psmid: Pointer to program supplied message identifier
 * @msg: The message text
 * @length: The message length
 *
 * Returns AP queue status structure.
 * Condition code 1 on DQAP means the receive has taken place
 * but only partially.	The response is incomplete, hence the
 * DQAP is repeated.
 * Condition code 2 on DQAP also means the receive is incomplete,
 * this time because a segment boundary was reached. Again, the
 * DQAP is repeated.
 * Note that gpr2 is used by the DQAP instruction to keep track of
 * any 'residual' length, in case the instruction gets interrupted.
 * Hence it gets zeroed before the instruction.
 */
static inline struct ap_queue_status ap_dqap(ap_qid_t qid,
					     unsigned long long *psmid,
					     void *msg, size_t length)
{
	register unsigned long reg0 asm("0") = qid | 0x80000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm("2") = 0UL;
	register unsigned long reg4 asm("4") = (unsigned long) msg;
	register unsigned long reg5 asm("5") = (unsigned long) length;
	register unsigned long reg6 asm("6") = 0UL;
	register unsigned long reg7 asm("7") = 0UL;


	asm volatile(
		"0: .long 0xb2ae0064\n"		/* DQAP */
		"   brc   6,0b\n"
		: "+d" (reg0), "=d" (reg1), "+d" (reg2),
		  "+d" (reg4), "+d" (reg5), "+d" (reg6), "+d" (reg7)
		: : "cc", "memory");
	*psmid = (((unsigned long long) reg6) << 32) + reg7;
	return reg1;
}

#endif /* _AP_ASM_H_ */
