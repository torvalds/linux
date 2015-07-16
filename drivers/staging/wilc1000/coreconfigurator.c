
/*!
 *  @file	coreconfigurator.c
 *  @brief
 *  @author
 *  @sa		coreconfigurator.h
 *  @date	1 Mar 2012
 *  @version	1.0
 */


/*****************************************************************************/
/* File Includes                                                             */
/*****************************************************************************/
#include "coreconfigurator.h"
/*****************************************************************************/
/* Constants                                                                 */
/*****************************************************************************/
#define INLINE static __inline
#define PHY_802_11n
#define MAX_CFG_PKTLEN     1450
#define MSG_HEADER_LEN     4
#define QUERY_MSG_TYPE     'Q'
#define WRITE_MSG_TYPE     'W'
#define RESP_MSG_TYPE      'R'
#define WRITE_RESP_SUCCESS 1
#define INVALID         255
#define MAC_ADDR_LEN    6
#define TAG_PARAM_OFFSET	(MAC_HDR_LEN + TIME_STAMP_LEN + \
							BEACON_INTERVAL_LEN + CAP_INFO_LEN)

/*****************************************************************************/
/* Function Macros                                                           */
/*****************************************************************************/


/*****************************************************************************/
/* Type Definitions                                                          */
/*****************************************************************************/

/* Basic Frame Type Codes (2-bit) */
typedef enum {
	FRAME_TYPE_CONTROL     = 0x04,
	FRAME_TYPE_DATA        = 0x08,
	FRAME_TYPE_MANAGEMENT  = 0x00,
	FRAME_TYPE_RESERVED    = 0x0C,
	FRAME_TYPE_FORCE_32BIT = 0xFFFFFFFF
} tenuBasicFrmType;

/* Frame Type and Subtype Codes (6-bit) */
typedef enum {
	ASSOC_REQ             = 0x00,
	ASSOC_RSP             = 0x10,
	REASSOC_REQ           = 0x20,
	REASSOC_RSP           = 0x30,
	PROBE_REQ             = 0x40,
	PROBE_RSP             = 0x50,
	BEACON                = 0x80,
	ATIM                  = 0x90,
	DISASOC               = 0xA0,
	AUTH                  = 0xB0,
	DEAUTH                = 0xC0,
	ACTION                = 0xD0,
	PS_POLL               = 0xA4,
	RTS                   = 0xB4,
	CTS                   = 0xC4,
	ACK                   = 0xD4,
	CFEND                 = 0xE4,
	CFEND_ACK             = 0xF4,
	DATA                  = 0x08,
	DATA_ACK              = 0x18,
	DATA_POLL             = 0x28,
	DATA_POLL_ACK         = 0x38,
	NULL_FRAME            = 0x48,
	CFACK                 = 0x58,
	CFPOLL                = 0x68,
	CFPOLL_ACK            = 0x78,
	QOS_DATA              = 0x88,
	QOS_DATA_ACK          = 0x98,
	QOS_DATA_POLL         = 0xA8,
	QOS_DATA_POLL_ACK     = 0xB8,
	QOS_NULL_FRAME        = 0xC8,
	QOS_CFPOLL            = 0xE8,
	QOS_CFPOLL_ACK        = 0xF8,
	BLOCKACK_REQ          = 0x84,
	BLOCKACK              = 0x94,
	FRAME_SUBTYPE_FORCE_32BIT  = 0xFFFFFFFF
} tenuFrmSubtype;

/* Basic Frame Classes */
typedef enum {
	CLASS1_FRAME_TYPE      = 0x00,
	CLASS2_FRAME_TYPE      = 0x01,
	CLASS3_FRAME_TYPE      = 0x02,
	FRAME_CLASS_FORCE_32BIT  = 0xFFFFFFFF
} tenuFrameClass;

/* Element ID  of various Information Elements */
typedef enum {
	ISSID               = 0,   /* Service Set Identifier         */
	ISUPRATES           = 1,   /* Supported Rates                */
	IFHPARMS            = 2,   /* FH parameter set               */
	IDSPARMS            = 3,   /* DS parameter set               */
	ICFPARMS            = 4,   /* CF parameter set               */
	ITIM                = 5,   /* Traffic Information Map        */
	IIBPARMS            = 6,   /* IBSS parameter set             */
	ICOUNTRY            = 7,   /* Country element                */
	IEDCAPARAMS         = 12,  /* EDCA parameter set             */
	ITSPEC              = 13,  /* Traffic Specification          */
	ITCLAS              = 14,  /* Traffic Classification         */
	ISCHED              = 15,  /* Schedule                       */
	ICTEXT              = 16,  /* Challenge Text                 */
	IPOWERCONSTRAINT    = 32,  /* Power Constraint               */
	IPOWERCAPABILITY    = 33,  /* Power Capability               */
	ITPCREQUEST         = 34,  /* TPC Request                    */
	ITPCREPORT          = 35,  /* TPC Report                     */
	ISUPCHANNEL         = 36,  /* Supported channel list         */
	ICHSWANNOUNC        = 37,  /* Channel Switch Announcement    */
	IMEASUREMENTREQUEST = 38,  /* Measurement request            */
	IMEASUREMENTREPORT  = 39,  /* Measurement report             */
	IQUIET              = 40,  /* Quiet element Info             */
	IIBSSDFS            = 41,  /* IBSS DFS                       */
	IERPINFO            = 42,  /* ERP Information                */
	ITSDELAY            = 43,  /* TS Delay                       */
	ITCLASPROCESS       = 44,  /* TCLAS Processing               */
	IHTCAP              = 45,  /* HT Capabilities                */
	IQOSCAP             = 46,  /* QoS Capability                 */
	IRSNELEMENT         = 48,  /* RSN Information Element        */
	IEXSUPRATES         = 50,  /* Extended Supported Rates       */
	IEXCHSWANNOUNC      = 60,  /* Extended Ch Switch Announcement*/
	IHTOPERATION        = 61,  /* HT Information                 */
	ISECCHOFF           = 62,  /* Secondary Channel Offeset      */
	I2040COEX           = 72,  /* 20/40 Coexistence IE           */
	I2040INTOLCHREPORT  = 73,  /* 20/40 Intolerant channel report*/
	IOBSSSCAN           = 74,  /* OBSS Scan parameters           */
	IEXTCAP             = 127, /* Extended capability            */
	IWMM                = 221, /* WMM parameters                 */
	IWPAELEMENT         = 221, /* WPA Information Element        */
	INFOELEM_ID_FORCE_32BIT  = 0xFFFFFFFF
} tenuInfoElemID;


typedef struct {
	char *pcRespBuffer;
	s32 s32MaxRespBuffLen;
	s32 s32BytesRead;
	bool bRespRequired;
} tstrConfigPktInfo;



/*****************************************************************************/
/* Extern Variable Declarations                                              */
/*****************************************************************************/


/*****************************************************************************/
/* Extern Function Declarations                                              */
/*****************************************************************************/
extern s32 SendRawPacket(s8 *ps8Packet, s32 s32PacketLen);
extern void NetworkInfoReceived(u8 *pu8Buffer, u32 u32Length);
extern void GnrlAsyncInfoReceived(u8 *pu8Buffer, u32 u32Length);
extern void host_int_ScanCompleteReceived(u8 *pu8Buffer, u32 u32Length);
/*****************************************************************************/
/* Global Variables                                                          */
/*****************************************************************************/
static struct semaphore SemHandleSendPkt;
static struct semaphore SemHandlePktResp;

static s8 *gps8ConfigPacket;

static tstrConfigPktInfo gstrConfigPktInfo;

static u8 g_seqno;

static s16 g_wid_num          = -1;

static u16 Res_Len;

static u8 g_oper_mode    = SET_CFG;

