/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skfddi.c" for further information.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * FORMAC+ Driver for tag mode
 */

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/supern_2.h"
#include <linux/bitrev.h>

#ifndef	lint
static const char ID_sccs[] = "@(#)fplustm.c	1.32 99/02/23 (C) SK " ;
#endif

#ifndef UNUSED
#ifdef  lint
#define UNUSED(x)	(x) = (x)
#else
#define UNUSED(x)
#endif
#endif

#define FM_ADDRX	 (FM_ADDET|FM_EXGPA0|FM_EXGPA1)
#define MS2BCLK(x)	((x)*12500L)
#define US2BCLK(x)	((x)*1250L)

/*
 * prototypes for static function
 */
static void build_claim_beacon(struct s_smc *smc, u_long t_request);
static int init_mac(struct s_smc *smc, int all);
static void rtm_init(struct s_smc *smc);
static void smt_split_up_fifo(struct s_smc *smc);

#if (!defined(NO_SMT_PANIC) || defined(DEBUG))
static	char write_mdr_warning [] = "E350 write_mdr() FM_SNPPND is set\n";
static	char cam_warning [] = "E_SMT_004: CAM still busy\n";
#endif

#define	DUMMY_READ()	smc->hw.mc_dummy = (u_short) inp(ADDR(B0_RAP))

#define	CHECK_NPP() {	unsigned k = 10000 ;\
			while ((inpw(FM_A(FM_STMCHN)) & FM_SNPPND) && k) k--;\
			if (!k) { \
				SMT_PANIC(smc,SMT_E0130, SMT_E0130_MSG) ; \
			}	\
		}

#define	CHECK_CAM() {	unsigned k = 10 ;\
			while (!(inpw(FM_A(FM_AFSTAT)) & FM_DONE) && k) k--;\
			if (!k) { \
				SMT_PANIC(smc,SMT_E0131, SMT_E0131_MSG) ; \
			}	\
		}

const struct fddi_addr fddi_broadcast = {{0xff,0xff,0xff,0xff,0xff,0xff}};
static const struct fddi_addr null_addr = {{0,0,0,0,0,0}};
static const struct fddi_addr dbeacon_multi = {{0x01,0x80,0xc2,0x00,0x01,0x00}};

static const u_short my_said = 0xffff ;	/* short address (n.u.) */
static const u_short my_sagp = 0xffff ;	/* short group address (n.u.) */

/*
 * define my address
 */
#ifdef	USE_CAN_ADDR
#define MA	smc->hw.fddi_canon_addr
#else
#define MA	smc->hw.fddi_home_addr
#endif


/*
 * useful interrupt bits
 */
static const int mac_imsk1u = FM_STXABRS | FM_STXABRA0 | FM_SXMTABT ;
static const int mac_imsk1l = FM_SQLCKS | FM_SQLCKA0 | FM_SPCEPDS | FM_SPCEPDA0|
			FM_STBURS | FM_STBURA0 ;

	/* delete FM_SRBFL after tests */
static const int mac_imsk2u = FM_SERRSF | FM_SNFSLD | FM_SRCVOVR | FM_SRBFL |
			FM_SMYCLM ;
static const int mac_imsk2l = FM_STRTEXR | FM_SDUPCLM | FM_SFRMCTR |
			FM_SERRCTR | FM_SLSTCTR |
			FM_STRTEXP | FM_SMULTDA | FM_SRNGOP ;

static const int mac_imsk3u = FM_SRCVOVR2 | FM_SRBFL2 ;
static const int mac_imsk3l = FM_SRPERRQ2 | FM_SRPERRQ1 ;

static const int mac_beacon_imsk2u = FM_SOTRBEC | FM_SMYBEC | FM_SBEC |
			FM_SLOCLM | FM_SHICLM | FM_SMYCLM | FM_SCLM ;


static u_long mac_get_tneg(struct s_smc *smc)
{
	u_long	tneg ;

	tneg = (u_long)((long)inpw(FM_A(FM_TNEG))<<5) ;
	return((u_long)((tneg + ((inpw(FM_A(FM_TMRS))>>10)&0x1f)) |
		0xffe00000L)) ;
}

void mac_update_counter(struct s_smc *smc)
{
	smc->mib.m[MAC0].fddiMACFrame_Ct =
		(smc->mib.m[MAC0].fddiMACFrame_Ct & 0xffff0000L)
		+ (u_short) inpw(FM_A(FM_FCNTR)) ;
	smc->mib.m[MAC0].fddiMACLost_Ct =
		(smc->mib.m[MAC0].fddiMACLost_Ct & 0xffff0000L)
		+ (u_short) inpw(FM_A(FM_LCNTR)) ;
	smc->mib.m[MAC0].fddiMACError_Ct =
		(smc->mib.m[MAC0].fddiMACError_Ct & 0xffff0000L)
		+ (u_short) inpw(FM_A(FM_ECNTR)) ;
	smc->mib.m[MAC0].fddiMACT_Neg = mac_get_tneg(smc) ;
#ifdef SMT_REAL_TOKEN_CT
	/*
	 * If the token counter is emulated it is updated in smt_event.
	 */
	TBD
#else
	smt_emulate_token_ct( smc, MAC0 );
#endif
}

/*
 * write long value into buffer memory over memory data register (MDR),
 */
static void write_mdr(struct s_smc *smc, u_long val)
{
	CHECK_NPP() ;
	MDRW(val) ;
}

#if 0
/*
 * read long value from buffer memory over memory data register (MDR),
 */
static u_long read_mdr(struct s_smc *smc, unsigned int addr)
{
	long p ;
	CHECK_NPP() ;
	MARR(addr) ;
	outpw(FM_A(FM_CMDREG1),FM_IRMEMWO) ;
	CHECK_NPP() ;	/* needed for PCI to prevent from timeing violations */
/*	p = MDRR() ; */	/* bad read values if the workaround */
			/* smc->hw.mc_dummy = *((short volatile far *)(addr)))*/
			/* is used */
	p = (u_long)inpw(FM_A(FM_MDRU))<<16 ;
	p += (u_long)inpw(FM_A(FM_MDRL)) ;
	return(p) ;
}
#endif

/*
 * clear buffer memory
 */
static void init_ram(struct s_smc *smc)
{
	u_short i ;

	smc->hw.fp.fifo.rbc_ram_start = 0 ;
	smc->hw.fp.fifo.rbc_ram_end =
		smc->hw.fp.fifo.rbc_ram_start + RBC_MEM_SIZE ;
	CHECK_NPP() ;
	MARW(smc->hw.fp.fifo.rbc_ram_start) ;
	for (i = smc->hw.fp.fifo.rbc_ram_start;
		i < (u_short) (smc->hw.fp.fifo.rbc_ram_end-1); i++)
		write_mdr(smc,0L) ;
	/* Erase the last byte too */
	write_mdr(smc,0L) ;
}

/*
 * set receive FIFO pointer
 */
