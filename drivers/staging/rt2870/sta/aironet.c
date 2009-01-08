/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	aironet.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Paul Lin	04-06-15		Initial
*/
#include "../rt_config.h"

/*
	==========================================================================
	Description:
		association	state machine init,	including state	transition and timer init
	Parameters:
		S -	pointer	to the association state machine
	==========================================================================
 */
VOID	AironetStateMachineInit(
	IN	PRTMP_ADAPTER		pAd,
	IN	STATE_MACHINE		*S,
	OUT	STATE_MACHINE_FUNC	Trans[])
{
	StateMachineInit(S,	Trans, MAX_AIRONET_STATE, MAX_AIRONET_MSG, (STATE_MACHINE_FUNC)Drop, AIRONET_IDLE, AIRONET_MACHINE_BASE);
	StateMachineSetAction(S, AIRONET_IDLE, MT2_AIRONET_MSG, (STATE_MACHINE_FUNC)AironetMsgAction);
	StateMachineSetAction(S, AIRONET_IDLE, MT2_AIRONET_SCAN_REQ, (STATE_MACHINE_FUNC)AironetRequestAction);
	StateMachineSetAction(S, AIRONET_SCANNING, MT2_AIRONET_SCAN_DONE, (STATE_MACHINE_FUNC)AironetReportAction);
}

/*
	==========================================================================
	Description:
		This is	state machine function.
		When receiving EAPOL packets which is  for 802.1x key management.
		Use	both in	WPA, and WPAPSK	case.
		In this	function, further dispatch to different	functions according	to the received	packet.	 3 categories are :
		  1.  normal 4-way pairwisekey and 2-way groupkey handshake
		  2.  MIC error	(Countermeasures attack)  report packet	from STA.
		  3.  Request for pairwise/group key update	from STA
	Return:
	==========================================================================
*/
VOID	AironetMsgAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)
{
	USHORT							Length;
	UCHAR							Index, i;
	PUCHAR							pData;
	PAIRONET_RM_REQUEST_FRAME		pRMReq;
	PRM_REQUEST_ACTION				pReqElem;

	DBGPRINT(RT_DEBUG_TRACE, ("-----> AironetMsgAction\n"));

	// 0. Get Aironet IAPP header first
	pRMReq = (PAIRONET_RM_REQUEST_FRAME) &Elem->Msg[LENGTH_802_11];
	pData  = (PUCHAR) &Elem->Msg[LENGTH_802_11];

	// 1. Change endian format form network to little endian
	Length = be2cpu16(pRMReq->IAPP.Length);

	// 2.0 Sanity check, this should only happen when CCX 2.0 support is enabled
	if (pAd->StaCfg.CCXEnable != TRUE)
		return;

	// 2.1 Radio measurement must be on
	if (pAd->StaCfg.CCXControl.field.RMEnable != 1)
		return;

	// 2.2. Debug print all bit information
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP ID & Length %d\n", Length));
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP Type %x\n", pRMReq->IAPP.Type));
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP SubType %x\n", pRMReq->IAPP.SubType));
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP Dialog Token %x\n", pRMReq->IAPP.Token));
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP Activation Delay %x\n", pRMReq->Delay));
	DBGPRINT(RT_DEBUG_TRACE, ("IAPP Measurement Offset %x\n", pRMReq->Offset));

	// 3. Check IAPP frame type, it must be 0x32 for Cisco Aironet extension
	if (pRMReq->IAPP.Type != AIRONET_IAPP_TYPE)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Wrong IAPP type for Cisco Aironet extension\n"));
		return;
	}

	// 4. Check IAPP frame subtype, it must be 0x01 for Cisco Aironet extension request.
	//    Since we are acting as client only, we will disregards reply subtype.
	if (pRMReq->IAPP.SubType != AIRONET_IAPP_SUBTYPE_REQUEST)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Wrong IAPP subtype for Cisco Aironet extension\n"));
		return;
	}

	// 5. Verify Destination MAC and Source MAC, both should be all zeros.
	if (! MAC_ADDR_EQUAL(pRMReq->IAPP.DA, ZERO_MAC_ADDR))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Wrong IAPP DA for Cisco Aironet extension, it's not Zero\n"));
		return;
	}

	if (! MAC_ADDR_EQUAL(pRMReq->IAPP.SA, ZERO_MAC_ADDR))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Wrong IAPP SA for Cisco Aironet extension, it's not Zero\n"));
		return;
	}

	// 6. Reinit all report related fields
	NdisZeroMemory(pAd->StaCfg.FrameReportBuf, 2048);
	NdisZeroMemory(pAd->StaCfg.BssReportOffset, sizeof(USHORT) * MAX_LEN_OF_BSS_TABLE);
	NdisZeroMemory(pAd->StaCfg.MeasurementRequest, sizeof(RM_REQUEST_ACTION) * 4);

	// 7. Point to the start of first element report element
	pAd->StaCfg.FrameReportLen   = LENGTH_802_11 + sizeof(AIRONET_IAPP_HEADER);
	DBGPRINT(RT_DEBUG_TRACE, ("FR len = %d\n", pAd->StaCfg.FrameReportLen));
	pAd->StaCfg.LastBssIndex     = 0xff;
	pAd->StaCfg.RMReqCnt         = 0;
	pAd->StaCfg.ParallelReq      = FALSE;
	pAd->StaCfg.ParallelDuration = 0;
	pAd->StaCfg.ParallelChannel  = 0;
	pAd->StaCfg.IAPPToken        = pRMReq->IAPP.Token;
	pAd->StaCfg.CurrentRMReqIdx  = 0;
	pAd->StaCfg.CLBusyBytes      = 0;
	// Reset the statistics
	for (i = 0; i < 8; i++)
		pAd->StaCfg.RPIDensity[i] = 0;

	Index = 0;

	// 8. Save dialog token for report
	pAd->StaCfg.IAPPToken = pRMReq->IAPP.Token;

	// Save Activation delay & measurement offset, Not really needed

	// 9. Point to the first request element
	pData += sizeof(AIRONET_RM_REQUEST_FRAME);
	//    Length should exclude the CISCO Aironet SNAP header
	Length -= (sizeof(AIRONET_RM_REQUEST_FRAME) - LENGTH_802_1_H);

	// 10. Start Parsing the Measurement elements.
	//    Be careful about multiple MR elements within one frames.
	while (Length > 0)
	{
		pReqElem = (PRM_REQUEST_ACTION) pData;
		switch (pReqElem->ReqElem.Eid)
		{
			case IE_MEASUREMENT_REQUEST:
				// From the example, it seems we only need to support one request in one frame
				// There is no multiple request in one frame.
				// Besides, looks like we need to take care the measurement request only.
				// The measurement request is always 4 bytes.

				// Start parsing this type of request.
				// 0. Eid is IE_MEASUREMENT_REQUEST
				// 1. Length didn't include Eid and Length field, it always be 8.
				// 2. Measurement Token, we nned to save it for the corresponding report.
				// 3. Measurement Mode, Although there are definitions, but we din't see value other than
				//    0 from test specs examples.
				// 4. Measurement Type, this is what we need to do.
				switch (pReqElem->ReqElem.Type)
				{
					case MSRN_TYPE_CHANNEL_LOAD_REQ:
					case MSRN_TYPE_NOISE_HIST_REQ:
					case MSRN_TYPE_BEACON_REQ:
						// Check the Enable non-serving channel measurement control
						if (pAd->StaCfg.CCXControl.field.DCRMEnable == 0)
						{
							// Check channel before enqueue the action
							if (pReqElem->Measurement.Channel != pAd->CommonCfg.Channel)
								break;
						}
						else
						{
							// If off channel measurement, check the TU duration limit
							if (pReqElem->Measurement.Channel != pAd->CommonCfg.Channel)
								if (pReqElem->Measurement.Duration > pAd->StaCfg.CCXControl.field.TuLimit)
									break;
						}

						// Save requests and execute actions later
						NdisMoveMemory(&pAd->StaCfg.MeasurementRequest[Index], pReqElem, sizeof(RM_REQUEST_ACTION));
						Index += 1;
						break;

					case MSRN_TYPE_FRAME_REQ:
						// Since it's option, we will support later
						// FrameRequestAction(pAd, pData);
						break;

					default:
						break;
				}

				// Point to next Measurement request
				pData  += sizeof(RM_REQUEST_ACTION);
				Length -= sizeof(RM_REQUEST_ACTION);
				break;

			// We accept request only, all others are dropped
			case IE_MEASUREMENT_REPORT:
			case IE_AP_TX_POWER:
			case IE_MEASUREMENT_CAPABILITY:
			default:
				return;
		}
	}

	// 11. Update some flags and index
	pAd->StaCfg.RMReqCnt = Index;

	if (Index)
	{
		MlmeEnqueue(pAd, AIRONET_STATE_MACHINE, MT2_AIRONET_SCAN_REQ, 0, NULL);
		RT28XX_MLME_HANDLER(pAd);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<----- AironetMsgAction\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	AironetRequestAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)
{
	PRM_REQUEST_ACTION	pReq;

	// 1. Point to next request element
	pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[pAd->StaCfg.CurrentRMReqIdx];

	// 2. Parse measurement type and call appropriate functions
	if (pReq->ReqElem.Type == MSRN_TYPE_CHANNEL_LOAD_REQ)
		// Channel Load measurement request
		ChannelLoadRequestAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else if (pReq->ReqElem.Type == MSRN_TYPE_NOISE_HIST_REQ)
		// Noise Histogram measurement request
		NoiseHistRequestAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else if (pReq->ReqElem.Type == MSRN_TYPE_BEACON_REQ)
		// Beacon measurement request
		BeaconRequestAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else
		// Unknown. Do nothing and return, this should never happen
		return;

	// 3. Peek into the next request, if it's parallel, we will update the scan time to the largest one
	if ((pAd->StaCfg.CurrentRMReqIdx + 1) < pAd->StaCfg.RMReqCnt)
	{
		pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[pAd->StaCfg.CurrentRMReqIdx + 1];
		// Check for parallel bit
		if ((pReq->ReqElem.Mode & 0x01) && (pReq->Measurement.Channel == pAd->StaCfg.CCXScanChannel))
		{
			// Update parallel mode request information
			pAd->StaCfg.ParallelReq = TRUE;
			pAd->StaCfg.CCXScanTime = ((pReq->Measurement.Duration > pAd->StaCfg.CCXScanTime) ?
			(pReq->Measurement.Duration) : (pAd->StaCfg.CCXScanTime));
		}
	}

	// 4. Call RT28XX_MLME_HANDLER to execute the request mlme commands, Scan request is the only one used
	RT28XX_MLME_HANDLER(pAd);

}


