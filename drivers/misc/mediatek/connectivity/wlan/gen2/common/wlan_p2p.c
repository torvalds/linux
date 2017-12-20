/*
** Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/common/wlan_p2p.c#8
*/

/*! \file wlan_bow.c
    \brief This file contains the Wi-Fi Direct commands processing routines for
	   MediaTek Inc. 802.11 Wireless LAN Adapters.
*/

/*
** Log: wlan_p2p.c
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 11 24 2011 yuche.tsai
 * NULL
 * Fix P2P IOCTL of multicast address bug, add low power driver stop control.
 *
 * 11 22 2011 yuche.tsai
 * NULL
 * Update RSSI link quality of P2P Network query method. (Bug fix)
 *
 * 11 19 2011 yuche.tsai
 * NULL
 * Add RSSI support for P2P network.
 *
 * 11 08 2011 yuche.tsai
 * [WCXRP00001094] [Volunteer Patch][Driver] Driver version & supplicant version query & set support
 * for service discovery version check.
 * Add support for driver version query & p2p supplicant verseion set.
 * For new service discovery mechanism sync.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Support Channel Query.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * New 2.1 branch

 *
 * 08 23 2011 yuche.tsai
 * NULL
 * Fix Multicast Issue of P2P.
 *
 * 04 27 2011 george.huang
 * [WCXRP00000684] [MT6620 Wi-Fi][Driver] Support P2P setting ARP filter
 * Support P2P ARP filter setting on early suspend/ late resume
 *
 * 04 08 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * separate settings of P2P and AIS
 *
 * 03 22 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * link with supplicant commands
 *
 * 03 17 2011 wh.su
 * [WCXRP00000571] [MT6620 Wi-Fi] [Driver] Not check the p2p role during set key
 * Skip the p2p role for adding broadcast key issue.
 *
 * 03 16 2011 wh.su
 * [WCXRP00000530] [MT6620 Wi-Fi] [Driver] skip doing p2pRunEventAAAComplete after send assoc response Tx Done
 * fixed compiling error while enable dbg.
 *
 * 03 08 2011 yuche.tsai
 * [WCXRP00000480] [Volunteer Patch][MT6620][Driver] WCS IE format
 * issue[WCXRP00000509] [Volunteer Patch][MT6620][Driver] Kernal panic when remove p2p module.
 * .
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add Security check related code.
 *
 * 03 02 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Fix SD Request Query Length issue.
 *
 * 03 02 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Service Discovery Request.
 *
 * 03 01 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Update Service Discovery Wlan OID related function.
 *
 * 03 01 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Update Service Discovery Related wlanoid function.
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Add Service Discovery Indication Related code.
 *
 * 01 26 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Add Service Discovery Function.
 *
 * 01 05 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface
 *  for supporting Wi-Fi Direct Service Discovery ioctl implementations for P2P Service Discovery
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to
 * ease physically continuous memory demands separate kalMemAlloc() into virtually-continuous
 * and physically-continuous type to ease slab system pressure
 *
 * 12 22 2010 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface
 * for supporting Wi-Fi Direct Service Discovery
 * 1. header file restructure for more clear module isolation
 * 2. add function interface definition for implementing Service Discovery callbacks
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T
 * and replaced by ENUM_NETWORK_TYPE_INDEX_T only remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 28 2010 wh.su
 * NULL
 * [WCXRP00000069][MT6620 Wi-Fi][Driver] Fix some code for phase 1 P2P Demo.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000054] [MT6620 Wi-Fi][Driver] Restructure driver for second Interface
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 23 2010 cp.wu
 * NULL
 * revise constant definitions to be matched with implementation (original cmd-event definition is deprecated)
 *
 * 08 16 2010 cp.wu
 * NULL
 * add subroutines for P2P to set multicast list.
 *
 * 08 16 2010 george.huang
 * NULL
 * .
 *
 * 08 16 2010 george.huang
 * NULL
 * support wlanoidSetP2pPowerSaveProfile() in P2P
 *
 * 08 16 2010 george.huang
 * NULL
 * Support wlanoidSetNetworkAddress() for P2P
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 25 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add API in que_mgt to retrieve sta-rec index for security frames.
 *
 * 06 24 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 802.1x and bluetooth-over-Wi-Fi security frames are now delievered to firmware via command path instead of data path.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
**
*/

/******************************************************************************
*                         C O M P I L E R   F L A G S
*******************************************************************************
*/

/******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
*******************************************************************************
*/
#include "precomp.h"
#include "gl_p2p_ioctl.h"

/******************************************************************************
*                              C O N S T A N T S
*******************************************************************************
*/

/******************************************************************************
*                             D A T A   T Y P E S
*******************************************************************************
*/

/******************************************************************************
*                            P U B L I C   D A T A
*******************************************************************************
*/

/******************************************************************************
*                           P R I V A T E   D A T A
*******************************************************************************
*/

/******************************************************************************
*                                 M A C R O S
*******************************************************************************
*/

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/