/* WID Switches */
static tstrWID gastrWIDs[] = {
	{WID_FIRMWARE_VERSION,          WID_STR},
	{WID_PHY_VERSION,               WID_STR},
	{WID_HARDWARE_VERSION,          WID_STR},
	{WID_BSS_TYPE,                  WID_CHAR},
	{WID_QOS_ENABLE,                WID_CHAR},
	{WID_11I_MODE,                  WID_CHAR},
	{WID_CURRENT_TX_RATE,           WID_CHAR},
	{WID_LINKSPEED,                 WID_CHAR},
	{WID_RTS_THRESHOLD,             WID_SHORT},
	{WID_FRAG_THRESHOLD,            WID_SHORT},
	{WID_SSID,                      WID_STR},
	{WID_BSSID,                     WID_ADR},
	{WID_BEACON_INTERVAL,           WID_SHORT},
	{WID_POWER_MANAGEMENT,          WID_CHAR},
	{WID_LISTEN_INTERVAL,           WID_CHAR},
	{WID_DTIM_PERIOD,               WID_CHAR},
	{WID_CURRENT_CHANNEL,           WID_CHAR},
	{WID_TX_POWER_LEVEL_11A,        WID_CHAR},
	{WID_TX_POWER_LEVEL_11B,        WID_CHAR},
	{WID_PREAMBLE,                  WID_CHAR},
	{WID_11G_OPERATING_MODE,        WID_CHAR},
	{WID_MAC_ADDR,                  WID_ADR},
	{WID_IP_ADDRESS,                WID_ADR},
	{WID_ACK_POLICY,                WID_CHAR},
	{WID_PHY_ACTIVE_REG,            WID_CHAR},
	{WID_AUTH_TYPE,                 WID_CHAR},
	{WID_REKEY_POLICY,              WID_CHAR},
	{WID_REKEY_PERIOD,              WID_INT},
	{WID_REKEY_PACKET_COUNT,        WID_INT},
	{WID_11I_PSK,                   WID_STR},
	{WID_1X_KEY,                    WID_STR},
	{WID_1X_SERV_ADDR,              WID_IP},
	{WID_SUPP_USERNAME,             WID_STR},
	{WID_SUPP_PASSWORD,             WID_STR},
	{WID_USER_CONTROL_ON_TX_POWER,  WID_CHAR},
	{WID_MEMORY_ADDRESS,            WID_INT},
	{WID_MEMORY_ACCESS_32BIT,       WID_INT},
	{WID_MEMORY_ACCESS_16BIT,       WID_SHORT},
	{WID_MEMORY_ACCESS_8BIT,        WID_CHAR},
	{WID_SITE_SURVEY_RESULTS,       WID_STR},
	{WID_PMKID_INFO,                WID_STR},
	{WID_ASSOC_RES_INFO,            WID_STR},
	{WID_MANUFACTURER,              WID_STR}, /* 4 Wids added for the CAPI tool*/
	{WID_MODEL_NAME,                WID_STR},
	{WID_MODEL_NUM,                 WID_STR},
	{WID_DEVICE_NAME,               WID_STR},
	{WID_SSID_PROBE_REQ,            WID_STR},

#ifdef MAC_802_11N
	{WID_11N_ENABLE,                WID_CHAR},
	{WID_11N_CURRENT_TX_MCS,        WID_CHAR},
	{WID_TX_POWER_LEVEL_11N,        WID_CHAR},
	{WID_11N_OPERATING_MODE,        WID_CHAR},
	{WID_11N_SMPS_MODE,             WID_CHAR},
	{WID_11N_PROT_MECH,             WID_CHAR},
	{WID_11N_ERP_PROT_TYPE,         WID_CHAR},
	{WID_11N_HT_PROT_TYPE,          WID_CHAR},
	{WID_11N_PHY_ACTIVE_REG_VAL,    WID_INT},
	{WID_11N_PRINT_STATS,           WID_CHAR},
	{WID_11N_AUTORATE_TABLE,        WID_BIN_DATA},
	{WID_HOST_CONFIG_IF_TYPE,       WID_CHAR},
	{WID_HOST_DATA_IF_TYPE,         WID_CHAR},
	{WID_11N_SIG_QUAL_VAL,          WID_SHORT},
	{WID_11N_IMMEDIATE_BA_ENABLED,  WID_CHAR},
	{WID_11N_TXOP_PROT_DISABLE,     WID_CHAR},
	{WID_11N_SHORT_GI_20MHZ_ENABLE, WID_CHAR},
	{WID_SHORT_SLOT_ALLOWED,        WID_CHAR},
	{WID_11W_ENABLE,                WID_CHAR},
	{WID_11W_MGMT_PROT_REQ,         WID_CHAR},
	{WID_2040_ENABLE,               WID_CHAR},
	{WID_2040_COEXISTENCE,          WID_CHAR},
	{WID_USER_SEC_CHANNEL_OFFSET,   WID_CHAR},
	{WID_2040_CURR_CHANNEL_OFFSET,  WID_CHAR},
	{WID_2040_40MHZ_INTOLERANT,     WID_CHAR},
	{WID_HUT_RESTART,               WID_CHAR},
	{WID_HUT_NUM_TX_PKTS,           WID_INT},
	{WID_HUT_FRAME_LEN,             WID_SHORT},
	{WID_HUT_TX_FORMAT,             WID_CHAR},
	{WID_HUT_BANDWIDTH,             WID_CHAR},
	{WID_HUT_OP_BAND,               WID_CHAR},
	{WID_HUT_STBC,                  WID_CHAR},
	{WID_HUT_ESS,                   WID_CHAR},
	{WID_HUT_ANTSET,                WID_CHAR},
	{WID_HUT_HT_OP_MODE,            WID_CHAR},
	{WID_HUT_RIFS_MODE,             WID_CHAR},
	{WID_HUT_SMOOTHING_REC,         WID_CHAR},
	{WID_HUT_SOUNDING_PKT,          WID_CHAR},
	{WID_HUT_HT_CODING,             WID_CHAR},
	{WID_HUT_TEST_DIR,              WID_CHAR},
	{WID_HUT_TXOP_LIMIT,            WID_SHORT},
	{WID_HUT_DEST_ADDR,             WID_ADR},
	{WID_HUT_TX_PATTERN,            WID_BIN_DATA},
	{WID_HUT_TX_TIME_TAKEN,         WID_INT},
	{WID_HUT_PHY_TEST_MODE,         WID_CHAR},
	{WID_HUT_PHY_TEST_RATE_HI,      WID_CHAR},
	{WID_HUT_PHY_TEST_RATE_LO,      WID_CHAR},
	{WID_HUT_TX_TEST_TIME,          WID_INT},
	{WID_HUT_LOG_INTERVAL,          WID_INT},
	{WID_HUT_DISABLE_RXQ_REPLENISH, WID_CHAR},
	{WID_HUT_TEST_ID,               WID_STR},
	{WID_HUT_KEY_ORIGIN,            WID_CHAR},
	{WID_HUT_BCST_PERCENT,          WID_CHAR},
	{WID_HUT_GROUP_CIPHER_TYPE,     WID_CHAR},
	{WID_HUT_STATS,                 WID_BIN_DATA},
	{WID_HUT_TSF_TEST_MODE,         WID_CHAR},
	{WID_HUT_SIG_QUAL_AVG,          WID_SHORT},
	{WID_HUT_SIG_QUAL_AVG_CNT,      WID_SHORT},
	{WID_HUT_TSSI_VALUE,            WID_CHAR},
	{WID_HUT_MGMT_PERCENT,          WID_CHAR},
	{WID_HUT_MGMT_BCST_PERCENT,     WID_CHAR},
	{WID_HUT_MGMT_ALLOW_HT,         WID_CHAR},
	{WID_HUT_UC_MGMT_TYPE,          WID_CHAR},
	{WID_HUT_BC_MGMT_TYPE,          WID_CHAR},
	{WID_HUT_UC_MGMT_FRAME_LEN,     WID_SHORT},
	{WID_HUT_BC_MGMT_FRAME_LEN,     WID_SHORT},
	{WID_HUT_11W_MFP_REQUIRED_TX,   WID_CHAR},
	{WID_HUT_11W_MFP_PEER_CAPABLE,  WID_CHAR},
	{WID_HUT_11W_TX_IGTK_ID,        WID_CHAR},
	{WID_HUT_FC_TXOP_MOD,           WID_CHAR},
	{WID_HUT_FC_PROT_TYPE,          WID_CHAR},
	{WID_HUT_SEC_CCA_ASSERT,        WID_CHAR},
#endif /* MAC_802_11N */
};

u16 g_num_total_switches = (sizeof(gastrWIDs) / sizeof(tstrWID));
/*****************************************************************************/
/* Static Function Declarations                                              */
/*****************************************************************************/



/*****************************************************************************/
/* Functions                                                                 */
/*****************************************************************************/
INLINE u8 ascii_hex_to_dec(u8 num)
{
	if ((num >= '0') && (num <= '9'))
		return (num - '0');
	else if ((num >= 'A') && (num <= 'F'))
		return (10 + (num - 'A'));
	else if ((num >= 'a') && (num <= 'f'))
		return (10 + (num - 'a'));

	return INVALID;
}

INLINE u8 get_hex_char(u8 inp)
{
	u8 *d2htab = "0123456789ABCDEF";

	return d2htab[inp & 0xF];
}

/* This function extracts the MAC address held in a string in standard format */
/* into another buffer as integers.                                           */
INLINE u16 extract_mac_addr(char *str, u8 *buff)
{
	*buff = 0;
	while (*str != '\0') {
		if ((*str == ':') || (*str == '-'))
			*(++buff) = 0;
		else
			*buff = (*buff << 4) + ascii_hex_to_dec(*str);

		str++;
	}

	return MAC_ADDR_LEN;
}

/* This function creates MAC address in standard format from a buffer of      */
/* integers.                                                                  */
INLINE void create_mac_addr(u8 *str, u8 *buff)
{
	u32 i = 0;
	u32 j = 0;

	for (i = 0; i < MAC_ADDR_LEN; i++) {
		str[j++] = get_hex_char((u8)((buff[i] >> 4) & 0x0F));
		str[j++] = get_hex_char((u8)(buff[i] & 0x0F));
		str[j++] = ':';
	}
	str[--j] = '\0';
}

