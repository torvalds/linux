/*
 *	i82596 ethernet controller bits and structures (little endian)
 *
 *	$Id: i82596.h,v 1.8 1996/09/03 11:19:03 rick Exp $
 */

/************************************************************************/
/*									*/
/*	PORT commands (p. 4-20).  The least significant nibble is one	*/
/*	of these commands, the rest of the command is a memory address	*/
/*	aligned on a 16 byte boundary.  Note that port commands must	*/
/*	be written to the PORT address and the PORT address+2 with two	*/
/*	halfword writes.  Write the LSH first to PORT, then the MSH to	*/
/*	PORT+2.  Blame Intel.						*/
/*									*/
/************************************************************************/
#define	I596_PORT_RESET		0x0	/* Reset. Wait 5 SysClks & 10 TxClks */
#define	I596_PORT_SELFTEST	0x1	/* Do a selftest */
#define	I596_PORT_SCP_ADDR	0x2	/* Set new SCP address */
#define	I596_PORT_DUMP		0x3	/* Dump internal data structures */

/*
 *	I596_ST:	Selftest results (p. 4-21)
 */
typedef volatile struct
{
	ulong	signature;	/* ROM checksum */
	ulong	result;		/* Selftest results: non-zero is a failure */
} I596_ST;

#define	I596_ST_SELFTEST_FAIL	0x1000	/* Selftest Failed */
#define	I596_ST_DIAGNOSE_FAIL	0x0020	/* Diagnose Failed */
#define	I596_ST_BUSTIMER_FAIL	0x0010	/* Bus Timer Failed */
#define	I596_ST_REGISTER_FAIL	0x0008	/* Register Failed */
#define	I596_ST_ROM_FAIL	0x0004	/* ROM Failed */

/*
 *	I596_DUMP:	Dump results
 */
typedef volatile struct
{
	ulong	dump[77];
} I596_DUMP;

/************************************************************************/
/*									*/
/*	I596_TBD:	Transmit Buffer Descriptor (p. 4-59)		*/
/*									*/
/************************************************************************/
typedef volatile struct _I596_TBD
{
	ulong			count;
	vol struct _I596_TBD	*next;
	uchar			*buf;
	ushort			unused1;
	ushort			unused2;
} I596_TBD;

#define	I596_TBD_NOLINK		((I596_TBD *) 0xffffffff)
#define	I596_TBD_EOF		0x8000
#define I596_TBD_COUNT_MASK	0x3fff

/************************************************************************/
/*									*/
/*	I596_TFD:	Transmit Frame Descriptor (p. 4-56)		*/
/*			a.k.a. I596_CB_XMIT				*/
/*									*/
/************************************************************************/
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	I596_TBD		*tbdp;
	ulong			count;	/* for speed */

	/* Application defined data follows structure... */

#if 0	/* We don't use these intel defined ones */
		uchar			addr[6];
		ushort			len;
		uchar			data[1];
#else
		ulong		dstchan;/* Used by multi-NIC mode */
#endif
} I596_TFD;

#define	I596_TFD_NOCRC	0x0010	/* cmd: No CRC insertion */
#define	I596_TFD_FLEX	0x0008	/* cmd: Flexible mode */

/************************************************************************/
/*									*/
/*	I596_RBD:	Receive Buffer Descriptor (p. 4-84)		*/
/*									*/
/************************************************************************/
typedef volatile struct _I596_RBD
{
#ifdef INTEL_RETENTIVE
	ushort			count;	/* Length of data in buf */
	ushort			offset;
#else
	ulong			count;	/* Length of data in buf */
#endif
	vol struct _I596_RBD	*next;	/* Next buffer descriptor in list */
	uchar			*buf;	/* Data buffer */
#ifdef INTEL_RETENTIVE
	ushort			size;	/* Size of buf (constant) */
	ushort			zero;
#else
	ulong			size;	/* Size of buf (constant) */
#endif

	/* Application defined data follows structure... */

	uchar			chan;
	uchar			refcnt;
	ushort			len;
} I596_RBD;

#define	I596_RBD_NOLINK		((I596_RBD *) 0xffffffff)
#define	I596_RBD_EOF		0x8000	/* This is last buffer in a frame */
#define	I596_RBD_F		0x4000	/* The actual count is valid */

#define	I596_RBD_EL		0x8000	/* Last buffer in list */

/************************************************************************/
/*									*/
/*	I596_RFD:	Receive Frame Descriptor (p. 4-79)		*/
/*									*/
/************************************************************************/
typedef volatile struct _I596_RFD
{
	ushort			status;
	ushort			cmd;
	vol struct _I596_RFD	*next;
	vol struct _I596_RBD	*rbdp;
	ushort			count;	/* Len of data in RFD: always 0 */
	ushort			size;	/* Size of RFD buffer: always 0 */

	/* Application defined data follows structure... */

#	if 0	/* We don't use these intel defined ones */
		uchar		addr[6];
		ushort		len;
		uchar		data[1];
#	else
		ulong		dstchan;/* Used by multi-nic mode */
#	endif
} I596_RFD;

