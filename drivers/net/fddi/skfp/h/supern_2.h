/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
	defines for AMD Supernet II chip set
	the chips are referred to as
		FPLUS	Formac Plus
		PLC	Physical Layer

	added defines for AMD Supernet III chip set
	added comments on differences between Supernet II and Supernet III
	added defines for the Motorola ELM (MOT_ELM)
*/

#ifndef	_SUPERNET_
#define _SUPERNET_

/*
 * Define Supernet 3 when used
 */
#ifdef	PCI
#ifndef	SUPERNET_3
#define	SUPERNET_3
#endif
#define TAG
#endif

#define	MB	0xff
#define	MW	0xffff
#define	MD	0xffffffff

/*
 * FORMAC frame status (rx_msext)
 */
#define	FS_EI		(1<<2)
#define	FS_AI		(1<<1)
#define	FS_CI		(1<<0)

#define FS_MSVALID	(1<<15)		/* end of queue */
#define FS_MSRABT	(1<<14)		/* frame was aborted during reception*/
#define FS_SSRCRTG	(1<<12)		/* if SA has set MSB (source-routing)*/
#define FS_SEAC2	(FS_EI<<9)	/* error indicator */
#define FS_SEAC1	(FS_AI<<9)	/* address indicator */
#define FS_SEAC0	(FS_CI<<9)	/* copy indicator */
#define FS_SFRMERR	(1<<8)		/* error detected (CRC or length) */
#define FS_SADRRG	(1<<7)		/* address recognized */
#define FS_SFRMTY2	(1<<6)		/* frame-class bit */
#define FS_SFRMTY1	(1<<5)		/* frame-type bit (impementor) */
#define FS_SFRMTY0	(1<<4)		/* frame-type bit (LLC) */
#define FS_ERFBB1	(1<<1)		/* byte offset (depends on LSB bit) */
#define FS_ERFBB0	(1<<0)		/*  - " - */

/*
 * status frame type
 */
#define	FRM_SMT		(0)	/* asynchr. frames */
#define	FRM_LLCA	(1)
#define	FRM_IMPA	(2)	
#define	FRM_MAC		(4)	/* synchr. frames */
#define	FRM_LLCS	(5)
#define	FRM_IMPS	(6)

/*
 * bits in rx_descr.i	(receive frame status word)
 */
#define RX_MSVALID	((long)1<<31)	/* memory status valid */
#define RX_MSRABT	((long)1<<30)	/* memory status receive abort */
#define RX_FS_E		((long)FS_SEAC2<<16)	/* error indicator */
#define RX_FS_A		((long)FS_SEAC1<<16)	/* address indicator */
#define RX_FS_C		((long)FS_SEAC0<<16)	/* copy indicator */
#define RX_FS_CRC	((long)FS_SFRMERR<<16)/* error detected */
#define RX_FS_ADDRESS	((long)FS_SADRRG<<16)	/* address recognized */
#define RX_FS_MAC	((long)FS_SFRMTY2<<16)/* MAC frame */
#define RX_FS_SMT	((long)0<<16)		/* SMT frame */
#define RX_FS_IMPL	((long)FS_SFRMTY1<<16)/* implementer frame */
#define RX_FS_LLC	((long)FS_SFRMTY0<<16)/* LLC frame */

/*
 * receive frame descriptor
 */
union rx_descr {
	struct {
#ifdef	LITTLE_ENDIAN
	unsigned int	rx_length :16 ;	/* frame length lower/upper byte */
	unsigned int	rx_erfbb  :2 ;	/* received frame byte boundary */
	unsigned int	rx_reserv2:2 ;	/* reserved */
	unsigned int	rx_sfrmty :3 ;	/* frame type bits */
	unsigned int	rx_sadrrg :1 ;	/* DA == MA or broad-/multicast */
	unsigned int	rx_sfrmerr:1 ;	/* received frame not valid */
	unsigned int	rx_seac0  :1 ;	/* frame-copied  C-indicator */
	unsigned int	rx_seac1  :1 ;	/* address-match A-indicator */
	unsigned int	rx_seac2  :1 ;	/* frame-error   E-indicator */
	unsigned int	rx_ssrcrtg:1 ;	/* == 1 SA has MSB set */
	unsigned int	rx_reserv1:1 ;	/* reserved */
	unsigned int	rx_msrabt :1 ;	/* memory status receive abort */
	unsigned int	rx_msvalid:1 ;	/* memory status valid */
#else
	unsigned int	rx_msvalid:1 ;	/* memory status valid */
	unsigned int	rx_msrabt :1 ;	/* memory status receive abort */
	unsigned int	rx_reserv1:1 ;	/* reserved */
	unsigned int	rx_ssrcrtg:1 ;	/* == 1 SA has MSB set */
	unsigned int	rx_seac2  :1 ;	/* frame-error   E-indicator */
	unsigned int	rx_seac1  :1 ;	/* address-match A-indicator */
	unsigned int	rx_seac0  :1 ;	/* frame-copied  C-indicator */
	unsigned int	rx_sfrmerr:1 ;	/* received frame not valid */
	unsigned int	rx_sadrrg :1 ;	/* DA == MA or broad-/multicast */
	unsigned int	rx_sfrmty :3 ;	/* frame type bits */
	unsigned int	rx_erfbb  :2 ;	/* received frame byte boundary */
	unsigned int	rx_reserv2:2 ;	/* reserved */
	unsigned int	rx_length :16 ;	/* frame length lower/upper byte */
#endif
	} r ;
	long	i ;
} ;

/* defines for Receive Frame Descriptor access */
#define RD_S_ERFBB	0x00030000L	/* received frame byte boundary */
#define RD_S_RES2	0x000c0000L	/* reserved */
#define RD_S_SFRMTY	0x00700000L	/* frame type bits */
#define RD_S_SADRRG	0x00800000L	/* DA == MA or broad-/multicast */
#define RD_S_SFRMERR	0x01000000L	/* received frame not valid */
#define	RD_S_SEAC	0x0e000000L	/* frame status indicators */
#define RD_S_SEAC0	0x02000000L	/* frame-copied  case-indicator */
#define RD_S_SEAC1	0x04000000L	/* address-match A-indicator */
#define RD_S_SEAC2	0x08000000L	/* frame-error   E-indicator */
#define RD_S_SSRCRTG	0x10000000L	/* == 1 SA has MSB set */
#define RD_S_RES1	0x20000000L	/* reserved */
#define RD_S_MSRABT	0x40000000L	/* memory status receive abort */
#define RD_S_MSVALID	0x80000000L	/* memory status valid */

#define	RD_STATUS	0xffff0000L
#define	RD_LENGTH	0x0000ffffL

/* defines for Receive Frames Status Word values */
/*RD_S_SFRMTY*/
#define RD_FRM_SMT	(unsigned long)(0<<20)     /* asynchr. frames */
#define RD_FRM_LLCA	(unsigned long)(1<<20)
#define RD_FRM_IMPA	(unsigned long)(2<<20)
#define RD_FRM_MAC	(unsigned long)(4<<20)     /* synchr. frames */
#define RD_FRM_LLCS	(unsigned long)(5<<20)
#define RD_FRM_IMPS	(unsigned long)(6<<20)

#define TX_DESCRIPTOR	0x40000000L
#define TX_OFFSET_3	0x18000000L

#define TXP1	2

/*
 * transmit frame descriptor
 */
