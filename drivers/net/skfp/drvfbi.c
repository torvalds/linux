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
 * FBI board dependent Driver for SMT and LLC
 */

#include "h/types.h"
#include "h/fddi.h"
#include "h/smc.h"
#include "h/supern_2.h"
#include "h/skfbiinc.h"

#ifndef	lint
static const char ID_sccs[] = "@(#)drvfbi.c	1.63 99/02/11 (C) SK " ;
#endif

/*
 * PCM active state
 */
#define PC8_ACTIVE	8

#define	LED_Y_ON	0x11	/* Used for ring up/down indication */
#define	LED_Y_OFF	0x10


#define MS2BCLK(x)	((x)*12500L)

/*
 * valid configuration values are:
 */
#ifdef	ISA
const int opt_ints[] = {8,	3, 4, 5, 9, 10, 11, 12, 15} ;
const int opt_iops[] = {8,
	0x100, 0x120, 0x180, 0x1a0, 0x220, 0x240, 0x320, 0x340};
const int opt_dmas[] = {4,	3, 5, 6, 7} ;
const int opt_eproms[] = {15,	0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce,
			0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc} ;
#endif
#ifdef	EISA
const int opt_ints[] = {5, 9, 10, 11} ;
const int opt_dmas[] = {0, 5, 6, 7} ;
const int opt_eproms[] = {0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce,
				0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc} ;
#endif

#ifdef	MCA
int	opt_ints[] = {3, 11, 10, 9} ;			/* FM1 */
int	opt_eproms[] = {0, 0xc4, 0xc8, 0xcc, 0xd0, 0xd4, 0xd8, 0xdc} ;
#endif	/* MCA */

/*
 *	xPOS_ID:xxxx
 *	|	\  /
 *	|	 \/
 *	|	  --------------------- the patched POS_ID of the Adapter
 *	|				xxxx = (Vendor ID low byte,
 *	|					Vendor ID high byte,
 *	|					Device ID low byte,
 *	|					Device ID high byte)
 *	+------------------------------ the patched oem_id must be
 *					'S' for SK or 'I' for IBM
 *					this is a short id for the driver.
 */
#ifndef MULT_OEM
#ifndef	OEM_CONCEPT
#ifndef MCA
const u_char oem_id[] = "xPOS_ID:xxxx" ;
#else
const u_char oem_id[] = "xPOSID1:xxxx" ;	/* FM1 card id. */
#endif
#else	/* OEM_CONCEPT */
#ifndef MCA
const u_char oem_id[] = OEM_ID ;
#else
const u_char oem_id[] = OEM_ID1 ;	/* FM1 card id. */
#endif	/* MCA */
#endif	/* OEM_CONCEPT */
#define	ID_BYTE0	8
#define	OEMID(smc,i)	oem_id[ID_BYTE0 + i]
#else	/* MULT_OEM */
const struct s_oem_ids oem_ids[] = {
#include "oemids.h"
{0}
};
#define	OEMID(smc,i)	smc->hw.oem_id->oi_id[i]
#endif	/* MULT_OEM */

/* Prototypes of external functions */
#ifdef AIX
extern int AIX_vpdReadByte() ;
#endif


/* Prototype of a local function. */
static void smt_stop_watchdog(struct s_smc *smc);

#ifdef MCA
static int read_card_id() ;
static void DisableSlotAccess() ;
static void EnableSlotAccess() ;
#ifdef AIX
extern int attach_POS_addr() ;
extern int detach_POS_addr() ;
extern u_char read_POS() ;
extern void write_POS() ;
extern int AIX_vpdReadByte() ;
#else
#define	read_POS(smc,a1,a2)	((u_char) inp(a1))
#define	write_POS(smc,a1,a2,a3)	outp((a1),(a3))
#endif
#endif	/* MCA */


/*
 * FDDI card reset
 */
