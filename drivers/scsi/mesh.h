/*
 * mesh.h: definitions for the driver for the MESH SCSI bus adaptor
 * (Macintosh Enhanced SCSI Hardware) found on Power Macintosh computers.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#ifndef _MESH_H
#define _MESH_H

/*
 * Registers in the MESH controller.
 */

struct mesh_regs {
	unsigned char	count_lo;
	char pad0[15];
	unsigned char	count_hi;
	char pad1[15];
	unsigned char	fifo;
	char pad2[15];
	unsigned char	sequence;
	char pad3[15];
	unsigned char	bus_status0;
	char pad4[15];
	unsigned char	bus_status1;
	char pad5[15];
	unsigned char	fifo_count;
	char pad6[15];
	unsigned char	exception;
	char pad7[15];
	unsigned char	error;
	char pad8[15];
	unsigned char	intr_mask;
	char pad9[15];
	unsigned char	interrupt;
	char pad10[15];
	unsigned char	source_id;
	char pad11[15];
	unsigned char	dest_id;
	char pad12[15];
	unsigned char	sync_params;
	char pad13[15];
	unsigned char	mesh_id;
	char pad14[15];
	unsigned char	sel_timeout;
	char pad15[15];
};

/* Bits in the sequence register. */
#define SEQ_DMA_MODE	0x80	/* use DMA for data transfer */
#define SEQ_TARGET	0x40	/* put the controller into target mode */
#define SEQ_ATN		0x20	/* assert ATN signal */
#define SEQ_ACTIVE_NEG	0x10	/* use active negation on REQ/ACK */
#define SEQ_CMD		0x0f	/* command bits: */
#define SEQ_ARBITRATE	1	/*  get the bus */
#define SEQ_SELECT	2	/*  select a target */
#define SEQ_COMMAND	3	/*  send a command */
#define SEQ_STATUS	4	/*  receive status */
#define SEQ_DATAOUT	5	/*  send data */
#define SEQ_DATAIN	6	/*  receive data */
#define SEQ_MSGOUT	7	/*  send a message */
#define SEQ_MSGIN	8	/*  receive a message */
#define SEQ_BUSFREE	9	/*  look for bus free */
#define SEQ_ENBPARITY	0x0a	/*  enable parity checking */
#define SEQ_DISPARITY	0x0b	/*  disable parity checking */
#define SEQ_ENBRESEL	0x0c	/*  enable reselection */
#define SEQ_DISRESEL	0x0d	/*  disable reselection */
#define SEQ_RESETMESH	0x0e	/*  reset the controller */
#define SEQ_FLUSHFIFO	0x0f	/*  clear out the FIFO */

/* Bits in the bus_status0 and bus_status1 registers:
   these correspond directly to the SCSI bus control signals. */
#define BS0_REQ		0x20
#define BS0_ACK		0x10
#define BS0_ATN		0x08
#define BS0_MSG		0x04
#define BS0_CD		0x02
#define BS0_IO		0x01
#define BS1_RST		0x80
#define BS1_BSY		0x40
#define BS1_SEL		0x20

/* Bus phases defined by the bits in bus_status0 */
#define BS0_PHASE	(BS0_MSG+BS0_CD+BS0_IO)
#define BP_DATAOUT	0
#define BP_DATAIN	BS0_IO
#define BP_COMMAND	BS0_CD
#define BP_STATUS	(BS0_CD+BS0_IO)
#define BP_MSGOUT	(BS0_MSG+BS0_CD)
#define BP_MSGIN	(BS0_MSG+BS0_CD+BS0_IO)

/* Bits in the exception register. */
#define EXC_SELWATN	0x20	/* (as target) we were selected with ATN */
#define EXC_SELECTED	0x10	/* (as target) we were selected w/o ATN */
#define EXC_RESELECTED	0x08	/* (as initiator) we were reselected */
#define EXC_ARBLOST	0x04	/* we lost arbitration */
#define EXC_PHASEMM	0x02	/* SCSI phase mismatch */
#define EXC_SELTO	0x01	/* selection timeout */

/* Bits in the error register */
#define ERR_UNEXPDISC	0x40	/* target unexpectedly disconnected */
#define ERR_SCSIRESET	0x20	/* SCSI bus got reset on us */
#define ERR_SEQERR	0x10	/* we did something the chip didn't like */
#define ERR_PARITY	0x01	/* parity error was detected */

/* Bits in the interrupt and intr_mask registers */
#define INT_ERROR	0x04	/* error interrupt */
#define INT_EXCEPTION	0x02	/* exception interrupt */
#define INT_CMDDONE	0x01	/* command done interrupt */

/* Fields in the sync_params register */
#define SYNC_OFF(x)	((x) >> 4)	/* offset field */
#define SYNC_PER(x)	((x) & 0xf)	/* period field */
#define SYNC_PARAMS(o, p)	(((o) << 4) | (p))
#define ASYNC_PARAMS	2	/* sync_params value for async xfers */

/*
 * Assuming a clock frequency of 50MHz:
 *
 * The transfer period with SYNC_PER(sync_params) == x
 * is (x + 2) * 40ns, except that x == 0 gives 100ns.
 *
 * The units of the sel_timeout register are 10ms.
 */


#endif /* _MESH_H */
