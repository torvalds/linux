#include "headers.h"

static UINT CreateSFToClassifierRuleMapping(B_UINT16 uiVcid,
					    B_UINT16 uiClsId,
					    struct bcm_phs_table *psServiceFlowTable,
					    struct bcm_phs_rule *psPhsRule,
					    B_UINT8 u8AssociatedPHSI);

static UINT CreateClassiferToPHSRuleMapping(B_UINT16 uiVcid,
					    B_UINT16  uiClsId,
					    struct bcm_phs_entry *pstServiceFlowEntry,
					    struct bcm_phs_rule *psPhsRule,
					    B_UINT8 u8AssociatedPHSI);

static UINT CreateClassifierPHSRule(B_UINT16  uiClsId,
				    struct bcm_phs_classifier_table *psaClassifiertable,
				    struct bcm_phs_rule *psPhsRule,
				    enum bcm_phs_classifier_context eClsContext,
				    B_UINT8 u8AssociatedPHSI);

static UINT UpdateClassifierPHSRule(B_UINT16 uiClsId,
				    struct bcm_phs_classifier_entry *pstClassifierEntry,
				    struct bcm_phs_classifier_table *psaClassifiertable,
				    struct bcm_phs_rule *psPhsRule,
				    B_UINT8 u8AssociatedPHSI);

static bool ValidatePHSRuleComplete(const struct bcm_phs_rule *psPhsRule);

static bool DerefPhsRule(B_UINT16 uiClsId,
			 struct bcm_phs_classifier_table *psaClassifiertable,
			 struct bcm_phs_rule *pstPhsRule);

static UINT GetClassifierEntry(struct bcm_phs_classifier_table *pstClassifierTable,
			       B_UINT32 uiClsid,
			       enum bcm_phs_classifier_context eClsContext,
			       struct bcm_phs_classifier_entry **ppstClassifierEntry);

static UINT GetPhsRuleEntry(struct bcm_phs_classifier_table *pstClassifierTable,
			    B_UINT32 uiPHSI,
			    enum bcm_phs_classifier_context eClsContext,
			    struct bcm_phs_rule **ppstPhsRule);

static void free_phs_serviceflow_rules(struct bcm_phs_table *psServiceFlowRulesTable);

static int phs_compress(struct bcm_phs_rule *phs_members,
			unsigned char *in_buf,
			unsigned char *out_buf,
			unsigned int *header_size,
			UINT *new_header_size);

static int verify_suppress_phsf(unsigned char *in_buffer,
				unsigned char *out_buffer,
				unsigned char *phsf,
				unsigned char *phsm,
				unsigned int phss,
				unsigned int phsv,
				UINT *new_header_size);

static int phs_decompress(unsigned char *in_buf,
			  unsigned char *out_buf,
			  struct bcm_phs_rule *phs_rules,
			  UINT *header_size);

static ULONG PhsCompress(void *pvContext,
			 B_UINT16 uiVcid,
			 B_UINT16 uiClsId,
			 void *pvInputBuffer,
			 void *pvOutputBuffer,
			 UINT *pOldHeaderSize,
			 UINT *pNewHeaderSize);

static ULONG PhsDeCompress(void *pvContext,
			   B_UINT16 uiVcid,
			   void *pvInputBuffer,
			   void *pvOutputBuffer,
			   UINT *pInHeaderSize,
			   UINT *pOutHeaderSize);

#define IN
#define OUT

/*
 * Function: PHSTransmit
 * Description:	This routine handle PHS(Payload Header Suppression for Tx path.
 *	It extracts a fragment of the NDIS_PACKET containing the header
 *	to be suppressed. It then suppresses the header by invoking PHS exported compress routine.
 *	The header data after suppression is copied back to the NDIS_PACKET.
 *
 * Input parameters: IN struct bcm_mini_adapter *Adapter         - Miniport Adapter Context
 *	IN Packet - NDIS packet containing data to be transmitted
 *	IN USHORT Vcid - vcid pertaining to connection on which the packet is being sent.Used to
 *		identify PHS rule to be applied.
 *	B_UINT16 uiClassifierRuleID - Classifier Rule ID
 *	BOOLEAN bHeaderSuppressionEnabled - indicates if header suprression is enabled for SF.
 *
 * Return:	STATUS_SUCCESS - If the send was successful.
 *	Other  - If an error occurred.
 */

int PHSTransmit(struct bcm_mini_adapter *Adapter,
		struct sk_buff **pPacket,
		USHORT Vcid,
		B_UINT16 uiClassifierRuleID,
		bool bHeaderSuppressionEnabled,
		UINT *PacketLen,
		UCHAR bEthCSSupport)
{
	/* PHS Sepcific */
	UINT unPHSPktHdrBytesCopied = 0;
	UINT unPhsOldHdrSize = 0;
	UINT unPHSNewPktHeaderLen = 0;
	/* Pointer to PHS IN Hdr Buffer */
	PUCHAR pucPHSPktHdrInBuf = Adapter->stPhsTxContextInfo.ucaHdrSuppressionInBuf;
	/* Pointer to PHS OUT Hdr Buffer */
	PUCHAR pucPHSPktHdrOutBuf = Adapter->stPhsTxContextInfo.ucaHdrSuppressionOutBuf;
	UINT usPacketType;
	UINT BytesToRemove = 0;
	bool bPHSI = 0;
	LONG ulPhsStatus = 0;
	UINT numBytesCompressed = 0;
	struct sk_buff *newPacket = NULL;
	struct sk_buff *Packet = *pPacket;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
			"In PHSTransmit");

	if (!bEthCSSupport)
		BytesToRemove = ETH_HLEN;
	/*
	 * Accumulate the header upto the size we support suppression
	 * from NDIS packet
	 */

	usPacketType = ((struct ethhdr *)(Packet->data))->h_proto;

	pucPHSPktHdrInBuf = Packet->data + BytesToRemove;
	/* considering data after ethernet header */
	if ((*PacketLen - BytesToRemove) < MAX_PHS_LENGTHS)
		unPHSPktHdrBytesCopied = (*PacketLen - BytesToRemove);
	else
		unPHSPktHdrBytesCopied = MAX_PHS_LENGTHS;

	if ((unPHSPktHdrBytesCopied > 0) &&
		(unPHSPktHdrBytesCopied <= MAX_PHS_LENGTHS)) {

		/*
		 * Step 2 Suppress Header using PHS and fill into intermediate ucaPHSPktHdrOutBuf.
		 * Suppress only if IP Header and PHS Enabled For the Service Flow
		 */
		if (((usPacketType == ETHERNET_FRAMETYPE_IPV4) ||
				(usPacketType == ETHERNET_FRAMETYPE_IPV6)) &&
			(bHeaderSuppressionEnabled)) {

			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND,
					DBG_LVL_ALL,
					"\nTrying to PHS Compress Using Classifier rule 0x%X",
					uiClassifierRuleID);
			unPHSNewPktHeaderLen = unPHSPktHdrBytesCopied;
			ulPhsStatus = PhsCompress(&Adapter->stBCMPhsContext,
						  Vcid,
						  uiClassifierRuleID,
						  pucPHSPktHdrInBuf,
						  pucPHSPktHdrOutBuf,
						  &unPhsOldHdrSize,
						  &unPHSNewPktHeaderLen);
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND,
					DBG_LVL_ALL,
					"\nPHS Old header Size : %d New Header Size  %d\n",
					unPhsOldHdrSize, unPHSNewPktHeaderLen);

			if (unPHSNewPktHeaderLen == unPhsOldHdrSize) {

				if (ulPhsStatus == STATUS_PHS_COMPRESSED)
					bPHSI = *pucPHSPktHdrOutBuf;

				ulPhsStatus = STATUS_PHS_NOCOMPRESSION;
			}

			if (ulPhsStatus == STATUS_PHS_COMPRESSED) {

				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS,
						PHS_SEND, DBG_LVL_ALL,
						"PHS Sending packet Compressed");

				if (skb_cloned(Packet)) {
					newPacket = skb_copy(Packet, GFP_ATOMIC);

					if (newPacket == NULL)
						return STATUS_FAILURE;

					dev_kfree_skb(Packet);
					*pPacket = Packet = newPacket;
					pucPHSPktHdrInBuf =
						Packet->data + BytesToRemove;
				}

				numBytesCompressed = unPhsOldHdrSize -
					(unPHSNewPktHeaderLen + PHSI_LEN);

				memcpy(pucPHSPktHdrInBuf + numBytesCompressed,
				       pucPHSPktHdrOutBuf,
				       unPHSNewPktHeaderLen + PHSI_LEN);
				memcpy(Packet->data + numBytesCompressed,
				       Packet->data, BytesToRemove);
				skb_pull(Packet, numBytesCompressed);

				return STATUS_SUCCESS;
			} else {
				/* if one byte headroom is not available,
				 * increase it through skb_cow
				 */
				if (!(skb_headroom(Packet) > 0)) {

					if (skb_cow(Packet, 1)) {
						BCM_DEBUG_PRINT(Adapter,
								DBG_TYPE_PRINTK,
								0, 0,
								"SKB Cow Failed\n");
						return STATUS_FAILURE;
					}
				}
				skb_push(Packet, 1);

				/*
				 * CAUTION: The MAC Header is getting corrupted
				 * here for IP CS - can be saved by copying 14
				 * Bytes.  not needed .... hence corrupting it.
				 */
				*(Packet->data + BytesToRemove) = bPHSI;
				return STATUS_SUCCESS;
			}
		} else {

			if (!bHeaderSuppressionEnabled)
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS,
						PHS_SEND, DBG_LVL_ALL,
						"\nHeader Suppression Disabled For SF: No PHS\n");

			return STATUS_SUCCESS;
		}
	}

	/* BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,"PHSTransmit : Dumping data packet After PHS"); */
	return STATUS_SUCCESS;
}

