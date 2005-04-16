/*
 * Intel 82586 IEEE 802.3 Ethernet LAN Coprocessor.
 *
 * See:
 *	Intel Microcommunications 1991
 *	p1-1 to p1-37
 *	Intel order No. 231658
 *	ISBN 1-55512-119-5
 *
 *     Unfortunately, the above chapter mentions neither
 * the System Configuration Pointer (SCP) nor the
 * Intermediate System Configuration Pointer (ISCP),
 * so we probably need to look elsewhere for the
 * whole story -- some recommend the "Intel LAN
 * Components manual" but I have neither a copy
 * nor a full reference.  But "elsewhere" may be
 * in the same publication...
 *     The description of a later device, the
 * "82596CA High-Performance 32-Bit Local Area Network
 * Coprocessor", (ibid. p1-38 to p1-109) does mention
 * the SCP and ISCP and also has an i82586 compatibility
 * mode.  Even more useful is "AP-235 An 82586 Data Link
 * Driver" (ibid. p1-337 to p1-417).
 */

#define	I82586_MEMZ	(64 * 1024)

#define	I82586_SCP_ADDR	(I82586_MEMZ - sizeof(scp_t))

#define	ADDR_LEN	6
#define	I82586NULL	0xFFFF

#define	toff(t,p,f) 	(unsigned short)((void *)(&((t *)((void *)0 + (p)))->f) - (void *)0)

/*
 * System Configuration Pointer (SCP).
 */
typedef struct scp_t	scp_t;
struct scp_t
{
	unsigned short	scp_sysbus;	/* 82586 bus width:	*/
#define		SCP_SY_16BBUS	(0x0 << 0)	/* 16 bits */
#define		SCP_SY_8BBUS	(0x1 << 0)	/*  8 bits. */
	unsigned short	scp_junk[2];	/* Unused */
	unsigned short	scp_iscpl;	/* lower 16 bits of ISCP_ADDR */
	unsigned short	scp_iscph;	/* upper 16 bits of ISCP_ADDR */
};

/*
 * Intermediate System Configuration Pointer (ISCP).
 */
typedef struct iscp_t	iscp_t;
struct iscp_t
{
	unsigned short	iscp_busy;	/* set by CPU before first CA,	*/
					/* cleared by 82586 after read.	*/
	unsigned short	iscp_offset;	/* offset of SCB		*/
	unsigned short	iscp_basel;	/* base of SCB			*/
	unsigned short	iscp_baseh;	/*  "				*/
};

/*
 * System Control Block (SCB).
 *	The 82586 writes its status to scb_status and then
 *	raises an interrupt to alert the CPU.
 *	The CPU writes a command to scb_command and
 *	then issues a Channel Attention (CA) to alert the 82586.
 */