/******************************************************************************
*                              F U N C T I O N S
*******************************************************************************
*/
/*----------------------------------------------------------------------------*/
/*!
* \brief command packet generation utility
*
* \param[in] prAdapter          Pointer to the Adapter structure.
* \param[in] ucCID              Command ID
* \param[in] fgSetQuery         Set or Query
* \param[in] fgNeedResp         Need for response
* \param[in] pfCmdDoneHandler   Function pointer when command is done
* \param[in] u4SetQueryInfoLen  The length of the set/query buffer
* \param[in] pucInfoBuffer      Pointer to set/query buffer
*
*
* \retval WLAN_STATUS_PENDING
* \retval WLAN_STATUS_FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSendSetQueryP2PCmd(IN P_ADAPTER_T prAdapter,
			  UINT_8 ucCID,
			  BOOLEAN fgSetQuery,
			  BOOLEAN fgNeedResp,
			  BOOLEAN fgIsOid,
			  PFN_CMD_DONE_HANDLER pfCmdDoneHandler,
			  PFN_CMD_TIMEOUT_HANDLER pfCmdTimeoutHandler,
			  UINT_32 u4SetQueryInfoLen,
			  PUINT_8 pucInfoBuffer, OUT PVOID pvSetQueryBuffer, IN UINT_32 u4SetQueryBufferLen)
{
	P_GLUE_INFO_T prGlueInfo;
	P_CMD_INFO_T prCmdInfo;
	P_WIFI_CMD_T prWifiCmd;
	UINT_8 ucCmdSeqNum;

	ASSERT(prAdapter);

	prGlueInfo = prAdapter->prGlueInfo;
	ASSERT(prGlueInfo);

	DEBUGFUNC("wlanoidSendSetQueryP2PCmd");
	DBGLOG(REQ, TRACE, "Command ID = 0x%08X\n", ucCID);

	prCmdInfo = cmdBufAllocateCmdInfo(prAdapter, (CMD_HDR_SIZE + u4SetQueryInfoLen));

	if (!prCmdInfo) {
		DBGLOG(P2P, ERROR, "Allocate CMD_INFO_T ==> FAILED.\n");
		return WLAN_STATUS_FAILURE;
	}
	/* increase command sequence number */
	ucCmdSeqNum = nicIncreaseCmdSeqNum(prAdapter);
	DBGLOG(REQ, TRACE, "ucCmdSeqNum =%d\n", ucCmdSeqNum);

	/* Setup common CMD Info Packet */
	prCmdInfo->eCmdType = COMMAND_TYPE_NETWORK_IOCTL;
	prCmdInfo->eNetworkType = NETWORK_TYPE_P2P_INDEX;
	prCmdInfo->u2InfoBufLen = (UINT_16) (CMD_HDR_SIZE + u4SetQueryInfoLen);
	prCmdInfo->pfCmdDoneHandler = pfCmdDoneHandler;
	prCmdInfo->pfCmdTimeoutHandler = pfCmdTimeoutHandler;
	prCmdInfo->fgIsOid = fgIsOid;
	prCmdInfo->ucCID = ucCID;
	prCmdInfo->fgSetQuery = fgSetQuery;
	prCmdInfo->fgNeedResp = fgNeedResp;
	prCmdInfo->fgDriverDomainMCR = FALSE;
	prCmdInfo->ucCmdSeqNum = ucCmdSeqNum;
	prCmdInfo->u4SetInfoLen = u4SetQueryInfoLen;
	prCmdInfo->pvInformationBuffer = pvSetQueryBuffer;
	prCmdInfo->u4InformationBufferLength = u4SetQueryBufferLen;

	/* Setup WIFI_CMD_T (no payload) */
	prWifiCmd = (P_WIFI_CMD_T) (prCmdInfo->pucInfoBuffer);
	prWifiCmd->u2TxByteCount_UserPriority = prCmdInfo->u2InfoBufLen;
	prWifiCmd->ucCID = prCmdInfo->ucCID;
	prWifiCmd->ucSetQuery = prCmdInfo->fgSetQuery;
	prWifiCmd->ucSeqNum = prCmdInfo->ucCmdSeqNum;

	if (u4SetQueryInfoLen > 0 && pucInfoBuffer != NULL)
		kalMemCopy(prWifiCmd->aucBuffer, pucInfoBuffer, u4SetQueryInfoLen);
	/* insert into prCmdQueue */
	kalEnqueueCommand(prGlueInfo, (P_QUE_ENTRY_T) prCmdInfo);

	/* wakeup txServiceThread later */
	GLUE_SET_EVENT(prGlueInfo);
	return WLAN_STATUS_PENDING;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set a key to Wi-Fi Direct driver
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetAddP2PKey(IN P_ADAPTER_T prAdapter,
		    IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	CMD_802_11_KEY rCmdKey;
	P_PARAM_KEY_T prNewKey;

	DEBUGFUNC("wlanoidSetAddP2PKey");
	DBGLOG(REQ, INFO, "\n");

	ASSERT(prAdapter);
	ASSERT(pvSetBuffer);
	ASSERT(pu4SetInfoLen);

	prNewKey = (P_PARAM_KEY_T) pvSetBuffer;

	/* Verify the key structure length. */
	if (prNewKey->u4Length > u4SetBufferLen) {
		DBGLOG(REQ, WARN, "Invalid key structure length (%d) greater than total buffer length (%d)\n",
				   (UINT_8) prNewKey->u4Length, (UINT_8) u4SetBufferLen);

		*pu4SetInfoLen = u4SetBufferLen;
		return WLAN_STATUS_INVALID_LENGTH;
	}
	/* Verify the key material length for key material buffer */
	else if (prNewKey->u4KeyLength > prNewKey->u4Length - OFFSET_OF(PARAM_KEY_T, aucKeyMaterial)) {
		DBGLOG(REQ, WARN, "Invalid key material length (%d)\n", (UINT_8) prNewKey->u4KeyLength);
		*pu4SetInfoLen = u4SetBufferLen;
		return WLAN_STATUS_INVALID_DATA;
	}
	/* Exception check */
	else if (prNewKey->u4KeyIndex & 0x0fffff00)
		return WLAN_STATUS_INVALID_DATA;
	/* Exception check, pairwise key must with transmit bit enabled */
	else if ((prNewKey->u4KeyIndex & BITS(30, 31)) == IS_UNICAST_KEY) {
		return WLAN_STATUS_INVALID_DATA;
	} else if (!(prNewKey->u4KeyLength == CCMP_KEY_LEN) && !(prNewKey->u4KeyLength == TKIP_KEY_LEN)) {
		return WLAN_STATUS_INVALID_DATA;
	}
	/* Exception check, pairwise key must with transmit bit enabled */
	else if ((prNewKey->u4KeyIndex & BITS(30, 31)) == BITS(30, 31)) {
		if (((prNewKey->u4KeyIndex & 0xff) != 0) ||
		    ((prNewKey->arBSSID[0] == 0xff) && (prNewKey->arBSSID[1] == 0xff) && (prNewKey->arBSSID[2] == 0xff)
		     && (prNewKey->arBSSID[3] == 0xff) && (prNewKey->arBSSID[4] == 0xff)
		     && (prNewKey->arBSSID[5] == 0xff))) {
			return WLAN_STATUS_INVALID_DATA;
		}
	}

	*pu4SetInfoLen = u4SetBufferLen;

	/* fill CMD_802_11_KEY */
	kalMemZero(&rCmdKey, sizeof(CMD_802_11_KEY));
	rCmdKey.ucAddRemove = 1;	/* add */
	rCmdKey.ucTxKey = ((prNewKey->u4KeyIndex & IS_TRANSMIT_KEY) == IS_TRANSMIT_KEY) ? 1 : 0;
	rCmdKey.ucKeyType = ((prNewKey->u4KeyIndex & IS_UNICAST_KEY) == IS_UNICAST_KEY) ? 1 : 0;
	if (kalP2PGetRole(prAdapter->prGlueInfo) == 1) {	/* group client */
		rCmdKey.ucIsAuthenticator = 0;
	} else {		/* group owner */
		rCmdKey.ucIsAuthenticator = 1;
	}
	COPY_MAC_ADDR(rCmdKey.aucPeerAddr, prNewKey->arBSSID);
	rCmdKey.ucNetType = NETWORK_TYPE_P2P_INDEX;
	if (prNewKey->u4KeyLength == CCMP_KEY_LEN)
		rCmdKey.ucAlgorithmId = CIPHER_SUITE_CCMP;	/* AES */
	else if (prNewKey->u4KeyLength == TKIP_KEY_LEN)
		rCmdKey.ucAlgorithmId = CIPHER_SUITE_TKIP;	/* TKIP */
	rCmdKey.ucKeyId = (UINT_8) (prNewKey->u4KeyIndex & 0xff);
	rCmdKey.ucKeyLen = (UINT_8) prNewKey->u4KeyLength;
	kalMemCopy(rCmdKey.aucKeyMaterial, (PUINT_8) prNewKey->aucKeyMaterial, rCmdKey.ucKeyLen);

	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_ADD_REMOVE_KEY,
					 TRUE,
					 FALSE,
					 TRUE,
					 nicCmdEventSetCommon,
					 NULL,
					 sizeof(CMD_802_11_KEY), (PUINT_8) &rCmdKey, pvSetBuffer, u4SetBufferLen);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to request Wi-Fi Direct driver to remove keys
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_DATA
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_INVALID_DATA
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetRemoveP2PKey(IN P_ADAPTER_T prAdapter,
		       IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	CMD_802_11_KEY rCmdKey;
	P_PARAM_REMOVE_KEY_T prRemovedKey;

	DEBUGFUNC("wlanoidSetRemoveP2PKey");
	ASSERT(prAdapter);

	if (u4SetBufferLen < sizeof(PARAM_REMOVE_KEY_T))
		return WLAN_STATUS_INVALID_LENGTH;

	ASSERT(pvSetBuffer);
	prRemovedKey = (P_PARAM_REMOVE_KEY_T) pvSetBuffer;

	/* Check bit 31: this bit should always 0 */
	if (prRemovedKey->u4KeyIndex & IS_TRANSMIT_KEY) {
		/* Bit 31 should not be set */
		DBGLOG(REQ, ERROR, "invalid key index: 0x%08x\n", prRemovedKey->u4KeyIndex);
		return WLAN_STATUS_INVALID_DATA;
	}

	/* Check bits 8 ~ 29 should always be 0 */
	if (prRemovedKey->u4KeyIndex & BITS(8, 29)) {
		/* Bit 31 should not be set */
		DBGLOG(REQ, ERROR, "invalid key index: 0x%08x\n", prRemovedKey->u4KeyIndex);
		return WLAN_STATUS_INVALID_DATA;
	}

	/* There should not be any key operation for P2P Device */
	if (kalP2PGetRole(prAdapter->prGlueInfo) == 0)
		; /* return WLAN_STATUS_NOT_ACCEPTED; */

	kalMemZero((PUINT_8) &rCmdKey, sizeof(CMD_802_11_KEY));

	rCmdKey.ucAddRemove = 0;	/* remove */
	if (kalP2PGetRole(prAdapter->prGlueInfo) == 1) {	/* group client */
		rCmdKey.ucIsAuthenticator = 0;
	} else {		/* group owner */
		rCmdKey.ucIsAuthenticator = 1;
	}
	kalMemCopy(rCmdKey.aucPeerAddr, (PUINT_8) prRemovedKey->arBSSID, MAC_ADDR_LEN);
	rCmdKey.ucNetType = NETWORK_TYPE_P2P_INDEX;
	rCmdKey.ucKeyId = (UINT_8) (prRemovedKey->u4KeyIndex & 0x000000ff);

	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_ADD_REMOVE_KEY,
					 TRUE,
					 FALSE,
					 TRUE,
					 nicCmdEventSetCommon,
					 NULL,
					 sizeof(CMD_802_11_KEY), (PUINT_8) &rCmdKey, pvSetBuffer, u4SetBufferLen);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Setting the IP address for pattern search function.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
