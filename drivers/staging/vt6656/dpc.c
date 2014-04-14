/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: dpc.c
 *
 * Purpose: handle dpc rx functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 20, 2003
 *
 * Functions:
 *      device_receive_frame - Rcv 802.11 frame function
 *      s_bAPModeRxCtl- AP Rcv frame filer Ctl.
 *      s_bAPModeRxData- AP Rcv data frame handle
 *      s_bHandleRxEncryption- Rcv decrypted data via on-fly
 *      s_bHostWepRxEncryption- Rcv encrypted data via host
 *      s_byGetRateIdx- get rate index
 *      s_vGetDASA- get data offset
 *      s_vProcessRxMACHeader- Rcv 802.11 and translate to 802.3
 *
 * Revision History:
 *
 */

#include "dpc.h"
#include "device.h"
#include "rxtx.h"
#include "tether.h"
#include "card.h"
#include "bssdb.h"
#include "mac.h"
#include "baseband.h"
#include "michael.h"
#include "tkip.h"
#include "tcrc.h"
#include "wctl.h"
#include "hostap.h"
#include "rf.h"
#include "iowpa.h"
#include "aes_ccmp.h"
#include "datarate.h"
#include "usbpipe.h"

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

static const u8 acbyRxRate[MAX_RATE] =
{2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};

static u8 s_byGetRateIdx(u8 byRate);

static
void
s_vGetDASA(
      u8 * pbyRxBufferAddr,
     unsigned int *pcbHeaderSize,
     struct ethhdr *psEthHeader
    );

static void s_vProcessRxMACHeader(struct vnt_private *pDevice,
	u8 *pbyRxBufferAddr, u32 cbPacketSize, int bIsWEP, int bExtIV,
	u32 *pcbHeadSize);

static int s_bAPModeRxCtl(struct vnt_private *pDevice, u8 *pbyFrame,
	s32 iSANodeIndex);

static int s_bAPModeRxData(struct vnt_private *pDevice, struct sk_buff *skb,
	u32 FrameSize, u32 cbHeaderOffset, s32 iSANodeIndex, s32 iDANodeIndex);

static int s_bHandleRxEncryption(struct vnt_private *pDevice, u8 *pbyFrame,
	u32 FrameSize, u8 *pbyRsr, u8 *pbyNewRsr, PSKeyItem *pKeyOut,
	s32 *pbExtIV, u16 *pwRxTSC15_0, u32 *pdwRxTSC47_16);

static int s_bHostWepRxEncryption(struct vnt_private *pDevice, u8 *pbyFrame,
	u32 FrameSize, u8 *pbyRsr, int bOnFly, PSKeyItem pKey, u8 *pbyNewRsr,
	s32 *pbExtIV, u16 *pwRxTSC15_0, u32 *pdwRxTSC47_16);

/*+
 *
 * Description:
 *    Translate Rcv 802.11 header to 802.3 header with Rx buffer
 *
 * Parameters:
 *  In:
 *      pDevice
 *      dwRxBufferAddr  - Address of Rcv Buffer
 *      cbPacketSize    - Rcv Packet size
 *      bIsWEP          - If Rcv with WEP
 *  Out:
 *      pcbHeaderSize   - 802.11 header size
 *
 * Return Value: None
 *
-*/

static void s_vProcessRxMACHeader(struct vnt_private *pDevice,
	u8 *pbyRxBufferAddr, u32 cbPacketSize, int bIsWEP, int bExtIV,
	u32 *pcbHeadSize)
{
	u8 *pbyRxBuffer;
	u32 cbHeaderSize = 0;
	u16 *pwType;
	struct ieee80211_hdr *pMACHeader;
	int ii;

    pMACHeader = (struct ieee80211_hdr *) (pbyRxBufferAddr + cbHeaderSize);

    s_vGetDASA((u8 *)pMACHeader, &cbHeaderSize, &pDevice->sRxEthHeader);

    if (bIsWEP) {
        if (bExtIV) {
            // strip IV&ExtIV , add 8 byte
            cbHeaderSize += (WLAN_HDR_ADDR3_LEN + 8);
        } else {
            // strip IV , add 4 byte
            cbHeaderSize += (WLAN_HDR_ADDR3_LEN + 4);
        }
    }
    else {
        cbHeaderSize += WLAN_HDR_ADDR3_LEN;
    };

    pbyRxBuffer = (u8 *) (pbyRxBufferAddr + cbHeaderSize);
    if (ether_addr_equal(pbyRxBuffer, pDevice->abySNAP_Bridgetunnel)) {
        cbHeaderSize += 6;
    } else if (ether_addr_equal(pbyRxBuffer, pDevice->abySNAP_RFC1042)) {
        cbHeaderSize += 6;
        pwType = (u16 *) (pbyRxBufferAddr + cbHeaderSize);
	if ((*pwType == cpu_to_be16(ETH_P_IPX)) ||
	    (*pwType == cpu_to_le16(0xF380))) {
		cbHeaderSize -= 8;
            pwType = (u16 *) (pbyRxBufferAddr + cbHeaderSize);
            if (bIsWEP) {
                if (bExtIV) {
                    *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN - 8);    // 8 is IV&ExtIV
                } else {
                    *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN - 4);    // 4 is IV
                }
            }
            else {
                *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN);
            }
        }
    }
    else {
        cbHeaderSize -= 2;
        pwType = (u16 *) (pbyRxBufferAddr + cbHeaderSize);
        if (bIsWEP) {
            if (bExtIV) {
                *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN - 8);    // 8 is IV&ExtIV
            } else {
                *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN - 4);    // 4 is IV
            }
        }
        else {
            *pwType = htons(cbPacketSize - WLAN_HDR_ADDR3_LEN);
        }
    }

    cbHeaderSize -= (ETH_ALEN * 2);
    pbyRxBuffer = (u8 *) (pbyRxBufferAddr + cbHeaderSize);
    for (ii = 0; ii < ETH_ALEN; ii++)
        *pbyRxBuffer++ = pDevice->sRxEthHeader.h_dest[ii];
    for (ii = 0; ii < ETH_ALEN; ii++)
        *pbyRxBuffer++ = pDevice->sRxEthHeader.h_source[ii];

    *pcbHeadSize = cbHeaderSize;
}

static u8 s_byGetRateIdx(u8 byRate)
{
    u8    byRateIdx;

    for (byRateIdx = 0; byRateIdx <MAX_RATE ; byRateIdx++) {
        if (acbyRxRate[byRateIdx%MAX_RATE] == byRate)
            return byRateIdx;
    }
    return 0;
}

