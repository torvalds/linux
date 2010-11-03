/*
 *  linux/drivers/acorn/net/ether1.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Network driver for Acorn Ether1 cards.
 */

#ifndef _LINUX_ether1_H
#define _LINUX_ether1_H

#ifdef __ETHER1_C
/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 0
#endif

#define priv(dev)	((struct ether1_priv *)netdev_priv(dev))

/* Page register */
#define REG_PAGE	(priv(dev)->base + 0x0000)

/* Control register */
#define REG_CONTROL	(priv(dev)->base + 0x0004)
#define CTRL_RST	0x01
#define CTRL_LOOPBACK	0x02
#define CTRL_CA		0x04
#define CTRL_ACK	0x08

#define ETHER1_RAM	(priv(dev)->base + 0x2000)

/* HW address */
#define IDPROM_ADDRESS	(priv(dev)->base + 0x0024)

struct ether1_priv {
	void __iomem *base;
	unsigned int tx_link;
	unsigned int tx_head;
	volatile unsigned int tx_tail;
	volatile unsigned int rx_head;
	volatile unsigned int rx_tail;
	unsigned char bus_type;
	unsigned char resetting;
	unsigned char initialising : 1;
	unsigned char restart      : 1;
};

#define I82586_NULL (-1)

typedef struct { /* tdr */
	unsigned short tdr_status;
	unsigned short tdr_command;
	unsigned short tdr_link;
	unsigned short tdr_result;
#define TDR_TIME	(0x7ff)
#define TDR_SHORT	(1 << 12)
#define TDR_OPEN	(1 << 13)
#define TDR_XCVRPROB	(1 << 14)
#define TDR_LNKOK	(1 << 15)
} tdr_t;

typedef struct { /* transmit */
	unsigned short tx_status;
	unsigned short tx_command;
	unsigned short tx_link;
	unsigned short tx_tbdoffset;
} tx_t;

typedef struct { /* tbd */
	unsigned short tbd_opts;
#define TBD_CNT		(0x3fff)
#define TBD_EOL		(1 << 15)
	unsigned short tbd_link;
	unsigned short tbd_bufl;
	unsigned short tbd_bufh;
} tbd_t;

typedef struct { /* rfd */
	unsigned short rfd_status;
#define RFD_NOEOF	(1 << 6)
#define RFD_FRAMESHORT	(1 << 7)
#define RFD_DMAOVRN	(1 << 8)
#define RFD_NORESOURCES	(1 << 9)
#define RFD_ALIGNERROR	(1 << 10)
#define RFD_CRCERROR	(1 << 11)
#define RFD_OK		(1 << 13)
#define RFD_FDCONSUMED	(1 << 14)
#define RFD_COMPLETE	(1 << 15)
	unsigned short rfd_command;
#define RFD_CMDSUSPEND	(1 << 14)
#define RFD_CMDEL	(1 << 15)
	unsigned short rfd_link;
	unsigned short rfd_rbdoffset;
	unsigned char  rfd_dest[6];
	unsigned char  rfd_src[6];
	unsigned short rfd_len;
} rfd_t;

typedef struct { /* rbd */
	unsigned short rbd_status;
#define RBD_ACNT	(0x3fff)
#define RBD_ACNTVALID	(1 << 14)
#define RBD_EOF		(1 << 15)
	unsigned short rbd_link;
	unsigned short rbd_bufl;
	unsigned short rbd_bufh;
	unsigned short rbd_len;
} rbd_t;

typedef struct { /* nop */
	unsigned short nop_status;
	unsigned short nop_command;
	unsigned short nop_link;
} nop_t;

typedef struct { /* set multicast */
	unsigned short mc_status;
	unsigned short mc_command;
	unsigned short mc_link;
	unsigned short mc_cnt;
	unsigned char  mc_addrs[1][6];
} mc_t;

typedef struct { /* set address */
	unsigned short sa_status;
	unsigned short sa_command;
	unsigned short sa_link;
	unsigned char  sa_addr[6];
} sa_t;

typedef struct { /* config command */
	unsigned short cfg_status;
	unsigned short cfg_command;
	unsigned short cfg_link;
	unsigned char  cfg_bytecnt;	/* size foll data: 4 - 12		 */
	unsigned char  cfg_fifolim;	/* FIFO threshold			 */
	unsigned char  cfg_byte8;
#define CFG8_SRDY	(1 << 6)
#define CFG8_SAVEBADF	(1 << 7)
	unsigned char  cfg_byte9;
#define CFG9_ADDRLEN(x)	(x)
#define CFG9_ADDRLENBUF	(1 << 3)
#define CFG9_PREAMB2	(0 << 4)
#define CFG9_PREAMB4	(1 << 4)
#define CFG9_PREAMB8	(2 << 4)
#define CFG9_PREAMB16	(3 << 4)
#define CFG9_ILOOPBACK	(1 << 6)
#define CFG9_ELOOPBACK	(1 << 7)
	unsigned char  cfg_byte10;
#define CFG10_LINPRI(x)	(x)
#define CFG10_ACR(x)	(x << 4)
#define CFG10_BOFMET	(1 << 7)
	unsigned char  cfg_ifs;
	unsigned char  cfg_slotl;
	unsigned char  cfg_byte13;
#define CFG13_SLOTH(x)	(x)
#define CFG13_RETRY(x)	(x << 4)
	unsigned char  cfg_byte14;
#define CFG14_PROMISC	(1 << 0)
#define CFG14_DISBRD	(1 << 1)
#define CFG14_MANCH	(1 << 2)
#define CFG14_TNCRS	(1 << 3)
#define CFG14_NOCRC	(1 << 4)
#define CFG14_CRC16	(1 << 5)
#define CFG14_BTSTF	(1 << 6)
#define CFG14_FLGPAD	(1 << 7)
	unsigned char  cfg_byte15;
#define CFG15_CSTF(x)	(x)
#define CFG15_ICSS	(1 << 3)
#define CFG15_CDTF(x)	(x << 4)
#define CFG15_ICDS	(1 << 7)
	unsigned short cfg_minfrmlen;
} cfg_t;