static void set_recvptr(struct s_smc *smc)
{
	/*
	 * initialize the pointer for receive queue 1
	 */
	outpw(FM_A(FM_RPR1),smc->hw.fp.fifo.rx1_fifo_start) ;	/* RPR1 */
	outpw(FM_A(FM_SWPR1),smc->hw.fp.fifo.rx1_fifo_start) ;	/* SWPR1 */
	outpw(FM_A(FM_WPR1),smc->hw.fp.fifo.rx1_fifo_start) ;	/* WPR1 */
	outpw(FM_A(FM_EARV1),smc->hw.fp.fifo.tx_s_start-1) ;	/* EARV1 */

	/*
	 * initialize the pointer for receive queue 2
	 */
	if (smc->hw.fp.fifo.rx2_fifo_size) {
		outpw(FM_A(FM_RPR2),smc->hw.fp.fifo.rx2_fifo_start) ;
		outpw(FM_A(FM_SWPR2),smc->hw.fp.fifo.rx2_fifo_start) ;
		outpw(FM_A(FM_WPR2),smc->hw.fp.fifo.rx2_fifo_start) ;
		outpw(FM_A(FM_EARV2),smc->hw.fp.fifo.rbc_ram_end-1) ;
	}
	else {
		outpw(FM_A(FM_RPR2),smc->hw.fp.fifo.rbc_ram_end-1) ;
		outpw(FM_A(FM_SWPR2),smc->hw.fp.fifo.rbc_ram_end-1) ;
		outpw(FM_A(FM_WPR2),smc->hw.fp.fifo.rbc_ram_end-1) ;
		outpw(FM_A(FM_EARV2),smc->hw.fp.fifo.rbc_ram_end-1) ;
	}
}

/*
 * set transmit FIFO pointer
 */
static void set_txptr(struct s_smc *smc)
{
	outpw(FM_A(FM_CMDREG2),FM_IRSTQ) ;	/* reset transmit queues */

	/*
	 * initialize the pointer for asynchronous transmit queue
	 */
	outpw(FM_A(FM_RPXA0),smc->hw.fp.fifo.tx_a0_start) ;	/* RPXA0 */
	outpw(FM_A(FM_SWPXA0),smc->hw.fp.fifo.tx_a0_start) ;	/* SWPXA0 */
	outpw(FM_A(FM_WPXA0),smc->hw.fp.fifo.tx_a0_start) ;	/* WPXA0 */
	outpw(FM_A(FM_EAA0),smc->hw.fp.fifo.rx2_fifo_start-1) ;	/* EAA0 */

	/*
	 * initialize the pointer for synchronous transmit queue
	 */
	if (smc->hw.fp.fifo.tx_s_size) {
		outpw(FM_A(FM_RPXS),smc->hw.fp.fifo.tx_s_start) ;
		outpw(FM_A(FM_SWPXS),smc->hw.fp.fifo.tx_s_start) ;
		outpw(FM_A(FM_WPXS),smc->hw.fp.fifo.tx_s_start) ;
		outpw(FM_A(FM_EAS),smc->hw.fp.fifo.tx_a0_start-1) ;
	}
	else {
		outpw(FM_A(FM_RPXS),smc->hw.fp.fifo.tx_a0_start-1) ;
		outpw(FM_A(FM_SWPXS),smc->hw.fp.fifo.tx_a0_start-1) ;
		outpw(FM_A(FM_WPXS),smc->hw.fp.fifo.tx_a0_start-1) ;
		outpw(FM_A(FM_EAS),smc->hw.fp.fifo.tx_a0_start-1) ;
	}
}

/*
 * init memory buffer management registers
 */
static void init_rbc(struct s_smc *smc)
{
	u_short	rbc_ram_addr ;

	/*
	 * set unused pointers or permanent pointers
	 */
	rbc_ram_addr = smc->hw.fp.fifo.rx2_fifo_start - 1 ;

	outpw(FM_A(FM_RPXA1),rbc_ram_addr) ;	/* a1-send pointer */
	outpw(FM_A(FM_WPXA1),rbc_ram_addr) ;
	outpw(FM_A(FM_SWPXA1),rbc_ram_addr) ;
	outpw(FM_A(FM_EAA1),rbc_ram_addr) ;

	set_recvptr(smc) ;
	set_txptr(smc) ;
}

/*
 * init rx pointer
 */
static void init_rx(struct s_smc *smc)
{
	struct s_smt_rx_queue	*queue ;

	/*
	 * init all tx data structures for receive queue 1
	 */
	smc->hw.fp.rx[QUEUE_R1] = queue = &smc->hw.fp.rx_q[QUEUE_R1] ;
	queue->rx_bmu_ctl = (HW_PTR) ADDR(B0_R1_CSR) ;
	queue->rx_bmu_dsc = (HW_PTR) ADDR(B4_R1_DA) ;

	/*
	 * init all tx data structures for receive queue 2
	 */
	smc->hw.fp.rx[QUEUE_R2] = queue = &smc->hw.fp.rx_q[QUEUE_R2] ;
	queue->rx_bmu_ctl = (HW_PTR) ADDR(B0_R2_CSR) ;
	queue->rx_bmu_dsc = (HW_PTR) ADDR(B4_R2_DA) ;
}

/*
 * set the TSYNC register of the FORMAC to regulate synchronous transmission
 */
void set_formac_tsync(struct s_smc *smc, long sync_bw)
{
	outpw(FM_A(FM_TSYNC),(unsigned int) (((-sync_bw) >> 5) & 0xffff) ) ;
}

/*
 * init all tx data structures
 */
static void init_tx(struct s_smc *smc)
{
	struct s_smt_tx_queue	*queue ;

	/*
	 * init all tx data structures for the synchronous queue
	 */
	smc->hw.fp.tx[QUEUE_S] = queue = &smc->hw.fp.tx_q[QUEUE_S] ;
	queue->tx_bmu_ctl = (HW_PTR) ADDR(B0_XS_CSR) ;
	queue->tx_bmu_dsc = (HW_PTR) ADDR(B5_XS_DA) ;

#ifdef ESS
	set_formac_tsync(smc,smc->ess.sync_bw) ;
#endif

	/*
	 * init all tx data structures for the asynchronous queue 0
	 */
	smc->hw.fp.tx[QUEUE_A0] = queue = &smc->hw.fp.tx_q[QUEUE_A0] ;
	queue->tx_bmu_ctl = (HW_PTR) ADDR(B0_XA_CSR) ;
	queue->tx_bmu_dsc = (HW_PTR) ADDR(B5_XA_DA) ;


	llc_recover_tx(smc) ;
}

static void mac_counter_init(struct s_smc *smc)
{
	int i ;
	u_long *ec ;

	/*
	 * clear FORMAC+ frame-, lost- and error counter
	 */
	outpw(FM_A(FM_FCNTR),0) ;
	outpw(FM_A(FM_LCNTR),0) ;
	outpw(FM_A(FM_ECNTR),0) ;
	/*
	 * clear internal error counter stucture
	 */
	ec = (u_long *)&smc->hw.fp.err_stats ;
	for (i = (sizeof(struct err_st)/sizeof(long)) ; i ; i--)
		*ec++ = 0L ;
	smc->mib.m[MAC0].fddiMACRingOp_Ct = 0 ;
}

/*
 * set FORMAC address, and t_request
 */
static	void set_formac_addr(struct s_smc *smc)
{
	long	t_requ = smc->mib.m[MAC0].fddiMACT_Req ;

	outpw(FM_A(FM_SAID),my_said) ;	/* set short address */
	outpw(FM_A(FM_LAIL),(unsigned)((smc->hw.fddi_home_addr.a[4]<<8) +
					smc->hw.fddi_home_addr.a[5])) ;
	outpw(FM_A(FM_LAIC),(unsigned)((smc->hw.fddi_home_addr.a[2]<<8) +
					smc->hw.fddi_home_addr.a[3])) ;
	outpw(FM_A(FM_LAIM),(unsigned)((smc->hw.fddi_home_addr.a[0]<<8) +
					smc->hw.fddi_home_addr.a[1])) ;

	outpw(FM_A(FM_SAGP),my_sagp) ;	/* set short group address */

	outpw(FM_A(FM_LAGL),(unsigned)((smc->hw.fp.group_addr.a[4]<<8) +
					smc->hw.fp.group_addr.a[5])) ;
	outpw(FM_A(FM_LAGC),(unsigned)((smc->hw.fp.group_addr.a[2]<<8) +
					smc->hw.fp.group_addr.a[3])) ;
	outpw(FM_A(FM_LAGM),(unsigned)((smc->hw.fp.group_addr.a[0]<<8) +
					smc->hw.fp.group_addr.a[1])) ;

	/* set r_request regs. (MSW & LSW of TRT ) */
	outpw(FM_A(FM_TREQ1),(unsigned)(t_requ>>16)) ;
	outpw(FM_A(FM_TREQ0),(unsigned)t_requ) ;
}

