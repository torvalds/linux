/************************************************************
 * CMHOST.C
 * This file contains the routines for handling Connection
 * Management.
 ************************************************************/

/* #define CONN_MSG */
#include "headers.h"

enum E_CLASSIFIER_ACTION {
	eInvalidClassifierAction,
	eAddClassifier,
	eReplaceClassifier,
	eDeleteClassifier
};

static ULONG GetNextTargetBufferLocation(struct bcm_mini_adapter *Adapter, B_UINT16 tid);

/************************************************************
 * Function - SearchSfid
 *
 * Description - This routinue would search QOS queues having
 *  specified SFID as input parameter.
 *
 * Parameters -	Adapter: Pointer to the Adapter structure
 *  uiSfid : Given SFID for matching
 *
 * Returns - Queue index for this SFID(If matched)
 *  Else Invalid Queue Index(If Not matched)
 ************************************************************/
int SearchSfid(struct bcm_mini_adapter *Adapter, UINT uiSfid)
{
	int i;

	for (i = (NO_OF_QUEUES-1); i >= 0; i--)
		if (Adapter->PackInfo[i].ulSFID == uiSfid)
			return i;

	return NO_OF_QUEUES+1;
}

/***************************************************************
 * Function -SearchFreeSfid
 *
 * Description - This routinue would search Free available SFID.
 *
 * Parameter - Adapter: Pointer to the Adapter structure
 *
 * Returns - Queue index for the free SFID
 *  Else returns Invalid Index.
 ****************************************************************/
static int SearchFreeSfid(struct bcm_mini_adapter *Adapter)
{
	int i;

	for (i = 0; i < (NO_OF_QUEUES-1); i++)
		if (Adapter->PackInfo[i].ulSFID == 0)
			return i;

	return NO_OF_QUEUES+1;
}

/*
 * Function: SearchClsid
 * Description:	This routinue would search Classifier  having specified ClassifierID as input parameter
 * Input parameters: struct bcm_mini_adapter *Adapter - Adapter Context
 *  unsigned int uiSfid   - The SF in which the classifier is to searched
 *  B_UINT16  uiClassifierID - The classifier ID to be searched
 * Return: int :Classifier table index of matching entry
 */
static int SearchClsid(struct bcm_mini_adapter *Adapter, ULONG ulSFID, B_UINT16  uiClassifierID)
{
	int i;

	for (i = 0; i < MAX_CLASSIFIERS; i++) {
		if ((Adapter->astClassifierTable[i].bUsed) &&
			(Adapter->astClassifierTable[i].uiClassifierRuleIndex == uiClassifierID) &&
			(Adapter->astClassifierTable[i].ulSFID == ulSFID))
			return i;
	}

	return MAX_CLASSIFIERS+1;
}

/*
 * @ingroup ctrl_pkt_functions
 * This routinue would search Free available Classifier entry in classifier table.
 * @return free Classifier Entry index in classifier table for specified SF
 */
static int SearchFreeClsid(struct bcm_mini_adapter *Adapter /**Adapter Context*/)
{
	int i;

	for (i = 0; i < MAX_CLASSIFIERS; i++) {
		if (!Adapter->astClassifierTable[i].bUsed)
			return i;
	}

	return MAX_CLASSIFIERS+1;
}

static VOID deleteSFBySfid(struct bcm_mini_adapter *Adapter, UINT uiSearchRuleIndex)
{
	/* deleting all the packet held in the SF */
	flush_queue(Adapter, uiSearchRuleIndex);

	/* Deleting the all classifiers for this SF */
	DeleteAllClassifiersForSF(Adapter, uiSearchRuleIndex);

	/* Resetting only MIBS related entries in the SF */
	memset((PVOID)&Adapter->PackInfo[uiSearchRuleIndex], 0, sizeof(S_MIBS_SERVICEFLOW_TABLE));
}

static inline VOID
CopyIpAddrToClassifier(struct bcm_classifier_rule *pstClassifierEntry,
		B_UINT8 u8IpAddressLen, B_UINT8 *pu8IpAddressMaskSrc,
		BOOLEAN bIpVersion6, E_IPADDR_CONTEXT eIpAddrContext)
{
	int i = 0;
	UINT nSizeOfIPAddressInBytes = IP_LENGTH_OF_ADDRESS;
	UCHAR *ptrClassifierIpAddress = NULL;
	UCHAR *ptrClassifierIpMask = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	if (bIpVersion6)
		nSizeOfIPAddressInBytes = IPV6_ADDRESS_SIZEINBYTES;

	/* Destination Ip Address */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Ip Address Range Length:0x%X ", u8IpAddressLen);
	if ((bIpVersion6 ? (IPV6_ADDRESS_SIZEINBYTES * MAX_IP_RANGE_LENGTH * 2) :
			(TOTAL_MASKED_ADDRESS_IN_BYTES)) >= u8IpAddressLen) {
		/*
		 * checking both the mask and address togethor in Classification.
		 * So length will be : TotalLengthInBytes/nSizeOfIPAddressInBytes * 2
		 * (nSizeOfIPAddressInBytes for address and nSizeOfIPAddressInBytes for mask)
		 */
		if (eIpAddrContext == eDestIpAddress) {
			pstClassifierEntry->ucIPDestinationAddressLength = u8IpAddressLen/(nSizeOfIPAddressInBytes * 2);
			if (bIpVersion6) {
				ptrClassifierIpAddress = pstClassifierEntry->stDestIpAddress.ucIpv6Address;
				ptrClassifierIpMask = pstClassifierEntry->stDestIpAddress.ucIpv6Mask;
			} else {
				ptrClassifierIpAddress = pstClassifierEntry->stDestIpAddress.ucIpv4Address;
				ptrClassifierIpMask = pstClassifierEntry->stDestIpAddress.ucIpv4Mask;
			}
		} else if (eIpAddrContext == eSrcIpAddress) {
			pstClassifierEntry->ucIPSourceAddressLength = u8IpAddressLen/(nSizeOfIPAddressInBytes * 2);
			if (bIpVersion6) {
				ptrClassifierIpAddress = pstClassifierEntry->stSrcIpAddress.ucIpv6Address;
				ptrClassifierIpMask = pstClassifierEntry->stSrcIpAddress.ucIpv6Mask;
			} else {
				ptrClassifierIpAddress = pstClassifierEntry->stSrcIpAddress.ucIpv4Address;
				ptrClassifierIpMask = pstClassifierEntry->stSrcIpAddress.ucIpv4Mask;
			}
		}
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Address Length:0x%X\n", pstClassifierEntry->ucIPDestinationAddressLength);
		while ((u8IpAddressLen >= nSizeOfIPAddressInBytes) && (i < MAX_IP_RANGE_LENGTH)) {
			memcpy(ptrClassifierIpAddress +
				(i * nSizeOfIPAddressInBytes),
				(pu8IpAddressMaskSrc+(i*nSizeOfIPAddressInBytes*2)),
				nSizeOfIPAddressInBytes);

			if (!bIpVersion6) {
				if (eIpAddrContext == eSrcIpAddress) {
					pstClassifierEntry->stSrcIpAddress.ulIpv4Addr[i] = ntohl(pstClassifierEntry->stSrcIpAddress.ulIpv4Addr[i]);
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Src Ip Address:0x%luX ",
							pstClassifierEntry->stSrcIpAddress.ulIpv4Addr[i]);
				} else if (eIpAddrContext == eDestIpAddress) {
					pstClassifierEntry->stDestIpAddress.ulIpv4Addr[i] = ntohl(pstClassifierEntry->stDestIpAddress.ulIpv4Addr[i]);
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Dest Ip Address:0x%luX ",
							pstClassifierEntry->stDestIpAddress.ulIpv4Addr[i]);
				}
			}
			u8IpAddressLen -= nSizeOfIPAddressInBytes;
			if (u8IpAddressLen >= nSizeOfIPAddressInBytes) {
				memcpy(ptrClassifierIpMask +
					(i * nSizeOfIPAddressInBytes),
					(pu8IpAddressMaskSrc+nSizeOfIPAddressInBytes +
						(i*nSizeOfIPAddressInBytes*2)),
					nSizeOfIPAddressInBytes);

				if (!bIpVersion6) {
					if (eIpAddrContext == eSrcIpAddress) {
						pstClassifierEntry->stSrcIpAddress.ulIpv4Mask[i] =
							ntohl(pstClassifierEntry->stSrcIpAddress.ulIpv4Mask[i]);
						BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Src Ip Mask Address:0x%luX ",
								pstClassifierEntry->stSrcIpAddress.ulIpv4Mask[i]);
					} else if (eIpAddrContext == eDestIpAddress) {
						pstClassifierEntry->stDestIpAddress.ulIpv4Mask[i] =
							ntohl(pstClassifierEntry->stDestIpAddress.ulIpv4Mask[i]);
						BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Dest Ip Mask Address:0x%luX ",
								pstClassifierEntry->stDestIpAddress.ulIpv4Mask[i]);
					}
				}
				u8IpAddressLen -= nSizeOfIPAddressInBytes;
			}
			if (u8IpAddressLen == 0)
				pstClassifierEntry->bDestIpValid = TRUE;

			i++;
		}
		if (bIpVersion6) {
			/* Restore EndianNess of Struct */
			for (i = 0; i < MAX_IP_RANGE_LENGTH * 4; i++) {
				if (eIpAddrContext == eSrcIpAddress) {
					pstClassifierEntry->stSrcIpAddress.ulIpv6Addr[i] = ntohl(pstClassifierEntry->stSrcIpAddress.ulIpv6Addr[i]);
					pstClassifierEntry->stSrcIpAddress.ulIpv6Mask[i] = ntohl(pstClassifierEntry->stSrcIpAddress.ulIpv6Mask[i]);
				} else if (eIpAddrContext == eDestIpAddress) {
					pstClassifierEntry->stDestIpAddress.ulIpv6Addr[i] = ntohl(pstClassifierEntry->stDestIpAddress.ulIpv6Addr[i]);
					pstClassifierEntry->stDestIpAddress.ulIpv6Mask[i] = ntohl(pstClassifierEntry->stDestIpAddress.ulIpv6Mask[i]);
				}
			}
		}
	}
}

void ClearTargetDSXBuffer(struct bcm_mini_adapter *Adapter, B_UINT16 TID, BOOLEAN bFreeAll)
{
	int i;

	for (i = 0; i < Adapter->ulTotalTargetBuffersAvailable; i++) {
		if (Adapter->astTargetDsxBuffer[i].valid)
			continue;

		if ((bFreeAll) || (Adapter->astTargetDsxBuffer[i].tid == TID)) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "ClearTargetDSXBuffer: found tid %d buffer cleared %lx\n",
					TID, Adapter->astTargetDsxBuffer[i].ulTargetDsxBuffer);
			Adapter->astTargetDsxBuffer[i].valid = 1;
			Adapter->astTargetDsxBuffer[i].tid = 0;
			Adapter->ulFreeTargetBufferCnt++;
		}
	}
}

/*
 * @ingroup ctrl_pkt_functions
 * copy classifier rule into the specified SF index
 */
