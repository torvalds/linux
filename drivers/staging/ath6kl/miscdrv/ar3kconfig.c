//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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
// AR3K configuration implementation
//
// Author(s): ="Atheros"
//==============================================================================

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#define ATH_MODULE_NAME misc
#include "a_debug.h"
#include "common_drv.h"
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
#include "export_hci_transport.h"
#else
#include "hci_transport_api.h"
#endif
#include "ar3kconfig.h"
#include "tlpm.h"

#define BAUD_CHANGE_COMMAND_STATUS_OFFSET   5
#define HCI_EVENT_RESP_TIMEOUTMS            3000
#define HCI_CMD_OPCODE_BYTE_LOW_OFFSET      0
#define HCI_CMD_OPCODE_BYTE_HI_OFFSET       1
#define HCI_EVENT_OPCODE_BYTE_LOW           3
#define HCI_EVENT_OPCODE_BYTE_HI            4
#define HCI_CMD_COMPLETE_EVENT_CODE         0xE
#define HCI_MAX_EVT_RECV_LENGTH             257
#define EXIT_MIN_BOOT_COMMAND_STATUS_OFFSET  5

A_STATUS AthPSInitialize(AR3K_CONFIG_INFO *hdev);

static A_STATUS SendHCICommand(AR3K_CONFIG_INFO *pConfig,
                               A_UINT8          *pBuffer,
                               int              Length)
{
    HTC_PACKET  *pPacket = NULL;
    A_STATUS    status = A_OK;
       
    do {   
        
        pPacket = (HTC_PACKET *)A_MALLOC(sizeof(HTC_PACKET));     
        if (NULL == pPacket) {
            status = A_NO_MEMORY;
            break;    
        }       
        
        A_MEMZERO(pPacket,sizeof(HTC_PACKET));      
        SET_HTC_PACKET_INFO_TX(pPacket,
                               NULL,
                               pBuffer, 
                               Length,
                               HCI_COMMAND_TYPE, 
                               AR6K_CONTROL_PKT_TAG);
        
            /* issue synchronously */                                      
        status = HCI_TransportSendPkt(pConfig->pHCIDev,pPacket,TRUE);   
        
    } while (FALSE);
   
    if (pPacket != NULL) {
        A_FREE(pPacket);
    }
        
    return status;
}

static A_STATUS RecvHCIEvent(AR3K_CONFIG_INFO *pConfig,
                             A_UINT8          *pBuffer,
                             int              *pLength)
{
    A_STATUS    status = A_OK; 
    HTC_PACKET  *pRecvPacket = NULL;
    
    do {
                 
        pRecvPacket = (HTC_PACKET *)A_MALLOC(sizeof(HTC_PACKET));
        if (NULL == pRecvPacket) {
            status = A_NO_MEMORY;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to alloc HTC struct \n"));
            break;    
        }     
        
        A_MEMZERO(pRecvPacket,sizeof(HTC_PACKET)); 
         
        SET_HTC_PACKET_INFO_RX_REFILL(pRecvPacket,NULL,pBuffer,*pLength,HCI_EVENT_TYPE);
        
        status = HCI_TransportRecvHCIEventSync(pConfig->pHCIDev,
                                               pRecvPacket,
                                               HCI_EVENT_RESP_TIMEOUTMS);
        if (A_FAILED(status)) {
            break;    
        }

        *pLength = pRecvPacket->ActualLength;
        
    } while (FALSE);
       
    if (pRecvPacket != NULL) {
        A_FREE(pRecvPacket);    
    }
    
    return status;
} 
    
