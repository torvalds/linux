#ifndef BCM_MINIPORT_PHSMODULE_H
#define BCM_MINIPORT_PHSMODULE_H

int PHSTransmit(PMINI_ADAPTER Adapter,
					struct sk_buff **pPacket,
					 USHORT Vcid,
					 B_UINT16 uiClassifierRuleID,
					 BOOLEAN bHeaderSuppressionEnabled,
					 PUINT PacketLen,
					 UCHAR bEthCSSupport);

int PHSRecieve(PMINI_ADAPTER Adapter,
					USHORT usVcid,
					struct sk_buff *packet,
					UINT *punPacketLen,
					UCHAR *pucEthernetHdr,
					UINT
					);


void DumpDataPacketHeader(PUCHAR pPkt);

void DumpFullPacket(UCHAR *pBuf,UINT nPktLen);

void DumpPhsRules(PPHS_DEVICE_EXTENSION pDeviceExtension);


int phs_init(PPHS_DEVICE_EXTENSION pPhsdeviceExtension,PMINI_ADAPTER Adapter);

void free_phs_serviceflow_rules(S_SERVICEFLOW_TABLE *psServiceFlowRulesTable);

int phs_compress(S_PHS_RULE   *phs_members,unsigned char *in_buf,
						unsigned char *out_buf,unsigned int *header_size,UINT *new_header_size );


int verify_suppress_phsf(unsigned char *in_buffer,unsigned char *out_buffer,
								unsigned char *phsf,unsigned char *phsm,unsigned int phss,unsigned int phsv,UINT *new_header_size );

int phs_decompress(unsigned char *in_buf,unsigned char *out_buf,\
						  S_PHS_RULE   *phs_rules,UINT *header_size);


int PhsCleanup(PPHS_DEVICE_EXTENSION pPHSDeviceExt);

//Utility Functions
ULONG PhsUpdateClassifierRule(void* pvContext,B_UINT16 uiVcid,B_UINT16 uiClsId,S_PHS_RULE *psPhsRule,B_UINT8  u8AssociatedPHSI );

ULONG PhsDeletePHSRule(void* pvContext,B_UINT16 uiVcid,B_UINT8 u8PHSI);

ULONG PhsDeleteClassifierRule(void* pvContext, B_UINT16 uiVcid ,B_UINT16  uiClsId);

ULONG PhsDeleteSFRules(void* pvContext,B_UINT16 uiVcid) ;


ULONG PhsCompress(void* pvContext,
				  B_UINT16 uiVcid,
				  B_UINT16 uiClsId,
				  void *pvInputBuffer,
				  void *pvOutputBuffer,
				  UINT *pOldHeaderSize,
				  UINT *pNewHeaderSize );

ULONG PhsDeCompress(void* pvContext,
				  B_UINT16 uiVcid,
				  void *pvInputBuffer,
				  void *pvOutputBuffer,
				  UINT *pInHeaderSize,
				  UINT *pOutHeaderSize);


BOOLEAN ValidatePHSRule(S_PHS_RULE *psPhsRule);

BOOLEAN ValidatePHSRuleComplete(S_PHS_RULE *psPhsRule);

UINT GetServiceFlowEntry(S_SERVICEFLOW_TABLE *psServiceFlowTable,B_UINT16 uiVcid,S_SERVICEFLOW_ENTRY **ppstServiceFlowEntry);

UINT GetClassifierEntry(S_CLASSIFIER_TABLE *pstClassifierTable,B_UINT32 uiClsid,E_CLASSIFIER_ENTRY_CONTEXT eClsContext, S_CLASSIFIER_ENTRY **ppstClassifierEntry);

UINT GetPhsRuleEntry(S_CLASSIFIER_TABLE *pstClassifierTable,B_UINT32 uiPHSI,E_CLASSIFIER_ENTRY_CONTEXT eClsContext,S_PHS_RULE **ppstPhsRule);


UINT CreateSFToClassifierRuleMapping(B_UINT16 uiVcid,B_UINT16  uiClsId,S_SERVICEFLOW_TABLE *psServiceFlowTable,S_PHS_RULE *psPhsRule,B_UINT8 u8AssociatedPHSI);

UINT CreateClassiferToPHSRuleMapping(B_UINT16 uiVcid,B_UINT16  uiClsId,S_SERVICEFLOW_ENTRY *pstServiceFlowEntry,S_PHS_RULE *psPhsRule,B_UINT8 u8AssociatedPHSI);

UINT CreateClassifierPHSRule(B_UINT16  uiClsId,S_CLASSIFIER_TABLE *psaClassifiertable ,S_PHS_RULE *psPhsRule,E_CLASSIFIER_ENTRY_CONTEXT eClsContext,B_UINT8 u8AssociatedPHSI);

UINT UpdateClassifierPHSRule(B_UINT16  uiClsId,S_CLASSIFIER_ENTRY *pstClassifierEntry,S_CLASSIFIER_TABLE *psaClassifiertable ,S_PHS_RULE *psPhsRule,B_UINT8 u8AssociatedPHSI);

BOOLEAN DerefPhsRule(B_UINT16  uiClsId,S_CLASSIFIER_TABLE *psaClassifiertable,S_PHS_RULE *pstPhsRule);

void DumpPhsRules(PPHS_DEVICE_EXTENSION pDeviceExtension);


#endif
