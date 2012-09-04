#include "headers.h"

static BOOLEAN MatchSrcIpv6Address(struct bcm_classifier_rule *pstClassifierRule,
	IPV6Header *pstIpv6Header);
static BOOLEAN MatchDestIpv6Address(struct bcm_classifier_rule *pstClassifierRule,
	IPV6Header *pstIpv6Header);
static VOID DumpIpv6Header(IPV6Header *pstIpv6Header);

static UCHAR *GetNextIPV6ChainedHeader(UCHAR **ppucPayload,
	UCHAR *pucNextHeader, BOOLEAN *bParseDone, USHORT *pusPayloadLength)
{
	UCHAR *pucRetHeaderPtr = NULL;
	UCHAR *pucPayloadPtr = NULL;
	USHORT  usNextHeaderOffset = 0 ;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	if ((ppucPayload == NULL) || (*pusPayloadLength == 0) ||
		(*bParseDone)) {
		*bParseDone = TRUE;
		return NULL;
	}

	pucRetHeaderPtr = *ppucPayload;
	pucPayloadPtr = *ppucPayload;

	if (!pucRetHeaderPtr || !pucPayloadPtr) {
		*bParseDone = TRUE;
		return NULL;
	}

	/* Get the Nextt Header Type */
	*bParseDone = FALSE;


	switch (*pucNextHeader) {
	case IPV6HDR_TYPE_HOPBYHOP:
		{

			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 HopByHop Header");
			usNextHeaderOffset += sizeof(IPV6HopByHopOptionsHeader);
		}
		break;

	case IPV6HDR_TYPE_ROUTING:
		{
			IPV6RoutingHeader *pstIpv6RoutingHeader;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 Routing Header");
			pstIpv6RoutingHeader = (IPV6RoutingHeader *)pucPayloadPtr;
			usNextHeaderOffset += sizeof(IPV6RoutingHeader);
			usNextHeaderOffset += pstIpv6RoutingHeader->ucNumAddresses * IPV6_ADDRESS_SIZEINBYTES;

		}
		break;
	case IPV6HDR_TYPE_FRAGMENTATION:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL,
					"\nIPv6 Fragmentation Header");
			usNextHeaderOffset += sizeof(IPV6FragmentHeader);

		}
		break;
	case IPV6HDR_TYPE_DESTOPTS:
		{
			IPV6DestOptionsHeader *pstIpv6DestOptsHdr = (IPV6DestOptionsHeader *)pucPayloadPtr;
			int nTotalOptions = pstIpv6DestOptsHdr->ucHdrExtLen;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL,
					"\nIPv6 DestOpts Header Header");
			usNextHeaderOffset += sizeof(IPV6DestOptionsHeader);
			usNextHeaderOffset += nTotalOptions * IPV6_DESTOPTS_HDR_OPTIONSIZE ;

		}
		break;
	case IPV6HDR_TYPE_AUTHENTICATION:
		{
			IPV6AuthenticationHeader *pstIpv6AuthHdr = (IPV6AuthenticationHeader *)pucPayloadPtr;
			int nHdrLen = pstIpv6AuthHdr->ucLength;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL,
					"\nIPv6 Authentication Header");
			usNextHeaderOffset += nHdrLen * 4;
		}
		break;
	case IPV6HDR_TYPE_ENCRYPTEDSECURITYPAYLOAD:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL,
					"\nIPv6 Encrypted Security Payload Header");
			*bParseDone = TRUE;

		}
		break;
	case IPV6_ICMP_HDR_TYPE:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nICMP Header");
			*bParseDone = TRUE;
		}
		break;
	case TCP_HEADER_TYPE:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nTCP Header");
			*bParseDone = TRUE;
		}
		break;
	case UDP_HEADER_TYPE:
		{
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nUDP Header");
			*bParseDone = TRUE;
		}
		break;
	default:
		{
			*bParseDone = TRUE;

		}
		break;


	}

	if (*bParseDone == FALSE) {
		if (*pusPayloadLength <= usNextHeaderOffset) {
			*bParseDone = TRUE;
		} else {
			*pucNextHeader = *pucPayloadPtr;
			pucPayloadPtr += usNextHeaderOffset;
			(*pusPayloadLength) -= usNextHeaderOffset;
		}

	}

	*ppucPayload = pucPayloadPtr;
	return pucRetHeaderPtr;
}


