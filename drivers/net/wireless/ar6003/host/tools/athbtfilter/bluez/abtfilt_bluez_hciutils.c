//------------------------------------------------------------------------------
// <copyright file="abtfilt_bluez_hciutils.c" company="Atheros">
//    Copyright (c) 2011 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================

#include "abtfilt_bluez_dbus.h"

#undef HCI_INQUIRY
#include <bluetooth.h>
#include <hci.h>
#include <hci_lib.h>

#include "hciutils.h"
#include <dlfcn.h>

extern ABF_BT_INFO    * g_pAbfBtInfo;

#define ForgetRemoteAudioDevice(pA)     \
{                                       \
    A_MEMZERO((pA)->DefaultRemoteAudioDeviceAddress,sizeof((pA)->DefaultRemoteAudioDeviceAddress)); \
    (pA)->DefaultRemoteAudioDevicePropsValid = FALSE;                                               \
}

#if !defined(STATIC_LINK_HCILIBS)
static void *g_hciHandle;
#endif

static tHCIUTILS_STATUS  (* pfn_HCIUTILS_RegisterHCINotification) ( tHCIUTILS_NOTIFICATION_TYPE t_type, int  nOpCode, tHCIUTILS_EVENT_CALLBACK  eventNotificationCallback ,void * p_appdata ) = NULL;
static void ( *pfn_HCIUTILS_SendCmd) (tHCIUTILS_HCI_CMD   tCmd, void *  p_hci_cmd_params) = NULL;
static void (*pfn_HCIUTILS_UnRegisterHCINotification)(tHCIUTILS_NOTIFICATION_TYPE t_type,int nOpCode) = NULL;

static A_UINT32 hciEventList[] =
{
    HCI_EVT_REMOTE_DEV_LMP_VERSION, /* 0xb*/
    HCI_EVT_REMOTE_DEV_VERSION, /* 0xc */
    EVT_INQUIRY_COMPLETE,
    EVT_PIN_CODE_REQ,
    EVT_LINK_KEY_NOTIFY,
    HCI_EVT_ROLE_CHANGE, /* 0x12*/
    EVT_CONN_COMPLETE,
    HCI_EVT_SCO_CONNECT_COMPLETE,
    HCI_EVT_DISCONNECT,
};

static A_UINT32 hciCmdList[] =
{
    HCI_CMD_OPCODE_INQUIRY_START,
    HCI_CMD_OPCODE_INQUIRY_CANCEL,
    HCI_CMD_OPCODE_CONNECT,
} ;