int PHSReceive(struct bcm_mini_adapter *Adapter,
	       USHORT usVcid,
	       struct sk_buff *packet,
	       UINT *punPacketLen,
	       UCHAR *pucEthernetHdr,
	       UINT bHeaderSuppressionEnabled)
{
	u32 nStandardPktHdrLen = 0;
	u32 nTotalsuppressedPktHdrBytes = 0;
	int ulPhsStatus	= 0;
	PUCHAR pucInBuff = NULL;
	UINT TotalBytesAdded = 0;

	if (!bHeaderSuppressionEnabled) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE,
				DBG_LVL_ALL,
				"\nPhs Disabled for incoming packet");
		return ulPhsStatus;
	}

	pucInBuff = packet->data;

	/* Restore PHS suppressed header */
	nStandardPktHdrLen = packet->len;
	ulPhsStatus = PhsDeCompress(&Adapter->stBCMPhsContext,
				    usVcid,
				    pucInBuff,
				    Adapter->ucaPHSPktRestoreBuf,
				    &nTotalsuppressedPktHdrBytes,
				    &nStandardPktHdrLen);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE, DBG_LVL_ALL,
			"\nSuppressed PktHdrLen : 0x%x Restored PktHdrLen : 0x%x",
			nTotalsuppressedPktHdrBytes, nStandardPktHdrLen);

	if (ulPhsStatus != STATUS_PHS_COMPRESSED) {
		skb_pull(packet, 1);
		return STATUS_SUCCESS;
	} else {
		TotalBytesAdded = nStandardPktHdrLen -
			nTotalsuppressedPktHdrBytes - PHSI_LEN;

		if (TotalBytesAdded) {
			if (skb_headroom(packet) >= (SKB_RESERVE_ETHERNET_HEADER + TotalBytesAdded))
				skb_push(packet, TotalBytesAdded);
			else {
				if (skb_cow(packet, skb_headroom(packet) + TotalBytesAdded)) {
					BCM_DEBUG_PRINT(Adapter,
							DBG_TYPE_PRINTK, 0, 0,
							"cow failed in receive\n");
					return STATUS_FAILURE;
				}

				skb_push(packet, TotalBytesAdded);
			}
		}

		memcpy(packet->data, Adapter->ucaPHSPktRestoreBuf,
		       nStandardPktHdrLen);
	}

	return STATUS_SUCCESS;
}

void DumpFullPacket(UCHAR *pBuf, UINT nPktLen)
{
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, IPV4_DBG, DBG_LVL_ALL,
			"Dumping Data Packet");
	BCM_DEBUG_PRINT_BUFFER(Adapter, DBG_TYPE_TX, IPV4_DBG, DBG_LVL_ALL,
			       pBuf, nPktLen);
}

/*
 * Procedure:   phs_init
 *
 * Description: This routine is responsible for allocating memory for classifier and
 * PHS rules.
 *
 * Arguments:
 * pPhsdeviceExtension - ptr to Device extension containing PHS Classifier rules and PHS Rules , RX, TX buffer etc
 *
 * Returns:
 * TRUE(1)	-If allocation of memory was successful.
 * FALSE	-If allocation of memory fails.
 */
int phs_init(struct bcm_phs_extension *pPhsdeviceExtension,
	     struct bcm_mini_adapter *Adapter)
{
	int i;
	struct bcm_phs_table *pstServiceFlowTable;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"\nPHS:phs_init function");

	if (pPhsdeviceExtension->pstServiceFlowPhsRulesTable)
		return -EINVAL;

	pPhsdeviceExtension->pstServiceFlowPhsRulesTable =
		kzalloc(sizeof(struct bcm_phs_table), GFP_KERNEL);

	if (!pPhsdeviceExtension->pstServiceFlowPhsRulesTable) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL,
				"\nAllocation ServiceFlowPhsRulesTable failed");
		return -ENOMEM;
	}

	pstServiceFlowTable = pPhsdeviceExtension->pstServiceFlowPhsRulesTable;
	for (i = 0; i < MAX_SERVICEFLOWS; i++) {
		struct bcm_phs_entry sServiceFlow =
			pstServiceFlowTable->stSFList[i];
		sServiceFlow.pstClassifierTable =
			kzalloc(sizeof(struct bcm_phs_classifier_table),
				GFP_KERNEL);
		if (!sServiceFlow.pstClassifierTable) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
					DBG_LVL_ALL, "\nAllocation failed");
			free_phs_serviceflow_rules(pPhsdeviceExtension->pstServiceFlowPhsRulesTable);
			pPhsdeviceExtension->pstServiceFlowPhsRulesTable = NULL;
			return -ENOMEM;
		}
	}

	pPhsdeviceExtension->CompressedTxBuffer = kmalloc(PHS_BUFFER_SIZE, GFP_KERNEL);
	if (pPhsdeviceExtension->CompressedTxBuffer == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL, "\nAllocation failed");
		free_phs_serviceflow_rules(pPhsdeviceExtension->pstServiceFlowPhsRulesTable);
		pPhsdeviceExtension->pstServiceFlowPhsRulesTable = NULL;
		return -ENOMEM;
	}

	pPhsdeviceExtension->UnCompressedRxBuffer =
		kmalloc(PHS_BUFFER_SIZE, GFP_KERNEL);
	if (pPhsdeviceExtension->UnCompressedRxBuffer == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL, "\nAllocation failed");
		kfree(pPhsdeviceExtension->CompressedTxBuffer);
		free_phs_serviceflow_rules(pPhsdeviceExtension->pstServiceFlowPhsRulesTable);
		pPhsdeviceExtension->pstServiceFlowPhsRulesTable = NULL;
		return -ENOMEM;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"\n phs_init Successful");
	return STATUS_SUCCESS;
}