static void set_int(char *p, int l)
{
	p[0] = (char)(l >> 24) ;
	p[1] = (char)(l >> 16) ;
	p[2] = (char)(l >> 8) ;
	p[3] = (char)(l >> 0) ;
}

/*
 * copy TX descriptor to buffer mem
 * append FC field and MAC frame
 * if more bit is set in descr
 *	append pointer to descriptor (endless loop)
 * else
 *	append 'end of chain' pointer
 */
static void copy_tx_mac(struct s_smc *smc, u_long td, struct fddi_mac *mac,
			unsigned off, int len)
/* u_long td;		 transmit descriptor */
/* struct fddi_mac *mac; mac frame pointer */
/* unsigned off;	 start address within buffer memory */
/* int len ;		 length of the frame including the FC */
{
	int	i ;
	__le32	*p ;

	CHECK_NPP() ;
	MARW(off) ;		/* set memory address reg for writes */

	p = (__le32 *) mac ;
	for (i = (len + 3)/4 ; i ; i--) {
		if (i == 1) {
			/* last word, set the tag bit */
			outpw(FM_A(FM_CMDREG2),FM_ISTTB) ;
		}
		write_mdr(smc,le32_to_cpu(*p)) ;
		p++ ;
	}

	outpw(FM_A(FM_CMDREG2),FM_ISTTB) ;	/* set the tag bit */
	write_mdr(smc,td) ;	/* write over memory data reg to buffer */
}

/*
	BEGIN_MANUAL_ENTRY(module;tests;3)
	How to test directed beacon frames
	----------------------------------------------------------------

	o Insert a break point in the function build_claim_beacon()
	  before calling copy_tx_mac() for building the claim frame.
	o Modify the RM3_DETECT case so that the RM6_DETECT state
	  will always entered from the RM3_DETECT state (function rmt_fsm(),
	  rmt.c)
	o Compile the driver.
	o Set the parameter TREQ in the protocol.ini or net.cfg to a
	  small value to make sure your station will win the claim
	  process.
	o Start the driver.
	o When you reach the break point, modify the SA and DA address
	  of the claim frame (e.g. SA = DA = 10005affffff).
	o When you see RM3_DETECT and RM6_DETECT, observe the direct
	  beacon frames on the UPPSLANA.

	END_MANUAL_ENTRY
 */
static void directed_beacon(struct s_smc *smc)
{
	SK_LOC_DECL(__le32,a[2]) ;

	/*
	 * set UNA in frame
	 * enable FORMAC to send endless queue of directed beacon
	 * important: the UNA starts at byte 1 (not at byte 0)
	 */
	* (char *) a = (char) ((long)DBEACON_INFO<<24L) ;
	a[1] = 0 ;
	memcpy((char *)a+1,(char *) &smc->mib.m[MAC0].fddiMACUpstreamNbr,6) ;

	CHECK_NPP() ;
	 /* set memory address reg for writes */
	MARW(smc->hw.fp.fifo.rbc_ram_start+DBEACON_FRAME_OFF+4) ;
	write_mdr(smc,le32_to_cpu(a[0])) ;
	outpw(FM_A(FM_CMDREG2),FM_ISTTB) ;	/* set the tag bit */
	write_mdr(smc,le32_to_cpu(a[1])) ;

	outpw(FM_A(FM_SABC),smc->hw.fp.fifo.rbc_ram_start + DBEACON_FRAME_OFF) ;
}

/*
	setup claim & beacon pointer
	NOTE :
		special frame packets end with a pointer to their own
		descriptor, and the MORE bit is set in the descriptor
*/
static void build_claim_beacon(struct s_smc *smc, u_long t_request)
{
	u_int	td ;
	int	len ;
	struct fddi_mac_sf *mac ;

	/*
	 * build claim packet
	 */
	len = 17 ;
	td = TX_DESCRIPTOR | ((((u_int)len-1)&3)<<27) ;
	mac = &smc->hw.fp.mac_sfb ;
	mac->mac_fc = FC_CLAIM ;
	/* DA == SA in claim frame */
	mac->mac_source = mac->mac_dest = MA ;
	/* 2's complement */
	set_int((char *)mac->mac_info,(int)t_request) ;

	copy_tx_mac(smc,td,(struct fddi_mac *)mac,
		smc->hw.fp.fifo.rbc_ram_start + CLAIM_FRAME_OFF,len) ;
	/* set CLAIM start pointer */
	outpw(FM_A(FM_SACL),smc->hw.fp.fifo.rbc_ram_start + CLAIM_FRAME_OFF) ;

	/*
	 * build beacon packet
	 */
	len = 17 ;
	td = TX_DESCRIPTOR | ((((u_int)len-1)&3)<<27) ;
	mac->mac_fc = FC_BEACON ;
	mac->mac_source = MA ;
	mac->mac_dest = null_addr ;		/* DA == 0 in beacon frame */
	set_int((char *) mac->mac_info,((int)BEACON_INFO<<24) + 0 ) ;

	copy_tx_mac(smc,td,(struct fddi_mac *)mac,
		smc->hw.fp.fifo.rbc_ram_start + BEACON_FRAME_OFF,len) ;
	/* set beacon start pointer */
	outpw(FM_A(FM_SABC),smc->hw.fp.fifo.rbc_ram_start + BEACON_FRAME_OFF) ;

	/*
	 * build directed beacon packet
	 * contains optional UNA
	 */
	len = 23 ;
	td = TX_DESCRIPTOR | ((((u_int)len-1)&3)<<27) ;
	mac->mac_fc = FC_BEACON ;
	mac->mac_source = MA ;
	mac->mac_dest = dbeacon_multi ;		/* multicast */
	set_int((char *) mac->mac_info,((int)DBEACON_INFO<<24) + 0 ) ;
	set_int((char *) mac->mac_info+4,0) ;
	set_int((char *) mac->mac_info+8,0) ;

	copy_tx_mac(smc,td,(struct fddi_mac *)mac,
		smc->hw.fp.fifo.rbc_ram_start + DBEACON_FRAME_OFF,len) ;

	/* end of claim/beacon queue */
	outpw(FM_A(FM_EACB),smc->hw.fp.fifo.rx1_fifo_start-1) ;

	outpw(FM_A(FM_WPXSF),0) ;
	outpw(FM_A(FM_RPXSF),0) ;
}

static void formac_rcv_restart(struct s_smc *smc)
{
	/* enable receive function */
	SETMASK(FM_A(FM_MDREG1),smc->hw.fp.rx_mode,FM_ADDRX) ;

	outpw(FM_A(FM_CMDREG1),FM_ICLLR) ;	/* clear receive lock */
}

void formac_tx_restart(struct s_smc *smc)
{
	outpw(FM_A(FM_CMDREG1),FM_ICLLS) ;	/* clear s-frame lock */
	outpw(FM_A(FM_CMDREG1),FM_ICLLA0) ;	/* clear a-frame lock */
}

