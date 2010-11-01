
/*
 * File Name: hostmibs.c
 *
 * Author: Beceem Communications Pvt. Ltd
 *
 * Abstract: This file contains the routines to copy the statistics used by
 * the driver to the Host MIBS structure and giving the same to Application.
 *
 */
#include "headers.h"

INT  ProcessGetHostMibs(PMINI_ADAPTER Adapter, S_MIBS_HOST_STATS_MIBS *pstHostMibs)
{
	S_SERVICEFLOW_ENTRY    *pstServiceFlowEntry = NULL;
	S_PHS_RULE             *pstPhsRule          = NULL;
	S_CLASSIFIER_TABLE     *pstClassifierTable  = NULL;
	S_CLASSIFIER_ENTRY     *pstClassifierRule   = NULL;
	PPHS_DEVICE_EXTENSION  pDeviceExtension     = (PPHS_DEVICE_EXTENSION)&Adapter->stBCMPhsContext;

	UINT nClassifierIndex = 0, nPhsTableIndex = 0,nSfIndex = 0, uiIndex = 0;

	if(pDeviceExtension == NULL)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, HOST_MIBS, DBG_LVL_ALL, "Invalid Device Extension\n");
		return STATUS_FAILURE;
	}

	//Copy the classifier Table
	for(nClassifierIndex=0; nClassifierIndex < MAX_CLASSIFIERS;
			nClassifierIndex++)
	{
		if(Adapter->astClassifierTable[nClassifierIndex].bUsed == TRUE)
			memcpy((PVOID)&pstHostMibs->astClassifierTable[nClassifierIndex],
				(PVOID)&Adapter->astClassifierTable[nClassifierIndex],
				sizeof(S_MIBS_CLASSIFIER_RULE));
	}

  //Copy the SF Table
	for(nSfIndex=0; nSfIndex < NO_OF_QUEUES ; nSfIndex++)
	{
	if(Adapter->PackInfo[nSfIndex].bValid)
	{
			memcpy((PVOID)&pstHostMibs->astSFtable[nSfIndex],(PVOID)&Adapter->PackInfo[nSfIndex],sizeof(S_MIBS_SERVICEFLOW_TABLE));
	}
	else
	{
		//if index in not valid, don't process this for the PHS table. Go For the next entry.
		continue ;
	}

		//Retrieve the SFID Entry Index for requested Service Flow
		if(PHS_INVALID_TABLE_INDEX == GetServiceFlowEntry(pDeviceExtension->pstServiceFlowPhsRulesTable,
						  Adapter->PackInfo[nSfIndex].usVCID_Value ,&pstServiceFlowEntry))
		{

		continue;
		}

		pstClassifierTable = pstServiceFlowEntry->pstClassifierTable;


		for(uiIndex = 0; uiIndex < MAX_PHSRULE_PER_SF; uiIndex++)
		{
			pstClassifierRule = &pstClassifierTable->stActivePhsRulesList[uiIndex];

		if(pstClassifierRule->bUsed)
		{
			pstPhsRule = pstClassifierRule->pstPhsRule;

			pstHostMibs->astPhsRulesTable[nPhsTableIndex].ulSFID = Adapter->PackInfo[nSfIndex].ulSFID;

			memcpy(&pstHostMibs->astPhsRulesTable[nPhsTableIndex].u8PHSI,
						&pstPhsRule->u8PHSI,
						sizeof(S_PHS_RULE));
				nPhsTableIndex++;

			}

		}

	}


	//copy other Host Statistics parameters
	pstHostMibs->stHostInfo.GoodTransmits = Adapter->dev->stats.tx_packets;
	pstHostMibs->stHostInfo.GoodReceives = Adapter->dev->stats.rx_packets;
	pstHostMibs->stHostInfo.CurrNumFreeDesc =
			atomic_read(&Adapter->CurrNumFreeTxDesc);
	pstHostMibs->stHostInfo.BEBucketSize = Adapter->BEBucketSize;
	pstHostMibs->stHostInfo.rtPSBucketSize = Adapter->rtPSBucketSize;
	pstHostMibs->stHostInfo.TimerActive = Adapter->TimerActive;
	pstHostMibs->stHostInfo.u32TotalDSD = Adapter->u32TotalDSD;

	memcpy(pstHostMibs->stHostInfo.aTxPktSizeHist,Adapter->aTxPktSizeHist,sizeof(UINT32)*MIBS_MAX_HIST_ENTRIES);
	memcpy(pstHostMibs->stHostInfo.aRxPktSizeHist,Adapter->aRxPktSizeHist,sizeof(UINT32)*MIBS_MAX_HIST_ENTRIES);

	return STATUS_SUCCESS;
}


VOID GetDroppedAppCntrlPktMibs(S_MIBS_HOST_STATS_MIBS *pstHostMibs, const PPER_TARANG_DATA pTarang)
{
	memcpy(&(pstHostMibs->stDroppedAppCntrlMsgs),
	       &(pTarang->stDroppedAppCntrlMsgs),sizeof(S_MIBS_DROPPED_APP_CNTRL_MESSAGES));
}


VOID CopyMIBSExtendedSFParameters(PMINI_ADAPTER Adapter,
		CServiceFlowParamSI *psfLocalSet, UINT uiSearchRuleIndex)
{
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfSfid = psfLocalSet->u32SFID;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsMaxSustainedRate = psfLocalSet->u32MaxSustainedTrafficRate;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsMaxTrafficBurst = psfLocalSet->u32MaxTrafficBurst;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsMinReservedRate = psfLocalSet->u32MinReservedTrafficRate;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsToleratedJitter = psfLocalSet->u32ToleratedJitter;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsMaxLatency = psfLocalSet->u32MaximumLatency;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsFixedVsVariableSduInd = psfLocalSet->u8FixedLengthVSVariableLengthSDUIndicator;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsFixedVsVariableSduInd = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsFixedVsVariableSduInd);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSduSize = psfLocalSet->u8SDUSize;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSduSize = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSduSize);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSfSchedulingType = psfLocalSet->u8ServiceFlowSchedulingType;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSfSchedulingType = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsSfSchedulingType);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqEnable = psfLocalSet->u8ARQEnable;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqEnable = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqEnable);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqWindowSize = ntohs(psfLocalSet->u16ARQWindowSize);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqWindowSize = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqWindowSize);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockLifetime = ntohs(psfLocalSet->u16ARQBlockLifeTime);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockLifetime = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockLifetime);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqSyncLossTimeout = ntohs(psfLocalSet->u16ARQSyncLossTimeOut);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqSyncLossTimeout = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqSyncLossTimeout);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqDeliverInOrder = psfLocalSet->u8ARQDeliverInOrder;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqDeliverInOrder = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqDeliverInOrder);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqRxPurgeTimeout = ntohs(psfLocalSet->u16ARQRxPurgeTimeOut);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqRxPurgeTimeout = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqRxPurgeTimeout);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockSize = ntohs(psfLocalSet->u16ARQBlockSize);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockSize = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsArqBlockSize);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsReqTxPolicy = psfLocalSet->u8RequesttransmissionPolicy;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsReqTxPolicy = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsReqTxPolicy);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnSfCsSpecification = psfLocalSet->u8CSSpecification;
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnSfCsSpecification = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnSfCsSpecification);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsTargetSaid = ntohs(psfLocalSet->u16TargetSAID);
	Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsTargetSaid = ntohl(Adapter->PackInfo[uiSearchRuleIndex].stMibsExtServiceFlowTable.wmanIfCmnCpsTargetSaid);

}
