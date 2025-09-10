/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright		   : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#ifndef _GPIB_H
#define _GPIB_H

#define GPIB_MAX_NUM_BOARDS 16
#define GPIB_MAX_NUM_DESCRIPTORS 0x1000

enum ibsta_bit_numbers {
	DCAS_NUM = 0,
	DTAS_NUM = 1,
	LACS_NUM = 2,
	TACS_NUM = 3,
	ATN_NUM = 4,
	CIC_NUM = 5,
	REM_NUM = 6,
	LOK_NUM = 7,
	CMPL_NUM = 8,
	EVENT_NUM = 9,
	SPOLL_NUM = 10,
	RQS_NUM = 11,
	SRQI_NUM = 12,
	END_NUM = 13,
	TIMO_NUM = 14,
	ERR_NUM = 15
};

/* IBSTA status bits (returned by all functions) */
enum ibsta_bits {
	DCAS = (1 << DCAS_NUM),	/* device clear state */
	DTAS = (1 << DTAS_NUM),	/* device trigger state */
	LACS = (1 <<  LACS_NUM),	/* GPIB interface is addressed as Listener */
	TACS = (1 <<  TACS_NUM),	/* GPIB interface is addressed as Talker */
	ATN = (1 <<  ATN_NUM),	/* Attention is asserted */
	CIC = (1 <<  CIC_NUM),	/* GPIB interface is Controller-in-Charge */
	REM = (1 << REM_NUM),	/* remote state */
	LOK = (1 << LOK_NUM),	/* lockout state */
	CMPL = (1 <<  CMPL_NUM),	/* I/O is complete  */
	EVENT = (1 << EVENT_NUM),	/* DCAS, DTAS, or IFC has occurred */
	SPOLL = (1 << SPOLL_NUM),	/* board serial polled by busmaster */
	RQS = (1 <<  RQS_NUM),	/* Device requesting service  */
	SRQI = (1 << SRQI_NUM),	/* SRQ is asserted */
	END = (1 << END_NUM),	/* EOI or EOS encountered */
	TIMO = (1 << TIMO_NUM),	/* Time limit on I/O or wait function exceeded */
	ERR = (1 << ERR_NUM),	/* Function call terminated on error */

	device_status_mask = ERR | TIMO | END | CMPL | RQS,
	board_status_mask = ERR | TIMO | END | CMPL | SPOLL |
		EVENT | LOK | REM | CIC | ATN | TACS | LACS | DTAS | DCAS | SRQI,
};

/* End-of-string (EOS) modes for use with ibeos */

enum eos_flags {
	EOS_MASK = 0x1c00,
	REOS = 0x0400,		/* Terminate reads on EOS	*/
	XEOS = 0x800,	/* assert EOI when EOS char is sent */
	BIN = 0x1000		/* Do 8-bit compare on EOS	*/
};

/* GPIB Bus Control Lines bit vector */
enum bus_control_line {
	VALID_DAV = 0x01,
	VALID_NDAC = 0x02,
	VALID_NRFD = 0x04,
	VALID_IFC = 0x08,
	VALID_REN = 0x10,
	VALID_SRQ = 0x20,
	VALID_ATN = 0x40,
	VALID_EOI = 0x80,
	VALID_ALL = 0xff,
	BUS_DAV = 0x0100,		/* DAV	line status bit */
	BUS_NDAC = 0x0200,		/* NDAC line status bit */
	BUS_NRFD = 0x0400,		/* NRFD line status bit */
	BUS_IFC = 0x0800,		/* IFC	line status bit */
	BUS_REN = 0x1000,		/* REN	line status bit */
	BUS_SRQ = 0x2000,		/* SRQ	line status bit */
	BUS_ATN = 0x4000,		/* ATN	line status bit */
	BUS_EOI = 0x8000		/* EOI	line status bit */
};

enum ppe_bits {
	PPC_DISABLE = 0x10,
	PPC_SENSE = 0x8,	/* parallel poll sense bit	*/
	PPC_DIO_MASK = 0x7
};

enum {
	request_service_bit = 0x40,
};

enum gpib_events {
	EVENT_NONE = 0,
	EVENT_DEV_TRG = 1,
	EVENT_DEV_CLR = 2,
	EVENT_IFC = 3
};

#endif	/* _GPIB_H */

