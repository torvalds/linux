/******************************************************************************
 *
 *  (c) Copyright 2008, RealTEK Technologies Inc. All Rights Reserved.
 *
 *  Module:	r819xusb_cmdpkt.c
 *		(RTL8190 TX/RX command packet handler Source C File)
 *
 *  Note:	The module is responsible for handling TX and RX command packet.
 *		1. TX : Send set and query configuration command packet.
 *		2. RX : Receive tx feedback, beacon state, query configuration
 *			command packet.
 *
 *  Function:
 *
 *  Export:
 *
 *  Abbrev:
 *
 *  History:
 *
 *	Date		Who		Remark
 *	05/06/2008	amy		Create initial version porting from
 *					windows driver.
 *
 ******************************************************************************/
#include "r8192U.h"
#include "r819xU_cmdpkt.h"

rt_status SendTxCommandPacket(struct net_device *dev, void *pData, u32 DataLen)
{
	rt_status	rtStatus = RT_STATUS_SUCCESS;
	struct r8192_priv   *priv = ieee80211_priv(dev);
	struct sk_buff	    *skb;
	cb_desc		    *tcb_desc;
	unsigned char	    *ptr_buf;

	/* Get TCB and local buffer from common pool.
	   (It is shared by CmdQ, MgntQ, and USB coalesce DataQ) */
	skb  = dev_alloc_skb(USB_HWDESC_HEADER_LEN + DataLen + 4);
	if (!skb)
		return RT_STATUS_FAILURE;
	memcpy((unsigned char *)(skb->cb), &dev, sizeof(dev));
	tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	tcb_desc->queue_index = TXCMD_QUEUE;
	tcb_desc->bCmdOrInit = DESC_PACKET_TYPE_NORMAL;
	tcb_desc->bLastIniPkt = 0;
	skb_reserve(skb, USB_HWDESC_HEADER_LEN);
	ptr_buf = skb_put(skb, DataLen);
	memcpy(ptr_buf, pData, DataLen);
	tcb_desc->txbuf_size = (u16)DataLen;

	if (!priv->ieee80211->check_nic_enough_desc(dev, tcb_desc->queue_index) ||
	    (!skb_queue_empty(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index])) ||
	    (priv->ieee80211->queue_stop)) {
		RT_TRACE(COMP_FIRMWARE, "=== NULL packet ======> tx full!\n");
		skb_queue_tail(&priv->ieee80211->skb_waitQ[tcb_desc->queue_index], skb);
	} else {
		priv->ieee80211->softmac_hard_start_xmit(skb, dev);
	}

	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    cmpk_counttxstatistic()
 *
 * Overview:
 *
 * Input:       PADAPTER	pAdapter
 *              CMPK_TXFB_T	*psTx_FB
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who	Remark
 *  05/12/2008		amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_count_txstatistic(struct net_device *dev, cmpk_txfb_t *pstx_fb)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef ENABLE_PS
	RT_RF_POWER_STATE	rtState;

	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE,
					  (pu1Byte)(&rtState));

	/* When RF is off, we should not count the packet for hw/sw synchronize
	   reason, ie. there may be a duration while sw switch is changed and
	   hw switch is being changed. */
	if (rtState == eRfOff)
		return;
#endif

#ifdef TODO
	if (pAdapter->bInHctTest)
		return;
#endif
	/* We can not know the packet length and transmit type:
	   broadcast or uni or multicast. So the relative statistics
	   must be collected in tx feedback info. */
	if (pstx_fb->tok) {
		priv->stats.txfeedbackok++;
		priv->stats.txoktotal++;
		priv->stats.txokbytestotal += pstx_fb->pkt_length;
		priv->stats.txokinperiod++;

		/* We can not make sure broadcast/multicast or unicast mode. */
		if (pstx_fb->pkt_type == PACKET_MULTICAST) {
			priv->stats.txmulticast++;
			priv->stats.txbytesmulticast += pstx_fb->pkt_length;
		} else if (pstx_fb->pkt_type == PACKET_BROADCAST) {
			priv->stats.txbroadcast++;
			priv->stats.txbytesbroadcast += pstx_fb->pkt_length;
		} else {
			priv->stats.txunicast++;
			priv->stats.txbytesunicast += pstx_fb->pkt_length;
		}
	} else {
		priv->stats.txfeedbackfail++;
		priv->stats.txerrtotal++;
		priv->stats.txerrbytestotal += pstx_fb->pkt_length;

		/* We can not make sure broadcast/multicast or unicast mode. */
		if (pstx_fb->pkt_type == PACKET_MULTICAST)
			priv->stats.txerrmulticast++;
		else if (pstx_fb->pkt_type == PACKET_BROADCAST)
			priv->stats.txerrbroadcast++;
		else
			priv->stats.txerrunicast++;
	}

	priv->stats.txretrycount += pstx_fb->retry_cnt;
	priv->stats.txfeedbackretry += pstx_fb->retry_cnt;

}



