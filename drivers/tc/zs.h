/*
 * drivers/tc/zs.h: Definitions for the DECstation Z85C30 serial driver.
 *
 * Adapted from drivers/sbus/char/sunserial.h by Paul Mackerras.
 * Adapted from drivers/macintosh/macserial.h by Harald Koerfgen.
 *
 * Copyright (C) 1996 Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2004, 2005  Maciej W. Rozycki
 */
#ifndef _DECSERIAL_H
#define _DECSERIAL_H

#include <asm/dec/serial.h>

#define NUM_ZSREGS 16

struct serial_struct {
	int	type;
	int	line;
	int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	reserved_char[2];
	int	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	int	reserved[4];
};

/*
 * For the close wait times, 0 means wait forever for serial port to
 * flush its output.  65535 means don't wait at all.
 */
#define ZILOG_CLOSING_WAIT_INF	0
#define ZILOG_CLOSING_WAIT_NONE	65535

/*
 * Definitions for ZILOG_struct (and serial_struct) flags field
 */
#define ZILOG_HUP_NOTIFY 0x0001 /* Notify getty on hangups and closes
				   on the callout port */
#define ZILOG_FOURPORT  0x0002	/* Set OU1, OUT2 per AST Fourport settings */
#define ZILOG_SAK	0x0004	/* Secure Attention Key (Orange book) */
#define ZILOG_SPLIT_TERMIOS 0x0008 /* Separate termios for dialin/callout */

#define ZILOG_SPD_MASK	0x0030
#define ZILOG_SPD_HI	0x0010	/* Use 56000 instead of 38400 bps */

#define ZILOG_SPD_VHI	0x0020  /* Use 115200 instead of 38400 bps */
#define ZILOG_SPD_CUST	0x0030  /* Use user-specified divisor */

#define ZILOG_SKIP_TEST	0x0040 /* Skip UART test during autoconfiguration */
#define ZILOG_AUTO_IRQ  0x0080 /* Do automatic IRQ during autoconfiguration */
#define ZILOG_SESSION_LOCKOUT 0x0100 /* Lock out cua opens based on session */
#define ZILOG_PGRP_LOCKOUT    0x0200 /* Lock out cua opens based on pgrp */
#define ZILOG_CALLOUT_NOHUP   0x0400 /* Don't do hangups for cua device */

#define ZILOG_FLAGS	0x0FFF	/* Possible legal ZILOG flags */
#define ZILOG_USR_MASK 0x0430	/* Legal flags that non-privileged
				 * users can set or reset */

/* Internal flags used only by kernel/chr_drv/serial.c */
#define ZILOG_INITIALIZED	0x80000000 /* Serial port was initialized */
#define ZILOG_CALLOUT_ACTIVE	0x40000000 /* Call out device is active */
#define ZILOG_NORMAL_ACTIVE	0x20000000 /* Normal device is active */
#define ZILOG_BOOT_AUTOCONF	0x10000000 /* Autoconfigure port on bootup */
#define ZILOG_CLOSING		0x08000000 /* Serial port is closing */
#define ZILOG_CTS_FLOW		0x04000000 /* Do CTS flow control */
#define ZILOG_CHECK_CD		0x02000000 /* i.e., CLOCAL */

/* Software state per channel */

#ifdef __KERNEL__
/*
 * This is our internal structure for each serial port's state.
 *
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct dec_zschannel {
	volatile unsigned char *control;
	volatile unsigned char *data;

	/* Current write register values */
	unsigned char curregs[NUM_ZSREGS];
};

struct dec_serial {
	struct dec_serial	*zs_next;	/* For IRQ servicing chain.  */
	struct dec_zschannel	*zs_channel;	/* Channel registers.  */
	struct dec_zschannel	*zs_chan_a;	/* A side registers.  */
	unsigned char		read_reg_zero;

	struct dec_serial_hook	*hook;		/* Hook on this channel.  */
	int			tty_break;	/* Set on BREAK condition.  */
	int			is_cons;	/* Is this our console.  */
	int			tx_active;	/* Char is being xmitted.  */
	int			tx_stopped;	/* Output is suspended.  */

