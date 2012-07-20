/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_CTRL_PRIM_H__
#define CSR_WIFI_ROUTER_CTRL_PRIM_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSR_WIFI_ROUTER_CTRL_PRIM                                       (0x0401)

typedef CsrPrim CsrWifiRouterCtrlPrim;

typedef CsrResult (*CsrWifiRouterCtrlRawSdioByteWrite)(u8 func, CsrUint32 address, u8 data);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioByteRead)(u8 func, CsrUint32 address, u8 *pdata);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioFirmwareDownload)(CsrUint32 length, const u8 *pdata);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioReset)(void);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioCoreDumpPrepare)(CsrBool suspendSme);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioByteBlockRead)(u8 func, CsrUint32 address, u8 *pdata, CsrUint32 length);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioGpRead16)(u8 func, CsrUint32 address, u16 *pdata);
typedef CsrResult (*CsrWifiRouterCtrlRawSdioGpWrite16)(u8 func, CsrUint32 address, u16 data);

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckRole

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR
                   -
    CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlBlockAckRole;
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR   ((CsrWifiRouterCtrlBlockAckRole) 0x00)
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT    ((CsrWifiRouterCtrlBlockAckRole) 0x01)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlControlIndication

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_ERROR
                   -
    CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_EXIT
                   -
    CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_USER_REQUESTED
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlControlIndication;
#define CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_ERROR            ((CsrWifiRouterCtrlControlIndication) 0x01)
#define CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_EXIT             ((CsrWifiRouterCtrlControlIndication) 0x02)
#define CSR_WIFI_ROUTER_CTRL_CONTROL_INDICATION_USER_REQUESTED   ((CsrWifiRouterCtrlControlIndication) 0x03)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlListAction

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_LIST_ACTION_GET
                   -
    CSR_WIFI_ROUTER_CTRL_LIST_ACTION_ADD
                   -
    CSR_WIFI_ROUTER_CTRL_LIST_ACTION_REMOVE
                   -
    CSR_WIFI_ROUTER_CTRL_LIST_ACTION_FLUSH
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlListAction;
#define CSR_WIFI_ROUTER_CTRL_LIST_ACTION_GET      ((CsrWifiRouterCtrlListAction) 0x00)
#define CSR_WIFI_ROUTER_CTRL_LIST_ACTION_ADD      ((CsrWifiRouterCtrlListAction) 0x01)
#define CSR_WIFI_ROUTER_CTRL_LIST_ACTION_REMOVE   ((CsrWifiRouterCtrlListAction) 0x02)
#define CSR_WIFI_ROUTER_CTRL_LIST_ACTION_FLUSH    ((CsrWifiRouterCtrlListAction) 0x03)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlLowPowerMode

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_LOW_POWER_MODE_DISABLED
                   -
    CSR_WIFI_ROUTER_CTRL_LOW_POWER_MODE_ENABLED
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlLowPowerMode;
#define CSR_WIFI_ROUTER_CTRL_LOW_POWER_MODE_DISABLED   ((CsrWifiRouterCtrlLowPowerMode) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_LOW_POWER_MODE_ENABLED    ((CsrWifiRouterCtrlLowPowerMode) 0x0001)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMediaStatus

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_CONNECTED
                   -
    CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_DISCONNECTED
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlMediaStatus;
#define CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_CONNECTED      ((CsrWifiRouterCtrlMediaStatus) 0x00)
#define CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_DISCONNECTED   ((CsrWifiRouterCtrlMediaStatus) 0x01)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMode

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_MODE_NONE    -
    CSR_WIFI_ROUTER_CTRL_MODE_IBSS    -
    CSR_WIFI_ROUTER_CTRL_MODE_STA     -
    CSR_WIFI_ROUTER_CTRL_MODE_AP      -
    CSR_WIFI_ROUTER_CTRL_MODE_MONITOR -
    CSR_WIFI_ROUTER_CTRL_MODE_AMP     -
    CSR_WIFI_ROUTER_CTRL_MODE_P2P     -
    CSR_WIFI_ROUTER_CTRL_MODE_P2PGO   -
    CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI  -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlMode;
