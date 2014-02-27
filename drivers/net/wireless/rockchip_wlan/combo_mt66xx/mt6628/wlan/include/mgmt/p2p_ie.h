#ifndef _P2P_IE_H
#define _P2P_IE_H

#if CFG_SUPPORT_WFD

#define ELEM_MAX_LEN_WFD 62             // TODO: Move to appropriate place


/*---------------- WFD Data Element Definitions ----------------*/
/* WFD 4.1.1 - WFD IE format */
#define WFD_OUI_TYPE_LEN                            4
#define WFD_IE_OUI_HDR                              (ELEM_HDR_LEN + WFD_OUI_TYPE_LEN) /* == OFFSET_OF(IE_P2P_T, aucP2PAttributes[0]) */

/* WFD 4.1.1 - General WFD Attribute */
#define WFD_ATTRI_HDR_LEN                           3 /* ID(1 octet) + Length(2 octets) */

/* WFD Attribute Code */
#define WFD_ATTRI_ID_DEV_INFO                                 0
#define WFD_ATTRI_ID_ASSOC_BSSID                          1
#define WFD_ATTRI_ID_COUPLED_SINK_INFO                 6
#define WFD_ATTRI_ID_EXT_CAPABILITY                        7
#define WFD_ATTRI_ID_SESSION_INFO                           9
#define WFD_ATTRI_ID_ALTER_MAC_ADDRESS                10

/* Maximum Length of WFD Attributes */
#define WFD_ATTRI_MAX_LEN_DEV_INFO                              6 /* 0 */
#define WFD_ATTRI_MAX_LEN_ASSOC_BSSID                       6 /* 1 */
#define WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO              7 /* 6 */
#define WFD_ATTRI_MAX_LEN_EXT_CAPABILITY                     2 /* 7 */
#define WFD_ATTRI_MAX_LEN_SESSION_INFO                      0 /* 9 */ /* 24 * #Clients */
#define WFD_ATTRI_MAX_LEN_ALTER_MAC_ADDRESS            6 /* 10 */





/* WFD 1.10 5.1.1 */
typedef struct _IE_WFD_T {
    UINT_8      ucId;                   /* Element ID */
    UINT_8      ucLength;               /* Length */
    UINT_8      aucOui[3];              /* OUI */
    UINT_8      ucOuiType;              /* OUI Type */
    UINT_8      aucWFDAttributes[1];    /* WFD Subelement */
} __KAL_ATTRIB_PACKED__ IE_WFD_T, *P_IE_WFD_T;

typedef struct _WFD_ATTRIBUTE_T {
    UINT_8     ucElemID;                   /* Subelement ID */
    UINT_16   u2Length;               /* Length */
    UINT_8     aucBody[1];             /* Body field */
} __KAL_ATTRIB_PACKED__ WFD_ATTRIBUTE_T, *P_WFD_ATTRIBUTE_T;

typedef struct _WFD_DEVICE_INFORMATION_IE_T {
    UINT_8 ucElemID;
    UINT_16 u2Length;
    UINT_16 u2WfdDevInfo;
    UINT_16 u2SessionMgmtCtrlPort;
    UINT_16 u2WfdDevMaxSpeed;
} __KAL_ATTRIB_PACKED__ WFD_DEVICE_INFORMATION_IE_T, *P_WFD_DEVICE_INFORMATION_IE_T;

typedef struct _WFD_ASSOCIATED_BSSID_IE_T {
    UINT_8 ucElemID;
    UINT_16 u2Length;
    UINT_8 aucAssocBssid[MAC_ADDR_LEN];
} __KAL_ATTRIB_PACKED__ WFD_ASSOCIATED_BSSID_IE_T, *P_WFD_ASSOCIATED_BSSID_IE_T;

typedef struct _WFD_COUPLE_SINK_INFORMATION_IE_T {
    UINT_8 ucElemID;
    UINT_16 u2Length;
    UINT_8 ucCoupleSinkStatusBp;
    UINT_8 aucCoupleSinkMac[MAC_ADDR_LEN];
} __KAL_ATTRIB_PACKED__ WFD_COUPLE_SINK_INFORMATION_IE_T, *P_WFD_COUPLE_SINK_INFORMATION_IE_T;

typedef struct _WFD_EXTENDED_CAPABILITY_IE_T {
    UINT_8 ucElemID;
    UINT_16 u2Length;
    UINT_16 u2WfdExtCapabilityBp;
}
__KAL_ATTRIB_PACKED__ WFD_EXTENDED_CAPABILITY_IE_T, *P_WFD_EXTENDED_CAPABILITY_IE_T;

typedef struct _WFD_SESSION_INFORMATION_IE_T {
    UINT_8 ucElemID;
    UINT_16 u2Length;
    PUINT_8 pucWfdDevInfoDesc[1];
} __KAL_ATTRIB_PACKED__ WFD_SESSION_INFORMATION_IE_T, *P_WFD_SESSION_INFORMATION_IE_T;

typedef struct _WFD_DEVICE_INFORMATION_DESCRIPTOR_T {
    UINT_8 ucLength;
    UINT_8 aucDevAddr[MAC_ADDR_LEN];
    UINT_8 aucAssocBssid[MAC_ADDR_LEN];
    UINT_16 u2WfdDevInfo;
    UINT_16 u2WfdDevMaxSpeed;
    UINT_8 ucCoupleSinkStatusBp;
    UINT_8 aucCoupleSinkMac[MAC_ADDR_LEN];
} __KAL_ATTRIB_PACKED__ WFD_DEVICE_INFORMATION_DESCRIPTOR_T, *P_WFD_DEVICE_INFORMATION_DESCRIPTOR_T;


#endif


UINT_32
p2pCalculate_IEForAssocReq(

    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


VOID
p2pGenerate_IEForAssocReq(

    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );


#if CFG_SUPPORT_WFD

UINT_32
wfdFuncAppendAttriDevInfo(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
wfdFuncAppendAttriAssocBssid(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
wfdFuncAppendAttriCoupledSinkInfo(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
wfdFuncAppendAttriExtCapability(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
wfdFuncCalculateAttriLenSessionInfo(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

UINT_32
wfdFuncAppendAttriSessionInfo(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
wfdFuncCalculateWfdIELenForProbeResp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

VOID
wfdFuncGenerateWfdIEForProbeResp(
    IN  P_ADAPTER_T       prAdapter,
    IN  P_MSDU_INFO_T     prMsduInfo
    );


UINT_32
wfdFuncCalculateWfdIELenForAssocReq(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


VOID
wfdFuncGenerateWfdIEForAssocReq(
    IN  P_ADAPTER_T       prAdapter,
    IN  P_MSDU_INFO_T     prMsduInfo
    );

UINT_32
wfdFuncCalculateWfdIELenForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


VOID
wfdFuncGenerateWfdIEForAssocRsp(
    IN  P_ADAPTER_T       prAdapter,
    IN  P_MSDU_INFO_T     prMsduInfo
    );


UINT_32
wfdFuncCalculateWfdIELenForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


VOID
wfdFuncGenerateWfdIEForBeacon(
    IN  P_ADAPTER_T       prAdapter,
    IN  P_MSDU_INFO_T     prMsduInfo
    );


#endif



#endif