	/*
	 * We need to know the current clock divisor
	 * to read the bps rate the chip has currently loaded.
	 */
	int			clk_divisor;	/* May be 1, 16, 32, or 64.  */
	int			zs_baud;

	char			change_needed;

	int			magic;
	int			baud_base;
	int			port;
	int			irq;
	int			flags; 		/* Defined in tty.h.  */
	int			type; 		/* UART type.  */
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int			custom_divisor;
	int			x_char;		/* XON/XOFF character.  */
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int			count;		/* # of fds on device.  */
	int			blocked_open;	/* # of blocked opens.  */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct tasklet_struct	tlet;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
};


#define SERIAL_MAGIC 0x5301

/*
 * The size of the serial xmit buffer is 1 page, or 4096 bytes
 */
#define SERIAL_XMIT_SIZE 4096

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define RS_EVENT_WRITE_WAKEUP	0

#endif /* __KERNEL__ */

/* Conversion routines to/from brg time constants from/to bits
 * per second.
 */
#define BRG_TO_BPS(brg, freq) ((freq) / 2 / ((brg) + 2))
#define BPS_TO_BRG(bps, freq) ((((freq) + (bps)) / (2 * (bps))) - 2)

/* The Zilog register set */

#define	FLAG	0x7e

/* Write Register 0 */
#define	R0	0		/* Register selects */
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15

#define	NULLCODE	0	/* Null Code */
#define	POINT_HIGH	0x8	/* Select upper half of registers */
#define	RES_EXT_INT	0x10	/* Reset Ext. Status Interrupts */
#define	SEND_ABORT	0x18	/* HDLC Abort */
#define	RES_RxINT_FC	0x20	/* Reset RxINT on First Character */
#define	RES_Tx_P	0x28	/* Reset TxINT Pending */
#define	ERR_RES		0x30	/* Error Reset */
#define	RES_H_IUS	0x38	/* Reset highest IUS */

#define	RES_Rx_CRC	0x40	/* Reset Rx CRC Checker */
#define	RES_Tx_CRC	0x80	/* Reset Tx CRC Checker */
#define	RES_EOM_L	0xC0	/* Reset EOM latch */

/* Write Register 1 */

#define	EXT_INT_ENAB	0x1	/* Ext Int Enable */
#define	TxINT_ENAB	0x2	/* Tx Int Enable */
#define	PAR_SPEC	0x4	/* Parity is special condition */

#define	RxINT_DISAB	0	/* Rx Int Disable */
#define	RxINT_FCERR	0x8	/* Rx Int on First Character Only or Error */
#define	RxINT_ALL	0x10	/* Int on all Rx Characters or error */
#define	RxINT_ERR	0x18	/* Int on error only */
#define	RxINT_MASK	0x18

#define	WT_RDY_RT	0x20	/* Wait/Ready on R/T */
#define	WT_FN_RDYFN	0x40	/* Wait/FN/Ready FN */
#define	WT_RDY_ENAB	0x80	/* Wait/Ready Enable */

/* Write Register #2 (Interrupt Vector) */

/* Write Register 3 */

#define	RxENABLE	0x1	/* Rx Enable */
#define	SYNC_L_INH	0x2	/* Sync Character Load Inhibit */
#define	ADD_SM		0x4	/* Address Search Mode (SDLC) */
#define	RxCRC_ENAB	0x8	/* Rx CRC Enable */
#define	ENT_HM		0x10	/* Enter Hunt Mode */
#define	AUTO_ENAB	0x20	/* Auto Enables */
#define	Rx5		0x0	/* Rx 5 Bits/Character */
#define	Rx7		0x40	/* Rx 7 Bits/Character */
#define	Rx6		0x80	/* Rx 6 Bits/Character */
#define	Rx8		0xc0	/* Rx 8 Bits/Character */
#define RxNBITS_MASK	0xc0

/* Write Register 4 */

#define	PAR_ENA		0x1	/* Parity Enable */
#define	PAR_EVEN	0x2	/* Parity Even/Odd* */

