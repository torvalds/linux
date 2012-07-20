/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_LIB_H__
#define CSR_WIFI_SME_LIB_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_sched.h"
#include "csr_util.h"
#include "csr_msg_transport.h"

#include "csr_wifi_lib.h"

#include "csr_wifi_sme_prim.h"
#include "csr_wifi_sme_task.h"


#ifndef CSR_WIFI_SME_LIB_DESTINATION_QUEUE
# ifdef CSR_WIFI_NME_ENABLE
# include "csr_wifi_nme_task.h"
# define CSR_WIFI_SME_LIB_DESTINATION_QUEUE CSR_WIFI_NME_IFACEQUEUE
# else
# define CSR_WIFI_SME_LIB_DESTINATION_QUEUE CSR_WIFI_SME_IFACEQUEUE
# endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  CsrWifiSmeFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_SME upstream message. Does not
 *      free the message itself, and can only be used for upstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_SME upstream message
 *----------------------------------------------------------------------------*/
void CsrWifiSmeFreeUpstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 *  CsrWifiSmeFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_SME downstream message. Does not
 *      free the message itself, and can only be used for downstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_SME downstream message
 *----------------------------------------------------------------------------*/
void CsrWifiSmeFreeDownstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 * Enum to string functions
 *----------------------------------------------------------------------------*/
const char* CsrWifiSme80211NetworkTypeToString(CsrWifiSme80211NetworkType value);
const char* CsrWifiSme80211PrivacyModeToString(CsrWifiSme80211PrivacyMode value);
const char* CsrWifiSme80211dTrustLevelToString(CsrWifiSme80211dTrustLevel value);
const char* CsrWifiSmeAmpStatusToString(CsrWifiSmeAmpStatus value);
const char* CsrWifiSmeAuthModeToString(CsrWifiSmeAuthMode value);
const char* CsrWifiSmeBasicUsabilityToString(CsrWifiSmeBasicUsability value);
const char* CsrWifiSmeBssTypeToString(CsrWifiSmeBssType value);
const char* CsrWifiSmeCoexSchemeToString(CsrWifiSmeCoexScheme value);
const char* CsrWifiSmeControlIndicationToString(CsrWifiSmeControlIndication value);
const char* CsrWifiSmeCtsProtectionTypeToString(CsrWifiSmeCtsProtectionType value);
const char* CsrWifiSmeD3AutoScanModeToString(CsrWifiSmeD3AutoScanMode value);
const char* CsrWifiSmeEncryptionToString(CsrWifiSmeEncryption value);
const char* CsrWifiSmeFirmwareDriverInterfaceToString(CsrWifiSmeFirmwareDriverInterface value);
const char* CsrWifiSmeHostPowerModeToString(CsrWifiSmeHostPowerMode value);
const char* CsrWifiSmeIEEE80211ReasonToString(CsrWifiSmeIEEE80211Reason value);
const char* CsrWifiSmeIEEE80211ResultToString(CsrWifiSmeIEEE80211Result value);
const char* CsrWifiSmeIndicationsToString(CsrWifiSmeIndications value);
const char* CsrWifiSmeKeyTypeToString(CsrWifiSmeKeyType value);
const char* CsrWifiSmeListActionToString(CsrWifiSmeListAction value);
const char* CsrWifiSmeMediaStatusToString(CsrWifiSmeMediaStatus value);
const char* CsrWifiSmeP2pCapabilityToString(CsrWifiSmeP2pCapability value);
const char* CsrWifiSmeP2pGroupCapabilityToString(CsrWifiSmeP2pGroupCapability value);
const char* CsrWifiSmeP2pNoaConfigMethodToString(CsrWifiSmeP2pNoaConfigMethod value);
const char* CsrWifiSmeP2pRoleToString(CsrWifiSmeP2pRole value);
const char* CsrWifiSmeP2pStatusToString(CsrWifiSmeP2pStatus value);
const char* CsrWifiSmePacketFilterModeToString(CsrWifiSmePacketFilterMode value);
const char* CsrWifiSmePowerSaveLevelToString(CsrWifiSmePowerSaveLevel value);
const char* CsrWifiSmePreambleTypeToString(CsrWifiSmePreambleType value);
const char* CsrWifiSmeRadioIFToString(CsrWifiSmeRadioIF value);
const char* CsrWifiSmeRegulatoryDomainToString(CsrWifiSmeRegulatoryDomain value);
const char* CsrWifiSmeRoamReasonToString(CsrWifiSmeRoamReason value);
const char* CsrWifiSmeScanTypeToString(CsrWifiSmeScanType value);
const char* CsrWifiSmeTrafficTypeToString(CsrWifiSmeTrafficType value);
const char* CsrWifiSmeTspecCtrlToString(CsrWifiSmeTspecCtrl value);
const char* CsrWifiSmeTspecResultCodeToString(CsrWifiSmeTspecResultCode value);
const char* CsrWifiSmeWepAuthModeToString(CsrWifiSmeWepAuthMode value);
const char* CsrWifiSmeWepCredentialTypeToString(CsrWifiSmeWepCredentialType value);
const char* CsrWifiSmeWmmModeToString(CsrWifiSmeWmmMode value);
const char* CsrWifiSmeWmmQosInfoToString(CsrWifiSmeWmmQosInfo value);
const char* CsrWifiSmeWpsConfigTypeToString(CsrWifiSmeWpsConfigType value);
const char* CsrWifiSmeWpsDeviceCategoryToString(CsrWifiSmeWpsDeviceCategory value);
const char* CsrWifiSmeWpsDeviceSubCategoryToString(CsrWifiSmeWpsDeviceSubCategory value);
const char* CsrWifiSmeWpsDpidToString(CsrWifiSmeWpsDpid value);
const char* CsrWifiSmeWpsRegistrationToString(CsrWifiSmeWpsRegistration value);


/*----------------------------------------------------------------------------*
 * CsrPrim Type toString function.
 * Converts a message type to the String name of the Message
 *----------------------------------------------------------------------------*/
const char* CsrWifiSmePrimTypeToString(CsrPrim msgType);

/*----------------------------------------------------------------------------*
 * Lookup arrays for PrimType name Strings
 *----------------------------------------------------------------------------*/
extern const char *CsrWifiSmeUpstreamPrimNames[CSR_WIFI_SME_PRIM_UPSTREAM_COUNT];
extern const char *CsrWifiSmeDownstreamPrimNames[CSR_WIFI_SME_PRIM_DOWNSTREAM_COUNT];

