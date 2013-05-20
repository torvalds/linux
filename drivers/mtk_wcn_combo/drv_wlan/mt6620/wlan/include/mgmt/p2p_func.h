#ifndef _P2P_FUNC_H
#define _P2P_FUNC_H


VOID
p2pFuncRequestScan(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_SCAN_REQ_INFO_T prScanReqInfo
    );

VOID
p2pFuncCancelScan(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_SCAN_REQ_INFO_T prScanReqInfo
    );



VOID
p2pFuncStartGO(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN PUINT_8 pucSsidBuf,
    IN UINT_8 ucSsidLen,
    IN UINT_8 ucChannelNum,
    IN ENUM_BAND_T eBand,
    IN ENUM_CHNL_EXT_T eSco,
    IN BOOLEAN fgIsPureAP
    );



VOID
p2pFuncAcquireCh(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    );


VOID
p2pFuncReleaseCh(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    );

VOID
p2pFuncSetChannel(
    IN P_ADAPTER_T prAdapter,
    IN P_RF_CHANNEL_INFO_T prRfChannelInfo
    );


BOOLEAN
p2pFuncRetryJOIN(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_P2P_JOIN_INFO_T prJoinInfo
    );

VOID
p2pFuncUpdateBssInfoForJOIN (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prAssocRspSwRfb
    );


WLAN_STATUS
p2pFuncTxMgmtFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_MGMT_TX_REQ_INFO_T prMgmtTxReqInfo,
    IN P_MSDU_INFO_T prMgmtTxMsdu,
    IN UINT_64 u8Cookie
    );

WLAN_STATUS
p2pFuncBeaconUpdate(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_BEACON_UPDATE_INFO_T prBcnUpdateInfo,
    IN PUINT_8 pucNewBcnHdr,
    IN UINT_32 u4NewHdrLen,
    IN PUINT_8 pucNewBcnBody,
    IN UINT_32 u4NewBodyLen
    );


BOOLEAN
p2pFuncValidateAuth(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN PP_STA_RECORD_T pprStaRec,
    OUT PUINT_16 pu2StatusCode
    );

BOOLEAN
p2pFuncValidateAssocReq(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_16 pu2StatusCode
    );


VOID
p2pFuncResetStaRecStatus(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

VOID
p2pFuncInitConnectionSettings(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CONNECTION_SETTINGS_T prP2PConnSettings
    );


BOOLEAN
p2pFuncParseCheckForP2PInfoElem(
    IN  P_ADAPTER_T prAdapter,
    IN  PUINT_8 pucBuf,
    OUT PUINT_8 pucOuiType
    );


BOOLEAN
p2pFuncValidateProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_32 pu4ControlFlags
    );

VOID
p2pFuncValidateRxActionFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

BOOLEAN
p2pFuncIsAPMode(
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    );


VOID
p2pFuncParseBeaconContent(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN PUINT_8 pucIEInfo,
    IN UINT_32 u4IELen
    );


P_BSS_DESC_T
p2pFuncKeepOnConnection(

    IN P_ADAPTER_T prAdapter,
    IN P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo,
    IN P_P2P_SCAN_REQ_INFO_T prScanReqInfo
    );


VOID
p2pFuncStoreAssocRspIEBuffer(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );


VOID
p2pFuncMgmtFrameRegister(
    IN P_ADAPTER_T  prAdapter,
    IN  UINT_16 u2FrameType,
    IN BOOLEAN fgIsRegistered,
    OUT PUINT_32 pu4P2pPacketFilter
    );

VOID
p2pFuncUpdateMgmtFrameRegister(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4OsFilter
    );


VOID
p2pFuncGetStationInfo(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucMacAddr,
    OUT P_P2P_STATION_INFO_T prStaInfo
    );

BOOLEAN
p2pFuncGetAttriList(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucOuiType,
    IN PUINT_8 pucIE,
    IN UINT_16 u2IELength,
    OUT PPUINT_8 ppucAttriList,
    OUT PUINT_16 pu2AttriListLen
    );

P_MSDU_INFO_T
p2pFuncProcessP2pProbeRsp(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMgmtTxMsdu
    );

#if 0 //LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
UINT_32
p2pFuncCalculateExtra_IELenForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

VOID
p2pFuncGenerateExtra_IEForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );


#else
UINT_32
p2pFuncCalculateP2p_IELenForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


VOID
p2pFuncGenerateP2p_IEForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );


UINT_32
p2pFuncCalculateWSC_IELenForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

VOID
p2pFuncGenerateWSC_IEForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );
#endif
UINT_32
p2pFuncCalculateP2p_IELenForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

VOID
p2pFuncGenerateP2p_IEForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );


UINT_32
p2pFuncCalculateWSC_IELenForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

VOID
p2pFuncGenerateWSC_IEForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );



UINT_32
p2pFuncCalculateP2P_IELen(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    );

VOID
p2pFuncGenerateP2P_IE(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    );


UINT_32
p2pFuncAppendAttriStatusForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );


UINT_32
p2pFuncAppendAttriExtListenTiming(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

VOID
p2pFuncDissolve(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN BOOLEAN fgSendDeauth,
    IN UINT_16 u2ReasonCode
    );


P_IE_HDR_T
p2pFuncGetSpecIE(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2BufferLen,
    IN UINT_8 ucElemID,
    IN PBOOLEAN pfgIsMore
    );


P_ATTRIBUTE_HDR_T
p2pFuncGetSpecAttri(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucOuiType,
    IN PUINT_8 pucIEBuf,
    IN UINT_16 u2BufferLen,
    IN UINT_16 u2AttriID
    );

#endif