union tx_descr {
	struct {
#ifdef	LITTLE_ENDIAN
	unsigned int	tx_length:16 ;	/* frame length lower/upper byte */
	unsigned int	tx_res	 :8 ;	/* reserved 	 (bit 16..23) */
	unsigned int	tx_xmtabt:1 ;	/* transmit abort */
	unsigned int	tx_nfcs  :1 ;	/* no frame check sequence */
	unsigned int	tx_xdone :1 ;	/* give up token */
	unsigned int	tx_rpxm  :2 ;	/* byte offset */
	unsigned int	tx_pat1  :2 ;	/* must be TXP1 */
	unsigned int	tx_more	 :1 ;	/* more frame in chain */
#else
	unsigned int	tx_more	 :1 ;	/* more frame in chain */
	unsigned int	tx_pat1  :2 ;	/* must be TXP1 */
	unsigned int	tx_rpxm  :2 ;	/* byte offset */
	unsigned int	tx_xdone :1 ;	/* give up token */
	unsigned int	tx_nfcs  :1 ;	/* no frame check sequence */
	unsigned int	tx_xmtabt:1 ;	/* transmit abort */
	unsigned int	tx_res	 :8 ;	/* reserved 	 (bit 16..23) */
	unsigned int	tx_length:16 ;	/* frame length lower/upper byte */
#endif
	} t ;
	long	i ;
} ;

/* defines for Transmit Descriptor access */
#define	TD_C_MORE	0x80000000L	/* more frame in chain */
#define	TD_C_DESCR	0x60000000L	/* must be TXP1 */
#define	TD_C_TXFBB	0x18000000L	/* byte offset */
#define	TD_C_XDONE	0x04000000L	/* give up token */
#define TD_C_NFCS	0x02000000L	/* no frame check sequence */
#define TD_C_XMTABT	0x01000000L	/* transmit abort */

#define	TD_C_LNCNU	0x0000ff00L	
#define TD_C_LNCNL	0x000000ffL
#define TD_C_LNCN	0x0000ffffL	/* frame length lower/upper byte */
 
/*
 * transmit pointer
 */
union tx_pointer {
	struct t {
#ifdef	LITTLE_ENDIAN
	unsigned int	tp_pointer:16 ;	/* pointer to tx_descr (low/high) */
	unsigned int	tp_res	  :8 ;	/* reserved 	 (bit 16..23) */
	unsigned int	tp_pattern:8 ;	/* fixed pattern (bit 24..31) */
#else
	unsigned int	tp_pattern:8 ;	/* fixed pattern (bit 24..31) */
	unsigned int	tp_res	  :8 ;	/* reserved 	 (bit 16..23) */
	unsigned int	tp_pointer:16 ;	/* pointer to tx_descr (low/high) */
#endif
	} t ;
	long	i ;
} ;

/* defines for Nontag Mode Pointer access */
#define	TD_P_CNTRL	0xff000000L
#define TD_P_RPXU	0x0000ff00L
#define TD_P_RPXL	0x000000ffL
#define TD_P_RPX	0x0000ffffL


#define TX_PATTERN	0xa0
#define TX_POINTER_END	0xa0000000L
#define TX_INT_PATTERN	0xa0000000L

struct tx_queue {
	struct tx_queue *tq_next ;
	u_short tq_pack_offset ;	/* offset buffer memory */
	u_char  tq_pad[2] ;
} ;

/*
	defines for FORMAC Plus (Am79C830)
*/

/*
 *  FORMAC+ read/write (r/w) registers
 */
#define FM_CMDREG1	0x00		/* write command reg 1 instruction */
#define FM_CMDREG2	0x01		/* write command reg 2 instruction */
#define FM_ST1U		0x00		/* read upper 16-bit of status reg 1 */
#define FM_ST1L		0x01		/* read lower 16-bit of status reg 1 */
#define FM_ST2U		0x02		/* read upper 16-bit of status reg 2 */
#define FM_ST2L		0x03		/* read lower 16-bit of status reg 2 */
#define FM_IMSK1U	0x04		/* r/w upper 16-bit of IMSK 1 */
#define FM_IMSK1L	0x05		/* r/w lower 16-bit of IMSK 1 */
#define FM_IMSK2U	0x06		/* r/w upper 16-bit of IMSK 2 */
#define FM_IMSK2L	0x07		/* r/w lower 16-bit of IMSK 2 */
#define FM_SAID		0x08		/* r/w short addr.-individual */
#define FM_LAIM		0x09		/* r/w long addr.-ind. (MSW of LAID) */
#define FM_LAIC		0x0a		/* r/w long addr.-ind. (middle)*/
#define FM_LAIL		0x0b		/* r/w long addr.-ind. (LSW) */
#define FM_SAGP		0x0c		/* r/w short address-group */
#define FM_LAGM		0x0d		/* r/w long addr.-gr. (MSW of LAGP) */
#define FM_LAGC		0x0e		/* r/w long addr.-gr. (middle) */
#define FM_LAGL		0x0f		/* r/w long addr.-gr. (LSW) */
#define FM_MDREG1	0x10		/* r/w 16-bit mode reg 1 */
#define FM_STMCHN	0x11		/* read state-machine reg */
#define FM_MIR1		0x12		/* read upper 16-bit of MAC Info Reg */
#define FM_MIR0		0x13		/* read lower 16-bit of MAC Info Reg */
#define FM_TMAX		0x14		/* r/w 16-bit TMAX reg */
#define FM_TVX		0x15		/* write 8-bit TVX reg with NP7-0
					   read TVX on NP7-0, timer on NP15-8*/
#define FM_TRT		0x16		/* r/w upper 16-bit of TRT timer */
#define FM_THT		0x17		/* r/w upper 16-bit of THT timer */
#define FM_TNEG		0x18		/* read upper 16-bit of TNEG (TTRT) */
#define FM_TMRS		0x19		/* read lower 5-bit of TNEG,TRT,THT */
			/* F E D C  B A 9 8  7 6 5 4  3 2 1 0
			   x |-TNEG4-0| |-TRT4-0-| |-THT4-0-| (x-late count) */
#define FM_TREQ0	0x1a		/* r/w 16-bit TREQ0 reg (LSW of TRT) */
#define FM_TREQ1	0x1b		/* r/w 16-bit TREQ1 reg (MSW of TRT) */
#define FM_PRI0		0x1c		/* r/w priority r. for asyn.-queue 0 */
#define FM_PRI1		0x1d		/* r/w priority r. for asyn.-queue 1 */
#define FM_PRI2		0x1e		/* r/w priority r. for asyn.-queue 2 */
#define FM_TSYNC	0x1f		/* r/w 16-bit of the TSYNC register */
#define FM_MDREG2	0x20		/* r/w 16-bit mode reg 2 */
#define FM_FRMTHR	0x21		/* r/w the frame threshold register */
#define FM_EACB		0x22		/* r/w end addr of claim/beacon area */
#define FM_EARV		0x23		/* r/w end addr of receive queue */
/* Supernet 3 */
#define	FM_EARV1	FM_EARV

#define FM_EAS		0x24		/* r/w end addr of synchr. queue */
#define FM_EAA0		0x25		/* r/w end addr of asyn. queue 0 */
#define FM_EAA1		0x26		/* r/w end addr of asyn. queue 1 */
#define FM_EAA2		0x27		/* r/w end addr of asyn. queue 2 */
#define FM_SACL		0x28		/* r/w start addr of claim frame */
#define FM_SABC		0x29		/* r/w start addr of beacon frame */
#define FM_WPXSF	0x2a		/* r/w the write ptr. for special fr.*/
#define FM_RPXSF	0x2b		/* r/w the read ptr. for special fr. */
#define FM_RPR		0x2d		/* r/w the read ptr. for receive qu. */
#define FM_WPR		0x2e		/* r/w the write ptr. for receive qu.*/
#define FM_SWPR		0x2f		/* r/w the shadow wr.-ptr. for rec.q.*/
/* Supernet 3 */ 
#define FM_RPR1         FM_RPR   
#define FM_WPR1         FM_WPR 
#define FM_SWPR1        FM_SWPR