static
void
s_vGetDASA (
      u8 * pbyRxBufferAddr,
     unsigned int *pcbHeaderSize,
     struct ethhdr *psEthHeader
    )
{
	unsigned int            cbHeaderSize = 0;
	struct ieee80211_hdr *pMACHeader;
	int             ii;

	pMACHeader = (struct ieee80211_hdr *) (pbyRxBufferAddr + cbHeaderSize);

	if ((pMACHeader->frame_control & FC_TODS) == 0) {
		if (pMACHeader->frame_control & FC_FROMDS) {
			for (ii = 0; ii < ETH_ALEN; ii++) {
				psEthHeader->h_dest[ii] =
					pMACHeader->addr1[ii];
				psEthHeader->h_source[ii] =
					pMACHeader->addr3[ii];
			}
		} else {
			/* IBSS mode */
			for (ii = 0; ii < ETH_ALEN; ii++) {
				psEthHeader->h_dest[ii] =
					pMACHeader->addr1[ii];
				psEthHeader->h_source[ii] =
					pMACHeader->addr2[ii];
			}
		}
	} else {
		/* Is AP mode.. */
		if (pMACHeader->frame_control & FC_FROMDS) {
			for (ii = 0; ii < ETH_ALEN; ii++) {
				psEthHeader->h_dest[ii] =
					pMACHeader->addr3[ii];
				psEthHeader->h_source[ii] =
					pMACHeader->addr4[ii];
				cbHeaderSize += 6;
			}
		} else {
			for (ii = 0; ii < ETH_ALEN; ii++) {
				psEthHeader->h_dest[ii] =
					pMACHeader->addr3[ii];
				psEthHeader->h_source[ii] =
					pMACHeader->addr2[ii];
			}
		}
	};
    *pcbHeaderSize = cbHeaderSize;
}

int RXbBulkInProcessData(struct vnt_private *pDevice, struct vnt_rcb *pRCB,
	unsigned long BytesToIndicate)
{
	struct net_device_stats *pStats = &pDevice->stats;
	struct sk_buff *skb;
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	struct vnt_rx_mgmt *pRxPacket = &pMgmt->sRxPacket;
	struct ieee80211_hdr *p802_11Header;
	u8 *pbyRsr, *pbyNewRsr, *pbyRSSI, *pbyFrame;
	u64 *pqwTSFTime;
	u32 bDeFragRx = false;
	u32 cbHeaderOffset, cbIVOffset;
	u32 FrameSize;
	u16 wEtherType = 0;
	s32 iSANodeIndex = -1, iDANodeIndex = -1;
	int ii;
	u8 *pbyRxSts, *pbyRxRate, *pbySQ, *pby3SQ;
	u32 cbHeaderSize;
	PSKeyItem pKey = NULL;
	u16 wRxTSC15_0 = 0;
	u32 dwRxTSC47_16 = 0;
	SKeyItem STempKey;
	/* signed long ldBm = 0; */
	int bIsWEP = false; int bExtIV = false;
	u32 dwWbkStatus;
	struct vnt_rcb *pRCBIndicate = pRCB;
	u8 *pbyDAddress;
	u16 *pwPLCP_Length;
	u8 abyVaildRate[MAX_RATE]
		= {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
	u16 wPLCPwithPadding;
	struct ieee80211_hdr *pMACHeader;
	int bRxeapol_key = false;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---------- RXbBulkInProcessData---\n");

    skb = pRCB->skb;

	/* [31:16]RcvByteCount ( not include 4-byte Status ) */
	dwWbkStatus = *((u32 *)(skb->data));
	FrameSize = dwWbkStatus >> 16;
	FrameSize += 4;

	if (BytesToIndicate != FrameSize) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"------- WRONG Length 1\n");
		pStats->rx_frame_errors++;
		return false;
	}

    if ((BytesToIndicate > 2372) || (BytesToIndicate <= 40)) {
        // Frame Size error drop this packet.
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "---------- WRONG Length 2\n");
	pStats->rx_frame_errors++;
        return false;
    }

    pbyDAddress = (u8 *)(skb->data);
    pbyRxSts = pbyDAddress+4;
    pbyRxRate = pbyDAddress+5;

    //real Frame Size = USBFrameSize -4WbkStatus - 4RxStatus - 8TSF - 4RSR - 4SQ3 - ?Padding
    //if SQ3 the range is 24~27, if no SQ3 the range is 20~23
    //real Frame size in PLCPLength field.
    pwPLCP_Length = (u16 *) (pbyDAddress + 6);
    //Fix hardware bug => PLCP_Length error
    if ( ((BytesToIndicate - (*pwPLCP_Length)) > 27) ||
         ((BytesToIndicate - (*pwPLCP_Length)) < 24) ||
         (BytesToIndicate < (*pwPLCP_Length)) ) {

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Wrong PLCP Length %x\n", (int) *pwPLCP_Length);
	pStats->rx_frame_errors++;
        return false;
    }
    for ( ii=RATE_1M;ii<MAX_RATE;ii++) {
        if ( *pbyRxRate == abyVaildRate[ii] ) {
            break;
        }
    }
    if ( ii==MAX_RATE ) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Wrong RxRate %x\n",(int) *pbyRxRate);
        return false;
    }

    wPLCPwithPadding = ( (*pwPLCP_Length / 4) + ( (*pwPLCP_Length % 4) ? 1:0 ) ) *4;

	pqwTSFTime = (u64 *)(pbyDAddress + 8 + wPLCPwithPadding);
  if(pDevice->byBBType == BB_TYPE_11G)  {
      pby3SQ = pbyDAddress + 8 + wPLCPwithPadding + 12;
      pbySQ = pby3SQ;
    }
  else {
   pbySQ = pbyDAddress + 8 + wPLCPwithPadding + 8;
   pby3SQ = pbySQ;
  }
    pbyNewRsr = pbyDAddress + 8 + wPLCPwithPadding + 9;
    pbyRSSI = pbyDAddress + 8 + wPLCPwithPadding + 10;
    pbyRsr = pbyDAddress + 8 + wPLCPwithPadding + 11;

    FrameSize = *pwPLCP_Length;

    pbyFrame = pbyDAddress + 8;

    pMACHeader = (struct ieee80211_hdr *) pbyFrame;

