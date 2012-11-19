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
 * File: main_usb.c
 *
 * Purpose: driver entry for initial, open, close, tx and rx.
 *
 * Author: Lyndon Chen
 *
 * Date: Dec 8, 2005
 *
 * Functions:
 *
 *   vt6656_probe - module initial (insmod) driver entry
 *   device_remove1 - module remove entry
 *   device_open - allocate dma/descripter resource & initial mac/bbp function
 *   device_xmit - asynchronous data tx function
 *   device_set_multi - set mac filter
 *   device_ioctl - ioctl entry
 *   device_close - shutdown mac/bbp & free dma/descriptor resource
 *   device_alloc_frag_buf - rx fragement pre-allocated function
 *   device_free_tx_bufs - free tx buffer function
 *   device_dma0_tx_80211- tx 802.11 frame via dma0
 *   device_dma0_xmit- tx PS buffered frame via dma0
 *   device_init_registers- initial MAC & BBP & RF internal registers.
 *   device_init_rings- initial tx/rx ring buffer
 *   device_init_defrag_cb- initial & allocate de-fragement buffer.
 *   device_tx_srv- tx interrupt service function
 *
 * Revision History:
 */
#undef __NO_VERSION__

#include "device.h"
#include "card.h"
#include "baseband.h"
#include "mac.h"
#include "tether.h"
#include "wmgr.h"
#include "wctl.h"
#include "power.h"
#include "wcmd.h"
#include "iocmd.h"
#include "tcrc.h"
#include "rxtx.h"
#include "bssdb.h"
#include "hostap.h"
#include "wpactl.h"
#include "ioctl.h"
#include "iwctl.h"
#include "dpc.h"
#include "datarate.h"
#include "rf.h"
#include "firmware.h"
#include "rndis.h"
#include "control.h"
#include "channel.h"
#include "int.h"
#include "iowpa.h"

/*---------------------  Static Definitions -------------------------*/
//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

//
// Define module options
//

// Version Information
#define DRIVER_AUTHOR "VIA Networking Technologies, Inc., <lyndonchen@vntek.com.tw>"
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DEVICE_FULL_DRV_NAM);

#define DEVICE_PARAM(N,D) \
        static int N[MAX_UINTS]=OPTION_DEFAULT;\
        module_param_array(N, int, NULL, 0);\
        MODULE_PARM_DESC(N, D);

#define RX_DESC_DEF0     64
DEVICE_PARAM(RxDescriptors0,"Number of receive usb desc buffer");


#define TX_DESC_DEF0     64
DEVICE_PARAM(TxDescriptors0,"Number of transmit usb desc buffer");

#define CHANNEL_DEF     6
DEVICE_PARAM(Channel, "Channel number");


/* PreambleType[] is the preamble length used for transmit.
   0: indicate allows long preamble type
   1: indicate allows short preamble type
*/

#define PREAMBLE_TYPE_DEF     1

DEVICE_PARAM(PreambleType, "Preamble Type");

#define RTS_THRESH_DEF     2347
DEVICE_PARAM(RTSThreshold, "RTS threshold");

#define FRAG_THRESH_DEF     2346
DEVICE_PARAM(FragThreshold, "Fragmentation threshold");


#define DATA_RATE_DEF     13
/* datarate[] index
   0: indicate 1 Mbps   0x02
   1: indicate 2 Mbps   0x04
   2: indicate 5.5 Mbps 0x0B
   3: indicate 11 Mbps  0x16
   4: indicate 6 Mbps   0x0c
   5: indicate 9 Mbps   0x12
   6: indicate 12 Mbps  0x18
   7: indicate 18 Mbps  0x24
   8: indicate 24 Mbps  0x30
   9: indicate 36 Mbps  0x48
  10: indicate 48 Mbps  0x60
  11: indicate 54 Mbps  0x6c
  12: indicate 72 Mbps  0x90
  13: indicate auto rate
*/

DEVICE_PARAM(ConnectionRate, "Connection data rate");

#define OP_MODE_DEF     0
DEVICE_PARAM(OPMode, "Infrastruct, adhoc, AP mode ");

/* OpMode[] is used for transmit.
   0: indicate infrastruct mode used
   1: indicate adhoc mode used
   2: indicate AP mode used
*/


/* PSMode[]
   0: indicate disable power saving mode
   1: indicate enable power saving mode
*/

#define PS_MODE_DEF     0
DEVICE_PARAM(PSMode, "Power saving mode");


#define SHORT_RETRY_DEF     8
DEVICE_PARAM(ShortRetryLimit, "Short frame retry limits");

#define LONG_RETRY_DEF     4
DEVICE_PARAM(LongRetryLimit, "long frame retry limits");

/* BasebandType[] baseband type selected
   0: indicate 802.11a type
   1: indicate 802.11b type
   2: indicate 802.11g type
*/

#define BBP_TYPE_DEF     2
DEVICE_PARAM(BasebandType, "baseband type");



/* 80211hEnable[]
   0: indicate disable 802.11h
   1: indicate enable 802.11h
*/

#define X80211h_MODE_DEF     0

DEVICE_PARAM(b80211hEnable, "802.11h mode");


//
// Static vars definitions
//

static struct usb_device_id vt6656_table[] = {
	{USB_DEVICE(VNT_USB_VENDOR_ID, VNT_USB_PRODUCT_ID)},
	{}
};

// Frequency list (map channels to frequencies)
/*
static const long frequency_list[] = {
    2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462, 2467, 2472, 2484,
    4915, 4920, 4925, 4935, 4940, 4945, 4960, 4980,
    5035, 5040, 5045, 5055, 5060, 5080, 5170, 5180, 5190, 5200, 5210, 5220, 5230, 5240,
    5260, 5280, 5300, 5320, 5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680,
    5700, 5745, 5765, 5785, 5805, 5825
	};


static const struct iw_handler_def	iwctl_handler_def;
*/

/*---------------------  Static Functions  --------------------------*/

static int vt6656_probe(struct usb_interface *intf,
			const struct usb_device_id *id);
static void vt6656_disconnect(struct usb_interface *intf);

#ifdef CONFIG_PM	/* Minimal support for suspend and resume */
static int vt6656_suspend(struct usb_interface *intf, pm_message_t message);
static int vt6656_resume(struct usb_interface *intf);
#endif /* CONFIG_PM */

static struct net_device_stats *device_get_stats(struct net_device *dev);
static int  device_open(struct net_device *dev);
static int  device_xmit(struct sk_buff *skb, struct net_device *dev);
static void device_set_multi(struct net_device *dev);
static int  device_close(struct net_device *dev);
static int  device_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

static BOOL device_init_registers(PSDevice pDevice, DEVICE_INIT_TYPE InitType);
static BOOL device_init_defrag_cb(PSDevice pDevice);
static void device_init_diversity_timer(PSDevice pDevice);
static int  device_dma0_tx_80211(struct sk_buff *skb, struct net_device *dev);

static int  ethtool_ioctl(struct net_device *dev, void *useraddr);
static void device_free_tx_bufs(PSDevice pDevice);
static void device_free_rx_bufs(PSDevice pDevice);
static void device_free_int_bufs(PSDevice pDevice);
static void device_free_frag_bufs(PSDevice pDevice);
static BOOL device_alloc_bufs(PSDevice pDevice);

static int Read_config_file(PSDevice pDevice);
static unsigned char *Config_FileOperation(PSDevice pDevice);
static int Config_FileGetParameter(unsigned char *string,
				   unsigned char *dest,
				   unsigned char *source);

static BOOL device_release_WPADEV(PSDevice pDevice);

static void usb_device_reset(PSDevice pDevice);



/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/


