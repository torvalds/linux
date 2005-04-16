/* soc.h: Definitions for Sparc SUNW,soc Fibre Channel Sbus driver.
 *
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef __SOC_H
#define __SOC_H

#include "fc.h"
#include "fcp.h"
#include "fcp_impl.h"

/* Hardware register offsets and constants first {{{ */
#define CFG	0x00UL		/* Config Register */
#define SAE	0x04UL		/* Slave Access Error Register */
#define CMD	0x08UL		/* Command and Status Register */
#define IMASK	0x0cUL		/* Interrupt Mask Register */

/* Config Register */
#define SOC_CFG_EXT_RAM_BANK_MASK	0x07000000
#define SOC_CFG_EEPROM_BANK_MASK	0x00030000
#define SOC_CFG_BURST64_MASK		0x00000700
#define SOC_CFG_SBUS_PARITY_TEST	0x00000020
#define SOC_CFG_SBUS_PARITY_CHECK	0x00000010
#define SOC_CFG_SBUS_ENHANCED		0x00000008
#define SOC_CFG_BURST_MASK		0x00000007
/* Bursts */
#define SOC_CFG_BURST_4			0x00000000
#define SOC_CFG_BURST_16		0x00000004
#define SOC_CFG_BURST_32		0x00000005
#define SOC_CFG_BURST_64		0x00000006

/* Slave Access Error Register */
#define SOC_SAE_ALIGNMENT		0x00000004
#define SOC_SAE_UNSUPPORTED		0x00000002
#define SOC_SAE_PARITY			0x00000001

/* Command & Status Register */
#define SOC_CMD_RSP_QALL		0x000f0000
#define SOC_CMD_RSP_Q0			0x00010000
#define SOC_CMD_RSP_Q1			0x00020000
#define SOC_CMD_RSP_Q2			0x00040000
#define SOC_CMD_RSP_Q3			0x00080000
#define SOC_CMD_REQ_QALL		0x00000f00
#define SOC_CMD_REQ_Q0			0x00000100
#define SOC_CMD_REQ_Q1			0x00000200
#define SOC_CMD_REQ_Q2			0x00000400
#define SOC_CMD_REQ_Q3			0x00000800
#define SOC_CMD_SAE			0x00000080
#define SOC_CMD_INTR_PENDING		0x00000008
#define SOC_CMD_NON_QUEUED		0x00000004
#define SOC_CMD_IDLE			0x00000002
#define SOC_CMD_SOFT_RESET		0x00000001

/* Interrupt Mask Register */
#define SOC_IMASK_RSP_QALL		0x000f0000
#define SOC_IMASK_RSP_Q0		0x00010000
#define SOC_IMASK_RSP_Q1		0x00020000
#define SOC_IMASK_RSP_Q2		0x00040000
#define SOC_IMASK_RSP_Q3		0x00080000
#define SOC_IMASK_REQ_QALL		0x00000f00
#define SOC_IMASK_REQ_Q0		0x00000100
#define SOC_IMASK_REQ_Q1		0x00000200
#define SOC_IMASK_REQ_Q2		0x00000400
#define SOC_IMASK_REQ_Q3		0x00000800
#define SOC_IMASK_SAE			0x00000080
#define SOC_IMASK_NON_QUEUED		0x00000004

#define SOC_INTR(s, cmd) \
	(((cmd & SOC_CMD_RSP_QALL) | ((~cmd) & SOC_CMD_REQ_QALL)) \
	 & s->imask)
	 
#define SOC_SETIMASK(s, i) \
do {	(s)->imask = (i); \
	sbus_writel((i), (s)->regs + IMASK); \
} while(0)

/* XRAM
 *
 * This is a 64KB register area. It accepts only halfword access.
 * That's why here are the following inline functions...
 */
 
typedef void __iomem *xram_p;

/* Get 32bit number from XRAM */
static inline u32 xram_get_32(xram_p x)
{
	return ((sbus_readw(x + 0x00UL) << 16) |
		(sbus_readw(x + 0x02UL)));
}

/* Like the above, but when we don't care about the high 16 bits */
static inline u32 xram_get_32low(xram_p x)
{
	return (u32) sbus_readw(x + 0x02UL);
}

static inline u16 xram_get_16(xram_p x)
{
	return sbus_readw(x);
}

static inline u8 xram_get_8(xram_p x)
{
	if ((unsigned long)x & 0x1UL) {
		x = x - 1;
		return (u8) sbus_readw(x);
	} else {
		return (u8) (sbus_readw(x) >> 8);
	}
}

static inline void xram_copy_from(void *p, xram_p x, int len)
{
	for (len >>= 2; len > 0; len--, x += sizeof(u32)) {
		u32 val, *p32 = p;

		val = ((sbus_readw(x + 0x00UL) << 16) |
		       (sbus_readw(x + 0x02UL)));
		*p32++ = val;
		p = p32;
	}
}

static inline void xram_copy_to(xram_p x, void *p, int len)
{
	for (len >>= 2; len > 0; len--, x += sizeof(u32)) {
		u32 tmp, *p32 = p;

		tmp = *p32++;
		p = p32;
		sbus_writew(tmp >> 16, x + 0x00UL);
		sbus_writew(tmp, x + 0x02UL);
	}
}