static void card_start(struct s_smc *smc)
{
	int i ;
#ifdef	PCI
	u_char	rev_id ;
	u_short word;
#endif

	smt_stop_watchdog(smc) ;

#ifdef	ISA
	outpw(CSR_A,0) ;			/* reset for all chips */
	for (i = 10 ; i ; i--)			/* delay for PLC's */
		(void)inpw(ISR_A) ;
	OUT_82c54_TIMER(3,COUNT(2) | RW_OP(3) | TMODE(2)) ;
					/* counter 2, mode 2 */
	OUT_82c54_TIMER(2,97) ;		/* LSB */
	OUT_82c54_TIMER(2,0) ;		/* MSB ( 15.6 us ) */
	outpw(CSR_A,CS_CRESET) ;
#endif
#ifdef	EISA
	outpw(CSR_A,0) ;			/* reset for all chips */
	for (i = 10 ; i ; i--)			/* delay for PLC's */
		(void)inpw(ISR_A) ;
	outpw(CSR_A,CS_CRESET) ;
	smc->hw.led = (2<<6) ;
	outpw(CSR_A,CS_CRESET | smc->hw.led) ;
#endif
#ifdef	MCA
	outp(ADDR(CARD_DIS),0) ;		/* reset for all chips */
	for (i = 10 ; i ; i--)			/* delay for PLC's */
		(void)inpw(ISR_A) ;
	outp(ADDR(CARD_EN),0) ;
	/* first I/O after reset must not be a access to FORMAC or PLC */

	/*
	 * bus timeout (MCA)
	 */
	OUT_82c54_TIMER(3,COUNT(2) | RW_OP(3) | TMODE(3)) ;
					/* counter 2, mode 3 */
	OUT_82c54_TIMER(2,(2*24)) ;	/* 3.9 us * 2 square wave */
	OUT_82c54_TIMER(2,0) ;		/* MSB */

	/* POS 102 indicated an activ Check Line or Buss Error monitoring */
	if (inpw(CSA_A) & (POS_EN_CHKINT | POS_EN_BUS_ERR)) {
		outp(ADDR(IRQ_CHCK_EN),0) ;
	}

	if (!((i = inpw(CSR_A)) & CS_SAS)) {
		if (!(i & CS_BYSTAT)) {
			outp(ADDR(BYPASS(STAT_INS)),0) ;/* insert station */
		}
	}
	outpw(LEDR_A,LED_1) ;	/* yellow */
#endif	/* MCA */
#ifdef	PCI
	/*
	 * make sure no transfer activity is pending
	 */
	outpw(FM_A(FM_MDREG1),FM_MINIT) ;
	outp(ADDR(B0_CTRL), CTRL_HPI_SET) ;
	hwt_wait_time(smc,hwt_quick_read(smc),MS2BCLK(10)) ;
	/*
	 * now reset everything
	 */
	outp(ADDR(B0_CTRL),CTRL_RST_SET) ;	/* reset for all chips */
	i = (int) inp(ADDR(B0_CTRL)) ;		/* do dummy read */
	SK_UNUSED(i) ;				/* Make LINT happy. */
	outp(ADDR(B0_CTRL), CTRL_RST_CLR) ;

	/*
	 * Reset all bits in the PCI STATUS register
	 */
	outp(ADDR(B0_TST_CTRL), TST_CFG_WRITE_ON) ;	/* enable for writes */
	word = inpw(PCI_C(PCI_STATUS)) ;
	outpw(PCI_C(PCI_STATUS), word | PCI_ERRBITS) ;
	outp(ADDR(B0_TST_CTRL), TST_CFG_WRITE_OFF) ;	/* disable writes */

	/*
	 * Release the reset of all the State machines
	 * Release Master_Reset
	 * Release HPI_SM_Reset
	 */
	outp(ADDR(B0_CTRL), CTRL_MRST_CLR|CTRL_HPI_CLR) ;

	/*
	 * determine the adapter type
	 * Note: Do it here, because some drivers may call card_start() once
	 *	 at very first before any other initialization functions is
	 *	 executed.
	 */
	rev_id = inp(PCI_C(PCI_REV_ID)) ;
	if ((rev_id & 0xf0) == SK_ML_ID_1 || (rev_id & 0xf0) == SK_ML_ID_2) {
		smc->hw.hw_is_64bit = TRUE ;
	} else {
		smc->hw.hw_is_64bit = FALSE ;
	}

	/*
	 * Watermark initialization
	 */
	if (!smc->hw.hw_is_64bit) {
		outpd(ADDR(B4_R1_F), RX_WATERMARK) ;
		outpd(ADDR(B5_XA_F), TX_WATERMARK) ;
		outpd(ADDR(B5_XS_F), TX_WATERMARK) ;
	}

	outp(ADDR(B0_CTRL),CTRL_RST_CLR) ;	/* clear the reset chips */
	outp(ADDR(B0_LED),LED_GA_OFF|LED_MY_ON|LED_GB_OFF) ; /* ye LED on */

	/* init the timer value for the watch dog 2,5 minutes */
	outpd(ADDR(B2_WDOG_INI),0x6FC23AC0) ;

	/* initialize the ISR mask */
	smc->hw.is_imask = ISR_MASK ;
	smc->hw.hw_state = STOPPED ;
#endif
	GET_PAGE(0) ;		/* necessary for BOOT */
}

void card_stop(struct s_smc *smc)
{
	smt_stop_watchdog(smc) ;
	smc->hw.mac_ring_is_up = 0 ;		/* ring down */
#ifdef	ISA
	outpw(CSR_A,0) ;			/* reset for all chips */
#endif
#ifdef	EISA
	outpw(CSR_A,0) ;			/* reset for all chips */
#endif
#ifdef	MCA
	outp(ADDR(CARD_DIS),0) ;		/* reset for all chips */
#endif
#ifdef	PCI
	/*
	 * make sure no transfer activity is pending
	 */
	outpw(FM_A(FM_MDREG1),FM_MINIT) ;
	outp(ADDR(B0_CTRL), CTRL_HPI_SET) ;
	hwt_wait_time(smc,hwt_quick_read(smc),MS2BCLK(10)) ;
	/*
	 * now reset everything
	 */
	outp(ADDR(B0_CTRL),CTRL_RST_SET) ;	/* reset for all chips */
	outp(ADDR(B0_CTRL),CTRL_RST_CLR) ;	/* reset for all chips */
	outp(ADDR(B0_LED),LED_GA_OFF|LED_MY_OFF|LED_GB_OFF) ; /* all LEDs off */
	smc->hw.hw_state = STOPPED ;
#endif
}
/*--------------------------- ISR handling ----------------------------------*/