static void
device_set_options(PSDevice pDevice) {

    BYTE    abyBroadcastAddr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    BYTE    abySNAP_RFC1042[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
    u8 abySNAP_Bridgetunnel[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8};

    memcpy(pDevice->abyBroadcastAddr, abyBroadcastAddr, ETH_ALEN);
    memcpy(pDevice->abySNAP_RFC1042, abySNAP_RFC1042, ETH_ALEN);
    memcpy(pDevice->abySNAP_Bridgetunnel, abySNAP_Bridgetunnel, ETH_ALEN);

    pDevice->cbTD = TX_DESC_DEF0;
    pDevice->cbRD = RX_DESC_DEF0;
    pDevice->uChannel = CHANNEL_DEF;
    pDevice->wRTSThreshold = RTS_THRESH_DEF;
    pDevice->wFragmentationThreshold = FRAG_THRESH_DEF;
    pDevice->byShortRetryLimit = SHORT_RETRY_DEF;
    pDevice->byLongRetryLimit = LONG_RETRY_DEF;
    pDevice->wMaxTransmitMSDULifetime = DEFAULT_MSDU_LIFETIME;
    pDevice->byShortPreamble = PREAMBLE_TYPE_DEF;
    pDevice->ePSMode = PS_MODE_DEF;
    pDevice->b11hEnable = X80211h_MODE_DEF;
    pDevice->eOPMode = OP_MODE_DEF;
    pDevice->uConnectionRate = DATA_RATE_DEF;
    if (pDevice->uConnectionRate < RATE_AUTO) pDevice->bFixRate = TRUE;
    pDevice->byBBType = BBP_TYPE_DEF;
    pDevice->byPacketType = pDevice->byBBType;
    pDevice->byAutoFBCtrl = AUTO_FB_0;
    pDevice->bUpdateBBVGA = TRUE;
    pDevice->byFOETuning = 0;
    pDevice->byAutoPwrTunning = 0;
    pDevice->wCTSDuration = 0;
    pDevice->byPreambleType = 0;
    pDevice->bExistSWNetAddr = FALSE;
//    pDevice->bDiversityRegCtlON = TRUE;
    pDevice->bDiversityRegCtlON = FALSE;
}


static void device_init_diversity_timer(PSDevice pDevice)
{
    init_timer(&pDevice->TimerSQ3Tmax1);
    pDevice->TimerSQ3Tmax1.data = (unsigned long)pDevice;
    pDevice->TimerSQ3Tmax1.function = (TimerFunction)TimerSQ3CallBack;
    pDevice->TimerSQ3Tmax1.expires = RUN_AT(HZ);

    init_timer(&pDevice->TimerSQ3Tmax2);
    pDevice->TimerSQ3Tmax2.data = (unsigned long)pDevice;
    pDevice->TimerSQ3Tmax2.function = (TimerFunction)TimerSQ3CallBack;
    pDevice->TimerSQ3Tmax2.expires = RUN_AT(HZ);

    init_timer(&pDevice->TimerSQ3Tmax3);
    pDevice->TimerSQ3Tmax3.data = (unsigned long)pDevice;
    pDevice->TimerSQ3Tmax3.function = (TimerFunction)TimerSQ3Tmax3CallBack;
    pDevice->TimerSQ3Tmax3.expires = RUN_AT(HZ);

    return;
}


//
// Initialization of MAC & BBP registers
//

static BOOL device_init_registers(PSDevice pDevice, DEVICE_INIT_TYPE InitType)
{
    u8 abyBroadcastAddr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    u8 abySNAP_RFC1042[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
    u8 abySNAP_Bridgetunnel[ETH_ALEN] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8};
    BYTE            byAntenna;
    unsigned int            ii;
    CMD_CARD_INIT   sInitCmd;
    int ntStatus = STATUS_SUCCESS;
    RSP_CARD_INIT   sInitRsp;
    PSMgmtObject    pMgmt = &(pDevice->sMgmtObj);
    BYTE            byTmp;
    BYTE            byCalibTXIQ = 0;
    BYTE            byCalibTXDC = 0;
    BYTE            byCalibRXIQ = 0;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "---->INIbInitAdapter. [%d][%d]\n", InitType, pDevice->byPacketType);
	spin_lock_irq(&pDevice->lock);
	if (InitType == DEVICE_INIT_COLD) {
		memcpy(pDevice->abyBroadcastAddr, abyBroadcastAddr, ETH_ALEN);
		memcpy(pDevice->abySNAP_RFC1042, abySNAP_RFC1042, ETH_ALEN);
		memcpy(pDevice->abySNAP_Bridgetunnel,
		       abySNAP_Bridgetunnel,
		       ETH_ALEN);

        if ( !FIRMWAREbCheckVersion(pDevice) ) {
            if (FIRMWAREbDownload(pDevice) == TRUE) {
                if (FIRMWAREbBrach2Sram(pDevice) == FALSE) {
                    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" FIRMWAREbBrach2Sram fail \n");
                  	spin_unlock_irq(&pDevice->lock);
                    return FALSE;
                }
            } else {

                DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" FIRMWAREbDownload fail \n");
                spin_unlock_irq(&pDevice->lock);
                return FALSE;
            }
        }

        if ( !BBbVT3184Init(pDevice) ) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" BBbVT3184Init fail \n");
            spin_unlock_irq(&pDevice->lock);
            return FALSE;
        }
    }

    sInitCmd.byInitClass = (BYTE)InitType;
    sInitCmd.bExistSWNetAddr = (BYTE) pDevice->bExistSWNetAddr;
    for (ii = 0; ii < 6; ii++)
	sInitCmd.bySWNetAddr[ii] = pDevice->abyCurrentNetAddr[ii];
    sInitCmd.byShortRetryLimit = pDevice->byShortRetryLimit;
    sInitCmd.byLongRetryLimit = pDevice->byLongRetryLimit;

    //issue Card_init command to device
    ntStatus = CONTROLnsRequestOut(pDevice,
                                    MESSAGE_TYPE_CARDINIT,
                                    0,
                                    0,
                                    sizeof(CMD_CARD_INIT),
                                    (PBYTE) &(sInitCmd));

    if ( ntStatus != STATUS_SUCCESS ) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" Issue Card init fail \n");
        spin_unlock_irq(&pDevice->lock);
        return FALSE;
    }
    if (InitType == DEVICE_INIT_COLD) {

        ntStatus = CONTROLnsRequestIn(pDevice,MESSAGE_TYPE_INIT_RSP,0,0,sizeof(RSP_CARD_INIT), (PBYTE) &(sInitRsp));

        if (ntStatus != STATUS_SUCCESS) {
            DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Cardinit request in status fail!\n");
            spin_unlock_irq(&pDevice->lock);
            return FALSE;
        }

        //Local ID for AES functions
        ntStatus = CONTROLnsRequestIn(pDevice,
                                    MESSAGE_TYPE_READ,
                                    MAC_REG_LOCALID,
                                    MESSAGE_REQUEST_MACREG,
                                    1,
                                    &pDevice->byLocalID);

        if ( ntStatus != STATUS_SUCCESS ) {
            spin_unlock_irq(&pDevice->lock);
            return FALSE;
        }

        // Do MACbSoftwareReset in MACvInitialize
        // force CCK
        pDevice->bCCK = TRUE;
        pDevice->bProtectMode = FALSE;          //Only used in 11g type, sync with ERP IE
        pDevice->bNonERPPresent = FALSE;
        pDevice->bBarkerPreambleMd = FALSE;
        if ( pDevice->bFixRate ) {
            pDevice->wCurrentRate = (WORD) pDevice->uConnectionRate;
        } else {
            if ( pDevice->byBBType == BB_TYPE_11B )
                pDevice->wCurrentRate = RATE_11M;
            else
                pDevice->wCurrentRate = RATE_54M;
        }

        CHvInitChannelTable(pDevice);

        pDevice->byTopOFDMBasicRate = RATE_24M;
        pDevice->byTopCCKBasicRate = RATE_1M;
        pDevice->byRevId = 0;                   //Target to IF pin while programming to RF chip.
        pDevice->byCurPwr = 0xFF;

        pDevice->byCCKPwr = pDevice->abyEEPROM[EEP_OFS_PWR_CCK];
        pDevice->byOFDMPwrG = pDevice->abyEEPROM[EEP_OFS_PWR_OFDMG];
        // Load power Table
        for (ii=0;ii<14;ii++) {
            pDevice->abyCCKPwrTbl[ii] = pDevice->abyEEPROM[ii + EEP_OFS_CCK_PWR_TBL];
            if (pDevice->abyCCKPwrTbl[ii] == 0)
                pDevice->abyCCKPwrTbl[ii] = pDevice->byCCKPwr;
            pDevice->abyOFDMPwrTbl[ii] = pDevice->abyEEPROM[ii + EEP_OFS_OFDM_PWR_TBL];
            if (pDevice->abyOFDMPwrTbl[ii] == 0)
                pDevice->abyOFDMPwrTbl[ii] = pDevice->byOFDMPwrG;
        }

	  //original zonetype is USA,but customize zonetype is europe,
	  // then need recover 12,13 ,14 channel  with 11 channel
          if(((pDevice->abyEEPROM[EEP_OFS_ZONETYPE] == ZoneType_Japan) ||
	        (pDevice->abyEEPROM[EEP_OFS_ZONETYPE] == ZoneType_Europe))&&
	     (pDevice->byOriginalZonetype == ZoneType_USA)) {
		for (ii = 11; ii < 14; ii++) {
			pDevice->abyCCKPwrTbl[ii] = pDevice->abyCCKPwrTbl[10];
			pDevice->abyOFDMPwrTbl[ii] = pDevice->abyOFDMPwrTbl[10];
		}
	  }

        //{{ RobertYu: 20041124
        pDevice->byOFDMPwrA = 0x34; // same as RFbMA2829SelectChannel
        // Load OFDM A Power Table
        for (ii=0;ii<CB_MAX_CHANNEL_5G;ii++) { //RobertYu:20041224, bug using CB_MAX_CHANNEL
            pDevice->abyOFDMAPwrTbl[ii] = pDevice->abyEEPROM[ii + EEP_OFS_OFDMA_PWR_TBL];
            if (pDevice->abyOFDMAPwrTbl[ii] == 0)
                pDevice->abyOFDMAPwrTbl[ii] = pDevice->byOFDMPwrA;
        }
        //}} RobertYu

        byAntenna = pDevice->abyEEPROM[EEP_OFS_ANTENNA];
        if (byAntenna & EEP_ANTINV)
            pDevice->bTxRxAntInv = TRUE;
        else
            pDevice->bTxRxAntInv = FALSE;

        byAntenna &= (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);

        if (byAntenna == 0) // if not set default is All
            byAntenna = (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN);

        if (byAntenna == (EEP_ANTENNA_AUX | EEP_ANTENNA_MAIN)) {
            pDevice->byAntennaCount = 2;
            pDevice->byTxAntennaMode = ANT_B;
            pDevice->dwTxAntennaSel = 1;
            pDevice->dwRxAntennaSel = 1;
            if (pDevice->bTxRxAntInv == TRUE)
                pDevice->byRxAntennaMode = ANT_A;
            else
                pDevice->byRxAntennaMode = ANT_B;

            if (pDevice->bDiversityRegCtlON)
                pDevice->bDiversityEnable = TRUE;
            else
                pDevice->bDiversityEnable = FALSE;
        } else  {
            pDevice->bDiversityEnable = FALSE;
            pDevice->byAntennaCount = 1;
            pDevice->dwTxAntennaSel = 0;
            pDevice->dwRxAntennaSel = 0;
            if (byAntenna & EEP_ANTENNA_AUX) {
                pDevice->byTxAntennaMode = ANT_A;
                if (pDevice->bTxRxAntInv == TRUE)
                    pDevice->byRxAntennaMode = ANT_B;
                else
                    pDevice->byRxAntennaMode = ANT_A;
            } else {
                pDevice->byTxAntennaMode = ANT_B;
                if (pDevice->bTxRxAntInv == TRUE)
                    pDevice->byRxAntennaMode = ANT_A;
                else
                    pDevice->byRxAntennaMode = ANT_B;
            }
        }
        pDevice->ulDiversityNValue = 100*255;
        pDevice->ulDiversityMValue = 100*16;
        pDevice->byTMax = 1;
        pDevice->byTMax2 = 4;
        pDevice->ulSQ3TH = 0;
        pDevice->byTMax3 = 64;
        // -----------------------------------------------------------------

        //Get Auto Fall Back Type
        pDevice->byAutoFBCtrl = AUTO_FB_0;

        // Set SCAN Time
        pDevice->uScanTime = WLAN_SCAN_MINITIME;

        // default Auto Mode
        //pDevice->NetworkType = Ndis802_11Automode;
        pDevice->eConfigPHYMode = PHY_TYPE_AUTO;
        pDevice->byBBType = BB_TYPE_11G;

        // initialize BBP registers
        pDevice->ulTxPower = 25;

        // Get Channel range
        pDevice->byMinChannel = 1;
        pDevice->byMaxChannel = CB_MAX_CHANNEL;

        // Get RFType
        pDevice->byRFType = sInitRsp.byRFType;

        if ((pDevice->byRFType & RF_EMU) != 0) {
            // force change RevID for VT3253 emu
            pDevice->byRevId = 0x80;
        }

        // Load EEPROM calibrated vt3266 parameters
        if (pDevice->byRFType == RF_VT3226D0) {
            if((pDevice->abyEEPROM[EEP_OFS_MAJOR_VER] == 0x1) &&
                (pDevice->abyEEPROM[EEP_OFS_MINOR_VER] >= 0x4)) {
                byCalibTXIQ = pDevice->abyEEPROM[EEP_OFS_CALIB_TX_IQ];
                byCalibTXDC = pDevice->abyEEPROM[EEP_OFS_CALIB_TX_DC];
                byCalibRXIQ = pDevice->abyEEPROM[EEP_OFS_CALIB_RX_IQ];
                if( (byCalibTXIQ || byCalibTXDC || byCalibRXIQ) ) {
                    ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xFF, 0x03); // CR255, Set BB to support TX/RX IQ and DC compensation Mode
                    ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xFB, byCalibTXIQ); // CR251, TX I/Q Imbalance Calibration
                    ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xFC, byCalibTXDC); // CR252, TX DC-Offset Calibration
                    ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xFD, byCalibRXIQ); // CR253, RX I/Q Imbalance Calibration
                } else {
                // turn off BB Calibration compensation
                    ControlvWriteByte(pDevice, MESSAGE_REQUEST_BBREG, 0xFF, 0x0); // CR255
                }
            }
        }
        pMgmt->eScanType = WMAC_SCAN_PASSIVE;
        pMgmt->uCurrChannel = pDevice->uChannel;
        pMgmt->uIBSSChannel = pDevice->uChannel;
        CARDbSetMediaChannel(pDevice, pMgmt->uCurrChannel);

        // get Permanent network address
        memcpy(pDevice->abyPermanentNetAddr,&(sInitRsp.byNetAddr[0]),6);
	memcpy(pDevice->abyCurrentNetAddr,
	       pDevice->abyPermanentNetAddr,
	       ETH_ALEN);

        // if exist SW network address, use SW network address.

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"Network address = %pM\n",
		pDevice->abyCurrentNetAddr);
    }

    // Set BB and packet type at the same time.
    // Set Short Slot Time, xIFS, and RSPINF.
    if (pDevice->byBBType == BB_TYPE_11A) {
        CARDbAddBasicRate(pDevice, RATE_6M);
        pDevice->bShortSlotTime = TRUE;
    } else {
        CARDbAddBasicRate(pDevice, RATE_1M);
        pDevice->bShortSlotTime = FALSE;
    }
    BBvSetShortSlotTime(pDevice);
    CARDvSetBSSMode(pDevice);

    if (pDevice->bUpdateBBVGA) {
        pDevice->byBBVGACurrent = pDevice->abyBBVGA[0];
        pDevice->byBBVGANew = pDevice->byBBVGACurrent;
        BBvSetVGAGainOffset(pDevice, pDevice->abyBBVGA[0]);
    }

    pDevice->byRadioCtl = pDevice->abyEEPROM[EEP_OFS_RADIOCTL];
    pDevice->bHWRadioOff = FALSE;
    if ( (pDevice->byRadioCtl & EEP_RADIOCTL_ENABLE) != 0 ) {
        ntStatus = CONTROLnsRequestIn(pDevice,
                                    MESSAGE_TYPE_READ,
                                    MAC_REG_GPIOCTL1,
                                    MESSAGE_REQUEST_MACREG,
                                    1,
                                    &byTmp);

        if ( ntStatus != STATUS_SUCCESS ) {
            spin_unlock_irq(&pDevice->lock);
            return FALSE;
        }
        if ( (byTmp & GPIO3_DATA) == 0 ) {
            pDevice->bHWRadioOff = TRUE;
            MACvRegBitsOn(pDevice,MAC_REG_GPIOCTL1,GPIO3_INTMD);
        } else {
            MACvRegBitsOff(pDevice,MAC_REG_GPIOCTL1,GPIO3_INTMD);
            pDevice->bHWRadioOff = FALSE;
        }

    } //EEP_RADIOCTL_ENABLE

    ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_TMLEN,0x38);
    ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
    MACvRegBitsOn(pDevice,MAC_REG_GPIOCTL0,0x01);

    if ((pDevice->bHWRadioOff == TRUE) || (pDevice->bRadioControlOff == TRUE)) {
        CARDbRadioPowerOff(pDevice);
    } else {
        CARDbRadioPowerOn(pDevice);
    }

    spin_unlock_irq(&pDevice->lock);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"<----INIbInitAdapter Exit\n");
    return TRUE;
}