//mike add: to judge if current AP is activated?
    if ((pMgmt->eCurrMode == WMAC_MODE_STANDBY) ||
        (pMgmt->eCurrMode == WMAC_MODE_ESS_STA)) {
       if (pMgmt->sNodeDBTable[0].bActive) {
	 if (ether_addr_equal(pMgmt->abyCurrBSSID, pMACHeader->addr2)) {
	    if (pMgmt->sNodeDBTable[0].uInActiveCount != 0)
                  pMgmt->sNodeDBTable[0].uInActiveCount = 0;
           }
       }
    }

    if (!is_multicast_ether_addr(pMACHeader->addr1)) {
        if (WCTLbIsDuplicate(&(pDevice->sDupRxCache), (struct ieee80211_hdr *) pbyFrame)) {
            return false;
        }

	if (!ether_addr_equal(pDevice->abyCurrentNetAddr, pMACHeader->addr1)) {
		return false;
        }
    }

    // Use for TKIP MIC
    s_vGetDASA(pbyFrame, &cbHeaderSize, &pDevice->sRxEthHeader);

    if (ether_addr_equal((u8 *)pDevice->sRxEthHeader.h_source,
			 pDevice->abyCurrentNetAddr))
        return false;

    if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) || (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)) {
        if (IS_CTL_PSPOLL(pbyFrame) || !IS_TYPE_CONTROL(pbyFrame)) {
            p802_11Header = (struct ieee80211_hdr *) (pbyFrame);
            // get SA NodeIndex
            if (BSSbIsSTAInNodeDB(pDevice, (u8 *)(p802_11Header->addr2), &iSANodeIndex)) {
                pMgmt->sNodeDBTable[iSANodeIndex].ulLastRxJiffer = jiffies;
                pMgmt->sNodeDBTable[iSANodeIndex].uInActiveCount = 0;
            }
        }
    }

    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
        if (s_bAPModeRxCtl(pDevice, pbyFrame, iSANodeIndex) == true) {
            return false;
        }
    }

    if (IS_FC_WEP(pbyFrame)) {
        bool     bRxDecryOK = false;

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"rx WEP pkt\n");
        bIsWEP = true;
        if ((pDevice->bEnableHostWEP) && (iSANodeIndex >= 0)) {
            pKey = &STempKey;
            pKey->byCipherSuite = pMgmt->sNodeDBTable[iSANodeIndex].byCipherSuite;
            pKey->dwKeyIndex = pMgmt->sNodeDBTable[iSANodeIndex].dwKeyIndex;
            pKey->uKeyLength = pMgmt->sNodeDBTable[iSANodeIndex].uWepKeyLength;
            pKey->dwTSC47_16 = pMgmt->sNodeDBTable[iSANodeIndex].dwTSC47_16;
            pKey->wTSC15_0 = pMgmt->sNodeDBTable[iSANodeIndex].wTSC15_0;
            memcpy(pKey->abyKey,
                &pMgmt->sNodeDBTable[iSANodeIndex].abyWepKey[0],
                pKey->uKeyLength
                );

            bRxDecryOK = s_bHostWepRxEncryption(pDevice,
                                                pbyFrame,
                                                FrameSize,
                                                pbyRsr,
                                                pMgmt->sNodeDBTable[iSANodeIndex].bOnFly,
                                                pKey,
                                                pbyNewRsr,
                                                &bExtIV,
                                                &wRxTSC15_0,
                                                &dwRxTSC47_16);
        } else {
            bRxDecryOK = s_bHandleRxEncryption(pDevice,
                                                pbyFrame,
                                                FrameSize,
                                                pbyRsr,
                                                pbyNewRsr,
                                                &pKey,
                                                &bExtIV,
                                                &wRxTSC15_0,
                                                &dwRxTSC47_16);
        }

        if (bRxDecryOK) {
            if ((*pbyNewRsr & NEWRSR_DECRYPTOK) == 0) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ICV Fail\n");
                if ( (pMgmt->eAuthenMode == WMAC_AUTH_WPA) ||
                    (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) ||
                    (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) ||
                    (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) ||
                    (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) {
                }
                return false;
            }
        } else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"WEP Func Fail\n");
            return false;
        }
        if ((pKey != NULL) && (pKey->byCipherSuite == KEY_CTL_CCMP))
            FrameSize -= 8;         // Message Integrity Code
        else
            FrameSize -= 4;         // 4 is ICV
    }

    //
    // RX OK
    //
    /* remove the FCS/CRC length */
    FrameSize -= ETH_FCS_LEN;

    if ( !(*pbyRsr & (RSR_ADDRBROAD | RSR_ADDRMULTI)) && // unicast address
        (IS_FRAGMENT_PKT((pbyFrame)))
        ) {
        // defragment
        bDeFragRx = WCTLbHandleFragment(pDevice, (struct ieee80211_hdr *) (pbyFrame), FrameSize, bIsWEP, bExtIV);
        if (bDeFragRx) {
            // defrag complete
            // TODO skb, pbyFrame
            skb = pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx].skb;
            FrameSize = pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx].cbFrameLength;
            pbyFrame = skb->data + 8;
        }
        else {
            return false;
        }
    }

    //
    // Management & Control frame Handle
    //
    if ((IS_TYPE_DATA((pbyFrame))) == false) {
        // Handle Control & Manage Frame

        if (IS_TYPE_MGMT((pbyFrame))) {
            u8 * pbyData1;
            u8 * pbyData2;

            pRxPacket = &(pRCB->sMngPacket);
            pRxPacket->p80211Header = (PUWLAN_80211HDR)(pbyFrame);
            pRxPacket->cbMPDULen = FrameSize;
            pRxPacket->uRSSI = *pbyRSSI;
            pRxPacket->bySQ = *pbySQ;
		pRxPacket->qwLocalTSF = cpu_to_le64(*pqwTSFTime);
            if (bIsWEP) {
                // strip IV
                pbyData1 = WLAN_HDR_A3_DATA_PTR(pbyFrame);
                pbyData2 = WLAN_HDR_A3_DATA_PTR(pbyFrame) + 4;
                for (ii = 0; ii < (FrameSize - 4); ii++) {
                    *pbyData1 = *pbyData2;
                     pbyData1++;
                     pbyData2++;
                }
            }

            pRxPacket->byRxRate = s_byGetRateIdx(*pbyRxRate);

            if ( *pbyRxSts == 0 ) {
                //Discard beacon packet which channel is 0
                if ( (WLAN_GET_FC_FSTYPE((pRxPacket->p80211Header->sA3.wFrameCtl)) == WLAN_FSTYPE_BEACON) ||
                     (WLAN_GET_FC_FSTYPE((pRxPacket->p80211Header->sA3.wFrameCtl)) == WLAN_FSTYPE_PROBERESP) ) {
			return false;
                }
            }
            pRxPacket->byRxChannel = (*pbyRxSts) >> 2;

            // hostap Deamon handle 802.11 management
            if (pDevice->bEnableHostapd) {
	            skb->dev = pDevice->apdev;
	            //skb->data += 4;
	            //skb->tail += 4;
	            skb->data += 8;
	            skb->tail += 8;
                skb_put(skb, FrameSize);
		skb_reset_mac_header(skb);
	            skb->pkt_type = PACKET_OTHERHOST;
    	        skb->protocol = htons(ETH_P_802_2);
	            memset(skb->cb, 0, sizeof(skb->cb));
	            netif_rx(skb);
                return true;
	        }

            //
            // Insert the RCB in the Recv Mng list
            //
            EnqueueRCB(pDevice->FirstRecvMngList, pDevice->LastRecvMngList, pRCBIndicate);
            pDevice->NumRecvMngList++;
            if ( bDeFragRx == false) {
                pRCB->Ref++;
            }
            if (pDevice->bIsRxMngWorkItemQueued == false) {
                pDevice->bIsRxMngWorkItemQueued = true;
		schedule_work(&pDevice->rx_mng_work_item);
            }

        }
        else {
            // Control Frame
        };
        return false;
    }
    else {
        if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
            //In AP mode, hw only check addr1(BSSID or RA) if equal to local MAC.
            if ( !(*pbyRsr & RSR_BSSIDOK)) {
                if (bDeFragRx) {
                    if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
                        DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                        pDevice->dev->name);
                    }
                }
                return false;
            }
        }
        else {
            // discard DATA packet while not associate || BSSID error
            if ((pDevice->bLinkPass == false) ||
                !(*pbyRsr & RSR_BSSIDOK)) {
                if (bDeFragRx) {
                    if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
                        DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                        pDevice->dev->name);
                    }
                }
                return false;
            }
   //mike add:station mode check eapol-key challenge--->
   	  {
   	    u8  Protocol_Version;    //802.1x Authentication
	    u8  Packet_Type;           //802.1x Authentication
	    u8  Descriptor_type;
             u16 Key_info;
              if (bIsWEP)
                  cbIVOffset = 8;
              else
                  cbIVOffset = 0;
              wEtherType = (skb->data[cbIVOffset + 8 + 24 + 6] << 8) |
                          skb->data[cbIVOffset + 8 + 24 + 6 + 1];
	      Protocol_Version = skb->data[cbIVOffset + 8 + 24 + 6 + 1 +1];
	      Packet_Type = skb->data[cbIVOffset + 8 + 24 + 6 + 1 +1+1];
	     if (wEtherType == ETH_P_PAE) {         //Protocol Type in LLC-Header
                  if(((Protocol_Version==1) ||(Protocol_Version==2)) &&
		     (Packet_Type==3)) {  //802.1x OR eapol-key challenge frame receive
                        bRxeapol_key = true;
		      Descriptor_type = skb->data[cbIVOffset + 8 + 24 + 6 + 1 +1+1+1+2];
		      Key_info = (skb->data[cbIVOffset + 8 + 24 + 6 + 1 +1+1+1+2+1]<<8) |skb->data[cbIVOffset + 8 + 24 + 6 + 1 +1+1+1+2+2] ;
		      if(Descriptor_type==2) {    //RSN
                         //  printk("WPA2_Rx_eapol-key_info<-----:%x\n",Key_info);
		      }
		     else  if(Descriptor_type==254) {
                        //  printk("WPA_Rx_eapol-key_info<-----:%x\n",Key_info);
		     }
                  }
	      }
   	  }
    //mike add:station mode check eapol-key challenge<---
        }
    }