/* This function converts the IP address string in dotted decimal format to */
/* unsigned integer. This functionality is similar to the library function  */
/* inet_addr() but is reimplemented here since I could not confirm that     */
/* inet_addr is platform independent.                                       */
/* ips=>IP Address String in dotted decimal format                          */
/* ipn=>Pointer to IP Address in integer format                             */
INLINE u8 conv_ip_to_int(u8 *ips, u32 *ipn)
{
	u8 i   = 0;
	u8 ipb = 0;
	*ipn = 0;
	/* Integer to string for each component */
	while (ips[i] != '\0') {
		if (ips[i] == '.') {
			*ipn = ((*ipn) << 8) | ipb;
			ipb = 0;
		} else {
			ipb = ipb * 10 + ascii_hex_to_dec(ips[i]);
		}

		i++;
	}

	/* The last byte of the IP address is read in here */
	*ipn = ((*ipn) << 8) | ipb;

	return 0;
}

/* This function converts the IP address from integer format to dotted    */
/* decimal string format. Alternative to std library fn inet_ntoa().      */
/* ips=>Buffer to hold IP Address String dotted decimal format (Min 17B)  */
/* ipn=>IP Address in integer format                                      */
INLINE u8 conv_int_to_ip(u8 *ips, u32 ipn)
{
	u8 i   = 0;
	u8 ipb = 0;
	u8 cnt = 0;
	u8 ipbsize = 0;

	for (cnt = 4; cnt > 0; cnt--) {
		ipb = (ipn >> (8 * (cnt - 1))) & 0xFF;

		if (ipb >= 100)
			ipbsize = 2;
		else if (ipb >= 10)
			ipbsize = 1;
		else
			ipbsize = 0;

		switch (ipbsize) {
		case 2:
			ips[i++] = get_hex_char(ipb / 100);
			ipb %= 100;

		case 1:
			ips[i++] = get_hex_char(ipb / 10);
			ipb %= 10;

		default:
			ips[i++] = get_hex_char(ipb);
		}

		if (cnt > 1)
			ips[i++] = '.';
	}

	ips[i] = '\0';

	return i;
}

INLINE tenuWIDtype get_wid_type(u32 wid_num)
{
	/* Check for iconfig specific WID types first */
	if ((wid_num == WID_BSSID) ||
	    (wid_num == WID_MAC_ADDR) ||
	    (wid_num == WID_IP_ADDRESS) ||
	    (wid_num == WID_HUT_DEST_ADDR)) {
		return WID_ADR;
	}

	if ((WID_1X_SERV_ADDR == wid_num) ||
	    (WID_STACK_IP_ADDR == wid_num) ||
	    (WID_STACK_NETMASK_ADDR == wid_num)) {
		return WID_IP;
	}

	/* Next check for standard WID types */
	if (wid_num < 0x1000)
		return WID_CHAR;
	else if (wid_num < 0x2000)
		return WID_SHORT;
	else if (wid_num < 0x3000)
		return WID_INT;
	else if (wid_num < 0x4000)
		return WID_STR;
	else if (wid_num < 0x5000)
		return WID_BIN_DATA;

	return WID_UNDEF;
}


/* This function extracts the beacon period field from the beacon or probe   */
/* response frame.                                                           */
INLINE u16 get_beacon_period(u8 *data)
{
	u16 bcn_per = 0;

	bcn_per  = data[0];
	bcn_per |= (data[1] << 8);

	return bcn_per;
}

