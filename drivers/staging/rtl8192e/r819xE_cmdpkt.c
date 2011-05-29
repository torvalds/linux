/******************************************************************************

     (c) Copyright 2008, RealTEK Technologies Inc. All Rights Reserved.

 Module:	r819xusb_cmdpkt.c	(RTL8190 TX/RX command packet handler Source C File)

 Note:      The module is responsible for handling TX and RX command packet.
 			1. TX : Send set and query configuration command packet.
 			2. RX : Receive tx feedback, beacon state, query configuration
 			        command packet.

 Function:

 Export:

 Abbrev:

 History:
	Data		Who		Remark

	05/06/2008  amy    	Create initial version porting from windows driver.

******************************************************************************/
#include "r8192E.h"
#include "r8192E_hw.h"
#include "r819xE_cmdpkt.h"

/*
 * Driver internal module can call the API to send message to
 * firmware side. For example, you can send a debug command packet.
 * Or you can send a request for FW to modify RLX4181 LBUS HW bank.
 * Otherwise, you can change MAC/PHT/RF register by firmware at
 * run time. We do not support message more than one segment now.
 */
RT_STATUS cmpk_message_handle_tx(
	struct r8192_priv *priv,
	u8*	code_virtual_address,
	u32	packettype,
	u32	buffer_len)
{
	RT_STATUS 	    rt_status = RT_STATUS_SUCCESS;
	u16		    frag_threshold;
	u16		    frag_length = 0, frag_offset = 0;
	rt_firmware	    *pfirmware = priv->pFirmware;
	struct sk_buff	    *skb;
	unsigned char	    *seg_ptr;
	cb_desc		    *tcb_desc;
	u8                  bLastIniPkt;

	PTX_FWINFO_8190PCI      pTxFwInfo = NULL;
        int i;

	RT_TRACE(COMP_CMDPKT,"%s(),buffer_len is %d\n",__FUNCTION__,buffer_len);
	firmware_init_param(priv);
	//Fragmentation might be required
	frag_threshold = pfirmware->cmdpacket_frag_thresold;
        do {
            if((buffer_len - frag_offset) > frag_threshold) {
                frag_length = frag_threshold ;
                bLastIniPkt = 0;

            } else {
                frag_length =(u16)(buffer_len - frag_offset);
                bLastIniPkt = 1;

            }

            /* Allocate skb buffer to contain firmware info and tx descriptor info
             * add 4 to avoid packet appending overflow.
             * */
            skb  = dev_alloc_skb(frag_length + priv->ieee80211->tx_headroom + 4);
            if(skb == NULL) {
                rt_status = RT_STATUS_FAILURE;
                goto Failed;
            }

            tcb_desc = (cb_desc*)(skb->cb + MAX_DEV_ADDR_SIZE);
            tcb_desc->queue_index = TXCMD_QUEUE;
            tcb_desc->bCmdOrInit = packettype;
            tcb_desc->bLastIniPkt = bLastIniPkt;
            tcb_desc->pkt_size = frag_length;

            //seg_ptr = skb_put(skb, frag_length + priv->ieee80211->tx_headroom);
            seg_ptr = skb_put(skb, priv->ieee80211->tx_headroom);

            pTxFwInfo = (PTX_FWINFO_8190PCI)seg_ptr;
            memset(pTxFwInfo,0,sizeof(TX_FWINFO_8190PCI));
            memset(pTxFwInfo,0x12,8);

            seg_ptr +=sizeof(TX_FWINFO_8190PCI);

            /*
             * Transform from little endian to big endian
             * and pending  zero
             */
            seg_ptr = skb_tail_pointer(skb);
            for(i=0 ; i < frag_length; i+=4) {
                *seg_ptr++ = ((i+0)<frag_length)?code_virtual_address[i+3]:0;
                *seg_ptr++ = ((i+1)<frag_length)?code_virtual_address[i+2]:0;
                *seg_ptr++ = ((i+2)<frag_length)?code_virtual_address[i+1]:0;
                *seg_ptr++ = ((i+3)<frag_length)?code_virtual_address[i+0]:0;
            }
            skb_put(skb, i);
            priv->ieee80211->softmac_hard_start_xmit(skb, priv->ieee80211);

            code_virtual_address += frag_length;
            frag_offset += frag_length;

        }while(frag_offset < buffer_len);

Failed:
	return rt_status;
}