static BOOL device_release_WPADEV(PSDevice pDevice)
{
  viawget_wpa_header *wpahdr;
  int ii=0;
 // wait_queue_head_t	Set_wait;
  //send device close to wpa_supplicant layer
    if (pDevice->bWPADEVUp==TRUE) {
                 wpahdr = (viawget_wpa_header *)pDevice->skb->data;
                 wpahdr->type = VIAWGET_DEVICECLOSE_MSG;
                 wpahdr->resp_ie_len = 0;
                 wpahdr->req_ie_len = 0;
                 skb_put(pDevice->skb, sizeof(viawget_wpa_header));
                 pDevice->skb->dev = pDevice->wpadev;
		 skb_reset_mac_header(pDevice->skb);
                 pDevice->skb->pkt_type = PACKET_HOST;
                 pDevice->skb->protocol = htons(ETH_P_802_2);
                 memset(pDevice->skb->cb, 0, sizeof(pDevice->skb->cb));
                 netif_rx(pDevice->skb);
                 pDevice->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);

 //wait release WPADEV
              //    init_waitqueue_head(&Set_wait);
              //    wait_event_timeout(Set_wait, ((pDevice->wpadev==NULL)&&(pDevice->skb == NULL)),5*HZ);    //1s wait
              while(pDevice->bWPADEVUp==TRUE) {
	        set_current_state(TASK_UNINTERRUPTIBLE);
                 schedule_timeout (HZ/20);          //wait 50ms
                 ii++;
	        if(ii>20)
		  break;
              }
           }
    return TRUE;
}