static inline VOID CopyClassifierRuleToSF(struct bcm_mini_adapter *Adapter, stConvergenceSLTypes  *psfCSType, UINT uiSearchRuleIndex, UINT nClassifierIndex)
{
	struct bcm_classifier_rule *pstClassifierEntry = NULL;
	/* VOID *pvPhsContext = NULL; */
	int i;
	/* UCHAR ucProtocolLength=0; */
	/* ULONG ulPhsStatus; */

	if (Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value == 0 ||
		nClassifierIndex > (MAX_CLASSIFIERS-1))
		return;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Storing Classifier Rule Index : %X",
			ntohs(psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex));

	if (nClassifierIndex > MAX_CLASSIFIERS-1)
		return;

	pstClassifierEntry = &Adapter->astClassifierTable[nClassifierIndex];
	if (pstClassifierEntry) {
		/* Store if Ipv6 */
		pstClassifierEntry->bIpv6Protocol = (Adapter->PackInfo[uiSearchRuleIndex].ucIpVersion == IPV6) ? TRUE : FALSE;

		/* Destinaiton Port */
		pstClassifierEntry->ucDestPortRangeLength = psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRangeLength / 4;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Destination Port Range Length:0x%X ", pstClassifierEntry->ucDestPortRangeLength);

		if (psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRangeLength <= MAX_PORT_RANGE) {
			for (i = 0; i < (pstClassifierEntry->ucDestPortRangeLength); i++) {
				pstClassifierEntry->usDestPortRangeLo[i] = *((PUSHORT)(psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange+i));
				pstClassifierEntry->usDestPortRangeHi[i] =
					*((PUSHORT)(psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange+2+i));
				pstClassifierEntry->usDestPortRangeLo[i] = ntohs(pstClassifierEntry->usDestPortRangeLo[i]);
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Destination Port Range Lo:0x%X ",
						pstClassifierEntry->usDestPortRangeLo[i]);
				pstClassifierEntry->usDestPortRangeHi[i] = ntohs(pstClassifierEntry->usDestPortRangeHi[i]);
			}
		} else {
			pstClassifierEntry->ucDestPortRangeLength = 0;
		}

		/* Source Port */
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Source Port Range Length:0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength);
		if (psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength <= MAX_PORT_RANGE) {
			pstClassifierEntry->ucSrcPortRangeLength = psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength/4;
			for (i = 0; i < (pstClassifierEntry->ucSrcPortRangeLength); i++) {
				pstClassifierEntry->usSrcPortRangeLo[i] =
					*((PUSHORT)(psfCSType->cCPacketClassificationRule.
							u8ProtocolSourcePortRange+i));
				pstClassifierEntry->usSrcPortRangeHi[i] =
					*((PUSHORT)(psfCSType->cCPacketClassificationRule.
							u8ProtocolSourcePortRange+2+i));
				pstClassifierEntry->usSrcPortRangeLo[i] =
					ntohs(pstClassifierEntry->usSrcPortRangeLo[i]);
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Source Port Range Lo:0x%X ",
						pstClassifierEntry->usSrcPortRangeLo[i]);
				pstClassifierEntry->usSrcPortRangeHi[i] = ntohs(pstClassifierEntry->usSrcPortRangeHi[i]);
			}
		}
		/* Destination Ip Address and Mask */
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Ip Destination Parameters : ");
		CopyIpAddrToClassifier(pstClassifierEntry,
				psfCSType->cCPacketClassificationRule.u8IPDestinationAddressLength,
				psfCSType->cCPacketClassificationRule.u8IPDestinationAddress,
				(Adapter->PackInfo[uiSearchRuleIndex].ucIpVersion == IPV6) ?
			TRUE : FALSE, eDestIpAddress);

		/* Source Ip Address and Mask */
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Ip Source Parameters : ");

		CopyIpAddrToClassifier(pstClassifierEntry,
				psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddressLength,
				psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddress,
				(Adapter->PackInfo[uiSearchRuleIndex].ucIpVersion == IPV6) ? TRUE : FALSE,
				eSrcIpAddress);

		/* TOS */
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "TOS Length:0x%X ", psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength);
		if (psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength == 3) {
			pstClassifierEntry->ucIPTypeOfServiceLength = psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength;
			pstClassifierEntry->ucTosLow = psfCSType->cCPacketClassificationRule.u8IPTypeOfService[0];
			pstClassifierEntry->ucTosHigh = psfCSType->cCPacketClassificationRule.u8IPTypeOfService[1];
			pstClassifierEntry->ucTosMask = psfCSType->cCPacketClassificationRule.u8IPTypeOfService[2];
			pstClassifierEntry->bTOSValid = TRUE;
		}
		if (psfCSType->cCPacketClassificationRule.u8Protocol == 0) {
			/* we didn't get protocol field filled in by the BS */
			pstClassifierEntry->ucProtocolLength = 0;
		} else {
			pstClassifierEntry->ucProtocolLength = 1; /* 1 valid protocol */
		}

		pstClassifierEntry->ucProtocol[0] = psfCSType->cCPacketClassificationRule.u8Protocol;
		pstClassifierEntry->u8ClassifierRulePriority = psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority;

		/* store the classifier rule ID and set this classifier entry as valid */
		pstClassifierEntry->ucDirection = Adapter->PackInfo[uiSearchRuleIndex].ucDirection;
		pstClassifierEntry->uiClassifierRuleIndex = ntohs(psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex);
		pstClassifierEntry->usVCID_Value = Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value;
		pstClassifierEntry->ulSFID = Adapter->PackInfo[uiSearchRuleIndex].ulSFID;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Search Index %d Dir: %d, Index: %d, Vcid: %d\n",
				uiSearchRuleIndex, pstClassifierEntry->ucDirection,
				pstClassifierEntry->uiClassifierRuleIndex,
				pstClassifierEntry->usVCID_Value);

		if (psfCSType->cCPacketClassificationRule.u8AssociatedPHSI)
			pstClassifierEntry->u8AssociatedPHSI = psfCSType->cCPacketClassificationRule.u8AssociatedPHSI;

		/* Copy ETH CS Parameters */
		pstClassifierEntry->ucEthCSSrcMACLen = (psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddressLength);
		memcpy(pstClassifierEntry->au8EThCSSrcMAC, psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress, MAC_ADDRESS_SIZE);
		memcpy(pstClassifierEntry->au8EThCSSrcMACMask, psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress + MAC_ADDRESS_SIZE, MAC_ADDRESS_SIZE);
		pstClassifierEntry->ucEthCSDestMACLen = (psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);
		memcpy(pstClassifierEntry->au8EThCSDestMAC, psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress, MAC_ADDRESS_SIZE);
		memcpy(pstClassifierEntry->au8EThCSDestMACMask, psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress + MAC_ADDRESS_SIZE, MAC_ADDRESS_SIZE);
		pstClassifierEntry->ucEtherTypeLen = (psfCSType->cCPacketClassificationRule.u8EthertypeLength);
		memcpy(pstClassifierEntry->au8EthCSEtherType, psfCSType->cCPacketClassificationRule.u8Ethertype, NUM_ETHERTYPE_BYTES);
		memcpy(pstClassifierEntry->usUserPriority, &psfCSType->cCPacketClassificationRule.u16UserPriority, 2);
		pstClassifierEntry->usVLANID = ntohs(psfCSType->cCPacketClassificationRule.u16VLANID);
		pstClassifierEntry->usValidityBitMap = ntohs(psfCSType->cCPacketClassificationRule.u16ValidityBitMap);

		pstClassifierEntry->bUsed = TRUE;
	}
}

/*
 * @ingroup ctrl_pkt_functions
 */
static inline VOID DeleteClassifierRuleFromSF(struct bcm_mini_adapter *Adapter, UINT uiSearchRuleIndex, UINT nClassifierIndex)
{
	struct bcm_classifier_rule *pstClassifierEntry = NULL;
	B_UINT16 u16PacketClassificationRuleIndex;
	USHORT usVCID;
	/* VOID *pvPhsContext = NULL; */
	/*ULONG ulPhsStatus; */

	usVCID = Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value;

	if (nClassifierIndex > MAX_CLASSIFIERS-1)
		return;

	if (usVCID == 0)
		return;

	u16PacketClassificationRuleIndex = Adapter->astClassifierTable[nClassifierIndex].uiClassifierRuleIndex;
	pstClassifierEntry = &Adapter->astClassifierTable[nClassifierIndex];
	if (pstClassifierEntry) {
		pstClassifierEntry->bUsed = FALSE;
		pstClassifierEntry->uiClassifierRuleIndex = 0;
		memset(pstClassifierEntry, 0, sizeof(struct bcm_classifier_rule));

		/* Delete the PHS Rule for this classifier */
		PhsDeleteClassifierRule(&Adapter->stBCMPhsContext, usVCID, u16PacketClassificationRuleIndex);
	}
}

/*
 * @ingroup ctrl_pkt_functions
 */
VOID DeleteAllClassifiersForSF(struct bcm_mini_adapter *Adapter, UINT uiSearchRuleIndex)
{
	struct bcm_classifier_rule *pstClassifierEntry = NULL;
	int i;
	/* B_UINT16  u16PacketClassificationRuleIndex; */
	USHORT ulVCID;
	/* VOID *pvPhsContext = NULL; */
	/* ULONG ulPhsStatus; */

	ulVCID = Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value;

	if (ulVCID == 0)
		return;

	for (i = 0; i < MAX_CLASSIFIERS; i++) {
		if (Adapter->astClassifierTable[i].usVCID_Value == ulVCID) {
			pstClassifierEntry = &Adapter->astClassifierTable[i];

			if (pstClassifierEntry->bUsed)
				DeleteClassifierRuleFromSF(Adapter, uiSearchRuleIndex, i);
		}
	}

	/* Delete All Phs Rules Associated with this SF */
	PhsDeleteSFRules(&Adapter->stBCMPhsContext, ulVCID);
}

/*
 * This routinue  copies the Connection Management
 * related data into the Adapter structure.
 * @ingroup ctrl_pkt_functions
 */