#define	SYNC_ENAB	0	/* Sync Modes Enable */
#define	SB1		0x4	/* 1 stop bit/char */
#define	SB15		0x8	/* 1.5 stop bits/char */
#define	SB2		0xc	/* 2 stop bits/char */
#define SB_MASK		0xc

#define	MONSYNC		0	/* 8 Bit Sync character */
#define	BISYNC		0x10	/* 16 bit sync character */
#define	SDLC		0x20	/* SDLC Mode (01111110 Sync Flag) */
#define	EXTSYNC		0x30	/* External Sync Mode */

#define	X1CLK		0x0	/* x1 clock mode */
#define	X16CLK		0x40	/* x16 clock mode */
#define	X32CLK		0x80	/* x32 clock mode */
#define	X64CLK		0xC0	/* x64 clock mode */
#define XCLK_MASK	0xC0

/* Write Register 5 */

#define	TxCRC_ENAB	0x1	/* Tx CRC Enable */
#define	RTS		0x2	/* RTS */
#define	SDLC_CRC	0x4	/* SDLC/CRC-16 */
#define	TxENAB		0x8	/* Tx Enable */
#define	SND_BRK		0x10	/* Send Break */
#define	Tx5		0x0	/* Tx 5 bits (or less)/character */
#define	Tx7		0x20	/* Tx 7 bits/character */
#define	Tx6		0x40	/* Tx 6 bits/character */
#define	Tx8		0x60	/* Tx 8 bits/character */
#define TxNBITS_MASK	0x60
#define	DTR		0x80	/* DTR */

/* Write Register 6 (Sync bits 0-7/SDLC Address Field) */

/* Write Register 7 (Sync bits 8-15/SDLC 01111110) */

/* Write Register 8 (transmit buffer) */

/* Write Register 9 (Master interrupt control) */
#define	VIS	1	/* Vector Includes Status */
#define	NV	2	/* No Vector */
#define	DLC	4	/* Disable Lower Chain */
#define	MIE	8	/* Master Interrupt Enable */
#define	STATHI	0x10	/* Status high */
#define	SOFTACK	0x20	/* Software Interrupt Acknowledge */
#define	NORESET	0	/* No reset on write to R9 */
#define	CHRB	0x40	/* Reset channel B */
#define	CHRA	0x80	/* Reset channel A */
#define	FHWRES	0xc0	/* Force hardware reset */

/* Write Register 10 (misc control bits) */
#define	BIT6	1	/* 6 bit/8bit sync */
#define	LOOPMODE 2	/* SDLC Loop mode */
#define	ABUNDER	4	/* Abort/flag on SDLC xmit underrun */
#define	MARKIDLE 8	/* Mark/flag on idle */
#define	GAOP	0x10	/* Go active on poll */
#define	NRZ	0	/* NRZ mode */
#define	NRZI	0x20	/* NRZI mode */
#define	FM1	0x40	/* FM1 (transition = 1) */
#define	FM0	0x60	/* FM0 (transition = 0) */
#define	CRCPS	0x80	/* CRC Preset I/O */

/* Write Register 11 (Clock Mode control) */
#define	TRxCXT	0	/* TRxC = Xtal output */
#define	TRxCTC	1	/* TRxC = Transmit clock */
#define	TRxCBR	2	/* TRxC = BR Generator Output */
#define	TRxCDP	3	/* TRxC = DPLL output */
#define	TRxCOI	4	/* TRxC O/I */
#define	TCRTxCP	0	/* Transmit clock = RTxC pin */
#define	TCTRxCP	8	/* Transmit clock = TRxC pin */
#define	TCBR	0x10	/* Transmit clock = BR Generator output */
#define	TCDPLL	0x18	/* Transmit clock = DPLL output */
#define	RCRTxCP	0	/* Receive clock = RTxC pin */
#define	RCTRxCP	0x20	/* Receive clock = TRxC pin */
#define	RCBR	0x40	/* Receive clock = BR Generator output */
#define	RCDPLL	0x60	/* Receive clock = DPLL output */
#define	RTxCX	0x80	/* RTxC Xtal/No Xtal */

/* Write Register 12 (lower byte of baud rate generator time constant) */

