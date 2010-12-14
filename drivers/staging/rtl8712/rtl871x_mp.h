#ifndef __RTL871X_MP_H_
#define __RTL871X_MP_H_

/*	00 - Success */
/*	11 - Error */
#define STATUS_SUCCESS			(0x00000000L)
#define STATUS_PENDING			(0x00000103L)
#define STATUS_UNSUCCESSFUL		(0xC0000001L)
#define STATUS_INSUFFICIENT_RESOURCES	(0xC000009AL)
#define STATUS_NOT_SUPPORTED		(0xC00000BBL)
#define NDIS_STATUS_SUCCESS		((uint)STATUS_SUCCESS)
#define NDIS_STATUS_PENDING		((uint) STATUS_PENDING)
#define NDIS_STATUS_NOT_RECOGNIZED	((uint)0x00010001L)
#define NDIS_STATUS_NOT_COPIED		((uint)0x00010002L)
#define NDIS_STATUS_NOT_ACCEPTED	((uint)0x00010003L)
#define NDIS_STATUS_CALL_ACTIVE		((uint)0x00010007L)
#define NDIS_STATUS_FAILURE		((uint) STATUS_UNSUCCESSFUL)
#define NDIS_STATUS_RESOURCES		((uint)\
					STATUS_INSUFFICIENT_RESOURCES)