static void cmpk_count_txstatistic(struct r8192_priv *priv, cmpk_txfb_t *pstx_fb)
{
#ifdef ENABLE_PS
	RT_RF_POWER_STATE	rtState;

	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));

	// When RF is off, we should not count the packet for hw/sw synchronize
	// reason, ie. there may be a duration while sw switch is changed and hw
	// switch is being changed. 2006.12.04, by shien chang.
	if (rtState == eRfOff)
	{
		return;
	}
#endif

#ifdef TODO
	if(pAdapter->bInHctTest)
		return;
#endif
	/* We can not know the packet length and transmit type: broadcast or uni
	   or multicast. So the relative statistics must be collected in tx
	   feedback info. */
	if (pstx_fb->tok)
	{
		priv->stats.txoktotal++;

		/* We can not make sure broadcast/multicast or unicast mode. */
		if (pstx_fb->pkt_type != PACKET_MULTICAST &&
		    pstx_fb->pkt_type != PACKET_BROADCAST) {
			priv->stats.txbytesunicast += pstx_fb->pkt_length;
		}
	}
}



/*
 * The function is responsible for extract the message inside TX
 * feedbck message from firmware. It will contain dedicated info in
 * ws-06-0063-rtl8190-command-packet-specification. Please
 * refer to chapter "TX Feedback Element". We have to read 20 bytes
 * in the command packet.
 */
static void cmpk_handle_tx_feedback(struct r8192_priv *priv, u8 *pmsg)
{
	cmpk_txfb_t		rx_tx_fb;	/* */

	priv->stats.txfeedback++;

	memcpy((u8*)&rx_tx_fb, pmsg, sizeof(cmpk_txfb_t));
	/* Use tx feedback info to count TX statistics. */
	cmpk_count_txstatistic(priv, &rx_tx_fb);
}


/*
 * The function is responsible for extract the message from
 * firmware. It will contain dedicated info in
 * ws-07-0063-v06-rtl819x-command-packet-specification-070315.doc.
 * Please refer to chapter "Interrupt Status Element".
 */
static void cmpk_handle_interrupt_status(struct r8192_priv *priv, u8 *pmsg)
{
	cmpk_intr_sta_t		rx_intr_status;	/* */

	DMESG("---> cmpk_Handle_Interrupt_Status()\n");

	/* 1. Extract TX feedback info from RFD to temp structure buffer. */
	/* It seems that FW use big endian(MIPS) and DRV use little endian in
	   windows OS. So we have to read the content byte by byte or transfer
	   endian type before copy the message copy. */
	//rx_bcn_state.Element_ID 	= pMsg[0];
	//rx_bcn_state.Length 		= pMsg[1];
	rx_intr_status.length = pmsg[1];
	if (rx_intr_status.length != (sizeof(cmpk_intr_sta_t) - 2))
	{
		DMESG("cmpk_Handle_Interrupt_Status: wrong length!\n");
		return;
	}


	// Statistics of beacon for ad-hoc mode.
	if(	priv->ieee80211->iw_mode == IW_MODE_ADHOC)
	{
		//2 maybe need endian transform?
		rx_intr_status.interrupt_status = *((u32 *)(pmsg + 4));
		//rx_intr_status.InterruptStatus = N2H4BYTE(*((UINT32 *)(pMsg + 4)));

		DMESG("interrupt status = 0x%x\n", rx_intr_status.interrupt_status);

		if (rx_intr_status.interrupt_status & ISR_TxBcnOk)
		{
			priv->ieee80211->bibsscoordinator = true;
			priv->stats.txbeaconokint++;
		}
		else if (rx_intr_status.interrupt_status & ISR_TxBcnErr)
		{
			priv->ieee80211->bibsscoordinator = false;
			priv->stats.txbeaconerr++;
		}
	}

	 // Other informations in interrupt status we need?


	DMESG("<---- cmpk_handle_interrupt_status()\n");

}