INLINE u32 get_beacon_timestamp_lo(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = MAC_HDR_LEN;

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

INLINE u32 get_beacon_timestamp_hi(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = (MAC_HDR_LEN + 4);

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

/* This function extracts the 'frame type' bits from the MAC header of the   */
/* input frame.                                                              */
/* Returns the value in the LSB of the returned value.                       */
INLINE tenuBasicFrmType get_type(u8 *header)
{
	return ((tenuBasicFrmType)(header[0] & 0x0C));
}

/* This function extracts the 'frame type and sub type' bits from the MAC    */
/* header of the input frame.                                                */
/* Returns the value in the LSB of the returned value.                       */
INLINE tenuFrmSubtype get_sub_type(u8 *header)
{
	return ((tenuFrmSubtype)(header[0] & 0xFC));
}

/* This function extracts the 'to ds' bit from the MAC header of the input   */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
INLINE u8 get_to_ds(u8 *header)
{
	return (header[1] & 0x01);
}

/* This function extracts the 'from ds' bit from the MAC header of the input */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
INLINE u8 get_from_ds(u8 *header)
{
	return ((header[1] & 0x02) >> 1);
}

/* This function extracts the MAC Address in 'address1' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
INLINE void get_address1(u8 *pu8msa, u8 *addr)
{
	WILC_memcpy(addr, pu8msa + 4, 6);
}

/* This function extracts the MAC Address in 'address2' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
INLINE void get_address2(u8 *pu8msa, u8 *addr)
{
	WILC_memcpy(addr, pu8msa + 10, 6);
}

/* This function extracts the MAC Address in 'address3' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
INLINE void get_address3(u8 *pu8msa, u8 *addr)
{
	WILC_memcpy(addr, pu8msa + 16, 6);
}

/* This function extracts the BSSID from the incoming WLAN packet based on   */
/* the 'from ds' bit, and updates the MAC Address in the allocated 'addr'    */
/* variable.                                                                 */
INLINE void get_BSSID(u8 *data, u8 *bssid)
{
	if (get_from_ds(data) == 1)
		get_address2(data, bssid);
	else if (get_to_ds(data) == 1)
		get_address1(data, bssid);
	else
		get_address3(data, bssid);
}

/* This function extracts the SSID from a beacon/probe response frame        */
INLINE void get_ssid(u8 *data, u8 *ssid, u8 *p_ssid_len)
{
	u8 len = 0;
	u8 i   = 0;
	u8 j   = 0;

	len = data[MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN +
		   CAP_INFO_LEN + 1];
	j   = MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN +
		CAP_INFO_LEN + 2;

	/* If the SSID length field is set wrongly to a value greater than the   */
	/* allowed maximum SSID length limit, reset the length to 0              */
	if (len >= MAX_SSID_LEN)
		len = 0;

	for (i = 0; i < len; i++, j++)
		ssid[i] = data[j];

	ssid[len] = '\0';

	*p_ssid_len = len;
}

/* This function extracts the capability info field from the beacon or probe */
/* response frame.                                                           */
INLINE u16 get_cap_info(u8 *data)
{
	u16 cap_info = 0;
	u16 index    = MAC_HDR_LEN;
	tenuFrmSubtype st = BEACON;

	st = get_sub_type(data);

	/* Location of the Capability field is different for Beacon and */
	/* Association frames.                                          */
	if ((st == BEACON) || (st == PROBE_RSP))
		index += TIME_STAMP_LEN + BEACON_INTERVAL_LEN;

	cap_info  = data[index];
	cap_info |= (data[index + 1] << 8);

	return cap_info;
}

/* This function extracts the capability info field from the Association */
/* response frame.                                                                       */
INLINE u16 get_assoc_resp_cap_info(u8 *data)
{
	u16 cap_info = 0;

	cap_info  = data[0];
	cap_info |= (data[1] << 8);

	return cap_info;
}

/* This funcion extracts the association status code from the incoming       */
/* association response frame and returns association status code            */
INLINE u16 get_asoc_status(u8 *data)
{
	u16 asoc_status = 0;

	asoc_status = data[3];
	asoc_status = (asoc_status << 8) | data[2];

	return asoc_status;
}

/* This function extracts association ID from the incoming association       */
/* response frame							                                     */
INLINE u16 get_asoc_id(u8 *data)
{
	u16 asoc_id = 0;

	asoc_id  = data[4];
	asoc_id |= (data[5] << 8);

	return asoc_id;
}

/**
 *  @brief              initializes the Core Configurator
 *  @details
 *  @return     Error code indicating success/failure
 *  @note
 *  @author	mabubakr
 *  @date		1 Mar 2012
 *  @version		1.0
 */

s32 CoreConfiguratorInit(void)
{
	s32 s32Error = WILC_SUCCESS;
	PRINT_D(CORECONFIG_DBG, "CoreConfiguratorInit()\n");

	sema_init(&SemHandleSendPkt, 1);
	sema_init(&SemHandlePktResp, 0);

	gps8ConfigPacket = (s8 *)WILC_MALLOC(MAX_PACKET_BUFF_SIZE);
	if (gps8ConfigPacket == NULL) {
		PRINT_ER("failed in gps8ConfigPacket allocation\n");
		s32Error = WILC_NO_MEM;
		goto _fail_;
	}

	WILC_memset((void *)gps8ConfigPacket, 0, MAX_PACKET_BUFF_SIZE);

	WILC_memset((void *)(&gstrConfigPktInfo), 0, sizeof(tstrConfigPktInfo));
_fail_:
	return s32Error;
}

u8 *get_tim_elm(u8 *pu8msa, u16 u16RxLen, u16 u16TagParamOffset)
{
	u16 u16index = 0;

	/*************************************************************************/
	/*                       Beacon Frame - Frame Body                       */
	/* --------------------------------------------------------------------- */
	/* |Timestamp |BeaconInt |CapInfo |SSID |SupRates |DSParSet |TIM elm   | */
	/* --------------------------------------------------------------------- */
	/* |8         |2         |2       |2-34 |3-10     |3        |4-256     | */
	/* --------------------------------------------------------------------- */
	/*                                                                       */
	/*************************************************************************/

	u16index = u16TagParamOffset;

	/* Search for the TIM Element Field and return if the element is found */
	while (u16index < (u16RxLen - FCS_LEN)) {
		if (pu8msa[u16index] == ITIM) {
			return &pu8msa[u16index];
		} else {
			u16index += (IE_HDR_LEN + pu8msa[u16index + 1]);
		}
	}

	return 0;
}

/* This function gets the current channel information from
 * the 802.11n beacon/probe response frame */
u8 get_current_channel_802_11n(u8 *pu8msa, u16 u16RxLen)
{
	u16 index;

	index = TAG_PARAM_OFFSET;
	while (index < (u16RxLen - FCS_LEN)) {
		if (pu8msa[index] == IDSPARMS)
			return pu8msa[index + 2];
		else
			/* Increment index by length information and header */
			index += pu8msa[index + 1] + IE_HDR_LEN;
	}

	/* Return current channel information from the MIB, if beacon/probe  */
	/* response frame does not contain the DS parameter set IE           */
	/* return (mget_CurrentChannel() + 1); */
	return 0;  /* no MIB here */
}

u8 get_current_channel(u8 *pu8msa, u16 u16RxLen)
{
#ifdef PHY_802_11n
#ifdef FIVE_GHZ_BAND
	/* Get the current channel as its not set in */
	/* 802.11a beacons/probe response            */
	return (get_rf_channel() + 1);
#else /* FIVE_GHZ_BAND */
	/* Extract current channel information from */
	/* the beacon/probe response frame          */
	return get_current_channel_802_11n(pu8msa, u16RxLen);
#endif /* FIVE_GHZ_BAND */
#else
	return 0;
#endif /* PHY_802_11n */
}

/**
 *  @brief                      parses the received 'N' message
 *  @details
 *  @param[in]  pu8MsgBuffer The message to be parsed
 *  @param[out]         ppstrNetworkInfo pointer to pointer to the structure containing the parsed Network Info
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mabubakr
 *  @date			1 Mar 2012
 *  @version		1.0
 */
s32 ParseNetworkInfo(u8 *pu8MsgBuffer, tstrNetworkInfo **ppstrNetworkInfo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrNetworkInfo *pstrNetworkInfo = NULL;
	u8 u8MsgType = 0;
	u8 u8MsgID = 0;
	u16 u16MsgLen = 0;

	u16 u16WidID = (u16)WID_NIL;
	u16 u16WidLen  = 0;
	u8  *pu8WidVal = 0;

	u8MsgType = pu8MsgBuffer[0];

	/* Check whether the received message type is 'N' */
	if ('N' != u8MsgType) {
		PRINT_ER("Received Message format incorrect.\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	/* Extract message ID */
	u8MsgID = pu8MsgBuffer[1];

	/* Extract message Length */
	u16MsgLen = MAKE_WORD16(pu8MsgBuffer[2], pu8MsgBuffer[3]);

	/* Extract WID ID */
	u16WidID = MAKE_WORD16(pu8MsgBuffer[4], pu8MsgBuffer[5]);

	/* Extract WID Length */
	u16WidLen = MAKE_WORD16(pu8MsgBuffer[6], pu8MsgBuffer[7]);

	/* Assign a pointer to the WID value */
	pu8WidVal  = &pu8MsgBuffer[8];

	/* parse the WID value of the WID "WID_NEWORK_INFO" */
	{
		u8  *pu8msa = 0;
		u16 u16RxLen = 0;
		u8 *pu8TimElm = 0;
		u8 *pu8IEs = 0;
		u16 u16IEsLen = 0;
		u8 u8index = 0;
		u32 u32Tsf_Lo;
		u32 u32Tsf_Hi;

		pstrNetworkInfo = (tstrNetworkInfo *)WILC_MALLOC(sizeof(tstrNetworkInfo));
		WILC_memset((void *)(pstrNetworkInfo), 0, sizeof(tstrNetworkInfo));

		pstrNetworkInfo->s8rssi = pu8WidVal[0];

		/* Assign a pointer to msa "Mac Header Start Address" */
		pu8msa = &pu8WidVal[1];

		u16RxLen = u16WidLen - 1;

		/* parse msa*/

		/* Get the cap_info */
		pstrNetworkInfo->u16CapInfo = get_cap_info(pu8msa);
		#ifdef WILC_P2P
		/* Get time-stamp [Low only 32 bit] */
		pstrNetworkInfo->u32Tsf = get_beacon_timestamp_lo(pu8msa);
		PRINT_D(CORECONFIG_DBG, "TSF :%x\n", pstrNetworkInfo->u32Tsf);
		#endif

		/* Get full time-stamp [Low and High 64 bit] */
		u32Tsf_Lo = get_beacon_timestamp_lo(pu8msa);
		u32Tsf_Hi = get_beacon_timestamp_hi(pu8msa);

		pstrNetworkInfo->u64Tsf = u32Tsf_Lo | ((u64)u32Tsf_Hi << 32);

		/* Get SSID */
		get_ssid(pu8msa, pstrNetworkInfo->au8ssid, &(pstrNetworkInfo->u8SsidLen));

		/* Get BSSID */
		get_BSSID(pu8msa, pstrNetworkInfo->au8bssid);

		/* Get the current channel */
		pstrNetworkInfo->u8channel = get_current_channel(pu8msa, (u16RxLen + FCS_LEN));

		/* Get beacon period */
		u8index = (MAC_HDR_LEN + TIME_STAMP_LEN);

		pstrNetworkInfo->u16BeaconPeriod = get_beacon_period(pu8msa + u8index);

		u8index += BEACON_INTERVAL_LEN + CAP_INFO_LEN;

		/* Get DTIM Period */
		pu8TimElm = get_tim_elm(pu8msa, (u16RxLen + FCS_LEN), u8index);
		if (pu8TimElm != 0) {
			pstrNetworkInfo->u8DtimPeriod = pu8TimElm[3];
		}
		pu8IEs = &pu8msa[MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN + CAP_INFO_LEN];
		u16IEsLen = u16RxLen - (MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN + CAP_INFO_LEN);

		if (u16IEsLen > 0) {
			pstrNetworkInfo->pu8IEs = (u8 *)WILC_MALLOC(u16IEsLen);
			WILC_memset((void *)(pstrNetworkInfo->pu8IEs), 0, u16IEsLen);

			WILC_memcpy(pstrNetworkInfo->pu8IEs, pu8IEs, u16IEsLen);
		}
		pstrNetworkInfo->u16IEsLen = u16IEsLen;

	}

	*ppstrNetworkInfo = pstrNetworkInfo;

ERRORHANDLER:
	return s32Error;
}

/**
 *  @brief              Deallocates the parsed Network Info
 *  @details
 *  @param[in]  pstrNetworkInfo Network Info to be deallocated
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mabubakr
 *  @date		1 Mar 2012
 *  @version		1.0
 */
s32 DeallocateNetworkInfo(tstrNetworkInfo *pstrNetworkInfo)
{
	s32 s32Error = WILC_SUCCESS;

	if (pstrNetworkInfo != NULL) {
		if (pstrNetworkInfo->pu8IEs != NULL) {
			WILC_FREE(pstrNetworkInfo->pu8IEs);
			pstrNetworkInfo->pu8IEs = NULL;
		} else {
			s32Error = WILC_FAIL;
		}

		WILC_FREE(pstrNetworkInfo);
		pstrNetworkInfo = NULL;

	} else {
		s32Error = WILC_FAIL;
	}

	return s32Error;
}

/**
 *  @brief                      parses the received Association Response frame
 *  @details
 *  @param[in]  pu8Buffer The Association Response frame to be parsed
 *  @param[out]         ppstrConnectRespInfo pointer to pointer to the structure containing the parsed Association Response Info
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mabubakr
 *  @date			2 Apr 2012
 *  @version		1.0
 */
s32 ParseAssocRespInfo(u8 *pu8Buffer, u32 u32BufferLen,
			       tstrConnectRespInfo **ppstrConnectRespInfo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrConnectRespInfo *pstrConnectRespInfo = NULL;
	u16 u16AssocRespLen = 0;
	u8 *pu8IEs = 0;
	u16 u16IEsLen = 0;

	pstrConnectRespInfo = (tstrConnectRespInfo *)WILC_MALLOC(sizeof(tstrConnectRespInfo));
	WILC_memset((void *)(pstrConnectRespInfo), 0, sizeof(tstrConnectRespInfo));

	/* u16AssocRespLen = pu8Buffer[0]; */
	u16AssocRespLen = (u16)u32BufferLen;

	/* get the status code */
	pstrConnectRespInfo->u16ConnectStatus = get_asoc_status(pu8Buffer);
	if (pstrConnectRespInfo->u16ConnectStatus == SUCCESSFUL_STATUSCODE) {

		/* get the capability */
		pstrConnectRespInfo->u16capability = get_assoc_resp_cap_info(pu8Buffer);

		/* get the Association ID */
		pstrConnectRespInfo->u16AssocID = get_asoc_id(pu8Buffer);

		/* get the Information Elements */
		pu8IEs = &pu8Buffer[CAP_INFO_LEN + STATUS_CODE_LEN + AID_LEN];
		u16IEsLen = u16AssocRespLen - (CAP_INFO_LEN + STATUS_CODE_LEN + AID_LEN);

		pstrConnectRespInfo->pu8RespIEs = (u8 *)WILC_MALLOC(u16IEsLen);
		WILC_memset((void *)(pstrConnectRespInfo->pu8RespIEs), 0, u16IEsLen);

		WILC_memcpy(pstrConnectRespInfo->pu8RespIEs, pu8IEs, u16IEsLen);
		pstrConnectRespInfo->u16RespIEsLen = u16IEsLen;
	}

	*ppstrConnectRespInfo = pstrConnectRespInfo;


	return s32Error;
}

/**
 *  @brief                      Deallocates the parsed Association Response Info
 *  @details
 *  @param[in]  pstrNetworkInfo Network Info to be deallocated
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mabubakr
 *  @date			2 Apr 2012
 *  @version		1.0
 */
s32 DeallocateAssocRespInfo(tstrConnectRespInfo *pstrConnectRespInfo)
{
	s32 s32Error = WILC_SUCCESS;

	if (pstrConnectRespInfo != NULL) {
		if (pstrConnectRespInfo->pu8RespIEs != NULL) {
			WILC_FREE(pstrConnectRespInfo->pu8RespIEs);
			pstrConnectRespInfo->pu8RespIEs = NULL;
		} else {
			s32Error = WILC_FAIL;
		}

		WILC_FREE(pstrConnectRespInfo);
		pstrConnectRespInfo = NULL;

	} else {
		s32Error = WILC_FAIL;
	}

	return s32Error;
}

#ifndef CONNECT_DIRECT
s32 ParseSurveyResults(u8 ppu8RcvdSiteSurveyResults[][MAX_SURVEY_RESULT_FRAG_SIZE],
			       wid_site_survey_reslts_s **ppstrSurveyResults,
			       u32 *pu32SurveyResultsCount)
{
	s32 s32Error = WILC_SUCCESS;
	wid_site_survey_reslts_s *pstrSurveyResults = NULL;
	u32 u32SurveyResultsCount = 0;
	u32 u32SurveyBytesLength = 0;
	u8 *pu8BufferPtr;
	u32 u32RcvdSurveyResultsNum = 2;
	u8 u8ReadSurveyResFragNum;
	u32 i;
	u32 j;

	for (i = 0; i < u32RcvdSurveyResultsNum; i++) {
		u32SurveyBytesLength = ppu8RcvdSiteSurveyResults[i][0];


		for (j = 0; j < u32SurveyBytesLength; j += SURVEY_RESULT_LENGTH) {
			u32SurveyResultsCount++;
		}
	}

	pstrSurveyResults = (wid_site_survey_reslts_s *)WILC_MALLOC(u32SurveyResultsCount * sizeof(wid_site_survey_reslts_s));
	if (pstrSurveyResults == NULL) {
		u32SurveyResultsCount = 0;
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	WILC_memset((void *)(pstrSurveyResults), 0, u32SurveyResultsCount * sizeof(wid_site_survey_reslts_s));

	u32SurveyResultsCount = 0;

	for (i = 0; i < u32RcvdSurveyResultsNum; i++) {
		pu8BufferPtr = ppu8RcvdSiteSurveyResults[i];

		u32SurveyBytesLength = pu8BufferPtr[0];

		/* TODO: mostafa: pu8BufferPtr[1] contains the fragment num */
		u8ReadSurveyResFragNum = pu8BufferPtr[1];

		pu8BufferPtr += 2;

		for (j = 0; j < u32SurveyBytesLength; j += SURVEY_RESULT_LENGTH) {
			WILC_memcpy(&pstrSurveyResults[u32SurveyResultsCount], pu8BufferPtr, SURVEY_RESULT_LENGTH);
			pu8BufferPtr += SURVEY_RESULT_LENGTH;
			u32SurveyResultsCount++;
		}
	}

ERRORHANDLER:
	*ppstrSurveyResults = pstrSurveyResults;
	*pu32SurveyResultsCount = u32SurveyResultsCount;

	return s32Error;
}


s32 DeallocateSurveyResults(wid_site_survey_reslts_s *pstrSurveyResults)
{
	s32 s32Error = WILC_SUCCESS;

	if (pstrSurveyResults != NULL) {
		WILC_FREE(pstrSurveyResults);
	}

	return s32Error;
}
#endif

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessCharWid                                         */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_CHAR and       */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessCharWid(char *pcPacket, s32 *ps32PktLen,
		    tstrWID *pstrWID, s8 *ps8WidVal)
{
	u8 *pu8val = (u8 *)ps8WidVal;
	u8 u8val = 0;
	s32 s32PktLen = *ps32PktLen;
	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set CHAR val 0x%x ,NULL structure\n", u8val);
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid >> 8) & 0xFF;
	if (g_oper_mode == SET_CFG) {
		u8val = *pu8val;

		/* Length */
		pcPacket[s32PktLen++] = sizeof(u8);


		/* Value */
		pcPacket[s32PktLen++] = u8val;
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessShortWid                                        */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_SHORT and      */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessShortWid(char *pcPacket, s32 *ps32PktLen,
		     tstrWID *pstrWID, s8 *ps8WidVal)
{
	u16 *pu16val = (u16 *)ps8WidVal;
	u16 u16val = 0;
	s32 s32PktLen = *ps32PktLen;
	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set SHORT val 0x%x ,NULL structure\n", u16val);
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		u16val = *pu16val;

		/* Length */
		pcPacket[s32PktLen++] = sizeof(u16);

		/* Value */
		pcPacket[s32PktLen++] = (u8)(u16val & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u16val >> 8) & 0xFF);
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessIntWid                                          */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_INT and        */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessIntWid(char *pcPacket, s32 *ps32PktLen,
		   tstrWID *pstrWID, s8 *ps8WidVal)
{
	u32 *pu32val = (u32 *)ps8WidVal;
	u32 u32val = 0;
	s32 s32PktLen = *ps32PktLen;
	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set INT val 0x%x , NULL structure\n", u32val);
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		u32val = *pu32val;

		/* Length */
		pcPacket[s32PktLen++] = sizeof(u32);

		/* Value */
		pcPacket[s32PktLen++] = (u8)(u32val & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 8) & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 16) & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 24) & 0xFF);
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessIPwid                                           */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_IP and         */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessIPwid(char *pcPacket, s32 *ps32PktLen,
		  tstrWID *pstrWID, u8 *pu8ip)
{
	u32 u32val = 0;
	s32 s32PktLen = *ps32PktLen;

	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set IP Addr , NULL structure\n");
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		/* Length */
		pcPacket[s32PktLen++] = sizeof(u32);

		/* Convert the IP Address String to Integer */
		conv_ip_to_int(pu8ip, &u32val);

		/* Value */
		pcPacket[s32PktLen++] = (u8)(u32val & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 8) & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 16) & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u32val >> 24) & 0xFF);
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessStrWid                                          */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_STR and        */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessStrWid(char *pcPacket, s32 *ps32PktLen,
		   tstrWID *pstrWID, u8 *pu8val, s32 s32ValueSize)
{
	u16 u16MsgLen = 0;
	u16 idx    = 0;
	s32 s32PktLen = *ps32PktLen;
	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set STR val, NULL structure\n");
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		/* Message Length */
		/* u16MsgLen = WILC_strlen(pu8val); */
		u16MsgLen = (u16)s32ValueSize;

		/* Length */
		pcPacket[s32PktLen++] = (u8)u16MsgLen;

		/* Value */
		for (idx = 0; idx < u16MsgLen; idx++)
			pcPacket[s32PktLen++] = pu8val[idx];
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessAdrWid                                          */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_ADR and        */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Value to set                                          */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    :                                                          */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessAdrWid(char *pcPacket, s32 *ps32PktLen,
		   tstrWID *pstrWID, u8 *pu8val)
{
	u16 u16MsgLen = 0;
	s32 s32PktLen = *ps32PktLen;

	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set Addr WID, NULL structure\n");
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		/* Message Length */
		u16MsgLen = MAC_ADDR_LEN;

		/* Length */
		pcPacket[s32PktLen++] = (u8)u16MsgLen;

		/* Value */
		extract_mac_addr(pu8val, pcPacket + s32PktLen);
		s32PktLen += u16MsgLen;
	}
	*ps32PktLen = s32PktLen;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ProcessBinWid                                          */
/*                                                                           */
/*  Description   : This function processes a WID of type WID_BIN_DATA and        */
/*                  updates the cfg packet with the supplied value.          */
/*                                                                           */
/*  Inputs        : 1) Pointer to WID cfg structure                          */
/*                  2) Name of file containing the binary data in text mode  */
/*                                                                           */
/*  Globals       :                                                          */
/*                                                                           */
/*  Processing    : The binary data is expected to be supplied through a     */
/*                  file in text mode. This file is expected to be in the    */
/*                  finject format. It is parsed, converted to binary format */
/*                  and copied into g_cfg_pkt for further processing. This   */
/*                  is obviously a round-about way of processing involving   */
/*                  multiple (re)conversions between bin & ascii formats.    */
/*                  But it is done nevertheless to retain uniformity and for */
/*                  ease of debugging.                                       */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : None                                                     */
/*                                                                           */

/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

void ProcessBinWid(char *pcPacket, s32 *ps32PktLen,
		   tstrWID *pstrWID, u8 *pu8val, s32 s32ValueSize)
{
	/* WILC_ERROR("processing Binary WIDs is not supported\n"); */

	u16 u16MsgLen = 0;
	u16 idx    = 0;
	s32 s32PktLen = *ps32PktLen;
	u8 u8checksum = 0;

	if (pstrWID == NULL) {
		PRINT_WRN(CORECONFIG_DBG, "Can't set BIN val, NULL structure\n");
		return;
	}

	/* WID */
	pcPacket[s32PktLen++] = (u8)(pstrWID->u16WIDid & 0xFF);
	pcPacket[s32PktLen++] = (u8)((pstrWID->u16WIDid >> 8) & 0xFF);

	if (g_oper_mode == SET_CFG) {
		/* Message Length */
		u16MsgLen = (u16)s32ValueSize;

		/* Length */
		/* pcPacket[s32PktLen++] = (u8)u16MsgLen; */
		pcPacket[s32PktLen++] = (u8)(u16MsgLen  & 0xFF);
		pcPacket[s32PktLen++] = (u8)((u16MsgLen >> 8) & 0xFF);

		/* Value */
		for (idx = 0; idx < u16MsgLen; idx++)
			pcPacket[s32PktLen++] = pu8val[idx];

		/* checksum */
		for (idx = 0; idx < u16MsgLen; idx++)
			u8checksum += pcPacket[MSG_HEADER_LEN + idx + 4];

		pcPacket[s32PktLen++] = u8checksum;
	}
	*ps32PktLen = s32PktLen;
}


/*****************************************************************************/
/*                                                                           */
/*  Function Name : further_process_response                                 */
/*                                                                           */
/*  Description   : This function parses the response frame got from the     */
/*                  device.                                                  */
/*                                                                           */
/*  Inputs        : 1) The received response frame                           */
/*                  2) WID                                                   */
/*                  3) WID Length                                            */
/*                  4) Output file handle                                    */
/*                  5) Process Wid Number(i.e wid from --widn switch)        */
/*                  6) Index the array in the Global Wid Structure.          */
/*                                                                           */
/*  Globals       : g_wid_num, gastrWIDs                                     */
/*                                                                           */
/*  Processing    : This function parses the response of the device depending*/
/*                  WID type and writes it to the output file in Hex or      */
/*                  decimal notation depending on the --getx or --get switch.*/
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : 0 on Success & -2 on Failure                             */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2009   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

s32 further_process_response(u8 *resp,
				     u16 u16WIDid,
				     u16 cfg_len,
				     bool process_wid_num,
				     u32 cnt,
				     tstrWID *pstrWIDresult)
{
	u32 retval = 0;
	u32 idx = 0;
	u8 cfg_chr  = 0;
	u16 cfg_sht  = 0;
	u32 cfg_int  = 0;
	u8 cfg_str[256] = {0};
	tenuWIDtype enuWIDtype = WID_UNDEF;

	if (process_wid_num) {
		enuWIDtype = get_wid_type(g_wid_num);
	} else {
		enuWIDtype = gastrWIDs[cnt].enuWIDtype;
	}


	switch (enuWIDtype) {
	case WID_CHAR:
		cfg_chr = resp[idx];
		/*Set local copy of WID*/
		*(pstrWIDresult->ps8WidVal) = cfg_chr;
		break;

	case WID_SHORT:
	{
		u16 *pu16val = (u16 *)(pstrWIDresult->ps8WidVal);
		cfg_sht = MAKE_WORD16(resp[idx], resp[idx + 1]);
		/*Set local copy of WID*/
		/* pstrWIDresult->ps8WidVal = (s8*)(s32)cfg_sht; */
		*pu16val = cfg_sht;
		break;
	}

	case WID_INT:
	{
		u32 *pu32val = (u32 *)(pstrWIDresult->ps8WidVal);
		cfg_int = MAKE_WORD32(
				MAKE_WORD16(resp[idx], resp[idx + 1]),
				MAKE_WORD16(resp[idx + 2], resp[idx + 3])
				);
		/*Set local copy of WID*/
		/* pstrWIDresult->ps8WidVal = (s8*)cfg_int; */
		*pu32val = cfg_int;
		break;
	}

	case WID_STR:
		WILC_memcpy(cfg_str, resp + idx, cfg_len);
		/* cfg_str[cfg_len] = '\0'; //mostafa: no need currently for NULL termination */
		if (process_wid_num) {
			/*fprintf(out_file,"0x%4.4x = %s\n",g_wid_num,
			 *                              cfg_str);*/
		} else {
			/*fprintf(out_file,"%s = %s\n",gastrWIDs[cnt].cfg_switch,
			 *                           cfg_str);*/
		}

		if (pstrWIDresult->s32ValueSize >= cfg_len) {
			WILC_memcpy(pstrWIDresult->ps8WidVal, cfg_str, cfg_len); /* mostafa: no need currently for the extra NULL byte */
			pstrWIDresult->s32ValueSize = cfg_len;
		} else {
			PRINT_ER("allocated WID buffer length is smaller than the received WID Length\n");
			retval = -2;
		}

		break;

	case WID_ADR:
		create_mac_addr(cfg_str, resp + idx);

		WILC_strncpy(pstrWIDresult->ps8WidVal, cfg_str, WILC_strlen(cfg_str));
		pstrWIDresult->ps8WidVal[WILC_strlen(cfg_str)] = '\0';
		if (process_wid_num) {
			/*fprintf(out_file,"0x%4.4x = %s\n",g_wid_num,
			 *                              cfg_str);*/
		} else {
			/*fprintf(out_file,"%s = %s\n",gastrWIDs[cnt].cfg_switch,
			 *                           cfg_str);*/
		}
		break;

	case WID_IP:
		cfg_int = MAKE_WORD32(
				MAKE_WORD16(resp[idx], resp[idx + 1]),
				MAKE_WORD16(resp[idx + 2], resp[idx + 3])
				);
		conv_int_to_ip(cfg_str, cfg_int);
		if (process_wid_num) {
			/*fprintf(out_file,"0x%4.4x = %s\n",g_wid_num,
			 *                              cfg_str);*/
		} else {
			/*fprintf(out_file,"%s = %s\n",gastrWIDs[cnt].cfg_switch,
			 *                           cfg_str);*/
		}
		break;

	case WID_BIN_DATA:
		if (pstrWIDresult->s32ValueSize >= cfg_len) {
			WILC_memcpy(pstrWIDresult->ps8WidVal, resp + idx, cfg_len);
			pstrWIDresult->s32ValueSize = cfg_len;
		} else {
			PRINT_ER("Allocated WID buffer length is smaller than the received WID Length Err(%d)\n", retval);
			retval = -2;
		}
		break;

	default:
		PRINT_ER("ERROR: Check config database: Error(%d)\n", retval);
		retval = -2;
		break;
	}

	return retval;
}

/*****************************************************************************/
/*                                                                           */
/*  Function Name : ParseResponse                                           */
/*                                                                           */
/*  Description   : This function parses the command-line options and        */
/*                  creates the config packets which can be sent to the WLAN */
/*                  station.                                                 */
/*                                                                           */
/*  Inputs        : 1) The received response frame                           */
/*                                                                           */
/*  Globals       : g_opt_list, gastrWIDs	                                 */
/*                                                                           */
/*  Processing    : This function parses the options and creates different   */
/*                  types of packets depending upon the WID-type             */
/*                  corresponding to the option.                             */
/*                                                                           */
/*  Outputs       : None                                                     */
/*                                                                           */
/*  Returns       : 0 on Success & -1 on Failure                             */
/*                                                                           */
/*  Issues        : None                                                     */
/*                                                                           */
/*  Revision History:                                                        */
/*                                                                           */
/*         DD MM YYYY   Author(s)       Changes (Describe the changes made)  */
/*         08 01 2008   Ittiam          Draft                                */
/*                                                                           */
/*****************************************************************************/

s32 ParseResponse(u8 *resp, tstrWID *pstrWIDcfgResult)
{
	u16 u16RespLen = 0;
	u16 u16WIDid  = 0;
	u16 cfg_len  = 0;
	tenuWIDtype enuWIDtype = WID_UNDEF;
	bool num_wid_processed = false;
	u32 cnt = 0;
	u32 idx = 0;
	u32 ResCnt = 0;
	/* Check whether the received frame is a valid response */
	if (RESP_MSG_TYPE != resp[0]) {
		PRINT_INFO(CORECONFIG_DBG, "Received Message format incorrect.\n");
		return -1;
	}

	/* Extract Response Length */
	u16RespLen = MAKE_WORD16(resp[2], resp[3]);
	Res_Len = u16RespLen;

	for (idx = MSG_HEADER_LEN; idx < u16RespLen; ) {
		u16WIDid = MAKE_WORD16(resp[idx], resp[idx + 1]);
		cfg_len = resp[idx + 2];
		/* Incase of Bin Type Wid, the length is given by two byte field      */
		enuWIDtype = get_wid_type(u16WIDid);
		if (WID_BIN_DATA == enuWIDtype) {
			cfg_len |= ((u16)resp[idx + 3] << 8) & 0xFF00;
			idx++;
		}
		idx += 3;
		if ((u16WIDid == g_wid_num) && (!num_wid_processed)) {
			num_wid_processed = true;

			if (-2 == further_process_response(&resp[idx], u16WIDid, cfg_len, true, 0, &pstrWIDcfgResult[ResCnt])) {
				return -2;
			}
			ResCnt++;
		} else {
			for (cnt = 0; cnt < g_num_total_switches; cnt++) {
				if (gastrWIDs[cnt].u16WIDid == u16WIDid) {
					if (-2 == further_process_response(&resp[idx], u16WIDid, cfg_len, false, cnt,
									   &pstrWIDcfgResult[ResCnt])) {
						return -2;
					}
					ResCnt++;
				}
			}
		}
		idx += cfg_len;
		/* In case if BIN type Wid, The last byte of the Cfg packet is the    */
		/* Checksum. The WID Length field does not accounts for the checksum. */
		/* The Checksum is discarded.                                         */
		if (WID_BIN_DATA == enuWIDtype) {
			idx++;
		}
	}

	return 0;
}

/**
 *  @brief              parses the write response [just detects its status: success or failure]
 *  @details
 *  @param[in]  pu8RespBuffer The Response to be parsed
 *  @return     Error code indicating Write Operation status:
 *                            WRITE_RESP_SUCCESS (1) => Write Success.
 *                            WILC_FAIL (-100)               => Write Failure.
 *  @note
 *  @author		Ittiam
 *  @date		11 Aug 2009
 *  @version	1.0
 */

s32 ParseWriteResponse(u8 *pu8RespBuffer)
{
	s32 s32Error = WILC_FAIL;
	u16 u16RespLen   = 0;
	u16 u16WIDtype = (u16)WID_NIL;

	/* Check whether the received frame is a valid response */
	if (RESP_MSG_TYPE != pu8RespBuffer[0]) {
		PRINT_ER("Received Message format incorrect.\n");
		return WILC_FAIL;
	}

	/* Extract Response Length */
	u16RespLen = MAKE_WORD16(pu8RespBuffer[2], pu8RespBuffer[3]);

	u16WIDtype = MAKE_WORD16(pu8RespBuffer[4], pu8RespBuffer[5]);

	/* Check for WID_STATUS ID and then check the length and status value */
	if ((u16WIDtype == WID_STATUS) &&
	    (pu8RespBuffer[6] == 1) &&
	    (pu8RespBuffer[7] == WRITE_RESP_SUCCESS)) {
		s32Error = WRITE_RESP_SUCCESS;
		return s32Error;
	}

	/* If the length or status are not as expected return failure    */
	s32Error = WILC_FAIL;
	return s32Error;

}

/**
 *  @brief                      creates the header of the Configuration Packet
 *  @details
 *  @param[in,out] pcpacket The Configuration Packet
 *  @param[in,out] ps32PacketLength Length of the Configuration Packet
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		aismail
 *  @date		18 Feb 2012
 *  @version		1.0
 */

s32 CreatePacketHeader(char *pcpacket, s32 *ps32PacketLength)
{
	s32 s32Error = WILC_SUCCESS;
	u16 u16MsgLen = (u16)(*ps32PacketLength);
	u16 u16MsgInd = 0;

	/* The format of the message is:                                         */
	/* +-------------------------------------------------------------------+ */
	/* | Message Type | Message ID |  Message Length |Message body         | */
	/* +-------------------------------------------------------------------+ */
	/* |     1 Byte   |   1 Byte   |     2 Bytes     | Message Length - 4  | */
	/* +-------------------------------------------------------------------+ */

	/* The format of a message body of a message type 'W' is:                */
	/* +-------------------------------------------------------------------+ */
	/* | WID0      | WID0 Length | WID0 Value  | ......................... | */
	/* +-------------------------------------------------------------------+ */
	/* | 2 Bytes   | 1 Byte      | WID0 Length | ......................... | */
	/* +-------------------------------------------------------------------+ */



	/* Message Type */
	if (g_oper_mode == SET_CFG)
		pcpacket[u16MsgInd++] = WRITE_MSG_TYPE;
	else
		pcpacket[u16MsgInd++] = QUERY_MSG_TYPE;

	/* Sequence Number */
	pcpacket[u16MsgInd++] = g_seqno++;

	/* Message Length */
	pcpacket[u16MsgInd++] = (u8)(u16MsgLen & 0xFF);
	pcpacket[u16MsgInd++] = (u8)((u16MsgLen >> 8) & 0xFF);

	*ps32PacketLength = u16MsgLen;

	return s32Error;
}

/**
 *  @brief              creates Configuration packet based on the Input WIDs
 *  @details
 *  @param[in]  pstrWIDs WIDs to be sent in the configuration packet
 *  @param[in]  u32WIDsCount number of WIDs to be sent in the configuration packet
 *  @param[out]         ps8packet The created Configuration Packet
 *  @param[out]         ps32PacketLength Length of the created Configuration Packet
 *  @return     Error code indicating success/failure
 *  @note
 *  @author
 *  @date		1 Mar 2012
 *  @version	1.0
 */

s32 CreateConfigPacket(s8 *ps8packet, s32 *ps32PacketLength,
			       tstrWID *pstrWIDs, u32 u32WIDsCount)
{
	s32 s32Error = WILC_SUCCESS;
	u32 u32idx = 0;
	*ps32PacketLength = MSG_HEADER_LEN;
	for (u32idx = 0; u32idx < u32WIDsCount; u32idx++) {
		switch (pstrWIDs[u32idx].enuWIDtype) {
		case WID_CHAR:
			ProcessCharWid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
				       pstrWIDs[u32idx].ps8WidVal);
			break;

		case WID_SHORT:
			ProcessShortWid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
					pstrWIDs[u32idx].ps8WidVal);
			break;

		case WID_INT:
			ProcessIntWid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
				      pstrWIDs[u32idx].ps8WidVal);
			break;

		case WID_STR:
			ProcessStrWid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
				      pstrWIDs[u32idx].ps8WidVal, pstrWIDs[u32idx].s32ValueSize);
			break;

		case WID_IP:
			ProcessIPwid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
				     pstrWIDs[u32idx].ps8WidVal);
			break;

		case WID_BIN_DATA:
			ProcessBinWid(ps8packet, ps32PacketLength, &pstrWIDs[u32idx],
				      pstrWIDs[u32idx].ps8WidVal, pstrWIDs[u32idx].s32ValueSize);
			break;

		default:
			PRINT_ER("ERROR: Check Config database\n");
		}
	}

	CreatePacketHeader(ps8packet, ps32PacketLength);

	return s32Error;
}