static void enable_formac(struct s_smc *smc)
{
	/* set formac IMSK : 0 enables irq */
	outpw(FM_A(FM_IMSK1U),(unsigned short)~mac_imsk1u);
	outpw(FM_A(FM_IMSK1L),(unsigned short)~mac_imsk1l);
	outpw(FM_A(FM_IMSK2U),(unsigned short)~mac_imsk2u);
	outpw(FM_A(FM_IMSK2L),(unsigned short)~mac_imsk2l);
	outpw(FM_A(FM_IMSK3U),(unsigned short)~mac_imsk3u);
	outpw(FM_A(FM_IMSK3L),(unsigned short)~mac_imsk3l);
}

#if 0	/* Removed because the driver should use the ASICs TX complete IRQ. */
	/* The FORMACs tx complete IRQ should be used any longer */

/*
	BEGIN_MANUAL_ENTRY(if,func;others;4)

	void enable_tx_irq(smc, queue)
	struct s_smc *smc ;
	u_short	queue ;

Function	DOWNCALL	(SMT, fplustm.c)
		enable_tx_irq() enables the FORMACs transmit complete
		interrupt of the queue.

Para	queue	= QUEUE_S:	synchronous queue
		= QUEUE_A0:	asynchronous queue

Note	After any ring operational change the transmit complete
	interrupts are disabled.
	The operating system dependent module must enable
	the transmit complete interrupt of a queue,
		- when it queues the first frame,
		  because of no transmit resources are beeing
		  available and
		- when it escapes from the function llc_restart_tx
		  while some frames are still queued.

	END_MANUAL_ENTRY
 */
void enable_tx_irq(struct s_smc *smc, u_short queue)
/* u_short queue; 0 = synchronous queue, 1 = asynchronous queue 0 */
{
	u_short	imask ;

	imask = ~(inpw(FM_A(FM_IMSK1U))) ;

	if (queue == 0) {
		outpw(FM_A(FM_IMSK1U),~(imask|FM_STEFRMS)) ;
	}
	if (queue == 1) {
		outpw(FM_A(FM_IMSK1U),~(imask|FM_STEFRMA0)) ;
	}
}

/*
	BEGIN_MANUAL_ENTRY(if,func;others;4)

	void disable_tx_irq(smc, queue)
	struct s_smc *smc ;
	u_short	queue ;

Function	DOWNCALL	(SMT, fplustm.c)
		disable_tx_irq disables the FORMACs transmit complete
		interrupt of the queue

Para	queue	= QUEUE_S:	synchronous queue
		= QUEUE_A0:	asynchronous queue

Note	The operating system dependent module should disable
	the transmit complete interrupts if it escapes from the
	function llc_restart_tx and no frames are queued.

	END_MANUAL_ENTRY
 */
void disable_tx_irq(struct s_smc *smc, u_short queue)
/* u_short queue; 0 = synchronous queue, 1 = asynchronous queue 0 */
{
	u_short	imask ;

	imask = ~(inpw(FM_A(FM_IMSK1U))) ;

	if (queue == 0) {
		outpw(FM_A(FM_IMSK1U),~(imask&~FM_STEFRMS)) ;
	}
	if (queue == 1) {
		outpw(FM_A(FM_IMSK1U),~(imask&~FM_STEFRMA0)) ;
	}
}
#endif

static void disable_formac(struct s_smc *smc)
{
	/* clear formac IMSK : 1 disables irq */
	outpw(FM_A(FM_IMSK1U),MW) ;
	outpw(FM_A(FM_IMSK1L),MW) ;
	outpw(FM_A(FM_IMSK2U),MW) ;
	outpw(FM_A(FM_IMSK2L),MW) ;
	outpw(FM_A(FM_IMSK3U),MW) ;
	outpw(FM_A(FM_IMSK3L),MW) ;
}


static void mac_ring_up(struct s_smc *smc, int up)
{
	if (up) {
		formac_rcv_restart(smc) ;	/* enable receive function */
		smc->hw.mac_ring_is_up = TRUE ;
		llc_restart_tx(smc) ;		/* TX queue */
	}
	else {
		/* disable receive function */
		SETMASK(FM_A(FM_MDREG1),FM_MDISRCV,FM_ADDET) ;

		/* abort current transmit activity */
		outpw(FM_A(FM_CMDREG2),FM_IACTR) ;

		smc->hw.mac_ring_is_up = FALSE ;
	}
}

/*--------------------------- ISR handling ----------------------------------*/
/*
 * mac1_irq is in drvfbi.c
 */

/*
 * mac2_irq:	status bits for the receive queue 1, and ring status
 * 		ring status indication bits
 */