#define NDIS_STATUS_CLOSING		((uint)0xC0010002L)
#define NDIS_STATUS_BAD_VERSION		((uint)0xC0010004L)
#define NDIS_STATUS_BAD_CHARACTERISTICS	((uint)0xC0010005L)
#define NDIS_STATUS_ADAPTER_NOT_FOUND	((uint)0xC0010006L)
#define NDIS_STATUS_OPEN_FAILED		((uint)0xC0010007L)
#define NDIS_STATUS_DEVICE_FAILED	((uint)0xC0010008L)
#define NDIS_STATUS_MULTICAST_FULL	((uint)0xC0010009L)
#define NDIS_STATUS_MULTICAST_EXISTS	((uint)0xC001000AL)
#define NDIS_STATUS_MULTICAST_NOT_FOUND	((uint)0xC001000BL)
#define NDIS_STATUS_REQUEST_ABORTED	((uint)0xC001000CL)
#define NDIS_STATUS_RESET_IN_PROGRESS	((uint)0xC001000DL)
#define NDIS_STATUS_CLOSING_INDICATING	((uint)0xC001000EL)
#define NDIS_STATUS_NOT_SUPPORTED	((uint)STATUS_NOT_SUPPORTED)
#define NDIS_STATUS_INVALID_PACKET	((uint)0xC001000FL)
#define NDIS_STATUS_OPEN_LIST_FULL	((uint)0xC0010010L)
#define NDIS_STATUS_ADAPTER_NOT_READY	((uint)0xC0010011L)
#define NDIS_STATUS_ADAPTER_NOT_OPEN	((uint)0xC0010012L)
#define NDIS_STATUS_NOT_INDICATING	((uint)0xC0010013L)
#define NDIS_STATUS_INVALID_LENGTH	((uint)0xC0010014L)
#define NDIS_STATUS_INVALID_DATA	((uint)0xC0010015L)
#define NDIS_STATUS_BUFFER_TOO_SHORT	((uint)0xC0010016L)
#define NDIS_STATUS_INVALID_OID		((uint)0xC0010017L)
#define NDIS_STATUS_ADAPTER_REMOVED	((uint)0xC0010018L)
#define NDIS_STATUS_UNSUPPORTED_MEDIA	((uint)0xC0010019L)
#define NDIS_STATUS_GROUP_ADDRESS_IN_USE ((uint)0xC001001AL)
#define NDIS_STATUS_FILE_NOT_FOUND	((uint)0xC001001BL)
#define NDIS_STATUS_ERROR_READING_FILE	((uint)0xC001001CL)
#define NDIS_STATUS_ALREADY_MAPPED	((uint)0xC001001DL)
#define NDIS_STATUS_RESOURCE_CONFLICT	((uint)0xC001001EL)
#define NDIS_STATUS_NO_CABLE		((uint)0xC001001FL)
#define NDIS_STATUS_INVALID_SAP		((uint)0xC0010020L)
#define NDIS_STATUS_SAP_IN_USE		((uint)0xC0010021L)
#define NDIS_STATUS_INVALID_ADDRESS	((uint)0xC0010022L)
#define NDIS_STATUS_VC_NOT_ACTIVATED	((uint)0xC0010023L)
#define NDIS_STATUS_DEST_OUT_OF_ORDER	((uint)0xC0010024L) /* cause 27*/
#define NDIS_STATUS_VC_NOT_AVAILABLE	((uint)0xC0010025L) /* 35,45*/
#define NDIS_STATUS_CELLRATE_NOT_AVAILABLE ((uint)0xC0010026L) /* 37*/
#define NDIS_STATUS_INCOMPATABLE_QOS	((uint)0xC0010027L)  /* 49*/
#define NDIS_STATUS_AAL_PARAMS_UNSUPPORTED ((uint)0xC0010028L)  /*  93*/
#define NDIS_STATUS_NO_ROUTE_TO_DESTINATION ((uint)0xC0010029L)  /*  3*/
#define MPT_NOOP			0
#define MPT_READ_MAC_1BYTE		1
#define MPT_READ_MAC_2BYTE		2
#define MPT_READ_MAC_4BYTE		3
#define MPT_WRITE_MAC_1BYTE		4
#define MPT_WRITE_MAC_2BYTE		5
#define MPT_WRITE_MAC_4BYTE		6
#define MPT_READ_BB_CCK			7
#define MPT_WRITE_BB_CCK		8
#define MPT_READ_BB_OFDM		9
#define MPT_WRITE_BB_OFDM		10
#define MPT_READ_RF			11
#define MPT_WRITE_RF			12
#define MPT_READ_EEPROM_1BYTE		13
#define MPT_WRITE_EEPROM_1BYTE		14
#define MPT_READ_EEPROM_2BYTE		15
#define MPT_WRITE_EEPROM_2BYTE		16
#define MPT_SET_CSTHRESHOLD		21
#define MPT_SET_INITGAIN		22
#define MPT_SWITCH_BAND			23
#define MPT_SWITCH_CHANNEL		24
#define MPT_SET_DATARATE		25
#define MPT_SWITCH_ANTENNA		26
#define MPT_SET_TX_POWER		27
#define MPT_SET_CONT_TX			28
#define MPT_SET_SINGLE_CARRIER		29
#define MPT_SET_CARRIER_SUPPRESSION	30
#define MPT_GET_RATE_TABLE		31
#define MPT_READ_TSSI			32
#define MPT_GET_THERMAL_METER		33
#define MAX_MP_XMITBUF_SZ	2048
#define NR_MP_XMITFRAME		8

struct mp_xmit_frame {
	struct list_head list;
	struct pkt_attrib attrib;
	_pkt *pkt;
	int frame_tag;
	struct _adapter *padapter;
	u8 *mem_addr;
	u16 sz[8];
	struct urb *pxmit_urb[8];
	u8 bpending[8];
	u8 last[8];
	uint mem[(MAX_MP_XMITBUF_SZ >> 2)];
};

struct mp_wiparam {
	u32 bcompleted;
	u32 act_type;
	u32 io_offset;
	u32 io_value;
};

