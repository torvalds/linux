/********************************************************************************************************************************
 * This file is created to process BA Action Frame. According to 802.11 spec, there are 3 BA action types at all. And as BA is
 * related to TS, this part need some structure defined in QOS side code. Also TX RX is going to be resturctured, so how to send
 * ADDBAREQ ADDBARSP and DELBA packet is still on consideration. Temporarily use MANAGE QUEUE instead of Normal Queue.
 * WB 2008-05-27
 * *****************************************************************************************************************************/
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include "ieee80211.h"
#include "rtl819x_BA.h"

/********************************************************************************************************************
 *function:  Activate BA entry. And if Time is nozero, start timer.
 *   input:  PBA_RECORD			pBA  //BA entry to be enabled
 *	     u16			Time //indicate time delay.
 *  output:  none
********************************************************************************************************************/
static void ActivateBAEntry(struct ieee80211_device *ieee, PBA_RECORD pBA, u16 Time)
{
	pBA->bValid = true;
	if(Time != 0)
		mod_timer(&pBA->Timer, jiffies + MSECS(Time));
}

/********************************************************************************************************************
 *function:  deactivate BA entry, including its timer.
 *   input:  PBA_RECORD			pBA  //BA entry to be disabled
 *  output:  none
********************************************************************************************************************/
static void DeActivateBAEntry(struct ieee80211_device *ieee, PBA_RECORD pBA)
{
	pBA->bValid = false;
	del_timer_sync(&pBA->Timer);
}
/********************************************************************************************************************
 *function: deactivete BA entry in Tx Ts, and send DELBA.
 *   input:
 *	     PTX_TS_RECORD		pTxTs //Tx Ts which is to deactivate BA entry.
 *  output:  none
 *  notice:  As PTX_TS_RECORD structure will be defined in QOS, so wait to be merged. //FIXME
********************************************************************************************************************/
static u8 TxTsDeleteBA(struct ieee80211_device *ieee, PTX_TS_RECORD pTxTs)
{
	PBA_RECORD		pAdmittedBa = &pTxTs->TxAdmittedBARecord;  //These two BA entries must exist in TS structure
	PBA_RECORD		pPendingBa = &pTxTs->TxPendingBARecord;
	u8			bSendDELBA = false;

	// Delete pending BA
	if (pPendingBa->bValid)
	{
		DeActivateBAEntry(ieee, pPendingBa);
		bSendDELBA = true;
	}

	// Delete admitted BA
	if (pAdmittedBa->bValid)
	{
		DeActivateBAEntry(ieee, pAdmittedBa);
		bSendDELBA = true;
	}

	return bSendDELBA;
}

/********************************************************************************************************************
 *function: deactivete BA entry in Tx Ts, and send DELBA.
 *   input:
 *	     PRX_TS_RECORD		pRxTs //Rx Ts which is to deactivate BA entry.
 *  output:  none
 *  notice:  As PRX_TS_RECORD structure will be defined in QOS, so wait to be merged. //FIXME, same with above
********************************************************************************************************************/
static u8 RxTsDeleteBA(struct ieee80211_device *ieee, PRX_TS_RECORD pRxTs)
{
	PBA_RECORD		pBa = &pRxTs->RxAdmittedBARecord;
	u8			bSendDELBA = false;

	if (pBa->bValid)
	{
		DeActivateBAEntry(ieee, pBa);
		bSendDELBA = true;
	}

	return bSendDELBA;
}