static UCHAR GetIpv6ProtocolPorts(UCHAR *pucPayload, USHORT *pusSrcPort,
	USHORT *pusDestPort, USHORT usPayloadLength, UCHAR ucNextHeader)
{
	UCHAR *pIpv6HdrScanContext = pucPayload;
	BOOLEAN bDone = FALSE;
	UCHAR ucHeaderType = 0;
	UCHAR *pucNextHeader = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	if (!pucPayload || (usPayloadLength == 0))
		return 0;

	*pusSrcPort = *pusDestPort = 0;
	ucHeaderType = ucNextHeader;
	while (!bDone) {
		pucNextHeader = GetNextIPV6ChainedHeader(&pIpv6HdrScanContext,
					&ucHeaderType, &bDone, &usPayloadLength);
		if (bDone) {
			if ((ucHeaderType == TCP_HEADER_TYPE) ||
				(ucHeaderType == UDP_HEADER_TYPE)) {
				*pusSrcPort = *((PUSHORT)(pucNextHeader));
				*pusDestPort = *((PUSHORT)(pucNextHeader+2));
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
						DBG_LVL_ALL,
						"\nProtocol Ports - Src Port :0x%x Dest Port : 0x%x",
						ntohs(*pusSrcPort),
						ntohs(*pusDestPort));
			}
			break;

		}
	}
	return ucHeaderType;
}


/*
 * Arg 1 struct bcm_mini_adapter *Adapter is a pointer ot the driver contorl structure
 * Arg 2 PVOID pcIpHeader is a pointer to the IP header of the packet
 */
USHORT	IpVersion6(struct bcm_mini_adapter *Adapter, PVOID pcIpHeader,
					struct bcm_classifier_rule *pstClassifierRule)
{
	USHORT	ushDestPort = 0;
	USHORT	ushSrcPort = 0;
	UCHAR   ucNextProtocolAboveIP = 0;
	IPV6Header *pstIpv6Header = NULL;
	BOOLEAN bClassificationSucceed = FALSE;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
			DBG_LVL_ALL, "IpVersion6 ==========>\n");

	pstIpv6Header = (IPV6Header *)pcIpHeader;

	DumpIpv6Header(pstIpv6Header);

	/*
	 * Try to get the next higher layer protocol
	 * and the Ports Nos if TCP or UDP
	 */
	ucNextProtocolAboveIP = GetIpv6ProtocolPorts((UCHAR *)(pcIpHeader + sizeof(IPV6Header)),
							&ushSrcPort,
							&ushDestPort,
							pstIpv6Header->usPayloadLength,
							pstIpv6Header->ucNextHeader);

	do {
		if (pstClassifierRule->ucDirection == 0) {
			/*
			 * cannot be processed for classification.
			 * it is a down link connection
			 */
			break;
		}

		if (!pstClassifierRule->bIpv6Protocol) {
			/*
			 * We are looking for Ipv6 Classifiers
			 * Lets ignore this classifier and try the next one
			 */
			break;
		}

		bClassificationSucceed = MatchSrcIpv6Address(pstClassifierRule,
								pstIpv6Header);
		if (!bClassificationSucceed)
			break;

		bClassificationSucceed = MatchDestIpv6Address(pstClassifierRule,
								pstIpv6Header);
		if (!bClassificationSucceed)
			break;

		/*
		 * Match the protocol type.
		 * For IPv6 the next protocol at end of
		 * Chain of IPv6 prot headers
		 */
		bClassificationSucceed = MatchProtocol(pstClassifierRule,
							ucNextProtocolAboveIP);
		if (!bClassificationSucceed)
			break;

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
				DBG_LVL_ALL, "\nIPv6 Protocol Matched");

		if ((ucNextProtocolAboveIP == TCP_HEADER_TYPE) ||
			(ucNextProtocolAboveIP == UDP_HEADER_TYPE)) {
			/* Match Src Port */
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 Source Port:%x\n",
					ntohs(ushSrcPort));
			bClassificationSucceed = MatchSrcPort(pstClassifierRule,
							ntohs(ushSrcPort));
			if (!bClassificationSucceed)
				break;

			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 Src Port Matched");

			/* Match Dest Port */
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 Destination Port:%x\n",
					ntohs(ushDestPort));
			bClassificationSucceed = MatchDestPort(pstClassifierRule,
							ntohs(ushDestPort));
			if (!bClassificationSucceed)
				break;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
					DBG_LVL_ALL, "\nIPv6 Dest Port Matched");
		}
	} while (0);

	if (bClassificationSucceed == TRUE) {
		INT iMatchedSFQueueIndex = 0;
		iMatchedSFQueueIndex = SearchSfid(Adapter, pstClassifierRule->ulSFID);
		if (iMatchedSFQueueIndex >= NO_OF_QUEUES) {
			bClassificationSucceed = FALSE;
		} else {
			if (Adapter->PackInfo[iMatchedSFQueueIndex].bActive == FALSE)
				bClassificationSucceed = FALSE;
		}
	}

	return bClassificationSucceed;
}


