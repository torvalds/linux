#ifndef _PROTOTYPES_H_
#define _PROTOTYPES_H_

VOID LinkControlResponseMessage(struct bcm_mini_adapter *Adapter, PUCHAR pucBuffer);

VOID StatisticsResponse(struct bcm_mini_adapter *Adapter,PVOID pvBuffer);

VOID IdleModeResponse(struct bcm_mini_adapter *Adapter,PUINT puiBuffer);

int control_packet_handler	(struct bcm_mini_adapter *Adapter);

VOID DeleteAllClassifiersForSF(struct bcm_mini_adapter *Adapter,UINT uiSearchRuleIndex);

VOID flush_all_queues(struct bcm_mini_adapter *Adapter);

int register_control_device_interface(struct bcm_mini_adapter *ps_adapter);

void unregister_control_device_interface(struct bcm_mini_adapter *Adapter);

INT CopyBufferToControlPacket(struct bcm_mini_adapter *Adapter,/**<Logical Adapter*/
									  PVOID ioBuffer/**<Control Packet Buffer*/
									  );

VOID SortPackInfo(struct bcm_mini_adapter *Adapter);

VOID SortClassifiers(struct bcm_mini_adapter *Adapter);

VOID flush_all_queues(struct bcm_mini_adapter *Adapter);

VOID PruneQueueAllSF(struct bcm_mini_adapter *Adapter);

INT SearchSfid(struct bcm_mini_adapter *Adapter,UINT uiSfid);

USHORT ClassifyPacket(struct bcm_mini_adapter *Adapter,struct sk_buff* skb);

BOOLEAN MatchSrcPort(struct bcm_classifier_rule *pstClassifierRule,USHORT ushSrcPort);
BOOLEAN MatchDestPort(struct bcm_classifier_rule *pstClassifierRule,USHORT ushSrcPort);
BOOLEAN MatchProtocol(struct bcm_classifier_rule *pstClassifierRule,UCHAR ucProtocol);


INT SetupNextSend(struct bcm_mini_adapter *Adapter, /**<Logical Adapter*/
					struct sk_buff *Packet, /**<data buffer*/
					USHORT Vcid)	;

VOID LinkMessage(struct bcm_mini_adapter *Adapter);

VOID transmit_packets(struct bcm_mini_adapter *Adapter);

INT SendControlPacket(struct bcm_mini_adapter *Adapter, /**<Logical Adapter*/
							char *pControlPacket/**<Control Packet*/
							);


int register_networkdev(struct bcm_mini_adapter *Adapter);
void unregister_networkdev(struct bcm_mini_adapter *Adapter);

INT AllocAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);

VOID AdapterFree(struct bcm_mini_adapter *Adapter);

INT FreeAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);

int tx_pkt_handler(struct bcm_mini_adapter *Adapter);

int  reset_card_proc(struct bcm_mini_adapter *Adapter );

int run_card_proc(struct bcm_mini_adapter *Adapter );

int InitCardAndDownloadFirmware(struct bcm_mini_adapter *ps_adapter);


INT ReadMacAddressFromNVM(struct bcm_mini_adapter *Adapter);

int register_control_device_interface(struct bcm_mini_adapter *ps_adapter);

void DumpPackInfo(struct bcm_mini_adapter *Adapter);