/********************************************************************************************************************
 *function: reset BA entry
 *   input:
 *	     PBA_RECORD		pBA //entry to be reset
 *  output:  none
********************************************************************************************************************/
void ResetBaEntry(PBA_RECORD pBA)
{
	pBA->bValid			= false;
	pBA->BaParamSet.shortData	= 0;
	pBA->BaTimeoutValue		= 0;
	pBA->DialogToken		= 0;
	pBA->BaStartSeqCtrl.ShortData	= 0;
}
//These functions need porting here or not?
/*******************************************************************************************************************************
 *function:  construct ADDBAREQ and ADDBARSP frame here together.
 *   input:  u8*		Dst	//ADDBA frame's destination
 *	     PBA_RECORD		pBA	//BA_RECORD entry which stores the necessary information for BA.
 *	     u16		StatusCode  //status code in RSP and I will use it to indicate whether it's RSP or REQ(will I?)
 *	     u8			type	//indicate whether it's RSP(ACT_ADDBARSP) ow REQ(ACT_ADDBAREQ)
 *  output:  none
 *  return:  sk_buff*		skb     //return constructed skb to xmit
*******************************************************************************************************************************/
static struct sk_buff *ieee80211_ADDBA(struct ieee80211_device *ieee, u8 *Dst, PBA_RECORD pBA, u16 StatusCode, u8 type)
{
	struct sk_buff *skb = NULL;
	 struct rtl_80211_hdr_3addr *BAReq = NULL;
	u8 *tag = NULL;
	u16 len = ieee->tx_headroom + 9;
	//category(1) + action field(1) + Dialog Token(1) + BA Parameter Set(2) +  BA Timeout Value(2) +  BA Start SeqCtrl(2)(or StatusCode(2))
	IEEE80211_DEBUG(IEEE80211_DL_TRACE | IEEE80211_DL_BA, "========>%s(), frame(%d) sentd to:%pM, ieee->dev:%p\n", __func__, type, Dst, ieee->dev);
	if (pBA == NULL)
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "pBA is NULL\n");
		return NULL;
	}
	skb = dev_alloc_skb(len + sizeof( struct rtl_80211_hdr_3addr)); //need to add something others? FIXME
	if (skb == NULL)
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't alloc skb for ADDBA_REQ\n");
		return NULL;
	}

	memset(skb->data, 0, sizeof( struct rtl_80211_hdr_3addr));	//I wonder whether it's necessary. Apparently kernel will not do it when alloc a skb.
	skb_reserve(skb, ieee->tx_headroom);

	BAReq = ( struct rtl_80211_hdr_3addr *) skb_put(skb,sizeof( struct rtl_80211_hdr_3addr));

	memcpy(BAReq->addr1, Dst, ETH_ALEN);
	memcpy(BAReq->addr2, ieee->dev->dev_addr, ETH_ALEN);

	memcpy(BAReq->addr3, ieee->current_network.bssid, ETH_ALEN);

	BAReq->frame_ctl = cpu_to_le16(IEEE80211_STYPE_MANAGE_ACT); //action frame

	//tag += sizeof( struct rtl_80211_hdr_3addr); //move to action field
	tag = (u8 *)skb_put(skb, 9);
	*tag ++= ACT_CAT_BA;
	*tag ++= type;
	// Dialog Token
	*tag ++= pBA->DialogToken;

	if (ACT_ADDBARSP == type)
	{
		// Status Code
		printk("=====>to send ADDBARSP\n");

		put_unaligned_le16(StatusCode, tag);
		tag += 2;
	}
	// BA Parameter Set

	put_unaligned_le16(pBA->BaParamSet.shortData, tag);
	tag += 2;
	// BA Timeout Value

	put_unaligned_le16(pBA->BaTimeoutValue, tag);
	tag += 2;

	if (ACT_ADDBAREQ == type)
	{
	// BA Start SeqCtrl
		memcpy(tag, (u8 *)&(pBA->BaStartSeqCtrl), 2);
		tag += 2;
	}

	IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA|IEEE80211_DL_BA, skb->data, skb->len);
	return skb;
	//return NULL;
}