static void eventNotificationCallback ( tHCIUTILS_NOTIFICATION * pEvent)
{
    ABF_BT_INFO     *pAbfBtInfo = (ABF_BT_INFO *)g_pAbfBtInfo;
    ATHBT_FILTER_INFO *pInfo = pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    if(pEvent->tType == HCIUTILS_COMMAND) {
        if(pEvent->nOpCode == HCI_CMD_OPCODE_INQUIRY_START) {
 	        A_DEBUG("Device Inquiry Started \n");
            pAbfBtInfo->btInquiryState |= (1 << 0);
            AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
        }
        if(pEvent->nOpCode == HCI_CMD_OPCODE_INQUIRY_CANCEL  ) {
 	        A_DEBUG("Device Inquiry cancelled \n");
            if(pAbfBtInfo->btInquiryState) {
	            pAbfBtInfo->btInquiryState &= ~(1 << 0);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
	        }
 	    }
        if(pEvent->nOpCode == HCI_CMD_OPCODE_CONNECT) {
            A_DEBUG("Bt-Connect\n");
        }
    }
	if(pEvent->tType == HCIUTILS_EVENT) {

#define LMP_FEATURE_ACL_EDR_2MBPS_BYTE_INDEX  3
#define LMP_FEATURE_ACL_EDR_2MBPS_BIT_MASK    0x2
#define LMP_FEATURE_ACL_EDR_3MBPS_BYTE_INDEX  3
#define LMP_FEATURE_ACL_EDR_3MBPS_BIT_MASK    0x4
#define LMP_FEATURES_LENGTH                   8
	    if(pEvent->nOpCode == HCI_EVT_REMOTE_DEV_LMP_VERSION) {
	        A_UINT32 i = 0;
            A_UINT32 len = pEvent->n_data_length;
	        A_UCHAR * eventPtr = (A_UCHAR *)pEvent->p_notification_data_buf;
            A_UINT8 *lmp_features;

          /* Process LMP Features */


            A_DUMP_BUFFER(eventPtr, len,"Remote Device LMP Features:");

            eventPtr += 1;
            len -= 1 ;
            lmp_features = &eventPtr[3];
            A_DUMP_BUFFER(lmp_features, sizeof(lmp_features),"Remote Device LMP Features:");

            if ((lmp_features[LMP_FEATURE_ACL_EDR_2MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_2MBPS_BIT_MASK)  ||    
                (lmp_features[LMP_FEATURE_ACL_EDR_3MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_3MBPS_BIT_MASK)) 
            {
                A_DEBUG("Device is EDR capable \n");
                pAbfBtInfo->DefaultAudioDeviceLmpVersion = 3;
            } else {
                A_DEBUG("Device is NOT EDR capable \n");
                pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
            }
            pAbfBtInfo->DefaultRemoteAudioDevicePropsValid = TRUE;
            pInfo->A2DPConnection_LMPVersion =  pInfo->SCOConnection_LMPVersion = 
                                                pAbfBtInfo->DefaultAudioDeviceLmpVersion;
        }
        if(pEvent->nOpCode ==  HCI_EVT_REMOTE_DEV_VERSION) {
	        A_UCHAR * eventPtr = (A_UCHAR *)pEvent->p_notification_data_buf;
            A_UINT32 len = pEvent->n_data_length;

            A_DUMP_BUFFER(eventPtr, len,"Remote Device Version");
            eventPtr += 1;
            len -= 1;
            A_DUMP_BUFFER(eventPtr, len,"Remote Device Version");

	        if (eventPtr[3] == 0) {
                strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.0");
                pAbfBtInfo->DefaultAudioDeviceLmpVersion = 0;
		        A_DEBUG("Its 1.0 \n");
	        } else if (eventPtr[3] == 1) {
	            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.1");
                pAbfBtInfo->DefaultAudioDeviceLmpVersion = 1;
		        A_DEBUG("Its 1.1 \n");
        	} else if (eventPtr[3] == 2) {
	            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.2");
                pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
    		    A_DEBUG("Its 1.2 \n");
        	} else if (eventPtr[3] == 3) {
	            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "2.0");
/*                    pAbfBtInfo->DefaultAudioDeviceLmpVersion = 3; */
	    	    A_DEBUG("Its 2.0 \n");
        	}else {
	            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "2.1");
/*                    pAbfBtInfo->DefaultAudioDeviceLmpVersion = 4; */
		        A_DEBUG("Its 2.1 \n");
            }
           
            pInfo->A2DPConnection_LMPVersion =  pInfo->SCOConnection_LMPVersion = 
                                                pAbfBtInfo->DefaultAudioDeviceLmpVersion;
	    }
        if (pEvent->nOpCode == EVT_INQUIRY_COMPLETE) {
            A_DEBUG("Device Inquiry Completed\n");
            if(pAbfBtInfo->btInquiryState) {
            	pAbfBtInfo->btInquiryState &= ~(1 << 0);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
            }
        }
        if (pEvent->nOpCode == EVT_PIN_CODE_REQ) {
            A_DEBUG("Pin Code Request\n");
        }

        if (pEvent->nOpCode == EVT_LINK_KEY_NOTIFY) {
           A_DEBUG("link key notify\n");
        }
	    if(pEvent->nOpCode == HCI_EVT_ROLE_CHANGE) {
	        A_DEBUG("Role Change\n");
    	    A_UCHAR * eventPtr = (A_UCHAR *)pEvent->p_notification_data_buf;
            A_UINT32 len = pEvent->n_data_length;

            A_DUMP_BUFFER(eventPtr, len,"Remote Device Role ");
            eventPtr += 8;
            len -= 8;
	        if(*eventPtr == 0x00) {
	            A_DEBUG("ROLE IS MASTER \n");
                pAbfBtInfo->pInfo->A2DPConnection_Role = 0x0;
            }
    	    if(*eventPtr == 0x01) {
	            A_DEBUG("ROLE IS SLAVE \n");
                pAbfBtInfo->pInfo->A2DPConnection_Role = 0x1;
            }
        }
        if(pEvent->nOpCode == EVT_CONN_COMPLETE) {
            A_DEBUG("Conn complete\n");
            if(pAbfBtInfo->btInquiryState) {
            	pAbfBtInfo->btInquiryState &= ~(1 << 1);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
            }
        }

        if(pEvent->nOpCode == HCI_EVT_SCO_CONNECT_COMPLETE) {
            A_UINT32 len = pEvent->n_data_length;
	        A_UCHAR * eventPtr = (A_UCHAR*)pEvent->p_notification_data_buf;

            A_DUMP_BUFFER(eventPtr, len,"SCO CONNECT_COMPLETE");
	        A_DEBUG("SCO CONNECT COMPLETE \n");
            pInfo->SCOConnection_LMPVersion =
                               pAbfBtInfo->DefaultAudioDeviceLmpVersion;

            pInfo->SCOConnectInfo.LinkType = eventPtr[10];
            pInfo->SCOConnectInfo.TransmissionInterval = eventPtr[11];
            pInfo->SCOConnectInfo.RetransmissionInterval = eventPtr[12];
            pInfo->SCOConnectInfo.RxPacketLength = eventPtr[13];
            pInfo->SCOConnectInfo.TxPacketLength = eventPtr[15];
            pInfo->SCOConnectInfo.Valid = TRUE;

            A_INFO("HCI SYNC_CONN_COMPLETE event captured, conn info (%d, %d, %d, %d, %d) \n",
                pInfo->SCOConnectInfo.LinkType,
                pInfo->SCOConnectInfo.TransmissionInterval,
                pInfo->SCOConnectInfo.RetransmissionInterval,
                pInfo->SCOConnectInfo.RxPacketLength,
                pInfo->SCOConnectInfo.TxPacketLength);

            AthBtIndicateState(pInstance,
                	      pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO? ATH_BT_ESCO: ATH_BT_SCO,
                              STATE_ON);
        }
        if(pEvent->nOpCode == HCI_EVT_DISCONNECT) {
           A_UINT32 bitmap = FCore_GetCurrentBTStateBitMap(&pInfo->FilterCore);

	        A_DEBUG("HCI_EVT_DISCONNECT event \n");

            if( (bitmap & (1 << ATH_BT_SCO))|| (bitmap & (1 << ATH_BT_ESCO))) {
                AthBtIndicateState(pInstance,
                	      pInfo->SCOConnectInfo.LinkType == BT_LINK_TYPE_ESCO? ATH_BT_ESCO: ATH_BT_SCO,
                              STATE_OFF);
            }
            ForgetRemoteAudioDevice(pAbfBtInfo);
            pInfo->SCOConnectInfo.Valid = FALSE;
            pAbfBtInfo->pInfo->A2DPConnection_Role = 0x0;
        }
   }
}