void mac1_irq(struct s_smc *smc, u_short stu, u_short stl)
{
	int	restart_tx = 0 ;
again:
#ifndef PCI
#ifndef ISA
/*
 * FORMAC+ bug modified the queue pointer if many read/write accesses happens!?
 */
	if (stl & (FM_SPCEPDS  |	/* parit/coding err. syn.q.*/
		   FM_SPCEPDA0 |	/* parit/coding err. a.q.0 */
		   FM_SPCEPDA1 |	/* parit/coding err. a.q.1 */
		   FM_SPCEPDA2)) {	/* parit/coding err. a.q.2 */
		SMT_PANIC(smc,SMT_E0132, SMT_E0132_MSG) ;
	}
	if (stl & (FM_STBURS  |	/* tx buffer underrun syn.q.*/
		   FM_STBURA0 |	/* tx buffer underrun a.q.0 */
		   FM_STBURA1 |	/* tx buffer underrun a.q.1 */
		   FM_STBURA2)) {	/* tx buffer underrun a.q.2 */
		SMT_PANIC(smc,SMT_E0133, SMT_E0133_MSG) ;
	}
#endif
	if ( (stu & (FM_SXMTABT |		/* transmit abort */
#ifdef	SYNC
		     FM_STXABRS |	/* syn. tx abort */
#endif	/* SYNC */
		     FM_STXABRA0)) ||	/* asyn. tx abort */
	     (stl & (FM_SQLCKS |		/* lock for syn. q. */
		     FM_SQLCKA0)) ) {	/* lock for asyn. q. */
		formac_tx_restart(smc) ;		/* init tx */
		restart_tx = 1 ;
		stu = inpw(FM_A(FM_ST1U)) ;
		stl = inpw(FM_A(FM_ST1L)) ;
		stu &= ~ (FM_STECFRMA0 | FM_STEFRMA0 | FM_STEFRMS) ;
		if (stu || stl)
			goto again ;
	}

#ifndef	SYNC
	if (stu & (FM_STECFRMA0 | /* end of chain asyn tx */
		   FM_STEFRMA0)) { /* end of frame asyn tx */
		/* free tx_queue */
		smc->hw.n_a_send = 0 ;
		if (++smc->hw.fp.tx_free < smc->hw.fp.tx_max) {
			start_next_send(smc);
		}
		restart_tx = 1 ;
	}
#else	/* SYNC */
	if (stu & (FM_STEFRMA0 |	/* end of asyn tx */
		    FM_STEFRMS)) {	/* end of sync tx */
		restart_tx = 1 ;
	}
#endif	/* SYNC */
	if (restart_tx)
		llc_restart_tx(smc) ;
}
#else	/* PCI */

	/*
	 * parity error: note encoding error is not possible in tag mode
	 */
	if (stl & (FM_SPCEPDS  |	/* parity err. syn.q.*/
		   FM_SPCEPDA0 |	/* parity err. a.q.0 */
		   FM_SPCEPDA1)) {	/* parity err. a.q.1 */
		SMT_PANIC(smc,SMT_E0134, SMT_E0134_MSG) ;
	}
	/*
	 * buffer underrun: can only occur if a tx threshold is specified
	 */
	if (stl & (FM_STBURS  |		/* tx buffer underrun syn.q.*/
		   FM_STBURA0 |		/* tx buffer underrun a.q.0 */
		   FM_STBURA1)) {	/* tx buffer underrun a.q.2 */
		SMT_PANIC(smc,SMT_E0133, SMT_E0133_MSG) ;
	}

	if ( (stu & (FM_SXMTABT |		/* transmit abort */
		     FM_STXABRS |		/* syn. tx abort */
		     FM_STXABRA0)) ||		/* asyn. tx abort */
	     (stl & (FM_SQLCKS |		/* lock for syn. q. */
		     FM_SQLCKA0)) ) {		/* lock for asyn. q. */
		formac_tx_restart(smc) ;	/* init tx */
		restart_tx = 1 ;
		stu = inpw(FM_A(FM_ST1U)) ;
		stl = inpw(FM_A(FM_ST1L)) ;
		stu &= ~ (FM_STECFRMA0 | FM_STEFRMA0 | FM_STEFRMS) ;
		if (stu || stl)
			goto again ;
	}

	if (stu & (FM_STEFRMA0 |	/* end of asyn tx */
		    FM_STEFRMS)) {	/* end of sync tx */
		restart_tx = 1 ;
	}

	if (restart_tx)
		llc_restart_tx(smc) ;
}
#endif	/* PCI */
/*
 * interrupt source= plc1
 * this function is called in nwfbisr.asm
 */
void plc1_irq(struct s_smc *smc)
{
	u_short	st = inpw(PLC(PB,PL_INTR_EVENT)) ;

#if	(defined(ISA) || defined(EISA))
	/* reset PLC Int. bits */
	outpw(PLC1_I,inpw(PLC1_I)) ;
#endif
	plc_irq(smc,PB,st) ;
}

/*
 * interrupt source= plc2
 * this function is called in nwfbisr.asm
 */
void plc2_irq(struct s_smc *smc)
{
	u_short	st = inpw(PLC(PA,PL_INTR_EVENT)) ;

#if	(defined(ISA) || defined(EISA))
	/* reset PLC Int. bits */
	outpw(PLC2_I,inpw(PLC2_I)) ;
#endif
	plc_irq(smc,PA,st) ;
}


/*
 * interrupt source= timer
 */
void timer_irq(struct s_smc *smc)
{
	hwt_restart(smc);
	smc->hw.t_stop = smc->hw.t_start;
	smt_timer_done(smc) ;
}

/*
 * return S-port (PA or PB)
 */
int pcm_get_s_port(struct s_smc *smc)
{
	SK_UNUSED(smc) ;
	return(PS) ;
}

/*
 * Station Label = "FDDI-XYZ" where
 *
 *	X = connector type
 *	Y = PMD type
 *	Z = port type
 */
#define STATION_LABEL_CONNECTOR_OFFSET	5
#define STATION_LABEL_PMD_OFFSET	6
#define STATION_LABEL_PORT_OFFSET	7