typedef struct { /* scb */
	unsigned short scb_status;	/* status of 82586			*/
#define SCB_STRXMASK		(7 << 4)	/* Receive unit status		*/
#define SCB_STRXIDLE		(0 << 4)	/* Idle				*/
#define SCB_STRXSUSP		(1 << 4)	/* Suspended			*/
#define SCB_STRXNRES		(2 << 4)	/* No resources			*/
#define SCB_STRXRDY		(4 << 4)	/* Ready			*/
#define SCB_STCUMASK		(7 << 8)	/* Command unit status		*/
#define SCB_STCUIDLE		(0 << 8)	/* Idle				*/
#define SCB_STCUSUSP		(1 << 8)	/* Suspended			*/
#define SCB_STCUACTV		(2 << 8)	/* Active			*/
#define SCB_STRNR		(1 << 12)	/* Receive unit not ready	*/
#define SCB_STCNA		(1 << 13)	/* Command unit not ready	*/
#define SCB_STFR		(1 << 14)	/* Frame received		*/
#define SCB_STCX		(1 << 15)	/* Command completed		*/
	unsigned short scb_command;	/* Next command				*/
#define SCB_CMDRXSTART		(1 << 4)	/* Start (at rfa_offset)	*/
#define SCB_CMDRXRESUME		(2 << 4)	/* Resume reception		*/
#define SCB_CMDRXSUSPEND	(3 << 4)	/* Suspend reception		*/
#define SCB_CMDRXABORT		(4 << 4)	/* Abort reception		*/
#define SCB_CMDCUCSTART		(1 << 8)	/* Start (at cbl_offset)	*/
#define SCB_CMDCUCRESUME	(2 << 8)	/* Resume execution		*/
#define SCB_CMDCUCSUSPEND	(3 << 8)	/* Suspend execution		*/
#define SCB_CMDCUCABORT		(4 << 8)	/* Abort execution		*/
#define SCB_CMDACKRNR		(1 << 12)	/* Ack RU not ready		*/
#define SCB_CMDACKCNA		(1 << 13)	/* Ack CU not ready		*/
#define SCB_CMDACKFR		(1 << 14)	/* Ack Frame received		*/
#define SCB_CMDACKCX		(1 << 15)	/* Ack Command complete		*/
	unsigned short scb_cbl_offset;	/* Offset of first command unit		*/
	unsigned short scb_rfa_offset;	/* Offset of first receive frame area	*/
	unsigned short scb_crc_errors;	/* Properly aligned frame with CRC error*/
	unsigned short scb_aln_errors;	/* Misaligned frames			*/
	unsigned short scb_rsc_errors;	/* Frames lost due to no space		*/
	unsigned short scb_ovn_errors;	/* Frames lost due to slow bus		*/
} scb_t;

typedef struct { /* iscp */
	unsigned short iscp_busy;	/* set by CPU before CA			*/
	unsigned short iscp_offset;	/* offset of SCB			*/
	unsigned short iscp_basel;	/* base of SCB				*/
	unsigned short iscp_baseh;
} iscp_t;

    /* this address must be 0xfff6 */
typedef struct { /* scp */
	unsigned short scp_sysbus;	/* bus size */
#define SCP_SY_16BBUS	0x00
#define SCP_SY_8BBUS	0x01
	unsigned short scp_junk[2];	/* junk */
	unsigned short scp_iscpl;	/* lower 16 bits of iscp */
	unsigned short scp_iscph;	/* upper 16 bits of iscp */
} scp_t;

/* commands */
#define CMD_NOP			0
#define CMD_SETADDRESS		1
#define CMD_CONFIG		2
#define CMD_SETMULTICAST	3
#define CMD_TX			4
#define CMD_TDR			5
#define CMD_DUMP		6
#define CMD_DIAGNOSE		7

#define CMD_MASK		7

#define CMD_INTR		(1 << 13)
#define CMD_SUSP		(1 << 14)
#define CMD_EOL			(1 << 15)

#define STAT_COLLISIONS		(15)
#define STAT_COLLEXCESSIVE	(1 << 5)
#define STAT_COLLAFTERTX	(1 << 6)
#define STAT_TXDEFERRED		(1 << 7)
#define STAT_TXSLOWDMA		(1 << 8)
#define STAT_TXLOSTCTS		(1 << 9)
#define STAT_NOCARRIER		(1 << 10)
#define STAT_FAIL		(1 << 11)
#define STAT_ABORTED		(1 << 12)
#define STAT_OK			(1 << 13)
#define STAT_BUSY		(1 << 14)
#define STAT_COMPLETE		(1 << 15)
#endif
#endif

/*
 * Ether1 card definitions:
 *
 * FAST accesses:
 *	+0	Page register
 * 			16 pages
 *	+4	Control
 *			'1' = reset
 *			'2' = loopback
 *			'4' = CA
 *			'8' = int ack
 *
 * RAM at address + 0x2000
 * Pod. Prod id = 3
 * Words after ID block [base + 8 words]
 *	+0 pcb issue (0x0c and 0xf3 invalid)
 *	+1 - +6 eth hw address
 */
