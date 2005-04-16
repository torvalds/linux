/* socal.h: Definitions for Sparc SUNW,socal (SOC+) Fibre Channel Sbus driver.
 *
 * Copyright (C) 1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#ifndef __SOCAL_H
#define __SOCAL_H

#include "fc.h"
#include "fcp.h"
#include "fcp_impl.h"

/* Hardware register offsets and constants first {{{ */
#define CFG	0x00UL
#define SAE	0x04UL
#define CMD	0x08UL
#define IMASK	0x0cUL
#define REQP	0x10UL
#define RESP	0x14UL

/* Config Register */
#define SOCAL_CFG_EXT_RAM_BANK_MASK	0x07000000
#define SOCAL_CFG_EEPROM_BANK_MASK	0x00030000
#define SOCAL_CFG_BURST64_MASK		0x00000700
#define SOCAL_CFG_SBUS_PARITY_TEST	0x00000020
#define SOCAL_CFG_SBUS_PARITY_CHECK	0x00000010
#define SOCAL_CFG_SBUS_ENHANCED		0x00000008
#define SOCAL_CFG_BURST_MASK		0x00000007
/* Bursts */
#define SOCAL_CFG_BURST_4		0x00000000
#define SOCAL_CFG_BURST_8		0x00000003
#define SOCAL_CFG_BURST_16		0x00000004
#define SOCAL_CFG_BURST_32		0x00000005
#define SOCAL_CFG_BURST_64		0x00000006
#define SOCAL_CFG_BURST_128		0x00000007

/* Slave Access Error Register */
#define SOCAL_SAE_ALIGNMENT		0x00000004
#define SOCAL_SAE_UNSUPPORTED		0x00000002
#define SOCAL_SAE_PARITY		0x00000001

/* Command & Status Register */
#define SOCAL_CMD_RSP_QALL		0x000f0000
#define SOCAL_CMD_RSP_Q0		0x00010000
#define SOCAL_CMD_RSP_Q1		0x00020000
#define SOCAL_CMD_RSP_Q2		0x00040000
#define SOCAL_CMD_RSP_Q3		0x00080000
#define SOCAL_CMD_REQ_QALL		0x00000f00
#define SOCAL_CMD_REQ_Q0		0x00000100
#define SOCAL_CMD_REQ_Q1		0x00000200
#define SOCAL_CMD_REQ_Q2		0x00000400
#define SOCAL_CMD_REQ_Q3		0x00000800
#define SOCAL_CMD_SAE			0x00000080
#define SOCAL_CMD_INTR_PENDING		0x00000008
#define SOCAL_CMD_NON_QUEUED		0x00000004
#define SOCAL_CMD_IDLE			0x00000002
#define SOCAL_CMD_SOFT_RESET		0x00000001

/* Interrupt Mask Register */
#define SOCAL_IMASK_RSP_QALL		0x000f0000
#define SOCAL_IMASK_RSP_Q0		0x00010000
#define SOCAL_IMASK_RSP_Q1		0x00020000
#define SOCAL_IMASK_RSP_Q2		0x00040000
#define SOCAL_IMASK_RSP_Q3		0x00080000
#define SOCAL_IMASK_REQ_QALL		0x00000f00
#define SOCAL_IMASK_REQ_Q0		0x00000100
#define SOCAL_IMASK_REQ_Q1		0x00000200
#define SOCAL_IMASK_REQ_Q2		0x00000400
#define SOCAL_IMASK_REQ_Q3		0x00000800
#define SOCAL_IMASK_SAE			0x00000080
#define SOCAL_IMASK_NON_QUEUED		0x00000004

#define SOCAL_INTR(s, cmd) \
	(((cmd & SOCAL_CMD_RSP_QALL) | ((~cmd) & SOCAL_CMD_REQ_QALL)) \
	 & s->imask)
	 
#define SOCAL_SETIMASK(s, i) \
do {	(s)->imask = (i); \
	sbus_writel((i), (s)->regs + IMASK); \
} while (0)
	
#define SOCAL_MAX_EXCHANGES		1024

/* XRAM
 *
 * This is a 64KB register area.
 * From the documentation, it seems like it is finally able to cope
 * at least with 1,2,4 byte accesses for read and 2,4 byte accesses for write.
 */
 