struct mp_priv {
	struct _adapter *papdater;
	/*OID cmd handler*/
	struct mp_wiparam workparam;
	u8 act_in_progress;
	/*Tx Section*/
	u8 TID;
	u32 tx_pktcount;
	/*Rx Section*/
	u32 rx_pktcount;
	u32 rx_crcerrpktcount;
	u32 rx_pktloss;
	struct recv_stat rxstat;
	/*RF/BB relative*/
	u32 curr_ch;
	u32 curr_rateidx;
	u8 curr_bandwidth;
	u8 curr_modem;
	u8 curr_txpoweridx;
	u32 curr_crystalcap;
	u16 antenna_tx;
	u16 antenna_rx;
	u8 curr_rfpath;
	u8 check_mp_pkt;
	uint ForcedDataRate;
	struct wlan_network mp_network;
	unsigned char network_macaddr[6];
	/*Testing Flag*/
	u32 mode;/*0 for normal type packet,
		  * 1 for loopback packet (16bytes TXCMD)*/
	sint prev_fw_state;
	u8 *pallocated_mp_xmitframe_buf;
	u8 *pmp_xmtframe_buf;
	struct  __queue free_mp_xmitqueue;
	u32 free_mp_xmitframe_cnt;
};

struct IOCMD_STRUCT {
	u8	cmdclass;
	u16	value;
	u8	index;
};

struct rf_reg_param {
	u32 path;
	u32 offset;
	u32 value;
};

struct bb_reg_param {
	u32 offset;
	u32 value;
};
/* ======================================================================= */

#define LOWER	true
#define RAISE	false
#define IOCMD_CTRL_REG			0x10250370
#define IOCMD_DATA_REG			0x10250374
#define IOCMD_GET_THERMAL_METER		0xFD000028
#define IOCMD_CLASS_BB_RF		0xF0
#define IOCMD_BB_READ_IDX		0x00
#define IOCMD_BB_WRITE_IDX		0x01
#define IOCMD_RF_READ_IDX		0x02
#define IOCMD_RF_WRIT_IDX		0x03
#define BB_REG_BASE_ADDR		0x800
#define RF_PATH_A	0
#define RF_PATH_B	1
#define RF_PATH_C	2
#define RF_PATH_D	3
#define MAX_RF_PATH_NUMS	2
#define _2MAC_MODE_	0
#define _LOOPBOOK_MODE_	1

/* MP set force data rate base on the definition. */
enum {
	/* CCK rate. */
	MPT_RATE_1M,	/* 0 */
	MPT_RATE_2M,
	MPT_RATE_55M,
	MPT_RATE_11M,	/* 3 */

	/* OFDM rate. */
	MPT_RATE_6M,	/* 4 */
	MPT_RATE_9M,
	MPT_RATE_12M,
	MPT_RATE_18M,
	MPT_RATE_24M,
	MPT_RATE_36M,
	MPT_RATE_48M,
	MPT_RATE_54M,	/* 11 */

	/* HT rate. */
	MPT_RATE_MCS0,	/* 12 */
	MPT_RATE_MCS1,
	MPT_RATE_MCS2,
	MPT_RATE_MCS3,
	MPT_RATE_MCS4,
	MPT_RATE_MCS5,
	MPT_RATE_MCS6,
	MPT_RATE_MCS7,	/* 19 */
	MPT_RATE_MCS8,
	MPT_RATE_MCS9,
	MPT_RATE_MCS10,
	MPT_RATE_MCS11,
	MPT_RATE_MCS12,
	MPT_RATE_MCS13,
	MPT_RATE_MCS14,
	MPT_RATE_MCS15,	/* 27 */
	MPT_RATE_LAST
};

/* Represent Channel Width in HT Capabilities */
enum HT_CHANNEL_WIDTH {
	HT_CHANNEL_WIDTH_20 = 0,
	HT_CHANNEL_WIDTH_40 = 1,
};

#define MAX_TX_PWR_INDEX_N_MODE 64	/* 0x3F */

enum POWER_MODE {
	POWER_LOW = 0,
	POWER_NORMAL
};

#define RX_PKT_BROADCAST	1
#define RX_PKT_DEST_ADDR	2
#define RX_PKT_PHY_MATCH	3

#define RPTMaxCount 0x000FFFFF;

