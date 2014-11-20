/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NLM_FMN_H_
#define _NLM_FMN_H_

#include <asm/netlogic/mips-extns.h> /* for COP2 access */

/* Station IDs */
#define FMN_STNID_CPU0			0x00
#define FMN_STNID_CPU1			0x08
#define FMN_STNID_CPU2			0x10
#define FMN_STNID_CPU3			0x18
#define FMN_STNID_CPU4			0x20
#define FMN_STNID_CPU5			0x28
#define FMN_STNID_CPU6			0x30
#define FMN_STNID_CPU7			0x38

#define FMN_STNID_XGS0_TX		64
#define FMN_STNID_XMAC0_00_TX		64
#define FMN_STNID_XMAC0_01_TX		65
#define FMN_STNID_XMAC0_02_TX		66
#define FMN_STNID_XMAC0_03_TX		67
#define FMN_STNID_XMAC0_04_TX		68
#define FMN_STNID_XMAC0_05_TX		69
#define FMN_STNID_XMAC0_06_TX		70
#define FMN_STNID_XMAC0_07_TX		71
#define FMN_STNID_XMAC0_08_TX		72
#define FMN_STNID_XMAC0_09_TX		73
#define FMN_STNID_XMAC0_10_TX		74
#define FMN_STNID_XMAC0_11_TX		75
#define FMN_STNID_XMAC0_12_TX		76
#define FMN_STNID_XMAC0_13_TX		77
#define FMN_STNID_XMAC0_14_TX		78
#define FMN_STNID_XMAC0_15_TX		79

#define FMN_STNID_XGS1_TX		80
#define FMN_STNID_XMAC1_00_TX		80
#define FMN_STNID_XMAC1_01_TX		81
#define FMN_STNID_XMAC1_02_TX		82
#define FMN_STNID_XMAC1_03_TX		83
#define FMN_STNID_XMAC1_04_TX		84
#define FMN_STNID_XMAC1_05_TX		85
#define FMN_STNID_XMAC1_06_TX		86
#define FMN_STNID_XMAC1_07_TX		87
#define FMN_STNID_XMAC1_08_TX		88
#define FMN_STNID_XMAC1_09_TX		89
#define FMN_STNID_XMAC1_10_TX		90
#define FMN_STNID_XMAC1_11_TX		91
#define FMN_STNID_XMAC1_12_TX		92
#define FMN_STNID_XMAC1_13_TX		93
#define FMN_STNID_XMAC1_14_TX		94
#define FMN_STNID_XMAC1_15_TX		95

#define FMN_STNID_GMAC			96
#define FMN_STNID_GMACJFR_0		96
#define FMN_STNID_GMACRFR_0		97
#define FMN_STNID_GMACTX0		98
#define FMN_STNID_GMACTX1		99
#define FMN_STNID_GMACTX2		100
#define FMN_STNID_GMACTX3		101
#define FMN_STNID_GMACJFR_1		102
#define FMN_STNID_GMACRFR_1		103

#define FMN_STNID_DMA			104
#define FMN_STNID_DMA_0			104
#define FMN_STNID_DMA_1			105
#define FMN_STNID_DMA_2			106
#define FMN_STNID_DMA_3			107

#define FMN_STNID_XGS0FR		112
#define FMN_STNID_XMAC0JFR		112
#define FMN_STNID_XMAC0RFR		113

#define FMN_STNID_XGS1FR		114
#define FMN_STNID_XMAC1JFR		114
#define FMN_STNID_XMAC1RFR		115
#define FMN_STNID_SEC			120
#define FMN_STNID_SEC0			120
#define FMN_STNID_SEC1			121
#define FMN_STNID_SEC2			122
#define FMN_STNID_SEC3			123
#define FMN_STNID_PK0			124
#define FMN_STNID_SEC_RSA		124
#define FMN_STNID_SEC_RSVD0		125
#define FMN_STNID_SEC_RSVD1		126
#define FMN_STNID_SEC_RSVD2		127

#define FMN_STNID_GMAC1			80
#define FMN_STNID_GMAC1_FR_0		81
#define FMN_STNID_GMAC1_TX0		82
#define FMN_STNID_GMAC1_TX1		83
#define FMN_STNID_GMAC1_TX2		84
#define FMN_STNID_GMAC1_TX3		85
#define FMN_STNID_GMAC1_FR_1		87
#define FMN_STNID_GMAC0			96
#define FMN_STNID_GMAC0_FR_0		97
#define FMN_STNID_GMAC0_TX0		98
#define FMN_STNID_GMAC0_TX1		99
#define FMN_STNID_GMAC0_TX2		100
#define FMN_STNID_GMAC0_TX3		101
#define FMN_STNID_GMAC0_FR_1		103
#define FMN_STNID_CMP_0			108
#define FMN_STNID_CMP_1			109
#define FMN_STNID_CMP_2			110
#define FMN_STNID_CMP_3			111
#define FMN_STNID_PCIE_0		116
#define FMN_STNID_PCIE_1		117
#define FMN_STNID_PCIE_2		118
#define FMN_STNID_PCIE_3		119
#define FMN_STNID_XLS_PK0		121