void read_address(struct s_smc *smc, u_char *mac_addr)
{
	char ConnectorType ;
	char PmdType ;
	int	i ;

	extern const u_char canonical[256] ;

#if	(defined(ISA) || defined(MCA))
	for (i = 0; i < 4 ;i++) {	/* read mac address from board */
		smc->hw.fddi_phys_addr.a[i] =
			canonical[(inpw(PR_A(i+SA_MAC))&0xff)] ;
	}
	for (i = 4; i < 6; i++) {
		smc->hw.fddi_phys_addr.a[i] =
			canonical[(inpw(PR_A(i+SA_MAC+PRA_OFF))&0xff)] ;
	}
#endif
#ifdef	EISA
	/*
	 * Note: We get trouble on an Alpha machine if we make a inpw()
	 * instead of inp()
	 */
	for (i = 0; i < 4 ;i++) {	/* read mac address from board */
		smc->hw.fddi_phys_addr.a[i] =
			canonical[inp(PR_A(i+SA_MAC))] ;
	}
	for (i = 4; i < 6; i++) {
		smc->hw.fddi_phys_addr.a[i] =
			canonical[inp(PR_A(i+SA_MAC+PRA_OFF))] ;
	}
#endif
#ifdef	PCI
	for (i = 0; i < 6; i++) {	/* read mac address from board */
		smc->hw.fddi_phys_addr.a[i] =
			canonical[inp(ADDR(B2_MAC_0+i))] ;
	}
#endif
#ifndef	PCI
	ConnectorType = inpw(PR_A(SA_PMD_TYPE)) & 0xff ;
	PmdType = inpw(PR_A(SA_PMD_TYPE+1)) & 0xff ;
#else
	ConnectorType = inp(ADDR(B2_CONN_TYP)) ;
	PmdType = inp(ADDR(B2_PMD_TYP)) ;
#endif

	smc->y[PA].pmd_type[PMD_SK_CONN] =
	smc->y[PB].pmd_type[PMD_SK_CONN] = ConnectorType ;
	smc->y[PA].pmd_type[PMD_SK_PMD ] =
	smc->y[PB].pmd_type[PMD_SK_PMD ] = PmdType ;

	if (mac_addr) {
		for (i = 0; i < 6 ;i++) {
			smc->hw.fddi_canon_addr.a[i] = mac_addr[i] ;
			smc->hw.fddi_home_addr.a[i] = canonical[mac_addr[i]] ;
		}
		return ;
	}
	smc->hw.fddi_home_addr = smc->hw.fddi_phys_addr ;

	for (i = 0; i < 6 ;i++) {
		smc->hw.fddi_canon_addr.a[i] =
			canonical[smc->hw.fddi_phys_addr.a[i]] ;
	}
}

/*
 * FDDI card soft reset
 */
void init_board(struct s_smc *smc, u_char *mac_addr)
{
	card_start(smc) ;
	read_address(smc,mac_addr) ;

#ifndef	PCI
	if (inpw(CSR_A) & CS_SAS)
#else
	if (!(inp(ADDR(B0_DAS)) & DAS_AVAIL))
#endif
		smc->s.sas = SMT_SAS ;	/* Single att. station */
	else
		smc->s.sas = SMT_DAS ;	/* Dual att. station */

#ifndef	PCI
	if (inpw(CSR_A) & CS_BYSTAT)
#else
	if (!(inp(ADDR(B0_DAS)) & DAS_BYP_ST))
#endif
		smc->mib.fddiSMTBypassPresent = 0 ;
		/* without opt. bypass */
	else
		smc->mib.fddiSMTBypassPresent = 1 ;
		/* with opt. bypass */
}

/*
 * insert or deinsert optical bypass (called by ECM)
 */
void sm_pm_bypass_req(struct s_smc *smc, int mode)
{
#if	(defined(ISA) || defined(EISA))
	int csra_v ;
#endif

	DB_ECMN(1,"ECM : sm_pm_bypass_req(%s)\n",(mode == BP_INSERT) ?
					"BP_INSERT" : "BP_DEINSERT",0) ;

	if (smc->s.sas != SMT_DAS)
		return ;

#if	(defined(ISA) || defined(EISA))

	csra_v = inpw(CSR_A) & ~CS_BYPASS ;
#ifdef	EISA
	csra_v |= smc->hw.led ;
#endif

	switch(mode) {
	case BP_INSERT :
		outpw(CSR_A,csra_v | CS_BYPASS) ;
		break ;
	case BP_DEINSERT :
		outpw(CSR_A,csra_v) ;
		break ;
	}
#endif	/* ISA / EISA */
#ifdef	MCA
	switch(mode) {
	case BP_INSERT :
		outp(ADDR(BYPASS(STAT_INS)),0) ;/* insert station */
		break ;
	case BP_DEINSERT :
		outp(ADDR(BYPASS(STAT_BYP)),0) ;	/* bypass station */
		break ;
	}
#endif
#ifdef	PCI
	switch(mode) {
	case BP_INSERT :
		outp(ADDR(B0_DAS),DAS_BYP_INS) ;	/* insert station */
		break ;
	case BP_DEINSERT :
		outp(ADDR(B0_DAS),DAS_BYP_RMV) ;	/* bypass station */
		break ;
	}
#endif
}

/*
 * check if bypass connected
 */
int sm_pm_bypass_present(struct s_smc *smc)
{
#ifndef	PCI
	return(	(inpw(CSR_A) & CS_BYSTAT) ? FALSE : TRUE ) ;
#else
	return(	(inp(ADDR(B0_DAS)) & DAS_BYP_ST) ? TRUE: FALSE) ;
#endif
}