static VOID CopyToAdapter(register struct bcm_mini_adapter *Adapter, /* <Pointer to the Adapter structure */
			register pstServiceFlowParamSI psfLocalSet, /* <Pointer to the ServiceFlowParamSI structure */
			register UINT uiSearchRuleIndex, /* <Index of Queue, to which this data belongs */
			register UCHAR ucDsxType,
			stLocalSFAddIndicationAlt *pstAddIndication) {

	/* UCHAR ucProtocolLength = 0; */
	ULONG ulSFID;
	UINT nClassifierIndex = 0;
	enum E_CLASSIFIER_ACTION eClassifierAction = eInvalidClassifierAction;
	B_UINT16 u16PacketClassificationRuleIndex = 0;
	int i;
	stConvergenceSLTypes *psfCSType = NULL;
	S_PHS_RULE sPhsRule;
	USHORT uVCID = Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value;
	UINT UGIValue = 0;

	Adapter->PackInfo[uiSearchRuleIndex].bValid = TRUE;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Search Rule Index = %d\n", uiSearchRuleIndex);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "%s: SFID= %x ", __func__, ntohl(psfLocalSet->u32SFID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Updating Queue %d", uiSearchRuleIndex);

	ulSFID = ntohl(psfLocalSet->u32SFID);
	/* Store IP Version used */
	/* Get The Version Of IP used (IPv6 or IPv4) from CSSpecification field of SF */

	Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = 0;
	Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport = 0;

	/* Enable IP/ETh CS Support As Required */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "CopyToAdapter : u8CSSpecification : %X\n", psfLocalSet->u8CSSpecification);
	switch (psfLocalSet->u8CSSpecification) {
	case eCSPacketIPV4:
	{
		Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = IPV4_CS;
		break;
	}
	case eCSPacketIPV6:
	{
		Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = IPV6_CS;
		break;
	}
	case eCS802_3PacketEthernet:
	case eCS802_1QPacketVLAN:
	{
		Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport = ETH_CS_802_3;
		break;
	}
	case eCSPacketIPV4Over802_1QVLAN:
	case eCSPacketIPV4Over802_3Ethernet:
	{
		Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = IPV4_CS;
		Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport = ETH_CS_802_3;
		break;
	}
	case eCSPacketIPV6Over802_1QVLAN:
	case eCSPacketIPV6Over802_3Ethernet:
	{
		Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = IPV6_CS;
		Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport = ETH_CS_802_3;
		break;
	}
	default:
	{
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Error in value of CS Classification.. setting default to IP CS\n");
		Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport = IPV4_CS;
		break;
	}
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "CopyToAdapter : Queue No : %X ETH CS Support :  %X  , IP CS Support : %X\n",
			uiSearchRuleIndex,
			Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport,
			Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport);

	/* Store IP Version used */
	/* Get The Version Of IP used (IPv6 or IPv4) from CSSpecification field of SF */
	if (Adapter->PackInfo[uiSearchRuleIndex].bIPCSSupport == IPV6_CS)
		Adapter->PackInfo[uiSearchRuleIndex].ucIpVersion = IPV6;
	else
		Adapter->PackInfo[uiSearchRuleIndex].ucIpVersion = IPV4;

	/* To ensure that the ETH CS code doesn't gets executed if the BS doesn't supports ETH CS */
	if (!Adapter->bETHCSEnabled)
		Adapter->PackInfo[uiSearchRuleIndex].bEthCSSupport = 0;

	if (psfLocalSet->u8ServiceClassNameLength > 0 && psfLocalSet->u8ServiceClassNameLength < 32)
		memcpy(Adapter->PackInfo[uiSearchRuleIndex].ucServiceClassName,	psfLocalSet->u8ServiceClassName, psfLocalSet->u8ServiceClassNameLength);

	Adapter->PackInfo[uiSearchRuleIndex].u8QueueType = psfLocalSet->u8ServiceFlowSchedulingType;

	if (Adapter->PackInfo[uiSearchRuleIndex].u8QueueType == BE && Adapter->PackInfo[uiSearchRuleIndex].ucDirection)
		Adapter->usBestEffortQueueIndex = uiSearchRuleIndex;

	Adapter->PackInfo[uiSearchRuleIndex].ulSFID = ntohl(psfLocalSet->u32SFID);

	Adapter->PackInfo[uiSearchRuleIndex].u8TrafficPriority = psfLocalSet->u8TrafficPriority;

	/* copy all the classifier in the Service Flow param  structure */
	for (i = 0; i < psfLocalSet->u8TotalClassifiers; i++) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Classifier index =%d", i);
		psfCSType = &psfLocalSet->cConvergenceSLTypes[i];
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Classifier index =%d", i);

		if (psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority)
			Adapter->PackInfo[uiSearchRuleIndex].bClassifierPriority = TRUE;

		if (psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority)
			Adapter->PackInfo[uiSearchRuleIndex].bClassifierPriority = TRUE;

		if (ucDsxType == DSA_ACK) {
			eClassifierAction = eAddClassifier;
		} else if (ucDsxType == DSC_ACK) {
			switch (psfCSType->u8ClassfierDSCAction) {
			case 0: /* DSC Add Classifier */
			{
				eClassifierAction = eAddClassifier;
			}
			break;
			case 1: /* DSC Replace Classifier */
			{
				eClassifierAction = eReplaceClassifier;
			}
			break;
			case 2: /* DSC Delete Classifier */
			{
				eClassifierAction = eDeleteClassifier;
			}
			break;
			default:
			{
				eClassifierAction = eInvalidClassifierAction;
			}
			}
		}

		u16PacketClassificationRuleIndex = ntohs(psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex);

		switch (eClassifierAction) {
		case eAddClassifier:
		{
			/* Get a Free Classifier Index From Classifier table for this SF to add the Classifier */
			/* Contained in this message */
			nClassifierIndex = SearchClsid(Adapter, ulSFID, u16PacketClassificationRuleIndex);

			if (nClassifierIndex > MAX_CLASSIFIERS) {
				nClassifierIndex = SearchFreeClsid(Adapter);
				if (nClassifierIndex > MAX_CLASSIFIERS) {
					/* Failed To get a free Entry */
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Error Failed To get a free Classifier Entry");
					break;
				}
				/* Copy the Classifier Rule for this service flow into our Classifier table maintained per SF. */
				CopyClassifierRuleToSF(Adapter, psfCSType, uiSearchRuleIndex, nClassifierIndex);
			} else {
				/* This Classifier Already Exists and it is invalid to Add Classifier with existing PCRI */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,
						"CopyToAdapter: Error The Specified Classifier Already Exists and attempted To Add Classifier with Same PCRI : 0x%x\n",
						u16PacketClassificationRuleIndex);
			}
		}
		break;
		case eReplaceClassifier:
		{
			/* Get the Classifier Index From Classifier table for this SF and replace existing  Classifier */
			/* with the new classifier Contained in this message */
			nClassifierIndex = SearchClsid(Adapter, ulSFID, u16PacketClassificationRuleIndex);
			if (nClassifierIndex > MAX_CLASSIFIERS) {
				/* Failed To search the classifier */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Error Search for Classifier To be replaced failed");
				break;
			}
			/* Copy the Classifier Rule for this service flow into our Classifier table maintained per SF. */
			CopyClassifierRuleToSF(Adapter, psfCSType, uiSearchRuleIndex, nClassifierIndex);
		}
		break;
		case eDeleteClassifier:
		{
			/* Get the Classifier Index From Classifier table for this SF and replace existing  Classifier */
			/* with the new classifier Contained in this message */
			nClassifierIndex = SearchClsid(Adapter, ulSFID, u16PacketClassificationRuleIndex);
			if (nClassifierIndex > MAX_CLASSIFIERS)	{
				/* Failed To search the classifier */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Error Search for Classifier To be deleted failed");
				break;
			}

			/* Delete This classifier */
			DeleteClassifierRuleFromSF(Adapter, uiSearchRuleIndex, nClassifierIndex);
		}
		break;
		default:
		{
			/* Invalid Action for classifier */
			break;
		}
		}
	}

	/* Repeat parsing Classification Entries to process PHS Rules */
	for (i = 0; i < psfLocalSet->u8TotalClassifiers; i++) {
		psfCSType = &psfLocalSet->cConvergenceSLTypes[i];
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "psfCSType->u8PhsDSCAction : 0x%x\n", psfCSType->u8PhsDSCAction);

		switch (psfCSType->u8PhsDSCAction) {
		case eDeleteAllPHSRules:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Deleting All PHS Rules For VCID: 0x%X\n", uVCID);

			/* Delete All the PHS rules for this Service flow */
			PhsDeleteSFRules(&Adapter->stBCMPhsContext, uVCID);
			break;
		}
		case eDeletePHSRule:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "PHS DSC Action = Delete PHS Rule\n");

			if (psfCSType->cPhsRule.u8PHSI)
				PhsDeletePHSRule(&Adapter->stBCMPhsContext, uVCID, psfCSType->cCPacketClassificationRule.u8AssociatedPHSI);

			break;
		}
		default:
		{
			if (ucDsxType == DSC_ACK) {
				/* BCM_DEBUG_PRINT(CONN_MSG,("Invalid PHS DSC Action For DSC\n",psfCSType->cPhsRule.u8PHSI)); */
				break; /* FOr DSC ACK Case PHS DSC Action must be in valid set */
			}
		}
		/* Proceed To Add PHS rule for DSA_ACK case even if PHS DSC action is unspecified */
		/* No Break Here . Intentionally! */

		case eAddPHSRule:
		case eSetPHSRule:
		{
			if (psfCSType->cPhsRule.u8PHSI)	{
				/* Apply This PHS Rule to all classifiers whose Associated PHSI Match */
				unsigned int uiClassifierIndex = 0;
				if (pstAddIndication->u8Direction == UPLINK_DIR) {
					for (uiClassifierIndex = 0; uiClassifierIndex < MAX_CLASSIFIERS; uiClassifierIndex++) {
						if ((Adapter->astClassifierTable[uiClassifierIndex].bUsed) &&
							(Adapter->astClassifierTable[uiClassifierIndex].ulSFID == Adapter->PackInfo[uiSearchRuleIndex].ulSFID) &&
							(Adapter->astClassifierTable[uiClassifierIndex].u8AssociatedPHSI == psfCSType->cPhsRule.u8PHSI)) {
							BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,
									"Adding PHS Rule For Classifier: 0x%x cPhsRule.u8PHSI: 0x%x\n",
									Adapter->astClassifierTable[uiClassifierIndex].uiClassifierRuleIndex,
									psfCSType->cPhsRule.u8PHSI);
							/* Update The PHS Rule for this classifier as Associated PHSI id defined */

							/* Copy the PHS Rule */
							sPhsRule.u8PHSI = psfCSType->cPhsRule.u8PHSI;
							sPhsRule.u8PHSFLength = psfCSType->cPhsRule.u8PHSFLength;
							sPhsRule.u8PHSMLength = psfCSType->cPhsRule.u8PHSMLength;
							sPhsRule.u8PHSS = psfCSType->cPhsRule.u8PHSS;
							sPhsRule.u8PHSV = psfCSType->cPhsRule.u8PHSV;
							memcpy(sPhsRule.u8PHSF, psfCSType->cPhsRule.u8PHSF, MAX_PHS_LENGTHS);
							memcpy(sPhsRule.u8PHSM, psfCSType->cPhsRule.u8PHSM, MAX_PHS_LENGTHS);
							sPhsRule.u8RefCnt = 0;
							sPhsRule.bUnclassifiedPHSRule = FALSE;
							sPhsRule.PHSModifiedBytes = 0;
							sPhsRule.PHSModifiedNumPackets = 0;
							sPhsRule.PHSErrorNumPackets = 0;

							/* bPHSRuleAssociated = TRUE; */
							/* Store The PHS Rule for this classifier */

							PhsUpdateClassifierRule(
								&Adapter->stBCMPhsContext,
								uVCID,
								Adapter->astClassifierTable[uiClassifierIndex].uiClassifierRuleIndex,
								&sPhsRule,
								Adapter->astClassifierTable[uiClassifierIndex].u8AssociatedPHSI);

							/* Update PHS Rule For the Classifier */
							if (sPhsRule.u8PHSI) {
								Adapter->astClassifierTable[uiClassifierIndex].u32PHSRuleID = sPhsRule.u8PHSI;
								memcpy(&Adapter->astClassifierTable[uiClassifierIndex].sPhsRule, &sPhsRule, sizeof(S_PHS_RULE));
							}
						}
					}
				} else {
					/* Error PHS Rule specified in signaling could not be applied to any classifier */

					/* Copy the PHS Rule */
					sPhsRule.u8PHSI = psfCSType->cPhsRule.u8PHSI;
					sPhsRule.u8PHSFLength = psfCSType->cPhsRule.u8PHSFLength;
					sPhsRule.u8PHSMLength = psfCSType->cPhsRule.u8PHSMLength;
					sPhsRule.u8PHSS = psfCSType->cPhsRule.u8PHSS;
					sPhsRule.u8PHSV = psfCSType->cPhsRule.u8PHSV;
					memcpy(sPhsRule.u8PHSF, psfCSType->cPhsRule.u8PHSF, MAX_PHS_LENGTHS);
					memcpy(sPhsRule.u8PHSM, psfCSType->cPhsRule.u8PHSM, MAX_PHS_LENGTHS);
					sPhsRule.u8RefCnt = 0;
					sPhsRule.bUnclassifiedPHSRule = TRUE;
					sPhsRule.PHSModifiedBytes = 0;
					sPhsRule.PHSModifiedNumPackets = 0;
					sPhsRule.PHSErrorNumPackets = 0;
					/* Store The PHS Rule for this classifier */

					/*
					 * Passing the argument u8PHSI instead of clsid. Because for DL with no classifier rule,
					 * clsid will be zero hence we can't have multiple PHS rules for the same SF.
					 * To support multiple PHS rule, passing u8PHSI.
					 */
					PhsUpdateClassifierRule(
						&Adapter->stBCMPhsContext,
						uVCID,
						sPhsRule.u8PHSI,
						&sPhsRule,
						sPhsRule.u8PHSI);
				}
			}
		}
		break;
		}
	}

	if (psfLocalSet->u32MaxSustainedTrafficRate == 0) {
		/* No Rate Limit . Set Max Sustained Traffic Rate to Maximum */
		Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate = WIMAX_MAX_ALLOWED_RATE;
	} else if (ntohl(psfLocalSet->u32MaxSustainedTrafficRate) > WIMAX_MAX_ALLOWED_RATE) {
		/* Too large Allowed Rate specified. Limiting to Wi Max  Allowed rate */
		Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate = WIMAX_MAX_ALLOWED_RATE;
	} else {
		Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate =  ntohl(psfLocalSet->u32MaxSustainedTrafficRate);
	}

	Adapter->PackInfo[uiSearchRuleIndex].uiMaxLatency = ntohl(psfLocalSet->u32MaximumLatency);
	if (Adapter->PackInfo[uiSearchRuleIndex].uiMaxLatency == 0) /* 0 should be treated as infinite */
		Adapter->PackInfo[uiSearchRuleIndex].uiMaxLatency = MAX_LATENCY_ALLOWED;

	if ((Adapter->PackInfo[uiSearchRuleIndex].u8QueueType == ERTPS ||
			Adapter->PackInfo[uiSearchRuleIndex].u8QueueType == UGS))
		UGIValue = ntohs(psfLocalSet->u16UnsolicitedGrantInterval);

	if (UGIValue == 0)
		UGIValue = DEFAULT_UG_INTERVAL;

	/*
	 * For UGI based connections...
	 * DEFAULT_UGI_FACTOR*UGIInterval worth of data is the max token count at host...
	 * The extra amount of token is to ensure that a large amount of jitter won't have loss in throughput...
	 * In case of non-UGI based connection, 200 frames worth of data is the max token count at host...
	 */
	Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize =
		(DEFAULT_UGI_FACTOR*Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate*UGIValue)/1000;

	if (Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize < WIMAX_MAX_MTU*8) {
		UINT UGIFactor = 0;
		/* Special Handling to ensure the biggest size of packet can go out from host to FW as follows:
		 * 1. Any packet from Host to FW can go out in different packet size.
		 * 2. So in case the Bucket count is smaller than MTU, the packets of size (Size > TokenCount), will get dropped.
		 * 3. We can allow packets of MaxSize from Host->FW that can go out from FW in multiple SDUs by fragmentation at Wimax Layer
		 */
		UGIFactor = (Adapter->PackInfo[uiSearchRuleIndex].uiMaxLatency/UGIValue + 1);

		if (UGIFactor > DEFAULT_UGI_FACTOR)
			Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize =
				(UGIFactor*Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate*UGIValue)/1000;

		if (Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize > WIMAX_MAX_MTU*8)
			Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize = WIMAX_MAX_MTU*8;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "LAT: %d, UGI: %d\n", Adapter->PackInfo[uiSearchRuleIndex].uiMaxLatency, UGIValue);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "uiMaxAllowedRate: 0x%x, u32MaxSustainedTrafficRate: 0x%x ,uiMaxBucketSize: 0x%x",
			Adapter->PackInfo[uiSearchRuleIndex].uiMaxAllowedRate,
			ntohl(psfLocalSet->u32MaxSustainedTrafficRate),
			Adapter->PackInfo[uiSearchRuleIndex].uiMaxBucketSize);

	/* copy the extended SF Parameters to Support MIBS */
	CopyMIBSExtendedSFParameters(Adapter, psfLocalSet, uiSearchRuleIndex);

	/* store header suppression enabled flag per SF */
	Adapter->PackInfo[uiSearchRuleIndex].bHeaderSuppressionEnabled =
		!(psfLocalSet->u8RequesttransmissionPolicy &
			MASK_DISABLE_HEADER_SUPPRESSION);

	kfree(Adapter->PackInfo[uiSearchRuleIndex].pstSFIndication);
	Adapter->PackInfo[uiSearchRuleIndex].pstSFIndication = pstAddIndication;

	/* Re Sort the SF list in PackInfo according to Traffic Priority */
	SortPackInfo(Adapter);

	/* Re Sort the Classifier Rules table and re - arrange
	 * according to Classifier Rule Priority
	 */
	SortClassifiers(Adapter);
	DumpPhsRules(&Adapter->stBCMPhsContext);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "%s <=====", __func__);
}