#define CSR_WIFI_ROUTER_CTRL_MODE_NONE      ((CsrWifiRouterCtrlMode) 0x00)
#define CSR_WIFI_ROUTER_CTRL_MODE_IBSS      ((CsrWifiRouterCtrlMode) 0x01)
#define CSR_WIFI_ROUTER_CTRL_MODE_STA       ((CsrWifiRouterCtrlMode) 0x02)
#define CSR_WIFI_ROUTER_CTRL_MODE_AP        ((CsrWifiRouterCtrlMode) 0x03)
#define CSR_WIFI_ROUTER_CTRL_MODE_MONITOR   ((CsrWifiRouterCtrlMode) 0x04)
#define CSR_WIFI_ROUTER_CTRL_MODE_AMP       ((CsrWifiRouterCtrlMode) 0x05)
#define CSR_WIFI_ROUTER_CTRL_MODE_P2P       ((CsrWifiRouterCtrlMode) 0x06)
#define CSR_WIFI_ROUTER_CTRL_MODE_P2PGO     ((CsrWifiRouterCtrlMode) 0x07)
#define CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI    ((CsrWifiRouterCtrlMode) 0x08)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerStatus

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE
                   -
    CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE
                   -
    CSR_WIFI_ROUTER_CTRL_PEER_DISCONNECTED
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlPeerStatus;
#define CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE       ((CsrWifiRouterCtrlPeerStatus) 0x00)
#define CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE   ((CsrWifiRouterCtrlPeerStatus) 0x01)
#define CSR_WIFI_ROUTER_CTRL_PEER_DISCONNECTED           ((CsrWifiRouterCtrlPeerStatus) 0x02)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPortAction

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN
                   -
    CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD
                   -
    CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_BLOCK
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlPortAction;
#define CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN             ((CsrWifiRouterCtrlPortAction) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD   ((CsrWifiRouterCtrlPortAction) 0x0001)
#define CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_BLOCK     ((CsrWifiRouterCtrlPortAction) 0x0002)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPowersaveType

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_AC_BK_PS_INFO_PRESENT
                   - If set, AC BK PS info is present in b4 and b5
    CSR_WIFI_ROUTER_CTRL_AC_BE_PS_INFO_PRESENT
                   - If set, AC BE PS info is present in b6 and b7
    CSR_WIFI_ROUTER_CTRL_AC_VI_PS_INFO_PRESENT
                   - If set, AC VI PS info is present in b8 and b9
    CSR_WIFI_ROUTER_CTRL_AC_VO_PS_INFO_PRESENT
                   - If set, AC VO PS info is present in b10 and b11
    CSR_WIFI_ROUTER_CTRL_AC_BK_TRIGGER_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_BK_DELIVERY_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_BE_TRIGGER_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_BE_DELIVERY_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_VI_TRIGGER_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_VI_DELIVERY_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_VO_TRIGGER_ENABLED
                   -
    CSR_WIFI_ROUTER_CTRL_AC_VO_DELIVERY_ENABLED
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlPowersaveType;
#define CSR_WIFI_ROUTER_CTRL_AC_BK_PS_INFO_PRESENT    ((CsrWifiRouterCtrlPowersaveType) 0x0001)
#define CSR_WIFI_ROUTER_CTRL_AC_BE_PS_INFO_PRESENT    ((CsrWifiRouterCtrlPowersaveType) 0x0002)
#define CSR_WIFI_ROUTER_CTRL_AC_VI_PS_INFO_PRESENT    ((CsrWifiRouterCtrlPowersaveType) 0x0004)
#define CSR_WIFI_ROUTER_CTRL_AC_VO_PS_INFO_PRESENT    ((CsrWifiRouterCtrlPowersaveType) 0x0008)
#define CSR_WIFI_ROUTER_CTRL_AC_BK_TRIGGER_ENABLED    ((CsrWifiRouterCtrlPowersaveType) 0x0010)
#define CSR_WIFI_ROUTER_CTRL_AC_BK_DELIVERY_ENABLED   ((CsrWifiRouterCtrlPowersaveType) 0x0020)
#define CSR_WIFI_ROUTER_CTRL_AC_BE_TRIGGER_ENABLED    ((CsrWifiRouterCtrlPowersaveType) 0x0040)
#define CSR_WIFI_ROUTER_CTRL_AC_BE_DELIVERY_ENABLED   ((CsrWifiRouterCtrlPowersaveType) 0x0080)
#define CSR_WIFI_ROUTER_CTRL_AC_VI_TRIGGER_ENABLED    ((CsrWifiRouterCtrlPowersaveType) 0x0100)
#define CSR_WIFI_ROUTER_CTRL_AC_VI_DELIVERY_ENABLED   ((CsrWifiRouterCtrlPowersaveType) 0x0200)
#define CSR_WIFI_ROUTER_CTRL_AC_VO_TRIGGER_ENABLED    ((CsrWifiRouterCtrlPowersaveType) 0x0400)
#define CSR_WIFI_ROUTER_CTRL_AC_VO_DELIVERY_ENABLED   ((CsrWifiRouterCtrlPowersaveType) 0x0800)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlProtocolDirection

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_RX
                   -
    CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlProtocolDirection;
