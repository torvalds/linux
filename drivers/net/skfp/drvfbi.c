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
#include <linux/bitrev.h>

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
const u_char oem_id[] = "xPOS_ID:xxxx" ;
#else	/* OEM_CONCEPT */
const u_char oem_id[] = OEM_ID ;
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

/*
 * interrupt source= plc1
 * this function is called in nwfbisr.asm
 */
void plc1_irq(struct s_smc *smc)
{
	u_short	st = inpw(PLC(PB,PL_INTR_EVENT)) ;

	plc_irq(smc,PB,st) ;
}

/*
 * interrupt source= plc2
 * this function is called in nwfbisr.asm
 */
void plc2_irq(struct s_smc *smc)
{
	u_short	st = inpw(PLC(PA,PL_INTR_EVENT)) ;

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

#ifdef	PCI
	for (i = 0; i < 6; i++) {	/* read mac address from board */
		smc->hw.fddi_phys_addr.a[i] =
			bitrev8(inp(ADDR(B2_MAC_0+i)));
	}
#endif

	ConnectorType = inp(ADDR(B2_CONN_TYP)) ;
	PmdType = inp(ADDR(B2_PMD_TYP)) ;

	smc->y[PA].pmd_type[PMD_SK_CONN] =
	smc->y[PB].pmd_type[PMD_SK_CONN] = ConnectorType ;
	smc->y[PA].pmd_type[PMD_SK_PMD ] =
	smc->y[PB].pmd_type[PMD_SK_PMD ] = PmdType ;

	if (mac_addr) {
		for (i = 0; i < 6 ;i++) {
			smc->hw.fddi_canon_addr.a[i] = mac_addr[i] ;
			smc->hw.fddi_home_addr.a[i] = bitrev8(mac_addr[i]);
		}
		return ;
	}
	smc->hw.fddi_home_addr = smc->hw.fddi_phys_addr ;

	for (i = 0; i < 6 ;i++) {
		smc->hw.fddi_canon_addr.a[i] =
			bitrev8(smc->hw.fddi_phys_addr.a[i]);
	}
}

/*
 * FDDI card soft reset
 */
void init_board(struct s_smc *smc, u_char *mac_addr)
{
	card_start(smc) ;
	read_address(smc,mac_addr) ;

	if (!(inp(ADDR(B0_DAS)) & DAS_AVAIL))
		smc->s.sas = SMT_SAS ;	/* Single att. station */
	else
		smc->s.sas = SMT_DAS ;	/* Dual att. station */

	if (!(inp(ADDR(B0_DAS)) & DAS_BYP_ST))
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
	DB_ECMN(1,"ECM : sm_pm_bypass_req(%s)\n",(mode == BP_INSERT) ?
					"BP_INSERT" : "BP_DEINSERT",0) ;

	if (smc->s.sas != SMT_DAS)
		return ;

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
	return(	(inp(ADDR(B0_DAS)) & DAS_BYP_ST) ? TRUE: FALSE) ;
}

void plc_clear_irq(struct s_smc *smc, int p)
{
	SK_UNUSED(p) ;

	SK_UNUSED(smc) ;
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

void driver_get_bia(struct s_smc *smc, struct fddi_addr *bia_addr)
{
	int i ;

	for (i = 0 ; i < 6 ; i++)
		bia_addr->a[i] = bitrev8(smc->hw.fddi_phys_addr.a[i]);
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

