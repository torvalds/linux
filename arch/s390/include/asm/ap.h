/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Adjunct processor (AP) interfaces
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Tony Krowiak <akrowia@linux.vnet.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Harald Freudenberger <freude@de.ibm.com>
 */

#ifndef _ASM_S390_AP_H_
#define _ASM_S390_AP_H_

/**
 * The ap_qid_t identifier of an ap queue.
 * If the AP facilities test (APFT) facility is available,
 * card and queue index are 8 bit values, otherwise
 * card index is 6 bit and queue index a 4 bit value.
 */
typedef unsigned int ap_qid_t;

#define AP_MKQID(_card, _queue) (((_card) & 0xff) << 8 | ((_queue) & 0xff))
#define AP_QID_CARD(_qid) (((_qid) >> 8) & 0xff)
#define AP_QID_QUEUE(_qid) ((_qid) & 0xff)

/**
 * struct ap_queue_status - Holds the AP queue status.
 * @queue_empty: Shows if queue is empty
 * @replies_waiting: Waiting replies
 * @queue_full: Is 1 if the queue is full
 * @irq_enabled: Shows if interrupts are enabled for the AP
 * @response_code: Holds the 8 bit response code
 *
 * The ap queue status word is returned by all three AP functions
 * (PQAP, NQAP and DQAP).  There's a set of flags in the first
 * byte, followed by a 1 byte response code.
 */
struct ap_queue_status {
	unsigned int queue_empty	: 1;
	unsigned int replies_waiting	: 1;
	unsigned int queue_full		: 1;
	unsigned int _pad1		: 4;
	unsigned int irq_enabled	: 1;
	unsigned int response_code	: 8;
	unsigned int _pad2		: 16;
};

/**
 * ap_intructions_available() - Test if AP instructions are available.
 *
 * Returns 1 if the AP instructions are installed, otherwise 0.
 */
static inline int ap_instructions_available(void)
{
	register unsigned long reg0 asm ("0") = AP_MKQID(0, 0);
	register unsigned long reg1 asm ("1") = 0;
	register unsigned long reg2 asm ("2") = 0;

	asm volatile(
		"   .long 0xb2af0000\n"		/* PQAP(TAPQ) */
		"0: la    %0,1\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (reg1), "+d" (reg2)
		: "d" (reg0)
		: "cc");
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
	register unsigned long reg2 asm ("2");

	asm volatile(".long 0xb2af0000"		/* PQAP(TAPQ) */
		     : "=d" (reg1), "=d" (reg2)
		     : "d" (reg0)
		     : "cc");
	if (info)
		*info = reg2;
	return reg1;
}

/**
 * ap_test_queue(): Test adjunct processor queue.
 * @qid: The AP queue number
 * @tbit: Test facilities bit
 * @info: Pointer to queue descriptor
 *
 * Returns AP queue status structure.
 */
static inline struct ap_queue_status ap_test_queue(ap_qid_t qid,
						   int tbit,
						   unsigned long *info)
{
	if (tbit)
		qid |= 1UL << 23; /* set T bit*/
	return ap_tapq(qid, info);
}

/**
 * ap_pqap_rapq(): Reset adjunct processor queue.
 * @qid: The AP queue number
 *
 * Returns AP queue status structure.
 */
static inline struct ap_queue_status ap_rapq(ap_qid_t qid)
{
	register unsigned long reg0 asm ("0") = qid | (1UL << 24);
	register struct ap_queue_status reg1 asm ("1");

	asm volatile(
		".long 0xb2af0000"		/* PQAP(RAPQ) */
		: "=d" (reg1)
		: "d" (reg0)
		: "cc");
	return reg1;
}

/**
 * ap_pqap_zapq(): Reset and zeroize adjunct processor queue.
 * @qid: The AP queue number
 *
 * Returns AP queue status structure.
 */
static inline struct ap_queue_status ap_zapq(ap_qid_t qid)
{
	register unsigned long reg0 asm ("0") = qid | (2UL << 24);
	register struct ap_queue_status reg1 asm ("1");

	asm volatile(
		".long 0xb2af0000"		/* PQAP(ZAPQ) */
		: "=d" (reg1)
		: "d" (reg0)
		: "cc");
	return reg1;
}

