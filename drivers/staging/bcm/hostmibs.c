/*
 * File Name: hostmibs.c
 *
 * Author: Beceem Communications Pvt. Ltd
 *
 * Abstract: This file contains the routines to copy the statistics used by
 * the driver to the Host MIBS structure and giving the same to Application.
 */

#include "headers.h"

INT ProcessGetHostMibs(struct bcm_mini_adapter *Adapter, struct bcm_host_stats_mibs *pstHostMibs)
{
	S_SERVICEFLOW_ENTRY *pstServiceFlowEntry = NULL;
	S_PHS_RULE *pstPhsRule = NULL;
	S_CLASSIFIER_TABLE *pstClassifierTable = NULL;
	S_CLASSIFIER_ENTRY *pstClassifierRule = NULL;
	PPHS_DEVICE_EXTENSION pDeviceExtension = (PPHS_DEVICE_EXTENSION) &Adapter->stBCMPhsContext;

	UINT nClassifierIndex = 0, nPhsTableIndex = 0, nSfIndex = 0, uiIndex = 0;

	if (pDeviceExtension == NULL) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, HOST_MIBS, DBG_LVL_ALL, "Invalid Device Extension\n");
		return STATUS_FAILURE;
	}

	/* Copy the classifier Table */
	for (nClassifierIndex = 0; nClassifierIndex < MAX_CLASSIFIERS; nClassifierIndex++) {
		if (Adapter->astClassifierTable[nClassifierIndex].bUsed == TRUE)
			memcpy((PVOID) & pstHostMibs->
			       astClassifierTable[nClassifierIndex],
			       (PVOID) & Adapter->
			       astClassifierTable[nClassifierIndex],
			       sizeof(S_MIBS_CLASSIFIER_RULE));
	}

	/* Copy the SF Table */
	for (nSfIndex = 0; nSfIndex < NO_OF_QUEUES; nSfIndex++) {
		if (Adapter->PackInfo[nSfIndex].bValid) {
			memcpy((PVOID) & pstHostMibs->astSFtable[nSfIndex],
			       (PVOID) & Adapter->PackInfo[nSfIndex],
			       sizeof(S_MIBS_SERVICEFLOW_TABLE));
		} else {
			/* If index in not valid,
			 * don't process this for the PHS table.
			 * Go For the next entry.
			 */
			continue;
		}

		/* Retrieve the SFID Entry Index for requested Service Flow */
		if (PHS_INVALID_TABLE_INDEX ==
		    GetServiceFlowEntry(pDeviceExtension->
					pstServiceFlowPhsRulesTable,
					Adapter->PackInfo[nSfIndex].
					usVCID_Value, &pstServiceFlowEntry))

			continue;

		pstClassifierTable = pstServiceFlowEntry->pstClassifierTable;

		for (uiIndex = 0; uiIndex < MAX_PHSRULE_PER_SF; uiIndex++) {
			pstClassifierRule = &pstClassifierTable->stActivePhsRulesList[uiIndex];

			if (pstClassifierRule->bUsed) {
				pstPhsRule = pstClassifierRule->pstPhsRule;

				pstHostMibs->astPhsRulesTable[nPhsTableIndex].
				    ulSFID = Adapter->PackInfo[nSfIndex].ulSFID;

				memcpy(&pstHostMibs->
				       astPhsRulesTable[nPhsTableIndex].u8PHSI,
				       &pstPhsRule->u8PHSI, sizeof(S_PHS_RULE));
				nPhsTableIndex++;

			}

		}

	}

	/* Copy other Host Statistics parameters */
	pstHostMibs->stHostInfo.GoodTransmits = Adapter->dev->stats.tx_packets;
	pstHostMibs->stHostInfo.GoodReceives = Adapter->dev->stats.rx_packets;
	pstHostMibs->stHostInfo.CurrNumFreeDesc = atomic_read(&Adapter->CurrNumFreeTxDesc);
	pstHostMibs->stHostInfo.BEBucketSize = Adapter->BEBucketSize;
	pstHostMibs->stHostInfo.rtPSBucketSize = Adapter->rtPSBucketSize;
	pstHostMibs->stHostInfo.TimerActive = Adapter->TimerActive;
	pstHostMibs->stHostInfo.u32TotalDSD = Adapter->u32TotalDSD;

	memcpy(pstHostMibs->stHostInfo.aTxPktSizeHist, Adapter->aTxPktSizeHist, sizeof(UINT32) * MIBS_MAX_HIST_ENTRIES);
	memcpy(pstHostMibs->stHostInfo.aRxPktSizeHist, Adapter->aRxPktSizeHist, sizeof(UINT32) * MIBS_MAX_HIST_ENTRIES);

	return STATUS_SUCCESS;
}