typedef struct scb_t	scb_t;
struct scb_t
{
	unsigned short	scb_status;	/* Status of 82586		*/
#define		SCB_ST_INT	(0xF << 12)	/* Some of:		*/
#define		SCB_ST_CX	(0x1 << 15)	/* Cmd completed	*/
#define		SCB_ST_FR	(0x1 << 14)	/* Frame received	*/
#define		SCB_ST_CNA	(0x1 << 13)	/* Cmd unit not active	*/
#define		SCB_ST_RNR	(0x1 << 12)	/* Rcv unit not ready	*/
#define		SCB_ST_JUNK0	(0x1 << 11)	/* 0			*/
#define		SCB_ST_CUS	(0x7 <<  8)	/* Cmd unit status	*/
#define			SCB_ST_CUS_IDLE	(0 << 8)	/* Idle		*/
#define			SCB_ST_CUS_SUSP	(1 << 8)	/* Suspended	*/
#define			SCB_ST_CUS_ACTV	(2 << 8)	/* Active	*/
#define		SCB_ST_JUNK1	(0x1 <<  7)	/* 0			*/
#define		SCB_ST_RUS	(0x7 <<  4)	/* Rcv unit status	*/
#define			SCB_ST_RUS_IDLE	(0 << 4)	/* Idle		*/
#define			SCB_ST_RUS_SUSP	(1 << 4)	/* Suspended	*/
#define			SCB_ST_RUS_NRES	(2 << 4)	/* No resources	*/
#define			SCB_ST_RUS_RDY	(4 << 4)	/* Ready	*/
	unsigned short	scb_command;	/* Next command			*/
#define		SCB_CMD_ACK_CX	(0x1 << 15)	/* Ack cmd completion	*/
#define		SCB_CMD_ACK_FR	(0x1 << 14)	/* Ack frame received	*/
#define		SCB_CMD_ACK_CNA	(0x1 << 13)	/* Ack CU not active	*/
#define		SCB_CMD_ACK_RNR	(0x1 << 12)	/* Ack RU not ready	*/
#define		SCB_CMD_JUNKX	(0x1 << 11)	/* Unused		*/
#define		SCB_CMD_CUC	(0x7 <<  8)	/* Command Unit command	*/
#define			SCB_CMD_CUC_NOP	(0 << 8)	/* Nop		*/
#define			SCB_CMD_CUC_GO	(1 << 8)	/* Start cbl_offset */
#define			SCB_CMD_CUC_RES	(2 << 8)	/* Resume execution */
#define			SCB_CMD_CUC_SUS	(3 << 8)	/* Suspend   "	*/
#define			SCB_CMD_CUC_ABT	(4 << 8)	/* Abort     "	*/
#define		SCB_CMD_RESET	(0x1 <<  7)	/* Reset chip (hardware) */
#define		SCB_CMD_RUC	(0x7 <<  4)	/* Receive Unit command	*/
#define			SCB_CMD_RUC_NOP	(0 << 4)	/* Nop		*/
#define			SCB_CMD_RUC_GO	(1 << 4)	/* Start rfa_offset */
#define			SCB_CMD_RUC_RES	(2 << 4)	/* Resume reception */
#define			SCB_CMD_RUC_SUS	(3 << 4)	/* Suspend   "	*/
#define			SCB_CMD_RUC_ABT	(4 << 4)	/* Abort     "	*/
	unsigned short	scb_cbl_offset;	/* Offset of first command unit	*/
					/* Action Command		*/
	unsigned short	scb_rfa_offset;	/* Offset of first Receive	*/
					/* Frame Descriptor in the	*/
					/* Receive Frame Area		*/
	unsigned short	scb_crcerrs;	/* Properly aligned frames	*/
					/* received with a CRC error	*/
	unsigned short	scb_alnerrs;	/* Misaligned frames received	*/
					/* with a CRC error		*/
	unsigned short	scb_rscerrs;	/* Frames lost due to no space	*/
	unsigned short	scb_ovrnerrs;	/* Frames lost due to slow bus	*/
};

#define	scboff(p,f) 	toff(scb_t, p, f)

/*
 * The eight Action Commands.
 */
typedef enum acmd_e	acmd_e;
enum acmd_e
{
	acmd_nop	= 0,	/* Do nothing				*/
	acmd_ia_setup	= 1,	/* Load an (ethernet) address into the	*/
				/* 82586				*/
	acmd_configure	= 2,	/* Update the 82586 operating parameters */
	acmd_mc_setup	= 3,	/* Load a list of (ethernet) multicast	*/
				/* addresses into the 82586		*/
	acmd_transmit	= 4,	/* Transmit a frame			*/
	acmd_tdr	= 5,	/* Perform a Time Domain Reflectometer	*/
				/* test on the serial link		*/
	acmd_dump	= 6,	/* Copy 82586 registers to memory	*/
	acmd_diagnose	= 7,	/* Run an internal self test		*/
};

/*
 * Generic Action Command header.
 */