* \return WLAN_STATUS_ADAPTER_NOT_READY
* \return WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2pNetworkAddress(IN P_ADAPTER_T prAdapter,
			    IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
	UINT_32 i, j;
	P_CMD_SET_NETWORK_ADDRESS_LIST prCmdNetworkAddressList;
	P_PARAM_NETWORK_ADDRESS_LIST prNetworkAddressList = (P_PARAM_NETWORK_ADDRESS_LIST) pvSetBuffer;
	P_PARAM_NETWORK_ADDRESS prNetworkAddress;
	P_PARAM_NETWORK_ADDRESS_IP prNetAddrIp;
	UINT_32 u4IpAddressCount, u4CmdSize;

	DEBUGFUNC("wlanoidSetP2pNetworkAddress");
	DBGLOG(P2P, TRACE, "\n");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = 4;

	if (u4SetBufferLen < sizeof(PARAM_NETWORK_ADDRESS_LIST))
		return WLAN_STATUS_INVALID_DATA;

	*pu4SetInfoLen = 0;
	u4IpAddressCount = 0;

	prNetworkAddress = prNetworkAddressList->arAddress;
	for (i = 0; i < prNetworkAddressList->u4AddressCount; i++) {
		if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
		    prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
			u4IpAddressCount++;
		}

		prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prNetworkAddress +
							      (ULONG) (prNetworkAddress->u2AddressLength +
								       OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));
	}

	/* construct payload of command packet */
	u4CmdSize = OFFSET_OF(CMD_SET_NETWORK_ADDRESS_LIST, arNetAddress) +
	    sizeof(IPV4_NETWORK_ADDRESS) * u4IpAddressCount;

	prCmdNetworkAddressList = (P_CMD_SET_NETWORK_ADDRESS_LIST) kalMemAlloc(u4CmdSize, VIR_MEM_TYPE);

	if (prCmdNetworkAddressList == NULL)
		return WLAN_STATUS_FAILURE;

	/* fill P_CMD_SET_NETWORK_ADDRESS_LIST */
	prCmdNetworkAddressList->ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
	prCmdNetworkAddressList->ucAddressCount = (UINT_8) u4IpAddressCount;
	prNetworkAddress = prNetworkAddressList->arAddress;
	for (i = 0, j = 0; i < prNetworkAddressList->u4AddressCount; i++) {
		if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
		    prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
			prNetAddrIp = (P_PARAM_NETWORK_ADDRESS_IP) prNetworkAddress->aucAddress;

			kalMemCopy(prCmdNetworkAddressList->arNetAddress[j].aucIpAddr,
				   &(prNetAddrIp->in_addr), sizeof(UINT_32));

			j++;
		}

		prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prNetworkAddress +
							      (ULONG) (prNetworkAddress->u2AddressLength +
								       OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));
	}

	rStatus = wlanSendSetQueryCmd(prAdapter,
				      CMD_ID_SET_IP_ADDRESS,
				      TRUE,
				      FALSE,
				      TRUE,
				      nicCmdEventSetIpAddress,
				      nicOidCmdTimeoutCommon,
				      u4CmdSize, (PUINT_8) prCmdNetworkAddressList, pvSetBuffer, u4SetBufferLen);

	kalMemFree(prCmdNetworkAddressList, VIR_MEM_TYPE, u4CmdSize);
	return rStatus;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to query the power save profile.
