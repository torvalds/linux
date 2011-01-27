#ifndef _PROTOTYPES_H_
#define _PROTOTYPES_H_

VOID LinkControlResponseMessage(PMINI_ADAPTER Adapter, PUCHAR pucBuffer);

VOID StatisticsResponse(PMINI_ADAPTER Adapter,PVOID pvBuffer);

VOID IdleModeResponse(PMINI_ADAPTER Adapter,PUINT puiBuffer);

int control_packet_handler	(PMINI_ADAPTER Adapter);

VOID DeleteAllClassifiersForSF(PMINI_ADAPTER Adapter,UINT uiSearchRuleIndex);

VOID flush_all_queues(PMINI_ADAPTER Adapter);

int register_control_device_interface(PMINI_ADAPTER ps_adapter);

void unregister_control_device_interface(PMINI_ADAPTER Adapter);

INT CopyBufferToControlPacket(PMINI_ADAPTER Adapter,/**<Logical Adapter*/
									  PVOID ioBuffer/**<Control Packet Buffer*/
									  );

VOID SortPackInfo(PMINI_ADAPTER Adapter);

VOID SortClassifiers(PMINI_ADAPTER Adapter);

VOID flush_all_queues(PMINI_ADAPTER Adapter);

VOID PruneQueueAllSF(PMINI_ADAPTER Adapter);

INT SearchSfid(PMINI_ADAPTER Adapter,UINT uiSfid);

USHORT ClassifyPacket(PMINI_ADAPTER Adapter,struct sk_buff* skb);

BOOLEAN MatchSrcPort(S_CLASSIFIER_RULE *pstClassifierRule,USHORT ushSrcPort);
BOOLEAN MatchDestPort(S_CLASSIFIER_RULE *pstClassifierRule,USHORT ushSrcPort);
BOOLEAN MatchProtocol(S_CLASSIFIER_RULE *pstClassifierRule,UCHAR ucProtocol);


INT SetupNextSend(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
					struct sk_buff *Packet, /**<data buffer*/
					USHORT Vcid)	;

VOID LinkMessage(PMINI_ADAPTER Adapter);

VOID transmit_packets(PMINI_ADAPTER Adapter);

INT SendControlPacket(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
							char *pControlPacket/**<Control Packet*/
							);


int register_networkdev(PMINI_ADAPTER Adapter);
void unregister_networkdev(PMINI_ADAPTER Adapter);

INT AllocAdapterDsxBuffer(PMINI_ADAPTER Adapter);

VOID AdapterFree(PMINI_ADAPTER Adapter);

INT FreeAdapterDsxBuffer(PMINI_ADAPTER Adapter);

int tx_pkt_handler(PMINI_ADAPTER Adapter);

int  reset_card_proc(PMINI_ADAPTER Adapter );

int run_card_proc(PMINI_ADAPTER Adapter );

int InitCardAndDownloadFirmware(PMINI_ADAPTER ps_adapter);


INT ReadMacAddressFromNVM(PMINI_ADAPTER Adapter);

int register_control_device_interface(PMINI_ADAPTER ps_adapter);

void DumpPackInfo(PMINI_ADAPTER Adapter);

int rdm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int rdmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int get_dsx_sf_data_to_application(PMINI_ADAPTER Adapter, UINT uiSFId, void __user * user_buffer);

void SendIdleModeResponse(PMINI_ADAPTER Adapter);


int  ProcessGetHostMibs(PMINI_ADAPTER Adapter, S_MIBS_HOST_STATS_MIBS *buf);
void GetDroppedAppCntrlPktMibs(S_MIBS_HOST_STATS_MIBS *ioBuffer, PPER_TARANG_DATA pTarang);
void beceem_parse_target_struct(PMINI_ADAPTER Adapter);

int bcm_ioctl_fw_download(PMINI_ADAPTER Adapter, FIRMWARE_INFO *psFwInfo);

void CopyMIBSExtendedSFParameters(PMINI_ADAPTER Adapter,
		CServiceFlowParamSI *psfLocalSet, UINT uiSearchRuleIndex);