/*
 * The function is responsible for extract the message from
 * firmware. It will contain dedicated info in
 * ws-06-0063-rtl8190-command-packet-specification. Please
 * refer to chapter "Beacon State Element".
 */
static void cmpk_handle_query_config_rx(struct r8192_priv *priv, u8 *pmsg)
{
	cmpk_query_cfg_t	rx_query_cfg;	/* */

	/* 0. Display received message. */
	//cmpk_Display_Message(CMPK_RX_BEACON_STATE_SIZE, pMsg);

	/* 1. Extract TX feedback info from RFD to temp structure buffer. */
	/* It seems that FW use big endian(MIPS) and DRV use little endian in
	   windows OS. So we have to read the content byte by byte or transfer
	   endian type before copy the message copy. */
	//rx_query_cfg.Element_ID 	= pMsg[0];
	//rx_query_cfg.Length 		= pMsg[1];
	rx_query_cfg.cfg_action 	= (pmsg[4] & 0x80000000)>>31;
	rx_query_cfg.cfg_type 		= (pmsg[4] & 0x60) >> 5;
	rx_query_cfg.cfg_size 		= (pmsg[4] & 0x18) >> 3;
	rx_query_cfg.cfg_page 		= (pmsg[6] & 0x0F) >> 0;
	rx_query_cfg.cfg_offset 		= pmsg[7];
	rx_query_cfg.value 			= (pmsg[8] << 24) | (pmsg[9] << 16) |
								  (pmsg[10] << 8) | (pmsg[11] << 0);
	rx_query_cfg.mask 			= (pmsg[12] << 24) | (pmsg[13] << 16) |
								  (pmsg[14] << 8) | (pmsg[15] << 0);

}


/*
 * Count aggregated tx status from firmwar of one type rx command
 * packet element id = RX_TX_STATUS.
 */
static void cmpk_count_tx_status(struct r8192_priv *priv, cmpk_tx_status_t *pstx_status)
{

#ifdef ENABLE_PS

	RT_RF_POWER_STATE	rtstate;

	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));

	// When RF is off, we should not count the packet for hw/sw synchronize
	// reason, ie. there may be a duration while sw switch is changed and hw
	// switch is being changed. 2006.12.04, by shien chang.
	if (rtState == eRfOff)
	{
		return;
	}
#endif

	priv->stats.txfeedbackok	+= pstx_status->txok;
	priv->stats.txoktotal		+= pstx_status->txok;

	priv->stats.txbytesunicast		+= pstx_status->txuclength;
}



/*
 * Firmware add a new tx feedback status to reduce rx command
 * packet buffer operation load.
 */
static void cmpk_handle_tx_status(struct r8192_priv *priv, u8 *pmsg)
{
	cmpk_tx_status_t	rx_tx_sts;	/* */

	memcpy((void*)&rx_tx_sts, (void*)pmsg, sizeof(cmpk_tx_status_t));
	/* 2. Use tx feedback info to count TX statistics. */
	cmpk_count_tx_status(priv, &rx_tx_sts);

}


/* Firmware add a new tx rate history */
static void cmpk_handle_tx_rate_history(struct r8192_priv *priv, u8 *pmsg)
{
	u8				i;
	u16				length = sizeof(cmpk_tx_rahis_t);
	u32				*ptemp;

#ifdef ENABLE_PS
	pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));

	// When RF is off, we should not count the packet for hw/sw synchronize
	// reason, ie. there may be a duration while sw switch is changed and hw
	// switch is being changed. 2006.12.04, by shien chang.
	if (rtState == eRfOff)
	{
		return;
	}
#endif

	ptemp = (u32 *)pmsg;

	//
	// Do endian transfer to word alignment(16 bits) for windows system.
	// You must do different endian transfer for linux and MAC OS
	//
	for (i = 0; i < (length/4); i++)
	{
		u16	 temp1, temp2;

		temp1 = ptemp[i]&0x0000FFFF;
		temp2 = ptemp[i]>>16;
		ptemp[i] = (temp1<<16)|temp2;
	}
}


