#ifndef _PROTOTYPES_H_
#define _PROTOTYPES_H_

int BcmFileDownload(PMINI_ADAPTER Adapter,/**< Logical Adapter */
                        char *path,     /**< path to image file */
                        unsigned int loc    /**< Download Address on the chip*/
                        );
VOID LinkControlResponseMessage(PMINI_ADAPTER Adapter, PUCHAR pucBuffer);

VOID StatisticsResponse(PMINI_ADAPTER Adapter,PVOID pvBuffer);

VOID IdleModeResponse(PMINI_ADAPTER Adapter,PUINT puiBuffer);

void bcm_kfree_skb(struct sk_buff *skb);
VOID bcm_kfree(VOID *ptr);


VOID handle_rx_control_packet(PMINI_ADAPTER Adapter, 	/**<Pointer to the Adapter structure*/
								struct sk_buff *skb);				/**<Pointer to the socket buffer*/

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

USHORT	IpVersion4(PMINI_ADAPTER Adapter, /**< Pointer to the driver control structure */
					struct iphdr *iphd, /**<Pointer to the IP Hdr of the packet*/
					S_CLASSIFIER_RULE *pstClassifierRule );

VOID PruneQueue(PMINI_ADAPTER Adapter,/**<Pointer to the driver control structure*/
					INT iIndex/**<Queue Index*/
					);

VOID PruneQueueAllSF(PMINI_ADAPTER Adapter);

INT SearchSfid(PMINI_ADAPTER Adapter,UINT uiSfid);

USHORT GetPacketQueueIndex(PMINI_ADAPTER Adapter, /**<Pointer to the driver control structure */
								struct sk_buff* Packet /**< Pointer to the Packet to be sent*/
								);

VOID
reply_to_arp_request(struct sk_buff *skb  /**<sk_buff of ARP request*/
						);

INT SetupNextSend(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
					struct sk_buff *Packet, /**<data buffer*/
					USHORT Vcid)	;

VOID LinkMessage(PMINI_ADAPTER Adapter);

VOID transmit_packets(PMINI_ADAPTER Adapter);

INT SendControlPacket(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
							char *pControlPacket/**<Control Packet*/
							);

INT bcm_transmit(struct sk_buff *skb, 		/**< skb */
					struct net_device *dev 	/**< net device pointer */
					);

int register_networkdev(PMINI_ADAPTER Adapter);

INT AllocAdapterDsxBuffer(PMINI_ADAPTER Adapter);

VOID AdapterFree(PMINI_ADAPTER Adapter);

INT FreeAdapterDsxBuffer(PMINI_ADAPTER Adapter);

int create_worker_threads(PMINI_ADAPTER psAdapter);

int tx_pkt_handler(PMINI_ADAPTER Adapter);

int  reset_card_proc(PMINI_ADAPTER Adapter );

int run_card_proc(PMINI_ADAPTER Adapter );

int InitCardAndDownloadFirmware(PMINI_ADAPTER ps_adapter);

int bcm_parse_target_params(PMINI_ADAPTER Adapter);

INT ReadMacAddressFromNVM(PMINI_ADAPTER Adapter);

int register_control_device_interface(PMINI_ADAPTER ps_adapter);

void DumpPackInfo(PMINI_ADAPTER Adapter);

int rdm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);

int wrmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int rdmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int get_dsx_sf_data_to_application(PMINI_ADAPTER Adapter, UINT uiSFId, void __user * user_buffer);

void SendLinkDown(PMINI_ADAPTER Adapter);

void SendIdleModeResponse(PMINI_ADAPTER Adapter);

void HandleShutDownModeRequest(PMINI_ADAPTER Adapter,PUCHAR pucBuffer);

int  ProcessGetHostMibs(PMINI_ADAPTER Adapter, PVOID ioBuffer,
	ULONG inputBufferLength);

int GetDroppedAppCntrlPktMibs(PVOID ioBuffer, PPER_TARANG_DATA pTarang);
void beceem_parse_target_struct(PMINI_ADAPTER Adapter);

void doPowerAutoCorrection(PMINI_ADAPTER psAdapter);

int bcm_ioctl_fw_download(PMINI_ADAPTER Adapter, FIRMWARE_INFO *psFwInfo);

void bcm_unregister_networkdev(PMINI_ADAPTER Adapter);

int SearchVcid(PMINI_ADAPTER Adapter,unsigned short usVcid);

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

void beceem_protocol_reset (PMINI_ADAPTER Adapter);

void flush_queue(PMINI_ADAPTER Adapter, UINT iQIndex);


INT flushAllAppQ(VOID);


INT BeceemEEPROMBulkRead(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);


INT BeceemFlashBulkRead(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes);

UINT BcmGetEEPROMSize(PMINI_ADAPTER Adapter);

INT WriteBeceemEEPROM(PMINI_ADAPTER Adapter,UINT uiEEPROMOffset, UINT uiData);

UINT BcmGetFlashSize(PMINI_ADAPTER Adapter);

UINT BcmGetFlashSectorSize(PMINI_ADAPTER Adapter, UINT FlashSectorSizeSig, UINT FlashSectorSize);

INT BeceemFlashBulkWrite(
	PMINI_ADAPTER Adapter,
	PUINT pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);

INT PropagateCalParamsFromFlashToMemory(PMINI_ADAPTER Adapter);