void mac2_irq(struct s_smc *smc, u_short code_s2u, u_short code_s2l)
{
	u_short	change_s2l ;
	u_short	change_s2u ;

	/* (jd) 22-Feb-1999
	 * Restart 2_DMax Timer after end of claiming or beaconing
	 */
	if (code_s2u & (FM_SCLM|FM_SHICLM|FM_SBEC|FM_SOTRBEC)) {
		queue_event(smc,EVENT_RMT,RM_TX_STATE_CHANGE) ;
	}
	else if (code_s2l & (FM_STKISS)) {
		queue_event(smc,EVENT_RMT,RM_TX_STATE_CHANGE) ;
	}

	/*
	 * XOR current st bits with the last to avoid useless RMT event queuing
	 */
	change_s2l = smc->hw.fp.s2l ^ code_s2l ;
	change_s2u = smc->hw.fp.s2u ^ code_s2u ;

	if ((change_s2l & FM_SRNGOP) ||
		(!smc->hw.mac_ring_is_up && ((code_s2l & FM_SRNGOP)))) {
		if (code_s2l & FM_SRNGOP) {
			mac_ring_up(smc,1) ;
			queue_event(smc,EVENT_RMT,RM_RING_OP) ;
			smc->mib.m[MAC0].fddiMACRingOp_Ct++ ;
		}
		else {
			mac_ring_up(smc,0) ;
			queue_event(smc,EVENT_RMT,RM_RING_NON_OP) ;
		}
		goto mac2_end ;
	}
	if (code_s2l & FM_SMISFRM) {	/* missed frame */
		smc->mib.m[MAC0].fddiMACNotCopied_Ct++ ;
	}
	if (code_s2u & (FM_SRCVOVR |	/* recv. FIFO overflow */
			FM_SRBFL)) {	/* recv. buffer full */
		smc->hw.mac_ct.mac_r_restart_counter++ ;
/*		formac_rcv_restart(smc) ;	*/
		smt_stat_counter(smc,1) ;
/*		goto mac2_end ;			*/
	}
	if (code_s2u & FM_SOTRBEC)
		queue_event(smc,EVENT_RMT,RM_OTHER_BEACON) ;
	if (code_s2u & FM_SMYBEC)
		queue_event(smc,EVENT_RMT,RM_MY_BEACON) ;
	if (change_s2u & code_s2u & FM_SLOCLM) {
		DB_RMTN(2,"RMT : lower claim received\n",0,0) ;
	}
	if ((code_s2u & FM_SMYCLM) && !(code_s2l & FM_SDUPCLM)) {
		/*
		 * This is my claim and that claim is not detected as a
		 * duplicate one.
		 */
		queue_event(smc,EVENT_RMT,RM_MY_CLAIM) ;
	}
	if (code_s2l & FM_SDUPCLM) {
		/*
		 * If a duplicate claim frame (same SA but T_Bid != T_Req)
		 * this flag will be set.
		 * In the RMT state machine we need a RM_VALID_CLAIM event
		 * to do the appropriate state change.
		 * RM(34c)
		 */
		queue_event(smc,EVENT_RMT,RM_VALID_CLAIM) ;
	}
	if (change_s2u & code_s2u & FM_SHICLM) {
		DB_RMTN(2,"RMT : higher claim received\n",0,0) ;
	}
	if ( (code_s2l & FM_STRTEXP) ||
	     (code_s2l & FM_STRTEXR) )
		queue_event(smc,EVENT_RMT,RM_TRT_EXP) ;
	if (code_s2l & FM_SMULTDA) {
		/*
		 * The MAC has found a 2. MAC with the same address.
		 * Signal dup_addr_test = failed to RMT state machine.
		 * RM(25)
		 */
		smc->r.dup_addr_test = DA_FAILED ;
		queue_event(smc,EVENT_RMT,RM_DUP_ADDR) ;
	}
	if (code_s2u & FM_SBEC)
		smc->hw.fp.err_stats.err_bec_stat++ ;
	if (code_s2u & FM_SCLM)
		smc->hw.fp.err_stats.err_clm_stat++ ;
	if (code_s2l & FM_STVXEXP)
		smc->mib.m[MAC0].fddiMACTvxExpired_Ct++ ;
	if ((code_s2u & (FM_SBEC|FM_SCLM))) {
		if (!(change_s2l & FM_SRNGOP) && (smc->hw.fp.s2l & FM_SRNGOP)) {
			mac_ring_up(smc,0) ;
			queue_event(smc,EVENT_RMT,RM_RING_NON_OP) ;

			mac_ring_up(smc,1) ;
			queue_event(smc,EVENT_RMT,RM_RING_OP) ;
			smc->mib.m[MAC0].fddiMACRingOp_Ct++ ;
		}
	}
	if (code_s2l & FM_SPHINV)
		smc->hw.fp.err_stats.err_phinv++ ;
	if (code_s2l & FM_SSIFG)
		smc->hw.fp.err_stats.err_sifg_det++ ;
	if (code_s2l & FM_STKISS)
		smc->hw.fp.err_stats.err_tkiss++ ;
	if (code_s2l & FM_STKERR)
		smc->hw.fp.err_stats.err_tkerr++ ;
	if (code_s2l & FM_SFRMCTR)
		smc->mib.m[MAC0].fddiMACFrame_Ct += 0x10000L ;
	if (code_s2l & FM_SERRCTR)
		smc->mib.m[MAC0].fddiMACError_Ct += 0x10000L ;
	if (code_s2l & FM_SLSTCTR)
		smc->mib.m[MAC0].fddiMACLost_Ct  += 0x10000L ;
	if (code_s2u & FM_SERRSF) {
		SMT_PANIC(smc,SMT_E0114, SMT_E0114_MSG) ;
	}
mac2_end:
	/* notice old status */
	smc->hw.fp.s2l = code_s2l ;
	smc->hw.fp.s2u = code_s2u ;
	outpw(FM_A(FM_IMSK2U),~mac_imsk2u) ;
}

/*
 * mac3_irq:	receive queue 2 bits and address detection bits
 */
void mac3_irq(struct s_smc *smc, u_short code_s3u, u_short code_s3l)
{
	UNUSED(code_s3l) ;

	if (code_s3u & (FM_SRCVOVR2 |	/* recv. FIFO overflow */
			FM_SRBFL2)) {	/* recv. buffer full */
		smc->hw.mac_ct.mac_r_restart_counter++ ;
		smt_stat_counter(smc,1);
	}


	if (code_s3u & FM_SRPERRQ2) {	/* parity error receive queue 2 */
		SMT_PANIC(smc,SMT_E0115, SMT_E0115_MSG) ;
	}
	if (code_s3u & FM_SRPERRQ1) {	/* parity error receive queue 2 */
		SMT_PANIC(smc,SMT_E0116, SMT_E0116_MSG) ;
	}
}


/*
 * take formac offline
 */
static void formac_offline(struct s_smc *smc)
{
	outpw(FM_A(FM_CMDREG2),FM_IACTR) ;/* abort current transmit activity */

	/* disable receive function */
	SETMASK(FM_A(FM_MDREG1),FM_MDISRCV,FM_ADDET) ;

	/* FORMAC+ 'Initialize Mode' */
	SETMASK(FM_A(FM_MDREG1),FM_MINIT,FM_MMODE) ;

	disable_formac(smc) ;
	smc->hw.mac_ring_is_up = FALSE ;
	smc->hw.hw_state = STOPPED ;
}

/*
 * bring formac online
 */
static void formac_online(struct s_smc *smc)
{
	enable_formac(smc) ;
	SETMASK(FM_A(FM_MDREG1),FM_MONLINE | FM_SELRA | MDR1INIT |
		smc->hw.fp.rx_mode, FM_MMODE | FM_SELRA | FM_ADDRX) ;
}

/*
 * FORMAC+ full init. (tx, rx, timer, counter, claim & beacon)
 */
int init_fplus(struct s_smc *smc)
{
	smc->hw.fp.nsa_mode = FM_MRNNSAFNMA ;
	smc->hw.fp.rx_mode = FM_MDAMA ;
	smc->hw.fp.group_addr = fddi_broadcast ;
	smc->hw.fp.func_addr = 0 ;
	smc->hw.fp.frselreg_init = 0 ;

	init_driver_fplus(smc) ;
	if (smc->s.sas == SMT_DAS)
		smc->hw.fp.mdr3init |= FM_MENDAS ;

	smc->hw.mac_ct.mac_nobuf_counter = 0 ;
	smc->hw.mac_ct.mac_r_restart_counter = 0 ;

	smc->hw.fp.fm_st1u = (HW_PTR) ADDR(B0_ST1U) ;
	smc->hw.fp.fm_st1l = (HW_PTR) ADDR(B0_ST1L) ;
	smc->hw.fp.fm_st2u = (HW_PTR) ADDR(B0_ST2U) ;
	smc->hw.fp.fm_st2l = (HW_PTR) ADDR(B0_ST2L) ;
	smc->hw.fp.fm_st3u = (HW_PTR) ADDR(B0_ST3U) ;
	smc->hw.fp.fm_st3l = (HW_PTR) ADDR(B0_ST3L) ;

	smc->hw.fp.s2l = smc->hw.fp.s2u = 0 ;
	smc->hw.mac_ring_is_up = 0 ;

	mac_counter_init(smc) ;

	/* convert BCKL units to symbol time */
	smc->hw.mac_pa.t_neg = (u_long)0 ;
	smc->hw.mac_pa.t_pri = (u_long)0 ;

	/* make sure all PCI settings are correct */
	mac_do_pci_fix(smc) ;

	return(init_mac(smc,1)) ;
	/* enable_formac(smc) ; */
}