#define FM_WPXS		0x30		/* r/w the write ptr. for synchr. qu.*/
#define FM_WPXA0	0x31		/* r/w the write ptr. for asyn. qu.0 */
#define FM_WPXA1	0x32		/* r/w the write ptr. for asyn. qu.1 */
#define FM_WPXA2	0x33		/* r/w the write ptr. for asyn. qu.2 */
#define FM_SWPXS	0x34		/* r/w the shadow wr.-ptr. for syn.q.*/
#define FM_SWPXA0	0x35		/* r/w the shad. wr.-ptr. for asyn.q0*/
#define FM_SWPXA1	0x36		/* r/w the shad. wr.-ptr. for asyn.q1*/
#define FM_SWPXA2	0x37		/* r/w the shad. wr.-ptr. for asyn.q2*/
#define FM_RPXS		0x38		/* r/w the read ptr. for synchr. qu. */
#define FM_RPXA0	0x39		/* r/w the read ptr. for asyn. qu. 0 */
#define FM_RPXA1	0x3a		/* r/w the read ptr. for asyn. qu. 1 */
#define FM_RPXA2	0x3b		/* r/w the read ptr. for asyn. qu. 2 */
#define FM_MARR		0x3c		/* r/w the memory read addr register */
#define FM_MARW		0x3d		/* r/w the memory write addr register*/
#define FM_MDRU		0x3e		/* r/w upper 16-bit of mem. data reg */
#define FM_MDRL		0x3f		/* r/w lower 16-bit of mem. data reg */

/* following instructions relate to MAC counters and timer */
#define FM_TMSYNC	0x40		/* r/w upper 16 bits of TMSYNC timer */
#define FM_FCNTR	0x41		/* r/w the 16-bit frame counter */
#define FM_LCNTR	0x42		/* r/w the 16-bit lost counter */
#define FM_ECNTR	0x43		/* r/w the 16-bit error counter */

/* Supernet 3:	extensions to old register block */
#define	FM_FSCNTR	0x44		/* r/? Frame Strip Counter */
#define	FM_FRSELREG	0x45		/* r/w Frame Selection Register */

/* Supernet 3:	extensions for 2. receive queue etc. */
#define	FM_MDREG3	0x60		/* r/w Mode Register 3 */
#define	FM_ST3U		0x61		/* read upper 16-bit of status reg 3 */
#define	FM_ST3L		0x62		/* read lower 16-bit of status reg 3 */
#define	FM_IMSK3U	0x63		/* r/w upper 16-bit of IMSK reg 3 */
#define	FM_IMSK3L	0x64		/* r/w lower 16-bit of IMSK reg 3 */
#define	FM_IVR		0x65		/* read Interrupt Vector register */
#define	FM_IMR		0x66		/* r/w Interrupt mask register */
/* 0x67	Hidden */
#define	FM_RPR2		0x68		/* r/w the read ptr. for rec. qu. 2 */
#define	FM_WPR2		0x69		/* r/w the write ptr. for rec. qu. 2 */
#define	FM_SWPR2	0x6a		/* r/w the shadow wptr. for rec. q. 2 */
#define	FM_EARV2	0x6b		/* r/w end addr of rec. qu. 2 */
#define	FM_UNLCKDLY	0x6c		/* r/w Auto Unlock Delay register */
					/* Bit 15-8: RECV2 unlock threshold */
					/* Bit  7-0: RECV1 unlock threshold */
/* 0x6f-0x73	Hidden */
#define	FM_LTDPA1	0x79		/* r/w Last Trans desc ptr for A1 qu. */
/* 0x80-0x9a	PLCS registers of built-in PLCS  (Supernet 3 only) */

/* Supernet 3: Adderss Filter Registers */
#define	FM_AFCMD	0xb0		/* r/w Address Filter Command Reg */
#define	FM_AFSTAT	0xb2		/* r/w Address Filter Status Reg */
#define	FM_AFBIST	0xb4		/* r/w Address Filter BIST signature */
#define	FM_AFCOMP2	0xb6		/* r/w Address Filter Comparand 2 */
#define	FM_AFCOMP1	0xb8		/* r/w Address Filter Comparand 1 */
#define	FM_AFCOMP0	0xba		/* r/w Address Filter Comparand 0 */
#define	FM_AFMASK2	0xbc		/* r/w Address Filter Mask 2 */
#define	FM_AFMASK1	0xbe		/* r/w Address Filter Mask 1 */
#define	FM_AFMASK0	0xc0		/* r/w Address Filter Mask 0 */
#define	FM_AFPERS	0xc2		/* r/w Address Filter Personality Reg */

/* Supernet 3: Orion (PDX?) Registers */
#define	FM_ORBIST	0xd0		/* r/w Orion BIST signature */
#define	FM_ORSTAT	0xd2		/* r/w Orion Status Register */


/*
 * Mode Register 1 (MDREG1)
 */
#define FM_RES0		0x0001		/* reserved */
					/* SN3: other definition */
#define	FM_XMTINH_HOLD	0x0002		/* transmit-inhibit/hold bit */
					/* SN3: other definition */
#define	FM_HOFLXI	0x0003		/* SN3: Hold / Flush / Inhibit */
#define	FM_FULL_HALF	0x0004		/* full-duplex/half-duplex bit */
#define	FM_LOCKTX	0x0008		/* lock-transmit-asynchr.-queues bit */
#define FM_EXGPA0	0x0010		/* extended-group-addressing bit 0 */
#define FM_EXGPA1	0x0020		/* extended-group-addressing bit 1 */
#define FM_DISCRY	0x0040		/* disable-carry bit */
					/* SN3: reserved */
#define FM_SELRA	0x0080		/* select input from PHY (1=RA,0=RB) */

#define FM_ADDET	0x0700		/* address detection */
#define FM_MDAMA	(0<<8)		/* address detection : DA = MA */
#define FM_MDASAMA	(1<<8)		/* address detection : DA=MA||SA=MA */
#define	FM_MRNNSAFNMA	(2<<8)		/* rec. non-NSA frames DA=MA&&SA!=MA */
#define	FM_MRNNSAF	(3<<8)		/* rec. non-NSA frames DA = MA */
#define	FM_MDISRCV	(4<<8)		/* disable receive function */
#define	FM_MRES0	(5<<8)		/* reserve */
#define	FM_MLIMPROM	(6<<8)		/* limited-promiscuous mode */
#define FM_MPROMISCOUS	(7<<8)		/* address detection : promiscuous */

#define FM_SELSA	0x0800		/* select-short-address bit */

#define FM_MMODE	0x7000		/* mode select */
#define FM_MINIT	(0<<12)		/* initialize */
#define FM_MMEMACT	(1<<12)		/* memory activate */
#define FM_MONLINESP	(2<<12)		/* on-line special */
#define FM_MONLINE	(3<<12)		/* on-line (FDDI operational mode) */
#define FM_MILOOP	(4<<12)		/* internal loopback */
#define FM_MRES1	(5<<12)		/* reserved */
#define FM_MRES2	(6<<12)		/* reserved */
#define FM_MELOOP	(7<<12)		/* external loopback */

#define	FM_SNGLFRM	0x8000		/* single-frame-receive mode */
					/* SN3: reserved */

#define	MDR1INIT	(FM_MINIT | FM_MDAMA)

/*
 * Mode Register 2 (MDREG2)
 */
#define	FM_AFULL	0x000f		/* 4-bit value (empty loc.in txqueue)*/
#define	FM_RCVERR	0x0010		/* rec.-errored-frames bit */
#define	FM_SYMCTL	0x0020		/* sysmbol-control bit */
					/* SN3: reserved */
#define	FM_SYNPRQ	0x0040		/* synchron.-NP-DMA-request bit */
#define	FM_ENNPRQ	0x0080		/* enable-NP-DMA-request bit */
#define	FM_ENHSRQ	0x0100		/* enable-host-request bit */
#define	FM_RXFBB01	0x0600		/* rec. frame byte boundary bit0 & 1 */
#define	FM_LSB		0x0800		/* determ. ordering of bytes in buffer*/
#define	FM_PARITY	0x1000		/* 1 = even, 0 = odd */
#define	FM_CHKPAR	0x2000		/* 1 = parity of 32-bit buffer BD-bus*/
#define	FM_STRPFCS	0x4000		/* 1 = strips FCS field of rec.frame */
#define	FM_BMMODE	0x8000		/* Buffer-Memory-Mode (1 = tag mode) */
					/* SN3: 1 = tag, 0 = modified tag */