// Data frame Handle

    if (pDevice->bEnablePSMode) {
        if (IS_FC_MOREDATA((pbyFrame))) {
            if (*pbyRsr & RSR_ADDROK) {
                //PSbSendPSPOLL((PSDevice)pDevice);
            }
        }
        else {
            if (pMgmt->bInTIMWake == true) {
                pMgmt->bInTIMWake = false;
            }
        }
    }

    // Now it only supports 802.11g Infrastructure Mode, and support rate must up to 54 Mbps
    if (pDevice->bDiversityEnable && (FrameSize>50) &&
       (pDevice->eOPMode == OP_MODE_INFRASTRUCTURE) &&
       (pDevice->bLinkPass == true)) {
        BBvAntennaDiversity(pDevice, s_byGetRateIdx(*pbyRxRate), 0);
    }

    // ++++++++ For BaseBand Algorithm +++++++++++++++
    pDevice->uCurrRSSI = *pbyRSSI;
    pDevice->byCurrSQ = *pbySQ;

    // todo
/*
    if ((*pbyRSSI != 0) &&
        (pMgmt->pCurrBSS!=NULL)) {
        RFvRSSITodBm(pDevice, *pbyRSSI, &ldBm);
        // Monitor if RSSI is too strong.
        pMgmt->pCurrBSS->byRSSIStatCnt++;
        pMgmt->pCurrBSS->byRSSIStatCnt %= RSSI_STAT_COUNT;
        pMgmt->pCurrBSS->ldBmAverage[pMgmt->pCurrBSS->byRSSIStatCnt] = ldBm;
	for (ii = 0; ii < RSSI_STAT_COUNT; ii++) {
		if (pMgmt->pCurrBSS->ldBmAverage[ii] != 0) {
			pMgmt->pCurrBSS->ldBmMAX =
				max(pMgmt->pCurrBSS->ldBmAverage[ii], ldBm);
		}
        }
    }
*/

    // -----------------------------------------------

    if ((pMgmt->eCurrMode == WMAC_MODE_ESS_AP) && (pDevice->bEnable8021x == true)){
        u8    abyMacHdr[24];

        // Only 802.1x packet incoming allowed
        if (bIsWEP)
            cbIVOffset = 8;
        else
            cbIVOffset = 0;
        wEtherType = (skb->data[cbIVOffset + 8 + 24 + 6] << 8) |
                    skb->data[cbIVOffset + 8 + 24 + 6 + 1];

	    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"wEtherType = %04x \n", wEtherType);
        if (wEtherType == ETH_P_PAE) {
            skb->dev = pDevice->apdev;

            if (bIsWEP == true) {
                // strip IV header(8)
                memcpy(&abyMacHdr[0], (skb->data + 8), 24);
                memcpy((skb->data + 8 + cbIVOffset), &abyMacHdr[0], 24);
            }

            skb->data +=  (cbIVOffset + 8);
            skb->tail +=  (cbIVOffset + 8);
            skb_put(skb, FrameSize);
	    skb_reset_mac_header(skb);
            skb->pkt_type = PACKET_OTHERHOST;
            skb->protocol = htons(ETH_P_802_2);
            memset(skb->cb, 0, sizeof(skb->cb));
            netif_rx(skb);
            return true;

        }
        // check if 802.1x authorized
        if (!(pMgmt->sNodeDBTable[iSANodeIndex].dwFlags & WLAN_STA_AUTHORIZED))
            return false;
    }

    if ((pKey != NULL) && (pKey->byCipherSuite == KEY_CTL_TKIP)) {
        if (bIsWEP) {
            FrameSize -= 8;  //MIC
        }
    }

    //--------------------------------------------------------------------------------
    // Soft MIC
    if ((pKey != NULL) && (pKey->byCipherSuite == KEY_CTL_TKIP)) {
        if (bIsWEP) {
            u32 *          pdwMIC_L;
            u32 *          pdwMIC_R;
            u32           dwMIC_Priority;
            u32           dwMICKey0 = 0, dwMICKey1 = 0;
            u32           dwLocalMIC_L = 0;
            u32           dwLocalMIC_R = 0;

            if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
                dwMICKey0 = cpu_to_le32(*(u32 *)(&pKey->abyKey[24]));
                dwMICKey1 = cpu_to_le32(*(u32 *)(&pKey->abyKey[28]));
            }
            else {
                if (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) {
                    dwMICKey0 = cpu_to_le32(*(u32 *)(&pKey->abyKey[16]));
                    dwMICKey1 = cpu_to_le32(*(u32 *)(&pKey->abyKey[20]));
                } else if ((pKey->dwKeyIndex & BIT28) == 0) {
                    dwMICKey0 = cpu_to_le32(*(u32 *)(&pKey->abyKey[16]));
                    dwMICKey1 = cpu_to_le32(*(u32 *)(&pKey->abyKey[20]));
                } else {
                    dwMICKey0 = cpu_to_le32(*(u32 *)(&pKey->abyKey[24]));
                    dwMICKey1 = cpu_to_le32(*(u32 *)(&pKey->abyKey[28]));
                }
            }

            MIC_vInit(dwMICKey0, dwMICKey1);
            MIC_vAppend((u8 *)&(pDevice->sRxEthHeader.h_dest[0]), 12);
            dwMIC_Priority = 0;
            MIC_vAppend((u8 *)&dwMIC_Priority, 4);
            // 4 is Rcv buffer header, 24 is MAC Header, and 8 is IV and Ext IV.
            MIC_vAppend((u8 *)(skb->data + 8 + WLAN_HDR_ADDR3_LEN + 8),
                        FrameSize - WLAN_HDR_ADDR3_LEN - 8);
            MIC_vGetMIC(&dwLocalMIC_L, &dwLocalMIC_R);
            MIC_vUnInit();

            pdwMIC_L = (u32 *)(skb->data + 8 + FrameSize);
            pdwMIC_R = (u32 *)(skb->data + 8 + FrameSize + 4);

            if ((cpu_to_le32(*pdwMIC_L) != dwLocalMIC_L) || (cpu_to_le32(*pdwMIC_R) != dwLocalMIC_R) ||
                (pDevice->bRxMICFail == true)) {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MIC comparison is fail!\n");
                pDevice->bRxMICFail = false;
                if (bDeFragRx) {
                    if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
                        DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                            pDevice->dev->name);
                    }
                }
				//send event to wpa_supplicant
				//if(pDevice->bWPASuppWextEnabled == true)
				{
					union iwreq_data wrqu;
					struct iw_michaelmicfailure ev;
					int keyidx = pbyFrame[cbHeaderSize+3] >> 6; //top two-bits
					memset(&ev, 0, sizeof(ev));
					ev.flags = keyidx & IW_MICFAILURE_KEY_ID;
					if ((pMgmt->eCurrMode == WMAC_MODE_ESS_STA) &&
							(pMgmt->eCurrState == WMAC_STATE_ASSOC) &&
								(*pbyRsr & (RSR_ADDRBROAD | RSR_ADDRMULTI)) == 0) {
						ev.flags |= IW_MICFAILURE_PAIRWISE;
					} else {
						ev.flags |= IW_MICFAILURE_GROUP;
					}

					ev.src_addr.sa_family = ARPHRD_ETHER;
					memcpy(ev.src_addr.sa_data, pMACHeader->addr2, ETH_ALEN);
					memset(&wrqu, 0, sizeof(wrqu));
					wrqu.data.length = sizeof(ev);
			PRINT_K("wireless_send_event--->IWEVMICHAELMICFAILURE\n");
					wireless_send_event(pDevice->dev, IWEVMICHAELMICFAILURE, &wrqu, (char *)&ev);

				}

                return false;

            }
        }
    } //---end of SOFT MIC-----------------------------------------------------------------------

    // ++++++++++ Reply Counter Check +++++++++++++

    if ((pKey != NULL) && ((pKey->byCipherSuite == KEY_CTL_TKIP) ||
                           (pKey->byCipherSuite == KEY_CTL_CCMP))) {
        if (bIsWEP) {
            u16        wLocalTSC15_0 = 0;
            u32       dwLocalTSC47_16 = 0;
	    unsigned long long       RSC = 0;
            // endian issues
	    RSC = *((unsigned long long *) &(pKey->KeyRSC));
            wLocalTSC15_0 = (u16) RSC;
            dwLocalTSC47_16 = (u32) (RSC>>16);

            RSC = dwRxTSC47_16;
            RSC <<= 16;
            RSC += wRxTSC15_0;
		memcpy(&(pKey->KeyRSC), &RSC,  sizeof(u64));

		if (pDevice->vnt_mgmt.eCurrMode == WMAC_MODE_ESS_STA &&
			pDevice->vnt_mgmt.eCurrState == WMAC_STATE_ASSOC) {
			/* check RSC */
                if ( (wRxTSC15_0 < wLocalTSC15_0) &&
                     (dwRxTSC47_16 <= dwLocalTSC47_16) &&
                     !((dwRxTSC47_16 == 0) && (dwLocalTSC47_16 == 0xFFFFFFFF))) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"TSC is illegal~~!\n ");

                    if (bDeFragRx) {
                        if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
                            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                                pDevice->dev->name);
                        }
                    }
                    return false;
                }
            }
        }
    } // ----- End of Reply Counter Check --------------------------

    s_vProcessRxMACHeader(pDevice, (u8 *)(skb->data+8), FrameSize, bIsWEP, bExtIV, &cbHeaderOffset);
    FrameSize -= cbHeaderOffset;
    cbHeaderOffset += 8;        // 8 is Rcv buffer header

    // Null data, framesize = 12
    if (FrameSize < 12)
        return false;

    if (pMgmt->eCurrMode == WMAC_MODE_ESS_AP) {
        if (s_bAPModeRxData(pDevice,
                            skb,
                            FrameSize,
                            cbHeaderOffset,
                            iSANodeIndex,
                            iDANodeIndex
                            ) == false) {

            if (bDeFragRx) {
                if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
                    DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                    pDevice->dev->name);
                }
            }
            return false;
        }

    }

	skb->data += cbHeaderOffset;
	skb->tail += cbHeaderOffset;
    skb_put(skb, FrameSize);
    skb->protocol=eth_type_trans(skb, skb->dev);
    skb->ip_summed=CHECKSUM_NONE;
    pStats->rx_bytes +=skb->len;
    pStats->rx_packets++;
    netif_rx(skb);
    if (bDeFragRx) {
        if (!device_alloc_frag_buf(pDevice, &pDevice->sRxDFCB[pDevice->uCurrentDFCBIdx])) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc more frag bufs\n",
                pDevice->dev->name);
        }
        return false;
    }

    return true;
}