/********************************************************************************************************************
 *function:  construct DELBA frame
 *   input:  u8*		dst	//DELBA frame's destination
 *	     PBA_RECORD		pBA	//BA_RECORD entry which stores the necessary information for BA
 *	     TR_SELECT	        TxRxSelect  //TX RX direction
 *	     u16		ReasonCode  //status code.
 *  output:  none
 *  return:  sk_buff*		skb     //return constructed skb to xmit
********************************************************************************************************************/
static struct sk_buff *ieee80211_DELBA(
	struct ieee80211_device  *ieee,
	u8		         *dst,
	PBA_RECORD		 pBA,
	TR_SELECT		 TxRxSelect,
	u16			 ReasonCode
	)
{
	DELBA_PARAM_SET	DelbaParamSet;
	struct sk_buff *skb = NULL;
	 struct rtl_80211_hdr_3addr *Delba = NULL;
	u8 *tag = NULL;
	//len = head len + DELBA Parameter Set(2) + Reason Code(2)
	u16 len = 6 + ieee->tx_headroom;

	if (net_ratelimit())
	IEEE80211_DEBUG(IEEE80211_DL_TRACE | IEEE80211_DL_BA, "========>%s(), ReasonCode(%d) sentd to:%pM\n", __func__, ReasonCode, dst);

	memset(&DelbaParamSet, 0, 2);

	DelbaParamSet.field.Initiator	= (TxRxSelect==TX_DIR)?1:0;
	DelbaParamSet.field.TID	= pBA->BaParamSet.field.TID;

	skb = dev_alloc_skb(len + sizeof( struct rtl_80211_hdr_3addr)); //need to add something others? FIXME
	if (skb == NULL)
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't alloc skb for ADDBA_REQ\n");
		return NULL;
	}
//	memset(skb->data, 0, len+sizeof( struct rtl_80211_hdr_3addr));
	skb_reserve(skb, ieee->tx_headroom);

	Delba = ( struct rtl_80211_hdr_3addr *) skb_put(skb,sizeof( struct rtl_80211_hdr_3addr));

	memcpy(Delba->addr1, dst, ETH_ALEN);
	memcpy(Delba->addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(Delba->addr3, ieee->current_network.bssid, ETH_ALEN);
	Delba->frame_ctl = cpu_to_le16(IEEE80211_STYPE_MANAGE_ACT); //action frame

	tag = (u8 *)skb_put(skb, 6);

	*tag ++= ACT_CAT_BA;
	*tag ++= ACT_DELBA;

	// DELBA Parameter Set

	put_unaligned_le16(DelbaParamSet.shortData, tag);
	tag += 2;
	// Reason Code

	put_unaligned_le16(ReasonCode, tag);
	tag += 2;

	IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA|IEEE80211_DL_BA, skb->data, skb->len);
	if (net_ratelimit())
	IEEE80211_DEBUG(IEEE80211_DL_TRACE | IEEE80211_DL_BA, "<=====%s()\n", __func__);
	return skb;
}

/********************************************************************************************************************
 *function: send ADDBAReq frame out
 *   input:  u8*		dst	//ADDBAReq frame's destination
 *	     PBA_RECORD		pBA	//BA_RECORD entry which stores the necessary information for BA
 *  output:  none
 *  notice: If any possible, please hide pBA in ieee. And temporarily use Manage Queue as softmac_mgmt_xmit() usually does
********************************************************************************************************************/
static void ieee80211_send_ADDBAReq(struct ieee80211_device *ieee,
				    u8 *dst, PBA_RECORD pBA)
{
	struct sk_buff *skb = NULL;
	skb = ieee80211_ADDBA(ieee, dst, pBA, 0, ACT_ADDBAREQ); //construct ACT_ADDBAREQ frames so set statuscode zero.

	if (skb)
	{
		softmac_mgmt_xmit(skb, ieee);
		//add statistic needed here.
		//and skb will be freed in softmac_mgmt_xmit(), so omit all dev_kfree_skb_any() outside softmac_mgmt_xmit()
		//WB
	}
	else
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "alloc skb error in function %s()\n", __func__);
	}
	return;
}