s32 ConfigWaitResponse(char *pcRespBuffer, s32 s32MaxRespBuffLen, s32 *ps32BytesRead,
			       bool bRespRequired)
{
	s32 s32Error = WILC_SUCCESS;
	/*bug 3878*/
	/*removed to caller function*/
	/*gstrConfigPktInfo.pcRespBuffer = pcRespBuffer;
	 * gstrConfigPktInfo.s32MaxRespBuffLen = s32MaxRespBuffLen;
	 * gstrConfigPktInfo.bRespRequired = bRespRequired;*/


	if (gstrConfigPktInfo.bRespRequired) {
		down(&SemHandlePktResp);

		*ps32BytesRead = gstrConfigPktInfo.s32BytesRead;
	}

	WILC_memset((void *)(&gstrConfigPktInfo), 0, sizeof(tstrConfigPktInfo));

	return s32Error;
}

/**
 *  @brief              sends certain Configuration Packet based on the input WIDs pstrWIDs
 *                      and retrieves the packet response pu8RxResp
 *  @details
 *  @param[in]  pstrWIDs WIDs to be sent in the configuration packet
 *  @param[in]  u32WIDsCount number of WIDs to be sent in the configuration packet
 *  @param[out]         pu8RxResp The received Packet Response
 *  @param[out]         ps32RxRespLen Length of the received Packet Response
 *  @return     Error code indicating success/failure
 *  @note
 *  @author	mabubakr
 *  @date		1 Mar 2012
 *  @version	1.0
 */