void plc_clear_irq(struct s_smc *smc, int p)
{
	SK_UNUSED(p) ;

#if	(defined(ISA) || defined(EISA))
	switch (p) {
	case PA :
		/* reset PLC Int. bits */
		outpw(PLC2_I,inpw(PLC2_I)) ;
		break ;
	case PB :
		/* reset PLC Int. bits */
		outpw(PLC1_I,inpw(PLC1_I)) ;
		break ;
	}
#else
	SK_UNUSED(smc) ;
#endif
}


/*
 * led_indication called by rmt_indication() and
 * pcm_state_change()
 *
 * Input:
 *	smc:	SMT context
 *	led_event:
 *	0	Only switch green LEDs according to their respective PCM state
 *	LED_Y_OFF	just switch yellow LED off
 *	LED_Y_ON	just switch yello LED on
 */
static void led_indication(struct s_smc *smc, int led_event)
{
	/* use smc->hw.mac_ring_is_up == TRUE 
	 * as indication for Ring Operational
	 */
	u_short			led_state ;
	struct s_phy		*phy ;
	struct fddi_mib_p	*mib_a ;
	struct fddi_mib_p	*mib_b ;

	phy = &smc->y[PA] ;
	mib_a = phy->mib ;
	phy = &smc->y[PB] ;
	mib_b = phy->mib ;

#ifdef	EISA
	/* Ring up = yellow led OFF*/
	if (led_event == LED_Y_ON) {
		smc->hw.led |= CS_LED_1 ;
	}
	else if (led_event == LED_Y_OFF) {
		smc->hw.led &= ~CS_LED_1 ;
	}
	else {
		/* Link at Port A or B = green led ON */
		if (mib_a->fddiPORTPCMState == PC8_ACTIVE ||
		    mib_b->fddiPORTPCMState == PC8_ACTIVE) {
			smc->hw.led |= CS_LED_0 ;
		}
		else {
			smc->hw.led &= ~CS_LED_0 ;
		}
	}
#endif
#ifdef	MCA
	led_state = inpw(LEDR_A) ;
	
	/* Ring up = yellow led OFF*/
	if (led_event == LED_Y_ON) {
		led_state |= LED_1 ;
	}
	else if (led_event == LED_Y_OFF) {
		led_state &= ~LED_1 ;
	}
	else {
                led_state &= ~(LED_2|LED_0) ;

		/* Link at Port A = green led A ON */
		if (mib_a->fddiPORTPCMState == PC8_ACTIVE) {	
			led_state |= LED_2 ;
		}
		
		/* Link at Port B/S = green led B ON */
		if (mib_b->fddiPORTPCMState == PC8_ACTIVE) {
			led_state |= LED_0 ;
		}
	}

        outpw(LEDR_A, led_state) ;
#endif	/* MCA */
#ifdef	PCI
        led_state = 0 ;
	
	/* Ring up = yellow led OFF*/
	if (led_event == LED_Y_ON) {
		led_state |= LED_MY_ON ;
	}
	else if (led_event == LED_Y_OFF) {
		led_state |= LED_MY_OFF ;
	}
	else {	/* PCM state changed */
		/* Link at Port A/S = green led A ON */
		if (mib_a->fddiPORTPCMState == PC8_ACTIVE) {	
			led_state |= LED_GA_ON ;
		}
		else {
			led_state |= LED_GA_OFF ;
		}
		
		/* Link at Port B = green led B ON */
		if (mib_b->fddiPORTPCMState == PC8_ACTIVE) {
			led_state |= LED_GB_ON ;
		}
		else {
			led_state |= LED_GB_OFF ;
		}
	}

        outp(ADDR(B0_LED), led_state) ;
#endif	/* PCI */

}


void pcm_state_change(struct s_smc *smc, int plc, int p_state)
{
	/*
	 * the current implementation of pcm_state_change() in the driver
	 * parts must be renamed to drv_pcm_state_change() which will be called
	 * now after led_indication.
	 */
	DRV_PCM_STATE_CHANGE(smc,plc,p_state) ;
	
	led_indication(smc,0) ;
}


void rmt_indication(struct s_smc *smc, int i)
{
	/* Call a driver special function if defined */
	DRV_RMT_INDICATION(smc,i) ;

        led_indication(smc, i ? LED_Y_OFF : LED_Y_ON) ;
}


/*
 * llc_recover_tx called by init_tx (fplus.c)
 */
void llc_recover_tx(struct s_smc *smc)
{
#ifdef	LOAD_GEN
	extern	int load_gen_flag ;

	load_gen_flag = 0 ;
#endif
#ifndef	SYNC
	smc->hw.n_a_send= 0 ;
#else
	SK_UNUSED(smc) ;
#endif
}

#ifdef MULT_OEM
static int is_equal_num(char comp1[], char comp2[], int num)
{
	int i ;

	for (i = 0 ; i < num ; i++) {
		if (comp1[i] != comp2[i])
			return (0) ;
	}
		return (1) ;
}	/* is_equal_num */


/*
 * set the OEM ID defaults, and test the contents of the OEM data base
 * The default OEM is the first ACTIVE entry in the OEM data base 
 *
 * returns:	0	success
 *		1	error in data base
 *		2	data base empty
 *		3	no active entry	
 */