/*-----------------------------------------------------------------------------
 * Function:    cmpk_handle_tx_feedback()
 *
 * Overview:	The function is responsible for extract the message inside TX
 *		feedbck message from firmware. It will contain dedicated info in
 *		ws-06-0063-rtl8190-command-packet-specification.
 *		Please refer to chapter "TX Feedback Element".
 *              We have to read 20 bytes in the command packet.
 *
 * Input:       struct net_device	*dev
 *              u8			*pmsg	- Msg Ptr of the command packet.
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who	Remark
 *  05/08/2008		amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_handle_tx_feedback(struct net_device *dev, u8 *pmsg)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	cmpk_txfb_t		rx_tx_fb;

	priv->stats.txfeedback++;

	/* 1. Extract TX feedback info from RFD to temp structure buffer. */
	/* It seems that FW use big endian(MIPS) and DRV use little endian in
	   windows OS. So we have to read the content byte by byte or transfer
	   endian type before copy the message copy. */
	/* Use pointer to transfer structure memory. */
	memcpy((u8 *)&rx_tx_fb, pmsg, sizeof(cmpk_txfb_t));
	/* 2. Use tx feedback info to count TX statistics. */
	cmpk_count_txstatistic(dev, &rx_tx_fb);
	/* Comment previous method for TX statistic function. */
	/* Collect info TX feedback packet to fill TCB. */
	/* We can not know the packet length and transmit type: broadcast or uni
	   or multicast. */

}

static void cmdpkt_beacontimerinterrupt_819xusb(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u16 tx_rate;
		/* 87B have to S/W beacon for DTM encryption_cmn. */
		if (priv->ieee80211->current_network.mode == IEEE_A ||
			priv->ieee80211->current_network.mode == IEEE_N_5G ||
			(priv->ieee80211->current_network.mode == IEEE_N_24G &&
			 (!priv->ieee80211->pHTInfo->bCurSuppCCK))) {
			tx_rate = 60;
			DMESG("send beacon frame  tx rate is 6Mbpm\n");
		} else {
			tx_rate = 10;
			DMESG("send beacon frame  tx rate is 1Mbpm\n");
		}

		rtl819xusb_beacon_tx(dev, tx_rate); /* HW Beacon */


}




/*-----------------------------------------------------------------------------
 * Function:    cmpk_handle_interrupt_status()
 *
 * Overview:    The function is responsible for extract the message from
 *		firmware. It will contain dedicated info in
 *		ws-07-0063-v06-rtl819x-command-packet-specification-070315.doc.
 *		Please refer to chapter "Interrupt Status Element".
 *
 * Input:       struct net_device *dev
 *              u8 *pmsg		- Message Pointer of the command packet.
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who	Remark
 *  05/12/2008		amy	Add this for rtl8192 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_handle_interrupt_status(struct net_device *dev, u8 *pmsg)
{
	cmpk_intr_sta_t		rx_intr_status;	/* */
	struct r8192_priv *priv = ieee80211_priv(dev);

	DMESG("---> cmpk_Handle_Interrupt_Status()\n");

	/* 1. Extract TX feedback info from RFD to temp structure buffer. */
	/* It seems that FW use big endian(MIPS) and DRV use little endian in
	   windows OS. So we have to read the content byte by byte or transfer
	   endian type before copy the message copy. */
	rx_intr_status.length = pmsg[1];
	if (rx_intr_status.length != (sizeof(cmpk_intr_sta_t) - 2)) {
		DMESG("cmpk_Handle_Interrupt_Status: wrong length!\n");
		return;
	}


	/* Statistics of beacon for ad-hoc mode. */
	if (priv->ieee80211->iw_mode == IW_MODE_ADHOC) {
		/* 2 maybe need endian transform? */
		rx_intr_status.interrupt_status = *((u32 *)(pmsg + 4));

		DMESG("interrupt status = 0x%x\n",
		      rx_intr_status.interrupt_status);

		if (rx_intr_status.interrupt_status & ISR_TxBcnOk) {
			priv->ieee80211->bibsscoordinator = true;
			priv->stats.txbeaconokint++;
		} else if (rx_intr_status.interrupt_status & ISR_TxBcnErr) {
			priv->ieee80211->bibsscoordinator = false;
			priv->stats.txbeaconerr++;
		}

		if (rx_intr_status.interrupt_status & ISR_BcnTimerIntr)
			cmdpkt_beacontimerinterrupt_819xusb(dev);

	}

	/* Other informations in interrupt status we need? */


	DMESG("<---- cmpk_handle_interrupt_status()\n");

}