/********************************************************************************************************************
 *function: send ADDBARSP frame out
 *   input:  u8*		dst	//DELBA frame's destination
 *	     PBA_RECORD		pBA	//BA_RECORD entry which stores the necessary information for BA
 *	     u16		StatusCode //RSP StatusCode
 *  output:  none
 *  notice: If any possible, please hide pBA in ieee. And temporarily use Manage Queue as softmac_mgmt_xmit() usually does
********************************************************************************************************************/
static void ieee80211_send_ADDBARsp(struct ieee80211_device *ieee, u8 *dst,
				    PBA_RECORD pBA, u16 StatusCode)
{
	struct sk_buff *skb = NULL;
	skb = ieee80211_ADDBA(ieee, dst, pBA, StatusCode, ACT_ADDBARSP); //construct ACT_ADDBARSP frames
	if (skb)
	{
		softmac_mgmt_xmit(skb, ieee);
		//same above
	}
	else
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "alloc skb error in function %s()\n", __func__);
	}

	return;

}
/********************************************************************************************************************
 *function: send ADDBARSP frame out
 *   input:  u8*		dst	//DELBA frame's destination
 *	     PBA_RECORD		pBA	//BA_RECORD entry which stores the necessary information for BA
 *	     TR_SELECT          TxRxSelect //TX or RX
 *	     u16		ReasonCode //DEL ReasonCode
 *  output:  none
 *  notice: If any possible, please hide pBA in ieee. And temporarily use Manage Queue as softmac_mgmt_xmit() usually does
********************************************************************************************************************/

static void ieee80211_send_DELBA(struct ieee80211_device *ieee, u8 *dst,
				 PBA_RECORD pBA, TR_SELECT TxRxSelect,
				 u16 ReasonCode)
{
	struct sk_buff *skb = NULL;
	skb = ieee80211_DELBA(ieee, dst, pBA, TxRxSelect, ReasonCode); //construct ACT_ADDBARSP frames
	if (skb)
	{
		softmac_mgmt_xmit(skb, ieee);
		//same above
	}
	else
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "alloc skb error in function %s()\n", __func__);
	}
	return ;
}

/********************************************************************************************************************
 *function: RX ADDBAReq
 *   input:  struct sk_buff *   skb	//incoming ADDBAReq skb.
 *  return:  0(pass), other(fail)
 *  notice:  As this function need support of QOS, I comment some code out. And when qos is ready, this code need to be support.
********************************************************************************************************************/
int ieee80211_rx_ADDBAReq(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	 struct rtl_80211_hdr_3addr *req = NULL;
	u16 rc = 0;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	PBA_RECORD pBA = NULL;
	PBA_PARAM_SET	pBaParamSet = NULL;
	u16 *pBaTimeoutVal = NULL;
	PSEQUENCE_CONTROL pBaStartSeqCtrl = NULL;
	PRX_TS_RECORD	pTS = NULL;

	if (skb->len < sizeof(struct rtl_80211_hdr_3addr) + 9) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR,
				" Invalid skb len in BAREQ(%d / %zu)\n",
				skb->len,
				(sizeof(struct rtl_80211_hdr_3addr) + 9));
		return -1;
	}

	IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA|IEEE80211_DL_BA, skb->data, skb->len);

	req = (struct rtl_80211_hdr_3addr *) skb->data;
	tag = (u8 *)req;
	dst = (u8 *)(&req->addr2[0]);
	tag += sizeof(struct rtl_80211_hdr_3addr);
	pDialogToken = tag + 2;  //category+action
	pBaParamSet = (PBA_PARAM_SET)(tag + 3);   //+DialogToken
	pBaTimeoutVal = (u16 *)(tag + 5);
	pBaStartSeqCtrl = (PSEQUENCE_CONTROL)(req + 7);

	printk("====================>rx ADDBAREQ from :%pM\n", dst);
