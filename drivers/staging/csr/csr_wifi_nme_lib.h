/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_LIB_H__
#define CSR_WIFI_NME_LIB_H__

#include "csr_pmem.h"
#include "csr_sched.h"
#include "csr_macro.h"
#include "csr_msg_transport.h"

#include "csr_wifi_lib.h"

#include "csr_wifi_nme_prim.h"
#include "csr_wifi_nme_task.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_lib.h
#endif

/*----------------------------------------------------------------------------*
 *  CsrWifiNmeFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_NME upstream message. Does not
 *      free the message itself, and can only be used for upstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_NME upstream message
 *----------------------------------------------------------------------------*/
void CsrWifiNmeFreeUpstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 *  CsrWifiNmeFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_NME downstream message. Does not
 *      free the message itself, and can only be used for downstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_NME downstream message
 *----------------------------------------------------------------------------*/
void CsrWifiNmeFreeDownstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 * Enum to string functions
 *----------------------------------------------------------------------------*/
const char* CsrWifiNmeAuthModeToString(CsrWifiNmeAuthMode value);
const char* CsrWifiNmeBssTypeToString(CsrWifiNmeBssType value);
const char* CsrWifiNmeCcxOptionsMaskToString(CsrWifiNmeCcxOptionsMask value);
const char* CsrWifiNmeConfigActionToString(CsrWifiNmeConfigAction value);
const char* CsrWifiNmeConnectionStatusToString(CsrWifiNmeConnectionStatus value);
const char* CsrWifiNmeCredentialTypeToString(CsrWifiNmeCredentialType value);
const char* CsrWifiNmeEapMethodToString(CsrWifiNmeEapMethod value);
const char* CsrWifiNmeEncryptionToString(CsrWifiNmeEncryption value);
const char* CsrWifiNmeIndicationsToString(CsrWifiNmeIndications value);
const char* CsrWifiNmeSecErrorToString(CsrWifiNmeSecError value);
const char* CsrWifiNmeSimCardTypeToString(CsrWifiNmeSimCardType value);
const char* CsrWifiNmeUmtsAuthResultToString(CsrWifiNmeUmtsAuthResult value);
const char* CsrWifiNmeWmmQosInfoToString(CsrWifiNmeWmmQosInfo value);


/*----------------------------------------------------------------------------*
 * CsrPrim Type toString function.
 * Converts a message type to the String name of the Message
 *----------------------------------------------------------------------------*/
const char* CsrWifiNmePrimTypeToString(CsrPrim msgType);

/*----------------------------------------------------------------------------*
 * Lookup arrays for PrimType name Strings
 *----------------------------------------------------------------------------*/
extern const char *CsrWifiNmeUpstreamPrimNames[CSR_WIFI_NME_PRIM_UPSTREAM_COUNT];
extern const char *CsrWifiNmeDownstreamPrimNames[CSR_WIFI_NME_PRIM_DOWNSTREAM_COUNT];

/*******************************************************************************

  NAME
    CsrWifiNmeConnectionStatusGetReqSend

  DESCRIPTION
    Requests the current connection status of the NME.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiNmeConnectionStatusGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeConnectionStatusGetReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_CONNECTION_STATUS_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiNmeConnectionStatusGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiNmeConnectionStatusGetReq *msg__; \
        CsrWifiNmeConnectionStatusGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeConnectionStatusGetReqSend(src__, interfaceTag__) \
    CsrWifiNmeConnectionStatusGetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiNmeConnectionStatusGetCfmSend

  DESCRIPTION
    Reports the connection status of the NME.

  PARAMETERS
    queue            - Destination Task Queue
    interfaceTag     - Interface Identifier; unique identifier of an interface
    status           - Indicates the success or otherwise of the requested
                       operation.
    connectionStatus - NME current connection status

*******************************************************************************/
#define CsrWifiNmeConnectionStatusGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionStatus__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeConnectionStatusGetCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_CONNECTION_STATUS_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectionStatus = (connectionStatus__);

#define CsrWifiNmeConnectionStatusGetCfmSendTo(dst__, src__, interfaceTag__, status__, connectionStatus__) \
    { \
        CsrWifiNmeConnectionStatusGetCfm *msg__; \
        CsrWifiNmeConnectionStatusGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectionStatus__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeConnectionStatusGetCfmSend(dst__, interfaceTag__, status__, connectionStatus__) \
    CsrWifiNmeConnectionStatusGetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__, connectionStatus__)