#define CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_RX   ((CsrWifiRouterCtrlProtocolDirection) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX   ((CsrWifiRouterCtrlProtocolDirection) 0x0001)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlQoSControl

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_OFF
                   -
    CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_WMM_ON
                   -
    CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_80211_ON
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlQoSControl;
#define CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_OFF        ((CsrWifiRouterCtrlQoSControl) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_WMM_ON     ((CsrWifiRouterCtrlQoSControl) 0x0001)
#define CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_80211_ON   ((CsrWifiRouterCtrlQoSControl) 0x0002)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlQueueConfig

  DESCRIPTION
    Defines which Queues are enabled for use.

 VALUES
    CSR_WIFI_ROUTER_CTRL_QUEUE_BE_ENABLE
                   -
    CSR_WIFI_ROUTER_CTRL_QUEUE_BK_ENABLE
                   -
    CSR_WIFI_ROUTER_CTRL_QUEUE_VI_ENABLE
                   -
    CSR_WIFI_ROUTER_CTRL_QUEUE_VO_ENABLE
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlQueueConfig;
#define CSR_WIFI_ROUTER_CTRL_QUEUE_BE_ENABLE   ((CsrWifiRouterCtrlQueueConfig) 0x01)
#define CSR_WIFI_ROUTER_CTRL_QUEUE_BK_ENABLE   ((CsrWifiRouterCtrlQueueConfig) 0x02)
#define CSR_WIFI_ROUTER_CTRL_QUEUE_VI_ENABLE   ((CsrWifiRouterCtrlQueueConfig) 0x04)
#define CSR_WIFI_ROUTER_CTRL_QUEUE_VO_ENABLE   ((CsrWifiRouterCtrlQueueConfig) 0x08)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficConfigType

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_RESET
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_FILTER
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_CLS
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlTrafficConfigType;
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_RESET    ((CsrWifiRouterCtrlTrafficConfigType) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_FILTER   ((CsrWifiRouterCtrlTrafficConfigType) 0x0001)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_CLS      ((CsrWifiRouterCtrlTrafficConfigType) 0x0002)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficPacketType

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_NONE
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_EAPOL
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP_ACK
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ARP
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_AIRONET
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ALL
                   -

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlTrafficPacketType;
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_NONE       ((CsrWifiRouterCtrlTrafficPacketType) 0x0000)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_EAPOL      ((CsrWifiRouterCtrlTrafficPacketType) 0x0001)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP       ((CsrWifiRouterCtrlTrafficPacketType) 0x0002)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP_ACK   ((CsrWifiRouterCtrlTrafficPacketType) 0x0004)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ARP        ((CsrWifiRouterCtrlTrafficPacketType) 0x0008)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_AIRONET    ((CsrWifiRouterCtrlTrafficPacketType) 0x0010)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM     ((CsrWifiRouterCtrlTrafficPacketType) 0x0020)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ALL        ((CsrWifiRouterCtrlTrafficPacketType) 0x00FF)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficType

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_OCCASIONAL
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_BURSTY
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_PERIODIC
                   -
    CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_CONTINUOUS
                   -

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlTrafficType;
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_OCCASIONAL   ((CsrWifiRouterCtrlTrafficType) 0x00)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_BURSTY       ((CsrWifiRouterCtrlTrafficType) 0x01)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_PERIODIC     ((CsrWifiRouterCtrlTrafficType) 0x02)
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_CONTINUOUS   ((CsrWifiRouterCtrlTrafficType) 0x03)


/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerRecordHandle

  DESCRIPTION

*******************************************************************************/
typedef CsrUint32 CsrWifiRouterCtrlPeerRecordHandle;
/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPowersaveTypeMask

  DESCRIPTION
    Mask type for use with the values defined by
    CsrWifiRouterCtrlPowersaveType

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlPowersaveTypeMask;
/*******************************************************************************

  NAME
    CsrWifiRouterCtrlQueueConfigMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiRouterCtrlQueueConfig

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlQueueConfigMask;
/*******************************************************************************

  NAME
    CsrWifiRouterCtrlRequestorInfo

  DESCRIPTION

*******************************************************************************/
typedef u16 CsrWifiRouterCtrlRequestorInfo;
/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficStreamId

  DESCRIPTION

*******************************************************************************/
typedef u8 CsrWifiRouterCtrlTrafficStreamId;


/*******************************************************************************

  NAME
    CsrWifiRouterCtrlSmeVersions

  DESCRIPTION

  MEMBERS
    firmwarePatch -
    smeBuild      -
    smeHip        -

*******************************************************************************/
typedef struct
{
    CsrUint32      firmwarePatch;
    CsrCharString *smeBuild;
    CsrUint32      smeHip;
} CsrWifiRouterCtrlSmeVersions;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlStaInfo

  DESCRIPTION

  MEMBERS
    wmmOrQosEnabled     -
    powersaveMode       -
    maxSpLength         -
    listenIntervalInTus -

*******************************************************************************/
typedef struct
{
    CsrBool                            wmmOrQosEnabled;
    CsrWifiRouterCtrlPowersaveTypeMask powersaveMode;
    u8                           maxSpLength;
    u16                          listenIntervalInTus;
} CsrWifiRouterCtrlStaInfo;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficFilter

  DESCRIPTION

  MEMBERS
    etherType     -
    ipType        -
    udpSourcePort -
    udpDestPort   -

*******************************************************************************/
typedef struct
{
    CsrUint32 etherType;
    u8  ipType;
    CsrUint32 udpSourcePort;
    CsrUint32 udpDestPort;
} CsrWifiRouterCtrlTrafficFilter;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficStats

  DESCRIPTION

  MEMBERS
    rxMeanRate   - Mean rx data rate over the interval
    rxFramesNum  - Keep number of Rx frames per second, for CYCLE_3.
    txFramesNum  - Keep number of Tx frames per second, for CYCLE_3.
    rxBytesCount - Keep calculated Rx throughput per second, for CYCLE_2.
    txBytesCount - Keep calculated Tx throughput per second, for CYCLE_2.
    intervals    - array size 11 MUST match TA_INTERVALS_NUM

*******************************************************************************/
typedef struct
{
    CsrUint32 rxMeanRate;
    CsrUint32 rxFramesNum;
    CsrUint32 txFramesNum;
    CsrUint32 rxBytesCount;
    CsrUint32 txBytesCount;
    u8  intervals[11];
} CsrWifiRouterCtrlTrafficStats;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlVersions

  DESCRIPTION

  MEMBERS
    chipId        -
    chipVersion   -
    firmwareBuild -
    firmwareHip   -
    routerBuild   -
    routerHip     -

*******************************************************************************/
typedef struct
{
    CsrUint32      chipId;
    CsrUint32      chipVersion;
    CsrUint32      firmwareBuild;
    CsrUint32      firmwareHip;
    CsrCharString *routerBuild;
    CsrUint32      routerHip;
} CsrWifiRouterCtrlVersions;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficConfig

  DESCRIPTION

  MEMBERS
    packetFilter -
    customFilter -

*******************************************************************************/
typedef struct
{
    u16                      packetFilter;
    CsrWifiRouterCtrlTrafficFilter customFilter;
} CsrWifiRouterCtrlTrafficConfig;


/* Downstream */
#define CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_ROUTER_CTRL_CONFIGURE_POWER_MODE_REQ     ((CsrWifiRouterCtrlPrim) (0x0000 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_HIP_REQ                      ((CsrWifiRouterCtrlPrim) (0x0001 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_REQ             ((CsrWifiRouterCtrlPrim) (0x0002 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MULTICAST_ADDRESS_RES        ((CsrWifiRouterCtrlPrim) (0x0003 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PORT_CONFIGURE_REQ           ((CsrWifiRouterCtrlPrim) (0x0004 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_REQ              ((CsrWifiRouterCtrlPrim) (0x0005 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_SUSPEND_RES                  ((CsrWifiRouterCtrlPrim) (0x0006 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TCLAS_ADD_REQ                ((CsrWifiRouterCtrlPrim) (0x0007 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RESUME_RES                   ((CsrWifiRouterCtrlPrim) (0x0008 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RAW_SDIO_DEINITIALISE_REQ    ((CsrWifiRouterCtrlPrim) (0x0009 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RAW_SDIO_INITIALISE_REQ      ((CsrWifiRouterCtrlPrim) (0x000A + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TCLAS_DEL_REQ                ((CsrWifiRouterCtrlPrim) (0x000B + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_CLASSIFICATION_REQ   ((CsrWifiRouterCtrlPrim) (0x000C + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_REQ           ((CsrWifiRouterCtrlPrim) (0x000D + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_OFF_REQ                 ((CsrWifiRouterCtrlPrim) (0x000E + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_OFF_RES                 ((CsrWifiRouterCtrlPrim) (0x000F + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_ON_REQ                  ((CsrWifiRouterCtrlPrim) (0x0010 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_ON_RES                  ((CsrWifiRouterCtrlPrim) (0x0011 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_M4_TRANSMIT_REQ              ((CsrWifiRouterCtrlPrim) (0x0012 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MODE_SET_REQ                 ((CsrWifiRouterCtrlPrim) (0x0013 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_ADD_REQ                 ((CsrWifiRouterCtrlPrim) (0x0014 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_DEL_REQ                 ((CsrWifiRouterCtrlPrim) (0x0015 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_UPDATE_REQ              ((CsrWifiRouterCtrlPrim) (0x0016 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_CAPABILITIES_REQ             ((CsrWifiRouterCtrlPrim) (0x0017 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ENABLE_REQ         ((CsrWifiRouterCtrlPrim) (0x0018 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_DISABLE_REQ        ((CsrWifiRouterCtrlPrim) (0x0019 + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_RX_PKT_REQ              ((CsrWifiRouterCtrlPrim) (0x001A + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_MULTICAST_FILTER_REQ    ((CsrWifiRouterCtrlPrim) (0x001B + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_FILTER_REQ      ((CsrWifiRouterCtrlPrim) (0x001C + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_TX_PKT_REQ      ((CsrWifiRouterCtrlPrim) (0x001D + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_FILTER_REQ              ((CsrWifiRouterCtrlPrim) (0x001E + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_HIGHEST           (0x001E + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_ROUTER_CTRL_HIP_IND                      ((CsrWifiRouterCtrlPrim)(0x0000 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MULTICAST_ADDRESS_IND        ((CsrWifiRouterCtrlPrim)(0x0001 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PORT_CONFIGURE_CFM           ((CsrWifiRouterCtrlPrim)(0x0002 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RESUME_IND                   ((CsrWifiRouterCtrlPrim)(0x0003 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_SUSPEND_IND                  ((CsrWifiRouterCtrlPrim)(0x0004 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TCLAS_ADD_CFM                ((CsrWifiRouterCtrlPrim)(0x0005 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RAW_SDIO_DEINITIALISE_CFM    ((CsrWifiRouterCtrlPrim)(0x0006 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_RAW_SDIO_INITIALISE_CFM      ((CsrWifiRouterCtrlPrim)(0x0007 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TCLAS_DEL_CFM                ((CsrWifiRouterCtrlPrim)(0x0008 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_PROTOCOL_IND         ((CsrWifiRouterCtrlPrim)(0x0009 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_TRAFFIC_SAMPLE_IND           ((CsrWifiRouterCtrlPrim)(0x000A + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_OFF_IND                 ((CsrWifiRouterCtrlPrim)(0x000B + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_OFF_CFM                 ((CsrWifiRouterCtrlPrim)(0x000C + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_ON_IND                  ((CsrWifiRouterCtrlPrim)(0x000D + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WIFI_ON_CFM                  ((CsrWifiRouterCtrlPrim)(0x000E + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_M4_READY_TO_SEND_IND         ((CsrWifiRouterCtrlPrim)(0x000F + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_M4_TRANSMITTED_IND           ((CsrWifiRouterCtrlPrim)(0x0010 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MIC_FAILURE_IND              ((CsrWifiRouterCtrlPrim)(0x0011 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_CONNECTED_IND                ((CsrWifiRouterCtrlPrim)(0x0012 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_ADD_CFM                 ((CsrWifiRouterCtrlPrim)(0x0013 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_DEL_CFM                 ((CsrWifiRouterCtrlPrim)(0x0014 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_UNEXPECTED_FRAME_IND         ((CsrWifiRouterCtrlPrim)(0x0015 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_PEER_UPDATE_CFM              ((CsrWifiRouterCtrlPrim)(0x0016 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_CAPABILITIES_CFM             ((CsrWifiRouterCtrlPrim)(0x0017 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ENABLE_CFM         ((CsrWifiRouterCtrlPrim)(0x0018 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_DISABLE_CFM        ((CsrWifiRouterCtrlPrim)(0x0019 + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ERROR_IND          ((CsrWifiRouterCtrlPrim)(0x001A + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_STA_INACTIVE_IND             ((CsrWifiRouterCtrlPrim)(0x001B + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_RX_MIC_CHECK_IND        ((CsrWifiRouterCtrlPrim)(0x001C + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_MODE_SET_CFM                 ((CsrWifiRouterCtrlPrim)(0x001D + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_TX_ENCRYPT_IND  ((CsrWifiRouterCtrlPrim)(0x001E + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_HIGHEST             (0x001E + CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_COUNT               (CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlConfigurePowerModeReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    mode       -
    wakeHost   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiRouterCtrlLowPowerMode  mode;
    CsrBool                        wakeHost;
} CsrWifiRouterCtrlConfigurePowerModeReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlHipReq

  DESCRIPTION
    This primitive is used for transferring MLME messages to the HIP.

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    mlmeCommandLength - Length of the MLME signal
    mlmeCommand       - Pointer to the MLME signal
    dataRef1Length    - Length of the dataRef1 bulk data
    dataRef1          - Pointer to the bulk data 1
    dataRef2Length    - Length of the dataRef2 bulk data
    dataRef2          - Pointer to the bulk data 2

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       mlmeCommandLength;
    u8       *mlmeCommand;
    u16       dataRef1Length;
    u8       *dataRef1;
    u16       dataRef2Length;
    u8       *dataRef2;
} CsrWifiRouterCtrlHipReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMediaStatusReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -
    mediaStatus  -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiRouterCtrlMediaStatus   mediaStatus;
} CsrWifiRouterCtrlMediaStatusReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMulticastAddressRes

  DESCRIPTION

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      -
    clientData        -
    status            -
    action            -
    getAddressesCount -
    getAddresses      -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
    CsrWifiRouterCtrlListAction    action;
    u8                       getAddressesCount;
    CsrWifiMacAddress             *getAddresses;
} CsrWifiRouterCtrlMulticastAddressRes;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPortConfigureReq

  DESCRIPTION

  MEMBERS
    common                 - Common header for use with the CsrWifiFsm Module
    interfaceTag           -
    clientData             -
    uncontrolledPortAction -
    controlledPortAction   -
    macAddress             -
    setProtection          -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiRouterCtrlPortAction    uncontrolledPortAction;
    CsrWifiRouterCtrlPortAction    controlledPortAction;
    CsrWifiMacAddress              macAddress;
    CsrBool                        setProtection;
} CsrWifiRouterCtrlPortConfigureReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlQosControlReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -
    control      -
    queueConfig  -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                  common;
    u16                        interfaceTag;
    CsrWifiRouterCtrlRequestorInfo   clientData;
    CsrWifiRouterCtrlQoSControl      control;
    CsrWifiRouterCtrlQueueConfigMask queueConfig;
} CsrWifiRouterCtrlQosControlReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlSuspendRes

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    status     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
} CsrWifiRouterCtrlSuspendRes;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTclasAddReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -
    tclasLength  -
    tclas        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      tclasLength;
    u8                      *tclas;
} CsrWifiRouterCtrlTclasAddReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlResumeRes

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    status     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
} CsrWifiRouterCtrlResumeRes;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlRawSdioDeinitialiseReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlRawSdioDeinitialiseReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlRawSdioInitialiseReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlRawSdioInitialiseReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTclasDelReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -
    tclasLength  -
    tclas        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      tclasLength;
    u8                      *tclas;
} CsrWifiRouterCtrlTclasDelReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficClassificationReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -
    trafficType  -
    period       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiRouterCtrlTrafficType   trafficType;
    u16                      period;
} CsrWifiRouterCtrlTrafficClassificationReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficConfigReq

  DESCRIPTION

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      -
    clientData        -
    trafficConfigType -
    config            -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                    common;
    u16                          interfaceTag;
    CsrWifiRouterCtrlRequestorInfo     clientData;
    CsrWifiRouterCtrlTrafficConfigType trafficConfigType;
    CsrWifiRouterCtrlTrafficConfig     config;
} CsrWifiRouterCtrlTrafficConfigReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOffReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlWifiOffReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOffRes

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlWifiOffRes;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOnReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    dataLength - Number of bytes in the buffer pointed to by 'data'
    data       - Pointer to the buffer containing 'dataLength' bytes

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrUint32                      dataLength;
    u8                      *data;
} CsrWifiRouterCtrlWifiOnReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOnRes

  DESCRIPTION

  MEMBERS
    common              - Common header for use with the CsrWifiFsm Module
    clientData          -
    status              -
    numInterfaceAddress -
    stationMacAddress   - array size 1 MUST match CSR_WIFI_NUM_INTERFACES
    smeVersions         -
    scheduledInterrupt  -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
    u16                      numInterfaceAddress;
    CsrWifiMacAddress              stationMacAddress[2];
    CsrWifiRouterCtrlSmeVersions   smeVersions;
    CsrBool                        scheduledInterrupt;
} CsrWifiRouterCtrlWifiOnRes;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlM4TransmitReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    clientData   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlM4TransmitReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlModeSetReq

  DESCRIPTION

  MEMBERS
    common              - Common header for use with the CsrWifiFsm Module
    interfaceTag        -
    clientData          -
    mode                -
    bssid               - BSSID of the network the device is going to be a part
                          of
    protection          - Set to TRUE if encryption is enabled for the
                          connection/broadcast frames
    intraBssDistEnabled - If set to TRUE, intra BSS destribution will be
                          enabled. If set to FALSE, any unicast PDU which does
                          not have the RA as the the local MAC address, shall be
                          ignored. This field is interpreted by the receive if
                          mode is set to CSR_WIFI_ROUTER_CTRL_MODE_P2PGO

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiRouterCtrlMode          mode;
    CsrWifiMacAddress              bssid;
    CsrBool                        protection;
    CsrBool                        intraBssDistEnabled;
} CsrWifiRouterCtrlModeSetReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerAddReq

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   -
    clientData     -
    peerMacAddress -
    associationId  -
    staInfo        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrWifiMacAddress              peerMacAddress;
    u16                      associationId;
    CsrWifiRouterCtrlStaInfo       staInfo;
} CsrWifiRouterCtrlPeerAddReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerDelReq

  DESCRIPTION

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     -
    clientData       -
    peerRecordHandle -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                   common;
    u16                         interfaceTag;
    CsrWifiRouterCtrlRequestorInfo    clientData;
    CsrWifiRouterCtrlPeerRecordHandle peerRecordHandle;
} CsrWifiRouterCtrlPeerDelReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerUpdateReq

  DESCRIPTION

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     -
    clientData       -
    peerRecordHandle -
    powersaveMode    -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                    common;
    u16                          interfaceTag;
    CsrWifiRouterCtrlRequestorInfo     clientData;
    CsrWifiRouterCtrlPeerRecordHandle  peerRecordHandle;
    CsrWifiRouterCtrlPowersaveTypeMask powersaveMode;
} CsrWifiRouterCtrlPeerUpdateReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlCapabilitiesReq

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlCapabilitiesReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckEnableReq

  DESCRIPTION

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    -
    clientData      -
    macAddress      -
    trafficStreamID -
    role            -
    bufferSize      -
    timeout         -
    ssn             -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                  common;
    u16                        interfaceTag;
    CsrWifiRouterCtrlRequestorInfo   clientData;
    CsrWifiMacAddress                macAddress;
    CsrWifiRouterCtrlTrafficStreamId trafficStreamID;
    CsrWifiRouterCtrlBlockAckRole    role;
    u16                        bufferSize;
    u16                        timeout;
    u16                        ssn;
} CsrWifiRouterCtrlBlockAckEnableReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckDisableReq

  DESCRIPTION

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    -
    clientData      -
    macAddress      -
    trafficStreamID -
    role            -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                  common;
    u16                        interfaceTag;
    CsrWifiRouterCtrlRequestorInfo   clientData;
    CsrWifiMacAddress                macAddress;
    CsrWifiRouterCtrlTrafficStreamId trafficStreamID;
    CsrWifiRouterCtrlBlockAckRole    role;
} CsrWifiRouterCtrlBlockAckDisableReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiRxPktReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    signalLength -
    signal       -
    dataLength   -
    data         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u16       signalLength;
    u8       *signal;
    u16       dataLength;
    u8       *data;
} CsrWifiRouterCtrlWapiRxPktReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiMulticastFilterReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u8        status;
} CsrWifiRouterCtrlWapiMulticastFilterReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiUnicastFilterReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u8        status;
} CsrWifiRouterCtrlWapiUnicastFilterReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiUnicastTxPktReq

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    dataLength   -
    data         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u16       dataLength;
    u8       *data;
} CsrWifiRouterCtrlWapiUnicastTxPktReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiFilterReq

  DESCRIPTION

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    -
    isWapiConnected -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrBool         isWapiConnected;
} CsrWifiRouterCtrlWapiFilterReq;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlHipInd

  DESCRIPTION
    This primitive is used for transferring MLME messages from the HIP.

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    mlmeCommandLength - Length of the MLME signal
    mlmeCommand       - Pointer to the MLME signal
    dataRef1Length    - Length of the dataRef1 bulk data
    dataRef1          - Pointer to the bulk data 1
    dataRef2Length    - Length of the dataRef2 bulk data
    dataRef2          - Pointer to the bulk data 2

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       mlmeCommandLength;
    u8       *mlmeCommand;
    u16       dataRef1Length;
    u8       *dataRef1;
    u16       dataRef2Length;
    u8       *dataRef2;
} CsrWifiRouterCtrlHipInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMulticastAddressInd

  DESCRIPTION

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    clientData        -
    interfaceTag      -
    action            -
    setAddressesCount -
    setAddresses      -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiRouterCtrlListAction    action;
    u8                       setAddressesCount;
    CsrWifiMacAddress             *setAddresses;
} CsrWifiRouterCtrlMulticastAddressInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPortConfigureCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -
    macAddress   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
    CsrWifiMacAddress              macAddress;
} CsrWifiRouterCtrlPortConfigureCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlResumeInd

  DESCRIPTION

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    clientData      -
    powerMaintained -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrBool                        powerMaintained;
} CsrWifiRouterCtrlResumeInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlSuspendInd

  DESCRIPTION

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    clientData  -
    hardSuspend -
    d3Suspend   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrBool                        hardSuspend;
    CsrBool                        d3Suspend;
} CsrWifiRouterCtrlSuspendInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTclasAddCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlTclasAddCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlRawSdioDeinitialiseCfm

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    result     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      result;
} CsrWifiRouterCtrlRawSdioDeinitialiseCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlRawSdioInitialiseCfm

  DESCRIPTION

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    clientData       -
    result           -
    byteRead         -
    byteWrite        -
    firmwareDownload -
    reset            -
    coreDumpPrepare  -
    byteBlockRead    -
    gpRead16         -
    gpWrite16        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                          common;
    CsrWifiRouterCtrlRequestorInfo           clientData;
    CsrResult                                result;
    CsrWifiRouterCtrlRawSdioByteRead         byteRead;
    CsrWifiRouterCtrlRawSdioByteWrite        byteWrite;
    CsrWifiRouterCtrlRawSdioFirmwareDownload firmwareDownload;
    CsrWifiRouterCtrlRawSdioReset            reset;
    CsrWifiRouterCtrlRawSdioCoreDumpPrepare  coreDumpPrepare;
    CsrWifiRouterCtrlRawSdioByteBlockRead    byteBlockRead;
    CsrWifiRouterCtrlRawSdioGpRead16         gpRead16;
    CsrWifiRouterCtrlRawSdioGpWrite16        gpWrite16;
} CsrWifiRouterCtrlRawSdioInitialiseCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTclasDelCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlTclasDelCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficProtocolInd

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    packetType   -
    direction    -
    srcAddress   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                    common;
    CsrWifiRouterCtrlRequestorInfo     clientData;
    u16                          interfaceTag;
    CsrWifiRouterCtrlTrafficPacketType packetType;
    CsrWifiRouterCtrlProtocolDirection direction;
    CsrWifiMacAddress                  srcAddress;
} CsrWifiRouterCtrlTrafficProtocolInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlTrafficSampleInd

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    stats        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiRouterCtrlTrafficStats  stats;
} CsrWifiRouterCtrlTrafficSampleInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOffInd

  DESCRIPTION

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    clientData        -
    controlIndication -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                    common;
    CsrWifiRouterCtrlRequestorInfo     clientData;
    CsrWifiRouterCtrlControlIndication controlIndication;
} CsrWifiRouterCtrlWifiOffInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOffCfm

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
} CsrWifiRouterCtrlWifiOffCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOnInd

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    status     -
    versions   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
    CsrWifiRouterCtrlVersions      versions;
} CsrWifiRouterCtrlWifiOnInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWifiOnCfm

  DESCRIPTION

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    clientData -
    status     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    CsrResult                      status;
} CsrWifiRouterCtrlWifiOnCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlM4ReadyToSendInd

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    clientData     -
    interfaceTag   -
    peerMacAddress -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              peerMacAddress;
} CsrWifiRouterCtrlM4ReadyToSendInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlM4TransmittedInd

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    clientData     -
    interfaceTag   -
    peerMacAddress -
    status         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              peerMacAddress;
    CsrResult                      status;
} CsrWifiRouterCtrlM4TransmittedInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlMicFailureInd

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    clientData     -
    interfaceTag   -
    peerMacAddress -
    unicastPdu     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              peerMacAddress;
    CsrBool                        unicastPdu;
} CsrWifiRouterCtrlMicFailureInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlConnectedInd

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    clientData     -
    interfaceTag   -
    peerMacAddress -
    peerStatus     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              peerMacAddress;
    CsrWifiRouterCtrlPeerStatus    peerStatus;
} CsrWifiRouterCtrlConnectedInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerAddCfm

  DESCRIPTION

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    clientData       -
    interfaceTag     -
    peerMacAddress   -
    peerRecordHandle -
    status           -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                   common;
    CsrWifiRouterCtrlRequestorInfo    clientData;
    u16                         interfaceTag;
    CsrWifiMacAddress                 peerMacAddress;
    CsrWifiRouterCtrlPeerRecordHandle peerRecordHandle;
    CsrResult                         status;
} CsrWifiRouterCtrlPeerAddCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerDelCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlPeerDelCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlUnexpectedFrameInd

  DESCRIPTION

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    clientData     -
    interfaceTag   -
    peerMacAddress -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              peerMacAddress;
} CsrWifiRouterCtrlUnexpectedFrameInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlPeerUpdateCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlPeerUpdateCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlCapabilitiesCfm

  DESCRIPTION
    The router sends this primitive to confirm the size of the queues of the
    HIP.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    clientData       -
    commandQueueSize - Size of command queue
    trafficQueueSize - Size of traffic queue (per AC)

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      commandQueueSize;
    u16                      trafficQueueSize;
} CsrWifiRouterCtrlCapabilitiesCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckEnableCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlBlockAckEnableCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckDisableCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrResult                      status;
} CsrWifiRouterCtrlBlockAckDisableCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlBlockAckErrorInd

  DESCRIPTION

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    clientData      -
    interfaceTag    -
    trafficStreamID -
    peerMacAddress  -
    status          -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                  common;
    CsrWifiRouterCtrlRequestorInfo   clientData;
    u16                        interfaceTag;
    CsrWifiRouterCtrlTrafficStreamId trafficStreamID;
    CsrWifiMacAddress                peerMacAddress;
    CsrResult                        status;
} CsrWifiRouterCtrlBlockAckErrorInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlStaInactiveInd

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    staAddress   -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiMacAddress              staAddress;
} CsrWifiRouterCtrlStaInactiveInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiRxMicCheckInd

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    signalLength -
    signal       -
    dataLength   -
    data         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    u16                      signalLength;
    u8                      *signal;
    u16                      dataLength;
    u8                      *data;
} CsrWifiRouterCtrlWapiRxMicCheckInd;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlModeSetCfm

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    mode         -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    CsrWifiRouterCtrlMode          mode;
    CsrResult                      status;
} CsrWifiRouterCtrlModeSetCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterCtrlWapiUnicastTxEncryptInd

  DESCRIPTION

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    clientData   -
    interfaceTag -
    dataLength   -
    data         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrWifiRouterCtrlRequestorInfo clientData;
    u16                      interfaceTag;
    u16                      dataLength;
    u8                      *data;
} CsrWifiRouterCtrlWapiUnicastTxEncryptInd;


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_ROUTER_CTRL_PRIM_H__ */

