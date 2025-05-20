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

/* Possible GPIB command messages */

enum cmd_byte {
	GTL = 0x1,	/* go to local			*/
	SDC = 0x4,	/* selected device clear	*/
	PP_CONFIG = 0x5,
#ifndef PPC
	PPC = PP_CONFIG, /* parallel poll configure	*/
#endif
	GET = 0x8,	/* group execute trigger	*/
	TCT = 0x9,	/* take control			*/
	LLO = 0x11,	/* local lockout		*/
	DCL = 0x14,	/* device clear			*/
	PPU = 0x15,	/* parallel poll unconfigure	*/
	SPE = 0x18,	/* serial poll enable		*/
	SPD = 0x19,	/* serial poll disable		*/
	CFE = 0x1f, /* configure enable */
	LAD = 0x20,	/* value to be 'ored' in to obtain listen address */
	UNL = 0x3F,	/* unlisten			*/
	TAD = 0x40,	/* value to be 'ored' in to obtain talk address	  */
	UNT = 0x5F,	/* untalk			*/
	SAD = 0x60,	/* my secondary address (base) */
	PPE = 0x60,	/* parallel poll enable (base)	*/
	PPD = 0x70	/* parallel poll disable	*/
};

enum ppe_bits {
	PPC_DISABLE = 0x10,
	PPC_SENSE = 0x8,	/* parallel poll sense bit	*/
	PPC_DIO_MASK = 0x7
};

/* confine address to range 0 to 30. */
static inline unsigned int gpib_address_restrict(unsigned int addr)
{
	addr &= 0x1f;
	if (addr == 0x1f)
		addr = 0;
	return addr;
}

static inline __u8 MLA(unsigned int addr)
{
	return gpib_address_restrict(addr) | LAD;
}

static inline __u8 MTA(unsigned int addr)
{
	return gpib_address_restrict(addr) | TAD;
}

static inline __u8 MSA(unsigned int addr)
{
	return (addr & 0x1f) | SAD;
}

static inline __u8 PPE_byte(unsigned int dio_line, int sense)
{
	__u8 cmd;

	cmd = PPE;
	if (sense)
		cmd |= PPC_SENSE;
	cmd |= (dio_line - 1) & 0x7;
	return cmd;
}

/* mask of bits that actually matter in a command byte */
enum {
	gpib_command_mask = 0x7f,
};

static inline int is_PPE(__u8 command)
{
	return (command & 0x70) == 0x60;
}

static inline int is_PPD(__u8 command)
{
	return (command & 0x70) == 0x70;
}

static inline int in_addressed_command_group(__u8 command)
{
	return (command & 0x70) == 0x0;
}

static inline int in_universal_command_group(__u8 command)
{
	return (command & 0x70) == 0x10;
}

static inline int in_listen_address_group(__u8 command)
{
	return (command & 0x60) == 0x20;
}

static inline int in_talk_address_group(__u8 command)
{
	return (command & 0x60) == 0x40;
}

static inline int in_primary_command_group(__u8 command)
{
	return in_addressed_command_group(command) ||
		in_universal_command_group(command) ||
		in_listen_address_group(command) ||
		in_talk_address_group(command);
}

static inline int gpib_address_equal(unsigned int pad1, int sad1, unsigned int pad2, int sad2)
{
	if (pad1 == pad2) {
		if (sad1 == sad2)
			return 1;
		if (sad1 < 0 && sad2 < 0)
			return 1;
	}

	return 0;
}

enum ibask_option {
	IBA_PAD = 0x1,
	IBA_SAD = 0x2,
	IBA_TMO = 0x3,
	IBA_EOT = 0x4,
	IBA_PPC = 0x5,	/* board only */
	IBA_READ_DR = 0x6,	/* device only */
	IBA_AUTOPOLL = 0x7,	/* board only */
	IBA_CICPROT = 0x8,	/* board only */
	IBA_IRQ = 0x9,	/* board only */
	IBA_SC = 0xa,	/* board only */
	IBA_SRE = 0xb,	/* board only */
	IBA_EOS_RD = 0xc,
	IBA_EOS_WRT = 0xd,
	IBA_EOS_CMP = 0xe,
	IBA_EOS_CHAR = 0xf,
	IBA_PP2 = 0x10,	/* board only */
	IBA_TIMING = 0x11,	/* board only */
	IBA_DMA = 0x12,	/* board only */
	IBA_READ_ADJUST = 0x13,
	IBA_WRITE_ADJUST = 0x14,
	IBA_EVENT_QUEUE = 0x15,	/* board only */
	IBA_SPOLL_BIT = 0x16,	/* board only */
	IBA_SEND_LLO = 0x17,	/* board only */
	IBA_SPOLL_TIME = 0x18,	/* device only */
	IBA_PPOLL_TIME = 0x19,	/* board only */
	IBA_END_BIT_IS_NORMAL = 0x1a,
	IBA_UN_ADDR = 0x1b,	/* device only */
	IBA_HS_CABLE_LENGTH = 0x1f,	/* board only */
	IBA_IST = 0x20,	/* board only */
	IBA_RSV = 0x21,	/* board only */
	IBA_BNA = 0x200,	/* device only */
	/* linux-gpib extensions */
	IBA_7_BIT_EOS = 0x1000	/* board only. Returns 1 if board supports 7 bit eos compares*/
};

enum ibconfig_option {
	IBC_PAD = 0x1,
	IBC_SAD = 0x2,
	IBC_TMO = 0x3,
	IBC_EOT = 0x4,
	IBC_PPC = 0x5,	/* board only */
	IBC_READDR = 0x6,	/* device only */
	IBC_AUTOPOLL = 0x7,	/* board only */
	IBC_CICPROT = 0x8,	/* board only */
	IBC_IRQ = 0x9,	/* board only */
	IBC_SC = 0xa,	/* board only */
	IBC_SRE = 0xb,	/* board only */
	IBC_EOS_RD = 0xc,
	IBC_EOS_WRT = 0xd,
	IBC_EOS_CMP = 0xe,
	IBC_EOS_CHAR = 0xf,
	IBC_PP2 = 0x10,	/* board only */
	IBC_TIMING = 0x11,	/* board only */
	IBC_DMA = 0x12,	/* board only */
	IBC_READ_ADJUST = 0x13,
	IBC_WRITE_ADJUST = 0x14,
	IBC_EVENT_QUEUE = 0x15,	/* board only */
	IBC_SPOLL_BIT = 0x16,	/* board only */
	IBC_SEND_LLO = 0x17,	/* board only */
	IBC_SPOLL_TIME = 0x18,	/* device only */
	IBC_PPOLL_TIME = 0x19,	/* board only */
	IBC_END_BIT_IS_NORMAL = 0x1a,
	IBC_UN_ADDR = 0x1b,	/* device only */
	IBC_HS_CABLE_LENGTH = 0x1f,	/* board only */
	IBC_IST = 0x20,	/* board only */
	IBC_RSV = 0x21,	/* board only */
	IBC_BNA = 0x200	/* device only */
};

enum t1_delays {
	T1_DELAY_2000ns = 1,
	T1_DELAY_500ns = 2,
	T1_DELAY_350ns = 3
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

enum gpib_stb {
	IB_STB_RQS = 0x40, /* IEEE 488.1 & 2  */
	IB_STB_ESB = 0x20, /* IEEE 488.2 only */
	IB_STB_MAV = 0x10	 /* IEEE 488.2 only */
};

#endif	/* _GPIB_H */