int rdm(struct bcm_mini_adapter *Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrm(struct bcm_mini_adapter *Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrmalt (struct bcm_mini_adapter *Adapter, UINT uiAddress, unsigned int *pucBuff, size_t sSize);

int rdmalt (struct bcm_mini_adapter *Adapter, UINT uiAddress, unsigned int *pucBuff, size_t sSize);

int get_dsx_sf_data_to_application(struct bcm_mini_adapter *Adapter, UINT uiSFId, void __user * user_buffer);

void SendIdleModeResponse(struct bcm_mini_adapter *Adapter);


int  ProcessGetHostMibs(struct bcm_mini_adapter *Adapter, struct bcm_host_stats_mibs *buf);
void GetDroppedAppCntrlPktMibs(struct bcm_host_stats_mibs *ioBuffer, struct bcm_tarang_data *pTarang);
void beceem_parse_target_struct(struct bcm_mini_adapter *Adapter);

int bcm_ioctl_fw_download(struct bcm_mini_adapter *Adapter, struct bcm_firmware_info *psFwInfo);

void CopyMIBSExtendedSFParameters(struct bcm_mini_adapter *Adapter,
		struct bcm_connect_mgr_params *psfLocalSet, UINT uiSearchRuleIndex);

VOID ResetCounters(struct bcm_mini_adapter *Adapter);

int InitLedSettings(struct bcm_mini_adapter *Adapter);

struct bcm_classifier_rule *GetFragIPClsEntry(struct bcm_mini_adapter *Adapter,USHORT usIpIdentification,ULONG SrcIP);

void AddFragIPClsEntry(struct bcm_mini_adapter *Adapter, struct bcm_fragmented_packet_info *psFragPktInfo);

void DelFragIPClsEntry(struct bcm_mini_adapter *Adapter,USHORT usIpIdentification,ULONG SrcIp);

void update_per_cid_rx (struct bcm_mini_adapter *Adapter);

void update_per_sf_desc_cnts( struct bcm_mini_adapter *Adapter);

void ClearTargetDSXBuffer(struct bcm_mini_adapter *Adapter,B_UINT16 TID,BOOLEAN bFreeAll);


void flush_queue(struct bcm_mini_adapter *Adapter, UINT iQIndex);


INT flushAllAppQ(VOID);


INT BeceemEEPROMBulkRead(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);



INT WriteBeceemEEPROM(struct bcm_mini_adapter *Adapter,UINT uiEEPROMOffset, UINT uiData);

INT PropagateCalParamsFromFlashToMemory(struct bcm_mini_adapter *Adapter);


INT BeceemEEPROMBulkWrite(
	struct bcm_mini_adapter *Adapter,
	PUCHAR pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);


INT ReadBeceemEEPROM(struct bcm_mini_adapter *Adapter,UINT dwAddress, UINT *pdwData);


INT BeceemNVMRead(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);

INT BeceemNVMWrite(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);


INT BcmInitNVM(struct bcm_mini_adapter *Adapter);

INT BcmUpdateSectorSize(struct bcm_mini_adapter *Adapter,UINT uiSectorSize);
BOOLEAN IsSectionExistInFlash(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val section);

INT BcmGetFlash2xSectionalBitMap(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_bitmap *psFlash2xBitMap);

INT BcmFlash2xBulkWrite(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	enum bcm_flash2x_section_val eFlashSectionVal,
	UINT uiOffset,
	UINT uiNumBytes,
	UINT bVerify);

INT BcmFlash2xBulkRead(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	enum bcm_flash2x_section_val eFlashSectionVal,
	UINT uiOffsetWithinSectionVal,
	UINT uiNumBytes);

INT BcmGetSectionValStartOffset(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlashSectionVal);

INT BcmSetActiveSection(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectVal);
INT BcmAllocFlashCSStructure(struct bcm_mini_adapter *psAdapter);
INT BcmDeAllocFlashCSStructure(struct bcm_mini_adapter *psAdapter);

INT BcmCopyISO(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_copy_section sCopySectStrut);
INT BcmFlash2xCorruptSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal);
INT BcmFlash2xWriteSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlashSectionVal);
INT	validateFlash2xReadWrite(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_readwrite *psFlash2xReadWrite);
INT IsFlash2x(struct bcm_mini_adapter *Adapter);
INT	BcmCopySection(struct bcm_mini_adapter *Adapter,
						enum bcm_flash2x_section_val SrcSection,
						enum bcm_flash2x_section_val DstSection,
						UINT offset,
						UINT numOfBytes);


BOOLEAN IsNonCDLessDevice(struct bcm_mini_adapter *Adapter);


VOID OverrideServiceFlowParams(struct bcm_mini_adapter *Adapter,PUINT puiBuffer);

int wrmaltWithLock (struct bcm_mini_adapter *Adapter, UINT uiAddress, unsigned int *pucBuff, size_t sSize);
int rdmaltWithLock (struct bcm_mini_adapter *Adapter, UINT uiAddress, unsigned int *pucBuff, size_t sSize);

int wrmWithLock(struct bcm_mini_adapter *Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);
INT buffDnldVerify(struct bcm_mini_adapter *Adapter, unsigned char *mappedbuffer, unsigned int u32FirmwareLength,
		unsigned long u32StartingAddress);


VOID putUsbSuspend(struct work_struct *work);
BOOLEAN IsReqGpioIsLedInNVM(struct bcm_mini_adapter *Adapter, UINT gpios);


#endif