/***********************************************************************
 * Function - DumpCmControlPacket
 *
 * Description - This routinue Dumps the Contents of the AddIndication
 *  Structure in the Connection Management Control Packet
 *
 * Parameter - pvBuffer: Pointer to the buffer containing the
 *  AddIndication data.
 *
 * Returns - None
 *************************************************************************/
static VOID DumpCmControlPacket(PVOID pvBuffer)
{
	int uiLoopIndex;
	int nIndex;
	stLocalSFAddIndicationAlt *pstAddIndication;
	UINT nCurClassifierCnt;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	pstAddIndication = (stLocalSFAddIndicationAlt *)pvBuffer;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "======>");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Type: 0x%X", pstAddIndication->u8Type);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Direction: 0x%X", pstAddIndication->u8Direction);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16TID: 0x%X", ntohs(pstAddIndication->u16TID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16CID: 0x%X", ntohs(pstAddIndication->u16CID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16VCID: 0x%X", ntohs(pstAddIndication->u16VCID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " AuthorizedSet--->");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32SFID: 0x%X", htonl(pstAddIndication->sfAuthorizedSet.u32SFID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16CID: 0x%X", htons(pstAddIndication->sfAuthorizedSet.u16CID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassNameLength: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8ServiceClassNameLength);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassName: 0x%X ,0x%X , 0x%X, 0x%X, 0x%X, 0x%X",
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[0],
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[1],
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[2],
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[3],
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[4],
			pstAddIndication->sfAuthorizedSet.u8ServiceClassName[5]);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8MBSService: 0x%X", pstAddIndication->sfAuthorizedSet.u8MBSService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8QosParamSet: 0x%X", pstAddIndication->sfAuthorizedSet.u8QosParamSet);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TrafficPriority: 0x%X, %p",
			pstAddIndication->sfAuthorizedSet.u8TrafficPriority, &pstAddIndication->sfAuthorizedSet.u8TrafficPriority);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaxSustainedTrafficRate: 0x%X 0x%p",
			pstAddIndication->sfAuthorizedSet.u32MaxSustainedTrafficRate,
			&pstAddIndication->sfAuthorizedSet.u32MaxSustainedTrafficRate);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaxTrafficBurst: 0x%X", pstAddIndication->sfAuthorizedSet.u32MaxTrafficBurst);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MinReservedTrafficRate	: 0x%X",
			pstAddIndication->sfAuthorizedSet.u32MinReservedTrafficRate);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParamLength: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8VendorSpecificQoSParamLength);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParam: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8VendorSpecificQoSParam[0]);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceFlowSchedulingType: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8ServiceFlowSchedulingType);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32ToleratedJitter: 0x%X", pstAddIndication->sfAuthorizedSet.u32ToleratedJitter);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaximumLatency: 0x%X", pstAddIndication->sfAuthorizedSet.u32MaximumLatency);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8FixedLengthVSVariableLengthSDUIndicator: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8FixedLengthVSVariableLengthSDUIndicator);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8SDUSize: 0x%X",	pstAddIndication->sfAuthorizedSet.u8SDUSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16TargetSAID: 0x%X", pstAddIndication->sfAuthorizedSet.u16TargetSAID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ARQEnable: 0x%X", pstAddIndication->sfAuthorizedSet.u8ARQEnable);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQWindowSize: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQWindowSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRetryTxTimeOut: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQRetryTxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRetryRxTimeOut: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQRetryRxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQBlockLifeTime: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQBlockLifeTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQSyncLossTimeOut: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQSyncLossTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ARQDeliverInOrder: 0x%X", pstAddIndication->sfAuthorizedSet.u8ARQDeliverInOrder);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRxPurgeTimeOut: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQRxPurgeTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQBlockSize: 0x%X", pstAddIndication->sfAuthorizedSet.u16ARQBlockSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8CSSpecification: 0x%X",	pstAddIndication->sfAuthorizedSet.u8CSSpecification);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TypeOfDataDeliveryService: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8TypeOfDataDeliveryService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16SDUInterArrivalTime: 0x%X", pstAddIndication->sfAuthorizedSet.u16SDUInterArrivalTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16TimeBase: 0x%X", pstAddIndication->sfAuthorizedSet.u16TimeBase);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8PagingPreference: 0x%X", pstAddIndication->sfAuthorizedSet.u8PagingPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16UnsolicitedPollingInterval: 0x%X",
			pstAddIndication->sfAuthorizedSet.u16UnsolicitedPollingInterval);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "sfAuthorizedSet.u8HARQChannelMapping %x  %x %x ",
			*(unsigned int *)pstAddIndication->sfAuthorizedSet.u8HARQChannelMapping,
			*(unsigned int *)&pstAddIndication->sfAuthorizedSet.u8HARQChannelMapping[4],
			*(USHORT *)&pstAddIndication->sfAuthorizedSet.u8HARQChannelMapping[8]);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TrafficIndicationPreference: 0x%X",
			pstAddIndication->sfAuthorizedSet.u8TrafficIndicationPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " Total Classifiers Received: 0x%X", pstAddIndication->sfAuthorizedSet.u8TotalClassifiers);

	nCurClassifierCnt = pstAddIndication->sfAuthorizedSet.u8TotalClassifiers;
	if (nCurClassifierCnt > MAX_CLASSIFIERS_IN_SF)
		nCurClassifierCnt = MAX_CLASSIFIERS_IN_SF;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL,  "pstAddIndication->sfAuthorizedSet.bValid %d", pstAddIndication->sfAuthorizedSet.bValid);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL,  "pstAddIndication->sfAuthorizedSet.u16MacOverhead %x", pstAddIndication->sfAuthorizedSet.u16MacOverhead);
	if (!pstAddIndication->sfAuthorizedSet.bValid)
		pstAddIndication->sfAuthorizedSet.bValid = 1;
	for (nIndex = 0; nIndex < nCurClassifierCnt; nIndex++) {
		stConvergenceSLTypes *psfCSType = NULL;
		psfCSType =  &pstAddIndication->sfAuthorizedSet.cConvergenceSLTypes[nIndex];

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "psfCSType = %p", psfCSType);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "CCPacketClassificationRuleSI====>");
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ClassifierRulePriority: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL,  "u8IPTypeOfServiceLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPTypeOfService[3]: 0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[0],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[1],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[2]);

		for (uiLoopIndex = 0; uiLoopIndex < 1; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Protocol: 0x%02X ",
					psfCSType->cCPacketClassificationRule.u8Protocol);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddressLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddress[32]: 0x%02X ",
					psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPDestinationAddressLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPDestinationAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPDestinationAddress[32]: 0x%02X ",
					psfCSType->cCPacketClassificationRule.u8IPDestinationAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolSourcePortRangeLength:0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolSourcePortRange[4]: 0x%02X ,0x%02X ,0x%02X ,0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolDestPortRangeLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRangeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolDestPortRange[4]: 0x%02X ,0x%02X ,0x%02X ,0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthernetDestMacAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL,
				DBG_LVL_ALL, "u8EthernetDestMacAddress[6]: %pM",
				psfCSType->cCPacketClassificationRule.
						u8EthernetDestMacAddress);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthernetSourceMACAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL,
				DBG_LVL_ALL, "u8EthernetSourceMACAddress[6]: "
				"%pM", psfCSType->cCPacketClassificationRule.
						u8EthernetSourceMACAddress);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthertypeLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8EthertypeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Ethertype[3]: 0x%02X ,0x%02X ,0x%02X ",
				psfCSType->cCPacketClassificationRule.u8Ethertype[0],
				psfCSType->cCPacketClassificationRule.u8Ethertype[1],
				psfCSType->cCPacketClassificationRule.u8Ethertype[2]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16UserPriority: 0x%X ", psfCSType->cCPacketClassificationRule.u16UserPriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16VLANID: 0x%X ", psfCSType->cCPacketClassificationRule.u16VLANID);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8AssociatedPHSI: 0x%02X ", psfCSType->cCPacketClassificationRule.u8AssociatedPHSI);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16PacketClassificationRuleIndex: 0x%X ",
				psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificClassifierParamLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParamLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificClassifierParam[1]: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParam[0]);