#define nlm_read_c2_cc0(s)		__read_32bit_c2_register($16, s)
#define nlm_read_c2_cc1(s)		__read_32bit_c2_register($17, s)
#define nlm_read_c2_cc2(s)		__read_32bit_c2_register($18, s)
#define nlm_read_c2_cc3(s)		__read_32bit_c2_register($19, s)
#define nlm_read_c2_cc4(s)		__read_32bit_c2_register($20, s)
#define nlm_read_c2_cc5(s)		__read_32bit_c2_register($21, s)
#define nlm_read_c2_cc6(s)		__read_32bit_c2_register($22, s)
#define nlm_read_c2_cc7(s)		__read_32bit_c2_register($23, s)
#define nlm_read_c2_cc8(s)		__read_32bit_c2_register($24, s)
#define nlm_read_c2_cc9(s)		__read_32bit_c2_register($25, s)
#define nlm_read_c2_cc10(s)		__read_32bit_c2_register($26, s)
#define nlm_read_c2_cc11(s)		__read_32bit_c2_register($27, s)
#define nlm_read_c2_cc12(s)		__read_32bit_c2_register($28, s)
#define nlm_read_c2_cc13(s)		__read_32bit_c2_register($29, s)
#define nlm_read_c2_cc14(s)		__read_32bit_c2_register($30, s)
#define nlm_read_c2_cc15(s)		__read_32bit_c2_register($31, s)

#define nlm_write_c2_cc0(s, v)		__write_32bit_c2_register($16, s, v)
#define nlm_write_c2_cc1(s, v)		__write_32bit_c2_register($17, s, v)
#define nlm_write_c2_cc2(s, v)		__write_32bit_c2_register($18, s, v)
#define nlm_write_c2_cc3(s, v)		__write_32bit_c2_register($19, s, v)
#define nlm_write_c2_cc4(s, v)		__write_32bit_c2_register($20, s, v)
#define nlm_write_c2_cc5(s, v)		__write_32bit_c2_register($21, s, v)
#define nlm_write_c2_cc6(s, v)		__write_32bit_c2_register($22, s, v)
#define nlm_write_c2_cc7(s, v)		__write_32bit_c2_register($23, s, v)
#define nlm_write_c2_cc8(s, v)		__write_32bit_c2_register($24, s, v)
#define nlm_write_c2_cc9(s, v)		__write_32bit_c2_register($25, s, v)
#define nlm_write_c2_cc10(s, v)		__write_32bit_c2_register($26, s, v)
#define nlm_write_c2_cc11(s, v)		__write_32bit_c2_register($27, s, v)
#define nlm_write_c2_cc12(s, v)		__write_32bit_c2_register($28, s, v)
#define nlm_write_c2_cc13(s, v)		__write_32bit_c2_register($29, s, v)
#define nlm_write_c2_cc14(s, v)		__write_32bit_c2_register($30, s, v)
#define nlm_write_c2_cc15(s, v)		__write_32bit_c2_register($31, s, v)

#define nlm_read_c2_status0()		__read_32bit_c2_register($2, 0)
#define nlm_write_c2_status0(v)		__write_32bit_c2_register($2, 0, v)
#define nlm_read_c2_status1()		__read_32bit_c2_register($2, 1)
#define nlm_write_c2_status1(v)		__write_32bit_c2_register($2, 1, v)
#define nlm_read_c2_status(sel)		__read_32bit_c2_register($2, 0)
#define nlm_read_c2_config()		__read_32bit_c2_register($3, 0)
#define nlm_write_c2_config(v)		__write_32bit_c2_register($3, 0, v)
#define nlm_read_c2_bucksize(b)		__read_32bit_c2_register($4, b)
#define nlm_write_c2_bucksize(b, v)	__write_32bit_c2_register($4, b, v)

#define nlm_read_c2_rx_msg0()		__read_64bit_c2_register($1, 0)
#define nlm_read_c2_rx_msg1()		__read_64bit_c2_register($1, 1)
#define nlm_read_c2_rx_msg2()		__read_64bit_c2_register($1, 2)
#define nlm_read_c2_rx_msg3()		__read_64bit_c2_register($1, 3)

#define nlm_write_c2_tx_msg0(v)		__write_64bit_c2_register($0, 0, v)
#define nlm_write_c2_tx_msg1(v)		__write_64bit_c2_register($0, 1, v)
#define nlm_write_c2_tx_msg2(v)		__write_64bit_c2_register($0, 2, v)
#define nlm_write_c2_tx_msg3(v)		__write_64bit_c2_register($0, 3, v)

#define FMN_STN_RX_QSIZE		256
#define FMN_NSTATIONS			128
#define FMN_CORE_NBUCKETS		8