/*-----------------------------------------------------------------------------
 * Function:    cmpk_handle_query_config_rx()
 *
 * Overview:    The function is responsible for extract the message from
 *		firmware. It will contain dedicated info in
 *		ws-06-0063-rtl8190-command-packet-specification. Please
 *		refer to chapter "Beacon State Element".
 *
 * Input:       u8    *pmsg	-	Message Pointer of the command packet.
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who	Remark
 *  05/12/2008		amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_handle_query_config_rx(struct net_device *dev, u8 *pmsg)
{
	cmpk_query_cfg_t	rx_query_cfg;


	/* 1. Extract TX feedback info from RFD to temp structure buffer. */
	/* It seems that FW use big endian(MIPS) and DRV use little endian in
	   windows OS. So we have to read the content byte by byte or transfer
	   endian type before copy the message copy. */
	rx_query_cfg.cfg_action		= (pmsg[4] & 0x80000000) >> 31;
	rx_query_cfg.cfg_type		= (pmsg[4] & 0x60) >> 5;
	rx_query_cfg.cfg_size		= (pmsg[4] & 0x18) >> 3;
	rx_query_cfg.cfg_page		= (pmsg[6] & 0x0F) >> 0;
	rx_query_cfg.cfg_offset		= pmsg[7];
	rx_query_cfg.value		= (pmsg[8]  << 24) | (pmsg[9]  << 16) |
					  (pmsg[10] <<  8) | (pmsg[11] <<  0);
	rx_query_cfg.mask		= (pmsg[12] << 24) | (pmsg[13] << 16) |
					  (pmsg[14] <<  8) | (pmsg[15] <<  0);

}


/*-----------------------------------------------------------------------------
 * Function:	cmpk_count_tx_status()
 *
 * Overview:	Count aggregated tx status from firmwar of one type rx command
 *		packet element id = RX_TX_STATUS.
 *
 * Input:	NONE
 *
 * Output:	NONE
 *
 * Return:	NONE
 *
 * Revised History:
 *	When		Who	Remark
 *	05/12/2008	amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_count_tx_status(struct net_device *dev,
				 cmpk_tx_status_t *pstx_status)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

#ifdef ENABLE_PS

	RT_RF_POWER_STATE	rtstate;

	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE,
					  (pu1Byte)(&rtState));

	/* When RF is off, we should not count the packet for hw/sw synchronize
	   reason, ie. there may be a duration while sw switch is changed and
	   hw switch is being changed. */
	if (rtState == eRfOff)
		return;
#endif

	priv->stats.txfeedbackok	+= pstx_status->txok;
	priv->stats.txoktotal		+= pstx_status->txok;

	priv->stats.txfeedbackfail	+= pstx_status->txfail;
	priv->stats.txerrtotal		+= pstx_status->txfail;

	priv->stats.txretrycount	+= pstx_status->txretry;
	priv->stats.txfeedbackretry	+= pstx_status->txretry;


	priv->stats.txmulticast		+= pstx_status->txmcok;
	priv->stats.txbroadcast		+= pstx_status->txbcok;
	priv->stats.txunicast		+= pstx_status->txucok;

	priv->stats.txerrmulticast	+= pstx_status->txmcfail;
	priv->stats.txerrbroadcast	+= pstx_status->txbcfail;
	priv->stats.txerrunicast	+= pstx_status->txucfail;

	priv->stats.txbytesmulticast	+= pstx_status->txmclength;
	priv->stats.txbytesbroadcast	+= pstx_status->txbclength;
	priv->stats.txbytesunicast	+= pstx_status->txuclength;

	priv->stats.last_packet_rate	= pstx_status->rate;
}



/*-----------------------------------------------------------------------------
 * Function:	cmpk_handle_tx_status()
 *
 * Overview:	Firmware add a new tx feedback status to reduce rx command
 *		packet buffer operation load.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who	Remark
 *	05/12/2008	amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_handle_tx_status(struct net_device *dev, u8 *pmsg)
{
	cmpk_tx_status_t	rx_tx_sts;

	memcpy((void *)&rx_tx_sts, (void *)pmsg, sizeof(cmpk_tx_status_t));
	/* 2. Use tx feedback info to count TX statistics. */
	cmpk_count_tx_status(dev, &rx_tx_sts);

}