static BOOLEAN MatchSrcIpv6Address(struct bcm_classifier_rule *pstClassifierRule,
	IPV6Header *pstIpv6Header)
{
	UINT uiLoopIndex = 0;
	UINT uiIpv6AddIndex = 0;
	UINT uiIpv6AddrNoLongWords = 4;
	ULONG aulSrcIP[4];
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	/*
	 * This is the no. of Src Addresses ie Range of IP Addresses contained
	 * in the classifier rule for which we need to match
	 */
	UINT  uiCountIPSrcAddresses = (UINT)pstClassifierRule->ucIPSourceAddressLength;


	if (uiCountIPSrcAddresses == 0)
		return TRUE;


	/* First Convert the Ip Address in the packet to Host Endian order */
	for (uiIpv6AddIndex = 0; uiIpv6AddIndex < uiIpv6AddrNoLongWords; uiIpv6AddIndex++)
		aulSrcIP[uiIpv6AddIndex] = ntohl(pstIpv6Header->ulSrcIpAddress[uiIpv6AddIndex]);

	for (uiLoopIndex = 0; uiLoopIndex < uiCountIPSrcAddresses; uiLoopIndex += uiIpv6AddrNoLongWords) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Src Ipv6 Address In Received Packet :\n ");
		DumpIpv6Address(aulSrcIP);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Src Ipv6 Mask In Classifier Rule:\n");
		DumpIpv6Address(&pstClassifierRule->stSrcIpAddress.ulIpv6Mask[uiLoopIndex]);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Src Ipv6 Address In Classifier Rule :\n");
		DumpIpv6Address(&pstClassifierRule->stSrcIpAddress.ulIpv6Addr[uiLoopIndex]);

		for (uiIpv6AddIndex = 0; uiIpv6AddIndex < uiIpv6AddrNoLongWords; uiIpv6AddIndex++) {
			if ((pstClassifierRule->stSrcIpAddress.ulIpv6Mask[uiLoopIndex+uiIpv6AddIndex] & aulSrcIP[uiIpv6AddIndex])
				!= pstClassifierRule->stSrcIpAddress.ulIpv6Addr[uiLoopIndex+uiIpv6AddIndex]) {
				/*
				 * Match failed for current Ipv6 Address
				 * Try next Ipv6 Address
				 */
				break;
			}

			if (uiIpv6AddIndex ==  uiIpv6AddrNoLongWords-1) {
				/* Match Found */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
						DBG_LVL_ALL,
						"Ipv6 Src Ip Address Matched\n");
				return TRUE;
			}
		}
	}
	return FALSE;
}