/*
 * Status Register 1, Upper 16 Bits (ST1U)
 */
#define FM_STEFRMS	0x0001		/* transmit end of frame: synchr. qu.*/
#define FM_STEFRMA0	0x0002		/* transmit end of frame: asyn. qu.0 */
#define FM_STEFRMA1	0x0004		/* transmit end of frame: asyn. qu.1 */
#define FM_STEFRMA2	0x0008		/* transmit end of frame: asyn. qu.2 */
					/* SN3: reserved */
#define FM_STECFRMS	0x0010		/* transmit end of chain of syn. qu. */
					/* SN3: reserved */
#define FM_STECFRMA0	0x0020		/* transmit end of chain of asyn. q0 */
					/* SN3: reserved */
#define FM_STECFRMA1	0x0040		/* transmit end of chain of asyn. q1 */
					/* SN3: STECMDA1 */
#define FM_STECMDA1	0x0040		/* SN3: 'no description' */
#define FM_STECFRMA2	0x0080		/* transmit end of chain of asyn. q2 */
					/* SN3: reserved */
#define	FM_STEXDONS	0x0100		/* transmit until XDONE in syn. qu. */
#define	FM_STBFLA	0x0200		/* asynchr.-queue trans. buffer full */
#define	FM_STBFLS	0x0400		/* synchr.-queue transm. buffer full */
#define	FM_STXABRS	0x0800		/* synchr. queue transmit-abort */
#define	FM_STXABRA0	0x1000		/* asynchr. queue 0 transmit-abort */
#define	FM_STXABRA1	0x2000		/* asynchr. queue 1 transmit-abort */
#define	FM_STXABRA2	0x4000		/* asynchr. queue 2 transmit-abort */
					/* SN3: reserved */
#define	FM_SXMTABT	0x8000		/* transmit abort */

/*
 * Status Register 1, Lower 16 Bits (ST1L)
 */
#define FM_SQLCKS	0x0001		/* queue lock for synchr. queue */
#define FM_SQLCKA0	0x0002		/* queue lock for asynchr. queue 0 */
#define FM_SQLCKA1	0x0004		/* queue lock for asynchr. queue 1 */
#define FM_SQLCKA2	0x0008		/* queue lock for asynchr. queue 2 */
					/* SN3: reserved */
#define FM_STXINFLS	0x0010		/* transmit instruction full: syn. */
					/* SN3: reserved */
#define FM_STXINFLA0	0x0020		/* transmit instruction full: asyn.0 */
					/* SN3: reserved */
#define FM_STXINFLA1	0x0040		/* transmit instruction full: asyn.1 */
					/* SN3: reserved */
#define FM_STXINFLA2	0x0080		/* transmit instruction full: asyn.2 */
					/* SN3: reserved */
#define FM_SPCEPDS	0x0100		/* parity/coding error: syn. queue */
#define FM_SPCEPDA0	0x0200		/* parity/coding error: asyn. queue0 */
#define FM_SPCEPDA1	0x0400		/* parity/coding error: asyn. queue1 */
#define FM_SPCEPDA2	0x0800		/* parity/coding error: asyn. queue2 */
					/* SN3: reserved */
#define FM_STBURS	0x1000		/* transmit buffer underrun: syn. q. */
#define FM_STBURA0	0x2000		/* transmit buffer underrun: asyn.0 */
#define FM_STBURA1	0x4000		/* transmit buffer underrun: asyn.1 */
#define FM_STBURA2	0x8000		/* transmit buffer underrun: asyn.2 */
					/* SN3: reserved */

/*
 * Status Register 2, Upper 16 Bits (ST2U)
 */
#define FM_SOTRBEC	0x0001		/* other beacon received */
#define FM_SMYBEC	0x0002		/* my beacon received */
#define FM_SBEC		0x0004		/* beacon state entered */
#define FM_SLOCLM	0x0008		/* low claim received */
#define FM_SHICLM	0x0010		/* high claim received */
#define FM_SMYCLM	0x0020		/* my claim received */
#define FM_SCLM		0x0040		/* claim state entered */
#define FM_SERRSF	0x0080		/* error in special frame */
#define FM_SNFSLD	0x0100		/* NP and FORMAC+ simultaneous load */
#define FM_SRFRCTOV	0x0200		/* receive frame counter overflow */
					/* SN3: reserved */
#define FM_SRCVFRM	0x0400		/* receive frame */
					/* SN3: reserved */
#define FM_SRCVOVR	0x0800		/* receive FIFO overflow */
#define FM_SRBFL	0x1000		/* receive buffer full */
#define FM_SRABT	0x2000		/* receive abort */
#define FM_SRBMT	0x4000		/* receive buffer empty */
#define FM_SRCOMP	0x8000		/* receive complete. Nontag mode */

/*
 * Status Register 2, Lower 16 Bits (ST2L)
 * Attention: SN3 docu shows these bits the other way around
 */
#define FM_SRES0	0x0001		/* reserved */
#define FM_SESTRIPTK	0x0001		/* SN3: 'no description' */
#define FM_STRTEXR	0x0002		/* TRT expired in claim | beacon st. */
#define FM_SDUPCLM	0x0004		/* duplicate claim received */
#define FM_SSIFG	0x0008		/* short interframe gap */
#define FM_SFRMCTR	0x0010		/* frame counter overflow */
#define FM_SERRCTR	0x0020		/* error counter overflow */
#define FM_SLSTCTR	0x0040		/* lost counter overflow */
#define FM_SPHINV	0x0080		/* PHY invalid */
#define FM_SADET	0x0100		/* address detect */
#define FM_SMISFRM	0x0200		/* missed frame */
#define FM_STRTEXP	0x0400		/* TRT expired and late count > 0 */
#define FM_STVXEXP	0x0800		/* TVX expired */
#define FM_STKISS	0x1000		/* token issued */
#define FM_STKERR	0x2000		/* token error */
#define FM_SMULTDA	0x4000		/* multiple destination address */
#define FM_SRNGOP	0x8000		/* ring operational */

/*
 * Supernet 3:
 * Status Register 3, Upper 16 Bits (ST3U)
 */
#define	FM_SRQUNLCK1	0x0001		/* receive queue unlocked queue 1 */
#define	FM_SRQUNLCK2	0x0002		/* receive queue unlocked queue 2 */
#define	FM_SRPERRQ1	0x0004		/* receive parity error rx queue 1 */
#define	FM_SRPERRQ2	0x0008		/* receive parity error rx queue 2 */
					/* Bit 4-10: reserved */
#define	FM_SRCVOVR2	0x0800		/* receive FIFO overfull rx queue 2 */
#define	FM_SRBFL2	0x1000		/* receive buffer full rx queue 2 */
#define	FM_SRABT2	0x2000		/* receive abort rx queue 2 */
#define	FM_SRBMT2	0x4000		/* receive buf empty rx queue 2 */
#define	FM_SRCOMP2	0x8000		/* receive comp rx queue 2 */

/*
 * Supernet 3:
 * Status Register 3, Lower 16 Bits (ST3L)
 */
#define	FM_AF_BIST_DONE		0x0001	/* Address Filter BIST is done */
#define	FM_PLC_BIST_DONE	0x0002	/* internal PLC Bist is done */
#define	FM_PDX_BIST_DONE	0x0004	/* PDX BIST is done */
					/* Bit  3: reserved */
#define	FM_SICAMDAMAT		0x0010	/* Status internal CAM DA match */
#define	FM_SICAMDAXACT		0x0020	/* Status internal CAM DA exact match */
#define	FM_SICAMSAMAT		0x0040	/* Status internal CAM SA match */
#define	FM_SICAMSAXACT		0x0080	/* Status internal CAM SA exact match */

/*
 * MAC State-Machine Register FM_STMCHN
 */