/*-----------------------------------------------------------------------------
 * Function:	cmpk_handle_tx_rate_history()
 *
 * Overview:	Firmware add a new tx rate history
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who	Remark
 *	05/12/2008	amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static void cmpk_handle_tx_rate_history(struct net_device *dev, u8 *pmsg)
{
	cmpk_tx_rahis_t	*ptxrate;
	u8		i, j;
	u16		length = sizeof(cmpk_tx_rahis_t);
	u32		*ptemp;
	struct r8192_priv *priv = ieee80211_priv(dev);


#ifdef ENABLE_PS
	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE,
					  (pu1Byte)(&rtState));

	/* When RF is off, we should not count the packet for hw/sw synchronize
	   reason, ie. there may be a duration while sw switch is changed and
	   hw switch is being changed. */
	if (rtState == eRfOff)
		return;
#endif

	ptemp = (u32 *)pmsg;

	/* Do endian transfer to word alignment(16 bits) for windows system.
	   You must do different endian transfer for linux and MAC OS */
	for (i = 0; i < (length/4); i++) {
		u16	 temp1, temp2;

		temp1 = ptemp[i] & 0x0000FFFF;
		temp2 = ptemp[i] >> 16;
		ptemp[i] = (temp1 << 16) | temp2;
	}

	ptxrate = (cmpk_tx_rahis_t *)pmsg;

	if (ptxrate == NULL)
		return;

	for (i = 0; i < 16; i++) {
		/* Collect CCK rate packet num */
		if (i < 4)
			priv->stats.txrate.cck[i] += ptxrate->cck[i];

		/* Collect OFDM rate packet num */
		if (i < 8)
			priv->stats.txrate.ofdm[i] += ptxrate->ofdm[i];

		for (j = 0; j < 4; j++)
			priv->stats.txrate.ht_mcs[j][i] += ptxrate->ht_mcs[j][i];
	}

}


/*-----------------------------------------------------------------------------
 * Function:    cmpk_message_handle_rx()
 *
 * Overview:    In the function, we will capture different RX command packet
 *		info. Every RX command packet element has different message
 *		length and meaning in content. We only support three type of RX
 *		command packet now. Please refer to document
 *		ws-06-0063-rtl8190-command-packet-specification.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who	Remark
 *  05/06/2008		amy	Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
u32 cmpk_message_handle_rx(struct net_device *dev,
			   struct ieee80211_rx_stats *pstats)
{
	int			total_length;
	u8			cmd_length, exe_cnt = 0;
	u8			element_id;
	u8			*pcmd_buff;

	/* 0. Check inpt arguments. If is is a command queue message or
	   pointer is null. */
	if (pstats == NULL)
		return 0;	/* This is not a command packet. */

	/* 1. Read received command packet message length from RFD. */
	total_length = pstats->Length;

	/* 2. Read virtual address from RFD. */
	pcmd_buff = pstats->virtual_address;

	/* 3. Read command packet element id and length. */
	element_id = pcmd_buff[0];

	/* 4. Check every received command packet content according to different
	      element type. Because FW may aggregate RX command packet to
	      minimize transmit time between DRV and FW.*/
	/* Add a counter to prevent the lock in the loop from being held too
	   long */
	while (total_length > 0 && exe_cnt++ < 100) {
		/* We support aggregation of different cmd in the same packet */
		element_id = pcmd_buff[0];

		switch (element_id) {
		case RX_TX_FEEDBACK:
			cmpk_handle_tx_feedback(dev, pcmd_buff);
			cmd_length = CMPK_RX_TX_FB_SIZE;
			break;

		case RX_INTERRUPT_STATUS:
			cmpk_handle_interrupt_status(dev, pcmd_buff);
			cmd_length = sizeof(cmpk_intr_sta_t);
			break;

		case BOTH_QUERY_CONFIG:
			cmpk_handle_query_config_rx(dev, pcmd_buff);
			cmd_length = CMPK_BOTH_QUERY_CONFIG_SIZE;
			break;

		case RX_TX_STATUS:
			cmpk_handle_tx_status(dev, pcmd_buff);
			cmd_length = CMPK_RX_TX_STS_SIZE;
			break;

		case RX_TX_PER_PKT_FEEDBACK:
			/* You must at lease add a switch case element here,
			   Otherwise, we will jump to default case. */
			cmd_length = CMPK_RX_TX_FB_SIZE;
			break;

		case RX_TX_RATE_HISTORY:
			cmpk_handle_tx_rate_history(dev, pcmd_buff);
			cmd_length = CMPK_TX_RAHIS_SIZE;
			break;

		default:

			RT_TRACE(COMP_ERR, "---->%s():unknown CMD Element\n",
				 __func__);
			return 1;	/* This is a command packet. */
		}

		total_length -= cmd_length;
		pcmd_buff    += cmd_length;
	}
	return	1;	/* This is a command packet. */

}