static int init_mac(struct s_smc *smc, int all)
{
	u_short	t_max,x ;
	u_long	time=0 ;

	/*
	 * clear memory
	 */
	outpw(FM_A(FM_MDREG1),FM_MINIT) ;	/* FORMAC+ init mode */
	set_formac_addr(smc) ;
	outpw(FM_A(FM_MDREG1),FM_MMEMACT) ;	/* FORMAC+ memory activ mode */
	/* Note: Mode register 2 is set here, incase parity is enabled. */
	outpw(FM_A(FM_MDREG2),smc->hw.fp.mdr2init) ;

	if (all) {
		init_ram(smc) ;
	}
	else {
		/*
		 * reset the HPI, the Master and the BMUs
		 */
		outp(ADDR(B0_CTRL), CTRL_HPI_SET) ;
		time = hwt_quick_read(smc) ;
	}

	/*
	 * set all pointers, frames etc
	 */
	smt_split_up_fifo(smc) ;

	init_tx(smc) ;
	init_rx(smc) ;
	init_rbc(smc) ;

	build_claim_beacon(smc,smc->mib.m[MAC0].fddiMACT_Req) ;

	/* set RX threshold */
	/* see Errata #SN2 Phantom receive overflow */
	outpw(FM_A(FM_FRMTHR),14<<12) ;		/* switch on */

	/* set formac work mode */
	outpw(FM_A(FM_MDREG1),MDR1INIT | FM_SELRA | smc->hw.fp.rx_mode) ;
	outpw(FM_A(FM_MDREG2),smc->hw.fp.mdr2init) ;
	outpw(FM_A(FM_MDREG3),smc->hw.fp.mdr3init) ;
	outpw(FM_A(FM_FRSELREG),smc->hw.fp.frselreg_init) ;

	/* set timer */
	/*
	 * errata #22 fplus:
	 * T_MAX must not be FFFE
	 * or one of FFDF, FFB8, FF91 (-0x27 etc..)
	 */
	t_max = (u_short)(smc->mib.m[MAC0].fddiMACT_Max/32) ;
	x = t_max/0x27 ;
	x *= 0x27 ;
	if ((t_max == 0xfffe) || (t_max - x == 0x16))
		t_max-- ;
	outpw(FM_A(FM_TMAX),(u_short)t_max) ;

	/* BugFix for report #10204 */
	if (smc->mib.m[MAC0].fddiMACTvxValue < (u_long) (- US2BCLK(52))) {
		outpw(FM_A(FM_TVX), (u_short) (- US2BCLK(52))/255 & MB) ;
	} else {
		outpw(FM_A(FM_TVX),
			(u_short)((smc->mib.m[MAC0].fddiMACTvxValue/255) & MB)) ;
	}

	outpw(FM_A(FM_CMDREG1),FM_ICLLS) ;	/* clear s-frame lock */
	outpw(FM_A(FM_CMDREG1),FM_ICLLA0) ;	/* clear a-frame lock */
	outpw(FM_A(FM_CMDREG1),FM_ICLLR);	/* clear receive lock */

	/* Auto unlock receice threshold for receive queue 1 and 2 */
	outpw(FM_A(FM_UNLCKDLY),(0xff|(0xff<<8))) ;

	rtm_init(smc) ;				/* RT-Monitor */

	if (!all) {
		/*
		 * after 10ms, reset the BMUs and repair the rings
		 */
		hwt_wait_time(smc,time,MS2BCLK(10)) ;
		outpd(ADDR(B0_R1_CSR),CSR_SET_RESET) ;
		outpd(ADDR(B0_XA_CSR),CSR_SET_RESET) ;
		outpd(ADDR(B0_XS_CSR),CSR_SET_RESET) ;
		outp(ADDR(B0_CTRL), CTRL_HPI_CLR) ;
		outpd(ADDR(B0_R1_CSR),CSR_CLR_RESET) ;
		outpd(ADDR(B0_XA_CSR),CSR_CLR_RESET) ;
		outpd(ADDR(B0_XS_CSR),CSR_CLR_RESET) ;
		if (!smc->hw.hw_is_64bit) {
			outpd(ADDR(B4_R1_F), RX_WATERMARK) ;
			outpd(ADDR(B5_XA_F), TX_WATERMARK) ;
			outpd(ADDR(B5_XS_F), TX_WATERMARK) ;
		}
		smc->hw.hw_state = STOPPED ;
		mac_drv_repair_descr(smc) ;
	}
	smc->hw.hw_state = STARTED ;

	return(0) ;
}


/*
 * called by CFM
 */
void config_mux(struct s_smc *smc, int mux)
{
	plc_config_mux(smc,mux) ;

	SETMASK(FM_A(FM_MDREG1),FM_SELRA,FM_SELRA) ;
}

/*
 * called by RMT
 * enable CLAIM/BEACON interrupts
 * (only called if these events are of interest, e.g. in DETECT state
 * the interrupt must not be permanently enabled
 * RMT calls this function periodically (timer driven polling)
 */
void sm_mac_check_beacon_claim(struct s_smc *smc)
{
	/* set formac IMSK : 0 enables irq */
	outpw(FM_A(FM_IMSK2U),~(mac_imsk2u | mac_beacon_imsk2u)) ;
	/* the driver must receive the directed beacons */
	formac_rcv_restart(smc) ;
	process_receive(smc) ;
}

/*-------------------------- interface functions ----------------------------*/
/*
 * control MAC layer	(called by RMT)
 */
void sm_ma_control(struct s_smc *smc, int mode)
{
	switch(mode) {
	case MA_OFFLINE :
		/* Add to make the MAC offline in RM0_ISOLATED state */
		formac_offline(smc) ;
		break ;
	case MA_RESET :
		(void)init_mac(smc,0) ;
		break ;
	case MA_BEACON :
		formac_online(smc) ;
		break ;
	case MA_DIRECTED :
		directed_beacon(smc) ;
		break ;
	case MA_TREQ :
		/*
		 * no actions necessary, TREQ is already set
		 */
		break ;
	}
}

int sm_mac_get_tx_state(struct s_smc *smc)
{
	return((inpw(FM_A(FM_STMCHN))>>4)&7) ;
}

/*
 * multicast functions
 */

static struct s_fpmc* mac_get_mc_table(struct s_smc *smc,
				       struct fddi_addr *user,
				       struct fddi_addr *own,
				       int del, int can)
{
	struct s_fpmc	*tb ;
	struct s_fpmc	*slot ;
	u_char	*p ;
	int i ;

	/*
	 * set own = can(user)
	 */
	*own = *user ;
	if (can) {
		p = own->a ;
		for (i = 0 ; i < 6 ; i++, p++)
			*p = bitrev8(*p);
	}
	slot = NULL;
	for (i = 0, tb = smc->hw.fp.mc.table ; i < FPMAX_MULTICAST ; i++, tb++){
		if (!tb->n) {		/* not used */
			if (!del && !slot)	/* if !del save first free */
				slot = tb ;
			continue ;
		}
		if (memcmp((char *)&tb->a,(char *)own,6))
			continue ;
		return(tb) ;
	}
	return(slot) ;			/* return first free or NULL */
}

/*
	BEGIN_MANUAL_ENTRY(if,func;others;2)

	void mac_clear_multicast(smc)
	struct s_smc *smc ;

Function	DOWNCALL	(SMT, fplustm.c)
		Clear all multicast entries

	END_MANUAL_ENTRY()
 */
void mac_clear_multicast(struct s_smc *smc)
{
	struct s_fpmc	*tb ;
	int i ;

	smc->hw.fp.os_slots_used = 0 ;	/* note the SMT addresses */
					/* will not be deleted */
	for (i = 0, tb = smc->hw.fp.mc.table ; i < FPMAX_MULTICAST ; i++, tb++){
		if (!tb->perm) {
			tb->n = 0 ;
		}
	}
}

/*
	BEGIN_MANUAL_ENTRY(if,func;others;2)

	int mac_add_multicast(smc,addr,can)
	struct s_smc *smc ;
	struct fddi_addr *addr ;
	int can ;

Function	DOWNCALL	(SMC, fplustm.c)
		Add an entry to the multicast table

Para	addr	pointer to a multicast address
	can	= 0:	the multicast address has the physical format
		= 1:	the multicast address has the canonical format
		| 0x80	permanent

Returns	0: success
	1: address table full

Note	After a 'driver reset' or a 'station set address' all
	entries of the multicast table are cleared.
	In this case the driver has to fill the multicast table again.
	After the operating system dependent module filled
	the multicast table it must call mac_update_multicast
	to activate the new multicast addresses!

	END_MANUAL_ENTRY()
 */