//some other capability is not ready now.
	if ((ieee->current_network.qos_data.active == 0) ||
		(ieee->pHTInfo->bCurrentHTSupport == false)) //||
	//	(ieee->pStaQos->bEnableRxImmBA == false)	)
	{
		rc = ADDBA_STATUS_REFUSED;
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "Failed to reply on ADDBA_REQ as some capability is not ready(%d, %d)\n", ieee->current_network.qos_data.active, ieee->pHTInfo->bCurrentHTSupport);
		goto OnADDBAReq_Fail;
	}
	// Search for related traffic stream.
	// If there is no matched TS, reject the ADDBA request.
	if (!GetTs(
			ieee,
			(PTS_COMMON_INFO *)(&pTS),
			dst,
			(u8)(pBaParamSet->field.TID),
			RX_DIR,
			true)	)
	{
		rc = ADDBA_STATUS_REFUSED;
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't get TS in %s()\n", __func__);
		goto OnADDBAReq_Fail;
	}
	pBA = &pTS->RxAdmittedBARecord;
	// To Determine the ADDBA Req content
	// We can do much more check here, including BufferSize, AMSDU_Support, Policy, StartSeqCtrl...
	// I want to check StartSeqCtrl to make sure when we start aggregation!!!
	//
	if (pBaParamSet->field.BAPolicy == BA_POLICY_DELAYED)
	{
		rc = ADDBA_STATUS_INVALID_PARAM;
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "BA Policy is not correct in %s()\n", __func__);
		goto OnADDBAReq_Fail;
	}
		// Admit the ADDBA Request
	//
	DeActivateBAEntry(ieee, pBA);
	pBA->DialogToken = *pDialogToken;
	pBA->BaParamSet = *pBaParamSet;
	pBA->BaTimeoutValue = *pBaTimeoutVal;
	pBA->BaStartSeqCtrl = *pBaStartSeqCtrl;
	//for half N mode we only aggregate 1 frame
	if (ieee->GetHalfNmodeSupportByAPsHandler(ieee->dev))
	pBA->BaParamSet.field.BufferSize = 1;
	else
	pBA->BaParamSet.field.BufferSize = 32;
	ActivateBAEntry(ieee, pBA, pBA->BaTimeoutValue);
	ieee80211_send_ADDBARsp(ieee, dst, pBA, ADDBA_STATUS_SUCCESS);

	// End of procedure.
	return 0;

OnADDBAReq_Fail:
	{
		BA_RECORD	BA;
		BA.BaParamSet = *pBaParamSet;
		BA.BaTimeoutValue = *pBaTimeoutVal;
		BA.DialogToken = *pDialogToken;
		BA.BaParamSet.field.BAPolicy = BA_POLICY_IMMEDIATE;
		ieee80211_send_ADDBARsp(ieee, dst, &BA, rc);
		return 0; //we send RSP out.
	}

}