#ifdef CONFIG_PM	/* Minimal support for suspend and resume */

static int vt6656_suspend(struct usb_interface *intf, pm_message_t message)
{
	PSDevice device = usb_get_intfdata(intf);

	if (!device || !device->dev)
		return -ENODEV;

	if (device->flags & DEVICE_FLAGS_OPENED)
		device_close(device->dev);

	usb_put_dev(interface_to_usbdev(intf));

	return 0;
}

static int vt6656_resume(struct usb_interface *intf)
{
	PSDevice device = usb_get_intfdata(intf);

	if (!device || !device->dev)
		return -ENODEV;

	usb_get_dev(interface_to_usbdev(intf));

	if (!(device->flags & DEVICE_FLAGS_OPENED))
		device_open(device->dev);

	return 0;
}

#endif /* CONFIG_PM */

static const struct net_device_ops device_netdev_ops = {
    .ndo_open               = device_open,
    .ndo_stop               = device_close,
    .ndo_do_ioctl           = device_ioctl,
    .ndo_get_stats          = device_get_stats,
    .ndo_start_xmit         = device_xmit,
    .ndo_set_rx_mode	    = device_set_multi,
};

static int
vt6656_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	u8 fake_mac[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	struct usb_device *udev = interface_to_usbdev(intf);
	int rc = 0;
	struct net_device *netdev = NULL;
	PSDevice pDevice = NULL;

	printk(KERN_NOTICE "%s Ver. %s\n", DEVICE_FULL_DRV_NAM, DEVICE_VERSION);
	printk(KERN_NOTICE "Copyright (c) 2004 VIA Networking Technologies, Inc.\n");

	udev = usb_get_dev(udev);
	netdev = alloc_etherdev(sizeof(DEVICE_INFO));
	if (!netdev) {
		printk(KERN_ERR DEVICE_NAME ": allocate net device failed\n");
		rc = -ENOMEM;
		goto err_nomem;
	}

	pDevice = netdev_priv(netdev);
	memset(pDevice, 0, sizeof(DEVICE_INFO));

	pDevice->dev = netdev;
	pDevice->usb = udev;

	device_set_options(pDevice);
	spin_lock_init(&pDevice->lock);

	pDevice->tx_80211 = device_dma0_tx_80211;
	pDevice->sMgmtObj.pAdapter = (void *) pDevice;

	netdev->netdev_ops = &device_netdev_ops;
	netdev->wireless_handlers =
		(struct iw_handler_def *) &iwctl_handler_def;

	usb_set_intfdata(intf, pDevice);
	SET_NETDEV_DEV(netdev, &intf->dev);
	memcpy(pDevice->dev->dev_addr, fake_mac, ETH_ALEN);
	rc = register_netdev(netdev);
	if (rc) {
		printk(KERN_ERR DEVICE_NAME " Failed to register netdev\n");
		goto err_netdev;
	}

	usb_device_reset(pDevice);

	{
		union iwreq_data wrqu;
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.flags = RT_INSMOD_EVENT_FLAG;
		wrqu.data.length = IFNAMSIZ;
		wireless_send_event(pDevice->dev,
				    IWEVCUSTOM,
				    &wrqu,
				    pDevice->dev->name);
	}

	return 0;

err_netdev:
	free_netdev(netdev);
err_nomem:
	usb_put_dev(udev);

	return rc;
}

static void device_free_tx_bufs(PSDevice pDevice)
{
    PUSB_SEND_CONTEXT pTxContext;
    int ii;

    for (ii = 0; ii < pDevice->cbTD; ii++) {

        pTxContext = pDevice->apTD[ii];
        //de-allocate URBs
        if (pTxContext->pUrb) {
            usb_kill_urb(pTxContext->pUrb);
            usb_free_urb(pTxContext->pUrb);
        }
        kfree(pTxContext);
    }
    return;
}


static void device_free_rx_bufs(PSDevice pDevice)
{
    PRCB pRCB;
    int ii;

    for (ii = 0; ii < pDevice->cbRD; ii++) {

        pRCB = pDevice->apRCB[ii];
        //de-allocate URBs
        if (pRCB->pUrb) {
            usb_kill_urb(pRCB->pUrb);
            usb_free_urb(pRCB->pUrb);
        }
        //de-allocate skb
        if (pRCB->skb)
            dev_kfree_skb(pRCB->skb);
    }
    kfree(pDevice->pRCBMem);

    return;
}

static void usb_device_reset(PSDevice pDevice)
{
 int status;
 status = usb_reset_device(pDevice->usb);
	if (status)
            printk("usb_device_reset fail status=%d\n",status);
	return ;
}

static void device_free_int_bufs(PSDevice pDevice)
{
    kfree(pDevice->intBuf.pDataBuf);
    return;
}


static BOOL device_alloc_bufs(PSDevice pDevice) {

    PUSB_SEND_CONTEXT pTxContext;
    PRCB pRCB;
    int ii;


    for (ii = 0; ii < pDevice->cbTD; ii++) {

        pTxContext = kmalloc(sizeof(USB_SEND_CONTEXT), GFP_KERNEL);
        if (pTxContext == NULL) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s : allocate tx usb context failed\n", pDevice->dev->name);
            goto free_tx;
        }
        pDevice->apTD[ii] = pTxContext;
	pTxContext->pDevice = (void *) pDevice;
        //allocate URBs
        pTxContext->pUrb = usb_alloc_urb(0, GFP_ATOMIC);
        if (pTxContext->pUrb == NULL) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "alloc tx urb failed\n");
            goto free_tx;
        }
        pTxContext->bBoolInUse = FALSE;
    }

    // allocate rcb mem
	pDevice->pRCBMem = kzalloc((sizeof(RCB) * pDevice->cbRD), GFP_KERNEL);
    if (pDevice->pRCBMem == NULL) {
        DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s : alloc rx usb context failed\n", pDevice->dev->name);
        goto free_tx;
    }


    pDevice->FirstRecvFreeList = NULL;
    pDevice->LastRecvFreeList = NULL;
    pDevice->FirstRecvMngList = NULL;
    pDevice->LastRecvMngList = NULL;
    pDevice->NumRecvFreeList = 0;
    pRCB = (PRCB) pDevice->pRCBMem;

    for (ii = 0; ii < pDevice->cbRD; ii++) {

        pDevice->apRCB[ii] = pRCB;
	pRCB->pDevice = (void *) pDevice;
        //allocate URBs
        pRCB->pUrb = usb_alloc_urb(0, GFP_ATOMIC);

        if (pRCB->pUrb == NULL) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR" Failed to alloc rx urb\n");
            goto free_rx_tx;
        }
        pRCB->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
        if (pRCB->skb == NULL) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR" Failed to alloc rx skb\n");
            goto free_rx_tx;
        }
        pRCB->skb->dev = pDevice->dev;
        pRCB->bBoolInUse = FALSE;
        EnqueueRCB(pDevice->FirstRecvFreeList, pDevice->LastRecvFreeList, pRCB);
        pDevice->NumRecvFreeList++;
        pRCB++;
    }


	pDevice->pControlURB = usb_alloc_urb(0, GFP_ATOMIC);
	if (pDevice->pControlURB == NULL) {
	    DBG_PRT(MSG_LEVEL_ERR,KERN_ERR"Failed to alloc control urb\n");
	    goto free_rx_tx;
	}

	pDevice->pInterruptURB = usb_alloc_urb(0, GFP_ATOMIC);
	if (pDevice->pInterruptURB == NULL) {
	    DBG_PRT(MSG_LEVEL_ERR,KERN_ERR"Failed to alloc int urb\n");
	    usb_free_urb(pDevice->pControlURB);
	    goto free_rx_tx;
	}

    pDevice->intBuf.pDataBuf = kmalloc(MAX_INTERRUPT_SIZE, GFP_KERNEL);
	if (pDevice->intBuf.pDataBuf == NULL) {
	    DBG_PRT(MSG_LEVEL_ERR,KERN_ERR"Failed to alloc int buf\n");
	    usb_free_urb(pDevice->pControlURB);
	    usb_free_urb(pDevice->pInterruptURB);
	    goto free_rx_tx;
	}

    return TRUE;

free_rx_tx:
    device_free_rx_bufs(pDevice);

free_tx:
    device_free_tx_bufs(pDevice);

	return FALSE;
}




