#ifndef _P2P_IE_H
#define _P2P_IE_H


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



#endif