int PhsCleanup(IN struct bcm_phs_extension *pPHSDeviceExt)
{
	if (pPHSDeviceExt->pstServiceFlowPhsRulesTable) {
		free_phs_serviceflow_rules(pPHSDeviceExt->pstServiceFlowPhsRulesTable);
		pPHSDeviceExt->pstServiceFlowPhsRulesTable = NULL;
	}

	kfree(pPHSDeviceExt->CompressedTxBuffer);
	pPHSDeviceExt->CompressedTxBuffer = NULL;

	kfree(pPHSDeviceExt->UnCompressedRxBuffer);
	pPHSDeviceExt->UnCompressedRxBuffer = NULL;

	return 0;
}

/*
 * PHS functions
 * PhsUpdateClassifierRule
 *
 * Routine Description:
 *   Exported function to add or modify a PHS Rule.
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context
 *	IN B_UINT16 uiVcid    - The Service Flow ID for which the PHS rule applies
 *	IN B_UINT16  uiClsId   - The Classifier ID within the Service Flow for which the PHS rule applies.
 *	IN struct bcm_phs_rule *psPhsRule - The PHS Rule strcuture to be added to the PHS Rule table.
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
ULONG PhsUpdateClassifierRule(IN void *pvContext,
			      IN B_UINT16 uiVcid ,
			      IN B_UINT16 uiClsId   ,
			      IN struct bcm_phs_rule *psPhsRule,
			      IN B_UINT8 u8AssociatedPHSI)
{
	ULONG lStatus = 0;
	UINT nSFIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension = (struct bcm_phs_extension *)pvContext;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"PHS With Corr2 Changes\n");

	if (pDeviceExtension == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL, "Invalid Device Extension\n");
		return ERR_PHS_INVALID_DEVICE_EXETENSION;
	}

	if (u8AssociatedPHSI == 0)
		return ERR_PHS_INVALID_PHS_RULE;

	/* Retrieve the SFID Entry Index for requested Service Flow */
	nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
				       uiVcid, &pstServiceFlowEntry);

	if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
		/* This is a new SF. Create a mapping entry for this */
		lStatus = CreateSFToClassifierRuleMapping(uiVcid, uiClsId,
							  pDeviceExtension->pstServiceFlowPhsRulesTable,
							  psPhsRule,
							  u8AssociatedPHSI);
		return lStatus;
	}

	/* SF already Exists Add PHS Rule to existing SF */
	lStatus = CreateClassiferToPHSRuleMapping(uiVcid, uiClsId,
						  pstServiceFlowEntry,
						  psPhsRule,
						  u8AssociatedPHSI);

	return lStatus;
}

/*
 * PhsDeletePHSRule
 *
 * Routine Description:
 *   Deletes the specified phs Rule within Vcid
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context
 *	IN B_UINT16  uiVcid    - The Service Flow ID for which the PHS rule applies
 *	IN B_UINT8  u8PHSI   - the PHS Index identifying PHS rule to be deleted.
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
ULONG PhsDeletePHSRule(IN void *pvContext,
		       IN B_UINT16 uiVcid,
		       IN B_UINT8 u8PHSI)
{
	UINT nSFIndex = 0, nClsidIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_phs_classifier_table *pstClassifierRulesTable = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension = (struct bcm_phs_extension *)pvContext;
	struct bcm_phs_classifier_entry *curr_entry;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"======>\n");

	if (pDeviceExtension) {
		/* Retrieve the SFID Entry Index for requested Service Flow */
		nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
					       uiVcid, &pstServiceFlowEntry);

		if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
					DBG_LVL_ALL, "SFID Match Failed\n");
			return ERR_SF_MATCH_FAIL;
		}

		pstClassifierRulesTable = pstServiceFlowEntry->pstClassifierTable;
		if (pstClassifierRulesTable) {
			for (nClsidIndex = 0; nClsidIndex < MAX_PHSRULE_PER_SF; nClsidIndex++) {
				curr_entry = &pstClassifierRulesTable->stActivePhsRulesList[nClsidIndex];
				if (curr_entry->bUsed &&
				    curr_entry->pstPhsRule &&
				    (curr_entry->pstPhsRule->u8PHSI == u8PHSI)) {

					if (curr_entry->pstPhsRule->u8RefCnt)
						curr_entry->pstPhsRule->u8RefCnt--;

					if (0 == curr_entry->pstPhsRule->u8RefCnt)
						kfree(curr_entry->pstPhsRule);

					memset(curr_entry,
					       0,
					       sizeof(struct bcm_phs_classifier_entry));
				}
			}
		}
	}
	return 0;
}

/*
 * PhsDeleteClassifierRule
 *
 * Routine Description:
 *    Exported function to Delete a PHS Rule for the SFID,CLSID Pair.
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context
 *	IN B_UINT16  uiVcid    - The Service Flow ID for which the PHS rule applies
 *	IN B_UINT16  uiClsId   - The Classifier ID within the Service Flow for which the PHS rule applies.
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
ULONG PhsDeleteClassifierRule(IN void *pvContext, IN B_UINT16 uiVcid, IN B_UINT16 uiClsId)
{
	UINT nSFIndex = 0, nClsidIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_phs_classifier_entry *pstClassifierEntry = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension =
		(struct bcm_phs_extension *)pvContext;

	if (!pDeviceExtension)
		goto out;

	/* Retrieve the SFID Entry Index for requested Service Flow */
	nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
				       uiVcid, &pstServiceFlowEntry);
	if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL, "SFID Match Failed\n");
		return ERR_SF_MATCH_FAIL;
	}

	nClsidIndex =
		GetClassifierEntry(pstServiceFlowEntry->pstClassifierTable,
				   uiClsId,
				   eActiveClassifierRuleContext,
				   &pstClassifierEntry);

	if ((nClsidIndex != PHS_INVALID_TABLE_INDEX) &&
			(!pstClassifierEntry->bUnclassifiedPHSRule)) {
		if (pstClassifierEntry->pstPhsRule) {
			if (pstClassifierEntry->pstPhsRule->u8RefCnt)
				pstClassifierEntry->pstPhsRule->u8RefCnt--;

			if (0 == pstClassifierEntry->pstPhsRule->u8RefCnt)
				kfree(pstClassifierEntry->pstPhsRule);
		}
		memset(pstClassifierEntry, 0,
		       sizeof(struct bcm_phs_classifier_entry));
	}

	nClsidIndex =
		GetClassifierEntry(pstServiceFlowEntry->pstClassifierTable,
				   uiClsId,
				   eOldClassifierRuleContext,
				   &pstClassifierEntry);

	if ((nClsidIndex != PHS_INVALID_TABLE_INDEX) &&
			(!pstClassifierEntry->bUnclassifiedPHSRule)) {
		kfree(pstClassifierEntry->pstPhsRule);
		memset(pstClassifierEntry, 0,
		       sizeof(struct bcm_phs_classifier_entry));
	}

