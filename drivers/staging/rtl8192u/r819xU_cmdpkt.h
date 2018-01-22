/* SPDX-License-Identifier: GPL-2.0 */
#ifndef R819XUSB_CMDPKT_H
#define R819XUSB_CMDPKT_H
/* Different command packet have dedicated message length and definition. */
#define		CMPK_RX_TX_FB_SIZE		sizeof(cmpk_txfb_t)	/* 20 */
#define		CMPK_TX_SET_CONFIG_SIZE		sizeof(cmpk_set_cfg_t)	/* 16 */
#define		CMPK_BOTH_QUERY_CONFIG_SIZE	sizeof(cmpk_set_cfg_t)	/* 16 */
#define		CMPK_RX_TX_STS_SIZE		sizeof(cmpk_tx_status_t)
#define		CMPK_RX_DBG_MSG_SIZE		sizeof(cmpk_rx_dbginfo_t)
#define		CMPK_TX_RAHIS_SIZE		sizeof(cmpk_tx_rahis_t)

/* 2008/05/08 amy For USB constant. */
#define ISR_TxBcnOk		BIT(27)		/* Transmit Beacon OK */
#define ISR_TxBcnErr		BIT(26)		/* Transmit Beacon Error */
#define ISR_BcnTimerIntr	BIT(13)		/* Beacon Timer Interrupt */


/* Define element ID of command packet. */

/*------------------------------Define structure----------------------------*/
/* Define different command packet structure. */
/* 1. RX side: TX feedback packet. */
typedef struct tag_cmd_pkt_tx_feedback {
	/* DWORD 0 */
	u8	element_id;			/* Command packet type. */
	u8	length;				/* Command packet length. */
	/* Change tx feedback info field. */
	/*------TX Feedback Info Field */
	u8	TID:4;
	u8	fail_reason:3;
	u8	tok:1;				/* Transmit ok. */
	u8	reserve1:4;
	u8	pkt_type:2;
	u8	bandwidth:1;
	u8	qos_pkt:1;

	/* DWORD 1 */
	u8	reserve2;
	/*------TX Feedback Info Field */
	u8	retry_cnt;
	u16	pkt_id;

	/* DWORD 3 */
	u16	seq_num;
	u8	s_rate;				/* Start rate. */
	u8	f_rate;				/* Final rate. */

	/* DWORD 4 */
	u8	s_rts_rate;
	u8	f_rts_rate;
	u16	pkt_length;

	/* DWORD 5 */
	u16	reserve3;
	u16	duration;
} cmpk_txfb_t;

/* 2. RX side: Interrupt status packet. It includes Beacon State,
 * Beacon Timer Interrupt and other useful informations in MAC ISR Reg.
 */
typedef struct tag_cmd_pkt_interrupt_status {
	u8	element_id;			/* Command packet type. */
	u8	length;				/* Command packet length. */
	u16	reserve;
	u32	interrupt_status;		/* Interrupt Status. */
} cmpk_intr_sta_t;


/* 3. TX side: Set configuration packet. */
typedef struct tag_cmd_pkt_set_configuration {
	u8	element_id;			/* Command packet type. */
	u8	length;				/* Command packet length. */
	u16	reserve1;
	/* Configuration info. */
	u8	cfg_reserve1:3;
	u8	cfg_size:2;
	u8	cfg_type:2;
	u8	cfg_action:1;
	u8	cfg_reserve2;
	u8	cfg_page:4;
	u8	cfg_reserve3:4;
	u8	cfg_offset;
	u32	value;
	u32	mask;
} cmpk_set_cfg_t;

/* 4. Both side : TX/RX query configuraton packet. The query structure is the
 *    same as set configuration.
 */
#define		cmpk_query_cfg_t	cmpk_set_cfg_t

/* 5. Multi packet feedback status. */
typedef struct tag_tx_stats_feedback {
	/* For endian transfer --> Driver will not the same as
	 *  firmware structure.
	 */
	/* DW 0 */
	u16	reserve1;
	u8	length;				/* Command packet length */
	u8	element_id;			/* Command packet type */

	/* DW 1 */
	u16	txfail;				/* Tx fail count */
	u16	txok;				/* Tx ok count */

	/* DW 2 */
	u16	txmcok;				/* Tx multicast */
	u16	txretry;			/* Tx retry count */

	/* DW 3 */
	u16	txucok;				/* Tx unicast */
	u16	txbcok;				/* Tx broadcast */

	/* DW 4 */
	u16	txbcfail;
	u16	txmcfail;

	/* DW 5 */
	u16	reserve2;
	u16	txucfail;

	/* DW 6-8 */
	u32	txmclength;
	u32	txbclength;
	u32	txuclength;

	/* DW 9 */
	u16	reserve3_23;
	u8	reserve3_1;
	u8	rate;
} __packed cmpk_tx_status_t;

/* 6. Debug feedback message. */
/* Define RX debug message  */
typedef struct tag_rx_debug_message_feedback {
	/* For endian transfer --> for driver */
	/* DW 0 */
	u16	reserve1;
	u8	length;				/* Command packet length */
	u8	element_id;			/* Command packet type */

	/* DW 1-?? */
	/* Variable debug message. */

} cmpk_rx_dbginfo_t;

/* Define transmit rate history. For big endian format. */
typedef struct tag_tx_rate_history {
	/* For endian transfer --> for driver */
	/* DW 0 */
	u8	element_id;			/* Command packet type */
	u8	length;				/* Command packet length */
	u16	reserved1;

	/* DW 1-2	CCK rate counter */
	u16	cck[4];

	/* DW 3-6 */
	u16	ofdm[8];

	/* DW 7-14	BW=0 SG=0
	 * DW 15-22	BW=1 SG=0
	 * DW 23-30	BW=0 SG=1
	 * DW 31-38	BW=1 SG=1
	 */
	u16	ht_mcs[4][16];

} __packed cmpk_tx_rahis_t;

typedef enum tag_command_packet_directories {
	RX_TX_FEEDBACK			= 0,
	RX_INTERRUPT_STATUS		= 1,
	TX_SET_CONFIG			= 2,
	BOTH_QUERY_CONFIG		= 3,
	RX_TX_STATUS			= 4,
	RX_DBGINFO_FEEDBACK		= 5,
	RX_TX_PER_PKT_FEEDBACK		= 6,
	RX_TX_RATE_HISTORY		= 7,
	RX_CMD_ELE_MAX
} cmpk_element_e;

typedef enum _rt_status {
	RT_STATUS_SUCCESS,
	RT_STATUS_FAILURE,
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE
} rt_status, *prt_status;

u32 cmpk_message_handle_rx(struct net_device *dev,
			   struct ieee80211_rx_stats *pstats);
rt_status SendTxCommandPacket(struct net_device *dev,
			      void *pData, u32 DataLen);


#endif