#ifdef VERSION_D5
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPv6FlowLableLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLableLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPv6FlowLable[6]: 0x %02X %02X %02X %02X %02X %02X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[0],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[1],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[2],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[3],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[4],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[5]);
#endif
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "bValid: 0x%02X", pstAddIndication->sfAuthorizedSet.bValid);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "AdmittedSet--->");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32SFID: 0x%X", pstAddIndication->sfAdmittedSet.u32SFID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16CID: 0x%X", pstAddIndication->sfAdmittedSet.u16CID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassNameLength: 0x%X",
			pstAddIndication->sfAdmittedSet.u8ServiceClassNameLength);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassName: 0x %02X %02X %02X %02X %02X %02X",
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[0],
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[1],
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[2],
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[3],
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[4],
			pstAddIndication->sfAdmittedSet.u8ServiceClassName[5]);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8MBSService: 0x%02X", pstAddIndication->sfAdmittedSet.u8MBSService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8QosParamSet: 0x%02X", pstAddIndication->sfAdmittedSet.u8QosParamSet);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TrafficPriority: 0x%02X", pstAddIndication->sfAdmittedSet.u8TrafficPriority);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaxTrafficBurst: 0x%X", pstAddIndication->sfAdmittedSet.u32MaxTrafficBurst);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MinReservedTrafficRate: 0x%X",
			pstAddIndication->sfAdmittedSet.u32MinReservedTrafficRate);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParamLength: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8VendorSpecificQoSParamLength);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParam: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8VendorSpecificQoSParam[0]);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceFlowSchedulingType: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8ServiceFlowSchedulingType);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32ToleratedJitter: 0x%X", pstAddIndication->sfAdmittedSet.u32ToleratedJitter);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaximumLatency: 0x%X", pstAddIndication->sfAdmittedSet.u32MaximumLatency);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8FixedLengthVSVariableLengthSDUIndicator: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8FixedLengthVSVariableLengthSDUIndicator);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8SDUSize: 0x%02X", pstAddIndication->sfAdmittedSet.u8SDUSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16TargetSAID: 0x%02X", pstAddIndication->sfAdmittedSet.u16TargetSAID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ARQEnable: 0x%02X", pstAddIndication->sfAdmittedSet.u8ARQEnable);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQWindowSize: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQWindowSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRetryTxTimeOut: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQRetryTxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRetryRxTimeOut: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQRetryRxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQBlockLifeTime: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQBlockLifeTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQSyncLossTimeOut: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQSyncLossTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ARQDeliverInOrder: 0x%02X", pstAddIndication->sfAdmittedSet.u8ARQDeliverInOrder);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQRxPurgeTimeOut: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQRxPurgeTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16ARQBlockSize: 0x%X", pstAddIndication->sfAdmittedSet.u16ARQBlockSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8CSSpecification: 0x%02X", pstAddIndication->sfAdmittedSet.u8CSSpecification);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TypeOfDataDeliveryService: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8TypeOfDataDeliveryService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16SDUInterArrivalTime: 0x%X", pstAddIndication->sfAdmittedSet.u16SDUInterArrivalTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16TimeBase: 0x%X", pstAddIndication->sfAdmittedSet.u16TimeBase);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8PagingPreference: 0x%X", pstAddIndication->sfAdmittedSet.u8PagingPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TrafficIndicationPreference: 0x%02X",
			pstAddIndication->sfAdmittedSet.u8TrafficIndicationPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " Total Classifiers Received: 0x%X", pstAddIndication->sfAdmittedSet.u8TotalClassifiers);

	nCurClassifierCnt = pstAddIndication->sfAdmittedSet.u8TotalClassifiers;
	if (nCurClassifierCnt > MAX_CLASSIFIERS_IN_SF)
		nCurClassifierCnt = MAX_CLASSIFIERS_IN_SF;

	for (nIndex = 0; nIndex < nCurClassifierCnt; nIndex++) {
		stConvergenceSLTypes *psfCSType = NULL;

		psfCSType =  &pstAddIndication->sfAdmittedSet.cConvergenceSLTypes[nIndex];
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " CCPacketClassificationRuleSI====>");
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ClassifierRulePriority: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPTypeOfServiceLength: 0x%02X",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPTypeOfService[3]: 0x%02X %02X %02X",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[0],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[1],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[2]);
		for (uiLoopIndex = 0; uiLoopIndex < 1; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Protocol: 0x%02X ", psfCSType->cCPacketClassificationRule.u8Protocol);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddress[32]: 0x%02X ",
					psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPDestinationAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8IPDestinationAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPDestinationAddress[32]: 0x%02X ",
					psfCSType->cCPacketClassificationRule.u8IPDestinationAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolSourcePortRangeLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolSourcePortRange[4]: 0x %02X %02X %02X %02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolDestPortRangeLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRangeLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ProtocolDestPortRange[4]: 0x %02X %02X %02X %02X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthernetDestMacAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL,
				DBG_LVL_ALL, "u8EthernetDestMacAddress[6]: %pM",
				psfCSType->cCPacketClassificationRule.
						u8EthernetDestMacAddress);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthernetSourceMACAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL,
				DBG_LVL_ALL, "u8EthernetSourceMACAddress[6]: "
				"%pM", psfCSType->cCPacketClassificationRule.
						u8EthernetSourceMACAddress);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthertypeLength: 0x%02X ", psfCSType->cCPacketClassificationRule.u8EthertypeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8Ethertype[3]: 0x%02X %02X %02X",
				psfCSType->cCPacketClassificationRule.u8Ethertype[0],
				psfCSType->cCPacketClassificationRule.u8Ethertype[1],
				psfCSType->cCPacketClassificationRule.u8Ethertype[2]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16UserPriority: 0x%X ", psfCSType->cCPacketClassificationRule.u16UserPriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16VLANID: 0x%X ", psfCSType->cCPacketClassificationRule.u16VLANID);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8AssociatedPHSI: 0x%02X ", psfCSType->cCPacketClassificationRule.u8AssociatedPHSI);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16PacketClassificationRuleIndex: 0x%X ",
				psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificClassifierParamLength: 0x%02X",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParamLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificClassifierParam[1]: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParam[0]);
#ifdef VERSION_D5
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPv6FlowLableLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLableLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPv6FlowLable[6]: 0x %02X %02X %02X %02X %02X %02X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[0],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[1],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[2],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[3],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[4],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[5]);
#endif
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "bValid: 0x%X", pstAddIndication->sfAdmittedSet.bValid);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " ActiveSet--->");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32SFID: 0x%X", pstAddIndication->sfActiveSet.u32SFID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u16CID: 0x%X", pstAddIndication->sfActiveSet.u16CID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassNameLength: 0x%X", pstAddIndication->sfActiveSet.u8ServiceClassNameLength);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceClassName: 0x %02X %02X %02X %02X %02X %02X",
			pstAddIndication->sfActiveSet.u8ServiceClassName[0],
			pstAddIndication->sfActiveSet.u8ServiceClassName[1],
			pstAddIndication->sfActiveSet.u8ServiceClassName[2],
			pstAddIndication->sfActiveSet.u8ServiceClassName[3],
			pstAddIndication->sfActiveSet.u8ServiceClassName[4],
			pstAddIndication->sfActiveSet.u8ServiceClassName[5]);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8MBSService: 0x%02X", pstAddIndication->sfActiveSet.u8MBSService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8QosParamSet: 0x%02X", pstAddIndication->sfActiveSet.u8QosParamSet);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8TrafficPriority: 0x%02X", pstAddIndication->sfActiveSet.u8TrafficPriority);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaxTrafficBurst: 0x%X", pstAddIndication->sfActiveSet.u32MaxTrafficBurst);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MinReservedTrafficRate: 0x%X",
			pstAddIndication->sfActiveSet.u32MinReservedTrafficRate);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParamLength: 0x%02X",
			pstAddIndication->sfActiveSet.u8VendorSpecificQoSParamLength);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8VendorSpecificQoSParam: 0x%02X",
			pstAddIndication->sfActiveSet.u8VendorSpecificQoSParam[0]);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8ServiceFlowSchedulingType: 0x%02X",
			pstAddIndication->sfActiveSet.u8ServiceFlowSchedulingType);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32ToleratedJitter: 0x%X", pstAddIndication->sfActiveSet.u32ToleratedJitter);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u32MaximumLatency: 0x%X",	pstAddIndication->sfActiveSet.u32MaximumLatency);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8FixedLengthVSVariableLengthSDUIndicator: 0x%02X",
			pstAddIndication->sfActiveSet.u8FixedLengthVSVariableLengthSDUIndicator);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8SDUSize: 0x%X",	pstAddIndication->sfActiveSet.u8SDUSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16TargetSAID: 0x%X", pstAddIndication->sfActiveSet.u16TargetSAID);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ARQEnable: 0x%X", pstAddIndication->sfActiveSet.u8ARQEnable);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQWindowSize: 0x%X", pstAddIndication->sfActiveSet.u16ARQWindowSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQRetryTxTimeOut: 0x%X", pstAddIndication->sfActiveSet.u16ARQRetryTxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQRetryRxTimeOut: 0x%X", pstAddIndication->sfActiveSet.u16ARQRetryRxTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQBlockLifeTime: 0x%X", pstAddIndication->sfActiveSet.u16ARQBlockLifeTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQSyncLossTimeOut: 0x%X", pstAddIndication->sfActiveSet.u16ARQSyncLossTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ARQDeliverInOrder: 0x%X", pstAddIndication->sfActiveSet.u8ARQDeliverInOrder);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQRxPurgeTimeOut: 0x%X", pstAddIndication->sfActiveSet.u16ARQRxPurgeTimeOut);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16ARQBlockSize: 0x%X", pstAddIndication->sfActiveSet.u16ARQBlockSize);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8CSSpecification: 0x%X", pstAddIndication->sfActiveSet.u8CSSpecification);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8TypeOfDataDeliveryService: 0x%X",
			pstAddIndication->sfActiveSet.u8TypeOfDataDeliveryService);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16SDUInterArrivalTime: 0x%X", pstAddIndication->sfActiveSet.u16SDUInterArrivalTime);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16TimeBase: 0x%X", pstAddIndication->sfActiveSet.u16TimeBase);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8PagingPreference: 0x%X", pstAddIndication->sfActiveSet.u8PagingPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8TrafficIndicationPreference: 0x%X",
			pstAddIndication->sfActiveSet.u8TrafficIndicationPreference);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " Total Classifiers Received: 0x%X", pstAddIndication->sfActiveSet.u8TotalClassifiers);

	nCurClassifierCnt = pstAddIndication->sfActiveSet.u8TotalClassifiers;
	if (nCurClassifierCnt > MAX_CLASSIFIERS_IN_SF)
		nCurClassifierCnt = MAX_CLASSIFIERS_IN_SF;

	for (nIndex = 0; nIndex < nCurClassifierCnt; nIndex++)	{
		stConvergenceSLTypes *psfCSType = NULL;

		psfCSType =  &pstAddIndication->sfActiveSet.cConvergenceSLTypes[nIndex];
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " CCPacketClassificationRuleSI====>");
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ClassifierRulePriority: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8ClassifierRulePriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8IPTypeOfServiceLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfServiceLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8IPTypeOfService[3]: 0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[0],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[1],
				psfCSType->cCPacketClassificationRule.u8IPTypeOfService[2]);

		for (uiLoopIndex = 0; uiLoopIndex < 1; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8Protocol: 0x%X ", psfCSType->cCPacketClassificationRule.u8Protocol);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddressLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPMaskedSourceAddress[32]: 0x%X ",
					psfCSType->cCPacketClassificationRule.u8IPMaskedSourceAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8IPDestinationAddressLength: 0x%02X ",
				psfCSType->cCPacketClassificationRule.u8IPDestinationAddressLength);

		for (uiLoopIndex = 0; uiLoopIndex < 32; uiLoopIndex++)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8IPDestinationAddress[32]:0x%X ",
					psfCSType->cCPacketClassificationRule.u8IPDestinationAddress[uiLoopIndex]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ProtocolSourcePortRangeLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRangeLength);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ProtocolSourcePortRange[4]: 0x%X ,0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolSourcePortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ProtocolDestPortRangeLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRangeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8ProtocolDestPortRange[4]: 0x%X ,0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[0],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[1],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[2],
				psfCSType->cCPacketClassificationRule.u8ProtocolDestPortRange[3]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8EthernetDestMacAddressLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8EthernetDestMacAddress[6]: 0x%X ,0x%X ,0x%X ,0x%X ,0x%X ,0x%X",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[0],
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[1],
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[2],
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[3],
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[4],
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddress[5]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8EthernetSourceMACAddressLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8EthernetDestMacAddressLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, "u8EthernetSourceMACAddress[6]: 0x%X ,0x%X ,0x%X ,0x%X ,0x%X ,0x%X",
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[0],
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[1],
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[2],
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[3],
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[4],
				psfCSType->cCPacketClassificationRule.u8EthernetSourceMACAddress[5]);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8EthertypeLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8EthertypeLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8Ethertype[3]: 0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8Ethertype[0],
				psfCSType->cCPacketClassificationRule.u8Ethertype[1],
				psfCSType->cCPacketClassificationRule.u8Ethertype[2]);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16UserPriority: 0x%X ",
				psfCSType->cCPacketClassificationRule.u16UserPriority);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16VLANID: 0x%X ", psfCSType->cCPacketClassificationRule.u16VLANID);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8AssociatedPHSI: 0x%X ", psfCSType->cCPacketClassificationRule.u8AssociatedPHSI);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u16PacketClassificationRuleIndex:0x%X ",
				psfCSType->cCPacketClassificationRule.u16PacketClassificationRuleIndex);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8VendorSpecificClassifierParamLength:0x%X ",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParamLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8VendorSpecificClassifierParam[1]:0x%X ",
				psfCSType->cCPacketClassificationRule.u8VendorSpecificClassifierParam[0]);