out:
	return 0;
}

/*
 * PhsDeleteSFRules
 *
 * Routine Description:
 *    Exported function to Delete a all PHS Rules for the SFID.
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context
 *	IN B_UINT16 uiVcid   - The Service Flow ID for which the PHS rules need to be deleted
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
ULONG PhsDeleteSFRules(IN void *pvContext, IN B_UINT16 uiVcid)
{
	UINT nSFIndex = 0, nClsidIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_phs_classifier_table *pstClassifierRulesTable = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension =
		(struct bcm_phs_extension *)pvContext;
	struct bcm_phs_classifier_entry *curr_clsf_entry;
	struct bcm_phs_classifier_entry *curr_rules_list;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"====>\n");

	if (!pDeviceExtension)
		goto out;

	/* Retrieve the SFID Entry Index for requested Service Flow */
	nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
				       uiVcid, &pstServiceFlowEntry);
	if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL, "SFID Match Failed\n");
		return ERR_SF_MATCH_FAIL;
	}

	pstClassifierRulesTable = pstServiceFlowEntry->pstClassifierTable;
	if (pstClassifierRulesTable) {
		for (nClsidIndex = 0; nClsidIndex < MAX_PHSRULE_PER_SF; nClsidIndex++) {
			curr_clsf_entry =
				&pstClassifierRulesTable->stActivePhsRulesList[nClsidIndex];

			curr_rules_list =
				&pstClassifierRulesTable->stOldPhsRulesList[nClsidIndex];

			if (curr_clsf_entry->pstPhsRule) {

				if (curr_clsf_entry->pstPhsRule->u8RefCnt)
					curr_clsf_entry->pstPhsRule->u8RefCnt--;

				if (0 == curr_clsf_entry->pstPhsRule->u8RefCnt)
					kfree(curr_clsf_entry->pstPhsRule);

				curr_clsf_entry->pstPhsRule = NULL;
			}
			memset(curr_clsf_entry, 0,
			       sizeof(struct bcm_phs_classifier_entry));
			if (curr_rules_list->pstPhsRule) {

				if (curr_rules_list->pstPhsRule->u8RefCnt)
					curr_rules_list->pstPhsRule->u8RefCnt--;

				if (0 == curr_rules_list->pstPhsRule->u8RefCnt)
					kfree(curr_rules_list->pstPhsRule);

				curr_rules_list->pstPhsRule = NULL;
			}
			memset(curr_rules_list, 0,
			       sizeof(struct bcm_phs_classifier_entry));
		}
	}
	pstServiceFlowEntry->bUsed = false;
	pstServiceFlowEntry->uiVcid = 0;

out:
	return 0;
}

/*
 * PhsCompress
 *
 * Routine Description:
 *    Exported function to compress the data using PHS.
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context.
 *	IN B_UINT16 uiVcid    - The Service Flow ID to which current packet header compression applies.
 *	IN UINT  uiClsId   - The Classifier ID to which current packet header compression applies.
 *	IN void *pvInputBuffer - The Input buffer containg packet header data
 *	IN void *pvOutputBuffer - The output buffer returned by this function after PHS
 *	IN UINT *pOldHeaderSize  - The actual size of the header before PHS
 *	IN UINT *pNewHeaderSize - The new size of the header after applying PHS
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
static ULONG PhsCompress(IN void *pvContext,
			 IN B_UINT16 uiVcid,
			 IN B_UINT16 uiClsId,
			 IN void *pvInputBuffer,
			 OUT void *pvOutputBuffer,
			 OUT UINT *pOldHeaderSize,
			 OUT UINT *pNewHeaderSize)
{
	UINT nSFIndex = 0, nClsidIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_phs_classifier_entry *pstClassifierEntry = NULL;
	struct bcm_phs_rule *pstPhsRule = NULL;
	ULONG lStatus = 0;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension =
		(struct bcm_phs_extension *)pvContext;

	if (pDeviceExtension == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"Invalid Device Extension\n");
		lStatus = STATUS_PHS_NOCOMPRESSION;
		return lStatus;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
			"Suppressing header\n");

	/* Retrieve the SFID Entry Index for requested Service Flow */
	nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
				       uiVcid, &pstServiceFlowEntry);
	if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"SFID Match Failed\n");
		lStatus = STATUS_PHS_NOCOMPRESSION;
		return lStatus;
	}

	nClsidIndex = GetClassifierEntry(pstServiceFlowEntry->pstClassifierTable,
					 uiClsId, eActiveClassifierRuleContext,
					 &pstClassifierEntry);

	if (nClsidIndex == PHS_INVALID_TABLE_INDEX) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"No PHS Rule Defined For Classifier\n");
		lStatus =  STATUS_PHS_NOCOMPRESSION;
		return lStatus;
	}

	/* get rule from SF id,Cls ID pair and proceed */
	pstPhsRule = pstClassifierEntry->pstPhsRule;
	if (!ValidatePHSRuleComplete(pstPhsRule)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
				"PHS Rule Defined For Classifier But Not Complete\n");
		lStatus = STATUS_PHS_NOCOMPRESSION;
		return lStatus;
	}

	/* Compress Packet */
	lStatus = phs_compress(pstPhsRule,
			       (PUCHAR)pvInputBuffer,
			       (PUCHAR)pvOutputBuffer,
			       pOldHeaderSize,
			       pNewHeaderSize);

	if (lStatus == STATUS_PHS_COMPRESSED) {
		pstPhsRule->PHSModifiedBytes +=
			*pOldHeaderSize - *pNewHeaderSize - 1;
		pstPhsRule->PHSModifiedNumPackets++;
	} else {
		pstPhsRule->PHSErrorNumPackets++;
	}

	return lStatus;
}

/*
 * PhsDeCompress
 *
 * Routine Description:
 *    Exported function to restore the packet header in Rx path.
 *
 * Arguments:
 *	IN void* pvContext - PHS Driver Specific Context.
 *	IN B_UINT16 uiVcid    - The Service Flow ID to which current packet header restoration applies.
 *	IN  void *pvInputBuffer - The Input buffer containg suppressed packet header data
 *	OUT void *pvOutputBuffer - The output buffer returned by this function after restoration
 *	OUT UINT *pHeaderSize   - The packet header size after restoration is returned in this parameter.
 *
 * Return Value:
 *
 * 0 if successful,
 * >0 Error.
 */