/*
	========================================================================

	Routine Description:
		Prepare channel load report action, special scan operation added
		to support

	Arguments:
		pAd	Pointer	to our adapter
		pData		Start from element ID

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	ChannelLoadRequestAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PRM_REQUEST_ACTION				pReq;
	MLME_SCAN_REQ_STRUCT			ScanReq;
	UCHAR							ZeroSsid[32];
	NDIS_STATUS						NStatus;
	PUCHAR							pOutBuffer = NULL;
	PHEADER_802_11					pNullFrame;

	DBGPRINT(RT_DEBUG_TRACE, ("ChannelLoadRequestAction ----->\n"));

	pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[Index];
	NdisZeroMemory(ZeroSsid, 32);

	// Prepare for special scan request
	// The scan definition is different with our Active, Passive scan definition.
	// For CCX2, Active means send out probe request with broadcast BSSID.
	// Passive means no probe request sent, only listen to the beacons.
	// The channel scanned is fixed as specified, no need to scan all channels.
	// The scan wait time is specified in the request too.
	// Passive scan Mode

	// Control state machine is not idle, reject the request
	if ((pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE) && (Index == 0))
		return;

	// Fill out stuff for scan request
	ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_CISCO_CHANNEL_LOAD);
	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
	pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;

	// Reset some internal control flags to make sure this scan works.
	BssTableInit(&pAd->StaCfg.CCXBssTab);
	pAd->StaCfg.ScanCnt        = 0;
	pAd->StaCfg.CCXScanChannel = pReq->Measurement.Channel;
	pAd->StaCfg.CCXScanTime    = pReq->Measurement.Duration;

	DBGPRINT(RT_DEBUG_TRACE, ("Duration %d, Channel %d!\n", pReq->Measurement.Duration, pReq->Measurement.Channel));

	// If it's non serving channel scan, send out a null frame with PSM bit on.
	if (pAd->StaCfg.CCXScanChannel != pAd->CommonCfg.Channel)
	{
		// Use MLME enqueue method
		NStatus = MlmeAllocateMemory(pAd, (PVOID)&pOutBuffer);  //Get an unused nonpaged memory
		if (NStatus	!= NDIS_STATUS_SUCCESS)
			return;

		pNullFrame = (PHEADER_802_11) pOutBuffer;;
		// Make the power save Null frame with PSM bit on
		MgtMacHeaderInit(pAd, pNullFrame, SUBTYPE_NULL_FUNC, 1, pAd->CommonCfg.Bssid, pAd->CommonCfg.Bssid);
		pNullFrame->Duration 	= 0;
		pNullFrame->FC.Type 	= BTYPE_DATA;
		pNullFrame->FC.PwrMgmt	= PWR_SAVE;

		// Send using priority queue
		MiniportMMRequest(pAd, 0, pOutBuffer, sizeof(HEADER_802_11));
		MlmeFreeMemory(pAd, pOutBuffer);
		DBGPRINT(RT_DEBUG_TRACE, ("Send PSM Data frame for off channel RM\n"));
		RTMPusecDelay(5000);
	}

	pAd->StaCfg.CCXReqType     = MSRN_TYPE_CHANNEL_LOAD_REQ;
	pAd->StaCfg.CLBusyBytes    = 0;
	// Enable Rx with promiscuous reception
	RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, 0x1010);

	// Set channel load measurement flag
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_MEASUREMENT);

	pAd->Mlme.AironetMachine.CurrState = AIRONET_SCANNING;

	DBGPRINT(RT_DEBUG_TRACE, ("ChannelLoadRequestAction <-----\n"));
}

/*
	========================================================================

	Routine Description:
		Prepare noise histogram report action, special scan operation added
		to support

	Arguments:
		pAd	Pointer	to our adapter
		pData		Start from element ID

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	NoiseHistRequestAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PRM_REQUEST_ACTION				pReq;
	MLME_SCAN_REQ_STRUCT			ScanReq;
	UCHAR							ZeroSsid[32], i;
	NDIS_STATUS						NStatus;
	PUCHAR							pOutBuffer = NULL;
	PHEADER_802_11					pNullFrame;

	DBGPRINT(RT_DEBUG_TRACE, ("NoiseHistRequestAction ----->\n"));

	pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[Index];
	NdisZeroMemory(ZeroSsid, 32);

	// Prepare for special scan request
	// The scan definition is different with our Active, Passive scan definition.
	// For CCX2, Active means send out probe request with broadcast BSSID.
	// Passive means no probe request sent, only listen to the beacons.
	// The channel scanned is fixed as specified, no need to scan all channels.
	// The scan wait time is specified in the request too.
	// Passive scan Mode

	// Control state machine is not idle, reject the request
	if ((pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE) && (Index == 0))
		return;

	// Fill out stuff for scan request
	ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_CISCO_NOISE);
	MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
	pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;

	// Reset some internal control flags to make sure this scan works.
	BssTableInit(&pAd->StaCfg.CCXBssTab);
	pAd->StaCfg.ScanCnt        = 0;
	pAd->StaCfg.CCXScanChannel = pReq->Measurement.Channel;
	pAd->StaCfg.CCXScanTime    = pReq->Measurement.Duration;
	pAd->StaCfg.CCXReqType     = MSRN_TYPE_NOISE_HIST_REQ;

	DBGPRINT(RT_DEBUG_TRACE, ("Duration %d, Channel %d!\n", pReq->Measurement.Duration, pReq->Measurement.Channel));

	// If it's non serving channel scan, send out a null frame with PSM bit on.
	if (pAd->StaCfg.CCXScanChannel != pAd->CommonCfg.Channel)
	{
		// Use MLME enqueue method
		NStatus = MlmeAllocateMemory(pAd, (PVOID)&pOutBuffer);  //Get an unused nonpaged memory
		if (NStatus	!= NDIS_STATUS_SUCCESS)
			return;

		pNullFrame = (PHEADER_802_11) pOutBuffer;
		// Make the power save Null frame with PSM bit on
		MgtMacHeaderInit(pAd, pNullFrame, SUBTYPE_NULL_FUNC, 1, pAd->CommonCfg.Bssid, pAd->CommonCfg.Bssid);
		pNullFrame->Duration 	= 0;
		pNullFrame->FC.Type  	= BTYPE_DATA;
		pNullFrame->FC.PwrMgmt	= PWR_SAVE;

		// Send using priority queue
		MiniportMMRequest(pAd, 0, pOutBuffer, sizeof(HEADER_802_11));
		MlmeFreeMemory(pAd, pOutBuffer);
		DBGPRINT(RT_DEBUG_TRACE, ("Send PSM Data frame for off channel RM\n"));
		RTMPusecDelay(5000);
	}

	// Reset the statistics
	for (i = 0; i < 8; i++)
		pAd->StaCfg.RPIDensity[i] = 0;

	// Enable Rx with promiscuous reception
	RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, 0x1010);

	// Set channel load measurement flag
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_MEASUREMENT);

	pAd->Mlme.AironetMachine.CurrState = AIRONET_SCANNING;

	DBGPRINT(RT_DEBUG_TRACE, ("NoiseHistRequestAction <-----\n"));
}

/*
	========================================================================

	Routine Description:
		Prepare Beacon report action, special scan operation added
		to support

	Arguments:
		pAd	Pointer	to our adapter
		pData		Start from element ID

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	BeaconRequestAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PRM_REQUEST_ACTION				pReq;
	NDIS_STATUS						NStatus;
	PUCHAR							pOutBuffer = NULL;
	PHEADER_802_11					pNullFrame;
	MLME_SCAN_REQ_STRUCT			ScanReq;
	UCHAR							ZeroSsid[32];

	DBGPRINT(RT_DEBUG_TRACE, ("BeaconRequestAction ----->\n"));

	pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[Index];
	NdisZeroMemory(ZeroSsid, 32);

	// Prepare for special scan request
	// The scan definition is different with our Active, Passive scan definition.
	// For CCX2, Active means send out probe request with broadcast BSSID.
	// Passive means no probe request sent, only listen to the beacons.
	// The channel scanned is fixed as specified, no need to scan all channels.
	// The scan wait time is specified in the request too.
	if (pReq->Measurement.ScanMode == MSRN_SCAN_MODE_PASSIVE)
	{
		// Passive scan Mode
		DBGPRINT(RT_DEBUG_TRACE, ("Passive Scan Mode!\n"));

		// Control state machine is not idle, reject the request
		if ((pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE) && (Index == 0))
			return;

		// Fill out stuff for scan request
		ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_CISCO_PASSIVE);
		MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;

		// Reset some internal control flags to make sure this scan works.
		BssTableInit(&pAd->StaCfg.CCXBssTab);
		pAd->StaCfg.ScanCnt        = 0;
		pAd->StaCfg.CCXScanChannel = pReq->Measurement.Channel;
		pAd->StaCfg.CCXScanTime    = pReq->Measurement.Duration;
		pAd->StaCfg.CCXReqType     = MSRN_TYPE_BEACON_REQ;
		DBGPRINT(RT_DEBUG_TRACE, ("Duration %d!\n", pReq->Measurement.Duration));

		// If it's non serving channel scan, send out a null frame with PSM bit on.
		if (pAd->StaCfg.CCXScanChannel != pAd->CommonCfg.Channel)
		{
			// Use MLME enqueue method
			NStatus = MlmeAllocateMemory(pAd, (PVOID)&pOutBuffer);  //Get an unused nonpaged memory
			if (NStatus	!= NDIS_STATUS_SUCCESS)
				return;

			pNullFrame = (PHEADER_802_11) pOutBuffer;
			// Make the power save Null frame with PSM bit on
			MgtMacHeaderInit(pAd, pNullFrame, SUBTYPE_NULL_FUNC, 1, pAd->CommonCfg.Bssid, pAd->CommonCfg.Bssid);
			pNullFrame->Duration 	= 0;
			pNullFrame->FC.Type     = BTYPE_DATA;
			pNullFrame->FC.PwrMgmt  = PWR_SAVE;

			// Send using priority queue
			MiniportMMRequest(pAd, 0, pOutBuffer, sizeof(HEADER_802_11));
			MlmeFreeMemory(pAd, pOutBuffer);
			DBGPRINT(RT_DEBUG_TRACE, ("Send PSM Data frame for off channel RM\n"));
			RTMPusecDelay(5000);
		}

		pAd->Mlme.AironetMachine.CurrState = AIRONET_SCANNING;
	}
	else if (pReq->Measurement.ScanMode == MSRN_SCAN_MODE_ACTIVE)
	{
		// Active scan Mode
		DBGPRINT(RT_DEBUG_TRACE, ("Active Scan Mode!\n"));

		// Control state machine is not idle, reject the request
		if (pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE)
			return;

		// Fill out stuff for scan request
		ScanParmFill(pAd, &ScanReq, ZeroSsid, 0, BSS_ANY, SCAN_CISCO_ACTIVE);
		MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_SCAN_REQ, sizeof(MLME_SCAN_REQ_STRUCT), &ScanReq);
		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_LIST_SCAN;

		// Reset some internal control flags to make sure this scan works.
		BssTableInit(&pAd->StaCfg.CCXBssTab);
		pAd->StaCfg.ScanCnt        = 0;
		pAd->StaCfg.CCXScanChannel = pReq->Measurement.Channel;
		pAd->StaCfg.CCXScanTime    = pReq->Measurement.Duration;
		pAd->StaCfg.CCXReqType     = MSRN_TYPE_BEACON_REQ;
		DBGPRINT(RT_DEBUG_TRACE, ("Duration %d!\n", pReq->Measurement.Duration));

		// If it's non serving channel scan, send out a null frame with PSM bit on.
		if (pAd->StaCfg.CCXScanChannel != pAd->CommonCfg.Channel)
		{
			// Use MLME enqueue method
			NStatus = MlmeAllocateMemory(pAd, (PVOID)&pOutBuffer);  //Get an unused nonpaged memory
			if (NStatus	!= NDIS_STATUS_SUCCESS)
				return;

			pNullFrame = (PHEADER_802_11) pOutBuffer;
			// Make the power save Null frame with PSM bit on
			MgtMacHeaderInit(pAd, pNullFrame, SUBTYPE_NULL_FUNC, 1, pAd->CommonCfg.Bssid, pAd->CommonCfg.Bssid);
			pNullFrame->Duration 	= 0;
			pNullFrame->FC.Type     = BTYPE_DATA;
			pNullFrame->FC.PwrMgmt  = PWR_SAVE;

			// Send using priority queue
			MiniportMMRequest(pAd, 0, pOutBuffer, sizeof(HEADER_802_11));
			MlmeFreeMemory(pAd, pOutBuffer);
			DBGPRINT(RT_DEBUG_TRACE, ("Send PSM Data frame for off channel RM\n"));
			RTMPusecDelay(5000);
		}

		pAd->Mlme.AironetMachine.CurrState = AIRONET_SCANNING;
	}
	else if (pReq->Measurement.ScanMode == MSRN_SCAN_MODE_BEACON_TABLE)
	{
		// Beacon report Mode, report all the APS in current bss table
		DBGPRINT(RT_DEBUG_TRACE, ("Beacon Report Mode!\n"));

		// Copy current BSS table to CCX table, we can omit this step later on.
		NdisMoveMemory(&pAd->StaCfg.CCXBssTab, &pAd->ScanTab, sizeof(BSS_TABLE));

		// Create beacon report from Bss table
		AironetCreateBeaconReportFromBssTable(pAd);

		// Set state to scanning
		pAd->Mlme.AironetMachine.CurrState = AIRONET_SCANNING;

		// Enqueue report request
		// Cisco scan request is finished, prepare beacon report
		MlmeEnqueue(pAd, AIRONET_STATE_MACHINE, MT2_AIRONET_SCAN_DONE, 0, NULL);
	}
	else
	{
		// Wrong scan Mode
		DBGPRINT(RT_DEBUG_TRACE, ("Wrong Scan Mode!\n"));
	}

	DBGPRINT(RT_DEBUG_TRACE, ("BeaconRequestAction <-----\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	AironetReportAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	MLME_QUEUE_ELEM	*Elem)
{
	PRM_REQUEST_ACTION	pReq;
	ULONG				Now32;

    NdisGetSystemUpTime(&Now32);
	pAd->StaCfg.LastBeaconRxTime = Now32;

	pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[pAd->StaCfg.CurrentRMReqIdx];

	DBGPRINT(RT_DEBUG_TRACE, ("AironetReportAction ----->\n"));

	// 1. Parse measurement type and call appropriate functions
	if (pReq->ReqElem.Type == MSRN_TYPE_CHANNEL_LOAD_REQ)
		// Channel Load measurement request
		ChannelLoadReportAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else if (pReq->ReqElem.Type == MSRN_TYPE_NOISE_HIST_REQ)
		// Noise Histogram measurement request
		NoiseHistReportAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else if (pReq->ReqElem.Type == MSRN_TYPE_BEACON_REQ)
		// Beacon measurement request
		BeaconReportAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
	else
		// Unknown. Do nothing and return
		;

	// 2. Point to the correct index of action element, start from 0
	pAd->StaCfg.CurrentRMReqIdx++;

	// 3. Check for parallel actions
	if (pAd->StaCfg.ParallelReq == TRUE)
	{
		pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[pAd->StaCfg.CurrentRMReqIdx];

		// Process next action right away
		if (pReq->ReqElem.Type == MSRN_TYPE_CHANNEL_LOAD_REQ)
			// Channel Load measurement request
			ChannelLoadReportAction(pAd, pAd->StaCfg.CurrentRMReqIdx);
		else if (pReq->ReqElem.Type == MSRN_TYPE_NOISE_HIST_REQ)
			// Noise Histogram measurement request
			NoiseHistReportAction(pAd, pAd->StaCfg.CurrentRMReqIdx);

		pAd->StaCfg.ParallelReq = FALSE;
		pAd->StaCfg.CurrentRMReqIdx++;
	}

	if (pAd->StaCfg.CurrentRMReqIdx >= pAd->StaCfg.RMReqCnt)
	{
		// 4. There is no more unprocessed measurement request, go for transmit this report
		AironetFinalReportAction(pAd);
		pAd->Mlme.AironetMachine.CurrState = AIRONET_IDLE;
	}
	else
	{
		pReq = (PRM_REQUEST_ACTION) &pAd->StaCfg.MeasurementRequest[pAd->StaCfg.CurrentRMReqIdx];

		if (pReq->Measurement.Channel != pAd->CommonCfg.Channel)
		{
			RTMPusecDelay(100000);
		}

		// 5. There are more requests to be measure
		MlmeEnqueue(pAd, AIRONET_STATE_MACHINE, MT2_AIRONET_SCAN_REQ, 0, NULL);
		RT28XX_MLME_HANDLER(pAd);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("AironetReportAction <-----\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	AironetFinalReportAction(
	IN	PRTMP_ADAPTER	pAd)
{
	PUCHAR					pDest;
	PAIRONET_IAPP_HEADER	pIAPP;
	PHEADER_802_11			pHeader;
	UCHAR					AckRate = RATE_2;
	USHORT					AckDuration = 0;
	NDIS_STATUS				NStatus;
	PUCHAR					pOutBuffer = NULL;
	ULONG					FrameLen = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("AironetFinalReportAction ----->\n"));

	// 0. Set up the frame pointer, Frame was inited at the end of message action
	pDest = &pAd->StaCfg.FrameReportBuf[LENGTH_802_11];

	// 1. Update report IAPP fields
	pIAPP = (PAIRONET_IAPP_HEADER) pDest;

	// 2. Copy Cisco SNAP header
	NdisMoveMemory(pIAPP->CiscoSnapHeader, SNAP_AIRONET, LENGTH_802_1_H);

	// 3. network order for this 16bit length
	pIAPP->Length  = cpu2be16(pAd->StaCfg.FrameReportLen - LENGTH_802_11 - LENGTH_802_1_H);

	// 3.1 sanity check the report length, ignore it if there is nothing to report
	if (be2cpu16(pIAPP->Length) <= 18)
		return;

	// 4. Type must be 0x32
	pIAPP->Type    = AIRONET_IAPP_TYPE;

	// 5. SubType for report must be 0x81
	pIAPP->SubType = AIRONET_IAPP_SUBTYPE_REPORT;

	// 6. DA is not used and must be zero, although the whole frame was cleared at the start of function
	//    We will do it again here. We can use BSSID instead
	COPY_MAC_ADDR(pIAPP->DA, pAd->CommonCfg.Bssid);

	// 7. SA is the client reporting which must be our MAC
	COPY_MAC_ADDR(pIAPP->SA, pAd->CurrentAddress);

	// 8. Copy the saved dialog token
	pIAPP->Token = pAd->StaCfg.IAPPToken;

	// 9. Make the Report frame 802.11 header
	//    Reuse function in wpa.c
	pHeader = (PHEADER_802_11) pAd->StaCfg.FrameReportBuf;
	pAd->Sequence ++;
	WpaMacHeaderInit(pAd, pHeader, 0, pAd->CommonCfg.Bssid);

	// ACK size	is 14 include CRC, and its rate	is based on real time information
	AckRate     = pAd->CommonCfg.ExpectedACKRate[pAd->CommonCfg.MlmeRate];
	AckDuration = RTMPCalcDuration(pAd, AckRate, 14);
	pHeader->Duration = pAd->CommonCfg.Dsifs + AckDuration;

	// Use MLME enqueue method
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);  //Get an unused nonpaged memory
	if (NStatus	!= NDIS_STATUS_SUCCESS)
		return;

	// 10. Prepare report frame with dynamic outbuffer. Just simply copy everything.
	MakeOutgoingFrame(pOutBuffer,                       &FrameLen,
	                  pAd->StaCfg.FrameReportLen, pAd->StaCfg.FrameReportBuf,
		              END_OF_ARGS);

	// 11. Send using priority queue
	MiniportMMRequest(pAd, 0, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	pAd->StaCfg.CCXReqType = MSRN_TYPE_UNUSED;

	DBGPRINT(RT_DEBUG_TRACE, ("AironetFinalReportAction <-----\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	ChannelLoadReportAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PMEASUREMENT_REPORT_ELEMENT	pReport;
	PCHANNEL_LOAD_REPORT		pLoad;
	PUCHAR						pDest;
	UCHAR						CCABusyFraction;

	DBGPRINT(RT_DEBUG_TRACE, ("ChannelLoadReportAction ----->\n"));

	// Disable Rx with promiscuous reception, make it back to normal
	RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, STANORMAL); // Staion not drop control frame will fail WiFi Certification.

	// 0. Setup pointer for processing beacon & probe response
	pDest = (PUCHAR) &pAd->StaCfg.FrameReportBuf[pAd->StaCfg.FrameReportLen];
	pReport = (PMEASUREMENT_REPORT_ELEMENT) pDest;

	// 1. Fill Measurement report element field.
	pReport->Eid    = IE_MEASUREMENT_REPORT;
	// Fixed Length at 9, not include Eid and length fields
	pReport->Length = 9;
	pReport->Token  = pAd->StaCfg.MeasurementRequest[Index].ReqElem.Token;
	pReport->Mode   = pAd->StaCfg.MeasurementRequest[Index].ReqElem.Mode;
	pReport->Type   = MSRN_TYPE_CHANNEL_LOAD_REQ;

	// 2. Fill channel report measurement data
	pDest += sizeof(MEASUREMENT_REPORT_ELEMENT);
	pLoad  = (PCHANNEL_LOAD_REPORT) pDest;
	pLoad->Channel  = pAd->StaCfg.MeasurementRequest[Index].Measurement.Channel;
	pLoad->Spare    = 0;
	pLoad->Duration = pAd->StaCfg.MeasurementRequest[Index].Measurement.Duration;

	// 3. Calculate the CCA Busy Fraction
	//    (Bytes + ACK size) * 8 / Tx speed * 255 / 1000 / measurement duration, use 24 us Tx speed
	//     =  (Bytes + ACK) / 12 / duration
	//     9 is the good value for pAd->StaCfg.CLFactor
	// CCABusyFraction = (UCHAR) (pAd->StaCfg.CLBusyBytes / 9 / pLoad->Duration);
	CCABusyFraction = (UCHAR) (pAd->StaCfg.CLBusyBytes / pAd->StaCfg.CLFactor / pLoad->Duration);
	if (CCABusyFraction < 10)
			CCABusyFraction = (UCHAR) (pAd->StaCfg.CLBusyBytes / 3 / pLoad->Duration) + 1;

	pLoad->CCABusy = CCABusyFraction;
	DBGPRINT(RT_DEBUG_TRACE, ("CLBusyByte %ld, Duration %d, Result, %d\n", pAd->StaCfg.CLBusyBytes, pLoad->Duration, CCABusyFraction));

	DBGPRINT(RT_DEBUG_TRACE, ("FrameReportLen %d\n", pAd->StaCfg.FrameReportLen));
	pAd->StaCfg.FrameReportLen += (sizeof(MEASUREMENT_REPORT_ELEMENT) + sizeof(CHANNEL_LOAD_REPORT));
	DBGPRINT(RT_DEBUG_TRACE, ("FrameReportLen %d\n", pAd->StaCfg.FrameReportLen));

	// 4. Clear channel load measurement flag
	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_MEASUREMENT);

	// 5. reset to idle state
	pAd->Mlme.AironetMachine.CurrState = AIRONET_IDLE;

	DBGPRINT(RT_DEBUG_TRACE, ("ChannelLoadReportAction <-----\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	NoiseHistReportAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	PMEASUREMENT_REPORT_ELEMENT	pReport;
	PNOISE_HIST_REPORT			pNoise;
	PUCHAR						pDest;
	UCHAR						i,NoiseCnt;
	USHORT						TotalRPICnt, TotalRPISum;

	DBGPRINT(RT_DEBUG_TRACE, ("NoiseHistReportAction ----->\n"));

	// 0. Disable Rx with promiscuous reception, make it back to normal
	RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, STANORMAL); // Staion not drop control frame will fail WiFi Certification.
	// 1. Setup pointer for processing beacon & probe response
	pDest = (PUCHAR) &pAd->StaCfg.FrameReportBuf[pAd->StaCfg.FrameReportLen];
	pReport = (PMEASUREMENT_REPORT_ELEMENT) pDest;

	// 2. Fill Measurement report element field.
	pReport->Eid    = IE_MEASUREMENT_REPORT;
	// Fixed Length at 16, not include Eid and length fields
	pReport->Length = 16;
	pReport->Token  = pAd->StaCfg.MeasurementRequest[Index].ReqElem.Token;
	pReport->Mode   = pAd->StaCfg.MeasurementRequest[Index].ReqElem.Mode;
	pReport->Type   = MSRN_TYPE_NOISE_HIST_REQ;

	// 3. Fill noise histogram report measurement data
	pDest += sizeof(MEASUREMENT_REPORT_ELEMENT);
	pNoise  = (PNOISE_HIST_REPORT) pDest;
	pNoise->Channel  = pAd->StaCfg.MeasurementRequest[Index].Measurement.Channel;
	pNoise->Spare    = 0;
	pNoise->Duration = pAd->StaCfg.MeasurementRequest[Index].Measurement.Duration;
	// 4. Fill Noise histogram, the total RPI counts should be 0.4 * TU
	//    We estimate 4000 normal packets received durning 10 seconds test.
	//    Adjust it if required.
	// 3 is a good value for pAd->StaCfg.NHFactor
	// TotalRPICnt = pNoise->Duration * 3 / 10;
	TotalRPICnt = pNoise->Duration * pAd->StaCfg.NHFactor / 10;
	TotalRPISum = 0;

	for (i = 0; i < 8; i++)
	{
		TotalRPISum += pAd->StaCfg.RPIDensity[i];
		DBGPRINT(RT_DEBUG_TRACE, ("RPI %d Conuts %d\n", i, pAd->StaCfg.RPIDensity[i]));
	}

	// Double check if the counter is larger than our expectation.
	// We will replace it with the total number plus a fraction.
	if (TotalRPISum > TotalRPICnt)
		TotalRPICnt = TotalRPISum + pNoise->Duration / 20;

	DBGPRINT(RT_DEBUG_TRACE, ("Total RPI Conuts %d\n", TotalRPICnt));

	// 5. Initialize noise count for the total summation of 0xff
	NoiseCnt = 0;
	for (i = 1; i < 8; i++)
	{
		pNoise->Density[i] = (UCHAR) (pAd->StaCfg.RPIDensity[i] * 255 / TotalRPICnt);
		if ((pNoise->Density[i] == 0) && (pAd->StaCfg.RPIDensity[i] != 0))
			pNoise->Density[i]++;
		NoiseCnt += pNoise->Density[i];
		DBGPRINT(RT_DEBUG_TRACE, ("Reported RPI[%d]  = 0x%02x\n", i, pNoise->Density[i]));
	}

	// 6. RPI[0] represents the rest of counts
	pNoise->Density[0] = 0xff - NoiseCnt;
	DBGPRINT(RT_DEBUG_TRACE, ("Reported RPI[0]  = 0x%02x\n", pNoise->Density[0]));

	pAd->StaCfg.FrameReportLen += (sizeof(MEASUREMENT_REPORT_ELEMENT) + sizeof(NOISE_HIST_REPORT));

	// 7. Clear channel load measurement flag
	RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RADIO_MEASUREMENT);

	// 8. reset to idle state
	pAd->Mlme.AironetMachine.CurrState = AIRONET_IDLE;

	DBGPRINT(RT_DEBUG_TRACE, ("NoiseHistReportAction <-----\n"));
}

/*
	========================================================================

	Routine Description:
		Prepare Beacon report action,

	Arguments:
		pAd	Pointer	to our adapter

	Return Value:
		None

	Note:

	========================================================================
*/
VOID	BeaconReportAction(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Index)
{
	DBGPRINT(RT_DEBUG_TRACE, ("BeaconReportAction ----->\n"));

	// Looks like we don't have anything thing need to do here.
	// All measurement report already finished in AddBeaconReport
	// The length is in the FrameReportLen

	// reset Beacon index for next beacon request
	pAd->StaCfg.LastBssIndex = 0xff;

	// reset to idle state
	pAd->Mlme.AironetMachine.CurrState = AIRONET_IDLE;

	DBGPRINT(RT_DEBUG_TRACE, ("BeaconReportAction <-----\n"));
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Index		Current BSSID in CCXBsstab entry index

	Return Value:

	Note:

	========================================================================
*/
VOID	AironetAddBeaconReport(
	IN	PRTMP_ADAPTER		pAd,
	IN	ULONG				Index,
	IN	PMLME_QUEUE_ELEM	pElem)
{
	PVOID						pMsg;
	PUCHAR						pSrc, pDest;
	UCHAR						ReqIdx;
	ULONG						MsgLen;
	USHORT						Length;
	PFRAME_802_11				pFrame;
	PMEASUREMENT_REPORT_ELEMENT	pReport;
	PEID_STRUCT			        pEid;
	PBEACON_REPORT				pBeaconReport;
	PBSS_ENTRY					pBss;

	// 0. Setup pointer for processing beacon & probe response
	pMsg   = pElem->Msg;
	MsgLen = pElem->MsgLen;
	pFrame = (PFRAME_802_11) pMsg;
	pSrc   = pFrame->Octet;				// Start from AP TSF
	pBss   = (PBSS_ENTRY) &pAd->StaCfg.CCXBssTab.BssEntry[Index];
	ReqIdx = pAd->StaCfg.CurrentRMReqIdx;

	// 1 Check the Index, if we already create this entry, only update the average RSSI
	if ((Index <= pAd->StaCfg.LastBssIndex) && (pAd->StaCfg.LastBssIndex != 0xff))
	{
		pDest  = (PUCHAR) &pAd->StaCfg.FrameReportBuf[pAd->StaCfg.BssReportOffset[Index]];
		// Point to bss report information
		pDest += sizeof(MEASUREMENT_REPORT_ELEMENT);
		pBeaconReport = (PBEACON_REPORT) pDest;

		// Update Rx power, in dBm
		// Get the original RSSI readback from BBP
		pBeaconReport->RxPower += pAd->BbpRssiToDbmDelta;
		// Average the Rssi reading
		pBeaconReport->RxPower  = (pBeaconReport->RxPower + pBss->Rssi) / 2;
		// Get to dBm format
		pBeaconReport->RxPower -= pAd->BbpRssiToDbmDelta;

		DBGPRINT(RT_DEBUG_TRACE, ("Bssid %02x:%02x:%02x:%02x:%02x:%02x ",
			pBss->Bssid[0], pBss->Bssid[1], pBss->Bssid[2],
			pBss->Bssid[3], pBss->Bssid[4], pBss->Bssid[5]));
		DBGPRINT(RT_DEBUG_TRACE, ("RxPower[%ld] Rssi %d, Avg Rssi %d\n", Index, (pBss->Rssi - pAd->BbpRssiToDbmDelta), pBeaconReport->RxPower - 256));
		DBGPRINT(RT_DEBUG_TRACE, ("FrameReportLen = %d\n", pAd->StaCfg.BssReportOffset[Index]));

		// Update other information here

		// Done
		return;
	}

	// 2. Update reported Index
	pAd->StaCfg.LastBssIndex = Index;

	// 3. Setup the buffer address for copying this BSSID into reporting frame
	//    The offset should start after 802.11 header and report frame header.
	pDest = (PUCHAR) &pAd->StaCfg.FrameReportBuf[pAd->StaCfg.FrameReportLen];

	// 4. Save the start offset of each Bss in report frame
	pAd->StaCfg.BssReportOffset[Index] = pAd->StaCfg.FrameReportLen;

	// 5. Fill Measurement report fields
	pReport = (PMEASUREMENT_REPORT_ELEMENT) pDest;
	pReport->Eid = IE_MEASUREMENT_REPORT;
	pReport->Length = 0;
	pReport->Token  = pAd->StaCfg.MeasurementRequest[ReqIdx].ReqElem.Token;
	pReport->Mode   = pAd->StaCfg.MeasurementRequest[ReqIdx].ReqElem.Mode;
	pReport->Type   = MSRN_TYPE_BEACON_REQ;
	Length          = sizeof(MEASUREMENT_REPORT_ELEMENT);
	pDest          += sizeof(MEASUREMENT_REPORT_ELEMENT);

	// 6. Start thebeacon report format
	pBeaconReport = (PBEACON_REPORT) pDest;
	pDest        += sizeof(BEACON_REPORT);
	Length       += sizeof(BEACON_REPORT);

	// 7. Copy Channel number
	pBeaconReport->Channel        = pBss->Channel;
	pBeaconReport->Spare          = 0;
	pBeaconReport->Duration       = pAd->StaCfg.MeasurementRequest[ReqIdx].Measurement.Duration;
	pBeaconReport->PhyType        = ((pBss->SupRateLen+pBss->ExtRateLen > 4) ? PHY_ERP : PHY_DSS);
	// 8. Rx power, in dBm
	pBeaconReport->RxPower        = pBss->Rssi - pAd->BbpRssiToDbmDelta;

	DBGPRINT(RT_DEBUG_TRACE, ("Bssid %02x:%02x:%02x:%02x:%02x:%02x ",
		pBss->Bssid[0], pBss->Bssid[1], pBss->Bssid[2],
		pBss->Bssid[3], pBss->Bssid[4], pBss->Bssid[5]));
	DBGPRINT(RT_DEBUG_TRACE, ("RxPower[%ld], Rssi %d\n", Index, pBeaconReport->RxPower - 256));
	DBGPRINT(RT_DEBUG_TRACE, ("FrameReportLen = %d\n", pAd->StaCfg.FrameReportLen));

	pBeaconReport->BeaconInterval = pBss->BeaconPeriod;
	COPY_MAC_ADDR(pBeaconReport->BSSID, pFrame->Hdr.Addr3);
	NdisMoveMemory(pBeaconReport->ParentTSF, pSrc, 4);
	NdisMoveMemory(pBeaconReport->TargetTSF, &pElem->TimeStamp.u.LowPart, 4);
	NdisMoveMemory(&pBeaconReport->TargetTSF[4], &pElem->TimeStamp.u.HighPart, 4);

	// 9. Skip the beacon frame and offset to start of capabilityinfo since we already processed capabilityinfo
	pSrc += (TIMESTAMP_LEN + 2);
	pBeaconReport->CapabilityInfo = *(USHORT *)pSrc;

	// 10. Point to start of element ID
	pSrc += 2;
	pEid = (PEID_STRUCT) pSrc;

	// 11. Start process all variable Eid oayload and add the appropriate to the frame report
	while (((PUCHAR) pEid + pEid->Len + 1) < ((PUCHAR) pFrame + MsgLen))
	{
		// Only limited EID are required to report for CCX 2. It includes SSID, Supported rate,
		// FH paramenter set, DS parameter set, CF parameter set, IBSS parameter set,
		// TIM (report first 4 bytes only, radio measurement capability
		switch (pEid->Eid)
		{
			case IE_SSID:
			case IE_SUPP_RATES:
			case IE_FH_PARM:
			case IE_DS_PARM:
			case IE_CF_PARM:
			case IE_IBSS_PARM:
				NdisMoveMemory(pDest, pEid, pEid->Len + 2);
				pDest  += (pEid->Len + 2);
				Length += (pEid->Len + 2);
				break;

			case IE_MEASUREMENT_CAPABILITY:
				// Since this IE is duplicated with WPA security IE, we has to do sanity check before
				// recognize it.
				// 1. It also has fixed 6 bytes IE length.
				if (pEid->Len != 6)
					break;
				// 2. Check the Cisco Aironet OUI
				if (NdisEqualMemory(CISCO_OUI, (pSrc + 2), 3))
				{
					// Matched, this is what we want
					NdisMoveMemory(pDest, pEid, pEid->Len + 2);
					pDest  += (pEid->Len + 2);
					Length += (pEid->Len + 2);
				}
				break;

			case IE_TIM:
				if (pEid->Len > 4)
				{
					// May truncate and report the first 4 bytes only, with the eid & len, total should be 6
					NdisMoveMemory(pDest, pEid, 6);
					pDest  += 6;
					Length += 6;
				}
				else
				{
					NdisMoveMemory(pDest, pEid, pEid->Len + 2);
					pDest  += (pEid->Len + 2);
					Length += (pEid->Len + 2);
				}
				break;

			default:
				break;
		}
		// 12. Move to next element ID
		pSrc += (2 + pEid->Len);
		pEid = (PEID_STRUCT) pSrc;
	}

	// 13. Update the length in the header, not include EID and length
	pReport->Length = Length - 4;

	// 14. Update the frame report buffer data length
	pAd->StaCfg.FrameReportLen += Length;
	DBGPRINT(RT_DEBUG_TRACE, ("FR len = %d\n", pAd->StaCfg.FrameReportLen));
}

/*
	========================================================================

	Routine Description:

	Arguments:
		Index		Current BSSID in CCXBsstab entry index

	Return Value:

	Note:

	========================================================================
*/
VOID	AironetCreateBeaconReportFromBssTable(
	IN	PRTMP_ADAPTER		pAd)
{
	PMEASUREMENT_REPORT_ELEMENT	pReport;
	PBEACON_REPORT				pBeaconReport;
	UCHAR						Index, ReqIdx;
	USHORT						Length;
	PUCHAR						pDest;
	PBSS_ENTRY					pBss;

	// 0. setup base pointer
	ReqIdx = pAd->StaCfg.CurrentRMReqIdx;

	for (Index = 0; Index < pAd->StaCfg.CCXBssTab.BssNr; Index++)
	{
		// 1. Setup the buffer address for copying this BSSID into reporting frame
		//    The offset should start after 802.11 header and report frame header.
		pDest  = (PUCHAR) &pAd->StaCfg.FrameReportBuf[pAd->StaCfg.FrameReportLen];
		pBss   = (PBSS_ENTRY) &pAd->StaCfg.CCXBssTab.BssEntry[Index];
		Length = 0;

		// 2. Fill Measurement report fields
		pReport         = (PMEASUREMENT_REPORT_ELEMENT) pDest;
		pReport->Eid    = IE_MEASUREMENT_REPORT;
		pReport->Length = 0;
		pReport->Token  = pAd->StaCfg.MeasurementRequest[ReqIdx].ReqElem.Token;
		pReport->Mode   = pAd->StaCfg.MeasurementRequest[ReqIdx].ReqElem.Mode;
		pReport->Type   = MSRN_TYPE_BEACON_REQ;
		Length          = sizeof(MEASUREMENT_REPORT_ELEMENT);
		pDest          += sizeof(MEASUREMENT_REPORT_ELEMENT);

		// 3. Start the beacon report format
		pBeaconReport = (PBEACON_REPORT) pDest;
		pDest        += sizeof(BEACON_REPORT);
		Length       += sizeof(BEACON_REPORT);

		// 4. Copy Channel number
		pBeaconReport->Channel        = pBss->Channel;
		pBeaconReport->Spare          = 0;
		pBeaconReport->Duration       = pAd->StaCfg.MeasurementRequest[ReqIdx].Measurement.Duration;
		pBeaconReport->PhyType        = ((pBss->SupRateLen+pBss->ExtRateLen > 4) ? PHY_ERP : PHY_DSS);
		pBeaconReport->RxPower        = pBss->Rssi - pAd->BbpRssiToDbmDelta;
		pBeaconReport->BeaconInterval = pBss->BeaconPeriod;
		pBeaconReport->CapabilityInfo = pBss->CapabilityInfo;
		COPY_MAC_ADDR(pBeaconReport->BSSID, pBss->Bssid);
		NdisMoveMemory(pBeaconReport->ParentTSF, pBss->PTSF, 4);
		NdisMoveMemory(pBeaconReport->TargetTSF, pBss->TTSF, 8);

		// 5. Create SSID
		*pDest++ = 0x00;
		*pDest++ = pBss->SsidLen;
		NdisMoveMemory(pDest, pBss->Ssid, pBss->SsidLen);
		pDest  += pBss->SsidLen;
		Length += (2 + pBss->SsidLen);

		// 6. Create SupportRates
		*pDest++ = 0x01;
		*pDest++ = pBss->SupRateLen;
		NdisMoveMemory(pDest, pBss->SupRate, pBss->SupRateLen);
		pDest  += pBss->SupRateLen;
		Length += (2 + pBss->SupRateLen);

		// 7. DS Parameter
		*pDest++ = 0x03;
		*pDest++ = 1;
		*pDest++ = pBss->Channel;
		Length  += 3;

		// 8. IBSS parameter if presents
		if (pBss->BssType == BSS_ADHOC)
		{
			*pDest++ = 0x06;
			*pDest++ = 2;
			*(PUSHORT) pDest = pBss->AtimWin;
			pDest   += 2;
			Length  += 4;
		}

		// 9. Update length field, not include EID and length
		pReport->Length = Length - 4;

		// 10. Update total frame size
		pAd->StaCfg.FrameReportLen += Length;
	}
}