*
* \param[in] prAdapter Pointer to the Adapter structure.
* \param[out] pvQueryBuf A pointer to the buffer that holds the result of
*                           the query.
* \param[in] u4QueryBufLen The length of the query buffer.
* \param[out] pu4QueryInfoLen If the call is successful, returns the number of
*                            bytes written into the query buffer. If the call
*                            failed due to invalid length of the query buffer,
*                            returns the amount of storage needed.
*
* \return WLAN_STATUS_SUCCESS
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidQueryP2pPowerSaveProfile(IN P_ADAPTER_T prAdapter,
				IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	DEBUGFUNC("wlanoidQueryP2pPowerSaveProfile");

	ASSERT(prAdapter);
	ASSERT(pu4QueryInfoLen);

	if (u4QueryBufferLen != 0) {
		ASSERT(pvQueryBuffer);

		*(PPARAM_POWER_MODE) pvQueryBuffer =
		    (PARAM_POWER_MODE) (prAdapter->rWlanInfo.arPowerSaveMode[NETWORK_TYPE_P2P_INDEX].ucPsProfile);
		*pu4QueryInfoLen = sizeof(PARAM_POWER_MODE);
	}

	return WLAN_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set the power save profile.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2pPowerSaveProfile(IN P_ADAPTER_T prAdapter,
			      IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS status;
	PARAM_POWER_MODE ePowerMode;

	DEBUGFUNC("wlanoidSetP2pPowerSaveProfile");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = sizeof(PARAM_POWER_MODE);
	if (u4SetBufferLen < sizeof(PARAM_POWER_MODE)) {
		DBGLOG(REQ, WARN, "Invalid length %u\n", u4SetBufferLen);
		return WLAN_STATUS_INVALID_LENGTH;
	} else if (*(PPARAM_POWER_MODE) pvSetBuffer >= Param_PowerModeMax) {
		WARNLOG(("Invalid power mode %d\n", *(PPARAM_POWER_MODE) pvSetBuffer));
		return WLAN_STATUS_INVALID_DATA;
	}

	ePowerMode = *(PPARAM_POWER_MODE) pvSetBuffer;

	if (prAdapter->fgEnCtiaPowerMode) {
		if (ePowerMode == Param_PowerModeCAM) {
			/*Todo::  Nothing*/
			/*Todo::  Nothing*/
		} else {
			/* User setting to PS mode (Param_PowerModeMAX_PSP or Param_PowerModeFast_PSP) */

			if (prAdapter->u4CtiaPowerMode == 0) {
				/* force to keep in CAM mode */
				ePowerMode = Param_PowerModeCAM;
			} else if (prAdapter->u4CtiaPowerMode == 1) {
				ePowerMode = Param_PowerModeMAX_PSP;
			} else if (prAdapter->u4CtiaPowerMode == 2) {
				ePowerMode = Param_PowerModeFast_PSP;
			}
		}
	}

	status = nicConfigPowerSaveProfile(prAdapter, NETWORK_TYPE_P2P_INDEX, ePowerMode, TRUE);
	return status;
}				/* end of wlanoidSetP2pPowerSaveProfile() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set the power save profile.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2pSetNetworkAddress(IN P_ADAPTER_T prAdapter,
			       IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
	UINT_32 i, j;
	P_CMD_SET_NETWORK_ADDRESS_LIST prCmdNetworkAddressList;
	P_PARAM_NETWORK_ADDRESS_LIST prNetworkAddressList = (P_PARAM_NETWORK_ADDRESS_LIST) pvSetBuffer;
	P_PARAM_NETWORK_ADDRESS prNetworkAddress;
	P_PARAM_NETWORK_ADDRESS_IP prNetAddrIp;
	UINT_32 u4IpAddressCount, u4CmdSize;
	PUINT_8 pucBuf = (PUINT_8) pvSetBuffer;

	DEBUGFUNC("wlanoidSetP2pSetNetworkAddress");
	DBGLOG(P2P, TRACE, "\n");
	DBGLOG(P2P, INFO, "wlanoidSetP2pSetNetworkAddress (%d)\n", (INT_16) u4SetBufferLen);

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = 4;

	if (u4SetBufferLen < sizeof(PARAM_NETWORK_ADDRESS_LIST))
		return WLAN_STATUS_INVALID_DATA;

	*pu4SetInfoLen = 0;
	u4IpAddressCount = 0;

	prNetworkAddress = prNetworkAddressList->arAddress;
	for (i = 0; i < prNetworkAddressList->u4AddressCount; i++) {
		if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
		    prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
			u4IpAddressCount++;
		}

		prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prNetworkAddress +
							      (ULONG) (prNetworkAddress->u2AddressLength +
								       OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));
	}

	/* construct payload of command packet */
	u4CmdSize = OFFSET_OF(CMD_SET_NETWORK_ADDRESS_LIST, arNetAddress) +
	    sizeof(IPV4_NETWORK_ADDRESS) * u4IpAddressCount;

	if (u4IpAddressCount == 0)
		u4CmdSize = sizeof(CMD_SET_NETWORK_ADDRESS_LIST);

	prCmdNetworkAddressList = (P_CMD_SET_NETWORK_ADDRESS_LIST) kalMemAlloc(u4CmdSize, VIR_MEM_TYPE);

	if (prCmdNetworkAddressList == NULL)
		return WLAN_STATUS_FAILURE;

	/* fill P_CMD_SET_NETWORK_ADDRESS_LIST */
	prCmdNetworkAddressList->ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;

	/* only to set IP address to FW once ARP filter is enabled */
	if (prAdapter->fgEnArpFilter) {
		prCmdNetworkAddressList->ucAddressCount = (UINT_8) u4IpAddressCount;
		prNetworkAddress = prNetworkAddressList->arAddress;

		DBGLOG(P2P, INFO, "u4IpAddressCount (%u)\n", u4IpAddressCount);
		for (i = 0, j = 0; i < prNetworkAddressList->u4AddressCount; i++) {
			if (prNetworkAddress->u2AddressType == PARAM_PROTOCOL_ID_TCP_IP &&
			    prNetworkAddress->u2AddressLength == sizeof(PARAM_NETWORK_ADDRESS_IP)) {
				prNetAddrIp = (P_PARAM_NETWORK_ADDRESS_IP) prNetworkAddress->aucAddress;

				kalMemCopy(prCmdNetworkAddressList->arNetAddress[j].aucIpAddr,
					   &(prNetAddrIp->in_addr), sizeof(UINT_32));

				j++;

				pucBuf = (PUINT_8) &prNetAddrIp->in_addr;
				DBGLOG(P2P, INFO, "prNetAddrIp->in_addr:%d:%d:%d:%d\n",
					(UINT_8) pucBuf[0], (UINT_8) pucBuf[1],
					(UINT_8) pucBuf[2], (UINT_8) pucBuf[3]);
			}

			prNetworkAddress = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prNetworkAddress +
								      (ULONG) (prNetworkAddress->u2AddressLength +
									       OFFSET_OF(PARAM_NETWORK_ADDRESS,
											 aucAddress)));
		}

	} else {
		prCmdNetworkAddressList->ucAddressCount = 0;
	}

	rStatus = wlanSendSetQueryCmd(prAdapter,
				      CMD_ID_SET_IP_ADDRESS,
				      TRUE,
				      FALSE,
				      TRUE,
				      nicCmdEventSetIpAddress,
				      nicOidCmdTimeoutCommon,
				      u4CmdSize, (PUINT_8) prCmdNetworkAddressList, pvSetBuffer, u4SetBufferLen);

	kalMemFree(prCmdNetworkAddressList, VIR_MEM_TYPE, u4CmdSize);
	return rStatus;
}				/* end of wlanoidSetP2pSetNetworkAddress() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set Multicast Address List.
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2PMulticastList(IN P_ADAPTER_T prAdapter,
			   IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	CMD_MAC_MCAST_ADDR rCmdMacMcastAddr;

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	/* The data must be a multiple of the Ethernet address size. */
	if ((u4SetBufferLen % MAC_ADDR_LEN)) {
		DBGLOG(REQ, WARN, "Invalid MC list length %u\n", u4SetBufferLen);

		*pu4SetInfoLen = (((u4SetBufferLen + MAC_ADDR_LEN) - 1) / MAC_ADDR_LEN) * MAC_ADDR_LEN;

		return WLAN_STATUS_INVALID_LENGTH;
	}

	*pu4SetInfoLen = u4SetBufferLen;

	/* Verify if we can support so many multicast addresses. */
	if ((u4SetBufferLen / MAC_ADDR_LEN) > MAX_NUM_GROUP_ADDR) {
		DBGLOG(REQ, WARN, "Too many MC addresses\n");

		return WLAN_STATUS_MULTICAST_FULL;
	}

	/* NOTE(Kevin): Windows may set u4SetBufferLen == 0 &&
	 * pvSetBuffer == NULL to clear exist Multicast List.
	 */
	if (u4SetBufferLen)
		ASSERT(pvSetBuffer);

	if (prAdapter->rAcpiState == ACPI_STATE_D3) {
		DBGLOG(REQ, WARN, "Fail in set multicast list! (Adapter not ready). ACPI=D%d, Radio=%d\n",
				   prAdapter->rAcpiState, prAdapter->fgIsRadioOff);
		return WLAN_STATUS_ADAPTER_NOT_READY;
	}

	rCmdMacMcastAddr.u4NumOfGroupAddr = u4SetBufferLen / MAC_ADDR_LEN;
	rCmdMacMcastAddr.ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
	kalMemCopy(rCmdMacMcastAddr.arAddress, pvSetBuffer, u4SetBufferLen);

	/* This CMD response is no need to complete the OID. Or the event would unsync. */
	return wlanoidSendSetQueryP2PCmd(prAdapter, CMD_ID_MAC_MCAST_ADDR, TRUE, FALSE, FALSE,
					 nicCmdEventSetCommon,
					 nicOidCmdTimeoutCommon,
					 sizeof(CMD_MAC_MCAST_ADDR),
					 (PUINT_8) &rCmdMacMcastAddr, pvSetBuffer, u4SetBufferLen);

}				/* end of wlanoidSetP2PMulticastList() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to send GAS frame for P2P Service Discovery Request
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSendP2PSDRequest(IN P_ADAPTER_T prAdapter,
			IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	if (u4SetBufferLen)
		ASSERT(pvSetBuffer);

	if (u4SetBufferLen < sizeof(PARAM_P2P_SEND_SD_REQUEST)) {
		*pu4SetInfoLen = sizeof(PARAM_P2P_SEND_SD_REQUEST);
		return WLAN_STATUS_BUFFER_TOO_SHORT;
	}
/* rWlanStatus = p2pFsmRunEventSDRequest(prAdapter, (P_PARAM_P2P_SEND_SD_REQUEST)pvSetBuffer); */

	return rWlanStatus;
}				/* end of wlanoidSendP2PSDRequest() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to send GAS frame for P2P Service Discovery Response
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSendP2PSDResponse(IN P_ADAPTER_T prAdapter,
			 IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	if (u4SetBufferLen)
		ASSERT(pvSetBuffer);

	if (u4SetBufferLen < sizeof(PARAM_P2P_SEND_SD_RESPONSE)) {
		*pu4SetInfoLen = sizeof(PARAM_P2P_SEND_SD_RESPONSE);
		return WLAN_STATUS_BUFFER_TOO_SHORT;
	}
/* rWlanStatus = p2pFsmRunEventSDResponse(prAdapter, (P_PARAM_P2P_SEND_SD_RESPONSE)pvSetBuffer); */

	return rWlanStatus;
}				/* end of wlanoidGetP2PSDRequest() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get GAS frame for P2P Service Discovery Request
*
* \param[in]  prAdapter        Pointer to the Adapter structure.
* \param[out] pvQueryBuffer    A pointer to the buffer that holds the result of
*                              the query.
* \param[in]  u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen  If the call is successful, returns the number of
*                              bytes written into the query buffer. If the call
*                              failed due to invalid length of the query buffer,
*                              returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidGetP2PSDRequest(IN P_ADAPTER_T prAdapter,
		       IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
	/*PUINT_8 pucPacketBuffer = NULL, pucTA = NULL;*/