#ifdef VERSION_D5
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8IPv6FlowLableLength: 0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLableLength);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " u8IPv6FlowLable[6]: 0x%X ,0x%X ,0x%X ,0x%X ,0x%X ,0x%X ",
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[0],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[1],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[2],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[3],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[4],
				psfCSType->cCPacketClassificationRule.u8IPv6FlowLable[5]);
#endif
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_CONTROL, DBG_LVL_ALL, " bValid: 0x%X", pstAddIndication->sfActiveSet.bValid);
}

static inline ULONG RestoreSFParam(struct bcm_mini_adapter *Adapter, ULONG ulAddrSFParamSet, PUCHAR pucDestBuffer)
{
	UINT  nBytesToRead = sizeof(stServiceFlowParamSI);

	if (ulAddrSFParamSet == 0 || NULL == pucDestBuffer) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Got Param address as 0!!");
		return 0;
	}
	ulAddrSFParamSet = ntohl(ulAddrSFParamSet);

	/* Read out the SF Param Set At the indicated Location */
	if (rdm(Adapter, ulAddrSFParamSet, (PUCHAR)pucDestBuffer, nBytesToRead) < 0)
		return STATUS_FAILURE;

	return 1;
}

static ULONG StoreSFParam(struct bcm_mini_adapter *Adapter, PUCHAR pucSrcBuffer, ULONG ulAddrSFParamSet)
{
	UINT nBytesToWrite = sizeof(stServiceFlowParamSI);
	int ret = 0;

	if (ulAddrSFParamSet == 0 || NULL == pucSrcBuffer)
		return 0;

	ret = wrm(Adapter, ulAddrSFParamSet, (u8 *)pucSrcBuffer, nBytesToWrite);
	if (ret < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "%s:%d WRM failed", __func__, __LINE__);
		return ret;
	}
	return 1;
}

ULONG StoreCmControlResponseMessage(struct bcm_mini_adapter *Adapter, PVOID pvBuffer, UINT *puBufferLength)
{
	stLocalSFAddIndicationAlt *pstAddIndicationAlt = NULL;
	stLocalSFAddIndication *pstAddIndication = NULL;
	stLocalSFDeleteRequest *pstDeletionRequest;
	UINT uiSearchRuleIndex;
	ULONG ulSFID;

	pstAddIndicationAlt = (stLocalSFAddIndicationAlt *)(pvBuffer);

	/*
	 * In case of DSD Req By MS, we should immediately delete this SF so that
	 * we can stop the further classifying the pkt for this SF.
	 */
	if (pstAddIndicationAlt->u8Type == DSD_REQ) {
		pstDeletionRequest = (stLocalSFDeleteRequest *)pvBuffer;

		ulSFID = ntohl(pstDeletionRequest->u32SFID);
		uiSearchRuleIndex = SearchSfid(Adapter, ulSFID);

		if (uiSearchRuleIndex < NO_OF_QUEUES) {
			deleteSFBySfid(Adapter, uiSearchRuleIndex);
			Adapter->u32TotalDSD++;
		}
		return 1;
	}

	if ((pstAddIndicationAlt->u8Type == DSD_RSP) ||
		(pstAddIndicationAlt->u8Type == DSD_ACK)) {
		/* No Special handling send the message as it is */
		return 1;
	}
	/* For DSA_REQ, only up to "psfAuthorizedSet" parameter should be accessed by driver! */

	pstAddIndication = kmalloc(sizeof(*pstAddIndication), GFP_KERNEL);
	if (pstAddIndication == NULL)
		return 0;

	/* AUTHORIZED SET */
	pstAddIndication->psfAuthorizedSet = (stServiceFlowParamSI *)
			GetNextTargetBufferLocation(Adapter, pstAddIndicationAlt->u16TID);
	if (!pstAddIndication->psfAuthorizedSet) {
		kfree(pstAddIndication);
		return 0;
	}

	if (StoreSFParam(Adapter, (PUCHAR)&pstAddIndicationAlt->sfAuthorizedSet,
				(ULONG)pstAddIndication->psfAuthorizedSet) != 1) {
		kfree(pstAddIndication);
		return 0;
	}

	/* this can't possibly be right */
	pstAddIndication->psfAuthorizedSet = (stServiceFlowParamSI *)ntohl((ULONG)pstAddIndication->psfAuthorizedSet);

	if (pstAddIndicationAlt->u8Type == DSA_REQ) {
		stLocalSFAddRequest AddRequest;

		AddRequest.u8Type = pstAddIndicationAlt->u8Type;
		AddRequest.eConnectionDir = pstAddIndicationAlt->u8Direction;
		AddRequest.u16TID = pstAddIndicationAlt->u16TID;
		AddRequest.u16CID = pstAddIndicationAlt->u16CID;
		AddRequest.u16VCID = pstAddIndicationAlt->u16VCID;
		AddRequest.psfParameterSet = pstAddIndication->psfAuthorizedSet;
		(*puBufferLength) = sizeof(stLocalSFAddRequest);
		memcpy(pvBuffer, &AddRequest, sizeof(stLocalSFAddRequest));
		kfree(pstAddIndication);
		return 1;
	}

	/* Since it's not DSA_REQ, we can access all field in pstAddIndicationAlt */
	/* We need to extract the structure from the buffer and pack it differently */

	pstAddIndication->u8Type = pstAddIndicationAlt->u8Type;
	pstAddIndication->eConnectionDir = pstAddIndicationAlt->u8Direction;
	pstAddIndication->u16TID = pstAddIndicationAlt->u16TID;
	pstAddIndication->u16CID = pstAddIndicationAlt->u16CID;
	pstAddIndication->u16VCID = pstAddIndicationAlt->u16VCID;
	pstAddIndication->u8CC = pstAddIndicationAlt->u8CC;

	/* ADMITTED SET */
	pstAddIndication->psfAdmittedSet = (stServiceFlowParamSI *)
		GetNextTargetBufferLocation(Adapter, pstAddIndicationAlt->u16TID);
	if (!pstAddIndication->psfAdmittedSet) {
		kfree(pstAddIndication);
		return 0;
	}
	if (StoreSFParam(Adapter, (PUCHAR)&pstAddIndicationAlt->sfAdmittedSet, (ULONG)pstAddIndication->psfAdmittedSet) != 1) {
		kfree(pstAddIndication);
		return 0;
	}

	pstAddIndication->psfAdmittedSet = (stServiceFlowParamSI *)ntohl((ULONG)pstAddIndication->psfAdmittedSet);

	/* ACTIVE SET */
	pstAddIndication->psfActiveSet = (stServiceFlowParamSI *)
		GetNextTargetBufferLocation(Adapter, pstAddIndicationAlt->u16TID);
	if (!pstAddIndication->psfActiveSet) {
		kfree(pstAddIndication);
		return 0;
	}
	if (StoreSFParam(Adapter, (PUCHAR)&pstAddIndicationAlt->sfActiveSet, (ULONG)pstAddIndication->psfActiveSet) != 1) {
		kfree(pstAddIndication);
		return 0;
	}

	pstAddIndication->psfActiveSet = (stServiceFlowParamSI *)ntohl((ULONG)pstAddIndication->psfActiveSet);

	(*puBufferLength) = sizeof(stLocalSFAddIndication);
	*(stLocalSFAddIndication *)pvBuffer = *pstAddIndication;
	kfree(pstAddIndication);
	return 1;
}