#ifdef SIMULATION
s32 SendConfigPkt(u8 u8Mode, tstrWID *pstrWIDs,
			  u32 u32WIDsCount, bool bRespRequired, u32 drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
	s32 err = WILC_SUCCESS;
	s32 s32ConfigPacketLen = 0;
	s32 s32RcvdRespLen = 0;

	down(&SemHandleSendPkt);

	/*set the packet mode*/
	g_oper_mode = u8Mode;

	WILC_memset((void *)gps8ConfigPacket, 0, MAX_PACKET_BUFF_SIZE);

	if (CreateConfigPacket(gps8ConfigPacket, &s32ConfigPacketLen, pstrWIDs, u32WIDsCount) != WILC_SUCCESS) {
		s32Error = WILC_FAIL;
		goto End_ConfigPkt;
	}
	/*bug 3878*/
	gstrConfigPktInfo.pcRespBuffer = gps8ConfigPacket;
	gstrConfigPktInfo.s32MaxRespBuffLen = MAX_PACKET_BUFF_SIZE;
	PRINT_INFO(CORECONFIG_DBG, "GLOBAL =bRespRequired =%d\n", bRespRequired);
	gstrConfigPktInfo.bRespRequired = bRespRequired;

	s32Error = SendRawPacket(gps8ConfigPacket, s32ConfigPacketLen);
	if (s32Error != WILC_SUCCESS) {
		goto End_ConfigPkt;
	}

	WILC_memset((void *)gps8ConfigPacket, 0, MAX_PACKET_BUFF_SIZE);

	ConfigWaitResponse(gps8ConfigPacket, MAX_PACKET_BUFF_SIZE, &s32RcvdRespLen, bRespRequired);


	if (bRespRequired)	{
		/* If the operating Mode is GET, then we expect a response frame from */
		/* the driver. Hence start listening to the port for response         */
		if (g_oper_mode == GET_CFG) {
			#if 1
			err = ParseResponse(gps8ConfigPacket, pstrWIDs);
			if (err != 0) {
				s32Error = WILC_FAIL;
				goto End_ConfigPkt;
			} else {
				s32Error = WILC_SUCCESS;
			}
			#endif
		} else {
			err = ParseWriteResponse(gps8ConfigPacket);
			if (err != WRITE_RESP_SUCCESS) {
				s32Error = WILC_FAIL;
				goto End_ConfigPkt;
			} else {
				s32Error = WILC_SUCCESS;
			}
		}


	}


End_ConfigPkt:
	up(&SemHandleSendPkt);

	return s32Error;
}
#endif
s32 ConfigProvideResponse(char *pcRespBuffer, s32 s32RespLen)
{
	s32 s32Error = WILC_SUCCESS;

	if (gstrConfigPktInfo.bRespRequired) {
		if (s32RespLen <= gstrConfigPktInfo.s32MaxRespBuffLen) {
			WILC_memcpy(gstrConfigPktInfo.pcRespBuffer, pcRespBuffer, s32RespLen);
			gstrConfigPktInfo.s32BytesRead = s32RespLen;
		} else {
			WILC_memcpy(gstrConfigPktInfo.pcRespBuffer, pcRespBuffer, gstrConfigPktInfo.s32MaxRespBuffLen);
			gstrConfigPktInfo.s32BytesRead = gstrConfigPktInfo.s32MaxRespBuffLen;
			PRINT_ER("BusProvideResponse() Response greater than the prepared Buffer Size\n");
		}

		up(&SemHandlePktResp);
	}

	return s32Error;
}