/* PUINT_8 pucChannelNum = NULL; */
	/*PUINT_16 pu2PacketLength = NULL;*/
	/*P_WLAN_MAC_HEADER_T prWlanHdr = (P_WLAN_MAC_HEADER_T) NULL;*/
	/*UINT_8 ucVersionNum = 0;*/
/* UINT_8 ucChannelNum = 0, ucSeqNum = 0; */

	ASSERT(prAdapter);
	ASSERT(pu4QueryInfoLen);

	if (u4QueryBufferLen)
		ASSERT(pvQueryBuffer);

	if (u4QueryBufferLen < sizeof(PARAM_P2P_GET_SD_REQUEST)) {
		*pu4QueryInfoLen = sizeof(PARAM_P2P_GET_SD_REQUEST);
		return WLAN_STATUS_BUFFER_TOO_SHORT;
	}

	DBGLOG(P2P, TRACE, "Get Service Discovery Request\n");
#if 0
	ucVersionNum = p2pFuncGetVersionNumOfSD(prAdapter);
	if (ucVersionNum == 0) {
		P_PARAM_P2P_GET_SD_REQUEST prP2pGetSdReq = (P_PARAM_P2P_GET_SD_REQUEST) pvQueryBuffer;

		pucPacketBuffer = prP2pGetSdReq->aucPacketContent;
		pu2PacketLength = &prP2pGetSdReq->u2PacketLength;
		pucTA = &prP2pGetSdReq->rTransmitterAddr;
	} else {
		P_PARAM_P2P_GET_SD_REQUEST_EX prP2pGetSdReqEx = (P_PARAM_P2P_GET_SD_REQUEST_EX) NULL;

		prP2pGetSdReqEx = (P_PARAM_P2P_GET_SD_REQUEST) pvQueryBuffer;
		pucPacketBuffer = prP2pGetSdReqEx->aucPacketContent;
		pu2PacketLength = &prP2pGetSdReqEx->u2PacketLength;
		pucTA = &prP2pGetSdReqEx->rTransmitterAddr;
		pucChannelNum = &prP2pGetSdReqEx->ucChannelNum;
		ucSeqNum = prP2pGetSdReqEx->ucSeqNum;
	}

	rWlanStatus = p2pFuncGetServiceDiscoveryFrame(prAdapter,
						      pucPacketBuffer,
						      (u4QueryBufferLen - sizeof(PARAM_P2P_GET_SD_REQUEST)),
						      (PUINT_32) pu2PacketLength, pucChannelNum, ucSeqNum);
#else
	*pu4QueryInfoLen = 0;
	return rWlanStatus;