INT PropagateCalParamsFromEEPROMToMemory(PMINI_ADAPTER Adapter);


INT BeceemEEPROMBulkWrite(
	PMINI_ADAPTER Adapter,
	PUCHAR pBuffer,
	UINT uiOffset,
	UINT uiNumBytes,
	BOOLEAN bVerify);


INT ReadBeceemEEPROMBulk(PMINI_ADAPTER Adapter,UINT dwAddress, UINT *pdwData, UINT dwNumData);

INT ReadBeceemEEPROM(PMINI_ADAPTER Adapter,UINT dwAddress, UINT *pdwData);

NVM_TYPE BcmGetNvmType(PMINI_ADAPTER Adapter);

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

INT BcmUpdateSectorSize(PMINI_ADAPTER Adapter,UINT uiSectorSize);

INT BcmInitNVM(PMINI_ADAPTER Adapter);

INT BcmGetNvmSize(PMINI_ADAPTER Adapter);

INT IsSectionExistInVendorInfo(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL section);

VOID BcmValidateNvmType(PMINI_ADAPTER Adapter);

VOID ConfigureEndPointTypesThroughEEPROM(PMINI_ADAPTER Adapter);

INT BcmGetFlashCSInfo(PMINI_ADAPTER Adapter);
INT ReadDSDHeader(PMINI_ADAPTER Adapter, PDSD_HEADER psDSDHeader, FLASH2X_SECTION_VAL dsd);
INT BcmGetActiveDSD(PMINI_ADAPTER Adapter);
INT ReadISOHeader(PMINI_ADAPTER Adapter, PISO_HEADER psISOHeader, FLASH2X_SECTION_VAL IsoImage);
INT BcmGetActiveISO(PMINI_ADAPTER Adapter);
B_UINT8 IsOffsetWritable(PMINI_ADAPTER Adapter, UINT uiOffset);
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
INT BcmGetSectionValEndOffset(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlashSectionVal);

INT BcmGetSectionValStartOffset(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlashSectionVal);

INT BcmSetActiveSection(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectVal);
INT BcmAllocFlashCSStructure(PMINI_ADAPTER psAdapter);
INT BcmDeAllocFlashCSStructure(PMINI_ADAPTER psAdapter);

INT BcmCopyISO(PMINI_ADAPTER Adapter, FLASH2X_COPY_SECTION sCopySectStrut);
INT BcmFlash2xCorruptSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectionVal);
INT BcmFlash2xWriteSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlashSectionVal);
INT	validateFlash2xReadWrite(PMINI_ADAPTER Adapter, PFLASH2X_READWRITE psFlash2xReadWrite);
INT IsFlash2x(PMINI_ADAPTER Adapter);
INT GetFlashBaseAddr(PMINI_ADAPTER Adapter);
INT SaveHeaderIfPresent(PMINI_ADAPTER Adapter, PUCHAR pBuff, UINT uiSectAlignAddr);
INT	BcmCopySection(PMINI_ADAPTER Adapter,
						FLASH2X_SECTION_VAL SrcSection,
						FLASH2X_SECTION_VAL DstSection,
						UINT offset,
						UINT numOfBytes);

INT BcmDoChipSelect(PMINI_ADAPTER Adapter, UINT offset);
INT BcmMakeFlashCSActive(PMINI_ADAPTER Adapter, UINT offset);
INT ReadDSDSignature(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL dsd);
INT ReadDSDPriority(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL dsd);
FLASH2X_SECTION_VAL getHighestPriDSD(PMINI_ADAPTER Adapter);
INT ReadISOSignature(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL iso);
INT ReadISOPriority(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL iso);
FLASH2X_SECTION_VAL getHighestPriISO(PMINI_ADAPTER Adapter);
INT WriteToFlashWithoutSectorErase(PMINI_ADAPTER Adapter,
										PUINT pBuff,
										FLASH2X_SECTION_VAL eFlash2xSectionVal,
										UINT uiOffset,
										UINT uiNumBytes
										);

//UINT getNumOfSubSectionWithWRPermisson(PMINI_ADAPTER Adapter, SECTION_TYPE secType);
BOOLEAN IsSectionExistInFlash(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL section);
INT IsSectionWritable(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL Section);
INT CorruptDSDSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectionVal);
INT CorruptISOSig(PMINI_ADAPTER Adapter, FLASH2X_SECTION_VAL eFlash2xSectionVal);
BOOLEAN IsNonCDLessDevice(PMINI_ADAPTER Adapter);


VOID OverrideServiceFlowParams(PMINI_ADAPTER Adapter,PUINT puiBuffer);

int wrmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);
int rdmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t sSize);

int rdmWithLock(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);
int wrmWithLock(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t size);
INT buffDnldVerify(PMINI_ADAPTER Adapter, unsigned char *mappedbuffer, unsigned int u32FirmwareLength,
		unsigned long u32StartingAddress);


VOID putUsbSuspend(struct work_struct *work);
BOOLEAN IsReqGpioIsLedInNVM(PMINI_ADAPTER Adapter, UINT gpios);

#ifdef BCM_SHM_INTERFACE
INT beceem_virtual_device_init(void);
VOID virtual_mail_box_interrupt(void);
INT beceem_virtual_device_exit(void);
#endif

#endif




