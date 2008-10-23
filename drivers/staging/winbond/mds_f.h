unsigned char Mds_initial(  PADAPTER Adapter );
void Mds_Destroy(  PADAPTER Adapter );
void Mds_Tx(  PADAPTER Adapter );
void Mds_HeaderCopy(  PADAPTER Adapter,  PDESCRIPTOR pDes,  PUCHAR TargetBuffer );
u16 Mds_BodyCopy(  PADAPTER Adapter,  PDESCRIPTOR pDes,  PUCHAR TargetBuffer );
void Mds_DurationSet(  PADAPTER Adapter,  PDESCRIPTOR pDes,  PUCHAR TargetBuffer );
void Mds_SendComplete(  PADAPTER Adapter,  PT02_DESCRIPTOR pT02 );
void Mds_MpduProcess(  PADAPTER Adapter,  PDESCRIPTOR pRxDes );
void Mds_reset_descriptor(  PADAPTER Adapter );
extern void DataDmp(u8 *pdata, u32 len, u32 offset);


void vRxTimerInit(PWB32_ADAPTER Adapter);
void vRxTimerStart(PWB32_ADAPTER Adapter, int timeout_value);
void RxTimerHandler_1a( PADAPTER Adapter);
void vRxTimerStop(PWB32_ADAPTER Adapter);
void RxTimerHandler( void*			SystemSpecific1,
					   PWB32_ADAPTER 	Adapter,
					   void*			SystemSpecific2,
					   void*			SystemSpecific3);


// For Asynchronous indicating. The routine collocates with USB.
void Mds_MsduProcess(  PWB32_ADAPTER Adapter,  PRXLAYER1 pRxLayer1,  u8 SlotIndex);

// For data frame sending 20060802
u16 MDS_GetPacketSize(  PADAPTER Adapter );
void MDS_GetNextPacket(  PADAPTER Adapter,  PDESCRIPTOR pDes );
void MDS_GetNextPacketComplete(  PADAPTER Adapter,  PDESCRIPTOR pDes );
void MDS_SendResult(  PADAPTER Adapter,  u8 PacketId,  unsigned char SendOK );
void MDS_EthernetPacketReceive(  PADAPTER Adapter,  PRXLAYER1 pRxLayer1 );