int mac_add_multicast(struct s_smc *smc, struct fddi_addr *addr, int can)
{
	SK_LOC_DECL(struct fddi_addr,own) ;
	struct s_fpmc	*tb ;

	/*
	 * check if there are free table entries
	 */
	if (can & 0x80) {
		if (smc->hw.fp.smt_slots_used >= SMT_MAX_MULTI) {
			return(1) ;
		}
	}
	else {
		if (smc->hw.fp.os_slots_used >= FPMAX_MULTICAST-SMT_MAX_MULTI) {
			return(1) ;
		}
	}

	/*
	 * find empty slot
	 */
	if (!(tb = mac_get_mc_table(smc,addr,&own,0,can & ~0x80)))
		return(1) ;
	tb->n++ ;
	tb->a = own ;
	tb->perm = (can & 0x80) ? 1 : 0 ;

	if (can & 0x80)
		smc->hw.fp.smt_slots_used++ ;
	else
		smc->hw.fp.os_slots_used++ ;

	return(0) ;
}

/*
 * mode
 */

#define RX_MODE_PROM		0x1
#define RX_MODE_ALL_MULTI	0x2

/*
	BEGIN_MANUAL_ENTRY(if,func;others;2)

	void mac_update_multicast(smc)
	struct s_smc *smc ;

Function	DOWNCALL	(SMT, fplustm.c)
		Update FORMAC multicast registers

	END_MANUAL_ENTRY()
 */
void mac_update_multicast(struct s_smc *smc)
{
	struct s_fpmc	*tb ;
	u_char	*fu ;
	int	i ;

	/*
	 * invalidate the CAM
	 */
	outpw(FM_A(FM_AFCMD),FM_IINV_CAM) ;

	/*
	 * set the functional address
	 */
	if (smc->hw.fp.func_addr) {
		fu = (u_char *) &smc->hw.fp.func_addr ;
		outpw(FM_A(FM_AFMASK2),0xffff) ;
		outpw(FM_A(FM_AFMASK1),(u_short) ~((fu[0] << 8) + fu[1])) ;
		outpw(FM_A(FM_AFMASK0),(u_short) ~((fu[2] << 8) + fu[3])) ;
		outpw(FM_A(FM_AFPERS),FM_VALID|FM_DA) ;
		outpw(FM_A(FM_AFCOMP2), 0xc000) ;
		outpw(FM_A(FM_AFCOMP1), 0x0000) ;
		outpw(FM_A(FM_AFCOMP0), 0x0000) ;
		outpw(FM_A(FM_AFCMD),FM_IWRITE_CAM) ;
	}

	/*
	 * set the mask and the personality register(s)
	 */
	outpw(FM_A(FM_AFMASK0),0xffff) ;
	outpw(FM_A(FM_AFMASK1),0xffff) ;
	outpw(FM_A(FM_AFMASK2),0xffff) ;
	outpw(FM_A(FM_AFPERS),FM_VALID|FM_DA) ;

	for (i = 0, tb = smc->hw.fp.mc.table; i < FPMAX_MULTICAST; i++, tb++) {
		if (tb->n) {
			CHECK_CAM() ;

			/*
			 * write the multicast address into the CAM
			 */
			outpw(FM_A(FM_AFCOMP2),
				(u_short)((tb->a.a[0]<<8)+tb->a.a[1])) ;
			outpw(FM_A(FM_AFCOMP1),
				(u_short)((tb->a.a[2]<<8)+tb->a.a[3])) ;
			outpw(FM_A(FM_AFCOMP0),
				(u_short)((tb->a.a[4]<<8)+tb->a.a[5])) ;
			outpw(FM_A(FM_AFCMD),FM_IWRITE_CAM) ;
		}
	}
}

/*
	BEGIN_MANUAL_ENTRY(if,func;others;3)

	void mac_set_rx_mode(smc,mode)
	struct s_smc *smc ;
	int mode ;

Function	DOWNCALL/INTERN	(SMT, fplustm.c)
		This function enables / disables the selected receive.
		Don't call this function if the hardware module is
		used -- use mac_drv_rx_mode() instead of.

Para	mode =	1	RX_ENABLE_ALLMULTI	enable all multicasts
		2	RX_DISABLE_ALLMULTI	disable "enable all multicasts"
		3	RX_ENABLE_PROMISC	enable promiscous
		4	RX_DISABLE_PROMISC	disable promiscous
		5	RX_ENABLE_NSA		enable reception of NSA frames
		6	RX_DISABLE_NSA		disable reception of NSA frames

Note	The selected receive modes will be lost after 'driver reset'
	or 'set station address'

	END_MANUAL_ENTRY
 */
void mac_set_rx_mode(struct s_smc *smc, int mode)
{
	switch (mode) {
	case RX_ENABLE_ALLMULTI :
		smc->hw.fp.rx_prom |= RX_MODE_ALL_MULTI ;
		break ;
	case RX_DISABLE_ALLMULTI :
		smc->hw.fp.rx_prom &= ~RX_MODE_ALL_MULTI ;
		break ;
	case RX_ENABLE_PROMISC :
		smc->hw.fp.rx_prom |= RX_MODE_PROM ;
		break ;
	case RX_DISABLE_PROMISC :
		smc->hw.fp.rx_prom &= ~RX_MODE_PROM ;
		break ;
	case RX_ENABLE_NSA :
		smc->hw.fp.nsa_mode = FM_MDAMA ;
		smc->hw.fp.rx_mode = (smc->hw.fp.rx_mode & ~FM_ADDET) |
			smc->hw.fp.nsa_mode ;
		break ;
	case RX_DISABLE_NSA :
		smc->hw.fp.nsa_mode = FM_MRNNSAFNMA ;
		smc->hw.fp.rx_mode = (smc->hw.fp.rx_mode & ~FM_ADDET) |
			smc->hw.fp.nsa_mode ;
		break ;
	}
	if (smc->hw.fp.rx_prom & RX_MODE_PROM) {
		smc->hw.fp.rx_mode = FM_MLIMPROM ;
	}
	else if (smc->hw.fp.rx_prom & RX_MODE_ALL_MULTI) {
		smc->hw.fp.rx_mode = smc->hw.fp.nsa_mode | FM_EXGPA0 ;
	}
	else
		smc->hw.fp.rx_mode = smc->hw.fp.nsa_mode ;
	SETMASK(FM_A(FM_MDREG1),smc->hw.fp.rx_mode,FM_ADDRX) ;
	mac_update_multicast(smc) ;
}

/*
	BEGIN_MANUAL_ENTRY(module;tests;3)
	How to test the Restricted Token Monitor
	----------------------------------------------------------------

	o Insert a break point in the function rtm_irq()
	o Remove all stations with a restricted token monitor from the
	  network.
	o Connect a UPPS ISA or EISA station to the network.
	o Give the FORMAC of UPPS station the command to send
	  restricted tokens until the ring becomes instable.
	o Now connect your test test client.
	o The restricted token monitor should detect the restricted token,
	  and your break point will be reached.
	o You can ovserve how the station will clean the ring.

	END_MANUAL_ENTRY
 */