static BOOL device_init_defrag_cb(PSDevice pDevice) {
    int i;
    PSDeFragControlBlock pDeF;

    /* Init the fragment ctl entries */
    for (i = 0; i < CB_MAX_RX_FRAG; i++) {
        pDeF = &(pDevice->sRxDFCB[i]);
        if (!device_alloc_frag_buf(pDevice, pDeF)) {
            DBG_PRT(MSG_LEVEL_ERR,KERN_ERR "%s: can not alloc frag bufs\n",
                pDevice->dev->name);
            goto free_frag;
        }
    }
    pDevice->cbDFCB = CB_MAX_RX_FRAG;
    pDevice->cbFreeDFCB = pDevice->cbDFCB;
    return TRUE;

free_frag:
    device_free_frag_bufs(pDevice);
    return FALSE;
}



static void device_free_frag_bufs(PSDevice pDevice) {
    PSDeFragControlBlock pDeF;
    int i;

    for (i = 0; i < CB_MAX_RX_FRAG; i++) {

        pDeF = &(pDevice->sRxDFCB[i]);

        if (pDeF->skb)
            dev_kfree_skb(pDeF->skb);
    }
}



BOOL device_alloc_frag_buf(PSDevice pDevice, PSDeFragControlBlock pDeF) {

    pDeF->skb = dev_alloc_skb((int)pDevice->rx_buf_sz);
    if (pDeF->skb == NULL)
        return FALSE;
    ASSERT(pDeF->skb);
    pDeF->skb->dev = pDevice->dev;

    return TRUE;
}


/*-----------------------------------------------------------------*/

static int  device_open(struct net_device *dev) {
    PSDevice    pDevice=(PSDevice) netdev_priv(dev);

     extern SWPAResult wpa_Result;
     memset(wpa_Result.ifname,0,sizeof(wpa_Result.ifname));
     wpa_Result.proto = 0;
     wpa_Result.key_mgmt = 0;
     wpa_Result.eap_type = 0;
     wpa_Result.authenticated = FALSE;
     pDevice->fWPA_Authened = FALSE;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " device_open...\n");


    pDevice->rx_buf_sz = MAX_TOTAL_SIZE_WITH_ALL_HEADERS;

    if (device_alloc_bufs(pDevice) == FALSE) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " device_alloc_bufs fail... \n");
        return -ENOMEM;
    }

    if (device_init_defrag_cb(pDevice)== FALSE) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " Initial defragment cb fail \n");
        goto free_rx_tx;
    }

    MP_CLEAR_FLAG(pDevice, fMP_DISCONNECTED);
    MP_CLEAR_FLAG(pDevice, fMP_CONTROL_READS);
    MP_CLEAR_FLAG(pDevice, fMP_CONTROL_WRITES);
    MP_SET_FLAG(pDevice, fMP_POST_READS);
    MP_SET_FLAG(pDevice, fMP_POST_WRITES);

   //read config file
    Read_config_file(pDevice);

    if (device_init_registers(pDevice, DEVICE_INIT_COLD) == FALSE) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " init register fail\n");
        goto free_all;
    }

    device_set_multi(pDevice->dev);
    // Init for Key Management

    KeyvInitTable(pDevice,&pDevice->sKey);
    memcpy(pDevice->sMgmtObj.abyMACAddr, pDevice->abyCurrentNetAddr, ETH_ALEN);
    memcpy(pDevice->dev->dev_addr, pDevice->abyCurrentNetAddr, ETH_ALEN);
    pDevice->bStopTx0Pkt = FALSE;
    pDevice->bStopDataPkt = FALSE;
    pDevice->bRoaming = FALSE;
    pDevice->bIsRoaming = FALSE;
    pDevice->bEnableRoaming = FALSE;
    if (pDevice->bDiversityRegCtlON) {
        device_init_diversity_timer(pDevice);
    }

    vMgrObjectInit(pDevice);
    tasklet_init(&pDevice->RxMngWorkItem, (void *)RXvMngWorkItem, (unsigned long)pDevice);
    tasklet_init(&pDevice->ReadWorkItem, (void *)RXvWorkItem, (unsigned long)pDevice);
    tasklet_init(&pDevice->EventWorkItem, (void *)INTvWorkItem, (unsigned long)pDevice);
    add_timer(&(pDevice->sMgmtObj.sTimerSecondCallback));
    pDevice->int_interval = 100;  //Max 100 microframes.
    pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;

    pDevice->bIsRxWorkItemQueued = TRUE;
    pDevice->fKillEventPollingThread = FALSE;
    pDevice->bEventAvailable = FALSE;

   pDevice->bWPADEVUp = FALSE;
     pDevice->bwextstep0 = FALSE;
     pDevice->bwextstep1 = FALSE;
     pDevice->bwextstep2 = FALSE;
     pDevice->bwextstep3 = FALSE;
     pDevice->bWPASuppWextEnabled = FALSE;
    pDevice->byReAssocCount = 0;

    RXvWorkItem(pDevice);
    INTvWorkItem(pDevice);

    // Patch: if WEP key already set by iwconfig but device not yet open
    if ((pDevice->bEncryptionEnable == TRUE) && (pDevice->bTransmitKey == TRUE)) {
         spin_lock_irq(&pDevice->lock);
         KeybSetDefaultKey( pDevice,
                            &(pDevice->sKey),
                            pDevice->byKeyIndex | (1 << 31),
                            pDevice->uKeyLength,
                            NULL,
                            pDevice->abyKey,
                            KEY_CTL_WEP
                          );
         spin_unlock_irq(&pDevice->lock);
         pDevice->eEncryptionStatus = Ndis802_11Encryption1Enabled;
    }

    if (pDevice->sMgmtObj.eConfigMode == WMAC_CONFIG_AP) {
		bScheduleCommand((void *) pDevice, WLAN_CMD_RUN_AP, NULL);
	}
	else {
	//mike:mark@2008-11-10
	  bScheduleCommand((void *) pDevice, WLAN_CMD_BSSID_SCAN, NULL);
	  /* bScheduleCommand((void *) pDevice, WLAN_CMD_SSID, NULL); */
    }


    netif_stop_queue(pDevice->dev);
    pDevice->flags |= DEVICE_FLAGS_OPENED;

{
  union iwreq_data      wrqu;
  memset(&wrqu, 0, sizeof(wrqu));
  wrqu.data.flags = RT_UPDEV_EVENT_FLAG;
  wireless_send_event(pDevice->dev, IWEVCUSTOM, &wrqu, NULL);
}

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "device_open success.. \n");
    return 0;

free_all:
    device_free_frag_bufs(pDevice);
free_rx_tx:
    device_free_rx_bufs(pDevice);
    device_free_tx_bufs(pDevice);
    device_free_int_bufs(pDevice);
	usb_kill_urb(pDevice->pControlURB);
	usb_kill_urb(pDevice->pInterruptURB);
    usb_free_urb(pDevice->pControlURB);
    usb_free_urb(pDevice->pInterruptURB);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "device_open fail.. \n");
    return -ENOMEM;
}



static int  device_close(struct net_device *dev) {
    PSDevice    pDevice=(PSDevice) netdev_priv(dev);
    PSMgmtObject     pMgmt = &(pDevice->sMgmtObj);

        int uu;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "device_close1 \n");
    if (pDevice == NULL)
        return -ENODEV;

{
  union iwreq_data      wrqu;
  memset(&wrqu, 0, sizeof(wrqu));
  wrqu.data.flags = RT_DOWNDEV_EVENT_FLAG;
  wireless_send_event(pDevice->dev, IWEVCUSTOM, &wrqu, NULL);
}

    if (pDevice->bLinkPass) {
	bScheduleCommand((void *) pDevice, WLAN_CMD_DISASSOCIATE, NULL);
        mdelay(30);
    }

device_release_WPADEV(pDevice);

        memset(pMgmt->abyDesireSSID, 0, WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1);
        pMgmt->bShareKeyAlgorithm = FALSE;
        pDevice->bEncryptionEnable = FALSE;
        pDevice->eEncryptionStatus = Ndis802_11EncryptionDisabled;
	spin_lock_irq(&pDevice->lock);
	for (uu = 0; uu < MAX_KEY_TABLE; uu++)
                MACvDisableKeyEntry(pDevice,uu);
	spin_unlock_irq(&pDevice->lock);

    if ((pDevice->flags & DEVICE_FLAGS_UNPLUG) == FALSE) {
        MACbShutdown(pDevice);
    }
    netif_stop_queue(pDevice->dev);
    MP_SET_FLAG(pDevice, fMP_DISCONNECTED);
    MP_CLEAR_FLAG(pDevice, fMP_POST_WRITES);
    MP_CLEAR_FLAG(pDevice, fMP_POST_READS);
    pDevice->fKillEventPollingThread = TRUE;
    del_timer(&pDevice->sTimerCommand);
    del_timer(&pMgmt->sTimerSecondCallback);

    del_timer(&pDevice->sTimerTxData);

    if (pDevice->bDiversityRegCtlON) {
        del_timer(&pDevice->TimerSQ3Tmax1);
        del_timer(&pDevice->TimerSQ3Tmax2);
        del_timer(&pDevice->TimerSQ3Tmax3);
    }
    tasklet_kill(&pDevice->RxMngWorkItem);
    tasklet_kill(&pDevice->ReadWorkItem);
    tasklet_kill(&pDevice->EventWorkItem);

   pDevice->bRoaming = FALSE;
   pDevice->bIsRoaming = FALSE;
   pDevice->bEnableRoaming = FALSE;
    pDevice->bCmdRunning = FALSE;
    pDevice->bLinkPass = FALSE;
    memset(pMgmt->abyCurrBSSID, 0, 6);
    pMgmt->eCurrState = WMAC_STATE_IDLE;

    device_free_tx_bufs(pDevice);
    device_free_rx_bufs(pDevice);
    device_free_int_bufs(pDevice);
    device_free_frag_bufs(pDevice);

	usb_kill_urb(pDevice->pControlURB);
	usb_kill_urb(pDevice->pInterruptURB);
    usb_free_urb(pDevice->pControlURB);
    usb_free_urb(pDevice->pInterruptURB);

    BSSvClearNodeDBTable(pDevice, 0);
    pDevice->flags &=(~DEVICE_FLAGS_OPENED);

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "device_close2 \n");

    return 0;
}

