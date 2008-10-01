//============================================================
// wblinux_s.h
//
#define OS_MEMORY_ALLOC( _V, _S )	WBLINUX_MemoryAlloc( _V, _S )
#define OS_LINK_STATUS			(Adapter->WbLinux.LinkStatus == OS_CONNECTED)
#define OS_SET_SHUTDOWN( _A )		_A->WbLinux.shutdown=1
#define OS_SET_RESUME( _A )		_A->WbLinux.shutdown=0
#define OS_CONNECT_STATUS_INDICATE( _A, _F )		WBLINUX_ConnectStatus( _A, _F )
#define OS_DISCONNECTED	0
#define OS_CONNECTED	1
#define OS_STOP( _A )	WBLINUX_stop( _A )

#define OS_CURRENT_RX_BYTE( _A )		_A->WbLinux.RxByteCount
#define OS_CURRENT_TX_BYTE( _A )		_A->WbLinux.TxByteCount
#define OS_EVENT_INDICATE( _A, _B, _F )
#define OS_PMKID_STATUS_EVENT( _A )
#define OS_RECEIVE_PACKET_INDICATE( _A, _D )		WBLinux_ReceivePacket( _A, _D )
#define OS_RECEIVE_802_1X_PACKET_INDICATE( _A, _D )	EAP_ReceivePacket( _A, _D )
#define OS_GET_PACKET( _A, _D )				WBLINUX_GetNextPacket( _A, _D )
#define OS_GET_PACKET_COMPLETE( _A, _D )	WBLINUX_GetNextPacketCompleted( _A, _D )
#define OS_SEND_RESULT( _A, _ID, _R )

#define WBLINUX_PACKET_ARRAY_SIZE	(ETHERNET_TX_DESCRIPTORS*4)

typedef struct _WBLINUX
{
	OS_SPIN_LOCK	AtomicSpinLock;
	OS_SPIN_LOCK	SpinLock;
	u32	shutdown;

	OS_ATOMIC	ThreadCount;

	u32	LinkStatus;		// OS_DISCONNECTED or OS_CONNECTED

	u32	RxByteCount;
	u32	TxByteCount;

	struct sk_buff *skb_array[ WBLINUX_PACKET_ARRAY_SIZE ];
	struct sk_buff *packet_return;
	s32	skb_SetIndex;
	s32	skb_GetIndex;
	s32	netif_state_stop; // 1: stop  0: normal
} WBLINUX, *PWBLINUX;