static inline stLocalSFAddIndicationAlt
*RestoreCmControlResponseMessage(register struct bcm_mini_adapter *Adapter, register PVOID pvBuffer)
{
	ULONG ulStatus = 0;
	stLocalSFAddIndication *pstAddIndication = NULL;
	stLocalSFAddIndicationAlt *pstAddIndicationDest = NULL;

	pstAddIndication = (stLocalSFAddIndication *)(pvBuffer);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "=====>");
	if ((pstAddIndication->u8Type == DSD_REQ) ||
		(pstAddIndication->u8Type == DSD_RSP) ||
		(pstAddIndication->u8Type == DSD_ACK))
		return (stLocalSFAddIndicationAlt *)pvBuffer;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Inside RestoreCmControlResponseMessage ");
	/*
	 * Need to Allocate memory to contain the SUPER Large structures
	 * Our driver can't create these structures on Stack :(
	 */
	pstAddIndicationDest = kmalloc(sizeof(stLocalSFAddIndicationAlt), GFP_KERNEL);

	if (pstAddIndicationDest) {
		memset(pstAddIndicationDest, 0, sizeof(stLocalSFAddIndicationAlt));
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Failed to allocate memory for SF Add Indication Structure ");
		return NULL;
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-u8Type : 0x%X", pstAddIndication->u8Type);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-u8Direction : 0x%X", pstAddIndication->eConnectionDir);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-u8TID : 0x%X", ntohs(pstAddIndication->u16TID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-u8CID : 0x%X", ntohs(pstAddIndication->u16CID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-u16VCID : 0x%X", ntohs(pstAddIndication->u16VCID));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-autorized set loc : %p", pstAddIndication->psfAuthorizedSet);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-admitted set loc : %p", pstAddIndication->psfAdmittedSet);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "AddIndication-Active set loc : %p", pstAddIndication->psfActiveSet);

	pstAddIndicationDest->u8Type = pstAddIndication->u8Type;
	pstAddIndicationDest->u8Direction = pstAddIndication->eConnectionDir;
	pstAddIndicationDest->u16TID = pstAddIndication->u16TID;
	pstAddIndicationDest->u16CID = pstAddIndication->u16CID;
	pstAddIndicationDest->u16VCID = pstAddIndication->u16VCID;
	pstAddIndicationDest->u8CC = pstAddIndication->u8CC;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,  "Restoring Active Set ");
	ulStatus = RestoreSFParam(Adapter, (ULONG)pstAddIndication->psfActiveSet, (PUCHAR)&pstAddIndicationDest->sfActiveSet);
	if (ulStatus != 1)
		goto failed_restore_sf_param;

	if (pstAddIndicationDest->sfActiveSet.u8TotalClassifiers > MAX_CLASSIFIERS_IN_SF)
		pstAddIndicationDest->sfActiveSet.u8TotalClassifiers = MAX_CLASSIFIERS_IN_SF;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,  "Restoring Admitted Set ");
	ulStatus = RestoreSFParam(Adapter, (ULONG)pstAddIndication->psfAdmittedSet, (PUCHAR)&pstAddIndicationDest->sfAdmittedSet);
	if (ulStatus != 1)
		goto failed_restore_sf_param;

	if (pstAddIndicationDest->sfAdmittedSet.u8TotalClassifiers > MAX_CLASSIFIERS_IN_SF)
		pstAddIndicationDest->sfAdmittedSet.u8TotalClassifiers = MAX_CLASSIFIERS_IN_SF;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,  "Restoring Authorized Set ");
	ulStatus = RestoreSFParam(Adapter, (ULONG)pstAddIndication->psfAuthorizedSet, (PUCHAR)&pstAddIndicationDest->sfAuthorizedSet);
	if (ulStatus != 1)
		goto failed_restore_sf_param;

	if (pstAddIndicationDest->sfAuthorizedSet.u8TotalClassifiers > MAX_CLASSIFIERS_IN_SF)
		pstAddIndicationDest->sfAuthorizedSet.u8TotalClassifiers = MAX_CLASSIFIERS_IN_SF;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Dumping the whole raw packet");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "============================================================");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, " pstAddIndicationDest->sfActiveSet size  %zx %p", sizeof(*pstAddIndicationDest), pstAddIndicationDest);
	/* BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, (unsigned char *)pstAddIndicationDest, sizeof(*pstAddIndicationDest)); */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "============================================================");
	return pstAddIndicationDest;
failed_restore_sf_param:
	kfree(pstAddIndicationDest);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "<=====");
	return NULL;
}

ULONG SetUpTargetDsxBuffers(struct bcm_mini_adapter *Adapter)
{
	ULONG ulTargetDsxBuffersBase = 0;
	ULONG ulCntTargetBuffers;
	ULONG i;
	int Status;

	if (!Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Adapter was NULL!!!");
		return 0;
	}

	if (Adapter->astTargetDsxBuffer[0].ulTargetDsxBuffer)
		return 1;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Size of Each DSX Buffer(Also size of ServiceFlowParamSI): %zx ", sizeof(stServiceFlowParamSI));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Reading DSX buffer From Target location %x ", DSX_MESSAGE_EXCHANGE_BUFFER);

	Status = rdmalt(Adapter, DSX_MESSAGE_EXCHANGE_BUFFER, (PUINT)&ulTargetDsxBuffersBase, sizeof(UINT));
	if (Status < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "RDM failed!!");
		return 0;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Base Address Of DSX  Target Buffer : 0x%lx", ulTargetDsxBuffersBase);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL,  "Tgt Buffer is Now %lx :", ulTargetDsxBuffersBase);
	ulCntTargetBuffers = DSX_MESSAGE_EXCHANGE_BUFFER_SIZE / sizeof(stServiceFlowParamSI);

	Adapter->ulTotalTargetBuffersAvailable =
		ulCntTargetBuffers > MAX_TARGET_DSX_BUFFERS ?
		MAX_TARGET_DSX_BUFFERS : ulCntTargetBuffers;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, " Total Target DSX Buffer setup %lx ", Adapter->ulTotalTargetBuffersAvailable);

	for (i = 0; i < Adapter->ulTotalTargetBuffersAvailable; i++) {
		Adapter->astTargetDsxBuffer[i].ulTargetDsxBuffer = ulTargetDsxBuffersBase;
		Adapter->astTargetDsxBuffer[i].valid = 1;
		Adapter->astTargetDsxBuffer[i].tid = 0;
		ulTargetDsxBuffersBase += sizeof(stServiceFlowParamSI);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "  Target DSX Buffer %lx setup at 0x%lx",
				i, Adapter->astTargetDsxBuffer[i].ulTargetDsxBuffer);
	}
	Adapter->ulCurrentTargetBuffer = 0;
	Adapter->ulFreeTargetBufferCnt = Adapter->ulTotalTargetBuffersAvailable;
	return 1;
}

static ULONG GetNextTargetBufferLocation(struct bcm_mini_adapter *Adapter, B_UINT16 tid)
{
	ULONG ulTargetDSXBufferAddress;
	ULONG ulTargetDsxBufferIndexToUse, ulMaxTry;

	if ((Adapter->ulTotalTargetBuffersAvailable == 0) || (Adapter->ulFreeTargetBufferCnt == 0)) {
		ClearTargetDSXBuffer(Adapter, tid, FALSE);
		return 0;
	}

	ulTargetDsxBufferIndexToUse = Adapter->ulCurrentTargetBuffer;
	ulMaxTry = Adapter->ulTotalTargetBuffersAvailable;
	while ((ulMaxTry) && (Adapter->astTargetDsxBuffer[ulTargetDsxBufferIndexToUse].valid != 1)) {
		ulTargetDsxBufferIndexToUse = (ulTargetDsxBufferIndexToUse+1) % Adapter->ulTotalTargetBuffersAvailable;
		ulMaxTry--;
	}

	if (ulMaxTry == 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "\n GetNextTargetBufferLocation : Error No Free Target DSX Buffers FreeCnt : %lx ", Adapter->ulFreeTargetBufferCnt);
		ClearTargetDSXBuffer(Adapter, tid, FALSE);
		return 0;
	}

	ulTargetDSXBufferAddress = Adapter->astTargetDsxBuffer[ulTargetDsxBufferIndexToUse].ulTargetDsxBuffer;
	Adapter->astTargetDsxBuffer[ulTargetDsxBufferIndexToUse].valid = 0;
	Adapter->astTargetDsxBuffer[ulTargetDsxBufferIndexToUse].tid = tid;
	Adapter->ulFreeTargetBufferCnt--;
	ulTargetDsxBufferIndexToUse = (ulTargetDsxBufferIndexToUse+1)%Adapter->ulTotalTargetBuffersAvailable;
	Adapter->ulCurrentTargetBuffer = ulTargetDsxBufferIndexToUse;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "GetNextTargetBufferLocation :Returning address %lx tid %d\n", ulTargetDSXBufferAddress, tid);

	return ulTargetDSXBufferAddress;
}

int AllocAdapterDsxBuffer(struct bcm_mini_adapter *Adapter)
{
	/*
	 * Need to Allocate memory to contain the SUPER Large structures
	 * Our driver can't create these structures on Stack
	 */
	Adapter->caDsxReqResp = kmalloc(sizeof(stLocalSFAddIndicationAlt)+LEADER_SIZE, GFP_KERNEL);
	if (!Adapter->caDsxReqResp)
		return -ENOMEM;

	return 0;
}

int FreeAdapterDsxBuffer(struct bcm_mini_adapter *Adapter)
{
	kfree(Adapter->caDsxReqResp);
	return 0;
}

/*
 * @ingroup ctrl_pkt_functions
 * This routinue would process the Control responses
 * for the Connection Management.
 * @return - Queue index for the free SFID else returns Invalid Index.
 */