/********************************************************************************************************************
 *function: RX ADDBARSP
 *   input:  struct sk_buff *   skb	//incoming ADDBAReq skb.
 *  return:  0(pass), other(fail)
 *  notice:  As this function need support of QOS, I comment some code out. And when qos is ready, this code need to be support.
********************************************************************************************************************/
int ieee80211_rx_ADDBARsp(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	 struct rtl_80211_hdr_3addr *rsp = NULL;
	PBA_RECORD		pPendingBA, pAdmittedBA;
	PTX_TS_RECORD		pTS = NULL;
	u8 *dst = NULL, *pDialogToken = NULL, *tag = NULL;
	u16 *pStatusCode = NULL, *pBaTimeoutVal = NULL;
	PBA_PARAM_SET		pBaParamSet = NULL;
	u16			ReasonCode;

	if (skb->len < sizeof(struct rtl_80211_hdr_3addr) + 9) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR,
				" Invalid skb len in BARSP(%d / %zu)\n",
				skb->len,
				(sizeof(struct rtl_80211_hdr_3addr) + 9));
		return -1;
	}
	rsp = (struct rtl_80211_hdr_3addr *)skb->data;
	tag = (u8 *)rsp;
	dst = (u8 *)(&rsp->addr2[0]);
	tag += sizeof(struct rtl_80211_hdr_3addr);
	pDialogToken = tag + 2;
	pStatusCode = (u16 *)(tag + 3);
	pBaParamSet = (PBA_PARAM_SET)(tag + 5);
	pBaTimeoutVal = (u16 *)(tag + 7);

	// Check the capability
	// Since we can always receive A-MPDU, we just check if it is under HT mode.
	if (ieee->current_network.qos_data.active == 0  ||
	    ieee->pHTInfo->bCurrentHTSupport == false ||
	    ieee->pHTInfo->bCurrentAMPDUEnable == false) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "reject to ADDBA_RSP as some capability is not ready(%d, %d, %d)\n",ieee->current_network.qos_data.active, ieee->pHTInfo->bCurrentHTSupport, ieee->pHTInfo->bCurrentAMPDUEnable);
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}


	//
	// Search for related TS.
	// If there is no TS found, we wil reject ADDBA Rsp by sending DELBA frame.
	//
	if (!GetTs(
			ieee,
			(PTS_COMMON_INFO *)(&pTS),
			dst,
			(u8)(pBaParamSet->field.TID),
			TX_DIR,
			false)	)
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't get TS in %s()\n", __func__);
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}

	pTS->bAddBaReqInProgress = false;
	pPendingBA = &pTS->TxPendingBARecord;
	pAdmittedBA = &pTS->TxAdmittedBARecord;


	//
	// Check if related BA is waiting for setup.
	// If not, reject by sending DELBA frame.
	//
	if((pAdmittedBA->bValid==true))
	{
		// Since BA is already setup, we ignore all other ADDBA Response.
		IEEE80211_DEBUG(IEEE80211_DL_BA, "OnADDBARsp(): Recv ADDBA Rsp. Drop because already admit it! \n");
		return -1;
	}
	else if((pPendingBA->bValid == false) ||(*pDialogToken != pPendingBA->DialogToken))
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR,  "OnADDBARsp(): Recv ADDBA Rsp. BA invalid, DELBA! \n");
		ReasonCode = DELBA_REASON_UNKNOWN_BA;
		goto OnADDBARsp_Reject;
	}
	else
	{
		IEEE80211_DEBUG(IEEE80211_DL_BA, "OnADDBARsp(): Recv ADDBA Rsp. BA is admitted! Status code:%X\n", *pStatusCode);
		DeActivateBAEntry(ieee, pPendingBA);
	}


	if(*pStatusCode == ADDBA_STATUS_SUCCESS)
	{
		//
		// Determine ADDBA Rsp content here.
		// We can compare the value of BA parameter set that Peer returned and Self sent.
		// If it is OK, then admitted. Or we can send DELBA to cancel BA mechanism.
		//
		if (pBaParamSet->field.BAPolicy == BA_POLICY_DELAYED)
		{
			// Since this is a kind of ADDBA failed, we delay next ADDBA process.
			pTS->bAddBaReqDelayed = true;
			DeActivateBAEntry(ieee, pAdmittedBA);
			ReasonCode = DELBA_REASON_END_BA;
			goto OnADDBARsp_Reject;
		}


		//
		// Admitted condition
		//
		pAdmittedBA->DialogToken = *pDialogToken;
		pAdmittedBA->BaTimeoutValue = *pBaTimeoutVal;
		pAdmittedBA->BaStartSeqCtrl = pPendingBA->BaStartSeqCtrl;
		pAdmittedBA->BaParamSet = *pBaParamSet;
		DeActivateBAEntry(ieee, pAdmittedBA);
		ActivateBAEntry(ieee, pAdmittedBA, *pBaTimeoutVal);
	}
	else
	{
		// Delay next ADDBA process.
		pTS->bAddBaReqDelayed = true;
	}

	// End of procedure
	return 0;

