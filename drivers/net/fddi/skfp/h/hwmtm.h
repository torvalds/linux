/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef	_HWM_
#define	_HWM_

#include "mbuf.h"

/*
 * MACRO for DMA synchronization:
 *	The descriptor 'desc' is flushed for the device 'flag'.
 *	Devices are the CPU (DDI_DMA_SYNC_FORCPU) and the
 *	adapter (DDI_DMA_SYNC_FORDEV).
 *
 *	'desc'	Pointer to a Rx or Tx descriptor.
 *	'flag'	Flag for direction (view for CPU or DEVICE) that
 *		should be synchronized.
 *
 *	Empty macros and defines are specified here. The real macro
 *	is os-specific and should be defined in osdef1st.h.
 */
#ifndef DRV_BUF_FLUSH
#define DRV_BUF_FLUSH(desc,flag)
#define DDI_DMA_SYNC_FORCPU
#define DDI_DMA_SYNC_FORDEV
#endif

	/*
	 * hardware modul dependent receive modes
	 */
#define	RX_ENABLE_PASS_SMT	21
#define	RX_DISABLE_PASS_SMT	22
#define	RX_ENABLE_PASS_NSA	23
#define	RX_DISABLE_PASS_NSA	24
#define	RX_ENABLE_PASS_DB	25
#define	RX_DISABLE_PASS_DB	26
#define	RX_DISABLE_PASS_ALL	27
#define	RX_DISABLE_LLC_PROMISC	28
#define	RX_ENABLE_LLC_PROMISC	29


#ifndef	DMA_RD
#define DMA_RD		1	/* memory -> hw */
#endif
#ifndef DMA_WR
#define DMA_WR		2	/* hw -> memory */
#endif
#define SMT_BUF		0x80

	/*
	 * bits of the frame status byte
	 */
#define EN_IRQ_EOF	0x02	/* get IRQ after end of frame transmission */
#define	LOC_TX		0x04	/* send frame to the local SMT */
#define LAST_FRAG	0x08	/* last TxD of the frame */
#define	FIRST_FRAG	0x10	/* first TxD of the frame */
#define	LAN_TX		0x20	/* send frame to network if set */
#define RING_DOWN	0x40	/* error: unable to send, ring down */
#define OUT_OF_TXD	0x80	/* error: not enough TxDs available */


#ifndef NULL
#define NULL 		0
#endif

#define C_INDIC		(1L<<25)
#define A_INDIC		(1L<<26)
#define	RD_FS_LOCAL	0x80

	/*
	 * DEBUG FLAGS
	 */
#define	DEBUG_SMTF	1
#define	DEBUG_SMT	2
#define	DEBUG_ECM	3
#define	DEBUG_RMT	4
#define	DEBUG_CFM	5
#define	DEBUG_PCM	6
#define	DEBUG_SBA	7
#define	DEBUG_ESS	8

#define	DB_HWM_RX	10
#define	DB_HWM_TX	11
#define DB_HWM_GEN	12

struct s_mbuf_pool {
#ifndef	MB_OUTSIDE_SMC
	SMbuf		mb[MAX_MBUF] ;		/* mbuf pool */
#endif
	SMbuf		*mb_start ;		/* points to the first mb */
	SMbuf		*mb_free ;		/* free queue */
} ;

struct hwm_r {
	/*
	 * hardware modul specific receive variables
	 */
	u_int			len ;		/* length of the whole frame */
	char			*mb_pos ;	/* SMbuf receive position */
} ;

struct hw_modul {
	/*
	 * All hardware modul specific variables
	 */
	struct	s_mbuf_pool	mbuf_pool ;
	struct	hwm_r	r ;

	union s_fp_descr volatile *descr_p ; /* points to the desriptor area */

	u_short pass_SMT ;		/* pass SMT frames */
	u_short pass_NSA ;		/* pass all NSA frames */
	u_short pass_DB ;		/* pass Direct Beacon Frames */
	u_short pass_llc_promisc ;	/* pass all llc frames (default ON) */

	SMbuf	*llc_rx_pipe ;		/* points to the first queued llc fr */
	SMbuf	*llc_rx_tail ;		/* points to the last queued llc fr */
	int	queued_rx_frames ;	/* number of queued frames */

	SMbuf	*txd_tx_pipe ;		/* points to first mb in the txd ring */
	SMbuf	*txd_tx_tail ;		/* points to last mb in the txd ring */
	int	queued_txd_mb ;		/* number of SMT MBufs in txd ring */