#define	I596_RFD_C		0x8000	/* status: frame complete */
#define	I596_RFD_B		0x4000	/* status: frame busy or waiting */
#define	I596_RFD_OK		0x2000	/* status: frame OK */
#define	I596_RFD_ERR_LENGTH	0x1000	/* status: length error */
#define	I596_RFD_ERR_CRC	0x0800	/* status: CRC error */
#define	I596_RFD_ERR_ALIGN	0x0400	/* status: alignment error */
#define	I596_RFD_ERR_NOBUFS	0x0200	/* status: resource error */
#define	I596_RFD_ERR_DMA	0x0100	/* status: DMA error */
#define	I596_RFD_ERR_SHORT	0x0080	/* status: too short error */
#define	I596_RFD_NOMATCH	0x0002	/* status: IA was not matched */
#define	I596_RFD_COLLISION	0x0001	/* status: collision during receive */

#define	I596_RFD_EL		0x8000	/* cmd: end of RFD list */
#define	I596_RFD_FLEX		0x0008	/* cmd: Flexible mode */
#define	I596_RFD_EOF		0x8000	/* count: last buffer in the frame */
#define	I596_RFD_F		0x4000	/* count: The actual count is valid */

/************************************************************************/
/*									*/
/*	Commands							*/
/*									*/
/************************************************************************/

	/* values for cmd halfword in all the structs below */
#define I596_CB_CMD		0x07	/* CB COMMANDS */
#define I596_CB_CMD_NOP		0
#define I596_CB_CMD_IA		1
#define I596_CB_CMD_CONF	2
#define I596_CB_CMD_MCAST	3
#define I596_CB_CMD_XMIT	4
#define I596_CB_CMD_TDR		5
#define I596_CB_CMD_DUMP	6
#define I596_CB_CMD_DIAG	7

#define	I596_CB_CMD_EL		0x8000	/* CB is last in linked list */
#define	I596_CB_CMD_S		0x4000	/* Suspend after execution */
#define	I596_CB_CMD_I		0x2000	/* cause interrupt */

	/* values for the status halfword in all the struct below */
#define	I596_CB_STATUS		0xF000	/* All four status bits */
#define	I596_CB_STATUS_C	0x8000	/* Command complete */
#define	I596_CB_STATUS_B	0x4000	/* Command busy executing */
#define	I596_CB_STATUS_C_OR_B	0xC000	/* Command complete or busy */
#define	I596_CB_STATUS_OK	0x2000	/* Command complete, no errors */
#define	I596_CB_STATUS_A	0x1000	/* Command busy executing */

#define	I596_CB_NOLINK		((I596_CB *) 0xffffffff)

/*
 *	I596_CB_NOP:	NOP Command (p. 4-34)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
} I596_CB_NOP;

/*
 *	Same as above, but command and status in one ulong for speed
 */
typedef volatile struct
{
	ulong			csr;
	union _I596_CB		*next;
} I596_CB_FAST;
#define	FASTs(X)	(X)
#define	FASTc(X)	((X)<<16)

/*
 *	I596_CB_IA:	Individual (MAC) Address Command (p. 4-35)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	uchar			addr[6];
} I596_CB_IA;

/*
 *	I596_CB_CONF:	Configure Command (p. 4-37)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	uchar			conf[14];
} I596_CB_CONF;

#define	I596_CONF0_P		0x80	/* Enable RBD Prefetch Bit */
#define	I596_CONF0_COUNT	14	/* Count of configuration bytes */

#define	I596_CONF1_MON_OFF	0xC0	/* Monitor mode: Monitor off */
#define	I596_CONF1_MON_ON	0x80	/* Monitor mode: Monitor on */
#define	I596_CONF1_TxFIFO(W)	(W)	/* TxFIFO trigger, in words */

#define	I596_CONF2_SAVEBF	0x80	/* Save bad frames */

#define	I596_CONF3_ADDRLEN(B)	(B)	/* Address length */
#define	I596_CONF3_NOSRCINSERT	0x08	/* Do not insert source address */
#define	I596_CONF3_PREAMBLE8	0x20	/* 8 byte preamble */
#define	I596_CONF3_LOOPOFF	0x00	/* Loopback: Off */
#define	I596_CONF3_LOOPINT	0x40	/* Loopback: internal */
#define	I596_CONF3_LOOPEXT	0xC0	/* Loopback: external */