typedef struct ach_t	ach_t;
struct ach_t
{
	unsigned short	ac_status;		/* Command status:	*/
#define		AC_SFLD_C	(0x1 << 15)	/* Command completed	*/
#define		AC_SFLD_B	(0x1 << 14)	/* Busy executing	*/
#define		AC_SFLD_OK	(0x1 << 13)	/* Completed error free	*/
#define		AC_SFLD_A	(0x1 << 12)	/* Command aborted	*/
#define		AC_SFLD_FAIL	(0x1 << 11)	/* Selftest failed	*/
#define		AC_SFLD_S10	(0x1 << 10)	/* No carrier sense	*/
						/* during transmission	*/
#define		AC_SFLD_S9	(0x1 <<  9)	/* Tx unsuccessful:	*/
						/* (stopped) lost CTS	*/
#define		AC_SFLD_S8	(0x1 <<  8)	/* Tx unsuccessful:	*/
						/* (stopped) slow DMA	*/
#define		AC_SFLD_S7	(0x1 <<  7)	/* Tx deferred:		*/
						/* other link traffic	*/
#define		AC_SFLD_S6	(0x1 <<  6)	/* Heart Beat: collision */
						/* detect after last tx	*/
#define		AC_SFLD_S5	(0x1 <<  5)	/* Tx stopped:		*/
						/* excessive collisions	*/
#define		AC_SFLD_MAXCOL	(0xF <<  0)	/* Collision count  	*/
	unsigned short	ac_command;		/* Command specifier:	*/
#define		AC_CFLD_EL	(0x1 << 15)	/* End of command list	*/
#define		AC_CFLD_S	(0x1 << 14)	/* Suspend on completion */
#define		AC_CFLD_I	(0x1 << 13)	/* Interrupt on completion */
#define		AC_CFLD_CMD	(0x7 <<  0)	/* acmd_e		*/
	unsigned short	ac_link;		/* Next Action Command	*/
};

#define	acoff(p,f) 	toff(ach_t, p, f)

/*
 * The Nop Action Command.
 */
typedef struct ac_nop_t	ac_nop_t;
struct ac_nop_t
{
	ach_t	nop_h;
};

/*
 * The IA-Setup Action Command.
 */
typedef struct ac_ias_t	ac_ias_t;
struct ac_ias_t
{
	ach_t		ias_h;
	unsigned char	ias_addr[ADDR_LEN]; /* The (ethernet) address	*/
};

/*
 * The Configure Action Command.
 */
typedef struct ac_cfg_t	ac_cfg_t;
struct ac_cfg_t
{
	ach_t		cfg_h;
	unsigned char	cfg_byte_cnt;	/* Size foll data: 4-12	*/
#define	AC_CFG_BYTE_CNT(v)	(((v) & 0xF) << 0)
	unsigned char	cfg_fifolim;	/* FIFO threshold	*/
#define	AC_CFG_FIFOLIM(v)	(((v) & 0xF) << 0)
	unsigned char	cfg_byte8;
#define	AC_CFG_SAV_BF(v) 	(((v) & 0x1) << 7)	/* Save rxd bad frames	*/
#define	AC_CFG_SRDY(v) 		(((v) & 0x1) << 6)	/* SRDY/ARDY pin means	*/
							/* external sync.	*/
	unsigned char	cfg_byte9;
#define	AC_CFG_ELPBCK(v)	(((v) & 0x1) << 7)	/* External loopback	*/
#define	AC_CFG_ILPBCK(v)	(((v) & 0x1) << 6)	/* Internal loopback	*/
#define	AC_CFG_PRELEN(v)	(((v) & 0x3) << 4)	/* Preamble length	*/
#define		AC_CFG_PLEN_2		0		/*  2 bytes	*/
#define		AC_CFG_PLEN_4		1		/*  4 bytes	*/
#define		AC_CFG_PLEN_8		2		/*  8 bytes	*/
#define		AC_CFG_PLEN_16		3		/* 16 bytes	*/
#define	AC_CFG_ALOC(v)		(((v) & 0x1) << 3)	/* Addr/len data is	*/
							/* explicit in buffers	*/
#define	AC_CFG_ADDRLEN(v)	(((v) & 0x7) << 0)	/* Bytes per address	*/
	unsigned char	cfg_byte10;
#define	AC_CFG_BOFMET(v)	(((v) & 0x1) << 7)	/* Use alternate expo.	*/
							/* backoff method	*/
#define	AC_CFG_ACR(v)		(((v) & 0x7) << 4)	/* Accelerated cont. res. */
#define	AC_CFG_LINPRIO(v)	(((v) & 0x7) << 0)	/* Linear priority	*/
	unsigned char	cfg_ifs;	/* Interframe spacing		*/
	unsigned char	cfg_slotl;	/* Slot time (low byte)		*/
	unsigned char	cfg_byte13;
#define	AC_CFG_RETRYNUM(v)	(((v) & 0xF) << 4)	/* Max. collision retry	*/
#define	AC_CFG_SLTTMHI(v)	(((v) & 0x7) << 0)	/* Slot time (high bits) */
	unsigned char	cfg_byte14;
#define	AC_CFG_FLGPAD(v)	(((v) & 0x1) << 7)	/* Pad with HDLC flags	*/
#define	AC_CFG_BTSTF(v)		(((v) & 0x1) << 6)	/* Do HDLC bitstuffing	*/
#define	AC_CFG_CRC16(v)		(((v) & 0x1) << 5)	/* 16 bit CCITT CRC	*/
#define	AC_CFG_NCRC(v)		(((v) & 0x1) << 4)	/* Insert no CRC	*/
#define	AC_CFG_TNCRS(v)		(((v) & 0x1) << 3)	/* Tx even if no carrier */
#define	AC_CFG_MANCH(v)		(((v) & 0x1) << 2)	/* Manchester coding	*/
#define	AC_CFG_BCDIS(v)		(((v) & 0x1) << 1)	/* Disable broadcast	*/
#define	AC_CFG_PRM(v)		(((v) & 0x1) << 0)	/* Promiscuous mode	*/
	unsigned char	cfg_byte15;
#define	AC_CFG_ICDS(v)		(((v) & 0x1) << 7)	/* Internal collision	*/
							/* detect source	*/
#define	AC_CFG_CDTF(v)		(((v) & 0x7) << 4)	/* Collision detect	*/
							/* filter in bit times	*/
#define	AC_CFG_ICSS(v)		(((v) & 0x1) << 3)	/* Internal carrier	*/
							/* sense source		*/
#define	AC_CFG_CSTF(v)		(((v) & 0x7) << 0)	/* Carrier sense	*/
							/* filter in bit times	*/
	unsigned short	cfg_min_frm_len;
#define	AC_CFG_MNFRM(v)		(((v) & 0xFF) << 0)	/* Min. bytes/frame (<= 255) */
};