A_STATUS SendHCICommandWaitCommandComplete(AR3K_CONFIG_INFO *pConfig,
                                           A_UINT8          *pHCICommand,
                                           int              CmdLength,
                                           A_UINT8          **ppEventBuffer,
                                           A_UINT8          **ppBufferToFree)
{
    A_STATUS    status = A_OK;   
    A_UINT8     *pBuffer = NULL;
    A_UINT8     *pTemp;
    int         length;
    A_BOOL      commandComplete = FALSE;
    A_UINT8     opCodeBytes[2];
                               
    do {
        
        length = max(HCI_MAX_EVT_RECV_LENGTH,CmdLength);
        length += pConfig->pHCIProps->HeadRoom + pConfig->pHCIProps->TailRoom;
        length += pConfig->pHCIProps->IOBlockPad;
                                     
        pBuffer = (A_UINT8 *)A_MALLOC(length);        
        if (NULL == pBuffer) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: Failed to allocate bt buffer \n"));
            status = A_NO_MEMORY;
            break;    
        }
        
            /* get the opcodes to check the command complete event */
        opCodeBytes[0] = pHCICommand[HCI_CMD_OPCODE_BYTE_LOW_OFFSET];
        opCodeBytes[1] = pHCICommand[HCI_CMD_OPCODE_BYTE_HI_OFFSET];
        
            /* copy HCI command */
        A_MEMCPY(pBuffer + pConfig->pHCIProps->HeadRoom,pHCICommand,CmdLength);         
            /* send command */
        status = SendHCICommand(pConfig,
                                pBuffer + pConfig->pHCIProps->HeadRoom,
                                CmdLength);
        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: Failed to send HCI Command (%d) \n", status));
            AR_DEBUG_PRINTBUF(pHCICommand,CmdLength,"HCI Bridge Failed HCI Command");
            break;    
        }   
        
            /* reuse buffer to capture command complete event */
        A_MEMZERO(pBuffer,length);
        status = RecvHCIEvent(pConfig,pBuffer,&length);        
        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: HCI event recv failed \n"));
            AR_DEBUG_PRINTBUF(pHCICommand,CmdLength,"HCI Bridge Failed HCI Command");
            break;    
        }
        
        pTemp = pBuffer + pConfig->pHCIProps->HeadRoom;        
        if (pTemp[0] == HCI_CMD_COMPLETE_EVENT_CODE) {
            if ((pTemp[HCI_EVENT_OPCODE_BYTE_LOW] == opCodeBytes[0]) &&
                (pTemp[HCI_EVENT_OPCODE_BYTE_HI] == opCodeBytes[1])) {
                commandComplete = TRUE;    
            }
        }
        
        if (!commandComplete) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: Unexpected HCI event : %d \n",pTemp[0]));
            AR_DEBUG_PRINTBUF(pTemp,pTemp[1],"Unexpected HCI event");
            status = A_ECOMM;
            break;    
        }       
        
        if (ppEventBuffer != NULL) {
                /* caller wants to look at the event */
            *ppEventBuffer = pTemp;
            if (ppBufferToFree == NULL) {
                status = A_EINVAL;
                break;        
            }
                /* caller must free the buffer */
            *ppBufferToFree = pBuffer;
            pBuffer = NULL;            
        }
        
    } while (FALSE);

    if (pBuffer != NULL) {
        A_FREE(pBuffer);    
    }
    
    return status;    
}

static A_STATUS AR3KConfigureHCIBaud(AR3K_CONFIG_INFO *pConfig)
{
    A_STATUS    status = A_OK;
    A_UINT8     hciBaudChangeCommand[] =  {0x0c,0xfc,0x2,0,0};
    A_UINT16    baudVal; 
    A_UINT8     *pEvent = NULL;
    A_UINT8     *pBufferToFree = NULL;
    
    do {
        
        if (pConfig->Flags & AR3K_CONFIG_FLAG_SET_AR3K_BAUD) {
            baudVal = (A_UINT16)(pConfig->AR3KBaudRate / 100);
            hciBaudChangeCommand[3] = (A_UINT8)baudVal;
            hciBaudChangeCommand[4] = (A_UINT8)(baudVal >> 8);
            
            status = SendHCICommandWaitCommandComplete(pConfig,
                                                       hciBaudChangeCommand,
                                                       sizeof(hciBaudChangeCommand),
                                                       &pEvent,
                                                       &pBufferToFree);          
            if (A_FAILED(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: Baud rate change failed! \n"));  
                break;    
            }
            
            if (pEvent[BAUD_CHANGE_COMMAND_STATUS_OFFSET] != 0) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("AR3K Config: Baud change command event status failed: %d \n", 
                                pEvent[BAUD_CHANGE_COMMAND_STATUS_OFFSET]));
                status = A_ECOMM; 
                break;           
            } 
            
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                    ("AR3K Config: Baud Changed to %d \n",pConfig->AR3KBaudRate));  
        }
        
        if (pConfig->Flags & AR3K_CONFIG_FLAG_AR3K_BAUD_CHANGE_DELAY) {
                /* some versions of AR3K do not switch baud immediately, up to 300MS */
            A_MDELAY(325);
        }
        
        if (pConfig->Flags & AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP) {
            /* Tell target to change UART baud rate for AR6K */
            status = HCI_TransportSetBaudRate(pConfig->pHCIDev, pConfig->AR3KBaudRate);

            if (A_FAILED(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                    ("AR3K Config: failed to set scale and step values: %d \n", status));
                break;    
            }
    
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,
                    ("AR3K Config: Baud changed to %d for AR6K\n", pConfig->AR3KBaudRate));            
        }
                
    } while (FALSE);
                        
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
        
    return status;
}