VOID ResetCounters(PMINI_ADAPTER Adapter);

int InitLedSettings(PMINI_ADAPTER Adapter);

S_CLASSIFIER_RULE *GetFragIPClsEntry(PMINI_ADAPTER Adapter,USHORT usIpIdentification,ULONG SrcIP);

void AddFragIPClsEntry(PMINI_ADAPTER Adapter,PS_FRAGMENTED_PACKET_INFO psFragPktInfo);

void DelFragIPClsEntry(PMINI_ADAPTER Adapter,USHORT usIpIdentification,ULONG SrcIp);

void update_per_cid_rx (PMINI_ADAPTER Adapter);

void update_per_sf_desc_cnts( PMINI_ADAPTER Adapter);

void ClearTargetDSXBuffer(PMINI_ADAPTER Adapter,B_UINT16 TID,BOOLEAN bFreeAll);


void flush_queue(PMINI_ADAPTER Adapter, UINT iQIndex);


INT flushAllAppQ(VOID);


INT BeceemEEPROMBulkRead(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);



INT WriteBeceemEEPROM(PMINI_ADAPTER Adapter,UINT uiEEPROMOffset, UINT uiData);

INT PropagateCalParamsFromFlashToMemory(PMINI_ADAPTER Adapter);


INT BeceemEEPROMBulkWrite(
	PMINI_ADAPTER Adapter,
	PUCHAR pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);


INT ReadBeceemEEPROM(PMINI_ADAPTER Adapter,UINT dwAddress, UINT *pdwData);


INT BeceemNVMRead(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);

INT BeceemNVMWrite(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);


INT BcmInitNVM(PMINI_ADAPTER Adapter);

INT BcmUpdateSectorSize(PMINI_ADAPTER Adapter,UINT uiSectorSize);
BOOLEAN IsSectionExistInFlash(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL section);

INT BcmGetFlash2xSectionalBitMap(PMINI_ADAPTER Adapter, PFLASH2X_BITMAP psFlash2xBitMap);

INT BcmFlash2xBulkWrite(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	FLASH2X_SECTION_VAL eFlashSectionVal,
	UINT uiOffset,
	UINT uiNumBytes,
	UINT bVerify);

INT BcmFlash2xBulkRead(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	FLASH2X_SECTION_VAL eFlashSectionVal,
	UINT uiOffsetWithinSectionVal,
	UINT uiNumBytes);

INT BcmGetSectionValStartOffset(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlashSectionVal);

INT BcmSetActiveSection(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectVal);
INT BcmAllocFlashCSStructure(PMINI_ADAPTER psAdapter);
INT BcmDeAllocFlashCSStructure(PMINI_ADAPTER psAdapter);

INT BcmCopyISO(PMINI_ADAPTER Adapter, FLASH2X_COPY_SECTION sCopySectStrut);
INT BcmFlash2xCorruptSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectionVal);
INT BcmFlash2xWriteSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlashSectionVal);
INT	validateFlash2xReadWrite(PMINI_ADAPTER Adapter, PFLASH2X_READWRITE psFlash2xReadWrite);
INT IsFlash2x(PMINI_ADAPTER Adapter);
INT	BcmCopySection(PMINI_ADAPTER Adapter,
						FLASH2X_SECTION_VAL SrcSection,
						FLASH2X_SECTION_VAL DstSection,
						UINT offset,
						UINT numOfBytes);


BOOLEAN IsNonCDLessDevice(PMINI_ADAPTER Adapter);


VOID OverrideServiceFlowParams(PMINI_ADAPTER Adapter,PUINT puiBuffer);

int wrmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);
int rdmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int wrmWithLock(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);
INT buffDnldVerify(PMINI_ADAPTER Adapter, unsigned char *mappedbuffer, unsigned int u32FirmwareLength,
		unsigned long u32StartingAddress);


VOID putUsbSuspend(struct work_struct *work);
BOOLEAN IsReqGpioIsLedInNVM(PMINI_ADAPTER Adapter, UINT gpios);


#endif