/* Circular Queue */

#define SOCAL_CQ_REQ_OFFSET	0x200
#define SOCAL_CQ_RSP_OFFSET	0x220

typedef struct {
	u32			address;
	u8			in;
	u8			out;
	u8			last;
	u8			seqno;
} socal_hw_cq;

#define SOCAL_PORT_A	0x0000	/* From/To Port A */
#define SOCAL_PORT_B	0x0001	/* From/To Port A */
#define SOCAL_FC_HDR	0x0002  /* Contains FC Header */
#define SOCAL_NORSP	0x0004  /* Don't generate response nor interrupt */
#define SOCAL_NOINT	0x0008  /* Generate response but not interrupt */
#define SOCAL_XFERRDY	0x0010  /* Generate XFERRDY */
#define SOCAL_IGNOREPARAM 0x0020 /* Ignore PARAM field in the FC header */
#define SOCAL_COMPLETE	0x0040  /* Command completed */
#define SOCAL_UNSOLICITED 0x0080 /* For request this is the packet to establish unsolicited pools, */
				/* for rsp this is unsolicited packet */
#define SOCAL_STATUS	0x0100	/* State change (on/off line) */
#define SOCAL_RSP_HDR	0x0200	/* Return frame header in any case */

typedef struct {
	u32			token;
	u16			flags;
	u8			class;
	u8			segcnt;
	u32			bytecnt;
} socal_hdr;

typedef struct {
	u32			base;
	u32			count;
} socal_data;

#define SOCAL_CQTYPE_NOP	0x00
#define SOCAL_CQTYPE_OUTBOUND	0x01
#define SOCAL_CQTYPE_INBOUND	0x02
#define SOCAL_CQTYPE_SIMPLE	0x03
#define SOCAL_CQTYPE_IO_WRITE	0x04
#define SOCAL_CQTYPE_IO_READ	0x05
#define SOCAL_CQTYPE_UNSOLICITED 0x06
#define SOCAL_CQTYPE_BYPASS_DEV	0x06
#define SOCAL_CQTYPE_DIAG	0x07
#define SOCAL_CQTYPE_OFFLINE	0x08
#define SOCAL_CQTYPE_ADD_POOL	0x09
#define SOCAL_CQTYPE_DELETE_POOL 0x0a
#define SOCAL_CQTYPE_ADD_BUFFER	0x0b
#define SOCAL_CQTYPE_ADD_POOL_BUFFER 0x0c
#define SOCAL_CQTYPE_REQUEST_ABORT 0x0d
#define SOCAL_CQTYPE_REQUEST_LIP 0x0e
#define SOCAL_CQTYPE_REPORT_MAP	0x0f
#define SOCAL_CQTYPE_RESPONSE	0x10
#define SOCAL_CQTYPE_INLINE	0x20

#define SOCAL_CQFLAGS_CONT	0x01
#define SOCAL_CQFLAGS_FULL	0x02
#define SOCAL_CQFLAGS_BADHDR	0x04
#define SOCAL_CQFLAGS_BADPKT	0x08

typedef struct {
	socal_hdr		shdr;
	socal_data		data[3];
	fc_hdr			fchdr;
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} socal_req;

#define SOCAL_OK		0
#define SOCAL_P_RJT		2
#define SOCAL_F_RJT		3
#define SOCAL_P_BSY		4
#define SOCAL_F_BSY		5
#define SOCAL_ONLINE		0x10
#define SOCAL_OFFLINE		0x11
#define SOCAL_TIMEOUT		0x12
#define SOCAL_OVERRUN		0x13
#define SOCAL_ONLINE_LOOP	0x14
#define SOCAL_OLD_PORT		0x15
#define SOCAL_AL_PORT		0x16
#define SOCAL_UNKOWN_CQ_TYPE	0x20
#define SOCAL_BAD_SEG_CNT	0x21
#define SOCAL_MAX_XCHG_EXCEEDED	0x22
#define SOCAL_BAD_XID		0x23
#define SOCAL_XCHG_BUSY		0x24
#define SOCAL_BAD_POOL_ID	0x25
#define SOCAL_INSUFFICIENT_CQES	0x26
#define SOCAL_ALLOC_FAIL	0x27
#define SOCAL_BAD_SID		0x28
#define SOCAL_NO_SEG_INIT	0x29
#define SOCAL_BAD_DID		0x2a
#define SOCAL_ABORTED		0x30
#define SOCAL_ABORT_FAILED	0x31