int set_oi_id_def(struct s_smc *smc)
{
	int sel_id ;
	int i ;
	int act_entries ;

	i = 0 ;
	sel_id = -1 ;
	act_entries = FALSE ;
	smc->hw.oem_id = 0 ;
	smc->hw.oem_min_status = OI_STAT_ACTIVE ;
	
	/* check OEM data base */
	while (oem_ids[i].oi_status) {
		switch (oem_ids[i].oi_status) {
		case OI_STAT_ACTIVE:
			act_entries = TRUE ;	/* we have active IDs */
			if (sel_id == -1)
				sel_id = i ;	/* save the first active ID */
		case OI_STAT_VALID:
		case OI_STAT_PRESENT:
			i++ ;
			break ;			/* entry ok */
		default:
			return (1) ;		/* invalid oi_status */
		}
	}

	if (i == 0)
		return (2) ;
	if (!act_entries)
		return (3) ;

	/* ok, we have a valid OEM data base with an active entry */
	smc->hw.oem_id = (struct s_oem_ids *)  &oem_ids[sel_id] ;
	return (0) ;
}
#endif	/* MULT_OEM */


#ifdef	MCA
/************************
 *
 * BEGIN_MANUAL_ENTRY()
 *
 *	exist_board
 *
 *	Check if an MCA board is present in the specified slot.
 *
 *	int exist_board(
 *		struct s_smc *smc,
 *		int slot) ;
 * In
 *	smc - A pointer to the SMT Context struct.
 *
 *	slot - The number of the slot to inspect.
 * Out
 *	0 = No adapter present.
 *	1 = Found FM1 adapter.
 *
 * Pseudo
 *      Read MCA ID
 *	for all valid OEM_IDs
 *		compare with ID read
 *		if equal, return 1
 *	return(0
 *
 * Note
 *	The smc pointer must be valid now.
 *
 * END_MANUAL_ENTRY()
 *
 ************************/
#define LONG_CARD_ID(lo, hi)	((((hi) & 0xff) << 8) | ((lo) & 0xff))
int exist_board(struct s_smc *smc, int slot)
{
#ifdef MULT_OEM
	SK_LOC_DECL(u_char,id[2]) ;
	int idi ;
#endif	/* MULT_OEM */

	/* No longer valid. */
	if (smc == NULL)
		return(0) ;

#ifndef MULT_OEM
	if (read_card_id(smc, slot)
		== LONG_CARD_ID(OEMID(smc,0), OEMID(smc,1)))
		return (1) ;	/* Found FM adapter. */

#else	/* MULT_OEM */
	idi = read_card_id(smc, slot) ;
	id[0] = idi & 0xff ;
	id[1] = idi >> 8 ;

        smc->hw.oem_id = (struct s_oem_ids *) &oem_ids[0] ;
	for (; smc->hw.oem_id->oi_status != OI_STAT_LAST; smc->hw.oem_id++) {
		if (smc->hw.oem_id->oi_status < smc->hw.oem_min_status)
			continue ;

		if (is_equal_num(&id[0],&OEMID(smc,0),2))
			return (1) ;
	}
#endif	/* MULT_OEM */
	return (0) ;	/* No adapter found. */
}

/************************
 *
 *	read_card_id
 *
 *	Read the MCA card id from the specified slot.
 * In
 *	smc - A pointer to the SMT Context struct.
 *	CAVEAT: This pointer may be NULL and *must not* be used within this
 *	function. It's only purpose is for drivers that need some information
 *	for the inp() and outp() macros.
 *
 *	slot - The number of the slot for which the card id is returned.
 * Out
 *	Returns the card id read from the specified slot. If an illegal slot
 *	number is specified, the function returns zero.
 *
 ************************/
static int read_card_id(struct s_smc *smc, int slot)
/* struct s_smc *smc ;	Do not use. */
{
	int card_id ;

	SK_UNUSED(smc) ;	/* Make LINT happy. */
	if ((slot < 1) || (slot > 15))	/* max 16 slots, 0 = motherboard */
		return (0) ;	/* Illegal slot number specified. */

	EnableSlotAccess(smc, slot) ;

	card_id = ((read_POS(smc,POS_ID_HIGH,slot - 1) & 0xff) << 8) |
				(read_POS(smc,POS_ID_LOW,slot - 1) & 0xff) ;

	DisableSlotAccess(smc) ;

	return (card_id) ;
}

/************************
 *
 * BEGIN_MANUAL_ENTRY()
 *
 *	get_board_para
 *
 *	Get adapter configuration information. Fill all board specific
 *	parameters within the 'smc' structure.
 *
 *	int get_board_para(
 *		struct s_smc *smc,
 *		int slot) ;
 * In
 *	smc - A pointer to the SMT Context struct, to which this function will
 *	write some adapter configuration data.
 *
 *	slot - The number of the slot, in which the adapter is installed.
 * Out
 *	0 = No adapter present.
 *	1 = Ok.
 *	2 = Adapter present, but card enable bit not set.
 *
 * END_MANUAL_ENTRY()
 *
 ************************/
