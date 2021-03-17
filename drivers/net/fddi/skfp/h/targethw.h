/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef	_TARGETHW_
#define _TARGETHW_

	/*
	 *  PCI Watermark definition
	 */
#ifdef	PCI
#define	RX_WATERMARK	24
#define TX_WATERMARK	24
#define SK_ML_ID_1	0x20
#define SK_ML_ID_2	0x30
#endif

#include	"skfbi.h"
#ifndef TAG_MODE	
#include	"fplus.h"
#else
#include	"fplustm.h"
#endif

#ifndef	HW_PTR
#define HW_PTR  void __iomem *
#endif

#ifdef MULT_OEM
#define	OI_STAT_LAST		0	/* end of OEM data base */
#define	OI_STAT_PRESENT		1	/* entry present but not empty */
#define	OI_STAT_VALID		2	/* holds valid ID, but is not active */ 
#define	OI_STAT_ACTIVE		3	/* holds valid ID, entry is active */
					/* active = adapter is supported */

/* Memory representation of IDs must match representation in adapter. */
struct	s_oem_ids {
	u_char	oi_status ;		/* Stat: last, present, valid, active */
	u_char	oi_mark[5] ;		/* "PID00" .. "PID07" ..	*/
	u_char 	oi_id[4] ;		/* id bytes, representation as	*/
					/* defined by hardware,		*/	
#ifdef PCI
	u_char 	oi_sub_id[4] ;		/* sub id bytes, representation as */
					/* defined by hardware,		*/
#endif
} ;
#endif	/* MULT_OEM */


struct s_smt_hw {
	/*
	 * global
	 */
	HW_PTR	iop ;			/* IO base address */
	short	dma ;			/* DMA channel */
	short	irq ;			/* IRQ level */
	short	eprom ;			/* FLASH prom */

#ifndef SYNC
	u_short	n_a_send ;		/* pending send requests */
#endif

#if	defined(PCI)
	short	slot ;			/* slot number */
	short   max_slots ;		/* maximum number of slots */
	short	wdog_used ;		/* TRUE if the watch dog is used */
#endif

#ifdef	PCI
	u_short	pci_handle ;		/* handle to access the BIOS func */
	u_long	is_imask ;		/* int maske for the int source reg */
	u_long	phys_mem_addr ;		/* physical memory address */
	u_short	mc_dummy ;		/* work around for MC compiler bug */	
	/*
	 * state of the hardware
	 */
	u_short hw_state ;		/* started or stopped */

#define	STARTED		1
#define	STOPPED		0

	int	hw_is_64bit ;		/* does we have a 64 bit adapter */
#endif

#ifdef	TAG_MODE
	u_long	pci_fix_value ;		/* value parsed by PCIFIX */
#endif

	/*
	 * hwt.c
	 */
	u_long	t_start ;		/* HWT start */
	u_long	t_stop ;		/* HWT stop */
	u_short	timer_activ ;		/* HWT timer active */

	/*
	 * PIC
	 */
	u_char	pic_a1 ;
	u_char	pic_21 ;

	/*
	 * GENERIC ; do not modify beyond this line
	 */

	/*
	 * physical and canonical address
	 */
	struct fddi_addr fddi_home_addr ;
	struct fddi_addr fddi_canon_addr ;
	struct fddi_addr fddi_phys_addr ;

	/*
	 * mac variables
	 */
	struct mac_parameter mac_pa ;	/* tmin, tmax, tvx, treq .. */
	struct mac_counter mac_ct ;	/* recv., lost, error  */
	u_short	mac_ring_is_up ;	/* ring is up flag */

	struct s_smt_fp	fp ;		/* formac+ */

#ifdef MULT_OEM
	struct s_oem_ids *oem_id ;	/* pointer to selected id */
	int oem_min_status ;		/* IDs to take care of */
#endif	/* MULT_OEM */

} ;
#endif
