#ifndef BCM_MINIPORT_PHSMODULE_H
#define BCM_MINIPORT_PHSMODULE_H

int PHSTransmit(struct bcm_mini_adapter *Adapter,
					struct sk_buff **pPacket,
					 USHORT Vcid,
					 B_UINT16 uiClassifierRuleID,
					 bool bHeaderSuppressionEnabled,
					 PUINT PacketLen,
					 UCHAR bEthCSSupport);

int PHSReceive(struct bcm_mini_adapter *Adapter,
					USHORT usVcid,
					struct sk_buff *packet,
					UINT *punPacketLen,
					UCHAR *pucEthernetHdr,
					UINT
					);


void DumpDataPacketHeader(PUCHAR pPkt);

void DumpFullPacket(UCHAR *pBuf,UINT nPktLen);

void DumpPhsRules(struct bcm_phs_extension *pDeviceExtension);


int phs_init(struct bcm_phs_extension *pPhsdeviceExtension,struct bcm_mini_adapter *Adapter);

int PhsCleanup(struct bcm_phs_extension *pPHSDeviceExt);

//Utility Functions
ULONG PhsUpdateClassifierRule(void* pvContext,B_UINT16 uiVcid,B_UINT16 uiClsId, struct bcm_phs_rule *psPhsRule,B_UINT8  u8AssociatedPHSI );

ULONG PhsDeletePHSRule(void* pvContext,B_UINT16 uiVcid,B_UINT8 u8PHSI);

ULONG PhsDeleteClassifierRule(void* pvContext, B_UINT16 uiVcid ,B_UINT16  uiClsId);

ULONG PhsDeleteSFRules(void* pvContext,B_UINT16 uiVcid) ;


bool ValidatePHSRule(struct bcm_phs_rule *psPhsRule);

UINT GetServiceFlowEntry(struct bcm_phs_table *psServiceFlowTable,B_UINT16 uiVcid, struct bcm_phs_entry **ppstServiceFlowEntry);


void DumpPhsRules(struct bcm_phs_extension *pDeviceExtension);


#endif
