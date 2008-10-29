#define OS_SET_SHUTDOWN( _A )		_A->shutdown=1
#define OS_SET_RESUME( _A )		_A->shutdown=0
#define OS_STOP( _A )	WBLINUX_stop( _A )

#define OS_CURRENT_RX_BYTE( _A )		_A->RxByteCount
#define OS_CURRENT_TX_BYTE( _A )		_A->TxByteCount
#define OS_EVENT_INDICATE( _A, _B, _F )
#define OS_PMKID_STATUS_EVENT( _A )
#define OS_RECEIVE_PACKET_INDICATE( _A, _D )		WBLinux_ReceivePacket( _A, _D )
#define OS_RECEIVE_802_1X_PACKET_INDICATE( _A, _D )	EAP_ReceivePacket( _A, _D )
#define OS_GET_PACKET( _A, _D )				WBLINUX_GetNextPacket( _A, _D )
#define OS_GET_PACKET_COMPLETE( _A, _D )	WBLINUX_GetNextPacketCompleted( _A, _D )
#define OS_SEND_RESULT( _A, _ID, _R )

#define WBLINUX_PACKET_ARRAY_SIZE	(ETHERNET_TX_DESCRIPTORS*4)

#define MAX_ANSI_STRING		40

struct wb35_adapter {
	u32 adapterIndex;	// 20060703.4 Add for using padapterContext global adapter point

	WB_LOCALDESCRIPT sLocalPara;	// Myself connected parameters
	PWB_BSSDESCRIPTION asBSSDescriptElement;

	MLME_FRAME sMlmeFrame;	// connect to peerSTA parameters

	MTO_PARAMETERS sMtoPara;	// MTO_struct ...
	hw_data_t sHwData;	//For HAL
	MDS Mds;

	spinlock_t SpinLock;
	u32 shutdown;

	atomic_t ThreadCount;

	u32 RxByteCount;
	u32 TxByteCount;

	struct sk_buff *skb_array[WBLINUX_PACKET_ARRAY_SIZE];
	struct sk_buff *packet_return;
	s32 skb_SetIndex;
	s32 skb_GetIndex;
	s32 netif_state_stop;	// 1: stop  0: normal
	struct iw_statistics iw_stats;

	u8 LinkName[MAX_ANSI_STRING];
};
