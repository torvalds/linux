/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
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
 *	AMD Fplus in tag mode data structs
 *	defs for fplustm.c
 */

#ifndef	_FPLUS_
#define _FPLUS_

#ifndef	HW_PTR
#define	HW_PTR	void __iomem *
#endif

/*
 * fplus error statistic structure
 */
struct err_st {
	u_long err_valid ;		/* memory status valid */
	u_long err_abort ;		/* memory status receive abort */
	u_long err_e_indicator ;	/* error indicator */
	u_long err_crc ;		/* error detected (CRC or length) */
	u_long err_llc_frame ;		/* LLC frame */
	u_long err_mac_frame ;		/* MAC frame */
	u_long err_smt_frame ;		/* SMT frame */
	u_long err_imp_frame ;		/* implementer frame */
	u_long err_no_buf ;		/* no buffer available */
	u_long err_too_long ;		/* longer than max. buffer */
	u_long err_bec_stat ;		/* beacon state entered */
	u_long err_clm_stat ;		/* claim state entered */
	u_long err_sifg_det ;		/* short interframe gap detect */
	u_long err_phinv ;		/* PHY invalid */
	u_long err_tkiss ;		/* token issued */
	u_long err_tkerr ;		/* token error */
} ;

/*
 *	Transmit Descriptor struct
 */
struct s_smt_fp_txd {
	u_int txd_tbctrl ;		/* transmit buffer control */
	u_int txd_txdscr ;		/* transmit frame status word */
	u_int txd_tbadr ;		/* physical tx buffer address */
	u_int txd_ntdadr ;		/* physical pointer to the next TxD */
#ifdef	ENA_64BIT_SUP
	u_int txd_tbadr_hi ;		/* physical tx buffer addr (high dword)*/
#endif
	char far *txd_virt ;		/* virtual pointer to the data frag */
					/* virt pointer to the next TxD */
	struct s_smt_fp_txd volatile far *txd_next ;
	struct s_txd_os txd_os ;	/* OS - specific struct */
} ;

/*
 *	Receive Descriptor struct
 */
struct s_smt_fp_rxd {
	u_int rxd_rbctrl ;		/* receive buffer control */
	u_int rxd_rfsw ;		/* receive frame status word */
	u_int rxd_rbadr ;		/* physical rx buffer address */
	u_int rxd_nrdadr ;		/* physical pointer to the next RxD */
#ifdef	ENA_64BIT_SUP
	u_int rxd_rbadr_hi ;		/* physical tx buffer addr (high dword)*/
#endif
	char far *rxd_virt ;		/* virtual pointer to the data frag */
					/* virt pointer to the next RxD */
	struct s_smt_fp_rxd volatile far *rxd_next ;
	struct s_rxd_os rxd_os ;	/* OS - specific struct */
} ;

/*
 *	Descriptor Union Definition
 */
union s_fp_descr {
	struct	s_smt_fp_txd t ;		/* pointer to the TxD */
	struct	s_smt_fp_rxd r ;		/* pointer to the RxD */
} ;

/*
 *	TxD Ring Control struct
 */
struct s_smt_tx_queue {
	struct s_smt_fp_txd volatile *tx_curr_put ; /* next free TxD */
	struct s_smt_fp_txd volatile *tx_prev_put ; /* shadow put pointer */
	struct s_smt_fp_txd volatile *tx_curr_get ; /* next TxD to release*/
	u_short tx_free ;			/* count of free TxD's */
	u_short tx_used ;			/* count of used TxD's */
	HW_PTR tx_bmu_ctl ;			/* BMU addr for tx start */
	HW_PTR tx_bmu_dsc ;			/* BMU addr for curr dsc. */
} ;

/*
 *	RxD Ring Control struct
 */
struct s_smt_rx_queue {
	struct s_smt_fp_rxd volatile *rx_curr_put ; /* next RxD to queue into */
	struct s_smt_fp_rxd volatile *rx_prev_put ; /* shadow put pointer */
	struct s_smt_fp_rxd volatile *rx_curr_get ; /* next RxD to fill */
	u_short rx_free ;			/* count of free RxD's */
	u_short rx_used ;			/* count of used RxD's */
	HW_PTR rx_bmu_ctl ;			/* BMU addr for rx start */
	HW_PTR rx_bmu_dsc ;			/* BMU addr for curr dsc. */
} ;

#define VOID_FRAME_OFF		0x00
#define CLAIM_FRAME_OFF		0x08
#define BEACON_FRAME_OFF	0x10
#define DBEACON_FRAME_OFF	0x18
#define RX_FIFO_OFF		0x21		/* to get a prime number for */
						/* the RX_FIFO_SPACE */

#define RBC_MEM_SIZE		0x8000
#define SEND_ASYNC_AS_SYNC	0x1
#define	SYNC_TRAFFIC_ON		0x2

/* big FIFO memory */
#define	RX_FIFO_SPACE		0x4000 - RX_FIFO_OFF
#define	TX_FIFO_SPACE		0x4000

#define	TX_SMALL_FIFO		0x0900
#define	TX_MEDIUM_FIFO		TX_FIFO_SPACE / 2	
#define	TX_LARGE_FIFO		TX_FIFO_SPACE - TX_SMALL_FIFO	