static ULONG PhsDeCompress(IN void *pvContext,
			   IN B_UINT16 uiVcid,
			   IN void *pvInputBuffer,
			   OUT void *pvOutputBuffer,
			   OUT UINT *pInHeaderSize,
			   OUT UINT *pOutHeaderSize)
{
	UINT nSFIndex = 0, nPhsRuleIndex = 0;
	struct bcm_phs_entry *pstServiceFlowEntry = NULL;
	struct bcm_phs_rule *pstPhsRule = NULL;
	UINT phsi;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_extension *pDeviceExtension =
		(struct bcm_phs_extension *)pvContext;

	*pInHeaderSize = 0;
	if (pDeviceExtension == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE,
				DBG_LVL_ALL, "Invalid Device Extension\n");
		return ERR_PHS_INVALID_DEVICE_EXETENSION;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE, DBG_LVL_ALL,
			"Restoring header\n");

	phsi = *((unsigned char *)(pvInputBuffer));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE, DBG_LVL_ALL,
			"PHSI To Be Used For restore : %x\n", phsi);
	if (phsi == UNCOMPRESSED_PACKET)
		return STATUS_PHS_NOCOMPRESSION;

	/* Retrieve the SFID Entry Index for requested Service Flow */
	nSFIndex = GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
				       uiVcid, &pstServiceFlowEntry);
	if (nSFIndex == PHS_INVALID_TABLE_INDEX) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE,
				DBG_LVL_ALL,
				"SFID Match Failed During Lookup\n");
		return ERR_SF_MATCH_FAIL;
	}

	nPhsRuleIndex = GetPhsRuleEntry(pstServiceFlowEntry->pstClassifierTable,
					phsi,
					eActiveClassifierRuleContext,
					&pstPhsRule);
	if (nPhsRuleIndex == PHS_INVALID_TABLE_INDEX) {
		/* Phs Rule does not exist in  active rules table. Lets try in the old rules table. */
		nPhsRuleIndex = GetPhsRuleEntry(pstServiceFlowEntry->pstClassifierTable,
						phsi,
						eOldClassifierRuleContext,
						&pstPhsRule);
		if (nPhsRuleIndex == PHS_INVALID_TABLE_INDEX)
			return ERR_PHSRULE_MATCH_FAIL;
	}

	*pInHeaderSize = phs_decompress((PUCHAR)pvInputBuffer,
					(PUCHAR)pvOutputBuffer,
					pstPhsRule,
					pOutHeaderSize);

	pstPhsRule->PHSModifiedBytes += *pOutHeaderSize - *pInHeaderSize - 1;

	pstPhsRule->PHSModifiedNumPackets++;
	return STATUS_PHS_COMPRESSED;
}

/*
 * Procedure:   free_phs_serviceflow_rules
 *
 * Description: This routine is responsible for freeing memory allocated for PHS rules.
 *
 * Arguments:
 * rules	- ptr to S_SERVICEFLOW_TABLE structure.
 *
 * Returns:
 * Does not return any value.
 */
static void free_phs_serviceflow_rules(struct bcm_phs_table *psServiceFlowRulesTable)
{
	int i, j;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);
	struct bcm_phs_classifier_entry *curr_act_rules_list;
	struct bcm_phs_classifier_entry *curr_old_rules_list;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"=======>\n");

	if (!psServiceFlowRulesTable)
		goto out;

	for (i = 0; i < MAX_SERVICEFLOWS; i++) {
		struct bcm_phs_entry stServiceFlowEntry =
			psServiceFlowRulesTable->stSFList[i];
		struct bcm_phs_classifier_table *pstClassifierRulesTable =
			stServiceFlowEntry.pstClassifierTable;

		if (pstClassifierRulesTable) {
			for (j = 0; j < MAX_PHSRULE_PER_SF; j++) {
				curr_act_rules_list =
					&pstClassifierRulesTable->stActivePhsRulesList[j];

				curr_old_rules_list =
					&pstClassifierRulesTable->stOldPhsRulesList[j];

				if (curr_act_rules_list->pstPhsRule) {

					if (curr_act_rules_list->pstPhsRule->u8RefCnt)
						curr_act_rules_list->pstPhsRule->u8RefCnt--;

					if (0 == curr_act_rules_list->pstPhsRule->u8RefCnt)
						kfree(curr_act_rules_list->pstPhsRule);

					curr_act_rules_list->pstPhsRule = NULL;
				}

				if (curr_old_rules_list->pstPhsRule) {

					if (curr_old_rules_list->pstPhsRule->u8RefCnt)
						curr_old_rules_list->pstPhsRule->u8RefCnt--;

					if (0 == curr_old_rules_list->pstPhsRule->u8RefCnt)
						kfree(curr_old_rules_list->pstPhsRule);

					curr_old_rules_list->pstPhsRule = NULL;
				}
			}
			kfree(pstClassifierRulesTable);
			stServiceFlowEntry.pstClassifierTable =
				pstClassifierRulesTable = NULL;
		}
	}

out:

	kfree(psServiceFlowRulesTable);
	psServiceFlowRulesTable = NULL;
}

static bool ValidatePHSRuleComplete(IN const struct bcm_phs_rule *psPhsRule)
{
	return (psPhsRule &&
		psPhsRule->u8PHSI &&
		psPhsRule->u8PHSS &&
		psPhsRule->u8PHSFLength);
}

UINT GetServiceFlowEntry(IN struct bcm_phs_table *psServiceFlowTable,
			 IN B_UINT16 uiVcid,
			 struct bcm_phs_entry **ppstServiceFlowEntry)
{
	int i;

	for (i = 0; i < MAX_SERVICEFLOWS; i++) {
		if (psServiceFlowTable->stSFList[i].bUsed) {
			if (psServiceFlowTable->stSFList[i].uiVcid == uiVcid) {
				*ppstServiceFlowEntry =
					&psServiceFlowTable->stSFList[i];
				return i;
			}
		}
	}

	*ppstServiceFlowEntry = NULL;
	return PHS_INVALID_TABLE_INDEX;
}

static UINT GetClassifierEntry(IN struct bcm_phs_classifier_table *pstClassifierTable,
			       IN B_UINT32 uiClsid,
			       enum bcm_phs_classifier_context eClsContext,
			       OUT struct bcm_phs_classifier_entry **ppstClassifierEntry)
{
	int  i;
	struct bcm_phs_classifier_entry *psClassifierRules = NULL;

	for (i = 0; i < MAX_PHSRULE_PER_SF; i++) {

		if (eClsContext == eActiveClassifierRuleContext)
			psClassifierRules =
				&pstClassifierTable->stActivePhsRulesList[i];
		else
			psClassifierRules =
				&pstClassifierTable->stOldPhsRulesList[i];

		if (psClassifierRules->bUsed) {
			if (psClassifierRules->uiClassifierRuleId == uiClsid) {
				*ppstClassifierEntry = psClassifierRules;
				return i;
			}
		}
	}

	*ppstClassifierEntry = NULL;
	return PHS_INVALID_TABLE_INDEX;
}

static UINT GetPhsRuleEntry(IN struct bcm_phs_classifier_table *pstClassifierTable,
			    IN B_UINT32 uiPHSI,
			    enum bcm_phs_classifier_context eClsContext,
			    OUT struct bcm_phs_rule **ppstPhsRule)
{
	int  i;
	struct bcm_phs_classifier_entry *pstClassifierRule = NULL;

	for (i = 0; i < MAX_PHSRULE_PER_SF; i++) {
		if (eClsContext == eActiveClassifierRuleContext)
			pstClassifierRule =
				&pstClassifierTable->stActivePhsRulesList[i];
		else
			pstClassifierRule =
				&pstClassifierTable->stOldPhsRulesList[i];

		if (pstClassifierRule->bUsed) {
			if (pstClassifierRule->u8PHSI == uiPHSI) {
				*ppstPhsRule = pstClassifierRule->pstPhsRule;
				return i;
			}
		}
	}

	*ppstPhsRule = NULL;
	return PHS_INVALID_TABLE_INDEX;
}