typedef struct {
	socal_hdr		shdr;
	u32			status;
	socal_data		data;
	u8			xxx1[10];
	u16			ncmds;
	fc_hdr			fchdr;
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} socal_rsp;

typedef struct {
	socal_hdr		shdr;
	u8			xxx1[48];
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} socal_cmdonly;

#define SOCAL_DIAG_NOP		0x00
#define SOCAL_DIAG_INT_LOOP	0x01
#define SOCAL_DIAG_EXT_LOOP	0x02
#define SOCAL_DIAG_REM_LOOP	0x03
#define SOCAL_DIAG_XRAM_TEST	0x04
#define SOCAL_DIAG_SOC_TEST	0x05
#define SOCAL_DIAG_HCB_TEST	0x06
#define SOCAL_DIAG_SOCLB_TEST	0x07
#define SOCAL_DIAG_SRDSLB_TEST	0x08
#define SOCAL_DIAG_EXTOE_TEST	0x09

typedef struct {
	socal_hdr		shdr;
	u32			cmd;
	u8			xxx1[44];
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} socal_diag_req;

#define SOCAL_POOL_MASK_RCTL	0x800000
#define SOCAL_POOL_MASK_DID	0x700000
#define SOCAL_POOL_MASK_SID	0x070000
#define SOCAL_POOL_MASK_TYPE	0x008000
#define SOCAL_POOL_MASK_F_CTL	0x007000
#define SOCAL_POOL_MASK_SEQ_ID	0x000800
#define SOCAL_POOL_MASK_D_CTL	0x000400
#define SOCAL_POOL_MASK_SEQ_CNT	0x000300
#define SOCAL_POOL_MASK_OX_ID	0x0000f0
#define SOCAL_POOL_MASK_PARAM	0x00000f

typedef struct {
	socal_hdr		shdr;
	u32			pool_id;
	u32			header_mask;
	u32			buf_size;
	u32			entries;
	u8			xxx1[8];
	fc_hdr			fchdr;
	u8			count;
	u8			type;
	u8			flags;
	u8			seqno;
} socal_pool_req;

/* }}} */

/* Now our software structures and constants we use to drive the beast {{{ */

#define SOCAL_CQ_REQ0_SIZE	4
#define SOCAL_CQ_REQ1_SIZE	256
#define SOCAL_CQ_RSP0_SIZE	8
#define SOCAL_CQ_RSP1_SIZE	4
#define SOCAL_CQ_RSP2_SIZE	4

#define SOCAL_SOLICITED_RSP_Q	0
#define SOCAL_SOLICITED_BAD_RSP_Q 1
#define SOCAL_UNSOLICITED_RSP_Q	2

struct socal;

typedef struct {
	/* This must come first */
	fc_channel		fc;
	struct socal		*s;
	u16			flags;
	u16			mask;
} socal_port; 

typedef struct {
	socal_hw_cq		__iomem *hw_cq;	/* Related XRAM cq */
	socal_req		*pool;
	u8			in;
	u8			out;
	u8			last;
	u8			seqno;
} socal_cq;

struct socal {
	spinlock_t		lock;
	socal_port		port[2]; /* Every SOCAL has one or two FC ports */
	socal_cq		req[4]; /* Request CQs */
	socal_cq		rsp[4]; /* Response CQs */
	int			socal_no;
	void __iomem		*regs;
	void __iomem		*xram;
	void __iomem		*eeprom;
	fc_wwn			wwn;
	u32			imask;	/* Our copy of regs->imask */
	u32			cfg;	/* Our copy of regs->cfg */
	char			serv_params[80];
	struct socal		*next;
	int			curr_port; /* Which port will have priority to fcp_queue_empty */

	socal_req *		req_cpu;
	u32			req_dvma;
};

/* }}} */

#endif /* !(__SOCAL_H) */