void rtm_irq(struct s_smc *smc)
{
	outpw(ADDR(B2_RTM_CRTL),TIM_CL_IRQ) ;		/* clear IRQ */
	if (inpw(ADDR(B2_RTM_CRTL)) & TIM_RES_TOK) {
		outpw(FM_A(FM_CMDREG1),FM_ICL) ;	/* force claim */
		DB_RMT("RMT: fddiPATHT_Rmode expired\n",0,0) ;
		AIX_EVENT(smc, (u_long) FDDI_RING_STATUS,
				(u_long) FDDI_SMT_EVENT,
				(u_long) FDDI_RTT, smt_get_event_word(smc));
	}
	outpw(ADDR(B2_RTM_CRTL),TIM_START) ;	/* enable RTM monitoring */
}

static void rtm_init(struct s_smc *smc)
{
	outpd(ADDR(B2_RTM_INI),0) ;		/* timer = 0 */
	outpw(ADDR(B2_RTM_CRTL),TIM_START) ;	/* enable IRQ */
}

void rtm_set_timer(struct s_smc *smc)
{
	/*
	 * MIB timer and hardware timer have the same resolution of 80nS
	 */
	DB_RMT("RMT: setting new fddiPATHT_Rmode, t = %d ns\n",
		(int) smc->mib.a[PATH0].fddiPATHT_Rmode,0) ;
	outpd(ADDR(B2_RTM_INI),smc->mib.a[PATH0].fddiPATHT_Rmode) ;
}

static void smt_split_up_fifo(struct s_smc *smc)
{

/*
	BEGIN_MANUAL_ENTRY(module;mem;1)
	-------------------------------------------------------------
	RECEIVE BUFFER MEMORY DIVERSION
	-------------------------------------------------------------

	R1_RxD == SMT_R1_RXD_COUNT
	R2_RxD == SMT_R2_RXD_COUNT

	SMT_R1_RXD_COUNT must be unequal zero

		   | R1_RxD R2_RxD |R1_RxD R2_RxD | R1_RxD R2_RxD
		   |   x      0	   |  x	    1-3	  |   x     < 3
	----------------------------------------------------------------------
		   |   63,75 kB	   |    54,75	  |	R1_RxD
	rx queue 1 | RX_FIFO_SPACE | RX_LARGE_FIFO| ------------- * 63,75 kB
		   |		   |		  | R1_RxD+R2_RxD
	----------------------------------------------------------------------
		   |		   |    9 kB	  |     R2_RxD
	rx queue 2 |	0 kB	   | RX_SMALL_FIFO| ------------- * 63,75 kB
		   |  (not used)   |		  | R1_RxD+R2_RxD

	END_MANUAL_ENTRY
*/

	if (SMT_R1_RXD_COUNT == 0) {
		SMT_PANIC(smc,SMT_E0117, SMT_E0117_MSG) ;
	}

	switch(SMT_R2_RXD_COUNT) {
	case 0:
		smc->hw.fp.fifo.rx1_fifo_size = RX_FIFO_SPACE ;
		smc->hw.fp.fifo.rx2_fifo_size = 0 ;
		break ;
	case 1:
	case 2:
	case 3:
		smc->hw.fp.fifo.rx1_fifo_size = RX_LARGE_FIFO ;
		smc->hw.fp.fifo.rx2_fifo_size = RX_SMALL_FIFO ;
		break ;
	default:	/* this is not the real defaule */
		smc->hw.fp.fifo.rx1_fifo_size = RX_FIFO_SPACE *
		SMT_R1_RXD_COUNT/(SMT_R1_RXD_COUNT+SMT_R2_RXD_COUNT) ;
		smc->hw.fp.fifo.rx2_fifo_size = RX_FIFO_SPACE *
		SMT_R2_RXD_COUNT/(SMT_R1_RXD_COUNT+SMT_R2_RXD_COUNT) ;
		break ;
	}

/*
	BEGIN_MANUAL_ENTRY(module;mem;1)
	-------------------------------------------------------------
	TRANSMIT BUFFER MEMORY DIVERSION
	-------------------------------------------------------------


		 | no sync bw	| sync bw available and | sync bw available and
		 | available	| SynchTxMode = SPLIT	| SynchTxMode = ALL
	-----------------------------------------------------------------------
	sync tx	 |     0 kB	|	32 kB		|	55 kB
	queue	 |		|   TX_MEDIUM_FIFO	|   TX_LARGE_FIFO
	-----------------------------------------------------------------------
	async tx |    64 kB	|	32 kB		|	 9 k
	queue	 | TX_FIFO_SPACE|   TX_MEDIUM_FIFO	|   TX_SMALL_FIFO

	END_MANUAL_ENTRY
*/

	/*
	 * set the tx mode bits
	 */
	if (smc->mib.a[PATH0].fddiPATHSbaPayload) {
#ifdef ESS
		smc->hw.fp.fifo.fifo_config_mode |=
			smc->mib.fddiESSSynchTxMode | SYNC_TRAFFIC_ON ;
#endif
	}
	else {
		smc->hw.fp.fifo.fifo_config_mode &=
			~(SEND_ASYNC_AS_SYNC|SYNC_TRAFFIC_ON) ;
	}

	/*
	 * split up the FIFO
	 */
	if (smc->hw.fp.fifo.fifo_config_mode & SYNC_TRAFFIC_ON) {
		if (smc->hw.fp.fifo.fifo_config_mode & SEND_ASYNC_AS_SYNC) {
			smc->hw.fp.fifo.tx_s_size = TX_LARGE_FIFO ;
			smc->hw.fp.fifo.tx_a0_size = TX_SMALL_FIFO ;
		}
		else {
			smc->hw.fp.fifo.tx_s_size = TX_MEDIUM_FIFO ;
			smc->hw.fp.fifo.tx_a0_size = TX_MEDIUM_FIFO ;
		}
	}
	else {
			smc->hw.fp.fifo.tx_s_size = 0 ;
			smc->hw.fp.fifo.tx_a0_size = TX_FIFO_SPACE ;
	}

	smc->hw.fp.fifo.rx1_fifo_start = smc->hw.fp.fifo.rbc_ram_start +
		RX_FIFO_OFF ;
	smc->hw.fp.fifo.tx_s_start = smc->hw.fp.fifo.rx1_fifo_start +
		smc->hw.fp.fifo.rx1_fifo_size ;
	smc->hw.fp.fifo.tx_a0_start = smc->hw.fp.fifo.tx_s_start +
		smc->hw.fp.fifo.tx_s_size ;
	smc->hw.fp.fifo.rx2_fifo_start = smc->hw.fp.fifo.tx_a0_start +
		smc->hw.fp.fifo.tx_a0_size ;

	DB_SMT("FIFO split: mode = %x\n",smc->hw.fp.fifo.fifo_config_mode,0) ;
	DB_SMT("rbc_ram_start =	%x	 rbc_ram_end = 	%x\n",
		smc->hw.fp.fifo.rbc_ram_start, smc->hw.fp.fifo.rbc_ram_end) ;
	DB_SMT("rx1_fifo_start = %x	 tx_s_start = 	%x\n",
		smc->hw.fp.fifo.rx1_fifo_start, smc->hw.fp.fifo.tx_s_start) ;
	DB_SMT("tx_a0_start =	%x	 rx2_fifo_start = 	%x\n",
		smc->hw.fp.fifo.tx_a0_start, smc->hw.fp.fifo.rx2_fifo_start) ;
}

void formac_reinit_tx(struct s_smc *smc)
{
	/*
	 * Split up the FIFO and reinitialize the MAC if synchronous
	 * bandwidth becomes available but no synchronous queue is
	 * configured.
	 */
	if (!smc->hw.fp.fifo.tx_s_size && smc->mib.a[PATH0].fddiPATHSbaPayload){
		(void)init_mac(smc,0) ;
	}
}