/**
 *  @brief              writes the received packet pu8RxPacket in the global Rx FIFO buffer
 *  @details
 *  @param[in]  pu8RxPacket The received packet
 *  @param[in]  s32RxPacketLen Length of the received packet
 *  @return     Error code indicating success/failure
 *  @note
 *
 *  @author	mabubakr
 *  @date		1 Mar 2012
 *  @version	1.0
 */

s32 ConfigPktReceived(u8 *pu8RxPacket, s32 s32RxPacketLen)
{
	s32 s32Error = WILC_SUCCESS;
	u8 u8MsgType = 0;

	u8MsgType = pu8RxPacket[0];

	switch (u8MsgType) {
	case 'R':
		ConfigProvideResponse(pu8RxPacket, s32RxPacketLen);

		break;

	case 'N':
		PRINT_INFO(CORECONFIG_DBG, "NetworkInfo packet received\n");
		NetworkInfoReceived(pu8RxPacket, s32RxPacketLen);
		break;

	case 'I':
		GnrlAsyncInfoReceived(pu8RxPacket, s32RxPacketLen);
		break;

	case 'S':
		host_int_ScanCompleteReceived(pu8RxPacket, s32RxPacketLen);
		break;

	default:
		PRINT_ER("ConfigPktReceived(): invalid received msg type at the Core Configurator\n");
		break;
	}

	return s32Error;
}