/*
 * The MC-Setup Action Command.
 */
typedef struct ac_mcs_t	ac_mcs_t;
struct ac_mcs_t
{
	ach_t		mcs_h;
	unsigned short	mcs_cnt;	/* No. of bytes of MC addresses	*/
#if 0
	unsigned char	mcs_data[ADDR_LEN]; /* The first MC address ..	*/
	...
#endif
};

#define I82586_MAX_MULTICAST_ADDRESSES	128	/* Hardware hashed filter */

/*
 * The Transmit Action Command.
 */
typedef struct ac_tx_t	ac_tx_t;
struct ac_tx_t
{
	ach_t		tx_h;
	unsigned short	tx_tbd_offset;	/* Address of list of buffers.	*/
#if	0
Linux packets are passed down with the destination MAC address
and length/type field already prepended to the data,
so we do not need to insert it.  Consistent with this
we must also set the AC_CFG_ALOC(..) flag during the
ac_cfg_t action command.
	unsigned char	tx_addr[ADDR_LEN]; /* The frame dest. address	*/
	unsigned short	tx_length;	/* The frame length		*/
#endif	/* 0 */
};

/*
 * The Time Domain Reflectometer Action Command.
 */
typedef struct ac_tdr_t	ac_tdr_t;
struct ac_tdr_t
{
	ach_t		tdr_h;
	unsigned short	tdr_result;	/* Result.	*/
#define		AC_TDR_LNK_OK	(0x1 << 15)	/* No link problem	*/
#define		AC_TDR_XCVR_PRB	(0x1 << 14)	/* Txcvr cable problem	*/
#define		AC_TDR_ET_OPN	(0x1 << 13)	/* Open on the link	*/
#define		AC_TDR_ET_SRT	(0x1 << 12)	/* Short on the link	*/
#define		AC_TDR_TIME	(0x7FF << 0)	/* Distance to problem	*/
						/* site	in transmit	*/
						/* clock cycles		*/
};

/*
 * The Dump Action Command.
 */
typedef struct ac_dmp_t	ac_dmp_t;
struct ac_dmp_t
{
	ach_t		dmp_h;
	unsigned short	dmp_offset;	/* Result.	*/
};

/*
 * Size of the result of the dump command.
 */
#define	DUMPBYTES	170