#endif
	/*
	prWlanHdr = (P_WLAN_MAC_HEADER_T) pucPacketBuffer;

	kalMemCopy(pucTA, prWlanHdr->aucAddr2, MAC_ADDR_LEN);

	if (pu4QueryInfoLen) {
		if (ucVersionNum == 0)
			*pu4QueryInfoLen = (UINT_32) (sizeof(PARAM_P2P_GET_SD_REQUEST) + (*pu2PacketLength));
		else
			*pu4QueryInfoLen = (UINT_32) (sizeof(PARAM_P2P_GET_SD_REQUEST_EX) + (*pu2PacketLength));

	}

	return rWlanStatus;
	*/
}				/* end of wlanoidGetP2PSDRequest() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get GAS frame for P2P Service Discovery Response
*
* \param[in]  prAdapter        Pointer to the Adapter structure.
* \param[out] pvQueryBuffer    A pointer to the buffer that holds the result of
*                              the query.
* \param[in]  u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen  If the call is successful, returns the number of
*                              bytes written into the query buffer. If the call
*                              failed due to invalid length of the query buffer,
*                              returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidGetP2PSDResponse(IN P_ADAPTER_T prAdapter,
			IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
	/*P_WLAN_MAC_HEADER_T prWlanHdr = (P_WLAN_MAC_HEADER_T) NULL;*/
	/* UINT_8 ucSeqNum = 0, */
	/*UINT_8 ucVersionNum = 0;*/
	/*PUINT_8 pucPacketContent = (PUINT_8) NULL, pucTA = (PUINT_8) NULL;*/
	/*PUINT_16 pu2PacketLength = (PUINT_16) NULL;*/

	ASSERT(prAdapter);
	ASSERT(pu4QueryInfoLen);

	if (u4QueryBufferLen)
		ASSERT(pvQueryBuffer);

	if (u4QueryBufferLen < sizeof(PARAM_P2P_GET_SD_RESPONSE)) {
		*pu4QueryInfoLen = sizeof(PARAM_P2P_GET_SD_RESPONSE);
		return WLAN_STATUS_BUFFER_TOO_SHORT;
	}

	DBGLOG(P2P, TRACE, "Get Service Discovery Response\n");

#if 0
	ucVersionNum = p2pFuncGetVersionNumOfSD(prAdapter);
	if (ucVersionNum == 0) {
		P_PARAM_P2P_GET_SD_RESPONSE prP2pGetSdRsp = (P_PARAM_P2P_GET_SD_RESPONSE) NULL;

		prP2pGetSdRsp = (P_PARAM_P2P_GET_SD_REQUEST) pvQueryBuffer;
		pucPacketContent = prP2pGetSdRsp->aucPacketContent;
		pucTA = &prP2pGetSdRsp->rTransmitterAddr;
		pu2PacketLength = &prP2pGetSdRsp->u2PacketLength;
	} else {
		P_PARAM_P2P_GET_SD_RESPONSE_EX prP2pGetSdRspEx = (P_PARAM_P2P_GET_SD_RESPONSE_EX) NULL;

		prP2pGetSdRspEx = (P_PARAM_P2P_GET_SD_RESPONSE_EX) pvQueryBuffer;
		pucPacketContent = prP2pGetSdRspEx->aucPacketContent;
		pucTA = &prP2pGetSdRspEx->rTransmitterAddr;
		pu2PacketLength = &prP2pGetSdRspEx->u2PacketLength;
		ucSeqNum = prP2pGetSdRspEx->ucSeqNum;
	}

/* rWlanStatus = p2pFuncGetServiceDiscoveryFrame(prAdapter, */
/* pucPacketContent, */
/* (u4QueryBufferLen - sizeof(PARAM_P2P_GET_SD_RESPONSE)), */
/* (PUINT_32)pu2PacketLength, */
/* NULL, */
/* ucSeqNum); */
#else
	*pu4QueryInfoLen = 0;
	return rWlanStatus;
#endif
	/*
	prWlanHdr = (P_WLAN_MAC_HEADER_T) pucPacketContent;

	kalMemCopy(pucTA, prWlanHdr->aucAddr2, MAC_ADDR_LEN);

	if (pu4QueryInfoLen) {
		if (ucVersionNum == 0)
			*pu4QueryInfoLen = (UINT_32) (sizeof(PARAM_P2P_GET_SD_RESPONSE) + *pu2PacketLength);
		else
			*pu4QueryInfoLen = (UINT_32) (sizeof(PARAM_P2P_GET_SD_RESPONSE_EX) + *pu2PacketLength);
	}

	return rWlanStatus;
	*/
}				/* end of wlanoidGetP2PSDResponse() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to terminate P2P Service Discovery Phase
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2PTerminateSDPhase(IN P_ADAPTER_T prAdapter,
			      IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
	P_PARAM_P2P_TERMINATE_SD_PHASE prP2pTerminateSD = (P_PARAM_P2P_TERMINATE_SD_PHASE) NULL;
	UINT_8 aucNullAddr[] = NULL_MAC_ADDR;

	do {
		if ((prAdapter == NULL) || (pu4SetInfoLen == NULL))
			break;

		if ((u4SetBufferLen) && (pvSetBuffer == NULL))
			break;

		if (u4SetBufferLen < sizeof(PARAM_P2P_TERMINATE_SD_PHASE)) {
			*pu4SetInfoLen = sizeof(PARAM_P2P_TERMINATE_SD_PHASE);
			rWlanStatus = WLAN_STATUS_BUFFER_TOO_SHORT;
			break;
		}

		prP2pTerminateSD = (P_PARAM_P2P_TERMINATE_SD_PHASE) pvSetBuffer;

		if (EQUAL_MAC_ADDR(prP2pTerminateSD->rPeerAddr, aucNullAddr)) {
			DBGLOG(P2P, TRACE, "Service Discovery Version 2.0\n");
/* p2pFuncSetVersionNumOfSD(prAdapter, 2); */
		}
		/* rWlanStatus = p2pFsmRunEventSDAbort(prAdapter); */

	} while (FALSE);

	return rWlanStatus;
}				/* end of wlanoidSetP2PTerminateSDPhase() */

#if CFG_SUPPORT_ANTI_PIRACY
/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to
*
* \param[in] prAdapter      Pointer to the Adapter structure.
* \param[in] pvSetBuffer    Pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                           bytes read from the set buffer. If the call failed
*                           due to invalid length of the set buffer, returns
*                           the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetSecCheckRequest(IN P_ADAPTER_T prAdapter,
			  IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	if (u4SetBufferLen)
		ASSERT(pvSetBuffer);

	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_SEC_CHECK,
					 FALSE,
					 TRUE,
					 TRUE,
					 NULL,
					 nicOidCmdTimeoutCommon,
					 u4SetBufferLen, (PUINT_8) pvSetBuffer, pvSetBuffer, u4SetBufferLen);

}				/* end of wlanoidSetSecCheckRequest() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to
*
* \param[in]  prAdapter        Pointer to the Adapter structure.
* \param[out] pvQueryBuffer    A pointer to the buffer that holds the result of
*                              the query.
* \param[in]  u4QueryBufferLen The length of the query buffer.
* \param[out] pu4QueryInfoLen  If the call is successful, returns the number of
*                              bytes written into the query buffer. If the call
*                              failed due to invalid length of the query buffer,
*                              returns the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
* \retval WLAN_STATUS_ADAPTER_NOT_READY
* \retval WLAN_STATUS_MULTICAST_FULL
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidGetSecCheckResponse(IN P_ADAPTER_T prAdapter,
			   IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	WLAN_STATUS rWlanStatus = WLAN_STATUS_SUCCESS;
	/* P_WLAN_MAC_HEADER_T prWlanHdr = (P_WLAN_MAC_HEADER_T)NULL; */
	P_GLUE_INFO_T prGlueInfo;

	prGlueInfo = prAdapter->prGlueInfo;

	ASSERT(prAdapter);
	ASSERT(pu4QueryInfoLen);

	if (u4QueryBufferLen)
		ASSERT(pvQueryBuffer);

	if (u4QueryBufferLen > 256)
		u4QueryBufferLen = 256;

	*pu4QueryInfoLen = u4QueryBufferLen;

#if DBG
	DBGLOG_MEM8(SEC, LOUD, prGlueInfo->prP2PInfo->aucSecCheckRsp, u4QueryBufferLen);