static void __devexit vt6656_disconnect(struct usb_interface *intf)
{
	PSDevice device = usb_get_intfdata(intf);

	if (!device)
		return;

	{
		union iwreq_data req;
		memset(&req, 0, sizeof(req));
		req.data.flags = RT_RMMOD_EVENT_FLAG;
		wireless_send_event(device->dev, IWEVCUSTOM, &req, NULL);
	}

	device_release_WPADEV(device);

	usb_set_intfdata(intf, NULL);
	usb_put_dev(interface_to_usbdev(intf));

	device->flags |= DEVICE_FLAGS_UNPLUG;

	if (device->dev) {
		unregister_netdev(device->dev);
		wpa_set_wpadev(device, 0);
		free_netdev(device->dev);
	}
}

static int device_dma0_tx_80211(struct sk_buff *skb, struct net_device *dev)
{
	PSDevice pDevice = netdev_priv(dev);

	spin_lock_irq(&pDevice->lock);

	if (unlikely(pDevice->bStopTx0Pkt))
		dev_kfree_skb_irq(skb);
	else
		vDMA0_tx_80211(pDevice, skb);

	spin_unlock_irq(&pDevice->lock);

	return NETDEV_TX_OK;
}

static int device_xmit(struct sk_buff *skb, struct net_device *dev)
{
	PSDevice pDevice = netdev_priv(dev);
	struct net_device_stats *stats = &pDevice->stats;

	spin_lock_irq(&pDevice->lock);

	netif_stop_queue(dev);

	if (!pDevice->bLinkPass) {
		dev_kfree_skb_irq(skb);
		goto out;
	}

	if (pDevice->bStopDataPkt) {
		dev_kfree_skb_irq(skb);
		stats->tx_dropped++;
		goto out;
	}

	if (nsDMA_tx_packet(pDevice, TYPE_AC0DMA, skb)) {
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	}

out:
	spin_unlock_irq(&pDevice->lock);

	return NETDEV_TX_OK;
}

static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
    int crc = -1;

    while(--length >= 0) {
        unsigned char current_octet = *data++;
        int bit;
        for (bit = 0; bit < 8; bit++, current_octet >>= 1) {
            crc = (crc << 1) ^
                ((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
        }
    }
    return crc;
}

//find out the start  position of str2 from str1
static unsigned char *kstrstr(const unsigned char *str1,
			      const unsigned char *str2) {
  int str1_len = strlen(str1);
  int str2_len = strlen(str2);

  while (str1_len >= str2_len) {
       str1_len--;
      if(memcmp(str1,str2,str2_len)==0)
	return (unsigned char *) str1;
        str1++;
  }
  return NULL;
}

static int Config_FileGetParameter(unsigned char *string,
				   unsigned char *dest,
				   unsigned char *source)
{
  unsigned char buf1[100];
  unsigned char buf2[100];
  unsigned char *start_p = NULL, *end_p = NULL, *tmp_p = NULL;
  int ii;

    memset(buf1,0,100);
    strcat(buf1, string);
    strcat(buf1, "=");
    source+=strlen(buf1);

//find target string start point
    start_p = kstrstr(source,buf1);
    if (start_p == NULL)
	return FALSE;

//check if current config line is marked by "#" ??
    for (ii = 1; ; ii++) {
	if (memcmp(start_p - ii, "\n", 1) == 0)
		break;
	if (memcmp(start_p - ii, "#", 1) == 0)
		return FALSE;
    }

//find target string end point
     end_p = kstrstr(start_p,"\n");
     if (end_p == NULL) {       //can't find "\n",but don't care
          end_p=start_p+strlen(start_p);   //no include "\n"
       }

   memset(buf2,0,100);
   memcpy(buf2,start_p,end_p-start_p);    //get the target line
   buf2[end_p-start_p]='\0';

   //find value
   start_p = kstrstr(buf2,"=");
   if (start_p == NULL)
      return FALSE;
   memset(buf1,0,100);
   strcpy(buf1,start_p+1);

  //except space
  tmp_p = buf1;
  while(*tmp_p != 0x00) {
  	if(*tmp_p==' ')
	    tmp_p++;
         else
	  break;
  }

   memcpy(dest,tmp_p,strlen(tmp_p));
 return TRUE;
}

//if read fail,return NULL,or return data pointer;
static unsigned char *Config_FileOperation(PSDevice pDevice)
{
    unsigned char *config_path = CONFIG_PATH;
    unsigned char *buffer = NULL;
    struct file   *filp=NULL;
    mm_segment_t old_fs = get_fs();
    //int oldfsuid=0,oldfsgid=0;
    int result = 0;

    set_fs (KERNEL_DS);
    /* Can't do this anymore, so we rely on correct filesystem permissions:
    //Make sure a caller can read or write power as root
    oldfsuid=current->fsuid;
    oldfsgid=current->fsgid;
    current->fsuid = 0;
    current->fsgid = 0;
    */

    //open file
      filp = filp_open(config_path, O_RDWR, 0);
        if (IS_ERR(filp)) {
	     printk("Config_FileOperation file Not exist\n");
	     result=-1;
             goto error2;
	  }

     if(!(filp->f_op) || !(filp->f_op->read) ||!(filp->f_op->write)) {
           printk("file %s is not read or writeable?\n",config_path);
	  result = -1;
	  goto error1;
     	}

    buffer = kmalloc(1024, GFP_KERNEL);
    if(buffer==NULL) {
      printk("allocate mem for file fail?\n");
      result = -1;
      goto error1;
    }

    if(filp->f_op->read(filp, buffer, 1024, &filp->f_pos)<0) {
     printk("read file error?\n");
     result = -1;
    }

error1:
  if(filp_close(filp,NULL))
       printk("Config_FileOperation:close file fail\n");

error2:
  set_fs (old_fs);

  /*
  current->fsuid=oldfsuid;
  current->fsgid=oldfsgid;
  */

if(result!=0) {
    kfree(buffer);
    buffer=NULL;
}
  return buffer;
}

//return --->-1:fail;  >=0:successful
static int Read_config_file(PSDevice pDevice) {
  int result = 0;
  unsigned char tmpbuffer[100];
  unsigned char *buffer = NULL;

  //init config setting
 pDevice->config_file.ZoneType = -1;
 pDevice->config_file.eAuthenMode = -1;
 pDevice->config_file.eEncryptionStatus = -1;

  buffer = Config_FileOperation(pDevice);
  if (buffer == NULL) {
     result =-1;
     return result;
  }

//get zonetype
{
    memset(tmpbuffer,0,sizeof(tmpbuffer));
    if(Config_FileGetParameter("ZONETYPE",tmpbuffer,buffer) ==TRUE) {
    if(memcmp(tmpbuffer,"USA",3)==0) {
      pDevice->config_file.ZoneType=ZoneType_USA;
    }
    else if(memcmp(tmpbuffer,"JAPAN",5)==0) {
      pDevice->config_file.ZoneType=ZoneType_Japan;
    }
    else if(memcmp(tmpbuffer,"EUROPE",6)==0) {
     pDevice->config_file.ZoneType=ZoneType_Europe;
    }
    else {
      printk("Unknown Zonetype[%s]?\n",tmpbuffer);
   }
 }
}

//get other parameter
  {
	memset(tmpbuffer,0,sizeof(tmpbuffer));
       if(Config_FileGetParameter("AUTHENMODE",tmpbuffer,buffer)==TRUE) {
	 pDevice->config_file.eAuthenMode = (int) simple_strtol(tmpbuffer, NULL, 10);
       }

	memset(tmpbuffer,0,sizeof(tmpbuffer));
       if(Config_FileGetParameter("ENCRYPTIONMODE",tmpbuffer,buffer)==TRUE) {
	 pDevice->config_file.eEncryptionStatus= (int) simple_strtol(tmpbuffer, NULL, 10);
       }
  }

  kfree(buffer);
  return result;
}

static void device_set_multi(struct net_device *dev) {
    PSDevice         pDevice = (PSDevice) netdev_priv(dev);
    PSMgmtObject     pMgmt = &(pDevice->sMgmtObj);
    u32              mc_filter[2];
    int              ii;
    struct netdev_hw_addr *ha;
    BYTE             pbyData[8] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
    BYTE             byTmpMode = 0;
    int              rc;


	spin_lock_irq(&pDevice->lock);
    rc = CONTROLnsRequestIn(pDevice,
                            MESSAGE_TYPE_READ,
                            MAC_REG_RCR,
                            MESSAGE_REQUEST_MACREG,
                            1,
                            &byTmpMode
                            );
    if (rc == 0) pDevice->byRxMode = byTmpMode;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pDevice->byRxMode in= %x\n", pDevice->byRxMode);

    if (dev->flags & IFF_PROMISC) {         // Set promiscuous.
        DBG_PRT(MSG_LEVEL_ERR,KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
        // Unconditionally log net taps.
        pDevice->byRxMode |= (RCR_MULTICAST|RCR_BROADCAST|RCR_UNICAST);
    }
    else if ((netdev_mc_count(dev) > pDevice->multicast_limit) ||
	     (dev->flags & IFF_ALLMULTI)) {
        CONTROLnsRequestOut(pDevice,
                            MESSAGE_TYPE_WRITE,
                            MAC_REG_MAR0,
                            MESSAGE_REQUEST_MACREG,
                            8,
                            pbyData
                            );
        pDevice->byRxMode |= (RCR_MULTICAST|RCR_BROADCAST);
    }
    else {
        memset(mc_filter, 0, sizeof(mc_filter));
	netdev_for_each_mc_addr(ha, dev) {
            int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;
            mc_filter[bit_nr >> 5] |= cpu_to_le32(1 << (bit_nr & 31));
        }
        for (ii = 0; ii < 4; ii++) {
             MACvWriteMultiAddr(pDevice, ii, *((PBYTE)&mc_filter[0] + ii));
             MACvWriteMultiAddr(pDevice, ii+ 4, *((PBYTE)&mc_filter[1] + ii));
        }
        pDevice->byRxMode &= ~(RCR_UNICAST);
        pDevice->byRxMode |= (RCR_MULTICAST|RCR_BROADCAST);
    }

    if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
        // If AP mode, don't enable RCR_UNICAST. Since hw only compare addr1 with local mac.
        pDevice->byRxMode |= (RCR_MULTICAST|RCR_BROADCAST);
        pDevice->byRxMode &= ~(RCR_UNICAST);
    }
    ControlvWriteByte(pDevice, MESSAGE_REQUEST_MACREG, MAC_REG_RCR, pDevice->byRxMode);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "pDevice->byRxMode out= %x\n", pDevice->byRxMode);
	spin_unlock_irq(&pDevice->lock);

}