static BOOLEAN MatchDestIpv6Address(struct bcm_classifier_rule *pstClassifierRule,
	IPV6Header *pstIpv6Header)
{
	UINT uiLoopIndex = 0;
	UINT uiIpv6AddIndex = 0;
	UINT uiIpv6AddrNoLongWords = 4;
	ULONG aulDestIP[4];
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	/*
	 * This is the no. of Destination Addresses
	 * ie Range of IP Addresses contained in the classifier rule
	 * for which we need to match
	 */
	UINT  uiCountIPDestinationAddresses = (UINT)pstClassifierRule->ucIPDestinationAddressLength;


	if (uiCountIPDestinationAddresses == 0)
		return TRUE;


	/* First Convert the Ip Address in the packet to Host Endian order */
	for (uiIpv6AddIndex = 0; uiIpv6AddIndex < uiIpv6AddrNoLongWords; uiIpv6AddIndex++)
		aulDestIP[uiIpv6AddIndex] = ntohl(pstIpv6Header->ulDestIpAddress[uiIpv6AddIndex]);

	for (uiLoopIndex = 0; uiLoopIndex < uiCountIPDestinationAddresses; uiLoopIndex += uiIpv6AddrNoLongWords) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Destination Ipv6 Address In Received Packet :\n ");
		DumpIpv6Address(aulDestIP);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Destination Ipv6 Mask In Classifier Rule :\n");
		DumpIpv6Address(&pstClassifierRule->stDestIpAddress.ulIpv6Mask[uiLoopIndex]);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				"\n Destination Ipv6 Address In Classifier Rule :\n");
		DumpIpv6Address(&pstClassifierRule->stDestIpAddress.ulIpv6Addr[uiLoopIndex]);

		for (uiIpv6AddIndex = 0; uiIpv6AddIndex < uiIpv6AddrNoLongWords; uiIpv6AddIndex++) {
			if ((pstClassifierRule->stDestIpAddress.ulIpv6Mask[uiLoopIndex+uiIpv6AddIndex] & aulDestIP[uiIpv6AddIndex])
				!= pstClassifierRule->stDestIpAddress.ulIpv6Addr[uiLoopIndex+uiIpv6AddIndex]) {
				/*
				 * Match failed for current Ipv6 Address.
				 * Try next Ipv6 Address
				 */
				break;
			}

			if (uiIpv6AddIndex ==  uiIpv6AddrNoLongWords-1) {
				/* Match Found */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG,
						DBG_LVL_ALL,
						"Ipv6 Destination Ip Address Matched\n");
				return TRUE;
			}
		}
	}
	return FALSE;

}

VOID DumpIpv6Address(ULONG *puIpv6Address)
{
	UINT uiIpv6AddrNoLongWords = 4;
	UINT uiIpv6AddIndex = 0;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	for (uiIpv6AddIndex = 0; uiIpv6AddIndex < uiIpv6AddrNoLongWords; uiIpv6AddIndex++) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
				":%lx", puIpv6Address[uiIpv6AddIndex]);
	}

}

static VOID DumpIpv6Header(IPV6Header *pstIpv6Header)
{
	UCHAR ucVersion;
	UCHAR ucPrio;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"----Ipv6 Header---");
	ucVersion = pstIpv6Header->ucVersionPrio & 0xf0;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Version : %x\n", ucVersion);
	ucPrio = pstIpv6Header->ucVersionPrio & 0x0f;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Priority : %x\n", ucPrio);
	/*
	 * BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
	 * "Flow Label : %x\n",(pstIpv6Header->ucVersionPrio &0xf0);
	 */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Payload Length : %x\n",
			ntohs(pstIpv6Header->usPayloadLength));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Next Header : %x\n", pstIpv6Header->ucNextHeader);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Hop Limit : %x\n", pstIpv6Header->ucHopLimit);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Src Address :\n");
	DumpIpv6Address(pstIpv6Header->ulSrcIpAddress);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"Dest Address :\n");
	DumpIpv6Address(pstIpv6Header->ulDestIpAddress);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV6_DBG, DBG_LVL_ALL,
			"----Ipv6 Header End---");


}
