
/*!
 *  @file	coreconfigurator.c
 *  @brief
 *  @author
 *  @sa		coreconfigurator.h
 *  @date	1 Mar 2012
 *  @version	1.0
 */

#include "coreconfigurator.h"
#include "wilc_wlan_if.h"
#include "wilc_wlan.h"
#include <linux/errno.h>
#include <linux/slab.h>
#define TAG_PARAM_OFFSET	(MAC_HDR_LEN + TIME_STAMP_LEN + \
							BEACON_INTERVAL_LEN + CAP_INFO_LEN)

/* Basic Frame Type Codes (2-bit) */
enum basic_frame_type {
	FRAME_TYPE_CONTROL     = 0x04,
	FRAME_TYPE_DATA        = 0x08,
	FRAME_TYPE_MANAGEMENT  = 0x00,
	FRAME_TYPE_RESERVED    = 0x0C,
	FRAME_TYPE_FORCE_32BIT = 0xFFFFFFFF
};

/* Frame Type and Subtype Codes (6-bit) */
enum sub_frame_type {
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
};

/* Element ID  of various Information Elements */
enum info_element_id {
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
};

/* This function extracts the beacon period field from the beacon or probe   */
/* response frame.                                                           */
static inline u16 get_beacon_period(u8 *data)
{
	u16 bcn_per;

	bcn_per  = data[0];
	bcn_per |= (data[1] << 8);

	return bcn_per;
}