static int s_bAPModeRxCtl(struct vnt_private *pDevice, u8 *pbyFrame,
	s32 iSANodeIndex)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	struct ieee80211_hdr *p802_11Header;
	CMD_STATUS Status;

    if (IS_CTL_PSPOLL(pbyFrame) || !IS_TYPE_CONTROL(pbyFrame)) {

        p802_11Header = (struct ieee80211_hdr *) (pbyFrame);
        if (!IS_TYPE_MGMT(pbyFrame)) {

            // Data & PS-Poll packet
            // check frame class
            if (iSANodeIndex > 0) {
                // frame class 3 fliter & checking
                if (pMgmt->sNodeDBTable[iSANodeIndex].eNodeState < NODE_AUTH) {
                    // send deauth notification
                    // reason = (6) class 2 received from nonauth sta
                    vMgrDeAuthenBeginSta(pDevice,
                                         pMgmt,
                                         (u8 *)(p802_11Header->addr2),
                                         (WLAN_MGMT_REASON_CLASS2_NONAUTH),
                                         &Status
                                         );
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: send vMgrDeAuthenBeginSta 1\n");
                    return true;
                }
                if (pMgmt->sNodeDBTable[iSANodeIndex].eNodeState < NODE_ASSOC) {
                    // send deassoc notification
                    // reason = (7) class 3 received from nonassoc sta
                    vMgrDisassocBeginSta(pDevice,
                                         pMgmt,
                                         (u8 *)(p802_11Header->addr2),
                                         (WLAN_MGMT_REASON_CLASS3_NONASSOC),
                                         &Status
                                         );
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: send vMgrDisassocBeginSta 2\n");
                    return true;
                }

                if (pMgmt->sNodeDBTable[iSANodeIndex].bPSEnable) {
                    // delcare received ps-poll event
                    if (IS_CTL_PSPOLL(pbyFrame)) {
                        pMgmt->sNodeDBTable[iSANodeIndex].bRxPSPoll = true;
			bScheduleCommand((void *) pDevice,
					 WLAN_CMD_RX_PSPOLL,
					 NULL);
                        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: WLAN_CMD_RX_PSPOLL 1\n");
                    }
                    else {
                        // check Data PS state
                        // if PW bit off, send out all PS bufferring packets.
                        if (!IS_FC_POWERMGT(pbyFrame)) {
                            pMgmt->sNodeDBTable[iSANodeIndex].bPSEnable = false;
                            pMgmt->sNodeDBTable[iSANodeIndex].bRxPSPoll = true;
				bScheduleCommand((void *) pDevice,
						 WLAN_CMD_RX_PSPOLL,
						 NULL);
                            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: WLAN_CMD_RX_PSPOLL 2\n");
                        }
                    }
                }
                else {
                   if (IS_FC_POWERMGT(pbyFrame)) {
                       pMgmt->sNodeDBTable[iSANodeIndex].bPSEnable = true;
                       // Once if STA in PS state, enable multicast bufferring
                       pMgmt->sNodeDBTable[0].bPSEnable = true;
                   }
                   else {
                      // clear all pending PS frame.
                      if (pMgmt->sNodeDBTable[iSANodeIndex].wEnQueueCnt > 0) {
                          pMgmt->sNodeDBTable[iSANodeIndex].bPSEnable = false;
                          pMgmt->sNodeDBTable[iSANodeIndex].bRxPSPoll = true;
			bScheduleCommand((void *) pDevice,
					 WLAN_CMD_RX_PSPOLL,
					 NULL);
                         DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: WLAN_CMD_RX_PSPOLL 3\n");

                      }
                   }
                }
            }
            else {
                  vMgrDeAuthenBeginSta(pDevice,
                                       pMgmt,
                                       (u8 *)(p802_11Header->addr2),
                                       (WLAN_MGMT_REASON_CLASS2_NONAUTH),
                                       &Status
                                       );
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: send vMgrDeAuthenBeginSta 3\n");
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "BSSID:%pM\n",
				p802_11Header->addr3);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ADDR2:%pM\n",
				p802_11Header->addr2);
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ADDR1:%pM\n",
				p802_11Header->addr1);
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "dpc: frame_control= %x\n", p802_11Header->frame_control);
                    return true;
            }
        }
    }
    return false;

}

