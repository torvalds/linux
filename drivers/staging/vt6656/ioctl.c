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
 * File: ioctl.c
 *
 * Purpose:  private ioctl functions
 *
 * Author: Lyndon Chen
 *
 * Date: Auguest 20, 2003
 *
 * Functions:
 *
 * Revision History:
 *
 */

#include "ioctl.h"
#include "iocmd.h"
#include "mac.h"
#include "card.h"
#include "hostap.h"
#include "wpactl.h"
#include "control.h"
#include "rndis.h"
#include "rf.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/
//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

    SWPAResult wpa_Result;

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

int private_ioctl(PSDevice pDevice, struct ifreq *rq) {

	PSCmdRequest        pReq = (PSCmdRequest)rq;
    PSMgmtObject        pMgmt = &(pDevice->sMgmtObj);
	int 		        result = 0;
    PWLAN_IE_SSID       pItemSSID;
    SCmdBSSJoin         sJoinCmd;
    SCmdZoneTypeSet sZoneTypeCmd;
    SCmdScan            sScanCmd;
    SCmdStartAP         sStartAPCmd;
    SCmdSetWEP          sWEPCmd;
    SCmdValue           sValue;
    SBSSIDList          sList;
    SNodeList           sNodeList;
    PSBSSIDList         pList;
    PSNodeList          pNodeList;
    unsigned int                cbListCount;
    PKnownBSS           pBSS;
    PKnownNodeDB        pNode;
    unsigned int                ii, jj;
    SCmdLinkStatus      sLinkStatus;
    BYTE                abySuppRates[] = {WLAN_EID_SUPP_RATES, 4, 0x02, 0x04, 0x0B, 0x16};
    BYTE                abyNullAddr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    DWORD               dwKeyIndex= 0;
    BYTE                abyScanSSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
    signed long                ldBm;

    pReq->wResult = 0;

    switch(pReq->wCmdCode) {

    case WLAN_CMD_BSS_SCAN:

        if (copy_from_user(&sScanCmd, pReq->data, sizeof(SCmdScan))) {
			result = -EFAULT;
			break;
		};

        pItemSSID = (PWLAN_IE_SSID)sScanCmd.ssid;
        if (pItemSSID->len != 0) {
            memset(abyScanSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
            memcpy(abyScanSSID, pItemSSID, pItemSSID->len + WLAN_IEHDR_LEN);
        }
        spin_lock_irq(&pDevice->lock);

	if (memcmp(pMgmt->abyCurrBSSID, &abyNullAddr[0], 6) == 0)
		BSSvClearBSSList((void *) pDevice, FALSE);
	else
		BSSvClearBSSList((void *) pDevice, pDevice->bLinkPass);

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_BSS_SCAN..begin\n");

	if (pItemSSID->len != 0)
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_BSSID_SCAN,
				 abyScanSSID);
	else
		bScheduleCommand((void *) pDevice, WLAN_CMD_BSSID_SCAN, NULL);

        spin_unlock_irq(&pDevice->lock);
        break;

    case WLAN_CMD_ZONETYPE_SET:
	//mike add :cann't support.
           result=-EOPNOTSUPP;
	  break;

        if (copy_from_user(&sZoneTypeCmd, pReq->data, sizeof(SCmdZoneTypeSet))) {
			result = -EFAULT;
			break;
		};

          if(sZoneTypeCmd.bWrite==TRUE) {
	  //////write zonetype
                if(sZoneTypeCmd.ZoneType == ZoneType_USA) {
                  //set to USA
                   printk("set_ZoneType:USA\n");
		}
                else if(sZoneTypeCmd.ZoneType == ZoneType_Japan) {
                  //set to Japan
                  printk("set_ZoneType:Japan\n");
		}
	       else if(sZoneTypeCmd.ZoneType == ZoneType_Europe) {
                  //set to Europe
                  printk("set_ZoneType:Europe\n");
		}
            }
	else {
          ///////read zonetype
	  BYTE                       zonetype=0;


           if(zonetype == 0x00)  { //USA
             sZoneTypeCmd.ZoneType = ZoneType_USA;
           }
	 else if(zonetype == 0x01) { //Japan
             sZoneTypeCmd.ZoneType = ZoneType_Japan;
	  }
	 else if(zonetype == 0x02) { //Europe
             sZoneTypeCmd.ZoneType = ZoneType_Europe;
	 }
	 else { //Unknown ZoneType
	        printk("Error:ZoneType[%x] Unknown ???\n",zonetype);
	         result = -EFAULT;
		break;
	 }
	   if (copy_to_user(pReq->data, &sZoneTypeCmd, sizeof(SCmdZoneTypeSet))) {
			result = -EFAULT;
			break;
		};
	}

	     break;

    case WLAN_CMD_BSS_JOIN:

        if (copy_from_user(&sJoinCmd, pReq->data, sizeof(SCmdBSSJoin))) {
			result = -EFAULT;
			break;
		};

        pItemSSID = (PWLAN_IE_SSID)sJoinCmd.ssid;
        memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
		memcpy(pMgmt->abyDesireSSID, pItemSSID, pItemSSID->len + WLAN_IEHDR_LEN);
	    if (sJoinCmd.wBSSType == ADHOC) {
	        pMgmt->eConfigMode = WMAC_CONFIG_IBSS_STA;
	        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ioct set to adhoc mode\n");
	    }
	    else {
	        pMgmt->eConfigMode = WMAC_CONFIG_ESS_STA;
	        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ioct set to STA mode\n");
	    }
	    if (sJoinCmd.bPSEnable == TRUE) {
            pDevice->ePSMode = WMAC_POWER_FAST;
//            pDevice->ePSMode = WMAC_POWER_MAX;
            pMgmt->wListenInterval = 2;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Power Saving On\n");
        }
        else {
            pDevice->ePSMode = WMAC_POWER_CAM;
            pMgmt->wListenInterval = 1;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Power Saving Off \n");
        }

        if (sJoinCmd.bShareKeyAuth == TRUE){
            pMgmt->bShareKeyAlgorithm = TRUE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Share Key \n");
        }
        else {
            pMgmt->bShareKeyAlgorithm = FALSE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Open System \n");
        }
	    pDevice->uChannel = sJoinCmd.uChannel;
        netif_stop_queue(pDevice->dev);
        spin_lock_irq(&pDevice->lock);
        pMgmt->eCurrState = WMAC_STATE_IDLE;
	bScheduleCommand((void *) pDevice,
			 WLAN_CMD_BSSID_SCAN,
			 pMgmt->abyDesireSSID);
	bScheduleCommand((void *) pDevice, WLAN_CMD_SSID, NULL);
        spin_unlock_irq(&pDevice->lock);
        break;

    case WLAN_CMD_SET_WEP:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_WEP Key. \n");
        memset(&sWEPCmd, 0 ,sizeof(SCmdSetWEP));
        if (copy_from_user(&sWEPCmd, pReq->data, sizeof(SCmdSetWEP))) {
			result = -EFAULT;
			break;
		};
	    if (sWEPCmd.bEnableWep != TRUE) {
	        int uu;

            pDevice->bEncryptionEnable = FALSE;
            pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;
            spin_lock_irq(&pDevice->lock);
	    for (uu = 0; uu < MAX_KEY_TABLE; uu++)
		MACvDisableKeyEntry(pDevice, uu);
            spin_unlock_irq(&pDevice->lock);
	    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WEP function disable.\n");
            break;
        }

        for (ii = 0; ii < WLAN_WEP_NKEYS; ii ++) {
            if (sWEPCmd.bWepKeyAvailable[ii]) {
                if (ii == sWEPCmd.byKeyIndex)
//2006-1207-01<Modify>by Einsn Liu
//		    dwKeyIndex|= (1 << 31);
                    dwKeyIndex=ii|(1 << 31);
                else
                    dwKeyIndex = ii;
                spin_lock_irq(&pDevice->lock);
                KeybSetDefaultKey(  pDevice,
                                    &(pDevice->sKey),
                                    dwKeyIndex,
                                    sWEPCmd.auWepKeyLength[ii],
                                    NULL,
                                    (PBYTE)&sWEPCmd.abyWepKey[ii][0],
                                    KEY_CTL_WEP
                                  );
               spin_unlock_irq(&pDevice->lock);

            }
        }
        pDevice->byKeyIndex = sWEPCmd.byKeyIndex;
        pDevice->bTransmitKey = TRUE;
        pDevice->bEncryptionEnable = TRUE;
        pDevice->eEncryptionStatus = Ndis802_11Encryption1Enabled;

        break;

    case WLAN_CMD_GET_LINK:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_GET_LINK status. \n");

        memset(sLinkStatus.abySSID, 0 , WLAN_SSID_MAXLEN + 1);

        if (pMgmt->eCurrMode == WMAC_MODE_IBSS_STA)
            sLinkStatus.wBSSType = ADHOC;
        else
            sLinkStatus.wBSSType = INFRA;

        if (pMgmt->eCurrState == WMAC_STATE_JOINTED)
            sLinkStatus.byState = ADHOC_JOINTED;
        else
            sLinkStatus.byState = ADHOC_STARTED;

        sLinkStatus.uChannel = pMgmt->uCurrChannel;
        if (pDevice->bLinkPass == TRUE) {
            sLinkStatus.bLink = TRUE;
 		    pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;
		    memcpy(sLinkStatus.abySSID, pItemSSID->abySSID, pItemSSID->len);
		    memcpy(sLinkStatus.abyBSSID, pMgmt->abyCurrBSSID, WLAN_BSSID_LEN);
		    sLinkStatus.uLinkRate = pMgmt->sNodeDBTable[0].wTxDataRate;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" Link Success ! \n");
        }
        else {
            sLinkStatus.bLink = FALSE;
        }
        if (copy_to_user(pReq->data, &sLinkStatus, sizeof(SCmdLinkStatus))) {
			result = -EFAULT;
			break;
		};

        break;

    case WLAN_CMD_GET_LISTLEN:
		cbListCount = 0;
		pBSS = &(pMgmt->sBSSList[0]);
        for (ii = 0; ii < MAX_BSS_NUM; ii++) {
            pBSS = &(pMgmt->sBSSList[ii]);
            if (!pBSS->bActive)
                continue;
            cbListCount++;
        };
        sList.uItem = cbListCount;
        if (copy_to_user(pReq->data, &sList, sizeof(SBSSIDList))) {
			result = -EFAULT;
			break;
		};
        pReq->wResult = 0;
        break;

    case WLAN_CMD_GET_LIST:
        if (copy_from_user(&sList, pReq->data, sizeof(SBSSIDList))) {
			result = -EFAULT;
			break;
		};
        pList = (PSBSSIDList)kmalloc(sizeof(SBSSIDList) + (sList.uItem * sizeof(SBSSIDItem)), (int)GFP_ATOMIC);
        if (pList == NULL) {
            result = -ENOMEM;
            break;
        }
		pList->uItem = sList.uItem;
		pBSS = &(pMgmt->sBSSList[0]);
        for (ii = 0, jj = 0; jj < MAX_BSS_NUM ; jj++) {
            pBSS = &(pMgmt->sBSSList[jj]);
            if (pBSS->bActive) {
    		    pList->sBSSIDList[ii].uChannel = pBSS->uChannel;
    		    pList->sBSSIDList[ii].wBeaconInterval = pBSS->wBeaconInterval;
    		    pList->sBSSIDList[ii].wCapInfo = pBSS->wCapInfo;
    		    RFvRSSITodBm(pDevice, (BYTE)(pBSS->uRSSI), &ldBm);
		    pList->sBSSIDList[ii].uRSSI = (unsigned int) ldBm;
//    		    pList->sBSSIDList[ii].uRSSI = pBSS->uRSSI;
    		    memcpy(pList->sBSSIDList[ii].abyBSSID, pBSS->abyBSSID, WLAN_BSSID_LEN);
    		    pItemSSID = (PWLAN_IE_SSID)pBSS->abySSID;
    		    memset(pList->sBSSIDList[ii].abySSID, 0, WLAN_SSID_MAXLEN + 1);
    		    memcpy(pList->sBSSIDList[ii].abySSID, pItemSSID->abySSID, pItemSSID->len);
                if (WLAN_GET_CAP_INFO_ESS(pBSS->wCapInfo)) {
    		        pList->sBSSIDList[ii].byNetType = INFRA;
                }
                else {
    		        pList->sBSSIDList[ii].byNetType = ADHOC;
    		    }
    		    if (WLAN_GET_CAP_INFO_PRIVACY(pBSS->wCapInfo)) {
    		        pList->sBSSIDList[ii].bWEPOn = TRUE;
                }
                else {
    		        pList->sBSSIDList[ii].bWEPOn = FALSE;
    		    }
    		    ii ++;
    		    if (ii >= pList->uItem)
    		        break;
            }
        }

        if (copy_to_user(pReq->data, pList, sizeof(SBSSIDList) + (sList.uItem * sizeof(SBSSIDItem)))) {
			result = -EFAULT;
			break;
		};
        kfree(pList);
        pReq->wResult = 0;
        break;

    case WLAN_CMD_GET_MIB:
        if (copy_to_user(pReq->data, &(pDevice->s802_11Counter), sizeof(SDot11MIBCount))) {
			result = -EFAULT;
			break;
		};
        break;

    case WLAN_CMD_GET_STAT:
        if (copy_to_user(pReq->data, &(pDevice->scStatistic), sizeof(SStatCounter))) {
			result = -EFAULT;
			break;
		};
        break;
    case WLAN_CMD_STOP_MAC:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_STOP_MAC\n");
        // Todo xxxxxx
        netif_stop_queue(pDevice->dev);
        spin_lock_irq(&pDevice->lock);
        if (pDevice->bRadioOff == FALSE) {
            CARDbRadioPowerOff(pDevice);
        }
        pDevice->bLinkPass = FALSE;
        ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
        memset(pMgmt->abyCurrBSSID, 0, 6);
        pMgmt->eCurrState = WMAC_STATE_IDLE;
//        del_timer(&pDevice->sTimerCommand);
//        del_timer(&pMgmt->sTimerSecondCallback);
        pDevice->bCmdRunning = FALSE;
        spin_unlock_irq(&pDevice->lock);

        break;

    case WLAN_CMD_START_MAC:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_START_MAC\n");
        // Todo xxxxxxx
        if (pDevice->bRadioOff == TRUE)
            CARDbRadioPowerOn(pDevice);
        break;

    case WLAN_CMD_SET_HOSTAPD:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_HOSTAPD\n");

        if (copy_from_user(&sValue, pReq->data, sizeof(SCmdValue))) {
			result = -EFAULT;
			break;
		};
		if (sValue.dwValue == 1) {
            if (vt6656_hostap_set_hostapd(pDevice, 1, 1) == 0){
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Enable HOSTAP\n");
            }
            else {
			    result = -EFAULT;
			    break;
			}
        }
        else {
            vt6656_hostap_set_hostapd(pDevice, 0, 1);
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disable HOSTAP\n");
        }

        break;

    case WLAN_CMD_SET_HOSTAPD_STA:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_HOSTAPD_STA\n");

        break;
    case WLAN_CMD_SET_802_1X:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_802_1X\n");
        if (copy_from_user(&sValue, pReq->data, sizeof(SCmdValue))) {
			result = -EFAULT;
			break;
		};

		if (sValue.dwValue == 1) {
            pDevice->bEnable8021x = TRUE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Enable 802.1x\n");
        }
        else {
            pDevice->bEnable8021x = FALSE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disable 802.1x\n");
        }

        break;


    case WLAN_CMD_SET_HOST_WEP:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_HOST_WEP\n");
        if (copy_from_user(&sValue, pReq->data, sizeof(SCmdValue))) {
			result = -EFAULT;
			break;
		};

		if (sValue.dwValue == 1) {
            pDevice->bEnableHostWEP = TRUE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Enable HostWEP\n");
        }
        else {
            pDevice->bEnableHostWEP = FALSE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Disable HostWEP\n");
        }

        break;

    case WLAN_CMD_SET_WPA:
         DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_SET_WPA\n");

        if (copy_from_user(&sValue, pReq->data, sizeof(SCmdValue))) {
			result = -EFAULT;
			break;
		};
		if (sValue.dwValue == 1) {
                     DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "up wpadev\n");
		   memcpy(pDevice->wpadev->dev_addr,
			  pDevice->dev->dev_addr,
			  ETH_ALEN);
		   pDevice->bWPADEVUp = TRUE;
        }
        else {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "close wpadev\n");
	   pDevice->bWPADEVUp = FALSE;
        }

        break;

    case WLAN_CMD_AP_START:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "WLAN_CMD_AP_START\n");
        if (pDevice->bRadioOff == TRUE) {
            CARDbRadioPowerOn(pDevice);
            add_timer(&pMgmt->sTimerSecondCallback);
        }
        if (copy_from_user(&sStartAPCmd, pReq->data, sizeof(SCmdStartAP))) {
			result = -EFAULT;
			break;
		};

	    if (sStartAPCmd.wBSSType == AP) {
	        pMgmt->eConfigMode = WMAC_CONFIG_AP;
	        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ioct set to AP mode\n");
	    }
	    else {
	        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "ioct BSS type not set to AP mode\n");
			result = -EFAULT;
			break;
	    }


	    if (sStartAPCmd.wBBPType == PHY80211g) {
            pMgmt->byAPBBType = PHY_TYPE_11G;
        }
        else if (sStartAPCmd.wBBPType == PHY80211a) {
                 pMgmt->byAPBBType = PHY_TYPE_11A;
        }
        else {
            pMgmt->byAPBBType = PHY_TYPE_11B;
        }

        pItemSSID = (PWLAN_IE_SSID)sStartAPCmd.ssid;
        memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
		memcpy(pMgmt->abyDesireSSID, pItemSSID, pItemSSID->len + WLAN_IEHDR_LEN);

	    if ((sStartAPCmd.uChannel > 0)&&(sStartAPCmd.uChannel <= 14))
	        pDevice->uChannel = sStartAPCmd.uChannel;

	    if ((sStartAPCmd.uBeaconInt >= 20) && (sStartAPCmd.uBeaconInt <= 1000))
            pMgmt->wIBSSBeaconPeriod = sStartAPCmd.uBeaconInt;
        else
            pMgmt->wIBSSBeaconPeriod = 100;

        if (sStartAPCmd.bShareKeyAuth == TRUE){
            pMgmt->bShareKeyAlgorithm = TRUE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Share Key \n");
        }
        else {
            pMgmt->bShareKeyAlgorithm = FALSE;
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Open System \n");
        }
        memcpy(pMgmt->abyIBSSSuppRates, abySuppRates, 6);

        if (sStartAPCmd.byBasicRate & BIT3) {
            pMgmt->abyIBSSSuppRates[2] |= BIT7;
            pMgmt->abyIBSSSuppRates[3] |= BIT7;
            pMgmt->abyIBSSSuppRates[4] |= BIT7;
            pMgmt->abyIBSSSuppRates[5] |= BIT7;
        }else if (sStartAPCmd.byBasicRate & BIT2) {
             pMgmt->abyIBSSSuppRates[2] |= BIT7;
             pMgmt->abyIBSSSuppRates[3] |= BIT7;
             pMgmt->abyIBSSSuppRates[4] |= BIT7;
        }else if (sStartAPCmd.byBasicRate & BIT1) {
             pMgmt->abyIBSSSuppRates[2] |= BIT7;
             pMgmt->abyIBSSSuppRates[3] |= BIT7;
        }else if (sStartAPCmd.byBasicRate & BIT1) {
             pMgmt->abyIBSSSuppRates[2] |= BIT7;
        }else {
            //default 1,2M
             pMgmt->abyIBSSSuppRates[2] |= BIT7;
             pMgmt->abyIBSSSuppRates[3] |= BIT7;
        }

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Support Rate= %x %x %x %x\n",
                pMgmt->abyIBSSSuppRates[2],
                pMgmt->abyIBSSSuppRates[3],
                pMgmt->abyIBSSSuppRates[4],
                pMgmt->abyIBSSSuppRates[5]
                );

        netif_stop_queue(pDevice->dev);
        spin_lock_irq(&pDevice->lock);
	bScheduleCommand((void *) pDevice, WLAN_CMD_RUN_AP, NULL);
        spin_unlock_irq(&pDevice->lock);
        break;

    case WLAN_CMD_GET_NODE_CNT:

		cbListCount = 0;
		pNode = &(pMgmt->sNodeDBTable[0]);
        for (ii = 0; ii < (MAX_NODE_NUM + 1); ii++) {
            pNode = &(pMgmt->sNodeDBTable[ii]);
            if (!pNode->bActive)
                continue;
            cbListCount++;
        };

        sNodeList.uItem = cbListCount;
        if (copy_to_user(pReq->data, &sNodeList, sizeof(SNodeList))) {
			result = -EFAULT;
			break;
		};
        pReq->wResult = 0;
        break;

    case WLAN_CMD_GET_NODE_LIST:

        if (copy_from_user(&sNodeList, pReq->data, sizeof(SNodeList))) {
			result = -EFAULT;
			break;
		};
        pNodeList = (PSNodeList)kmalloc(sizeof(SNodeList) + (sNodeList.uItem * sizeof(SNodeItem)), (int)GFP_ATOMIC);
        if (pNodeList == NULL) {
            result = -ENOMEM;
            break;
        }
		pNodeList->uItem = sNodeList.uItem;
		pNode = &(pMgmt->sNodeDBTable[0]);
        for (ii = 0, jj = 0; ii < (MAX_NODE_NUM + 1); ii++) {
            pNode = &(pMgmt->sNodeDBTable[ii]);
            if (pNode->bActive) {
    		    pNodeList->sNodeList[jj].wAID = pNode->wAID;
    		    memcpy(pNodeList->sNodeList[jj].abyMACAddr, pNode->abyMACAddr, WLAN_ADDR_LEN);
    		    pNodeList->sNodeList[jj].wTxDataRate = pNode->wTxDataRate;
    		    pNodeList->sNodeList[jj].wInActiveCount = (WORD)pNode->uInActiveCount;
    		    pNodeList->sNodeList[jj].wEnQueueCnt = (WORD)pNode->wEnQueueCnt;
    		    pNodeList->sNodeList[jj].wFlags = (WORD)pNode->dwFlags;
    		    pNodeList->sNodeList[jj].bPWBitOn = pNode->bPSEnable;
    		    pNodeList->sNodeList[jj].byKeyIndex = pNode->byKeyIndex;
    		    pNodeList->sNodeList[jj].wWepKeyLength = pNode->uWepKeyLength;
    		    memcpy(&(pNodeList->sNodeList[jj].abyWepKey[0]), &(pNode->abyWepKey[0]), WEP_KEYMAXLEN);
                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "key= %2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
                            pNodeList->sNodeList[jj].abyWepKey[0],
                            pNodeList->sNodeList[jj].abyWepKey[1],
                            pNodeList->sNodeList[jj].abyWepKey[2],
                            pNodeList->sNodeList[jj].abyWepKey[3],
                            pNodeList->sNodeList[jj].abyWepKey[4]
                           );
    		    pNodeList->sNodeList[jj].bIsInFallback = pNode->bIsInFallback;
    		    pNodeList->sNodeList[jj].uTxFailures = pNode->uTxFailures;
    		    pNodeList->sNodeList[jj].uTxAttempts = pNode->uTxAttempts;
    		    pNodeList->sNodeList[jj].wFailureRatio = (WORD)pNode->uFailureRatio;
    		    jj ++;
    		    if (jj >= pNodeList->uItem)
    		        break;
    		}
		};
        if (copy_to_user(pReq->data, pNodeList, sizeof(SNodeList) + (sNodeList.uItem * sizeof(SNodeItem)))) {
			result = -EFAULT;
			break;
		};
        kfree(pNodeList);
        pReq->wResult = 0;
        break;

    case 0xFF:
        memset(wpa_Result.ifname,0,sizeof(wpa_Result.ifname));
	    wpa_Result.proto = 0;
	    wpa_Result.key_mgmt = 0;
	    wpa_Result.eap_type = 0;
	    wpa_Result.authenticated = FALSE;
	      pDevice->fWPA_Authened = FALSE;
        if (copy_from_user(&wpa_Result, pReq->data, sizeof(wpa_Result))) {
            result = -EFAULT;
			break;
		}
