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
 *      s_bHandleRxEncryption- Rcv decrypted data via on-fly
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
#include "wctl.h"
#include "rf.h"
#include "iowpa.h"
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

static int s_bHandleRxEncryption(struct vnt_private *pDevice, u8 *pbyFrame,
	u32 FrameSize, u8 *pbyRsr, u8 *pbyNewRsr, PSKeyItem *pKeyOut,
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
	s32 iSANodeIndex = -1;
	int ii;
	u8 *pbyRxSts, *pbyRxRate, *pbySQ, *pby3SQ;
	u32 cbHeaderSize;
	PSKeyItem pKey = NULL;
	u16 wRxTSC15_0 = 0;
	u32 dwRxTSC47_16 = 0;
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

    if (IS_FC_WEP(pbyFrame)) {
        bool     bRxDecryOK = false;

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"rx WEP pkt\n");
        bIsWEP = true;

	bRxDecryOK = s_bHandleRxEncryption(pDevice, pbyFrame, FrameSize,
		pbyRsr, pbyNewRsr, &pKey, &bExtIV, &wRxTSC15_0, &dwRxTSC47_16);

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

void RXvWorkItem(struct work_struct *work)
{
	struct vnt_private *priv =
		container_of(work, struct vnt_private, read_work_item);
	int status;
	struct vnt_rcb *rcb = NULL;
	unsigned long flags;

	if (priv->Flags & fMP_DISCONNECTED)
		return;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Rx Polling Thread\n");

	spin_lock_irqsave(&priv->lock, flags);

	while ((priv->Flags & fMP_POST_READS) && MP_IS_READY(priv) &&
			(priv->NumRecvFreeList != 0)) {
		rcb = priv->FirstRecvFreeList;

		priv->NumRecvFreeList--;

		DequeueRCB(priv->FirstRecvFreeList, priv->LastRecvFreeList);

		status = PIPEnsBulkInUsbRead(priv, rcb);
	}

	priv->bIsRxWorkItemQueued = false;

	spin_unlock_irqrestore(&priv->lock, flags);
}

void RXvFreeRCB(struct vnt_rcb *rcb, int re_alloc_skb)
{
	struct vnt_private *priv = rcb->pDevice;

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->RXvFreeRCB\n");

	if (re_alloc_skb == false) {
		kfree_skb(rcb->skb);
		re_alloc_skb = true;
	}

	if (re_alloc_skb == true) {
		rcb->skb = netdev_alloc_skb(priv->dev, priv->rx_buf_sz);
		/* TODO error handling */
		if (!rcb->skb) {
			DBG_PRT(MSG_LEVEL_ERR, KERN_ERR
				" Failed to re-alloc rx skb\n");
		}
	}

	/* Insert the RCB back in the Recv free list */
	EnqueueRCB(priv->FirstRecvFreeList, priv->LastRecvFreeList, rcb);
	priv->NumRecvFreeList++;

	if ((priv->Flags & fMP_POST_READS) && MP_IS_READY(priv) &&
			(priv->bIsRxWorkItemQueued == false)) {
		priv->bIsRxWorkItemQueued = true;
		schedule_work(&priv->read_work_item);
	}

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"<----RXFreeRCB %d %d\n",
			priv->NumRecvFreeList, priv->NumRecvMngList);
}

void RXvMngWorkItem(struct work_struct *work)
{
	struct vnt_private *pDevice =
		container_of(work, struct vnt_private, rx_mng_work_item);
	struct vnt_rcb *pRCB = NULL;
	struct vnt_rx_mgmt *pRxPacket;
	int bReAllocSkb = false;
	unsigned long flags;

	if (pDevice->Flags & fMP_DISCONNECTED)
		return;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"---->Rx Mng Thread\n");

    while (pDevice->NumRecvMngList!=0)
    {
	spin_lock_irqsave(&pDevice->lock, flags);

        pRCB = pDevice->FirstRecvMngList;
        pDevice->NumRecvMngList--;
        DequeueRCB(pDevice->FirstRecvMngList, pDevice->LastRecvMngList);

	spin_unlock_irqrestore(&pDevice->lock, flags);

        if(!pRCB){
            break;
        }
        pRxPacket = &(pRCB->sMngPacket);
	vMgrRxManagePacket(pDevice, &pDevice->vnt_mgmt, pRxPacket);
        pRCB->Ref--;
	if (pRCB->Ref == 0) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"RxvFreeMng %d %d\n",
			pDevice->NumRecvFreeList, pDevice->NumRecvMngList);

		spin_lock_irqsave(&pDevice->lock, flags);

		RXvFreeRCB(pRCB, bReAllocSkb);

		spin_unlock_irqrestore(&pDevice->lock, flags);
	} else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Rx Mng Only we have the right to free RCB\n");
        }
    }

	pDevice->bIsRxMngWorkItemQueued = false;
}