static int s_bHandleRxEncryption(struct vnt_private *pDevice, u8 *pbyFrame,
	u32 FrameSize, u8 *pbyRsr, u8 *pbyNewRsr, PSKeyItem *pKeyOut,
	s32 *pbExtIV, u16 *pwRxTSC15_0, u32 *pdwRxTSC47_16)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	u32 PayloadLen = FrameSize;
	u8 *pbyIV;
	u8 byKeyIdx;
	PSKeyItem pKey = NULL;
	u8 byDecMode = KEY_CTL_WEP;

    *pwRxTSC15_0 = 0;
    *pdwRxTSC47_16 = 0;

    pbyIV = pbyFrame + WLAN_HDR_ADDR3_LEN;
    if ( WLAN_GET_FC_TODS(*(u16 *)pbyFrame) &&
         WLAN_GET_FC_FROMDS(*(u16 *)pbyFrame) ) {
         pbyIV += 6;             // 6 is 802.11 address4
         PayloadLen -= 6;
    }
    byKeyIdx = (*(pbyIV+3) & 0xc0);
    byKeyIdx >>= 6;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"\nKeyIdx: %d\n", byKeyIdx);

    if ((pMgmt->eAuthenMode == WMAC_AUTH_WPA) ||
        (pMgmt->eAuthenMode == WMAC_AUTH_WPAPSK) ||
        (pMgmt->eAuthenMode == WMAC_AUTH_WPANONE) ||
        (pMgmt->eAuthenMode == WMAC_AUTH_WPA2) ||
        (pMgmt->eAuthenMode == WMAC_AUTH_WPA2PSK)) {
        if (((*pbyRsr & (RSR_ADDRBROAD | RSR_ADDRMULTI)) == 0) &&
            (pMgmt->byCSSPK != KEY_CTL_NONE)) {
            // unicast pkt use pairwise key
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"unicast pkt\n");
            if (KeybGetKey(&(pDevice->sKey), pDevice->abyBSSID, 0xFFFFFFFF, &pKey) == true) {
                if (pMgmt->byCSSPK == KEY_CTL_TKIP)
                    byDecMode = KEY_CTL_TKIP;
                else if (pMgmt->byCSSPK == KEY_CTL_CCMP)
                    byDecMode = KEY_CTL_CCMP;
            }
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"unicast pkt: %d, %p\n", byDecMode, pKey);
        } else {
            // use group key
            KeybGetKey(&(pDevice->sKey), pDevice->abyBSSID, byKeyIdx, &pKey);
            if (pMgmt->byCSSGK == KEY_CTL_TKIP)
                byDecMode = KEY_CTL_TKIP;
            else if (pMgmt->byCSSGK == KEY_CTL_CCMP)
                byDecMode = KEY_CTL_CCMP;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"group pkt: %d, %d, %p\n", byKeyIdx, byDecMode, pKey);
        }
    }
    // our WEP only support Default Key
    if (pKey == NULL) {
        // use default group key
        KeybGetKey(&(pDevice->sKey), pDevice->abyBroadcastAddr, byKeyIdx, &pKey);
        if (pMgmt->byCSSGK == KEY_CTL_TKIP)
            byDecMode = KEY_CTL_TKIP;
        else if (pMgmt->byCSSGK == KEY_CTL_CCMP)
            byDecMode = KEY_CTL_CCMP;
    }
    *pKeyOut = pKey;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"AES:%d %d %d\n", pMgmt->byCSSPK, pMgmt->byCSSGK, byDecMode);

    if (pKey == NULL) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"pKey == NULL\n");
        return false;
    }
    if (byDecMode != pKey->byCipherSuite) {
        *pKeyOut = NULL;
        return false;
    }
    if (byDecMode == KEY_CTL_WEP) {
        // handle WEP
        if ((pDevice->byLocalID <= REV_ID_VT3253_A1) ||
		(((PSKeyTable)(pKey->pvKeyTable))->bSoftWEP == true)) {
            // Software WEP
            // 1. 3253A
            // 2. WEP 256

            PayloadLen -= (WLAN_HDR_ADDR3_LEN + 4 + 4); // 24 is 802.11 header,4 is IV, 4 is crc
            memcpy(pDevice->abyPRNG, pbyIV, 3);
            memcpy(pDevice->abyPRNG + 3, pKey->abyKey, pKey->uKeyLength);
            rc4_init(&pDevice->SBox, pDevice->abyPRNG, pKey->uKeyLength + 3);
            rc4_encrypt(&pDevice->SBox, pbyIV+4, pbyIV+4, PayloadLen);

            if (ETHbIsBufferCrc32Ok(pbyIV+4, PayloadLen)) {
                *pbyNewRsr |= NEWRSR_DECRYPTOK;
            }
        }
    } else if ((byDecMode == KEY_CTL_TKIP) ||
               (byDecMode == KEY_CTL_CCMP)) {
        // TKIP/AES

        PayloadLen -= (WLAN_HDR_ADDR3_LEN + 8 + 4); // 24 is 802.11 header, 8 is IV&ExtIV, 4 is crc
        *pdwRxTSC47_16 = cpu_to_le32(*(u32 *)(pbyIV + 4));
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ExtIV: %x\n", *pdwRxTSC47_16);
        if (byDecMode == KEY_CTL_TKIP) {
            *pwRxTSC15_0 = cpu_to_le16(MAKEWORD(*(pbyIV+2), *pbyIV));
        } else {
            *pwRxTSC15_0 = cpu_to_le16(*(u16 *)pbyIV);
        }
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"TSC0_15: %x\n", *pwRxTSC15_0);

        if ((byDecMode == KEY_CTL_TKIP) &&
            (pDevice->byLocalID <= REV_ID_VT3253_A1)) {
            // Software TKIP
            // 1. 3253 A
            struct ieee80211_hdr *pMACHeader = (struct ieee80211_hdr *) (pbyFrame);
            TKIPvMixKey(pKey->abyKey, pMACHeader->addr2, *pwRxTSC15_0, *pdwRxTSC47_16, pDevice->abyPRNG);
            rc4_init(&pDevice->SBox, pDevice->abyPRNG, TKIP_KEY_LEN);
            rc4_encrypt(&pDevice->SBox, pbyIV+8, pbyIV+8, PayloadLen);
            if (ETHbIsBufferCrc32Ok(pbyIV+8, PayloadLen)) {
                *pbyNewRsr |= NEWRSR_DECRYPTOK;
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ICV OK!\n");
            } else {
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ICV FAIL!!!\n");
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"PayloadLen = %d\n", PayloadLen);
            }
        }
    }// end of TKIP/AES

    if ((*(pbyIV+3) & 0x20) != 0)
        *pbExtIV = true;
    return true;
}