#endif
	kalMemCopy((PUINT_8) (pvQueryBuffer + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer)),
		   prGlueInfo->prP2PInfo->aucSecCheckRsp, u4QueryBufferLen);

	return rWlanStatus;
}				/* end of wlanoidGetSecCheckResponse() */
#endif

WLAN_STATUS
wlanoidSetNoaParam(IN P_ADAPTER_T prAdapter,
		   IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	P_PARAM_CUSTOM_NOA_PARAM_STRUCT_T prNoaParam;
	CMD_CUSTOM_NOA_PARAM_STRUCT_T rCmdNoaParam;

	DEBUGFUNC("wlanoidSetNoaParam");
	DBGLOG(P2P, TRACE, "\n");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = sizeof(PARAM_CUSTOM_NOA_PARAM_STRUCT_T);

	if (u4SetBufferLen < sizeof(PARAM_CUSTOM_NOA_PARAM_STRUCT_T))
		return WLAN_STATUS_INVALID_LENGTH;

	ASSERT(pvSetBuffer);

	prNoaParam = (P_PARAM_CUSTOM_NOA_PARAM_STRUCT_T) pvSetBuffer;

	kalMemZero(&rCmdNoaParam, sizeof(CMD_CUSTOM_NOA_PARAM_STRUCT_T));
	rCmdNoaParam.u4NoaDurationMs = prNoaParam->u4NoaDurationMs;
	rCmdNoaParam.u4NoaIntervalMs = prNoaParam->u4NoaIntervalMs;
	rCmdNoaParam.u4NoaCount = prNoaParam->u4NoaCount;

#if 0
	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_SET_NOA_PARAM,
				   TRUE,
				   FALSE,
				   TRUE,
				   nicCmdEventSetCommon,
				   nicOidCmdTimeoutCommon,
				   sizeof(CMD_CUSTOM_NOA_PARAM_STRUCT_T),
				   (PUINT_8) &rCmdNoaParam, pvSetBuffer, u4SetBufferLen);
#else
	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_SET_NOA_PARAM,
					 TRUE,
					 FALSE,
					 TRUE,
					 NULL,
					 nicOidCmdTimeoutCommon,
					 sizeof(CMD_CUSTOM_NOA_PARAM_STRUCT_T),
					 (PUINT_8) &rCmdNoaParam, pvSetBuffer, u4SetBufferLen);

#endif

}

WLAN_STATUS
wlanoidSetOppPsParam(IN P_ADAPTER_T prAdapter,
		     IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	P_PARAM_CUSTOM_OPPPS_PARAM_STRUCT_T prOppPsParam;
	CMD_CUSTOM_OPPPS_PARAM_STRUCT_T rCmdOppPsParam;

	DEBUGFUNC("wlanoidSetOppPsParam");
	DBGLOG(P2P, TRACE, "\n");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = sizeof(PARAM_CUSTOM_OPPPS_PARAM_STRUCT_T);

	if (u4SetBufferLen < sizeof(PARAM_CUSTOM_OPPPS_PARAM_STRUCT_T))
		return WLAN_STATUS_INVALID_LENGTH;

	ASSERT(pvSetBuffer);

	prOppPsParam = (P_PARAM_CUSTOM_OPPPS_PARAM_STRUCT_T) pvSetBuffer;

	kalMemZero(&rCmdOppPsParam, sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T));
	rCmdOppPsParam.u4CTwindowMs = prOppPsParam->u4CTwindowMs;

#if 0
	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_SET_OPPPS_PARAM,
				   TRUE,
				   FALSE,
				   TRUE,
				   nicCmdEventSetCommon,
				   nicOidCmdTimeoutCommon,
				   sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T),
				   (PUINT_8) &rCmdOppPsParam, pvSetBuffer, u4SetBufferLen);
#else
	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_SET_NOA_PARAM,
					 TRUE,
					 FALSE,
					 TRUE,
					 NULL,
					 nicOidCmdTimeoutCommon,
					 sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T),
					 (PUINT_8) &rCmdOppPsParam, pvSetBuffer, u4SetBufferLen);

#endif

}

WLAN_STATUS
wlanoidSetUApsdParam(IN P_ADAPTER_T prAdapter,
		     IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	P_PARAM_CUSTOM_UAPSD_PARAM_STRUCT_T prUapsdParam;
	CMD_CUSTOM_UAPSD_PARAM_STRUCT_T rCmdUapsdParam;
	P_PM_PROFILE_SETUP_INFO_T prPmProfSetupInfo;
	P_BSS_INFO_T prBssInfo;

	DEBUGFUNC("wlanoidSetUApsdParam");
	DBGLOG(P2P, TRACE, "\n");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	*pu4SetInfoLen = sizeof(PARAM_CUSTOM_UAPSD_PARAM_STRUCT_T);

	if (u4SetBufferLen < sizeof(PARAM_CUSTOM_UAPSD_PARAM_STRUCT_T))
		return WLAN_STATUS_INVALID_LENGTH;

	ASSERT(pvSetBuffer);

	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
	prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;

	prUapsdParam = (P_PARAM_CUSTOM_UAPSD_PARAM_STRUCT_T) pvSetBuffer;

	kalMemZero(&rCmdUapsdParam, sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T));
	rCmdUapsdParam.fgEnAPSD = prUapsdParam->fgEnAPSD;
	prAdapter->rWifiVar.fgSupportUAPSD = prUapsdParam->fgEnAPSD;

	rCmdUapsdParam.fgEnAPSD_AcBe = prUapsdParam->fgEnAPSD_AcBe;
	rCmdUapsdParam.fgEnAPSD_AcBk = prUapsdParam->fgEnAPSD_AcBk;
	rCmdUapsdParam.fgEnAPSD_AcVo = prUapsdParam->fgEnAPSD_AcVo;
	rCmdUapsdParam.fgEnAPSD_AcVi = prUapsdParam->fgEnAPSD_AcVi;
	prPmProfSetupInfo->ucBmpDeliveryAC =
	    ((prUapsdParam->fgEnAPSD_AcBe << 0) |
	     (prUapsdParam->fgEnAPSD_AcBk << 1) |
	     (prUapsdParam->fgEnAPSD_AcVi << 2) | (prUapsdParam->fgEnAPSD_AcVo << 3));
	prPmProfSetupInfo->ucBmpTriggerAC =
	    ((prUapsdParam->fgEnAPSD_AcBe << 0) |
	     (prUapsdParam->fgEnAPSD_AcBk << 1) |
	     (prUapsdParam->fgEnAPSD_AcVi << 2) | (prUapsdParam->fgEnAPSD_AcVo << 3));

	rCmdUapsdParam.ucMaxSpLen = prUapsdParam->ucMaxSpLen;
	prPmProfSetupInfo->ucUapsdSp = prUapsdParam->ucMaxSpLen;

#if 0
	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_SET_UAPSD_PARAM,
				   TRUE,
				   FALSE,
				   TRUE,
				   nicCmdEventSetCommon,
				   nicOidCmdTimeoutCommon,
				   sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T),
				   (PUINT_8) &rCmdUapsdParam, pvSetBuffer, u4SetBufferLen);