OnADDBARsp_Reject:
	{
		BA_RECORD	BA;
		BA.BaParamSet = *pBaParamSet;
		ieee80211_send_DELBA(ieee, dst, &BA, TX_DIR, ReasonCode);
		return 0;
	}

}

/********************************************************************************************************************
 *function: RX DELBA
 *   input:  struct sk_buff *   skb	//incoming ADDBAReq skb.
 *  return:  0(pass), other(fail)
 *  notice:  As this function need support of QOS, I comment some code out. And when qos is ready, this code need to be support.
********************************************************************************************************************/
int ieee80211_rx_DELBA(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	 struct rtl_80211_hdr_3addr *delba = NULL;
	PDELBA_PARAM_SET	pDelBaParamSet = NULL;
	u16			*pReasonCode = NULL;
	u8			*dst = NULL;

	if (skb->len < sizeof(struct rtl_80211_hdr_3addr) + 6) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR,
				" Invalid skb len in DELBA(%d / %zu)\n",
				skb->len,
				(sizeof(struct rtl_80211_hdr_3addr) + 6));
		return -1;
	}

	if (ieee->current_network.qos_data.active == 0 ||
		ieee->pHTInfo->bCurrentHTSupport == false )
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "received DELBA while QOS or HT is not supported(%d, %d)\n",ieee->current_network.qos_data.active, ieee->pHTInfo->bCurrentHTSupport);
		return -1;
	}

	IEEE80211_DEBUG_DATA(IEEE80211_DL_DATA|IEEE80211_DL_BA, skb->data, skb->len);
	delba = (struct rtl_80211_hdr_3addr *)skb->data;
	dst = (u8 *)(&delba->addr2[0]);
	delba += sizeof(struct rtl_80211_hdr_3addr);
	pDelBaParamSet = (PDELBA_PARAM_SET)(delba+2);
	pReasonCode = (u16 *)(delba+4);

	if(pDelBaParamSet->field.Initiator == 1)
	{
		PRX_TS_RECORD	pRxTs;

		if (!GetTs(
				ieee,
				(PTS_COMMON_INFO *)&pRxTs,
				dst,
				(u8)pDelBaParamSet->field.TID,
				RX_DIR,
				false)	)
		{
			IEEE80211_DEBUG(IEEE80211_DL_ERR,  "can't get TS for RXTS in %s()\n", __func__);
			return -1;
		}

		RxTsDeleteBA(ieee, pRxTs);
	}
	else
	{
		PTX_TS_RECORD	pTxTs;

		if (!GetTs(
			ieee,
			(PTS_COMMON_INFO *)&pTxTs,
			dst,
			(u8)pDelBaParamSet->field.TID,
			TX_DIR,
			false)	)
		{
			IEEE80211_DEBUG(IEEE80211_DL_ERR,  "can't get TS for TXTS in %s()\n", __func__);
			return -1;
		}

		pTxTs->bUsingBa = false;
		pTxTs->bAddBaReqInProgress = false;
		pTxTs->bAddBaReqDelayed = false;
		del_timer_sync(&pTxTs->TsAddBaTimer);
		//PlatformCancelTimer(Adapter, &pTxTs->TsAddBaTimer);
		TxTsDeleteBA(ieee, pTxTs);
	}
	return 0;
}