#define	FM_MDRTAG	0x0004		/* tag bit of long word read */
#define	FM_SNPPND	0x0008		/* r/w from buffer mem. is pending */
#define	FM_TXSTAT	0x0070		/* transmitter state machine state */
#define	FM_RCSTAT	0x0380		/* receiver state machine state */
#define	FM_TM01		0x0c00		/* indicate token mode */
#define	FM_SIM		0x1000		/* indicate send immediate-mode */
#define	FM_REV		0xe000		/* FORMAC Plus revision number */

/*
 * Supernet 3
 * Mode Register 3
 */
#define	FM_MENRS	0x0001		/* Ena enhanced rec status encoding */
#define	FM_MENXS	0x0002		/* Ena enhanced xmit status encoding */
#define	FM_MENXCT	0x0004		/* Ena EXACT/INEXACT matching */
#define	FM_MENAFULL	0x0008		/* Ena enh QCTRL encoding for AFULL */
#define	FM_MEIND	0x0030		/* Ena enh A,C indicator settings */
#define	FM_MENQCTRL	0x0040		/* Ena enh QCTRL encoding */
#define	FM_MENRQAUNLCK	0x0080		/* Ena rec q auto unlock */
#define	FM_MENDAS	0x0100		/* Ena DAS connections by cntr MUX */
#define	FM_MENPLCCST	0x0200		/* Ena Counter Segm test in PLC blck */
#define	FM_MENSGLINT	0x0400		/* Ena Vectored Interrupt reading */
#define	FM_MENDRCV	0x0800		/* Ena dual receive queue operation */
#define	FM_MENFCLOC	0x3000		/* Ena FC location within frm data */
#define	FM_MENTRCMD	0x4000		/* Ena ASYNC1 xmit only after command */
#define	FM_MENTDLPBK	0x8000		/* Ena TDAT to RDAT lkoopback */

/*
 * Supernet 3
 * Frame Selection Register
 */
#define	FM_RECV1	0x000f		/* options for receive queue 1 */
#define	FM_RCV1_ALL	(0<<0)		/* receive all frames */
#define	FM_RCV1_LLC	(1<<0)		/* rec all LLC frames */
#define	FM_RCV1_SMT	(2<<0)		/* rec all SMT frames */
#define	FM_RCV1_NSMT	(3<<0)		/* rec non-SMT frames */
#define	FM_RCV1_IMP	(4<<0)		/* rec Implementor frames */
#define	FM_RCV1_MAC	(5<<0)		/* rec all MAC frames */
#define	FM_RCV1_SLLC	(6<<0)		/* rec all sync LLC frames */
#define	FM_RCV1_ALLC	(7<<0)		/* rec all async LLC frames */
#define	FM_RCV1_VOID	(8<<0)		/* rec all void frames */
#define	FM_RCV1_ALSMT	(9<<0)		/* rec all async LLC & SMT frames */
#define	FM_RECV2	0x00f0		/* options for receive queue 2 */
#define	FM_RCV2_ALL	(0<<4)		/* receive all other frames */
#define	FM_RCV2_LLC	(1<<4)		/* rec all LLC frames */
#define	FM_RCV2_SMT	(2<<4)		/* rec all SMT frames */
#define	FM_RCV2_NSMT	(3<<4)		/* rec non-SMT frames */
#define	FM_RCV2_IMP	(4<<4)		/* rec Implementor frames */
#define	FM_RCV2_MAC	(5<<4)		/* rec all MAC frames */
#define	FM_RCV2_SLLC	(6<<4)		/* rec all sync LLC frames */
#define	FM_RCV2_ALLC	(7<<4)		/* rec all async LLC frames */
#define	FM_RCV2_VOID	(8<<4)		/* rec all void frames */
#define	FM_RCV2_ALSMT	(9<<4)		/* rec all async LLC & SMT frames */
#define	FM_ENXMTADSWAP	0x4000		/* enh rec addr swap (phys -> can) */
#define	FM_ENRCVADSWAP	0x8000		/* enh tx addr swap (can -> phys) */

/*
 * Supernet 3:
 * Address Filter Command Register (AFCMD)
 */
#define	FM_INST		0x0007		/* Address Filter Operation */
#define FM_IINV_CAM	(0<<0)		/* Invalidate CAM */
#define FM_IWRITE_CAM	(1<<0)		/* Write CAM */
#define FM_IREAD_CAM	(2<<0)		/* Read CAM */
#define FM_IRUN_BIST	(3<<0)		/* Run BIST */
#define FM_IFIND	(4<<0)		/* Find */
#define FM_IINV		(5<<0)		/* Invalidate */
#define FM_ISKIP	(6<<0)		/* Skip */
#define FM_ICL_SKIP	(7<<0)		/* Clear all SKIP bits */

/*
 * Supernet 3:
 * Address Filter Status Register (AFSTAT)
 */
					/* Bit  0-4: reserved */
#define	FM_REV_NO	0x00e0		/* Revision Number of Address Filter */
#define	FM_BIST_DONE	0x0100		/* BIST complete */
#define	FM_EMPTY	0x0200		/* CAM empty */
#define	FM_ERROR	0x0400		/* Error (improper operation) */
#define	FM_MULT		0x0800		/* Multiple Match */
#define	FM_EXACT	0x1000		/* Exact Match */
#define	FM_FOUND	0x2000		/* Comparand found in CAM */
#define	FM_FULL		0x4000		/* CAM full */
#define	FM_DONE		0x8000		/* DONE indicator */

/*
 * Supernet 3:
 * BIST Signature Register (AFBIST)
 */
#define	AF_BIST_SIGNAT	0x0553		/* Address Filter BIST Signature */

/*
 * Supernet 3:
 * Personality Register (AFPERS)
 */
#define	FM_VALID	0x0001		/* CAM Entry Valid */
#define	FM_DA		0x0002		/* Destination Address */
#define	FM_DAX		0x0004		/* Destination Address Exact */
#define	FM_SA		0x0008		/* Source Address */
#define	FM_SAX		0x0010		/* Source Address Exact */
#define	FM_SKIP		0x0020		/* Skip this entry */

/*
 * instruction set for command register 1 (NPADDR6-0 = 0x00)
 */
#define FM_IRESET	0x01		/* software reset */
#define FM_IRMEMWI	0x02		/* load Memory Data Reg., inc MARR */
#define FM_IRMEMWO	0x03		/* load MDR from buffer memory, n.i. */
#define FM_IIL		0x04		/* idle/listen */
#define FM_ICL		0x05		/* claim/listen */
#define FM_IBL		0x06		/* beacon/listen */
#define FM_ILTVX	0x07		/* load TVX timer from TVX reg */
#define FM_INRTM	0x08		/* nonrestricted token mode */
#define FM_IENTM	0x09		/* enter nonrestricted token mode */
#define FM_IERTM	0x0a		/* enter restricted token mode */
#define FM_IRTM		0x0b		/* restricted token mode */
#define FM_ISURT	0x0c		/* send unrestricted token */
#define FM_ISRT		0x0d		/* send restricted token */
#define FM_ISIM		0x0e		/* enter send-immediate mode */
#define FM_IESIM	0x0f		/* exit send-immediate mode */
#define FM_ICLLS	0x11		/* clear synchronous queue lock */
#define FM_ICLLA0	0x12		/* clear asynchronous queue 0 lock */
#define FM_ICLLA1	0x14		/* clear asynchronous queue 1 lock */
#define FM_ICLLA2	0x18		/* clear asynchronous queue 2 lock */
					/* SN3: reserved */
#define FM_ICLLR	0x20		/* clear receive queue (SN3:1) lock */
#define FM_ICLLR2	0x21		/* SN3: clear receive queue 2 lock */
#define FM_ITRXBUS	0x22		/* SN3: Tristate X-Bus (SAS only) */
#define FM_IDRXBUS	0x23		/* SN3: drive X-Bus */
#define FM_ICLLAL	0x3f		/* clear all queue locks */

/*
 * instruction set for command register 2 (NPADDR6-0 = 0x01)
 */