static A_STATUS AR3KExitMinBoot(AR3K_CONFIG_INFO *pConfig)
{
    A_STATUS  status;
    A_CHAR    exitMinBootCmd[] = {0x25,0xFC,0x0c,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
                                  0x00,0x00,0x00,0x00,0x00};
    A_UINT8   *pEvent = NULL;
    A_UINT8   *pBufferToFree = NULL;
    
    status = SendHCICommandWaitCommandComplete(pConfig,
                                               exitMinBootCmd,
                                               sizeof(exitMinBootCmd),
                                               &pEvent,
                                               &pBufferToFree);
    
    if (A_SUCCESS(status)) {
        if (pEvent[EXIT_MIN_BOOT_COMMAND_STATUS_OFFSET] != 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                ("AR3K Config: MinBoot exit command event status failed: %d \n", 
                            pEvent[EXIT_MIN_BOOT_COMMAND_STATUS_OFFSET]));
            status = A_ECOMM;            
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, 
                                ("AR3K Config: MinBoot Exit Command Complete (Success) \n"));
            A_MDELAY(1);
        }
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: MinBoot Exit Failed! \n"));    
    }
    
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    
    return status;                                              
}
                                 
static A_STATUS AR3KConfigureSendHCIReset(AR3K_CONFIG_INFO *pConfig)
{
    A_STATUS status = A_OK;
    A_UINT8 hciResetCommand[] = {0x03,0x0c,0x0};
    A_UINT8 *pEvent = NULL;
    A_UINT8 *pBufferToFree = NULL;

    status = SendHCICommandWaitCommandComplete( pConfig,
                                                hciResetCommand,
                                                sizeof(hciResetCommand),
                                                &pEvent,
                                                &pBufferToFree );

    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: HCI reset failed! \n"));
    }

    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);
    }

    return status;
}

static A_STATUS AR3KEnableTLPM(AR3K_CONFIG_INFO *pConfig)
{
    A_STATUS  status;
    /* AR3K vendor specific command for Host Wakeup Config */
    A_CHAR    hostWakeupConfig[] = {0x31,0xFC,0x18,
                                    0x02,0x00,0x00,0x00,
                                    0x01,0x00,0x00,0x00,
                                    TLPM_DEFAULT_IDLE_TIMEOUT_LSB,TLPM_DEFAULT_IDLE_TIMEOUT_MSB,0x00,0x00,    //idle timeout in ms
                                    0x00,0x00,0x00,0x00,
                                    TLPM_DEFAULT_WAKEUP_TIMEOUT_MS,0x00,0x00,0x00,    //wakeup timeout in ms
                                    0x00,0x00,0x00,0x00};
    /* AR3K vendor specific command for Target Wakeup Config */
    A_CHAR    targetWakeupConfig[] = {0x31,0xFC,0x18,
                                      0x04,0x00,0x00,0x00,
                                      0x01,0x00,0x00,0x00,
                                      TLPM_DEFAULT_IDLE_TIMEOUT_LSB,TLPM_DEFAULT_IDLE_TIMEOUT_MSB,0x00,0x00,  //idle timeout in ms
                                      0x00,0x00,0x00,0x00,
                                      TLPM_DEFAULT_WAKEUP_TIMEOUT_MS,0x00,0x00,0x00,  //wakeup timeout in ms
                                      0x00,0x00,0x00,0x00};
    /* AR3K vendor specific command for Host Wakeup Enable */
    A_CHAR    hostWakeupEnable[] = {0x31,0xFC,0x4,
                                    0x01,0x00,0x00,0x00};
    /* AR3K vendor specific command for Target Wakeup Enable */
    A_CHAR    targetWakeupEnable[] = {0x31,0xFC,0x4,
                                      0x06,0x00,0x00,0x00};
    /* AR3K vendor specific command for Sleep Enable */
    A_CHAR    sleepEnable[] = {0x4,0xFC,0x1,
                               0x1};
    A_UINT8   *pEvent = NULL;
    A_UINT8   *pBufferToFree = NULL;
    
    if (0 != pConfig->IdleTimeout) {
        A_UINT8 idle_lsb = pConfig->IdleTimeout & 0xFF;
        A_UINT8 idle_msb = (pConfig->IdleTimeout & 0xFF00) >> 8;
        hostWakeupConfig[11] = targetWakeupConfig[11] = idle_lsb;
        hostWakeupConfig[12] = targetWakeupConfig[12] = idle_msb;
    }

    if (0 != pConfig->WakeupTimeout) {
        hostWakeupConfig[19] = targetWakeupConfig[19] = (pConfig->WakeupTimeout & 0xFF);
    }

    status = SendHCICommandWaitCommandComplete(pConfig,
                                               hostWakeupConfig,
                                               sizeof(hostWakeupConfig),
                                               &pEvent,
                                               &pBufferToFree);
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("HostWakeup Config Failed! \n"));    
        return status;
    }
    
    pEvent = NULL;
    pBufferToFree = NULL;
    status = SendHCICommandWaitCommandComplete(pConfig,
                                               targetWakeupConfig,
                                               sizeof(targetWakeupConfig),
                                               &pEvent,
                                               &pBufferToFree);
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Target Wakeup Config Failed! \n"));    
        return status;
    }

    pEvent = NULL;
    pBufferToFree = NULL;
    status = SendHCICommandWaitCommandComplete(pConfig,
                                               hostWakeupEnable,
                                               sizeof(hostWakeupEnable),
                                               &pEvent,
                                               &pBufferToFree);
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("HostWakeup Enable Failed! \n"));    
        return status;
    }

    pEvent = NULL;
    pBufferToFree = NULL;
    status = SendHCICommandWaitCommandComplete(pConfig,
                                               targetWakeupEnable,
                                               sizeof(targetWakeupEnable),
                                               &pEvent,
                                               &pBufferToFree);
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Target Wakeup Enable Failed! \n"));    
        return status;
    }

    pEvent = NULL;
    pBufferToFree = NULL;
    status = SendHCICommandWaitCommandComplete(pConfig,
                                               sleepEnable,
                                               sizeof(sleepEnable),
                                               &pEvent,
                                               &pBufferToFree);
    if (pBufferToFree != NULL) {
        A_FREE(pBufferToFree);    
    }
    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Sleep Enable Failed! \n"));    
    }
    
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR3K Config: Enable TLPM Completed (status = %d) \n",status));

    return status;                                              
}