//
// ADDBA initiate. This can only be called by TX side.
//
void
TsInitAddBA(
	struct ieee80211_device *ieee,
	PTX_TS_RECORD	pTS,
	u8		Policy,
	u8		bOverwritePending
	)
{
	PBA_RECORD			pBA = &pTS->TxPendingBARecord;

	if(pBA->bValid==true && bOverwritePending==false)
		return;

	// Set parameters to "Pending" variable set
	DeActivateBAEntry(ieee, pBA);

	pBA->DialogToken++;						// DialogToken: Only keep the latest dialog token
	pBA->BaParamSet.field.AMSDU_Support = 0;	// Do not support A-MSDU with A-MPDU now!!
	pBA->BaParamSet.field.BAPolicy = Policy;	// Policy: Delayed or Immediate
	pBA->BaParamSet.field.TID = pTS->TsCommonInfo.TSpec.f.TSInfo.field.ucTSID;	// TID
	// BufferSize: This need to be set according to A-MPDU vector
	pBA->BaParamSet.field.BufferSize = 32;		// BufferSize: This need to be set according to A-MPDU vector
	pBA->BaTimeoutValue = 0;					// Timeout value: Set 0 to disable Timer
	pBA->BaStartSeqCtrl.field.SeqNum = (pTS->TxCurSeq + 3) % 4096;	// Block Ack will start after 3 packets later.

	ActivateBAEntry(ieee, pBA, BA_SETUP_TIMEOUT);

	ieee80211_send_ADDBAReq(ieee, pTS->TsCommonInfo.Addr, pBA);
}

void
TsInitDelBA( struct ieee80211_device *ieee, PTS_COMMON_INFO pTsCommonInfo, TR_SELECT TxRxSelect)
{

	if(TxRxSelect == TX_DIR)
	{
		PTX_TS_RECORD	pTxTs = (PTX_TS_RECORD)pTsCommonInfo;

		if(TxTsDeleteBA(ieee, pTxTs))
			ieee80211_send_DELBA(
				ieee,
				pTsCommonInfo->Addr,
				(pTxTs->TxAdmittedBARecord.bValid)?(&pTxTs->TxAdmittedBARecord):(&pTxTs->TxPendingBARecord),
				TxRxSelect,
				DELBA_REASON_END_BA);
	}
	else if(TxRxSelect == RX_DIR)
	{
		PRX_TS_RECORD	pRxTs = (PRX_TS_RECORD)pTsCommonInfo;
		if(RxTsDeleteBA(ieee, pRxTs))
			ieee80211_send_DELBA(
				ieee,
				pTsCommonInfo->Addr,
				&pRxTs->RxAdmittedBARecord,
				TxRxSelect,
				DELBA_REASON_END_BA	);
	}
}
/********************************************************************************************************************
 *function:  BA setup timer
 *   input:  unsigned long	 data		//acturally we send TX_TS_RECORD or RX_TS_RECORD to these timer
 *  return:  NULL
 *  notice:
********************************************************************************************************************/
void BaSetupTimeOut(unsigned long data)
{
	PTX_TS_RECORD	pTxTs = (PTX_TS_RECORD)data;

	pTxTs->bAddBaReqInProgress = false;
	pTxTs->bAddBaReqDelayed = true;
	pTxTs->TxPendingBARecord.bValid = false;
}

void TxBaInactTimeout(unsigned long data)
{
	PTX_TS_RECORD	pTxTs = (PTX_TS_RECORD)data;
	struct ieee80211_device *ieee = container_of(pTxTs, struct ieee80211_device, TxTsRecord[pTxTs->num]);
	TxTsDeleteBA(ieee, pTxTs);
	ieee80211_send_DELBA(
		ieee,
		pTxTs->TsCommonInfo.Addr,
		&pTxTs->TxAdmittedBARecord,
		TX_DIR,
		DELBA_REASON_TIMEOUT);
}

void RxBaInactTimeout(unsigned long data)
{
	PRX_TS_RECORD	pRxTs = (PRX_TS_RECORD)data;
	struct ieee80211_device *ieee = container_of(pRxTs, struct ieee80211_device, RxTsRecord[pRxTs->num]);

	RxTsDeleteBA(ieee, pRxTs);
	ieee80211_send_DELBA(
		ieee,
		pRxTs->TsCommonInfo.Addr,
		&pRxTs->RxAdmittedBARecord,
		RX_DIR,
		DELBA_REASON_TIMEOUT);
	return ;
}