static inline void xram_bzero(xram_p x, int len)
{
	for (len >>= 1; len > 0; len--, x += sizeof(u16))
		sbus_writew(0, x);
}

/* Circular Queue */

#define SOC_CQ_REQ_OFFSET	(0x100 * sizeof(u16))
#define SOC_CQ_RSP_OFFSET	(0x110 * sizeof(u16))

typedef struct {
	u32			address;
	u8			in;
	u8			out;
	u8			last;
	u8			seqno;
} soc_hw_cq;

#define SOC_PORT_A	0x0000	/* From/To Port A */
#define SOC_PORT_B	0x0001	/* From/To Port A */
#define SOC_FC_HDR	0x0002  /* Contains FC Header */
#define SOC_NORSP	0x0004  /* Don't generate response nor interrupt */
#define SOC_NOINT	0x0008  /* Generate response but not interrupt */
#define SOC_XFERRDY	0x0010  /* Generate XFERRDY */
#define SOC_IGNOREPARAM	0x0020	/* Ignore PARAM field in the FC header */
#define SOC_COMPLETE	0x0040  /* Command completed */
#define SOC_UNSOLICITED	0x0080	/* For request this is the packet to establish unsolicited pools, */
				/* for rsp this is unsolicited packet */
#define SOC_STATUS	0x0100	/* State change (on/off line) */

typedef struct {
	u32			token;
	u16			flags;
	u8			class;
	u8			segcnt;
	u32			bytecnt;
} soc_hdr;

typedef struct {
	u32			base;
	u32			count;
} soc_data;

#define SOC_CQTYPE_OUTBOUND	0x01
#define SOC_CQTYPE_INBOUND	0x02
#define SOC_CQTYPE_SIMPLE	0x03
#define SOC_CQTYPE_IO_WRITE	0x04
#define SOC_CQTYPE_IO_READ	0x05
#define SOC_CQTYPE_UNSOLICITED	0x06
#define SOC_CQTYPE_DIAG		0x07
#define SOC_CQTYPE_OFFLINE	0x08
#define SOC_CQTYPE_RESPONSE	0x10
#define SOC_CQTYPE_INLINE	0x20

#define SOC_CQFLAGS_CONT	0x01
#define SOC_CQFLAGS_FULL	0x02
#define SOC_CQFLAGS_BADHDR	0x04
#define SOC_CQFLAGS_BADPKT	0x08

typedef struct {
	soc_hdr			shdr;
	soc_data		data[3];
	fc_hdr			fchdr;
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} soc_req;

#define SOC_OK			0
#define SOC_P_RJT		2
#define SOC_F_RJT		3
#define SOC_P_BSY		4
#define SOC_F_BSY		5
#define SOC_ONLINE		0x10
#define SOC_OFFLINE		0x11
#define SOC_TIMEOUT		0x12
#define SOC_OVERRUN		0x13
#define SOC_UNKOWN_CQ_TYPE	0x20
#define SOC_BAD_SEG_CNT		0x21
#define SOC_MAX_XCHG_EXCEEDED	0x22
#define SOC_BAD_XID		0x23
#define SOC_XCHG_BUSY		0x24
#define SOC_BAD_POOL_ID		0x25
#define SOC_INSUFFICIENT_CQES	0x26
#define SOC_ALLOC_FAIL		0x27
#define SOC_BAD_SID		0x28
#define SOC_NO_SEG_INIT		0x29

typedef struct {
	soc_hdr			shdr;
	u32			status;
	soc_data		data;
	u8			xxx1[12];
	fc_hdr			fchdr;
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} soc_rsp;

/* }}} */

/* Now our software structures and constants we use to drive the beast {{{ */

#define SOC_CQ_REQ0_SIZE	4
#define SOC_CQ_REQ1_SIZE	64
#define SOC_CQ_RSP0_SIZE	8
#define SOC_CQ_RSP1_SIZE	4

#define SOC_SOLICITED_RSP_Q	0
#define SOC_UNSOLICITED_RSP_Q	1

struct soc;

typedef struct {
	/* This must come first */
	fc_channel		fc;
	struct soc		*s;
	u16			flags;
	u16			mask;
} soc_port; 

typedef struct {
	soc_hw_cq		__iomem *hw_cq;	/* Related XRAM cq */
	soc_req			__iomem *pool;
	u8			in;
	u8			out;
	u8			last;
	u8			seqno;
} soc_cq_rsp;

typedef struct {
	soc_hw_cq		__iomem *hw_cq;	/* Related XRAM cq */
	soc_req			*pool;
	u8			in;
	u8			out;
	u8			last;
	u8			seqno;
} soc_cq_req;

struct soc {
	spinlock_t		lock;
	soc_port		port[2]; /* Every SOC has one or two FC ports */
	soc_cq_req		req[2]; /* Request CQs */
	soc_cq_rsp		rsp[2]; /* Response CQs */
	int			soc_no;
	void __iomem		*regs;
	xram_p			xram;
	fc_wwn			wwn;
	u32			imask;	/* Our copy of regs->imask */
	u32			cfg;	/* Our copy of regs->cfg */
	char			serv_params[80];
	struct soc		*next;
	int			curr_port; /* Which port will have priority to fcp_queue_empty */

	soc_req			*req_cpu;
	u32			req_dvma;
};

/* }}} */

#endif /* !(__SOC_H) */