static UINT CreateSFToClassifierRuleMapping(IN B_UINT16 uiVcid,
					    IN B_UINT16  uiClsId,
					    IN struct bcm_phs_table *psServiceFlowTable,
					    struct bcm_phs_rule *psPhsRule,
					    B_UINT8 u8AssociatedPHSI)
{
	struct bcm_phs_classifier_table *psaClassifiertable = NULL;
	UINT uiStatus = 0;
	int iSfIndex;
	bool bFreeEntryFound = false;

	/* Check for a free entry in SFID table */
	for (iSfIndex = 0; iSfIndex < MAX_SERVICEFLOWS; iSfIndex++) {
		if (!psServiceFlowTable->stSFList[iSfIndex].bUsed) {
			bFreeEntryFound = TRUE;
			break;
		}
	}

	if (!bFreeEntryFound)
		return ERR_SFTABLE_FULL;

	psaClassifiertable =
		psServiceFlowTable->stSFList[iSfIndex].pstClassifierTable;
	uiStatus =
		CreateClassifierPHSRule(uiClsId, psaClassifiertable, psPhsRule,
					eActiveClassifierRuleContext, u8AssociatedPHSI);
	if (uiStatus == PHS_SUCCESS) {
		/* Add entry at free index to the SF */
		psServiceFlowTable->stSFList[iSfIndex].bUsed = TRUE;
		psServiceFlowTable->stSFList[iSfIndex].uiVcid = uiVcid;
	}

	return uiStatus;
}

static UINT CreateClassiferToPHSRuleMapping(IN B_UINT16 uiVcid,
					    IN B_UINT16 uiClsId,
					    IN struct bcm_phs_entry *pstServiceFlowEntry,
					    struct bcm_phs_rule *psPhsRule,
					    B_UINT8 u8AssociatedPHSI)
{
	struct bcm_phs_classifier_entry *pstClassifierEntry = NULL;
	UINT uiStatus = PHS_SUCCESS;
	UINT nClassifierIndex = 0;
	struct bcm_phs_classifier_table *psaClassifiertable = NULL;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	psaClassifiertable = pstServiceFlowEntry->pstClassifierTable;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"==>");

	/* Check if the supplied Classifier already exists */
	nClassifierIndex = GetClassifierEntry(
		pstServiceFlowEntry->pstClassifierTable,
		uiClsId,
		eActiveClassifierRuleContext,
		&pstClassifierEntry);

	if (nClassifierIndex == PHS_INVALID_TABLE_INDEX) {
		/*
		 * The Classifier doesn't exist. So its a new classifier being added.
		 * Add new entry to associate PHS Rule to the Classifier
		 */

		uiStatus = CreateClassifierPHSRule(uiClsId, psaClassifiertable,
						   psPhsRule,
						   eActiveClassifierRuleContext,
						   u8AssociatedPHSI);
		return uiStatus;
	}

	/*
	 * The Classifier exists.The PHS Rule for this classifier
	 * is being modified
	 */

	if (pstClassifierEntry->u8PHSI == psPhsRule->u8PHSI) {
		if (pstClassifierEntry->pstPhsRule == NULL)
			return ERR_PHS_INVALID_PHS_RULE;

		/*
		 * This rule already exists if any fields are changed for this PHS
		 * rule update them.
		 */
		/* If any part of PHSF is valid then we update PHSF */
		if (psPhsRule->u8PHSFLength) {
			/* update PHSF */
			memcpy(pstClassifierEntry->pstPhsRule->u8PHSF,
			       psPhsRule->u8PHSF,
			       MAX_PHS_LENGTHS);
		}

		if (psPhsRule->u8PHSFLength) {
			/* update PHSFLen */
			pstClassifierEntry->pstPhsRule->u8PHSFLength =
				psPhsRule->u8PHSFLength;
		}

		if (psPhsRule->u8PHSMLength) {
			/* update PHSM */
			memcpy(pstClassifierEntry->pstPhsRule->u8PHSM,
			       psPhsRule->u8PHSM,
			       MAX_PHS_LENGTHS);
		}

		if (psPhsRule->u8PHSMLength) {
			/* update PHSM Len */
			pstClassifierEntry->pstPhsRule->u8PHSMLength =
				psPhsRule->u8PHSMLength;
		}

		if (psPhsRule->u8PHSS) {
			/* update PHSS */
			pstClassifierEntry->pstPhsRule->u8PHSS =
				psPhsRule->u8PHSS;
		}

		/* update PHSV */
		pstClassifierEntry->pstPhsRule->u8PHSV = psPhsRule->u8PHSV;
	} else {
		/* A new rule is being set for this classifier. */
		uiStatus = UpdateClassifierPHSRule(uiClsId,
						   pstClassifierEntry,
						   psaClassifiertable,
						   psPhsRule,
						   u8AssociatedPHSI);
	}

	return uiStatus;
}

static UINT CreateClassifierPHSRule(IN B_UINT16  uiClsId,
				    struct bcm_phs_classifier_table *psaClassifiertable,
				    struct bcm_phs_rule *psPhsRule,
				    enum bcm_phs_classifier_context eClsContext,
				    B_UINT8 u8AssociatedPHSI)
{
	UINT iClassifierIndex = 0;
	bool bFreeEntryFound = false;
	struct bcm_phs_classifier_entry *psClassifierRules = NULL;
	UINT nStatus = PHS_SUCCESS;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH, DBG_LVL_ALL,
			"Inside CreateClassifierPHSRule");

	if (psaClassifiertable == NULL)
		return ERR_INVALID_CLASSIFIERTABLE_FOR_SF;

	if (eClsContext == eOldClassifierRuleContext) {
		/*
		 * If An Old Entry for this classifier ID already exists in the
		 * old rules table replace it.
		 */

		iClassifierIndex = GetClassifierEntry(psaClassifiertable,
						      uiClsId,
						      eClsContext,
						      &psClassifierRules);

		if (iClassifierIndex != PHS_INVALID_TABLE_INDEX) {
			/*
			 * The Classifier already exists in the old rules table
			 * Lets replace the old classifier with the new one.
			 */
			bFreeEntryFound = TRUE;
		}
	}

	if (!bFreeEntryFound) {
		/* Continue to search for a free location to add the rule */
		for (iClassifierIndex = 0; iClassifierIndex <
			     MAX_PHSRULE_PER_SF; iClassifierIndex++) {
			if (eClsContext == eActiveClassifierRuleContext)
				psClassifierRules = &psaClassifiertable->stActivePhsRulesList[iClassifierIndex];
			else
				psClassifierRules = &psaClassifiertable->stOldPhsRulesList[iClassifierIndex];

			if (!psClassifierRules->bUsed) {
				bFreeEntryFound = TRUE;
				break;
			}
		}
	}

	if (!bFreeEntryFound) {

		if (eClsContext == eActiveClassifierRuleContext)
			return ERR_CLSASSIFIER_TABLE_FULL;
		else {
			/* Lets replace the oldest rule if we are looking in old Rule table */
			if (psaClassifiertable->uiOldestPhsRuleIndex >= MAX_PHSRULE_PER_SF)
				psaClassifiertable->uiOldestPhsRuleIndex = 0;

			iClassifierIndex =
				psaClassifiertable->uiOldestPhsRuleIndex;
			psClassifierRules =
				&psaClassifiertable->stOldPhsRulesList[iClassifierIndex];

			(psaClassifiertable->uiOldestPhsRuleIndex)++;
		}
	}

	if (eClsContext == eOldClassifierRuleContext) {

		if (psClassifierRules->pstPhsRule == NULL) {

			psClassifierRules->pstPhsRule =
				kmalloc(sizeof(struct bcm_phs_rule),
					GFP_KERNEL);

			if (NULL == psClassifierRules->pstPhsRule)
				return ERR_PHSRULE_MEMALLOC_FAIL;
		}

		psClassifierRules->bUsed = TRUE;
		psClassifierRules->uiClassifierRuleId = uiClsId;
		psClassifierRules->u8PHSI = psPhsRule->u8PHSI;
		psClassifierRules->bUnclassifiedPHSRule =
			psPhsRule->bUnclassifiedPHSRule;

		/* Update The PHS rule */
		memcpy(psClassifierRules->pstPhsRule, psPhsRule,
		       sizeof(struct bcm_phs_rule));
	} else
		nStatus = UpdateClassifierPHSRule(uiClsId,
						  psClassifierRules,
						  psaClassifiertable,
						  psPhsRule,
						  u8AssociatedPHSI);

	return nStatus;
}

