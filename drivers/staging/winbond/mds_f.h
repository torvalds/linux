unsigned char Mds_initial(  struct wb35_adapter *adapter );
void Mds_Destroy(  struct wb35_adapter *adapter );
void Mds_Tx(  struct wb35_adapter *adapter );
void Mds_HeaderCopy(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes,  u8 *TargetBuffer );
u16 Mds_BodyCopy(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes,  u8 *TargetBuffer );
void Mds_DurationSet(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes,  u8 *TargetBuffer );
void Mds_SendComplete(  struct wb35_adapter *adapter,  PT02_DESCRIPTOR pT02 );
void Mds_MpduProcess(  struct wb35_adapter *adapter,  PDESCRIPTOR pRxDes );
void Mds_reset_descriptor(  struct wb35_adapter *adapter );
extern void DataDmp(u8 *pdata, u32 len, u32 offset);


void vRxTimerInit(struct wb35_adapter *adapter);
void vRxTimerStart(struct wb35_adapter *adapter, int timeout_value);
void vRxTimerStop(struct wb35_adapter *adapter);

// For Asynchronous indicating. The routine collocates with USB.
void Mds_MsduProcess(  struct wb35_adapter *adapter,  PRXLAYER1 pRxLayer1,  u8 SlotIndex);

// For data frame sending 20060802
u16 MDS_GetPacketSize(  struct wb35_adapter *adapter );
void MDS_GetNextPacket(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes );
void MDS_GetNextPacketComplete(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes );
void MDS_SendResult(  struct wb35_adapter *adapter,  u8 PacketId,  unsigned char SendOK );
void MDS_EthernetPacketReceive(  struct wb35_adapter *adapter,  PRXLAYER1 pRxLayer1 );