BOOLEAN CmControlResponseMessage(struct bcm_mini_adapter *Adapter,  /* <Pointer to the Adapter structure */
				PVOID pvBuffer /* Starting Address of the Buffer, that contains the AddIndication Data */)
{
	stServiceFlowParamSI *psfLocalSet = NULL;
	stLocalSFAddIndicationAlt *pstAddIndication = NULL;
	stLocalSFChangeIndicationAlt *pstChangeIndication = NULL;
	struct bcm_leader *pLeader = NULL;

	/*
	 * Otherwise the message contains a target address from where we need to
	 * read out the rest of the service flow param structure
	 */
	pstAddIndication = RestoreCmControlResponseMessage(Adapter, pvBuffer);
	if (pstAddIndication == NULL) {
		ClearTargetDSXBuffer(Adapter, ((stLocalSFAddIndication *)pvBuffer)->u16TID, FALSE);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Error in restoring Service Flow param structure from DSx message");
		return FALSE;
	}

	DumpCmControlPacket(pstAddIndication);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "====>");
	pLeader = (struct bcm_leader *)Adapter->caDsxReqResp;

	pLeader->Status = CM_CONTROL_NEWDSX_MULTICLASSIFIER_REQ;
	pLeader->Vcid = 0;

	ClearTargetDSXBuffer(Adapter, pstAddIndication->u16TID, FALSE);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "### TID RECEIVED %d\n", pstAddIndication->u16TID);
	switch (pstAddIndication->u8Type) {
	case DSA_REQ:
	{
		pLeader->PLength = sizeof(stLocalSFAddIndicationAlt);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Sending DSA Response....\n");
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SENDING DSA RESPONSE TO MAC %d", pLeader->PLength);
		*((stLocalSFAddIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))
			= *pstAddIndication;
		((stLocalSFAddIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))->u8Type = DSA_RSP;

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, " VCID = %x", ntohs(pstAddIndication->u16VCID));
		CopyBufferToControlPacket(Adapter, (PVOID)Adapter->caDsxReqResp);
		kfree(pstAddIndication);
	}
	break;
	case DSA_RSP:
	{
		pLeader->PLength = sizeof(stLocalSFAddIndicationAlt);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SENDING DSA ACK TO MAC %d",
				pLeader->PLength);
		*((stLocalSFAddIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))
			= *pstAddIndication;
		((stLocalSFAddIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))->u8Type = DSA_ACK;

	} /* no break here..we should go down. */
	case DSA_ACK:
	{
		UINT uiSearchRuleIndex = 0;

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "VCID:0x%X",
				ntohs(pstAddIndication->u16VCID));
		uiSearchRuleIndex = SearchFreeSfid(Adapter);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "uiSearchRuleIndex:0x%X ",
				uiSearchRuleIndex);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Direction:0x%X ",
				pstAddIndication->u8Direction);
		if ((uiSearchRuleIndex < NO_OF_QUEUES)) {
			Adapter->PackInfo[uiSearchRuleIndex].ucDirection =
				pstAddIndication->u8Direction;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "bValid:0x%X ",
					pstAddIndication->sfActiveSet.bValid);
			if (pstAddIndication->sfActiveSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bActiveSet = TRUE;

			if (pstAddIndication->sfAuthorizedSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bAuthorizedSet = TRUE;

			if (pstAddIndication->sfAdmittedSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bAdmittedSet = TRUE;

			if (pstAddIndication->sfActiveSet.bValid == FALSE) {
				Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].bActivateRequestSent = FALSE;
				if (pstAddIndication->sfAdmittedSet.bValid)
					psfLocalSet = &pstAddIndication->sfAdmittedSet;
				else if (pstAddIndication->sfAuthorizedSet.bValid)
					psfLocalSet = &pstAddIndication->sfAuthorizedSet;
			} else {
				psfLocalSet = &pstAddIndication->sfActiveSet;
				Adapter->PackInfo[uiSearchRuleIndex].bActive = TRUE;
			}

			if (!psfLocalSet) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "No set is valid\n");
				Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].bValid = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = 0;
				kfree(pstAddIndication);
			} else if (psfLocalSet->bValid && (pstAddIndication->u8CC == 0)) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "DSA ACK");
				Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = ntohs(pstAddIndication->u16VCID);
				Adapter->PackInfo[uiSearchRuleIndex].usCID = ntohs(pstAddIndication->u16CID);

				if (UPLINK_DIR == pstAddIndication->u8Direction)
					atomic_set(&Adapter->PackInfo[uiSearchRuleIndex].uiPerSFTxResourceCount, DEFAULT_PERSFCOUNT);

				CopyToAdapter(Adapter, psfLocalSet, uiSearchRuleIndex, DSA_ACK, pstAddIndication);
				/* don't free pstAddIndication */

				/* Inside CopyToAdapter, Sorting of all the SFs take place.
				 * Hence any access to the newly added SF through uiSearchRuleIndex is invalid.
				 * SHOULD BE STRICTLY AVOIDED.
				 */
				/* *(PULONG)(((PUCHAR)pvBuffer)+1)=psfLocalSet->u32SFID; */
				memcpy((((PUCHAR)pvBuffer)+1), &psfLocalSet->u32SFID, 4);

				if (pstAddIndication->sfActiveSet.bValid == TRUE) {
					if (UPLINK_DIR == pstAddIndication->u8Direction) {
						if (!Adapter->LinkUpStatus) {
							netif_carrier_on(Adapter->dev);
							netif_start_queue(Adapter->dev);
							Adapter->LinkUpStatus = 1;
							if (netif_msg_link(Adapter))
								pr_info(PFX "%s: link up\n", Adapter->dev->name);
							atomic_set(&Adapter->TxPktAvail, 1);
							wake_up(&Adapter->tx_packet_wait_queue);
							Adapter->liTimeSinceLastNetEntry = get_seconds();
						}
					}
				}
			} else {
				Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].bValid = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = 0;
				kfree(pstAddIndication);
			}
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "DSA ACK did not get valid SFID");
			kfree(pstAddIndication);
			return FALSE;
		}
	}
	break;
	case DSC_REQ:
	{
		pLeader->PLength = sizeof(stLocalSFChangeIndicationAlt);
		pstChangeIndication = (stLocalSFChangeIndicationAlt *)pstAddIndication;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SENDING DSC RESPONSE TO MAC %d", pLeader->PLength);

		*((stLocalSFChangeIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE])) = *pstChangeIndication;
		((stLocalSFChangeIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))->u8Type = DSC_RSP;

		CopyBufferToControlPacket(Adapter, (PVOID)Adapter->caDsxReqResp);
		kfree(pstAddIndication);
	}
	break;
	case DSC_RSP:
	{
		pLeader->PLength = sizeof(stLocalSFChangeIndicationAlt);
		pstChangeIndication = (stLocalSFChangeIndicationAlt *)pstAddIndication;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SENDING DSC ACK TO MAC %d", pLeader->PLength);
		*((stLocalSFChangeIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE])) = *pstChangeIndication;
		((stLocalSFChangeIndicationAlt *)&(Adapter->caDsxReqResp[LEADER_SIZE]))->u8Type = DSC_ACK;
	}
	case DSC_ACK:
	{
		UINT uiSearchRuleIndex = 0;

		pstChangeIndication = (stLocalSFChangeIndicationAlt *)pstAddIndication;
		uiSearchRuleIndex = SearchSfid(Adapter, ntohl(pstChangeIndication->sfActiveSet.u32SFID));
		if (uiSearchRuleIndex > NO_OF_QUEUES-1)
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "SF doesn't exist for which DSC_ACK is received");

		if ((uiSearchRuleIndex < NO_OF_QUEUES)) {
			Adapter->PackInfo[uiSearchRuleIndex].ucDirection = pstChangeIndication->u8Direction;
			if (pstChangeIndication->sfActiveSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bActiveSet = TRUE;

			if (pstChangeIndication->sfAuthorizedSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bAuthorizedSet = TRUE;

			if (pstChangeIndication->sfAdmittedSet.bValid == TRUE)
				Adapter->PackInfo[uiSearchRuleIndex].bAdmittedSet = TRUE;

			if (pstChangeIndication->sfActiveSet.bValid == FALSE) {
				Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].bActivateRequestSent = FALSE;

				if (pstChangeIndication->sfAdmittedSet.bValid)
					psfLocalSet = &pstChangeIndication->sfAdmittedSet;
				else if (pstChangeIndication->sfAuthorizedSet.bValid)
					psfLocalSet = &pstChangeIndication->sfAuthorizedSet;
			} else {
				psfLocalSet = &pstChangeIndication->sfActiveSet;
				Adapter->PackInfo[uiSearchRuleIndex].bActive = TRUE;
			}

			if (!psfLocalSet) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "No set is valid\n");
				Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].bValid = FALSE;
				Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = 0;
				kfree(pstAddIndication);
			} else if (psfLocalSet->bValid && (pstChangeIndication->u8CC == 0)) {
				Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = ntohs(pstChangeIndication->u16VCID);
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "CC field is %d bvalid = %d\n",
						pstChangeIndication->u8CC, psfLocalSet->bValid);
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "VCID= %d\n", ntohs(pstChangeIndication->u16VCID));
				Adapter->PackInfo[uiSearchRuleIndex].usCID = ntohs(pstChangeIndication->u16CID);
				CopyToAdapter(Adapter, psfLocalSet, uiSearchRuleIndex, DSC_ACK, pstAddIndication);

				*(PULONG)(((PUCHAR)pvBuffer)+1) = psfLocalSet->u32SFID;
			} else if (pstChangeIndication->u8CC == 6) {
				deleteSFBySfid(Adapter, uiSearchRuleIndex);
				kfree(pstAddIndication);
			}
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "DSC ACK did not get valid SFID");
			kfree(pstAddIndication);
			return FALSE;
		}
	}
	break;
	case DSD_REQ:
	{
		UINT uiSearchRuleIndex;
		ULONG ulSFID;

		pLeader->PLength = sizeof(stLocalSFDeleteIndication);
		*((stLocalSFDeleteIndication *)&(Adapter->caDsxReqResp[LEADER_SIZE])) = *((stLocalSFDeleteIndication *)pstAddIndication);

		ulSFID = ntohl(((stLocalSFDeleteIndication *)pstAddIndication)->u32SFID);
		uiSearchRuleIndex = SearchSfid(Adapter, ulSFID);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "DSD - Removing connection %x", uiSearchRuleIndex);

		if (uiSearchRuleIndex < NO_OF_QUEUES) {
			/* Delete All Classifiers Associated with this SFID */
			deleteSFBySfid(Adapter, uiSearchRuleIndex);
			Adapter->u32TotalDSD++;
		}

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SENDING DSD RESPONSE TO MAC");
		((stLocalSFDeleteIndication *)&(Adapter->caDsxReqResp[LEADER_SIZE]))->u8Type = DSD_RSP;
		CopyBufferToControlPacket(Adapter, (PVOID)Adapter->caDsxReqResp);
	}
	case DSD_RSP:
	{
		/* Do nothing as SF has already got Deleted */
	}
	break;
	case DSD_ACK:
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "DSD ACK Rcd, let App handle it\n");
		break;
	default:
		kfree(pstAddIndication);
		return FALSE;
	}
	return TRUE;
}

int get_dsx_sf_data_to_application(struct bcm_mini_adapter *Adapter, UINT uiSFId, void __user *user_buffer)
{
	int status = 0;
	struct bcm_packet_info *psSfInfo = NULL;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "status =%d", status);
	status = SearchSfid(Adapter, uiSFId);
	if (status >= NO_OF_QUEUES) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SFID %d not present in queue !!!", uiSFId);
		return -EINVAL;
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "status =%d", status);
	psSfInfo = &Adapter->PackInfo[status];
	if (psSfInfo->pstSFIndication && copy_to_user(user_buffer,
							psSfInfo->pstSFIndication, sizeof(stLocalSFAddIndicationAlt))) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "copy to user failed SFID %d, present in queue !!!", uiSFId);
		status = -EFAULT;
		return status;
	}
	return STATUS_SUCCESS;
}

VOID OverrideServiceFlowParams(struct bcm_mini_adapter *Adapter, PUINT puiBuffer)
{
	B_UINT32 u32NumofSFsinMsg = ntohl(*(puiBuffer + 1));
	stIM_SFHostNotify *pHostInfo = NULL;
	UINT uiSearchRuleIndex = 0;
	ULONG ulSFID = 0;

	puiBuffer += 2;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "u32NumofSFsinMsg: 0x%x\n", u32NumofSFsinMsg);

	while (u32NumofSFsinMsg != 0 && u32NumofSFsinMsg < NO_OF_QUEUES) {
		u32NumofSFsinMsg--;
		pHostInfo = (stIM_SFHostNotify *)puiBuffer;
		puiBuffer = (PUINT)(pHostInfo + 1);

		ulSFID = ntohl(pHostInfo->SFID);
		uiSearchRuleIndex = SearchSfid(Adapter, ulSFID);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "SFID: 0x%lx\n", ulSFID);

		if (uiSearchRuleIndex >= NO_OF_QUEUES || uiSearchRuleIndex == HiPriority) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "The SFID <%lx> doesn't exist in host entry or is Invalid\n", ulSFID);
			continue;
		}

		if (pHostInfo->RetainSF == FALSE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "Going to Delete SF");
			deleteSFBySfid(Adapter, uiSearchRuleIndex);
		} else {
			Adapter->PackInfo[uiSearchRuleIndex].usVCID_Value = ntohs(pHostInfo->VCID);
			Adapter->PackInfo[uiSearchRuleIndex].usCID = ntohs(pHostInfo->newCID);
			Adapter->PackInfo[uiSearchRuleIndex].bActive = FALSE;

			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG, DBG_LVL_ALL, "pHostInfo->QoSParamSet: 0x%x\n", pHostInfo->QoSParamSet);

			if (pHostInfo->QoSParamSet & 0x1)
				Adapter->PackInfo[uiSearchRuleIndex].bAuthorizedSet = TRUE;
			if (pHostInfo->QoSParamSet & 0x2)
				Adapter->PackInfo[uiSearchRuleIndex].bAdmittedSet = TRUE;
			if (pHostInfo->QoSParamSet & 0x4) {
				Adapter->PackInfo[uiSearchRuleIndex].bActiveSet = TRUE;
				Adapter->PackInfo[uiSearchRuleIndex].bActive = TRUE;
			}
		}
	}
}