/* Write Register 13 (upper byte of baud rate generator time constant) */

/* Write Register 14 (Misc control bits) */
#define	BRENABL	1	/* Baud rate generator enable */
#define	BRSRC	2	/* Baud rate generator source */
#define	DTRREQ	4	/* DTR/Request function */
#define	AUTOECHO 8	/* Auto Echo */
#define	LOOPBAK	0x10	/* Local loopback */
#define	SEARCH	0x20	/* Enter search mode */
#define	RMC	0x40	/* Reset missing clock */
#define	DISDPLL	0x60	/* Disable DPLL */
#define	SSBR	0x80	/* Set DPLL source = BR generator */
#define	SSRTxC	0xa0	/* Set DPLL source = RTxC */
#define	SFMM	0xc0	/* Set FM mode */
#define	SNRZI	0xe0	/* Set NRZI mode */

/* Write Register 15 (external/status interrupt control) */
#define	ZCIE	2	/* Zero count IE */
#define	DCDIE	8	/* DCD IE */
#define	SYNCIE	0x10	/* Sync/hunt IE */
#define	CTSIE	0x20	/* CTS IE */
#define	TxUIE	0x40	/* Tx Underrun/EOM IE */
#define	BRKIE	0x80	/* Break/Abort IE */


/* Read Register 0 */
#define	Rx_CH_AV	0x1	/* Rx Character Available */
#define	ZCOUNT		0x2	/* Zero count */
#define	Tx_BUF_EMP	0x4	/* Tx Buffer empty */
#define	DCD		0x8	/* DCD */
#define	SYNC_HUNT	0x10	/* Sync/hunt */
#define	CTS		0x20	/* CTS */
#define	TxEOM		0x40	/* Tx underrun */
#define	BRK_ABRT	0x80	/* Break/Abort */

/* Read Register 1 */
#define	ALL_SNT		0x1	/* All sent */
/* Residue Data for 8 Rx bits/char programmed */
#define	RES3		0x8	/* 0/3 */
#define	RES4		0x4	/* 0/4 */
#define	RES5		0xc	/* 0/5 */
#define	RES6		0x2	/* 0/6 */
#define	RES7		0xa	/* 0/7 */
#define	RES8		0x6	/* 0/8 */
#define	RES18		0xe	/* 1/8 */
#define	RES28		0x0	/* 2/8 */
/* Special Rx Condition Interrupts */
#define	PAR_ERR		0x10	/* Parity error */
#define	Rx_OVR		0x20	/* Rx Overrun Error */
#define	FRM_ERR		0x40	/* CRC/Framing Error */
#define	END_FR		0x80	/* End of Frame (SDLC) */

/* Read Register 2 (channel b only) - Interrupt vector */

/* Read Register 3 (interrupt pending register) ch a only */
#define	CHBEXT	0x1		/* Channel B Ext/Stat IP */
#define	CHBTxIP	0x2		/* Channel B Tx IP */
#define	CHBRxIP	0x4		/* Channel B Rx IP */
#define	CHAEXT	0x8		/* Channel A Ext/Stat IP */
#define	CHATxIP	0x10		/* Channel A Tx IP */
#define	CHARxIP	0x20		/* Channel A Rx IP */

/* Read Register 8 (receive data register) */

/* Read Register 10  (misc status bits) */
#define	ONLOOP	2		/* On loop */
#define	LOOPSEND 0x10		/* Loop sending */
#define	CLK2MIS	0x40		/* Two clocks missing */
#define	CLK1MIS	0x80		/* One clock missing */

/* Read Register 12 (lower byte of baud rate generator constant) */

/* Read Register 13 (upper byte of baud rate generator constant) */

/* Read Register 15 (value of WR 15) */

/* Misc macros */
#define ZS_CLEARERR(channel)	(write_zsreg(channel, 0, ERR_RES))
#define ZS_CLEARFIFO(channel)	do { volatile unsigned char garbage; \
				     garbage = read_zsdata(channel); \
				     garbage = read_zsdata(channel); \
				     garbage = read_zsdata(channel); \
				} while(0)

#endif /* !(_DECSERIAL_H) */