/*
 * The Diagnose Action Command.
 */
typedef struct ac_dgn_t	ac_dgn_t;
struct ac_dgn_t
{
	ach_t		dgn_h;
};

/*
 * Transmit Buffer Descriptor (TBD).
 */
typedef struct tbd_t	tbd_t;
struct tbd_t
{
	unsigned short	tbd_status;		/* Written by the CPU	*/
#define		TBD_STATUS_EOF	(0x1 << 15)	/* This TBD is the	*/
						/* last for this frame	*/
#define		TBD_STATUS_ACNT	(0x3FFF << 0)	/* Actual count of data	*/
						/* bytes in this buffer	*/
	unsigned short	tbd_next_bd_offset;	/* Next in list		*/
	unsigned short	tbd_bufl;		/* Buffer address (low)	*/
	unsigned short	tbd_bufh;		/*    "	     "	(high)	*/
};

/*
 * Receive Buffer Descriptor (RBD).
 */
typedef struct rbd_t	rbd_t;
struct rbd_t
{
	unsigned short	rbd_status;		/* Written by the 82586	*/
#define		RBD_STATUS_EOF	(0x1 << 15)	/* This RBD is the	*/
						/* last for this frame	*/
#define		RBD_STATUS_F	(0x1 << 14)	/* ACNT field is valid	*/
#define		RBD_STATUS_ACNT	(0x3FFF << 0)	/* Actual no. of data	*/
						/* bytes in this buffer	*/
	unsigned short	rbd_next_rbd_offset;	/* Next rbd in list	*/
	unsigned short	rbd_bufl;		/* Data pointer (low)	*/
	unsigned short	rbd_bufh;		/*  "	   "    (high)	*/
	unsigned short	rbd_el_size;		/* EL+Data buf. size	*/
#define		RBD_EL	(0x1 << 15)		/* This BD is the	*/
						/* last in the list	*/
#define		RBD_SIZE (0x3FFF << 0)		/* No. of bytes the	*/
						/* buffer can hold	*/
};

#define	rbdoff(p,f) 	toff(rbd_t, p, f)

/*
 * Frame Descriptor (FD).
 */
typedef struct fd_t	fd_t;
struct fd_t
{
	unsigned short	fd_status;		/* Written by the 82586	*/
#define		FD_STATUS_C	(0x1 << 15)	/* Completed storing frame */
#define		FD_STATUS_B	(0x1 << 14)	/* FD was consumed by RU */
#define		FD_STATUS_OK	(0x1 << 13)	/* Frame rxd successfully */
#define		FD_STATUS_S11	(0x1 << 11)	/* CRC error		*/
#define		FD_STATUS_S10	(0x1 << 10)	/* Alignment error	*/
#define		FD_STATUS_S9	(0x1 <<  9)	/* Ran out of resources	*/
#define		FD_STATUS_S8	(0x1 <<  8)	/* Rx DMA overrun	*/
#define		FD_STATUS_S7	(0x1 <<  7)	/* Frame too short	*/
#define		FD_STATUS_S6	(0x1 <<  6)	/* No EOF flag		*/
	unsigned short	fd_command;		/* Command		*/
#define		FD_COMMAND_EL	(0x1 << 15)	/* Last FD in list	*/
#define		FD_COMMAND_S	(0x1 << 14)	/* Suspend RU after rx	*/
	unsigned short	fd_link_offset;		/* Next FD		*/
	unsigned short	fd_rbd_offset;		/* First RBD (data)	*/
						/* Prepared by CPU,	*/
						/* updated by 82586	*/
#if	0
I think the rest is unused since we
have set AC_CFG_ALOC(..).  However, just
in case, we leave the space.
#endif	/* 0 */
	unsigned char	fd_dest[ADDR_LEN];	/* Destination address	*/
						/* Written by 82586	*/
	unsigned char	fd_src[ADDR_LEN];	/* Source address	*/
						/* Written by 82586	*/
	unsigned short	fd_length;		/* Frame length or type	*/
						/* Written by 82586	*/
};

#define	fdoff(p,f) 	toff(fd_t, p, f)

/*
 * This software may only be used and distributed
 * according to the terms of the GNU General Public License.
 *
 * For more details, see wavelan.c.
 */