A_STATUS AR3KConfigure(AR3K_CONFIG_INFO *pConfig)
{
    A_STATUS        status = A_OK; 
        
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR3K Config: Configuring AR3K ...\n"));
                                
    do {
        
        if ((pConfig->pHCIDev == NULL) || (pConfig->pHCIProps == NULL) || (pConfig->pHIFDevice == NULL)) {
            status = A_EINVAL;
            break;    
        }
        
            /* disable asynchronous recv while we issue commands and receive events synchronously */
        status = HCI_TransportEnableDisableAsyncRecv(pConfig->pHCIDev,FALSE);
        if (A_FAILED(status)) {
            break;    
        }
      
        if (pConfig->Flags & AR3K_CONFIG_FLAG_FORCE_MINBOOT_EXIT) {
            status =  AR3KExitMinBoot(pConfig);   
            if (A_FAILED(status)) {
                break;    
            }    
        }
        
       
        /* Load patching and PST file if available*/
        if (A_OK != AthPSInitialize(pConfig)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Patch Download Failed!\n"));
        }

        /* Send HCI reset to make PS tags take effect*/
        AR3KConfigureSendHCIReset(pConfig);

 	if (pConfig->Flags & 
                (AR3K_CONFIG_FLAG_SET_AR3K_BAUD | AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP)) {
            status = AR3KConfigureHCIBaud(pConfig);      
            if (A_FAILED(status)) {
                break;    
            }
        }     



        if (pConfig->PwrMgmtEnabled) {
            /* the delay is required after the previous HCI reset before further
             * HCI commands can be issued
             */
            A_MDELAY(200);
            AR3KEnableTLPM(pConfig);
        }
               
           /* re-enable asynchronous recv */
        status = HCI_TransportEnableDisableAsyncRecv(pConfig->pHCIDev,TRUE);
        if (A_FAILED(status)) {
            break;    
        }     
    
    
    } while (FALSE);   
    
  
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR3K Config: Configuration Complete (status = %d) \n",status));
    
    return status;
}

A_STATUS AR3KConfigureExit(void *config)
{
    A_STATUS        status = A_OK; 
    AR3K_CONFIG_INFO *pConfig = (AR3K_CONFIG_INFO *)config;
        
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR3K Config: Cleaning up AR3K ...\n"));
                                
    do {
        
        if ((pConfig->pHCIDev == NULL) || (pConfig->pHCIProps == NULL) || (pConfig->pHIFDevice == NULL)) {
            status = A_EINVAL;
            break;    
        }
        
            /* disable asynchronous recv while we issue commands and receive events synchronously */
        status = HCI_TransportEnableDisableAsyncRecv(pConfig->pHCIDev,FALSE);
        if (A_FAILED(status)) {
            break;    
        }
      
        if (pConfig->Flags & 
                (AR3K_CONFIG_FLAG_SET_AR3K_BAUD | AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP)) {
            status = AR3KConfigureHCIBaud(pConfig);      
            if (A_FAILED(status)) {
                break;    
            }
        }

           /* re-enable asynchronous recv */
        status = HCI_TransportEnableDisableAsyncRecv(pConfig->pHCIDev,TRUE);
        if (A_FAILED(status)) {
            break;    
        }     
    
    
    } while (FALSE);   
    
  
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR3K Config: Cleanup Complete (status = %d) \n",status));
    
    return status;
}