/*******************************************************************************

  NAME
    CsrWifiNmeEventMaskSetReqSend

  DESCRIPTION
    The wireless manager application may register with the NME to receive
    notification of interesting events. Indications will be sent only if the
    wireless manager explicitly registers to be notified of that event.
    indMask is a bit mask of values defined in CsrWifiNmeIndicationsMask.

  PARAMETERS
    queue   - Message Source Task Queue (Cfm's will be sent to this Queue)
    indMask - Set mask with values from CsrWifiNmeIndications

*******************************************************************************/
#define CsrWifiNmeEventMaskSetReqCreate(msg__, dst__, src__, indMask__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeEventMaskSetReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_EVENT_MASK_SET_REQ, dst__, src__); \
    msg__->indMask = (indMask__);

#define CsrWifiNmeEventMaskSetReqSendTo(dst__, src__, indMask__) \
    { \
        CsrWifiNmeEventMaskSetReq *msg__; \
        CsrWifiNmeEventMaskSetReqCreate(msg__, dst__, src__, indMask__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeEventMaskSetReqSend(src__, indMask__) \
    CsrWifiNmeEventMaskSetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, indMask__)

/*******************************************************************************

  NAME
    CsrWifiNmeEventMaskSetCfmSend

  DESCRIPTION
    The NME calls the primitive to report the result of the request
    primitive.

  PARAMETERS
    queue  - Destination Task Queue
    status - Reports the result of the request

*******************************************************************************/
#define CsrWifiNmeEventMaskSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeEventMaskSetCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_EVENT_MASK_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeEventMaskSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeEventMaskSetCfm *msg__; \
        CsrWifiNmeEventMaskSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeEventMaskSetCfmSend(dst__, status__) \
    CsrWifiNmeEventMaskSetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileConnectReqSend

  DESCRIPTION
    Requests the NME to attempt to connect to the specified profile.
    Overrides any current connection attempt.

  PARAMETERS
    queue           - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag    - Interface Identifier; unique identifier of an interface
    profileIdentity - Identity (BSSID, SSID) of profile to be connected to.
                      It must match an existing profile in the NME.

*******************************************************************************/
#define CsrWifiNmeProfileConnectReqCreate(msg__, dst__, src__, interfaceTag__, profileIdentity__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileConnectReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_CONNECT_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->profileIdentity = (profileIdentity__);

#define CsrWifiNmeProfileConnectReqSendTo(dst__, src__, interfaceTag__, profileIdentity__) \
    { \
        CsrWifiNmeProfileConnectReq *msg__; \
        CsrWifiNmeProfileConnectReqCreate(msg__, dst__, src__, interfaceTag__, profileIdentity__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileConnectReqSend(src__, interfaceTag__, profileIdentity__) \
    CsrWifiNmeProfileConnectReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, profileIdentity__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileConnectCfmSend

  DESCRIPTION
    Reports the status of the NME PROFILE CONNECT REQ. If unsuccessful the
    connectAttempt parameters contain details of the APs that the NME
    attempted to connect to before reporting the failure of the request.

  PARAMETERS
    queue                - Destination Task Queue
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    status               - Indicates the success or otherwise of the requested
                           operation.
    connectAttemptsCount - This parameter is relevant only if
                           status!=CSR_WIFI_NME_STATUS_SUCCESS.
                           Number of connection attempt elements provided with
                           this primitive
    connectAttempts      - This parameter is relevant only if
                           status!=CSR_WIFI_NME_STATUS_SUCCESS.
                           Points to the list of connection attempt elements
                           provided with this primitive
                           Each element of the list provides information about
                           an AP on which the connection attempt was made and
                           the error that occurred during the attempt.

*******************************************************************************/
#define CsrWifiNmeProfileConnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectAttemptsCount__, connectAttempts__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileConnectCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_CONNECT_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->connectAttemptsCount = (connectAttemptsCount__); \
    msg__->connectAttempts = (connectAttempts__);

#define CsrWifiNmeProfileConnectCfmSendTo(dst__, src__, interfaceTag__, status__, connectAttemptsCount__, connectAttempts__) \
    { \
        CsrWifiNmeProfileConnectCfm *msg__; \
        CsrWifiNmeProfileConnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__, connectAttemptsCount__, connectAttempts__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileConnectCfmSend(dst__, interfaceTag__, status__, connectAttemptsCount__, connectAttempts__) \
    CsrWifiNmeProfileConnectCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__, connectAttemptsCount__, connectAttempts__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteAllReqSend

  DESCRIPTION
    Deletes all profiles present in the NME, but does NOT modify the
    preferred profile list.

  PARAMETERS
    queue  - Message Source Task Queue (Cfm's will be sent to this Queue)

*******************************************************************************/
#define CsrWifiNmeProfileDeleteAllReqCreate(msg__, dst__, src__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileDeleteAllReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_DELETE_ALL_REQ, dst__, src__);

#define CsrWifiNmeProfileDeleteAllReqSendTo(dst__, src__) \
    { \
        CsrWifiNmeProfileDeleteAllReq *msg__; \
        CsrWifiNmeProfileDeleteAllReqCreate(msg__, dst__, src__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileDeleteAllReqSend(src__) \
    CsrWifiNmeProfileDeleteAllReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteAllCfmSend

  DESCRIPTION
    Reports the status of the CSR_WIFI_NME_PROFILE_DELETE_ALL_REQ.
    Returns always CSR_WIFI_NME_STATUS_SUCCESS.

  PARAMETERS
    queue  - Destination Task Queue
    status - Indicates the success or otherwise of the requested operation, but
             in this case it always set to success.

*******************************************************************************/
#define CsrWifiNmeProfileDeleteAllCfmCreate(msg__, dst__, src__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileDeleteAllCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_DELETE_ALL_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeProfileDeleteAllCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeProfileDeleteAllCfm *msg__; \
        CsrWifiNmeProfileDeleteAllCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileDeleteAllCfmSend(dst__, status__) \
    CsrWifiNmeProfileDeleteAllCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteReqSend

  DESCRIPTION
    Will delete the profile with a matching identity, but does NOT modify the
    preferred profile list.

  PARAMETERS
    queue           - Message Source Task Queue (Cfm's will be sent to this Queue)
    profileIdentity - Identity (BSSID, SSID) of profile to be deleted.

*******************************************************************************/
#define CsrWifiNmeProfileDeleteReqCreate(msg__, dst__, src__, profileIdentity__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileDeleteReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_DELETE_REQ, dst__, src__); \
    msg__->profileIdentity = (profileIdentity__);

#define CsrWifiNmeProfileDeleteReqSendTo(dst__, src__, profileIdentity__) \
    { \
        CsrWifiNmeProfileDeleteReq *msg__; \
        CsrWifiNmeProfileDeleteReqCreate(msg__, dst__, src__, profileIdentity__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileDeleteReqSend(src__, profileIdentity__) \
    CsrWifiNmeProfileDeleteReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, profileIdentity__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteCfmSend

  DESCRIPTION
    Reports the status of the CSR_WIFI_NME_PROFILE_DELETE_REQ.
    Returns CSR_WIFI_NME_STATUS_NOT_FOUND if there is no matching profile.

  PARAMETERS
    queue  - Destination Task Queue
    status - Indicates the success or otherwise of the requested operation.

*******************************************************************************/
#define CsrWifiNmeProfileDeleteCfmCreate(msg__, dst__, src__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileDeleteCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_DELETE_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeProfileDeleteCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeProfileDeleteCfm *msg__; \
        CsrWifiNmeProfileDeleteCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileDeleteCfmSend(dst__, status__) \
    CsrWifiNmeProfileDeleteCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDisconnectIndSend

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that informs that application that the current profile
    connection has disconnected. The indication will contain information
    about APs that it attempted to maintain the connection via i.e. in the
    case of failed roaming.

  PARAMETERS
    queue                - Destination Task Queue
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    connectAttemptsCount - Number of connection attempt elements provided with
                           this primitive
    connectAttempts      - Points to the list of connection attempt elements
                           provided with this primitive
                           Each element of the list provides information about
                           an AP on which the connection attempt was made and
                           the error occurred during the attempt.

*******************************************************************************/
#define CsrWifiNmeProfileDisconnectIndCreate(msg__, dst__, src__, interfaceTag__, connectAttemptsCount__, connectAttempts__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileDisconnectInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_DISCONNECT_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->connectAttemptsCount = (connectAttemptsCount__); \
    msg__->connectAttempts = (connectAttempts__);

#define CsrWifiNmeProfileDisconnectIndSendTo(dst__, src__, interfaceTag__, connectAttemptsCount__, connectAttempts__) \
    { \
        CsrWifiNmeProfileDisconnectInd *msg__; \
        CsrWifiNmeProfileDisconnectIndCreate(msg__, dst__, src__, interfaceTag__, connectAttemptsCount__, connectAttempts__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileDisconnectIndSend(dst__, interfaceTag__, connectAttemptsCount__, connectAttempts__) \
    CsrWifiNmeProfileDisconnectIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, connectAttemptsCount__, connectAttempts__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileOrderSetReqSend

  DESCRIPTION
    Defines the preferred order that profiles present in the NME should be
    used during the NME auto-connect behaviour.
    If profileIdentitysCount == 0, it removes any existing preferred profile
    list already present in the NME, effectively disabling the auto-connect
    behaviour.
    NOTE: Profile identities that do not match any profile stored in the NME
    are ignored during the auto-connect procedure.
    NOTE: during auto-connect the NME will only attempt to join an existing
    adhoc network and it will never attempt to host an adhoc network; for
    hosting and adhoc network, use CSR_WIFI_NME_PROFILE_CONNECT_REQ

  PARAMETERS
    queue                 - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag          - Interface Identifier; unique identifier of an
                            interface
    profileIdentitysCount - The number of profiles identities in the list.
    profileIdentitys      - Points to the list of profile identities.

*******************************************************************************/
#define CsrWifiNmeProfileOrderSetReqCreate(msg__, dst__, src__, interfaceTag__, profileIdentitysCount__, profileIdentitys__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileOrderSetReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_ORDER_SET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->profileIdentitysCount = (profileIdentitysCount__); \
    msg__->profileIdentitys = (profileIdentitys__);

#define CsrWifiNmeProfileOrderSetReqSendTo(dst__, src__, interfaceTag__, profileIdentitysCount__, profileIdentitys__) \
    { \
        CsrWifiNmeProfileOrderSetReq *msg__; \
        CsrWifiNmeProfileOrderSetReqCreate(msg__, dst__, src__, interfaceTag__, profileIdentitysCount__, profileIdentitys__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileOrderSetReqSend(src__, interfaceTag__, profileIdentitysCount__, profileIdentitys__) \
    CsrWifiNmeProfileOrderSetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, profileIdentitysCount__, profileIdentitys__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileOrderSetCfmSend

  DESCRIPTION
    Confirmation to UNIFI_NME_PROFILE_ORDER_SET.request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Indicates the success or otherwise of the requested
                   operation.

*******************************************************************************/
#define CsrWifiNmeProfileOrderSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileOrderSetCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_ORDER_SET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiNmeProfileOrderSetCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiNmeProfileOrderSetCfm *msg__; \
        CsrWifiNmeProfileOrderSetCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileOrderSetCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiNmeProfileOrderSetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileSetReqSend

  DESCRIPTION
    Creates or updates an existing profile in the NME that matches the unique
    identity of the profile. Each profile is identified by the combination of
    BSSID and SSID. The profile contains all the required credentials for
    attempting to connect to the network. Creating or updating a profile via
    the NME PROFILE SET REQ does NOT add the profile to the preferred profile
    list within the NME used for the NME auto-connect behaviour.

  PARAMETERS
    queue   - Message Source Task Queue (Cfm's will be sent to this Queue)
    profile - Specifies the identity and credentials of the network.

*******************************************************************************/
#define CsrWifiNmeProfileSetReqCreate(msg__, dst__, src__, profile__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileSetReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_SET_REQ, dst__, src__); \
    msg__->profile = (profile__);

#define CsrWifiNmeProfileSetReqSendTo(dst__, src__, profile__) \
    { \
        CsrWifiNmeProfileSetReq *msg__; \
        CsrWifiNmeProfileSetReqCreate(msg__, dst__, src__, profile__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileSetReqSend(src__, profile__) \
    CsrWifiNmeProfileSetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, profile__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileSetCfmSend

  DESCRIPTION
    Reports the status of the NME PROFILE SET REQ; the request will only fail
    if the details specified in the profile contains an invalid combination
    of parameters for example specifying the profile as cloaked but not
    specifying the SSID. The NME doesn't limit the number of profiles that
    may be created. The NME assumes that the entity configuring it is aware
    of the appropriate limits.

  PARAMETERS
    queue  - Destination Task Queue
    status - Indicates the success or otherwise of the requested operation.

*******************************************************************************/
#define CsrWifiNmeProfileSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileSetCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeProfileSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeProfileSetCfm *msg__; \
        CsrWifiNmeProfileSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileSetCfmSend(dst__, status__) \
    CsrWifiNmeProfileSetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileUpdateIndSend

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that informs that application that the contained profile has
    changed.
    For example, either the credentials EAP-FAST PAC file or the session data
    within the profile has changed.
    It is up to the application whether it stores this updated profile or
    not.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    profile      - The identity and credentials of the network.

*******************************************************************************/
#define CsrWifiNmeProfileUpdateIndCreate(msg__, dst__, src__, interfaceTag__, profile__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeProfileUpdateInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_PROFILE_UPDATE_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->profile = (profile__);

#define CsrWifiNmeProfileUpdateIndSendTo(dst__, src__, interfaceTag__, profile__) \
    { \
        CsrWifiNmeProfileUpdateInd *msg__; \
        CsrWifiNmeProfileUpdateIndCreate(msg__, dst__, src__, interfaceTag__, profile__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeProfileUpdateIndSend(dst__, interfaceTag__, profile__) \
    CsrWifiNmeProfileUpdateIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, profile__)

/*******************************************************************************

  NAME
    CsrWifiNmeSimGsmAuthIndSend

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the UICC Manager to perform a GSM
    authentication on behalf of the NME. This indication is generated when
    the NME is attempting to connect to a profile configured for EAP-SIM. An
    application MUST register to receive this indication for the NME to
    support the EAP-SIM credential types. Otherwise the NME has no route to
    obtain the information from the UICC. EAP-SIM authentication requires 2
    or 3 GSM authentication rounds and therefore 2 or 3 RANDS (GSM Random
    Challenges) are included.

  PARAMETERS
    queue       - Destination Task Queue
    randsLength - GSM RAND is 16 bytes long hence valid values are 32 (2 RANDS)
                  or 48 (3 RANDs).
    rands       - 2 or 3 RANDs values.

*******************************************************************************/
#define CsrWifiNmeSimGsmAuthIndCreate(msg__, dst__, src__, randsLength__, rands__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimGsmAuthInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_GSM_AUTH_IND, dst__, src__); \
    msg__->randsLength = (randsLength__); \
    msg__->rands = (rands__);

#define CsrWifiNmeSimGsmAuthIndSendTo(dst__, src__, randsLength__, rands__) \
    { \
        CsrWifiNmeSimGsmAuthInd *msg__; \
        CsrWifiNmeSimGsmAuthIndCreate(msg__, dst__, src__, randsLength__, rands__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimGsmAuthIndSend(dst__, randsLength__, rands__) \
    CsrWifiNmeSimGsmAuthIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, randsLength__, rands__)

/*******************************************************************************

  NAME
    CsrWifiNmeSimGsmAuthResSend

  DESCRIPTION
    Response from the application that received the NME SIM GSM AUTH IND. For
    each GSM authentication round a GSM Ciphering key (Kc) and a signed
    response (SRES) are produced. Since 2 or 3 GSM authentication rounds are
    used the 2 or 3 Kc's obtained respectively are combined into one buffer
    and similarly the 2 or 3 SRES's obtained are combined into another
    buffer. The order of Kc values (SRES values respectively) in their buffer
    is the same as that of their corresponding RAND values in the incoming
    indication.

  PARAMETERS
    status     - Indicates the outcome of the requested operation:
                 STATUS_SUCCESS or STATUS_ERROR
    kcsLength  - Length in Bytes of Kc buffer. Legal values are: 16 or 24.
    kcs        - Kc buffer holding 2 or 3 Kc values.
    sresLength - Length in Bytes of SRES buffer. Legal values are: 8 or 12.
    sres       - SRES buffer holding 2 or 3 SRES values.

*******************************************************************************/
#define CsrWifiNmeSimGsmAuthResCreate(msg__, dst__, src__, status__, kcsLength__, kcs__, sresLength__, sres__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimGsmAuthRes), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_GSM_AUTH_RES, dst__, src__); \
    msg__->status = (status__); \
    msg__->kcsLength = (kcsLength__); \
    msg__->kcs = (kcs__); \
    msg__->sresLength = (sresLength__); \
    msg__->sres = (sres__);

#define CsrWifiNmeSimGsmAuthResSendTo(dst__, src__, status__, kcsLength__, kcs__, sresLength__, sres__) \
    { \
        CsrWifiNmeSimGsmAuthRes *msg__; \
        CsrWifiNmeSimGsmAuthResCreate(msg__, dst__, src__, status__, kcsLength__, kcs__, sresLength__, sres__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimGsmAuthResSend(src__, status__, kcsLength__, kcs__, sresLength__, sres__) \
    CsrWifiNmeSimGsmAuthResSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, status__, kcsLength__, kcs__, sresLength__, sres__)

/*******************************************************************************

  NAME
    CsrWifiNmeSimImsiGetIndSend

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the IMSI and UICC type from the UICC Manager.
    This indication is generated when the NME is attempting to connect to a
    profile configured for EAP-SIM/AKA. An application MUST register to
    receive this indication for the NME to support the EAP-SIM/AKA credential
    types. Otherwise the NME has no route to obtain the information from the
    UICC.

  PARAMETERS
    queue  - Destination Task Queue

*******************************************************************************/
#define CsrWifiNmeSimImsiGetIndCreate(msg__, dst__, src__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimImsiGetInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_IMSI_GET_IND, dst__, src__);

#define CsrWifiNmeSimImsiGetIndSendTo(dst__, src__) \
    { \
        CsrWifiNmeSimImsiGetInd *msg__; \
        CsrWifiNmeSimImsiGetIndCreate(msg__, dst__, src__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimImsiGetIndSend(dst__) \
    CsrWifiNmeSimImsiGetIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE)

/*******************************************************************************

  NAME
    CsrWifiNmeSimImsiGetResSend

  DESCRIPTION
    Response from the application that received the NME SIM IMSI GET IND.

  PARAMETERS
    status   - Indicates the outcome of the requested operation: STATUS_SUCCESS
               or STATUS_ERROR.
    imsi     - The value of the IMSI obtained from the UICC.
    cardType - The UICC type (GSM only (SIM), UMTS only (USIM), Both).

*******************************************************************************/
#define CsrWifiNmeSimImsiGetResCreate(msg__, dst__, src__, status__, imsi__, cardType__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimImsiGetRes), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_IMSI_GET_RES, dst__, src__); \
    msg__->status = (status__); \
    msg__->imsi = (imsi__); \
    msg__->cardType = (cardType__);

#define CsrWifiNmeSimImsiGetResSendTo(dst__, src__, status__, imsi__, cardType__) \
    { \
        CsrWifiNmeSimImsiGetRes *msg__; \
        CsrWifiNmeSimImsiGetResCreate(msg__, dst__, src__, status__, imsi__, cardType__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimImsiGetResSend(src__, status__, imsi__, cardType__) \
    CsrWifiNmeSimImsiGetResSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, status__, imsi__, cardType__)

/*******************************************************************************

  NAME
    CsrWifiNmeSimUmtsAuthIndSend

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the UICC Manager to perform a UMTS
    authentication on behalf of the NME. This indication is generated when
    the NME is attempting to connect to a profile configured for EAP-AKA. An
    application MUST register to receive this indication for the NME to
    support the EAP-AKA credential types. Otherwise the NME has no route to
    obtain the information from the USIM. EAP-AKA requires one UMTS
    authentication round and therefore only one RAND and one AUTN values are
    included.

  PARAMETERS
    queue  - Destination Task Queue
    rand   - UMTS RAND value.
    autn   - UMTS AUTN value.

*******************************************************************************/
#define CsrWifiNmeSimUmtsAuthIndCreate(msg__, dst__, src__, rand__, autn__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimUmtsAuthInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_UMTS_AUTH_IND, dst__, src__); \
    memcpy(msg__->rand, (rand__), sizeof(u8) * 16); \
    memcpy(msg__->autn, (autn__), sizeof(u8) * 16);

#define CsrWifiNmeSimUmtsAuthIndSendTo(dst__, src__, rand__, autn__) \
    { \
        CsrWifiNmeSimUmtsAuthInd *msg__; \
        CsrWifiNmeSimUmtsAuthIndCreate(msg__, dst__, src__, rand__, autn__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimUmtsAuthIndSend(dst__, rand__, autn__) \
    CsrWifiNmeSimUmtsAuthIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, rand__, autn__)

/*******************************************************************************

  NAME
    CsrWifiNmeSimUmtsAuthResSend

  DESCRIPTION
    Response from the application that received the NME SIM UMTS AUTH IND.
    The values of umtsCipherKey, umtsIntegrityKey, resParameterLength and
    resParameter are only meanigful when result = UMTS_AUTH_RESULT_SUCCESS.
    The value of auts is only meaningful when
    result=UMTS_AUTH_RESULT_SYNC_FAIL.

  PARAMETERS
    status             - Indicates the outcome of the requested operation:
                         STATUS_SUCCESS or STATUS_ERROR.
    result             - The result of UMTS authentication as performed by the
                         UICC which could be: Success, Authentication Reject or
                         Synchronisation Failure. For all these 3 outcomes the
                         value of status is success.
    umtsCipherKey      - The UMTS Cipher Key as calculated and returned by the
                         UICC.
    umtsIntegrityKey   - The UMTS Integrity Key as calculated and returned by
                         the UICC.
    resParameterLength - The length (in bytes) of the RES parameter (min=4; max
                         = 16).
    resParameter       - The RES parameter as calculated and returned by the
                         UICC.
    auts               - The AUTS parameter as calculated and returned by the
                         UICC.

*******************************************************************************/
#define CsrWifiNmeSimUmtsAuthResCreate(msg__, dst__, src__, status__, result__, umtsCipherKey__, umtsIntegrityKey__, resParameterLength__, resParameter__, auts__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeSimUmtsAuthRes), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_SIM_UMTS_AUTH_RES, dst__, src__); \
    msg__->status = (status__); \
    msg__->result = (result__); \
    memcpy(msg__->umtsCipherKey, (umtsCipherKey__), sizeof(u8) * 16); \
    memcpy(msg__->umtsIntegrityKey, (umtsIntegrityKey__), sizeof(u8) * 16); \
    msg__->resParameterLength = (resParameterLength__); \
    msg__->resParameter = (resParameter__); \
    memcpy(msg__->auts, (auts__), sizeof(u8) * 14);

#define CsrWifiNmeSimUmtsAuthResSendTo(dst__, src__, status__, result__, umtsCipherKey__, umtsIntegrityKey__, resParameterLength__, resParameter__, auts__) \
    { \
        CsrWifiNmeSimUmtsAuthRes *msg__; \
        CsrWifiNmeSimUmtsAuthResCreate(msg__, dst__, src__, status__, result__, umtsCipherKey__, umtsIntegrityKey__, resParameterLength__, resParameter__, auts__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeSimUmtsAuthResSend(src__, status__, result__, umtsCipherKey__, umtsIntegrityKey__, resParameterLength__, resParameter__, auts__) \
    CsrWifiNmeSimUmtsAuthResSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, status__, result__, umtsCipherKey__, umtsIntegrityKey__, resParameterLength__, resParameter__, auts__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCancelReqSend

  DESCRIPTION
    Requests the NME to cancel any WPS procedure that it is currently
    performing. This includes WPS registrar activities started because of
    CSR_WIFI_NME_AP_REGISTER.request

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiNmeWpsCancelReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsCancelReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_CANCEL_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiNmeWpsCancelReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiNmeWpsCancelReq *msg__; \
        CsrWifiNmeWpsCancelReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsCancelReqSend(src__, interfaceTag__) \
    CsrWifiNmeWpsCancelReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCancelCfmSend

  DESCRIPTION
    Reports the status of the NME WPS REQ, the request is always SUCCESSFUL.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Only returns CSR_WIFI_NME_STATUS_SUCCESS

*******************************************************************************/
#define CsrWifiNmeWpsCancelCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsCancelCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_CANCEL_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiNmeWpsCancelCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiNmeWpsCancelCfm *msg__; \
        CsrWifiNmeWpsCancelCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsCancelCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiNmeWpsCancelCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCfmSend

  DESCRIPTION
    Reports the status of the NME WPS REQ.
    If CSR_WIFI_NME_STATUS_SUCCESS, the profile parameter contains the
    identity and credentials of the AP.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Indicates the success or otherwise of the requested
                   operation.
    profile      - This parameter is relevant only if
                   status==CSR_WIFI_NME_STATUS_SUCCESS.
                   The identity and credentials of the network.

*******************************************************************************/
#define CsrWifiNmeWpsCfmCreate(msg__, dst__, src__, interfaceTag__, status__, profile__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->profile = (profile__);

#define CsrWifiNmeWpsCfmSendTo(dst__, src__, interfaceTag__, status__, profile__) \
    { \
        CsrWifiNmeWpsCfm *msg__; \
        CsrWifiNmeWpsCfmCreate(msg__, dst__, src__, interfaceTag__, status__, profile__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsCfmSend(dst__, interfaceTag__, status__, profile__) \
    CsrWifiNmeWpsCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__, profile__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsConfigSetReqSend

  DESCRIPTION
    This primitive passes the WPS information for the device to NME. This may
    be accepted only if no interface is active.

  PARAMETERS
    queue     - Message Source Task Queue (Cfm's will be sent to this Queue)
    wpsConfig - WPS config.

*******************************************************************************/
#define CsrWifiNmeWpsConfigSetReqCreate(msg__, dst__, src__, wpsConfig__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsConfigSetReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_CONFIG_SET_REQ, dst__, src__); \
    msg__->wpsConfig = (wpsConfig__);

#define CsrWifiNmeWpsConfigSetReqSendTo(dst__, src__, wpsConfig__) \
    { \
        CsrWifiNmeWpsConfigSetReq *msg__; \
        CsrWifiNmeWpsConfigSetReqCreate(msg__, dst__, src__, wpsConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsConfigSetReqSend(src__, wpsConfig__) \
    CsrWifiNmeWpsConfigSetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, wpsConfig__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsConfigSetCfmSend

  DESCRIPTION
    Confirm.

  PARAMETERS
    queue  - Destination Task Queue
    status - Status of the request.

*******************************************************************************/
#define CsrWifiNmeWpsConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsConfigSetCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeWpsConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeWpsConfigSetCfm *msg__; \
        CsrWifiNmeWpsConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsConfigSetCfmSend(dst__, status__) \
    CsrWifiNmeWpsConfigSetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeWpsReqSend

  DESCRIPTION
    Requests the NME to look for WPS enabled APs and attempt to perform WPS
    to determine the appropriate security credentials to connect to the AP.
    If the PIN == '00000000' then 'push button mode' is indicated, otherwise
    the PIN has to match that of the AP. 4 digit pin is passed by sending the
    pin digits in pin[0]..pin[3] and rest of the contents filled with '-'.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface Identifier; unique identifier of an interface
    pin          - PIN value.
    ssid         - Service Set identifier
    bssid        - ID of Basic Service Set for which a WPS connection attempt is
                   being made.

*******************************************************************************/
#define CsrWifiNmeWpsReqCreate(msg__, dst__, src__, interfaceTag__, pin__, ssid__, bssid__) \
    msg__ = kmalloc(sizeof(CsrWifiNmeWpsReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_PRIM, CSR_WIFI_NME_WPS_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    memcpy(msg__->pin, (pin__), sizeof(u8) * 8); \
    msg__->ssid = (ssid__); \
    msg__->bssid = (bssid__);

#define CsrWifiNmeWpsReqSendTo(dst__, src__, interfaceTag__, pin__, ssid__, bssid__) \
    { \
        CsrWifiNmeWpsReq *msg__; \
        CsrWifiNmeWpsReqCreate(msg__, dst__, src__, interfaceTag__, pin__, ssid__, bssid__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_PRIM, msg__); \
    }

#define CsrWifiNmeWpsReqSend(src__, interfaceTag__, pin__, ssid__, bssid__) \
    CsrWifiNmeWpsReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, pin__, ssid__, bssid__)


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_NME_LIB_H__ */