#define FM_ITRS		0x01		/* transmit synchronous queue */
					/* SN3: reserved */
#define FM_ITRA0	0x02		/* transmit asynchronous queue 0 */
					/* SN3: reserved */
#define FM_ITRA1	0x04		/* transmit asynchronous queue 1 */
					/* SN3: reserved */
#define FM_ITRA2	0x08		/* transmit asynchronous queue 2 */
					/* SN3: reserved */
#define FM_IACTR	0x10		/* abort current transmit activity */
#define FM_IRSTQ	0x20		/* reset transmit queues */
#define FM_ISTTB	0x30		/* set tag bit */
#define FM_IERSF	0x40		/* enable receive single frame */
					/* SN3: reserved */
#define	FM_ITR		0x50		/* SN3: Transmit Command */


/*
 *	defines for PLC (Am79C864)
 */

/*
 *  PLC read/write (r/w) registers
 */
#define PL_CNTRL_A	0x00		/* control register A (r/w) */
#define PL_CNTRL_B	0x01		/* control register B (r/w) */
#define PL_INTR_MASK	0x02		/* interrupt mask (r/w) */
#define PL_XMIT_VECTOR	0x03		/* transmit vector register (r/w) */
#define PL_VECTOR_LEN	0x04		/* transmit vector length (r/w) */
#define PL_LE_THRESHOLD	0x05		/* link error event threshold (r/w) */
#define PL_C_MIN	0x06		/* minimum connect state time (r/w) */
#define PL_TL_MIN	0x07		/* min. line state transmit t. (r/w) */
#define PL_TB_MIN	0x08		/* minimum break time (r/w) */
#define PL_T_OUT	0x09		/* signal timeout (r/w) */
#define PL_CNTRL_C	0x0a		/* control register C (r/w) */
#define PL_LC_LENGTH	0x0b		/* link confidence test time (r/w) */
#define PL_T_SCRUB	0x0c		/* scrub time = MAC TVX (r/w) */
#define PL_NS_MAX	0x0d		/* max. noise time before break (r/w)*/
#define PL_TPC_LOAD_V	0x0e		/* TPC timer load value (write only) */
#define PL_TNE_LOAD_V	0x0f		/* TNE timer load value (write only) */
#define PL_STATUS_A	0x10		/* status register A (read only) */
#define PL_STATUS_B	0x11		/* status register B (read only) */
#define PL_TPC		0x12		/* timer for PCM (ro) [20.48 us] */
#define PL_TNE		0x13		/* time of noise event [0.32 us] */
#define PL_CLK_DIV	0x14		/* TNE clock divider (read only) */
#define PL_BIST_SIGNAT	0x15		/* built in self test signature (ro)*/
#define PL_RCV_VECTOR	0x16		/* receive vector reg. (read only) */
#define PL_INTR_EVENT	0x17		/* interrupt event reg. (read only) */
#define PL_VIOL_SYM_CTR	0x18		/* violation symbol count. (read o) */
#define PL_MIN_IDLE_CTR	0x19		/* minimum idle counter (read only) */
#define PL_LINK_ERR_CTR	0x1a		/* link error event ctr.(read only) */
#ifdef	MOT_ELM
#define	PL_T_FOT_ASS	0x1e		/* FOTOFF Assert Timer */
#define	PL_T_FOT_DEASS	0x1f		/* FOTOFF Deassert Timer */
#endif	/* MOT_ELM */

#ifdef	MOT_ELM
/*
 * Special Quad-Elm Registers.
 * A Quad-ELM consists of for ELMs and these additional registers.
 */
#define	QELM_XBAR_W	0x80		/* Crossbar Control ELM W */
#define	QELM_XBAR_X	0x81		/* Crossbar Control ELM X */
#define	QELM_XBAR_Y	0x82		/* Crossbar Control ELM Y */
#define	QELM_XBAR_Z	0x83		/* Crossbar Control ELM Z */
#define	QELM_XBAR_P	0x84		/* Crossbar Control Bus P */
#define	QELM_XBAR_S	0x85		/* Crossbar Control Bus S */
#define	QELM_XBAR_R	0x86		/* Crossbar Control Bus R */
#define	QELM_WR_XBAR	0x87		/* Write the Crossbar now (write) */
#define	QELM_CTR_W	0x88		/* Counter W */
#define	QELM_CTR_X	0x89		/* Counter X */
#define	QELM_CTR_Y	0x8a		/* Counter Y */
#define	QELM_CTR_Z	0x8b		/* Counter Z */
#define	QELM_INT_MASK	0x8c		/* Interrupt mask register */
#define	QELM_INT_DATA	0x8d		/* Interrupt data (event) register */
#define	QELM_ELMB	0x00		/* Elm base */
#define	QELM_ELM_SIZE	0x20		/* ELM size */
#endif	/* MOT_ELM */
/*
 * PLC control register A (PL_CNTRL_A: log. addr. 0x00)
 * It is used for timer configuration, specification of PCM MAINT state option,
 * counter interrupt frequency, PLC data path config. and Built In Self Test.
 */
#define	PL_RUN_BIST	0x0001		/* begin running its Built In Self T.*/
#define	PL_RF_DISABLE	0x0002		/* disable the Repeat Filter state m.*/
#define	PL_SC_REM_LOOP	0x0004		/* remote loopback path */
#define	PL_SC_BYPASS	0x0008		/* by providing a physical bypass */
#define	PL_LM_LOC_LOOP	0x0010		/* loop path just after elastic buff.*/
#define	PL_EB_LOC_LOOP	0x0020		/* loop path just prior to PDT/PDR IF*/
#define	PL_FOT_OFF	0x0040		/* assertion of /FOTOFF pin of PLC */
#define	PL_LOOPBACK	0x0080		/* it cause the /LPBCK pin ass. low */
#define	PL_MINI_CTR_INT 0x0100		/* partially contr. when bit is ass. */
#define	PL_VSYM_CTR_INT	0x0200		/* controls when int bit is asserted */
#define	PL_ENA_PAR_CHK	0x0400		/* enable parity check */
#define	PL_REQ_SCRUB	0x0800		/* limited access to scrub capability*/
#define	PL_TPC_16BIT	0x1000		/* causes the TPC as a 16 bit timer */
#define	PL_TNE_16BIT	0x2000		/* causes the TNE as a 16 bit timer */
#define	PL_NOISE_TIMER	0x4000		/* allows the noise timing function */

/*
 * PLC control register B (PL_CNTRL_B: log. addr. 0x01)
 * It contains signals and requeste to direct the process of PCM and it is also
 * used to control the Line State Match interrupt.
 */
#define	PL_PCM_CNTRL	0x0003		/* control PCM state machine */
#define	PL_PCM_NAF	(0)		/* state is not affected */
#define	PL_PCM_START	(1)		/* goes to the BREAK state */
#define	PL_PCM_TRACE	(2)		/* goes to the TRACE state */
#define	PL_PCM_STOP	(3)		/* goes to the OFF state */

#define	PL_MAINT	0x0004		/* if OFF state --> MAINT state */
#define	PL_LONG		0x0008		/* perf. a long Link Confid.Test(LCT)*/
#define	PL_PC_JOIN	0x0010		/* if NEXT state --> JOIN state */

#define	PL_PC_LOOP	0x0060		/* loopback used in the LCT */
#define	PL_NOLCT	(0<<5)		/* no LCT is performed */
#define	PL_TPDR		(1<<5)		/* PCM asserts transmit PDR */
#define	PL_TIDLE	(2<<5)		/* PCM asserts transmit idle */
#define	PL_RLBP		(3<<5)		/* trans. PDR & remote loopb. path */

#define	PL_CLASS_S	0x0080		/* signif. that single att. station */