static inline u32 get_beacon_timestamp_lo(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = MAC_HDR_LEN;

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

static inline u32 get_beacon_timestamp_hi(u8 *data)
{
	u32 time_stamp = 0;
	u32 index    = (MAC_HDR_LEN + 4);

	time_stamp |= data[index++];
	time_stamp |= (data[index++] << 8);
	time_stamp |= (data[index++] << 16);
	time_stamp |= (data[index]   << 24);

	return time_stamp;
}

/* This function extracts the 'frame type and sub type' bits from the MAC    */
/* header of the input frame.                                                */
/* Returns the value in the LSB of the returned value.                       */
static inline enum sub_frame_type get_sub_type(u8 *header)
{
	return ((enum sub_frame_type)(header[0] & 0xFC));
}

/* This function extracts the 'to ds' bit from the MAC header of the input   */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
static inline u8 get_to_ds(u8 *header)
{
	return (header[1] & 0x01);
}

/* This function extracts the 'from ds' bit from the MAC header of the input */
/* frame.                                                                    */
/* Returns the value in the LSB of the returned value.                       */
static inline u8 get_from_ds(u8 *header)
{
	return ((header[1] & 0x02) >> 1);
}

/* This function extracts the MAC Address in 'address1' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
static inline void get_address1(u8 *pu8msa, u8 *addr)
{
	memcpy(addr, pu8msa + 4, 6);
}

/* This function extracts the MAC Address in 'address2' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
static inline void get_address2(u8 *pu8msa, u8 *addr)
{
	memcpy(addr, pu8msa + 10, 6);
}

/* This function extracts the MAC Address in 'address3' field of the MAC     */
/* header and updates the MAC Address in the allocated 'addr' variable.      */
static inline void get_address3(u8 *pu8msa, u8 *addr)
{
	memcpy(addr, pu8msa + 16, 6);
}

/* This function extracts the BSSID from the incoming WLAN packet based on   */
/* the 'from ds' bit, and updates the MAC Address in the allocated 'addr'    */
/* variable.                                                                 */
static inline void get_BSSID(u8 *data, u8 *bssid)
{
	if (get_from_ds(data) == 1)
		get_address2(data, bssid);
	else if (get_to_ds(data) == 1)
		get_address1(data, bssid);
	else
		get_address3(data, bssid);
}

/* This function extracts the SSID from a beacon/probe response frame        */
static inline void get_ssid(u8 *data, u8 *ssid, u8 *p_ssid_len)
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
static inline u16 get_cap_info(u8 *data)
{
	u16 cap_info = 0;
	u16 index    = MAC_HDR_LEN;
	enum sub_frame_type st;

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
static inline u16 get_assoc_resp_cap_info(u8 *data)
{
	u16 cap_info;

	cap_info  = data[0];
	cap_info |= (data[1] << 8);

	return cap_info;
}

/* This function extracts the association status code from the incoming       */
/* association response frame and returns association status code            */
static inline u16 get_asoc_status(u8 *data)
{
	u16 asoc_status;

	asoc_status = data[3];
	asoc_status = (asoc_status << 8) | data[2];

	return asoc_status;
}

/* This function extracts association ID from the incoming association       */
/* response frame							                                     */
static inline u16 get_asoc_id(u8 *data)
{
	u16 asoc_id;

	asoc_id  = data[4];
	asoc_id |= (data[5] << 8);

	return asoc_id;
}

u8 *get_tim_elm(u8 *pu8msa, u16 u16RxLen, u16 u16TagParamOffset)
{
	u16 u16index;

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
		if (pu8msa[u16index] == ITIM)
			return &pu8msa[u16index];
		u16index += (IE_HDR_LEN + pu8msa[u16index + 1]);
	}

	return NULL;
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
		/* Increment index by length information and header */
		index += pu8msa[index + 1] + IE_HDR_LEN;
	}

	/* Return current channel information from the MIB, if beacon/probe  */
	/* response frame does not contain the DS parameter set IE           */
	/* return (mget_CurrentChannel() + 1); */
	return 0;  /* no MIB here */
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
s32 parse_network_info(u8 *pu8MsgBuffer, tstrNetworkInfo **ppstrNetworkInfo)
{
	tstrNetworkInfo *pstrNetworkInfo = NULL;
	u8 u8MsgType = 0;
	u8 u8MsgID = 0;
	u16 u16MsgLen = 0;

	u16 u16WidID = (u16)WID_NIL;
	u16 u16WidLen  = 0;
	u8  *pu8WidVal = NULL;

	u8MsgType = pu8MsgBuffer[0];

	/* Check whether the received message type is 'N' */
	if ('N' != u8MsgType) {
		PRINT_ER("Received Message format incorrect.\n");
		return -EFAULT;
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
		u8  *pu8msa = NULL;
		u16 u16RxLen = 0;
		u8 *pu8TimElm = NULL;
		u8 *pu8IEs = NULL;
		u16 u16IEsLen = 0;
		u8 u8index = 0;
		u32 u32Tsf_Lo;
		u32 u32Tsf_Hi;

		pstrNetworkInfo = kzalloc(sizeof(tstrNetworkInfo), GFP_KERNEL);
		if (!pstrNetworkInfo)
			return -ENOMEM;

		pstrNetworkInfo->s8rssi = pu8WidVal[0];

		/* Assign a pointer to msa "Mac Header Start Address" */
		pu8msa = &pu8WidVal[1];

		u16RxLen = u16WidLen - 1;

		/* parse msa*/

		/* Get the cap_info */
		pstrNetworkInfo->u16CapInfo = get_cap_info(pu8msa);
		/* Get time-stamp [Low only 32 bit] */
		pstrNetworkInfo->u32Tsf = get_beacon_timestamp_lo(pu8msa);
		PRINT_D(CORECONFIG_DBG, "TSF :%x\n", pstrNetworkInfo->u32Tsf);

		/* Get full time-stamp [Low and High 64 bit] */
		u32Tsf_Lo = get_beacon_timestamp_lo(pu8msa);
		u32Tsf_Hi = get_beacon_timestamp_hi(pu8msa);

		pstrNetworkInfo->u64Tsf = u32Tsf_Lo | ((u64)u32Tsf_Hi << 32);

		/* Get SSID */
		get_ssid(pu8msa, pstrNetworkInfo->au8ssid, &pstrNetworkInfo->u8SsidLen);

		/* Get BSSID */
		get_BSSID(pu8msa, pstrNetworkInfo->au8bssid);

		/*
		 * Extract current channel information from
		 * the beacon/probe response frame
		 */
		pstrNetworkInfo->u8channel = get_current_channel_802_11n(pu8msa,
							u16RxLen + FCS_LEN);

		/* Get beacon period */
		u8index = MAC_HDR_LEN + TIME_STAMP_LEN;

		pstrNetworkInfo->u16BeaconPeriod = get_beacon_period(pu8msa + u8index);

		u8index += BEACON_INTERVAL_LEN + CAP_INFO_LEN;

		/* Get DTIM Period */
		pu8TimElm = get_tim_elm(pu8msa, u16RxLen + FCS_LEN, u8index);
		if (pu8TimElm != NULL)
			pstrNetworkInfo->u8DtimPeriod = pu8TimElm[3];
		pu8IEs = &pu8msa[MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN + CAP_INFO_LEN];
		u16IEsLen = u16RxLen - (MAC_HDR_LEN + TIME_STAMP_LEN + BEACON_INTERVAL_LEN + CAP_INFO_LEN);

		if (u16IEsLen > 0) {
			pstrNetworkInfo->pu8IEs = kmemdup(pu8IEs, u16IEsLen,
							  GFP_KERNEL);
			if (!pstrNetworkInfo->pu8IEs)
				return -ENOMEM;
		}
		pstrNetworkInfo->u16IEsLen = u16IEsLen;

	}

	*ppstrNetworkInfo = pstrNetworkInfo;

	return 0;
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
	s32 s32Error = 0;

	if (pstrNetworkInfo != NULL) {
		if (pstrNetworkInfo->pu8IEs != NULL) {
			kfree(pstrNetworkInfo->pu8IEs);
			pstrNetworkInfo->pu8IEs = NULL;
		} else {
			s32Error = -EFAULT;
		}

		kfree(pstrNetworkInfo);
		pstrNetworkInfo = NULL;

	} else {
		s32Error = -EFAULT;
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
	s32 s32Error = 0;
	tstrConnectRespInfo *pstrConnectRespInfo = NULL;
	u16 u16AssocRespLen = 0;
	u8 *pu8IEs = NULL;
	u16 u16IEsLen = 0;

	pstrConnectRespInfo = kzalloc(sizeof(tstrConnectRespInfo), GFP_KERNEL);
	if (!pstrConnectRespInfo)
		return -ENOMEM;

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

		pstrConnectRespInfo->pu8RespIEs = kmemdup(pu8IEs, u16IEsLen, GFP_KERNEL);
		if (!pstrConnectRespInfo->pu8RespIEs)
			return -ENOMEM;

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
	s32 s32Error = 0;

	if (pstrConnectRespInfo != NULL) {
		if (pstrConnectRespInfo->pu8RespIEs != NULL) {
			kfree(pstrConnectRespInfo->pu8RespIEs);
			pstrConnectRespInfo->pu8RespIEs = NULL;
		} else {
			s32Error = -EFAULT;
		}

		kfree(pstrConnectRespInfo);
		pstrConnectRespInfo = NULL;

	} else {
		s32Error = -EFAULT;
	}

	return s32Error;
}

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
s32 send_config_pkt(u8 mode, struct wid *wids, u32 count, u32 drv)
{
	s32 counter = 0, ret = 0;

	if (mode == GET_CFG) {
		for (counter = 0; counter < count; counter++) {
			PRINT_INFO(CORECONFIG_DBG, "Sending CFG packet [%d][%d]\n", !counter,
				   (counter == count - 1));
			if (!wilc_wlan_cfg_get(!counter,
					       wids[counter].id,
					       (counter == count - 1),
					       drv)) {
				ret = -1;
				printk("[Sendconfigpkt]Get Timed out\n");
				break;
			}
		}
		counter = 0;
		for (counter = 0; counter < count; counter++) {
			wids[counter].size = wilc_wlan_cfg_get_val(
					wids[counter].id,
					wids[counter].val,
					wids[counter].size);

		}
	} else if (mode == SET_CFG) {
		for (counter = 0; counter < count; counter++) {
			PRINT_D(CORECONFIG_DBG, "Sending config SET PACKET WID:%x\n", wids[counter].id);
			if (!wilc_wlan_cfg_set(!counter,
					       wids[counter].id,
					       wids[counter].val,
					       wids[counter].size,
					       (counter == count - 1),
					       drv)) {
				ret = -1;
				printk("[Sendconfigpkt]Set Timed out\n");
				break;
			}
		}
	}

	return ret;
}