/*
 * In the function, we will capture different RX command packet
 * info. Every RX command packet element has different message
 * length and meaning in content. We only support three type of RX
 * command packet now. Please refer to document
 * ws-06-0063-rtl8190-command-packet-specification.
 */
u32 cmpk_message_handle_rx(struct r8192_priv *priv, struct ieee80211_rx_stats *pstats)
{
//	u32			debug_level = DBG_LOUD;
	int			total_length;
	u8			cmd_length, exe_cnt = 0;
	u8			element_id;
	u8			*pcmd_buff;

        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx()\n");

	/* 0. Check inpt arguments. If is is a command queue message or pointer is
	      null. */
	if (/*(prfd->queue_id != CMPK_RX_QUEUE_ID) || */(pstats== NULL))
	{
		/* Print error message. */
		/*RT_TRACE(COMP_SEND, DebugLevel,
				("\n\r[CMPK]-->Err queue id or pointer"));*/
		return 0;	/* This is not a command packet. */
	}

	/* 1. Read received command packet message length from RFD. */
	total_length = pstats->Length;

	/* 2. Read virtual address from RFD. */
	pcmd_buff = pstats->virtual_address;

	/* 3. Read command pakcet element id and length. */
	element_id = pcmd_buff[0];
	/*RT_TRACE(COMP_SEND, DebugLevel,
			("\n\r[CMPK]-->element ID=%d Len=%d", element_id, total_length));*/

	/* 4. Check every received command packet conent according to different
	      element type. Because FW may aggregate RX command packet to minimize
	      transmit time between DRV and FW.*/
	// Add a counter to prevent to locked in the loop too long
	while (total_length > 0 || exe_cnt++ >100)
	{
		/* 2007/01/17 MH We support aggregation of different cmd in the same packet. */
		element_id = pcmd_buff[0];

		switch(element_id)
		{
			case RX_TX_FEEDBACK:

        			RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():RX_TX_FEEDBACK\n");
				cmpk_handle_tx_feedback(priv, pcmd_buff);
				cmd_length = CMPK_RX_TX_FB_SIZE;
				break;

			case RX_INTERRUPT_STATUS:

			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():RX_INTERRUPT_STATUS\n");
				cmpk_handle_interrupt_status(priv, pcmd_buff);
				cmd_length = sizeof(cmpk_intr_sta_t);
				break;

			case BOTH_QUERY_CONFIG:

			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():BOTH_QUERY_CONFIG\n");
				cmpk_handle_query_config_rx(priv, pcmd_buff);
				cmd_length = CMPK_BOTH_QUERY_CONFIG_SIZE;
				break;

			case RX_TX_STATUS:

			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():RX_TX_STATUS\n");
				cmpk_handle_tx_status(priv, pcmd_buff);
				cmd_length = CMPK_RX_TX_STS_SIZE;
				break;

			case RX_TX_PER_PKT_FEEDBACK:
				// You must at lease add a switch case element here,
				// Otherwise, we will jump to default case.
				//DbgPrint("CCX Test\r\n");
			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():RX_TX_PER_PKT_FEEDBACK\n");
				cmd_length = CMPK_RX_TX_FB_SIZE;
				break;

			case RX_TX_RATE_HISTORY:
				//DbgPrint(" rx tx rate history\r\n");

			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():RX_TX_HISTORY\n");
				cmpk_handle_tx_rate_history(priv, pcmd_buff);
				cmd_length = CMPK_TX_RAHIS_SIZE;
				break;

			default:

			        RT_TRACE(COMP_EVENTS, "---->cmpk_message_handle_rx():unknown CMD Element\n");
				return 1;	/* This is a command packet. */
		}

		total_length -= cmd_length;
		pcmd_buff    += cmd_length;
	}	/* while (total_length > 0) */
	return	1;	/* This is a command packet. */

	RT_TRACE(COMP_EVENTS, "<----cmpk_message_handle_rx()\n");
}