#define	PL_MAINT_LS	0x0700		/* line state while in the MAINT st. */
#define	PL_M_QUI0	(0<<8)		/* transmit QUIET line state */
#define	PL_M_IDLE	(1<<8)		/* transmit IDLE line state */
#define	PL_M_HALT	(2<<8)		/* transmit HALT line state */
#define	PL_M_MASTR	(3<<8)		/* transmit MASTER line state */
#define	PL_M_QUI1	(4<<8)		/* transmit QUIET line state */
#define	PL_M_QUI2	(5<<8)		/* transmit QUIET line state */
#define	PL_M_TPDR	(6<<8)		/* tr. PHY_DATA requ.-symbol is tr.ed*/
#define	PL_M_QUI3	(7<<8)		/* transmit QUIET line state */

#define	PL_MATCH_LS	0x7800		/* line state to be comp. with curr.*/
#define	PL_I_ANY	(0<<11)		/* Int. on any change in *_LINE_ST */
#define	PL_I_IDLE	(1<<11)		/* Interrupt on IDLE line state */
#define	PL_I_HALT	(2<<11)		/* Interrupt on HALT line state */
#define	PL_I_MASTR	(4<<11)		/* Interrupt on MASTER line state */
#define	PL_I_QUIET	(8<<11)		/* Interrupt on QUIET line state */

#define	PL_CONFIG_CNTRL	0x8000		/* control over scrub, byp. & loopb.*/

/*
 * PLC control register C (PL_CNTRL_C: log. addr. 0x0a)
 * It contains the scrambling control registers (PLC-S only)
 */
#define PL_C_CIPHER_ENABLE	(1<<0)	/* enable scrambler */
#define PL_C_CIPHER_LPBCK	(1<<1)	/* loopback scrambler */
#define PL_C_SDOFF_ENABLE	(1<<6)	/* enable SDOFF timer */
#define PL_C_SDON_ENABLE	(1<<7)	/* enable SDON timer */
#ifdef	MOT_ELM
#define PL_C_FOTOFF_CTRL	(3<<2)	/* FOTOFF timer control */
#define PL_C_FOTOFF_TIM		(0<<2)	/* FOTOFF use timer for (de)-assert */
#define PL_C_FOTOFF_INA		(2<<2)	/* FOTOFF forced inactive */
#define PL_C_FOTOFF_ACT		(3<<2)	/* FOTOFF forced active */
#define PL_C_FOTOFF_SRCE	(1<<4)	/* FOTOFF source is PCM state != OFF */
#define	PL_C_RXDATA_EN		(1<<5)	/* Rec scr data forced to 0 */
#define	PL_C_SDNRZEN		(1<<8)	/* Monitor rec descr. data for act */
#else	/* nMOT_ELM */
#define PL_C_FOTOFF_CTRL	(3<<8)	/* FOTOFF timer control */
#define PL_C_FOTOFF_0		(0<<8)	/* timer off */
#define PL_C_FOTOFF_30		(1<<8)	/* 30uS */
#define PL_C_FOTOFF_50		(2<<8)	/* 50uS */
#define PL_C_FOTOFF_NEVER	(3<<8)	/* never */
#define PL_C_SDON_TIMER		(3<<10)	/* SDON timer control */
#define PL_C_SDON_084		(0<<10)	/* 0.84 uS */
#define PL_C_SDON_132		(1<<10)	/* 1.32 uS */
#define PL_C_SDON_252		(2<<10)	/* 2.52 uS */
#define PL_C_SDON_512		(3<<10)	/* 5.12 uS */
#define PL_C_SOFF_TIMER		(3<<12)	/* SDOFF timer control */
#define PL_C_SOFF_076		(0<<12)	/* 0.76 uS */
#define PL_C_SOFF_132		(1<<12)	/* 1.32 uS */
#define PL_C_SOFF_252		(2<<12)	/* 2.52 uS */
#define PL_C_SOFF_512		(3<<12)	/* 5.12 uS */
#define PL_C_TSEL		(3<<14)	/* scrambler path select */
#endif	/* nMOT_ELM */

/*
 * PLC status register A (PL_STATUS_A: log. addr. 0x10)
 * It is used to report status information to the Node Processor about the 
 * Line State Machine (LSM).
 */
#ifdef	MOT_ELM
#define PLC_INT_MASK	0xc000		/* ELM integration bits in status A */
#define PLC_INT_C	0x0000		/* ELM Revision Band C */
#define PLC_INT_CAMEL	0x4000		/* ELM integrated into CAMEL */
#define PLC_INT_QE	0x8000		/* ELM integrated into Quad ELM */
#define PLC_REV_MASK	0x3800		/* revision bits in status A */
#define PLC_REVISION_B	0x0000		/* rev bits for ELM Rev B */
#define PLC_REVISION_QA	0x0800		/* rev bits for ELM core in QELM-A */
#else	/* nMOT_ELM */
#define PLC_REV_MASK	0xf800		/* revision bits in status A */
#define PLC_REVISION_A	0x0000		/* revision bits for PLC */
#define PLC_REVISION_S	0xf800		/* revision bits for PLC-S */
#define PLC_REV_SN3	0x7800		/* revision bits for PLC-S in IFCP */
#endif	/* nMOT_ELM */
#define	PL_SYM_PR_CTR	0x0007		/* contains the LSM symbol pair Ctr. */
#define	PL_UNKN_LINE_ST	0x0008		/* unknown line state bit from LSM */
#define	PL_LSM_STATE	0x0010		/* state bit of LSM */

#define	PL_LINE_ST	0x00e0		/* contains recogn. line state of LSM*/
#define	PL_L_NLS	(0<<5)		/* noise line state */
#define	PL_L_ALS	(1<<5)		/* activ line state */
#define	PL_L_UND	(2<<5)		/* undefined */
#define	PL_L_ILS4	(3<<5)		/* idle l. s. (after 4 idle symbols) */
#define	PL_L_QLS	(4<<5)		/* quiet line state */
#define	PL_L_MLS	(5<<5)		/* master line state */
#define	PL_L_HLS	(6<<5)		/* halt line state */
#define	PL_L_ILS16	(7<<5)		/* idle line state (after 16 idle s.)*/

#define	PL_PREV_LINE_ST	0x0300		/* value of previous line state */
#define	PL_P_QLS	(0<<8)		/* quiet line state */
#define	PL_P_MLS	(1<<8)		/* master line state */
#define	PL_P_HLS	(2<<8)		/* halt line state */
#define	PL_P_ILS16	(3<<8)		/* idle line state (after 16 idle s.)*/

#define	PL_SIGNAL_DET	0x0400		/* 1=that signal detect is deasserted*/


/*
 * PLC status register B (PL_STATUS_B: log. addr. 0x11)
 * It contains signals and status from the repeat filter and PCM state machine.
 */
#define	PL_BREAK_REASON	0x0007		/* reason for PCM state mach.s to br.*/
#define	PL_B_NOT	(0)		/* PCM SM has not gone to BREAK state*/
#define	PL_B_PCS	(1)		/* PC_Start issued */
#define	PL_B_TPC	(2)		/* TPC timer expired after T_OUT */
#define	PL_B_TNE	(3)		/* TNE timer expired after NS_MAX */
#define	PL_B_QLS	(4)		/* quit line state detected */
#define	PL_B_ILS	(5)		/* idle line state detected */
#define	PL_B_HLS	(6)		/* halt line state detected */

#define	PL_TCF		0x0008		/* transmit code flag (start exec.) */
#define	PL_RCF		0x0010		/* receive code flag (start exec.) */
#define	PL_LSF		0x0020		/* line state flag (l.s. has been r.)*/
#define	PL_PCM_SIGNAL	0x0040		/* indic. that XMIT_VECTOR hb.written*/