	int	rx_break ;		/* rev. was breaked because ind. off */
	int	leave_isr ;		/* leave fddi_isr immedeately if set */
	int	isr_flag ;		/* set, when HWM is entered from isr */
	/*
	 * variables for the current transmit frame
	 */
	struct s_smt_tx_queue *tx_p ;	/* pointer to the transmit queue */
	u_long	tx_descr ;		/* tx descriptor for FORMAC+ */
	int	tx_len ;		/* tx frame length */
	SMbuf	*tx_mb ;		/* SMT tx MBuf pointer */
	char	*tx_data ;		/* data pointer to the SMT tx Mbuf */

	int	detec_count ;		/* counter for out of RxD condition */
	u_long	rx_len_error ;		/* rx len FORMAC != sum of fragments */
} ;


/*
 * DEBUG structs and macros
 */

#ifdef	DEBUG
struct os_debug {
	int	hwm_rx ;
	int	hwm_tx ;
	int	hwm_gen ;
} ;
#endif

#ifdef	DEBUG
#ifdef	DEBUG_BRD
#define	DB_P	smc->debug
#else
#define DB_P	debug
#endif

#define DB_RX(lev, fmt, ...)						\
do {									\
	if (DB_P.d_os.hwm_rx >= (lev))					\
		printf(fmt "\n", ##__VA_ARGS__);			\
} while (0)
#define DB_TX(lev, fmt, ...)						\
do {									\
	if (DB_P.d_os.hwm_tx >= (lev))					\
		printf(fmt "\n", ##__VA_ARGS__);			\
} while (0)
#define DB_GEN(lev, fmt, ...)						\
do {									\
	if (DB_P.d_os.hwm_gen >= (lev))					\
		printf(fmt "\n", ##__VA_ARGS__);			\
} while (0)
#else	/* DEBUG */
#define DB_RX(lev, fmt, ...)	no_printk(fmt "\n", ##__VA_ARGS__)
#define DB_TX(lev, fmt, ...)	no_printk(fmt "\n", ##__VA_ARGS__)
#define DB_GEN(lev, fmt, ...)	no_printk(fmt "\n", ##__VA_ARGS__)
#endif	/* DEBUG */

#ifndef	SK_BREAK
#define	SK_BREAK()
#endif


/*
 * HWM Macros
 */

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_TX_PHYS)
 *	u_long HWM_GET_TX_PHYS(txd)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to read
 *		the physical address of the specified TxD.
 *
 * para	txd	pointer to the TxD
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_TX_PHYS(txd)		(u_long)AIX_REVERSE((txd)->txd_tbadr)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_TX_LEN)
 *	int HWM_GET_TX_LEN(txd)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to read
 *		the fragment length of the specified TxD
 *
 * para	rxd	pointer to the TxD
 *
 * return	the length of the fragment in bytes
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_TX_LEN(txd)	((int)AIX_REVERSE((txd)->txd_tbctrl)& RD_LENGTH)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_TX_USED)
 *	txd *HWM_GET_TX_USED(smc,queue)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to get the
 *		number of used TxDs for the queue, specified by the index.
 *
 * para	queue	the number of the send queue: Can be specified by
 *		QUEUE_A0, QUEUE_S or (frame_status & QUEUE_A0)
 *
 * return	number of used TxDs for this send queue
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_TX_USED(smc,queue)	(int) (smc)->hw.fp.tx_q[queue].tx_used

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_CURR_TXD)
 *	txd *HWM_GET_CURR_TXD(smc,queue)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to get the
 *		pointer to the TxD which points to the current queue put
 *		position.
 *
 * para	queue	the number of the send queue: Can be specified by
 *		QUEUE_A0, QUEUE_S or (frame_status & QUEUE_A0)
 *
 * return	pointer to the current TxD
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_CURR_TXD(smc,queue)	(struct s_smt_fp_txd volatile *)\
					(smc)->hw.fp.tx_q[queue].tx_curr_put

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_RX_FRAG_LEN)
 *	int HWM_GET_RX_FRAG_LEN(rxd)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to read
 *		the fragment length of the specified RxD
 *
 * para	rxd	pointer to the RxD
 *
 * return	the length of the fragment in bytes
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_RX_FRAG_LEN(rxd)	((int)AIX_REVERSE((rxd)->rxd_rbctrl)& \
				RD_LENGTH)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_RX_PHYS)
 *	u_long HWM_GET_RX_PHYS(rxd)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to read
 *		the physical address of the specified RxD.
 *
 * para	rxd	pointer to the RxD
 *
 * return	the RxD's physical pointer to the data fragment
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_RX_PHYS(rxd)	(u_long)AIX_REVERSE((rxd)->rxd_rbadr)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_RX_USED)
 *	int HWM_GET_RX_USED(smc)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to get
 *		the count of used RXDs in receive queue 1.
 *
 * return	the used RXD count of receive queue 1
 *
 * NOTE: Remember, because of an ASIC bug at least one RXD should be unused
 *	 in the descriptor ring !
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_RX_USED(smc)	((int)(smc)->hw.fp.rx_q[QUEUE_R1].rx_used)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_RX_FREE)
 *	int HWM_GET_RX_FREE(smc)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to get
 *		the rxd_free count of receive queue 1.
 *
 * return	the rxd_free count of receive queue 1
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_RX_FREE(smc)	((int)(smc)->hw.fp.rx_q[QUEUE_R1].rx_free-1)