static UINT UpdateClassifierPHSRule(IN B_UINT16  uiClsId,
				    IN struct bcm_phs_classifier_entry *pstClassifierEntry,
				    struct bcm_phs_classifier_table *psaClassifiertable,
				    struct bcm_phs_rule *psPhsRule,
				    B_UINT8 u8AssociatedPHSI)
{
	struct bcm_phs_rule *pstAddPhsRule = NULL;
	UINT nPhsRuleIndex = 0;
	bool bPHSRuleOrphaned = false;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	psPhsRule->u8RefCnt = 0;

	/* Step 1 Deref Any Exisiting PHS Rule in this classifier Entry */
	bPHSRuleOrphaned = DerefPhsRule(uiClsId, psaClassifiertable,
					pstClassifierEntry->pstPhsRule);

	/* Step 2 Search if there is a PHS Rule with u8AssociatedPHSI in Classifier table for this SF */
	nPhsRuleIndex = GetPhsRuleEntry(psaClassifiertable, u8AssociatedPHSI,
					eActiveClassifierRuleContext,
					&pstAddPhsRule);
	if (PHS_INVALID_TABLE_INDEX == nPhsRuleIndex) {

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL,
				"\nAdding New PHSRuleEntry For Classifier");

		if (psPhsRule->u8PHSI == 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
					DBG_LVL_ALL, "\nError PHSI is Zero\n");
			return ERR_PHS_INVALID_PHS_RULE;
		}

		/* Step 2.a PHS Rule Does Not Exist .Create New PHS Rule for uiClsId */
		if (false == bPHSRuleOrphaned) {

			pstClassifierEntry->pstPhsRule =
				kmalloc(sizeof(struct bcm_phs_rule),
					GFP_KERNEL);
			if (NULL == pstClassifierEntry->pstPhsRule)
				return ERR_PHSRULE_MEMALLOC_FAIL;
		}
		memcpy(pstClassifierEntry->pstPhsRule, psPhsRule,
		       sizeof(struct bcm_phs_rule));
	} else {
		/* Step 2.b PHS Rule  Exists Tie uiClsId with the existing PHS Rule */
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_DISPATCH,
				DBG_LVL_ALL,
				"\nTying Classifier to Existing PHS Rule");
		if (bPHSRuleOrphaned) {
			kfree(pstClassifierEntry->pstPhsRule);
			pstClassifierEntry->pstPhsRule = NULL;
		}
		pstClassifierEntry->pstPhsRule = pstAddPhsRule;
	}

	pstClassifierEntry->bUsed = TRUE;
	pstClassifierEntry->u8PHSI = pstClassifierEntry->pstPhsRule->u8PHSI;
	pstClassifierEntry->uiClassifierRuleId = uiClsId;
	pstClassifierEntry->pstPhsRule->u8RefCnt++;
	pstClassifierEntry->bUnclassifiedPHSRule =
		pstClassifierEntry->pstPhsRule->bUnclassifiedPHSRule;

	return PHS_SUCCESS;
}

static bool DerefPhsRule(IN B_UINT16  uiClsId,
			 struct bcm_phs_classifier_table *psaClassifiertable,
			 struct bcm_phs_rule *pstPhsRule)
{
	if (pstPhsRule == NULL)
		return false;

	if (pstPhsRule->u8RefCnt)
		pstPhsRule->u8RefCnt--;

	if (0 == pstPhsRule->u8RefCnt) {
		/*
		 * if(pstPhsRule->u8PHSI)
		 * Store the currently active rule into the old rules list
		 * CreateClassifierPHSRule(uiClsId,psaClassifiertable,pstPhsRule,eOldClassifierRuleContext,pstPhsRule->u8PHSI);
		 */
		return TRUE;
	} else
		return false;
}

static void dbg_print_st_cls_entry(struct bcm_mini_adapter *ad,
				   struct bcm_phs_entry *st_serv_flow_entry,
				   struct bcm_phs_classifier_entry *st_cls_entry)
{
	int k;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "\n VCID  : %#X", st_serv_flow_entry->uiVcid);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n ClassifierID  : %#X", st_cls_entry->uiClassifierRuleId);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSRuleID  : %#X", st_cls_entry->u8PHSI);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n****************PHS Rule********************\n");
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSI  : %#X", st_cls_entry->pstPhsRule->u8PHSI);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSFLength : %#X ", st_cls_entry->pstPhsRule->u8PHSFLength);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSF : ");

	for (k = 0 ; k < st_cls_entry->pstPhsRule->u8PHSFLength; k++)
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "%#X  ", st_cls_entry->pstPhsRule->u8PHSF[k]);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSMLength  : %#X", st_cls_entry->pstPhsRule->u8PHSMLength);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSM :");

	for (k = 0; k < st_cls_entry->pstPhsRule->u8PHSMLength; k++)
		BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "%#X  ", st_cls_entry->pstPhsRule->u8PHSM[k]);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSS : %#X ", st_cls_entry->pstPhsRule->u8PHSS);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, (DBG_LVL_ALL|DBG_NO_FUNC_PRINT), "\n PHSV  : %#X", st_cls_entry->pstPhsRule->u8PHSV);
	BCM_DEBUG_PRINT(ad, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "\n********************************************\n");
}

static void phsrules_per_sf_dbg_print(struct bcm_mini_adapter *ad,
				      struct bcm_phs_entry *st_serv_flow_entry)
{
	int j, l;
	struct bcm_phs_classifier_entry st_cls_entry;

	for (j = 0; j < MAX_PHSRULE_PER_SF; j++) {

		for (l = 0; l < 2; l++) {

			if (l == 0) {
				st_cls_entry = st_serv_flow_entry->pstClassifierTable->stActivePhsRulesList[j];
				if (st_cls_entry.bUsed)
					BCM_DEBUG_PRINT(ad,
							DBG_TYPE_OTHERS,
							DUMP_INFO,
							(DBG_LVL_ALL | DBG_NO_FUNC_PRINT),
							"\n Active PHS Rule :\n");
			} else {
				st_cls_entry = st_serv_flow_entry->pstClassifierTable->stOldPhsRulesList[j];
				if (st_cls_entry.bUsed)
					BCM_DEBUG_PRINT(ad,
							DBG_TYPE_OTHERS,
							DUMP_INFO,
							(DBG_LVL_ALL | DBG_NO_FUNC_PRINT),
							"\n Old PHS Rule :\n");
			}

			if (st_cls_entry.bUsed) {
				dbg_print_st_cls_entry(ad,
						       st_serv_flow_entry,
						       &st_cls_entry);
			}
		}
	}
}

void DumpPhsRules(struct bcm_phs_extension *pDeviceExtension)
{
	int i;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,
			"\n Dumping PHS Rules :\n");

	for (i = 0; i < MAX_SERVICEFLOWS; i++) {

		struct bcm_phs_entry stServFlowEntry =
			pDeviceExtension->pstServiceFlowPhsRulesTable->stSFList[i];

		if (!stServFlowEntry.bUsed)
			continue;

		phsrules_per_sf_dbg_print(Adapter, &stServFlowEntry);
	}
}