void
Abf_RegisterToHciLib( ABF_BT_INFO * pAbfBtInfo)
{
    void *                                      handle = 0;
    tHCIUTILS_STATUS                            ret;

    char * pparam = NULL;
    int n_opcode = 0, n_type = 0;
    int i;
    A_INFO("Register To HCI LIB \n");

    if (pfn_HCIUTILS_RegisterHCINotification) {
        for (i=0; i <= 2;i++) {
            ret = (*pfn_HCIUTILS_RegisterHCINotification)
                  (
                  HCIUTILS_COMMAND,
                  hciCmdList[i],
                  eventNotificationCallback,
                  (void *) pAbfBtInfo
                  );
            A_DEBUG("Registered for HCI cmd %x, ret= %d\n", hciCmdList[i],ret);
        }
        for (i=0; i <= 8; i++) {
            ret = (*pfn_HCIUTILS_RegisterHCINotification)
                  (
                  HCIUTILS_EVENT,
                  hciEventList[i],
                  eventNotificationCallback,
                  (void *)pAbfBtInfo
                  );
            A_DEBUG("Hcievent List[%d] =%x, ret =%x\n",i, hciEventList[i], ret);
        }
    }
}

void
Abf_UnRegisterToHciLib( ABF_BT_INFO * pAbfBtInfo)
{
    A_DEBUG("Unregistering HCI library handler\n");
    if(pfn_HCIUTILS_UnRegisterHCINotification) {
        int i;
        for (i=0; i <= 2;i++) {
            (*pfn_HCIUTILS_UnRegisterHCINotification)
                  (
                  HCIUTILS_COMMAND,
                  hciCmdList[i]
                  );
            A_DEBUG("Unregistered for HCI cmd %x\n", hciCmdList[i]);
        }
        for (i=0; i <= 8; i++) {
            (*pfn_HCIUTILS_UnRegisterHCINotification)
                  (
                  HCIUTILS_EVENT,
                  hciEventList[i]
                  );
            A_DEBUG("Unregistered Hcievent List[%d] =%x\n",i, hciEventList[i]);
        }
    }
}