static int s_bHostWepRxEncryption(struct vnt_private *pDevice, u8 *pbyFrame,
	u32 FrameSize, u8 *pbyRsr, int bOnFly, PSKeyItem pKey, u8 *pbyNewRsr,
	s32 *pbExtIV, u16 *pwRxTSC15_0, u32 *pdwRxTSC47_16)
{
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	struct ieee80211_hdr *pMACHeader;
	u32 PayloadLen = FrameSize;
	u8 *pbyIV;
	u8 byKeyIdx;
	u8 byDecMode = KEY_CTL_WEP;

	*pwRxTSC15_0 = 0;
	*pdwRxTSC47_16 = 0;

    pbyIV = pbyFrame + WLAN_HDR_ADDR3_LEN;
    if ( WLAN_GET_FC_TODS(*(u16 *)pbyFrame) &&
         WLAN_GET_FC_FROMDS(*(u16 *)pbyFrame) ) {
         pbyIV += 6;             // 6 is 802.11 address4
         PayloadLen -= 6;
    }
    byKeyIdx = (*(pbyIV+3) & 0xc0);
    byKeyIdx >>= 6;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"\nKeyIdx: %d\n", byKeyIdx);

    if (pMgmt->byCSSGK == KEY_CTL_TKIP)
        byDecMode = KEY_CTL_TKIP;
    else if (pMgmt->byCSSGK == KEY_CTL_CCMP)
        byDecMode = KEY_CTL_CCMP;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"AES:%d %d %d\n", pMgmt->byCSSPK, pMgmt->byCSSGK, byDecMode);

    if (byDecMode != pKey->byCipherSuite) {
        return false;
    }

    if (byDecMode == KEY_CTL_WEP) {
        // handle WEP
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"byDecMode == KEY_CTL_WEP\n");
        if ((pDevice->byLocalID <= REV_ID_VT3253_A1) ||
		(((PSKeyTable)(pKey->pvKeyTable))->bSoftWEP == true) ||
            (bOnFly == false)) {
            // Software WEP
            // 1. 3253A
            // 2. WEP 256
            // 3. NotOnFly

            PayloadLen -= (WLAN_HDR_ADDR3_LEN + 4 + 4); // 24 is 802.11 header,4 is IV, 4 is crc
            memcpy(pDevice->abyPRNG, pbyIV, 3);
            memcpy(pDevice->abyPRNG + 3, pKey->abyKey, pKey->uKeyLength);
            rc4_init(&pDevice->SBox, pDevice->abyPRNG, pKey->uKeyLength + 3);
            rc4_encrypt(&pDevice->SBox, pbyIV+4, pbyIV+4, PayloadLen);

            if (ETHbIsBufferCrc32Ok(pbyIV+4, PayloadLen)) {
                *pbyNewRsr |= NEWRSR_DECRYPTOK;
            }
        }
    } else if ((byDecMode == KEY_CTL_TKIP) ||
               (byDecMode == KEY_CTL_CCMP)) {
        // TKIP/AES

        PayloadLen -= (WLAN_HDR_ADDR3_LEN + 8 + 4); // 24 is 802.11 header, 8 is IV&ExtIV, 4 is crc
        *pdwRxTSC47_16 = cpu_to_le32(*(u32 *)(pbyIV + 4));
	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ExtIV: %x\n", *pdwRxTSC47_16);

        if (byDecMode == KEY_CTL_TKIP) {
            *pwRxTSC15_0 = cpu_to_le16(MAKEWORD(*(pbyIV+2), *pbyIV));
        } else {
            *pwRxTSC15_0 = cpu_to_le16(*(u16 *)pbyIV);
        }
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"TSC0_15: %x\n", *pwRxTSC15_0);

        if (byDecMode == KEY_CTL_TKIP) {

            if ((pDevice->byLocalID <= REV_ID_VT3253_A1) || (bOnFly == false)) {
                // Software TKIP
                // 1. 3253 A
                // 2. NotOnFly
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"soft KEY_CTL_TKIP \n");
                pMACHeader = (struct ieee80211_hdr *) (pbyFrame);
                TKIPvMixKey(pKey->abyKey, pMACHeader->addr2, *pwRxTSC15_0, *pdwRxTSC47_16, pDevice->abyPRNG);
                rc4_init(&pDevice->SBox, pDevice->abyPRNG, TKIP_KEY_LEN);
                rc4_encrypt(&pDevice->SBox, pbyIV+8, pbyIV+8, PayloadLen);
                if (ETHbIsBufferCrc32Ok(pbyIV+8, PayloadLen)) {
                    *pbyNewRsr |= NEWRSR_DECRYPTOK;
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ICV OK!\n");
                } else {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"ICV FAIL!!!\n");
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"PayloadLen = %d\n", PayloadLen);
                }
            }
        }

        if (byDecMode == KEY_CTL_CCMP) {
            if (bOnFly == false) {
                // Software CCMP
                // NotOnFly
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"soft KEY_CTL_CCMP\n");
                if (AESbGenCCMP(pKey->abyKey, pbyFrame, FrameSize)) {
                    *pbyNewRsr |= NEWRSR_DECRYPTOK;
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"CCMP MIC compare OK!\n");
                } else {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"CCMP MIC fail!\n");
                }
            }
        }

    }// end of TKIP/AES

    if ((*(pbyIV+3) & 0x20) != 0)
        *pbExtIV = true;
    return true;
}