int get_board_para(struct s_smc *smc, int slot)
{
	int val ;
	int i ;

	/* Check if adapter present & get type of adapter. */
	switch (exist_board(smc, slot)) {
	case 0:	/* Adapter not present. */
		return (0) ;
	case 1:	/* FM Rev. 1 */
		smc->hw.rev = FM1_REV ;
		smc->hw.VFullRead = 0x0a ;
		smc->hw.VFullWrite = 0x05 ;
		smc->hw.DmaWriteExtraBytes = 8 ;	/* 2 extra words. */
		break ;
	}
	smc->hw.slot = slot ;

	EnableSlotAccess(smc, slot) ;

	if (!(read_POS(smc,POS_102, slot - 1) & POS_CARD_EN)) {
		DisableSlotAccess(smc) ;
		return (2) ;	/* Card enable bit not set. */
	}

	val = read_POS(smc,POS_104, slot - 1) ;	/* I/O, IRQ */

#ifndef MEM_MAPPED_IO	/* is defined by the operating system */
	i = val & POS_IOSEL ;	/* I/O base addr. (0x0200 .. 0xfe00) */
	smc->hw.iop = (i + 1) * 0x0400 - 0x200 ;
#endif
	i = ((val & POS_IRQSEL) >> 6) & 0x03 ;	/* IRQ <0, 1> */
	smc->hw.irq = opt_ints[i] ;

	/* FPROM base addr. */
	i = ((read_POS(smc,POS_103, slot - 1) & POS_MSEL) >> 4) & 0x07 ;
	smc->hw.eprom = opt_eproms[i] ;

	DisableSlotAccess(smc) ;

	/* before this, the smc->hw.iop must be set !!! */
	smc->hw.slot_32 = inpw(CSF_A) & SLOT_32 ;

	return (1) ;
}

/* Enable access to specified MCA slot. */
static void EnableSlotAccess(struct s_smc *smc, int slot)
{
	SK_UNUSED(slot) ;

#ifndef AIX
	SK_UNUSED(smc) ;

	/* System mode. */
	outp(POS_SYS_SETUP, POS_SYSTEM) ;

	/* Select slot. */
	outp(POS_CHANNEL_POS, POS_CHANNEL_BIT | (slot-1)) ;
#else
	attach_POS_addr (smc) ;
#endif
}

/* Disable access to MCA slot formerly enabled via EnableSlotAccess(). */
static void DisableSlotAccess(struct s_smc *smc)
{
#ifndef AIX
	SK_UNUSED(smc) ;

	outp(POS_CHANNEL_POS, 0) ;
#else
	detach_POS_addr (smc) ;
#endif
}
#endif	/* MCA */

#ifdef	EISA
#ifndef	MEM_MAPPED_IO
#define	SADDR(slot)	(((slot)<<12)&0xf000)
#else	/* MEM_MAPPED_IO */
#define	SADDR(slot)	(smc->hw.iop)
#endif	/* MEM_MAPPED_IO */

/************************
 *
 * BEGIN_MANUAL_ENTRY()
 *
 *	exist_board
 *
 *	Check if an EISA board is present in the specified slot.
 *
 *	int exist_board(
 *		struct s_smc *smc,
 *		int slot) ;
 * In
 *	smc - A pointer to the SMT Context struct.
 *
 *	slot - The number of the slot to inspect.
 * Out
 *	0 = No adapter present.
 *	1 = Found adapter.
 *
 * Pseudo
 *      Read EISA ID
 *	for all valid OEM_IDs
 *		compare with ID read
 *		if equal, return 1
 *	return(0
 *
 * Note
 *	The smc pointer must be valid now.
 *
 ************************/
int exist_board(struct s_smc *smc, int slot)
{
	int i ;
#ifdef MULT_OEM
	SK_LOC_DECL(u_char,id[4]) ;
#endif	/* MULT_OEM */

	/* No longer valid. */
	if (smc == NULL)
		return(0);

	SK_UNUSED(slot) ;

#ifndef MULT_OEM
	for (i = 0 ; i < 4 ; i++) {
		if (inp(SADDR(slot)+PRA(i)) != OEMID(smc,i))
			return(0) ;
	}
	return(1) ;
#else	/* MULT_OEM */
	for (i = 0 ; i < 4 ; i++)
		id[i] = inp(SADDR(slot)+PRA(i)) ;

	smc->hw.oem_id = (struct s_oem_ids *) &oem_ids[0] ;

	for (; smc->hw.oem_id->oi_status != OI_STAT_LAST; smc->hw.oem_id++) {
		if (smc->hw.oem_id->oi_status < smc->hw.oem_min_status)
			continue ;

		if (is_equal_num(&id[0],&OEMID(smc,0),4))
			return (1) ;
	}
	return (0) ;	/* No adapter found. */
#endif	/* MULT_OEM */
}


int get_board_para(struct s_smc *smc, int slot)
{
	int	i ;

	if (!exist_board(smc,slot))
		return(0) ;

	smc->hw.slot = slot ;
#ifndef	MEM_MAPPED_IO		/* if defined by the operating system */
	smc->hw.iop = SADDR(slot) ;
#endif

	if (!(inp(C0_A(0))&CFG_CARD_EN)) {
		return(2) ;			/* CFG_CARD_EN bit not set! */
	}

	smc->hw.irq = opt_ints[(inp(C1_A(0)) & CFG_IRQ_SEL)] ;
	smc->hw.dma = opt_dmas[((inp(C1_A(0)) & CFG_DRQ_SEL)>>3)] ;

	if ((i = inp(C2_A(0)) & CFG_EPROM_SEL) != 0x0f)
		smc->hw.eprom = opt_eproms[i] ;
	else
		smc->hw.eprom = 0 ;

	smc->hw.DmaWriteExtraBytes = 8 ;

	return(1) ;
}
#endif	/* EISA */

#ifdef	ISA
#ifndef MULT_OEM
const u_char sklogo[6] = SKLOGO_STR ;
#define	SIZE_SKLOGO(smc)	sizeof(sklogo)
#define	SKLOGO(smc,i)		sklogo[i]
#else	/* MULT_OEM */
#define	SIZE_SKLOGO(smc)	smc->hw.oem_id->oi_logo_len
#define	SKLOGO(smc,i)		smc->hw.oem_id->oi_logo[i]
#endif	/* MULT_OEM */