static inline void nlm_msgsnd(unsigned int stid)
{
	__asm__ volatile (
	    ".set	push\n"
	    ".set	noreorder\n"
	    ".set	noat\n"
	    "move	$1, %0\n"
	    "c2		0x10001\n"	/* msgsnd $1 */
	    ".set	pop\n"
	    : : "r" (stid) : "$1"
	);
}

static inline void nlm_msgld(unsigned int pri)
{
	__asm__ volatile (
	    ".set	push\n"
	    ".set	noreorder\n"
	    ".set	noat\n"
	    "move	$1, %0\n"
	    "c2		0x10002\n"    /* msgld $1 */
	    ".set	pop\n"
	    : : "r" (pri) : "$1"
	);
}

static inline void nlm_msgwait(unsigned int mask)
{
	__asm__ volatile (
	    ".set	push\n"
	    ".set	noreorder\n"
	    ".set	noat\n"
	    "move	$8, %0\n"
	    "c2		0x10003\n"    /* msgwait $1 */
	    ".set	pop\n"
	    : : "r" (mask) : "$1"
	);
}

/*
 * Disable interrupts and enable COP2 access
 */
static inline uint32_t nlm_cop2_enable_irqsave(void)
{
	uint32_t sr = read_c0_status();

	write_c0_status((sr & ~ST0_IE) | ST0_CU2);
	return sr;
}

static inline void nlm_cop2_disable_irqrestore(uint32_t sr)
{
	write_c0_status(sr);
}

static inline void nlm_fmn_setup_intr(int irq, unsigned int tmask)
{
	uint32_t config;

	config = (1 << 24)	/* interrupt water mark - 1 msg */
		| (irq << 16)	/* irq */
		| (tmask << 8)	/* thread mask */
		| 0x2;		/* enable watermark intr, disable empty intr */
	nlm_write_c2_config(config);
}

struct nlm_fmn_msg {
	uint64_t msg0;
	uint64_t msg1;
	uint64_t msg2;
	uint64_t msg3;
};

static inline int nlm_fmn_send(unsigned int size, unsigned int code,
		unsigned int stid, struct nlm_fmn_msg *msg)
{
	unsigned int dest;
	uint32_t status;
	int i;

	/*
	 * Make sure that all the writes pending at the cpu are flushed.
	 * Any writes pending on CPU will not be see by devices. L1/L2
	 * caches are coherent with IO, so no cache flush needed.
	 */
	__asm __volatile("sync");

	/* Load TX message buffers */
	nlm_write_c2_tx_msg0(msg->msg0);
	nlm_write_c2_tx_msg1(msg->msg1);
	nlm_write_c2_tx_msg2(msg->msg2);
	nlm_write_c2_tx_msg3(msg->msg3);
	dest = ((size - 1) << 16) | (code << 8) | stid;

	/*
	 * Retry a few times on credit fail, this should be a
	 * transient condition, unless there is a configuration
	 * failure, or the receiver is stuck.
	 */
	for (i = 0; i < 8; i++) {
		nlm_msgsnd(dest);
		status = nlm_read_c2_status0();
		if ((status & 0x2) == 1)
			pr_info("Send pending fail!\n");
		if ((status & 0x4) == 0)
			return 0;
	}

	/* If there is a credit failure, return error */
	return status & 0x06;
}

static inline int nlm_fmn_receive(int bucket, int *size, int *code, int *stid,
		struct nlm_fmn_msg *msg)
{
	uint32_t status, tmp;

	nlm_msgld(bucket);

	/* wait for load pending to clear */
	do {
		status = nlm_read_c2_status0();
	} while ((status & 0x08) != 0);

	/* receive error bits */
	tmp = status & 0x30;
	if (tmp != 0)
		return tmp;

	*size = ((status & 0xc0) >> 6) + 1;
	*code = (status & 0xff00) >> 8;
	*stid = (status & 0x7f0000) >> 16;
	msg->msg0 = nlm_read_c2_rx_msg0();
	msg->msg1 = nlm_read_c2_rx_msg1();
	msg->msg2 = nlm_read_c2_rx_msg2();
	msg->msg3 = nlm_read_c2_rx_msg3();

	return 0;
}

struct xlr_fmn_info {
	int num_buckets;
	int start_stn_id;
	int end_stn_id;
	int credit_config[128];
};

struct xlr_board_fmn_config {
	int bucket_size[128];		/* size of buckets for all stations */
	struct xlr_fmn_info cpu[8];
	struct xlr_fmn_info gmac[2];
	struct xlr_fmn_info dma;
	struct xlr_fmn_info cmp;
	struct xlr_fmn_info sae;
	struct xlr_fmn_info xgmac[2];
};

extern int nlm_register_fmn_handler(int start, int end,
	void (*fn)(int, int, int, int, struct nlm_fmn_msg *, void *),
	void *arg);
extern void xlr_percpu_fmn_init(void);
extern void nlm_setup_fmn_irq(void);
extern void xlr_board_info_setup(void);

extern struct xlr_board_fmn_config xlr_board_fmn_config;
#endif