/**
 * struct ap_config_info - convenience struct for AP crypto
 * config info as returned by the ap_qci() function.
 */
struct ap_config_info {
	unsigned int apsc	 : 1;	/* S bit */
	unsigned int apxa	 : 1;	/* N bit */
	unsigned int qact	 : 1;	/* C bit */
	unsigned int rc8a	 : 1;	/* R bit */
	unsigned char _reserved1 : 4;
	unsigned char _reserved2[3];
	unsigned char Na;		/* max # of APs - 1 */
	unsigned char Nd;		/* max # of Domains - 1 */
	unsigned char _reserved3[10];
	unsigned int apm[8];		/* AP ID mask */
	unsigned int aqm[8];		/* AP queue mask */
	unsigned int adm[8];		/* AP domain mask */
	unsigned char _reserved4[16];
} __aligned(8);

/**
 * ap_qci(): Get AP configuration data
 *
 * Returns 0 on success, or -EOPNOTSUPP.
 */
static inline int ap_qci(struct ap_config_info *config)
{
	register unsigned long reg0 asm ("0") = 4UL << 24;
	register unsigned long reg1 asm ("1") = -EOPNOTSUPP;
	register struct ap_config_info *reg2 asm ("2") = config;

	asm volatile(
		".long 0xb2af0000\n"		/* PQAP(QCI) */
		"0: la    %0,0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (reg1)
		: "d" (reg0), "d" (reg2)
		: "cc", "memory");

	return reg1;
}

/*
 * struct ap_qirq_ctrl - convenient struct for easy invocation
 * of the ap_aqic() function. This struct is passed as GR1
 * parameter to the PQAP(AQIC) instruction. For details please
 * see the AR documentation.
 */
struct ap_qirq_ctrl {
	unsigned int _res1 : 8;
	unsigned int zone  : 8;	/* zone info */
	unsigned int ir    : 1;	/* ir flag: enable (1) or disable (0) irq */
	unsigned int _res2 : 4;
	unsigned int gisc  : 3;	/* guest isc field */
	unsigned int _res3 : 6;
	unsigned int gf    : 2;	/* gisa format */
	unsigned int _res4 : 1;
	unsigned int gisa  : 27;	/* gisa origin */
	unsigned int _res5 : 1;
	unsigned int isc   : 3;	/* irq sub class */
};

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
		: "=d" (reg1_out)
		: "d" (reg0), "d" (reg1_in), "d" (reg2)
		: "cc");
	return reg1_out;
}

/*
 * union ap_qact_ap_info - used together with the
 * ap_aqic() function to provide a convenient way
 * to handle the ap info needed by the qact function.
 */
union ap_qact_ap_info {
	unsigned long val;
	struct {
		unsigned int	  : 3;
		unsigned int mode : 3;
		unsigned int	  : 26;
		unsigned int cat  : 8;
		unsigned int	  : 8;
		unsigned char ver[2];
	};
};

/**
 * ap_qact(): Query AP combatibility type.
 * @qid: The AP queue number
 * @apinfo: On input the info about the AP queue. On output the
 *	    alternate AP queue info provided by the qact function
 *	    in GR2 is stored in.
 *
 * Returns AP queue status. Check response_code field for failures.
 */
static inline struct ap_queue_status ap_qact(ap_qid_t qid, int ifbit,
					     union ap_qact_ap_info *apinfo)
{
	register unsigned long reg0 asm ("0") = qid | (5UL << 24)
		| ((ifbit & 0x01) << 22);
	register unsigned long reg1_in asm ("1") = apinfo->val;
	register struct ap_queue_status reg1_out asm ("1");
	register unsigned long reg2 asm ("2");

	asm volatile(
		".long 0xb2af0000"		/* PQAP(QACT) */
		: "+d" (reg1_in), "=d" (reg1_out), "=d" (reg2)
		: "d" (reg0)
		: "cc");
	apinfo->val = reg2;
	return reg1_out;
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

#endif /* _ASM_S390_AP_H_ */