/* parameter 1 : BitMask
 *	bit 0  : OFDM PPDU
 *	bit 1  : OFDM False Alarm
 *	bit 2  : OFDM MPDU OK
 *	bit 3  : OFDM MPDU Fail
 *	bit 4  : CCK PPDU
 *	bit 5  : CCK False Alarm
 *	bit 6  : CCK MPDU ok
 *	bit 7  : CCK MPDU fail
 *	bit 8  : HT PPDU counter
 *	bit 9  : HT false alarm
 *	bit 10 : HT MPDU total
 *	bit 11 : HT MPDU OK
 *	bit 12 : HT MPDU fail
 *	bit 15 : RX full drop
 */
enum RXPHY_BITMASK {
	OFDM_PPDU_BIT = 0,
	OFDM_MPDU_OK_BIT,
	OFDM_MPDU_FAIL_BIT,
	CCK_PPDU_BIT,
	CCK_MPDU_OK_BIT,
	CCK_MPDU_FAIL_BIT,
	HT_PPDU_BIT,
	HT_MPDU_BIT,
	HT_MPDU_OK_BIT,
	HT_MPDU_FAIL_BIT,
};

enum ENCRY_CTRL_STATE {
	HW_CONTROL,		/*hw encryption& decryption*/
	SW_CONTROL,		/*sw encryption& decryption*/
	HW_ENCRY_SW_DECRY,	/*hw encryption & sw decryption*/
	SW_ENCRY_HW_DECRY	/*sw encryption & hw decryption*/
};

/* Bandwidth Offset */
#define HAL_PRIME_CHNL_OFFSET_DONT_CARE	0
#define HAL_PRIME_CHNL_OFFSET_LOWER	1
#define HAL_PRIME_CHNL_OFFSET_UPPER	2
/*=======================================================================*/
void mp871xinit(struct _adapter *padapter);
void mp871xdeinit(struct _adapter *padapter);
u32 r8712_bb_reg_read(struct _adapter *Adapter, u16 offset);
u8 r8712_bb_reg_write(struct _adapter *Adapter, u16 offset, u32 value);
u32 r8712_rf_reg_read(struct _adapter *Adapter, u8 path, u8 offset);
u8 r8712_rf_reg_write(struct _adapter *Adapter, u8 path,
		      u8 offset, u32 value);
u32 r8712_get_bb_reg(struct _adapter *Adapter, u16 offset, u32 bitmask);
u8 r8712_set_bb_reg(struct _adapter *Adapter, u16 offset,
		    u32 bitmask, u32 value);
u32 r8712_get_rf_reg(struct _adapter *Adapter, u8 path, u8 offset,
		     u32 bitmask);
u8 r8712_set_rf_reg(struct _adapter *Adapter, u8 path, u8 offset,
		    u32 bitmask, u32 value);

void r8712_SetChannel(struct _adapter *pAdapter);
void r8712_SetTxPower(struct _adapter *pAdapte);
void r8712_SetTxAGCOffset(struct _adapter *pAdapter, u32 ulTxAGCOffset);
void r8712_SetDataRate(struct _adapter *pAdapter);
void r8712_SwitchBandwidth(struct _adapter *pAdapter);
void r8712_SwitchAntenna(struct _adapter *pAdapter);
void r8712_SetCrystalCap(struct _adapter *pAdapter);
void r8712_GetThermalMeter(struct _adapter *pAdapter, u32 *value);
void r8712_SetContinuousTx(struct _adapter *pAdapter, u8 bStart);
void r8712_SetSingleCarrierTx(struct _adapter *pAdapter, u8 bStart);
void r8712_SetSingleToneTx(struct _adapter *pAdapter, u8 bStart);
void r8712_SetCarrierSuppressionTx(struct _adapter *pAdapter, u8 bStart);
void r8712_ResetPhyRxPktCount(struct _adapter *pAdapter);
u32 r8712_GetPhyRxPktReceived(struct _adapter *pAdapter);
u32 r8712_GetPhyRxPktCRC32Error(struct _adapter *pAdapter);

#endif /*__RTL871X_MP_H_*/