/**
 *  @brief              Deinitializes the Core Configurator
 *  @details
 *  @return     Error code indicating success/failure
 *  @note
 *  @author	mabubakr
 *  @date		1 Mar 2012
 *  @version	1.0
 */

s32 CoreConfiguratorDeInit(void)
{
	s32 s32Error = WILC_SUCCESS;

	PRINT_D(CORECONFIG_DBG, "CoreConfiguratorDeInit()\n");

	if (gps8ConfigPacket != NULL) {

		WILC_FREE(gps8ConfigPacket);
		gps8ConfigPacket = NULL;
	}

	return s32Error;
}


#ifndef SIMULATION
/*Using the global handle of the driver*/
extern wilc_wlan_oup_t *gpstrWlanOps;
/**
 *  @brief              sends certain Configuration Packet based on the input WIDs pstrWIDs
 *  using driver config layer
 *
 *  @details
 *  @param[in]  pstrWIDs WIDs to be sent in the configuration packet
 *  @param[in]  u32WIDsCount number of WIDs to be sent in the configuration packet
 *  @param[out]         pu8RxResp The received Packet Response
 *  @param[out]         ps32RxRespLen Length of the received Packet Response
 *  @return     Error code indicating success/failure
 *  @note
 *  @author	mabubakr
 *  @date		1 Mar 2012
 *  @version	1.0
 */
s32 SendConfigPkt(u8 u8Mode, tstrWID *pstrWIDs,
			  u32 u32WIDsCount, bool bRespRequired, u32 drvHandler)
{
	s32 counter = 0, ret = 0;
	if (gpstrWlanOps == NULL) {
		PRINT_D(CORECONFIG_DBG, "Net Dev is still not initialized\n");
		return 1;
	} else {
		PRINT_D(CORECONFIG_DBG, "Net Dev is initialized\n");
	}
	if (gpstrWlanOps->wlan_cfg_set == NULL ||
	    gpstrWlanOps->wlan_cfg_get == NULL)	{
		PRINT_D(CORECONFIG_DBG, "Set and Get is still not initialized\n");
		return 1;
	} else {
		PRINT_D(CORECONFIG_DBG, "SET is initialized\n");
	}
	if (u8Mode == GET_CFG) {
		for (counter = 0; counter < u32WIDsCount; counter++) {
			PRINT_INFO(CORECONFIG_DBG, "Sending CFG packet [%d][%d]\n", !counter,
				   (counter == u32WIDsCount - 1));
			if (!gpstrWlanOps->wlan_cfg_get(!counter,
							pstrWIDs[counter].u16WIDid,
							(counter == u32WIDsCount - 1), drvHandler)) {
				ret = -1;
				printk("[Sendconfigpkt]Get Timed out\n");
				break;
			}
		}
		/**
		 *      get the value
		 **/
		/* WILC_Sleep(1000); */
		counter = 0;
		for (counter = 0; counter < u32WIDsCount; counter++) {
			pstrWIDs[counter].s32ValueSize = gpstrWlanOps->wlan_cfg_get_value(
					pstrWIDs[counter].u16WIDid,
					pstrWIDs[counter].ps8WidVal, pstrWIDs[counter].s32ValueSize);

		}
	} else if (u8Mode == SET_CFG) {
		for (counter = 0; counter < u32WIDsCount; counter++) {
			PRINT_D(CORECONFIG_DBG, "Sending config SET PACKET WID:%x\n", pstrWIDs[counter].u16WIDid);
			if (!gpstrWlanOps->wlan_cfg_set(!counter,
							pstrWIDs[counter].u16WIDid, pstrWIDs[counter].ps8WidVal,
							pstrWIDs[counter].s32ValueSize,
							(counter == u32WIDsCount - 1), drvHandler)) {
				ret = -1;
				printk("[Sendconfigpkt]Set Timed out\n");
				break;
			}
		}
	}

	return ret;
}
#endif