A_STATUS  Abf_IssueAFHViaHciLib (ABF_BT_INFO  * pAbfBtInfo,
                                int CurrentWLANChannel)
{
    A_UINT32 center;
    tHCIUTILS_HCICMD_SET_AFH_CHANNELS setChannels;

    A_INFO("WLAN Operating Channel: %d \n", CurrentWLANChannel);

    if(!CurrentWLANChannel) {
        setChannels.first = 79;
        setChannels.last = 79;
        center = 0;
   }else {
        if( (CurrentWLANChannel < 2412) || 
           (CurrentWLANChannel >  2470))
        {
            return A_ERROR;
        }
        center = CurrentWLANChannel;
        center = center - 2400;
        setChannels.first = center - 10;
        setChannels.last = center + 10;
  }

    if (pfn_HCIUTILS_SendCmd) {
        (*pfn_HCIUTILS_SendCmd) (HCIUTILS_SET_AFH_CHANNELS, &setChannels);
        A_DEBUG("Issue AFH first =%x, last = %x, center =%x\n",
                setChannels.first, setChannels.last, center);
    } else {
        A_ERR( "%s : Fail to issue AFH due to NULL pointer of pfn_HCIUTILS_SendCmd\n", __FUNCTION__);
        return A_ERROR;
    }

   return A_OK;
}

A_STATUS Abf_HciLibInit(A_UINT32 *btfiltFlags)
{        
#ifdef STATIC_LINK_HCILIBS
    pfn_HCIUTILS_RegisterHCINotification = HCIUTILS_RegisterHCINotification;
    pfn_HCIUTILS_SendCmd = HCIUTILS_SendCmd;
    pfn_HCIUTILS_UnRegisterHCINotification = HCIUTILS_UnRegisterHCINotification;
    /* We don't force to set ONLY DBUS flags here since we are static linking */
    return A_OK;
#else
    g_hciHandle = dlopen("hciutils.so", RTLD_NOW);
    if( g_hciHandle == NULL){
        A_ERR( "%s : Error loading library hciutils.so %s\n", __FUNCTION__, dlerror());
        return A_ERROR;
    } else {
        A_DEBUG( "Load hciutils.so successfully\n");
        pfn_HCIUTILS_RegisterHCINotification = dlsym(g_hciHandle, "HCIUTILS_RegisterHCINotification");
        pfn_HCIUTILS_SendCmd = dlsym(g_hciHandle, "HCIUTILS_SendCmd");
        pfn_HCIUTILS_UnRegisterHCINotification = dlsym(g_hciHandle, "HCIUTILS_UnRegisterHCINotification");
        if ( (NULL == pfn_HCIUTILS_RegisterHCINotification) || (NULL == pfn_HCIUTILS_SendCmd) || 
              (NULL == pfn_HCIUTILS_UnRegisterHCINotification) )
        {
		    A_ERR("ERROR GETTING HCIUTILS SYMBOLS \n");
            dlclose(g_hciHandle);
            g_hciHandle = NULL;
            return A_ERROR;
        }
        /* Make sure we enable ONLY DBUS flags */
        *btfiltFlags |= ABF_USE_ONLY_DBUS_FILTERING;
        return A_OK;
    }
#endif
}

void Abf_HciLibDeInit(void)
{
#ifndef STATIC_LINK_HCILIBS
    if(g_hciHandle) {
        dlclose(g_hciHandle);
        g_hciHandle = NULL;
    }
#endif
}