int exist_board(struct s_smc *smc, HW_PTR port)
{
	int	i ;
#ifdef MULT_OEM
	int	bytes_read ;
	u_char	board_logo[15] ;
	SK_LOC_DECL(u_char,id[4]) ;
#endif	/* MULT_OEM */

	/* No longer valid. */
	if (smc == NULL)
		return(0);

	SK_UNUSED(smc) ;
#ifndef MULT_OEM
	for (i = SADDRL ; i < (signed) (SADDRL+SIZE_SKLOGO(smc)) ; i++) {
		if ((u_char)inpw((PRA(i)+port)) != SKLOGO(smc,i-SADDRL)) {
			return(0) ;
		}
	}

	/* check MAC address (S&K or other) */
	for (i = 0 ; i < 3 ; i++) {
		if ((u_char)inpw((PRA(i)+port)) != OEMID(smc,i))
			return(0) ;
	}
	return(1) ;
#else	/* MULT_OEM */
        smc->hw.oem_id = (struct s_oem_ids *)  &oem_ids[0] ;
	board_logo[0] = (u_char)inpw((PRA(SADDRL)+port)) ;
	bytes_read = 1 ;

	for (; smc->hw.oem_id->oi_status != OI_STAT_LAST; smc->hw.oem_id++) {
		if (smc->hw.oem_id->oi_status < smc->hw.oem_min_status)
			continue ;

		/* Test all read bytes with current OEM_entry */
		/* for (i=0; (i<bytes_read) && (i < SIZE_SKLOGO(smc)); i++) { */
		for (i = 0; i < bytes_read; i++) {
			if (board_logo[i] != SKLOGO(smc,i))
				break ;
		}

		/* If mismatch, switch to next OEM entry */
		if ((board_logo[i] != SKLOGO(smc,i)) && (i < bytes_read))
			continue ;

		--i ;
		while (bytes_read < SIZE_SKLOGO(smc)) {
			//   inpw next byte SK_Logo
			i++ ;
			board_logo[i] = (u_char)inpw((PRA(SADDRL+i)+port)) ;
			bytes_read++ ;
			if (board_logo[i] != SKLOGO(smc,i))
				break ;
		}

		for (i = 0 ; i < 3 ; i++)
			id[i] = (u_char)inpw((PRA(i)+port)) ;

		if ((board_logo[i] == SKLOGO(smc,i))
			&& (bytes_read == SIZE_SKLOGO(smc))) {

			if (is_equal_num(&id[0],&OEMID(smc,0),3))
				return(1);
		}
	}	/* for */
	return(0) ;
#endif	/* MULT_OEM */
}

int get_board_para(struct s_smc *smc, int slot)
{
	SK_UNUSED(smc) ;
	SK_UNUSED(slot) ;
	return(0) ;	/* for ISA not supported */
}
#endif	/* ISA */

#ifdef PCI
#ifdef USE_BIOS_FUN
int exist_board(struct s_smc *smc, int slot)
{
	u_short dev_id ;
	u_short ven_id ;
	int found ; 
	int i ;

	found = FALSE ;		/* make sure we returned with adatper not found*/
				/* if an empty oemids.h was included */

#ifdef MULT_OEM
        smc->hw.oem_id = (struct s_oem_ids *) &oem_ids[0] ;
	for (; smc->hw.oem_id->oi_status != OI_STAT_LAST; smc->hw.oem_id++) {
		if (smc->hw.oem_id->oi_status < smc->hw.oem_min_status)
			continue ;
#endif
		ven_id = OEMID(smc,0) + (OEMID(smc,1) << 8) ; 
		dev_id = OEMID(smc,2) + (OEMID(smc,3) << 8) ; 
		for (i = 0; i < slot; i++) {
			if (pci_find_device(i,&smc->hw.pci_handle,
				dev_id,ven_id) != 0) {

				found = FALSE ;
			} else {
				found = TRUE ;
			}
		}
		if (found) {
			return(1) ;	/* adapter was found */
		}
#ifdef MULT_OEM
	}
#endif
	return(0) ;	/* adapter was not found */
}
#endif	/* PCI */
#endif	/* USE_BIOS_FUNC */

void driver_get_bia(struct s_smc *smc, struct fddi_addr *bia_addr)
{
	int i ;

	extern const u_char canonical[256] ;

	for (i = 0 ; i < 6 ; i++) {
		bia_addr->a[i] = canonical[smc->hw.fddi_phys_addr.a[i]] ;
	}
}

void smt_start_watchdog(struct s_smc *smc)
{
	SK_UNUSED(smc) ;	/* Make LINT happy. */

#ifndef	DEBUG

#ifdef	PCI
	if (smc->hw.wdog_used) {
		outpw(ADDR(B2_WDOG_CRTL),TIM_START) ;	/* Start timer. */
	}
#endif

#endif	/* DEBUG */
}

static void smt_stop_watchdog(struct s_smc *smc)
{
	SK_UNUSED(smc) ;	/* Make LINT happy. */
#ifndef	DEBUG

#ifdef	PCI
	if (smc->hw.wdog_used) {
		outpw(ADDR(B2_WDOG_CRTL),TIM_STOP) ;	/* Stop timer. */
	}
#endif

#endif	/* DEBUG */
}

#ifdef	PCI

void mac_do_pci_fix(struct s_smc *smc)
{
	SK_UNUSED(smc) ;
}
#endif	/* PCI */

