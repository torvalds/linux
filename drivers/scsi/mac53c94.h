/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mac53c94.h: definitions for the driver for the 53c94 SCSI bus adaptor
 * found on Power Macintosh computers, controlling the external SCSI chain.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifndef _MAC53C94_H
#define _MAC53C94_H

/*
 * Registers in the 53C94 controller.
 */

struct mac53c94_regs {
	unsigned char	count_lo;
	char pad0[15];
	unsigned char	count_mid;
	char pad1[15];
	unsigned char	fifo;
	char pad2[15];
	unsigned char	command;
	char pad3[15];
	unsigned char	status;
	char pad4[15];
	unsigned char	interrupt;
	char pad5[15];
	unsigned char	seqstep;
	char pad6[15];
	unsigned char	flags;
	char pad7[15];
	unsigned char	config1;
	char pad8[15];
	unsigned char	clk_factor;
	char pad9[15];
	unsigned char	test;
	char pad10[15];
	unsigned char	config2;
	char pad11[15];
	unsigned char	config3;
	char pad12[15];
	unsigned char	config4;
	char pad13[15];
	unsigned char	count_hi;
	char pad14[15];
	unsigned char	fifo_res;
	char pad15[15];
};

/*
 * Alternate functions for some registers.
 */
#define dest_id		status
#define sel_timeout	interrupt
#define sync_period	seqstep
#define sync_offset	flags

/*
 * Bits in command register.
 */
#define CMD_DMA_MODE	0x80
#define CMD_MODE_MASK	0x70
#define CMD_MODE_INIT	0x10
#define CMD_MODE_TARG	0x20
#define CMD_MODE_DISC	0x40

#define CMD_NOP		0
#define CMD_FLUSH	1
#define CMD_RESET	2
#define CMD_SCSI_RESET	3

#define CMD_XFER_DATA	0x10
#define CMD_I_COMPLETE	0x11
#define CMD_ACCEPT_MSG	0x12
#define CMD_XFER_PAD	0x18
#define CMD_SET_ATN	0x1a
#define CMD_CLR_ATN	0x1b

#define CMD_SEND_MSG	0x20
#define CMD_SEND_STATUS	0x21
#define CMD_SEND_DATA	0x22
#define CMD_DISC_SEQ	0x23
#define CMD_TERMINATE	0x24
#define CMD_T_COMPLETE	0x25
#define CMD_DISCONNECT	0x27
#define CMD_RECV_MSG	0x28
#define CMD_RECV_CDB	0x29
#define CMD_RECV_DATA	0x2a
#define CMD_RECV_CMD	0x2b
#define CMD_ABORT_DMA	0x04

#define CMD_RESELECT	0x40
#define CMD_SELECT	0x41
#define CMD_SELECT_ATN	0x42
#define CMD_SELATN_STOP	0x43
#define CMD_ENABLE_SEL	0x44
#define CMD_DISABLE_SEL	0x45
#define CMD_SEL_ATN3	0x46
#define CMD_RESEL_ATN3	0x47

/*
 * Bits in status register.
 */
#define STAT_IRQ	0x80
#define STAT_ERROR	0x40
#define STAT_PARITY	0x20
#define STAT_TC_ZERO	0x10
#define STAT_DONE	0x08
#define STAT_PHASE	0x07
#define STAT_MSG	0x04
#define STAT_CD		0x02
#define STAT_IO		0x01

/*
 * Bits in interrupt register.
 */
#define INTR_RESET	0x80	/* SCSI bus was reset */
#define INTR_ILL_CMD	0x40	/* illegal command */
#define INTR_DISCONNECT	0x20	/* we got disconnected */
#define INTR_BUS_SERV	0x10	/* bus service requested */
#define INTR_DONE	0x08	/* function completed */
#define INTR_RESELECTED	0x04	/* we were reselected */
#define INTR_SEL_ATN	0x02	/* we were selected, ATN asserted */
#define INTR_SELECT	0x01	/* we were selected, ATN negated */

/*
 * Encoding for the select timeout.
 */
#define TIMO_VAL(x)	((x) * 5000 / 7682)

/*
 * Bits in sequence step register.
 */
#define SS_MASK		7
#define SS_ARB_SEL	0	/* Selection & arbitration complete */
#define SS_MSG_SENT	1	/* One message byte sent */
#define SS_NOT_CMD	2	/* Not in command phase */
#define SS_PHASE_CHG	3	/* Early phase change, cmd bytes lost */
#define SS_DONE		4	/* Command was sent OK */

/*
 * Encoding for sync transfer period.
 */
#define SYNCP_MASK	0x1f
#define SYNCP_MIN	4
#define SYNCP_MAX	31

/*
 * Bits in flags register.
 */
#define FLAGS_FIFO_LEV	0x1f
#define FLAGS_SEQ_STEP	0xe0

/*
 * Encoding for sync offset.
 */
#define SYNCO_MASK	0x0f
#define SYNCO_ASS_CTRL	0x30	/* REQ/ACK assertion control */
#define SYNCO_NEG_CTRL	0xc0	/* REQ/ACK negation control */

/*
 * Bits in config1 register.
 */
#define CF1_SLOW_CABLE	0x80	/* Slow cable mode */
#define CF1_NO_RES_REP	0x40	/* Disable SCSI reset reports */
#define CF1_PAR_TEST	0x20	/* Parity test mode enable */
#define CF1_PAR_ENABLE	0x10	/* Enable parity checks */
#define CF1_TEST	0x08	/* Chip tests */
#define CF1_MY_ID	0x07	/* Controller's address on bus */

/*
 * Encoding for clk_factor register.
 */
#define CLKF_MASK	7
#define CLKF_VAL(freq)	((((freq) + 4999999) / 5000000) & CLKF_MASK)

/*
 * Bits in test mode register.
 */
#define TEST_TARGET	1	/* target test mode */
#define TEST_INITIATOR	2	/* initiator test mode */
#define TEST_TRISTATE	4	/* tristate (hi-z) test mode */

/*
 * Bits in config2 register.
 */
#define CF2_RFB		0x80
#define CF2_FEATURE_EN	0x40	/* enable features / phase latch */
#define CF2_BYTECTRL	0x20
#define CF2_DREQ_HIZ	0x10
#define CF2_SCSI2	0x08
#define CF2_PAR_ABORT	0x04	/* bad parity target abort */
#define CF2_REG_PARERR	0x02	/* register parity error */
#define CF2_DMA_PARERR	0x01	/* DMA parity error */

/*
 * Bits in the config3 register.
 */
#define CF3_ID_MSG_CHK	0x80
#define CF3_3B_MSGS	0x40
#define CF3_CDB10	0x20
#define CF3_FASTSCSI	0x10	/* enable fast SCSI support */
#define CF3_FASTCLOCK	0x08
#define CF3_SAVERESID	0x04
#define CF3_ALT_DMA	0x02
#define CF3_THRESH_8	0x01

/*
 * Bits in the config4 register.
 */
#define CF4_EAN		0x04
#define CF4_TEST	0x02
#define CF4_BBTE	0x01

#endif /* _MAC53C94_H */