VOID GetDroppedAppCntrlPktMibs(struct bcm_host_stats_mibs *pstHostMibs, struct bcm_tarang_data *pTarang)
{
	memcpy(&(pstHostMibs->stDroppedAppCntrlMsgs),
	       &(pTarang->stDroppedAppCntrlMsgs),
	       sizeof(S_MIBS_DROPPED_APP_CNTRL_MESSAGES));
}

VOID CopyMIBSExtendedSFParameters(struct bcm_mini_adapter *Adapter, struct bcm_connect_mgr_params *psfLocalSet, UINT uiSearchRuleIndex)
{
	S_MIBS_EXTSERVICEFLOW_PARAMETERS *t = &Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable;

	t->wmanIfSfid = psfLocalSet->u32SFID;
	t->wmanIfCmnCpsMaxSustainedRate = psfLocalSet->u32MaxSustainedTrafficRate;
	t->wmanIfCmnCpsMaxTrafficBurst = psfLocalSet->u32MaxTrafficBurst;
	t->wmanIfCmnCpsMinReservedRate = psfLocalSet->u32MinReservedTrafficRate;
	t->wmanIfCmnCpsToleratedJitter = psfLocalSet->u32ToleratedJitter;
	t->wmanIfCmnCpsMaxLatency = psfLocalSet->u32MaximumLatency;
	t->wmanIfCmnCpsFixedVsVariableSduInd = psfLocalSet->u8FixedLengthVSVariableLengthSDUIndicator;
	t->wmanIfCmnCpsFixedVsVariableSduInd = ntohl(t->wmanIfCmnCpsFixedVsVariableSduInd);
	t->wmanIfCmnCpsSduSize = psfLocalSet->u8SDUSize;
	t->wmanIfCmnCpsSduSize = ntohl(t->wmanIfCmnCpsSduSize);
	t->wmanIfCmnCpsSfSchedulingType = psfLocalSet->u8ServiceFlowSchedulingType;
	t->wmanIfCmnCpsSfSchedulingType = ntohl(t->wmanIfCmnCpsSfSchedulingType);
	t->wmanIfCmnCpsArqEnable = psfLocalSet->u8ARQEnable;
	t->wmanIfCmnCpsArqEnable = ntohl(t->wmanIfCmnCpsArqEnable);
	t->wmanIfCmnCpsArqWindowSize = ntohs(psfLocalSet->u16ARQWindowSize);
	t->wmanIfCmnCpsArqWindowSize = ntohl(t->wmanIfCmnCpsArqWindowSize);
	t->wmanIfCmnCpsArqBlockLifetime = ntohs(psfLocalSet->u16ARQBlockLifeTime);
	t->wmanIfCmnCpsArqBlockLifetime = ntohl(t->wmanIfCmnCpsArqBlockLifetime);
	t->wmanIfCmnCpsArqSyncLossTimeout = ntohs(psfLocalSet->u16ARQSyncLossTimeOut);
	t->wmanIfCmnCpsArqSyncLossTimeout = ntohl(t->wmanIfCmnCpsArqSyncLossTimeout);
	t->wmanIfCmnCpsArqDeliverInOrder = psfLocalSet->u8ARQDeliverInOrder;
	t->wmanIfCmnCpsArqDeliverInOrder = ntohl(t->wmanIfCmnCpsArqDeliverInOrder);
	t->wmanIfCmnCpsArqRxPurgeTimeout = ntohs(psfLocalSet->u16ARQRxPurgeTimeOut);
	t->wmanIfCmnCpsArqRxPurgeTimeout = ntohl(t->wmanIfCmnCpsArqRxPurgeTimeout);
	t->wmanIfCmnCpsArqBlockSize = ntohs(psfLocalSet->u16ARQBlockSize);
	t->wmanIfCmnCpsArqBlockSize = ntohl(t->wmanIfCmnCpsArqBlockSize);
	t->wmanIfCmnCpsReqTxPolicy = psfLocalSet->u8RequesttransmissionPolicy;
	t->wmanIfCmnCpsReqTxPolicy = ntohl(t->wmanIfCmnCpsReqTxPolicy);
	t->wmanIfCmnSfCsSpecification = psfLocalSet->u8CSSpecification;
	t->wmanIfCmnSfCsSpecification = ntohl(t->wmanIfCmnSfCsSpecification);
	t->wmanIfCmnCpsTargetSaid = ntohs(psfLocalSet->u16TargetSAID);
	t->wmanIfCmnCpsTargetSaid = ntohl(t->wmanIfCmnCpsTargetSaid);

}