static int s_bAPModeRxData(struct vnt_private *pDevice, struct sk_buff *skb,
	u32 FrameSize, u32 cbHeaderOffset, s32 iSANodeIndex, s32 iDANodeIndex)
{
	struct sk_buff *skbcpy;
	struct vnt_manager *pMgmt = &pDevice->vnt_mgmt;
	int  bRelayAndForward = false;
	int bRelayOnly = false;
	u8 byMask[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
	u16 wAID;

    if (FrameSize > CB_MAX_BUF_SIZE)
        return false;
    // check DA
    if (is_multicast_ether_addr((u8 *)(skb->data+cbHeaderOffset))) {
       if (pMgmt->sNodeDBTable[0].bPSEnable) {

           skbcpy = dev_alloc_skb((int)pDevice->rx_buf_sz);

        // if any node in PS mode, buffer packet until DTIM.
           if (skbcpy == NULL) {
               DBG_PRT(MSG_LEVEL_NOTICE, KERN_INFO "relay multicast no skb available \n");
           }
           else {
               skbcpy->dev = pDevice->dev;
               skbcpy->len = FrameSize;
               memcpy(skbcpy->data, skb->data+cbHeaderOffset, FrameSize);
               skb_queue_tail(&(pMgmt->sNodeDBTable[0].sTxPSQueue), skbcpy);
               pMgmt->sNodeDBTable[0].wEnQueueCnt++;
               // set tx map
               pMgmt->abyPSTxMap[0] |= byMask[0];
           }
       }
       else {
           bRelayAndForward = true;
       }
    }
    else {
        // check if relay
        if (BSSbIsSTAInNodeDB(pDevice, (u8 *)(skb->data+cbHeaderOffset), &iDANodeIndex)) {
            if (pMgmt->sNodeDBTable[iDANodeIndex].eNodeState >= NODE_ASSOC) {
                if (pMgmt->sNodeDBTable[iDANodeIndex].bPSEnable) {
                    // queue this skb until next PS tx, and then release.

	                skb->data += cbHeaderOffset;
	                skb->tail += cbHeaderOffset;
                    skb_put(skb, FrameSize);
                    skb_queue_tail(&pMgmt->sNodeDBTable[iDANodeIndex].sTxPSQueue, skb);

                    pMgmt->sNodeDBTable[iDANodeIndex].wEnQueueCnt++;
                    wAID = pMgmt->sNodeDBTable[iDANodeIndex].wAID;
                    pMgmt->abyPSTxMap[wAID >> 3] |=  byMask[wAID & 7];
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "relay: index= %d, pMgmt->abyPSTxMap[%d]= %d\n",
                               iDANodeIndex, (wAID >> 3), pMgmt->abyPSTxMap[wAID >> 3]);
                    return true;
                }
                else {
                    bRelayOnly = true;
                }
            }
        }
    }

    if (bRelayOnly || bRelayAndForward) {
        // relay this packet right now
        if (bRelayAndForward)
            iDANodeIndex = 0;

        if ((pDevice->uAssocCount > 1) && (iDANodeIndex >= 0)) {
		bRelayPacketSend(pDevice, (u8 *) (skb->data + cbHeaderOffset),
				 FrameSize, (unsigned int) iDANodeIndex);
        }

        if (bRelayOnly)
            return false;
    }
    // none associate, don't forward
    if (pDevice->uAssocCount == 0)
        return false;

    return true;
}

void RXvWorkItem(struct work_struct *work)
{
	struct vnt_private *pDevice =
		container_of(work, struct vnt_private, read_work_item);
	int ntStatus;
	struct vnt_rcb *pRCB = NULL;

	if (pDevice->Flags & fMP_DISCONNECTED)
		return;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Rx Polling Thread\n");
    spin_lock_irq(&pDevice->lock);

    while ((pDevice->Flags & fMP_POST_READS) &&
            MP_IS_READY(pDevice) &&
            (pDevice->NumRecvFreeList != 0) ) {
        pRCB = pDevice->FirstRecvFreeList;
        pDevice->NumRecvFreeList--;
        DequeueRCB(pDevice->FirstRecvFreeList, pDevice->LastRecvFreeList);
        ntStatus = PIPEnsBulkInUsbRead(pDevice, pRCB);
    }
    pDevice->bIsRxWorkItemQueued = false;
    spin_unlock_irq(&pDevice->lock);

}

void RXvFreeRCB(struct vnt_rcb *pRCB, int bReAllocSkb)
{
	struct vnt_private *pDevice = pRCB->pDevice;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->RXvFreeRCB\n");

	if (bReAllocSkb == false) {
		kfree_skb(pRCB->skb);
		bReAllocSkb = true;
	}

    if (bReAllocSkb == true) {
        pRCB->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
        // todo error handling
        if (pRCB->skb == NULL) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR" Failed to re-alloc rx skb\n");
        }else {
            pRCB->skb->dev = pDevice->dev;
        }
    }
    //
    // Insert the RCB back in the Recv free list
    //
    EnqueueRCB(pDevice->FirstRecvFreeList, pDevice->LastRecvFreeList, pRCB);
    pDevice->NumRecvFreeList++;

    if ((pDevice->Flags & fMP_POST_READS) && MP_IS_READY(pDevice) &&
        (pDevice->bIsRxWorkItemQueued == false) ) {

        pDevice->bIsRxWorkItemQueued = true;
	schedule_work(&pDevice->read_work_item);
    }
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"<----RXFreeRCB %d %d\n",pDevice->NumRecvFreeList, pDevice->NumRecvMngList);
}

void RXvMngWorkItem(struct work_struct *work)
{
	struct vnt_private *pDevice =
		container_of(work, struct vnt_private, rx_mng_work_item);
	struct vnt_rcb *pRCB = NULL;
	struct vnt_rx_mgmt *pRxPacket;
	int bReAllocSkb = false;

	if (pDevice->Flags & fMP_DISCONNECTED)
		return;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Rx Mng Thread\n");

    spin_lock_irq(&pDevice->lock);
    while (pDevice->NumRecvMngList!=0)
    {
        pRCB = pDevice->FirstRecvMngList;
        pDevice->NumRecvMngList--;
        DequeueRCB(pDevice->FirstRecvMngList, pDevice->LastRecvMngList);
        if(!pRCB){
            break;
        }
        pRxPacket = &(pRCB->sMngPacket);
	vMgrRxManagePacket(pDevice, &pDevice->vnt_mgmt, pRxPacket);
        pRCB->Ref--;
        if(pRCB->Ref == 0) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"RxvFreeMng %d %d\n",pDevice->NumRecvFreeList, pDevice->NumRecvMngList);
            RXvFreeRCB(pRCB, bReAllocSkb);
        } else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Rx Mng Only we have the right to free RCB\n");
        }
    }

	pDevice->bIsRxMngWorkItemQueued = false;
	spin_unlock_irq(&pDevice->lock);

}