#define	PL_PCM_STATE	0x0780		/* state bits of PCM state machine */
#define	PL_PC0		(0<<7)		/* OFF	   - when /RST or PCM_CNTRL */
#define	PL_PC1		(1<<7)		/* BREAK   - entry point in start PCM*/
#define	PL_PC2		(2<<7)		/* TRACE   - to localize stuck Beacon*/
#define	PL_PC3		(3<<7)		/* CONNECT - synchronize ends of conn*/
#define	PL_PC4		(4<<7)		/* NEXT	   - to separate the signalng*/
#define	PL_PC5		(5<<7)		/* SIGNAL  - PCM trans/rec. bit infos*/
#define	PL_PC6		(6<<7)		/* JOIN	   - 1. state to activ conn. */
#define	PL_PC7		(7<<7)		/* VERIFY  - 2. - " - (3. ACTIVE) */
#define	PL_PC8		(8<<7)		/* ACTIVE  - PHY has been incorporated*/
#define	PL_PC9		(9<<7)		/* MAINT   - for test purposes or so 
					   that PCM op. completely in softw. */

#define	PL_PCI_SCRUB	0x0800		/* scrubbing function is being exec. */

#define	PL_PCI_STATE	0x3000		/* Physical Connect. Insertion SM */
#define	PL_CI_REMV	(0<<12)		/* REMOVED */
#define	PL_CI_ISCR	(1<<12)		/* INSERT_SCRUB */
#define	PL_CI_RSCR	(2<<12)		/* REMOVE_SCRUB */
#define	PL_CI_INS	(3<<12)		/* INSERTED */

#define	PL_RF_STATE	0xc000		/* state bit of repeate filter SM */
#define	PL_RF_REPT	(0<<14)		/* REPEAT */
#define	PL_RF_IDLE	(1<<14)		/* IDLE */
#define	PL_RF_HALT1	(2<<14)		/* HALT1 */
#define	PL_RF_HALT2	(3<<14)		/* HALT2 */


/*
 * PLC interrupt event register (PL_INTR_EVENT: log. addr. 0x17)
 * It is read only and is clearde whenever it is read!
 * It is used by the PLC to report events to the node processor.
 */
#define	PL_PARITY_ERR	0x0001		/* p. error h.b.detected on TX9-0 inp*/
#define	PL_LS_MATCH	0x0002		/* l.s.== l.s. PLC_CNTRL_B's MATCH_LS*/
#define	PL_PCM_CODE	0x0004		/* transmit&receive | LCT complete */
#define	PL_TRACE_PROP	0x0008		/* master l.s. while PCM ACTIV|TRACE */
#define	PL_SELF_TEST	0x0010		/* QUIET|HALT while PCM in TRACE st. */
#define	PL_PCM_BREAK	0x0020		/* PCM has entered the BREAK state */
#define	PL_PCM_ENABLED	0x0040		/* asserted SC_JOIN, scrub. & ACTIV */
#define	PL_TPC_EXPIRED	0x0080		/* TPC timer reached zero */
#define	PL_TNE_EXPIRED	0x0100		/* TNE timer reached zero */
#define	PL_EBUF_ERR	0x0200		/* elastic buff. det. over-|underflow*/
#define	PL_PHYINV	0x0400		/* physical layer invalid signal */
#define	PL_VSYM_CTR	0x0800		/* violation symbol counter has incr.*/
#define	PL_MINI_CTR	0x1000		/* dep. on PLC_CNTRL_A's MINI_CTR_INT*/
#define	PL_LE_CTR	0x2000		/* link error event counter */
#define	PL_LSDO		0x4000		/* SDO input pin changed to a 1 */
#define	PL_NP_ERR	0x8000		/* NP has requested to r/w an inv. r.*/

/*
 * The PLC interrupt mask register (PL_INTR_MASK: log. addr. 0x02) constr. is
 * equal PL_INTR_EVENT register.
 * For each set bit, the setting of corresponding bit generate an int to NP. 
 */

#ifdef	MOT_ELM
/*
 * Quad ELM Crosbar Control register values (QELM_XBAR_?)
 */
#define	QELM_XOUT_IDLE	0x0000		/* Idles/Passthrough */
#define	QELM_XOUT_P	0x0001		/* Output to: Bus P */
#define	QELM_XOUT_S	0x0002		/* Output to: Bus S */
#define	QELM_XOUT_R	0x0003		/* Output to: Bus R */
#define	QELM_XOUT_W	0x0004		/* Output to: ELM W */
#define	QELM_XOUT_X	0x0005		/* Output to: ELM X */
#define	QELM_XOUT_Y	0x0006		/* Output to: ELM Y */
#define	QELM_XOUT_Z	0x0007		/* Output to: ELM Z */

/*
 * Quad ELM Interrupt data and event registers.
 */
#define	QELM_NP_ERR	(1<<15)		/* Node Processor Error */
#define	QELM_COUNT_Z	(1<<7)		/* Counter Z Interrupt */
#define	QELM_COUNT_Y	(1<<6)		/* Counter Y Interrupt */
#define	QELM_COUNT_X	(1<<5)		/* Counter X Interrupt */
#define	QELM_COUNT_W	(1<<4)		/* Counter W Interrupt */
#define	QELM_ELM_Z	(1<<3)		/* ELM Z Interrupt */
#define	QELM_ELM_Y	(1<<2)		/* ELM Y Interrupt */
#define	QELM_ELM_X	(1<<1)		/* ELM X Interrupt */
#define	QELM_ELM_W	(1<<0)		/* ELM W Interrupt */
#endif	/* MOT_ELM */
/*
 * PLC Timing Parameters
 */
#define	TP_C_MIN	0xff9c	/*   2    ms */
#define	TP_TL_MIN	0xfff0	/*   0.3  ms */
#define	TP_TB_MIN	0xff10	/*   5    ms */
#define	TP_T_OUT	0xd9db	/* 200    ms */
#define	TP_LC_LENGTH	0xf676	/*  50    ms */
#define	TP_LC_LONGLN	0xa0a2	/* 500    ms */
#define	TP_T_SCRUB	0xff6d	/*   3.5  ms */
#define	TP_NS_MAX	0xf021	/*   1.3   ms */

/*
 * BIST values
 */
#define PLC_BIST	0x6ecd		/* BIST signature for PLC */
#define PLCS_BIST	0x5b6b 		/* BIST signature for PLC-S */
#define	PLC_ELM_B_BIST	0x6ecd		/* BIST signature of ELM Rev. B */
#define	PLC_ELM_D_BIST	0x5b6b		/* BIST signature of ELM Rev. D */
#define	PLC_CAM_A_BIST	0x9e75		/* BIST signature of CAMEL Rev. A */
#define	PLC_CAM_B_BIST	0x5b6b		/* BIST signature of CAMEL Rev. B */
#define	PLC_IFD_A_BIST	0x9e75		/* BIST signature of IFDDI Rev. A */
#define	PLC_IFD_B_BIST	0x5b6b		/* BIST signature of IFDDI Rev. B */
#define	PLC_QELM_A_BIST	0x5b6b		/* BIST signature of QELM Rev. A */

/*
 	FDDI board recources	
 */

/*
 * request register array (log. addr: RQA_A + a<<1 {a=0..7}) write only.
 * It specifies to FORMAC+ the type of buffer memory access the host requires.
 */
#define	RQ_NOT		0		/* not request */
#define	RQ_RES		1		/* reserved */
#define	RQ_SFW		2		/* special frame write */
#define	RQ_RRQ		3		/* read request: receive queue */
#define	RQ_WSQ		4		/* write request: synchronous queue */
#define	RQ_WA0		5		/* write requ.: asynchronous queue 0 */
#define	RQ_WA1		6		/* write requ.: asynchronous queue 1 */
#define	RQ_WA2		7		/* write requ.: asynchronous queue 2 */

#define	SZ_LONG		(sizeof(long))

/*
 * FDDI defaults
 * NOTE : In the ANSI docs, times are specified in units of "symbol time".
 * 	  AMD chips use BCLK as unit. 1 BCKL == 2 symbols
 */
#define	COMPLREF	((u_long)32*256*256)	/* two's complement 21 bit */
#define MSTOBCLK(x)	((u_long)(x)*12500L)
#define MSTOTVX(x)	(((u_long)(x)*1000L)/80/255)

#endif	/* _SUPERNET_ */