static struct net_device_stats *device_get_stats(struct net_device *dev) {
    PSDevice pDevice=(PSDevice) netdev_priv(dev);

    return &pDevice->stats;
}


static int  device_ioctl(struct net_device *dev, struct ifreq *rq, int cmd) {
	PSDevice pDevice = (PSDevice)netdev_priv(dev);
	PSMgmtObject pMgmt = &pDevice->sMgmtObj;
	PSCmdRequest pReq;
	u8 *buffer;
	struct iwreq *wrq = (struct iwreq *) rq;
	int rc = 0;

    if (pMgmt == NULL) {
        rc = -EFAULT;
        return rc;
    }

    switch(cmd) {

	case SIOCGIWNAME:
		rc = iwctl_giwname(dev, NULL, (char *)&(wrq->u.name), NULL);
		break;

	case SIOCSIWNWID:
	case SIOCGIWNWID:     //0x8b03  support
		rc = -EOPNOTSUPP;
		break;

		// Set frequency/channel
	case SIOCSIWFREQ:
	    rc = iwctl_siwfreq(dev, NULL, &(wrq->u.freq), NULL);
		break;

		// Get frequency/channel
	case SIOCGIWFREQ:
		rc = iwctl_giwfreq(dev, NULL, &(wrq->u.freq), NULL);
		break;

		// Set desired network name (ESSID)
	case SIOCSIWESSID:

		{
			char essid[IW_ESSID_MAX_SIZE+1];
			if (wrq->u.essid.length > IW_ESSID_MAX_SIZE) {
				rc = -E2BIG;
				break;
			}
			if (copy_from_user(essid, wrq->u.essid.pointer,
					   wrq->u.essid.length)) {
				rc = -EFAULT;
				break;
			}
			rc = iwctl_siwessid(dev, NULL,
					    &(wrq->u.essid), essid);
		}
		break;


		// Get current network name (ESSID)
	case SIOCGIWESSID:

		{
			char essid[IW_ESSID_MAX_SIZE+1];
			if (wrq->u.essid.pointer) {
				iwctl_giwessid(dev, NULL,
					    &(wrq->u.essid), essid);
				if (copy_to_user(wrq->u.essid.pointer,
						         essid,
						         wrq->u.essid.length) )
					rc = -EFAULT;
			}
		}
		break;

	case SIOCSIWAP:

		rc = iwctl_siwap(dev, NULL, &(wrq->u.ap_addr), NULL);
		break;


		// Get current Access Point (BSSID)
	case SIOCGIWAP:
		rc = iwctl_giwap(dev, NULL, &(wrq->u.ap_addr), NULL);
		break;


		// Set desired station name
	case SIOCSIWNICKN:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWNICKN \n");
        rc = -EOPNOTSUPP;
		break;

		// Get current station name
	case SIOCGIWNICKN:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWNICKN \n");
        rc = -EOPNOTSUPP;
		break;

		// Set the desired bit-rate
	case SIOCSIWRATE:
		rc = iwctl_siwrate(dev, NULL, &(wrq->u.bitrate), NULL);
		break;

	// Get the current bit-rate
	case SIOCGIWRATE:
		iwctl_giwrate(dev, NULL, &(wrq->u.bitrate), NULL);
		break;

	// Set the desired RTS threshold
	case SIOCSIWRTS:

		rc = iwctl_siwrts(dev, &(wrq->u.rts));
		break;

	// Get the current RTS threshold
	case SIOCGIWRTS:

		rc = iwctl_giwrts(dev, NULL, &(wrq->u.rts), NULL);
		break;

		// Set the desired fragmentation threshold
	case SIOCSIWFRAG:

		rc = iwctl_siwfrag(dev, NULL, &(wrq->u.frag), NULL);
	    break;

	// Get the current fragmentation threshold
	case SIOCGIWFRAG:

		rc = iwctl_giwfrag(dev, NULL, &(wrq->u.frag), NULL);
		break;

		// Set mode of operation
	case SIOCSIWMODE:
    	rc = iwctl_siwmode(dev, NULL, &(wrq->u.mode), NULL);
		break;

		// Get mode of operation
	case SIOCGIWMODE:
		iwctl_giwmode(dev, NULL, &(wrq->u.mode), NULL);
		break;

		// Set WEP keys and mode
	case SIOCSIWENCODE:
		{
            char abyKey[WLAN_WEP232_KEYLEN];

			if (wrq->u.encoding.pointer) {


				if (wrq->u.encoding.length > WLAN_WEP232_KEYLEN) {
					rc = -E2BIG;
					break;
				}
				memset(abyKey, 0, WLAN_WEP232_KEYLEN);
				if (copy_from_user(abyKey,
				                  wrq->u.encoding.pointer,
				                  wrq->u.encoding.length)) {
					rc = -EFAULT;
					break;
				}
			} else if (wrq->u.encoding.length != 0) {
				rc = -EINVAL;
				break;
			}
			rc = iwctl_siwencode(dev, NULL, &(wrq->u.encoding), abyKey);
		}
		break;

		// Get the WEP keys and mode
	case SIOCGIWENCODE:

		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}
		{
		    char abyKey[WLAN_WEP232_KEYLEN];

		    rc = iwctl_giwencode(dev, NULL, &(wrq->u.encoding), abyKey);
		    if (rc != 0) break;
			if (wrq->u.encoding.pointer) {
				if (copy_to_user(wrq->u.encoding.pointer,
						        abyKey,
						        wrq->u.encoding.length))
					rc = -EFAULT;
			}
		}
		break;

		// Get the current Tx-Power
	case SIOCGIWTXPOW:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWTXPOW \n");
        rc = -EOPNOTSUPP;
		break;

	case SIOCSIWTXPOW:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWTXPOW \n");
        rc = -EOPNOTSUPP;
		break;

	case SIOCSIWRETRY:

		rc = iwctl_siwretry(dev, NULL, &(wrq->u.retry), NULL);
		break;

	case SIOCGIWRETRY:

		rc = iwctl_giwretry(dev, NULL, &(wrq->u.retry), NULL);
		break;

		// Get range of parameters
	case SIOCGIWRANGE:

		{
			struct iw_range range;

			iwctl_giwrange(dev, NULL, &(wrq->u.data), (char *) &range);
			if (copy_to_user(wrq->u.data.pointer, &range, sizeof(struct iw_range)))
				rc = -EFAULT;
		}

		break;

	case SIOCGIWPOWER:

		rc = iwctl_giwpower(dev, NULL, &(wrq->u.power), NULL);
		break;


	case SIOCSIWPOWER:

		rc = iwctl_siwpower(dev, NULL, &(wrq->u.power), NULL);
		break;


	case SIOCGIWSENS:

	    rc = iwctl_giwsens(dev, NULL, &(wrq->u.sens), NULL);
		break;

	case SIOCSIWSENS:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWSENS \n");
		rc = -EOPNOTSUPP;
		break;

	case SIOCGIWAPLIST:
		if (wrq->u.data.pointer) {
			buffer = kzalloc((sizeof(struct sockaddr) +
				sizeof(struct iw_quality)) * IW_MAX_AP,
					GFP_KERNEL);
			if (buffer == NULL) {
				rc = -ENOMEM;
				break;
			}

			rc = iwctl_giwaplist(dev, NULL, &(wrq->u.data), buffer);
			if (rc < 0) {
				kfree(buffer);
				break;
			}

			if (copy_to_user(wrq->u.data.pointer, buffer,
				wrq->u.data.length * (sizeof(struct sockaddr)
					+ sizeof(struct iw_quality))))
				rc = -EFAULT;

			kfree(buffer);
		}
		break;


#ifdef WIRELESS_SPY
		// Set the spy list
	case SIOCSIWSPY:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWSPY \n");
		rc = -EOPNOTSUPP;
		break;

		// Get the spy list
	case SIOCGIWSPY:

        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWSPY \n");
		rc = -EOPNOTSUPP;
		break;

#endif // WIRELESS_SPY

	case SIOCGIWPRIV:
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWPRIV \n");
		rc = -EOPNOTSUPP;
/*
		if(wrq->u.data.pointer) {
			wrq->u.data.length = sizeof(iwctl_private_args) / sizeof( iwctl_private_args[0]);

			if(copy_to_user(wrq->u.data.pointer,
					(u_char *) iwctl_private_args,
					sizeof(iwctl_private_args)))
				rc = -EFAULT;
		}
*/
		break;

	case SIOCSIWAUTH:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWAUTH\n");
		rc = iwctl_siwauth(dev, NULL, &(wrq->u.param), NULL);
		break;

	case SIOCGIWAUTH:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWAUTH \n");
		rc = iwctl_giwauth(dev, NULL, &(wrq->u.param), NULL);
		break;

	case SIOCSIWGENIE:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWGENIE \n");
		rc = iwctl_siwgenie(dev, NULL, &(wrq->u.data), wrq->u.data.pointer);
		break;

	case SIOCGIWGENIE:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWGENIE \n");
		rc = iwctl_giwgenie(dev, NULL, &(wrq->u.data), wrq->u.data.pointer);
		break;

	case SIOCSIWENCODEEXT:
		{
			char extra[sizeof(struct iw_encode_ext)+MAX_KEY_LEN+1];
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWENCODEEXT \n");
			if(wrq->u.encoding.pointer){
				memset(extra, 0, sizeof(struct iw_encode_ext)+MAX_KEY_LEN+1);
				if(wrq->u.encoding.length > (sizeof(struct iw_encode_ext)+ MAX_KEY_LEN)){
					rc = -E2BIG;
					break;
				}
				if(copy_from_user(extra, wrq->u.encoding.pointer,wrq->u.encoding.length)){
					rc = -EFAULT;
					break;
				}
			}else if(wrq->u.encoding.length != 0){
				rc = -EINVAL;
				break;
			}
			rc = iwctl_siwencodeext(dev, NULL, &(wrq->u.encoding), extra);
		}
		break;

	case SIOCGIWENCODEEXT:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCGIWENCODEEXT \n");
		rc = iwctl_giwencodeext(dev, NULL, &(wrq->u.encoding), NULL);
		break;

	case SIOCSIWMLME:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO " SIOCSIWMLME \n");
		rc = iwctl_siwmlme(dev, NULL, &(wrq->u.data), wrq->u.data.pointer);
		break;


    case IOCTL_CMD_TEST:

		if (!(pDevice->flags & DEVICE_FLAGS_OPENED)) {
		    rc = -EFAULT;
		    break;
		} else {
		    rc = 0;
		}
        pReq = (PSCmdRequest)rq;

   //20080130-01,<Remark> by Mike Liu
      // if(pDevice->bLinkPass==TRUE)
          pReq->wResult = MAGIC_CODE;         //Linking status:0x3142
   //20080130-02,<Remark> by Mike Liu
      //  else
      //	 pReq->wResult = MAGIC_CODE+1;    //disconnect status:0x3143
        break;

    case IOCTL_CMD_SET:
		if (!(pDevice->flags & DEVICE_FLAGS_OPENED) &&
		       (((PSCmdRequest)rq)->wCmdCode !=WLAN_CMD_SET_WPA))
		{
		    rc = -EFAULT;
		    break;
		} else {
		    rc = 0;
		}

	    if (test_and_set_bit( 0, (void*)&(pMgmt->uCmdBusy))) {
		    return -EBUSY;
	    }
        rc = private_ioctl(pDevice, rq);
        clear_bit( 0, (void*)&(pMgmt->uCmdBusy));
        break;

    case IOCTL_CMD_HOSTAPD:

		if (!(pDevice->flags & DEVICE_FLAGS_OPENED)) {
		    rc = -EFAULT;
		    break;
		} else {
		    rc = 0;
		}

		rc = vt6656_hostap_ioctl(pDevice, &wrq->u.data);
        break;

    case IOCTL_CMD_WPA:

		if (!(pDevice->flags & DEVICE_FLAGS_OPENED)) {
		    rc = -EFAULT;
		    break;
		} else {
		    rc = 0;
		}

		rc = wpa_ioctl(pDevice, &wrq->u.data);
        break;

	case SIOCETHTOOL:
        return ethtool_ioctl(dev, (void *) rq->ifr_data);
	// All other calls are currently unsupported

	default:
		rc = -EOPNOTSUPP;
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Ioctl command not supported..%x\n", cmd);


    }

    if (pDevice->bCommit) {
       if (pMgmt->eConfigMode == WMAC_CONFIG_AP) {
           netif_stop_queue(pDevice->dev);
           spin_lock_irq(&pDevice->lock);
	bScheduleCommand((void *) pDevice, WLAN_CMD_RUN_AP, NULL);
           spin_unlock_irq(&pDevice->lock);
       }
       else {
           DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO "Commit the settings\n");
           spin_lock_irq(&pDevice->lock);
//2007-1121-01<Modify>by EinsnLiu
	    if (pDevice->bLinkPass &&
		  memcmp(pMgmt->abyCurrSSID,pMgmt->abyDesireSSID,WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN)) {
		bScheduleCommand((void *) pDevice, WLAN_CMD_DISASSOCIATE, NULL);
	     } else {
           pDevice->bLinkPass = FALSE;
	   pMgmt->eCurrState = WMAC_STATE_IDLE;
	   memset(pMgmt->abyCurrBSSID, 0, 6);
		 }
           ControlvMaskByte(pDevice,MESSAGE_REQUEST_MACREG,MAC_REG_PAPEDELAY,LEDSTS_STS,LEDSTS_SLOW);
//End Modify
           netif_stop_queue(pDevice->dev);
           pMgmt->eScanType = WMAC_SCAN_ACTIVE;
	   if (!pDevice->bWPASuppWextEnabled)
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_BSSID_SCAN,
				 pMgmt->abyDesireSSID);
		bScheduleCommand((void *) pDevice,
				 WLAN_CMD_SSID,
				 NULL);
           spin_unlock_irq(&pDevice->lock);
      }
      pDevice->bCommit = FALSE;
    }


    return rc;
}


static int ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	u32 ethcmd;

	if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

        switch (ethcmd) {
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};
		strncpy(info.driver, DEVICE_NAME, sizeof(info.driver)-1);
		strncpy(info.version, DEVICE_VERSION, sizeof(info.version)-1);
		if (copy_to_user(useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}

        }

	return -EOPNOTSUPP;
}


/*------------------------------------------------------------------*/

MODULE_DEVICE_TABLE(usb, vt6656_table);

static struct usb_driver vt6656_driver = {
	.name =		DEVICE_NAME,
	.probe =	vt6656_probe,
	.disconnect =	vt6656_disconnect,
	.id_table =	vt6656_table,
#ifdef CONFIG_PM
	.suspend = vt6656_suspend,
	.resume = vt6656_resume,
#endif /* CONFIG_PM */
};

module_usb_driver(vt6656_driver);