//DavidWang  for some AP maybe good authenticate
   if(wpa_Result.key_mgmt==0x20)
      pMgmt->Cisco_cckm =1;
    else
    pMgmt->Cisco_cckm =0;
//DavidWang

if(wpa_Result.authenticated==TRUE) {
   {
     union iwreq_data      wrqu;

     pItemSSID = (PWLAN_IE_SSID)pMgmt->abyCurrSSID;

     memset(&wrqu, 0, sizeof(wrqu));
     wrqu.data.flags = RT_WPACONNECTED_EVENT_FLAG;
     wrqu.data.length =pItemSSID->len;
     wireless_send_event(pDevice->dev, IWEVCUSTOM, &wrqu, pItemSSID->abySSID);
   }
         pDevice->fWPA_Authened = TRUE;           //is successful peer to wpa_Result.authenticated?
}

        //printk("get private wpa_supplicant announce WPA SM\n");
	//printk("wpa-->ifname=%s\n",wpa_Result.ifname);
	//printk("wpa-->proto=%d\n",wpa_Result.proto);
	//printk("wpa-->key-mgmt=%d\n",wpa_Result.key_mgmt);
	//printk("wpa-->eap_type=%d\n",wpa_Result.eap_type);
	//printk("wpa-->authenticated is %s\n",(wpa_Result.authenticated==TRUE)?"TRUE":"FALSE");

	pReq->wResult = 0;
        break;

    default:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Private command not support..\n");
    }

    return result;
}