/*******************************************************************************

  NAME
    CsrWifiSmeActivateReqSend

  DESCRIPTION
    The WMA sends this primitive to activate the SME.
    The WMA must activate the SME before it can send any other primitive.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeActivateReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeActivateReq *) CsrPmemAlloc(sizeof(CsrWifiSmeActivateReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ACTIVATE_REQ, dst__, src__);

#define CsrWifiSmeActivateReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeActivateReq *msg__; \
        CsrWifiSmeActivateReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeActivateReqSend(src__) \
    CsrWifiSmeActivateReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeActivateCfmSend

  DESCRIPTION
    The SME sends this primitive when the activation is complete.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeActivateCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeActivateCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeActivateCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ACTIVATE_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeActivateCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeActivateCfm *msg__; \
        CsrWifiSmeActivateCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeActivateCfmSend(dst__, status__) \
    CsrWifiSmeActivateCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the adHocConfig parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeAdhocConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeAdhocConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeAdhocConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ADHOC_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmeAdhocConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeAdhocConfigGetReq *msg__; \
        CsrWifiSmeAdhocConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAdhocConfigGetReqSend(src__) \
    CsrWifiSmeAdhocConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue       - Destination Task Queue
    status      - Reports the result of the request
    adHocConfig - Contains the values used when starting an Ad-hoc (IBSS)
                  connection.

*******************************************************************************/
#define CsrWifiSmeAdhocConfigGetCfmCreate(msg__, dst__, src__, status__, adHocConfig__) \
    msg__ = (CsrWifiSmeAdhocConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeAdhocConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ADHOC_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->adHocConfig = (adHocConfig__);

#define CsrWifiSmeAdhocConfigGetCfmSendTo(dst__, src__, status__, adHocConfig__) \
    { \
        CsrWifiSmeAdhocConfigGetCfm *msg__; \
        CsrWifiSmeAdhocConfigGetCfmCreate(msg__, dst__, src__, status__, adHocConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAdhocConfigGetCfmSend(dst__, status__, adHocConfig__) \
    CsrWifiSmeAdhocConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, adHocConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the adHocConfig parameter.

  PARAMETERS
    queue       - Message Source Task Queue (Cfm's will be sent to this Queue)
    adHocConfig - Sets the values to use when starting an ad hoc network.

*******************************************************************************/
#define CsrWifiSmeAdhocConfigSetReqCreate(msg__, dst__, src__, adHocConfig__) \
    msg__ = (CsrWifiSmeAdhocConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeAdhocConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ADHOC_CONFIG_SET_REQ, dst__, src__); \
    msg__->adHocConfig = (adHocConfig__);

#define CsrWifiSmeAdhocConfigSetReqSendTo(dst__, src__, adHocConfig__) \
    { \
        CsrWifiSmeAdhocConfigSetReq *msg__; \
        CsrWifiSmeAdhocConfigSetReqCreate(msg__, dst__, src__, adHocConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAdhocConfigSetReqSend(src__, adHocConfig__) \
    CsrWifiSmeAdhocConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, adHocConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeAdhocConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeAdhocConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeAdhocConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ADHOC_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeAdhocConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeAdhocConfigSetCfm *msg__; \
        CsrWifiSmeAdhocConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAdhocConfigSetCfmSend(dst__, status__) \
    CsrWifiSmeAdhocConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeAmpStatusChangeIndSend

  DESCRIPTION
    Indication of change to AMP activity.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface on which the AMP activity changed.
    ampStatus    - The new status of AMP activity.Range: {AMP_ACTIVE,
                   AMP_INACTIVE}.

*******************************************************************************/
#define CsrWifiSmeAmpStatusChangeIndCreate(msg__, dst__, src__, interfaceTag__, ampStatus__) \
    msg__ = (CsrWifiSmeAmpStatusChangeInd *) CsrPmemAlloc(sizeof(CsrWifiSmeAmpStatusChangeInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_AMP_STATUS_CHANGE_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->ampStatus = (ampStatus__);

#define CsrWifiSmeAmpStatusChangeIndSendTo(dst__, src__, interfaceTag__, ampStatus__) \
    { \
        CsrWifiSmeAmpStatusChangeInd *msg__; \
        CsrWifiSmeAmpStatusChangeIndCreate(msg__, dst__, src__, interfaceTag__, ampStatus__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAmpStatusChangeIndSend(dst__, interfaceTag__, ampStatus__) \
    CsrWifiSmeAmpStatusChangeIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, ampStatus__)

/*******************************************************************************

  NAME
    CsrWifiSmeAssociationCompleteIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it completes an attempt to associate with an AP. If
    the association was successful, status will be set to
    CSR_WIFI_SME_STATUS_SUCCESS, otherwise status and deauthReason shall be
    set to appropriate error codes.

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the association procedure
    connectionInfo - This parameter is relevant only if result is
                     CSR_WIFI_SME_STATUS_SUCCESS:
                     it points to the connection information for the new network
    deauthReason   - This parameter is relevant only if result is not
                     CSR_WIFI_SME_STATUS_SUCCESS:
                     if the AP deauthorised the station, it gives the reason of
                     the deauthorization

*******************************************************************************/
#define CsrWifiSmeAssociationCompleteIndCreate(msg__, dst__, src__, interfaceTag__, status__, connectionInfo__, deauthReason__) \
    msg__ = (CsrWifiSmeAssociationCompleteInd *) CsrPmemAlloc(sizeof(CsrWifiSmeAssociationCompleteInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ASSOCIATION_COMPLETE_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectionInfo = (connectionInfo__); \
    msg__->deauthReason = (deauthReason__);

#define CsrWifiSmeAssociationCompleteIndSendTo(dst__, src__, interfaceTag__, status__, connectionInfo__, deauthReason__) \
    { \
        CsrWifiSmeAssociationCompleteInd *msg__; \
        CsrWifiSmeAssociationCompleteIndCreate(msg__, dst__, src__, interfaceTag__, status__, connectionInfo__, deauthReason__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAssociationCompleteIndSend(dst__, interfaceTag__, status__, connectionInfo__, deauthReason__) \
    CsrWifiSmeAssociationCompleteIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, connectionInfo__, deauthReason__)

/*******************************************************************************

  NAME
    CsrWifiSmeAssociationStartIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it begins an attempt to associate with an AP.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    address      - BSSID of the associating network
    ssid         - Service Set identifier of the associating network

*******************************************************************************/
#define CsrWifiSmeAssociationStartIndCreate(msg__, dst__, src__, interfaceTag__, address__, ssid__) \
    msg__ = (CsrWifiSmeAssociationStartInd *) CsrPmemAlloc(sizeof(CsrWifiSmeAssociationStartInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ASSOCIATION_START_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->address = (address__); \
    msg__->ssid = (ssid__);

#define CsrWifiSmeAssociationStartIndSendTo(dst__, src__, interfaceTag__, address__, ssid__) \
    { \
        CsrWifiSmeAssociationStartInd *msg__; \
        CsrWifiSmeAssociationStartIndCreate(msg__, dst__, src__, interfaceTag__, address__, ssid__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeAssociationStartIndSend(dst__, interfaceTag__, address__, ssid__) \
    CsrWifiSmeAssociationStartIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, address__, ssid__)

/*******************************************************************************

  NAME
    CsrWifiSmeBlacklistReqSend

  DESCRIPTION
    The wireless manager application should call this primitive to notify the
    driver of any networks that should not be connected to. The interface
    allows the wireless manager application to query, add, remove, and flush
    the BSSIDs that the driver may not connect or roam to.
    When this primitive adds to the black list the BSSID to which the SME is
    currently connected, the SME will try to roam, if applicable, to another
    BSSID in the same ESS; if the roaming procedure fails, the SME will
    disconnect.

  PARAMETERS
    queue           - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag    - Interface Identifier; unique identifier of an interface
    action          - The value of the CsrWifiSmeListAction parameter instructs
                      the driver to modify or provide the list of blacklisted
                      networks.
    setAddressCount - Number of BSSIDs sent with this primitive
    setAddresses    - Pointer to the list of BBSIDs sent with the primitive, set
                      to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmeBlacklistReqCreate(msg__, dst__, src__, interfaceTag__, action__, setAddressCount__, setAddresses__) \
    msg__ = (CsrWifiSmeBlacklistReq *) CsrPmemAlloc(sizeof(CsrWifiSmeBlacklistReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_BLACKLIST_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->action = (action__); \
    msg__->setAddressCount = (setAddressCount__); \
    msg__->setAddresses = (setAddresses__);

#define CsrWifiSmeBlacklistReqSendTo(dst__, src__, interfaceTag__, action__, setAddressCount__, setAddresses__) \
    { \
        CsrWifiSmeBlacklistReq *msg__; \
        CsrWifiSmeBlacklistReqCreate(msg__, dst__, src__, interfaceTag__, action__, setAddressCount__, setAddresses__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeBlacklistReqSend(src__, interfaceTag__, action__, setAddressCount__, setAddresses__) \
    CsrWifiSmeBlacklistReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, action__, setAddressCount__, setAddresses__)

/*******************************************************************************

  NAME
    CsrWifiSmeBlacklistCfmSend

  DESCRIPTION
    The SME will call this primitive when the action on the blacklist has
    completed. For a GET action, this primitive also reports the list of
    BBSIDs in the blacklist.

  PARAMETERS
    queue           - Destination Task Queue
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    action          - Action in the request
    getAddressCount - This parameter is only relevant if action is
                      CSR_WIFI_SME_LIST_ACTION_GET:
                      number of BSSIDs sent with this primitive
    getAddresses    - Pointer to the list of BBSIDs sent with the primitive, set
                      to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmeBlacklistCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getAddressCount__, getAddresses__) \
    msg__ = (CsrWifiSmeBlacklistCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeBlacklistCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_BLACKLIST_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->action = (action__); \
    msg__->getAddressCount = (getAddressCount__); \
    msg__->getAddresses = (getAddresses__);

#define CsrWifiSmeBlacklistCfmSendTo(dst__, src__, interfaceTag__, status__, action__, getAddressCount__, getAddresses__) \
    { \
        CsrWifiSmeBlacklistCfm *msg__; \
        CsrWifiSmeBlacklistCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getAddressCount__, getAddresses__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeBlacklistCfmSend(dst__, interfaceTag__, status__, action__, getAddressCount__, getAddresses__) \
    CsrWifiSmeBlacklistCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, action__, getAddressCount__, getAddresses__)

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataGetReqSend

  DESCRIPTION
    This primitive retrieves the Wi-Fi radio calibration data.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeCalibrationDataGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeCalibrationDataGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCalibrationDataGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CALIBRATION_DATA_GET_REQ, dst__, src__);

#define CsrWifiSmeCalibrationDataGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeCalibrationDataGetReq *msg__; \
        CsrWifiSmeCalibrationDataGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCalibrationDataGetReqSend(src__) \
    CsrWifiSmeCalibrationDataGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue                 - Destination Task Queue
    status                - Reports the result of the request
    calibrationDataLength - Number of bytes in the buffer pointed by
                            calibrationData
    calibrationData       - Pointer to a buffer of length calibrationDataLength
                            containing the calibration data

*******************************************************************************/
#define CsrWifiSmeCalibrationDataGetCfmCreate(msg__, dst__, src__, status__, calibrationDataLength__, calibrationData__) \
    msg__ = (CsrWifiSmeCalibrationDataGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCalibrationDataGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CALIBRATION_DATA_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->calibrationDataLength = (calibrationDataLength__); \
    msg__->calibrationData = (calibrationData__);

#define CsrWifiSmeCalibrationDataGetCfmSendTo(dst__, src__, status__, calibrationDataLength__, calibrationData__) \
    { \
        CsrWifiSmeCalibrationDataGetCfm *msg__; \
        CsrWifiSmeCalibrationDataGetCfmCreate(msg__, dst__, src__, status__, calibrationDataLength__, calibrationData__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCalibrationDataGetCfmSend(dst__, status__, calibrationDataLength__, calibrationData__) \
    CsrWifiSmeCalibrationDataGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, calibrationDataLength__, calibrationData__)

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataSetReqSend

  DESCRIPTION
    This primitive sets the Wi-Fi radio calibration data.
    The usage of the primitive with proper calibration data will avoid
    time-consuming configuration after power-up.

  PARAMETERS
    queue                 - Message Source Task Queue (Cfm's will be sent to this Queue)
    calibrationDataLength - Number of bytes in the buffer pointed by
                            calibrationData
    calibrationData       - Pointer to a buffer of length calibrationDataLength
                            containing the calibration data

*******************************************************************************/
#define CsrWifiSmeCalibrationDataSetReqCreate(msg__, dst__, src__, calibrationDataLength__, calibrationData__) \
    msg__ = (CsrWifiSmeCalibrationDataSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCalibrationDataSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CALIBRATION_DATA_SET_REQ, dst__, src__); \
    msg__->calibrationDataLength = (calibrationDataLength__); \
    msg__->calibrationData = (calibrationData__);

#define CsrWifiSmeCalibrationDataSetReqSendTo(dst__, src__, calibrationDataLength__, calibrationData__) \
    { \
        CsrWifiSmeCalibrationDataSetReq *msg__; \
        CsrWifiSmeCalibrationDataSetReqCreate(msg__, dst__, src__, calibrationDataLength__, calibrationData__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCalibrationDataSetReqSend(src__, calibrationDataLength__, calibrationData__) \
    CsrWifiSmeCalibrationDataSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, calibrationDataLength__, calibrationData__)

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeCalibrationDataSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeCalibrationDataSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCalibrationDataSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CALIBRATION_DATA_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeCalibrationDataSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeCalibrationDataSetCfm *msg__; \
        CsrWifiSmeCalibrationDataSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCalibrationDataSetCfmSend(dst__, status__) \
    CsrWifiSmeCalibrationDataSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the CcxConfig parameter.
    CURRENTLY NOT SUPPORTED.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeCcxConfigGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeCcxConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCcxConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CCX_CONFIG_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeCcxConfigGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeCcxConfigGetReq *msg__; \
        CsrWifiSmeCcxConfigGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCcxConfigGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeCcxConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    ccxConfig    - Currently not supported

*******************************************************************************/
#define CsrWifiSmeCcxConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, ccxConfig__) \
    msg__ = (CsrWifiSmeCcxConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCcxConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CCX_CONFIG_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->ccxConfig = (ccxConfig__);

#define CsrWifiSmeCcxConfigGetCfmSendTo(dst__, src__, interfaceTag__, status__, ccxConfig__) \
    { \
        CsrWifiSmeCcxConfigGetCfm *msg__; \
        CsrWifiSmeCcxConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, ccxConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCcxConfigGetCfmSend(dst__, interfaceTag__, status__, ccxConfig__) \
    CsrWifiSmeCcxConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, ccxConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the CcxConfig parameter.
    CURRENTLY NOT SUPPORTED.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface
    ccxConfig    - Currently not supported

*******************************************************************************/
#define CsrWifiSmeCcxConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, ccxConfig__) \
    msg__ = (CsrWifiSmeCcxConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCcxConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CCX_CONFIG_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->ccxConfig = (ccxConfig__);

#define CsrWifiSmeCcxConfigSetReqSendTo(dst__, src__, interfaceTag__, ccxConfig__) \
    { \
        CsrWifiSmeCcxConfigSetReq *msg__; \
        CsrWifiSmeCcxConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, ccxConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCcxConfigSetReqSend(src__, interfaceTag__, ccxConfig__) \
    CsrWifiSmeCcxConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, ccxConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeCcxConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeCcxConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCcxConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CCX_CONFIG_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeCcxConfigSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeCcxConfigSetCfm *msg__; \
        CsrWifiSmeCcxConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCcxConfigSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeCcxConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsGetReqSend

  DESCRIPTION
    This primitive gets the value of the CloakedSsids parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeCloakedSsidsGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeCloakedSsidsGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCloakedSsidsGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CLOAKED_SSIDS_GET_REQ, dst__, src__);

#define CsrWifiSmeCloakedSsidsGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeCloakedSsidsGetReq *msg__; \
        CsrWifiSmeCloakedSsidsGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCloakedSsidsGetReqSend(src__) \
    CsrWifiSmeCloakedSsidsGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    status       - Reports the result of the request
    cloakedSsids - Reports list of cloaked SSIDs that are explicitly scanned for
                   by the driver

*******************************************************************************/
#define CsrWifiSmeCloakedSsidsGetCfmCreate(msg__, dst__, src__, status__, cloakedSsids__) \
    msg__ = (CsrWifiSmeCloakedSsidsGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCloakedSsidsGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CLOAKED_SSIDS_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->cloakedSsids = (cloakedSsids__);

#define CsrWifiSmeCloakedSsidsGetCfmSendTo(dst__, src__, status__, cloakedSsids__) \
    { \
        CsrWifiSmeCloakedSsidsGetCfm *msg__; \
        CsrWifiSmeCloakedSsidsGetCfmCreate(msg__, dst__, src__, status__, cloakedSsids__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCloakedSsidsGetCfmSend(dst__, status__, cloakedSsids__) \
    CsrWifiSmeCloakedSsidsGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, cloakedSsids__)

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsSetReqSend

  DESCRIPTION
    This primitive sets the list of cloaked SSIDs for which the WMA possesses
    profiles.
    When the driver detects a cloaked AP, the SME will explicitly scan for it
    using the list of cloaked SSIDs provided it, and, if the scan succeeds,
    it will report the AP to the WMA either via CSR_WIFI_SME_SCAN_RESULT_IND
    (if registered) or via CSR_WIFI_SCAN_RESULT_GET_CFM.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    cloakedSsids - Sets the list of cloaked SSIDs

*******************************************************************************/
#define CsrWifiSmeCloakedSsidsSetReqCreate(msg__, dst__, src__, cloakedSsids__) \
    msg__ = (CsrWifiSmeCloakedSsidsSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCloakedSsidsSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CLOAKED_SSIDS_SET_REQ, dst__, src__); \
    msg__->cloakedSsids = (cloakedSsids__);

#define CsrWifiSmeCloakedSsidsSetReqSendTo(dst__, src__, cloakedSsids__) \
    { \
        CsrWifiSmeCloakedSsidsSetReq *msg__; \
        CsrWifiSmeCloakedSsidsSetReqCreate(msg__, dst__, src__, cloakedSsids__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCloakedSsidsSetReqSend(src__, cloakedSsids__) \
    CsrWifiSmeCloakedSsidsSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, cloakedSsids__)

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeCloakedSsidsSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeCloakedSsidsSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCloakedSsidsSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CLOAKED_SSIDS_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeCloakedSsidsSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeCloakedSsidsSetCfm *msg__; \
        CsrWifiSmeCloakedSsidsSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCloakedSsidsSetCfmSend(dst__, status__) \
    CsrWifiSmeCloakedSsidsSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the CoexConfig parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeCoexConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeCoexConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmeCoexConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeCoexConfigGetReq *msg__; \
        CsrWifiSmeCoexConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexConfigGetReqSend(src__) \
    CsrWifiSmeCoexConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue      - Destination Task Queue
    status     - Reports the result of the request
    coexConfig - Reports the parameters used to configure the coexistence
                 behaviour

*******************************************************************************/
#define CsrWifiSmeCoexConfigGetCfmCreate(msg__, dst__, src__, status__, coexConfig__) \
    msg__ = (CsrWifiSmeCoexConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->coexConfig = (coexConfig__);

#define CsrWifiSmeCoexConfigGetCfmSendTo(dst__, src__, status__, coexConfig__) \
    { \
        CsrWifiSmeCoexConfigGetCfm *msg__; \
        CsrWifiSmeCoexConfigGetCfmCreate(msg__, dst__, src__, status__, coexConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexConfigGetCfmSend(dst__, status__, coexConfig__) \
    CsrWifiSmeCoexConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, coexConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the CoexConfig parameter.

  PARAMETERS
    queue      - Message Source Task Queue (Cfm's will be sent to this Queue)
    coexConfig - Configures the coexistence behaviour

*******************************************************************************/
#define CsrWifiSmeCoexConfigSetReqCreate(msg__, dst__, src__, coexConfig__) \
    msg__ = (CsrWifiSmeCoexConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_CONFIG_SET_REQ, dst__, src__); \
    msg__->coexConfig = (coexConfig__);

#define CsrWifiSmeCoexConfigSetReqSendTo(dst__, src__, coexConfig__) \
    { \
        CsrWifiSmeCoexConfigSetReq *msg__; \
        CsrWifiSmeCoexConfigSetReqCreate(msg__, dst__, src__, coexConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexConfigSetReqSend(src__, coexConfig__) \
    CsrWifiSmeCoexConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, coexConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeCoexConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeCoexConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeCoexConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeCoexConfigSetCfm *msg__; \
        CsrWifiSmeCoexConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexConfigSetCfmSend(dst__, status__) \
    CsrWifiSmeCoexConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexInfoGetReqSend

  DESCRIPTION
    This primitive gets the value of the CoexInfo parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeCoexInfoGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeCoexInfoGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexInfoGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_INFO_GET_REQ, dst__, src__);

#define CsrWifiSmeCoexInfoGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeCoexInfoGetReq *msg__; \
        CsrWifiSmeCoexInfoGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexInfoGetReqSend(src__) \
    CsrWifiSmeCoexInfoGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexInfoGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue    - Destination Task Queue
    status   - Reports the result of the request
    coexInfo - Reports information and state related to coexistence.

*******************************************************************************/
#define CsrWifiSmeCoexInfoGetCfmCreate(msg__, dst__, src__, status__, coexInfo__) \
    msg__ = (CsrWifiSmeCoexInfoGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeCoexInfoGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_COEX_INFO_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->coexInfo = (coexInfo__);

#define CsrWifiSmeCoexInfoGetCfmSendTo(dst__, src__, status__, coexInfo__) \
    { \
        CsrWifiSmeCoexInfoGetCfm *msg__; \
        CsrWifiSmeCoexInfoGetCfmCreate(msg__, dst__, src__, status__, coexInfo__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoexInfoGetCfmSend(dst__, status__, coexInfo__) \
    CsrWifiSmeCoexInfoGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, coexInfo__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to start the
    process of joining an 802.11 wireless network or to start an ad hoc
    network.
    The structure pointed by connectionConfig contains parameters describing
    the network to join or, in case of an ad hoc network, to host or join.
    The SME will select a network, perform the IEEE 802.11 Join, Authenticate
    and Associate exchanges.
    The SME selects the networks from the current scan list that match both
    the SSID and BSSID, however either or both of these may be the wildcard
    value. Using this rule, the following operations are possible:
      * To connect to a network by name, specify the SSID and set the BSSID to
        0xFF 0xFF 0xFF 0xFF 0xFF 0xFF. If there are two or more networks visible,
        the SME will select the one with the strongest signal.
      * To connect to a specific network, specify the BSSID. The SSID is
        optional, but if given it must match the SSID of the network. An empty
        SSID may be specified by setting the SSID length to zero. Please note
        that if the BSSID is specified (i.e. not equal to 0xFF 0xFF 0xFF 0xFF
        0xFF 0xFF), the SME will not attempt to roam if signal conditions become
        poor, even if there is an alternative AP with an SSID that matches the
        current network SSID.
      * To connect to any network matching the other parameters (i.e. security,
        etc), set the SSID length to zero and set the BSSID to 0xFF 0xFF 0xFF
        0xFF 0xFF 0xFF. In this case, the SME will order all available networks
        by their signal strengths and will iterate through this list until it
        successfully connects.
    NOTE: Specifying the BSSID will restrict the selection to one specific
    network. If SSID and BSSID are given, they must both match the network
    for it to be selected. To select a network based on the SSID only, the
    wireless manager application must set the BSSID to 0xFF 0xFF 0xFF 0xFF
    0xFF 0xFF.
    The SME will try to connect to each network that matches the provided
    parameters, one by one, until it succeeds or has tried unsuccessfully
    with all the matching networks.
    If there is no network that matches the parameters and the request allows
    to host an ad hoc network, the SME will advertise a new ad hoc network
    instead.
    If the SME cannot connect, it will notify the failure in the confirm.

  PARAMETERS
    queue            - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag     - Interface Identifier; unique identifier of an interface
    connectionConfig - Describes the candidate network to join or to host.

*******************************************************************************/
#define CsrWifiSmeConnectReqCreate(msg__, dst__, src__, interfaceTag__, connectionConfig__) \
    msg__ = (CsrWifiSmeConnectReq *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECT_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->connectionConfig = (connectionConfig__);

#define CsrWifiSmeConnectReqSendTo(dst__, src__, interfaceTag__, connectionConfig__) \
    { \
        CsrWifiSmeConnectReq *msg__; \
        CsrWifiSmeConnectReqCreate(msg__, dst__, src__, interfaceTag__, connectionConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectReqSend(src__, interfaceTag__, connectionConfig__) \
    CsrWifiSmeConnectReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, connectionConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectCfmSend

  DESCRIPTION
    The SME calls this primitive when the connection exchange is complete or
    all connection attempts fail.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request.
                   CSR_WIFI_SME_STATUS_NOT_FOUND: all attempts by the SME to
                   locate the requested AP failed

*******************************************************************************/
#define CsrWifiSmeConnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeConnectCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECT_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeConnectCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeConnectCfm *msg__; \
        CsrWifiSmeConnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeConnectCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the ConnectionConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeConnectionConfigGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeConnectionConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_CONFIG_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeConnectionConfigGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeConnectionConfigGetReq *msg__; \
        CsrWifiSmeConnectionConfigGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionConfigGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeConnectionConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue            - Destination Task Queue
    interfaceTag     - Interface Identifier; unique identifier of an interface
    status           - Reports the result of the request
    connectionConfig - Parameters used by the SME for selecting a network

*******************************************************************************/
#define CsrWifiSmeConnectionConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionConfig__) \
    msg__ = (CsrWifiSmeConnectionConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_CONFIG_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectionConfig = (connectionConfig__);

#define CsrWifiSmeConnectionConfigGetCfmSendTo(dst__, src__, interfaceTag__, status__, connectionConfig__) \
    { \
        CsrWifiSmeConnectionConfigGetCfm *msg__; \
        CsrWifiSmeConnectionConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionConfigGetCfmSend(dst__, interfaceTag__, status__, connectionConfig__) \
    CsrWifiSmeConnectionConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, connectionConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionInfoGetReqSend

  DESCRIPTION
    This primitive gets the value of the ConnectionInfo parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeConnectionInfoGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeConnectionInfoGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionInfoGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_INFO_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeConnectionInfoGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeConnectionInfoGetReq *msg__; \
        CsrWifiSmeConnectionInfoGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionInfoGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeConnectionInfoGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionInfoGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    connectionInfo - Information about the current connection

*******************************************************************************/
#define CsrWifiSmeConnectionInfoGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionInfo__) \
    msg__ = (CsrWifiSmeConnectionInfoGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionInfoGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_INFO_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectionInfo = (connectionInfo__);

#define CsrWifiSmeConnectionInfoGetCfmSendTo(dst__, src__, interfaceTag__, status__, connectionInfo__) \
    { \
        CsrWifiSmeConnectionInfoGetCfm *msg__; \
        CsrWifiSmeConnectionInfoGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionInfo__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionInfoGetCfmSend(dst__, interfaceTag__, status__, connectionInfo__) \
    CsrWifiSmeConnectionInfoGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, connectionInfo__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionQualityIndSend

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever the value of the current connection quality
    parameters change by more than a certain configurable amount.
    The wireless manager application may configure the trigger thresholds for
    this indication using the field in smeConfig parameter of
    CSR_WIFI_SME_SME_CONFIG_SET_REQ.
    Connection quality messages can be suppressed by setting both thresholds
    to zero.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    linkQuality  - Indicates the quality of the link

*******************************************************************************/
#define CsrWifiSmeConnectionQualityIndCreate(msg__, dst__, src__, interfaceTag__, linkQuality__) \
    msg__ = (CsrWifiSmeConnectionQualityInd *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionQualityInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_QUALITY_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->linkQuality = (linkQuality__);

#define CsrWifiSmeConnectionQualityIndSendTo(dst__, src__, interfaceTag__, linkQuality__) \
    { \
        CsrWifiSmeConnectionQualityInd *msg__; \
        CsrWifiSmeConnectionQualityIndCreate(msg__, dst__, src__, interfaceTag__, linkQuality__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionQualityIndSend(dst__, interfaceTag__, linkQuality__) \
    CsrWifiSmeConnectionQualityIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, linkQuality__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionStatsGetReqSend

  DESCRIPTION
    This primitive gets the value of the ConnectionStats parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeConnectionStatsGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeConnectionStatsGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionStatsGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_STATS_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeConnectionStatsGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeConnectionStatsGetReq *msg__; \
        CsrWifiSmeConnectionStatsGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionStatsGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeConnectionStatsGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionStatsGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue           - Destination Task Queue
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    connectionStats - Statistics for current connection.

*******************************************************************************/
#define CsrWifiSmeConnectionStatsGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionStats__) \
    msg__ = (CsrWifiSmeConnectionStatsGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeConnectionStatsGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CONNECTION_STATS_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectionStats = (connectionStats__);

#define CsrWifiSmeConnectionStatsGetCfmSendTo(dst__, src__, interfaceTag__, status__, connectionStats__) \
    { \
        CsrWifiSmeConnectionStatsGetCfm *msg__; \
        CsrWifiSmeConnectionStatsGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionStats__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeConnectionStatsGetCfmSend(dst__, interfaceTag__, status__, connectionStats__) \
    CsrWifiSmeConnectionStatsGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, connectionStats__)

/*******************************************************************************

  NAME
    CsrWifiSmeCoreDumpIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive Wi-Fi Chip core dump data.
    The core dump data may be fragmented and sent using more than one
    indication.
    To indicate that all the data has been sent, the last indication contains
    a 'length' of 0 and 'data' of NULL.

  PARAMETERS
    queue      - Destination Task Queue
    dataLength - Number of bytes in the buffer pointed to by 'data'
    data       - Pointer to the buffer containing 'dataLength' bytes of core
                 dump data

*******************************************************************************/
#define CsrWifiSmeCoreDumpIndCreate(msg__, dst__, src__, dataLength__, data__) \
    msg__ = (CsrWifiSmeCoreDumpInd *) CsrPmemAlloc(sizeof(CsrWifiSmeCoreDumpInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_CORE_DUMP_IND, dst__, src__); \
    msg__->dataLength = (dataLength__); \
    msg__->data = (data__);

#define CsrWifiSmeCoreDumpIndSendTo(dst__, src__, dataLength__, data__) \
    { \
        CsrWifiSmeCoreDumpInd *msg__; \
        CsrWifiSmeCoreDumpIndCreate(msg__, dst__, src__, dataLength__, data__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeCoreDumpIndSend(dst__, dataLength__, data__) \
    CsrWifiSmeCoreDumpIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, dataLength__, data__)

/*******************************************************************************

  NAME
    CsrWifiSmeDeactivateReqSend

  DESCRIPTION
    The WMA sends this primitive to deactivate the SME.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeDeactivateReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeDeactivateReq *) CsrPmemAlloc(sizeof(CsrWifiSmeDeactivateReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_DEACTIVATE_REQ, dst__, src__);

#define CsrWifiSmeDeactivateReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeDeactivateReq *msg__; \
        CsrWifiSmeDeactivateReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeDeactivateReqSend(src__) \
    CsrWifiSmeDeactivateReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeDeactivateCfmSend

  DESCRIPTION
    The SME sends this primitive when the deactivation is complete.
    The WMA cannot send any more primitives until it actives the SME again
    sending another CSR_WIFI_SME_ACTIVATE_REQ.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeDeactivateCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeDeactivateCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeDeactivateCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_DEACTIVATE_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeDeactivateCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeDeactivateCfm *msg__; \
        CsrWifiSmeDeactivateCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeDeactivateCfmSend(dst__, status__) \
    CsrWifiSmeDeactivateCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeDisconnectReqSend

  DESCRIPTION
    The wireless manager application may disconnect from the current network
    by calling this primitive

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeDisconnectReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeDisconnectReq *) CsrPmemAlloc(sizeof(CsrWifiSmeDisconnectReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_DISCONNECT_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeDisconnectReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeDisconnectReq *msg__; \
        CsrWifiSmeDisconnectReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeDisconnectReqSend(src__, interfaceTag__) \
    CsrWifiSmeDisconnectReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeDisconnectCfmSend

  DESCRIPTION
    On reception of CSR_WIFI_SME_DISCONNECT_REQ the SME will perform a
    disconnect operation, sending a CsrWifiSmeMediaStatusInd with
    CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED and then call this primitive when
    disconnection is complete.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeDisconnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeDisconnectCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeDisconnectCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_DISCONNECT_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeDisconnectCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeDisconnectCfm *msg__; \
        CsrWifiSmeDisconnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeDisconnectCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeDisconnectCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeErrorIndSend

  DESCRIPTION
    Important error message indicating a error of some importance

  PARAMETERS
    queue        - Destination Task Queue
    errorMessage - Contains the error message.

*******************************************************************************/
#define CsrWifiSmeErrorIndCreate(msg__, dst__, src__, errorMessage__) \
    msg__ = (CsrWifiSmeErrorInd *) CsrPmemAlloc(sizeof(CsrWifiSmeErrorInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ERROR_IND, dst__, src__); \
    msg__->errorMessage = (errorMessage__);

#define CsrWifiSmeErrorIndSendTo(dst__, src__, errorMessage__) \
    { \
        CsrWifiSmeErrorInd *msg__; \
        CsrWifiSmeErrorIndCreate(msg__, dst__, src__, errorMessage__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeErrorIndSend(dst__, errorMessage__) \
    CsrWifiSmeErrorIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, errorMessage__)

/*******************************************************************************

  NAME
    CsrWifiSmeEventMaskSetReqSend

  DESCRIPTION
    The wireless manager application may register with the SME to receive
    notification of interesting events. Indications will be sent only if the
    wireless manager explicitly registers to be notified of that event.
    indMask is a bit mask of values defined in CsrWifiSmeIndicationsMask.

  PARAMETERS
    queue   - Message Source Task Queue (Cfm's will be sent to this Queue)
    indMask - Set mask with values from CsrWifiSmeIndications

*******************************************************************************/
#define CsrWifiSmeEventMaskSetReqCreate(msg__, dst__, src__, indMask__) \
    msg__ = (CsrWifiSmeEventMaskSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeEventMaskSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_EVENT_MASK_SET_REQ, dst__, src__); \
    msg__->indMask = (indMask__);

#define CsrWifiSmeEventMaskSetReqSendTo(dst__, src__, indMask__) \
    { \
        CsrWifiSmeEventMaskSetReq *msg__; \
        CsrWifiSmeEventMaskSetReqCreate(msg__, dst__, src__, indMask__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeEventMaskSetReqSend(src__, indMask__) \
    CsrWifiSmeEventMaskSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, indMask__)

/*******************************************************************************

  NAME
    CsrWifiSmeEventMaskSetCfmSend

  DESCRIPTION
    The SME calls the primitive to report the result of the request
    primitive.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeEventMaskSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeEventMaskSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeEventMaskSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_EVENT_MASK_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeEventMaskSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeEventMaskSetCfm *msg__; \
        CsrWifiSmeEventMaskSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeEventMaskSetCfmSend(dst__, status__) \
    CsrWifiSmeEventMaskSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the hostConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeHostConfigGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeHostConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeHostConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_HOST_CONFIG_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeHostConfigGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeHostConfigGetReq *msg__; \
        CsrWifiSmeHostConfigGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeHostConfigGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeHostConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    hostConfig   - Current host power state.

*******************************************************************************/
#define CsrWifiSmeHostConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, hostConfig__) \
    msg__ = (CsrWifiSmeHostConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeHostConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_HOST_CONFIG_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->hostConfig = (hostConfig__);

#define CsrWifiSmeHostConfigGetCfmSendTo(dst__, src__, interfaceTag__, status__, hostConfig__) \
    { \
        CsrWifiSmeHostConfigGetCfm *msg__; \
        CsrWifiSmeHostConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, hostConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeHostConfigGetCfmSend(dst__, interfaceTag__, status__, hostConfig__) \
    CsrWifiSmeHostConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, hostConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the hostConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface
    hostConfig   - Communicates a change of host power state (for example, on
                   mains power, on battery power etc) and of the periodicity of
                   traffic data

*******************************************************************************/
#define CsrWifiSmeHostConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, hostConfig__) \
    msg__ = (CsrWifiSmeHostConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeHostConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_HOST_CONFIG_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->hostConfig = (hostConfig__);

#define CsrWifiSmeHostConfigSetReqSendTo(dst__, src__, interfaceTag__, hostConfig__) \
    { \
        CsrWifiSmeHostConfigSetReq *msg__; \
        CsrWifiSmeHostConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, hostConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeHostConfigSetReqSend(src__, interfaceTag__, hostConfig__) \
    CsrWifiSmeHostConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, hostConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeHostConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeHostConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeHostConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_HOST_CONFIG_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeHostConfigSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeHostConfigSetCfm *msg__; \
        CsrWifiSmeHostConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeHostConfigSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeHostConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeIbssStationIndSend

  DESCRIPTION
    The SME will send this primitive to indicate that a station has joined or
    left the ad-hoc network.

  PARAMETERS
    queue       - Destination Task Queue
    address     - MAC address of the station that has joined or left
    isconnected - TRUE if the station joined, FALSE if the station left

*******************************************************************************/
#define CsrWifiSmeIbssStationIndCreate(msg__, dst__, src__, address__, isconnected__) \
    msg__ = (CsrWifiSmeIbssStationInd *) CsrPmemAlloc(sizeof(CsrWifiSmeIbssStationInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_IBSS_STATION_IND, dst__, src__); \
    msg__->address = (address__); \
    msg__->isconnected = (isconnected__);

#define CsrWifiSmeIbssStationIndSendTo(dst__, src__, address__, isconnected__) \
    { \
        CsrWifiSmeIbssStationInd *msg__; \
        CsrWifiSmeIbssStationIndCreate(msg__, dst__, src__, address__, isconnected__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeIbssStationIndSend(dst__, address__, isconnected__) \
    CsrWifiSmeIbssStationIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, address__, isconnected__)

/*******************************************************************************

  NAME
    CsrWifiSmeInfoIndSend

  DESCRIPTION
    Message indicating a some info about current activity. Mostly of interest
    in testing but may be useful in the field.

  PARAMETERS
    queue       - Destination Task Queue
    infoMessage - Contains the message.

*******************************************************************************/
#define CsrWifiSmeInfoIndCreate(msg__, dst__, src__, infoMessage__) \
    msg__ = (CsrWifiSmeInfoInd *) CsrPmemAlloc(sizeof(CsrWifiSmeInfoInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_INFO_IND, dst__, src__); \
    msg__->infoMessage = (infoMessage__);

#define CsrWifiSmeInfoIndSendTo(dst__, src__, infoMessage__) \
    { \
        CsrWifiSmeInfoInd *msg__; \
        CsrWifiSmeInfoIndCreate(msg__, dst__, src__, infoMessage__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeInfoIndSend(dst__, infoMessage__) \
    CsrWifiSmeInfoIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, infoMessage__)

/*******************************************************************************

  NAME
    CsrWifiSmeInterfaceCapabilityGetReqSend

  DESCRIPTION
    The Wireless Manager calls this primitive to ask the SME for the
    capabilities of the supported interfaces

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeInterfaceCapabilityGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeInterfaceCapabilityGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeInterfaceCapabilityGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_REQ, dst__, src__);

#define CsrWifiSmeInterfaceCapabilityGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeInterfaceCapabilityGetReq *msg__; \
        CsrWifiSmeInterfaceCapabilityGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeInterfaceCapabilityGetReqSend(src__) \
    CsrWifiSmeInterfaceCapabilityGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeInterfaceCapabilityGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue         - Destination Task Queue
    status        - Result of the request
    numInterfaces - Number of the interfaces supported
    capBitmap     - Points to the list of capabilities bitmaps provided for each
                    interface.
                    The bits represent the following capabilities:
                    -bits 7 to 4-Reserved
                    -bit 3-AMP
                    -bit 2-P2P
                    -bit 1-AP
                    -bit 0-STA

*******************************************************************************/
#define CsrWifiSmeInterfaceCapabilityGetCfmCreate(msg__, dst__, src__, status__, numInterfaces__, capBitmap__) \
    msg__ = (CsrWifiSmeInterfaceCapabilityGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeInterfaceCapabilityGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->numInterfaces = (numInterfaces__); \
    CsrMemCpy(msg__->capBitmap, (capBitmap__), sizeof(u8) * 2);

#define CsrWifiSmeInterfaceCapabilityGetCfmSendTo(dst__, src__, status__, numInterfaces__, capBitmap__) \
    { \
        CsrWifiSmeInterfaceCapabilityGetCfm *msg__; \
        CsrWifiSmeInterfaceCapabilityGetCfmCreate(msg__, dst__, src__, status__, numInterfaces__, capBitmap__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeInterfaceCapabilityGetCfmSend(dst__, status__, numInterfaces__, capBitmap__) \
    CsrWifiSmeInterfaceCapabilityGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, numInterfaces__, capBitmap__)

/*******************************************************************************

  NAME
    CsrWifiSmeKeyReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to add or remove
    keys that the chip should use for encryption of data.
    The interface allows the wireless manager application to add and remove
    keys according to the specified action.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface
    action       - The value of the CsrWifiSmeListAction parameter instructs the
                   driver to modify or provide the list of keys.
                   CSR_WIFI_SME_LIST_ACTION_GET is not supported here.
    key          - Key to be added or removed

*******************************************************************************/
#define CsrWifiSmeKeyReqCreate(msg__, dst__, src__, interfaceTag__, action__, key__) \
    msg__ = (CsrWifiSmeKeyReq *) CsrPmemAlloc(sizeof(CsrWifiSmeKeyReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_KEY_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->action = (action__); \
    msg__->key = (key__);

#define CsrWifiSmeKeyReqSendTo(dst__, src__, interfaceTag__, action__, key__) \
    { \
        CsrWifiSmeKeyReq *msg__; \
        CsrWifiSmeKeyReqCreate(msg__, dst__, src__, interfaceTag__, action__, key__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeKeyReqSend(src__, interfaceTag__, action__, key__) \
    CsrWifiSmeKeyReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, action__, key__)

/*******************************************************************************

  NAME
    CsrWifiSmeKeyCfmSend

  DESCRIPTION
    The SME calls the primitive to report the result of the request
    primitive.

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    action         - Action in the request
    keyType        - Type of the key added/deleted
    peerMacAddress - Peer MAC Address of the key added/deleted

*******************************************************************************/
#define CsrWifiSmeKeyCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, keyType__, peerMacAddress__) \
    msg__ = (CsrWifiSmeKeyCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeKeyCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_KEY_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->action = (action__); \
    msg__->keyType = (keyType__); \
    msg__->peerMacAddress = (peerMacAddress__);

#define CsrWifiSmeKeyCfmSendTo(dst__, src__, interfaceTag__, status__, action__, keyType__, peerMacAddress__) \
    { \
        CsrWifiSmeKeyCfm *msg__; \
        CsrWifiSmeKeyCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, keyType__, peerMacAddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeKeyCfmSend(dst__, interfaceTag__, status__, action__, keyType__, peerMacAddress__) \
    CsrWifiSmeKeyCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, action__, keyType__, peerMacAddress__)

/*******************************************************************************

  NAME
    CsrWifiSmeLinkQualityGetReqSend

  DESCRIPTION
    This primitive gets the value of the LinkQuality parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeLinkQualityGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeLinkQualityGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeLinkQualityGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_LINK_QUALITY_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeLinkQualityGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeLinkQualityGetReq *msg__; \
        CsrWifiSmeLinkQualityGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeLinkQualityGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeLinkQualityGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeLinkQualityGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    linkQuality  - Indicates the quality of the link

*******************************************************************************/
#define CsrWifiSmeLinkQualityGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, linkQuality__) \
    msg__ = (CsrWifiSmeLinkQualityGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeLinkQualityGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_LINK_QUALITY_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->linkQuality = (linkQuality__);

#define CsrWifiSmeLinkQualityGetCfmSendTo(dst__, src__, interfaceTag__, status__, linkQuality__) \
    { \
        CsrWifiSmeLinkQualityGetCfm *msg__; \
        CsrWifiSmeLinkQualityGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, linkQuality__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeLinkQualityGetCfmSend(dst__, interfaceTag__, status__, linkQuality__) \
    CsrWifiSmeLinkQualityGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, linkQuality__)

/*******************************************************************************

  NAME
    CsrWifiSmeMediaStatusIndSend

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it when a network connection is established, lost or has moved to
    another AP.

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   - Interface Identifier; unique identifier of an interface
    mediaStatus    - Indicates the media status
    connectionInfo - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_CONNECTED:
                     it points to the connection information for the new network
    disassocReason - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED:
                     if a disassociation has occurred it gives the reason of the
                     disassociation
    deauthReason   - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED:
                     if a deauthentication has occurred it gives the reason of
                     the deauthentication

*******************************************************************************/
#define CsrWifiSmeMediaStatusIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, connectionInfo__, disassocReason__, deauthReason__) \
    msg__ = (CsrWifiSmeMediaStatusInd *) CsrPmemAlloc(sizeof(CsrWifiSmeMediaStatusInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MEDIA_STATUS_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->mediaStatus = (mediaStatus__); \
    msg__->connectionInfo = (connectionInfo__); \
    msg__->disassocReason = (disassocReason__); \
    msg__->deauthReason = (deauthReason__);

#define CsrWifiSmeMediaStatusIndSendTo(dst__, src__, interfaceTag__, mediaStatus__, connectionInfo__, disassocReason__, deauthReason__) \
    { \
        CsrWifiSmeMediaStatusInd *msg__; \
        CsrWifiSmeMediaStatusIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, connectionInfo__, disassocReason__, deauthReason__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMediaStatusIndSend(dst__, interfaceTag__, mediaStatus__, connectionInfo__, disassocReason__, deauthReason__) \
    CsrWifiSmeMediaStatusIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, mediaStatus__, connectionInfo__, disassocReason__, deauthReason__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the MibConfig parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeMibConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeMibConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMibConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmeMibConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeMibConfigGetReq *msg__; \
        CsrWifiSmeMibConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibConfigGetReqSend(src__) \
    CsrWifiSmeMibConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue     - Destination Task Queue
    status    - Reports the result of the request
    mibConfig - Reports various IEEE 802.11 attributes as currently configured

*******************************************************************************/
#define CsrWifiSmeMibConfigGetCfmCreate(msg__, dst__, src__, status__, mibConfig__) \
    msg__ = (CsrWifiSmeMibConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMibConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->mibConfig = (mibConfig__);

#define CsrWifiSmeMibConfigGetCfmSendTo(dst__, src__, status__, mibConfig__) \
    { \
        CsrWifiSmeMibConfigGetCfm *msg__; \
        CsrWifiSmeMibConfigGetCfmCreate(msg__, dst__, src__, status__, mibConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibConfigGetCfmSend(dst__, status__, mibConfig__) \
    CsrWifiSmeMibConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, mibConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the MibConfig parameter.

  PARAMETERS
    queue     - Message Source Task Queue (Cfm's will be sent to this Queue)
    mibConfig - Conveys the desired value of various IEEE 802.11 attributes as
                currently configured

*******************************************************************************/
#define CsrWifiSmeMibConfigSetReqCreate(msg__, dst__, src__, mibConfig__) \
    msg__ = (CsrWifiSmeMibConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMibConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_CONFIG_SET_REQ, dst__, src__); \
    msg__->mibConfig = (mibConfig__);

#define CsrWifiSmeMibConfigSetReqSendTo(dst__, src__, mibConfig__) \
    { \
        CsrWifiSmeMibConfigSetReq *msg__; \
        CsrWifiSmeMibConfigSetReqCreate(msg__, dst__, src__, mibConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibConfigSetReqSend(src__, mibConfig__) \
    CsrWifiSmeMibConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, mibConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeMibConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeMibConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMibConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeMibConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeMibConfigSetCfm *msg__; \
        CsrWifiSmeMibConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibConfigSetCfmSend(dst__, status__) \
    CsrWifiSmeMibConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetCfmSend

  DESCRIPTION
    The SME calls this primitive to return the requested MIB variable values.

  PARAMETERS
    queue              - Destination Task Queue
    status             - Reports the result of the request
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names and values of the MIB variables requested

*******************************************************************************/
#define CsrWifiSmeMibGetCfmCreate(msg__, dst__, src__, status__, mibAttributeLength__, mibAttribute__) \
    msg__ = (CsrWifiSmeMibGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMibGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->mibAttributeLength = (mibAttributeLength__); \
    msg__->mibAttribute = (mibAttribute__);

#define CsrWifiSmeMibGetCfmSendTo(dst__, src__, status__, mibAttributeLength__, mibAttribute__) \
    { \
        CsrWifiSmeMibGetCfm *msg__; \
        CsrWifiSmeMibGetCfmCreate(msg__, dst__, src__, status__, mibAttributeLength__, mibAttribute__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibGetCfmSend(dst__, status__, mibAttributeLength__, mibAttribute__) \
    CsrWifiSmeMibGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, mibAttributeLength__, mibAttribute__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetNextReqSend

  DESCRIPTION
    To read a sequence of MIB parameters, for example a table, call this
    primitive to find the name of the next MIB variable

  PARAMETERS
    queue              - Message Source Task Queue (Cfm's will be sent to this Queue)
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to a VarBind or VarBindList containing the
                         name(s) of the MIB variable(s) to search from.

*******************************************************************************/
#define CsrWifiSmeMibGetNextReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__) \
    msg__ = (CsrWifiSmeMibGetNextReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMibGetNextReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_GET_NEXT_REQ, dst__, src__); \
    msg__->mibAttributeLength = (mibAttributeLength__); \
    msg__->mibAttribute = (mibAttribute__);

#define CsrWifiSmeMibGetNextReqSendTo(dst__, src__, mibAttributeLength__, mibAttribute__) \
    { \
        CsrWifiSmeMibGetNextReq *msg__; \
        CsrWifiSmeMibGetNextReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibGetNextReqSend(src__, mibAttributeLength__, mibAttribute__) \
    CsrWifiSmeMibGetNextReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, mibAttributeLength__, mibAttribute__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetNextCfmSend

  DESCRIPTION
    The SME calls this primitive to return the requested MIB name(s).
    The wireless manager application can then read the value of the MIB
    variable using CSR_WIFI_SME_MIB_GET_REQ, using the names provided.

  PARAMETERS
    queue              - Destination Task Queue
    status             - Reports the result of the request
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to a VarBind or VarBindList containing the
                         name(s) of the MIB variable(s) lexicographically
                         following the name(s) given in the request

*******************************************************************************/
#define CsrWifiSmeMibGetNextCfmCreate(msg__, dst__, src__, status__, mibAttributeLength__, mibAttribute__) \
    msg__ = (CsrWifiSmeMibGetNextCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMibGetNextCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_GET_NEXT_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->mibAttributeLength = (mibAttributeLength__); \
    msg__->mibAttribute = (mibAttribute__);

#define CsrWifiSmeMibGetNextCfmSendTo(dst__, src__, status__, mibAttributeLength__, mibAttribute__) \
    { \
        CsrWifiSmeMibGetNextCfm *msg__; \
        CsrWifiSmeMibGetNextCfmCreate(msg__, dst__, src__, status__, mibAttributeLength__, mibAttribute__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibGetNextCfmSend(dst__, status__, mibAttributeLength__, mibAttribute__) \
    CsrWifiSmeMibGetNextCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, mibAttributeLength__, mibAttribute__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to retrieve one or
    more MIB variables.

  PARAMETERS
    queue              - Message Source Task Queue (Cfm's will be sent to this Queue)
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names of the MIB variables to be retrieved

*******************************************************************************/
#define CsrWifiSmeMibGetReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__) \
    msg__ = (CsrWifiSmeMibGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMibGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_GET_REQ, dst__, src__); \
    msg__->mibAttributeLength = (mibAttributeLength__); \
    msg__->mibAttribute = (mibAttribute__);

#define CsrWifiSmeMibGetReqSendTo(dst__, src__, mibAttributeLength__, mibAttribute__) \
    { \
        CsrWifiSmeMibGetReq *msg__; \
        CsrWifiSmeMibGetReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibGetReqSend(src__, mibAttributeLength__, mibAttribute__) \
    CsrWifiSmeMibGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, mibAttributeLength__, mibAttribute__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibSetReqSend

  DESCRIPTION
    The SME provides raw access to the MIB on the chip, which may be used by
    some configuration or diagnostic utilities, but is not normally needed by
    the wireless manager application.
    The MIB access functions use BER encoded names (OID) of the MIB
    parameters and BER encoded values, as described in the chip Host
    Interface Protocol Specification.
    The MIB parameters are described in 'Wi-Fi 5.0.0 Management Information
    Base Reference Guide'.
    The wireless manager application calls this primitive to set one or more
    MIB variables

  PARAMETERS
    queue              - Message Source Task Queue (Cfm's will be sent to this Queue)
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names and values of the MIB variables to set

*******************************************************************************/
#define CsrWifiSmeMibSetReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__) \
    msg__ = (CsrWifiSmeMibSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMibSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_SET_REQ, dst__, src__); \
    msg__->mibAttributeLength = (mibAttributeLength__); \
    msg__->mibAttribute = (mibAttribute__);

#define CsrWifiSmeMibSetReqSendTo(dst__, src__, mibAttributeLength__, mibAttribute__) \
    { \
        CsrWifiSmeMibSetReq *msg__; \
        CsrWifiSmeMibSetReqCreate(msg__, dst__, src__, mibAttributeLength__, mibAttribute__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibSetReqSend(src__, mibAttributeLength__, mibAttribute__) \
    CsrWifiSmeMibSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, mibAttributeLength__, mibAttribute__)

/*******************************************************************************

  NAME
    CsrWifiSmeMibSetCfmSend

  DESCRIPTION
    The SME calls the primitive to report the result of the set primitive.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeMibSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeMibSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMibSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIB_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeMibSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeMibSetCfm *msg__; \
        CsrWifiSmeMibSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMibSetCfmSend(dst__, status__) \
    CsrWifiSmeMibSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeMicFailureIndSend

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever the chip firmware reports a MIC failure.

  PARAMETERS
    queue         - Destination Task Queue
    interfaceTag  - Interface Identifier; unique identifier of an interface
    secondFailure - TRUE if this indication is for a second failure in 60
                    seconds
    count         - The number of MIC failure events since the connection was
                    established
    address       - MAC address of the transmitter that caused the MIC failure
    keyType       - Type of key for which the failure occurred

*******************************************************************************/
#define CsrWifiSmeMicFailureIndCreate(msg__, dst__, src__, interfaceTag__, secondFailure__, count__, address__, keyType__) \
    msg__ = (CsrWifiSmeMicFailureInd *) CsrPmemAlloc(sizeof(CsrWifiSmeMicFailureInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MIC_FAILURE_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->secondFailure = (secondFailure__); \
    msg__->count = (count__); \
    msg__->address = (address__); \
    msg__->keyType = (keyType__);

#define CsrWifiSmeMicFailureIndSendTo(dst__, src__, interfaceTag__, secondFailure__, count__, address__, keyType__) \
    { \
        CsrWifiSmeMicFailureInd *msg__; \
        CsrWifiSmeMicFailureIndCreate(msg__, dst__, src__, interfaceTag__, secondFailure__, count__, address__, keyType__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMicFailureIndSend(dst__, interfaceTag__, secondFailure__, count__, address__, keyType__) \
    CsrWifiSmeMicFailureIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, secondFailure__, count__, address__, keyType__)

/*******************************************************************************

  NAME
    CsrWifiSmeMulticastAddressReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to specify the
    multicast addresses which the chip should recognise. The interface allows
    the wireless manager application to query, add, remove and flush the
    multicast addresses for the network interface according to the specified
    action.

  PARAMETERS
    queue             - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag      - Interface Identifier; unique identifier of an interface
    action            - The value of the CsrWifiSmeListAction parameter
                        instructs the driver to modify or provide the list of
                        MAC addresses.
    setAddressesCount - Number of MAC addresses sent with the primitive
    setAddresses      - Pointer to the list of MAC Addresses sent with the
                        primitive, set to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmeMulticastAddressReqCreate(msg__, dst__, src__, interfaceTag__, action__, setAddressesCount__, setAddresses__) \
    msg__ = (CsrWifiSmeMulticastAddressReq *) CsrPmemAlloc(sizeof(CsrWifiSmeMulticastAddressReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MULTICAST_ADDRESS_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->action = (action__); \
    msg__->setAddressesCount = (setAddressesCount__); \
    msg__->setAddresses = (setAddresses__);

#define CsrWifiSmeMulticastAddressReqSendTo(dst__, src__, interfaceTag__, action__, setAddressesCount__, setAddresses__) \
    { \
        CsrWifiSmeMulticastAddressReq *msg__; \
        CsrWifiSmeMulticastAddressReqCreate(msg__, dst__, src__, interfaceTag__, action__, setAddressesCount__, setAddresses__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMulticastAddressReqSend(src__, interfaceTag__, action__, setAddressesCount__, setAddresses__) \
    CsrWifiSmeMulticastAddressReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, action__, setAddressesCount__, setAddresses__)

/*******************************************************************************

  NAME
    CsrWifiSmeMulticastAddressCfmSend

  DESCRIPTION
    The SME will call this primitive when the operation is complete. For a
    GET action, this primitive reports the current list of MAC addresses.

  PARAMETERS
    queue             - Destination Task Queue
    interfaceTag      - Interface Identifier; unique identifier of an interface
    status            - Reports the result of the request
    action            - Action in the request
    getAddressesCount - This parameter is only relevant if action is
                        CSR_WIFI_SME_LIST_ACTION_GET:
                        number of MAC addresses sent with the primitive
    getAddresses      - Pointer to the list of MAC Addresses sent with the
                        primitive, set to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmeMulticastAddressCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getAddressesCount__, getAddresses__) \
    msg__ = (CsrWifiSmeMulticastAddressCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeMulticastAddressCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_MULTICAST_ADDRESS_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->action = (action__); \
    msg__->getAddressesCount = (getAddressesCount__); \
    msg__->getAddresses = (getAddresses__);

#define CsrWifiSmeMulticastAddressCfmSendTo(dst__, src__, interfaceTag__, status__, action__, getAddressesCount__, getAddresses__) \
    { \
        CsrWifiSmeMulticastAddressCfm *msg__; \
        CsrWifiSmeMulticastAddressCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getAddressesCount__, getAddresses__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeMulticastAddressCfmSend(dst__, interfaceTag__, status__, action__, getAddressesCount__, getAddresses__) \
    CsrWifiSmeMulticastAddressCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, action__, getAddressesCount__, getAddresses__)

/*******************************************************************************

  NAME
    CsrWifiSmePacketFilterSetReqSend

  DESCRIPTION
    The wireless manager application should call this primitive to enable or
    disable filtering of broadcast packets: uninteresting broadcast packets
    will be dropped by the Wi-Fi chip, instead of passing them up to the
    host.
    This has the advantage of saving power in the host application processor
    as it removes the need to process unwanted packets.
    All broadcast packets are filtered according to the filter and the filter
    mode provided, except ARP packets, which are filtered using
    arpFilterAddress.
    Filters are not cumulative: only the parameters specified in the most
    recent successful request are significant.
    For more information, see 'UniFi Firmware API Specification'.

  PARAMETERS
    queue            - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag     - Interface Identifier; unique identifier of an interface
    filterLength     - Length of the filter in bytes.
                       filterLength=0 disables the filter previously set
    filter           - Points to the first byte of the filter provided, if any.
                       This shall include zero or more instance of the
                       information elements of one of these types
                         * Traffic Classification (TCLAS) elements
                         * WMM-SA TCLAS elements
    mode             - Specifies whether the filter selects or excludes packets
                       matching the filter
    arpFilterAddress - IPv4 address to be used for filtering the ARP packets.
                         * If the specified address is the IPv4 broadcast address
                           (255.255.255.255), all ARP packets are reported to the
                           host,
                         * If the specified address is NOT the IPv4 broadcast
                           address, only ARP packets with the specified address in
                           the Source or Target Protocol Address fields are reported
                           to the host

*******************************************************************************/
#define CsrWifiSmePacketFilterSetReqCreate(msg__, dst__, src__, interfaceTag__, filterLength__, filter__, mode__, arpFilterAddress__) \
    msg__ = (CsrWifiSmePacketFilterSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmePacketFilterSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PACKET_FILTER_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->filterLength = (filterLength__); \
    msg__->filter = (filter__); \
    msg__->mode = (mode__); \
    msg__->arpFilterAddress = (arpFilterAddress__);

#define CsrWifiSmePacketFilterSetReqSendTo(dst__, src__, interfaceTag__, filterLength__, filter__, mode__, arpFilterAddress__) \
    { \
        CsrWifiSmePacketFilterSetReq *msg__; \
        CsrWifiSmePacketFilterSetReqCreate(msg__, dst__, src__, interfaceTag__, filterLength__, filter__, mode__, arpFilterAddress__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePacketFilterSetReqSend(src__, interfaceTag__, filterLength__, filter__, mode__, arpFilterAddress__) \
    CsrWifiSmePacketFilterSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, filterLength__, filter__, mode__, arpFilterAddress__)

/*******************************************************************************

  NAME
    CsrWifiSmePacketFilterSetCfmSend

  DESCRIPTION
    The SME calls the primitive to report the result of the set primitive.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmePacketFilterSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmePacketFilterSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmePacketFilterSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PACKET_FILTER_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmePacketFilterSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmePacketFilterSetCfm *msg__; \
        CsrWifiSmePacketFilterSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePacketFilterSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmePacketFilterSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmePermanentMacAddressGetReqSend

  DESCRIPTION
    This primitive retrieves the MAC address stored in EEPROM

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmePermanentMacAddressGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmePermanentMacAddressGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmePermanentMacAddressGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_REQ, dst__, src__);

#define CsrWifiSmePermanentMacAddressGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmePermanentMacAddressGetReq *msg__; \
        CsrWifiSmePermanentMacAddressGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePermanentMacAddressGetReqSend(src__) \
    CsrWifiSmePermanentMacAddressGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmePermanentMacAddressGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue               - Destination Task Queue
    status              - Reports the result of the request
    permanentMacAddress - MAC address stored in the EEPROM

*******************************************************************************/
#define CsrWifiSmePermanentMacAddressGetCfmCreate(msg__, dst__, src__, status__, permanentMacAddress__) \
    msg__ = (CsrWifiSmePermanentMacAddressGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmePermanentMacAddressGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->permanentMacAddress = (permanentMacAddress__);

#define CsrWifiSmePermanentMacAddressGetCfmSendTo(dst__, src__, status__, permanentMacAddress__) \
    { \
        CsrWifiSmePermanentMacAddressGetCfm *msg__; \
        CsrWifiSmePermanentMacAddressGetCfmCreate(msg__, dst__, src__, status__, permanentMacAddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePermanentMacAddressGetCfmSend(dst__, status__, permanentMacAddress__) \
    CsrWifiSmePermanentMacAddressGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, permanentMacAddress__)

/*******************************************************************************

  NAME
    CsrWifiSmePmkidCandidateListIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it when a new network supporting preauthentication and/or PMK
    caching is seen.

  PARAMETERS
    queue                - Destination Task Queue
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    pmkidCandidatesCount - Number of PMKID candidates provided
    pmkidCandidates      - Points to the first PMKID candidate

*******************************************************************************/
#define CsrWifiSmePmkidCandidateListIndCreate(msg__, dst__, src__, interfaceTag__, pmkidCandidatesCount__, pmkidCandidates__) \
    msg__ = (CsrWifiSmePmkidCandidateListInd *) CsrPmemAlloc(sizeof(CsrWifiSmePmkidCandidateListInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PMKID_CANDIDATE_LIST_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->pmkidCandidatesCount = (pmkidCandidatesCount__); \
    msg__->pmkidCandidates = (pmkidCandidates__);

#define CsrWifiSmePmkidCandidateListIndSendTo(dst__, src__, interfaceTag__, pmkidCandidatesCount__, pmkidCandidates__) \
    { \
        CsrWifiSmePmkidCandidateListInd *msg__; \
        CsrWifiSmePmkidCandidateListIndCreate(msg__, dst__, src__, interfaceTag__, pmkidCandidatesCount__, pmkidCandidates__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePmkidCandidateListIndSend(dst__, interfaceTag__, pmkidCandidatesCount__, pmkidCandidates__) \
    CsrWifiSmePmkidCandidateListIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, pmkidCandidatesCount__, pmkidCandidates__)

/*******************************************************************************

  NAME
    CsrWifiSmePmkidReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to request an
    operation on the SME PMKID list.
    The action argument specifies the operation to perform.
    When the connection is complete, the wireless manager application may
    then send and receive EAPOL packets to complete WPA or WPA2
    authentication if appropriate.
    The wireless manager application can then pass the resulting encryption
    keys using this primitive.

  PARAMETERS
    queue          - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag   - Interface Identifier; unique identifier of an interface
    action         - The value of the CsrWifiSmeListAction parameter instructs
                     the driver to modify or provide the list of PMKIDs.
    setPmkidsCount - Number of PMKIDs sent with the primitive
    setPmkids      - Pointer to the list of PMKIDs sent with the primitive, set
                     to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmePmkidReqCreate(msg__, dst__, src__, interfaceTag__, action__, setPmkidsCount__, setPmkids__) \
    msg__ = (CsrWifiSmePmkidReq *) CsrPmemAlloc(sizeof(CsrWifiSmePmkidReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PMKID_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->action = (action__); \
    msg__->setPmkidsCount = (setPmkidsCount__); \
    msg__->setPmkids = (setPmkids__);

#define CsrWifiSmePmkidReqSendTo(dst__, src__, interfaceTag__, action__, setPmkidsCount__, setPmkids__) \
    { \
        CsrWifiSmePmkidReq *msg__; \
        CsrWifiSmePmkidReqCreate(msg__, dst__, src__, interfaceTag__, action__, setPmkidsCount__, setPmkids__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePmkidReqSend(src__, interfaceTag__, action__, setPmkidsCount__, setPmkids__) \
    CsrWifiSmePmkidReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, action__, setPmkidsCount__, setPmkids__)

/*******************************************************************************

  NAME
    CsrWifiSmePmkidCfmSend

  DESCRIPTION
    The SME will call this primitive when the operation is complete. For a
    GET action, this primitive reports the current list of PMKIDs

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    action         - Action in the request
    getPmkidsCount - This parameter is only relevant if action is
                     CSR_WIFI_SME_LIST_ACTION_GET:
                     number of PMKIDs sent with the primitive
    getPmkids      - Pointer to the list of PMKIDs sent with the primitive, set
                     to NULL if none is sent.

*******************************************************************************/
#define CsrWifiSmePmkidCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getPmkidsCount__, getPmkids__) \
    msg__ = (CsrWifiSmePmkidCfm *) CsrPmemAlloc(sizeof(CsrWifiSmePmkidCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_PMKID_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->action = (action__); \
    msg__->getPmkidsCount = (getPmkidsCount__); \
    msg__->getPmkids = (getPmkids__);

#define CsrWifiSmePmkidCfmSendTo(dst__, src__, interfaceTag__, status__, action__, getPmkidsCount__, getPmkids__) \
    { \
        CsrWifiSmePmkidCfm *msg__; \
        CsrWifiSmePmkidCfmCreate(msg__, dst__, src__, interfaceTag__, status__, action__, getPmkidsCount__, getPmkids__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePmkidCfmSend(dst__, interfaceTag__, status__, action__, getPmkidsCount__, getPmkids__) \
    CsrWifiSmePmkidCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, action__, getPmkidsCount__, getPmkids__)

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the PowerConfig parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmePowerConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmePowerConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmePowerConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_POWER_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmePowerConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmePowerConfigGetReq *msg__; \
        CsrWifiSmePowerConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePowerConfigGetReqSend(src__) \
    CsrWifiSmePowerConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue       - Destination Task Queue
    status      - Reports the result of the request
    powerConfig - Returns the current parameters for the power configuration of
                  the firmware

*******************************************************************************/
#define CsrWifiSmePowerConfigGetCfmCreate(msg__, dst__, src__, status__, powerConfig__) \
    msg__ = (CsrWifiSmePowerConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmePowerConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_POWER_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->powerConfig = (powerConfig__);

#define CsrWifiSmePowerConfigGetCfmSendTo(dst__, src__, status__, powerConfig__) \
    { \
        CsrWifiSmePowerConfigGetCfm *msg__; \
        CsrWifiSmePowerConfigGetCfmCreate(msg__, dst__, src__, status__, powerConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePowerConfigGetCfmSend(dst__, status__, powerConfig__) \
    CsrWifiSmePowerConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, powerConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the PowerConfig parameter.

  PARAMETERS
    queue       - Message Source Task Queue (Cfm's will be sent to this Queue)
    powerConfig - Power saving configuration

*******************************************************************************/
#define CsrWifiSmePowerConfigSetReqCreate(msg__, dst__, src__, powerConfig__) \
    msg__ = (CsrWifiSmePowerConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmePowerConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_POWER_CONFIG_SET_REQ, dst__, src__); \
    msg__->powerConfig = (powerConfig__);

#define CsrWifiSmePowerConfigSetReqSendTo(dst__, src__, powerConfig__) \
    { \
        CsrWifiSmePowerConfigSetReq *msg__; \
        CsrWifiSmePowerConfigSetReqCreate(msg__, dst__, src__, powerConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePowerConfigSetReqSend(src__, powerConfig__) \
    CsrWifiSmePowerConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, powerConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmePowerConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmePowerConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmePowerConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_POWER_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmePowerConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmePowerConfigSetCfm *msg__; \
        CsrWifiSmePowerConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmePowerConfigSetCfmSend(dst__, status__) \
    CsrWifiSmePowerConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomainInfoGetReqSend

  DESCRIPTION
    This primitive gets the value of the RegulatoryDomainInfo parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeRegulatoryDomainInfoGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeRegulatoryDomainInfoGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeRegulatoryDomainInfoGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_REQ, dst__, src__);

#define CsrWifiSmeRegulatoryDomainInfoGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeRegulatoryDomainInfoGetReq *msg__; \
        CsrWifiSmeRegulatoryDomainInfoGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRegulatoryDomainInfoGetReqSend(src__) \
    CsrWifiSmeRegulatoryDomainInfoGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomainInfoGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue      - Destination Task Queue
    status     - Reports the result of the request
    regDomInfo - Reports information and state related to regulatory domain
                 operation.

*******************************************************************************/
#define CsrWifiSmeRegulatoryDomainInfoGetCfmCreate(msg__, dst__, src__, status__, regDomInfo__) \
    msg__ = (CsrWifiSmeRegulatoryDomainInfoGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeRegulatoryDomainInfoGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->regDomInfo = (regDomInfo__);

#define CsrWifiSmeRegulatoryDomainInfoGetCfmSendTo(dst__, src__, status__, regDomInfo__) \
    { \
        CsrWifiSmeRegulatoryDomainInfoGetCfm *msg__; \
        CsrWifiSmeRegulatoryDomainInfoGetCfmCreate(msg__, dst__, src__, status__, regDomInfo__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRegulatoryDomainInfoGetCfmSend(dst__, status__, regDomInfo__) \
    CsrWifiSmeRegulatoryDomainInfoGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, regDomInfo__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamCompleteIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it completes an attempt to roam to an AP. If the roam
    attempt was successful, status will be set to CSR_WIFI_SME_SUCCESS,
    otherwise it shall be set to the appropriate error code.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the roaming procedure

*******************************************************************************/
#define CsrWifiSmeRoamCompleteIndCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeRoamCompleteInd *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamCompleteInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAM_COMPLETE_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeRoamCompleteIndSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeRoamCompleteInd *msg__; \
        CsrWifiSmeRoamCompleteIndCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamCompleteIndSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeRoamCompleteIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamStartIndSend

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it begins an attempt to roam to an AP.
    If the wireless manager application connect request specified the SSID
    and the BSSID was set to the broadcast address (0xFF 0xFF 0xFF 0xFF 0xFF
    0xFF), the SME monitors the signal quality and maintains a list of
    candidates to roam to. When the signal quality of the current connection
    falls below a threshold, and there is a candidate with better quality,
    the SME will attempt to the candidate AP.
    If the roaming procedure succeeds, the SME will also issue a Media
    Connect indication to inform the wireless manager application of the
    change.
    NOTE: to prevent the SME from initiating roaming the WMA must specify the
    BSSID in the connection request; this forces the SME to connect only to
    that AP.
    The wireless manager application can obtain statistics for roaming
    purposes using CSR_WIFI_SME_CONNECTION_QUALITY_IND and
    CSR_WIFI_SME_CONNECTION_STATS_GET_REQ.
    When the wireless manager application wishes to roam to another AP, it
    must issue a connection request specifying the BSSID of the desired AP.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    roamReason   - Indicates the reason for starting the roaming procedure
    reason80211  - Indicates the reason for deauthentication or disassociation

*******************************************************************************/
#define CsrWifiSmeRoamStartIndCreate(msg__, dst__, src__, interfaceTag__, roamReason__, reason80211__) \
    msg__ = (CsrWifiSmeRoamStartInd *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamStartInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAM_START_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->roamReason = (roamReason__); \
    msg__->reason80211 = (reason80211__);

#define CsrWifiSmeRoamStartIndSendTo(dst__, src__, interfaceTag__, roamReason__, reason80211__) \
    { \
        CsrWifiSmeRoamStartInd *msg__; \
        CsrWifiSmeRoamStartIndCreate(msg__, dst__, src__, interfaceTag__, roamReason__, reason80211__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamStartIndSend(dst__, interfaceTag__, roamReason__, reason80211__) \
    CsrWifiSmeRoamStartIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, roamReason__, reason80211__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the RoamingConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeRoamingConfigGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeRoamingConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamingConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAMING_CONFIG_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeRoamingConfigGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeRoamingConfigGetReq *msg__; \
        CsrWifiSmeRoamingConfigGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamingConfigGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeRoamingConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue         - Destination Task Queue
    interfaceTag  - Interface Identifier; unique identifier of an interface
    status        - Reports the result of the request
    roamingConfig - Reports the roaming behaviour of the driver and firmware

*******************************************************************************/
#define CsrWifiSmeRoamingConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, roamingConfig__) \
    msg__ = (CsrWifiSmeRoamingConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamingConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAMING_CONFIG_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->roamingConfig = (roamingConfig__);

#define CsrWifiSmeRoamingConfigGetCfmSendTo(dst__, src__, interfaceTag__, status__, roamingConfig__) \
    { \
        CsrWifiSmeRoamingConfigGetCfm *msg__; \
        CsrWifiSmeRoamingConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, roamingConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamingConfigGetCfmSend(dst__, interfaceTag__, status__, roamingConfig__) \
    CsrWifiSmeRoamingConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, roamingConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the RoamingConfig parameter.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag  - Interface Identifier; unique identifier of an interface
    roamingConfig - Desired roaming behaviour values

*******************************************************************************/
#define CsrWifiSmeRoamingConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, roamingConfig__) \
    msg__ = (CsrWifiSmeRoamingConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamingConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAMING_CONFIG_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->roamingConfig = (roamingConfig__);

#define CsrWifiSmeRoamingConfigSetReqSendTo(dst__, src__, interfaceTag__, roamingConfig__) \
    { \
        CsrWifiSmeRoamingConfigSetReq *msg__; \
        CsrWifiSmeRoamingConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, roamingConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamingConfigSetReqSend(src__, interfaceTag__, roamingConfig__) \
    CsrWifiSmeRoamingConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, roamingConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigSetCfmSend

  DESCRIPTION
    This primitive sets the value of the RoamingConfig parameter.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeRoamingConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeRoamingConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeRoamingConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_ROAMING_CONFIG_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeRoamingConfigSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeRoamingConfigSetCfm *msg__; \
        CsrWifiSmeRoamingConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeRoamingConfigSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeRoamingConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the ScanConfig parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeScanConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeScanConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeScanConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmeScanConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeScanConfigGetReq *msg__; \
        CsrWifiSmeScanConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanConfigGetReqSend(src__) \
    CsrWifiSmeScanConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue      - Destination Task Queue
    status     - Reports the result of the request
    scanConfig - Returns the current parameters for the autonomous scanning
                 behaviour of the firmware

*******************************************************************************/
#define CsrWifiSmeScanConfigGetCfmCreate(msg__, dst__, src__, status__, scanConfig__) \
    msg__ = (CsrWifiSmeScanConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeScanConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->scanConfig = (scanConfig__);

#define CsrWifiSmeScanConfigGetCfmSendTo(dst__, src__, status__, scanConfig__) \
    { \
        CsrWifiSmeScanConfigGetCfm *msg__; \
        CsrWifiSmeScanConfigGetCfmCreate(msg__, dst__, src__, status__, scanConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanConfigGetCfmSend(dst__, status__, scanConfig__) \
    CsrWifiSmeScanConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, scanConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the ScanConfig parameter.
    The SME normally configures the firmware to perform autonomous scanning
    without involving the host.
    The firmware passes beacon / probe response or indicates loss of beacon
    on certain changes of state, for example:
      * A new AP is seen for the first time
      * An AP is no longer visible
      * The signal strength of an AP changes by more than a certain amount, as
        configured by the thresholds in the scanConfig parameter
    In addition to the autonomous scan, the wireless manager application may
    request a scan at any time using CSR_WIFI_SME_SCAN_FULL_REQ.

  PARAMETERS
    queue      - Message Source Task Queue (Cfm's will be sent to this Queue)
    scanConfig - Reports the configuration for the autonomous scanning behaviour
                 of the firmware

*******************************************************************************/
#define CsrWifiSmeScanConfigSetReqCreate(msg__, dst__, src__, scanConfig__) \
    msg__ = (CsrWifiSmeScanConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeScanConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_CONFIG_SET_REQ, dst__, src__); \
    msg__->scanConfig = (scanConfig__);

#define CsrWifiSmeScanConfigSetReqSendTo(dst__, src__, scanConfig__) \
    { \
        CsrWifiSmeScanConfigSetReq *msg__; \
        CsrWifiSmeScanConfigSetReqCreate(msg__, dst__, src__, scanConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanConfigSetReqSend(src__, scanConfig__) \
    CsrWifiSmeScanConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, scanConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeScanConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeScanConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeScanConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeScanConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeScanConfigSetCfm *msg__; \
        CsrWifiSmeScanConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanConfigSetCfmSend(dst__, status__) \
    CsrWifiSmeScanConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanFullReqSend

  DESCRIPTION
    The wireless manager application should call this primitive to request a
    full scan.
    Channels are scanned actively or passively according to the requirement
    set by regulatory domain.
    If the SME receives this primitive while a full scan is going on, the new
    request is buffered and it will be served after the current full scan is
    completed.

  PARAMETERS
    queue            - Message Source Task Queue (Cfm's will be sent to this Queue)
    ssidCount        - Number of SSIDs provided.
                       If it is 0, the SME will attempt to detect any network
    ssid             - Points to the first SSID provided, if any.
    bssid            - BSS identifier.
                       If it is equal to FF-FF-FF-FF-FF, the SME will listen for
                       messages from any BSS.
                       If it is different from FF-FF-FF-FF-FF and any SSID is
                       provided, one SSID must match the network of the BSS.
    forceScan        - Forces the scan even if the SME is in a state which would
                       normally prevent it (e.g. autonomous scan is running).
    bssType          - Type of BSS to scan for
    scanType         - Type of scan to perform
    channelListCount - Number of channels provided.
                       If it is 0, the SME will initiate a scan of all the
                       supported channels that are permitted by the current
                       regulatory domain.
    channelList      - Points to the first channel , or NULL if channelListCount
                       is zero.
    probeIeLength    - Length of the information element in bytes to be sent
                       with the probe message.
    probeIe          - Points to the first byte of the information element to be
                       sent with the probe message.

*******************************************************************************/
#define CsrWifiSmeScanFullReqCreate(msg__, dst__, src__, ssidCount__, ssid__, bssid__, forceScan__, bssType__, scanType__, channelListCount__, channelList__, probeIeLength__, probeIe__) \
    msg__ = (CsrWifiSmeScanFullReq *) CsrPmemAlloc(sizeof(CsrWifiSmeScanFullReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_FULL_REQ, dst__, src__); \
    msg__->ssidCount = (ssidCount__); \
    msg__->ssid = (ssid__); \
    msg__->bssid = (bssid__); \
    msg__->forceScan = (forceScan__); \
    msg__->bssType = (bssType__); \
    msg__->scanType = (scanType__); \
    msg__->channelListCount = (channelListCount__); \
    msg__->channelList = (channelList__); \
    msg__->probeIeLength = (probeIeLength__); \
    msg__->probeIe = (probeIe__);

#define CsrWifiSmeScanFullReqSendTo(dst__, src__, ssidCount__, ssid__, bssid__, forceScan__, bssType__, scanType__, channelListCount__, channelList__, probeIeLength__, probeIe__) \
    { \
        CsrWifiSmeScanFullReq *msg__; \
        CsrWifiSmeScanFullReqCreate(msg__, dst__, src__, ssidCount__, ssid__, bssid__, forceScan__, bssType__, scanType__, channelListCount__, channelList__, probeIeLength__, probeIe__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanFullReqSend(src__, ssidCount__, ssid__, bssid__, forceScan__, bssType__, scanType__, channelListCount__, channelList__, probeIeLength__, probeIe__) \
    CsrWifiSmeScanFullReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, ssidCount__, ssid__, bssid__, forceScan__, bssType__, scanType__, channelListCount__, channelList__, probeIeLength__, probeIe__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanFullCfmSend

  DESCRIPTION
    The SME calls this primitive when the results from the scan are
    available.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeScanFullCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeScanFullCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeScanFullCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_FULL_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeScanFullCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeScanFullCfm *msg__; \
        CsrWifiSmeScanFullCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanFullCfmSend(dst__, status__) \
    CsrWifiSmeScanFullCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultIndSend

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever a scan indication is received from the firmware.

  PARAMETERS
    queue  - Destination Task Queue
    result - Points to a buffer containing a scan result.

*******************************************************************************/
#define CsrWifiSmeScanResultIndCreate(msg__, dst__, src__, result__) \
    msg__ = (CsrWifiSmeScanResultInd *) CsrPmemAlloc(sizeof(CsrWifiSmeScanResultInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_RESULT_IND, dst__, src__); \
    msg__->result = (result__);

#define CsrWifiSmeScanResultIndSendTo(dst__, src__, result__) \
    { \
        CsrWifiSmeScanResultInd *msg__; \
        CsrWifiSmeScanResultIndCreate(msg__, dst__, src__, result__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanResultIndSend(dst__, result__) \
    CsrWifiSmeScanResultIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, result__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsFlushReqSend

  DESCRIPTION
    The Wireless Manager calls this primitive to ask the SME to delete all
    scan results from its cache, except for the scan result of any currently
    connected network.
    As scan results are received by the SME from the firmware, they are
    cached in the SME memory.
    Any time the Wireless Manager requests scan results, they are returned
    from the SME internal cache.
    For some applications it may be desirable to clear this cache prior to
    requesting that a scan be performed; this will ensure that the cache then
    only contains the networks detected in the most recent scan.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeScanResultsFlushReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeScanResultsFlushReq *) CsrPmemAlloc(sizeof(CsrWifiSmeScanResultsFlushReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_RESULTS_FLUSH_REQ, dst__, src__);

#define CsrWifiSmeScanResultsFlushReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeScanResultsFlushReq *msg__; \
        CsrWifiSmeScanResultsFlushReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanResultsFlushReqSend(src__) \
    CsrWifiSmeScanResultsFlushReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsFlushCfmSend

  DESCRIPTION
    The SME will call this primitive when the cache has been cleared.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeScanResultsFlushCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeScanResultsFlushCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeScanResultsFlushCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_RESULTS_FLUSH_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeScanResultsFlushCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeScanResultsFlushCfm *msg__; \
        CsrWifiSmeScanResultsFlushCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanResultsFlushCfmSend(dst__, status__) \
    CsrWifiSmeScanResultsFlushCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsGetReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to retrieve the
    current set of scan results, either after receiving a successful
    CSR_WIFI_SME_SCAN_FULL_CFM, or to get autonomous scan results.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeScanResultsGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeScanResultsGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeScanResultsGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_RESULTS_GET_REQ, dst__, src__);

#define CsrWifiSmeScanResultsGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeScanResultsGetReq *msg__; \
        CsrWifiSmeScanResultsGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanResultsGetReqSend(src__) \
    CsrWifiSmeScanResultsGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsGetCfmSend

  DESCRIPTION
    The SME sends this primitive to provide the current set of scan results.

  PARAMETERS
    queue            - Destination Task Queue
    status           - Reports the result of the request
    scanResultsCount - Number of scan results
    scanResults      - Points to a buffer containing an array of
                       CsrWifiSmeScanResult structures.

*******************************************************************************/
#define CsrWifiSmeScanResultsGetCfmCreate(msg__, dst__, src__, status__, scanResultsCount__, scanResults__) \
    msg__ = (CsrWifiSmeScanResultsGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeScanResultsGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SCAN_RESULTS_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->scanResultsCount = (scanResultsCount__); \
    msg__->scanResults = (scanResults__);

#define CsrWifiSmeScanResultsGetCfmSendTo(dst__, src__, status__, scanResultsCount__, scanResults__) \
    { \
        CsrWifiSmeScanResultsGetCfm *msg__; \
        CsrWifiSmeScanResultsGetCfmCreate(msg__, dst__, src__, status__, scanResultsCount__, scanResults__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeScanResultsGetCfmSend(dst__, status__, scanResultsCount__, scanResults__) \
    CsrWifiSmeScanResultsGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, scanResultsCount__, scanResults__)

/*******************************************************************************

  NAME
    CsrWifiSmeSetReqSend

  DESCRIPTION
    Used to pass custom data to the SME. Format is the same as 802.11 Info
    Elements => | Id | Length | Data
    1) Cmanr Test Mode "Id:0 Length:1 Data:0x00 = OFF 0x01 = ON" "0x00 0x01
    (0x00|0x01)"

  PARAMETERS
    queue      - Message Source Task Queue (Cfm's will be sent to this Queue)
    dataLength - Number of bytes in the buffer pointed to by 'data'
    data       - Pointer to the buffer containing 'dataLength' bytes

*******************************************************************************/
#define CsrWifiSmeSetReqCreate(msg__, dst__, src__, dataLength__, data__) \
    msg__ = (CsrWifiSmeSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SET_REQ, dst__, src__); \
    msg__->dataLength = (dataLength__); \
    msg__->data = (data__);

#define CsrWifiSmeSetReqSendTo(dst__, src__, dataLength__, data__) \
    { \
        CsrWifiSmeSetReq *msg__; \
        CsrWifiSmeSetReqCreate(msg__, dst__, src__, dataLength__, data__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSetReqSend(src__, dataLength__, data__) \
    CsrWifiSmeSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, dataLength__, data__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the Sme common parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeSmeCommonConfigGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeSmeCommonConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeCommonConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_COMMON_CONFIG_GET_REQ, dst__, src__);

#define CsrWifiSmeSmeCommonConfigGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeSmeCommonConfigGetReq *msg__; \
        CsrWifiSmeSmeCommonConfigGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeCommonConfigGetReqSend(src__) \
    CsrWifiSmeSmeCommonConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    status       - Reports the result of the request
    deviceConfig - Configuration options in the SME

*******************************************************************************/
#define CsrWifiSmeSmeCommonConfigGetCfmCreate(msg__, dst__, src__, status__, deviceConfig__) \
    msg__ = (CsrWifiSmeSmeCommonConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeCommonConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_COMMON_CONFIG_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->deviceConfig = (deviceConfig__);

#define CsrWifiSmeSmeCommonConfigGetCfmSendTo(dst__, src__, status__, deviceConfig__) \
    { \
        CsrWifiSmeSmeCommonConfigGetCfm *msg__; \
        CsrWifiSmeSmeCommonConfigGetCfmCreate(msg__, dst__, src__, status__, deviceConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeCommonConfigGetCfmSend(dst__, status__, deviceConfig__) \
    CsrWifiSmeSmeCommonConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, deviceConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the Sme common.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    deviceConfig - Configuration options in the SME

*******************************************************************************/
#define CsrWifiSmeSmeCommonConfigSetReqCreate(msg__, dst__, src__, deviceConfig__) \
    msg__ = (CsrWifiSmeSmeCommonConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeCommonConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_COMMON_CONFIG_SET_REQ, dst__, src__); \
    msg__->deviceConfig = (deviceConfig__);

#define CsrWifiSmeSmeCommonConfigSetReqSendTo(dst__, src__, deviceConfig__) \
    { \
        CsrWifiSmeSmeCommonConfigSetReq *msg__; \
        CsrWifiSmeSmeCommonConfigSetReqCreate(msg__, dst__, src__, deviceConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeCommonConfigSetReqSend(src__, deviceConfig__) \
    CsrWifiSmeSmeCommonConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, deviceConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigSetCfmSend

  DESCRIPTION
    Reports the result of the request

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeSmeCommonConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeSmeCommonConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeCommonConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_COMMON_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeSmeCommonConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeSmeCommonConfigSetCfm *msg__; \
        CsrWifiSmeSmeCommonConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeCommonConfigSetCfmSend(dst__, status__) \
    CsrWifiSmeSmeCommonConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigGetReqSend

  DESCRIPTION
    This primitive gets the value of the SmeStaConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiSmeSmeStaConfigGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeSmeStaConfigGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeStaConfigGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_STA_CONFIG_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeSmeStaConfigGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeSmeStaConfigGetReq *msg__; \
        CsrWifiSmeSmeStaConfigGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeStaConfigGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeSmeStaConfigGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    smeConfig    - Current SME Station Parameters

*******************************************************************************/
#define CsrWifiSmeSmeStaConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, smeConfig__) \
    msg__ = (CsrWifiSmeSmeStaConfigGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeStaConfigGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_STA_CONFIG_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->smeConfig = (smeConfig__);

#define CsrWifiSmeSmeStaConfigGetCfmSendTo(dst__, src__, interfaceTag__, status__, smeConfig__) \
    { \
        CsrWifiSmeSmeStaConfigGetCfm *msg__; \
        CsrWifiSmeSmeStaConfigGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, smeConfig__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeStaConfigGetCfmSend(dst__, interfaceTag__, status__, smeConfig__) \
    CsrWifiSmeSmeStaConfigGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, smeConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigSetReqSend

  DESCRIPTION
    This primitive sets the value of the SmeConfig parameter.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface
    smeConfig    - SME Station Parameters to be set

*******************************************************************************/
#define CsrWifiSmeSmeStaConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, smeConfig__) \
    msg__ = (CsrWifiSmeSmeStaConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeStaConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_STA_CONFIG_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->smeConfig = (smeConfig__);

#define CsrWifiSmeSmeStaConfigSetReqSendTo(dst__, src__, interfaceTag__, smeConfig__) \
    { \
        CsrWifiSmeSmeStaConfigSetReq *msg__; \
        CsrWifiSmeSmeStaConfigSetReqCreate(msg__, dst__, src__, interfaceTag__, smeConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeStaConfigSetReqSend(src__, interfaceTag__, smeConfig__) \
    CsrWifiSmeSmeStaConfigSetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, smeConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeSmeStaConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeSmeStaConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeSmeStaConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_SME_STA_CONFIG_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeSmeStaConfigSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeSmeStaConfigSetCfm *msg__; \
        CsrWifiSmeSmeStaConfigSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeSmeStaConfigSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeSmeStaConfigSetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeStationMacAddressGetReqSend

  DESCRIPTION
    This primitives is used to retrieve the current MAC address used by the
    station.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeStationMacAddressGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeStationMacAddressGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeStationMacAddressGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_REQ, dst__, src__);

#define CsrWifiSmeStationMacAddressGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeStationMacAddressGetReq *msg__; \
        CsrWifiSmeStationMacAddressGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeStationMacAddressGetReqSend(src__) \
    CsrWifiSmeStationMacAddressGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeStationMacAddressGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue             - Destination Task Queue
    status            - Reports the result of the request
    stationMacAddress - Current MAC address of the station.

*******************************************************************************/
#define CsrWifiSmeStationMacAddressGetCfmCreate(msg__, dst__, src__, status__, stationMacAddress__) \
    msg__ = (CsrWifiSmeStationMacAddressGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeStationMacAddressGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    CsrMemCpy(msg__->stationMacAddress, (stationMacAddress__), sizeof(CsrWifiMacAddress) * 2);

#define CsrWifiSmeStationMacAddressGetCfmSendTo(dst__, src__, status__, stationMacAddress__) \
    { \
        CsrWifiSmeStationMacAddressGetCfm *msg__; \
        CsrWifiSmeStationMacAddressGetCfmCreate(msg__, dst__, src__, status__, stationMacAddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeStationMacAddressGetCfmSend(dst__, status__, stationMacAddress__) \
    CsrWifiSmeStationMacAddressGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, stationMacAddress__)

/*******************************************************************************

  NAME
    CsrWifiSmeTspecReqSend

  DESCRIPTION
    The wireless manager application should call this primitive to use the
    TSPEC feature.
    The chip supports the use of TSPECs and TCLAS for the use of IEEE
    802.11/WMM Quality of Service features.
    The API allows the wireless manager application to supply a correctly
    formatted TSPEC and TCLAS pair to the driver.
    After performing basic validation, the driver negotiates the installation
    of the TSPEC with the AP as defined by the 802.11 specification.
    The driver retains all TSPEC and TCLAS pairs until they are specifically
    removed.
    It is not compulsory for a TSPEC to have a TCLAS (NULL is used to
    indicate that no TCLAS is supplied), while a TCLASS always require a
    TSPEC.
    The format of the TSPEC element is specified in 'WMM (including WMM Power
    Save) Specification - Version 1.1' and 'ANSI/IEEE Std 802.11-REVmb/D3.0'.
    For more information, see 'UniFi Configuring WMM and WMM-PS'.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag  - Interface Identifier; unique identifier of an interface
    action        - Specifies the action to be carried out on the list of TSPECs.
                    CSR_WIFI_SME_LIST_ACTION_FLUSH is not applicable here.
    transactionId - Unique Transaction ID for the TSPEC, as assigned by the
                    driver
    strict        - If it set to false, allows the SME to perform automatic
                    TSPEC negotiation
    ctrlMask      - Additional TSPEC configuration for CCX.
                    Set mask with values from CsrWifiSmeTspecCtrl.
                    CURRENTLY NOT SUPPORTED
    tspecLength   - Length of the TSPEC.
    tspec         - Points to the first byte of the TSPEC
    tclasLength   - Length of the TCLAS.
                    If it is equal to 0, no TCLASS is provided for the TSPEC
    tclas         - Points to the first byte of the TCLAS, if any.

*******************************************************************************/
#define CsrWifiSmeTspecReqCreate(msg__, dst__, src__, interfaceTag__, action__, transactionId__, strict__, ctrlMask__, tspecLength__, tspec__, tclasLength__, tclas__) \
    msg__ = (CsrWifiSmeTspecReq *) CsrPmemAlloc(sizeof(CsrWifiSmeTspecReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_TSPEC_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->action = (action__); \
    msg__->transactionId = (transactionId__); \
    msg__->strict = (strict__); \
    msg__->ctrlMask = (ctrlMask__); \
    msg__->tspecLength = (tspecLength__); \
    msg__->tspec = (tspec__); \
    msg__->tclasLength = (tclasLength__); \
    msg__->tclas = (tclas__);

#define CsrWifiSmeTspecReqSendTo(dst__, src__, interfaceTag__, action__, transactionId__, strict__, ctrlMask__, tspecLength__, tspec__, tclasLength__, tclas__) \
    { \
        CsrWifiSmeTspecReq *msg__; \
        CsrWifiSmeTspecReqCreate(msg__, dst__, src__, interfaceTag__, action__, transactionId__, strict__, ctrlMask__, tspecLength__, tspec__, tclasLength__, tclas__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeTspecReqSend(src__, interfaceTag__, action__, transactionId__, strict__, ctrlMask__, tspecLength__, tspec__, tclasLength__, tclas__) \
    CsrWifiSmeTspecReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, interfaceTag__, action__, transactionId__, strict__, ctrlMask__, tspecLength__, tspec__, tclasLength__, tclas__)

/*******************************************************************************

  NAME
    CsrWifiSmeTspecIndSend

  DESCRIPTION
    The SME will send this primitive to all the task that have registered to
    receive it when a status change in the TSPEC occurs.

  PARAMETERS
    queue           - Destination Task Queue
    interfaceTag    - Interface Identifier; unique identifier of an interface
    transactionId   - Unique Transaction ID for the TSPEC, as assigned by the
                      driver
    tspecResultCode - Specifies the TSPEC operation requested by the peer
                      station
    tspecLength     - Length of the TSPEC.
    tspec           - Points to the first byte of the TSPEC

*******************************************************************************/
#define CsrWifiSmeTspecIndCreate(msg__, dst__, src__, interfaceTag__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    msg__ = (CsrWifiSmeTspecInd *) CsrPmemAlloc(sizeof(CsrWifiSmeTspecInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_TSPEC_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->transactionId = (transactionId__); \
    msg__->tspecResultCode = (tspecResultCode__); \
    msg__->tspecLength = (tspecLength__); \
    msg__->tspec = (tspec__);

#define CsrWifiSmeTspecIndSendTo(dst__, src__, interfaceTag__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    { \
        CsrWifiSmeTspecInd *msg__; \
        CsrWifiSmeTspecIndCreate(msg__, dst__, src__, interfaceTag__, transactionId__, tspecResultCode__, tspecLength__, tspec__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeTspecIndSend(dst__, interfaceTag__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    CsrWifiSmeTspecIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, transactionId__, tspecResultCode__, tspecLength__, tspec__)

/*******************************************************************************

  NAME
    CsrWifiSmeTspecCfmSend

  DESCRIPTION
    The SME calls the primitive to report the result of the TSpec primitive
    request.

  PARAMETERS
    queue           - Destination Task Queue
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    transactionId   - Unique Transaction ID for the TSPEC, as assigned by the
                      driver
    tspecResultCode - Specifies the result of the negotiated TSPEC operation
    tspecLength     - Length of the TSPEC.
    tspec           - Points to the first byte of the TSPEC

*******************************************************************************/
#define CsrWifiSmeTspecCfmCreate(msg__, dst__, src__, interfaceTag__, status__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    msg__ = (CsrWifiSmeTspecCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeTspecCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_TSPEC_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->transactionId = (transactionId__); \
    msg__->tspecResultCode = (tspecResultCode__); \
    msg__->tspecLength = (tspecLength__); \
    msg__->tspec = (tspec__);

#define CsrWifiSmeTspecCfmSendTo(dst__, src__, interfaceTag__, status__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    { \
        CsrWifiSmeTspecCfm *msg__; \
        CsrWifiSmeTspecCfmCreate(msg__, dst__, src__, interfaceTag__, status__, transactionId__, tspecResultCode__, tspecLength__, tspec__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeTspecCfmSend(dst__, interfaceTag__, status__, transactionId__, tspecResultCode__, tspecLength__, tspec__) \
    CsrWifiSmeTspecCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, transactionId__, tspecResultCode__, tspecLength__, tspec__)

/*******************************************************************************

  NAME
    CsrWifiSmeVersionsGetReqSend

  DESCRIPTION
    This primitive gets the value of the Versions parameter.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeVersionsGetReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeVersionsGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeVersionsGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_VERSIONS_GET_REQ, dst__, src__);

#define CsrWifiSmeVersionsGetReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeVersionsGetReq *msg__; \
        CsrWifiSmeVersionsGetReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeVersionsGetReqSend(src__) \
    CsrWifiSmeVersionsGetReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeVersionsGetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue    - Destination Task Queue
    status   - Reports the result of the request
    versions - Version IDs of the product

*******************************************************************************/
#define CsrWifiSmeVersionsGetCfmCreate(msg__, dst__, src__, status__, versions__) \
    msg__ = (CsrWifiSmeVersionsGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeVersionsGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_VERSIONS_GET_CFM, dst__, src__); \
    msg__->status = (status__); \
    msg__->versions = (versions__);

#define CsrWifiSmeVersionsGetCfmSendTo(dst__, src__, status__, versions__) \
    { \
        CsrWifiSmeVersionsGetCfm *msg__; \
        CsrWifiSmeVersionsGetCfmCreate(msg__, dst__, src__, status__, versions__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeVersionsGetCfmSend(dst__, status__, versions__) \
    CsrWifiSmeVersionsGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__, versions__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiFlightmodeReqSend

  DESCRIPTION
    The wireless manager application may call this primitive on boot-up of
    the platform to ensure that the chip is placed in a mode that prevents
    any emission of RF energy.
    This primitive is an alternative to CSR_WIFI_SME_WIFI_ON_REQ.
    As in CSR_WIFI_SME_WIFI_ON_REQ, it causes the download of the patch file
    (if any) and the programming of the initial MIB settings (if supplied by
    the WMA), but it also ensures that the chip is left in its lowest
    possible power-mode with the radio subsystems disabled.
    This feature is useful on platforms where power cannot be removed from
    the chip (leaving the chip not initialised will cause it to consume more
    power so calling this function ensures that the chip is initialised into
    a low power mode but without entering a state where it could emit any RF
    energy).
    NOTE: this primitive does not cause the Wi-Fi to change state: Wi-Fi
    stays conceptually off. Configuration primitives can be sent after
    CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ and the configuration will be maintained.
    Requests that require the state of the Wi-Fi to be ON will return
    CSR_WIFI_SME_STATUS_WIFI_OFF in their confirms.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    address       - Optionally specifies a station MAC address.
                    In normal use, the manager should set the address to 0xFF
                    0xFF 0xFF 0xFF 0xFF 0xFF, which will cause the chip to use
                    the MAC address in the MIB.
    mibFilesCount - Number of provided data blocks with initial MIB values
    mibFiles      - Points to the first data block with initial MIB values.
                    These data blocks are typically the contents of the provided
                    files ufmib.dat and localmib.dat, available from the host
                    file system, if they exist.
                    These files typically contain radio tuning and calibration
                    values.
                    More values can be created using the Host Tools.

*******************************************************************************/
#define CsrWifiSmeWifiFlightmodeReqCreate(msg__, dst__, src__, address__, mibFilesCount__, mibFiles__) \
    msg__ = (CsrWifiSmeWifiFlightmodeReq *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiFlightmodeReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ, dst__, src__); \
    msg__->address = (address__); \
    msg__->mibFilesCount = (mibFilesCount__); \
    msg__->mibFiles = (mibFiles__);

#define CsrWifiSmeWifiFlightmodeReqSendTo(dst__, src__, address__, mibFilesCount__, mibFiles__) \
    { \
        CsrWifiSmeWifiFlightmodeReq *msg__; \
        CsrWifiSmeWifiFlightmodeReqCreate(msg__, dst__, src__, address__, mibFilesCount__, mibFiles__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiFlightmodeReqSend(src__, address__, mibFilesCount__, mibFiles__) \
    CsrWifiSmeWifiFlightmodeReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, address__, mibFilesCount__, mibFiles__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiFlightmodeCfmSend

  DESCRIPTION
    The SME calls this primitive when the chip is initialised for low power
    mode and with the radio subsystem disabled. To leave flight mode, and
    enable Wi-Fi, the wireless manager application should call
    CSR_WIFI_SME_WIFI_ON_REQ.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeWifiFlightmodeCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeWifiFlightmodeCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiFlightmodeCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_FLIGHTMODE_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeWifiFlightmodeCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeWifiFlightmodeCfm *msg__; \
        CsrWifiSmeWifiFlightmodeCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiFlightmodeCfmSend(dst__, status__) \
    CsrWifiSmeWifiFlightmodeCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to turn off the
    chip, thus saving power when Wi-Fi is not in use.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiSmeWifiOffReqCreate(msg__, dst__, src__) \
    msg__ = (CsrWifiSmeWifiOffReq *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOffReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_OFF_REQ, dst__, src__);

#define CsrWifiSmeWifiOffReqSendTo(dst__, src__) \
    { \
        CsrWifiSmeWifiOffReq *msg__; \
        CsrWifiSmeWifiOffReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOffReqSend(src__) \
    CsrWifiSmeWifiOffReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffIndSend

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it to report that the chip has been turned off.

  PARAMETERS
    queue  - Destination Task Queue
    reason - Indicates the reason why the Wi-Fi has been switched off.

*******************************************************************************/
#define CsrWifiSmeWifiOffIndCreate(msg__, dst__, src__, reason__) \
    msg__ = (CsrWifiSmeWifiOffInd *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOffInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_OFF_IND, dst__, src__); \
    msg__->reason = (reason__);

#define CsrWifiSmeWifiOffIndSendTo(dst__, src__, reason__) \
    { \
        CsrWifiSmeWifiOffInd *msg__; \
        CsrWifiSmeWifiOffIndCreate(msg__, dst__, src__, reason__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOffIndSend(dst__, reason__) \
    CsrWifiSmeWifiOffIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, reason__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffCfmSend

  DESCRIPTION
    After receiving CSR_WIFI_SME_WIFI_OFF_REQ, if the chip is connected to a
    network, the SME will perform a disconnect operation, will send a
    CSR_WIFI_SME_MEDIA_STATUS_IND with
    CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED, and then will call
    CSR_WIFI_SME_WIFI_OFF_CFM when the chip is off.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeWifiOffCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeWifiOffCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOffCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_OFF_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeWifiOffCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeWifiOffCfm *msg__; \
        CsrWifiSmeWifiOffCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOffCfmSend(dst__, status__) \
    CsrWifiSmeWifiOffCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnReqSend

  DESCRIPTION
    The wireless manager application calls this primitive to turn on the
    Wi-Fi chip.
    If the Wi-Fi chip is currently off, the SME turns the Wi-Fi chip on,
    downloads the patch file (if any), and programs the initial MIB settings
    (if supplied by the WMA).
    The patch file is not provided with the SME API; its downloading is
    automatic and handled internally by the system.
    The MIB settings, when provided, override the default values that the
    firmware loads from EEPROM.
    If the Wi-Fi chip is already on, the SME takes no action and returns a
    successful status in the confirm.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    address       - Optionally specifies a station MAC address.
                    In normal use, the manager should set the address to 0xFF
                    0xFF 0xFF 0xFF 0xFF 0xFF, which will cause the chip to use
                    the MAC address in the MIB
    mibFilesCount - Number of provided data blocks with initial MIB values
    mibFiles      - Points to the first data block with initial MIB values.
                    These data blocks are typically the contents of the provided
                    files ufmib.dat and localmib.dat, available from the host
                    file system, if they exist.
                    These files typically contain radio tuning and calibration
                    values.
                    More values can be created using the Host Tools.

*******************************************************************************/
#define CsrWifiSmeWifiOnReqCreate(msg__, dst__, src__, address__, mibFilesCount__, mibFiles__) \
    msg__ = (CsrWifiSmeWifiOnReq *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOnReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_ON_REQ, dst__, src__); \
    msg__->address = (address__); \
    msg__->mibFilesCount = (mibFilesCount__); \
    msg__->mibFiles = (mibFiles__);

#define CsrWifiSmeWifiOnReqSendTo(dst__, src__, address__, mibFilesCount__, mibFiles__) \
    { \
        CsrWifiSmeWifiOnReq *msg__; \
        CsrWifiSmeWifiOnReqCreate(msg__, dst__, src__, address__, mibFilesCount__, mibFiles__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOnReqSend(src__, address__, mibFilesCount__, mibFiles__) \
    CsrWifiSmeWifiOnReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, address__, mibFilesCount__, mibFiles__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnIndSend

  DESCRIPTION
    The SME sends this primitive to all tasks that have registered to receive
    it once the chip becomes available and ready to use.

  PARAMETERS
    queue   - Destination Task Queue
    address - Current MAC address

*******************************************************************************/
#define CsrWifiSmeWifiOnIndCreate(msg__, dst__, src__, address__) \
    msg__ = (CsrWifiSmeWifiOnInd *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOnInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_ON_IND, dst__, src__); \
    msg__->address = (address__);

#define CsrWifiSmeWifiOnIndSendTo(dst__, src__, address__) \
    { \
        CsrWifiSmeWifiOnInd *msg__; \
        CsrWifiSmeWifiOnIndCreate(msg__, dst__, src__, address__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOnIndSend(dst__, address__) \
    CsrWifiSmeWifiOnIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, address__)

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnCfmSend

  DESCRIPTION
    The SME sends this primitive to the task that has sent the request once
    the chip has been initialised and is available for use.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiSmeWifiOnCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeWifiOnCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeWifiOnCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WIFI_ON_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeWifiOnCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeWifiOnCfm *msg__; \
        CsrWifiSmeWifiOnCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWifiOnCfmSend(dst__, status__) \
    CsrWifiSmeWifiOnCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigurationReqSend

  DESCRIPTION
    This primitive passes the WPS information for the device to SME. This may
    be accepted only if no interface is active.

  PARAMETERS
    queue     - Message Source Task Queue (Cfm's will be sent to this Queue)
    wpsConfig - WPS config.

*******************************************************************************/
#define CsrWifiSmeWpsConfigurationReqCreate(msg__, dst__, src__, wpsConfig__) \
    msg__ = (CsrWifiSmeWpsConfigurationReq *) CsrPmemAlloc(sizeof(CsrWifiSmeWpsConfigurationReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WPS_CONFIGURATION_REQ, dst__, src__); \
    msg__->wpsConfig = (wpsConfig__);

#define CsrWifiSmeWpsConfigurationReqSendTo(dst__, src__, wpsConfig__) \
    { \
        CsrWifiSmeWpsConfigurationReq *msg__; \
        CsrWifiSmeWpsConfigurationReqCreate(msg__, dst__, src__, wpsConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWpsConfigurationReqSend(src__, wpsConfig__) \
    CsrWifiSmeWpsConfigurationReqSendTo(CSR_WIFI_SME_LIB_DESTINATION_QUEUE, src__, wpsConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigurationCfmSend

  DESCRIPTION
    Confirm.

  PARAMETERS
    queue  - Destination Task Queue
    status - Status of the request.

*******************************************************************************/
#define CsrWifiSmeWpsConfigurationCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeWpsConfigurationCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeWpsConfigurationCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_PRIM, CSR_WIFI_SME_WPS_CONFIGURATION_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeWpsConfigurationCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeWpsConfigurationCfm *msg__; \
        CsrWifiSmeWpsConfigurationCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_PRIM, msg__); \
    }

#define CsrWifiSmeWpsConfigurationCfmSend(dst__, status__) \
    CsrWifiSmeWpsConfigurationCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_SME_LIB_H__ */