#define	I596_CONF4_LINPRI(ST)	(ST)	/* Linear priority: slot times */
#define	I596_CONF4_EXPPRI(ST)	(ST)	/* Exponential priority: slot times */
#define	I596_CONF4_IEEE_BOM	0	/* IEEE 802.3 backoff method */

#define	I596_CONF5_IFS(X)	(X)	/* Interframe spacing in clocks */

#define	I596_CONF6_ST_LOW(X)	(X&255)	/* Slot time, low byte */

#define	I596_CONF7_ST_HI(X)	(X>>8)	/* Slot time, high bits */
#define	I596_CONF7_RETRY(X)	(X<<4)	/* Max retry number */

#define	I596_CONF8_PROMISC	0x01	/* Rcv all frames */
#define	I596_CONF8_NOBROAD	0x02
#define	I596_CONF8_MANCHESTER	0x04
#define	I596_CONF8_TxNOCRS	0x08
#define	I596_CONF8_NOCRC	0x10
#define	I596_CONF8_CRC_CCITT	0x20
#define	I596_CONF8_BITSTUFFING	0x40
#define	I596_CONF8_PADDING	0x80

#define	I596_CONF9_CSFILTER(X)	(X)
#define	I596_CONF9_CSINT(X)	0x08
#define	I596_CONF9_CDFILTER(X)	(X<<4)
#define	I596_CONF9_CDINT(X)	0x80

#define	I596_CONF10_MINLEN(X)	(X)	/* Minimum frame length */

#define	I596_CONF11_PRECRS_	0x01	/* Preamble before carrier sense */
#define	I596_CONF11_LNGFLD_	0x02	/* Padding in End of Carrier */
#define	I596_CONF11_CRCINM_	0x04	/* CRC in memory */
#define	I596_CONF11_AUTOTX	0x08	/* Auto retransmit */
#define	I596_CONF11_CSBSAC_	0x10	/* Collision detect by src addr cmp. */
#define	I596_CONF11_MCALL_	0x20	/* Multicast all */

#define I596_CONF13_RESERVED	0x3f	/* Reserved: must be ones */
#define I596_CONF13_MULTIA	0x40	/* Enable multiple addr. reception */
#define I596_CONF13_DISBOF	0x80	/* Disable backoff algorithm */
/*
 *	I596_CB_MCAST:	Multicast-Setup Command (p. 4-54)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	ushort			count;		/* Number of 6-byte addrs that follow */
	uchar			addr[6][1];
} I596_CB_MCAST;

/*
 *	I596_CB_XMIT:	Transmit Command (p. 4-56)
 */
typedef	I596_TFD	I596_CB_XMIT;

#define	I596_CB_XMIT_NOCRC	0x0010	/* cmd: No CRC insertion */
#define	I596_CB_XMIT_FLEX	0x0008	/* cmd: Flexible memory mode */

#define	I596_CB_XMIT_ERR_LATE	0x0800	/* status: error: late collision */
#define	I596_CB_XMIT_ERR_NOCRS	0x0400	/* status: error: no carriers sense */
#define	I596_CB_XMIT_ERR_NOCTS	0x0200	/* status: error: loss of CTS */
#define	I596_CB_XMIT_ERR_UNDER	0x0100	/* status: error: DMA underrun */
#define	I596_CB_XMIT_ERR_MAXCOL	0x0020	/* status: error: maximum collisions */
#define	I596_CB_XMIT_COLLISIONS	0x000f	/* status: number of collisions */

/*
 *	I596_CB_TDR:	Time Domain Reflectometry Command (p. 4-63)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	ushort			time;
} I596_CB_TDR;

/*
 *	I596_CB_DUMP:	Dump Command (p. 4-65)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
	uchar			*buf;
} I596_CB_DUMP;

/*
 *	I596_CB_DIAG:	Diagnose Command (p. 4-77)
 */
typedef volatile struct
{
	ushort			status;
	ushort			cmd;
	union _I596_CB		*next;
} I596_CB_DIAG;

/*
 *	I596_CB:	Command Block
 */
typedef union _I596_CB
{
	I596_CB_NOP		nop;
	I596_CB_IA		ia;
	I596_CB_CONF		conf;
	I596_CB_MCAST		mcast;
	I596_CB_XMIT		xmit;
	I596_CB_TDR		tdr;
	I596_CB_DUMP		dump;
	I596_CB_DIAG		diag;

	/* command and status in one ulong for speed... */
	I596_CB_FAST		fast;
} I596_CB;

/************************************************************************/
/*									*/
/*	I596_SCB:	System Configuration Block (p. 4-26)		*/
/*									*/
/************************************************************************/
typedef volatile struct
{
	volatile ushort		status;		/* Status word */
	volatile ushort		cmd;		/* Command word */
	I596_CB		*cbp;
	I596_RFD	*rfdp;
	ulong		crc_errs;
	ulong		align_errs;
	ulong		resource_errs;
	ulong		overrun_errs;
	ulong		rcvcdt_errs;
	ulong		short_errs;
	ushort		toff;
	ushort		ton;
} I596_SCB;

	/* cmd halfword values */