#else
	return wlanoidSendSetQueryP2PCmd(prAdapter,
					 CMD_ID_SET_UAPSD_PARAM,
					 TRUE,
					 FALSE,
					 TRUE,
					 NULL,
					 nicOidCmdTimeoutCommon,
					 sizeof(CMD_CUSTOM_OPPPS_PARAM_STRUCT_T),
					 (PUINT_8) &rCmdUapsdParam, pvSetBuffer, u4SetBufferLen);

#endif
}

WLAN_STATUS
wlanoidQueryP2pOpChannel(IN P_ADAPTER_T prAdapter,
			 IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{

	WLAN_STATUS rResult = WLAN_STATUS_FAILURE;
/* PUINT_8 pucOpChnl = (PUINT_8)pvQueryBuffer; */

	do {
		if ((prAdapter == NULL) || (pu4QueryInfoLen == NULL))
			break;

		if ((u4QueryBufferLen) && (pvQueryBuffer == NULL))
			break;

		if (u4QueryBufferLen < sizeof(UINT_8)) {
			*pu4QueryInfoLen = sizeof(UINT_8);
			rResult = WLAN_STATUS_BUFFER_TOO_SHORT;
			break;
		}
#if 0
		if (!p2pFuncGetCurrentOpChnl(prAdapter, pucOpChnl)) {
			rResult = WLAN_STATUS_INVALID_DATA;
			break;
		}
#else
		rResult = WLAN_STATUS_INVALID_DATA;
		break;
#endif
		/*
		*pu4QueryInfoLen = sizeof(UINT_8);
		rResult = WLAN_STATUS_SUCCESS;
		*/

	} while (FALSE);

	return rResult;
}				/* wlanoidQueryP2pOpChannel */

WLAN_STATUS
wlanoidQueryP2pVersion(IN P_ADAPTER_T prAdapter,
		       IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	WLAN_STATUS rResult = WLAN_STATUS_FAILURE;
/* PUINT_8 pucVersionNum = (PUINT_8)pvQueryBuffer; */

	do {
		if ((prAdapter == NULL) || (pu4QueryInfoLen == NULL))
			break;

		if ((u4QueryBufferLen) && (pvQueryBuffer == NULL))
			break;

		if (u4QueryBufferLen < sizeof(UINT_8)) {
			*pu4QueryInfoLen = sizeof(UINT_8);
			rResult = WLAN_STATUS_BUFFER_TOO_SHORT;
			break;
		}

	} while (FALSE);

	return rResult;
}				/* wlanoidQueryP2pVersion */

WLAN_STATUS
wlanoidSetP2pSupplicantVersion(IN P_ADAPTER_T prAdapter,
			       IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS rResult = WLAN_STATUS_FAILURE;
	UINT_8 ucVersionNum;

	do {
		if ((prAdapter == NULL) || (pu4SetInfoLen == NULL)) {

			rResult = WLAN_STATUS_INVALID_DATA;
			break;
		}

		if ((u4SetBufferLen) && (pvSetBuffer == NULL)) {
			rResult = WLAN_STATUS_INVALID_DATA;
			break;
		}

		*pu4SetInfoLen = sizeof(UINT_8);

		if (u4SetBufferLen < sizeof(UINT_8)) {
			rResult = WLAN_STATUS_INVALID_LENGTH;
			break;
		}

		ucVersionNum = *((PUINT_8) pvSetBuffer);

		rResult = WLAN_STATUS_SUCCESS;
	} while (FALSE);

	return rResult;
}				/* wlanoidSetP2pSupplicantVersion */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set the WPS mode.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
* \param[in] pvSetBuffer A pointer to the buffer that holds the data to be set.
* \param[in] u4SetBufferLen The length of the set buffer.
* \param[out] pu4SetInfoLen If the call is successful, returns the number of
*                          bytes read from the set buffer. If the call failed
*                          due to invalid length of the set buffer, returns
*                          the amount of storage needed.
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanoidSetP2pWPSmode(IN P_ADAPTER_T prAdapter,
		     IN PVOID pvSetBuffer, IN UINT_32 u4SetBufferLen, OUT PUINT_32 pu4SetInfoLen)
{
	WLAN_STATUS status;
	UINT_32 u4IsWPSmode = 0;

	DEBUGFUNC("wlanoidSetP2pWPSmode");

	ASSERT(prAdapter);
	ASSERT(pu4SetInfoLen);

	if (pvSetBuffer)
		u4IsWPSmode = *(PUINT_32) pvSetBuffer;
	else
		u4IsWPSmode = 0;

	if (u4IsWPSmode)
		prAdapter->rWifiVar.prP2pFsmInfo->fgIsWPSMode = 1;
	else
		prAdapter->rWifiVar.prP2pFsmInfo->fgIsWPSMode = 0;

	status = nicUpdateBss(prAdapter, NETWORK_TYPE_P2P_INDEX);

	return status;
}				/* end of wlanoidSetP2pWPSmode() */

#if CFG_SUPPORT_P2P_RSSI_QUERY
WLAN_STATUS
wlanoidQueryP2pRssi(IN P_ADAPTER_T prAdapter,
		    IN PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen, OUT PUINT_32 pu4QueryInfoLen)
{
	DEBUGFUNC("wlanoidQueryP2pRssi");

	ASSERT(prAdapter);
	ASSERT(pu4QueryInfoLen);
	if (u4QueryBufferLen)
		ASSERT(pvQueryBuffer);

	*pu4QueryInfoLen = sizeof(PARAM_RSSI);

	/* Check for query buffer length */
	if (u4QueryBufferLen < *pu4QueryInfoLen) {
		DBGLOG(REQ, WARN, "Too short length %u\n", u4QueryBufferLen);
		return WLAN_STATUS_BUFFER_TOO_SHORT;
	}

	if (prAdapter->fgIsP2pLinkQualityValid == TRUE &&
	    (kalGetTimeTick() - prAdapter->rP2pLinkQualityUpdateTime) <= CFG_LINK_QUALITY_VALID_PERIOD) {
		PARAM_RSSI rRssi;

		rRssi = (PARAM_RSSI) prAdapter->rP2pLinkQuality.cRssi;	/* ranged from (-128 ~ 30) in unit of dBm */

		if (rRssi > PARAM_WHQL_RSSI_MAX_DBM)
			rRssi = PARAM_WHQL_RSSI_MAX_DBM;
		else if (rRssi < PARAM_WHQL_RSSI_MIN_DBM)
			rRssi = PARAM_WHQL_RSSI_MIN_DBM;

		kalMemCopy(pvQueryBuffer, &rRssi, sizeof(PARAM_RSSI));
		return WLAN_STATUS_SUCCESS;
	}
#ifdef LINUX
	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_GET_LINK_QUALITY,
				   FALSE,
				   TRUE,
				   TRUE,
				   nicCmdEventQueryLinkQuality,
				   nicOidCmdTimeoutCommon,
				   *pu4QueryInfoLen, pvQueryBuffer, pvQueryBuffer, u4QueryBufferLen);
#else
	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_GET_LINK_QUALITY,
				   FALSE,
				   TRUE,
				   TRUE,
				   nicCmdEventQueryLinkQuality,
				   nicOidCmdTimeoutCommon, 0, NULL, pvQueryBuffer, u4QueryBufferLen);

#endif
}				/* wlanoidQueryP2pRssi */
#endif