/*
 * Procedure:   phs_decompress
 *
 * Description: This routine restores the static fields within the packet.
 *
 * Arguments:
 *	in_buf			- ptr to incoming packet buffer.
 *	out_buf			- ptr to output buffer where the suppressed header is copied.
 *	decomp_phs_rules - ptr to PHS rule.
 *	header_size		- ptr to field which holds the phss or phsf_length.
 *
 * Returns:
 *	size -The number of bytes of dynamic fields present with in the incoming packet
 *			header.
 *	0	-If PHS rule is NULL.If PHSI is 0 indicateing packet as uncompressed.
 */
static int phs_decompress(unsigned char *in_buf,
			  unsigned char *out_buf,
			  struct bcm_phs_rule *decomp_phs_rules,
			  UINT *header_size)
{
	int phss, size = 0;
	struct bcm_phs_rule *tmp_memb;
	int bit, i = 0;
	unsigned char *phsf, *phsm;
	int in_buf_len = *header_size - 1;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	in_buf++;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE, DBG_LVL_ALL,
			"====>\n");
	*header_size = 0;

	if (decomp_phs_rules == NULL)
		return 0;

	tmp_memb = decomp_phs_rules;
	/*
	 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, PHS_RECEIVE,DBG_LVL_ALL,"\nDECOMP:In phs_decompress PHSI 1  %d",phsi));
	 * header_size = tmp_memb->u8PHSFLength;
	 */
	phss = tmp_memb->u8PHSS;
	phsf = tmp_memb->u8PHSF;
	phsm = tmp_memb->u8PHSM;

	if (phss > MAX_PHS_LENGTHS)
		phss = MAX_PHS_LENGTHS;

	/*
	 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, PHS_RECEIVE,DBG_LVL_ALL,"\nDECOMP:
	 * In phs_decompress PHSI  %d phss %d index %d",phsi,phss,index));
	 */
	while ((phss > 0) && (size < in_buf_len)) {
		bit = ((*phsm << i) & SUPPRESS);

		if (bit == SUPPRESS) {
			*out_buf = *phsf;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE,
					DBG_LVL_ALL,
					"\nDECOMP:In phss  %d phsf %d output %d",
					phss, *phsf, *out_buf);
		} else {
			*out_buf = *in_buf;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_RECEIVE,
					DBG_LVL_ALL,
					"\nDECOMP:In phss  %d input %d output %d",
					phss, *in_buf, *out_buf);
			in_buf++;
			size++;
		}
		out_buf++;
		phsf++;
		phss--;
		i++;
		*header_size = *header_size + 1;

		if (i > MAX_NO_BIT) {
			i = 0;
			phsm++;
		}
	}

	return size;
}

/*
 * Procedure:   phs_compress
 *
 * Description: This routine suppresses the static fields within the packet.Before
 * that it will verify the fields to be suppressed with the corresponding fields in the
 * phsf. For verification it checks the phsv field of PHS rule. If set and verification
 * succeeds it suppresses the field.If any one static field is found different none of
 * the static fields are suppressed then the packet is sent as uncompressed packet with
 * phsi=0.
 *
 * Arguments:
 *	phs_rule - ptr to PHS rule.
 *	in_buf		- ptr to incoming packet buffer.
 *	out_buf		- ptr to output buffer where the suppressed header is copied.
 *	header_size	- ptr to field which holds the phss.
 *
 * Returns:
 *	size-The number of bytes copied into the output buffer i.e dynamic fields
 *	0	-If PHS rule is NULL.If PHSV field is not set.If the verification fails.
 */
static int phs_compress(struct bcm_phs_rule *phs_rule,
			unsigned char *in_buf,
			unsigned char *out_buf,
			UINT *header_size,
			UINT *new_header_size)
{
	unsigned char *old_addr = out_buf;
	int suppress = 0;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	if (phs_rule == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"\nphs_compress(): phs_rule null!");
		*out_buf = ZERO_PHSI;
		return STATUS_PHS_NOCOMPRESSION;
	}

	if (phs_rule->u8PHSS <= *new_header_size)
		*header_size = phs_rule->u8PHSS;
	else
		*header_size = *new_header_size;

	/* To copy PHSI */
	out_buf++;
	suppress = verify_suppress_phsf(in_buf, out_buf, phs_rule->u8PHSF,
					phs_rule->u8PHSM, phs_rule->u8PHSS,
					phs_rule->u8PHSV, new_header_size);

	if (suppress == STATUS_PHS_COMPRESSED) {
		*old_addr = (unsigned char)phs_rule->u8PHSI;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"\nCOMP:In phs_compress phsi %d",
				phs_rule->u8PHSI);
	} else {
		*old_addr = ZERO_PHSI;
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
				"\nCOMP:In phs_compress PHSV Verification failed");
	}

	return suppress;
}

/*
 * Procedure:	verify_suppress_phsf
 *
 * Description: This routine verifies the fields of the packet and if all the
 * static fields are equal it adds the phsi of that PHS rule.If any static
 * field differs it woun't suppress any field.
 *
 * Arguments:
 * rules_set	- ptr to classifier_rules.
 * in_buffer	- ptr to incoming packet buffer.
 * out_buffer	- ptr to output buffer where the suppressed header is copied.
 * phsf			- ptr to phsf.
 * phsm			- ptr to phsm.
 * phss			- variable holding phss.
 *
 * Returns:
 *	size-The number of bytes copied into the output buffer i.e dynamic fields.
 *	0	-Packet has failed the verification.
 */
static int verify_suppress_phsf(unsigned char *in_buffer,
				unsigned char *out_buffer,
				unsigned char *phsf,
				unsigned char *phsm,
				unsigned int phss,
				unsigned int phsv,
				UINT *new_header_size)
{
	unsigned int size = 0;
	int bit, i = 0;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
			"\nCOMP:In verify_phsf PHSM - 0x%X", *phsm);

	if (phss > (*new_header_size))
		phss = *new_header_size;

	while (phss > 0) {
		bit = ((*phsm << i) & SUPPRESS);
		if (bit == SUPPRESS) {
			if (*in_buffer != *phsf) {
				if (phsv == VERIFY) {
					BCM_DEBUG_PRINT(Adapter,
							DBG_TYPE_OTHERS,
							PHS_SEND,
							DBG_LVL_ALL,
							"\nCOMP:In verify_phsf failed for field  %d buf  %d phsf %d",
							phss,
							*in_buffer,
							*phsf);
					return STATUS_PHS_NOCOMPRESSION;
				}
			} else
				BCM_DEBUG_PRINT(Adapter,
						DBG_TYPE_OTHERS,
						PHS_SEND,
						DBG_LVL_ALL,
						"\nCOMP:In verify_phsf success for field  %d buf  %d phsf %d",
						phss,
						*in_buffer,
						*phsf);
		} else {
			*out_buffer = *in_buffer;
			BCM_DEBUG_PRINT(Adapter,
					DBG_TYPE_OTHERS,
					PHS_SEND,
					DBG_LVL_ALL,
					"\nCOMP:In copying_header input %d  out %d",
					*in_buffer,
					*out_buffer);
			out_buffer++;
			size++;
		}

		in_buffer++;
		phsf++;
		phss--;
		i++;

		if (i > MAX_NO_BIT) {
			i = 0;
			phsm++;
		}
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, PHS_SEND, DBG_LVL_ALL,
			"\nCOMP:In verify_phsf success");
	*new_header_size = size;
	return STATUS_PHS_COMPRESSED;
}