#define	I596_SCB_ACK		0xF000	/* ACKNOWLEDGMENTS */
#define	I596_SCB_ACK_CX		0x8000	/* Ack command completion */
#define	I596_SCB_ACK_FR		0x4000	/* Ack received frame */
#define	I596_SCB_ACK_CNA	0x2000	/* Ack command unit not active */
#define	I596_SCB_ACK_RNR	0x1000	/* Ack rcv unit not ready */
#define	I596_SCB_ACK_ALL	0xF000	/* Ack everything */

#define	I596_SCB_CUC		0x0700	/* COMMAND UNIT COMMANDS */
#define	I596_SCB_CUC_NOP	0x0000	/* No operation */
#define	I596_SCB_CUC_START	0x0100	/* Start execution of first CB */
#define	I596_SCB_CUC_RESUME	0x0200	/* Resume execution */
#define	I596_SCB_CUC_SUSPEND	0x0300	/* Suspend after current CB */
#define	I596_SCB_CUC_ABORT	0x0400	/* Abort current CB immediately */
#define	I596_SCB_CUC_LOAD	0x0500	/* Load Bus throttle timers */
#define	I596_SCB_CUC_LOADIMM	0x0600	/* Load Bus throttle timers, now */

#define	I596_SCB_RUC		0x0070	/* RECEIVE UNIT COMMANDS */
#define	I596_SCB_RUC_NOP	0x0000	/* No operation */
#define	I596_SCB_RUC_START	0x0010	/* Start reception */
#define	I596_SCB_RUC_RESUME	0x0020	/* Resume reception */
#define	I596_SCB_RUC_SUSPEND	0x0030	/* Suspend reception */
#define	I596_SCB_RUC_ABORT	0x0040	/* Abort reception */

#define	I596_SCB_RESET		0x0080	/* Hard reset chip */

	/* status halfword values */
#define	I596_SCB_STAT		0xF000	/* STATUS */
#define	I596_SCB_CX		0x8000	/* command completion */
#define	I596_SCB_FR		0x4000	/* received frame */
#define	I596_SCB_CNA		0x2000	/* command unit not active */
#define	I596_SCB_RNR		0x1000	/* rcv unit not ready */

#define	I596_SCB_CUS		0x0700	/* COMMAND UNIT STATUS */
#define	I596_SCB_CUS_IDLE	0x0000	/* Idle */
#define	I596_SCB_CUS_SUSPENDED	0x0100	/* Suspended */
#define	I596_SCB_CUS_ACTIVE	0x0200	/* Active */

#define	I596_SCB_RUS		0x00F0	/* RECEIVE UNIT STATUS */
#define	I596_SCB_RUS_IDLE	0x0000	/* Idle */
#define	I596_SCB_RUS_SUSPENDED	0x0010	/* Suspended */
#define	I596_SCB_RUS_NORES	0x0020	/* No Resources */
#define	I596_SCB_RUS_READY	0x0040	/* Ready */
#define	I596_SCB_RUS_NORBDS	0x0080	/* No more RBDs modifier */

#define	I596_SCB_LOADED		0x0008	/* Bus timers loaded */

/************************************************************************/
/*									*/
/*	I596_ISCP:	Intermediate System Configuration Ptr (p 4-26)	*/
/*									*/
/************************************************************************/
typedef volatile struct
{
	ulong		busy;	/* Set to 1; I596 clears it when scbp is read */
	I596_SCB	*scbp;
} I596_ISCP;

/************************************************************************/
/*									*/
/*	I596_SCP:	System Configuration Pointer (p. 4-23)		*/
/*									*/
/************************************************************************/
typedef volatile struct
{
	ulong		sysbus;	
	ulong		dummy;
	I596_ISCP	*iscpp;
} I596_SCP;

	/* .sysbus values */
#define I596_SCP_RESERVED	0x400000	/* Reserved bits must be set */
#define I596_SCP_INTLOW		0x200000	/* Intr. Polarity active low */
#define I596_SCP_INTHIGH	0		/* Intr. Polarity active high */
#define I596_SCP_LOCKDIS	0x100000	/* Lock Function disabled */
#define I596_SCP_LOCKEN		0		/* Lock Function enabled */
#define I596_SCP_ETHROTTLE	0x080000	/* External Bus Throttle */
#define I596_SCP_ITHROTTLE	0		/* Internal Bus Throttle */
#define I596_SCP_LINEAR		0x040000	/* Linear Mode */
#define I596_SCP_SEGMENTED	0x020000	/* Segmented Mode */
#define I596_SCP_82586		0x000000	/* 82586 Mode */