#define	RX_SMALL_FIFO		0x0900
#define	RX_LARGE_FIFO		RX_FIFO_SPACE - RX_SMALL_FIFO	

struct s_smt_fifo_conf {
	u_short	rbc_ram_start ;		/* FIFO start address */
	u_short	rbc_ram_end ;		/* FIFO size */
	u_short	rx1_fifo_start ;	/* rx queue start address */
	u_short	rx1_fifo_size ;		/* rx queue size */
	u_short	rx2_fifo_start ;	/* rx queue start address */
	u_short	rx2_fifo_size ;		/* rx queue size */
	u_short	tx_s_start ;		/* sync queue start address */
	u_short	tx_s_size ;		/* sync queue size */
	u_short	tx_a0_start ;		/* async queue A0 start address */
	u_short	tx_a0_size ;		/* async queue A0 size */
	u_short	fifo_config_mode ;	/* FIFO configuration mode */
} ;

#define FM_ADDRX	(FM_ADDET|FM_EXGPA0|FM_EXGPA1)

struct s_smt_fp {
	u_short	mdr2init ;		/* mode register 2 init value */
	u_short	mdr3init ;		/* mode register 3 init value */
	u_short frselreg_init ;		/* frame selection register init val */
	u_short	rx_mode ;		/* address mode broad/multi/promisc */
	u_short	nsa_mode ;
	u_short rx_prom ;
	u_short	exgpa ;

	struct err_st err_stats ;	/* error statistics */

	/*
	 * MAC buffers
	 */
	struct fddi_mac_sf {		/* special frame build buffer */
		u_char			mac_fc ;
		struct fddi_addr	mac_dest ;
		struct fddi_addr	mac_source ;
		u_char			mac_info[0x20] ;
	} mac_sfb ;


	/*
	 * queues
	 */
#define QUEUE_S			0
#define QUEUE_A0		1
#define QUEUE_R1		0
#define QUEUE_R2		1
#define USED_QUEUES		2

	/*
	 * queue pointers; points to the queue dependent variables
	 */
	struct s_smt_tx_queue *tx[USED_QUEUES] ;
	struct s_smt_rx_queue *rx[USED_QUEUES] ;

	/*
	 * queue dependent variables
	 */
	struct s_smt_tx_queue tx_q[USED_QUEUES] ;
	struct s_smt_rx_queue rx_q[USED_QUEUES] ;

	/*
	 * FIFO configuration struct
	 */
	struct	s_smt_fifo_conf	fifo ;

	/* last formac status */
	u_short	 s2u ;
	u_short	 s2l ;

	/* calculated FORMAC+ reg.addr. */
	HW_PTR	fm_st1u ;
	HW_PTR	fm_st1l ;
	HW_PTR	fm_st2u ;
	HW_PTR	fm_st2l ;
	HW_PTR	fm_st3u ;
	HW_PTR	fm_st3l ;


	/*
	 * multicast table
	 */
#define FPMAX_MULTICAST 32 
#define	SMT_MAX_MULTI	4
	struct {
		struct s_fpmc {
			struct fddi_addr	a ;	/* mc address */
			u_char			n ;	/* usage counter */
			u_char			perm ;	/* flag: permanent */
		} table[FPMAX_MULTICAST] ;
	} mc ;
	struct fddi_addr	group_addr ;
	u_long	func_addr ;		/* functional address */
	int	smt_slots_used ;	/* count of table entries for the SMT */
	int	os_slots_used ;		/* count of table entries */ 
					/* used by the os-specific module */
} ;

/*
 * modes for mac_set_rx_mode()
 */
#define RX_ENABLE_ALLMULTI	1	/* enable all multicasts */
#define RX_DISABLE_ALLMULTI	2	/* disable "enable all multicasts" */
#define RX_ENABLE_PROMISC	3	/* enable promiscous */
#define RX_DISABLE_PROMISC	4	/* disable promiscous */
#define RX_ENABLE_NSA		5	/* enable reception of NSA frames */
#define RX_DISABLE_NSA		6	/* disable reception of NSA frames */


/*
 * support for byte reversal in AIX
 * (descriptors and pointers must be byte reversed in memory
 *  CPU is big endian; M-Channel is little endian)
 */
#ifdef	AIX
#define MDR_REV
#define	AIX_REVERSE(x)		((((x)<<24L)&0xff000000L)	+	\
				 (((x)<< 8L)&0x00ff0000L)	+	\
				 (((x)>> 8L)&0x0000ff00L)	+	\
				 (((x)>>24L)&0x000000ffL))
#else
#ifndef AIX_REVERSE
#define	AIX_REVERSE(x)	(x)
#endif
#endif

#ifdef	MDR_REV	
#define	MDR_REVERSE(x)		((((x)<<24L)&0xff000000L)	+	\
				 (((x)<< 8L)&0x00ff0000L)	+	\
				 (((x)>> 8L)&0x0000ff00L)	+	\
				 (((x)>>24L)&0x000000ffL))
#else
#ifndef MDR_REVERSE
#define	MDR_REVERSE(x)	(x)
#endif
#endif

#endif
