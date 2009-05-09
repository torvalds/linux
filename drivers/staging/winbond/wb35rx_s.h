//============================================================================
// wb35rx.h --
//============================================================================

// Definition for this module used
#define MAX_USB_RX_BUFFER	4096	// This parameter must be 4096 931130.4.f

#define MAX_USB_RX_BUFFER_NUMBER	ETHERNET_RX_DESCRIPTORS		// Maximum 254, 255 is RESERVED ID
#define RX_INTERFACE				0	// Interface 1
#define RX_PIPE						2	// Pipe 3
#define MAX_PACKET_SIZE				1600 //1568	// 8 + 1532 + 4 + 24(IV EIV MIC ICV CRC) for check DMA data 931130.4.g
#define RX_END_TAG					0x0badbeef


//====================================
// Internal variable for module
//====================================
struct wb35_rx {
	u32			ByteReceived;// For calculating throughput of BulkIn
	atomic_t		RxFireCounter;// Does Wb35Rx module fire?

	u8	RxBuffer[ MAX_USB_RX_BUFFER_NUMBER ][ ((MAX_USB_RX_BUFFER+3) & ~0x03 ) ];
	u16	RxBufferSize[ ((MAX_USB_RX_BUFFER_NUMBER+1) & ~0x01) ];
	u8	RxOwner[ ((MAX_USB_RX_BUFFER_NUMBER+3) & ~0x03 ) ];//Ownership of buffer  0: SW 1:HW

	u32	RxProcessIndex;//The next index to process
	u32	RxBufferId;
	u32	EP3vm_state;

	u32	rx_halt; // For VM stopping

	u16	MoreDataSize;
	u16	PacketSize;

	u32	CurrentRxBufferId; // For complete routine usage
	u32	Rx3UrbCancel;

	u32	LastR1; // For RSSI reporting
	struct urb *				RxUrb;
	u32		Ep3ErrorCount2; // 20060625.1 Usbd for Rx DMA error count

	int		EP3VM_status;
	u8 *	pDRx;
};