/*
 *	BEGIN_MANUAL_ENTRY(HWM_GET_CURR_RXD)
 *	rxd *HWM_GET_CURR_RXD(smc)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro may be invoked by the OS-specific module to get the
 *		pointer to the RxD which points to the current queue put
 *		position.
 *
 * return	pointer to the current RxD
 *
 *	END_MANUAL_ENTRY
 */
#define	HWM_GET_CURR_RXD(smc)	(struct s_smt_fp_rxd volatile *)\
				(smc)->hw.fp.rx_q[QUEUE_R1].rx_curr_put

/*
 *	BEGIN_MANUAL_ENTRY(HWM_RX_CHECK)
 *	void HWM_RX_CHECK(smc,low_water)
 *
 * function	MACRO		(hardware module, hwmtm.h)
 *		This macro is invoked by the OS-specific before it left the
 *		function mac_drv_rx_complete. This macro calls mac_drv_fill_rxd
 *		if the number of used RxDs is equal or lower than the
 *		the given low water mark.
 *
 * para	low_water	low water mark of used RxD's
 *
 *	END_MANUAL_ENTRY
 */
#ifndef HWM_NO_FLOW_CTL
#define	HWM_RX_CHECK(smc,low_water) {\
	if ((low_water) >= (smc)->hw.fp.rx_q[QUEUE_R1].rx_used) {\
		mac_drv_fill_rxd(smc) ;\
	}\
}
#else
#define	HWM_RX_CHECK(smc,low_water)		mac_drv_fill_rxd(smc)
#endif

#ifndef	HWM_EBASE
#define	HWM_EBASE	500
#endif

#define	HWM_E0001	HWM_EBASE + 1
#define	HWM_E0001_MSG	"HWM: Wrong size of s_rxd_os struct"
#define	HWM_E0002	HWM_EBASE + 2
#define	HWM_E0002_MSG	"HWM: Wrong size of s_txd_os struct"
#define	HWM_E0003	HWM_EBASE + 3
#define	HWM_E0003_MSG	"HWM: smt_free_mbuf() called with NULL pointer"
#define	HWM_E0004	HWM_EBASE + 4
#define	HWM_E0004_MSG	"HWM: Parity error rx queue 1"
#define	HWM_E0005	HWM_EBASE + 5
#define	HWM_E0005_MSG	"HWM: Encoding error rx queue 1"
#define	HWM_E0006	HWM_EBASE + 6
#define	HWM_E0006_MSG	"HWM: Encoding error async tx queue"
#define	HWM_E0007	HWM_EBASE + 7
#define	HWM_E0007_MSG	"HWM: Encoding error sync tx queue"
#define	HWM_E0008	HWM_EBASE + 8
#define	HWM_E0008_MSG	""
#define	HWM_E0009	HWM_EBASE + 9
#define	HWM_E0009_MSG	"HWM: Out of RxD condition detected"
#define	HWM_E0010	HWM_EBASE + 10
#define	HWM_E0010_MSG	"HWM: A protocol layer has tried to send a frame with an invalid frame control"
#define HWM_E0011	HWM_EBASE + 11
#define HWM_E0011_MSG	"HWM: mac_drv_clear_tx_queue was called although the hardware wasn't stopped"
#define HWM_E0012	HWM_EBASE + 12
#define HWM_E0012_MSG	"HWM: mac_drv_clear_rx_queue was called although the hardware wasn't stopped"
#define HWM_E0013	HWM_EBASE + 13
#define HWM_E0013_MSG	"HWM: mac_drv_repair_descr was called although the hardware wasn't stopped"

#endif
